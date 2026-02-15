// Figuration pattern system for harmony-first prelude generation.

#ifndef BACH_FORMS_PRELUDE_FIGURATION_H
#define BACH_FORMS_PRELUDE_FIGURATION_H

#include <cstdint>
#include <functional>
#include <vector>

#include "core/basic_types.h"
#include "harmony/chord_voicer.h"
#include "harmony/harmonic_event.h"

namespace bach {

/// @brief Types of figuration patterns for prelude generation.
enum class FigurationType : uint8_t {
  BrokenChord,   ///< BWV 846 style: chord tones sounded in sequence.
  Alberti,       ///< Alberti bass: low-high-mid-high pattern.
  ScaleConnect,  ///< Chord tones connected by scale passing tones.
};

/// @brief A single step within a figuration pattern.
struct FigurationStep {
  uint8_t voice_index;    ///< Index into ChordVoicing pitches.
  int8_t scale_offset;    ///< 0=chord tone, ±1=adjacent scale tone (passing/neighbor).
  Tick relative_tick;     ///< Offset from beat start (in ticks).
  Tick duration;          ///< Duration of this note (in ticks).
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

}  // namespace bach

#endif  // BACH_FORMS_PRELUDE_FIGURATION_H
