/// @file
/// @brief Implementation of Fux-style cantus firmus generation with arch-shaped melody.

#include "counterpoint/cantus_firmus.h"

#include <algorithm>
#include <cstdlib>
#include <random>

#include "core/pitch_utils.h"
#include "core/rng_util.h"

namespace bach {

// ---------------------------------------------------------------------------
// CantusFirmus member functions
// ---------------------------------------------------------------------------

size_t CantusFirmus::noteCount() const {
  return notes.size();
}

uint8_t CantusFirmus::lowestPitch() const {
  if (notes.empty()) return 127;
  uint8_t lowest = 127;
  for (const auto& note : notes) {
    if (note.pitch < lowest) lowest = note.pitch;
  }
  return lowest;
}

uint8_t CantusFirmus::highestPitch() const {
  if (notes.empty()) return 0;
  uint8_t highest = 0;
  for (const auto& note : notes) {
    if (note.pitch > highest) highest = note.pitch;
  }
  return highest;
}

// ---------------------------------------------------------------------------
// CantusFirmusGenerator
// ---------------------------------------------------------------------------

namespace {

/// @brief Probability of choosing a third interval instead of a second for melodic steps.
constexpr float kThirdProb = 0.2f;

}  // namespace

CantusFirmus CantusFirmusGenerator::generate(Key key, uint8_t length_bars,
                                             uint32_t seed) const {
  std::mt19937 gen(seed);

  // Clamp length to 4-8 bars.
  if (length_bars < 4) length_bars = 4;
  if (length_bars > 8) length_bars = 8;

  int key_offset = static_cast<int>(key);
  constexpr int kBaseNote = 60;  // C4

  // All notes are whole notes.
  constexpr Tick kCfDuration = kTicksPerBar;

  CantusFirmus result;
  result.key = key;

  // Strategy: generate internal notes, then bookend with tonic.
  // The climax (highest pitch) should appear exactly once, roughly in the
  // middle of the melody.

  int num_notes = static_cast<int>(length_bars);
  int climax_position = rng::rollRange(gen, num_notes / 3,
                                       (num_notes * 2) / 3);

  // Generate the melody as scale degrees.
  std::vector<int> degrees(num_notes, 0);

  // First note = tonic (degree 0).
  degrees[0] = 0;
  // Last note = tonic (degree 0).
  degrees[num_notes - 1] = 0;

  // Generate ascending phase (toward climax).
  int current_degree = 0;
  for (int idx = 1; idx < climax_position; ++idx) {
    // Prefer ascending motion toward the climax.
    bool use_third = rng::rollProbability(gen, kThirdProb);
    int step = 0;
    if (use_third) {
      step = rng::rollProbability(gen, 0.8f) ? 2 : 3;
    } else {
      step = rng::rollProbability(gen, 0.85f) ? 1 : 2;
    }
    current_degree += step;

    // Clamp to keep range within an octave (7 diatonic degrees).
    if (current_degree > 7) current_degree = 7;
    degrees[idx] = current_degree;
  }

  // Climax note: the highest point. Ensure it is higher than all neighbors.
  int climax_degree = current_degree;
  if (climax_position > 0 && climax_degree <= degrees[climax_position - 1]) {
    climax_degree = degrees[climax_position - 1] + 1;
  }
  if (climax_degree > 7) climax_degree = 7;
  degrees[climax_position] = climax_degree;

  // Generate descending phase (from climax to penultimate note).
  current_degree = climax_degree;
  for (int idx = climax_position + 1; idx < num_notes - 1; ++idx) {
    // Prefer descending motion toward the tonic.
    bool use_third = rng::rollProbability(gen, kThirdProb);
    int step = 0;
    if (use_third) {
      step = rng::rollProbability(gen, 0.8f) ? -2 : -3;
    } else {
      step = rng::rollProbability(gen, 0.85f) ? -1 : -2;
    }
    current_degree += step;

    // Clamp: do not go below degree 0 (tonic) until the final note.
    if (current_degree < 1) current_degree = 1;
    if (current_degree > 7) current_degree = 7;
    degrees[idx] = current_degree;
  }

  // Ensure the climax pitch is unique: if any other degree equals the
  // climax degree, lower it by 1.
  for (int idx = 0; idx < num_notes; ++idx) {
    if (idx != climax_position && degrees[idx] >= climax_degree) {
      degrees[idx] = climax_degree - 1;
    }
  }

  // Convert degrees to NoteEvents.
  for (int idx = 0; idx < num_notes; ++idx) {
    int pitch = degreeToPitch(degrees[idx], kBaseNote, key_offset);
    pitch = std::max(36, std::min(96, pitch));

    NoteEvent note;
    note.start_tick = static_cast<Tick>(idx) * kCfDuration;
    note.duration = kCfDuration;
    note.pitch = static_cast<uint8_t>(pitch);
    note.velocity = 80;
    note.voice = 0;

    result.notes.push_back(note);
  }

  return result;
}

}  // namespace bach
