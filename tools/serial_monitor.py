#!/usr/bin/env python3
"""
IMB serial monitor — colourised live output with NFC tag classification.
Usage: python3 tools/serial_monitor.py [port] | tee /tmp/imb_smoke.log
Default port: /dev/cu.usbserial-A5069RR4
"""

import serial, sys, time, re, signal, io
sys.stdout = io.TextIOWrapper(sys.stdout.buffer, line_buffering=True)

PORT = sys.argv[1] if len(sys.argv) > 1 else '/dev/cu.usbserial-A5069RR4'
BAUD = 115200

# ANSI colours
GRN  = '\033[92m'
YEL  = '\033[93m'
RED  = '\033[91m'
CYN  = '\033[96m'
DIM  = '\033[2m'
BOLD = '\033[1m'
RST  = '\033[0m'

# ── NFC/RFID classification ──────────────────────────────────────────────────
# Firmware uses InListPassiveTarget baud=0 → ISO 14443A (NFC, 13.56 MHz, ~5–7 cm).
# ISO 15693 HF-RFID (~100 cm) is NOT used.
#
# ATQA quick-ref (ISO 14443A):
#   0x4400 → NTAG213/215/216, MIFARE Ultralight (NFC Forum Type 2)
#   0x0400 → MIFARE Classic 1K
#   0x0200 → MIFARE Classic 4K / Mini
#   0x0344 → MIFARE DESFire

ATQA_NAMES = {
    '4400': ('NFC', 'NTAG21x / Ultralight',  '0–7 cm'),
    '0400': ('NFC', 'MIFARE Classic 1K',      '0–7 cm'),
    '0200': ('NFC', 'MIFARE Classic 4K/Mini', '0–7 cm'),
    '0344': ('NFC', 'MIFARE DESFire',         '0–7 cm'),
    '4403': ('NFC', 'MIFARE Plus',            '0–7 cm'),
}

def classify(atqa_str: str):
    key = atqa_str.upper().replace(' ', '')
    entry = ATQA_NAMES.get(key)
    if entry:
        return entry
    return ('NFC', f'ISO 14443A (ATQA={key})', '0–7 cm')

# TAG log line: [PN532 #1] TAG  ATQA=4400 SAK=00 UID(7): AA BB CC DD EE FF 00
TAG_RE = re.compile(
    r'\[PN532 #(\d+)\]\s+TAG\s+ATQA=([0-9A-Fa-f]+)\s+SAK=([0-9A-Fa-f]+)\s+UID\((\d+)\):\s+([0-9A-Fa-f ]+)',
    re.IGNORECASE
)
NO_TAG_RE = re.compile(r'\[PN532 #(\d+)\]\s+(No tag|no target)\b', re.IGNORECASE)

def header():
    print(f'\n{BOLD}┌─────────────────────────────────────────────────────┐{RST}')
    print(f'{BOLD}│  IMB Serial Monitor  •  {PORT}  │{RST}')
    print(f'{BOLD}│  Protocol: ISO 14443A (NFC, 13.56 MHz)              │{RST}')
    print(f'{BOLD}│  Expected range: 0–7 cm  (optimal 1–5 cm)           │{RST}')
    print(f'{BOLD}└─────────────────────────────────────────────────────┘{RST}\n')

def fmt_tag(reader, atqa, sak, uid_len, uid_hex):
    proto, tag_name, rng = classify(atqa)
    uid_clean = uid_hex.strip()
    ts = time.strftime('%H:%M:%S')
    print(
        f'  {GRN}●{RST} {GRN}{BOLD}[{ts}] Reader #{reader} — {proto} TAG DETECTED{RST}\n'
        f'     Tag type : {CYN}{tag_name}{RST}\n'
        f'     ATQA     : {atqa}  SAK: {sak}\n'
        f'     UID ({uid_len} bytes): {BOLD}{uid_clean}{RST}\n'
        f'     Range    : {YEL}{rng}{RST}\n'
    )

def main():
    signal.signal(signal.SIGINT, lambda *_: sys.exit(0))
    header()
    s = serial.Serial(PORT, BAUD, timeout=0.1)
    s.setDTR(False)
    s.setRTS(False)
    print(f'{DIM}Listening … press EN/RST on the board{RST}\n')

    line_buf = ''
    while True:
        chunk = s.read(512)
        if not chunk:
            continue
        text = chunk.decode('utf-8', errors='replace')
        line_buf += text
        while '\n' in line_buf:
            line, line_buf = line_buf.split('\n', 1)
            line = line.rstrip('\r')

            m = TAG_RE.search(line)
            if m:
                fmt_tag(m.group(1), m.group(2), m.group(3), m.group(4), m.group(5))
                continue

            m2 = NO_TAG_RE.search(line)
            if m2:
                ts = time.strftime('%H:%M:%S')
                print(f'  {DIM}○ [{ts}] Reader #{m2.group(1)} — no tag{RST}')
                continue

            if line.strip():
                print(f'  {DIM}{line}{RST}')

if __name__ == '__main__':
    main()
