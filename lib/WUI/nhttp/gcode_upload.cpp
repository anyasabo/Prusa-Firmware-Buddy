#include <buddy/upload_experiment.hpp>
#include "gcode_upload.h"
#include "server.h"
#include "upload_prepare.h"
#include "handler.h"
#include "splice.h"
#include "../../src/common/filename_type.hpp"
#include "../wui_api.h"

#include <common/stat_retry.hpp>
#include <lfn.h>
#include <lwip/tcpip.h>
#include <path_utils.h>
#include <timing.h>
#include <transfers/files.hpp>
#include <transfers/changed_path.hpp>

#include <sys/stat.h>
#include <atomic>
#include <cstring>
#include <span>
#include <unistd.h>
#include <bsod/bsod.h>
#include <logging/log.hpp>

LOG_COMPONENT_REF(transfers);

extern "C" {

// Inject for tests, which are compiled on systems without it in the header.
size_t strlcpy(char *, const char *, size_t);
}

namespace nhttp::printer {

using handler::Continue;
using handler::RequestParser;
using handler::StatusPage;
using handler::Step;
using http::Status;
using splice::Result;
using std::array;
using std::make_tuple;
using std::move;
using std::nullopt;
using std::optional;
using std::string_view;
using std::tuple;
using transfers::ChangedPath;
using transfers::CHECK_FILENAME;
using transfers::Monitor;
using transfers::next_transfer_idx;
using transfers::PartialFile;
using transfers::transfer_name;
using transfers::USB_MOUNT_POINT;
using transfers::USB_MOUNT_POINT_LENGTH;

using Type = ChangedPath::Type;
using Incident = ChangedPath::Incident;

GcodeUpload::GcodeUpload(PutParams &&uploader, Monitor::Slot &&slot, bool json_errors, size_t length, size_t upload_idx, PartialFile::Ptr &&file, UploadedNotify *uploaded)
    : upload(move(uploader))
    , monitor_slot(move(slot))
    , uploaded_notify(uploaded)
    , size_rest(length)
    , json_errors(json_errors)
    , cleanup_temp_file(true)
    , tmp_upload_file(move(file))
    , file_idx(upload_idx)
    , filename_checked(false) {
}

GcodeUpload::GcodeUpload(GcodeUpload &&other)
    : upload(move(other.upload))
    , monitor_slot(move(other.monitor_slot))
    , uploaded_notify(other.uploaded_notify)
    , size_rest(other.size_rest)
    , json_errors(other.json_errors)
    , cleanup_temp_file(other.cleanup_temp_file)
    , tmp_upload_file(move(other.tmp_upload_file))
    , file_idx(other.file_idx)
    , filename_checked(other.filename_checked)
    , send_continue(other.send_continue)
    , async_prepare(other.async_prepare) {
    // The ownership of the temp file is passed to the new instance.
    other.cleanup_temp_file = false;
}

GcodeUpload &GcodeUpload::operator=(GcodeUpload &&other) {
    upload = move(other.upload);
    uploaded_notify = other.uploaded_notify;
    size_rest = other.size_rest;
    json_errors = other.json_errors;

    cleanup_temp_file = other.cleanup_temp_file;
    // The ownership of the temp file is passed to the new instance.
    other.cleanup_temp_file = false;

    tmp_upload_file = move(other.tmp_upload_file);
    file_idx = other.file_idx;
    filename_checked = other.filename_checked;
    send_continue = other.send_continue;
    async_prepare = other.async_prepare;

    return *this;
}

GcodeUpload::~GcodeUpload() {
    if (cleanup_temp_file) {
        // This is area is entered in the last instance in a move chain. Note
        // that even in that case, the tmp_upload_file may be null (already
        // closed, before attempt to rename it) and the temp file to remove not
        // exist (because it got moved).
        //
        // This code is mostly responsible for cleanup during failure cases
        // (the success case being the above).
        //
        // We still perform the attempt to remove it even in case the file is
        // already closed (and therefore nullptr) because there _is_ a failure
        // case where we closed the file before renaming and failed to rename.
        // Even in that case we want to remove the temp file.
        tmp_upload_file.reset();
        const auto fname = transfer_name(file_idx);
        remove(fname.begin());
    } else {
        // Leftover open file in moved object :-O
        debug_assert(tmp_upload_file.get() == nullptr);
    }
}

GcodeUpload::UploadResult GcodeUpload::start(const RequestParser &parser, UploadedNotify *uploaded, bool json_errors, PutParams &&uploadParams) {
    // Note: authentication already checked by the caller.
    // Note: We return errors with connection-close because we don't know if the client sent part of the data.

    // One day we may want to support chunked and connection-close, but we are not there yet.
    if (!parser.content_length.has_value()) {
        return StatusPage(Status::LengthRequired, StatusPage::CloseHandling::ErrorClose, json_errors);
    }

    if (parser.content_length.value() == 0) {
        // Somehow, we never get our ::step called later on due to that. We
        // could hack around it, but supporting 0-sized files seems like a
        // niche thing to spend the time on. At least report it doesn't work
        // instead of getting into some weird transfer-stuck state that never
        // goes away.
        return StatusPage(Status::NotImplemented, parser, "0-sized files aren't implemented");
    }

    const char *path = uploadParams.filepath.data();
    auto slot = Monitor::instance.allocate(Monitor::Type::Link, path, *parser.content_length, parser.print_after_upload);
    if (!slot.has_value()) {
        // FIXME: Is this the right status to return? Change would need to be
        //  defined in the API spec first.
        return StatusPage(Status::Conflict, parser, "Another transfer in progress");
    }
    // EXPERIMENT: only once accepted, so a rejected upload doesn't change the
    // switches under a running one.
    upload_experiment::apply_from_path(path);

    const size_t file_idx = next_transfer_idx();
    const auto fname = transfer_name(file_idx);
    // HTTP/1.0 clients don't know 100 Continue (RFC 9110, 15.2).
    const bool send_continue = parser.expect_continue && parser.http_1_1_or_newer() && upload_experiment::answer_expect_continue;

    upload_experiment::timing = {};
    upload_experiment::timing.start_ms = ticks_ms();
    upload_experiment::timing.usb_at_start = upload_experiment::UsbSnapshot::now();
    upload_experiment::timing.async = upload_experiment::async_prepare;

    if (upload_experiment::async_prepare) {
        // Don't touch the USB drive from the tcpip thread; the file gets
        // created on the async job worker once the body starts (see step()).
        GcodeUpload upload(move(uploadParams), move(*slot), json_errors, *parser.content_length, file_idx, PartialFile::Ptr(), uploaded);
        upload.send_continue = send_continue;
        upload.async_prepare = true;
        // Nothing to clean up yet (and the job owns the temp file from then on).
        upload.cleanup_temp_file = false;
        return upload;
    }

    const uint32_t create_start = ticks_ms();
    auto preallocated = PartialFile::create(fname.begin(), *parser.content_length);
    upload_experiment::timing.cre_ms = ticks_ms() - create_start;
    if (const char **err = get_if<const char *>(&preallocated); err != nullptr) {
        log_error(transfers, "create %s: %s", fname.begin(), *err);
        return StatusPage(Status::InsufficientStorage, StatusPage::CloseHandling::ErrorClose, json_errors, nullopt, *err);
    } else {
        GcodeUpload upload(move(uploadParams), move(*slot), json_errors, *parser.content_length, file_idx, move(get<PartialFile::Ptr>(preallocated)), uploaded);
        upload.send_continue = send_continue;
        return upload;
    }
}

namespace {

