// BassRealizer -- role-based bass line realization from harmonic scheme.

#ifndef BACH_SOLO_STRING_ARCH_BASS_REALIZER_H
#define BACH_SOLO_STRING_ARCH_BASS_REALIZER_H

#include <cstdint>
#include <vector>

#include "core/basic_types.h"
#include "core/note_source.h"

namespace bach {

class ChaconneScheme;
struct KeySignature;

/// @brief Bass realization style determines rhythmic and melodic character.
///
/// Each style is mapped 1:1 from a VariationRole by getRealizationStyle().
/// The style governs note density, passing tone usage, and register behavior.
enum class BassRealizationStyle : uint8_t {
  Simple,      ///< Establish/Resolve: chord root, whole/half notes.
  Walking,     ///< Develop: strong-beat chord tones, weak-beat stepwise passing.
  Syncopated,  ///< Destabilize: limited weak-beat emphasis, chord tones only.
  Lyrical,     ///< Illuminate: half-note base with 3rd/5th leaps.
  Elaborate,   ///< Accumulate: eighth-note arpeggios, staged register expansion.
};

/// @brief Register profile for a specific VariationRole.
///
/// Defines the effective pitch range for bass note generation. The profile
/// is narrower than the full instrument range for most roles, expanding
/// progressively during Accumulate variations.
struct BassRegisterProfile {
  uint8_t effective_low;   ///< Effective lower bound for this role.
  uint8_t effective_high;  ///< Effective upper bound for this role.
};

/// @brief Map VariationRole to BassRealizationStyle.
/// @param role The variation role determining musical character.
/// @return Corresponding bass realization style.
BassRealizationStyle getRealizationStyle(VariationRole role);

/// @brief Compute role-specific register profile with staged expansion.
///
/// For most roles, the register is the inner portion of the instrument range.
/// Accumulate variations expand progressively based on accumulate_index:
///   - index 0: +3 semitones above Simple range
///   - index 1: +5 semitones
///   - index 2: +7 semitones (maximum)
///
/// @param role VariationRole for register selection.
/// @param instrument_low Instrument's lowest MIDI pitch.
/// @param instrument_high Instrument's highest MIDI pitch.
/// @param accumulate_index Position within Accumulate block (0-based), ignored for other roles.
/// @return BassRegisterProfile with effective bounds.
BassRegisterProfile getBassRegisterProfile(
    VariationRole role, uint8_t instrument_low, uint8_t instrument_high,
    int accumulate_index = 0);

/// @brief Generate a bass line from the harmonic scheme.
///
/// Realizes the ChaconneScheme as a sequence of NoteEvents, with rhythmic
/// and melodic character determined by the VariationRole. Output notes are
/// 0-based (start_tick relative to the scheme start).
///
/// All output notes carry BachNoteSource::ChaconneBass.
///
/// @param scheme The harmonic scheme defining the chord progression.
/// @param key The key for pitch realization.
/// @param role VariationRole determines realization style.
/// @param register_low Instrument's lowest MIDI pitch.
/// @param register_high Instrument's highest MIDI pitch.
/// @param seed RNG seed for deterministic generation.
/// @param accumulate_index Position within Accumulate block (0-based), ignored for other roles.
/// @return Vector of NoteEvents forming the bass line.
std::vector<NoteEvent> realizeBass(
    const ChaconneScheme& scheme,
    const KeySignature& key,
    VariationRole role,
    uint8_t register_low, uint8_t register_high,
    uint32_t seed,
    int accumulate_index = 0);

}  // namespace bach

#endif  // BACH_SOLO_STRING_ARCH_BASS_REALIZER_H
