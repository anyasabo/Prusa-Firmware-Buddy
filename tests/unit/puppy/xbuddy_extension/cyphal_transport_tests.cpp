/// Host tests of the Cyphal transport of the xBuddy extension puppy.
/// The test compiles the real transport with a fake CAN bus and a fake clock.
#include "cyphal_clock.hpp"
#include "hal_pub.hpp"

#include <catch2/catch_test_macros.hpp>
#include <cstring>
#include <deque>
#include <freertos/timing.hpp>
#include <vector>

// The transport class is internal to its file.
#include "cyphal_transport.cpp"

namespace {

/// Milliseconds as uint32_t, as on the puppy.
uint32_t now_ms = 1000;

/// Frames from the remote nodes to the transport.
std::deque<hal::pub::RxFrame> bus_rx;

/// CAN identifiers of the frames from the transport to the bus.
std::vector<hal::pub::Identifier> bus_tx;

} // namespace

size_t freertos::millis() { return now_ms; }
void freertos::delay(size_t) {}

bool hal::pub::transmit(Identifier identifier, Bytes) {
    bus_tx.push_back(identifier);
    return true;
}

bool hal::pub::receive(RxFrame &frame) {
    if (bus_rx.empty()) {
        return false;
    }
    frame = bus_rx.front();
    bus_rx.pop_front();
    return true;
}

namespace {

constexpr CanardPortID diagnostic_record_port = 8184;
constexpr CanardNodeID remote_node = 5;

/// A remote node with its own libcanard instance, as on the ANFC or the tool offset sensor.
/// A new instance is a node after a reset.
class RemoteNode {
public:
    RemoteNode() {
        canard = canardInit(allocate, deallocate);
        canard.node_id = remote_node;
        tx_queue = canardTxInit(16, CANARD_MTU_CAN_FD);
    }

    /// Put the frames of one message transfer on the bus.
    void publish(CanardPortID port, const std::vector<uint8_t> &payload) {
        const CanardTransferMetadata metadata {
            .priority = CanardPriorityNominal,
            .transfer_kind = CanardTransferKindMessage,
            .port_id = port,
            .remote_node_id = CANARD_NODE_ID_UNSET,
            .transfer_id = transfer_id++,
        };
        REQUIRE(canardTxPush(&tx_queue, &canard, 0, &metadata, payload.size(), payload.data()) > 0);
        while (const CanardTxQueueItem *item = canardTxPeek(&tx_queue)) {
            hal::pub::RxFrame frame {};
            frame.identifier = item->frame.extended_can_id;
            frame.size = item->frame.payload_size;
            std::memcpy(frame.data, item->frame.payload, frame.size);
            bus_rx.push_back(frame);
            canard.memory_free(&canard, canardTxPop(&tx_queue, item));
        }
    }

private:
    static void *allocate(CanardInstance *, size_t amount) { return malloc(amount); }
    static void deallocate(CanardInstance *, void *pointer) { free(pointer); }

    CanardInstance canard;
    CanardTxQueue tx_queue;
    CanardTransferID transfer_id = 0;
};

/// Run the transport until the bus is empty. Return the payloads that it received.
std::vector<std::vector<uint8_t>> drain(TransportImplementation &transport) {
    while (transport.step()) {
    }
    std::vector<std::vector<uint8_t>> payloads;
    CanardRxTransfer transfer;
    while (transport.receive(transfer)) {
        const auto *data = static_cast<const uint8_t *>(transfer.payload);
        payloads.emplace_back(data, data + transfer.payload_size);
        transport.dispose(transfer);
    }
    return payloads;
}

void reset_world() {
    now_ms = 1000;
    bus_rx.clear();
    bus_tx.clear();
}

} // namespace

TEST_CASE("cyphal transport: a restarted node is heard again") {
    reset_world();
    TransportImplementation transport;
    REQUIRE(transport.subscribe(CanardTransferKindMessage, diagnostic_record_port, 300));

    // The node sends one message after each boot, for example a diagnostic record.
    // Each boot starts with transfer-ID 0.
    {
        RemoteNode node;
        node.publish(diagnostic_record_port, { 1, 2, 3 });
        CHECK(drain(transport) == std::vector<std::vector<uint8_t>> { { 1, 2, 3 } });
    }

    // The node restarts. Its first transfer has the same transfer-ID as the last one.
    // After the transfer-ID timeout (2 s), this is a new transfer, not a duplicate.
    now_ms += 5000;
    {
        RemoteNode node;
        node.publish(diagnostic_record_port, { 4, 5, 6 });
        CHECK(drain(transport) == std::vector<std::vector<uint8_t>> { { 4, 5, 6 } });
    }
}

