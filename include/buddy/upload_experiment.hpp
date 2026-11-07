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
//   vN  v1 = stock: an upload without print-after-upload opens the print
//       preview (if the printer is idle and no preview is open); v0 = don't.
//       While a preview is open it stats the file every second on the async
//       job worker and further uploads skip print_start, so v0 keeps test
//       uploads independent (and lets the test delete the file right away)
//   rN  r1 = at the end of a non-overwriting upload, rename without the stat
//       of the target first (the rename itself fails with "exists"); r0 = stock
//   hN  h1 = FatFs makes 8.3 aliases from a hash of the long name right away
//       instead of trying NAME~1..NAME~5 first (each try is a full directory
//       scan; names sharing their first 6 characters collide); h0 = stock.
//       For all files created on the USB drive
//   xN  TCP receive window advertised in NEW connections, KB: 8 (stock), 12, 16.
//       For all TCP connections; each keeps the window it started with, so
//       like m it applies from the next connection (set it with one upload,
//       measure with the next). The RX ring has the stock size again (10
//       MTUs, ~11 KB): x12 and x16 advertise more window than the ring backs
//       (x16 on an 18-MTU ring gave nothing, 2026-11-07)
//   oN  cap on the RX ring's usage, KB (0 = no cap, the full ring; minimum 4)
//   yN  y1 = network task and tcpip thread at Normal priority (stock:
//       BelowNormal, under the GUI, USB host, logging, metrics); y0 = stock.
//       While printing, y1 can delay the media prefetch (its job starts at
//       BelowNormal) and starve Connect. Watch the stp_stall metric
//
// Unknown letters stop the parsing. Defaults at boot: everything on, u8, m1024,
// except the ones that change behaviour beyond uploads: k500, v1, h0, x8, o0,
// y0 (stock). q defaults to 33 (fits a 16 KB window of 512 B segments).
// x/o/y are reported as "upx2". Applied settings are reported as the "upx" metric;
// per-upload timings as the "upx_t", "upx_f", "upx_u" and "upx_n" metrics (see
// record_timing), USB I/O per second as "usb_io", "usb_io2", "usb_io3" (see
// report_usb_io).
#include <cstddef>
#include <cstdint>
#include <printers.h>

namespace upload_experiment {

inline size_t queue_cap = 33;
inline bool ooseq_fix_a = true;
inline bool async_prepare = true;
inline bool name_check_in_memory = true;
inline bool lockfree_drive_check = true;
inline bool answer_expect_continue = true;
inline bool finish_async = true;
inline uint16_t advertised_mss = 1024;
inline bool sfn_on_worker = true;
inline bool preview_no_wait = true;
inline bool preview_after_upload = true;
inline bool rename_without_stat = true;
inline uint32_t tcp_wnd_kb = 8;
inline size_t rx_ring_cap = 0; ///< bytes, 0 = no cap (pbuf_rx.cpp)
inline bool net_prio_raised = false;
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
///      (with a small k most of these are just the polling interval passing,
///      so compare w only between runs with the same k)
/// "upx_n" (done() on the tcpip thread):
///   s  8.3 name lookup there (get_SFN_path in wui_start_print; 0 with n1),
///   n  the whole uploaded_notify (includes s and the Marlin request),
///   l  the preview request: 0 not sent (printer not ready, eg. a preview is
///      already open), 1 sent and waited for Marlin (stock), 2 sent without
///      waiting (p1), 3 dropped (p1, Marlin's queue full), 4 skipped (v0),
///      5 print after upload (autostart path, always waits), 9 the notify
///      never ran (failed / cancelled upload)
/// "upx_v": c  USB requests of the name check alone (the rest of upx_u o is
///   the file creation), a / b  reads / writes during the finish,
///   p / f  ms waiting for the FatFs lock (held by another task) during the
///   preparation / finish
/// "upx_g": USB requests per finish step, same keys as upx_f (m, y, c, r, s)
struct UsbSnapshot {
    uint32_t requests = 0;
    uint32_t reads = 0;
    uint32_t writes = 0;
    uint32_t busy_ms = 0;
    uint32_t wait_timeouts = 0;
    uint32_t lock_wait_ms = 0;
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
    uint8_t notify_mode = 9; ///< NotifyMode, notify_none until the notify runs
    // upx_v
    uint32_t check_usb_req = 0;
    uint32_t fin_reads = 0;
    uint32_t fin_writes = 0;
    uint32_t prep_lock_ms = 0;
    uint32_t fin_lock_ms = 0;
    // upx_g
    uint32_t mkdir_req = 0;
    uint32_t sync_req = 0;
    uint32_t close_req = 0;
    uint32_t rename_req = 0;
    uint32_t sfn_req = 0;
};

/// The l values of upx_n.
enum NotifyMode : uint8_t {
    notify_not_sent = 0,
    notify_waited = 1,
    notify_no_wait_sent = 2,
    notify_dropped = 3,
    notify_skipped = 4,
    notify_autostart = 5,
    notify_none = 9, ///< the notify never ran (upload failed or was cancelled)
};

/// Set by GcodeUpload's done() (tcpip thread) around uploaded_notify when the
/// name it passes is already the 8.3 path (n1) and/or the preview request
/// should not wait for Marlin (p1). Read by wui_start_print on the same thread.
struct NotifyHint {
    bool active = false; ///< inside an upload's notify (timings belong to it)
    bool name_is_sfn = false;
    bool no_wait = false;
    bool skip_preview = false; ///< v0 and not print-after-upload
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

/// Every ~5 s from the same place: "heap_x" s = bytes never handed to malloc
/// yet (sbrk headroom; only ever goes down, so it is the low-water mark),
/// f = free inside malloc's pool, o = number of free chunks in the pool,
/// l = largest free chunk in the pool (bytes; s is the contiguous rest); "rx_ring" p = peak RX ring usage (bytes)
/// since the last report. Also, every 3 s from app_metrics.cpp, "task_cpu"
/// n = task, c = CPU time since the last record in permille, p = priority.
void report_memory();

/// Per second from the same place, when there was traffic: "net_lat"
/// e / w / a = ETH interrupt -> network task wake-ups: count, total ms, max ms;
/// n / q / b = packets through the tcpip mailbox: count, total wait ms, max ms.
void report_net_lat();

} // namespace upload_experiment

/// EXPERIMENT (switch y, wui.cpp): network task and the calling thread (the
/// tcpip thread) at Normal priority (true) or stock BelowNormal (false).
extern "C" void buddy_net_priority_raise(bool raised);

namespace ooseq_limit {
/// EXPERIMENT: switch the inbound ooseq caps between stock and fix A
/// (defined in ooseq_limit.cpp, runs on the tcpip thread).
void set_inbound_fix_a(bool on);
} // namespace ooseq_limit
