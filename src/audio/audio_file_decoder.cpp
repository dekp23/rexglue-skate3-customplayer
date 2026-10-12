/**
 * Audio file decoder (dekp23 custom music). See audio_file_decoder.h.
 */

#include <rex/audio/audio_file_decoder.h>

#include <cstring>
#include <memory>

extern "C" {
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libavutil/channel_layout.h"
#include "libavutil/opt.h"
#include "libswresample/swresample.h"
}

namespace rex::audio {
namespace {

constexpr int kOutRate = 44100;

std::string AvError(int err) {
  char buf[AV_ERROR_MAX_STRING_SIZE] = {};
  av_strerror(err, buf, sizeof(buf));
  return buf;
}

struct FormatCloser {
  void operator()(AVFormatContext* f) const { avformat_close_input(&f); }
};
struct CodecCloser {
  void operator()(AVCodecContext* c) const { avcodec_free_context(&c); }
};
struct SwrCloser {
  void operator()(SwrContext* s) const { swr_free(&s); }
};
struct FrameCloser {
  void operator()(AVFrame* f) const { av_frame_free(&f); }
};
struct PacketCloser {
  void operator()(AVPacket* p) const { av_packet_free(&p); }
};

std::string Tag(AVDictionary* d, const char* key) {
  const AVDictionaryEntry* e = av_dict_get(d, key, nullptr, 0);
  return e && e->value ? e->value : "";
}

// Opens the file and reads tags + cover; returns the best audio stream index or < 0.
int Open(const std::string& path, std::unique_ptr<AVFormatContext, FormatCloser>* fmt,
         DecodedAudioFile* out) {
  AVFormatContext* raw = nullptr;
  int err = avformat_open_input(&raw, path.c_str(), nullptr, nullptr);
  if (err < 0) {
    out->error = "cannot open file: " + AvError(err);
    return -1;
  }
  fmt->reset(raw);
  err = avformat_find_stream_info(raw, nullptr);
  if (err < 0) {
    out->error = "cannot read stream info: " + AvError(err);
    return -1;
  }
  const int stream = av_find_best_stream(raw, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
  if (stream < 0) {
    out->error = "no supported audio stream";
    return -1;
  }
  // Tags: container first, then the audio stream (ogg/opus keep them on the stream).
  AVDictionary* dicts[2] = {raw->metadata, raw->streams[stream]->metadata};
  for (AVDictionary* d : dicts) {
    if (out->title.empty()) out->title = Tag(d, "title");
    if (out->artist.empty()) out->artist = Tag(d, "artist");
    if (out->artist.empty()) out->artist = Tag(d, "album_artist");
    if (out->album.empty()) out->album = Tag(d, "album");
  }
  for (unsigned i = 0; i < raw->nb_streams; ++i) {
    const AVStream* s = raw->streams[i];
    if ((s->disposition & AV_DISPOSITION_ATTACHED_PIC) && s->attached_pic.size > 0) {
      out->cover.assign(s->attached_pic.data, s->attached_pic.data + s->attached_pic.size);
      break;
    }
  }
  return stream;
}

}  // namespace

bool ReadAudioFileTags(const std::string& path_utf8, DecodedAudioFile* out) {
  std::unique_ptr<AVFormatContext, FormatCloser> fmt;
  return Open(path_utf8, &fmt, out) >= 0;
}

bool DecodeAudioFile(const std::string& path_utf8, DecodedAudioFile* out,
                     const std::atomic<bool>* cancel, double max_seconds) {
  out->pcm.clear();
  out->error.clear();
  std::unique_ptr<AVFormatContext, FormatCloser> fmt;
  const int stream = Open(path_utf8, &fmt, out);
  if (stream < 0) return false;
  AVStream* st = fmt->streams[stream];

  const AVCodec* codec = avcodec_find_decoder(st->codecpar->codec_id);
  if (!codec) {
    out->error = std::string("unsupported audio codec: ") + avcodec_get_name(st->codecpar->codec_id);
    return false;
  }
  out->codec = codec->name;
  std::unique_ptr<AVCodecContext, CodecCloser> dec(avcodec_alloc_context3(codec));
  if (!dec || avcodec_parameters_to_context(dec.get(), st->codecpar) < 0) {
    out->error = "cannot set up decoder";
    return false;
  }
  dec->pkt_timebase = st->time_base;
  int err = avcodec_open2(dec.get(), codec, nullptr);
  if (err < 0) {
    out->error = "cannot open decoder: " + AvError(err);
    return false;
  }

  std::unique_ptr<AVFrame, FrameCloser> frame(av_frame_alloc());
  std::unique_ptr<AVPacket, PacketCloser> pkt(av_packet_alloc());
  std::unique_ptr<SwrContext, SwrCloser> swr;
  int64_t in_layout = 0;
  int in_rate = 0, in_format = -1;
  const size_t max_samples =
      max_seconds > 0 ? static_cast<size_t>(max_seconds * kOutRate) : SIZE_MAX;

  auto convert = [&](const AVFrame* f) -> bool {
    const int64_t layout = f->channel_layout ? static_cast<int64_t>(f->channel_layout)
                                             : av_get_default_channel_layout(f->channels);
    if (!swr || layout != in_layout || f->sample_rate != in_rate || f->format != in_format) {
      in_layout = layout;
      in_rate = f->sample_rate;
      in_format = f->format;
      swr.reset(swr_alloc_set_opts(nullptr, AV_CH_LAYOUT_STEREO, AV_SAMPLE_FMT_S16, kOutRate,
                                   layout, static_cast<AVSampleFormat>(f->format), f->sample_rate,
                                   0, nullptr));
      if (!swr || swr_init(swr.get()) < 0) {
        out->error = "cannot set up resampler";
        return false;
      }
    }
    const int cap = swr_get_out_samples(swr.get(), f ? f->nb_samples : 0);
    if (cap <= 0) return true;
    const size_t base = out->pcm.size();
    out->pcm.resize(base + static_cast<size_t>(cap) * 2);
    uint8_t* dst = reinterpret_cast<uint8_t*>(out->pcm.data() + base);
    const int got = swr_convert(swr.get(), &dst, cap,
                                f ? const_cast<const uint8_t**>(f->extended_data) : nullptr,
                                f ? f->nb_samples : 0);
    if (got < 0) {
      out->error = "resampling failed";
      return false;
    }
    out->pcm.resize(base + static_cast<size_t>(got) * 2);
    return true;
  };

  auto drain = [&]() -> bool {
    while (true) {
      const int r = avcodec_receive_frame(dec.get(), frame.get());
      if (r == AVERROR(EAGAIN) || r == AVERROR_EOF) return true;
      if (r < 0) {
        out->error = "decoding failed: " + AvError(r);
        return false;
      }
      const bool ok = convert(frame.get());
      av_frame_unref(frame.get());
      if (!ok) return false;
    }
  };

  while (true) {
    if (cancel && cancel->load()) {
      out->error = "cancelled";
      return false;
    }
    if (out->pcm.size() / 2 >= max_samples) break;
    err = av_read_frame(fmt.get(), pkt.get());
    if (err == AVERROR_EOF) break;
    if (err < 0) {
      // Damaged tail: keep what was decoded.
      if (!out->pcm.empty()) break;
      out->error = "read failed: " + AvError(err);
      return false;
    }
    if (pkt->stream_index == stream) {
      const int s = avcodec_send_packet(dec.get(), pkt.get());
      if (s < 0 && s != AVERROR(EAGAIN) && s != AVERROR_INVALIDDATA) {
        av_packet_unref(pkt.get());
        out->error = "decoding failed: " + AvError(s);
        return false;
      }
      if (!drain()) {
        av_packet_unref(pkt.get());
        return false;
      }
    }
    av_packet_unref(pkt.get());
  }
  avcodec_send_packet(dec.get(), nullptr);
  if (!drain()) return false;
  if (swr) {
    // Flush the resampler's delay.
    const int cap = swr_get_out_samples(swr.get(), 0);
    if (cap > 0) {
      const size_t base = out->pcm.size();
      out->pcm.resize(base + static_cast<size_t>(cap) * 2);
      uint8_t* dst = reinterpret_cast<uint8_t*>(out->pcm.data() + base);
      const int got = swr_convert(swr.get(), &dst, cap, nullptr, 0);
      out->pcm.resize(base + static_cast<size_t>(got > 0 ? got : 0) * 2);
    }
  }
  if (out->pcm.size() > max_samples * 2) out->pcm.resize(max_samples * 2);
  if (out->pcm.empty()) {
    out->error = "no audio decoded";
    return false;
  }
  return true;
}

}  // namespace rex::audio
