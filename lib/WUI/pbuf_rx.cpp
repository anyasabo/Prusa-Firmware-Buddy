#include "pbuf_rx.h"
#include "ooseq_limit.hpp"

#include <ring_allocator.hpp>
#include <freertos/mutex.hpp>
#include <buddy/upload_experiment.hpp>
#include <buddy/rx_stats.h>

namespace {

// TODO: The size (derive from somewhere)
constexpr size_t mtu_size = 1024;
constexpr size_t alloc_record = 4;
constexpr size_t pbuf_head = LWIP_MEM_ALIGN_SIZE(sizeof(struct pbuf_custom));
// EXPERIMENT (upload-experiments): the stock 10 again. 18 gave room for a 16 KB
// TCP window (switch x), but x16 did not help (2026-11-07) and cost 8.4 KB of
// heap. Now x12 and x16 advertise more window than the ring backs.
constexpr size_t mtu_cnt = 10;
// Each allocation carries its size in front (for the usage accounting).
constexpr size_t size_prefix = buddy::RingAllocator::alignment;
static_assert(size_prefix >= sizeof(uint32_t));

// A bit more for "small" packets.
constexpr size_t buffer_size = mtu_cnt * (mtu_size + alloc_record + pbuf_head + size_prefix) + 500;

alignas(buddy::RingAllocator::alignment) std::array<uint8_t, buffer_size> rx_allocator_buffer;
buddy::RingAllocator rx_allocator(rx_allocator_buffer);

freertos::Mutex allocator_mutex;
size_t used_bytes = 0; // under allocator_mutex

void rx_free(struct pbuf *p) {
    std::unique_lock<freertos::Mutex> lock(allocator_mutex);
    uint8_t *base = reinterpret_cast<uint8_t *>(p) - size_prefix;
    used_bytes -= *reinterpret_cast<uint32_t *>(base);
    rx_allocator.free(base);
}

} // namespace

struct pbuf *pbuf_alloc_rx(u16_t length) {
    std::unique_lock<freertos::Mutex> lock(allocator_mutex);
    size_t req_size = size_prefix + pbuf_head + length;
    const size_t cap = upload_experiment::rx_ring_cap;
    uint8_t *base = (cap != 0 && used_bytes + req_size > cap) ? nullptr : static_cast<uint8_t *>(rx_allocator.allocate(req_size));
    if (base != nullptr) {
        *reinterpret_cast<uint32_t *>(base) = req_size;
        used_bytes += req_size;
        if (used_bytes > buddy_rx_stats.ring_peak) {
            buddy_rx_stats.ring_peak = used_bytes;
        }
    }
    struct pbuf_custom *p = base != nullptr ? reinterpret_cast<pbuf_custom *>(base + size_prefix) : nullptr;
    if (!p) {
        lock.unlock();
        // The ring is full. Out-of-order TCP data may be what holds it; ask the
        // tcpip thread to free that, so in-order frames get through again. The
        // frame that failed here is still dropped.
        buddy_ooseq_reclaim_request();
        return nullptr;
    }
    p->custom_free_function = rx_free;
    void *payload_mem = ((uint8_t *)p) + pbuf_head;
    return pbuf_alloced_custom(PBUF_RAW, length, PBUF_POOL, p, payload_mem, length);
}
