// Random motion (blocks, input shapers, pressure advance, steps/mm, start position) through the real
// motion core, with Klipper's step generation as the oracle. See
// tests/unit/lib/Marlin/motion_klipper_diff/README.md.
#include "compare.hpp"
#include "scenario.hpp"

#include <cstdio>
#include <cstdlib>

using namespace motion_diff;

namespace {
[[noreturn]] void fail(const char *what, int axis, const AxisComparison *c = nullptr) {
    std::fprintf(stderr, "motion_klipper: %s (axis %d) %s\n", what, axis, c ? describe(*c).c_str() : "");
    std::abort();
}
} // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    ByteReader in(data, size);
    ScenarioLimits limits;
    limits.max_blocks = 8;
    const Scenario s = make_scenario(in, limits);

    // A BSOD or a failed debug_assert() throws (tests/unit/tests_include/bsod.cpp). The exception is
    // not caught, so libFuzzer reports a crash.
    const BuddyResult buddy = run_buddy(s.config, s.blocks);
    if (!buddy.finished) {
        fail("the motion did not come back to a halt", -1);
    }
    const KlipperResult klipper = run_klipper(s.config, buddy.segments, start_position_mm(s.config));

    for (int axis = 0; axis < AXES; ++axis) {
        for (const Step &k : klipper.raw.axis[axis]) {
            if (!(k.time_us >= 0. && k.time_us < 1e13)) {
                fail("Klipper step time is not finite", axis);
            }
        }
        const AxisComparison c = compare_axis(s.config, buddy, klipper, axis);
        if (const std::string reason = check_axis(c); !reason.empty()) {
            fail(reason.c_str(), axis, &c);
        }
    }
    return 0;
}
