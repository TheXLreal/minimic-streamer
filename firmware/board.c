// board.c - see board.h. LED, BOOTSEL reading (briefly disconnects flash, so core1 is paused and
// interrupts are off while we look), reboot/reset, and the USB "reset interface" so picotool can
// reboot the board without the button.
#include "board.h"
#include "radio.h"
#include "cdc.h"
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "pico/bootrom.h"
#include "pico/usb_reset_interface.h"
#include "hardware/structs/ioqspi.h"
#include "hardware/structs/sio.h"
#include "hardware/watchdog.h"
#include "hardware/sync.h"
#include "tusb.h"
#include "device/usbd_pvt.h"
#if MINIMIC_HAS_CYW43_LED
#include "pico/cyw43_arch.h"
#endif

// ---------------------------------------------------------------- LED (board-dependent)
static bool led_ok = false;
static volatile bool led_force = false;

void board_led_init(void) {
#if MINIMIC_HAS_CYW43_LED
    led_ok = (cyw43_arch_init() == 0);               // LED is on the Wi-Fi chip (pico_w / pico2_w)
#elif defined(PICO_DEFAULT_LED_PIN)
    gpio_init(PICO_DEFAULT_LED_PIN); gpio_set_dir(PICO_DEFAULT_LED_PIN, GPIO_OUT);
    led_ok = true;                                   // plain GPIO LED (pico / pico2)
#endif
}
void board_led(bool on) {
    static int cur = -1;
    if (!led_ok || cur == (int)on) return;
    cur = on;
#if MINIMIC_HAS_CYW43_LED
    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, on);
#elif defined(PICO_DEFAULT_LED_PIN)
    gpio_put(PICO_DEFAULT_LED_PIN, on);
#endif
}
void board_led_force(bool on) { led_force = on; }

// ---------------------------------------------------------------- reboot / BOOTSEL entry
void board_to_bootsel(void) { rom_reset_usb_boot_extra(-1, 0, false); }
void board_reboot(void) { watchdog_reboot(0, 0, 10); }

// ---------------------------------------------------------------- BOOTSEL button
// The QSPI chip-select "pin" is bit 1 of gpio_hi on RP2040; RP2350 gives it a named macro.
#ifdef SIO_GPIO_HI_IN_QSPI_CSN_BITS
#define QSPI_CSN_BIT SIO_GPIO_HI_IN_QSPI_CSN_BITS
#else
#define QSPI_CSN_BIT (1u << 1)
#endif
static bool __no_inline_not_in_flash_func(bootsel_raw)(void) {
    uint32_t flags = save_and_disable_interrupts();
    hw_write_masked(&ioqspi_hw->io[1].ctrl, GPIO_OVERRIDE_LOW << IO_QSPI_GPIO_QSPI_SS_CTRL_OEOVER_LSB,
                    IO_QSPI_GPIO_QSPI_SS_CTRL_OEOVER_BITS);
    for (volatile int i = 0; i < 1000; i++);
    bool down = !(sio_hw->gpio_hi_in & QSPI_CSN_BIT);
    hw_write_masked(&ioqspi_hw->io[1].ctrl, GPIO_OVERRIDE_NORMAL << IO_QSPI_GPIO_QSPI_SS_CTRL_OEOVER_LSB,
                    IO_QSPI_GPIO_QSPI_SS_CTRL_OEOVER_BITS);
    restore_interrupts(flags);
    return down;
}
static bool bootsel_down(void) {
    multicore_lockout_start_blocking();
    bool d = bootsel_raw();
    multicore_lockout_end_blocking();
    return d;
}

void board_ui_task(void) {
    static uint64_t next = 0, down_since = 0, done_at = 0, one_at = 0; static bool was = false, fired = false;
    static int last_state = RADIO_PAIR_IDLE; static uint32_t last_count = 0;
    uint64_t now = time_us_64();
    if (radio_core_ready() && now >= next) {
        next = now + 50000;
        bool d = bootsel_down();
        if (d && !was) { down_since = now; fired = false; }
        if (!d && was && !fired && now - down_since < 1000000) cdc_printf("[button] tap\r\n");  // short press
        if (d && !fired && now - down_since > 1500000) { fired = true; radio_button_pairing(); }
        was = d;
    }
    int ps = radio_pair_state();
    if (ps != last_state) { if (ps == RADIO_PAIR_DONE) done_at = now; last_state = ps; }
    uint32_t pc = radio_pair_count();
    if (pc != last_count) { if (pc > last_count) one_at = now; last_count = pc; }
    if (radio_pairing_active()) board_led(now - one_at < 1000000 || ((now / 100000) & 1));  // fast blink; 1 s per paired
    else board_led(led_force || (ps == RADIO_PAIR_DONE && now - done_at < 3000000));        // 3 s on after pairing
}

// ---------------------------------------------------------------- USB reset interface (as in pico_stdio_usb)
static uint8_t reset_itf;
static void resetd_init(void) {}
static void resetd_reset(uint8_t rhport) { (void)rhport; reset_itf = 0; }
static uint16_t resetd_open(uint8_t rhport, tusb_desc_interface_t const *d, uint16_t max_len) {
    (void)rhport;
    TU_VERIFY(d->bInterfaceClass == TUSB_CLASS_VENDOR_SPECIFIC && d->bInterfaceSubClass == RESET_INTERFACE_SUBCLASS &&
              d->bInterfaceProtocol == RESET_INTERFACE_PROTOCOL, 0);
    TU_VERIFY(max_len >= sizeof(tusb_desc_interface_t), 0);
    reset_itf = d->bInterfaceNumber;
    return sizeof(tusb_desc_interface_t);
}
static bool resetd_control_xfer_cb(uint8_t rhport, uint8_t stage, tusb_control_request_t const *r) {
    (void)rhport;
    if (stage != CONTROL_STAGE_SETUP) return true;
    if (r->wIndex == reset_itf) {
        if (r->bRequest == RESET_REQUEST_BOOTSEL) rom_reset_usb_boot_extra(-1, r->wValue & 0x7f, false);
        if (r->bRequest == RESET_REQUEST_FLASH) { board_reboot(); return true; }
    }
    return false;
}
static bool resetd_xfer_cb(uint8_t rhport, uint8_t ep, xfer_result_t res, uint32_t n) {
    (void)rhport; (void)ep; (void)res; (void)n; return true;
}
static usbd_class_driver_t const resetd_driver = {
    .init = resetd_init, .reset = resetd_reset, .open = resetd_open,
    .control_xfer_cb = resetd_control_xfer_cb, .xfer_cb = resetd_xfer_cb, .sof = NULL };
usbd_class_driver_t const *usbd_app_driver_get_cb(uint8_t *count) { *count = 1; return &resetd_driver; }
