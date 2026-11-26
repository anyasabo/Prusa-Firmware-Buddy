# Host tests of ToolMapper and SpoolJoin (lib/Marlin/Marlin/src/module/prusa/tool_mapper.cpp,
# spool_join.cpp), with the INDX tool count (8 tools).
set(MARLIN_SRC ${CMAKE_SOURCE_DIR}/lib/Marlin/Marlin/src)

add_executable(
  tool_mapping_tests ${MARLIN_SRC}/module/prusa/tool_mapper.cpp
                     ${MARLIN_SRC}/module/prusa/spool_join.cpp fakes.cpp tool_mapping_tests.cpp
  )
target_include_directories(
  tool_mapping_tests BEFORE
  PRIVATE stub_include stub_include/relative_include_base/a/b ${MARLIN_SRC}
          ${CMAKE_SOURCE_DIR}/src/common ${CMAKE_SOURCE_DIR}/include
  )
target_compile_options(
  tool_mapping_tests PRIVATE -UPRINTER_TYPE -DPRINTER_TYPE=8 -UPRINTER_VERSION -DPRINTER_VERSION=10
  )
target_link_libraries(tool_mapping_tests buddy_utils)
add_catch_test(tool_mapping_tests)
