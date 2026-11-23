add_executable(
  pressure_advance_calibration_tests
  ${CMAKE_SOURCE_DIR}/src/feature/pressure_advance_calibration/step_response_analyzer.cpp
  step_response_analyzer_tests.cpp
  )
target_include_directories(pressure_advance_calibration_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_catch_test(pressure_advance_calibration_tests)
