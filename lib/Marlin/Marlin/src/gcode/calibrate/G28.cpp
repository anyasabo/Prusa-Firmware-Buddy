/**
 * Marlin 3D Printer Firmware
 * Copyright (c) 2020 MarlinFirmware [https://github.com/MarlinFirmware/Marlin]
 *
 * Based on Sprinter and grbl.
 * Copyright (c) 2011 Camiel Gubbels / Erik van der Zalm
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 */

#include "../../inc/MarlinConfig.h"
#include "module/motion.h"

#include "../gcode.h"

#include <bsod.h>

#include "../../module/endstops.h"
#include "../../module/planner.h"
#include "../../module/stepper.h" // for various
#include <option/has_crash_detection.h>
#include <option/has_switchable_homing_calibration.h>
#include <option/has_loadcell_hx717.h>
#if HAS_LOADCELL_HX717()
  #include <loadcell.hpp>
#endif

#if HAS_MULTI_HOTEND
  #include "../../module/tool_change.h"
#endif

#if HAS_LEVELING
  #include "../../feature/bedlevel/bedlevel.h"
#endif

#if ENABLED(BD_SENSOR)
  // #error dead code found by automatic analyses (see BFW-5461)
  #include "../../feature/bedlevel/bdl/bdl.h"
#endif

#if ENABLED(SENSORLESS_HOMING)
  #include "../../feature/motordriver_util.h"
#endif

#if HAS_CRASH_DETECTION()
  #include "../../feature/prusa/crash_recovery.hpp"
#endif

#if HAS_PRECISE_HOMING_COREXY()
  #include "../../module/prusa/homing_corexy.hpp"
  #include "corexy_refine_policy.hpp"
  #include <mapi/parking.hpp>
#endif

#include "../../module/probe.h"
#include "../../lcd/ultralcd.h"

#if ENABLED(EXTENSIBLE_UI)
  //#include "../../lcd/extui/ui_api.h"
#elif ENABLED(DWIN_CREALITY_LCD)
  // #error dead code found by automatic analyses (see BFW-5461)
  #include "../../lcd/e3v2/creality/dwin.h"
#elif ENABLED(DWIN_LCD_PROUI)
  // #error dead code found by automatic analyses (see BFW-5461)
  #include "../../lcd/e3v2/proui/dwin.h"
#endif

#include <printers.h>
#include <option/has_indx.h>
#include <option/has_dwarf.h>
#include <option/has_print_sheet_detection.h>
#include <option/has_toolchanger.h>
#if ENABLED(SKEW_CORRECTION)
  #include "../../module/prusa/skew.hpp"
#endif
#if HAS_TOOLCHANGER()
  #include <module/prusa/toolchanger.h>
#endif

#if ENABLED(NOZZLE_LOAD_CELL)
#  include "../../feature/prusa/e-stall_detector.h"
#endif

#include <feature/tmc_util.h> // for disabling Wave Table during homing

#include <feature/phase_stepping/phase_stepping.hpp> // for disabling phase stepping during homing
#include <feature/pressure_advance/pressure_advance_config.hpp> // for disabling PA during homing

#include <option/has_nozzle_cleaner.h>

#if ENABLED(DELTA) || ENABLED(SCARA) || ENABLED(AXEL_TPARA) || ENABLED(FOAMCUTTER_XYUV)
  #error These babies are no longer welcome here. The relevants ifdefs were removed.
#endif

#include <raii/scope_guard.hpp>
#include "g28_plan.hpp"
#include <optional>
#include <marlin_server.hpp>
#include <feature/print_status_message/print_status_message_guard.hpp>
#include <config_store/store_instance.hpp>

#include <option/has_ceiling_clearance.h>
#if HAS_CEILING_CLEARANCE()
  #include <feature/ceiling_clearance/ceiling_clearance.hpp>
#endif

#if HAS_PRECISE_HOMING_COREXY()
bool corexy_refine_during_G28(float fr_mm_s, const G28Flags &flags);
#endif

#if ENABLED(QUICK_HOME)

  static void quick_home_xy() {
    axes_home_level[X_AXIS] = AxisHomeLevel::not_homed;
    axes_home_level[Y_AXIS] = AxisHomeLevel::not_homed;

    {
        // Pretend the current position is 0,0
        auto target = current_position;
        target.set(0, 0);
        set_current_position(target);
        sync_plan_position();
    }

    const int x_axis_home_dir = X_HOME_DIR;

    const float speed_ratio = homing_feedrate(X_AXIS) / homing_feedrate(Y_AXIS);
    const float speed_ratio_inv = homing_feedrate(Y_AXIS) / homing_feedrate(X_AXIS);
    const float length_ratio = max_length(X_AXIS) / max_length(Y_AXIS);
    const bool length_r_less_than_speed_r = length_ratio < speed_ratio;

    const float mlx = length_r_less_than_speed_r ? (max_length(Y_AXIS) * speed_ratio) : (max_length(X_AXIS));
    const float mly = length_r_less_than_speed_r ? (max_length(Y_AXIS)) : (max_length(X_AXIS) * speed_ratio_inv);
    const float fr_mm_s = SQRT(sq(homing_feedrate(X_AXIS)) + sq(homing_feedrate(Y_AXIS)));

    #if ENABLED(SENSORLESS_HOMING)
      #if HAS_CRASH_DETECTION()
        Crash_Temporary_Deactivate ctd;
      #endif

      sensorless_t stealth_states {
        NUM_AXIS_LIST(
          TERN0(X_SENSORLESS, enable_crash_detection(X_AXIS)),
          TERN0(Y_SENSORLESS, enable_crash_detection(Y_AXIS)),
          false, false, false, false
        )
        , 0
        , 0
      };

      #if HAS_CRASH_DETECTION()
        // Technically we should call end_sensorless_homing_per_axis() after
        // the move, but what follows is homing anyway, so it's not needed.
        crash_s.start_sensorless_homing_per_axis(X_AXIS);
        crash_s.start_sensorless_homing_per_axis(Y_AXIS);
      #endif
    #endif

    do_blocking_move_to_xy(1.5f * mlx * x_axis_home_dir, 1.5f * mly * home_dir(Y_AXIS), fr_mm_s);

    endstops.validate_homing_move();

    {
        // Pretend the current position is 0,0
        auto target = current_position;
        target.set(0, 0);
        set_current_position(target);
        sync_plan_position();
    }

    #if ENABLED(SENSORLESS_HOMING)
      #if ENABLED(ENDSTOPS_ALWAYS_ON_DEFAULT) || HAS_CRASH_DETECTION()
        UNUSED(stealth_states);
      #else
        // #error dead code found by automatic analyses (see BFW-5461)
        TERN_(X_SENSORLESS, disable_crash_detection(X_AXIS, stealth_states.x));
        TERN_(Y_SENSORLESS, disable_crash_detection(Y_AXIS, stealth_states.y));
      #endif
    #endif
  }

