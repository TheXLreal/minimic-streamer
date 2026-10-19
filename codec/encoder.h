// encoder.h - encoder for the microphone codec (format described in CODEC.md).
// Plain C, no Pico dependencies, so it also builds on the PC for testing.
//
// Audio in: mono float samples at 48 kHz (full scale = 1.0).
// Out: frames of two 28-byte codec blocks (payload bytes 2..29 of packets 0x18 and 0x19).
#pragma once
#include <stdint.h>

#define ENC_N      64          // MDCT bins per block
#define ENC_BLOCK  28          // bytes per codec block

typedef struct {
    // 48 kHz input history for the linear resampler
    float prev;                // the previous 48 kHz input sample
    uint64_t pos125;           // next output position in input samples, in units of 1/125
    uint64_t nin;              // input samples consumed so far
    // resampled signal (FS = 20833.33 Hz) waiting for the MDCT
    float x[2 * ENC_N];
    int nx;
    // pairing of blocks into frames
    uint8_t pend[ENC_BLOCK];
    int have_pend;
} enc_state_t;

void enc_init(enc_state_t *s);
// one codec block from 64 MDCT coefficients
void enc_block(const float *c, uint8_t out[ENC_BLOCK]);
// MDCT of 128 samples (sine window)
void enc_mdct(const float *x128, float *c64);
// push 48 kHz mono samples; calls emit(frame56) for every finished frame (0x18 block then 0x19 block)
void enc_push(enc_state_t *s, const float *x48, int n, void (*emit)(const uint8_t *frame56, void *ctx), void *ctx);
