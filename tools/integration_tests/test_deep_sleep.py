"""
Deep sleep integration test.

The firmware enters deep sleep after the other tests and wakes itself
via a 3-second timer. The harness just keeps reading through the silent
gap — no special handling needed.

Expected firmware token:
  [PASS] deep_sleep   (wakeup cause == TIMER)
  [FAIL] deep_sleep: <reason>

Run standalone:
  python3 tools/integration_tests/test_deep_sleep.py [port]
"""

import sys, os
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', '..'))

from tools.integration_tests.harness import SerialHarness
from tools.integration_tests._runner import run_test

TESTS = ['deep_sleep']
DESCRIPTION = 'Deep sleep — 3 s timer wakeup, verifies ESP_SLEEP_WAKEUP_TIMER'
TIMEOUT_S = 30   # first boot NVS+NDEF (~8 s) + 3 s sleep + second boot

if __name__ == '__main__':
    port = sys.argv[1] if len(sys.argv) > 1 else '/dev/cu.usbserial-A5069RR4'
    run_test(port, TESTS, DESCRIPTION, TIMEOUT_S)
