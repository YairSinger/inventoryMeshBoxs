# dev-setup skill

Guides a new team member through setting up their development environment for this repository from scratch. Run this skill when someone joins the project or sets up a new machine.

## Steps

### 1. Prerequisites

Check and install system dependencies:

```bash
# Homebrew (if not installed)
/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"

# Required tools
brew install cmake ninja dfu-util python3 git
```

### 2. Clone the repository

```bash
git clone <repo-url>
cd inventoryMeshBoxs
```

### 3. Install ESP-IDF v5.3

ESP-IDF is the Espressif firmware framework. It manages its own toolchain and Python environment — do NOT use UV or pip to manage its dependencies.

```bash
mkdir -p ~/esp && cd ~/esp
git clone --recursive https://github.com/espressif/esp-idf.git -b v5.3
cd esp-idf
./install.sh esp32s3
```

**Known issue on macOS with Apple Python 3.9**: `importlib.metadata` cannot find `ruamel.yaml` due to a Python 3.9 package-name normalization bug. Apply this one-time fix after `install.sh` completes:

```bash
SITE_PKGS=~/.espressif/python_env/idf5.3_py3.9_env/lib/python3.9/site-packages
ln -sf "$SITE_PKGS/ruamel_yaml-0.19.1.dist-info" \
       "$SITE_PKGS/ruamel.yaml-0.19.1.dist-info"
```

Add the activation alias to `~/.zshrc`:

```bash
echo 'alias get_idf=". ~/esp/esp-idf/export.sh"' >> ~/.zshrc
source ~/.zshrc
```

Activate ESP-IDF in every new terminal session before using `idf.py`:

```bash
get_idf
```

### 4. Verify ESP-IDF installation

```bash
get_idf
idf.py --version   # should print: ESP-IDF v5.3
```

### 5. Verify host tests work (no hardware needed)

```bash
cd components/imb_detector/test
make run
# Expected: 5 Tests 0 Failures 0 Ignored — OK
```

### 6. Build the main firmware (ESP32-S3 target)

```bash
get_idf
cd /path/to/inventoryMeshBoxs
idf.py build
```

### 7. Flash to hardware (when you have the board)

Connect the ESP32-S3 N16R8 via USB-C (UART port, not native USB port).

```bash
idf.py -p /dev/cu.usbserial-* flash monitor
# Press BOOT button if the board doesn't enter download mode automatically
```

## Testing cheat sheet

| What | Command | Needs hardware? |
|---|---|---|
| Logic unit tests | `cd components/<name>/test && make run` | No |
| On-device driver tests | `idf.py -p /dev/cu.* flash monitor` | Yes |
| Erase NVS (reset state) | `python $IDF_PATH/components/partition_table/parttool.py -p /dev/cu.* erase_partition --partition-name nvs` | Yes |

## Hardware setup

- **Board**: ESP32-S3 N16R8 (16MB flash, 8MB PSRAM, dual USB-C)
- **UART port**: use for flashing and serial monitor
- **Native USB port**: not used for development
- **Boot button** (GPIO0): temporary lid-open trigger during prototyping
- See CLAUDE.md for full GPIO pin assignments

## Architecture orientation

Read `CLAUDE.md` in the repo root — it documents every major design decision, the two-layer testing strategy, NVS layout, BLE GATT structure, and GPIO assignments. Start there before touching any code.
