#include "EclOwner.hpp"

namespace th185 {

i32 EclOwner::spawn(const std::string& subroutine, i32 requested_id) {
    if (threads.size() >= MAX_THREADS) return -1;

    EclThread thread;
    thread.subroutine = subroutine;
    if (requested_id >= 0) {
        // Reuse the requested id, replacing any dead thread that holds it.
        for (EclThread& existing : threads) {
            if (existing.id == requested_id && !existing.alive) {
                existing = thread;
                existing.id = requested_id;
                existing.alive = true;
                return requested_id;
            }
        }
        thread.id = requested_id;
        if (requested_id >= next_id) next_id = requested_id + 1;
    } else {
        thread.id = next_id++;
    }
    threads.push_back(thread);
    return thread.id;
}

EclThread* EclOwner::find(i32 id) {
    for (EclThread& thread : threads) {
        if (thread.id == id && thread.alive) return &thread;
    }
    return nullptr;
}

bool EclOwner::cancel(i32 id) {
    EclThread* thread = find(id);
    if (!thread) return false;
    thread->alive = false;
    return true;
}

bool EclOwner::set_flag(i32 id, bool on) {
    EclThread* thread = find(id);
    if (!thread) return false;
    if (on) thread->flags |= 1u;
    else thread->flags &= ~1u;
    return true;
}

bool EclOwner::set_state(i32 id, u32 state) {
    EclThread* thread = find(id);
    if (!thread) return false;
    thread->state = state;
    return true;
}

u32 EclOwner::cancel_secondary() {
    u32 cancelled = 0;
    for (std::size_t i = 1; i < threads.size(); ++i) {
        if (threads[i].alive) {
            threads[i].alive = false;
            ++cancelled;
        }
    }
    return cancelled;
}

u32 EclOwner::live_count() const {
    u32 count = 0;
    for (const EclThread& thread : threads) {
        if (thread.alive) ++count;
    }
    return count;
}

}  // namespace th185
