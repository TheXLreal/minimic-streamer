#include "platform.h"

void platform_init(void) {
#ifdef NRF5340_XXAA_APPLICATION
    NRF_CLOCK->HFCLKCTRL = CLOCK_HFCLKCTRL_HCLK_Div1;
    SystemCoreClockUpdate();
#endif
    NRF_CLOCK->EVENTS_HFCLKSTARTED = 0;
    NRF_CLOCK->TASKS_HFCLKSTART = 1;
    while (!NRF_CLOCK->EVENTS_HFCLKSTARTED) {}
    NRF_TIMER0->MODE = TIMER_MODE_MODE_Timer;
    NRF_TIMER0->BITMODE = TIMER_BITMODE_BITMODE_32Bit;
    NRF_TIMER0->PRESCALER = 4; /* 16 MHz / 16 = 1 MHz on both cores. */
    NRF_TIMER0->TASKS_CLEAR = 1;
    NRF_TIMER0->TASKS_START = 1;
}
uint32_t micros(void) {
    NRF_TIMER0->TASKS_CAPTURE[0] = 1;
    return NRF_TIMER0->CC[0];
}
void wait_until(uint32_t deadline) { while (!due(micros(), deadline)) {} }
void delay_us(uint32_t us) { wait_until(micros() + us); }
