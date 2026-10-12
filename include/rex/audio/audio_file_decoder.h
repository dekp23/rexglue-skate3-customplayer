/**
 * Audio file decoder (dekp23 custom music).
 *
 * Decodes a music file (mp3, m4a/aac/alac, webm/mkv/opus, ogg/vorbis/opus, flac, wav, aiff,
 * wma) with the runtime's bundled FFmpeg into interleaved 44.1 kHz stereo int16, and reads
 * its tags and embedded cover art. No external tools. Host-side only; thread-safe (each
 * call is independent).
 */
#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

namespace rex::audio {

struct DecodedAudioFile {
  std::vector<int16_t> pcm;          // interleaved stereo, 44100 Hz
  std::string title, artist, album;  // from tags (UTF-8), empty if absent
  std::vector<uint8_t> cover;        // embedded picture as stored (usually JPEG or PNG)
  std::string codec;                 // e.g. "opus"
  std::string error;                 // set when decoding failed
};

// path_utf8: file path in UTF-8. cancel (optional) stops early (returns false).
// max_seconds (0 = no limit) caps very long files.
bool DecodeAudioFile(const std::string& path_utf8, DecodedAudioFile* out,
                     const std::atomic<bool>* cancel = nullptr, double max_seconds = 0);

// Reads only tags and cover art (fast; no audio decoding).
bool ReadAudioFileTags(const std::string& path_utf8, DecodedAudioFile* out);

}  // namespace rex::audio
