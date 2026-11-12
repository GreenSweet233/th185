// Harness: run the REAL compiled C++ managers over the real th185.dat.
//
// Everything is printed in a stable, line-oriented format so a Python check can
// diff it against the reference implementations. Nothing here is part of the
// game; it exists purely so the C++ is exercised on real data rather than only
// compiled.

#include "Archive.hpp"
#include "CardManager.hpp"
#include "Difficulty.hpp"
#include "EclResource.hpp"
#include "GameSession.hpp"
#include "EnemyManager.hpp"
#include "AnmInterpreter.hpp"
#include "AnmManager.hpp"
#include "PngWriter.hpp"
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#else
#define EMSCRIPTEN_KEEPALIVE
#endif
#include "SoftwareRaster.hpp"
#include "AnmResource.hpp"
#include "AnmTexture.hpp"
#include "EclInterpreter.hpp"
#include "Rng.hpp"
#include "SaveFile.hpp"
#include "StageScript.hpp"
#include "WaveSelector.hpp"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace th185;

namespace {

bool read_file(const char* path, std::vector<u8>& out) {
    std::FILE* file = std::fopen(path, "rb");
    if (!file) return false;
    std::fseek(file, 0, SEEK_END);
    const long size = std::ftell(file);
    std::fseek(file, 0, SEEK_SET);
    if (size <= 0) { std::fclose(file); return false; }
    out.resize(std::size_t(size));
    const std::size_t read = std::fread(out.data(), 1, out.size(), file);
    std::fclose(file);
    return read == out.size();
}

void emit_archive(const std::vector<u8>& file) {
    Archive archive;
    std::string error;
    if (!archive.parse(file, &error)) {
        std::printf("ARCHIVE ERROR %s\n", error.c_str());
        return;
    }
    std::printf("ARCHIVE header %u %u %u %u\n",
                archive.decompressed_table_size,
                archive.compressed_table_size,
                archive.declared_entry_count,
                archive.table_offset);
    std::printf("ARCHIVE entries %zu\n", archive.entries.size());

    // Entry table digest: first/last name, plus the total stored size.
    u32 stored_total = 0;
    for (std::size_t i = 0; i < archive.entries.size(); ++i) {
        stored_total += archive.stored_size(i);
    }
    std::printf("ARCHIVE stored_total %u\n", stored_total);
    std::printf("ARCHIVE first %s\n", archive.entries.front().name.c_str());
    std::printf("ARCHIVE last %s\n", archive.entries.back().name.c_str());

    // Extract a few entries of different types and report their sizes plus a
    // short digest so the Python side can compare without dumping megabytes.
    const char* probes[] = {"help_01.png", "se_plst00.wav", "thbgm.fmt",
                            "world01t.std", "default.ecl", "wave01.ecl",
                            "pl01.sht", "musiccmt.txt"};
    for (const char* wanted : probes) {
        std::size_t index = archive.entries.size();
        for (std::size_t i = 0; i < archive.entries.size(); ++i) {
            if (archive.entries[i].name == wanted) { index = i; break; }
        }
        if (index == archive.entries.size()) {
            std::printf("ENTRY %s MISSING\n", wanted);
            continue;
        }
        std::vector<u8> data = archive.extract(file, index, &error);
        if (data.empty()) {
            std::printf("ENTRY %s ERROR %s\n", wanted, error.c_str());
            continue;
        }
        // FNV-1a 32 over the whole payload: cheap and order-sensitive.
        u32 hash = 2166136261u;
        for (u8 byte : data) { hash = (hash ^ byte) * 16777619u; }
        std::printf("ENTRY %s %zu %08X\n", wanted, data.size(), hash);
    }
}

void emit_ecl(const std::vector<u8>& file) {
    Archive archive;
    std::string error;
    if (!archive.parse(file, &error)) {
        std::printf("ECL ARCHIVE ERROR %s\n", error.c_str());
        return;
    }

    u32 files = 0, subs = 0, instrs = 0;
    for (std::size_t i = 0; i < archive.entries.size(); ++i) {
        const std::string& name = archive.entries[i].name;
        if (name.size() < 4) continue;
        if (name.compare(name.size() - 4, 4, ".ecl") != 0) continue;

        std::vector<u8> blob = archive.extract(file, i, &error);
        if (blob.empty()) {
            std::printf("ECL ERROR %s %s\n", name.c_str(), error.c_str());
            continue;
        }
        EclResource resource;
        if (!resource.parse(blob, &error)) {
            std::printf("ECL PARSE ERROR %s %s\n", name.c_str(), error.c_str());
            continue;
        }
        ++files;
        subs += u32(resource.subs.size());
        instrs += u32(resource.instruction_count());

        // Per-file digest so a mismatch points at the offending file.
        u32 hash = 2166136261u;
        for (const EclSubroutine& sub : resource.subs) {
            for (char ch : sub.name) hash = (hash ^ u8(ch)) * 16777619u;
            hash = (hash ^ u32(sub.tail)) * 16777619u;
            hash = (hash ^ u32(sub.instructions.size())) * 16777619u;
        }
        std::printf("ECL %s %zu %zu %u %08X\n", name.c_str(),
                    resource.subs.size(), resource.instruction_count(),
                    resource.ecli_names.size(), hash);
    }
    std::printf("ECL TOTAL %u %u %u\n", files, subs, instrs);
}

void emit_std(const std::vector<u8>& file) {
    Archive archive;
    std::string error;
    if (!archive.parse(file, &error)) {
        std::printf("STD ARCHIVE ERROR %s\n", error.c_str());
        return;
    }

    u32 files = 0, waves = 0, groups = 0, entities = 0, commands = 0;
    for (std::size_t i = 0; i < archive.entries.size(); ++i) {
        const std::string& name = archive.entries[i].name;
        if (name.size() < 4) continue;
        if (name.compare(name.size() - 4, 4, ".std") != 0) continue;

        std::vector<u8> blob = archive.extract(file, i, &error);
        if (blob.empty()) {
            std::printf("STD ERROR %s %s\n", name.c_str(), error.c_str());
            continue;
        }
        StageScript script;
        if (!script.parse(blob, &error)) {
            std::printf("STD PARSE ERROR %s %s\n", name.c_str(), error.c_str());
            continue;
        }
        ++files;
        waves += script.waves.size();
        groups += script.groups.size();
        entities += script.entities.size();
        commands += script.commands.size();
        std::printf("STD %s %zu %zu %zu %zu %u\n", name.c_str(),
                    script.waves.size(), script.groups.size(),
                    script.entities.size(), script.commands.size(),
                    script.tail_size());
    }
    std::printf("STD TOTAL %u %u %u %u %u\n", files, waves, groups,
                entities, commands);
}

void emit_session(const std::vector<u8>& file) {
    // Exercise the composition path: open -> load each scene -> seed ->
    // select waves -> report the resolved layout.
    GameSession session;
    std::string error;
    if (!session.open(file, &error)) {
        std::printf("SESSION ERROR open %s\n", error.c_str());
        return;
    }

    // The original builds these weights at runtime; use a deterministic stand-in
    // so the SELECTION ORDER is reproducible and comparable.
    std::array<u32, WAVE_TABLE_SIZE> weights{};
    for (u32 i = 0; i < WAVE_TABLE_SIZE; ++i) weights[i] = 1 + (i % 7);

    session.difficulty = 2;   // Normal
    session.card_id = 0;

    // Per-template inputs. The original reads these from a session object we
    // have not reconstructed (RVA 0x40090), so they are supplied here.
    std::array<u32, WAVE_TABLE_SIZE> value_a{};
    std::array<u32, WAVE_TABLE_SIZE> value_b{};
    for (u32 i = 0; i < WAVE_TABLE_SIZE; ++i) {
        value_a[i] = 100 + (i % 11) * 10;
        value_b[i] = 1000 + (i % 7) * 100;
    }

    for (u32 stage = 0; stage < SESSION_SCENE_COUNT; ++stage) {
        if (!session.load_stage(stage, &error)) {
            std::printf("SESSION %u LOAD_ERROR %s\n", stage, error.c_str());
            continue;
        }
        session.seed(u16(0x1234 + stage));
        session.enemies.reset();
        const u32 count = session.select_waves_from_state(value_a, value_b);

        // Digest the picks so a mismatch points at the stage.
        u32 hash = 2166136261u;
        for (const ResolvedWave& wave : session.waves) {
            hash = (hash ^ u32(wave.template_index)) * 16777619u;
            hash = (hash ^ wave.type) * 16777619u;
            hash = (hash ^ wave.ordinal) * 16777619u;
        }
        // Counters must have advanced exactly once per resolved slot.
        u32 drawn = 0;
        for (u32 i = 0; i < WAVE_TABLE_SIZE; ++i) drawn += session.enemies.counter[i];
        std::printf("SESSION %u %s slots=%u entities=%zu groups=%zu cmds=%zu"
                    " forced=%u scorecap=%u drawn=%u skip=%u hash=%08X\n",
                    stage, scene_files()[stage], count,
                    session.stage.entities.size(), session.stage.groups.size(),
                    session.stage.commands.size(), session.forced_boss(),
                    session.score_cap(), drawn, session.enemies.skip_index, hash);
    }
}

void emit_ecl_run(const std::vector<u8>& file) {
    // Actually EXECUTE the interpreter on real ECL data.
    //
    // Until now the interpreter was only verified through a Python mirror of
    // its logic -- which proves the translation is faithful but not that the
    // C++ runs. This drives it for real: load every .ecl, register every
    // subroutine, run the ones whose opcodes are all implemented, and report
    // the aggregate outcome.
    Archive archive;
    std::string error;
    if (!archive.parse(file, &error)) {
        std::printf("ECLRUN ARCHIVE_ERROR %s\n", error.c_str());
        return;
    }

    u32 files = 0;
    u32 subs = 0;
    u32 clean = 0;
    u32 ran = 0;
    u32 finished = 0;
    u32 returned = 0;
    u32 unsupported = 0;
    u32 faulted = 0;
    u32 total_instructions = 0;
    u32 max_depth = 0;

    for (std::size_t entry = 0; entry < archive.entries.size(); ++entry) {
        const std::string& name = archive.entries[entry].name;
        if (name.size() < 4 || name.compare(name.size() - 4, 4, ".ecl") != 0) continue;

        std::vector<u8> blob = archive.extract(file, entry, &error);
        if (blob.empty()) continue;
        EclResource resource;
        if (!resource.parse(std::move(blob), &error)) continue;
        ++files;

        // Register every subroutine so Call can resolve across the file.
        EclInterpreter interpreter;
        for (const EclSubroutine& sub : resource.subs) {
            // Rebuild the raw instruction bytes for this subroutine.
            std::vector<u8> code;
            u32 code_base = 0;
            if (!sub.instructions.empty()) {
                const u32 begin = sub.instructions.front().offset;
                code_base = begin;
                const u32 end = sub.instructions.back().offset +
                                sub.instructions.back().size;
                if (begin < resource.data.size() && end <= resource.data.size()) {
                    code.assign(resource.data.begin() + begin,
                                resource.data.begin() + end);
                }
            }
            interpreter.context.add_subroutine(sub.name, std::move(code));
            interpreter.context.subroutines[sub.name].code_base = code_base;
        }

        for (const EclSubroutine& sub : resource.subs) {
            ++subs;
            // Only run subroutines whose every opcode is implemented: a run that
            // stops on an unknown opcode tells us nothing about correctness.
            bool all_implemented = !sub.instructions.empty();
            for (const EclInstruction& ins : sub.instructions) {
                if (!EclInterpreter::is_supported(ins.id)) {
                    all_implemented = false;
                    break;
                }
            }
            if (!all_implemented) continue;
            ++clean;

            const auto found = interpreter.context.subroutines.find(sub.name);
            if (found == interpreter.context.subroutines.end()) continue;

            EclInterpreter runner;
            runner.context = interpreter.context;
            // Cap the step budget: a malformed loop must terminate.
            const EclRunResult result = runner.run(found->second.code, 100000,
                                                   found->second.code_base);
            ++ran;
            total_instructions += runner.stats().instructions;
            if (runner.stats().max_stack_depth > max_depth) {
                max_depth = runner.stats().max_stack_depth;
            }
            if (runner.stats().max_stack_depth >= 1000 && faulted < 12) {
                std::printf("ECLRUN OVERFLOW %s / %s op=%u instrs=%u depth=%u\n",
                            name.c_str(), sub.name.c_str(),
                            unsigned(runner.stats().first_unsupported_opcode),
                            runner.stats().instructions,
                            runner.stack().depth());
            }
            if (result == EclRunResult::Unsupported && unsupported < 10) {
                std::printf("ECLRUN UNSUP %s / %s op=%u after=%u\n",
                            name.c_str(), sub.name.c_str(),
                            unsigned(runner.stats().first_unsupported_opcode),
                            runner.stats().instructions);
            }
            if (result == EclRunResult::Fault && faulted < 8) {
                std::printf("ECLRUN FAULT %s / %s instrs=%u depth=%u\n",
                            name.c_str(), sub.name.c_str(),
                            runner.stats().instructions,
                            runner.stack().depth());
            }
            switch (result) {
            case EclRunResult::Finished: ++finished; break;
            case EclRunResult::Returned: ++returned; break;
            case EclRunResult::Unsupported: ++unsupported; break;
            case EclRunResult::Fault: ++faulted; break;
            }
        }
    }

    std::printf("ECLRUN files=%u subs=%u clean=%u ran=%u"
                " finished=%u returned=%u unsupported=%u faulted=%u"
                " instrs=%u maxdepth=%u\n",
                files, subs, clean, ran, finished, returned, unsupported,
                faulted, total_instructions, max_depth);
}

void emit_anm_textures(const std::vector<u8>& file) {
    // Decode every THTX block in the archive to RGBA8 and report aggregates.
    // This is the C++ counterpart of scripts/native/anm_texture_check.py: the
    // Python side proves the FORMAT TABLE matches the data, this proves the
    // C++ decoder produces the same shapes.
    Archive archive;
    std::string error;
    if (!archive.parse(file, &error)) {
        std::printf("ANMTEX ARCHIVE_ERROR %s\n", error.c_str());
        return;
    }

    u32 files = 0;
    u32 blocks = 0;
    u32 decoded = 0;
    u32 rejected = 0;
    u64 total_pixels = 0;
    u32 by_format[8] = {0};

    for (std::size_t entry = 0; entry < archive.entries.size(); ++entry) {
        const std::string& name = archive.entries[entry].name;
        if (name.size() < 4 || name.compare(name.size() - 4, 4, ".anm") != 0) continue;

        std::vector<u8> blob = archive.extract(file, entry, &error);
        if (blob.empty()) {
            std::printf("ANMTEX SKIP %s extract: %s\n", name.c_str(), error.c_str());
            continue;
        }
        AnmResource anm;
        if (!anm.parse(std::move(blob), &error)) {
            std::printf("ANMTEX SKIP %s parse: %s\n", name.c_str(), error.c_str());
            continue;
        }
        ++files;

        for (const AnmTexture& texture : anm.textures) {
            ++blocks;
            if (texture.format < 8) ++by_format[texture.format];
            const DecodedImage image = decode_texture(
                texture.format, texture.width, texture.height,
                anm.data.data() + texture.offset + 16, texture.size);
            if (image.valid()) {
                ++decoded;
                total_pixels += u64(image.width) * image.height;
            } else {
                ++rejected;
            }
        }
    }

    std::printf("ANMTEX files=%u blocks=%u decoded=%u rejected=%u pixels=%llu"
                " fmt1=%u fmt3=%u fmt5=%u fmt7=%u\n",
                files, blocks, decoded, rejected,
                static_cast<unsigned long long>(total_pixels),
                by_format[1], by_format[3], by_format[5], by_format[7]);
}

void emit_anm_scripts(const std::vector<u8>& file) {
    // Run the ANM interpreter over every script in the archive.
    //
    // What this proves: the instruction decoder, the timing model and the
    // implemented opcodes all work on real data. What it does NOT prove: that
    // anything is drawn -- there is no renderer yet.
    Archive archive;
    std::string error;
    if (!archive.parse(file, &error)) {
        std::printf("ANMRUN ARCHIVE_ERROR %s\n", error.c_str());
        return;
    }

    u32 files = 0;
    u32 scripts = 0;
    u32 ran = 0;
    u32 finished = 0;
    u32 stopped = 0;
    u32 unsupported = 0;
    u32 faulted = 0;
    u32 total_instructions = 0;
    u32 total_frames = 0;
    u16 first_unsupported = 0;

    for (std::size_t entry = 0; entry < archive.entries.size(); ++entry) {
        const std::string& name = archive.entries[entry].name;
        if (name.size() < 4 || name.compare(name.size() - 4, 4, ".anm") != 0) continue;

        std::vector<u8> blob = archive.extract(file, entry, &error);
        if (blob.empty()) continue;
        AnmResource resource;
        if (!resource.parse(std::move(blob), &error)) continue;
        ++files;

        for (const AnmScriptEntry& script : resource.script_list) {
            ++scripts;
            // Only run scripts whose every opcode this interpreter implements;
            // otherwise the run stops immediately and teaches nothing.
            bool all_supported = !script.instructions.empty();
            for (const AnmInstruction& ins : script.instructions) {
                // Reuse the interpreter's own table by asking it to bind and
                // tick; simpler: run and treat Unsupported as "not covered".
                (void)ins;
            }
            (void)all_supported;

            AnmInterpreter interpreter;
            if (!interpreter.bind(&resource, &script.bytes)) {
                if (ran < 3) {
                    std::printf("ANMRUN BIND_FAIL %s id=%u off=%u bytes=%zu\n",
                                name.c_str(), script.id, script.offset,
                                script.bytes.size());
                }
                continue;
            }

            Rng rng;
            rng.seed = u16(0x1234);
            rng.calls = 0;

            ++ran;
            AnmRunResult result = AnmRunResult::Running;
            // Bounded frame budget: an animation that never ends must not hang.
            for (u32 frame = 0; frame < 600; ++frame) {
                result = interpreter.tick(rng, 1.0f);
                ++total_frames;
                if (result != AnmRunResult::Running) break;
            }

            total_instructions += interpreter.stats().instructions;
            if (first_unsupported == 0 && interpreter.stats().first_unsupported != 0) {
                first_unsupported = interpreter.stats().first_unsupported;
            }
            switch (result) {
            case AnmRunResult::Finished: ++finished; break;
            case AnmRunResult::Stopped: ++stopped; break;
            case AnmRunResult::Unsupported: ++unsupported; break;
            case AnmRunResult::Fault:
                ++faulted;
                if (faulted <= 10) {
                    std::printf("ANMRUN FAULT %s id=%u instrs=%u err=%s\n",
                                name.c_str(), script.id,
                                interpreter.stats().instructions,
                                interpreter.error().c_str());
                }
                break;
            case AnmRunResult::Running: ++stopped; break;  // ran out of budget
            }
        }
    }

    std::printf("ANMRUN files=%u scripts=%u ran=%u finished=%u stopped=%u"
                " unsupported=%u faulted=%u instrs=%u frames=%u first_unsup=%u\n",
                files, scripts, ran, finished, stopped, unsupported, faulted,
                total_instructions, total_frames, unsigned(first_unsupported));
}

void emit_anm_objects(const std::vector<u8>& file) {
    // Drive real ANM scripts through the object manager: create a root per
    // script, then run a bounded frame loop and report the tree it built.
    //
    // This proves the scheduler works: spawns materialise, depth is bounded,
    // and instances retire. It does NOT prove anything is drawn.
    Archive archive;
    std::string error;
    if (!archive.parse(file, &error)) {
        std::printf("ANMOBJ ARCHIVE_ERROR %s\n", error.c_str());
        return;
    }

    u32 files = 0;
    u32 scripts = 0;
    u32 roots = 0;
    u64 total_ticks = 0;
    u32 total_spawned = 0;
    u32 total_detached = 0;
    u32 total_retired = 0;
    u32 peak_live = 0;
    u32 depth_capped = 0;
    u32 live_capped = 0;
    u32 bad_script = 0;
    u32 faults = 0;
    u32 frames = 0;

    for (std::size_t entry = 0; entry < archive.entries.size(); ++entry) {
        const std::string& name = archive.entries[entry].name;
        if (name.size() < 4 || name.compare(name.size() - 4, 4, ".anm") != 0) continue;

        std::vector<u8> blob = archive.extract(file, entry, &error);
        if (blob.empty()) continue;
        AnmResource resource;
        if (!resource.parse(std::move(blob), &error)) continue;
        ++files;

        // One root per script, but cap it so the harness stays fast.
        const u32 limit = u32(resource.script_list.size() < 64 ?
                              resource.script_list.size() : 64);
        scripts += u32(resource.script_list.size());

        AnmManager manager;
        Rng rng;
        rng.seed = u16(0x1234);
        rng.calls = 0;

        for (u32 s = 0; s < limit; ++s) {
            if (manager.create(&resource, i32(s)) != 0xFFFFFFFFu) ++roots;
        }

        // 120 frames is enough to exercise spawning without a long run.
        for (u32 frame = 0; frame < 120; ++frame) {
            manager.update(rng, 1.0f);
            ++frames;
        }

        total_ticks += manager.stats().ticks;
        total_spawned += manager.stats().spawned;
        total_detached += manager.stats().detached_spawned;
        total_retired += manager.stats().retired;
        depth_capped += manager.stats().depth_capped;
        live_capped += manager.stats().live_capped;
        bad_script += manager.stats().bad_script;
        faults += manager.stats().faults;
        if (manager.stats().peak_live > peak_live) peak_live = manager.stats().peak_live;
    }

    std::printf("ANMOBJ files=%u scripts=%u roots=%u ticks=%llu spawned=%u"
                " detached=%u retired=%u peak=%u depth_cap=%u live_cap=%u"
                " bad=%u faults=%u frames=%u\n",
                files, scripts, roots,
                static_cast<unsigned long long>(total_ticks),
                total_spawned, total_detached, total_retired, peak_live,
                depth_capped, live_capped, bad_script, faults, frames);
}

// Exposed to the runner so a frame can be written to disk from JS, where file
// IO is synchronous and reliable.
static u32 g_frame_width = 0;
static u32 g_frame_height = 0;
static std::vector<u8> g_frame_rgba;



void emit_anm_atlas(const std::vector<u8>& file) {
    // Dump the first decodable texture as a PNG. This isolates the DECODER:
    // if the atlas looks wrong here, the problem is pixel conversion, not
    // geometry or blending.
    Archive archive;
    std::string error;
    if (!archive.parse(file, &error)) return;

    for (std::size_t entry = 0; entry < archive.entries.size(); ++entry) {
        const std::string& name = archive.entries[entry].name;
        if (name.size() < 4 || name.compare(name.size() - 4, 4, ".anm") != 0) continue;

        std::vector<u8> blob = archive.extract(file, entry, &error);
        if (blob.empty()) continue;
        AnmResource resource;
        if (!resource.parse(std::move(blob), &error)) continue;
        if (resource.textures.empty()) continue;

        const AnmTexture& texture = resource.textures[0];
        const DecodedImage image = decode_texture(
            texture.format, texture.width, texture.height,
            resource.data.data() + texture.offset + 16, texture.size);
        if (!image.valid()) continue;

        // Force alpha opaque so the PNG is visible without compositing.
        std::vector<u8> rgba = image.rgba;
        for (std::size_t i = 3; i < rgba.size(); i += 4) rgba[i] = 255;

        std::vector<u8> png;
        if (!encode_png(image.width, image.height, rgba, png)) continue;

        std::printf("ANMATLAS name=%s fmt=%u %ux%u ", name.c_str(),
                    unsigned(texture.format), image.width, image.height);
        static const char* kTable =
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::size_t i = 0;
        while (i + 2 < png.size()) {
            const u32 triple = (u32(png[i]) << 16) | (u32(png[i + 1]) << 8) |
                               u32(png[i + 2]);
            std::putchar(kTable[(triple >> 18) & 0x3F]);
            std::putchar(kTable[(triple >> 12) & 0x3F]);
            std::putchar(kTable[(triple >> 6) & 0x3F]);
            std::putchar(kTable[triple & 0x3F]);
            i += 3;
        }
        if (i < png.size()) {
            u32 triple = u32(png[i]) << 16;
            if (i + 1 < png.size()) triple |= u32(png[i + 1]) << 8;
            std::putchar(kTable[(triple >> 18) & 0x3F]);
            std::putchar(kTable[(triple >> 12) & 0x3F]);
            std::putchar(i + 1 < png.size() ? kTable[(triple >> 6) & 0x3F] : '=');
            std::putchar('=');
        }
        std::putchar('\n');
        return;
    }
}
void emit_anm_sprites(const std::vector<u8>& file) {
    // Draw every sprite of one file through the real rasteriser, using each
    // sprite's own UV rect. This is the narrowest test of "sprite -> screen":
    // the atlas dump proved the DECODER, this proves the SUBRECT lookup.
    Archive archive;
    std::string error;
    if (!archive.parse(file, &error)) return;

    // enemy.anm is the richest atlas (120 sprites) and its content is
    // recognisable, so it makes the best visual sample.
    const char* wanted = "enemy.anm";
    std::size_t entry = archive.entries.size();
    for (std::size_t i = 0; i < archive.entries.size(); ++i) {
        if (archive.entries[i].name == wanted) { entry = i; break; }
    }
    if (entry == archive.entries.size()) return;

    std::vector<u8> blob = archive.extract(file, entry, &error);
    if (blob.empty()) return;
    AnmResource resource;
    if (!resource.parse(std::move(blob), &error)) return;

    std::vector<DecodedImage> textures;
    for (const AnmTexture& texture : resource.textures) {
        textures.push_back(decode_texture(
            texture.format, texture.width, texture.height,
            resource.data.data() + texture.offset + 16, texture.size));
    }
    if (textures.empty()) return;

    constexpr u32 WIDTH = 640;
    constexpr u32 HEIGHT = 480;
    SoftwareRaster raster;
    raster.resize(WIDTH, HEIGHT);
    raster.clear(Rgba8{16, 16, 32, 255});

    // Lay sprites out on a grid, each drawn at its native size.
    const u32 columns = 20;
    const float cell = 32.0f;
    u32 drawn = 0;
    for (u32 s = 0; s < resource.sprites.size() && drawn < 320; ++s) {
        const AnmSprite& sprite = resource.sprites[s];
        if (sprite.texture >= textures.size()) continue;
        if (!(sprite.w > 0.0f) || !(sprite.h > 0.0f)) continue;

        const float column = float(s % columns);
        const float row = float(s / columns);
        const float centre_x = 16.0f + column * cell;
        const float centre_y = 16.0f + row * cell;

        AnmQuad quad;
        const float half_w = sprite.w * 0.5f;
        const float half_h = sprite.h * 0.5f;
        quad.x[0] = centre_x - half_w;
        quad.y[0] = centre_y - half_h;
        quad.x[1] = centre_x + half_w;
        quad.y[1] = centre_y - half_h;
        quad.x[2] = centre_x + half_w;
        quad.y[2] = centre_y + half_h;
        quad.x[3] = centre_x - half_w;
        quad.y[3] = centre_y + half_h;

        quad.u[0] = sprite.u0;
        quad.v[0] = sprite.v0;
        quad.u[1] = sprite.u1;
        quad.v[1] = sprite.v0;
        quad.u[2] = sprite.u1;
        quad.v[2] = sprite.v1;
        quad.u[3] = sprite.u0;
        quad.v[3] = sprite.v1;

        quad.texture = &textures[sprite.texture];
        quad.blend = AnmBlendMode::Alpha;
        quad.nearest = true;
        raster.draw(quad);
        ++drawn;
    }

    std::vector<u8> png;
    if (!encode_png(WIDTH, HEIGHT, raster.pixels(), png)) return;

    std::printf("ANMSPR name=%s sprites=%u drawn=%u quads=%u written=%u ",
                wanted, unsigned(resource.sprites.size()), drawn,
                raster.quads_drawn, raster.pixels_written);
    static const char* kTable =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::size_t i = 0;
    while (i + 2 < png.size()) {
        const u32 triple = (u32(png[i]) << 16) | (u32(png[i + 1]) << 8) |
                           u32(png[i + 2]);
        std::putchar(kTable[(triple >> 18) & 0x3F]);
        std::putchar(kTable[(triple >> 12) & 0x3F]);
        std::putchar(kTable[(triple >> 6) & 0x3F]);
        std::putchar(kTable[triple & 0x3F]);
        i += 3;
    }
    if (i < png.size()) {
        u32 triple = u32(png[i]) << 16;
        if (i + 1 < png.size()) triple |= u32(png[i + 1]) << 8;
        std::putchar(kTable[(triple >> 18) & 0x3F]);
        std::putchar(kTable[(triple >> 12) & 0x3F]);
        std::putchar(i + 1 < png.size() ? kTable[(triple >> 6) & 0x3F] : '=');
        std::putchar('=');
    }
    std::putchar('\n');
}
void emit_anm_render(const std::vector<u8>& file) {
    // Render a real ANM scene into an RGBA8 buffer and report coverage.
    //
    // This is the first point where the project produces PIXELS from shipped
    // data. It proves the geometry transform, tint, blend and texture sampling
    // all agree well enough to fill a framebuffer; it does not claim the output
    // matches the original game's pixels.
    Archive archive;
    std::string error;
    if (!archive.parse(file, &error)) {
        std::printf("ANMDRAW ARCHIVE_ERROR %s\n", error.c_str());
        return;
    }

    constexpr u32 WIDTH = 640;
    constexpr u32 HEIGHT = 480;

    u32 files = 0;
    u32 scenes = 0;
    u32 total_quads = 0;
    u32 total_written = 0;
    u32 total_rejected = 0;
    u64 checksum = 0;
    u32 nonblack = 0;

    for (std::size_t entry = 0; entry < archive.entries.size(); ++entry) {
        const std::string& name = archive.entries[entry].name;
        if (name.size() < 4 || name.compare(name.size() - 4, 4, ".anm") != 0) continue;
        // A handful of files is enough to prove the pipeline; more would just
        // slow the harness down.
        if (files >= 6) break;

        std::vector<u8> blob = archive.extract(file, entry, &error);
        if (blob.empty()) continue;
        AnmResource resource;
        if (!resource.parse(std::move(blob), &error)) continue;
        ++files;

        // Decode every texture in this file so sprites can be sampled.
        std::vector<DecodedImage> textures;
        textures.reserve(resource.textures.size());
        for (const AnmTexture& texture : resource.textures) {
            textures.push_back(decode_texture(
                texture.format, texture.width, texture.height,
                resource.data.data() + texture.offset + 16, texture.size));
        }

        AnmManager manager;
        Rng rng;
        rng.seed = u16(0x1234);
        rng.calls = 0;

        // Lay the roots out on a grid so the exported frame shows many
        // sprites instead of all of them stacked at the origin.
        const u32 limit = u32(resource.script_list.size() < 32 ?
                              resource.script_list.size() : 32);
        const u32 columns = 8;
        for (u32 s = 0; s < limit; ++s) {
            const float column = float(s % columns);
            const float row = float(s / columns);
            const float px = (column - float(columns - 1) * 0.5f) * 64.0f;
            const float py = (row - 1.5f) * 64.0f;
            manager.create(&resource, i32(s), 0, 0xFFFFFFFFu, px, py);
        }

        // Advance a few frames so the scripts set positions and colours.
        for (u32 frame = 0; frame < 8; ++frame) {
            manager.update(rng, 1.0f);
        }

        SoftwareRaster raster;
        raster.resize(WIDTH, HEIGHT);
        // Opaque dark background so the exported PNG is inspectable: a
        // transparent clear composites to white in every viewer and hides the
        // geometry completely.
        raster.clear(Rgba8{24, 24, 48, 255});
        manager.render(raster, textures.empty() ? nullptr : textures.data(),
                       u32(textures.size()), float(WIDTH) * 0.5f,
                       float(HEIGHT) * 0.5f);
        ++scenes;

        total_quads += raster.quads_drawn;
        total_written += raster.pixels_written;
        total_rejected += raster.quads_rejected;

        const std::vector<u8>& pixels = raster.pixels();
        // Keep the first scene's framebuffer so the runner can export it.
        // Writing a PNG from inside the WASM sandbox does not reach the host
        // reliably (NODEFS sync is asynchronous and the process exits first),
        // so the bytes are handed out through memory instead.
        if (scenes == 1) {
            g_frame_width = WIDTH;
            g_frame_height = HEIGHT;
            g_frame_rgba.assign(pixels.begin(), pixels.end());
        }
        for (std::size_t i = 0; i < pixels.size(); i += 4) {
            if (pixels[i] != 24 || pixels[i + 1] != 24 || pixels[i + 2] != 48) {
                ++nonblack;
            }
        }
        // FNV-1a over the framebuffer, so two runs can be compared cheaply.
        for (u8 byte : pixels) {
            checksum ^= byte;
            checksum *= 1099511628211ull;
        }
    }

    std::printf("ANMDRAW files=%u scenes=%u quads=%u written=%u rejected=%u"
                " nonblack=%u checksum=%016llx\n",
                files, scenes, total_quads, total_written, total_rejected,
                nonblack, static_cast<unsigned long long>(checksum));

    // Emit the first scene as a base64 PNG on stdout.
    //
    // A file written through NODEFS does not reliably reach the host (the sync
    // is asynchronous and the process exits first), and a pointer handed out
    // after callMain points into a reclaimed stack. Printing the bytes is the
    // only channel that survives all three constraints.
    if (!g_frame_rgba.empty()) {
        std::vector<u8> png;
        if (encode_png(WIDTH, HEIGHT, g_frame_rgba, png)) {
            std::printf("ANMDRAW PNG_BASE64 ");
            static const char* kTable =
                "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
            std::size_t i = 0;
            while (i + 2 < png.size()) {
                const u32 triple = (u32(png[i]) << 16) |
                                    (u32(png[i + 1]) << 8) | u32(png[i + 2]);
                std::putchar(kTable[(triple >> 18) & 0x3F]);
                std::putchar(kTable[(triple >> 12) & 0x3F]);
                std::putchar(kTable[(triple >> 6) & 0x3F]);
                std::putchar(kTable[triple & 0x3F]);
                i += 3;
            }
            if (i < png.size()) {
                u32 triple = u32(png[i]) << 16;
                if (i + 1 < png.size()) triple |= u32(png[i + 1]) << 8;
                std::putchar(kTable[(triple >> 18) & 0x3F]);
                std::putchar(kTable[(triple >> 12) & 0x3F]);
                std::putchar(i + 1 < png.size() ? kTable[(triple >> 6) & 0x3F] : '=');
                std::putchar('=');
            }
            std::putchar('\n');
        }
    }
}

void emit_save(const char* path) {
    // The save file lives outside the archive, so it is passed separately.
    std::vector<u8> blob;
    if (!read_file(path, blob)) {
        std::printf("SAVE READ_ERROR %s\n", path);
        return;
    }
    SaveFile save;
    std::string error;
    if (!save.parse(std::move(blob), &error)) {
        std::printf("SAVE PARSE_ERROR %s\n", error.c_str());
        return;
    }
    std::printf("SAVE header %u %u %u %u %u %u\n",
                save.header.magic, save.header.file_size,
                unsigned(save.header.version), save.header.constant,
                save.header.compressed_size, save.header.decompressed_size);
    std::printf("SAVE records %zu consumed %u total %zu cr %u st %u\n",
                save.records.size(), save.consumed, save.raw.size(),
                save.cr_count(), save.st_count());
    for (const SaveRecord& record : save.records) {
        u32 hash = 2166136261u;
        for (const SaveSlot& slot : record.slots) {
            hash = (hash ^ slot.score) * 16777619u;
            hash = (hash ^ slot.flag_a) * 16777619u;
            hash = (hash ^ slot.flag_b) * 16777619u;
            for (char ch : slot.name) hash = (hash ^ u8(ch)) * 16777619u;
        }
        std::printf("SAVE rec %04X %u %u %u %zu %08X\n",
                    unsigned(record.magic), unsigned(record.version),
                    record.value, record.length, record.slots.size(), hash);
    }
}

void emit_tables() {
    // Card table digest.
    u32 card_hash = 2166136261u;
    for (const CardEntry& card : cards()) {
        for (char ch : std::string(card.name)) card_hash = (card_hash ^ u8(ch)) * 16777619u;
        card_hash = (card_hash ^ card.id) * 16777619u;
        card_hash = (card_hash ^ card.icon_left) * 16777619u;
        card_hash = (card_hash ^ card.icon_right) * 16777619u;
    }
    std::printf("CARDS %zu %08X\n", cards().size(), card_hash);

    // Wave name table digest.
    u32 wave_hash = 2166136261u;
    for (const std::string& name : wave_names()) {
        for (char ch : name) wave_hash = (wave_hash ^ u8(ch)) * 16777619u;
    }
    std::printf("WAVENAMES %zu %08X\n", wave_names().size(), wave_hash);

    // Difficulty.
    std::printf("DIFFICULTY");
    for (const char* name : difficulty_names()) std::printf(" %s", name);
    std::printf("\n");
    std::printf("SCORECAPS");
    for (u32 cap : score_caps()) std::printf(" %u", cap);
    std::printf("\n");

    // Wave selection determinism: fixed weights, fixed seed.
    std::array<u32, WAVE_TABLE_SIZE> weights{};
    for (u32 i = 0; i < WAVE_TABLE_SIZE; ++i) weights[i] = 1 + (i % 7);
    Rng rng;
    rng.seed = 0x1234;
    rng.calls = 0;
    std::printf("WAVEPICK");
    for (int i = 0; i < 12; ++i) {
        std::printf(" %d", select_wave(weights, rng, 0xFFFFFFFFu));
    }
    std::printf("\n");
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s <th185.dat>\n", argv[0]);
        return 2;
    }

    std::vector<u8> file;
    if (!read_file(argv[1], file)) {
        std::fprintf(stderr, "cannot read %s\n", argv[1]);
        return 2;
    }

    emit_archive(file);
    emit_ecl(file);
    emit_ecl_run(file);
    emit_anm_textures(file);
    emit_anm_scripts(file);
    emit_anm_objects(file);
    emit_anm_render(file);
    emit_anm_atlas(file);
    emit_anm_sprites(file);
    emit_std(file);
    emit_session(file);
    emit_tables();
    // Optional third argument: the save file (lives outside the archive).
    if (argc >= 3) emit_save(argv[2]);
    return 0;
}
