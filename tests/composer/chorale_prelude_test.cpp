// Organ Chorale Prelude (cantus firmus + counterpoint) tests.
//
// Covers the three halves of the Chorale-Prelude foundation:
//   1. CandidateSearch replays the cantus firmus under a CantusFirmusCarrier
//      span: the plain skeleton (material.cf_is_embellished=false) stamps only
//      CantusFirmusReplayed, while the embellished line
//      (material.cf_is_embellished=true) stamps CantusFirmusReplayed +
//      CFEmbellishmentApplied on every replayed note.
//   2. The Validator's cantus_firmus_immutable rule passes when every bar
//      downbeat matches the skeleton tone, fires a StructuralFail when a replayed
//      downbeat is mutated off the skeleton, and stays inert when no cantus
//      firmus exists (Phase 3-18 fixtures).
//   3. The Phase19 fixture runs through the full Composer cleanly for every seed
//      family (seed % 4 selects the scalar-wave start offset), produces two
//      voices, and stamps both Chorale-Prelude bits in provenance.

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
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

// Single C-major chord covering the whole piece.
HarmonicPlan cMajorWhole() {
  HarmonicPlan plan;
  plan.tonic_pc = 0;
  plan.is_minor = false;
  ChordEvent c;
  c.start_tick = 0;
  c.root_pc = 0;
  c.quality = ChordQuality::Major;
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

// Cantus-firmus provenance: Material source carrying CantusFirmusReplayed,
// optionally plus CFEmbellishmentApplied, mirroring what CandidateSearch stamps.
NoteProvenance cfProv(bool embellished = false) {
  NoteProvenance p;
  p.voice_intent = VoiceIntent::CantusFirmusCarrier;
  p.source = NoteSource::Material;
  p.satisfied_rules = (RuleIdMask{1} << RuleBit::CantusFirmusReplayed);
  if (embellished)
    p.satisfied_rules |= (RuleIdMask{1} << RuleBit::CFEmbellishmentApplied);
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
  return RuleIdMask{1} << b;
}

// A two-bar skeleton cantus firmus: C3 (48) at bar 0, D3 (50) at bar 1.
Material twoBarCantusFirmus(bool embellished) {
  Material m;
  m.cantus_firmus.push_back(mnote(0, kTicksPerBar, 48));
  m.cantus_firmus.push_back(mnote(kTicksPerBar, kTicksPerBar, 50));
  if (embellished) {
    // Bar 0: skeleton C3 half note + two passing tones; bar 1: skeleton D3.
    m.cf_embellished.push_back(mnote(0, 2 * kTicksPerBeat, 48));
    m.cf_embellished.push_back(mnote(2 * kTicksPerBeat, kTicksPerBeat, 50));
    m.cf_embellished.push_back(mnote(3 * kTicksPerBeat, kTicksPerBeat, 52));
    m.cf_embellished.push_back(mnote(kTicksPerBar, 2 * kTicksPerBeat, 50));
    m.cf_embellished.push_back(mnote(kTicksPerBar + 2 * kTicksPerBeat, kTicksPerBeat, 52));
    m.cf_embellished.push_back(mnote(kTicksPerBar + 3 * kTicksPerBeat, kTicksPerBeat, 50));
    m.cf_is_embellished = true;
  }
  return m;
}

}  // namespace

// --- 1. CandidateSearch cantus-firmus replay -------------------------------

// Plain (non-embellished) skeleton replay: each bar's whole-note tone is
// replayed verbatim, stamping CantusFirmusReplayed and NOT CFEmbellishmentApplied.
TEST(ChoralePreludeTest, CantusFirmusCarrierReplaysPlainSkeletonWithReplayBitOnly) {
  const Material material = twoBarCantusFirmus(/*embellished=*/false);
  Span s;
  s.id = 1;
  s.start_tick = 0;
  s.end_tick = 2 * kTicksPerBar;
  s.voice = 1;
  s.intent = VoiceIntent::CantusFirmusCarrier;
  const auto cands = CandidateSearch{}.enumerate(s, cMajorWhole(), material, CandidateContext{});

  ASSERT_EQ(cands.size(), 2u);
  EXPECT_EQ(cands[0].pitch, 48);
  EXPECT_EQ(cands[1].pitch, 50);
  for (const auto& c : cands) {
    EXPECT_NE(c.satisfied_rules & bit(RuleBit::CantusFirmusReplayed), 0u);
    EXPECT_EQ(c.satisfied_rules & bit(RuleBit::CFEmbellishmentApplied), 0u);
  }
}

// Embellished replay: the cf_embellished line is replayed, and every note
// carries CantusFirmusReplayed + CFEmbellishmentApplied.
TEST(ChoralePreludeTest, CantusFirmusCarrierReplaysEmbellishedWithBothBits) {
  const Material material = twoBarCantusFirmus(/*embellished=*/true);
  Span s;
  s.id = 1;
  s.start_tick = 0;
  s.end_tick = 2 * kTicksPerBar;
  s.voice = 1;
  s.intent = VoiceIntent::CantusFirmusCarrier;
  const auto cands = CandidateSearch{}.enumerate(s, cMajorWhole(), material, CandidateContext{});

  ASSERT_EQ(cands.size(), material.cf_embellished.size());
  for (std::size_t i = 0; i < cands.size(); ++i) {
    EXPECT_EQ(cands[i].pitch, material.cf_embellished[i].pitch) << "note " << i;
    EXPECT_NE(cands[i].satisfied_rules & bit(RuleBit::CantusFirmusReplayed), 0u) << "note " << i;
    EXPECT_NE(cands[i].satisfied_rules & bit(RuleBit::CFEmbellishmentApplied), 0u) << "note " << i;
  }
  // The bar downbeats (ticks 0 and kTicksPerBar) must equal the skeleton tones.
  EXPECT_EQ(cands.front().pitch, 48);
}

