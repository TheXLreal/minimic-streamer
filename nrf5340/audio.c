/* UAC1 control and USB PCM path, adapted from firmware/usb_audio.c. */
#include "audio.h"
#include "ipc.h"
#include "encoder.h"
#include "tusb.h"
#include "usb_descriptors.h"
#include <string.h>

static enc_state_t encoder;
static bool streaming, reset_pending;
static uint32_t pcm_peak, encoded_frames;
void audio_init(void) { enc_init(&encoder); }
void audio_reset(void) { reset_pending = true; IPC->epoch++; __DMB(); }
void audio_stop(void) { streaming = false; audio_reset(); }
bool audio_streaming(void) { return streaming; }
uint32_t audio_fifo(void) { return tud_audio_available(); }
uint32_t audio_pcm_peak(void) { return pcm_peak; }
uint32_t audio_encoded_frames(void) { return encoded_frames; }
static void emit(const uint8_t *f, void *ctx) {
    (void)ctx; uint8_t frame[84]; memcpy(frame, f, 56);
    for (unsigned i = 0; i < 28; i++) frame[56 + i] = f[i] ^ f[28 + i];
    encoded_frames++;
    app_push_frame(frame);
}
void audio_task(void) {
    int16_t pcm[96]; float mono[48];
    bool consume = streaming && IPC->usb_ready && IPC->enabled && !app_raw_active() &&
        IPC->active && IPC->epoch_ack == IPC->epoch;
    if (reset_pending || !consume) {
        if (reset_pending) { enc_init(&encoder); reset_pending = false; pcm_peak = 0; }
        uint16_t available;
        while ((available = tud_audio_available()) != 0)
            tud_audio_read(pcm, available < sizeof pcm ? available : sizeof pcm);
        return;
    }
    while (queue_depth() < 4 && tud_audio_available() >= sizeof pcm) {
        unsigned n = tud_audio_read(pcm, sizeof pcm) / 4;
        uint32_t peak = 0;
        for (unsigned i = 0; i < 2 * n; i++) {
            int sample = pcm[i]; uint32_t magnitude = sample < 0 ? (uint32_t)-sample : (uint32_t)sample;
            if (magnitude > peak) peak = magnitude;
        }
        pcm_peak = peak;
        for (unsigned i = 0; i < n; i++) mono[i] = IPC->muted ? 0.0f :
            (pcm[2 * i] + pcm[2 * i + 1]) * (0.5f / 32768.0f);
        enc_push(&encoder, mono, (int)n, emit, NULL);
    }
}
bool tud_audio_set_req_ep_cb(uint8_t port, const tusb_control_request_t *p, uint8_t *buf) {
    (void)port;
    if (TU_U16_HIGH(p->wValue) != AUDIO10_EP_CTRL_SAMPLING_FREQ ||
        p->bRequest != AUDIO10_CS_REQ_SET_CUR || p->wLength != 3) return false;
    uint32_t rate = buf[0] | (uint32_t)buf[1] << 8 | (uint32_t)buf[2] << 16;
    return rate == 48000;
}
bool tud_audio_get_req_ep_cb(uint8_t port, const tusb_control_request_t *p) {
    if (TU_U16_HIGH(p->wValue) != AUDIO10_EP_CTRL_SAMPLING_FREQ || p->bRequest != AUDIO10_CS_REQ_GET_CUR)
        return false;
    uint8_t rate[3] = {0x80, 0xBB, 0};
    return tud_audio_buffer_and_schedule_control_xfer(port, p, rate, sizeof rate);
}
bool tud_audio_set_itf_cb(uint8_t port, const tusb_control_request_t *p) {
    (void)port;
    if (TU_U16_LOW(p->wIndex) == ITF_NUM_AUDIO_STREAMING) {
        streaming = TU_U16_LOW(p->wValue) != 0; audio_reset();
    }
    return true;
}
bool tud_audio_set_itf_close_ep_cb(uint8_t port, const tusb_control_request_t *p) {
    return tud_audio_set_itf_cb(port, p);
}
void tud_audio_feedback_params_cb(uint8_t func, uint8_t alt, audio_feedback_params_t *fb) {
    (void)func; (void)alt;
    fb->method = AUDIO_FEEDBACK_METHOD_FIFO_COUNT; fb->sample_freq = 48000;
}
