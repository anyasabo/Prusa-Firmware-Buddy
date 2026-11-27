# Host tests of the real Marlin G-code parser (lib/Marlin/Marlin/src/gcode/parser.cpp).
#
# The parser includes the configuration with relative paths ("../inc/MarlinConfig.h"). As in the
# planner tests, copy the real sources and the stubs into one tree in the build directory, so that
# the relative includes find the stubs.
set(mirror ${CMAKE_CURRENT_BINARY_DIR}/mirror)
set(marlin_src ${CMAKE_SOURCE_DIR}/lib/Marlin/Marlin/src)
foreach(f IN ITEMS core/macros.h gcode/parser.cpp gcode/parser.h)
  configure_file(${marlin_src}/${f} ${mirror}/${f} COPYONLY)
endforeach()
file(
  GLOB_RECURSE stubs
  RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}/stubs
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs/*.h
  )
foreach(f IN LISTS stubs)
  configure_file(${CMAKE_CURRENT_SOURCE_DIR}/stubs/${f} ${mirror}/${f} COPYONLY)
endforeach()

# The parser reads the line in place and the handlers read it again later. AddressSanitizer finds a
# read past the end of the line, and UndefinedBehaviorSanitizer an overflow in the value accessors.
# A toolchain without the sanitizer runtimes builds the tests without them.
set(sanitizers -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer)
include(CheckCXXSourceCompiles)
set(CMAKE_REQUIRED_FLAGS ${sanitizers})
set(CMAKE_REQUIRED_LINK_OPTIONS ${sanitizers})
check_cxx_source_compiles("int main() { return 0; }" MARLIN_GCODE_PARSER_SANITIZERS)
unset(CMAKE_REQUIRED_FLAGS)
unset(CMAKE_REQUIRED_LINK_OPTIONS)
if(NOT MARLIN_GCODE_PARSER_SANITIZERS)
  message(STATUS "marlin_gcode_parser_tests: no sanitizer runtimes, the tests run without them")
  set(sanitizers)
endif()

add_library(marlin_gcode_parser STATIC ${mirror}/gcode/parser.cpp)
target_include_directories(marlin_gcode_parser BEFORE PUBLIC ${mirror} ${mirror}/gcode)
target_compile_options(marlin_gcode_parser PUBLIC ${sanitizers})
target_link_options(marlin_gcode_parser PUBLIC ${sanitizers})

add_executable(marlin_gcode_parser_tests ${CMAKE_CURRENT_SOURCE_DIR}/gcode_parser_tests.cpp)
target_link_libraries(marlin_gcode_parser_tests marlin_gcode_parser)
add_catch_test(marlin_gcode_parser_tests)
