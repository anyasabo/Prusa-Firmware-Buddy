// lwIP options for the lwip_eth_input fuzz harness. The file is buddy/lwipopts.h because the
// vendored lwip/opt.h includes <buddy/lwipopts.h>.
//
// The protocol options are the ones of include/buddy/lwipopts.h that change how the printer handles
// received frames: Ethernet, ARP, IPv4 with reassembly, ICMP, IGMP, UDP, raw, DHCP with the address
// conflict detection, and the mDNS responder of lib/WUI/mdns. TCP is off: fuzz_lwip_tcp_input
// covers it. The host values: NO_SYS, malloc for the heap and the pools (so that ASan finds a
// leak or a use after free, else the pools of the firmware), and no checksum check (the fuzzer
// cannot compute checksums). On the 64-bit host, structs with pointers are larger than on the
// printer, so the pool use is not exactly the same.
#pragma once

#include <stdint.h>

#define NO_SYS                         1
#define LWIP_TIMERS                    1
#define LWIP_TESTMODE                  1 // the harness clears the timeout list between inputs
#define SYS_LIGHTWEIGHT_PROT           0
#define LWIP_NETCONN                   0
#define LWIP_SOCKET                    0
#define LWIP_IPV4                      1
#define LWIP_IPV6                      0
#define LWIP_TCP                       0
#define LWIP_UDP                       1
#define LWIP_RAW                       1 // Buddy
#define LWIP_ICMP                      1
#define LWIP_ARP                       1
#define LWIP_ETHERNET                  1
#define LWIP_DHCP                      1 // Buddy
#define LWIP_DHCP_DOES_ACD_CHECK       1 // Buddy
#define LWIP_IGMP                      1 // Buddy (mDNS)
#define LWIP_DNS                       0
#define LWIP_ALTCP                     0
#define LWIP_NETIF_HOSTNAME            1 // Buddy
#define LWIP_NUM_NETIF_CLIENT_DATA     1 // Buddy (mDNS)
#define LWIP_NETIF_EXT_STATUS_CALLBACK 1 // Buddy (mDNS)
#define LWIP_MDNS_RESPONDER            1 // Buddy
#define MDNS_MAX_STORED_PKTS           1 // Buddy
#define IP_REASSEMBLY                  1 // lwIP default, as in Buddy
#define IP_FRAG                        1 // lwIP default, as in Buddy
#define IP_REASS_MAX_PBUFS             15 // Buddy
#define MEM_ALIGNMENT                  8
#define PBUF_POOL_SIZE                 0 // Buddy: the RX path has its own pbufs
#if BUDDY_FUZZ_LWIP_POOLS
    // The heap pools (buddy/lwippools.h) and the pool counts of the firmware.
    #define MEM_LIBC_MALLOC       0
    #define MEMP_MEM_MALLOC       0
    #define MEM_USE_POOLS         1
    #define MEMP_USE_CUSTOM_POOLS 1
    #define MEMP_NUM_UDP_PCB      5
    // The firmware adds 2 to the core count. Its core count also has the TCP and DNS timers, which
    // this build does not have, so that the free count is the same.
    #define MEMP_NUM_SYS_TIMEOUT (LWIP_NUM_SYS_TIMEOUT_INTERNAL + 2 + 6 + MDNS_MAX_STORED_PKTS)
#else
    #define MEM_LIBC_MALLOC      1
    #define MEMP_MEM_MALLOC      1
    #define MEMP_NUM_SYS_TIMEOUT 64 // ignored with MEMP_MEM_MALLOC
#endif
#define LWIP_STATS          0
#define CHECKSUM_CHECK_IP   0
#define CHECKSUM_CHECK_UDP  0
#define CHECKSUM_CHECK_ICMP 0
