// Tests for gmProgramFor and applyInstrument.

#include "core/instrument_program.h"

#include <gtest/gtest.h>

#include <vector>

#include "core/basic_types.h"
#include "core/gm_program.h"

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

}  // namespace
}  // namespace bach
