// Tonal plan: key modulation schedule for fugue development.

#ifndef BACH_FUGUE_TONAL_PLAN_H
#define BACH_FUGUE_TONAL_PLAN_H

#include <string>
#include <vector>

#include "core/basic_types.h"
#include "fugue/fugue_config.h"

namespace bach {

/// @brief A key change event within the tonal plan.
struct KeyChange {
  Key target_key = Key::C;
  Tick tick = 0;                               // When the modulation occurs
  FuguePhase phase = FuguePhase::Establish;    // Which phase this belongs to
};

/// @brief Tonal plan for an entire fugue.
///
/// The tonal plan determines the modulation schedule. During Establish,
/// the home key is maintained. During Develop, modulations to near-related
/// keys occur. During Resolve, the home key returns.
struct TonalPlan {
  Key home_key = Key::C;
  bool is_minor = false;  // Whether the home key is minor
  std::vector<KeyChange> modulations;

  /// @brief Get the active key at a given tick.
  /// @param tick The tick position to query.
  /// @return The key in effect at that tick.
  Key keyAtTick(Tick tick) const;

  /// @brief Get all keys visited in order (no duplicates in succession).
  /// @return Ordered list of unique key transitions.
  std::vector<Key> keySequence() const;

  /// @brief Get the number of modulations.
  /// @return Number of KeyChange entries in the modulations vector.
  size_t modulationCount() const;

  /// @brief Convert to JSON representation.
  /// @return JSON string containing home_key, is_minor, and modulations array.
  std::string toJson() const;
};

/// @brief Get the dominant key (perfect 5th above).
/// @param key The home key.
/// @return Key a perfect 5th above.
Key getDominantKey(Key key);

/// @brief Get the subdominant key (perfect 4th above / 5th below).
/// @param key The home key.
/// @return Key a perfect 4th above.
Key getSubdominantKey(Key key);

/// @brief Get the relative minor/major key.
/// @param key The home key.
/// @param is_home_minor True if the home key is minor.
/// @return The relative key (minor 3rd down for major, minor 3rd up for minor).
Key getRelativeKey(Key key, bool is_home_minor);

/// @brief Get the parallel major/minor key (same tonic, opposite mode).
/// @param key The home key.
/// @param is_home_minor True if the home key is minor.
/// @return The parallel key (same pitch class, always returns the input key since
///         parallel keys share the same tonic).
/// @note For Key enum purposes, parallel major/minor share the same tonic,
///       so this returns the same Key value. The mode change is implicit.
Key getParallelKey(Key key, bool is_home_minor);

/// @brief Get all near-related keys for a given home key.
///
/// Near-related keys are those within one step on the circle of fifths,
/// plus the relative major/minor. Returns: dominant, subdominant, relative,
/// and parallel keys.
///
/// @param key Home key.
/// @param is_minor Whether the home key is minor.
/// @return Vector of near-related keys (up to 4, duplicates removed).
std::vector<Key> getNearRelatedKeys(Key key, bool is_minor);

/// @brief Generate a tonal plan for a fugue.
///
/// Creates a standard fugue modulation schedule:
/// - Establish: stays in home key
/// - Develop: modulates through near-related keys
/// - Resolve: returns to home key
///
/// For major keys: home -> dominant -> relative minor -> subdominant -> home
/// For minor keys: home -> relative major -> dominant -> subdominant -> home
///
/// @param config Fugue configuration (key, num_voices, etc.).
/// @param is_minor Whether the fugue key is minor.
/// @param total_duration_ticks Total fugue duration in ticks.
/// @return Generated TonalPlan.
TonalPlan generateTonalPlan(const FugueConfig& config, bool is_minor,
                            Tick total_duration_ticks);

}  // namespace bach

#endif  // BACH_FUGUE_TONAL_PLAN_H
