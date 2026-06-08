# CLAUDE.md

Guidance for Claude Code (claude.ai/code) when working with code in this repository.

## What this project is

Event-driven, decentralized smart inventory system for rugged/mobile environments (camping trips). Multiple physical boxes form a single ESP-Mesh network. Each box tracks NFC-tagged items using two directional NFC readers, detects cross-box item migrations, and delivers one consolidated inventory report to a nearby smartphone — all without cloud infrastructure.

Targets ESP-IDF on ESP32-S3 N16R8. Phone client is a separate codebase, built against [`docs/ble-contract.md`](docs/ble-contract.md).

## Documentation map

Detailed design + reference docs in `docs/`. Read on demand, not eagerly:

| Doc | When to read |
|---|---|
| [`docs/architecture.md`](docs/architecture.md) | Mode state machine, NVS layout, mesh/box identity, design constraints |
| [`docs/hardware.md`](docs/hardware.md) | Component list, GPIO pin map, deep-sleep & wake, factory reset trigger |
| [`docs/protocols.md`](docs/protocols.md) | Directional NFC scanning rules, tag provisioning flow, LED color contract |
| [`docs/ble-contract.md`](docs/ble-contract.md) | **Locked BLE/GATT contract** — UUIDs, advertisement, characteristics, message types, security, error handling. Source of truth for phone team. |
| [`docs/testing.md`](docs/testing.md) | Two-layer test architecture, HAL pattern, toolchain commands, host vs on-device |
| [`TASKS.md`](TASKS.md) | Active work tracking by phase |

## Shared Protocol Strategy (Contract as Code)

To prevent drift between the firmware (C) and phone app (Flutter/Dart), we use a **Single Source of Truth** pattern:

1.  **Source of Truth**: `components/imb_protocol/include/imb_protocol.h` defines all wire-level structs.
2.  **Consumption**: The phone repository includes this firmware repository as a **Git Submodule** (under `assets/protocol` or similar).
3.  **Code Generation**: A script in the phone repository parses the C headers to generate Dart model classes.
4.  **Enforcement**: Changes to the protocol MUST be made in the firmware repo first, then pulled into the phone repo via submodule update.

## Domain Terminology (from CONTEXT.md)

Strictly adhere to the following domain language in comments, docs, and code naming:
*   **Mesh**: A decentralized collection of Boxes sharing a PIN Hash. (Avoid: Network, group)
*   **Box**: The physical ESP32-S3 node. (Avoid: Device, node)
*   **Tag**: The physical NFC sticker/chip used for detection. (Avoid: Chip, sensor)
*   **Item**: The conceptual object (e.g., "Flashlight") attached to a Tag. (Avoid: Object, asset)
*   **Reachable**: A Box actively communicating over the mesh backhaul. (Avoid: Online, connected)
*   **Item Registration**: The act of associating an Item with a Tag. (Avoid: Tagging, enrolling)
*   **Anonymous Tag**: A Tag detected but not yet registered. (Avoid: Pending tag, unknown tag)

## Hot rules (always apply, no need to open subdocs)

- **`imb_local` NVS is ground truth.** Never derive item presence from `imb_mesh` when local data exists.
- **NDEF write is the registration commit point.** A tag is in the registry IFF its NDEF text record has been successfully written to the physical chip.
- **Logic components never call ESP-IDF directly.** They take a HAL struct (function pointers) at init. See `docs/testing.md` for the pattern.
- **Naming:** enums `_e`, struct typedefs `_t`, tagged unions `_u`.
- **No JSON over BLE.** All payloads are packed binary structs defined in `components/imb_protocol/include/imb_protocol.h`.
- **AMBIGUOUS NFC events are never silently resolved** — always surfaced to the user.
- **Single client per box** at the BLE layer. Multi-client may be needed at the mesh layer in a future commercial-inventory pivot.

## Quick commands

```bash
# Setup (one-time per shell)
. $IDF_PATH/export.sh

# Host tests (fast TDD, no hardware)
cd components/<name>/test && make run

# Flash + monitor (non-TTY: use python3 /tmp/read_serial.py for monitor)
idf.py -p /dev/cu.usbserial-* flash monitor

# After host tests, restore target before flashing
idf.py set-target esp32s3
```

Full toolchain reference: [`docs/testing.md`](docs/testing.md).
