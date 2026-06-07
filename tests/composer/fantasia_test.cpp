// Organ Fantasia (free sectional, multi-style / BWV542/562/572) tests.
//
// Covers the two halves of the Fantasia foundation (a single voice organized
// into FOUR CONTRASTING 4-bar sections, each a verbatim-replayed
// FantasiaCarrier line):
//   1. CandidateSearch replays the FantasiaSection whose window matches the
//      span verbatim (matched by section.start_tick == span.start_tick &&
//      section.end_tick == span.end_tick, NOT by voice), stamping
//      FantasiaSectionContrast on every note (plus the baseline ChordTone/P7/P8
//      bits via emitMaterialNote). The branch is inert when
//      material.fantasia_sections is empty.
//   2. The Validator's section_contrast_required rule (SOFT MusicalFail when an
//      adjacent section pair differs by < 2 notes/bar AND < 5 semitones of mean
//      register) stays silent on the contrasting fixture, stays inert with
//      fewer than two sections, and fires on two near-identical sections (same
//      density AND same register).
//   3. The Phase22 fixture runs through the full Composer cleanly for every seed
//      family (seed % 4 selects the scalar-wave start offset): one voice with
//      four sections at the 4 / 8 / 16 / 2 notes-per-bar densities, every
//      fantasia note stamps FantasiaSectionContrast, and
//      section_contrast_required does NOT soft-fail the fixture.

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

// Single C-major chord covering the whole piece (the Fantasia is internally in
// C major; transposition happens only at MIDI output).
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

