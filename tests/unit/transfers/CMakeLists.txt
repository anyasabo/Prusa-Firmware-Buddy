add_executable(
  transfers_tests
  ${CMAKE_SOURCE_DIR}/src/common/crc32.cpp
  ${CMAKE_SOURCE_DIR}/src/common/filename_type.cpp
  ${CMAKE_SOURCE_DIR}/src/common/filepath_operation.cpp
  ${CMAKE_SOURCE_DIR}/src/common/path_utils.cpp
  ${CMAKE_SOURCE_DIR}/src/common/random_sw.cpp
  ${CMAKE_SOURCE_DIR}/src/common/stat_retry.cpp
  ${CMAKE_SOURCE_DIR}/src/transfers/monitor.cpp
  ${CMAKE_SOURCE_DIR}/src/transfers/changed_path.cpp
  ${CMAKE_SOURCE_DIR}/src/transfers/transfer_recovery.cpp
  ${CMAKE_SOURCE_DIR}/tests/stubs/strlcpy.c
  monitor.cpp
  missing_functions.cpp
  changed_path_tests.cpp
  transfer_tests.cpp
  partial_file_mock.cpp
  )

target_include_directories(
  transfers_tests
  PRIVATE .
          ${CMAKE_CURRENT_SOURCE_DIR}/mock
          ${CMAKE_SOURCE_DIR}/src
          ${CMAKE_SOURCE_DIR}/include
          ${CMAKE_SOURCE_DIR}/src/common
          ${CMAKE_SOURCE_DIR}/src/common/utils
          ${CMAKE_SOURCE_DIR}/lib/WUI
          ${CMAKE_SOURCE_DIR}/tests/stubs
          ${CMAKE_SOURCE_DIR}/include/usb_host
          ${CMAKE_BINARY_DIR}/include
  )
add_definitions(-DDATA_PATH="${CMAKE_CURRENT_SOURCE_DIR}")

file(GLOB AUTOMATA_GENERATORS ${PROJECT_ROOT_DIR}/utils/gen-automata/*.py)

target_link_libraries(
  transfers_tests
  mbedTLS
  SG14
  freertos_tests
  logging-mock
  random
  random_sw
  buddy_utils
  )

add_catch_test(transfers_tests)

# Only iX builds the AES-CTR decryptor (WEBSOCKET is off), but test it in each configuration: it is
# the only user of AES-CTR.
add_executable(
  transfers_decrypt_tests ${CMAKE_SOURCE_DIR}/src/transfers/decrypt.cpp decrypt.cpp
                          decrypt_missing_functions.c
  )
target_include_directories(transfers_decrypt_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(transfers_decrypt_tests mbedTLS logging-mock)
add_catch_test(transfers_decrypt_tests)
