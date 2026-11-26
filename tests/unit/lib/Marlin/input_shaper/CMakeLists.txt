add_executable(input_shaper_m593_tests ${CMAKE_CURRENT_SOURCE_DIR}/m593_params_tests.cpp)
target_include_directories(
  input_shaper_m593_tests
  PRIVATE ${CMAKE_SOURCE_DIR}/lib/Marlin/Marlin/src ${CMAKE_SOURCE_DIR}/include
          ${CMAKE_SOURCE_DIR}/src/common/utils ${CMAKE_SOURCE_DIR}/src/module/utils/buddy_utils
  )
# CORE One L
target_compile_definitions(
  input_shaper_m593_tests PRIVATE PRINTER_TYPE=8 PRINTER_VERSION=1 PRINTER_SUBVERSION=0
  )
add_catch_test(input_shaper_m593_tests)
