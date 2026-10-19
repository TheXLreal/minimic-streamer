// radio.h - the radio "core": everything that runs on core1 and drives the single nRF24.
// It transmits codec frames on the microphone's hopping grid, runs pairing, and (optionally) listens
// for receiver beacons in the idle quarter of each frame. Core0 feeds it frames and reads telemetry.
#pragma once
#include <stdint.h>
#include <stdbool.h>

enum { RADIO_PAIR_IDLE = 0, RADIO_PAIR_SEARCH, RADIO_PAIR_REPLIED, RADIO_PAIR_DONE, RADIO_PAIR_TIMEOUT };

void     radio_launch(void);                     // init the bus and start core1
bool     radio_core_ready(void);

// frame queue (84 bytes = three 28-byte codec blocks for packets 0x18, 0x19, 0x1A)
bool     radio_push_frame(const uint8_t frame84[84]);
uint32_t radio_depth(void);

// runtime controls
void     radio_set_addr(const uint8_t addr[4]);  // use this address until reboot (CDC 'A')
void     radio_set_hop(const uint8_t hop[16]);   // use this hop table until reboot (CDC 'H')
void     radio_set_monitor(bool on);             // listen for beacons in the idle quarter (CDC 'M'/'m')
void     radio_start_pairing(void);              // open a pairing window (CDC 'P')
void     radio_button_pairing(void);             // BOOTSEL long-press: start, or cancel if already pairing

// telemetry (read from core0)
void     radio_get_addr(uint8_t out[4]);
int      radio_pair_state(void);
bool     radio_pairing_active(void);             // true while a pairing window is opening or open (for the LED)
uint32_t radio_pair_beacons(void);
uint32_t radio_pair_count(void);
void     radio_get_paired_id(uint8_t out[4]);
uint32_t radio_tx_count(void);
uint32_t radio_under_count(void);
uint32_t radio_drop_count(void);                 // frames dropped because the ring was full
uint32_t radio_reset_count(void);                // times the radio was found misconfigured and re-initialised
// a beacon captured by pairing/monitor; returns true and fills the buffers when newer than *seq
bool     radio_take_beacon(uint32_t *seq, uint8_t buf[32], uint32_t *us, uint32_t *counter);
