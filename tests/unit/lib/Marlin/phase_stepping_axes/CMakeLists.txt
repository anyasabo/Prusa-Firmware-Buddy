add_executable(
  phase_stepping_axes_tests
  ${CMAKE_CURRENT_SOURCE_DIR}/axes_tests.cpp
  ${CMAKE_SOURCE_DIR}/lib/Marlin/Marlin/src/feature/phase_stepping/axes.cpp
  ${CMAKE_SOURCE_DIR}/lib/Marlin/Marlin/src/feature/phase_stepping/lut.cpp
  )

# The local stubs come first: they replace the planner and the config store.
target_include_directories(
  phase_stepping_axes_tests
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/stub ${PROJECT_SOURCE_DIR}/tests/stubs/Marlin/src
          ${CMAKE_SOURCE_DIR}/include ${CMAKE_BINARY_DIR}/include
          ${CMAKE_SOURCE_DIR}/lib/Marlin/Marlin/src/feature/phase_stepping
  )

add_catch_test(phase_stepping_axes_tests)
