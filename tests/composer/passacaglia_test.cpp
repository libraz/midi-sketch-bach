// Organ Passacaglia (BWV582) tests.
//
// Covers the three halves of the Passacaglia foundation (modeled on the
// chaconne arch with an 8-bar period and a climax marker instead of a
// per-variation VariationRole):
//   1. CandidateSearch replays the immutable 8-bar ground bass period-tiled
//      under a PassacagliaGround span (every note stamps
//      PassacagliaGroundReplayed) and replays a PassacagliaVariation block
//      verbatim under a PassacagliaVariation span (every note stamps
//      VariationApplied; every note of a block flagged is_climax additionally
//      stamps ClimaxPlaced).
//   2. The Validator's passacaglia_ground_immutable rule (each replayed
//      bar-head tone matches the Material::passacaglia_ground skeleton;
//      StructuralFail otherwise) fires on a mutated bar head, stays silent on
//      clean or rhythmically subdivided material, and stays inert when the
//      ground is empty.
//   3. The Passacaglia fixture runs through the full Composer cleanly for every seed
//      family (seed % 4 selects the scalar-wave start offset) and stamps all
//      three Passacaglia bits in provenance.

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

#include "composer/candidate_search.h"
#include "composer/composer.h"
#include "composer/harmonic_plan.h"
#include "composer/harness_fixture.h"
#include "composer/material.h"
#include "composer/provenance.h"
#include "composer/span.h"
#include "composer/validator.h"
#include "composer/voice_intent.h"
#include "core/basic_types.h"

