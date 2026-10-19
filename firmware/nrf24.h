// nrf24.h - minimal nRF24L01+ driver (single module on SPI0). Low-level only: SPI, register and payload
// access, and the two radio setups this project uses (transmit, and receive for pairing/monitor).
#pragma once
#include <stdint.h>

// wiring (same as the sniffer): CE=GP2 CSN=GP3 MISO=GP4 SCK=GP6 MOSI=GP7 on SPI0
#define NRF_PIN_CE   2
#define NRF_PIN_CSN  3
#define NRF_PIN_MISO 4
#define NRF_PIN_SCK  6
#define NRF_PIN_MOSI 7

// SPI commands
#define NRF_W_REGISTER  0x20
#define NRF_R_REGISTER  0x00
#define NRF_W_TX_PAYLOAD 0xA0
#define NRF_R_RX_PAYLOAD 0x61
#define NRF_FLUSH_TX    0xE1
#define NRF_FLUSH_RX    0xE2
#define NRF_NOP         0xFF
// registers
#define NRF_REG_CONFIG   0x00
#define NRF_REG_RF_CH    0x05
#define NRF_REG_RF_SETUP 0x06
#define NRF_REG_STATUS   0x07
#define NRF_REG_TX_ADDR  0x10

void    nrf_bus_init(void);                         // configure SPI0 + CE/CSN pins
void    nrf_ce(int level);
void    nrf_cs(int level);
uint8_t nrf_wreg(uint8_t reg, const uint8_t *v, uint8_t n);   // write register, returns STATUS
uint8_t nrf_wreg1(uint8_t reg, uint8_t v);
uint8_t nrf_rreg1(uint8_t reg);                              // read a 1-byte register
uint8_t nrf_cmd(uint8_t c);                         // one-byte command, returns STATUS
void    nrf_write_payload(const uint8_t p[32]);     // W_TX_PAYLOAD + 32 bytes
void    nrf_read_payload(uint8_t p[32]);            // R_RX_PAYLOAD + 32 bytes

void    nrf_setup_tx(const uint8_t addr[4]);        // transmit config (CONFIG=0x7E, 2 Mbps, this address)
void    nrf_setup_rx(uint8_t channel, const uint8_t addr[4]);  // receive on a channel (pairing/monitor)
