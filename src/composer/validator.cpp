#include "composer/validator.h"

#include <algorithm>
#include <array>
#include <cstdint>

#include "composer/chord_voicing.h"
#include "composer/motif_ops.h"
#include "composer/rule_helpers.h"

namespace bach::composer {

namespace {

using rule_helpers::isCrossRelationPc;
using rule_helpers::isLeadingTone;
using rule_helpers::isPerfectInterval;
using rule_helpers::isStrongBeat;
using rule_helpers::pitchClass;
using rule_helpers::voicePitchAt;

std::array<std::uint8_t, 3> triadFor(const ChordEvent& chord) {
  std::uint8_t third = 4;
  std::uint8_t fifth = 7;
  switch (chord.quality) {
    case ChordQuality::Major:
    case ChordQuality::Major7:
    case ChordQuality::Dominant7:
      third = 4;
      fifth = 7;
      break;
    case ChordQuality::Minor:
    case ChordQuality::Minor7:
      third = 3;
      fifth = 7;
      break;
    case ChordQuality::Diminished:
    case ChordQuality::HalfDiminished7:
    case ChordQuality::Diminished7:
      third = 3;
      fifth = 6;
      break;
    case ChordQuality::Augmented:
      third = 4;
      fifth = 8;
      break;
  }
  return {
      static_cast<std::uint8_t>(chord.root_pc % 12),
      static_cast<std::uint8_t>((chord.root_pc + third) % 12),
      static_cast<std::uint8_t>((chord.root_pc + fifth) % 12),
  };
}

// 7th-chord arithmetic lives in composer/chord_voicing.h so spacing,
// voicing helpers, and downstream P8 modulation code share one source
// of truth. We import the names below into this translation unit so
// existing rule bodies don't have to qualify them.
using ::bach::composer::hasSeventh;
using ::bach::composer::seventhOffset;

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

bool resolvesLeadingTone(std::uint8_t leading_pitch, std::uint8_t resolution_pitch,
                         const HarmonicPlan& plan) {
  return rule_helpers::isLeadingToneResolution(leading_pitch, static_cast<int>(resolution_pitch),
                                               plan);
}

std::array<std::uint8_t, 7> scalePcs(const HarmonicPlan& plan) {
  if (plan.is_minor) {
    return {
        static_cast<std::uint8_t>(plan.tonic_pc % 12),
        static_cast<std::uint8_t>((plan.tonic_pc + 2) % 12),
        static_cast<std::uint8_t>((plan.tonic_pc + 3) % 12),
        static_cast<std::uint8_t>((plan.tonic_pc + 5) % 12),
        static_cast<std::uint8_t>((plan.tonic_pc + 7) % 12),
        static_cast<std::uint8_t>((plan.tonic_pc + 8) % 12),
        static_cast<std::uint8_t>((plan.tonic_pc + 11) % 12),
    };
  }
  return {
      static_cast<std::uint8_t>(plan.tonic_pc % 12),
      static_cast<std::uint8_t>((plan.tonic_pc + 2) % 12),
      static_cast<std::uint8_t>((plan.tonic_pc + 4) % 12),
      static_cast<std::uint8_t>((plan.tonic_pc + 5) % 12),
      static_cast<std::uint8_t>((plan.tonic_pc + 7) % 12),
      static_cast<std::uint8_t>((plan.tonic_pc + 9) % 12),
      static_cast<std::uint8_t>((plan.tonic_pc + 11) % 12),
  };
}

int scaleIndex(std::uint8_t pc, const HarmonicPlan& plan) {
  const auto pcs = scalePcs(plan);
  for (int i = 0; i < static_cast<int>(pcs.size()); ++i) {
    if (pcs[static_cast<std::size_t>(i)] == pc)
      return i;
  }
  return -1;
}

bool isAugmentedMelodicInterval(std::uint8_t from, std::uint8_t to, const HarmonicPlan& plan) {
  const int semis = std::abs(static_cast<int>(to) - static_cast<int>(from)) % 12;
  if (semis == 6)
    return true;  // augmented fourth spelling is indistinguishable from tritone in MIDI.
  if (semis != 3)
    return false;
  const int a = scaleIndex(pitchClass(from), plan);
  const int b = scaleIndex(pitchClass(to), plan);
  if (a < 0 || b < 0)
    return true;
  const int degree_distance = std::abs(a - b);
  return degree_distance == 1 || degree_distance == 6;
}

bool isDiminishedMelodicInterval(std::uint8_t from, std::uint8_t to) {
  const int semis = std::abs(static_cast<int>(to) - static_cast<int>(from)) % 12;
  return semis == 6 || semis == 11;
}

bool isPerfectFifth(int semitones) {
  return std::abs(semitones) % 12 == 7;
}

std::size_t noteIndexStartingAt(const std::vector<NoteEvent>& notes, VoiceId voice, Tick tick) {
  for (std::size_t i = 0; i < notes.size(); ++i) {
    if (notes[i].voice == voice && notes[i].start_tick == tick)
      return i;
  }
  return notes.size();
}

bool isTonicTriadPc(std::uint8_t pc, const HarmonicPlan& plan) {
  const std::uint8_t tonic = static_cast<std::uint8_t>(plan.tonic_pc % 12);
  const std::uint8_t third = static_cast<std::uint8_t>((tonic + (plan.is_minor ? 3 : 4)) % 12);
  const std::uint8_t fifth = static_cast<std::uint8_t>((tonic + 7) % 12);
  return pc == tonic || pc == third || pc == fifth;
}

bool hasRuleBit(const std::vector<NoteProvenance>& provenance, std::size_t index, RuleBit bit) {
  if (index >= provenance.size())
    return false;
  return (provenance[index].satisfied_rules & (1ull << bit)) != 0;
}

}  // namespace

ValidationReport Validator::validate(const std::vector<NoteEvent>& notes,
                                     const std::vector<NoteProvenance>& provenance,
                                     const HarmonicPlan& harmonic_plan,
                                     const Material& material) const {
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

  // P3 cadence voice leading. CadenceEvent marks a required two-note
  // formula at tick-kTicksPerBeat -> tick. The upper line is voice 0;
  // the bass line is the highest-indexed voice that is actually
  // sounding at the cadence tick (voices that have not yet entered —
  // e.g. V2 before its delayed re-entry in a 3-voice fugue — are
  // excluded so cadences inside a 2-voice exposition still validate).
  for (const auto& cadence : harmonic_plan.cadences) {
    if (voices.size() < 2 || cadence.tick < kTicksPerBeat) {
      ValidationFailure failure;
      failure.rule_id = "cadence_voice_leading";
      failure.kind = FailKind::StructuralFail;
      report.failures.push_back(failure);
      continue;
    }
    const VoiceId upper_voice = voices.front();
    const Tick approach_tick = cadence.tick - kTicksPerBeat;
    VoiceId bass_voice = upper_voice;
    for (auto it = voices.rbegin(); it != voices.rend(); ++it) {
      if (*it == upper_voice)
        continue;
      if (voicePitchAt(notes, *it, cadence.tick) != 0) {
        bass_voice = *it;
        break;
      }
    }
    if (bass_voice == upper_voice) {
      ValidationFailure failure;
      failure.rule_id = "cadence_voice_leading";
      failure.kind = FailKind::StructuralFail;
      report.failures.push_back(failure);
      continue;
    }
    const std::uint8_t upper_prev = voicePitchAt(notes, upper_voice, approach_tick);
    const std::uint8_t upper_now = voicePitchAt(notes, upper_voice, cadence.tick);
    const std::uint8_t bass_prev = voicePitchAt(notes, bass_voice, approach_tick);
    const std::uint8_t bass_now = voicePitchAt(notes, bass_voice, cadence.tick);
    const std::uint8_t tonic = static_cast<std::uint8_t>(harmonic_plan.tonic_pc % 12);
    const std::uint8_t dominant = static_cast<std::uint8_t>((tonic + 7) % 12);
    const std::uint8_t subdominant = static_cast<std::uint8_t>((tonic + 5) % 12);
    const std::uint8_t submediant = static_cast<std::uint8_t>((tonic + 9) % 12);
    const std::uint8_t minor_submediant = static_cast<std::uint8_t>((tonic + 8) % 12);
    bool ok = upper_prev != 0 && upper_now != 0 && bass_prev != 0 && bass_now != 0;
    if (ok) {
      switch (cadence.type) {
        case CadenceType::Perfect:
          ok = resolvesLeadingTone(upper_prev, upper_now, harmonic_plan) &&
               pitchClass(bass_prev) == dominant && pitchClass(bass_now) == tonic;
          break;
        case CadenceType::ImperfectAuthentic:
          ok = pitchClass(bass_prev) == dominant && pitchClass(bass_now) == tonic &&
               isTonicTriadPc(pitchClass(upper_now), harmonic_plan);
          break;
        case CadenceType::PicardyThird:
          ok = resolvesLeadingTone(upper_prev, upper_now, harmonic_plan) &&
               pitchClass(bass_prev) == dominant && pitchClass(bass_now) == tonic &&
               pitchClass(upper_now) == static_cast<std::uint8_t>((tonic + 4) % 12);
          break;
        case CadenceType::Plagal:
          ok = pitchClass(bass_prev) == subdominant && pitchClass(bass_now) == tonic &&
               isTonicTriadPc(pitchClass(upper_now), harmonic_plan);
          break;
        case CadenceType::Half:
          ok = pitchClass(bass_now) == dominant;
          break;
        case CadenceType::Deceptive:
          ok = pitchClass(bass_prev) == dominant && pitchClass(bass_now) == submediant;
          break;
        case CadenceType::Phrygian:
          ok = pitchClass(bass_prev) == minor_submediant && pitchClass(bass_now) == dominant &&
               bass_now < bass_prev &&
               std::abs(static_cast<int>(bass_now) - static_cast<int>(bass_prev)) <= 2;
          break;
      }
    }
    if (!ok) {
      SpanId fail_span = kInvalidSpanId;
      for (std::size_t k = 0; k < notes.size(); ++k) {
        if (notes[k].voice == bass_voice && notes[k].start_tick == cadence.tick) {
          if (k < provenance.size())
            fail_span = provenance[k].span_id;
          break;
        }
      }
      ValidationFailure failure;
      failure.span_id = fail_span;
      failure.rule_id = "cadence_voice_leading";
      report.failures.push_back(failure);
    }
  }

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
        const std::size_t current_lower_index = noteIndexStartingAt(notes, voices[vb], t);
        const bool current_is_cadence =
            hasRuleBit(provenance, current_lower_index, RuleBit::CadenceCellCommitted);
        const bool current_is_material =
            current_lower_index < provenance.size() &&
            provenance[current_lower_index].source == NoteSource::Material;
        if (prev_interval != INT32_MIN && both_moved && isPerfectInterval(interval) &&
            isPerfectInterval(prev_interval) && interval == prev_interval && interval != 0 &&
            !current_is_cadence && !current_is_material) {
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
        const int motion_a = static_cast<int>(pa) - static_cast<int>(prev_pa);
        const int motion_b = static_cast<int>(pb) - static_cast<int>(prev_pb);
        const bool similar_motion = prev_pa != 0 && prev_pb != 0 && motion_a != 0 &&
                                    motion_b != 0 && ((motion_a > 0) == (motion_b > 0));
        if (prev_interval != INT32_MIN && similar_motion && isPerfectInterval(interval) &&
            !isPerfectInterval(prev_interval) && !current_is_cadence && !current_is_material) {
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
          failure.rule_id =
              isPerfectFifth(interval) ? "hidden_parallel_fifth" : "hidden_parallel_octave";
          report.failures.push_back(failure);
        }
        prev_interval = interval;
        prev_pa = pa;
        prev_pb = pb;
      }
    }
  }

  // 2b. Cross-relation: chromatic alteration conflict in different voices,
  // either simultaneous or on adjacent starts. Natural half-steps (E/F,
  // B/C) are not cross-relations because they are different letter names.
  for (std::size_t i = 0; i < notes.size(); ++i) {
    for (std::size_t j = i + 1; j < notes.size(); ++j) {
      if (notes[i].voice == notes[j].voice)
        continue;
      const bool simultaneous = notes[i].start_tick <= notes[j].start_tick &&
                                notes[j].start_tick < notes[i].start_tick + notes[i].duration;
      const bool reverse_simultaneous =
          notes[j].start_tick <= notes[i].start_tick &&
          notes[i].start_tick < notes[j].start_tick + notes[j].duration;
      const bool adjacent =
          std::abs(static_cast<int>(notes[i].start_tick) - static_cast<int>(notes[j].start_tick)) <=
          static_cast<int>(kTicksPerBeat);
      if (!simultaneous && !reverse_simultaneous && !adjacent)
        continue;
      if (!isCrossRelationPc(pitchClass(notes[i].pitch), pitchClass(notes[j].pitch)))
        continue;
      SpanId fail_span = kInvalidSpanId;
      if (j < provenance.size()) {
        fail_span = provenance[j].span_id;
      }
      ValidationFailure failure;
      failure.span_id = fail_span;
      failure.rule_id = "cross_relation";
      report.failures.push_back(failure);
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

    // Rule P1: forbidden melodic augmented/diminished intervals and
    // direct tritone leaps. Scope is Compose-source notes; Material is
    // fixed input and is handled by material markers / catalog curation.
    for (std::size_t i = 1; i < indices.size(); ++i) {
      const std::size_t current = indices[i];
      if (current >= provenance.size())
        continue;
      if (provenance[current].source != NoteSource::Compose)
        continue;
      if (hasRuleBit(provenance, current, RuleBit::CadenceCellCommitted))
        continue;
      const std::uint8_t prev_pitch = notes[indices[i - 1]].pitch;
      const std::uint8_t current_pitch = notes[current].pitch;
      const int semis =
          std::abs(static_cast<int>(current_pitch) - static_cast<int>(prev_pitch)) % 12;
      if (isAugmentedMelodicInterval(prev_pitch, current_pitch, harmonic_plan)) {
        ValidationFailure failure;
        failure.span_id = provenance[current].span_id;
        failure.rule_id = "augmented_melodic";
        report.failures.push_back(failure);
      }
      if (semis == 6) {
        ValidationFailure failure;
        failure.span_id = provenance[current].span_id;
        failure.rule_id = "tritone_melodic";
        report.failures.push_back(failure);
      }
      if (isDiminishedMelodicInterval(prev_pitch, current_pitch)) {
        ValidationFailure failure;
        failure.span_id = provenance[current].span_id;
        failure.rule_id = "diminished_melodic";
        report.failures.push_back(failure);
      }
    }

    // Rule P1: leading tone resolves upward to tonic on the next note.
    for (std::size_t i = 0; i < indices.size(); ++i) {
      const std::size_t current = indices[i];
      if (current >= provenance.size())
        continue;
      if (provenance[current].source != NoteSource::Compose)
        continue;
      if (!isLeadingTone(notes[current].pitch, harmonic_plan))
        continue;
      if (i + 1 >= indices.size() ||
          !resolvesLeadingTone(notes[current].pitch, notes[indices[i + 1]].pitch, harmonic_plan)) {
        ValidationFailure failure;
        failure.span_id = provenance[current].span_id;
        failure.rule_id = "leading_tone_resolution";
        report.failures.push_back(failure);
      }
    }

    // Rule 3: consecutive_leaps.
    for (std::size_t i = 2; i < indices.size(); ++i) {
      if (indices[i] >= provenance.size() || provenance[indices[i]].source != NoteSource::Compose)
        continue;
      if (hasRuleBit(provenance, indices[i], RuleBit::CadenceCellCommitted))
        continue;
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
      if (hasRuleBit(provenance, k, RuleBit::CadenceCellCommitted))
        continue;
      if (i + 1 < indices.size() &&
          hasRuleBit(provenance, indices[i + 1], RuleBit::CadenceCellCommitted))
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

  // P4 Suspension checks. For each declared SuspensionPattern, the
  // emitted notes at preparation_tick / suspension_tick / resolution_tick
  // must match its declared pitches, the preparation must be consonant
  // against the bass at its onset (and tie via pitch identity into the
  // suspension), and the resolution must be a single diatonic step
  // (1 or 2 semitones) in the direction the SuspensionType prescribes
  // (down for 4-3 / 7-6 / 9-8, up for 2-3).
  for (const auto& sp : material.suspension_patterns) {
    const std::uint8_t prep_actual = voicePitchAt(notes, sp.voice, sp.preparation_tick);
    const std::uint8_t sus_actual = voicePitchAt(notes, sp.voice, sp.suspension_tick);
    const std::uint8_t res_actual = voicePitchAt(notes, sp.voice, sp.resolution_tick);
    SpanId fail_span = kInvalidSpanId;
    for (std::size_t k = 0; k < notes.size(); ++k) {
      if (notes[k].voice == sp.voice && notes[k].start_tick == sp.suspension_tick) {
        if (k < provenance.size())
          fail_span = provenance[k].span_id;
        break;
      }
    }

    // Preparation: must be consonant against the lowest sounding voice at
    // the preparation tick and tie (pitch identity) into the suspension.
    // If no other voice sounds, the preparation rule is vacuously OK.
    bool prep_ok = prep_actual != 0 && prep_actual == sus_actual;
    if (prep_ok) {
      std::uint8_t bass_pitch = 0;
      for (auto it = voices.rbegin(); it != voices.rend(); ++it) {
        if (*it == sp.voice)
          continue;
        const std::uint8_t bp = voicePitchAt(notes, *it, sp.preparation_tick);
        if (bp != 0) {
          bass_pitch = bp;
          break;
        }
      }
      if (bass_pitch != 0) {
        const int interval = static_cast<int>(prep_actual) - static_cast<int>(bass_pitch);
        if (!rule_helpers::isConsonantInterval(interval)) {
          prep_ok = false;
        }
      }
    }
    if (!prep_ok) {
      ValidationFailure failure;
      failure.span_id = fail_span;
      failure.rule_id = "suspension_preparation";
      failure.kind = FailKind::MusicalFail;
      report.failures.push_back(failure);
    }

    // Resolution: must be a 1- or 2-semitone step in the prescribed
    // direction. Sus2_3 ascends; the other three descend.
    bool res_ok = sus_actual != 0 && res_actual != 0;
    if (res_ok) {
      const int delta = static_cast<int>(res_actual) - static_cast<int>(sus_actual);
      const bool small_step = std::abs(delta) == 1 || std::abs(delta) == 2;
      const bool ascending = sp.type == SuspensionType::Sus2_3;
      res_ok = small_step && ((ascending && delta > 0) || (!ascending && delta < 0));
    }
    if (!res_ok) {
      ValidationFailure failure;
      failure.span_id = fail_span;
      failure.rule_id = "suspension_resolution_step_down";
      failure.kind = FailKind::MusicalFail;
      report.failures.push_back(failure);
    }
  }

  // P6 Tonal answer head-mutation check. Bach's tonal answer keeps the
  // answer entry on the dominant key but mutates head-of-subject tonic
  // and dominant degrees so the answer opens on the dominant (mapping
  // I→V) and any subject-dominant pitch resolves back to the tonic
  // (mapping V→I). The minimal verifiable invariant is the very first
  // note of the head:
  //   subject_pc[0] == tonic_pc  ⇒ tonal_answer_pc[0] == dominant_pc
  //   subject_pc[0] == dominant_pc ⇒ tonal_answer_pc[0] == tonic_pc
  // Any other mapping fires `tonal_answer_dominant_mapping`. The check
  // runs only when `use_tonal_answer` is set AND both subject and
  // tonal_answer are non-empty.
  if (material.use_tonal_answer && !material.subject.empty() && !material.tonal_answer.empty()) {
    const std::uint8_t tonic_pc = static_cast<std::uint8_t>(harmonic_plan.tonic_pc % 12);
    const std::uint8_t dom_pc = static_cast<std::uint8_t>((harmonic_plan.tonic_pc + 7) % 12);
    const std::uint8_t subj_head_pc =
        static_cast<std::uint8_t>(material.subject.front().pitch % 12);
    const std::uint8_t ta_head_pc =
        static_cast<std::uint8_t>(material.tonal_answer.front().pitch % 12);
    bool mapping_ok = true;
    if (subj_head_pc == tonic_pc) {
      mapping_ok = (ta_head_pc == dom_pc);
    } else if (subj_head_pc == dom_pc) {
      mapping_ok = (ta_head_pc == tonic_pc);
    }
    // Subjects that don't open on I or V are not required to mutate;
    // mapping_ok stays true so the check is vacuously satisfied.
    if (!mapping_ok) {
      ValidationFailure failure;
      failure.rule_id = "tonal_answer_dominant_mapping";
      failure.kind = FailKind::MusicalFail;
      report.failures.push_back(failure);
    }
  }

  // P6 Countersubject continuity check. When a Countersubject is
  // declared (material.countersubject non-empty) AND an answer is
  // also declared (material.answer or material.tonal_answer), the
  // countersubject must sound continuously across the answer's
  // tick window (every quarter-note position in the window has a
  // sounding note from the CS, allowing for tied notes that span
  // multiple beats). Any gap fires `countersubject_continuous`.
  //
  // The CS voice is identified by the first note's voice field of
  // material.countersubject. The answer window is the [first, last]
  // tick range of material.tonal_answer if used, else material.answer.
  if (!material.countersubject.empty()) {
    const std::vector<MaterialNote>& answer_src =
        (material.use_tonal_answer && !material.tonal_answer.empty()) ? material.tonal_answer
                                                                      : material.answer;
    if (!answer_src.empty()) {
      const Tick window_start = answer_src.front().start_tick;
      const Tick window_end = answer_src.back().start_tick + answer_src.back().duration;
      // Walk window in quarter-beat increments; require a CS-voice note
      // sounding at every step. CS voice id is taken from the first CS
      // material note; if the CS material is multi-voice we'd need
      // explicit voice tagging, but Phase 6 uses single-voice CS only.
      const VoiceId cs_voice = 0;  // Phase 6 convention: CS is in V0
      bool gap_found = false;
      for (Tick t = window_start; t < window_end; t += kTicksPerBeat) {
        if (voicePitchAt(notes, cs_voice, t) == 0) {
          gap_found = true;
          break;
        }
      }
      if (gap_found) {
        ValidationFailure failure;
        failure.rule_id = "countersubject_continuous";
        failure.kind = FailKind::MusicalFail;
        report.failures.push_back(failure);
      }
    }
  }

  // P5 Episode motif-derivation check. For every EpisodeFragment in
  // Material::episodes, re-derive the expected notes via
  // motif_ops::applyTransform on the declared source slice of
  // Material::subject and assert that every emitted note in the target
  // voice at the derived ticks matches by (start_tick, duration, pitch).
  // A mismatch — wrong pitch, wrong duration, or missing note — fires
  // an `episode_motif_derived` MusicalFail.
  //
  // Empty material.episodes means "no Episode spans active", so the
  // loop short-circuits and existing harness fixtures stay green.
  for (const auto& frag : material.episodes) {
    if (material.subject.empty())
      continue;
    const std::size_t src_begin = frag.source_start_index;
    if (src_begin >= material.subject.size())
      continue;
    const std::size_t src_count =
        (frag.source_count == 0) ? (material.subject.size() - src_begin) : frag.source_count;
    const std::size_t src_end = std::min(src_begin + src_count, material.subject.size());
    std::vector<MaterialNote> source_slice;
    source_slice.reserve(src_end - src_begin);
    for (std::size_t k = src_begin; k < src_end; ++k) {
      source_slice.push_back(material.subject[k]);
    }
    const auto transform = static_cast<motif_ops::EpisodeMotifTransform>(frag.transform);
    const int factor = (transform == motif_ops::EpisodeMotifTransform::Augment)
                           ? frag.augment_factor
                           : frag.diminish_factor;
    const auto expected = motif_ops::applyTransform(source_slice, transform, frag.target_start_tick,
                                                    frag.invert_pivot, factor);
    for (const auto& exp : expected) {
      SpanId fail_span = kInvalidSpanId;
      bool matched = false;
      for (std::size_t k = 0; k < notes.size(); ++k) {
        if (notes[k].voice != frag.voice)
          continue;
        if (notes[k].start_tick != exp.start_tick)
          continue;
        if (k < provenance.size())
          fail_span = provenance[k].span_id;
        if (notes[k].pitch == exp.pitch && notes[k].duration == exp.duration) {
          matched = true;
        }
        break;
      }
      if (!matched) {
        ValidationFailure failure;
        failure.span_id = fail_span;
        failure.rule_id = "episode_motif_derived";
        failure.kind = FailKind::MusicalFail;
        report.failures.push_back(failure);
      }
    }
  }

  // P7 doubling / spacing / voice-leading rules.
  //
  // The three checks share a per-chord setup: at each ChordEvent that
  // declares `has_degree`, collect every voice's sounding pitch at the
  // chord's start_tick (so we sample the moment the new chord enters
  // the sonority) and walk the rules:
  //   1. doubling_no_leading_tone: in any chord that contains the
  //      diatonic leading tone, the leading-tone pitch class must not
  //      sound in two or more voices simultaneously.
  //   2. doubling_no_seventh: for 7th-quality chords the chord
  //      seventh's pitch class must not sound in two or more voices.
  //   3. spacing_adjacent_voices_within_octave: for ≥ 3 voice
  //      sonorities the interval between upper-voice pairs (V0-V1 and
  //      V1-V2) must be within an octave (≤ 12 semitones). The
  //      bass-to-tenor pair (V2-V3 in 4-voice writing) is allowed to
  //      exceed an octave per Bach textbook spacing.
  //
  // ChordEvents that leave `has_degree` unset are skipped — pre-P7
  // fixtures stay backwards-compatible. The rules look only at the
  // sounding voices at chord_start; mid-chord neighbor/passing motion
  // is not checked here (NCT recognition lives elsewhere).
  if (!harmonic_plan.chords.empty()) {
    // Find the max voice id present in the notes so we can iterate
    // voice 0..max_voice inclusively without depending on caller
    // metadata.
    VoiceId max_voice = 0;
    for (const auto& n : notes) {
      if (n.voice > max_voice)
        max_voice = n.voice;
    }
    const std::uint8_t tonic_pc = static_cast<std::uint8_t>(harmonic_plan.tonic_pc % 12);
    const std::uint8_t leading_tone_pc = static_cast<std::uint8_t>((tonic_pc + 11) % 12);

    for (const auto& chord : harmonic_plan.chords) {
      if (!chord.has_degree)
        continue;
      // Sample every voice at chord_start. Voices that aren't sounding
      // (returning 0) are skipped so we don't count "no note" as a
      // leading-tone collision.
      std::vector<std::uint8_t> voiced_pitches;
      voiced_pitches.reserve(max_voice + 1);
      std::vector<VoiceId> voiced_ids;
      voiced_ids.reserve(max_voice + 1);
      for (VoiceId v = 0; v <= max_voice; ++v) {
        const std::uint8_t p = voicePitchAt(notes, v, chord.start_tick);
        if (p > 0) {
          voiced_pitches.push_back(p);
          voiced_ids.push_back(v);
        }
      }
      if (voiced_pitches.size() < 2)
        continue;

      // Rule 1: doubling_no_leading_tone.
      // The leading tone is only relevant to chords that contain it
      // (V, vii, V7, viidim7). We approximate "contains the leading
      // tone" by checking the chord triad's pitch classes; this
      // covers V and vii in any key and skips harmless cases like I
      // or IV where the leading tone is non-chord.
      const auto triad = triadFor(chord);
      const bool chord_owns_leading_tone = (triad[0] == leading_tone_pc) ||
                                           (triad[1] == leading_tone_pc) ||
                                           (triad[2] == leading_tone_pc);
      if (chord_owns_leading_tone) {
        std::size_t lt_count = 0;
        for (std::uint8_t p : voiced_pitches) {
          if (static_cast<std::uint8_t>(p % 12) == leading_tone_pc)
            ++lt_count;
        }
        if (lt_count >= 2) {
          ValidationFailure failure;
          failure.rule_id = "doubling_no_leading_tone";
          failure.kind = FailKind::MusicalFail;
          report.failures.push_back(failure);
        }
      }

      // Rule 2: doubling_no_seventh.
      if (hasSeventh(chord.quality)) {
        const std::uint8_t seventh_pc =
            static_cast<std::uint8_t>((chord.root_pc + seventhOffset(chord.quality)) % 12);
        std::size_t sev_count = 0;
        for (std::uint8_t p : voiced_pitches) {
          if (static_cast<std::uint8_t>(p % 12) == seventh_pc)
            ++sev_count;
        }
        if (sev_count >= 2) {
          ValidationFailure failure;
          failure.rule_id = "doubling_no_seventh";
          failure.kind = FailKind::MusicalFail;
          report.failures.push_back(failure);
        }
      }

      // Rule 3: spacing_adjacent_voices_within_octave.
      // Bach textbook: keep upper-voice (S/A, A/T) pairs within an
      // octave; the bass-tenor gap may exceed. We approximate this by
      // requiring intervals between voiced_ids[i] and voiced_ids[i+1]
      // (in ascending voice-id order, which equals descending vocal
      // register: V0 = soprano, V1 = alto, V2 = tenor, V3 = bass) to
      // be ≤ 12 semitones for every adjacent pair *except* the last
      // (tenor-bass) when 4 voices are present.
      if (voiced_pitches.size() >= 3) {
        // Textbook (Bach part-writing): upper voice pairs stay within
        // an octave; the bottom two voices may sit wider. That means
        // we check N - 2 adjacent pairs from the top: for 3 voices
        // only (V0, V1) is checked; for 4 voices both (V0, V1) and
        // (V1, V2) are checked; (V_{N-2}, V_{N-1}) is always
        // excluded.
        const std::size_t pairs_to_check = voiced_pitches.size() - 2;
        for (std::size_t i = 0; i < pairs_to_check; ++i) {
          const int hi = static_cast<int>(voiced_pitches[i]);
          const int lo = static_cast<int>(voiced_pitches[i + 1]);
          if (std::abs(hi - lo) > 12) {
            ValidationFailure failure;
            failure.rule_id = "spacing_adjacent_voices_within_octave";
            failure.kind = FailKind::MusicalFail;
            report.failures.push_back(failure);
            break;  // One report per chord is sufficient.
          }
        }
      }
    }
  }

  if (!report.failures.empty()) {
    report.status = ValidationStatus::FailedSpan;
  }
  return report;
}

}  // namespace bach::composer
