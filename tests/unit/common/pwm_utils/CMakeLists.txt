add_executable(pwm_utils_tests pwm_utils_tests.cpp)
target_include_directories(pwm_utils_tests PUBLIC ${CMAKE_SOURCE_DIR}/src/common)
add_catch_test(pwm_utils_tests)
