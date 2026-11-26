# Host tests of the INDX toolchanger (lib/Marlin/Marlin/src/module/prusa/toolchanger_*.cpp). The
# real toolchanger sources compile against the fakes in stub_include/ and fake_world.cpp, as a CORE
# One L INDX.
set(MARLIN_SRC ${CMAKE_SOURCE_DIR}/lib/Marlin/Marlin/src)

add_executable(
  toolchanger_indx_tests
  ${MARLIN_SRC}/module/prusa/toolchanger_indx.cpp ${MARLIN_SRC}/module/prusa/toolchanger_common.cpp
  ${MARLIN_SRC}/module/prusa/toolchanger_utils_indx.cpp fake_world.cpp toolchanger_indx_tests.cpp
  )
target_include_directories(
  toolchanger_indx_tests BEFORE PRIVATE stub_include ${MARLIN_SRC} ${CMAKE_SOURCE_DIR}/src/common
                                        ${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/src
  )
# The unit tests are set up as a MINI. Build these as a CORE One L (PRINTER_TYPE 8, INDX variant
# 10).
target_compile_options(
  toolchanger_indx_tests PRIVATE -UPRINTER_TYPE -DPRINTER_TYPE=8 -UPRINTER_VERSION
                                 -DPRINTER_VERSION=10
  )
target_link_libraries(toolchanger_indx_tests buddy_utils raii SG14)
add_catch_test(toolchanger_indx_tests)
