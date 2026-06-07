# App-Side BLE Changes Required

**Firmware contract source of truth:** `/Users/yair/git/inventoryMeshBoxs/docs/ble-contract.md`

This document lists every change the phone app must make to stay aligned with the firmware after the contract-compliance fixes applied on 2026-06-07.

---

## 1. Subscribe BOTH characteristics before CMD_HELLO

**What changed:** The firmware now fires the queue-flush (and pending-UID replay) only after *both* `EVENT_NOTIFY` and `REPORT_NOTIFY` CCCDs are enabled, not after just `EVENT_NOTIFY`.

**Required app change:** Ensure the connect sequence subscribes both characteristics and waits for both write-with-response confirmations before sending `CMD_HELLO`. If the app already does this, no change needed. If it sends `CMD_HELLO` immediately after the first subscribe, it may miss the queue-flush.

---

## 2. CMD_SET_PIN now returns EVENT_ACK + EVENT_MODE

**What changed:** `CMD_SET_PIN` (0x14) was previously a no-op on the firmware side. It now:
1. Returns `EVENT_ACK { acked_msg_type=0x14, status=OK }` on success
2. Returns `EVENT_ACK { acked_msg_type=0x14, status=INVALID_MODE }` if not in SETUP mode
3. Fires `EVENT_MODE { mode=FIELD_CHECK }` immediately after the ACK
4. Re-advertises as `IMB-<box_name>-<last4MAC>` with `pin_hash` and `op_mode=FIELD_CHECK`

**Required app changes:**
- Wait for `EVENT_ACK[CMD_SET_PIN]` before showing "Box is ready"
- Handle `EVENT_MODE` on the connection — update local mode state when received
- After `EVENT_MODE[FIELD_CHECK]` following setup, disconnect and show the provisioned box card (the advertisement now carries the box's real name)

**Wire struct:**
```c
typedef struct __attribute__((packed)) {
    uint8_t  msg_type;               /* 0x14 */
    uint8_t  msg_id;
    uint32_t pin_hash;               /* CRC32 of PIN */
    char     box_name[32];           /* null-terminated, max 31 chars + null */
} imb_pkt_cmd_set_pin_t;
```

---

## 3. EVENT_MODE is now sent on every mode transition

**What changed:** Previously `EVENT_MODE` (0x02) was never sent. The firmware now sends it after:
- `CMD_SET_PIN` succeeds → `EVENT_MODE[FIELD_CHECK]`
- `CMD_MODE` succeeds → `EVENT_MODE[<new_mode>]`
- `REGISTRATION_INCOMPLETE` auto-resumes on reconnect → no explicit EVENT_MODE (mode transition is silent; the advertisement `op_mode` field reflects it on next scan)

**Required app changes:**
- Register a handler for `EVENT_MODE { msg_type=0x02, mode=uint8 }`
- On receipt, update local mode state and refresh UI (mode badge on box card, button states, etc.)
- On `EVENT_MODE[REGISTRATION]`: switch to the registration UI flow
- On `EVENT_MODE[FIELD_CHECK]`: switch back to field-check UI; update Android connection priority hint to `HIGH`
- On `EVENT_MODE[FIELD_CHECK]` received right after `CMD_SET_PIN` ACK: treat as setup-complete signal

---

## 4. Connection parameters update on mode change

**What changed:** Firmware now requests updated connection parameters when mode transitions:
- `REGISTRATION` → requests 100–200 ms interval, latency=4, supervision=6 s
- Any other mode → requests 15–30 ms interval, latency=0, supervision=2 s

**Required app changes (Android only):**
- On `EVENT_MODE[REGISTRATION]`: call `BluetoothGatt.requestConnectionPriority(CONNECTION_PRIORITY_BALANCED)`
- On `EVENT_MODE[FIELD_CHECK]`: call `BluetoothGatt.requestConnectionPriority(CONNECTION_PRIORITY_HIGH)`
- iOS handles this automatically; no action needed.

---

## 5. CMD_UNBOND now returns EVENT_ACK before disconnecting

**What changed:** `CMD_UNBOND` (0x17) was previously silently ignored. It now:
1. Erases the NimBLE bond for the current peer
2. Returns `EVENT_ACK { acked_msg_type=0x17, status=OK }`
3. Disconnects

**Required app changes:**
- Wait for `EVENT_ACK[CMD_UNBOND]` before deleting the local bond on the phone side
- Handle the subsequent disconnect gracefully (do not show "reconnecting" — it's intentional)
- After disconnecting, delete the local bond and return to the scanner screen

---

## 6. REGISTRATION_INCOMPLETE: auto-resume on reconnect

**What changed:** When the firmware is in `REGISTRATION_INCOMPLETE` mode and a phone connects:
1. Firmware silently transitions back to `REGISTRATION`
2. Firmware re-queues all pending (unnamed) UIDs as `EVENT_TAG { direction=INSERT, name="" }` packets
3. These are flushed to the phone on subscribe (before `CMD_HELLO` ACK)

**The advertisement still shows `op_mode=REGISTRATION_INCOMPLETE` until the app connects and the session transitions internally.** On the *next* scan after connection, the advertisement will reflect `REGISTRATION`.

**Required app changes:**
- When connecting to a box advertised as `REGISTRATION_INCOMPLETE`, show the resume prompt (§6.4 flow is unchanged)
- Expect `EVENT_TAG` packets for unnamed UIDs to arrive during the subscribe-flush window (before `CMD_HELLO` response)
- Do not show these as new insertions — show them as "resume naming" prompts
- The flow after that is identical to normal registration: `CMD_NAME` per UID, then `CMD_MODE: FIELD_CHECK`

---

## 7. Advertisement now updates ~100 ms (was ~1280 ms)

**What changed:** Advertising interval is now 100–125 ms instead of the NimBLE default of 1280 ms.

**Impact:** The phone's BLE scanner will see the box significantly faster after power-on or lid-open. No app code change needed, but if the app has any discovery timeout shorter than ~500 ms it should be relaxed (the old 1280 ms default meant the first packet could arrive after >1 s).

---

## 8. Locked UUIDs (verify the app is using these)

These are locked as of 2026-05-27 in the firmware. Confirm the app's UUID constants match exactly:

```
Service:         e5d50000-01d0-47e0-afc5-01e466d9298e
EVENT_NOTIFY:    e5d50001-01d0-47e0-afc5-01e466d9298e
REPORT_NOTIFY:   e5d50002-01d0-47e0-afc5-01e466d9298e
COMMAND_WRITE:   e5d50003-01d0-47e0-afc5-01e466d9298e
```

---

## 9. Not yet implemented on firmware (do not expect)

These commands are received by the firmware but **not yet handled** — they will be silently ignored (no ACK returned):
- `CMD_GET_LOG` (0x18)
- `CMD_MESH_STATUS` (0x19)

Do not block any user-visible flow on an ACK from these commands.
