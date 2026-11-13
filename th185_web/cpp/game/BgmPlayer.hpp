#pragma once
// TH185 background music: thbgm.dat plus the index in th185.dat's thbgm.fmt.
//
// ## The two files
//
//   thbgm.dat   255,321,904 bytes, a bare ZWAV header followed by PCM
//   thbgm.fmt   537 bytes inside th185.dat: 16 bytes of header, then 10
//               entries of 52 bytes, then 17 trailing bytes
//
// The ZWAV header is 16 bytes: magic "ZWAV", version 1, then 0x1850. There is
// no per-track header in the data file at all -- every offset and length comes
// from thbgm.fmt. Verified: the last entry's start+length equals the file size
// exactly, so the index covers the whole stream.
//
// ## Entry layout (52 bytes, measured on all 10 entries)
//
//   +0x00  char  name[16]        e.g. "th185_01.wav"
//   +0x10  int32 start_offset    byte offset into thbgm.dat
//   +0x14  int32 length_bytes
//   +0x18  int32 loop_start      byte offset of the loop point
//   +0x1C  int32 length_bytes_2  duplicate of +0x14
//   +0x20  int32 format_flags    0x20001 on every track
//   +0x24  int32 sample_rate     44100
//   +0x28  int32 byte_rate       176400 = 44100 * 4
//   +0x2C  int32 flags2          0x100004
//   +0x30  int32 reserved        0
//
// Every track is 44100 Hz stereo 16-bit (byte_rate / sample_rate == 4).

#include "Types.hpp"

#include <string>
#include <vector>

namespace th185 {

constexpr u32 BGM_ENTRY_SIZE = 52;
constexpr u32 BGM_HEADER_SIZE = 16;
constexpr u32 BGM_ZWAV_HEADER = 16;

struct BgmTrack {
    std::string name;
    u32 start_offset = 0;
    u32 length_bytes = 0;
    u32 loop_start = 0;
    u32 sample_rate = 44100;
    u32 channels = 2;
    u32 bits = 16;
};

struct BgmStats {
    u32 tracks = 0;
    u32 playing = 0;
    u32 mixed_frames = 0;
};

class BgmPlayer {
public:
    // Parses thbgm.fmt. Returns false when the header does not match.
    bool parse_index(std::vector<u8> fmt_blob);

    // Points the player at the PCM stream. The buffer is NOT copied: thbgm.dat
    // is 255 MB and duplicating it would be wasteful, so the caller keeps it
    // alive for the player's lifetime.
    void attach_stream(const u8* data, std::size_t size);

    // Starts track `index`. Returns false when the index is out of range or no
    // stream is attached.
    bool play(u32 index);
    void stop();
    bool is_playing() const { return playing_; }
    u32 current_track() const { return playing_ ? track_ : 0xFFFFFFFFu; }

    // ADDS `frames` stereo float samples into `out`. It does not clear the
    // buffer: the sound bank and the BGM both mix into the same frame, and a
    // clearing mixer would erase whatever ran before it.
    void mix(float* out, u32 frames);

    const std::vector<BgmTrack>& tracks() const { return tracks_; }
    const BgmStats& stats() const { return stats_; }

private:
    std::vector<BgmTrack> tracks_;
    const u8* stream_ = nullptr;
    std::size_t stream_size_ = 0;

    bool playing_ = false;
    u32 track_ = 0;
    u32 position_ = 0;      // byte offset within the track

    BgmStats stats_{};
};

}  // namespace th185
