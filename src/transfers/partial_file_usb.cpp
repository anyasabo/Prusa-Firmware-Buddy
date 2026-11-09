#include "partial_file_usb.hpp"

#include <buddy/fatfs.h>
#include <buddy/filesystem_fatfs.h>
#include <common/unique_file_ptr.hpp>
#include <common/bsod.h>
#include <logging/log.hpp>

#include <algorithm>
#include <common/heap.h>
#include <cstdlib>
#include <buddy/upload_experiment.hpp>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>

LOG_COMPONENT_REF(transfers);
using namespace transfers;
using std::holds_alternative;
using std::make_tuple;
using std::variant;

static_assert(UsbPartialFile::SECTOR_SIZE == FF_MAX_SS);
static_assert(UsbPartialFile::SECTOR_SIZE == FF_MIN_SS);

UsbPartialFile::UsbPartialFile(UsbhMscRequest::LunNbr lun, UsbhMscRequest::SectorNbr first_sector, State state, int file_lock, void *fatfs, uint16_t mount_id)
    : PartialFile(state)
    , sector_pool(lun, usb_msc_write_finished_callback, this, upload_experiment::usb_sectors_per_write)
    , write_error(false)
    , first_sector_nbr(first_sector)
    , current_sector(nullptr)
    , current_offset(0)
    , file_lock(file_lock)
    , fatfs(fatfs)
    , mount_id(mount_id) {
}

UsbPartialFile::~UsbPartialFile() {
    // the current sector may contain incomplete content, so we must avoid overwriting potentially valid data
    discard_current_sector();
    // synchronization is required due to the validity of callback pointers
    sector_pool.sync(0, true);
    if (file_lock != -1) {
        close(file_lock);
    }
}

static PartialFile::Result build(const char *path, unique_file_ptr file, PartialFile::State state) {
    FIL *fatfs_file = filesystem_fastfs_get_underlying_struct(file.get());
    if (!fatfs_file) {
        return "Failed to open file";
    }

    // check file contiguity
    int is_contiguous = 0;
    auto result = fatfs_test_contiguous_file(fatfs_file, &is_contiguous);
    if (result != FR_OK) {
        return "Failed to check file contiguity";
    }
    if (!is_contiguous) {
        return "File is not contiguous";
    }

    state.total_size = f_size(fatfs_file);

    // For the drive_unchanged() check.
    FATFS *volume = fatfs_file->obj.fs;
    const uint16_t mount_id = fatfs_file->obj.id;

    // get first sector
    auto drive = fatfs_file->obj.fs->pdrv;
    auto lba = fatfs_file->obj.fs->database + fatfs_file->obj.fs->csize * (fatfs_file->obj.sclust - 2);

    // We want to keep a *read only* file open for our lifetime to prevent
    // someone from deleting it (and us then writing into sectors no longer
    // allocated for the file and other funny things).
    //
    // For that we first have to *close* the read-write/write file to get it
    // (and we want only a file descriptor, not FILE *). Yes, there's short a
    // race condition there when someone could delete the file and create a new
    // one with the same name but different sectors between we close & open,
    // but it's still better than not having the file lock at all.
    file.reset();
    int fd = ::open(path, O_RDONLY);
    if (fd == -1) {
        return "Can't lock file in place";
    }

    return std::make_shared<UsbPartialFile>(drive, lba, state, fd, volume, mount_id);
}

PartialFile::Result UsbPartialFile::create(const char *path, unique_file_ptr file, size_t size) {
    // we want to allocate contiguous space on the drive
    // so lets get a bit dirty and go one level lower
    FIL *fatfs_file = filesystem_fastfs_get_underlying_struct(file.get());
    if (!fatfs_file) {
        file.reset();
        remove(path);
        return "Failed to prepare file for writing";
    }

    // expand the file size
    auto alloc_result = f_expand(fatfs_file, size, /*allocate_now=*/1, /*yield=*/1);
    if (alloc_result != FR_OK) {
        file.reset();
        remove(path);
        return "USB drive full";
    }

    return build(path, std::move(file), State());
}

PartialFile::Result UsbPartialFile::open(const char *path, unique_file_ptr file, State state) {
    return build(path, std::move(file), state);
}

