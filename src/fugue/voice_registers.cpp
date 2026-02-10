// Shared voice range constants for fugue voices.

#include "fugue/voice_registers.h"

namespace bach {

std::pair<uint8_t, uint8_t> getFugueVoiceRange(VoiceId voice_id, uint8_t num_voices) {
  if (num_voices == 2) {
    constexpr uint8_t kRanges2[][2] = {
        {55, 84},  // Voice 0 (upper): G3-C6
        {36, 67},  // Voice 1 (lower): C2-G4
    };
    auto idx = voice_id < 2 ? voice_id : 1;
    return {kRanges2[idx][0], kRanges2[idx][1]};
  }
  if (num_voices == 3) {
    constexpr uint8_t kRanges3[][2] = {
        {60, 96},  // Voice 0 (soprano): C4-C6
        {55, 79},  // Voice 1 (alto): G3-G5
        {36, 60},  // Voice 2 (bass): C2-C4
    };
    auto idx = voice_id < 3 ? voice_id : 2;
    return {kRanges3[idx][0], kRanges3[idx][1]};
  }
  // 4+ voices
  constexpr uint8_t kRanges4[][2] = {
      {60, 96},  // Voice 0 (soprano): C4-C6
      {55, 79},  // Voice 1 (alto): G3-G5
      {48, 72},  // Voice 2 (tenor): C3-C5
      {24, 50},  // Voice 3 (bass/pedal): C1-D3
  };
  auto idx = voice_id < 4 ? voice_id : 3;
  return {kRanges4[idx][0], kRanges4[idx][1]};
}

}  // namespace bach
