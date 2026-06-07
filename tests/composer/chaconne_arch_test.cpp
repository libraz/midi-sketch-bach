// Solo String Arch (BWV1004 Chaconne) tests.
//
// Covers the three halves of the Arch foundation:
//   1. CandidateSearch replays the immutable ground bass period-tiled under a
//      GroundCarrier span (every note stamps GroundBassReplayed) and replays a
//      VariationDecl verbatim under a VariationCarrier span (every note stamps
//      VariationRoleApplied; the first note of a variation whose density tier
//      differs from the immediately preceding variation also stamps
//      TextureDensityShift).
//   2. The Validator's two Arch rules — ground_bass_immutable (every replayed
//      cycle pitch-matches Material::ground_bass; StructuralFail otherwise) and
//      variation_role_ornament_constraint (a Ground-role variation carries no
//      sub-quarter note; MusicalFail otherwise) — fire on violations, stay
//      silent on clean material, and stay inert when ground_bass / variations
//      are empty.
//   3. The Phase16 fixture runs through the full Composer cleanly for every
//      seed family (seed % 4 selects the scalar-wave start offset) and stamps
//      all three Arch bits in provenance.

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

// Single C-minor chord covering the whole piece (the Chaconne is internally in
// C minor; transposition happens only at MIDI output).
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

// Ground-bass provenance: Material source carrying the GroundBassReplayed bit,
// mirroring what CandidateSearch stamps on a GroundCarrier note.
NoteProvenance groundProv() {
  NoteProvenance p;
  p.voice_intent = VoiceIntent::GroundCarrier;
  p.source = NoteSource::Material;
  p.satisfied_rules = (ruleBitMask(RuleBit::GroundBassReplayed));
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

// A 2-note ground bass with period P, replayed under a GroundCarrier span that
// covers two periods: the cycle is laid down twice (4 notes), pitches repeat,
// and ticks are shifted by whole periods. Every note carries GroundBassReplayed.
TEST(ChaconneArchTest, GroundCarrierTilesGroundBassAcrossPeriods) {
  Material material;
  const Tick period = 2 * kTicksPerBeat;  // two-beat cycle.
  // Cycle-relative ground: C3 (48) at tick 0, G2 (43) at tick one-beat.
  material.ground_bass.push_back(mnote(0, kTicksPerBeat, 48));
  material.ground_bass.push_back(mnote(kTicksPerBeat, kTicksPerBeat, 43));
  material.ground_bass_period = period;

  Span span;
  span.id = 1;
  span.start_tick = 0;
  span.end_tick = 2 * period;  // exactly two cycles.
  span.voice = 1;
  span.intent = VoiceIntent::GroundCarrier;

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
    EXPECT_NE(cands[i].satisfied_rules & bit(RuleBit::GroundBassReplayed), 0u)
        << "ground note " << i << " missing GroundBassReplayed";
  }
}

// Two adjacent variations of different density: VariationRoleApplied on every
// note, TextureDensityShift on the first note of each (var 0 always shifts; var
// 1 shifts because its density differs from var 0).
TEST(ChaconneArchTest, VariationCarrierStampsRoleAndDensityShiftOnDensityChange) {
  Material material;
  VariationDecl v0;
  v0.role = VariationRole::Ground;
  v0.voice = 0;
  v0.start_tick = 0;
  v0.end_tick = kTicksPerBar;
  v0.density_level = 0;
  v0.notes.push_back(mnote(0, kTicksPerBeat, 60));
  v0.notes.push_back(mnote(kTicksPerBeat, kTicksPerBeat, 62));
  material.variations.push_back(v0);

  VariationDecl v1;
  v1.role = VariationRole::Respond;
  v1.voice = 0;
  v1.start_tick = kTicksPerBar;
  v1.end_tick = 2 * kTicksPerBar;
  v1.density_level = 1;  // differs from v0.
  v1.notes.push_back(mnote(kTicksPerBar, kTicksPerBeat / 2, 64));
  v1.notes.push_back(mnote(kTicksPerBar + kTicksPerBeat / 2, kTicksPerBeat / 2, 65));
  material.variations.push_back(v1);

  // Enumerate var 0 (its window).
  Span s0;
  s0.id = 1;
  s0.start_tick = 0;
  s0.end_tick = kTicksPerBar;
  s0.voice = 0;
  s0.intent = VoiceIntent::VariationCarrier;
  const auto c0 = CandidateSearch{}.enumerate(s0, cMinorWhole(), material, CandidateContext{});
  ASSERT_EQ(c0.size(), 2u);
  for (const auto& c : c0)
    EXPECT_NE(c.satisfied_rules & bit(RuleBit::VariationRoleApplied), 0u);
  // var 0 (index 0) always shifts; only its first note.
  EXPECT_NE(c0[0].satisfied_rules & bit(RuleBit::TextureDensityShift), 0u);
  EXPECT_EQ(c0[1].satisfied_rules & bit(RuleBit::TextureDensityShift), 0u);

  // Enumerate var 1 (its window).
  Span s1;
  s1.id = 2;
  s1.start_tick = kTicksPerBar;
  s1.end_tick = 2 * kTicksPerBar;
  s1.voice = 0;
  s1.intent = VoiceIntent::VariationCarrier;
  const auto c1 = CandidateSearch{}.enumerate(s1, cMinorWhole(), material, CandidateContext{});
  ASSERT_EQ(c1.size(), 2u);
  for (const auto& c : c1)
    EXPECT_NE(c.satisfied_rules & bit(RuleBit::VariationRoleApplied), 0u);
  // var 1 density (1) differs from var 0 (0), so it shifts on its first note.
  EXPECT_NE(c1[0].satisfied_rules & bit(RuleBit::TextureDensityShift), 0u);
  EXPECT_EQ(c1[1].satisfied_rules & bit(RuleBit::TextureDensityShift), 0u);
}

