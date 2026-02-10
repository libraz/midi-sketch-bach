// Tests for natural harmonics detection and marking.

#include "instrument/bowed/harmonics.h"

#include <gtest/gtest.h>

#include "core/basic_types.h"

namespace bach {
namespace {

// ---------------------------------------------------------------------------
// getNaturalHarmonicPitches
// ---------------------------------------------------------------------------

TEST(HarmonicsTest, ViolinHarmonicPitchCount) {
  auto harmonics = getNaturalHarmonicPitches(InstrumentType::Violin);
  // Violin: 4 strings x 2 harmonics each = 8, minus any duplicates.
  // G3(55)+12=67, G3+19=74, D4(62)+12=74, D4+19=81, A4(69)+12=81, A4+19=88,
  // E5(76)+12=88, E5+19=95.
  // Duplicates: 74 appears twice, 81 appears twice, 88 appears twice.
  // Unique: {67, 74, 81, 88, 95} = 5 pitches.
  EXPECT_EQ(harmonics.size(), 5u);
}

TEST(HarmonicsTest, CelloHarmonicPitchCount) {
  auto harmonics = getNaturalHarmonicPitches(InstrumentType::Cello);
  // Cello: C2(36)+12=48, C2+19=55, G2(43)+12=55, G2+19=62, D3(50)+12=62,
  // D3+19=69, A3(57)+12=69, A3+19=76.
  // Duplicates: 55 appears twice, 62 appears twice, 69 appears twice.
  // Unique: {48, 55, 62, 69, 76} = 5 pitches.
  EXPECT_EQ(harmonics.size(), 5u);
}

TEST(HarmonicsTest, ViolinOctaveHarmonicsPresent) {
  auto harmonics = getNaturalHarmonicPitches(InstrumentType::Violin);
  // Octave harmonics: G3+12=67, D4+12=74, A4+12=81, E5+12=88.
  EXPECT_NE(std::find(harmonics.begin(), harmonics.end(), 67), harmonics.end());
  EXPECT_NE(std::find(harmonics.begin(), harmonics.end(), 74), harmonics.end());
  EXPECT_NE(std::find(harmonics.begin(), harmonics.end(), 81), harmonics.end());
  EXPECT_NE(std::find(harmonics.begin(), harmonics.end(), 88), harmonics.end());
}

TEST(HarmonicsTest, ViolinFifthHarmonicsPresent) {
  auto harmonics = getNaturalHarmonicPitches(InstrumentType::Violin);
  // Fifth harmonics: G3+19=74, D4+19=81, A4+19=88, E5+19=95.
  EXPECT_NE(std::find(harmonics.begin(), harmonics.end(), 74), harmonics.end());
  EXPECT_NE(std::find(harmonics.begin(), harmonics.end(), 81), harmonics.end());
  EXPECT_NE(std::find(harmonics.begin(), harmonics.end(), 88), harmonics.end());
  EXPECT_NE(std::find(harmonics.begin(), harmonics.end(), 95), harmonics.end());
}

TEST(HarmonicsTest, PitchesAreSortedAscending) {
  auto harmonics = getNaturalHarmonicPitches(InstrumentType::Violin);
  for (size_t idx = 1; idx < harmonics.size(); ++idx) {
    EXPECT_LT(harmonics[idx - 1], harmonics[idx]);
  }
}

// ---------------------------------------------------------------------------
// isNaturalHarmonic
// ---------------------------------------------------------------------------

TEST(HarmonicsTest, ViolinOctaveHarmonicsDetected) {
  // Octave harmonics on each string.
  EXPECT_TRUE(isNaturalHarmonic(67, InstrumentType::Violin));   // G3 + 12
  EXPECT_TRUE(isNaturalHarmonic(74, InstrumentType::Violin));   // D4 + 12
  EXPECT_TRUE(isNaturalHarmonic(81, InstrumentType::Violin));   // A4 + 12
  EXPECT_TRUE(isNaturalHarmonic(88, InstrumentType::Violin));   // E5 + 12
}

TEST(HarmonicsTest, ViolinFifthHarmonicsDetected) {
  EXPECT_TRUE(isNaturalHarmonic(95, InstrumentType::Violin));   // E5 + 19
}

TEST(HarmonicsTest, NonHarmonicPitchReturnsFalse) {
  // Middle C is not a natural harmonic on violin.
  EXPECT_FALSE(isNaturalHarmonic(60, InstrumentType::Violin));
  // Open G string itself is not a harmonic (it is a fundamental).
  EXPECT_FALSE(isNaturalHarmonic(55, InstrumentType::Violin));
  // Random pitch.
  EXPECT_FALSE(isNaturalHarmonic(65, InstrumentType::Violin));
}

TEST(HarmonicsTest, CelloHarmonicsDetected) {
  // C2 + 12 = 48 (C3).
  EXPECT_TRUE(isNaturalHarmonic(48, InstrumentType::Cello));
  // A3 + 19 = 76.
  EXPECT_TRUE(isNaturalHarmonic(76, InstrumentType::Cello));
}

TEST(HarmonicsTest, OrganHasNoHarmonics) {
  // Organ is not a string instrument.
  EXPECT_FALSE(isNaturalHarmonic(67, InstrumentType::Organ));
}

// ---------------------------------------------------------------------------
// markHarmonics
// ---------------------------------------------------------------------------

TEST(HarmonicsTest, MarkHarmonicsOnlyDuringPeak) {
  std::vector<NoteEvent> notes;
  NoteEvent note;
  note.pitch = 67;  // G3 + 12 = harmonic on violin
  note.duration = kTicksPerBeat;
  note.start_tick = 0;
  notes.push_back(note);

  // Should not mark during Ascent.
  markHarmonics(notes, InstrumentType::Violin, ArcPhase::Ascent);
  EXPECT_EQ(notes[0].is_harmonic, 0u);

  // Should not mark during Descent.
  markHarmonics(notes, InstrumentType::Violin, ArcPhase::Descent);
  EXPECT_EQ(notes[0].is_harmonic, 0u);

  // Should mark during Peak.
  markHarmonics(notes, InstrumentType::Violin, ArcPhase::Peak);
  EXPECT_EQ(notes[0].is_harmonic, 1u);
}

TEST(HarmonicsTest, MarkHarmonicsRequiresMinDuration) {
  std::vector<NoteEvent> notes;

  // Short note (less than a beat) -- should not be marked.
  NoteEvent short_note;
  short_note.pitch = 67;  // Harmonic pitch
  short_note.duration = kTicksPerBeat / 2;
  short_note.start_tick = 0;
  notes.push_back(short_note);

  // Long note (full beat) -- should be marked.
  NoteEvent long_note;
  long_note.pitch = 74;  // Harmonic pitch
  long_note.duration = kTicksPerBeat;
  long_note.start_tick = kTicksPerBeat;
  notes.push_back(long_note);

  markHarmonics(notes, InstrumentType::Violin, ArcPhase::Peak);

  EXPECT_EQ(notes[0].is_harmonic, 0u);  // Too short
  EXPECT_EQ(notes[1].is_harmonic, 1u);  // Long enough
}

TEST(HarmonicsTest, NonHarmonicPitchNotMarked) {
  std::vector<NoteEvent> notes;
  NoteEvent note;
  note.pitch = 60;  // C4, not a harmonic on violin
  note.duration = kTicksPerBeat;
  note.start_tick = 0;
  notes.push_back(note);

  markHarmonics(notes, InstrumentType::Violin, ArcPhase::Peak);
  EXPECT_EQ(notes[0].is_harmonic, 0u);
}

TEST(HarmonicsTest, MarkHarmonicsEmptyNotes) {
  std::vector<NoteEvent> notes;
  markHarmonics(notes, InstrumentType::Violin, ArcPhase::Peak);
  EXPECT_TRUE(notes.empty());
}

TEST(HarmonicsTest, IsHarmonicFieldDefaultsToZero) {
  NoteEvent note;
  EXPECT_EQ(note.is_harmonic, 0u);
}

}  // namespace
}  // namespace bach