#endif // QUICK_HOME

#if ENABLED(Z_SAFE_HOMING)

  inline bool home_z_safely() {
    // Disallow Z homing if X or Y homing is needed
    if (homing_needed_error(_BV(X_AXIS) | _BV(Y_AXIS))) return false;

    sync_plan_position();

    /**
     * Move the Z probe (or just the nozzle) to the safe homing point
     * (Z is already at the right height)
     */
    constexpr xy_float_t safe_homing_xy = { Z_SAFE_HOMING_X_POINT, Z_SAFE_HOMING_Y_POINT };
    #if HAS_HOME_OFFSET
      xy_float_t okay_homing_xy = safe_homing_xy;
      okay_homing_xy -= home_offset.xy();
    #else
      // #error dead code found by automatic analyses (see BFW-5461)
      constexpr xy_float_t okay_homing_xy = safe_homing_xy;
    #endif

    xyze_pos_t dest_pos;
    dest_pos.set(okay_homing_xy, current_position.z);

    TERN_(HOMING_Z_WITH_PROBE, dest_pos -= probe_offset);
    TERN_(HAS_HOTEND_OFFSET, dest_pos -= hotend_currently_applied_offset);

    if (position_is_reachable(dest_pos.xy())) {
#if HAS_TOOLCHANGER()
      do_blocking_move_to_xy(dest_pos, PrusaToolChanger::limit_stealth_feedrate(XY_PROBE_FEEDRATE_MM_S));
#elif HAS_NOZZLE_CLEANER()
    // with nozzle cleaner (iX), move in Y first to avoid going over the cleaner
    do_blocking_move_to_xy(current_position.x, dest_pos.y);
    do_blocking_move_to_xy(dest_pos.x, dest_pos.y);
#else
      do_blocking_move_to_xy(dest_pos);
#endif

      if (!homeaxis(Z_AXIS)) {
        return false;
      }
    }
    else {
      LCD_MESSAGE(MSG_ZPROBE_OUT);
      SERIAL_ECHO_MSG(STR_ZPROBE_OUT_SER);
    }

    return true;
  }

  #if HAS_PRINT_SHEET_DETECTION()
    /**
     * @brief Detect print sheet
     *
     * @param z_homing_height z clearance before moving to detect print sheet point
     * @retval true print sheet detected
     * @retval false print sheet not detected or move was interrupted
     */
    static bool detect_print_sheet(const float z_homing_height) {
      // Disallow detection if if X or Y or Z homing is needed
      if (homing_needed_error(_BV(X_AXIS) | _BV(Y_AXIS) | _BV(Z_AXIS))) return false;

      #if ENABLED(NOZZLE_LOAD_CELL) && HOMING_Z_WITH_PROBE
        // Enable loadcell high precision across the entire procedure to prime the noise filters
        auto loadcellPrecisionEnabler = Loadcell::HighPrecisionEnabler(loadcell);
      #endif

      #if PRINTER_IS_PRUSA_COREONEL() && HAS_INDX()
        // on C1L INDX the detect point is partially in the first dock - we need to ensure that dock 0 is empty
        prusa_toolchanger.pick_tool_out_of_dock(PhysicalToolIndex::from_raw(0));

        // It lies inside the dock area, where any X motion crashes into the docks
        ScopeGuard exit_dock_area([&] {
          if (current_position.y < Y_DOCK_PARKING_MIN_SAFE_POS) {
            do_z_clearance(z_homing_height);
            do_blocking_move_to_y(Y_DOCK_PARKING_MIN_SAFE_POS);
          }
        });
      #endif

      /**
       * Move the Z probe (or just the nozzle) to the sheet
       * detect point
       */
      do_z_clearance(z_homing_height);
      constexpr xy_float_t sheet_detect_xy = { DETECT_PRINT_SHEET_X_POINT, DETECT_PRINT_SHEET_Y_POINT };
      #if HAS_HOME_OFFSET
        xy_float_t okay_homing_xy = sheet_detect_xy;
        okay_homing_xy -= home_offset.xy();
      #else
        // #error dead code found by automatic analyses (see BFW-5461)
        constexpr xy_float_t okay_homing_xy = safe_homing_xy;
      #endif

      destination.set(okay_homing_xy, current_position.z);

      TERN_(HOMING_Z_WITH_PROBE, destination -= probe_offset);

      if (position_is_reachable(destination.xy())) {
        do_blocking_move_to(destination);
        bool endstop_triggered;
        run_z_probe({
          .expected_trigger_z = 0 - (Z_PROBE_LOW_POINT) + DETECT_PRINT_SHEET_Z_POINT,
          .single_only = true,
          .endstop_triggered = &endstop_triggered
        });
        if(!endstop_triggered) {
          return false;
        }
      }
      else {
        LCD_MESSAGE(MSG_ZPROBE_OUT);
        SERIAL_ECHO_MSG(STR_ZPROBE_OUT_SER);
      }

      return true;
    }
  #endif

