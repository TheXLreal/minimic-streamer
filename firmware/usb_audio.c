// usb_audio.c - see usb_audio.h. The card has no hardware volume (the PC scales the audio itself), so
// the UAC requests are answered but the gain stays at unity. Audio is 48 kHz stereo; the encoder sums
// to mono, resamples to ~20.83 kHz and emits 56-byte frames (two codec blocks); we add the 28-byte
// parity block and push the 84-byte frame to the radio. Raw CDC frames, when present, take over.
#include "usb_audio.h"
#include "encoder.h"
#include "usb_descriptors.h"
#include "radio.h"
#include "cdc.h"
#include <string.h>
#include "tusb.h"

static uint8_t mute[3];
static int16_t volume[3];                        // whole dB per channel (0 = master)
static uint32_t current_sample_rate = 48000;
static volatile bool streaming = false;
static float gain = 1.0f;
static enc_state_t enc;
#define AUDIO_TARGET_DEPTH 4                     // frames queued for the radio (~25 ms)

void usb_audio_init(void) { enc_init(&enc); gain = 1.0f; }
bool usb_audio_streaming(void) { return streaming; }
unsigned usb_audio_fifo(void) { return (unsigned)tud_audio_available(); }

static void emit(const uint8_t *f56, void *ctx) {
    (void)ctx;
    if (cdc_raw_active()) return;
    uint8_t fr[84];
    memcpy(fr, f56, 56);
    for (int i = 0; i < 28; i++) fr[56 + i] = f56[i] ^ f56[28 + i];   // 0x1A parity = 0x18 XOR 0x19
    radio_push_frame(fr);
}

void usb_audio_task(void) {
    static int16_t pcm[96 * 2]; static float x[96];
    const uint16_t chunk = 48 * 4;               // 1 ms stereo 16-bit
    if (cdc_raw_active()) {                       // keep the FIFO moving, ignore the audio
        while (tud_audio_available() >= chunk) tud_audio_read(pcm, chunk);
        return;
    }
    while (radio_depth() < AUDIO_TARGET_DEPTH && tud_audio_available() >= chunk) {
        uint16_t n = tud_audio_read(pcm, chunk) / 4;
        for (int i = 0; i < n; i++) x[i] = gain * (pcm[2 * i] + pcm[2 * i + 1]) * (0.5f / 32768.0f);
        enc_push(&enc, x, n, emit, NULL);
    }
}

// ---------------------------------------------------------------- UAC1 requests (from the TinyUSB example)
bool tud_audio_set_req_ep_cb(uint8_t rhport, tusb_control_request_t const *p, uint8_t *buf) {
    (void)rhport;
    if (TU_U16_HIGH(p->wValue) == AUDIO10_EP_CTRL_SAMPLING_FREQ && p->bRequest == AUDIO10_CS_REQ_SET_CUR) {
        TU_VERIFY(p->wLength == 3); current_sample_rate = tu_unaligned_read32(buf) & 0x00FFFFFF; return true; }
    return false;
}
bool tud_audio_get_req_ep_cb(uint8_t rhport, tusb_control_request_t const *p) {
    if (TU_U16_HIGH(p->wValue) == AUDIO10_EP_CTRL_SAMPLING_FREQ && p->bRequest == AUDIO10_CS_REQ_GET_CUR) {
        uint8_t f[3] = {(uint8_t)current_sample_rate, (uint8_t)(current_sample_rate >> 8), (uint8_t)(current_sample_rate >> 16)};
        return tud_audio_buffer_and_schedule_control_xfer(rhport, p, f, 3); }
    return false;
}
bool tud_audio_set_req_entity_cb(uint8_t rhport, tusb_control_request_t const *p, uint8_t *buf) {
    (void)rhport;
    uint8_t ch = TU_U16_LOW(p->wValue), sel = TU_U16_HIGH(p->wValue), ent = TU_U16_HIGH(p->wIndex);
    if (ent != UAC1_ENTITY_FEATURE_UNIT || ch > 2 || p->bRequest != AUDIO10_CS_REQ_SET_CUR) return false;
    if (sel == AUDIO10_FU_CTRL_MUTE) { TU_VERIFY(p->wLength == 1); mute[ch] = buf[0]; return true; }
    if (sel == AUDIO10_FU_CTRL_VOLUME) { TU_VERIFY(p->wLength == 2); volume[ch] = (int16_t)tu_unaligned_read16(buf) / 256; return true; }
    return false;
}
bool tud_audio_get_req_entity_cb(uint8_t rhport, tusb_control_request_t const *p) {
    uint8_t ch = TU_U16_LOW(p->wValue), sel = TU_U16_HIGH(p->wValue), ent = TU_U16_HIGH(p->wIndex);
    if (ent != UAC1_ENTITY_FEATURE_UNIT || ch > 2) return false;
    if (sel == AUDIO10_FU_CTRL_MUTE) return tud_audio_buffer_and_schedule_control_xfer(rhport, p, &mute[ch], 1);
    if (sel == AUDIO10_FU_CTRL_VOLUME) {
        int16_t v;
        switch (p->bRequest) {
            case AUDIO10_CS_REQ_GET_CUR: v = (int16_t)(volume[ch] * 256); break;
            case AUDIO10_CS_REQ_GET_MIN: v = -60 * 256; break;
            case AUDIO10_CS_REQ_GET_MAX: v = 0; break;
            case AUDIO10_CS_REQ_GET_RES: v = 256; break;
            default: return false;
        }
        return tud_audio_buffer_and_schedule_control_xfer(rhport, p, &v, 2);
    }
    return false;
}
bool tud_audio_set_itf_cb(uint8_t rhport, tusb_control_request_t const *p) {
    (void)rhport;
    if (tu_u16_low(p->wIndex) == ITF_NUM_AUDIO_STREAMING) streaming = tu_u16_low(p->wValue) != 0;
    return true;
}
bool tud_audio_set_itf_close_ep_cb(uint8_t rhport, tusb_control_request_t const *p) {
    (void)rhport;
    if (tu_u16_low(p->wIndex) == ITF_NUM_AUDIO_STREAMING && tu_u16_low(p->wValue) == 0) streaming = false;
    return true;
}
void tud_audio_feedback_params_cb(uint8_t func_id, uint8_t alt_itf, audio_feedback_params_t *fb) {
    (void)func_id; (void)alt_itf;
    fb->method = AUDIO_FEEDBACK_METHOD_FIFO_COUNT;   // keep the audio FIFO half full
    fb->sample_freq = current_sample_rate;
}
