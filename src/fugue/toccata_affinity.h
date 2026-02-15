// Toccata-fugue Kerngestalt affinity scorer.
// Measures how well a fugue subject echoes toccata opening gesture intervals.

#ifndef BACH_FUGUE_TOCCATA_AFFINITY_H
#define BACH_FUGUE_TOCCATA_AFFINITY_H

#include <vector>

namespace bach {

/// @brief Compute affinity between a subject's interval profile and toccata
///        gesture core intervals.
///
/// Checks original and inverted forms of toccata_intervals against
/// subject_intervals[0:8] (head only). Uses direction match (weight 0.7)
/// and magnitude proximity (weight 0.3) for scoring.
///
/// @param subject_intervals Directed interval sequence of the fugue subject.
/// @param toccata_intervals Core intervals from toccata opening gesture.
/// @return Normalized affinity score in [0.0, 1.0]. Returns 0 if either input
///         has fewer than 3 intervals.
float computeToccataAffinity(const std::vector<int>& subject_intervals,
                             const std::vector<int>& toccata_intervals);

}  // namespace bach

#endif  // BACH_FUGUE_TOCCATA_AFFINITY_H
