/**
 * nrf24.c - nRF24L01(+) driver for STM32 HAL
 * See nrf24.h for usage.
 */

#include "nrf24.h"
#include <string.h>

/* ==========================================================================
 * DWT-based microsecond delay (self-contained, no TIM needed)
 * ========================================================================== */
static void nrf24_dwt_init(void)
{
    if (!(CoreDebug->DEMCR & CoreDebug_DEMCR_TRCENA_Msk)) {
        CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
        DWT->CYCCNT = 0;
        DWT->CTRL  |= DWT_CTRL_CYCCNTENA_Msk;
    }
}

void nrf24_delay_us(uint32_t us)
{
    uint32_t start  = DWT->CYCCNT;
    uint32_t cycles = us * (SystemCoreClock / 1000000U);
    while ((DWT->CYCCNT - start) < cycles) { /* spin */ }
}

/* ==========================================================================
 * Pin helpers
 * ========================================================================== */
static inline void ce_low(nrf24_t *d)  { HAL_GPIO_WritePin(d->ce_port,  d->ce_pin,  GPIO_PIN_RESET); }
static inline void ce_high(nrf24_t *d) { HAL_GPIO_WritePin(d->ce_port,  d->ce_pin,  GPIO_PIN_SET);   }
static inline void csn_low(nrf24_t *d) { HAL_GPIO_WritePin(d->csn_port, d->csn_pin, GPIO_PIN_RESET); }
static inline void csn_high(nrf24_t *d){ HAL_GPIO_WritePin(d->csn_port, d->csn_pin, GPIO_PIN_SET);   }

/* ==========================================================================
 * Low-level SPI register access
 * ========================================================================== */
uint8_t nrf24_send_cmd(nrf24_t *dev, uint8_t cmd)
{
    uint8_t status;
    csn_low(dev);
    HAL_SPI_TransmitReceive(dev->hspi, &cmd, &status, 1, HAL_MAX_DELAY);
    csn_high(dev);
    return status;
}

uint8_t nrf24_read_reg(nrf24_t *dev, uint8_t reg)
{
    uint8_t tx[2] = { (uint8_t)(NRF24_CMD_R_REGISTER | reg), NRF24_CMD_NOP };
    uint8_t rx[2];
    csn_low(dev);
    HAL_SPI_TransmitReceive(dev->hspi, tx, rx, 2, HAL_MAX_DELAY);
    csn_high(dev);
    return rx[1];
}

void nrf24_write_reg(nrf24_t *dev, uint8_t reg, uint8_t val)
{
    uint8_t tx[2] = { (uint8_t)(NRF24_CMD_W_REGISTER | reg), val };
    csn_low(dev);
    HAL_SPI_Transmit(dev->hspi, tx, 2, HAL_MAX_DELAY);
    csn_high(dev);
}

void nrf24_read_reg_buf(nrf24_t *dev, uint8_t reg, uint8_t *buf, uint8_t len)
{
    uint8_t cmd = (uint8_t)(NRF24_CMD_R_REGISTER | reg);
    csn_low(dev);
    HAL_SPI_Transmit(dev->hspi, &cmd, 1, HAL_MAX_DELAY);
    memset(buf, NRF24_CMD_NOP, len);
    HAL_SPI_TransmitReceive(dev->hspi, buf, buf, len, HAL_MAX_DELAY);
    csn_high(dev);
}

void nrf24_write_reg_buf(nrf24_t *dev, uint8_t reg, const uint8_t *buf, uint8_t len)
{
    uint8_t cmd = (uint8_t)(NRF24_CMD_W_REGISTER | reg);
    csn_low(dev);
    HAL_SPI_Transmit(dev->hspi, &cmd, 1, HAL_MAX_DELAY);
    HAL_SPI_Transmit(dev->hspi, (uint8_t *)buf, len, HAL_MAX_DELAY);
    csn_high(dev);
}

