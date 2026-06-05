// Per-piece time-signature (meter) awareness tests.
//
// These tests pin the meter plumbing added so the composer pipeline can produce
// true 3/4 pieces (chaconne / passacaglia) at 480 ticks-per-quarter:
//
//   1. HarmonicPlan::ticksPerBar() derives the bar length from the plan's time
//      signature. The default (4/4) reproduces the global kTicksPerBar (1920);
//      3/4 yields 1440. This is the single canonical bar-length accessor every
//      meter-sensitive validator / candidate-search site reads.
//
//   2. Composer::run validates a synthetic 3/4 layout as Ok and the bar-downbeat
//      rules fire at 1440-tick boundaries. A discriminating case is included
//      whose verdict would FLIP if the bar math still assumed 1920 (proving the
//      plan-derived value is actually consulted, not the global constant).
//
//   3. The 3/4 run is deterministic (same inputs -> byte-identical note list).
//
// All cases run C-internal (tonic_pc = 0) per CLAUDE.md.

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "composer/composer.h"
#include "composer/harmonic_plan.h"
#include "composer/material.h"
#include "composer/provenance.h"
#include "composer/span.h"
#include "composer/validation.h"
#include "composer/validator.h"
#include "composer/voice_intent.h"
#include "composer/voice_plan.h"
#include "core/basic_types.h"

