#pragma once
// EXPERIMENT (upload-experiments branch, not for upstream as is): cumulative
// counters for USB mass storage I/O, to see what one FatFs operation (stat,
// rename, close, ...) costs on the USB drive. Reported per second as the
// "usb_io" metric and as per-upload deltas in "upx_p" / "upx_f"
// (lib/WUI/upload_experiment.cpp).
//
// The counters below the line "worker" are written only by the USB MSC worker
// task (single writer, plain increments); `unaligned` by any task that reads or
// writes through FatFs, so it is only approximate (no atomics in C here).
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct buddy_usb_stats {
    // worker task (usbh_diskio.cpp, USBH_MSC_WorkerTask)
    uint32_t reads; ///< read requests executed
    uint32_t writes; ///< write requests executed
    uint32_t sectors; ///< sectors read + written
    uint32_t busy_ms; ///< sum of the time the requests took (USBH_MSC_Read/Write)
    uint32_t max_ms; ///< longest request since the reporter last reset it to 0
    uint32_t errors; ///< requests that didn't return USBH_OK
    // usbh_msc.c, same worker task
    uint32_t wait_timeouts; ///< ulTaskNotifyTake in USBH_MSC_Read/Write timed out (no URB notification)
    // any FatFs caller (usbh_diskio.cpp USBH_read/USBH_write)
    uint32_t unaligned; ///< requests split into single sectors through the scratch buffer (unaligned buffer)
};

// Weak, zero-initialised (see buddy/rx_stats.h for the pattern).
__attribute__((weak)) struct buddy_usb_stats buddy_usb_stats;

/// How long USBH_MSC_Read/Write sleep waiting for a URB notification before
/// looking at the transfer state again (ms, >= 1). Stock: 500. Switch "k" of the
/// upload experiments (buddy/upload_experiment.hpp).
__attribute__((weak)) volatile uint32_t buddy_usbh_msc_wait_ms = 500;

#ifdef __cplusplus
}
#endif
