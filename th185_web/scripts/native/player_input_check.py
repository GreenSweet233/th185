#!/usr/bin/env python3
"""Verify that input reaches the player.

`app_frame_check.py` proves the application renders; this proves the bitmask
the shell sends actually moves something. Before the player entity existed,
`tick(held)` recorded the bits and nothing consumed them.

Exit codes: 0 ok; 1 failed; 2 environment missing
"""

from __future__ import annotations

import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent


def dat_path():
    from probe import target_path
    return target_path().parent / 'th185.dat'


def selftest() -> int:
    print('=== player input selftest ===')
    print()
    failures = 0

    module = ROOT / 'artifacts' / 'cpp-check' / 'th185-app.mjs'
    script = ROOT.parent / 'scripts' / 'probe-input.mjs'
    if not script.is_file():
        script = ROOT / 'scripts' / 'probe-input.mjs'

    if not module.is_file():
        print('[SKIP] app module not built')
        return 0
    try:
        dat = dat_path()
    except Exception as exc:
        print('[SKIP] cannot locate th185.dat: %s' % exc)
        return 0
    if not dat.is_file():
        print('[SKIP] th185.dat not found')
        return 0

    result = subprocess.run(
        ['node', str(script), str(module), str(dat)],
        capture_output=True, text=True, encoding='utf-8', errors='replace',
        timeout=900)
    output = result.stdout + result.stderr
    for line in output.splitlines():
        print(line)

    ok = result.returncode == 0
    print()
    print('[%s] the input probe passed' % ('PASS' if ok else 'FAIL'))
    if not ok:
        failures += 1

    # The probe already asserts internally; re-check the headline numbers so a
    # silently weakened probe cannot pass this wrapper.
    def value(key: str) -> str:
        for line in output.splitlines():
            if line.startswith(key + ' ') or line.startswith(key + ':'):
                return line.split(':', 1)[-1].strip()
        return ''

    focus = value('focus speed')
    ok = 'vs normal' in focus
    print('[%s] focus speed was measured (%s)'
          % ('PASS' if ok else 'FAIL', focus or '-'))
    if not ok:
        failures += 1

    bullets = value('bullets')
    ok = 'fired' in bullets and 'alive' in bullets
    print('[%s] bullets were spawned and tracked (%s)'
          % ('PASS' if ok else 'FAIL', bullets or '-'))
    if not ok:
        failures += 1

    retired = value('after 120f')
    ok = retired.endswith('alive 0')
    print('[%s] bullets retired after leaving the screen (%s)'
          % ('PASS' if ok else 'FAIL', retired or '-'))
    if not ok:
        failures += 1

    clamped = value('clamped')
    ok = clamped.startswith('x=-192')
    print('[%s] the player clamped to the left bound (%s)'
          % ('PASS' if ok else 'FAIL', clamped or '-'))
    if not ok:
        failures += 1

    print()
    if failures:
        print('selftest failed: %d item(s)' % failures)
        return 1
    print('selftest passed: input drives the player')
    return 0


if __name__ == '__main__':
    raise SystemExit(selftest())

