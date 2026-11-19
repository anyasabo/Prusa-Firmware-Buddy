#pragma once
// EXPERIMENT (branch upload-switches, not for upstream): the current values of
// the upload switches. See lib/WUI/upload_switches.hpp for the file name syntax.
// Written by the tcpip thread (nhttp), read by the tcpip thread (lwIP, nhttp)
// and by the network task (ethernetif.c). Each value is one byte.
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

extern volatile uint8_t upload_switch_rx_desc;
extern volatile uint8_t upload_switch_ooseq_fix;
extern volatile uint8_t upload_switch_queue_cap;

/// Sets the switches from the file name of an upload. Call it on the tcpip thread.
void upload_switches_apply_from_path(const char *path);

#ifdef __cplusplus
}
#endif
