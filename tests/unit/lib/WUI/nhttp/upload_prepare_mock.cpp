// Test double for UploadPrepare (the real one runs on the AsyncJob worker).
// Runs the preparation right away, or, with upload_prepare_mock_defer set,
// only when the test calls upload_prepare_mock_run().
#include "upload_prepare_mock.h"

#include <nhttp/upload_prepare.h>

namespace {
nhttp::printer::UploadPrepare::Fn pending_fn = nullptr;
void *pending_arg = nullptr;
bool cancel_flag = false;
} // namespace

bool upload_prepare_mock_defer = false;

bool upload_prepare_mock_pending() {
    return pending_fn != nullptr;
}

void upload_prepare_mock_run() {
    auto fn = pending_fn;
    pending_fn = nullptr;
    if (fn) {
        fn(pending_arg);
    }
}

namespace nhttp::printer {

bool UploadPrepare::busy() const {
    return pending_fn != nullptr;
}

void UploadPrepare::start(Fn fn, void *arg) {
    cancel_flag = false;
    pending_fn = fn;
    pending_arg = arg;
    if (!upload_prepare_mock_defer) {
        upload_prepare_mock_run();
    }
}

bool UploadPrepare::cancel() {
    cancel_flag = true;
    const bool dequeued = pending_fn != nullptr;
    pending_fn = nullptr;
    return dequeued;
}

bool UploadPrepare::cancelled() const {
    return cancel_flag;
}

bool UploadPrepare::publish(Fn publish, void *arg) {
    if (cancel_flag) {
        return false;
    }
    publish(arg);
    return true;
}

} // namespace nhttp::printer
