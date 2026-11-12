#include "AnmManager.hpp"

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
                       u32 parent) {
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
                const u32 child = create(vm->resource, request.script,
                                         vm->depth + 1, u32(i));
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

void AnmManager::collect() {
    // Slots are returned to free_slots_ the moment an instance dies, so the
    // only work left is dropping stale child lists.
    for (auto& slot : pool_) {
        if (slot && !slot->alive) slot->children.clear();
    }
}

}  // namespace th185
