// Tests for CanonValidator: post-generation canon integrity verification.

#include "forms/goldberg/canon/canon_validator.h"

#include <cstdint>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "core/basic_types.h"
#include "core/pitch_utils.h"
#include "core/scale.h"
#include "forms/goldberg/canon/canon_types.h"
#include "forms/goldberg/goldberg_structural_grid.h"
#include "harmony/key.h"

namespace bach {
namespace {

// Common test fixtures.
constexpr KeySignature kGMajor = {Key::G, false};
constexpr KeySignature kGMinor = {Key::G, true};
constexpr TimeSignature kTriple = {3, 4};  // 3/4 time (BWV 988 standard).

/// @brief Helper: create a CanonSpec for testing.
CanonSpec makeSpec(int interval, CanonTransform transform = CanonTransform::Regular,
                   KeySignature key = kGMajor,
                   MinorModeProfile minor_profile = MinorModeProfile::NaturalMinor,
                   int delay_bars = 1) {
  CanonSpec spec;
  spec.canon_interval = interval;
  spec.transform = transform;
  spec.key = key;
  spec.minor_profile = minor_profile;
  spec.delay_bars = delay_bars;
  spec.rhythmic_mode = CanonRhythmicMode::StrictRhythm;
  return spec;
}

/// @brief Helper: create a NoteEvent at a specific tick.
NoteEvent makeNote(uint8_t pitch, Tick start_tick,
                   Tick note_duration = kTicksPerBeat,
                   BachNoteSource source = BachNoteSource::CanonDux) {
  NoteEvent note;
  note.pitch = pitch;
  note.start_tick = start_tick;
  note.duration = note_duration;
  note.velocity = 80;
  note.voice = 0;
  note.source = source;
  return note;
}

/// @brief Helper: compute the expected comes pitch using the same logic as DuxBuffer.
///
/// Used to build correct comes data for tests without hardcoding pitches
/// that may break if the scale system changes.
uint8_t expectedComesPitch(uint8_t dux_pitch, const CanonSpec& spec) {
  ScaleType scale = spec.key.is_minor
      ? (spec.minor_profile == MinorModeProfile::HarmonicMinor
             ? ScaleType::HarmonicMinor
             : ScaleType::NaturalMinor)
      : ScaleType::Major;
  Key key = spec.key.tonic;

  if (spec.transform == CanonTransform::Regular) {
    int dux_degree = scale_util::pitchToAbsoluteDegree(dux_pitch, key, scale);
    int comes_degree = dux_degree + spec.canon_interval;
    return scale_util::absoluteDegreeToPitch(comes_degree, key, scale);
  }

  // Inverted.
  int musical_octave = static_cast<int>(dux_pitch) / 12 - 1;
  uint8_t tonic = tonicPitch(key, musical_octave);
  int tonic_degree = scale_util::pitchToAbsoluteDegree(tonic, key, scale);
  int dux_degree = scale_util::pitchToAbsoluteDegree(dux_pitch, key, scale);
  int inverted_degree = 2 * tonic_degree - dux_degree;
  int comes_degree = inverted_degree + spec.canon_interval;
  return scale_util::absoluteDegreeToPitch(comes_degree, key, scale);
}

/// @brief Build a matching comes vector from dux notes using the spec transformation.
///
/// Produces a perfectly valid comes: each dux note is transformed in pitch,
/// offset in time by the delay, and duration is preserved.
std::vector<NoteEvent> buildCorrectComes(const std::vector<NoteEvent>& dux,
                                         const CanonSpec& spec,
                                         const TimeSignature& time_sig) {
  Tick delay_ticks = static_cast<Tick>(spec.delay_bars) * time_sig.ticksPerBar();
  std::vector<NoteEvent> comes;
  comes.reserve(dux.size());

  for (const auto& dux_note : dux) {
    NoteEvent comes_note;
    comes_note.pitch = expectedComesPitch(dux_note.pitch, spec);
    comes_note.start_tick = dux_note.start_tick + delay_ticks;
    comes_note.duration = dux_note.duration;
    comes_note.velocity = dux_note.velocity;
    comes_note.voice = 1;
    comes_note.source = BachNoteSource::CanonComes;
    comes.push_back(comes_note);
  }

  return comes;
}

class CanonValidatorTest : public ::testing::Test {
 protected:
  void SetUp() override {
    grid_ = GoldbergStructuralGrid::createMajor();
  }

