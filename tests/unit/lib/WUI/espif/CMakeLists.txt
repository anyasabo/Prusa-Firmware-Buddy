# The test includes espif.cpp. The stub directory comes first: it replaces the UART, the DMA and the
# FreeRTOS queue, mutex and semaphore.
add_executable(espif_scan_tests ${CMAKE_CURRENT_SOURCE_DIR}/espif_scan_tests.cpp)
target_include_directories(
  espif_scan_tests BEFORE
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/stub
          ${CMAKE_SOURCE_DIR}/tests/stubs
          ${CMAKE_SOURCE_DIR}/include
          ${CMAKE_SOURCE_DIR}/lib/WUI
          ${CMAKE_SOURCE_DIR}/src
          ${CMAKE_SOURCE_DIR}/src/common
          ${CMAKE_SOURCE_DIR}/src/common/utils
          ${CMAKE_SOURCE_DIR}/lib/Middlewares/Third_Party/LwIP/src/include
          ${CMAKE_SOURCE_DIR}/lib/Middlewares/Third_Party/LwIP/system
          ${CMAKE_SOURCE_DIR}/lib
  )
target_link_libraries(espif_scan_tests logging-mock raii random bsod freertos_stubs)
add_catch_test(espif_scan_tests)
