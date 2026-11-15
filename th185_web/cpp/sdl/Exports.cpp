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
// Advances one frame WITHOUT rendering (diagnostics only).
//
// The 9-stage clear test runs up to 10000 frames per stage; with rendering
// that is ~27 minutes because the software rasteriser costs 4-20 ms a frame.
// Simulation does not depend on the framebuffer, so the test can skip it.
EMSCRIPTEN_KEEPALIVE int th185_tick_no_render(unsigned held) {
    return th185::app::instance().tick(held, false) ? 1 : 0;
}

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

EMSCRIPTEN_KEEPALIVE int th185_player_power() {
    return th185::app::instance().player().power();
}

EMSCRIPTEN_KEEPALIVE int th185_player_shot_damage() {
    return th185::app::instance().player().shot_damage();
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

EMSCRIPTEN_KEEPALIVE unsigned th185_schedules_total() {
    return th185::app::instance().stats().schedules_total;
}

EMSCRIPTEN_KEEPALIVE unsigned th185_schedules_fired() {
    return th185::app::instance().stats().schedules_fired;
}

EMSCRIPTEN_KEEPALIVE unsigned th185_ecl_scripts_active() {
    return th185::app::instance().active_script_count();
}

// Counts only enemy fire, so a test can subtract it from the total.
EMSCRIPTEN_KEEPALIVE unsigned th185_hostile_bullets_alive() {
    unsigned count = 0;
    for (const th185::Bullet& b : th185::app::instance().bullets().bullets()) {
        if (b.alive && b.hostile) ++count;
    }
    return count;
}

EMSCRIPTEN_KEEPALIVE unsigned th185_wave_templates() {
    return unsigned(th185::app::instance().ecl().wave_templates.size());
}

EMSCRIPTEN_KEEPALIVE int th185_last_wave_template() {
    return th185::app::instance().last_wave_template();
}

EMSCRIPTEN_KEEPALIVE unsigned th185_first_unsupported() {
    return th185::app::instance().stats().ecl_first_unsupported;
}

EMSCRIPTEN_KEEPALIVE unsigned th185_calls_made() {
    return unsigned(th185::app::instance().ecl().calls_made.size());
}

// Name of the i-th recorded Call target, for diagnostics.
EMSCRIPTEN_KEEPALIVE const char* th185_call_name(unsigned index) {
    const auto& calls = th185::app::instance().ecl().calls_made;
    if (index >= calls.size()) return "";
    return calls[index].c_str();
}

// Sprite index of the i-th live enemy, for verifying that op1011 actually
// drives the look of a wave.
EMSCRIPTEN_KEEPALIVE unsigned th185_enemy_sprite(unsigned index) {
    unsigned seen = 0;
    for (const th185::Enemy& e : th185::app::instance().enemies().enemies()) {
        if (!e.alive) continue;
        if (seen++ == index) return e.sprite;
    }
    return 0xFFFFFFFFu;
}

// Position of the i-th live enemy, for verifying that the .std group table
// really drives the formation.
EMSCRIPTEN_KEEPALIVE float th185_enemy_x_at(unsigned index) {
    unsigned seen = 0;
    for (const th185::Enemy& e : th185::app::instance().enemies().enemies()) {
        if (!e.alive) continue;
        if (seen++ == index) return e.x;
    }
    return 0.0f;
}

EMSCRIPTEN_KEEPALIVE float th185_enemy_y_at(unsigned index) {
    unsigned seen = 0;
    for (const th185::Enemy& e : th185::app::instance().enemies().enemies()) {
        if (!e.alive) continue;
        if (seen++ == index) return e.y;
    }
    return 0.0f;
}

// Number of .std group entries loaded for the current stage. Zero means the
// scene has no group table (the tutorial stages) or the .std failed to parse.
EMSCRIPTEN_KEEPALIVE unsigned th185_stage_groups() {
    return unsigned(th185::app::instance().stage().groups.size());
}

EMSCRIPTEN_KEEPALIVE unsigned th185_stage_waves() {
    return unsigned(th185::app::instance().stage().waves.size());
}

// Raw .std group values, for verifying the formation mapping.
EMSCRIPTEN_KEEPALIVE float th185_group_x(unsigned index) {
    const auto& g = th185::app::instance().stage().groups;
    return index < g.size() ? g[index].x : 0.0f;
}

EMSCRIPTEN_KEEPALIVE float th185_group_y(unsigned index) {
    const auto& g = th185::app::instance().stage().groups;
    return index < g.size() ? g[index].y : 0.0f;
}

// Raw decrypted+decompressed bytes of one archive entry, for differential
// testing against the Python reference. Returns the length written into the
// caller-provided buffer, or 0 on failure.
EMSCRIPTEN_KEEPALIVE unsigned th185_probe_extract(const char* name, unsigned char* out,
                                                  unsigned capacity) {
    const auto& archive = th185::app::instance().archive();
    std::size_t entry = archive.entries.size();
    for (std::size_t k = 0; k < archive.entries.size(); ++k) {
        if (archive.entries[k].name == name) { entry = k; break; }
    }
    if (entry >= archive.entries.size()) return 0;
    std::string err;
    std::vector<th185::u8> blob =
        archive.extract(th185::app::instance().archive_bytes(), entry, &err);
    if (blob.empty() || blob.size() > capacity) return 0;
    std::memcpy(out, blob.data(), blob.size());
    return unsigned(blob.size());
}

// Decrypt-only path for differential testing: returns the decrypted stored
// bytes WITHOUT LZSS, so a mismatch can be attributed to one stage.
EMSCRIPTEN_KEEPALIVE unsigned th185_probe_decrypt(const char* name,
                                                  unsigned char* out,
                                                  unsigned capacity) {
    const auto& app = th185::app::instance();
    const auto& archive = app.archive();
    std::size_t entry = archive.entries.size();
    for (std::size_t k = 0; k < archive.entries.size(); ++k) {
        if (archive.entries[k].name == name) { entry = k; break; }
    }
    if (entry >= archive.entries.size()) return 0;
    const th185::ArchiveEntry& e = archive.entries[entry];
    const th185::u32 stored = archive.stored_size(entry);
    if (stored == 0 || stored > capacity) return 0;
    const std::vector<th185::u8>& file = app.archive_bytes();
    std::memcpy(out, file.data() + e.offset, stored);
    const th185::CipherParams params = th185::cipher_for(e.name);
    th185::decrypt_in_place(out, stored, params.key, params.step, params.block,
                            params.limit);
    return stored;
}

// Number of ECL bullet patterns recorded so far (cumulative).
EMSCRIPTEN_KEEPALIVE unsigned th185_bullet_requests() {
    return unsigned(th185::app::instance().ecl().bullet_requests.size());
}

// Slot of the i-th recorded schedule, for verifying the slot -> group mapping.
EMSCRIPTEN_KEEPALIVE unsigned th185_schedule_slot(unsigned index) {
    const auto& s = th185::app::instance().ecl().schedules;
    return index < s.size() ? s[index].slot : 0xFFFFFFFFu;
}

// Hostile bullets still waiting on their op535 delay. They exist in the pool
// but must not render, move or collide yet.
EMSCRIPTEN_KEEPALIVE unsigned th185_bullets_delayed() {
    unsigned count = 0;
    for (const th185::Bullet& b : th185::app::instance().bullets().bullets()) {
        if (b.alive && b.delay > 0) ++count;
    }
    return count;
}

// Delay of entry i of the most recently recorded bullet request.
EMSCRIPTEN_KEEPALIVE unsigned th185_last_request_delay(unsigned index) {
    const auto& r = th185::app::instance().ecl().bullet_requests;
    if (r.empty() || index >= 8) return 0xFFFFFFFFu;
    return r.back().delays[index];
}

EMSCRIPTEN_KEEPALIVE unsigned th185_bullet_request_count() {
    return unsigned(th185::app::instance().ecl().bullet_requests.size());
}

EMSCRIPTEN_KEEPALIVE unsigned th185_requests_total() {
    return th185::app::instance().ecl().requests_total;
}

EMSCRIPTEN_KEEPALIVE unsigned th185_delays_seen_count() {
    return unsigned(th185::app::instance().ecl().delays_seen.size());
}

EMSCRIPTEN_KEEPALIVE unsigned th185_delays_seen_at(unsigned rec, unsigned idx) {
    const auto& v = th185::app::instance().ecl().delays_seen;
    if (rec >= v.size() || idx >= 8) return 0xFFFFFFFFu;
    return v[rec][idx];
}

EMSCRIPTEN_KEEPALIVE unsigned th185_wave_block_count() {
    return unsigned(th185::app::instance().ecl().wave_blocks.size());
}

EMSCRIPTEN_KEEPALIVE unsigned th185_wave_block_at(unsigned b, unsigned i) {
    const auto& v = th185::app::instance().ecl().wave_blocks;
    if (b >= v.size() || i >= v[b].size()) return 0xFFFFFFFFu;
    return unsigned(v[b][i]);
}

EMSCRIPTEN_KEEPALIVE unsigned th185_pending_templates() {
    return unsigned(th185::app::instance().ecl().pending_templates.size());
}

EMSCRIPTEN_KEEPALIVE unsigned th185_sub_count() {
    return unsigned(th185::app::instance().ecl().subroutines.size());
}

EMSCRIPTEN_KEEPALIVE unsigned th185_sub_code_size(const char* name) {
    const auto& s = th185::app::instance().ecl().subroutines;
    const auto it = s.find(name);
    return it == s.end() ? 0xFFFFFFFFu : unsigned(it->second.code.size());
}

EMSCRIPTEN_KEEPALIVE unsigned th185_sub_code_base(const char* name) {
    const auto& s = th185::app::instance().ecl().subroutines;
    const auto it = s.find(name);
    return it == s.end() ? 0xFFFFFFFFu : it->second.code_base;
}

// --- scoring ---
EMSCRIPTEN_KEEPALIVE unsigned th185_score() {
    return th185::app::instance().stats().score;
}

EMSCRIPTEN_KEEPALIVE unsigned th185_score_cap() {
    return th185::app::instance().stats().score_cap;
}

EMSCRIPTEN_KEEPALIVE unsigned th185_grazes() {
    return th185::app::instance().stats().grazes;
}

EMSCRIPTEN_KEEPALIVE unsigned th185_score_capped() {
    return th185::app::instance().stats().score_capped;
}

EMSCRIPTEN_KEEPALIVE unsigned th185_score_kills() {
    return th185::app::instance().scoring().stats().kills;
}


// --- difficulty ---
EMSCRIPTEN_KEEPALIVE void th185_set_difficulty(unsigned difficulty) {
    th185::app::instance().set_difficulty(difficulty);
}

EMSCRIPTEN_KEEPALIVE unsigned th185_difficulty() {
    return th185::app::instance().difficulty();
}

// The difficulty the ECL interpreter itself sees (diagnostics only).
EMSCRIPTEN_KEEPALIVE unsigned th185_ecl_difficulty() {
    return th185::app::instance().ecl().difficulty;
}

EMSCRIPTEN_KEEPALIVE unsigned th185_ecl_difficulty_skipped() {
    return th185::app::instance().ecl_difficulty_skipped();
}

EMSCRIPTEN_KEEPALIVE unsigned th185_first_masked_opcode() {
    return th185::app::instance().ecl_first_masked_opcode();
}

EMSCRIPTEN_KEEPALIVE unsigned th185_first_masked_rank() {
    return th185::app::instance().ecl_first_masked_rank();
}

// The enemy family named by the most recent op300/op304 (diagnostics only).
EMSCRIPTEN_KEEPALIVE const char* th185_enemy_family() {
    return th185::app::instance().ecl().enemy_family.c_str();
}

EMSCRIPTEN_KEEPALIVE unsigned th185_enemy_family_calls() {
    return th185::app::instance().ecl().enemy_family_calls;
}

// How many times op602 set a bullet appearance (diagnostics only).
EMSCRIPTEN_KEEPALIVE unsigned th185_bullet_appearance_sets() {
    return th185::app::instance().ecl().bullet_appearance_sets;
}

// Distinct bullet sprite indices currently alive (diagnostics only).
EMSCRIPTEN_KEEPALIVE unsigned th185_bullet_sprite_kinds() {
    return th185::app::instance().bullet_sprite_kinds();
}

// Diagnostics for the op58 divide-by-zero in wave45/wave65.
EMSCRIPTEN_KEEPALIVE unsigned th185_div0_count() {
    return th185::app::instance().ecl().div0_count;
}

// Enemy state-flag writes from the TH20-referenced opcode block.
EMSCRIPTEN_KEEPALIVE unsigned th185_enemy_flag_writes() {
    return th185::app::instance().ecl().enemy_flag_writes;
}

EMSCRIPTEN_KEEPALIVE unsigned th185_enemy_state_flags() {
    return th185::app::instance().ecl().enemy_state_flags;
}

EMSCRIPTEN_KEEPALIVE unsigned th185_enemy_health_sets() {
    return th185::app::instance().ecl().enemy_health_sets;
}

EMSCRIPTEN_KEEPALIVE unsigned th185_enemy_health_at(unsigned i) {
    const auto& w = th185::app::instance().ecl().enemy_health_words;
    return i < w.size() ? w[i] : 0u;
}

EMSCRIPTEN_KEEPALIVE int th185_div0_left() {
    return th185::app::instance().ecl().div0_left;
}

EMSCRIPTEN_KEEPALIVE unsigned th185_div0_had_left() {
    return th185::app::instance().ecl().div0_had_left;
}

EMSCRIPTEN_KEEPALIVE unsigned th185_div0_had_right() {
    return th185::app::instance().ecl().div0_had_right;
}

EMSCRIPTEN_KEEPALIVE unsigned th185_div0_builtin0() {
    return th185::app::instance().ecl().div0_builtin0;
}

EMSCRIPTEN_KEEPALIVE unsigned th185_idiv_overflow() {
    return th185::app::instance().ecl().idiv_overflow_count;
}

EMSCRIPTEN_KEEPALIVE unsigned th185_stack_overflow() {
    return th185::app::instance().ecl().stack_overflow_count;
}

EMSCRIPTEN_KEEPALIVE unsigned th185_overflow_depth() {
    return th185::app::instance().ecl().overflow_depth;
}

// Background quads drawn last frame (diagnostics only).
EMSCRIPTEN_KEEPALIVE unsigned th185_background_quads() {
    return th185::app::instance().stats().background_quads;
}

EMSCRIPTEN_KEEPALIVE unsigned th185_stage_entities() {
    return unsigned(th185::app::instance().stage_entity_count());
}

EMSCRIPTEN_KEEPALIVE unsigned th185_rank_ff() {
    return th185::app::instance().ecl_rank_ff();
}

EMSCRIPTEN_KEEPALIVE unsigned th185_rank_other() {
    return th185::app::instance().ecl_rank_other();
}

// Reads a builtin slot back, so a test can confirm the ECL scripts will see
// the difficulty the shell selected.
EMSCRIPTEN_KEEPALIVE unsigned th185_builtin_slot(unsigned slot) {
    const auto& b = th185::app::instance().ecl().builtins;
    return slot < b.size() ? b[slot] : 0xFFFFFFFFu;
}

EMSCRIPTEN_KEEPALIVE unsigned th185_bombs_left() {
    return th185::app::instance().stats().bombs_left;
}

EMSCRIPTEN_KEEPALIVE unsigned th185_bombs_used() {
    return th185::app::instance().stats().bombs_used;
}

EMSCRIPTEN_KEEPALIVE unsigned th185_items_alive() {
    return th185::app::instance().stats().items_alive;
}

EMSCRIPTEN_KEEPALIVE unsigned th185_items_collected() {
    return th185::app::instance().stats().items_collected;
}

EMSCRIPTEN_KEEPALIVE unsigned th185_life_fragments() {
    return th185::app::instance().stats().life_fragments;
}

EMSCRIPTEN_KEEPALIVE unsigned th185_item_x_at(unsigned i) {
    const auto& p = th185::app::instance().items();
    return i < th185::ITEM_CAPACITY && p.items()[i].alive
               ? unsigned(int(p.items()[i].x + 4096.0f)) : 0xFFFFFFFFu;
}

EMSCRIPTEN_KEEPALIVE unsigned th185_item_y_at(unsigned i) {
    const auto& p = th185::app::instance().items();
    return i < th185::ITEM_CAPACITY && p.items()[i].alive
               ? unsigned(int(p.items()[i].y + 4096.0f)) : 0xFFFFFFFFu;
}

EMSCRIPTEN_KEEPALIVE unsigned th185_player_lives_gained() {
    return th185::app::instance().player().lives_gained();
}


EMSCRIPTEN_KEEPALIVE unsigned th185_game_over() {
    return th185::app::instance().stats().game_over;
}

EMSCRIPTEN_KEEPALIVE unsigned th185_deaths() {
    return th185::app::instance().stats().deaths;
}

EMSCRIPTEN_KEEPALIVE unsigned th185_sounds_loaded() {
    return th185::app::instance().stats().sounds_loaded;
}

EMSCRIPTEN_KEEPALIVE unsigned th185_sounds_played() {
    return th185::app::instance().stats().sounds_played;
}

EMSCRIPTEN_KEEPALIVE unsigned th185_sounds_dropped() {
    return th185::app::instance().stats().sounds_dropped;
}

// Frames in the i-th loaded sample; 0xFFFFFFFF when the index is out of range.
EMSCRIPTEN_KEEPALIVE unsigned th185_sound_frames(unsigned i) {
    const auto& s = th185::app::instance().sounds().samples();
    return i < s.size() ? s[i].frame_count : 0xFFFFFFFFu;
}

EMSCRIPTEN_KEEPALIVE unsigned th185_sound_rate(unsigned i) {
    const auto& s = th185::app::instance().sounds().samples();
    return i < s.size() ? s[i].sample_rate : 0xFFFFFFFFu;
}

EMSCRIPTEN_KEEPALIVE unsigned th185_sound_channels(unsigned i) {
    const auto& s = th185::app::instance().sounds().samples();
    return i < s.size() ? s[i].channels : 0xFFFFFFFFu;
}

EMSCRIPTEN_KEEPALIVE unsigned th185_sounds_throttled() {
    return th185::app::instance().stats().sounds_throttled;
}

// --- BGM ---
//
// thbgm.dat is passed through its own path because it is a separate file from
// the archive. The bytes are copied into the module heap once.
EMSCRIPTEN_KEEPALIVE int th185_attach_bgm(const unsigned char* data, unsigned size) {
    std::vector<th185::u8> bytes(data, data + size);
    return th185::app::instance().attach_bgm(std::move(bytes)) ? 1 : 0;
}

EMSCRIPTEN_KEEPALIVE unsigned th185_bgm_tracks() {
    return th185::app::instance().stats().bgm_tracks;
}

EMSCRIPTEN_KEEPALIVE unsigned th185_bgm_play(unsigned index) {
    return th185::app::instance().play_bgm(index) ? 1u : 0u;
}

EMSCRIPTEN_KEEPALIVE unsigned th185_bgm_track() {
    return th185::app::instance().stats().bgm_track;
}

EMSCRIPTEN_KEEPALIVE unsigned th185_bgm_playing() {
    return th185::app::instance().stats().bgm_playing;
}

EMSCRIPTEN_KEEPALIVE unsigned th185_bgm_start(unsigned i) {
    const auto& t = th185::app::instance().bgm().tracks();
    return i < t.size() ? t[i].start_offset : 0xFFFFFFFFu;
}

EMSCRIPTEN_KEEPALIVE unsigned th185_bgm_length(unsigned i) {
    const auto& t = th185::app::instance().bgm().tracks();
    return i < t.size() ? t[i].length_bytes : 0xFFFFFFFFu;
}

EMSCRIPTEN_KEEPALIVE unsigned th185_bgm_rate(unsigned i) {
    const auto& t = th185::app::instance().bgm().tracks();
    return i < t.size() ? t[i].sample_rate : 0xFFFFFFFFu;
}

// --- audio output ---
//
// The mixer runs inside tick(), but a browser needs the samples to hand to
// WebAudio. Rather than interleaving callbacks, the shell pulls one frame of
// audio after each tick: `th185_audio_frames()` samples at 22050 Hz stereo,
// interleaved float32, in a buffer that stays valid until the next call.
constexpr unsigned AUDIO_MIX_RATE = 22050;
constexpr unsigned AUDIO_FRAMES_PER_TICK = AUDIO_MIX_RATE / 60;

EMSCRIPTEN_KEEPALIVE unsigned th185_audio_frames_per_tick() {
    return AUDIO_FRAMES_PER_TICK;
}

EMSCRIPTEN_KEEPALIVE unsigned th185_audio_rate() {
    return AUDIO_MIX_RATE;
}

EMSCRIPTEN_KEEPALIVE float* th185_audio_frame() {
    return th185::app::instance().audio_frame();
}

EMSCRIPTEN_KEEPALIVE unsigned th185_audio_frame_samples() {
    return th185::app::instance().audio_frame_samples();
}

EMSCRIPTEN_KEEPALIVE unsigned th185_stage_signal() {
    return th185::app::instance().ecl().stage_signal;
}

EMSCRIPTEN_KEEPALIVE unsigned th185_stage_signal_sets() {
    return th185::app::instance().ecl().stage_signal_sets;
}

EMSCRIPTEN_KEEPALIVE unsigned th185_stage_signal_waits() {
    return th185::app::instance().ecl().stage_signal_waits;
}

// Program counter of the scene controller, for verifying how far main got.
EMSCRIPTEN_KEEPALIVE unsigned th185_main_pc() {
    const auto& c = th185::app::instance().ecl().coroutines;
    const auto it = c.find("main");
    return it == c.end() ? 0xFFFFFFFFu : it->second.pc;
}

EMSCRIPTEN_KEEPALIVE unsigned th185_main_wait() {
    const auto& c = th185::app::instance().ecl().coroutines;
    const auto it = c.find("main");
    return it == c.end() ? 0xFFFFFFFFu : it->second.wait_frames;
}

EMSCRIPTEN_KEEPALIVE unsigned th185_stage_clear() {
    return th185::app::instance().stats().stage_clear;
}

EMSCRIPTEN_KEEPALIVE unsigned th185_clear_frames() {
    return th185::app::instance().stats().clear_frames;
}

// End-of-stage bookkeeping.
EMSCRIPTEN_KEEPALIVE unsigned th185_clear_frame() {
    return th185::app::instance().stats().clear_frame;
}

EMSCRIPTEN_KEEPALIVE unsigned th185_banked() {
    return th185::app::instance().stats().banked;
}

EMSCRIPTEN_KEEPALIVE unsigned th185_market_open_flag() {
    return th185::app::instance().stats().market_open;
}

// Resets the loadout limits to their starting values (tests only).
//
// Progress accumulates across clears, so a probe that measures the starting
// cap must be able to get back to it.
EMSCRIPTEN_KEEPALIVE void th185_reset_progress() {
    th185::app::instance().cards().set_loadout_slots(8);
    th185::app::instance().cards().set_cost_cap(100);
    th185::app::instance().cards().clear_loadout();
}

// Pause menu: 0=Resume, 1=Restart, 2=Quit.
EMSCRIPTEN_KEEPALIVE unsigned th185_pause_item() {
    return unsigned(th185::app::instance().pause_item());
}

EMSCRIPTEN_KEEPALIVE void th185_pause_move(int delta) {
    th185::app::instance().pause_move(delta);
}

EMSCRIPTEN_KEEPALIVE void th185_pause_confirm() {
    th185::app::instance().pause_confirm();
}

// Loadout limits after progress (what the original's cost/slot growth yields).
EMSCRIPTEN_KEEPALIVE unsigned th185_progress_slots() {
    return th185::app::instance().cards().loadout_slots();
}

EMSCRIPTEN_KEEPALIVE unsigned th185_progress_cost_cap() {
    return th185::app::instance().cards().cost_cap();
}

// ECL interpreter counters, exported so a test can prove they actually move.
// Before ecl_merge_check.py these were written by the interpreter and then
// thrown away by the context merge, so every one of them read as 0.
EMSCRIPTEN_KEEPALIVE unsigned th185_ecl_query_count() {
    return th185::app::instance().ecl().query_count;
}

EMSCRIPTEN_KEEPALIVE unsigned th185_ecl_set_count() {
    return th185::app::instance().ecl().set_count;
}

EMSCRIPTEN_KEEPALIVE unsigned th185_ecl_position_sets() {
    return th185::app::instance().ecl().position_sets;
}

EMSCRIPTEN_KEEPALIVE unsigned th185_ecl_comments() {
    return unsigned(th185::app::instance().ecl().comments.size());
}

EMSCRIPTEN_KEEPALIVE unsigned th185_ecl_timer_writes() {
    return th185::app::instance().ecl().timer_writes;
}

EMSCRIPTEN_KEEPALIVE unsigned th185_ecl_call_depth() {
    return th185::app::instance().ecl().call_depth;
}

EMSCRIPTEN_KEEPALIVE unsigned th185_active_script_count() {
    return th185::app::instance().active_script_count();
}

EMSCRIPTEN_KEEPALIVE unsigned th185_boss_index() {
    return th185::app::instance().ecl().boss_index;
}

EMSCRIPTEN_KEEPALIVE unsigned th185_boss_index_sets() {
    return th185::app::instance().ecl().boss_index_sets;
}

EMSCRIPTEN_KEEPALIVE unsigned th185_bullet_patterns() {
    return th185::app::instance().ecl().bullet_patterns;
}

EMSCRIPTEN_KEEPALIVE unsigned th185_bullet_launches() {
    return th185::app::instance().ecl().bullet_launches;
}

EMSCRIPTEN_KEEPALIVE unsigned th185_effect_slot_calls() {
    return th185::app::instance().ecl().effect_slot_calls;
}

EMSCRIPTEN_KEEPALIVE unsigned th185_boss_phases() {
    return th185::app::instance().ecl().boss_phases;
}

EMSCRIPTEN_KEEPALIVE unsigned th185_position_pairs() {
    return th185::app::instance().ecl().position_pairs;
}

EMSCRIPTEN_KEEPALIVE unsigned th185_attack_prepares() {
    return th185::app::instance().ecl().attack_prepares;
}

EMSCRIPTEN_KEEPALIVE unsigned th185_timed_values() {
    return th185::app::instance().ecl().timed_values;
}

EMSCRIPTEN_KEEPALIVE unsigned th185_pattern_extras() {
    return th185::app::instance().ecl().pattern_extras;
}

EMSCRIPTEN_KEEPALIVE unsigned th185_spawn_boxes() {
    return th185::app::instance().ecl().spawn_boxes;
}

EMSCRIPTEN_KEEPALIVE unsigned th185_named_call_exts() {
    return th185::app::instance().ecl().named_call_exts;
}

EMSCRIPTEN_KEEPALIVE unsigned th185_named_effect_calls() {
    return th185::app::instance().ecl().named_effect_calls;
}

// PC and wait of the i-th coroutine whose name starts with `prefix`.
// Returns 0xFFFFFFFF when there is no i-th match.
EMSCRIPTEN_KEEPALIVE unsigned th185_coro_count(const char* prefix) {
    const auto& c = th185::app::instance().ecl().coroutines;
    unsigned n = 0;
    const std::string p(prefix);
    for (const auto& kv : c) {
        if (kv.first.compare(0, p.size(), p) == 0) ++n;
    }
    return n;
}

EMSCRIPTEN_KEEPALIVE unsigned th185_coro_pc(const char* prefix, unsigned i) {
    const auto& c = th185::app::instance().ecl().coroutines;
    const std::string p(prefix);
    unsigned n = 0;
    for (const auto& kv : c) {
        if (kv.first.compare(0, p.size(), p) != 0) continue;
        if (n == i) return kv.second.pc;
        ++n;
    }
    return 0xFFFFFFFFu;
}

EMSCRIPTEN_KEEPALIVE unsigned th185_coro_wait(const char* prefix, unsigned i) {
    const auto& c = th185::app::instance().ecl().coroutines;
    const std::string p(prefix);
    unsigned n = 0;
    for (const auto& kv : c) {
        if (kv.first.compare(0, p.size(), p) != 0) continue;
        if (n == i) return kv.second.wait_frames;
        ++n;
    }
    return 0xFFFFFFFFu;
}

EMSCRIPTEN_KEEPALIVE unsigned th185_boss_spawned() {
    return th185::app::instance().stats().boss_spawned;
}

EMSCRIPTEN_KEEPALIVE unsigned th185_boss_alive() {
    return th185::app::instance().enemies().boss_alive() ? 1u : 0u;
}

EMSCRIPTEN_KEEPALIVE unsigned th185_schedule_count() {
    return unsigned(th185::app::instance().ecl().schedules.size());
}

EMSCRIPTEN_KEEPALIVE unsigned th185_schedule_slot_at(unsigned i) {
    const auto& s = th185::app::instance().ecl().schedules;
    return i < s.size() ? s[i].slot : 0xFFFFFFFFu;
}

EMSCRIPTEN_KEEPALIVE unsigned th185_schedule_time_at(unsigned i, unsigned k) {
    const auto& s = th185::app::instance().ecl().schedules;
    return (i < s.size() && k < 8) ? s[i].times[k] : 0xFFFFFFFFu;
}

EMSCRIPTEN_KEEPALIVE unsigned th185_schedules_expected() {
    return th185::app::instance().stats().schedules_expected;
}

EMSCRIPTEN_KEEPALIVE const char* th185_active_script_name(unsigned i) {
    const auto& s = th185::app::instance().active_scripts();
    return i < s.size() ? s[i].c_str() : "";
}

// Re-renders the current frame (diagnostics only).
//
// tick() renders internally, so this is called twice in a row to price the
// rasteriser: the first call pays for the clear plus every quad, the second
// one is the same work again. Comparing it against a whole tick tells whether
// stage 5's 58 ms/frame is simulation or drawing.
EMSCRIPTEN_KEEPALIVE void th185_render_only() {
    th185::app::instance().render();
}

// --- Cards ---------------------------------------------------------------

EMSCRIPTEN_KEEPALIVE unsigned th185_card_count() {
    return th185::CARD_COUNT;
}

EMSCRIPTEN_KEEPALIVE unsigned th185_card_id(unsigned index) {
    const auto& table = th185::cards();
    return index < table.size() ? table[index].id : 0xFFFFFFFFu;
}

EMSCRIPTEN_KEEPALIVE const char* th185_card_name(unsigned index) {
    const auto& table = th185::cards();
    return index < table.size() ? table[index].name : "";
}

EMSCRIPTEN_KEEPALIVE unsigned th185_card_price(unsigned index) {
    const auto& table = th185::cards();
    return index < table.size() ? table[index].price() : 0u;
}

EMSCRIPTEN_KEEPALIVE unsigned th185_card_cost(unsigned index) {
    const auto& table = th185::cards();
    return index < table.size() ? table[index].cost() : 0u;
}

// Equip budget: `Total Cost %d%%/%d%%` in the original.
EMSCRIPTEN_KEEPALIVE unsigned th185_cost_cap() {
    return th185::app::instance().cards().cost_cap();
}

EMSCRIPTEN_KEEPALIVE unsigned th185_loadout_cost() {
    return th185::app::instance().cards().loadout_cost();
}

EMSCRIPTEN_KEEPALIVE unsigned th185_loadout_slots() {
    return th185::app::instance().cards().loadout_slots();
}

EMSCRIPTEN_KEEPALIVE void th185_grant_progress(unsigned slots, unsigned cost) {
    th185::app::instance().cards().grant_progress(slots, cost);
}

EMSCRIPTEN_KEEPALIVE unsigned th185_card_is_active(unsigned index) {
    const auto& table = th185::cards();
    return (index < table.size() && table[index].is_active()) ? 1u : 0u;
}

EMSCRIPTEN_KEEPALIVE void th185_cards_add_currency(unsigned amount) {
    th185::app::instance().cards().add_currency(amount);
}

EMSCRIPTEN_KEEPALIVE unsigned th185_cards_currency() {
    return th185::app::instance().cards().currency();
}

EMSCRIPTEN_KEEPALIVE int th185_cards_buy(unsigned card_id) {
    return th185::app::instance().cards().buy(card_id) ? 1 : 0;
}

EMSCRIPTEN_KEEPALIVE unsigned th185_cards_owned() {
    return th185::app::instance().cards().owned_count();
}

EMSCRIPTEN_KEEPALIVE int th185_cards_equip(unsigned card_id) {
    return th185::app::instance().cards().equip(card_id) ? 1 : 0;
}

EMSCRIPTEN_KEEPALIVE void th185_cards_clear_loadout() {
    th185::app::instance().cards().clear_loadout();
}

EMSCRIPTEN_KEEPALIVE unsigned th185_cards_loadout_size() {
    return unsigned(th185::app::instance().cards().loadout().size());
}

EMSCRIPTEN_KEEPALIVE unsigned th185_cards_loadout_at(unsigned index) {
    const auto& l = th185::app::instance().cards().loadout();
    return index < l.size() ? l[index] : 0xFFFFFFFFu;
}

EMSCRIPTEN_KEEPALIVE unsigned th185_cards_extra_lives() {
    return th185::app::instance().cards().extra_lives();
}

EMSCRIPTEN_KEEPALIVE unsigned th185_cards_extra_bombs() {
    return th185::app::instance().cards().extra_bombs();
}

EMSCRIPTEN_KEEPALIVE unsigned th185_cards_shot_bonus() {
    return unsigned(th185::app::instance().cards().shot_power_bonus());
}

// --- Dual currency -------------------------------------------------------
//
// funds @ 0x4d106c (cap 999,990) and bulletMoney @ 0x4d1070 in the original;
// see docs/BUILD.md.
EMSCRIPTEN_KEEPALIVE void th185_add_funds(unsigned amount) {
    th185::app::instance().cards().add_funds(amount);
}

EMSCRIPTEN_KEEPALIVE unsigned th185_funds() {
    return th185::app::instance().cards().funds();
}

EMSCRIPTEN_KEEPALIVE unsigned th185_funds_cap() {
    return th185::FUNDS_CAP;
}

EMSCRIPTEN_KEEPALIVE void th185_add_bullet_money(int amount) {
    th185::app::instance().cards().add_bullet_money(amount);
}

EMSCRIPTEN_KEEPALIVE int th185_bullet_money() {
    return th185::app::instance().cards().bullet_money();
}

EMSCRIPTEN_KEEPALIVE void th185_void_bullet_money() {
    th185::app::instance().cards().void_bullet_money();
}

EMSCRIPTEN_KEEPALIVE unsigned th185_bank_bullet_money() {
    return th185::app::instance().cards().bank_bullet_money();
}

// --- Stage structure -----------------------------------------------------
//
// Nine selectable scenes; see docs/BUILD.md for the wave counts and boss
// candidates.
EMSCRIPTEN_KEEPALIVE unsigned th185_scene_count() {
    return th185::SCENE_COUNT;
}

EMSCRIPTEN_KEEPALIVE const char* th185_scene_name(unsigned scene) {
    const auto& s = th185::scenes();
    return scene < s.size() ? s[scene].name : "";
}

EMSCRIPTEN_KEEPALIVE unsigned th185_scene_waves(unsigned scene) {
    const auto& s = th185::scenes();
    return scene < s.size() ? s[scene].waves : 0u;
}

EMSCRIPTEN_KEEPALIVE unsigned th185_scene_boss_candidates(unsigned scene) {
    const auto& s = th185::scenes();
    return scene < s.size() ? s[scene].boss_candidates : 0u;
}

EMSCRIPTEN_KEEPALIVE unsigned th185_scene_pool_size(unsigned scene) {
    return unsigned(th185::scene_card_pool(scene).size());
}

EMSCRIPTEN_KEEPALIVE unsigned th185_scene_pool_at(unsigned scene, unsigned i) {
    const auto pool = th185::scene_card_pool(scene);
    return i < pool.size() ? pool[i] : 0xFFFFFFFFu;
}

// --- Black market --------------------------------------------------------

EMSCRIPTEN_KEEPALIVE void th185_market_open(unsigned seed) {
    // Offers are drawn from the cards that carry a price, which is this
    // build's stand-in for "the cards this stage's boss can drop".
    std::vector<unsigned> pool;
    const auto& table = th185::cards();
    for (const th185::CardEntry& card : table) {
        if (card.price() > 0) pool.push_back(card.id);
    }
    th185::app::instance().cards().open_market(pool, seed);
}

EMSCRIPTEN_KEEPALIVE unsigned th185_market_offers() {
    return unsigned(th185::app::instance().cards().market_offers().size());
}

EMSCRIPTEN_KEEPALIVE unsigned th185_market_offer_at(unsigned index) {
    const auto& o = th185::app::instance().cards().market_offers();
    return index < o.size() ? o[index] : 0xFFFFFFFFu;
}

EMSCRIPTEN_KEEPALIVE unsigned th185_market_purchases_left() {
    return th185::app::instance().cards().market_purchases_left();
}

EMSCRIPTEN_KEEPALIVE int th185_market_buy(unsigned index) {
    return th185::app::instance().cards().market_buy(index) ? 1 : 0;
}

EMSCRIPTEN_KEEPALIVE unsigned th185_temporary_cards() {
    return unsigned(th185::app::instance().cards().temporary_cards().size());
}

// What the equipped loadout actually did to the run (applied in reset_scenario).
EMSCRIPTEN_KEEPALIVE unsigned th185_card_extra_lives_applied() {
    return th185::app::instance().stats().card_extra_lives;
}

EMSCRIPTEN_KEEPALIVE unsigned th185_card_extra_bombs_applied() {
    return th185::app::instance().stats().card_extra_bombs;
}

// Requests the last tick turned into bullets (diagnostics only).
EMSCRIPTEN_KEEPALIVE unsigned th185_last_frame_requests() {
    return th185::app::instance().stats().last_frame_requests;
}

// Total bullets the pending requests would emit (sum of angles.size()).
//
// Diagnostics only: this is how the emission size was traced back to op606.
EMSCRIPTEN_KEEPALIVE unsigned th185_request_total_bullets() {
    const auto& r = th185::app::instance().ecl().bullet_requests;
    unsigned n = 0;
    for (const auto& q : r) n += unsigned(q.angles.size());
    return n;
}

// Instructions executed by the i-th active script during the last tick.
EMSCRIPTEN_KEEPALIVE unsigned th185_script_instrs(unsigned i) {
    const auto& v = th185::app::instance().stats().script_instrs;
    return i < v.size() ? v[i] : 0u;
}

EMSCRIPTEN_KEEPALIVE const char* th185_error() {
    return th185::app::instance().error().c_str();
}

}  // extern "C"
