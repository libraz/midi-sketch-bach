// Chord tone snapping utilities for harmonic validation.

#include "harmony/chord_tone_utils.h"

#include <cstdlib>

#include "core/basic_types.h"
#include "core/pitch_utils.h"
#include "harmony/chord_types.h"

namespace bach {

uint8_t nearestChordTone(uint8_t pitch, const HarmonicEvent& event) {
  int root_pc = getPitchClass(event.chord.root_pitch);

  // Determine chord intervals based on quality.
  int third_interval = 4;  // Major 3rd default
  int fifth_interval = 7;  // Perfect 5th default

  switch (event.chord.quality) {
    case ChordQuality::Minor:
    case ChordQuality::Minor7:
    case ChordQuality::Diminished:
    case ChordQuality::Diminished7:
    case ChordQuality::HalfDiminished7:
      third_interval = 3;
      break;
    default:
      third_interval = 4;
      break;
  }

  switch (event.chord.quality) {
    case ChordQuality::Diminished:
    case ChordQuality::Diminished7:
    case ChordQuality::HalfDiminished7:
      fifth_interval = 6;
      break;
    case ChordQuality::Augmented:
      fifth_interval = 8;
      break;
    default:
      fifth_interval = 7;
      break;
  }

  // Chord pitch classes.
  int chord_pcs[3] = {
      root_pc,
      (root_pc + third_interval) % 12,
      (root_pc + fifth_interval) % 12};

  // Find closest chord tone across same and adjacent octaves.
  int best_pitch = pitch;
  int best_dist = 127;

  for (int pc : chord_pcs) {
    // Try candidate in same octave as pitch, and +/- 1 octave.
    int base_octave = (static_cast<int>(pitch) / 12) * 12;
    for (int oct_offset = -12; oct_offset <= 12; oct_offset += 12) {
      int candidate = base_octave + pc + oct_offset;
      if (candidate < 0 || candidate > 127) continue;
      int dist = std::abs(candidate - static_cast<int>(pitch));
      if (dist < best_dist) {
        best_dist = dist;
        best_pitch = candidate;
      }
    }
  }

  return static_cast<uint8_t>(best_pitch);
}

uint8_t nearestChordTone(uint8_t target,
                         const std::vector<uint8_t>& chord_pitches) {
  if (chord_pitches.empty()) {
    return target;
  }

  uint8_t best = chord_pitches[0];
  int best_dist = absoluteInterval(target, best);

  for (size_t idx = 1; idx < chord_pitches.size(); ++idx) {
    int dist = absoluteInterval(target, chord_pitches[idx]);
    if (dist < best_dist) {
      best_dist = dist;
      best = chord_pitches[idx];
    }
  }

  return best;
}

}  // namespace bach
