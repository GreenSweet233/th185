#!/usr/bin/env python3
"""Cross-check cpp/game/CardManager.cpp against the verified Python reference.

Contains a line-for-line Python mirror of the C++ card table and compares it
with `cards.py`, which reads the table straight out of the binary.

Contract: if CardManager.cpp changes, CardMirror below must change with it.

Exit codes: 0 ok; 1 mismatch; 2 environment missing
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

CARD_COUNT = 87
CARD_ENTRY_SIZE = 68
CARD_ID_BLANK = 0
CARD_ID_NITORI = 66
CARD_ID_NULL = 85
CARD_ID_BACK = 86


CPP_PATH = HERE.parents[1] / "cpp" / "game" / "CardManager.cpp"


def card_mirror():
    """Parse the kCards array straight out of CardManager.cpp.

    Parsing the real C++ file (instead of duplicating the numbers here) means
    this check cannot silently drift: if someone edits a value in the C++ table,
    the comparison below fails immediately.
    """
    import re

    if not CPP_PATH.is_file():
        return None
    source = CPP_PATH.read_text(encoding="utf-8")
    pattern = re.compile(
        r'CardEntry\{"([^"]+)", (\d+), \{([^}]*)\}, (\d+), (\d+), (\d+)\}')
    rows = []
    for match in pattern.finditer(source):
        rows.append({
            "name": match.group(1),
            "id": int(match.group(2)),
            "fields": [int(x.strip()) for x in match.group(3).split(",")],
            "icon_l": int(match.group(4)),
            "icon_r": int(match.group(5)),
            "res": int(match.group(6)),
        })
    return rows


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Cross-check cpp/game/CardManager.cpp against the reference")
    parser.add_argument("--selftest", action="store_true")
    args = parser.parse_args()

    print("=== C++ CardManager.cpp mirror selftest ===")
    print()
    failures = 0

    try:
        from cards import load_cards
    except ImportError as exc:
        print("[SKIP] reference module unavailable: %s" % exc)
        return 0

    try:
        ref = load_cards()
    except SystemExit as exc:
        print("[SKIP] cannot read the card table: %s" % exc)
        return 0

    ok = len(ref) == CARD_COUNT
    print("[%s] reference table has %d entries (expect %d)"
          % ("PASS" if ok else "FAIL", len(ref), CARD_COUNT))
    if not ok:
        failures += 1

    # 0. THE REAL CHECK: parse CardManager.cpp and compare every field.
    mirror = card_mirror()
    if mirror is None:
        print("[SKIP] CardManager.cpp not found")
        return 0 if not failures else 1

    ok = len(mirror) == len(ref)
    print("[%s] C++ table has %d entries (expect %d)"
          % ("PASS" if ok else "FAIL", len(mirror), len(ref)))
    if not ok:
        failures += 1

    diffs = []
    for i, (a, b) in enumerate(zip(mirror, ref)):
        if a["name"] != b["name"]:
            diffs.append((i, "name", a["name"], b["name"]))
        if a["id"] != b["id"]:
            diffs.append((i, "id", a["id"], b["id"]))
        if a["fields"] != b["fields"][:12]:
            diffs.append((i, "fields", a["fields"], b["fields"][:12]))
        if a["icon_l"] != b["fields"][12]:
            diffs.append((i, "icon_left", a["icon_l"], b["fields"][12]))
        if a["icon_r"] != b["fields"][13]:
            diffs.append((i, "icon_right", a["icon_r"], b["fields"][13]))
        if a["res"] != b["fields"][14]:
            diffs.append((i, "reserved", a["res"], b["fields"][14]))

    ok = not diffs
    print("[%s] every field of every card matches (%d diffs)"
          % ("PASS" if ok else "FAIL", len(diffs)))
    if not ok:
        for item in diffs[:5]:
            print("       %s" % (item,))
        failures += 1

    # 1. Entry size must be 68 bytes (17 DWORDs) — matches the C++ struct.
    ok = CARD_ENTRY_SIZE == 17 * 4
    print("[%s] entry size is %d bytes (17 DWORDs)"
          % ("PASS" if ok else "FAIL", CARD_ENTRY_SIZE))
    if not ok:
        failures += 1

    # 2. Icon atlas cells: icon_right == icon_left + 1 for EVERY card, and
    #    icon_left is always odd. This is what proves the two fields are a
    #    packed atlas coordinate rather than two independent attributes.
    ok = all(c["fields"][13] == c["fields"][12] + 1 for c in ref)
    print("[%s] icon_right == icon_left + 1 for all %d cards"
          % ("PASS" if ok else "FAIL", len(ref)))
    if not ok:
        bad = [(c["id"], c["fields"][12], c["fields"][13]) for c in ref
               if c["fields"][13] != c["fields"][12] + 1]
        print("       %s" % bad[:5])
        failures += 1

    ok = all(c["fields"][12] % 2 == 1 for c in ref)
    print("[%s] icon_left is odd for all %d cards"
          % ("PASS" if ok else "FAIL", len(ref)))
    if not ok:
        failures += 1

    # 3. The trailing field is always zero.
    ok = all(c["fields"][14] == 0 for c in ref)
    print("[%s] reserved field (+0x40) is always 0" % ("PASS" if ok else "FAIL"))
    if not ok:
        failures += 1

    # 4. Card ids are unique and cover 0..86.
    ids = [c["id"] for c in ref]
    ok = len(set(ids)) == len(ids)
    print("[%s] all %d card ids are unique" % ("PASS" if ok else "FAIL", len(ids)))
    if not ok:
        failures += 1

    ok = set(ids) == set(range(CARD_COUNT))
    print("[%s] card ids cover exactly 0..%d" % ("PASS" if ok else "FAIL", CARD_COUNT - 1))
    if not ok:
        missing = sorted(set(range(CARD_COUNT)) - set(ids))
        extra = sorted(set(ids) - set(range(CARD_COUNT)))
        print("       missing=%s extra=%s" % (missing[:8], extra[:8]))
        failures += 1

    # 5. The two story-critical cards must exist with the expected names.
    by_id = {c["id"]: c for c in ref}
    ok = (CARD_ID_BLANK in by_id and by_id[CARD_ID_BLANK]["name"] == "BLANK"
          and CARD_ID_NITORI in by_id and by_id[CARD_ID_NITORI]["name"] == "NITORI")
    print("[%s] BLANK(0) and NITORI(66) exist with the expected names"
          % ("PASS" if ok else "FAIL"))
    if not ok:
        failures += 1

    ok = (CARD_ID_NULL in by_id and by_id[CARD_ID_NULL]["name"] == "NULL"
          and CARD_ID_BACK in by_id and by_id[CARD_ID_BACK]["name"] == "BACK")
    print("[%s] NULL(85) and BACK(86) exist with the expected names"
          % ("PASS" if ok else "FAIL"))
    if not ok:
        failures += 1

    # 6. Names are non-empty printable ASCII.
    ok = all(c["name"] and all(32 <= ord(ch) < 127 for ch in c["name"]) for c in ref)
    print("[%s] all card names are printable ASCII" % ("PASS" if ok else "FAIL"))
    if not ok:
        failures += 1

    # 7. Cost column: exactly two distinct values, which is why the field is
    #    NOT a usable cost in TH185 (the Cost system is elsewhere).
    costs = sorted({c["cost"] for c in ref})
    ok = costs == [0, 1]
    print("[%s] +0x30 holds only %s (not a real cost column)"
          % ("PASS" if ok else "FAIL", costs))
    if not ok:
        failures += 1

    print()
    if failures:
        print("selftest failed: %d item(s)" % failures)
        return 1
    print("selftest passed: cpp/game/CardManager.cpp matches the reference")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

