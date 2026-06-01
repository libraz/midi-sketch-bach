// Organ Trio Sonata (BWV525-529) tests.
//
// Covers the two halves of the Trio Sonata foundation (three INDEPENDENT
// voices: RH = Great, LH = Swell, Pedal, each a verbatim-replayed
// TrioVoiceCarrier line):
//   1. CandidateSearch replays each TrioVoiceLine whose voice matches the span
//      verbatim (matched by line.voice == span.voice, not by window position),
//      stamping TrioVoiceIndependent on every note (plus the baseline
//      ChordTone/P7/P8 bits via emitMaterialNote). The branch is inert when
//      material.trio_voices is empty.
//   2. The Validator's voice_independence_threshold rule (SOFT MusicalFail when
//      the mean pairwise independence of the bit-62 voices drops below 0.6)
//      stays silent on three rhythmically distinct voices, stays inert with
//      fewer than two trio voices, and fires on three rhythmically locked,
//      parallel-moving voices.
//   3. The Phase21 fixture runs through the full Composer cleanly for every seed
//      family (seed % 4 selects the V0/V1 scalar-wave start offset): three
//      voices at the 16 / 8 / 4 notes-per-bar densities, every trio note stamps
//      TrioVoiceIndependent, and voice_independence_threshold does NOT soft-fail
//      the fixture.

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

// Single C-major chord covering the whole piece (the Trio Sonata is internally
// in C major; transposition happens only at MIDI output).
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

