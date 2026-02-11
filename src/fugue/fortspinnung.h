// Fortspinnung ("spinning forth") episode generation using motif pool fragments.
//
// An additional episode variant for Baroque-style sequential development.
// Existing character-specific generators remain as the default; this engine
// provides an alternative that draws from the MotifPool for richer motivic
// continuity across episodes.

#ifndef BACH_FUGUE_FORTSPINNUNG_H
#define BACH_FUGUE_FORTSPINNUNG_H

#include <cstdint>
#include <vector>

#include "core/basic_types.h"
#include "fugue/motif_pool.h"
#include "fugue/subject.h"
#include "harmony/key.h"

namespace bach {

/// @brief Generate Fortspinnung-style episode material from the motif pool.
///
/// Fortspinnung ("spinning forth") is a Baroque compositional technique
/// where a short motif is developed through sequential progression,
/// fragmentation, and transformation. This engine uses the MotifPool
/// to select and connect fragments with proper voice-leading rules:
///   - Common tone connection (shared pitch between adjacent fragments)
///   - Stepwise connection (step of 1-2 semitones between fragments)
///   - Leap recovery (leaps > 4 semitones followed by contrary step)
///
/// Fortspinnung is an ADDITIONAL episode variant (not a replacement).
/// Existing character-specific generators remain as the default.
///
/// @param pool The motif pool (must be built before calling).
/// @param start_tick Starting tick position.
/// @param duration_ticks Available duration for the episode.
/// @param num_voices Number of active voices.
/// @param seed RNG seed for fragment selection.
/// @param character Subject character (influences fragment choice probability).
/// @param key Musical key for pedal anchor pitch calculation.
/// @return Vector of NoteEvents for the Fortspinnung passage.
std::vector<NoteEvent> generateFortspinnung(const MotifPool& pool,
                                            Tick start_tick,
                                            Tick duration_ticks,
                                            uint8_t num_voices,
                                            uint32_t seed,
                                            SubjectCharacter character,
                                            Key key);

}  // namespace bach

#endif  // BACH_FUGUE_FORTSPINNUNG_H