    // EXPERIMENT: times consecutive steps and counts the USB requests each one
    // made (all tasks; see upx_f / upx_g).
    struct StepClock {
        uint32_t t = ticks_ms();
        uint32_t req = upload_experiment::UsbSnapshot::now().requests;
        void lap(uint32_t &ms, uint32_t &requests) {
            const uint32_t now = ticks_ms();
            const uint32_t r = upload_experiment::UsbSnapshot::now().requests;
            ms = now - t;
            requests = r - req;
            t = now;
            req = r;
        }
    };

    GcodeUpload::Result prepend_usb_path(string_view filename, char *filepath_out, size_t filepath_size) {
        if ((filename.length() + USB_MOUNT_POINT_LENGTH) >= filepath_size) {
            // The Request header fields too large is a bit of a stretch...
            return make_tuple(Status::RequestHeaderFieldsTooLarge, "File name too long");
        }
        strlcpy(filepath_out, USB_MOUNT_POINT, USB_MOUNT_POINT_LENGTH + 1);
        strncat(filepath_out, filename.data(), filename.length());
        filepath_out[USB_MOUNT_POINT_LENGTH + filename.length()] = '\0';

        return make_tuple(Status::Ok, nullptr);
    }

    std::variant<bool, GcodeUpload::Result> file_dir_exists(string_view path) {
        size_t pos = path.find_last_of('/');
        if (pos == path.npos) {
            return true;
        }

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wvla" // TODO: person who knows a reasonable buffer size should refactor this code to not use variable length array
        char last_dir[pos + USB_MOUNT_POINT_LENGTH + 1];
#pragma GCC diagnostic pop
        auto error = prepend_usb_path(path.substr(0, pos), last_dir, sizeof(last_dir));
        if (std::get<0>(error) != Status::Ok) {
            return error;
        }
        struct stat st;
        if (stat(last_dir, &st) == 0) {
            if (S_ISDIR(st.st_mode)) {
                return true;
            } else {
                return make_tuple(Status::Conflict, "Conflicting upload path");
            }
        }
        return false;
    }

    template <class F>
    GcodeUpload::Result try_rename(const char *src, const char *dest_fname, bool overwrite, F f) {
        char fn[filename_defs::filename_buffer_size];

        auto error = prepend_usb_path(dest_fname, fn, sizeof(fn));
        if (std::get<0>(error) != Status::Ok) {
            return error;
        }

        struct stat st = {};
        // EXPERIMENT (r1): without overwrite, the rename itself refuses an
        // existing target (FatFs FR_EXIST -> EEXIST), so the stat (a directory
        // scan) is only needed to find a file to replace.
        const bool skip_stat = !overwrite && upload_experiment::rename_without_stat;
        int stat_result = skip_stat ? -1 : stat_retry(fn, &st);
        if (stat_result == 0 && S_ISREG(st.st_mode) && overwrite) {
            int result = remove(fn);
            if (result == -1) {
                remove(src);
                switch (errno) {
                case EBUSY:
                    return make_tuple(Status::Conflict, "File is busy");
                case EISDIR:
                    // Shouldn't happen due to st_mode already checked?
                    return make_tuple(Status::Conflict, "Is a directory or ongoing transfer");
                default:
                    return make_tuple(Status::InternalServerError, "Unknown error");
                }
            }
        } else if (stat_result == 0 && S_ISDIR(st.st_mode) && overwrite) {
            return make_tuple(Status::Conflict, "Is a directory or ongoing transfer");
        } else if (stat_result == 0) {
            remove(src);
            return make_tuple(Status::Conflict, "File already exists");
        }

        int result = rename(src, fn);
        if (result != 0) {
            // Backup errno before calling other things
            int old_errno = errno;
            log_error(transfers, "rename %s -> %s: errno %d", src, fn, old_errno);
            remove(src); // No check - no way to handle errors anyway.
            switch (old_errno) {
            case EEXIST:
                // Only reachable with r1 (otherwise the stat caught it).
                return make_tuple(Status::Conflict, "File already exists");
            case EISDIR:
                return make_tuple(Status::Conflict, "Is a directory or ongoing transfer");
            case ENOTDIR:
                return make_tuple(Status::NotFound, "Destination directory does not exist");
            default:
                // we already checked for existence of the file, so we guess
                // it is weird file name/forbidden chars that contain (422 Unprocessable Entity).
                return make_tuple(Status::UnprocessableEntity, "Filename contains invalid characters");
            }
        } else {
            return f(std::span(fn));
        }
    }

