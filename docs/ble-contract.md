# BLE Contract — Phone App ↔ Box

**Status:** Locked design as of 2026-05-27. Phone team and firmware team build to this spec.
**Open items at end of doc.**

This document defines the BLE/GATT contract between the IMB phone app (client) and the IMB box (ESP32-S3 GATT server). Wire-level message struct definitions live in [`components/imb_protocol/include/imb_protocol.h`](../components/imb_protocol/include/imb_protocol.h) — this doc covers everything *around* those structs: discovery, connection, security, mode behaviors, error handling.

Written for a developer building the phone-side implementation. No prior project context assumed.

---

## 1. Discovery (advertisement layer)

Box broadcasts every ~100 ms while lid is open. Phone scans for boxes without connecting.

### 1.1 Advertisement payload (31-byte budget)

| Field | Bytes | Content |
|---|---|---|
| Flags | 3 | Mandatory: BR/EDR not supported, LE General Discoverable |
| Complete Local Name | ~14 | `IMB-<name>-<last4MAC>` (e.g. `IMB-kitchen-A3F1`) or `IMB-SETUP-<last4MAC>` if PIN not yet set |
| Manufacturer Specific Data | 8 | See below |

### 1.2 Manufacturer Specific Data layout

```c
struct __attribute__((packed)) {
    uint16_t company_id;   // 0xFFFF (test/internal — change when assigned NXP ID)
    uint32_t pin_hash;     // CRC32 of user-set PIN; 0 in SETUP mode
    uint8_t  op_mode;      // imb_op_mode_e: SETUP=0, FIELD_CHECK=1, REGISTRATION=2, REGISTRATION_INCOMPLETE=3
    uint8_t  flags;        // bit 0: has unread report; bit 1: registration paused (grace window)
};
```

### 1.3 Phone-side discovery flow

```pseudo
scanner.scan(filter: name_prefix == "IMB-")

on_scan_result(adv):
    if adv.name starts with "IMB-SETUP-":
        // unprovisioned box; prompt user to set up
        show_setup_card(adv)
    else if adv.mfg_data.pin_hash == my_mesh.pin_hash:
        // belongs to my mesh
        show_box_card(adv.name, adv.mfg_data.op_mode, adv.mfg_data.flags)
    else:
        // different mesh — hide from main UI
        ignore()
```

**Why filter on PIN hash:** prevents the user from accidentally connecting to a neighbor's box. The hash is not a security mechanism (32-bit CRC, trivially crackable) — it's a discovery filter.

---

## 2. GATT service structure

Phone connects to the box, discovers the IMB service, subscribes to the two NOTIFY characteristics, and writes commands.

### 2.1 UUIDs (TO BE GENERATED — placeholder shown)

All 128-bit. Share a base UUID; only last 16 bits differ.

```
Base UUID:        XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX  (TBD — generate once with uuidgen)

Service:          XXXXXXXX-XXXX-XXXX-XXXX-XXXX00000000  (last 16 bits = 0x0000)
  ├ EVENT_NOTIFY   XXXXXXXX-XXXX-XXXX-XXXX-XXXX00000001  (notify)
  ├ REPORT_NOTIFY  XXXXXXXX-XXXX-XXXX-XXXX-XXXX00000002  (notify)
  └ COMMAND_WRITE  XXXXXXXX-XXXX-XXXX-XXXX-XXXX00000003  (write-with-response)
```

UUIDs are **not** advertised — phone discovers them after connecting. Saves 18 bytes in the advertisement budget.

### 2.2 Characteristic semantics

| Characteristic | Direction | Type | Purpose |
|---|---|---|---|
| `EVENT_NOTIFY` | box → phone | Notify | Real-time tag events, mode changes, command ACKs, queued backlog on subscribe |
| `REPORT_NOTIFY` | box → phone | Notify | Fragmented delta report on lid close (REGISTRATION/FIELD_CHECK) |
| `COMMAND_WRITE` | phone → box | Write-With-Response | All phone-initiated commands |