// An adjacent pair with the SAME density: the second variation must NOT stamp
// TextureDensityShift on its first note (the tier did not change).
TEST(ChaconneArchTest, VariationCarrierSkipsDensityShiftOnSameDensity) {
  Material material;
  VariationDecl v0;
  v0.role = VariationRole::Respond;
  v0.voice = 0;
  v0.start_tick = 0;
  v0.end_tick = kTicksPerBar;
  v0.density_level = 2;
  v0.notes.push_back(mnote(0, kTicksPerBeat, 60));
  material.variations.push_back(v0);

  VariationDecl v1;
  v1.role = VariationRole::Propel;
  v1.voice = 0;
  v1.start_tick = kTicksPerBar;
  v1.end_tick = 2 * kTicksPerBar;
  v1.density_level = 2;  // SAME as v0.
  v1.notes.push_back(mnote(kTicksPerBar, kTicksPerBeat, 64));
  material.variations.push_back(v1);

  Span s1;
  s1.id = 2;
  s1.start_tick = kTicksPerBar;
  s1.end_tick = 2 * kTicksPerBar;
  s1.voice = 0;
  s1.intent = VoiceIntent::VariationCarrier;
  const auto c1 = CandidateSearch{}.enumerate(s1, cMinorWhole(), material, CandidateContext{});
  ASSERT_EQ(c1.size(), 1u);
  EXPECT_NE(c1[0].satisfied_rules & bit(RuleBit::VariationRoleApplied), 0u);
  EXPECT_EQ(c1[0].satisfied_rules & bit(RuleBit::TextureDensityShift), 0u)
      << "same-density adjacent variation must not stamp TextureDensityShift";
}

// --- 2a. ground_bass_immutable ---------------------------------------------

// Two period-tiled, identical cycles of the ground bass: the rule stays silent.
TEST(ChaconneArchTest, GroundBassImmutablePassesOnIdenticalCycles) {
  Material material;
  material.ground_bass.push_back(mnote(0, kTicksPerBeat, 48));
  material.ground_bass.push_back(mnote(kTicksPerBeat, kTicksPerBeat, 43));
  material.ground_bass_period = 2 * kTicksPerBeat;

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
  EXPECT_FALSE(hasRule(r, "ground_bass_immutable"));
}

// Mutate one stamped ground note's pitch: the cycle no longer matches the
// canonical ground, so the rule fires with a StructuralFail.
TEST(ChaconneArchTest, GroundBassImmutableFailsOnMutatedCycle) {
  Material material;
  material.ground_bass.push_back(mnote(0, kTicksPerBeat, 48));
  material.ground_bass.push_back(mnote(kTicksPerBeat, kTicksPerBeat, 43));
  material.ground_bass_period = 2 * kTicksPerBeat;

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
  // Corrupt the second note of cycle 1 (index 3): G2 (43) -> Ab2 (44).
  notes[3].pitch = 44;

  const ValidationReport r = Validator{}.validate(notes, prov, cMinorWhole(), material);
  EXPECT_TRUE(hasRuleKind(r, "ground_bass_immutable", FailKind::StructuralFail));
}

// Inert when there is no ground bass declared: even a note carrying the
// GroundBassReplayed bit cannot trip the rule.
TEST(ChaconneArchTest, GroundBassImmutableInertWhenGroundEmpty) {
  Material material;  // no ground_bass.
  std::vector<NoteEvent> notes = {makeNote(0, kTicksPerBeat, 48, 1)};
  std::vector<NoteProvenance> prov = {groundProv()};
  const ValidationReport r = Validator{}.validate(notes, prov, cMinorWhole(), material);
  EXPECT_FALSE(hasRule(r, "ground_bass_immutable"));
}

