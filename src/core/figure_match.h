// Figure matching utilities for comparing note sequences against
// vocabulary MelodicFigure definitions.

#ifndef BACH_CORE_FIGURE_MATCH_H
#define BACH_CORE_FIGURE_MATCH_H

#include <cstdint>

#include "core/bach_vocabulary.h"
#include "core/basic_types.h"

namespace bach {
namespace figure_match {

/// @brief Score how well a pitch sequence matches a MelodicFigure.
///
/// For Degree-mode figures: converts pitches to scale degrees and compares
/// degree intervals. Direction-first scoring: exact match=1.0, same direction
/// +/-1 degree=0.3, direction-only=0.1, wrong direction=0.0. Chroma offset
/// match adds +0.1 bonus.
///
/// For Semitone-mode figures: compares directed semitone intervals directly.
/// Exact match=1.0, +/-1 semitone=0.3, else 0.0.
///
/// @param pitches Array of MIDI pitch values.
/// @param count Number of pitches (must equal figure.note_count or returns 0).
/// @param figure The MelodicFigure to match against.
/// @param key Current musical key.
/// @param scale Current scale type.
/// @return Match quality in [0.0, 1.0]. Returns 0.0 if count != figure.note_count.
float matchFigure(const uint8_t* pitches, int count,
                  const MelodicFigure& figure,
                  Key key, ScaleType scale);

/// @brief Find the best matching figure from a table.
///
/// Iterates through the table, calling matchFigure for each entry.
/// Returns the index of the highest-scoring figure that meets the threshold.
///
/// @param pitches Array of MIDI pitch values.
/// @param count Number of pitches.
/// @param table Array of pointers to MelodicFigure.
/// @param table_size Number of entries in the table.
/// @param key Current musical key.
/// @param scale Current scale type.
/// @param threshold Minimum match quality to accept (default 0.7).
/// @return Index of best match in table, or -1 if none meets threshold.
int findBestFigure(const uint8_t* pitches, int count,
                   const MelodicFigure* const* table, int table_size,
                   Key key, ScaleType scale,
                   float threshold = 0.7f);

}  // namespace figure_match
}  // namespace bach

#endif  // BACH_CORE_FIGURE_MATCH_H