    // Is this well-formed UTF-8 (what FatFs's tchar2uni accepts with
    // FF_LFN_UNICODE == 2)? No overlongs, surrogates or code points past U+10FFFF.
    bool valid_utf8(string_view s) {
        size_t i = 0;
        while (i < s.size()) {
            const unsigned char c = static_cast<unsigned char>(s[i]);
            size_t len;
            uint32_t cp;
            if (c < 0x80) {
                i++;
                continue;
            } else if ((c & 0xe0) == 0xc0) {
                len = 2;
                cp = c & 0x1f;
            } else if ((c & 0xf0) == 0xe0) {
                len = 3;
                cp = c & 0x0f;
            } else if ((c & 0xf8) == 0xf0) {
                len = 4;
                cp = c & 0x07;
            } else {
                return false;
            }
            if (i + len > s.size()) {
                return false;
            }
            for (size_t k = 1; k < len; k++) {
                const unsigned char cc = static_cast<unsigned char>(s[i + k]);
                if ((cc & 0xc0) != 0x80) {
                    return false;
                }
                cp = (cp << 6) | (cc & 0x3f);
            }
            static constexpr uint32_t min_for_len[] = { 0, 0, 0x80, 0x800, 0x10000 };
            if (cp < min_for_len[len] || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff)) {
                return false;
            }
            i += len;
        }
        return true;
    }

    // Would FatFs accept this as a (long) file or directory name? Mirrors
    // create_name() in ff.c for LFN builds: no control characters, none of
    // "*:<>?| and DEL; trailing dots and spaces are dropped (so a component
    // made only of those is invalid). Anything exotic this misses still fails
    // at the final rename, with the same 422 the probe would have given.
    bool name_component_valid(string_view component) {
        if (component.size() > filename_defs::max_filename_length) {
            return false;
        }
        for (const char c : component) {
            const unsigned char uc = static_cast<unsigned char>(c);
            if (uc < 0x20 || uc == 0x7f || strchr("\"*:<>?|", c) != nullptr) {
                return false;
            }
        }
        return component.find_last_not_of(". ") != string_view::npos;
    }

    bool name_valid(const char *relative_path) {
        string_view rest(relative_path);
        if (!valid_utf8(rest)) {
            return false;
        }
        bool has_name = false;
        while (!rest.empty()) {
            // FatFs takes both / and \ as separators and skips repeated ones.
            const size_t sep = rest.find_first_of("/\\");
            const string_view component = rest.substr(0, sep);
            if (!component.empty()) {
                if (!name_component_valid(component)) {
                    return false;
                }
                has_name = true;
            }
            if (sep == string_view::npos) {
                break;
            }
            rest.remove_prefix(sep + 1);
        }
        return has_name;
    }

    // Checks the target of an upload (relative to /usb/) before receiving the
    // body. May touch the USB drive, so it runs on the async job worker if the
    // upload is prepared asynchronously.
    //
    // in_memory: validate the name in memory and check for an existing file with
    // one stat, instead of the probe (create, rename to the target, delete). The
    // probe costs several directory writes (each LFN create/rename also scans
    // the directory for a free 8.3 alias) and gives the same answers, apart from
    // names FatFs would still reject for reasons name_valid() doesn't model;
    // those fail at the end of the upload, like names in not yet existing
    // directories always did (see below).
    GcodeUpload::Result check_target_name(const char *filename, bool overwrite, bool in_memory) {
        // Note: If the directory we want to upload to doesn't exist,
        // we assume the filename is OK. If it is not OK for any reason
        // (weird chars in filename or whatever), we just fail at the end of the download
        // which is still fine. Alternativelly we would have to create all those dirs
        // and then delete them, we don't want to do that for simplicity and performance.
        if (auto result = file_dir_exists(filename); holds_alternative<bool>(result)) {
            if (!get<bool>(result)) {
                if (in_memory && !name_valid(filename)) {
                    return make_tuple(Status::UnprocessableEntity, "Filename contains invalid characters");
                }
                return make_tuple(Status::Ok, nullptr);
            }
        } else {
            return get<GcodeUpload::Result>(result);
        }

        if (overwrite) {
            char filepath[filename_defs::filename_buffer_size];
            auto error = prepend_usb_path(filename, filepath, sizeof(filepath));
            if (std::get<0>(error) != Status::Ok) {
                return error;
            }

            if (wui_is_file_being_printed(filepath)) {
                return make_tuple(Status::Conflict, "File is busy");
            } else {
                return make_tuple(Status::Ok, nullptr);
            }
        }

        if (in_memory) {
            if (!name_valid(filename)) {
                return make_tuple(Status::UnprocessableEntity, "Filename contains invalid characters");
            }
            char filepath[filename_defs::filename_buffer_size];
            auto error = prepend_usb_path(filename, filepath, sizeof(filepath));
            if (std::get<0>(error) != Status::Ok) {
                return error;
            }
            struct stat st = {};
            if (stat_retry(filepath, &st) == 0) {
                if (S_ISDIR(st.st_mode)) {
                    return make_tuple(Status::Conflict, "Is a directory or ongoing transfer");
                }
                return make_tuple(Status::Conflict, "File already exists");
            }
            return make_tuple(Status::Ok, nullptr);
        }

        // We try to create and then delete the file. This places an empty file
        // there for a really short while, but it's there anyway ‒ something could
        // detect it. Any better way to check if the file name is fine?
        FILE *tmp = fopen(CHECK_FILENAME, "wb");
        if (!tmp) {
            return make_tuple(Status::InternalServerError, "Failed to create a check temp file");
        }
        fclose(tmp);
        return try_rename(CHECK_FILENAME, filename, false, [](std::span<const char> fname) -> GcodeUpload::Result {
            remove(fname.data());
            return make_tuple(Status::Ok, nullptr);
        });
    }

