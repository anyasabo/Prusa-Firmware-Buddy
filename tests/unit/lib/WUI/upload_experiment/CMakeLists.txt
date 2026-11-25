# EXPERIMENT (upload-experiments): the real lib/WUI/upload_experiment.cpp (switch parsing and the
# metrics it formats) with a metric.h stub that captures the formatted values.
add_executable(
  upload_experiment_tests ${CMAKE_SOURCE_DIR}/lib/WUI/upload_experiment.cpp
                          upload_experiment_tests.cpp
  )
# The test dir comes first so its metric.h wins.
target_include_directories(
  upload_experiment_tests
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/tests/stubs ${CMAKE_SOURCE_DIR}/include
          ${CMAKE_SOURCE_DIR}/src/common ${CMAKE_BINARY_DIR}/include
  )
add_catch_test(upload_experiment_tests)

# The same source with HAS_FORK_DIAGNOSTICS off, as on the MINI. off/ comes first, so its stub
# option header wins over the generated one in ${CMAKE_BINARY_DIR}/include.
add_executable(
  upload_experiment_off_tests ${CMAKE_SOURCE_DIR}/lib/WUI/upload_experiment.cpp
                              upload_experiment_off_tests.cpp
  )
target_include_directories(
  upload_experiment_off_tests
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/off ${CMAKE_CURRENT_SOURCE_DIR}
          ${CMAKE_SOURCE_DIR}/tests/stubs ${CMAKE_SOURCE_DIR}/include
          ${CMAKE_SOURCE_DIR}/src/common ${CMAKE_BINARY_DIR}/include
  )
add_catch_test(upload_experiment_off_tests)
