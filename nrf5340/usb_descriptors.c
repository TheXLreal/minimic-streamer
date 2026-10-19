#include "tusb.h"
#include "usb_descriptors.h"
#include "nrf.h"
#include "nrfx_config.h"
#include "version.h"
#include <string.h>
/* Development-only identity; obtain your own VID/PID before distributing a product. */
static const tusb_desc_device_t device = {
    .bLength = sizeof(tusb_desc_device_t), .bDescriptorType = TUSB_DESC_DEVICE, .bcdUSB = 0x0200,
    .bDeviceClass = TUSB_CLASS_MISC, .bDeviceSubClass = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol = MISC_PROTOCOL_IAD, .bMaxPacketSize0 = 64,
    .idVendor = 0xCAFE, .idProduct = 0x5340, .bcdDevice = MINIMIC_BCD_DEVICE,
    .iManufacturer = 1, .iProduct = 2, .iSerialNumber = 3, .bNumConfigurations = 1
};
const uint8_t *tud_descriptor_device_cb(void) { return (const uint8_t *)&device; }
#define AUDIO_LEN (TUD_AUDIO10_SPEAKER_STEREO_FB_DESC_LEN(1) - TUD_AUDIO10_DESC_FEATURE_UNIT_LEN(2))
#define TOTAL_LEN (TUD_CONFIG_DESC_LEN + 8 + AUDIO_LEN + TUD_CDC_DESC_LEN)
static const uint8_t config[] = {
    TUD_CONFIG_DESCRIPTOR(1, 4, 0, TOTAL_LEN, 0, 250),
    /* CDC already has an IAD. Windows therefore uses IAD grouping for the whole
     * composite device and will NOT infer the legacy UAC1 collection. Without
     * this audio IAD it creates separate MI_00/MI_01 devices, both with Code 10. */
    8, TUSB_DESC_INTERFACE_ASSOCIATION, 0, 2,
    TUSB_CLASS_AUDIO, AUDIO_SUBCLASS_CONTROL, AUDIO_INT_PROTOCOL_CODE_V1, 4,
    /* No hardware volume controls: connect the USB terminal directly to the
     * speaker terminal. A Feature Unit with all-zero controls makes usbaudio.sys
     * construct a dangling KS topology node and no render endpoint on Windows. */
    TUD_AUDIO10_DESC_STD_AC(0, 0, 4),
    TUD_AUDIO10_DESC_CS_AC(0x0100,
        TUD_AUDIO10_DESC_INPUT_TERM_LEN + TUD_AUDIO10_DESC_OUTPUT_TERM_LEN, 1),
    TUD_AUDIO10_DESC_INPUT_TERM(1, AUDIO_TERM_TYPE_USB_STREAMING, 0, 2,
        AUDIO10_CHANNEL_CONFIG_LEFT_FRONT | AUDIO10_CHANNEL_CONFIG_RIGHT_FRONT, 0, 0),
    TUD_AUDIO10_DESC_OUTPUT_TERM(3, AUDIO_TERM_TYPE_OUT_DESKTOP_SPEAKER, 0, 1, 0),
    TUD_AUDIO10_DESC_STD_AS_INT(1, 0, 0, 0),
    TUD_AUDIO10_DESC_STD_AS_INT(1, 1, 2, 0),
    TUD_AUDIO10_DESC_CS_AS_INT(1, 0, AUDIO10_DATA_FORMAT_TYPE_I_PCM),
    TUD_AUDIO10_DESC_TYPE_I_FORMAT(2, 2, 16, 48000),
    /* nRF USBD has a single ISO IN/OUT pair, fixed to endpoint 8. */
    TUD_AUDIO10_DESC_STD_AS_ISO_EP(0x08, TUSB_XFER_ISOCHRONOUS | TUSB_ISO_EP_ATT_ASYNCHRONOUS,
        CFG_TUD_AUDIO_FUNC_1_EP_OUT_SZ_FS, 1, 0x88),
    TUD_AUDIO10_DESC_CS_AS_ISO_EP(AUDIO10_CS_AS_ISO_DATA_EP_ATT_SAMPLING_FRQ, 0, 0),
    TUD_AUDIO10_DESC_STD_AS_ISO_SYNC_EP(0x88, 0),
    TUD_CDC_DESCRIPTOR(2, 5, 0x81, 8, 0x02, 0x82, 64)
};
_Static_assert(sizeof config == TOTAL_LEN, "USB descriptor size");
const uint8_t *tud_descriptor_configuration_cb(uint8_t index) { (void)index; return config; }
const uint16_t *tud_descriptor_string_cb(uint8_t index, uint16_t lang) {
    (void)lang;
    static uint16_t out[40];
    static const char *const strings[] = {"", "MiniMic DIY", "MiniMic Streamer nRF5340",
        "", "MiniMic Audio", "MiniMic Control"};
    unsigned n = 0;
    if (index == 0) { out[1] = 0x0409; n = 1; }
    else if (index == 3) {
        static const char hex[] = "0123456789ABCDEF";
        for (unsigned word = 0; word < 2; word++)
            for (unsigned i = 0; i < 8; i++)
                out[1 + n++] = hex[(NRF_FICR->INFO.DEVICEID[word] >> (28 - 4 * i)) & 15];
    } else {
        if (index >= sizeof strings / sizeof strings[0]) return NULL;
        n = strlen(strings[index]);
        for (unsigned i = 0; i < n; i++) out[1 + i] = (uint8_t)strings[index][i];
    }
    out[0] = (uint16_t)(TUSB_DESC_STRING << 8 | (2 * n + 2));
    return out;
}