**Write-With-Response** means the BLE stack confirms "your bytes arrived" at the link layer (automatic, ~ms). The application-layer result (PIN_MISMATCH, NDEF_WRITE_FAILED, etc.) comes back asynchronously on `EVENT_NOTIFY` as an `EVENT_ACK` packet.

---

## 3. Connection lifecycle

### 3.1 Two connection profiles

Box requests different connection parameters based on its operational mode. Phone can ignore (iOS does), but should set its own connection priority hint:

| Mode | Min interval | Max interval | Slave latency | Supervision timeout | Android priority hint |
|---|---|---|---|---|---|
| FIELD_CHECK | 15 ms | 30 ms | 0 | 2 s | `HIGH` |
| REGISTRATION | 100 ms | 200 ms | 4 | 6 s | `BALANCED` |

The box re-requests on mode transitions; phone team only needs to update Android priority hint on `EVENT_MODE` reception.

### 3.2 Connect sequence (phone-side contract)

Every connect, every time:

```pseudo
1. connect(box.address)
   // First-ever connect: OS pairing dialog appears (Just Works + LE Secure Connections).
   // User taps "Pair". Bond saved.
   // Subsequent connects: silent.

2. discover_service(IMB_SERVICE_UUID)

3. subscribe(EVENT_NOTIFY)
4. subscribe(REPORT_NOTIFY)
   // Both subscribes complete BEFORE step 5.
   // Box queues up to 8 events during connect→subscribe window; flushes on subscribe.

5. write_with_response(COMMAND_WRITE, CMD_HELLO { msg_id, pin_hash })

6. await EVENT_ACK[acked_msg_id == sent_msg_id] within 5 s
   - status=OK              → ready; UI may issue commands
   - status=PIN_MISMATCH    → box will disconnect; show "this box isn't in your mesh"
   - status=NOT_AUTHED      → shouldn't happen here; protocol bug
   - timeout (no ACK)       → disconnect, show retry prompt
```

### 3.3 Disconnect / reconnect

- Box advertises while lid is open, regardless of connection state.
- Single client only per box. Second concurrent connection attempt: rejected.
- If box's previous connection went stale (phone out of range), supervision timeout (2–6 s) cleans it up. Use phone-side backoff retry: `[1 s, 2 s, 4 s]`.
- Bonded reconnect: silent, no OS dialog. Still must send `CMD_HELLO` (defense in depth — PIN may have rotated since bond).

### 3.4 Mid-session disconnect during REGISTRATION

If phone disconnects while in REGISTRATION:
- Box keeps session state in RAM for a **60 s grace window**, advertising `flags.bit1 = 1` (registration paused).
- Phone reconnects within 60 s → box auto-resumes; re-fires pending EVENT_TAGs (the unnamed UIDs).
- Grace window expires AND no pending unnamed UIDs → box transitions to FIELD_CHECK.
- Grace window expires AND pending unnamed UIDs exist → box transitions to `REGISTRATION_INCOMPLETE` (persisted to NVS, survives reboot). See §6.4.

### 3.5 Reports and the FIELD_CHECK drive-by

```
1. Lid closes, delta is non-empty.
2. Box opens 30 s "report delivery window."
3. Phone connects (or is already connected): box fragments REPORT into chunks (see §4.3).
4. Phone ACKs each chunk via CMD_REPORT_ACK (or CMD_REPORT_NACK to request resend).
5. Once all chunks ACKed, box sleeps. If window expires with un-ACKed chunks: box persists report to NVS, sleeps, retries on next session.
```

If the delta is empty (nothing changed since last check), the box stays silent — no advertisement post-close, no notification. **No alert fatigue.**

---

## 4. Wire protocol (recap + additions)

All wire-level message struct definitions live in [`components/imb_protocol/include/imb_protocol.h`](../components/imb_protocol/include/imb_protocol.h). 

### 4.1 Consuming the Protocol (Phone Side)

The phone app MUST NOT manually redefine these structs. Instead:
1.  **Add Submodule**: Add this repository as a submodule to your project.
    `git submodule add https://github.com/your-org/inventoryMeshBoxs.git protocol`
