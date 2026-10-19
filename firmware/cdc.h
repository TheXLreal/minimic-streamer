// cdc.h - the USB CDC serial port: the binary command protocol from the PC and the printf used for the
// status line and beacon dumps. See the protocol table at the top of main.c.
#pragma once
#include <stdint.h>
#include <stdbool.h>

void     cdc_task(void);                 // call from the core0 loop: parse incoming bytes
void     cdc_printf(const char *fmt, ...);
bool     cdc_raw_active(void);           // true while raw 84-byte frames are arriving (USB audio is ignored then)
