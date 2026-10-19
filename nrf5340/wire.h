#pragma once
#include <stdint.h>
#define MM_FRAME_US 6144u
#define MM_SLOT_US 1536u
extern const uint8_t mm_address[4], mm_hop[16], mm_pair_channels[4];
void mm_payload(uint8_t out[32], unsigned slot, uint8_t counter, const uint8_t *codec);
void mm_reply(uint8_t out[32], const uint8_t beacon[32]);
uint32_t mm_radio_base(const uint8_t address[4]);
uint8_t mm_radio_prefix(const uint8_t address[4]);
void mm_pair_target(uint8_t out[4], const uint8_t id[4], uint32_t order, uint32_t attempt);
