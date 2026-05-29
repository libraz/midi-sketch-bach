#include "composer/validator.h"

#include <algorithm>
#include <array>
#include <cstdint>

namespace bach::composer {

namespace {

std::array<std::uint8_t, 3> triadFor(const ChordEvent& chord) {
  std::uint8_t third = (chord.quality == ChordQuality::Major) ? 4 : 3;
  std::uint8_t fifth = (chord.quality == ChordQuality::Diminished) ? 6 : 7;
  return {
      static_cast<std::uint8_t>(chord.root_pc % 12),
      static_cast<std::uint8_t>((chord.root_pc + third) % 12),
      static_cast<std::uint8_t>((chord.root_pc + fifth) % 12),
  };
}

const ChordEvent& activeChord(const HarmonicPlan& plan, Tick at) {
  const ChordEvent* current = &plan.chords.front();
  for (const auto& chord : plan.chords) {
    if (chord.start_tick <= at) {
      current = &chord;
    } else {
      break;
    }
  }
  return *current;
}

bool isStrongBeat(Tick tick) {
  return (tick % kTicksPerBar) == 0;
}

bool isPerfectInterval(int semitones) {
  int abs_semi = std::abs(semitones) % 12;
  return abs_semi == 0 || abs_semi == 7;
}

// Returns the pitch active in `voice` at `tick`, or 0 if none.
std::uint8_t voicePitchAt(const std::vector<NoteEvent>& notes, VoiceId voice, Tick tick) {
  std::uint8_t pitch = 0;
  for (const auto& note : notes) {
    if (note.voice != voice)
      continue;
    if (note.start_tick > tick)
      break;
    if (note.start_tick <= tick && tick < note.start_tick + note.duration) {
      pitch = note.pitch;
    }
  }
  return pitch;
}

}  // namespace

ValidationReport Validator::validate(const std::vector<NoteEvent>& notes,
                                     const std::vector<NoteProvenance>& provenance,
                                     const HarmonicPlan& harmonic_plan) const {
  ValidationReport report;

  // 1. Strong-beat dissonance — only check Compose-source notes. Material
  //    notes are inputs and not subject to candidate-search rules.
  for (std::size_t i = 0; i < notes.size(); ++i) {
    const auto& note = notes[i];
    if (i >= provenance.size())
      continue;
    if (provenance[i].source != NoteSource::Compose)
      continue;
    if (!isStrongBeat(note.start_tick))
      continue;

    const auto& chord = activeChord(harmonic_plan, note.start_tick);
    const auto triad = triadFor(chord);
    const std::uint8_t pc = static_cast<std::uint8_t>(note.pitch % 12);
    if (pc != triad[0] && pc != triad[1] && pc != triad[2]) {
      ValidationFailure failure;
      failure.span_id = provenance[i].span_id;
      failure.rule_id = "strong_beat_dissonance";
      report.failures.push_back(failure);
    }
  }

  // 2. Parallel perfect (5ths and 8ths) between any pair of voices.
  //    Iterate adjacent tick positions where any note starts. For each
  //    voice pair, compare the interval at this start to the interval at
  //    the previous start.
  //
  //    Implementation: collect distinct start ticks in order, then walk.
  std::vector<Tick> ticks;
  ticks.reserve(notes.size());
  for (const auto& note : notes)
    ticks.push_back(note.start_tick);
  std::sort(ticks.begin(), ticks.end());
  ticks.erase(std::unique(ticks.begin(), ticks.end()), ticks.end());

  // Identify all voices in the score.
  std::vector<VoiceId> voices;
  for (const auto& note : notes) {
    if (std::find(voices.begin(), voices.end(), note.voice) == voices.end()) {
      voices.push_back(note.voice);
    }
  }
  std::sort(voices.begin(), voices.end());

  for (std::size_t va = 0; va < voices.size(); ++va) {
    for (std::size_t vb = va + 1; vb < voices.size(); ++vb) {
      int prev_interval = INT32_MIN;
      std::uint8_t prev_pa = 0;
      std::uint8_t prev_pb = 0;
      for (Tick t : ticks) {
        std::uint8_t pa = voicePitchAt(notes, voices[va], t);
        std::uint8_t pb = voicePitchAt(notes, voices[vb], t);
        if (pa == 0 || pb == 0) {
          prev_interval = INT32_MIN;
          continue;
        }
        int interval = static_cast<int>(pa) - static_cast<int>(pb);

        // Voice crossing: by convention, lower voice index = higher
        // pitch (voice 0 = soprano). A negative interval means the
        // upper-indexed voice has risen above the lower-indexed voice.
        if (interval < 0) {
          SpanId fail_span = kInvalidSpanId;
          for (std::size_t k = 0; k < notes.size(); ++k) {
            if (notes[k].voice == voices[va] && notes[k].start_tick == t) {
              if (k < provenance.size())
                fail_span = provenance[k].span_id;
              break;
            }
          }
          ValidationFailure failure;
          failure.span_id = fail_span;
          failure.rule_id = "voice_crossing";
          report.failures.push_back(failure);
        }

        // Parallel perfect: both voices move (oblique and static motion
        // are allowed even on perfect intervals) AND the interval stays
        // identical AND non-zero.
        const bool both_moved = prev_pa != 0 && prev_pb != 0 && pa != prev_pa && pb != prev_pb;
        if (prev_interval != INT32_MIN && both_moved && isPerfectInterval(interval) &&
            isPerfectInterval(prev_interval) && interval == prev_interval && interval != 0) {
          // Find the span id of voices[vb]'s note starting at t (failing
          // span is the lower voice by convention).
          SpanId fail_span = kInvalidSpanId;
          for (std::size_t k = 0; k < notes.size(); ++k) {
            if (notes[k].voice == voices[vb] && notes[k].start_tick == t) {
              if (k < provenance.size())
                fail_span = provenance[k].span_id;
              break;
            }
          }
          ValidationFailure failure;
          failure.span_id = fail_span;
          failure.rule_id = (std::abs(interval) % 12 == 7) ? "parallel_fifth" : "parallel_octave";
          report.failures.push_back(failure);
        }
        prev_interval = interval;
        prev_pa = pa;
        prev_pb = pb;
      }
    }
  }

  // 5. Vertical dissonance on strong beats. Every voice pair that
  //    sounds together at a strong-beat tick must form a consonant
  //    interval. Consonant set (semitones mod 12): {0, 3, 4, 5, 7,
  //    8, 9}. The blame falls on the Compose-source side of the
  //    pair; if both notes are Material, the pair is skipped (the
  //    composer cannot fix fixed inputs).
  static constexpr auto isConsonantSemis = [](int semis) {
    const int pc = std::abs(semis) % 12;
    return pc == 0 || pc == 3 || pc == 4 || pc == 5 || pc == 7 || pc == 8 || pc == 9;
  };
  for (std::size_t va = 0; va < voices.size(); ++va) {
    for (std::size_t vb = va + 1; vb < voices.size(); ++vb) {
      for (Tick t : ticks) {
        if (!isStrongBeat(t))
          continue;
        const std::uint8_t pa = voicePitchAt(notes, voices[va], t);
        const std::uint8_t pb = voicePitchAt(notes, voices[vb], t);
        if (pa == 0 || pb == 0)
          continue;
        const int interval = static_cast<int>(pa) - static_cast<int>(pb);
        if (isConsonantSemis(interval))
          continue;

        // Find span_id: prefer the Compose-source side of the pair.
        SpanId compose_span = kInvalidSpanId;
        SpanId fallback_span = kInvalidSpanId;
        for (std::size_t k = 0; k < notes.size(); ++k) {
          if (notes[k].start_tick != t)
            continue;
          if (notes[k].voice != voices[va] && notes[k].voice != voices[vb]) {
            continue;
          }
          if (k >= provenance.size())
            continue;
          if (provenance[k].source == NoteSource::Compose) {
            compose_span = provenance[k].span_id;
            break;
          }
          if (fallback_span == kInvalidSpanId) {
            fallback_span = provenance[k].span_id;
          }
        }
        if (compose_span == kInvalidSpanId) {
          continue;  // both Material; nothing the composer can do.
        }
        ValidationFailure failure;
        failure.span_id = compose_span;
        failure.rule_id = "vertical_dissonance";
        report.failures.push_back(failure);
      }
    }
  }

  // 3. Consecutive leaps & 4. Weak-beat unprepared dissonance.
  //    Both rules require a per-voice walk in start_tick order, so
  //    the index gather is shared.
  for (VoiceId voice : voices) {
    std::vector<std::size_t> indices;
    for (std::size_t k = 0; k < notes.size(); ++k) {
      if (notes[k].voice == voice)
        indices.push_back(k);
    }
    std::sort(indices.begin(), indices.end(), [&](std::size_t a, std::size_t b) {
      return notes[a].start_tick < notes[b].start_tick;
    });

    // Rule 3: consecutive_leaps.
    for (std::size_t i = 2; i < indices.size(); ++i) {
      const int delta_pre = static_cast<int>(notes[indices[i - 1]].pitch) -
                            static_cast<int>(notes[indices[i - 2]].pitch);
      const int delta_cur =
          static_cast<int>(notes[indices[i]].pitch) - static_cast<int>(notes[indices[i - 1]].pitch);
      if (std::abs(delta_pre) >= 7 && std::abs(delta_cur) >= 7) {
        SpanId fail_span = kInvalidSpanId;
        if (indices[i] < provenance.size()) {
          fail_span = provenance[indices[i]].span_id;
        }
        ValidationFailure failure;
        failure.span_id = fail_span;
        failure.rule_id = "consecutive_leaps";
        report.failures.push_back(failure);
      }
    }

    // Rule 4: weak-beat non-chord-tone must be approached AND left by
    // step (<= 2 semis). Only Compose-source notes are subject to the
    // rule. Voice-boundary notes (no prev or no next) are exempt.
    for (std::size_t i = 1; i + 1 < indices.size(); ++i) {
      const std::size_t k = indices[i];
      if (k >= provenance.size())
        continue;
      if (provenance[k].source != NoteSource::Compose)
        continue;
      if (isStrongBeat(notes[k].start_tick))
        continue;  // covered by rule 1

      const auto& chord = activeChord(harmonic_plan, notes[k].start_tick);
      const auto triad = triadFor(chord);
      const std::uint8_t pc = static_cast<std::uint8_t>(notes[k].pitch % 12);
      const bool is_triad = (pc == triad[0]) || (pc == triad[1]) || (pc == triad[2]);
      if (is_triad)
        continue;  // chord tone, no constraint

      const int delta_prev =
          static_cast<int>(notes[k].pitch) - static_cast<int>(notes[indices[i - 1]].pitch);
      const int delta_next =
          static_cast<int>(notes[indices[i + 1]].pitch) - static_cast<int>(notes[k].pitch);
      if (std::abs(delta_prev) > 2 || std::abs(delta_next) > 2) {
        ValidationFailure failure;
        failure.span_id = provenance[k].span_id;
        failure.rule_id = "unprepared_dissonance";
        report.failures.push_back(failure);
      }
    }
  }

  if (!report.failures.empty()) {
    report.status = ValidationStatus::FailedSpan;
  }
  return report;
}

}  // namespace bach::composer
