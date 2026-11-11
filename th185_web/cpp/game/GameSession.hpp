#pragma once
// TH185 game session: ties the reconstructed managers together.
//
// This is the first piece that composes modules instead of decoding one format.
// It owns the pieces a running scene needs and drives them in the right order:
//
//   load archive  ->  pick a scene (.std)  ->  seed the RNG  ->
//   select waves from the template pool  ->  expose the resulting layout
//
// It deliberately contains NO rendering, input or audio: those subsystems are
// still unimplemented, and the point of this class is to make the data flow
// executable and testable today.
//
// What it does NOT claim: that a scene plays like the original. Wave selection
// needs the per-template weights, which the original builds at runtime in
// EnemyManager and which we have not dumped yet (docs/WAVES.md). Until then a
// caller must supply the weights; the selection ALGORITHM itself is verified
// bit-identical (WaveSelector + Rng).

#include "Archive.hpp"
#include "CardManager.hpp"
#include "Difficulty.hpp"
#include "Rng.hpp"
#include "StageScript.hpp"
#include "WaveSelector.hpp"

#include <array>
#include <string>
#include <vector>

namespace th185 {

constexpr u32 SESSION_SCENE_COUNT = 9;

// The 9 scene files, in stage order. stage 0 is the tutorial.
const std::array<const char*, SESSION_SCENE_COUNT>& scene_files();

// One wave slot resolved for a scene.
struct ResolvedWave {
    u32 slot = 0;          // index into StageScript::waves
    i32 template_index = -1;  // index into the 111-entry template table
    u32 type = 0;          // StageWave::marker high 16 bits
    u32 ordinal = 0;       // StageWave::marker low 16 bits
};

// Session state for one stage attempt.
struct GameSession {
    Archive archive;
    std::vector<u8> file;            // the whole .dat, kept for extraction
    StageScript stage;
    Rng rng;
    u32 stage_index = 0;
    u32 difficulty = 2;              // Normal
    u32 card_id = 0;

    // Resolved layout.
    std::vector<ResolvedWave> waves;

    // Load the archive from raw bytes. Returns false and fills `error`.
    bool open(std::vector<u8> archive_bytes, std::string* error);

    // Load scene `stage_index` (0..8) from the archive.
    bool load_stage(u32 stage_index, std::string* error);

    // Seed the RNG. Must be called before select_waves().
    void seed(u16 value);

    // Resolve every wave slot in the loaded scene using `weights`.
    //   skip tracking: the previous pick is passed as skip_index so the same
    //   template never appears twice in a row, matching the original.
    // Returns the number of slots resolved.
    u32 select_waves(const std::array<u32, WAVE_TABLE_SIZE>& weights);

    // True when the current stage/difficulty/card combination forces a boss.
    bool has_forced_boss() const;

    // The forced boss template index, or 0xFFFFFFFF when none applies.
    u32 forced_boss() const;

    // Score cap for the current stage and difficulty.
    u32 score_cap() const;
};

}  // namespace th185
