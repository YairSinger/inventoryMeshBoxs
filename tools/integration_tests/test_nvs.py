"""
NVS driver integration test.

Expected firmware tokens (both must be PASS):
  [PASS] nvs_write_read
  [PASS] nvs_erase

Run standalone:
  python3 tools/integration_tests/test_nvs.py [port]
"""

import sys, os
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', '..'))

from tools.integration_tests.harness import SerialHarness
from tools.integration_tests._runner import run_test

TESTS = ['nvs_write_read', 'nvs_erase']
DESCRIPTION = 'NVS — write/read/erase in imb_local namespace'
TIMEOUT_S = 20   # no sleep involved; fast

if __name__ == '__main__':
    port = sys.argv[1] if len(sys.argv) > 1 else '/dev/cu.usbserial-A5069RR4'
    run_test(port, TESTS, DESCRIPTION, TIMEOUT_S)
