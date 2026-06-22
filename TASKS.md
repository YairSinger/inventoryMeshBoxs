# Project Tasks

> **Current branch:** `feat/async-ndef-write` (branched from `feat/item-registration`, merged to main 2026-06-08)
> **Context snapshot:** Async 60s NDEF write window + read-back/cross-mesh auto-register implemented; PN532/NDEF extracted into `imb_nfc` component with HAL. 2026-06-15 bringup session fixed reader-direction convention, MIFARE key fallback, and session ambiguous-resolution. 2026-06-16: `tools/box_scenario_test` unblocked — fixed dual-core IPC hang in standalone tool binaries (`FREERTOS_UNICORE=y` + no `esp_psram`); documented in `docs/testing.md`. Test binary boots clean and reaches Phase A. Next: run the full 3-boot scenario test (Phase A–E) with cards 1-9.

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

### Session 2026-06-15 — direction convention, MIFARE keys, ambiguous resolution
- **Old board (MAC `e0:72:a1:d3:62:34`) flash chip is dead** — unresponsive to esptool across power cycles/cables/ports. Set aside.
- **New board (MAC `e0:72:a1:d4:0f:00`)** confirmed working, same wiring/pinout. Use this going forward.
- **9 NDEF cards written + verified** on the outer reader (GPIO9) — `card1`..`card9`, UIDs recorded in `tools/card_write_test`. Cards 6/7/9 needed MIFARE key2 (`D3F7D3F7D3F7`) instead of the factory default.
- **Fixed reader-direction inversion** in `imb_detector`: outer reader (1) first = INSERT, inner (0) first = EXTRACT, matching the documented turnstile convention (`docs/protocols.md`).
- **Added MIFARE key-cache fallback** (key0/key1/key2) in `imb_nfc_pn532` so cards provisioned with non-default keys authenticate.
- **Fixed `imb_session` ambiguous resolution**: an Ambiguous Detection for a Tag is now superseded by the next clear INSERT/EXTRACT for that Tag (latest wins); an orphan EXTRACT (no presence record) is a no-op. `CONTEXT.md` now defines **Ambiguous Detection** to resolve the prior overloaded use of "AMBIGUOUS".
- Committed as `03088bc` (gpio.h fix + HW test prompts) and `c943059` (direction/MIFARE/session fixes).
- New `components/imb_nfc/test_hw` on-device test added (Group 5 movement scenarios).

### Tasks
- [x] PN532 #1 (CS GPIO10): verify `GetFirmwareVersion` returns `IC=0x32 Ver=1` — **DONE** (IC=0x32 Ver=1 Rev=6)
- [x] PN532 #2 (CS GPIO9): verify `GetFirmwareVersion` — **DONE** (IC=0x32 Ver=1 Rev=6)
- [x] WS2812B RMT driver: cycles through LED color contract (GPIO 48) — **DONE** (main/main.c, all 10 patterns confirmed on board)
- [x] Deep sleep + timer wakeup — **DONE** (integration test PASS, 3s timer → WAKEUP_TIMER confirmed)
- [x] NVS driver: write + read + erase `imb_local` namespace — **DONE** (integration tests PASS)
- [ ] MC-38 Lid Trigger wake: GPIO4 wakes from deep sleep on lid-open HIGH
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
- [ ] MC-38 Lid Trigger driver: GPIO4 input + pull-up; lid closed LOW, lid open HIGH
- [ ] Deep sleep + Lid Trigger wake: GPIO4 wakes from deep sleep on lid-open HIGH
- [x] BLE GATT server: phone connects, subscribes to notify chars, writes COMMAND_WRITE — **DONE** (imb_ble + imb_ble_session wired in main.c)
- [x] Passive buzzer (GPIO 17, LEDC PWM): `imb_buzzer` component, 6 named patterns, wired to NFC events — **DONE**
- [x] **`imb_led` component** (WS2812B GPIO 48): HAL-pattern component, 9 named patterns, wired to NFC events — **DONE** (7/7 host tests PASS; RMT HAL in imb_led_rmt.c)

## Phase 1 — Lid Trigger + Box Activity

**Hardware:** MC-38 NO magnetic reed switch on GPIO4. One wire to GND, one wire to GPIO4. Internal pull-up enabled. Magnet is close when the lid is closed.

**Polarity:** lid closed = LOW, lid open = HIGH. A disconnected sensor reads HIGH and is treated as lid open.

