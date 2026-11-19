add_executable(precise_stepping_resync_tests ${CMAKE_CURRENT_SOURCE_DIR}/resync_tests.cpp)
target_include_directories(
  precise_stepping_resync_tests PRIVATE ${CMAKE_SOURCE_DIR}/lib/Marlin/Marlin/src
  )
add_catch_test(precise_stepping_resync_tests)
