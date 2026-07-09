# nRF24L01(+) Driver for STM32 HAL

Lightweight driver for the 2.4 GHz nRF24L01/nRF24L01+ radio module on top of the
STM32 HAL. Supports normal TX/RX packet mode (auto-ack, multi-pipe, retransmit)
and interrupt-driven operation via the IRQ pin.

- **Target:** STM32F4xx (tested on STM32F405RGT6)
- **Bus:** SPI mode 0 (CPOL=0, CPHA=0), 8-bit, MSB first, SCK max 10 MHz
- **Dependencies:** STM32 HAL only. Microsecond delays use the DWT cycle counter
  (no separate TIMER required).

---

## Files

- `nrf24.h` — register map, commands, enums, and the API prototypes
- `nrf24.c` — implementation

Copy both into your project (e.g. `Core/Src` and `Core/Inc`, or a dedicated
`Drivers/nrf24/` folder added to your include path).

---

## Wiring

| nRF24 | STM32F405 | Notes |
|-------|-----------|-------|
| VCC   | 3.3V      | **Not 5V.** A 10 µF cap next to the module is mandatory |
| GND   | GND       | |
| CE    | GPIO out  | e.g. PB0 |
| CSN   | GPIO out  | e.g. PB1 |
| SCK   | SPI1_SCK  | PA5 |
| MOSI  | SPI1_MOSI | PA7 |
| MISO  | SPI1_MISO | PA6 |
| IRQ   | GPIO EXTI | optional — interrupt mode only |

Missing 10 µF decoupling is the number-one cause of a module that "won't respond".
Don't skip it.

### CubeMX pin setup

- **CSN, CE:** GPIO_Output, push-pull, no pull, low speed.
  Initial level: CSN = High, CE = Low (optional; the library sets these itself in init).
- **SPI:** mode 0, 8-bit, MSB first. Set the prescaler so SCK < 10 MHz.
  On the F405 with APB2 = 84 MHz → prescaler 16 = 5.25 MHz (safe).
- **IRQ (optional):** GPIO_EXTI, **Falling edge** trigger, Pull-up.
  Enable the EXTI line in the NVIC tab.

---

## Quick start

```c
#include "nrf24.h"

nrf24_t dev;

void setup(void) {
    if (!nrf24_init(&dev, &hspi1,
                    NRF_CE_GPIO_Port,  NRF_CE_Pin,
                    NRF_CSN_GPIO_Port, NRF_CSN_Pin)) {
        // module not responding — check wiring, decoupling, and SPI mode
    }
}
```

`nrf24_init()` returns a `bool`: it reads back the `RF_CH` register after writing
it, so this doubles as an **SPI link check**. If it returns `false`, the usual
culprits are swapped CSN/SCK or insufficient decoupling.

Defaults after init: 2-byte CRC, 5-byte address width, 32-byte payload,
auto-ack off, channel 76, 1 Mbps, 0 dBm, powered up.

---

## Example: Transmitter

```c
uint8_t addr[5] = {0xE7, 0xE7, 0xE7, 0xE7, 0xE7};

nrf24_set_channel(&dev, 76);
nrf24_set_data_rate(&dev, NRF24_DR_1MBPS);
nrf24_set_power(&dev, NRF24_PWR_0DBM);
nrf24_set_tx_address(&dev, addr);
nrf24_set_rx_address(&dev, 0, addr);   // pipe 0 must match TX addr for auto-ack
nrf24_set_auto_ack(&dev, 0, true);
nrf24_set_retries(&dev, 5, 15);        // 1500 us, 15 retries
nrf24_tx_mode(&dev);

uint8_t payload[32] = "hello";
if (nrf24_transmit(&dev, payload, sizeof(payload))) {
    // TX_DS: sent (and acked, if auto-ack is on)
} else {
    // MAX_RT / timeout: failed
}
```

## Example: Receiver (polling)

```c
uint8_t addr[5] = {0xE7, 0xE7, 0xE7, 0xE7, 0xE7};

nrf24_set_channel(&dev, 76);
nrf24_set_rx_address(&dev, 1, addr);
nrf24_enable_pipe(&dev, 1, true);
nrf24_set_auto_ack(&dev, 1, true);
nrf24_set_payload_size(&dev, 1, 32);
nrf24_rx_mode(&dev);

// loop:
uint8_t buf[32], pipe;
if (nrf24_receive(&dev, buf, 32, &pipe)) {
    // buf[] holds data from `pipe`
}
```

