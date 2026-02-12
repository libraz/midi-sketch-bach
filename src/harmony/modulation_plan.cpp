// Implementation of ModulationPlan -- immutable key plan for fugue episodes.

#include "harmony/modulation_plan.h"

namespace bach {

ModulationPlan ModulationPlan::createForMajor(Key home) {
  ModulationPlan plan;
  plan.targets.reserve(4);

  // Target 0: dominant, major, Develop phase, Half cadence entry
  ModulationTarget dominant;
  dominant.target_key = getDominant(KeySignature{home, false}).tonic;
  dominant.target_is_minor = false;
  dominant.phase = FuguePhase::Develop;
  dominant.entry_cadence = CadenceType::Half;
  plan.targets.push_back(dominant);

  // Target 1: relative minor, minor, Develop phase, Perfect cadence
  ModulationTarget relative_minor;
  relative_minor.target_key = getRelative(KeySignature{home, false}).tonic;
  relative_minor.target_is_minor = true;
  relative_minor.phase = FuguePhase::Develop;
  relative_minor.entry_cadence = CadenceType::Perfect;
  plan.targets.push_back(relative_minor);

  // Target 2: subdominant, major, Develop phase, Perfect cadence
  ModulationTarget subdominant;
  subdominant.target_key = getSubdominant(KeySignature{home, false}).tonic;
  subdominant.target_is_minor = false;
  subdominant.phase = FuguePhase::Develop;
  subdominant.entry_cadence = CadenceType::Perfect;
  plan.targets.push_back(subdominant);

  // Target 3: home key, major, Resolve phase, Perfect cadence
  ModulationTarget home_return;
  home_return.target_key = home;
  home_return.target_is_minor = false;
  home_return.phase = FuguePhase::Resolve;
  home_return.entry_cadence = CadenceType::Perfect;
  plan.targets.push_back(home_return);

  return plan;
}

ModulationPlan ModulationPlan::createForMinor(Key home) {
  ModulationPlan plan;
  plan.targets.reserve(4);

  // Target 0: relative major, major, Develop phase, Half cadence
  ModulationTarget relative_major;
  relative_major.target_key = getRelative(KeySignature{home, true}).tonic;
  relative_major.target_is_minor = false;
  relative_major.phase = FuguePhase::Develop;
  relative_major.entry_cadence = CadenceType::Half;
  plan.targets.push_back(relative_major);

  // Target 1: dominant, MAJOR, Develop phase, Perfect cadence.
  // In Baroque practice, V in minor keys is major (harmonic minor raised 7th).
  ModulationTarget dominant_minor;
  dominant_minor.target_key = getDominant(KeySignature{home, true}).tonic;
  dominant_minor.target_is_minor = false;
  dominant_minor.phase = FuguePhase::Develop;
  dominant_minor.entry_cadence = CadenceType::Perfect;
  plan.targets.push_back(dominant_minor);

  // Target 2: subdominant, minor, Develop phase, Perfect cadence
  ModulationTarget subdominant;
  subdominant.target_key = getSubdominant(KeySignature{home, true}).tonic;
  subdominant.target_is_minor = true;
  subdominant.phase = FuguePhase::Develop;
  subdominant.entry_cadence = CadenceType::Perfect;
  plan.targets.push_back(subdominant);

  // Target 3: home key, minor, Resolve phase, Perfect cadence
  ModulationTarget home_return;
  home_return.target_key = home;
  home_return.target_is_minor = true;
  home_return.phase = FuguePhase::Resolve;
  home_return.entry_cadence = CadenceType::Perfect;
  plan.targets.push_back(home_return);

  return plan;
}

Key ModulationPlan::getTargetKey(int episode_index, Key home_key) const {
  if (episode_index < 0 || episode_index >= static_cast<int>(targets.size())) {
    return home_key;
  }
  return targets[episode_index].target_key;
}

std::vector<KeySignature> getRelatedKeys(Key tonic, bool is_minor) {
  KeySignature home{tonic, is_minor};
  std::vector<KeySignature> result;
  result.reserve(4);

  // Dominant (same mode)
  result.push_back(getDominant(home));

  // Subdominant (same mode)
  result.push_back(getSubdominant(home));

  // Relative major/minor (opposite mode)
  result.push_back(getRelative(home));

  // Parallel (same tonic, opposite mode)
  result.push_back(getParallel(home));

  return result;
}

}  // namespace bach