// Fantasia provenance: a Material source carrying the FantasiaSectionContrast
// bit, mirroring what CandidateSearch stamps on a FantasiaCarrier note.
NoteProvenance fantasiaProv() {
  NoteProvenance p;
  p.voice_intent = VoiceIntent::FantasiaCarrier;
  p.source = NoteSource::Material;
  p.satisfied_rules = (ruleBitMask(RuleBit::FantasiaSectionContrast));
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

// A FantasiaCarrier span selects the FantasiaSection whose window matches the
// span (start_tick AND end_tick equal): the matching section's notes are
// replayed verbatim and every note carries FantasiaSectionContrast. A section
// with a different window is ignored.
TEST(FantasiaTest, CarrierReplaysWindowMatchedSectionVerbatim) {
  Material material;
  FantasiaSection a;
  a.voice = 0;
  a.start_tick = 0;
  a.end_tick = kTicksPerBar;
  a.notes.push_back(mnote(0, kTicksPerBeat, 48));
  a.notes.push_back(mnote(kTicksPerBeat, kTicksPerBeat, 50));
  material.fantasia_sections.push_back(a);

  FantasiaSection b;
  b.voice = 0;
  b.start_tick = kTicksPerBar;
  b.end_tick = 2 * kTicksPerBar;
  b.notes.push_back(mnote(kTicksPerBar, kTicksPerBeat, 72));
  material.fantasia_sections.push_back(b);

  Span span;
  span.id = 1;
  span.start_tick = 0;
  span.end_tick = kTicksPerBar;
  span.voice = 0;
  span.intent = VoiceIntent::FantasiaCarrier;

  const auto cands = CandidateSearch{}.enumerate(span, cMajorWhole(), material, CandidateContext{});

  // Only section A (window 0..kTicksPerBar) replayed (2 notes), section B skipped.
  ASSERT_EQ(cands.size(), 2u);
  EXPECT_EQ(cands[0].start_tick, 0);
  EXPECT_EQ(cands[0].pitch, 48u);
  EXPECT_EQ(cands[1].start_tick, kTicksPerBeat);
  EXPECT_EQ(cands[1].pitch, 50u);
  for (std::size_t i = 0; i < cands.size(); ++i) {
    EXPECT_NE(cands[i].satisfied_rules & bit(RuleBit::FantasiaSectionContrast), 0u)
        << "fantasia note " << i << " missing FantasiaSectionContrast";
  }
}

// Inert when there are no fantasia sections declared: a FantasiaCarrier span
// produces no candidates.
TEST(FantasiaTest, CarrierInertWhenSectionsEmpty) {
  Material material;  // no fantasia_sections.
  Span span;
  span.id = 1;
  span.start_tick = 0;
  span.end_tick = kTicksPerBar;
  span.voice = 0;
  span.intent = VoiceIntent::FantasiaCarrier;
  const auto cands = CandidateSearch{}.enumerate(span, cMajorWhole(), material, CandidateContext{});
  EXPECT_TRUE(cands.empty());
}

// --- 2. section_contrast_required ------------------------------------------

// Two sections that DIFFER strongly in both density and register: the rule
// stays silent. Section A is 1 sparse note/bar at C3; section B is 4 dense
// notes/bar at C5. (kMinDensityMargin = 2, kMinRegisterMargin = 5.)
TEST(FantasiaTest, SectionContrastPassesOnDistinctSections) {
  Material material;
  FantasiaSection a;
  a.start_tick = 0;
  a.end_tick = kTicksPerBar;
  material.fantasia_sections.push_back(a);
  FantasiaSection b;
  b.start_tick = kTicksPerBar;
  b.end_tick = 2 * kTicksPerBar;
  material.fantasia_sections.push_back(b);

  std::vector<NoteEvent> notes;
  std::vector<NoteProvenance> prov;
  // Section A: 1 note in the bar, low register (C3 = 48).
  notes.push_back(makeNote(0, kTicksPerBar, 48, 0));
  prov.push_back(fantasiaProv());
  // Section B: 16 notes in the bar, high register (~C5).
  for (int i = 0; i < 16; ++i) {
    notes.push_back(makeNote(kTicksPerBar + static_cast<Tick>(i) * (kTicksPerBeat / 4),
                             kTicksPerBeat / 4, static_cast<std::uint8_t>(72 + (i % 5)), 0));
    prov.push_back(fantasiaProv());
  }

  const ValidationReport r = Validator{}.validate(notes, prov, cMajorWhole(), material);
  EXPECT_FALSE(hasRule(r, "section_contrast_required"));
}

// Inert with fewer than two sections carrying the bit: a single section cannot
// trip the rule (no adjacent pair to assess).
TEST(FantasiaTest, SectionContrastInertWithSingleSection) {
  Material material;
  FantasiaSection a;
  a.start_tick = 0;
  a.end_tick = kTicksPerBar;
  material.fantasia_sections.push_back(a);

  std::vector<NoteEvent> notes = {makeNote(0, kTicksPerBeat, 60, 0)};
  std::vector<NoteProvenance> prov = {fantasiaProv()};
  const ValidationReport r = Validator{}.validate(notes, prov, cMajorWhole(), material);
  EXPECT_FALSE(hasRule(r, "section_contrast_required"));
}

// Two sections that are NEAR-IDENTICAL in BOTH density and register: same
// notes-per-bar (4) AND same mean register (C4 = 60). This pair is NOT
// contrasting and the rule MUST soft-fail it with a MusicalFail. This mirrors
// how the trio test asserts lockstep-parallel motion fires
// voice_independence_threshold: the positive case that the contrast rule is not
// vacuously silent.
TEST(FantasiaTest, SectionContrastFailsOnNearIdenticalSections) {
  Material material;
  FantasiaSection a;
  a.start_tick = 0;
  a.end_tick = kTicksPerBar;
  material.fantasia_sections.push_back(a);
  FantasiaSection b;
  b.start_tick = kTicksPerBar;
  b.end_tick = 2 * kTicksPerBar;
  material.fantasia_sections.push_back(b);

  std::vector<NoteEvent> notes;
  std::vector<NoteProvenance> prov;
  // Both sections: 4 notes/bar, identical pitches around C4 (60). Density diff
  // 0 (< 2) AND register diff 0 (< 5) -> uncontrasting -> one MusicalFail.
  for (int sec = 0; sec < 2; ++sec) {
    const Tick base = static_cast<Tick>(sec) * kTicksPerBar;
    for (int beat = 0; beat < 4; ++beat) {
      notes.push_back(makeNote(base + static_cast<Tick>(beat) * kTicksPerBeat, kTicksPerBeat,
                               static_cast<std::uint8_t>(60 + beat), 0));
      prov.push_back(fantasiaProv());
    }
  }

  const ValidationReport r = Validator{}.validate(notes, prov, cMajorWhole(), material);
  // Near-identical sections are not contrasting: the rule must soft-fail.
  EXPECT_TRUE(hasRule(r, "section_contrast_required"));
  EXPECT_TRUE(hasRuleKind(r, "section_contrast_required", FailKind::MusicalFail));
}

// --- 3. Phase22 fixture integration ----------------------------------------

// The free-sectional organ-fantasia fixture must run through the full Composer
// cleanly for every seed family (seed % 4 selects the scalar-wave start offset):
// no validator failure, one voice with four sections at the 4 / 8 / 16 / 2
// notes-per-bar densities, every fantasia note stamps FantasiaSectionContrast,
// and the section_contrast_required rule does NOT soft-fail.
TEST(FantasiaTest, Phase22FixtureValidatesCleanAndStampsFantasiaBit) {
  for (int seed : {0, 1, 2, 3}) {
    const HarnessFixture fx = buildHarnessFixture(HarnessPhase::Phase22, seed);
    const ComposeResult r = Composer{}.run(fx.material, fx.harmony, fx.voice_plan);

    EXPECT_TRUE(r.validation.failures.empty())
        << "seed " << seed << " produced " << r.validation.failures.size() << " failure(s); first="
        << (r.validation.failures.empty() ? "" : r.validation.failures.front().rule_id);
    EXPECT_FALSE(hasRule(r.validation, "section_contrast_required"))
        << "seed " << seed << " soft-failed section_contrast_required";
    ASSERT_FALSE(r.notes.empty());
    ASSERT_EQ(r.notes.size(), r.provenance.size());
    EXPECT_EQ(fx.voice_plan.num_voices, 1);

    // One voice, 120 fantasia notes total. Per-section densities: A bars 0-3
    // (4 quarters/bar -> 16), B bars 4-7 (8 eighths/bar -> 32), C bars 8-11 (16
    // sixteenths/bar -> 64), D bars 12-15 (2 halves/bar -> 8).
    int section_count[4] = {0, 0, 0, 0};
    int fantasia_notes = 0;
    bool saw_fantasia_bit = false;
    bool all_fantasia_stamped = true;
    for (std::size_t i = 0; i < r.notes.size(); ++i) {
      if (r.provenance[i].voice_intent != VoiceIntent::FantasiaCarrier)
        continue;
      ++fantasia_notes;
      const int bar = static_cast<int>(r.notes[i].start_tick / kTicksPerBar);
      if (bar >= 0 && bar <= 3)
        ++section_count[0];
      else if (bar >= 4 && bar <= 7)
        ++section_count[1];
      else if (bar >= 8 && bar <= 11)
        ++section_count[2];
      else if (bar >= 12 && bar <= 15)
        ++section_count[3];
      if (r.provenance[i].satisfied_rules & bit(RuleBit::FantasiaSectionContrast))
        saw_fantasia_bit = true;
      else
        all_fantasia_stamped = false;
    }
    EXPECT_EQ(fantasia_notes, 120) << "seed " << seed << " total fantasia notes";
    EXPECT_EQ(section_count[0], 16) << "seed " << seed << " section A (Free, quarters)";
    EXPECT_EQ(section_count[1], 32) << "seed " << seed << " section B (Fugal, eighths)";
    EXPECT_EQ(section_count[2], 64) << "seed " << seed << " section C (Toccata, sixteenths)";
    EXPECT_EQ(section_count[3], 8) << "seed " << seed << " section D (Chordal, halves)";
    EXPECT_TRUE(saw_fantasia_bit) << "seed " << seed << " missing FantasiaSectionContrast";
    EXPECT_TRUE(all_fantasia_stamped)
        << "seed " << seed << " has a FantasiaCarrier note without FantasiaSectionContrast";
  }
}

}  // namespace bach::composer
