// Output placement (composer/compass_placement.h) tests.

#include "composer/compass_placement.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

#include "composer/composer.h"
#include "composer/harmonic_plan.h"
#include "composer/material.h"
#include "composer/provenance.h"
#include "core/basic_types.h"
#include "core/instrument_program.h"
#include "core/pitch_utils.h"

namespace bach::composer {
namespace {

void addNote(ComposeResult* result, Tick start, Tick duration, std::uint8_t pitch, VoiceId voice) {
  NoteEvent note;
  note.start_tick = start;
  note.duration = duration;
  note.pitch = pitch;
  note.voice = voice;
  result->notes.push_back(note);
  NoteProvenance prov;
  prov.source = NoteSource::Material;
  prov.has_authored_note = true;
  prov.authored_start_tick = start;
  prov.authored_duration = duration;
  prov.authored_pitch = pitch;
  result->provenance.push_back(prov);
}

// Two bars, two voices. The upper voice climbs to C6 in bar 1 while the lower
// voice drops to A1 under it, so the score spans 51 semitones: in D no whole
// octave fits it, by two semitones above or one below.
ComposeResult wideScore() {
  ComposeResult result;
  const std::uint8_t upper[2][4] = {{72, 74, 76, 77}, {79, 81, 83, 84}};
  for (int bar = 0; bar < 2; ++bar) {
    for (int beat = 0; beat < 4; ++beat) {
      addNote(&result, static_cast<Tick>(bar) * kTicksPerBar + beat * kTicksPerBeat, kTicksPerBeat,
              upper[bar][beat], 0);
    }
  }
  addNote(&result, 0, kTicksPerBar, 36, 1);
  addNote(&result, kTicksPerBar, kTicksPerBar, 33, 1);
  return result;
}

void expectInsideOrganCompass(const ComposeResult& result, Key key, int octave_shift) {
  for (const NoteEvent& note : result.notes) {
    const int sounding =
        static_cast<int>(note.pitch) + keyTranspositionSemitones(key) + octave_shift;
    EXPECT_GE(sounding, organ_range::kPedalLow);
    EXPECT_LE(sounding, organ_range::kManualHigh);
  }
}

const auto kAcceptAll = [](const ComposeResult&) { return true; };

TEST(CompassPlacementTest, ScoreThatFitsByWholeOctaveMovesNothing) {
  ComposeResult result;
  addNote(&result, 0, kTicksPerBar, 72, 0);
  addNote(&result, 0, kTicksPerBar, 40, 1);
  const ComposeResult before = result;
  int accept_calls = 0;
  const auto placement = placeInOutputCompass(
      &result, Material{}, HarmonicPlan{}, Key::D, InstrumentType::Organ, kTicksPerBar,
      [&accept_calls](const ComposeResult&) { return ++accept_calls > 0; });
  ASSERT_TRUE(placement.has_value());
  EXPECT_EQ(placement->octave_shift,
            selectOutputOctaveShift(before.notes, Key::D, InstrumentType::Organ).value());
  EXPECT_EQ(placement->displaced_notes, 0u);
  EXPECT_EQ(accept_calls, 0);
  for (std::size_t idx = 0; idx < result.notes.size(); ++idx)
    EXPECT_EQ(result.notes[idx].pitch, before.notes[idx].pitch);
}

TEST(CompassPlacementTest, BarOutsideTheCompassMovesAnOctaveInward) {
  ComposeResult result = wideScore();
  const auto placement = placeInOutputCompass(&result, Material{}, HarmonicPlan{}, Key::D,
                                              InstrumentType::Organ, kTicksPerBar, kAcceptAll);
  ASSERT_TRUE(placement.has_value());
  expectInsideOrganCompass(result, Key::D, placement->octave_shift);
  // Moving the lone bass note of bar 1 costs one note against four for the
  // upper voice's bar, and keeps it under the upper voice.
  EXPECT_EQ(placement->octave_shift, -12);
  EXPECT_EQ(placement->displaced_notes, 1u);
  EXPECT_EQ(placement->crossing_notes, 0u);
  EXPECT_EQ(result.notes.back().pitch, 45);
  EXPECT_EQ(result.provenance.back().authored_pitch, 45);
  EXPECT_EQ(result.notes[8].pitch, 36);
}

TEST(CompassPlacementTest, PinnedNotesStayWhileAnotherPlacementExists) {
  ComposeResult result = wideScore();
  Material material;
  SuspensionPattern held;
  held.voice = 1;
  held.preparation_tick = kTicksPerBar;
  held.suspension_tick = kTicksPerBar + kTicksPerBeat;
  held.resolution_tick = kTicksPerBar + 2 * kTicksPerBeat;
  material.suspension_patterns.push_back(held);
  const auto placement = placeInOutputCompass(&result, material, HarmonicPlan{}, Key::D,
                                              InstrumentType::Organ, kTicksPerBar, kAcceptAll);
  ASSERT_TRUE(placement.has_value());
  expectInsideOrganCompass(result, Key::D, placement->octave_shift);
  EXPECT_EQ(placement->octave_shift, 0);
  EXPECT_EQ(placement->pinned_notes, 0u);
  EXPECT_EQ(result.notes.back().pitch, 33);
  for (int beat = 0; beat < 4; ++beat)
    EXPECT_LT(result.notes[4 + beat].pitch, 79);
}

TEST(CompassPlacementTest, RejectedPlacementLeavesTheScoreAsComposed) {
  ComposeResult result = wideScore();
  const ComposeResult before = result;
  const auto placement =
      placeInOutputCompass(&result, Material{}, HarmonicPlan{}, Key::D, InstrumentType::Organ,
                           kTicksPerBar, [](const ComposeResult&) { return false; });
  ASSERT_TRUE(placement.has_value());
  EXPECT_EQ(placement->displaced_notes, 0u);
  EXPECT_EQ(placement->octave_shift,
            selectOutputOctaveShift(before.notes, Key::D, InstrumentType::Organ).value());
  for (std::size_t idx = 0; idx < result.notes.size(); ++idx)
    EXPECT_EQ(result.notes[idx].pitch, before.notes[idx].pitch);
}

TEST(CompassPlacementTest, PreferredCompassNeverMovesNotes) {
  ComposeResult result = wideScore();
  ASSERT_FALSE(pitchRangeFor(InstrumentType::Harpsichord).hard_limit);
  const ComposeResult before = result;
  const auto placement =
      placeInOutputCompass(&result, Material{}, HarmonicPlan{}, Key::D, InstrumentType::Harpsichord,
                           kTicksPerBar, kAcceptAll);
  ASSERT_TRUE(placement.has_value());
  EXPECT_EQ(placement->displaced_notes, 0u);
  for (std::size_t idx = 0; idx < result.notes.size(); ++idx)
    EXPECT_EQ(result.notes[idx].pitch, before.notes[idx].pitch);
}

}  // namespace
}  // namespace bach::composer