#endif // Z_SAFE_HOMING

#if HAS_TRINAMIC && defined(HAS_TMC_WAVETABLE)
static void reenable_wavetable(AxisEnum axis)
{
    tmc_enable_wavetable(axis == X_AXIS, axis == Y_AXIS, false);
}
#endif


/** \addtogroup G-Codes
 * @{
 */

/**
 *###G28: Home all axes according to settings <a href="https://reprap.org/wiki/G-code#G28:_Move_to_Origin_.28Home.29">G28: Move to Origin (Home)</a>
 *
 *If `HAS_PRECISE_HOMING()` is enabled, there are specific amount
 *of tries to home an X/Y axis. If it fails it runs re-calibration
 *
 *Home to all axes with no parameters.
 *
 *#### Usage
 *
 *    G28 [ X | Y | Z | I | N | O | P | R | S ]
 *
 *#### Parameters
 *
 * - `X` - Flag to go back to the X axis origin
 * - `Y` - Flag to go back to the Y axis origin
 * - `Z` - Flag to go back to the Z axis origin
 * - `I` - Imprecise: do not perform precise refinement
 * - `L` - Force leveling state ON (if possible) or OFF after homing (Requires `RESTORE_LEVELING_AFTER_G28` or `ENABLE_LEVELING_AFTER_G28`)
 * - `N` - No-change mode (do not change any motion setting such as feedrate)
 * - `O` - Home only if the position is not known and trusted
 * - `Q` - Same as `O`, but only for printers that are precise homing level-aware; otherwise home unconditionally (backwards compatibility reasons)
 * - `P` - Do not check print sheet presence
 * - `R` - <linear> Raise by n mm/inches before homing
 * - `S` - Simulated homing only in `MARLIN_DEV_MODE`
 * - `D` - Disallow homing self-calibration
 * - `C` - Force homing self-calibration
 */
void GcodeSuite::G28() {
#if ENABLED(NOZZLE_LOAD_CELL)
  BlockEStallDetection block_e_stall_detection;
#endif

  bool X = parser.seen('X');
  bool Y = parser.seen('Y');
  bool Z = parser.seen('Z');

  G28Flags flags;
  flags.only_if_needed = parser.boolval('O')
    // We will start emitting this new parameter into our gcodes, so that it would be applied only on the new firwmares, where the printers remember whether they are homed precisely or not
    || parser.boolval('Q');

  flags.z_raise = parser.seenval('R') ? parser.value_linear_units() : Z_HOMING_HEIGHT;
  flags.no_change = parser.seen('N');
  flags.can_calibrate = !parser.seen('D');
  flags.force_calibrate = parser.seen('C');
  #if ENABLED(MARLIN_DEV_MODE)
    // #error dead code found by automatic analyses (see BFW-5461)
    flags.simulate = parser.seen('S');
  #endif
  #if HAS_PRINT_SHEET_DETECTION()
    flags.check_sheet = !parser.boolval('P');
  #endif
  flags.precise = !parser.seen('I'); // do not perform precise refinement

  // No axes were specified -> interpret as home all
  if(!X && !Y && !Z) {
    X = Y = Z = true;
  }

  #if HAS_CRASH_DETECTION()
    const bool all_axes = (X && Y && Z);
    if (all_axes) {
      // Skip all recovery when homing all axes
      crash_s.set_gcode_replay_flags(Crash_s::RECOVER_NONE);
    } else if (X && Y) {
      // Skip the extra axis/position recovery if both XY are being rehomed
      crash_s.set_gcode_replay_flags(Crash_s::RECOVER_AXIS_STATE | Crash_s::RECOVER_Z_POSITION);
    }
  #endif

  G28_no_parser(X, Y, Z, flags);
}
/** @}*/

