#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "nrf.h"
#include "nrfx_config.h"
void platform_init(void);
uint32_t micros(void);
void delay_us(uint32_t us);
void wait_until(uint32_t deadline);
static inline bool due(uint32_t now, uint32_t at) { return (int32_t)(now - at) >= 0; }
