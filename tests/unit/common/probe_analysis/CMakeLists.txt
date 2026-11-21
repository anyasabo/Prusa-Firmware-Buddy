# define the test executable
add_executable(probe_analysis_tests ${CMAKE_SOURCE_DIR}/src/common/probe_analysis.cpp tests.cpp)

target_link_libraries(probe_analysis_tests raii)

# define required search paths
target_include_directories(
  probe_analysis_tests PUBLIC . ${CMAKE_SOURCE_DIR}/src/common ${CMAKE_SOURCE_DIR}/include
  )

# tell build system about the test case
add_catch_test(probe_analysis_tests)

# define the test executable
add_executable(
  position_lookback ${CMAKE_SOURCE_DIR}/src/common/position_lookback.cpp position_lookback_test.cpp
  )

# define required search paths
target_include_directories(
  position_lookback PUBLIC . stub ${CMAKE_SOURCE_DIR}/src/common ${CMAKE_SOURCE_DIR}/include
  )

target_link_libraries(position_lookback SG14 raii bsod)

# tell build system about the test case
add_catch_test(position_lookback)

add_executable(probe_timing_tests probe_timing_tests.cpp)
target_include_directories(probe_timing_tests PUBLIC ${CMAKE_SOURCE_DIR}/src/common)
add_catch_test(probe_timing_tests)

add_executable(probe_grid_tests probe_grid_tests.cpp)
target_include_directories(probe_grid_tests PUBLIC ${CMAKE_SOURCE_DIR}/src/common)
add_catch_test(probe_grid_tests)
