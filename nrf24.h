/**
 * nrf24.h - nRF24L01(+) driver for STM32 HAL
 *
 * Target : STM32F4xx (tested on STM32F405RGT6)
 * SPI    : max SCK 10 MHz, mode 0 (CPOL=0, CPHA=0), 8-bit, MSB first
 * Notes  : RPD (register 0x09) only meaningful on nRF24L01+.
 *
 * Usage:
 *   nrf24_t dev;
 *   nrf24_init(&dev, &hspi1, CE_GPIO_Port, CE_Pin, CSN_GPIO_Port, CSN_Pin);
 *   nrf24_set_channel(&dev, 76);
 *   nrf24_set_data_rate(&dev, NRF24_DR_1MBPS);
 *   nrf24_set_power(&dev, NRF24_PWR_0DBM);
 *   ... configure addresses / pipes ...
 *   nrf24_rx_mode(&dev);   // or nrf24_tx_mode(&dev);
 */

#ifndef NRF24_H
#define NRF24_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32f4xx_hal.h"   /* change to your family header if not F4 */
#include <stdint.h>
#include <stdbool.h>

/* ==========================================================================
 * SPI commands
 * ========================================================================== */
#define NRF24_CMD_R_REGISTER        0x00  /* | reg (0..0x1D) */
#define NRF24_CMD_W_REGISTER        0x20  /* | reg (0..0x1D) */
#define NRF24_CMD_R_RX_PAYLOAD      0x61
#define NRF24_CMD_W_TX_PAYLOAD      0xA0
#define NRF24_CMD_FLUSH_TX          0xE1
#define NRF24_CMD_FLUSH_RX          0xE2
#define NRF24_CMD_REUSE_TX_PL       0xE3
#define NRF24_CMD_R_RX_PL_WID       0x60
#define NRF24_CMD_W_ACK_PAYLOAD     0xA8  /* | pipe (0..5) */
#define NRF24_CMD_W_TX_PAYLOAD_NOACK 0xB0
#define NRF24_CMD_NOP               0xFF

/* ==========================================================================
 * Register map
 * ========================================================================== */
#define NRF24_REG_CONFIG            0x00
#define NRF24_REG_EN_AA             0x01
#define NRF24_REG_EN_RXADDR         0x02
#define NRF24_REG_SETUP_AW          0x03
#define NRF24_REG_SETUP_RETR        0x04
#define NRF24_REG_RF_CH             0x05
#define NRF24_REG_RF_SETUP          0x06
#define NRF24_REG_STATUS            0x07
#define NRF24_REG_OBSERVE_TX        0x08
#define NRF24_REG_RPD               0x09  /* CD on non-plus part */
#define NRF24_REG_RX_ADDR_P0        0x0A
#define NRF24_REG_RX_ADDR_P1        0x0B
#define NRF24_REG_RX_ADDR_P2        0x0C
#define NRF24_REG_RX_ADDR_P3        0x0D
#define NRF24_REG_RX_ADDR_P4        0x0E
#define NRF24_REG_RX_ADDR_P5        0x0F
#define NRF24_REG_TX_ADDR           0x10
#define NRF24_REG_RX_PW_P0          0x11
#define NRF24_REG_RX_PW_P1          0x12
#define NRF24_REG_RX_PW_P2          0x13
#define NRF24_REG_RX_PW_P3          0x14
#define NRF24_REG_RX_PW_P4          0x15
#define NRF24_REG_RX_PW_P5          0x16
#define NRF24_REG_FIFO_STATUS       0x17
#define NRF24_REG_DYNPD             0x1C
#define NRF24_REG_FEATURE           0x1D

/* ==========================================================================
 * Bit positions
 * ========================================================================== */
/* CONFIG */
#define NRF24_BIT_MASK_RX_DR        6
#define NRF24_BIT_MASK_TX_DS        5
#define NRF24_BIT_MASK_MAX_RT       4
#define NRF24_BIT_EN_CRC            3
#define NRF24_BIT_CRCO              2
#define NRF24_BIT_PWR_UP            1
#define NRF24_BIT_PRIM_RX           0