    class PutTransfer final : public splice::Transfer {
    public:
        GcodeUpload::UploadedNotify *uploaded_notify = nullptr;
        PartialFile::Ptr f;
        array<char, filename_defs::path_buffer_size> filepath;
        bool print_after_upload;
        bool overwrite;
        size_t file_idx;

        // Asynchronous preparation (name check + temp file creation) on the
        // async job worker, see start_prepare().
        //
        // The inputs (filepath, overwrite, file_idx, prep_*) are written in the
        // tcpip thread before the job starts and must not change until it's no
        // longer busy() (checked before a new upload touches them). The results
        // (prepared_*) are written by the job inside prepare_job.publish(),
        // which is serialized with cancel(), before prepare_done is set
        // (release); the tcpip thread reads them after seeing it (acquire).
        UploadPrepare prepare_job;
        bool preparing = false; // tcpip thread only
        uint32_t prepare_start_ms = 0; // tcpip thread only
        std::atomic<bool> prepare_done = false;
        bool prep_in_memory = false;
        size_t prep_size = 0;
        PartialFile::Ptr prepared_file;
        GcodeUpload::Result prepared_error { Status::Ok, nullptr };

        /// Giving up on a preparation the worker doesn't get to in this time
        /// (it's shared, eg. with gcode prefetching and key generation); the
        /// client gets a 503 rather than waiting in a closed window forever.
        static constexpr uint32_t PREPARE_TIMEOUT_MS = 30'000;

        /// Is a previous preparation still running (it may still use the inputs)?
        bool prepare_busy() const {
            return prepare_job.busy();
        }

        void start_prepare(size_t size, bool in_memory) {
            prepared_file.reset();
            prepared_error = { Status::Ok, nullptr };
            prep_size = size;
            prep_in_memory = in_memory;
            preparing = true;
            prepare_start_ms = ticks_ms();
            prepare_done = false;
            prepare_job.start(prepare_routine, this);
        }

        // Runs on the async job worker.
        static void prepare_routine(void *arg) {
            static_cast<PutTransfer *>(arg)->prepare();
        }

        // Results of a prepare() on the way to publish (worker stack).
        struct Prepared {
            PutTransfer *self;
            PartialFile::Ptr file;
            GcodeUpload::Result error;
            uint32_t queue_ms;
            uint32_t check_ms;
            uint32_t create_ms;
            uint32_t usb_ms = 0;
            uint32_t usb_req = 0;
            uint32_t check_req = 0;
            uint32_t lock_ms = 0;
        };

        static void publish_prepared(void *arg) {
            auto *p = static_cast<Prepared *>(arg);
            p->self->prepared_file = move(p->file);
            p->self->prepared_error = p->error;
            // Only a published (current) job's timings count.
            upload_experiment::timing.queue_ms += p->queue_ms;
            upload_experiment::timing.chk_ms = p->check_ms;
            upload_experiment::timing.cre_ms = p->create_ms;
            upload_experiment::timing.prep_usb_ms = p->usb_ms;
            upload_experiment::timing.prep_usb_req = p->usb_req;
            upload_experiment::timing.check_usb_req = p->check_req;
            upload_experiment::timing.prep_lock_ms = p->lock_ms;
            p->self->prepare_done.store(true, std::memory_order_release);
        }

