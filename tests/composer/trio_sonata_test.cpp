// Organ Trio Sonata (BWV525-529) tests.
//
// Covers the two halves of the Trio Sonata foundation (three INDEPENDENT
// voices: RH = Great, LH = Swell, Pedal, each a verbatim-replayed
// TrioVoiceCarrier line):
//   1. CandidateSearch replays each TrioVoiceLine whose voice matches the span
//      verbatim (matched by line.voice == span.voice, not by window position),
//      stamping TrioVoiceIndependent on every note (plus the baseline
//      ChordTone and harmonic-plan bits via emitMaterialNote). The branch is inert when
//      material.trio_voices is empty.
//   2. The Validator's voice_independence_threshold rule (SOFT MusicalFail when
//      the mean pairwise independence of the bit-62 voices drops below 0.6)
//      stays silent on three rhythmically distinct voices, stays inert with
//      fewer than two trio voices, and fires on three rhythmically locked,
//      parallel-moving voices.
//   3. The TrioSonata fixture runs through the full Composer cleanly for every seed
//      family (seed % 4 selects the V0/V1 scalar-wave start offset): three
//      voices at the 16 / 8 / 4 notes-per-bar densities, every trio note stamps
//      TrioVoiceIndependent, and voice_independence_threshold does NOT soft-fail
//      the fixture.
//   4. The trio form declares the prepared suspensions its manuals form, and
//      the chains of suspensions it writes into the sparse manual: each is one
//      note held from preparation to resolution, read by the shared declaration
//      rules, on a tone outside the bar's triad, and stamped when it ships.
//      Every declaration, and the passing tone a chain resumes on, still holds
//      on the final material the later passes leave.

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <vector>

#include "composer/candidate_search.h"
#include "composer/composer.h"
#include "composer/form_director.h"
#include "composer/harmonic_plan.h"
#include "composer/harness_fixture.h"
#include "composer/material.h"
#include "composer/provenance.h"
#include "composer/rule_helpers.h"
#include "composer/span.h"
#include "composer/suspension_declaration.h"
#include "composer/texture_helpers.h"
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
  p.satisfied_rules = (ruleBitMask(RuleBit::TrioVoiceIndependent));
  return p;
}

bool hasRule(const ValidationReport& r, const std::string& rule_id) {
  for (const auto& f : r.failures) {
    if (f.rule_id == rule_id)
      return true;
  }
  return false;
}

constexpr RuleIdMask bit(RuleBit b) {
  return ruleBitMask(b);
}

constexpr std::array<SubjectCharacter, 4> kTrioCharacters = {
    SubjectCharacter::Severe, SubjectCharacter::Playful, SubjectCharacter::Noble,
    SubjectCharacter::Restless};

// Sixteen 8-bar periods, each closing on a pinned dominant seventh bar.
constexpr std::uint16_t kDeclarationBars = 128;

HarnessFixture buildTrioForm(SubjectCharacter character, bool minor, std::uint32_t seed) {
  ComposeRequest req;
  req.form = FormType::TrioSonata;
  req.character = character;
  req.is_minor = minor;
  req.seed = seed;
  req.target_bars = kDeclarationBars;
  HarnessFixture fixture;
  EXPECT_EQ(buildFormFixture(req, &fixture), FormDirectorStatus::Ok);
  return fixture;
}

// A pattern the cadential carrier hosts starts a SuspensionCarrier span; every
// other one is declared on the voice's own trio line.
bool isMaterialHosted(const HarnessFixture& fixture, const SuspensionPattern& pattern) {
  return std::none_of(
      fixture.voice_plan.spans.begin(), fixture.voice_plan.spans.end(), [&](const Span& span) {
        return span.intent == VoiceIntent::SuspensionCarrier && span.voice == pattern.voice &&
               span.start_tick == pattern.preparation_tick;
      });
}

const std::vector<MaterialNote>* trioLine(const HarnessFixture& fixture, VoiceId voice) {
  for (const TrioVoiceLine& line : fixture.material.trio_voices) {
    if (line.voice == voice)
      return &line.notes;
  }
  return nullptr;
}

