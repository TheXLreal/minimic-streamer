#pragma once
#define NRFX_CONFIG_H__
#define NRFX_CONFIG_API_VER_MAJOR 3
#define NRFX_CONFIG_API_VER_MINOR 14
#define NRFX_CONFIG_API_VER_MICRO 0
#define NRFX_CLOCK_ENABLED 1
#define NRFX_DEFAULT_IRQ_PRIORITY 3
#define NRFX_CONFIG_LOG_ENABLED 0
#ifdef NRF5340_XXAA_APPLICATION
#include <templates/nrfx_config_nrf5340_application.h>
#else
#include <templates/nrfx_config_nrf5340_network.h>
#endif
