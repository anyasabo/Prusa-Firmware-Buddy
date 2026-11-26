# Host tests of the real Marlin planner (lib/Marlin/Marlin/src/module/planner.cpp) as a CORE One L.
# See README.md.

# The real sources include each other and the configuration with relative paths
# ("../inc/MarlinConfig.h"). As in motion_klipper_diff, copy them and the stubs into one tree in the
# build directory, so that the relative includes find the stubs. The order of the copies is the
# order of precedence: the stubs of this directory replace the stubs of motion_klipper_diff.
set(mirror ${CMAKE_CURRENT_BINARY_DIR}/mirror)
set(marlin_src ${CMAKE_SOURCE_DIR}/lib/Marlin/Marlin/src)
set(motion_diff_dir ${CMAKE_CURRENT_SOURCE_DIR}/../motion_klipper_diff)
set(real_files
    core/drivers.h
    core/macros.h
    core/mtypes.hpp
    core/time_ticks.hpp
    core/types.h
    core/vector.hpp
    feature/input_shaper/input_shaper.cpp
    feature/input_shaper/input_shaper.hpp
    feature/input_shaper/input_shaper_config.cpp
    feature/input_shaper/input_shaper_config.hpp
    feature/precise_stepping/common.hpp
    feature/precise_stepping/fwdecl.hpp
    feature/precise_stepping/internal.hpp
    feature/precise_stepping/precise_stepping.cpp
    feature/precise_stepping/precise_stepping.hpp
    feature/pressure_advance/pressure_advance.cpp
    feature/pressure_advance/pressure_advance.hpp
    feature/pressure_advance/pressure_advance_config.cpp
    feature/pressure_advance/pressure_advance_config.hpp
    inc/Conditionals_LCD.h
    inc/Conditionals_adv.h
    module/planner.cpp
    module/planner.h
    module/prusa/corexy_transform.cpp
    module/prusa/corexy_transform.hpp
    )
foreach(f IN LISTS real_files)
  configure_file(${marlin_src}/${f} ${mirror}/${f} COPYONLY)
endforeach()
configure_file(${CMAKE_SOURCE_DIR}/src/common/cmath_ext.h ${mirror}/cmath_ext.h COPYONLY)

# The stubs of motion_klipper_diff, without the stand-in planner and the stand-in configuration. Its
# MarlinConfig.h (step and move timers) is kept as motion_diff_hal.h for stubs/inc/MarlinConfig.h.
file(
  GLOB_RECURSE motion_diff_stubs
  RELATIVE ${motion_diff_dir}/stubs
  ${motion_diff_dir}/stubs/*.h ${motion_diff_dir}/stubs/*.hpp
  )
list(REMOVE_ITEM motion_diff_stubs module/planner.h core/types.h inc/MarlinConfig.h
     inc/MarlinConfigPre.h
     )
foreach(f IN LISTS motion_diff_stubs)
  configure_file(${motion_diff_dir}/stubs/${f} ${mirror}/${f} COPYONLY)
endforeach()
configure_file(${motion_diff_dir}/stubs/inc/MarlinConfig.h ${mirror}/inc/motion_diff_hal.h COPYONLY)

file(
  GLOB_RECURSE planner_stubs
  RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}/stubs
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs/*.h ${CMAKE_CURRENT_SOURCE_DIR}/stubs/*.hpp
  )
foreach(f IN LISTS planner_stubs)
  configure_file(${CMAKE_CURRENT_SOURCE_DIR}/stubs/${f} ${mirror}/${f} COPYONLY)
endforeach()

# The option headers of a CORE One L build (utils/cmake/options or the printer CMake files), in the
# format of include/option/option_boolean.h.in. The options that switch off code paths of the
# planner are off: crash recovery, emergency stop, auto retract, filament tracker, cancel object.
# They change the position or block the queue, and need their own fakes.
set(options
    has_auto_retract=0
    has_cancel_object=0
    has_ceiling_clearance=1
    has_crash_detection=0
    has_emergency_stop=0
    has_filament_tracker=0
    has_indx=0
    has_loadcell=1
    has_mmu2=0
    has_nozzle_cleaner=0
    has_pause=1
    has_planner=1
    has_power_panic=1
    has_precise_homing=0
    has_precise_homing_corexy=1
    has_print_sheet_detection=1
    has_toolchanger=0
    has_wastebin_fill_tracking=0
    )
foreach(o IN LISTS options)
  string(REPLACE "=" ";" kv ${o})
  list(GET kv 0 name)
  list(GET kv 1 value)
  string(TOUPPER ${name} upper)
  file(
    CONFIGURE
    OUTPUT
    ${mirror}/option/${name}.h
    CONTENT
    "#pragma once\n#define ${upper}() ${value}\nnamespace option { inline constexpr bool ${name} = ${value}; }\n"
    )
endforeach()

add_library(
  planner_under_test STATIC
  ${mirror}/feature/input_shaper/input_shaper.cpp
  ${mirror}/feature/input_shaper/input_shaper_config.cpp
  ${mirror}/feature/precise_stepping/precise_stepping.cpp
  ${mirror}/feature/pressure_advance/pressure_advance.cpp
  ${mirror}/feature/pressure_advance/pressure_advance_config.cpp
  ${mirror}/module/planner.cpp
  ${mirror}/module/prusa/corexy_transform.cpp
  planner_fakes.cpp
  planner_harness.cpp
  planner_checks.cpp
  )
# The mirror first, so that its stubs replace the real headers. The configuration comes from
# include/marlin.
target_include_directories(
  planner_under_test BEFORE
  PUBLIC ${mirror}
         ${CMAKE_CURRENT_SOURCE_DIR}
         ${CMAKE_SOURCE_DIR}/include
         ${CMAKE_SOURCE_DIR}/src/common
         ${CMAKE_SOURCE_DIR}/src/common/utils
         ${CMAKE_SOURCE_DIR}/src/module/utils/buddy_utils
         ${CMAKE_SOURCE_DIR}/src/module/raii/include
  )
# The unit tests are set up as a MINI. Build these as a CORE One L (PRINTER_TYPE 8, version 1). Keep
# the debug_assert() checks on, also in a Release build.
target_compile_options(
  planner_under_test PUBLIC -UPRINTER_TYPE -DPRINTER_TYPE=8 -UPRINTER_VERSION -DPRINTER_VERSION=1
                            -UNDEBUG
  )
target_link_libraries(planner_under_test PUBLIC bsod)

add_executable(planner_tests planner_tests.cpp)
target_link_libraries(planner_tests planner_under_test)
add_catch_test(planner_tests)