        void prepare() {
            // remove the "/usb/" prefix
            const char *filename = filepath.data() + USB_MOUNT_POINT_LENGTH;
            const uint32_t check_start = ticks_ms();
            const auto usb_start = upload_experiment::UsbSnapshot::now();
            // How long the job waited for the (shared) worker.
            const uint32_t queue_ms = check_start - prepare_start_ms;
            Prepared prepared { this, nullptr, check_target_name(filename, overwrite, prep_in_memory), queue_ms, 0, 0 };
            prepared.check_ms = ticks_ms() - check_start;
            prepared.check_req = upload_experiment::UsbSnapshot::now().requests - usb_start.requests;

            const auto fname = transfer_name(file_idx);
            if (std::get<0>(prepared.error) == Status::Ok && !prepare_job.cancelled()) {
                const uint32_t create_start = ticks_ms();
                auto preallocated = PartialFile::create(fname.begin(), prep_size);
                prepared.create_ms = ticks_ms() - create_start;
                if (const char **err = get_if<const char *>(&preallocated); err != nullptr) {
                    log_error(transfers, "create %s: %s", fname.begin(), *err);
                    prepared.error = make_tuple(Status::InsufficientStorage, *err);
                } else {
                    prepared.file = move(get<PartialFile::Ptr>(preallocated));
                }
            }

            const auto usb_end = upload_experiment::UsbSnapshot::now();
            prepared.usb_ms = usb_end.busy_ms - usb_start.busy_ms;
            prepared.usb_req = usb_end.requests - usb_start.requests;
            prepared.lock_ms = usb_end.lock_wait_ms - usb_start.lock_wait_ms;

            const bool had_file = prepared.file != nullptr;
            if (!prepare_job.publish(publish_prepared, &prepared)) {
                // Given up on meanwhile; nobody will pick it up, clean up here
                // (on the worker, not the tcpip thread).
                if (had_file) {
                    prepared.file.reset();
                    remove(fname.begin());
                }
                return;
            }
            // Wake the transfer slot (if this gets lost because the tcpip
            // mailbox is full, its poll callback picks it up within a second).
            tcpip_try_callback(prepare_finished, this);
        }

        static void prepare_finished(void *arg) {
            auto *self = static_cast<PutTransfer *>(arg);
            if (self->server != nullptr && (self->preparing || self->finishing)) {
                self->server->kick_transfer();
            }
        }

        virtual Readiness poll_ready() override {
            if (!preparing) {
                return f ? Readiness::Ready : Readiness::Failed;
            }
            if (!prepare_done.load(std::memory_order_acquire)) {
                if (ticks_diff(ticks_ms(), prepare_start_ms) > static_cast<int32_t>(PREPARE_TIMEOUT_MS)) {
                    log_error(transfers, "upload preparation timed out");
                    abort_prepare();
                    prepared_error = make_tuple(Status::ServiceTemporarilyUnavailable, "Storage busy, try again");
                    result = Result::CantWrite;
                    return Readiness::Failed;
                }
                return Readiness::Waiting;
            }
            preparing = false;
            upload_experiment::timing.rdy_ms = ticks_ms() - upload_experiment::timing.start_ms;
            if (std::get<0>(prepared_error) != Status::Ok) {
                result = Result::CantWrite;
                return Readiness::Failed;
            }
            f = move(prepared_file);
            return Readiness::Ready;
        }

        virtual void abort_prepare() override {
            if (finishing) {
                cancel_finish();
                finished_async = false;
                return;
            }
            if (!preparing) {
                return;
            }
            preparing = false;
            // If the job hasn't published yet, it won't (it cleans up its own
            // file then). Otherwise the result is ours to dispose of: closing and
            // removing a preallocated file is a FatFs operation on the tcpip
            // thread, but only for an upload abandoned in the short moment
            // between the preparation finishing and the transfer slot noticing.
            prepare_job.cancel();
            if (prepare_done.load(std::memory_order_acquire) && prepared_file) {
                prepared_file.reset();
                const auto fname = transfer_name(file_idx);
                remove(fname.begin());
            }
            // The monitor slot records the failure when released (like the
            // other failure paths).
        }

        // Asynchronous finishing (sync, close, mkdir, rename) on the same worker,
        // see start_finish(). From start_finish() on, the file belongs to the
        // job (finishing_file; file() is null); the results (finish_error,
        // final_name) are published like the preparation's.
        bool finishing = false; // tcpip thread only
        bool finished_async = false; // tcpip thread only: done() uses the results
        bool sfn_requested = false; // input of finish(), written before the job starts
        uint32_t finish_start_ms = 0; // tcpip thread only
        std::atomic<bool> finish_done = false;
        PartialFile::Ptr finishing_file;
        GcodeUpload::Result finish_error { Status::Ok, nullptr };
        std::array<char, filename_defs::filename_buffer_size> final_name {};
        std::array<char, filename_defs::path_buffer_size> final_sfn {}; ///< n1: 8.3 path of final_name, else empty

        virtual bool start_finish() override {
            if (!upload_experiment::finish_async || f == nullptr || prepare_job.busy()) {
                return false;
            }
            finishing_file = move(f);
            finishing = true;
            finished_async = true;
            finish_done = false;
            finish_error = { Status::Ok, nullptr };
            final_name[0] = '\0';
            final_sfn[0] = '\0';
            sfn_requested = upload_experiment::sfn_on_worker;
            finish_start_ms = ticks_ms();
            prepare_job.start(finish_routine, this);
            return true;
        }

        static void finish_routine(void *arg) {
            static_cast<PutTransfer *>(arg)->finish();
        }

        struct Finished {
            PutTransfer *self;
            GcodeUpload::Result error;
            std::array<char, filename_defs::filename_buffer_size> name;
            uint32_t queue_ms;
            /// The 8.3 path of name (n1), empty if not looked up.
            std::array<char, filename_defs::path_buffer_size> sfn {};
            uint32_t mkdir_ms = 0;
            uint32_t sync_ms = 0;
            uint32_t close_ms = 0;
            uint32_t rename_ms = 0;
            uint32_t sfn_ms = 0;
            uint32_t usb_ms = 0;
            uint32_t usb_req = 0;
            uint32_t reads = 0;
            uint32_t writes = 0;
            uint32_t lock_ms = 0;
            uint32_t mkdir_req = 0;
            uint32_t sync_req = 0;
            uint32_t close_req = 0;
            uint32_t rename_req = 0;
            uint32_t sfn_req = 0;
        };

