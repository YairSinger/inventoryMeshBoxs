# Hardware

## Components Per Node

| Component | Part | Interface |
|---|---|---|
| MCU | ESP32-S3 N16R8 (16MB flash, 8MB PSRAM) | — |
| NFC Reader #1 | PN532 | SPI (shared bus) |
| NFC Reader #2 | PN532 | SPI (shared bus) |
| NFC Tags | NTAG213 (13.56MHz) | passive |
| Lid Trigger | MC-38 NO magnetic reed switch | GPIO4 input, internal pull-up |
| LED | WS2812B onboard GPIO48 | RMT peripheral |
| Buzzer | Passive buzzer 12mm (1.5–6V, SKU A61-B15) | GPIO17 (LEDC PWM) |
| Display | 0.96" SSD1306 OLED 128×64 | I2C (GPIO 2/3) |
| Power (proto) | USB power bank | USB-C |
| Power (final) | LiPo 2000mAh + TP4056 + MAX17043 | I2C fuel gauge |

## GPIO Pin Assignment

| Function | GPIO | Notes |
|---|---|---|
| SPI MOSI | 11 | shared PN532 #1 + #2 |
| SPI MISO | 13 | shared PN532 #1 + #2 |
| SPI SCK | 12 | shared PN532 #1 + #2 |
| PN532 #1 CS | 10 | inner reader |
| PN532 #2 CS | 9 | outer reader |
| Lid Trigger | 4 | MC-38 NO reed switch; lid closed = LOW, lid open = HIGH |
| GPIO spare | 5 | free after removing HC-SR04 prototype |
| WS2812B LED | 48 | onboard, RMT driver |
| I2C SDA | 2 | OLED + fuel gauge |
| I2C SCL | 3 | OLED + fuel gauge |
| BOOT / Factory reset | 0 | onboard BOOT button, active-low; do not use for lid state |
| Buzzer | 17 | passive buzzer, LEDC PWM, direct drive |

GPIO 19 and 20 are reserved for USB — do not use.

## Power Topology

Both PN532s **must not share a single ESP 3V3 pin** — empirically caused dirty SPI bus (see `TASKS.md` Phase 0 hardware discoveries 2026-05-27). Wire each PN532's VCC to a separate ESP 3V3 pin.

## Lid Trigger Wiring

The final Lid Trigger is an MC-38 NO magnetic reed switch. Mount the magnet so it is close to the reed switch when the lid is closed.

| MC-38 wire | Connects to |
|---|---|
| Wire A | GND |
| Wire B | GPIO4 |

Wire order does not matter. Configure GPIO4 as input with internal pull-up:
- Lid closed: reed closed, GPIO4 reads LOW
- Lid open: reed open, GPIO4 reads HIGH
- Broken/disconnected wire: GPIO4 reads HIGH and is treated as lid open

## Deep Sleep & Wake

- Primary wake source: GPIO4 (Lid Trigger, active-high open)
- Configure: `esp_sleep_enable_ext0_wakeup(GPIO_NUM_4, 1)` or the equivalent ESP32-S3 deep-sleep GPIO wake API
- On wake: read GPIO4 level to determine lid state
- BLE active only while lid is open + brief report delivery window after close
- Both PN532s powered down before deep sleep entry

Use compile-time flag `CONFIG_USE_BOOT_BUTTON_AS_LID` only as a prototype fallback. Production lid state comes from the MC-38 Lid Trigger on GPIO4. GPIO0 remains reserved for BOOT/factory reset because it is a boot strapping pin.

## Factory Reset (Hardware-Only)

**Long-press BOOT button (GPIO 0) for 10 s** while powered on:
- LED feedback: slow red breathing during hold, fast red flash + reboot at 10 s mark
- Erases all four IMB NVS namespaces + NimBLE bonding store
- No software path to trigger factory reset — physical access required

See [ble-contract.md §8](ble-contract.md#8-factory-reset).
