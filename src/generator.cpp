// Implementation of the unified generator that routes to form-specific generators.

#include "generator.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include "analysis/analysis_runner.h"
#include "core/interval.h"
#include "core/json_helpers.h"
#include "core/pitch_utils.h"
#include "core/rng_util.h"
#include "expression/articulation.h"
#include "forms/chorale_prelude.h"
#include "forms/fantasia.h"
#include "forms/goldberg/goldberg_config.h"
#include "forms/passacaglia.h"
#include "forms/prelude.h"
#include "forms/toccata.h"
#include "forms/trio_sonata.h"
#include "fugue/fugue_config.h"
#include "fugue/fugue_generator.h"
#include "harmony/harmonic_timeline.h"
#include "harmony/key.h"
#include "harmony/modulation_plan.h"
#include "harmony/tempo_map.h"
#include "instrument/common/impossibility_guard.h"
#include "midi/velocity_curve.h"
#include "solo_string/arch/chaconne_engine.h"
#include "solo_string/flow/harmonic_arpeggio_engine.h"

namespace bach {

namespace {

/// @brief Seed offset applied to the prelude seed so it differs from the fugue seed.
constexpr uint32_t kPreludeSeedOffset = 7919u;

constexpr size_t kNoteSourceCount = static_cast<size_t>(BachNoteSource::CadenceApproach) + 1u;

bool isSubjectAuditSource(BachNoteSource source) {
  return source == BachNoteSource::SubjectCore || source == BachNoteSource::FugueSubject ||
         source == BachNoteSource::FugueAnswer;
}

bool isDialogueAuditSource(BachNoteSource source) {
  return source == BachNoteSource::Countersubject || source == BachNoteSource::FalseEntry ||
         source == BachNoteSource::SequenceNote;
}

bool isEarlyExpositionDialogueSource(BachNoteSource source) {
  return isSubjectAuditSource(source) || source == BachNoteSource::Countersubject ||
         source == BachNoteSource::FalseEntry || source == BachNoteSource::SequenceNote;
}

bool isEpisodeDialogueAuditSource(BachNoteSource source) {
  return source == BachNoteSource::SequenceNote || source == BachNoteSource::FalseEntry;
}

bool isEpisodeMotifAuditSource(BachNoteSource source) {
  return source == BachNoteSource::EpisodeMaterial || source == BachNoteSource::SequenceNote ||
         source == BachNoteSource::FalseEntry;
}

bool isBassIntentAuditSource(BachNoteSource source) {
  return source == BachNoteSource::SubjectCore || source == BachNoteSource::FugueSubject ||
         source == BachNoteSource::FugueAnswer || source == BachNoteSource::PedalPoint ||
         source == BachNoteSource::CadenceApproach || source == BachNoteSource::Coda ||
         source == BachNoteSource::SequenceNote;
}

bool hasPitchRepair(uint8_t modified_by) {
  constexpr uint8_t kPitchRepairMask = static_cast<uint8_t>(NoteModifiedBy::ParallelRepair) |
                                       static_cast<uint8_t>(NoteModifiedBy::ChordToneSnap) |
                                       static_cast<uint8_t>(NoteModifiedBy::OctaveAdjust) |
                                       static_cast<uint8_t>(NoteModifiedBy::LeapResolution) |
                                       static_cast<uint8_t>(NoteModifiedBy::RepeatedNoteRep);
  return (modified_by & kPitchRepairMask) != 0;
}

bool hasDurationRepairOnly(uint8_t modified_by) {
  constexpr uint8_t kOverlapTrim = static_cast<uint8_t>(NoteModifiedBy::OverlapTrim);
  return modified_by != 0 && (modified_by & ~kOverlapTrim) == 0;
}

bool isFlexibleContourAuditSource(BachNoteSource source) {
  return source == BachNoteSource::FreeCounterpoint || source == BachNoteSource::EpisodeMaterial ||
         source == BachNoteSource::SequenceNote;
}

bool isComposedIntentAuditSource(BachNoteSource source) {
  return source == BachNoteSource::SubjectCore || source == BachNoteSource::FugueSubject ||
         source == BachNoteSource::FugueAnswer || source == BachNoteSource::Countersubject ||
         source == BachNoteSource::FalseEntry || source == BachNoteSource::SequenceNote ||
         source == BachNoteSource::PedalPoint || source == BachNoteSource::CadenceApproach ||
         source == BachNoteSource::Coda;
}

bool noteSoundsAt(const NoteEvent& note, Tick tick) {
  return note.start_tick <= tick && tick < note.start_tick + note.duration;
}

bool isExposedHardDissonance(uint8_t lhs_pitch, uint8_t rhs_pitch) {
  const int simple_interval = absoluteInterval(lhs_pitch, rhs_pitch) % interval::kOctave;
  return simple_interval == interval::kMinor2nd || simple_interval == interval::kMajor2nd ||
         simple_interval == interval::kTritone || simple_interval == interval::kMinor7th ||
         simple_interval == interval::kMajor7th;
}

bool isBassFourthInstability(const NoteEvent& lhs, const NoteEvent& rhs, uint8_t bass_voice) {
  const int simple_interval = absoluteInterval(lhs.pitch, rhs.pitch) % interval::kOctave;
  if (simple_interval != interval::kPerfect4th)
    return false;
  const NoteEvent& lower = lhs.pitch <= rhs.pitch ? lhs : rhs;
  return lower.voice == bass_voice;
}

bool isEarlyListeningUnstableInterval(const NoteEvent& lhs, const NoteEvent& rhs,
                                      uint8_t bass_voice) {
  return isExposedHardDissonance(lhs.pitch, rhs.pitch) ||
         isBassFourthInstability(lhs, rhs, bass_voice);
}

int directedSimpleInterval(uint8_t from_pitch, uint8_t to_pitch) {
  const int directed = directedInterval(from_pitch, to_pitch);
  if (directed == 0)
    return 0;
  int magnitude = std::abs(directed) % interval::kOctave;
  if (magnitude == 0)
    magnitude = interval::kOctave;
  return directed > 0 ? magnitude : -magnitude;
}

Tick secondsToTicks(double seconds, uint16_t bpm) {
  if (seconds <= 0.0 || bpm == 0)
    return 0;
  const double ticks =
      seconds * static_cast<double>(bpm) * static_cast<double>(kTicksPerBeat) / 60.0;
  return static_cast<Tick>(ticks + 0.5);
}

uint32_t countOccurrences(const std::string& text, const std::string& needle) {
  if (needle.empty())
    return 0;
  uint32_t count = 0;
  size_t pos = 0;
  while ((pos = text.find(needle, pos)) != std::string::npos) {
    ++count;
    pos += needle.size();
  }
  return count;
}

Tick parseTickField(const std::string& text, const std::string& field) {
  size_t pos = text.find(field);
  if (pos == std::string::npos)
    return 0;
  pos += field.size();
  while (pos < text.size() && (text[pos] == ' ' || text[pos] == ':')) {
    ++pos;
  }
  Tick value = 0;
  while (pos < text.size() && text[pos] >= '0' && text[pos] <= '9') {
    value = value * 10 + static_cast<Tick>(text[pos] - '0');
    ++pos;
  }
  return value;
}

struct StructureAuditSummary {
  struct ListeningHotspotAudit {
    int bar = 0;
    uint32_t note_count = 0;
    uint32_t dialogue_notes = 0;
    uint32_t flexible_notes = 0;
    uint32_t pitch_repair_modified_notes = 0;
    uint32_t flexible_large_leap_count = 0;
    uint32_t max_flexible_leap = 0;
    uint32_t active_voice_sample_count = 0;
    uint32_t active_voice_sample_sum = 0;
    uint32_t min_active_voices = 0;
    uint32_t max_active_voices = 0;
    uint32_t hard_clash_sample_count = 0;
    uint32_t hard_clash_count = 0;
    double pitch_repair_ratio = 0.0;
    double average_active_voices = 0.0;
    double hard_clash_ratio = 0.0;
    bool covered = false;
    bool pitch_repair_pass = true;
    bool texture_pass = true;
    bool hard_clash_pass = true;
  };

  struct TimedStabilityAudit {
    const char* label = "";
    double start_seconds = 0.0;
    double end_seconds = 0.0;
    uint32_t sample_count = 0;
    uint32_t hard_clash_count = 0;
    uint32_t unstable_interval_count = 0;
    uint32_t pitch_repair_active_count = 0;
    uint32_t low_texture_count = 0;
    uint32_t active_voice_sample_sum = 0;
    double hard_clash_ratio = 0.0;
    double unstable_interval_ratio = 0.0;
    double pitch_repair_active_ratio = 0.0;
    double low_texture_ratio = 0.0;
    double average_active_voices = 0.0;
    bool pass = true;
  };

