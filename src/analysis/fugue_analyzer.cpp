// Fugue structure quality analysis implementation.

#include "analysis/fugue_analyzer.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include "analysis/cadence_detector.h"
#include "analysis/counterpoint_analyzer.h"
#include "core/basic_types.h"
#include "core/interval.h"
#include "core/pitch_utils.h"
#include "harmony/harmonic_timeline.h"

namespace bach {
namespace {

/// @brief Extract notes for a single voice, sorted by start_tick.
std::vector<NoteEvent> extractVoice(const std::vector<NoteEvent>& notes, VoiceId voice) {
  std::vector<NoteEvent> result;
  for (const auto& note : notes) {
    if (note.voice == voice) result.push_back(note);
  }
  std::sort(result.begin(), result.end(),
            [](const NoteEvent& lhs, const NoteEvent& rhs) {
              return lhs.start_tick < rhs.start_tick;
            });
  return result;
}

/// @brief Compute interval sequence (signed pitch diffs between consecutive notes).
std::vector<int> intervalSequence(const std::vector<NoteEvent>& sorted_notes) {
  std::vector<int> intervals;
  if (sorted_notes.size() < 2) return intervals;
  intervals.reserve(sorted_notes.size() - 1);
  for (size_t idx = 1; idx < sorted_notes.size(); ++idx) {
    intervals.push_back(static_cast<int>(sorted_notes[idx].pitch) -
                        static_cast<int>(sorted_notes[idx - 1].pitch));
  }
  return intervals;
}

/// @brief Check if candidate contains the subject interval pattern (sliding window).
bool matchesIntervalPattern(const std::vector<int>& candidate,
                            const std::vector<int>& subject, int tolerance = 1) {
  if (candidate.size() < subject.size()) return false;
  for (size_t start = 0; start + subject.size() <= candidate.size(); ++start) {
    bool match = true;
    for (size_t idx = 0; idx < subject.size(); ++idx) {
      if (std::abs(candidate[start + idx] - subject[idx]) > tolerance) { match = false; break; }
    }
    if (match) return true;
  }
  return false;
}

Tick totalEndTick(const std::vector<NoteEvent>& notes) {
  Tick max_end = 0;
  for (const auto& note : notes) {
    Tick end = note.start_tick + note.duration;
    if (end > max_end) max_end = end;
  }
  return max_end;
}

int dominantPitchClass(const std::vector<NoteEvent>& notes) {
  uint32_t counts[12] = {};
  for (const auto& note : notes) counts[static_cast<int>(note.pitch) % 12]++;
  int best = 0;
  for (int idx = 1; idx < 12; ++idx) {
    if (counts[idx] > counts[best]) best = idx;
  }
  return best;
}

std::vector<NoteEvent> notesInRange(const std::vector<NoteEvent>& notes, Tick start, Tick end) {
  std::vector<NoteEvent> result;
  for (const auto& note : notes) {
    if (note.start_tick >= start && note.start_tick < end) result.push_back(note);
  }
  return result;
}

float clamp01(float val) { return val < 0.0f ? 0.0f : (val > 1.0f ? 1.0f : val); }

/// @brief Compute the subject's tick duration.
Tick subjectDuration(const std::vector<NoteEvent>& subject_notes) {
  Tick max_end = 0;
  for (const auto& note : subject_notes) {
    Tick end = note.start_tick + note.duration;
    if (end > max_end) max_end = end;
  }
  return max_end > 0 ? max_end : kTicksPerBar;
}

}  // namespace

// ---------------------------------------------------------------------------
// expositionCompletenessScore
// ---------------------------------------------------------------------------

float expositionCompletenessScore(const std::vector<NoteEvent>& notes,
                                  uint8_t num_voices,
                                  const std::vector<NoteEvent>& subject_notes) {
  if (num_voices == 0 || subject_notes.size() < 2) return 0.0f;
  auto subj_ivl = intervalSequence(subject_notes);
  if (subj_ivl.empty()) return 0.0f;

  Tick expo_end = subjectDuration(subject_notes) * num_voices + kTicksPerBar;
  uint8_t voices_found = 0;

  for (uint8_t vid = 0; vid < num_voices; ++vid) {
    auto voice = extractVoice(notes, vid);
    std::vector<NoteEvent> expo_notes;
    for (const auto& note : voice) {
      if (note.start_tick < expo_end) expo_notes.push_back(note);
    }
    if (expo_notes.size() < subject_notes.size()) continue;
    if (matchesIntervalPattern(intervalSequence(expo_notes), subj_ivl)) ++voices_found;
  }
  return static_cast<float>(voices_found) / static_cast<float>(num_voices);
}

// ---------------------------------------------------------------------------
// tonalPlanScore
// ---------------------------------------------------------------------------

float tonalPlanScore(const std::vector<NoteEvent>& notes) {
  if (notes.empty()) return 0.0f;
  Tick end = totalEndTick(notes);
  if (end == 0) return 0.0f;

  constexpr int kQuarters = 4;
  Tick qlen = std::max(end / kQuarters, static_cast<Tick>(1));
  int centers[kQuarters] = {};

  for (int qtr = 0; qtr < kQuarters; ++qtr) {
    Tick qs = static_cast<Tick>(qtr) * qlen;
    Tick qe = (qtr == kQuarters - 1) ? end : qs + qlen;
    auto qnotes = notesInRange(notes, qs, qe);
    centers[qtr] = qnotes.empty() ? -1 : dominantPitchClass(qnotes);
  }

  // Count distinct tonal centers.
  int unique = 0;
  bool seen[12] = {};
  for (int qtr = 0; qtr < kQuarters; ++qtr) {
    if (centers[qtr] >= 0 && !seen[centers[qtr]]) { seen[centers[qtr]] = true; ++unique; }
  }

  float score = clamp01(static_cast<float>(unique) / 4.0f);
  if (centers[0] >= 0 && centers[0] == centers[kQuarters - 1]) score += 0.15f;  // Tonal return.
  if (centers[0] >= 0 && centers[1] >= 0 && centers[0] != centers[1]) score += 0.1f;  // Modulation.
  return clamp01(score);
}

// ---------------------------------------------------------------------------
// analyzeFugue
// ---------------------------------------------------------------------------

FugueAnalysisResult analyzeFugue(const std::vector<NoteEvent>& notes,
                                 uint8_t num_voices,
                                 const std::vector<NoteEvent>& subject_notes) {
  FugueAnalysisResult result;
  result.exposition_completeness_score =
      expositionCompletenessScore(notes, num_voices, subject_notes);
  result.tonal_plan_score = tonalPlanScore(notes);

  // Answer accuracy: check second voice entry for proper P5/P4 transposition.
  if (num_voices >= 2 && subject_notes.size() >= 2) {
    auto subj_ivl = intervalSequence(subject_notes);
    auto answer_voice = extractVoice(notes, 1);
    if (answer_voice.size() >= subject_notes.size()) {
      auto ans_ivl = intervalSequence(answer_voice);
      if (matchesIntervalPattern(ans_ivl, subj_ivl, 1)) {
        int trans = static_cast<int>(answer_voice[0].pitch) -
                    static_cast<int>(subject_notes[0].pitch);
        int abs_trans = interval_util::compoundToSimple(trans);
        if (abs_trans == 7 || abs_trans == 5) result.answer_accuracy_score = 1.0f;
        else if (abs_trans == 0) result.answer_accuracy_score = 0.5f;
        else result.answer_accuracy_score = 0.3f;
      }
    }
  }

  // Episode motif usage: check post-exposition notes for subject fragment reuse.
  if (subject_notes.size() >= 2) {
    Tick sub_dur = subjectDuration(subject_notes);
    Tick expo_end = sub_dur * num_voices + kTicksPerBar;
    Tick piece_end = totalEndTick(notes);

    if (piece_end > expo_end) {
      auto post_expo = notesInRange(notes, expo_end, piece_end);
      if (post_expo.size() >= 2) {
        auto full_ivl = intervalSequence(subject_notes);
        size_t frag_len = std::min(full_ivl.size(), static_cast<size_t>(3));
        std::vector<int> fragment(full_ivl.begin(),
                                  full_ivl.begin() + static_cast<ptrdiff_t>(frag_len));

        uint32_t voices_with_motif = 0;
        for (uint8_t vid = 0; vid < num_voices; ++vid) {
          auto vpost = extractVoice(post_expo, vid);
          if (vpost.size() < fragment.size() + 1) continue;
          if (matchesIntervalPattern(intervalSequence(vpost), fragment, 2)) ++voices_with_motif;
        }
        result.episode_motif_usage_rate =
            clamp01(static_cast<float>(voices_with_motif) / static_cast<float>(num_voices));
      }
    }
  }
  return result;
}

// ---------------------------------------------------------------------------
// computeCadenceDetectionRate
// ---------------------------------------------------------------------------

float computeCadenceDetectionRate(const HarmonicTimeline& timeline,
                                  const std::vector<Tick>& section_end_ticks) {
  if (section_end_ticks.empty()) return 0.0f;

  auto detected = detectCadences(timeline);
  return cadenceDetectionRate(detected, section_end_ticks);
}

// ---------------------------------------------------------------------------
// computeMotivicUnityScore
// ---------------------------------------------------------------------------

float computeMotivicUnityScore(const std::vector<NoteEvent>& notes,
                                const std::vector<NoteEvent>& subject_notes,
                                uint8_t num_voices) {
  if (notes.empty() || subject_notes.size() < 4 || num_voices == 0) return 0.0f;

  // Extract a 3-interval fragment from the subject (first 4 notes -> 3 intervals).
  auto sorted_subject = subject_notes;
  std::sort(sorted_subject.begin(), sorted_subject.end(),
            [](const NoteEvent& lhs, const NoteEvent& rhs) {
              return lhs.start_tick < rhs.start_tick;
            });
  auto full_ivl = intervalSequence(sorted_subject);
  if (full_ivl.size() < 3) return 0.0f;

  std::vector<int> fragment(full_ivl.begin(), full_ivl.begin() + 3);

  // Divide piece into 4 equal time quarters.
  Tick end_tick = totalEndTick(notes);
  if (end_tick == 0) return 0.0f;

  constexpr int kQuarters = 4;
  Tick quarter_len = std::max(end_tick / kQuarters, static_cast<Tick>(1));

  uint32_t cells_hit = 0;
  uint32_t total_cells = static_cast<uint32_t>(kQuarters) * static_cast<uint32_t>(num_voices);

  for (int qtr = 0; qtr < kQuarters; ++qtr) {
    Tick qtr_start = static_cast<Tick>(qtr) * quarter_len;
    Tick qtr_end = (qtr == kQuarters - 1) ? end_tick : qtr_start + quarter_len;
    auto qtr_notes = notesInRange(notes, qtr_start, qtr_end);

    for (uint8_t vid = 0; vid < num_voices; ++vid) {
      auto voice_notes = extractVoice(qtr_notes, vid);
      if (voice_notes.size() < 4) continue;  // Need at least 4 notes for 3 intervals.
      auto voice_ivl = intervalSequence(voice_notes);
      if (matchesIntervalPattern(voice_ivl, fragment, 2)) {
        ++cells_hit;
      }
    }
  }

  return clamp01(static_cast<float>(cells_hit) / static_cast<float>(total_cells));
}

// ---------------------------------------------------------------------------
// computeTonalConsistencyScore
// ---------------------------------------------------------------------------

float computeTonalConsistencyScore(const std::vector<NoteEvent>& notes,
                                    Key tonic_key, bool is_minor) {
  if (notes.empty()) return 0.0f;

  // Count pitch class distribution (12 bins).
  uint32_t counts[12] = {};
  uint32_t total_notes = 0;
  for (const auto& note : notes) {
    counts[static_cast<int>(note.pitch) % 12]++;
    ++total_notes;
  }
  if (total_notes == 0) return 0.0f;

  const int* scale_offsets = is_minor ? kScaleNaturalMinor : kScaleMajor;
  int tonic_pc = static_cast<int>(tonic_key);

  // Build a set of scale-tone pitch classes.
  bool on_scale[12] = {};
  for (int idx = 0; idx < 7; ++idx) {
    int pitch_class = (tonic_pc + scale_offsets[idx]) % 12;
    on_scale[pitch_class] = true;
  }

  // Count notes on scale tones.
  uint32_t on_scale_count = 0;
  for (int pitch_class = 0; pitch_class < 12; ++pitch_class) {
    if (on_scale[pitch_class]) {
      on_scale_count += counts[pitch_class];
    }
  }

  float score = static_cast<float>(on_scale_count) / static_cast<float>(total_notes);

  // Bonus: +0.1 if tonic pitch class is the most frequent.
  uint32_t tonic_count = counts[tonic_pc];
  bool tonic_is_max = true;
  for (int pitch_class = 0; pitch_class < 12; ++pitch_class) {
    if (pitch_class != tonic_pc && counts[pitch_class] > tonic_count) {
      tonic_is_max = false;
      break;
    }
  }
  if (tonic_is_max && tonic_count > 0) {
    score += 0.1f;
  }

  return clamp01(score);
}

// ---------------------------------------------------------------------------
// invertibleCounterpointScore
// ---------------------------------------------------------------------------

float invertibleCounterpointScore(const std::vector<NoteEvent>& subject_notes,
                                   const std::vector<NoteEvent>& counter_notes,
                                   uint8_t num_voices) {
  (void)num_voices;  // Inversion analysis is always between two voice parts.
  if (subject_notes.empty() || counter_notes.empty()) return 1.0f;

  // Create inverted version: swap octave relationship.
  // Original: subject in upper, counter in lower.
  // Inverted: counter as upper voice, subject transposed down an octave as lower.
  std::vector<NoteEvent> inverted_notes;
  inverted_notes.reserve(subject_notes.size() + counter_notes.size());

  // Subject notes transposed down an octave (voice 1 = lower).
  for (const auto& note : subject_notes) {
    NoteEvent inv = note;
    int new_pitch = static_cast<int>(note.pitch) - 12;
    inv.pitch = static_cast<uint8_t>(std::max(0, std::min(127, new_pitch)));
    inv.voice = 1;
    inverted_notes.push_back(inv);
  }

  // Counter notes as upper voice (voice 0 = upper).
  for (const auto& note : counter_notes) {
    NoteEvent inv = note;
    inv.voice = 0;
    inverted_notes.push_back(inv);
  }

  // Count parallel perfect intervals in the inverted version.
  uint32_t violations = countParallelPerfect(inverted_notes, 2);

  // Score: deduct per violation relative to total beats.
  Tick total_end = 0;
  for (const auto& note : inverted_notes) {
    Tick end = note.start_tick + note.duration;
    if (end > total_end) total_end = end;
  }
  uint32_t total_beats = static_cast<uint32_t>(total_end / kTicksPerBeat);
  if (total_beats == 0) return 1.0f;

  float violation_rate = static_cast<float>(violations) / static_cast<float>(total_beats);
  float score = 1.0f - violation_rate;
  if (score < 0.0f) score = 0.0f;
  return score;
}

}  // namespace bach
