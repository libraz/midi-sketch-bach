// Shared voice range constants for fugue voices.

#ifndef BACH_FUGUE_VOICE_REGISTERS_H
#define BACH_FUGUE_VOICE_REGISTERS_H

#include <cstdint>
#include <utility>
#include <vector>

#include "core/basic_types.h"
#include "core/form_profile.h"
#include "fugue/fugue_config.h"

namespace bach {

/// @brief Get the pitch range for a fugue voice based on Bach organ practice.
///
/// Returns the same ranges used by exposition voice assignment:
///   2 voices: V0(55-84), V1(36-67)
///   3 voices: V0(60-96), V1(55-79), V2(48-72) [Auto/ManualBass] or V2(24-50) [TruePedal]
///   4+ voices: V0(60-96), V1(55-79), V2(48-72), V3(24-50)
///
/// @param voice_id Voice identifier (0 = soprano/first, increasing = lower).
/// @param num_voices Total number of voices in the fugue (2-5).
/// @param pedal_mode Pedal voice treatment (only affects 3-voice V2).
/// @return Pair of (low_pitch, high_pitch) inclusive MIDI pitch bounds.
std::pair<uint8_t, uint8_t> getFugueVoiceRange(VoiceId voice_id, uint8_t num_voices,
                                                PedalMode pedal_mode = PedalMode::Auto);

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

/// @brief Phase-position-aware register fitting with envelope observation.
///
/// Calculates an effective narrowed range based on the RegisterEnvelope and
/// the piece position (phase_pos), then delegates to the existing fitToRegister.
/// Phase A (observation-only): penalty = 0; counts notes outside the envelope
/// range via the optional overflow counter.
///
/// Piecewise linear interpolation between four phases:
///   [0.00, 0.25) opening_range_ratio (exposition)
///   [0.25, 0.60) middle_range_ratio  (development)
///   [0.60, 0.85) climax_range_ratio  (stretto)
///   [0.85, 1.00] closing_range_ratio (coda)
///
/// @param notes Vector of NoteEvent whose pitches are evaluated.
/// @param voice_id Voice identifier (0 = soprano, increasing = lower).
/// @param num_voices Total number of voices in the fugue.
/// @param phase_pos Position in the piece as fraction [0.0, 1.0].
/// @param envelope RegisterEnvelope with per-phase range ratios.
/// @param reference_pitch Previous pitch in the same voice (0 = none).
/// @param adjacent_last_pitch Last pitch sounded by an adjacent voice (0 = none).
/// @param envelope_overflow_count If non-null, incremented for each note outside
///        the envelope-narrowed range (observation only, no penalty applied).
/// @return Optimal octave shift in semitones (multiple of 12).
int fitToRegisterWithEnvelope(
    const std::vector<NoteEvent>& notes,
    uint8_t voice_id, uint8_t num_voices,
    float phase_pos,
    const RegisterEnvelope& envelope,
    uint8_t reference_pitch = 0,
    uint8_t adjacent_last_pitch = 0,
    int* envelope_overflow_count = nullptr,
    uint8_t adjacent_lo = 0,
    uint8_t adjacent_hi = 0);

}  // namespace bach

#endif  // BACH_FUGUE_VOICE_REGISTERS_H
