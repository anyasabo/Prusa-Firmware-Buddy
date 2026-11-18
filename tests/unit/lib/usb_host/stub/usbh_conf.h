// Host test copy of include/usb_host/usbh_conf.h: the same library options, no HAL, no RTOS.
#pragma once
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define USBH_MAX_NUM_ENDPOINTS       2
#define USBH_MAX_NUM_INTERFACES      2
#define USBH_MAX_NUM_CONFIGURATION   1
#define USBH_KEEP_CFG_DESCRIPTOR     1
#define USBH_MAX_NUM_SUPPORTED_CLASS 1
#define USBH_MAX_SIZE_CONFIGURATION  256
#define USBH_MAX_DATA_BUFFER         512
#define USBH_USE_OS                  0
#define USBH_USE_MSC_CLASS_EVENTS    0
#define USBH_USE_ACK_INTERRUPTS      0
// 1 here: no NAK mask register write in USBH_MSC_InterfaceInit (there is no hardware). Buddy has 0.
#define USBH_USE_IN_CHANNEL_BULK_NACK_INTERRUPTS 1
#define USBH_USE_URB_EVENTS                      0
#define USBH_MSC_TRANSFER_SIZE                   512
#define USBH_MSC_BLOCK_SIZE                      512U
#define USBH_MSC_IO_TIMEOUT                      (25 * 1000)
#define MAX_SUPPORTED_LUN                        1
#define HOST_HS                                  0
#define HOST_FS                                  1

#define USBH_malloc malloc
#define USBH_free   free
#define USBH_memset memset
#define USBH_memcpy memcpy
#define USBH_UsrLog(...) \
    do {                 \
    } while (0)
#define USBH_ErrLog(...) \
    do {                 \
    } while (0)
#define USBH_DbgLog(...) \
    do {                 \
    } while (0)

// From the STM32 HAL and FreeRTOS, as the library uses them
#define __IO         volatile
#define UNUSED(X)    (void)X
#define EP_TYPE_CTRL 0U
#define EP_TYPE_ISOC 1U
#define EP_TYPE_BULK 2U
#define EP_TYPE_INTR 3U
#define EP_TYPE_MSK  3U
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
    uint32_t dummy;
} USB_OTG_GlobalTypeDef;
typedef struct {
    USB_OTG_GlobalTypeDef *Instance;
} HCD_HandleTypeDef;
uint32_t USB_GetCurrentFrame(USB_OTG_GlobalTypeDef const *USBx);
typedef uint32_t TickType_t;
#define pdFALSE            0
#define portTICK_PERIOD_MS 1
uint32_t ulTaskNotifyTake(int clear, TickType_t ticks);
uint32_t ticks_ms(void);
#ifdef __cplusplus
}
#endif
