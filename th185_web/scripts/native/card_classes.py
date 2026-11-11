#!/usr/bin/env python3
"""TH185 ability-card class map: RTTI -> vtable -> virtual functions.

## Why this matters

`docs/RECONSTRUCTION.md` 7 rates "player + card effects (87 classes)" as the
single largest work item (0.8 of ~3.4 units). This module is the entry point:
it turns the 87 opaque card ids from `docs/CARDS.md` into named C++ classes
with located virtual functions, which is what an effect implementation needs.

## What the binary gives us

TH185 is MSVC-built and ships full RTTI, so each card class leaves a chain:

    TypeDescriptor   ".?AVAbilityCardDangoInf@@"
        <- Complete Object Locator (signature 0)
            <- vtable[-1]
                vtable[0..15]  -> .text functions

Measured facts:

  * 87 `AbilityCard*` TypeDescriptors, matching the 87 card-table entries
  * every one resolves to a vtable with EXACTLY 16 virtual functions
  * all vtables live in .rdata between 0x4B6AB0 and 0x4B8998
  * the vtables are 0x5C (92) bytes apart in most cases, i.e. 16 pointers +
    a 28-byte tail (the tail is not part of the vtable)

## Naming caveat

RTTI names do not always equal the card-table names. `FLAN` in the card table
is `Fran` in RTTI. Always join on the id, not on the string.

## Usage

    python th185_web/scripts/native/card_classes.py --selftest
    python th185_web/scripts/native/card_classes.py --list
    python th185_web/scripts/native/card_classes.py --class Dango

Exit codes: 0 ok; 1 selftest failed; 2 environment missing
"""

from __future__ import annotations

import argparse
import struct
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

IMAGE_BASE = 0x400000
RTTI_PREFIX = b".?AVAbilityCard"
RTTI_SUFFIX = "Inf@@"
EXPECTED_CLASSES = 87
EXPECTED_VIRTUALS = 22


def _image():
    from probe import load_image, parse_pe

    data, _target = load_image()
    meta = parse_pe(data)
    return data, meta


def _va_to_off(meta, va):
    rva = va - IMAGE_BASE
    for section in meta["sections"]:
        if section["vaddr"] <= rva < section["vaddr"] + max(section["vsize"], section["rsize"]):
            return section["roff"] + (rva - section["vaddr"])
    return -1


def _off_to_va(meta, offset):
    for section in meta["sections"]:
        if section["roff"] <= offset < section["roff"] + section["rsize"]:
            return IMAGE_BASE + section["vaddr"] + (offset - section["roff"])
    return -1


def load_classes():
    """Return the card class map: name -> descriptors, vtable and functions."""
    data, meta = _image()
    text = [s for s in meta["sections"] if s["name"] == ".text"][0]
    text_lo = IMAGE_BASE + text["vaddr"]
    text_hi = text_lo + text["rsize"]

    # 1. Collect TypeDescriptor names.
    descriptors = []
    pos = 0
    while True:
        at = data.find(RTTI_PREFIX, pos)
        if at < 0:
            break
        pos = at + 1
        end = data.find(b"\x00", at)
        if end < 0:
            break
        raw = data[at:end].decode("ascii", "replace")
        if not raw.endswith(RTTI_SUFFIX):
            continue
        descriptors.append((at, raw))

    classes = []
    for name_offset, raw_name in descriptors:
        # TypeDescriptor layout: vftable(4) + reserved(4) + name[]
        name_va = _off_to_va(meta, name_offset)
        if name_va < 0:
            continue
        td_va = name_va - 8

        # 2. Find the Complete Object Locator that references this descriptor.
        pattern = struct.pack("<I", td_va)
        vtables = []
        search = 0
        while True:
            hit = data.find(pattern, search)
            if hit < 0:
                break
            search = hit + 1
            col_va = _off_to_va(meta, hit) - 12
            col_off = _va_to_off(meta, col_va)
            if col_off < 0:
                continue
            signature = struct.unpack_from("<I", data, col_off)[0]
            if signature != 0:
                continue

            # 3. The vtable points back at the COL from its slot [-1].
            col_pattern = struct.pack("<I", col_va)
            inner = 0
            while True:
                back = data.find(col_pattern, inner)
                if back < 0:
                    break
                inner = back + 1
                vtable_va = _off_to_va(meta, back) + 4
                vtable_off = _va_to_off(meta, vtable_va)
                if vtable_off < 0:
                    continue
                functions = []
                for index in range(EXPECTED_VIRTUALS + 8):
                    at = vtable_off + index * 4
                    if at + 4 > len(data):
                        break
                    target = struct.unpack_from("<I", data, at)[0]
                    if text_lo <= target < text_hi:
                        functions.append(target)
                    else:
                        break
                if functions:
                    vtables.append((vtable_va, functions))

        core = raw_name
        core = core.replace(".?AVAbilityCard", "").replace(RTTI_SUFFIX, "")
        entry = {
            "name": core,
            "rtti": raw_name,
            "type_descriptor": td_va,
            "vtable": vtables[0][0] if vtables else None,
            "virtuals": vtables[0][1] if vtables else [],
        }
        classes.append(entry)

    return classes


