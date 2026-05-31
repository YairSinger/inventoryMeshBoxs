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
- **Power matters**: both PN532s wired to the same ESP 3V3 pin produced dirty bus. Splitting power across two 3V3 pins required.
- **Jumpers corrected (2026-05-27)**: SW1→I0, SW2→I1. PN532 Table 44: I1=1,I0=0 = SPI. Therefore SPI = SW1=0V (I0=0), SW2=3.3V (I1=1). Earlier "correction" to both-0V was wrong — that is HSU/UART mode. Original dev-setup note (SW2=HIGH for SPI) was correct.
- **Bit order**: PN532 is natively LSB-first. These boards have NO bit-reversal circuit. ESP-IDF `SPI_DEVICE_BIT_LSBFIRST` did NOT work (all-0x00 status). Bit-bang LSB-first is the working approach.
- **ESP-IDF hardware SPI does not work** for these chips — use bit-bang (see `main/main.c`). Root cause unclear (GPIO sleep isolation or DMA alignment suspected).
- **Do NOT poll STATREAD before sending a command** — it will always be 0x00. Correct flow: send command → poll → read ACK → poll → read response.
- **PN532 needs 2s boot delay** and chips must be **power-cycled** (disconnect 3V3) between ESP resets for consistent behavior.

### Last state (2026-05-27) ✅ RESOLVED
- Both PN532s confirmed: `IC=0x32 Ver=1 Rev=6 Support=0x07`
- Root cause of all prior failures: SW2 must be HIGH (3.3V) for SPI mode; both-0V = HSU/UART
- Firmware: bit-bang LSB-first, MISO pull-up, wakeup pulse (`main/main.c`)
- **Next**: read NTAG213 UID via `InListPassiveTarget` on each reader

### Tasks
- [x] PN532 #1 (CS GPIO10): `GetFirmwareVersion` → `IC=0x32 Ver=1 Rev=6 Support=0x07` ✅ (2026-05-27)
- [x] PN532 #2 (CS GPIO9): `GetFirmwareVersion` → `IC=0x32 Ver=1 Rev=6 Support=0x07` ✅ (2026-05-27)
- [ ] PN532 #1 + #2: read NTAG213 UID via `InListPassiveTarget` — **IN PROGRESS** (firmware flashed, awaiting tag test)
- [ ] WS2812B RMT driver: cycles through LED color contract (GPIO 48)
- [ ] Deep sleep + BOOT button wake: GPIO 0 wakes from deep sleep
- [ ] NVS driver: write + read + erase `imb_local` namespace
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

- [ ] PN532 #1 SPI driver: reads NTAG213 UID (CS GPIO 10, inner reader)
- [ ] PN532 #2 SPI driver: reads NTAG213 UID (CS GPIO 9, outer reader)
- [ ] PN532 NDEF write: writes text record to tag via `InDataExchange`
- [ ] WS2812B RMT driver: cycles through LED color contract (GPIO 48)
- [ ] Deep sleep + BOOT button wake: GPIO 0 wakes from deep sleep
- [ ] NVS driver: write + read + erase `imb_local` namespace
- [ ] BLE GATT server: phone connects, subscribes to notify chars, writes COMMAND_WRITE

## Phase 1 — BLE Contract (locked 2026-05-27)

**Source of truth:** [`docs/ble-contract.md`](docs/ble-contract.md) — full GATT/transport spec for phone team.

