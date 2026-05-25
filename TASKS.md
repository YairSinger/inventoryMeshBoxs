# Project Tasks

## Phase 0 — Hello World (hardware bringup)
> Smoke-test every physical component before implementing real logic.

- [ ] Scaffold hello world `app_main`: BOOT button wakes from deep sleep, WS2812B cycles colors, BLE advertises, two PN532s detected on SPI, NVS read/write round-trip
- [ ] Verify GPIO pin assignments on real board (see CLAUDE.md for map)

## Phase 1 — Single Box Logic (TDD, host-testable)

### `imb_detector` — directional NFC scan detection
- [x] Reader 0 → Reader 1 within 200ms → INSERT
- [x] Reader 1 → Reader 0 within 200ms → EXTRACT
- [x] Only Reader 0, window expires → AMBIGUOUS
- [x] Only Reader 1, window expires → AMBIGUOUS
- [x] Second reader fires after window → AMBIGUOUS + new pending starts

### `imb_registry` — local item CRUD over NVS HAL
- [ ] Add item (uid + name) → can retrieve by uid
- [ ] Add duplicate uid → overwrites, count unchanged
- [ ] Remove item → no longer retrievable
- [ ] Get all items → returns full list
- [ ] Registry survives simulated NVS reboot (HAL stub persists state)

### `imb_session` — cumulative event log during lid-open
- [ ] INSERT event → item added to present set
- [ ] EXTRACT event → item removed from present set
- [ ] AMBIGUOUS event → item added to ambiguous set, not present set
- [ ] Same item INSERT then EXTRACT → not in present set
- [ ] Session reset → all sets cleared

### `imb_delta` — diff session state against registry
- [ ] Item in registry + in session present → PRESENT
- [ ] Item in registry + not in session → MISSING
- [ ] Item in session + not in registry → FOREIGN
- [ ] Item in session ambiguous set → AMBIGUOUS in report
- [ ] Empty session against non-empty registry → all MISSING

### `imb_protocol` — BLE binary payload pack/unpack
- [ ] Pack EVENT_NOTIFY (insert/extract/ambiguous) → correct bytes
- [ ] Pack REPORT_NOTIFY (delta report) → correct bytes
- [ ] Unpack COMMAND_WRITE (mode command) → correct struct
- [ ] Unpack COMMAND_WRITE (tag name assignment) → correct struct
- [ ] Round-trip: pack → unpack → same data

## Phase 1 — Single Box Drivers (on-device, flash required)

- [ ] PN532 #1 SPI driver: reads NTAG213 UID
- [ ] PN532 #2 SPI driver: reads NTAG213 UID
- [ ] PN532 NDEF write: writes text record to tag
- [ ] WS2812B RMT driver: cycles through LED color contract
- [ ] Deep sleep + BOOT button wake: GPIO0 wakes from deep sleep
- [ ] NVS driver: write + read + erase `imb_local` namespace
- [ ] BLE GATT server: phone can connect, subscribe to notify characteristics, write to COMMAND_WRITE

## Phase 1 — Integration (single box, no mesh)

- [ ] Wire `imb_detector` → `imb_session` in main loop
- [ ] Wire `imb_session` + `imb_registry` → `imb_delta` on lid close
- [ ] Wire `imb_delta` → `imb_protocol` → BLE REPORT_NOTIFY
- [ ] Registration mode: phone sends START_REGISTRATION → box enters PROVISION_WRITE mode
- [ ] Registration gate: unnamed tag detected → BLE alert → phone sends name → box writes NDEF → stored in registry
- [ ] Field check: lid close → delta computed → REPORT_NOTIFY fires only if changed
- [ ] Accept/reject: post-registration unknown tag → BLE alert → phone accept → added to registry
- [ ] Mode persisted in NVS: survives reboot

## Phase 2 — Master Box (future)
- [ ] OLED SSD1306 driver (I2C)
- [ ] 3-button navigation (UP / SELECT / BACK)
- [ ] Master box UI: registration flow, field check report display

## Phase 3 — Mesh (future)
- [ ] ESP-Mesh join / leave
- [ ] Transaction log broadcast + idempotent apply
- [ ] Cross-box item migration
- [ ] Mesh registry sync on epoch mismatch
- [ ] PIN-based mesh identity + phone join approval
- [ ] Consolidated mesh BLE report

---

## Testing infrastructure
- [x] Host test runner: plain Makefile + vendored Unity (zero ESP-IDF dep)
- [x] Host test structure documented in CLAUDE.md
- [x] Dev-setup skill: `.claude/skills/dev-setup.md`
- [ ] On-device test structure: standalone ESP-IDF project per driver component (see CLAUDE.md)