/* Read-modify-write a single bit */
static void nrf24_set_bit(nrf24_t *dev, uint8_t reg, uint8_t bit, bool val)
{
    uint8_t r = nrf24_read_reg(dev, reg);
    if (val) r |=  (uint8_t)(1u << bit);
    else     r &= (uint8_t)~(1u << bit);
    nrf24_write_reg(dev, reg, r);
}

/* ==========================================================================
 * Configuration
 * ========================================================================== */
void nrf24_set_channel(nrf24_t *dev, uint8_t ch)
{
    if (ch > 125) ch = 125;
    nrf24_write_reg(dev, NRF24_REG_RF_CH, ch);
}

void nrf24_set_data_rate(nrf24_t *dev, nrf24_datarate_t dr)
{
    uint8_t rf = nrf24_read_reg(dev, NRF24_REG_RF_SETUP);
    rf &= (uint8_t)~((1u << NRF24_BIT_RF_DR_LOW) | (1u << NRF24_BIT_RF_DR_HIGH));
    if (dr == NRF24_DR_2MBPS)        rf |= (1u << NRF24_BIT_RF_DR_HIGH);
    else if (dr == NRF24_DR_250KBPS) rf |= (1u << NRF24_BIT_RF_DR_LOW);
    /* 1 Mbps = both bits clear */
    nrf24_write_reg(dev, NRF24_REG_RF_SETUP, rf);
}

void nrf24_set_power(nrf24_t *dev, nrf24_power_t pwr)
{
    uint8_t rf = nrf24_read_reg(dev, NRF24_REG_RF_SETUP);
    rf &= (uint8_t)~0x06;                 /* clear bits 2:1 */
    rf |= (uint8_t)((pwr & 0x03) << 1);
    nrf24_write_reg(dev, NRF24_REG_RF_SETUP, rf);
}

void nrf24_set_crc(nrf24_t *dev, nrf24_crc_t crc)
{
    uint8_t cfg = nrf24_read_reg(dev, NRF24_REG_CONFIG);
    if (crc == NRF24_CRC_DISABLE) {
        cfg &= (uint8_t)~(1u << NRF24_BIT_EN_CRC);
    } else {
        cfg |= (1u << NRF24_BIT_EN_CRC);
        if (crc == NRF24_CRC_2BYTE) cfg |=  (1u << NRF24_BIT_CRCO);
        else                        cfg &= (uint8_t)~(1u << NRF24_BIT_CRCO);
    }
    nrf24_write_reg(dev, NRF24_REG_CONFIG, cfg);
}

void nrf24_set_address_width(nrf24_t *dev, nrf24_addr_width_t aw)
{
    nrf24_write_reg(dev, NRF24_REG_SETUP_AW, (uint8_t)aw);
    dev->addr_width = (uint8_t)(aw + 2);  /* enc 1->3, 2->4, 3->5 */
}

void nrf24_set_retries(nrf24_t *dev, uint8_t delay, uint8_t count)
{
    nrf24_write_reg(dev, NRF24_REG_SETUP_RETR,
                    (uint8_t)(((delay & 0x0F) << 4) | (count & 0x0F)));
}

void nrf24_set_payload_size(nrf24_t *dev, uint8_t pipe, uint8_t size)
{
    if (size > 32) size = 32;
    if (pipe > 5)  return;
    nrf24_write_reg(dev, (uint8_t)(NRF24_REG_RX_PW_P0 + pipe), size);
    if (pipe == 0) dev->payload_size = size;
}

void nrf24_set_tx_address(nrf24_t *dev, const uint8_t *addr)
{
    nrf24_write_reg_buf(dev, NRF24_REG_TX_ADDR, addr, dev->addr_width);
}

