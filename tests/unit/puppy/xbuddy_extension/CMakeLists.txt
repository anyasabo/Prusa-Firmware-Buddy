add_executable(
  xbuddy_extension_yet_another_circular_buffer_tests yet_another_circular_buffer_tests.cpp
  )
target_include_directories(
  xbuddy_extension_yet_another_circular_buffer_tests
  PRIVATE include ${CMAKE_SOURCE_DIR}/src/puppy/xbuddy_extension
  )
add_catch_test(xbuddy_extension_yet_another_circular_buffer_tests)

add_executable(
  xbuddy_extension_cyphal_application_tests
  ${CMAKE_SOURCE_DIR}/src/puppy/xbuddy_extension/cyphal_application.cpp
  ${CMAKE_SOURCE_DIR}/src/puppy/xbuddy_extension/cyphal_nfc_node.cpp
  ${CMAKE_SOURCE_DIR}/src/puppy/xbuddy_extension/master_activity.cpp cyphal_application_tests.cpp
  )
target_include_directories(
  xbuddy_extension_cyphal_application_tests PRIVATE include
                                                    ${CMAKE_SOURCE_DIR}/src/puppy/xbuddy_extension
  )
target_link_libraries(
  xbuddy_extension_cyphal_application_tests
  ac_controller
  anfc
  tool_offset_sensor
  freertos_tests
  buddy_utils
  xbuddy_extension
  )
add_catch_test(xbuddy_extension_cyphal_application_tests)

add_executable(
  cyphal_nfc_node_tests ${CMAKE_SOURCE_DIR}/src/puppy/xbuddy_extension/cyphal_nfc_node.cpp
                        cyphal_nfc_node_tests.cpp
  )
target_link_libraries(cyphal_nfc_node_tests ac_controller anfc tool_offset_sensor)
target_include_directories(
  cyphal_nfc_node_tests PRIVATE ${CMAKE_SOURCE_DIR}/src/puppy/xbuddy_extension
  )
add_catch_test(cyphal_nfc_node_tests)

add_executable(
  xbuddy_extension_hal_logic_tests
  ${CMAKE_SOURCE_DIR}/src/puppy/xbuddy_extension/temperature.cpp
  ${CMAKE_SOURCE_DIR}/src/module/utils/buddy_utils/utils/timing/timer_event_period_tracker.cpp
  hal_logic_tests.cpp
  )
target_include_directories(
  xbuddy_extension_hal_logic_tests PRIVATE ${CMAKE_SOURCE_DIR}/src/puppy/xbuddy_extension
                                           ${CMAKE_SOURCE_DIR}/src/module/utils/buddy_utils
  )
add_catch_test(xbuddy_extension_hal_logic_tests)

# mmu_include has option/has_mmu2.h. The test does not use the include directory of the other
# targets, because its freertos/timing.hpp has an inline millis() and the test defines its own.
add_executable(
  xbuddy_extension_mmu_bridge_tests
  ${CMAKE_SOURCE_DIR}/src/puppy/xbuddy_extension/mmu.cpp
  ${CMAKE_SOURCE_DIR}/lib/Prusa-Firmware-MMU/src/modules/protocol.cpp
  ${CMAKE_SOURCE_DIR}/lib/Prusa-Firmware-MMU/src/modules/crc.cpp mmu_bridge_tests.cpp
  )
target_include_directories(
  xbuddy_extension_mmu_bridge_tests
  PRIVATE mmu_include
          ${CMAKE_SOURCE_DIR}/src/puppy/xbuddy_extension
          ${CMAKE_SOURCE_DIR}/src/freertos/include
          ${CMAKE_SOURCE_DIR}/src/module/utils/byte_utils/include
          ${CMAKE_SOURCE_DIR}/lib/Prusa-Firmware-MMU/src
  )
target_link_libraries(xbuddy_extension_mmu_bridge_tests modbus xbuddy_extension)
add_catch_test(xbuddy_extension_mmu_bridge_tests)

# The test defines its own millis(), so it does not use the include directory of the other targets.
add_executable(xbuddy_extension_cyphal_transport_tests cyphal_transport_tests.cpp)
target_include_directories(
  xbuddy_extension_cyphal_transport_tests PRIVATE ${CMAKE_SOURCE_DIR}/src/puppy/xbuddy_extension
  )
target_link_libraries(
  xbuddy_extension_cyphal_transport_tests canard o1heap raii byte_utils freertos_tests
  )
add_catch_test(xbuddy_extension_cyphal_transport_tests)
