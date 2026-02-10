// Shared voice range constants for fugue voices.

#ifndef BACH_FUGUE_VOICE_REGISTERS_H
#define BACH_FUGUE_VOICE_REGISTERS_H

#include <cstdint>
#include <utility>

#include "core/basic_types.h"

namespace bach {

/// @brief Get the pitch range for a fugue voice based on Bach organ practice.
///
/// Returns the same ranges used by exposition voice assignment:
///   2 voices: V0(55-84), V1(36-67)
///   3 voices: V0(60-96), V1(55-79), V2(36-60)
///   4+ voices: V0(60-96), V1(55-79), V2(48-72), V3(24-50)
///
/// @param voice_id Voice identifier (0 = soprano/first, increasing = lower).
/// @param num_voices Total number of voices in the fugue (2-5).
/// @return Pair of (low_pitch, high_pitch) inclusive MIDI pitch bounds.
std::pair<uint8_t, uint8_t> getFugueVoiceRange(VoiceId voice_id, uint8_t num_voices);

}  // namespace bach

#endif  // BACH_FUGUE_VOICE_REGISTERS_H