void nrf24_set_rx_address(nrf24_t *dev, uint8_t pipe, const uint8_t *addr)
{
    if (pipe > 5) return;
    /* pipes 0-1 take the full address width; pipes 2-5 only differ in LSB */
    if (pipe < 2) nrf24_write_reg_buf(dev, (uint8_t)(NRF24_REG_RX_ADDR_P0 + pipe), addr, dev->addr_width);
    else          nrf24_write_reg(dev, (uint8_t)(NRF24_REG_RX_ADDR_P0 + pipe), addr[0]);
}

void nrf24_enable_pipe(nrf24_t *dev, uint8_t pipe, bool enable)
{
    if (pipe > 5) return;
    nrf24_set_bit(dev, NRF24_REG_EN_RXADDR, pipe, enable);
}

void nrf24_set_auto_ack(nrf24_t *dev, uint8_t pipe, bool enable)
{
    if (pipe > 5) return;
    nrf24_set_bit(dev, NRF24_REG_EN_AA, pipe, enable);
}

/* ==========================================================================
 * Power / mode
 * ========================================================================== */
void nrf24_power_up(nrf24_t *dev)
{
    nrf24_set_bit(dev, NRF24_REG_CONFIG, NRF24_BIT_PWR_UP, true);
    HAL_Delay(2);              /* Tpd2stby, max 1.5 ms */
}

void nrf24_power_down(nrf24_t *dev)
{
    ce_low(dev);
    nrf24_set_bit(dev, NRF24_REG_CONFIG, NRF24_BIT_PWR_UP, false);
}

void nrf24_rx_mode(nrf24_t *dev)
{
    nrf24_set_bit(dev, NRF24_REG_CONFIG, NRF24_BIT_PWR_UP, true);
    nrf24_set_bit(dev, NRF24_REG_CONFIG, NRF24_BIT_PRIM_RX, true);
    nrf24_clear_irq(dev);
    nrf24_flush_rx(dev);
    ce_high(dev);
    nrf24_delay_us(150);       /* Tstby2a */
}

void nrf24_tx_mode(nrf24_t *dev)
{
    ce_low(dev);
    nrf24_set_bit(dev, NRF24_REG_CONFIG, NRF24_BIT_PWR_UP, true);
    nrf24_set_bit(dev, NRF24_REG_CONFIG, NRF24_BIT_PRIM_RX, false);
    nrf24_clear_irq(dev);
    nrf24_flush_tx(dev);
    nrf24_delay_us(150);
}

/* ==========================================================================
 * Data path
 * ========================================================================== */
bool nrf24_transmit(nrf24_t *dev, const uint8_t *data, uint8_t len)
{
    if (len > 32) len = 32;

    /* load payload */
    uint8_t cmd = NRF24_CMD_W_TX_PAYLOAD;
    csn_low(dev);
    HAL_SPI_Transmit(dev->hspi, &cmd, 1, HAL_MAX_DELAY);
    HAL_SPI_Transmit(dev->hspi, (uint8_t *)data, len, HAL_MAX_DELAY);
    csn_high(dev);

    /* pulse CE > 10 us to start transmission */
    ce_high(dev);
    nrf24_delay_us(15);
    ce_low(dev);

    /* wait for TX_DS or MAX_RT */
    uint32_t t0 = HAL_GetTick();
    uint8_t status;
    do {
        status = nrf24_get_status(dev);
        if (status & ((1u << NRF24_BIT_TX_DS) | (1u << NRF24_BIT_MAX_RT))) break;
    } while ((HAL_GetTick() - t0) < 100);

    bool ok = (status & (1u << NRF24_BIT_TX_DS)) != 0;
    if (status & (1u << NRF24_BIT_MAX_RT)) nrf24_flush_tx(dev);
    nrf24_clear_irq(dev);
    return ok;
}

bool nrf24_available(nrf24_t *dev, uint8_t *pipe_out)
{
    uint8_t status = nrf24_get_status(dev);
    uint8_t pipe = (status & NRF24_STATUS_RX_P_NO_MASK) >> 1;
    if (pipe == NRF24_RX_P_NO_EMPTY) return false;
    if (pipe_out) *pipe_out = pipe;
    return true;
}

