add_executable(arc_segments_tests ${CMAKE_CURRENT_SOURCE_DIR}/arc_segments_tests.cpp)
target_include_directories(
  arc_segments_tests PRIVATE ${CMAKE_SOURCE_DIR}/lib/Marlin/Marlin/src/gcode/motion
  )
add_catch_test(arc_segments_tests)
