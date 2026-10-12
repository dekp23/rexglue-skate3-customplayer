/**
 * Standalone XMA2 stream decoder (dekp23 custom music). See stream_decoder.h.
 *
 * Packet/frame layout as used by XmaContext's decoder (xma_context.cpp, helpers.h):
 *   packet header (32 bits): frame count (6) | first frame bit offset (15) |
 *                            metadata (3) | packet skip (8)
 *   frame: 15-bit length (in bits, including the length field) ... last bit = 1 if
 *          another frame follows in the same packet. Frames may continue into the next
 *          packet of the stream (after that packet's 32-bit header).
 * Each frame is handed to FFmpeg's "xmaframes" decoder in the same form XmaContext uses:
 * byte 0 = (padding_start << 5) | (padding_end << 2), then the frame bits.
 */

#include <rex/audio/xma/stream_decoder.h>

#include <algorithm>
#include <cmath>
#include <cstring>

extern "C" {
#include "libavcodec/avcodec.h"
}

namespace rex::audio {
namespace {

constexpr uint32_t kBitsPerPacket = XmaStreamDecoder::kPacketBytes * 8;
constexpr uint32_t kBitsPerPacketHeader = 32;
constexpr uint32_t kBitsPerFrameHeader = 15;
constexpr uint32_t kMaxFrameLength = 0x7FFF;

uint32_t PacketFrameOffset(const uint8_t* packet) {
  return static_cast<uint32_t>(((packet[0] & 0x3) << 13) | (packet[1] << 5) | (packet[2] >> 3)) +
         kBitsPerPacketHeader;
}
uint32_t PacketFrameCount(const uint8_t* packet) { return packet[0] >> 2; }
uint32_t PacketSkip(const uint8_t* packet) { return packet[3]; }

// Cursor over the bits of one XMA stream spread across packets.
class StreamCursor {
 public:
  StreamCursor(const uint8_t* packets, size_t count) : packets_(packets), count_(count) {}

  bool valid() const { return packet_ < count_ && bit_ < kBitsPerPacket; }
  size_t packet() const { return packet_; }
  const uint8_t* packet_ptr() const { return packets_ + packet_ * XmaStreamDecoder::kPacketBytes; }

  // Positions the cursor at the first frame that begins in packet `p` (or a later packet).
  void SeekFirstFrame(size_t p) {
    packet_ = p;
    while (packet_ < count_) {
      const uint8_t* pk = packet_ptr();
      const uint32_t off = PacketFrameOffset(pk);
      if (PacketFrameCount(pk) > 0 && off < kBitsPerPacket) {
        bit_ = off;
        return;
      }
      packet_ = NextPacket(packet_);
    }
    bit_ = kBitsPerPacket;
  }

  size_t NextPacket(size_t p) const {
    return p + 1 + PacketSkip(packets_ + p * XmaStreamDecoder::kPacketBytes);
  }

  // Reads one bit; crosses into the next packet of the stream when needed.
  bool ReadBit(uint32_t* out) {
    if (bit_ >= kBitsPerPacket) {
      packet_ = NextPacket(packet_);
      bit_ = kBitsPerPacketHeader;
    }
    if (packet_ >= count_) return false;
    const uint8_t byte = packet_ptr()[bit_ >> 3];
    *out = (byte >> (7 - (bit_ & 7))) & 1u;
    ++bit_;
    return true;
  }

  bool Read(uint32_t n, uint32_t* out) {
    uint32_t v = 0;
    for (uint32_t i = 0; i < n; ++i) {
      uint32_t b;
      if (!ReadBit(&b)) return false;
      v = (v << 1) | b;
    }
    *out = v;
    return true;
  }

  // Bits left in the current packet.
  uint32_t BitsLeftInPacket() const { return bit_ < kBitsPerPacket ? kBitsPerPacket - bit_ : 0; }

  void Save(size_t* p, uint32_t* b) const { *p = packet_; *b = bit_; }
  void Restore(size_t p, uint32_t b) { packet_ = p; bit_ = b; }

 private:
  const uint8_t* packets_;
  size_t count_;
  size_t packet_ = 0;
  uint32_t bit_ = kBitsPerPacket;
};

}  // namespace

struct XmaStreamDecoder::Impl {
  const AVCodec* codec = nullptr;
  AVCodecContext* ctx = nullptr;
  AVPacket* pkt = nullptr;
  AVFrame* frame = nullptr;
  uint32_t channels = 0;
  uint32_t sample_rate = 0;
  std::vector<uint8_t> frame_buf;