bool GcodeSuite::G28_no_parser(bool X, bool Y, bool Z, const G28Flags& flags) {
#if HAS_TOOLCHANGER()
  const bool no_tool = std::holds_alternative<NoTool>(PhysicalToolIndex::currently_selected());
#else
  const bool no_tool = false;
#endif

  static_assert(X_AXIS == g28::axis_x && Y_AXIS == g28::axis_y && Z_AXIS == g28::axis_z);
  const g28::Plan plan(
    {
      .z_safe_homing = ENABLED(Z_SAFE_HOMING),
      .codependent_xy_homing = ENABLED(CODEPENDENT_XY_HOMING),
      .precise_homing_corexy = HAS_PRECISE_HOMING_COREXY(),
    },
    {
      .requested = { X, Y, Z },
      .precise = flags.precise,
      .only_if_needed = flags.only_if_needed,
      .initial_levels = axes_home_level,
      .z_needs_tool_pick = no_tool,
    });

  const auto should_home_at_all = [&](AxisEnum axis) {
    return plan.home_at_all(axis);
  };
  const auto should_home_to_level = [&](AxisEnum axis, AxisHomeLevel to_level = AxisHomeLevel::full) {
    return plan.home_to_level(axis, to_level);
  };

  // Home (O)nly if position is unknown with respect to the required axes
  if (plan.nothing_to_do()) {
    return true;
  }


  #if ENABLED(MARLIN_DEV_MODE)
    // #error dead code found by automatic analyses (see BFW-5461)
    if (flags.simulate) {
      planner.synchronize();
      if (planner.draining())
        return false;
      LOOP_NUM_AXES(a) set_axis_is_at_home((AxisEnum)a);
      sync_plan_position();
      SERIAL_ECHOLNPGM("Simulated Homing");
      report_current_position();
      return true;
    }
  #endif

  PrintStatusMessageGuard statusGuard;
  statusGuard.update<PrintStatusMessage::homing>({});
  marlin_server::FSM_Holder fsm_holder{PhaseWait::print_status_message};

  #if HAS_CEILING_CLEARANCE()
  buddy::CeilingClearanceCheckDisabler ccd;
  #endif

  TERN_(BD_SENSOR, bdl.config_state = 0);

  #if ENABLED(FULL_REPORT_TO_HOST_FEATURE)
    // #error dead code found by automatic analyses (see BFW-5461)
    const M_StateEnum old_grblstate = M_State_grbl;
    set_and_report_grblstate(M_HOMING);
  #endif

  TERN_(HAS_DWIN_E3V2_BASIC, DWIN_HomingStart());
  //TERN_(EXTENSIBLE_UI, ExtUI::onHomingStart());

  planner.synchronize();          // Wait for planner moves to finish!

  // If we are homing Z, assume that max_printed_z is zero (the Z homing couldn't be safe otherwise)
  if(plan.resets_max_printed_z() && Z_HOME_DIR < 0) {
    planner.max_printed_z = 0;
  }

  /**
   * @brief Set to true when homing fails.
   * It is used to skip all motion until stepper currents
   * and variables are reverted to non-homing state.
   */
  bool failed = false;

  SET_SOFT_ENDSTOP_LOOSE(false);  // Reset a leftover 'loose' motion state

  // Disable the leveling matrix before homing
  #if CAN_SET_LEVELING_AFTER_G28
    // #error dead code found by automatic analyses (see BFW-5461)
    const bool leveling_restore_state = parser.boolval('L', TERN1(RESTORE_LEVELING_AFTER_G28, planner.leveling_active));
  #endif

  // Disable leveling before homing
  TERN_(HAS_LEVELING, set_bed_leveling_enabled(false));

  #if ENABLED(SKEW_CORRECTION)
    // Buddy: homing moves go along the machine axes and set machine positions, so turn the skew off.
    // (Marlin homes with the skew on, its homing targets are out of the limits, so skew() does nothing there.)
    TemporarySkewDisabler skew_disabler;
  #endif

  // Reset to the XY plane
  TERN_(CNC_WORKSPACE_PLANES, workspace_plane = PLANE_XY);

  #define HAS_CURRENT_HOME(N) (defined(N##_CURRENT_HOME) && N##_CURRENT_HOME != N##_CURRENT)
  #if HAS_CURRENT_HOME(X) || HAS_CURRENT_HOME(Y) || HAS_CURRENT_HOME(I) || HAS_CURRENT_HOME(J) || HAS_CURRENT_HOME(K) || HAS_CURRENT_HOME(U) || HAS_CURRENT_HOME(V) || HAS_CURRENT_HOME(W)
    #define HAS_HOMING_CURRENT 1
  #endif

  #if HAS_HOMING_CURRENT
    #if HAS_CURRENT_HOME(X)
      const int16_t tmc_save_current_X = stepperX.getMilliamps();
      if(!flags.no_change) {
        stepperX.rms_current(X_CURRENT_HOME);
      }
    #endif
    #if HAS_CURRENT_HOME(Y)
      const int16_t tmc_save_current_Y = stepperY.getMilliamps();
      if(!flags.no_change) {
        stepperY.rms_current(Y_CURRENT_HOME);
      }
    #endif
    #if HAS_CURRENT_HOME(I)
      // #error dead code found by automatic analyses (see BFW-5461)
      const int16_t tmc_save_current_I = stepperI.getMilliamps();
      if(!no_change) {
        stepperI.rms_current(I_CURRENT_HOME);
      }
    #endif
    #if HAS_CURRENT_HOME(J)
      // #error dead code found by automatic analyses (see BFW-5461)
      const int16_t tmc_save_current_J = stepperJ.getMilliamps();
      if(!no_change) {
        stepperJ.rms_current(J_CURRENT_HOME);
      }
    #endif
    #if HAS_CURRENT_HOME(K)
      // #error dead code found by automatic analyses (see BFW-5461)
      const int16_t tmc_save_current_K = stepperK.getMilliamps();
      if(!no_change) {
        stepperK.rms_current(K_CURRENT_HOME);
      }
    #endif
    #if HAS_CURRENT_HOME(U)
      // #error dead code found by automatic analyses (see BFW-5461)
      const int16_t tmc_save_current_U = stepperU.getMilliamps();
      if(!no_change) {
        stepperU.rms_current(U_CURRENT_HOME);
      }
    #endif
    #if HAS_CURRENT_HOME(V)
      // #error dead code found by automatic analyses (see BFW-5461)
      const int16_t tmc_save_current_V = stepperV.getMilliamps();
      if(!no_change) {
        stepperV.rms_current(V_CURRENT_HOME);
      }
    #endif
    #if HAS_CURRENT_HOME(W)
      // #error dead code found by automatic analyses (see BFW-5461)
      const int16_t tmc_save_current_W = stepperW.getMilliamps();
      if(!no_change) {
        stepperW.rms_current(W_CURRENT_HOME);
      }
    #endif
    #if SENSORLESS_STALLGUARD_DELAY
      // #error dead code found by automatic analyses (see BFW-5461)
      safe_delay(SENSORLESS_STALLGUARD_DELAY); // Short delay needed to settle
    #endif
  #endif

  #if ENABLED(XY_HOMING_STEALTCHCHOP)
    // Enable stealtchop on X and Y before homing
    stepperX.stored.stealthChop_enabled = true;
    stepperX.refresh_stepping_mode();
    stepperY.stored.stealthChop_enabled = true;
    stepperY.refresh_stepping_mode();
  #endif /*ENABLED(XY_HOMING_STEALTCHCHOP)*/

  #if ENABLED(IMPROVE_HOMING_RELIABILITY)
    Motion_Parameters saved_motion_state;
    saved_motion_state.save();
    ScopeGuard restore_motion_state_guard = [&] {
      saved_motion_state.load();
    };
    if (!flags.no_change) {
      // Reset default feedrate and acceleration limits during homing
      Motion_Parameters::reset(true);

      auto s = planner.user_settings;
      s.max_acceleration_mm_per_s2[X_AXIS] = XY_HOMING_ACCELERATION;
      s.max_acceleration_mm_per_s2[Y_AXIS] = XY_HOMING_ACCELERATION;
      s.travel_acceleration = XY_HOMING_ACCELERATION;
      #if HAS_CLASSIC_JERK
        s.max_jerk.set(XY_HOMING_JERK, XY_HOMING_JERK);
      #endif

      // use feedrates as given! These shouldn't be alterated by any other policy!
      planner.apply_settings(s, true);
    }
  #endif

  // Always home with tool 0 active (but not with HAS_TOOLCHANGER())
  #if HAS_MULTI_HOTEND && !HAS_TOOLCHANGER()
    // #error dead code found by automatic analyses (see BFW-5461)
    const auto old_tool_index = PhysicalToolIndex::currently_selected();
    tool_change(0, tool_return_t::no_return);
  #endif

  // Disable phase stepping/PA just before homing XY. This will synchronize only if needed
  phase_stepping::EnsureSuitableForHoming phstep_disabler;
  pressure_advance::PressureAdvanceDisabler pa_disabler;

  // Homing feedrate
  float fr_mm_s = flags.no_change ? feedrate_mm_s : 0.0f;
  remember_feedrate_scaling_off();
  ScopeGuard resatore_feedrate_scaling = [] {
    restore_feedrate_and_scaling();
  };

  endstops.enable(true); // Enable endstops for next homing move

  TERN_(HOME_Z_FIRST, if (!failed && doZ) failed = !homeaxis(Z_AXIS));

  const bool seenR = !isnan(flags.z_raise);
  const float z_homing_height = seenR ? flags.z_raise : Z_HOMING_HEIGHT;

  if (!failed && z_homing_height && (seenR || should_home_at_all(X_AXIS) || should_home_at_all(Y_AXIS) || TERN0(Z_SAFE_HOMING, should_home_at_all(Z_AXIS)))) {
    // Raise Z before homing any other axes and z is not already high enough (never lower z)
    const auto trigger_states = do_z_clearance(z_homing_height);

    // If we have the Z homed and trigger an endstop, that means that we have homed wrong.
    // Letting this continue could lead to collision with the print or with the bedsheet, so rather raise a redscreen.
    // BFW-5334
    if((trigger_states & (1 << EndstopEnum::Z_MAX)) && !should_home_at_all(Z_AXIS) && axes_home_level.is_homed(Z_AXIS, AxisHomeLevel::imprecise)) {
      raise_redscreen(ErrCode::ERR_UNDEF, "Unexpected Z MAX endstop trigger", "G28");
    }
  }

  #if HAS_LOADCELL_HX717()
    // Measure whether the load cell feels the XY homing, before Z homing trusts it
    if (!failed && plan.measures_loadcell_liveness()) {
      loadcell.start_liveness_window();
    }
  #endif

  // Diagonal move first if both are homing
  #if ENABLED(QUICK_HOME)
    if (!failed && should_home_to_level(X_AXIS, AxisHomeLevel::imprecise) && should_home_to_level(Y_AXIS, AxisHomeLevel::imprecise))
      quick_home_xy();
  #endif

#if HAS_TRINAMIC && defined(HAS_TMC_WAVETABLE)
  // Only allow wavetable change if homing performs a backoff. This backoff is made in the way that it ends on stepper zero-position, so that re-enabling wavetable is safe.
  bool wavetable_off_X = false, wavetable_off_Y = false;
  #if HAS_PRECISE_HOMING_COREXY()
    #error "wavetable switching currently not compatible with HAS_PRECISE_HOMING_COREXY()"
  #endif
  #ifdef HOMING_BACKOFF_POST_MM
    constexpr xyz_float_t homing_backoff = HOMING_BACKOFF_POST_MM;
    wavetable_off_X = (homing_backoff[X] > 0.0f) && should_home_at_all(X_AXIS);
    wavetable_off_Y = (homing_backoff[Y] > 0.0f) && should_home_at_all(Y_AXIS);
  #endif
  void (*reenable_wt_X)(AxisEnum) = wavetable_off_X ? reenable_wavetable : NULL;
  void (*reenable_wt_Y)(AxisEnum) = wavetable_off_Y ? reenable_wavetable : NULL;
  // function pointers are useful because homing code doesn't need access to trinamic headres in order to re-enable wavetable during homing procedures

  // Turn off Wave Table for X and Y, which should improve homing
  // NOTE: change of Wave Table shall normally be done only when motors are guaranteed at zero-step. Here we are far enough from the print, so if the motors do something "wild" they should make no harm.
  // re-enabling wavetable back will take place during homing, when we are guaranteed at stepper zero
  if (!failed) {
    tmc_disable_wavetable(wavetable_off_X, wavetable_off_Y, false);
  }
#else
  void (*reenable_wt_X)(AxisEnum) = NULL;
  void (*reenable_wt_Y)(AxisEnum) = NULL;
#endif

  #if HAS_TOOLCHANGER()  && HAS_DWARF()
  if (!failed && should_home_to_level(X_AXIS, AxisHomeLevel::imprecise) && should_home_to_level(Y_AXIS, AxisHomeLevel::imprecise)) {
    // Bump right edge to align toolchanger locking plates
    // We need to align them before the actual homing, because the align locks homing move could throw off the precise position
    if (!prusa_toolchanger.align_locks()) {
      ui.status_printf_P(0, "Toolchanger lock alignment failed");
      homing_failed([]() { fatal_error(ErrCode::ERR_ELECTRO_HOMING_ERROR_X); }); // The alignment happens in X axis
      failed = true;
    }
  }

  // X position is unknown or near right edge
  if (!failed && (axes_need_homing(_BV(X_AXIS)) || current_position.x > X_MAX_POS - MOVE_BACK_BEFORE_HOMING_DISTANCE)) {
    do_homing_move(X_AXIS, -1 * MOVE_BACK_BEFORE_HOMING_DISTANCE); // Move a bit left to avoid unlocking the tool
  }
  #endif

  // Home Y (before X)
  if (ENABLED(HOME_Y_BEFORE_X) && !failed && should_home_to_level(Y_AXIS, AxisHomeLevel::imprecise)) {
    failed = !homeaxis(Y_AXIS, fr_mm_s, false, reenable_wt_Y, flags.can_calibrate, true, flags.throw_homing_failed);
  }

  // Home X
  if (!failed && should_home_to_level(X_AXIS, AxisHomeLevel::imprecise)) {
  #ifdef HOMING_PREEMPTIVE_MOVE_Y
    do_blocking_move_to_y(HOMING_PREEMPTIVE_MOVE_Y);
  #endif
    failed = !homeaxis(X_AXIS, fr_mm_s, false, reenable_wt_X, flags.can_calibrate, true, flags.throw_homing_failed);
  }

  // Home Y (after X)
  if (DISABLED(HOME_Y_BEFORE_X) && !failed && should_home_to_level(Y_AXIS, AxisHomeLevel::imprecise)) {
    failed = !homeaxis(Y_AXIS, fr_mm_s, false, reenable_wt_Y, flags.can_calibrate, true, flags.throw_homing_failed);
  }

  #if HAS_PRECISE_HOMING_COREXY()
    // absolute refinement requires both axes to be already probed
    if (!failed && plan.refines_xy()) {
      #if !HAS_TOOLCHANGER()
        // skip refinement without data if we're not allowed to calibrate
        const bool do_refine = (corexy_home_is_calibrated() || flags.can_calibrate || flags.force_calibrate);
      #else
        // TODO[BFW-6527]: this is a temporary workaround until home calibration is enforced
        const bool do_refine = true;
      #endif

      if(do_refine) {
        failed |= !corexy_refine_during_G28(fr_mm_s, flags);

      }
    }
  #endif

  #if HAS_LOADCELL_HX717()
    loadcell.end_liveness_window(!failed);
  #endif

  // Home Z last if homing towards the bed
  #if HAS_Z_AXIS && DISABLED(HOME_Z_FIRST)
    if (!failed && should_home_at_all(Z_AXIS)) {
      #if EITHER(Z_TRIPLE_ENDSTOPS, Z_STEPPER_AUTO_ALIGN)
        // #error dead code found by automatic analyses (see BFW-5461)
        stepper.set_all_z_lock(false);
        stepper.set_separate_multi_axis(false);
      #endif

      #if HAS_TOOLCHANGER()
      if (std::holds_alternative<NoTool>(PhysicalToolIndex::currently_selected())) {
        if(!prusa_toolchanger.can_move_safely()) {
          // If !can_move_safely, the toolchange would trigger a nested G28
          // That breaks things, because calling align_locks with a homing_move inside would actually screw up the current homing
          bsod("Cannot toolchange");
        }

        failed = !prusa_toolchanger.pick_any_tool(tool_return_t::no_return, current_position.xyz(), tool_change_lift_t::no_lift, false);
      }
      #endif

      if (!failed) {
      #if ENABLED(Z_SAFE_HOMING)
        failed = !home_z_safely();

        #if HAS_PRINT_SHEET_DETECTION()
        if (!failed && flags.check_sheet) {
          PrintStatusMessageGuard status_guard;
          status_guard.update<PrintStatusMessage::detecting_steel_sheet>({});

          failed = [&] {
            // Do multiple attempts of detect print sheet
            // The point is that we want to prevent false failures caused by a dirty nozzle (cold filament left hanging out)
            // BFW-5028
            uint8_t attempt = 0;
            while(true) {
              // If detect_print_sheet, return success (failed -> false)
              if(detect_print_sheet(z_homing_height)) {
                return false;
              }

              // Fail straight away if draining
              if(planner.draining()) {
                return true;
              }

              bool ignore_fail = false;

              // Ran out of attempts -> report detect fail
              if(++attempt == 3) {
                // Report missing bed sheet
                static constexpr auto warning_type = WarningType::SteelSheetNotDetected;
                marlin_server::set_warning(warning_type);

                // Move the bed to the bottom to give space for the user to insert the sheet
                // Do this asynchronously so that we can process the response while moving
                {
                  line_to_machine_pos(current_machine_position().with_z(DETECT_PRINT_SHEET_Z_AFTER_FAILURE), homing_feedrate(Z_AXIS));
                }

                // Continue after the user puts the print sheet on
                const Response response = marlin_server::wait_for_response(warning_type_phase(warning_type));

                marlin_server::clear_warning(warning_type);

                // Wait for the movement to finish
                planner.synchronize();
                ignore_fail = (response == Response::Ignore);
                attempt = 0;
                if (response == Response::Abort) {
                  if (marlin_server::is_printing()) {
                    marlin_server::quick_stop();
                    marlin_server::print_abort();
                  }
                  return false;
                }
              }

              // Raise the Z again to prevent crashing into the sheet
              do_z_clearance(z_homing_height);

              // Return to the XY homing position over the printbed and try rehoming z
              if(!home_z_safely()) {
                // Fail straight away if z homing fails, only repeat if detect_print_sheet fails
                return true;
              }

              if(ignore_fail) {
                return false;
              }
            }
          }();
        }
        #endif
      #else
        // #error dead code found by automatic analyses (see BFW-5461)
        failed = !homeaxis(Z_AXIS);
      #endif
      }

      if (!failed) {
        move_z_after_probing();
      }
    }
  #endif

  SECONDARY_AXIS_CODE(
    if (!failed && doI) failed = !homeaxis(I_AXIS),
    if (!failed && doJ) failed = !homeaxis(J_AXIS),
    if (!failed && doK) failed = !homeaxis(K_AXIS),
    if (!failed && doU) failed = !homeaxis(U_AXIS),
    if (!failed && doV) failed = !homeaxis(V_AXIS),
    if (!failed && doW) failed = !homeaxis(W_AXIS)
  );

  sync_plan_position();

  // clear any step fraction: we're at home
  PreciseStepping::reset_from_halt(false);

  endstops.not_homing();

  // Restore previous phase stepping state before we move again
  phstep_disabler.release();

  TERN_(CAN_SET_LEVELING_AFTER_G28, if (leveling_restore_state) set_bed_leveling_enabled());

  // Restore the active tool after homing
  #if HAS_MULTI_HOTEND && !HAS_TOOLCHANGER()
    // #error dead code found by automatic analyses (see BFW-5461)
    tool_change(old_tool_index, true);   // Do move if one of these
  #endif

  #if HAS_HOMING_CURRENT
    #if HAS_CURRENT_HOME(X)
      stepperX.rms_current(tmc_save_current_X);
    #endif
    #if HAS_CURRENT_HOME(Y)
      stepperY.rms_current(tmc_save_current_Y);
    #endif
    #if HAS_CURRENT_HOME(I)
      // #error dead code found by automatic analyses (see BFW-5461)
      stepperI.rms_current(tmc_save_current_I);
    #endif
    #if HAS_CURRENT_HOME(J)
      // #error dead code found by automatic analyses (see BFW-5461)
      stepperJ.rms_current(tmc_save_current_J);
    #endif
    #if HAS_CURRENT_HOME(K)
      // #error dead code found by automatic analyses (see BFW-5461)
      stepperK.rms_current(tmc_save_current_K);
    #endif
    #if HAS_CURRENT_HOME(U)
      // #error dead code found by automatic analyses (see BFW-5461)
      stepperU.rms_current(tmc_save_current_U);
    #endif
    #if HAS_CURRENT_HOME(V)
      // #error dead code found by automatic analyses (see BFW-5461)
      stepperV.rms_current(tmc_save_current_V);
    #endif
    #if HAS_CURRENT_HOME(W)
      // #error dead code found by automatic analyses (see BFW-5461)
      stepperW.rms_current(tmc_save_current_W);
    #endif
    #if SENSORLESS_STALLGUARD_DELAY
      // #error dead code found by automatic analyses (see BFW-5461)
      safe_delay(SENSORLESS_STALLGUARD_DELAY); // Short delay needed to settle
    #endif
  #endif // HAS_HOMING_CURRENT

  TERN_(HAS_DWIN_E3V2_BASIC, DWIN_HomingDone());
  //TERN_(EXTENSIBLE_UI, ExtUI::onHomingDone());

  report_current_position();

  if (ENABLED(NANODLP_Z_SYNC) && (should_home_at_all(Z_AXIS) || ENABLED(NANODLP_ALL_AXIS)))
    SERIAL_ECHOLNPGM(STR_Z_MOVE_COMP);

  TERN_(FULL_REPORT_TO_HOST_FEATURE, set_and_report_grblstate(old_grblstate));

  return !failed;
}

