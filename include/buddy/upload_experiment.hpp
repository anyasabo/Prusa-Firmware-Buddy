#pragma once
// EXPERIMENT (upload-experiments branch, not for upstream as is): runtime
// switches for the upload fixes, so one flash can A/B all of them without
// touching the printer. An upload whose file name contains "upx-" sets them for
// that and later uploads (until the next "upx-" upload or a reboot), e.g.
// "upx-q8-a0-w0-f0-s0-e0-foo.gcode" is the stock behaviour:
//
//   qN  nhttp upload queue cap, 1..17 (8 = stock, 17 = #5591 fix)
//   aN  a1 = fix A (inbound ooseq caps 3 KB / 3 segs), a0 = stock (512 B / 1)
//   wN  w1 = prepare the upload (name check + file create) on the async job
//       worker instead of the tcpip thread; w0 = stock (synchronous)
//   fN  f1 = check the file name in memory + one stat, f0 = stock probe file
//       (create, rename, delete)
//   sN  s1 = lock-free "is the drive still the same" check before each USB
//       write, s0 = stock lseek poke (takes the FatFS lock on tcpip thread)
//   eN  e1 = answer "Expect: 100-continue" with "100 Continue", e0 = ignore it
//   dN  d1 = finish the upload (sync, close, mkdir, rename) on the async job
//       worker instead of the tcpip thread; d0 = stock
//   uN  USB write size in sectors for files created from now on (8 = stock,
//       16, 32; 2 buffers of that size, falls back to 8 if the heap is short)
//   mN  TCP MSS the printer advertises in new connections (1024 = stock,
//       up to 1460); takes effect from the next connection
//   nN  n1 = look up the 8.3 name of the finished upload on the async job
//       worker (with d1), so announcing it (wui_uploaded_gcode) doesn't scan
//       the directory on the tcpip thread; n0 = stock
//   pN  p1 = after an upload without print-after-upload, ask Marlin to show
//       the preview without waiting for it to take the request (it is best
//       effort anyway); p0 = stock (the tcpip thread waits for Marlin, which
//       scans the directory once more for the long name)
//   kN  how long the USB MSC worker sleeps waiting for a USB transfer
//       notification before looking again, ms (500 = stock, 1..500); for all
//       USB I/O, not just uploads
//
// Unknown letters stop the parsing. Defaults at boot: everything on, u8, m1024,
// k500 (stock). Applied settings are reported as the "upx" metric (k in "upx_k");
// per-upload timings as the "upx_t", "upx_f", "upx_u" and "upx_n" metrics (see
// record_timing), USB I/O per second as "usb_io" (see report_usb_io).
#include <cstddef>
#include <cstdint>
#include <printers.h>

namespace upload_experiment {

inline size_t queue_cap = 17;
inline bool ooseq_fix_a = true;
inline bool async_prepare = true;
inline bool name_check_in_memory = true;
inline bool lockfree_drive_check = true;
inline bool answer_expect_continue = true;
inline bool finish_async = true;
inline uint16_t advertised_mss = 1024;
inline bool sfn_on_worker = true;
inline bool preview_no_wait = true;
#if PRINTER_IS_PRUSA_MINI()
inline size_t usb_sectors_per_write = 1; // Low on RAM on mini
#else
inline size_t usb_sectors_per_write = 8;
#endif

/// Parses the switches from an upload path. Runs on the tcpip thread (nhttp),
/// like the ooseq hooks and the transfer slot that read them.
void apply_from_path(const char *path);

/// Per-upload timings in ms, recorded as the "upx_t" metric at the end of the
/// upload:
///   chk  file name check
///   cre  temp file creation + preallocation (PartialFile::create)
///   rdy  from the start of the request handling until the transfer slot could
///        write the first byte (includes chk + cre + queueing to the worker)
///   fin  the end of the upload (sync, close, rename) on the tcpip thread
///   q    time the prepare/finish jobs waited for the async job worker
/// Keys in the metric: c = chk, k = cre, r = rdy, f = fin, q (each capped at
/// 99999 to fit the 47 character limit of custom metric values).
///
/// "upx_f" (the finish steps, ms; on the worker with d1, else the tcpip thread):
///   m  make_dirs, y  sync (flush the last buffers), c  close, r  stat + rename
///   (try_rename), s  8.3 name lookup on the worker (n1, else 0)
/// "upx_u" (USB I/O done by the USB MSC worker while a step ran, all tasks):
///   p  ms during the preparation (check + create), o  its requests,
///   f  ms during the finish, x  its requests,
///   w  USB MSC notification waits that timed out during the whole upload
/// "upx_n" (done() on the tcpip thread):
///   s  8.3 name lookup there (get_SFN_path in wui_start_print; 0 with n1),
///   n  the whole uploaded_notify (includes s and the Marlin request),
///   l  1 if the preview request didn't wait for Marlin (p1 applied)
struct UsbSnapshot {
    uint32_t requests = 0;
    uint32_t busy_ms = 0;
    uint32_t wait_timeouts = 0;
    static UsbSnapshot now();
};

struct Timing {
    uint32_t start_ms = 0;
    uint32_t queue_ms = 0; ///< waiting for the (shared) async job worker, prepare + finish
    uint32_t chk_ms = 0;
    uint32_t cre_ms = 0;
    uint32_t rdy_ms = 0;
    uint32_t fin_ms = 0;
    bool async = false;
    // upx_f
    uint32_t mkdir_ms = 0;
    uint32_t sync_ms = 0;
    uint32_t close_ms = 0;
    uint32_t rename_ms = 0;
    uint32_t sfn_worker_ms = 0;
    // upx_u
    uint32_t prep_usb_ms = 0;
    uint32_t prep_usb_req = 0;
    uint32_t fin_usb_ms = 0;
    uint32_t fin_usb_req = 0;
    UsbSnapshot usb_at_start;
    // upx_n
    uint32_t sfn_tcpip_ms = 0;
    uint32_t notify_ms = 0;
    bool notify_no_wait = false;
};

/// Set by GcodeUpload's done() (tcpip thread) around uploaded_notify when the
/// name it passes is already the 8.3 path (n1) and/or the preview request
/// should not wait for Marlin (p1). Read by wui_start_print on the same thread.
struct NotifyHint {
    bool name_is_sfn = false;
    bool no_wait = false;
};
inline NotifyHint notify_hint;
inline Timing timing;
void record_timing();

/// How long one HTTP server step took (tcpip thread), reported per second as
/// the "http_blk" metric: m = longest step in ms, h/g = the handler (index into
/// nhttp::handler::ConnectionState) before/after that step, n = steps >= 10 ms.
/// h = 90: transfer slot step (writing an upload / download), 91: its done().
void record_http_step(uint32_t ms, uint8_t handler_before, uint8_t handler_after);

/// Reports the "usb_io" metric when about a second passed since the last
/// report and there was USB I/O: r reads, w writes, t ms busy, m longest
/// request (ms), o notification wait timeouts, all since the last report.
/// Called from the tcpip thread (buddy_tcpip_busy_record), because the USB
/// worker task has too little stack to format metrics.
void report_usb_io();

} // namespace upload_experiment

namespace ooseq_limit {
/// EXPERIMENT: switch the inbound ooseq caps between stock and fix A
/// (defined in ooseq_limit.cpp, runs on the tcpip thread).
void set_inbound_fix_a(bool on);
} // namespace ooseq_limit
