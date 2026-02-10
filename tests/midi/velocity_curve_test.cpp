// Tests for midi/velocity_curve.h -- phrase-aware velocity computation.

#include "midi/velocity_curve.h"

#include <gtest/gtest.h>

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
    if (note.velocity != 80) any_different = true;
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
  uint8_t pre_cadence_vel =
      computeVelocity(cadence_tick - kTicksPerBeat * 2, cadences);
  // Note well before cadence.
  uint8_t normal_vel = computeVelocity(0, cadences);

  // Pre-cadence gets base + beat emphasis - 3.
  // Both are on beat 0, so beat emphasis is the same.
  // pre_cadence = 70 + 10 - 3 = 77, normal = 70 + 10 + 8 (phrase start) = 88.
  EXPECT_LT(pre_cadence_vel, normal_vel);
}

}  // namespace
}  // namespace bach
