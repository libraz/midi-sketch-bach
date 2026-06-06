// Solo String Flow (BWV1007) tests.
//
// Covers the two halves of the Flow foundation:
//   1. CandidateSearch replays Material::arpeggio_template verbatim under an
//      ArpeggioFlow span, stamping ArpeggioFlowActive + ImplicitVoiceTracked
//      on every note.
//   2. The Validator's two Flow rules — implicit_voice_counterpoint (each
//      register-defined implicit-voice stream, per-cell min/max, is
//      melodically valid) and arpeggio_no_parallel_perfect (bass / top
//      implicit streams do not move in parallel perfect 5ths/8ves across
//      cells) — fire on violations and stay silent on clean lines.

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

NoteEvent note(Tick start, std::uint8_t pitch) {
  NoteEvent n;
  n.start_tick = start;
  n.duration = kTicksPerBeat / 4;
  n.pitch = pitch;
  n.voice = 0;
  n.velocity = 80;
  return n;
}

NoteProvenance flowProv() {
  NoteProvenance p;
  p.voice_intent = VoiceIntent::ArpeggioFlow;
  p.source = NoteSource::Material;
  p.satisfied_rules =
      (ruleBitMask(RuleBit::ArpeggioFlowActive)) | (ruleBitMask(RuleBit::ImplicitVoiceTracked));
  return p;
}

bool hasRule(const ValidationReport& r, const std::string& rule_id) {
  for (const auto& f : r.failures) {
    if (f.rule_id == rule_id)
      return true;
  }
  return false;
}

constexpr Tick kSix = kTicksPerBeat / 4;  // sixteenth note.

}  // namespace

// --- 1. CandidateSearch verbatim replay ------------------------------------

TEST(ArpeggioFlowTest, ReplaysTemplateVerbatimWithFlowBits) {
  Material material;
  material.arpeggio_template.group_size = 4;
  // One sixteenth-note cell of a C-major arpeggio: C4 E4 G4 E4.
  const std::uint8_t pitches[4] = {60, 64, 67, 64};
  for (int i = 0; i < 4; ++i)
    material.arpeggio_template.notes.push_back(mnote(i * kSix, kSix, pitches[i]));

  Span span;
  span.id = 1;
  span.start_tick = 0;
  span.end_tick = kTicksPerBeat;
  span.voice = 0;
  span.intent = VoiceIntent::ArpeggioFlow;

  CandidateContext ctx;
  const auto cands = CandidateSearch{}.enumerate(span, cMajorWhole(), material, ctx);

  ASSERT_EQ(cands.size(), 4u);
  for (int i = 0; i < 4; ++i) {
    EXPECT_EQ(cands[i].pitch, pitches[i]);
    EXPECT_EQ(cands[i].start_tick, static_cast<Tick>(i * kSix));
    EXPECT_NE(cands[i].satisfied_rules & (ruleBitMask(RuleBit::ArpeggioFlowActive)), 0u)
        << "note " << i << " missing ArpeggioFlowActive";
    EXPECT_NE(cands[i].satisfied_rules & (ruleBitMask(RuleBit::ImplicitVoiceTracked)), 0u)
        << "note " << i << " missing ImplicitVoiceTracked";
  }
}

TEST(ArpeggioFlowTest, ReplayClipsNotesOutsideSpanWindow) {
  Material material;
  material.arpeggio_template.group_size = 4;
  // Two cells; the span covers only the first beat, so cell 2 is clipped.
  for (int cell = 0; cell < 2; ++cell)
    for (int i = 0; i < 4; ++i)
      material.arpeggio_template.notes.push_back(mnote(cell * kTicksPerBeat + i * kSix, kSix, 60));

  Span span;
  span.id = 1;
  span.start_tick = 0;
  span.end_tick = kTicksPerBeat;  // first beat only.
  span.voice = 0;
  span.intent = VoiceIntent::ArpeggioFlow;

  const auto cands = CandidateSearch{}.enumerate(span, cMajorWhole(), material, CandidateContext{});
  EXPECT_EQ(cands.size(), 4u);
}

// --- 2a. implicit_voice_counterpoint --------------------------------------

// Two cells whose bass stream (per-cell min) leaps C4 -> F#4 = a tritone. The
// implicit bass line is melodically invalid.
TEST(ArpeggioFlowTest, ImplicitVoiceCounterpointFiresOnTritoneBassStream) {
  Material material;
  material.arpeggio_template.group_size = 4;
  // cell 0: C4 E4 G4 E4 ; cell 1: F#4(bass) A4 C5 A4  -> bass C4->F#4 tritone.
  const std::uint8_t p[8] = {60, 64, 67, 64, 66, 69, 72, 69};

  std::vector<NoteEvent> notes;
  std::vector<NoteProvenance> prov;
  for (int i = 0; i < 8; ++i) {
    notes.push_back(note(i * kSix, p[i]));
    prov.push_back(flowProv());
  }

  const ValidationReport r = Validator{}.validate(notes, prov, cMajorWhole(), material);
  EXPECT_TRUE(hasRule(r, "implicit_voice_counterpoint"));
}

