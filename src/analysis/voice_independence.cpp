// Voice independence analysis for multi-voice textures.

#include "analysis/voice_independence.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <set>
#include <vector>

namespace bach {
namespace {

/// Tolerance for considering two onsets "simultaneous" (16th-note = 120 ticks).
constexpr Tick kSimultaneousTolerance = kTicksPerBeat / 4;  // 120

/// @brief Filter notes belonging to a specific voice and sort by start_tick.
std::vector<NoteEvent> filterAndSort(const std::vector<NoteEvent>& notes, VoiceId voice) {
  std::vector<NoteEvent> result;
  result.reserve(notes.size());
  for (const auto& note : notes) {
    if (note.voice == voice) {
      result.push_back(note);
    }
  }
  std::sort(result.begin(), result.end(),
            [](const NoteEvent& lhs, const NoteEvent& rhs) {
              return lhs.start_tick < rhs.start_tick;
            });
  return result;
}

/// @brief Determine the end tick of the latest note in a voice.
Tick lastEndTick(const std::vector<NoteEvent>& voice_notes) {
  Tick max_end = 0;
  for (const auto& note : voice_notes) {
    Tick end = note.start_tick + note.duration;
    if (end > max_end) {
      max_end = end;
    }
  }
  return max_end;
}

/// @brief Find the pitch of the note sounding at or most recently before a given tick.
/// @return -1 if no note is found before tick.
int pitchAtTick(const std::vector<NoteEvent>& sorted_notes, Tick tick) {
  int result = -1;
  for (const auto& note : sorted_notes) {
    if (note.start_tick <= tick) {
      result = static_cast<int>(note.pitch);
    } else {
      break;
    }
  }
  return result;
}

/// @brief Classify melodic direction between two consecutive pitch values.
/// @return -1 for down, 0 for same, +1 for up.
int melodicDirection(int prev_pitch, int curr_pitch) {
  if (curr_pitch > prev_pitch) return 1;
  if (curr_pitch < prev_pitch) return -1;
  return 0;
}

/// @brief Clamp a float to [0, 1].
float clamp01(float val) {
  if (val < 0.0f) return 0.0f;
  if (val > 1.0f) return 1.0f;
  return val;
}

}  // namespace

// ---------------------------------------------------------------------------
// VoiceIndependenceScore
// ---------------------------------------------------------------------------

float VoiceIndependenceScore::composite() const {
  return clamp01(0.4f * rhythm_independence +
                 0.3f * contour_independence +
                 0.3f * register_separation);
}

bool VoiceIndependenceScore::meetsTrioStandard() const {
  return composite() >= 0.6f;
}

// ---------------------------------------------------------------------------
// Rhythm independence
// ---------------------------------------------------------------------------

float calculateRhythmIndependence(const std::vector<NoteEvent>& voice_a_notes,
                                  const std::vector<NoteEvent>& voice_b_notes) {
  if (voice_a_notes.empty() && voice_b_notes.empty()) {
    return 0.0f;
  }
  if (voice_a_notes.empty() || voice_b_notes.empty()) {
    // One voice is silent -- fully independent rhythmically.
    return 1.0f;
  }

  // Collect all unique onset ticks across both voices.
  std::set<Tick> all_onsets;
  std::set<Tick> onsets_a;
  std::set<Tick> onsets_b;
  for (const auto& note : voice_a_notes) {
    all_onsets.insert(note.start_tick);
    onsets_a.insert(note.start_tick);
  }
  for (const auto& note : voice_b_notes) {
    all_onsets.insert(note.start_tick);
    onsets_b.insert(note.start_tick);
  }

  if (all_onsets.empty()) {
    return 0.0f;
  }

  // Count simultaneous onsets (onset in A that has a matching onset in B
  // within the tolerance window).
  uint32_t simultaneous_count = 0;
  for (Tick onset_a : onsets_a) {
    for (Tick onset_b : onsets_b) {
      Tick diff = (onset_a >= onset_b) ? (onset_a - onset_b) : (onset_b - onset_a);
      if (diff <= kSimultaneousTolerance) {
        ++simultaneous_count;
        break;  // Count each onset_a at most once.
      }
    }
  }

  auto total_unique = static_cast<float>(all_onsets.size());
  float ratio = static_cast<float>(simultaneous_count) / total_unique;
  return clamp01(1.0f - ratio);
}

// ---------------------------------------------------------------------------
// Contour independence
// ---------------------------------------------------------------------------

float calculateContourIndependence(const std::vector<NoteEvent>& voice_a_notes,
                                   const std::vector<NoteEvent>& voice_b_notes) {
  if (voice_a_notes.empty() || voice_b_notes.empty()) {
    return 0.0f;
  }

  // Determine the time span we need to analyze.
  Tick end_a = lastEndTick(voice_a_notes);
  Tick end_b = lastEndTick(voice_b_notes);
  Tick end_tick = std::max(end_a, end_b);

  if (end_tick < kTicksPerBeat) {
    // Not enough material for a single beat comparison.
    return 0.0f;
  }

  // Walk beat-by-beat, comparing directions.
  int total_comparisons = 0;
  int opposite_count = 0;

  int prev_pitch_a = -1;
  int prev_pitch_b = -1;

  for (Tick beat = 0; beat < end_tick; beat += kTicksPerBeat) {
    int curr_pitch_a = pitchAtTick(voice_a_notes, beat);
    int curr_pitch_b = pitchAtTick(voice_b_notes, beat);

    // Skip if either voice has no note yet.
    if (curr_pitch_a < 0 || curr_pitch_b < 0) {
      prev_pitch_a = curr_pitch_a;
      prev_pitch_b = curr_pitch_b;
      continue;
    }
    // Need a previous point to compute direction.
    if (prev_pitch_a < 0 || prev_pitch_b < 0) {
      prev_pitch_a = curr_pitch_a;
      prev_pitch_b = curr_pitch_b;
      continue;
    }

    int dir_a = melodicDirection(prev_pitch_a, curr_pitch_a);
    int dir_b = melodicDirection(prev_pitch_b, curr_pitch_b);

    // Only count when both voices actually move (skip if either is "same").
    if (dir_a != 0 && dir_b != 0) {
      ++total_comparisons;
      if (dir_a != dir_b) {
        ++opposite_count;
      }
    }

    prev_pitch_a = curr_pitch_a;
    prev_pitch_b = curr_pitch_b;
  }

  if (total_comparisons == 0) {
    return 0.0f;
  }
  return clamp01(static_cast<float>(opposite_count) /
                 static_cast<float>(total_comparisons));
}

// ---------------------------------------------------------------------------
// Register separation
// ---------------------------------------------------------------------------

float calculateRegisterSeparation(const std::vector<NoteEvent>& voice_a_notes,
                                  const std::vector<NoteEvent>& voice_b_notes) {
  if (voice_a_notes.empty() || voice_b_notes.empty()) {
    return 0.0f;
  }

  // Find min/max pitch for each voice.
  uint8_t min_a = 127, max_a = 0;
  for (const auto& note : voice_a_notes) {
    if (note.pitch < min_a) min_a = note.pitch;
    if (note.pitch > max_a) max_a = note.pitch;
  }

  uint8_t min_b = 127, max_b = 0;
  for (const auto& note : voice_b_notes) {
    if (note.pitch < min_b) min_b = note.pitch;
    if (note.pitch > max_b) max_b = note.pitch;
  }

  // Compute overlap in semitones.
  // Overlap region: [max(min_a, min_b), min(max_a, max_b)]
  int overlap_low = std::max(static_cast<int>(min_a), static_cast<int>(min_b));
  int overlap_high = std::min(static_cast<int>(max_a), static_cast<int>(max_b));
  int overlap = std::max(0, overlap_high - overlap_low);

  // Denominator: the wider of the two ranges (at least 1 to avoid div-by-zero).
  int range_a = static_cast<int>(max_a) - static_cast<int>(min_a);
  int range_b = static_cast<int>(max_b) - static_cast<int>(min_b);
  int max_range = std::max({range_a, range_b, 1});

  float ratio = static_cast<float>(overlap) / static_cast<float>(max_range);
  return clamp01(1.0f - ratio);
}

// ---------------------------------------------------------------------------
// Pair analysis
// ---------------------------------------------------------------------------

VoiceIndependenceScore analyzeVoicePair(const std::vector<NoteEvent>& notes,
                                        VoiceId voice_a, VoiceId voice_b) {
  auto notes_a = filterAndSort(notes, voice_a);
  auto notes_b = filterAndSort(notes, voice_b);

  VoiceIndependenceScore score;
  score.rhythm_independence = calculateRhythmIndependence(notes_a, notes_b);
  score.contour_independence = calculateContourIndependence(notes_a, notes_b);
  score.register_separation = calculateRegisterSeparation(notes_a, notes_b);
  return score;
}

// ---------------------------------------------------------------------------
// Overall analysis (minimum across all pairs)
// ---------------------------------------------------------------------------

VoiceIndependenceScore analyzeOverall(const std::vector<NoteEvent>& notes,
                                      uint8_t num_voices) {
  if (num_voices < 2) {
    return {};
  }

  VoiceIndependenceScore min_score;
  min_score.rhythm_independence = 1.0f;
  min_score.contour_independence = 1.0f;
  min_score.register_separation = 1.0f;

  bool any_pair = false;

  for (uint8_t idx = 0; idx < num_voices; ++idx) {
    for (uint8_t jdx = idx + 1; jdx < num_voices; ++jdx) {
      auto pair_score = analyzeVoicePair(notes, idx, jdx);
      any_pair = true;
      min_score.rhythm_independence =
          std::min(min_score.rhythm_independence, pair_score.rhythm_independence);
      min_score.contour_independence =
          std::min(min_score.contour_independence, pair_score.contour_independence);
      min_score.register_separation =
          std::min(min_score.register_separation, pair_score.register_separation);
    }
  }

  if (!any_pair) {
    return {};
  }
  return min_score;
}

}  // namespace bach
