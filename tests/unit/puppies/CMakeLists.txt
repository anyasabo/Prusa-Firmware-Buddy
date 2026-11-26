add_subdirectory(cyphal_node_stubs)
add_subdirectory(minimal_cyphal_node)

add_executable(
  time_sync ${CMAKE_CURRENT_SOURCE_DIR}/time_sync.cpp ${CMAKE_SOURCE_DIR}/src/puppies/time_sync.cpp
  )

target_include_directories(
  time_sync PUBLIC ${CMAKE_SOURCE_DIR}/tests/stubs ${CMAKE_SOURCE_DIR}/include
                   ${CMAKE_SOURCE_DIR}/src/common
  )

target_link_libraries(time_sync SG14 buddy_utils)

add_catch_test(time_sync)

# The unit tests are set up as a MINI. The INDX error codes exist only for the INDX printers, so
# generate the error codes of the CORE One L INDX (printer code 37) for this test.
set(indx_fault_error_codes_dir "${CMAKE_CURRENT_BINARY_DIR}/indx_fault_error_codes")
add_custom_command(
  OUTPUT "${indx_fault_error_codes_dir}/error_codes.hpp"
  COMMAND
    "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/lib/Prusa-Error-Codes/generate_buddy_headers.py"
    "${CMAKE_SOURCE_DIR}/lib/Prusa-Error-Codes/yaml/buddy-error-codes.yaml"
    "${indx_fault_error_codes_dir}/error_codes.hpp" COREONEL_INDX 37
  DEPENDS "${CMAKE_SOURCE_DIR}/lib/Prusa-Error-Codes/yaml/buddy-error-codes.yaml"
          "${CMAKE_SOURCE_DIR}/lib/Prusa-Error-Codes/generate_buddy_headers.py"
  VERBATIM
  )

add_executable(
  indx_fault_tests
  ${CMAKE_CURRENT_SOURCE_DIR}/indx_fault.cpp ${CMAKE_SOURCE_DIR}/src/puppies/indx_fault.cpp
  "${indx_fault_error_codes_dir}/error_codes.hpp"
  )
target_include_directories(
  indx_fault_tests
  PRIVATE "${indx_fault_error_codes_dir}" ${CMAKE_SOURCE_DIR}/lib/Prusa-Error-Codes/include
          ${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/src/module/indx_head/include
  )
# The firmware build has -Werror=switch. The test has it too, so a new FaultStatusMask bit without a
# decision also fails the host build.
target_compile_options(indx_fault_tests PRIVATE -Werror=switch)
add_catch_test(indx_fault_tests)
