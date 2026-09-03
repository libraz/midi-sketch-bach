// Tests for gmProgramFor and applyInstrument.

#include "core/instrument_program.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <initializer_list>
#include <utility>
#include <vector>

#include "core/basic_types.h"
#include "core/gm_program.h"
#include "core/pitch_utils.h"

namespace bach {
namespace {

// ---------------------------------------------------------------------------
// gmProgramFor: legacy-consistent GM program per instrument
// ---------------------------------------------------------------------------

TEST(InstrumentProgramTest, OrganMapsToChurchOrgan) {
  EXPECT_EQ(gmProgramFor(InstrumentType::Organ), GmProgram::kChurchOrgan);
  EXPECT_EQ(gmProgramFor(InstrumentType::Organ), 19);
}

TEST(InstrumentProgramTest, HarpsichordMapsToHarpsichord) {
  EXPECT_EQ(gmProgramFor(InstrumentType::Harpsichord), GmProgram::kHarpsichord);
  EXPECT_EQ(gmProgramFor(InstrumentType::Harpsichord), 6);
}

TEST(InstrumentProgramTest, PianoMapsToAcousticGrand) {
  EXPECT_EQ(gmProgramFor(InstrumentType::Piano), GmProgram::kPiano);
  EXPECT_EQ(gmProgramFor(InstrumentType::Piano), 0);
}

TEST(InstrumentProgramTest, ViolinMapsToViolin) {
  EXPECT_EQ(gmProgramFor(InstrumentType::Violin), GmProgram::kViolin);
  EXPECT_EQ(gmProgramFor(InstrumentType::Violin), 40);
}

TEST(InstrumentProgramTest, CelloMapsToCello) {
  EXPECT_EQ(gmProgramFor(InstrumentType::Cello), GmProgram::kCello);
  EXPECT_EQ(gmProgramFor(InstrumentType::Cello), 42);
}

TEST(InstrumentProgramTest, GuitarMapsToNylonGuitar) {
  EXPECT_EQ(gmProgramFor(InstrumentType::Guitar), GmProgram::kNylonGuitar);
  EXPECT_EQ(gmProgramFor(InstrumentType::Guitar), 24);
}

// ---------------------------------------------------------------------------
// applyInstrument: program assignment and name fill-in
// ---------------------------------------------------------------------------

TEST(InstrumentProgramTest, ApplyInstrumentSetsProgramOnAllTracks) {
  std::vector<Track> tracks(3);
  tracks[0].channel = 0;
  tracks[1].channel = 1;
  tracks[2].channel = 2;

  applyInstrument(tracks, InstrumentType::Violin);

  for (const auto& track : tracks) {
    EXPECT_EQ(track.program, GmProgram::kViolin);
  }
}

TEST(InstrumentProgramTest, ApplyInstrumentFillsEmptyNamesWithVoiceIndex) {
  std::vector<Track> tracks(3);

  applyInstrument(tracks, InstrumentType::Cello);

  EXPECT_EQ(tracks[0].name, "Voice 0");
  EXPECT_EQ(tracks[1].name, "Voice 1");
  EXPECT_EQ(tracks[2].name, "Voice 2");
  EXPECT_EQ(tracks[0].instrument_name, "cello");
  EXPECT_EQ(tracks[1].instrument_name, "cello");
  EXPECT_EQ(tracks[2].instrument_name, "cello");
}

TEST(InstrumentProgramTest, ApplyInstrumentPreservesExistingNames) {
  std::vector<Track> tracks(3);
  tracks[0].name = "Cantus Firmus";
  tracks[2].name = "Pedal";

  applyInstrument(tracks, InstrumentType::Organ);

  // Existing names are preserved; only empty names are filled in.
  EXPECT_EQ(tracks[0].name, "Cantus Firmus");
  EXPECT_EQ(tracks[1].name, "Voice 1");
  EXPECT_EQ(tracks[2].name, "Pedal");
}

TEST(InstrumentProgramTest, ApplyInstrumentOnEmptyTracksIsNoOp) {
  std::vector<Track> tracks;
  applyInstrument(tracks, InstrumentType::Piano);
  EXPECT_TRUE(tracks.empty());
}

TEST(InstrumentProgramTest, ApplyInstrumentOverwritesPreviousProgram) {
  std::vector<Track> tracks(2);
  tracks[0].program = 99;
  tracks[1].program = 99;

  applyInstrument(tracks, InstrumentType::Harpsichord);

  EXPECT_EQ(tracks[0].program, GmProgram::kHarpsichord);
  EXPECT_EQ(tracks[1].program, GmProgram::kHarpsichord);
}

// ---------------------------------------------------------------------------
// selectOutputOctaveShift: one whole-octave placement for the whole score
// ---------------------------------------------------------------------------

std::vector<NoteEvent> scoreWithPitches(std::initializer_list<int> pitches) {
  std::vector<NoteEvent> notes;
  for (int pitch : pitches) {
    NoteEvent note;
    note.pitch = static_cast<std::uint8_t>(pitch);
    notes.push_back(note);
  }
  return notes;
}

// Lowest and highest sounding pitch after key transposition and `shift`.
std::pair<int, int> renderedExtremes(const std::vector<NoteEvent>& notes, Key key, int shift) {
  int low = 128;
  int high = -1;
  for (const NoteEvent& note : notes) {
    const int rendered = static_cast<int>(note.pitch) + keyTranspositionSemitones(key) + shift;
    low = std::min(low, rendered);
    high = std::max(high, rendered);
  }
  return {low, high};
}

bool insideMidiRange(const std::vector<NoteEvent>& notes, Key key, int shift) {
  const std::pair<int, int> extremes = renderedExtremes(notes, key, shift);
  return extremes.first >= 0 && extremes.second <= 127;
}

// Total semitones the score falls outside the instrument compass at `shift`.
int compassExcess(const std::vector<NoteEvent>& notes, Key key, InstrumentType instrument,
                  int shift) {
  const InstrumentPitchRange range = pitchRangeFor(instrument);
  const std::pair<int, int> extremes = renderedExtremes(notes, key, shift);
  const int below = std::max(0, static_cast<int>(range.low) - extremes.first);
  const int above = std::max(0, extremes.second - static_cast<int>(range.high));
  return below + above;
}

// No other whole-octave displacement MIDI can hold leaves less of the score
// outside the compass than `shift` does.
void expectMinimalExcess(const std::vector<NoteEvent>& notes, Key key, InstrumentType instrument,
                         int shift) {
  const int chosen = compassExcess(notes, key, instrument, shift);
  for (int candidate = -120; candidate <= 120; candidate += 12) {
    if (!insideMidiRange(notes, key, candidate)) {
      continue;
    }
    EXPECT_GE(compassExcess(notes, key, instrument, candidate), chosen)
        << "displacement " << candidate << " leaves less outside the compass than " << shift;
  }
}

TEST(InstrumentProgramTest, OutputRangeUsesOneOctaveForTheEntirePiece) {
  const std::vector<NoteEvent> notes = scoreWithPitches({36, 84});

  const auto shift = selectOutputOctaveShift(notes, Key::B, InstrumentType::Violin);
  ASSERT_TRUE(shift.has_value());
  EXPECT_EQ(*shift, 12);
  const InstrumentPitchRange range = pitchRangeFor(InstrumentType::Violin);
  for (const NoteEvent& note : notes) {
    const int rendered = static_cast<int>(note.pitch) + keyTranspositionSemitones(Key::B) + *shift;
    EXPECT_GE(rendered, range.low);
    EXPECT_LE(rendered, range.high);
  }
}

TEST(InstrumentProgramTest, OutputRangeKeepsTheSmallestDisplacementThatFits) {
  // Piano compass 21-108 swallows a two-note score at seven displacements.
  const std::vector<NoteEvent> notes = scoreWithPitches({60, 62});

  EXPECT_EQ(compassExcess(notes, Key::C, InstrumentType::Piano, -12), 0);
  EXPECT_EQ(compassExcess(notes, Key::C, InstrumentType::Piano, 0), 0);
  EXPECT_EQ(compassExcess(notes, Key::C, InstrumentType::Piano, 12), 0);

  const auto shift = selectOutputOctaveShift(notes, Key::C, InstrumentType::Piano);
  ASSERT_TRUE(shift.has_value());
  EXPECT_EQ(*shift, 0);
}

TEST(InstrumentProgramTest, OutputRangeStaysUnshiftedWhenBothNeighbourOctavesAlsoFit) {
  // A compass is one contiguous span, so a score that fits both an octave up
  // and an octave down necessarily fits untransposed as well: the smallest
  // magnitude then decides and no displacement is applied.
  const std::vector<NoteEvent> notes = scoreWithPitches({55, 60, 64});

  ASSERT_EQ(compassExcess(notes, Key::C, InstrumentType::Piano, -12), 0);
  ASSERT_EQ(compassExcess(notes, Key::C, InstrumentType::Piano, 12), 0);

  const auto shift = selectOutputOctaveShift(notes, Key::C, InstrumentType::Piano);
  ASSERT_TRUE(shift.has_value());
  EXPECT_EQ(*shift, 0);
}

TEST(InstrumentProgramTest, OutputRangeFallsBackToTheNearestDisplacementWhenNothingFits) {
  // Guitar compass 40-83 is 44 semitones wide and the score spans 40, so some
  // alignment would fit -- but only whole octaves are available and none of
  // them lands inside.
  const std::vector<NoteEvent> notes = scoreWithPitches({24, 64});
  const InstrumentPitchRange range = pitchRangeFor(InstrumentType::Guitar);
  ASSERT_LT(64 - 24, static_cast<int>(range.high) - static_cast<int>(range.low));

  const auto shift = selectOutputOctaveShift(notes, Key::C, InstrumentType::Guitar);
  ASSERT_TRUE(shift.has_value());
  EXPECT_EQ(*shift, 12);
  EXPECT_EQ(compassExcess(notes, Key::C, InstrumentType::Guitar, *shift), 4);
  expectMinimalExcess(notes, Key::C, InstrumentType::Guitar, *shift);
}

TEST(InstrumentProgramTest, OutputRangePlacesAPieceNoOctaveCanFitInsteadOfRefusingIt) {
  // The violin output compass 36-96 is one semitone wider than this score, and
  // the key transposition moves it off the only alignment that fits. Placing it
  // one semitone below the floor keeps the piece; refusing it would not.
  const std::vector<NoteEvent> notes = scoreWithPitches({36, 96});

  const auto shift = selectOutputOctaveShift(notes, Key::B, InstrumentType::Violin);
  ASSERT_TRUE(shift.has_value());
  EXPECT_EQ(*shift, 0);
  EXPECT_EQ(compassExcess(notes, Key::B, InstrumentType::Violin, *shift), 1);
  expectMinimalExcess(notes, Key::B, InstrumentType::Violin, *shift);
}

TEST(InstrumentProgramTest, OutputRangePrefersTheSmallerExcessOverTheSmallerDisplacement) {
  // Leaving the score untransposed puts 5 semitones above the guitar ceiling;
  // an octave down puts 3 below its floor. The smaller excess wins even though
  // it is the larger displacement.
  const std::vector<NoteEvent> notes = scoreWithPitches({49, 88});
  ASSERT_EQ(compassExcess(notes, Key::C, InstrumentType::Guitar, 0), 5);
  ASSERT_EQ(compassExcess(notes, Key::C, InstrumentType::Guitar, -12), 3);

  const auto shift = selectOutputOctaveShift(notes, Key::C, InstrumentType::Guitar);
  ASSERT_TRUE(shift.has_value());
  EXPECT_EQ(*shift, -12);
  expectMinimalExcess(notes, Key::C, InstrumentType::Guitar, *shift);
}

TEST(InstrumentProgramTest, OutputRangeBreaksAnExcessTieByTheSmallerDisplacement) {
  // A score wider than the compass leaves the same excess across a whole band
  // of displacements; the smallest magnitude in that band is chosen.
  const std::vector<NoteEvent> notes = scoreWithPitches({30, 100});
  ASSERT_EQ(compassExcess(notes, Key::C, InstrumentType::Guitar, -12),
            compassExcess(notes, Key::C, InstrumentType::Guitar, 0));

  const auto shift = selectOutputOctaveShift(notes, Key::C, InstrumentType::Guitar);
  ASSERT_TRUE(shift.has_value());
  EXPECT_EQ(*shift, 0);
  expectMinimalExcess(notes, Key::C, InstrumentType::Guitar, *shift);
}

TEST(InstrumentProgramTest, OutputRangeOnAnEmptyScoreIsZero) {
  const std::vector<NoteEvent> notes;
  const auto shift = selectOutputOctaveShift(notes, Key::G, InstrumentType::Violin);
  ASSERT_TRUE(shift.has_value());
  EXPECT_EQ(*shift, 0);
}

TEST(InstrumentProgramTest, OutputRangeRejectsAScoreNoDisplacementCanPlaceInsideMidi) {
  // A pitch is a uint8_t, so an ambitus wider than MIDI cannot be represented;
  // the reachable form of the same condition is a score filling MIDI end to end
  // whose key transposition pushes it off the only alignment MIDI can hold.
  const std::vector<NoteEvent> notes = scoreWithPitches({0, 127});
  ASSERT_EQ(keyTranspositionSemitones(Key::G), -5);
  for (int candidate = -120; candidate <= 120; candidate += 12) {
    ASSERT_FALSE(insideMidiRange(notes, Key::G, candidate)) << "displacement " << candidate;
  }

  EXPECT_FALSE(selectOutputOctaveShift(notes, Key::G, InstrumentType::Violin).has_value());
}

}  // namespace
}  // namespace bach
