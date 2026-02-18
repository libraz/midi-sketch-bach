// Obligation data structures implementation.

#include "constraint/obligation.h"


namespace bach {

const char* obligationTypeToString(ObligationType type) {
  switch (type) {
    case ObligationType::LeadingTone:      return "LeadingTone";
    case ObligationType::Seventh:          return "Seventh";
    case ObligationType::LeapResolve:      return "LeapResolve";
    case ObligationType::CadenceStable:    return "CadenceStable";
    case ObligationType::CadenceApproach:  return "CadenceApproach";
    case ObligationType::ImitationEntry:   return "ImitationEntry";
    case ObligationType::ImitationDistance: return "ImitationDistance";
    case ObligationType::StrongBeatHarm:   return "StrongBeatHarm";
    case ObligationType::InvariantRecovery: return "InvariantRecovery";
  }
  return "Unknown";
}

const char* obligationStrengthToString(ObligationStrength strength) {
  switch (strength) {
    case ObligationStrength::Structural: return "Structural";
    case ObligationStrength::Soft:       return "Soft";
  }
  return "Unknown";
}

int SubjectConstraintProfile::min_safe_stretto_offset(int num_voices) const {
  int best_offset = -1;

  for (const auto& entry : stretto_matrix) {
    if (entry.num_voices != num_voices) continue;
    if (entry.feasibility_score() < StrettoFeasibilityEntry::kMinFeasibleScore)
      continue;
    if (best_offset < 0 || entry.offset_ticks < best_offset) {
      best_offset = entry.offset_ticks;
    }
  }
  return best_offset;
}

}  // namespace bach
