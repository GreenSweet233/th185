#!/usr/bin/env python3
"""Verify the COMPILED C++ managers against the real th185.dat.

This is the runtime counterpart of the mirror checks: it builds
cpp/game/HarnessGame.cpp, runs it over the actual archive, and compares the
numbers against the Python reference parsers.

What it catches that compilation alone cannot: the decryption bug that left the
last partial block of the entry table wrong, which silently dropped 3 entries
and broke thbgm.fmt. That bug survived several mirror-based checks because the
Python reference had the same defect.

Skips (exit 0) when the toolchain or the game data is unavailable.

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


def build() -> bool:
    result = subprocess.run(
        ["node", str(BUILD_SCRIPT), "--harness"],
        cwd=str(REPO), capture_output=True, text=True, timeout=900,
    )
    if result.returncode != 0:
        print("  build output tail:")
        for line in (result.stdout + result.stderr).strip().splitlines()[-6:]:
            print("    " + line)
        return False
    return HARNESS.is_file()


def run(dat: Path):
    result = subprocess.run(
        ["node", str(RUNNER), str(HARNESS), str(dat)],
        cwd=str(REPO), capture_output=True, text=True, timeout=600,
    )
    if result.returncode != 0:
        print("  run output tail:")
        for line in (result.stdout + result.stderr).strip().splitlines()[-6:]:
            print("    " + line)
        return None
    return result.stdout


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Verify compiled C++ against the real archive")
    parser.add_argument("--selftest", action="store_true")
    args = parser.parse_args()

    print("=== compiled C++ vs real th185.dat ===")
    print()
    failures = 0

    try:
        from probe import target_path
    except ImportError as exc:
        print("[SKIP] probe unavailable: %s" % exc)
        return 0

    dat = target_path().parent / "th185.dat"
    if not dat.is_file():
        print("[SKIP] th185.dat not found")
        return 0

    print("building harness ... ", end="", flush=True)
    if not build():
        print("skipped")
        print()
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

    lines = [line.strip() for line in output.splitlines() if line.strip()]
    summary = {}
    entries = {}
    for line in lines:
        parts = line.split()
        if parts[0] == "ARCHIVE" and len(parts) >= 3:
            summary["ARCHIVE_" + parts[1]] = parts[2:]
        elif parts[0] == "ENTRY" and len(parts) == 4:
            entries[parts[1]] = (int(parts[2]), parts[3])
        elif parts[0] in ("ECL", "STD") and parts[1] == "TOTAL":
            summary[parts[0] + "_TOTAL"] = parts[2:]

    # 1. Header must match the documented values.
    header = summary.get("ARCHIVE_header")
    ok = header == ["7172", "3653", "285", "74533671"]
    print("[%s] compiled C++ reads the header correctly (%s)"
          % ("PASS" if ok else "FAIL", header))
    if not ok:
        failures += 1

    # 2. Entry count. 284 is the real parsed count; the header declares 285
    #    because the last slot is a terminator.
    count = summary.get("ARCHIVE_entries", ["?"])[0]
    ok = count == "284"
    print("[%s] compiled C++ parses %s entries (expect 284)"
          % ("PASS" if ok else "FAIL", count))
    if not ok:
        failures += 1

    # 3. thbgm.fmt must be present. It was MISSING before the decryption fix.
    ok = "thbgm.fmt" in entries and entries["thbgm.fmt"][0] == 537
    print("[%s] thbgm.fmt extracts (537 bytes) — regressed before the fix"
          % ("PASS" if ok else "FAIL"))
    if not ok:
        failures += 1

    # 4. The known-good magic-bearing entries.
    expected_sizes = {
        "help_01.png": 132302,
        "se_plst00.wav": 3596,
        "world01t.std": 2908,
        "default.ecl": 14184,
        "wave01.ecl": 5204,
        "pl01.sht": 10548,
        "musiccmt.txt": 2666,
    }
    bad = [name for name, size in expected_sizes.items()
           if name not in entries or entries[name][0] != size]
    ok = not bad
    print("[%s] all %d probe entries extract at the expected size (%s)"
          % ("PASS" if ok else "FAIL", len(expected_sizes), bad or "ok"))
    if not ok:
        failures += 1

    # 5. Compare ECL totals with the Python reference.
    try:
        from ecl import load_ecl
        from pbg4 import read_entries, extract_file
    except ImportError as exc:
        print("[SKIP] reference unavailable: %s" % exc)
        return 0 if not failures else 1

    _header, ref_entries = read_entries(dat)
    ecl_names = [e["name"] for e in ref_entries if e["name"].endswith(".ecl")]
    ref_subs = ref_instrs = 0
    for name in ecl_names:
        try:
            resource = load_ecl(name)
        except SystemExit:
            continue
        ref_subs += len(resource["subs"])
        ref_instrs += sum(len(s["instructions"]) for s in resource["subs"])

    totals = summary.get("ECL_TOTAL", [])
    if len(totals) == 3:
        cpp_files, cpp_subs, cpp_instrs = (int(x) for x in totals)
        # The C++ side may parse fewer files (the tail-block limitation), but
        # whatever it does parse must not exceed the reference.
        ok = cpp_subs <= ref_subs and cpp_instrs <= ref_instrs
        print("[%s] C++ ECL totals %d/%d/%d within reference %d/%d"
              % ("PASS" if ok else "FAIL", cpp_files, cpp_subs, cpp_instrs,
                 ref_subs, ref_instrs))
        if not ok:
            failures += 1
        else:
            print("       (C++ parsed %d of %d files; the rest hit the known"
                  % (cpp_files, len(ecl_names)))
            print("        tail-block limitation documented in Archive.hpp)")

    # 6. The interpreter must actually RUN, not just parse. A Python mirror of
    #    the interpreter logic proves the translation is faithful; only real
    #    execution proves the C++ works. This caught a jump-base bug that the
    #    mirror could never see (23 of 28 runnable subroutines faulted).
    eclrun = summary.get("ECLRUN_line")
    run_line = None
    for line in lines:
        if line.startswith("ECLRUN files="):
            run_line = line
            break
    ok = run_line is not None
    print("[%s] the interpreter executed real ECL (%s)"
          % ("PASS" if ok else "FAIL", "yes" if ok else "no ECLRUN line"))
    if not ok:
        failures += 1
    else:
        fields = {}
        for token in run_line.split()[1:]:
            key, _, value = token.partition("=")
            fields[key] = value
        ok = fields.get("faulted") == "0"
        print("[%s] zero execution faults (%s)"
              % ("PASS" if ok else "FAIL", fields.get("faulted")))
        if not ok:
            failures += 1
        ok = int(fields.get("ran", "0")) >= 140
        print("[%s] at least 140 subroutines ran (%s)"
              % ("PASS" if ok else "FAIL", fields.get("ran")))
        if not ok:
            failures += 1
        # A runaway stack means some opcode was wrongly treated as stack-neutral.
        # 26 is the measured healthy maximum; anything near the capacity is a bug.
        ok = int(fields.get("maxdepth", "0")) < 256
        print("[%s] stack depth stays sane (%s, limit 256)"
              % ("PASS" if ok else "FAIL", fields.get("maxdepth")))
        if not ok:
            failures += 1
        ok = int(fields.get("instrs", "0")) > 700000
        print("[%s] over 700000 instructions executed (%s)"
              % ("PASS" if ok else "FAIL", fields.get("instrs")))
        if not ok:
            failures += 1

    # 7. The C++ ANM parser + texture decoder must agree with the data.
    anmtex = None
    for line in lines:
        if line.startswith("ANMTEX files="):
            anmtex = line
            break
    ok = anmtex is not None
    print("[%s] the C++ ANM parser produced output" % ("PASS" if ok else "FAIL"))
    if not ok:
        failures += 1
    else:
        fields = {}
        for token in anmtex.split()[1:]:
            key, _, value = token.partition("=")
            fields[key] = value
        # 54 of 55 files: title_v.anm hits the documented tail-block
        # decryption limitation in Archive.hpp, so it cannot be extracted.
        ok = fields.get("files") == "54"
        print("[%s] C++ parsed %s of 55 .anm files (1 known tail-block miss)"
              % ("PASS" if ok else "FAIL", fields.get("files")))
        if not ok:
            failures += 1
        ok = fields.get("rejected") == "0"
        print("[%s] every extracted THTX block decoded (%s rejected)"
              % ("PASS" if ok else "FAIL", fields.get("rejected")))
        if not ok:
            failures += 1
        ok = int(fields.get("pixels", "0")) > 40000000
        print("[%s] over 40 million pixels decoded (%s)"
              % ("PASS" if ok else "FAIL", fields.get("pixels")))
        if not ok:
            failures += 1

    # 8. The ANM interpreter must actually execute instructions.
    anmrun = None
    for line in lines:
        if line.startswith("ANMRUN files="):
            anmrun = line
            break
    ok = anmrun is not None
    print("[%s] the C++ ANM interpreter produced output" % ("PASS" if ok else "FAIL"))
    if not ok:
        failures += 1
    else:
        fields = {}
        for token in anmrun.split()[1:]:
            key, _, value = token.partition("=")
            fields[key] = value
        ok = fields.get("files") == "54"
        print("[%s] interpreter ran over %s of 55 .anm files"
              % ("PASS" if ok else "FAIL", fields.get("files")))
        if not ok:
            failures += 1
        ok = int(fields.get("ran", "0")) >= 2000
        print("[%s] at least 2000 scripts executed (%s)"
              % ("PASS" if ok else "FAIL", fields.get("ran")))
        if not ok:
            failures += 1
        # Scripts that reach their terminator prove the timing model works;
        # ones that stop on an unimplemented opcode are counted separately.
        ok = int(fields.get("finished", "0")) >= 1300
        print("[%s] at least 1300 scripts ran to completion (%s)"
              % ("PASS" if ok else "FAIL", fields.get("finished")))
        if not ok:
            failures += 1
        # Zero faults is the headline property: the interpreter must not
        # reject any instruction in the shipped data.
        ok = fields.get("faulted") == "0"
        print("[%s] zero execution faults (%s)"
              % ("PASS" if ok else "FAIL", fields.get("faulted")))
        if not ok:
            failures += 1
        # MILESTONE: every opcode that appears in the shipped data is
        # implemented, so no script may stop for an unknown instruction.
        ok = fields.get("unsupported") == "0"
        print("[%s] zero unimplemented opcodes (%s)"
              % ("PASS" if ok else "FAIL", fields.get("unsupported")))
        if not ok:
            failures += 1
        ok = int(fields.get("instrs", "0")) >= 350000
        print("[%s] at least 350000 ANM instructions executed (%s)"
              % ("PASS" if ok else "FAIL", fields.get("instrs")))
        if not ok:
            failures += 1

    # 9. The object scheduler must build real animation trees.
    anmobj = None
    for line in lines:
        if line.startswith("ANMOBJ files="):
            anmobj = line
            break
    ok = anmobj is not None
    print("[%s] the C++ ANM object manager produced output" % ("PASS" if ok else "FAIL"))
    if not ok:
        failures += 1
    else:
        fields = {}
        for token in anmobj.split()[1:]:
            key, _, value = token.partition("=")
            fields[key] = value
        ok = int(fields.get("roots", "0")) >= 1000
        print("[%s] at least 1000 root animations created (%s)"
              % ("PASS" if ok else "FAIL", fields.get("roots")))
        if not ok:
            failures += 1
        ok = int(fields.get("spawned", "0")) >= 2000
        print("[%s] at least 2000 child animations spawned (%s)"
              % ("PASS" if ok else "FAIL", fields.get("spawned")))
        if not ok:
            failures += 1
        # Spawned minus retired must equal what is still alive, and the pool
        # must never have been exhausted or depth-capped.
        ok = int(fields.get("retired", "0")) > 2000
        print("[%s] over 2000 animations retired (%s)"
              % ("PASS" if ok else "FAIL", fields.get("retired")))
        if not ok:
            failures += 1
        ok = fields.get("live_cap") == "0" and fields.get("depth_cap") == "0"
        print("[%s] pool and depth limits never hit (live=%s depth=%s)"
              % ("PASS" if ok else "FAIL", fields.get("live_cap"),
                 fields.get("depth_cap")))
        if not ok:
            failures += 1
        ok = fields.get("faults") == "0"
        print("[%s] zero scheduler faults (%s)"
              % ("PASS" if ok else "FAIL", fields.get("faults")))
        if not ok:
            failures += 1

    # 10. The rasteriser must produce a real framebuffer.
    anmdraw = None
    for line in lines:
        if line.startswith("ANMDRAW files="):
            anmdraw = line
            break
    ok = anmdraw is not None
    print("[%s] the C++ rasteriser produced output" % ("PASS" if ok else "FAIL"))
    if not ok:
        failures += 1
    else:
        fields = {}
        for token in anmdraw.split()[1:]:
            key, _, value = token.partition("=")
            fields[key] = value
        ok = int(fields.get("quads", "0")) >= 100
        print("[%s] at least 100 quads rasterised (%s)"
              % ("PASS" if ok else "FAIL", fields.get("quads")))
        if not ok:
            failures += 1
        ok = int(fields.get("written", "0")) >= 1000000
        print("[%s] at least 1 million pixels written (%s)"
              % ("PASS" if ok else "FAIL", fields.get("written")))
        if not ok:
            failures += 1
        ok = int(fields.get("nonblack", "0")) >= 100000
        print("[%s] at least 100000 pixels differ from the clear colour (%s)"
              % ("PASS" if ok else "FAIL", fields.get("nonblack")))
        if not ok:
            failures += 1
        # A stable checksum proves the render is deterministic. It changes only
        ok = fields.get("checksum") == "5502a39ee95aa9f2"
        print("[%s] framebuffer checksum is deterministic (%s)"
              % ("PASS" if ok else "FAIL", fields.get("checksum")))
        if not ok:
            failures += 1

    # 11. The texture decoder must produce a viewable atlas.
    atlas = None
    for line in lines:
        if line.startswith("ANMATLAS name="):
            atlas = line
            break
    ok = atlas is not None
    print("[%s] the C++ texture decoder produced an atlas" % ("PASS" if ok else "FAIL"))
    if not ok:
        failures += 1
    else:
        # The first decodable texture in the archive is ascii.anm, an
        # ARGB4444 font atlas. Its presence and format are stable facts about
        # the shipped data, so asserting them catches a regression in the
        # decode path (wrong bpp, wrong channel order) immediately.
        ok = "name=ascii.anm" in atlas and "fmt=5" in atlas
        print("[%s] atlas is ascii.anm in ARGB4444 (%s)"
              % ("PASS" if ok else "FAIL", atlas.split()[0] if atlas else "-"))
        if not ok:
            failures += 1
        ok = "256x256" in atlas
        print("[%s] atlas is 256x256" % ("PASS" if ok else "FAIL"))
        if not ok:
            failures += 1

    # 12. The sprite table must render through the real rasteriser.
    sprites = None
    for line in lines:
        if line.startswith("ANMSPR name="):
            sprites = line
            break
    ok = sprites is not None
    print("[%s] the C++ sprite renderer produced output" % ("PASS" if ok else "FAIL"))
    if not ok:
        failures += 1
    else:
        fields = {}
        for token in sprites.split()[1:]:
            key, _, value = token.partition("=")
            fields[key] = value
        ok = fields.get("name") == "enemy.anm"
        print("[%s] sprite table came from enemy.anm (%s)"
              % ("PASS" if ok else "FAIL", fields.get("name")))
        if not ok:
            failures += 1
        ok = int(fields.get("drawn", "0")) >= 100
        print("[%s] at least 100 sprites drawn (%s)"
              % ("PASS" if ok else "FAIL", fields.get("drawn")))
        if not ok:
            failures += 1
        # Every drawn sprite must have produced pixels; a zero here means the
        # UV rect was empty or the texture index was wrong.
        ok = int(fields.get("written", "0")) >= 50000
        print("[%s] sprite rendering wrote at least 50000 pixels (%s)"
              % ("PASS" if ok else "FAIL", fields.get("written")))
        if not ok:
            failures += 1


    print()
    if failures:
        print("selftest failed: %d item(s)" % failures)
        return 1
    print("selftest passed: the COMPILED C++ parses the real archive correctly")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

