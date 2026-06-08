# build-board-debug skill

Hardware bringup and troubleshooting guide for the InventoryMeshBox ESP32-S3 board. Use this skill when diagnosing SPI/NFC reader issues, wiring problems, or serial output failures.

**Architecture context:** `docs/architecture.md` on branch `feat/phase2-master-box` (base commit `88ca922`). GPIO pin map in `docs/hardware.md`.

---

## Board assembly checklist

### Power
- ESP32-S3 N16R8 powered via USB-C UART port
- Each PN532 on its **own** 3V3 pin — shared 3V3 pin causes dirty SPI bus
- GND shared across all components

### PN532 SPI mode jumpers
Both PN532 boards need SW1 and SW2 set before first power-on:

| Pin | Jumper | Voltage | Meaning |
|-----|--------|---------|---------|
| I0  | SW1    | 0V (GND)   | LOW  |
| I1  | SW2    | 3.3V (VCC) | HIGH |

→ **I1=1, I0=0 = SPI mode** (PN532 User Manual Table 44)

Other modes for reference:
- I1=0, I0=1 = I2C
- I1=0, I0=0 = HSU/UART ← easy mistake, chips are silent in this mode

Verify with multimeter: probe I0 and I1 pads directly (not just SW pads).  
**Chips latch mode at power-on** — changing jumpers without power-cycling has no effect.

### SPI wiring (ESP32-S3 → both PN532 boards, shared bus)
| Signal | ESP GPIO | PN532 pin |
|--------|----------|-----------|
| MOSI   | 11       | SDI / MOSI |
| MISO   | 13       | SDO / MISO |
| SCK    | 12       | SCK        |
| CS #1  | 10       | NSS (inner reader) |
| CS #2  | 9        | NSS (outer reader) |
| GND    | GND      | GND        |

---

## SPI firmware notes

- **ESP-IDF hardware SPI does not work** for these chips — use bit-bang (see `main/main.c`)
- **LSB-first**: PN532 is natively LSB-first; boards have no bit-reversal circuit
- **MISO pull-up**: enable `GPIO_PULLUP_ENABLE` on GPIO 13 so floating line reads `0xFF`, not noise
- **Wakeup pulse**: assert CS low for 2ms with no clock before first command
- **2s boot delay**: PN532 needs ~1.5s after power-on before accepting commands
- **Protocol order**: send command → poll STATREAD until `0x01` → DATAREAD ACK → poll → DATAREAD response
  - Do NOT poll STATREAD before sending a command (returns `0x00` always, no pending data)

---

## Troubleshooting

### MISO always `0xFF` (all polls)
Chip is not driving MISO at all.

1. **Check jumpers first** — most likely cause. Probe I0 and I1 pads with multimeter.
   - I0=0V, I1=3.3V = SPI ✓
   - I0=0V, I1=0V = HSU/UART (silent on MISO) ✗
   - I0=3.3V, I1=0V = I2C ✗
2. Power-cycle the PN532 after any jumper change
3. Check MISO wire continuity (PN532 SDO → ESP GPIO 13)
4. Check CS wire — if CS never goes low at the chip, MISO stays high-Z

### MISO alternating `0xFF` / `0x00` on every poll
Chip is in HSU/UART mode. UART TX idles HIGH, occasionally sends UART bytes (appears as `0x00` transitions). Fix: jumpers.

### `status=0x08` or similar non-`0x01` non-`0xFF` on first poll
Normal — chip is mid-response, first byte may be partial. It will settle to `0x01` on next poll if in SPI mode.

### Chips don't respond after ESP reset (MISO=0 throughout)
Power-cycle the PN532 boards (disconnect 3V3). Stale state from previous session can prevent correct initialization.

### `idf_monitor` / `idf.py monitor` not working
Use the read_serial.py script instead — `idf_monitor` requires interactive TTY.
```bash
python3 /tmp/read_serial.py | tee /tmp/imb_smoke.log
# Press EN/RST on board after starting
```

### After running host tests, hardware flash fails
Host tests change `sdkconfig` to `linux` target. Reset before flashing:
```bash
idf.py set-target esp32s3
idf.py build flash
```

### nimble component error on build
`imb_ble` requires NimBLE which isn't enabled in Phase 0/1. Add to `CMakeLists.txt`:
```cmake
set(EXCLUDE_COMPONENTS "imb_ble")
```

---

## Confirmed working state (2026-05-27)

Both PN532s return `GetFirmwareVersion`:
```
IC=0x32  Ver=1  Rev=6  Support=0x07
```

ACK frame: `00 00 FF 00 FF 00`  
GFV response: `00 00 FF 06 FA D5 03 32 01 06 07 E8 00`

Firmware at this state: `main/main.c` — bit-bang LSB-first, wakeup pulse, MISO pull-up, STATREAD polling + force-read fallback.

**Next driver task**: `InListPassiveTarget` to read NTAG213 UID from a physical tag.
