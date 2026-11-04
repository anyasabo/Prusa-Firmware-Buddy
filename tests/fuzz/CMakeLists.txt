# Fuzz harnesses for host-testable firmware parsers. Added from tests/unit/CMakeLists.txt so the
# harnesses compile with the same defines, include paths and stubs as the unit tests.
#
# Two flavours from the same sources (each harness defines LLVMFuzzerTestOneInput): BUDDY_FUZZ=ON
# (Clang): libFuzzer engine, or $LIB_FUZZING_ENGINE (OSS-Fuzz/ClusterFuzzLite). Configure the whole
# tree with -fsanitize=fuzzer-no-link,address,undefined so the code under test is
# coverage-instrumented (see tests/fuzz/README.md). BUDDY_FUZZ=OFF (default, any compiler): each
# harness links replay_main.cpp and ctest replays its checked-in corpus, so seeds and fixed-bug
# reproducers act as regression tests.
option(BUDDY_FUZZ "Build fuzz harnesses with libFuzzer (Clang only)" OFF)

if(BUDDY_FUZZ)
  if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(FATAL_ERROR "BUDDY_FUZZ requires Clang (libFuzzer)")
  endif()
  if(DEFINED ENV{LIB_FUZZING_ENGINE})
    set(BUDDY_FUZZ_ENGINE "$ENV{LIB_FUZZING_ENGINE}")
  else()
    set(BUDDY_FUZZ_ENGINE "-fsanitize=fuzzer")
  endif()
else()
  add_library(fuzz_replay_main STATIC ${CMAKE_CURRENT_SOURCE_DIR}/replay_main.cpp)
endif()

# buddy_add_fuzzer(<name> SOURCES ... [LIBS ...] [INCLUDES ...]) The corpus is
# tests/fuzz/corpus/<name>; a dictionary tests/fuzz/<name>.dict is copied next to the binary when
# present.
function(buddy_add_fuzzer name)
  cmake_parse_arguments(FZ "" "" "SOURCES;LIBS;INCLUDES" ${ARGN})
  set(target fuzz_${name})
  add_executable(${target} ${FZ_SOURCES})
  target_link_libraries(${target} PRIVATE ${FZ_LIBS})
  target_include_directories(${target} PRIVATE ${FZ_INCLUDES})
  set(corpus ${CMAKE_CURRENT_SOURCE_DIR}/corpus/${name})
  if(BUDDY_FUZZ)
    target_link_options(${target} PRIVATE ${BUDDY_FUZZ_ENGINE})
    add_test(NAME ${target} COMMAND ${target} -runs=0 ${corpus})
  else()
    target_link_libraries(${target} PRIVATE fuzz_replay_main)
    add_test(NAME ${target} COMMAND ${target} ${corpus})
  endif()
  # UBSan reports are recoverable by default; make them fail the replay like any other crash.
  set_tests_properties(
    ${target} PROPERTIES LABELS fuzz WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR} ENVIRONMENT
                         "UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1"
    )
  if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${name}.dict)
    configure_file(
      ${CMAKE_CURRENT_SOURCE_DIR}/${name}.dict ${CMAKE_CURRENT_BINARY_DIR}/${name}.dict COPYONLY
      )
  endif()
  add_dependencies(tests ${target})
endfunction()

# PrusaLink request paths (PFB-006 was here). Pure function, very fast.
buddy_add_fuzzer(
  url_decode
  SOURCES
  url_decode.cpp
  ${CMAKE_SOURCE_DIR}/src/common/http/url_decode.cpp
  ${CMAKE_SOURCE_DIR}/src/common/str_utils.cpp
  # Coverage instrumentation keeps str_utils' unused RectTextLayout alive; it needs this.
  ${CMAKE_SOURCE_DIR}/src/lang/string_view_utf8.cpp
  INCLUDES
  ${CMAKE_SOURCE_DIR}/src/common
  ${CMAKE_SOURCE_DIR}/src/lang
  )

# MeatPack decoding of .bgcode G-code blocks and serial input (PFB fix 0458dfc17 was here).
buddy_add_fuzzer(
  meatpack SOURCES meatpack.cpp ${CMAKE_SOURCE_DIR}/src/common/gcode/meatpack.cpp INCLUDES
  ${CMAKE_SOURCE_DIR}/src/common/gcode
  )

