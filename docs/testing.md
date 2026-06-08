# Testing & Toolchain

## Phase 1 Scope (Single Box, No Mesh)

**IN:** BOOT button lid trigger, two PN532s (SPI, directional, AMBIGUOUS), WS2812B LED, NVS `imb_local` + `imb_state` only, BLE GATT server (3 characteristics), full BLE contract per `docs/ble-contract.md`, REGISTRATION flow with NDEF write + pending UIDs in NVS, REGISTRATION_INCOMPLETE sticky state, FIELD_CHECK delta-only alerts, accept/reject for post-registration tags, PIN provisioning in SETUP mode, factory reset via BOOT long-press.

**OUT:** ESP-Mesh, master box OLED + buttons, SR04, power monitoring, advanced box naming flow (Phase 2 master box drives this).

## Toolchain

Targets ESP-IDF (Espressif IoT Development Framework).

```bash
# One-time setup
. $IDF_PATH/export.sh

# Build
idf.py build

# Flash + monitor (adjust port for your system)
idf.py -p /dev/cu.usbserial-* flash monitor

# Flash only / monitor only
idf.py -p /dev/cu.usbserial-* flash
idf.py -p /dev/cu.usbserial-* monitor

# Run a single component's unit tests on-device (Unity test runner)
idf.py -T components/<component_name> build flash monitor

# Run a single component's host tests (no hardware needed — fast TDD loop)
cd components/<component_name>/test && make run

# Erase NVS (resets all state — use during dev)
python $IDF_PATH/components/partition_table/parttool.py -p /dev/cu.usbserial-* erase_partition --partition-name nvs
```

`idf_monitor` requires interactive TTY. For non-TTY use (e.g. agent runs), use `python3 /tmp/read_serial.py` — see `.claude/skills/dev-setup.md`.

## Testing Strategy

### Two-layer architecture

```
┌─────────────────────────────────┐
│  LOGIC LAYER (pure C)           │  ← host tests, <1s, plain Makefile + Unity
│  imb_detector, imb_registry,    │    no ESP-IDF dependency
│  imb_delta, imb_protocol,       │    run with: cd components/<name>/test && make run
│  imb_session                    │
├─────────────────────────────────┤
│  HAL INTERFACE (function ptrs)  │  ← the boundary — never cross it in logic code
├─────────────────────────────────┤
│  DRIVER LAYER (ESP-IDF)         │  ← on-device tests, 30s flash cycle
│  PN532 SPI, WS2812B RMT,        │    run with: idf.py -p /dev/cu.* flash monitor
│  BLE GATT (imb_ble), NVS,       │    (standalone test project per component)
│  GPIO/sleep                     │
└─────────────────────────────────┘
```

ESP-IDF's `linux` target is too coupled to `esp_err.h` and hardware headers to compile cleanly on macOS for arbitrary components. Logic components use a plain `Makefile` + vendored Unity (`vendor/unity/` — 3 files, zero deps) instead.

### Host test structure (per logic component)

```
components/<name>/
├── include/<name>.h
├── <name>.c
└── test/
    ├── Makefile               # plain cc, no ESP-IDF
    ├── vendor/unity/          # unity.c, unity.h, unity_internals.h
    └── main/
        ├── test_<name>.c      # test functions: void test_xxx(void)
        └── runner.c           # main() with UNITY_BEGIN / RUN_TEST / UNITY_END
```

After running host tests, `sdkconfig` gets reset to `linux` target → run `idf.py set-target esp32s3` before next hardware build.

### HAL Abstraction Pattern

Logic components must never call hardware APIs directly. Use a thin interface struct passed at init:

```c
typedef struct {
    esp_err_t (*read_tag)(uint8_t reader_id, nfc_tag_t *out);
    esp_err_t (*write_tag)(uint8_t reader_id, const char *ndef_text);
    uint32_t  (*get_timestamp_ms)(void);
} imb_nfc_hal_t;
```

Production code populates the struct with real ESP-IDF driver functions. Host tests populate it with stubs returning controlled data. This pattern applies to NFC, NVS, and BLE interfaces.

### On-device test structure (per driver component)

Each driver component has a standalone ESP-IDF test project under `components/<name>/test/` with its own `CMakeLists.txt` + `sdkconfig`. Flash to hardware to run. Results appear on serial monitor.

## Naming conventions

- Enums: `_e` suffix (e.g. `imb_op_mode_e`)
- Struct typedefs: `_t` suffix (e.g. `imb_item_t`)
- Tagged unions: `_u` suffix (e.g. `imb_entry_u`)
