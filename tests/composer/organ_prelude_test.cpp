// Organ Prelude (BWV846 free sectional form) tests.
//
// Covers the three halves of the Prelude foundation:
//   1. CandidateSearch replays a FigurationSection verbatim under a
//      FigurationCarrier span whose window matches the section (every note
//      stamps FigurationCommitted; a section flagged is_cadenza also stamps
//      CadenzaApplied on its notes; a section flagged is_pedal_prep stamps
//      PedalPreparation), and stamps those flags ONLY on the matching section's
//      notes (a normal section nearby stays plain).
//   2. The Validator's figuration_harmonic_consistency rule fires when a
//      bar-downbeat figuration note is not a chord tone (MusicalFail), stays
//      silent when every downbeat is a chord tone, exempts a PedalPreparation
//      note that sits off the chord, and stays inert when no figuration note
//      exists (Phase 3-16 fixtures).
//   3. The Phase17 fixture runs through the full Composer cleanly for every
//      seed family (seed % 4 selects the scalar-wave start offset) and stamps
//      all three Prelude bits in provenance.

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

// A two-bar C-major / G-major progression: bar 0 = I (C E G), bar 1 = V (G B D).
HarmonicPlan cThenG() {
  HarmonicPlan plan;
  plan.tonic_pc = 0;
  plan.is_minor = false;
  ChordEvent c0;
  c0.start_tick = 0;
  c0.root_pc = 0;
  c0.quality = ChordQuality::Major;
  plan.chords.push_back(c0);
  ChordEvent c1;
  c1.start_tick = kTicksPerBar;
  c1.root_pc = 7;
  c1.quality = ChordQuality::Major;
  plan.chords.push_back(c1);
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

// Figuration provenance: Material source carrying FigurationCommitted, optionally
// plus PedalPreparation, mirroring what CandidateSearch stamps on a carrier note.
NoteProvenance figProv(bool pedal = false) {
  NoteProvenance p;
  p.voice_intent = VoiceIntent::FigurationCarrier;
  p.source = NoteSource::Material;
  p.satisfied_rules = (RuleIdMask{1} << RuleBit::FigurationCommitted);
  if (pedal)
    p.satisfied_rules |= (RuleIdMask{1} << RuleBit::PedalPreparation);
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

}  // namespace

// --- 1. CandidateSearch verbatim figuration replay -------------------------

// Three figuration sections on two voices, each replayed under its own
// FigurationCarrier span: a normal section (bars 0-0), a cadenza section
// (bars 1-1), and a pedal-prep section (bars 2-2, separate voice/span). The
// flags must land only on the matching section's notes.
TEST(OrganPreludeTest, FigurationCarrierReplaysSectionAndStampsFlagsPerSection) {
  Material material;

  // Section 0: normal figuration, bar 0, voice 0.
  FigurationSection normal;
  normal.voice = 0;
  normal.start_tick = 0;
  normal.end_tick = kTicksPerBar;
  normal.notes.push_back(mnote(0, kTicksPerBeat, 60));
  normal.notes.push_back(mnote(kTicksPerBeat, kTicksPerBeat, 64));
  material.figuration_sections.push_back(normal);

  // Section 1: cadenza, bar 1, voice 0.
  FigurationSection cadenza;
  cadenza.voice = 0;
  cadenza.start_tick = kTicksPerBar;
  cadenza.end_tick = 2 * kTicksPerBar;
  cadenza.is_cadenza = true;
  cadenza.notes.push_back(mnote(kTicksPerBar, kTicksPerBeat, 67));
  material.figuration_sections.push_back(cadenza);

  // Section 2: pedal prep, bar 2, voice 1.
  FigurationSection pedal;
  pedal.voice = 1;
  pedal.start_tick = 2 * kTicksPerBar;
  pedal.end_tick = 3 * kTicksPerBar;
  pedal.is_pedal_prep = true;
  pedal.notes.push_back(mnote(2 * kTicksPerBar, kTicksPerBar, 43));
  material.figuration_sections.push_back(pedal);

  auto enumerateSpan = [&](Tick start, Tick end, VoiceId voice) {
    Span s;
    s.id = 1;
    s.start_tick = start;
    s.end_tick = end;
    s.voice = voice;
    s.intent = VoiceIntent::FigurationCarrier;
    return CandidateSearch{}.enumerate(s, cMajorWhole(), material, CandidateContext{});
  };

  // Normal section: FigurationCommitted on all, no cadenza / pedal bits.
  const auto n = enumerateSpan(0, kTicksPerBar, 0);
  ASSERT_EQ(n.size(), 2u);
  for (const auto& c : n) {
    EXPECT_NE(c.satisfied_rules & bit(RuleBit::FigurationCommitted), 0u);
    EXPECT_EQ(c.satisfied_rules & bit(RuleBit::CadenzaApplied), 0u);
    EXPECT_EQ(c.satisfied_rules & bit(RuleBit::PedalPreparation), 0u);
  }

  // Cadenza section: FigurationCommitted + CadenzaApplied, no PedalPreparation.
  const auto cad = enumerateSpan(kTicksPerBar, 2 * kTicksPerBar, 0);
  ASSERT_EQ(cad.size(), 1u);
  EXPECT_NE(cad[0].satisfied_rules & bit(RuleBit::FigurationCommitted), 0u);
  EXPECT_NE(cad[0].satisfied_rules & bit(RuleBit::CadenzaApplied), 0u);
  EXPECT_EQ(cad[0].satisfied_rules & bit(RuleBit::PedalPreparation), 0u);

  // Pedal-prep section: FigurationCommitted + PedalPreparation, no CadenzaApplied.
  const auto ped = enumerateSpan(2 * kTicksPerBar, 3 * kTicksPerBar, 1);
  ASSERT_EQ(ped.size(), 1u);
  EXPECT_NE(ped[0].satisfied_rules & bit(RuleBit::FigurationCommitted), 0u);
  EXPECT_EQ(ped[0].satisfied_rules & bit(RuleBit::CadenzaApplied), 0u);
  EXPECT_NE(ped[0].satisfied_rules & bit(RuleBit::PedalPreparation), 0u);
}

// --- 2. figuration_harmonic_consistency ------------------------------------

// Every bar-downbeat figuration note is a chord tone of its bar's chord (bar 0
// I -> C; bar 1 V -> G): the rule stays silent.
TEST(OrganPreludeTest, FigurationHarmonicConsistencyPassesOnChordToneDownbeats) {
  std::vector<NoteEvent> notes;
  std::vector<NoteProvenance> prov;
  // Bar 0 (I = C E G): downbeat C4, then a passing D4 mid-bar (not checked).
  notes.push_back(makeNote(0, kTicksPerBeat / 4, 60, 0));
  prov.push_back(figProv());
  notes.push_back(makeNote(kTicksPerBeat / 4, kTicksPerBeat / 4, 62, 0));
  prov.push_back(figProv());
  // Bar 1 (V = G B D): downbeat G4.
  notes.push_back(makeNote(kTicksPerBar, kTicksPerBeat / 4, 67, 0));
  prov.push_back(figProv());

  const ValidationReport r = Validator{}.validate(notes, prov, cThenG(), Material{});
  EXPECT_FALSE(hasRule(r, "figuration_harmonic_consistency"));
}

// A bar-downbeat figuration note that is NOT a chord tone of its bar's chord:
// bar 1 chord is V (G B D) but the downbeat is A4 (pc 9, off the chord). The
// rule fires with a MusicalFail.
TEST(OrganPreludeTest, FigurationHarmonicConsistencyFailsOnNonChordToneDownbeat) {
  std::vector<NoteEvent> notes;
  std::vector<NoteProvenance> prov;
  notes.push_back(makeNote(0, kTicksPerBeat / 4, 60, 0));  // bar 0 I -> C4 (ok).
  prov.push_back(figProv());
  notes.push_back(makeNote(kTicksPerBar, kTicksPerBeat / 4, 69, 0));  // bar 1 V -> A4 (off).
  prov.push_back(figProv());

  const ValidationReport r = Validator{}.validate(notes, prov, cThenG(), Material{});
  EXPECT_TRUE(hasRuleKind(r, "figuration_harmonic_consistency", FailKind::MusicalFail));
}

// A PedalPreparation note sitting off the bar's chord must NOT fail: a pedal is
// a single sustained pitch held against changing harmony, so it is exempt. The
// bar-1 downbeat is A4 (off the V chord) but carries PedalPreparation.
TEST(OrganPreludeTest, FigurationHarmonicConsistencyExemptsPedalPreparation) {
  std::vector<NoteEvent> notes;
  std::vector<NoteProvenance> prov;
  notes.push_back(makeNote(0, kTicksPerBeat / 4, 60, 0));  // bar 0 I -> C4 (ok).
  prov.push_back(figProv());
  // Bar 1 downbeat A4 (pc 9, off V) but PedalPreparation -> exempt.
  notes.push_back(makeNote(kTicksPerBar, kTicksPerBar, 69, 1));
  prov.push_back(figProv(/*pedal=*/true));

  const ValidationReport r = Validator{}.validate(notes, prov, cThenG(), Material{});
  EXPECT_FALSE(hasRule(r, "figuration_harmonic_consistency"));
}

// Inert when no figuration note exists (Phase 3-16 fixtures): a note carrying no
// FigurationCommitted bit cannot trip the rule even if it is off the chord.
TEST(OrganPreludeTest, FigurationHarmonicConsistencyInertWhenNoFiguration) {
  std::vector<NoteEvent> notes = {makeNote(0, kTicksPerBeat, 69, 0)};  // A4 under I, no bit.
  NoteProvenance p;
  p.voice_intent = VoiceIntent::SequentialCounterline;
  p.source = NoteSource::Compose;  // no FigurationCommitted.
  std::vector<NoteProvenance> prov = {p};

  const ValidationReport r = Validator{}.validate(notes, prov, cMajorWhole(), Material{});
  EXPECT_FALSE(hasRule(r, "figuration_harmonic_consistency"));
}

// --- 3. Phase17 fixture integration ----------------------------------------

// The BWV846 organ-prelude fixture must run through the full Composer cleanly
// for every seed family (seed % 4 selects the scalar-wave start offset): no
// validator failure, two voices, and all three Prelude bits present somewhere
// in the provenance.
TEST(OrganPreludeTest, Phase17FixtureValidatesCleanAndStampsAllPreludeBits) {
  for (int seed : {0, 1, 2, 3, 4, 5}) {
    const HarnessFixture fx = buildHarnessFixture(HarnessPhase::Phase17, seed);
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

    bool saw_figuration = false;
    bool saw_cadenza = false;
    bool saw_pedal = false;
    for (const auto& p : r.provenance) {
      if (p.satisfied_rules & bit(RuleBit::FigurationCommitted))
        saw_figuration = true;
      if (p.satisfied_rules & bit(RuleBit::CadenzaApplied))
        saw_cadenza = true;
      if (p.satisfied_rules & bit(RuleBit::PedalPreparation))
        saw_pedal = true;
    }
    EXPECT_TRUE(saw_figuration) << "seed " << seed << " missing FigurationCommitted";
    EXPECT_TRUE(saw_cadenza) << "seed " << seed << " missing CadenzaApplied";
    EXPECT_TRUE(saw_pedal) << "seed " << seed << " missing PedalPreparation";
  }
}

}  // namespace bach::composer
