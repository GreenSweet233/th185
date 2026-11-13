#include "SoundBank.hpp"

namespace th185 {

namespace {

u32 read_u16(const u8* p) { return u32(p[0]) | (u32(p[1]) << 8); }
u32 read_u32(const u8* p) {
    return u32(p[0]) | (u32(p[1]) << 8) | (u32(p[2]) << 16) | (u32(p[3]) << 24);
}

bool match4(const u8* p, const char* tag) {
    return p[0] == u8(tag[0]) && p[1] == u8(tag[1]) && p[2] == u8(tag[2]) &&
           p[3] == u8(tag[3]);
}

}  // namespace

bool SoundBank::load(const std::string& name, std::vector<u8> blob) {
    // A RIFF/WAVE container: "RIFF" <size> "WAVE" then a chunk list. The
    // shipped files always put "fmt " first and "data" after it, but the
    // walk below accepts any order and skips unknown chunks.
    if (blob.size() < 12 || !match4(blob.data(), "RIFF") ||
        !match4(blob.data() + 8, "WAVE")) {
        ++stats_.failed_decode;
        return false;
    }

    SoundSample sample;
    sample.name = name;

    u32 channels = 1;
    u32 rate = 22050;
    u32 bits = 8;
    const u8* pcm = nullptr;
    u32 pcm_size = 0;

    std::size_t at = 12;
    while (at + 8 <= blob.size()) {
        const u8* chunk = blob.data() + at;
        const u32 chunk_size = read_u32(chunk + 4);
        if (at + 8 + chunk_size > blob.size()) break;

        if (match4(chunk, "fmt ") && chunk_size >= 16) {
            const u8* f = chunk + 8;
            // format tag at +0, channels at +2, rate at +4, bits at +14
            channels = read_u16(f + 2);
            rate = read_u32(f + 4);
            bits = read_u16(f + 14);
        } else if (match4(chunk, "data")) {
            pcm = chunk + 8;
            pcm_size = chunk_size;
        }
        at += 8 + chunk_size + (chunk_size & 1u);
    }

    if (!pcm || pcm_size == 0 || channels == 0 || (bits != 8 && bits != 16)) {
        ++stats_.failed_decode;
        return false;
    }

    sample.channels = channels;
    sample.sample_rate = rate;

    const u32 bytes_per_sample = bits / 8u;
    const u32 total = pcm_size / bytes_per_sample;
    sample.samples.reserve(total);
    for (u32 i = 0; i < total; ++i) {
        if (bits == 8) {
            // 8-bit WAV is UNSIGNED, centred on 128.
            sample.samples.push_back((float(pcm[i]) - 128.0f) / 128.0f);
        } else {
            const i16 v = i16(i16(pcm[i * 2]) | (i16(pcm[i * 2 + 1]) << 8));
            sample.samples.push_back(float(v) / 32768.0f);
        }
    }
    sample.frame_count = total / channels;

    samples_.push_back(std::move(sample));
    last_play_frame_.push_back(0);
    ever_played_.push_back(false);
    ++stats_.loaded;
    return true;
}

const SoundSample* SoundBank::find(const std::string& name) const {
    for (const SoundSample& s : samples_) {
        if (s.name == name) return &s;
    }
    return nullptr;
}

i32 SoundBank::play(const std::string& name, float volume, u32 min_gap_frames) {
    i32 index = -1;
    for (std::size_t i = 0; i < samples_.size(); ++i) {
        if (samples_[i].name == name) { index = i32(i); break; }
    }
    if (index < 0) return -1;

    // Throttle: a sound already started inside the window is skipped rather
    // than queued. This is what stops a dense graze pattern from turning into
    // a constant buzz.
    const std::size_t si = std::size_t(index);
    if (min_gap_frames > 0 && ever_played_[si] &&
        frame_ - last_play_frame_[si] < min_gap_frames) {
        ++stats_.throttled;
        return -1;
    }
    last_play_frame_[si] = frame_;
    ever_played_[si] = true;

    for (u32 v = 0; v < SOUND_MAX_VOICES; ++v) {
        if (voices_[v].alive) continue;
        voices_[v] = Voice{};
        voices_[v].sample = index;
        voices_[v].volume = volume;
        voices_[v].alive = true;
        ++stats_.played;
        return i32(v);
    }

    // No free voice: drop rather than steal. Stealing would cut a longer
    // sound (a death cry) for a shorter one (a shot), which sounds worse than
    // a missed shot.
    ++stats_.dropped;
    return -1;
}

void SoundBank::mix(float* out, u32 frames) {
    if (!out || frames == 0) return;
    for (u32 i = 0; i < frames * 2; ++i) out[i] = 0.0f;

    u32 active = 0;
    for (u32 v = 0; v < SOUND_MAX_VOICES; ++v) {
        Voice& voice = voices_[v];
        if (!voice.alive) continue;
        ++active;

        const SoundSample& sample = samples_[std::size_t(voice.sample)];
        for (u32 f = 0; f < frames; ++f) {
            if (voice.position >= sample.frame_count) {
                if (voice.looping) {
                    voice.position = 0;
                } else {
                    voice.alive = false;
                    break;
                }
            }
            const u32 base = voice.position * sample.channels;
            const float left = sample.samples[base] * voice.volume;
            const float right = (sample.channels > 1)
                                    ? sample.samples[base + 1] * voice.volume
                                    : left;
            out[f * 2] += left;
            out[f * 2 + 1] += right;
            ++voice.position;
        }
    }

    // Hard clamp: summing several voices can exceed 1.0, and the consumer
    // expects a normalised buffer.
    for (u32 i = 0; i < frames * 2; ++i) {
        if (out[i] > 1.0f) out[i] = 1.0f;
        else if (out[i] < -1.0f) out[i] = -1.0f;
    }

    stats_.active_voices = active;
    stats_.mixed_frames += frames;
}

void SoundBank::stop_all() {
    for (u32 v = 0; v < SOUND_MAX_VOICES; ++v) voices_[v] = Voice{};
}

}  // namespace th185
