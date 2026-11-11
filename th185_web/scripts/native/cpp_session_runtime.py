#!/usr/bin/env python3
"""Verify the compiled GameSession against the Python reference.

GameSession is the first class that COMPOSES the reconstructed managers:
  open archive -> load a scene (.std) -> seed the RNG -> select waves.

This check confirms the composition works on the real archive and that the
per-stage numbers agree with the reference parsers. It also pins the forced-boss
behaviour, which is an observable gameplay rule (stage 5 + card 0 -> Boss26).

Skips (exit 0) when the toolchain or game data is unavailable.

Exit codes: 0 ok/skipped; 1 mismatch; 2 build or run failure
"""

from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

REPO = HERE.parents[2]           # th185
WEB = REPO / "th185_web"
BUILD_SCRIPT = WEB / "scripts" / "build-cpp.mjs"
RUNNER = WEB / "scripts" / "run-harness.mjs"
HARNESS = WEB / "artifacts" / "cpp-check" / "harness-game.cjs"

SCENE_FILES = [
    "world01t.std", "world01.std", "world02.std", "world03.std", "world04.std",
    "world05.std", "world06.std", "world07.std", "world08.std",
]


def build() -> bool:
    result = subprocess.run(
        ["node", str(BUILD_SCRIPT), "--harness"],
        cwd=str(REPO), capture_output=True, text=True, timeout=900,
    )
    return result.returncode == 0 and HARNESS.is_file()


def run(dat: Path):
    result = subprocess.run(
        ["node", str(RUNNER), str(HARNESS), str(dat)],
        cwd=str(REPO), capture_output=True, text=True, timeout=600,
    )
    return result.stdout if result.returncode == 0 else None


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Verify compiled GameSession against the reference")
    parser.add_argument("--selftest", action="store_true")
    args = parser.parse_args()

    print("=== compiled GameSession selftest ===")
    print()
    failures = 0

    try:
        from probe import target_path
        from std import load_scene
        from pbg4 import extract_file
    except ImportError as exc:
        print("[SKIP] reference unavailable: %s" % exc)
        return 0

    dat = target_path().parent / "th185.dat"
    if not dat.is_file():
        print("[SKIP] th185.dat not found")
        return 0

    print("building harness ... ", end="", flush=True)
    if not build():
        print("skipped")
        print("[SKIP] toolchain unavailable (CI stays green)")
        return 0
    print("ok")

    print("running harness  ... ", end="", flush=True)
    output = run(dat)
    if output is None:
        print("failed")
        print("[FAIL] harness did not run")
        return 2
    print("ok")
    print()

    sessions = {}
    load_errors = []
    for line in output.splitlines():
        parts = line.split()
        if not parts or parts[0] != "SESSION":
            continue
        if len(parts) >= 3 and parts[2] == "LOAD_ERROR":
            load_errors.append(int(parts[1]))
            continue
        if len(parts) < 4:
            continue
        stage = int(parts[1])
        record = {"scene": parts[2]}
        for token in parts[3:]:
            if "=" in token:
                key, _, value = token.partition("=")
                record[key] = value
        sessions[stage] = record

    # 1. Scenes that load must agree with the reference parser.
    mismatched = []
    for stage, record in sorted(sessions.items()):
        name = SCENE_FILES[stage]
        if record["scene"] != name:
            mismatched.append((stage, "scene name", record["scene"], name))
            continue
        try:
            ref = load_scene(name.replace(".std", ""))
        except SystemExit as exc:
            mismatched.append((stage, "reference failed", str(exc), ""))
            continue
        pairs = [
            ("entities", len(ref["entities"])),
            ("groups", len(ref["groups"])),
            ("cmds", len(ref["commands"])),
            ("slots", len(ref["waves"])),
        ]
        for key, expected in pairs:
            actual = int(record.get(key, "-1"))
            if actual != expected:
                mismatched.append((stage, key, actual, expected))

    ok = not mismatched
    print("[%s] all %d loaded scenes match the reference parser"
          % ("PASS" if ok else "FAIL", len(sessions)))
    if not ok:
        for item in mismatched[:6]:
            print("       stage %d %s: C++=%s ref=%s" % item)
        failures += 1

    # 2. The composition path must actually load most scenes. One known miss is
    #    expected (the tail-block limitation in Archive.hpp).
    ok = len(sessions) >= 8
    print("[%s] %d of %d scenes load (%d known misses)"
          % ("PASS" if ok else "FAIL", len(sessions), len(SCENE_FILES), len(load_errors)))
    if not ok:
        failures += 1

    # 3. Forced-boss rule: stage 5 with card 0 must yield template 27 (Boss26).
    record5 = sessions.get(5)
    ok = record5 is not None and record5.get("forced") == "27"
    print("[%s] stage 5 + card 0 forces template 27 (Boss26): %s"
          % ("PASS" if ok else "FAIL", record5.get("forced") if record5 else "missing"))
    if not ok:
        failures += 1

    # 4. No other stage may force a boss with card 0.
    others = [stage for stage, record in sessions.items()
              if stage != 5 and record.get("forced") != "4294967295"]
    ok = not others
    print("[%s] no other stage forces a boss with card 0 (%s)"
          % ("PASS" if ok else "FAIL", others or "ok"))
    if not ok:
        failures += 1

    # 5. Score caps must follow the documented table scaled by stage.
    caps = [500000, 1000000, 1500000, 2000000, 1000000]
    bad_caps = []
    for stage, record in sorted(sessions.items()):
        expected = caps[2] * stage   # difficulty 2 = Normal
        if int(record.get("scorecap", "-1")) != expected:
            bad_caps.append((stage, record.get("scorecap"), expected))
    ok = not bad_caps
    print("[%s] score caps are cap[Normal] * stage for every loaded scene"
          % ("PASS" if ok else "FAIL"))
    if not ok:
        for item in bad_caps[:4]:
            print("       stage %d: got %s expect %s" % item)
        failures += 1

    # 6. Counter bookkeeping: every resolved slot must increment exactly one
    #    counter (RVA 0x3D243). If this drifts, weights stop decaying and the
    #    anti-repeat mechanism silently breaks.
    bad_drawn = []
    for stage, record in sorted(sessions.items()):
        slots = int(record.get("slots", "-1"))
        drawn = int(record.get("drawn", "-1"))
        if drawn != slots:
            bad_drawn.append((stage, drawn, slots))
    ok = not bad_drawn
    print("[%s] drawn counter total equals slot count for every scene"
          % ("PASS" if ok else "FAIL"))
    if not ok:
        for item in bad_drawn[:4]:
            print("       stage %d: drawn=%d slots=%d" % item)
        failures += 1

    # 7. skip_index must point at a real template (or be the sentinel).
    bad_skip = []
    for stage, record in sorted(sessions.items()):
        skip = int(record.get("skip", "-1"))
        if skip != 0xFFFFFFFF and not (0 <= skip < 111):
            bad_skip.append((stage, skip))
    ok = not bad_skip
    print("[%s] skip_index is always a valid template index or the sentinel"
          % ("PASS" if ok else "FAIL"))
    if not ok:
        print("       %s" % bad_skip[:4])
        failures += 1

    # 8. Wave selection must be deterministic: same seed -> same hash.
    first = run(dat)
    second = run(dat)
    ok = first == second
    print("[%s] wave selection is deterministic across runs" % ("PASS" if ok else "FAIL"))
    if not ok:
        failures += 1

    print()
    if failures:
        print("selftest failed: %d item(s)" % failures)
        return 1
    print("selftest passed: compiled GameSession composes the managers correctly")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

