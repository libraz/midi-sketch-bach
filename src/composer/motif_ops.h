#ifndef BACH_COMPOSER_MOTIF_OPS_H
#define BACH_COMPOSER_MOTIF_OPS_H

#include <cstddef>
#include <cstdint>
#include <vector>

#include "composer/material.h"
#include "core/basic_types.h"

namespace bach::composer::motif_ops {

// The five canonical episode-derivation operations Bach uses on a
// subject motif. Original is the identity (no transformation, used by
// "subject quotation" episode patches and by tests).
enum class EpisodeMotifTransform : std::uint8_t {
  Original = 0,
  Invert = 1,
  Retrograde = 2,
  Augment = 3,
  Diminish = 4,
};

// Chromatic pitch inversion around `pivot`. Each note's pitch becomes
// 2*pivot - pitch (clamped to [0, 127]). Start_tick and duration are
// unchanged.
std::vector<MaterialNote> invertMelody(const std::vector<MaterialNote>& notes, std::uint8_t pivot);

// Reverse the order of notes, preserving rhythmic structure (durations
// and inter-note gaps appear in reverse). The result starts at
// `start_tick`.
std::vector<MaterialNote> retrogradeMelody(const std::vector<MaterialNote>& notes, Tick start_tick);

// Multiply each note's offset-from-start and duration by `factor`. The
// result starts at `start_tick`. Factor <= 0 is treated as 1 (identity).
std::vector<MaterialNote> augmentDuration(const std::vector<MaterialNote>& notes, Tick start_tick,
                                          int factor = 2);

// Divide each note's offset-from-start and duration by `factor`.
// Duration is clamped to a minimum of 1 tick so no zero-length notes
// are produced. Factor <= 0 is treated as 1 (identity).
std::vector<MaterialNote> diminishDuration(const std::vector<MaterialNote>& notes, Tick start_tick,
                                           int factor = 2);

// Composite: dispatch to one of the four ops by name (Original returns
// the input notes verbatim, re-anchored at `target_start_tick`).
// `invert_pivot` is used only for Invert. `factor` is used only for
// Augment / Diminish.
std::vector<MaterialNote> applyTransform(const std::vector<MaterialNote>& source,
                                         EpisodeMotifTransform transform, Tick target_start_tick,
                                         std::uint8_t invert_pivot = 60, int factor = 2);

// Identity transform that re-anchors the source notes to `start_tick`.
// Pulled out so applyTransform(Original) and the test harness can call
// the same helper.
std::vector<MaterialNote> reanchorMelody(const std::vector<MaterialNote>& notes, Tick start_tick);

// Routing from SubjectCharacter to EpisodeMotifTransform. Matches plan
// §5 P5: Severe=Original, Playful=Invert, Noble=Augment, Restless=Diminish.
EpisodeMotifTransform characterToTransform(SubjectCharacter character);

}  // namespace bach::composer::motif_ops

#endif  // BACH_COMPOSER_MOTIF_OPS_H
