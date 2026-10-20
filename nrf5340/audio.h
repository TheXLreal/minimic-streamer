#pragma once
#include <stdbool.h>
#include <stdint.h>
void audio_init(void);
void audio_reset(void);
void audio_stop(void);
void audio_task(void);
bool audio_streaming(void);
uint32_t audio_fifo(void);
uint32_t audio_pcm_peak(void);
uint32_t audio_encoded_frames(void);
bool app_raw_active(void);
bool app_push_frame(const uint8_t frame[84]);
