add_executable(measure_range_tests ${CMAKE_CURRENT_SOURCE_DIR}/measure_range_tests.cpp)
target_include_directories(
  measure_range_tests PRIVATE ${CMAKE_SOURCE_DIR}/lib/Marlin/Marlin/src/gcode/calibrate
  )
add_catch_test(measure_range_tests)
