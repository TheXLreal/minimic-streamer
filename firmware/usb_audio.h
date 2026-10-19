// usb_audio.h - the USB side of the sound card: UAC1 speaker requests, and pulling PC audio out of the
// TinyUSB FIFO, running it through the codec encoder, and handing finished frames to the radio.
#pragma once
#include <stdint.h>
#include <stdbool.h>

void     usb_audio_init(void);     // init the encoder
void     usb_audio_task(void);     // call from the core0 loop
bool     usb_audio_streaming(void);
unsigned usb_audio_fifo(void);     // bytes available in the audio FIFO (for the status line)