### `imb_lid_trigger` logic component (host-testable, HAL-injected)
- [ ] Add instance-based `imb_lid_trigger_t`
- [ ] HAL exposes `read_state(ctx)` and `now_ms(ctx)`; hardware-specific polarity stays outside logic
- [ ] Implement 50 ms debounce for transitions after initialization
- [ ] Initialize stable state immediately from current HAL state on boot
- [ ] API: `imb_lid_trigger_get_state()` and `imb_lid_trigger_poll()` returning changed stable state
- [ ] Host tests: boot-open seeding, boot-closed seeding, bounce ignored, stable open edge, stable close edge

### `imb_lid_trigger_gpio_mc38_no` ESP-IDF driver
- [ ] Configure GPIO4 as input with internal pull-up
- [ ] Map GPIO4 HIGH → `IMB_LID_OPEN`, GPIO4 LOW → `IMB_LID_CLOSED`
- [ ] Configure deep-sleep wake on GPIO4 HIGH
- [ ] Keep GPIO0 for BOOT/factory reset only
- [ ] Hardware verify: lid closed reads LOW, lid open reads HIGH, disconnected sensor reads HIGH
- [ ] Hardware verify: deep sleep wakes on lid open from GPIO4 HIGH

### `imb_box_activity` logic component (host-testable orchestrator)
- [ ] Own `imb_session_t` lifecycle instead of mutating session directly from `main.c`
- [ ] Route detector events through `imb_box_activity_on_scan_event()`; apply only during open sessions
- [ ] Model activity states: sleeping, open session, closing session, report unpersisted, report finalized
- [ ] Lid open starts or resumes the current Field Check Session
- [ ] Lid close freezes scan input and calls mocked `imb_inventory_state_finalize_field_check(session, out_report_id)`
- [ ] If local finalization succeeds, reset session and allow delivery/deep-sleep policy
- [ ] If local finalization fails, keep frozen session in RAM, surface error, and do not enter deep sleep
- [ ] If lid reopens before finalization succeeds, resume same session and discard generated RAM report
- [ ] If lid reopens after finalization succeeds, start a new session; pending phone delivery remains separate
- [ ] Host tests for reopen-before-finalize, reopen-after-finalize, persistence failure, late scan ignored, empty report finalizes

### Planned dependency: `imb_inventory_state`
- [ ] Plan as separate NVS-backed component; mock/stub during Lid Trigger implementation
- [ ] Persist latest Box Inventory State locally
- [ ] Apply finalized Field Check Report as the transition from old state to new state
- [ ] Persist empty/clean Field Check Reports as successful finalized checks
- [ ] Track delivery-pending state independently from local finalization
- [ ] Expose Box Inventory State to OLED and pending Field Check Reports to BLE delivery
- [ ] Domain model: identity class (`REGISTERED`, `FOREIGN`) separate from presence state (`UNCHECKED`, `PRESENT`, `MISSING`, `AMBIGUOUS`)
- [ ] Do not seed Box Inventory State during Item Registration; registration updates `imb_local` identity only

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

### Steps 1–6 — **DONE** (2026-06-08)
- [x] `imb_protocol`: all message types, pack/unpack, `imb_pkt_cmd_set_pin_t`, `imb_pkt_cmd_box_name_t` (0x1A), locked UUIDs
- [x] `imb_ble`: NimBLE transport, GATT table, advertise (100–125 ms interval), Just Works + LE SC bonding, both-CCCD subscribe gate, `imb_ble_unpair_current()`
- [x] `imb_ble_session`: auth gate, 8-event queue, EVENT_DROPPED, mode state machine, CMD_SET_PIN (full), CMD_BOX_NAME (0x1A), EVENT_MODE on transitions, CMD_UNBOND, REGISTRATION_INCOMPLETE resume, grace window
- [x] NVS schema: `imb_state` namespace (op_mode, pending_uids) — HAL-injected, not yet wired to real NVS in main.c
- [x] Integration: `main.c` wires PN532 → imb_detector → imb_session + imb_buzzer + imb_led + imb_ble_session; app callbacks on_set_pin + on_mode_set + on_box_rename implemented
- [x] App-side changes documented in `docs/app-ble-changes.md` (9 sections, source of truth: `docs/ble-contract.md`)

### Remaining BLE / integration tasks

#### ✅ Done (2026-06-08, this session)
- [x] Wire real NVS HAL into `imb_ble_session_init` — `imb_state` op_mode + pending_uids now persisted
- [x] Load `pin_hash` + `box_name` from `imb_identity` NVS on boot — provisioning survives reboot
- [x] Persist `pin_hash` + `box_name` in `app_on_set_pin` and `app_on_box_rename`
- [x] Initial advertisement reflects persisted state (SETUP vs FIELD_CHECK, correct pin_hash + name)
- [x] BLE debug logging: enc_change, repeat_pairing handler, pin_hash compare, NOT_AUTHED gate, HELLO timeout

