# EXPERIMENT (upload-experiments): the real lib/WUI/upload_experiment.cpp (switch parsing and the
# metrics it formats) with a metric.h stub that captures the formatted values.
add_executable(
  upload_experiment_tests ${CMAKE_SOURCE_DIR}/lib/WUI/upload_experiment.cpp
                          upload_experiment_tests.cpp
  )
# The test dir comes first so its metric.h wins.
target_include_directories(
  upload_experiment_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/tests/stubs
                                  ${CMAKE_SOURCE_DIR}/include ${CMAKE_BINARY_DIR}/include
  )
add_catch_test(upload_experiment_tests)
