/**
 * \file
 *
 * Runs the preparation of an upload (name check, temp file creation) on the
 * async job worker, so the tcpip thread never waits on the USB drive for it.
 *
 * A thin wrapper around AsyncJob, in its own translation unit so the nhttp code
 * (and its host tests, which stub the job system) don't depend on its headers.
 */
#pragma once

namespace nhttp::printer {

class UploadPrepare {
public:
    using Fn = void (*)(void *arg);

    /// Is a preparation queued or running and not yet published? Must be false
    /// before start(). Also true for a cancelled one that is still running (it
    /// may still read its inputs). After a successful publish() it's false (fn
    /// doesn't touch the inputs any more).
    bool busy() const;

    /// Queue fn(arg) on the async job worker. Must not be busy().
    void start(Fn fn, void *arg);

    /// Give up on the current preparation (issuing thread). If still queued, it
    /// won't run, and cancel() returns true (whatever fn would have cleaned up
    /// is the caller's then); if running, cancelled() turns true and publish()
    /// refuses.
    bool cancel();

    /// For fn: has the preparation been given up on meanwhile?
    bool cancelled() const;

    /// For fn: run publish(arg) (storing the results) atomically with respect
    /// to cancel(). Returns false, without running it, if cancelled; then fn
    /// owns (and must clean up) whatever it produced.
    bool publish(Fn publish, void *arg);
};

} // namespace nhttp::printer
