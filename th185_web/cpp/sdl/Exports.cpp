// C exports for the web shell (sdl-runtime/shell.mjs).
//
// The shell expects a specific surface; this file provides exactly that and
// nothing more. Each export is a thin wrapper -- no logic lives here.
//
// Naming mirrors th11 so the shell scripts stay similar, but the set is
// deliberately smaller: there is no rendering, audio or input subsystem yet,
// so exporting th11_*_audio_probe style functions would be dishonest.

#include "Application.hpp"

#include <cstdio>
#include <cstring>
#include <vector>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#else
#define EMSCRIPTEN_KEEPALIVE
#endif

namespace {

// The archive bytes live in the module's filesystem; this buffers them once.
std::vector<unsigned char> g_archive;

}  // namespace

extern "C" {

// Loads th185.dat from the virtual filesystem. Returns 1 on success.
EMSCRIPTEN_KEEPALIVE int th185_initialize() {
    using namespace th185::app;
    Application& app = instance();

    // The shell writes the archive to /th185.dat before calling this.
    FILE* file = std::fopen("/th185.dat", "rb");
    if (!file) {
        return 0;
    }
    std::fseek(file, 0, SEEK_END);
    const long size = std::ftell(file);
    std::fseek(file, 0, SEEK_SET);
    if (size <= 0) {
        std::fclose(file);
        return 0;
    }
    g_archive.resize(std::size_t(size));
    const std::size_t read = std::fread(g_archive.data(), 1, g_archive.size(), file);
    std::fclose(file);
    if (read != g_archive.size()) {
        return 0;
    }

    std::vector<th185::u8> bytes(g_archive.begin(), g_archive.end());
    return app.initialize(std::move(bytes)) ? 1 : 0;
}

// Advances one tick. Returns 0 on fatal error, 1 otherwise.
EMSCRIPTEN_KEEPALIVE int th185_tick(unsigned held) {
    return th185::app::instance().tick(held) ? 1 : 0;
}

EMSCRIPTEN_KEEPALIVE int th185_start() {
    return th185::app::instance().start() ? 1 : 0;
}

EMSCRIPTEN_KEEPALIVE int th185_pause() {
    return th185::app::instance().pause() ? 1 : 0;
}

EMSCRIPTEN_KEEPALIVE int th185_resume() {
    return th185::app::instance().resume() ? 1 : 0;
}

EMSCRIPTEN_KEEPALIVE int th185_restart() {
    return th185::app::instance().restart() ? 1 : 0;
}

EMSCRIPTEN_KEEPALIVE int th185_load_stage(unsigned stage) {
    return th185::app::instance().load_stage(stage) ? 1 : 0;
}

EMSCRIPTEN_KEEPALIVE unsigned th185_phase() {
    return unsigned(th185::app::instance().phase());
}

EMSCRIPTEN_KEEPALIVE unsigned th185_frame() {
    return th185::app::instance().stats().frame;
}

EMSCRIPTEN_KEEPALIVE unsigned th185_ticks() {
    return th185::app::instance().stats().ticks;
}

EMSCRIPTEN_KEEPALIVE unsigned th185_ecl_instructions() {
    return th185::app::instance().stats().ecl_instructions;
}

EMSCRIPTEN_KEEPALIVE unsigned th185_ecl_faults() {
    return th185::app::instance().stats().ecl_faults;
}

EMSCRIPTEN_KEEPALIVE unsigned th185_max_stack_depth() {
    return th185::app::instance().stats().max_stack_depth;
}

EMSCRIPTEN_KEEPALIVE unsigned th185_runnable_subroutines() {
    return th185::app::instance().runnable_subroutines();
}

EMSCRIPTEN_KEEPALIVE unsigned th185_frame_width() {
    return th185::app::instance().framebuffer_width();
}

EMSCRIPTEN_KEEPALIVE unsigned th185_frame_height() {
    return th185::app::instance().framebuffer_height();
}

// Returns a pointer into the framebuffer. Valid until the next tick().
EMSCRIPTEN_KEEPALIVE const unsigned char* th185_frame_pixels() {
    const std::vector<th185::u8>& pixels = th185::app::instance().framebuffer();
    return pixels.empty() ? nullptr : pixels.data();
}

EMSCRIPTEN_KEEPALIVE unsigned th185_frame_bytes() {
    return unsigned(th185::app::instance().framebuffer().size());
}

EMSCRIPTEN_KEEPALIVE unsigned th185_render_quads() {
    return th185::app::instance().stats().quads;
}

EMSCRIPTEN_KEEPALIVE unsigned th185_live_animations() {
    return th185::app::instance().stats().live_animations;
}

EMSCRIPTEN_KEEPALIVE const char* th185_animation_bank() {
    return th185::app::instance().animation_bank().c_str();
}

EMSCRIPTEN_KEEPALIVE unsigned th185_stage_index() {
    return th185::app::instance().session().stage_index;
}

EMSCRIPTEN_KEEPALIVE float th185_player_x() {
    return th185::app::instance().stats().player_x;
}

EMSCRIPTEN_KEEPALIVE float th185_player_y() {
    return th185::app::instance().stats().player_y;
}

EMSCRIPTEN_KEEPALIVE unsigned th185_player_focused() {
    return th185::app::instance().stats().player_focused ? 1u : 0u;
}

EMSCRIPTEN_KEEPALIVE unsigned th185_player_shots() {
    return th185::app::instance().stats().player_shots;
}

EMSCRIPTEN_KEEPALIVE unsigned th185_player_bombs() {
    return th185::app::instance().stats().player_bombs;
}

EMSCRIPTEN_KEEPALIVE unsigned th185_bullets_alive() {
    return th185::app::instance().stats().bullets_alive;
}

EMSCRIPTEN_KEEPALIVE unsigned th185_bullets_fired() {
    return th185::app::instance().stats().bullets_fired;
}

EMSCRIPTEN_KEEPALIVE unsigned th185_enemies_alive() {
    return th185::app::instance().stats().enemies_alive;
}

EMSCRIPTEN_KEEPALIVE unsigned th185_enemies_killed() {
    return th185::app::instance().stats().enemies_killed;
}

EMSCRIPTEN_KEEPALIVE unsigned th185_collision_hits() {
    return th185::app::instance().stats().collision_hits;
}

// Spawns `count` drifting targets. Used by tests to set up a scenario.
EMSCRIPTEN_KEEPALIVE unsigned th185_spawn_test_wave(unsigned count) {
    return th185::app::instance().spawn_test_wave(count);
}

// First live enemy position, or 0 when none. For diagnosing collisions.
EMSCRIPTEN_KEEPALIVE float th185_debug_enemy_x() {
    for (const th185::Enemy& e : th185::app::instance().enemies().enemies()) {
        if (e.alive) return e.x;
    }
    return 0.0f;
}

EMSCRIPTEN_KEEPALIVE float th185_debug_enemy_y() {
    for (const th185::Enemy& e : th185::app::instance().enemies().enemies()) {
        if (e.alive) return e.y;
    }
    return 0.0f;
}

EMSCRIPTEN_KEEPALIVE float th185_debug_bullet_x() {
    for (const th185::Bullet& b : th185::app::instance().bullets().bullets()) {
        if (b.alive) return b.x;
    }
    return 0.0f;
}

EMSCRIPTEN_KEEPALIVE float th185_debug_bullet_y() {
    for (const th185::Bullet& b : th185::app::instance().bullets().bullets()) {
        if (b.alive) return b.y;
    }
    return 0.0f;
}

EMSCRIPTEN_KEEPALIVE void th185_reset_scenario() {
    th185::app::instance().reset_scenario();
}

EMSCRIPTEN_KEEPALIVE unsigned th185_player_lives() {
    return th185::app::instance().stats().player_lives;
}

EMSCRIPTEN_KEEPALIVE unsigned th185_player_hits_taken() {
    return th185::app::instance().stats().player_hits_taken;
}

EMSCRIPTEN_KEEPALIVE unsigned th185_enemy_shots() {
    return th185::app::instance().stats().enemy_shots;
}

EMSCRIPTEN_KEEPALIVE const char* th185_error() {
    return th185::app::instance().error().c_str();
}

}  // extern "C"