UsbhMscRequest::SectorNbr UsbPartialFile::get_sector_nbr(size_t offset) {
    auto sector = first_sector_nbr + offset / FF_MAX_SS;
    // total_size won't change, don't have to lock
    if (offset >= state.total_size) {
        sector += 1;
    }
    return sector;
}

size_t UsbPartialFile::get_offset(UsbhMscRequest::SectorNbr sector_nbr) {
    return (sector_nbr - first_sector_nbr) * SECTOR_SIZE;
}

bool UsbPartialFile::drive_unchanged() const {
    // The USB host unmounts on disconnect (f_mount(0) in usb_host.cpp, which
    // clears fs_type) and the next drive gets mounted lazily, which bumps id. So
    // an unplugged drive fails the first test and a different (or re-plugged)
    // one the second, without asking the drive or taking any lock. Both are
    // single aligned loads (no tearing on Cortex-M), read through volatile so
    // they are re-read every time. A stale "unchanged" answer is possible only
    // in the window between this check and the write, same as with the lseek
    // poke this replaces.
    const volatile FATFS *volume = static_cast<const volatile FATFS *>(fatfs);
    return volume->fs_type != 0 && volume->id == mount_id;
}

bool UsbPartialFile::write_current_sector() {
    debug_assert(current_sector != nullptr);
    log_debug(transfers, "Sending sector over USB %" PRIu32 " (%.20s)", current_sector->sector_nbr, current_sector->data);
    if (upload_experiment::lockfree_drive_check) {
        if (!drive_unchanged()) {
            return false;
        }
    } else if (lseek(file_lock, 0, SEEK_SET) == -1) {
        // Safety measure. It is possible that between creation of this
        // PartialFile and current call to write_sector, the USB got unplugged
        // and some other got plugged in. This would have severe effects on the
        // filesystem, as we bypass the filesystem here and just send the data
        // to specific offset.
        //
        // The "usual" file descriptors are already hooked up to this mechanism
        // to protect them (hopefully), so we simply abuse that mechanism by
        // "poking" the file descriptor for this given file. We use lseek as
        // hopefully cheap way to "poke" it. We use the 'rewind' mode, the
        // 'ftell' mode has a shortcut in it and actually does _not_ check the
        // validity of the file.
        //
        // FIXME: This actually can block! It probably doesn't "do" anything
        // with the USB itself, but acqires some lock and it takes a long time
        // to do so.
        return false;
    }
    const uint32_t slot_idx = reinterpret_cast<uint32_t>(current_sector->callback_param2);
    auto start = get_offset(current_sector->sector_nbr);
    auto end = current_offset;
    auto len = end - start;
    // Round up to full sectors
    current_sector->count = (len + SECTOR_SIZE - 1) / SECTOR_SIZE;
    // Synchronized through release-acquire pair through the queue into USB thread
    future_extend[slot_idx] = ValidPart { start, end };
    // This _in theory_ can block, which we don't like. But, as the queue for
    // the requests is currently 5 long, we can put at most 2 there, there can
    // be other 3 threads doing something with the USB at the same time and
    // still not block... unlikely.
    //
    // So we don't complicate the things and keep it this way (at worst, we
    // will stall tcpip thread for a short bit).
    auto result = usbh_msc_submit_request(current_sector);
    if (result != USBH_OK) {
        return false;
    }
    return true;
}

bool UsbPartialFile::seek(size_t offset) {
    auto new_sector = get_sector_nbr(offset);

    if (current_sector && current_sector->sector_nbr == new_sector) {
        current_offset = offset;
        return true;
    }

    if (current_sector && current_sector->sector_nbr != new_sector) {
        log_warning(transfers, "Discarding buffered data for sector %" PRIu32, current_sector->sector_nbr);
    }

    current_offset = offset;
    discard_current_sector();
    return true;
}

void UsbPartialFile::discard_current_sector() {
    if (current_sector) {
        sector_pool.release(reinterpret_cast<uint32_t>(current_sector->callback_param2));
        current_sector = nullptr;
    }
}