// --- 2b. variation_role_ornament_constraint --------------------------------

// A Ground-role variation whose notes are all quarter-or-longer: the rule stays
// silent (no ornamental subdivision).
TEST(ChaconneArchTest, VariationOrnamentConstraintPassesOnQuarterGround) {
  Material material;
  VariationDecl var;
  var.role = VariationRole::Ground;
  var.voice = 0;
  var.start_tick = 0;
  var.end_tick = kTicksPerBar;
  var.density_level = 0;
  for (int beat = 0; beat < 4; ++beat)
    var.notes.push_back(mnote(static_cast<Tick>(beat) * kTicksPerBeat, kTicksPerBeat, 60));
  material.variations.push_back(var);

  const ValidationReport r = Validator{}.validate({}, {}, cMinorWhole(), material);
  EXPECT_FALSE(hasRule(r, "variation_role_ornament_constraint"));
}

// A Ground-role variation containing a sub-quarter (eighth) note: an illegal
// ornament, so the rule fires with a MusicalFail.
TEST(ChaconneArchTest, VariationOrnamentConstraintFailsOnSubQuarterGround) {
  Material material;
  VariationDecl var;
  var.role = VariationRole::Ground;
  var.voice = 0;
  var.start_tick = 0;
  var.end_tick = kTicksPerBar;
  var.density_level = 0;
  var.notes.push_back(mnote(0, kTicksPerBeat, 60));
  // Eighth note (kTicksPerBeat / 2 < kTicksPerBeat) inside a Ground variation.
  var.notes.push_back(mnote(kTicksPerBeat, kTicksPerBeat / 2, 62));
  material.variations.push_back(var);

  const ValidationReport r = Validator{}.validate({}, {}, cMinorWhole(), material);
  EXPECT_TRUE(hasRuleKind(r, "variation_role_ornament_constraint", FailKind::MusicalFail));
}

// A non-Ground variation (Respond) may freely subdivide below the beat: the
// constraint only binds Ground-role variations.
TEST(ChaconneArchTest, VariationOrnamentConstraintIgnoresNonGroundRoles) {
  Material material;
  VariationDecl var;
  var.role = VariationRole::Respond;
  var.voice = 0;
  var.start_tick = 0;
  var.end_tick = kTicksPerBar;
  var.density_level = 1;
  var.notes.push_back(mnote(0, kTicksPerBeat / 2, 60));  // eighth, but not Ground.
  material.variations.push_back(var);

  const ValidationReport r = Validator{}.validate({}, {}, cMinorWhole(), material);
  EXPECT_FALSE(hasRule(r, "variation_role_ornament_constraint"));
}

// Inert when no variations are declared.
TEST(ChaconneArchTest, VariationOrnamentConstraintInertWhenVariationsEmpty) {
  Material material;  // no variations.
  const ValidationReport r = Validator{}.validate({}, {}, cMinorWhole(), material);
  EXPECT_FALSE(hasRule(r, "variation_role_ornament_constraint"));
}

// --- 3. Phase16 fixture integration ----------------------------------------

// The BWV1004 chaconne fixture must run through the full Composer cleanly for
// every seed family (seed % 4 selects the scalar-wave start offset): no
// validator failure, two voices, and all three Arch bits present somewhere in
// the provenance.
TEST(ChaconneArchTest, Phase16FixtureValidatesCleanAndStampsAllArchBits) {
  for (int seed : {0, 1, 2, 3, 4, 5}) {
    const HarnessFixture fx = buildHarnessFixture(HarnessPhase::Phase16, seed);
    const ComposeResult r = Composer{}.run(fx.material, fx.harmony, fx.voice_plan);

    EXPECT_TRUE(r.validation.failures.empty())
        << "seed " << seed << " produced " << r.validation.failures.size() << " failure(s); first="
        << (r.validation.failures.empty() ? "" : r.validation.failures.front().rule_id);
    ASSERT_FALSE(r.notes.empty());
    ASSERT_EQ(r.notes.size(), r.provenance.size());

    bool saw_ground = false;
    bool saw_role = false;
    bool saw_density = false;
    for (const auto& p : r.provenance) {
      if (p.satisfied_rules & bit(RuleBit::GroundBassReplayed))
        saw_ground = true;
      if (p.satisfied_rules & bit(RuleBit::VariationRoleApplied))
        saw_role = true;
      if (p.satisfied_rules & bit(RuleBit::TextureDensityShift))
        saw_density = true;
    }
    EXPECT_TRUE(saw_ground) << "seed " << seed << " missing GroundBassReplayed";
    EXPECT_TRUE(saw_role) << "seed " << seed << " missing VariationRoleApplied";
    EXPECT_TRUE(saw_density) << "seed " << seed << " missing TextureDensityShift";
  }
}

}  // namespace bach::composer
