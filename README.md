# th185-decomp

A matching decompilation of 東方虹龍洞 外伝 ～ バレットフィリア達の闇市場 ～
(TH185) v1.00a: C++ source written to compile, with ZUN's own compiler and
flags, back to the machine code of `th185.exe`, checked function by function.

**Status: bootstrap complete, matching just begun.** 50 of 1678 functions are
matched so far. This is not a finished decompilation; the toolchain gap below
blocks the byte-equality check entirely.

This repository does not include the game's executable or its data files
(`th185.dat`, `thbgm.dat`, the manual); you need your own copy of the game.
`th185.exe` must have SHA-256
`76bbf844e962c16004b64f1b2ef3551593056baa43ea53b9a5df45ad731345c7`.

## Status

Six gate scripts implement the rules in [AGENTS.md](AGENTS.md). Two pass. The
other four report `INCOMPLETE`, each for a reason stated plainly below — an
`INCOMPLETE` gate is one that exists and runs but cannot reach a conclusion,
not one that was waved through. Nothing in
this repository is claimed to be byte-identical, because the check that would
prove it has never run.

| Gate | What it checks | Status |
|---|---|---|
| G1 | Global type consistency | **PASS** — but `definitions = 0`; no struct is defined yet |
| G2 | Binary witness, both directions | **INCOMPLETE** — 50 / 1678 functions matched (2.98%) |
| G3 | Structure alignment (loops vs back edges) | **PASS** |
| G4 | Canonical form + blacklist | **PASS** — without `clang-format`, which is absent here |
| G5 | No UB + perturbation stability | **INCOMPLETE** — needs the two MSVC generations above |
| G6 | Independent evidence (RTTI/vtable) | **INCOMPLETE** — three of its four requirements are unsupported natively |

`dashboard/metrics.json` is the only thing a human needs to read; it records
these states and, where a number is unknown, stores `null` rather than `0`.

The three non-PASS gates in one line each: G2 has explained 50 of 1678
functions; G4 passes its blacklist but has never been run through
`clang-format`, which is not installed; G5 and G6 cannot conclude without a
matched toolchain and without RTTI, `.pdata` or a second disassembler.

### What is actually matched

50 functions. 49 of them fall into eight instruction-shape groups found by
scanning every `call` target in `.text`; the remaining one was written first,
by hand, as the pilot:

| Shape | Instructions | Functions | Meaning |
|---|---|---:|---|
| A | `mov eax, ecx; ret` | 2 | return `this` |
| B | `ret 0` | 13 | empty body |
| C | `mov eax, [ecx]; ret` | 2 | read first field (u32) |
| D | `movss xmm0, [ecx]; ret` | 2 | read first field (float) |
| E | `fsincos` sequence | 20 | polar → Vec2 |
| F | `xor eax, eax; ret` | 4 | return 0 |
| G | `movq` + 8-byte copy | 3 | Vec3 copy |
| H | `mov [ecx], eax; ret 4` | 3 | write first field |

The pilot function, `0x00402A50` (44 bytes, 8 instructions, 6 callers),
decrements a countdown at `[ecx+0x8C]` and, when it goes negative, clears that
field along with `[ecx+0x00]` and `[ecx+0xD4]`:

```asm
mov edx, [ecx+0x8C]      ; countdown
sub edx, 1
mov [ecx+0x8C], edx
jns done
mov dword [ecx+0x8C], 0  ; clamp and reset
mov dword [ecx+0x00], 0
mov dword [ecx+0xD4], 0
ret
```

Its source deliberately defines no struct. Only these three offsets are known;
what lies between them is not, and padding a struct to make the offsets come
out right would be an unwitnessed element that G2 rejects and G4 forbids. The
offsets are named constants and the layout is listed as unresolved.

Shape E is worth reading too: the original computes `x = sin(angle) * length` and
`y = cos(angle) * length`, not the usual `(cos, sin)`. That follows from the x87
stack order after `fsincos` — `st0` is sin and is popped first — and writing it
the intuitive way would be 90° wrong.

One source function can carry several addresses when the shape repeats across
classes:

```cpp
//@witness func=0x00404B70+0x00405A70+...+0x0048F370 kind=stdcall
void polar_to_vec2(void* self, float angle, float length);
```

