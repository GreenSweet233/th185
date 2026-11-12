#include "Interpolation.hpp"

#include <cmath>

namespace th185 {

namespace {

// Preserves th15's truncate_int semantics: out-of-range or NaN yields INT32_MIN.
i32 truncate_int(float value) noexcept {
    if (!std::isfinite(value) || value >= 2147483648.0f || value < -2147483648.0f) {
        return -2147483647 - 1;
    }
    return static_cast<i32>(value);
}

}  // namespace

float normalize_angle(float value) {
    constexpr double PI = 3.1415927410125732421875;
    constexpr double TAU = 6.283185482025146484375;
    u32 loops = 0;
    while (value > float(PI)) {
        value = float(double(value) - TAU);
        if (++loops > 32) break;
    }
    while (value < float(-PI)) {
        value = float(double(value) + TAU);
        if (++loops > 32) break;
    }
    return value;
}

void Timer::set(i32 frame) {
    if (!(flags & 1u)) {
        current = 0;
        previous = -999999;
        fractional = 0;
        rate_index = 0;
        flags |= 1u;
    }
    current = frame;
    previous = wrapping_add(frame, -1);
    fractional = float(frame);
}

void Timer::tick(const float* rate) {
    rate_index = 0;
    previous = current;
    const float speed = rate ? *rate : 1.0f;
    if (speed > 0.99f && speed < 1.01f) {
        current = wrapping_add(current, 1);
        fractional = float(fractional + 1.0f);
    } else {
        fractional = float(fractional + speed);
        current = truncate_int(fractional);
    }
}

void Timer::decrement(const float* rate) {
    rate_index = 0;
    previous = current;
    const float speed = rate ? *rate : 1.0f;
    const float amount = (speed > 0.99f && speed < 1.01f) ? 1.0f : speed;
    fractional = float(fractional - amount);
    current = truncate_int(fractional);
}

float interpolation_weight(i32 mode, float elapsed, float duration) {
    if (duration == 0 || std::isnan(duration)) return 1.0f;
    float t = float(elapsed / duration);

    auto power = [](float v, u32 exponent) {
        float r = v;
        for (u32 i = 1; i < exponent; ++i) r = float(r * v);
        return r;
    };

    if (mode >= 1 && mode <= 3) return power(t, u32(mode + 1));
    if (mode >= 4 && mode <= 6) return float(1.0f - power(float(1.0f - t), u32(mode - 2)));

    if (mode >= 9 && mode <= 14) {
        const u32 exponent = u32((mode - 9) % 3 + 2);
        t = float(t * 2.0f);
        if (mode <= 11) {
            return t < 1.0f ? float(power(t, exponent) * 0.5f)
                            : float(float(2.0f - power(float(2.0f - t), exponent)) * 0.5f);
        }
        return t < 1.0f ? float(0.5f - float(power(float(1.0f - t), exponent) * 0.5f))
                        : float(float(power(float(t - 1.0f), exponent) * 0.5f) + 0.5f);
    }

    if (mode == 15) return 0.0f;
    if (mode == 16) return 1.0f;

    auto sine = [](float angle) { return float(std::sin(double(angle))); };

    if (mode == 18) return sine(float(float(t * 3.1415927410125732421875f) * 0.5f));
    if (mode == 19) {
        return float(1.0f - sine(float(float(float(t * 3.1415927410125732421875f) * 0.5f) +
                                      1.57079637050628662109375f)));
    }
    if (mode == 20) {
        t = float(t * 2.0f);
        const float s = sine(float(float(t * 3.1415927410125732421875f) * 0.5f));
        return t < 1.0f ? float(s * 0.5f) : float(float(float(1.0f - s) * 0.5f) + 0.5f);
    }
    if (mode == 21) {
        t = float(t * 2.0f);
        if (t < 1.0f) {
            return float(float(1.0f - sine(float(float(float(t * 3.1415927410125732421875f) * 0.5f) +
                                                  1.57079637050628662109375f))) * 0.5f);
        }
        return float(0.5f + float(sine(float(float(float(t - 1.0f) * 3.1415927410125732421875f) * 0.5f)) * 0.5f));
    }

    if (mode >= 22 && mode <= 31) {
        constexpr float shift[] = {
            0.25f, 0.300000011920928955078125f, 0.3499999940395355224609375f,
            0.37999999523162841796875f, 0.4000000059604644775390625f};
        constexpr float divisor[] = {
            0.5625f, 0.4899999797344207763671875f, 0.422499954700469970703125f,
            0.38440001010894775390625f, 0.36000001430511474609375f};
        constexpr float subtract[] = {
            0.111111111938953399658203125f, 0.18367348611354827880859375f,
            0.28994083404541015625f, 0.375650346279144287109375f,
            0.4444444477558135986328125f};
        constexpr float scale[] = {
            0.888888895511627197265625f, 0.81632649898529052734375f,
            0.71005916595458984375f, 0.624349653720855712890625f,
            0.5555555820465087890625f};
        const u32 i = u32((mode - 22) % 5);
        if (mode >= 27) t = float(1.0f - t);
        t = float(t - shift[i]);
        t = float(t * t);
        t = float(t / divisor[i]);
        t = float(t - subtract[i]);
        t = float(t / scale[i]);
        return mode >= 27 ? float(1.0f - t) : t;
    }

    return t;
}

// Explicit instantiations of the float template used by the interpreter.
template struct FloatInterpolation<1, false>;
template struct FloatInterpolation<2, false>;
template struct FloatInterpolation<3, false>;
template struct FloatInterpolation<1, true>;

}  // namespace th185