2.  **Generate Models**: Use the provided script (see phone repo `scripts/gen_protocol.py`) to generate Dart/Kotlin classes directly from the C headers.
3.  **Sync**: When the protocol changes, run `git submodule update --remote` and re-run the generator.

### 4.2 Message types

| Type byte | Name | Direction | Notes |
|---|---|---|---|
| 0x01 | `EVENT_TAG` | box → phone | Tag insert/extract/ambiguous |
| 0x02 | `EVENT_MODE` | box → phone | Mode transitioned |
| 0x03 | `REPORT_CHUNK` **[NEW]** | box → phone | Fragmented delta report; replaces single-shot REPORT |
| 0x04 | `EVENT_ACK` **[NEW]** | box → phone | Application-level ACK to a CMD_* |
| 0x05 | `EVENT_DROPPED` **[NEW]** | box → phone | N events dropped during connect→subscribe window |
| 0x10 | `CMD_MODE` | phone → box | Set op_mode |
| 0x11 | `CMD_NAME` | phone → box | Assign name to a scanned UID; triggers NDEF write |
| 0x12 | `CMD_ACCEPT` | phone → box | Accept (1) or reject (0) a foreign tag |
| 0x13 | `CMD_HELLO` **[NEW]** | phone → box | Mandatory first message; carries pin_hash |
| 0x14 | `CMD_SET_PIN` **[NEW]** | phone → box | First-time PIN provisioning (SETUP mode only) |
| 0x15 | `CMD_REPORT_ACK` **[NEW]** | phone → box | Acknowledge receipt of a report chunk |
| 0x16 | `CMD_REPORT_NACK` **[NEW]** | phone → box | Request resend of a specific report chunk |
| 0x17 | `CMD_UNBOND` **[NEW]** | phone → box | Erase this phone's bond from box (optional clean-disconnect) |

### 4.2 Common command header

Every CMD_* message starts with this 2-byte header. The `msg_id` is set by the phone, monotonic with 8-bit wraparound. Box echoes it in the corresponding EVENT_ACK.

```c
typedef struct __attribute__((packed)) {
    uint8_t msg_type;
    uint8_t msg_id;       // phone-set; 0..255, monotonic, wraps
    // ...command-specific fields follow
} imb_cmd_header_t;
```

### 4.3 Report fragmentation

A REPORT can have up to 512 entries × 50 bytes = 25 KB. Far over BLE MTU. We fragment app-side:

```c
typedef struct __attribute__((packed)) {
    uint8_t  msg_type;          // IMB_MSG_REPORT_CHUNK = 0x03
    uint16_t report_id;         // monotonic; identifies the report
    uint16_t chunk_index;       // 0-based
    uint16_t chunk_total;       // total chunks in this report
    uint16_t entries_in_chunk;
    imb_pkt_report_entry_t entries[];  // up to 4 at MTU=247
} imb_pkt_report_chunk_t;
```

- MTU negotiated to **247** on connect (box requests; both stacks generally honor).
- Chunk header = 9 bytes. Payload = MTU(247) − ATT_header(3) − chunk_header(9) = **235 bytes → 4 entries per chunk**.
- Worst case Phase 1 (64 entries) = 16 chunks; mesh-wide (512 entries) = 128 chunks. ~10 ms per chunk → 1.3 s worst case.
- Phone reassembles by `report_id`. If a chunk is missing (gap in `chunk_index`), phone sends `CMD_REPORT_NACK { report_id, missing_index }`; box resends that single chunk. Phone ACKs whole report with `CMD_REPORT_ACK { report_id }` after `chunk_total` chunks received.

### 4.4 EVENT_ACK structure (the universal command reply)

```c
typedef struct __attribute__((packed)) {
    uint8_t msg_type;        // 0x04 (IMB_MSG_EVENT_ACK)
    uint8_t acked_msg_id;    // matches CMD's msg_id
    uint8_t acked_msg_type;  // safety check (should match what phone sent)
    uint8_t status;          // imb_ack_status_e
} imb_pkt_event_ack_t;
```

#### ACK status codes