## Toolchain

What the executable says about how ZUN built it:

| | | Evidence |
|---|---|---|
| Compiler | **two MSVC generations**: VS2017 15.x (237 objects) + VS2019 16.x (170 objects) | Rich header, key `0x0D2865FF`. Product IDs 27412 / 31328 / 31332 — see the correction note below |
| Target | PE32 / x86, ImageBase `0x00400000`, entry RVA `0x0009003B` | Optional header |
| Linker flags | not yet recovered | LTCG signature absent from the POGO debug entry |
| Timestamp | `1659144319` = 2022-07-30T01:25:19Z | COFF header |
| PE CheckSum | `0x000DE79F` | recomputed value matches |
| Sections | `.text` 711114 / `.rdata` 97948 / `.data` 734132 / `.rsrc` 7168 (VSize) | section table |

### The compiler identification, corrected

`.data` declares 734132 bytes of virtual size but only 65536 are stored, so
668596 bytes are BSS — vtables live there and their initial contents cannot be
read from the file, only reconstructed from the constructors that write them.

An earlier revision of this document claimed "three MSVC builds side by side"
and concluded that matching was blocked until three toolchains could be
installed. **That was wrong, and the error is worth recording.**

Each Rich Header entry is a pair `(comp_id, count)`. The high 16 bits of
`comp_id` are the build number; the low 16 bits are the product id. The earlier
parse had the two fields swapped, so product ids `27412 / 31328 / 31332` were
read as build numbers. The actual build numbers are `257 / 259 / 260 / 261 /
262 / 265 / 255 / 258` — which is the right magnitude, as one glance at any
installed MSVC's `FileVersion` would have shown (this machine's is
`19.44.35213.0`, where `35213` is the build).

Grouped by product id, the truth is two compiler generations, not three:

| Product id | Objects | Meaning |
|---|---:|---|
| 27412 | 237 | VS2017 15.x MSVC |
| 31328 | 89 | VS2019 16.x MSVC |
| 31332 | 81 | VS2019 16.x MSVC (possibly the same toolset as 31328) |

So the requirement is two Visual Studio generations, and the byte-equality
check is blocked on an install rather than on an impossibility. `toolchain.lock`
now records this as `PARTIALLY_RESOLVED`; the original wrong conclusion is kept
in `AGENTS.md` under a collapsed section rather than edited away.

### This machine's toolchain

```
original needs : MSVC build 27412 / 31328 / 31332  (VS2019 16.0.x / 16.8 / 16.9)
this machine   : MSVC 14.44.35207 (VS2022 Community)
                 C:\Program Files (x86)\Microsoft Visual Studio\2019 exists but is empty
```

AGENTS.md §2 forbids changing the compiler to make bytes line up, so VS2022 is
not used as a stand-in. The consequence is stated rather than worked around:
**the byte-equality check has never run, and no function here is claimed to be
byte-identical.**

## Repository layout

```
orig/        original binary (not committed; README explains the hash)
src/         reconstructed source
include/     shared headers — the only place types may be defined
types/       db.json (all types) + lock.json (locked layouts)
evidence/    witness maps, vtable scans, offset statistics, gate self-tests
ledger/      unresolved.md, violations.md
dashboard/   metrics.json — the human-readable state of everything
tools/       gate scripts
```

## Gates

```sh
python tools/check_types.py        # G1 global type consistency
python tools/check_witness.py      # G2 binary witness
python tools/check_structure.py    # G3 structure alignment
python tools/check_style.py        # G4 canonical form + blacklist
python tools/check_stability.py    # G5 no UB + perturbation stability
python tools/check_evidence.py     # G6 independent evidence
python tools/check_bootstrap.py    # meta: are all six implemented and run?
```

Each prints PASS / FAIL / INCOMPLETE and writes its result into
`dashboard/metrics.json`. `INCOMPLETE` is used where a gate exists but cannot
reach a conclusion — it is not a soft pass.

Every gate ships a mutation-test record under `evidence/`, because a checker
that cannot fail is worse than no checker:

