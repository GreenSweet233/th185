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
    emit_std(file);
    emit_session(file);
    emit_tables();
    // Optional third argument: the save file (lives outside the archive).
    if (argc >= 3) emit_save(argv[2]);
    return 0;
}