namespace bach::composer {
namespace {

// Single C-minor chord covering the whole piece (the Passacaglia is internally
// in C minor; transposition happens only at MIDI output).
HarmonicPlan cMinorWhole() {
  HarmonicPlan plan;
  plan.tonic_pc = 0;
  plan.is_minor = true;
  ChordEvent c;
  c.start_tick = 0;
  c.root_pc = 0;
  c.quality = ChordQuality::Minor;
  plan.chords.push_back(c);
  return plan;
}

MaterialNote mnote(Tick start, Tick dur, std::uint8_t pitch) {
  MaterialNote n;
  n.start_tick = start;
  n.duration = dur;
  n.pitch = pitch;
  return n;
}

NoteEvent makeNote(Tick start, Tick dur, std::uint8_t pitch, VoiceId voice) {
  NoteEvent n;
  n.start_tick = start;
  n.duration = dur;
  n.pitch = pitch;
  n.voice = voice;
  n.velocity = 80;
  return n;
}

// Ground provenance: Material source carrying the PassacagliaGroundReplayed bit,
// mirroring what CandidateSearch stamps on a PassacagliaGround note.
NoteProvenance groundProv() {
  NoteProvenance p;
  p.voice_intent = VoiceIntent::PassacagliaGround;
  p.source = NoteSource::Material;
  p.satisfied_rules = (ruleBitMask(RuleBit::PassacagliaGroundReplayed));
  return p;
}

bool hasRule(const ValidationReport& r, const std::string& rule_id) {
  for (const auto& f : r.failures) {
    if (f.rule_id == rule_id)
      return true;
  }
  return false;
}

bool hasRuleKind(const ValidationReport& r, const std::string& rule_id, FailKind kind) {
  for (const auto& f : r.failures) {
    if (f.rule_id == rule_id && f.kind == kind)
      return true;
  }
  return false;
}

constexpr RuleIdMask bit(RuleBit b) {
  return ruleBitMask(b);
}

}  // namespace

// --- 1. CandidateSearch verbatim replay ------------------------------------

// A 2-note ground bass with period P, replayed under a PassacagliaGround span
// that covers two periods: the cycle is laid down twice (4 notes), pitches
// repeat, and ticks are shifted by whole periods. Every note carries
// PassacagliaGroundReplayed.
TEST(PassacagliaTest, GroundCarrierTilesGroundBassAcrossPeriods) {
  Material material;
  const Tick period = 2 * kTicksPerBeat;  // two-beat cycle.
  // Cycle-relative ground: C3 (48) at tick 0, G2 (43) at tick one-beat.
  material.passacaglia_ground.push_back(mnote(0, kTicksPerBeat, 48));
  material.passacaglia_ground.push_back(mnote(kTicksPerBeat, kTicksPerBeat, 43));
  material.passacaglia_ground_period = period;

  Span span;
  span.id = 1;
  span.start_tick = 0;
  span.end_tick = 2 * period;  // exactly two cycles.
  span.voice = 1;
  span.intent = VoiceIntent::PassacagliaGround;

  const auto cands = CandidateSearch{}.enumerate(span, cMinorWhole(), material, CandidateContext{});

  ASSERT_EQ(cands.size(), 4u);
  // Cycle 0.
  EXPECT_EQ(cands[0].start_tick, 0);
  EXPECT_EQ(cands[0].pitch, 48u);
  EXPECT_EQ(cands[1].start_tick, kTicksPerBeat);
  EXPECT_EQ(cands[1].pitch, 43u);
  // Cycle 1: same pitches, shifted by one period.
  EXPECT_EQ(cands[2].start_tick, period);
  EXPECT_EQ(cands[2].pitch, 48u);
  EXPECT_EQ(cands[3].start_tick, period + kTicksPerBeat);
  EXPECT_EQ(cands[3].pitch, 43u);
  for (std::size_t i = 0; i < cands.size(); ++i) {
    EXPECT_NE(cands[i].satisfied_rules & bit(RuleBit::PassacagliaGroundReplayed), 0u)
        << "ground note " << i << " missing PassacagliaGroundReplayed";
  }
}

// Late-cycle rhythmic intensification: from passacaglia_ground_split_from on,
// each tiled ground note longer than a beat is restated as repeated same-pitch
// quarters; earlier cycles replay the long notes untouched, and the piece's
// FINAL ground note stays unsplit (the bass joins the held closing chord).
// Every emitted note still carries PassacagliaGroundReplayed and the original
// pitches.
TEST(PassacagliaTest, GroundCarrierSplitsLateCyclesIntoQuarters) {
  Material material;
  const Tick period = 2 * kTicksPerBar;  // two full-bar notes per cycle.
  material.passacaglia_ground.push_back(mnote(0, kTicksPerBar, 48));
  material.passacaglia_ground.push_back(mnote(kTicksPerBar, kTicksPerBar, 43));
  material.passacaglia_ground_period = period;
  material.passacaglia_ground_split_from = period;  // split from cycle 1 on.

  Span span;
  span.id = 1;
  span.start_tick = 0;
  span.end_tick = 2 * period;  // exactly two cycles.
  span.voice = 2;
  span.intent = VoiceIntent::PassacagliaGround;

  const auto cands = CandidateSearch{}.enumerate(span, cMinorWhole(), material, CandidateContext{});

  // Cycle 0: two untouched full-bar notes. Cycle 1: the first bar restated as
  // four same-pitch quarters (1920 / 480); the second bar is the piece's final
  // ground note and stays one unsplit full-bar note.
  ASSERT_EQ(cands.size(), 2u + 4u + 1u);
  EXPECT_EQ(cands[0].duration, kTicksPerBar);
  EXPECT_EQ(cands[0].pitch, 48u);
  EXPECT_EQ(cands[1].duration, kTicksPerBar);
  EXPECT_EQ(cands[1].pitch, 43u);
  for (std::size_t i = 2; i < 6; ++i) {
    EXPECT_EQ(cands[i].duration, kTicksPerBeat) << "late-cycle note " << i << " must be a quarter";
    EXPECT_EQ(cands[i].pitch, 48u) << "pitch must never change";
    EXPECT_EQ(cands[i].start_tick, period + static_cast<Tick>(i - 2) * kTicksPerBeat);
    EXPECT_NE(cands[i].satisfied_rules & bit(RuleBit::PassacagliaGroundReplayed), 0u);
  }
  EXPECT_EQ(cands[6].duration, kTicksPerBar) << "final ground note must stay unsplit";
  EXPECT_EQ(cands[6].pitch, 43u);
  EXPECT_EQ(cands[6].start_tick, period + kTicksPerBar);
  EXPECT_NE(cands[6].satisfied_rules & bit(RuleBit::PassacagliaGroundReplayed), 0u);
}

// A non-climax variation: VariationApplied on every note, ClimaxPlaced on none.
TEST(PassacagliaTest, VariationCarrierStampsVariationAppliedWithoutClimax) {
  Material material;
  PassacagliaVariation v0;
  v0.voice = 0;
  v0.start_tick = 0;
  v0.end_tick = kTicksPerBar;
  v0.density_level = 0;
  v0.is_climax = false;
  v0.notes.push_back(mnote(0, kTicksPerBeat, 60));
  v0.notes.push_back(mnote(kTicksPerBeat, kTicksPerBeat, 62));
  material.passacaglia_variations.push_back(v0);

  Span s0;
  s0.id = 1;
  s0.start_tick = 0;
  s0.end_tick = kTicksPerBar;
  s0.voice = 0;
  s0.intent = VoiceIntent::PassacagliaVariation;
  const auto c0 = CandidateSearch{}.enumerate(s0, cMinorWhole(), material, CandidateContext{});
  ASSERT_EQ(c0.size(), 2u);
  for (const auto& c : c0) {
    EXPECT_NE(c.satisfied_rules & bit(RuleBit::VariationApplied), 0u);
    EXPECT_EQ(c.satisfied_rules & bit(RuleBit::ClimaxPlaced), 0u)
        << "non-climax variation must not stamp ClimaxPlaced";
  }
}

// A climax variation: every note carries BOTH VariationApplied and ClimaxPlaced.
TEST(PassacagliaTest, VariationCarrierStampsClimaxPlacedOnClimaxBlock) {
  Material material;
  PassacagliaVariation v0;
  v0.voice = 0;
  v0.start_tick = kTicksPerBar;
  v0.end_tick = 2 * kTicksPerBar;
  v0.density_level = 2;
  v0.is_climax = true;
  v0.notes.push_back(mnote(kTicksPerBar, kTicksPerBeat, 64));
  v0.notes.push_back(mnote(kTicksPerBar + kTicksPerBeat, kTicksPerBeat, 65));
  material.passacaglia_variations.push_back(v0);

  Span s0;
  s0.id = 1;
  s0.start_tick = kTicksPerBar;
  s0.end_tick = 2 * kTicksPerBar;
  s0.voice = 0;
  s0.intent = VoiceIntent::PassacagliaVariation;
  const auto c0 = CandidateSearch{}.enumerate(s0, cMinorWhole(), material, CandidateContext{});
  ASSERT_EQ(c0.size(), 2u);
  for (const auto& c : c0) {
    EXPECT_NE(c.satisfied_rules & bit(RuleBit::VariationApplied), 0u);
    EXPECT_NE(c.satisfied_rules & bit(RuleBit::ClimaxPlaced), 0u)
        << "climax variation must stamp ClimaxPlaced on every note";
  }
}

// --- 2. passacaglia_ground_immutable ---------------------------------------

// Two period-tiled, identical cycles of the ground bass: the rule stays silent.
TEST(PassacagliaTest, GroundImmutablePassesOnIdenticalCycles) {
  Material material;
  material.passacaglia_ground.push_back(mnote(0, kTicksPerBeat, 48));
  material.passacaglia_ground.push_back(mnote(kTicksPerBeat, kTicksPerBeat, 43));
  material.passacaglia_ground_period = 2 * kTicksPerBeat;

  std::vector<NoteEvent> notes;
  std::vector<NoteProvenance> prov;
  const std::uint8_t cycle[2] = {48, 43};
  for (int c = 0; c < 2; ++c) {
    for (int k = 0; k < 2; ++k) {
      notes.push_back(
          makeNote(static_cast<Tick>(c) * 2 * kTicksPerBeat + static_cast<Tick>(k) * kTicksPerBeat,
                   kTicksPerBeat, cycle[k], 1));
      prov.push_back(groundProv());
    }
  }

  const ValidationReport r = Validator{}.validate(notes, prov, cMinorWhole(), material);
  EXPECT_FALSE(hasRule(r, "passacaglia_ground_immutable"));
}

// Mutate a stamped ground note's pitch on a bar head: the bar-head skeleton no
// longer matches the canonical ground, so the rule fires with a StructuralFail.
TEST(PassacagliaTest, GroundImmutableFailsOnMutatedBarHead) {
  Material material;
  material.passacaglia_ground.push_back(mnote(0, kTicksPerBeat, 48));
  material.passacaglia_ground.push_back(mnote(kTicksPerBeat, kTicksPerBeat, 43));
  material.passacaglia_ground_period = 2 * kTicksPerBeat;

  std::vector<NoteEvent> notes;
  std::vector<NoteProvenance> prov;
  const std::uint8_t cycle[2] = {48, 43};
  for (int c = 0; c < 4; ++c) {
    for (int k = 0; k < 2; ++k) {
      notes.push_back(
          makeNote(static_cast<Tick>(c) * 2 * kTicksPerBeat + static_cast<Tick>(k) * kTicksPerBeat,
                   kTicksPerBeat, cycle[k], 1));
      prov.push_back(groundProv());
    }
  }
  // Corrupt the bar-head note at tick 1920 (cycle 2's downbeat): C3 (48) -> Bb2.
  notes[4].pitch = 46;

  const ValidationReport r = Validator{}.validate(notes, prov, cMinorWhole(), material);
  EXPECT_TRUE(hasRuleKind(r, "passacaglia_ground_immutable", FailKind::StructuralFail));
}

// Subdividing a ground bar into repeated same-pitch notes keeps every bar-head
// tone intact, so the rule stays silent (rhythm is free below the bar head).
TEST(PassacagliaTest, GroundImmutablePassesOnSubdividedBar) {
  Material material;
  // One full-bar note per 4/4 bar, two-bar cycle.
  material.passacaglia_ground.push_back(mnote(0, kTicksPerBar, 48));
  material.passacaglia_ground.push_back(mnote(kTicksPerBar, kTicksPerBar, 43));
  material.passacaglia_ground_period = 2 * kTicksPerBar;

  std::vector<NoteEvent> notes;
  std::vector<NoteProvenance> prov;
  const std::uint8_t cycle[2] = {48, 43};
  for (int bar = 0; bar < 4; ++bar) {
    // Each bar is split into four same-pitch quarters (martellato restatement).
    for (int beat = 0; beat < 4; ++beat) {
      notes.push_back(
          makeNote(static_cast<Tick>(bar) * kTicksPerBar + static_cast<Tick>(beat) * kTicksPerBeat,
                   kTicksPerBeat, cycle[bar % 2], 1));
      prov.push_back(groundProv());
    }
  }

  const ValidationReport r = Validator{}.validate(notes, prov, cMinorWhole(), material);
  EXPECT_FALSE(hasRule(r, "passacaglia_ground_immutable"));
}

// Inert when there is no passacaglia ground declared: even a note carrying the
// PassacagliaGroundReplayed bit cannot trip the rule.
TEST(PassacagliaTest, GroundImmutableInertWhenGroundEmpty) {
  Material material;  // no passacaglia_ground.
  std::vector<NoteEvent> notes = {makeNote(0, kTicksPerBeat, 48, 1)};
  std::vector<NoteProvenance> prov = {groundProv()};
  const ValidationReport r = Validator{}.validate(notes, prov, cMinorWhole(), material);
  EXPECT_FALSE(hasRule(r, "passacaglia_ground_immutable"));
}

// --- 3. Passacaglia fixture integration ----------------------------------------

// The BWV582 passacaglia fixture must run through the full Composer cleanly for
// every seed family (seed % 4 selects the scalar-wave start offset): no
// validator failure, two voices, and all three Passacaglia bits present
// somewhere in the provenance. ClimaxPlaced fires only on the last (climax)
// variation block.
TEST(PassacagliaTest, PassacagliaFixtureValidatesCleanAndStampsAllPassacagliaBits) {
  for (int seed : {0, 1, 2, 3}) {
    const HarnessFixture fx = buildHarnessFixture(HarnessPhase::Passacaglia, seed);
    const ComposeResult r = Composer{}.run(fx.material, fx.harmony, fx.voice_plan);

    EXPECT_TRUE(r.validation.failures.empty())
        << "seed " << seed << " produced " << r.validation.failures.size() << " failure(s); first="
        << (r.validation.failures.empty() ? "" : r.validation.failures.front().rule_id);
    ASSERT_FALSE(r.notes.empty());
    ASSERT_EQ(r.notes.size(), r.provenance.size());
    EXPECT_EQ(fx.voice_plan.num_voices, 2);

    bool saw_ground = false;
    bool saw_variation = false;
    bool saw_climax = false;
    for (const auto& p : r.provenance) {
      if (p.satisfied_rules & bit(RuleBit::PassacagliaGroundReplayed))
        saw_ground = true;
      if (p.satisfied_rules & bit(RuleBit::VariationApplied))
        saw_variation = true;
      if (p.satisfied_rules & bit(RuleBit::ClimaxPlaced))
        saw_climax = true;
    }
    EXPECT_TRUE(saw_ground) << "seed " << seed << " missing PassacagliaGroundReplayed";
    EXPECT_TRUE(saw_variation) << "seed " << seed << " missing VariationApplied";
    EXPECT_TRUE(saw_climax) << "seed " << seed << " missing ClimaxPlaced";
  }
}

}  // namespace bach::composer