// --- 2. cantus_firmus_immutable --------------------------------------------

// Every bar downbeat of the replayed (embellished) line equals the skeleton
// tone: the rule stays silent.
TEST(ChoralePreludeTest, CantusFirmusImmutablePassesWhenDownbeatsMatchSkeleton) {
  const Material material = twoBarCantusFirmus(/*embellished=*/true);
  std::vector<NoteEvent> notes;
  std::vector<NoteProvenance> prov;
  for (const auto& m : material.cf_embellished) {
    notes.push_back(makeNote(m.start_tick, m.duration, m.pitch, 1));
    prov.push_back(cfProv(/*embellished=*/true));
  }

  const ValidationReport r = Validator{}.validate(notes, prov, cMajorWhole(), material);
  EXPECT_FALSE(hasRule(r, "cantus_firmus_immutable"));
}

// A replayed bar-downbeat tone mutated off the skeleton (bar 1 downbeat should
// be D3 = 50 but is replayed as E3 = 52): the rule fires a StructuralFail. The
// off-downbeat passing tones are unconstrained.
TEST(ChoralePreludeTest, CantusFirmusImmutableFailsWhenDownbeatMutated) {
  const Material material = twoBarCantusFirmus(/*embellished=*/true);
  std::vector<NoteEvent> notes;
  std::vector<NoteProvenance> prov;
  for (const auto& m : material.cf_embellished) {
    std::uint8_t pitch = m.pitch;
    if (m.start_tick == kTicksPerBar)
      pitch = 52;  // bar-1 downbeat mutated off the D3 skeleton tone.
    notes.push_back(makeNote(m.start_tick, m.duration, pitch, 1));
    prov.push_back(cfProv(/*embellished=*/true));
  }

  const ValidationReport r = Validator{}.validate(notes, prov, cMajorWhole(), material);
  EXPECT_TRUE(hasRuleKind(r, "cantus_firmus_immutable", FailKind::StructuralFail));
}

// Inert when no cantus firmus exists (Phase 3-18 fixtures): a note carrying no
// CantusFirmusReplayed bit cannot trip the rule, and the empty-skeleton guard
// keeps the rule silent.
TEST(ChoralePreludeTest, CantusFirmusImmutableInertWhenNoCantusFirmus) {
  std::vector<NoteEvent> notes = {makeNote(0, kTicksPerBar, 60, 0)};
  NoteProvenance p;
  p.voice_intent = VoiceIntent::SequentialCounterline;
  p.source = NoteSource::Compose;  // no CantusFirmusReplayed.
  std::vector<NoteProvenance> prov = {p};

  const ValidationReport r = Validator{}.validate(notes, prov, cMajorWhole(), Material{});
  EXPECT_FALSE(hasRule(r, "cantus_firmus_immutable"));
}

// --- 3. Phase19 fixture integration ----------------------------------------

// The organ chorale-prelude fixture must run through the full Composer cleanly
// for every seed family (seed % 4 selects the scalar-wave start offset): no
// validator failure, two voices, and both Chorale-Prelude bits present
// somewhere in the provenance.
TEST(ChoralePreludeTest, Phase19FixtureValidatesCleanAndStampsBothChoraleBits) {
  for (int seed : {0, 1, 2, 3}) {
    const HarnessFixture fx = buildHarnessFixture(HarnessPhase::Phase19, seed);
    const ComposeResult r = Composer{}.run(fx.material, fx.harmony, fx.voice_plan);

    EXPECT_TRUE(r.validation.failures.empty())
        << "seed " << seed << " produced " << r.validation.failures.size() << " failure(s); first="
        << (r.validation.failures.empty() ? "" : r.validation.failures.front().rule_id);
    ASSERT_FALSE(r.notes.empty());
    ASSERT_EQ(r.notes.size(), r.provenance.size());

    bool saw_two_voices = false;
    for (const auto& note : r.notes) {
      if (note.voice == 1) {
        saw_two_voices = true;
        break;
      }
    }
    EXPECT_TRUE(saw_two_voices) << "seed " << seed << " missing voice 1";

    bool saw_replayed = false;
    bool saw_embellished = false;
    for (const auto& p : r.provenance) {
      if (p.satisfied_rules & bit(RuleBit::CantusFirmusReplayed))
        saw_replayed = true;
      if (p.satisfied_rules & bit(RuleBit::CFEmbellishmentApplied))
        saw_embellished = true;
    }
    EXPECT_TRUE(saw_replayed) << "seed " << seed << " missing CantusFirmusReplayed";
    EXPECT_TRUE(saw_embellished) << "seed " << seed << " missing CFEmbellishmentApplied";
  }
}

}  // namespace bach::composer
