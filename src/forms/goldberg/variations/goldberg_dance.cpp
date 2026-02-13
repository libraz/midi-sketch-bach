// Dance variation generator implementation for Goldberg Variations.

#include "forms/goldberg/variations/goldberg_dance.h"

#include <algorithm>
#include <random>

#include "forms/goldberg/goldberg_binary.h"
#include "forms/goldberg/goldberg_figuren.h"

namespace bach {

namespace {

/// Number of bars in one section (half of the 32-bar grid).
constexpr int kSectionBars = 16;

/// Variation number for the Sarabande-to-Tirata split variation.
constexpr int kVar26 = 26;

/// @brief Build a FiguraProfile from a DanceProfile.
/// @param dance The dance profile to convert.
/// @return FiguraProfile suitable for FigurenGenerator::generate().
FiguraProfile buildFiguraProfile(const DanceProfile& dance) {
  return {
      dance.primary_figura,
      dance.secondary_figura,
      dance.notes_per_beat,
      dance.direction,
      dance.chord_tone_ratio,
      dance.sequence_probability
  };
}

/// @brief Build an alternate FiguraProfile for Var 26 second half (Tirata).
/// @param dance The base dance profile.
/// @return FiguraProfile with Tirata as primary for rapid scale passages.
FiguraProfile buildTirataProfile(const DanceProfile& dance) {
  return {
      FiguraType::Tirata,       // Switch to rapid scale passages.
      dance.secondary_figura,
      static_cast<uint8_t>(dance.notes_per_beat * 2),  // Double density for Tirata.
      DirectionBias::Ascending,
      dance.chord_tone_ratio - 0.1f,  // Slightly less chord-tone snapping.
      dance.sequence_probability + 0.1f  // More sequential repetition.
  };
}

/// @brief Generate notes for a sub-range of bars using FigurenGenerator.
///
/// Generates notes for bars [start_bar, start_bar + num_bars) by generating
/// a full 32-bar grid and filtering to the desired range. Tick offsets are
/// preserved relative to the original grid.
///
/// @param profile FiguraProfile for generation.
/// @param grid The 32-bar structural grid.
/// @param key Key signature.
/// @param time_sig Time signature.
/// @param voice_index Voice index for register placement.
/// @param seed Random seed.
/// @param start_bar First bar to include (0-indexed).
/// @param num_bars Number of bars to include.
/// @return Filtered notes within the specified bar range.
std::vector<NoteEvent> generateBarRange(
    const FiguraProfile& profile,
    const GoldbergStructuralGrid& grid,
    const KeySignature& key,
    const TimeSignature& time_sig,
    uint8_t voice_index,
    uint32_t seed,
    int start_bar,
    int num_bars) {
  FigurenGenerator figuren;
  auto all_notes = figuren.generate(profile, grid, key, time_sig, voice_index, seed);

  Tick ticks_per_bar = time_sig.ticksPerBar();
  Tick range_start = static_cast<Tick>(start_bar) * ticks_per_bar;
  Tick range_end = static_cast<Tick>(start_bar + num_bars) * ticks_per_bar;

  std::vector<NoteEvent> filtered;
  filtered.reserve(all_notes.size() / 2);

  for (const auto& note : all_notes) {
    if (note.start_tick >= range_start && note.start_tick < range_end) {
      filtered.push_back(note);
    }
  }

  return filtered;
}

}  // namespace

// ---------------------------------------------------------------------------
// getDanceProfile
// ---------------------------------------------------------------------------

DanceProfile getDanceProfile(int variation_number) {
  switch (variation_number) {
    case 4:
      return {
          FiguraType::Passepied,    // primary_figura
          FiguraType::Circulatio,   // secondary_figura
          {3, 8},                   // time_sig: 3/8
          2,                        // notes_per_beat
          DirectionBias::Symmetric, // direction
          0.6f,                     // chord_tone_ratio
          0.3f,                     // sequence_probability
          3                         // voice_count
      };

    case 7:
      return {
          FiguraType::Gigue,        // primary_figura
          FiguraType::Batterie,     // secondary_figura
          {6, 8},                   // time_sig: 6/8
          2,                        // notes_per_beat
          DirectionBias::Ascending, // direction
          0.5f,                     // chord_tone_ratio
          0.4f,                     // sequence_probability
          2                         // voice_count
      };

    case 19:
      return {
          FiguraType::Passepied,    // primary_figura
          FiguraType::Circulatio,   // secondary_figura
          {3, 8},                   // time_sig: 3/8
          2,                        // notes_per_beat
          DirectionBias::Symmetric, // direction
          0.6f,                     // chord_tone_ratio
          0.3f,                     // sequence_probability
          3                         // voice_count
      };

    case kVar26:
      return {
          FiguraType::Sarabande,    // primary_figura
          FiguraType::Suspirans,    // secondary_figura
          {3, 4},                   // time_sig: 3/4
          2,                        // notes_per_beat
          DirectionBias::Symmetric, // direction
          0.7f,                     // chord_tone_ratio
          0.2f,                     // sequence_probability
          2                         // voice_count
      };

    default:
      // Default: Passepied profile for unsupported variation numbers.
      return {
          FiguraType::Passepied,
          FiguraType::Circulatio,
          {3, 8},
          2,
          DirectionBias::Symmetric,
          0.6f,
          0.3f,
          3
      };
  }
}

// ---------------------------------------------------------------------------
// DanceGenerator::generate
// ---------------------------------------------------------------------------

DanceResult DanceGenerator::generate(
    int variation_number,
    const GoldbergStructuralGrid& grid,
    const KeySignature& key,
    uint32_t seed) const {
  DanceResult result;
  DanceProfile dance = getDanceProfile(variation_number);

  FigurenGenerator figuren;
  std::mt19937 rng(seed);

  std::vector<NoteEvent> all_notes;

  if (variation_number == kVar26) {
    // Var 26 special: first 16 bars Sarabande, last 16 bars Tirata.
    FiguraProfile sarabande_profile = buildFiguraProfile(dance);
    FiguraProfile tirata_profile = buildTirataProfile(dance);

    // Generate voices for the first half (Sarabande).
    for (uint8_t voice_idx = 0; voice_idx < dance.voice_count; ++voice_idx) {
      uint32_t voice_seed = seed + voice_idx * 1000 + 1;
      auto first_half = generateBarRange(
          sarabande_profile, grid, key, dance.time_sig,
          voice_idx, voice_seed, 0, kSectionBars);
      all_notes.insert(all_notes.end(), first_half.begin(), first_half.end());
    }

    // Generate voices for the second half (Tirata).
    for (uint8_t voice_idx = 0; voice_idx < dance.voice_count; ++voice_idx) {
      uint32_t voice_seed = seed + voice_idx * 1000 + 2;
      auto second_half = generateBarRange(
          tirata_profile, grid, key, dance.time_sig,
          voice_idx, voice_seed, kSectionBars, kSectionBars);
      all_notes.insert(all_notes.end(), second_half.begin(), second_half.end());
    }
  } else {
    // Standard dance: generate all 32 bars with the same profile.
    FiguraProfile profile = buildFiguraProfile(dance);

    for (uint8_t voice_idx = 0; voice_idx < dance.voice_count; ++voice_idx) {
      uint32_t voice_seed = seed + voice_idx * 1000;
      auto voice_notes = figuren.generate(
          profile, grid, key, dance.time_sig, voice_idx, voice_seed);

      // Set the provenance source for dance notes.
      for (auto& note : voice_notes) {
        note.source = BachNoteSource::GoldbergDance;
      }

      all_notes.insert(all_notes.end(), voice_notes.begin(), voice_notes.end());
    }
  }

  // Set the provenance source for all notes (including Var 26).
  for (auto& note : all_notes) {
    note.source = BachNoteSource::GoldbergDance;
  }

  // Sort by start_tick for clean output.
  std::sort(all_notes.begin(), all_notes.end(),
            [](const NoteEvent& lhs, const NoteEvent& rhs) {
              return lhs.start_tick < rhs.start_tick;
            });

  // Apply binary repeats: ||: A :||: B :||
  Tick section_ticks = static_cast<Tick>(kSectionBars) * dance.time_sig.ticksPerBar();
  result.notes = applyBinaryRepeats(all_notes, section_ticks);
  result.success = !result.notes.empty();

  return result;
}

}  // namespace bach
