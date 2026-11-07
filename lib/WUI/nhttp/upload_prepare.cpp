#include "upload_prepare.h"

#include <async_job/async_job.hpp>
#include <freertos/mutex.hpp>

#include <atomic>
#include <cstdint>
#include <mutex>

namespace nhttp::printer {

namespace {
    AsyncJob job;
    // From start() until fn published, returned, or the job got cancelled
    // before it ran. AsyncJob's own state can't tell: a discarded job stops
    // counting as active while it may still be running.
    std::atomic<bool> in_flight = false;
    // Serializes cancel(), publish() and the end of a run.
    freertos::Mutex mutex;
    // All guarded by mutex.
    bool cancel_requested = false;
    bool published = false;
    // Which start() the running fn belongs to (a previous run's tail may still
    // be finishing when the next one starts).
    uint32_t generation = 0;
    // What the job runs. In statics, not in the lambda: AsyncJob's callback
    // only has room for a pointer or so. The job takes them (and the
    // generation) when it starts; a start() while it's still queued replaces
    // the queued job, so a starting job always belongs to the latest start().
    UploadPrepare::Fn job_fn = nullptr;
    void *job_arg = nullptr;
} // namespace

bool UploadPrepare::busy() const {
    return in_flight.load();
}

void UploadPrepare::start(Fn fn, void *arg) {
    in_flight = true;
    {
        std::lock_guard lock(mutex);
        cancel_requested = false;
        published = false;
        ++generation;
        job_fn = fn;
        job_arg = arg;
    }
    job.issue([](AsyncJobExecutionControl &) {
        UploadPrepare::Fn run_fn;
        void *run_arg;
        uint32_t run_gen;
        {
            std::lock_guard lock(mutex);
            run_fn = job_fn;
            run_arg = job_arg;
            run_gen = generation;
        }
        run_fn(run_arg);
        std::lock_guard lock(mutex);
        if (run_gen == generation && !published) {
            in_flight = false;
        }
    });
}

bool UploadPrepare::cancel() {
    {
        std::lock_guard lock(mutex);
        cancel_requested = true;
    }
    if (job.try_cancel()) {
        // It was still queued and won't run.
        in_flight = false;
        return true;
    }
    return false;
}

bool UploadPrepare::cancelled() const {
    std::lock_guard lock(mutex);
    return cancel_requested;
}

bool UploadPrepare::publish(Fn publish, void *arg) {
    std::lock_guard lock(mutex);
    if (cancel_requested) {
        return false;
    }
    publish(arg);
    published = true;
    in_flight = false;
    return true;
}

} // namespace nhttp::printer