/* RF_SETUP */
#define NRF24_BIT_CONT_WAVE         7
#define NRF24_BIT_RF_DR_LOW         5
#define NRF24_BIT_PLL_LOCK          4
#define NRF24_BIT_RF_DR_HIGH        3

/* STATUS */
#define NRF24_BIT_RX_DR             6
#define NRF24_BIT_TX_DS             5
#define NRF24_BIT_MAX_RT            4
#define NRF24_BIT_TX_FULL           0
#define NRF24_STATUS_RX_P_NO_MASK   0x0E   /* bits 3:1 */
#define NRF24_RX_P_NO_EMPTY         0x07

/* FIFO_STATUS */
#define NRF24_BIT_TX_REUSE          6
#define NRF24_BIT_FIFO_TX_FULL      5
#define NRF24_BIT_TX_EMPTY          4
#define NRF24_BIT_RX_FULL           1
#define NRF24_BIT_RX_EMPTY          0

/* FEATURE */
#define NRF24_BIT_EN_DPL            2
#define NRF24_BIT_EN_ACK_PAY        1
#define NRF24_BIT_EN_DYN_ACK        0

/* ==========================================================================
 * Enums
 * ========================================================================== */
typedef enum {
    NRF24_DR_1MBPS   = 0,
    NRF24_DR_2MBPS   = 1,
    NRF24_DR_250KBPS = 2
} nrf24_datarate_t;

typedef enum {
    NRF24_PWR_M18DBM = 0,  /* -18 dBm */
    NRF24_PWR_M12DBM = 1,  /* -12 dBm */
    NRF24_PWR_M6DBM  = 2,  /*  -6 dBm */
    NRF24_PWR_0DBM   = 3   /*   0 dBm */
} nrf24_power_t;

typedef enum {
    NRF24_CRC_DISABLE = 0,
    NRF24_CRC_1BYTE   = 1,
    NRF24_CRC_2BYTE   = 2
} nrf24_crc_t;

typedef enum {
    NRF24_AW_3BYTES = 1,   /* SETUP_AW encoding */
    NRF24_AW_4BYTES = 2,
    NRF24_AW_5BYTES = 3
} nrf24_addr_width_t;

/* ==========================================================================
 * Device handle
 * ========================================================================== */
typedef struct {
    SPI_HandleTypeDef *hspi;
    GPIO_TypeDef *ce_port;
    uint16_t      ce_pin;
    GPIO_TypeDef *csn_port;
    uint16_t      csn_pin;
    uint8_t       addr_width;   /* actual byte count 3..5, cached */
    uint8_t       payload_size; /* fixed payload size, cached */
} nrf24_t;

/* ==========================================================================
 * API
 * ========================================================================== */

/* Init: sets a sane default config (2-byte CRC, 5-byte addr, 32B payload,
 * auto-ack off, 1 Mbps, 0 dBm, channel 76). Returns true if the module
 * responds (config readback matches what was written). */
bool nrf24_init(nrf24_t *dev, SPI_HandleTypeDef *hspi,
                GPIO_TypeDef *ce_port, uint16_t ce_pin,
                GPIO_TypeDef *csn_port, uint16_t csn_pin);

/* Low-level register access */
uint8_t nrf24_read_reg(nrf24_t *dev, uint8_t reg);
void    nrf24_write_reg(nrf24_t *dev, uint8_t reg, uint8_t val);
void    nrf24_read_reg_buf(nrf24_t *dev, uint8_t reg, uint8_t *buf, uint8_t len);
void    nrf24_write_reg_buf(nrf24_t *dev, uint8_t reg, const uint8_t *buf, uint8_t len);
uint8_t nrf24_send_cmd(nrf24_t *dev, uint8_t cmd);