        static void publish_finished(void *arg) {
            auto *p = static_cast<Finished *>(arg);
            p->self->finish_error = p->error;
            p->self->final_name = p->name;
            p->self->final_sfn = p->sfn;
            upload_experiment::timing.queue_ms += p->queue_ms;
            upload_experiment::timing.mkdir_ms = p->mkdir_ms;
            upload_experiment::timing.sync_ms = p->sync_ms;
            upload_experiment::timing.close_ms = p->close_ms;
            upload_experiment::timing.rename_ms = p->rename_ms;
            upload_experiment::timing.sfn_worker_ms = p->sfn_ms;
            upload_experiment::timing.fin_usb_ms = p->usb_ms;
            upload_experiment::timing.fin_usb_req = p->usb_req;
            upload_experiment::timing.fin_reads = p->reads;
            upload_experiment::timing.fin_writes = p->writes;
            upload_experiment::timing.fin_lock_ms = p->lock_ms;
            upload_experiment::timing.mkdir_req = p->mkdir_req;
            upload_experiment::timing.sync_req = p->sync_req;
            upload_experiment::timing.close_req = p->close_req;
            upload_experiment::timing.rename_req = p->rename_req;
            upload_experiment::timing.sfn_req = p->sfn_req;
            p->self->finish_done.store(true, std::memory_order_release);
        }

        // Runs on the async job worker: what done() does on the success path,
        // minus the parts that talk to the rest of the printer (monitor,
        // changed path, print after upload), which stay in done().
        void finish() {
            const auto fname = transfer_name(file_idx);
            Finished finished { this, { Status::Ok, nullptr }, {}, ticks_ms() - finish_start_ms };
            const auto usb_start = upload_experiment::UsbSnapshot::now();
            StepClock clock;
            // create directories if uploading points to non existing one
            if (!make_dirs(filepath.data())) {
                finished.error = make_tuple(Status::InternalServerError, "Failed to create a folder");
                clock.lap(finished.mkdir_ms, finished.mkdir_req);
            } else {
                clock.lap(finished.mkdir_ms, finished.mkdir_req);
                if (!finishing_file->sync()) {
                    finished.error = { Status::InsufficientStorage, "Couldn't sync to file" };
                }
                clock.lap(finished.sync_ms, finished.sync_req);
            }
            // Close the file first, otherwise it can't be moved
            finishing_file.reset();
            clock.lap(finished.close_ms, finished.close_req);
            if (std::get<0>(finished.error) == Status::Ok && prepare_job.cancelled()) {
                // The connection is gone; like the synchronous path (done()
                // with Timeout), don't keep the upload.
                finished.error = { Status::RequestTimeout, "Connection timeout" };
            }
            if (std::get<0>(finished.error) == Status::Ok) {
                // Remove the "/usb/" prefix
                const char *final_filename = filepath.data() + USB_MOUNT_POINT_LENGTH;
                finished.error = try_rename(fname.begin(), final_filename, overwrite, [&](std::span<char> filename) -> GcodeUpload::Result {
                    strlcpy(finished.name.data(), filename.data(), finished.name.size());
                    return make_tuple(Status::Ok, nullptr);
                });
                clock.lap(finished.rename_ms, finished.rename_req);
                // (try_rename removes the temp file when it fails.)
                if (std::get<0>(finished.error) == Status::Ok && sfn_requested && !prepare_job.cancelled()) {
                    // n1: what wui_start_print would do on the tcpip thread
                    // (a readdir of the whole directory), here.
                    strlcpy(finished.sfn.data(), finished.name.data(), finished.sfn.size());
                    get_SFN_path(std::span(finished.sfn));
                    clock.lap(finished.sfn_ms, finished.sfn_req);
                }
            } else {
                remove(fname.begin());
            }
            const auto usb_end = upload_experiment::UsbSnapshot::now();
            finished.usb_ms = usb_end.busy_ms - usb_start.busy_ms;
            finished.usb_req = usb_end.requests - usb_start.requests;
            finished.reads = usb_end.reads - usb_start.reads;
            finished.writes = usb_end.writes - usb_start.writes;
            finished.lock_ms = usb_end.lock_wait_ms - usb_start.lock_wait_ms;
            if (!prepare_job.publish(publish_finished, &finished)) {
                // Nobody waits for the result. If the rename made it, the
                // complete upload stays; at least tell the file list about it
                // (ChangedPath is thread safe), but don't start a print.
                if (std::get<0>(finished.error) == Status::Ok) {
                    ChangedPath::instance.changed_path(finished.name.data(), Type::File, Incident::Created);
                }
                return;
            }
            tcpip_try_callback(prepare_finished, this);
        }

        // Give up on finishing. A running job notices (cancelled()) and drops
        // the upload, or, if already past that, keeps it. One that never ran
        // leaves the file to us (closing and removing it here is FatFs on the
        // tcpip thread, but only on this rare path).
        void cancel_finish() {
            finishing = false;
            if (prepare_job.cancel()) {
                finishing_file.reset();
                const auto fname = transfer_name(file_idx);
                remove(fname.begin());
            }
        }

        virtual Readiness poll_finished() override {
            if (!finishing) {
                return Readiness::Ready;
            }
            if (!finish_done.load(std::memory_order_acquire)) {
                if (ticks_diff(ticks_ms(), finish_start_ms) > static_cast<int32_t>(PREPARE_TIMEOUT_MS)) {
                    log_error(transfers, "upload finishing timed out");
                    cancel_finish();
                    finish_error = make_tuple(Status::ServiceTemporarilyUnavailable, "Storage busy, try again");
                    return Readiness::Ready;
                }
                return Readiness::Waiting;
            }
            finishing = false;
            upload_experiment::timing.fin_ms = ticks_ms() - finish_start_ms;
            return Readiness::Ready;
        }