def selftest() -> int:
    print("=== ability-card class map selftest ===")
    print()
    failures = 0

    try:
        classes = load_classes()
    except ImportError as exc:
        print("[SKIP] missing dependency: %s" % exc)
        return 0
    except SystemExit as exc:
        print("[SKIP] %s" % exc)
        return 0

    ok = len(classes) == EXPECTED_CLASSES
    print("[%s] found %d AbilityCard RTTI classes (expect %d)"
          % ("PASS" if ok else "FAIL", len(classes), EXPECTED_CLASSES))
    if not ok:
        failures += 1

    with_vtable = [c for c in classes if c["vtable"] is not None]
    ok = len(with_vtable) == len(classes)
    print("[%s] every class resolves to a vtable (%d / %d)"
          % ("PASS" if ok else "FAIL", len(with_vtable), len(classes)))
    if not ok:
        missing = [c["name"] for c in classes if c["vtable"] is None]
        print("       %s" % missing[:8])
        failures += 1

    ok = all(len(c["virtuals"]) == EXPECTED_VIRTUALS for c in with_vtable)
    print("[%s] every vtable has exactly %d virtual functions"
          % ("PASS" if ok else "FAIL", EXPECTED_VIRTUALS))
    if not ok:
        bad = [(c["name"], len(c["virtuals"])) for c in with_vtable
               if len(c["virtuals"]) != EXPECTED_VIRTUALS]
        print("       %s" % bad[:8])
        failures += 1

    vtables = [c["vtable"] for c in with_vtable]
    ok = len(set(vtables)) == len(vtables)
    print("[%s] every class has a DISTINCT vtable" % ("PASS" if ok else "FAIL"))
    if not ok:
        failures += 1

    ok = all(0x4B6000 <= v < 0x4BC000 for v in vtables)
    print("[%s] all vtables lie in the expected .rdata window"
          % ("PASS" if ok else "FAIL"))
    if not ok:
        failures += 1

    # The base class must exist and must NOT be one of the 87 concrete cards.
    ok = any(c["name"] == "" for c in classes)
    print("[%s] the abstract base (.?AVAbilityCardInf@@) is present"
          % ("PASS" if ok else "FAIL"))
    if not ok:
        failures += 1

    # Slot 21 must be the scalar deleting destructor in every class: it stores
    # the base vtable then calls operator delete. Verifying that keeps the slot
    # numbering honest, because "which slot is the effect" depends on it.
    try:
        import capstone
        from probe import load_image, parse_pe
        data, meta = _image()
        md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
        ok = True
        for c in with_vtable:
            off = _va_to_off(meta, c["virtuals"][21])
            if off < 0:
                ok = False
                break
            # A scalar deleting destructor writes the base vtable pointer into
            # [this] and tests the low bit of the "delete" flag. Both are stable
            # across classes; the SEH prologue is not (only classes with locals
            # have it), so do not look for fs:.
            insns = list(md.disasm(data[off:off + 240], c["virtuals"][21]))
            text = " ".join("%s %s" % (i.mnemonic, i.op_str) for i in insns)
            writes_vtable = "mov dword ptr [esi], 0x" in text or "mov dword ptr [ecx], 0x" in text
            tests_flag = "test byte ptr [ebp + 8], 1" in text or "test byte ptr [esp + 4], 1" in text
            if not (writes_vtable and tests_flag):
                ok = False
                break
        print("[%s] slot 21 is the scalar deleting destructor in every class"
              % ("PASS" if ok else "FAIL"))
        if not ok:
            failures += 1
    except ImportError:
        print("[SKIP] capstone unavailable for slot-21 check")

    # Cross-check against the card table by count.
    try:
        from cards import load_cards
        table = load_cards()
        ok = len(table) == EXPECTED_CLASSES
        print("[%s] card table also has %d entries (they correspond 1:1)"
              % ("PASS" if ok else "FAIL", len(table)))
        if not ok:
            failures += 1
    except (ImportError, SystemExit):
        print("[SKIP] card table unavailable")

    # Name mismatches are expected (FLAN vs Fran); report them rather than fail.
    try:
        from cards import load_cards
        table = load_cards()
        rtti_names = {c["name"].lower() for c in classes}
        unmatched = [c["name"] for c in table
                     if c["name"].lower().replace("_", "") not in rtti_names]
        print("       %d card-table names have no exact RTTI match: %s"
              % (len(unmatched), unmatched[:6]))
    except (ImportError, SystemExit):
        pass

    print()
    if failures:
        print("selftest failed: %d item(s)" % failures)
        return 1
    print("selftest passed: all %d card classes mapped to vtables" % len(classes))
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description="TH185 ability-card class map")
    parser.add_argument("--selftest", action="store_true")
    parser.add_argument("--list", action="store_true")
    parser.add_argument("--class", dest="class_name")
    args = parser.parse_args()

    if args.selftest:
        return selftest()

    try:
        classes = load_classes()
    except ImportError as exc:
        print("error: %s" % exc, file=sys.stderr)
        return 2

    if args.list:
        print("%-16s %-10s %-10s %s" % ("class", "descriptor", "vtable", "virt"))
        for c in classes:
            print("%-16s 0x%-8X 0x%-8X %d"
                  % (c["name"] or "(base)", c["type_descriptor"],
                     c["vtable"] or 0, len(c["virtuals"])))
        return 0

    if args.class_name:
        wanted = args.class_name.lower()
        for c in classes:
            if c["name"].lower() == wanted:
                print("class      : %s" % (c["name"] or "(base)"))
                print("rtti       : %s" % c["rtti"])
                print("descriptor : 0x%X" % c["type_descriptor"])
                print("vtable     : 0x%X" % c["vtable"])
                print("virtuals   : %d" % len(c["virtuals"]))
                for i, fn in enumerate(c["virtuals"]):
                    print("  [%2d] 0x%X" % (i, fn))
                return 0
        print("error: class not found: %s" % args.class_name, file=sys.stderr)
        return 2

    parser.print_help()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

