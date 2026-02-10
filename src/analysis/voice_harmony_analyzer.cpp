// Implementation of read-only voice-harmony alignment analysis.

#include "analysis/voice_harmony_analyzer.h"

#include <algorithm>
#include <cmath>
#include <sstream>

#include "core/pitch_utils.h"
#include "harmony/chord_types.h"
#include "harmony/harmonic_event.h"

namespace bach {

namespace {

/// @brief Check whether the interval from one pitch to the next is stepwise.
/// @param pitch_a Starting pitch.
/// @param pitch_b Ending pitch.
/// @return True if the absolute interval is 1 or 2 semitones.
bool isStepwise(uint8_t pitch_a, uint8_t pitch_b) {
  int diff = absoluteInterval(pitch_a, pitch_b);
  return diff >= 1 && diff <= 2;
}

/// @brief Determine melodic direction between two pitches.
/// @param pitch_a Earlier pitch.
/// @param pitch_b Later pitch.
/// @return +1 ascending, -1 descending, 0 same.
int melodicDirection(uint8_t pitch_a, uint8_t pitch_b) {
  if (pitch_b > pitch_a) return 1;
  if (pitch_b < pitch_a) return -1;
  return 0;
}

/// @brief Format a percentage as a string (e.g. "78%").
std::string formatPercent(float ratio) {
  int pct = static_cast<int>(std::round(ratio * 100.0f));
  return std::to_string(pct) + "%";
}

}  // namespace

// ---------------------------------------------------------------------------
// Single-track analysis
// ---------------------------------------------------------------------------

VoiceHarmonyReport analyzeTrackHarmony(const std::vector<NoteEvent>& notes,
                                       const HarmonicTimeline& timeline) {
  VoiceHarmonyReport report;
  report.total_notes = static_cast<int>(notes.size());

  if (notes.empty()) {
    return report;
  }

  // Count chord tones and stepwise motion.
  for (size_t idx = 0; idx < notes.size(); ++idx) {
    const auto& note = notes[idx];
    const HarmonicEvent& event = timeline.getAt(note.start_tick);

    if (isChordTone(note.pitch, event)) {
      ++report.chord_tone_notes;
    }

    // Stepwise motion: compare with previous note (first note has no predecessor).
    if (idx > 0) {
      if (isStepwise(notes[idx - 1].pitch, note.pitch)) {
        ++report.stepwise_notes;
      }
    }
  }

  // Compute ratios.
  report.chord_tone_coverage =
      static_cast<float>(report.chord_tone_notes) / static_cast<float>(report.total_notes);

  // Voice leading quality: stepwise transitions out of possible transitions (n-1).
  int transitions = report.total_notes - 1;
  if (transitions > 0) {
    report.voice_leading_quality =
        static_cast<float>(report.stepwise_notes) / static_cast<float>(transitions);
  }

  // Count suspensions.
  report.suspension_count = countSuspensions(notes, timeline);

  return report;
}

// ---------------------------------------------------------------------------
// Multi-track analysis
// ---------------------------------------------------------------------------

VoiceHarmonyReport analyzeVoiceHarmony(const std::vector<Track>& tracks,
                                       const HarmonicTimeline& timeline) {
  VoiceHarmonyReport report;

  if (tracks.empty()) {
    return report;
  }

  // Aggregate per-track results.
  int total_chord_tones = 0;
  int total_notes = 0;
  int total_stepwise = 0;
  int total_transitions = 0;
  int total_suspensions = 0;

  for (size_t track_idx = 0; track_idx < tracks.size(); ++track_idx) {
    const auto& track = tracks[track_idx];
    VoiceHarmonyReport track_report = analyzeTrackHarmony(track.notes, timeline);

    total_chord_tones += track_report.chord_tone_notes;
    total_notes += track_report.total_notes;
    total_stepwise += track_report.stepwise_notes;
    total_suspensions += track_report.suspension_count;

    int track_transitions = track_report.total_notes - 1;
    if (track_transitions > 0) {
      total_transitions += track_transitions;
    }

    // Add per-track observation.
    if (track_report.total_notes > 0) {
      std::ostringstream oss;
      oss << "Track " << track_idx << ": "
          << formatPercent(track_report.chord_tone_coverage) << " chord tones";
      report.observations.push_back(oss.str());
    }
  }

  report.total_notes = total_notes;
  report.chord_tone_notes = total_chord_tones;
  report.stepwise_notes = total_stepwise;
  report.suspension_count = total_suspensions;

  if (total_notes > 0) {
    report.chord_tone_coverage =
        static_cast<float>(total_chord_tones) / static_cast<float>(total_notes);
  }

  if (total_transitions > 0) {
    report.voice_leading_quality =
        static_cast<float>(total_stepwise) / static_cast<float>(total_transitions);
  }

  // Contrary motion analysis: compare consecutive notes between track pairs.
  int contrary_count = 0;
  int comparison_count = 0;

  for (size_t idx_a = 0; idx_a < tracks.size(); ++idx_a) {
    for (size_t idx_b = idx_a + 1; idx_b < tracks.size(); ++idx_b) {
      const auto& notes_a = tracks[idx_a].notes;
      const auto& notes_b = tracks[idx_b].notes;

      if (notes_a.size() < 2 || notes_b.size() < 2) {
        continue;
      }

      // Compare motion at each beat boundary. Walk both note lists by tick.
      size_t pos_a = 0;
      size_t pos_b = 0;

      while (pos_a + 1 < notes_a.size() && pos_b + 1 < notes_b.size()) {
        // Find the next transition in both voices that occurs near the same tick.
        Tick tick_a = notes_a[pos_a + 1].start_tick;
        Tick tick_b = notes_b[pos_b + 1].start_tick;

        constexpr Tick kProximity = 120;  // 16th-note tolerance

        if (tick_a <= tick_b + kProximity && tick_b <= tick_a + kProximity) {
          // Both voices transition at roughly the same time.
          int dir_a = melodicDirection(notes_a[pos_a].pitch, notes_a[pos_a + 1].pitch);
          int dir_b = melodicDirection(notes_b[pos_b].pitch, notes_b[pos_b + 1].pitch);

          if (dir_a != 0 && dir_b != 0) {
            ++comparison_count;
            if (dir_a != dir_b) {
              ++contrary_count;
            }
          }
          ++pos_a;
          ++pos_b;
        } else if (tick_a < tick_b) {
          ++pos_a;
        } else {
          ++pos_b;
        }
      }
    }
  }

  if (comparison_count > 0) {
    report.contrary_motion_ratio =
        static_cast<float>(contrary_count) / static_cast<float>(comparison_count);
  }

  // Add summary observations.
  if (report.contrary_motion_ratio < 0.4f && comparison_count > 0) {
    std::ostringstream oss;
    oss << "Low contrary motion (" << formatPercent(report.contrary_motion_ratio) << ")";
    report.observations.push_back(oss.str());
  }

  if (report.chord_tone_coverage < 0.6f && total_notes > 0) {
    std::ostringstream oss;
    oss << "Low chord tone coverage (" << formatPercent(report.chord_tone_coverage) << ")";
    report.observations.push_back(oss.str());
  }

  return report;
}

// ---------------------------------------------------------------------------
// Suspension detection
// ---------------------------------------------------------------------------

int countSuspensions(const std::vector<NoteEvent>& notes, const HarmonicTimeline& timeline) {
  if (notes.size() < 2) {
    return 0;
  }

  int suspensions = 0;

  for (size_t idx = 0; idx + 1 < notes.size(); ++idx) {
    const auto& held_note = notes[idx];
    const auto& next_note = notes[idx + 1];

    // Condition 1: The held note must extend past the next note's start tick
    // (i.e., it is actually held/tied across a boundary), OR the next note
    // starts on the same pitch (repeated = effectively held).
    Tick held_end = held_note.start_tick + held_note.duration;
    bool is_held = held_end >= next_note.start_tick;
    bool is_repeated = held_note.pitch == next_note.pitch;

    if (!is_held && !is_repeated) {
      continue;
    }

    // For suspension detection, we need to check the chord at the point where
    // the held note becomes dissonant. This is typically the next beat boundary.
    Tick check_tick = next_note.start_tick;
    if (is_held && next_note.start_tick > held_note.start_tick) {
      check_tick = next_note.start_tick;
    }

    const HarmonicEvent& event_at_check = timeline.getAt(check_tick);

    // Condition 2: The held pitch must be dissonant against the current chord.
    if (isChordTone(held_note.pitch, event_at_check)) {
      continue;  // Not dissonant, so not a suspension.
    }

    // Condition 3: Resolution must be downward by step.
    if (idx + 1 < notes.size()) {
      // Check if the next note resolves downward by step from the held pitch.
      // In a repeated-pitch suspension, check the note after the repetition.
      size_t resolution_idx = idx + 1;
      if (is_repeated && resolution_idx + 1 < notes.size()) {
        resolution_idx = idx + 2;
      }

      if (resolution_idx < notes.size()) {
        const auto& resolution_note = notes[resolution_idx];
        int directed = directedInterval(held_note.pitch, resolution_note.pitch);
        // Downward by step: directed interval is -1 or -2.
        if (directed >= -2 && directed <= -1) {
          ++suspensions;
        }
      }
    }
  }

  return suspensions;
}

}  // namespace bach
