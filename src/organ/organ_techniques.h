// Shared baroque organ performance techniques extracted from toccata.cpp.
// These techniques are common across all organ forms in Bach's music.

#ifndef BACH_ORGAN_ORGAN_TECHNIQUES_H
#define BACH_ORGAN_ORGAN_TECHNIQUES_H

#include <cstdint>
#include <utility>
#include <vector>

#include "core/basic_types.h"
#include "harmony/key.h"
#include "organ/registration.h"

namespace bach {

// ---------------------------------------------------------------------------
// Organ velocity constant
// ---------------------------------------------------------------------------

/// @brief Organ fixed velocity (pipe organs have no velocity sensitivity).
constexpr uint8_t kOrganVelocity = 80;

/// @brief Grand pause duration (2 beats).
constexpr Tick kGrandPauseDuration = duration::kHalfNote;

// ---------------------------------------------------------------------------
// Cadential pedal point
// ---------------------------------------------------------------------------

/// @brief Type of cadential pedal point.
enum class PedalPointType : uint8_t { Tonic, Dominant };

/// @brief Generate a cadential pedal point in the pedal voice.
///
/// Produces sustained notes on either the tonic or dominant pitch, placed
/// in the organ pedal range. The pedal note is held for the full duration
/// from start_tick to end_tick.
///
/// @param key_sig Key signature for pitch determination.
/// @param start_tick Start tick of the pedal point.
/// @param end_tick End tick of the pedal point.
/// @param type Tonic or Dominant pedal.
/// @param voice_id Voice index for the pedal notes.
/// @return Vector of NoteEvents forming the pedal point.
std::vector<NoteEvent> generateCadentialPedal(
    const KeySignature& key_sig, Tick start_tick, Tick end_tick,
    PedalPointType type, uint8_t voice_id);

// ---------------------------------------------------------------------------
// Picardy third
// ---------------------------------------------------------------------------

/// @brief Apply Picardy third to the final chord of a minor-key piece.
///
/// Finds notes in the final bar and raises any minor thirds to major thirds.
/// No-op if the key is already major.
///
/// @param notes Note vector to modify in-place.
/// @param key_sig Key signature (only applies when is_minor == true).
/// @param final_bar_tick Start tick of the final bar.
void applyPicardyToFinalChord(std::vector<NoteEvent>& notes,
                              const KeySignature& key_sig,
                              Tick final_bar_tick);

// ---------------------------------------------------------------------------
// Block chord
// ---------------------------------------------------------------------------

/// @brief Generate a structural block chord at the given position.
///
/// Creates a chord spanning the specified voices, each sounding the
/// appropriate chord tone for its register.
///
/// @param key_sig Key signature for chord construction.
/// @param tick Chord start tick.
/// @param duration Chord duration in ticks.
/// @param num_voices Number of voices in the chord.
/// @param voice_ranges Low/high pitch pairs for each voice.
/// @return Vector of NoteEvents forming the block chord.
std::vector<NoteEvent> generateBlockChord(
    const KeySignature& key_sig, Tick tick, Tick duration,
    uint8_t num_voices,
    const std::vector<std::pair<uint8_t, uint8_t>>& voice_ranges);

// ---------------------------------------------------------------------------
// Registration presets (Principle 4: design values)
// ---------------------------------------------------------------------------

/// @brief Fixed registration presets for organ stops.
///
/// velocity_hint represents stop density, not performance dynamics.
struct OrganRegistrationPresets {
  /// @brief Light stops (flute 8', principal 4').
  static Registration piano();

  /// @brief Principal chorus (principal 8', 4', 2').
  static Registration mezzo();

  /// @brief Full principal + flutes.
  static Registration forte();

  /// @brief Principal + mixtures.
  static Registration pleno();

  /// @brief All stops + reeds.
  static Registration tutti();
};

// ---------------------------------------------------------------------------
// Registration plans
// ---------------------------------------------------------------------------

/// @brief Create a simple 3-point registration plan (start -> mid -> end).
///
/// Suitable for preludes, fantasias, and other non-fugue forms.
///
/// @param start_tick Piece start tick.
/// @param end_tick Piece end tick.
/// @return ExtendedRegistrationPlan with 3 points.
ExtendedRegistrationPlan createSimpleRegistrationPlan(
    Tick start_tick, Tick end_tick);

/// @brief Create a gradual crescendo registration plan for variation forms.
///
/// Each variation gets progressively fuller registration.
///
/// @param num_variations Total number of variations.
/// @param variation_duration Duration of each variation in ticks.
/// @return ExtendedRegistrationPlan with one point per variation.
ExtendedRegistrationPlan createVariationRegistrationPlan(
    int num_variations, Tick variation_duration);

}  // namespace bach

#endif  // BACH_ORGAN_ORGAN_TECHNIQUES_H
