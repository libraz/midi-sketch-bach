/// @file
/// @brief Tests for archetype-specific quality scoring.

#include "fugue/archetype_scorer.h"

#include <gtest/gtest.h>

#include "core/basic_types.h"
#include "fugue/archetype_policy.h"
#include "fugue/subject.h"

namespace bach {
namespace {

// Helper: create a simple subject for testing.
Subject makeTestSubject(std::vector<uint8_t> pitches,
                        std::vector<Tick> durations,
                        Key key = Key::C, bool is_minor = false) {
  Subject s;
  s.key = key;
  s.is_minor = is_minor;
  s.character = SubjectCharacter::Severe;
  Tick tick = 0;
  for (size_t i = 0; i < pitches.size(); ++i) {
    NoteEvent n;
    n.start_tick = tick;
    n.duration = (i < durations.size()) ? durations[i] : kTicksPerBeat;
    n.pitch = pitches[i];
    n.velocity = 80;
    n.voice = 0;
    n.source = BachNoteSource::FugueSubject;
    s.notes.push_back(n);
    tick += n.duration;
  }
  s.length_ticks = tick;
  return s;
}

// ---------------------------------------------------------------------------
// ArchetypeScore composite
// ---------------------------------------------------------------------------

TEST(ArchetypeScorerTest, CompositeWeightsSum) {
  ArchetypeScore score;
  score.archetype_fitness = 1.0f;
  score.inversion_quality = 1.0f;
  score.stretto_potential = 1.0f;
  score.kopfmotiv_strength = 1.0f;
  // 0.30 + 0.25 + 0.25 + 0.20 = 1.0
  EXPECT_NEAR(score.composite(), 1.0f, 0.001f);
}

TEST(ArchetypeScorerTest, CompositeZeroForAllZeros) {
  ArchetypeScore score;
  EXPECT_NEAR(score.composite(), 0.0f, 0.001f);
}

// ---------------------------------------------------------------------------
// Archetype fitness
// ---------------------------------------------------------------------------

TEST(ArchetypeScorerTest, FitnessHighForMatchingSubject) {
  // C major stepwise subject within Compact range.
  auto subject = makeTestSubject(
      {60, 62, 64, 65, 67, 65, 64, 62, 60},
      {kTicksPerBeat, kTicksPerBeat, kTicksPerBeat, kTicksPerBeat,
       kTicksPerBeat, kTicksPerBeat, kTicksPerBeat, kTicksPerBeat,
       kTicksPerBeat});
  const auto& policy = getArchetypePolicy(FugueArchetype::Compact);
  ArchetypeScorer scorer;
  float fitness = scorer.scoreArchetypeFitness(subject, policy);
  EXPECT_GE(fitness, 0.6f) << "Stepwise subject should fit Compact well";
}

// ---------------------------------------------------------------------------
// Inversion quality
// ---------------------------------------------------------------------------

TEST(ArchetypeScorerTest, InversionQualityNonZero) {
  auto subject = makeTestSubject(
      {60, 62, 64, 65, 67, 65, 64, 62, 60},
      {kTicksPerBeat, kTicksPerBeat, kTicksPerBeat, kTicksPerBeat,
       kTicksPerBeat, kTicksPerBeat, kTicksPerBeat, kTicksPerBeat,
       kTicksPerBeat});
  ArchetypeScorer scorer;
  float inv = scorer.scoreInversionQuality(subject);
  EXPECT_GT(inv, 0.0f) << "Stepwise subject should have reasonable inversion";
}

// ---------------------------------------------------------------------------
// Stretto potential
// ---------------------------------------------------------------------------

TEST(ArchetypeScorerTest, StrettoPotentialBaseScore) {
  // Even a short subject should get at least the base score.
  auto subject = makeTestSubject(
      {60, 64, 67, 65, 60},
      {kTicksPerBeat, kTicksPerBeat, kTicksPerBeat, kTicksPerBeat,
       kTicksPerBeat});
  subject.length_ticks = kTicksPerBeat * 5;
  ArchetypeScorer scorer;
  float sp = scorer.scoreStrettoPotential(subject);
  EXPECT_GE(sp, 0.0f);
}

// ---------------------------------------------------------------------------
// Kopfmotiv strength
// ---------------------------------------------------------------------------

TEST(ArchetypeScorerTest, KopfmotivStrengthWithLeap) {
  // Subject starting with a leap should score higher.
  auto subject = makeTestSubject(
      {60, 67, 65, 64, 62, 60},
      {kTicksPerBeat, kTicksPerBeat / 2, kTicksPerBeat / 2, kTicksPerBeat,
       kTicksPerBeat, kTicksPerBeat});
  const auto& policy = getArchetypePolicy(FugueArchetype::Compact);
  ArchetypeScorer scorer;
  float kopf = scorer.scoreKopfmotivStrength(subject, policy);
  EXPECT_GE(kopf, 0.4f) << "Opening leap should contribute to Kopfmotiv strength";
}

TEST(ArchetypeScorerTest, KopfmotivStrengthWithRepetition) {
  // Subject with repeated pitches should score lower.
  // Use Cantabile policy (zero fragment/sequence weights) to test base scoring.
  auto subject = makeTestSubject(
      {60, 60, 60, 60},
      {kTicksPerBeat, kTicksPerBeat, kTicksPerBeat, kTicksPerBeat});
  const auto& policy = getArchetypePolicy(FugueArchetype::Cantabile);
  ArchetypeScorer scorer;
  float kopf = scorer.scoreKopfmotivStrength(subject, policy);
  EXPECT_LE(kopf, 0.3f) << "Repeated pitches = weak Kopfmotiv";
}

TEST(ArchetypeScorerTest, KopfmotivStrengthPolicyWeightsAffectScore) {
  // Compact policy has fragment_reusability_weight=0.3, sequence_potential_weight=0.2.
  // A narrow-range subject with repeated durations should get a higher score under
  // Compact (reusable fragment) than under Cantabile (base scoring only).
  auto subject = makeTestSubject(
      {60, 62, 64, 62},
      {kTicksPerBeat, kTicksPerBeat, kTicksPerBeat, kTicksPerBeat});
  ArchetypeScorer scorer;
  const auto& compact_policy = getArchetypePolicy(FugueArchetype::Compact);
  const auto& cantabile_policy = getArchetypePolicy(FugueArchetype::Cantabile);
  float compact_score = scorer.scoreKopfmotivStrength(subject, compact_policy);
  float cantabile_score = scorer.scoreKopfmotivStrength(subject, cantabile_policy);
  // Both should be in valid range.
  EXPECT_GE(compact_score, 0.0f);
  EXPECT_LE(compact_score, 1.0f);
  EXPECT_GE(cantabile_score, 0.0f);
  EXPECT_LE(cantabile_score, 1.0f);
  // Policy weights should cause different scores.
  EXPECT_NE(compact_score, cantabile_score)
      << "Different policy weights should produce different Kopfmotiv scores";
}

// ---------------------------------------------------------------------------
// Hard gate
// ---------------------------------------------------------------------------

TEST(ArchetypeScorerTest, HardGatePassesForCompact) {
  auto subject = makeTestSubject(
      {60, 62, 64, 65, 67, 65, 64, 62, 60},
      {kTicksPerBeat, kTicksPerBeat, kTicksPerBeat, kTicksPerBeat,
       kTicksPerBeat, kTicksPerBeat, kTicksPerBeat, kTicksPerBeat,
       kTicksPerBeat});
  const auto& policy = getArchetypePolicy(FugueArchetype::Compact);
  ArchetypeScorer scorer;
  EXPECT_TRUE(scorer.checkHardGate(subject, policy));
}

TEST(ArchetypeScorerTest, HardGateContourSymmetryRejectsAscendingOnly) {
  // Purely ascending subject should fail contour symmetry.
  auto subject = makeTestSubject(
      {60, 62, 64, 65, 67, 69, 71, 72},
      {kTicksPerBeat, kTicksPerBeat, kTicksPerBeat, kTicksPerBeat,
       kTicksPerBeat, kTicksPerBeat, kTicksPerBeat, kTicksPerBeat});
  const auto& policy = getArchetypePolicy(FugueArchetype::Invertible);
  ArchetypeScorer scorer;
  EXPECT_FALSE(scorer.checkHardGate(subject, policy))
      << "Purely ascending subject should fail contour symmetry for Invertible";
}

// ---------------------------------------------------------------------------
// Full evaluate
// ---------------------------------------------------------------------------

TEST(ArchetypeScorerTest, EvaluateReturnsAllDimensions) {
  auto subject = makeTestSubject(
      {60, 64, 67, 65, 62, 60},
      {kTicksPerBeat, kTicksPerBeat / 2, kTicksPerBeat / 2, kTicksPerBeat,
       kTicksPerBeat, kTicksPerBeat});
  subject.length_ticks = kTicksPerBeat * 5;
  const auto& policy = getArchetypePolicy(FugueArchetype::Compact);
  ArchetypeScorer scorer;
  auto score = scorer.evaluate(subject, policy);
  EXPECT_GE(score.archetype_fitness, 0.0f);
  EXPECT_LE(score.archetype_fitness, 1.0f);
  EXPECT_GE(score.inversion_quality, 0.0f);
  EXPECT_LE(score.inversion_quality, 1.0f);
  EXPECT_GE(score.stretto_potential, 0.0f);
  EXPECT_LE(score.stretto_potential, 1.0f);
  EXPECT_GE(score.kopfmotiv_strength, 0.0f);
  EXPECT_LE(score.kopfmotiv_strength, 1.0f);
  EXPECT_GE(score.composite(), 0.0f);
  EXPECT_LE(score.composite(), 1.0f);
}

// ---------------------------------------------------------------------------
// Step 2: Symmetry scoring connection to fitness
// ---------------------------------------------------------------------------

TEST(ArchetypeScorerTest, SymmetryWeightAffectsFitness) {
  // A subject with balanced ascending/descending motion should produce
  // different fitness scores under Invertible (symmetry_score_weight=0.4) vs
  // Compact (symmetry_score_weight=0.0), since the symmetry blending code
  // path in scoreArchetypeFitness is only active when the weight is nonzero.
  auto subject = makeTestSubject(
      {60, 62, 64, 67, 65, 64, 62, 60},
      {kTicksPerBeat, kTicksPerBeat, kTicksPerBeat, kTicksPerBeat,
       kTicksPerBeat, kTicksPerBeat, kTicksPerBeat, kTicksPerBeat});
  const auto& invertible_policy = getArchetypePolicy(FugueArchetype::Invertible);
  const auto& compact_policy = getArchetypePolicy(FugueArchetype::Compact);

  // Verify the policy difference that drives the test.
  ASSERT_GT(invertible_policy.symmetry_score_weight, 0.0f)
      << "Invertible must have nonzero symmetry_score_weight";
  ASSERT_FLOAT_EQ(compact_policy.symmetry_score_weight, 0.0f)
      << "Compact must have zero symmetry_score_weight";

  ArchetypeScorer scorer;
  float invertible_fitness =
      scorer.scoreArchetypeFitness(subject, invertible_policy);
  float compact_fitness =
      scorer.scoreArchetypeFitness(subject, compact_policy);

  // The symmetry weight blends contour symmetry into fitness; with different
  // weights the scores must differ for any subject whose symmetry != raw fitness.
  EXPECT_NE(invertible_fitness, compact_fitness)
      << "Symmetry weight should cause different fitness for the same subject";

  // Both must remain in valid [0, 1] range.
  EXPECT_GE(invertible_fitness, 0.0f);
  EXPECT_LE(invertible_fitness, 1.0f);
  EXPECT_GE(compact_fitness, 0.0f);
  EXPECT_LE(compact_fitness, 1.0f);
}

TEST(ArchetypeScorerTest, SymmetricSubjectScoresHigherWithSymmetryWeight) {
  // Symmetric subject: C-D-E-G-E-D-C (balanced ascending/descending).
  auto symmetric = makeTestSubject(
      {60, 62, 64, 67, 64, 62, 60},
      {kTicksPerBeat, kTicksPerBeat, kTicksPerBeat, kTicksPerBeat,
       kTicksPerBeat, kTicksPerBeat, kTicksPerBeat});

  // Asymmetric subject: C-D-E-F-G-A-B (all ascending, zero descending motion).
  auto asymmetric = makeTestSubject(
      {60, 62, 64, 65, 67, 69, 71},
      {kTicksPerBeat, kTicksPerBeat, kTicksPerBeat, kTicksPerBeat,
       kTicksPerBeat, kTicksPerBeat, kTicksPerBeat});

  const auto& policy = getArchetypePolicy(FugueArchetype::Invertible);
  ASSERT_GT(policy.symmetry_score_weight, 0.0f);

  ArchetypeScorer scorer;
  float symmetric_fitness = scorer.scoreArchetypeFitness(symmetric, policy);
  float asymmetric_fitness = scorer.scoreArchetypeFitness(asymmetric, policy);

  // Under Invertible policy, the symmetric subject should receive a higher
  // fitness score because its contour symmetry is blended in.
  EXPECT_GT(symmetric_fitness, asymmetric_fitness)
      << "Symmetric subject should score higher than asymmetric under "
         "Invertible policy (symmetry_score_weight="
      << policy.symmetry_score_weight << ")";
}

}  // namespace
}  // namespace bach
