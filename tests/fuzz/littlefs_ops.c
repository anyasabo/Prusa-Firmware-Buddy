// littlefs with the firmware geometry: random file operations, a model of the expected content,
// and power loss at a random block device write. After a power loss or at the end, the harness
// mounts the filesystem again and compares each file with the model.
//
// The firmware keeps its configuration, the Connect certificate, the E2EE keys and the resources in
// littlefs on the W25X NOR flash (/system), and the files of some printers on the MT29F NAND flash
// (/internal). A power cut can occur at any time, so littlefs must keep each file either in its old
// or in its new state.
//
// Input: byte 0-1 = the block device write that loses power (0 = no power loss), byte 2 = the
// geometry, then operations (one opcode byte and its argument bytes each).
//
// The file includes the littlefs sources, so that LFS_ASSERT can abort also with NDEBUG (a
// Release build): the assertions of littlefs are invariants of the filesystem.

#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LFS_ASSERT(test) \
    do {                 \
        if (!(test)) {   \
            abort();     \
        }                \
    } while (0)
#define LFS_NO_DEBUG
#define LFS_NO_WARN
#define LFS_NO_ERROR
#define LFS_THREADSAFE

#include "lfs.c"
#include "lfs_util.c"
#include "bd/lfs_emubd.c"

#define NFILES    6
#define NDIRFILES 4
#define MAXSIZE   9000 // more than two W25X blocks, so files use the CTZ skip-list

typedef struct {
    int exists;
    int unknown; // a failed operation (no space) leaves the content undefined
    uint32_t size;
    uint8_t data[MAXSIZE];
} model_file_t;

typedef struct {
    model_file_t file[NFILES];
    int dir_exists;
    int dir_file[NDIRFILES];
} model_t;

// The state before and after the operation that runs. After a power loss, each file must match
// one of the two.
static model_t before, after;
static jmp_buf powerloss_jmp;

static lfs_t lfs;
static struct lfs_config cfg;
static lfs_emubd_t bd;
static struct lfs_emubd_config bdcfg;
static uint8_t read_buf[4096], prog_buf[4096], lookahead_buf[16], file_buf[4096];
static uint8_t io_buf[MAXSIZE];

static void powerloss(void *data) {
    (void)data;
    longjmp(powerloss_jmp, 1);
}

static int lock(const struct lfs_config *c) {
    (void)c;
    return 0;
}

static void name_of(char *out, int i) {
    snprintf(out, 16, "f%d", i);
}

static void dir_name_of(char *out, int k) {
    snprintf(out, 16, "d/%d", k);
}

static void fill(uint8_t *buf, uint32_t size, uint32_t seed) {
    for (uint32_t k = 0; k < size; k++) {
        buf[k] = (uint8_t)((seed * 31u + k * 7u + (k >> 8)) ^ (seed >> 3));
    }
}

static struct lfs_file_config file_cfg(void) {
    struct lfs_file_config fc;
    memset(&fc, 0, sizeof(fc));
    fc.buffer = file_buf;
    return fc;
}

// 1 if file i of the filesystem matches f. If allow_empty, an empty file also matches: an open
// with LFS_O_CREAT commits the new file at once, before the data.
static int file_matches(int i, const model_file_t *f, int allow_empty) {
    char name[16];
    name_of(name, i);
    struct lfs_info info;
    int err = lfs_stat(&lfs, name, &info);
    LFS_ASSERT(err == 0 || err == LFS_ERR_NOENT);
    if (f->unknown) {
        return 1;
    }
    if (!f->exists) {
        return err == LFS_ERR_NOENT;
    }
    if (err != 0 || info.type != LFS_TYPE_REG) {
        return 0;
    }
    if (allow_empty && info.size == 0) {
        return 1;
    }
    if (info.size != f->size) {
        return 0;
    }
    lfs_file_t file;
    struct lfs_file_config fc = file_cfg();
    LFS_ASSERT(lfs_file_opencfg(&lfs, &file, name, LFS_O_RDONLY, &fc) == 0);
    lfs_ssize_t n = lfs_file_read(&lfs, &file, io_buf, f->size);
    LFS_ASSERT(lfs_file_close(&lfs, &file) == 0);
    return n == (lfs_ssize_t)f->size && memcmp(io_buf, f->data, f->size) == 0;
}

static model_t resolved;

