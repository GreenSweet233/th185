#!/usr/bin/env python3
"""Check that every mutable EclContext field is merged back after a coroutine run.

## Why

`Application::tick` runs the scene controller against a COPY of EclContext and
merges back only the fields listed in `std::move(runner.context.X)`. A field
that is written by the interpreter but missing from that list is silently
discarded -- and the symptom is always "the counter is 0" rather than a crash.

That trap was hit twice (wave_blocks, then stage_signal), so this check exists
to make the third time impossible.

## What it does

1. Parse `cpp/game/EclContext.hpp` for the members of `struct EclContext`.
2. Parse `cpp/sdl/Application.cpp` for `ecl_.X = ... runner.context.X`.
3. Report any field that the interpreter writes (recorded in `record_op`,
   the `schedules` vector, the bullet-request counters, ...) but is not merged.

Read-only fields (frames, builtins, subroutines) are exempt: the callee is not
expected to change them.

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

# Fields the callee never writes, so they need no merge.
READ_ONLY = {
    "owner", "builtins", "subroutines", "frames", "recorded_total",
    "first_unsupported_opcode", "short_pops",
}


def parse_context_fields(text: str):
    """Member names declared DIRECTLY inside `struct EclContext { ... }`.

    Nested structs (BulletRequest, CoroutineState, SpawnSchedule, ...) are
    skipped: their members belong to those types, not to the context, and
    several are merged indirectly through the vectors that hold them.
    """
    start = text.find("struct EclContext {")
    if start < 0:
        raise SystemExit("EclContext struct not found")

    # Walk from the opening brace, tracking brace depth so nested structs are
    # skipped wholesale.
    brace = text.find("{", start)
    if brace < 0:
        raise SystemExit("EclContext has no body")

    depth = 0
    end = -1
    for i in range(brace, len(text)):
        if text[i] == "{":
            depth += 1
        elif text[i] == "}":
            depth -= 1
            if depth == 0:
                end = i
                break
    if end < 0:
        raise SystemExit("EclContext struct is not terminated")

    body = text[brace + 1:end]

    fields = []
    depth = 0
    for line in body.splitlines():
        stripped = line.strip()

        # A nested type opens a scope: record nothing from inside it.
        if depth > 0:
            depth += stripped.count("{") - stripped.count("}")
            if depth < 0:
                depth = 0
            continue

        if not stripped or stripped.startswith("//"):
            continue

        if re.match(r"(struct|class|enum|union)\b", stripped):
            # Either a full definition (opens a brace) or a forward use.
            if "{" in stripped:
                depth = stripped.count("{") - stripped.count("}")
            continue

        if re.match(r"(void|static|constexpr|using|typedef|friend)\b", stripped):
            continue
        if "(" in stripped:
            continue

        match = re.match(r"^[\w:<>,\s]+?\s+(\w+)\s*(=|;|\{)", stripped)
        if match:
            name = match.group(1)
            if name in ("return", "public", "private", "protected"):
                continue
            fields.append(name)
    return fields


def parse_merged_fields(text: str):
    """Field names on the right of `ecl_.X = ... runner.context.X`."""
    merged = set()
    for match in re.finditer(r"ecl_\.(\w+)\s*=\s*(?:std::move\()?runner\.context\.(\w+)", text):
        merged.add(match.group(2))
    return merged


def check():
    context_text = CONTEXT.read_text(encoding="utf-8")
    app_text = APP.read_text(encoding="utf-8")

    fields = parse_context_fields(context_text)
    merged = parse_merged_fields(app_text)

    if not fields:
        print("[FAIL] no EclContext fields parsed")
        return 1
    if not merged:
        print("[FAIL] no merged fields parsed")
        return 1

    missing = [f for f in fields if f not in merged and f not in READ_ONLY]
    extra = [f for f in merged if f not in fields]

    print("[%s] EclContext fields parsed (%d)" % ("PASS" if fields else "FAIL", len(fields)))
    print("[%s] merge list parsed (%d)" % ("PASS" if merged else "FAIL", len(merged)))

    ok = True
    if missing:
        ok = False
        print("[FAIL] fields written by the interpreter but never merged: %s"
              % ", ".join(missing))
        print("       add `ecl_.X = std::move(runner.context.X);` in Application::tick")
    else:
        print("[PASS] every mutable field is merged back")

    if extra:
        ok = False
        print("[FAIL] merge list names fields that do not exist: %s" % ", ".join(extra))
    else:
        print("[PASS] the merge list names no stale fields")

    return 0 if ok else 1


def selftest():
    """Prove the parser catches a missing field, not just that it runs."""
    sample_context = """
struct EclContext {
    u32 kept = 0;
    u32 dropped = 0;
    u32 read_only_thing = 0;
};
"""
    sample_app = """
    ecl_.kept = std::move(runner.context.kept);
"""

    fields = parse_context_fields(sample_context)
    merged = parse_merged_fields(sample_app)
    missing = [f for f in fields if f not in merged and f not in READ_ONLY]

    ok = True
    if "dropped" not in missing:
        ok = False
        print("[FAIL] the parser did not notice the unmerged field")
    else:
        print("[PASS] the parser notices an unmerged field")

    if "kept" in missing:
        ok = False
        print("[FAIL] the parser flagged a merged field")
    else:
        print("[PASS] the parser accepts a merged field")

    return 0 if ok else 1


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--selftest", action="store_true",
                        help="check the parser itself")
    args = parser.parse_args()

    if args.selftest:
        return selftest()
    return check()


if __name__ == "__main__":
    sys.exit(main())
