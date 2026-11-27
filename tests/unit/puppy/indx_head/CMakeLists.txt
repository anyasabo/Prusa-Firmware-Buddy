add_executable(indx_head_temperature_tests indx_head_temperature_tests.cpp)
target_include_directories(
  indx_head_temperature_tests PUBLIC ${CMAKE_SOURCE_DIR}/src/puppy/indx_head/src
  )
add_catch_test(indx_head_temperature_tests)