### Wire-protocol additions to `imb_protocol`
- [ ] Add `IMB_MSG_REPORT_CHUNK` (replaces single-shot REPORT; fragmented w/ report_id + chunk_index)
- [ ] Add `IMB_MSG_EVENT_ACK` (universal CMD reply with msg_id echo + status code)
- [ ] Add `IMB_MSG_EVENT_DROPPED` (signals queue overflow during connect→subscribe window)
- [ ] Add `IMB_MSG_CMD_HELLO` (mandatory first message; carries pin_hash)
- [ ] Add `IMB_MSG_CMD_SET_PIN` (SETUP mode provisioning)
- [ ] Add `IMB_MSG_CMD_REPORT_ACK` / `IMB_MSG_CMD_REPORT_NACK` (per-chunk ack flow)
- [ ] Add `IMB_MSG_CMD_UNBOND` (optional clean disconnect)
- [ ] Add `msg_id` byte to every CMD_* (at offset 1); update existing pack/unpack
- [ ] Add `imb_ack_status_e` enum (8 values: OK, PIN_MISMATCH, REGISTRY_FULL, NDEF_WRITE_FAILED, INVALID_MODE, UNKNOWN_UID, NOT_AUTHED, REGISTRATION_INCOMPLETE)
- [ ] Update host tests for all new messages + round-trip

### Types additions
- [ ] Add `IMB_MODE_REGISTRATION_INCOMPLETE = 3` to `imb_op_mode_e`
- [ ] Add `IMB_EVENT_QUEUE_DEPTH = 8` constant

### NVS schema additions (`imb_state` namespace)
- [ ] `pending_count` uint8
- [ ] `pending_uids` packed array (up to 64 × 15 bytes)
- [ ] `pending_epoch` uint32

### BLE driver layer (new component: `imb_ble`)
- [ ] Generate base UUID + 4 derived (one-time `uuidgen`); paste in `docs/ble-contract.md` §2.1 and code
- [ ] NimBLE GATT server: 1 service, 3 characteristics (EVENT_NOTIFY, REPORT_NOTIFY notify; COMMAND_WRITE write-with-response)
- [ ] Advertisement: `IMB-<name>-<last4MAC>` + mfg-data (company_id, pin_hash, op_mode, flags)
- [ ] MTU negotiation to 247 on connect
- [ ] Connection parameter profiles: FIELD_CHECK fast (15–30ms, lat=0, sup=2s) / REGISTRATION slow (100–200ms, lat=4, sup=6s); re-request on mode change
- [ ] Just Works pairing + LE Secure Connections + bonding (NimBLE bonding store in NVS)
- [ ] 8-event RAM queue for connect→subscribe window; flush on second CCCD subscribe
- [ ] CMD_HELLO gate: reject all CMDs before HELLO with NOT_AUTHED; 5s HELLO timeout → disconnect
- [ ] Single-client enforcement (reject second concurrent connection)
- [ ] CMD_UNBOND handler: delete bond, ACK, disconnect

### Integration (after drivers up)
- [ ] Wire `imb_detector` → `imb_session` in main loop
- [ ] Wire `imb_session` + `imb_registry` → `imb_delta` on lid close
- [ ] Wire `imb_delta` → REPORT chunking → REPORT_NOTIFY
- [ ] Report ACK/NACK loop with NVS-backed retry on incomplete delivery
- [ ] REGISTRATION flow with pending_uids NVS persistence
- [ ] REGISTRATION_INCOMPLETE sticky state + lid-open-rescan recovery
- [ ] Factory reset: 10s BOOT button hold → erase all imb_* NVS namespaces + NimBLE store → reboot
- [ ] Mode persisted in NVS (`imb_state.op_mode`) survives reboot

---

## Phase 1 — Phone Application (TDD, Mock-driven)

- [x] `IBleClient` interface + `MockBleClient` for host-agnostic testing
- [x] `BoxSessionProvider`: sequential naming queue + automatic retry logic
- [x] `RegistrationOverlay`: reactive naming modal flow
- [x] Registration Workflow: START/END flow with "Hard Gate" (blocked if unnamed exist)
- [x] Protocol Sync: generated Dart models from `imb_protocol.h`
- [ ] Mesh View UI: multi-box summary (next)

## Phase 2 — Master Box (current branch: `feat/phase2-master-box`)

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
n-device test structure: standalone ESP-IDF project per driver component (see CLAUDE.md)
