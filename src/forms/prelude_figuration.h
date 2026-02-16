// Figuration pattern system for harmony-first prelude generation.

#ifndef BACH_FORMS_PRELUDE_FIGURATION_H
#define BACH_FORMS_PRELUDE_FIGURATION_H

#include <cstdint>
#include <functional>
#include <random>
#include <vector>

#include "core/basic_types.h"
#include "harmony/chord_voicer.h"
#include "harmony/harmonic_event.h"
#include "solo_string/solo_vocabulary.h"

namespace bach {

/// @brief Types of figuration patterns for prelude generation.
enum class FigurationType : uint8_t {
  BrokenChord,   ///< BWV 846 style: chord tones sounded in sequence.
  Alberti,       ///< Alberti bass: low-high-mid-high pattern.
  ScaleConnect,  ///< Chord tones connected by scale passing tones.
  SlotPattern,   ///< Bach reference slot pattern from FigurationSlotPattern.
  Falling3v,     ///< 3-voice descending sweep: sop->mid->bass.
  Arch3v,        ///< 3-voice arch: bass->mid->sop->mid.
  Mixed3v,       ///< 3-voice mixed: alternating chord tones with passing.
  Falling4v,     ///< 4-voice descending sweep with inner passing tones.
};

/// @brief Non-chord-tone function label for figuration steps.
/// Used to enforce strict enclosure conditions for passing and neighbor tones.
enum class NCTFunction : uint8_t {
  ChordTone,  ///< Harmony note (scale_offset == 0).
  Passing,    ///< Passing tone: stepwise approach and departure in same direction.
  Neighbor,   ///< Neighbor tone: weak-beat departure returning to original pitch.
};

/// @brief A single step within a figuration pattern.
struct FigurationStep {
  uint8_t voice_index;    ///< Index into ChordVoicing pitches.
  int8_t scale_offset;    ///< 0=chord tone, +/-1=adjacent scale tone (passing/neighbor).
  Tick relative_tick;     ///< Offset from beat start (in ticks).
  Tick duration;          ///< Duration of this note (in ticks).
  NCTFunction nct_function = NCTFunction::ChordTone;  ///< Non-chord-tone function label.
};

/// @brief A complete figuration template for one beat.
struct FigurationTemplate {
  FigurationType type;
  std::vector<FigurationStep> steps;
};

/// @brief Create a figuration template for the given type and voice count.
/// @param type Figuration type.
/// @param num_voices Number of voices in the voicing (2-5).
/// @return A template with steps spanning one beat (kTicksPerBeat ticks).
FigurationTemplate createFigurationTemplate(FigurationType type,
                                            uint8_t num_voices);

/// @brief Create a figuration template with rhythm variation support.
/// @param type Figuration type.
/// @param num_voices Number of voices in the voicing (2-5).
/// @param rng Random number generator for rhythm variation.
/// @param rhythm_variation_prob Probability of rhythm variation (0.0-1.0).
/// @return A template with steps spanning one beat (kTicksPerBeat ticks).
FigurationTemplate createFigurationTemplate(FigurationType type,
                                            uint8_t num_voices,
                                            std::mt19937& rng,
                                            float rhythm_variation_prob = 0.0f);

/// @brief Create a figuration template from a vocabulary slot pattern.
/// @param pattern The slot pattern defining the chord tone ordering.
/// @param num_voices Number of voices in the voicing (2-5).
/// @return A template with steps spanning one beat (kTicksPerBeat ticks).
FigurationTemplate createFigurationTemplateFromSlot(
    const FigurationSlotPattern& pattern, uint8_t num_voices);

/// @brief Apply a figuration template to a chord voicing, producing NoteEvents.
///
/// For steps with scale_offset != 0, the pitch is adjusted to the nearest
/// adjacent scale tone. All pitches are clamped to the voice's range.
///
/// @param voicing The chord voicing to figurate.
/// @param tmpl The figuration template.
/// @param beat_start_tick Absolute tick of the beat start.
/// @param event Harmonic event (for key/scale context).
/// @param voice_range Voice range function for pitch clamping.
/// @return Vector of NoteEvents with source=PreludeFiguration.
std::vector<NoteEvent> applyFiguration(const ChordVoicing& voicing,
                                       const FigurationTemplate& tmpl,
                                       Tick beat_start_tick,
                                       const HarmonicEvent& event,
                                       VoiceRangeFn voice_range);

/// @brief Apply figuration with section-aware NCT direction bias.
///
/// Identical to the base overload but biases non-chord-tone directions based
/// on position within the piece:
///   - Opening (0.0-0.15): prefer lower neighbors (harmonic stability).
///   - Middle (0.15-0.85): balanced upper/lower neighbors (original direction).
///   - Closing (0.85-1.0): prefer downward resolution for passing and neighbor tones.
///
/// @param voicing The chord voicing to figurate.
/// @param tmpl The figuration template.
/// @param beat_start_tick Absolute tick of the beat start.
/// @param event Harmonic event (for key/scale context).
/// @param voice_range Voice range function for pitch clamping.
/// @param section_progress Progress within piece (0.0=start, 1.0=end).
/// @return Vector of NoteEvents with source=PreludeFiguration.
std::vector<NoteEvent> applyFiguration(const ChordVoicing& voicing,
                                       const FigurationTemplate& tmpl,
                                       Tick beat_start_tick,
                                       const HarmonicEvent& event,
                                       VoiceRangeFn voice_range,
                                       float section_progress);

}  // namespace bach

#endif  // BACH_FORMS_PRELUDE_FIGURATION_H