  uint32_t total_notes = 0;
  uint32_t composed_intent_notes = 0;
  uint32_t protected_subject_notes = 0;
  uint32_t protected_dialogue_notes = 0;
  uint32_t episode_dialogue_notes = 0;
  uint32_t episode_material_notes = 0;
  uint32_t bass_voice_notes = 0;
  uint32_t bass_intent_notes = 0;
  uint32_t bass_activity_sample_count = 0;
  uint32_t bass_active_sample_count = 0;
  uint32_t bass_motion_intervals = 0;
  uint32_t bass_step_or_structural_motion_intervals = 0;
  uint32_t bass_large_leaps = 0;
  uint32_t max_bass_leap = 0;
  uint32_t immutable_notes = 0;
  uint32_t flexible_notes = 0;
  uint32_t repair_modified_notes = 0;
  uint32_t pitch_repair_modified_notes = 0;
  uint32_t repair_hotspot_bar = 0;
  uint32_t repair_hotspot_note_count = 0;
  uint32_t repair_hotspot_modified_notes = 0;
  uint32_t repair_hotspot_pitch_repair_notes = 0;
  uint32_t episode_material_repair_hotspot_bar = 0;
  uint32_t episode_material_repair_hotspot_note_count = 0;
  uint32_t episode_material_repair_hotspot_pitch_repair_notes = 0;
  uint32_t protected_subject_pitch_repair_notes = 0;
  uint32_t protected_dialogue_pitch_repair_notes = 0;
  uint32_t immutable_pitch_repair_notes = 0;
  uint32_t flexible_pitch_repair_notes = 0;
  uint32_t countersubject_pitch_repair_notes = 0;
  uint32_t sequence_pitch_repair_notes = 0;
  uint32_t episode_material_pitch_repair_notes = 0;
  uint32_t cadence_approach_pitch_repair_notes = 0;
  uint32_t coda_source_pitch_repair_notes = 0;
  uint32_t duration_repair_only_notes = 0;
  uint32_t listening_hotspot_notes = 0;
  uint32_t listening_hotspot_pitch_repair_notes = 0;
  uint32_t early_listening_hard_clash_sample_count = 0;
  uint32_t early_listening_hard_clash_count = 0;
  uint32_t early_listening_unstable_interval_sample_count = 0;
  uint32_t early_listening_unstable_interval_count = 0;
  uint32_t eight_second_dialogue_sample_count = 0;
  uint32_t eight_second_dialogue_hard_clash_count = 0;
  uint32_t eight_second_dialogue_unstable_interval_count = 0;
  uint32_t early_exposition_notes = 0;
  uint32_t early_exposition_dialogue_notes = 0;
  uint32_t early_exposition_pitch_repair_notes = 0;
  uint32_t early_exposition_free_counterpoint_notes = 0;
  uint32_t early_exposition_free_counterpoint_pitch_repair_notes = 0;
  uint32_t early_exposition_counterline_large_leaps = 0;
  uint32_t early_answer_counterline_notes = 0;
  uint32_t early_answer_counterline_same_pitch_run_max = 0;
  uint32_t resolve_region_notes = 0;
  uint32_t resolve_region_pitch_repair_notes = 0;
  uint32_t coda_notes = 0;
  uint32_t coda_bass_notes = 0;
  uint32_t coda_sixteenth_notes = 0;
  uint32_t coda_pitch_repair_notes = 0;
  uint32_t coda_final_tonic_bass_notes = 0;
  uint32_t coda_subject_head_interval_count = 0;
  uint32_t coda_subject_head_match_count = 0;
  uint32_t flexible_large_leap_count = 0;
  uint32_t flexible_remote_leap_count = 0;
  uint32_t max_flexible_leap = 0;
  uint32_t max_pitch_repair_run = 0;
  uint32_t max_pitch_repair_run_bar = 0;
  uint32_t max_pitch_repair_run_voice = 0;
  BachNoteSource max_pitch_repair_run_source = BachNoteSource::Unknown;
  uint32_t opening_subject_note_count = 0;
  uint32_t opening_sixth_note_pitch = 0;
  int32_t opening_sixth_approach_interval = 0;
  int32_t opening_sixth_resolution_interval = 0;
  uint32_t opening_subject_large_leap_count = 0;
  uint32_t opening_subject_unresolved_leap_count = 0;
  uint32_t opening_subject_max_leap = 0;
  uint32_t two_voice_sample_count = 0;
  uint32_t thematic_two_voice_sample_count = 0;
  uint32_t subject_dialogue_pair_sample_count = 0;
  uint32_t thematic_dialogue_pair_sample_count = 0;
  uint32_t thematic_dialogue_hard_clash_count = 0;
  uint32_t early_thematic_dialogue_pair_sample_count = 0;
  uint32_t early_thematic_dialogue_hard_clash_count = 0;
  uint32_t episode_dialogue_window_count = 0;
  uint32_t episode_dialogue_weak_window_count = 0;
  uint32_t subject_motif_interval_pair_count = 0;
  uint32_t episode_motif_interval_pair_count = 0;
  uint32_t episode_motif_derived_pair_count = 0;
  uint32_t formal_section_count = 0;
  uint32_t establish_section_count = 0;
  uint32_t develop_section_count = 0;
  uint32_t resolve_section_count = 0;
  uint32_t middle_entry_count = 0;
  uint32_t stretto_count = 0;
  uint32_t coda_section_count = 0;
  Tick formal_structure_ticks = 0;
  double composed_intent_ratio = 0.0;
  double repair_modified_ratio = 0.0;
  double pitch_repair_modified_ratio = 0.0;
  double repair_hotspot_modified_ratio = 0.0;
  double repair_hotspot_pitch_repair_ratio = 0.0;
  double episode_material_repair_hotspot_pitch_repair_ratio = 0.0;
  double protected_dialogue_pitch_repair_ratio = 0.0;
  double immutable_pitch_repair_ratio = 0.0;
  double flexible_pitch_repair_ratio = 0.0;
  double episode_material_pitch_repair_ratio = 0.0;
  double episode_material_pitch_repair_density = 0.0;
  double bass_activity_ratio = 0.0;
  double bass_step_or_structural_motion_ratio = 0.0;
  double listening_hotspot_pitch_repair_ratio = 0.0;
  double early_listening_hard_clash_ratio = 0.0;
  double early_listening_unstable_interval_ratio = 0.0;
  double eight_second_dialogue_hard_clash_ratio = 0.0;
  double eight_second_dialogue_unstable_interval_ratio = 0.0;
  double early_exposition_dialogue_ratio = 0.0;
  double early_exposition_free_counterpoint_ratio = 0.0;
  double early_exposition_pitch_repair_ratio = 0.0;
  double early_exposition_free_counterpoint_pitch_repair_ratio = 0.0;
  double resolve_region_pitch_repair_ratio = 0.0;
  double coda_pitch_repair_ratio = 0.0;
  double coda_subject_head_match_ratio = 0.0;
  double thematic_two_voice_ratio = 0.0;
  double subject_dialogue_pair_ratio = 0.0;
  double thematic_dialogue_hard_clash_ratio = 0.0;
  double early_thematic_dialogue_hard_clash_ratio = 0.0;
  double min_episode_dialogue_window_ratio = 1.0;
  double episode_motif_derivation_ratio = 0.0;
  std::array<ListeningHotspotAudit, 4> listening_hotspots{};
  std::array<TimedStabilityAudit, 3> critic_time_windows{};
  bool listening_hotspot_pitch_repair_pass = true;
  bool listening_hotspot_texture_pass = true;
  bool listening_hotspot_hard_clash_pass = true;
  bool early_listening_hard_clash_pass = true;
  bool early_listening_unstable_interval_pass = true;
  bool eight_second_dialogue_pass = true;
  bool critic_time_window_pass = true;
  bool bass_line_motion_pass = true;
  bool early_exposition_intent_pass = true;
  bool early_exposition_dialogue_pass = true;
  bool early_exposition_counterline_pass = true;
  bool early_answer_counterline_repetition_pass = true;
  bool resolve_region_repair_pass = true;
  bool coda_intent_pass = false;
  bool coda_closing_cadence_pass = true;
  bool coda_subject_head_pass = true;
  bool pitch_repair_run_pass = true;
  bool repair_hotspot_pass = true;
  bool episode_material_repair_dependency_pass = true;
  bool opening_sixth_note_pass = true;
  bool opening_subject_contour_pass = true;
  bool global_arc_pass = true;
  bool thematic_two_voice_pass = true;
  bool thematic_dialogue_clash_pass = true;
  bool early_thematic_dialogue_clash_pass = true;
  bool episode_dialogue_window_pass = true;
  bool episode_motif_derivation_pass = true;
  bool episode_dialogue_pass = false;
  bool bass_intent_pass = false;
  bool repair_surrogate_pass = false;
  bool repair_dependency_pass = false;
  bool flexible_contour_pass = false;
  bool pass = false;
};

int listeningHotspotIndex(int bar) {
  switch (bar) {
    case 9:
      return 0;
    case 14:
      return 1;
    case 18:
      return 2;
    case 23:
      return 3;
    default:
      return -1;
  }
}

StructureAuditSummary computeStructureAudit(const GeneratorResult& result,
                                            const GeneratorConfig& config) {
  StructureAuditSummary audit;
  struct RepairWindowCounts {
    uint32_t note_count = 0;
    uint32_t modified_notes = 0;
    uint32_t pitch_repair_notes = 0;
    uint32_t episode_material_notes = 0;
    uint32_t episode_material_pitch_repair_notes = 0;
  };
  struct EpisodeDialogueWindowCounts {
    uint32_t motif_notes = 0;
    uint32_t dialogue_notes = 0;
  };
  const size_t repair_window_count = std::max<size_t>(
      1u, static_cast<size_t>((result.total_duration_ticks + kTicksPerBar - 1) / kTicksPerBar));
  std::vector<RepairWindowCounts> repair_windows(repair_window_count);
  constexpr Tick kEpisodeDialogueWindowTicks = kTicksPerBar * 2;
  const size_t episode_dialogue_window_count = std::max<size_t>(
      1u, static_cast<size_t>((result.total_duration_ticks + kEpisodeDialogueWindowTicks - 1) /
                              kEpisodeDialogueWindowTicks));
  std::vector<EpisodeDialogueWindowCounts> episode_dialogue_windows(episode_dialogue_window_count);
  audit.listening_hotspots[0].bar = 9;
  audit.listening_hotspots[1].bar = 14;
  audit.listening_hotspots[2].bar = 18;
  audit.listening_hotspots[3].bar = 23;
  audit.critic_time_windows[0].label = "8s_dialogue";
  audit.critic_time_windows[0].start_seconds = 8.0;
  audit.critic_time_windows[0].end_seconds = 8.75;
  audit.critic_time_windows[1].label = "18s_transition";
  audit.critic_time_windows[1].start_seconds = 18.0;
  audit.critic_time_windows[1].end_seconds = 19.0;
  audit.critic_time_windows[2].label = "26s_stability";
  audit.critic_time_windows[2].start_seconds = 26.0;
  audit.critic_time_windows[2].end_seconds = 27.0;
  const uint8_t bass_voice =
      config.num_voices > 0 ? static_cast<uint8_t>(config.num_voices - 1) : 0;
  const bool coda_required =
      config.form == FormType::Fugue && result.total_duration_ticks >= kTicksPerBar * 8;
  const Tick early_exposition_end = std::min<Tick>(result.total_duration_ticks, kTicksPerBar * 8);
  const Tick resolve_region_start =
      static_cast<Tick>((static_cast<uint64_t>(result.total_duration_ticks) * 7u) / 10u);
  std::vector<std::pair<int, int>> subject_motif_pairs;
  if (!result.structure_json.empty()) {
    audit.formal_section_count = countOccurrences(result.structure_json, "\"type\"");
    audit.establish_section_count =
        countOccurrences(result.structure_json, "\"phase\":\"Establish\"");
    audit.develop_section_count = countOccurrences(result.structure_json, "\"phase\":\"Develop\"");
    audit.resolve_section_count = countOccurrences(result.structure_json, "\"phase\":\"Resolve\"");
    audit.middle_entry_count = countOccurrences(result.structure_json, "\"type\":\"MiddleEntry\"");
    audit.stretto_count = countOccurrences(result.structure_json, "\"type\":\"Stretto\"");
    audit.coda_section_count = countOccurrences(result.structure_json, "\"type\":\"Coda\"");
    audit.formal_structure_ticks =
        parseTickField(result.structure_json, "\"total_duration_ticks\"");

    const size_t exposition_pos = result.structure_json.find("\"type\":\"Exposition\"");
    const size_t develop_pos = result.structure_json.find("\"phase\":\"Develop\"");
    const size_t resolve_pos = result.structure_json.find("\"phase\":\"Resolve\"");
    const size_t stretto_pos = result.structure_json.find("\"type\":\"Stretto\"");
    const size_t coda_pos = result.structure_json.find("\"type\":\"Coda\"");
    const bool has_ordered_arc = exposition_pos != std::string::npos &&
                                 develop_pos != std::string::npos &&
                                 resolve_pos != std::string::npos && exposition_pos < develop_pos &&
                                 develop_pos < resolve_pos;
    const bool has_resolving_close =
        !coda_required || (stretto_pos != std::string::npos && coda_pos != std::string::npos &&
                           stretto_pos < coda_pos);
    const bool covers_generated_span =
        audit.formal_structure_ticks == 0 ||
        audit.formal_structure_ticks + kTicksPerBar >= result.total_duration_ticks;
    audit.global_arc_pass = audit.establish_section_count >= 1 &&
                            audit.develop_section_count >= 1 && audit.resolve_section_count >= 1 &&
                            audit.formal_section_count >= 4 && has_ordered_arc &&
                            has_resolving_close && covers_generated_span;
  } else {
    audit.global_arc_pass = !coda_required;
  }

  for (const auto& track : result.tracks) {
    std::array<const NoteEvent*, 8> previous_by_voice{};
    uint32_t pitch_repair_run = 0;
    Tick previous_note_end = 0;
    bool has_previous_note = false;
    for (const auto& note : track.notes) {
      ++audit.total_notes;
      if (isSubjectAuditSource(note.source)) {
        ++audit.protected_subject_notes;
      }
      if (isComposedIntentAuditSource(note.source)) {
        ++audit.composed_intent_notes;
      }
      if (isDialogueAuditSource(note.source)) {
        ++audit.protected_dialogue_notes;
      }
      if (isEpisodeDialogueAuditSource(note.source)) {
        ++audit.episode_dialogue_notes;
      }
      if (note.source == BachNoteSource::EpisodeMaterial) {
        ++audit.episode_material_notes;
      }
      if (note.voice == bass_voice) {
        ++audit.bass_voice_notes;
        if (isBassIntentAuditSource(note.source)) {
          ++audit.bass_intent_notes;
        }
      }
      const bool immutable = getProtectionLevel(note.source) == ProtectionLevel::Immutable;
      if (immutable) {
        ++audit.immutable_notes;
      } else {
        ++audit.flexible_notes;
      }
      if (note.modified_by != 0) {
        ++audit.repair_modified_notes;
        if (hasPitchRepair(note.modified_by)) {
          ++audit.pitch_repair_modified_notes;
          if (isSubjectAuditSource(note.source)) {
            ++audit.protected_subject_pitch_repair_notes;
          }
          if (isDialogueAuditSource(note.source)) {
            ++audit.protected_dialogue_pitch_repair_notes;
          }
          if (immutable) {
            ++audit.immutable_pitch_repair_notes;
          } else {
            ++audit.flexible_pitch_repair_notes;
          }
          switch (note.source) {
            case BachNoteSource::Countersubject:
              ++audit.countersubject_pitch_repair_notes;
              break;
            case BachNoteSource::SequenceNote:
              ++audit.sequence_pitch_repair_notes;
              break;
            case BachNoteSource::EpisodeMaterial:
              ++audit.episode_material_pitch_repair_notes;
              break;
            case BachNoteSource::CadenceApproach:
              ++audit.cadence_approach_pitch_repair_notes;
              break;
            case BachNoteSource::Coda:
              ++audit.coda_source_pitch_repair_notes;
              break;
            default:
              break;
          }
        }
        if (hasDurationRepairOnly(note.modified_by)) {
          ++audit.duration_repair_only_notes;
        }
      }
      const size_t repair_window_idx = static_cast<size_t>(note.start_tick / kTicksPerBar);
      if (repair_window_idx < repair_windows.size()) {
        auto& window = repair_windows[repair_window_idx];
        ++window.note_count;
        if (note.source == BachNoteSource::EpisodeMaterial) {
          ++window.episode_material_notes;
        }
        if (note.modified_by != 0) {
          ++window.modified_notes;
        }
        if (hasPitchRepair(note.modified_by)) {
          ++window.pitch_repair_notes;
          if (note.source == BachNoteSource::EpisodeMaterial) {
            ++window.episode_material_pitch_repair_notes;
          }
        }
      }
      const size_t episode_window_idx =
          static_cast<size_t>(note.start_tick / kEpisodeDialogueWindowTicks);
      if (episode_window_idx < episode_dialogue_windows.size() &&
          isEpisodeMotifAuditSource(note.source)) {
        auto& window = episode_dialogue_windows[episode_window_idx];
        ++window.motif_notes;
        if (isEpisodeDialogueAuditSource(note.source)) {
          ++window.dialogue_notes;
        }
      }
      const bool pitch_repaired = hasPitchRepair(note.modified_by);
      const Tick gap_from_previous = has_previous_note && note.start_tick > previous_note_end
                                         ? note.start_tick - previous_note_end
                                         : 0;
      if (pitch_repaired && (!has_previous_note || gap_from_previous <= duration::kHalfNote)) {
        ++pitch_repair_run;
      } else if (pitch_repaired) {
        pitch_repair_run = 1;
      } else {
        pitch_repair_run = 0;
      }
      if (pitch_repair_run > audit.max_pitch_repair_run) {
        audit.max_pitch_repair_run = pitch_repair_run;
        audit.max_pitch_repair_run_bar = static_cast<uint32_t>(note.start_tick / kTicksPerBar) + 1u;
        audit.max_pitch_repair_run_voice = note.voice;
        audit.max_pitch_repair_run_source = note.source;
      }
      previous_note_end = note.start_tick + note.duration;
      has_previous_note = true;
      if (note.source == BachNoteSource::Coda) {
        ++audit.coda_notes;
        if (note.voice == bass_voice) {
          ++audit.coda_bass_notes;
        }
        if (note.duration <= duration::kSixteenthNote) {
          ++audit.coda_sixteenth_notes;
        }
        if (hasPitchRepair(note.modified_by)) {
          ++audit.coda_pitch_repair_notes;
        }
      }
      if (note.start_tick < early_exposition_end) {
        ++audit.early_exposition_notes;
        if (isEarlyExpositionDialogueSource(note.source)) {
          ++audit.early_exposition_dialogue_notes;
        }
        const bool pitch_repaired = hasPitchRepair(note.modified_by);
        if (pitch_repaired) {
          ++audit.early_exposition_pitch_repair_notes;
        }
        if (note.source == BachNoteSource::FreeCounterpoint) {
          ++audit.early_exposition_free_counterpoint_notes;
          if (pitch_repaired) {
            ++audit.early_exposition_free_counterpoint_pitch_repair_notes;
          }
        }
      }
      if (coda_required && note.start_tick >= resolve_region_start) {
        ++audit.resolve_region_notes;
        if (hasPitchRepair(note.modified_by)) {
          ++audit.resolve_region_pitch_repair_notes;
        }
      }
      int bar = static_cast<int>(note.start_tick / kTicksPerBar) + 1;
      int hotspot_idx = listeningHotspotIndex(bar);
      if (hotspot_idx >= 0) {
        auto& hotspot = audit.listening_hotspots[hotspot_idx];
        ++hotspot.note_count;
        if (isDialogueAuditSource(note.source)) {
          ++hotspot.dialogue_notes;
        }
        if (isFlexibleContourAuditSource(note.source)) {
          ++hotspot.flexible_notes;
        }
        ++audit.listening_hotspot_notes;
        if (hasPitchRepair(note.modified_by)) {
          ++hotspot.pitch_repair_modified_notes;
          ++audit.listening_hotspot_pitch_repair_notes;
        }
      }
      if (note.voice < previous_by_voice.size()) {
        const NoteEvent* previous = previous_by_voice[note.voice];
        if (previous != nullptr && note.start_tick < early_exposition_end &&
            note.source == BachNoteSource::Countersubject &&
            previous->source == BachNoteSource::Countersubject) {
          Tick gap = note.start_tick > previous->start_tick + previous->duration
                         ? note.start_tick - (previous->start_tick + previous->duration)
                         : 0;
          if (gap <= duration::kHalfNote &&
              absoluteInterval(previous->pitch, note.pitch) > interval::kPerfect5th) {
            ++audit.early_exposition_counterline_large_leaps;
          }
        }
        if (previous != nullptr && isFlexibleContourAuditSource(previous->source) &&
            isFlexibleContourAuditSource(note.source)) {
          Tick gap = note.start_tick > previous->start_tick + previous->duration
                         ? note.start_tick - (previous->start_tick + previous->duration)
                         : 0;
          if (gap <= duration::kHalfNote) {
            uint32_t leap = static_cast<uint32_t>(absoluteInterval(previous->pitch, note.pitch));
            audit.max_flexible_leap = std::max(audit.max_flexible_leap, leap);
            if (leap > interval::kPerfect5th) {
              ++audit.flexible_large_leap_count;
              int leap_hotspot_idx = listeningHotspotIndex(bar);
              if (leap_hotspot_idx >= 0) {
                ++audit.listening_hotspots[leap_hotspot_idx].flexible_large_leap_count;
              }
            }
            if (leap > interval::kOctave) {
              ++audit.flexible_remote_leap_count;
            }
            int leap_hotspot_idx = listeningHotspotIndex(bar);
            if (leap_hotspot_idx >= 0) {
              auto& hotspot = audit.listening_hotspots[leap_hotspot_idx];
              hotspot.max_flexible_leap = std::max(hotspot.max_flexible_leap, leap);
            }
          }
        }
        previous_by_voice[note.voice] = &note;
      }
    }
  }

  std::vector<const NoteEvent*> opening_subject_notes;
  for (uint8_t voice = 0; voice < 8 && opening_subject_notes.empty(); ++voice) {
    std::vector<const NoteEvent*> voice_subject_notes;
    for (const auto& track : result.tracks) {
      for (const auto& note : track.notes) {
        if (note.voice == voice && isSubjectAuditSource(note.source)) {
          voice_subject_notes.push_back(&note);
        }
      }
    }
    std::sort(voice_subject_notes.begin(), voice_subject_notes.end(),
              [](const NoteEvent* lhs, const NoteEvent* rhs) {
                if (lhs->start_tick != rhs->start_tick) {
                  return lhs->start_tick < rhs->start_tick;
                }
                return lhs->pitch < rhs->pitch;
              });
    if (!voice_subject_notes.empty() && voice_subject_notes.front()->start_tick <= kTicksPerBar) {
      opening_subject_notes = std::move(voice_subject_notes);
    }
  }
  if (!opening_subject_notes.empty()) {
    const Tick opening_start = opening_subject_notes.front()->start_tick;
    const Tick opening_end = opening_start + kTicksPerBar * 2;
    opening_subject_notes.erase(
        std::remove_if(
            opening_subject_notes.begin(), opening_subject_notes.end(),
            [opening_end](const NoteEvent* note) { return note->start_tick >= opening_end; }),
        opening_subject_notes.end());
    if (opening_subject_notes.size() > 10) {
      opening_subject_notes.resize(10);
    }
    audit.opening_subject_note_count = static_cast<uint32_t>(opening_subject_notes.size());
    if (opening_subject_notes.size() >= 6) {
      const NoteEvent* fifth = opening_subject_notes[4];
      const NoteEvent* sixth = opening_subject_notes[5];
      audit.opening_sixth_note_pitch = sixth->pitch;
      audit.opening_sixth_approach_interval = directedInterval(fifth->pitch, sixth->pitch);
      if (opening_subject_notes.size() >= 7) {
        const NoteEvent* seventh = opening_subject_notes[6];
        audit.opening_sixth_resolution_interval = directedInterval(sixth->pitch, seventh->pitch);
      }
    }
    for (size_t idx = 1; idx < opening_subject_notes.size(); ++idx) {
      const int directed_leap = directedInterval(opening_subject_notes[idx - 1]->pitch,
                                                 opening_subject_notes[idx]->pitch);
      const uint32_t leap = static_cast<uint32_t>(std::abs(directed_leap));
      audit.opening_subject_max_leap = std::max(audit.opening_subject_max_leap, leap);
      if (leap <= interval::kPerfect4th)
        continue;
      ++audit.opening_subject_large_leap_count;
      bool resolved = false;
      if (idx + 1 < opening_subject_notes.size()) {
        const int resolution = directedInterval(opening_subject_notes[idx]->pitch,
                                                opening_subject_notes[idx + 1]->pitch);
        resolved =
            resolution != 0 &&
            ((directed_leap > 0 && resolution < 0) || (directed_leap < 0 && resolution > 0)) &&
            std::abs(resolution) <= interval::kMajor2nd;
      }
      if (!resolved) {
        ++audit.opening_subject_unresolved_leap_count;
      }
    }
  }

  if (config.form == FormType::Fugue) {
    const Tick early_answer_counterline_start =
        secondsToTicks(6.5, static_cast<uint16_t>(config.bpm));
    const Tick early_answer_counterline_end = std::min<Tick>(
        secondsToTicks(8.75, static_cast<uint16_t>(config.bpm)), result.total_duration_ticks);
    std::array<std::vector<const NoteEvent*>, 8> countersubject_by_voice;
    for (const auto& track : result.tracks) {
      for (const auto& note : track.notes) {
        if (note.voice >= countersubject_by_voice.size())
          continue;
        if (note.source != BachNoteSource::Countersubject)
          continue;
        if (note.start_tick < early_answer_counterline_start ||
            note.start_tick >= early_answer_counterline_end) {
          continue;
        }
        countersubject_by_voice[note.voice].push_back(&note);
      }
    }
    for (auto& notes : countersubject_by_voice) {
      std::sort(notes.begin(), notes.end(), [](const NoteEvent* lhs, const NoteEvent* rhs) {
        if (lhs->start_tick != rhs->start_tick) {
          return lhs->start_tick < rhs->start_tick;
        }
        return lhs->duration < rhs->duration;
      });
      uint32_t same_pitch_run = 0;
      uint8_t previous_pitch = 0;
      bool has_previous_pitch = false;
      for (const NoteEvent* note : notes) {
        ++audit.early_answer_counterline_notes;
        if (has_previous_pitch && note->pitch == previous_pitch) {
          ++same_pitch_run;
        } else {
          same_pitch_run = 1;
        }
        audit.early_answer_counterline_same_pitch_run_max =
            std::max(audit.early_answer_counterline_same_pitch_run_max, same_pitch_run);
        previous_pitch = note->pitch;
        has_previous_pitch = true;
      }
    }
  }

  if (config.form == FormType::Fugue && !result.structure_json.empty() &&
      result.total_duration_ticks >= kTicksPerBar * 8) {
    const Tick sample_step = duration::kEighthNote;
    for (Tick tick = 0; tick < result.total_duration_ticks; tick += sample_step) {
      std::array<const NoteEvent*, 8> active_note{};
      uint32_t active_voice_count = 0;
      uint32_t composed_voice_count = 0;
      bool has_subject = false;
      bool has_dialogue = false;
      for (const auto& track : result.tracks) {
        for (const auto& note : track.notes) {
          if (note.voice >= active_note.size())
            continue;
          if (!noteSoundsAt(note, tick))
            continue;
          if (active_note[note.voice] == nullptr ||
              note.start_tick >= active_note[note.voice]->start_tick) {
            active_note[note.voice] = &note;
          }
        }
      }
      for (const NoteEvent* note : active_note) {
        if (note == nullptr)
          continue;
        ++active_voice_count;
        if (isComposedIntentAuditSource(note->source)) {
          ++composed_voice_count;
        }
        if (isSubjectAuditSource(note->source)) {
          has_subject = true;
        }
        if (isDialogueAuditSource(note->source)) {
          has_dialogue = true;
        }
      }
      if (active_voice_count < 2)
        continue;
      ++audit.two_voice_sample_count;
      if (composed_voice_count >= 2 && (has_subject || has_dialogue)) {
        ++audit.thematic_two_voice_sample_count;
      }
      if (has_subject && has_dialogue) {
        ++audit.subject_dialogue_pair_sample_count;
      }
    }

    const Tick clash_sample_step = duration::kSixteenthNote;
    for (Tick tick = 0; tick < early_exposition_end; tick += clash_sample_step) {
      std::array<const NoteEvent*, 8> active_note{};
      for (const auto& track : result.tracks) {
        for (const auto& note : track.notes) {
          if (note.voice >= active_note.size())
            continue;
          if (!noteSoundsAt(note, tick))
            continue;
          if (active_note[note.voice] == nullptr ||
              note.start_tick >= active_note[note.voice]->start_tick) {
            active_note[note.voice] = &note;
          }
        }
      }

      bool has_subject = false;
      bool has_dialogue = false;
      bool hard_clash = false;
      for (const NoteEvent* subject : active_note) {
        if (subject == nullptr || !isSubjectAuditSource(subject->source))
          continue;
        has_subject = true;
        for (const NoteEvent* dialogue : active_note) {
          if (dialogue == nullptr || subject->voice == dialogue->voice ||
              !isDialogueAuditSource(dialogue->source)) {
            continue;
          }
          has_dialogue = true;
          if (isExposedHardDissonance(subject->pitch, dialogue->pitch)) {
            hard_clash = true;
          }
        }
      }
      if (!has_subject || !has_dialogue)
        continue;
      ++audit.early_thematic_dialogue_pair_sample_count;
      ++audit.thematic_dialogue_pair_sample_count;
      if (hard_clash) {
        ++audit.early_thematic_dialogue_hard_clash_count;
        ++audit.thematic_dialogue_hard_clash_count;
      }
    }

    for (Tick tick = early_exposition_end; tick < result.total_duration_ticks;
         tick += clash_sample_step) {
      std::array<const NoteEvent*, 8> active_note{};
      for (const auto& track : result.tracks) {
        for (const auto& note : track.notes) {
          if (note.voice >= active_note.size())
            continue;
          if (!noteSoundsAt(note, tick))
            continue;
          if (active_note[note.voice] == nullptr ||
              note.start_tick >= active_note[note.voice]->start_tick) {
            active_note[note.voice] = &note;
          }
        }
      }

      bool has_subject = false;
      bool has_dialogue = false;
      bool hard_clash = false;
      for (const NoteEvent* subject : active_note) {
        if (subject == nullptr || !isSubjectAuditSource(subject->source))
          continue;
        has_subject = true;
        for (const NoteEvent* dialogue : active_note) {
          if (dialogue == nullptr || subject->voice == dialogue->voice ||
              !isDialogueAuditSource(dialogue->source)) {
            continue;
          }
          has_dialogue = true;
          if (isExposedHardDissonance(subject->pitch, dialogue->pitch)) {
            hard_clash = true;
          }
        }
      }
      if (!has_subject || !has_dialogue)
        continue;
      ++audit.thematic_dialogue_pair_sample_count;
      if (hard_clash) {
        ++audit.thematic_dialogue_hard_clash_count;
      }
    }

    std::vector<const NoteEvent*> bass_notes;
    for (const auto& track : result.tracks) {
      for (const auto& note : track.notes) {
        if (note.voice == bass_voice) {
          bass_notes.push_back(&note);
        }
      }
    }
    std::sort(bass_notes.begin(), bass_notes.end(), [](const NoteEvent* a, const NoteEvent* b) {
      if (a->start_tick != b->start_tick) {
        return a->start_tick < b->start_tick;
      }
      return a->pitch < b->pitch;
    });

    for (Tick tick = 0; tick < result.total_duration_ticks; tick += kTicksPerBeat) {
      ++audit.bass_activity_sample_count;
      for (const NoteEvent* note : bass_notes) {
        if (noteSoundsAt(*note, tick)) {
          ++audit.bass_active_sample_count;
          break;
        }
      }
    }

    const NoteEvent* previous_bass = nullptr;
    for (const NoteEvent* note : bass_notes) {
      if (previous_bass != nullptr) {
        const Tick previous_end = previous_bass->start_tick + previous_bass->duration;
        const Tick gap = note->start_tick > previous_end ? note->start_tick - previous_end : 0;
        const uint32_t leap =
            static_cast<uint32_t>(absoluteInterval(previous_bass->pitch, note->pitch));
        if (gap <= kTicksPerBar && leap > 0) {
          ++audit.bass_motion_intervals;
          audit.max_bass_leap = std::max(audit.max_bass_leap, leap);
          if (leap <= interval::kPerfect4th || leap == interval::kPerfect5th ||
              leap == interval::kOctave) {
            ++audit.bass_step_or_structural_motion_intervals;
          }
          if (leap > interval::kOctave) {
            ++audit.bass_large_leaps;
          }
        }
      }
      previous_bass = note;
    }
    const NoteEvent* final_bass = nullptr;
    for (const NoteEvent* note : bass_notes) {
      if (note->source != BachNoteSource::Coda)
        continue;
      if (final_bass == nullptr ||
          note->start_tick + note->duration > final_bass->start_tick + final_bass->duration ||
          (note->start_tick + note->duration == final_bass->start_tick + final_bass->duration &&
           note->start_tick > final_bass->start_tick)) {
        final_bass = note;
      }
    }
    if (final_bass != nullptr) {
      const int tonic_pc = getPitchClass(tonicPitch(config.key.tonic, 4));
      if (getPitchClass(final_bass->pitch) == tonic_pc) {
        ++audit.coda_final_tonic_bass_notes;
      }
    }
  }

  if (coda_required && opening_subject_notes.size() >= 4) {
    Tick coda_start = std::numeric_limits<Tick>::max();
    for (const auto& track : result.tracks) {
      for (const auto& note : track.notes) {
        if (note.source == BachNoteSource::Coda) {
          coda_start = std::min(coda_start, note.start_tick);
        }
      }
    }

    if (coda_start != std::numeric_limits<Tick>::max()) {
      std::vector<const NoteEvent*> coda_head_notes;
      for (const auto& track : result.tracks) {
        for (const auto& note : track.notes) {
          if (note.source != BachNoteSource::Coda || note.voice != 0)
            continue;
          if (note.start_tick < coda_start || note.start_tick >= coda_start + kTicksPerBar) {
            continue;
          }
          coda_head_notes.push_back(&note);
        }
      }
      std::sort(coda_head_notes.begin(), coda_head_notes.end(),
                [](const NoteEvent* lhs, const NoteEvent* rhs) {
                  if (lhs->start_tick != rhs->start_tick) {
                    return lhs->start_tick < rhs->start_tick;
                  }
                  return lhs->pitch < rhs->pitch;
                });

      const size_t interval_count = std::min(opening_subject_notes.size(), coda_head_notes.size());
      if (interval_count >= 4) {
        for (size_t idx = 1; idx < interval_count && idx < 5; ++idx) {
          const int subject_interval = directedInterval(opening_subject_notes.front()->pitch,
                                                        opening_subject_notes[idx]->pitch);
          const int coda_interval =
              directedInterval(coda_head_notes.front()->pitch, coda_head_notes[idx]->pitch);
          ++audit.coda_subject_head_interval_count;
          if (subject_interval == coda_interval) {
            ++audit.coda_subject_head_match_count;
          }
        }
      }
    }
  }

  auto collect_interval_pairs = [&](auto source_matches, std::vector<std::pair<int, int>>& pairs) {
    for (uint8_t voice = 0; voice < 8; ++voice) {
      std::vector<const NoteEvent*> voice_notes;
      for (const auto& track : result.tracks) {
        for (const auto& note : track.notes) {
          if (note.voice == voice && source_matches(note.source)) {
            voice_notes.push_back(&note);
          }
        }
      }
      std::sort(voice_notes.begin(), voice_notes.end(),
                [](const NoteEvent* lhs, const NoteEvent* rhs) {
                  if (lhs->start_tick != rhs->start_tick) {
                    return lhs->start_tick < rhs->start_tick;
                  }
                  return lhs->pitch < rhs->pitch;
                });
      for (size_t idx = 2; idx < voice_notes.size(); ++idx) {
        const NoteEvent* first = voice_notes[idx - 2];
        const NoteEvent* second = voice_notes[idx - 1];
        const NoteEvent* third = voice_notes[idx];
        const Tick first_gap = second->start_tick > first->start_tick + first->duration
                                   ? second->start_tick - (first->start_tick + first->duration)
                                   : 0;
        const Tick second_gap = third->start_tick > second->start_tick + second->duration
                                    ? third->start_tick - (second->start_tick + second->duration)
                                    : 0;
        if (first_gap > duration::kHalfNote || second_gap > duration::kHalfNote) {
          continue;
        }
        const int first_interval = directedSimpleInterval(first->pitch, second->pitch);
        const int second_interval = directedSimpleInterval(second->pitch, third->pitch);
        if (first_interval == 0 && second_interval == 0)
          continue;
        pairs.emplace_back(first_interval, second_interval);
      }
    }
  };

  collect_interval_pairs([](BachNoteSource source) { return isSubjectAuditSource(source); },
                         subject_motif_pairs);
  audit.subject_motif_interval_pair_count = static_cast<uint32_t>(subject_motif_pairs.size());

  std::vector<std::pair<int, int>> episode_motif_pairs;
  collect_interval_pairs([](BachNoteSource source) { return isEpisodeMotifAuditSource(source); },
                         episode_motif_pairs);
  audit.episode_motif_interval_pair_count = static_cast<uint32_t>(episode_motif_pairs.size());
  for (const auto& episode_pair : episode_motif_pairs) {
    const std::pair<int, int> inverted_pair =
        std::make_pair(-episode_pair.first, -episode_pair.second);
    if (std::find(subject_motif_pairs.begin(), subject_motif_pairs.end(), episode_pair) !=
            subject_motif_pairs.end() ||
        std::find(subject_motif_pairs.begin(), subject_motif_pairs.end(), inverted_pair) !=
            subject_motif_pairs.end()) {
      ++audit.episode_motif_derived_pair_count;
    }
  }

  if (audit.total_notes > 0) {
    audit.composed_intent_ratio =
        static_cast<double>(audit.composed_intent_notes) / static_cast<double>(audit.total_notes);
    audit.repair_modified_ratio =
        static_cast<double>(audit.repair_modified_notes) / static_cast<double>(audit.total_notes);
    audit.pitch_repair_modified_ratio = static_cast<double>(audit.pitch_repair_modified_notes) /
                                        static_cast<double>(audit.total_notes);
  }
  for (size_t idx = 0; idx < repair_windows.size(); ++idx) {
    const auto& window = repair_windows[idx];
    if (window.note_count < 4)
      continue;
    const double modified_ratio =
        static_cast<double>(window.modified_notes) / static_cast<double>(window.note_count);
    const double pitch_ratio =
        static_cast<double>(window.pitch_repair_notes) / static_cast<double>(window.note_count);
    const double current_score = std::max(pitch_ratio, modified_ratio * 0.75);
    const double best_score = std::max(audit.repair_hotspot_pitch_repair_ratio,
                                       audit.repair_hotspot_modified_ratio * 0.75);
    if (current_score > best_score ||
        (current_score == best_score &&
         window.pitch_repair_notes > audit.repair_hotspot_pitch_repair_notes)) {
      audit.repair_hotspot_bar = static_cast<uint32_t>(idx + 1);
      audit.repair_hotspot_note_count = window.note_count;
      audit.repair_hotspot_modified_notes = window.modified_notes;
      audit.repair_hotspot_pitch_repair_notes = window.pitch_repair_notes;
      audit.repair_hotspot_modified_ratio = modified_ratio;
      audit.repair_hotspot_pitch_repair_ratio = pitch_ratio;
    }
    if (window.episode_material_notes > 0) {
      const double episode_pitch_ratio =
          static_cast<double>(window.episode_material_pitch_repair_notes) /
          static_cast<double>(window.episode_material_notes);
      if (episode_pitch_ratio > audit.episode_material_repair_hotspot_pitch_repair_ratio ||
          (episode_pitch_ratio == audit.episode_material_repair_hotspot_pitch_repair_ratio &&
           window.episode_material_pitch_repair_notes >
               audit.episode_material_repair_hotspot_pitch_repair_notes)) {
        audit.episode_material_repair_hotspot_bar = static_cast<uint32_t>(idx + 1);
        audit.episode_material_repair_hotspot_note_count = window.episode_material_notes;
        audit.episode_material_repair_hotspot_pitch_repair_notes =
            window.episode_material_pitch_repair_notes;
        audit.episode_material_repair_hotspot_pitch_repair_ratio = episode_pitch_ratio;
      }
    }
  }
  for (size_t idx = 0; idx < episode_dialogue_windows.size(); ++idx) {
    const auto& window = episode_dialogue_windows[idx];
    const Tick window_start = static_cast<Tick>(idx) * kEpisodeDialogueWindowTicks;
    if (window_start >= (result.total_duration_ticks * 7) / 10)
      continue;
    if (window.motif_notes < 6)
      continue;
    ++audit.episode_dialogue_window_count;
    const double dialogue_ratio =
        static_cast<double>(window.dialogue_notes) / static_cast<double>(window.motif_notes);
    audit.min_episode_dialogue_window_ratio =
        std::min(audit.min_episode_dialogue_window_ratio, dialogue_ratio);
    if (window.dialogue_notes == 0 || dialogue_ratio < 0.12) {
      ++audit.episode_dialogue_weak_window_count;
    }
  }
  if (audit.episode_dialogue_window_count == 0) {
    audit.min_episode_dialogue_window_ratio = 0.0;
  }
  if (audit.immutable_notes > 0) {
    audit.immutable_pitch_repair_ratio = static_cast<double>(audit.immutable_pitch_repair_notes) /
                                         static_cast<double>(audit.immutable_notes);
  }
  if (audit.protected_dialogue_notes > 0) {
    audit.protected_dialogue_pitch_repair_ratio =
        static_cast<double>(audit.protected_dialogue_pitch_repair_notes) /
        static_cast<double>(audit.protected_dialogue_notes);
  }
  if (audit.flexible_notes > 0) {
    audit.flexible_pitch_repair_ratio = static_cast<double>(audit.flexible_pitch_repair_notes) /
                                        static_cast<double>(audit.flexible_notes);
  }
  if (audit.pitch_repair_modified_notes > 0) {
    audit.episode_material_pitch_repair_ratio =
        static_cast<double>(audit.episode_material_pitch_repair_notes) /
        static_cast<double>(audit.pitch_repair_modified_notes);
  }
  if (audit.episode_material_notes > 0) {
    audit.episode_material_pitch_repair_density =
        static_cast<double>(audit.episode_material_pitch_repair_notes) /
        static_cast<double>(audit.episode_material_notes);
  }
  if (audit.listening_hotspot_notes > 0) {
    audit.listening_hotspot_pitch_repair_ratio =
        static_cast<double>(audit.listening_hotspot_pitch_repair_notes) /
        static_cast<double>(audit.listening_hotspot_notes);
  }
  if (audit.early_exposition_notes > 0) {
    audit.early_exposition_dialogue_ratio =
        static_cast<double>(audit.early_exposition_dialogue_notes) /
        static_cast<double>(audit.early_exposition_notes);
    audit.early_exposition_free_counterpoint_ratio =
        static_cast<double>(audit.early_exposition_free_counterpoint_notes) /
        static_cast<double>(audit.early_exposition_notes);
    audit.early_exposition_pitch_repair_ratio =
        static_cast<double>(audit.early_exposition_pitch_repair_notes) /
        static_cast<double>(audit.early_exposition_notes);
  }
  if (audit.early_exposition_free_counterpoint_notes > 0) {
    audit.early_exposition_free_counterpoint_pitch_repair_ratio =
        static_cast<double>(audit.early_exposition_free_counterpoint_pitch_repair_notes) /
        static_cast<double>(audit.early_exposition_free_counterpoint_notes);
  }
  if (audit.resolve_region_notes > 0) {
    audit.resolve_region_pitch_repair_ratio =
        static_cast<double>(audit.resolve_region_pitch_repair_notes) /
        static_cast<double>(audit.resolve_region_notes);
  }
  if (audit.coda_notes > 0) {
    audit.coda_pitch_repair_ratio =
        static_cast<double>(audit.coda_pitch_repair_notes) / static_cast<double>(audit.coda_notes);
  }
  if (audit.coda_subject_head_interval_count > 0) {
    audit.coda_subject_head_match_ratio =
        static_cast<double>(audit.coda_subject_head_match_count) /
        static_cast<double>(audit.coda_subject_head_interval_count);
  }
  if (audit.two_voice_sample_count > 0) {
    audit.thematic_two_voice_ratio = static_cast<double>(audit.thematic_two_voice_sample_count) /
                                     static_cast<double>(audit.two_voice_sample_count);
    audit.subject_dialogue_pair_ratio =
        static_cast<double>(audit.subject_dialogue_pair_sample_count) /
        static_cast<double>(audit.two_voice_sample_count);
  }
  if (audit.thematic_dialogue_pair_sample_count > 0) {
    audit.thematic_dialogue_hard_clash_ratio =
        static_cast<double>(audit.thematic_dialogue_hard_clash_count) /
        static_cast<double>(audit.thematic_dialogue_pair_sample_count);
  }
  if (audit.early_thematic_dialogue_pair_sample_count > 0) {
    audit.early_thematic_dialogue_hard_clash_ratio =
        static_cast<double>(audit.early_thematic_dialogue_hard_clash_count) /
        static_cast<double>(audit.early_thematic_dialogue_pair_sample_count);
  }
  if (audit.episode_motif_interval_pair_count > 0) {
    audit.episode_motif_derivation_ratio =
        static_cast<double>(audit.episode_motif_derived_pair_count) /
        static_cast<double>(audit.episode_motif_interval_pair_count);
  }
  if (audit.bass_activity_sample_count > 0) {
    audit.bass_activity_ratio = static_cast<double>(audit.bass_active_sample_count) /
                                static_cast<double>(audit.bass_activity_sample_count);
  }
  if (audit.bass_motion_intervals > 0) {
    audit.bass_step_or_structural_motion_ratio =
        static_cast<double>(audit.bass_step_or_structural_motion_intervals) /
        static_cast<double>(audit.bass_motion_intervals);
  }
  for (auto& hotspot : audit.listening_hotspots) {
    if (hotspot.note_count > 0) {
      hotspot.pitch_repair_ratio = static_cast<double>(hotspot.pitch_repair_modified_notes) /
                                   static_cast<double>(hotspot.note_count);
    }
    hotspot.pitch_repair_pass = hotspot.pitch_repair_ratio <= 0.35;
    audit.listening_hotspot_pitch_repair_pass =
        audit.listening_hotspot_pitch_repair_pass && hotspot.pitch_repair_pass;
  }

  for (auto& hotspot : audit.listening_hotspots) {
    Tick bar_start = static_cast<Tick>(hotspot.bar - 1) * kTicksPerBar;
    if (bar_start >= result.total_duration_ticks) {
      hotspot.covered = false;
      hotspot.texture_pass = true;
      hotspot.hard_clash_pass = true;
      continue;
    }
    hotspot.covered = true;
    hotspot.min_active_voices = std::numeric_limits<uint32_t>::max();
    for (int beat = 0; beat < 4; ++beat) {
      Tick sample_tick = bar_start + static_cast<Tick>(beat) * kTicksPerBeat;
      uint32_t active = 0;
      std::array<bool, 8> voice_active{};
      for (const auto& track : result.tracks) {
        for (const auto& note : track.notes) {
          if (note.start_tick <= sample_tick && sample_tick < note.start_tick + note.duration &&
              note.voice < voice_active.size() && !voice_active[note.voice]) {
            voice_active[note.voice] = true;
            ++active;
          }
        }
      }
      ++hotspot.active_voice_sample_count;
      hotspot.active_voice_sample_sum += active;
      hotspot.min_active_voices = std::min(hotspot.min_active_voices, active);
      hotspot.max_active_voices = std::max(hotspot.max_active_voices, active);
    }
    for (Tick sample_tick = bar_start;
         sample_tick < bar_start + kTicksPerBar && sample_tick < result.total_duration_ticks;
         sample_tick += duration::kSixteenthNote) {
      std::array<const NoteEvent*, 8> active_note{};
      for (const auto& track : result.tracks) {
        for (const auto& note : track.notes) {
          if (note.voice >= active_note.size())
            continue;
          if (!noteSoundsAt(note, sample_tick))
            continue;
          if (active_note[note.voice] == nullptr ||
              note.start_tick >= active_note[note.voice]->start_tick) {
            active_note[note.voice] = &note;
          }
        }
      }
      uint32_t active = 0;
      bool hard_clash = false;
      for (size_t lhs = 0; lhs < active_note.size(); ++lhs) {
        if (active_note[lhs] == nullptr)
          continue;
        ++active;
        for (size_t rhs = lhs + 1; rhs < active_note.size(); ++rhs) {
          if (active_note[rhs] == nullptr)
            continue;
          if (isExposedHardDissonance(active_note[lhs]->pitch, active_note[rhs]->pitch)) {
            hard_clash = true;
          }
        }
      }
      if (active < 2)
        continue;
      ++hotspot.hard_clash_sample_count;
      if (hard_clash) {
        ++hotspot.hard_clash_count;
      }
    }
    if (hotspot.active_voice_sample_count > 0) {
      hotspot.average_active_voices = static_cast<double>(hotspot.active_voice_sample_sum) /
                                      static_cast<double>(hotspot.active_voice_sample_count);
    }
    if (hotspot.hard_clash_sample_count > 0) {
      hotspot.hard_clash_ratio = static_cast<double>(hotspot.hard_clash_count) /
                                 static_cast<double>(hotspot.hard_clash_sample_count);
    }
    hotspot.texture_pass = hotspot.average_active_voices >= 2.0 && hotspot.max_active_voices >= 2;
    hotspot.hard_clash_pass = hotspot.hard_clash_sample_count == 0 ||
                              (hotspot.hard_clash_ratio <= 0.25 && hotspot.hard_clash_count <= 4);
    audit.listening_hotspot_texture_pass =
        audit.listening_hotspot_texture_pass && hotspot.texture_pass;
    audit.listening_hotspot_hard_clash_pass =
        audit.listening_hotspot_hard_clash_pass && hotspot.hard_clash_pass;
  }

  const Tick early_listening_start = secondsToTicks(6.0, static_cast<uint16_t>(config.bpm));
  const Tick early_listening_end = std::min<Tick>(
      secondsToTicks(12.0, static_cast<uint16_t>(config.bpm)), result.total_duration_ticks);
  if (early_listening_start < early_listening_end) {
    for (Tick sample_tick = early_listening_start; sample_tick < early_listening_end;
         sample_tick += duration::kSixteenthNote) {
      std::array<const NoteEvent*, 8> active_note{};
      for (const auto& track : result.tracks) {
        for (const auto& note : track.notes) {
          if (note.voice >= active_note.size())
            continue;
          if (!noteSoundsAt(note, sample_tick))
            continue;
          if (active_note[note.voice] == nullptr ||
              note.start_tick >= active_note[note.voice]->start_tick) {
            active_note[note.voice] = &note;
          }
        }
      }
      uint32_t active = 0;
      bool hard_clash = false;
      bool unstable_interval = false;
      for (size_t lhs = 0; lhs < active_note.size(); ++lhs) {
        if (active_note[lhs] == nullptr)
          continue;
        ++active;
        for (size_t rhs = lhs + 1; rhs < active_note.size(); ++rhs) {
          if (active_note[rhs] == nullptr)
            continue;
          if (isExposedHardDissonance(active_note[lhs]->pitch, active_note[rhs]->pitch)) {
            hard_clash = true;
          }
          if (isEarlyListeningUnstableInterval(*active_note[lhs], *active_note[rhs], bass_voice)) {
            unstable_interval = true;
          }
        }
      }
      if (active < 2)
        continue;
      ++audit.early_listening_hard_clash_sample_count;
      ++audit.early_listening_unstable_interval_sample_count;
      if (hard_clash) {
        ++audit.early_listening_hard_clash_count;
      }
      if (unstable_interval) {
        ++audit.early_listening_unstable_interval_count;
      }
    }
  }
  if (audit.early_listening_hard_clash_sample_count > 0) {
    audit.early_listening_hard_clash_ratio =
        static_cast<double>(audit.early_listening_hard_clash_count) /
        static_cast<double>(audit.early_listening_hard_clash_sample_count);
  }
  if (audit.early_listening_unstable_interval_sample_count > 0) {
    audit.early_listening_unstable_interval_ratio =
        static_cast<double>(audit.early_listening_unstable_interval_count) /
        static_cast<double>(audit.early_listening_unstable_interval_sample_count);
  }

  const Tick eight_second_window_start = secondsToTicks(7.5, static_cast<uint16_t>(config.bpm));
  const Tick eight_second_window_end = std::min<Tick>(
      secondsToTicks(8.75, static_cast<uint16_t>(config.bpm)), result.total_duration_ticks);
  if (eight_second_window_start < eight_second_window_end) {
    for (Tick sample_tick = eight_second_window_start; sample_tick < eight_second_window_end;
         sample_tick += duration::kSixteenthNote) {
      std::array<const NoteEvent*, 8> active_note{};
      for (const auto& track : result.tracks) {
        for (const auto& note : track.notes) {
          if (note.voice >= active_note.size())
            continue;
          if (!noteSoundsAt(note, sample_tick))
            continue;
          if (active_note[note.voice] == nullptr ||
              note.start_tick >= active_note[note.voice]->start_tick) {
            active_note[note.voice] = &note;
          }
        }
      }

      bool has_dialogue_pair = false;
      bool hard_clash = false;
      bool unstable_interval = false;
      for (const NoteEvent* subject : active_note) {
        if (subject == nullptr || !isSubjectAuditSource(subject->source))
          continue;
        for (const NoteEvent* dialogue : active_note) {
          if (dialogue == nullptr || dialogue->voice == subject->voice ||
              !isDialogueAuditSource(dialogue->source)) {
            continue;
          }
          has_dialogue_pair = true;
          if (isExposedHardDissonance(subject->pitch, dialogue->pitch)) {
            hard_clash = true;
          }
          if (isEarlyListeningUnstableInterval(*subject, *dialogue, bass_voice)) {
            unstable_interval = true;
          }
        }
      }
      if (!has_dialogue_pair)
        continue;
      ++audit.eight_second_dialogue_sample_count;
      if (hard_clash) {
        ++audit.eight_second_dialogue_hard_clash_count;
      }
      if (unstable_interval) {
        ++audit.eight_second_dialogue_unstable_interval_count;
      }
    }
  }
  if (audit.eight_second_dialogue_sample_count > 0) {
    audit.eight_second_dialogue_hard_clash_ratio =
        static_cast<double>(audit.eight_second_dialogue_hard_clash_count) /
        static_cast<double>(audit.eight_second_dialogue_sample_count);
    audit.eight_second_dialogue_unstable_interval_ratio =
        static_cast<double>(audit.eight_second_dialogue_unstable_interval_count) /
        static_cast<double>(audit.eight_second_dialogue_sample_count);
  }

  for (auto& window : audit.critic_time_windows) {
    const Tick window_start =
        secondsToTicks(window.start_seconds, static_cast<uint16_t>(config.bpm));
    const Tick window_end =
        std::min<Tick>(secondsToTicks(window.end_seconds, static_cast<uint16_t>(config.bpm)),
                       result.total_duration_ticks);
    if (window_start >= window_end)
      continue;

    for (Tick sample_tick = window_start; sample_tick < window_end;
         sample_tick += duration::kSixteenthNote) {
      std::array<const NoteEvent*, 8> active_note{};
      for (const auto& track : result.tracks) {
        for (const auto& note : track.notes) {
          if (note.voice >= active_note.size())
            continue;
          if (!noteSoundsAt(note, sample_tick))
            continue;
          if (active_note[note.voice] == nullptr ||
              note.start_tick >= active_note[note.voice]->start_tick) {
            active_note[note.voice] = &note;
          }
        }
      }

      uint32_t active = 0;
      bool hard_clash = false;
      bool unstable_interval = false;
      bool pitch_repair_active = false;
      for (size_t lhs = 0; lhs < active_note.size(); ++lhs) {
        if (active_note[lhs] == nullptr)
          continue;
        ++active;
        if (hasPitchRepair(active_note[lhs]->modified_by)) {
          pitch_repair_active = true;
        }
        for (size_t rhs = lhs + 1; rhs < active_note.size(); ++rhs) {
          if (active_note[rhs] == nullptr)
            continue;
          if (isExposedHardDissonance(active_note[lhs]->pitch, active_note[rhs]->pitch)) {
            hard_clash = true;
          }
          if (isEarlyListeningUnstableInterval(*active_note[lhs], *active_note[rhs], bass_voice)) {
            unstable_interval = true;
          }
        }
      }
      if (active == 0)
        continue;
      ++window.sample_count;
      window.active_voice_sample_sum += active;
      if (active < 2) {
        ++window.low_texture_count;
      }
      if (hard_clash) {
        ++window.hard_clash_count;
      }
      if (unstable_interval) {
        ++window.unstable_interval_count;
      }
      if (pitch_repair_active) {
        ++window.pitch_repair_active_count;
      }
    }

    if (window.sample_count > 0) {
      const double sample_count = static_cast<double>(window.sample_count);
      window.hard_clash_ratio = static_cast<double>(window.hard_clash_count) / sample_count;
      window.unstable_interval_ratio =
          static_cast<double>(window.unstable_interval_count) / sample_count;
      window.pitch_repair_active_ratio =
          static_cast<double>(window.pitch_repair_active_count) / sample_count;
      window.low_texture_ratio = static_cast<double>(window.low_texture_count) / sample_count;
      window.average_active_voices =
          static_cast<double>(window.active_voice_sample_sum) / sample_count;
    }
    window.pass = window.sample_count == 0 ||
                  (window.hard_clash_count == 0 && window.unstable_interval_ratio <= 0.20 &&
                   window.pitch_repair_active_ratio <= 0.35 && window.low_texture_ratio <= 0.50 &&
                   window.average_active_voices >= 1.5);
    audit.critic_time_window_pass = audit.critic_time_window_pass && window.pass;
  }

  const uint32_t min_episode_dialogue = std::max<uint32_t>(8u, audit.total_notes / 12u);
  const uint32_t min_bass_intent =
      audit.bass_voice_notes == 0 ? 0u : std::max<uint32_t>(4u, audit.bass_voice_notes / 5u);

  audit.episode_dialogue_pass = audit.episode_dialogue_notes >= min_episode_dialogue &&
                                audit.protected_dialogue_notes >= min_episode_dialogue;
  audit.bass_intent_pass =
      audit.bass_voice_notes == 0 || audit.bass_intent_notes >= min_bass_intent;
  audit.bass_line_motion_pass =
      !coda_required ||
      (audit.bass_activity_ratio >= 0.25 && audit.bass_motion_intervals >= 8 &&
       audit.bass_step_or_structural_motion_ratio >= 0.80 && audit.bass_large_leaps <= 1 &&
       audit.max_bass_leap <= interval::kOctave + interval::kMajor2nd);
  audit.repair_surrogate_pass = audit.pitch_repair_modified_ratio <= 0.25;
  audit.early_exposition_intent_pass =
      audit.early_exposition_pitch_repair_ratio <= 0.12 &&
      audit.early_exposition_free_counterpoint_pitch_repair_ratio <= 0.28;
  audit.early_exposition_dialogue_pass = audit.early_exposition_dialogue_ratio >= 0.65 &&
                                         audit.early_exposition_free_counterpoint_ratio <= 0.30;
  audit.early_exposition_counterline_pass = audit.early_exposition_counterline_large_leaps == 0;
  audit.early_answer_counterline_repetition_pass =
      audit.early_answer_counterline_notes == 0 ||
      audit.early_answer_counterline_same_pitch_run_max <= 2;
  audit.resolve_region_repair_pass = !coda_required || audit.resolve_region_notes == 0 ||
                                     (audit.resolve_region_pitch_repair_ratio <= 0.16 &&
                                      audit.resolve_region_pitch_repair_notes <=
                                          std::max<uint32_t>(6u, audit.resolve_region_notes / 8u));
  audit.pitch_repair_run_pass = audit.max_pitch_repair_run <= 3;
  audit.repair_hotspot_pass =
      audit.repair_hotspot_note_count == 0 ||
      (audit.repair_hotspot_pitch_repair_ratio <= 0.40 &&
       audit.repair_hotspot_pitch_repair_notes <= 6 &&
       (audit.repair_hotspot_modified_ratio <= 0.70 || audit.repair_hotspot_modified_notes <= 3));
  const uint32_t episode_material_repair_cap =
      std::max<uint32_t>(4u, audit.episode_material_notes / 32u);
  audit.episode_material_repair_dependency_pass =
      audit.episode_material_pitch_repair_notes <= episode_material_repair_cap &&
      audit.episode_material_pitch_repair_density <= 0.08 &&
      audit.episode_material_repair_hotspot_pitch_repair_notes <= 1;
  audit.opening_sixth_note_pass =
      audit.opening_subject_note_count < 6 ||
      std::abs(audit.opening_sixth_approach_interval) <= interval::kPerfect4th ||
      (audit.opening_subject_note_count >= 7 && audit.opening_sixth_resolution_interval != 0 &&
       ((audit.opening_sixth_approach_interval > 0 &&
         audit.opening_sixth_resolution_interval < 0) ||
        (audit.opening_sixth_approach_interval < 0 &&
         audit.opening_sixth_resolution_interval > 0)) &&
       std::abs(audit.opening_sixth_resolution_interval) <= interval::kMajor2nd);
  audit.opening_subject_contour_pass =
      audit.opening_subject_note_count == 0 ||
      (audit.opening_subject_max_leap <= interval::kOctave &&
       audit.opening_subject_unresolved_leap_count == 0 &&
       audit.opening_subject_large_leap_count <= 2 && audit.opening_sixth_note_pass);
  audit.thematic_two_voice_pass =
      audit.two_voice_sample_count == 0 ||
      (audit.thematic_two_voice_ratio >= 0.40 && audit.subject_dialogue_pair_ratio >= 0.25);
  audit.thematic_dialogue_clash_pass = audit.thematic_dialogue_pair_sample_count == 0 ||
                                       audit.thematic_dialogue_hard_clash_ratio <= 0.25;
  audit.early_thematic_dialogue_clash_pass =
      audit.early_thematic_dialogue_pair_sample_count == 0 ||
      (audit.early_thematic_dialogue_hard_clash_ratio <= 0.22 &&
       audit.early_thematic_dialogue_hard_clash_count <= 20);
  audit.early_listening_hard_clash_pass = audit.early_listening_hard_clash_sample_count == 0 ||
                                          (audit.early_listening_hard_clash_ratio <= 0.25 &&
                                           audit.early_listening_hard_clash_count <= 8);
  audit.early_listening_unstable_interval_pass =
      audit.early_listening_unstable_interval_sample_count == 0 ||
      (audit.early_listening_unstable_interval_ratio <= 0.30 &&
       audit.early_listening_unstable_interval_count <= 10);
  audit.eight_second_dialogue_pass = audit.eight_second_dialogue_sample_count == 0 ||
                                     (audit.eight_second_dialogue_hard_clash_count == 0 &&
                                      audit.eight_second_dialogue_unstable_interval_count == 0);
  audit.episode_dialogue_window_pass = !coda_required || audit.episode_dialogue_window_count == 0 ||
                                       audit.episode_dialogue_weak_window_count == 0;
  audit.episode_motif_derivation_pass = !coda_required ||
                                        audit.episode_motif_interval_pair_count == 0 ||
                                        (audit.subject_motif_interval_pair_count >= 2 &&
                                         audit.episode_motif_derivation_ratio >= 0.18);
  audit.repair_dependency_pass =
      audit.protected_subject_pitch_repair_notes == 0 &&
      audit.protected_dialogue_pitch_repair_ratio <= 0.10 &&
      audit.immutable_pitch_repair_ratio <= 0.10 && audit.pitch_repair_modified_ratio <= 0.20 &&
      audit.flexible_pitch_repair_ratio <= 0.45 && audit.episode_material_repair_dependency_pass &&
      audit.composed_intent_ratio >= 0.45 && audit.pitch_repair_run_pass &&
      audit.resolve_region_repair_pass && audit.repair_hotspot_pass &&
      audit.opening_subject_contour_pass && audit.listening_hotspot_pitch_repair_pass;
  audit.flexible_contour_pass =
      audit.flexible_remote_leap_count == 0 &&
      audit.flexible_large_leap_count <= std::max<uint32_t>(2u, audit.flexible_notes / 35u);
  audit.coda_intent_pass =
      !coda_required || (audit.coda_notes > 0 && audit.coda_notes <= 72 &&
                         audit.coda_sixteenth_notes <= 4 && audit.coda_pitch_repair_notes <= 6);
  audit.coda_closing_cadence_pass =
      !coda_required || (audit.coda_bass_notes > 0 && audit.coda_final_tonic_bass_notes > 0 &&
                         audit.coda_pitch_repair_ratio <= 0.20);
  audit.coda_subject_head_pass = !coda_required || (audit.coda_subject_head_interval_count >= 3 &&
                                                    audit.coda_subject_head_match_ratio >= 0.75);
  audit.pass = audit.episode_dialogue_pass && audit.bass_intent_pass &&
               audit.bass_line_motion_pass && audit.repair_surrogate_pass &&
               audit.repair_dependency_pass && audit.early_exposition_intent_pass &&
               audit.early_exposition_dialogue_pass && audit.early_exposition_counterline_pass &&
               audit.early_answer_counterline_repetition_pass && audit.resolve_region_repair_pass &&
               audit.flexible_contour_pass && audit.coda_intent_pass &&
               audit.coda_closing_cadence_pass && audit.coda_subject_head_pass &&
               audit.pitch_repair_run_pass && audit.repair_hotspot_pass &&
               audit.opening_subject_contour_pass && audit.global_arc_pass &&
               audit.thematic_two_voice_pass && audit.thematic_dialogue_clash_pass &&
               audit.early_thematic_dialogue_clash_pass && audit.early_listening_hard_clash_pass &&
               audit.early_listening_unstable_interval_pass && audit.eight_second_dialogue_pass &&
               audit.critic_time_window_pass && audit.episode_dialogue_window_pass &&
               audit.episode_motif_derivation_pass && audit.listening_hotspot_pitch_repair_pass &&
               audit.listening_hotspot_hard_clash_pass && audit.listening_hotspot_texture_pass;
  return audit;
}

void writeSourceSummary(JsonWriter& writer, const GeneratorResult& result) {
  std::array<uint32_t, kNoteSourceCount> counts{};
  for (const auto& track : result.tracks) {
    for (const auto& note : track.notes) {
      size_t idx = static_cast<size_t>(note.source);
      if (idx < counts.size()) {
        ++counts[idx];
      }
    }
  }

  writer.key("source_summary");
  writer.beginArray();
  for (size_t idx = 0; idx < counts.size(); ++idx) {
    if (counts[idx] == 0)
      continue;
    auto source = static_cast<BachNoteSource>(idx);
    writer.beginObject();
    writer.key("source");
    writer.value(std::string(bachNoteSourceToString(source)));
    writer.key("count");
    writer.value(counts[idx]);
    writer.endObject();
  }
  writer.endArray();
}

void writeStructureAudit(JsonWriter& writer, const StructureAuditSummary& audit) {
  writer.key("structure_audit");
  writer.beginObject();
  writer.key("pass");
  writer.value(audit.pass);
  writer.key("episode_dialogue_pass");
  writer.value(audit.episode_dialogue_pass);
  writer.key("bass_intent_pass");
  writer.value(audit.bass_intent_pass);
  writer.key("bass_line_motion_pass");
  writer.value(audit.bass_line_motion_pass);
  writer.key("repair_surrogate_pass");
  writer.value(audit.repair_surrogate_pass);
  writer.key("repair_dependency_pass");
  writer.value(audit.repair_dependency_pass);
  writer.key("flexible_contour_pass");
  writer.value(audit.flexible_contour_pass);
  writer.key("listening_hotspot_pitch_repair_pass");
  writer.value(audit.listening_hotspot_pitch_repair_pass);
  writer.key("listening_hotspot_texture_pass");
  writer.value(audit.listening_hotspot_texture_pass);
  writer.key("listening_hotspot_hard_clash_pass");
  writer.value(audit.listening_hotspot_hard_clash_pass);
  writer.key("early_listening_hard_clash_pass");
  writer.value(audit.early_listening_hard_clash_pass);
  writer.key("early_listening_unstable_interval_pass");
  writer.value(audit.early_listening_unstable_interval_pass);
  writer.key("eight_second_dialogue_pass");
  writer.value(audit.eight_second_dialogue_pass);
  writer.key("critic_time_window_pass");
  writer.value(audit.critic_time_window_pass);
  writer.key("early_exposition_intent_pass");
  writer.value(audit.early_exposition_intent_pass);
  writer.key("early_exposition_dialogue_pass");
  writer.value(audit.early_exposition_dialogue_pass);
  writer.key("early_exposition_counterline_pass");
  writer.value(audit.early_exposition_counterline_pass);
  writer.key("early_answer_counterline_repetition_pass");
  writer.value(audit.early_answer_counterline_repetition_pass);
  writer.key("resolve_region_repair_pass");
  writer.value(audit.resolve_region_repair_pass);
  writer.key("coda_intent_pass");
  writer.value(audit.coda_intent_pass);
  writer.key("coda_closing_cadence_pass");
  writer.value(audit.coda_closing_cadence_pass);
  writer.key("coda_subject_head_pass");
  writer.value(audit.coda_subject_head_pass);
  writer.key("pitch_repair_run_pass");
  writer.value(audit.pitch_repair_run_pass);
  writer.key("repair_hotspot_pass");
  writer.value(audit.repair_hotspot_pass);
  writer.key("episode_material_repair_dependency_pass");
  writer.value(audit.episode_material_repair_dependency_pass);
  writer.key("opening_sixth_note_pass");
  writer.value(audit.opening_sixth_note_pass);
  writer.key("opening_subject_contour_pass");
  writer.value(audit.opening_subject_contour_pass);
  writer.key("global_arc_pass");
  writer.value(audit.global_arc_pass);
  writer.key("thematic_two_voice_pass");
  writer.value(audit.thematic_two_voice_pass);
  writer.key("thematic_dialogue_clash_pass");
  writer.value(audit.thematic_dialogue_clash_pass);
  writer.key("early_thematic_dialogue_clash_pass");
  writer.value(audit.early_thematic_dialogue_clash_pass);
  writer.key("episode_dialogue_window_pass");
  writer.value(audit.episode_dialogue_window_pass);
  writer.key("episode_motif_derivation_pass");
  writer.value(audit.episode_motif_derivation_pass);
  writer.key("total_notes");
  writer.value(audit.total_notes);
  writer.key("composed_intent_notes");
  writer.value(audit.composed_intent_notes);
  writer.key("protected_subject_notes");
  writer.value(audit.protected_subject_notes);
  writer.key("protected_dialogue_notes");
  writer.value(audit.protected_dialogue_notes);
  writer.key("episode_dialogue_notes");
  writer.value(audit.episode_dialogue_notes);
  writer.key("episode_material_notes");
  writer.value(audit.episode_material_notes);
  writer.key("bass_voice_notes");
  writer.value(audit.bass_voice_notes);
  writer.key("bass_intent_notes");
  writer.value(audit.bass_intent_notes);
  writer.key("bass_activity_sample_count");
  writer.value(audit.bass_activity_sample_count);
  writer.key("bass_active_sample_count");
  writer.value(audit.bass_active_sample_count);
  writer.key("bass_motion_intervals");
  writer.value(audit.bass_motion_intervals);
  writer.key("bass_step_or_structural_motion_intervals");
  writer.value(audit.bass_step_or_structural_motion_intervals);
  writer.key("bass_large_leaps");
  writer.value(audit.bass_large_leaps);
  writer.key("max_bass_leap");
  writer.value(audit.max_bass_leap);
  writer.key("immutable_notes");
  writer.value(audit.immutable_notes);
  writer.key("flexible_notes");
  writer.value(audit.flexible_notes);
  writer.key("repair_modified_notes");
  writer.value(audit.repair_modified_notes);
  writer.key("pitch_repair_modified_notes");
  writer.value(audit.pitch_repair_modified_notes);
  writer.key("repair_hotspot_bar");
  writer.value(audit.repair_hotspot_bar);
  writer.key("repair_hotspot_note_count");
  writer.value(audit.repair_hotspot_note_count);
  writer.key("repair_hotspot_modified_notes");
  writer.value(audit.repair_hotspot_modified_notes);
  writer.key("repair_hotspot_pitch_repair_notes");
  writer.value(audit.repair_hotspot_pitch_repair_notes);
  writer.key("episode_material_repair_hotspot_bar");
  writer.value(audit.episode_material_repair_hotspot_bar);
  writer.key("episode_material_repair_hotspot_note_count");
  writer.value(audit.episode_material_repair_hotspot_note_count);
  writer.key("episode_material_repair_hotspot_pitch_repair_notes");
  writer.value(audit.episode_material_repair_hotspot_pitch_repair_notes);
  writer.key("protected_subject_pitch_repair_notes");
  writer.value(audit.protected_subject_pitch_repair_notes);
  writer.key("protected_dialogue_pitch_repair_notes");
  writer.value(audit.protected_dialogue_pitch_repair_notes);
  writer.key("immutable_pitch_repair_notes");
  writer.value(audit.immutable_pitch_repair_notes);
  writer.key("flexible_pitch_repair_notes");
  writer.value(audit.flexible_pitch_repair_notes);
  writer.key("countersubject_pitch_repair_notes");
  writer.value(audit.countersubject_pitch_repair_notes);
  writer.key("sequence_pitch_repair_notes");
  writer.value(audit.sequence_pitch_repair_notes);
  writer.key("episode_material_pitch_repair_notes");
  writer.value(audit.episode_material_pitch_repair_notes);
  writer.key("cadence_approach_pitch_repair_notes");
  writer.value(audit.cadence_approach_pitch_repair_notes);
  writer.key("coda_source_pitch_repair_notes");
  writer.value(audit.coda_source_pitch_repair_notes);
  writer.key("duration_repair_only_notes");
  writer.value(audit.duration_repair_only_notes);
  writer.key("listening_hotspot_notes");
  writer.value(audit.listening_hotspot_notes);
  writer.key("listening_hotspot_pitch_repair_notes");
  writer.value(audit.listening_hotspot_pitch_repair_notes);
  writer.key("early_listening_hard_clash_sample_count");
  writer.value(audit.early_listening_hard_clash_sample_count);
  writer.key("early_listening_hard_clash_count");
  writer.value(audit.early_listening_hard_clash_count);
  writer.key("early_listening_unstable_interval_sample_count");
  writer.value(audit.early_listening_unstable_interval_sample_count);
  writer.key("early_listening_unstable_interval_count");
  writer.value(audit.early_listening_unstable_interval_count);
  writer.key("eight_second_dialogue_sample_count");
  writer.value(audit.eight_second_dialogue_sample_count);
  writer.key("eight_second_dialogue_hard_clash_count");
  writer.value(audit.eight_second_dialogue_hard_clash_count);
  writer.key("eight_second_dialogue_unstable_interval_count");
  writer.value(audit.eight_second_dialogue_unstable_interval_count);
  writer.key("critic_time_windows");
  writer.beginArray();
  for (const auto& window : audit.critic_time_windows) {
    writer.beginObject();
    writer.key("label");
    writer.value(std::string(window.label));
    writer.key("start_seconds");
    writer.value(window.start_seconds);
    writer.key("end_seconds");
    writer.value(window.end_seconds);
    writer.key("sample_count");
    writer.value(window.sample_count);
    writer.key("hard_clash_count");
    writer.value(window.hard_clash_count);
    writer.key("unstable_interval_count");
    writer.value(window.unstable_interval_count);
    writer.key("pitch_repair_active_count");
    writer.value(window.pitch_repair_active_count);
    writer.key("low_texture_count");
    writer.value(window.low_texture_count);
    writer.key("average_active_voices");
    writer.value(window.average_active_voices);
    writer.key("hard_clash_ratio");
    writer.value(window.hard_clash_ratio);
    writer.key("unstable_interval_ratio");
    writer.value(window.unstable_interval_ratio);
    writer.key("pitch_repair_active_ratio");
    writer.value(window.pitch_repair_active_ratio);
    writer.key("low_texture_ratio");
    writer.value(window.low_texture_ratio);
    writer.key("pass");
    writer.value(window.pass);
    writer.endObject();
  }
  writer.endArray();
  writer.key("early_exposition_notes");
  writer.value(audit.early_exposition_notes);
  writer.key("early_exposition_dialogue_notes");
  writer.value(audit.early_exposition_dialogue_notes);
  writer.key("early_exposition_pitch_repair_notes");
  writer.value(audit.early_exposition_pitch_repair_notes);
  writer.key("early_exposition_free_counterpoint_notes");
  writer.value(audit.early_exposition_free_counterpoint_notes);
  writer.key("early_exposition_free_counterpoint_pitch_repair_notes");
  writer.value(audit.early_exposition_free_counterpoint_pitch_repair_notes);
  writer.key("early_exposition_counterline_large_leaps");
  writer.value(audit.early_exposition_counterline_large_leaps);
  writer.key("early_answer_counterline_notes");
  writer.value(audit.early_answer_counterline_notes);
  writer.key("early_answer_counterline_same_pitch_run_max");
  writer.value(audit.early_answer_counterline_same_pitch_run_max);
  writer.key("resolve_region_notes");
  writer.value(audit.resolve_region_notes);
  writer.key("resolve_region_pitch_repair_notes");
  writer.value(audit.resolve_region_pitch_repair_notes);
  writer.key("coda_notes");
  writer.value(audit.coda_notes);
  writer.key("coda_bass_notes");
  writer.value(audit.coda_bass_notes);
  writer.key("coda_sixteenth_notes");
  writer.value(audit.coda_sixteenth_notes);
  writer.key("coda_pitch_repair_notes");
  writer.value(audit.coda_pitch_repair_notes);
  writer.key("coda_final_tonic_bass_notes");
  writer.value(audit.coda_final_tonic_bass_notes);
  writer.key("coda_subject_head_interval_count");
  writer.value(audit.coda_subject_head_interval_count);
  writer.key("coda_subject_head_match_count");
  writer.value(audit.coda_subject_head_match_count);
  writer.key("flexible_large_leap_count");
  writer.value(audit.flexible_large_leap_count);
  writer.key("flexible_remote_leap_count");
  writer.value(audit.flexible_remote_leap_count);
  writer.key("max_flexible_leap");
  writer.value(audit.max_flexible_leap);
  writer.key("max_pitch_repair_run");
  writer.value(audit.max_pitch_repair_run);
  writer.key("max_pitch_repair_run_bar");
  writer.value(audit.max_pitch_repair_run_bar);
  writer.key("max_pitch_repair_run_voice");
  writer.value(audit.max_pitch_repair_run_voice);
  writer.key("max_pitch_repair_run_source");
  writer.value(std::string(bachNoteSourceToString(audit.max_pitch_repair_run_source)));
  writer.key("opening_subject_note_count");
  writer.value(audit.opening_subject_note_count);
  writer.key("opening_sixth_note_pitch");
  writer.value(audit.opening_sixth_note_pitch);
  writer.key("opening_sixth_approach_interval");
  writer.value(audit.opening_sixth_approach_interval);
  writer.key("opening_sixth_resolution_interval");
  writer.value(audit.opening_sixth_resolution_interval);
  writer.key("opening_subject_large_leap_count");
  writer.value(audit.opening_subject_large_leap_count);
  writer.key("opening_subject_unresolved_leap_count");
  writer.value(audit.opening_subject_unresolved_leap_count);
  writer.key("opening_subject_max_leap");
  writer.value(audit.opening_subject_max_leap);
  writer.key("two_voice_sample_count");
  writer.value(audit.two_voice_sample_count);
  writer.key("thematic_two_voice_sample_count");
  writer.value(audit.thematic_two_voice_sample_count);
  writer.key("subject_dialogue_pair_sample_count");
  writer.value(audit.subject_dialogue_pair_sample_count);
  writer.key("thematic_dialogue_pair_sample_count");
  writer.value(audit.thematic_dialogue_pair_sample_count);
  writer.key("thematic_dialogue_hard_clash_count");
  writer.value(audit.thematic_dialogue_hard_clash_count);
  writer.key("early_thematic_dialogue_pair_sample_count");
  writer.value(audit.early_thematic_dialogue_pair_sample_count);
  writer.key("early_thematic_dialogue_hard_clash_count");
  writer.value(audit.early_thematic_dialogue_hard_clash_count);
  writer.key("episode_dialogue_window_count");
  writer.value(audit.episode_dialogue_window_count);
  writer.key("episode_dialogue_weak_window_count");
  writer.value(audit.episode_dialogue_weak_window_count);
  writer.key("subject_motif_interval_pair_count");
  writer.value(audit.subject_motif_interval_pair_count);
  writer.key("episode_motif_interval_pair_count");
  writer.value(audit.episode_motif_interval_pair_count);
  writer.key("episode_motif_derived_pair_count");
  writer.value(audit.episode_motif_derived_pair_count);
  writer.key("formal_section_count");
  writer.value(audit.formal_section_count);
  writer.key("establish_section_count");
  writer.value(audit.establish_section_count);
  writer.key("develop_section_count");
  writer.value(audit.develop_section_count);
  writer.key("resolve_section_count");
  writer.value(audit.resolve_section_count);
  writer.key("middle_entry_count");
  writer.value(audit.middle_entry_count);
  writer.key("stretto_count");
  writer.value(audit.stretto_count);
  writer.key("coda_section_count");
  writer.value(audit.coda_section_count);
  writer.key("formal_structure_ticks");
  writer.value(audit.formal_structure_ticks);
  writer.key("composed_intent_ratio");
  writer.value(audit.composed_intent_ratio);
  writer.key("repair_modified_ratio");
  writer.value(audit.repair_modified_ratio);
  writer.key("pitch_repair_modified_ratio");
  writer.value(audit.pitch_repair_modified_ratio);
  writer.key("repair_hotspot_modified_ratio");
  writer.value(audit.repair_hotspot_modified_ratio);
  writer.key("repair_hotspot_pitch_repair_ratio");
  writer.value(audit.repair_hotspot_pitch_repair_ratio);
  writer.key("episode_material_repair_hotspot_pitch_repair_ratio");
  writer.value(audit.episode_material_repair_hotspot_pitch_repair_ratio);
  writer.key("protected_dialogue_pitch_repair_ratio");
  writer.value(audit.protected_dialogue_pitch_repair_ratio);
  writer.key("immutable_pitch_repair_ratio");
  writer.value(audit.immutable_pitch_repair_ratio);
  writer.key("flexible_pitch_repair_ratio");
  writer.value(audit.flexible_pitch_repair_ratio);
  writer.key("episode_material_pitch_repair_ratio");
  writer.value(audit.episode_material_pitch_repair_ratio);
  writer.key("episode_material_pitch_repair_density");
  writer.value(audit.episode_material_pitch_repair_density);
  writer.key("bass_activity_ratio");
  writer.value(audit.bass_activity_ratio);
  writer.key("bass_step_or_structural_motion_ratio");
  writer.value(audit.bass_step_or_structural_motion_ratio);
  writer.key("listening_hotspot_pitch_repair_ratio");
  writer.value(audit.listening_hotspot_pitch_repair_ratio);
  writer.key("early_listening_hard_clash_ratio");
  writer.value(audit.early_listening_hard_clash_ratio);
  writer.key("early_listening_unstable_interval_ratio");
  writer.value(audit.early_listening_unstable_interval_ratio);
  writer.key("eight_second_dialogue_hard_clash_ratio");
  writer.value(audit.eight_second_dialogue_hard_clash_ratio);
  writer.key("eight_second_dialogue_unstable_interval_ratio");
  writer.value(audit.eight_second_dialogue_unstable_interval_ratio);
  writer.key("early_exposition_dialogue_ratio");
  writer.value(audit.early_exposition_dialogue_ratio);
  writer.key("early_exposition_free_counterpoint_ratio");
  writer.value(audit.early_exposition_free_counterpoint_ratio);
  writer.key("early_exposition_pitch_repair_ratio");
  writer.value(audit.early_exposition_pitch_repair_ratio);
  writer.key("early_exposition_free_counterpoint_pitch_repair_ratio");
  writer.value(audit.early_exposition_free_counterpoint_pitch_repair_ratio);
  writer.key("resolve_region_pitch_repair_ratio");
  writer.value(audit.resolve_region_pitch_repair_ratio);
  writer.key("coda_pitch_repair_ratio");
  writer.value(audit.coda_pitch_repair_ratio);
  writer.key("coda_subject_head_match_ratio");
  writer.value(audit.coda_subject_head_match_ratio);
  writer.key("thematic_two_voice_ratio");
  writer.value(audit.thematic_two_voice_ratio);
  writer.key("subject_dialogue_pair_ratio");
  writer.value(audit.subject_dialogue_pair_ratio);
  writer.key("thematic_dialogue_hard_clash_ratio");
  writer.value(audit.thematic_dialogue_hard_clash_ratio);
  writer.key("early_thematic_dialogue_hard_clash_ratio");
  writer.value(audit.early_thematic_dialogue_hard_clash_ratio);
  writer.key("min_episode_dialogue_window_ratio");
  writer.value(audit.min_episode_dialogue_window_ratio);
  writer.key("episode_motif_derivation_ratio");
  writer.value(audit.episode_motif_derivation_ratio);
  writer.key("listening_hotspots");
  writer.beginArray();
  for (const auto& hotspot : audit.listening_hotspots) {
    writer.beginObject();
    writer.key("bar");
    writer.value(hotspot.bar);
    writer.key("note_count");
    writer.value(hotspot.note_count);
    writer.key("dialogue_notes");
    writer.value(hotspot.dialogue_notes);
    writer.key("flexible_notes");
    writer.value(hotspot.flexible_notes);
    writer.key("pitch_repair_modified_notes");
    writer.value(hotspot.pitch_repair_modified_notes);
    writer.key("pitch_repair_ratio");
    writer.value(hotspot.pitch_repair_ratio);
    writer.key("pitch_repair_pass");
    writer.value(hotspot.pitch_repair_pass);
    writer.key("flexible_large_leap_count");
    writer.value(hotspot.flexible_large_leap_count);
    writer.key("max_flexible_leap");
    writer.value(hotspot.max_flexible_leap);
    writer.key("covered");
    writer.value(hotspot.covered);
    writer.key("active_voice_sample_count");
    writer.value(hotspot.active_voice_sample_count);
    writer.key("min_active_voices");
    writer.value(hotspot.min_active_voices);
    writer.key("max_active_voices");
    writer.value(hotspot.max_active_voices);
    writer.key("average_active_voices");
    writer.value(hotspot.average_active_voices);
    writer.key("texture_pass");
    writer.value(hotspot.texture_pass);
    writer.key("hard_clash_sample_count");
    writer.value(hotspot.hard_clash_sample_count);
    writer.key("hard_clash_count");
    writer.value(hotspot.hard_clash_count);
    writer.key("hard_clash_ratio");
    writer.value(hotspot.hard_clash_ratio);
    writer.key("hard_clash_pass");
    writer.value(hotspot.hard_clash_pass);
    writer.endObject();
  }
  writer.endArray();
  writer.endObject();
}

void writeAnalysisGate(JsonWriter& writer, const GeneratorResult& result,
                       const GeneratorConfig& config) {
  if (result.timeline.size() == 0)
    return;

  const HarmonicTimeline* generation_timeline =
      result.generation_timeline.size() > 0 ? &result.generation_timeline : nullptr;
  AnalysisReport report = runAnalysis(result.tracks, config.form, config.num_voices,
                                      result.timeline, config.key, generation_timeline);

  writer.key("analysis_gate");
  writer.beginObject();
  writer.key("selection_score");
  writer.value(report.selection_score);
  writer.key("selection_pass");
  writer.value(report.selection_pass);
  writer.key("analysis_gate_pass");
  writer.value(report.selection_pass && report.overall_pass);
  writer.key("melodic_structure_pass");
  writer.value(report.melodic_structure_pass);
  writer.key("flexible_large_leap_count");
  writer.value(report.flexible_large_leap_count);
  writer.key("flexible_remote_leap_count");
  writer.value(report.flexible_remote_leap_count);
  writer.key("max_flexible_leap");
  writer.value(report.max_flexible_leap);
  writer.key("penalty_affecting_violations");
  writer.value(report.penalty_affecting_violations);
  writer.key("penalty_affecting_density");
  writer.value(report.penalty_affecting_density);
  writer.key("overall_pass");
  writer.value(report.overall_pass);
  writer.endObject();
}

/// @brief Get fugue develop_pairs and episode_bars for a DurationScale.
/// @param scale The duration scale.
/// @param[out] pairs Output: number of Episode+MiddleEntry pairs.
/// @param[out] ep_bars Output: bars per episode.
void fugueDurationParams(DurationScale scale, int& pairs, int& ep_bars) {
  switch (scale) {
    case DurationScale::Short:
      pairs = 2;
      ep_bars = 2;
      break;  // 2 pairs for ~18 bars develop section
    case DurationScale::Medium:
      pairs = 3;
      ep_bars = 3;
      break;
    case DurationScale::Long:
      pairs = 5;
      ep_bars = 3;
      break;  // Reduced from 6 for quality stability
    case DurationScale::Full:
      pairs = 8;
      ep_bars = 4;
      break;  // Reduced from 10 for quality stability
  }
}

/// @brief Get chaconne target_variations for a DurationScale.
/// @param scale The duration scale.
/// @return Target number of variations.
int chaconneVariationsForScale(DurationScale scale) {
  switch (scale) {
    case DurationScale::Short:
      return 0;  // Use standard plan
    case DurationScale::Medium:
      return 24;
    case DurationScale::Long:
      return 40;
    case DurationScale::Full:
      return 64;
  }
  return 0;
}

/// @brief Get flow section count for a DurationScale.
/// @param scale The duration scale.
/// @return Number of sections.
int flowSectionsForScale(DurationScale scale) {
  switch (scale) {
    case DurationScale::Short:
      return 6;
    case DurationScale::Medium:
      return 10;
    case DurationScale::Long:
      return 16;
    case DurationScale::Full:
      return 20;
  }
  return 6;
}

/// @brief Map GeneratorConfig to FugueConfig for fugue-based forms.
/// @param config The unified generator configuration.
/// @return FugueConfig populated from the generator config.
FugueConfig toFugueConfig(const GeneratorConfig& config) {
  FugueConfig fconfig;
  fconfig.key = config.key.tonic;
  fconfig.is_minor = config.key.is_minor;
  fconfig.num_voices = config.num_voices;
  fconfig.bpm = config.bpm;
  fconfig.seed = config.seed;
  fconfig.character = config.character;

  if (config.target_bars > 0) {
    // Estimate: each pair adds ~episode_bars + subject_bars bars.
    // Baseline without pairs: ~10 bars (exposition + return episode + stretto + coda).
    int baseline = 10;
    int bars_left = static_cast<int>(config.target_bars) - baseline;
    int pair_cost = fconfig.episode_bars + fconfig.subject_bars;
    if (pair_cost < 2)
      pair_cost = 2;
    fconfig.develop_pairs = std::max(1, bars_left / pair_cost);
  } else {
    fugueDurationParams(config.scale, fconfig.develop_pairs, fconfig.episode_bars);
  }

  // Create modulation plan based on key mode (Principle 4: design values).
  if (config.key.is_minor) {
    fconfig.modulation_plan = ModulationPlan::createForMinor(config.key.tonic);
  } else {
    fconfig.modulation_plan = ModulationPlan::createForMajor(config.key.tonic);
  }
  fconfig.has_modulation_plan = true;

  return fconfig;
}

/// @brief Map GeneratorConfig to PreludeConfig for prelude generation.
/// @param config The unified generator configuration.
/// @param fugue_length_ticks Duration of the paired fugue (used to scale prelude length).
/// @return PreludeConfig populated from the generator config.
PreludeConfig toPreludeConfig(const GeneratorConfig& config, Tick fugue_length_ticks) {
  PreludeConfig pconfig;
  pconfig.key = config.key;
  pconfig.num_voices = config.num_voices;
  pconfig.bpm = config.bpm;
  pconfig.seed = config.seed + kPreludeSeedOffset;
  pconfig.fugue_length_ticks = fugue_length_ticks;
  return pconfig;
}

/// @brief Calculate the total duration of a set of tracks.
/// @param tracks The tracks to measure.
/// @return The maximum end tick across all notes in all tracks.
Tick calculateTotalDuration(const std::vector<Track>& tracks) {
  Tick max_end = 0;
  for (const auto& track : tracks) {
    for (const auto& note : track.notes) {
      Tick end_tick = note.start_tick + note.duration;
      if (end_tick > max_end) {
        max_end = end_tick;
      }
    }
  }
  return max_end;
}

/// @brief Offset all note start_ticks in the given tracks by a fixed amount.
/// @param tracks Tracks whose notes will be shifted forward in time.
/// @param offset_ticks Number of ticks to add to each note's start_tick.
void offsetTrackNotes(std::vector<Track>& tracks, Tick offset_ticks) {
  for (auto& track : tracks) {
    for (auto& note : track.notes) {
      note.start_tick += offset_ticks;
    }
  }
}

/// @brief Merge two sets of tracks, offsetting the second by a duration.
///
/// The second set of tracks (fugue/suffix) is offset in time and merged
/// into the first set (prelude/prefix) by track index. Tracks beyond
/// the first set's size inherit channel/program from the second set.
///
/// @param prefix_tracks The first form's tracks (modified in-place to receive merged result).
/// @param suffix_tracks The second form's tracks (notes already offset).
void mergeTracksInPlace(std::vector<Track>& prefix_tracks, std::vector<Track>& suffix_tracks) {
  size_t track_count = std::max(prefix_tracks.size(), suffix_tracks.size());
  prefix_tracks.resize(track_count);
  for (size_t idx = 0; idx < track_count; ++idx) {
    if (idx < suffix_tracks.size()) {
      auto& dest = prefix_tracks[idx].notes;
      auto& src = suffix_tracks[idx].notes;
      dest.insert(dest.end(), src.begin(), src.end());
      if (prefix_tracks[idx].name.empty()) {
        prefix_tracks[idx].name = suffix_tracks[idx].name;
        prefix_tracks[idx].channel = suffix_tracks[idx].channel;
        prefix_tracks[idx].program = suffix_tracks[idx].program;
      }
    }
  }
}

/// @brief Remove within-track overlaps after merging compound forms.
///
/// Sorts notes by start_tick, removes same-tick duplicates (keeping the longer
/// @brief Map a track index to a VoiceRole for articulation purposes.
///
/// For organ-system multi-voice forms the mapping follows exposition entry order:
///   0 -> Assert (subject voice)
///   1 -> Respond (answer voice)
///   2 -> Propel (free counterpoint)
///   3+ -> Ground (pedal / bass foundation)
///
/// For solo-string forms there is typically a single track; Assert is used
/// to give a moderate non-legato articulation.
///
/// @param track_index Zero-based index of the track.
/// @param is_organ True for organ-system forms, false for solo-string.
/// @return VoiceRole suitable for articulation rule lookup.
VoiceRole voiceRoleForTrack(size_t track_index, bool is_organ) {
  if (!is_organ) {
    return VoiceRole::Assert;
  }
  switch (track_index) {
    case 0:
      return VoiceRole::Assert;
    case 1:
      return VoiceRole::Respond;
    case 2:
      return VoiceRole::Propel;
    default:
      return VoiceRole::Ground;
  }
}

/// @brief Apply articulation to every track in a GeneratorResult.
///
/// This must be called as the final processing step before the result is
/// returned to the caller (and subsequently written to MIDI).  It adjusts
/// note durations via gate ratios and adds phrase breathing at cadence points.
///
/// @param result The generation result whose tracks will be modified in place.
/// @param instrument The instrument type (organ vs non-organ affects velocity).
void applyArticulationToResult(GeneratorResult& result, InstrumentType instrument) {
  if (!result.success) {
    return;
  }

  bool is_organ = (instrument == InstrumentType::Organ);
  const HarmonicTimeline* timeline_ptr = result.timeline.size() > 0 ? &result.timeline : nullptr;

  for (size_t idx = 0; idx < result.tracks.size(); ++idx) {
    VoiceRole role = voiceRoleForTrack(idx, is_organ);
    applyArticulation(result.tracks[idx].notes, role, timeline_ptr, is_organ);
  }
}

/// @brief Generate a fugue-only form (Fugue, ToccataAndFugue, etc.).
/// @param config Unified generator configuration.
/// @return GeneratorResult with fugue tracks.
GeneratorResult generateFugueForm(const GeneratorConfig& config) {
  GeneratorResult result;
  FugueConfig fconfig = toFugueConfig(config);

  FugueResult fugue_result = generateFugue(fconfig);

  if (!fugue_result.success) {
    result.success = false;
    result.error_message = fugue_result.error_message;
    return result;
  }

  result.tracks = std::move(fugue_result.tracks);
  result.total_duration_ticks = calculateTotalDuration(result.tracks);
  result.tempo_events = generateFugueTempoMap(fugue_result.structure, config.bpm);
  result.structure_json = fugue_result.structure.toJson();
  result.timeline = fugue_result.timeline.size() > 0
                        ? std::move(fugue_result.timeline)
                        : HarmonicTimeline::createStandard(config.key, result.total_duration_ticks,
                                                           HarmonicResolution::Bar);
  result.generation_timeline = std::move(fugue_result.generation_timeline);
  result.success = true;
  result.form_description = std::string(formTypeToString(config.form)) + " in " +
                            keySignatureToString(config.key) + ", " +
                            std::to_string(config.num_voices) + " voices, " +
                            subjectCharacterToString(config.character) + " character";

  return result;
}

/// @brief Generate a prelude-and-fugue form (prelude + fugue concatenated).
/// @param config Unified generator configuration.
/// @return GeneratorResult with prelude and fugue tracks merged.
GeneratorResult generatePreludeAndFugueForm(const GeneratorConfig& config) {
  GeneratorResult result;

  // Step 1: Generate the fugue first to know its length.
  FugueConfig fconfig = toFugueConfig(config);
  FugueResult fugue_result = generateFugue(fconfig);

  if (!fugue_result.success) {
    result.success = false;
    result.error_message = "Fugue generation failed: " + fugue_result.error_message;
    return result;
  }

  Tick fugue_duration = calculateTotalDuration(fugue_result.tracks);

  // Step 2: Generate the prelude, scaled to fugue length.
  PreludeConfig pconfig = toPreludeConfig(config, fugue_duration);
  PreludeResult prelude_result = generatePrelude(pconfig);

  if (!prelude_result.success) {
    result.success = false;
    result.error_message = "Prelude generation failed";
    return result;
  }

  Tick prelude_duration = prelude_result.total_duration_ticks;

  // Step 3: Offset all fugue notes by prelude duration.
  offsetTrackNotes(fugue_result.tracks, prelude_duration);

  // Step 4: Merge prelude and fugue tracks (same channel/voice mapping).
  // Both generators produce tracks indexed by voice, so we merge by index.
  size_t track_count = prelude_result.tracks.size();
  if (fugue_result.tracks.size() > track_count) {
    track_count = fugue_result.tracks.size();
  }

  result.tracks.resize(track_count);
  for (size_t idx = 0; idx < track_count; ++idx) {
    if (idx < prelude_result.tracks.size()) {
      result.tracks[idx] = std::move(prelude_result.tracks[idx]);
    }
    if (idx < fugue_result.tracks.size()) {
      // Append fugue notes to the track.
      auto& dest_notes = result.tracks[idx].notes;
      auto& src_notes = fugue_result.tracks[idx].notes;
      dest_notes.insert(dest_notes.end(), src_notes.begin(), src_notes.end());

      // If the prelude track was empty (fewer voices in prelude), copy metadata.
      if (result.tracks[idx].name.empty()) {
        result.tracks[idx].name = fugue_result.tracks[idx].name;
        result.tracks[idx].channel = fugue_result.tracks[idx].channel;
        result.tracks[idx].program = fugue_result.tracks[idx].program;
      }
    }
  }

  // Step 5: Build tempo events (prelude tempo at tick 0, fugue tempo map offset).
  result.tempo_events.push_back({0, config.bpm});
  auto fugue_tempo = generateFugueTempoMap(fugue_result.structure, config.bpm);
  for (auto& evt : fugue_tempo) {
    evt.tick += prelude_duration;
    result.tempo_events.push_back(evt);
  }

  result.total_duration_ticks = prelude_duration + fugue_duration;
  // Use the fugue's tonal plan timeline (offset by prelude duration) if available.
  if (fugue_result.timeline.size() > 0) {
    // Build a combined timeline: prelude uses standard, fugue uses tonal plan.
    HarmonicTimeline combined =
        HarmonicTimeline::createStandard(config.key, prelude_duration, HarmonicResolution::Bar);
    for (const auto& ev : fugue_result.timeline.events()) {
      HarmonicEvent offset_ev = ev;
      offset_ev.tick += prelude_duration;
      offset_ev.end_tick += prelude_duration;
      combined.addEvent(offset_ev);
    }
    result.timeline = std::move(combined);
  } else {
    result.timeline = HarmonicTimeline::createStandard(config.key, result.total_duration_ticks,
                                                       HarmonicResolution::Bar);
  }
  // Offset generation_timeline by prelude duration for dual-timeline analysis.
  if (fugue_result.generation_timeline.size() > 0) {
    HarmonicTimeline offset_gen_tl;
    for (const auto& ev : fugue_result.generation_timeline.events()) {
      HarmonicEvent offset_ev = ev;
      offset_ev.tick += prelude_duration;
      offset_ev.end_tick += prelude_duration;
      offset_gen_tl.addEvent(offset_ev);
    }
    result.generation_timeline = std::move(offset_gen_tl);
  }
  result.success = true;
  result.form_description = "Prelude and Fugue in " + keySignatureToString(config.key) + ", " +
                            std::to_string(config.num_voices) + " voices, " +
                            subjectCharacterToString(config.character) + " character";

  return result;
}

/// @brief Result of merging a preamble form (toccata/fantasia) with a fugue.
struct PreambleFugueResult {
  std::vector<Track> tracks;
  Tick total_duration_ticks;
  std::vector<TempoEvent> tempo_events;
  HarmonicTimeline timeline;
  HarmonicTimeline generation_timeline;
};

/// @brief Merge preamble tracks with fugue tracks into a single result.
///
/// Offsets fugue notes by preamble duration, merges tracks, combines
/// tempo maps, and offsets generation_timeline.
///
/// @param preamble_tracks Tracks from the preamble form (moved).
/// @param preamble_duration Duration of the preamble in ticks.
/// @param fugue_result Fugue generation result.
/// @param preamble_tempo Tempo events from the preamble form.
/// @param key_sig Key signature for timeline creation.
/// @param bpm BPM for fugue tempo map generation.
/// @return Combined result.
PreambleFugueResult mergePreambleWithFugue(std::vector<Track> preamble_tracks,
                                           Tick preamble_duration, FugueResult& fugue_result,
                                           std::vector<TempoEvent> preamble_tempo,
                                           KeySignature key_sig, uint16_t bpm) {
  PreambleFugueResult result;

  // Compute fugue duration BEFORE offset (avoid double-counting).
  Tick fugue_duration = calculateTotalDuration(fugue_result.tracks);

  // Offset fugue notes by preamble duration and merge tracks.
  offsetTrackNotes(fugue_result.tracks, preamble_duration);
  result.tracks = std::move(preamble_tracks);
  mergeTracksInPlace(result.tracks, fugue_result.tracks);

  result.total_duration_ticks = preamble_duration + fugue_duration;

  // Combine tempo maps: preamble tempo + offset fugue tempo.
  result.tempo_events = std::move(preamble_tempo);
  auto fugue_tempo = generateFugueTempoMap(fugue_result.structure, bpm);
  for (auto& evt : fugue_tempo) {
    evt.tick += preamble_duration;
    result.tempo_events.push_back(evt);
  }

  result.timeline = HarmonicTimeline::createStandard(key_sig, result.total_duration_ticks,
                                                     HarmonicResolution::Bar);

  // Offset generation_timeline by preamble duration for dual-timeline analysis.
  if (fugue_result.generation_timeline.size() > 0) {
    HarmonicTimeline offset_gen_tl;
    for (const auto& ev : fugue_result.generation_timeline.events()) {
      HarmonicEvent offset_ev = ev;
      offset_ev.tick += preamble_duration;
      offset_ev.end_tick += preamble_duration;
      offset_gen_tl.addEvent(offset_ev);
    }
    result.generation_timeline = std::move(offset_gen_tl);
  }

  return result;
}

}  // namespace

/// @brief Remove duplicate notes and truncate overlapping notes within each track.
///
/// Note: This cleanup includes pitch in the dedup key (unlike finalizeFormNotes
/// which deduplicates by voice+tick only). Retained as a final safety net for
/// the generator output pipeline, after articulation and instrument enforcement.
///
/// Sorts notes by (start_tick, voice, duration DESC), deduplicates same-tick +
/// same-voice + same-pitch notes (keeping the longest), and truncates notes
/// that extend past the next same-voice note's start.
///
/// Voice-aware: notes in different voices at the same tick are preserved
/// (e.g., ground bass voice=0 + texture voice=1 in chaconne).
///
/// @param tracks Tracks to clean up (modified in place).
void cleanupTrackOverlaps(std::vector<Track>& tracks) {
  for (auto& track : tracks) {
    auto& notes = track.notes;
    if (notes.size() < 2)
      continue;

    // Sort by start_tick, then voice, then duration descending.
    std::sort(notes.begin(), notes.end(), [](const NoteEvent& a, const NoteEvent& b) {
      if (a.start_tick != b.start_tick)
        return a.start_tick < b.start_tick;
      if (a.voice != b.voice)
        return a.voice < b.voice;
      return a.duration > b.duration;
    });

    // Remove same-tick + same-voice + same-pitch duplicates (keep longer).
    notes.erase(std::unique(notes.begin(), notes.end(),
                            [](const NoteEvent& a, const NoteEvent& b) {
                              return a.start_tick == b.start_tick && a.voice == b.voice &&
                                     a.pitch == b.pitch;
                            }),
                notes.end());

    // Truncate overlapping notes within the same voice.
    // Per-voice tracking with fixed array (VoiceId is uint8_t, solo string uses 0-1).
    constexpr size_t kMaxCleanupVoices = 8;
    std::array<size_t, kMaxCleanupVoices> prev_index{};
    std::array<bool, kMaxCleanupVoices> has_prev{};
    has_prev.fill(false);

    for (size_t i = 0; i < notes.size(); ++i) {
      VoiceId v = notes[i].voice;
      if (v >= kMaxCleanupVoices)
        continue;
      if (has_prev[v]) {
        size_t prev_i = prev_index[v];
        // Skip same-tick chord tones (simultaneous notes in the same voice).
        if (notes[prev_i].start_tick != notes[i].start_tick) {
          Tick end_tick = notes[prev_i].start_tick + notes[prev_i].duration;
          if (end_tick > notes[i].start_tick) {
            notes[prev_i].duration = notes[i].start_tick - notes[prev_i].start_tick;
            notes[prev_i].modified_by |= static_cast<uint8_t>(NoteModifiedBy::OverlapTrim);
            if (notes[prev_i].duration == 0)
              notes[prev_i].duration = 1;
          }
        }
      }
      prev_index[v] = i;
      has_prev[v] = true;
    }

    // Cross-voice overlap cleanup (final safety net after articulation).
    // Resolves overlaps between different voices sharing a single MIDI track
    // (e.g., chaconne bass voice=0 and texture voice=1). First stagger any
    // remaining same-tick notes by 1 tick each, then truncate all overlaps.
    // This runs last, after articulation, so durations are final.
    std::sort(notes.begin(), notes.end(), [](const NoteEvent& lhs, const NoteEvent& rhs) {
      if (lhs.start_tick != rhs.start_tick)
        return lhs.start_tick < rhs.start_tick;
      return lhs.pitch < rhs.pitch;
    });

    // Stagger same-tick notes by 1 tick each (post-articulation, so no minimum
    // duration clamp will inflate them back).
    for (size_t idx = 0; idx + 1 < notes.size(); ++idx) {
      if (notes[idx].start_tick == notes[idx + 1].start_tick) {
        notes[idx + 1].start_tick += 1;
        if (notes[idx + 1].duration > 1) {
          notes[idx + 1].duration -= 1;
        }
      }
    }

    // Re-sort after stagger adjustments.
    std::sort(notes.begin(), notes.end(), [](const NoteEvent& lhs, const NoteEvent& rhs) {
      if (lhs.start_tick != rhs.start_tick)
        return lhs.start_tick < rhs.start_tick;
      return lhs.pitch < rhs.pitch;
    });

    // Truncate all remaining overlaps.
    for (size_t idx = 0; idx + 1 < notes.size(); ++idx) {
      Tick end_tick = notes[idx].start_tick + notes[idx].duration;
      if (end_tick > notes[idx + 1].start_tick) {
        Tick new_dur = notes[idx + 1].start_tick - notes[idx].start_tick;
        if (new_dur == 0)
          new_dur = 1;
        notes[idx].duration = new_dur;
        notes[idx].modified_by |= static_cast<uint8_t>(NoteModifiedBy::OverlapTrim);
      }
    }
  }
}

GeneratorResult generate(const GeneratorConfig& config) {
  GeneratorResult result;

  // Auto-select seed if 0.
  GeneratorConfig effective_config = config;
  if (effective_config.seed == 0) {
    effective_config.seed = rng::generateRandomSeed();
  }
  result.seed_used = effective_config.seed;

  switch (effective_config.form) {
    case FormType::Fugue: {
      result = generateFugueForm(effective_config);
      result.seed_used = effective_config.seed;
      break;
    }

    case FormType::ToccataAndFugue: {
      // Generate toccata free section, then append a fugue.
      ToccataConfig tconfig;
      if (effective_config.toccata_archetype_auto) {
        // Auto-select archetype from seed for structural variety.
        constexpr ToccataArchetype kArchetypes[] = {
            ToccataArchetype::Dramaticus,
            ToccataArchetype::Perpetuus,
            ToccataArchetype::Concertato,
            ToccataArchetype::Sectionalis,
        };
        tconfig.archetype = kArchetypes[effective_config.seed % 4];
      } else {
        tconfig.archetype = effective_config.toccata_archetype;
      }
      tconfig.key = effective_config.key;
      tconfig.bpm = effective_config.bpm;
      tconfig.seed = effective_config.seed;
      tconfig.num_voices = effective_config.num_voices;
      tconfig.total_bars = 24;

      ToccataResult toc_result = generateToccata(tconfig);
      if (!toc_result.success) {
        result.success = false;
        result.seed_used = effective_config.seed;
        result.error_message = "Toccata generation failed: " + toc_result.error_message;
        break;
      }

      Tick toc_duration = toc_result.total_duration_ticks;

      // Generate the fugue section, subtracting toccata bars from target.
      GeneratorConfig fugue_gen_config = effective_config;
      if (fugue_gen_config.target_bars > 0) {
        uint32_t toc_bars = tconfig.total_bars;
        fugue_gen_config.target_bars = (fugue_gen_config.target_bars > toc_bars)
                                           ? fugue_gen_config.target_bars - toc_bars
                                           : 12;  // minimum fugue baseline
      }
      FugueConfig fconfig = toFugueConfig(fugue_gen_config);
      fconfig.toccata_core_intervals = toc_result.core_intervals;
      FugueResult fugue_result = generateFugue(fconfig);
      if (!fugue_result.success) {
        result.success = false;
        result.seed_used = effective_config.seed;
        result.error_message = "Fugue generation failed: " + fugue_result.error_message;
        break;
      }

      auto preamble_tempo =
          generateToccataTempoMap(toc_result.archetype, toc_result.sections, effective_config.bpm);
      auto merged = mergePreambleWithFugue(std::move(toc_result.tracks), toc_duration, fugue_result,
                                           std::move(preamble_tempo), effective_config.key,
                                           effective_config.bpm);
      result.tracks = std::move(merged.tracks);
      result.total_duration_ticks = merged.total_duration_ticks;
      result.tempo_events = std::move(merged.tempo_events);
      result.timeline = std::move(merged.timeline);
      result.generation_timeline = std::move(merged.generation_timeline);
      result.success = true;
      result.seed_used = effective_config.seed;
      result.form_description = "Toccata and Fugue in " +
                                keySignatureToString(effective_config.key) + " (" +
                                toccataArchetypeToString(tconfig.archetype) + "), " +
                                std::to_string(effective_config.num_voices) + " voices";
      break;
    }

    case FormType::FantasiaAndFugue: {
      // Generate fantasia free section, then append a fugue.
      FantasiaConfig fant_config;
      fant_config.key = effective_config.key;
      fant_config.bpm = effective_config.bpm;
      fant_config.seed = effective_config.seed;
      fant_config.num_voices = effective_config.num_voices;
      fant_config.section_bars = 32;

      FantasiaResult fant_result = generateFantasia(fant_config);
      if (!fant_result.success) {
        result.success = false;
        result.seed_used = effective_config.seed;
        result.error_message = "Fantasia generation failed: " + fant_result.error_message;
        break;
      }

      Tick fant_duration = fant_result.total_duration_ticks;

      // Generate the fugue section, subtracting fantasia bars from target.
      GeneratorConfig fugue_gen_config = effective_config;
      if (fugue_gen_config.target_bars > 0) {
        uint32_t fant_bars = fant_config.section_bars;
        fugue_gen_config.target_bars = (fugue_gen_config.target_bars > fant_bars)
                                           ? fugue_gen_config.target_bars - fant_bars
                                           : 12;  // minimum fugue baseline
      }
      FugueConfig fconfig = toFugueConfig(fugue_gen_config);
      FugueResult fugue_result = generateFugue(fconfig);
      if (!fugue_result.success) {
        result.success = false;
        result.seed_used = effective_config.seed;
        result.error_message = "Fugue generation failed: " + fugue_result.error_message;
        break;
      }

      auto preamble_tempo =
          generateFantasiaTempoMap(fant_duration, fant_config.section_bars, effective_config.bpm);
      auto merged = mergePreambleWithFugue(std::move(fant_result.tracks), fant_duration,
                                           fugue_result, std::move(preamble_tempo),
                                           effective_config.key, effective_config.bpm);
      result.tracks = std::move(merged.tracks);
      result.total_duration_ticks = merged.total_duration_ticks;
      result.tempo_events = std::move(merged.tempo_events);
      result.timeline = std::move(merged.timeline);
      result.generation_timeline = std::move(merged.generation_timeline);
      result.success = true;
      result.seed_used = effective_config.seed;
      result.form_description = "Fantasia and Fugue in " +
                                keySignatureToString(effective_config.key) + ", " +
                                std::to_string(effective_config.num_voices) + " voices";
      break;
    }

    case FormType::Passacaglia: {
      PassacagliaConfig pconfig;
      pconfig.key = effective_config.key;
      pconfig.bpm = effective_config.bpm;
      pconfig.seed = effective_config.seed;
      // Passacaglia requires at least 4 voices (3 manuals + pedal).
      pconfig.num_voices = std::max(effective_config.num_voices, static_cast<uint8_t>(4));

      PassacagliaResult pass_result = generatePassacaglia(pconfig);
      if (!pass_result.success) {
        result.success = false;
        result.seed_used = effective_config.seed;
        result.error_message = "Passacaglia generation failed: " + pass_result.error_message;
        break;
      }

      result.tracks = std::move(pass_result.tracks);
      result.total_duration_ticks = pass_result.total_duration_ticks;
      result.tempo_events = generatePassacagliaTempoMap(
          pconfig.num_variations, pconfig.ground_bass_bars, effective_config.bpm);
      result.timeline =
          pass_result.timeline.size() > 0
              ? std::move(pass_result.timeline)
              : HarmonicTimeline::createStandard(effective_config.key, result.total_duration_ticks,
                                                 HarmonicResolution::Bar);
      result.success = true;
      result.seed_used = effective_config.seed;
      result.form_description = "Passacaglia in " + keySignatureToString(effective_config.key);
      break;
    }

    case FormType::PreludeAndFugue: {
      result = generatePreludeAndFugueForm(effective_config);
      result.seed_used = effective_config.seed;
      break;
    }

    case FormType::TrioSonata: {
      TrioSonataConfig ts_config;
      ts_config.key = effective_config.key;
      ts_config.seed = effective_config.seed;
      // Fast movements use the user BPM; slow movement uses ~55% (Baroque practice).
      ts_config.bpm_fast = effective_config.bpm;
      ts_config.bpm_slow = std::max(static_cast<uint16_t>(40),
                                    static_cast<uint16_t>(effective_config.bpm * 55 / 100));

      TrioSonataResult ts_result = generateTrioSonata(ts_config);
      if (!ts_result.success || ts_result.movements.empty()) {
        result.success = false;
        result.seed_used = effective_config.seed;
        result.error_message = "Trio Sonata generation failed";
        break;
      }

      // Concatenate 3 movements sequentially: offset and merge tracks.
      Tick accumulated_ticks = 0;
      result.tracks = std::move(ts_result.movements[0].tracks);
      result.tempo_events.push_back({0, ts_result.movements[0].bpm});
      accumulated_ticks = ts_result.movements[0].total_duration_ticks;

      for (size_t mov = 1; mov < ts_result.movements.size(); ++mov) {
        auto& movement = ts_result.movements[mov];
        result.tempo_events.push_back({accumulated_ticks, movement.bpm});
        offsetTrackNotes(movement.tracks, accumulated_ticks);
        mergeTracksInPlace(result.tracks, movement.tracks);
        accumulated_ticks += movement.total_duration_ticks;
      }

      result.total_duration_ticks = accumulated_ticks;
      result.timeline = HarmonicTimeline::createStandard(
          effective_config.key, result.total_duration_ticks, HarmonicResolution::Bar);
      result.success = true;
      result.seed_used = effective_config.seed;
      result.form_description = "Trio Sonata in " + keySignatureToString(effective_config.key);
      break;
    }

    case FormType::ChoralePrelude: {
      // Validate character-form compatibility before generation.
      if (!isCharacterFormCompatible(effective_config.character, FormType::ChoralePrelude)) {
        result.success = false;
        result.seed_used = effective_config.seed;
        result.error_message = "Incompatible character for ChoralePrelude: " +
                               std::string(subjectCharacterToString(effective_config.character));
        break;
      }

      ChoralePreludeConfig cpconfig;
      cpconfig.key = effective_config.key;
      cpconfig.bpm = effective_config.bpm;
      cpconfig.seed = effective_config.seed;

      ChoralePreludeResult cp_result = generateChoralePrelude(cpconfig);

      if (!cp_result.success) {
        result.success = false;
        result.seed_used = effective_config.seed;
        result.error_message = "ChoralePrelude generation failed";
        break;
      }

      result.tracks = std::move(cp_result.tracks);
      result.total_duration_ticks = cp_result.total_duration_ticks;
      result.tempo_events.push_back({0, effective_config.bpm});
      result.timeline =
          cp_result.timeline.size() > 0
              ? std::move(cp_result.timeline)
              : HarmonicTimeline::createStandard(effective_config.key, result.total_duration_ticks,
                                                 HarmonicResolution::Bar);
      result.success = true;
      result.seed_used = effective_config.seed;
      result.form_description = "Chorale Prelude in " + keySignatureToString(effective_config.key);
      break;
    }

    case FormType::CelloPrelude: {
      ArpeggioFlowConfig flow_config;
      flow_config.key = effective_config.key;
      flow_config.bpm = effective_config.bpm;
      flow_config.seed = effective_config.seed;
      flow_config.instrument = effective_config.instrument;

      if (effective_config.target_bars > 0) {
        flow_config.num_sections = std::max(
            3, static_cast<int>(effective_config.target_bars / flow_config.bars_per_section));
      } else {
        flow_config.num_sections = flowSectionsForScale(effective_config.scale);
      }

      ArpeggioFlowResult flow_result = generateArpeggioFlow(flow_config);

      if (!flow_result.success) {
        result.success = false;
        result.seed_used = effective_config.seed;
        result.error_message = "CelloPrelude generation failed: " + flow_result.error_message;
        break;
      }

      result.tracks = std::move(flow_result.tracks);
      result.total_duration_ticks = flow_result.total_duration_ticks;
      result.tempo_events.push_back({0, effective_config.bpm});
      result.timeline =
          flow_result.timeline.size() > 0
              ? std::move(flow_result.timeline)
              : HarmonicTimeline::createStandard(effective_config.key, result.total_duration_ticks,
                                                 HarmonicResolution::Bar);
      result.success = true;
      result.seed_used = effective_config.seed;
      result.form_description = "Cello Prelude in " + keySignatureToString(effective_config.key);
      break;
    }

    case FormType::Chaconne: {
      ChaconneConfig ch_config;
      ch_config.key = effective_config.key;
      ch_config.bpm = effective_config.bpm;
      ch_config.seed = effective_config.seed;
      ch_config.instrument = effective_config.instrument;

      if (effective_config.target_bars > 0) {
        // Each variation = 4 bars (ground bass cycle).
        ch_config.target_variations =
            std::max(10, static_cast<int>(effective_config.target_bars / 4));
      } else {
        ch_config.target_variations = chaconneVariationsForScale(effective_config.scale);
      }

      ChaconneResult ch_result = generateChaconne(ch_config);

      if (!ch_result.success) {
        result.success = false;
        result.seed_used = effective_config.seed;
        result.error_message = ch_result.error_message;
        break;
      }

      result.tracks = std::move(ch_result.tracks);
      result.total_duration_ticks = ch_result.total_duration_ticks;
      result.tempo_events.push_back({0, effective_config.bpm});
      result.timeline =
          ch_result.timeline.size() > 0
              ? std::move(ch_result.timeline)
              : HarmonicTimeline::createStandard(effective_config.key, result.total_duration_ticks,
                                                 HarmonicResolution::Bar);
      result.success = true;
      result.seed_used = ch_result.seed_used;
      result.form_description = "Chaconne in " + keySignatureToString(effective_config.key);
      break;
    }

    case FormType::GoldbergVariations: {
      GoldbergConfig gconfig;
      gconfig.key = effective_config.key;
      gconfig.bpm = effective_config.bpm;
      gconfig.seed = effective_config.seed;
      gconfig.instrument = effective_config.instrument;
      gconfig.scale = effective_config.scale;

      GoldbergResult gold_result = generateGoldbergVariations(gconfig);

      if (!gold_result.success) {
        result.success = false;
        result.seed_used = effective_config.seed;
        result.error_message = gold_result.error_message;
        break;
      }

      result.tracks = std::move(gold_result.tracks);
      result.total_duration_ticks = gold_result.total_duration_ticks;
      result.tempo_events = std::move(gold_result.tempo_events);
      result.timeline =
          gold_result.timeline.size() > 0
              ? std::move(gold_result.timeline)
              : HarmonicTimeline::createStandard(effective_config.key, result.total_duration_ticks,
                                                 HarmonicResolution::Bar);
      result.success = true;
      result.seed_used = gold_result.seed_used;
      result.form_description =
          "Goldberg Variations in " + keySignatureToString(effective_config.key);
      break;
    }
  }

  // Apply articulation as the final processing step before returning.
  // This adjusts note durations (gate ratio) and adds phrase breathing at cadences.
  // Skipped automatically for failed results (applyArticulationToResult checks success).
  applyArticulationToResult(result, effective_config.instrument);

  // Enforce physical impossibility constraints for the target instrument.
  if (result.success) {
    auto guard = createGuard(effective_config.instrument);
    enforceImpossibilityGuard(result.tracks, guard);
  }

  // Apply velocity curves for non-organ instruments.
  if (result.success && effective_config.instrument != InstrumentType::Organ) {
    std::vector<Tick> cadence_ticks;
    // Extract cadence ticks from timeline if available.
    if (result.timeline.size() > 0) {
      const auto& events = result.timeline.events();
      for (size_t idx = 1; idx < events.size(); ++idx) {
        if (events[idx].chord.degree == ChordDegree::I &&
            events[idx - 1].chord.degree == ChordDegree::V) {
          cadence_ticks.push_back(events[idx].tick);
        }
      }
    }
    for (auto& track : result.tracks) {
      applyVelocityCurve(track.notes, effective_config.instrument, cadence_ticks);
    }
  }

  // Overlap assertion: constraint-driven generation should produce clean output.
  // If this fires, there's a generation bug to fix, not a post-processing gap.
#ifndef NDEBUG
  if (result.success) {
    for (const auto& track : result.tracks) {
      for (size_t i = 1; i < track.notes.size(); ++i) {
        if (track.notes[i - 1].start_tick + track.notes[i - 1].duration >
            track.notes[i].start_tick) {
          std::fprintf(stderr, "[generator] WARNING: overlap in voice %u at tick %u\n",
                       track.notes[i].voice, track.notes[i].start_tick);
        }
      }
    }
  }
#endif

  // Pipeline exit: warn if any notes still have Unknown source.
  if (result.success) {
    for (const auto& track : result.tracks) {
      int unknown_count = countUnknownSource(track.notes);
      if (unknown_count > 0) {
        std::fprintf(stderr, "[%s] WARNING: %d notes with Unknown source\n",
                     formTypeToString(effective_config.form), unknown_count);
      }
    }
  }

  return result;
}

InstrumentType defaultInstrumentForForm(FormType form) {
  switch (form) {
    case FormType::Fugue:
    case FormType::PreludeAndFugue:
    case FormType::TrioSonata:
    case FormType::ChoralePrelude:
    case FormType::ToccataAndFugue:
    case FormType::Passacaglia:
    case FormType::FantasiaAndFugue:
      return InstrumentType::Organ;

    case FormType::CelloPrelude:
      return InstrumentType::Cello;

    case FormType::Chaconne:
      return InstrumentType::Violin;

    case FormType::GoldbergVariations:
      return InstrumentType::Harpsichord;
  }

  return InstrumentType::Organ;
}

InstrumentType instrumentTypeFromString(const std::string& str) {
  if (str == "organ")
    return InstrumentType::Organ;
  if (str == "harpsichord")
    return InstrumentType::Harpsichord;
  if (str == "piano")
    return InstrumentType::Piano;
  if (str == "violin")
    return InstrumentType::Violin;
  if (str == "cello")
    return InstrumentType::Cello;
  if (str == "guitar")
    return InstrumentType::Guitar;
  return InstrumentType::Organ;
}

std::string buildEventsJson(const GeneratorResult& result, const GeneratorConfig& config) {
  JsonWriter writer;
  writer.beginObject();

  writer.key("form");
  writer.value(std::string(formTypeToString(config.form)));
  writer.key("key");
  writer.value(keySignatureToString(config.key));
  writer.key("bpm");
  writer.value(static_cast<int>(config.bpm));
  writer.key("seed");
  writer.value(result.seed_used);
  writer.key("total_ticks");
  writer.value(result.total_duration_ticks);
  writer.key("total_bars");
  writer.value(static_cast<int>((result.total_duration_ticks + kTicksPerBar - 1) / kTicksPerBar));
  writer.key("description");
  writer.value(result.form_description);
  if (!result.structure_json.empty()) {
    writer.key("structure");
    writer.valueRaw(result.structure_json);
  }
  writeSourceSummary(writer, result);
  writeStructureAudit(writer, computeStructureAudit(result, config));
  writeAnalysisGate(writer, result, config);

  writer.key("tracks");
  writer.beginArray();
  for (const auto& track : result.tracks) {
    writer.beginObject();
    writer.key("name");
    writer.value(track.name);
    writer.key("channel");
    writer.value(static_cast<int>(track.channel));
    writer.key("program");
    writer.value(static_cast<int>(track.program));
    writer.key("note_count");
    writer.value(static_cast<int>(track.notes.size()));

    writer.key("notes");
    writer.beginArray();
    for (const auto& note : track.notes) {
      writer.beginObject();
      writer.key("pitch");
      writer.value(static_cast<int>(note.pitch));
      writer.key("velocity");
      writer.value(static_cast<int>(note.velocity));
      writer.key("start_tick");
      writer.value(note.start_tick);
      writer.key("duration");
      writer.value(note.duration);
      writer.key("voice");
      writer.value(static_cast<int>(note.voice));
      writer.key("source");
      writer.value(std::string(bachNoteSourceToString(note.source)));
      if (note.modified_by != 0) {
        writer.key("modified_by");
        writer.value(noteModifiedByToString(note.modified_by));
      }
      if (note.gesture_id != 0) {
        writer.key("gesture_id");
        writer.value(static_cast<int>(note.gesture_id));
        writer.key("gesture_role");
        const char* role_str = "none";
        switch (note.gesture_role) {
          case GestureRole::Leader:
            role_str = "leader";
            break;
          case GestureRole::OctaveEcho:
            role_str = "octave_echo";
            break;
          case GestureRole::LowerEcho:
            role_str = "lower_echo";
            break;
          case GestureRole::PedalHit:
            role_str = "pedal_hit";
            break;
          case GestureRole::Accumulation:
            role_str = "accumulation";
            break;
          default:
            break;
        }
        writer.value(std::string(role_str));
      }
      writer.endObject();
    }
    writer.endArray();

    writer.endObject();
  }
  writer.endArray();

  writer.endObject();
  return writer.toString();
}

}  // namespace bach
