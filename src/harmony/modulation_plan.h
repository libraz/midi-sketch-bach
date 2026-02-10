// Modulation plan -- immutable key plan for fugue episodes.

#ifndef BACH_HARMONY_MODULATION_PLAN_H
#define BACH_HARMONY_MODULATION_PLAN_H

#include <vector>

#include "core/basic_types.h"
#include "harmony/harmonic_timeline.h"
#include "harmony/key.h"

namespace bach {

/// @brief A single modulation target within a fugue's key plan.
struct ModulationTarget {
  Key target_key = Key::C;
  bool target_is_minor = false;
  FuguePhase phase = FuguePhase::Develop;       // When to modulate
  CadenceType entry_cadence = CadenceType::Perfect;  // How to enter the new key
};

/// @brief Immutable modulation plan for a fugue (P4: design values, not searched).
///
/// Created at config time with fixed values based on home key and mode.
/// Once created, the plan never changes during generation.
struct ModulationPlan {
  std::vector<ModulationTarget> targets;

  /// @brief Create a standard modulation plan for a major key fugue.
  ///
  /// Design values (P4): Develop -> dominant, Develop -> relative minor,
  /// Develop -> subdominant, Resolve -> home.
  ///
  /// @param home The home key tonic.
  /// @return Modulation plan with 4 targets.
  static ModulationPlan createForMajor(Key home);

  /// @brief Create a standard modulation plan for a minor key fugue.
  ///
  /// Design values (P4): Develop -> relative major, Develop -> dominant minor,
  /// Develop -> subdominant, Resolve -> home.
  ///
  /// @param home The home key tonic.
  /// @return Modulation plan with 4 targets.
  static ModulationPlan createForMinor(Key home);

  /// @brief Get the target key for a specific episode index.
  /// @param episode_index 0-based episode index.
  /// @param home_key Fallback home key if index is out of range.
  /// @return Target key for that episode, or home key if index out of range.
  Key getTargetKey(int episode_index, Key home_key) const;
};

/// @brief Compute related keys for a given tonic.
/// @param tonic The home key.
/// @param is_minor Whether the home key is minor.
/// @return Vector of closely related key signatures (dominant, subdominant,
///         relative, parallel).
std::vector<KeySignature> getRelatedKeys(Key tonic, bool is_minor);

}  // namespace bach

#endif  // BACH_HARMONY_MODULATION_PLAN_H
