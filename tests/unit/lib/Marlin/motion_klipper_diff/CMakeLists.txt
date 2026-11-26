# Differential test of the motion core (precise_stepping, input_shaper, pressure_advance) against
# Klipper's step generation, which these files were ported from. See README.md.

# The real motion sources include each other and the firmware configuration with relative paths
# ("../../inc/MarlinConfig.h"). Copy them and the stubs into one tree, so that those relative
# includes find the stubs. configure_file() copies again when a source changes.
set(mirror ${CMAKE_CURRENT_BINARY_DIR}/mirror)
set(marlin_src ${CMAKE_SOURCE_DIR}/lib/Marlin/Marlin/src)
set(real_files
    core/macros.h
    core/mtypes.hpp
    core/time_ticks.hpp
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
    feature/precise_stepping/resync.hpp
    feature/pressure_advance/pressure_advance.cpp
    feature/pressure_advance/pressure_advance.hpp
    feature/pressure_advance/pressure_advance_config.cpp
    feature/pressure_advance/pressure_advance_config.hpp
    feature/pressure_advance/pressure_advance_sampling.hpp
    )
foreach(f IN LISTS real_files)
  configure_file(${marlin_src}/${f} ${mirror}/${f} COPYONLY)
endforeach()
configure_file(${CMAKE_SOURCE_DIR}/src/common/cmath_ext.h ${mirror}/cmath_ext.h COPYONLY)
file(
  GLOB_RECURSE stub_files
  RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}/stubs
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs/*.h ${CMAKE_CURRENT_SOURCE_DIR}/stubs/*.hpp
  )
foreach(f IN LISTS stub_files)
  configure_file(${CMAKE_CURRENT_SOURCE_DIR}/stubs/${f} ${mirror}/${f} COPYONLY)
endforeach()

# Klipper's step generation, the oracle (klipper/README.md).
add_library(
  motion_klipper_oracle STATIC
  klipper/itersolve.c
  klipper/kin_cartesian.c
  klipper/kin_corexy.c
  klipper/kin_extruder.c
  klipper/kin_shaper.c
  klipper/stepcompress_recorder.c
  klipper/trapq.c
  klipper_oracle.c
  )
# Klipper's own build uses -O2 and these flags (scripts/buildcommands.py does not apply; see
# klippy/chelper/__init__.py COMPILE_ARGS). The Klipper files are copied unchanged, so the warnings
# of the unit test flags are not fixed there.
target_compile_options(
  motion_klipper_oracle PRIVATE -O2 -fno-strict-aliasing -Wno-attributes -Wno-sign-compare
  )
target_link_libraries(motion_klipper_oracle PUBLIC m)
# Klipper's list.h needs GNU C (typeof); klippy/chelper/__init__.py compiles with -std=gnu11.
set_target_properties(motion_klipper_oracle PROPERTIES C_STANDARD 11 C_EXTENSIONS ON)

# The Buddy motion core, from the real sources, plus the drivers of both sides.
add_library(
  motion_diff_pipeline STATIC
  ${mirror}/feature/input_shaper/input_shaper.cpp
  ${mirror}/feature/input_shaper/input_shaper_config.cpp
  ${mirror}/feature/precise_stepping/precise_stepping.cpp
  ${mirror}/feature/pressure_advance/pressure_advance.cpp
  ${mirror}/feature/pressure_advance/pressure_advance_config.cpp
  buddy_driver.cpp
  compare.cpp
  klipper_runner.cpp
  scenario.cpp
  )
target_include_directories(
  motion_diff_pipeline BEFORE
  PUBLIC ${mirror} ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/include
         ${CMAKE_SOURCE_DIR}/src/common/utils ${CMAKE_SOURCE_DIR}/src/module/utils/buddy_utils
  )
# Keep the debug_assert() checks of the motion core on, also in a Release build: they check
# invariants (step times inside the move, position inside the PA interpolation interval).
target_compile_options(motion_diff_pipeline PUBLIC -UNDEBUG)
target_link_libraries(motion_diff_pipeline PUBLIC bsod motion_klipper_oracle)

# One regression_<topic>.cpp for each fixed defect, so that the fix branches do not conflict.
file(GLOB regression_tests CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/regression_*.cpp)
add_executable(
  motion_klipper_diff_tests ${regression_tests} harness_mutation_tests.cpp
                            motion_klipper_diff_tests.cpp
  )
target_link_libraries(motion_klipper_diff_tests motion_diff_pipeline)
add_catch_test(motion_klipper_diff_tests)
