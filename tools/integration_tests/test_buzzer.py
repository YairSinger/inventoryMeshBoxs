"""
Buzzer driver integration test.

Expected firmware tokens (all must be PASS):
  [PASS] buzzer_init
  [PASS] buzzer_tag_placed
  [PASS] buzzer_item_removed
  [PASS] buzzer_unknown_tag
  [PASS] buzzer_error
  [PASS] buzzer_ble_connected
  [PASS] buzzer_factory_reset

Run standalone:
  python3 tools/integration_tests/test_buzzer.py [port]
"""

import sys, os
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', '..'))

from tools.integration_tests.harness import SerialHarness
from tools.integration_tests._runner import run_test

TESTS = [
    'buzzer_init',
    'buzzer_tag_placed',
    'buzzer_item_removed',
    'buzzer_unknown_tag',
    'buzzer_error',
    'buzzer_ble_connected',
    'buzzer_factory_reset',
]
DESCRIPTION = 'Buzzer — LEDC init + all six patterns drain to idle'
TIMEOUT_S = 15

if __name__ == '__main__':
    port = sys.argv[1] if len(sys.argv) > 1 else '/dev/cu.usbserial-A5069RR4'
    run_test(port, TESTS, DESCRIPTION, TIMEOUT_S)
