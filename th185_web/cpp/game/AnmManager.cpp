#include "AnmManager.hpp"

#include <cmath>

namespace th185 {

u32 AnmManager::allocate() {
    if (!free_slots_.empty()) {
        const u32 handle = free_slots_.back();
        free_slots_.pop_back();
        return handle;
    }
    if (pool_.size() >= ANM_MAX_LIVE) return 0xFFFFFFFFu;
    pool_.push_back(std::make_unique<AnmVm>());
    return u32(pool_.size() - 1);
}

u32 AnmManager::create(const AnmResource* resource, i32 script_index, u32 depth,
                       u32 parent, float translate_x, float translate_y) {
    if (!resource) {
        error_ = "null resource";
        return 0xFFFFFFFFu;
    }
    if (script_index < 0 || u32(script_index) >= resource->script_list.size()) {
        error_ = "script index out of range";
        ++stats_.bad_script;
        return 0xFFFFFFFFu;
    }

    const u32 handle = allocate();
    if (handle == 0xFFFFFFFFu) {
        ++stats_.live_capped;
        error_ = "animation pool full";
        return handle;
    }

    AnmVm& vm = *pool_[handle];
    vm.interpreter = AnmInterpreter{};
    vm.resource = resource;
    vm.script_index = script_index;
    vm.depth = depth;
    vm.alive = true;
    vm.detached = false;
    vm.age = 0;
    vm.translate_x = translate_x;
    vm.translate_y = translate_y;
    vm.children.clear();
    vm.parent = parent;

    if (!vm.interpreter.bind(resource, &resource->script_list[u32(script_index)].bytes)) {
        error_ = "bind failed";
        ++stats_.bad_script;
        free_slots_.push_back(handle);
        return 0xFFFFFFFFu;
    }

    ++live_;
    if (live_ > stats_.peak_live) stats_.peak_live = live_;
    return handle;
}

AnmVm* AnmManager::find(u32 handle) {
    if (handle >= pool_.size()) return nullptr;
    AnmVm* vm = pool_[handle].get();
    return (vm && vm->alive) ? vm : nullptr;
}

void AnmManager::kill(u32 handle) {
    AnmVm* vm = find(handle);
    if (!vm) return;
    vm->alive = false;
    --live_;
    ++stats_.retired;
    free_slots_.push_back(handle);
}

u32 AnmManager::update(Rng& rng, float rate) {
    ++stats_.frames;

    // Snapshot the pool size: a tick can spawn children, and children created
    // this frame are ticked on the NEXT frame. That matches the original, where
    // a spawn is a request fulfilled by the frame loop.
    const std::size_t count = pool_.size();
    u32 ticked = 0;

    for (std::size_t i = 0; i < count; ++i) {
        AnmVm* vm = pool_[i].get();
        if (!vm || !vm->alive) continue;

        const AnmRunResult result = vm->interpreter.tick(rng, rate);
        ++vm->age;
        ++ticked;
        ++stats_.ticks;

        // Fulfil spawn requests recorded by this tick.
        auto& requests = vm->interpreter.mutable_stats().spawn_requests;
        if (!requests.empty()) {
            for (const auto& request : requests) {
                if (vm->depth + 1 >= ANM_MAX_DEPTH) {
                    ++stats_.depth_capped;
                    continue;
                }
                // A child inherits its parent's position, so a spawned effect
                // appears where its parent is rather than at the origin.
                const u32 child = create(vm->resource, request.script,
                                         vm->depth + 1, u32(i),
                                         vm->translate_x + request.anchor_x,
                                         vm->translate_y + request.anchor_y);
                if (child == 0xFFFFFFFFu) continue;
                if (request.detached) {
                    pool_[child]->detached = true;
                    ++stats_.detached_spawned;
                } else {
                    vm->children.push_back(child);
                }
                ++stats_.spawned;
            }
            requests.clear();
        }

        if (result == AnmRunResult::Finished ||
            result == AnmRunResult::Stopped ||
            result == AnmRunResult::Fault) {
            if (result == AnmRunResult::Fault) ++stats_.faults;
            else ++stats_.finished;
            kill(u32(i));
        }
    }
    return ticked;
}

void AnmManager::clear() {
    // pool_ is a vector that grows on demand, so iterate its actual size --
    // looping to ANM_MAX_LIVE reads past the end and traps in WASM.
    for (u32 i = 0; i < u32(pool_.size()); ++i) {
        if (pool_[i] && pool_[i]->alive) kill(i);
    }
    collect();
}

void AnmManager::collect() {
    // Slots are returned to free_slots_ the moment an instance dies, so the
    // only work left is dropping stale child lists.
    for (auto& slot : pool_) {
        if (slot && !slot->alive) slot->children.clear();
    }
}

u32 AnmManager::render_bank(SoftwareRaster& target, const AnmResource* resource,
                            const DecodedImage* textures, u32 texture_count,
                            float view_x, float view_y) const {
    u32 submitted = 0;
    for (const auto& slot : pool_) {
        if (!slot || !slot->alive) continue;
        // Only instances from this bank: a sprite index is meaningful only
        // inside the resource it came from.
        if (resource && slot->resource != resource) continue;

        const AnmVisual& visual = slot->interpreter.visual();
        if (!visual.visible()) continue;

        const AnmVariables& variables = slot->interpreter.variables();

        // Sprite size: the interpreter stores it when opcode 300/301 selects a
        // sprite; 436 can override it. Fall back to 32x32 when nothing set it.
        float width = visual.size.x;
        float height = visual.size.y;
        if (!(width > 0.0f)) width = 32.0f;
        if (!(height > 0.0f)) height = 32.0f;
        width *= visual.scale.x;
        height *= visual.scale.y;

        const float half_w = width * 0.5f;
        const float half_h = height * 0.5f;
        const float angle = variables.rotation.z;
        const float cosine = float(std::cos(double(angle)));
        const float sine = float(std::sin(double(angle)));

        AnmQuad quad;
        const float local_x[4] = {-half_w, half_w, half_w, -half_w};
        const float local_y[4] = {-half_h, -half_h, half_h, half_h};
        for (u32 i = 0; i < 4; ++i) {
            const float rx = local_x[i] * cosine - local_y[i] * sine;
            const float ry = local_x[i] * sine + local_y[i] * cosine;
            quad.x[i] = view_x + slot->translate_x + variables.position.x + rx;
            quad.y[i] = view_y + slot->translate_y + variables.position.y + ry;
        }

        // UVs come from the SELECTED SPRITE, not from 0..1: an ANM sprite is a
        // sub-rectangle of an atlas.
        float u0 = 0.0f, v0 = 0.0f, u1 = 1.0f, v1 = 1.0f;
        const AnmResource* source = slot->resource;
        if (source && visual.sprite >= 0 &&
            u32(visual.sprite) < source->sprites.size()) {
            const AnmSprite& sprite = source->sprites[u32(visual.sprite)];
            u0 = sprite.u0;
            v0 = sprite.v0;
            u1 = sprite.u1;
            v1 = sprite.v1;
        }
        quad.u[0] = u0;
        quad.u[1] = u1;
        quad.u[2] = u1;
        quad.u[3] = u0;
        quad.v[0] = v0;
        quad.v[1] = v0;
        quad.v[2] = v1;
        quad.v[3] = v1;

        quad.tint = Rgba8{u8((visual.color >> 16) & 0xFF),
                          u8((visual.color >> 8) & 0xFF),
                          u8(visual.color & 0xFF),
                          u8((visual.color >> 24) & 0xFF)};
        quad.blend = blend_mode_from_flags(visual.flags);
        quad.nearest = (visual.render_flags & 0x800u) != 0;

        // The SPRITE names its texture; the sprite index is not a texture index.
        if (textures && source && visual.sprite >= 0 &&
            u32(visual.sprite) < source->sprites.size()) {
            const u32 texture_index = source->sprites[u32(visual.sprite)].texture;
            if (texture_index < texture_count) {
                quad.texture = &textures[texture_index];
            }
        }

        target.draw(quad);
        ++submitted;
    }
    return submitted;
}

u32 AnmManager::render(SoftwareRaster& target, const DecodedImage* textures,
                       u32 texture_count, float view_x, float view_y) const {
    return render_bank(target, nullptr, textures, texture_count, view_x, view_y);
}
}  // namespace th185
