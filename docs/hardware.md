# Hardware

## Components Per Node

| Component | Part | Interface |
|---|---|---|
| MCU | ESP32-S3 N16R8 (16MB flash, 8MB PSRAM) | — |
| NFC Reader #1 | PN532 | SPI (shared bus) |
| NFC Reader #2 | PN532 | SPI (shared bus) |
| NFC Tags | NTAG213 (13.56MHz) | passive |
| Lid trigger (proto) | BOOT button GPIO0 / HC-SR04 | GPIO0 active-low |
| LED | WS2812B onboard GPIO48 | RMT peripheral |
| Buzzer | Passive buzzer 12mm (1.5–6V, SKU A61-B15) | GPIO17 (LEDC PWM) |
| Master box screen | OLED SSD1306 0.96" | I2C |
| Master box input | 3x tactile buttons | GPIO |
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
| SR04 TRIG | 4 | temporary lid sensor |
| SR04 ECHO | 5 | temporary lid sensor |
| WS2812B LED | 48 | onboard, RMT driver |
| I2C SDA (master) | 2 | OLED + fuel gauge |
| I2C SCL (master) | 3 | OLED + fuel gauge |
| Button UP (master) | 14 | |
| Button SELECT (master) | 15 | |
| Button BACK (master) | 16 | |
| Lid trigger (proto) | 0 | BOOT button, active-low |
| Buzzer | 17 | passive buzzer, LEDC PWM, direct drive |

GPIO 19 and 20 are reserved for USB — do not use.

## Power Topology

Both PN532s **must not share a single ESP 3V3 pin** — empirically caused dirty SPI bus (see `TASKS.md` Phase 0 hardware discoveries 2026-05-27). Wire each PN532's VCC to a separate ESP 3V3 pin.

## Deep Sleep & Wake

- Primary wake source: GPIO0 (BOOT button, active-low) — temporary until Hall effect sensor
- Configure: `esp_sleep_enable_ext0_wakeup(GPIO_NUM_0, 0)`
- On wake: read GPIO0 level to determine lid state
- BLE active only while lid is open + brief report delivery window after close
- Both PN532s powered down before deep sleep entry

Use compile-time flag `CONFIG_USE_BOOT_BUTTON_AS_LID` to switch between BOOT button and future Hall effect sensor without architectural changes.

## Factory Reset (Hardware-Only)

**Long-press BOOT button (GPIO 0) for 10 s** while powered on:
- LED feedback: slow red breathing during hold, fast red flash + reboot at 10 s mark
- Erases all four IMB NVS namespaces + NimBLE bonding store
- No software path to trigger factory reset — physical access required

See [ble-contract.md §8](ble-contract.md#8-factory-reset).
