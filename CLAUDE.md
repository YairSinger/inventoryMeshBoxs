# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

Event-driven, decentralized smart inventory system for rugged/mobile environments (camping trips). Multiple physical boxes form a single ESP-Mesh network. Each box tracks NFC-tagged items using two directional NFC readers, detects cross-box item migrations, and delivers one consolidated inventory report to a nearby smartphone — all without cloud infrastructure.

## Hardware Per Node

| Component | Part | Interface |
|---|---|---|
| MCU | ESP32-S3 N16R8 (16MB flash, 8MB PSRAM) | — |
| NFC Reader #1 | PN532 | SPI (shared bus) |
| NFC Reader #2 | PN532 | SPI (shared bus) |
| NFC Tags | NTAG213 (13.56MHz) | passive |
| Lid trigger (proto) | BOOT button GPIO0 / HC-SR04 | GPIO0 active-low |
| LED | WS2812B onboard GPIO48 | RMT peripheral |
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

GPIO 19 and 20 are reserved for USB — do not use.

## Directional NFC Scanning

Two PN532 readers are positioned at the box opening like a turnstile gate. Sequence of detection determines direction:

- Reader #1 fires first → Reader #2 fires within 200ms → **item inserted**
- Reader #2 fires first → Reader #1 fires within 200ms → **item extracted**
- Only one reader fires within 200ms → **AMBIGUOUS** (logged, excluded from authoritative set, surfaced as warning in report)

The authoritative item set is the **cumulative result of all insert/extract events** since the session started — not a point-in-time scan. Tags are NTAG213; item identity is read from the NDEF text record written to user memory pages (not from the UID alone).

## Tag Provisioning

Tags carry a human-readable item name written directly to NTAG213 user memory as an NDEF text record. The box writes the tag; the phone provides the name via BLE. Flow:

1. User places new item in box during registration session
2. PN532 reads tag — no valid NDEF name found
3. Box fires `EVENT_NOTIFY` → phone shows "Name this item" prompt
4. User types name → phone writes to `COMMAND_WRITE` characteristic
5. Box writes NDEF text record to tag via PN532 `InDataExchange`
6. Registration is **blocked** until all tags have valid names written

## Operational Modes

```
[SETUP] ──first BLE connect + PIN set──▶ [FIELD_CHECK]
[FIELD_CHECK] ──phone sends START_REGISTRATION──▶ [REGISTRATION]
[REGISTRATION] ──phone sends END_REGISTRATION──▶ [FIELD_CHECK]
```

### REGISTRATION mode
- Triggered by explicit BLE command from phone app
- Box scans continuously while lid is open (two PN532s active)
- Any new unregistered tag → immediate BLE alert + accept/reject prompt on phone
- Accepted tags: written to NVS `imb_local`, name written to physical tag
- Rejected tags: tracked as `FOREIGN` status
- Session ends when phone sends `END_REGISTRATION` command
- Registration is a hard gate: all tags must be named before `END_REGISTRATION` is accepted

### FIELD_CHECK mode (default after registration)
- Lid opens → both PN532s begin continuous polling → BLE advertisement starts
- Insert/extract events fire LED color + `EVENT_NOTIFY` BLE characteristic in real time
- Lid closes → delta computed against `imb_local` registry → if delta changed since last check, `REPORT_NOTIFY` fires
- Silence if nothing changed (no alert fatigue)
- Box stops BLE advertising after delivering report → deep sleep

## Deep Sleep & Wake

- Primary wake source: GPIO0 (BOOT button, active-low) — temporary until Hall effect sensor
- Configure: `esp_sleep_enable_ext0_wakeup(GPIO_NUM_0, 0)`
- On wake: read GPIO0 level to determine lid state
- BLE active only while lid is open + brief report delivery window after close
- Both PN532s powered down before deep sleep entry

Use compile-time flag `CONFIG_USE_BOOT_BUTTON_AS_LID` to switch between BOOT button and future Hall effect sensor without architectural changes.

## BLE GATT Structure

