#pragma once
// TH185 animation interpolation.
//
// ## Provenance
//
// Ported from th15's `web/cpp/game/Interpolation.{hpp,cpp}` and `Timer.hpp`.
// The same engine family (TH13+ ANM v8), so the easing table and the
// arithmetic order are taken verbatim rather than re-derived. The original
// comments emphasise that arithmetic ORDER matters because interpolation
// feeds collision and replay state, not only pixels.
//
// ## Why this matters now
//
// Before this file the interpreter stopped on every interpolating opcode
// (407..420, 427..430, 433, 435) and 1967 of 2121 scripts hit one of them.
// Interpolation needs no renderer and no object host, so it is the cheapest
// large win available.

#include "Types.hpp"

#include <array>

namespace th185 {

// Wraps an angle into (-pi, pi].
float normalize_angle(float value);

// TH15 uses SSE single precision and a rate-table index rather than a pointer.
struct Timer {
    i32 previous = -999999;
    i32 current = 0;
    float fractional = 0;
    u32 rate_index = 0;
    u32 flags = 0;

    void set(i32 frame);
    void tick(const float* rate);
    void decrement(const float* rate);
};

// Easing curve. `mode` selects one of the original 32 curves.
float interpolation_weight(i32 mode, float elapsed, float duration);

// Template mirror of th15's FloatInterpolation. N is the component count;
// Angle=true applies angle wrapping at each step.
template <u32 N, bool Angle = false>
struct FloatInterpolation {
    std::array<float, N> start{};
    std::array<float, N> end{};
    std::array<float, N> control1{};
    std::array<float, N> control2{};
    std::array<float, N> current{};
    Timer timer{};
    i32 duration = 0;
    i32 mode = 0;

    void begin(i32 frames, i32 easing, const std::array<float, N>& from,
               const std::array<float, N>& to) {
        start = from;
        end = to;
        duration = frames;
        mode = easing;
        timer.set(0);
    }

    std::array<float, N> step(float rate = 1.0f) {
        if (duration > 0) {
            timer.tick(&rate);
            if (timer.current >= duration) {
                timer.set(duration);
                duration = 0;
            }
        }
        return evaluate();
    }

    std::array<float, N> evaluate();
};

using ScalarInterpolation = FloatInterpolation<1>;
using Vec2Interpolation = FloatInterpolation<2>;
using Vec3Interpolation = FloatInterpolation<3>;
using AngleInterpolation = FloatInterpolation<1, true>;

// Integer variant: same easing table, wrapping integer arithmetic.
template <u32 N>
struct IntegerInterpolation {
    std::array<i32, N> start{};
    std::array<i32, N> end{};
    std::array<i32, N> control1{};
    std::array<i32, N> control2{};
    std::array<i32, N> current{};
    Timer timer{};
    i32 duration = 0;
    i32 mode = 0;

    void begin(i32 frames, i32 easing, const std::array<i32, N>& from,
               const std::array<i32, N>& to) {
        start = from;
        end = to;
        duration = frames;
        mode = easing;
        timer.set(0);
    }

    std::array<i32, N> step(float rate = 1.0f) {
        if (duration > 0) {
            timer.tick(&rate);
            if (timer.current >= duration) {
                timer.set(duration);
                duration = 0;
            }
        }
        if (duration == 0) return (mode == 7 || mode == 17) ? start : end;
        if (mode == 7 || mode == 17) {
            for (u32 i = 0; i < N; ++i) {
                start[i] = wrapping_add(start[i], mode == 7 ? end[i] : control2[i]);
                current[i] = start[i];
                if (mode == 17) control2[i] = wrapping_add(control2[i], end[i]);
            }
            return current;
        }
        if (mode == 8) {
            // Cubic Hermite with the same operation order as the original.
            const float t = float(timer.fractional / float(duration));
            const float twice = float(t * 2.0f);
            const float a = float(t - 1.0f);
            const float b = float(1.0f - t);
            const float h00 = float(float(a * a) * float(twice + 1.0f));
            const float h11 = float(float(a * t) * t);
            const float h01 = float(float(3.0f - twice) * float(t * t));
            const float h10 = float(float(b * b) * t);
            for (u32 i = 0; i < N; ++i) {
                const float p = float(float(start[i]) * h00);
                const float q = float(float(end[i]) * h01);
                const float r = float(float(control1[i]) * h10);
                const float s = float(float(control2[i]) * h11);
                current[i] = i32(float(float(float(p + q) + r) + s));
            }
            return current;
        }
        const float weight =
            interpolation_weight(mode, timer.fractional, float(duration));
        for (u32 i = 0; i < N; ++i) {
            const i32 difference = i32(u32(end[i]) - u32(start[i]));
            current[i] = i32(u32(start[i]) +
                             u32(i32(float(float(difference) * weight))));
        }
        return current;
    }
};

}  // namespace th185
