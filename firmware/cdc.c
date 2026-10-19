// cdc.c - see cdc.h. Parses the 0x5A-prefixed command stream and forwards to the radio / board.
#include "cdc.h"
#include "radio.h"
#include "board.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "tusb.h"

void cdc_printf(const char *fmt, ...) {
    if (!tud_cdc_connected()) return;
    char b[200];
    va_list ap; va_start(ap, fmt); int n = vsnprintf(b, sizeof b, fmt, ap); va_end(ap);
    if (n > 0) { tud_cdc_write(b, (uint32_t)(n < (int)sizeof b ? n : (int)sizeof b - 1)); tud_cdc_write_flush(); }
}

static uint64_t raw_last_us = 0;                 // time of the last raw frame from the CDC port
bool cdc_raw_active(void) { return raw_last_us && time_us_64() - raw_last_us < 300000; }

// a 1200-baud "touch" reboots into BOOTSEL (Arduino-style update without the button)
void tud_cdc_line_coding_cb(uint8_t itf, cdc_line_coding_t const *lc) {
    (void)itf;
    if (lc->bit_rate == 1200) board_to_bootsel();
}

void cdc_task(void) {
    static int st = 0, bi = 0; static uint8_t buf[84];
    uint8_t in[64];
    while (tud_cdc_available()) {
        uint32_t n = tud_cdc_read(in, sizeof in);
        for (uint32_t i = 0; i < n; i++) {
            uint8_t c = in[i];
            if (st == 0) { st = (c == 0x5A) ? 1 : 0; }
            else if (st == 1) { st = (c == 0xA5) ? 2 : (c == 0xC3) ? 3 : (c == 0x5A) ? 1 : 0; bi = 0; }
            else if (st == 2) { buf[bi++] = c; if (bi == 84) { radio_push_frame(buf); raw_last_us = time_us_64(); st = 0; } }
            else if (st == 3) {
                st = 0;
                if (c == 'B') board_to_bootsel();
                else if (c == 'R') board_reboot();
                else if (c == 'P') radio_start_pairing();
                else if (c == 'A') { st = 4; bi = 0; }
                else if (c == 'H') { st = 5; bi = 0; }
                else if (c == 'M') radio_set_monitor(true);
                else if (c == 'm') radio_set_monitor(false);
                else if (c == 'L') board_led_force(true);
                else if (c == 'l') board_led_force(false);
            }
            else if (st == 4) { buf[bi++] = c; if (bi == 4) { radio_set_addr(buf); st = 0; } }
            else if (st == 5) { buf[bi++] = c; if (bi == 16) { radio_set_hop(buf); st = 0; } }
        }
    }
}
