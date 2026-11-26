add_executable(pressure_advance_sampling_tests ${CMAKE_CURRENT_SOURCE_DIR}/sampling_tests.cpp)
target_include_directories(
  pressure_advance_sampling_tests PRIVATE ${CMAKE_SOURCE_DIR}/lib/Marlin/Marlin/src
  )
add_catch_test(pressure_advance_sampling_tests)