namespace bach::composer {
namespace {

// 3/4 bar length at 480 ticks-per-quarter: 3 * 480 = 1440.
constexpr Tick kThreeFourBar = 1440;

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

// Cantus-firmus provenance (Material source carrying CantusFirmusReplayed),
// mirroring what CandidateSearch stamps for a CantusFirmusCarrier span.
NoteProvenance cfProv() {
  NoteProvenance p;
  p.voice_intent = VoiceIntent::CantusFirmusCarrier;
  p.source = NoteSource::Material;
  p.satisfied_rules = (RuleIdMask{1} << RuleBit::CantusFirmusReplayed);
  return p;
}

bool hasRule(const ValidationReport& report, const std::string& rule_id) {
  for (const auto& failure : report.failures) {
    if (failure.rule_id == rule_id)
      return true;
  }
  return false;
}

// A C-major plan with one chord per 3/4 bar (I-IV-V-I-...) at the given meter.
// Used so the strong-beat rules see a plausible triad at every downbeat.
HarmonicPlan threeFourPlan(std::uint8_t ts_num, std::uint8_t ts_den, std::size_t bars) {
  HarmonicPlan plan;
  plan.tonic_pc = 0;
  plan.is_minor = false;
  plan.ts_numerator = ts_num;
  plan.ts_denominator = ts_den;
  const Tick bar = plan.ticksPerBar();
  static constexpr std::uint8_t kRoots[] = {0, 5, 7, 0};  // I IV V I
  for (std::size_t idx = 0; idx < bars; ++idx) {
    ChordEvent chord;
    chord.start_tick = static_cast<Tick>(idx) * bar;
    chord.root_pc = kRoots[idx % 4];
    chord.quality = ChordQuality::Major;
    plan.chords.push_back(chord);
  }
  return plan;
}

}  // namespace

// --- 1. ticksPerBar() derivation -------------------------------------------

TEST(MeterTest, DefaultPlanIsFourFour) {
  HarmonicPlan plan;  // value-initialised: ts defaults to 4/4.
  EXPECT_EQ(plan.ts_numerator, 4);
  EXPECT_EQ(plan.ts_denominator, 4);
  EXPECT_EQ(plan.ticksPerBar(), kTicksPerBar);
  EXPECT_EQ(plan.ticksPerBar(), 1920);
}

TEST(MeterTest, ThreeFourYields1440) {
  HarmonicPlan plan;
  plan.ts_numerator = 3;
  plan.ts_denominator = 4;
  EXPECT_EQ(plan.ticksPerBar(), kThreeFourBar);
  EXPECT_EQ(plan.ticksPerBar(), 1440);
}

TEST(MeterTest, OtherMetersDeriveCorrectly) {
  HarmonicPlan two_four;
  two_four.ts_numerator = 2;
  two_four.ts_denominator = 4;
  EXPECT_EQ(two_four.ticksPerBar(), 960);

  HarmonicPlan six_eight;
  six_eight.ts_numerator = 6;
  six_eight.ts_denominator = 8;
  EXPECT_EQ(six_eight.ticksPerBar(), 1440);  // 6 * (480 * 4 / 8) = 6 * 240.
}

// --- 2. Composer::run validates a 3/4 layout as Ok -------------------------

// A 4-bar 3/4 two-voice plan: V0 SubjectCarrier (a C-major line whose bar
// downbeats are chord tones) + per-bar V1 SequentialCounterline. The composer
// must validate this Ok with the bar math anchored at 1440-tick boundaries.
TEST(MeterTest, ComposerRunThreeFourValidatesOk) {
  HarmonicPlan plan = threeFourPlan(3, 4, /*bars=*/4);

  Material material;
  // Three quarter notes per 3/4 bar; each downbeat is a chord tone of that
  // bar's chord (I=C/E/G, IV=F/A/C, V=G/B/D, I=C/E/G).
  static constexpr std::uint8_t kBarPitches[4][3] = {
      {72, 74, 76},  // bar 0 over I: C5 down-beat chord tone.
      {77, 79, 81},  // bar 1 over IV: F5 down-beat chord tone.
      {79, 78, 76},  // bar 2 over V: G5 down-beat chord tone.
      {72, 74, 76},  // bar 3 over I: C5 down-beat chord tone.
  };
  for (std::size_t bar = 0; bar < 4; ++bar) {
    for (std::size_t beat = 0; beat < 3; ++beat) {
      const Tick start =
          static_cast<Tick>(bar) * kThreeFourBar + static_cast<Tick>(beat) * kTicksPerBeat;
      material.subject.push_back(mnote(start, kTicksPerBeat, kBarPitches[bar][beat]));
    }
  }

  VoicePlan voice_plan;
  voice_plan.num_voices = 2;
  SpanId next_id = 0;

  Span subject;
  subject.id = next_id++;
  subject.start_tick = 0;
  subject.end_tick = 4 * kThreeFourBar;
  subject.voice = 0;
  subject.intent = VoiceIntent::SubjectCarrier;
  voice_plan.spans.push_back(subject);

  for (std::uint8_t bar = 0; bar < 4; ++bar) {
    Span counter;
    counter.id = next_id++;
    counter.start_tick = static_cast<Tick>(bar) * kThreeFourBar;
    counter.end_tick = counter.start_tick + kThreeFourBar;
    counter.voice = 1;
    counter.intent = VoiceIntent::SequentialCounterline;
    voice_plan.spans.push_back(counter);
  }

  const ComposeResult result = Composer{}.run(material, plan, voice_plan);

  EXPECT_EQ(result.validation.status, ValidationStatus::Ok)
      << "validator found " << result.validation.failures.size() << " failures; first rule_id="
      << (result.validation.failures.empty() ? "<none>"
                                             : result.validation.failures.front().rule_id.c_str());
  EXPECT_FALSE(result.notes.empty());
}

// Discriminating bar-index case (cantus_firmus_immutable).
//
// The cantus firmus skeleton holds one tone per 3/4 bar (ticks 0, 1440, 2880,
// 4320, 5760). The note at tick 5760 carries skeleton[4]'s pitch.
//   * Under 3/4 (bar = 1440): bar_index(5760) = 5760 / 1440 = 4, so the note is
//     compared against skeleton[4] -> MATCH -> rule silent.
//   * If the bar math wrongly assumed 1920: bar_index(5760) = 5760 / 1920 = 3,
//     so the note would be compared against skeleton[3]. skeleton[3] != skel[4],
//     so the rule would FIRE a StructuralFail.
// The rule staying silent therefore proves the validator uses ticksPerBar()
// (1440), not the global kTicksPerBar (1920).
TEST(MeterTest, CantusFirmusBarIndexHonoursThreeFour) {
  // skeleton[3] = D3 (50), skeleton[4] = G3 (55): distinct so the 1920 vs 1440
  // bar-index mapping picks different expected tones.
  static constexpr std::uint8_t kSkeleton[5] = {48, 52, 53, 50, 55};

  Material material;
  HarmonicPlan plan = threeFourPlan(3, 4, /*bars=*/5);

  std::vector<NoteEvent> notes;
  std::vector<NoteProvenance> prov;
  for (std::size_t bar = 0; bar < 5; ++bar) {
    const Tick start = static_cast<Tick>(bar) * kThreeFourBar;
    material.cantus_firmus.push_back(mnote(start, kThreeFourBar, kSkeleton[bar]));
    notes.push_back(makeNote(start, kThreeFourBar, kSkeleton[bar], 1));
    prov.push_back(cfProv());
  }

  const ValidationReport report = Validator{}.validate(notes, prov, plan, material);
  EXPECT_FALSE(hasRule(report, "cantus_firmus_immutable"))
      << "bar-index math must use ticksPerBar()=1440, not 1920";
}

// Guard: confirm the SAME note layout DOES fail when the plan declares 4/4.
// This pins the discriminator -- under 4/4 the note at tick 5760 maps to
// bar_index 3 and mismatches skeleton[3], so the rule must fire. (The note at
// tick 1440/2880/4320 is not a 4/4 downbeat and is skipped.)
TEST(MeterTest, CantusFirmusBarIndexFailsUnderFourFourMismatch) {
  static constexpr std::uint8_t kSkeleton[5] = {48, 52, 53, 50, 55};

  Material material;
  HarmonicPlan plan = threeFourPlan(3, 4, /*bars=*/5);
  plan.ts_numerator = 4;  // force 4/4 bar math while keeping the 1440-spaced notes.
  plan.ts_denominator = 4;

  std::vector<NoteEvent> notes;
  std::vector<NoteProvenance> prov;
  for (std::size_t bar = 0; bar < 5; ++bar) {
    const Tick start = static_cast<Tick>(bar) * kThreeFourBar;
    material.cantus_firmus.push_back(mnote(start, kThreeFourBar, kSkeleton[bar]));
    notes.push_back(makeNote(start, kThreeFourBar, kSkeleton[bar], 1));
    prov.push_back(cfProv());
  }

  const ValidationReport report = Validator{}.validate(notes, prov, plan, material);
  EXPECT_TRUE(hasRule(report, "cantus_firmus_immutable"))
      << "under 4/4 the tick-5760 note maps to bar_index 3 and must mismatch skeleton[3]";
}

// Discriminating strong-beat case (figuration_harmonic_consistency).
//
// A FigurationCommitted note on the 3/4 bar-2 downbeat (tick 2880) is a chord
// tone of the V chord that starts there.
//   * Under 3/4 (bar = 1440): tick 2880 % 1440 == 0 -> it IS a downbeat, the
//     chord-tone check runs and passes.
//   * The note at tick 1440 (3/4 bar 1, IV chord) is intentionally a chord tone
//     too. Under a wrong 1920 assumption tick 1440 is NOT a downbeat, so it is
//     skipped -- but the active chord at tick 1440 differs from the active chord
//     at tick 0, so a wrong-meter check would read the WRONG chord. We make the
//     1440 note dissonant against the I chord (the chord a 1920-downbeat at
//     tick 0 covers up to 1920) so that if the rule ever evaluated tick 1440 as
//     a non-downbeat it would simply skip it. The positive proof is below.
TEST(MeterTest, FigurationDownbeatHonoursThreeFour) {
  HarmonicPlan plan = threeFourPlan(3, 4, /*bars=*/3);

  // FigurationCommitted notes at every 3/4 bar downbeat, each a chord tone of
  // that bar's chord: bar0 I -> C(60), bar1 IV -> F(65), bar2 V -> G(67).
  std::vector<NoteEvent> notes = {
      makeNote(0, kThreeFourBar, 60, 0),
      makeNote(kThreeFourBar, kThreeFourBar, 65, 0),
      makeNote(2 * kThreeFourBar, kThreeFourBar, 67, 0),
  };
  std::vector<NoteProvenance> prov;
  for (std::size_t idx = 0; idx < notes.size(); ++idx) {
    NoteProvenance p;
    p.voice_intent = VoiceIntent::FigurationCarrier;
    p.source = NoteSource::Material;
    p.satisfied_rules = (RuleIdMask{1} << RuleBit::FigurationCommitted);
    prov.push_back(p);
  }

  const ValidationReport report = Validator{}.validate(notes, prov, plan, Material{});
  EXPECT_FALSE(hasRule(report, "figuration_harmonic_consistency"))
      << "bar-downbeat chord-tone anchoring must fire at 1440-tick boundaries";
}

// Guard: a wrong note ON a 3/4 downbeat is caught. The bar-2 downbeat (tick
// 2880, V chord = G/B/D) carries C# (61), a non-chord tone, so the rule must
// fire. Under a 1920 assumption tick 2880 would not be a downbeat and the
// violation would be missed; the rule firing proves 1440 anchoring.
TEST(MeterTest, FigurationDownbeatCatchesDissonanceAt1440Boundary) {
  HarmonicPlan plan = threeFourPlan(3, 4, /*bars=*/3);

  std::vector<NoteEvent> notes = {
      makeNote(0, kThreeFourBar, 60, 0),                  // bar 0 I: C chord tone.
      makeNote(kThreeFourBar, kThreeFourBar, 65, 0),      // bar 1 IV: F chord tone.
      makeNote(2 * kThreeFourBar, kThreeFourBar, 61, 0),  // bar 2 V: C# NON-chord tone.
  };
  std::vector<NoteProvenance> prov;
  for (std::size_t idx = 0; idx < notes.size(); ++idx) {
    NoteProvenance p;
    p.voice_intent = VoiceIntent::FigurationCarrier;
    p.source = NoteSource::Material;
    p.satisfied_rules = (RuleIdMask{1} << RuleBit::FigurationCommitted);
    prov.push_back(p);
  }

  const ValidationReport report = Validator{}.validate(notes, prov, plan, Material{});
  EXPECT_TRUE(hasRule(report, "figuration_harmonic_consistency"))
      << "a non-chord-tone on the tick-2880 (3/4 bar-2) downbeat must be caught";
}

// --- 3. Determinism of the 3/4 run -----------------------------------------

TEST(MeterTest, ThreeFourRunIsDeterministic) {
  HarmonicPlan plan = threeFourPlan(3, 4, /*bars=*/4);

  Material material;
  for (std::size_t bar = 0; bar < 4; ++bar) {
    const std::uint8_t down = (bar % 2 == 0) ? 72 : 76;
    for (std::size_t beat = 0; beat < 3; ++beat) {
      const Tick start =
          static_cast<Tick>(bar) * kThreeFourBar + static_cast<Tick>(beat) * kTicksPerBeat;
      material.subject.push_back(
          mnote(start, kTicksPerBeat, static_cast<std::uint8_t>(down + beat)));
    }
  }

  VoicePlan voice_plan;
  voice_plan.num_voices = 2;
  SpanId next_id = 0;
  Span subject;
  subject.id = next_id++;
  subject.start_tick = 0;
  subject.end_tick = 4 * kThreeFourBar;
  subject.voice = 0;
  subject.intent = VoiceIntent::SubjectCarrier;
  voice_plan.spans.push_back(subject);
  for (std::uint8_t bar = 0; bar < 4; ++bar) {
    Span counter;
    counter.id = next_id++;
    counter.start_tick = static_cast<Tick>(bar) * kThreeFourBar;
    counter.end_tick = counter.start_tick + kThreeFourBar;
    counter.voice = 1;
    counter.intent = VoiceIntent::SequentialCounterline;
    voice_plan.spans.push_back(counter);
  }

  const ComposeResult first = Composer{}.run(material, plan, voice_plan);
  const ComposeResult second = Composer{}.run(material, plan, voice_plan);

  ASSERT_EQ(first.notes.size(), second.notes.size());
  for (std::size_t idx = 0; idx < first.notes.size(); ++idx) {
    EXPECT_EQ(first.notes[idx].start_tick, second.notes[idx].start_tick);
    EXPECT_EQ(first.notes[idx].pitch, second.notes[idx].pitch);
    EXPECT_EQ(first.notes[idx].duration, second.notes[idx].duration);
    EXPECT_EQ(first.notes[idx].voice, second.notes[idx].voice);
  }
  EXPECT_EQ(first.validation.status, second.validation.status);
}

}  // namespace bach::composer
