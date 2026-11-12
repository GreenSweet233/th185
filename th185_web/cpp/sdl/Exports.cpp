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

EMSCRIPTEN_KEEPALIVE const char* th185_error() {
    return th185::app::instance().error().c_str();
}

}  // extern "C"
