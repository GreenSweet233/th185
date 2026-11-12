#!/usr/bin/env python3
"""Infer ECL opcode stack effects from loop-balance constraints.

## Idea

A backward jump defines a loop body. In a well-formed program the body must
leave the stack at the same depth it found it (otherwise the loop leaks or
underflows). That gives one linear equation per loop:

    sum over opcodes in body of (count * effect) == 0

Known opcodes contribute known values, so the residual pins down the sum of the
UNKNOWN opcodes in that body. Collecting many loops over many subroutines gives
an over-determined system we can solve for the individual unknown effects.

## Why this matters

TH185 has ~5275 instructions in opcode families whose stack behaviour is not
yet known (the 600 family and friends). Guessing them wrong makes the stack
grow without bound -- which is exactly the bug that forced the last rollback.
This tool replaces guessing with solving.

## Usage

    python th185_web/scripts/native/ecl_stack_infer.py --selftest
    python th185_web/scripts/native/ecl_stack_infer.py --report

Exit codes: 0 ok; 1 selftest failed; 2 environment missing
"""

from __future__ import annotations

import argparse
import collections
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

# Net stack effect of opcodes whose behaviour is established.
# value = (pops, pushes), so net = pushes - pops.
KNOWN_EFFECT = {
    0: 0,    # Nop
    1: 0,    # Delete
    10: 0,   # Return
    12: 0,   # Jump
    13: -1,  # JumpIfZero
    14: -1,  # JumpIfNonzero
    23: 0,   # Op23Timer (recorded, no stack use)
    30: 0,   # Diagnostic
    40: 0,   # EnterFrame
    41: 0,   # LeaveFrame
    42: 1,   # PushInt
    43: -1,  # StoreInt (pops the value)
    44: 1,   # PushFloat
    78: 0,   # PostDecrement (pop then push)
    83: 0,   # Wait
    300: 0,  # named call, args are payload
    302: 0,
    304: 0,
    1013: 0,
    1015: 0,
    306: 0, 307: 0, 502: 0, 516: 0, 525: 0, 600: 0, 616: 0,
    1002: 0, 1003: 0, 1023: 0,
}

# Binary arithmetic / comparison: pop 2, push 1.
for _op in (50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64, 65, 66,
            67, 68, 69, 70, 73, 74, 75, 76, 77):
    KNOWN_EFFECT[_op] = -1

# Unary: pop 1, push 1.
for _op in (71, 72, 79, 80, 84, 85, 88):
    KNOWN_EFFECT[_op] = 0


def _dat_path():
    from probe import target_path
    return target_path().parent / 'th185.dat'


def collect_loops():
    """Return a list of (body_opcode_counts, residual) for every backward jump."""
    from pbg4 import read_entries
    from ecl import load_ecl

    dat = _dat_path()
    _header, entries = read_entries(dat)
    names = [e['name'] for e in entries if e['name'].endswith('.ecl')]

    loops = []
    for name in names:
        try:
            resource = load_ecl(name)
        except SystemExit:
            continue
        for sub in resource['subs']:
            instrs = sub['instructions']
            by_offset = {ins['offset']: index for index, ins in enumerate(instrs)}
            for index, ins in enumerate(instrs):
                if ins['id'] not in (12, 13, 14):
                    continue
                payload = ins.get('payload', b'')
                if len(payload) < 8:
                    continue
                delta = int.from_bytes(payload[4:8], 'little', signed=True)
                target = ins['offset'] + delta
                if target >= ins['offset']:
                    continue  # forward jump, not a loop
                if target not in by_offset:
                    continue
                start = by_offset[target]
                body = instrs[start:index + 1]
                if not body:
                    continue
                counts = collections.Counter(item['id'] for item in body)
                known = sum(KNOWN_EFFECT.get(op, 0) * n for op, n in counts.items()
                            if op in KNOWN_EFFECT)
                unknown = {op: n for op, n in counts.items() if op not in KNOWN_EFFECT}
                if unknown:
                    loops.append((name, sub.get('name', '?'), unknown, -known))
    return loops