| Status | Code | Meaning | Phone action |
|---|---|---|---|
| `OK` | 0 | Command succeeded | Update local state |
| `PIN_MISMATCH` | 1 | (CMD_HELLO) PIN hash didn't match | Box disconnects; show "wrong mesh" |
| `REGISTRY_FULL` | 2 | (CMD_ACCEPT) Box at IMB_REGISTRY_MAX_ITEMS=64 | Show "box is full" |
| `NDEF_WRITE_FAILED` | 3 | (CMD_NAME) PN532 failed to write tag | Show "hold steady, retry"; retry same `msg_id` |
| `INVALID_MODE` | 4 | (CMD_MODE) Illegal mode transition | Show specific message based on current/target mode |
| `UNKNOWN_UID` | 5 | (CMD_NAME / CMD_ACCEPT) UID not in pending set | Silently drop (race condition) |
| `NOT_AUTHED` | 6 | Any command before CMD_HELLO | Phone-side bug; reconnect and retry |
| `REGISTRATION_INCOMPLETE` | 7 | (CMD_MODE→FIELD_CHECK) Pending unnamed UIDs exist | Show "name all tags or remove from box" |

### 4.5 Phone command helper (pseudocode)

Every CMD_* call from the phone follows this pattern:

```pseudo
async fn send_cmd(cmd_bytes, expected_ack_msg_type, timeout = 2s) -> AckStatus {
    let id = next_msg_id()
    cmd_bytes[1] = id                          // set msg_id at offset 1
    let ack_future = wait_for_event_ack(id, expected_ack_msg_type)
    write_with_response(COMMAND_WRITE, cmd_bytes)
    return await(ack_future, timeout) || AckStatus.Timeout
}
```

CMD_HELLO timeout = 5 s (covers slow first-time pairing). All others = 2 s. CMD_NAME may need 3 s (NDEF write to tag is slow on bad RF coupling).

---

## 5. Security model

### 5.1 Layers

| Layer | Mechanism | Purpose |
|---|---|---|
| 1. Discovery filter | PIN hash in mfg-data | Reduce accidental cross-mesh connection attempts |
| 2. Link encryption | LE Secure Connections, Just Works pairing + bonding | Prevent passive sniffing |
| 3. Application auth | CMD_HELLO with pin_hash | Reject unauthorized phones even if they reach GATT |

### 5.2 What this defends against

- **Passive sniffer in radio range:** link is encrypted ✓
- **Neighbor's phone running IMB app accidentally connecting:** PIN-hash filter + CMD_HELLO gate ✓
- **Malicious app on neighbor's phone (knows your mesh exists, bypasses ADV filter):** CMD_HELLO gate ✓

### 5.3 What this does NOT defend against (Phase 1)

- **Determined attacker with custom firmware:** Just Works pairing is breakable. Phase 2 master box adds Passkey via OLED.
- **Stolen bonded phone:** holder has full access until user issues CMD_UNBOND (or factory-resets the box).

### 5.4 PIN

- 4–8 digit PIN, user-set. Stored as `pin_hash = crc32(PIN)` only — raw PIN never persisted on box.
- Phone retains the raw PIN in its secure storage (keychain/keystore) so it can derive `pin_hash` for CMD_HELLO.
- PIN rotation: phone sends `CMD_SET_PIN` in SETUP mode only. After SETUP, PIN can be rotated by factory-reset → re-pair → CMD_SET_PIN.

---

## 6. Operational mode state machine

### 6.1 States

| Mode | Value | Description |
|---|---|---|
| `SETUP` | 0 | Brand new or factory-reset box; no PIN; accepts CMD_SET_PIN |
| `FIELD_CHECK` | 1 | Default operating mode; lid-open scan → delta report on lid-close |
| `REGISTRATION` | 2 | Active interactive session for naming new tags |
| `REGISTRATION_INCOMPLETE` | 3 | Sticky state: unnamed tags exist; cannot leave until resolved |

### 6.2 Transitions

