// Fugue structure quality analysis implementation.

#include "analysis/fugue_analyzer.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include "core/basic_types.h"
#include "core/pitch_utils.h"

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
        int abs_trans = ((trans % 12) + 12) % 12;
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

}  // namespace bach
