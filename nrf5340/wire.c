#include "wire.h"
#include <string.h>
const uint8_t mm_address[4] = {0x12, 0x34, 0x56, 0x78};
const uint8_t mm_hop[16] = {24, 76, 18, 44, 66, 36, 16, 78, 68, 20, 62, 22, 38, 48, 42, 28};
const uint8_t mm_pair_channels[4] = {10, 46, 28, 64};
void mm_payload(uint8_t out[32], unsigned slot, uint8_t counter, const uint8_t *codec) {
    static const uint8_t type[] = {0x18, 0x19, 0x1A}, low[] = {1, 0, 1};
    out[0] = type[slot]; out[1] = (uint8_t)((counter << 2) | low[slot]);
    if (codec) memcpy(out + 2, codec, 28); else memset(out + 2, 0, 28);
    out[30] = slot < 2 ? 0x20 : 0; out[31] = 0;
}
void mm_reply(uint8_t out[32], const uint8_t b[32]) {
    memset(out, 0, 32);
    out[0] = (uint8_t)(((b[0] & 0xF0) + 0x20) | 0x10);
    out[1] = 0x35; out[4] = 0x93; out[5] = 0x14;
    memcpy(out + 6, b + 12, 10); memcpy(out + 16, mm_address, 4);
    out[24] = 0x04; out[25] = 0x08; out[26] = 0x05;
}
static uint8_t reverse8(uint8_t x) {
    x = (uint8_t)((x >> 4) | (x << 4));
    x = (uint8_t)(((x & 0xCC) >> 2) | ((x & 0x33) << 2));
    return (uint8_t)(((x & 0xAA) >> 1) | ((x & 0x55) << 1));
}
/* Input is nRF24 TX_ADDR SPI order: least-significant byte first.
 * On air the final input byte goes first, and a[0] is the last (prefix) byte.
 * Native RADIO transmits address bits LSB first, so reverse each byte too.
 * BALEN=3 uses the upper 24 bits of BASE0, transmitting bits 8..15 first,
 * then 16..23, then 24..31, then PREFIX. See Nordic's address-byte-order
 * explanation: devzone.nordicsemi.com/f/nordic-q-a/612/nrf24l01-address-byte-order */
uint32_t mm_radio_base(const uint8_t a[4]) {
    return (uint32_t)reverse8(a[1]) << 24 | (uint32_t)reverse8(a[2]) << 16 |
           (uint32_t)reverse8(a[3]) << 8;
}
uint8_t mm_radio_prefix(const uint8_t a[4]) { return reverse8(a[0]); }
void mm_pair_target(uint8_t out[4], const uint8_t id[4], uint32_t order, uint32_t attempt) {
    if (order != UINT32_MAX) {
        for (unsigned i = 0; i < 4; i++) out[i] = id[order == 1 ? 3 - i : i];
        return;
    }
    /* Debugger-only bounded address-mapping test: 24 byte permutations,
     * followed by the same set with reversed bits within each ID byte.
     * Payload/session/advertised transmitter address remain unchanged. */
    unsigned candidate = attempt % 48, permutation = candidate % 24;
    uint8_t pool[] = {0, 1, 2, 3};
    const uint8_t factorial[] = {6, 2, 1, 1};
    for (unsigned i = 0; i < 4; i++) {
        unsigned at = permutation / factorial[i];
        permutation %= factorial[i];
        uint8_t value = id[pool[at]];
        out[i] = candidate >= 24 ? reverse8(value) : value;
        for (unsigned j = at; j + 1 < 4 - i; j++) pool[j] = pool[j + 1];
    }
}
