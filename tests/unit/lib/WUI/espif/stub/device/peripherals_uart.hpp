// Host test stub: the ESP UART. The test does not send or receive through it.
#pragma once
#include <cstdint>

typedef enum {
    HAL_OK = 0,
    HAL_ERROR = 1,
} HAL_StatusTypeDef;

struct DMA_HandleTypeDef {
    uint32_t counter;
};

struct UART_HandleTypeDef {
    DMA_HandleTypeDef *hdmarx;
};

extern UART_HandleTypeDef uart_handle_for_esp;

#define UART_IT_IDLE                      0
#define __HAL_UART_DISABLE_IT(handle, it) ((void)(handle), (void)(it))
#define __HAL_UART_ENABLE_IT(handle, it)  ((void)(handle), (void)(it))
#define __HAL_DMA_GET_COUNTER(hdma)       ((hdma)->counter)

HAL_StatusTypeDef HAL_UART_Transmit_DMA(UART_HandleTypeDef *huart, const uint8_t *data, uint16_t size);
HAL_StatusTypeDef HAL_UART_Abort(UART_HandleTypeDef *huart);
HAL_StatusTypeDef HAL_UART_Init(UART_HandleTypeDef *huart);
HAL_StatusTypeDef HAL_UART_Receive_DMA(UART_HandleTypeDef *huart, uint8_t *data, uint16_t size);
