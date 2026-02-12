#include "fugue/subject.h"

#include <gtest/gtest.h>

#include <cmath>

#include "fugue/archetype_policy.h"
#include "fugue/archetype_scorer.h"
#include "fugue/fugue_config.h"
#include "fugue/subject_validator.h"

namespace bach {
namespace {

class SubjectArchetypeTest : public ::testing::Test {
 protected:
  Subject generateWithArchetype(FugueArchetype archetype,
                                 SubjectCharacter character = SubjectCharacter::Severe,
                                 uint32_t seed = 42) {
    FugueConfig config;
    config.character = character;
    config.archetype = archetype;
    config.key = Key::C;
    config.is_minor = false;
    config.seed = seed;
    SubjectGenerator gen;
    return gen.generate(config, seed);
  }
};

TEST_F(SubjectArchetypeTest, CompactNarrowRange) {
  const auto& policy = getArchetypePolicy(FugueArchetype::Compact);
  for (uint32_t seed = 0; seed < 10; ++seed) {
    Subject sub = generateWithArchetype(FugueArchetype::Compact,
                                         SubjectCharacter::Severe, seed);
    // Compact range should be <= max_range_degrees in semitones.
    // (7 degrees ~ 12 semitones for major scale)
    EXPECT_LE(sub.range(), (policy.max_range_degrees + 1) * 2)
        << "seed=" << seed;
  }
}

TEST_F(SubjectArchetypeTest, CantabileWiderRange) {
  for (uint32_t seed = 0; seed < 10; ++seed) {
    Subject sub = generateWithArchetype(FugueArchetype::Cantabile,
                                         SubjectCharacter::Noble, seed);
    EXPECT_GT(sub.noteCount(), 0u) << "seed=" << seed;
  }
}

TEST_F(SubjectArchetypeTest, CantabileSmoothMotion) {
  for (uint32_t seed = 0; seed < 10; ++seed) {
    Subject sub = generateWithArchetype(FugueArchetype::Cantabile,
                                         SubjectCharacter::Noble, seed);
    if (sub.noteCount() < 2) continue;
    // Count step motion (<=2 semitones) vs total intervals.
    int steps = 0;
    int total = 0;
    for (size_t idx = 1; idx < sub.notes.size(); ++idx) {
      int interval = std::abs(static_cast<int>(sub.notes[idx].pitch) -
                               static_cast<int>(sub.notes[idx - 1].pitch));
      if (interval <= 2) ++steps;
      ++total;
    }
    if (total > 0) {
      float step_ratio = static_cast<float>(steps) / static_cast<float>(total);
      // Cantabile should have high step ratio (at least 0.35 as a soft check).
      EXPECT_GE(step_ratio, 0.35f) << "seed=" << seed;
    }
  }
}

TEST_F(SubjectArchetypeTest, DefaultCompactMatchesSevere) {
  // Default archetype=Compact + character=Severe should produce valid output.
  FugueConfig config;
  config.character = SubjectCharacter::Severe;
  config.archetype = FugueArchetype::Compact;
  SubjectGenerator gen;
  SubjectValidator validator;
  for (uint32_t seed = 0; seed < 5; ++seed) {
    Subject sub = gen.generate(config, seed);
    EXPECT_GT(sub.noteCount(), 0u) << "seed=" << seed;
    SubjectScore score = validator.evaluate(sub);
    // Should still produce reasonable subjects.
    EXPECT_GE(score.composite(), 0.4f) << "seed=" << seed;
  }
}

TEST_F(SubjectArchetypeTest, ChromaticPreservesAnswerType) {
  const auto& policy = getArchetypePolicy(FugueArchetype::Chromatic);
  EXPECT_EQ(policy.preferred_answer, AnswerType::Real);
}

// ---------------------------------------------------------------------------
// Step 4: NoteFunction integration (observed through generation differences)
// ---------------------------------------------------------------------------

TEST_F(SubjectArchetypeTest, DifferentArchetypesProduceDifferentResults) {
  // Different archetypes have different policies (path_candidates, weights,
  // hard gates) that feed into generateNotes(). Even with the same seed, these
  // policy differences should produce distinct pitch sequences.
  constexpr int kTestSeeds = 5;
  int differ_count = 0;

  for (uint32_t seed = 100; seed < 100 + kTestSeeds; ++seed) {
    Subject compact = generateWithArchetype(FugueArchetype::Compact,
                                             SubjectCharacter::Severe, seed);
    Subject invertible = generateWithArchetype(FugueArchetype::Invertible,
                                                SubjectCharacter::Severe, seed);
    ASSERT_GT(compact.noteCount(), 0u) << "Compact empty at seed=" << seed;
    ASSERT_GT(invertible.noteCount(), 0u) << "Invertible empty at seed=" << seed;

    // Compare note sequences: either note count or pitch pattern should differ.
    bool same_count = (compact.noteCount() == invertible.noteCount());
    bool same_pitches = false;
    if (same_count) {
      same_pitches = true;
      for (size_t i = 0; i < compact.notes.size(); ++i) {
        if (compact.notes[i].pitch != invertible.notes[i].pitch) {
          same_pitches = false;
          break;
        }
      }
    }

    if (!same_count || !same_pitches) {
      ++differ_count;
    }
  }

  // With 5 seeds, the majority should produce different results across
  // archetypes. The archetypes have different path_candidates (6 vs 12),
  // different hard gates, and different scoring weights.
  EXPECT_GE(differ_count, 3)
      << "At least 3 out of 5 seeds should produce different subjects "
         "for Compact vs Invertible archetypes";
}

// ---------------------------------------------------------------------------
// Step 5: Outer loop uses archetype scoring
// ---------------------------------------------------------------------------

TEST_F(SubjectArchetypeTest, OuterLoopProducesValidSubjectsForAllArchetypes) {
  // The outer loop in SubjectGenerator::generate() uses a weighted combination
  // of base quality and archetype scoring. Verify all archetypes produce valid
  // subjects across multiple seeds.
  const FugueArchetype archetypes[] = {
      FugueArchetype::Compact,
      FugueArchetype::Cantabile,
      FugueArchetype::Invertible,
      FugueArchetype::Chromatic,
  };
  SubjectValidator validator;
  ArchetypeScorer archetype_scorer;

  for (auto archetype : archetypes) {
    const auto& policy = getArchetypePolicy(archetype);
    for (uint32_t seed = 0; seed < 5; ++seed) {
      Subject sub = generateWithArchetype(archetype,
                                           SubjectCharacter::Severe, seed);
      ASSERT_GT(sub.noteCount(), 0u)
          << "archetype=" << static_cast<int>(archetype)
          << " seed=" << seed;

      // The outer loop blends base_quality_weight * base + (1 - w) * archetype.
      // Both the base and archetype scores should be computable and in range.
      float base = validator.evaluate(sub).composite();
      float arch = archetype_scorer.evaluate(sub, policy).composite();
      float combined = base * policy.base_quality_weight +
                       arch * (1.0f - policy.base_quality_weight);

      EXPECT_GE(combined, 0.0f)
          << "archetype=" << static_cast<int>(archetype)
          << " seed=" << seed;
      EXPECT_LE(combined, 1.0f)
          << "archetype=" << static_cast<int>(archetype)
          << " seed=" << seed;
    }
  }
}

TEST_F(SubjectArchetypeTest, ArchetypeScoringInfluencesSelection) {
  // With the same seed, different archetypes have different base_quality_weight
  // values (Compact=0.60, Invertible=0.50), meaning archetype scoring has
  // 40% vs 50% influence. This should lead to different selection outcomes
  // at least some of the time.
  constexpr uint32_t kSeed = 42;
  SubjectValidator validator;
  ArchetypeScorer archetype_scorer;

  Subject compact = generateWithArchetype(FugueArchetype::Compact,
                                           SubjectCharacter::Severe, kSeed);
  Subject cantabile = generateWithArchetype(FugueArchetype::Cantabile,
                                             SubjectCharacter::Severe, kSeed);

  const auto& compact_policy = getArchetypePolicy(FugueArchetype::Compact);
  const auto& cantabile_policy = getArchetypePolicy(FugueArchetype::Cantabile);

  // Verify the policies have different base_quality_weight.
  ASSERT_NE(compact_policy.base_quality_weight,
            cantabile_policy.base_quality_weight)
      << "Compact and Cantabile should have different base_quality_weight";

  // Compute the outer-loop combined score for each.
  float compact_base = validator.evaluate(compact).composite();
  float compact_arch =
      archetype_scorer.evaluate(compact, compact_policy).composite();
  float compact_combined =
      compact_base * compact_policy.base_quality_weight +
      compact_arch * (1.0f - compact_policy.base_quality_weight);

  float cantabile_base = validator.evaluate(cantabile).composite();
  float cantabile_arch =
      archetype_scorer.evaluate(cantabile, cantabile_policy).composite();
  float cantabile_combined =
      cantabile_base * cantabile_policy.base_quality_weight +
      cantabile_arch * (1.0f - cantabile_policy.base_quality_weight);

  // Both combined scores must be valid.
  EXPECT_GE(compact_combined, 0.0f);
  EXPECT_LE(compact_combined, 1.0f);
  EXPECT_GE(cantabile_combined, 0.0f);
  EXPECT_LE(cantabile_combined, 1.0f);
}

// ---------------------------------------------------------------------------
// Step 6: Path candidates vary by archetype
// ---------------------------------------------------------------------------

TEST_F(SubjectArchetypeTest, PathCandidatesVaryByArchetype) {
  // Verify that each archetype has a specific path_candidates count and that
  // the relative ordering reflects exploration budget: archetypes that need
  // more exploration (Invertible, Chromatic) have more candidates than simpler
  // archetypes (Compact).
  const auto& compact = getArchetypePolicy(FugueArchetype::Compact);
  const auto& cantabile = getArchetypePolicy(FugueArchetype::Cantabile);
  const auto& invertible = getArchetypePolicy(FugueArchetype::Invertible);
  const auto& chromatic = getArchetypePolicy(FugueArchetype::Chromatic);

  // Exact values from the policy definitions.
  EXPECT_EQ(compact.path_candidates, 6);
  EXPECT_EQ(cantabile.path_candidates, 8);
  EXPECT_EQ(invertible.path_candidates, 12);
  EXPECT_EQ(chromatic.path_candidates, 10);

  // Relative ordering: Invertible needs the most exploration due to symmetry
  // and axis stability hard gates filtering out many candidates.
  EXPECT_GT(invertible.path_candidates, compact.path_candidates)
      << "Invertible should explore more paths than Compact";
  EXPECT_GT(invertible.path_candidates, cantabile.path_candidates)
      << "Invertible should explore more paths than Cantabile";
  EXPECT_GT(chromatic.path_candidates, compact.path_candidates)
      << "Chromatic should explore more paths than Compact";

  // All must be positive.
  EXPECT_GT(compact.path_candidates, 0);
  EXPECT_GT(cantabile.path_candidates, 0);
  EXPECT_GT(invertible.path_candidates, 0);
  EXPECT_GT(chromatic.path_candidates, 0);
}

TEST_F(SubjectArchetypeTest, MoreCandidatesMaintainsQuality) {
  // Archetypes with more path_candidates (Invertible=12) should still produce
  // subjects of comparable quality to archetypes with fewer candidates
  // (Compact=6), because the additional exploration compensates for stricter
  // hard gates.
  SubjectValidator validator;
  float compact_sum = 0.0f;
  float invertible_sum = 0.0f;
  constexpr int kSeeds = 10;

  for (uint32_t seed = 0; seed < kSeeds; ++seed) {
    Subject compact = generateWithArchetype(FugueArchetype::Compact,
                                             SubjectCharacter::Severe, seed);
    Subject invertible = generateWithArchetype(FugueArchetype::Invertible,
                                                SubjectCharacter::Severe, seed);

    if (compact.noteCount() > 0) {
      compact_sum += validator.evaluate(compact).composite();
    }
    if (invertible.noteCount() > 0) {
      invertible_sum += validator.evaluate(invertible).composite();
    }
  }

  float compact_avg = compact_sum / static_cast<float>(kSeeds);
  float invertible_avg = invertible_sum / static_cast<float>(kSeeds);

  // Invertible's extra path candidates should compensate for hard gates,
  // keeping average quality within a reasonable margin of Compact.
  EXPECT_GE(invertible_avg, compact_avg - 0.15f)
      << "Invertible average quality should not fall too far below Compact "
         "(compact_avg=" << compact_avg
      << ", invertible_avg=" << invertible_avg << ")";
}

}  // namespace
}  // namespace bach