// Trio provenance: a Material source carrying the TrioVoiceIndependent bit,
// mirroring what CandidateSearch stamps on a TrioVoiceCarrier note.
NoteProvenance trioProv() {
  NoteProvenance p;
  p.voice_intent = VoiceIntent::TrioVoiceCarrier;
  p.source = NoteSource::Material;
  p.satisfied_rules = (RuleIdMask{1} << RuleBit::TrioVoiceIndependent);
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

// --- 1. CandidateSearch verbatim replay ------------------------------------

// A TrioVoiceCarrier span selects the TrioVoiceLine whose voice matches the
// span (not by window position): the matching line's in-window notes are
// replayed verbatim and every note carries TrioVoiceIndependent. The non-
// matching line is ignored.
TEST(TrioSonataTest, CarrierReplaysMatchingVoiceLineVerbatim) {
  Material material;
  TrioVoiceLine line0;
  line0.voice = 0;
  line0.manual = 0;
  line0.notes.push_back(mnote(0, kTicksPerBeat, 72));
  line0.notes.push_back(mnote(kTicksPerBeat, kTicksPerBeat, 74));
  material.trio_voices.push_back(line0);

  TrioVoiceLine line1;
  line1.voice = 1;
  line1.manual = 1;
  line1.notes.push_back(mnote(0, kTicksPerBeat, 60));
  material.trio_voices.push_back(line1);

  Span span;
  span.id = 1;
  span.start_tick = 0;
  span.end_tick = kTicksPerBar;
  span.voice = 0;
  span.intent = VoiceIntent::TrioVoiceCarrier;

  const auto cands = CandidateSearch{}.enumerate(span, cMajorWhole(), material, CandidateContext{});

  // Only voice-0 line replayed (2 notes), voice-1 line skipped.
  ASSERT_EQ(cands.size(), 2u);
  EXPECT_EQ(cands[0].start_tick, 0);
  EXPECT_EQ(cands[0].pitch, 72u);
  EXPECT_EQ(cands[1].start_tick, kTicksPerBeat);
  EXPECT_EQ(cands[1].pitch, 74u);
  for (std::size_t i = 0; i < cands.size(); ++i) {
    EXPECT_NE(cands[i].satisfied_rules & bit(RuleBit::TrioVoiceIndependent), 0u)
        << "trio note " << i << " missing TrioVoiceIndependent";
  }
}

// Only notes inside the span window are replayed; notes outside it are skipped.
TEST(TrioSonataTest, CarrierClipsNotesToSpanWindow) {
  Material material;
  TrioVoiceLine line0;
  line0.voice = 0;
  line0.notes.push_back(mnote(0, kTicksPerBeat, 72));                 // inside.
  line0.notes.push_back(mnote(kTicksPerBar, kTicksPerBeat, 74));      // at end: excluded.
  line0.notes.push_back(mnote(2 * kTicksPerBar, kTicksPerBeat, 76));  // after: excluded.
  material.trio_voices.push_back(line0);

  Span span;
  span.id = 1;
  span.start_tick = 0;
  span.end_tick = kTicksPerBar;
  span.voice = 0;
  span.intent = VoiceIntent::TrioVoiceCarrier;

  const auto cands = CandidateSearch{}.enumerate(span, cMajorWhole(), material, CandidateContext{});
  ASSERT_EQ(cands.size(), 1u);
  EXPECT_EQ(cands[0].pitch, 72u);
}

// Inert when there are no trio voices declared (Phase 3-20 fixtures): a
// TrioVoiceCarrier span produces no candidates.
TEST(TrioSonataTest, CarrierInertWhenTrioVoicesEmpty) {
  Material material;  // no trio_voices.
  Span span;
  span.id = 1;
  span.start_tick = 0;
  span.end_tick = kTicksPerBar;
  span.voice = 0;
  span.intent = VoiceIntent::TrioVoiceCarrier;
  const auto cands = CandidateSearch{}.enumerate(span, cMajorWhole(), material, CandidateContext{});
  EXPECT_TRUE(cands.empty());
}

// --- 2. voice_independence_threshold ---------------------------------------

// Three voices with three DISTINCT densities (4 / 2 / 1 notes per beat) over a
// bar: most onset boundaries see only a subset re-articulate, so the mean
// pairwise independence is comfortably above 0.6 and the rule stays silent.
TEST(TrioSonataTest, VoiceIndependencePassesOnDistinctDensities) {
  Material material;  // rule reads notes/provenance, not material directly.
  std::vector<NoteEvent> notes;
  std::vector<NoteProvenance> prov;
  const Tick sixteenth = kTicksPerBeat / 4;
  const Tick eighth = kTicksPerBeat / 2;
  // V0: 16 sixteenths over the bar, an ascending/descending wave.
  for (int i = 0; i < 16; ++i) {
    const std::uint8_t pitch = static_cast<std::uint8_t>(72 + (i < 8 ? i : 16 - i));
    notes.push_back(makeNote(static_cast<Tick>(i) * sixteenth, sixteenth, pitch, 0));
    prov.push_back(trioProv());
  }
  // V1: 8 eighths over the bar, an opposing wave.
  for (int i = 0; i < 8; ++i) {
    const std::uint8_t pitch = static_cast<std::uint8_t>(60 + (i < 4 ? 4 - i : i - 4));
    notes.push_back(makeNote(static_cast<Tick>(i) * eighth, eighth, pitch, 1));
    prov.push_back(trioProv());
  }
  // V2: 4 quarters alternating root/fifth.
  for (int i = 0; i < 4; ++i) {
    const std::uint8_t pitch = static_cast<std::uint8_t>((i % 2 == 0) ? 40 : 47);
    notes.push_back(makeNote(static_cast<Tick>(i) * kTicksPerBeat, kTicksPerBeat, pitch, 2));
    prov.push_back(trioProv());
  }

  const ValidationReport r = Validator{}.validate(notes, prov, cMajorWhole(), material);
  EXPECT_FALSE(hasRule(r, "voice_independence_threshold"));
}

// Inert with fewer than two trio voices: a single bit-62 voice cannot trip the
// rule (no pair to assess).
TEST(TrioSonataTest, VoiceIndependenceInertWithSingleVoice) {
  Material material;
  std::vector<NoteEvent> notes = {
      makeNote(0, kTicksPerBeat, 72, 0),
      makeNote(kTicksPerBeat, kTicksPerBeat, 74, 0),
  };
  std::vector<NoteProvenance> prov = {trioProv(), trioProv()};
  const ValidationReport r = Validator{}.validate(notes, prov, cMajorWhole(), material);
  EXPECT_FALSE(hasRule(r, "voice_independence_threshold"));
}

// Three voices locked in lockstep parallel motion (identical rhythm, all moving
// the same direction at every boundary) are MUSICALLY fully dependent (mean
// independence == 0). The rule MUST soft-fail this with a MusicalFail. Every
// onset boundary re-articulates all three voices in the same upward direction,
// so no boundary is scored independent and the mean independence is 0 < 0.6.
// (Previously the onsetPitch() helper seeded its running maximum with
// `Tick best = -1;`; because Tick is unsigned the wrapped 0xFFFFFFFF was never
// exceeded, the helper always returned its -1 default, every motion read as
// static, and the rule could never fire. Fixed with a `bool found` flag.)
TEST(TrioSonataTest, VoiceIndependenceFailsOnLockstepParallelMotion) {
  Material material;
  std::vector<NoteEvent> notes;
  std::vector<NoteProvenance> prov;
  // Four onsets, all three voices re-articulate together and rise by a step
  // each boundary (parallel/similar motion -> musically dependent everywhere).
  for (int i = 0; i < 4; ++i) {
    const Tick t = static_cast<Tick>(i) * kTicksPerBeat;
    notes.push_back(makeNote(t, kTicksPerBeat, static_cast<std::uint8_t>(72 + i), 0));
    notes.push_back(makeNote(t, kTicksPerBeat, static_cast<std::uint8_t>(64 + i), 1));
    notes.push_back(makeNote(t, kTicksPerBeat, static_cast<std::uint8_t>(48 + i), 2));
    prov.push_back(trioProv());
    prov.push_back(trioProv());
    prov.push_back(trioProv());
  }

  const ValidationReport r = Validator{}.validate(notes, prov, cMajorWhole(), material);
  // Lockstep parallel motion is fully dependent: the rule must soft-fail it.
  EXPECT_TRUE(hasRule(r, "voice_independence_threshold"));
}

// --- 3. Phase21 fixture integration ----------------------------------------

// The BWV525-529 trio-sonata fixture must run through the full Composer cleanly
// for every seed family (seed % 4 selects the V0/V1 scalar-wave start offset):
// no validator failure, three voices at the 16 / 8 / 4 notes-per-bar densities,
// every trio note stamps TrioVoiceIndependent, and the
// voice_independence_threshold rule does not soft-fail.
TEST(TrioSonataTest, Phase21FixtureValidatesCleanAndStampsTrioBit) {
  for (int seed : {0, 1, 2, 3}) {
    const HarnessFixture fx = buildHarnessFixture(HarnessPhase::Phase21, seed);
    const ComposeResult r = Composer{}.run(fx.material, fx.harmony, fx.voice_plan);

    EXPECT_TRUE(r.validation.failures.empty())
        << "seed " << seed << " produced " << r.validation.failures.size() << " failure(s); first="
        << (r.validation.failures.empty() ? "" : r.validation.failures.front().rule_id);
    EXPECT_FALSE(hasRule(r.validation, "voice_independence_threshold"))
        << "seed " << seed << " soft-failed voice_independence_threshold";
    ASSERT_FALSE(r.notes.empty());
    ASSERT_EQ(r.notes.size(), r.provenance.size());
    EXPECT_EQ(fx.voice_plan.num_voices, 3);

    // Three voices present with the 16 / 8 / 4 notes-per-bar densities. The
    // fixture is 16 bars, so the totals are 256 / 128 / 64.
    int v_count[3] = {0, 0, 0};
    bool saw_trio_bit = false;
    bool all_trio_stamped = true;
    for (std::size_t i = 0; i < r.notes.size(); ++i) {
      const VoiceId v = r.notes[i].voice;
      if (v >= 0 && v < 3)
        ++v_count[v];
      if (r.provenance[i].voice_intent == VoiceIntent::TrioVoiceCarrier) {
        if (r.provenance[i].satisfied_rules & bit(RuleBit::TrioVoiceIndependent))
          saw_trio_bit = true;
        else
          all_trio_stamped = false;
      }
    }
    EXPECT_EQ(v_count[0], 256) << "seed " << seed << " V0 (RH) density";
    EXPECT_EQ(v_count[1], 128) << "seed " << seed << " V1 (LH) density";
    EXPECT_EQ(v_count[2], 64) << "seed " << seed << " V2 (Pedal) density";
    EXPECT_TRUE(saw_trio_bit) << "seed " << seed << " missing TrioVoiceIndependent";
    EXPECT_TRUE(all_trio_stamped) << "seed " << seed
                                  << " has a TrioVoiceCarrier note without TrioVoiceIndependent";
  }
}

}  // namespace bach::composer