void nrf24_read_payload(nrf24_t *dev, uint8_t *buf, uint8_t len)
{
    if (len > 32) len = 32;
    uint8_t cmd = NRF24_CMD_R_RX_PAYLOAD;
    csn_low(dev);
    HAL_SPI_Transmit(dev->hspi, &cmd, 1, HAL_MAX_DELAY);
    memset(buf, NRF24_CMD_NOP, len);
    HAL_SPI_TransmitReceive(dev->hspi, buf, buf, len, HAL_MAX_DELAY);
    csn_high(dev);
    nrf24_set_bit(dev, NRF24_REG_STATUS, NRF24_BIT_RX_DR, true); /* clear RX_DR (write 1) */
}

bool nrf24_receive(nrf24_t *dev, uint8_t *buf, uint8_t len, uint8_t *pipe_out)
{
    uint8_t pipe;
    if (!nrf24_available(dev, &pipe)) return false;   /* nothing waiting */
    if (pipe_out) *pipe_out = pipe;
    nrf24_read_payload(dev, buf, len);                /* also clears RX_DR */
    return true;
}

/* ==========================================================================
 * Status / housekeeping
 * ========================================================================== */
uint8_t nrf24_get_status(nrf24_t *dev)      { return nrf24_send_cmd(dev, NRF24_CMD_NOP); }
uint8_t nrf24_get_fifo_status(nrf24_t *dev) { return nrf24_read_reg(dev, NRF24_REG_FIFO_STATUS); }
uint8_t nrf24_get_rpd(nrf24_t *dev)         { return (uint8_t)(nrf24_read_reg(dev, NRF24_REG_RPD) & 0x01); }
void    nrf24_flush_tx(nrf24_t *dev)        { nrf24_send_cmd(dev, NRF24_CMD_FLUSH_TX); }
void    nrf24_flush_rx(nrf24_t *dev)        { nrf24_send_cmd(dev, NRF24_CMD_FLUSH_RX); }

void nrf24_clear_irq(nrf24_t *dev)
{
    /* interrupt flags are cleared by writing 1 to STATUS bits */
    nrf24_write_reg(dev, NRF24_REG_STATUS,
                    (1u << NRF24_BIT_RX_DR) |
                    (1u << NRF24_BIT_TX_DS) |
                    (1u << NRF24_BIT_MAX_RT));
}

void nrf24_set_irq_mask(nrf24_t *dev, bool rx_dr, bool tx_ds, bool max_rt)
{
    uint8_t cfg = nrf24_read_reg(dev, NRF24_REG_CONFIG);
    /* CONFIG mask bit = 1 disables the interrupt, so invert the "enabled" args */
    cfg &= (uint8_t)~((1u << NRF24_BIT_MASK_RX_DR) |
                      (1u << NRF24_BIT_MASK_TX_DS) |
                      (1u << NRF24_BIT_MASK_MAX_RT));
    if (!rx_dr)  cfg |= (1u << NRF24_BIT_MASK_RX_DR);
    if (!tx_ds)  cfg |= (1u << NRF24_BIT_MASK_TX_DS);
    if (!max_rt) cfg |= (1u << NRF24_BIT_MASK_MAX_RT);
    nrf24_write_reg(dev, NRF24_REG_CONFIG, cfg);
}

/* ==========================================================================
 * Spectrum-monitor helpers
 * ========================================================================== */
void nrf24_scanner_begin(nrf24_t *dev)
{
    ce_low(dev);
    nrf24_set_auto_ack(dev, 0, false);          /* EN_AA off, all pipes */
    nrf24_write_reg(dev, NRF24_REG_EN_AA, 0x00);
    /* PWR_UP=1, PRIM_RX=1, CRC off so it never rejects garbage */
    nrf24_write_reg(dev, NRF24_REG_CONFIG,
                    (1u << NRF24_BIT_PWR_UP) | (1u << NRF24_BIT_PRIM_RX));
    HAL_Delay(2);
}

