// Fixed 13-byte GOG audio block -> sixteen little-endian signed PCM samples.
#pragma once
#include <stdint.h>

// Input/output are disjoint. Integer wrap, predictor truncation and shifts
// follow the original x86 decoder; no saturation or resampling is introduced.
uint32_t sh4_adpcm_block(uint8_t output[32],const uint8_t encoded[13],uint32_t *predictor);

// Stream packets always consume 26 bytes. Compact mode uses the first channel
// only (32 PCM bytes); stereo interleaves both channels (64 PCM bytes).
void sh4_adpcm_packets(uint8_t *output,const uint8_t *encoded,unsigned packets,int stereo);