```
[SETUP] ──CMD_SET_PIN──▶ [FIELD_CHECK]

[FIELD_CHECK] ──CMD_MODE:REGISTRATION──▶ [REGISTRATION]

[REGISTRATION] ──CMD_MODE:FIELD_CHECK, pending empty──▶ [FIELD_CHECK]
[REGISTRATION] ──CMD_MODE:FIELD_CHECK, pending non-empty──▶ stays in [REGISTRATION]
                                                          (ACK: REGISTRATION_INCOMPLETE)

[REGISTRATION] ──phone disconnects, grace expires, pending empty──▶ [FIELD_CHECK]
[REGISTRATION] ──phone disconnects, grace expires, pending non-empty──▶ [REGISTRATION_INCOMPLETE]

[REGISTRATION_INCOMPLETE] ──phone reconnects──▶ [REGISTRATION] (resume; replay pending EVENT_TAGs)
[REGISTRATION_INCOMPLETE] ──lid-open rescan, all pending UIDs absent──▶ [FIELD_CHECK]
[REGISTRATION_INCOMPLETE] ──reboot──▶ [REGISTRATION_INCOMPLETE] (sticky, persisted in NVS)
```

### 6.3 EVENT_TAG semantics by mode

| Mode | Empty name (`name[0]==0`) | Non-empty name |
|---|---|---|
| REGISTRATION | New tag — phone should prompt user to name | Already-registered tag was rescanned; no prompt |
| REGISTRATION_INCOMPLETE | (replay only) Previously-pending tag re-emitted on resume | (replay only) |
| FIELD_CHECK | Foreign tag — phone may prompt accept/reject | Real-time insert/extract animation |

### 6.4 REGISTRATION_INCOMPLETE recovery flow (phone UX)

1. Phone scans, sees box advertising with `mfg_data.op_mode == REGISTRATION_INCOMPLETE`
2. Phone displays prominent alert: "Box has N unnamed tags from a previous registration. Resume?"
3. User taps "Resume" → phone connects, runs §3.2 connect sequence
4. Box auto-resumes registration, re-fires EVENT_TAG for each pending UID (drained from queue on subscribe)
5. User names tags as in normal registration; each successful CMD_NAME drops one from pending
6. Once pending empty, user sends CMD_MODE: FIELD_CHECK → succeeds

**Alternative recovery:** user physically removes the unnamed tags from the box, closes lid, waits. Next lid-open rescan detects the missing UIDs in the pending set, drops them, auto-transitions REGISTRATION_INCOMPLETE → FIELD_CHECK, fires EVENT_MODE to any connected phone.

---

## 7. Setup, registration, and field-check flows (end-to-end)

### 7.1 First-time setup

```
1. User unboxes hardware, powers on
2. Box boots, op_mode = SETUP, advertises as IMB-SETUP-A3F1
3. Phone app scans, sees IMB-SETUP-*, prompts user
4. User taps "Set up this box," enters chosen PIN
5. Phone computes pin_hash = crc32(pin), connects to box
6. OS pairing dialog ("Pair with IMB-SETUP-A3F1?") → user confirms → bond saved
7. Phone subscribes to NOTIFY characteristics
8. Phone sends CMD_SET_PIN { msg_id, pin_hash, box_name }
9. Box persists pin_hash + box_name to NVS, transitions to FIELD_CHECK
10. Box returns EVENT_ACK[CMD_SET_PIN, OK]
11. Box fires EVENT_MODE [FIELD_CHECK]
12. Box re-advertises as IMB-<box_name>-A3F1 with updated mfg-data
13. Phone disconnects, shows "Box is ready"
```

### 7.2 Registration session

