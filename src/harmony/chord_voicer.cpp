// Chord voicing and voice leading implementation.

#include "harmony/chord_voicer.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdlib>
#include <vector>

#include "core/interval.h"
#include "core/pitch_utils.h"
#include "harmony/chord_types.h"

namespace bach {
namespace {

// Seventh intervals by chord quality. Returns -1 if no seventh.
int getSeventhInterval(ChordQuality quality) {
  switch (quality) {
    case ChordQuality::Dominant7:
    case ChordQuality::Minor7:
    case ChordQuality::HalfDiminished7:
      return 10;
    case ChordQuality::MajorMajor7:
      return 11;
    case ChordQuality::Diminished7:
      return 9;
    case ChordQuality::AugSixthFrench:
    case ChordQuality::AugSixthGerman:
      return 8;
    default:
      return -1;
  }
}

// Get third interval in semitones for a chord quality.
int getThirdInterval(ChordQuality quality) {
  switch (quality) {
    case ChordQuality::Minor:
    case ChordQuality::Minor7:
    case ChordQuality::Diminished:
    case ChordQuality::Diminished7:
    case ChordQuality::HalfDiminished7:
      return 3;
    default:
      return 4;
  }
}

// Get fifth interval in semitones for a chord quality.
int getFifthInterval(ChordQuality quality) {
  switch (quality) {
    case ChordQuality::Diminished:
    case ChordQuality::Diminished7:
    case ChordQuality::HalfDiminished7:
    case ChordQuality::AugSixthItalian:
    case ChordQuality::AugSixthFrench:
      return 6;
    case ChordQuality::Augmented:
    case ChordQuality::AugmentedSixth:
    case ChordQuality::AugSixthGerman:
      return 8;
    default:
      return 7;
  }
}

// Check if a pitch class is the leading tone (major 7th scale degree) of the key.
bool isLeadingTone(int pitch_class, Key key, bool is_minor) {
  int tonic_pc = static_cast<int>(key) % 12;
  int leading_pc = (tonic_pc + 11) % 12;
  return pitch_class == leading_pc;
}

// Check if two voices form parallel fifths or octaves.
// Returns true only when both voices move in the same direction.
bool hasParallelPerfect(int prev_upper, int prev_lower,
                        int curr_upper, int curr_lower) {
  int prev_iv = interval_util::compoundToSimple(std::abs(prev_upper - prev_lower));
  int curr_iv = interval_util::compoundToSimple(std::abs(curr_upper - curr_lower));

  // Check parallel P5 or P8.
  bool is_target = (prev_iv == 7 && curr_iv == 7) || (prev_iv == 0 && curr_iv == 0);
  if (!is_target) return false;

  int motion_upper = curr_upper - prev_upper;
  int motion_lower = curr_lower - prev_lower;

  // Both must move in the same direction (both positive or both negative).
  if (motion_upper == 0 || motion_lower == 0) return false;
  return (motion_upper > 0 && motion_lower > 0) ||
         (motion_upper < 0 && motion_lower < 0);
}

// Find the best octave placement of a pitch class within a range, nearest to target.
int bestOctavePlacement(int pitch_class, int target, int low, int high) {
  int best = -1;
  int best_dist = 999;
  for (int octave = 0; octave <= 10; ++octave) {
    int candidate = octave * 12 + pitch_class;
    if (candidate < low || candidate > high) continue;
    int dist = std::abs(candidate - target);
    if (dist < best_dist) {
      best_dist = dist;
      best = candidate;
    }
  }
  return best;
}

}  // namespace

std::vector<int> getChordPitchClasses(ChordQuality quality, int root_pc) {
  int third = getThirdInterval(quality);
  int fifth = getFifthInterval(quality);
  int seventh = getSeventhInterval(quality);

  std::vector<int> pcs = {
      root_pc % 12, (root_pc + third) % 12, (root_pc + fifth) % 12};
  if (seventh >= 0) {
    pcs.push_back((root_pc + seventh) % 12);
  }
  return pcs;
}

ChordVoicing voiceChord(const HarmonicEvent& event, uint8_t num_voices,
                        VoiceRangeFn voice_range) {
  assert(num_voices >= 2 && num_voices <= kMaxVoicingVoices);

  ChordVoicing result;
  result.num_voices = num_voices;

  int root_pc = getPitchClass(event.chord.root_pitch);
  auto chord_pcs = getChordPitchClasses(event.chord.quality, root_pc);
  int third_pc = chord_pcs[1];
  int fifth_pc = chord_pcs[2];

  // Determine which pitch classes should NOT be doubled.
  bool has_seventh = (chord_pcs.size() >= 4);
  bool is_diminished_fifth = (getFifthInterval(event.chord.quality) == 6);
  bool leading_tone_present =
      isLeadingTone(third_pc, event.key, event.is_minor) ||
      isLeadingTone(fifth_pc, event.key, event.is_minor) ||
      isLeadingTone(root_pc, event.key, event.is_minor);

  // --- Step 1: Place bass voice ---
  uint8_t bass_idx = num_voices - 1;
  auto [bass_low, bass_high] = voice_range(bass_idx);
  int bass_pitch;
  if (event.bass_pitch > 0) {
    bass_pitch = bestOctavePlacement(
        getPitchClass(event.bass_pitch),
        (static_cast<int>(bass_low) + static_cast<int>(bass_high)) / 2,
        bass_low, bass_high);
  } else {
    bass_pitch = bestOctavePlacement(
        root_pc,
        (static_cast<int>(bass_low) + static_cast<int>(bass_high)) / 2,
        bass_low, bass_high);
  }
  if (bass_pitch < 0) bass_pitch = clampPitch(event.bass_pitch, bass_low, bass_high);
  result.pitches[bass_idx] = static_cast<uint8_t>(bass_pitch);

  // --- Step 2: Place soprano (voice 0) ---
  auto [sop_low, sop_high] = voice_range(0);
  int sop_center = (static_cast<int>(sop_low) + static_cast<int>(sop_high)) / 2;
  // Pick the chord tone nearest to the center of soprano range.
  int best_sop = -1;
  int best_sop_dist = 999;
  for (int pc : chord_pcs) {
    int placed = bestOctavePlacement(pc, sop_center, sop_low, sop_high);
    if (placed < 0) continue;
    int dist = std::abs(placed - sop_center);
    if (dist < best_sop_dist) {
      best_sop_dist = dist;
      best_sop = placed;
    }
  }
  if (best_sop < 0) best_sop = clampPitch(sop_center, sop_low, sop_high);
  result.pitches[0] = static_cast<uint8_t>(best_sop);

  // --- Step 3: Distribute inner voices ---
  // Build a list of pitch classes to assign, respecting doubling rules.
  // For a triad with 3 voices, all 3 chord tones are covered.
  // For 4+ voices with a triad, double root first, then fifth.
  // For 7th chords, all 4 tones plus root doubling if needed.

  int bass_pc = getPitchClass(static_cast<uint8_t>(bass_pitch));
  int sop_pc = getPitchClass(result.pitches[0]);

  // Track which pitch classes are already used.
  std::vector<int> assigned_pcs = {sop_pc, bass_pc};

  // Determine which chord tones still need assignment.
  std::vector<int> needed_pcs;
  for (int pc : chord_pcs) {
    bool already_assigned = false;
    for (int a : assigned_pcs) {
      if (a == pc) {
        already_assigned = true;
        break;
      }
    }
    if (!already_assigned) {
      needed_pcs.push_back(pc);
    }
  }

  // If we need more voices than remaining unique chord tones, add doublings.
  int inner_count = static_cast<int>(num_voices) - 2;  // minus soprano and bass
  while (static_cast<int>(needed_pcs.size()) < inner_count) {
    // Doubling priority: root > fifth > third.
    // Never double: leading tone, seventh, diminished fifth.
    bool added = false;
    // Try root.
    if (!isLeadingTone(root_pc, event.key, event.is_minor)) {
      needed_pcs.push_back(root_pc);
      added = true;
    }
    if (!added && !is_diminished_fifth &&
        !isLeadingTone(fifth_pc, event.key, event.is_minor)) {
      needed_pcs.push_back(fifth_pc);
      added = true;
    }
    if (!added) {
      // Fallback: double root regardless (shouldn't normally happen).
      needed_pcs.push_back(root_pc);
    }
    if (static_cast<int>(needed_pcs.size()) >= inner_count) break;
  }

  // Assign inner voices (voices 1 to num_voices-2).
  for (int i = 0; i < inner_count; ++i) {
    uint8_t vid = static_cast<uint8_t>(i + 1);
    auto [v_low, v_high] = voice_range(vid);

    int pc_to_place = (i < static_cast<int>(needed_pcs.size())) ? needed_pcs[i] : root_pc;

    // Target: interpolate between soprano and bass for this voice position.
    float frac =
        static_cast<float>(vid) / static_cast<float>(num_voices - 1);
    int target = static_cast<int>(best_sop * (1.0f - frac) + bass_pitch * frac);

    int placed = bestOctavePlacement(pc_to_place, target, v_low, v_high);
    if (placed < 0) placed = clampPitch(target, v_low, v_high);
    result.pitches[vid] = static_cast<uint8_t>(placed);
  }

  // --- Step 4: Enforce no voice crossing (descending order) ---
  for (uint8_t i = 0; i + 1 < num_voices; ++i) {
    if (result.pitches[i] < result.pitches[i + 1]) {
      // Swap to maintain descending order.
      std::swap(result.pitches[i], result.pitches[i + 1]);
    }
  }
  // Bubble sort to ensure full descending order.
  for (uint8_t pass = 0; pass < num_voices; ++pass) {
    for (uint8_t i = 0; i + 1 < num_voices; ++i) {
      if (result.pitches[i] < result.pitches[i + 1]) {
        std::swap(result.pitches[i], result.pitches[i + 1]);
      }
    }
  }

  return result;
}

ChordVoicing smoothVoiceLeading(const ChordVoicing& prev,
                                const HarmonicEvent& next_event,
                                uint8_t num_voices,
                                VoiceRangeFn voice_range) {
  assert(num_voices >= 2 && num_voices <= kMaxVoicingVoices);
  assert(prev.num_voices == num_voices);

  int root_pc = getPitchClass(next_event.chord.root_pitch);
  auto chord_pcs = getChordPitchClasses(next_event.chord.quality, root_pc);
  int tonic_pc = static_cast<int>(next_event.key) % 12;
  int leading_pc = (tonic_pc + 11) % 12;
  int seventh_interval = getSeventhInterval(next_event.chord.quality);

  // Compute previous chord's pitch classes for resolution detection.
  // Check if previous chord was dominant (V or V7).
  // We detect dominant by checking if prev bass was a 5th above tonic.

  ChordVoicing result;
  result.num_voices = num_voices;

  // --- Step 1: Bass voice moves to nearest bass_pitch placement ---
  uint8_t bass_idx = num_voices - 1;
  auto [bass_low, bass_high] = voice_range(bass_idx);
  int bass_pc = (next_event.bass_pitch > 0)
                    ? getPitchClass(next_event.bass_pitch)
                    : root_pc;
  int bass_target = bestOctavePlacement(
      bass_pc, static_cast<int>(prev.pitches[bass_idx]), bass_low, bass_high);
  if (bass_target < 0) {
    bass_target = clampPitch(static_cast<int>(prev.pitches[bass_idx]),
                             bass_low, bass_high);
  }
  result.pitches[bass_idx] = static_cast<uint8_t>(bass_target);

  // --- Step 2: Each upper voice moves to nearest chord tone ---
  // First pass: greedy nearest assignment.
  std::vector<bool> pc_used(chord_pcs.size(), false);

  for (uint8_t v = 0; v < num_voices - 1; ++v) {
    auto [v_low, v_high] = voice_range(v);
    int prev_pitch = static_cast<int>(prev.pitches[v]);

    // Special resolution: leading tone → tonic (up by half step).
    int prev_pc = getPitchClass(prev.pitches[v]);
    if (prev_pc == leading_pc && !next_event.is_minor) {
      // Leading tone resolves up to tonic.
      int resolved = prev_pitch + 1;
      if (getPitchClassSigned(resolved) == tonic_pc &&
          resolved >= v_low && resolved <= v_high) {
        result.pitches[v] = static_cast<uint8_t>(resolved);
        // Mark tonic as used if it's in chord_pcs.
        for (size_t k = 0; k < chord_pcs.size(); ++k) {
          if (chord_pcs[k] == tonic_pc && !pc_used[k]) {
            pc_used[k] = true;
            break;
          }
        }
        continue;
      }
    }

    // Special resolution: seventh of V7 → resolves down to third of I.
    if (seventh_interval < 0) {
      // Check if previous note was a seventh that should resolve down.
      // The previous chord's seventh resolves down by step in the new chord.
      // This is handled by nearest-chord-tone below which naturally picks
      // the closest pitch, usually stepwise down.
    }

    // Find nearest chord tone.
    int best_pitch = -1;
    int best_dist = 999;
    int best_pc_idx = -1;

    for (size_t k = 0; k < chord_pcs.size(); ++k) {
      int placed = bestOctavePlacement(chord_pcs[k], prev_pitch, v_low, v_high);
      if (placed < 0) continue;
      int dist = std::abs(placed - prev_pitch);
      if (dist < best_dist) {
        best_dist = dist;
        best_pitch = placed;
        best_pc_idx = static_cast<int>(k);
      }
    }

    if (best_pitch < 0) {
      best_pitch = clampPitch(prev_pitch, v_low, v_high);
    }
    result.pitches[v] = static_cast<uint8_t>(best_pitch);
    if (best_pc_idx >= 0) {
      pc_used[best_pc_idx] = true;
    }
  }

  // --- Step 3: Check and fix parallel P5/P8 ---
  // For each pair of voices, if parallel perfect interval exists, nudge one voice.
  for (uint8_t i = 0; i < num_voices; ++i) {
    for (uint8_t j = i + 1; j < num_voices; ++j) {
      if (hasParallelPerfect(prev.pitches[i], prev.pitches[j],
                             result.pitches[i], result.pitches[j])) {
        // Fix by moving voice i to the next nearest chord tone.
        if (i < num_voices - 1) {  // Don't move bass.
          auto [v_low, v_high] = voice_range(i);
          int curr = static_cast<int>(result.pitches[i]);
          int prev_pitch = static_cast<int>(prev.pitches[i]);

          // Try moving in the opposite direction.
          for (int offset = 1; offset <= 12; ++offset) {
            for (int dir : {1, -1}) {
              int candidate = curr + dir * offset;
              if (candidate < v_low || candidate > v_high) continue;

              // Must be a chord tone.
              int cand_pc = getPitchClassSigned(candidate);
              bool is_chord = false;
              for (int pc : chord_pcs) {
                if (cand_pc == pc) {
                  is_chord = true;
                  break;
                }
              }
              if (!is_chord) continue;

              // Must not create new parallel perfect.
              bool creates_new = false;
              for (uint8_t k = 0; k < num_voices; ++k) {
                if (k == i) continue;
                if (hasParallelPerfect(prev.pitches[i], prev.pitches[k],
                                       candidate, result.pitches[k])) {
                  creates_new = true;
                  break;
                }
              }
              if (!creates_new) {
                result.pitches[i] = static_cast<uint8_t>(candidate);
                goto parallel_fixed;
              }
            }
          }
        parallel_fixed:;
        }
      }
    }
  }

  // --- Step 4: Enforce no voice crossing ---
  for (uint8_t pass = 0; pass < num_voices; ++pass) {
    for (uint8_t i = 0; i + 1 < num_voices; ++i) {
      if (result.pitches[i] < result.pitches[i + 1]) {
        std::swap(result.pitches[i], result.pitches[i + 1]);
      }
    }
  }

  return result;
}

}  // namespace bach
