#pragma once
// EXPERIMENT (upload-experiments branch, not for upstream as is): cumulative
// counters for USB mass storage I/O, to see what one FatFs operation (stat,
// rename, close, ...) costs on the USB drive. Reported per second as the
// "usb_io", "usb_io2" and "usb_io3" metrics and as per-upload deltas in
// "upx_u", "upx_v" and "upx_g" (lib/WUI/upload_experiment.cpp).
//
// Each group says who writes it. Single-writer counters are plain increments;
// the multi-writer ones (`unaligned`, `lock_*`) are approximate (no atomics in
// C here, a lost increment only skews a metric).
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
    uint32_t single_sector; ///< requests of 1 sector (SCSI command overhead dominates)
    uint32_t slow; ///< requests that took >= 20 ms
    // usbh_msc.c, same worker task
    uint32_t wait_timeouts; ///< ulTaskNotifyTake in USBH_MSC_Read/Write timed out (no URB notification)
    // any FatFs caller (usbh_diskio.cpp USBH_read/USBH_write)
    uint32_t unaligned; ///< requests split into single sectors through the scratch buffer (unaligned buffer)
    // USB host ISR (usbh_conf.cpp HAL_HCD_HC_NotifyURBChange_Callback)
    uint32_t urb_events; ///< URB state changes notified (a few per request is normal)
    uint32_t urb_notready; ///< of those, URB_NOTREADY (NAK / NYET / retried transaction errors)
    // any task taking a FatFs lock (ffsystem.c ff_mutex_take: the volume lock, and the system lock of
    // the file lock table that every call with a path also takes)
    uint32_t lock_waits; ///< the lock was held by another task
    uint32_t lock_wait_ms; ///< time spent waiting for it
};

// Weak, zero-initialised (see buddy/rx_stats.h for the pattern).
__attribute__((weak)) struct buddy_usb_stats buddy_usb_stats;

/// How long USBH_MSC_Read/Write sleep waiting for a URB notification before
/// looking at the transfer state again (ms, >= 1). Stock: 500. Switch "k" of the
/// upload experiments (buddy/upload_experiment.hpp).
__attribute__((weak)) volatile uint32_t buddy_usbh_msc_wait_ms = 500;

/// FatFs gen_numname: after how many ~N collisions of a generated 8.3 name it
/// switches from sequential numbers to a hash of the long name (each attempt
/// is a full directory scan). Stock: 5. Switch "h" (h1: 0, hash right away).
__attribute__((weak)) volatile uint32_t buddy_fatfs_numname_hash_after = 5;

/// FatFs directory lookups (ff.c, FAT/FAT32 only). All 0 = stock R0.16 behaviour.
/// Skip runs of deleted entries inside a sector without dir_next() (dir_find, dir_read, dir_alloc).
__attribute__((weak)) volatile uint32_t buddy_fatfs_skip_deleted = 0;
/// Directory read-ahead: sectors per read while dir_find/dir_read/dir_alloc walk a directory
/// (0 or 1: off, max FF_DIR_RA_MAX in ff.c). It never reads past the current cluster.
__attribute__((weak)) volatile uint32_t buddy_fatfs_dir_readahead = 0;
/// Name cache: hashes of all names in the last directory that dir_find scanned to its end.
/// dir_find answers "no such name" from it without disk reads.
__attribute__((weak)) volatile uint32_t buddy_fatfs_name_cache = 0;

struct buddy_fatfs_stats {
    // all written under the FatFs volume lock
    uint32_t nc_builds; ///< full scans that made the name cache valid
    uint32_t nc_hits; ///< lookups answered "not present" with no disk read
    uint32_t nc_maybe; ///< lookups the cache could not answer (name hash present: normal scan)
    uint32_t nc_drops; ///< cache invalidations (mount, mkdir, rmdir, too many names, unusual LFN)
    uint32_t ra_reads; ///< read-ahead disk reads
    uint32_t ra_hits; ///< directory sectors served from the read-ahead buffer
};
__attribute__((weak)) struct buddy_fatfs_stats buddy_fatfs_stats;

#ifdef __cplusplus
}
#endif
