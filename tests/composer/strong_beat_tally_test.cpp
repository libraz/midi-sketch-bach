#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <vector>

#include "composer/harmonic_plan.h"
#include "composer/json_export.h"
#include "composer/material.h"
#include "composer/provenance.h"
#include "composer/validation.h"
#include "composer/validator.h"
#include "core/basic_types.h"

namespace bach::composer {
namespace {

constexpr Tick kBar = kTicksPerBar;
constexpr Tick kBeat = kTicksPerBeat;

ChordEvent majorChord(Tick start, std::uint8_t root_pc) {
  ChordEvent chord;
  chord.start_tick = start;
  chord.root_pc = root_pc;
  chord.quality = ChordQuality::Major;
  return chord;
}

HarmonicPlan planOf(const std::vector<std::uint8_t>& roots) {
  HarmonicPlan plan;
  plan.tonic_pc = 0;
  plan.is_minor = false;
  for (std::size_t bar = 0; bar < roots.size(); ++bar)
    plan.chords.push_back(majorChord(static_cast<Tick>(bar) * kBar, roots[bar]));
  return plan;
}

NoteEvent makeNote(Tick start, Tick dur, std::uint8_t pitch, VoiceId voice) {
  NoteEvent note;
  note.start_tick = start;
  note.duration = dur;
  note.pitch = pitch;
  note.voice = voice;
  return note;
}

std::vector<NoteProvenance> composeProvenance(std::size_t count) {
  return std::vector<NoteProvenance>(count);
}

StrongBeatTally tallyOf(const std::vector<NoteEvent>& notes, const HarmonicPlan& plan,
                        const std::vector<SuspensionPattern>& patterns = {},
                        const std::vector<PedalPointDecl>& pedals = {}) {
  return computeStrongBeatTally(notes, composeProvenance(notes.size()), plan, patterns, pedals);
}

// Plan: bar 0 C major, bar 1 G major. A 4-3 on voice 0 over a G in the bass.
HarmonicPlan suspensionPlan() {
  return planOf({0, 7});
}

SuspensionPattern fourThree() {
  return {SuspensionType::Sus4_3, kBeat * 2, kBar, kBar + kBeat * 2, 72, 72, 71, 0};
}

std::vector<NoteEvent> bassForSuspension() {
  return {makeNote(0, kBar, 64, 1), makeNote(kBar, kBar, 55, 1)};
}

}  // namespace

TEST(StrongBeatTallyTest, TiedAndRestruckSuspensionAgreeOnSoundingAndDeclaration) {
  std::vector<NoteEvent> tied = bassForSuspension();
  tied.push_back(makeNote(kBeat * 2, kBar, 72, 0));
  tied.push_back(makeNote(kBar + kBeat * 2, kBeat * 2, 71, 0));

  std::vector<NoteEvent> restruck = bassForSuspension();
  restruck.push_back(makeNote(kBeat * 2, kBeat * 2, 72, 0));
  restruck.push_back(makeNote(kBar, kBeat * 2, 72, 0));
  restruck.push_back(makeNote(kBar + kBeat * 2, kBeat * 2, 71, 0));

  const std::vector<SuspensionPattern> patterns = {fourThree()};
  const StrongBeatTally tied_tally = tallyOf(tied, suspensionPlan(), patterns);
  const StrongBeatTally restruck_tally = tallyOf(restruck, suspensionPlan(), patterns);

  EXPECT_EQ(tied_tally.sounding, 1);
  EXPECT_EQ(restruck_tally.sounding, 1);
  EXPECT_EQ(tied_tally.declared_suspension, 1);
  EXPECT_EQ(restruck_tally.declared_suspension, 1);
  EXPECT_EQ(tied_tally.held_over, 1);
  EXPECT_EQ(restruck_tally.held_over, 0);
  EXPECT_EQ(tied_tally.onset, 0);
  EXPECT_EQ(restruck_tally.onset, 1);
}

TEST(StrongBeatTallyTest, SuspensionThatDoesNotResolveByStepIsNotDeclared) {
  std::vector<NoteEvent> notes = bassForSuspension();
  notes.push_back(makeNote(kBeat * 2, kBar, 72, 0));
  notes.push_back(makeNote(kBar + kBeat * 2, kBeat * 2, 74, 0));
  const StrongBeatTally tally = tallyOf(notes, suspensionPlan(), {fourThree()});
  EXPECT_EQ(tally.sounding, 1);
  EXPECT_EQ(tally.declared_suspension, 0);
}

TEST(StrongBeatTallyTest, HeldBassNonChordToneCountsAsDeclaredPedal) {
  const HarmonicPlan plan = planOf({0, 7, 5, 0});
  const std::vector<NoteEvent> notes = {
      makeNote(0, 4 * kBar, 36, 1),    makeNote(0, kBar, 64, 0),        makeNote(kBar, kBar, 62, 0),
      makeNote(2 * kBar, kBar, 65, 0), makeNote(3 * kBar, kBar, 64, 0),
  };
  PedalPointDecl pedal;
  pedal.voice = 1;
  pedal.start_tick = 0;
  pedal.duration = 4 * kBar;
  pedal.pitch = 36;

  // C is foreign to G major, so the pedal sounds against it at the bar head and beat 3.
  const StrongBeatTally declared = tallyOf(notes, plan, {}, {pedal});
  EXPECT_EQ(declared.sounding, 2);
  EXPECT_EQ(declared.held_over, 2);
  EXPECT_EQ(declared.declared_pedal, 2);
  EXPECT_EQ(declared.onset, 0);

  const StrongBeatTally undeclared = tallyOf(notes, plan);
  EXPECT_EQ(undeclared.sounding, 2);
  EXPECT_EQ(undeclared.held_over, 2);
  EXPECT_EQ(undeclared.declared_pedal, 0);
}

TEST(StrongBeatTallyTest, OnlyTheMovingVoiceIsCountedAgainstAHeldChordTone) {
  const HarmonicPlan plan = planOf({0, 0});
  const std::vector<NoteEvent> notes = {
      makeNote(0, 2 * kBar, 60, 1),
      makeNote(0, 2 * kBeat, 64, 0),
      makeNote(2 * kBeat, kBeat, 65, 0),
      makeNote(3 * kBeat, kBeat, 64, 0),
  };
  const StrongBeatTally tally = tallyOf(notes, plan);
  EXPECT_EQ(tally.sounding, 1);
  EXPECT_EQ(tally.onset, 1);
  EXPECT_EQ(tally.held_over, 0);
}

TEST(StrongBeatTallyTest, UndeclaredHeldOverNonChordToneIsCountedAsHeldOver) {
  const HarmonicPlan plan = planOf({0, 7});
  const std::vector<NoteEvent> notes = {
      makeNote(3 * kBeat, 2 * kBeat, 60, 0),
      makeNote(kBar + kBeat, kBeat, 59, 0),
      makeNote(0, kBar, 48, 1),
      makeNote(kBar, kBar, 43, 1),
  };
  const StrongBeatTally tally = tallyOf(notes, plan);
  EXPECT_EQ(tally.onset, 0);
  EXPECT_EQ(tally.sounding, 1);
  EXPECT_EQ(tally.held_over, 1);
  EXPECT_EQ(tally.declared_suspension, 0);
  EXPECT_EQ(tally.declared_pedal, 0);
}

TEST(StrongBeatTallyTest, NoteEndingOnTheAccentTickIsNotCounted) {
  // G major in both bars: C is foreign. It sounds through beat 3 and ends on the bar head.
  const HarmonicPlan plan = planOf({7, 7});
  const std::vector<NoteEvent> notes = {makeNote(kBeat, 3 * kBeat, 60, 0)};
  const StrongBeatTally tally = tallyOf(notes, plan);
  EXPECT_EQ(tally.sounding, 1);
  EXPECT_EQ(tally.held_over, 1);
  EXPECT_EQ(tally.onset, 0);
}

TEST(StrongBeatTallyTest, OrnamentSourceIsFlaggedInsideSounding) {
  const HarmonicPlan plan = planOf({0});
  const std::vector<NoteEvent> notes = {makeNote(0, kBeat, 62, 0),
                                        makeNote(2 * kBeat, kBeat, 62, 1)};
  std::vector<NoteProvenance> provenance = composeProvenance(notes.size());
  provenance[0].source = NoteSource::Ornament;
  const StrongBeatTally tally = computeStrongBeatTally(notes, provenance, plan, {}, {});
  EXPECT_EQ(tally.sounding, 2);
  EXPECT_EQ(tally.onset, 2);
  EXPECT_EQ(tally.ornament, 1);
}

TEST(StrongBeatTallyTest, ValidatorFillsTheSurfaceMatchingItsScope) {
  const HarmonicPlan plan = planOf({0});
  const std::vector<NoteEvent> notes = {makeNote(0, 2 * kBeat, 62, 0)};
  const std::vector<NoteProvenance> provenance = composeProvenance(notes.size());
  const Material material;
  const ValidationReport generation =
      Validator{}.validate(notes, provenance, plan, material, ValidationScope::Generation);
  const ValidationReport final_score =
      Validator{}.validate(notes, provenance, plan, material, ValidationScope::FinalScore);
  ASSERT_EQ(generation.strong_beat_tally.size(), 1u);
  ASSERT_EQ(final_score.strong_beat_tally.size(), 1u);
  EXPECT_EQ(generation.strong_beat_tally[0].surface, StrongBeatSurface::Composed);
  EXPECT_EQ(final_score.strong_beat_tally[0].surface, StrongBeatSurface::Shipped);
  EXPECT_EQ(generation.strong_beat_tally[0].sounding, 1);
}

TEST(StrongBeatTallyTest, GeneratedJsonCarriesBothSurfaces) {
  ValidationReport report;
  StrongBeatTally composed;
  composed.surface = StrongBeatSurface::Composed;
  composed.onset = 1;
  composed.sounding = 2;
  composed.held_over = 3;
  composed.declared_suspension = 4;
  composed.declared_pedal = 5;
  composed.ornament = 6;
  StrongBeatTally shipped = composed;
  shipped.surface = StrongBeatSurface::Shipped;
  shipped.sounding = 9;
  report.strong_beat_tally = {composed, shipped};

  const std::string json = emitGeneratedJson({}, report);
  EXPECT_NE(json.find("\"strong_beat_tally\":[{\"surface\":\"composed\",\"onset\":1,"
                      "\"sounding\":2,\"held_over\":3,\"declared_suspension\":4,"
                      "\"declared_pedal\":5,\"ornament\":6},{\"surface\":\"shipped\","
                      "\"onset\":1,\"sounding\":9,"),
            std::string::npos);
  EXPECT_NE(json.find("\"info\":"), std::string::npos);
}

}  // namespace bach::composer
