/**
 * Standalone XMA2 stream decoder (dekp23 custom music).
 *
 * Decodes a sequence of 2048-byte XMA2 packets of ONE stream (packet skip counts are
 * honoured) into interleaved int16 PCM with the same FFmpeg "xmaframes" decoder and the
 * same float->int16 conversion the emulated XMA hardware uses. It touches no guest
 * memory and no XmaContext state, so it can run on any host thread.
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace rex::audio {

class XmaStreamDecoder {
 public:
  static constexpr size_t kPacketBytes = 2048;
  static constexpr uint32_t kSamplesPerFrame = 512;  // per channel

  XmaStreamDecoder();
  ~XmaStreamDecoder();
  XmaStreamDecoder(const XmaStreamDecoder&) = delete;
  XmaStreamDecoder& operator=(const XmaStreamDecoder&) = delete;

  // channels: 1 or 2. Returns false if the codec is unavailable.
  bool Open(uint32_t sample_rate, uint32_t channels);

  // Decodes `packet_count` consecutive packets (packet_count * kPacketBytes bytes) as one
  // stream from its start and appends every decoded frame (kSamplesPerFrame * channels
  // interleaved native-endian int16) to `out`. The decoder is flushed first, so each
  // call is independent. Returns the number of frames decoded, or -1 on a decode error.
  int DecodeStream(const uint8_t* packets, size_t packet_count, std::vector<int16_t>* out);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace rex::audio
