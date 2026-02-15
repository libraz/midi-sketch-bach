// Implementation of 7-dimension subject quality scoring.

#include "fugue/subject_validator.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>

#include "core/interval.h"
#include "core/json_helpers.h"
#include "core/pitch_utils.h"

namespace bach {

// ---------------------------------------------------------------------------
// SubjectScore
// ---------------------------------------------------------------------------

float SubjectScore::composite() const {
  // Weights sum to 1.0.
  // Step motion ratio gets higher weight (0.25) to enforce melodic quality.
  return interval_variety * 0.15f +
         rhythm_diversity * 0.10f +
         contour_balance * 0.15f +
         range_score * 0.10f +
         step_motion_ratio * 0.25f +
         tonal_stability * 0.15f +
         answer_compatibility * 0.10f;
}

bool SubjectScore::isAcceptable() const {
  return composite() >= 0.7f;
}

std::string SubjectScore::toJson() const {
  JsonWriter writer;
  writer.beginObject();

  writer.key("interval_variety");
  writer.value(static_cast<double>(interval_variety));
  writer.key("rhythm_diversity");
  writer.value(static_cast<double>(rhythm_diversity));
  writer.key("contour_balance");
  writer.value(static_cast<double>(contour_balance));
  writer.key("range_score");
  writer.value(static_cast<double>(range_score));
  writer.key("step_motion_ratio");
  writer.value(static_cast<double>(step_motion_ratio));
  writer.key("tonal_stability");
  writer.value(static_cast<double>(tonal_stability));
  writer.key("answer_compatibility");
  writer.value(static_cast<double>(answer_compatibility));
  writer.key("composite");
  writer.value(static_cast<double>(composite()));
  writer.key("acceptable");
  writer.value(isAcceptable());

  writer.endObject();
  return writer.toString();
}

// ---------------------------------------------------------------------------
// SubjectValidator
// ---------------------------------------------------------------------------

SubjectScore SubjectValidator::evaluate(const Subject& subject) const {
  SubjectScore score;
  score.interval_variety = scoreIntervalVariety(subject);
  score.rhythm_diversity = scoreRhythmDiversity(subject);
  score.contour_balance = scoreContourBalance(subject);
  score.range_score = scoreRange(subject);
  score.step_motion_ratio = scoreStepMotionRatio(subject);
  score.tonal_stability = scoreTonalStability(subject);
  score.answer_compatibility = scoreAnswerCompatibility(subject);

  // Hard floor: step motion ratio below 0.35 is unacceptable.
  if (subject.noteCount() >= 2) {
    int step_count = 0;
    int total_intervals = 0;
    for (size_t idx = 1; idx < subject.notes.size(); ++idx) {
      int abs_interval = absoluteInterval(subject.notes[idx].pitch,
                                          subject.notes[idx - 1].pitch);
      total_intervals++;
      if (abs_interval >= 1 && abs_interval <= 2) step_count++;
    }
    if (total_intervals > 0) {
      float raw_step_ratio =
          static_cast<float>(step_count) / total_intervals;
      if (raw_step_ratio < 0.35f) {
        // Force rejection by zeroing step_motion_ratio.
        score.step_motion_ratio = 0.0f;
      }
    }
  }

  // Repeated pitch ratio: reject if >30% of notes are same-pitch repetitions.
  if (subject.noteCount() >= 2) {
    int repeated = 0;
    for (size_t idx = 1; idx < subject.notes.size(); ++idx) {
      if (subject.notes[idx].pitch == subject.notes[idx - 1].pitch) {
        repeated++;
      }
    }
    float repeated_pitch_ratio =
        static_cast<float>(repeated) /
        static_cast<float>(subject.noteCount() - 1);
    if (repeated_pitch_ratio > 0.30f) {
      score.interval_variety *= 0.3f;  // Heavy penalty.
    }
  }

  // Repeated rhythm ratio: reject if >40% of consecutive durations are identical.
  if (subject.noteCount() >= 2) {
    int same_dur = 0;
    for (size_t idx = 1; idx < subject.notes.size(); ++idx) {
      if (subject.notes[idx].duration == subject.notes[idx - 1].duration) {
        same_dur++;
      }
    }
    float repeated_rhythm_ratio =
        static_cast<float>(same_dur) /
        static_cast<float>(subject.noteCount() - 1);
    if (repeated_rhythm_ratio > 0.40f) {
      score.rhythm_diversity *= 0.5f;  // Moderate penalty.
    }
  }

  return score;
}

float SubjectValidator::scoreIntervalVariety(const Subject& subject) const {
  if (subject.noteCount() < 2) return 0.0f;

  std::set<int> unique_intervals;
  int thirds_and_sixths = 0;
  int total_intervals = 0;

  for (size_t idx = 1; idx < subject.notes.size(); ++idx) {
    int abs_interval = absoluteInterval(subject.notes[idx].pitch,
                                        subject.notes[idx - 1].pitch);
    // Reduce to within-octave.
    int reduced = interval_util::compoundToSimple(abs_interval);
    unique_intervals.insert(reduced);
    total_intervals++;

    // Bonus for consonant intervals (3rds and 6ths).
    if (reduced == interval::kMinor3rd || reduced == interval::kMajor3rd ||
        reduced == interval::kMinor6th || reduced == interval::kMajor6th) {
      thirds_and_sixths++;
    }
  }

  // Base score: unique intervals / 7 (there are ~7 distinct interval types
  // within an octave that are musically relevant).
  float base = std::min(1.0f, static_cast<float>(unique_intervals.size()) / 5.0f);

  // Bonus for 3rds/6ths (up to 0.2 extra).
  float consonance_bonus = 0.0f;
  if (total_intervals > 0) {
    float ratio = static_cast<float>(thirds_and_sixths) / total_intervals;
    consonance_bonus = std::min(0.2f, ratio * 0.4f);
  }

  return std::min(1.0f, base + consonance_bonus);
}

float SubjectValidator::scoreRhythmDiversity(const Subject& subject) const {
  if (subject.noteCount() < 2) return 0.0f;

  // Count occurrences of each duration.
  std::map<Tick, int> duration_counts;
  for (const auto& note : subject.notes) {
    duration_counts[note.duration]++;
  }

  // Find the most common duration.
  int max_count = 0;
  for (const auto& pair : duration_counts) {
    if (pair.second > max_count) max_count = pair.second;
  }

  // If the most common duration is <= 50% of total notes, score = 1.0.
  // Otherwise, linearly decrease.
  float dominant_ratio = static_cast<float>(max_count) /
                         static_cast<float>(subject.noteCount());

  if (dominant_ratio <= 0.5f) return 1.0f;

  // Linear falloff from 1.0 at 50% to 0.0 at 100%.
  return std::max(0.0f, 1.0f - (dominant_ratio - 0.5f) * 2.0f);
}

float SubjectValidator::scoreContourBalance(const Subject& subject) const {
  if (subject.noteCount() < 3) return 0.5f;

  // Count how many times the highest pitch appears (should be exactly 1).
  uint8_t highest = subject.highestPitch();
  int peak_count = 0;
  for (const auto& note : subject.notes) {
    if (note.pitch == highest) peak_count++;
  }

  // Single peak is ideal.
  float peak_score = (peak_count == 1) ? 1.0f : std::max(0.3f, 1.0f / peak_count);

  // Bonus: leap followed by contrary-direction step.
  int contrary_motion_bonus_count = 0;
  int leap_count = 0;
  for (size_t idx = 1; idx < subject.notes.size(); ++idx) {
    int curr_interval = directedInterval(
        subject.notes[idx - 1].pitch, subject.notes[idx].pitch);
    int abs_curr = std::abs(curr_interval);

    // A leap is 3+ semitones.
    if (abs_curr >= 3) {
      leap_count++;
      // Check if the next note moves in the opposite direction by step.
      if (idx + 1 < subject.notes.size()) {
        int next_interval = directedInterval(
            subject.notes[idx].pitch, subject.notes[idx + 1].pitch);
        bool opposite_dir = (curr_interval > 0 && next_interval < 0) ||
                            (curr_interval < 0 && next_interval > 0);
        int abs_next = std::abs(next_interval);
        bool is_step = (abs_next >= 1 && abs_next <= 2);
        if (opposite_dir && is_step) {
          contrary_motion_bonus_count++;
        }
      }
    }
  }

  float contrary_bonus = 0.0f;
  if (leap_count > 0) {
    contrary_bonus = std::min(
        0.2f,
        static_cast<float>(contrary_motion_bonus_count) / leap_count * 0.3f);
  }

  return std::min(1.0f, peak_score * 0.8f + contrary_bonus + 0.05f);
}

float SubjectValidator::scoreRange(const Subject& subject) const {
  if (subject.noteCount() < 2) return 0.5f;

  int range_semitones = subject.range();

  // Too small a range (< 3) is also penalized -- subject is too static.
  if (range_semitones < 3) {
    return 0.3f + static_cast<float>(range_semitones) * 0.1f;
  }

  // Ideal range: 4-12 semitones.
  if (range_semitones <= 12) return 1.0f;

  // Penalty for each semitone beyond 12.
  float penalty = static_cast<float>(range_semitones - 12) * 0.1f;
  return std::max(0.0f, 1.0f - penalty);
}

float SubjectValidator::scoreStepMotionRatio(const Subject& subject) const {
  if (subject.noteCount() < 2) return 0.5f;

  int step_count = 0;
  int total_intervals = 0;

  for (size_t idx = 1; idx < subject.notes.size(); ++idx) {
    int abs_interval = absoluteInterval(subject.notes[idx].pitch,
                                        subject.notes[idx - 1].pitch);
    total_intervals++;
    // Steps are 1-2 semitones.
    if (abs_interval >= 1 && abs_interval <= 2) {
      step_count++;
    }
  }

  if (total_intervals == 0) return 0.5f;

  float ratio = static_cast<float>(step_count) / total_intervals;

  // Ideal range: 0.5-0.85. Returns 1.0 within this window.
  if (ratio >= 0.5f && ratio <= 0.85f) return 1.0f;

  // Below 0.5: linear falloff.
  if (ratio < 0.5f) {
    return std::max(0.0f, ratio / 0.5f);
  }

  // Above 0.85: gentle falloff (too much stepwise is monotonous).
  return std::max(0.3f, 1.0f - (ratio - 0.85f) * 3.0f);
}

float SubjectValidator::scoreTonalStability(const Subject& subject) const {
  if (subject.notes.empty()) return 0.0f;

  int key_offset = static_cast<int>(subject.key);
  int tonic_class = getPitchClass(static_cast<uint8_t>(key_offset));
  int dominant_class = (key_offset + interval::kPerfect5th) % 12;

  float score = 0.0f;

  // Start on tonic or dominant = bonus.
  int start_class = getPitchClass(subject.notes.front().pitch);
  if (start_class == tonic_class) {
    score += 0.3f;
  } else if (start_class == dominant_class) {
    score += 0.25f;
  }

  // End on tonic or dominant = bonus.
  int end_class = getPitchClass(subject.notes.back().pitch);
  if (end_class == tonic_class) {
    score += 0.3f;
  } else if (end_class == dominant_class) {
    score += 0.25f;
  }

  // Count scale-tone usage.
  const int* scale = getScaleIntervals(
      subject.is_minor ? ScaleType::HarmonicMinor : ScaleType::Major);
  int scale_tone_count = 0;
  for (const auto& note : subject.notes) {
    int pc = getPitchClassSigned(getPitchClass(note.pitch) - key_offset);
    for (int deg = 0; deg < 7; ++deg) {
      if (scale[deg] == pc) {
        scale_tone_count++;
        break;
      }
    }
  }

  float scale_ratio = static_cast<float>(scale_tone_count) /
                      static_cast<float>(subject.notes.size());
  score += scale_ratio * 0.4f;

  return std::min(1.0f, score);
}

float SubjectValidator::scoreAnswerCompatibility(const Subject& subject) const {
  if (subject.noteCount() < 2) return 0.5f;

  int key_offset = static_cast<int>(subject.key);
  const int* scale = getScaleIntervals(
      subject.is_minor ? ScaleType::HarmonicMinor : ScaleType::Major);

  // Count chromatic notes (not in the scale).
  int chromatic_count = 0;
  for (const auto& note : subject.notes) {
    int pc = getPitchClassSigned(getPitchClass(note.pitch) - key_offset);
    bool in_scale = false;
    for (int deg = 0; deg < 7; ++deg) {
      if (scale[deg] == pc) {
        in_scale = true;
        break;
      }
    }
    if (!in_scale) chromatic_count++;
  }

  float chromatic_ratio = static_cast<float>(chromatic_count) /
                          static_cast<float>(subject.noteCount());

  // No chromaticism = 1.0; heavy chromaticism (>30%) = 0.0.
  if (chromatic_ratio <= 0.05f) return 1.0f;
  if (chromatic_ratio >= 0.3f) return 0.0f;

  // Linear falloff between 5% and 30%.
  return 1.0f - (chromatic_ratio - 0.05f) / 0.25f;
}

}  // namespace bach