# Plain and binary G-code files from USB / PrusaLink upload / Connect: block walking, heatshrink,
# MeatPack, metadata, thumbnails, encrypted-file handling. Reuses the reader unit test's objects.
buddy_add_fuzzer(bgcode_reader SOURCES bgcode_reader.cpp LIBS gcode_reader_test_base)
# The reader loads the test printer key from the working directory to decrypt encrypted seeds.
file(COPY ${CMAKE_SOURCE_DIR}/tests/unit/common/gcode/reader/printer_private_key.der
     DESTINATION ${CMAKE_CURRENT_BINARY_DIR}
     )

# buddy_add_fuzzer_from_test(<name> TEST <unit-test target> DROP <test .cpp files> SOURCES
# <harness>) Builds a fuzzer from an existing unit-test target: same sources (minus the Catch2 test
# files named in DROP), include paths, definitions, libraries and generator dependencies, so harness
# builds keep working as the tests evolve.
function(buddy_add_fuzzer_from_test name)
  cmake_parse_arguments(FZ "" "TEST" "DROP;SOURCES" ${ARGN})
  get_target_property(test_dir ${FZ_TEST} SOURCE_DIR)
  get_target_property(test_bin_dir ${FZ_TEST} BINARY_DIR)
  get_target_property(srcs ${FZ_TEST} SOURCES)
  set(kept)
  foreach(src IN LISTS srcs)
    if(NOT IS_ABSOLUTE ${src})
      set(src ${test_dir}/${src})
    endif()
    # Drop the test's own Catch2 files, however the test listed them (relative or absolute).
    get_filename_component(base ${src} NAME)
    get_filename_component(dir ${src} DIRECTORY)
    if(base IN_LIST FZ_DROP AND dir STREQUAL test_dir)
      continue()
    endif()
    if(src MATCHES "^${test_bin_dir}/")
      set_source_files_properties(${src} PROPERTIES GENERATED TRUE)
    endif()
    list(APPEND kept ${src})
  endforeach()
  buddy_add_fuzzer(${name} SOURCES ${FZ_SOURCES} ${kept})
  foreach(prop INCLUDE_DIRECTORIES COMPILE_DEFINITIONS COMPILE_OPTIONS)
    get_target_property(val ${FZ_TEST} ${prop})
    if(val)
      set_property(
        TARGET fuzz_${name}
        APPEND
        PROPERTY ${prop} ${val}
        )
    endif()
  endforeach()
  get_target_property(libs ${FZ_TEST} LINK_LIBRARIES)
  list(FILTER libs EXCLUDE REGEX "^(catch_main|Catch2::Catch2.*)$")
  target_link_libraries(fuzz_${name} PRIVATE ${libs})
  get_target_property(deps ${FZ_TEST} MANUALLY_ADDED_DEPENDENCIES)
  if(deps)
    add_dependencies(fuzz_${name} ${deps})
  endif()
endfunction()

# Connect server commands (JSON via jsmn + json::search) and plain G-code commands. PFB-008
# (SEND_FILE_INFO) and PFB-014 were in the Connect command path.
buddy_add_fuzzer_from_test(
  connect_command
  TEST
  connect_tests
  DROP
  command.cpp
  printer.cpp
  render.cpp
  hostname.cpp
  SOURCES
  connect_command.cpp
  )

# E2EE identity block of encrypted .bgcode files (from Connect or USB), parsed before any signature
# check. PFB-002 / PFB-011 were in the E2EE path.
buddy_add_fuzzer_from_test(
  e2ee_identity
  TEST
  decryptor_tests
  DROP
  decryptor.cpp
  identity_block.cpp
  SOURCES
  e2ee_identity.cpp
  )

# G-code line parser used for every line the printer executes (GCodeParser2: options, quoted
# strings, escapes, numeric conversions). PFB-005 was a %16s in G-code argument handling.
buddy_add_fuzzer_from_test(
  gcode_parser
  TEST
  gcode_parser_tests
  DROP
  gcode_parser_tests.cpp
  SOURCES
  gcode_parser.cpp
  )
