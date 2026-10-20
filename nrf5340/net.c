/* Dedicated network core: no BLE stack, interrupts or USB work in the RF schedule. */
#include "platform.h"
#include "ipc.h"
#include "wire.h"
#include <string.h>

static uint8_t dma[34] __attribute__((aligned(4)));
static uint32_t rx_task_us, pair_reference_us;
static uint32_t pair_target_start_us, pair_sweep_step;
static bool permitted(void) { return (IPC->usb_ready || IPC->debug_radio) && IPC->enabled; }
static void trace_packet(uint32_t kind, uint32_t task, uint32_t reference) {
    if (!IPC->trace_enabled) return;
    uint32_t h = IPC->trace_head, next = (h + 1) % RF_TRACE_RING;
    if (next == IPC->trace_tail) { IPC->trace_dropped++; return; }
    rf_trace_t *t = &IPC->trace[h];
    t->kind = kind; t->channel = NRF_RADIO->FREQUENCY;
    t->task_us = task; t->ready_us = NRF_TIMER0->CC[1];
    t->address_us = NRF_TIMER0->CC[2]; t->end_us = NRF_TIMER0->CC[3];
    t->reference_us = reference;
    t->base = NRF_RADIO->BASE0; t->prefix = NRF_RADIO->PREFIX0;
    t->crc = NRF_RADIO->CRCSTATUS;
    t->rssi = kind <= RF_RX_BAD ? NRF_RADIO->RSSISAMPLE : 0;
    t->pcnf0 = NRF_RADIO->PCNF0; t->pcnf1 = NRF_RADIO->PCNF1;
    t->mode = NRF_RADIO->MODE;
    t->base1 = NRF_RADIO->BASE1; t->prefix1 = NRF_RADIO->PREFIX1;
    t->txaddress = NRF_RADIO->TXADDRESS;
    t->crccnf = NRF_RADIO->CRCCNF; t->crcinit = NRF_RADIO->CRCINIT;
    t->crcpoly = NRF_RADIO->CRCPOLY; t->txpower = NRF_RADIO->TXPOWER;
    t->rxaddresses = NRF_RADIO->RXADDRESSES; t->shorts = NRF_RADIO->SHORTS;
    t->vreqh = NRF_VREQCTRL_NS->VREGRADIO.VREQH;
    t->vreqhready = NRF_VREQCTRL_NS->VREGRADIO.VREQHREADY;
    t->target_start_us = kind >= RF_TX_OK ? pair_target_start_us : 0;
    t->sweep_step = kind >= RF_TX_OK ? pair_sweep_step : UINT32_MAX;
    bool header = kind >= RF_TX_OK && NRF_RADIO->PCNF0 != 0;
    t->header = header ? ((uint32_t)dma[0] << 8) | dma[1] : 0;
    memcpy(t->payload, dma + (header ? 2 : 0), 32);
    __DMB(); IPC->trace_head = next;
}
static bool rf_off(void) {
    if (NRF_RADIO->STATE == RADIO_STATE_STATE_Disabled) return true;
    NRF_RADIO->EVENTS_DISABLED = 0;
    NRF_RADIO->TASKS_DISABLE = 1;
    uint32_t end = micros() + 500;
    while (!NRF_RADIO->EVENTS_DISABLED) {
        if (due(micros(), end)) { IPC->faults++; return false; }
    }
    return true;
}
static void rf_address(const uint8_t addr[4]) {
    NRF_RADIO->BASE0 = mm_radio_base(addr);
    NRF_RADIO->PREFIX0 = mm_radio_prefix(addr);
}
static void rf_init(void) {
    NRF_VREQCTRL_NS->VREGRADIO.VREQH = VREQCTRL_VREGRADIO_VREQH_VREQH_Disabled;
    NRF_RADIO->POWER = 1;
    NRF_RADIO->MODE = RADIO_MODE_MODE_Nrf_2Mbit;
    NRF_RADIO->TXPOWER = RADIO_TXPOWER_TXPOWER_0dBm;
    /* firmware/nrf24.c sets EN_AA=0 and SETUP_RETR=0. nRF24L01+ PS
     * section 7.9: this selects legacy ShockBurst and REMOVES the 9-bit
     * ESB packet-control field. No S0, LENGTH or S1 bytes exist in RAM/on air.
     * Do not use the Nordic ESB library's fixed-length (S0=8,S1=1) format. */
    NRF_RADIO->PCNF0 = 0;
    NRF_RADIO->PCNF1 = (32u << RADIO_PCNF1_MAXLEN_Pos) | (32u << RADIO_PCNF1_STATLEN_Pos) |
        (3u << RADIO_PCNF1_BALEN_Pos) | (RADIO_PCNF1_ENDIAN_Big << RADIO_PCNF1_ENDIAN_Pos);
    NRF_RADIO->CRCCNF = RADIO_CRCCNF_LEN_Two; /* CRC includes address. */
    NRF_RADIO->CRCINIT = 0xFFFF;
    NRF_RADIO->CRCPOLY = 0x11021;
    NRF_RADIO->TXADDRESS = 0; NRF_RADIO->RXADDRESSES = 1;
    NRF_RADIO->SHORTS = RADIO_SHORTS_READY_START_Msk | RADIO_SHORTS_END_DISABLE_Msk;
    /* RADIO events capture timestamps in hardware. Polling latency and USB
     * logging cannot move these samples. CC[0] remains reserved for micros(). */
    NRF_RADIO->PUBLISH_READY = 0x80000000u | 0;
    NRF_RADIO->PUBLISH_ADDRESS = 0x80000000u | 1;
    NRF_RADIO->PUBLISH_END = 0x80000000u | 2;
    NRF_TIMER0->SUBSCRIBE_CAPTURE[1] = 0x80000000u | 0;
    NRF_TIMER0->SUBSCRIBE_CAPTURE[2] = 0x80000000u | 1;
    NRF_TIMER0->SUBSCRIBE_CAPTURE[3] = 0x80000000u | 2;
    NRF_DPPIC_NS->CHENSET = 7;
    NRF_RADIO->PACKETPTR = (uint32_t)(uintptr_t)dma;
}
static bool rf_tx(const uint8_t payload[32], uint8_t channel, const uint8_t addr[4], uint32_t at) {
    if (!rf_off()) return false;
    rf_address(addr); NRF_RADIO->FREQUENCY = channel;
    uint32_t pcf = IPC->pair_state == PAIR_REPLY ? IPC->pair_pcf : 0;
    if (pcf >= 1 && pcf <= 3) {
        /* TX-only interoperability experiment. The default remains legacy
         * ShockBurst, and every RX is restored to the proven beacon format. */
        NRF_RADIO->PCNF0 = (1u << RADIO_PCNF0_S0LEN_Pos) | (1u << RADIO_PCNF0_S1LEN_Pos);
        dma[0] = (uint8_t)((pcf >= 2 ? 0x80 : 0) | (IPC->pair_replies & 3));
        dma[1] = pcf == 3 ? 1 : 0;
        memcpy(dma + 2, payload, 32);
    } else {
        NRF_RADIO->PCNF0 = 0;
        memcpy(dma, payload, 32);
    }
    NRF_RADIO->EVENTS_DISABLED = 0;
    NRF_RADIO->EVENTS_END = 0;
    __DMB(); wait_until(at);
    if (!permitted()) return false;
    uint32_t task = micros();
    NRF_RADIO->TASKS_TXEN = 1;
    uint32_t end = micros() + 700;
    while (!NRF_RADIO->EVENTS_DISABLED) {
        if (due(micros(), end)) {
            IPC->faults++; rf_off();
            if (IPC->pair_state == PAIR_REPLY) trace_packet(RF_TX_TIMEOUT, task, pair_reference_us);
            return false;
        }
    }
    if (IPC->pair_state == PAIR_REPLY) trace_packet(RF_TX_OK, task, pair_reference_us);
    return true;
}
static void rx_start(uint8_t channel) {
    static const uint8_t addr[] = {0xBC, 0xBC, 0xBC, 0xBC};
    if (!rf_off()) return;
    rf_address(addr); NRF_RADIO->FREQUENCY = channel;
    NRF_RADIO->PCNF0 = 0;
    memset(dma, 0, sizeof dma);
    NRF_RADIO->EVENTS_END = 0;
    NRF_RADIO->EVENTS_DISABLED = 0;
    NRF_RADIO->EVENTS_ADDRESS = 0;
    NRF_RADIO->EVENTS_RSSIEND = 0;
    NRF_RADIO->SHORTS |= RADIO_SHORTS_ADDRESS_RSSISTART_Msk | RADIO_SHORTS_DISABLED_RSSISTOP_Msk;
    rx_task_us = micros();
    NRF_RADIO->TASKS_RXEN = 1;
}
static bool rx_poll(uint8_t b[32], uint8_t channel, uint32_t *received_end) {
    if (NRF_RADIO->EVENTS_ADDRESS) {
        NRF_RADIO->EVENTS_ADDRESS = 0;
        IPC->rx_addresses++;
    }
    if (!NRF_RADIO->EVENTS_END) return false;
    bool valid = NRF_RADIO->CRCSTATUS != 0;
    uint32_t captured_end = NRF_TIMER0->CC[3];
    if (valid) IPC->rx_crc_ok++; else IPC->rx_crc_bad++;
    rf_off();
    trace_packet(valid ? RF_RX_OK : RF_RX_BAD, rx_task_us, 0);
    if (valid) memcpy(b, dma, 32);
    rx_start(channel);
    bool beacon = valid && (b[3] == 0x1E || b[3] == 0x16) && b[4] == 3;
    if (valid && !beacon) IPC->rx_other++;
    if (beacon && received_end) *received_end = captured_end;
    return beacon;
}
static void record_beacon(const uint8_t b[32], uint32_t at, uint32_t counter) {
    IPC->beacon_seq++; __DMB();
    memcpy(IPC->last_beacon, b, 32);
    IPC->beacon_time = at; IPC->beacon_counter = counter;
    __DMB(); IPC->beacon_seq++;
    IPC->beacons++;
}
static void discard_queue(void) {
    IPC->tail = IPC->head;
    __DMB(); IPC->epoch_ack = IPC->epoch;
}
static bool pair_power(bool high) {
    /* nRF5340 PS: TXPOWER=0 + VREQH=1 gives +3 dBm. Positive TXPOWER
     * enumerators are not supported on this SoC. Wait before enabling RADIO. */
    NRF_VREQCTRL_NS->VREGRADIO.VREQH = high ?
        VREQCTRL_VREGRADIO_VREQH_VREQH_Enabled : VREQCTRL_VREGRADIO_VREQH_VREQH_Disabled;
    if (!high) return true;
    uint32_t deadline = micros() + 1000;
    while (!(NRF_VREQCTRL_NS->VREGRADIO.VREQHREADY & VREQCTRL_VREGRADIO_VREQHREADY_READY_Msk)) {
        if (due(micros(), deadline)) {
            IPC->faults++;
            NRF_VREQCTRL_NS->VREGRADIO.VREQH = VREQCTRL_VREGRADIO_VREQH_VREQH_Disabled;
            return false;
        }
    }
    return true;
}
static void pairing(void) {
    uint8_t done[16][4]; unsigned n = 0;
    IPC->active = 0; IPC->pair_count = 0; IPC->beacons = 0;
    IPC->pair_replies = 0; IPC->pair_last_delay = 0;
    IPC->pair_state = PAIR_SEARCH;
    discard_queue();
    pair_target_start_us = 0; pair_sweep_step = UINT32_MAX;
    if (!rf_off() || !pair_power(IPC->pair_tx_dbm == 3)) {
        IPC->pair_state = PAIR_CANCELLED;
        return;
    }
    IPC->pair_started_us = micros();
    uint32_t end = IPC->pair_started_us + 30000000;
    IPC->pair_deadline_us = end;
    unsigned ci = 0;
    bool cancelled = false;
    while (!due(micros(), end)) {
        uint8_t ch = mm_pair_channels[ci++ & 3];
        rx_start(ch);
        uint32_t dwell = micros() + 765000;
        while (!due(micros(), dwell) && !due(micros(), end)) {
            IPC->heartbeat++;
            if (!permitted() || !IPC->pair_desired || IPC->pair_request != IPC->pair_ack) { cancelled = true; break; }
            uint8_t b[32];
            uint32_t heard;
            if (!rx_poll(b, ch, &heard)) continue;
            record_beacon(b, heard, 0);
            bool known = false;
            for (unsigned i = 0; i < n; i++) if (!memcmp(done[i], b + 8, 4)) known = true;
            if (b[3] == 0x16 && !memcmp(b + 22, mm_address, 4)) {
                if (!known) {
                    if (n < 16) memcpy(done[n++], b + 8, 4);
                    memcpy(IPC->paired_id, b + 8, 4); __DMB(); IPC->pair_count++;
                }
            } else if ((b[28] & 0x80) && !(b[29] & 8) && !known) {
                uint8_t r[32]; mm_reply(r, b);
                IPC->pair_state = PAIR_REPLY;
                /* The original 5800 us follows SPI register/payload writes.
                 * Native RADIO has a different preparation/ramp-up delay.
                 * Try one reply per request; never fill the receiver's window
                 * with a burst. A fixed delay can be selected over CDC. */
                uint32_t delay = IPC->pair_delay_us;
                if (IPC->pair_start_sweep) {
                    /* Target the beginning of transmission, not ADDRESS or
                     * TXEN. The previous hardware trace measured TXEN->READY
                     * at 130 us. Log both requested and actual READY timing;
                     * CPU polling may add a few microseconds. Repeat cyclically
                     * so each target gets three replies within one window. */
                    static const uint16_t starts[] = {5950, 6000, 6050, 6100, 6150, 6200};
                    pair_sweep_step = (IPC->pair_replies / 3) % 6;
                    pair_target_start_us = starts[pair_sweep_step];
                    delay = pair_target_start_us - 130;
                } else if (delay < 1000 || delay > 20000) {
                    unsigned i = IPC->pair_replies % 41;
                    int offset = (int)((i + 1) / 2) * 100;
                    delay = (uint32_t)(5800 + (i & 1 ? offset : -offset));
                }
                IPC->pair_last_delay = delay;
                pair_reference_us = heard;
                uint8_t target[4];
                mm_pair_target(target, b + 8, IPC->pair_address_order, IPC->pair_replies);
                if (rf_tx(r, ch, target, heard + delay)) IPC->pair_replies++;
                rx_start(ch); IPC->pair_state = PAIR_SEARCH;
            }
        }
        if (cancelled) break;
    }
    rf_off();
    pair_power(false);
    IPC->pair_ack = IPC->pair_request;
    IPC->pair_state = cancelled ? PAIR_CANCELLED : IPC->pair_count ? PAIR_DONE : PAIR_TIMEOUT;
    discard_queue();
}
int main(void) {
    platform_init();
    while (IPC->magic != IPC_MAGIC) {}
    __DMB(); rf_init(); IPC->ready = IPC_MAGIC;
    uint8_t counter = 0;
    uint32_t epoch = micros() + 2000;
    for (;;) {
        IPC->heartbeat++;
        if (!permitted()) {
            rf_off(); IPC->active = 0; discard_queue();
            IPC->pair_ack = IPC->pair_request;
            epoch = micros() + 2000; counter = 0;
            delay_us(100);
            continue;
        }
        if (IPC->pair_request != IPC->pair_ack) {
            IPC->pair_ack = IPC->pair_request;
            if (IPC->pair_desired) pairing();
            epoch = micros() + 2000; counter = 0;
            continue;
        }
        if (IPC->epoch != IPC->epoch_ack) discard_queue();
        IPC->active = 1;
        uint8_t fr[84], p[3][32], copy[32];
        bool have = IPC->head != IPC->tail;
        if (have) {
            __DMB(); memcpy(fr, IPC->frames[IPC->tail], 84); __DMB();
            IPC->tail = (IPC->tail + 1) % IPC_RING;
        } else if (!IPC->muted) IPC->underflows++;
        for (unsigned s = 0; s < 3; s++)
            mm_payload(p[s], s, counter, have && !IPC->muted ? fr + 28 * s : NULL);
        bool sent = true;
        for (unsigned s = 0; s < 3 && permitted(); s++) {
            uint8_t ch = mm_hop[(3 * counter + s) % 16];
            sent &= rf_tx(p[s], ch, mm_address, epoch + s * MM_SLOT_US);
            if (s < 2) {
                /* Original ordering: A, copy-of-B; B, copy-of-A; parity. */
                memcpy(copy, p[1 - s], 32); copy[0] |= 0x20;
                sent &= rf_tx(copy, ch, mm_address, micros());
            }
        }
        if (sent && permitted()) IPC->tx_frames++;
        if (IPC->monitor && permitted()) {
            uint8_t ch = mm_pair_channels[(IPC->tx_frames / 100) % 4];
            rx_start(ch);
            while (!due(micros(), epoch + MM_FRAME_US - 300) && permitted()) {
                uint8_t b[32];
                if (rx_poll(b, ch, NULL)) record_beacon(b, micros() - epoch, counter);
            }
            rf_off();
        }
        counter = (counter + 1) % 48;
        epoch += MM_FRAME_US;
        if (due(micros(), epoch)) { IPC->late_frames++; epoch = micros() + 2000; }
    }
}
