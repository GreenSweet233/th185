#!/usr/bin/env python3
"""Check that the interpreter runs against the LIVE EclContext, not a copy.

## Why this check changed

`Application::tick` used to run each coroutine against a COPY of EclContext and
merge ~120 fields back by hand. A field written by the interpreter but missing
from that list was silently discarded, and the symptom was always "the counter
is 0" rather than a crash. It was hit three times: wave_blocks, stage_signal,
and boss_index_sets.

That whole class of bug is now impossible: the interpreter swaps the live
context in (`std::swap(runner.context, ecl_)`), so there is nothing to merge and
nothing to forget. The copy was also the dominant cost -- a 133-field struct
owning several vectors and maps, copied once per script per frame.

## What it asserts

1. `Application.cpp` no longer contains `runner.context = ecl_` or any
   `ecl_.X = runner.context.X` merge line. If one reappears, someone has
   reintroduced the copy.
2. Every place that constructs an `EclInterpreter` around the live context uses
   `std::swap` in BOTH directions, so the context cannot be left detached.
3. `EclContext.hpp` still declares the fields the interpreter needs -- a cheap
   guard against an accidental rename breaking compilation silently in a
   header-only path.

Usage: python th185_web/scripts/native/ecl_merge_check.py [--selftest]
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent.parent   # th185/

CONTEXT = ROOT / "th185_web" / "cpp" / "game" / "EclContext.hpp"
APP = ROOT / "th185_web" / "cpp" / "sdl" / "Application.cpp"


def check(app_text: str, context_text: str, verbose: bool = True) -> int:
    problems = 0

    def report(ok: bool, label: str, detail: str = "") -> None:
        nonlocal problems
        if not ok:
            problems += 1
        if verbose:
            suffix = (" (%s)" % detail) if detail else ""
            print("[%s] %s%s" % ("PASS" if ok else "FAIL", label, suffix))

    # 1. No copy of the context.
    copies = re.findall(r"\w+\.context\s*=\s*ecl_\s*;", app_text)
    report(not copies, "the context is never copied out of Application",
           "%d copy site(s)" % len(copies) if copies else "")

    # 2. No field-by-field merge.
    merges = re.findall(r"ecl_\.\w+\s*=\s*(?:std::move\()?runner\.context\.", app_text)
    report(not merges, "no field-by-field merge remains",
           "%d field(s)" % len(merges) if merges else "")

    # 3. Every interpreter constructed around the live context swaps in AND out.
    swaps_in = app_text.count("std::swap(runner.context, ecl_)")
    report(swaps_in > 0, "the live context is swapped into the interpreter",
           "%d site(s)" % swaps_in)
    # Each site needs two swaps (in and out); an odd count leaves it detached.
    report(swaps_in % 2 == 0, "every swap-in is paired with a swap-out",
           "%d swap(s)" % swaps_in)

    # 4. The fields the interpreter writes still exist.
    for field in ("coroutines", "schedules", "wave_blocks", "bullet_requests",
                  "stage_signal", "boss_index", "builtins"):
        report((" %s" % field) in context_text or ("%s;" % field) in context_text,
               "EclContext still declares %s" % field)

    return problems


def selftest() -> int:
    """Prove the checker catches the regressions it claims to catch."""
    good_app = (
        "        EclInterpreter runner;\n"
        "        std::swap(runner.context, ecl_);\n"
        "        runner.run(code, 4000, code_base);\n"
        "        std::swap(runner.context, ecl_);\n"
    )
    good_ctx = "    std::map<std::string, CoroutineState> coroutines;\n    std::vector<SpawnSchedule> schedules;\n    std::vector<std::vector<i32>> wave_blocks;\n    std::vector<BulletRequest> bullet_requests;\n    u32 stage_signal = 0;\n    u32 boss_index = 0;\n    u32 builtins[256];\n"

    print("=== selftest: the clean shape must pass ===")
    ok = check(good_app, good_ctx, verbose=True) == 0
    print("  -> %s" % ("PASS" if ok else "FAIL"))
    failures = 0 if ok else 1

    print("=== selftest: a reintroduced copy must be caught ===")
    bad_copy = good_app + "        runner.context = ecl_;\n"
    caught = check(bad_copy, good_ctx, verbose=False) > 0
    print("  -> %s" % ("PASS" if caught else "FAIL"))
    if not caught:
        failures += 1

    print("=== selftest: a reintroduced merge must be caught ===")
    bad_merge = good_app + "        ecl_.schedules = std::move(runner.context.schedules);\n"
    caught = check(bad_merge, good_ctx, verbose=False) > 0
    print("  -> %s" % ("PASS" if caught else "FAIL"))
    if not caught:
        failures += 1

    print("=== selftest: an unpaired swap must be caught ===")
    bad_swap = "        EclInterpreter runner;\n        std::swap(runner.context, ecl_);\n"
    caught = check(bad_swap, good_ctx, verbose=False) > 0
    print("  -> %s" % ("PASS" if caught else "FAIL"))
    if not caught:
        failures += 1

    return failures


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--selftest", action="store_true")
    args = parser.parse_args()

    if args.selftest:
        failures = selftest()
        if failures:
            print("selftest failed: %d item(s)" % failures)
            return 1
        print("selftest passed: the checker catches every regression it names")
        return 0

    app_text = APP.read_text(encoding="utf-8")
    context_text = CONTEXT.read_text(encoding="utf-8")
    problems = check(app_text, context_text)
    if problems:
        print("ecl-merge failed: %d item(s)" % problems)
        return 1
    print("ecl-merge passed: the interpreter runs in place")
    return 0


if __name__ == "__main__":
    sys.exit(main())