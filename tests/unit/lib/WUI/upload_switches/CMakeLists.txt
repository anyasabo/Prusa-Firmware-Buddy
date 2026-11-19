add_executable(upload_switches_tests upload_switches_test.cpp)

target_include_directories(upload_switches_tests PRIVATE ${CMAKE_SOURCE_DIR}/lib/WUI)

add_catch_test(upload_switches_tests)
