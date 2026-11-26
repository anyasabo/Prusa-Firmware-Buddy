/// @file
/// Fake printer for the INDX toolchanger host tests.
///
/// The fake keeps a physical model of the CORE One L INDX head and docks:
/// - the carriage position in machine coordinates (after MBL),
/// - the tool that the head holds, the tool in each dock, the lock state (E service moves),
/// - the nozzle presence sensor of the head.
///
/// The fakes of Marlin (motion, planner, homing, parking), of the config store, of the
/// marlin_server dialogs and of the hotends all act on this model. The tests then compare
/// the firmware state (active_extruder, hotend_currently_applied_offset, current_position)
/// with the physical state.
#pragma once

#include <module/prusa/toolchanger.h>
#include <tool/hotend/hotend/indx_hotend.hpp>

#include <array>
#include <deque>
#include <functional>
#include <optional>
#include <string>
#include <vector>

/// Thrown by the fake planner to simulate a power loss in the middle of a toolchange.
/// The real firmware does not unwind: tool_change() continues, the planner drops the moves and wait()
/// returns early, then panic_loop() saves the state. The exception is an approximation: it stops the
/// toolchange at once. The ScopeGuards of the toolchanger still run (as in the real code).
/// A ScopeGuard that moves the head during the unwind throws again and ends the test binary.
struct FakePowerLoss {};

struct FakeMove {
    enum class Kind { line,
        e_service,
        homing,
        park } kind;
    float x0, y0, z0;
    float x1, y1, z1;
    float e_delta;
};

/// The data that src/common/power_panic.cpp saves. The AC fault ISR saves the phase. The rest is saved
/// later by panic_loop(), after tool_change() returned: see FakeWorld::complete_power_panic_save().
struct PowerPanicSnapshot {
    PrusaToolChanger::ToolchangePhase phase;
    PrusaToolChanger::ToolchangeReturnData return_data;
    std::variant<PhysicalToolIndex, NoTool> active_tool { NoTool {} };
    float z_native = 0;
    float max_printed_z = 0;
};

struct FakeWorld {
    static constexpr int tool_count = PhysicalToolIndex::count;
    static constexpr float lock_closed = PrusaToolChanger::E_FULL_CLOSE_DISTANCE;
    static constexpr float dock_tolerance = 0.5f;

    // ---- physical state
    MachinePosXYZ carriage { .x = 150, .y = 150, .z = 10 };
    std::optional<int> head_tool; ///< tool locked in the head
    std::array<std::optional<int>, tool_count> dock_content; ///< tool in each dock
    float lock_pos = 0; ///< lock mechanism: 0 = open, lock_closed = closed
    std::array<xyz_pos_t, tool_count> tool_offsets {}; ///< real offset of each physical tool
    std::array<xy_pos_t, tool_count> dock_pos {}; ///< real position of each dock (dock_x, dock_y), independent of the firmware calibration

    // ---- sensor
    bool sensor_valid = false;
    bool sensor_dead = false; ///< sensor never gives a value (puppy communication lost)
    int sensor_blackouts = 0; ///< after each of the next N invalidations, the sensor gives no value until the next invalidation
    bool sensor_blackout_active = false;

    // ---- faults
    int lock_grab_failures = 0; ///< next N lock moves do not grab the nozzle
    int release_failures = 0; ///< next N full-open moves do not release the nozzle
    bool homing_fails = false;

    // ---- marlin_server
    bool printing = false;
    bool aborted = false;
    std::deque<Response> responses; ///< scripted answers to the toolchange dialogs
    std::deque<std::optional<uint8_t>> dock_answers; ///< scripted answers to dock selection
    std::vector<PhaseNozzleMismatch> shown_phases;

    // ---- time and power
    uint32_t now_ms = 0;
    bool draining = false; ///< planner.draining(), set by a power panic. The fake planner does not drop moves.
    bool power_panic = false;
    int power_loss_at_move = -1; ///< move index that triggers the power panic
    std::optional<PowerPanicSnapshot> pp_snapshot;
    std::function<void()> on_before_move; ///< test hook, runs before each move
    std::function<bool()> power_loss_in_idle; ///< test hook: if it returns true in idle(), the power fails there

    /// Save the phase like the AC fault ISR, then stop the toolchange
    [[noreturn]] void trigger_power_panic();

    /// Save the rest of the power panic data like panic_loop() does, after tool_change() returned
    void complete_power_panic_save();

    // ---- records
    std::vector<FakeMove> moves;
    std::vector<std::string> violations; ///< physical collisions and dropped tools
    int lateral_moves_below_parking_safe_y = 0; ///< see the motion-audit note about Y_DOCK_PARKING_MIN_SAFE_POS
    int g28_calls = 0;

    /// Reset everything: calibrated docks at the defaults, tool i in dock i, head empty, axes homed, no MBL.
    void reset();

    /// Simulate a reboot after a power loss: RAM state of the firmware is lost, the physical state stays.
    void reboot_after_power_loss();

    // ---- helpers for the fakes
    float mbl(float x, float y) const;
    void record_line(FakeMove::Kind kind, const MachinePosXYZ &target);
    void e_service_move(float delta);
    void before_move();
    std::optional<int> dock_at_carriage(float y_tolerance) const;

    /// Position of the nozzle tip of the tool in the head (machine coordinates)
    xyz_pos_t nozzle_tip() const;
};

extern FakeWorld world;

IndxHotend &fake_hotend(int tool);
