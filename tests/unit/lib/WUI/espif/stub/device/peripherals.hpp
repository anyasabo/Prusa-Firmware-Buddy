// Host test stub: the ESP reset pin.
#pragma once
#include <cstdint>
#define ESP_RST_GPIO_Port nullptr
#define ESP_RST_Pin       0
#define GPIO_PIN_RESET    0
#define GPIO_PIN_SET      1
inline void HAL_GPIO_WritePin(void *, uint16_t, int) {}
