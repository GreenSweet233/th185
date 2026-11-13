#include "BgmPlayer.hpp"

namespace th185 {

namespace {

u32 read_u32(const u8* p) {
    return u32(p[0]) | (u32(p[1]) << 8) | (u32(p[2]) << 16) | (u32(p[3]) << 24);
}

bool match4(const u8* p, const char* tag) {
    return p[0] == u8(tag[0]) && p[1] == u8(tag[1]) && p[2] == u8(tag[2]) &&
           p[3] == u8(tag[3]);
}

}  // namespace

bool BgmPlayer::parse_index(std::vector<u8> fmt_blob) {
    tracks_.clear();
    if (fmt_blob.size() < BGM_HEADER_SIZE) return false;

    // The 16-byte header is not a magic we can check for: the shipped file
    // starts with the first track's name. So the check is structural -- the
    // entries must tile the data file with no gaps, which is verified by the
    // caller comparing the last start+length against the stream size.
    std::size_t at = 0;
    while (at + BGM_ENTRY_SIZE <= fmt_blob.size()) {
        const u8* e = fmt_blob.data() + at;

        // A name is NUL-terminated ASCII; an all-zero slot means the table ended
        // (the shipped file has 17 trailing bytes after the last entry).
        std::size_t name_len = 0;
        while (name_len < 16 && e[name_len] != 0) ++name_len;
        if (name_len == 0) break;

        BgmTrack track;
        track.name.assign(reinterpret_cast<const char*>(e), name_len);
        track.start_offset = read_u32(e + 0x10);
        track.length_bytes = read_u32(e + 0x14);
        track.loop_start = read_u32(e + 0x18);
        track.sample_rate = read_u32(e + 0x24);

        const u32 format_flags = read_u32(e + 0x20);
        const u32 byte_rate = read_u32(e + 0x28);
        track.channels = 2;
        track.bits = 16;

        // Nine of the ten entries carry format_flags 0x20001 and a sane
        // sample_rate. The tenth (th165_06.wav, a borrowed track) has
        // format_flags 0x76 and garbage in the rate fields -- its slot does not
        // follow the same layout. Falling back to the stream-wide format keeps
        // it playable instead of producing a negative rate.
        const bool sane = (format_flags == 0x20001u) && track.sample_rate > 0 &&
                          byte_rate > 0;
        if (sane) {
            // byte_rate = rate * channels * bits/8, so channels*bits/8 = ratio.
            const u32 ratio = byte_rate / track.sample_rate;
            if (ratio == 4) { track.channels = 2; track.bits = 16; }
            else if (ratio == 2) { track.channels = 1; track.bits = 16; }
            else if (ratio == 1) { track.channels = 1; track.bits = 8; }
        } else {
            // Every other track in this file is 44100 Hz stereo 16-bit.
            track.sample_rate = 44100;
            track.channels = 2;
            track.bits = 16;
        }

        if (track.length_bytes == 0) break;
        tracks_.push_back(std::move(track));
        at += BGM_ENTRY_SIZE;
    }

    stats_.tracks = u32(tracks_.size());
    return !tracks_.empty();
}

void BgmPlayer::attach_stream(const u8* data, std::size_t size) {
    stream_ = data;
    stream_size_ = size;
}

bool BgmPlayer::play(u32 index) {
    if (index >= tracks_.size() || !stream_) return false;
    const BgmTrack& track = tracks_[index];
    if (std::size_t(track.start_offset) + track.length_bytes > stream_size_) {
        return false;
    }
    track_ = index;
    position_ = 0;
    playing_ = true;
    return true;
}

void BgmPlayer::stop() {
    playing_ = false;
    position_ = 0;
}

void BgmPlayer::mix(float* out, u32 frames) {
    if (!out || frames == 0) return;
    for (u32 i = 0; i < frames * 2; ++i) out[i] = 0.0f;
    if (!playing_ || !stream_ || track_ >= tracks_.size()) return;

    const BgmTrack& track = tracks_[track_];
    const u32 bytes_per_frame = track.channels * (track.bits / 8u);
    const u32 length = track.length_bytes;
    // The loop point is an offset INTO the track, not into the file.
    u32 loop_from = track.loop_start;
    if (loop_from >= length) loop_from = 0;

    for (u32 f = 0; f < frames; ++f) {
        if (position_ + bytes_per_frame > length) {
            position_ = loop_from;
            if (position_ + bytes_per_frame > length) {
                playing_ = false;
                break;
            }
        }

        const u8* p = stream_ + track.start_offset + position_;
        float left = 0.0f;
        float right = 0.0f;
        if (track.bits == 16) {
            const i16 l = i16(i16(p[0]) | (i16(p[1]) << 8));
            left = float(l) / 32768.0f;
            if (track.channels > 1) {
                const i16 r = i16(i16(p[2]) | (i16(p[3]) << 8));
                right = float(r) / 32768.0f;
            } else {
                right = left;
            }
        } else {
            left = (float(p[0]) - 128.0f) / 128.0f;
            right = left;
        }
        out[f * 2] += left;
        out[f * 2 + 1] += right;
        position_ += bytes_per_frame;
    }

    for (u32 i = 0; i < frames * 2; ++i) {
        if (out[i] > 1.0f) out[i] = 1.0f;
        else if (out[i] < -1.0f) out[i] = -1.0f;
    }
    stats_.mixed_frames += frames;
}

}  // namespace th185
