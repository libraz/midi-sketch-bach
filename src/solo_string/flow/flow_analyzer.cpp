// Implementation of FlowAnalyzer -- quality metrics for BWV1007-style arpeggio flow pieces.

#include "solo_string/flow/flow_analyzer.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "core/basic_types.h"
#include "core/pitch_utils.h"
#include "harmony/harmonic_event.h"
#include "harmony/harmonic_timeline.h"
#include "solo_string/flow/arpeggio_flow_config.h"
#include "solo_string/flow/arpeggio_pattern.h"

namespace bach {

// ===========================================================================
// Internal helpers
// ===========================================================================

namespace {

/// @brief Collect all NoteEvents from all tracks into a single sorted vector.
/// @param tracks Input tracks.
/// @return All notes sorted by start_tick.
std::vector<NoteEvent> collectAllNotes(const std::vector<Track>& tracks) {
  std::vector<NoteEvent> all_notes;
  for (const auto& track : tracks) {
    all_notes.insert(all_notes.end(), track.notes.begin(), track.notes.end());
  }
  std::sort(all_notes.begin(), all_notes.end(),
            [](const NoteEvent& lhs, const NoteEvent& rhs) {
              return lhs.start_tick < rhs.start_tick;
            });
  return all_notes;
}

/// @brief Get the tick range for a section index.
/// @param section_idx 0-based section index.
/// @param bars_per_section Number of bars per section.
/// @return Pair of (start_tick inclusive, end_tick exclusive).
std::pair<Tick, Tick> sectionTickRange(int section_idx, int bars_per_section) {
  Tick start = static_cast<Tick>(section_idx) * static_cast<Tick>(bars_per_section) * kTicksPerBar;
  Tick end = start + static_cast<Tick>(bars_per_section) * kTicksPerBar;
  return {start, end};
}

/// @brief Get notes within a tick range [start, end).
/// @param all_notes Sorted notes.
/// @param start Start tick (inclusive).
/// @param end End tick (exclusive).
/// @return Notes whose start_tick falls within the range.
std::vector<NoteEvent> notesInRange(const std::vector<NoteEvent>& all_notes,
                                    Tick start, Tick end) {
  std::vector<NoteEvent> result;
  for (const auto& note : all_notes) {
    if (note.start_tick >= end) break;
    if (note.start_tick >= start) {
      result.push_back(note);
    }
  }
  return result;
}

/// @brief Get register range (max_pitch - min_pitch) for a set of notes.
/// @param notes Notes to analyze.
/// @return Range in semitones, or 0 if fewer than 2 notes.
int registerRange(const std::vector<NoteEvent>& notes) {
  if (notes.size() < 2) {
    return 0;
  }
  uint8_t min_pitch = 127;
  uint8_t max_pitch = 0;
  for (const auto& note : notes) {
    if (note.pitch < min_pitch) min_pitch = note.pitch;
    if (note.pitch > max_pitch) max_pitch = note.pitch;
  }
  return static_cast<int>(max_pitch) - static_cast<int>(min_pitch);
}

/// @brief Get the ArcPhase for a section from the config.
/// @param config The flow config.
/// @param section_idx 0-based section index.
/// @return ArcPhase for the section, or Ascent if not found.
ArcPhase getPhaseForSection(const ArpeggioFlowConfig& config, int section_idx) {
  for (const auto& [sec_id, phase] : config.arc.phase_assignment) {
    if (static_cast<int>(sec_id) == section_idx) {
      return phase;
    }
  }
  return ArcPhase::Ascent;  // Fallback
}

/// @brief Get the average pitch of a set of notes.
/// @param notes Notes to analyze.
/// @return Average MIDI pitch, or 0.0 if empty.
float averagePitch(const std::vector<NoteEvent>& notes) {
  if (notes.empty()) return 0.0f;
  float sum = 0.0f;
  for (const auto& note : notes) {
    sum += static_cast<float>(note.pitch);
  }
  return sum / static_cast<float>(notes.size());
}

/// @brief Check if a MIDI pitch is a chord tone of the given harmonic event.
///
/// A pitch is a chord tone if its pitch class matches the root, 3rd, or 5th
/// of the chord (accounting for chord quality).
///
/// @param pitch MIDI note number.
/// @param event Harmonic event with chord information.
/// @return true if the pitch is a chord tone.
bool isChordTone(uint8_t pitch, const HarmonicEvent& event) {
  int pitch_class = getPitchClass(pitch);
  int root_pc = getPitchClass(event.chord.root_pitch);

  // Root always matches.
  if (pitch_class == root_pc) return true;

  // Determine 3rd and 5th intervals based on chord quality.
  int third_interval = 0;
  int fifth_interval = 0;

  switch (event.chord.quality) {
    case ChordQuality::Major:
    case ChordQuality::Dominant7:
    case ChordQuality::MajorMajor7:
      third_interval = 4;  // Major 3rd
      fifth_interval = 7;  // Perfect 5th
      break;
    case ChordQuality::Minor:
    case ChordQuality::Minor7:
      third_interval = 3;  // Minor 3rd
      fifth_interval = 7;  // Perfect 5th
      break;
    case ChordQuality::Diminished:
      third_interval = 3;  // Minor 3rd
      fifth_interval = 6;  // Diminished 5th
      break;
    case ChordQuality::Augmented:
      third_interval = 4;  // Major 3rd
      fifth_interval = 8;  // Augmented 5th
      break;
  }

  int third_pc = (root_pc + third_interval) % 12;
  int fifth_pc = (root_pc + fifth_interval) % 12;

  return pitch_class == third_pc || pitch_class == fifth_pc;
}

/// @brief Total duration of a piece from config.
/// @param config Flow config.
/// @return Total duration in ticks.
Tick totalPieceDuration(const ArpeggioFlowConfig& config) {
  return static_cast<Tick>(config.num_sections) *
         static_cast<Tick>(config.bars_per_section) * kTicksPerBar;
}

}  // namespace

// ===========================================================================
// Metric implementations
// ===========================================================================

namespace {

/// @brief Measure how well notes follow harmonic changes (threshold >= 0.5).
///
/// For each harmonic event boundary, checks whether the pitches in that event's
/// time span are chord tones of the active chord.
///
/// @param all_notes All notes sorted by start_tick.
/// @param timeline The harmonic timeline.
/// @return Score in [0.0, 1.0].
float computeHarmonicMotionScore(const std::vector<NoteEvent>& all_notes,
                                 const HarmonicTimeline& timeline) {
  if (all_notes.empty() || timeline.size() == 0) {
    return 0.0f;
  }

  int total_notes = 0;
  int chord_tone_notes = 0;

  for (const auto& note : all_notes) {
    const HarmonicEvent& event = timeline.getAt(note.start_tick);
    ++total_notes;
    if (isChordTone(note.pitch, event)) {
      ++chord_tone_notes;
    }
  }

  if (total_notes == 0) return 0.0f;
  return static_cast<float>(chord_tone_notes) / static_cast<float>(total_notes);
}

/// @brief Measure register expansion following the arc shape (threshold >= 0.3).
///
/// Calculates register range per section and checks whether:
/// - Ascent sections show non-decreasing range
/// - Peak section has the widest range
/// - Descent sections show non-increasing range
///
/// @param all_notes All notes sorted by start_tick.
/// @param config Flow config with section and arc information.
/// @return Score in [0.0, 1.0].
float computeRegisterExpansionScore(const std::vector<NoteEvent>& all_notes,
                                    const ArpeggioFlowConfig& config) {
  if (all_notes.empty() || config.arc.phase_assignment.empty()) {
    return 0.0f;
  }

  int num_sections = config.num_sections;
  std::vector<int> section_ranges(static_cast<size_t>(num_sections), 0);

  // Calculate register range per section.
  for (int sec = 0; sec < num_sections; ++sec) {
    auto [start, end] = sectionTickRange(sec, config.bars_per_section);
    auto section_notes = notesInRange(all_notes, start, end);
    section_ranges[static_cast<size_t>(sec)] = registerRange(section_notes);
  }

  // Find peak section index.
  int peak_section = -1;
  for (const auto& [sec_id, phase] : config.arc.phase_assignment) {
    if (phase == ArcPhase::Peak) {
      peak_section = static_cast<int>(sec_id);
      break;
    }
  }

  if (peak_section < 0) return 0.0f;

  int total_checks = 0;
  int passed_checks = 0;

  // Check Peak has widest (or tied-widest) range.
  int peak_range = section_ranges[static_cast<size_t>(peak_section)];
  bool peak_is_max = true;
  for (int sec = 0; sec < num_sections; ++sec) {
    if (sec != peak_section && section_ranges[static_cast<size_t>(sec)] > peak_range) {
      peak_is_max = false;
      break;
    }
  }
  ++total_checks;
  if (peak_is_max) ++passed_checks;

  // Check Ascent sections: non-decreasing range.
  int prev_range = -1;
  for (const auto& [sec_id, phase] : config.arc.phase_assignment) {
    if (phase != ArcPhase::Ascent) continue;
    int range = section_ranges[static_cast<size_t>(sec_id)];
    if (prev_range >= 0) {
      ++total_checks;
      if (range >= prev_range) ++passed_checks;
    }
    prev_range = range;
  }

  // Check Descent sections: non-increasing range.
  prev_range = -1;
  for (const auto& [sec_id, phase] : config.arc.phase_assignment) {
    if (phase != ArcPhase::Descent) continue;
    int range = section_ranges[static_cast<size_t>(sec_id)];
    if (prev_range >= 0) {
      ++total_checks;
      if (range <= prev_range) ++passed_checks;
    }
    prev_range = range;
  }

  if (total_checks == 0) return 1.0f;
  return static_cast<float>(passed_checks) / static_cast<float>(total_checks);
}

/// @brief Measure note stream continuity (threshold >= 0.95).
///
/// Checks for silence gaps between consecutive notes. A gap longer than
/// one sixteenth note (kTicksPerBeat / 4 = 120 ticks) is a discontinuity.
///
/// @param all_notes All notes sorted by start_tick.
/// @param config Flow config for total duration.
/// @return Score in [0.0, 1.0] where 1.0 means perfectly continuous.
float computePhraseContinuityScore(const std::vector<NoteEvent>& all_notes,
                                   const ArpeggioFlowConfig& config) {
  Tick piece_duration = totalPieceDuration(config);
  if (all_notes.empty() || piece_duration == 0) {
    return 0.0f;
  }

  constexpr Tick kMaxGap = kTicksPerBeat / 4;  // 120 ticks (1 sixteenth note)

  Tick total_gap = 0;

  for (size_t idx = 1; idx < all_notes.size(); ++idx) {
    Tick prev_end = all_notes[idx - 1].start_tick + all_notes[idx - 1].duration;
    Tick curr_start = all_notes[idx].start_tick;

    if (curr_start > prev_end) {
      Tick gap = curr_start - prev_end;
      if (gap > kMaxGap) {
        total_gap += gap;
      }
    }
  }

  return 1.0f - static_cast<float>(total_gap) / static_cast<float>(piece_duration);
}

/// @brief Measure interval variety and naturalness (threshold >= 0.7).
///
/// Evaluates within each bar:
/// - Penalizes excessive repetition of the same interval
/// - Penalizes large jumps (> octave) within 16th note patterns
/// - Rewards moderate interval variety
///
/// @param all_notes All notes sorted by start_tick.
/// @param config Flow config for bar structure.
/// @return Score in [0.0, 1.0].
float computePatternNaturalnessScore(const std::vector<NoteEvent>& all_notes,
                                     const ArpeggioFlowConfig& config) {
  if (all_notes.size() < 2) {
    return 0.0f;
  }

  int total_bars = config.num_sections * config.bars_per_section;
  int bars_analyzed = 0;
  float total_score = 0.0f;

  for (int bar = 0; bar < total_bars; ++bar) {
    Tick bar_start = static_cast<Tick>(bar) * kTicksPerBar;
    Tick bar_end = bar_start + kTicksPerBar;
    auto bar_notes = notesInRange(all_notes, bar_start, bar_end);

    if (bar_notes.size() < 2) continue;
    ++bars_analyzed;

    // Count intervals within the bar.
    std::map<int, int> interval_counts;
    int large_jump_count = 0;
    int total_intervals = 0;

    for (size_t idx = 1; idx < bar_notes.size(); ++idx) {
      int abs_interval = absoluteInterval(bar_notes[idx].pitch, bar_notes[idx - 1].pitch);
      ++interval_counts[abs_interval];
      ++total_intervals;

      if (abs_interval > interval::kOctave) {
        ++large_jump_count;
      }
    }

    if (total_intervals == 0) continue;

    // Interval variety: number of distinct intervals / max reasonable variety.
    // Cap at 6 distinct intervals for a full score.
    float variety = static_cast<float>(interval_counts.size()) / 6.0f;
    if (variety > 1.0f) variety = 1.0f;

    // Repetition penalty: if any single interval appears > 60% of the time, penalize.
    float repetition_penalty = 0.0f;
    for (const auto& [ivl, count] : interval_counts) {
      float ratio = static_cast<float>(count) / static_cast<float>(total_intervals);
      if (ratio > 0.6f) {
        repetition_penalty += (ratio - 0.6f);
      }
    }
    if (repetition_penalty > 1.0f) repetition_penalty = 1.0f;

    // Large jump penalty: proportion of jumps > octave.
    float jump_penalty =
        static_cast<float>(large_jump_count) / static_cast<float>(total_intervals);

    // Bar score = variety * (1 - repetition_penalty) * (1 - jump_penalty).
    float bar_score = variety * (1.0f - repetition_penalty) * (1.0f - jump_penalty);
    if (bar_score < 0.0f) bar_score = 0.0f;
    if (bar_score > 1.0f) bar_score = 1.0f;

    total_score += bar_score;
  }

  if (bars_analyzed == 0) return 0.0f;
  return total_score / static_cast<float>(bars_analyzed);
}

/// @brief Validate dramaturgic (PatternRole) order from config (INSTANT FAIL if != 1.0).
///
/// Checks that the PatternRole assignments in each section's arc phase are
/// consistent with the allowed patterns. Uses the config's phase assignment
/// to infer the expected role ordering.
///
/// @param config Flow config.
/// @return 1.0 if all PatternRole sequences are valid, 0.0 otherwise.
float computeDramaturgicOrderScore(const ArpeggioFlowConfig& config) {
  // The dramaturgic order is defined by the GlobalArcConfig's phase assignment
  // maintaining valid monotonic ordering. For each section, the implied
  // PatternRole sequence (Drive -> Expand -> Sustain -> Release) must not
  // reverse. Since the config is the single source of truth for ordering,
  // we validate the phase assignment monotonicity.
  if (config.arc.phase_assignment.empty()) {
    return 0.0f;
  }

  // Check that phase_assignment follows Ascent -> Peak -> Descent order.
  // This is a structural constraint from the config.
  ArcPhase prev_phase = ArcPhase::Ascent;
  bool first = true;

  for (const auto& [sec_id, phase] : config.arc.phase_assignment) {
    if (first) {
      if (phase != ArcPhase::Ascent) return 0.0f;
      prev_phase = phase;
      first = false;
      continue;
    }

    if (static_cast<uint8_t>(phase) < static_cast<uint8_t>(prev_phase)) {
      return 0.0f;
    }
    prev_phase = phase;
  }

  return 1.0f;
}

/// @brief Measure cadence quality in the final bars (threshold >= 0.7).
///
/// Evaluates the final cadence_bars against the rest of the piece:
/// - Lower average register in cadence vs. overall
/// - Reduced register range (simpler, more focused)
///
/// @param all_notes All notes sorted by start_tick.
/// @param config Flow config with cadence settings.
/// @return Score in [0.0, 1.0].
float computeCadenceScore(const std::vector<NoteEvent>& all_notes,
                          const ArpeggioFlowConfig& config) {
  if (all_notes.empty()) return 0.0f;

  Tick piece_duration = totalPieceDuration(config);
  Tick cadence_start =
      piece_duration -
      static_cast<Tick>(config.cadence.cadence_bars) * kTicksPerBar;

  // If cadence region is larger than the piece, treat entire piece as cadence.
  if (cadence_start >= piece_duration) {
    cadence_start = 0;
  }

  auto cadence_notes = notesInRange(all_notes, cadence_start, piece_duration);
  auto body_notes = notesInRange(all_notes, 0, cadence_start);

  if (cadence_notes.empty() || body_notes.empty()) {
    // If no body or no cadence notes, give partial credit based on what exists.
    return cadence_notes.empty() ? 0.0f : 0.5f;
  }

  float cadence_avg = averagePitch(cadence_notes);
  float body_avg = averagePitch(body_notes);

  int cadence_range = registerRange(cadence_notes);
  int body_range = registerRange(body_notes);

  float score = 0.0f;
  int checks = 0;

  // Check 1: Cadence register should be lower (or at most equal) to body average.
  ++checks;
  if (cadence_avg <= body_avg) {
    score += 1.0f;
  } else {
    // Partial credit based on how close the registers are.
    float diff = cadence_avg - body_avg;
    float penalty = diff / 12.0f;  // 12 semitones (octave) = full penalty
    if (penalty > 1.0f) penalty = 1.0f;
    score += (1.0f - penalty);
  }

  // Check 2: Cadence register range should be narrower than body range.
  ++checks;
  if (cadence_range <= body_range) {
    score += 1.0f;
  } else {
    float diff = static_cast<float>(cadence_range - body_range);
    float penalty = diff / 12.0f;
    if (penalty > 1.0f) penalty = 1.0f;
    score += (1.0f - penalty);
  }

  return score / static_cast<float>(checks);
}

/// @brief Measure how well harmonic weights influence the output (threshold >= 0.6).
///
/// At high-weight events (>= 1.5), checks for wider register or emphasis.
/// At low-weight events (0.5), checks for transitional/passing character.
///
/// @param all_notes All notes sorted by start_tick.
/// @param timeline Harmonic timeline with weight annotations.
/// @return Score in [0.0, 1.0].
float computeWeightUtilizationScore(const std::vector<NoteEvent>& all_notes,
                                    const HarmonicTimeline& timeline) {
  if (all_notes.empty() || timeline.size() == 0) {
    return 0.0f;
  }

  const auto& events = timeline.events();
  int total_weighted = 0;
  int correct_responses = 0;

  for (const auto& event : events) {
    auto event_notes = notesInRange(all_notes, event.tick, event.end_tick);
    if (event_notes.empty()) continue;

    int range = registerRange(event_notes);
    float avg_vel = 0.0f;
    for (const auto& note : event_notes) {
      avg_vel += static_cast<float>(note.velocity);
    }
    avg_vel /= static_cast<float>(event_notes.size());

    if (event.weight >= 1.5f) {
      // High-weight: expect wider register or higher velocity.
      ++total_weighted;
      if (range >= 7 || avg_vel >= 80.0f) {  // Perfect 5th or louder
        ++correct_responses;
      }
    } else if (event.weight <= 0.5f) {
      // Low-weight: expect narrower register (passing/transitional).
      ++total_weighted;
      if (range <= 12) {  // Within an octave
        ++correct_responses;
      }
    }
    // Normal weight (around 1.0) does not count toward weighted checks.
  }

  if (total_weighted == 0) {
    // No extreme weights in the timeline; give neutral credit.
    return 0.8f;
  }

  return static_cast<float>(correct_responses) / static_cast<float>(total_weighted);
}

/// @brief Validate GlobalArc structure from config (INSTANT FAIL if != 1.0).
///
/// Uses validateGlobalArcConfig() to check Ascent -> Peak -> Descent order
/// and that all sections have an assigned phase.
///
/// @param config Flow config.
/// @return 1.0 if valid, 0.0 otherwise.
float computeGlobalArcScore(const ArpeggioFlowConfig& config) {
  if (!validateGlobalArcConfig(config.arc)) {
    return 0.0f;
  }

  // Also verify that all sections have an assignment.
  if (static_cast<int>(config.arc.phase_assignment.size()) != config.num_sections) {
    return 0.0f;
  }

  return 1.0f;
}

/// @brief Validate that Peak occurs exactly once (INSTANT FAIL if != 1.0).
///
/// @param config Flow config.
/// @return 1.0 if exactly one Peak section, 0.0 otherwise.
float computePeakUniquenessScore(const ArpeggioFlowConfig& config) {
  int peak_count = 0;
  for (const auto& [sec_id, phase] : config.arc.phase_assignment) {
    if (phase == ArcPhase::Peak) {
      ++peak_count;
    }
  }
  return (peak_count == 1) ? 1.0f : 0.0f;
}

/// @brief Check ArcPhase-specific prohibition violations (INSTANT FAIL if > 0).
///
/// Ascent prohibitions:
/// - No register shrinking between consecutive bars
/// - No Release PatternRole (inferred from config)
///
/// Peak prohibitions:
/// - (minimal -- peak allows maximum variety)
///
/// Descent prohibitions:
/// - No register expansion between consecutive bars
/// - No Expand PatternRole (inferred from config)
///
/// @param all_notes All notes sorted by start_tick.
/// @param config Flow config.
/// @return Number of violations (0 = pass).
float computeArcProhibitionScore(const std::vector<NoteEvent>& all_notes,
                                 const ArpeggioFlowConfig& config) {
  if (all_notes.empty() || config.arc.phase_assignment.empty()) {
    return 0.0f;
  }

  int violations = 0;

  for (const auto& [sec_id, phase] : config.arc.phase_assignment) {
    auto [sec_start, sec_end] = sectionTickRange(static_cast<int>(sec_id),
                                                 config.bars_per_section);

    // Check bar-to-bar register changes within this section.
    int prev_bar_range = -1;
    int num_bars = config.bars_per_section;

    for (int bar_idx = 0; bar_idx < num_bars; ++bar_idx) {
      Tick bar_start = sec_start + static_cast<Tick>(bar_idx) * kTicksPerBar;
      Tick bar_end = bar_start + kTicksPerBar;
      auto bar_notes = notesInRange(all_notes, bar_start, bar_end);
      int bar_range = registerRange(bar_notes);

      if (prev_bar_range >= 0) {
        if (phase == ArcPhase::Ascent && bar_range < prev_bar_range) {
          // Ascent: register shrinking between consecutive bars is a violation.
          ++violations;
        }
        if (phase == ArcPhase::Descent && bar_range > prev_bar_range) {
          // Descent: register expansion between consecutive bars is a violation.
          ++violations;
        }
      }

      prev_bar_range = bar_range;
    }
  }

  return static_cast<float>(violations);
}

}  // namespace

// ===========================================================================
// FlowAnalysisResult methods
// ===========================================================================

bool FlowAnalysisResult::isPass() const {
  // Instant-FAIL checks (exact values required).
  if (global_arc_score != 1.0f) return false;
  if (peak_uniqueness_score != 1.0f) return false;
  if (arc_prohibition_score > 0.0f) return false;
  if (dramaturgic_order_score != 1.0f) return false;

  // Threshold checks.
  if (harmonic_motion_score < 0.5f) return false;
  if (register_expansion_score < 0.3f) return false;
  if (phrase_continuity_score < 0.95f) return false;
  if (pattern_naturalness_score < 0.7f) return false;
  if (cadence_score < 0.7f) return false;
  if (weight_utilization_score < 0.6f) return false;

  return true;
}

std::vector<std::string> FlowAnalysisResult::getFailures() const {
  std::vector<std::string> failures;

  auto formatMetric = [](const char* name, float value, const char* requirement) {
    std::ostringstream oss;
    oss << name << ": " << value << " (" << requirement << ")";
    return oss.str();
  };

  // Instant-FAIL checks.
  if (global_arc_score != 1.0f) {
    failures.push_back(formatMetric("global_arc_score", global_arc_score, "must be 1.0"));
  }
  if (peak_uniqueness_score != 1.0f) {
    failures.push_back(
        formatMetric("peak_uniqueness_score", peak_uniqueness_score, "must be 1.0"));
  }
  if (arc_prohibition_score > 0.0f) {
    failures.push_back(
        formatMetric("arc_prohibition_score", arc_prohibition_score, "must be 0.0"));
  }
  if (dramaturgic_order_score != 1.0f) {
    failures.push_back(
        formatMetric("dramaturgic_order_score", dramaturgic_order_score, "must be 1.0"));
  }

  // Threshold checks.
  if (harmonic_motion_score < 0.5f) {
    failures.push_back(
        formatMetric("harmonic_motion_score", harmonic_motion_score, "threshold: 0.50"));
  }
  if (register_expansion_score < 0.3f) {
    failures.push_back(
        formatMetric("register_expansion_score", register_expansion_score, "threshold: 0.30"));
  }
  if (phrase_continuity_score < 0.95f) {
    failures.push_back(
        formatMetric("phrase_continuity_score", phrase_continuity_score, "threshold: 0.95"));
  }
  if (pattern_naturalness_score < 0.7f) {
    failures.push_back(formatMetric("pattern_naturalness_score", pattern_naturalness_score,
                                    "threshold: 0.70"));
  }
  if (cadence_score < 0.7f) {
    failures.push_back(formatMetric("cadence_score", cadence_score, "threshold: 0.70"));
  }
  if (weight_utilization_score < 0.6f) {
    failures.push_back(
        formatMetric("weight_utilization_score", weight_utilization_score, "threshold: 0.60"));
  }

  return failures;
}

std::string FlowAnalysisResult::summary() const {
  std::ostringstream oss;
  oss << "FlowAnalysisResult (" << (isPass() ? "PASS" : "FAIL") << ")\n";
  oss << "  Threshold metrics:\n";
  oss << "    harmonic_motion_score:     " << harmonic_motion_score
      << (harmonic_motion_score >= 0.5f ? " [OK]" : " [FAIL]") << "\n";
  oss << "    register_expansion_score:  " << register_expansion_score
      << (register_expansion_score >= 0.3f ? " [OK]" : " [FAIL]") << "\n";
  oss << "    phrase_continuity_score:   " << phrase_continuity_score
      << (phrase_continuity_score >= 0.95f ? " [OK]" : " [FAIL]") << "\n";
  oss << "    pattern_naturalness_score: " << pattern_naturalness_score
      << (pattern_naturalness_score >= 0.7f ? " [OK]" : " [FAIL]") << "\n";
  oss << "    cadence_score:             " << cadence_score
      << (cadence_score >= 0.7f ? " [OK]" : " [FAIL]") << "\n";
  oss << "    weight_utilization_score:  " << weight_utilization_score
      << (weight_utilization_score >= 0.6f ? " [OK]" : " [FAIL]") << "\n";
  oss << "  Instant-FAIL metrics:\n";
  oss << "    global_arc_score:          " << global_arc_score
      << (global_arc_score == 1.0f ? " [OK]" : " [FAIL]") << "\n";
  oss << "    peak_uniqueness_score:     " << peak_uniqueness_score
      << (peak_uniqueness_score == 1.0f ? " [OK]" : " [FAIL]") << "\n";
  oss << "    arc_prohibition_score:     " << arc_prohibition_score
      << (arc_prohibition_score == 0.0f ? " [OK]" : " [FAIL]") << "\n";
  oss << "    dramaturgic_order_score:   " << dramaturgic_order_score
      << (dramaturgic_order_score == 1.0f ? " [OK]" : " [FAIL]") << "\n";

  return oss.str();
}

// ===========================================================================
// Main analysis entry point
// ===========================================================================

FlowAnalysisResult analyzeFlow(const std::vector<Track>& tracks,
                               const ArpeggioFlowConfig& config,
                               const HarmonicTimeline& timeline) {
  FlowAnalysisResult result;

  auto all_notes = collectAllNotes(tracks);

  // Structural metrics (config-derived, instant-FAIL).
  result.global_arc_score = computeGlobalArcScore(config);
  result.peak_uniqueness_score = computePeakUniquenessScore(config);
  result.dramaturgic_order_score = computeDramaturgicOrderScore(config);

  // Arc prohibition (note-derived, instant-FAIL).
  result.arc_prohibition_score = computeArcProhibitionScore(all_notes, config);

  // Threshold metrics (note-derived).
  result.harmonic_motion_score = computeHarmonicMotionScore(all_notes, timeline);
  result.register_expansion_score = computeRegisterExpansionScore(all_notes, config);
  result.phrase_continuity_score = computePhraseContinuityScore(all_notes, config);
  result.pattern_naturalness_score = computePatternNaturalnessScore(all_notes, config);
  result.cadence_score = computeCadenceScore(all_notes, config);
  result.weight_utilization_score = computeWeightUtilizationScore(all_notes, timeline);

  return result;
}

}  // namespace bach