#if HAS_PRECISE_HOMING_COREXY()
namespace {

#if HAS_TRINAMIC && defined(XY_HOMING_MEASURE_SENS_MIN)
constexpr bool calibrates_sensitivity = true;
#else
constexpr bool calibrates_sensitivity = false;
#endif

class G28RefineHooks final : public corexy_refine::RefineHooks {
public:
  explicit G28RefineHooks(float fr_mm_s)
    // Do not handle feedrate defaults again within precise homing: do it here
    : fr_mm_s(fr_mm_s ?: homing_feedrate(A_AXIS)) {}

  bool draining() override {
    return planner.draining();
  }

  void move_to_start() override {
    // Move to the home position via a parking move. Refinement can now be done separately to the imprecise homing and the head can be anywhere,
    // so we need the collision avoidance of a parking move (e.g. to navigate around the nozzle cleaner / wastebin on INDX/iX).
    // The position taken from corexy_rehome_and_phase
    mapi::park({
      .x = base_home_pos(X_AXIS) - XY_HOMING_ORIGIN_OFFSET * X_HOME_DIR,
      .y = base_home_pos(Y_AXIS) - XY_HOMING_ORIGIN_OFFSET * Y_HOME_DIR,
    });
  }

  bool is_calibrated() override {
    return corexy_home_is_calibrated();
  }

