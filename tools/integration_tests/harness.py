"""
SerialHarness — shared serial reader for IMB integration tests.

Reads from the board until [DONE] or timeout, collecting:
  [PASS] <name>
  [FAIL] <name>: <reason>
  [SKIP] <name>: <reason>

Import and use from any test script or from serial_monitor.py:
    from tools.integration_tests.harness import SerialHarness
    h = SerialHarness('/dev/cu.usbserial-A5069RR4')
    h.open()
    results = h.collect(timeout_s=45)
    h.close()
"""

import re, time
import serial

PASS_RE = re.compile(r'\[PASS\]\s+(\S+)')
FAIL_RE = re.compile(r'\[FAIL\]\s+(\S+):\s*(.*)')
SKIP_RE = re.compile(r'\[SKIP\]\s+(\S+):\s*(.*)')
DONE_RE = re.compile(r'\[DONE\]')
SLEEP_ENTER_RE = re.compile(r'\[INFO\] deep_sleep: board going to sleep')
SLEEP_WAKE_RE  = re.compile(r'\[INFO\] deep_sleep: woke from sleep')

_GRN  = '\033[92m'; _RED  = '\033[91m'; _YEL = '\033[93m'
_CYN  = '\033[96m'; _DIM  = '\033[2m';  _BOLD = '\033[1m'; _RST = '\033[0m'


class SerialHarness:
    def __init__(self, port: str, baud: int = 115200):
        self.port = port
        self.baud = baud
        self._ser = None

    def open(self):
        self._ser = serial.Serial(self.port, self.baud, timeout=0.1)
        self._ser.setDTR(False)
        self._ser.setRTS(False)

    def close(self):
        if self._ser:
            self._ser.close()
            self._ser = None

    def collect(self, timeout_s: float = 45) -> dict:
        """
        Read serial lines until [DONE] or timeout.

        Returns dict keyed by test name:
            { 'nvs_write_read': {'status': 'PASS', 'detail': ''},
              'tag_write':       {'status': 'PASS', 'detail': ''},
              ... }

        Deep sleep is transparent — the board sleeps 3 s then auto-wakes.
        Timeout of 45 s comfortably covers the full two-boot sequence.
        """
        results: dict = {}
        buf = ''
        deadline = time.monotonic() + timeout_s

        while time.monotonic() < deadline:
            chunk = self._ser.read(256)
            if not chunk:
                continue
            buf += chunk.decode('utf-8', errors='replace')

            while '\n' in buf:
                line, buf = buf.split('\n', 1)
                line = line.rstrip('\r').strip()
                if not line:
                    continue

                self._echo(line)

                m = PASS_RE.search(line)
                if m:
                    results[m.group(1)] = {'status': 'PASS', 'detail': ''}
                    continue

                m = FAIL_RE.search(line)
                if m:
                    results[m.group(1)] = {'status': 'FAIL', 'detail': m.group(2).strip()}
                    continue

                m = SKIP_RE.search(line)
                if m:
                    results[m.group(1)] = {'status': 'SKIP', 'detail': m.group(2).strip()}
                    continue

                if DONE_RE.search(line):
                    return results

        return results  # timeout

    def _echo(self, line: str):
        if '[PASS]' in line:
            print(f'  {_GRN}{_BOLD}{line}{_RST}')
        elif '[FAIL]' in line:
            print(f'  {_RED}{_BOLD}{line}{_RST}')
        elif '[SKIP]' in line:
            print(f'  {_YEL}{line}{_RST}')
        elif SLEEP_ENTER_RE.search(line):
            print(f'  {_CYN}{_BOLD}{line}{_RST}')
            print(f'\n  {_CYN}┌─ Board is sleeping for 3 s ──────────────────────┐{_RST}')
            print(f'  {_CYN}│  Do NOT press EN/RST — it will wake automatically │{_RST}')
            print(f'  {_CYN}└───────────────────────────────────────────────────┘{_RST}\n')
        elif SLEEP_WAKE_RE.search(line):
            print(f'  {_CYN}{_BOLD}{line}{_RST}')
            print(f'  {_CYN}▶ Board woke up — checking result...{_RST}')
        elif '[DONE]' in line or '[INFO]' in line:
            print(f'  {_DIM}{line}{_RST}')
        else:
            print(f'  {line}')