```
1. User opens box (lid open), phone scans, connects, sends CMD_HELLO, gets OK
2. User taps "Start registration" → phone sends CMD_MODE: REGISTRATION
3. Box returns EVENT_ACK[CMD_MODE, OK], fires EVENT_MODE [REGISTRATION]
4. Box re-requests slow connection params (100–200 ms interval, latency=4)
5. User drops a tag in box
6. Box detects insert via PN532 directional scan, no NDEF name found
   → Box fires EVENT_TAG { direction=INSERT, uid, name="" }
   → Box adds UID to pending set, persists pending_uids to NVS
7. Phone shows "Name this tag" dialog
8. User types "flashlight" → phone sends CMD_NAME { msg_id, uid, name="flashlight" }
9. Box writes NDEF text record to physical tag via PN532
   - Success: box adds {uid, "flashlight"} to imb_local registry, removes from pending, NVS persist
     → EVENT_ACK[CMD_NAME, OK]
   - Failure: tag stays in pending; phone may retry
     → EVENT_ACK[CMD_NAME, NDEF_WRITE_FAILED]
10. User continues with more tags; loop 5–9
11. User taps "End registration" → phone sends CMD_MODE: FIELD_CHECK
    - All tags named: EVENT_ACK[OK], box transitions, fires EVENT_MODE [FIELD_CHECK]
    - Pending tags exist: EVENT_ACK[REGISTRATION_INCOMPLETE], phone shows error, user names them
```

### 7.3 Field-check drive-by

```
1. User opens box at home (lid open), phone in pocket
2. Box wakes from deep sleep on lid-open, starts advertising, both PN532s polling
3. Phone (background scan) sees box, auto-connects in background
4. Phone subscribes, sends CMD_HELLO, gets OK
5. Phone may receive EVENT_TAG for each insert/extract while user shuffles items (advisory; ignored if app not foregrounded)
6. User closes lid
7. Box stops PN532, computes delta = (session items vs imb_local registry)
   → PRESENT (matches registry), MISSING (registered but absent), FOREIGN (unregistered), AMBIGUOUS (scan order unclear)
8. Delta empty: box sleeps silently
9. Delta non-empty:
   a. Box generates REPORT_CHUNK[0], sends via REPORT_NOTIFY
   b. Phone may NACK any missing chunk: CMD_REPORT_NACK { report_id, missing_index } → box resends
   c. Phone sends CMD_REPORT_ACK { report_id } once all chunk_total chunks received
   d. Box deletes saved-report-pending flag from NVS, sleeps
10. If phone wasn't around within 30 s post-close: box persists report to NVS, sleeps
    → next session, on connect, box re-sends the saved report before processing new events
```

---

## 8. Factory reset

### 8.1 Trigger

**Long-press BOOT button (GPIO 0) for 10 s** while box is powered on. LED feedback: slow red breathing during hold, fast red flash + reboot at 10 s mark.

### 8.2 Effect

- Erases all four IMB NVS namespaces (`imb_identity`, `imb_local`, `imb_mesh`, `imb_state`)
- Clears NimBLE bonding store
- Reboots
- New state: SETUP mode, no PIN, no bonds, advertises as `IMB-SETUP-<last4MAC>`

### 8.3 Phone-side handling of factory-reset boxes

```pseudo
on_connect_failure(box, error):
    if error is pairing_rejected AND we have a bond for this address:
        // Box was factory-reset; our bond is stale
        delete_local_bond(box.address)
        prompt_user("This box was reset. Re-pair?")
        on confirm: reconnect (triggers fresh OS pairing dialog)
```

### 8.4 CMD_UNBOND (optional clean disconnect)

Phone can send `CMD_UNBOND { msg_id }` before disconnecting to ask the box to erase *this phone's* bond specifically. Useful when user does "Forget this box" in the app.

- Box deletes the bond from NimBLE store, returns `EVENT_ACK[CMD_UNBOND, OK]`, disconnects.
- Other bonds (other phones in the same mesh) are unaffected.
- Box stays in same op_mode, keeps PIN.

---

## 9. Edge cases and error handling

### 9.1 Phone-side handling matrix