| Gate | Mutation | Caught |
|---|---|---|
| G1 | locked offset 12 → 16 | `offset locked=16 found=12` |
| G1 | same struct, two different definitions | `cross_tu_mismatch (2 variants)` |
| G2 | witness a displacement that exists elsewhere but not in this function | `not inside this function` |
| G3 | claim a match for a function whose binary has a loop, with loop-free source | `source_fully_unrolled` |
| G4 | one line per blacklist rule | 9/9 caught; clean code + comments mentioning `goto` → 0 false positives |
| G6 | card count 87 → 88 | `card_vtable_count_mismatch` |

## Evidence gathered so far

- **117 vtables, 2164 slots.** 87 of them have exactly 22 slots and sit at a
  uniform `0x5C` stride — `(22 + 1) * 4`, the 22 virtual functions plus the
  MSVC COL back-pointer. The card table (`CARD_DESC_LIST` at `0x004CA370`) holds
  exactly 87 entries. Two independent structures agreeing on a count and a
  stride is the strongest cross-check available without RTTI.
- **9 virtual call slots** found by classifying all 592 `[ecx+N]` offsets by
  access pattern. `0xE4` is called indirectly 156 times, the hottest in `.text`.
- **592 offsets / 4398 accesses** classified into read-write, float, bit-flag,
  state-value and indirect-call groups (`evidence/offsets-ecx.json`). The file
  states its own limit: the same offset in different classes is merged, so this
  is not any one class's layout.

### What is not available natively

| Requirement | Why not |
|---|---|
| RTTI class names | zero `.??AV...@@` hits; the build used `/GR-` |
| `.pdata` function bounds | PE32 x86 has no `.pdata` — that is an x64 table |
| Cross-checking two disassemblers | no Ghidra, IDA or retdec on this machine |
| `clang-format` | not installed; G4 reports `FORMAT_UNAVAILABLE` every run |

None of these are silently skipped. Each is printed on every run and recorded
in `evidence/`.

## Documentation

- [AGENTS.md](AGENTS.md): the rules. Byte equality is the gate, not the goal;
  a candidate that fails G1–G6 is rejected however small its diff.
- [ledger/unresolved.md](ledger/unresolved.md): every function or field whose
  meaning is not yet established, each with its blocking condition.
- [ledger/violations.md](ledger/violations.md): rules that were broken, kept
  rather than edited away. The first entry records a `match:` commit made while
  only three of the six gates existed.
- `th185_web/docs/`: the format work from before this repository changed
  direction — PBG4 decryption, LZSS, ANM, ECL, the card table. That work is
  behaviourally verified but is *not* matching evidence.

## History

This repository spent 156 commits as a behaviour-equivalent reconstruction:
C++ compiled to WebAssembly, playable in a browser, verified by clearing all
nine stages with zero ECL faults. That work is preserved, and the original
specification is archived at
[`th185_web/docs/HISTORY-ROUTE-SWITCH.md`](th185_web/docs/HISTORY-ROUTE-SWITCH.md)
— including the clause that said this repository would not attempt byte-level
matching, which this direction change overrides.

The reconstruction is the reason Phase A of each function can be written
quickly: the formats, the opcodes and the card semantics were already worked
out. It is not evidence that anything compiles to the right bytes.

## License

The project's own work (the gate scripts, the analysis, the names, comments and
documentation) is MIT — see [LICENSE](LICENSE). The game itself, including the
data tables and strings the source reproduces from `th185.exe`, remains the
property of its author, ZUN (Team Shanghai Alice).

## Credits

- [th16-decomp](https://github.com/johnthreekay/th16) by JohnThreeKay, for the
  structure of this repository and its README; a demonstration that MSVC
  matching on a modern Touhou build is possible when the toolchain is pinned.
- [thprac](https://github.com/touhouworldcup/thprac): its TH185 module supplies
  the global addresses used here (`funds`, `bulletMoney`, `CARD_DESC_LIST`,
  `ENEMY_MANAGER`). Those addresses are cross-checked against this repository's
  own disassembly rather than trusted blindly.
- [thtk](https://github.com/thpatch/thtk): the authoritative ANM/ECL format
  definitions vendored under `th185_web/reference/thtk/`.
- [Touhou 20 reconstruction](https://github.com/Oracatt/Touhou20): its ECL
  opcode audit let 93 of TH185's 117 entity opcodes be checked against a
  same-generation implementation instead of being guessed.