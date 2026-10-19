// nrf24.c - see nrf24.h. Settings match the microphone's radio: 2 Mbps, 4-byte address, 2-byte CRC,
// auto-ack off, fixed 32-byte payloads, CONFIG byte 0x7E written each time we switch to transmit.
#include "nrf24.h"
#include "hardware/spi.h"
#include "hardware/gpio.h"
#include "pico/stdlib.h"

#define NRF_SPI    spi0
#define NRF_SPI_HZ (8 * 1000 * 1000)

void nrf_ce(int level) { gpio_put(NRF_PIN_CE, level); }
void nrf_cs(int level) { gpio_put(NRF_PIN_CSN, level); }

uint8_t nrf_wreg(uint8_t reg, const uint8_t *v, uint8_t n) {
    uint8_t h = NRF_W_REGISTER | reg, s;
    nrf_cs(0);
    spi_write_read_blocking(NRF_SPI, &h, &s, 1);
    if (n) spi_write_blocking(NRF_SPI, v, n);
    nrf_cs(1);
    return s;
}
uint8_t nrf_wreg1(uint8_t reg, uint8_t v) { return nrf_wreg(reg, &v, 1); }

uint8_t nrf_rreg1(uint8_t reg) {
    uint8_t h[2] = {(uint8_t)(NRF_R_REGISTER | reg), NRF_NOP}, x[2];
    nrf_cs(0);
    spi_write_read_blocking(NRF_SPI, h, x, 2);
    nrf_cs(1);
    return x[1];
}

uint8_t nrf_cmd(uint8_t c) {
    uint8_t s;
    nrf_cs(0);
    spi_write_read_blocking(NRF_SPI, &c, &s, 1);
    nrf_cs(1);
    return s;
}

void nrf_write_payload(const uint8_t p[32]) {
    uint8_t h = NRF_W_TX_PAYLOAD;
    nrf_cs(0);
    spi_write_blocking(NRF_SPI, &h, 1);
    spi_write_blocking(NRF_SPI, p, 32);
    nrf_cs(1);
}

void nrf_read_payload(uint8_t p[32]) {
    uint8_t h = NRF_R_RX_PAYLOAD;
    nrf_cs(0);
    spi_write_blocking(NRF_SPI, &h, 1);
    spi_read_blocking(NRF_SPI, 0xFF, p, 32);
    nrf_cs(1);
}

void nrf_bus_init(void) {
    spi_init(NRF_SPI, NRF_SPI_HZ);
    spi_set_format(NRF_SPI, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
    gpio_set_function(NRF_PIN_SCK, GPIO_FUNC_SPI);
    gpio_set_function(NRF_PIN_MOSI, GPIO_FUNC_SPI);
    gpio_set_function(NRF_PIN_MISO, GPIO_FUNC_SPI);
    gpio_init(NRF_PIN_CSN); gpio_set_dir(NRF_PIN_CSN, GPIO_OUT); gpio_put(NRF_PIN_CSN, 1);
    gpio_init(NRF_PIN_CE);  gpio_set_dir(NRF_PIN_CE, GPIO_OUT);  gpio_put(NRF_PIN_CE, 0);
}

void nrf_setup_tx(const uint8_t addr[4]) {
    nrf_ce(0); nrf_cs(1); sleep_ms(6);
    nrf_wreg1(NRF_REG_CONFIG, 0x7E); sleep_ms(2);
    nrf_wreg1(0x01, 0x00); nrf_wreg1(0x04, 0x00); nrf_wreg1(0x03, 0x02);   // no auto-ack, no retransmit, 4-byte addr
    nrf_wreg1(NRF_REG_RF_SETUP, 0x0B); nrf_wreg(NRF_REG_TX_ADDR, addr, 4); nrf_wreg1(0x02, 0x00);
    nrf_cmd(NRF_FLUSH_TX); nrf_wreg1(NRF_REG_STATUS, 0x70);
}

void nrf_setup_rx(uint8_t channel, const uint8_t addr[4]) {
    nrf_ce(0); nrf_cmd(NRF_FLUSH_RX); nrf_wreg1(NRF_REG_STATUS, 0x70);
    nrf_wreg(0x0A, addr, 4); nrf_wreg1(0x11, 32);             // RX_ADDR_P0, RX_PW_P0
    nrf_wreg1(NRF_REG_CONFIG, 0x3F); nrf_wreg1(0x02, 0x01);    // receive mode, pipe 0 only
    nrf_wreg1(NRF_REG_RF_CH, channel); nrf_ce(1);
}
