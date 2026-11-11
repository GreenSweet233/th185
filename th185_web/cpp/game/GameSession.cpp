#include "GameSession.hpp"

namespace th185 {

namespace {

// stage 0 is the tutorial scene (world01t), then world01..world08.
const std::array<const char*, SESSION_SCENE_COUNT> kSceneFiles = {{
    "world01t.std",
    "world01.std",
    "world02.std",
    "world03.std",
    "world04.std",
    "world05.std",
    "world06.std",
    "world07.std",
    "world08.std",
}};

}  // namespace

const std::array<const char*, SESSION_SCENE_COUNT>& scene_files() { return kSceneFiles; }

bool GameSession::open(std::vector<u8> archive_bytes, std::string* error) {
    file = std::move(archive_bytes);
    if (!archive.parse(file, error)) return false;
    return true;
}

bool GameSession::load_stage(u32 index, std::string* error) {
    if (index >= SESSION_SCENE_COUNT) {
        if (error) *error = "stage index out of range";
        return false;
    }
    if (archive.entries.empty()) {
        if (error) *error = "archive not open";
        return false;
    }

    const char* wanted = kSceneFiles[index];
    std::size_t entry = archive.entries.size();
    for (std::size_t i = 0; i < archive.entries.size(); ++i) {
        if (archive.entries[i].name == wanted) { entry = i; break; }
    }
    if (entry == archive.entries.size()) {
        if (error) *error = std::string("scene not found in archive: ") + wanted;
        return false;
    }

    std::vector<u8> blob = archive.extract(file, entry, error);
    if (blob.empty()) return false;

    if (!stage.parse(std::move(blob), error)) return false;

    stage_index = index;
    waves.clear();
    return true;
}

void GameSession::seed(u16 value) {
    rng.seed = value;
    rng.calls = 0;
}

u32 GameSession::select_waves(const std::array<u32, WAVE_TABLE_SIZE>& weights) {
    waves.clear();
    waves.reserve(stage.waves.size());

    // The original passes the previous pick as skip_index, so the same template
    // never appears twice in a row. The first slot has nothing to skip.
    u32 skip = 0xFFFFFFFFu;
    for (std::size_t i = 0; i < stage.waves.size(); ++i) {
        const StageWave& slot = stage.waves[i];
        const i32 picked = select_wave(weights, rng, skip);

        ResolvedWave resolved;
        resolved.slot = u32(i);
        resolved.template_index = picked;
        resolved.type = (slot.marker >> 16) & 0xFFFF;
        resolved.ordinal = slot.marker & 0xFFFF;
        waves.push_back(resolved);

        if (picked >= 0) skip = u32(picked);
    }
    return u32(waves.size());
}

bool GameSession::has_forced_boss() const {
    return forced_boss() != 0xFFFFFFFFu;
}

u32 GameSession::forced_boss() const {
    return forced_boss_index(stage_index, card_id);
}

u32 GameSession::score_cap() const {
    bool out_of_range = false;
    return score_cap_for_stage(difficulty, stage_index, &out_of_range);
}

}  // namespace th185
