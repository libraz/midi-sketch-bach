#include "composer/candidate_search.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <vector>

#include "composer/chord_voicing.h"
#include "composer/motif_ops.h"
#include "composer/rule_helpers.h"
#include "harmony/cadence_vocabulary.h"

namespace bach::composer {

namespace {

constexpr Tick kQuarter = kTicksPerBeat;

// Returns the chord triad pitch classes (0..11) for a ChordEvent.
std::array<std::uint8_t, 3> triadPitchClasses(const ChordEvent& chord) {
  std::uint8_t third_offset = 4;  // Major
  std::uint8_t fifth_offset = 7;
  switch (chord.quality) {
    case ChordQuality::Major:
    case ChordQuality::Major7:
    case ChordQuality::Dominant7:
      third_offset = 4;
      fifth_offset = 7;
      break;
    case ChordQuality::Minor:
    case ChordQuality::Minor7:
      third_offset = 3;
      fifth_offset = 7;
      break;
    case ChordQuality::Diminished:
    case ChordQuality::HalfDiminished7:
    case ChordQuality::Diminished7:
      third_offset = 3;
      fifth_offset = 6;
      break;
    case ChordQuality::Augmented:
      third_offset = 4;
      fifth_offset = 8;
      break;
  }
  return {
      static_cast<std::uint8_t>(chord.root_pc % 12),
      static_cast<std::uint8_t>((chord.root_pc + third_offset) % 12),
      static_cast<std::uint8_t>((chord.root_pc + fifth_offset) % 12),
  };
}

const ChordEvent& activeChord(const HarmonicPlan& plan, Tick at) {
  // chords assumed non-empty and sorted by start_tick.
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

// P7 helper: set the four P7 provenance bits on `rules` when the
// active chord opts into the strict regime (has_degree=true). Caller
// passes `pc` (candidate pitch class) and `is_chord_tone` so the
// helper doesn't recompute triad arithmetic.
//
//   ChordToneRoman  — set when the candidate is a chord tone of a
//                     degree-tagged chord. Stricter sibling of
//                     RuleBit::ChordTone (which fires regardless of
//                     has_degree).
//   InversionLabel  — set when the candidate's pitch class matches the
//                     chord's declared bass pc (i.e. the candidate
//                     could serve as the bass for the inversion). The
//                     bit fires per-voice; whichever voice carries the
//                     bass note will be the one that lights the bit.
//   DoublingChecked — set unconditionally inside a has_degree chord
//                     region: the doubling rules in the Validator (no
//                     leading-tone double, no 7th double) sweep the
//                     tick.
//   SpacingChecked  — set unconditionally inside a has_degree chord
//                     region: spacing rule sweeps the tick.
void applyP7Bits(RuleIdMask& rules, const ChordEvent& chord, std::uint8_t pc, bool is_chord_tone) {
  if (!chord.has_degree)
    return;
  rules |= 1ull << RuleBit::DoublingChecked;
  rules |= 1ull << RuleBit::SpacingChecked;
  if (is_chord_tone)
    rules |= 1ull << RuleBit::ChordToneRoman;
  if (pc == bassPitchClassFor(chord))
    rules |= 1ull << RuleBit::InversionLabel;
}

// P8 helper: set the four P8 provenance bits when the surrounding
// context matches each idiom.
//
//   ModulationCommitted        — the active chord sits at or after a
//                                ModulationEvent boundary (the plan has
//                                committed to a new key area, and this
//                                candidate's pitch is participating in
//                                that area).
//   SecondaryDominantResolved  — the active chord is the resolution of
//                                a previous secondary dominant: the
//                                most recent chord with has_secondary_of=
//                                true (strictly before the active chord)
//                                declares secondary_of equal to the
//                                active chord's degree.
//   PicardyThird               — the active chord is the final tonic
//                                with is_picardy=true and the candidate
//                                lands on the major third (root + 4).
//   ModalMixture               — the active chord declares is_borrowed=
//                                true (a parallel-mode loan) and the
//                                candidate is a chord tone of that
//                                chord.
void applyP8Bits(RuleIdMask& rules, const HarmonicPlan& plan, const ChordEvent& chord,
                 std::uint8_t pc, bool is_chord_tone) {
  for (const auto& mod : plan.modulations) {
    if (mod.tick <= chord.start_tick) {
      rules |= 1ull << RuleBit::ModulationCommitted;
      break;
    }
  }
  const ChordEvent* prev = nullptr;
  for (const auto& c : plan.chords) {
    if (c.start_tick >= chord.start_tick)
      break;
    if (c.has_secondary_of)
      prev = &c;
  }
  if (prev != nullptr && chord.has_degree && prev->secondary_of == chord.degree) {
    rules |= 1ull << RuleBit::SecondaryDominantResolved;
  }
  if (chord.is_picardy) {
    const std::uint8_t major_third_pc = static_cast<std::uint8_t>((chord.root_pc + 4) % 12);
    if (pc == major_third_pc) {
      rules |= 1ull << RuleBit::PicardyThird;
    }
  }
  if (chord.is_borrowed && is_chord_tone) {
    rules |= 1ull << RuleBit::ModalMixture;
  }
}

// Imports of shared rule primitives (defined in composer/rule_helpers.cpp).
// Local re-exports keep the existing call sites unchanged while routing
// all rule semantics through the single shared implementation.
using rule_helpers::createsCrossRelation;
using rule_helpers::createsHiddenParallelPerfect;
using rule_helpers::createsParallelPerfect;
using rule_helpers::createsVerticalDissonance;
using rule_helpers::createsVoiceCrossing;
using rule_helpers::isLeadingTone;
using rule_helpers::isStrongBeat;
using rule_helpers::sameVoiceStartingAt;
using rule_helpers::voicePitchAt;

// Guard form: returns true if `previous` is not a leading tone OR
// `candidate` is a valid stepwise upward resolution. Used by the
// candidate enumerator's "may I pick this next pitch?" gate.
bool resolvesLeadingTone(std::uint8_t previous, int candidate, const HarmonicPlan& plan) {
  if (!isLeadingTone(previous, plan))
    return true;
  return rule_helpers::isLeadingToneResolution(previous, candidate, plan);
}

// Cross-span lookahead context for sameVoiceStartingAt: when a Carrier
// (SubjectCarrier or AnswerCarrier) span has already been placed at the
// next tick, the current weak non-chord-tone pick must remain within ±2
// semitones of that fixed pitch or the Validator's `unprepared_dissonance`
// rule will fire on the last weak position of the preceding Compose span.

bool hasContraryMotion(const std::vector<NoteEvent>& placed, VoiceId candidate_voice,
                       std::uint8_t candidate_pitch, Tick cur_tick, std::uint8_t prev_pitch,
                       Tick prev_tick) {
  if (prev_pitch == 0)
    return false;
  const int this_motion = static_cast<int>(candidate_pitch) - static_cast<int>(prev_pitch);
  if (this_motion == 0)
    return false;

  std::vector<VoiceId> other_voices;
  for (const auto& n : placed) {
    if (n.voice == candidate_voice)
      continue;
    if (std::find(other_voices.begin(), other_voices.end(), n.voice) == other_voices.end()) {
      other_voices.push_back(n.voice);
    }
  }
  for (VoiceId ov : other_voices) {
    const std::uint8_t op_now = voicePitchAt(placed, ov, cur_tick);
    const std::uint8_t op_prev = voicePitchAt(placed, ov, prev_tick);
    if (op_now == 0 || op_prev == 0)
      continue;
    const int other_motion = static_cast<int>(op_now) - static_cast<int>(op_prev);
    if (other_motion == 0)
      continue;
    if ((this_motion > 0 && other_motion < 0) || (this_motion < 0 && other_motion > 0)) {
      return true;
    }
  }
  return false;
}

const CadenceCell* cadenceCellAt(const Material& material, Tick tick) {
  for (const auto& cell : material.cadence_cells) {
    if (cell.approach_tick == tick || cell.cadence_tick == tick)
      return &cell;
  }
  return nullptr;
}

}  // namespace

std::vector<Candidate> CandidateSearch::enumerate(const Span& span,
                                                  const HarmonicPlan& harmonic_plan,
                                                  const Material& material,
                                                  const CandidateContext& context) const {
  std::vector<Candidate> out;

  // Diatonic pitch-class mask for the key declared by the HarmonicPlan.
  // Used as a tiny chromatic penalty at eighth-note weak positions so
  // diatonic step approaches outrank chromatic alternatives of the same
  // step distance. The penalty is small (0.05) so genuinely required
  // chromatic motion is still reachable when no diatonic alternative
  // survives the rule cascade.
  std::array<bool, 12> diatonic_pc = {};
  {
    static constexpr int kMajorOffsets[7] = {0, 2, 4, 5, 7, 9, 11};
    static constexpr int kMinorOffsets[7] = {0, 2, 3, 5, 7, 8, 10};
    const auto* offsets = harmonic_plan.is_minor ? kMinorOffsets : kMajorOffsets;
    for (int i = 0; i < 7; ++i) {
      diatonic_pc[(harmonic_plan.tonic_pc + offsets[i]) % 12] = true;
    }
  }

  if (span.intent == VoiceIntent::SubjectCarrier || span.intent == VoiceIntent::AnswerCarrier ||
      span.intent == VoiceIntent::CountersubjectCarrier) {
    // Replay material verbatim. One Candidate per MaterialNote that falls
    // inside the span window. Score = 1.0. Sets bit 0 of satisfied_rules
    // (ChordTone) when the material pitch matches the triad of the chord
    // active at its onset, so the Composer's leave-side passing-tone
    // flag stays accurate across a Carrier→Compose span boundary.
    //
    // Source list:
    //   SubjectCarrier         → material.subject
    //   AnswerCarrier          → material.tonal_answer (if use_tonal_answer)
    //                            otherwise material.answer (real answer)
    //   CountersubjectCarrier  → material.countersubject
    // All three carry the same verbatim semantics (no candidate search,
    // score = 1.0).
    const std::vector<MaterialNote>* source_ptr = &material.subject;
    MaterialFragment fragment = MaterialFragment::Subject;
    bool emit_tonal_answer_bit = false;
    bool emit_countersubject_bit = false;
    if (span.intent == VoiceIntent::AnswerCarrier) {
      if (material.use_tonal_answer && !material.tonal_answer.empty()) {
        source_ptr = &material.tonal_answer;
        fragment = MaterialFragment::TonalAnswer;
        emit_tonal_answer_bit = true;
      } else {
        source_ptr = &material.answer;
        fragment = MaterialFragment::Answer;
      }
    } else if (span.intent == VoiceIntent::CountersubjectCarrier) {
      source_ptr = &material.countersubject;
      fragment = MaterialFragment::Countersubject;
      emit_countersubject_bit = true;
    }
    const auto& source = *source_ptr;
    for (std::size_t idx = 0; idx < source.size(); ++idx) {
      const auto& mnote = source[idx];
      if (mnote.start_tick < span.start_tick)
        continue;
      if (mnote.start_tick >= span.end_tick)
        break;
      Candidate c;
      c.start_tick = mnote.start_tick;
      c.duration = mnote.duration;
      c.pitch = mnote.pitch;
      c.score = 1.0f;
      c.satisfied_rules = 0;
      const ChordEvent& chord_here = activeChord(harmonic_plan, mnote.start_tick);
      const auto triad_here = triadPitchClasses(chord_here);
      const std::uint8_t pc_m = static_cast<std::uint8_t>(mnote.pitch % 12);
      const bool is_triad_m =
          (pc_m == triad_here[0] || pc_m == triad_here[1] || pc_m == triad_here[2]);
      if (is_triad_m) {
        c.satisfied_rules |= 1ull << RuleBit::ChordTone;
      }
      applyP7Bits(c.satisfied_rules, chord_here, pc_m, is_triad_m);
      applyP8Bits(c.satisfied_rules, harmonic_plan, chord_here, pc_m, is_triad_m);
      if (const CadenceCell* cadence_cell = cadenceCellAt(material, mnote.start_tick);
          cadence_cell != nullptr) {
        c.satisfied_rules |= 1ull << RuleBit::CadenceCellCommitted;
        if (mnote.start_tick == cadence_cell->cadence_tick) {
          c.satisfied_rules |= 1ull << RuleBit::CadenceVoiceLeadingChecked;
        }
      }
      for (const auto& marker : material.leading_tone_markers) {
        if (marker.fragment == fragment && marker.resolution_index == idx) {
          c.satisfied_rules |= 1ull << RuleBit::LeadingToneResolved;
          break;
        }
      }
      if (emit_tonal_answer_bit) {
        c.satisfied_rules |= 1ull << RuleBit::TonalAnswerMapped;
      }
      if (emit_countersubject_bit) {
        c.satisfied_rules |= 1ull << RuleBit::CountersubjectActive;
      }
      // P9 ImitationEntryMatched: set on idx==0 (the entry note) when
      // any declared ImitationEntry names this fragment as leader or
      // follower. The Validator's imitation_entry_match rule checks
      // the actual distance and interval against the declaration.
      if (idx == 0) {
        for (const auto& entry : material.imitation_entries) {
          if (entry.leader_fragment == fragment || entry.follower_fragment == fragment) {
            c.satisfied_rules |= 1ull << RuleBit::ImitationEntryMatched;
            break;
          }
        }
      }
      out.push_back(c);
    }
    return out;
  }

  if (span.intent == VoiceIntent::Episode) {
    // Replay each EpisodeFragment that targets this span's voice and
    // whose derived note window falls inside this span. Emits the full
    // transformed subject motif verbatim with EpisodeMotifSourced bit
    // set on every note so the Validator (and downstream provenance
    // analysis) can assert the span really came from a motif derivation.
    //
    // Multiple fragments per span are allowed; each is independently
    // anchored at fragment.target_start_tick. Fragments whose derived
    // notes spill past span.end_tick are skipped so the search never
    // emits notes outside the span window (the Composer assumes Span
    // bounds are authoritative).
    for (const auto& frag : material.episodes) {
      if (frag.voice != span.voice)
        continue;
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
      auto derived = motif_ops::applyTransform(
          source_slice, static_cast<motif_ops::EpisodeMotifTransform>(frag.transform),
          frag.target_start_tick, frag.invert_pivot,
          (static_cast<motif_ops::EpisodeMotifTransform>(frag.transform) ==
           motif_ops::EpisodeMotifTransform::Augment)
              ? frag.augment_factor
              : frag.diminish_factor);
      for (const auto& n : derived) {
        if (n.start_tick < span.start_tick)
          continue;
        if (n.start_tick >= span.end_tick)
          break;
        Candidate c;
        c.start_tick = n.start_tick;
        c.duration = n.duration;
        c.pitch = n.pitch;
        c.score = 1.0f;
        c.satisfied_rules = 1ull << RuleBit::EpisodeMotifSourced;
        // Chord-tone bit so passing-tone tracking on the next Compose
        // span (if any) reads correctly.
        const ChordEvent& chord_here = activeChord(harmonic_plan, n.start_tick);
        const auto triad_here = triadPitchClasses(chord_here);
        const std::uint8_t pc_m = static_cast<std::uint8_t>(n.pitch % 12);
        const bool is_triad_m =
            (pc_m == triad_here[0] || pc_m == triad_here[1] || pc_m == triad_here[2]);
        if (is_triad_m) {
          c.satisfied_rules |= 1ull << RuleBit::ChordTone;
        }
        applyP7Bits(c.satisfied_rules, chord_here, pc_m, is_triad_m);
        applyP8Bits(c.satisfied_rules, harmonic_plan, chord_here, pc_m, is_triad_m);
        out.push_back(c);
      }
    }
    return out;
  }

  if (span.intent == VoiceIntent::SuspensionCarrier) {
    // Replay each SuspensionPattern that targets this span's voice and
    // falls inside the span window. Emits exactly three notes
    // (preparation, suspension, resolution) per pattern. Score = 1.0.
    // Provenance bits SuspensionPrepared and SuspensionResolved are set
    // on the resolution note so the Validator and downstream gates can
    // assert that prep/sus/res actually shipped (not just intended).
    Tick prep_duration = kQuarter;
    Tick sus_duration = kQuarter;
    for (const auto& pattern : material.suspension_patterns) {
      if (pattern.voice != span.voice)
        continue;
      if (pattern.preparation_tick < span.start_tick)
        continue;
      if (pattern.resolution_tick >= span.end_tick)
        continue;
      // Note durations follow tick distances exactly so the held-over
      // dissonance reads as a tied/sustained event under the validator's
      // sounding-pitch lookup. Resolution duration falls back to one beat
      // unless the next pattern occupies that slot (left to Material to
      // arrange — Composer does not infer beyond the explicit ticks).
      prep_duration = pattern.suspension_tick - pattern.preparation_tick;
      sus_duration = pattern.resolution_tick - pattern.suspension_tick;
      Candidate prep;
      prep.start_tick = pattern.preparation_tick;
      prep.duration = prep_duration;
      prep.pitch = pattern.preparation_pitch;
      prep.score = 1.0f;
      prep.satisfied_rules = 1ull << RuleBit::SuspensionPrepared;
      out.push_back(prep);
      Candidate sus;
      sus.start_tick = pattern.suspension_tick;
      sus.duration = sus_duration;
      sus.pitch = pattern.suspension_pitch;
      sus.score = 1.0f;
      sus.satisfied_rules = 0;
      out.push_back(sus);
      Candidate res;
      res.start_tick = pattern.resolution_tick;
      res.duration = kQuarter;
      res.pitch = pattern.resolution_pitch;
      res.score = 1.0f;
      res.satisfied_rules = 1ull << RuleBit::SuspensionResolved;
      out.push_back(res);
    }
    return out;
  }

  if (span.intent == VoiceIntent::FortspinnungSpan) {
    // Replay each SequenceTemplate that targets this span's voice and
    // whose expanded note window falls inside this span. Emits the
    // seed motif followed by num_steps-1 transposed copies (each
    // shifted by step_length_ticks*k and step_offset_semis*k). All
    // notes carry FortspinnungSourced; steps 1..N-1 additionally carry
    // SequenceStep. P9 sequence patterns:
    //   DescendingFifths → -7 semis per step.
    //   DescendingStep   → -2 semis per step.
    //   AscendingStep    → +2 semis per step.
    auto step_semis = [](SequencePattern pattern) -> int {
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
      if (tmpl.voice != span.voice)
        continue;
      if (tmpl.seed_pitches.empty())
        continue;
      if (tmpl.seed_durations.size() != tmpl.seed_pitches.size())
        continue;
      const int offset = step_semis(tmpl.pattern);
      Tick local_offset = 0;
      for (std::size_t i = 0; i < tmpl.seed_pitches.size(); ++i) {
        local_offset += tmpl.seed_durations[i];
      }
      const Tick step_stride = tmpl.step_length_ticks > 0 ? tmpl.step_length_ticks : local_offset;
      for (std::uint8_t k = 0; k < tmpl.num_steps; ++k) {
        Tick beat_cursor = tmpl.target_start_tick + static_cast<Tick>(k) * step_stride;
        for (std::size_t i = 0; i < tmpl.seed_pitches.size(); ++i) {
          if (beat_cursor < span.start_tick) {
            beat_cursor += tmpl.seed_durations[i];
            continue;
          }
          if (beat_cursor >= span.end_tick)
            break;
          const int transposed =
              static_cast<int>(tmpl.seed_pitches[i]) + offset * static_cast<int>(k);
          if (transposed < 0 || transposed > 127) {
            beat_cursor += tmpl.seed_durations[i];
            continue;
          }
          Candidate c;
          c.start_tick = beat_cursor;
          c.duration = tmpl.seed_durations[i];
          c.pitch = static_cast<std::uint8_t>(transposed);
          c.score = 1.0f;
          c.satisfied_rules = 1ull << RuleBit::FortspinnungSourced;
          if (k > 0) {
            c.satisfied_rules |= 1ull << RuleBit::SequenceStep;
          }
          const ChordEvent& chord_here = activeChord(harmonic_plan, beat_cursor);
          const auto triad_here = triadPitchClasses(chord_here);
          const std::uint8_t pc_f = static_cast<std::uint8_t>(c.pitch % 12);
          const bool is_triad_f =
              (pc_f == triad_here[0] || pc_f == triad_here[1] || pc_f == triad_here[2]);
          if (is_triad_f) {
            c.satisfied_rules |= 1ull << RuleBit::ChordTone;
          }
          applyP7Bits(c.satisfied_rules, chord_here, pc_f, is_triad_f);
          applyP8Bits(c.satisfied_rules, harmonic_plan, chord_here, pc_f, is_triad_f);
          out.push_back(c);
          beat_cursor += tmpl.seed_durations[i];
        }
      }
    }
    return out;
  }

  if (span.intent == VoiceIntent::PedalCarrier) {
    // Replay each PedalPointDecl that targets this span's voice and whose
    // held note falls inside the span window. Emits exactly one sustained
    // note carrying PedalCommitted (plus ChordTone/P7/P8 bits so any
    // adjacent Compose span reads the boundary correctly).
    for (const auto& pedal : material.pedal_points) {
      if (pedal.voice != span.voice)
        continue;
      if (pedal.start_tick < span.start_tick)
        continue;
      if (pedal.start_tick >= span.end_tick)
        continue;
      Candidate c;
      c.start_tick = pedal.start_tick;
      c.duration = pedal.duration;
      c.pitch = pedal.pitch;
      c.score = 1.0f;
      c.satisfied_rules = 1ull << RuleBit::PedalCommitted;
      const ChordEvent& chord_here = activeChord(harmonic_plan, pedal.start_tick);
      const auto triad_here = triadPitchClasses(chord_here);
      const std::uint8_t pc_p = static_cast<std::uint8_t>(c.pitch % 12);
      const bool is_triad_p =
          (pc_p == triad_here[0] || pc_p == triad_here[1] || pc_p == triad_here[2]);
      if (is_triad_p) {
        c.satisfied_rules |= 1ull << RuleBit::ChordTone;
      }
      applyP7Bits(c.satisfied_rules, chord_here, pc_p, is_triad_p);
      applyP8Bits(c.satisfied_rules, harmonic_plan, chord_here, pc_p, is_triad_p);
      out.push_back(c);
    }
    return out;
  }

  if (span.intent == VoiceIntent::MiddleEntryCarrier ||
      span.intent == VoiceIntent::StrettoCarrier || span.intent == VoiceIntent::CodaCarrier ||
      span.intent == VoiceIntent::SubjectCarrierAugmented ||
      span.intent == VoiceIntent::SubjectCarrierDiminished ||
      span.intent == VoiceIntent::SubjectCarrierInverted) {
    // P11 development carriers: verbatim Material replay from a per-intent
    // source vector, stamping one provenance bit. Register safety (no
    // voice crossing) is the fixture's responsibility; because every P11
    // carrier is NoteSource::Material, the Validator's vertical/parallel
    // rules skip pairs where both notes are Material.
    const std::vector<MaterialNote>* source = nullptr;
    RuleBit bit = RuleBit::MiddleEntryCommitted;
    if (span.intent == VoiceIntent::MiddleEntryCarrier) {
      for (const auto& decl : material.middle_entries) {
        if (decl.voice == span.voice) {
          source = &decl.notes;
          break;
        }
      }
      bit = RuleBit::MiddleEntryCommitted;
    } else if (span.intent == VoiceIntent::StrettoCarrier) {
      for (const auto& decl : material.stretto_entries) {
        if (decl.follower_voice == span.voice) {
          source = &decl.follower_notes;
          break;
        }
      }
      bit = RuleBit::StrettoCommitted;
    } else if (span.intent == VoiceIntent::CodaCarrier) {
      for (const auto& decl : material.coda_extensions) {
        if (decl.voice == span.voice) {
          source = &decl.notes;
          break;
        }
      }
      bit = RuleBit::CodaCommitted;
    } else {
      // One of the three subject-variant intents.
      for (const auto& decl : material.subject_variants) {
        if (decl.voice == span.voice) {
          source = &decl.notes;
          break;
        }
      }
      bit = RuleBit::SubjectVariantApplied;
    }
    if (source == nullptr)
      return out;
    for (const auto& mnote : *source) {
      if (mnote.start_tick < span.start_tick)
        continue;
      if (mnote.start_tick >= span.end_tick)
        break;
      Candidate c;
      c.start_tick = mnote.start_tick;
      c.duration = mnote.duration;
      c.pitch = mnote.pitch;
      c.score = 1.0f;
      c.satisfied_rules = 1ull << bit;
      const ChordEvent& chord_here = activeChord(harmonic_plan, mnote.start_tick);
      const auto triad_here = triadPitchClasses(chord_here);
      const std::uint8_t pc_m = static_cast<std::uint8_t>(mnote.pitch % 12);
      const bool is_triad_m =
          (pc_m == triad_here[0] || pc_m == triad_here[1] || pc_m == triad_here[2]);
      if (is_triad_m) {
        c.satisfied_rules |= 1ull << RuleBit::ChordTone;
      }
      applyP7Bits(c.satisfied_rules, chord_here, pc_m, is_triad_m);
      applyP8Bits(c.satisfied_rules, harmonic_plan, chord_here, pc_m, is_triad_m);
      out.push_back(c);
    }
    return out;
  }

  if (span.intent == VoiceIntent::RhythmCarrier) {
    // P12 rhythm carrier: verbatim replay of every RhythmFragment that
    // targets this span's voice and whose notes fall inside the span
    // window. Each fragment's feature tag selects a provenance bit; a note
    // whose onset lands on a declared phrase start additionally carries
    // PhrasePeriodicityKept. The rhythm (dotted / syncopated / hemiola /
    // upbeat) lives in the fragment's note durations and onsets.
    for (const auto& frag : material.rhythm_fragments) {
      if (frag.voice != span.voice)
        continue;
      for (const auto& mnote : frag.notes) {
        if (mnote.start_tick < span.start_tick)
          continue;
        if (mnote.start_tick >= span.end_tick)
          continue;
        Candidate c;
        c.start_tick = mnote.start_tick;
        c.duration = mnote.duration;
        c.pitch = mnote.pitch;
        c.score = 1.0f;
        c.satisfied_rules = 0;
        switch (frag.feature) {
          case RhythmFragment::Feature::Anacrusis:
            c.satisfied_rules |= 1ull << RuleBit::AnacrusisActive;
            break;
          case RhythmFragment::Feature::Hemiola:
            c.satisfied_rules |= 1ull << RuleBit::HemiolaInserted;
            break;
          case RhythmFragment::Feature::Recurrence:
            c.satisfied_rules |= 1ull << RuleBit::RhythmicMotifRecurrence;
            break;
          case RhythmFragment::Feature::Dotted:
          case RhythmFragment::Feature::Syncopation:
            break;  // rhythm is the feature; no dedicated bit.
        }
        for (Tick start : material.phrase_structure.phrase_start_ticks) {
          if (start == mnote.start_tick) {
            c.satisfied_rules |= 1ull << RuleBit::PhrasePeriodicityKept;
            break;
          }
        }
        const ChordEvent& chord_here = activeChord(harmonic_plan, mnote.start_tick);
        const auto triad_here = triadPitchClasses(chord_here);
        const std::uint8_t pc_r = static_cast<std::uint8_t>(c.pitch % 12);
        const bool is_triad_r =
            (pc_r == triad_here[0] || pc_r == triad_here[1] || pc_r == triad_here[2]);
        if (is_triad_r) {
          c.satisfied_rules |= 1ull << RuleBit::ChordTone;
        }
        applyP7Bits(c.satisfied_rules, chord_here, pc_r, is_triad_r);
        applyP8Bits(c.satisfied_rules, harmonic_plan, chord_here, pc_r, is_triad_r);
        out.push_back(c);
      }
    }
    return out;
  }

  // Compose spans: lay down one note per beat aligned to span.start_tick.
  // Pitch picked from current chord tones near voice_center. Spans with
  // Subdivision::Eighth produce two notes per beat instead — the rest
  // of the rule cascade (vertical, leap, passing-tone) operates per
  // note position and is stride-agnostic.
  const Tick stride = (span.subdivision == Subdivision::Eighth) ? kQuarter / 2 : kQuarter;

  // Local cursor used for vertical (other-voice) parallel checks.
  // Holds the candidate this enumerate() call just committed, or the
  // caller-supplied anchor for the first iteration. The "previous tick"
  // is the actual start_tick of that committed candidate (not its end),
  // so the validator-style lookup at prev_tick observes the same pitch
  // configuration.
  std::uint8_t parallel_prev_pitch = context.prev_pitch;
  Tick parallel_prev_tick = context.prev_end_tick > 0 ? context.prev_end_tick - stride : 0;
  bool have_parallel_anchor = context.prev_pitch != 0;

  // For leap-resolution. We need both the previous pitch and the one
  // before it to detect a leap that the candidate must not continue
  // with another leap. Seed the local cursor from the context so the
  // rule applies across span boundaries, then update on each commit.
  std::uint8_t pre_prev_pitch_local = context.pre_prev_pitch;
  std::uint8_t prev_pitch_local = context.prev_pitch;

  // Leave-side passing-tone tracker. True iff the most recent commit
  // (in this span, or carried from the previous span via context)
  // landed on a non-chord-tone, so the next pitch must be a step
  // (≤2) from prev_pitch_local. Without this, the search can pick a
  // non-triad passing tone whose next-pitch choice ignores the
  // approach-rule recursion, violating the Validator's
  // `unprepared_dissonance` rule. Updated after each commit.
  bool prev_was_pt_local = context.prev_was_passing_tone;

  for (Tick t = span.start_tick; t < span.end_tick; t += stride) {
    const ChordEvent& chord = activeChord(harmonic_plan, t);
    const auto triad = triadPitchClasses(chord);
    const CadenceCell* cadence_cell = cadenceCellAt(material, t);
    const bool force_bass_cadence_pc = cadence_cell != nullptr && span.voice > 0;
    const std::uint8_t forced_cadence_pc =
        (cadence_cell != nullptr && t == cadence_cell->approach_tick)
            ? cadence_cell->bass_approach_pc
            : ((cadence_cell != nullptr) ? cadence_cell->bass_cadence_pc : 0);

    // Enumerate triad-tone pitches in [voice_center - 7, voice_center + 12].
    int best_pitch = -1;
    float best_score = -1.0f;
    RuleIdMask best_rules = 0;
    for (int p = context.voice_center - 7; p <= context.voice_center + 12; ++p) {
      if (p < 0 || p > 127)
        continue;
      const std::uint8_t pc = static_cast<std::uint8_t>(p % 12);
      if (force_bass_cadence_pc && pc != forced_cadence_pc)
        continue;
      const bool is_triad = (pc == triad[0]) || (pc == triad[1]) || (pc == triad[2]);
      const bool strong = isStrongBeat(t);
      if (prev_pitch_local != 0 && !resolvesLeadingTone(prev_pitch_local, p, harmonic_plan)) {
        continue;
      }
      if (strong && !is_triad)
        continue;  // strong-beat consonance rule

      // Vertical rule: reject candidate that crosses any already-placed
      // voice (lower voice index must stay above higher voice index).
      if (context.placed_notes != nullptr &&
          createsVoiceCrossing(*context.placed_notes, span.voice, static_cast<std::uint8_t>(p),
                               t)) {
        continue;
      }

      // P7 spacing pre-filter. When the active chord declares
      // has_degree=true, reject any candidate that would push an
      // upper-voice adjacent pair past an octave. We check only the
      // pairs the Validator's spacing rule covers (top N-2 pairs in
      // an N-voice texture). Without this filter the Composer can
      // pick a candidate pitch that drops too low (a textbook
      // "open" spacing) which the Validator rejects after the fact
      // and forces a seed-level fail.
      if (chord.has_degree && context.placed_notes != nullptr) {
        bool spacing_violation = false;
        for (const auto& placed : *context.placed_notes) {
          if (placed.voice == span.voice)
            continue;
          if (placed.start_tick > t)
            continue;
          if (t >= placed.start_tick + placed.duration)
            continue;
          // Spacing check only applies to upper-voice pairs: skip
          // when the lower-indexed voice of the pair (this voice or
          // the placed voice) is the bottom voice of the texture.
          // Voice id convention: V0 = top, V_{N-1} = bottom.
          const VoiceId hi_voice = std::min(placed.voice, span.voice);
          const VoiceId lo_voice = std::max(placed.voice, span.voice);
          // Only adjacent voice pairs constrained.
          if (lo_voice != hi_voice + 1)
            continue;
          // Skip the bottom-of-texture pair: spacing rule excludes
          // the lowest pair (V_{N-2} — V_{N-1}). The pair to exclude
          // has lo_voice == N - 1 (i.e. the bottom voice is in the
          // pair). For N = 3 only (V0, V1) is checked; (V1, V2) is
          // skipped because lo_voice = 2 = N - 1.
          if (context.num_voices >= 2 && lo_voice == context.num_voices - 1)
            continue;
          const int gap = std::abs(static_cast<int>(p) - static_cast<int>(placed.pitch));
          if (gap > 12) {
            spacing_violation = true;
            break;
          }
        }
        if (spacing_violation)
          continue;
      }

      // P7 doubling pre-filter. When the active chord declares
      // has_degree=true AND owns the leading tone (chord triad
      // contains tonic+11), reject any candidate whose pc matches
      // the leading tone if a previously-placed voice is already
      // sounding the leading tone at this tick. This keeps the
      // Composer from picking a doubled leading tone that the
      // Validator's `doubling_no_leading_tone` rule would later
      // reject. Without this, Phase7 carriers (whose pitches are
      // fixed Material) can coincide with a Compose voice landing on
      // B, fail validation, and bounce the seed.
      if (chord.has_degree && context.placed_notes != nullptr) {
        const std::uint8_t lt_pc = leadingTonePitchClass(harmonic_plan.tonic_pc);
        const bool chord_owns_lt =
            (triad[0] == lt_pc) || (triad[1] == lt_pc) || (triad[2] == lt_pc);
        if (chord_owns_lt && pc == lt_pc) {
          bool other_voice_has_lt = false;
          for (const auto& placed : *context.placed_notes) {
            if (placed.voice == span.voice)
              continue;
            if (placed.start_tick > t)
              continue;
            if (t >= placed.start_tick + placed.duration)
              continue;
            if (static_cast<std::uint8_t>(placed.pitch % 12) == lt_pc) {
              other_voice_has_lt = true;
              break;
            }
          }
          if (other_voice_has_lt)
            continue;
        }
      }

      // Vertical rule: on strong beats, candidate must form a
      // consonant interval with every already-placed voice that is
      // sounding at this tick. Weak beats apply a soft score penalty
      // instead (handled below in the score block) so passing-tone
      // dissonances remain reachable when no consonant option fits.
      const bool vertically_dissonant = context.placed_notes != nullptr &&
                                        createsVerticalDissonance(*context.placed_notes, span.voice,
                                                                  static_cast<std::uint8_t>(p), t);
      if (strong && vertically_dissonant) {
        continue;
      }

      // Vertical rule: reject candidate that creates parallel perfect
      // motion against any already-placed voice. Only attempted when
      // an other-voice context is supplied AND we have a valid prev
      // anchor in this voice.
      //
      // Cadence cells bypass the general parallel-perfect rule because
      // their bass pitch class is forced — accepting an occasional
      // parallel fifth into the cadence is preferred over failing the
      // span. Parallel octaves remain prohibited even under a cadence
      // cell (see createsParallelOctave below).
      if (!force_bass_cadence_pc && context.placed_notes != nullptr && have_parallel_anchor &&
          createsParallelPerfect(*context.placed_notes, span.voice, static_cast<std::uint8_t>(p), t,
                                 parallel_prev_pitch, parallel_prev_tick)) {
        continue;
      }
      if (context.placed_notes != nullptr && have_parallel_anchor &&
          rule_helpers::createsParallelOctave(*context.placed_notes, span.voice,
                                              static_cast<std::uint8_t>(p), t, parallel_prev_pitch,
                                              parallel_prev_tick)) {
        continue;
      }

      // P10 strong-beat perfect-4th pre-filter. Converts the Validator's
      // fourth_only_on_weak_beat rule into a confirming check: reject any
      // candidate that would form a perfect 4th (interval class 5) on a
      // strong beat with the sounding voice of an adjacent UPPER pair,
      // matching the Validator scoping (adjacent pair, bottom-of-texture
      // pair excluded). Without this filter the Composer can land an
      // upper-pair 4th on a downbeat that the Validator then rejects,
      // failing the whole span. A 4th inverts to a 5th under octave
      // inversion, so avoiding it preserves invertibility. Gated on
      // chord.has_degree to match the P7 spacing pre-filter scoping
      // (degree-tagged layouts carry a correct context.num_voices, which
      // the bottom-of-texture exclusion relies on).
      if (strong && chord.has_degree && context.placed_notes != nullptr) {
        bool forms_upper_pair_fourth = false;
        for (const auto& placed : *context.placed_notes) {
          if (placed.voice == span.voice)
            continue;
          if (placed.start_tick > t)
            continue;
          if (t >= placed.start_tick + placed.duration)
            continue;
          // Adjacent-pair scoping (V0 = top, V_{N-1} = bottom).
          const VoiceId hi_voice = std::min(placed.voice, span.voice);
          const VoiceId lo_voice = std::max(placed.voice, span.voice);
          if (lo_voice != hi_voice + 1)
            continue;
          // Exclude the bottom-of-texture pair (mirrors the Validator).
          if (context.num_voices >= 2 && lo_voice == context.num_voices - 1)
            continue;
          const int cls = std::abs(static_cast<int>(placed.pitch) - p) % 12;
          if (cls == 5) {
            forms_upper_pair_fourth = true;
            break;
          }
        }
        if (forms_upper_pair_fourth)
          continue;
      }
      if (!force_bass_cadence_pc && context.placed_notes != nullptr && have_parallel_anchor &&
          createsHiddenParallelPerfect(*context.placed_notes, span.voice,
                                       static_cast<std::uint8_t>(p), t, parallel_prev_pitch,
                                       parallel_prev_tick)) {
        continue;
      }
      if (!force_bass_cadence_pc && context.placed_notes != nullptr &&
          createsCrossRelation(*context.placed_notes, span.voice, static_cast<std::uint8_t>(p),
                               t)) {
        continue;
      }

      // Melodic rule: if the previous motion (pre_prev -> prev) was
      // a wide leap (>= P5), forbid a second wide leap from prev to
      // this candidate. The single-leap case stays unconstrained.
      if (!force_bass_cadence_pc && pre_prev_pitch_local != 0 && prev_pitch_local != 0) {
        const int delta_pre =
            static_cast<int>(prev_pitch_local) - static_cast<int>(pre_prev_pitch_local);
        const int delta_cur = p - static_cast<int>(prev_pitch_local);
        if (std::abs(delta_pre) >= 7 && std::abs(delta_cur) >= 7) {
          continue;
        }
      }

      // Passing-tone rule (approach side): a weak-beat non-chord tone
      // must be approached by step (<= 2 semis).
      if (!force_bass_cadence_pc && !strong && !is_triad && prev_pitch_local != 0) {
        if (std::abs(p - static_cast<int>(prev_pitch_local)) > 2) {
          continue;
        }
      }

      // Passing-tone rule (leave side): if the previous commit was a
      // non-chord-tone (passing tone), the current candidate must be
      // within 2 semis of prev_pitch_local. This is the in-cascade
      // counterpart of the Validator's `unprepared_dissonance` rule —
      // by enforcing it during enumeration we avoid generating
      // notes the Validator would later reject. Without this guard,
      // the search could pick a non-triad p at position k, then at
      // position k+1 pick a triad-tone 3+ semis away (because the
      // triad bonus dominates), violating the rule.
      if (!force_bass_cadence_pc && prev_was_pt_local && prev_pitch_local != 0) {
        if (std::abs(p - static_cast<int>(prev_pitch_local)) > 2) {
          continue;
        }
      }

      if (!force_bass_cadence_pc && isLeadingTone(static_cast<std::uint8_t>(p), harmonic_plan)) {
        const Tick t_next = t + stride;
        bool has_resolution = false;
        if (context.placed_notes != nullptr) {
          const std::uint8_t fixed_next =
              sameVoiceStartingAt(*context.placed_notes, span.voice, t_next);
          if (fixed_next != 0) {
            has_resolution =
                resolvesLeadingTone(static_cast<std::uint8_t>(p), fixed_next, harmonic_plan);
          }
        }
        if (!has_resolution && t_next < span.end_tick) {
          for (int q = p + 1; q <= p + 2; ++q) {
            if (!resolvesLeadingTone(static_cast<std::uint8_t>(p), q, harmonic_plan))
              continue;
            if (context.placed_notes != nullptr &&
                createsVoiceCrossing(*context.placed_notes, span.voice,
                                     static_cast<std::uint8_t>(q), t_next)) {
              continue;
            }
            if (context.placed_notes != nullptr &&
                createsVerticalDissonance(*context.placed_notes, span.voice,
                                          static_cast<std::uint8_t>(q), t_next)) {
              continue;
            }
            if (context.placed_notes != nullptr &&
                createsParallelPerfect(*context.placed_notes, span.voice,
                                       static_cast<std::uint8_t>(q), t_next,
                                       static_cast<std::uint8_t>(p), t)) {
              continue;
            }
            if (context.placed_notes != nullptr &&
                createsHiddenParallelPerfect(*context.placed_notes, span.voice,
                                             static_cast<std::uint8_t>(q), t_next,
                                             static_cast<std::uint8_t>(p), t)) {
              continue;
            }
            if (context.placed_notes != nullptr &&
                createsCrossRelation(*context.placed_notes, span.voice,
                                     static_cast<std::uint8_t>(q), t_next)) {
              continue;
            }
            const ChordEvent& q_chord = activeChord(harmonic_plan, t_next);
            const auto q_triad = triadPitchClasses(q_chord);
            const std::uint8_t q_pc = static_cast<std::uint8_t>(q % 12);
            const bool q_is_triad = q_pc == q_triad[0] || q_pc == q_triad[1] || q_pc == q_triad[2];
            if (!isStrongBeat(t_next) && !q_is_triad && context.placed_notes != nullptr) {
              const Tick q_next_tick = t_next + stride;
              const std::uint8_t fixed_after_q =
                  sameVoiceStartingAt(*context.placed_notes, span.voice, q_next_tick);
              if (fixed_after_q != 0 && std::abs(q - static_cast<int>(fixed_after_q)) > 2) {
                continue;
              }
            }
            has_resolution = true;
            break;
          }
        }
        if (!has_resolution) {
          continue;
        }
      }

      // Non-triad weak-pick lookahead. When the next position is a
      // strong beat (new bar / chord change), it must be a triad tone
      // of the new chord AND, by leave-side rule above, within ±2 of
      // p. If no triad tone of the next chord fits that ±2 window
      // AND survives vertical-dissonance AND parallel-perfect against
      // already-placed voices at t_next, p is a dead-end — picking
      // it would force the next position to emit nothing. Reject p
      // so the search falls back to a triad of THIS chord (which has
      // no leave-side restriction).
      //
      // Lookahead crosses span boundaries because the test plan often
      // aligns bar = span (so t_next == span.end_tick at the last
      // weak beat). Leave-side enforcement is already span-crossing
      // via CandidateContext::prev_was_passing_tone, so the lookahead
      // must agree.
      if (!force_bass_cadence_pc && !strong && !is_triad) {
        const Tick t_next = t + stride;
        // Cross-span fixed-next leave-side check: when a Carrier span
        // (SubjectCarrier or AnswerCarrier) has already placed a note
        // at t_next in this voice, the Validator's
        // `unprepared_dissonance` rule requires |p - fixed_next| ≤ 2.
        // The intra-span leave-side rule below (prev_was_pt_local +
        // approach-side) does not see this because t_next is in a
        // different span. Phase 4 V1 counterline bar 3 → V1
        // AnswerCarrier bar 4 is the canonical case.
        if (context.placed_notes != nullptr) {
          const std::uint8_t fixed_next =
              sameVoiceStartingAt(*context.placed_notes, span.voice, t_next);
          if (fixed_next != 0 && std::abs(p - static_cast<int>(fixed_next)) > 2) {
            continue;
          }
        }
        if (isStrongBeat(t_next)) {
          const ChordEvent& chord_next_lh = activeChord(harmonic_plan, t_next);
          const auto triad_next = triadPitchClasses(chord_next_lh);
          bool has_strong_followup = false;
          for (int q = p - 2; q <= p + 2; ++q) {
            if (q < 0 || q > 127)
              continue;
            const std::uint8_t pc_q = static_cast<std::uint8_t>(q % 12);
            const bool q_is_triad =
                (pc_q == triad_next[0]) || (pc_q == triad_next[1]) || (pc_q == triad_next[2]);
            if (!q_is_triad)
              continue;
            if (context.placed_notes != nullptr &&
                createsVerticalDissonance(*context.placed_notes, span.voice,
                                          static_cast<std::uint8_t>(q), t_next)) {
              continue;
            }
            // Parallel-perfect against placed voices at t_next, with
            // the candidate p (at current tick t) as the "prev" anchor.
            // Without this, the lookahead can claim a triad-tone
            // followup that the actual enumeration at t_next will
            // reject for parallel motion (observed: V2 58→60 across a
            // bar boundary forms a parallel P5 with V0 77→79).
            if (context.placed_notes != nullptr &&
                createsParallelPerfect(*context.placed_notes, span.voice,
                                       static_cast<std::uint8_t>(q), t_next,
                                       static_cast<std::uint8_t>(p), t)) {
              continue;
            }
            has_strong_followup = true;
            break;
          }
          if (!has_strong_followup)
            continue;
        }
      }

      float score = is_triad ? 0.8f : 0.4f;
      // Prefer pitches close to the voice center. Quarter mode uses
      // the original 0.01 weight; Eighth mode uses 0.025 so the
      // tessitura anchor outweighs the per-position prev_pitch_local
      // cursor below — without this stronger anchor at finer
      // resolution, the cursor can drift away from voice_center
      // across consecutive weak-eighth picks, producing descending
      // cascades (see three-voice Eighth V2 at bar 0 mid-bar before
      // this calibration).
      const float center_weight = (span.subdivision == Subdivision::Eighth) ? 0.025f : 0.01f;
      score -= center_weight * static_cast<float>(std::abs(p - context.voice_center));
      // Prefer pitches close to the immediately-preceding pitch. Uses
      // `prev_pitch_local` (the per-position cursor) rather than
      // `context.prev_pitch` (fixed at span entry) so the distance term
      // tracks the just-committed pitch inside a multi-position span.
      // Without this, a candidate's prev-distance is measured against
      // the span-entry pitch even after several intra-span commits —
      // which biases later positions toward the span-entry pitch and
      // can favor a leap back toward span entry over holding the
      // recently-committed pitch.
      if (prev_pitch_local != 0) {
        score -= 0.02f * static_cast<float>(std::abs(p - prev_pitch_local));
      }
      // Soft penalty for weak-beat vertical dissonance. Strong beats
      // are already hard-rejected above. The penalty needs to exceed
      // the prev-distance gap to a reachable consonant alternative, so
      // 0.15 (roughly seven semitones of prev-distance) is enough to
      // flip the choice when one exists but small enough that a fully
      // unreachable consonant set still lets a dissonant pick through.
      if (!strong && vertically_dissonant) {
        score -= 0.15f;
      }
      if (context.placed_notes != nullptr && have_parallel_anchor &&
          hasContraryMotion(*context.placed_notes, span.voice, static_cast<std::uint8_t>(p), t,
                            parallel_prev_pitch, parallel_prev_tick)) {
        score += 0.10f;
      }
      if (force_bass_cadence_pc) {
        score += 0.50f;
      }

      // P8 Picardy 3rd bias. The final picardy chord's identity is
      // carried by its major third (E natural in C major). Without
      // this bias the per-voice prev-distance heuristic pins every
      // voice on the chord root and the major third never sounds.
      // The bonus is small enough (0.05) to lose to a voice already
      // sitting on a chord tone close to its prev pitch, but large
      // enough to flip the choice when two chord tones tie on
      // prev-distance.
      if (chord.is_picardy) {
        const std::uint8_t major_third_pc = static_cast<std::uint8_t>((chord.root_pc + 4) % 12);
        if (static_cast<std::uint8_t>(pc) == major_third_pc) {
          score += 0.05f;
        }
      }

      // Quarter-mode unison-run penalty. When pre_prev and prev are
      // the same pitch (two consecutive unisons), a 3rd consecutive
      // unison candidate gets a 0.15 deduction so the broad step
      // bonus below can pull the search off the held pitch. Gated on
      // pre_prev != 0 to preserve the `QuarterSubdivisionKeepsHeldPitch`
      // regression (its single fixture has pre_prev=0, so 64 wins at
      // delta 0).
      if (span.subdivision == Subdivision::Quarter && !strong && pre_prev_pitch_local != 0 &&
          pre_prev_pitch_local == prev_pitch_local && p == static_cast<int>(prev_pitch_local)) {
        score -= 0.15f;
      }

      // Quarter-mode broad step bonus. Applies on every weak position
      // (not just unison-break) so the search prefers conjunct
      // passing-tone motion over triad-tone leaps whenever both are
      // feasible. Safe because:
      //   - Leave-side enforcement guarantees the non-triad's next
      //     pitch is ≤2 from p, so we don't generate validator-
      //     failing notes.
      //   - 1-step lookahead before strong beats rejects non-triad p
      //     when no triad of the next chord fits ±2 of p AND survives
      //     vertical-dissonance against placed voices.
      //
      // Bonus size 0.42 is the regression-budget maximum: the
      // `QuarterSubdivisionKeepsHeldPitch` fixture asserts unison 64
      // wins at prev=64, center=64; that gives unison score 0.80 vs
      // step candidate 0.40 + S - 0.03, so S < 0.43 preserves the
      // assertion. At 0.42 the unison still wins by 0.01 when no
      // vertical pressure is present, but any vertical penalty on
      // the unison flips the choice to a step.
      //
      // Applies to all voices: bass voices benefit too because the
      // alternative (holding a chord tone that becomes dissonant
      // when V0 moves) carries weak-beat soft penalty anyway, so a
      // diatonic step neighbor often outranks the dissonant unison.
      if (span.subdivision == Subdivision::Quarter && !strong && !is_triad &&
          prev_pitch_local != 0) {
        const int delta = std::abs(p - static_cast<int>(prev_pitch_local));
        if (delta == 1 || delta == 2) {
          score += 0.42f;
        }
      }

      // Chromatic-passing-tone penalty (Quarter, weak-position,
      // non-triad). Mirrors the Eighth-mode chromatic penalty: the
      // step bonus above lifts any non-triad step, including
      // non-diatonic ones; the 0.05 penalty keeps chromatic motion
      // reachable when no diatonic alternative survives the cascade
      // but lets the diatonic neighbor win every tie.
      if (span.subdivision == Subdivision::Quarter && !strong && !is_triad && !diatonic_pc[pc]) {
        score -= 0.05f;
      }

      // Eighth-note motion bias (two parts, only at Subdivision::Eighth):
      //
      // (a) Penalize weak-position unison. Without this, the score
      //     function picks the previous pitch whenever it is a triad
      //     tone near voice_center — the prev-distance penalty alone
      //     is 0 at delta 0. Bach eighth-note counterlines almost
      //     never sit on the same eighth twice in a row.
      //
      // (b) Bonus non-triad step approach. The triad bonus (0.40 gap
      //     between triad and non-triad starting scores) otherwise
      //     dominates, biasing eighth motion toward chord-tone leaps
      //     (m3, P4) rather than the stepwise passing motion that
      //     defines Bach idiom. A 0.40 bonus on non-triad candidates
      //     within a whole step of prev lifts genuine passing tones
      //     above triad-tone leaps of comparable distance.
      //
      // Both branches measure delta against `prev_pitch_local` (the
      // per-position cursor that advances on every commit), not
      // `context.prev_pitch` (which is fixed at span entry). Using the
      // local cursor is what lets the rule see position-to-position
      // motion inside a span; the span-entry value alone would mark
      // two consecutive eighths as non-unison whenever the second
      // happens to equal the span's entry pitch.
      //
      // Quarter subdivision keeps the original scoring: held pitches
      // at quarter resolution are musically valid (suspensions, pedal
      // tones, etc.), and quarter-level triad oscillation is fine.
      if (span.subdivision == Subdivision::Eighth && !strong && prev_pitch_local != 0) {
        const int delta = std::abs(p - static_cast<int>(prev_pitch_local));
        if (delta == 0) {
          score -= 0.15f;
        } else if (!is_triad && (delta == 1 || delta == 2)) {
          score += 0.40f;
        }
      }

      // Chromatic-passing-tone penalty (Eighth-only, weak-position).
      // The step bonus above lifts any non-triad pitch within a whole
      // step of prev; without further bias, a chromatic candidate
      // enumerated before a diatonic alternative wins on equal score
      // (loop iterates pitch ascending and uses strict `>`). The 0.05
      // penalty keeps chromatic motion reachable when the rule cascade
      // leaves no diatonic option, but lets diatonic steps win every
      // tie.
      if (span.subdivision == Subdivision::Eighth && !strong && !is_triad && !diatonic_pc[pc]) {
        score -= 0.05f;
      }

      RuleIdMask rules = 0;
      if (is_triad)
        rules |= 1ull << RuleBit::ChordTone;
      applyP7Bits(rules, chord, static_cast<std::uint8_t>(pc), is_triad);
      applyP8Bits(rules, harmonic_plan, chord, static_cast<std::uint8_t>(pc), is_triad);
      // P10 invertibility confirmation. Set InvertibleAt8va when we have
      // placed context AND this candidate does NOT form a perfect 4th
      // (class 5) on a strong beat with the sounding upper-ADJACENT
      // voice (V_{span.voice-1}). A strong-beat 4th in the upper pair
      // inverts to a 5th, so a clean candidate is one that avoids it.
      // The bit is a confirming check: the Composer already prefers
      // consonant strong-beat verticals, so this accrues on most
      // candidates rather than constraining selection.
      //
      // Scope: only the upper-adjacent pairs the P7 spacing / P10
      // validator rules cover (top N-2 pairs). The bottom pair
      // (V_{N-2}, V_{N-1}) is excluded, so the bass voice (the last
      // voice index) never lights the bit against its lower-adjacent
      // neighbor.
      const bool is_bottom_voice = context.num_voices > 0 && span.voice + 1 >= context.num_voices;
      if (context.placed_notes != nullptr && span.voice > 0 && !is_bottom_voice) {
        bool forms_strong_fourth = false;
        if (strong) {
          // Sounding pitch of the immediately-higher voice (V-1).
          const VoiceId upper_adjacent = static_cast<VoiceId>(span.voice - 1);
          std::uint8_t upper_pitch = 0;
          for (const auto& placed : *context.placed_notes) {
            if (placed.voice != upper_adjacent)
              continue;
            if (placed.start_tick > t)
              continue;
            if (t >= placed.start_tick + placed.duration)
              continue;
            upper_pitch = placed.pitch;
          }
          if (upper_pitch != 0) {
            const int cls = std::abs(static_cast<int>(upper_pitch) - p) % 12;
            forms_strong_fourth = (cls == 5);
          }
        }
        if (!forms_strong_fourth) {
          rules |= 1ull << RuleBit::InvertibleAt8va;
        }
      }
      if (strong && is_triad)
        rules |= 1ull << RuleBit::StrongBeatConsonance;
      if (context.prev_pitch > 0 && std::abs(p - context.prev_pitch) <= 4) {
        rules |= 1ull << RuleBit::SmallStep;
      }
      if (context.placed_notes != nullptr) {
        rules |= 1ull << RuleBit::ParallelPerfectChecked;
        rules |= 1ull << RuleBit::HiddenParallelChecked;
        rules |= 1ull << RuleBit::VoiceCrossingChecked;
        rules |= 1ull << RuleBit::CrossRelationChecked;
        // rule[7] = VerticalConsonanceChecked. Marks the candidate as
        // consonant against all currently-placed voices at this tick.
        // Strong beats are guaranteed (the rejection above would have
        // skipped a dissonant `p`); weak beats only set the bit when
        // they happen to avoid dissonance (i.e. the soft penalty did
        // not have to apply).
        if (!vertically_dissonant) {
          rules |= 1ull << RuleBit::VerticalConsonanceChecked;
        }
      }
      if (pre_prev_pitch_local != 0 && prev_pitch_local != 0) {
        rules |= 1ull << RuleBit::LeapResolutionChecked;
      }
      if (!strong && prev_pitch_local != 0) {
        rules |= 1ull << RuleBit::WeakBeatPassingChecked;
      }
      if (prev_pitch_local != 0 && isLeadingTone(prev_pitch_local, harmonic_plan)) {
        rules |= 1ull << RuleBit::LeadingToneResolved;
      }
      if (cadence_cell != nullptr) {
        rules |= 1ull << RuleBit::CadenceCellCommitted;
        if (t == cadence_cell->cadence_tick) {
          rules |= 1ull << RuleBit::CadenceVoiceLeadingChecked;
        }
      }

      if (score > best_score) {
        best_score = score;
        best_pitch = p;
        best_rules = rules;
      }
    }

    if (best_pitch < 0)
      continue;  // span saturated; caller handles

    Candidate c;
    c.start_tick = t;
    c.duration = stride;
    c.pitch = static_cast<std::uint8_t>(best_pitch);
    c.score = best_score;
    c.satisfied_rules = best_rules;
    out.push_back(c);

    parallel_prev_pitch = c.pitch;
    parallel_prev_tick = t;
    have_parallel_anchor = true;
    pre_prev_pitch_local = prev_pitch_local;
    prev_pitch_local = c.pitch;
    // The picked candidate is a triad-tone iff bit 0 of satisfied_rules
    // is set (rule[0] = ChordTone). Use that to update the leave-side
    // tracker — non-triad commits require the next position to be
    // within ±2.
    prev_was_pt_local = ((best_rules & (1ull << RuleBit::ChordTone)) == 0);
  }
  return out;
}

}  // namespace bach::composer
