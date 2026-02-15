// Implementation of subject identity: 3-layer Kerngestalt preservation.

#include "fugue/subject_identity.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <set>

#include "core/interval.h"
#include "core/pitch_utils.h"

namespace bach {

// ---------------------------------------------------------------------------
// KerngestaltType string conversion
// ---------------------------------------------------------------------------

const char* kerngestaltTypeToString(KerngestaltType type) {
  switch (type) {
    case KerngestaltType::IntervalDriven: return "interval_driven";
    case KerngestaltType::ChromaticCell:  return "chromatic_cell";
    case KerngestaltType::Arpeggio:       return "arpeggio";
    case KerngestaltType::Linear:         return "linear";
  }
  return "unknown";  // NOLINT(clang-diagnostic-covered-switch-default): defensive
}

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

namespace {

/// @brief Check if an absolute interval is a chord-tone interval.
/// Chord-tone intervals: minor 3rd (3), major 3rd (4), perfect 4th (5),
/// perfect 5th (7), minor 6th (8), major 6th (9).
bool isChordToneInterval(int abs_interval) {
  return abs_interval == 3 || abs_interval == 4 || abs_interval == 5 ||
         abs_interval == 7 || abs_interval == 8 || abs_interval == 9;
}

/// @brief Check the Arpeggio condition: consecutive same-direction 3rd-class
/// intervals (abs 3 or 4) >= 2, and at least 2 distinct triad pitch classes.
bool isArpeggioPattern(const std::vector<int>& intervals) {
  // Look for consecutive same-direction intervals with abs 3 or 4.
  int consecutive_count = 0;
  int max_consecutive = 0;
  bool has_distinct_triads = false;

  for (size_t idx = 0; idx + 1 < intervals.size(); ++idx) {
    int abs_cur = std::abs(intervals[idx]);
    int abs_nxt = std::abs(intervals[idx + 1]);
    bool cur_is_third = (abs_cur == 3 || abs_cur == 4);
    bool nxt_is_third = (abs_nxt == 3 || abs_nxt == 4);
    bool same_direction = (intervals[idx] > 0 && intervals[idx + 1] > 0) ||
                          (intervals[idx] < 0 && intervals[idx + 1] < 0);

    if (cur_is_third && nxt_is_third && same_direction) {
      if (consecutive_count == 0) {
        consecutive_count = 2;
      } else {
        ++consecutive_count;
      }
      max_consecutive = std::max(max_consecutive, consecutive_count);

      // Check for distinct pitch class types: the two intervals traverse
      // different amounts (e.g., m3+M3 = root->third->fifth).
      if (abs_cur != abs_nxt || consecutive_count >= 2) {
        has_distinct_triads = true;
      }
    } else {
      consecutive_count = 0;
    }
  }

  return max_consecutive >= 2 && has_distinct_triads;
}

/// @brief Classify the Kerngestalt type based on interval analysis.
///
/// Classification priority: Arpeggio > IntervalDriven > ChromaticCell > Linear.
///   - Arpeggio: consecutive same-direction 3rd-class intervals >= 2, with
///     at least 2 distinct triad pitch classes.
///   - IntervalDriven: signature interval abs >= 3 and has a leap.
///   - ChromaticCell: 2+ semitone motions (abs==1) in first 4 intervals.
///   - Linear: default (scale-based with internal repetition).
KerngestaltType classifyKerngestalt(const std::vector<int>& intervals,
                                    int signature_interval) {
  // Check Arpeggio first (highest priority).
  if (isArpeggioPattern(intervals)) {
    return KerngestaltType::Arpeggio;
  }

  // Check IntervalDriven: abs(signature_interval) >= 3 and has a leap.
  bool has_leap = false;
  int chromatic_in_head_count = 0;

  for (size_t idx = 0; idx < intervals.size(); ++idx) {
    int abs_val = std::abs(intervals[idx]);

    if (abs_val >= 3) {
      has_leap = true;
    }

    // Count chromatic motion (abs==1) in the first 4 intervals.
    if (idx < 4 && abs_val == 1) {
      ++chromatic_in_head_count;
    }
  }

  if (std::abs(signature_interval) >= 3 && has_leap) {
    return KerngestaltType::IntervalDriven;
  }

  // ChromaticCell requires 2+ semitone motions in head (first 4 intervals).
  if (chromatic_in_head_count >= 2) {
    return KerngestaltType::ChromaticCell;
  }

  // Default: Linear (scale-based with internal repetition).
  return KerngestaltType::Linear;
}

/// @brief Find the index of the highest pitch note (climax point).
size_t findClimaxIndex(const std::vector<NoteEvent>& notes) {
  if (notes.empty()) {
    return 0;
  }
  size_t max_idx = 0;
  uint8_t max_pitch = notes[0].pitch;
  for (size_t idx = 1; idx < notes.size(); ++idx) {
    if (notes[idx].pitch > max_pitch) {
      max_pitch = notes[idx].pitch;
      max_idx = idx;
    }
  }
  return max_idx;
}

/// @brief Check if the head fragment pattern recurs later in the core intervals.
bool headFragmentRecurs(const std::vector<int>& core_intervals,
                        const std::vector<Tick>& core_rhythm,
                        const std::vector<int>& head_intervals,
                        const std::vector<Tick>& head_rhythm) {
  if (head_intervals.empty() || core_intervals.empty()) {
    return false;
  }
  size_t head_len = head_intervals.size();
  if (head_len > core_intervals.size()) {
    return false;
  }

  // Search for the head pattern starting from index 1 onward.
  for (size_t start = 1; start + head_len <= core_intervals.size(); ++start) {
    bool match = true;
    for (size_t idx = 0; idx < head_len; ++idx) {
      if (core_intervals[start + idx] != head_intervals[idx]) {
        match = false;
        break;
      }
      // Also check rhythm match (need start+idx < core_rhythm.size()).
      if (start + idx < core_rhythm.size() && idx < head_rhythm.size() &&
          core_rhythm[start + idx] != head_rhythm[idx]) {
        match = false;
        break;
      }
    }
    if (match) {
      return true;
    }
  }
  return false;
}

// ---------------------------------------------------------------------------
// Helpers for isValidKerngestalt: interval, rhythm, and accent matching
// ---------------------------------------------------------------------------

/// @brief Duration class for rhythm matching (simplified categories).
enum class DurationClass : uint8_t {
  Short,   ///< <= 240 ticks (eighth note or shorter).
  Medium,  ///< 241-600 ticks (quarter note range).
  Long     ///< > 600 ticks (dotted quarter or longer).
};

/// @brief Classify a tick duration into a duration class.
DurationClass classifyDuration(Tick dur) {
  if (dur <= 240) return DurationClass::Short;
  if (dur <= 600) return DurationClass::Medium;
  return DurationClass::Long;
}

/// @brief Check if an interval sequence matches at a given offset in a haystack.
///
/// Supports exact match, inversion (negate all), and retrograde-inversion
/// (reverse + negate). For ChromaticCell type, allows +/-1 semitone tolerance
/// per interval.
///
/// @param haystack Full interval sequence to search within.
/// @param start Starting index in the haystack.
/// @param needle The interval pattern to find.
/// @param type Kerngestalt type (ChromaticCell gets tolerance).
/// @return True if a match is found at the given offset.
bool intervalSequenceMatchesAt(const std::vector<int>& haystack, size_t start,
                               const std::vector<int>& needle,
                               KerngestaltType type) {
  if (start + needle.size() > haystack.size()) {
    return false;
  }

  // Build transformation variants: original, inverted, retrograde-inverted.
  // Original intervals.
  std::vector<int> inverted(needle.size());
  std::vector<int> retro_inverted(needle.size());
  for (size_t idx = 0; idx < needle.size(); ++idx) {
    inverted[idx] = -needle[idx];
    retro_inverted[idx] = needle[needle.size() - 1 - idx];  // reverse, then negate below
  }
  // Retrograde-inversion = reverse + negate.
  for (size_t idx = 0; idx < needle.size(); ++idx) {
    retro_inverted[idx] = -retro_inverted[idx];
  }

  int tolerance = (type == KerngestaltType::ChromaticCell) ? 1 : 0;

  // Try each variant: original, inverted, retrograde-inverted.
  const std::vector<int>* variants[] = {&needle, &inverted, &retro_inverted};
  for (const auto* variant : variants) {
    bool match = true;
    for (size_t idx = 0; idx < variant->size(); ++idx) {
      int diff = std::abs(haystack[start + idx] - (*variant)[idx]);
      if (diff > tolerance) {
        match = false;
        break;
      }
    }
    if (match) {
      return true;
    }
  }
  return false;
}

/// @brief Search for an interval sequence match within a haystack range.
///
/// @param haystack Full interval sequence.
/// @param search_start First index to start searching.
/// @param search_end One-past-last index for search start positions.
/// @param needle The interval pattern to find.
/// @param type Kerngestalt type for match rules.
/// @return True if the needle (or a valid transformation) is found.
bool intervalSequenceMatches(const std::vector<int>& haystack,
                             size_t search_start, size_t search_end,
                             const std::vector<int>& needle,
                             KerngestaltType type) {
  if (needle.empty() || haystack.empty()) {
    return false;
  }
  if (search_end > haystack.size()) {
    search_end = haystack.size();
  }
  for (size_t pos = search_start; pos + needle.size() <= search_end; ++pos) {
    if (intervalSequenceMatchesAt(haystack, pos, needle, type)) {
      return true;
    }
  }
  return false;
}

/// @brief Check if a rhythm sequence matches at a given offset (edit distance <= 1).
///
/// Converts durations to DurationClass and checks if the sequences differ
/// by at most 1 element (minimum edit distance <= 1 using simple substitution).
///
/// @param haystack_rhythm Full rhythm sequence.
/// @param start Starting index in the haystack.
/// @param needle_rhythm The rhythm pattern to find.
/// @return True if the duration-class sequences match within edit distance 1.
bool rhythmSequenceMatchesAt(const std::vector<Tick>& haystack_rhythm, size_t start,
                              const std::vector<Tick>& needle_rhythm) {
  if (start + needle_rhythm.size() > haystack_rhythm.size()) {
    return false;
  }

  int mismatches = 0;
  for (size_t idx = 0; idx < needle_rhythm.size(); ++idx) {
    if (classifyDuration(haystack_rhythm[start + idx]) !=
        classifyDuration(needle_rhythm[idx])) {
      ++mismatches;
      if (mismatches > 1) {
        return false;
      }
    }
  }
  return true;
}

/// @brief Search for a rhythm sequence match within a haystack range.
bool rhythmSequenceMatches(const std::vector<Tick>& haystack_rhythm,
                            size_t search_start, size_t search_end,
                            const std::vector<Tick>& needle_rhythm) {
  if (needle_rhythm.empty() || haystack_rhythm.empty()) {
    return false;
  }
  if (search_end > haystack_rhythm.size()) {
    search_end = haystack_rhythm.size();
  }
  for (size_t pos = search_start; pos + needle_rhythm.size() <= search_end; ++pos) {
    if (rhythmSequenceMatchesAt(haystack_rhythm, pos, needle_rhythm)) {
      return true;
    }
  }
  return false;
}

/// @brief Check if an accent sequence matches at a given offset (exact S/W pattern).
bool accentSequenceMatchesAt(const std::vector<AccentPosition>& haystack, size_t start,
                              const std::vector<AccentPosition>& needle) {
  if (start + needle.size() > haystack.size()) {
    return false;
  }
  for (size_t idx = 0; idx < needle.size(); ++idx) {
    if (haystack[start + idx] != needle[idx]) {
      return false;
    }
  }
  return true;
}

/// @brief Search for an accent sequence match within a haystack range.
bool accentSequenceMatches(const std::vector<AccentPosition>& haystack,
                            size_t search_start, size_t search_end,
                            const std::vector<AccentPosition>& needle) {
  if (needle.empty() || haystack.empty()) {
    return false;
  }
  if (search_end > haystack.size()) {
    search_end = haystack.size();
  }
  for (size_t pos = search_start; pos + needle.size() <= search_end; ++pos) {
    if (accentSequenceMatchesAt(haystack, pos, needle)) {
      return true;
    }
  }
  return false;
}

}  // namespace

// ---------------------------------------------------------------------------
// buildEssentialIdentity
// ---------------------------------------------------------------------------

EssentialIdentity buildEssentialIdentity(const std::vector<NoteEvent>& notes,
                                         Key /*key*/, bool /*is_minor*/) {
  EssentialIdentity identity;

  if (notes.size() < 2) {
    return identity;
  }

  // 1. Core intervals: directed (signed) intervals between consecutive notes.
  identity.core_intervals.reserve(notes.size() - 1);
  for (size_t idx = 0; idx + 1 < notes.size(); ++idx) {
    int directed = static_cast<int>(notes[idx + 1].pitch) -
                   static_cast<int>(notes[idx].pitch);
    identity.core_intervals.push_back(directed);
  }

  // 2. Core rhythm: duration of each note.
  identity.core_rhythm.reserve(notes.size());
  for (const auto& note : notes) {
    identity.core_rhythm.push_back(note.duration);
  }

  // 3. Accent pattern: Strong if note starts on a beat, Weak otherwise.
  identity.accent_pattern.reserve(notes.size());
  for (const auto& note : notes) {
    if (note.start_tick % kTicksPerBeat == 0) {
      identity.accent_pattern.push_back(AccentPosition::Strong);
    } else {
      identity.accent_pattern.push_back(AccentPosition::Weak);
    }
  }

  // 4. Signature interval: largest absolute interval that is >= 3 semitones.
  //    Falls back to the first non-zero interval if none qualify.
  identity.signature_interval = 0;
  int max_abs = 0;
  for (int interval : identity.core_intervals) {
    int abs_val = std::abs(interval);
    if (abs_val >= 3 && abs_val > max_abs) {
      max_abs = abs_val;
      identity.signature_interval = interval;
    }
  }
  // Fallback: use first non-zero interval.
  if (identity.signature_interval == 0) {
    for (int interval : identity.core_intervals) {
      if (interval != 0) {
        identity.signature_interval = interval;
        break;
      }
    }
  }

  // 5. Head fragment: first min(3, size) intervals and durations.
  size_t head_count = std::min(static_cast<size_t>(3), identity.core_intervals.size());
  identity.head_fragment_intervals.assign(identity.core_intervals.begin(),
                                           identity.core_intervals.begin() +
                                               static_cast<ptrdiff_t>(head_count));
  size_t head_rhythm_count = std::min(static_cast<size_t>(3), identity.core_rhythm.size());
  identity.head_fragment_rhythm.assign(identity.core_rhythm.begin(),
                                        identity.core_rhythm.begin() +
                                            static_cast<ptrdiff_t>(head_rhythm_count));

  // 6. Tail fragment: last min(3, size) intervals and durations.
  size_t tail_count = std::min(static_cast<size_t>(3), identity.core_intervals.size());
  identity.tail_fragment_intervals.assign(
      identity.core_intervals.end() - static_cast<ptrdiff_t>(tail_count),
      identity.core_intervals.end());
  size_t tail_rhythm_count = std::min(static_cast<size_t>(3), identity.core_rhythm.size());
  identity.tail_fragment_rhythm.assign(
      identity.core_rhythm.end() - static_cast<ptrdiff_t>(tail_rhythm_count),
      identity.core_rhythm.end());

  // 7. Natural break point: climax index + 1.
  size_t climax_idx = findClimaxIndex(notes);
  identity.natural_break_point = climax_idx + 1;
  // Clamp to valid range (at least 1, at most notes.size() - 1).
  if (identity.natural_break_point >= notes.size()) {
    identity.natural_break_point = notes.size() - 1;
  }
  if (identity.natural_break_point == 0) {
    identity.natural_break_point = 1;
  }

  // 8. Kerngestalt type classification.
  identity.kerngestalt_type = classifyKerngestalt(
      identity.core_intervals, identity.signature_interval);

  return identity;
}

// ---------------------------------------------------------------------------
// buildDerivedTransformations
// ---------------------------------------------------------------------------

DerivedTransformations buildDerivedTransformations(const EssentialIdentity& essential) {
  DerivedTransformations derived;

  if (!essential.isValid()) {
    return derived;
  }

  // 1. Inverted intervals: negate each directed interval.
  derived.inverted_intervals.reserve(essential.core_intervals.size());
  for (int interval : essential.core_intervals) {
    derived.inverted_intervals.push_back(-interval);
  }

  // 2. Inverted rhythm: identical to core rhythm (rhythm preserved under inversion).
  derived.inverted_rhythm = essential.core_rhythm;

  // 3. Retrograde intervals: reverse the sequence and negate each.
  derived.retrograde_intervals.reserve(essential.core_intervals.size());
  for (auto iter = essential.core_intervals.rbegin();
       iter != essential.core_intervals.rend(); ++iter) {
    derived.retrograde_intervals.push_back(-(*iter));
  }

  // 4. Retrograde rhythm: reverse the duration sequence.
  derived.retrograde_rhythm.assign(essential.core_rhythm.rbegin(),
                                    essential.core_rhythm.rend());

  // 5. Augmented rhythm: double each duration.
  derived.augmented_rhythm.reserve(essential.core_rhythm.size());
  for (Tick dur : essential.core_rhythm) {
    derived.augmented_rhythm.push_back(dur * 2);
  }

  // 6. Diminished rhythm: halve each duration, minimum 32nd note (60 ticks).
  constexpr Tick kMinDiminishedDuration = duration::kThirtySecondNote;  // 60 ticks
  derived.diminished_rhythm.reserve(essential.core_rhythm.size());
  for (Tick dur : essential.core_rhythm) {
    Tick halved = dur / 2;
    derived.diminished_rhythm.push_back(std::max(halved, kMinDiminishedDuration));
  }

  return derived;
}

// ---------------------------------------------------------------------------
// buildSubjectIdentity
// ---------------------------------------------------------------------------

SubjectIdentity buildSubjectIdentity(const std::vector<NoteEvent>& notes,
                                      Key key, bool is_minor) {
  SubjectIdentity identity;
  identity.essential = buildEssentialIdentity(notes, key, is_minor);
  identity.derived = buildDerivedTransformations(identity.essential);
  return identity;
}

// ---------------------------------------------------------------------------
// isValidKerngestalt
// ---------------------------------------------------------------------------

bool isValidKerngestalt(const EssentialIdentity& identity) {
  if (!identity.isValid()) {
    return false;
  }

  // Common check 1: at least one interval with abs >= 3 (not purely stepwise).
  bool has_non_step = false;
  for (int interval : identity.core_intervals) {
    if (std::abs(interval) >= 3) {
      has_non_step = true;
      break;
    }
  }
  if (!has_non_step) {
    return false;
  }

  // Common check 2: head_fragment_intervals must have at least 1 element.
  if (identity.head_fragment_intervals.empty()) {
    return false;
  }

  // Determine search range: from natural_break_point through end minus cadence
  // (last 2 notes excluded). Intervals are between consecutive notes, so
  // core_intervals has size = notes - 1. Excluding last 2 notes means
  // excluding last 2 intervals for interval search.
  size_t total_intervals = identity.core_intervals.size();
  size_t total_rhythm = identity.core_rhythm.size();
  size_t total_accents = identity.accent_pattern.size();

  size_t search_start = identity.natural_break_point;
  // Exclude last 2 notes from search: for intervals, that means last 2 intervals.
  size_t interval_search_end = (total_intervals > 2) ? total_intervals - 2 : 0;
  // For rhythm/accent, exclude last 2 notes.
  size_t rhythm_search_end = (total_rhythm > 2) ? total_rhythm - 2 : 0;
  size_t accent_search_end = (total_accents > 2) ? total_accents - 2 : 0;

  // If search range is too small, clamp start to allow some searching.
  if (search_start >= interval_search_end && total_intervals > 0) {
    search_start = (total_intervals > 1) ? 1 : 0;
  }

  // Extract head needle sequences.
  const auto& head_intervals = identity.head_fragment_intervals;
  const auto& head_rhythm = identity.head_fragment_rhythm;
  size_t head_accent_len = std::min(head_intervals.size() + 1, total_accents);
  std::vector<AccentPosition> head_accents(
      identity.accent_pattern.begin(),
      identity.accent_pattern.begin() + static_cast<ptrdiff_t>(head_accent_len));

  // Check the 3 signature conditions.
  bool interval_match = intervalSequenceMatches(
      identity.core_intervals, search_start, interval_search_end,
      head_intervals, identity.kerngestalt_type);

  bool rhythm_match = rhythmSequenceMatches(
      identity.core_rhythm, search_start, rhythm_search_end, head_rhythm);

  bool accent_match = accentSequenceMatches(
      identity.accent_pattern, search_start, accent_search_end, head_accents);

  // For subjects with >= 8 notes, require all 3 conditions (AND).
  // For subjects with < 8 notes, require at least 1 condition (OR).
  size_t note_count = identity.core_rhythm.size();
  bool recurrence_valid;
  if (note_count >= 8) {
    recurrence_valid = interval_match && rhythm_match && accent_match;
  } else {
    recurrence_valid = interval_match || rhythm_match || accent_match;
  }

  if (!recurrence_valid) {
    return false;
  }

  // Type-specific checks.
  switch (identity.kerngestalt_type) {
    case KerngestaltType::IntervalDriven:
      return std::abs(identity.signature_interval) >= 3;

    case KerngestaltType::ChromaticCell:
      // Require 2+ semitone motions in core.
      {
        int semitone_count = 0;
        for (int interval : identity.core_intervals) {
          if (std::abs(interval) == 1) {
            ++semitone_count;
          }
        }
        return semitone_count >= 2;
      }

    case KerngestaltType::Arpeggio: {
      int chord_tone_count = 0;
      for (int interval : identity.core_intervals) {
        if (isChordToneInterval(std::abs(interval))) {
          ++chord_tone_count;
        }
      }
      return chord_tone_count >= 2;
    }

    case KerngestaltType::Linear:
      // At least one pair of consecutive equal durations in core_rhythm.
      for (size_t idx = 0; idx + 1 < identity.core_rhythm.size(); ++idx) {
        if (identity.core_rhythm[idx] == identity.core_rhythm[idx + 1]) {
          return true;
        }
      }
      return false;
  }

  return false;  // NOLINT(clang-diagnostic-covered-switch-default): defensive
}

}  // namespace bach
