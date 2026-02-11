/// @file
/// @brief Interval classification, naming, and inversion utilities.

#include "core/interval.h"

#include <cstdlib>

namespace bach {
namespace interval_util {

/// @brief Human-readable interval names indexed by simple interval (0-11 semitones).
static constexpr const char* kIntervalNames[12] = {
    "Perfect Unison",  // 0
    "Minor 2nd",       // 1
    "Major 2nd",       // 2
    "Minor 3rd",       // 3
    "Major 3rd",       // 4
    "Perfect 4th",     // 5
    "Tritone",         // 6
    "Perfect 5th",     // 7
    "Minor 6th",       // 8
    "Major 6th",       // 9
    "Minor 7th",       // 10
    "Major 7th"        // 11
};

int compoundToSimple(int semitones) {
  int abs_val = std::abs(semitones);
  return abs_val % 12;
}

const char* intervalName(int semitones) {
  int simple = compoundToSimple(semitones);
  if (simple < 0 || simple > 11) {
    return "Unknown";
  }
  return kIntervalNames[simple];
}

bool isPerfectInterval(int semitones) {
  int simple = compoundToSimple(semitones);
  return simple == interval::kUnison ||
         simple == interval::kPerfect4th ||
         simple == interval::kPerfect5th;
  // Octave reduces to 0 (unison) via mod 12, so it is covered.
}

bool isConsonance(int semitones) {
  IntervalQuality quality = classifyInterval(semitones);
  return quality == IntervalQuality::PerfectConsonance ||
         quality == IntervalQuality::ImperfectConsonance;
}

bool isPerfectConsonance(int semitones) {
  int simple = compoundToSimple(semitones);
  return simple == 0 || simple == 7;
}

int invertInterval(int semitones) {
  int simple = compoundToSimple(semitones);
  if (simple == 0) {
    return 0;  // Unison inverts to unison (or octave, but we keep simple)
  }
  return 12 - simple;
}

}  // namespace interval_util
}  // namespace bach
