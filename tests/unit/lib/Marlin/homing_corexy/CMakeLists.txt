# Host tests of the real CoreXY homing refinement
# (lib/Marlin/Marlin/src/module/prusa/homing_corexy.cpp) as a CORE One L.
#
# The real sources include the planner, the steppers and the endstops with relative paths
# ("../planner.h"). As in the planner tests, copy them and the stubs into one tree in the build
# directory, so that the relative includes find the stubs. fake_world.cpp is the fake machine.
set(mirror ${CMAKE_CURRENT_BINARY_DIR}/mirror)
set(marlin_src ${CMAKE_SOURCE_DIR}/lib/Marlin/Marlin/src)
set(real_files
    module/prusa/corexy_transform.cpp module/prusa/corexy_transform.hpp
    module/prusa/homing_corexy.cpp module/prusa/homing_corexy.hpp
    module/prusa/homing_corexy_config.hpp
    )
foreach(f IN LISTS real_files)
  configure_file(${marlin_src}/${f} ${mirror}/${f} COPYONLY)
endforeach()
configure_file(
  ${CMAKE_SOURCE_DIR}/src/marlin_stubs/COREONEL/configuration.hpp ${mirror}/configuration.hpp
  COPYONLY
  )

file(
  GLOB_RECURSE stubs
  RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}/stubs
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs/*.h ${CMAKE_CURRENT_SOURCE_DIR}/stubs/*.hpp
  )
foreach(f IN LISTS stubs)
  configure_file(${CMAKE_CURRENT_SOURCE_DIR}/stubs/${f} ${mirror}/${f} COPYONLY)
endforeach()

# The option headers of a CORE One L build, in the format of include/option/option_boolean.h.in.
# Crash detection is off: the refinement only switches it off for its duration.
set(options
    has_burst_stepping=0
    has_crash_detection=0
    has_indx=0
    has_loadcell=1
    has_mmu2=0
    has_nozzle_cleaner=0
    has_pause=1
    has_phase_stepping=1
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

add_executable(
  homing_corexy_tests ${mirror}/module/prusa/corexy_transform.cpp homing_corexy_tu.cpp
                      fake_world.cpp homing_corexy_tests.cpp
  )
# The mirror first, so that its stubs replace the real headers. The configuration comes from
# include/marlin.
target_include_directories(
  homing_corexy_tests BEFORE
  PRIVATE ${mirror}
          ${CMAKE_CURRENT_SOURCE_DIR}
          ${marlin_src}
          ${CMAKE_SOURCE_DIR}/lib/Marlin
          ${CMAKE_SOURCE_DIR}/include
          ${CMAKE_SOURCE_DIR}/src/common
          ${CMAKE_SOURCE_DIR}/src/module/raii/include
          ${CMAKE_SOURCE_DIR}/src/module/utils/buddy_utils
  )
# The unit tests are set up as a MINI. Build these as a CORE One L (PRINTER_TYPE 8, version 1). Keep
# the debug_assert() checks on.
target_compile_options(
  homing_corexy_tests PRIVATE -UPRINTER_TYPE -DPRINTER_TYPE=8 -UPRINTER_VERSION -DPRINTER_VERSION=1
                              -UNDEBUG
  )
target_link_libraries(homing_corexy_tests bsod)
add_catch_test(homing_corexy_tests)