All payloads are **packed binary structs** with a 1-byte `msg_type` prefix. No JSON.

| Characteristic | Direction | Type | Purpose |
|---|---|---|---|
| `EVENT_NOTIFY` | box→phone | Notify | Real-time: tag inserted/extracted, AMBIGUOUS, new unknown tag, mode changes |
| `REPORT_NOTIFY` | box→phone | Notify | Field-check delta report on lid close (delta-only, silent if unchanged) |
| `COMMAND_WRITE` | phone→box | Write no response | Mode commands, tag name assignments, accept/reject responses |

## LED Color Contract (WS2812B GPIO48)

| Event | Color / Pattern |
|---|---|
| Item inserted | Green single pulse |
| Item extracted | Red single pulse |
| AMBIGUOUS scan | Yellow single flash |
| Registration pass (tag named + written) | Solid green 2s |
| Registration fail (write error) | Red double-flash |
| Mesh disconnected | Blue slow breathing |
| BLE connected, idle | White dim pulse every 3s |
| Deep sleep | Off |

## NVS Layout

Two namespaces with different trust levels:

| Namespace | Key | Type | Description |
|---|---|---|---|
| `imb_identity` | `box_name` | string | Human-readable name (phone-assigned on first BLE connect) |
| `imb_identity` | `box_id` | uint16 | Last 2 bytes of MAC address |
| `imb_identity` | `mesh_pin_hash` | uint32 | Hash of user-set PIN for mesh identification |
| `imb_local` | `item_<uid>` | struct | Per-item: UID + NDEF name + status. **Always valid, never stale.** |
| `imb_local` | `item_count` | uint16 | Total local registered items |
| `imb_mesh` | `mesh_epoch` | uint32 | Increments at start of each registration session |
| `imb_mesh` | `item_<uid>` | struct | Mesh-wide item registry. Flagged `MESH_STALE` on epoch mismatch. |
| `imb_txlog` | `tx_<seq>` | struct | Transaction log entries (idempotent, deduplicated by box_id+seq) |
| `imb_txlog` | `tx_head` | uint32 | Latest sequence number |
| `imb_state` | `op_mode` | uint8 | SETUP / REGISTRATION / FIELD_CHECK |

`imb_local` is always authoritative — valid even when isolated from mesh. `imb_mesh` is best-effort and must be treated as stale if `mesh_epoch` mismatches peers.

## Mesh Identity & PIN

Each mesh is identified by a user-set PIN (set on first registration session). Boxes advertise a **hash of the PIN** in their BLE advertisement payload. The phone app only shows boxes whose PIN hash matches the current mesh — prevents accidental merging with a neighbor's mesh.

Box joining flow (e.g., adding Box C to an existing A+B mesh):
1. Box C advertises `IMB-<name>-<last4MAC>` with its PIN hash
2. Phone shows Box C under "Available Unjoined Boxes"
3. User approves join → phone pushes current epoch + full mesh registry to Box C over BLE
4. Box C writes to `imb_mesh`, adopts epoch, joins ESP-Mesh

## Box Identity

- `box_id` = last 2 bytes of MAC address (derived via `esp_read_mac()`, no Wi-Fi needed)
- Human-readable name assigned by phone app on first BLE connection
- Box advertises as `IMB-SETUP-<last4MAC>` until named, then as `IMB-<name>`

## On Power Loss / Reboot

1. Box boots → reads `imb_local` from NVS (always valid)
2. Requires lid-open to rescan physical contents and update local state
3. If lid cannot be opened → rejoins mesh with `STATE_STALE` flag in report
4. Detects mesh epoch mismatch → requests full registry sync from phone (phone pulls from mesh peers)

## Phase 1 Scope (Single Box, No Mesh)

**IN:** BOOT button lid trigger, two PN532s (SPI, directional, AMBIGUOUS), WS2812B LED, NVS `imb_local` only, BLE GATT server (3 characteristics), registration mode (tag write, name prompt, hard gate), field-check delta-only alerts, accept/reject for post-registration tags, PIN stored in NVS (mesh-ready, unused).

