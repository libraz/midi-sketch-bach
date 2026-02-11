// Tonal plan: key modulation schedule for fugue development.

#ifndef BACH_FUGUE_TONAL_PLAN_H
#define BACH_FUGUE_TONAL_PLAN_H

#include <string>
#include <vector>

#include "core/basic_types.h"
#include "fugue/fugue_config.h"
#include "harmony/harmonic_timeline.h"
#include "harmony/modulation_plan.h"

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

  /// @brief Convert the tonal plan to a bar-resolution HarmonicTimeline.
  ///
  /// Each modulation region maps to I-chord events in the active key at
  /// bar resolution. The harmony is approximate (always I chord) but the
  /// key changes are accurate.
  ///
  /// @param total_duration Total duration in ticks.
  /// @return HarmonicTimeline at bar resolution.
  HarmonicTimeline toHarmonicTimeline(Tick total_duration) const;

  /// @brief Convert to a beat-resolution HarmonicTimeline with real progressions.
  ///
  /// For each key region (between consecutive modulations), generates a
  /// circle-of-fifths progression at beat resolution using
  /// HarmonicTimeline::createProgression(). This provides harmonically rich
  /// context for counterpoint validation during fugue generation.
  ///
  /// @param total_duration Total duration in ticks.
  /// @return HarmonicTimeline at beat resolution with varied chord degrees.
  HarmonicTimeline toDetailedTimeline(Tick total_duration) const;

  /// @brief Convert to JSON representation.
  /// @return JSON string containing home_key, is_minor, and modulations array.
  std::string toJson() const;
};


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

/// @brief Generate a tonal plan aligned with actual fugue section boundaries.
///
/// Unlike generateTonalPlan() which estimates phase boundaries at 1/3 intervals,
/// this function places key changes at the midpoint of each episode, matching
/// the actual generation structure. Key targets come from the ModulationPlan
/// rather than being independently computed.
///
/// @param config Fugue configuration (key, num_voices, episode_bars, develop_pairs).
/// @param mod_plan Modulation plan providing target keys for each episode.
/// @param subject_length_ticks Duration of the subject in ticks.
/// @param estimated_duration Total estimated fugue duration in ticks.
/// @return TonalPlan with key changes aligned to episode midpoints.
TonalPlan generateStructureAlignedTonalPlan(const FugueConfig& config,
                                            const ModulationPlan& mod_plan,
                                            Tick subject_length_ticks,
                                            Tick estimated_duration);

}  // namespace bach

#endif  // BACH_FUGUE_TONAL_PLAN_H
