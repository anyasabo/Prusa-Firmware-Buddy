/// Host tests of the MMU bridge of the xBuddy extension puppy (mmu.cpp).
/// The test compiles the real file with a fake MMU UART and a fake clock.
#include "mmu.hpp"
#include "hal_mmu.hpp"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <cstring>
#include <deque>
#include <freertos/timing.hpp>
#include <functional>
#include <stdexcept>
#include <vector>
#include <xbuddy_extension/mmu_bridge.hpp>

using namespace modules::protocol;
using Status = modbus::Callbacks::Status;

namespace {

/// The fake world: a clock and the MMU at the other end of the UART.
struct FakeWorld {
    /// Milliseconds as uint32_t, as on the puppy. Wraps after 49.7 days.
    uint32_t now = 1000;

    /// Each call of millis() advances the clock by this, to model the busy
    /// wait of the bridge. A test can change it to model a preemption.
    std::function<uint32_t(uint32_t now)> step = [](uint32_t) { return 1u; };

    /// Number of millis() calls. A guard stops a wait that does not end.
    uint64_t millis_calls = 0;
    static constexpr uint64_t max_millis_calls = 1'000'000;

    /// Bytes from the bridge to the MMU.
    std::vector<std::vector<uint8_t>> requests;

    /// Makes the reply of the MMU to a request. Empty: no reply.
    std::function<std::vector<uint8_t>(const RequestMsg &)> mmu = [](const RequestMsg &) { return std::vector<uint8_t> {}; };

    /// millis() calls from the request to the reply. With the default step,
    /// one call is one millisecond.
    uint32_t reply_delay_calls = 1;

    /// Send the reply in parts of this many bytes, one part for each
    /// millis() call. 0: all at once.
    size_t reply_chunk = 0;

    std::deque<uint8_t> rx;
    std::vector<uint8_t> pending_reply;
    uint64_t reply_at_call = 0;

    uint32_t millis() {
        if (++millis_calls > max_millis_calls) {
            throw std::runtime_error("the bridge waits forever");
        }
        const uint32_t result = now;
        now += step(now);
        return result;
    }
};

FakeWorld *world = nullptr;

RequestMsg decode_request(const std::vector<uint8_t> &bytes) {
    Protocol p;
    for (uint8_t b : bytes) {
        if (p.DecodeRequest(b) == DecodeStatus::MessageCompleted) {
            return p.GetRequestMsg();
        }
    }
    FAIL("The bridge sent an invalid request");
    return RequestMsg(RequestMsgCodes::unknown, 0);
}

std::vector<uint8_t> encode_read_reply(const RequestMsg &rq, uint16_t value) {
    uint8_t buf[Protocol::MaxResponseSize()];
    const auto len = Protocol::EncodeResponseRead(rq, true, value, buf);
    return { buf, buf + len };
}

struct WorldGuard {
    FakeWorld w;
    WorldGuard() { world = &w; }
    ~WorldGuard() { world = nullptr; }
};

} // namespace

size_t freertos::millis() {
    return world->millis();
}

namespace hal::mmu {

void transmit(Bytes bytes) {
    std::vector<uint8_t> v;
    for (auto b : bytes) {
        v.push_back(static_cast<uint8_t>(b));
    }
    world->requests.push_back(v);
    world->pending_reply = world->mmu(decode_request(v));
    world->reply_at_call = world->millis_calls + world->reply_delay_calls;
}

WritableBytes receive(WritableBytes buffer) {
    if (!world->pending_reply.empty() && world->millis_calls >= world->reply_at_call) {
        const size_t n = world->reply_chunk ? std::min(world->reply_chunk, world->pending_reply.size()) : world->pending_reply.size();
        world->rx.insert(world->rx.end(), world->pending_reply.begin(), world->pending_reply.begin() + n);
        world->pending_reply.erase(world->pending_reply.begin(), world->pending_reply.begin() + n);
        world->reply_at_call = world->millis_calls + 1;
    }
    size_t n = 0;
    while (n < buffer.size() && !world->rx.empty()) {
        buffer[n++] = static_cast<std::byte>(world->rx.front());
        world->rx.pop_front();
    }
    return buffer.first(n);
}

} // namespace hal::mmu

TEST_CASE("xbe mmu: read a register", "[xbe][mmu]") {
    WorldGuard g;
    g.w.mmu = [](const RequestMsg &rq) {
        CHECK(rq.code == RequestMsgCodes::Read);
        CHECK(rq.value == 0x1a);
        return encode_read_reply(rq, 0x1234);
    };
    MMU mmu;
    uint16_t out[1] = {};
    CHECK(mmu.read_registers(0x1a, out) == Status::Ok);
    CHECK(out[0] == 0x1234);
    CHECK(g.w.requests.size() == 1);
}

TEST_CASE("xbe mmu: the version registers use the S request", "[xbe][mmu]") {
    WorldGuard g;
    g.w.mmu = [](const RequestMsg &rq) {
        CHECK(rq.code == RequestMsgCodes::Version);
        return encode_read_reply(rq, 10 + rq.value);
    };
    MMU mmu;
    uint16_t out[4] = {};
    CHECK(mmu.read_registers(0, out) == Status::Ok);
    CHECK(out[0] == 10);
    CHECK(out[3] == 13);
    CHECK(g.w.requests.size() == 4);
}