  bool refine(bool calibrate) override {
    return corexy_home_refine(fr_mm_s, calibrate ? CoreXYCalibrationMode::force : CoreXYCalibrationMode::disallow);
  }

  bool is_unstable() override {
    return corexy_home_is_unstable();
  }

  bool rehome() override {
    return corexy_rehome_xy(fr_mm_s);
  }

  bool calibrate_sensitivity() override {
#if HAS_TRINAMIC && defined(XY_HOMING_MEASURE_SENS_MIN)
    return corexy_sens_calibrate(fr_mm_s);
#else
    bsod_unreachable();
#endif
  }

  void clear_calibration() override {
    corexy_clear_homing_calibration();
  }

  uint16_t push_instability(bool unstable) override {
    config_store().precise_homing_instability_history.transform([unstable](uint16_t history) {
      return corexy_refine::push_instability_bit(history, unstable);
    });
    return config_store().precise_homing_instability_history.get();
  }

  void reset_instability_history() override {
    config_store().precise_homing_instability_history.set(0);
  }

  Tristate auto_recalibrate() override {
#if HAS_SWITCHABLE_HOMING_CALIBRATION()
    return config_store().auto_recalibrate_precise_homing.get();
#else
    bsod_unreachable();
#endif
  }

  void set_auto_recalibrate([[maybe_unused]] Tristate value) override {
#if HAS_SWITCHABLE_HOMING_CALIBRATION()
    config_store().auto_recalibrate_precise_homing.set(value);
#else
    bsod_unreachable();
#endif
  }

