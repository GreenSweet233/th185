#pragma once
// TH185 ANM object scheduling.
//
// ## What this adds
//
// The interpreter understands every opcode but opcode 500-506 / 508 (object
// spawning) could only be counted, because a spawned child has nowhere to live
// and nothing to advance it. This file supplies that: a pool of live animations,
// a parent/child tree, and a per-frame update that ticks the whole tree.
//
// ## Provenance
//
// The shape follows th15's `AnmManager` + `AnmVm` split: an `AnmVm` is one
// running animation (script cursor + variables + visual state + children), and
// the manager owns the pool and walks it each frame. th15 additionally owns a
// renderer and an audio device; those are deliberately absent here, so this is
// scheduling only -- nothing is drawn.
//
// ## Limits, stated plainly
//
//   * No rendering: visual state is produced and readable, never presented.
//   * No effect host: opcode 508 (spawn_effect) records instead of running.
//   * No geometry: opcode 600-611 store their vertex count but allocate nothing.
//   * Depth is capped so a self-spawning script cannot exhaust memory.

#include "AnmInterpreter.hpp"
#include "AnmResource.hpp"
#include "Rng.hpp"
#include "SoftwareRaster.hpp"
#include "Types.hpp"

#include <memory>
#include <string>
#include <vector>

namespace th185 {

constexpr u32 ANM_MAX_DEPTH = 8;
constexpr u32 ANM_MAX_LIVE = 4096;

// One running animation.
struct AnmVm {
    AnmInterpreter interpreter;
    const AnmResource* resource = nullptr;
    i32 script_index = -1;
    u32 depth = 0;
    bool alive = true;
    bool detached = false;
    // Frames this instance has been advanced.
    u32 age = 0;
    // Where this instance sits in animation space. th15 keeps this in
    // visual.translation and opcode 422 copies it into variables.position.
    float translate_x = 0.0f;
    float translate_y = 0.0f;

    // Children created by opcode 500-506. Owned through the manager's pool.
    std::vector<u32> children;
    // The parent, or UINT32_MAX for a root.
    u32 parent = 0xFFFFFFFFu;
};

struct AnmManagerStats {
    u32 spawned = 0;
    u32 detached_spawned = 0;
    u32 retired = 0;
    u32 peak_live = 0;
    u32 depth_capped = 0;
    u32 live_capped = 0;
    u32 bad_script = 0;
    u32 frames = 0;
    u32 ticks = 0;
    u32 finished = 0;
    u32 faults = 0;
};

class AnmManager {
public:
    // Creates a root animation running `script_index` of `resource`.
    // Returns the handle, or UINT32_MAX when the pool is full.
    u32 create(const AnmResource* resource, i32 script_index,
               u32 depth = 0, u32 parent = 0xFFFFFFFFu,
               float translate_x = 0.0f, float translate_y = 0.0f);

    // Advances every live animation one frame. Returns the number ticked.
    u32 update(Rng& rng, float rate = 1.0f);

    // Draws every live animation into `target`. Each instance contributes one
    // quad built from its visual state (sprite size, scale, rotation, position,
    // colour, blend mode). Returns the number of quads submitted.
    //
    // The transform follows th15's AnmRenderer::shape: the quad is centred on
    // the sprite size, scaled, rotated about Z, then translated.
    // `view_x`/`view_y` is where animation-space (0,0) lands in target pixels.
    // TH scripts place sprites around the origin, so a caller rendering to a
    // 640x480 target passes (320, 240) to centre them.
    u32 render(SoftwareRaster& target, const DecodedImage* textures,
               u32 texture_count, float view_x, float view_y) const;

    // Draws only the instances that belong to `resource`. A scene loads several
    // banks (background + enemies) and each instance must be sampled from its
    // OWN bank's textures, so the manager has to filter.
    u32 render_bank(SoftwareRaster& target, const AnmResource* resource,
                    const DecodedImage* textures, u32 texture_count,
                    float view_x, float view_y) const;

    // Frees finished animations and compacts the pool.
    void collect();

    AnmVm* find(u32 handle);
    // Marks a handle dead and returns its slot to the free list.
    void kill(u32 handle);

    // Retires every live animation. Loading a different scene must not keep
    // the previous scene's sprites alive: they belong to banks that are about
    // to be unloaded, so their textures would be dangling.
    void clear();
    u32 live_count() const { return live_; }
    const AnmManagerStats& stats() const { return stats_; }
    const std::string& error() const { return error_; }

private:
    u32 allocate();

    std::vector<std::unique_ptr<AnmVm>> pool_;
    std::vector<u32> free_slots_;
    u32 live_ = 0;
    AnmManagerStats stats_{};
    std::string error_;
};

}  // namespace th185
