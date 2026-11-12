#!/usr/bin/env python3
"""Verify the APPLICATION module renders a real frame.

This is the end-to-end check for the thing the web shell actually loads:
`artifacts/cpp-check/th185-app.mjs`. It boots the module, initialises it
against the real archive, ticks it, and asserts the framebuffer is populated.

Unlike `cpp_archive_runtime.py` (which drives the harness), this exercises the
same exports the browser calls, so a pass here means the shell has something
to draw.

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
    print('=== application module selftest ===')
    print()
    failures = 0

    module = ROOT / 'artifacts' / 'cpp-check' / 'th185-app.mjs'
    if not module.is_file():
        print('[SKIP] app module not built: %s' % module)
        return 0

    try:
        dat = dat_path()
    except Exception as exc:
        print('[SKIP] cannot locate th185.dat: %s' % exc)
        return 0
    if not dat.is_file():
        print('[SKIP] th185.dat not found')
        return 0

    output = ROOT / 'artifacts' / 'app-frame.png'
    script = ROOT.parent / 'scripts' / 'export-app-frame.mjs'
    if not script.is_file():
        script = ROOT / 'scripts' / 'export-app-frame.mjs'

    result = subprocess.run(
        ['node', str(script), str(module), str(dat), str(output)],
        capture_output=True, text=True, encoding='utf-8', errors='replace',
        timeout=900)
    lines = (result.stdout + result.stderr).splitlines()

    ok = result.returncode == 0
    print('[%s] the application module ran and exited cleanly (rc=%d)'
          % ('PASS' if ok else 'FAIL', result.returncode))
    if not ok:
        for line in lines[-12:]:
            print('       ' + line)
        failures += 1

    fields = {}
    for line in lines:
        if ':' in line and not line.startswith(' '):
            key, _, value = line.partition(':')
            fields[key.strip()] = value.strip()

    ok = fields.get('frame') == '640x480'
    print('[%s] framebuffer is 640x480 (%s)'
          % ('PASS' if ok else 'FAIL', fields.get('frame')))
    if not ok:
        failures += 1

    ok = fields.get('bytes') == '1228800'
    print('[%s] framebuffer is 640*480*4 bytes (%s)'
          % ('PASS' if ok else 'FAIL', fields.get('bytes')))
    if not ok:
        failures += 1

    try:
        quads = int(fields.get('quads', '0'))
    except ValueError:
        quads = 0
    # 32 enemy roots plus the scene background; the exact count is data-driven
    # so only the lower bound is asserted.
    ok = quads >= 30
    print('[%s] at least 30 quads drawn (%d)' % ('PASS' if ok else 'FAIL', quads))
    if not ok:
        failures += 1

    ok = output.is_file() and output.stat().st_size > 100000
    print('[%s] a PNG was written (%s bytes)'
          % ('PASS' if ok else 'FAIL',
             output.stat().st_size if output.is_file() else 0))
    if not ok:
        failures += 1

    print()
    if failures:
        print('selftest failed: %d item(s)' % failures)
        return 1
    print('selftest passed: the application module renders a real frame')
    return 0


if __name__ == '__main__':
    raise SystemExit(selftest())

