#include "composer/validator.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>

#include "composer/chord_voicing.h"
#include "composer/motif_ops.h"
#include "composer/rule_helpers.h"
#include "composer/stream_segregation.h"
#include "core/pitch_utils.h"

namespace bach::composer {

namespace {

using rule_helpers::activeChord;
using rule_helpers::isCrossRelationPc;
using rule_helpers::isPerfectInterval;
using rule_helpers::isStructuralAccent;
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

// Organ Toccata. Self-contained (character, archetype) compatibility
// predicate for the toccata_archetype_compatible rule. This does NOT reuse the
// legacy isCharacterFormCompatible (which keys off the legacy FormType). The
// only forbidden pair is Noble x Dramaticus: the Dramaticus archetype is the
// prototypical dramatic toccata (BWV565-style free, virtuosic opening),
// antithetical to the dignified Noble affect (the Noble character must not
// take a dramatic toccata). The switch is exhaustive and extensible: add cases
// to forbid further pairs.
bool isToccataPairCompatible(SubjectCharacter character, ToccataArchetype archetype) {
  if (character == SubjectCharacter::Noble && archetype == ToccataArchetype::Dramaticus)
    return false;
  return true;
}

bool resolvesLeadingTone(std::uint8_t leading_pitch, std::uint8_t resolution_pitch,
                         const HarmonicPlan& plan) {
  return rule_helpers::isLeadingToneResolution(leading_pitch, static_cast<int>(resolution_pitch),
                                               plan);
}

// scalePcs / scaleIndex / isAugmentedMelodicInterval / isDiminishedMelodicInterval
// now live in rule_helpers (shared with the CandidateSearch melodic pre-filter so
// the two stay in lockstep). The call sites below use the rule_helpers:: versions.

bool isPerfectFifth(int semitones) {
  return std::abs(semitones) % 12 == 7;
}

class VoiceOnsetIndex {
 public:
  explicit VoiceOnsetIndex(const std::vector<NoteEvent>& notes) : notes_(notes), by_voice_(256) {
    std::array<std::size_t, 256> counts{};
    for (const NoteEvent& note : notes)
      ++counts[note.voice];
    for (std::size_t voice = 0; voice < by_voice_.size(); ++voice)
      by_voice_[voice].reserve(counts[voice]);
    for (std::size_t index = 0; index < notes.size(); ++index)
      by_voice_[notes[index].voice].push_back(index);
    for (auto& indices : by_voice_) {
      std::stable_sort(indices.begin(), indices.end(), [&](std::size_t left, std::size_t right) {
        return notes_[left].start_tick < notes_[right].start_tick;
      });
    }
  }

  std::size_t startingAt(VoiceId voice, Tick tick) const {
    const auto& indices = by_voice_[voice];
    const auto it = std::lower_bound(
        indices.begin(), indices.end(), tick,
        [&](std::size_t index, Tick value) { return notes_[index].start_tick < value; });
    return it != indices.end() && notes_[*it].start_tick == tick ? *it : notes_.size();
  }

  std::size_t soundingAt(VoiceId voice, Tick tick) const {
    const auto& indices = by_voice_[voice];
    auto it = std::upper_bound(
        indices.begin(), indices.end(), tick,
        [&](Tick value, std::size_t index) { return value < notes_[index].start_tick; });
    while (it != indices.begin()) {
      --it;
      const NoteEvent& note = notes_[*it];
      if (note.start_tick <= tick && tick < note.start_tick + note.duration)
        return *it;
    }
    return notes_.size();
  }

  std::uint8_t pitchAt(VoiceId voice, Tick tick) const {
    const std::size_t index = soundingAt(voice, tick);
    return index < notes_.size() ? notes_[index].pitch : 0;
  }

