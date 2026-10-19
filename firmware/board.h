// board.h - board-specific bits that differ between Pico variants: the LED (on the Wi-Fi chip for the
// "W" boards, a plain GPIO otherwise), the BOOTSEL button, and reboot / reset-into-BOOTSEL. Also the UI
// task: a short BOOTSEL tap prints "[button] tap"; a ~1.5 s hold starts (or cancels) pairing; the LED
// blinks while pairing and is otherwise off.
#pragma once
#include <stdbool.h>

void board_led_init(void);
void board_led(bool on);
void board_led_force(bool on);     // CDC 'L'/'l': force the LED on/off (a PC script asking the user something)
void board_to_bootsel(void);       // reboot into BOOTSEL (UF2 drive / picotool)
void board_reboot(void);           // plain reboot
void board_ui_task(void);          // call from the core0 loop
