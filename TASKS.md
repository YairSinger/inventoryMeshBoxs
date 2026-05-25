# Project Tasks

> **Current branch:** `feat/phase2-master-box`
> **Context snapshot:** Phase 1 logic layer complete (25/25 tests). Phase 1 drivers + integration remain before Phase 2 begins.

---

## Phase 0 — Hello World (hardware bringup)
> Smoke-test every physical component before implementing real logic.

- [ ] Scaffold hello world `app_main`: BOOT button wakes from deep sleep, WS2812B cycles colors, BLE advertises, two PN532s detected on SPI, NVS read/write round-trip
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

- [ ] PN532 #1 SPI driver: reads NTAG213 UID (CS GPIO 10, inner reader)
- [ ] PN532 #2 SPI driver: reads NTAG213 UID (CS GPIO 9, outer reader)
- [ ] PN532 NDEF write: writes text record to tag via `InDataExchange`
- [ ] WS2812B RMT driver: cycles through LED color contract (GPIO 48)
- [ ] Deep sleep + BOOT button wake: GPIO 0 wakes from deep sleep
- [ ] NVS driver: write + read + erase `imb_local` namespace
- [ ] BLE GATT server: phone connects, subscribes to notify chars, writes COMMAND_WRITE

## Phase 1 — Integration (single box, no mesh)

- [ ] Wire `imb_detector` → `imb_session` in main loop
- [ ] Wire `imb_session` + `imb_registry` → `imb_delta` on lid close
- [ ] Wire `imb_delta` → `imb_protocol` → BLE REPORT_NOTIFY
- [ ] Registration mode: phone sends START_REGISTRATION → box enters REGISTRATION mode
- [ ] Registration gate: unnamed tag → BLE alert → phone sends name → NDEF write → registry
- [ ] Field check: lid close → delta → REPORT_NOTIFY fires only if changed since last check
- [ ] Accept/reject: post-registration foreign tag → BLE alert → phone accept → registry
- [ ] Mode persisted in NVS (`imb_state` namespace `op_mode` key): survives reboot

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
