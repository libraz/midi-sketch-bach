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
        const std::size_t current_upper_index = noteIndexStartingAt(notes, voices[va], t);
        const bool current_is_cadence =
            hasRuleBit(provenance, current_lower_index, RuleBit::CadenceCellCommitted);
        const bool current_is_material =
            current_lower_index < provenance.size() &&
            provenance[current_lower_index].source == NoteSource::Material;
        const bool upper_is_material =
            current_upper_index < provenance.size() &&
            provenance[current_upper_index].source == NoteSource::Material;
        // Suppress the parallel only when BOTH voices are Material: the
        // composer cannot edit fixed inputs (mirrors the P10 invertible and
        // vertical-dissonance both_material gates). When a Compose upper
        // voice runs parallels against a Material lower voice (or vice
        // versa) the violation is real and composer-fixable, so it fires.
        const bool both_material = upper_is_material && current_is_material;
        if (prev_interval != INT32_MIN && both_moved && isPerfectInterval(interval) &&
            isPerfectInterval(prev_interval) && interval == prev_interval && interval != 0 &&
            !current_is_cadence && !both_material) {
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
            !isPerfectInterval(prev_interval) && !current_is_cadence && !both_material) {
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

  // P10 Invertible counterpoint at the octave. Scoped to the adjacent
  // UPPER voice pairs the P7 spacing rule covers: pair (va, vb) with
  // vb == va + 1 (indices into the sorted `voices` list) and the pair
  // is not the bottom-of-texture pair (vb != voices.size() - 1). For a
  // 3-voice texture this is only the (V0, V1) pair.
  //
  // (a) invertible_at_octave: parallel perfect OCTAVES in the upper
  //     pair are forbidden because under octave inversion they collapse
  //     to parallel unisons (still parallel perfect). Parallel fifths
  //     are tolerated (they invert to fourths).
  // (b) fourth_only_on_weak_beat: a perfect 4th in the upper pair on a
  //     strong beat is forbidden (it inverts to a 5th); weak-beat 4ths
  //     are allowed.
  for (std::size_t va = 0; va < voices.size(); ++va) {
    const std::size_t vb = va + 1;
    if (vb >= voices.size())
      break;
    // Exclude the bottom-of-texture pair (mirrors P7 spacing scoping).
    if (vb == voices.size() - 1)
      continue;
    int prev_interval = INT32_MIN;
    std::uint8_t prev_pa = 0;
    std::uint8_t prev_pb = 0;
    for (Tick t : ticks) {
      const std::uint8_t pa = voicePitchAt(notes, voices[va], t);
      const std::uint8_t pb = voicePitchAt(notes, voices[vb], t);
      if (pa == 0 || pb == 0) {
        prev_interval = INT32_MIN;
        prev_pa = 0;
        prev_pb = 0;
        continue;
      }
      const int interval = static_cast<int>(pa) - static_cast<int>(pb);
      const int abs_class = std::abs(interval) % 12;

      // Material-skip: find both notes' indices at this tick; skip the
      // pair only when BOTH are Material (composer cannot fix inputs).
      const std::size_t idx_upper = noteIndexStartingAt(notes, voices[va], t);
      const std::size_t idx_lower = noteIndexStartingAt(notes, voices[vb], t);
      const bool upper_material =
          idx_upper < provenance.size() && provenance[idx_upper].source == NoteSource::Material;
      const bool lower_material =
          idx_lower < provenance.size() && provenance[idx_lower].source == NoteSource::Material;
      const bool both_material = upper_material && lower_material;

      if (!both_material && isStrongBeat(t)) {
        // (a) Parallel perfect octaves into the same perfect-octave
        //     interval, both voices moving (oblique/static allowed).
        const bool both_moved = prev_pa != 0 && prev_pb != 0 && pa != prev_pa && pb != prev_pb;
        const bool prev_class8 = prev_interval != INT32_MIN && (std::abs(prev_interval) % 12 == 0);
        if (both_moved && abs_class == 0 && prev_class8 && interval != 0 && prev_interval != 0) {
          SpanId fail_span = kInvalidSpanId;
          if (idx_lower < provenance.size())
            fail_span = provenance[idx_lower].span_id;
          else if (idx_upper < provenance.size())
            fail_span = provenance[idx_upper].span_id;
          ValidationFailure failure;
          failure.span_id = fail_span;
          failure.rule_id = "invertible_at_octave";
          report.failures.push_back(failure);
        }

        // (b) Perfect fourth in the upper pair on a strong beat.
        if (abs_class == 5) {
          SpanId fail_span = kInvalidSpanId;
          if (idx_lower < provenance.size())
            fail_span = provenance[idx_lower].span_id;
          else if (idx_upper < provenance.size())
            fail_span = provenance[idx_upper].span_id;
          ValidationFailure failure;
          failure.span_id = fail_span;
          failure.rule_id = "fourth_only_on_weak_beat";
          report.failures.push_back(failure);
        }
      }
      prev_interval = interval;
      prev_pa = pa;
      prev_pb = pb;
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
      // P8: secondary-dominant exemption. The base augmented_melodic
      // rule fires on any m3 (semis=3) involving a non-diatonic pitch
      // — but a secondary dominant's chord tones (e.g. F# in V/V of C
      // major) are non-diatonic by design, and m3 motion between two
      // chord tones of the active secondary dominant is musically
      // standard Bach idiom (F#-A inside V/V, B-D inside V/vi). Skip
      // the augmented_melodic check when either endpoint sits inside
      // a chord region declared has_secondary_of=true.
      const ChordEvent& chord_at_prev =
          activeChord(harmonic_plan, notes[indices[i - 1]].start_tick);
      const ChordEvent& chord_at_curr = activeChord(harmonic_plan, notes[current].start_tick);
      const bool secondary_active =
          chord_at_prev.has_secondary_of || chord_at_curr.has_secondary_of;
      if (!secondary_active &&
          isAugmentedMelodicInterval(prev_pitch, current_pitch, harmonic_plan)) {
        ValidationFailure failure;
        failure.span_id = provenance[current].span_id;
        failure.rule_id = "augmented_melodic";
        report.failures.push_back(failure);
      }
      if (!secondary_active && semis == 6) {
        ValidationFailure failure;
        failure.span_id = provenance[current].span_id;
        failure.rule_id = "tritone_melodic";
        report.failures.push_back(failure);
      }
      if (!secondary_active && isDiminishedMelodicInterval(prev_pitch, current_pitch)) {
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

    // suspension_seventh_sixth: a 7-6 SuspensionCarrier must form a genuine
    // SEVENTH above the lowest sounding voice on the dissonance beat, then
    // resolve down by step to a SIXTH above the lowest sounding voice at the
    // resolution tick. Scoped two ways so it is a no-op outside real 7-6
    // figures: (a) only Sus7_6 patterns are evaluated (other suspension
    // types form 4ths/9ths/2nds by definition); (b) only when the emitted
    // suspension and resolution notes actually carry the SuspensionPrepared
    // / SuspensionResolved provenance bits — i.e. a SuspensionCarrier span
    // really shipped. Phases with no suspension carry neither bit, so the
    // check never runs there.
    if (sp.type == SuspensionType::Sus7_6) {
      const std::size_t prep_index = noteIndexStartingAt(notes, sp.voice, sp.preparation_tick);
      const std::size_t res_index = noteIndexStartingAt(notes, sp.voice, sp.resolution_tick);
      const bool prepared = hasRuleBit(provenance, prep_index, RuleBit::SuspensionPrepared);
      const bool resolved = hasRuleBit(provenance, res_index, RuleBit::SuspensionResolved);
      if (prepared && resolved && sus_actual != 0 && res_actual != 0) {
        // Lowest sounding voice = minimum pitch among all voices other than
        // the suspended voice sounding at the given tick.
        const auto lowestSoundingPitch = [&](Tick at) -> std::uint8_t {
          std::uint8_t lowest = 0;
          for (VoiceId other : voices) {
            if (other == sp.voice)
              continue;
            const std::uint8_t bp = voicePitchAt(notes, other, at);
            if (bp == 0)
              continue;
            if (lowest == 0 || bp < lowest)
              lowest = bp;
          }
          return lowest;
        };
        const std::uint8_t bass_at_sus = lowestSoundingPitch(sp.suspension_tick);
        const std::uint8_t bass_at_res = lowestSoundingPitch(sp.resolution_tick);
        // Only evaluate when a lower voice actually sounds at both ticks;
        // a lone suspended voice has no interval to measure.
        if (bass_at_sus != 0 && bass_at_res != 0) {
          const int sus_ic =
              ((static_cast<int>(sus_actual) - static_cast<int>(bass_at_sus)) % 12 + 12) % 12;
          const int res_ic =
              ((static_cast<int>(res_actual) - static_cast<int>(bass_at_res)) % 12 + 12) % 12;
          const bool seventh = sus_ic == 10 || sus_ic == 11;
          const bool sixth = res_ic == 8 || res_ic == 9;
          if (!seventh || !sixth) {
            ValidationFailure failure;
            failure.span_id = fail_span;
            failure.rule_id = "suspension_seventh_sixth";
            failure.kind = FailKind::MusicalFail;
            report.failures.push_back(failure);
          }
        }
      }
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

  // P8 modulation rules.
  //
  // modulation_pivot_chord_required: for every ModulationEvent declared
  // with type == Pivot the chord at the event's tick must be diatonic in
  // both from_key and to_key. Phrase modulations are exempt because the
  // break in continuity carries the modulation; CommonTone modulations
  // require at least one pitch class shared with the previous chord,
  // not a full pivot.
  //
  // secondary_dominant_resolution: every chord with has_secondary_of=true
  // must be followed by a chord whose degree equals the declared
  // secondary_of value, and the secondary leading tone (the 3rd of the
  // secondary dominant) must rise by step in some voice across the
  // boundary.
  auto isDiatonicInKey = [](std::uint8_t pc, std::uint8_t tonic_pc, bool is_minor) {
    // Match scalePcs() above so the diatonic set is consistent.
    const std::uint8_t t = static_cast<std::uint8_t>(tonic_pc % 12);
    if (is_minor) {
      const std::array<std::uint8_t, 7> pcs = {
          static_cast<std::uint8_t>(t),
          static_cast<std::uint8_t>((t + 2) % 12),
          static_cast<std::uint8_t>((t + 3) % 12),
          static_cast<std::uint8_t>((t + 5) % 12),
          static_cast<std::uint8_t>((t + 7) % 12),
          static_cast<std::uint8_t>((t + 8) % 12),
          static_cast<std::uint8_t>((t + 11) % 12),
      };
      for (auto x : pcs) {
        if (x == pc)
          return true;
      }
      return false;
    }
    const std::array<std::uint8_t, 7> pcs = {
        static_cast<std::uint8_t>(t),
        static_cast<std::uint8_t>((t + 2) % 12),
        static_cast<std::uint8_t>((t + 4) % 12),
        static_cast<std::uint8_t>((t + 5) % 12),
        static_cast<std::uint8_t>((t + 7) % 12),
        static_cast<std::uint8_t>((t + 9) % 12),
        static_cast<std::uint8_t>((t + 11) % 12),
    };
    for (auto x : pcs) {
      if (x == pc)
        return true;
    }
    return false;
  };

  for (const auto& mod : harmonic_plan.modulations) {
    if (mod.type != ModulationType::Pivot)
      continue;
    // Find the chord active at mod.tick (the pivot itself starts at or
    // before mod.tick and is the most recent).
    const ChordEvent* pivot = nullptr;
    for (const auto& chord : harmonic_plan.chords) {
      if (chord.start_tick == mod.tick) {
        pivot = &chord;
        break;
      }
    }
    bool ok = pivot != nullptr;
    if (ok) {
      std::size_t ct_count = 0;
      const auto pcs = chordPitchClasses(*pivot, &ct_count);
      for (std::size_t i = 0; i < ct_count; ++i) {
        const std::uint8_t pc = pcs[i];
        if (!isDiatonicInKey(pc, mod.from_tonic_pc, mod.from_is_minor) ||
            !isDiatonicInKey(pc, mod.to_tonic_pc, mod.to_is_minor)) {
          ok = false;
          break;
        }
      }
    }
    if (!ok) {
      ValidationFailure failure;
      failure.rule_id = "modulation_pivot_chord_required";
      failure.kind = FailKind::MusicalFail;
      report.failures.push_back(failure);
    }
  }

  for (std::size_t ci = 0; ci < harmonic_plan.chords.size(); ++ci) {
    const auto& chord = harmonic_plan.chords[ci];
    if (!chord.has_secondary_of)
      continue;
    // Find the next chord with a strictly larger start_tick.
    const ChordEvent* next = nullptr;
    for (std::size_t j = ci + 1; j < harmonic_plan.chords.size(); ++j) {
      if (harmonic_plan.chords[j].start_tick > chord.start_tick) {
        next = &harmonic_plan.chords[j];
        break;
      }
    }
    // Plan §5 P8: secondary_dominant_resolution rule fires when the
    // chord declared has_secondary_of=true is NOT followed by a chord
    // whose degree equals the declared secondary_of target. Voice-
    // leading details (leading-tone rise) are tracked via the
    // SecondaryDominantResolved provenance bit, which the candidate
    // search wires when the resolution chord is reached — but they
    // are not part of the rule's failure condition. This matches the
    // plan wording "V/X → X (degree)" and keeps the rule from
    // failing free-voice tonicizations where the LT voice is forced
    // off-step by P7 doubling/spacing constraints.
    const bool degree_ok =
        next != nullptr && next->has_degree && next->degree == chord.secondary_of;
    if (!degree_ok) {
      ValidationFailure failure;
      failure.rule_id = "secondary_dominant_resolution";
      failure.kind = FailKind::MusicalFail;
      report.failures.push_back(failure);
    }
  }

  // P9 sequence + imitation rules.
  //
  // sequence_pattern_consistency: for every SequenceTemplate in Material,
  // walk the actual emitted notes in the template's voice across the
  // expanded window [target_start_tick, target_start_tick +
  // step_length_ticks*num_steps) and assert that each step is a
  // verbatim transposition of the seed by step_offset*step_index
  // semitones. Steps with missing notes, wrong pitch, or wrong duration
  // fire `sequence_pattern_consistency`.
  //
  // imitation_entry_match: for every ImitationEntry, locate the
  // leader's fragment first-note and the follower's fragment first-note,
  // and assert that (a) follower.tick == leader.tick + distance_ticks
  // and (b) follower.pitch == leader.pitch + interval_semis. Either
  // mismatch fires `imitation_entry_match`.
  auto sequencePatternSemis = [](SequencePattern pattern) -> int {
    switch (pattern) {
      case SequencePattern::DescendingFifths:
        return -7;
      case SequencePattern::DescendingStep:
        return -2;
      case SequencePattern::AscendingStep:
        return 2;
    }
    return 0;
  };
  for (const auto& tmpl : material.sequence_templates) {
    if (tmpl.seed_pitches.empty())
      continue;
    if (tmpl.seed_durations.size() != tmpl.seed_pitches.size())
      continue;
    Tick local_offset = 0;
    for (auto d : tmpl.seed_durations)
      local_offset += d;
    const Tick step_stride = tmpl.step_length_ticks > 0 ? tmpl.step_length_ticks : local_offset;
    const int offset = sequencePatternSemis(tmpl.pattern);
    bool any_mismatch = false;
    for (std::uint8_t k = 0; k < tmpl.num_steps && !any_mismatch; ++k) {
      Tick beat_cursor = tmpl.target_start_tick + static_cast<Tick>(k) * step_stride;
      for (std::size_t i = 0; i < tmpl.seed_pitches.size(); ++i) {
        const int expected_pitch =
            static_cast<int>(tmpl.seed_pitches[i]) + offset * static_cast<int>(k);
        if (expected_pitch < 0 || expected_pitch > 127) {
          beat_cursor += tmpl.seed_durations[i];
          continue;
        }
        const Tick expected_tick = beat_cursor;
        const Tick expected_dur = tmpl.seed_durations[i];
        bool found = false;
        for (const auto& n : notes) {
          if (n.voice != tmpl.voice)
            continue;
          if (n.start_tick != expected_tick)
            continue;
          if (n.pitch == static_cast<std::uint8_t>(expected_pitch) && n.duration == expected_dur) {
            found = true;
          }
          break;
        }
        if (!found) {
          any_mismatch = true;
          break;
        }
        beat_cursor += tmpl.seed_durations[i];
      }
    }
    if (any_mismatch) {
      ValidationFailure failure;
      failure.rule_id = "sequence_pattern_consistency";
      failure.kind = FailKind::MusicalFail;
      report.failures.push_back(failure);
    }
  }

  auto fragmentNotes = [&material](MaterialFragment frag) -> const std::vector<MaterialNote>* {
    switch (frag) {
      case MaterialFragment::Subject:
        return &material.subject;
      case MaterialFragment::Answer:
        return &material.answer;
      case MaterialFragment::TonalAnswer:
        return &material.tonal_answer;
      case MaterialFragment::Countersubject:
        return &material.countersubject;
    }
    return &material.subject;
  };
  for (const auto& entry : material.imitation_entries) {
    const std::vector<MaterialNote>* leader = fragmentNotes(entry.leader_fragment);
    const std::vector<MaterialNote>* follower = fragmentNotes(entry.follower_fragment);
    if (leader->empty() || follower->empty())
      continue;
    const Tick expected_follower_tick = leader->front().start_tick + entry.distance_ticks;
    const int expected_follower_pitch =
        static_cast<int>(leader->front().pitch) + entry.interval_semis;
    const bool tick_ok = follower->front().start_tick == expected_follower_tick;
    const bool pitch_ok =
        expected_follower_pitch >= 0 && expected_follower_pitch <= 127 &&
        follower->front().pitch == static_cast<std::uint8_t>(expected_follower_pitch);
    if (!tick_ok || !pitch_ok) {
      ValidationFailure failure;
      failure.rule_id = "imitation_entry_match";
      failure.kind = FailKind::MusicalFail;
      report.failures.push_back(failure);
    }
  }

  // P11 development-section rules.
  //
  // middle_entry_in_related_key: every MiddleEntryDecl must declare a
  //   related key (V / vi / IV / ii of the home tonic) AND every note
  //   pitch class must be diatonic to the major scale on that related
  //   key — i.e. the entry genuinely sits in the declared key, not just
  //   carries the label.
  // stretto_overlap_valid: the follower must enter strictly inside the
  //   leader's window (real time overlap) AND follower_notes[i].pitch
  //   must equal subject[i].pitch + interval_semis (the follower is the
  //   subject transposed by the declared interval).
  // pedal_point_tonic_or_dominant: every pedal pitch class must be the
  //   home tonic or dominant.
  const std::uint8_t home_tonic_pc = static_cast<std::uint8_t>(harmonic_plan.tonic_pc % 12);
  const std::uint8_t dominant_pc = static_cast<std::uint8_t>((home_tonic_pc + 7) % 12);
  const std::array<std::uint8_t, 4> related_key_pcs = {
      dominant_pc,                                          // V
      static_cast<std::uint8_t>((home_tonic_pc + 9) % 12),  // vi
      static_cast<std::uint8_t>((home_tonic_pc + 5) % 12),  // IV
      static_cast<std::uint8_t>((home_tonic_pc + 2) % 12),  // ii
  };
  for (const auto& entry : material.middle_entries) {
    const std::uint8_t key_pc = static_cast<std::uint8_t>(entry.related_key_pc % 12);
    bool key_ok = false;
    for (auto r : related_key_pcs) {
      if (key_pc == r)
        key_ok = true;
    }
    bool notes_ok = true;
    for (const auto& n : entry.notes) {
      if (!isDiatonicInKey(static_cast<std::uint8_t>(n.pitch % 12), key_pc, /*is_minor=*/false)) {
        notes_ok = false;
        break;
      }
    }
    if (!key_ok || !notes_ok) {
      ValidationFailure failure;
      failure.rule_id = "middle_entry_in_related_key";
      failure.kind = FailKind::MusicalFail;
      report.failures.push_back(failure);
    }
  }

  for (const auto& stretto : material.stretto_entries) {
    const bool overlap_ok =
        stretto.follower_entry_tick > stretto.leader_entry_tick &&
        stretto.follower_entry_tick < stretto.leader_entry_tick + stretto.leader_length_ticks;
    bool pitch_ok =
        !stretto.follower_notes.empty() && stretto.follower_notes.size() <= material.subject.size();
    if (pitch_ok) {
      for (std::size_t i = 0; i < stretto.follower_notes.size(); ++i) {
        const int expected = static_cast<int>(material.subject[i].pitch) + stretto.interval_semis;
        if (expected < 0 || expected > 127 ||
            stretto.follower_notes[i].pitch != static_cast<std::uint8_t>(expected)) {
          pitch_ok = false;
          break;
        }
      }
    }
    if (!overlap_ok || !pitch_ok) {
      ValidationFailure failure;
      failure.rule_id = "stretto_overlap_valid";
      failure.kind = FailKind::MusicalFail;
      report.failures.push_back(failure);
    }
  }

  for (const auto& pedal : material.pedal_points) {
    const std::uint8_t pc = static_cast<std::uint8_t>(pedal.pitch % 12);
    if (pc != home_tonic_pc && pc != dominant_pc) {
      ValidationFailure failure;
      failure.rule_id = "pedal_point_tonic_or_dominant";
      failure.kind = FailKind::MusicalFail;
      report.failures.push_back(failure);
    }
  }

  // P12 rhythm / meter / phrase rules.
  //
  // phrase_periodicity_4_or_8_bar: consecutive declared phrase starts must
  //   differ by exactly 4 or 8 bars (regular Baroque phrase grid). Skipped
  //   when fewer than two phrase starts are declared.
  // anacrusis_consistent: if the piece declares an anacrusis, the upbeat
  //   length must be a valid sub-bar value and every declared Anacrusis
  //   rhythm fragment must begin exactly anacrusis_ticks before some phrase
  //   start (the upbeat leads into a downbeat). If no anacrusis is declared
  //   there must be neither an anacrusis length nor an Anacrusis fragment.
  {
    const PhraseStructure& ps = material.phrase_structure;
    if (ps.phrase_start_ticks.size() >= 2) {
      const Tick four_bars = static_cast<Tick>(4) * kTicksPerBar;
      const Tick eight_bars = static_cast<Tick>(8) * kTicksPerBar;
      bool periodic = true;
      for (std::size_t i = 1; i < ps.phrase_start_ticks.size(); ++i) {
        const Tick len = ps.phrase_start_ticks[i] - ps.phrase_start_ticks[i - 1];
        if (len != four_bars && len != eight_bars) {
          periodic = false;
          break;
        }
      }
      if (!periodic) {
        ValidationFailure failure;
        failure.rule_id = "phrase_periodicity_4_or_8_bar";
        failure.kind = FailKind::MusicalFail;
        report.failures.push_back(failure);
      }
    }

    bool anacrusis_ok = true;
    if (ps.has_anacrusis) {
      anacrusis_ok = ps.anacrusis_ticks > 0 && ps.anacrusis_ticks < kTicksPerBar;
      if (anacrusis_ok) {
        for (const auto& frag : material.rhythm_fragments) {
          if (frag.feature != RhythmFragment::Feature::Anacrusis || frag.notes.empty())
            continue;
          const Tick pickup_start = frag.notes.front().start_tick;
          bool aligned = false;
          for (Tick s : ps.phrase_start_ticks) {
            if (s >= ps.anacrusis_ticks && s - ps.anacrusis_ticks == pickup_start) {
              aligned = true;
              break;
            }
          }
          if (!aligned) {
            anacrusis_ok = false;
            break;
          }
        }
      }
    } else {
      if (ps.anacrusis_ticks != 0) {
        anacrusis_ok = false;
      }
      for (const auto& frag : material.rhythm_fragments) {
        if (frag.feature == RhythmFragment::Feature::Anacrusis) {
          anacrusis_ok = false;
          break;
        }
      }
    }
    if (!anacrusis_ok) {
      ValidationFailure failure;
      failure.rule_id = "anacrusis_consistent";
      failure.kind = FailKind::MusicalFail;
      report.failures.push_back(failure);
    }
  }

  // P13 texture / instrument / expression rules.
  //
  // voice_range_integrity: every note whose voice has a declared MIDI range
  //   (Material::texture_plan.voice_ranges) must lie inside [lo, hi]. A note
  //   outside its voice's range fires the rule. Skipped when no ranges are
  //   declared (Phase 3-12 fixtures), so prior behavior is unchanged.
  // pedal_range_soft_penalty: the C1-D3 pedal compass (MIDI 24-50) is a soft
  //   target — notes inside it incur no penalty, notes just outside it incur
  //   a gradual penalty at scoring time (NOT a hard rejection, per the design
  //   invariant). This Validator guard only fires when a note in the declared
  //   pedal voice leaves the physically playable band [C0, D4] (MIDI 12-62),
  //   i.e. a pitch no pedalboard can sound. Skipped when no pedal voice is
  //   declared (pedal_voice == 0xFF).
  {
    const TexturePlan& tp = material.texture_plan;
    if (!tp.voice_ranges.empty()) {
      bool range_ok = true;
      for (const auto& note : notes) {
        for (const auto& range : tp.voice_ranges) {
          if (range.voice != note.voice)
            continue;
          if (note.pitch < range.lo || note.pitch > range.hi) {
            range_ok = false;
          }
          break;
        }
        if (!range_ok)
          break;
      }
      if (!range_ok) {
        ValidationFailure failure;
        failure.rule_id = "voice_range_integrity";
        failure.kind = FailKind::MusicalFail;
        report.failures.push_back(failure);
      }
    }

    if (tp.pedal_voice != 0xFF) {
      constexpr std::uint8_t kPedalHardLo = 12;  // C0: below this no pedalboard sounds.
      constexpr std::uint8_t kPedalHardHi = 62;  // D4: one octave above the D3 soft ceiling.
      bool pedal_ok = true;
      for (const auto& note : notes) {
        if (note.voice != tp.pedal_voice)
          continue;
        if (note.pitch < kPedalHardLo || note.pitch > kPedalHardHi) {
          pedal_ok = false;
          break;
        }
      }
      if (!pedal_ok) {
        ValidationFailure failure;
        failure.rule_id = "pedal_range_soft_penalty";
        failure.kind = FailKind::MusicalFail;
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
