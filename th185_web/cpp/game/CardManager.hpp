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
#include <set>
#include <vector>
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
    // Raw fields +0x08 .. +0x34 (13 DWORDs).
    //
    // Two of them are now identified from the full 87-card distribution:
    //   fields[11] = PRICE in market currency (10/25/30/50/60/75 across 11
    //                distinct values; the two hidden-boss cards cost 0)
    //   fields[9]  = category (1..5, 6 distinct values)
    //   fields[5]  = rarity tier (1 for 76 cards, 999 for the two special)
    //   fields[0]  = active vs passive (1 for 26 cards)
    // The rest stay verbatim so nothing is lost.
    std::array<u32, 13> fields{};
    u32 icon_left = 0;    // +0x38, always odd
    u32 icon_right = 0;   // +0x3C, always icon_left + 1
    u32 reserved = 0;     // +0x40, always 0

    // Price in market currency (fields[11]).
    u32 price() const { return fields[11]; }
    // True when the card has an active use (fields[0] == 1).
    bool is_active() const { return fields[0] == 1; }
};

// The 87-entry table, in table order (NOT sorted by id).
const std::array<CardEntry, CARD_COUNT>& cards();

// Look up by card id (0..86). Returns nullptr when absent.
const CardEntry* find_card(u32 id);

// True for the two ids that gate the hidden bosses.
bool is_special_card(u32 id);

// Owned cards, equipped loadout and the market wallet.
//
// TH185's loop is: buy cards in the market with currency, equip a loadout
// before a stage, and each card modifies the run. The table above describes
// the cards; this class holds the player's state.
class CardInventory {
public:
    static constexpr u32 LOADOUT_SIZE = 8;   // cards carried into a stage

    // Wallet.
    void add_currency(u32 amount);
    bool spend(u32 amount);
    u32 currency() const { return currency_; }

    // Buying. Fails when the card is already owned, unknown, or unaffordable.
    bool buy(u32 card_id);
    bool owns(u32 card_id) const;
    u32 owned_count() const;

    // Loadout. A card must be owned; equipping a full loadout replaces slot
    // `index` when it is in range, otherwise the oldest entry is dropped.
    bool equip(u32 card_id);
    bool unequip(u32 index);
    const std::vector<u32>& loadout() const { return loadout_; }
    void clear_loadout() { loadout_.clear(); }

    // Aggregate effects the run reads. Each is the sum over equipped cards,
    // which is how the original stacks card bonuses.
    u32 extra_lives() const;
    u32 extra_bombs() const;
    float shot_power_bonus() const;
    u32 starting_currency_bonus() const;

    // Resets everything except the wallet (a stage restart keeps purchases).
    void reset_run();

private:
    u32 currency_ = 0;
    std::set<u32> owned_;
    std::vector<u32> loadout_;
};

}  // namespace th185
