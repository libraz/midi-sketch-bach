#include "composer/harness_fixture.h"

#include <array>
#include <cstdint>

#include "composer/span.h"
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
    {72, 74, 76, 77, 79, 81, 79, 77, 76, 74, 76, 77, 79, 77, 76, 72},
    // 1: descent then ascent (start high)
    {84, 83, 81, 79, 77, 76, 77, 79, 81, 79, 77, 76, 74, 76, 77, 79},
    // 2: broken triad outline
    {79, 76, 79, 84, 76, 79, 76, 72, 74, 77, 74, 71, 72, 76, 79, 84},
    // 3: stepwise sequence
    {72, 74, 76, 77, 74, 76, 77, 79, 76, 77, 79, 81, 77, 79, 81, 79},
    // 4: upper-arch
    {76, 77, 79, 81, 79, 77, 76, 74, 72, 74, 76, 77, 79, 77, 76, 72},
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
      return {phase, /*voices=*/2, /*bars=*/8, /*subject_bars=*/8, false, false};
    case HarnessPhase::Phase35:
      return {phase, /*voices=*/2, /*bars=*/4, /*subject_bars=*/4, false, false};
    case HarnessPhase::Phase4:
      return {phase, /*voices=*/2, /*bars=*/8, /*subject_bars=*/4, true, false};
    case HarnessPhase::Phase5:
      return {phase, /*voices=*/3, /*bars=*/12, /*subject_bars=*/12, false, false};
    case HarnessPhase::Phase6:
      return {phase, /*voices=*/3, /*bars=*/16, /*subject_bars=*/4, true, true};
  }
  return {phase, 2, 8, 8, false, false};
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
      out.harmony.chords.push_back(c);
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

  if (spec.with_third_entry) {
    for (int b = subject_bars; b < spec.bars; ++b) {
      pushCounterlineBar(out.voice_plan, next_id, 0, b, subdivision);
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
    for (int b = subject_bars; b < spec.bars; ++b) {
      pushCounterlineBar(out.voice_plan, next_id, 0, b, subdivision);
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

  return out;
}

}  // namespace bach::composer
