#pragma once
#include <stdbool.h>
#include "nrf.h"
#define NRFX_ASSERT(x) do { if (!(x)) { __BKPT(0); } } while (0)
#define NRFX_STATIC_ASSERT(x) _Static_assert(x, #x)
#define NRFX_IRQ_PRIORITY_SET(i, p) NVIC_SetPriority(i, p)
#define NRFX_IRQ_ENABLE(i) NVIC_EnableIRQ(i)
#define NRFX_IRQ_DISABLE(i) NVIC_DisableIRQ(i)
#define NRFX_IRQ_PENDING_CLEAR(i) NVIC_ClearPendingIRQ(i)
#define NRFX_IRQ_PENDING_SET(i) NVIC_SetPendingIRQ(i)
#define NRFX_IRQ_IS_ENABLED(i) ((NVIC->ISER[(unsigned)(i) / 32] >> ((unsigned)(i) % 32)) & 1u)
#define NRFX_CRITICAL_SECTION_ENTER() { uint32_t irq_save = __get_PRIMASK(); __disable_irq();
#define NRFX_CRITICAL_SECTION_EXIT() __set_PRIMASK(irq_save); }
#define NRFX_DELAY_US(us) delay_us(us)