**OUT:** ESP-Mesh, master box OLED + buttons, SR04, power monitoring, box naming flow (hardcode "Box 1").

## Toolchain

Targets ESP-IDF (Espressif IoT Development Framework).

```bash
# One-time setup
. $IDF_PATH/export.sh

# Build
idf.py build

# Flash + monitor (adjust port for your system)
idf.py -p /dev/cu.usbserial-* flash monitor

# Flash only / monitor only
idf.py -p /dev/cu.usbserial-* flash
idf.py -p /dev/cu.usbserial-* monitor

# Run a single component's unit tests on-device (Unity test runner)
idf.py -T components/<component_name> build flash monitor

# Run a single component's host tests (no hardware needed — fast TDD loop)
cd components/<component_name>/test && make run

# Erase NVS (resets all state — use during dev)
python $IDF_PATH/components/partition_table/parttool.py -p /dev/cu.usbserial-* erase_partition --partition-name nvs
```

## Testing Strategy

### Two-layer architecture

```
┌─────────────────────────────────┐
│  LOGIC LAYER (pure C)           │  ← host tests, <1s, plain Makefile + Unity
│  imb_detector, imb_registry,    │    no ESP-IDF dependency
│  imb_delta, imb_protocol        │    run with: cd components/<name>/test && make run
├─────────────────────────────────┤
│  HAL INTERFACE (function ptrs)  │  ← the boundary — never cross it in logic code
├─────────────────────────────────┤
│  DRIVER LAYER (ESP-IDF)         │  ← on-device tests, 30s flash cycle
│  PN532 SPI, WS2812B RMT,        │    run with: idf.py -p /dev/cu.* flash monitor
│  BLE GATT, NVS, GPIO/sleep      │    (standalone test project per component)
└─────────────────────────────────┘
```

ESP-IDF's `linux` target is too coupled to `esp_err.h` and hardware headers to compile cleanly on macOS for arbitrary components. Logic components use a plain `Makefile` + vendored Unity (`vendor/unity/` — 3 files, zero deps) instead.

### Host test structure (per logic component)

```
components/<name>/
├── include/<name>.h
├── <name>.c
└── test/
    ├── Makefile               # plain cc, no ESP-IDF
    ├── vendor/unity/          # unity.c, unity.h, unity_internals.h
    └── main/
        ├── test_<name>.c      # test functions: void test_xxx(void)
        └── runner.c           # main() with UNITY_BEGIN / RUN_TEST / UNITY_END
```

### HAL Abstraction Pattern

Logic components must never call hardware APIs directly. Use a thin interface struct passed at init:

```c
typedef struct {
    esp_err_t (*read_tag)(uint8_t reader_id, nfc_tag_t *out);
    esp_err_t (*write_tag)(uint8_t reader_id, const char *ndef_text);
    uint32_t  (*get_timestamp_ms)(void);
} imb_nfc_hal_t;
```

Production code populates the struct with real ESP-IDF driver functions. Host tests populate it with stubs returning controlled data. This pattern applies to NFC, NVS, and BLE interfaces.

### On-device test structure (per driver component)

Each driver component has a standalone ESP-IDF test project under `components/<name>/test/` with its own `CMakeLists.txt` + `sdkconfig`. Flash to hardware to run. Results appear on serial monitor.

## Key Design Constraints

- **Power first**: minimize active-window duration. Both PN532s off before deep sleep.
- **Zero heap fragmentation**: static or pool-allocated buffers only. Nodes run indefinitely.
- **No external infrastructure**: BLE and ESP-Mesh only. No Wi-Fi, no cloud, no DNS.
- **`imb_local` is ground truth**: never derive item presence from `imb_mesh` when local data exists.
- **Idempotent transactions**: same transaction arriving twice is a no-op (deduplicate by `box_id` + `seq`).
- **NVS writes are atomic**: use versioned double-buffer or NVS transactions to survive power-loss mid-write.
- **Directional detection is authoritative**: AMBIGUOUS events are never silently resolved — always surfaced to user.