namespace th185 {

template <u32 N, bool Angle>
std::array<float, N> FloatInterpolation<N, Angle>::evaluate() {
    if (duration == 0) return (mode == 7 || mode == 17) ? start : end;

    if (mode == 7 || mode == 17) {
        for (u32 i = 0; i < N; ++i) {
            start[i] = float(start[i] + (mode == 7 ? end[i] : control2[i]));
            if (Angle) start[i] = normalize_angle(start[i]);
            current[i] = start[i];
            if (mode == 17) {
                control2[i] = float((Angle ? end[i] : control2[i]) +
                                    (Angle ? control2[i] : end[i]));
                if (Angle) control2[i] = normalize_angle(control2[i]);
            }
        }
        return current;
    }

    if (mode == 8) {
        const float t = float(timer.fractional / float(duration));
        const float twice = float(t * 2.0f);
        const float a = float(t - 1.0f);
        const float b = float(1.0f - t);
        const float h00 = float(float(a * a) * float(twice + 1.0f));
        const float h11 = float(float(a * t) * t);
        const float h01 = float(float(3.0f - twice) * float(t * t));
        const float h10 = float(float(b * b) * t);
        for (u32 i = 0; i < N; ++i) {
            const float p = float(start[i] * h00);
            const float q = float(end[i] * h01);
            const float r = float(control1[i] * h10);
            const float s = float(control2[i] * h11);
            if (Angle) {
                current[i] = normalize_angle(
                    float(normalize_angle(
                              float(normalize_angle(float(normalize_angle(p) + normalize_angle(q))) +
                                    normalize_angle(r))) +
                          normalize_angle(s)));
            } else if (N == 3) {
                current[i] = i ? float(s + float(r + float(p + q)))
                               : float(float(r + float(p + q)) + s);
            } else if (N == 1) {
                current[i] = float(float(float(q + p) + r) + s);
            } else {
                current[i] = float(float(float(p + q) + r) + s);
            }
        }
        return current;
    }

    const float weight = interpolation_weight(mode, timer.fractional, float(duration));
    for (u32 i = 0; i < N; ++i) {
        float difference = float(end[i] - start[i]);
        if (Angle) {
            if (difference > 3.1415927410125732421875f) {
                difference = float(end[i] - float(start[i] + 6.283185482025146484375f));
            } else if (float(start[i] - end[i]) > 3.1415927410125732421875f) {
                difference = float(end[i] - float(start[i] - 6.283185482025146484375f));
            }
            difference = normalize_angle(difference);
            current[i] = normalize_angle(
                float(start[i] + normalize_angle(float(difference * weight))));
        } else {
            current[i] = float(start[i] + float(difference * weight));
        }
    }
    return current;
}

}  // namespace th185