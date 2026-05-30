// P14 NCT pipeline integration tests.
//
// Drives the Phase14 all-technique fugue fixture through the Composer and
// inspects the resulting provenance: every P3-P14 RuleBit must fire, the
// four NCT post-pass bits must land on notes inside the authored bar-12..15
// voice-2 window, and a valid seed must produce no validator failures. A
// regression test confirms the NCT post-pass is a pure no-op on a phase
// (Phase11) that authors no NCT figures.
//
// The NctCarrier verbatim-replay property (figure pitches + onsets survive
// the pipeline unchanged with NoteSource::Material) is also pinned here, in
// the same spirit as the SubjectCarrier-replay tests in
// candidate_search_test.cpp.

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

#include "composer/composer.h"
#include "composer/harmonic_plan.h"
#include "composer/harness_fixture.h"
#include "composer/material.h"
#include "composer/provenance.h"
#include "composer/span.h"
#include "composer/voice_plan.h"
#include "core/basic_types.h"

namespace bach::composer {
namespace {

bool hasBit(const NoteProvenance& p, RuleBit bit) {
  return (p.satisfied_rules & (RuleIdMask{1} << bit)) != 0;
}

bool hasAnyNctBit(const NoteProvenance& p) {
  return hasBit(p, RuleBit::CambiataDetected) || hasBit(p, RuleBit::EchappeeDetected) ||
         hasBit(p, RuleBit::AnticipationDetected) || hasBit(p, RuleBit::NotaCambiataDetected);
}

// A single C-major chord covering the whole piece.
HarmonicPlan cMajorPlan() {
  HarmonicPlan plan;
  plan.tonic_pc = 0;
  plan.is_minor = false;
  ChordEvent chord;
  chord.start_tick = 0;
  chord.root_pc = 0;
  chord.quality = ChordQuality::Major;
  chord.has_degree = false;
  plan.chords.push_back(chord);
  return plan;
}

// One NctCarrier span on `voice` covering one bar.
VoicePlan oneNctSpan(VoiceId voice) {
  VoicePlan vp;
  vp.num_voices = static_cast<std::uint8_t>(voice + 1);
  Span span;
  span.id = 0;
  span.start_tick = 0;
  span.end_tick = kTicksPerBar;
  span.voice = voice;
  span.intent = VoiceIntent::NctCarrier;
  span.subdivision = Subdivision::Eighth;
  vp.spans.push_back(span);
  return vp;
}

MaterialNote figNote(Tick start, Tick dur, std::uint8_t pitch) {
  MaterialNote note;
  note.start_tick = start;
  note.duration = dur;
  note.pitch = pitch;
  return note;
}

ComposeResult runPhase(HarnessPhase phase, int seed) {
  HarnessFixture fx = buildHarnessFixture(phase, seed);
  return Composer{}.run(fx.material, fx.harmony, fx.voice_plan);
}

// Union of satisfied_rules across every provenance record.
RuleIdMask satisfiedUnion(const ComposeResult& r) {
  RuleIdMask u = 0;
  for (const auto& p : r.provenance) {
    u |= p.satisfied_rules;
  }
  return u;
}

}  // namespace

// The Phase14 layout exercises every device P3-P14 in one fugue, so the
// union of satisfied rule bits across all notes must cover bits 0..46.
TEST(ComposerNctTest, AllFortySevenBitsFireForPhase14) {
  const ComposeResult r = runPhase(HarnessPhase::Phase14, /*seed=*/0);
  ASSERT_FALSE(r.provenance.empty());
  const RuleIdMask u = satisfiedUnion(r);
  for (int b = 0; b < 47; ++b) {
    EXPECT_TRUE((u & (RuleIdMask{1} << b)) != 0) << "rule bit " << b << " never fired in Phase14";
  }
}

// The four NCT figures live in voice 2, bars 12-15. Each figure's NCT note
// must receive its detector bit, and every NCT-stamped note must sit in
// voice 2 within ticks [12*bar, 16*bar).
TEST(ComposerNctTest, NctBitsLandOnExpectedFigureNotes) {
  const ComposeResult r = runPhase(HarnessPhase::Phase14, /*seed=*/0);
  ASSERT_EQ(r.notes.size(), r.provenance.size());

  const Tick lo = 12 * kTicksPerBar;
  const Tick hi = 16 * kTicksPerBar;
  int cambiata = 0;
  int echappee = 0;
  int anticipation = 0;
  int nota_cambiata = 0;

  for (std::size_t i = 0; i < r.notes.size(); ++i) {
    const NoteProvenance& p = r.provenance[i];
    const bool any_nct =
        hasBit(p, RuleBit::CambiataDetected) || hasBit(p, RuleBit::EchappeeDetected) ||
        hasBit(p, RuleBit::AnticipationDetected) || hasBit(p, RuleBit::NotaCambiataDetected);
    if (!any_nct)
      continue;
    // Every NCT bit must be stamped on a voice-2 note inside the figure window.
    EXPECT_EQ(r.notes[i].voice, 2) << "NCT bit on a non-V2 note at index " << i;
    EXPECT_GE(r.notes[i].start_tick, lo);
    EXPECT_LT(r.notes[i].start_tick, hi);
    cambiata += hasBit(p, RuleBit::CambiataDetected) ? 1 : 0;
    echappee += hasBit(p, RuleBit::EchappeeDetected) ? 1 : 0;
    anticipation += hasBit(p, RuleBit::AnticipationDetected) ? 1 : 0;
    nota_cambiata += hasBit(p, RuleBit::NotaCambiataDetected) ? 1 : 0;
  }

  EXPECT_GE(cambiata, 1) << "no note carried CambiataDetected";
  EXPECT_GE(echappee, 1) << "no note carried EchappeeDetected";
  EXPECT_GE(anticipation, 1) << "no note carried AnticipationDetected";
  EXPECT_GE(nota_cambiata, 1) << "no note carried NotaCambiataDetected";
}

// Seed 0 and seed 1 of Phase14 are clean: the Validator reports no failures.
TEST(ComposerNctTest, Phase14HasNoValidatorFailuresSeed0) {
  const ComposeResult r = runPhase(HarnessPhase::Phase14, /*seed=*/0);
  EXPECT_TRUE(r.validation.failures.empty())
      << "seed 0 produced " << r.validation.failures.size() << " failure(s); first="
      << (r.validation.failures.empty() ? "" : r.validation.failures.front().rule_id);
}

TEST(ComposerNctTest, Phase14HasNoValidatorFailuresSeed1) {
  const ComposeResult r = runPhase(HarnessPhase::Phase14, /*seed=*/1);
  EXPECT_TRUE(r.validation.failures.empty())
      << "seed 1 produced " << r.validation.failures.size() << " failure(s); first="
      << (r.validation.failures.empty() ? "" : r.validation.failures.front().rule_id);
}

// Regression: the NCT post-pass must be a pure no-op when no NCT figures
// exist. Phase11 (development section, no nct_figures) must therefore carry
// none of the four NCT bits on any note.
TEST(ComposerNctTest, Phase11ProvenanceHasNoNctBits) {
  const ComposeResult r = runPhase(HarnessPhase::Phase11, /*seed=*/0);
  ASSERT_FALSE(r.provenance.empty());
  for (const auto& p : r.provenance) {
    EXPECT_FALSE(hasBit(p, RuleBit::CambiataDetected));
    EXPECT_FALSE(hasBit(p, RuleBit::EchappeeDetected));
    EXPECT_FALSE(hasBit(p, RuleBit::AnticipationDetected));
    EXPECT_FALSE(hasBit(p, RuleBit::NotaCambiataDetected));
  }
}

// T2 edge case: a NctCarrier span is planned but Material::nct_figures is
// empty. The carrier emits no notes, so the NCT post-pass has nothing to
// classify; the run must not crash and must stamp no NCT bits anywhere.
TEST(ComposerNctTest, NctCarrierWithEmptyFiguresStampsNothing) {
  Material material;  // nct_figures intentionally empty.
  const HarmonicPlan plan = cMajorPlan();
  const VoicePlan voice_plan = oneNctSpan(/*voice=*/0);

  const ComposeResult r = Composer{}.run(material, plan, voice_plan);
  ASSERT_EQ(r.notes.size(), r.provenance.size());
  for (const auto& prov : r.provenance) {
    EXPECT_FALSE(hasAnyNctBit(prov)) << "empty nct_figures must not produce NCT bits";
  }
}

// T2 edge case: a NctCarrier span replays a SINGLE note. A figure needs at
// least two notes, so no detector can fire. The post-pass must not read out
// of bounds; the run must not crash and must stamp no NCT bits.
TEST(ComposerNctTest, NctCarrierWithSingleNoteDoesNotCrashOrStamp) {
  Material material;
  material.nct_figures.push_back(figNote(0, kTicksPerBeat, 60));  // lone C5.
  const HarmonicPlan plan = cMajorPlan();
  const VoicePlan voice_plan = oneNctSpan(/*voice=*/0);

  const ComposeResult r = Composer{}.run(material, plan, voice_plan);
  ASSERT_EQ(r.notes.size(), r.provenance.size());
  for (const auto& prov : r.provenance) {
    EXPECT_FALSE(hasAnyNctBit(prov)) << "a single-note voice cannot form an NCT figure";
  }
}

// NctCarrier verbatim replay: the authored bar-12..15 voice-2 figure notes
// must survive the pipeline unchanged in pitch and onset, sourced from
// Material (not Compose). We collect every NoteSource::Material V2 note in
// the figure window and confirm the authored pitch sequence is present in
// order.
TEST(ComposerNctTest, NctCarrierReplaysFiguresVerbatim) {
  const ComposeResult r = runPhase(HarnessPhase::Phase14, /*seed=*/0);
  ASSERT_EQ(r.notes.size(), r.provenance.size());

  const Tick b12 = 12 * kTicksPerBar;
  const Tick b13 = 13 * kTicksPerBar;
  const Tick b14 = 14 * kTicksPerBar;
  const Tick b15 = 15 * kTicksPerBar;
  // The authored figure notes (tick, pitch) from harness_fixture.cpp. Bar 13
  // sustains the chord root G3 (55) in beats 1-2 (the bass under the V1 7-6
  // suspension) and runs the nota-cambiata figure in beats 3-4 (+960..+1680).
  const std::vector<std::pair<Tick, std::uint8_t>> expected = {
      {b12 + 0, 69},    {b12 + 240, 67}, {b12 + 480, 62}, {b12 + 720, 64},  {b12 + 960, 66},
      {b13 + 0, 55},    {b13 + 480, 55}, {b13 + 960, 62}, {b13 + 1200, 60}, {b13 + 1440, 57},
      {b13 + 1680, 59}, {b14 + 0, 60},   {b14 + 240, 62}, {b14 + 480, 56},  {b14 + 960, 65},
      {b14 + 1440, 64}, {b15 + 0, 64},
  };

  for (const auto& want : expected) {
    bool found = false;
    for (std::size_t i = 0; i < r.notes.size(); ++i) {
      if (r.notes[i].voice == 2 && r.notes[i].start_tick == want.first &&
          r.notes[i].pitch == want.second && r.provenance[i].source == NoteSource::Material) {
        found = true;
        break;
      }
    }
    EXPECT_TRUE(found) << "missing verbatim NctCarrier note: tick=" << want.first
                       << " pitch=" << static_cast<int>(want.second);
  }
}

}  // namespace bach::composer
