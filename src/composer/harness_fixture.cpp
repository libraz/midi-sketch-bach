#include "composer/harness_fixture.h"

#include <array>
#include <cstdint>

#include "composer/motif_ops.h"
#include "composer/span.h"
#include "composer/tonal_answer.h"
#include "composer/voice_intent.h"
#include "core/basic_types.h"

namespace bach::composer {

namespace {

// 5 subject patterns × 16 quarter-note pitches each. Diatonic to C
// major / A natural-minor. Same catalog the gtest harness uses; the
// canonical copy lives here so the harness test and the CLI dispatch
// path stay byte-identical.
constexpr std::array<std::array<std::uint8_t, 16>, 5> kSubjectPatterns = {{
    // 0: original Phase 3 arch
    {72, 74, 76, 77, 79, 81, 79, 77, 76, 74, 76, 77, 79, 77, 71, 72},
    // 1: descent then ascent (start high)
    {84, 83, 84, 79, 77, 76, 77, 79, 81, 79, 77, 76, 74, 76, 71, 72},
    // 2: broken triad outline
    {79, 76, 79, 84, 76, 79, 76, 72, 74, 77, 74, 71, 72, 76, 71, 72},
    // 3: stepwise sequence
    {71, 72, 76, 77, 74, 76, 77, 79, 76, 77, 79, 81, 77, 79, 71, 72},
    // 4: upper-arch
    {76, 77, 79, 81, 79, 77, 76, 74, 72, 74, 76, 77, 79, 77, 71, 72},
}};

struct ChordSpec {
  std::uint8_t root_pc;
  bool minor;
};

// 4 harmony patterns × 4 chords each. Roman numerals for reference:
// 0=I-IV-V-I, 1=I-vi-IV-V, 2=I-IV-I-V, 3=I-V-vi-I (deceptive resolved).
constexpr std::array<std::array<ChordSpec, 4>, 4> kHarmonyPatterns = {{
    {{{0, false}, {5, false}, {7, false}, {0, false}}},
    {{{0, false}, {9, true}, {5, false}, {7, false}}},
    {{{0, false}, {5, false}, {0, false}, {7, false}}},
    {{{0, false}, {7, false}, {9, true}, {0, false}}},
}};

void pushCounterlineBar(VoicePlan& vp, SpanId& next_id, std::uint8_t voice, int bar,
                        Subdivision subdivision) {
  Span s;
  s.id = next_id++;
  s.start_tick = static_cast<Tick>(bar) * kTicksPerBar;
  s.end_tick = s.start_tick + kTicksPerBar;
  s.voice = voice;
  s.intent = VoiceIntent::SequentialCounterline;
  s.subdivision = subdivision;
  vp.spans.push_back(s);
}

}  // namespace

HarnessPhaseSpec phaseSpec(HarnessPhase phase) {
  switch (phase) {
    case HarnessPhase::Phase3:
      return {phase, /*voices=*/2, /*bars=*/8, /*subject_bars=*/8,
              false, false,        false,      false,
              false, false,        false,      false,
              false, false,        false};
    case HarnessPhase::Phase35:
      return {phase, /*voices=*/2, /*bars=*/4, /*subject_bars=*/4,
              false, false,        false,      false,
              false, false,        false,      false,
              false, false,        false};
    case HarnessPhase::Phase4:
      return {phase, /*voices=*/2, /*bars=*/8, /*subject_bars=*/4,
              true,  false,        false,      false,
              false, false,        false,      false,
              false, false,        false};
    case HarnessPhase::Phase5:
      return {phase, /*voices=*/3, /*bars=*/12, /*subject_bars=*/12,
              false, false,        false,       false,
              false, false,        false,       false,
              false, false,        false};
    case HarnessPhase::Phase6:
      return {phase, /*voices=*/3, /*bars=*/16, /*subject_bars=*/4,
              true,  true,         false,       false,
              false, false,        false,       false,
              false, false,        false};
    case HarnessPhase::Phase4Sus:
      return {phase, /*voices=*/2, /*bars=*/8, /*subject_bars=*/4,
              true,  false,        true,       false,
              false, false,        false,      false,
              false, false,        false};
    case HarnessPhase::Phase6Episode:
      return {phase, /*voices=*/3, /*bars=*/16, /*subject_bars=*/4,
              true,  true,         false,       true,
              false, false,        false,       false,
              false, false,        false};
    case HarnessPhase::Phase6Tonal:
      return {phase, /*voices=*/3, /*bars=*/16, /*subject_bars=*/4,
              true,  true,         false,       false,
              true,  false,        false,       false,
              false, false,        false};
    case HarnessPhase::Phase7:
      return {phase, /*voices=*/3, /*bars=*/16, /*subject_bars=*/4,
              true,  true,         false,       false,
              false, true,         false,       false,
              false, false,        false};
    case HarnessPhase::Phase8:
      return {phase, /*voices=*/3, /*bars=*/16, /*subject_bars=*/4,
              true,  true,         false,       false,
              false, true,         true,        false,
              false, false,        false};
    case HarnessPhase::Phase9:
      return {phase, /*voices=*/3, /*bars=*/16, /*subject_bars=*/4,
              true,  true,         false,       false,
              false, true,         true,        true,
              true,  false,        false};
    case HarnessPhase::Phase10:
      return {phase, /*voices=*/3, /*bars=*/16, /*subject_bars=*/4,
              true,  true,         false,       false,
              false, true,         true,        false,
              false, false,        false};
    case HarnessPhase::Phase11:
      // 28 bar / 3 voice. Material assembly reuses with_answer +
      // with_third_entry (subject 0-3, answer 4-7, V2 re-entry 8-11);
      // with_development drives the bars 12-27 carriers and its own
      // voice plan. Degree tagging is on (like Phase7) so the P10
      // strong-4th candidate pre-filter — gated on chord.has_degree —
      // stays active for the exposition's Compose counterlines; without
      // it the composer would pick a strong-beat perfect 4th in the
      // (V0, V1) upper pair and trip fourth_only_on_weak_beat. Modulation
      // stays off (no chromatic idioms); the all-Material development
      // needs no P7/P8 help and gate (4) only needs the P11 bits.
      return {phase, /*voices=*/3, /*bars=*/28, /*subject_bars=*/4,
              true,  true,         false,       false,
              false, true,         false,       false,
              false, true,         false};
    case HarnessPhase::Phase12:
      // 28 bar / 3 voice. Same exposition assembly as Phase11 (with_answer
      // + with_third_entry + degree tagging for the P10 strong-4th
      // pre-filter), but with_rhythm drives the bars 12-27 rhythm section
      // and its own voice plan instead of with_development.
      return {phase, /*voices=*/3, /*bars=*/28, /*subject_bars=*/4,
              true,  true,         false,       false,
              false, true,         false,       false,
              false, false,        true};
  }
  return {phase, 2,     8,     8,     false, false, false, false,
          false, false, false, false, false, false, false};
}

HarnessFixture buildHarnessFixture(HarnessPhase phase, int seed) {
  const HarnessPhaseSpec spec = phaseSpec(phase);
  HarnessFixture out;

  const int num_blocks = spec.bars / 4;
  const int subj_a = (seed / 4) % 5;
  const int harm_a = seed % 4;
  const bool eighth = (seed % 2) == 1;
  const Subdivision subdivision = eighth ? Subdivision::Eighth : Subdivision::Quarter;

  auto subj_idx_for = [&](int blk) { return (subj_a + blk) % 5; };
  auto harm_idx_for = [&](int blk) { return (harm_a + blk) % 4; };

  const int subject_bars = spec.subject_bars;
  const int subject_blocks = subject_bars / 4;

  // V0 SubjectCarrier material.
  for (int blk = 0; blk < subject_blocks; ++blk) {
    const auto& pattern = kSubjectPatterns[subj_idx_for(blk)];
    for (int n = 0; n < 16; ++n) {
      MaterialNote mn;
      const int bar_in_block = n / 4;
      const int beat_in_bar = n % 4;
      mn.start_tick = static_cast<Tick>(blk * 4 + bar_in_block) * kTicksPerBar +
                      static_cast<Tick>(beat_in_bar) * kTicksPerBeat;
      mn.duration = kTicksPerBeat;
      mn.pitch = pattern[n];
      out.material.subject.push_back(mn);
    }
  }

  // V2 SubjectCarrier re-entry (Phase 6 only). Pattern -P8 so it sits
  // below the existing two voices without crossing.
  if (spec.with_third_entry) {
    const auto& src = kSubjectPatterns[subj_a];
    const int entry_bar = 2 * subject_bars;
    for (int n = 0; n < 16; ++n) {
      MaterialNote mn;
      const int bar_in_block = n / 4;
      const int beat_in_bar = n % 4;
      mn.start_tick = static_cast<Tick>(entry_bar + bar_in_block) * kTicksPerBar +
                      static_cast<Tick>(beat_in_bar) * kTicksPerBeat;
      mn.duration = kTicksPerBeat;
      mn.pitch = static_cast<std::uint8_t>(src[n] - 12);
      out.material.subject.push_back(mn);
    }
  }

  // V1 AnswerCarrier material (Phase 4+): real answer = subject -P4.
  if (spec.with_answer) {
    const auto& src = kSubjectPatterns[subj_a];
    for (int n = 0; n < 16; ++n) {
      MaterialNote an;
      const int bar_in_block = n / 4;
      const int beat_in_bar = n % 4;
      an.start_tick = static_cast<Tick>(subject_bars + bar_in_block) * kTicksPerBar +
                      static_cast<Tick>(beat_in_bar) * kTicksPerBeat;
      an.duration = kTicksPerBeat;
      an.pitch = static_cast<std::uint8_t>(src[n] - 5);
      out.material.answer.push_back(an);
    }
  }

  // Harmony.
  out.harmony.tonic_pc = 0;
  out.harmony.is_minor = false;
  for (int blk = 0; blk < num_blocks; ++blk) {
    const auto& pattern = kHarmonyPatterns[harm_idx_for(blk)];
    for (int b = 0; b < 4; ++b) {
      ChordEvent c;
      c.start_tick = static_cast<Tick>(blk * 4 + b) * kTicksPerBar;
      c.root_pc = pattern[b].root_pc;
      c.quality = pattern[b].minor ? ChordQuality::Minor : ChordQuality::Major;
      if (spec.with_degree_tagging) {
        // Phase7 enriches every ChordEvent with degree/inversion/function
        // so the Validator's P7 rules (doubling, spacing) fire and the
        // candidate provenance picks up ChordToneRoman / InversionLabel /
        // DoublingChecked / SpacingChecked bits. Mapping is fixed for the
        // C-major harness vocabulary:
        //   (root=0, !minor) → I (Tonic)
        //   (root=5, !minor) → IV (Subdominant)
        //   (root=7, !minor) → V (Dominant)
        //   (root=9, minor)  → vi (Predominant)
        // All Phase7 chords are emitted in root position.
        if (pattern[b].root_pc == 0) {
          c.degree = RomanNumeral::I;
          c.function = HarmonicFunction::T;
        } else if (pattern[b].root_pc == 5) {
          c.degree = RomanNumeral::IV;
          c.function = HarmonicFunction::S;
        } else if (pattern[b].root_pc == 7) {
          c.degree = RomanNumeral::V;
          c.function = HarmonicFunction::D;
        } else if (pattern[b].root_pc == 9 && pattern[b].minor) {
          c.degree = RomanNumeral::VI;
          c.function = HarmonicFunction::Pred;
        }
        c.inversion = ChordInversion::Root;
        c.has_degree = true;
      }
      out.harmony.chords.push_back(c);
    }
  }

  // Phase8 modulation injection. Augments the Phase7 layout with:
  //   - a ModulationEvent at bar 8 (the boundary is the implicit
  //     I-of-C = IV-of-G pivot already at that tick),
  //   - a V/V → V secondary-dominant pair at bars 12-13,
  //   - a borrowed iv (parallel minor mixture) at bar 14,
  //   - a Picardy 3rd marker on the final I chord at bar 15.
  // The pre-bar-12 chord vocabulary is untouched so existing Phase7
  // counterpoint behavior carries forward; only the last 4 bars host
  // the P8 idioms. Bars 12-15 sit entirely outside the V2
  // SubjectCarrier window (bars 8-11) so the chromatic chord tones
  // (F# from V/V, Ab from borrowed iv) do not clash with Material
  // pitches.
  if (spec.with_modulation) {
    ModulationEvent mod;
    mod.tick = static_cast<Tick>(8) * kTicksPerBar;
    mod.from_tonic_pc = 0;
    mod.from_is_minor = false;
    mod.to_tonic_pc = 7;
    mod.to_is_minor = false;
    mod.type = ModulationType::Pivot;
    out.harmony.modulations.push_back(mod);
    for (auto& chord : out.harmony.chords) {
      const int b = static_cast<int>(chord.start_tick / kTicksPerBar);
      if (b == 12) {
        // V/V — D-major secondary dominant of V (G major). secondary_of
        // is the home-key degree being tonicized.
        chord.root_pc = 2;
        chord.quality = ChordQuality::Major;
        chord.degree = RomanNumeral::V;
        chord.function = HarmonicFunction::Pred;
        chord.has_degree = true;
        chord.has_secondary_of = true;
        chord.secondary_of = RomanNumeral::V;
        chord.is_borrowed = false;
        chord.is_picardy = false;
        chord.inversion = ChordInversion::Root;
      } else if (b == 13) {
        // V — G-major resolves the secondary dominant.
        chord.root_pc = 7;
        chord.quality = ChordQuality::Major;
        chord.degree = RomanNumeral::V;
        chord.function = HarmonicFunction::D;
        chord.has_degree = true;
        chord.has_secondary_of = false;
        chord.is_borrowed = false;
        chord.is_picardy = false;
        chord.inversion = ChordInversion::Root;
      } else if (b == 14) {
        // Borrowed iv — F-minor loan from C parallel-minor.
        chord.root_pc = 5;
        chord.quality = ChordQuality::Minor;
        chord.degree = RomanNumeral::IV;
        chord.function = HarmonicFunction::S;
        chord.has_degree = true;
        chord.has_secondary_of = false;
        chord.is_borrowed = true;
        chord.is_picardy = false;
        chord.inversion = ChordInversion::Root;
      } else if (b == 15) {
        // Picardy 3rd — final I (C major). is_picardy=true lets the
        // PicardyThird bit fire on any voice landing on the major
        // third (E natural, pc=4).
        chord.root_pc = 0;
        chord.quality = ChordQuality::Major;
        chord.degree = RomanNumeral::I;
        chord.function = HarmonicFunction::T;
        chord.has_degree = true;
        chord.has_secondary_of = false;
        chord.is_borrowed = false;
        chord.is_picardy = true;
        chord.inversion = ChordInversion::Root;
      }
    }
  }

  // VoicePlan.
  out.voice_plan.num_voices = spec.voices;
  SpanId next_id = 0;
  Span subject_span;
  subject_span.id = next_id++;
  subject_span.start_tick = 0;
  subject_span.end_tick = static_cast<Tick>(subject_bars) * kTicksPerBar;
  subject_span.voice = 0;
  subject_span.intent = VoiceIntent::SubjectCarrier;
  out.voice_plan.spans.push_back(subject_span);

  if (spec.with_rhythm) {
    // Phase12 fixed layout (28 bars). Exposition bars 0-11 mirror Phase11.
    // The rhythm section (bars 12-27) is entirely Material: one V0
    // RhythmCarrier per 4-bar phrase (each replays whichever rhythm
    // fragments fall in its window) plus a V2 rhythmic-motif recurrence at
    // bars 16-19. V1 rests after the exposition. Register keeps V0 above
    // V2 at the only shared window (bars 16-19).
    //   V0: subject 0-3 | counterline 4-11 | rhythm phrases 12-15 / 16-19 /
    //       20-23 / 24-27
    //   V1: counterline 0-3 | answer 4-7 | counterline 8-11 | (rest)
    //   V2: (rest) | subject 8-11 | (rest) | recurrence 16-19 | (rest)
    auto pushSpan = [&](VoiceId voice, int first_bar, int last_bar, VoiceIntent intent) {
      Span s;
      s.id = next_id++;
      s.start_tick = static_cast<Tick>(first_bar) * kTicksPerBar;
      s.end_tick = static_cast<Tick>(last_bar + 1) * kTicksPerBar;
      s.voice = voice;
      s.intent = intent;
      s.subdivision = subdivision;
      out.voice_plan.spans.push_back(s);
    };
    for (int b = 4; b <= 11; ++b)
      pushCounterlineBar(out.voice_plan, next_id, 0, b, subdivision);
    pushSpan(0, 12, 15, VoiceIntent::RhythmCarrier);
    pushSpan(0, 16, 19, VoiceIntent::RhythmCarrier);
    pushSpan(0, 20, 23, VoiceIntent::RhythmCarrier);
    pushSpan(0, 24, 27, VoiceIntent::RhythmCarrier);
    for (int b = 0; b <= 3; ++b)
      pushCounterlineBar(out.voice_plan, next_id, 1, b, subdivision);
    pushSpan(1, 4, 7, VoiceIntent::AnswerCarrier);
    for (int b = 8; b <= 11; ++b)
      pushCounterlineBar(out.voice_plan, next_id, 1, b, subdivision);
    pushSpan(2, 8, 11, VoiceIntent::SubjectCarrier);
    pushSpan(2, 16, 19, VoiceIntent::RhythmCarrier);
  } else if (spec.with_development) {
    // Phase11 fixed layout (28 bars). The subject_span (V0 bars 0-3) is
    // already pushed above. Exposition bars 0-11 mirror Phase8 (V0 subject
    // + free counterline, V1 free counterline + answer, V2 subject
    // re-entry). The development bars 12-27 is entirely Material: each
    // development carrier sits directly after that voice's previous
    // Material (or is the voice's final span) so no Compose note is ever
    // immediately followed by a Material note in the same voice — this
    // dodges the unprepared_dissonance boundary failure mode (the rule
    // checks a Compose note's next same-voice note). Register layout
    // keeps V0 highest at every shared tick (no voice crossing):
    //   V0: subject 0-3 | counterline 4-11 | middle entry 12-15 (G/V) |
    //       inverted variant 16-19 | subject leader 20-23 | coda 24-27
    //   V1: counterline 0-3 | answer 4-7 | counterline 8-11 | (rest)
    //   V2: (rest) | subject 8-11 | dominant pedal 12-15 | (rest) |
    //       stretto follower 22-25 | (rest)
    auto pushSpan = [&](VoiceId voice, int first_bar, int last_bar, VoiceIntent intent) {
      Span s;
      s.id = next_id++;
      s.start_tick = static_cast<Tick>(first_bar) * kTicksPerBar;
      s.end_tick = static_cast<Tick>(last_bar + 1) * kTicksPerBar;
      s.voice = voice;
      s.intent = intent;
      s.subdivision = subdivision;
      out.voice_plan.spans.push_back(s);
    };
    // V0 free counterline bars 4-11.
    for (int b = 4; b <= 11; ++b)
      pushCounterlineBar(out.voice_plan, next_id, 0, b, subdivision);
    pushSpan(0, 12, 15, VoiceIntent::MiddleEntryCarrier);
    pushSpan(0, 16, 19, VoiceIntent::SubjectCarrierDiminished);
    pushSpan(0, 20, 23, VoiceIntent::SubjectCarrier);
    pushSpan(0, 24, 27, VoiceIntent::CodaCarrier);
    // V1 counterline 0-3, answer 4-7, counterline 8-11, then rests.
    for (int b = 0; b <= 3; ++b)
      pushCounterlineBar(out.voice_plan, next_id, 1, b, subdivision);
    pushSpan(1, 4, 7, VoiceIntent::AnswerCarrier);
    for (int b = 8; b <= 11; ++b)
      pushCounterlineBar(out.voice_plan, next_id, 1, b, subdivision);
    // V2 subject re-entry 8-11, dominant pedal 12-15, stretto follower 22-25.
    pushSpan(2, 8, 11, VoiceIntent::SubjectCarrier);
    pushSpan(2, 12, 15, VoiceIntent::PedalCarrier);
    pushSpan(2, 22, 25, VoiceIntent::StrettoCarrier);
  } else if (spec.with_third_entry) {
    // Phase6Episode replaces V0 counterline bars [bars - subject_bars, bars)
    // with one Episode span (Original transform of the V0 subject, re-anchored
    // at that bar). Phase6Tonal replaces V0 counterline bars [subject_bars,
    // 2*subject_bars) with one CountersubjectCarrier span that runs against
    // the V1 AnswerCarrier (tonal_answer). Phase9 replaces V0 counterline
    // bars [subject_bars, 2*subject_bars) (the V1 AnswerCarrier window) with
    // one FortspinnungSpan carrying a 2-step ascending sequence. Placing
    // the fortspinnung directly after V0 SubjectCarrier (Material→Material)
    // avoids the Compose→Material boundary issue where the composer cannot
    // see the carrier's first pitch in its lookahead, and keeps V0 still
    // active against the AnswerCarrier in V1. Phase6 keeps all V0
    // counterline bars contiguous.
    const int episode_first_bar = spec.with_episode ? (spec.bars - subject_bars) : -1;
    const int cs_first_bar = spec.with_tonal_answer ? subject_bars : -1;
    const int cs_last_bar = spec.with_tonal_answer ? (2 * subject_bars - 1) : -1;
    const int fs_first_bar = spec.with_fortspinnung ? subject_bars : -1;
    const int fs_last_bar = spec.with_fortspinnung ? (2 * subject_bars - 1) : -1;
    for (int b = subject_bars; b < spec.bars; ++b) {
      if (spec.with_episode && b >= episode_first_bar)
        continue;
      if (spec.with_tonal_answer && b >= cs_first_bar && b <= cs_last_bar)
        continue;
      if (spec.with_fortspinnung && b >= fs_first_bar && b <= fs_last_bar)
        continue;
      pushCounterlineBar(out.voice_plan, next_id, 0, b, subdivision);
    }
    if (spec.with_episode) {
      Span ep;
      ep.id = next_id++;
      ep.start_tick = static_cast<Tick>(episode_first_bar) * kTicksPerBar;
      ep.end_tick = static_cast<Tick>(spec.bars) * kTicksPerBar;
      ep.voice = 0;
      ep.intent = VoiceIntent::Episode;
      ep.subdivision = subdivision;
      out.voice_plan.spans.push_back(ep);
    }
    if (spec.with_tonal_answer) {
      Span cs;
      cs.id = next_id++;
      cs.start_tick = static_cast<Tick>(cs_first_bar) * kTicksPerBar;
      cs.end_tick = static_cast<Tick>(cs_last_bar + 1) * kTicksPerBar;
      cs.voice = 0;
      cs.intent = VoiceIntent::CountersubjectCarrier;
      cs.subdivision = subdivision;
      out.voice_plan.spans.push_back(cs);
    }
    if (spec.with_fortspinnung) {
      Span fs;
      fs.id = next_id++;
      fs.start_tick = static_cast<Tick>(fs_first_bar) * kTicksPerBar;
      fs.end_tick = static_cast<Tick>(fs_last_bar + 1) * kTicksPerBar;
      fs.voice = 0;
      fs.intent = VoiceIntent::FortspinnungSpan;
      fs.subdivision = subdivision;
      out.voice_plan.spans.push_back(fs);
    }
    for (int b = 0; b < subject_bars; ++b) {
      pushCounterlineBar(out.voice_plan, next_id, 1, b, subdivision);
    }
    Span answer_span;
    answer_span.id = next_id++;
    answer_span.start_tick = static_cast<Tick>(subject_bars) * kTicksPerBar;
    answer_span.end_tick = static_cast<Tick>(2 * subject_bars) * kTicksPerBar;
    answer_span.voice = 1;
    answer_span.intent = VoiceIntent::AnswerCarrier;
    out.voice_plan.spans.push_back(answer_span);
    for (int b = 2 * subject_bars; b < spec.bars; ++b) {
      pushCounterlineBar(out.voice_plan, next_id, 1, b, subdivision);
    }
    Span v2_subject;
    v2_subject.id = next_id++;
    v2_subject.start_tick = static_cast<Tick>(2 * subject_bars) * kTicksPerBar;
    v2_subject.end_tick = static_cast<Tick>(3 * subject_bars) * kTicksPerBar;
    v2_subject.voice = 2;
    v2_subject.intent = VoiceIntent::SubjectCarrier;
    out.voice_plan.spans.push_back(v2_subject);
    for (int b = 3 * subject_bars; b < spec.bars; ++b) {
      pushCounterlineBar(out.voice_plan, next_id, 2, b, subdivision);
    }
  } else if (spec.with_answer) {
    // Phase4Sus carves a 2-bar SuspensionCarrier span out of V0
    // counterline bars [subject_bars, subject_bars + 2). The remaining
    // V0 counterline bars run normally on either side. Phase4 (no
    // suspension) keeps all V0 counterline bars contiguous.
    const int sus_first_bar = spec.with_suspension ? subject_bars : -1;
    const int sus_last_bar = spec.with_suspension ? subject_bars + 1 : -1;
    for (int b = subject_bars; b < spec.bars; ++b) {
      if (spec.with_suspension && b >= sus_first_bar && b <= sus_last_bar)
        continue;
      pushCounterlineBar(out.voice_plan, next_id, 0, b, subdivision);
    }
    if (spec.with_suspension) {
      Span sus_span;
      sus_span.id = next_id++;
      sus_span.start_tick = static_cast<Tick>(sus_first_bar) * kTicksPerBar;
      sus_span.end_tick = static_cast<Tick>(sus_last_bar + 1) * kTicksPerBar;
      sus_span.voice = 0;
      sus_span.intent = VoiceIntent::SuspensionCarrier;
      out.voice_plan.spans.push_back(sus_span);
    }
    for (int b = 0; b < subject_bars; ++b) {
      pushCounterlineBar(out.voice_plan, next_id, 1, b, subdivision);
    }
    Span answer_span;
    answer_span.id = next_id++;
    answer_span.start_tick = static_cast<Tick>(subject_bars) * kTicksPerBar;
    answer_span.end_tick = static_cast<Tick>(spec.bars) * kTicksPerBar;
    answer_span.voice = 1;
    answer_span.intent = VoiceIntent::AnswerCarrier;
    out.voice_plan.spans.push_back(answer_span);
  } else {
    for (std::uint8_t v = 1; v < spec.voices; ++v) {
      for (int b = 0; b < spec.bars; ++b) {
        pushCounterlineBar(out.voice_plan, next_id, v, b, subdivision);
      }
    }
  }

  annotateLeadingToneMarkers(out.material, out.harmony.tonic_pc, out.harmony.is_minor);
  const Tick subject_cadence_tick = static_cast<Tick>(subject_bars) * kTicksPerBar - kTicksPerBeat;
  for (const auto& marker : out.material.leading_tone_markers) {
    if (marker.fragment != MaterialFragment::Subject)
      continue;
    if (marker.resolution_tick != subject_cadence_tick)
      continue;
    CadenceEvent cadence;
    cadence.tick = marker.resolution_tick;
    cadence.type = CadenceType::Perfect;
    out.harmony.cadences.push_back(cadence);
    if (marker.leading_tick >= kTicksPerBeat) {
      CadentialSixFour six_four;
      six_four.tick = marker.leading_tick - kTicksPerBeat;
      six_four.resolution_tick = marker.leading_tick;
      out.harmony.cadential_six_fours.push_back(six_four);
    }
  }
  annotateCadenceCells(out.material, out.harmony);

  if (spec.with_tonal_answer) {
    // Phase6Tonal: derive tonal_answer from the V0 subject (first 16 notes)
    // with a 4-note head mutation, anchor at bar `subject_bars`, and set
    // the dispatch flag so AnswerCarrier reads from tonal_answer instead
    // of `answer`. Bach's tonal-answer convention maps the subject's
    // tonic-degree head pitches to the dominant and vice versa.
    std::vector<MaterialNote> subj_head(out.material.subject.begin(),
                                        out.material.subject.begin() + 16);
    out.material.tonal_answer = tonal_answer::deriveTonalAnswer(
        subj_head, out.harmony.tonic_pc, static_cast<Tick>(subject_bars) * kTicksPerBar,
        /*head_length=*/4);
    out.material.use_tonal_answer = true;
    // Phase6Tonal CS material: stationary G5 (pitch 79) for 16 quarter
    // notes so V0 has a sounding note at every beat of the answer
    // window. The Validator's vertical/parallel rules skip both-Material
    // pairs (V0 CS vs V1 tonal_answer are both Material), so a pedal
    // pitch is safe regardless of the seed's tonal_answer head.
    for (int n = 0; n < 16; ++n) {
      MaterialNote cs;
      const int bar_in_block = n / 4;
      const int beat_in_bar = n % 4;
      cs.start_tick = static_cast<Tick>(subject_bars + bar_in_block) * kTicksPerBar +
                      static_cast<Tick>(beat_in_bar) * kTicksPerBeat;
      cs.duration = kTicksPerBeat;
      cs.pitch = 79;
      out.material.countersubject.push_back(cs);
    }
  }

  if (spec.with_episode) {
    // Phase6Episode injects one Episode fragment in V0 covering bars
    // [bars - subject_bars, bars). Transform = Original; source = the
    // first `subject_bars` of V0 SubjectCarrier material (indices
    // [0, 16)). Result re-anchors the subject pitches at the target
    // bar so the V0 line restates the subject in the closing bars —
    // a textbook Bach "subject-reentry-as-episode" recap.
    //
    // EpisodeMotifSourced bit on the emitted notes lets the closure
    // gate (4) confirm Episode derivation actually fired.
    EpisodeFragment ef;
    ef.transform = static_cast<std::uint8_t>(motif_ops::EpisodeMotifTransform::Original);
    ef.source_start_index = 0;
    ef.source_count = 16;  // first 4 bars × 4 beats
    ef.voice = 0;
    ef.target_start_tick = static_cast<Tick>(spec.bars - subject_bars) * kTicksPerBar;
    ef.invert_pivot = 72;
    ef.augment_factor = 2;
    ef.diminish_factor = 2;
    out.material.episodes.push_back(ef);
  }

  if (spec.with_suspension) {
    // One deterministic Sus7_6 in V0 spanning bars [subject_bars,
    // subject_bars + 1). Prep tied across the bar line so the
    // suspension lands on the bar-5 downbeat (strong beat = required
    // by the validator's isStrongBeat semantics). The prep/sus pitch
    // B5 (83) is pc 11, which lies in the consonant intersection of
    // every kSubjectPatterns' answer-V1 column at this tick (the
    // intersection of consonant pcs against V1 pitches across the
    // 5 patterns reduces to {pc 2, pc 11}; B5 is the higher of the two
    // and keeps V0 safely above V1's catalog maximum of 79). Step-down
    // resolution to A5 (81) on beat 2.
    SuspensionPattern sp;
    sp.type = SuspensionType::Sus7_6;
    sp.preparation_tick = static_cast<Tick>(subject_bars) * kTicksPerBar;
    sp.suspension_tick = static_cast<Tick>(subject_bars + 1) * kTicksPerBar;
    sp.resolution_tick = sp.suspension_tick + kTicksPerBeat;
    sp.preparation_pitch = 83;
    sp.suspension_pitch = 83;
    sp.resolution_pitch = 81;
    sp.voice = 0;
    out.material.suspension_patterns.push_back(sp);
  }

  if (spec.with_fortspinnung) {
    // Phase9 SequenceTemplate. Seed = 8-note motif over 2 bars (bars
    // 4-5) in V0. Pattern = AscendingStep (+2 semis per step).
    //
    // Pcs restricted to {0, 2, 7} (C, D, G) so the +2 transpose lands
    // inside C-major diatonic at step 1 (pcs {2, 4, 9}). A further
    // step would produce pc 6 (F#) → cross-relation, hence num_steps
    // is capped at 2. The 2-step pattern fills the full 4-bar V0
    // span (bars 4-7).
    //
    // Excluding pc 11 (B = leading tone in C major) from both step 0
    // and step 1 prevents `doubling_no_leading_tone` clashes with V1
    // AnswerCarrier idx 8 (= subject pattern idx 8 - P4), which for
    // catalog patterns 0 and 3 lands on B (pc 11) — and on harm
    // pattern (harm_a + 1) % 4 = 0, bar 6 chord = V which OWNS the
    // leading tone.
    //
    // Register: V0 must stay above V1 AnswerCarrier across all 5
    // subject patterns. V1 AnswerCarrier = subject pattern - 5
    // semitones; its max value at bars 4-7 is 79 (patterns 1 and 2
    // climb to 84 in idx 3 or idx 8 → 79 after -P4). Seed min = 79
    // (= unison with V1 max for pattern 1 idx 0 = 79); step 1 min =
    // 81. Unisons are not voice_crossing (interval ≥ 0).
    //
    // FortspinnungSpan placement = bars 4-7 sits directly after V0
    // SubjectCarrier (bars 0-3). Both spans are Material, so the
    // SubjectCarrier→FortspinnungSpan boundary has no Compose
    // mediation. The pitch jump 72 → 81 (subject_last → seed[0])
    // is a M6 leap inside Material, which the validator does not
    // analyze for melodic intervals (Material is verbatim).
    SequenceTemplate tmpl;
    tmpl.pattern = SequencePattern::AscendingStep;
    tmpl.target_start_tick = static_cast<Tick>(subject_bars) * kTicksPerBar;
    tmpl.step_length_ticks = 2 * kTicksPerBar;
    tmpl.num_steps = 2;
    tmpl.voice = 0;
    // Seed: G5 C6 D6 C6 G5 C6 D6 C6 — two-bar arpeggiated triad-tone
    // motif on the G-C-D pivot. Step 1 (+2): A5 D6 E6 D6 A5 D6 E6 D6.
    tmpl.seed_pitches = {79, 84, 86, 84, 79, 84, 86, 84};
    tmpl.seed_durations = {kTicksPerBeat, kTicksPerBeat, kTicksPerBeat, kTicksPerBeat,
                           kTicksPerBeat, kTicksPerBeat, kTicksPerBeat, kTicksPerBeat};
    out.material.sequence_templates.push_back(tmpl);
  }

  if (spec.with_imitation_entry) {
    // Phase9 ImitationEntry. Subject (V0) enters at bar 0; real answer
    // (V1) enters at bar `subject_bars` (= 4) with interval -5 semis
    // (real answer = P5 down = subject - 5). This matches the existing
    // Phase4+ harness convention; the declaration is purely documentary
    // so the Validator's imitation_entry_match rule fires the
    // ImitationEntryMatched bit on the entry note of both fragments.
    ImitationEntry entry;
    entry.leader_fragment = MaterialFragment::Subject;
    entry.follower_fragment = MaterialFragment::Answer;
    entry.distance_ticks = static_cast<Tick>(subject_bars) * kTicksPerBar;
    entry.interval_semis = -5;
    out.material.imitation_entries.push_back(entry);
  }

  if (spec.with_development) {
    // Phase11 development material (bars 12-27). Every fragment is the
    // seed's V0 subject pattern (kSubjectPatterns[subj_a]) under a fixed
    // pitch transform, so each device tracks the seed's exposition
    // subject. Anchored 4-bar (16 quarter-note) fragments; registers are
    // chosen so V0 stays above V2 at every shared tick (see voice plan).
    const auto& pat = kSubjectPatterns[subj_a];
    auto buildFragment = [&](int base_bar, auto transform) {
      std::vector<MaterialNote> v;
      v.reserve(16);
      for (int n = 0; n < 16; ++n) {
        MaterialNote mn;
        const int bar_in_block = n / 4;
        const int beat_in_bar = n % 4;
        mn.start_tick = static_cast<Tick>(base_bar + bar_in_block) * kTicksPerBar +
                        static_cast<Tick>(beat_in_bar) * kTicksPerBeat;
        mn.duration = kTicksPerBeat;
        mn.pitch = static_cast<std::uint8_t>(transform(static_cast<int>(pat[n])));
        v.push_back(mn);
      }
      return v;
    };

    // Middle entry: subject down a perfect fourth (-5) → the dominant key
    // (C major - P4 = G major). related_key_pc = 7 (= V of C). Range
    // 66-79 stays above the dominant pedal (G3 = 55). Bars 12-15, V0.
    MiddleEntryDecl middle;
    middle.voice = 0;
    middle.related_key_pc = 7;
    middle.notes = buildFragment(12, [](int p) { return p - 5; });
    out.material.middle_entries.push_back(middle);

    // Dominant pedal point: a single G3 (pc 7) held across bars 12-15 in
    // V2 (the bottom voice), under the middle entry.
    PedalPointDecl pedal;
    pedal.voice = 2;
    pedal.start_tick = static_cast<Tick>(12) * kTicksPerBar;
    pedal.duration = static_cast<Tick>(4) * kTicksPerBar;
    pedal.pitch = 55;
    pedal.is_dominant = true;
    out.material.pedal_points.push_back(pedal);

    // Subject variant: diminution — the subject at half duration (eighth
    // notes), so its 16 notes span 2 bars; played twice to fill bars
    // 16-19. Diminution preserves the subject's pitch sequence (and
    // register, 71-84), so it adds no awkward leaps the way inversion
    // would, and it connects smoothly to the verbatim subject leader at
    // bars 20-23. V0 is the sole sounding voice in this window.
    SubjectVariantDecl variant;
    variant.voice = 0;
    variant.transform = static_cast<std::uint8_t>(motif_ops::EpisodeMotifTransform::Diminish);
    for (int rep = 0; rep < 2; ++rep) {
      const Tick rep_start = static_cast<Tick>(16 + 2 * rep) * kTicksPerBar;
      for (int n = 0; n < 16; ++n) {
        MaterialNote mn;
        mn.start_tick = rep_start + static_cast<Tick>(n) * (kTicksPerBeat / 2);
        mn.duration = kTicksPerBeat / 2;
        mn.pitch = pat[n];
        variant.notes.push_back(mn);
      }
    }
    out.material.subject_variants.push_back(variant);

    // Stretto leader: the subject restated verbatim in V0 at bars 20-23.
    // Appended to material.subject so the V0 SubjectCarrier span at those
    // bars replays it (added after leading-tone / cadence annotation so it
    // gets no spurious cadence markers).
    {
      const auto leader = buildFragment(20, [](int p) { return p; });
      for (const auto& mn : leader)
        out.material.subject.push_back(mn);
    }

    // Stretto follower: subject down two octaves (-24). A 12-multiple
    // transpose preserves the subject's C-major pitch classes, so no Bb
    // is introduced to clash with the leader's B-natural (a -19 "twelfth"
    // would land the follower in F major and trip cross_relation). Range
    // 47-60 stays a clear margin below the leader (71-84), so the bars
    // 22-23 overlap never crosses voices for any subject pattern (the
    // worst early-high / late-low gap across the catalog is 13 < 24).
    // V2, bars 22-25; enters at bar 22 (strictly inside leader bars
    // 20-23).
    StrettoDecl stretto;
    stretto.leader_voice = 0;
    stretto.follower_voice = 2;
    stretto.leader_entry_tick = static_cast<Tick>(20) * kTicksPerBar;
    stretto.leader_length_ticks = static_cast<Tick>(4) * kTicksPerBar;
    stretto.follower_entry_tick = static_cast<Tick>(22) * kTicksPerBar;
    stretto.interval_semis = -24;
    stretto.follower_notes = buildFragment(22, [](int p) { return p - 24; });
    out.material.stretto_entries.push_back(stretto);

    // Coda: a stepwise C-major closing line (range 71-79) settling onto
    // the tonic, bars 24-27 in V0, above the stretto follower's tail.
    // Stepwise motion (no leap > 2 semitones) keeps the model's
    // unresolved-large-leap penalty off the closing phrase.
    CodaDecl coda;
    coda.voice = 0;
    static constexpr std::array<std::uint8_t, 16> kCoda = {79, 77, 76, 74, 72, 74, 76, 77,
                                                           76, 74, 72, 71, 72, 74, 72, 72};
    for (int n = 0; n < 16; ++n) {
      MaterialNote mn;
      const int bar_in_block = n / 4;
      const int beat_in_bar = n % 4;
      mn.start_tick = static_cast<Tick>(24 + bar_in_block) * kTicksPerBar +
                      static_cast<Tick>(beat_in_bar) * kTicksPerBeat;
      mn.duration = kTicksPerBeat;
      mn.pitch = kCoda[n];
      coda.notes.push_back(mn);
    }
    out.material.coda_extensions.push_back(coda);
  }

  if (spec.with_rhythm) {
    // Phase12 rhythm material (bars 12-27). Seed-independent, C-major,
    // register-safe (V0 stays above the V2 recurrence at bars 16-19). The
    // phrase grid is a regular 4-bar period (downbeats every 4 bars), with
    // a quarter-note anacrusis leading into bar 16.
    PhraseStructure& ps = out.material.phrase_structure;
    ps.has_anacrusis = true;
    ps.anacrusis_ticks = kTicksPerBeat;  // quarter-note upbeat
    for (int bar = 0; bar <= 24; bar += 4)
      ps.phrase_start_ticks.push_back(static_cast<Tick>(bar) * kTicksPerBar);

    auto addNote = [](std::vector<MaterialNote>& v, Tick t, Tick d, std::uint8_t p) {
      MaterialNote mn;
      mn.start_tick = t;
      mn.duration = d;
      mn.pitch = p;
      v.push_back(mn);
    };
    const Tick d8 = kTicksPerBeat / 2;   // eighth
    const Tick dq = kTicksPerBeat;       // quarter
    const Tick dd = kTicksPerBeat + d8;  // dotted quarter (720)
    const Tick dh = 2 * kTicksPerBeat;   // half
    const Tick ddh = 3 * kTicksPerBeat;  // dotted half (1440)
    auto bar = [](int b) { return static_cast<Tick>(b) * kTicksPerBar; };

    // Dotted figure (V0, bars 12-15): dotted-quarter + eighth + two
    // quarters per bar; bar 15 stops a beat early to leave room for the
    // anacrusis. First note is a phrase downbeat (PhrasePeriodicityKept).
    RhythmFragment dotted;
    dotted.feature = RhythmFragment::Feature::Dotted;
    dotted.voice = 0;
    {
      const std::array<std::uint8_t, 15> p = {72, 74, 76, 77, 79, 77, 76, 74,
                                              76, 77, 79, 77, 76, 74, 72};
      const std::array<Tick, 15> d = {dd, d8, dq, dq, dd, d8, dq, dq, dd, d8, dq, dq, dd, d8, dq};
      Tick t = bar(12);
      for (std::size_t i = 0; i < p.size(); ++i) {
        addNote(dotted.notes, t, d[i], p[i]);
        t += d[i];
      }
    }
    out.material.rhythm_fragments.push_back(dotted);

    // Anacrusis (V0): a quarter-note pickup (B4 = leading tone) on bar 15
    // beat 4, resolving up into the bar-16 downbeat. Starts exactly
    // anacrusis_ticks before the bar-16 phrase start.
    RhythmFragment anac;
    anac.feature = RhythmFragment::Feature::Anacrusis;
    anac.voice = 0;
    addNote(anac.notes, bar(16) - dq, dq, 71);
    out.material.rhythm_fragments.push_back(anac);

    // Syncopation (V0, bars 16-19, consequent phrase): off-beat onsets
    // (eighth, quarter, quarter, quarter, eighth per bar = onsets on the
    // 1.5 / 2.5 / 3.5 beats). First note is a phrase downbeat.
    RhythmFragment sync;
    sync.feature = RhythmFragment::Feature::Syncopation;
    sync.voice = 0;
    {
      const std::array<std::array<std::uint8_t, 5>, 4> rows = {
          {{72, 74, 76, 74, 72}, {74, 76, 77, 76, 74}, {76, 77, 79, 77, 76}, {74, 76, 74, 72, 71}}};
      const std::array<Tick, 5> d = {d8, dq, dq, dq, d8};
      for (int b = 0; b < 4; ++b) {
        Tick t = bar(16 + b);
        for (int i = 0; i < 5; ++i) {
          addNote(sync.notes, t, d[i],
                  rows[static_cast<std::size_t>(b)][static_cast<std::size_t>(i)]);
          t += d[i];
        }
      }
    }
    out.material.rhythm_fragments.push_back(sync);

    // Antecedent of the 20-23 phrase (V0, bars 20-21): plain quarters; the
    // first note is the bar-20 phrase downbeat (PhrasePeriodicityKept).
    RhythmFragment phrase20;
    phrase20.feature = RhythmFragment::Feature::Dotted;
    phrase20.voice = 0;
    {
      const std::array<std::uint8_t, 8> p = {72, 74, 76, 77, 76, 74, 72, 74};
      Tick t = bar(20);
      for (auto pitch : p) {
        addNote(phrase20.notes, t, dq, pitch);
        t += dq;
      }
    }
    out.material.rhythm_fragments.push_back(phrase20);

    // Hemiola (V0, bars 22-23, cadence approach): two dotted-half notes
    // plus a half note (3 + 3 + 2 beats) cut across the 4+4 barline grid,
    // a 3-against-2 regrouping. All notes carry HemiolaInserted.
    RhythmFragment hemiola;
    hemiola.feature = RhythmFragment::Feature::Hemiola;
    hemiola.voice = 0;
    {
      Tick t = bar(22);
      addNote(hemiola.notes, t, ddh, 76);
      t += ddh;
      addNote(hemiola.notes, t, ddh, 74);
      t += ddh;
      addNote(hemiola.notes, t, dh, 72);
    }
    out.material.rhythm_fragments.push_back(hemiola);

    // Closing phrase (V0, bars 24-27): stepwise descent broadening to the
    // final tonic. First note is the bar-24 phrase downbeat.
    RhythmFragment closing;
    closing.feature = RhythmFragment::Feature::Dotted;
    closing.voice = 0;
    {
      Tick t = bar(24);
      const std::array<std::uint8_t, 8> q = {77, 76, 74, 72, 74, 72, 71, 72};
      for (auto pitch : q) {
        addNote(closing.notes, t, dq, pitch);
        t += dq;
      }
      addNote(closing.notes, bar(26), dh, 71);
      addNote(closing.notes, bar(26) + dh, dh, 72);
      addNote(closing.notes, bar(27), 2 * dh, 72);  // whole-note final tonic
    }
    out.material.rhythm_fragments.push_back(closing);

    // Rhythmic-motif recurrence (V2, bars 16-19): the dotted figure's
    // rhythm restated an octave-and-a-bit lower (range 60-67), under the
    // syncopated consequent. RhythmicMotifRecurrence bit.
    RhythmFragment recur;
    recur.feature = RhythmFragment::Feature::Recurrence;
    recur.voice = 2;
    {
      const std::array<std::uint8_t, 15> p = {60, 62, 64, 65, 67, 65, 64, 62,
                                              64, 65, 67, 65, 64, 62, 60};
      const std::array<Tick, 15> d = {dd, d8, dq, dq, dd, d8, dq, dq, dd, d8, dq, dq, dd, d8, dq};
      Tick t = bar(16);
      for (std::size_t i = 0; i < p.size(); ++i) {
        addNote(recur.notes, t, d[i], p[i]);
        t += d[i];
      }
    }
    out.material.rhythm_fragments.push_back(recur);
  }

  return out;
}

}  // namespace bach::composer
