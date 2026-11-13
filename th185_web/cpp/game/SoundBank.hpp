#pragma once
// TH185 sound effects: the .wav files stored inside th185.dat.
//
// ## Format (measured)
//
// Every se_*.wav is a standard RIFF/WAVE container, unlike the BGM which
// lives in thbgm.dat behind a ZWAV header. Measured samples:
//
//   se_tan00.wav      mono   22050 Hz   8 bit   0.68 s
//   se_graze.wav      mono   22050 Hz  16 bit   0.10 s
//   se_select00.wav   mono   22050 Hz   8 bit   0.05 s
//   se_pldead00.wav   stereo 22050 Hz   8 bit   1.38 s
//
// So the decoder has to handle mono and stereo, 8 and 16 bit. Everything is
// converted to float samples in -1..1 for the mixer.
//
// ## What is not here
//
// There is no audio device in the WASM build yet. This bank decodes and
// mixes; the shell can pull a buffer and play it with WebAudio. Until that
// exists the mixer still runs, so "which sound would have played" is
// observable in tests.

#include "Types.hpp"

#include <string>
#include <vector>

namespace th185 {

// 32 was not enough: a dense graze pattern fired 152 sounds in 400 frames and
// 120 were dropped. 64 covers the observed peak with headroom.
constexpr u32 SOUND_MAX_VOICES = 64;

struct SoundSample {
    std::string name;
    std::vector<float> samples;   // interleaved, -1..1
    u32 channels = 1;
    u32 sample_rate = 22050;
    u32 frame_count = 0;          // samples.size() / channels
};

// One playing instance.
struct Voice {
    i32 sample = -1;      // index into the bank, -1 when free
    u32 position = 0;     // in frames
    float volume = 1.0f;
    bool looping = false;
    bool alive = false;
};

struct SoundStats {
    u32 loaded = 0;
    u32 played = 0;
    u32 dropped = 0;      // no free voice
    u32 failed_decode = 0;
    u32 active_voices = 0;
    u32 mixed_frames = 0;
    u32 throttled = 0;
};

class SoundBank {
public:
    // Parses a RIFF/WAVE blob. Returns false and increments failed_decode when
    // the container is not what the shipped data uses.
    bool load(const std::string& name, std::vector<u8> blob);

    // Starts a sample by name. Returns the voice index, or -1 when the name is
    // unknown, every voice is busy, or the same sound was started less than
    // `min_gap_frames` ago.
    //
    // The gap matters for graze and shot sounds: a dense pattern fires them
    // every few frames and the mix turns into a buzz. Measured: 152 plays in
    // 400 frames produced 33 drops even with 64 voices.
    i32 play(const std::string& name, float volume = 1.0f,
             u32 min_gap_frames = 0);

    // Call once per game frame so the throttle window advances.
    void advance_frame() { ++frame_; }
    u32 frame() const { return frame_; }

    // Clears `out` then sums every live voice into it (stereo float).
    // `out` must hold frames * 2 floats. This is the FIRST mixer in the chain,
    // so it owns the clear; later mixers only add.
    void mix(float* out, u32 frames);

    void stop_all();

    const SoundSample* find(const std::string& name) const;
    const std::vector<SoundSample>& samples() const { return samples_; }
    const SoundStats& stats() const { return stats_; }

private:
    std::vector<SoundSample> samples_;
    Voice voices_[SOUND_MAX_VOICES];
    SoundStats stats_{};
    u32 frame_ = 0;
    // Frame of the last play per sample, for the throttle.
    std::vector<u32> last_play_frame_;
    std::vector<bool> ever_played_;
};

}  // namespace th185
