#pragma once
// TH185 ability-card table (87 entries).
//
// The card description table lives at VA 0x4CA370 (thprac calls it
// CARD_DESC_LIST). Each entry is 17 DWORDs (68 bytes). Verified against the
// binary (see docs/CARDS.md).
//
//   struct CardEntry {              // 68 bytes
//       const char* name;           // +0x00  internal name, e.g. "PENDULUM"
//       uint32      id;             // +0x04  0..86
//       uint32      f2..f13;        // +0x08.. +0x34
//       uint32      icon_left;      // +0x38  atlas cell, always ODD
//       uint32      icon_right;     // +0x3C  always icon_left + 1
//       uint32      f16;            // +0x40  always 0
//   };
//
// Card IDs 0 and 66 are special: they gate the hidden bosses (see
// docs/WAVES.md 9 and WaveSelector::forced_boss_index).
//
//   0  BLANK   -> carry it on stage 5 to force Boss26
//   66 NITORI  -> carry it on stage 7 to force Boss27
//
// That matches the THBWiki plot: Marisa takes the blank card to the Moriya
// shrine festival, meets Nitori there, and receives Nitori is card.
//
// Verification: `python th185_web/scripts/native/cards.py --selftest` and
// `cards_cpp_check.py --selftest`.

#include "Types.hpp"

#include <array>
#include <string>

namespace th185 {

constexpr u32 CARD_TABLE_VA = 0x4CA370u;
constexpr u32 CARD_COUNT = 87;
constexpr u32 CARD_ENTRY_SIZE = 68;   // 17 DWORDs

// Special card ids.
constexpr u32 CARD_ID_BLANK = 0;       // forces Boss26 on stage 5
constexpr u32 CARD_ID_NITORI = 66;     // forces Boss27 on stage 7
constexpr u32 CARD_ID_NULL = 85;
constexpr u32 CARD_ID_BACK = 86;

struct CardEntry {
    const char* name = "";
    u32 id = 0;
    // Raw fields +0x08 .. +0x34 (13 DWORDs). Semantics not fully resolved;
    // small enumerations. Kept verbatim so nothing is lost.
    std::array<u32, 13> fields{};
    u32 icon_left = 0;    // +0x38, always odd
    u32 icon_right = 0;   // +0x3C, always icon_left + 1
    u32 reserved = 0;     // +0x40, always 0
};

// The 87-entry table, in table order (NOT sorted by id).
const std::array<CardEntry, CARD_COUNT>& cards();

// Look up by card id (0..86). Returns nullptr when absent.
const CardEntry* find_card(u32 id);

// True for the two ids that gate the hidden bosses.
bool is_special_card(u32 id);

}  // namespace th185
