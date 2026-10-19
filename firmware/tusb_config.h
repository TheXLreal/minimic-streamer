// TinyUSB config for MiniMic Streamer: UAC1 speaker (48 kHz, 16-bit stereo, async + feedback),
// a CDC serial port (status, raw frame mode, commands) and the Pico reset interface (picotool -f).
#pragma once
#include "pico.h"                  // __not_in_flash for TU_ATTR_FAST_FUNC

#ifndef CFG_TUSB_OS
#define CFG_TUSB_OS                 OPT_OS_NONE
#endif
#define CFG_TUSB_DEBUG              0
#define CFG_TUD_ENABLED             1
#define CFG_TUD_MAX_SPEED           OPT_MODE_FULL_SPEED
#define CFG_TUSB_MEM_SECTION
#define CFG_TUSB_MEM_ALIGN          __attribute__((aligned(4)))
#define CFG_TUD_ENDPOINT0_SIZE      64

#define CFG_TUD_AUDIO               1
#define CFG_TUD_CDC                 1
#define CFG_TUD_HID                 0
#define CFG_TUD_MSC                 0
#define CFG_TUD_MIDI                0
#define CFG_TUD_VENDOR              0

// audio: host -> device stereo 16-bit, 48 kHz
#define CFG_TUD_AUDIO_FUNC_1_N_CHANNELS_RX          2
#define CFG_TUD_AUDIO_FUNC_1_N_BYTES_PER_SAMPLE_RX  2
#define CFG_TUD_AUDIO_FUNC_1_RESOLUTION_RX          16
#define CFG_TUD_AUDIO_FUNC_1_SAMPLE_RATE            48000
#define CFG_TUD_AUDIO_FUNC_1_EP_OUT_SZ_FS           TUD_AUDIO_EP_SIZE(false, CFG_TUD_AUDIO_FUNC_1_SAMPLE_RATE, 2, 2)
#define CFG_TUD_AUDIO_FUNC_1_EP_OUT_SZ_MAX          CFG_TUD_AUDIO_FUNC_1_EP_OUT_SZ_FS
// ~20 ms of audio: the encoder takes whole radio frames (6.1 ms) at a time, feedback keeps it half full
#define CFG_TUD_AUDIO_FUNC_1_EP_OUT_SW_BUF_SZ       (20 * CFG_TUD_AUDIO_FUNC_1_EP_OUT_SZ_FS)
#define CFG_TUD_AUDIO_ENABLE_EP_OUT                 1
#define CFG_TUD_AUDIO_ENABLE_FEEDBACK_EP            1

#define CFG_TUD_CDC_RX_BUFSIZE      2048
#define CFG_TUD_CDC_TX_BUFSIZE      512
#define CFG_TUD_CDC_EP_BUFSIZE      64
