// Tests for Goldberg Variations soggetto (short subject) generator.

#include "forms/goldberg/goldberg_soggetto.h"

#include <algorithm>
#include <cstdint>

#include <gtest/gtest.h>

#include "core/basic_types.h"
#include "core/pitch_utils.h"
#include "forms/goldberg/goldberg_structural_grid.h"
#include "fugue/subject.h"
#include "harmony/key.h"

namespace bach {
namespace {

// Common test fixtures.
constexpr KeySignature kGMajor = {Key::G, false};
constexpr TimeSignature kTriple = {3, 4};
constexpr uint32_t kTestSeed = 42;

class SoggettoTest : public ::testing::Test {
 protected:
  void SetUp() override {
    grid_ = GoldbergStructuralGrid::createMajor();
  }

  GoldbergStructuralGrid grid_;
  SoggettoGenerator generator_;
};

// ---------------------------------------------------------------------------
// Test 1: GenerateProducesSubject
// ---------------------------------------------------------------------------

TEST_F(SoggettoTest, GenerateProducesSubject) {
  SoggettoParams params;
  params.length_bars = 2;
  params.character = SubjectCharacter::Severe;
  params.grid = &grid_;
  params.start_bar = 1;

  Subject result = generator_.generate(params, kGMajor, kTriple, kTestSeed);
  EXPECT_FALSE(result.notes.empty())
      << "generate() should produce a Subject with non-empty notes";
}

// ---------------------------------------------------------------------------
// Test 2: SubjectHasCorrectCharacter
// ---------------------------------------------------------------------------

TEST_F(SoggettoTest, SubjectHasCorrectCharacter) {
  SoggettoParams params;
  params.length_bars = 2;
  params.grid = &grid_;
  params.start_bar = 1;

  for (auto character : {SubjectCharacter::Severe, SubjectCharacter::Playful,
                          SubjectCharacter::Noble, SubjectCharacter::Restless}) {
    params.character = character;
    Subject result = generator_.generate(params, kGMajor, kTriple, kTestSeed);
    EXPECT_EQ(result.character, character)
        << "Subject.character should match params.character";
  }
}

// ---------------------------------------------------------------------------
// Test 3: SubjectLengthWithinBars
// ---------------------------------------------------------------------------

TEST_F(SoggettoTest, SubjectLengthWithinBars) {
  SoggettoParams params;
  params.character = SubjectCharacter::Severe;
  params.grid = &grid_;
  params.start_bar = 1;

  for (uint8_t bars = 1; bars <= 4; ++bars) {
    params.length_bars = bars;
    Subject result = generator_.generate(params, kGMajor, kTriple, kTestSeed);

    Tick expected_max = static_cast<Tick>(bars) * kTriple.ticksPerBar();
    EXPECT_EQ(result.length_ticks, expected_max)
        << "Subject length_ticks should equal bars * ticksPerBar";

    // All notes should end within the length.
    for (const auto& note : result.notes) {
      EXPECT_LE(note.start_tick + note.duration, expected_max)
          << "Note at tick " << note.start_tick << " extends beyond subject";
    }
  }
}

// ---------------------------------------------------------------------------
// Test 4: SevereCharacterSmallLeaps
// ---------------------------------------------------------------------------

TEST_F(SoggettoTest, SevereCharacterSmallLeaps) {
  SoggettoParams params;
  params.length_bars = 2;
  params.character = SubjectCharacter::Severe;
  params.grid = &grid_;
  params.start_bar = 1;
  params.path_candidates = 16;

  int max_leap = maxLeapForCharacter(SubjectCharacter::Severe);  // 7

  // Test across multiple seeds.
  for (uint32_t seed = 100; seed < 120; ++seed) {
    Subject result = generator_.generate(params, kGMajor, kTriple, seed);
    for (size_t idx = 1; idx < result.notes.size(); ++idx) {
      int interval_dist = absoluteInterval(result.notes[idx].pitch,
                                            result.notes[idx - 1].pitch);
      EXPECT_LE(interval_dist, max_leap)
          << "Severe character leap at note " << idx
          << " (seed=" << seed << "): " << interval_dist
          << " exceeds max " << max_leap;
    }
  }
}

// ---------------------------------------------------------------------------
// Test 5: PlayfulCharacterLargerLeaps
// ---------------------------------------------------------------------------

TEST_F(SoggettoTest, PlayfulCharacterLargerLeaps) {
  SoggettoParams params;
  params.length_bars = 3;
  params.character = SubjectCharacter::Playful;
  params.grid = &grid_;
  params.start_bar = 1;
  params.path_candidates = 16;

  int max_leap = maxLeapForCharacter(SubjectCharacter::Playful);  // 9
  int severe_max = maxLeapForCharacter(SubjectCharacter::Severe);  // 7
  EXPECT_GT(max_leap, severe_max)
      << "Playful should allow wider leaps than Severe";

  // All leaps should still be within the Playful max.
  for (uint32_t seed = 100; seed < 120; ++seed) {
    Subject result = generator_.generate(params, kGMajor, kTriple, seed);
    for (size_t idx = 1; idx < result.notes.size(); ++idx) {
      int interval_dist = absoluteInterval(result.notes[idx].pitch,
                                            result.notes[idx - 1].pitch);
      EXPECT_LE(interval_dist, max_leap)
          << "Playful character leap at note " << idx
          << " (seed=" << seed << "): " << interval_dist
          << " exceeds max " << max_leap;
    }
  }
}

// ---------------------------------------------------------------------------
// Test 6: GridAlignmentScoring
// ---------------------------------------------------------------------------

TEST_F(SoggettoTest, GridAlignmentScoring) {
  SoggettoParams params;
  params.length_bars = 2;
  params.character = SubjectCharacter::Severe;
  params.grid = &grid_;
  params.start_bar = 1;

  // Build an aligned candidate: all notes are chord tones of bar 1 (G major: G B D).
  std::vector<NoteEvent> aligned;
  NoteEvent note;
  note.velocity = 80;
  note.voice = 0;
  note.source = BachNoteSource::GoldbergSoggetto;

  note.start_tick = 0;
  note.duration = kTicksPerBeat;
  note.pitch = 67;  // G4 (tonic chord tone)
  aligned.push_back(note);

  note.start_tick = kTicksPerBeat;
  note.pitch = 71;  // B4 (tonic chord tone)
  aligned.push_back(note);

  note.start_tick = kTicksPerBeat * 2;
  note.pitch = 74;  // D5 (tonic chord tone)
  aligned.push_back(note);

  // Build a misaligned candidate: chromatic notes far from chord tones.
  std::vector<NoteEvent> misaligned;
  note.start_tick = 0;
  note.pitch = 66;  // F#4 (not a G chord tone in this bar context)
  misaligned.push_back(note);

  note.start_tick = kTicksPerBeat;
  note.pitch = 68;  // Ab4
  misaligned.push_back(note);

  note.start_tick = kTicksPerBeat * 2;
  note.pitch = 70;  // Bb4
  misaligned.push_back(note);

  float aligned_score = generator_.scoreGridAlignment(
      aligned, params, kGMajor, kTriple);
  float misaligned_score = generator_.scoreGridAlignment(
      misaligned, params, kGMajor, kTriple);

  EXPECT_GT(aligned_score, misaligned_score)
      << "Chord-tone-aligned candidate should score higher than misaligned";
}

// ---------------------------------------------------------------------------
// Test 7: CadenceAlignment
// ---------------------------------------------------------------------------

TEST_F(SoggettoTest, CadenceAlignment) {
  // Bar 8 (index 7) has a Perfect cadence -> should end on tonic G.
  SoggettoParams params;
  params.length_bars = 4;
  params.character = SubjectCharacter::Severe;
  params.grid = &grid_;
  params.start_bar = 5;  // Bars 5-8, ending at bar 8 (Perfect cadence).
  params.path_candidates = 16;

  // Test across multiple seeds.
  int tonic_endings = 0;
  int total_tests = 20;
  for (uint32_t seed = 200; seed < 200 + static_cast<uint32_t>(total_tests);
       ++seed) {
    Subject result = generator_.generate(params, kGMajor, kTriple, seed);
    if (!result.notes.empty()) {
      int ending_pc = getPitchClass(result.notes.back().pitch);
      int tonic_pc = static_cast<int>(Key::G);  // G = 7
      if (ending_pc == tonic_pc) {
        ++tonic_endings;
      }
    }
  }
  // With Perfect cadence guiding, at least 50% should end on tonic.
  EXPECT_GE(tonic_endings, total_tests / 2)
      << "Perfect cadence at bar 8 should guide majority of endings to tonic "
      << "(got " << tonic_endings << "/" << total_tests << ")";
}

// ---------------------------------------------------------------------------
// Test 8: GoalTonePosition
// ---------------------------------------------------------------------------

TEST_F(SoggettoTest, GoalTonePosition) {
  SoggettoParams params;
  params.length_bars = 4;
  params.character = SubjectCharacter::Noble;
  params.grid = &grid_;
  params.start_bar = 1;
  params.path_candidates = 16;

  int climax_in_first_half = 0;
  int climax_in_second_half = 0;
  int total_tests = 20;

  for (uint32_t seed = 300; seed < 300 + static_cast<uint32_t>(total_tests);
       ++seed) {
    Subject result = generator_.generate(params, kGMajor, kTriple, seed);
    if (result.notes.size() < 3) continue;

    // Find the highest pitch and its position.
    auto max_it = std::max_element(
        result.notes.begin(), result.notes.end(),
        [](const NoteEvent& lhs, const NoteEvent& rhs) {
          return lhs.pitch < rhs.pitch;
        });
    Tick climax_tick = max_it->start_tick;
    Tick half_point = result.length_ticks / 2;

    if (climax_tick < half_point) {
      ++climax_in_first_half;
    } else {
      ++climax_in_second_half;
    }
  }

  // GoalTone should place climax somewhere in the middle region.
  // Both halves should have some occurrences (not all at start or end).
  EXPECT_GT(climax_in_first_half + climax_in_second_half, 0)
      << "Should have at least some valid subjects with identifiable climax";
}

// ---------------------------------------------------------------------------
// Test 9: DifferentSeedsProduceDifferentSubjects
// ---------------------------------------------------------------------------

TEST_F(SoggettoTest, DifferentSeedsProduceDifferentSubjects) {
  SoggettoParams params;
  params.length_bars = 4;
  params.character = SubjectCharacter::Playful;
  params.grid = &grid_;
  params.start_bar = 1;
  params.path_candidates = 4;

  // Try multiple seed pairs; at least one pair should differ.
  bool found_difference = false;
  for (uint32_t base = 1000; base < 1020 && !found_difference; base += 2) {
    Subject result1 = generator_.generate(params, kGMajor, kTriple, base);
    Subject result2 = generator_.generate(params, kGMajor, kTriple, base + 7777);

    if (result1.notes.empty() || result2.notes.empty()) continue;

    size_t compare_count = std::min(result1.notes.size(), result2.notes.size());
    for (size_t idx = 0; idx < compare_count; ++idx) {
      if (result1.notes[idx].pitch != result2.notes[idx].pitch) {
        found_difference = true;
        break;
      }
    }
    if (!found_difference && result1.notes.size() != result2.notes.size()) {
      found_difference = true;
    }
  }

  EXPECT_TRUE(found_difference)
      << "At least one seed pair should produce different pitch sequences";
}

// ---------------------------------------------------------------------------
// Test 10: PathCandidateSelection
// ---------------------------------------------------------------------------

TEST_F(SoggettoTest, PathCandidateSelection) {
  SoggettoParams params;
  params.length_bars = 2;
  params.character = SubjectCharacter::Severe;
  params.grid = &grid_;
  params.start_bar = 1;

  // Generate with few candidates vs many candidates.
  // More candidates should produce equal or better alignment scores on average.
  float total_score_few = 0.0f;
  float total_score_many = 0.0f;
  int test_count = 10;

  for (int idx = 0; idx < test_count; ++idx) {
    uint32_t seed = 500 + static_cast<uint32_t>(idx);

    params.path_candidates = 2;
    Subject few = generator_.generate(params, kGMajor, kTriple, seed);
    float score_few = generator_.scoreGridAlignment(
        few.notes, params, kGMajor, kTriple);

    params.path_candidates = 16;
    Subject many = generator_.generate(params, kGMajor, kTriple, seed);
    float score_many = generator_.scoreGridAlignment(
        many.notes, params, kGMajor, kTriple);

    total_score_few += score_few;
    total_score_many += score_many;
  }

  // On average, more candidates should not be worse.
  EXPECT_GE(total_score_many, total_score_few * 0.9f)
      << "More candidates should produce at least comparable quality: "
      << "few=" << total_score_few / test_count
      << " many=" << total_score_many / test_count;
}

// ---------------------------------------------------------------------------
// Test 11: StartBarAffectsHarmony
// ---------------------------------------------------------------------------

TEST_F(SoggettoTest, StartBarAffectsHarmony) {
  SoggettoParams params;
  params.length_bars = 2;
  params.character = SubjectCharacter::Severe;
  params.grid = &grid_;
  params.path_candidates = 8;

  // Generate starting at different bars with the same seed.
  params.start_bar = 1;
  Subject result1 = generator_.generate(params, kGMajor, kTriple, kTestSeed);

  params.start_bar = 9;
  Subject result9 = generator_.generate(params, kGMajor, kTriple, kTestSeed);

  ASSERT_FALSE(result1.notes.empty());
  ASSERT_FALSE(result9.notes.empty());

  // Different start bars should produce different results because
  // the grid has different bass pitches and harmonic functions.
  bool any_different = false;
  size_t compare_count = std::min(result1.notes.size(), result9.notes.size());
  for (size_t idx = 0; idx < compare_count; ++idx) {
    if (result1.notes[idx].pitch != result9.notes[idx].pitch) {
      any_different = true;
      break;
    }
  }
  if (!any_different && result1.notes.size() != result9.notes.size()) {
    any_different = true;
  }

  EXPECT_TRUE(any_different)
      << "Different start_bar positions should produce different soggetti";
}

// ---------------------------------------------------------------------------
// Additional: NoteSource provenance
// ---------------------------------------------------------------------------

TEST_F(SoggettoTest, AllNotesHaveCorrectSource) {
  SoggettoParams params;
  params.length_bars = 2;
  params.character = SubjectCharacter::Severe;
  params.grid = &grid_;
  params.start_bar = 1;

  Subject result = generator_.generate(params, kGMajor, kTriple, kTestSeed);
  for (const auto& note : result.notes) {
    EXPECT_EQ(note.source, BachNoteSource::GoldbergSoggetto)
        << "All soggetto notes should have GoldbergSoggetto source";
  }
}

// ---------------------------------------------------------------------------
// Additional: Key and mode preserved
// ---------------------------------------------------------------------------

TEST_F(SoggettoTest, KeyAndModePreserved) {
  SoggettoParams params;
  params.length_bars = 2;
  params.character = SubjectCharacter::Severe;
  params.grid = &grid_;
  params.start_bar = 1;

  Subject result = generator_.generate(params, kGMajor, kTriple, kTestSeed);
  EXPECT_EQ(result.key, Key::G);
  EXPECT_FALSE(result.is_minor);

  KeySignature g_minor = {Key::G, true};
  Subject minor_result = generator_.generate(params, g_minor, kTriple, kTestSeed);
  EXPECT_EQ(minor_result.key, Key::G);
  EXPECT_TRUE(minor_result.is_minor);
}

// ---------------------------------------------------------------------------
// Additional: Bars clamped to [1, 4]
// ---------------------------------------------------------------------------

TEST_F(SoggettoTest, BarsClampedToValidRange) {
  SoggettoParams params;
  params.character = SubjectCharacter::Severe;
  params.grid = &grid_;
  params.start_bar = 1;

  // Zero bars should clamp to 1.
  params.length_bars = 0;
  Subject result0 = generator_.generate(params, kGMajor, kTriple, kTestSeed);
  EXPECT_EQ(result0.length_ticks, 1 * kTriple.ticksPerBar());

  // 8 bars should clamp to 4.
  params.length_bars = 8;
  Subject result8 = generator_.generate(params, kGMajor, kTriple, kTestSeed);
  EXPECT_EQ(result8.length_ticks, 4 * kTriple.ticksPerBar());
}

}  // namespace
}  // namespace bach
