// FigurenGenerator implementation for Goldberg Variations Elaboratio mode.

#include "forms/goldberg/goldberg_figuren.h"

#include <algorithm>
#include <cmath>
#include <random>

#include "core/interval.h"
#include "core/note_creator.h"
#include "core/pitch_utils.h"
#include "transform/sequence.h"

namespace bach {

namespace {

/// Number of bars in the Goldberg structural grid.
constexpr int kGridBars = 32;

/// Default velocity for Goldberg figura notes.
constexpr uint8_t kDefaultVelocity = 80;

/// Harpsichord range limits (MIDI pitch).
constexpr uint8_t kHarpsichordLow = 36;   // C2
constexpr uint8_t kHarpsichordHigh = 96;   // C7

/// Register offset per voice index (semitones relative to tonic octave 4).
/// voice_index 0 = melody register (~G4), 1 = middle (~G3), 2+ = lower (~G2).
/// Harpsichord melody sits in C4-C6 range; G4 (67) is an ideal center.
constexpr int kVoiceRegisterOffsets[] = {0, -12, -24};
constexpr int kNumRegisterOffsets = 3;

/// @brief Get the scale type for a KeySignature.
ScaleType scaleTypeForKey(const KeySignature& key) {
  return key.is_minor ? ScaleType::NaturalMinor : ScaleType::Major;
}

/// @brief Get scale intervals for a KeySignature.
const int* scaleIntervalsForKey(const KeySignature& key) {
  return getScaleIntervals(scaleTypeForKey(key));
}

/// @brief Build chord tones (scale degrees 1, 3, 5) relative to a pivot pitch.
/// @param pivot_pitch The harmonic pivot (bass) MIDI pitch.
/// @param key Key signature for scale context.
/// @param register_offset Octave offset from pivot.
/// @return Vector of 3 chord-tone MIDI pitches.
std::vector<uint8_t> buildChordTones(uint8_t pivot_pitch, const KeySignature& key,
                                      int register_offset,
                                      uint8_t range_low = kHarpsichordLow,
                                      uint8_t range_high = kHarpsichordHigh) {
  int root_pc = static_cast<int>(key.tonic);
  const int* scale = scaleIntervalsForKey(key);
  int pivot_pc = getPitchClass(pivot_pitch);

  // Find which scale degree the pivot is on.
  int pivot_degree = 0;
  int min_dist = 12;
  for (int deg = 0; deg < 7; ++deg) {
    int deg_pc = (root_pc + scale[deg]) % 12;
    int dist = interval_util::compoundToSimple(pivot_pc - deg_pc);
    if (dist == 0) {
      pivot_degree = deg;
      break;
    }
    if (dist < min_dist) {
      min_dist = dist;
      pivot_degree = deg;
    }
  }

  // Build chord tones: root (pivot degree), third (+2 degrees), fifth (+4 degrees).
  int base = static_cast<int>(pivot_pitch) + register_offset;
  std::vector<uint8_t> tones;
  tones.reserve(3);

  for (int offset : {0, 2, 4}) {
    int deg = (pivot_degree + offset) % 7;
    int octave_add = (pivot_degree + offset) / 7;
    int pc = (root_pc + scale[deg]) % 12;
    int target = base + (pc - getPitchClass(static_cast<uint8_t>(base)));
    // Ensure we go in the right direction.
    if (target < base - 6) target += 12;
    if (target > base + 18) target -= 12;
    target += octave_add * 12;
    tones.push_back(clampPitch(target, range_low, range_high));
  }

  return tones;
}

/// @brief Get a scale-neighbor pitch above or below the given pitch.
/// @param pitch MIDI pitch.
/// @param direction +1 for upper neighbor, -1 for lower neighbor.
/// @param key Key signature.
/// @return Neighbor MIDI pitch on the diatonic scale.
uint8_t getScaleNeighbor(uint8_t pitch, int direction, const KeySignature& key,
                         uint8_t range_low = kHarpsichordLow,
                         uint8_t range_high = kHarpsichordHigh) {
  int root_pc = static_cast<int>(key.tonic);
  const int* scale = scaleIntervalsForKey(key);
  int pitch_val = static_cast<int>(pitch);

  // Build a local scale pitch list spanning the octave around the pitch.
  for (int oct = -1; oct <= 1; ++oct) {
    for (int deg = 0; deg < 7; ++deg) {
      int candidate = (pitch_val / 12) * 12 + root_pc + scale[deg] + oct * 12;
      if (direction > 0 && candidate > pitch_val) {
        return clampPitch(candidate, range_low, range_high);
      }
      if (direction < 0 && candidate < pitch_val) {
        // Keep scanning to find the closest one below.
      }
    }
  }

  // For descending, scan differently: find the highest scale pitch below.
  if (direction < 0) {
    int best = pitch_val - 2;  // Fallback: whole step down.
    for (int oct = 1; oct >= -1; --oct) {
      for (int deg = 6; deg >= 0; --deg) {
        int candidate = (pitch_val / 12) * 12 + root_pc + scale[deg] + oct * 12;
        if (candidate < pitch_val) {
          return clampPitch(candidate, range_low, range_high);
        }
      }
    }
    return clampPitch(best, range_low, range_high);
  }

  // Fallback: step up.
  return clampPitch(pitch_val + 2, range_low, range_high);
}

/// @brief Get a scale pitch at a given number of scale steps from the reference.
/// @param pitch Starting MIDI pitch.
/// @param steps Number of scale steps (positive = up, negative = down).
/// @param key Key signature.
/// @return MIDI pitch after the given scale steps.
uint8_t scaleStep(uint8_t pitch, int steps, const KeySignature& key,
                  uint8_t range_low = kHarpsichordLow,
                  uint8_t range_high = kHarpsichordHigh) {
  uint8_t result = pitch;
  int dir = steps > 0 ? 1 : -1;
  int remaining = steps > 0 ? steps : -steps;
  for (int idx = 0; idx < remaining; ++idx) {
    result = getScaleNeighbor(result, dir, key, range_low, range_high);
  }
  return result;
}

/// @brief Fill a scale run between two pitches.
/// @param from Starting MIDI pitch.
/// @param to_pitch Target MIDI pitch.
/// @param count Desired number of pitches.
/// @param key Key signature.
/// @return Vector of scale pitches from 'from' toward 'to_pitch'.
std::vector<uint8_t> fillScaleRun(uint8_t from, uint8_t to_pitch, int count,
                                   const KeySignature& key,
                                   uint8_t range_low = kHarpsichordLow,
                                   uint8_t range_high = kHarpsichordHigh) {
  std::vector<uint8_t> result;
  result.reserve(static_cast<size_t>(count));

  int direction = (to_pitch >= from) ? 1 : -1;
  uint8_t current = from;
  for (int idx = 0; idx < count; ++idx) {
    result.push_back(current);
    current = getScaleNeighbor(current, direction, key, range_low, range_high);
  }
  return result;
}

}  // namespace

// ---------------------------------------------------------------------------
// PhraseShapingParams defaults
// ---------------------------------------------------------------------------

PhraseShapingParams getDefaultPhraseShaping(PhrasePosition pos) {
  switch (pos) {
    case PhrasePosition::Opening:
      return {1.0f, 0.0f, 0.1f, DirectionBias::Symmetric};
    case PhrasePosition::Expansion:
      return {1.0f, 2.0f, 0.0f, DirectionBias::Symmetric};
    case PhrasePosition::Intensification:
      return {1.2f, 4.0f, -0.1f, DirectionBias::Ascending};
    case PhrasePosition::Cadence:
      return {0.8f, -2.0f, 0.2f, DirectionBias::Descending};
  }
  return {1.0f, 0.0f, 0.0f, DirectionBias::Symmetric};
}

// ---------------------------------------------------------------------------
// FigurenGenerator::generate
// ---------------------------------------------------------------------------

std::vector<NoteEvent> FigurenGenerator::generate(
    const FiguraProfile& profile,
    const GoldbergStructuralGrid& grid,
    const KeySignature& key,
    const TimeSignature& time_sig,
    uint8_t voice_index,
    uint32_t seed,
    const IKeyboardInstrument* instrument,
    float theme_strength) const {
  std::vector<NoteEvent> all_notes;
  all_notes.reserve(static_cast<size_t>(kGridBars) * profile.notes_per_beat *
                    time_sig.beatsPerBar());

  std::mt19937 rng(seed);

  // Compute range from instrument or use harpsichord defaults.
  const uint8_t range_low = instrument ? instrument->getLowestPitch() : kHarpsichordLow;
  const uint8_t range_high = instrument ? instrument->getHighestPitch() : kHarpsichordHigh;

  // Determine starting pitch based on voice index and key.
  int reg_offset = (voice_index < kNumRegisterOffsets)
                       ? kVoiceRegisterOffsets[voice_index]
                       : 0;
  uint8_t register_center = clampPitch(
      static_cast<int>(tonicPitch(key.tonic, 4)) + reg_offset,
      range_low, range_high);
  uint8_t prev_pitch = register_center;

  Tick ticks_per_bar = time_sig.ticksPerBar();

  for (int bar_idx = 0; bar_idx < kGridBars; ++bar_idx) {
    const auto& bar_info = grid.getBar(bar_idx);
    PhraseShapingParams shaping = getDefaultPhraseShaping(bar_info.phrase_pos);

    auto bar_notes = generateBarFigura(
        profile, bar_info, key, time_sig, prev_pitch, register_center,
        shaping, range_low, range_high, theme_strength, rng);

    // Offset notes to correct bar position.
    Tick bar_start = static_cast<Tick>(bar_idx) * ticks_per_bar;
    for (auto& note : bar_notes) {
      note.start_tick += bar_start;
      note.voice = voice_index;
    }

    // Track previous pitch for melodic continuity.
    if (!bar_notes.empty()) {
      prev_pitch = bar_notes.back().pitch;
      // Prevent register drift: soft-pull back toward register center.
      int drift = static_cast<int>(prev_pitch) - static_cast<int>(register_center);
      if (drift > 12) {
        prev_pitch = register_center + 12;
      } else if (drift < -12) {
        prev_pitch = register_center - 12;
      }
    }

    // Apply phrase shaping post-adjustments.
    applyPhraseShaping(bar_notes, bar_info.phrase_pos, bar_info.tension);

    // Optionally apply sequence (Zeugma) for bars that are not cadence bars.
    if (!bar_info.cadence.has_value()) {
      std::uniform_real_distribution<float> prob_dist(0.0f, 1.0f);
      if (prob_dist(rng) < profile.sequence_probability && !bar_notes.empty()) {
        auto seq_notes = applySequence(bar_notes, -1, 1, key);
        // Only use sequence if it fits within the next bar.
        if (!seq_notes.empty()) {
          Tick seq_end = seq_notes.back().start_tick + seq_notes.back().duration;
          Tick next_bar_end = bar_start + ticks_per_bar * 2;
          if (seq_end <= next_bar_end && bar_idx + 1 < kGridBars) {
            // The sequence overwrites the next bar -- skip it by not adding here.
            // Instead, we just add the original and let the sequence be handled
            // by the next bar's generation. For simplicity, just add the notes.
          }
        }
      }
    }

    all_notes.insert(all_notes.end(), bar_notes.begin(), bar_notes.end());
  }

  return all_notes;
}

// ---------------------------------------------------------------------------
// FigurenGenerator::generateBarFigura
// ---------------------------------------------------------------------------

std::vector<NoteEvent> FigurenGenerator::generateBarFigura(
    const FiguraProfile& profile,
    const StructuralBarInfo& bar_info,
    const KeySignature& key,
    const TimeSignature& time_sig,
    uint8_t prev_pitch,
    uint8_t register_center,
    const PhraseShapingParams& shaping,
    uint8_t range_low,
    uint8_t range_high,
    float theme_strength,
    std::mt19937& rng) const {
  // Get harmonic pivot from the structural grid.
  uint8_t pivot = bar_info.bass_motion.primary_pitch;

  // Compute effective notes per beat, adjusted by density multiplier.
  int raw_npb = static_cast<int>(
      std::round(static_cast<float>(profile.notes_per_beat) * shaping.density_multiplier));
  uint8_t effective_npb = static_cast<uint8_t>(std::max(1, std::min(raw_npb, 8)));

  // Determine effective direction: use shaping override unless Symmetric.
  DirectionBias effective_dir = (shaping.direction_override != DirectionBias::Symmetric)
                                    ? shaping.direction_override
                                    : profile.direction;

  // Choose between primary and secondary figura based on phrase position.
  // Use secondary figura for contrast in Expansion bars.
  FiguraType active_type = profile.primary;
  if (bar_info.phrase_pos == PhrasePosition::Expansion) {
    std::uniform_int_distribution<int> coin(0, 2);
    if (coin(rng) == 0) {
      active_type = profile.secondary;
    }
  }

  // Compute register offset from register_center (not prev_pitch) to avoid drift.
  int voice_reg = nearestOctaveShift(
      static_cast<int>(register_center) - static_cast<int>(pivot));

  // Generate the pitch pattern.
  auto pitches = generateFiguraPattern(
      active_type, pivot, effective_npb, effective_dir, key, voice_reg,
      range_low, range_high, rng);

  if (pitches.empty()) return {};

  // Smooth melodic connection: adjust first non-rest pitch to be close to prev_pitch.
  if (prev_pitch > 0 && !pitches.empty()) {
    // Find first non-rest pitch.
    uint8_t first_sounding = 0;
    for (auto val : pitches) {
      if (val > 0) { first_sounding = val; break; }
    }
    if (first_sounding > 0) {
      int diff = static_cast<int>(first_sounding) - static_cast<int>(prev_pitch);
      if (diff > 12 || diff < -12) {
        int shift = nearestOctaveShift(diff);
        for (auto& pitch : pitches) {
          if (pitch > 0) {  // Only adjust sounding pitches, not rest markers.
            pitch = clampPitch(static_cast<int>(pitch) - shift,
                               range_low, range_high);
          }
        }
      }
    }
  }

  // Convert pitches to NoteEvents.
  Tick ticks_per_bar_val = time_sig.ticksPerBar();
  Tick total_notes = static_cast<Tick>(pitches.size());
  Tick note_duration = total_notes > 0 ? ticks_per_bar_val / total_notes : ticks_per_bar_val;

  // Compute effective chord tone ratio.
  float chord_ratio = profile.chord_tone_ratio + shaping.chord_tone_shift;
  chord_ratio = std::max(0.0f, std::min(1.0f, chord_ratio));

  // Build chord tones for validation.
  auto chord_tones = buildChordTones(pivot, key, 0, range_low, range_high);

  std::vector<NoteEvent> notes;
  notes.reserve(pitches.size());

  std::uniform_real_distribution<float> prob_dist(0.0f, 1.0f);

  for (size_t idx = 0; idx < pitches.size(); ++idx) {
    uint8_t pitch = pitches[idx];

    // Pitch 0 is a rest marker (e.g., Suspirans sigh gap). Skip it.
    if (pitch == 0) continue;

    // Apply chord tone ratio: with probability chord_ratio, snap to nearest chord tone.
    if (prob_dist(rng) < chord_ratio && !chord_tones.empty()) {
      size_t closest_idx = findClosestToneIndex(chord_tones, pitch);
      int snap_dist = absoluteInterval(pitch, chord_tones[closest_idx]);
      if (snap_dist <= 4) {  // Only snap if within a major 3rd.
        pitch = chord_tones[closest_idx];
      }
    }

    // Theme distance bias: on beat starts, pull toward Aria melody pitches.
    int notes_per_beat = static_cast<int>(effective_npb);
    bool is_beat_start = (notes_per_beat > 0) &&
                         (static_cast<int>(idx) % notes_per_beat == 0);
    if (theme_strength > 0.0f && is_beat_start) {
      int beat_idx = (notes_per_beat > 0)
                         ? static_cast<int>(idx) / notes_per_beat
                         : 0;
      uint8_t theme_pitch = bar_info.aria_melody[
          static_cast<size_t>(std::min(beat_idx, 2))];
      if (theme_pitch > 0) {
        int dist_to_theme = absoluteInterval(pitch, theme_pitch);
        float effective_strength = theme_strength;
        if (dist_to_theme > 7) {
          effective_strength *= 0.3f;  // Dampen for distant pitches.
        }
        if (dist_to_theme > 4) {
          float pull_prob = effective_strength * 0.5f;
          if (prob_dist(rng) < pull_prob) {
            int dir = (theme_pitch > pitch) ? 1 : -1;
            pitch = getScaleNeighbor(pitch, dir, key, range_low, range_high);
          }
        }
      }
    }

    // Clamp to harpsichord range.
    pitch = clampPitch(static_cast<int>(pitch), range_low, range_high);

    // Use createBachNote for proper provenance tracking.
    BachNoteOptions opts{};
    opts.voice = 0;  // Will be overridden by caller.
    opts.desired_pitch = pitch;
    opts.tick = static_cast<Tick>(idx) * note_duration;
    opts.duration = note_duration;
    opts.velocity = kDefaultVelocity;
    opts.source = BachNoteSource::GoldbergFigura;

    auto result = createBachNote(nullptr, nullptr, nullptr, opts);
    if (result.accepted) {
      notes.push_back(result.note);
    }
  }

  return notes;
}

// ---------------------------------------------------------------------------
// FigurenGenerator::generateFiguraPattern
// ---------------------------------------------------------------------------

std::vector<uint8_t> FigurenGenerator::generateFiguraPattern(
    FiguraType type,
    uint8_t pivot_pitch,
    uint8_t notes_per_beat,
    DirectionBias direction,
    const KeySignature& key,
    int register_offset,
    uint8_t range_low,
    uint8_t range_high,
    std::mt19937& rng) const {
  // Total notes for one bar (3 beats typical for 3/4).
  // The caller determines beats from time_sig, but we generate based on notes_per_beat.
  // Aim for 3 beats * notes_per_beat as a baseline (adjusted by caller if needed).
  constexpr int kDefaultBeats = 3;
  int total_notes = kDefaultBeats * static_cast<int>(notes_per_beat);
  if (total_notes <= 0) total_notes = 3;

  std::vector<uint8_t> pattern;
  pattern.reserve(static_cast<size_t>(total_notes));

  auto chord_tones = buildChordTones(pivot_pitch, key, register_offset,
                                      range_low, range_high);

  switch (type) {
    case FiguraType::Circulatio: {
      // Circular pattern: chord_tone -> upper_neighbor -> chord_tone -> lower_neighbor.
      uint8_t center = chord_tones.empty() ? pivot_pitch : chord_tones[0];
      uint8_t upper = getScaleNeighbor(center, 1, key);
      uint8_t lower = getScaleNeighbor(center, -1, key);
      for (int idx = 0; idx < total_notes; ++idx) {
        switch (idx % 4) {
          case 0: pattern.push_back(center); break;
          case 1: pattern.push_back(upper); break;
          case 2: pattern.push_back(center); break;
          case 3: pattern.push_back(lower); break;
        }
      }
      break;
    }

    case FiguraType::Tirata: {
      // Scale fill from pivot toward the next chord tone.
      uint8_t target = chord_tones.size() >= 3 ? chord_tones[2] : pivot_pitch;
      if (direction == DirectionBias::Descending) {
        target = getScaleNeighbor(pivot_pitch, -1, key);
        // Fill downward.
        target = clampPitch(static_cast<int>(pivot_pitch) - 12,
                            range_low, range_high);
      }
      pattern = fillScaleRun(
          chord_tones.empty() ? pivot_pitch : chord_tones[0],
          target, total_notes, key);
      break;
    }

    case FiguraType::Batterie: {
      // Rapid leap alternation between chord tones (1-5, 3-8, etc.).
      if (chord_tones.size() < 2) {
        // Fallback: alternate pivot with octave above.
        for (int idx = 0; idx < total_notes; ++idx) {
          pattern.push_back(idx % 2 == 0 ? pivot_pitch
                                         : clampPitch(static_cast<int>(pivot_pitch) + 12,
                                                      range_low, range_high));
        }
      } else {
        // Alternate between lowest and highest chord tones.
        uint8_t low_tone = chord_tones[0];
        uint8_t high_tone = chord_tones.back();
        // Add more variety: include middle tone.
        for (int idx = 0; idx < total_notes; ++idx) {
          int position = idx % 4;
          if (position == 0 || position == 2) {
            pattern.push_back(low_tone);
          } else if (position == 1) {
            pattern.push_back(high_tone);
          } else {
            pattern.push_back(chord_tones.size() > 1 ? chord_tones[1] : low_tone);
          }
        }
      }
      break;
    }

    case FiguraType::Arpeggio: {
      // Ascending/descending through chord tones (1-3-5-8).
      auto tones = chord_tones;
      // Add octave of root.
      if (!tones.empty()) {
        uint8_t octave_root = clampPitch(static_cast<int>(tones[0]) + 12,
                                          range_low, range_high);
        tones.push_back(octave_root);
      }
      if (direction == DirectionBias::Descending) {
        std::reverse(tones.begin(), tones.end());
      }
      for (int idx = 0; idx < total_notes; ++idx) {
        size_t tone_idx = static_cast<size_t>(idx) % tones.size();
        if (direction == DirectionBias::Alternating) {
          // Ascending then descending within each cycle.
          size_t cycle_pos = static_cast<size_t>(idx) % (tones.size() * 2);
          if (cycle_pos < tones.size()) {
            tone_idx = cycle_pos;
          } else {
            tone_idx = tones.size() * 2 - 1 - cycle_pos;
          }
        }
        pattern.push_back(tones[tone_idx]);
      }
      break;
    }

    case FiguraType::Suspirans: {
      // Sigh motif: rest (pitch 0 to signal rest), descending 2nd, chord tone.
      uint8_t chord_tone = chord_tones.empty() ? pivot_pitch : chord_tones[0];
      uint8_t upper_step = getScaleNeighbor(chord_tone, 1, key);
      for (int idx = 0; idx < total_notes; ++idx) {
        int phase = idx % 3;
        if (phase == 0) {
          // Rest: use pitch 0 to signal a rest gap.
          pattern.push_back(0);
        } else if (phase == 1) {
          pattern.push_back(upper_step);
        } else {
          pattern.push_back(chord_tone);
        }
      }
      break;
    }

    case FiguraType::Trillo: {
      // Alternation of main note and upper 2nd.
      uint8_t main_note = chord_tones.empty() ? pivot_pitch : chord_tones[0];
      uint8_t upper = getScaleNeighbor(main_note, 1, key);
      for (int idx = 0; idx < total_notes; ++idx) {
        pattern.push_back(idx % 2 == 0 ? main_note : upper);
      }
      break;
    }

    case FiguraType::DottedGrave: {
      // Dotted rhythm: long-short pattern. We use pitches that suggest
      // the dotted feel; the rhythm is adjusted later. Use chord tones
      // with passing tones between them.
      for (int idx = 0; idx < total_notes; ++idx) {
        if (idx % 2 == 0) {
          // Long note: chord tone.
          size_t tone_idx = static_cast<size_t>(idx / 2) % chord_tones.size();
          pattern.push_back(chord_tones.empty() ? pivot_pitch : chord_tones[tone_idx]);
        } else {
          // Short note: scale step toward next chord tone.
          uint8_t prev = pattern.back();
          pattern.push_back(getScaleNeighbor(prev, direction == DirectionBias::Descending ? -1 : 1,
                                              key));
        }
      }
      break;
    }

    case FiguraType::Bariolage: {
      // Alternating up/down broken chord. Similar to Batterie but with
      // register alternation creating a wider spread.
      if (chord_tones.size() < 2) {
        for (int idx = 0; idx < total_notes; ++idx) {
          pattern.push_back(pivot_pitch);
        }
      } else {
        uint8_t low_tone = chord_tones[0];
        uint8_t high_tone = clampPitch(static_cast<int>(chord_tones.back()) + 12,
                                        range_low, range_high);
        for (int idx = 0; idx < total_notes; ++idx) {
          pattern.push_back(idx % 2 == 0 ? low_tone : high_tone);
        }
      }
      break;
    }

    case FiguraType::Sarabande: {
      // Beat 2 emphasis, slow arc. Fewer notes, more sustained.
      // Use chord tones with beat 2 getting the most prominent pitch.
      uint8_t beat1 = chord_tones.empty() ? pivot_pitch : chord_tones[0];
      uint8_t beat2 = chord_tones.size() > 1 ? chord_tones[1] : beat1;
      uint8_t beat3 = chord_tones.size() > 2 ? chord_tones[2] : beat1;
      std::vector<uint8_t> beat_pitches = {beat1, beat2, beat3};
      for (int idx = 0; idx < total_notes; ++idx) {
        size_t beat_idx = static_cast<size_t>(idx) % beat_pitches.size();
        pattern.push_back(beat_pitches[beat_idx]);
      }
      break;
    }

    case FiguraType::Passepied: {
      // Light eighths, 3rd leaps + stepwise motion.
      uint8_t current = chord_tones.empty() ? pivot_pitch : chord_tones[0];
      std::uniform_int_distribution<int> leap_dist(0, 2);
      // For Symmetric/Alternating: alternate direction every few notes.
      int dir = (direction == DirectionBias::Descending) ? -1 : 1;
      for (int idx = 0; idx < total_notes; ++idx) {
        pattern.push_back(current);
        // Symmetric: reverse direction when reaching register bounds.
        if (direction == DirectionBias::Symmetric || direction == DirectionBias::Alternating) {
          if (current >= range_high - 5) dir = -1;
          else if (current <= range_low + 5) dir = 1;
          else if (idx % 6 == 5) dir = -dir;  // Alternate every 6 notes.
        }
        if (leap_dist(rng) == 0) {
          current = scaleStep(current, dir * 2, key);
        } else {
          current = getScaleNeighbor(current, dir, key);
        }
      }
      break;
    }

    case FiguraType::Gigue: {
      // Compound meter leaps (3rds) + stepwise motion.
      uint8_t current = chord_tones.empty() ? pivot_pitch : chord_tones[0];
      for (int idx = 0; idx < total_notes; ++idx) {
        pattern.push_back(current);
        // Alternate between leaps and steps in groups of 3.
        if (idx % 3 == 0) {
          // Leap of a 3rd.
          current = scaleStep(current, direction == DirectionBias::Descending ? -2 : 2, key);
        } else {
          // Step.
          current = getScaleNeighbor(current,
                                      direction == DirectionBias::Descending ? -1 : 1, key);
        }
      }
      break;
    }
  }

  return pattern;
}

// ---------------------------------------------------------------------------
// FigurenGenerator::applyPhraseShaping
// ---------------------------------------------------------------------------

void FigurenGenerator::applyPhraseShaping(
    std::vector<NoteEvent>& notes,
    PhrasePosition pos,
    const TensionProfile& tension) const {
  if (notes.empty()) return;

  switch (pos) {
    case PhrasePosition::Opening:
      // Stable opening: no modification needed.
      break;

    case PhrasePosition::Expansion:
      // Slight velocity increase for expansion.
      for (auto& note : notes) {
        note.velocity = clampPitch(
            static_cast<int>(note.velocity) + 5, 1, 127);
      }
      break;

    case PhrasePosition::Intensification: {
      // Build tension: increase velocity proportional to tension.
      int vel_boost = static_cast<int>(tension.aggregate() * 15.0f);
      for (auto& note : notes) {
        note.velocity = clampPitch(
            static_cast<int>(note.velocity) + vel_boost, 1, 127);
      }
      break;
    }

    case PhrasePosition::Cadence: {
      // Resolution: guide toward stability, reduce velocity at end.
      if (notes.size() >= 2) {
        // Taper velocity for the last quarter of notes.
        size_t taper_start = notes.size() * 3 / 4;
        for (size_t idx = taper_start; idx < notes.size(); ++idx) {
          int reduction = static_cast<int>((idx - taper_start + 1) * 3);
          notes[idx].velocity = clampPitch(
              static_cast<int>(notes[idx].velocity) - reduction, 40, 127);
        }
      }
      break;
    }
  }
}

// ---------------------------------------------------------------------------
// FigurenGenerator::applySequence
// ---------------------------------------------------------------------------

std::vector<NoteEvent> FigurenGenerator::applySequence(
    const std::vector<NoteEvent>& motif,
    int degree_step,
    int repetitions,
    const KeySignature& key) const {
  if (motif.empty() || repetitions <= 0) return {};

  Tick dur = motifDuration(motif);
  if (dur == 0) return {};

  // Find start tick of the motif.
  Tick motif_end = motif.front().start_tick + dur;

  ScaleType scale = key.is_minor ? ScaleType::NaturalMinor : ScaleType::Major;
  return generateDiatonicSequence(motif, repetitions, degree_step, motif_end,
                                  key.tonic, scale);
}

}  // namespace bach