int lowestOtherTrioPitch(const HarnessFixture& fixture, VoiceId voice, Tick tick) {
  int lowest = -1;
  for (const TrioVoiceLine& line : fixture.material.trio_voices) {
    if (line.voice == voice)
      continue;
    const int pitch = soundingMaterialPitch(line.notes, tick);
    if (pitch >= 0 && (lowest < 0 || pitch < lowest))
      lowest = pitch;
  }
  return lowest;
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

// Inert when there are no trio voices declared: a TrioVoiceCarrier span
// produces no candidates.
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

// --- 3. TrioSonata fixture integration ----------------------------------------

// The BWV525-529 trio-sonata fixture must run through the full Composer cleanly
// for every seed family (seed % 4 selects the V0/V1 scalar-wave start offset):
// no validator failure, three voices at the 16 / 8 / 4 notes-per-bar densities,
// every trio note stamps TrioVoiceIndependent, and the
// voice_independence_threshold rule does not soft-fail.
TEST(TrioSonataTest, TrioSonataFixtureValidatesCleanAndStampsTrioBit) {
  for (int seed : {0, 1, 2, 3}) {
    const HarnessFixture fx = buildHarnessFixture(HarnessPhase::TrioSonata, seed);
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

// --- 4. Suspensions declared on the trio lines ------------------------------

// Every declared trio suspension is one material note from the preparation to
// the resolution, followed by the resolution tone, and reads as its declared
// type under the shared rules, the duration rule included.
TEST(TrioSonataTest, DeclaredSuspensionIsOneNoteHeldIntoItsResolution) {
  std::size_t declared = 0;
  for (SubjectCharacter character : kTrioCharacters) {
    for (bool minor : {false, true}) {
      for (std::uint32_t seed = 1; seed <= 4; ++seed) {
        const HarnessFixture fixture = buildTrioForm(character, minor, seed);
        for (const SuspensionPattern& pattern : fixture.material.suspension_patterns) {
          if (!isMaterialHosted(fixture, pattern))
            continue;
          ++declared;
          const std::vector<MaterialNote>* line = trioLine(fixture, pattern.voice);
          ASSERT_NE(line, nullptr);
          const auto held = std::find_if(line->begin(), line->end(), [&](const MaterialNote& note) {
            return note.start_tick == pattern.preparation_tick;
          });
          ASSERT_NE(held, line->end()) << "seed=" << seed << " tick=" << pattern.preparation_tick;
          EXPECT_EQ(held->duration, pattern.resolution_tick - pattern.preparation_tick);
          EXPECT_EQ(held->pitch, pattern.preparation_pitch);
          EXPECT_EQ(held->pitch, pattern.suspension_pitch);
          ASSERT_NE(held + 1, line->end());
          EXPECT_EQ((held + 1)->start_tick, pattern.resolution_tick);
          EXPECT_EQ((held + 1)->pitch, pattern.resolution_pitch);
          EXPECT_GE(pattern.suspension_tick - pattern.preparation_tick,
                    pattern.resolution_tick - pattern.suspension_tick);

          DeclaredSuspensionReading reading;
          reading.preparation_tick = pattern.preparation_tick;
          reading.suspension_tick = pattern.suspension_tick;
          reading.resolution_tick = pattern.resolution_tick;
          reading.held_pitch = pattern.suspension_pitch;
          reading.resolution_pitch = pattern.resolution_pitch;
          reading.lowest_other_at_preparation =
              lowestOtherTrioPitch(fixture, pattern.voice, pattern.preparation_tick);
          reading.lowest_other_at_suspension =
              lowestOtherTrioPitch(fixture, pattern.voice, pattern.suspension_tick);
          reading.lowest_other_at_resolution =
              lowestOtherTrioPitch(fixture, pattern.voice, pattern.resolution_tick);
          EXPECT_TRUE(declaredSuspensionHolds(fixture.harmony, reading, pattern.type))
              << "seed=" << seed << " minor=" << minor << " tick=" << pattern.suspension_tick;
        }
      }
    }
  }
  EXPECT_GT(declared, 0u);
}

// A declaration suspends a tone outside the bar's triad -- the dominant's seventh
// before an internal cadence, or the fourth, seventh or ninth a chain holds over
// the bar's root -- on beat one or three. A re-struck chord root over a pedal
// fifth fits the interval table but is no dissonance and stays undeclared.
TEST(TrioSonataTest, DeclaredSuspensionHoldsAToneOutsideTheTriad) {
  for (SubjectCharacter character : kTrioCharacters) {
    for (bool minor : {false, true}) {
      for (std::uint32_t seed = 1; seed <= 4; ++seed) {
        const HarnessFixture fixture = buildTrioForm(character, minor, seed);
        for (const SuspensionPattern& pattern : fixture.material.suspension_patterns) {
          if (!isMaterialHosted(fixture, pattern))
            continue;
          const Tick bar_offset = pattern.suspension_tick % kTicksPerBar;
          EXPECT_TRUE(bar_offset == 0 || bar_offset == kTicksPerBar / 2)
              << "seed=" << seed << " tick=" << pattern.suspension_tick;
          const ChordEvent* chord = rule_helpers::chordAt(fixture.harmony, pattern.suspension_tick);
          ASSERT_NE(chord, nullptr);
          const std::array<std::uint8_t, 3> triad = rule_helpers::triadPitchClasses(*chord);
          const auto held_pc = static_cast<std::uint8_t>(pattern.suspension_pitch % 12);
          EXPECT_EQ(std::find(triad.begin(), triad.end(), held_pc), triad.end())
              << "seed=" << seed << " minor=" << minor << " tick=" << pattern.suspension_tick;
        }
      }
    }
  }
}

TEST(TrioSonataTest, DeclaredSuspensionsAreDeterministicPerSeed) {
  for (SubjectCharacter character : kTrioCharacters) {
    for (std::uint32_t seed = 1; seed <= 4; ++seed) {
      const HarnessFixture first = buildTrioForm(character, /*minor=*/false, seed);
      const HarnessFixture second = buildTrioForm(character, /*minor=*/false, seed);
      ASSERT_EQ(first.material.suspension_patterns.size(),
                second.material.suspension_patterns.size());
      for (std::size_t idx = 0; idx < first.material.suspension_patterns.size(); ++idx) {
        const SuspensionPattern& lhs = first.material.suspension_patterns[idx];
        const SuspensionPattern& rhs = second.material.suspension_patterns[idx];
        EXPECT_EQ(lhs.type, rhs.type);
        EXPECT_EQ(lhs.voice, rhs.voice);
        EXPECT_EQ(lhs.preparation_tick, rhs.preparation_tick);
        EXPECT_EQ(lhs.suspension_tick, rhs.suspension_tick);
        EXPECT_EQ(lhs.resolution_tick, rhs.resolution_tick);
        EXPECT_EQ(lhs.suspension_pitch, rhs.suspension_pitch);
        EXPECT_EQ(lhs.resolution_pitch, rhs.resolution_pitch);
      }
      ASSERT_EQ(first.material.trio_voices.size(), second.material.trio_voices.size());
      for (std::size_t voice = 0; voice < first.material.trio_voices.size(); ++voice) {
        const std::vector<MaterialNote>& lhs = first.material.trio_voices[voice].notes;
        const std::vector<MaterialNote>& rhs = second.material.trio_voices[voice].notes;
        ASSERT_EQ(lhs.size(), rhs.size());
        for (std::size_t idx = 0; idx < lhs.size(); ++idx) {
          EXPECT_EQ(lhs[idx].start_tick, rhs[idx].start_tick);
          EXPECT_EQ(lhs[idx].duration, rhs[idx].duration);
          EXPECT_EQ(lhs[idx].pitch, rhs[idx].pitch);
        }
      }
    }
  }
}

// Every declaration still describes the final material after the closing repair and the
// bar-line pass have run: one held note from the preparation to the resolution, the
// resolution tone starting there, and -- where the line resumes on a tone outside the bar's
// triad, which only the chain's passing tone writes -- an eighth a step from the resolution,
// left by step to the beat's own tone an eighth later. A weak-beat resume is the case
// unprepared_dissonance reads; a strong-beat one is held to the same shape.
TEST(TrioSonataTest, DeclaredSuspensionsSurviveTheLaterPassesExactly) {
  constexpr Tick kEighth = kTicksPerBeat / 2;
  const FormSpec& spec = formSpec(FormType::TrioSonata);
  const std::array<std::uint16_t, 3> lengths = {
      spec.natural_bars, static_cast<std::uint16_t>(spec.natural_bars * 2), kDeclarationBars};
  std::size_t declared = 0;
  std::size_t passing = 0;
  for (std::uint32_t seed : {1u, 5u, 42u, 99u}) {
    for (bool minor : {false, true}) {
      for (std::uint16_t bars : lengths) {
        ComposeRequest req;
        req.form = FormType::TrioSonata;
        req.seed = seed;
        req.is_minor = minor;
        req.target_bars = bars;
        HarnessFixture fixture;
        ASSERT_EQ(buildFormFixture(req, &fixture), FormDirectorStatus::Ok);
        for (const SuspensionPattern& pattern : fixture.material.suspension_patterns) {
          if (!isMaterialHosted(fixture, pattern))
            continue;
          ++declared;
          const std::vector<MaterialNote>* line = trioLine(fixture, pattern.voice);
          ASSERT_NE(line, nullptr);
          const auto held = std::find_if(line->begin(), line->end(), [&](const MaterialNote& note) {
            return note.start_tick == pattern.preparation_tick;
          });
          ASSERT_NE(held, line->end()) << "seed=" << seed << " minor=" << minor << " bars=" << bars
                                       << " tick=" << pattern.preparation_tick;
          EXPECT_EQ(held->start_tick + held->duration, pattern.resolution_tick)
              << "seed=" << seed << " minor=" << minor << " bars=" << bars;
          EXPECT_EQ(held->pitch, pattern.suspension_pitch) << "seed=" << seed;
          ASSERT_NE(held + 1, line->end());
          const MaterialNote& resolved = *(held + 1);
          EXPECT_EQ(resolved.start_tick, pattern.resolution_tick) << "seed=" << seed;
          EXPECT_EQ(resolved.pitch, pattern.resolution_pitch)
              << "seed=" << seed << " minor=" << minor << " bars=" << bars
              << " tick=" << pattern.resolution_tick;

          const Tick resume =
              pattern.resolution_tick + (pattern.resolution_tick - pattern.suspension_tick);
          if (held + 2 == line->end() || (held + 2)->start_tick != resume)
            continue;
          const MaterialNote& resumed = *(held + 2);
          const ChordEvent* chord = rule_helpers::chordAt(fixture.harmony, resume);
          ASSERT_NE(chord, nullptr);
          const std::array<std::uint8_t, 3> triad = rule_helpers::triadPitchClasses(*chord);
          if (std::find(triad.begin(), triad.end(),
                        static_cast<std::uint8_t>(resumed.pitch % 12)) != triad.end())
            continue;
          ++passing;
          EXPECT_EQ(resumed.duration, kEighth) << "seed=" << seed << " tick=" << resume;
          const int approach = std::abs(static_cast<int>(resumed.pitch) - resolved.pitch);
          EXPECT_TRUE(approach >= 1 && approach <= 2)
              << "seed=" << seed << " minor=" << minor << " bars=" << bars << " tick=" << resume;
          ASSERT_NE(held + 3, line->end());
          const MaterialNote& anchor = *(held + 3);
          EXPECT_EQ(anchor.start_tick, resume + kEighth) << "seed=" << seed;
          const int leave = std::abs(static_cast<int>(anchor.pitch) - resumed.pitch);
          EXPECT_TRUE(leave >= 1 && leave <= 2)
              << "seed=" << seed << " minor=" << minor << " bars=" << bars << " tick=" << resume;
        }
      }
    }
  }
  EXPECT_GT(declared, 0u);
  EXPECT_GT(passing, 0u);
}

// The held tone ships as one note carrying SuspensionPrepared, nothing in the
// voice strikes the suspension tick again, and the resolution carries
// SuspensionResolved.
TEST(TrioSonataTest, DeclaredSuspensionShipsTiedWithItsBits) {
  for (SubjectCharacter character : {SubjectCharacter::Severe, SubjectCharacter::Restless}) {
    for (bool minor : {false, true}) {
      for (std::uint32_t seed : {1u, 3u}) {
        const HarnessFixture fixture = buildTrioForm(character, minor, seed);
        const ComposeResult result =
            Composer{}.run(fixture.material, fixture.harmony, fixture.voice_plan);
        ASSERT_EQ(result.validation.status, ValidationStatus::Ok)
            << "seed=" << seed << " minor=" << minor << " first="
            << (result.validation.failures.empty() ? ""
                                                   : result.validation.failures.front().rule_id);
        for (const SuspensionPattern& pattern : fixture.material.suspension_patterns) {
          if (!isMaterialHosted(fixture, pattern))
            continue;
          bool held = false;
          bool resolved = false;
          for (std::size_t idx = 0; idx < result.notes.size(); ++idx) {
            const NoteEvent& note = result.notes[idx];
            if (note.voice != pattern.voice)
              continue;
            EXPECT_NE(note.start_tick, pattern.suspension_tick)
                << "seed=" << seed << " re-struck at " << pattern.suspension_tick;
            const RuleIdMask rules = result.provenance[idx].satisfied_rules;
            if (note.start_tick == pattern.preparation_tick) {
              held = note.duration == pattern.resolution_tick - pattern.preparation_tick &&
                     (rules & bit(RuleBit::SuspensionPrepared)).any();
            }
            if (note.start_tick == pattern.resolution_tick) {
              resolved = note.pitch == pattern.resolution_pitch &&
                         (rules & bit(RuleBit::SuspensionResolved)).any();
            }
          }
          EXPECT_TRUE(held) << "seed=" << seed << " tick=" << pattern.preparation_tick;
          EXPECT_TRUE(resolved) << "seed=" << seed << " tick=" << pattern.resolution_tick;
        }
      }
    }
  }
}

}  // namespace bach::composer