  ~Impl() { Close(); }
  void Close() {
    if (ctx) avcodec_free_context(&ctx);
    if (pkt) av_packet_free(&pkt);
    if (frame) av_frame_free(&frame);
  }
  bool Reopen() {
    if (ctx) avcodec_free_context(&ctx);
    ctx = avcodec_alloc_context3(codec);
    if (!ctx) return false;
    // Same settings as XmaContext::PrepareDecoder.
    ctx->sample_rate = static_cast<int>(sample_rate);
    ctx->channels = static_cast<int>(channels);
    ctx->flags2 |= AV_CODEC_FLAG2_SKIP_MANUAL;
    return avcodec_open2(ctx, codec, nullptr) >= 0;
  }
};

XmaStreamDecoder::XmaStreamDecoder() : impl_(std::make_unique<Impl>()) {}
XmaStreamDecoder::~XmaStreamDecoder() = default;

bool XmaStreamDecoder::Open(uint32_t sample_rate, uint32_t channels) {
  if (channels < 1 || channels > 2 || sample_rate == 0) return false;
  impl_->Close();
  impl_->codec = avcodec_find_decoder(AV_CODEC_ID_XMAFRAMES);
  if (!impl_->codec) return false;
  impl_->pkt = av_packet_alloc();
  impl_->frame = av_frame_alloc();
  impl_->channels = channels;
  impl_->sample_rate = sample_rate;
  impl_->frame_buf.assign(1 + (kMaxFrameLength + 7) / 8 + AV_INPUT_BUFFER_PADDING_SIZE, 0);
  return impl_->pkt && impl_->frame && impl_->Reopen();
}

int XmaStreamDecoder::DecodeStream(const uint8_t* packets, size_t packet_count,
                                   std::vector<int16_t>* out) {
  Impl& d = *impl_;
  if (!d.ctx || !packets || !out) return -1;
  // A fresh codec state per stream, like a newly set-up hardware context.
  if (!d.Reopen()) return -1;

  StreamCursor cur(packets, packet_count);
  cur.SeekFirstFrame(0);
  int frames = 0;
  constexpr float kScale = (1 << 15) - 1;  // as XmaContext::ConvertFrame

  while (cur.valid()) {
    const size_t start_packet = cur.packet();
    // End-of-packet padding or too few bits for a frame header: next packet.
    size_t save_p;
    uint32_t save_b;
    cur.Save(&save_p, &save_b);
    uint32_t len = 0;
    if (!cur.Read(kBitsPerFrameHeader, &len)) break;
    if (len == kMaxFrameLength || len <= kBitsPerFrameHeader) {
      cur.SeekFirstFrame(cur.NextPacket(start_packet));
      continue;
    }
    cur.Restore(save_p, save_b);

    // Copy the whole frame (length field included) to byte 1 onwards, bit-aligned.
    std::fill(d.frame_buf.begin(), d.frame_buf.end(), uint8_t{0});
    uint32_t last_bit = 0;
    bool truncated = false;
    for (uint32_t i = 0; i < len; ++i) {
      uint32_t b;
      if (!cur.ReadBit(&b)) {
        truncated = true;
        break;
      }
      if (b) d.frame_buf[1 + (i >> 3)] |= static_cast<uint8_t>(0x80u >> (i & 7));
      last_bit = b;
    }
    if (truncated) break;
    const bool crossed = cur.packet() != start_packet || cur.BitsLeftInPacket() == 0;

    const int size = static_cast<int>(1 + (len + 7) / 8);
    const uint32_t padding_end = static_cast<uint32_t>(size) * 8 - (8 + len);
    d.frame_buf[0] = static_cast<uint8_t>((padding_end & 7) << 2);  // padding_start = 0
    d.pkt->data = d.frame_buf.data();
    d.pkt->size = size;
    if (avcodec_send_packet(d.ctx, d.pkt) < 0) return -1;
    const int ret = avcodec_receive_frame(d.ctx, d.frame);
    if (ret == 0) {
      const uint32_t n = std::min<uint32_t>(kSamplesPerFrame, static_cast<uint32_t>(d.frame->nb_samples));
      const size_t base = out->size();
      out->resize(base + static_cast<size_t>(kSamplesPerFrame) * d.channels, 0);
      for (uint32_t c = 0; c < d.channels; ++c) {
        const auto* src = reinterpret_cast<const float*>(d.frame->data[c]);
        for (uint32_t i = 0; i < n; ++i) {
          // cvtps2dq rounds to nearest even; packssdw saturates.
          const float v = std::nearbyint(src[i] * kScale);
          const float s = std::clamp(v, -32768.0f, 32767.0f);
          (*out)[base + static_cast<size_t>(i) * d.channels + c] = static_cast<int16_t>(s);
        }
      }
      ++frames;
    } else if (ret != AVERROR(EAGAIN)) {
      return -1;
    }

    if (crossed) {
      // Frames resume after the split frame, at the new packet's first-frame offset.
      if (cur.BitsLeftInPacket() == 0) {
        cur.SeekFirstFrame(cur.NextPacket(cur.packet()));
      } else {
        cur.SeekFirstFrame(cur.packet());
      }
    } else if (!last_bit) {
      cur.SeekFirstFrame(cur.NextPacket(start_packet));
    }
  }
  return frames;
}

}  // namespace rex::audio
