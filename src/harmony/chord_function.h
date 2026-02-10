// Chord function analysis -- convenience interface for harmonic function
// classification, secondary dominant resolution, and functional progression
// validation.
//
// This header re-exports HarmonicFunction and related utilities from
// harmonic_function.h, providing a unified API for chord function analysis.

#ifndef BACH_HARMONY_CHORD_FUNCTION_H
#define BACH_HARMONY_CHORD_FUNCTION_H

#include "harmony/chord_types.h"
#include "harmony/harmonic_event.h"
#include "harmony/harmonic_function.h"

namespace bach {

// HarmonicFunction enum and classifyFunction are defined in harmonic_function.h.
// The following functions are declared there and available through this header:
//
//   HarmonicFunction classifyFunction(ChordDegree degree, bool is_minor);
//   bool isValidFunctionalProgression(HarmonicFunction from, HarmonicFunction to);
//   bool isValidDegreeProgression(ChordDegree from, ChordDegree to, bool is_minor);
//   ChordDegree getSecondaryDominantTarget(ChordDegree degree);
//   bool isSecondaryDominant(ChordDegree degree);

}  // namespace bach

#endif  // BACH_HARMONY_CHORD_FUNCTION_H
