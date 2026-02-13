// Hand-crossing variation generator implementation for Goldberg Variations.

#include "forms/goldberg/variations/goldberg_crossing.h"

#include <algorithm>

#include "core/note_creator.h"
#include "core/pitch_utils.h"
#include "forms/goldberg/goldberg_binary.h"
#include "forms/goldberg/goldberg_figuren.h"

namespace bach {

namespace {

/// Number of bars in the Goldberg structural grid.
constexpr int kGridBars = 32;

/// Number of bars per section for binary repeats.
constexpr int kBarsPerSection = 16;

/// Voice index for upper manual.
constexpr uint8_t kUpperVoice = 0;

/// Voice index for lower manual.
constexpr uint8_t kLowerVoice = 1;

}  // namespace

// ---------------------------------------------------------------------------
// CrossingGenerator::generate
// ---------------------------------------------------------------------------

CrossingResult CrossingGenerator::generate(
    int variation_number,
    const GoldbergStructuralGrid& grid,
    const KeySignature& key,
    const TimeSignature& time_sig,
    uint32_t seed) const {
  CrossingResult result;

  if (!isSupportedVariation(variation_number)) {
    return result;
  }

  FiguraProfile profile = buildProfile(variation_number);
  ManualAssignment manual = getManualAssignment(variation_number);

  FigurenGenerator figuren;

  // Generate upper manual voice (voice_index 0 = upper register).
  auto upper_notes = figuren.generate(
      profile, grid, key, time_sig, kUpperVoice, seed);

  // Generate lower manual voice (voice_index 1 = lower register).
  // Use a different seed derived from the base seed for voice independence.
  auto lower_notes = figuren.generate(
      profile, grid, key, time_sig, kLowerVoice, seed ^ 0x5A5A5A5Au);

  // Apply register protection: clamp each voice to its manual range.
  applyRegisterProtection(upper_notes, manual, true);
  applyRegisterProtection(lower_notes, manual, false);

  // Apply crossing logic at structurally appropriate positions.
  if (manual.allow_crossing) {
    applyCrossingLogic(upper_notes, lower_notes, grid, time_sig);
  }

  // Set voice IDs for proper track assignment.
  for (auto& note : upper_notes) {
    note.voice = kUpperVoice;
  }
  for (auto& note : lower_notes) {
    note.voice = kLowerVoice;
  }

  // Merge both voices.
  std::vector<NoteEvent> merged;
  merged.reserve(upper_notes.size() + lower_notes.size());
  merged.insert(merged.end(), upper_notes.begin(), upper_notes.end());
  merged.insert(merged.end(), lower_notes.begin(), lower_notes.end());

  // Sort by start_tick for proper ordering.
  std::sort(merged.begin(), merged.end(),
            [](const NoteEvent& lhs, const NoteEvent& rhs) {
              return lhs.start_tick < rhs.start_tick;
            });

  // Apply binary repeats: ||: A :||: B :||
  Tick section_ticks = static_cast<Tick>(kBarsPerSection) * time_sig.ticksPerBar();
  result.notes = applyBinaryRepeats(merged, section_ticks, false);
  result.success = !result.notes.empty();

  return result;
}

// ---------------------------------------------------------------------------
// CrossingGenerator::buildProfile
// ---------------------------------------------------------------------------

FiguraProfile CrossingGenerator::buildProfile(int variation_number) {
  switch (variation_number) {
    case 8:
      // Var 8: Wide leaps, alternating registers. Moderate sequence probability.
      return {
          FiguraType::Batterie,     // primary: rapid register alternation
          FiguraType::Arpeggio,     // secondary: arpeggiated contrast
          4,                        // notes_per_beat: sixteenth-note density
          DirectionBias::Alternating,
          0.7f,                     // chord_tone_ratio
          0.2f                      // sequence_probability
      };

    case 17:
      // Var 17: Rapid register alternation. Higher chord tone ratio for clarity.
      return {
          FiguraType::Batterie,     // primary: rapid register alternation
          FiguraType::Bariolage,    // secondary: register alternation contrast
          4,                        // notes_per_beat
          DirectionBias::Alternating,
          0.8f,                     // chord_tone_ratio: more consonant
          0.15f                     // sequence_probability
      };

    case 20:
      // Var 20: Virtuosic hand crossing. Wider leaps, lower chord tone ratio
      // for more passing-tone activity.
      return {
          FiguraType::Batterie,     // primary: rapid register alternation
          FiguraType::Tirata,       // secondary: scale runs for virtuosity
          4,                        // notes_per_beat
          DirectionBias::Alternating,
          0.6f,                     // chord_tone_ratio: more passing tones
          0.25f                     // sequence_probability
      };

    default:
      // Default: Var 8 profile.
      return buildProfile(8);
  }
}

// ---------------------------------------------------------------------------
// CrossingGenerator::getManualAssignment
// ---------------------------------------------------------------------------

ManualAssignment CrossingGenerator::getManualAssignment(int variation_number) {
  ManualAssignment manual;

  switch (variation_number) {
    case 8:
      // Var 8: Standard 2-manual range with moderate overlap.
      manual.upper_manual_low = 60;   // C4
      manual.upper_manual_high = 84;  // C6
      manual.lower_manual_low = 36;   // C2
      manual.lower_manual_high = 64;  // E4
      manual.allow_crossing = true;
      break;

    case 17:
      // Var 17: Slightly narrower ranges for tighter alternation.
      manual.upper_manual_low = 62;   // D4
      manual.upper_manual_high = 82;  // Bb5
      manual.lower_manual_low = 38;   // D2
      manual.lower_manual_high = 62;  // D4
      manual.allow_crossing = true;
      break;

    case 20:
      // Var 20: Widest ranges for virtuosic crossing.
      manual.upper_manual_low = 58;   // Bb3
      manual.upper_manual_high = 86;  // D6
      manual.lower_manual_low = 34;   // Bb1
      manual.lower_manual_high = 66;  // F#4
      manual.allow_crossing = true;
      break;

    default:
      // Default assignment.
      break;
  }

  return manual;
}

// ---------------------------------------------------------------------------
// CrossingGenerator::isCrossingAllowed
// ---------------------------------------------------------------------------

bool CrossingGenerator::isCrossingAllowed(PhrasePosition pos) {
  switch (pos) {
    case PhrasePosition::Opening:
    case PhrasePosition::Expansion:
      return true;
    case PhrasePosition::Intensification:
    case PhrasePosition::Cadence:
      return false;
  }
  return false;
}

// ---------------------------------------------------------------------------
// CrossingGenerator::applyRegisterProtection
// ---------------------------------------------------------------------------

void CrossingGenerator::applyRegisterProtection(
    std::vector<NoteEvent>& notes,
    const ManualAssignment& manual,
    bool is_upper) const {
  uint8_t range_low = is_upper ? manual.upper_manual_low : manual.lower_manual_low;
  uint8_t range_high = is_upper ? manual.upper_manual_high : manual.lower_manual_high;

  for (auto& note : notes) {
    if (note.pitch == 0) continue;  // Skip rest markers.
    note.pitch = clampPitch(static_cast<int>(note.pitch), range_low, range_high);
  }
}

// ---------------------------------------------------------------------------
// CrossingGenerator::applyCrossingLogic
// ---------------------------------------------------------------------------

void CrossingGenerator::applyCrossingLogic(
    std::vector<NoteEvent>& upper_notes,
    std::vector<NoteEvent>& lower_notes,
    const GoldbergStructuralGrid& grid,
    const TimeSignature& time_sig) const {
  Tick ticks_per_bar = time_sig.ticksPerBar();
  Tick beat_duration = ticks_per_bar / time_sig.beatsPerBar();

  // Apply crossing selectively: only on beat 2 of Opening and Expansion bars.
  // This creates idiomatic hand-crossing gestures on specific beats rather
  // than shifting entire bars, which would be musically unnatural and would
  // collapse register separation.
  for (int bar_idx = 0; bar_idx < kGridBars; ++bar_idx) {
    PhrasePosition pos = grid.getPhrasePosition(bar_idx);

    if (!isCrossingAllowed(pos)) continue;

    Tick bar_start = static_cast<Tick>(bar_idx) * ticks_per_bar;
    // Crossing window: beat 2 only (one beat's duration).
    Tick cross_start = bar_start + beat_duration;
    Tick cross_end = cross_start + beat_duration;

    // Upper voice dips into lower register on beat 2.
    for (auto& note : upper_notes) {
      if (note.start_tick >= cross_start && note.start_tick < cross_end && note.pitch > 0) {
        int crossed_pitch = static_cast<int>(note.pitch) - 12;
        if (crossed_pitch >= 36) {  // Don't go below C2.
          note.pitch = static_cast<uint8_t>(crossed_pitch);
        }
      }
    }

    // Lower voice reaches into upper register on beat 2.
    for (auto& note : lower_notes) {
      if (note.start_tick >= cross_start && note.start_tick < cross_end && note.pitch > 0) {
        int crossed_pitch = static_cast<int>(note.pitch) + 12;
        if (crossed_pitch <= 96) {  // Don't go above C7.
          note.pitch = static_cast<uint8_t>(crossed_pitch);
        }
      }
    }
  }
}

// ---------------------------------------------------------------------------
// CrossingGenerator::isSupportedVariation
// ---------------------------------------------------------------------------

bool CrossingGenerator::isSupportedVariation(int variation_number) {
  return variation_number == 8 || variation_number == 17 || variation_number == 20;
}

}  // namespace bach