// A clean two-cell line: bass C4 -> D4 (step), top stays in band, no forbidden
// leaps in any stream.
TEST(ArpeggioFlowTest, ImplicitVoiceCounterpointSilentOnStepwiseStreams) {
  Material material;
  material.arpeggio_template.group_size = 4;
  // cell 0: C4 E4 G4 E4 ; cell 1: D4 F4 A4 F4 (each stream moves by step).
  const std::uint8_t p[8] = {60, 64, 67, 64, 62, 65, 69, 65};

  std::vector<NoteEvent> notes;
  std::vector<NoteProvenance> prov;
  for (int i = 0; i < 8; ++i) {
    notes.push_back(note(i * kSix, p[i]));
    prov.push_back(flowProv());
  }

  const ValidationReport r = Validator{}.validate(notes, prov, cMajorWhole(), material);
  EXPECT_FALSE(hasRule(r, "implicit_voice_counterpoint"));
}

// --- 2b. arpeggio_no_parallel_perfect -------------------------------------

// Two cells where the bass/top streams both rise a whole step while framing a
// perfect 5th in both cells: C4..G4 (P5) -> D4..A4 (P5), similar motion =
// parallel fifths.
TEST(ArpeggioFlowTest, ParallelPerfectFiresOnSimilarMotionFifths) {
  Material material;
  material.arpeggio_template.group_size = 4;
  // cell 0: C4 E4 G4 E4 (bass 60, top 67 -> P5) ;
  // cell 1: D4 F4 A4 F4 (bass 62, top 69 -> P5), both rose a step.
  const std::uint8_t p[8] = {60, 64, 67, 64, 62, 65, 69, 65};

  std::vector<NoteEvent> notes;
  std::vector<NoteProvenance> prov;
  for (int i = 0; i < 8; ++i) {
    notes.push_back(note(i * kSix, p[i]));
    prov.push_back(flowProv());
  }

  const ValidationReport r = Validator{}.validate(notes, prov, cMajorWhole(), material);
  EXPECT_TRUE(hasRule(r, "arpeggio_no_parallel_perfect"));
}

// When the framed interval changes between cells the line escapes parallel
// motion, so the rule stays silent. (A maintained P5 with a rising bass forces
// a rising top — that is precisely the parallel case the rule catches — so the
// legitimate "clean" case is one where the second cell frames a different
// interval class.)
TEST(ArpeggioFlowTest, ParallelPerfectSilentWhenFramedIntervalChanges) {
  Material material;
  material.arpeggio_template.group_size = 4;
  // cell 0: C4 E4 G4 E4 (bass 60, top 67 -> P5) ;
  // cell 1: D4 E4 F4 E4 (bass 62, top 65 -> minor 3rd, ic 3): not perfect.
  const std::uint8_t p[8] = {60, 64, 67, 64, 62, 64, 65, 64};

  std::vector<NoteEvent> notes;
  std::vector<NoteProvenance> prov;
  for (int i = 0; i < 8; ++i) {
    notes.push_back(note(i * kSix, p[i]));
    prov.push_back(flowProv());
  }

  const ValidationReport r = Validator{}.validate(notes, prov, cMajorWhole(), material);
  EXPECT_FALSE(hasRule(r, "arpeggio_no_parallel_perfect"));
}

// --- 3. Phase15 fixture integration ---------------------------------------

// The BWV1007 arpeggio fixture must run through the full Composer cleanly for
// every seed family (seed%4 selects the figure ordering): no validator
// failure, single voice, and both Flow bits stamped on every note.
TEST(ArpeggioFlowTest, Phase15FixtureValidatesCleanAndStampsFlowBits) {
  for (int seed : {0, 1, 2, 3, 4, 5}) {
    const HarnessFixture fx = buildHarnessFixture(HarnessPhase::Phase15, seed);
    const ComposeResult r = Composer{}.run(fx.material, fx.harmony, fx.voice_plan);

    EXPECT_TRUE(r.validation.failures.empty())
        << "seed " << seed << " produced " << r.validation.failures.size() << " failure(s); first="
        << (r.validation.failures.empty() ? "" : r.validation.failures.front().rule_id);
    ASSERT_FALSE(r.notes.empty());
    ASSERT_EQ(r.notes.size(), r.provenance.size());
    for (std::size_t i = 0; i < r.notes.size(); ++i) {
      EXPECT_EQ(r.notes[i].voice, 0) << "Flow is monophonic";
      EXPECT_NE(r.provenance[i].satisfied_rules & (ruleBitMask(RuleBit::ArpeggioFlowActive)), 0u);
      EXPECT_NE(r.provenance[i].satisfied_rules & (ruleBitMask(RuleBit::ImplicitVoiceTracked)), 0u);
    }
  }
}

}  // namespace bach::composer