/* Configuration */
void nrf24_set_channel(nrf24_t *dev, uint8_t ch);        /* 0..125 */
void nrf24_set_data_rate(nrf24_t *dev, nrf24_datarate_t dr);
void nrf24_set_power(nrf24_t *dev, nrf24_power_t pwr);
void nrf24_set_crc(nrf24_t *dev, nrf24_crc_t crc);
void nrf24_set_address_width(nrf24_t *dev, nrf24_addr_width_t aw);
void nrf24_set_retries(nrf24_t *dev, uint8_t delay, uint8_t count); /* delay 0..15 (x250us), count 0..15 */
void nrf24_set_payload_size(nrf24_t *dev, uint8_t pipe, uint8_t size); /* 0..32 */

void nrf24_set_tx_address(nrf24_t *dev, const uint8_t *addr);
void nrf24_set_rx_address(nrf24_t *dev, uint8_t pipe, const uint8_t *addr);
void nrf24_enable_pipe(nrf24_t *dev, uint8_t pipe, bool enable);
void nrf24_set_auto_ack(nrf24_t *dev, uint8_t pipe, bool enable);

/* Power / mode control */
void nrf24_power_up(nrf24_t *dev);
void nrf24_power_down(nrf24_t *dev);
void nrf24_rx_mode(nrf24_t *dev);   /* PRIM_RX=1, CE high */
void nrf24_tx_mode(nrf24_t *dev);   /* PRIM_RX=0, CE low  */

/* Data path */
bool nrf24_transmit(nrf24_t *dev, const uint8_t *data, uint8_t len); /* blocking, returns true on TX_DS */
bool nrf24_available(nrf24_t *dev, uint8_t *pipe_out);              /* data waiting in RX FIFO? */
void nrf24_read_payload(nrf24_t *dev, uint8_t *buf, uint8_t len);

/* Non-blocking receive: if a payload is waiting, read `len` bytes into `buf`,
 * report the source pipe (if pipe_out != NULL) and return true. Otherwise
 * returns false immediately. Safe to call from an IRQ handler or main loop.
 * Does NOT clear TX_DS/MAX_RT — call nrf24_clear_irq() afterwards if needed. */
bool nrf24_receive(nrf24_t *dev, uint8_t *buf, uint8_t len, uint8_t *pipe_out);

/* Status / housekeeping */
uint8_t nrf24_get_status(nrf24_t *dev);
uint8_t nrf24_get_fifo_status(nrf24_t *dev);
uint8_t nrf24_get_rpd(nrf24_t *dev);        /* 1 if RF > -64 dBm on current channel */
void    nrf24_flush_tx(nrf24_t *dev);
void    nrf24_flush_rx(nrf24_t *dev);
void    nrf24_clear_irq(nrf24_t *dev);      /* clear RX_DR|TX_DS|MAX_RT */

/* Select which events assert the IRQ pin. `true` = interrupt ENABLED (pin will
 * be driven low on that event); `false` = masked/silent. Internally the CONFIG
 * mask bit is the inverse (mask=1 disables), handled for you here.
 * e.g. RX-only interrupt: nrf24_set_irq_mask(dev, true, false, false); */
void    nrf24_set_irq_mask(nrf24_t *dev, bool rx_dr, bool tx_ds, bool max_rt);

/* Spectrum-monitor helpers -------------------------------------------------
 * Put the device in a bare RX-carrier-detect state and probe one channel.
 * Returns 1 if energy above -64 dBm was seen during the dwell window. */
void    nrf24_scanner_begin(nrf24_t *dev);            /* config for RPD scanning */
uint8_t nrf24_scan_channel(nrf24_t *dev, uint8_t ch); /* returns 0/1 */

/* Constant carrier wave output (for testing / TX presence). Uses CONT_WAVE. */
void nrf24_carrier_start(nrf24_t *dev, uint8_t ch, nrf24_power_t pwr);
void nrf24_carrier_stop(nrf24_t *dev);

/* microsecond delay (DWT-based); exposed in case you need it too */
void nrf24_delay_us(uint32_t us);

#ifdef __cplusplus
}
#endif

#endif /* NRF24_H */
