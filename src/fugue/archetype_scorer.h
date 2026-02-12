/// @brief Archetype-specific quality scoring for fugue subjects.
///
/// Evaluates subjects against structural strategy requirements: inversion
/// compatibility, stretto potential, Kopfmotiv strength, and archetype fitness.

#ifndef BACH_FUGUE_ARCHETYPE_SCORER_H
#define BACH_FUGUE_ARCHETYPE_SCORER_H

#include "fugue/archetype_policy.h"
#include "fugue/subject.h"

namespace bach {

/// @brief Multi-dimensional archetype quality score for a fugue subject.
///
/// Each dimension is scored in [0, 1]. Hard gate conditions are checked
/// separately via checkHardGate().
struct ArchetypeScore {
  float archetype_fitness = 0.0f;   ///< Match to archetype structural requirements.
  float inversion_quality = 0.0f;   ///< Quality when diatonically inverted.
  float stretto_potential = 0.0f;   ///< Number/quality of valid stretto intervals.
  float kopfmotiv_strength = 0.0f;  ///< Head motif distinctiveness and reusability.

  /// @brief Compute weighted composite score.
  /// @return Weighted sum in [0, 1].
  ///
  /// Weights: fitness=0.30, inversion=0.25, stretto=0.25, kopfmotiv=0.20.
  float composite() const;
};

/// @brief Evaluates fugue subjects for archetype-specific quality.
///
/// Uses existing utilities:
/// - invertMelodyDiatonic() for inversion quality
/// - findValidStrettoIntervals() for stretto potential
/// - Subject::extractKopfmotiv() for head motif analysis
class ArchetypeScorer {
 public:
  /// @brief Evaluate a subject against an archetype policy.
  /// @param subject The subject to evaluate.
  /// @param policy Archetype policy with structural requirements.
  /// @return ArchetypeScore with all dimensions filled.
  ArchetypeScore evaluate(const Subject& subject,
                          const ArchetypePolicy& policy) const;

  /// @brief Check hard gate conditions from archetype policy.
  ///
  /// Returns false if the subject violates any required condition:
  /// - require_invertible: inversion must not create excessive parallel perfects
  /// - require_fragmentable: Kopfmotiv must be rhythmically distinct
  /// - require_contour_symmetry: ascending/descending contour must be balanced
  /// - require_functional_resolution: chromatic steps must resolve
  /// - require_axis_stability: inversion must stay within playable range
  ///
  /// @param subject The subject to check.
  /// @param policy Archetype policy with required conditions.
  /// @return True if all hard gate conditions pass.
  bool checkHardGate(const Subject& subject,
                     const ArchetypePolicy& policy) const;

  /// @brief Score how well the subject matches the archetype's structural profile.
  float scoreArchetypeFitness(const Subject& subject,
                              const ArchetypePolicy& policy) const;

  /// @brief Score the subject's quality when diatonically inverted.
  float scoreInversionQuality(const Subject& subject) const;

  /// @brief Score the subject's stretto potential.
  float scoreStrettoPotential(const Subject& subject) const;

  /// @brief Score the head motif's distinctiveness and reusability.
  /// @param subject The subject to evaluate.
  /// @param policy Archetype policy with fragment/sequence weights.
  /// @return Score in [0, 1].
  float scoreKopfmotivStrength(const Subject& subject,
                               const ArchetypePolicy& policy) const;
};

}  // namespace bach

#endif  // BACH_FUGUE_ARCHETYPE_SCORER_H
