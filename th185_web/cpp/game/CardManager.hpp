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

// --- Dual currency -------------------------------------------------------
//
// TH185 has TWO currencies, and both addresses are known from thprac's global
// table (see docs/BUILD.md):
//
//   funds        0x4d106c   the persistent money, capped at 999,990
//   bulletMoney  0x4d1070   bullet-money, int32, TWO copies at 0x4d1070/74
//
// bulletMoney is earned by collecting bullets and is VOIDED on a hit -- that
// is the whole point of the mechanic: it rewards clean play.
constexpr u32 FUNDS_VA = 0x4d106cu;
constexpr u32 BULLET_MONEY_VA = 0x4d1070u;
constexpr u32 FUNDS_CAP = 999990u;

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

    // COST, the equip budget this card consumes. Byte +0x34 in the table.
    //
    // Its 11 distinct values are 0/10/25/30/50/60/75/100/125/150/200 --
    // exactly the tier ladder the original's `Equip Cost %d%%` /
    // `Total Cost %d%%/%d%%` strings describe.
    //
    // docs/CARDS.md guessed +0x30; that field is only 0/1 across all 87 cards,
    // so it cannot be a cost.
    //
    // The index is 11 here because CardEntry::fields starts after name+id;
    // in the raw 17-dword entry the same slot is index 13.
    //
    // The market price reuses the same number in this build; the original may
    // scale it per shop (`Shop Cost %d(%d)`), which is not decoded.
    u32 cost() const { return fields[11]; }
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

// --- Stage structure -----------------------------------------------------
//
// TH185 is not a linear 6-stage game: it is nine SELECTABLE scenes, each with
// a fixed wave count and a set of boss candidates (docs/BUILD.md, from
// THBWiki + thprac's warp tables).
//
//   scene  waves  bosses
//   0 Tutorial      3   1  (三花)
//   1 1st Market    3   4  (三花/穰子/爱塔/合欢)
//   2 2nd Market    4   4  (琪露诺/若鹭姬/赤蛮奇/润美)
//   3 3rd Market    5   4  (璎花/久侘歌/成美/小町)
//   4 4th Market    5   5  (早苗/咲夜/妖梦/灵梦/荷取)
//   5 5th Market    7   4  (典/龙/克劳恩/天子)
//   6 6th Market    7   5  (萃香/二岩/早鬼/百百世/高岭)
//   7 End of Market 4   1  (天弓千亦, fixed)
//   8 Challenge    12   6  (Boss Rush)
//
// Totals: 50 mid-stage wave slots, 28 boss indices.
constexpr u32 SCENE_COUNT = 9;

struct SceneInfo {
    const char* name;
    u32 waves;
    u32 boss_candidates;
};

const std::array<SceneInfo, SCENE_COUNT>& scenes();

// The card ids a scene's bosses can drop.
//
// NOT established from the binary. docs/BUILD.md lists the boss NAMES per
// scene but not which card each one yields, so this is a deterministic
// stand-in: it walks the card table and hands each scene a disjoint slice,
// sized to the scene's candidate count. Recorded as `inferred` in the ledger.
std::vector<u32> scene_card_pool(u32 scene);

// Owned cards, equipped loadout and the market wallet.
//
// TH185's loop is: buy cards in the market with currency, equip a loadout
// before a stage, and each card modifies the run. The table above describes
// the cards; this class holds the player's state.
class CardInventory {
public:
    // Default slot count; the live value is loadout_slots_ and grows with

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
    // Wipes ownership, loadout and both wallets. Used by tests that need a
    // known-empty inventory; gameplay resets use reset_run() instead, which
    // keeps the wallet because a stage restart does not refund purchases.
    void reset_all() {
        owned_.clear();
        loadout_.clear();
        currency_ = 0;
        funds_ = 0;
        bullet_money_ = 0;
        temporary_.clear();
    }

    // Aggregate effects the run reads. Each is the sum over equipped cards,
    // which is how the original stacks card bonuses.
    u32 extra_lives() const;
    u32 extra_bombs() const;
    float shot_power_bonus() const;
    u32 starting_currency_bonus() const;

    // Resets everything except the wallet (a stage restart keeps purchases).
    void reset_run();

    // --- Dual currency ---
    //
    // `funds` persists across stages; `bullet_money` is per-run and is wiped
    // the moment the player is hit.
    void add_bullet_money(i32 amount);
    void void_bullet_money();          // on hit
    i32 bullet_money() const { return bullet_money_; }
    u32 funds() const { return funds_; }
    void add_funds(u32 amount);        // clamped to FUNDS_CAP
    // Collects the bullet money into funds (end of stage).
    u32 bank_bullet_money();

    // --- Equip cost budget ---
    //
    // The loadout is limited by total COST, not by card count alone: the
    // original shows `Total Cost %d%%/%d%%`. Slots and the cap both grow as
    // the player clears scenes and defeats new bosses.
    u32 cost_cap() const { return cost_cap_; }
    u32 loadout_cost() const;
    u32 loadout_slots() const { return loadout_slots_; }
    void set_cost_cap(u32 cap) { cost_cap_ = cap; }
    void set_loadout_slots(u32 slots) { loadout_slots_ = slots; }
    // Grows both limits, as clearing a new scene does in the original.
    void grant_progress(u32 extra_slots, u32 extra_cost);

    // --- Black market ---
    //
    // After each wave the market offers three purchases drawn from the cards
    // this stage's boss can drop. Bought cards are TEMPORARY: they last for
    // the run and are not added to the permanent collection.
    static constexpr u32 MARKET_OFFERS = 3;

    // Refreshes the offers. `pool` is the ids this stage may drop.
    void open_market(const std::vector<u32>& pool, u32 seed);
    const std::vector<u32>& market_offers() const { return offers_; }
    u32 market_purchases_left() const { return purchases_left_; }
    // Buys offer `index`. Spends bullet money first, then funds.
    bool market_buy(u32 index);
    // Temporary cards acquired this run.
    const std::vector<u32>& temporary_cards() const { return temporary_; }

private:
    u32 currency_ = 0;
    std::set<u32> owned_;
    std::vector<u32> loadout_;

    u32 funds_ = 0;
    i32 bullet_money_ = 0;

    // Starting limits: 8 slots and 100%% cost, which is what the original
    // shows on a fresh save.
    u32 loadout_slots_ = 8;
    u32 cost_cap_ = 100;

    std::vector<u32> offers_;
    u32 purchases_left_ = 0;
    std::vector<u32> temporary_;
};

}  // namespace th185
