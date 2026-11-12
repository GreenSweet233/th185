#pragma once
// TH185 ANM script interpreter.
//
// ## Provenance
//
// The opcode semantics come from th15's `web/cpp/game/AnmInterpreter.cpp`, which
// is the same engine family (TH13+ ANM, header version 8). thtk gives the
// instruction FORMAT (see reference/anm-opcodes.json); th15 gives the BEHAVIOUR.
// Neither is guessed here.
//
// ## Instruction layout
//
//   +0  i16 opcode
//   +2  u16 length      // total bytes, multiple of 4, >= 8
//   +4  i16 time        // frame at which this instruction takes effect
//   +6  u16 mask        // bit i set => argument i is a VARIABLE REFERENCE
//   +8  arguments, 4 bytes each (i32 / float bit pattern)
//
// ## Scope, stated honestly
//
// Implemented: timing, control flow (0/1/2/3/4/5/6/7/200/201), sprite selection
// (300/301), the flag setters that need no interpolation (302..317, 421..424,
// 431..438), position/rotation/scale/colour assignment (400..406, 422, 429,
// 434, 436), variable math (122..131), and 509 (copy variables from parent).
//
// NOT implemented: the interpolation family (407..420, 427..430, 433, 435),
// object spawning (500..508, needs an object host), and the blend-mode family
// (600..611). Those need renderer state this project does not have yet, so they
// are reported as unsupported rather than silently skipped.

#include "AnmResource.hpp"
#include "Interpolation.hpp"
#include "Rng.hpp"
#include "Types.hpp"

#include <array>
#include <string>
#include <vector>

namespace th185 {

constexpr u32 ANM_INTEGER_COUNT = 8;
constexpr u32 ANM_FLOAT_COUNT = 8;

struct AnmVariables {
    std::array<i32, ANM_INTEGER_COUNT> integers{};
    std::array<float, ANM_FLOAT_COUNT> floats{};
    Vec3 position{};
    Vec3 rotation{};
    float random_angle = 0.0f;
    float random_scale = 1.0f;

    // Resolves argument n when its mask bit is set.
    i32 integer(i32 value, Rng& rng);
    float floating(float value, Rng& rng);
};

// Visual state the interpreter writes. The renderer consumes this; nothing
// here touches a GPU.
struct AnmVisual {
    u32 flags = 0;
    u32 render_flags = 0;
    Vec2 scale{1.0f, 1.0f};
    Vec2 size{};
    Vec2 uv_scale{1.0f, 1.0f};
    u32 color = 0xFFFFFFFFu;
    u32 secondary_color = 0xFFFFFFFFu;
    i32 layer = 0;
    i32 sprite = -1;
    bool visible() const { return (flags & 1u) != 0; }
};

// The interpolator set, mirroring th15's AnmInterpolators.
struct AnmInterpolators {
    Vec3Interpolation position;
    ScalarInterpolation color_r, color_g, color_b;
    ScalarInterpolation alpha;
    Vec3Interpolation rotation;
    AngleInterpolation angle;
    Vec2Interpolation scale, secondary_scale, uv_scale;
    ScalarInterpolation secondary_alpha;

    // Applies every active interpolator to `variables` / `visual`.
    void advance(AnmVariables& variables, AnmVisual& visual, float rate);
};
enum class AnmRunResult : u8 {
    Running,       // still executing, call again next frame
    Finished,      // hit opcode 1 (delete)
    Stopped,       // hit opcode 2 (stop)
    Unsupported,   // hit an opcode this interpreter does not implement
    Fault,         // malformed instruction
};

struct AnmRunStats {
    u32 instructions = 0;
    u32 unsupported = 0;
    u32 faults = 0;
    u16 first_unsupported = 0;
    // Sprite references that point outside this file (indices are global).
    u32 external_sprites = 0;
};

class AnmInterpreter {
public:
    // Bind to a script. `resource` supplies sprites for opcode 300/301.
    // `code_base` is the file offset that script->at(0) corresponds to: jump
    // operands are ABSOLUTE file offsets, so a sliced buffer needs it to
    // resolve targets. (Same trap as the ECL interpreter's code_base.)
    bool bind(const AnmResource* resource, const std::vector<u8>* script,
              u32 code_base = 0);

    // Advances one frame at `rate` (1.0 = normal speed).
    AnmRunResult tick(Rng& rng, float rate = 1.0f);

    const AnmVariables& variables() const { return variables_; }
    const AnmVisual& visual() const { return visual_; }
    const AnmRunStats& stats() const { return stats_; }
    const std::string& error() const { return error_; }
    float timer() const { return timer_; }

private:
    bool jump(u32 offset, i32 time);

    const AnmResource* resource_ = nullptr;
    const std::vector<u8>* script_ = nullptr;
    i32 instruction_offset_ = 0;
    u32 code_base_ = 0;
    float timer_ = 0.0f;
    float saved_timer_ = 0.0f;
    i32 saved_offset_ = 0;
    AnmVariables variables_{};
    AnmVisual visual_{};
    AnmInterpolators interpolators_{};
    AnmRunStats stats_{};
    std::string error_;
};


}  // namespace th185
