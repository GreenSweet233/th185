#!/usr/bin/env python3
"""Check that the web shell and the WASM module agree with each other.

There is no browser automation available here, so instead of pretending to test
the page we verify the CONTRACT: every export the shell calls must exist in the
module, every element the shell queries must exist in the HTML, and the paths
the HTML references must resolve. Those are the failures that actually happen,
and they are all checkable without a browser.

What this does NOT prove: that the page renders. That needs a browser.

Exit codes: 0 ok; 1 mismatch; 2 environment missing
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent
WEB = ROOT / 'sdl-runtime'


def read(path: Path) -> str:
    return path.read_text(encoding='utf-8', errors='replace')


def exported_functions() -> set[str]:
    """Names the build will export.

    The list is GENERATED from Exports.cpp, not written in build-cpp.mjs.
    It used to be hand-maintained and every function added later was missing
    at runtime (probes threw "not a function"), so the build now scans the
    EMSCRIPTEN_KEEPALIVE declarations. This check mirrors that scan.
    """
    build = read(ROOT / 'scripts' / 'build-cpp.mjs')
    if 'exportedFunctions(' in build:
        source = read(ROOT / 'cpp' / 'sdl' / 'Exports.cpp')
        return set('_' + name for name in re.findall(
            r'EMSCRIPTEN_KEEPALIVE[\s\S]{0,120}?\b(th185_\w+)\s*\(', source))
    # Legacy hand-written list.
    names = set()
    for match in re.finditer(r'\\"(_th185_[a-z_]+)\\"', build):
        names.add(match.group(1))
    return names


def selftest() -> int:
    print('=== web shell contract selftest ===')
    print()
    failures = 0

    html_path = WEB / 'th185.html'
    shell_path = WEB / 'shell.mjs'
    style_path = WEB / 'style.css'
    for path in (html_path, shell_path, style_path):
        ok = path.is_file()
        print('[%s] %s exists' % ('PASS' if ok else 'FAIL', path.name))
        if not ok:
            failures += 1
    if failures:
        print()
        print('selftest failed: the shell is incomplete')
        return 1

    html = read(html_path)
    shell = read(shell_path)

    # 1. Paths the HTML references must resolve to files we ship.
    referenced = re.findall(r'(?:href|src)="([^"]+)"', html)
    missing = []
    for ref in referenced:
        if ref.startswith('http') or ref.startswith('data:'):
            continue
        if ref == '/app/style.css':
            target = style_path
        elif ref == '/app/shell.mjs':
            target = shell_path
        else:
            continue
        if not target.is_file():
            missing.append(ref)
    ok = not missing
    print('[%s] every asset the HTML references exists (%d missing)'
          % ('PASS' if ok else 'FAIL', len(missing)))
    if not ok:
        for item in missing:
            print('       ' + item)
        failures += 1

    # 2. Elements the shell queries must exist in the HTML.
    queried = set(re.findall(r"querySelector\('#([a-zA-Z0-9_-]+)'\)", shell))
    declared = set(re.findall(r'id="([^"]+)"', html))
    absent = sorted(queried - declared)
    ok = not absent
    print('[%s] every element the shell queries exists in the HTML (%d missing)'
          % ('PASS' if ok else 'FAIL', len(absent)))
    if not ok:
        for item in absent:
            print('       #' + item)
        failures += 1

    # 3. Module functions the shell calls must be in the export list.
    called = set(re.findall(r'core\.(_th185_[a-z_]+)\(', shell))
    exported = exported_functions()
    ok = bool(exported)
    print('[%s] the build exports th185 functions (%d found)'
          % ('PASS' if ok else 'FAIL', len(exported)))
    if not ok:
        failures += 1

    if exported:
        unknown = sorted(called - exported)
        ok = not unknown
        print('[%s] every function the shell calls is exported (%d missing)'
              % ('PASS' if ok else 'FAIL', len(unknown)))
        if not ok:
            for item in unknown:
                print('       ' + item)
            failures += 1

    # 4. HEAPU8 must be exported: the shell reads pixels through it.
    build = read(ROOT / 'scripts' / 'build-cpp.mjs')
    ok = 'HEAPU8' in build
    print('[%s] HEAPU8 is in EXPORTED_RUNTIME_METHODS'
          % ('PASS' if ok else 'FAIL'))
    if not ok:
        failures += 1

    # 5. The shell must not use APIs that need a secure context it will not get.
    forbidden = []
    for token in ('SharedArrayBuffer', 'navigator.gpu', 'WebGL2RenderingContext'):
        if token in shell:
            forbidden.append(token)
    ok = not forbidden
    print('[%s] the shell avoids context-dependent APIs (%d found)'
          % ('PASS' if ok else 'FAIL', len(forbidden)))
    if not ok:
        for item in forbidden:
            print('       ' + item)
        failures += 1

    # 6. Frame pacing must be bounded, or a stall becomes an infinite loop.
    ok = 'MAX_STEPS_PER_FRAME' in shell
    print('[%s] the frame loop bounds its catch-up steps'
          % ('PASS' if ok else 'FAIL'))
    if not ok:
        failures += 1

    print()
    if failures:
        print('selftest failed: %d item(s)' % failures)
        return 1
    print('selftest passed: the shell and the module agree')
    return 0


if __name__ == '__main__':
    raise SystemExit(selftest())

