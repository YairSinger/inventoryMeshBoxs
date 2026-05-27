# Architecture

## Project Overview

Event-driven, decentralized smart inventory system for rugged/mobile environments (camping trips). Multiple physical boxes form a single ESP-Mesh network. Each box tracks NFC-tagged items using two directional NFC readers, detects cross-box item migrations, and delivers one consolidated inventory report to a nearby smartphone — all without cloud infrastructure.

## Operational Modes

```
[SETUP] ──first BLE connect + PIN set──▶ [FIELD_CHECK]
[FIELD_CHECK] ──phone sends CMD_MODE:REGISTRATION──▶ [REGISTRATION]
[REGISTRATION] ──phone sends CMD_MODE:FIELD_CHECK──▶ [FIELD_CHECK]
[REGISTRATION] ──disconnect + pending unnamed UIDs──▶ [REGISTRATION_INCOMPLETE]
[REGISTRATION_INCOMPLETE] ──phone reconnects──▶ [REGISTRATION] (resume)
[REGISTRATION_INCOMPLETE] ──lid-open, pending UIDs absent──▶ [FIELD_CHECK]
```

Full mode state machine + transition details: [ble-contract.md §6](ble-contract.md#6-operational-mode-state-machine).

### REGISTRATION mode
- Triggered by explicit BLE command from phone
- Box scans continuously while lid is open (two PN532s active)
- Any new unregistered tag → immediate `EVENT_TAG` BLE notification + accept/reject prompt on phone
- Accepted tags: written to NVS `imb_local`, name written to physical NTAG213 NDEF text record
- **NDEF write is the commit point**: tag enters registry IFF NDEF text record successfully written. If NDEF write fails, UID stays in pending set.
- Registration is a **hard gate**: all tags must be named before `CMD_MODE:FIELD_CHECK` is accepted; otherwise ACK is `REGISTRATION_INCOMPLETE`.

### REGISTRATION_INCOMPLETE mode (sticky)
- Entered when: REGISTRATION grace window (60 s) expires with unnamed UIDs still pending
- Pending UIDs persisted to NVS (`imb_state` namespace, see below). Survives reboot.
- Only escape: name all pending UIDs (back to REGISTRATION → FIELD_CHECK), OR physically remove pending UIDs from box (lid-open rescan auto-clears).
- Box advertises this mode in mfg-data; phone shows high-priority "unfinished registration" alert.

### FIELD_CHECK mode (default)
- Lid opens → both PN532s begin continuous polling → BLE advertising starts
- Insert/extract events fire LED color + `EVENT_TAG` BLE notify in real time
- Lid closes → delta computed against `imb_local` registry → if delta changed since last check, `REPORT_NOTIFY` fragmented + sent (see [ble-contract.md §4.3](ble-contract.md#43-report-fragmentation))
- Silence if nothing changed (no alert fatigue)
- Box stops BLE advertising after report ACKed → deep sleep

## NVS Layout

Five namespaces with different trust levels:

| Namespace | Key | Type | Description |
|---|---|---|---|
| `imb_identity` | `box_name` | string | Human-readable name (phone-assigned during setup) |
| `imb_identity` | `box_id` | uint16 | Last 2 bytes of MAC address |
| `imb_identity` | `pin_hash` | uint32 | CRC32 of user-set PIN; mesh identity |
| `imb_local` | `item_<uid>` | struct | Per-item: UID + NDEF name + status. **Always valid, never stale.** |
| `imb_local` | `item_count` | uint16 | Total local registered items |
| `imb_state` | `op_mode` | uint8 | SETUP / FIELD_CHECK / REGISTRATION / REGISTRATION_INCOMPLETE |
| `imb_state` | `pending_count` | uint8 | Number of pending (unnamed) UIDs in current/sticky registration |
| `imb_state` | `pending_uids` | array | Packed array of pending UIDs (up to `IMB_REGISTRY_MAX_ITEMS`) |
| `imb_state` | `pending_epoch` | uint32 | Registration session id |
| `imb_mesh` | `mesh_epoch` | uint32 | Increments at start of each registration session (Phase 3) |
| `imb_mesh` | `item_<uid>` | struct | Mesh-wide item registry. Flagged `MESH_STALE` on epoch mismatch. (Phase 3) |
| `imb_txlog` | `tx_<seq>` | struct | Transaction log entries (idempotent, dedupe by box_id+seq). (Phase 3) |
| `imb_txlog` | `tx_head` | uint32 | Latest sequence number (Phase 3) |

`imb_local` is always authoritative — valid even when isolated from mesh. `imb_mesh` is best-effort and must be treated as stale if `mesh_epoch` mismatches peers.

## Mesh Identity & PIN

Each mesh is identified by a user-set PIN (set on first registration session). Boxes advertise a CRC32 **hash of the PIN** in their BLE advertisement payload. The phone app only shows boxes whose PIN hash matches the current mesh — prevents accidental merging with a neighbor's mesh.

The PIN hash is a **discovery filter, not a security mechanism**. Application-layer authentication is handled by `CMD_HELLO` over the encrypted BLE link. See [ble-contract.md §5](ble-contract.md#5-security-model).

Box joining flow (e.g., adding Box C to an existing A+B mesh):
1. Box C advertises `IMB-<name>-<last4MAC>` with its PIN hash
2. Phone shows Box C under "Available Unjoined Boxes"
3. User approves join → phone pushes current epoch + full mesh registry to Box C over BLE
4. Box C writes to `imb_mesh`, adopts epoch, joins ESP-Mesh

## Box Identity

- `box_id` = last 2 bytes of MAC address (derived via `esp_read_mac()`, no Wi-Fi needed)
- Human-readable name assigned by phone app during SETUP
- Box advertises as `IMB-SETUP-<last4MAC>` until named + PIN set, then as `IMB-<name>-<last4MAC>`

## On Power Loss / Reboot

1. Box boots → reads `imb_local` from NVS (always valid)
2. Reads `imb_state.op_mode`:
   - `SETUP`: advertise as IMB-SETUP, wait for provisioning
   - `FIELD_CHECK`: standard operation
   - `REGISTRATION_INCOMPLETE`: re-advertise sticky state, reload `pending_uids`, wait for phone to resume
3. Requires lid-open to rescan physical contents and update session state
4. If lid cannot be opened → rejoins mesh with `STATE_STALE` flag in report (Phase 3)
5. Detects mesh epoch mismatch → requests full registry sync from phone (phone pulls from mesh peers) (Phase 3)

## Key Design Constraints

- **Power first**: minimize active-window duration. Both PN532s off before deep sleep.
- **Zero heap fragmentation**: static or pool-allocated buffers only. Nodes run indefinitely.
- **No external infrastructure**: BLE and ESP-Mesh only. No Wi-Fi, no cloud, no DNS.
- **`imb_local` is ground truth**: never derive item presence from `imb_mesh` when local data exists.
- **Idempotent transactions**: same transaction arriving twice is a no-op (deduplicate by `box_id` + `seq`).
- **NVS writes are atomic**: use versioned double-buffer or NVS transactions to survive power-loss mid-write.
- **Directional detection is authoritative**: AMBIGUOUS events are never silently resolved — always surfaced to user.
- **NDEF write is registration's commit point**: physical tag carries name; if write fails, item is not registered.
- **Physical items in box ⊆ registered items** (FIELD_CHECK invariant): enforced by REGISTRATION_INCOMPLETE sticky state.
