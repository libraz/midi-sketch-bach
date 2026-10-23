// Tests for midi/velocity_curve.h -- phrase-aware velocity computation.

#include "midi/velocity_curve.h"

#include <gtest/gtest.h>

#include <vector>

#include "core/basic_types.h"

namespace bach {
namespace {

// ---------------------------------------------------------------------------
// computeVelocity -- beat emphasis
// ---------------------------------------------------------------------------

TEST(VelocityCurveTest, DownbeatHigherThanWeakBeat) {
  std::vector<Tick> no_cadences;
  uint8_t downbeat_vel = computeVelocity(0, no_cadences);          // Beat 0
  uint8_t weak_vel = computeVelocity(kTicksPerBeat, no_cadences);  // Beat 1

  EXPECT_GT(downbeat_vel, weak_vel);
}

TEST(VelocityCurveTest, Beat2StrongerThanBeat3) {
  std::vector<Tick> no_cadences;
  uint8_t beat2_vel = computeVelocity(kTicksPerBeat * 2, no_cadences);
  uint8_t beat3_vel = computeVelocity(kTicksPerBeat * 3, no_cadences);

  EXPECT_GT(beat2_vel, beat3_vel);
}

TEST(VelocityCurveTest, SarabandeTripleUsesItsThreeBeatHierarchy) {
  const std::vector<Tick> no_cadences;
  const TimeSignature sarabande_meter{3, 4};

  EXPECT_EQ(computeVelocity(0, no_cadences, 0, sarabande_meter, MeterProfile::SarabandeTriple), 88);
  EXPECT_EQ(computeVelocity(kTicksPerBeat, no_cadences, 0, sarabande_meter,
                            MeterProfile::SarabandeTriple),
            83);
  EXPECT_EQ(computeVelocity(kTicksPerBeat * 2, no_cadences, 0, sarabande_meter,
                            MeterProfile::SarabandeTriple),
            73);
}

TEST(VelocityCurveTest, StandardTripleLeavesBothNonDownbeatsWeak) {
  const std::vector<Tick> no_cadences;
  const TimeSignature triple_meter{3, 4};

  EXPECT_EQ(
      computeVelocity(kTicksPerBeat, no_cadences, 0, triple_meter, MeterProfile::StandardTriple),
      73);
  EXPECT_EQ(computeVelocity(kTicksPerBeat * 2, no_cadences, 0, triple_meter,
                            MeterProfile::StandardTriple),
            73);
}

TEST(VelocityCurveTest, FourFourAccentsOnlyTheFirstBarOfAPhrase) {
  std::vector<NoteEvent> notes;
  for (Tick tick : {0u, 480u, 960u, 1440u, 1919u, 1920u}) {
    NoteEvent note;
    note.start_tick = tick;
    note.duration = 1;
    note.pitch = 60;
    notes.push_back(note);
  }

  applyVelocityCurve(notes, InstrumentType::Violin, {});

  const std::vector<std::uint8_t> expected = {88, 73, 83, 73, 73, 80};
  ASSERT_EQ(notes.size(), expected.size());
  for (std::size_t idx = 0; idx < expected.size(); ++idx) {
    EXPECT_EQ(notes[idx].velocity, expected[idx]) << "tick=" << notes[idx].start_tick;
  }
}

TEST(VelocityCurveTest, ThreeFourPhraseStartsAtCadenceAndSpansNotatedBar) {
  std::vector<NoteEvent> notes;
  for (Tick tick : {1439u, 1440u, 1920u, 2400u, 2880u}) {
    NoteEvent note;
    note.start_tick = tick;
    note.duration = 1;
    note.pitch = 60;
    notes.push_back(note);
  }

  applyVelocityCurve(notes, InstrumentType::Violin, {1440u}, {3, 4}, MeterProfile::SarabandeTriple);

  const std::vector<std::uint8_t> expected = {70, 88, 83, 73, 80};
  ASSERT_EQ(notes.size(), expected.size());
  for (std::size_t idx = 0; idx < expected.size(); ++idx) {
    EXPECT_EQ(notes[idx].velocity, expected[idx]) << "tick=" << notes[idx].start_tick;
  }
}

TEST(VelocityCurveTest, VelocityWithinRange) {
  std::vector<Tick> no_cadences;
  for (Tick tick = 0; tick < kTicksPerBar * 4; tick += kTicksPerBeat) {
    uint8_t vel = computeVelocity(tick, no_cadences);
    EXPECT_GE(vel, 50);
    EXPECT_LE(vel, 110);
  }
}

// ---------------------------------------------------------------------------
// applyVelocityCurve -- organ unchanged
// ---------------------------------------------------------------------------

TEST(VelocityCurveTest, OrganNotModified) {
  std::vector<NoteEvent> notes;
  NoteEvent note;
  note.start_tick = 0;
  note.duration = kTicksPerBeat;
  note.pitch = 60;
  note.velocity = 80;
  notes.push_back(note);

  applyVelocityCurve(notes, InstrumentType::Organ, {});

  EXPECT_EQ(notes[0].velocity, 80);
}

TEST(VelocityCurveTest, HarpsichordNotModified) {
  std::vector<NoteEvent> notes(1);
  notes[0].start_tick = 0;
  notes[0].duration = kTicksPerBeat;
  notes[0].pitch = 60;
  notes[0].velocity = 80;

  applyVelocityCurve(notes, InstrumentType::Harpsichord, {});

  EXPECT_EQ(notes[0].velocity, 80);
}

TEST(VelocityCurveTest, ViolinGetsModifiedVelocity) {
  std::vector<NoteEvent> notes;
  for (int idx = 0; idx < 4; ++idx) {
    NoteEvent note;
    note.start_tick = static_cast<Tick>(idx) * kTicksPerBeat;
    note.duration = kTicksPerBeat;
    note.pitch = 60;
    note.velocity = 80;  // Default organ velocity
    notes.push_back(note);
  }

  applyVelocityCurve(notes, InstrumentType::Violin, {});

  // Velocity should no longer all be 80.
  bool any_different = false;
  for (const auto& note : notes) {
    if (note.velocity != 80)
      any_different = true;
  }
  EXPECT_TRUE(any_different) << "Violin notes should have varied velocity";
}

// ---------------------------------------------------------------------------
// Pre-cadence diminuendo
// ---------------------------------------------------------------------------

TEST(VelocityCurveTest, PreCadenceDiminuendo) {
  Tick cadence_tick = kTicksPerBar * 4;
  std::vector<Tick> cadences = {cadence_tick};

  // Note 2 beats before cadence should be slightly softer.
  uint8_t pre_cadence_vel = computeVelocity(cadence_tick - kTicksPerBeat * 2, cadences);
  // Note well before cadence.
  uint8_t normal_vel = computeVelocity(0, cadences);

  // Pre-cadence gets base + beat emphasis - 3.
  // Both are on beat 0, so beat emphasis is the same.
  // pre_cadence = 70 + 10 - 3 = 77, normal = 70 + 10 + 8 (phrase start) = 88.
  EXPECT_LT(pre_cadence_vel, normal_vel);
}

TEST(VelocityCurveTest, PhraseStartAccentOnlyOnFirstBarAfterBoundary) {
  const std::vector<Tick> cadences = {kTicksPerBar * 4};
  std::vector<NoteEvent> notes;
  for (const Tick bar : {Tick{0}, Tick{1}, Tick{3}, Tick{4}, Tick{5}, Tick{7}}) {
    NoteEvent note;
    note.start_tick = bar * kTicksPerBar;
    note.duration = kTicksPerBeat;
    note.pitch = 60;
    note.velocity = 80;
    notes.push_back(note);
  }

  applyVelocityCurve(notes, InstrumentType::Violin, cadences);

  // Downbeat = 70 + 10; the first bar of a phrase adds 8.
  EXPECT_EQ(notes[0].velocity, 88);  // bar 0: piece start
  EXPECT_EQ(notes[1].velocity, 80);  // bar 1: later bar of the same phrase
  EXPECT_EQ(notes[2].velocity, 80);  // bar 3
  EXPECT_EQ(notes[3].velocity, 88);  // bar 4: first bar after the cadence tick
  EXPECT_EQ(notes[4].velocity, 80);  // bar 5
  EXPECT_EQ(notes[5].velocity, 80);  // bar 7
}

}  // namespace
}  // namespace bach