// Compares the filesystem with the model m. If a is not NULL (a power loss during an operation),
// the files must all match m or all match a: an operation is atomic (for example, a rename must
// not lose the file). The files of the directory are removed one at a time, thus each can match
// m or a. Returns the model that matched, with an empty new file as unknown.
static const model_t *verify(const model_t *m, const model_t *a) {
    int all_m = 1;
    for (int i = 0; i < NFILES; i++) {
        all_m = all_m && file_matches(i, &m->file[i], 0);
    }
    if (all_m) {
        memcpy(&resolved, m, sizeof(resolved));
    } else {
        if (a == NULL) {
            abort(); // a file is not in the state of the model
        }
        memcpy(&resolved, a, sizeof(resolved));
        for (int i = 0; i < NFILES; i++) {
            const int created = a->file[i].exists && !m->file[i].exists;
            if (!file_matches(i, &a->file[i], created)) {
                abort(); // the files are in neither the old nor the new state
            }
            if (created && !file_matches(i, &a->file[i], 0)) {
                resolved.file[i].unknown = 1;
            }
        }
    }
    for (int k = 0; k < NDIRFILES; k++) {
        char name[16];
        dir_name_of(name, k);
        struct lfs_info info;
        int err = lfs_stat(&lfs, name, &info);
        LFS_ASSERT(err == 0 || err == LFS_ERR_NOENT);
        const int exists = err == 0;
        // -1: unknown after a failed operation
        const int in_m = m->dir_file[k] == -1 || exists == m->dir_file[k];
        const int in_a = a != NULL && (a->dir_file[k] == -1 || exists == a->dir_file[k]);
        if (!in_m && !in_a) {
            abort();
        }
        resolved.dir_file[k] = exists;
    }
    return &resolved;
}

static int mount(void) {
    memset(&lfs, 0, sizeof(lfs));
    return lfs_mount(&lfs, &cfg);
}

// Writes the file (truncate or append) from the model "after", which the caller has set.
static int write_file(int i, int append) {
    char name[16];
    name_of(name, i);
    const model_file_t *nf = &after.file[i];
    const model_file_t *of = &before.file[i];
    lfs_file_t file;
    struct lfs_file_config fc = file_cfg();
    int flags = LFS_O_WRONLY | LFS_O_CREAT | (append ? LFS_O_APPEND : LFS_O_TRUNC);
    int err = lfs_file_opencfg(&lfs, &file, name, flags, &fc);
    if (err) {
        return err;
    }
    const uint32_t start = append ? of->size : 0;
    // Several writes, as the firmware writes files in chunks.
    uint32_t pos = start;
    int werr = 0;
    while (pos < nf->size && werr == 0) {
        uint32_t chunk = nf->size - pos;
        if (chunk > 700) {
            chunk = 700;
        }
        lfs_ssize_t n = lfs_file_write(&lfs, &file, nf->data + pos, chunk);
        if (n < 0) {
            werr = (int)n;
        } else {
            LFS_ASSERT(n == (lfs_ssize_t)chunk);
            pos += chunk;
        }
    }
    err = lfs_file_close(&lfs, &file);
    return werr ? werr : err;
}

// Reads a file with seeks between the reads and compares the data with the model.
static void read_seek(int i, const uint8_t *arg) {
    const model_file_t *f = &before.file[i];
    if (!f->exists || f->unknown) {
        return;
    }
    char name[16];
    name_of(name, i);
    lfs_file_t file;
    struct lfs_file_config fc = file_cfg();
    LFS_ASSERT(lfs_file_opencfg(&lfs, &file, name, LFS_O_RDONLY, &fc) == 0);
    for (int r = 0; r < 4; r++) {
        // Positions near the block ends make the seek reuse the file cache.
        uint32_t off = f->size ? ((uint32_t)arg[r] * 61u + (arg[r] & 1 ? 4092u : 0u)) % (f->size + 1) : 0;
        uint32_t len = (uint32_t)arg[r] * 17u % 1500u + 1u;
        if (off + len > f->size) {
            len = f->size - off;
        }
        LFS_ASSERT(lfs_file_seek(&lfs, &file, (lfs_soff_t)off, LFS_SEEK_SET) == (lfs_soff_t)off);
        lfs_ssize_t n = lfs_file_read(&lfs, &file, io_buf, len);
        if (n != (lfs_ssize_t)len || memcmp(io_buf, f->data + off, len) != 0) {
            abort(); // wrong data after a seek
        }
    }
    LFS_ASSERT(lfs_file_close(&lfs, &file) == 0);
}