 private:
  const std::vector<NoteEvent>& notes_;
  std::vector<std::vector<std::size_t>> by_voice_;
};

std::uint8_t lowestSoundingPitch(const VoiceOnsetIndex& onset_index,
                                 const std::vector<VoiceId>& voices, Tick tick) {
  std::uint8_t lowest = 0;
  for (VoiceId voice : voices) {
    const std::uint8_t pitch = onset_index.pitchAt(voice, tick);
    if (pitch != 0 && (lowest == 0 || pitch < lowest))
      lowest = pitch;
  }
  return lowest;
}

bool isDeclaredSuspensionDissonance(const VoiceOnsetIndex& onset_index,
                                    const std::vector<VoiceId>& voices, const Material& material,
                                    VoiceId voice_a, VoiceId voice_b, Tick tick) {
  for (const auto& sp : material.suspension_patterns) {
    if ((sp.voice != voice_a && sp.voice != voice_b) || sp.suspension_tick != tick ||
        sp.preparation_tick >= sp.suspension_tick || sp.resolution_tick <= sp.suspension_tick) {
      continue;
    }
    const VoiceId other_voice = sp.voice == voice_a ? voice_b : voice_a;
    const std::uint8_t prep = onset_index.pitchAt(sp.voice, sp.preparation_tick);
    const std::uint8_t sus = onset_index.pitchAt(sp.voice, sp.suspension_tick);
    const std::uint8_t res = onset_index.pitchAt(sp.voice, sp.resolution_tick);
    const std::uint8_t other_sus = onset_index.pitchAt(other_voice, sp.suspension_tick);
    const std::uint8_t other_res = onset_index.pitchAt(other_voice, sp.resolution_tick);
    if (prep != sp.preparation_pitch || sus != sp.suspension_pitch || res != sp.resolution_pitch ||
        prep != sus || other_sus == 0 || other_res == 0)
      continue;
    const int resolution_motion = static_cast<int>(res) - static_cast<int>(sus);
    const bool ascending = sp.type == SuspensionType::Sus2_3;
    if ((!ascending && resolution_motion != -1 && resolution_motion != -2) ||
        (ascending && resolution_motion != 1 && resolution_motion != 2))
      continue;
    int sus_ic = 0;
    int res_ic = 0;
    if (ascending) {
      sus_ic = ((static_cast<int>(other_sus) - static_cast<int>(sus)) % 12 + 12) % 12;
      res_ic = ((static_cast<int>(other_res) - static_cast<int>(res)) % 12 + 12) % 12;
    } else {
      // Upper-voice suspensions are dissonant specifically against the lowest
      // sounding other voice, not against an incidental upper partner.
      std::uint8_t lowest_other = 0;
      for (VoiceId voice : voices) {
        if (voice == sp.voice)
          continue;
        const std::uint8_t pitch = onset_index.pitchAt(voice, tick);
        if (pitch != 0 && (lowest_other == 0 || pitch < lowest_other))
          lowest_other = pitch;
      }
      if (other_sus != lowest_other)
        continue;
      sus_ic = ((static_cast<int>(sus) - static_cast<int>(other_sus)) % 12 + 12) % 12;
      res_ic = ((static_cast<int>(res) - static_cast<int>(other_res)) % 12 + 12) % 12;
    }
    switch (sp.type) {
      case SuspensionType::Sus4_3:
        if (sus_ic == 5 && (res_ic == 3 || res_ic == 4))
          return true;
        break;
      case SuspensionType::Sus7_6:
        if ((sus_ic == 10 || sus_ic == 11) && (res_ic == 8 || res_ic == 9))
          return true;
        break;
      case SuspensionType::Sus9_8:
        if ((sus_ic == 1 || sus_ic == 2) && res_ic == 0)
          return true;
        break;
      case SuspensionType::Sus2_3:
        if ((sus_ic == 1 || sus_ic == 2) && (res_ic == 3 || res_ic == 4))
          return true;
        break;
    }
  }
  return false;
}

bool isDeclaredCadentialSixFour(const VoiceOnsetIndex& onset_index,
                                const std::vector<VoiceId>& voices, const HarmonicPlan& plan,
                                VoiceId upper_voice, Tick tick) {
  if (plan.chords.empty())
    return false;
  for (const auto& six_four : plan.cadential_six_fours) {
    if (six_four.type != SixFourType::Cadential || six_four.tick != tick ||
        six_four.resolution_tick <= six_four.tick)
      continue;
    const std::uint8_t bass = lowestSoundingPitch(onset_index, voices, tick);
    const std::uint8_t resolution_bass =
        lowestSoundingPitch(onset_index, voices, six_four.resolution_tick);
    const std::uint8_t upper = onset_index.pitchAt(upper_voice, tick);
    const std::uint8_t resolution = onset_index.pitchAt(upper_voice, six_four.resolution_tick);
    if (bass == 0 || resolution_bass == 0 || upper == 0 || resolution == 0)
      continue;
    const std::uint8_t dominant = static_cast<std::uint8_t>((plan.tonic_pc + 7) % 12);
    if (pitchClass(bass) != dominant || pitchClass(resolution_bass) != dominant ||
        pitchClass(upper) != plan.tonic_pc % 12 ||
        activeChord(plan, six_four.resolution_tick).root_pc % 12 != dominant)
      continue;
    const int motion = static_cast<int>(resolution) - static_cast<int>(upper);
    const int resolution_class =
        (static_cast<int>(resolution) - static_cast<int>(resolution_bass)) % 12;
    const std::uint8_t leading_tone = static_cast<std::uint8_t>((plan.tonic_pc + 11) % 12);
    if (motion == -1 && resolution_class == 4 && pitchClass(resolution) == leading_tone) {
      return true;
    }
  }
  return false;
}

bool isTonicTriadPc(std::uint8_t pc, const HarmonicPlan& plan, Tick tick) {
  const std::uint8_t tonic = static_cast<std::uint8_t>(plan.tonic_pc % 12);
  const bool picardy = plan.is_minor && activeChord(plan, tick).is_picardy;
  const std::uint8_t third =
      static_cast<std::uint8_t>((tonic + (plan.is_minor && !picardy ? 3 : 4)) % 12);
  const std::uint8_t fifth = static_cast<std::uint8_t>((tonic + 7) % 12);
  return pc == tonic || pc == third || pc == fifth;
}

bool hasRuleBit(const std::vector<NoteProvenance>& provenance, std::size_t index, RuleBit bit) {
  if (index >= provenance.size())
    return false;
  return (provenance[index].satisfied_rules & (ruleBitMask(bit))) != 0;
}

// Final-score contextual exemptions are never inferred from NoteSource alone.
// A fixed carrier must still equal the independent declaration stamped before
// ornamentation; an ornament must remain inside that declared note, stay in
// its close diatonic-neighbour range, and carry the OrnamentRealized bit.
// A mutated note therefore loses the exemption even though its source/intent
// fields are unchanged.
bool matchesAuthoredContext(const NoteEvent& note, const NoteProvenance& provenance) {
  // A scored-search note is authored by the composer at the point it is
  // emitted.  Unlike a Material carrier it has no immutable declaration to
  // replay, so final-score findings involving only such authored choices are
  // diagnostic evidence rather than a carrier-integrity failure.
  if (provenance.source == NoteSource::Compose)
    return true;
  if (!provenance.has_authored_note)
    return false;
  if (provenance.source == NoteSource::Material) {
    return note.start_tick == provenance.authored_start_tick &&
           note.duration == provenance.authored_duration && note.pitch == provenance.authored_pitch;
  }
  if (provenance.source != NoteSource::Ornament ||
      !(provenance.satisfied_rules & ruleBitMask(RuleBit::OrnamentRealized))) {
    return false;
  }
  const Tick authored_end = provenance.authored_start_tick + provenance.authored_duration;
  const Tick note_end = note.start_tick + note.duration;
  return provenance.authored_duration > 0 && note.duration > 0 &&
         note.start_tick >= provenance.authored_start_tick && note_end <= authored_end &&
         std::abs(static_cast<int>(note.pitch) - static_cast<int>(provenance.authored_pitch)) <= 4;
}

SubjectFeatures computeSubjectFeatures(const std::vector<MaterialNote>& subject) {
  SubjectFeatures features;
  features.length = static_cast<int>(subject.size());
  if (subject.empty())
    return features;

  int lo = std::numeric_limits<int>::max();
  int hi = std::numeric_limits<int>::min();
  std::array<bool, 12> pcs{};
  std::array<bool, 25> intervals{};  // absolute semitones, clamped to 24.
  for (const auto& note : subject) {
    const int pitch = static_cast<int>(note.pitch);
    lo = std::min(lo, pitch);
    hi = std::max(hi, pitch);
    pcs[static_cast<std::size_t>(pitch % 12)] = true;
  }
  features.range_semitones = hi - lo;
  features.unique_pitch_classes = static_cast<int>(std::count(pcs.begin(), pcs.end(), true));

  for (std::size_t i = 1; i < subject.size(); ++i) {
    const int interval =
        std::abs(static_cast<int>(subject[i].pitch) - static_cast<int>(subject[i - 1].pitch));
    if (i == 1)
      features.opening_interval = interval;
    features.max_leap = std::max(features.max_leap, interval);
    intervals[static_cast<std::size_t>(std::min(interval, 24))] = true;
  }
  features.unique_intervals =
      static_cast<int>(std::count(intervals.begin(), intervals.end(), true));
  return features;
}

}  // namespace

TextureMetrics computeTextureMetrics(const std::vector<NoteEvent>& notes) {
  TextureMetrics metrics;
  if (notes.empty()) {
    return metrics;
  }

  std::vector<Tick> boundaries;
  boundaries.reserve(notes.size() * 2);
  std::vector<VoiceId> voices;
  for (const auto& note : notes) {
    boundaries.push_back(note.start_tick);
    boundaries.push_back(note.start_tick + note.duration);
    if (std::find(voices.begin(), voices.end(), note.voice) == voices.end()) {
      voices.push_back(note.voice);
    }
    // Broad organ compass for fugue-family diagnostics: pedal C1 through
    // manual C6. This catches D7-style escapes without classifying playable
    // bass-register material as a violation.
    if (note.pitch < 24 || note.pitch > 84) {
      ++metrics.compass_violation_count;
    }
  }
  std::sort(voices.begin(), voices.end());
  std::sort(boundaries.begin(), boundaries.end());
  boundaries.erase(std::unique(boundaries.begin(), boundaries.end()), boundaries.end());

  long active_voice_ticks = 0;
  long mono_ticks = 0;
  long total_ticks = 0;
  for (std::size_t i = 0; i + 1 < boundaries.size(); ++i) {
    const Tick begin = boundaries[i];
    const Tick end = boundaries[i + 1];
    if (end <= begin) {
      continue;
    }
    int active = 0;
    for (VoiceId voice : voices) {
      const bool sounding = std::any_of(notes.begin(), notes.end(), [&](const NoteEvent& note) {
        return note.voice == voice && note.start_tick < end &&
               begin < note.start_tick + note.duration;
      });
      if (sounding) {
        ++active;
      }
    }
    metrics.max_active_voices = std::max(metrics.max_active_voices, active);
    const Tick span = end - begin;
    active_voice_ticks += static_cast<long>(active) * static_cast<long>(span);
    if (active == 1) {
      mono_ticks += static_cast<long>(span);
    }
    total_ticks += static_cast<long>(span);
  }
  metrics.avg_active_voices =
      total_ticks > 0 ? static_cast<double>(active_voice_ticks) / static_cast<double>(total_ticks)
                      : 0.0;
  metrics.mono_ratio =
      total_ticks > 0 ? static_cast<double>(mono_ticks) / static_cast<double>(total_ticks) : 0.0;

  metrics.voices.reserve(voices.size());
  for (VoiceId voice : voices) {
    std::vector<NoteEvent> voice_notes;
    for (const auto& note : notes) {
      if (note.voice == voice) {
        voice_notes.push_back(note);
      }
    }
    std::sort(voice_notes.begin(), voice_notes.end(), [](const NoteEvent& a, const NoteEvent& b) {
      if (a.start_tick != b.start_tick) {
        return a.start_tick < b.start_tick;
      }
      return a.pitch < b.pitch;
    });

    VoiceTextureMetrics vm;
    vm.voice = voice;
    vm.max_repeated_run = voice_notes.empty() ? 0 : 1;
    vm.min_pitch = std::numeric_limits<int>::max();
    vm.max_pitch = std::numeric_limits<int>::min();
    Tick voice_first = std::numeric_limits<Tick>::max();
    Tick voice_last = 0;
    long sounding_ticks = 0;
    int current_run = 0;
    int previous_pitch = -1;
    for (const auto& note : voice_notes) {
      const int pitch = static_cast<int>(note.pitch);
      vm.min_pitch = std::min(vm.min_pitch, pitch);
      vm.max_pitch = std::max(vm.max_pitch, pitch);
      voice_first = std::min(voice_first, note.start_tick);
      voice_last = std::max(voice_last, note.start_tick + note.duration);
      sounding_ticks += static_cast<long>(note.duration);
      current_run = (pitch == previous_pitch) ? current_run + 1 : 1;
      vm.max_repeated_run = std::max(vm.max_repeated_run, current_run);
      previous_pitch = pitch;
    }
    const long activity_window =
        voice_last > voice_first ? static_cast<long>(voice_last - voice_first) : 0;
    vm.silence_ratio = activity_window > 0
                           ? std::clamp(1.0 - (static_cast<double>(sounding_ticks) /
                                               static_cast<double>(activity_window)),
                                        0.0, 1.0)
                           : 0.0;
    if (voice_notes.empty()) {
      vm.min_pitch = 0;
      vm.max_pitch = 0;
    }
    metrics.voices.push_back(vm);
  }

  double overlap_sum = 0.0;
  int pair_count = 0;
  for (std::size_t i = 0; i < metrics.voices.size(); ++i) {
    for (std::size_t j = i + 1; j < metrics.voices.size(); ++j) {
      const int overlap_lo = std::max(metrics.voices[i].min_pitch, metrics.voices[j].min_pitch);
      const int overlap_hi = std::min(metrics.voices[i].max_pitch, metrics.voices[j].max_pitch);
      const int union_lo = std::min(metrics.voices[i].min_pitch, metrics.voices[j].min_pitch);
      const int union_hi = std::max(metrics.voices[i].max_pitch, metrics.voices[j].max_pitch);
      const int overlap = std::max(0, overlap_hi - overlap_lo + 1);
      const int range_union = std::max(1, union_hi - union_lo + 1);
      overlap_sum += static_cast<double>(overlap) / static_cast<double>(range_union);
      ++pair_count;
    }
  }
  metrics.register_overlap_ratio =
      pair_count > 0 ? overlap_sum / static_cast<double>(pair_count) : 0.0;
  return metrics;
}

ValidationReport Validator::validate(const std::vector<NoteEvent>& notes,
                                     const std::vector<NoteProvenance>& provenance,
                                     const HarmonicPlan& harmonic_plan, const Material& material,
                                     ValidationScope scope) const {
  ValidationReport report;
  const bool audit_final_score = scope == ValidationScope::FinalScore;
  if (audit_final_score && notes.size() != provenance.size()) {
    ValidationFailure failure;
    failure.rule_id = "note_provenance_alignment";
    failure.kind = FailKind::StructuralFail;
    report.failures.push_back(failure);
    report.status = ValidationStatus::FailedSpan;
    return report;
  }
  std::vector<bool> authored_context(notes.size(), false);
  if (audit_final_score) {
    for (std::size_t i = 0; i < notes.size() && i < provenance.size(); ++i) {
      authored_context[i] = matchesAuthoredContext(notes[i], provenance[i]);
      if (provenance[i].has_authored_note && !authored_context[i]) {
        ValidationFailure failure;
        failure.span_id = provenance[i].span_id;
        failure.rule_id = provenance[i].source == NoteSource::Ornament
                              ? "ornament_declaration_integrity"
                              : "carrier_declaration_integrity";
        failure.kind = FailKind::StructuralFail;
        report.failures.push_back(failure);
      }
    }
    // Validate each ornament expansion as a complete temporal replacement of
    // one authored carrier.  Per-note bounds alone would miss a deleted middle
    // subdivision or a gap, so group by the immutable authored-note identity
    // and require exact, contiguous coverage ending on the main tone.
    for (std::size_t i = 0; i < notes.size() && i < provenance.size(); ++i) {
      const auto& p = provenance[i];
      if (p.source != NoteSource::Ornament || !p.has_authored_note)
        continue;
      const auto same_group = [&](std::size_t j) {
        return j < provenance.size() && provenance[j].source == NoteSource::Ornament &&
               provenance[j].has_authored_note && notes[j].voice == notes[i].voice &&
               provenance[j].span_id == p.span_id && provenance[j].voice_intent == p.voice_intent &&
               provenance[j].authored_start_tick == p.authored_start_tick &&
               provenance[j].authored_duration == p.authored_duration &&
               provenance[j].authored_pitch == p.authored_pitch;
      };
      bool already_checked = false;
      for (std::size_t j = 0; j < i; ++j) {
        if (same_group(j)) {
          already_checked = true;
          break;
        }
      }
      if (already_checked)
        continue;
      std::vector<std::size_t> group;
      for (std::size_t j = i; j < notes.size(); ++j) {
        if (same_group(j))
          group.push_back(j);
      }
      std::sort(group.begin(), group.end(), [&](std::size_t a, std::size_t b) {
        return notes[a].start_tick < notes[b].start_tick;
      });
      Tick cursor = p.authored_start_tick;
      bool group_ok = group.size() >= 2;
      for (std::size_t index : group) {
        if (!authored_context[index] || notes[index].start_tick != cursor) {
          group_ok = false;
          break;
        }
        cursor = notes[index].start_tick + notes[index].duration;
      }
      group_ok = group_ok && cursor == p.authored_start_tick + p.authored_duration &&
                 notes[group.back()].pitch == p.authored_pitch;
      if (!group_ok) {
        ValidationFailure failure;
        failure.span_id = p.span_id;
        failure.rule_id = "ornament_group_integrity";
        failure.kind = FailKind::StructuralFail;
        report.failures.push_back(failure);
      }
    }
  }
  // Final-score audits always evaluate fixed authored material. A violation
  // owned entirely by immutable declarations is diagnostic information (the
  // composer cannot repair it in this pass); any generated/ornamented side
  // remains a blocking failure with an actionable span.
  const auto recordCounterpointFinding = [&](const ValidationFailure& finding,
                                             std::initializer_list<std::size_t> indices) {
    bool all_fixed = true;
    bool all_authored = audit_final_score;
    for (const std::size_t index : indices) {
      all_fixed = all_fixed && index < provenance.size() &&
                  (provenance[index].source == NoteSource::Material ||
                   provenance[index].source == NoteSource::Ornament);
      all_authored = all_authored && index < authored_context.size() && authored_context[index];
    }
    // Candidate generation cannot repair an immutable carrier pair. Keep that
    // exemption confined to Generation; FinalScore still evaluates the rule
    // and records fully authored findings as informational evidence.
    if (!audit_final_score && all_fixed) {
      return;
    }
    if (all_authored) {
      report.informational.push_back(finding);
    } else {
      report.failures.push_back(finding);
    }
  };
  if (!notes.empty()) {
    report.texture_metrics.push_back(computeTextureMetrics(notes));
  }
  if (!material.subject.empty()) {
    const std::size_t canonical_count =
        material.canonical_subject_note_count == 0
            ? material.subject.size()
            : std::min(material.canonical_subject_note_count, material.subject.size());
    report.subject_features.push_back(computeSubjectFeatures(std::vector<MaterialNote>(
        material.subject.begin(), material.subject.begin() + canonical_count)));
  }
  if (!material.arpeggio_template.notes.empty()) {
    StreamSegregationSpan stream_info =
        stream_segregation::analyzeSpan(material.arpeggio_template.notes, kInvalidSpanId);
    const int group_size = material.arpeggio_template.group_size;
    if (group_size >= 2) {
      stream_info.cell_count = static_cast<int>(material.arpeggio_template.notes.size() /
                                                static_cast<std::size_t>(group_size));
      stream_info.cell_based_stream_count = stream_info.cell_count >= 2 ? 2 : 1;
    }
    stream_info.disagrees_with_cell_counterpoint =
        stream_info.detected_stream_count != stream_info.cell_based_stream_count;
    report.stream_segregation.push_back(stream_info);
  }

  // Meter-derived bar length for every bar-position / bar-index rule below.
  // Defaults to 1920 (4/4) when the plan carries the default time signature,
  // so existing 4/4 fixtures validate byte-identically; a 3/4 plan yields 1440.
  const Tick ticks_per_bar = harmonic_plan.ticksPerBar();

  // 1. Strong-beat dissonance. Generation validation checks only search-owned
  //    notes; final-score validation checks every sounding source.
  for (std::size_t i = 0; i < notes.size(); ++i) {
    const auto& note = notes[i];
    if (i >= provenance.size())
      continue;
    if (!audit_final_score && provenance[i].source != NoteSource::Compose)
      continue;
    if (!isStructuralAccent(harmonic_plan, note.start_tick))
      continue;

    const auto& chord = activeChord(harmonic_plan, note.start_tick);
    const auto triad = triadFor(chord);
    const std::uint8_t pc = static_cast<std::uint8_t>(note.pitch % 12);
    if (pc != triad[0] && pc != triad[1] && pc != triad[2]) {
      ValidationFailure failure;
      failure.span_id = provenance[i].span_id;
      failure.rule_id = "strong_beat_dissonance";
      recordCounterpointFinding(failure, {i});
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
  const VoiceOnsetIndex onset_index(notes);

  // P3 cadence voice leading. CadenceEvent marks a required two-note
  // formula at tick-kTicksPerBeat -> tick. The upper line is the
  // lowest-indexed voice actually sounding at the cadence (a decorative
  // voice may already have dropped out); the bass line is the
  // highest-indexed sounding voice. A monophonic form is checked as a
  // melodic cadence instead of being rejected for not having a bass voice.
  for (const auto& cadence : harmonic_plan.cadences) {
    if (voices.empty() || cadence.tick < kTicksPerBeat) {
      ValidationFailure failure;
      failure.rule_id = "cadence_voice_leading";
      failure.kind = FailKind::StructuralFail;
      report.failures.push_back(failure);
      continue;
    }
    const Tick approach_tick = cadence.tick - kTicksPerBeat;
    // The upper cadential approach may be a diminuted run inside the final
    // beat.  Sample the pitch sounding immediately before the cadence rather
    // than the beat's first subdivision; the bass formula remains anchored to
    // the preceding beat so V -> I root motion is still measured structurally.
    const Tick upper_approach_tick = cadence.tick - 1;
    VoiceId upper_voice = voices.front();
    bool found_upper = false;
    for (VoiceId voice : voices) {
      if (onset_index.pitchAt(voice, cadence.tick) != 0) {
        upper_voice = voice;
        found_upper = true;
        break;
      }
    }
    if (!found_upper) {
      ValidationFailure failure;
      failure.rule_id = "cadence_voice_leading";
      failure.kind = FailKind::StructuralFail;
      report.failures.push_back(failure);
      continue;
    }
    VoiceId bass_voice = upper_voice;
    for (auto it = voices.rbegin(); it != voices.rend(); ++it) {
      if (*it == upper_voice)
        continue;
      if (onset_index.pitchAt(*it, cadence.tick) != 0) {
        bass_voice = *it;
        break;
      }
    }
    const std::uint8_t upper_prev = onset_index.pitchAt(upper_voice, upper_approach_tick);
    const std::uint8_t upper_now = onset_index.pitchAt(upper_voice, cadence.tick);
    const bool monophonic = bass_voice == upper_voice;
    const std::uint8_t bass_prev =
        monophonic ? upper_prev : onset_index.pitchAt(bass_voice, approach_tick);
    const std::uint8_t bass_now =
        monophonic ? upper_now : onset_index.pitchAt(bass_voice, cadence.tick);
    const std::uint8_t tonic = static_cast<std::uint8_t>(harmonic_plan.tonic_pc % 12);
    const std::uint8_t dominant = static_cast<std::uint8_t>((tonic + 7) % 12);
    const std::uint8_t subdominant = static_cast<std::uint8_t>((tonic + 5) % 12);
    const std::uint8_t submediant = static_cast<std::uint8_t>((tonic + 9) % 12);
    const std::uint8_t minor_submediant = static_cast<std::uint8_t>((tonic + 8) % 12);
    const ChordEvent& approach_chord = activeChord(harmonic_plan, approach_tick);
    const std::uint8_t authentic_bass_pc =
        approach_chord.root_pc % 12 == dominant ? bassPitchClassFor(approach_chord) : dominant;
    // A half cadence is an arrival condition: the bass must land on V, but a
    // rhetorical rest before that strike is valid and common at sectional
    // boundaries.  The other cadence kinds describe motion between two
    // sonorities and therefore still require both approach pitches.
    bool ok = upper_now != 0 && bass_now != 0;
    if (cadence.type != CadenceType::Half)
      ok = ok && upper_prev != 0 && bass_prev != 0;
    if (ok && monophonic) {
      switch (cadence.type) {
        case CadenceType::Perfect:
        case CadenceType::PicardyThird:
          ok = resolvesLeadingTone(upper_prev, upper_now, harmonic_plan);
          break;
        case CadenceType::ImperfectAuthentic:
          ok = isTonicTriadPc(pitchClass(upper_now), harmonic_plan, cadence.tick);
          break;
        case CadenceType::Plagal:
          ok = pitchClass(upper_prev) == subdominant && pitchClass(upper_now) == tonic;
          break;
        case CadenceType::Half:
          ok = pitchClass(upper_now) == dominant;
          break;
        case CadenceType::Deceptive:
          ok = pitchClass(upper_prev) == dominant &&
               (pitchClass(upper_now) == submediant || pitchClass(upper_now) == minor_submediant);
          break;
        case CadenceType::Phrygian:
          ok = pitchClass(upper_prev) == minor_submediant && pitchClass(upper_now) == dominant &&
               upper_now < upper_prev &&
               std::abs(static_cast<int>(upper_now) - static_cast<int>(upper_prev)) <= 2;
          break;
      }
    } else if (ok) {
      switch (cadence.type) {
        case CadenceType::Perfect:
          ok = resolvesLeadingTone(upper_prev, upper_now, harmonic_plan) &&
               pitchClass(bass_prev) == authentic_bass_pc && pitchClass(bass_now) == tonic;
          break;
        case CadenceType::ImperfectAuthentic:
          ok = pitchClass(bass_prev) == authentic_bass_pc && pitchClass(bass_now) == tonic &&
               isTonicTriadPc(pitchClass(upper_now), harmonic_plan, cadence.tick);
          break;
        case CadenceType::PicardyThird:
          ok = resolvesLeadingTone(upper_prev, upper_now, harmonic_plan) &&
               pitchClass(bass_prev) == authentic_bass_pc && pitchClass(bass_now) == tonic &&
               pitchClass(upper_now) == static_cast<std::uint8_t>((tonic + 4) % 12);
          break;
        case CadenceType::Plagal:
          ok = pitchClass(bass_prev) == subdominant && pitchClass(bass_now) == tonic &&
               isTonicTriadPc(pitchClass(upper_now), harmonic_plan, cadence.tick);
          break;
        case CadenceType::Half:
          ok = pitchClass(bass_now) == dominant;
          break;
        case CadenceType::Deceptive:
          ok = pitchClass(bass_prev) == authentic_bass_pc &&
               pitchClass(bass_now) == (harmonic_plan.is_minor ? minor_submediant : submediant);
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
      // A reachable cadence whose voices move against the formula is a
      // harmony-rule violation (MusicalFail); only the malformed-layout
      // paths above are StructuralFail. Set explicitly for consistency
      // with every other musical rule instead of relying on the default.
      failure.kind = FailKind::MusicalFail;
      report.failures.push_back(failure);
    }
  }

  for (std::size_t va = 0; va < voices.size(); ++va) {
    for (std::size_t vb = va + 1; vb < voices.size(); ++vb) {
      int prev_interval = INT32_MIN;
      std::uint8_t prev_pa = 0;
      std::uint8_t prev_pb = 0;
      for (Tick t : ticks) {
        std::uint8_t pa = onset_index.pitchAt(voices[va], t);
        std::uint8_t pb = onset_index.pitchAt(voices[vb], t);
        if (pa == 0 || pb == 0) {
          prev_interval = INT32_MIN;
          continue;
        }
        int interval = static_cast<int>(pa) - static_cast<int>(pb);

        // Voice crossing: by convention, lower voice index = higher
        // pitch (voice 0 = soprano). A negative interval means the
        // upper-indexed voice has risen above the lower-indexed voice.
        if (interval < 0) {
          const std::size_t upper_index = onset_index.soundingAt(voices[va], t);
          const std::size_t lower_index = onset_index.soundingAt(voices[vb], t);
          const bool is_trio_upper_pair =
              voices[va] == 0 && voices[vb] == 1 && upper_index < provenance.size() &&
              lower_index < provenance.size() &&
              provenance[upper_index].voice_intent == VoiceIntent::TrioVoiceCarrier &&
              provenance[lower_index].voice_intent == VoiceIntent::TrioVoiceCarrier;
          // A momentary exchange is one union onset only. If the inversion is
          // still present at the next onset, prev_interval is negative and the
          // regular failure path below rejects it as a sustained crossing.
          const bool allow_momentary_trio_exchange =
              harmonic_plan.voice_crossing_policy == VoiceCrossingPolicy::AllowTrioUpperMomentary &&
              is_trio_upper_pair && prev_interval >= 0;
          if (!allow_momentary_trio_exchange) {
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
        }

        const PerfectMotionKind perfect_motion =
            prev_pa != 0 && prev_pb != 0 ? classifyPerfectMotion(prev_pa, pa, prev_pb, pb)
                                         : PerfectMotionKind::None;
        const bool strict_parallel = perfect_motion == PerfectMotionKind::ParallelFifth ||
                                     perfect_motion == PerfectMotionKind::ParallelOctave;
        const std::size_t current_lower_index = onset_index.soundingAt(voices[vb], t);
        const std::size_t current_upper_index = onset_index.soundingAt(voices[va], t);
        const bool current_is_cadence =
            hasRuleBit(provenance, current_lower_index, RuleBit::CadenceCellCommitted);
        // Material and Ornament sources are both fixed inputs from the
        // composer's point of view: Material is replayed verbatim, and
        // Ornament sub-notes are post-pass decoration of a Material tone (the
        // pass runs after validation; a re-run must not turn a suppressed
        // Material parallel into a failure just because the tone now carries
        // trill sub-notes).
        auto is_fixed_source = [&](std::size_t idx) {
          return idx < provenance.size() && (provenance[idx].source == NoteSource::Material ||
                                             provenance[idx].source == NoteSource::Ornament);
        };
        const bool current_is_material = is_fixed_source(current_lower_index);
        const bool upper_is_material = is_fixed_source(current_upper_index);
        // Suppress the parallel only when BOTH voices are fixed sources: the
        // composer cannot edit fixed inputs (mirrors the P10 invertible and
        // vertical-dissonance both_material gates). When a Compose upper
        // voice runs parallels against a Material lower voice (or vice
        // versa) the violation is real and composer-fixable, so it fires.
        const bool both_material = upper_is_material && current_is_material;
        const bool parallel_context_exempt = !audit_final_score && both_material;
        if (strict_parallel && !current_is_cadence && !parallel_context_exempt) {
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
          failure.rule_id = perfect_motion == PerfectMotionKind::ParallelFifth ? "parallel_fifth"
                                                                               : "parallel_octave";
          recordCounterpointFinding(failure, {current_lower_index, current_upper_index});
        }
        const bool hidden_parallel = perfect_motion == PerfectMotionKind::HiddenFifth ||
                                     perfect_motion == PerfectMotionKind::HiddenOctave;
        if (hidden_parallel && !current_is_cadence && !parallel_context_exempt) {
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
          failure.rule_id = perfect_motion == PerfectMotionKind::HiddenFifth
                                ? "hidden_parallel_fifth"
                                : "hidden_parallel_octave";
          recordCounterpointFinding(failure, {current_lower_index, current_upper_index});
        }
        prev_interval = interval;
        prev_pa = pa;
        prev_pb = pb;
      }
    }
  }

  // A trio-sonata policy is meaningful only when its two manual voices share
  // some tessitura. Without this guard a form could opt into momentary
  // crossings while retaining disjoint, artificial shelves forever. The pedal
  // is deliberately excluded: its low register remains a structural anchor.
  if (harmonic_plan.voice_crossing_policy == VoiceCrossingPolicy::AllowTrioUpperMomentary) {
    int v0_low = INT32_MAX;
    int v0_high = INT32_MIN;
    int v1_low = INT32_MAX;
    int v1_high = INT32_MIN;
    for (const NoteEvent& note : notes) {
      if (note.voice == 0) {
        v0_low = std::min(v0_low, static_cast<int>(note.pitch));
        v0_high = std::max(v0_high, static_cast<int>(note.pitch));
      } else if (note.voice == 1) {
        v1_low = std::min(v1_low, static_cast<int>(note.pitch));
        v1_high = std::max(v1_high, static_cast<int>(note.pitch));
      }
    }
    if (v0_low != INT32_MAX && v1_low != INT32_MAX &&
        std::max(v0_low, v1_low) > std::min(v0_high, v1_high)) {
      ValidationFailure failure;
      failure.rule_id = "trio_upper_register_overlap";
      report.failures.push_back(failure);
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
      // Both Material; nothing the composer can do (mirrors the
      // parallel/hidden-parallel/vertical-dissonance both_material gate).
      // notes and provenance are index-aligned, so i,j index provenance.
      // A Compose-vs-Material or Compose-vs-Compose cross relation still
      // fires; only the uncontrollable fixed-vs-fixed pair is skipped.
      const bool generation_fixed_pair = !audit_final_score && i < provenance.size() &&
                                         j < provenance.size() &&
                                         provenance[i].source == NoteSource::Material &&
                                         provenance[j].source == NoteSource::Material;
      if (generation_fixed_pair) {
        continue;
      }
      SpanId fail_span = kInvalidSpanId;
      if (j < provenance.size()) {
        fail_span = provenance[j].span_id;
      }
      ValidationFailure failure;
      failure.span_id = fail_span;
      failure.rule_id = "cross_relation";
      recordCounterpointFinding(failure, {i, j});
    }
  }

  // 5. Vertical dissonance on structural accents. Perfect fourths are
  //    classified against the actual lowest sounding pitch: a fourth above
  //    the bass is dissonant unless a fully realized 4-3 suspension or
  //    cadential 6/4 declaration supplies its preparation and resolution.
  //    A fourth between upper voices remains valid when both notes are
  //    consonant above the bass.
  for (std::size_t va = 0; va < voices.size(); ++va) {
    for (std::size_t vb = va + 1; vb < voices.size(); ++vb) {
      for (Tick t : ticks) {
        if (!isStructuralAccent(harmonic_plan, t))
          continue;
        const std::uint8_t pa = onset_index.pitchAt(voices[va], t);
        const std::uint8_t pb = onset_index.pitchAt(voices[vb], t);
        if (pa == 0 || pb == 0)
          continue;
        const std::size_t authored_a = onset_index.soundingAt(voices[va], t);
        const std::size_t authored_b = onset_index.soundingAt(voices[vb], t);
        const std::uint8_t bass_pitch = lowestSoundingPitch(onset_index, voices, t);
        bool consonant = rule_helpers::isBassSensitiveConsonance(pa, pb, bass_pitch);
        if (!consonant)
          consonant = isDeclaredSuspensionDissonance(onset_index, voices, material, voices[va],
                                                     voices[vb], t);
        if (!consonant && std::abs(static_cast<int>(pa) - static_cast<int>(pb)) % 12 == 5 &&
            std::min(pa, pb) == bass_pitch) {
          const VoiceId upper_voice = pa > pb ? voices[va] : voices[vb];
          consonant =
              isDeclaredCadentialSixFour(onset_index, voices, harmonic_plan, upper_voice, t);
        }
        if (consonant)
          continue;

        // Find span_id: prefer the Compose-source side of the pair.
        SpanId compose_span = kInvalidSpanId;
        SpanId fallback_span = kInvalidSpanId;
        for (std::size_t k = 0; k < notes.size(); ++k) {
          if (notes[k].voice != voices[va] && notes[k].voice != voices[vb]) {
            continue;
          }
          if (t < notes[k].start_tick || t >= notes[k].start_tick + notes[k].duration)
            continue;
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
        if (compose_span == kInvalidSpanId && !audit_final_score) {
          continue;  // both Material; nothing the composer can do.
        }
        ValidationFailure failure;
        failure.span_id = compose_span != kInvalidSpanId ? compose_span : fallback_span;
        failure.rule_id = "vertical_dissonance";
        recordCounterpointFinding(failure, {authored_a, authored_b});
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
  // Upper-voice fourths are handled by the bass-sensitive rule above; they
  // are not intrinsically dissonant merely because they invert to fifths.
  for (std::size_t va = 0; va < voices.size(); ++va) {
    const std::size_t vb = va + 1;
    if (vb >= voices.size())
      break;
    // Exclude the bottom-of-texture pair (mirrors P7 spacing scoping).
    if (vb == voices.size() - 1)
      continue;
    std::uint8_t prev_pa = 0;
    std::uint8_t prev_pb = 0;
    for (Tick t : ticks) {
      const std::uint8_t pa = onset_index.pitchAt(voices[va], t);
      const std::uint8_t pb = onset_index.pitchAt(voices[vb], t);
      if (pa == 0 || pb == 0) {
        prev_pa = 0;
        prev_pb = 0;
        continue;
      }

      // Material-skip: find both notes' indices at this tick; skip the
      // pair only when BOTH are Material (composer cannot fix inputs).
      const std::size_t idx_upper = onset_index.soundingAt(voices[va], t);
      const std::size_t idx_lower = onset_index.soundingAt(voices[vb], t);
      const bool upper_material =
          idx_upper < provenance.size() && provenance[idx_upper].source == NoteSource::Material;
      const bool lower_material =
          idx_lower < provenance.size() && provenance[idx_lower].source == NoteSource::Material;
      const bool both_material = upper_material && lower_material;
      if ((audit_final_score || !both_material) && isStructuralAccent(harmonic_plan, t)) {
        const PerfectMotionKind kind = prev_pa != 0 && prev_pb != 0
                                           ? classifyPerfectMotion(prev_pa, pa, prev_pb, pb)
                                           : PerfectMotionKind::None;
        if (kind == PerfectMotionKind::ParallelOctave) {
          SpanId fail_span = kInvalidSpanId;
          if (idx_lower < provenance.size())
            fail_span = provenance[idx_lower].span_id;
          else if (idx_upper < provenance.size())
            fail_span = provenance[idx_upper].span_id;
          ValidationFailure failure;
          failure.span_id = fail_span;
          failure.rule_id = "invertible_at_octave";
          recordCounterpointFinding(failure, {idx_upper, idx_lower});
        }
      }
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

    // Rule P1: forbidden melodic augmented/diminished intervals and direct
    // tritone leaps. Generation validation limits repair responsibility to
    // Compose notes; final-score validation audits every source.
    for (std::size_t i = 1; i < indices.size(); ++i) {
      const std::size_t current = indices[i];
      if (current >= provenance.size())
        continue;
      if (!audit_final_score && provenance[current].source != NoteSource::Compose)
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
      if (!secondary_active && rule_helpers::isContextualAugmentedMelodicInterval(
                                   prev_pitch, current_pitch, harmonic_plan,
                                   notes[indices[i - 1]].start_tick, notes[current].start_tick)) {
        ValidationFailure failure;
        failure.span_id = provenance[current].span_id;
        failure.rule_id = "augmented_melodic";
        recordCounterpointFinding(failure, {indices[i - 1], current});
      }
      if (!secondary_active && semis == 6) {
        ValidationFailure failure;
        failure.span_id = provenance[current].span_id;
        failure.rule_id = "tritone_melodic";
        recordCounterpointFinding(failure, {indices[i - 1], current});
      }
      if (!secondary_active &&
          rule_helpers::isDiminishedMelodicInterval(prev_pitch, current_pitch)) {
        ValidationFailure failure;
        failure.span_id = provenance[current].span_id;
        failure.rule_id = "diminished_melodic";
        recordCounterpointFinding(failure, {indices[i - 1], current});
      }
    }

    // Rule P1: leading tone resolves upward to tonic on the next note.
    for (std::size_t i = 0; i < indices.size(); ++i) {
      const std::size_t current = indices[i];
      if (current >= provenance.size())
        continue;
      if (!audit_final_score && provenance[current].source != NoteSource::Compose)
        continue;
      if (!rule_helpers::isContextualLeadingTone(notes[current].pitch, harmonic_plan,
                                                 notes[current].start_tick))
        continue;
      if (i + 1 >= indices.size() || !rule_helpers::isContextualLeadingToneResolution(
                                         notes[current].pitch, notes[indices[i + 1]].pitch,
                                         harmonic_plan, notes[current].start_tick)) {
        ValidationFailure failure;
        failure.span_id = provenance[current].span_id;
        failure.rule_id = "leading_tone_resolution";
        recordCounterpointFinding(failure,
                                  {current, i + 1 < indices.size() ? indices[i + 1] : current});
      }
    }

    // Rule 3: consecutive_leaps.
    for (std::size_t i = 2; i < indices.size(); ++i) {
      if (indices[i] >= provenance.size() ||
          (!audit_final_score && provenance[indices[i]].source != NoteSource::Compose))
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
        recordCounterpointFinding(failure, {indices[i - 2], indices[i - 1], indices[i]});
      }
    }

    // Rule 4: weak-beat non-chord-tone must be approached AND left by step
    // (<= 2 semis). Final-score validation includes fixed and ornamented
    // sources. Voice-boundary notes (no prev or no next) are exempt.
    for (std::size_t i = 1; i + 1 < indices.size(); ++i) {
      const std::size_t k = indices[i];
      if (k >= provenance.size())
        continue;
      if (!audit_final_score && provenance[k].source != NoteSource::Compose)
        continue;
      if (hasRuleBit(provenance, k, RuleBit::CadenceCellCommitted))
        continue;
      if (i + 1 < indices.size() &&
          hasRuleBit(provenance, indices[i + 1], RuleBit::CadenceCellCommitted))
        continue;
      if (isStructuralAccent(harmonic_plan, notes[k].start_tick))
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
        recordCounterpointFinding(failure, {indices[i - 1], k, indices[i + 1]});
      }
    }
  }

  // Suspension checks. Each declaration must describe a real prepared
  // dissonance: consonant preparation, equal-or-longer preparation duration,
  // stronger metrical placement for the suspension, the type-specific
  // dissonance/resolution intervals, and a step in the prescribed direction.
  for (const auto& sp : material.suspension_patterns) {
    const std::uint8_t prep_actual = onset_index.pitchAt(sp.voice, sp.preparation_tick);
    const std::uint8_t sus_actual = onset_index.pitchAt(sp.voice, sp.suspension_tick);
    const std::uint8_t res_actual = onset_index.pitchAt(sp.voice, sp.resolution_tick);
    const std::size_t prep_index = onset_index.startingAt(sp.voice, sp.preparation_tick);
    const std::size_t sus_index = onset_index.startingAt(sp.voice, sp.suspension_tick);
    const std::size_t res_index = onset_index.startingAt(sp.voice, sp.resolution_tick);
    SpanId fail_span = kInvalidSpanId;
    if (sus_index < provenance.size())
      fail_span = provenance[sus_index].span_id;
    auto addSuspensionFailure = [&](const char* rule_id) {
      ValidationFailure failure;
      failure.span_id = fail_span;
      failure.rule_id = rule_id;
      failure.kind = FailKind::MusicalFail;
      report.failures.push_back(failure);
    };
    const bool ordered =
        sp.preparation_tick < sp.suspension_tick && sp.suspension_tick < sp.resolution_tick;

    // Preparation: must be consonant against the lowest sounding voice at
    // the preparation tick and repeat the same pitch into the suspension.
    const auto lowestOtherPitch = [&](Tick at) -> std::uint8_t {
      std::uint8_t lowest = 0;
      for (VoiceId other : voices) {
        if (other == sp.voice)
          continue;
        const std::uint8_t pitch = onset_index.pitchAt(other, at);
        if (pitch != 0 && (lowest == 0 || pitch < lowest))
          lowest = pitch;
      }
      return lowest;
    };
    bool prep_ok = ordered && prep_actual != 0 && prep_actual == sp.preparation_pitch &&
                   sus_actual == sp.suspension_pitch && res_actual == sp.resolution_pitch &&
                   prep_actual == sus_actual;
    if (prep_ok) {
      const std::uint8_t other_pitch = lowestOtherPitch(sp.preparation_tick);
      if (other_pitch == 0)
        prep_ok = false;
      else if (sp.type == SuspensionType::Sus2_3)
        prep_ok = rule_helpers::isConsonantAboveBass(other_pitch, prep_actual);
      else
        prep_ok = rule_helpers::isConsonantAboveBass(prep_actual, other_pitch);
    }
    if (!prep_ok)
      addSuspensionFailure("suspension_preparation");

    const bool duration_ok = prep_index < notes.size() && sus_index < notes.size() &&
                             notes[prep_index].duration >= notes[sus_index].duration;
    if (!duration_ok)
      addSuspensionFailure("suspension_preparation_duration");

    const MetricalStrength prep_strength =
        rule_helpers::metricalStrengthAt(harmonic_plan, sp.preparation_tick);
    const MetricalStrength sus_strength =
        rule_helpers::metricalStrengthAt(harmonic_plan, sp.suspension_tick);
    if (static_cast<int>(sus_strength) >= static_cast<int>(prep_strength))
      addSuspensionFailure("suspension_metrical_accent");

    // Resolution: must be a 1- or 2-semitone step in the prescribed
    // direction. Sus2_3 ascends; the other three descend.
    bool res_ok = ordered && sus_actual != 0 && res_actual != 0;
    if (res_ok) {
      const int delta = static_cast<int>(res_actual) - static_cast<int>(sus_actual);
      const bool small_step = std::abs(delta) == 1 || std::abs(delta) == 2;
      const bool ascending = sp.type == SuspensionType::Sus2_3;
      res_ok = small_step && ((ascending && delta > 0) || (!ascending && delta < 0));
    }
    if (!res_ok)
      addSuspensionFailure("suspension_resolution_step_down");

    const std::uint8_t other_at_sus = lowestOtherPitch(sp.suspension_tick);
    const std::uint8_t other_at_res = lowestOtherPitch(sp.resolution_tick);
    bool interval_ok = other_at_sus != 0 && other_at_res != 0 && sus_actual != 0 && res_actual != 0;
    int sus_ic = -1;
    int res_ic = -1;
    if (interval_ok) {
      if (sp.type == SuspensionType::Sus2_3) {
        sus_ic = ((static_cast<int>(other_at_sus) - static_cast<int>(sus_actual)) % 12 + 12) % 12;
        res_ic = ((static_cast<int>(other_at_res) - static_cast<int>(res_actual)) % 12 + 12) % 12;
      } else {
        sus_ic = ((static_cast<int>(sus_actual) - static_cast<int>(other_at_sus)) % 12 + 12) % 12;
        res_ic = ((static_cast<int>(res_actual) - static_cast<int>(other_at_res)) % 12 + 12) % 12;
      }
      switch (sp.type) {
        case SuspensionType::Sus4_3:
          interval_ok = sus_ic == 5 && (res_ic == 3 || res_ic == 4);
          break;
        case SuspensionType::Sus7_6:
          interval_ok = (sus_ic == 10 || sus_ic == 11) && (res_ic == 8 || res_ic == 9);
          break;
        case SuspensionType::Sus9_8:
          interval_ok = (sus_ic == 1 || sus_ic == 2) && res_ic == 0;
          break;
        case SuspensionType::Sus2_3:
          interval_ok = (sus_ic == 1 || sus_ic == 2) && (res_ic == 3 || res_ic == 4);
          break;
      }
    }
    if (!interval_ok)
      addSuspensionFailure("suspension_interval");

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
      const bool prepared = hasRuleBit(provenance, prep_index, RuleBit::SuspensionPrepared);
      const bool resolved = hasRuleBit(provenance, res_index, RuleBit::SuspensionResolved);
      if (prepared && resolved && sus_actual != 0 && res_actual != 0) {
        const std::uint8_t bass_at_sus = lowestOtherPitch(sp.suspension_tick);
        const std::uint8_t bass_at_res = lowestOtherPitch(sp.resolution_tick);
        // Only evaluate when a lower voice actually sounds at both ticks;
        // a lone suspended voice has no interval to measure.
        if (bass_at_sus != 0 && bass_at_res != 0) {
          const int sus_ic =
              ((static_cast<int>(sus_actual) - static_cast<int>(bass_at_sus)) % 12 + 12) % 12;
          const int res_ic =
              ((static_cast<int>(res_actual) - static_cast<int>(bass_at_res)) % 12 + 12) % 12;
          const bool seventh = sus_ic == 10 || sus_ic == 11;
          const bool sixth = res_ic == 8 || res_ic == 9;
          if (!seventh || !sixth)
            addSuspensionFailure("suspension_seventh_sixth");
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
  // The CS voice is derived from provenance, not hardcoded: MaterialNote
  // carries no voice field, so we read it from the emitted notes. The CS
  // voice is the voice of the first note whose provenance voice_intent is
  // VoiceIntent::CountersubjectCarrier (notes and provenance are
  // index-aligned). When no CountersubjectCarrier note was placed, a
  // declared-but-unplaced CS cannot be checked, so the continuity check is
  // skipped entirely rather than sampling an arbitrary voice. The answer
  // window is the [first, last] tick range of material.tonal_answer if
  // used, else material.answer.
  if (!material.countersubject.empty()) {
    const std::vector<MaterialNote>& answer_src =
        (material.use_tonal_answer && !material.tonal_answer.empty()) ? material.tonal_answer
                                                                      : material.answer;
    bool cs_placed = false;
    VoiceId cs_voice = 0;
    for (std::size_t k = 0; k < notes.size() && k < provenance.size(); ++k) {
      if (provenance[k].voice_intent == VoiceIntent::CountersubjectCarrier) {
        cs_voice = notes[k].voice;
        cs_placed = true;
        break;
      }
    }
    if (!answer_src.empty() && cs_placed) {
      const Tick window_start = answer_src.front().start_tick;
      const Tick window_end = answer_src.back().start_tick + answer_src.back().duration;
      // Walk window in quarter-beat increments; require a CS-voice note
      // sounding at every step.
      bool gap_found = false;
      for (Tick t = window_start; t < window_end; t += kTicksPerBeat) {
        if (onset_index.pitchAt(cs_voice, t) == 0) {
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
    const std::size_t canonical_count =
        material.canonical_subject_note_count == 0
            ? material.subject.size()
            : std::min(material.canonical_subject_note_count, material.subject.size());
    const std::size_t src_begin = frag.source_start_index;
    if (src_begin >= canonical_count)
      continue;
    const std::size_t src_count =
        (frag.source_count == 0) ? (canonical_count - src_begin) : frag.source_count;
    const std::size_t src_end = std::min(src_begin + src_count, canonical_count);
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
      std::vector<std::size_t> voiced_indices;
      voiced_indices.reserve(max_voice + 1);
      for (VoiceId v = 0; v <= max_voice; ++v) {
        const std::uint8_t p = onset_index.pitchAt(v, chord.start_tick);
        if (p > 0) {
          voiced_pitches.push_back(p);
          voiced_ids.push_back(v);
          voiced_indices.push_back(onset_index.soundingAt(v, chord.start_tick));
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
      const auto tonal_context = rule_helpers::tonalContextAt(harmonic_plan, chord.start_tick);
      const std::uint8_t leading_tone_pc = tonal_context.leading_tone_pc;
      const bool chord_owns_leading_tone =
          tonal_context.has_active_leading_tone &&
          ((triad[0] == leading_tone_pc) || (triad[1] == leading_tone_pc) ||
           (triad[2] == leading_tone_pc));
      if (chord_owns_leading_tone) {
        std::vector<std::size_t> leading_indices;
        for (std::size_t i = 0; i < voiced_pitches.size(); ++i) {
          const std::uint8_t p = voiced_pitches[i];
          if (static_cast<std::uint8_t>(p % 12) == leading_tone_pc)
            leading_indices.push_back(voiced_indices[i]);
        }
        if (leading_indices.size() >= 2) {
          ValidationFailure failure;
          failure.rule_id = "doubling_no_leading_tone";
          failure.kind = FailKind::MusicalFail;
          recordCounterpointFinding(failure, {leading_indices[0], leading_indices[1]});
        }
      }

      // Rule 2: doubling_no_seventh.
      if (hasSeventh(chord.quality)) {
        const std::uint8_t seventh_pc =
            static_cast<std::uint8_t>((chord.root_pc + seventhOffset(chord.quality)) % 12);
        std::vector<std::size_t> seventh_indices;
        for (std::size_t i = 0; i < voiced_pitches.size(); ++i) {
          const std::uint8_t p = voiced_pitches[i];
          if (static_cast<std::uint8_t>(p % 12) == seventh_pc)
            seventh_indices.push_back(voiced_indices[i]);
        }
        if (seventh_indices.size() >= 2) {
          ValidationFailure failure;
          failure.rule_id = "doubling_no_seventh";
          failure.kind = FailKind::MusicalFail;
          recordCounterpointFinding(failure, {seventh_indices[0], seventh_indices[1]});
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
      if (harmonic_plan.enforce_chordal_upper_voice_spacing && voiced_pitches.size() >= 3) {
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
    // whose degree equals the declared secondary_of target.
    //
    // DESIGN NOTE (name overstates behavior): despite "resolution" in
    // the name, this rule is intentionally DEGREE-ONLY. It checks only
    // that the next chord's degree == chord.secondary_of (the harmonic
    // "V/X → X" succession). The secondary leading-tone voice-leading
    // (the actual chromatic LT rising by step into the target) is NOT a
    // failure condition here; it is tracked descriptively via the
    // RuleBit::SecondaryDominantResolved provenance bit, which the
    // candidate search wires when the resolution chord is reached. This
    // keeps the rule from failing free-voice tonicizations where the LT
    // voice is legitimately forced off-step by P7 doubling/spacing
    // constraints, while the provenance bit still records whether the
    // ideal voice-leading shipped.
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
        for (std::size_t note_index = 0; note_index < notes.size(); ++note_index) {
          const auto& n = notes[note_index];
          if (n.voice != tmpl.voice)
            continue;
          const bool declared_match =
              note_index < provenance.size() && provenance[note_index].has_authored_note &&
              provenance[note_index].authored_start_tick == expected_tick &&
              provenance[note_index].authored_pitch == static_cast<std::uint8_t>(expected_pitch) &&
              provenance[note_index].authored_duration == expected_dur;
          if (n.start_tick != expected_tick && !declared_match)
            continue;
          if (declared_match || (n.pitch == static_cast<std::uint8_t>(expected_pitch) &&
                                 n.duration == expected_dur)) {
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
    if (entry.leader_start_index >= leader->size() ||
        entry.follower_start_index >= follower->size())
      continue;
    const std::size_t available = std::min(leader->size() - entry.leader_start_index,
                                           follower->size() - entry.follower_start_index);
    const std::size_t count = entry.note_count == 0 ? available : entry.note_count;
    if (count == 0 || count > available)
      continue;
    const MaterialNote& leader_head = (*leader)[entry.leader_start_index];
    const MaterialNote& follower_head = (*follower)[entry.follower_start_index];
    const Tick expected_follower_tick = leader_head.start_tick + entry.distance_ticks;
    const int expected_follower_pitch = static_cast<int>(leader_head.pitch) + entry.interval_semis;
    bool declaration_ok = follower_head.start_tick == expected_follower_tick;
    const bool pitch_ok = expected_follower_pitch >= 0 && expected_follower_pitch <= 127 &&
                          follower_head.pitch == static_cast<std::uint8_t>(expected_follower_pitch);
    declaration_ok = declaration_ok && pitch_ok;

    // Validate the declared entry's complete rhythm/contour window, not only
    // its head. Real answers retain a constant transposition. Tonal answers
    // may remap tonic/dominant degrees in the four-note head, then must rejoin
    // the stored real-answer contour exactly.
    const std::uint8_t tonic_pc = static_cast<std::uint8_t>(harmonic_plan.tonic_pc % 12);
    const std::uint8_t dominant_pc = static_cast<std::uint8_t>((tonic_pc + 7) % 12);
    const int tonal_tail_interval =
        entry.has_tonal_base_interval ? entry.tonal_base_interval_semis : entry.interval_semis;
    for (std::size_t offset = 0; offset < count && declaration_ok; ++offset) {
      const MaterialNote& leader_note = (*leader)[entry.leader_start_index + offset];
      const MaterialNote& follower_note = (*follower)[entry.follower_start_index + offset];
      declaration_ok = follower_note.start_tick == leader_note.start_tick + entry.distance_ticks &&
                       follower_note.duration == leader_note.duration;
      if (!declaration_ok)
        break;
      if (entry.follower_fragment == MaterialFragment::Answer) {
        declaration_ok = static_cast<int>(follower_note.pitch) ==
                         static_cast<int>(leader_note.pitch) + entry.interval_semis;
      } else if (entry.follower_fragment == MaterialFragment::TonalAnswer) {
        if (offset < 4) {
          const std::uint8_t leader_pc = static_cast<std::uint8_t>(leader_note.pitch % 12);
          if (leader_pc == tonic_pc) {
            declaration_ok = follower_note.pitch % 12 == dominant_pc;
          } else if (leader_pc == dominant_pc) {
            declaration_ok = follower_note.pitch % 12 == tonic_pc;
          } else {
            declaration_ok = static_cast<int>(follower_note.pitch) ==
                             static_cast<int>(leader_note.pitch) + tonal_tail_interval;
          }
        } else {
          declaration_ok = static_cast<int>(follower_note.pitch) ==
                           static_cast<int>(leader_note.pitch) + tonal_tail_interval;
        }
      }
    }

    if (!declaration_ok) {
      ValidationFailure failure;
      failure.rule_id = "imitation_entry_match";
      failure.kind = FailKind::MusicalFail;
      report.failures.push_back(failure);
      continue;
    }

    // On the generation carrier, verify that the declared material window is
    // actually present in the emitted notes for the declared voices, with
    // Material provenance and ImitationEntryMatched stamped on both heads.
    if (!audit_final_score && !notes.empty() && notes.size() == provenance.size()) {
      const auto emittedMatches = [&](const std::vector<MaterialNote>& expected,
                                      std::size_t start_index, VoiceId voice, VoiceIntent intent) {
        for (std::size_t offset = 0; offset < count; ++offset) {
          const MaterialNote& material_note = expected[start_index + offset];
          bool found = false;
          for (std::size_t note_index = 0; note_index < notes.size(); ++note_index) {
            const NoteEvent& note = notes[note_index];
            const NoteProvenance& note_provenance = provenance[note_index];
            if (note.voice != voice || note.start_tick != material_note.start_tick ||
                note.duration != material_note.duration || note.pitch != material_note.pitch ||
                note_provenance.source != NoteSource::Material ||
                note_provenance.voice_intent != intent) {
              continue;
            }
            if (offset == 0 &&
                !(note_provenance.satisfied_rules & ruleBitMask(RuleBit::ImitationEntryMatched))) {
              return false;
            }
            found = true;
            break;
          }
          if (!found)
            return false;
        }
        return true;
      };
      if (!emittedMatches(*leader, entry.leader_start_index, entry.leader_voice,
                          VoiceIntent::SubjectCarrier) ||
          !emittedMatches(*follower, entry.follower_start_index, entry.follower_voice,
                          VoiceIntent::AnswerCarrier)) {
        ValidationFailure failure;
        failure.rule_id = "imitation_entry_realization";
        failure.kind = FailKind::MusicalFail;
        report.failures.push_back(failure);
      }
    }
  }

  // Development-section rules.
  //
  // middle_entry_in_related_key: every MiddleEntryDecl must declare a
  //   related key and every note pitch class must stay in the home mode's
  //   diatonic collection. Major entries rotate V / vi / IV / ii; minor
  //   entries rotate v / III / iv and must not import a major-subject
  //   accidental into the minor context.
  // stretto_overlap_valid: the follower must enter strictly inside the
  //   leader's window (real time overlap) AND follower_notes[i].pitch
  //   must equal subject[i].pitch + interval_semis (the follower is the
  //   subject transposed by the declared interval).
  // pedal_point_tonic_or_dominant: every pedal pitch class must be the
  //   home tonic or dominant.
  const std::uint8_t home_tonic_pc = static_cast<std::uint8_t>(harmonic_plan.tonic_pc % 12);
  const std::uint8_t dominant_pc = static_cast<std::uint8_t>((home_tonic_pc + 7) % 12);
  const std::array<std::uint8_t, 4> major_related_key_pcs = {
      dominant_pc,                                          // V
      static_cast<std::uint8_t>((home_tonic_pc + 9) % 12),  // vi
      static_cast<std::uint8_t>((home_tonic_pc + 5) % 12),  // IV
      static_cast<std::uint8_t>((home_tonic_pc + 2) % 12),  // ii
  };
  const std::array<std::uint8_t, 3> minor_related_key_pcs = {
      dominant_pc,                                          // v
      static_cast<std::uint8_t>((home_tonic_pc + 3) % 12),  // III
      static_cast<std::uint8_t>((home_tonic_pc + 5) % 12),  // iv
  };
  for (const auto& entry : material.middle_entries) {
    const std::uint8_t key_pc = static_cast<std::uint8_t>(entry.related_key_pc % 12);
    bool key_ok = false;
    if (harmonic_plan.is_minor) {
      for (auto r : minor_related_key_pcs) {
        if (key_pc == r)
          key_ok = true;
      }
    } else {
      for (auto r : major_related_key_pcs) {
        if (key_pc == r)
          key_ok = true;
      }
    }
    // Major stations keep their related-key scale check; the relative vi is
    // naturally the home collection. Minor development uses degree shifts in
    // the home minor collection, so its context is deliberately the home key.
    const bool is_vi = key_pc == static_cast<std::uint8_t>((home_tonic_pc + 9) % 12);
    const std::uint8_t scale_tonic_pc = harmonic_plan.is_minor || is_vi ? home_tonic_pc : key_pc;
    const auto isInMinorContext = [](std::uint8_t pc, std::uint8_t tonic_pc) {
      // Minor subject rows use natural-minor descent (b7) and harmonic-minor
      // dominant approach (raised 7). Both are legitimate in one phrase.
      const std::uint8_t offset = static_cast<std::uint8_t>((pc + 12 - tonic_pc) % 12);
      return offset == 0 || offset == 2 || offset == 3 || offset == 5 || offset == 7 ||
             offset == 8 || offset == 10 || offset == 11;
    };
    bool notes_ok = true;
    for (const auto& n : entry.notes) {
      const std::uint8_t note_pc = static_cast<std::uint8_t>(n.pitch % 12);
      if (harmonic_plan.is_minor ? !isInMinorContext(note_pc, scale_tonic_pc)
                                 : !isDiatonicInKey(note_pc, scale_tonic_pc, false)) {
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
    const std::size_t canonical_count =
        material.canonical_subject_note_count == 0
            ? material.subject.size()
            : std::min(material.canonical_subject_note_count, material.subject.size());
    bool pitch_ok =
        !stretto.follower_notes.empty() && stretto.follower_notes.size() <= canonical_count;
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

  // Rhythm / meter / phrase rules.
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
      const Tick shortest_phrase = static_cast<Tick>(3) * ticks_per_bar;
      const Tick longest_phrase = static_cast<Tick>(8) * ticks_per_bar;
      bool periodic = true;
      for (std::size_t i = 1; i < ps.phrase_start_ticks.size(); ++i) {
        const Tick len = ps.phrase_start_ticks[i] - ps.phrase_start_ticks[i - 1];
        if (len < shortest_phrase || len > longest_phrase || len % ticks_per_bar != 0) {
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
      anacrusis_ok = ps.anacrusis_ticks > 0 && ps.anacrusis_ticks < ticks_per_bar;
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

  // Texture / instrument / expression rules.
  //
  // voice_range_integrity: every note whose voice has a declared MIDI range
  //   (Material::texture_plan.voice_ranges) must lie inside [lo, hi]. A note
  //   outside its voice's range fires the rule. Skipped when no ranges are
  //   declared, so prior behavior is unchanged.
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

  // Solo String Flow (BWV1007): implicit-voice rules over the
  // ArpeggioFlow line. The figure is regular, so collecting every
  // ArpeggioFlowActive note in onset order and partitioning into contiguous
  // cells of arpeggio_template.group_size reconstructs the implicit voices a
  // listener tracks. The two principal implicit voices are register-defined:
  // the bass stream is the lowest pitch of each cell, the top stream the
  // highest. (Register, not slot position, is what the ear segregates —
  // BWV1007's oscillating figures put the perceived top in the middle of the
  // written cell.)
  {
    const int group_size = material.arpeggio_template.group_size;
    std::vector<std::size_t> flow_indices;
    for (std::size_t i = 0; i < notes.size(); ++i) {
      if (i < provenance.size() && hasRuleBit(provenance, i, RuleBit::ArpeggioFlowActive))
        flow_indices.push_back(i);
    }
    std::sort(flow_indices.begin(), flow_indices.end(), [&](std::size_t a, std::size_t b) {
      return notes[a].start_tick < notes[b].start_tick;
    });

    const std::size_t g = (group_size >= 2) ? static_cast<std::size_t>(group_size) : 0;
    const std::size_t cell_count = (g >= 2) ? flow_indices.size() / g : 0;

    if (cell_count >= 2) {
      // Per-cell bass (min pitch) and top (max pitch) streams.
      std::vector<std::uint8_t> bass_stream;
      std::vector<std::uint8_t> top_stream;
      bass_stream.reserve(cell_count);
      top_stream.reserve(cell_count);
      for (std::size_t cell = 0; cell < cell_count; ++cell) {
        std::uint8_t lo = 255;
        std::uint8_t hi = 0;
        for (std::size_t k = 0; k < g; ++k) {
          const std::uint8_t p = notes[flow_indices[cell * g + k]].pitch;
          lo = std::min(lo, p);
          hi = std::max(hi, p);
        }
        bass_stream.push_back(lo);
        top_stream.push_back(hi);
      }

      // implicit_voice_counterpoint: the bass and top implicit streams must
      // each be melodically valid — no forbidden augmented / tritone /
      // diminished leap between consecutive cells. Reuses the shared
      // rule_helpers predicate so the implicit lines of the solo arpeggio are
      // held to the same melodic standard as the Organ Compose voices.
      bool implicit_ok = true;
      for (const std::vector<std::uint8_t>* stream : {&bass_stream, &top_stream}) {
        for (std::size_t cell = 1; cell < cell_count; ++cell) {
          const Tick from_tick = notes[flow_indices[(cell - 1) * g]].start_tick;
          const Tick to_tick = notes[flow_indices[cell * g]].start_tick;
          if (rule_helpers::isForbiddenMelodicLeap((*stream)[cell - 1], (*stream)[cell],
                                                   harmonic_plan, from_tick, to_tick)) {
            implicit_ok = false;
            break;
          }
        }
        if (!implicit_ok)
          break;
      }
      if (!implicit_ok) {
        ValidationFailure failure;
        failure.rule_id = "implicit_voice_counterpoint";
        failure.kind = FailKind::MusicalFail;
        report.failures.push_back(failure);
      }

      // arpeggio_no_parallel_perfect: the bass and top implicit streams must
      // not move in parallel perfect 5ths/8ves across consecutive cells — the
      // broken-chord equivalent of consecutive parallel perfects between two
      // real voices. Flagged only when both cells frame the same perfect
      // interval class AND both streams move in the same (nonzero) direction;
      // oblique / contrary / static motion is permitted.
      bool parallel_ok = true;
      for (std::size_t cell = 1; cell < cell_count && parallel_ok; ++cell) {
        parallel_ok = !isParallelPerfectMotion(top_stream[cell - 1], top_stream[cell],
                                               bass_stream[cell - 1], bass_stream[cell]);
      }
      if (!parallel_ok) {
        ValidationFailure failure;
        failure.rule_id = "arpeggio_no_parallel_perfect";
        failure.kind = FailKind::MusicalFail;
        report.failures.push_back(failure);
      }
    }
  }

  // Immutable-ground skeleton rules. One shared check for both ground
  // families: an immutable ground (Chaconne ground bass / Passacaglia
  // ground) is the harmonic skeleton of its form and must be replayed
  // unchanged on every cycle. The check is a bar-head skeleton match (the
  // same granularity as cantus_firmus_immutable): a replayed ground may
  // subdivide a bar rhythmically (repeated same-pitch notes, the late-cycle
  // quarter-note intensification), but each ground note on a bar head must
  // equal the canonical material tone sounding at the same cycle-relative
  // position. Any altered, transposed, or reordered restatement changes some
  // bar head and fails. Off-downbeat ground notes are unconstrained (the
  // builders keep their pitch equal to the bar tone by construction). The
  // rule is inert when either the canonical ground or the stamped run is
  // empty (fixtures that declare no ground).
  const auto checkImmutableGround = [&](const std::vector<MaterialNote>& ground,
                                        Tick declared_period, RuleBit replay_bit,
                                        const char* rule_id) {
    bool has_ground_note = false;
    bool ground_ok = true;
    const std::size_t n = ground.size();
    if (n > 0) {
      const Tick period =
          declared_period > 0 ? declared_period : static_cast<Tick>(n) * ticks_per_bar;
      for (std::size_t i = 0; i < notes.size() && ground_ok; ++i) {
        if (i >= provenance.size() || !hasRuleBit(provenance, i, replay_bit))
          continue;
        has_ground_note = true;
        const auto& note = notes[i];
        if (note.start_tick % ticks_per_bar != 0)
          continue;  // only the bar head carries the skeleton tone.
        // Canonical tone: the material note sounding at this cycle-relative
        // position (the latest material onset at or before it).
        const Tick cycle_tick = note.start_tick % period;
        int expected = -1;
        for (const MaterialNote& mnote : ground) {
          if (mnote.start_tick <= cycle_tick)
            expected = mnote.pitch;
        }
        if (expected >= 0 && note.pitch != expected)
          ground_ok = false;
      }
    }
    if (has_ground_note && !ground_ok) {
      ValidationFailure failure;
      failure.rule_id = rule_id;
      failure.kind = FailKind::StructuralFail;
      report.failures.push_back(failure);
    }
  };
  // Solo String Arch (BWV1004 Chaconne).
  checkImmutableGround(material.ground_bass, material.ground_bass_period,
                       RuleBit::GroundBassReplayed, "ground_bass_immutable");
  // Organ Passacaglia.
  checkImmutableGround(material.passacaglia_ground, material.passacaglia_ground_period,
                       RuleBit::PassacagliaGroundReplayed, "passacaglia_ground_immutable");
  // Goldberg's compressed aria-bass phrase is a dedicated 32-tone declaration,
  // not a passacaglia bar-head ground. Every declared onset/duration/pitch must
  // recur exactly in every four-bar variation block. A terminal CodaCarrier
  // may replace the final bar with a tonic cadence; that explicit extension is
  // outside the immutable aria-bass declaration.
  if (!material.goldberg_aria_bass.empty() && material.goldberg_aria_bass_period > 0) {
    Tick score_end = 0;
    for (const auto& note : notes)
      score_end = std::max(score_end, note.start_tick + note.duration);
    Tick immutable_end = score_end;
    const Tick terminal_window = score_end > material.goldberg_aria_bass_period
                                     ? score_end - material.goldberg_aria_bass_period
                                     : 0;
    for (std::size_t i = 0; i < notes.size(); ++i) {
      if (notes[i].start_tick >= terminal_window &&
          hasRuleBit(provenance, i, RuleBit::CodaCommitted)) {
        immutable_end = std::min(immutable_end, notes[i].start_tick);
      }
    }
    bool any = false;
    bool ok = true;
    for (Tick base = 0; base < immutable_end && ok; base += material.goldberg_aria_bass_period) {
      for (const auto& expected : material.goldberg_aria_bass) {
        const Tick tick = base + expected.start_tick;
        if (tick >= immutable_end)
          break;
        bool found = false;
        for (std::size_t i = 0; i < notes.size(); ++i) {
          if (!hasRuleBit(provenance, i, RuleBit::GoldbergBassReplayed))
            continue;
          any = true;
          if (notes[i].start_tick == tick && notes[i].duration == expected.duration &&
              notes[i].pitch == expected.pitch) {
            found = true;
            break;
          }
        }
        if (!found) {
          ok = false;
          break;
        }
      }
    }
    if (!any || !ok) {
      ValidationFailure failure;
      failure.rule_id = "goldberg_aria_bass_immutable";
      failure.kind = FailKind::StructuralFail;
      report.failures.push_back(failure);
    }
  }

  // variation_role_ornament_constraint: a Ground-role variation states the
  // ground bass plainly and must stay un-ornamented — no note may subdivide
  // below the beat. Any note shorter than a quarter (kTicksPerBeat) inside a
  // Ground-role VariationDecl is an illegal ornamental subdivision. Read from
  // the material decls directly (mirroring the other Material-decl rules); one
  // flagged note is enough to fail the span. Inert when material.variations is
  // empty (fixtures that declare no variations).
  for (const VariationDecl& var : material.variations) {
    if (var.role != VariationRole::Ground)
      continue;
    bool ornamented = false;
    for (const MaterialNote& note : var.notes) {
      if (note.duration < kTicksPerBeat) {
        ornamented = true;
        break;
      }
    }
    if (ornamented) {
      ValidationFailure failure;
      failure.rule_id = "variation_role_ornament_constraint";
      failure.kind = FailKind::MusicalFail;
      report.failures.push_back(failure);
      break;
    }
  }

  // figuration_harmonic_consistency: a free-form organ-prelude figuration line
  // (FigurationCarrier) outlines the underlying harmony. The figuration is
  // anchored to the harmony at each bar downbeat: the note that opens a bar must
  // be a chord tone of that bar's chord. Within the bar the line is free to run
  // scalewise (passing / neighbour tones are idiomatic), so only the bar
  // downbeat is constrained. Pedal-point notes (PedalPreparation) are exempt: a
  // pedal is by definition a single sustained pitch held against changing
  // harmony. For each note stamped FigurationCommitted (and not PedalPreparation)
  // whose onset lands on a bar downbeat (start_tick % ticks_per_bar == 0), resolve
  // the active ChordEvent (latest plan.chords entry with start_tick <= the note's
  // onset) and reuse the same chord-tone arithmetic the P7 rules use: triadFor
  // for the root/third/fifth and hasSeventh/seventhOffset for the seventh. If any
  // bar-downbeat figuration note's pitch class is not a chord tone, push ONE
  // MusicalFail. The rule is inert when no FigurationCommitted note exists
  // (fixtures that declare no figuration sections).
  {
    bool figuration_inconsistent = false;
    for (std::size_t i = 0; i < notes.size() && !figuration_inconsistent; ++i) {
      if (!hasRuleBit(provenance, i, RuleBit::FigurationCommitted))
        continue;
      if (hasRuleBit(provenance, i, RuleBit::PedalPreparation))
        continue;  // pedal points are held against changing harmony.
      const auto& note = notes[i];
      const Tick authored_tick =
          provenance[i].has_authored_note ? provenance[i].authored_start_tick : note.start_tick;
      const std::uint8_t authored_pitch =
          provenance[i].has_authored_note ? provenance[i].authored_pitch : note.pitch;
      if (authored_tick % ticks_per_bar != 0)
        continue;  // only the bar downbeat is harmonically anchored.
      const auto& chord = activeChord(harmonic_plan, authored_tick);
      const auto triad = triadFor(chord);
      const std::uint8_t pc = static_cast<std::uint8_t>(authored_pitch % 12);
      bool is_chord_tone = (pc == triad[0] || pc == triad[1] || pc == triad[2]);
      if (!is_chord_tone && hasSeventh(chord.quality)) {
        const std::uint8_t seventh_pc =
            static_cast<std::uint8_t>((chord.root_pc + seventhOffset(chord.quality)) % 12);
        is_chord_tone = (pc == seventh_pc);
      }
      if (!is_chord_tone)
        figuration_inconsistent = true;
    }
    if (figuration_inconsistent) {
      ValidationFailure failure;
      failure.rule_id = "figuration_harmonic_consistency";
      failure.kind = FailKind::MusicalFail;
      report.failures.push_back(failure);
    }
  }

  // toccata_archetype_compatible: an organ toccata's piece-level design pairs a
  // SubjectCharacter affect with one of the four ToccataArchetypes. Some pairs
  // are musically antithetical (see isToccataPairCompatible: Noble x Dramaticus
  // is forbidden). For each ToccataSection in material.toccata_sections, the
  // (character, archetype) pair must be compatible. If any section is an
  // incompatible pair, push ONE MusicalFail. The rule is inert when
  // material.toccata_sections is empty (fixtures that declare none).
  {
    bool toccata_incompatible = false;
    for (const auto& section : material.toccata_sections) {
      if (!isToccataPairCompatible(section.character, section.archetype)) {
        toccata_incompatible = true;
        break;
      }
    }
    if (toccata_incompatible) {
      ValidationFailure failure;
      failure.rule_id = "toccata_archetype_compatible";
      failure.kind = FailKind::MusicalFail;
      report.failures.push_back(failure);
    }
  }

  // cantus_firmus_immutable: the cantus firmus skeleton (the fixed chorale tune,
  // material.cantus_firmus) is the structural backbone of the chorale prelude and
  // is immutable. A CantusFirmusCarrier may replay an embellished
  // line, but each bar's DOWNBEAT tone must still equal the skeleton tone for
  // that bar. Gather every note stamped CantusFirmusReplayed in onset order; for
  // each whose onset lands on a bar downbeat (start_tick % ticks_per_bar == 0),
  // look up the expected skeleton tone material.cantus_firmus[bar_index] where
  // bar_index = start_tick / ticks_per_bar (bounds-guarded). If the replayed
  // downbeat pitch differs from the skeleton pitch, push ONE StructuralFail.
  // Off-downbeat embellishment notes are unconstrained. The rule is inert when
  // material.cantus_firmus is empty or no CantusFirmusReplayed note exists
  // (fixtures that declare no cantus firmus).
  {
    std::vector<std::size_t> cf_indices;
    for (std::size_t i = 0; i < notes.size(); ++i) {
      if (i < provenance.size() && hasRuleBit(provenance, i, RuleBit::CantusFirmusReplayed))
        cf_indices.push_back(i);
    }
    std::sort(cf_indices.begin(), cf_indices.end(), [&](std::size_t a, std::size_t b) {
      return notes[a].start_tick < notes[b].start_tick;
    });

    if (!material.cantus_firmus.empty() && !cf_indices.empty()) {
      bool cf_altered = false;
      for (std::size_t idx : cf_indices) {
        const auto& note = notes[idx];
        if (note.start_tick % ticks_per_bar != 0)
          continue;  // only bar downbeats are constrained.
        const std::size_t bar_index = static_cast<std::size_t>(note.start_tick / ticks_per_bar);
        if (bar_index >= material.cantus_firmus.size())
          continue;  // out of skeleton range; unconstrained.
        if (note.pitch != material.cantus_firmus[bar_index].pitch) {
          cf_altered = true;
          break;
        }
      }
      if (cf_altered) {
        ValidationFailure failure;
        failure.rule_id = "cantus_firmus_immutable";
        failure.kind = FailKind::StructuralFail;
        report.failures.push_back(failure);
      }
    }
  }

  // Organ Trio Sonata: voice_independence_threshold. A trio sonata's
  // defining technique is THREE independent voices (RH = Great, LH = Swell,
  // Pedal). This rule operates ONLY on notes carrying the TrioVoiceIndependent
  // bit; it groups them by voice and measures the pairwise voice independence of
  // the (up to three) trio voices, soft-failing (MusicalFail, per the
  // "voice independence >= 0.6" soft-penalty convention) below 0.6.
  //
  // Self-contained independence metric (does NOT call into src/analysis/):
  // Build the sorted set of all distinct onset ticks across both voices of a
  // pair. Walk adjacent onset boundaries; at each boundary t (after the first),
  // each voice is in one of three motion states relative to the previous
  // boundary: it has a NEW onset at t (it moved/re-articulated) or it does NOT
  // (it is sustaining / silent). A boundary is counted as INDEPENDENT when the
  // two voices differ in motion direction or rhythm:
  //   (a) rhythmic independence — exactly one voice has a new onset at t (the
  //       other sustains): oblique motion / differing rhythm; OR
  //   (b) both voices have a new onset at t but their pitch motions (sign of the
  //       interval from each voice's previous sounding pitch) are NOT the same
  //       non-zero direction — i.e. contrary, oblique (one static), or one moves
  //       while the other repeats. Only genuine PARALLEL/SIMILAR motion (both
  //       move the same non-zero direction) counts as dependent.
  // The pair's independence score = independent_boundaries / total_boundaries;
  // the rule's score is the mean across all voice pairs. Inert when fewer than
  // two trio voices are present. Deterministic and pure.
  {
    std::vector<VoiceId> trio_voice_ids;
    for (std::size_t i = 0; i < notes.size(); ++i) {
      if (!hasRuleBit(provenance, i, RuleBit::TrioVoiceIndependent))
        continue;
      if (std::find(trio_voice_ids.begin(), trio_voice_ids.end(), notes[i].voice) ==
          trio_voice_ids.end()) {
        trio_voice_ids.push_back(notes[i].voice);
      }
    }
    if (trio_voice_ids.size() >= 2) {
      std::sort(trio_voice_ids.begin(), trio_voice_ids.end());
      // Per-voice onset->pitch maps, restricted to TrioVoiceIndependent notes.
      // Returns true and writes the sounding pitch (latest trio onset <= `at`)
      // into `out_pitch`, or false when `voice` has no trio note sounding at
      // `at`. A `bool found` flag is used instead of a sentinel because `Tick`
      // is unsigned: a `-1` seed would wrap to 0xFFFFFFFF and never be exceeded
      // by a real start_tick, defeating the "latest onset" comparison.
      auto onsetPitch = [&](VoiceId voice, Tick at, int* out_pitch) -> bool {
        bool found = false;
        Tick best = 0;
        int pitch = -1;
        for (std::size_t i = 0; i < notes.size(); ++i) {
          if (notes[i].voice != voice)
            continue;
          if (!hasRuleBit(provenance, i, RuleBit::TrioVoiceIndependent))
            continue;
          if (notes[i].start_tick <= at && (!found || notes[i].start_tick > best)) {
            found = true;
            best = notes[i].start_tick;
            pitch = static_cast<int>(notes[i].pitch);
          }
        }
        if (found && out_pitch != nullptr)
          *out_pitch = pitch;
        return found;
      };
      auto hasOnsetAt = [&](VoiceId voice, Tick at) -> bool {
        for (std::size_t i = 0; i < notes.size(); ++i) {
          if (notes[i].voice != voice)
            continue;
          if (!hasRuleBit(provenance, i, RuleBit::TrioVoiceIndependent))
            continue;
          if (notes[i].start_tick == at)
            return true;
        }
        return false;
      };

      double independence_sum = 0.0;
      int pair_count = 0;
      for (std::size_t a = 0; a < trio_voice_ids.size(); ++a) {
        for (std::size_t b = a + 1; b < trio_voice_ids.size(); ++b) {
          const VoiceId va = trio_voice_ids[a];
          const VoiceId vb = trio_voice_ids[b];
          // Distinct onset ticks across both voices, sorted ascending.
          std::vector<Tick> boundaries;
          for (std::size_t i = 0; i < notes.size(); ++i) {
            if (!hasRuleBit(provenance, i, RuleBit::TrioVoiceIndependent))
              continue;
            if (notes[i].voice != va && notes[i].voice != vb)
              continue;
            if (std::find(boundaries.begin(), boundaries.end(), notes[i].start_tick) ==
                boundaries.end()) {
              boundaries.push_back(notes[i].start_tick);
            }
          }
          std::sort(boundaries.begin(), boundaries.end());
          if (boundaries.size() < 2) {
            // Not enough motion to assess; treat as fully independent (no drag).
            independence_sum += 1.0;
            ++pair_count;
            continue;
          }
          int independent = 0;
          int total = 0;
          for (std::size_t k = 1; k < boundaries.size(); ++k) {
            const Tick t = boundaries[k];
            const Tick prev = boundaries[k - 1];
            const bool a_onset = hasOnsetAt(va, t);
            const bool b_onset = hasOnsetAt(vb, t);
            ++total;
            if (a_onset != b_onset) {
              // Exactly one voice re-articulated: rhythmic independence.
              ++independent;
              continue;
            }
            // Both re-articulated: compare pitch-motion directions.
            int a_now = 0, a_prev = 0, b_now = 0, b_prev = 0;
            const bool a_known = onsetPitch(va, t, &a_now) && onsetPitch(va, prev, &a_prev);
            const bool b_known = onsetPitch(vb, t, &b_now) && onsetPitch(vb, prev, &b_prev);
            if (!a_known || !b_known) {
              // No prior sounding pitch for one voice: cannot establish genuine
              // same-direction motion, so this boundary is not dependent.
              ++independent;
              continue;
            }
            const int a_dir = (a_now > a_prev) ? 1 : (a_now < a_prev ? -1 : 0);
            const int b_dir = (b_now > b_prev) ? 1 : (b_now < b_prev ? -1 : 0);
            // Dependent only when both move the SAME non-zero direction
            // (parallel / similar motion). Otherwise independent.
            if (!(a_dir != 0 && a_dir == b_dir)) {
              ++independent;
            }
          }
          const double score =
              (total > 0) ? static_cast<double>(independent) / static_cast<double>(total) : 1.0;
          independence_sum += score;
          ++pair_count;
        }
      }
      const double mean_independence =
          (pair_count > 0) ? independence_sum / static_cast<double>(pair_count) : 1.0;
      if (mean_independence < 0.6) {
        ValidationFailure failure;
        failure.rule_id = "voice_independence_threshold";
        failure.kind = FailKind::MusicalFail;
        report.failures.push_back(failure);
      }
    }
  }

  // Organ Fantasia: section_contrast_required. A fantasia's defining
  // technique is CONTRASTING sections (free / fugal / toccata-like / chordal).
  // This rule operates ONLY on notes carrying the FantasiaSectionContrast bit.
  // It walks adjacent FantasiaSection windows (material.fantasia_sections, in
  // declaration order) and, for each section, measures two self-contained
  // traits from the emitted FantasiaSectionContrast notes whose onset falls in
  // [start_tick, end_tick):
  //   - density: notes per bar = note_count * ticks_per_bar / window_ticks,
  //              rounded down (the realized notes-per-bar tier).
  //   - mean register: integer mean MIDI pitch of the section's notes.
  // CRITERION: each ADJACENT pair of sections must differ in EITHER density
  // (|density_a - density_b| >= kMinDensityMargin = 2 notes/bar) OR mean
  // register (|register_a - register_b| >= kMinRegisterMargin = 5 semitones,
  // a perfect fourth). A pair that is near-identical in BOTH (density within 1
  // AND register within 4) is NOT contrasting and pushes ONE MusicalFail (SOFT).
  // Inert when fewer than 2 sections carry the bit. Deterministic and pure;
  // no src/analysis/ calls. Heeds the sentinel lesson: running maxima are not
  // seeded with -1 on unsigned types (note counts / ticks accumulate from 0).
  {
    constexpr int kMinDensityMargin = 2;   // notes/bar difference for contrast.
    constexpr int kMinRegisterMargin = 5;  // semitone difference for contrast.
    struct SectionStat {
      int density = 0;        // notes per bar (realized).
      int mean_register = 0;  // mean MIDI pitch.
      int note_count = 0;
    };
    std::vector<SectionStat> stats;
    stats.reserve(material.fantasia_sections.size());
    for (const auto& section : material.fantasia_sections) {
      int note_count = 0;
      long pitch_sum = 0;
      for (std::size_t i = 0; i < notes.size(); ++i) {
        if (!hasRuleBit(provenance, i, RuleBit::FantasiaSectionContrast))
          continue;
        // Section contrast is a property of the declared Fantasia carriers.
        // Ornament expansion inherits the carrier bit for provenance
        // traceability, but it must not inflate a sparse section's structural
        // density or blur its mean register after FinalScore decoration.
        if (i < provenance.size() && provenance[i].source == NoteSource::Ornament)
          continue;
        if (notes[i].start_tick < section.start_tick || notes[i].start_tick >= section.end_tick)
          continue;
        ++note_count;
        pitch_sum += static_cast<long>(notes[i].pitch);
      }
      if (note_count == 0)
        continue;  // no realized notes for this section window.
      const Tick window_ticks =
          (section.end_tick > section.start_tick) ? (section.end_tick - section.start_tick) : 0;
      SectionStat stat;
      stat.note_count = note_count;
      stat.density = (window_ticks > 0)
                         ? static_cast<int>(static_cast<long>(note_count) * ticks_per_bar /
                                            static_cast<long>(window_ticks))
                         : 0;
      stat.mean_register = static_cast<int>(pitch_sum / note_count);
      stats.push_back(stat);
    }
    if (stats.size() >= 2) {
      bool uncontrasting_pair = false;
      for (std::size_t i = 1; i < stats.size(); ++i) {
        const int density_diff = std::abs(stats[i].density - stats[i - 1].density);
        const int register_diff = std::abs(stats[i].mean_register - stats[i - 1].mean_register);
        const bool contrasts =
            (density_diff >= kMinDensityMargin) || (register_diff >= kMinRegisterMargin);
        if (!contrasts) {
          uncontrasting_pair = true;
          break;
        }
      }
      if (uncontrasting_pair) {
        ValidationFailure failure;
        failure.rule_id = "section_contrast_required";
        failure.kind = FailKind::MusicalFail;
        report.failures.push_back(failure);
      }
    }
  }

  // Fugue countersubject: countersubject_invertible. A fugue's countersubject
  // is conceived in invertible (double) counterpoint at the octave: it must work
  // both above and below the subject. Under octave inversion a perfect fifth
  // becomes a perfect fourth (a dissonance against the bass), so a perfect fifth
  // on a strong beat between the countersubject and the subject/answer it sounds
  // against is not strictly invertible at the octave. This rule operates ONLY on
  // notes carrying the CountersubjectInvertible bit (the first high-lane RuleBit).
  // It identifies the countersubject voice(s) from those notes and the
  // subject/answer voice(s) from notes whose provenance voice_intent is
  // SubjectCarrier or AnswerCarrier, then walks every strong-beat tick where a
  // countersubject voice and a subject/answer voice both sound. If the vertical
  // interval reduces to a perfect fifth (|interval| % 12 == 7) at any such tick,
  // the pair is not invertible at the octave.
  //
  // This is reported INFORMATIONALLY (report.informational), not as a gating
  // failure: free-style fugue countersubjects in the existing corpus routinely
  // place consonant fifths against the subject on strong beats (strict
  // invertibility is a design constraint of double-counterpoint countersubjects,
  // not of every countersubject), so a hard or soft FAILURE here would penalize
  // established pieces. The observation is recorded for provenance/audit; it
  // never sets status or empties-failures. Inert when no CountersubjectInvertible
  // note or no overlapping subject/answer voice exists (most fixtures declare no
  // countersubject). Deterministic and pure; reuses the same isStrongBeat /
  // voicePitchAt helpers the invertible_at_octave rule uses.
  {
    std::vector<VoiceId> cs_voices;
    std::vector<VoiceId> sa_voices;
    Tick cs_first = std::numeric_limits<Tick>::max();
    Tick cs_last = 0;
    bool has_cs = false;
    for (std::size_t i = 0; i < notes.size(); ++i) {
      if (i >= provenance.size())
        continue;
      if (hasRuleBit(provenance, i, RuleBit::CountersubjectInvertible)) {
        if (std::find(cs_voices.begin(), cs_voices.end(), notes[i].voice) == cs_voices.end())
          cs_voices.push_back(notes[i].voice);
        cs_first = std::min(cs_first, notes[i].start_tick);
        cs_last = std::max(cs_last, notes[i].start_tick + notes[i].duration);
        has_cs = true;
      } else if (provenance[i].voice_intent == VoiceIntent::SubjectCarrier ||
                 provenance[i].voice_intent == VoiceIntent::AnswerCarrier) {
        if (std::find(sa_voices.begin(), sa_voices.end(), notes[i].voice) == sa_voices.end())
          sa_voices.push_back(notes[i].voice);
      }
    }
    if (has_cs && !cs_voices.empty() && !sa_voices.empty()) {
      // Distinct strong-beat onset ticks inside the countersubject's span; the
      // pair only needs checking where the countersubject actually sounds.
      std::vector<Tick> ticks;
      for (std::size_t i = 0; i < notes.size(); ++i) {
        const Tick t = notes[i].start_tick;
        if (t < cs_first || t >= cs_last)
          continue;
        if (!isStructuralAccent(harmonic_plan, t))
          continue;
        if (std::find(ticks.begin(), ticks.end(), t) == ticks.end())
          ticks.push_back(t);
      }
      std::sort(ticks.begin(), ticks.end());

      bool non_invertible = false;
      for (Tick t : ticks) {
        if (non_invertible)
          break;
        for (VoiceId cs : cs_voices) {
          const std::uint8_t cs_pitch = onset_index.pitchAt(cs, t);
          if (cs_pitch == 0)
            continue;
          for (VoiceId sa : sa_voices) {
            if (sa == cs)
              continue;
            const std::uint8_t sa_pitch = onset_index.pitchAt(sa, t);
            if (sa_pitch == 0)
              continue;
            const int interval = static_cast<int>(cs_pitch) - static_cast<int>(sa_pitch);
            // A perfect fifth (and its compound forms) inverts to a fourth.
            if (isPerfectFifth(interval)) {
              non_invertible = true;
              break;
            }
          }
          if (non_invertible)
            break;
        }
      }
      if (non_invertible) {
        ValidationFailure observation;
        observation.rule_id = "countersubject_invertible";
        observation.kind = FailKind::MusicalFail;
        report.informational.push_back(observation);
      }
    }
  }

  if (!report.failures.empty()) {
    report.status = ValidationStatus::FailedSpan;
  }
  return report;
}

}  // namespace bach::composer
