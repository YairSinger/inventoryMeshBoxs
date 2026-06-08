# Protocols — NFC, Tag Provisioning, LED

## Directional NFC Scanning

Two PN532 readers are positioned at the box opening like a turnstile gate. Sequence of detection determines direction:

- Reader #1 fires first → Reader #2 fires within 200ms → **item inserted**
- Reader #2 fires first → Reader #1 fires within 200ms → **item extracted**
- Only one reader fires within 200ms → **AMBIGUOUS** (logged, excluded from authoritative set, surfaced as warning in report)

The authoritative item set is the **cumulative result of all insert/extract events** since the session started — not a point-in-time scan. Tags are NTAG213; item identity is read from the NDEF text record written to user memory pages (not from the UID alone).

Logic implementation: `components/imb_detector` (host-tested). HAL injection pattern keeps it driver-free.

## Tag Provisioning

Tags carry a human-readable item name written directly to NTAG213 user memory as an NDEF text record. The box writes the tag; the phone provides the name via BLE.

Flow:
1. User places new item in box during REGISTRATION session
2. PN532 reads tag — no valid NDEF name found
3. Box fires `EVENT_TAG { direction=INSERT, uid, name="" }` over BLE
4. Phone shows "Name this item" prompt
5. User types name → phone writes to `COMMAND_WRITE` characteristic: `CMD_NAME { msg_id, uid, name }`
6. Box writes NDEF text record to tag via PN532 `InDataExchange`
7. **Commit point**: if NDEF write succeeds → registry updated, EVENT_ACK[OK]. If fails → tag stays in pending set, EVENT_ACK[NDEF_WRITE_FAILED]; phone may retry.
8. Registration is **blocked** until all tags have valid names written — see [architecture.md](architecture.md#registration_incomplete-mode-sticky) for REGISTRATION_INCOMPLETE sticky state.

## LED Color Contract (WS2812B GPIO48)

| Event | Color / Pattern |
|---|---|
| Item inserted | Green single pulse |
| Item extracted | Red single pulse |
| AMBIGUOUS scan | Yellow single flash |
| Registration pass (tag named + NDEF written) | Solid green 2s |
| Registration fail (NDEF write error) | Red double-flash |
| Mesh disconnected (Phase 3) | Blue slow breathing |
| BLE connected, idle | White dim pulse every 3s |
| Factory reset hold (BOOT button) | Slow red breathing during 10s hold |
| Factory reset triggered | Fast red flash + reboot |
| Deep sleep | Off |