## Example: Receiver (interrupt)

```c
volatile bool nrf_irq_flag = false;

void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin) {
    if (GPIO_Pin == NRF_IRQ_Pin) nrf_irq_flag = true;
}

// after init:
nrf24_set_irq_mask(&dev, true, false, false);  // only RX_DR asserts IRQ
nrf24_rx_mode(&dev);

// main loop:
if (nrf_irq_flag) {
    nrf_irq_flag = false;
    uint8_t buf[32], pipe;
    while (nrf24_receive(&dev, buf, 32, &pipe)) {
        // process buf[] — the loop drains all FIFO levels (max 3)
    }
    nrf24_clear_irq(&dev);   // REQUIRED: release the IRQ pin so EXTI can fire again
}
```

> **Classic gotcha:** the nRF24 IRQ pin stays held LOW until the STATUS flags are
> cleared. If you forget `nrf24_clear_irq()`, EXTI fires once and then goes silent.

---

## API summary

### Init & low-level
| Function | Notes |
|----------|-------|
| `nrf24_init(dev, hspi, ce_port, ce_pin, csn_port, csn_pin)` | Init + SPI check. Returns `bool` |
| `nrf24_read_reg / write_reg` | Single-byte register access |
| `nrf24_read_reg_buf / write_reg_buf` | Multi-byte register access (addresses) |
| `nrf24_send_cmd(dev, cmd)` | Send 1-byte command, returns STATUS |

### Configuration
| Function | Notes |
|----------|-------|
| `nrf24_set_channel(dev, ch)` | Channel 0..125 |
| `nrf24_set_data_rate(dev, dr)` | `NRF24_DR_1MBPS / 2MBPS / 250KBPS` |
| `nrf24_set_power(dev, pwr)` | `NRF24_PWR_M18DBM / M12DBM / M6DBM / 0DBM` |
| `nrf24_set_crc(dev, crc)` | `NRF24_CRC_DISABLE / 1BYTE / 2BYTE` |
| `nrf24_set_address_width(dev, aw)` | `NRF24_AW_3BYTES / 4BYTES / 5BYTES` |
| `nrf24_set_retries(dev, delay, count)` | delay 0..15 (×250 µs), count 0..15 |
| `nrf24_set_payload_size(dev, pipe, size)` | Fixed payload 0..32 |
| `nrf24_set_tx_address(dev, addr)` | |
| `nrf24_set_rx_address(dev, pipe, addr)` | pipe 0-1 full width, 2-5 LSB only |
| `nrf24_enable_pipe(dev, pipe, en)` | |
| `nrf24_set_auto_ack(dev, pipe, en)` | |

### Mode & data
| Function | Notes |
|----------|-------|
| `nrf24_power_up / power_down` | |
| `nrf24_rx_mode / tx_mode` | |
| `nrf24_transmit(dev, data, len)` | Blocking, returns `true` on TX_DS |
| `nrf24_available(dev, &pipe)` | Data waiting in RX FIFO? |
| `nrf24_read_payload(dev, buf, len)` | |
| `nrf24_receive(dev, buf, len, &pipe)` | Non-blocking, IRQ-safe |

### Status & IRQ
| Function | Notes |
|----------|-------|
| `nrf24_get_status / get_fifo_status` | |
| `nrf24_flush_tx / flush_rx` | |
| `nrf24_clear_irq(dev)` | Clear RX_DR \| TX_DS \| MAX_RT |
| `nrf24_set_irq_mask(dev, rx_dr, tx_ds, max_rt)` | `true` = interrupt enabled |

---

## Troubleshooting

| Symptom | Likely cause |
|---------|--------------|
| `nrf24_init()` returns `false` | Swapped CSN/SCK, SPI not in mode 0, poor decoupling |
| TX always MAX_RT | RX addr ≠ TX addr, pipe 0 RX addr not set to TX addr, mismatched channel/data rate |
| IRQ fires once then goes silent | Forgot `nrf24_clear_irq()` |
| RX never receives data | Pipe not enabled, mismatched payload size, mismatched CRC across nodes |
| Corrupt data at close range | 0 dBm too hot for a nearby RX — lower power or add distance |

---

## License

Free to use and modify for your own projects.