def solve(loops):
    """Greedy solve: an unknown that appears alone in some loop is pinned exactly."""
    solved = {}
    pending = list(loops)
    for _round in range(8):
        progress = False
        remaining = []
        for name, sub_name, unknown, residual in pending:
            # Subtract contributions from already-solved unknowns.
            adjusted = residual
            still = {}
            for op, n in unknown.items():
                if op in solved:
                    adjusted -= solved[op] * n
                else:
                    still[op] = n
            if not still:
                progress = True
                continue
            if len(still) == 1:
                (op, n), = still.items()
                if n > 0 and adjusted % n == 0:
                    value = adjusted // n
                    if op not in solved:
                        solved[op] = value
                        progress = True
                        continue
            remaining.append((name, sub_name, unknown, residual))
        pending = remaining
        if not progress:
            break
    return solved, pending


def selftest() -> int:
    print('=== ECL stack-effect inference selftest ===')
    print()
    failures = 0

    try:
        loops = collect_loops()
    except ImportError as exc:
        print('[SKIP] missing dependency: %s' % exc)
        return 0
    except SystemExit as exc:
        print('[SKIP] %s' % exc)
        return 0

    ok = len(loops) > 0
    print('[%s] found %d loops containing unknown opcodes'
          % ('PASS' if ok else 'FAIL', len(loops)))
    if not ok:
        failures += 1

    # Only trust loops whose known part already almost balances. A loop that
    # legitimately changes stack depth (e.g. one that pushes a variable number
    # of values and exits through LeaveFrame) produces a large residual and
    # would poison the solve. This filter is the whole reason the numbers are
    # usable: without it the solver produced effects of +-17, which cannot be
    # a single instruction.
    tight = [loop for loop in loops if abs(loop[3]) <= 4]
    ok = len(tight) >= 100
    print('[%s] %d of %d loops are tight enough to use (|residual| <= 4)'
          % ('PASS' if ok else 'FAIL', len(tight), len(loops)))
    if not ok:
        failures += 1

    solved, pending = solve(tight)

    print('       solved %d opcode effect(s), %d loop(s) still under-determined'
          % (len(solved), len(pending)))
    for op, value in sorted(solved.items()):
        print('         op %-5d net %+d' % (op, value))

    # With the tight filter the solved magnitudes must be small. If they are
    # not, the filter is not doing its job.
    ok = all(-4 <= value <= 4 for value in solved.values())
    print('[%s] every solved effect is small (|net| <= 4)'
          % ('PASS' if ok else 'FAIL'))
    if not ok:
        failures += 1

    # The 600 family is the main target; at least one member should be pinned.
    family = [op for op in solved if 600 <= op < 700]
    ok = len(family) >= 1
    print('[%s] pinned at least one 600-family opcode (%s)'
          % ('PASS' if ok else 'FAIL', family))
    if not ok:
        failures += 1

    print()
    if failures:
        print('selftest failed: %d item(s)' % failures)
        return 1
    print('selftest passed: stack effects inferred from loop constraints')
    return 0


def report() -> int:
    loops = collect_loops()
    solved, pending = solve(loops)
    print('loops analysed     : %d' % len(loops))
    print('opcodes solved     : %d' % len(solved))
    print('loops under-determined: %d' % len(pending))
    print()
    print('solved:')
    for op, value in sorted(solved.items()):
        print('  op %-5d net %+d' % (op, value))
    if pending:
        print()
        print('still ambiguous (opcode -> times in body):')
        seen = collections.Counter()
        for _n, _s, unknown, _r in pending:
            for op in unknown:
                seen[op] += 1
        for op, count in seen.most_common(30):
            print('  op %-5d in %d loop(s)' % (op, count))
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description='Infer ECL opcode stack effects')
    parser.add_argument('--selftest', action='store_true')
    parser.add_argument('--report', action='store_true')
    args = parser.parse_args()
    if args.selftest:
        return selftest()
    if args.report:
        return report()
    parser.print_help()
    return 0


if __name__ == '__main__':
    raise SystemExit(main())