  corexy_refine::CalibrationAnswer ask_calibration() override {
    using Answer = corexy_refine::CalibrationAnswer;
    switch(marlin_server::prompt_warning(WarningType::HomingCalibrationNeeded, 60'000)) {
    case Response::Calibrate:
      return Answer::calibrate;
    case Response::Skip:
      return Answer::skip;
    case Response::Always:
      return Answer::always;
    case Response::Never:
      return Answer::never;
    case Response::_none:
    default:
      return Answer::timeout;
    }
  }

  corexy_refine::FailureAnswer ask_failure(corexy_refine::FailureWarning warning) override {
    const auto warning_type = (warning == corexy_refine::FailureWarning::calibrate_from_menu) ? WarningType::HomingCalibrationFromMenuNeeded : WarningType::HomingRefinementFailed;
    switch(marlin_server::prompt_warning(warning_type)) {
    case Response::Retry:
      return corexy_refine::FailureAnswer::retry;
    case Response::Abort:
      return corexy_refine::FailureAnswer::abort;
    case Response::Ignore:
      return corexy_refine::FailureAnswer::ignore;
    default:
      bsod_unreachable();
    }
  }

  void abort_print() override {
    marlin_server::quick_stop();
    marlin_server::print_abort();
  }

  void begin_recalibration_status() override {
    status_guard.emplace();
    status_guard->update<PrintStatusMessage::recalibrating_home>({});
  }

  void end_recalibration_status() override {
    status_guard.reset();
  }

private:
  const float fr_mm_s;
  std::optional<PrintStatusMessageGuard> status_guard;
};

} // namespace

bool corexy_refine_during_G28(float fr_mm_s, const G28Flags &flags) {
  static constexpr corexy_refine::Config config {
    .retries = PRECISE_HOMING_COREXY_RETRIES,
    .switchable_calibration = HAS_SWITCHABLE_HOMING_CALIBRATION(),
    .calibrates_sensitivity = calibrates_sensitivity,
  };
  G28RefineHooks hooks(fr_mm_s);
  if (!corexy_refine::refine(hooks, config, { .can_calibrate = flags.can_calibrate, .force_calibrate = flags.force_calibrate })) {
    return false;
  }

  // Mark the axes as precisely homed
  axes_home_level[X_AXIS] = AxisHomeLevel::full;
  axes_home_level[Y_AXIS] = AxisHomeLevel::full;
  return true;
}
#endif
