/**
 * XMA frame filter: lets the host application inspect or replace decoded XMA
 * frames before they reach guest memory (e.g. user music replacement).
 *
 * Only the default decoder path (xma_use_old_decoder = true) invokes the filter.
 */
#pragma once

#include <cstdint>

namespace rex::audio {

struct XmaFrameFilter {
  // Called on the XMA worker thread after a frame is decoded and converted to
  // interleaved big-endian int16 (512 samples per channel), before it is written
  // to the guest output ring.
  //   packet       2048-byte XMA packet in which the decoded frame begins, or for a
  //                frame split across packets (frame_index == -1), the packet in
  //                which it ends
  //   frame_index  index of the frame among frames beginning in `packet`
  //   channels     1 or 2
  // Return true if pcm_be was modified.
  bool (*on_frame)(void* user, uint32_t context_id, const uint8_t* packet, int frame_index,
                   uint32_t channels, uint8_t* pcm_be) = nullptr;
  // Called when the game clears or releases a context (voice reuse / rewind).
  void (*on_context_reset)(void* user, uint32_t context_id) = nullptr;
  void* user = nullptr;
};

// Installs the process-wide filter (copied). Pass nullptr to remove it.
// Install at startup before audio runs; removal is safe at any time.
void SetXmaFrameFilter(const XmaFrameFilter* filter);

// Current filter or nullptr. Returned object stays valid for the process lifetime.
const XmaFrameFilter* GetXmaFrameFilter();

}  // namespace rex::audio