#### Item Registration — `feat/item-registration` (current)

**Flow:** NFC scan detects tag → box sends `EVENT_TAG { uid, name="" }` → phone prompts user → phone sends `CMD_NAME { uid, name }` → box writes NDEF + registers → `EVENT_ACK[OK]`

- [ ] **Instantiate `imb_registry`** in `main.c` with NVS HAL wired to `imb_local` namespace
- [ ] **Implement `on_name_tag`** callback: given `uid`, locate tag on reader 0 or 1, write NDEF text record via PN532, call `imb_ble_session_ack(msg_id, OK or NDEF_WRITE_FAILED)`
- [ ] **PN532 NDEF write path**: `InListPassiveTarget` to find tag → `TgNDEFWrite` (or block-write for MIFARE Classic) — reference: existing NDEF integration test in Phase 1 drivers
- [ ] **Registry lookup in `on_scan_event`**: query `imb_registry_get(uid)` and populate `pkt.name` so phone sees `name != ""` for already-registered tags (no re-prompt)
- [ ] **`imb_registry_add`** after successful NDEF write: persist `{ uid, name }` to `imb_local` NVS
- [ ] **`imb_ble_session_ack`** called on NDEF write result so pending_count decrements and phone gets `EVENT_ACK`
- [ ] **`CMD_MODE → FIELD_CHECK`** unblocked once all pending UIDs named (flows through existing session logic)

#### Remaining after item registration
- [ ] Wire `on_accept_tag` → `imb_registry` accept/reject (FIELD_CHECK foreign tag flow)
- [ ] Wire lid-close: `imb_box_activity` → `imb_inventory_state` finalization → BLE report delivery if pending
- [ ] REGISTRATION_INCOMPLETE lid-open-rescan recovery
- [ ] Factory reset: 10 s BOOT button hold → erase all four NVS namespaces + NimBLE bond store → reboot
- [ ] Deep sleep + Lid Trigger wake: GPIO4 wakes from deep sleep on lid-open HIGH

---

## Phase 1 — Phone Application (TDD, Mock-driven)

- [x] `IBleClient` interface + `MockBleClient` for host-agnostic testing
- [x] `BoxSessionProvider`: sequential naming queue + automatic retry logic
- [x] `RegistrationOverlay`: reactive naming modal flow
- [x] Registration Workflow: START/END flow with "Hard Gate" (blocked if unnamed exist)
- [x] Protocol Sync: generated Dart models from `imb_protocol.h`
- [ ] Mesh View UI: multi-box summary (next)

## Phase 2 — Per-Box OLED Display

Every box gets a 0.96" SSD1306 OLED (128×64, I2C GPIO 2/3). No navigation buttons — display is event-driven.
Local finalization decoupled from BLE: lid close → Field Check Report → persisted Box Inventory State. Screen displays Box Inventory State; BLE delivers pending Field Check Reports.

### `imb_display` logic component (host-testable, HAL-injected)
- [ ] Define `imb_display_state_t`: box_name, op_mode, mesh_peer_count (`IMB_DISPLAY_MESH_UNKNOWN` until Phase 3), phone_connected, last_event (direction + item name + type), report (missing items array + count)
- [ ] Implement display state machine: Idle → Event (5s takeover) → Idle; Idle → Report cycling (2s/item, MISSING only) → Idle; any state → Error (held until next event)
- [ ] Screen layouts: SETUP (mode + last-4-MAC), FIELD_CHECK idle (box name + mode + mesh + phone), REGISTRATION idle (mode + pending count), REGISTRATION_INCOMPLETE idle (error — held until resolved), detection event (direction + item name), UNKNOWN TAG (error), AMBIGUOUS (error), report cycling (MISSING N/total + item name)
- [ ] Host tests for state machine transitions

### `imb_display_ssd1306` HAL (on-device only)
- [ ] SSD1306 I2C init sequence (reference esp-idf-ssd1306 or u8g2)
- [ ] HAL struct: `draw_text(row, col, str)` + `clear()`
- [ ] Wire into `main.c`: feed `imb_display_state_t` from NFC events, lid-close report, BLE connection state

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
- [ ] On-device test structure: standalone ESP-IDF project per driver component (see CLAUDE.md) — `components/imb_nfc/test_hw` (driver-level) and `tools/box_scenario_test` (full NVS/session integration, in progress) are the first instances
