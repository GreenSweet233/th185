#include "CardManager.hpp"

namespace th185 {

namespace {

// VA 0x4CA370, 87 entries x 68 bytes, in TABLE ORDER (not sorted by id).
// Generated from the binary; see docs/CARDS.md.
//
// The `fields` array is +0x08..+0x34 verbatim (13 DWORDs, of which the first
// 12 are stored here and icon_left/icon_right/reserved follow). The semantics
// of most fields are still unknown, so they are preserved rather than guessed.
const std::array<CardEntry, CARD_COUNT> kCards = {{
    CardEntry{"PENDULUM", 1, {1, 3, 0, 8, 0, 999, 0, 0, 1, 0, 1, 0}, 19, 20, 0},
        CardEntry{"PENDULUM2", 2, {1, 3, 0, 7, 0, 999, 0, 0, 1, 0, 1, 0}, 17, 18, 0},
        CardEntry{"DANGO", 4, {1, 2, 1, 6, 0, 9, 1, 3, 0, 1, 1, 10}, 21, 22, 0},
        CardEntry{"NARUMI", 34, {0, 2, 1, 6, 0, 9, 1, 3, 0, 1, 1, 10}, 23, 24, 0},
        CardEntry{"PACHE", 35, {0, 2, 1, 6, 0, 9, 1, 3, 0, 1, 1, 10}, 25, 26, 0},
        CardEntry{"MAGICSCROLL", 24, {1, 2, 1, 6, 0, 3, 1, 3, 0, 1, 1, 10}, 67, 68, 0},
        CardEntry{"FLAN", 57, {0, 2, 4, 6, 0, 3, 1, 3, 0, 1, 1, 25}, 127, 128, 0},
        CardEntry{"FUTO", 58, {0, 2, 1, 6, 0, 3, 1, 3, 0, 1, 1, 10}, 129, 130, 0},
        CardEntry{"AUN", 59, {0, 2, 1, 6, 0, 3, 1, 3, 0, 1, 1, 10}, 131, 132, 0},
        CardEntry{"EXTEND", 3, {1, 3, 3, 6, 0, 5, 0, 3, 0, 1, 1, 10}, 7, 8, 0},
        CardEntry{"SPEEDQUEEN", 27, {0, 2, 1, 6, 0, 5, 1, 3, 0, 1, 1, 10}, 65, 66, 0},
        CardEntry{"CIRNO_OP", 16, {1, 1, 2, 3, 0, 1, 1, 3, 0, 2, 1, 30}, 43, 44, 0},
        CardEntry{"STAR", 54, {0, 1, 2, 3, 0, 1, 1, 3, 0, 2, 1, 30}, 121, 122, 0},
        CardEntry{"LUNA", 55, {0, 1, 2, 3, 0, 1, 1, 3, 0, 2, 1, 30}, 123, 124, 0},
        CardEntry{"SUNNY", 56, {0, 1, 2, 3, 0, 1, 1, 3, 0, 2, 1, 30}, 125, 126, 0},
        CardEntry{"YUREIMU", 83, {0, 1, 5, 1, 0, 1, 1, 3, 0, 2, 1, 30}, 181, 182, 0},
        CardEntry{"YUMARISA", 84, {0, 1, 5, 1, 0, 1, 1, 3, 0, 2, 1, 30}, 179, 180, 0},
        CardEntry{"LARVA", 69, {0, 1, 2, 3, 0, 1, 1, 3, 0, 2, 1, 30}, 151, 152, 0},
        CardEntry{"SEIRAN", 63, {0, 1, 5, 3, 0, 1, 1, 3, 0, 2, 1, 60}, 139, 140, 0},
        CardEntry{"NEMUNO", 70, {0, 1, 5, 3, 0, 1, 1, 3, 0, 2, 1, 60}, 153, 154, 0},
        CardEntry{"YOUMU_OP", 14, {1, 1, 5, 3, 0, 1, 1, 3, 0, 3, 1, 60}, 47, 48, 0},
        CardEntry{"YOUMU_OP2", 77, {1, 1, 5, 1, 0, 1, 1, 3, 0, 3, 1, 75}, 167, 168, 0},
        CardEntry{"REIMU_OP", 6, {1, 1, 5, 3, 0, 1, 1, 3, 0, 3, 1, 60}, 27, 28, 0},
        CardEntry{"REIMU_OP2", 7, {0, 1, 6, 1, 0, 1, 1, 3, 0, 3, 1, 75}, 49, 50, 0},
        CardEntry{"ALICE_OP", 15, {1, 1, 6, 3, 0, 1, 1, 3, 0, 3, 1, 60}, 39, 40, 0},
        CardEntry{"MARISA_OP", 8, {0, 1, 5, 3, 0, 1, 1, 3, 0, 3, 1, 60}, 29, 30, 0},
        CardEntry{"MARISA_OP2", 9, {0, 1, 6, 1, 0, 1, 1, 3, 0, 3, 1, 75}, 51, 52, 0},
        CardEntry{"SAKUYA_OP", 10, {0, 1, 5, 3, 0, 1, 1, 3, 0, 3, 1, 60}, 31, 32, 0},
        CardEntry{"SAKUYA_OP2", 11, {0, 1, 6, 1, 0, 1, 1, 3, 0, 3, 1, 75}, 53, 54, 0},
        CardEntry{"SANAE_OP", 12, {0, 1, 5, 3, 0, 1, 1, 3, 0, 3, 1, 60}, 33, 34, 0},
        CardEntry{"SANAE_OP2", 13, {0, 1, 6, 1, 0, 1, 1, 3, 0, 3, 1, 75}, 55, 56, 0},
        CardEntry{"OKINA_OP", 17, {0, 1, 6, 2, 0, 1, 1, 3, 0, 3, 1, 60}, 41, 42, 0},
        CardEntry{"TORAMARU", 53, {0, 1, 6, 3, 0, 1, 1, 3, 0, 3, 1, 60}, 119, 120, 0},
        CardEntry{"NUE_OP", 18, {0, 1, 4, 2, 0, 1, 1, 3, 0, 3, 1, 75}, 45, 46, 0},
        CardEntry{"SEKIBANKI", 72, {0, 1, 4, 2, 0, 1, 1, 3, 0, 3, 1, 75}, 157, 158, 0},
        CardEntry{"EIKA", 76, {0, 1, 4, 2, 0, 1, 1, 3, 0, 3, 1, 75}, 165, 166, 0},
        CardEntry{"MAGATAMA", 49, {0, 1, 5, 3, 0, 1, 1, 3, 0, 3, 1, 75}, 109, 110, 0},
        CardEntry{"OPTION_BR", 28, {0, 1, 8, 2, 0, 1, 1, 3, 0, 3, 1, 75}, 69, 70, 0},
        CardEntry{"WARP", 39, {1, 0, 2, 2, 0, 1, 1, 3, 0, 1, 1, 25}, 83, 84, 0},
        CardEntry{"SEIJA", 78, {0, 0, 2, 4, 0, 1, 1, 3, 0, 1, 1, 25}, 169, 170, 0},
        CardEntry{"LILY", 46, {0, 0, 2, 2, 0, 1, 1, 3, 0, 1, 1, 25}, 97, 98, 0},
        CardEntry{"KOZUCHI", 40, {1, 0, 5, 4, 0, 1, 1, 3, 1, 0, 1, 50}, 91, 92, 0},
        CardEntry{"BASSDRUM", 47, {0, 0, 5, 2, 0, 1, 1, 3, 0, 2, 1, 50}, 99, 100, 0},
        CardEntry{"PSYCO", 48, {0, 0, 6, 2, 0, 1, 1, 3, 0, 2, 1, 50}, 101, 102, 0},
        CardEntry{"WAKASAGI", 71, {0, 0, 5, 3, 0, 1, 1, 3, 0, 2, 1, 50}, 155, 156, 0},
        CardEntry{"KUTAKA", 74, {0, 0, 5, 3, 0, 1, 1, 3, 0, 3, 1, 75}, 163, 164, 0},
        CardEntry{"KOMACHI", 75, {0, 0, 5, 3, 0, 1, 1, 3, 0, 3, 1, 75}, 161, 162, 0},
        CardEntry{"KANAME2", 79, {0, 0, 5, 3, 0, 1, 1, 3, 0, 3, 1, 75}, 171, 172, 0},
        CardEntry{"CYLINDER", 50, {0, 0, 5, 4, 0, 1, 1, 3, 0, 3, 1, 75}, 111, 112, 0},
        CardEntry{"KANAME", 41, {1, 0, 12, 2, 0, 1, 1, 3, 0, 4, 1, 100}, 89, 90, 0},
        CardEntry{"MOON", 42, {1, 0, 8, 2, 0, 1, 1, 3, 0, 4, 1, 100}, 85, 86, 0},
        CardEntry{"MIKOFLASH", 43, {0, 0, 12, 4, 0, 1, 1, 3, 0, 4, 1, 100}, 87, 88, 0},
        CardEntry{"VAMPIRE", 44, {0, 0, 13, 2, 0, 1, 1, 3, 0, 4, 1, 100}, 93, 94, 0},
        CardEntry{"RICEBALL", 51, {0, 0, 9, 4, 0, 1, 1, 3, 0, 4, 1, 100}, 113, 114, 0},
        CardEntry{"SUN", 45, {0, 0, 13, 4, 0, 1, 1, 3, 0, 5, 1, 150}, 95, 96, 0},
        CardEntry{"TEWI", 22, {1, 2, 2, 3, 0, 1, 1, 3, 0, 1, 1, 10}, 61, 62, 0},
        CardEntry{"MANEKI", 36, {1, 2, 3, 2, 0, 1, 1, 3, 0, 1, 1, 10}, 103, 104, 0},
        CardEntry{"KOISHI", 25, {1, 2, 1, 2, 0, 1, 1, 3, 0, 1, 1, 10}, 75, 76, 0},
        CardEntry{"ITEM_LINE", 20, {1, 2, 3, 2, 0, 1, 1, 3, 0, 1, 1, 10}, 37, 38, 0},
        CardEntry{"ITEM_CATCH", 19, {1, 2, 3, 3, 0, 1, 1, 3, 0, 1, 1, 25}, 35, 36, 0},
        CardEntry{"YUYUKO", 31, {0, 2, 5, 3, 0, 1, 1, 3, 0, 1, 1, 75}, 77, 78, 0},
        CardEntry{"URUMI", 73, {0, 2, 3, 3, 0, 1, 1, 3, 0, 1, 1, 25}, 159, 160, 0},
        CardEntry{"DEAD_SPELL", 29, {0, 2, 8, 2, 0, 1, 1, 3, 0, 2, 1, 50}, 71, 72, 0},
        CardEntry{"MONEY", 32, {0, 2, 2, 2, 0, 1, 1, 3, 0, 2, 1, 50}, 79, 80, 0},
        CardEntry{"YAMAWARO", 37, {1, 2, 5, 2, 0, 1, 1, 3, 0, 2, 1, 50}, 105, 106, 0},
        CardEntry{"KISERU", 38, {1, 2, 5, 2, 0, 1, 1, 3, 0, 3, 1, 75}, 107, 108, 0},
        CardEntry{"MUKADE", 52, {0, 2, 5, 3, 0, 1, 1, 3, 0, 3, 1, 75}, 115, 116, 0},
        CardEntry{"AUTOBOMB", 21, {1, 2, 4, 2, 0, 1, 1, 3, 0, 4, 1, 100}, 59, 60, 0},
        CardEntry{"MINORIKO", 68, {0, 2, 3, 3, 0, 1, 1, 3, 0, 3, 1, 75}, 149, 150, 0},
        CardEntry{"SAKI", 81, {0, 2, 9, 1, 0, 1, 1, 3, 0, 5, 1, 125}, 175, 176, 0},
        CardEntry{"MOKOU", 5, {1, 2, 13, 3, 0, 1, 1, 3, 0, 5, 1, 200}, 15, 16, 0},
        CardEntry{"TAKANE", 67, {0, 2, 2, 2, 0, 1, 1, 3, 0, 1, 1, 25}, 147, 148, 0},
        CardEntry{"MAINSHOT_PU", 23, {1, 2, 8, 3, 0, 1, 1, 3, 0, 3, 1, 75}, 57, 58, 0},
        CardEntry{"MAINSHOT_SP", 26, {0, 2, 8, 3, 0, 1, 1, 3, 0, 3, 1, 75}, 63, 64, 0},
        CardEntry{"POWERMAX", 30, {0, 2, 11, 3, 0, 1, 1, 3, 0, 5, 1, 125}, 73, 74, 0},
        CardEntry{"ROKUMON", 33, {0, 2, 6, 2, 0, 1, 1, 3, 0, 3, 1, 75}, 81, 82, 0},
        CardEntry{"JOON", 60, {0, 2, 4, 3, 0, 1, 1, 3, 0, 1, 1, 25}, 133, 134, 0},
        CardEntry{"PEACE", 80, {0, 2, 7, 3, 0, 1, 1, 3, 0, 4, 1, 50}, 173, 174, 0},
        CardEntry{"SUIKA", 82, {0, 2, 7, 3, 0, 1, 1, 3, 0, 4, 1, 75}, 177, 178, 0},
        CardEntry{"BLANK", 0, {0, 2, 5, 8, 0, 1, 1, 1, 0, 1, 1, 0}, 5, 6, 0},
        CardEntry{"NITORI", 66, {0, 2, 8, 1, 0, 1, 1, 3, 0, 1, 1, 50}, 145, 146, 0},
        CardEntry{"SHION", 61, {0, 3, 4, 2, 0, 1, 0, 3, 0, 2, 1, 25}, 135, 136, 0},
        CardEntry{"KEIKI", 62, {0, 3, 1, 2, 0, 1, 0, 3, 0, 3, 1, 50}, 137, 138, 0},
        CardEntry{"DREMII", 64, {0, 3, 5, 2, 0, 1, 0, 3, 0, 3, 1, 25}, 141, 142, 0},
        CardEntry{"JYUNKA", 65, {0, 2, 8, 2, 0, 1, 1, 3, 0, 3, 1, 125}, 143, 144, 0},
        CardEntry{"NULL", 85, {1, 4, 0, 0, 0, 1, 1, 0, 1, 1, 0, 0}, 183, 184, 0},
        CardEntry{"BACK", 86, {1, 4, 0, 0, 0, 1, 0, 0, 1, 1, 0, 0}, 3, 4, 0},
}};

}  // namespace

const std::array<CardEntry, CARD_COUNT>& cards() { return kCards; }

const CardEntry* find_card(u32 id) {
    // Linear scan: only 87 entries and ids are not sorted.
    for (const CardEntry& card : kCards) {
        if (card.id == id) return &card;
    }
    return nullptr;
}

bool is_special_card(u32 id) {
    return id == CARD_ID_BLANK || id == CARD_ID_NITORI;
}

// --- CardInventory ---

void CardInventory::add_currency(u32 amount) {
    currency_ += amount;
}

bool CardInventory::spend(u32 amount) {
    if (amount > currency_) return false;
    currency_ -= amount;
    return true;
}

bool CardInventory::buy(u32 card_id) {
    const CardEntry* card = find_card(card_id);
    if (card == nullptr) return false;
    if (owned_.count(card_id) != 0) return false;
    if (!spend(card->price())) return false;
    owned_.insert(card_id);
    return true;
}

bool CardInventory::owns(u32 card_id) const {
    return owned_.count(card_id) != 0;
}

u32 CardInventory::owned_count() const {
    return u32(owned_.size());
}

bool CardInventory::equip(u32 card_id) {
    if (!owns(card_id)) return false;
    for (const u32 existing : loadout_) {
        if (existing == card_id) return true;   // already carried
    }
    if (loadout_.size() >= LOADOUT_SIZE) {
        // Drop the oldest entry, matching a fixed-size belt where the newest
        // card displaces the first.
        loadout_.erase(loadout_.begin());
    }
    loadout_.push_back(card_id);
    return true;
}

bool CardInventory::unequip(u32 index) {
    if (index >= loadout_.size()) return false;
    loadout_.erase(loadout_.begin() + index);
    return true;
}

// The card table's remaining fields are not decoded, so the aggregates below
// use the ONE field whose meaning is established: fields[9] is the category
// (1..5). The mapping from category to effect is this build's interpretation
// and is recorded as such in the ledger -- it is not an original constant.
namespace {

constexpr u32 CATEGORY_LIFE = 1;
constexpr u32 CATEGORY_BOMB = 2;
constexpr u32 CATEGORY_SHOT = 3;
constexpr u32 CATEGORY_MONEY = 4;
constexpr u32 CATEGORY_UTILITY = 5;

u32 category_of(const CardEntry& card) { return card.fields[9]; }

}  // namespace

u32 CardInventory::extra_lives() const {
    u32 total = 0;
    for (const u32 id : loadout_) {
        const CardEntry* card = find_card(id);
        if (card != nullptr && category_of(*card) == CATEGORY_LIFE) ++total;
    }
    return total;
}

u32 CardInventory::extra_bombs() const {
    u32 total = 0;
    for (const u32 id : loadout_) {
        const CardEntry* card = find_card(id);
        if (card != nullptr && category_of(*card) == CATEGORY_BOMB) ++total;
    }
    return total;
}

float CardInventory::shot_power_bonus() const {
    float total = 0.0f;
    for (const u32 id : loadout_) {
        const CardEntry* card = find_card(id);
        if (card != nullptr && category_of(*card) == CATEGORY_SHOT) total += 1.0f;
    }
    return total;
}

u32 CardInventory::starting_currency_bonus() const {
    u32 total = 0;
    for (const u32 id : loadout_) {
        const CardEntry* card = find_card(id);
        if (card != nullptr && category_of(*card) == CATEGORY_MONEY) total += 10;
    }
    return total;
}

void CardInventory::reset_run() {
    loadout_.clear();
}

// --- Dual currency ---

void CardInventory::add_funds(u32 amount) {
    // The original clamps at 999,990 rather than wrapping.
    const u64 sum = u64(funds_) + u64(amount);
    funds_ = sum > FUNDS_CAP ? FUNDS_CAP : u32(sum);
}

void CardInventory::add_bullet_money(i32 amount) {
    // int32 in the original (address 0x4d1070); it can go negative if the
    // player is careless, and a hit wipes it entirely.
    bullet_money_ += amount;
}

void CardInventory::void_bullet_money() {
    bullet_money_ = 0;
}

u32 CardInventory::bank_bullet_money() {
    if (bullet_money_ <= 0) {
        bullet_money_ = 0;
        return 0;
    }
    const u32 gained = u32(bullet_money_);
    add_funds(gained);
    bullet_money_ = 0;
    return gained;
}

// --- Black market ---

void CardInventory::open_market(const std::vector<u32>& pool, u32 seed) {
    offers_.clear();
    purchases_left_ = MARKET_OFFERS;
    if (pool.empty()) return;

    // Deterministic draw: the same seed always offers the same three cards,
    // which is what makes the market reproducible in tests.
    u32 state = seed == 0 ? 1u : seed;
    std::vector<u32> remaining = pool;
    while (offers_.size() < MARKET_OFFERS && !remaining.empty()) {
        state = state * 1103515245u + 12345u;
        const std::size_t pick = std::size_t(state >> 8) % remaining.size();
        offers_.push_back(remaining[pick]);
        remaining.erase(remaining.begin() + pick);
    }
}

bool CardInventory::market_buy(u32 index) {
    if (index >= offers_.size()) return false;
    if (purchases_left_ == 0) return false;

    const u32 id = offers_[index];
    const CardEntry* card = find_card(id);
    if (card == nullptr) return false;

    // Bullet money first: it is the reward for clean play, so it should be
    // spent before the persistent wallet.
    u32 price = card->price();
    if (bullet_money_ > 0) {
        const i32 from_bullets =
            bullet_money_ < i32(price) ? bullet_money_ : i32(price);
        bullet_money_ -= from_bullets;
        price -= u32(from_bullets);
    }
    if (price > funds_) return false;
    funds_ -= price;

    // Market cards are TEMPORARY: they are not added to `owned_`, so they do
    // not survive the run.
    temporary_.push_back(id);
    loadout_.push_back(id);
    --purchases_left_;
    return true;
}

// --- Stage structure ---

namespace {

const std::array<SceneInfo, SCENE_COUNT> kScenes = {{
    {"Tutorial", 3, 1},
    {"1st Market", 3, 4},
    {"2nd Market", 4, 4},
    {"3rd Market", 5, 4},
    {"4th Market", 5, 5},
    {"5th Market", 7, 4},
    {"6th Market", 7, 5},
    {"End of Market", 4, 1},
    {"Challenge Market", 12, 6},
}};

}  // namespace

const std::array<SceneInfo, SCENE_COUNT>& scenes() { return kScenes; }

std::vector<u32> scene_card_pool(u32 scene) {
    std::vector<u32> pool;
    if (scene >= SCENE_COUNT) return pool;

    // Walk the priced cards and give this scene a slice sized to its boss
    // count. The offset accumulates across scenes so the pools stay disjoint,
    // which keeps a Challenge-Market run from seeing the tutorial's cards.
    std::vector<u32> priced;
    for (const CardEntry& card : kCards) {
        if (card.price() > 0) priced.push_back(card.id);
    }
    if (priced.empty()) return pool;

    u32 offset = 0;
    for (u32 i = 0; i < scene; ++i) offset += kScenes[i].boss_candidates;

    const u32 want = kScenes[scene].boss_candidates;
    for (u32 i = 0; i < want; ++i) {
        pool.push_back(priced[(offset + i) % priced.size()]);
    }
    return pool;
}

}  // namespace th185