// Removes all files of the directory while it reads the directory, as the firmware removes the
// E2EE identities.
static int remove_dir_files(void) {
    lfs_dir_t dir;
    int err = lfs_dir_open(&lfs, &dir, "d");
    if (err) {
        return err;
    }
    struct lfs_info info;
    int res;
    while ((res = lfs_dir_read(&lfs, &dir, &info)) > 0) {
        if (info.type != LFS_TYPE_REG) {
            continue;
        }
        char path[16];
        snprintf(path, sizeof(path), "d/%s", info.name);
        err = lfs_remove(&lfs, path);
        if (err) {
            break;
        }
    }
    LFS_ASSERT(lfs_dir_close(&lfs, &dir) == 0);
    return res < 0 ? res : err;
}

static void mark_unknown(void) {
    for (int i = 0; i < NFILES; i++) {
        if (memcmp(&before.file[i], &after.file[i], sizeof(model_file_t)) != 0) {
            after.file[i].unknown = 1;
        }
    }
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size < 3) {
        return 0;
    }
    const uint32_t power_cycles = ((uint32_t)data[0] | ((uint32_t)data[1] << 8)) % 2048u;
    const uint8_t geometry = data[2];
    data += 3;
    size -= 3;

    memset(&cfg, 0, sizeof(cfg));
    cfg.context = &bd;
    cfg.read = lfs_emubd_read;
    cfg.prog = lfs_emubd_prog;
    cfg.erase = lfs_emubd_erase;
    cfg.sync = lfs_emubd_sync;
    cfg.lock = lock;
    cfg.unlock = lock;
    cfg.read_buffer = read_buf;
    cfg.prog_buffer = prog_buf;
    cfg.lookahead_buffer = lookahead_buf;
    cfg.lookahead_size = sizeof(lookahead_buf);
    cfg.block_cycles = (geometry & 1) ? 4 : 500;
    if (geometry & 2) {
        // MT29F NAND (/internal): 4 KiB pages, 256 KiB blocks.
        cfg.read_size = 1;
        cfg.prog_size = 4096;
        cfg.block_size = 256 * 1024;
        cfg.block_count = 8u << ((geometry >> 2) & 3);
        cfg.cache_size = 4096;
        cfg.metadata_max = 32768;
    } else {
        // W25X NOR (/system): byte program, 4 KiB erase blocks. The cache of the MINI is 128 B.
        cfg.read_size = 1;
        cfg.prog_size = 1;
        cfg.block_size = 4096;
        cfg.block_count = 16u << ((geometry >> 2) & 3);
        cfg.cache_size = (geometry & 16) ? 128 : 512;
        cfg.metadata_max = 4096;
    }

    memset(&bdcfg, 0, sizeof(bdcfg));
    bdcfg.read_size = cfg.read_size;
    bdcfg.prog_size = cfg.prog_size;
    bdcfg.erase_size = cfg.block_size;
    bdcfg.erase_count = cfg.block_count;
    bdcfg.erase_value = 0xff;
    bdcfg.powerloss_cb = powerloss;
    LFS_ASSERT(lfs_emubd_create(&cfg, &bdcfg) == 0);

    memset(&before, 0, sizeof(before));
    LFS_ASSERT(lfs_format(&lfs, &cfg) == 0);
    LFS_ASSERT(mount() == 0);
    // Count the block device writes from here: a power loss during the format is not a case.
    lfs_emubd_setpowercycles(&cfg, power_cycles);

    // volatile: longjmp() returns here, and the values must survive it.
    volatile size_t pos = 0;
    volatile int in_op = 0;
    if (setjmp(powerloss_jmp) == 0) {
        while (pos + 4 <= size) {
            const uint8_t *op = data + pos;
            pos += 4;
            const int i = op[1] % NFILES;
            memcpy(&after, &before, sizeof(after));
            in_op = 1;
            int err = 0;
            switch (op[0] % 8) {
            case 0: // write a new file or replace a file
            case 1: { // append
                const int append = op[0] % 8 == 1 && before.file[i].exists && !before.file[i].unknown;
                model_file_t *f = &after.file[i];
                const uint32_t add = ((uint32_t)op[2] | ((uint32_t)op[3] << 8)) % (MAXSIZE / 2);
                const uint32_t start = append ? f->size : 0;
                const uint32_t end = start + add > MAXSIZE ? MAXSIZE : start + add;
                fill(f->data + start, end - start, (uint32_t)op[2] * 131u + op[3] + pos);
                f->size = end;
                f->exists = 1;
                f->unknown = 0;
                err = write_file(i, append);
                break;
            }
            case 2: { // remove
                // The model changes before the call: a power loss at the last write of an
                // operation returns here with the operation done.
                after.file[i].exists = 0;
                after.file[i].unknown = 0;
                char name[16];
                name_of(name, i);
                err = lfs_remove(&lfs, name);
                if (err == LFS_ERR_NOENT && (!before.file[i].exists || before.file[i].unknown)) {
                    err = 0;
                }
                break;
            }
            case 3: { // rename, replaces the target
                const int j = op[2] % NFILES;
                if (i == j || !before.file[i].exists) {
                    break;
                }
                after.file[j] = before.file[i];
                after.file[i].exists = 0;
                after.file[i].unknown = 0;
                char from[16], to[16];
                name_of(from, i);
                name_of(to, j);
                err = lfs_rename(&lfs, from, to);
                if (err == LFS_ERR_NOENT && before.file[i].unknown) {
                    // An earlier failed operation did not create the file.
                    memcpy(&after, &before, sizeof(after));
                    after.file[i].exists = 0;
                    after.file[i].unknown = 0;
                    err = 0;
                }
                break;
            }
            case 4: // read with seeks
                read_seek(i, op);
                break;
            case 5: { // create a file in the directory
                const int k = op[2] % NDIRFILES;
                after.dir_exists = 1;
                after.dir_file[k] = 1;
                if (!before.dir_exists) {
                    err = lfs_mkdir(&lfs, "d");
                    if (err && err != LFS_ERR_EXIST) {
                        break;
                    }
                }
                char name[16];
                dir_name_of(name, k);
                lfs_file_t file;
                struct lfs_file_config fc = file_cfg();
                err = lfs_file_opencfg(&lfs, &file, name, LFS_O_WRONLY | LFS_O_CREAT, &fc);
                if (err == 0) {
                    const lfs_ssize_t n = lfs_file_write(&lfs, &file, "id", 2);
                    const int cerr = lfs_file_close(&lfs, &file);
                    err = n < 0 ? (int)n : cerr;
                }
                break;
            }
            case 6: // remove all files of the directory while it reads it
                if (before.dir_exists) {
                    for (int k = 0; k < NDIRFILES; k++) {
                        after.dir_file[k] = 0;
                    }
                    err = remove_dir_files();
                }
                break;
            case 7: // unmount and mount again
                LFS_ASSERT(lfs_unmount(&lfs) == 0);
                LFS_ASSERT(mount() == 0);
                verify(&before, NULL);
                break;
            }
            // A create with no space gives LFS_ERR_NAMETOOLONG (lfs_file_opencfg maps it). The
            // names of this harness are short, so that error can only mean no space.
            if (err == LFS_ERR_NOSPC || err == LFS_ERR_NAMETOOLONG) {
                // littlefs does not define the content of a file after a failed write.
                mark_unknown();
                for (int k = 0; k < NDIRFILES; k++) {
                    if (after.dir_file[k] != before.dir_file[k]) {
                        after.dir_file[k] = -1;
                    }
                }
            } else if (err) {
                abort(); // an operation failed without a reason
            }
            in_op = 0;
            memcpy(&before, &after, sizeof(before));
        }
        lfs_emubd_setpowercycles(&cfg, 0);
        LFS_ASSERT(lfs_unmount(&lfs) == 0);
    }
    // After a power loss, the files of the operation that ran can be in the old or the new state.
    lfs_emubd_setpowercycles(&cfg, 0);
    LFS_ASSERT(mount() == 0);
    memcpy(&before, verify(&before, in_op ? &after : NULL), sizeof(before));
    // The first write after a mount repairs the state of an interrupted operation (orphans,
    // moves). Write once more and compare again.
    memcpy(&after, &before, sizeof(after));
    model_file_t *f = &after.file[NFILES - 1];
    f->exists = 1;
    f->unknown = 0;
    f->size = 300;
    fill(f->data, f->size, 7);
    const int err = write_file(NFILES - 1, 0);
    if (err == LFS_ERR_NOSPC || err == LFS_ERR_NAMETOOLONG) {
        mark_unknown();
    } else {
        LFS_ASSERT(err == 0);
    }
    LFS_ASSERT(lfs_unmount(&lfs) == 0);
    LFS_ASSERT(mount() == 0);
    verify(&after, NULL);
    LFS_ASSERT(lfs_unmount(&lfs) == 0);
    LFS_ASSERT(lfs_emubd_destroy(&cfg) == 0);
    return 0;
}
