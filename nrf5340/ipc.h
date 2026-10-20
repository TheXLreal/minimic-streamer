#pragma once
#include <stdint.h>
#include <stddef.h>
#include "nrf.h"
#include "nrfx_config.h"
#define IPC_MAGIC 0x4D4D530Au /* Includes the mailbox ABI version. */
#define IPC_RING 32u
#define RF_TRACE_RING 256u
#define FRAME_US 6144u
enum { PAIR_IDLE, PAIR_SEARCH, PAIR_REPLY, PAIR_DONE, PAIR_TIMEOUT, PAIR_CANCELLED };
enum { RF_RX_OK = 1, RF_RX_BAD, RF_TX_OK, RF_TX_TIMEOUT, RF_AUDIO_TX, RF_AUDIO_TIMEOUT,
       RF_AUDIO_RESTART, RF_AUDIO_RESTART_TIMEOUT };
typedef struct {
    uint32_t kind, channel, task_us, ready_us, address_us, end_us, reference_us;
    uint32_t base, prefix, crc, rssi, pcnf0, pcnf1, mode, header;
    uint32_t base1, prefix1, txaddress, crccnf, crcinit, crcpoly, txpower, rxaddresses, shorts;
    uint32_t vreqh, vreqhready, target_start_us, sweep_step;
    uint8_t payload[32];
} rf_trace_t;
typedef struct {
    /* app is the only writer of this control block and head. */
    volatile uint32_t magic, usb_ready, enabled, muted, monitor, pair_request, pair_desired, epoch;
    volatile uint32_t pair_delay_us; /* 0: scan around original nRF24 timing. */
    volatile uint32_t pair_address_order; /* Diagnostic: 0 SPI order, 1 on-air order in beacon. */
    volatile uint32_t pair_pcf; /* Diagnostic TX-only PCF: 0 none, 1 fixed, 2/3 length=32. */
    volatile uint32_t pair_start_sweep; /* Diagnostic: six READY/START targets, three replies each. */
    volatile uint32_t pair_tx_dbm; /* Diagnostic pairing power: 0 or +3 via VREQCTRL. */
    volatile uint32_t tx_dbm, trace_audio_frames; /* Normal RF power; bounded audio trace request. */
    volatile uint32_t audio_gap_us; /* 0..300: keep TX active between slot packets; 1000: old ramp-up. */
    /* Debugger-only opt-in permits RF without USB while powered through J2.
     * Both fields reset at boot; normal operation still requires USB J3. */
    volatile uint32_t debug_radio, trace_enabled, trace_tail;
    volatile uint32_t head;
    uint8_t frames[IPC_RING][84];
    /* net is the only writer of tail and the status block below. */
    volatile uint32_t tail, ready, active, epoch_ack, heartbeat;
    volatile uint32_t pair_ack, pair_state, pair_count, beacons;
    volatile uint32_t pair_replies, pair_last_delay;
    volatile uint32_t pair_started_us, pair_deadline_us;
    volatile uint32_t tx_frames, underflows, late_frames, faults;
    volatile uint32_t rx_addresses, rx_crc_ok, rx_crc_bad, rx_other;
    volatile uint32_t beacon_seq, beacon_time, beacon_counter;
    uint8_t paired_id[4], last_beacon[32];
    volatile uint32_t trace_head, trace_dropped;
    rf_trace_t trace[RF_TRACE_RING];
} minimic_ipc_t;
#define IPC ((minimic_ipc_t *)(uintptr_t)0x20070000u)
_Static_assert(sizeof(minimic_ipc_t) < 65536, "IPC exceeds reserved RAM");
static inline uint32_t queue_depth(void) { return (IPC->head + IPC_RING - IPC->tail) % IPC_RING; }
