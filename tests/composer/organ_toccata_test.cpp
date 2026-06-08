// Organ Toccata (BWV565/538/564/540 four-archetype sectional form) tests.
//
// Covers the three halves of the Toccata foundation:
//   1. CandidateSearch replays a ToccataSection verbatim under a ToccataCarrier
//      span whose window matches the section (every note stamps
//      ToccataArchetypeApplied; the FIRST note of a section flagged
//      is_section_head additionally stamps SectionTransition), and stamps
//      SectionTransition ONLY on a section-head section's first note (a
//      non-head section nearby stays plain).
//   2. The Validator's toccata_archetype_compatible rule fires when a
//      ToccataSection pairs an antithetical (character, archetype) — Noble x
//      Dramaticus (MusicalFail) — passes a compatible pair (Severe x
//      Dramaticus), and stays inert when no toccata section exists.
//   3. The OrganToccata fixture runs through the full Composer cleanly for one seed
//      per archetype (seed % 4 selects the archetype, (seed / 4) % 4 the
//      scalar-wave offset) and stamps both Toccata bits in provenance; the
//      SectionTransition count matches the archetype's section count.

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

MaterialNote mnote(Tick start, Tick dur, std::uint8_t pitch) {
  MaterialNote n;
  n.start_tick = start;
  n.duration = dur;
  n.pitch = pitch;
  return n;
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

// Build a ToccataSection at [start_bar, end_bar) with `count` quarter-note
// notes from start_bar's downbeat, all on voice 0.
ToccataSection makeSection(int start_bar, int end_bar, bool head, int count,
                           ToccataArchetype archetype = ToccataArchetype::Dramaticus,
                           SubjectCharacter character = SubjectCharacter::Severe) {
  ToccataSection s;
  s.archetype = archetype;
  s.character = character;
  s.voice = 0;
  s.start_tick = static_cast<Tick>(start_bar) * kTicksPerBar;
  s.end_tick = static_cast<Tick>(end_bar) * kTicksPerBar;
  s.is_section_head = head;
  for (int n = 0; n < count; ++n)
    s.notes.push_back(mnote(s.start_tick + static_cast<Tick>(n) * kTicksPerBeat, kTicksPerBeat,
                            static_cast<std::uint8_t>(60 + (n % 3) * 4)));  // C/E/G triad walk.
  return s;
}

// Expected SectionTransition count per archetype's section layout.
int expectedSectionCount(int seed) {
  switch (seed % 4) {
    case 0:  // Dramaticus: [0-3],[4-15].
      return 2;
    case 1:  // Perpetuus: [0-15].
      return 1;
    case 2:  // Concertato: [0-3],[4-7],[8-11],[12-15].
      return 4;
    case 3:  // Sectionalis: [0-7],[8-15].
      return 2;
    default:
      return 0;
  }
}

}  // namespace

// --- 1. CandidateSearch verbatim toccata replay -----------------------------

// Two toccata sections on voice 0, each replayed under its own ToccataCarrier
// span: a section-head section (bars 0-1) and a non-head section (bars 1-2).
// ToccataArchetypeApplied lands on every note; SectionTransition lands ONLY on
// the section-head section's first note.
TEST(OrganToccataTest, ToccataCarrierReplaysSectionAndStampsBitsPerSection) {
  Material material;
  // Section 0: section head, bar 0, 2 notes.
  material.toccata_sections.push_back(makeSection(0, 1, /*head=*/true, /*count=*/2));
  // Section 1: NOT a section head, bar 1, 2 notes.
  material.toccata_sections.push_back(makeSection(1, 2, /*head=*/false, /*count=*/2));

  auto enumerateSpan = [&](Tick start, Tick end) {
    Span s;
    s.id = 1;
    s.start_tick = start;
    s.end_tick = end;
    s.voice = 0;
    s.intent = VoiceIntent::ToccataCarrier;
    return CandidateSearch{}.enumerate(s, cMajorWhole(), material, CandidateContext{});
  };

  // Section-head section: ToccataArchetypeApplied on all; SectionTransition only
  // on the first note.
  const auto head = enumerateSpan(0, kTicksPerBar);
  ASSERT_EQ(head.size(), 2u);
  for (const auto& c : head)
    EXPECT_NE(c.satisfied_rules & bit(RuleBit::ToccataArchetypeApplied), 0u);
  EXPECT_NE(head[0].satisfied_rules & bit(RuleBit::SectionTransition), 0u);
  EXPECT_EQ(head[1].satisfied_rules & bit(RuleBit::SectionTransition), 0u);

  // Non-head section: ToccataArchetypeApplied on all; NO SectionTransition.
  const auto tail = enumerateSpan(kTicksPerBar, 2 * kTicksPerBar);
  ASSERT_EQ(tail.size(), 2u);
  for (const auto& c : tail) {
    EXPECT_NE(c.satisfied_rules & bit(RuleBit::ToccataArchetypeApplied), 0u);
    EXPECT_EQ(c.satisfied_rules & bit(RuleBit::SectionTransition), 0u);
  }
}

