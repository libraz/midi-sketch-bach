// Tests for vocabulary attestation infrastructure (Phase 3).
// Verifies matchVocabulary(), attestationRate(), and weightedAttestationScore()
// from vocabulary_data.inc, plus interaction with MotifOp transformations.

#include <gtest/gtest.h>

#include <vector>

#include "constraint/motif_constraint.h"
#include "core/basic_types.h"
#include "core/vocabulary_data.inc"

namespace bach {
namespace {

// ---------------------------------------------------------------------------
// matchVocabulary() tests
// ---------------------------------------------------------------------------

TEST(VocabularyAttestationTest, MatchVocabularyKnownPattern) {
  // Descending stepwise: {-1,-1,-1,-1} is the highest-count entry (847).
  // matchVocabulary normalizes by kEpisodeVocab[0].count, so this should be 1.0.
  int8_t pattern[] = {-1, -1, -1, -1};
  float score = vocab_data::matchVocabulary(pattern);
  EXPECT_GT(score, 0.0f);
  EXPECT_LE(score, 1.0f);
  // This is the most common pattern; expect near 1.0.
  EXPECT_GT(score, 0.9f);
}

TEST(VocabularyAttestationTest, MatchVocabularyAscendingScale) {
  // Ascending stepwise: {1,1,1,1} is the 6th entry (count=634).
  int8_t pattern[] = {1, 1, 1, 1};
  float score = vocab_data::matchVocabulary(pattern);
  EXPECT_GT(score, 0.0f);
  EXPECT_LE(score, 1.0f);
  // 634/847 ~ 0.749, should be well above 0.5.
  EXPECT_GT(score, 0.5f);
}

TEST(VocabularyAttestationTest, MatchVocabularyUnknownPattern) {
  // Wild alternating leaps: not in the vocabulary table.
  int8_t pattern[] = {7, -7, 7, -7};
  float score = vocab_data::matchVocabulary(pattern);
  EXPECT_EQ(score, 0.0f);
}

TEST(VocabularyAttestationTest, MatchVocabularyAllEntriesPositive) {
  // Every entry in kEpisodeVocab should match itself with score > 0.
  for (int idx = 0; idx < vocab_data::kEpisodeVocabSize; ++idx) {
    float score = vocab_data::matchVocabulary(vocab_data::kEpisodeVocab[idx].intervals);
    EXPECT_GT(score, 0.0f) << "Entry " << idx << " failed to self-match";
    EXPECT_LE(score, 1.0f) << "Entry " << idx << " exceeded 1.0";
  }
}

// ---------------------------------------------------------------------------
// attestationRate() tests
// ---------------------------------------------------------------------------

TEST(VocabularyAttestationTest, AttestationRateDescendingScale) {
  // C5, B4, A4, G4, F4, E4, D4, C4 — stepwise descent in semitones.
  // Intervals: -1, -2, -2, -2, -1, -2, -2 (semitones).
  // semitoneToDegree maps: -1->-1, -2->-1, so degree intervals are all -1.
  // Windows of 4 degree intervals:
  //   [-1,-1,-1,-1], [-1,-1,-1,-1], [-1,-1,-1,-1], [-1,-1,-1,-1]
  // All 4 windows match the top entry. Expect rate = 1.0.
  uint8_t pitches[] = {72, 71, 69, 67, 65, 64, 62, 60};
  float rate = vocab_data::attestationRate(pitches, 8);
  EXPECT_GT(rate, 0.0f);
  // All windows should match stepwise descent.
  EXPECT_FLOAT_EQ(rate, 1.0f);
}

TEST(VocabularyAttestationTest, AttestationRateAscendingScale) {
  // C4, D4, E4, F4, G4, A4, B4, C5 — stepwise ascent.
  // Intervals: +2, +2, +1, +2, +2, +2, +1 (semitones).
  // semitoneToDegree maps: +1->+1, +2->+1, so degree intervals are all +1.
  // All 4 windows should match {1,1,1,1}. Expect rate = 1.0.
  uint8_t pitches[] = {60, 62, 64, 65, 67, 69, 71, 72};
  float rate = vocab_data::attestationRate(pitches, 8);
  EXPECT_FLOAT_EQ(rate, 1.0f);
}

TEST(VocabularyAttestationTest, AttestationRateTooFewNotes) {
  // Only 4 notes: fewer than the 5-note window minimum.
  uint8_t pitches[] = {60, 62, 64, 67};
  float rate = vocab_data::attestationRate(pitches, 4);
  EXPECT_EQ(rate, 0.0f);
}

TEST(VocabularyAttestationTest, AttestationRateExactlyFiveNotes) {
  // Exactly 5 notes: one window. Stepwise descent should match.
  uint8_t pitches[] = {72, 71, 69, 67, 65};
  float rate = vocab_data::attestationRate(pitches, 5);
  EXPECT_GT(rate, 0.0f);
}

TEST(VocabularyAttestationTest, AttestationRateChromatic) {
  // Chromatic run: C4, C#4, D4, D#4, E4, F4, F#4, G4.
  // All semitone intervals = +1, semitoneToDegree(+1) = +1.
  // Degree intervals: {1,1,1,1} repeated. Should match ascending scale entry.
  uint8_t pitches[] = {60, 61, 62, 63, 64, 65, 66, 67};
  float rate = vocab_data::attestationRate(pitches, 8);
  EXPECT_GT(rate, 0.0f);
}

TEST(VocabularyAttestationTest, AttestationRateWideLeaps) {
  // Wide octave leaps: C4, C5, C4, C5, C4, C5, C4, C5.
  // Semitone intervals alternate +12, -12 -> degree +7, -7.
  // Pattern {7,-7,7,-7} is not in vocabulary. Expect 0.
  uint8_t pitches[] = {60, 72, 60, 72, 60, 72, 60, 72};
  float rate = vocab_data::attestationRate(pitches, 8);
  EXPECT_EQ(rate, 0.0f);
}

// ---------------------------------------------------------------------------
// weightedAttestationScore() tests
// ---------------------------------------------------------------------------

TEST(VocabularyAttestationTest, WeightedScoreHigherForIdiomatic) {
  // Descending scale (all windows match the top entry with score ~1.0).
  uint8_t desc[] = {72, 71, 69, 67, 65, 64, 62, 60};
  float desc_weighted = vocab_data::weightedAttestationScore(desc, 8);

  // Ascending scale (all windows match but with a lower-count entry).
  uint8_t asc[] = {60, 62, 64, 65, 67, 69, 71, 72};
  float asc_weighted = vocab_data::weightedAttestationScore(asc, 8);

  // Both should be positive.
  EXPECT_GT(desc_weighted, 0.0f);
  EXPECT_GT(asc_weighted, 0.0f);

  // Descending scale (count=847) should score higher than ascending (count=634).
  EXPECT_GT(desc_weighted, asc_weighted);
}

TEST(VocabularyAttestationTest, WeightedScoreZeroForUnknown) {
  uint8_t pitches[] = {60, 72, 60, 72, 60, 72, 60, 72};
  float score = vocab_data::weightedAttestationScore(pitches, 8);
  EXPECT_EQ(score, 0.0f);
}

TEST(VocabularyAttestationTest, WeightedScoreTooFewNotes) {
  uint8_t pitches[] = {60, 62, 64};
  float score = vocab_data::weightedAttestationScore(pitches, 3);
  EXPECT_EQ(score, 0.0f);
}

// ---------------------------------------------------------------------------
// semitoneToDegree() tests
// ---------------------------------------------------------------------------

TEST(VocabularyAttestationTest, SemitoneToDegreeMapping) {
  // Verify key semitone-to-degree conversions used by attestationRate.
  EXPECT_EQ(vocab_data::semitoneToDegree(0), 0);    // unison
  EXPECT_EQ(vocab_data::semitoneToDegree(1), 1);     // m2 -> step
  EXPECT_EQ(vocab_data::semitoneToDegree(2), 1);     // M2 -> step
  EXPECT_EQ(vocab_data::semitoneToDegree(3), 2);     // m3 -> third
  EXPECT_EQ(vocab_data::semitoneToDegree(4), 2);     // M3 -> third
  EXPECT_EQ(vocab_data::semitoneToDegree(5), 3);     // P4
  EXPECT_EQ(vocab_data::semitoneToDegree(7), 4);     // P5
  EXPECT_EQ(vocab_data::semitoneToDegree(12), 7);    // octave
  EXPECT_EQ(vocab_data::semitoneToDegree(-1), -1);   // descending m2
  EXPECT_EQ(vocab_data::semitoneToDegree(-2), -1);   // descending M2
  EXPECT_EQ(vocab_data::semitoneToDegree(-7), -4);   // descending P5
  EXPECT_EQ(vocab_data::semitoneToDegree(-12), -7);  // descending octave
}

// ---------------------------------------------------------------------------
// MotifOp -> attestation interaction tests
// ---------------------------------------------------------------------------

TEST(VocabularyAttestationTest, AttestationRateFromMotifOp) {
  // Create a simple C major ascending scale motif: C4, D4, E4, F4, G4, A4, B4, C5.
  // Eight notes give 4 windows — enough to measure meaningful attestation.
  std::vector<NoteEvent> motif;
  uint8_t scale_pitches[] = {60, 62, 64, 65, 67, 69, 71, 72};
  for (int idx = 0; idx < 8; ++idx) {
    NoteEvent n;
    n.pitch = scale_pitches[idx];
    n.start_tick = static_cast<Tick>(idx) * kTicksPerBeat;
    n.duration = kTicksPerBeat;
    n.velocity = 80;
    n.voice = 0;
    motif.push_back(n);
  }

  int ops_with_attestation = 0;
  MotifOp ops[] = {MotifOp::Original, MotifOp::Invert,
                   MotifOp::Retrograde, MotifOp::Diminish};

  for (MotifOp op : ops) {
    auto transformed = applyMotifOp(motif, op, Key::C, ScaleType::Major, -1);
    if (transformed.size() >= 5) {
      std::vector<uint8_t> pitches;
      pitches.reserve(transformed.size());
      for (const auto& n : transformed) {
        pitches.push_back(n.pitch);
      }
      float rate = vocab_data::attestationRate(
          pitches.data(), static_cast<int>(pitches.size()));
      if (rate > 0.0f) {
        ++ops_with_attestation;
      }
    }
  }

  // At least 2 of 4 ops should produce some attestation, confirming the
  // vocabulary captures common transforms of scale-based material.
  EXPECT_GE(ops_with_attestation, 2);
}

TEST(VocabularyAttestationTest, OriginalMotifPreservesAttestation) {
  // Original op is identity; a scale motif that attests should still attest.
  std::vector<NoteEvent> motif;
  uint8_t scale_pitches[] = {72, 71, 69, 67, 65};
  for (int idx = 0; idx < 5; ++idx) {
    NoteEvent n;
    n.pitch = scale_pitches[idx];
    n.start_tick = static_cast<Tick>(idx) * kTicksPerBeat;
    n.duration = kTicksPerBeat;
    n.velocity = 80;
    n.voice = 0;
    motif.push_back(n);
  }

  auto transformed = applyMotifOp(motif, MotifOp::Original, Key::C, ScaleType::Major, -1);
  ASSERT_GE(transformed.size(), 5u);

  std::vector<uint8_t> pitches;
  for (const auto& n : transformed) {
    pitches.push_back(n.pitch);
  }
  float rate = vocab_data::attestationRate(pitches.data(), static_cast<int>(pitches.size()));
  EXPECT_GT(rate, 0.0f);
}

// ---------------------------------------------------------------------------
// Vocabulary table integrity
// ---------------------------------------------------------------------------

TEST(VocabularyAttestationTest, VocabTableSizeConsistent) {
  EXPECT_EQ(vocab_data::kEpisodeVocabSize, 48);
}

TEST(VocabularyAttestationTest, VocabEntriesHavePositiveCounts) {
  for (int idx = 0; idx < vocab_data::kEpisodeVocabSize; ++idx) {
    EXPECT_GT(vocab_data::kEpisodeVocab[idx].count, 0)
        << "Entry " << idx << " has zero count";
  }
}

TEST(VocabularyAttestationTest, DuplicatePatternReturnsFirstMatch) {
  // kEpisodeVocab has a duplicate pattern {-1, 2, -1, -1} at indices 16 and 33.
  // matchVocabulary scans linearly; verify it returns the first (higher count).
  int8_t pattern[] = {-1, 2, -1, -1};
  float score = vocab_data::matchVocabulary(pattern);
  // First occurrence (index 16, count=198) normalizes to 198/847 ~ 0.234.
  float expected = 198.0f / 847.0f;
  EXPECT_NEAR(score, expected, 0.01f);
}

}  // namespace
}  // namespace bach
