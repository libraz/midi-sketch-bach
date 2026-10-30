// Output placement (composer/compass_placement.h) tests.

#include "composer/compass_placement.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <vector>

#include "application/composition_service.h"
#include "composer/composer.h"
#include "composer/counterpoint_budget.h"
#include "composer/form_director.h"
#include "composer/harmonic_plan.h"
#include "composer/harness_fixture.h"
#include "composer/material.h"
#include "composer/ornament_pass.h"
#include "composer/provenance.h"
#include "composer/validator.h"
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

// A three-voice chaconne-shaped score spanning 36-92 (56 semitones): it fits the
// violin compass 36-96 in C, but in G neither +7 (43-99) nor -5 (31-87) does.
ComposeResult violinScore() {
  ComposeResult result;
  const std::uint8_t upper[2][4] = {{72, 74, 76, 77}, {86, 88, 90, 92}};
  for (int bar = 0; bar < 2; ++bar) {
    for (int beat = 0; beat < 4; ++beat) {
      addNote(&result, static_cast<Tick>(bar) * kTicksPerBar + beat * kTicksPerBeat, kTicksPerBeat,
              upper[bar][beat], 0);
    }
    addNote(&result, static_cast<Tick>(bar) * kTicksPerBar, kTicksPerBar, 60, 1);
    addNote(&result, static_cast<Tick>(bar) * kTicksPerBar, kTicksPerBar, 36, 2);
  }
  return result;
}

// The violin's compass is a preferred range rather than a hard limit, and it is
// placed by the same mechanism: where no whole-octave displacement fits, the bar
// holding the outside notes moves an octave inward.
TEST(CompassPlacementTest, PreferredCompassIsPlacedLikeAHardOne) {
  ASSERT_FALSE(pitchRangeFor(InstrumentType::Violin).hard_limit);
  ComposeResult result = violinScore();
  const auto placement = placeInOutputCompass(&result, Material{}, HarmonicPlan{}, Key::G,
                                              InstrumentType::Violin, kTicksPerBar, kAcceptAll);
  ASSERT_TRUE(placement.has_value());
  EXPECT_GT(placement->displaced_notes, 0u);
  const InstrumentPitchRange range = pitchRangeFor(InstrumentType::Violin);
  for (const NoteEvent& note : result.notes) {
    const int sounding =
        static_cast<int>(note.pitch) + keyTranspositionSemitones(Key::G) + placement->octave_shift;
    EXPECT_GE(sounding, static_cast<int>(range.low));
    EXPECT_LE(sounding, static_cast<int>(range.high));
  }
}

// The same score in a key where a whole-octave displacement fits is left as
// composed, at the displacement the writer would have chosen anyway.
TEST(CompassPlacementTest, PreferredCompassLeavesAFittingScoreUntouched) {
  ComposeResult result = violinScore();
  const ComposeResult before = result;
  const auto placement = placeInOutputCompass(&result, Material{}, HarmonicPlan{}, Key::C,
                                              InstrumentType::Violin, kTicksPerBar, kAcceptAll);
  ASSERT_TRUE(placement.has_value());
  EXPECT_EQ(placement->displaced_notes, 0u);
  EXPECT_EQ(placement->octave_shift,
            selectOutputOctaveShift(before.notes, Key::C, InstrumentType::Violin).value());
  for (std::size_t idx = 0; idx < result.notes.size(); ++idx)
    EXPECT_EQ(result.notes[idx].pitch, before.notes[idx].pitch);
}

// The chaconne spans 56 semitones at 128 bars, which no whole octave fits
// inside the violin compass in G, D or F minor; every note of the placed piece
// must still sound inside it.
TEST(CompassPlacementTest, ChaconneSoundsInsideTheViolinCompassInEveryKey) {
  const InstrumentPitchRange range = pitchRangeFor(InstrumentType::Violin);
  for (Key key : {Key::C, Key::G, Key::D, Key::F}) {
    for (std::uint32_t seed : {1u, 2u, 4u}) {
      ComposeRequest request;
      request.form = FormType::Chaconne;
      request.is_minor = key != Key::C;
      request.character = SubjectCharacter::Noble;
      request.target_bars = 128;
      request.seed = seed;
      HarnessFixture fixture;
      ASSERT_EQ(buildFormFixture(request, &fixture), FormDirectorStatus::Ok);
      ComposeResult result = Composer{}.run(fixture.material, fixture.harmony, fixture.voice_plan);
      std::uint32_t total = 0;
      for (const NoteEvent& note : result.notes)
        total = std::max(total, note.start_tick + note.duration);
      OrnamentParams ornament;
      ornament.character = request.character;
      ornament.instrument = InstrumentType::Violin;
      ornament.mode = request.is_minor ? detail::Mode::Minor : detail::Mode::Major;
      ornament.seed = seed;
      ornament.bpm = application::kDefaultBpm;
      resolveFixtureOrnamentContext(fixture, request.form, total, &ornament);
      applyOrnamentPass(result, ornament);
      const auto placement = placeInOutputCompass(
          &result, fixture.material, fixture.harmony, key, InstrumentType::Violin,
          fixture.harmony.ticksPerBar(), [&](const ComposeResult& placed) {
            ValidationReport report =
                Validator{}.validate(placed.notes, placed.provenance, fixture.harmony,
                                     fixture.material, ValidationScope::FinalScore);
            applyCounterpointBudget(request.form, &report);
            return report.failures.empty();
          });
      ASSERT_TRUE(placement.has_value());
      for (const NoteEvent& note : result.notes) {
        const int sounding =
            static_cast<int>(note.pitch) + keyTranspositionSemitones(key) + placement->octave_shift;
        EXPECT_GE(sounding, static_cast<int>(range.low))
            << "key " << static_cast<int>(key) << " seed " << seed;
        EXPECT_LE(sounding, static_cast<int>(range.high))
            << "key " << static_cast<int>(key) << " seed " << seed;
      }
    }
  }
}

}  // namespace
}  // namespace bach::composer
