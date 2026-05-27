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

### ⚠️ Target reset after host tests

Running host tests (components/*/test with linux target) changes the root `sdkconfig` to `CONFIG_IDF_TARGET="linux"`. Before building for hardware you **must** reset the target:

```bash
get_idf
idf.py set-target esp32s3
idf.py build
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

Connect the ESP32-S3 N16R8 via USB-C (UART port, not native USB port). Port shows up as `/dev/cu.usbserial-*`.

```bash
idf.py -p /dev/cu.usbserial-* flash
```

`idf_monitor` requires an interactive TTY and does not work from Claude Code's shell. Use this Python script to read serial output instead:

```python
# /tmp/read_serial.py
import serial, time
s = serial.Serial('/dev/cu.usbserial-A5069RR4', 115200, timeout=0.1)
s.setDTR(False); s.setRTS(False)
print('Listening 10s — press RESET now', flush=True)
buf = b''
deadline = time.time() + 10
while time.time() < deadline:
    chunk = s.read(256)
    if chunk:
        buf += chunk
        deadline = max(deadline, time.time() + 2)
s.close()
print(buf.decode('utf-8', errors='replace'))
```

Run with: `python3 /tmp/read_serial.py | tee /tmp/output.log`  
Press **EN/RST** (not BOOT) on the board after starting the script.  
If it boots into download mode, DTR is holding GPIO0 low — use `setDTR(False)`.

Install pyserial if needed: `pip3 install pyserial`

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

## PN532 SPI notes (discovered during bringup)

- **Mode jumpers**: SW1 maps to I0, SW2 maps to I1. PN532 datasheet Table 44: I1=1,I0=0 → SPI; I1=0,I0=1 → I2C; I1=0,I0=0 → HSU. Therefore: **SPI = SW1=0V, SW2=3.3V**. I2C = SW1=3.3V, SW2=0V. HSU = both 0V. Must power-cycle chips after changing jumpers — mode is latched at power-on.
- **LSB-first**: PN532 is natively LSB-first. These boards have no bit-reversal circuit. Use bit-bang LSB-first (see main/main.c). ESP-IDF hardware SPI does not work.
- **Wakeup**: assert CS (SS) low for ≥5ms with no clock before sending the first command. Use manual GPIO CS (`spics_io_num = -1`) to do this properly.
- **Clock**: 500kHz for bringup; can increase to 1MHz+ once verified.
- **Flash size warning**: `W spi_flash: Detected size(16384k) larger than binary header(2048k)` is harmless — fix by setting flash size to 16MB in menuconfig (`CONFIG_ESPTOOLPY_FLASHSIZE_16MB`).

## Architecture orientation

Read `CLAUDE.md` in the repo root — it documents every major design decision, the two-layer testing strategy, NVS layout, BLE GATT structure, and GPIO assignments. Start there before touching any code.