// --- 2. toccata_archetype_compatible ----------------------------------------

// A compatible pair (Severe x Dramaticus) must NOT trip the rule.
TEST(OrganToccataTest, ToccataArchetypeCompatiblePassesOnCompatiblePair) {
  Material material;
  material.toccata_sections.push_back(makeSection(
      0, 1, /*head=*/true, /*count=*/1, ToccataArchetype::Dramaticus, SubjectCharacter::Severe));

  const ValidationReport r = Validator{}.validate({}, {}, cMajorWhole(), material);
  EXPECT_FALSE(hasRule(r, "toccata_archetype_compatible"));
}

// The forbidden pair (Noble x Dramaticus) must fire one MusicalFail.
TEST(OrganToccataTest, ToccataArchetypeCompatibleFailsOnNobleDramaticus) {
  Material material;
  material.toccata_sections.push_back(makeSection(
      0, 1, /*head=*/true, /*count=*/1, ToccataArchetype::Dramaticus, SubjectCharacter::Noble));

  const ValidationReport r = Validator{}.validate({}, {}, cMajorWhole(), material);
  EXPECT_TRUE(hasRuleKind(r, "toccata_archetype_compatible", FailKind::MusicalFail));
}

// Inert when no toccata section exists.
TEST(OrganToccataTest, ToccataArchetypeCompatibleInertWhenNoToccataSection) {
  const ValidationReport r = Validator{}.validate({}, {}, cMajorWhole(), Material{});
  EXPECT_FALSE(hasRule(r, "toccata_archetype_compatible"));
}

// --- 3. OrganToccata fixture integration -----------------------------------------

// The organ-toccata fixture must run through the full Composer cleanly for one
// seed per archetype (seed 0-3): no validator failure, single voice, both
// Toccata bits present, and the SectionTransition count equals the archetype's
// section count.
TEST(OrganToccataTest, OrganToccataFixtureValidatesCleanAndStampsToccataBits) {
  for (int seed : {0, 1, 2, 3}) {
    const HarnessFixture fx = buildHarnessFixture(HarnessPhase::OrganToccata, seed);
    const ComposeResult r = Composer{}.run(fx.material, fx.harmony, fx.voice_plan);

    EXPECT_TRUE(r.validation.failures.empty())
        << "seed " << seed << " produced " << r.validation.failures.size() << " failure(s); first="
        << (r.validation.failures.empty() ? "" : r.validation.failures.front().rule_id);
    ASSERT_FALSE(r.notes.empty());
    ASSERT_EQ(r.notes.size(), r.provenance.size());

    // Single voice: no note may carry voice != 0.
    for (const auto& note : r.notes)
      EXPECT_EQ(note.voice, 0) << "seed " << seed << " produced a non-zero voice";

    bool saw_archetype = false;
    int section_transitions = 0;
    for (const auto& p : r.provenance) {
      if (p.satisfied_rules & bit(RuleBit::ToccataArchetypeApplied))
        saw_archetype = true;
      if (p.satisfied_rules & bit(RuleBit::SectionTransition))
        ++section_transitions;
    }
    EXPECT_TRUE(saw_archetype) << "seed " << seed << " missing ToccataArchetypeApplied";
    EXPECT_EQ(section_transitions, expectedSectionCount(seed))
        << "seed " << seed << " section-head count mismatch";
  }
}

}  // namespace bach::composer
