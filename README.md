# nRF24L01+ STM32 HAL Driver

A lightweight, self-contained nRF24L01(+) driver for STM32 using the STM32 HAL library.

The library provides a simple API for configuring the radio, transmitting and receiving packets, channel scanning, and generating a continuous carrier for RF testing.

## Features

- STM32 HAL compatible
- No timer required (uses DWT cycle counter for microsecond delays)
- Simple SPI abstraction
- TX and RX mode support
- Configurable:
  - RF Channel
  - Data Rate
  - Output Power
  - CRC
  - Auto Retransmission
  - Address Width
  - Payload Size
- Multi-pipe receive support (Pipe 0–5)
- FIFO management
- IRQ handling
- Spectrum scanner (RPD)
- Continuous carrier mode for RF testing

---

## Requirements

- STM32 HAL
- SPI peripheral
- GPIO pins for:
  - CE
  - CSN
- Cortex-M3/M4/M7 (DWT cycle counter required)

---

## File Structure

```
nrf24.c
nrf24.h
```

---

## Hardware Connection

| nRF24L01 | STM32 |
|----------|--------|
| VCC | 3.3V |
| GND | GND |
| CE | GPIO Output |
| CSN | GPIO Output |
| SCK | SPI SCK |
| MOSI | SPI MOSI |
| MISO | SPI MISO |
| IRQ | Optional GPIO Input |

> **Note:** The nRF24L01 is **NOT 5V tolerant**.

---

## Initialization

```c
nrf24_t radio;

if (!nrf24_init(
        &radio,
        &hspi1,
        CE_GPIO_Port,
        CE_Pin,
        CSN_GPIO_Port,
        CSN_Pin))
{
    Error_Handler();
}
```

---

## Basic Configuration

```c
uint8_t addr[5] = {'N','O','D','E','1'};

nrf24_set_channel(&radio, 76);
nrf24_set_data_rate(&radio, NRF24_DR_1MBPS);
nrf24_set_power(&radio, NRF24_PWR_0DBM);

nrf24_set_tx_address(&radio, addr);
nrf24_set_rx_address(&radio, 0, addr);

nrf24_enable_pipe(&radio, 0, true);

nrf24_set_payload_size(&radio, 0, 32);
```

---

## Transmitting

```c
uint8_t message[] = "Hello";

nrf24_tx_mode(&radio);

if (nrf24_transmit(&radio, message, sizeof(message)))
{
    // Transmission successful
}
else
{
    // Transmission failed
}
```

---

## Receiving

```c
uint8_t buffer[32];
uint8_t pipe;

nrf24_rx_mode(&radio);

if (nrf24_receive(&radio, buffer, sizeof(buffer), &pipe))
{
    // Data received
}
```

---

## Scanner Mode

The library can perform a simple spectrum scan using the RPD (Received Power Detector).

```c
nrf24_scanner_begin(&radio);

for (uint8_t ch = 0; ch <= 125; ch++)
{
    uint8_t busy = nrf24_scan_channel(&radio, ch);

    if (busy)
    {
        // RF activity detected
    }
}
```

---

## Continuous Carrier

Useful for RF testing.

Start carrier:

```c
nrf24_carrier_start(&radio, 76, NRF24_PWR_0DBM);
```

Stop carrier:

```c
nrf24_carrier_stop(&radio);
```

---

## API Overview

### Initialization

```c
nrf24_init()
```

### Configuration

```c
nrf24_set_channel()
nrf24_set_data_rate()
nrf24_set_power()
nrf24_set_crc()
nrf24_set_address_width()
nrf24_set_retries()
nrf24_set_payload_size()
nrf24_set_tx_address()
nrf24_set_rx_address()
nrf24_enable_pipe()
nrf24_set_auto_ack()
```

### Radio Modes

```c
nrf24_power_up()
nrf24_power_down()
nrf24_tx_mode()
nrf24_rx_mode()
```

### Data Transfer

```c
nrf24_transmit()
nrf24_receive()
nrf24_available()
nrf24_read_payload()
```

### Status

```c
nrf24_get_status()
nrf24_get_fifo_status()
nrf24_clear_irq()
nrf24_flush_tx()
nrf24_flush_rx()
```

### RF Test

```c
nrf24_scanner_begin()
nrf24_scan_channel()
nrf24_carrier_start()
nrf24_carrier_stop()
```

---

## Default Configuration

The driver initializes the radio with the following defaults:

| Parameter | Value |
|-----------|-------|
| Channel | 76 |
| Address Width | 5 bytes |
| Payload Size | 32 bytes |
| Data Rate | 1 Mbps |
| TX Power | 0 dBm |
| CRC | 2 Bytes |
| Auto ACK | Disabled |
| Dynamic Payload | Disabled |

---

## Notes

- Maximum payload size is **32 bytes**.
- Supports receive pipes **0–5**.
- Microsecond delays are generated using the Cortex-M DWT cycle counter.
- Auto-ACK is disabled by default during initialization.
- IRQ flags are cleared automatically after transmit and receive operations.
