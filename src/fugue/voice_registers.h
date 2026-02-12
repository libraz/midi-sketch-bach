// Shared voice range constants for fugue voices.

#ifndef BACH_FUGUE_VOICE_REGISTERS_H
#define BACH_FUGUE_VOICE_REGISTERS_H

#include <cstdint>
#include <utility>
#include <vector>

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

/// @brief Per-voice pitch register boundaries.
struct VoiceRegister {
  uint8_t low;
  uint8_t high;
};

/// @brief Calculate the optimal octave shift to place pitches in a voice register.
///
/// Uses weighted scoring to evaluate candidate shifts (lower score = better):
///   overflow_penalty    (w=100): out-of-range overflow
///   instant_cross       (w=50/15): first pitch crosses adjacent voice's last pitch
///   parallel_risk       (w=20): parallel perfect consonance with adjacent voice
///   melodic_distance    (w=10): distance from reference pitch
///   order_violation     (w=5): sustained voice order inversion
///   register_drift      (w=3): exposition-only drift from last subject entry
///   clarity_penalty     (w=2): distance from voice characteristic range
///   center_distance     (w=1): distance from voice center
///
/// @param pitches Array of MIDI pitch values to shift.
/// @param num_pitches Number of elements in pitches array.
/// @param range_lo Lower bound of target voice register (inclusive).
/// @param range_hi Upper bound of target voice register (inclusive).
/// @param reference_pitch Previous pitch in the same voice (0 = none).
/// @param prev_reference_pitch Pitch before reference_pitch (0 = none).
/// @param adjacent_last_pitch Last pitch sounded by an adjacent voice (0 = none).
/// @param adjacent_prev_pitch Previous pitch of the adjacent voice (0 = none).
/// @param adjacent_lo Lower bound of adjacent voice's register (0 = none).
/// @param adjacent_hi Upper bound of adjacent voice's register (0 = none).
/// @param is_subject_voice True if this entry carries the subject (reduced cross penalty).
/// @param last_subject_pitch Last subject entry's starting pitch (0 = none).
/// @param is_exposition True if currently in exposition phase (enables drift penalty).
/// @return Optimal octave shift in semitones (multiple of 12).
int fitToRegister(const uint8_t* pitches, size_t num_pitches,
                  uint8_t range_lo, uint8_t range_hi,
                  uint8_t reference_pitch = 0,
                  uint8_t prev_reference_pitch = 0,
                  uint8_t adjacent_last_pitch = 0,
                  uint8_t adjacent_prev_pitch = 0,
                  uint8_t adjacent_lo = 0, uint8_t adjacent_hi = 0,
                  bool is_subject_voice = false,
                  uint8_t last_subject_pitch = 0,
                  bool is_exposition = false);

/// @brief NoteEvent vector overload of fitToRegister.
/// @param notes Vector of NoteEvent whose pitches are evaluated.
/// @param range_lo Lower bound of target voice register (inclusive).
/// @param range_hi Upper bound of target voice register (inclusive).
/// @param reference_pitch Previous pitch in the same voice (0 = none).
/// @param prev_reference_pitch Pitch before reference_pitch (0 = none).
/// @param adjacent_last_pitch Last pitch sounded by an adjacent voice (0 = none).
/// @param adjacent_prev_pitch Previous pitch of the adjacent voice (0 = none).
/// @param adjacent_lo Lower bound of adjacent voice's register (0 = none).
/// @param adjacent_hi Upper bound of adjacent voice's register (0 = none).
/// @param is_subject_voice True if this entry carries the subject (reduced cross penalty).
/// @param last_subject_pitch Last subject entry's starting pitch (0 = none).
/// @param is_exposition True if currently in exposition phase (enables drift penalty).
/// @return Optimal octave shift in semitones (multiple of 12).
int fitToRegister(const std::vector<NoteEvent>& notes,
                  uint8_t range_lo, uint8_t range_hi,
                  uint8_t reference_pitch = 0,
                  uint8_t prev_reference_pitch = 0,
                  uint8_t adjacent_last_pitch = 0,
                  uint8_t adjacent_prev_pitch = 0,
                  uint8_t adjacent_lo = 0, uint8_t adjacent_hi = 0,
                  bool is_subject_voice = false,
                  uint8_t last_subject_pitch = 0,
                  bool is_exposition = false);

}  // namespace bach

#endif  // BACH_FUGUE_VOICE_REGISTERS_H
