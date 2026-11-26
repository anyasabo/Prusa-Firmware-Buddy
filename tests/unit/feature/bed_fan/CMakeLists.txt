add_executable(
  bed_fan_controller_tests ${CMAKE_SOURCE_DIR}/src/feature/bed_fan/controller.cpp
                           bed_fan_controller_tests.cpp
  )
target_include_directories(
  bed_fan_controller_tests BEFORE PRIVATE stub_include ${CMAKE_SOURCE_DIR}/src
  )
target_link_libraries(bed_fan_controller_tests buddy_utils)
add_catch_test(bed_fan_controller_tests)