UsbPartialFile::BufferPeek UsbPartialFile::get_current_buffer(bool block_waiting) {
    if (!current_sector) {
        if (current_offset >= state.total_size) {
            return OutOfRange {};
        }
        const auto sector_nbr = get_sector_nbr(current_offset);

        current_sector = sector_pool.acquire(block_waiting);
        if (current_sector == nullptr) {
            if (block_waiting) {
                return WriteError {};
            } else {
                return WouldBlock {};
            }
        }
        current_sector->sector_nbr = sector_nbr;
    }

    const size_t buffer_offset = current_offset - get_offset(current_sector->sector_nbr);
    return BufferAndSizes {
        current_sector->data,
        buffer_offset,
        allowed_write_size(),
    };
}

size_t UsbPartialFile::allowed_write_size() const {
    return allowed_sectors() * SECTOR_SIZE;
}

size_t UsbPartialFile::allowed_sectors() const {
    const size_t sectors_per_write = sector_pool.sectors_per_buffer;
    return sectors_per_write - (current_sector->sector_nbr % sectors_per_write);
}

bool UsbPartialFile::advance_written(size_t by) {
    debug_assert(current_sector);
    const auto next_offset = current_offset + by;
    const auto next_sector_nbr = get_sector_nbr(next_offset);
    if (next_offset > state.total_size) {
        fatal_error("Request to write past the end of file.", "transfers");
    }
    if (next_sector_nbr < current_sector->sector_nbr || next_sector_nbr >= current_sector->sector_nbr + allowed_sectors()) {
        current_offset = next_offset;
        // TODO: We may need some non-blocking way?
        if (write_current_sector()) {
            current_sector = nullptr;
        } else {
            return false;
        }
    }

    current_offset = next_offset;
    log_progress();
    return true;
}

bool UsbPartialFile::write(const uint8_t *data, size_t size) {
    if (write_error) {
        return false;
    }
    while (size) {
        auto buffer = get_current_buffer(true);

        if (holds_alternative<WouldBlock>(buffer)) {
            // We ask for blocking mode
            debug_assert(0);
            return false;
        } else if (holds_alternative<WriteError>(buffer)) {
            return false;
        } else if (holds_alternative<OutOfRange>(buffer)) {
            log_error(transfers, "Write past end of file attempted");
            return false;
        }
        // else -> we got a buffer
        debug_assert(holds_alternative<BufferAndSizes>(buffer));
        const auto buff = get<BufferAndSizes>(buffer);

        const size_t buffer_remaining = buff.size - buff.offset;
        const size_t write_size = std::min(size, buffer_remaining);
        debug_assert(buffer_remaining > 0);
        debug_assert(write_size > 0);
        memcpy(buff.buffer + buff.offset, data, write_size);

        if (!advance_written(write_size)) {
            return false;
        }

        data += write_size;
        size -= write_size;
    }

    return true;
}

bool UsbPartialFile::sync() {
    uint32_t sync_avoid = 0;
    if (current_sector) {
        sync_avoid = 1;
        auto copied_sector = sector_pool.acquire(true);
        if (!copied_sector) {
            return false;
        }
        memcpy(copied_sector->data, current_sector->data, sector_pool.sectors_per_buffer * SECTOR_SIZE);
        copied_sector->sector_nbr = current_sector->sector_nbr;
        auto status = write_current_sector();
        if (status) {
            current_sector = copied_sector;
        } else {
            sector_pool.release(reinterpret_cast<uint32_t>(copied_sector->callback_param2));
            log_error(transfers, "Failed to write sector");
            return false;
        }
    }
    if (!sector_pool.sync(sync_avoid, false)) {
        return false;
    }
    return !write_error;
}