        /// Failures to prepare go out as errors that drain the rest of the
        /// body (see Server::TransferSlot::make_response).
        virtual bool json_errors() const override {
            return json;
        }
        bool json = false;

        virtual PartialFile *file() const override {
            return f.get();
        }
        // EXPERIMENT: uploaded_notify with the n/p hints and the upx_n timing.
        // sfn: the 8.3 path looked up on the worker (n1), or empty.
        bool notify_uploaded(std::span<char> name, std::span<char> sfn) {
            const uint32_t start = ticks_ms();
            upload_experiment::notify_hint = {};
            upload_experiment::notify_hint.active = true;
            upload_experiment::notify_hint.name_is_sfn = !sfn.empty();
            upload_experiment::notify_hint.no_wait = upload_experiment::preview_no_wait && !print_after_upload;
            upload_experiment::notify_hint.skip_preview = !upload_experiment::preview_after_upload && !print_after_upload;
            const bool ok = uploaded_notify(sfn.empty() ? name : sfn, print_after_upload);
            upload_experiment::notify_hint = {};
            upload_experiment::timing.notify_ms = ticks_ms() - start;
            return ok;
        }

        // TODO: alias for the type, probably unify with the GcodeUpload::Result
        virtual optional<tuple<http::Status, const char *>> done() override {
            debug_assert(monitor_slot.has_value());
            const uint32_t finish_start = ticks_ms();
            if (finished_async) {
                // start_finish() did the file work; announce the result.
                finished_async = false;
                const GcodeUpload::Result error = finish_error;
                GcodeUpload::Result response = error;
                if (std::get<0>(error) == Status::Ok) {
                    monitor_slot->done(Monitor::Outcome::Finished);
                    ChangedPath::instance.changed_path(final_name.data(), Type::File, Incident::Created);
                    if (uploaded_notify != nullptr && !notify_uploaded(std::span(final_name), final_sfn[0] != '\0' ? std::span(final_sfn) : std::span<char>())) {
                        response = make_tuple(Status::Conflict, "Can't print right now");
                    }
                }
                upload_experiment::record_timing();
                return response;
            }
            if (f == nullptr) {
                // The preparation failed (poll_ready), there's no file and
                // nothing to clean up.
                debug_assert(std::get<0>(prepared_error) != Status::Ok);
                upload_experiment::record_timing();
                return prepared_error;
            }
            bool cleanup_temp_file = true;
            const auto fname = transfer_name(file_idx);
            tuple<http::Status, const char *> error { Status::InternalServerError, "Unknown error" };
            switch (result) {
            case Result::Ok: {
                const auto usb_start = upload_experiment::UsbSnapshot::now();
                StepClock clock;
                // create directories if uploading points to non existing one
                if (!make_dirs(filepath.data())) {
                    error = make_tuple(Status::InternalServerError, "Failed to create a folder");
                    goto CLEANUP;
                }
                clock.lap(upload_experiment::timing.mkdir_ms, upload_experiment::timing.mkdir_req);
                if (!f->sync()) {
                    error = { Status::InsufficientStorage, "Couldn't sync to file" };
                    goto CLEANUP;
                }
                clock.lap(upload_experiment::timing.sync_ms, upload_experiment::timing.sync_req);

                // Close the file first, otherwise it can't be moved
                f.reset();
                clock.lap(upload_experiment::timing.close_ms, upload_experiment::timing.close_req);
                // Remove the "/usb/" prefix
                // FIXME: Why? Relict of old / Post / multipart upload?
                const char *final_filename = filepath.data() + USB_MOUNT_POINT_LENGTH;
                error = try_rename(fname.begin(), final_filename, overwrite, [&](std::span<char> filename) -> GcodeUpload::Result {
                    clock.lap(upload_experiment::timing.rename_ms, upload_experiment::timing.rename_req);
                    // (fin_usb_* include the notify's I/O here, d0 only.)
                    monitor_slot->done(Monitor::Outcome::Finished);
                    ChangedPath::instance.changed_path(filename.data(), Type::File, Incident::Created);
                    cleanup_temp_file = false;
                    if (uploaded_notify != nullptr) {
                        if (notify_uploaded(filename, std::span<char>())) {
                            return make_tuple(Status::Ok, nullptr);
                        } else {
                            return make_tuple(Status::Conflict, "Can't print right now");
                        }
                    } else {
                        return make_tuple(Status::Ok, nullptr);
                    }
                });
                const auto usb_end = upload_experiment::UsbSnapshot::now();
                upload_experiment::timing.fin_usb_ms = usb_end.busy_ms - usb_start.busy_ms;
                upload_experiment::timing.fin_usb_req = usb_end.requests - usb_start.requests;
                upload_experiment::timing.fin_reads = usb_end.reads - usb_start.reads;
                upload_experiment::timing.fin_writes = usb_end.writes - usb_start.writes;
                upload_experiment::timing.fin_lock_ms = usb_end.lock_wait_ms - usb_start.lock_wait_ms;
                break;
            }
            case Result::CantWrite:
                error = { Status::InsufficientStorage, "USB write error or USB full" };
                break;
            case Result::Stopped:
                error = { Status::ServiceTemporarilyUnavailable, "Upload stopped from connect" };
                break;
            case Result::ClosedByClient:
                error = { Status::BadRequest, "Connection closed by client" };
                break;
            case Result::Timeout:
                error = { Status::RequestTimeout, "Connection timeout" };
                break;
            }
        CLEANUP:
            f.reset();
            if (cleanup_temp_file) {
                remove(fname.begin());
            }
            upload_experiment::timing.fin_ms = ticks_ms() - finish_start;
            upload_experiment::record_timing();
            return error;
        }

