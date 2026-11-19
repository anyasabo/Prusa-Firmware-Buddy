set(USBH_LIB ${CMAKE_SOURCE_DIR}/lib/Middlewares/ST/STM32_USB_Host_Library)

add_executable(
  usb_host_msc_tests
  ${CMAKE_CURRENT_SOURCE_DIR}/msc_block_size_tests.cpp
  ${USBH_LIB}/Core/Src/usbh_core.c
  ${USBH_LIB}/Core/Src/usbh_ctlreq.c
  ${USBH_LIB}/Core/Src/usbh_ioreq.c
  ${USBH_LIB}/Core/Src/usbh_pipes.c
  ${USBH_LIB}/Class/MSC/Src/usbh_msc.c
  ${USBH_LIB}/Class/MSC/Src/usbh_msc_bot.c
  ${USBH_LIB}/Class/MSC/Src/usbh_msc_scsi.c
  )
# The stub directory comes first: it has the host copy of usbh_conf.h. The include directory comes
# last: anya/main's usbh_msc.c includes the experiment header buddy/usb_io_stats.h.
target_include_directories(
  usb_host_msc_tests BEFORE PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/stub ${USBH_LIB}/Core/Inc
                                    ${USBH_LIB}/Class/MSC/Inc
  )
target_include_directories(usb_host_msc_tests PRIVATE ${CMAKE_SOURCE_DIR}/include)
add_catch_test(usb_host_msc_tests)
