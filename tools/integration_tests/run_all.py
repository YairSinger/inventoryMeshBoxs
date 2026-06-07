#!/usr/bin/env python3
"""
IMB driver integration test runner — runs all three driver tests in one session.

Usage:
  python3 tools/integration_tests/run_all.py [port]

Sequence:
  Boot 1 — NVS tests → tag_write (place tag on reader #1) → board sleeps 3 s
  Boot 2 — board auto-wakes → deep_sleep result → [DONE]

Total run time: ~15-20 s (no button presses needed after EN/RST).

Can also be imported by serial_monitor.py:
  from tools.integration_tests.run_all import run_integration_tests
  run_integration_tests(port)
"""

import sys, os
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', '..'))

from tools.integration_tests.harness import SerialHarness

_GRN  = '\033[92m'; _RED  = '\033[91m'; _YEL  = '\033[93m'
_CYN  = '\033[96m'; _BOLD = '\033[1m';  _DIM  = '\033[2m';  _RST = '\033[0m'

DEFAULT_PORT = '/dev/cu.usbserial-A5069RR4'

ALL_TESTS = [
    ('buzzer_init',          'Buzzer LEDC + FreeRTOS timer init'),
    ('buzzer_tag_placed',    'TAG_PLACED pattern drains to idle'),
    ('buzzer_item_removed',  'ITEM_REMOVED 2-beep pattern drains to idle'),
    ('buzzer_unknown_tag',   'UNKNOWN_TAG long beep drains to idle'),
    ('buzzer_error',         'ERROR 3-beep pattern drains to idle'),
    ('buzzer_ble_connected', 'BLE_CONNECTED rising chirp drains to idle'),
    ('buzzer_factory_reset', 'FACTORY_RESET continuous; stops on silence()'),
    ('buzzer_on_insert',     'INSERT event → TAG_PLACED beep fires'),
    ('buzzer_on_extract',    'EXTRACT event → ITEM_REMOVED beep fires'),
    ('nvs_write_read',       'NVS write + read back'),
    ('nvs_erase',            'NVS erase + verify gone'),
    ('tag_write',            'Tag write + readback  (MIFARE Classic 1K or NTAG213)'),
    ('deep_sleep',           'Deep sleep 3 s timer + wakeup cause check'),
]


def run_integration_tests(port: str = DEFAULT_PORT, timeout_s: float = 45) -> dict:
    h = SerialHarness(port, baud=115200)
    h.open()
    try:
        results = h.collect(timeout_s=timeout_s)
    finally:
        h.close()
    return results


def print_summary(results: dict) -> bool:
    passed = failed = skipped = missing = 0
    print(f'\n{_BOLD}{"─"*56}{_RST}')
    print(f'{_BOLD}  IMB Driver Integration Test Results{_RST}')
    print(f'{_BOLD}{"─"*56}{_RST}')
    for name, desc in ALL_TESTS:
        r = results.get(name)
        if r is None:
            tag = f'{_RED}{_BOLD}MISSING{_RST}'; missing += 1
        elif r['status'] == 'PASS':
            tag = f'{_GRN}{_BOLD}PASS{_RST}'; passed += 1
        elif r['status'] == 'FAIL':
            tag = f'{_RED}{_BOLD}FAIL{_RST}  — {r["detail"]}'; failed += 1
        else:
            tag = f'{_YEL}SKIP{_RST}  — {r["detail"]}'; skipped += 1
        print(f'  {name:<24} {tag}')
        print(f'  {_DIM}{"":24} {desc}{_RST}')
    print(f'{_BOLD}{"─"*56}{_RST}')
    print(f'  {_GRN}{passed} passed{_RST}  '
          f'{_RED}{failed} failed{_RST}  '
          f'{_YEL}{skipped} skipped{_RST}  '
          f'{_RED}{missing} missing{_RST}')
    return (failed + missing) == 0


def main():
    port = sys.argv[1] if len(sys.argv) > 1 else DEFAULT_PORT

    print(f'\n{_BOLD}{"═"*56}{_RST}')
    print(f'{_BOLD}  IMB Driver Integration Tests{_RST}')
    print(f'{_BOLD}{"═"*56}{_RST}')
    print(f'  Port    : {port}')
    print(f'  Timeout : 45 s')
    print()
    print(f'  {_CYN}Steps:{_RST}')
    print(f'  {_CYN}  1. Place your tag on reader #1 NOW (before pressing EN/RST){_RST}')
    print(f'  {_CYN}  2. Press EN/RST on the board{_RST}')
    print(f'  {_CYN}  3. Board runs NVS tests, then scans for tag (15 s window){_RST}')
    print(f'  {_CYN}  4. Board sleeps 3 s and auto-wakes — do NOT press anything{_RST}')
    print(f'  {_CYN}  5. Results appear{_RST}')
    print(f'{_BOLD}{"─"*56}{_RST}')
    input(f'  Press EN/RST on the board, then hit Enter here... ')
    print()

    results = run_integration_tests(port, timeout_s=45)
    ok = print_summary(results)
    sys.exit(0 if ok else 1)


if __name__ == '__main__':
    main()
