# Project Tasks

> **Current branch:** `feat/phase2-master-box`
> **Context snapshot:** Phase 1 logic layer complete (25/25 tests). Phase 1 drivers + integration remain before Phase 2 begins.

---

## Phase 0 — Hello World (hardware bringup)
> Smoke-test every physical component before implementing real logic.

### Hardware discoveries (bringup sessions 2026-05-26 / 2026-05-27)
- PN532 wakeup requires CS held LOW ≥5ms with no clock → use manual GPIO CS (`spics_io_num = -1`)
- `idf_monitor` requires interactive TTY; use `python3 /tmp/read_serial.py` script instead (see dev-setup skill)
- After running host tests, `sdkconfig` gets reset to `linux` target → run `idf.py set-target esp32s3` before hardware builds
- Flash size warning (`16384k vs 2048k`) is harmless; fix later via menuconfig
- **Power matters**: both PN532s wired to the same ESP 3V3 pin produced dirty bus (MISO `10 10...` on #1). Splitting power across two 3V3 pins cleaned the bus.
- **Bit order**: tried both MSB-first and LSB-first with LSB flag — no difference in this state. Reverted to MSB-first. Likely irrelevant until SPI mode is confirmed.
- **SW1/SW2 jumper combo unknown for our specific boards** — TASKS.md previously claimed `SW1=0,SW2=1` works, but that was never confirmed by seeing a real `D5 03` response. Worth checking silkscreen labels next session.

### Session 2026-05-28 — tag detection confirmed
- **SAMConfiguration is mandatory**: must be sent before any RF command or the PN532 RF field stays inactive. Every reference library sends this as the first init command.
- **Wakeup pulse required before each scan**: PN532 enters standby between commands; CS must be held low ≥10ms (tOSC_START max 2ms + margin) before each SPI transaction.
- **Confirmed init order**: `probe (GetFirmwareVersion)` → `SAMConfiguration(NormalMode)` → `RFConfiguration(MaxRetries, MxRtyPassiveActivation=3)` → scan loop.
- **Tag detected**: MIFARE Classic 1K, ATQA=0004, SAK=08, UID `14 E0 99 2C` on reader #1.
- `/chip-integration` skill added globally — invoke before any new peripheral integration.

### Last state (2026-05-27, paused to focus on BLE)
- Both PN532s on bus, both MISO read clean idle `FF FF FF…` after split-power + new wires
- Neither chip drives MISO in response to `GetFirmwareVersion` (`00 00 FF 02 FE D4 02 2A 00`)
- Conclusion: chips are powered + on the bus but **not in SPI mode**, or not receiving SCK/MOSI
- Next time: physically verify SW1/SW2 jumper positions against board silkscreen; measure 3.3V at both VCC pads; consider scope/LA capture of SCK + MOSI at PN532 input pin to confirm signals arrive

### Tasks
- [x] PN532 #1 (CS GPIO10): verify `GetFirmwareVersion` returns `IC=0x32 Ver=1` — **DONE** (IC=0x32 Ver=1 Rev=6)
- [x] PN532 #2 (CS GPIO9): verify `GetFirmwareVersion` — **DONE** (IC=0x32 Ver=1 Rev=6)
- [x] WS2812B RMT driver: cycles through LED color contract (GPIO 48) — **DONE** (main/main.c, all 10 patterns confirmed on board)
- [x] Deep sleep + timer wakeup — **DONE** (integration test PASS, 3s timer → WAKEUP_TIMER confirmed)
- [x] NVS driver: write + read + erase `imb_local` namespace — **DONE** (integration tests PASS)
- [ ] Deep sleep + BOOT button wake: GPIO 0 wakes from deep sleep
- [ ] BLE GATT server: phone connects, subscribes to notify chars, writes COMMAND_WRITE
- [ ] Verify GPIO pin assignments on real board (see CLAUDE.md for map)

---

## Phase 1 — Single Box Logic (TDD, host-testable) ✅ COMPLETE

All components under `components/imb_*/`. Run tests: `cd components/<name>/test && make run`

| Component | What it does | Tests |
|---|---|---|
| `imb_types` | Shared types: `imb_item_t`, `imb_entry_u`, `imb_op_mode_e`, size constants | — |
| `imb_detector` | Directional NFC scan → INSERT / EXTRACT / AMBIGUOUS | 5/5 ✅ |
| `imb_registry` | Item CRUD over injected NVS HAL; max persisted; reboot-safe | 5/5 ✅ |
| `imb_session` | Net present/ambiguous sets for one lid-open cycle | 5/5 ✅ |
| `imb_delta` | Diff session vs registry → PRESENT/MISSING/FOREIGN/AMBIGUOUS | 5/5 ✅ |
| `imb_protocol` | BLE binary pack/unpack for all message types; round-trip tested | 5/5 ✅ |

### Key design decisions made
- HAL injection pattern: all logic components accept function-pointer structs; no direct ESP-IDF calls
- `imb_entry_u` union: session writes `uid`, registry fills `name` in-place — zero copy
- `IMB_DELTA_MAX_ENTRIES = 3 × 64`: R (registry) + F (foreign) + A (ambiguous) worst case
- `IMB_REPORT_MAX_ENTRIES = 8 × 64 = 512`: mesh-wide consolidated report (Phase 3)
- AMBIGUOUS takes priority over MISSING when same uid appears in both sets
- Enum types use `_e` suffix; struct typedefs use `_t`
- `box_id` before `status` in `imb_pkt_report_entry_t` for natural alignment

---

## Phase 1 — Single Box Drivers (on-device, flash required)

> Run with: `idf.py -T components/<name> build flash monitor`
> Erase NVS between runs: see CLAUDE.md toolchain section.

- [x] PN532 #1 SPI driver: reads ISO 14443A UID (CS GPIO 10, inner reader) — **DONE** (MIFARE Classic confirmed, UID 14 E0 99 2C)
- [x] PN532 #2 SPI driver: reads ISO 14443A UID (CS GPIO 9, outer reader) — **DONE**
- [x] PN532 NDEF / tag write: MIFARE Classic 1K block write + readback — **DONE** (integration test PASS; NTAG213 path implemented, untested)
- [x] WS2812B RMT driver: cycles through LED color contract (GPIO 48) — **DONE**
- [x] NVS driver: write + read + erase `imb_local` namespace — **DONE**
- [x] Deep sleep + timer wakeup — **DONE**
- [ ] Deep sleep + BOOT button wake: GPIO 0 wakes from deep sleep
- [ ] BLE GATT server: phone connects, subscribes to notify chars, writes COMMAND_WRITE

## Phase 1 — BLE Server (architecture decided 2026-05-31)

**Source of truth:** [`docs/ble-contract.md`](docs/ble-contract.md) — do NOT edit without explicit approval.

**Reference implementations:**
- `inventoryMeshBoxs-gemini/components/imb_ble/` — NimBLE boilerplate reference (GATT table, advertise, GAP events). Architecture differs (auth baked in) but NimBLE calls are correct.
- `inventoryMeshBoxs-phone/lib/protocol.dart` — locked wire format. All multi-byte fields little-endian.
- `inventoryMeshBoxs-gemini/components/imb_protocol/include/imb_protocol.h` — fully updated protocol header; use as source for step 1.

### Architecture

```
main.c
  ├── imb_ble          (new driver — pure NimBLE transport, no business logic)
  └── imb_ble_session  (new logic component — host-testable, HAL-injected NVS)
```

**`imb_ble` public API:**
```c
esp_err_t imb_ble_init(const imb_ble_callbacks_t *cbs, void *ctx);
esp_err_t imb_ble_notify_event(const uint8_t *buf, size_t len);
esp_err_t imb_ble_notify_report(const uint8_t *buf, size_t len);
esp_err_t imb_ble_update_adv(uint32_t pin_hash, imb_op_mode_e mode, uint8_t flags, const char *box_name);
void      imb_ble_disconnect(void);
```
Callbacks: `on_subscribed(ctx)` [EVENT_NOTIFY CCCD enabled], `on_cmd(ctx, buf, len)`, `on_disconnected(ctx)`

**`imb_ble_session` owns:**
- CMD_HELLO auth gate + PIN check; 5 s HELLO timeout via `ble_npl_callout`
- 8-event RAM queue; flushed on `on_subscribed` (before HELLO — phone never subscribes REPORT_NOTIFY before HELLO)
- Mode state machine + NVS persistence (HAL-injected `imb_ble_session_nvs_hal_t`)
- 60 s grace window timer via `ble_npl_callout` (fires on NimBLE task → no mutex needed)
- Report chunking + ACK/NACK retry loop (`CMD_REPORT_ACK`/`CMD_REPORT_NACK`)
- App callbacks: `on_name_tag`, `on_accept_tag`, `on_mode_set`, `on_set_pin`, `on_report_delivered`
- App calls `imb_ble_session_ack(msg_id, status)` after async hardware ops complete

### Key constraints
- UUIDs already locked: `e5d50000-01d0-47e0-afc5-01e466d9298e` (base); last 16 bits = 0x0001/0002/0003
- `imb_ble` uses `BLE_GATT_CHR_F_WRITE` (write-with-response) for COMMAND_WRITE
- Advertisement name: `IMB-<name>-<last4MAC>` in normal mode; `IMB-SETUP-<last4MAC>` in SETUP mode
- Connection params: FIELD_CHECK (15–30 ms, lat=0, sup=2 s) / REGISTRATION (100–200 ms, lat=4, sup=6 s)

### Step 1 — Port `imb_protocol` ✦ START HERE
- [ ] Copy updated structs from `inventoryMeshBoxs-gemini/components/imb_protocol/include/imb_protocol.h` into `components/imb_protocol/include/imb_protocol.h`: all `[NEW]` message types, `msg_id` in all CMD structs, `imb_ack_status_e`, `imb_pkt_adv_t`, UUID defines, updated pack/unpack signatures
- [ ] Update `imb_protocol.c` pack/unpack implementations to match (port from gemini)
- [ ] Update host tests: add round-trip tests for CMD_HELLO, EVENT_ACK, REPORT_CHUNK, CMD_MODE/NAME/ACCEPT with msg_id, all ACK status codes

### Step 2 — `imb_ble` driver component
- [ ] Create `components/imb_ble/` with CMakeLists, `imb_ble.h`, `imb_ble.c`
- [ ] NimBLE init: `nimble_port_init`, `ble_hs_cfg.sync_cb`, host task (port from gemini)
- [ ] GATT table: 1 service, 3 characteristics — EVENT_NOTIFY (notify), REPORT_NOTIFY (notify), COMMAND_WRITE (write) — UUID byte arrays from gemini
- [ ] `BLE_GAP_EVENT_SUBSCRIBE` handler: detect EVENT_NOTIFY CCCD enable → call `on_subscribed` callback
- [ ] `BLE_GAP_EVENT_CONNECT`: single-client enforcement (`ble_gap_terminate` if second connection); start 5 s HELLO timeout
- [ ] `BLE_GAP_EVENT_DISCONNECT`: call `on_disconnected`, restart advertising
- [ ] `BLE_GAP_EVENT_MTU`: log only
- [ ] Advertisement: `IMB-<name>-<last4MAC>` + 8-byte mfg-data struct; `IMB-SETUP-<last4MAC>` when pin_hash=0
- [ ] `imb_ble_update_adv()`: stop + restart advertisement with new mfg-data
- [ ] Connection parameter re-request on `imb_ble_set_conn_params(profile)` call
- [ ] Just Works pairing + LE Secure Connections: set `ble_hs_cfg.sm_sc=1`, `sm_bonding=1`, `sm_our_key_dist`, `sm_their_key_dist`
- [ ] **Hardware smoke-test milestone**: nRF Connect sees `IMB-*`, connects, sees 3 characteristics, can subscribe + write

### Step 3 — `imb_ble_session` logic component (TDD)
- [ ] Create `components/imb_ble_session/` with CMakeLists, header, source, test/
- [ ] Define `imb_ble_session_nvs_hal_t` (read/write op_mode, read/write pending_uids)
- [ ] Define `imb_ble_session_app_cbs_t` (on_name_tag, on_accept_tag, on_mode_set, on_set_pin, on_report_delivered)
- [ ] Auth gate: `on_cmd` dispatches CMD_HELLO first; all others return NOT_AUTHED until authed
- [ ] 8-event ring buffer (depth=8): `imb_ble_session_push_event_tag()` enqueues; `on_subscribed` flushes via `imb_ble_notify_event()`
- [ ] EVENT_DROPPED: if queue full, drop oldest + increment drop counter; send `EVENT_DROPPED` on flush
- [ ] Mode state machine: validate transitions per contract §6.2; persist on change via NVS HAL
- [ ] Grace window: `ble_npl_callout` started on `on_disconnected` during REGISTRATION; cancelled on reconnect; on expiry → FIELD_CHECK or REGISTRATION_INCOMPLETE depending on pending_uids
- [ ] REGISTRATION_INCOMPLETE: persist to NVS; replay pending EVENT_TAGs on reconnect
- [ ] Report delivery: `imb_ble_session_deliver_report(entries, count)` → chunks → REPORT_NOTIFY; track ACK/NACK; retry on NACK; call `on_report_delivered` when complete or 30 s window expires
- [ ] Host tests: auth gate, queue flush, drop counter, mode transitions, grace window expiry (both branches), REGISTRATION_INCOMPLETE persistence, report chunking + NACK retry

### Step 4 — NVS schema additions (`imb_state` namespace)
- [ ] `op_mode` uint8
- [ ] `pending_count` uint8
- [ ] `pending_uids` packed array (up to 64 × IMB_UID_LEN bytes)
- [ ] `pending_epoch` uint32 (grace window start time)

### Step 5 — Integration (`main.c` wiring)
- [ ] Wire `imb_ble` callbacks → `imb_ble_session`
- [ ] Wire `imb_ble_session` app-callbacks → PN532 driver (CMD_NAME → NDEF write), `imb_registry` (CMD_ACCEPT), NVS (CMD_SET_PIN, CMD_MODE)
- [ ] Wire `imb_detector` → `imb_session` in lid-open loop
- [ ] Wire `imb_session` + `imb_registry` → `imb_delta` on lid close → `imb_ble_session_deliver_report()`
- [ ] REGISTRATION_INCOMPLETE lid-open-rescan recovery: check pending UIDs against scan result, auto-transition if all absent
- [ ] Factory reset: 10 s BOOT button hold → erase all `imb_*` NVS namespaces + NimBLE bond store → reboot
- [ ] Mode persisted in `imb_state.op_mode`, loaded on boot

---

## Phase 2 — Master Box (current branch: `feat/phase2-master-box`)

- [ ] OLED SSD1306 driver (I2C SDA GPIO 2, SCL GPIO 3)
- [ ] 3-button navigation driver (UP GPIO 14, SELECT GPIO 15, BACK GPIO 16)
- [ ] Master box UI: registration flow on-device
- [ ] Master box UI: field check report display
- [ ] Master box UI: mesh status + box list

---

## Phase 3 — Mesh (future)

- [ ] ESP-Mesh join / leave
- [ ] Transaction log broadcast + idempotent apply (`imb_txlog` NVS namespace)
- [ ] Cross-box item migration detection
- [ ] Mesh registry sync on epoch mismatch → request full sync from phone
- [ ] PIN-based mesh identity: hash in BLE advertisement, box join approval flow
- [ ] Consolidated mesh BLE report (all boxes → one REPORT_NOTIFY via master)

---

## Testing infrastructure

- [x] Host test runner: plain Makefile + vendored Unity (zero ESP-IDF dep)
- [x] Host test structure documented in CLAUDE.md
- [x] Dev-setup skill: `.claude/skills/dev-setup.md`
- [ ] On-device test structure: standalone ESP-IDF project per driver component (see CLAUDE.md)