TEST_CASE("cyphal transport: a duplicate within the transfer-ID timeout is dropped") {
    reset_world();
    TransportImplementation transport;
    REQUIRE(transport.subscribe(CanardTransferKindMessage, diagnostic_record_port, 300));

    RemoteNode node;
    node.publish(diagnostic_record_port, { 1 });
    const auto frame = bus_rx.front();
    CHECK(drain(transport).size() == 1);

    // The same frame again, 100 ms later: the CAN controller of the node sent it twice.
    now_ms += 100;
    bus_rx.push_back(frame);
    CHECK(drain(transport).empty());

    // The next transfer of the node arrives.
    node.publish(diagnostic_record_port, { 2 });
    CHECK(drain(transport) == std::vector<std::vector<uint8_t>> { { 2 } });
}

TEST_CASE("cyphal transport: a restarted node is heard after the millisecond wrap") {
    reset_world();
    now_ms = UINT32_MAX - 1000;
    TransportImplementation transport;
    REQUIRE(transport.subscribe(CanardTransferKindMessage, diagnostic_record_port, 300));

    {
        RemoteNode node;
        node.publish(diagnostic_record_port, { 1 });
        CHECK(drain(transport).size() == 1);
    }

    // 5 s later, the 32-bit millisecond count is small again.
    now_ms += 5000;
    {
        RemoteNode node;
        node.publish(diagnostic_record_port, { 2 });
        CHECK(drain(transport) == std::vector<std::vector<uint8_t>> { { 2 } });
    }
}

TEST_CASE("cyphal transport: an idle bus does not hide a wrap of the millisecond count") {
    reset_world();
    TransportImplementation transport;
    REQUIRE(transport.subscribe(CanardTransferKindMessage, diagnostic_record_port, 300));

    {
        RemoteNode node;
        node.publish(diagnostic_record_port, { 1 });
        CHECK(drain(transport).size() == 1);
    }

    // No frames for 49.7 days. The transport steps on the idle bus.
    now_ms += 1u << 31;
    CHECK_FALSE(transport.step());
    now_ms += 1u << 31;
    CHECK_FALSE(transport.step());

    // The millisecond count is now only 500 ms after the last transfer, but the real time is 49.7 days later.
    now_ms += 500;
    {
        RemoteNode node;
        node.publish(diagnostic_record_port, { 2 });
        CHECK(drain(transport) == std::vector<std::vector<uint8_t>> { { 2 } });
    }
}

TEST_CASE("cyphal transport: a deadline of 0 means no deadline") {
    reset_world();
    TransportImplementation transport;

    const CanardTransferMetadata metadata {
        .priority = CanardPriorityNominal,
        .transfer_kind = CanardTransferKindMessage,
        .port_id = diagnostic_record_port,
        .remote_node_id = CANARD_NODE_ID_UNSET,
        .transfer_id = 0,
    };
    const std::byte payload[1] {};
    REQUIRE(transport.transmit(0, metadata, { payload, sizeof(payload) }));

    // The frame waits in the queue for a long time.
    now_ms += 60'000;
    while (transport.step()) {
    }
    CHECK(bus_tx.size() == 1);
}

TEST_CASE("cyphal transport: a frame after its deadline is dropped") {
    reset_world();
    TransportImplementation transport;

    const CanardTransferMetadata metadata {
        .priority = CanardPriorityNominal,
        .transfer_kind = CanardTransferKindMessage,
        .port_id = diagnostic_record_port,
        .remote_node_id = CANARD_NODE_ID_UNSET,
        .transfer_id = 0,
    };
    const std::byte payload[1] {};
    const CanardMicrosecond deadline = cyphal::MonotonicClock {}.update(now_ms) + 10'000;
    REQUIRE(transport.transmit(deadline, metadata, { payload, sizeof(payload) }));

    now_ms += 11;
    while (transport.step()) {
    }
    CHECK(bus_tx.empty());
}

TEST_CASE("cyphal clock: the time does not wrap with the millisecond count") {
    cyphal::MonotonicClock clock;
    CHECK(clock.update(0) == 0);
    CHECK(clock.update(1) == 1000);
    CHECK(clock.update(UINT32_MAX) == uint64_t { UINT32_MAX } * 1000);
    CHECK(clock.update(0) == (uint64_t { 1 } << 32) * 1000);
    CHECK(clock.update(5) == ((uint64_t { 1 } << 32) + 5) * 1000);
    CHECK(clock.update(UINT32_MAX) == ((uint64_t { 2 } << 32) - 1) * 1000);
    CHECK(clock.update(2) == ((uint64_t { 2 } << 32) + 2) * 1000);
}