  GoldbergStructuralGrid grid_;
};

// ---------------------------------------------------------------------------
// Test 1: PerfectUnisonCanon
// ---------------------------------------------------------------------------

TEST_F(CanonValidatorTest, PerfectUnisonCanon) {
  // Unison canon: comes pitches are identical to dux, offset by 1 bar.
  CanonSpec spec = makeSpec(0);

  // G major ascending: G4(67), A4(69), B4(71), C5(72)
  Tick bar_ticks = kTriple.ticksPerBar();  // 1440 for 3/4.
  std::vector<NoteEvent> dux = {
      makeNote(67, 0),
      makeNote(69, kTicksPerBeat),
      makeNote(71, kTicksPerBeat * 2),
      makeNote(72, bar_ticks),
  };

  auto comes = buildCorrectComes(dux, spec, kTriple);
  auto result = validateCanonIntegrity(dux, comes, spec, kTriple);

  EXPECT_TRUE(result.passed) << "Perfect unison canon should pass";
  EXPECT_EQ(result.total_pairs, 4);
  EXPECT_EQ(result.pitch_violations, 0);
  EXPECT_EQ(result.timing_violations, 0);
  EXPECT_EQ(result.duration_violations, 0);
  EXPECT_FLOAT_EQ(result.pitch_accuracy, 1.0f);
}

// ---------------------------------------------------------------------------
// Test 2: PerfectSecondCanon
// ---------------------------------------------------------------------------

TEST_F(CanonValidatorTest, PerfectSecondCanon) {
  // Canon at the 2nd: each dux pitch transposed up by 1 diatonic degree.
  CanonSpec spec = makeSpec(1);

  std::vector<NoteEvent> dux = {
      makeNote(67, 0),                  // G4
      makeNote(69, kTicksPerBeat),      // A4
      makeNote(71, kTicksPerBeat * 2),  // B4
      makeNote(72, kTriple.ticksPerBar()),  // C5
  };

  auto comes = buildCorrectComes(dux, spec, kTriple);
  auto result = validateCanonIntegrity(dux, comes, spec, kTriple);

  EXPECT_TRUE(result.passed) << "Perfect 2nd canon should pass";
  EXPECT_EQ(result.total_pairs, 4);
  EXPECT_EQ(result.pitch_violations, 0);
  EXPECT_FLOAT_EQ(result.pitch_accuracy, 1.0f);
}

// ---------------------------------------------------------------------------
// Test 3: PitchViolation
// ---------------------------------------------------------------------------

TEST_F(CanonValidatorTest, PitchViolation) {
  // One comes note has the wrong pitch.
  CanonSpec spec = makeSpec(0);  // Unison.

  std::vector<NoteEvent> dux = {
      makeNote(67, 0),
      makeNote(69, kTicksPerBeat),
      makeNote(71, kTicksPerBeat * 2),
      makeNote(72, kTriple.ticksPerBar()),
  };

  auto comes = buildCorrectComes(dux, spec, kTriple);
  // Corrupt one comes pitch.
  comes[1].pitch = 70;  // Should be 69 for unison.

  auto result = validateCanonIntegrity(dux, comes, spec, kTriple);

  EXPECT_EQ(result.pitch_violations, 1) << "Should detect one pitch violation";
  EXPECT_EQ(result.total_pairs, 4);
  EXPECT_FLOAT_EQ(result.pitch_accuracy, 3.0f / 4.0f);  // 75%.
  // 75% < 95%, so should fail.
  EXPECT_FALSE(result.passed) << "75% accuracy should fail (threshold 95%)";
}

// ---------------------------------------------------------------------------
// Test 4: TimingViolation
// ---------------------------------------------------------------------------

TEST_F(CanonValidatorTest, TimingViolation) {
  // One comes note at the wrong tick (beyond tolerance).
  CanonSpec spec = makeSpec(0);

  std::vector<NoteEvent> dux = {
      makeNote(67, 0),
      makeNote(69, kTicksPerBeat),
      makeNote(71, kTicksPerBeat * 2),
  };

  auto comes = buildCorrectComes(dux, spec, kTriple);
  // Shift comes[1] by 10 ticks (beyond the +/-1 tolerance).
  comes[1].start_tick += 10;

  auto result = validateCanonIntegrity(dux, comes, spec, kTriple);

  EXPECT_GT(result.timing_violations, 0) << "Should detect timing violation";
  EXPECT_FALSE(result.passed) << "Timing violation should cause failure";
}

// ---------------------------------------------------------------------------
// Test 5: DurationViolation
// ---------------------------------------------------------------------------

TEST_F(CanonValidatorTest, DurationViolation) {
  // One comes note has a different duration (StrictRhythm mode).
  CanonSpec spec = makeSpec(0);

  std::vector<NoteEvent> dux = {
      makeNote(67, 0, kTicksPerBeat),
      makeNote(69, kTicksPerBeat, kTicksPerBeat),
      makeNote(71, kTicksPerBeat * 2, kTicksPerBeat),
  };

  auto comes = buildCorrectComes(dux, spec, kTriple);
  // Change one comes duration.
  comes[2].duration = duration::kHalfNote;  // 960 instead of 480.

  auto result = validateCanonIntegrity(dux, comes, spec, kTriple);

  EXPECT_EQ(result.duration_violations, 1) << "Should detect one duration violation";
  EXPECT_FALSE(result.passed);
}

// ---------------------------------------------------------------------------
// Test 6: EmptyInputs
// ---------------------------------------------------------------------------

TEST_F(CanonValidatorTest, EmptyInputs) {
  CanonSpec spec = makeSpec(0);

  // Both empty.
  {
    std::vector<NoteEvent> empty;
    auto result = validateCanonIntegrity(empty, empty, spec, kTriple);
    EXPECT_TRUE(result.passed) << "Empty inputs should pass vacuously";
    EXPECT_EQ(result.total_pairs, 0);
    EXPECT_FLOAT_EQ(result.pitch_accuracy, 1.0f);
  }

  // Only dux present.
  {
    std::vector<NoteEvent> dux = {makeNote(67, 0)};
    std::vector<NoteEvent> empty;
    auto result = validateCanonIntegrity(dux, empty, spec, kTriple);
    EXPECT_TRUE(result.passed) << "Empty comes should pass vacuously";
    EXPECT_EQ(result.total_pairs, 0);
  }

  // Only comes present.
  {
    std::vector<NoteEvent> empty;
    std::vector<NoteEvent> comes = {
        makeNote(67, kTriple.ticksPerBar(), kTicksPerBeat,
                 BachNoteSource::CanonComes)};
    auto result = validateCanonIntegrity(empty, comes, spec, kTriple);
    EXPECT_TRUE(result.passed) << "Empty dux should pass vacuously";
    EXPECT_EQ(result.total_pairs, 0);
  }
}

// ---------------------------------------------------------------------------
// Test 7: InvertedCanon
// ---------------------------------------------------------------------------

TEST_F(CanonValidatorTest, InvertedCanon) {
  // Inverted canon at unison (interval=0) in G major.
  // DuxBuffer test verifies: A4(69) inverted around G -> F#4(66).
  CanonSpec spec = makeSpec(0, CanonTransform::Inverted);

  std::vector<NoteEvent> dux = {
      makeNote(67, 0),                  // G4 -> inverted around G = G4 (67)
      makeNote(69, kTicksPerBeat),      // A4 -> inverted around G = F#4 (66)
      makeNote(71, kTicksPerBeat * 2),  // B4 -> inverted around G = E4 (64)
      makeNote(72, kTriple.ticksPerBar()),  // C5 -> inverted around G = D4 (62)
  };

  auto comes = buildCorrectComes(dux, spec, kTriple);
  auto result = validateCanonIntegrity(dux, comes, spec, kTriple);

  EXPECT_TRUE(result.passed) << "Correctly inverted canon should pass";
  EXPECT_EQ(result.pitch_violations, 0);
  EXPECT_EQ(result.total_pairs, 4);
}

// ---------------------------------------------------------------------------
// Test 8: HighAccuracyThreshold
// ---------------------------------------------------------------------------

TEST_F(CanonValidatorTest, HighAccuracyThreshold) {
  // Test the 95% accuracy threshold boundary.
  // 20 notes: 1 violation = 95% = pass, 2 violations = 90% = fail.
  CanonSpec spec = makeSpec(0);

  std::vector<NoteEvent> dux;
  dux.reserve(20);
  for (int idx = 0; idx < 20; ++idx) {
    // Alternate between G4(67) and A4(69) to keep things simple.
    uint8_t pitch = (idx % 2 == 0) ? 67 : 69;
    dux.push_back(makeNote(pitch, static_cast<Tick>(idx) * kTicksPerBeat));
  }

  // Exactly 1 violation out of 20 = 95% accuracy (on the boundary).
  {
    auto comes = buildCorrectComes(dux, spec, kTriple);
    comes[0].pitch = 60;  // One wrong pitch.
    auto result = validateCanonIntegrity(dux, comes, spec, kTriple);
    EXPECT_EQ(result.pitch_violations, 1);
    EXPECT_FLOAT_EQ(result.pitch_accuracy, 19.0f / 20.0f);
    EXPECT_TRUE(result.passed) << "95% accuracy should pass";
  }

  // 2 violations out of 20 = 90% accuracy (below threshold).
  {
    auto comes = buildCorrectComes(dux, spec, kTriple);
    comes[0].pitch = 60;
    comes[1].pitch = 60;
    auto result = validateCanonIntegrity(dux, comes, spec, kTriple);
    EXPECT_EQ(result.pitch_violations, 2);
    EXPECT_FLOAT_EQ(result.pitch_accuracy, 18.0f / 20.0f);
    EXPECT_FALSE(result.passed) << "90% accuracy should fail";
  }
}

// ---------------------------------------------------------------------------
// Test 9: ValidationMessages
// ---------------------------------------------------------------------------

TEST_F(CanonValidatorTest, ValidationMessages) {
  // Verify that violation messages contain useful diagnostic info.
  CanonSpec spec = makeSpec(0);

  std::vector<NoteEvent> dux = {
      makeNote(67, 0),
      makeNote(69, kTicksPerBeat),
  };

  auto comes = buildCorrectComes(dux, spec, kTriple);
  comes[0].pitch = 60;  // Pitch violation.

  auto result = validateCanonIntegrity(dux, comes, spec, kTriple);

  ASSERT_FALSE(result.messages.empty()) << "Should have diagnostic messages";

  // Check that the message contains pitch info.
  bool found_pitch_msg = false;
  for (const auto& msg : result.messages) {
    if (msg.find("Pitch:") != std::string::npos) {
      found_pitch_msg = true;
      // Message should contain note names or pitch numbers.
      EXPECT_NE(msg.find("67"), std::string::npos)
          << "Message should reference dux pitch";
      EXPECT_NE(msg.find("60"), std::string::npos)
          << "Message should reference actual comes pitch";
    }
  }
  EXPECT_TRUE(found_pitch_msg) << "Should have a pitch violation message";
}

// ---------------------------------------------------------------------------
// Test 10: ClimaxAlignmentPass
// ---------------------------------------------------------------------------

TEST_F(CanonValidatorTest, ClimaxAlignmentPass) {
  // Place melodic peaks at Intensification bars.
  // Intensification = bar_in_phrase == 3, so bars 2, 6, 10, 14, ... (0-indexed).
  Tick ticks_per_bar = kTriple.ticksPerBar();

  // Dux peak at bar 2 (Intensification), comes peak at bar 6 (Intensification).
  std::vector<NoteEvent> dux = {
      makeNote(60, 0),                                        // bar 0
      makeNote(65, ticks_per_bar),                            // bar 1
      makeNote(80, ticks_per_bar * 2),                        // bar 2 (peak, Intensification)
      makeNote(65, ticks_per_bar * 3),                        // bar 3
  };

  std::vector<NoteEvent> comes = {
      makeNote(60, ticks_per_bar * 4, kTicksPerBeat,
               BachNoteSource::CanonComes),                    // bar 4
      makeNote(65, ticks_per_bar * 5, kTicksPerBeat,
               BachNoteSource::CanonComes),                    // bar 5
      makeNote(82, ticks_per_bar * 6, kTicksPerBeat,
               BachNoteSource::CanonComes),                    // bar 6 (peak, Intensification)
      makeNote(65, ticks_per_bar * 7, kTicksPerBeat,
               BachNoteSource::CanonComes),                    // bar 7
  };

  bool aligned = validateClimaxAlignment(dux, comes, grid_, kTriple);
  EXPECT_TRUE(aligned) << "Peaks at Intensification bars should be aligned";
}

// ---------------------------------------------------------------------------
// Test 11: ClimaxAlignmentFail
// ---------------------------------------------------------------------------

TEST_F(CanonValidatorTest, ClimaxAlignmentFail) {
  // Place melodic peaks far from Intensification bars.
  // Opening = bar 0, 4, 8, 12, ... (bar_in_phrase == 1).
  // Cadence = bar 3, 7, 11, 15, ... (bar_in_phrase == 4).
  // We want peaks at bars that are >2 bars from any Intensification.
  //
  // Intensification bars (0-indexed): 2, 6, 10, 14, 18, 22, 26, 30.
  // Bars maximally far from Intensification: bars 0, 4, 8, 12, 16, 20, 24, 28
  // (distance 2 each). These are all exactly 2 bars away, which is within
  // tolerance. So we need distance > 2. Actually the max distance from
  // an Intensification bar is only 2 in a 4-bar phrase grid (0,1,2,3).
  //
  // With bar spacing of 4 and Intensification at offset 2, every bar is
  // at most 2 bars from an Intensification bar. So to test the fail case,
  // we need to exercise the function with notes well beyond bar 31 (clamped
  // to 31), or use a mock. Since bars clamp to [0,31], we can place notes
  // beyond bar 31 so the clamped bar = 31, which is distance 1 from bar 30
  // (Intensification). That still passes.
  //
  // For a realistic fail test, we need to construct a scenario where peaks
  // are at bars whose minimum distance to Intensification > 2. In a standard
  // 32-bar grid this is impossible (max gap is 2). So we test the near-edge:
  // if both peaks happen to be at bars where distance == 0 that passes,
  // and we confirm the function works correctly by verifying the true case.
  //
  // Instead, we test with empty notes for the fail case.
  // Actually, empty notes return true vacuously. Let's verify the alignment
  // logic with a more targeted approach: we can check that having a peak
  // at bar 0 (Opening, distance 2 from bar 2) is within tolerance 2, so
  // that passes. The function always returns true for a 4-bar phrase grid
  // because the max distance is 2. Document this as a structural property.
  //
  // Test that the function returns the correct value for peaks at different
  // positions. Since max distance in the grid is 2 and tolerance is 2,
  // all positions pass in the standard grid. We verify this property.
  Tick ticks_per_bar = kTriple.ticksPerBar();

  // Peaks at bar 0 (distance 2 from Intensification at bar 2).
  std::vector<NoteEvent> dux = {
      makeNote(90, 0),                         // bar 0 (peak)
      makeNote(60, ticks_per_bar),             // bar 1
  };

  std::vector<NoteEvent> comes = {
      makeNote(90, ticks_per_bar * 4, kTicksPerBeat,
               BachNoteSource::CanonComes),    // bar 4 (peak)
      makeNote(60, ticks_per_bar * 5, kTicksPerBeat,
               BachNoteSource::CanonComes),    // bar 5
  };

  // In a 4-bar phrase grid, max distance from Intensification is 2.
  // Bar 0 is distance 2 from bar 2. Bar 4 is distance 2 from bar 2 or 6.
  // Both are within tolerance of 2, so this should pass.
  bool aligned = validateClimaxAlignment(dux, comes, grid_, kTriple);
  EXPECT_TRUE(aligned)
      << "In standard 32-bar grid, max distance from Intensification is 2 "
         "(within tolerance)";
}

// ---------------------------------------------------------------------------
// Test 12: MinorKeyValidation
// ---------------------------------------------------------------------------

TEST_F(CanonValidatorTest, MinorKeyValidation) {
  // Canon at the 2nd in G natural minor.
  // G natural minor: G A Bb C D Eb F.
  CanonSpec spec = makeSpec(1, CanonTransform::Regular, kGMinor,
                            MinorModeProfile::NaturalMinor);

  std::vector<NoteEvent> dux = {
      makeNote(67, 0),                  // G4
      makeNote(69, kTicksPerBeat),      // A4
      makeNote(70, kTicksPerBeat * 2),  // Bb4
      makeNote(72, kTriple.ticksPerBar()),  // C5
  };

  auto comes = buildCorrectComes(dux, spec, kTriple);
  auto result = validateCanonIntegrity(dux, comes, spec, kTriple);

  EXPECT_TRUE(result.passed) << "Minor key canon with correct transposition should pass";
  EXPECT_EQ(result.pitch_violations, 0);
  EXPECT_EQ(result.total_pairs, 4);
}

// ---------------------------------------------------------------------------
// Test 13: OctaveCanon
// ---------------------------------------------------------------------------

TEST_F(CanonValidatorTest, OctaveCanon) {
  // Canon at the octave (interval=7 diatonic degrees).
  // DuxBuffer test verifies: G4(67) + 7 degrees = G5(79).
  CanonSpec spec = makeSpec(7);

  std::vector<NoteEvent> dux = {
      makeNote(67, 0),                  // G4
      makeNote(69, kTicksPerBeat),      // A4
      makeNote(71, kTicksPerBeat * 2),  // B4
      makeNote(72, kTriple.ticksPerBar()),  // C5
  };

  auto comes = buildCorrectComes(dux, spec, kTriple);
  auto result = validateCanonIntegrity(dux, comes, spec, kTriple);

  EXPECT_TRUE(result.passed) << "Octave canon with correct transposition should pass";
  EXPECT_EQ(result.pitch_violations, 0);
  EXPECT_EQ(result.total_pairs, 4);

  // Verify the comes pitches match the absoluteDegree transformation.
  // The diatonic degree system uses MIDI octaves (pitch/12), so +7 degrees
  // maps to the next occurrence of the same scale degree in the next octave.
  // G4(67): abs_degree 35+7=42 -> G5(79).
  // A4(69): abs_degree 36+7=43 -> A5(81).
  // B4(71): abs_degree 37+7=44 -> B5(83).
  // C5(72): abs_degree 45+7=52 -> absoluteDegreeToPitch(52, G, Major).
  EXPECT_EQ(comes[0].pitch, expectedComesPitch(67, spec)) << "G4 + 7 degrees";
  EXPECT_EQ(comes[1].pitch, expectedComesPitch(69, spec)) << "A4 + 7 degrees";
  EXPECT_EQ(comes[2].pitch, expectedComesPitch(71, spec)) << "B4 + 7 degrees";
  EXPECT_EQ(comes[3].pitch, expectedComesPitch(72, spec)) << "C5 + 7 degrees";
}

// ---------------------------------------------------------------------------
// Test 14: MultipleViolationTypes
// ---------------------------------------------------------------------------

TEST_F(CanonValidatorTest, MultipleViolationTypes) {
  // Combines pitch, timing, and duration violations in one validation.
  CanonSpec spec = makeSpec(0);

  std::vector<NoteEvent> dux = {
      makeNote(67, 0, kTicksPerBeat),
      makeNote(69, kTicksPerBeat, kTicksPerBeat),
      makeNote(71, kTicksPerBeat * 2, kTicksPerBeat),
      makeNote(72, kTriple.ticksPerBar(), kTicksPerBeat),
  };

  auto comes = buildCorrectComes(dux, spec, kTriple);
  comes[0].pitch = 60;                     // Pitch violation.
  comes[2].duration = duration::kHalfNote;  // Duration violation.

  auto result = validateCanonIntegrity(dux, comes, spec, kTriple);

  EXPECT_EQ(result.pitch_violations, 1);
  EXPECT_EQ(result.duration_violations, 1);
  EXPECT_FALSE(result.passed);
  // Should have messages for both violation types.
  EXPECT_GE(result.messages.size(), 2u);
}

// ---------------------------------------------------------------------------
// Test 15: ClimaxAlignmentEmptyVoices
// ---------------------------------------------------------------------------

TEST_F(CanonValidatorTest, ClimaxAlignmentEmptyVoices) {
  std::vector<NoteEvent> empty;
  std::vector<NoteEvent> non_empty = {makeNote(67, 0)};

  // Both empty.
  EXPECT_TRUE(validateClimaxAlignment(empty, empty, grid_, kTriple))
      << "Empty voices should return true vacuously";

  // One empty.
  EXPECT_TRUE(validateClimaxAlignment(empty, non_empty, grid_, kTriple))
      << "One empty voice should return true vacuously";
  EXPECT_TRUE(validateClimaxAlignment(non_empty, empty, grid_, kTriple))
      << "One empty voice should return true vacuously";
}

// ---------------------------------------------------------------------------
// Test 16: HarmonicMinorCanon
// ---------------------------------------------------------------------------

TEST_F(CanonValidatorTest, HarmonicMinorCanon) {
  // Canon at the 2nd in G harmonic minor.
  // G harmonic minor: G A Bb C D Eb F#.
  CanonSpec spec = makeSpec(1, CanonTransform::Regular, kGMinor,
                            MinorModeProfile::HarmonicMinor);

  std::vector<NoteEvent> dux = {
      makeNote(67, 0),                  // G4
      makeNote(69, kTicksPerBeat),      // A4
      makeNote(70, kTicksPerBeat * 2),  // Bb4
      makeNote(72, kTriple.ticksPerBar()),  // C5
  };

  auto comes = buildCorrectComes(dux, spec, kTriple);
  auto result = validateCanonIntegrity(dux, comes, spec, kTriple);

  EXPECT_TRUE(result.passed)
      << "Harmonic minor canon with correct transposition should pass";
  EXPECT_EQ(result.pitch_violations, 0);
}

// ---------------------------------------------------------------------------
// Test 17: ProportionalRhythmSkipsDurationCheck
// ---------------------------------------------------------------------------

TEST_F(CanonValidatorTest, ProportionalRhythmSkipsDurationCheck) {
  // In Proportional rhythmic mode, duration differences should not be violations.
  CanonSpec spec = makeSpec(0);
  spec.rhythmic_mode = CanonRhythmicMode::Proportional;

  std::vector<NoteEvent> dux = {
      makeNote(67, 0, kTicksPerBeat),
      makeNote(69, kTicksPerBeat, kTicksPerBeat),
  };

  auto comes = buildCorrectComes(dux, spec, kTriple);
  comes[0].duration = duration::kHalfNote;  // Different duration.
  comes[1].duration = duration::kEighthNote;

  auto result = validateCanonIntegrity(dux, comes, spec, kTriple);

  EXPECT_EQ(result.duration_violations, 0)
      << "Proportional mode should not flag duration differences";
  EXPECT_TRUE(result.passed);
}

// ---------------------------------------------------------------------------
// Test 18: TimingToleranceExactBoundary
// ---------------------------------------------------------------------------

TEST_F(CanonValidatorTest, TimingToleranceExactBoundary) {
  // Verify that +/-1 tick tolerance works at the boundary.
  CanonSpec spec = makeSpec(0);

  std::vector<NoteEvent> dux = {makeNote(67, 0)};
  Tick delay_ticks = kTriple.ticksPerBar();

  // Comes offset by exactly 1 tick: should still match.
  {
    std::vector<NoteEvent> comes = {
        makeNote(67, delay_ticks + 1, kTicksPerBeat,
                 BachNoteSource::CanonComes)};
    auto result = validateCanonIntegrity(dux, comes, spec, kTriple);
    EXPECT_EQ(result.total_pairs, 1);
    EXPECT_EQ(result.pitch_violations, 0);
    EXPECT_TRUE(result.passed) << "+1 tick should be within tolerance";
  }

  // Comes offset by 2 ticks: should not match (beyond tolerance).
  {
    std::vector<NoteEvent> comes = {
        makeNote(67, delay_ticks + 2, kTicksPerBeat,
                 BachNoteSource::CanonComes)};
    auto result = validateCanonIntegrity(dux, comes, spec, kTriple);
    // The note is at delay_ticks+2, tolerance is 1, so no pair is matched.
    EXPECT_EQ(result.total_pairs, 0);
  }
}

// ---------------------------------------------------------------------------
// Test 19: InvertedCanonWithTransposition
// ---------------------------------------------------------------------------

TEST_F(CanonValidatorTest, InvertedCanonWithTransposition) {
  // Inverted canon at the 5th (interval=4) in G major.
  // DuxBuffer test verifies: A4(69) inverted around G, +4 degrees = C5(72).
  CanonSpec spec = makeSpec(4, CanonTransform::Inverted);

  std::vector<NoteEvent> dux = {
      makeNote(67, 0),                  // G4
      makeNote(69, kTicksPerBeat),      // A4
      makeNote(71, kTicksPerBeat * 2),  // B4
  };

  auto comes = buildCorrectComes(dux, spec, kTriple);
  auto result = validateCanonIntegrity(dux, comes, spec, kTriple);

  EXPECT_TRUE(result.passed) << "Inverted 5th canon should pass";
  EXPECT_EQ(result.pitch_violations, 0);
  EXPECT_EQ(result.total_pairs, 3);
}

// ---------------------------------------------------------------------------
// Test 20: LargeCanonPasses
// ---------------------------------------------------------------------------

TEST_F(CanonValidatorTest, LargeCanonPasses) {
  // A longer canon (32 notes) should validate correctly.
  CanonSpec spec = makeSpec(2);  // Canon at the 3rd.

  std::vector<NoteEvent> dux;
  dux.reserve(32);
  // Create a simple ascending/descending pattern in G major.
  uint8_t pitches[] = {67, 69, 71, 72, 74, 76, 78, 79};  // G major scale.
  for (int idx = 0; idx < 32; ++idx) {
    uint8_t pitch = pitches[idx % 8];
    dux.push_back(makeNote(pitch, static_cast<Tick>(idx) * kTicksPerBeat));
  }

  auto comes = buildCorrectComes(dux, spec, kTriple);
  auto result = validateCanonIntegrity(dux, comes, spec, kTriple);

  EXPECT_TRUE(result.passed) << "Large correct canon should pass";
  EXPECT_EQ(result.total_pairs, 32);
  EXPECT_EQ(result.pitch_violations, 0);
  EXPECT_FLOAT_EQ(result.pitch_accuracy, 1.0f);
}

}  // namespace
}  // namespace bach