        virtual tuple<size_t, size_t> write(const uint8_t *in, size_t in_size, uint8_t *out, size_t out_size) override {
            const size_t write_size = std::min(in_size, out_size);
            memcpy(out, in, write_size);
            return make_tuple(write_size, write_size);
        }

        virtual bool progress(size_t len) override {
            debug_assert(monitor_slot.has_value());
            monitor_slot->progress(len);
            return !monitor_slot->is_stopped();
        }
    };

    // TODO: A better place to have this? Or dynamic allocation? Share with the connect uploader?
    PutTransfer put_transfer;
} // namespace

void GcodeUpload::step(string_view input, bool terminated_by_client, uint8_t *output, size_t output_size, Step &out) {
    if (terminated_by_client && size_rest > 0) {
        out = Step { 0, 0, StatusPage(Status::BadRequest, StatusPage::CloseHandling::ErrorClose, json_errors, nullopt, "Truncated body") };
        return;
    }

    const size_t read = std::min(input.size(), size_rest);
    if (monitor_slot.is_stopped()) {
        monitor_slot.done(Monitor::Outcome::Stopped);
        out = Step { 0, 0, StatusPage(Status::ServiceTemporarilyUnavailable, StatusPage::CloseHandling::ErrorClose, json_errors, nullopt, "Upload stopped from connect") };
        return;
    }

    // remove the "/usb/" prefix
    const char *filename = upload.filepath.data() + USB_MOUNT_POINT_LENGTH;

    // The checks we can do before the body (and before a 100 Continue, so a
    // client waiting for it gets the final status instead, RFC 9110 15.2.1).
    if (!filename_checked) {
        if (!filename_is_printable(filename)) {
            out = Step { read, 0, StatusPage(Status::UnsupportedMediaType, StatusPage::CloseHandling::ErrorClose, json_errors, nullopt, "Not a GCODE") };
            return;
        }
        if (put_transfer.prepare_busy()) {
            // A previous, abandoned upload's preparation is still running (it
            // uses put_transfer's inputs). Rare; the client can retry shortly.
            out = Step { read, 0, StatusPage(Status::Conflict, StatusPage::CloseHandling::ErrorClose, json_errors, nullopt, "Another transfer in progress") };
            return;
        }
        if (!async_prepare) {
            const uint32_t check_start = ticks_ms();
            const auto usb_check_start = upload_experiment::UsbSnapshot::now();
            auto filename_error = check_target_name(filename, upload.overwrite, upload_experiment::name_check_in_memory);
            upload_experiment::timing.chk_ms = ticks_ms() - check_start;
            upload_experiment::timing.check_usb_req = upload_experiment::UsbSnapshot::now().requests - usb_check_start.requests;
            if (std::get<0>(filename_error) != Status::Ok) {
                out = Step { read, 0, StatusPage(std::get<0>(filename_error), StatusPage::CloseHandling::ErrorClose, json_errors, nullopt, std::get<1>(filename_error)) };
                return;
            }
            upload_experiment::timing.rdy_ms = ticks_ms() - upload_experiment::timing.start_ms;
            const auto usb = upload_experiment::UsbSnapshot::now();
            upload_experiment::timing.prep_usb_ms = usb.busy_ms - upload_experiment::timing.usb_at_start.busy_ms;
            upload_experiment::timing.prep_usb_req = usb.requests - upload_experiment::timing.usb_at_start.requests;
            upload_experiment::timing.prep_lock_ms = usb.lock_wait_ms - upload_experiment::timing.usb_at_start.lock_wait_ms;
        }
        filename_checked = true;
    }

    if (send_continue) {
        // RFC 9110, 10.1.1: the client waits (curl: 1 s) for this before
        // sending the body. The checks that need the drive (with
        // async_prepare) happen while the body flows; TCP holds it back.
        send_continue = false;
        static constexpr string_view continue_response = "HTTP/1.1 100 Continue\r\n\r\n";
        if (output != nullptr && output_size >= continue_response.size() && input.empty()) {
            memcpy(output, continue_response.data(), continue_response.size());
            out = Step { 0, continue_response.size(), handler::Continue() };
            return;
        }
        // The body already started (or no buffer); the client didn't wait,
        // and a late 100 would only confuse it.
    }

    debug_assert(put_transfer.f == nullptr); // No other transfer is happening at the moment.
    put_transfer.f = move(tmp_upload_file);
    tmp_upload_file.reset(); // Does move really set it to nullptr, or is it just a copy?
    put_transfer.set_monitor_slot(move(monitor_slot));
    put_transfer.uploaded_notify = uploaded_notify;
    put_transfer.filepath = upload.filepath;
    put_transfer.print_after_upload = upload.print_after_upload;
    put_transfer.overwrite = upload.overwrite;
    put_transfer.file_idx = file_idx;
    put_transfer.json = json_errors;
    put_transfer.preparing = false;
    // Set again by Server::inject_transfer; until then a finished preparation
    // has nobody to wake (the transfer slot polls it on its first step).
    put_transfer.server = nullptr;
    if (async_prepare) {
        put_transfer.start_prepare(size_rest, upload_experiment::name_check_in_memory);
    }
    cleanup_temp_file = false;
    out = Step { 0, 0, make_tuple(&put_transfer, size_rest) };
}

} // namespace nhttp::printer
