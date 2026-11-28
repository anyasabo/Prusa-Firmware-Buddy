// The heap pools of include/buddy/lwippools.h with mDNS on, for fuzz_lwip_eth_input_pools. That
// file includes a generated option header, which the host build does not have.
LWIP_MALLOC_MEMPOOL_START
LWIP_MALLOC_MEMPOOL(7, 128)
LWIP_MALLOC_MEMPOOL(2, 512)
LWIP_MALLOC_MEMPOOL(1, 1512)
LWIP_MALLOC_MEMPOOL_END
