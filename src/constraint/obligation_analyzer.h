// Obligation analyzer: extracts constraint profiles from subject note sequences.

#ifndef BACH_CONSTRAINT_OBLIGATION_ANALYZER_H
#define BACH_CONSTRAINT_OBLIGATION_ANALYZER_H

#include <vector>

#include "constraint/obligation.h"
#include "core/basic_types.h"

namespace bach {

/// @brief Analyze a subject's notes to extract its constraint profile.
///
/// Performs obligation extraction (LeadingTone, Seventh, LeapResolve,
/// StrongBeatHarm, CadenceStable, CadenceApproach), lateral dynamics
/// (HarmonicImpulse, RegisterTrajectory, AccentContour), and density metrics.
///
/// @param notes Subject notes (sorted by start_tick).
/// @param key Tonic pitch class.
/// @param is_minor True for minor key context.
/// @return Complete constraint profile for the subject.
SubjectConstraintProfile analyzeObligations(
    const std::vector<NoteEvent>& notes, Key key, bool is_minor);

/// @brief Compute the stretto feasibility matrix for a subject.
///
/// Evaluates all offset/voice combinations (offset from 1 beat to subject_length - 1 beat,
/// voices from 2 to 5) and populates profile.stretto_matrix with feasibility entries.
/// Each entry scores peak obligation density, vertical clash probability, rhythmic
/// interference, register overlap, perceptual overlap, and cadence conflict.
///
/// @param profile Constraint profile to populate (must have obligations and lateral dynamics).
/// @param notes Subject notes (sorted by start_tick).
/// @param subject_length Total subject duration in ticks.
void computeStrettoFeasibility(
    SubjectConstraintProfile& profile,
    const std::vector<NoteEvent>& notes,
    Tick subject_length);

}  // namespace bach

#endif  // BACH_CONSTRAINT_OBLIGATION_ANALYZER_H
