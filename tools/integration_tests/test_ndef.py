"""
Tag write driver integration test (MIFARE Classic 1K or NTAG213).

Expected firmware token:
  [PASS] tag_write   — write succeeded and readback matched
  [SKIP] tag_write: <reason>   — no tag present or unknown type

Run standalone:
  python3 tools/integration_tests/test_ndef.py [port]
"""

import sys, os
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', '..'))

from tools.integration_tests._runner import run_test

TESTS = ['tag_write']
DESCRIPTION = 'Tag write + readback (MIFARE Classic 1K or NTAG213 — place tag on reader #1)'
TIMEOUT_S = 20

if __name__ == '__main__':
    port = sys.argv[1] if len(sys.argv) > 1 else '/dev/cu.usbserial-A5069RR4'
    run_test(port, TESTS, DESCRIPTION, TIMEOUT_S)