| Situation | Detection | Phone action |
|---|---|---|
| Box busy (single-client) | Connection fails immediately | Retry with backoff `[1s, 2s, 4s]`; show "box busy" after final fail |
| Pairing rejected (factory-reset) | Pairing error during connect | Delete local bond, prompt to re-pair |
| PIN mismatch | EVENT_ACK[CMD_HELLO, PIN_MISMATCH] + disconnect | Show "wrong mesh"; offer to switch mesh in settings |
| HELLO timeout | No EVENT_ACK within 5 s | Disconnect; show "box unresponsive, retry?" |
| NDEF write failed | EVENT_ACK[CMD_NAME, NDEF_WRITE_FAILED] | Show "hold steady, retry"; retry same msg_id |
| Registration incomplete on mode change | EVENT_ACK[CMD_MODE, REGISTRATION_INCOMPLETE] | Show "N tags need names" with list |
| Report chunk gap | `chunk_index` skip in received stream | Send CMD_REPORT_NACK for missing index |
| Report timeout | < `chunk_total` chunks within 5 s of first chunk | Disconnect, retry connection; box will re-send saved report |
| Lost connection mid-session | OS disconnect callback | Show "reconnecting…"; auto-retry once; user can manually retry |
| EVENT_DROPPED received | EVENT_DROPPED on subscribe flush | Show "N events missed" toast; rely on REPORT for truth |

### 9.2 Box-side guarantees the phone team can rely on

1. **CMD_HELLO is mandatory:** box silently ignores any other CMD before HELLO, returns `NOT_AUTHED` if a CMD is somehow processed without prior HELLO.
2. **`msg_id` echo is exact:** EVENT_ACK's `acked_msg_id` is the bytes from the original CMD's `msg_id`. Use this for correlation, never timing.
3. **NDEF write is the commit point:** tag is in NVS registry IFF its NDEF text record has been successfully written to the physical tag.
4. **REGISTRATION_INCOMPLETE is sticky:** survives reboot. Only way out is to name pending or physically remove pending UIDs.
5. **REPORT_NOTIFY is authoritative; EVENT_NOTIFY is advisory:** if you missed events, REPORT will tell you the truth.
6. **8-event RAM queue on box** for the connect→subscribe→authed window. If you take more than ~1 s between connect and subscribe, you may see EVENT_DROPPED.

### 9.3 Timeouts summary

| Timeout | Duration | Owner | Trigger / Action |
|---|---|---|---|
| HELLO ACK | 5 s | Phone | If no ACK, disconnect + retry |
| Generic CMD ACK | 2 s | Phone | If no ACK, surface error to user |
| NDEF CMD ACK | 3 s | Phone | NDEF writes are slow; longer tolerance |
| Report chunk stream | 5 s after first chunk | Phone | If incomplete, disconnect + reconnect |
| Box supervision (FIELD_CHECK) | 2 s | Box → BLE stack | Stale connection cleanup |
| Box supervision (REGISTRATION) | 6 s | Box → BLE stack | Tolerate brief phone outages |
| Registration grace window | 60 s | Box | Phone reconnect window before INCOMPLETE/FIELD_CHECK |
| Report delivery window | 30 s | Box | After lid close, wait for phone before persisting report |

---

## 10. Open items (not yet locked)

- **UUIDs:** generate base UUID + 4 derived. Run `uuidgen` once, paste into both codebases.
- **Mfg-data company_id:** currently `0xFFFF` (test/internal). If commercialized, request NXP Bluetooth SIG company ID.
- **CCCD persistence across bonds:** ESP-IDF NimBLE bonding stores CCCD state automatically. Verify on first end-to-end test that a bonded reconnect doesn't require re-subscribing.
- **Phase 2 master box:** Passkey pairing replaces Just Works once display is available.
- **Phase 3 mesh:** consolidated REPORT_NOTIFY semantics (multiple boxes → one report via master); cross-box transaction broadcast (out of scope for this contract).
- **Possible Phase 3+ pivot to commercial / large-inventory:** if pivoting to warehouse-style multi-staff use, single-client-per-box constraint may need to become multi-client-per-mesh. The mesh layer would handle authorization; per-box auth stays single-client.

---

## 11. Quick reference card

```
SCAN:    name prefix "IMB-", parse mfg-data (company_id=0xFFFF, pin_hash, op_mode, flags)
CONNECT: pair (Just Works) → subscribe EVENT_NOTIFY → subscribe REPORT_NOTIFY → CMD_HELLO
COMMAND: write-with-response on COMMAND_WRITE; await EVENT_ACK matching msg_id
REPORT:  reassemble REPORT_CHUNK by report_id; NACK gaps; ACK whole report
RESET:   factory-reset by long-press BOOT 10s (hardware-only)
```