UsbPartialFile::SectorPool::SectorPool(UsbhMscRequest::LunNbr lun, UsbhMscRequestCallback callback, void *callback_param, size_t sectors_per_buffer)
    : semaphore { size, size }
    , slot_mask(~0u << size)
    , sectors_per_buffer(sectors_per_buffer) {
    // EXPERIMENT: the buffer size is a runtime choice (upload_experiment::
    // usb_sectors_per_write); bigger ones may not fit the heap. Plain new /
    // malloc red-screen on failure (heap.cpp's malloc failed hook), so take the
    // big ones with malloc_fallible, and only with headroom left for everyone
    // else; otherwise fall back to the stock size (allocated as before).
    static constexpr size_t heap_headroom = 16 * 1024;
    std::array<uint8_t *, size> buffers {};
    bool ok = sectors_per_buffer > SECTORS_PER_WRITE
        && heap_bytes_remaining() > size * sectors_per_buffer * SECTOR_SIZE + heap_headroom;
    for (unsigned i = 0; ok && i < size; ++i) {
        buffers[i] = static_cast<uint8_t *>(malloc_fallible(sectors_per_buffer * SECTOR_SIZE));
        ok = buffers[i] != nullptr;
    }
    if (!ok) {
        for (auto &b : buffers) {
            free(b);
            b = nullptr;
        }
        this->sectors_per_buffer = SECTORS_PER_WRITE;
        for (unsigned i = 0; i < size; ++i) {
            buffers[i] = static_cast<uint8_t *>(malloc(BUFFER_SIZE));
        }
    }
    for (unsigned i = 0; i < size; ++i) {
        pool[i] = {
            UsbhMscRequest::UsbhMscRequestOperation::Write,
            lun,
            1,
            0,
            buffers[i],
            USBH_FAIL,
            callback,
            callback_param,
            reinterpret_cast<void *>(i)
        };
    }
}

UsbPartialFile::SectorPool::~SectorPool() {
    sync(0, true);
    for (unsigned i = 0; i < size; ++i) {
        free(pool[i].data);
    }
}

UsbhMscRequest *UsbPartialFile::SectorPool::acquire(bool block_waiting) {
    if (!semaphore.try_acquire_for(block_waiting ? USBH_MSC_RW_MAX_DELAY_MS : 0)) {
        return nullptr;
    }

    mutex.lock();
    auto slot = get_available_slot();
    slot_mask |= 1 << slot;
    mutex.unlock();

    memset(pool[slot].data, 0, sectors_per_buffer * SECTOR_SIZE);
    return pool + slot;
}

void UsbPartialFile::SectorPool::release(uint32_t slot) {
    mutex.lock();
    slot_mask &= ~(1 << slot);
    semaphore.release();
    mutex.unlock();
}

bool UsbPartialFile::SectorPool::sync(uint32_t avoid, bool force) {
    debug_assert(avoid <= size);

    // We flush the whole queue by blocking all the slots for ourselves before
    // returning them back. When we have them all, none can be in flight.
    uint32_t block = size - avoid;
    uint32_t acquired = 0; // How many were actually acquired (in case of timeouts, it can be less)
    for (uint32_t i = 0; i < block; i++) {
        if (force) {
            semaphore.acquire();
            acquired++;
        } else {
            if (semaphore.try_acquire_for(USBH_MSC_RW_MAX_DELAY_MS)) {
                acquired++;
            } else {
                break;
            }
        }
    }
    for (uint32_t i = 0; i < acquired; i++) {
        semaphore.release();
    }

    return block == acquired;
}

void UsbPartialFile::release_file() {
    // Flush everything so we won't try writing through this later.
    discard_current_sector();
    sector_pool.sync(0, true);
    // Prevent any future attempts to write here.
    write_error = true;
    // And release the lock.
    close(file_lock);
    file_lock = -1;
}

uint32_t UsbPartialFile::SectorPool::get_available_slot() const {
    debug_assert(is_available_slot());
    return std::countr_one(slot_mask);
}

void UsbPartialFile::usbh_msc_finished(USBH_StatusTypeDef result, uint32_t slot) {
    if (result == USBH_OK && !write_error) {
        // Still safe, slot can't be reused before release below, so one can't take this until then.
        extend_valid_part(future_extend[slot]);
    } else {
        log_error(transfers, "Failed to write sector");
        write_error = true;
    }
    sector_pool.release(slot);

    // Call the callback. Make sure this is _after_ we released the slot, so it
    // can be reused from there.
    notify_written();
}

void UsbPartialFile::usb_msc_write_finished_callback(USBH_StatusTypeDef result, void *param1, void *param2) {
    reinterpret_cast<UsbPartialFile *>(param1)->usbh_msc_finished(result, reinterpret_cast<uint32_t>(param2));
}
