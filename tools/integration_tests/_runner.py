"""Shared CLI runner used by individual test_*.py scripts."""

import sys
from tools.integration_tests.harness import SerialHarness

_GRN  = '\033[92m'; _RED  = '\033[91m'; _YEL = '\033[93m'
_BOLD = '\033[1m';  _RST  = '\033[0m'


def run_test(port: str, expected_tests: list, description: str, timeout_s: float):
    print(f'\n{_BOLD}{description}{_RST}  port={port}')
    print('Press EN/RST on the board then hit Enter...')
    input()

    h = SerialHarness(port)
    h.open()
    try:
        results = h.collect(timeout_s=timeout_s)
    finally:
        h.close()

    _print_summary(results, expected_tests)
    failed = [n for n in expected_tests
              if results.get(n, {}).get('status') == 'FAIL'
              or n not in results]
    sys.exit(1 if failed else 0)


def _print_summary(results: dict, expected: list):
    print(f'\n{_BOLD}{"─"*48}{_RST}')
    for name in expected:
        r = results.get(name)
        if r is None:
            tag = f'{_RED}{_BOLD}MISSING{_RST}'
        elif r['status'] == 'PASS':
            tag = f'{_GRN}{_BOLD}PASS{_RST}'
        elif r['status'] == 'FAIL':
            tag = f'{_RED}{_BOLD}FAIL{_RST}  {r["detail"]}'
        else:
            tag = f'{_YEL}SKIP{_RST}  {r["detail"]}'
        print(f'  {name:<24} {tag}')
    print(f'{_BOLD}{"─"*48}{_RST}')
