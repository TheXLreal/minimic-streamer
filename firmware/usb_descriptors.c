// USB descriptors: UAC1 speaker with feedback EP (from TinyUSB example uac2_speaker_fb), CDC serial,
// and the Raspberry Pi reset interface so 'picotool load -f' / 'picotool reboot -f -u' work without BOOTSEL.
#include <string.h>
#include "tusb.h"
#include "pico/usb_reset_interface.h"
#include "usb_descriptors.h"

static tusb_desc_device_t const desc_device = {
    .bLength = sizeof(tusb_desc_device_t), .bDescriptorType = TUSB_DESC_DEVICE, .bcdUSB = 0x0200,
    .bDeviceClass = TUSB_CLASS_MISC, .bDeviceSubClass = MISC_SUBCLASS_COMMON, .bDeviceProtocol = MISC_PROTOCOL_IAD,
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor = 0x2E8A, .idProduct = 0x0009, .bcdDevice = 0x0200,     // Raspberry Pi VID (picotool finds it)
    .iManufacturer = 1, .iProduct = 2, .iSerialNumber = 3, .bNumConfigurations = 1};

uint8_t const *tud_descriptor_device_cb(void) { return (uint8_t const *)&desc_device; }

#define EPNUM_AUDIO     0x01
#define EPNUM_AUDIO_FB  0x81
#define EPNUM_CDC_NOTIF 0x82
#define EPNUM_CDC_OUT   0x03
#define EPNUM_CDC_IN    0x83

#define TUD_RESET_ITF_DESC_LEN 9
#define CONFIG_TOTAL_LEN (TUD_CONFIG_DESC_LEN + TUD_AUDIO10_SPEAKER_STEREO_FB_DESC_LEN(1) + TUD_CDC_DESC_LEN + TUD_RESET_ITF_DESC_LEN)

static uint8_t const desc_configuration[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, 0x00, 250),
    TUD_AUDIO10_SPEAKER_STEREO_FB_DESCRIPTOR(ITF_NUM_AUDIO_CONTROL, 4, 2, 16, EPNUM_AUDIO,
                                             CFG_TUD_AUDIO_FUNC_1_EP_OUT_SZ_FS, EPNUM_AUDIO_FB, 48000),
    TUD_CDC_DESCRIPTOR(ITF_NUM_CDC, 5, EPNUM_CDC_NOTIF, 8, EPNUM_CDC_OUT, EPNUM_CDC_IN, 64),
    // reset interface: vendor class, no endpoints
    9, TUSB_DESC_INTERFACE, ITF_NUM_RESET, 0, 0, TUSB_CLASS_VENDOR_SPECIFIC, RESET_INTERFACE_SUBCLASS,
    RESET_INTERFACE_PROTOCOL, 6,
};
TU_VERIFY_STATIC(sizeof(desc_configuration) == CONFIG_TOTAL_LEN, "Incorrect size");

uint8_t const *tud_descriptor_configuration_cb(uint8_t index) { (void)index; return desc_configuration; }

// index 3 is a fixed, neutral serial string so the device never exposes the Pico's real unique board id.
static char const *string_desc_arr[] = {
    (const char[]){0x09, 0x04}, "minimic", "MiniMic Streamer", "000000000000", "MiniMic Streamer", "MiniMic Serial", "Reset",
};
static uint16_t _desc_str[33];

uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
    (void)langid;
    size_t n;
    if (index == 0) { memcpy(&_desc_str[1], string_desc_arr[0], 2); n = 1; }
    else {
        if (index >= sizeof(string_desc_arr) / sizeof(string_desc_arr[0])) return NULL;
        const char *s = string_desc_arr[index];
        n = strlen(s); if (n > 32) n = 32;
        for (size_t i = 0; i < n; i++) _desc_str[1 + i] = (uint8_t)s[i];
    }
    _desc_str[0] = (uint16_t)((TUSB_DESC_STRING << 8) | (2 * n + 2));
    return _desc_str;
}