uint8_t nrf24_scan_channel(nrf24_t *dev, uint8_t ch)
{
    ce_low(dev);
    nrf24_set_channel(dev, ch);
    ce_high(dev);                 /* enter RX */
    nrf24_delay_us(200);          /* ~130us settle + short dwell */
    ce_low(dev);
    return nrf24_get_rpd(dev);
}

/* ==========================================================================
 * Constant carrier wave (TX)
 * ========================================================================== */
void nrf24_carrier_start(nrf24_t *dev, uint8_t ch, nrf24_power_t pwr)
{
    ce_low(dev);
    nrf24_set_channel(dev, ch);
    nrf24_set_power(dev, pwr);
    /* PWR_UP=1, PRIM_RX=0 */
    nrf24_write_reg(dev, NRF24_REG_CONFIG, (1u << NRF24_BIT_PWR_UP));
    /* set CONT_WAVE + PLL_LOCK in RF_SETUP */
    uint8_t rf = nrf24_read_reg(dev, NRF24_REG_RF_SETUP);
    rf |= (1u << NRF24_BIT_CONT_WAVE) | (1u << NRF24_BIT_PLL_LOCK);
    nrf24_write_reg(dev, NRF24_REG_RF_SETUP, rf);
    HAL_Delay(2);
    ce_high(dev);
}

void nrf24_carrier_stop(nrf24_t *dev)
{
    ce_low(dev);
    uint8_t rf = nrf24_read_reg(dev, NRF24_REG_RF_SETUP);
    rf &= (uint8_t)~((1u << NRF24_BIT_CONT_WAVE) | (1u << NRF24_BIT_PLL_LOCK));
    nrf24_write_reg(dev, NRF24_REG_RF_SETUP, rf);
}

/* ==========================================================================
 * Init
 * ========================================================================== */
bool nrf24_init(nrf24_t *dev, SPI_HandleTypeDef *hspi,
                GPIO_TypeDef *ce_port, uint16_t ce_pin,
                GPIO_TypeDef *csn_port, uint16_t csn_pin)
{
    dev->hspi     = hspi;
    dev->ce_port  = ce_port;
    dev->ce_pin   = ce_pin;
    dev->csn_port = csn_port;
    dev->csn_pin  = csn_pin;
    dev->addr_width   = 5;
    dev->payload_size = 32;

    nrf24_dwt_init();

    csn_high(dev);
    ce_low(dev);
    HAL_Delay(6);                 /* power-on reset settle */

    /* default config: 2-byte CRC, powered down for now */
    nrf24_write_reg(dev, NRF24_REG_CONFIG,
                    (1u << NRF24_BIT_EN_CRC) | (1u << NRF24_BIT_CRCO));
    nrf24_write_reg(dev, NRF24_REG_EN_AA,      0x00);   /* auto-ack off  */
    nrf24_write_reg(dev, NRF24_REG_EN_RXADDR,  0x00);   /* no pipes yet  */
    nrf24_set_address_width(dev, NRF24_AW_5BYTES);
    nrf24_set_retries(dev, 5, 15);                      /* 1500us, 15 retries */
    nrf24_set_channel(dev, 76);
    nrf24_set_data_rate(dev, NRF24_DR_1MBPS);
    nrf24_set_power(dev, NRF24_PWR_0DBM);
    nrf24_write_reg(dev, NRF24_REG_DYNPD,   0x00);
    nrf24_write_reg(dev, NRF24_REG_FEATURE, 0x00);
    nrf24_flush_tx(dev);
    nrf24_flush_rx(dev);
    nrf24_clear_irq(dev);
    nrf24_power_up(dev);

    /* sanity check: read back RF_CH we just wrote */
    return (nrf24_read_reg(dev, NRF24_REG_RF_CH) == 76);
}
