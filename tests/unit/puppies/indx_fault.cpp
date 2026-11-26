// Host tests for src/puppies/indx_fault.cpp: the action of the main board for each fault bit of the
// INDX head.
#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <cstring>
#include <optional>
#include <utility>

#include <puppies/indx_fault.hpp>

using buddy::puppies::indx_fault::Redscreen;
using buddy::puppies::indx_fault::redscreen_for_status;
using buddy::puppies::indx_fault::redscreen_title;
using indx_head::errors::FaultStatusMask;

namespace {

// The expected action for a bit.
enum class Expect {
    log_only, // acknowledge and log, no redscreen
    code, // redscreen with an error code from buddy-error-codes.yaml
    text, // redscreen with a text (ErrCode::ERR_UNDEF)
};

struct Case {
    uint16_t bit;
    Expect expect;
    ErrCode code = ErrCode::ERR_UNDEF;
};

uint16_t raw(FaultStatusMask bit) {
    return std::to_underlying(bit);
}

void check(const Case &c) {
    CAPTURE(c.bit);
    const std::optional<Redscreen> redscreen = redscreen_for_status(c.bit);
    switch (c.expect) {
    case Expect::log_only:
        CHECK_FALSE(redscreen.has_value());
        break;
    case Expect::code:
        REQUIRE(redscreen.has_value());
        CHECK(redscreen->code == c.code);
        CHECK(redscreen->text == nullptr);
        break;
    case Expect::text:
        REQUIRE(redscreen.has_value());
        CHECK(redscreen->code == ErrCode::ERR_UNDEF);
        REQUIRE(redscreen->text != nullptr);
        CHECK(std::strlen(redscreen->text) > 0);
        // Limit of fatal_error() (src/common/bsod.h).
        CHECK(std::strlen(redscreen->text) <= 107);
        break;
    }
}

} // namespace

TEST_CASE("indx_fault: the title of a text redscreen fits", "[indx_fault]") {
    // Limit of fatal_error() (src/common/bsod.h).
    CHECK(std::strlen(redscreen_title) <= 20);
}

TEST_CASE("indx_fault: no fault and the normal reset bits give no redscreen", "[indx_fault]") {
    check({ raw(FaultStatusMask::no_fault), Expect::log_only });
    // The head sets these bits at each boot: a power-on reset, or the reset pin of the bootstrap.
    check({ raw(FaultStatusMask::pin_reset), Expect::log_only });
    check({ raw(FaultStatusMask::power_reset), Expect::log_only });
    check({ static_cast<uint16_t>(raw(FaultStatusMask::pin_reset) | raw(FaultStatusMask::power_reset)), Expect::log_only });
}

TEST_CASE("indx_fault: the board and TPIS ambient temperatures give their redscreen", "[indx_fault]") {
    check({ raw(FaultStatusMask::board_min_temp), Expect::code, ErrCode::ERR_TEMPERATURE_INDX_HEAD_BOARD_MINTEMP_ERR });
    check({ raw(FaultStatusMask::board_max_temp), Expect::code, ErrCode::ERR_TEMPERATURE_INDX_HEAD_BOARD_MAXTEMP_ERR });
    check({ raw(FaultStatusMask::tpis_ambient_min_temp), Expect::code, ErrCode::ERR_TEMPERATURE_INDX_HEAD_TPIS_AMBIENT_MINTEMP_ERR });
    check({ raw(FaultStatusMask::tpis_ambient_max_temp), Expect::code, ErrCode::ERR_TEMPERATURE_INDX_HEAD_TPIS_AMBIENT_MAXTEMP_ERR });
}

TEST_CASE("indx_fault: the nozzle temperature faults give a hotend redscreen", "[indx_fault][!shouldfail][known_bug]") {
    check({ raw(FaultStatusMask::nozzle_max_temp), Expect::code, ErrCode::ERR_TEMPERATURE_HOTEND_MAXTEMP_ERROR });
    check({ raw(FaultStatusMask::nozzle_min_temp), Expect::code, ErrCode::ERR_TEMPERATURE_HOTEND_MINTEMP_ERROR });
    check({ raw(FaultStatusMask::tpis_invalid_timeout), Expect::code, ErrCode::ERR_TEMPERATURE_HOTEND_MINTEMP_ERROR });
}

TEST_CASE("indx_fault: the crash faults of the head give a redscreen", "[indx_fault][!shouldfail][known_bug]") {
    check({ raw(FaultStatusMask::hard_fault), Expect::text });
    check({ raw(FaultStatusMask::stack_overflow), Expect::text });
    check({ raw(FaultStatusMask::assert_failed), Expect::text });
    check({ raw(FaultStatusMask::loadcell_crc_mismatch), Expect::text });
    check({ raw(FaultStatusMask::watchdog_reset), Expect::text });
}

TEST_CASE("indx_fault: an unknown bit gives a redscreen", "[indx_fault][!shouldfail][known_bug]") {
    check({ raw(FaultStatusMask::undefined_error), Expect::text });
    // Bit 15 has no enumerator.
    check({ uint16_t(1u << 15), Expect::text });
}

TEST_CASE("indx_fault: each of the 16 bits has a decision", "[indx_fault][!shouldfail][known_bug]") {
    // Only these bits are log only. Each other bit, alone, gives a redscreen.
    const uint16_t log_only = raw(FaultStatusMask::pin_reset) | raw(FaultStatusMask::power_reset);
    for (unsigned i = 0; i < 16; ++i) {
        const auto bit = static_cast<uint16_t>(1u << i);
        check({ bit, (bit & log_only) ? Expect::log_only : Expect::text });
    }
}

TEST_CASE("indx_fault: the cause of a panic wins over the watchdog reset", "[indx_fault][!shouldfail][known_bug]") {
    // A panic in an interrupt latches its cause bit and stops the head. The watchdog then resets the
    // head, and the bootstrap of the main board resets it with the reset pin. The main board reads
    // all three bits after the restart.
    const auto status = [](FaultStatusMask cause) {
        return static_cast<uint16_t>(raw(cause) | raw(FaultStatusMask::watchdog_reset) | raw(FaultStatusMask::pin_reset));
    };

    const auto watchdog_only = redscreen_for_status(status(FaultStatusMask::no_fault));
    REQUIRE(watchdog_only.has_value());

    for (const auto cause : { FaultStatusMask::hard_fault, FaultStatusMask::stack_overflow, FaultStatusMask::assert_failed, FaultStatusMask::loadcell_crc_mismatch, FaultStatusMask::nozzle_min_temp }) {
        CAPTURE(raw(cause));
        const auto redscreen = redscreen_for_status(status(cause));
        REQUIRE(redscreen.has_value());
        CHECK(redscreen == redscreen_for_status(raw(cause)));
        CHECK(redscreen != watchdog_only);
    }

    const auto board = redscreen_for_status(status(FaultStatusMask::board_max_temp));
    REQUIRE(board.has_value());
    CHECK(board->code == ErrCode::ERR_TEMPERATURE_INDX_HEAD_BOARD_MAXTEMP_ERR);
}
