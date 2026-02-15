// Chord voicing and voice leading for harmony-first generation.

#ifndef BACH_HARMONY_CHORD_VOICER_H
#define BACH_HARMONY_CHORD_VOICER_H

#include <array>
#include <cstdint>
#include <functional>

#include "harmony/harmonic_event.h"

namespace bach {

/// Maximum number of voices supported by ChordVoicing.
constexpr uint8_t kMaxVoicingVoices = 5;

/// @brief A vertical chord voicing across multiple voices.
///
/// Pitches are stored in descending order: pitches[0] is the highest (soprano),
/// pitches[num_voices-1] is the lowest (bass). The invariant
/// pitches[i] >= pitches[i+1] is always maintained (no voice crossing).
struct ChordVoicing {
  std::array<uint8_t, kMaxVoicingVoices> pitches = {};
  uint8_t num_voices = 0;
};

/// Type alias for a function returning (low_pitch, high_pitch) range for a voice.
using VoiceRangeFn = std::function<std::pair<uint8_t, uint8_t>(uint8_t)>;

/// @brief Voice a chord across the given number of voices.
///
/// Places bass from event.bass_pitch, soprano near the top of voice 0's range,
/// and distributes remaining chord tones to inner voices. Applies doubling
/// rules: root > fifth > third priority; leading tone never doubled; diminished
/// fifth never doubled.
///
/// @param event Harmonic event with chord and bass_pitch.
/// @param num_voices Number of voices (2-5).
/// @param voice_range Function returning (low, high) pitch range per voice index.
/// @return ChordVoicing with pitches in descending order.
ChordVoicing voiceChord(const HarmonicEvent& event, uint8_t num_voices,
                        VoiceRangeFn voice_range);

/// @brief Create a smooth voice leading from prev voicing to the next chord.
///
/// Each voice moves to the nearest available chord tone, avoiding parallel
/// fifths and octaves. Leading tone resolves up to tonic; dominant seventh
/// resolves down by step.
///
/// @param prev Previous voicing.
/// @param next_event Next harmonic event.
/// @param num_voices Number of voices (must match prev.num_voices).
/// @param voice_range Function returning (low, high) pitch range per voice index.
/// @return New voicing with minimal total voice motion.
ChordVoicing smoothVoiceLeading(const ChordVoicing& prev,
                                const HarmonicEvent& next_event,
                                uint8_t num_voices,
                                VoiceRangeFn voice_range);

/// @brief Get all chord tone pitch classes for a given chord quality and root.
/// @param quality Chord quality.
/// @param root_pc Root pitch class (0-11).
/// @return Vector of pitch classes (size 3-4 depending on quality).
std::vector<int> getChordPitchClasses(ChordQuality quality, int root_pc);

}  // namespace bach

#endif  // BACH_HARMONY_CHORD_VOICER_H
