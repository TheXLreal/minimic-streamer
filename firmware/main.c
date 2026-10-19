// MiniMic Streamer - the Pico is a USB sound card; it encodes the PC's audio with the MiniMic's
// codec (encoder.c, see CODEC.md) and transmits it exactly like the MiniMic, so a paired receiver
// plays it. Switch the real MiniMic OFF while this transmits.
//
// The code is split into small modules:
//   encoder.c       the codec (portable C, no Pico dependencies - builds on a PC too)
//   nrf24.c         low-level nRF24L01+ driver (SPI, registers, payloads)
//   radio.c         core1: the transmit timing loop, pairing, and the optional beacon monitor
//   usb_audio.c     the UAC1 sound card: PC audio -> encoder -> radio
//   cdc.c           the USB serial command protocol (below) and status/printf
//   board.c         LED (differs per board), BOOTSEL button, reboot / reset-into-BOOTSEL
//   usb_descriptors.c  the USB descriptors (UAC1 speaker + CDC + reset interface)
// Core0 runs USB + encoder + UI; core1 runs the radio.
//
// USB: "MiniMic Streamer" = UAC1 speaker (48 kHz 16-bit stereo, async with feedback EP)
//      + CDC serial port + Raspberry Pi reset interface.
// CDC serial protocol (binary, PC -> Pico):
//   0x5A 0xA5 + 84 bytes  one raw frame: three 28-byte codec blocks (0x18, 0x19, 0x1A = payload bytes 2..29).
//                         While raw frames keep coming (and 300 ms after), USB audio is ignored.
//   0x5A 0xC3 'B'         reboot into BOOTSEL (UF2 drive / picotool) for a firmware update
//   0x5A 0xC3 'R'         plain reboot
//   0x5A 0xC3 'P'         start pairing mode (same as holding BOOTSEL)
//   0x5A 0xC3 'A' + 4 B   use this radio address until the next reboot (experiments)
//   0x5A 0xC3 'H' + 16 B  use this hop table until the next reboot (experiments)
//   0x5A 0xC3 'M' / 'm'   monitor on / off: listen for receiver beacons in the idle quarter of every frame
//   0x5A 0xC3 'L' / 'l'   LED on / off (experiments). A short BOOTSEL tap prints "[button] tap".
// Pairing: hold BOOTSEL ~1.5 s -> 30 s pairing window (LED blinks fast); restart the receivers. Every
// receiver that asks gets our address; the LED blinks 1 s per confirmation and stays on 3 s afterwards.
// Status line once a second (CDC): [modem] mode= tx=N(+/s) under=N(+/s) drop=N(+/s) rst=N(+/s) depth= fifo=
//   addr= pair= beacons= paired= rx=  -- the (+N) are per-second deltas for diagnosing dropouts (see main loop).
#include <stdio.h>
#include "pico/stdlib.h"
#include "tusb.h"
#include "radio.h"
#include "usb_audio.h"
#include "cdc.h"
#include "board.h"

int main(void) {
    board_led_init();
    usb_audio_init();
    tud_init(0);
    radio_launch();                                      // nRF24 bus + core1 transmit loop

    uint32_t beacon_seen = 0;
    uint64_t hb = time_us_64() + 1000000;
    while (true) {
        tud_task();
        cdc_task();
        usb_audio_task();
        board_ui_task();

        uint8_t b[32]; uint32_t us, c;
        if (radio_take_beacon(&beacon_seen, b, &us, &c)) {
            char hx[65]; for (int i = 0; i < 32; i++) snprintf(hx + 2 * i, 3, "%02X", b[i]);
            cdc_printf("[beacon] %s t=%lu c=%lu\r\n", hx, (unsigned long)us, (unsigned long)c);
        }

        if (time_us_64() >= hb) {
            hb += 1000000;
            // per-second deltas: during a drop, watch which number jumps.
            //   under(+) spikes -> core0/USB not feeding the radio (audio starvation)
            //   drop(+)  spikes -> the frame ring overflowed (core0 bursting)
            //   rst(+)   > 0    -> the radio browned out / lost its config (power or interference)
            //   none move but audio drops -> clean transmit, the receiver lost lock (interference / receiver power)
            static uint32_t p_tx = 0, p_under = 0, p_drop = 0, p_rst = 0;
            uint32_t tx = radio_tx_count(), under = radio_under_count(), drop = radio_drop_count(), rst = radio_reset_count();
            const char *mode = cdc_raw_active() ? "raw" : usb_audio_streaming() ? "audio" : "idle";
            static const char *PS[] = {"-", "searching", "replied", "paired", "timeout"};
            uint8_t ad[4], pid[4]; radio_get_addr(ad); radio_get_paired_id(pid);
            cdc_printf("[modem] mode=%s tx=%lu(+%lu) under=%lu(+%lu) drop=%lu(+%lu) rst=%lu(+%lu) depth=%lu fifo=%u "
                       "addr=%02X%02X%02X%02X pair=%s beacons=%lu paired=%lu rx=%02X%02X%02X%02X\r\n",
                       mode, (unsigned long)tx, (unsigned long)(tx - p_tx), (unsigned long)under, (unsigned long)(under - p_under),
                       (unsigned long)drop, (unsigned long)(drop - p_drop), (unsigned long)rst, (unsigned long)(rst - p_rst),
                       (unsigned long)radio_depth(), usb_audio_fifo(), ad[0], ad[1], ad[2], ad[3],
                       PS[radio_pair_state()], (unsigned long)radio_pair_beacons(), (unsigned long)radio_pair_count(),
                       pid[0], pid[1], pid[2], pid[3]);
            p_tx = tx; p_under = under; p_drop = drop; p_rst = rst;
        }
    }
}
