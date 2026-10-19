#include "platform.h"
#include "ipc.h"
#include "audio.h"
#include "tusb.h"
#include "version.h"
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t raw_at, drops;
static bool raw_mode, user_mute, led_test, net_fault;
static volatile uint32_t milliseconds;
static const unsigned leds[] = {28, 29, 30, 31};
static const unsigned buttons[] = {23, 24, 8, 9};
extern void tusb_hal_nrf_power_event(uint32_t event);
static bool log_line(const char *fmt, ...) {
    if (!tud_cdc_connected()) return false;
    char b[1024]; va_list ap; va_start(ap, fmt);
    int n = vsnprintf(b, sizeof b, fmt, ap); va_end(ap);
    if (n > 0 && (unsigned)n < sizeof b) {
        unsigned size = (unsigned)n;
        /* Status is optional; never stall the audio loop waiting for a serial reader. */
        if (tud_cdc_write_available() < size) return false;
        tud_cdc_write(b, size);
        tud_cdc_write_flush();
        return true;
    }
    return false;
}
static void trace_task(void) {
    if (!tud_cdc_connected()) return;
    for (unsigned count = 0; count < 4; count++) {
        uint32_t tail = IPC->trace_tail;
        if (tail == IPC->trace_head) return;
        __DMB(); rf_trace_t t = IPC->trace[tail];
        char hex[65];
        for (unsigned i = 0; i < 32; i++) snprintf(hex + i * 2, 3, "%02X", t.payload[i]);
        if (!log_line("[rf] kind=%lu ch=%lu task=%lu ready=%lu address=%lu end=%lu ref=%lu "
            "BASE0=0x%08lX BASE1=0x%08lX PREFIX0=0x%08lX PREFIX1=0x%08lX TXADDRESS=0x%08lX "
            "PCNF0=0x%08lX PCNF1=0x%08lX CRCCNF=0x%08lX CRCINIT=0x%08lX CRCPOLY=0x%08lX "
            "MODE=0x%08lX FREQUENCY=0x%08lX TXPOWER=0x%08lX RXADDRESSES=0x%08lX SHORTS=0x%08lX "
            "crc_status=%lu rssi=-%lu header=%04lX data=%s\r\n",
            (unsigned long)t.kind, (unsigned long)t.channel, (unsigned long)t.task_us,
            (unsigned long)t.ready_us, (unsigned long)t.address_us, (unsigned long)t.end_us,
            (unsigned long)t.reference_us, (unsigned long)t.base, (unsigned long)t.base1,
            (unsigned long)t.prefix, (unsigned long)t.prefix1, (unsigned long)t.txaddress,
            (unsigned long)t.pcnf0, (unsigned long)t.pcnf1, (unsigned long)t.crccnf,
            (unsigned long)t.crcinit, (unsigned long)t.crcpoly, (unsigned long)t.mode,
            (unsigned long)t.channel, (unsigned long)t.txpower, (unsigned long)t.rxaddresses,
            (unsigned long)t.shorts, (unsigned long)t.crc, (unsigned long)t.rssi,
            (unsigned long)t.header, hex)) return;
        __DMB(); IPC->trace_tail = (tail + 1) % RF_TRACE_RING;
    }
}
static void set_led(unsigned i, bool on) {
    if (on) NRF_P0->OUTCLR = 1u << leds[i]; else NRF_P0->OUTSET = 1u << leds[i];
}
bool app_raw_active(void) { return raw_mode && !due(micros(), raw_at + 300000); }
static bool pairing_active(void) {
    if (IPC->pair_request != IPC->pair_ack) return IPC->pair_desired != 0;
    return IPC->pair_state == PAIR_SEARCH || IPC->pair_state == PAIR_REPLY;
}
bool app_push_frame(const uint8_t f[84]) {
    if (!IPC->usb_ready || !IPC->enabled || !IPC->active || pairing_active() ||
        IPC->epoch_ack != IPC->epoch || net_fault) return false;
    uint32_t h = IPC->head, next = (h + 1) % IPC_RING;
    if (next == IPC->tail) { drops++; return false; }
    memcpy(IPC->frames[h], f, 84); __DMB(); IPC->head = next;
    return true;
}
static void enable_tx(bool on) {
    IPC->enabled = on; __DMB(); audio_reset();
    log_line("[control] transmitter=%s\r\n", on ? "on" : "off");
}
static void mute_tx(bool on) {
    user_mute = on; IPC->muted = on; __DMB(); audio_reset();
    log_line("[control] mute=%u\r\n", on);
}
static void pair_toggle(void) {
    if (!IPC->usb_ready || net_fault) {
        log_line("[error] USB not configured or network core unavailable\r\n"); return;
    }
    bool cancel = pairing_active();
    if (!cancel) IPC->enabled = 1;
    IPC->pair_desired = !cancel;
    audio_reset(); __DMB(); IPC->pair_request++;
    log_line("[pair] %s\r\n", cancel ? "cancel requested" : "30 second window requested");
}
static void status(void) {
    static const char *const states[] = {"idle", "searching", "replied", "paired", "timeout", "cancelled"};
    uint32_t ps = IPC->pair_state;
    log_line("[modem] fw=" MINIMIC_FW_VERSION " board=nrf5340dk radio=builtin usb=%lu enabled=%lu tx_active=%lu mute=%u "
        "mode=%s net=%s tx=%lu under=%lu drop=%lu late=%lu faults=%lu depth=%lu fifo=%lu "
        "pair=%s paired=%lu beacons=%lu addr=12345678 rx=%02X%02X%02X%02X "
        "rx_addr=%lu crc_ok=%lu crc_bad=%lu rx_other=%lu replies=%lu delay_us=%lu\r\n",
        (unsigned long)IPC->usb_ready, (unsigned long)IPC->enabled, (unsigned long)IPC->active,
        user_mute, app_raw_active() ? "raw" : audio_streaming() ? "audio" : "idle",
        net_fault ? "fault" : IPC->ready == IPC_MAGIC ? "ready" : "starting",
        (unsigned long)IPC->tx_frames, (unsigned long)IPC->underflows, (unsigned long)drops,
        (unsigned long)IPC->late_frames, (unsigned long)IPC->faults, (unsigned long)queue_depth(),
        (unsigned long)audio_fifo(), ps < sizeof states / sizeof states[0] ? states[ps] : "invalid",
        (unsigned long)IPC->pair_count, (unsigned long)IPC->beacons,
        IPC->paired_id[0], IPC->paired_id[1], IPC->paired_id[2], IPC->paired_id[3],
        (unsigned long)IPC->rx_addresses, (unsigned long)IPC->rx_crc_ok,
        (unsigned long)IPC->rx_crc_bad, (unsigned long)IPC->rx_other,
        (unsigned long)IPC->pair_replies, (unsigned long)IPC->pair_last_delay);
}
static void command(const char *line) {
    if (!strcmp(line, "STATUS")) status();
    else if (!strcmp(line, "ON")) enable_tx(true);
    else if (!strcmp(line, "OFF")) enable_tx(false);
    else if (!strcmp(line, "MUTE")) mute_tx(true);
    else if (!strcmp(line, "UNMUTE")) mute_tx(false);
    else if (!strcmp(line, "PAIR")) { if (!pairing_active()) pair_toggle(); }
    else if (!strcmp(line, "CANCEL")) { if (pairing_active()) pair_toggle(); }
    else if (!strcmp(line, "MONITOR ON")) IPC->monitor = 1;
    else if (!strcmp(line, "MONITOR OFF")) IPC->monitor = 0;
    else if (!strncmp(line, "PAIRDELAY ", 10)) {
        char *end; unsigned long value = strtoul(line + 10, &end, 10);
        if (end == line + 10 || *end || (value && (value < 1000 || value > 20000)))
            log_line("[error] PAIRDELAY 0 (auto) or 1000..20000 microseconds\r\n");
        else {
            IPC->pair_delay_us = value;
            log_line("[control] pair_delay_us=%lu (0=auto)\r\n", value);
        }
    }
    else if (!strcmp(line, "REBOOT")) { IPC->enabled = 0; __DMB(); delay_us(8000); NVIC_SystemReset(); }
    else if (!strcmp(line, "HELP"))
        log_line("[help] STATUS | ON | OFF | MUTE | UNMUTE | PAIR | CANCEL | PAIRDELAY <0|1000..20000> | MONITOR ON | MONITOR OFF | REBOOT\r\n");
    else log_line("[error] unknown command; send HELP\r\n");
}
static void serial_task(void) {
    static unsigned state, used, line_used;
    static uint8_t frame[84]; static char line[64]; static bool overflow;
    static uint32_t byte_at;
    if ((state || line_used || overflow) && due(micros(), byte_at + 500000)) {
        state = used = line_used = 0; overflow = false;
    }
    uint8_t in[64];
    while (tud_cdc_available()) {
        unsigned n = tud_cdc_read(in, sizeof in);
        for (unsigned i = 0; i < n; i++) {
            uint8_t c = in[i]; byte_at = micros();
            if (state == 1) {
                state = c == 0xA5 ? 2 : c == 0xC3 ? 3 : c == 0x5A ? 1 : 0;
                used = 0; continue;
            }
            if (state == 2) {
                frame[used++] = c;
                if (used == 84) {
                    if (!app_raw_active()) audio_reset();
                    raw_at = micros(); raw_mode = true;
                    app_push_frame(frame); state = 0;
                }
                continue;
            }
            if (state == 3) {
                state = 0;
                switch (c) {
                case 'P': if (!pairing_active()) pair_toggle(); break;
                case 'R': command("REBOOT"); break;
                case 'M': IPC->monitor = 1; break;
                case 'm': IPC->monitor = 0; break;
                case 'L': led_test = true; break;
                case 'l': led_test = false; break;
                case 'B': log_line("[error] no USB bootloader; use onboard J-Link to update\r\n"); break;
                default: log_line("[error] unsupported legacy command\r\n"); break;
                }
                continue;
            }
            if (c == 0x5A && line_used == 0) { state = 1; continue; }
            if (c == '\n' || c == '\r') {
                if (overflow) log_line("[error] command too long\r\n");
                else if (line_used) { line[line_used] = 0; command(line); }
                line_used = 0; overflow = false;
            } else if (c >= 32 && c < 127) {
                if (line_used + 1 < sizeof line) line[line_used++] = (char)toupper(c);
                else overflow = true;
            }
        }
    }
}
/* The serial speed is informational. A 1200 baud touch must not reset an nRF DK. */
void tud_cdc_line_coding_cb(uint8_t itf, const cdc_line_coding_t *coding) { (void)itf; (void)coding; }
static void usb_off(void) { IPC->usb_ready = 0; raw_mode = false; audio_reset(); }
void tud_mount_cb(void) { audio_reset(); IPC->usb_ready = 1; }
void tud_umount_cb(void) { usb_off(); audio_stop(); }
void tud_suspend_cb(bool remote_wakeup) { (void)remote_wakeup; usb_off(); }
void tud_resume_cb(void) { audio_reset(); IPC->usb_ready = tud_mounted(); }
void USBD_IRQHandler(void) { tud_int_handler(0); }
void SysTick_Handler(void) { milliseconds++; }
uint32_t tusb_time_millis_api(void) { return milliseconds; }
static void usb_power_task(void) {
    /* Process edges through the same TinyUSB hook used by its Nordic BSP. */
    static bool vbus, ready;
    uint32_t reg = NRF_USBREGULATOR->USBREGSTATUS;
    bool now_vbus = (reg & USBREG_USBREGSTATUS_VBUSDETECT_Msk) != 0;
    bool now_ready = (reg & USBREG_USBREGSTATUS_OUTPUTRDY_Msk) != 0;
    if (now_vbus != vbus) {
        vbus = now_vbus;
        if (vbus) tusb_hal_nrf_power_event(0);
        else { usb_off(); tusb_hal_nrf_power_event(1); ready = false; }
    }
    if (vbus && now_ready && !ready) { ready = true; tusb_hal_nrf_power_event(2); }
}
static void ui_task(uint32_t now) {
    static uint32_t next, pressed_at[4], result_at, paired_at, last_count;
    static uint32_t last_state;
    static bool stable[4], sample[4], fired[4];
    if (due(now, next)) {
        next = now + 20000;
        for (unsigned i = 0; i < 4; i++) {
            bool down = (NRF_P0->IN & (1u << buttons[i])) == 0;
            if (down == sample[i] && down != stable[i]) {
                stable[i] = down;
                if (down) { pressed_at[i] = now; fired[i] = false; }
                else if (!fired[i]) {
                    if (i == 1) enable_tx(!IPC->enabled);
                    if (i == 2) mute_tx(!user_mute);
                    if (i == 3) status();
                }
            }
            sample[i] = down;
            if (i == 0 && stable[i] && !fired[i] && due(now, pressed_at[i] + 1500000)) {
                fired[i] = true; pair_toggle();
            }
        }
    }
    if (IPC->pair_state != last_state) { last_state = IPC->pair_state; result_at = now; }
    if (IPC->pair_count != last_count) { last_count = IPC->pair_count; paired_at = now; }
    set_led(0, IPC->usb_ready);
    set_led(1, IPC->active && IPC->enabled && !pairing_active());
    bool recent_pair = last_count && !due(now, paired_at + 1000000);
    bool pair_led = pairing_active() ? recent_pair || (now / 100000) % 2 :
        IPC->pair_state == PAIR_DONE && !due(now, result_at + 3000000);
    set_led(2, led_test || pair_led);
    bool pair_failed = IPC->pair_state == PAIR_TIMEOUT && !due(now, result_at + 3000000);
    set_led(3, user_mute || ((net_fault || IPC->faults || pair_failed) && (now / 250000) % 2));
}
int main(void) {
    platform_init();
    SysTick_Config(SystemCoreClock / 1000);
    for (unsigned i = 0; i < 4; i++) {
        NRF_P0->OUTSET = 1u << leds[i];
        NRF_P0->PIN_CNF[leds[i]] = GPIO_PIN_CNF_DIR_Output | (GPIO_PIN_CNF_INPUT_Disconnect << GPIO_PIN_CNF_INPUT_Pos);
        NRF_P0->PIN_CNF[buttons[i]] = GPIO_PIN_CNF_PULL_Pullup << GPIO_PIN_CNF_PULL_Pos;
    }
    /* Stop any previous network image before initializing its shared memory. */
    NRF_RESET->NETWORK.FORCEOFF = 1; __DSB();
    for (unsigned region = 56; region < 64; region++)
        NRF_SPU->RAMREGION[region].PERM = SPU_RAMREGION_PERM_READ_Msk | SPU_RAMREGION_PERM_WRITE_Msk;
    NRF_SPU->EXTDOMAIN[0].PERM = 0; /* Network bus is nonsecure; mailbox is nonsecure. */
    memset(IPC, 0, sizeof *IPC);
    IPC->enabled = 1; IPC->trace_enabled = 1; IPC->magic = IPC_MAGIC; __DSB();
    NRF_RESET->NETWORK.FORCEOFF = 0; __DSB();
    audio_init();
    NVIC_SetPriority(USBD_IRQn, 2);
    tusb_rhport_init_t init = {.role = TUSB_ROLE_DEVICE, .speed = TUSB_SPEED_FULL};
    tusb_init(0, &init);
    uint32_t next_status = micros() + 1000000, last_heartbeat = 0, beacon_seen = 0;
    for (;;) {
        usb_power_task(); tud_task();
        if (IPC->usb_ready && (!tud_mounted() || tud_suspended())) {
            usb_off();
            if (!tud_mounted()) audio_stop();
        }
        serial_task();
        trace_task();
        if (raw_mode && !app_raw_active()) { raw_mode = false; audio_reset(); }
        audio_task();
        uint32_t now = micros(); ui_task(now);
        if (due(now, next_status)) {
            next_status = now + 1000000;
            uint32_t h = IPC->heartbeat;
            net_fault = IPC->ready != IPC_MAGIC || h == last_heartbeat;
            last_heartbeat = h;
            if (net_fault) IPC->enabled = 0;
            status();
        }
        if (IPC->monitor || pairing_active()) {
            uint32_t seq = IPC->beacon_seq;
            if (!(seq & 1) && seq != beacon_seen) {
                uint8_t b[32]; __DMB(); memcpy(b, IPC->last_beacon, 32); __DMB();
                if (IPC->beacon_seq == seq) {
                    char hex[65];
                    for (unsigned i = 0; i < 32; i++) snprintf(hex + 2 * i, 3, "%02X", b[i]);
                    log_line("[beacon] %s\r\n", hex); beacon_seen = seq;
                }
            }
        }
    }
}
