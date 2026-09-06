#include "composer/candidate_search.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>

#include "composer/free_counterpoint_search.h"
#include "composer/motif_ops.h"
#include "composer/rule_helpers.h"

namespace bach::composer {

namespace {

constexpr Tick kQuarter = kTicksPerBeat;

// Harmonic primitives are shared with composer.cpp via rule_helpers; the
// local aliases keep the existing call sites (triadPitchClasses, activeChord)
// unchanged while routing through the single shared implementation.
using rule_helpers::activeChord;
using rule_helpers::applyP7Bits;
using rule_helpers::applyP8Bits;
using rule_helpers::cadenceCellAt;
using rule_helpers::triadPitchClasses;

// Builds one verbatim Candidate from a MaterialNote (pitch / tick / duration
// copied as-is, score = 1.0) with the standard baseline provenance bits:
// ChordTone (when the pitch is a triad tone of the chord active at its onset)
// plus the P7 and P8 bit sets. `extra_bits` are OR-ed in first so the
// branch-specific provenance bit(s) (EpisodeMotifSourced, PedalCommitted,
// per-fragment rhythm bits, etc.) survive; baseline OR-ing is
// order-independent so the resulting mask is identical to the inlined clones
// this helper replaces. The Suspension branch deliberately omits the baseline
// bits and therefore does NOT route through this helper.
Candidate emitMaterialNote(const MaterialNote& mnote, const HarmonicPlan& plan,
                           RuleIdMask extra_bits) {
  Candidate c;
  c.start_tick = mnote.start_tick;
  c.duration = mnote.duration;
  c.pitch = mnote.pitch;
  c.score = 1.0f;
  c.satisfied_rules = extra_bits;
  const ChordEvent& chord_here = activeChord(plan, mnote.start_tick);
  const auto triad_here = triadPitchClasses(chord_here);
  const std::uint8_t pc_m = static_cast<std::uint8_t>(mnote.pitch % 12);
  const bool is_triad_m = (pc_m == triad_here[0] || pc_m == triad_here[1] || pc_m == triad_here[2]);
  if (is_triad_m) {
    c.satisfied_rules |= ruleBitMask(RuleBit::ChordTone);
  }
  applyP7Bits(c.satisfied_rules, chord_here, pc_m, is_triad_m);
  applyP8Bits(c.satisfied_rules, plan, chord_here, pc_m, is_triad_m);
  return c;
}

// Return the portion of an authored note that actually belongs to `span`.
//
// Suspension installation replaces one carrier span with
// before/suspension/after spans.  A long authored note can cross either cut:
// replaying it untrimmed overlaps the suspension, while filtering only by
// onset loses the suffix after the suspension.  Clipping preserves the
// carrier on both sides without changing its pitch or inventing duration.
bool clipMaterialNoteToSpan(const MaterialNote& source, const Span& span, MaterialNote* clipped) {
  if (clipped == nullptr || source.duration == 0)
    return false;
  const Tick source_end = source.start_tick + source.duration;
  const Tick clipped_start = std::max(source.start_tick, span.start_tick);
  const Tick clipped_end = std::min(source_end, span.end_tick);
  if (clipped_start >= clipped_end)
    return false;
  *clipped = source;
  clipped->start_tick = clipped_start;
  clipped->duration = clipped_end - clipped_start;
  return true;
}

}  // namespace

std::vector<Candidate> CandidateSearch::enumerate(const Span& span,
                                                  const HarmonicPlan& harmonic_plan,
                                                  const Material& material,
                                                  const CandidateContext& context,
                                                  std::size_t* saturated_positions) const {
  std::vector<Candidate> out;

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
      // Branch-specific bits (cadence / leading-tone marker / tonal-answer /
      // countersubject / imitation-entry); the baseline ChordTone/P7/P8 bits
      // are added by emitMaterialNote. OR order is irrelevant.
      RuleIdMask extra_bits = 0;
      if (const CadenceCell* cadence_cell = cadenceCellAt(material, mnote.start_tick);
          cadence_cell != nullptr) {
        extra_bits |= ruleBitMask(RuleBit::CadenceCellCommitted);
        if (mnote.start_tick == cadence_cell->cadence_tick) {
          extra_bits |= ruleBitMask(RuleBit::CadenceVoiceLeadingChecked);
        }
      }
      for (const auto& marker : material.leading_tone_markers) {
        if (marker.fragment == fragment && marker.resolution_index == idx) {
          extra_bits |= ruleBitMask(RuleBit::LeadingToneResolved);
          break;
        }
      }
      if (emit_tonal_answer_bit) {
        extra_bits |= ruleBitMask(RuleBit::TonalAnswerMapped);
      }
      if (emit_countersubject_bit) {
        extra_bits |= ruleBitMask(RuleBit::CountersubjectActive);
        // Identity bit for the countersubject_invertible rule: every
        // countersubject note is a candidate for the invertibility pairing the
        // Validator checks against the overlapping subject/answer voice. This is
        // the first high-lane (bit >= 64) RuleBit to ship.
        extra_bits |= ruleBitMask(RuleBit::CountersubjectInvertible);
      }
      // P9 ImitationEntryMatched: set on idx==0 (the entry note) when
      // any declared ImitationEntry names this fragment as leader or
      // follower. The Validator's imitation_entry_match rule checks
      // the actual distance and interval against the declaration.
      if (idx == 0) {
        for (const auto& entry : material.imitation_entries) {
          if (entry.leader_fragment == fragment || entry.follower_fragment == fragment) {
            extra_bits |= ruleBitMask(RuleBit::ImitationEntryMatched);
            break;
          }
        }
      }
      out.push_back(emitMaterialNote(mnote, harmonic_plan, extra_bits));
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
        // EpisodeMotifSourced plus the baseline ChordTone/P7/P8 bits (the
        // ChordTone bit keeps passing-tone tracking on the next Compose span
        // accurate).
        out.push_back(
            emitMaterialNote(n, harmonic_plan, ruleBitMask(RuleBit::EpisodeMotifSourced)));
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
      // Preparation and suspension durations follow tick distances exactly so
      // the held-over dissonance reads as a tie. A one-beat resolution is
      // followed by an explicit one-beat rest before the original carrier
      // resumes; installSuspensionCarrier reserves that complete window.
      prep_duration = pattern.suspension_tick - pattern.preparation_tick;
      sus_duration = pattern.resolution_tick - pattern.suspension_tick;
      Candidate prep;
      prep.start_tick = pattern.preparation_tick;
      prep.duration = prep_duration;
      prep.pitch = pattern.preparation_pitch;
      prep.score = 1.0f;
      prep.satisfied_rules = ruleBitMask(RuleBit::SuspensionPrepared);
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
      res.satisfied_rules = ruleBitMask(RuleBit::SuspensionResolved);
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
          MaterialNote step_note;
          step_note.start_tick = beat_cursor;
          step_note.duration = tmpl.seed_durations[i];
          step_note.pitch = static_cast<std::uint8_t>(transposed);
          RuleIdMask extra_bits = ruleBitMask(RuleBit::FortspinnungSourced);
          if (k > 0) {
            extra_bits |= ruleBitMask(RuleBit::SequenceStep);
          }
          out.push_back(emitMaterialNote(step_note, harmonic_plan, extra_bits));
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
      MaterialNote pedal_note;
      pedal_note.start_tick = pedal.start_tick;
      pedal_note.duration = pedal.duration;
      pedal_note.pitch = pedal.pitch;
      out.push_back(
          emitMaterialNote(pedal_note, harmonic_plan, ruleBitMask(RuleBit::PedalCommitted)));
    }
    return out;
  }

  if (span.intent == VoiceIntent::MiddleEntryCarrier ||
      span.intent == VoiceIntent::StrettoCarrier || span.intent == VoiceIntent::CodaCarrier ||
      span.intent == VoiceIntent::SubjectCarrierAugmented ||
      span.intent == VoiceIntent::SubjectCarrierDiminished ||
      span.intent == VoiceIntent::SubjectCarrierInverted) {
    // Development carriers: verbatim Material replay from a per-intent
    // source vector, stamping one provenance bit. Register safety (no
    // voice crossing) is the fixture's responsibility; because every
    // development carrier is NoteSource::Material, the Validator's
    // vertical/parallel rules skip pairs where both notes are Material.
    const std::vector<MaterialNote>* source = nullptr;
    const MiddleEntryDecl* middle_entry = nullptr;
    RuleBit bit = RuleBit::MiddleEntryCommitted;
    if (span.intent == VoiceIntent::MiddleEntryCarrier) {
      for (const auto& decl : material.middle_entries) {
        if (decl.voice == span.voice) {
          middle_entry = &decl;
          source = &decl.notes;
          break;
        }
      }
      bit = RuleBit::MiddleEntryCommitted;
    } else if (span.intent == VoiceIntent::StrettoCarrier) {
      // A follower voice may participate in more than one stretto moment.
      // Each StrettoCarrier span is window-sliced, so replay every matching
      // declaration rather than breaking at the first follower_voice match.
      // The old first-match lookup made later declarations for the same voice
      // silently emit no notes whenever their source lay outside that window.
      for (const auto& decl : material.stretto_entries) {
        if (decl.follower_voice == span.voice) {
          for (const auto& mnote : decl.follower_notes) {
            if (mnote.start_tick < span.start_tick)
              continue;
            if (mnote.start_tick >= span.end_tick)
              break;
            out.push_back(
                emitMaterialNote(mnote, harmonic_plan, ruleBitMask(RuleBit::StrettoCommitted)));
          }
        }
      }
      return out;
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
      RuleIdMask bits = ruleBitMask(bit);
      if (middle_entry != nullptr &&
          std::any_of(middle_entry->transform_regions.begin(),
                      middle_entry->transform_regions.end(), [&](const auto& region) {
                        return region.transform != 0 && mnote.start_tick >= region.start_tick &&
                               mnote.start_tick < region.end_tick;
                      })) {
        bits |= ruleBitMask(RuleBit::SubjectVariantApplied);
      }
      out.push_back(emitMaterialNote(mnote, harmonic_plan, bits));
    }
    return out;
  }

  if (span.intent == VoiceIntent::RhythmCarrier) {
    // Rhythm carrier: verbatim replay of every RhythmFragment that
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
        RuleIdMask extra_bits = 0;
        switch (frag.feature) {
          case RhythmFragment::Feature::Anacrusis:
            extra_bits |= ruleBitMask(RuleBit::AnacrusisActive);
            break;
          case RhythmFragment::Feature::Hemiola:
            extra_bits |= ruleBitMask(RuleBit::HemiolaInserted);
            break;
          case RhythmFragment::Feature::Recurrence:
            extra_bits |= ruleBitMask(RuleBit::RhythmicMotifRecurrence);
            break;
          case RhythmFragment::Feature::Dotted:
          case RhythmFragment::Feature::Syncopation:
            break;  // rhythm is the feature; no dedicated bit.
        }
        for (Tick start : material.phrase_structure.phrase_start_ticks) {
          if (start == mnote.start_tick) {
            extra_bits |= ruleBitMask(RuleBit::PhrasePeriodicityKept);
            break;
          }
        }
        out.push_back(emitMaterialNote(mnote, harmonic_plan, extra_bits));
      }
    }
    return out;
  }

  if (span.intent == VoiceIntent::NctCarrier) {
    // Non-chord-tone carrier: verbatim replay of material.nct_figures
    // that fall inside the span window, mirroring the SubjectCarrier
    // branch (source = Material, score = 1.0, standard ChordTone/P7/P8
    // bit-OR set). The figure provenance bits (CambiataDetected etc.) are
    // NOT stamped here: the nct_detector figures need the full sorted
    // single-voice note list, which only exists after the Composer's
    // post-sort, so the Composer's NCT post-pass stamps them instead.
    for (const auto& mnote : material.nct_figures) {
      if (mnote.start_tick < span.start_tick)
        continue;
      if (mnote.start_tick >= span.end_tick)
        break;
      // NCT figure bits (CambiataDetected etc.) are stamped later by the
      // Composer's NCT post-pass, so only the baseline bits are set here.
      out.push_back(emitMaterialNote(mnote, harmonic_plan, 0));
    }
    return out;
  }

  if (span.intent == VoiceIntent::ArpeggioFlow) {
    // Solo String Flow carrier: verbatim replay of the single-voice
    // broken-chord line in material.arpeggio_template that falls inside the
    // span window, like the other Material carriers (source = Material,
    // score = 1.0, baseline ChordTone/P7/P8 bit set via emitMaterialNote).
    // Every note additionally carries ArpeggioFlowActive + ImplicitVoiceTracked
    // so the provenance records the Flow device shipped and the
    // Validator's implicit-voice rules cover the line. Implicit-voice
    // membership is positional (group_size) and is reconstructed by the
    // Validator from the emitted note order, not encoded per note here.
    const RuleIdMask flow_bits =
        (ruleBitMask(RuleBit::ArpeggioFlowActive)) | (ruleBitMask(RuleBit::ImplicitVoiceTracked));
    for (const auto& mnote : material.arpeggio_template.notes) {
      if (mnote.start_tick < span.start_tick)
        continue;
      if (mnote.start_tick >= span.end_tick)
        break;
      out.push_back(emitMaterialNote(mnote, harmonic_plan, flow_bits));
    }
    return out;
  }

  if (span.intent == VoiceIntent::GroundCarrier) {
    // Solo String Arch ground carrier: period-tiled verbatim replay of the
    // immutable ground bass (material.ground_bass is one cycle of length
    // material.ground_bass_period ticks). The cycle is laid down repeatedly to
    // fill [span.start_tick, span.end_tick); each note is source = Material,
    // score = 1.0 (baseline ChordTone/P7/P8 bits via emitMaterialNote) plus
    // GroundBassReplayed so the provenance records the ground shipped.
    // The ground is immutable: notes are replayed verbatim, only
    // time-shifted by whole cycles.
    const RuleIdMask ground_bits = (ruleBitMask(RuleBit::GroundBassReplayed));
    const Tick period = material.ground_bass_period;
    if (period > 0 && !material.ground_bass.empty()) {
      for (Tick base = span.start_tick; base < span.end_tick; base += period) {
        for (const auto& mnote : material.ground_bass) {
          const Tick t = base + mnote.start_tick;
          if (t < span.start_tick)
            continue;
          if (t >= span.end_tick)
            break;
          MaterialNote shifted = mnote;
          shifted.start_tick = t;
          out.push_back(emitMaterialNote(shifted, harmonic_plan, ground_bits));
        }
      }
    }
    return out;
  }

  if (span.intent == VoiceIntent::VariationCarrier) {
    // Solo String Arch variation carrier: verbatim replay of the single
    // VariationDecl whose window matches this span (the fixture sets
    // span.start_tick/end_tick to the variation's window). Each note is
    // source = Material, score = 1.0 (baseline bits via emitMaterialNote) plus
    // VariationRoleApplied. The first note of a variation whose density tier
    // differs from the immediately preceding variation (material.variations
    // order) additionally carries TextureDensityShift so the provenance records
    // the texture-density progression shipped.
    const RuleIdMask var_bits = (ruleBitMask(RuleBit::VariationRoleApplied));
    for (std::size_t var_idx = 0; var_idx < material.variations.size(); ++var_idx) {
      const auto& var = material.variations[var_idx];
      if (var.start_tick > span.start_tick || var.end_tick < span.end_tick)
        continue;
      const bool density_shift =
          (var_idx == 0) || (material.variations[var_idx - 1].density_level != var.density_level);
      bool first = span.start_tick == var.start_tick;
      for (const auto& mnote : var.notes) {
        if (mnote.start_tick >= span.end_tick)
          break;
        MaterialNote clipped;
        if (!clipMaterialNoteToSpan(mnote, span, &clipped))
          continue;
        RuleIdMask bits = var_bits;
        if (first && density_shift)
          bits |= (ruleBitMask(RuleBit::TextureDensityShift));
        first = false;
        out.push_back(emitMaterialNote(clipped, harmonic_plan, bits));
      }
    }
    return out;
  }

  if (span.intent == VoiceIntent::FigurationCarrier) {
    // Organ Prelude figuration carrier: verbatim replay of the single
    // FigurationSection whose window matches this span (the fixture sets
    // span.start_tick/end_tick to the section's window). Each note is source =
    // Material, score = 1.0 (baseline ChordTone/P7/P8 bits via emitMaterialNote)
    // plus FigurationCommitted. A section flagged is_cadenza OR-s CadenzaApplied
    // onto every note; is_pedal_prep OR-s PedalPreparation. These bits let the
    // provenance record the prelude's free figuration / cadenza / dominant
    // pedal-preparation devices shipped. The on-beat chord-tone check is the
    // Validator's figuration_harmonic_consistency rule, not re-derived here.
    for (const auto& section : material.figuration_sections) {
      if (section.start_tick > span.start_tick || section.end_tick < span.end_tick)
        continue;
      // Match the section's voice too, so two voices may carry distinct
      // figuration over the same bar window (e.g. a fugue middle entry whose V0
      // and V1 figuration share a 4-bar window). Without the voice match the
      // span would replay every section in that window into one voice.
      if (section.voice != span.voice)
        continue;
      RuleIdMask fig_bits = (ruleBitMask(RuleBit::FigurationCommitted));
      if (section.is_cadenza)
        fig_bits |= (ruleBitMask(RuleBit::CadenzaApplied));
      if (section.is_pedal_prep)
        fig_bits |= (ruleBitMask(RuleBit::PedalPreparation));
      for (const auto& mnote : section.notes) {
        if (mnote.start_tick >= span.end_tick)
          break;
        MaterialNote clipped;
        if (!clipMaterialNoteToSpan(mnote, span, &clipped))
          continue;
        RuleIdMask note_bits = fig_bits;
        // A bar downbeat the builder proved it could not anchor on a chord tone
        // carries FigurationAnchorRelaxed, which is what the Validator's
        // figuration_harmonic_consistency rule reads to exempt it.
        if (std::find(section.relaxed_anchor_ticks.begin(), section.relaxed_anchor_ticks.end(),
                      mnote.start_tick) != section.relaxed_anchor_ticks.end())
          note_bits |= (ruleBitMask(RuleBit::FigurationAnchorRelaxed));
        out.push_back(emitMaterialNote(clipped, harmonic_plan, note_bits));
      }
    }
    return out;
  }

  if (span.intent == VoiceIntent::ToccataCarrier) {
    // Organ Toccata carrier: verbatim replay of the single ToccataSection
    // whose window matches this span (the fixture sets span.start_tick/end_tick
    // to the section's window). Each note is source = Material, score = 1.0
    // (baseline ChordTone/P7/P8 bits via emitMaterialNote) plus
    // ToccataArchetypeApplied. When the section is flagged is_section_head, its
    // FIRST note additionally carries SectionTransition (confirming the
    // sectional layout shipped). The (character, archetype) compatibility is the
    // Validator's toccata_archetype_compatible rule, not re-derived here.
    for (const auto& section : material.toccata_sections) {
      if (section.voice != span.voice)
        continue;
      if (section.start_tick > span.start_tick || section.end_tick < span.end_tick)
        continue;
      const RuleIdMask toc_bits = (ruleBitMask(RuleBit::ToccataArchetypeApplied));
      bool first = span.start_tick == section.start_tick;
      for (const auto& mnote : section.notes) {
        if (mnote.start_tick >= span.end_tick)
          break;
        MaterialNote clipped;
        if (!clipMaterialNoteToSpan(mnote, span, &clipped))
          continue;
        RuleIdMask bits = toc_bits;
        if (first && section.is_section_head)
          bits |= (ruleBitMask(RuleBit::SectionTransition));
        first = false;
        out.push_back(emitMaterialNote(clipped, harmonic_plan, bits));
      }
    }
    return out;
  }

  if (span.intent == VoiceIntent::CantusFirmusCarrier) {
    // Organ Chorale Prelude cantus firmus carrier: verbatim replay of the
    // fixed chorale tune that falls inside the span window. Source = the
    // embellished line (material.cf_embellished) when material.cf_is_embellished
    // is set, otherwise the plain skeleton (material.cantus_firmus). Each note is
    // source = Material, score = 1.0 (baseline ChordTone/P7/P8 bits via
    // emitMaterialNote) plus CantusFirmusReplayed. When the embellished line is
    // replayed, every note additionally carries CFEmbellishmentApplied. The
    // cantus firmus is immutable: its bar-downbeat tones are checked
    // by the Validator's cantus_firmus_immutable rule, not re-derived here.
    const std::vector<MaterialNote>& source =
        material.cf_is_embellished ? material.cf_embellished : material.cantus_firmus;
    RuleIdMask cf_bits = (ruleBitMask(RuleBit::CantusFirmusReplayed));
    if (material.cf_is_embellished)
      cf_bits |= (ruleBitMask(RuleBit::CFEmbellishmentApplied));
    for (const auto& mnote : source) {
      if (mnote.start_tick < span.start_tick)
        continue;
      if (mnote.start_tick >= span.end_tick)
        break;
      out.push_back(emitMaterialNote(mnote, harmonic_plan, cf_bits));
    }
    return out;
  }

  if (span.intent == VoiceIntent::PassacagliaGround) {
    // Organ Passacaglia ground carrier: period-tiled replay of the immutable
    // 8-bar passacaglia ground bass (material.passacaglia_ground is one
    // cycle of length material.passacaglia_ground_period ticks). The cycle is
    // laid down repeatedly to fill [span.start_tick, span.end_tick); each note is
    // source = Material, score = 1.0 (baseline ChordTone/P7/P8 bits via
    // emitMaterialNote) plus PassacagliaGroundReplayed so the provenance records
    // the ground shipped. The ground PITCHES are immutable: notes are replayed
    // time-shifted by whole cycles, and from
    // material.passacaglia_ground_split_from on each long note is restated as
    // repeated same-pitch quarters (rhythm-only late-cycle intensification).
    // Same shape as the GroundCarrier branch with the renamed source vector /
    // bit.
    const RuleIdMask ground_bits = (ruleBitMask(RuleBit::PassacagliaGroundReplayed));
    const Tick period = material.passacaglia_ground_period;
    const Tick split_from = material.passacaglia_ground_split_from;
    const auto& gnotes = material.passacaglia_ground;
    if (period > 0 && !gnotes.empty()) {
      for (Tick base = span.start_tick; base < span.end_tick; base += period) {
        for (std::size_t idx = 0; idx < gnotes.size(); ++idx) {
          const auto& mnote = gnotes[idx];
          const Tick t = base + mnote.start_tick;
          if (t < span.start_tick)
            continue;
          if (t >= span.end_tick)
            break;
          MaterialNote shifted = mnote;
          shifted.start_tick = t;
          // Late-cycle rhythmic intensification (design value carried by the
          // material): from `split_from` on, a tiled ground note longer than a
          // beat is restated as repeated same-pitch quarters. Pitch and the
          // bar-head onset are untouched, so the immutable skeleton holds. The
          // piece's final ground note stays unsplit: the bass joins the held
          // closing chord instead of hammering quarters through the cadence.
          const bool is_final_note = t + shifted.duration >= span.end_tick;
          // The ground's last bar carries the same pitch as the next cycle's
          // first bar (the BWV582-style tonic return), so hammering BOTH sides
          // of a cycle seam chains six same-pitch quarters -- past the corpus
          // repeated-note envelope (max run 4). When the successor in the
          // tiled stream restates the same pitch and will itself be split, the
          // current note stays a sustained long note (the phrase-end hold
          // before the martellato relaunch); the run is then at most the
          // successor bar's own quarters plus this single note.
          const bool last_in_cycle = idx + 1 == gnotes.size();
          const auto& next_note = last_in_cycle ? gnotes.front() : gnotes[idx + 1];
          const Tick next_t = last_in_cycle ? base + period : base + gnotes[idx + 1].start_tick;
          const bool next_would_split = split_from > 0 && next_t >= split_from &&
                                        next_note.duration > kTicksPerBeat &&
                                        next_t + next_note.duration < span.end_tick;
          const bool sustain_into_restatement =
              next_would_split && next_note.pitch == shifted.pitch;
          if (split_from > 0 && t >= split_from && shifted.duration > kTicksPerBeat &&
              !is_final_note && !sustain_into_restatement) {
            for (Tick off = 0; off < shifted.duration; off += kTicksPerBeat) {
              MaterialNote chunk = shifted;
              chunk.start_tick = t + off;
              chunk.duration = std::min(kTicksPerBeat, shifted.duration - off);
              out.push_back(emitMaterialNote(chunk, harmonic_plan, ground_bits));
            }
            continue;
          }
          out.push_back(emitMaterialNote(shifted, harmonic_plan, ground_bits));
        }
      }
    }
    return out;
  }

  if (span.intent == VoiceIntent::GoldbergBassCarrier) {
    const Tick period = material.goldberg_aria_bass_period;
    if (period > 0 && !material.goldberg_aria_bass.empty()) {
      for (Tick base = span.start_tick; base < span.end_tick; base += period) {
        for (const auto& source : material.goldberg_aria_bass) {
          MaterialNote shifted = source;
          shifted.start_tick = base + source.start_tick;
          if (shifted.start_tick >= span.end_tick)
            break;
          out.push_back(
              emitMaterialNote(shifted, harmonic_plan, ruleBitMask(RuleBit::GoldbergBassReplayed)));
        }
      }
    }
    return out;
  }

  if (span.intent == VoiceIntent::GoldbergVariationCarrier) {
    for (const auto& variation : material.goldberg_variations) {
      if (variation.voice != span.voice || variation.start_tick > span.start_tick ||
          variation.end_tick < span.end_tick) {
        continue;
      }
      RuleIdMask bits = ruleBitMask(RuleBit::GoldbergVariationRealized);
      if (variation.is_climax)
        bits |= ruleBitMask(RuleBit::ClimaxPlaced);
      for (const auto& note : variation.notes) {
        if (note.start_tick >= span.end_tick)
          break;
        MaterialNote clipped;
        if (clipMaterialNoteToSpan(note, span, &clipped))
          out.push_back(emitMaterialNote(clipped, harmonic_plan, bits));
      }
      break;
    }
    return out;
  }

  if (span.intent == VoiceIntent::GoldbergInnerVoiceCarrier) {
    for (const auto& note : material.goldberg_inner_voice) {
      if (note.start_tick < span.start_tick)
        continue;
      if (note.start_tick >= span.end_tick)
        break;
      out.push_back(
          emitMaterialNote(note, harmonic_plan, ruleBitMask(RuleBit::GoldbergInnerVoiceRealized)));
    }
    return out;
  }

  if (span.intent == VoiceIntent::PassacagliaVariation) {
    // Organ Passacaglia variation carrier: verbatim replay of the single
    // PassacagliaVariation whose window matches this span (the fixture sets
    // span.start_tick/end_tick to the variation's window). Each note is source =
    // Material, score = 1.0 (baseline bits via emitMaterialNote) plus
    // VariationApplied. When the variation block is flagged is_climax (the
    // dynamic / registral peak), every note additionally carries ClimaxPlaced so
    // the provenance records the climax shipped. Same shape as the
    // VariationCarrier branch with the climax flag replacing the density-shift
    // logic.
    const RuleIdMask var_bits = (ruleBitMask(RuleBit::VariationApplied));
    for (const auto& var : material.passacaglia_variations) {
      if (var.start_tick > span.start_tick || var.end_tick < span.end_tick)
        continue;
      RuleIdMask bits = var_bits;
      if (var.is_climax)
        bits |= (ruleBitMask(RuleBit::ClimaxPlaced));
      for (const auto& mnote : var.notes) {
        if (mnote.start_tick >= span.end_tick)
          break;
        MaterialNote clipped;
        if (clipMaterialNoteToSpan(mnote, span, &clipped))
          out.push_back(emitMaterialNote(clipped, harmonic_plan, bits));
      }
    }
    return out;
  }

  if (span.intent == VoiceIntent::TrioVoiceCarrier) {
    // Organ Trio Sonata carrier: verbatim replay of the single TrioVoiceLine
    // whose voice matches this span (material.trio_voices). Each note that falls
    // inside the span window is source = Material, score = 1.0 (baseline
    // ChordTone/P7/P8 bits via emitMaterialNote) plus TrioVoiceIndependent so the
    // Validator's voice_independence_threshold rule can collect the trio voices
    // and measure their pairwise independence. Inert when trio_voices is empty.
    // Clone of the PassacagliaVariation branch, matched by voice (not window).
    const RuleIdMask trio_bits = (ruleBitMask(RuleBit::TrioVoiceIndependent));
    for (const auto& line : material.trio_voices) {
      if (line.voice != span.voice)
        continue;
      for (const auto& mnote : line.notes) {
        if (mnote.start_tick >= span.end_tick)
          break;
        MaterialNote clipped;
        if (clipMaterialNoteToSpan(mnote, span, &clipped))
          out.push_back(emitMaterialNote(clipped, harmonic_plan, trio_bits));
      }
    }
    return out;
  }

  if (span.intent == VoiceIntent::FantasiaCarrier) {
    // Organ Fantasia carrier: verbatim replay of the single FantasiaSection
    // whose window matches this span (the fixture sets span.start_tick/end_tick
    // to the section's window). Each note is source = Material, score = 1.0
    // (baseline ChordTone/P7/P8 bits via emitMaterialNote) plus
    // FantasiaSectionContrast so the Validator's section_contrast_required rule
    // can collect the sections (by window) and measure the density / register
    // contrast between adjacent sections. Inert when fantasia_sections is empty.
    // Clone of the ToccataCarrier branch (window-matched verbatim replay).
    const RuleIdMask fan_bits = (ruleBitMask(RuleBit::FantasiaSectionContrast));
    for (const auto& section : material.fantasia_sections) {
      if (section.voice != span.voice)
        continue;
      if (section.start_tick > span.start_tick || section.end_tick < span.end_tick)
        continue;
      for (const auto& mnote : section.notes) {
        if (mnote.start_tick >= span.end_tick)
          break;
        MaterialNote clipped;
        if (clipMaterialNoteToSpan(mnote, span, &clipped))
          out.push_back(emitMaterialNote(clipped, harmonic_plan, fan_bits));
      }
    }
    return out;
  }

  // Every non-compose replay kind must have been consumed by an explicit
  // carrier branch above.  This makes the descriptor table part of dispatch:
  // adding a new replay intent cannot silently fall through to free generation.
  if (describeIntent(span.intent).replay != ReplayKind::kCompose) {
    return out;
  }

  return composeFreeSpan(span, harmonic_plan, material, context, saturated_positions);
}

}  // namespace bach::composer