TEST_CASE("xbe mmu: one Query serves the three command registers", "[xbe][mmu]") {
    using namespace xbuddy_extension::mmu_bridge;
    WorldGuard g;
    g.w.mmu = [](const RequestMsg &rq) {
        CHECK(rq.code == RequestMsgCodes::Query);
        // The MMU reports the command in progress (T1), Processing, progress 5
        RequestMsg cmd(RequestMsgCodes::Tool, 1);
        uint8_t buf[Protocol::MaxResponseSize()];
        const auto len = Protocol::EncodeResponseQueryOperation(cmd, ResponseCommandStatus(ResponseMsgParamCodes::Processing, 5), buf);
        return std::vector<uint8_t>(buf, buf + len);
    };
    MMU mmu;
    uint16_t out[3] = {};
    CHECK(mmu.read_registers(commandInProgressRegisterAddress, out) == Status::Ok);
    CHECK(out[0] == pack_command('T', 1));
    CHECK(out[1] == static_cast<uint8_t>(ResponseMsgParamCodes::Processing));
    CHECK(out[2] == 5);
    CHECK(g.w.requests.size() == 1);
}

TEST_CASE("xbe mmu: write registers", "[xbe][mmu]") {
    WorldGuard g;
    g.w.mmu = [](const RequestMsg &rq) {
        uint8_t buf[Protocol::MaxResponseSize()];
        const auto len = Protocol::EncodeResponseCmdAR(rq, ResponseMsgParamCodes::Accepted, buf);
        return std::vector<uint8_t>(buf, buf + len);
    };
    MMU mmu;

    SECTION("An 8-bit register gets the low byte") {
        const uint16_t in[1] = { 0x1234 };
        CHECK(mmu.write_registers(7, in) == Status::Ok);
        const auto rq = decode_request(g.w.requests.at(0));
        CHECK(rq.code == RequestMsgCodes::Write);
        CHECK(rq.value == 7);
        CHECK(rq.value2 == 0x34);
    }

    SECTION("A 16-bit register gets the full value") {
        const uint16_t in[1] = { 0x1234 };
        CHECK(mmu.write_registers(34, in) == Status::Ok);
        const auto rq = decode_request(g.w.requests.at(0));
        CHECK(rq.value == 34);
        CHECK(rq.value2 == 0x1234);
    }

    SECTION("A register that is not writable") {
        const uint16_t in[1] = { 1 };
        CHECK(mmu.write_registers(5, in) == Status::IllegalAddress);
        CHECK(g.w.requests.empty());
    }
}

TEST_CASE("xbe mmu: no reply gives a gateway error after 10 ms", "[xbe][mmu]") {
    WorldGuard g;
    MMU mmu;
    uint16_t out[1] = {};
    const uint32_t start = g.w.now;
    CHECK(mmu.read_registers(0x1a, out) == Status::GatewayPathUnavailable);
    CHECK(g.w.now - start >= 10);
    CHECK(g.w.now - start <= 12);
}

TEST_CASE("xbe mmu: a corrupt reply gives no Modbus reply", "[xbe][mmu]") {
    WorldGuard g;
    MMU mmu;
    uint16_t out[1] = {};

    SECTION("An invalid first byte") {
        g.w.mmu = [](const RequestMsg &) { return std::vector<uint8_t> { 'Z', 'Z', 'Z', '\n' }; };
        CHECK(mmu.read_registers(0x1a, out) == Status::Ignore);
    }

    SECTION("A wrong CRC") {
        g.w.mmu = [](const RequestMsg &rq) {
            auto reply = encode_read_reply(rq, 0x1234);
            // The reply ends with "*<crc>\n". Change one hex digit of the CRC.
            auto &digit = reply.at(reply.size() - 2);
            digit = digit == '0' ? '1' : '0';
            return reply;
        };
        CHECK(mmu.read_registers(0x1a, out) == Status::Ignore);
    }
}

TEST_CASE("xbe mmu: a reply in parts", "[xbe][mmu]") {
    // Each part of the reply restarts the 10 ms timeout. Thus a slow reply
    // of 1 byte each 5 ms is still accepted.
    WorldGuard g;
    g.w.mmu = [](const RequestMsg &rq) { return encode_read_reply(rq, 0x1234); };
    g.w.reply_chunk = 1;
    g.w.step = [](uint32_t) { return 5u; };
    MMU mmu;
    uint16_t out[1] = {};
    CHECK(mmu.read_registers(0x1a, out) == Status::Ok);
    CHECK(out[0] == 0x1234);
}

TEST_CASE("xbe mmu: the timeout is correct across the millisecond wrap", "[xbe][mmu]") {
    WorldGuard g;
    g.w.mmu = [](const RequestMsg &rq) { return encode_read_reply(rq, 42); };
    MMU mmu;
    uint16_t out[1] = {};

    SECTION("A reply 3 ms after a request just before the wrap") {
        g.w.reply_delay_calls = 3;
        const uint32_t offset = GENERATE(1u, 5u, 9u, 10u, 11u);
        CAPTURE(offset);
        g.w.now = uint32_t(0) - offset;
        CHECK(mmu.read_registers(0x1a, out) == Status::Ok);
        CHECK(out[0] == 42);
    }

    SECTION("No reply, and the task does not run during the last ms before the wrap") {
        g.w.mmu = [](const RequestMsg &) { return std::vector<uint8_t> {}; };
        g.w.now = 0xFFFF'FFF0;
        // A higher priority task or an interrupt holds the CPU from
        // 0xFFFFFFF8 to 0x00000004.
        g.w.step = [](uint32_t now) { return now == 0xFFFF'FFF7 ? 13u : 1u; };
        CHECK(mmu.read_registers(0x1a, out) == Status::GatewayPathUnavailable);
    }
}
