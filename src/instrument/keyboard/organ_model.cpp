/// @file
/// @brief Organ instrument model with manual ranges and pedal penalty.

#include "instrument/keyboard/organ_model.h"

#include "organ/pedal_constraints.h"

namespace bach {

// ---------------------------------------------------------------------------
// OrganManual string conversion
// ---------------------------------------------------------------------------

const char* organManualToString(OrganManual manual) {
  switch (manual) {
    case OrganManual::Great:   return "Great";
    case OrganManual::Swell:   return "Swell";
    case OrganManual::Positiv: return "Positiv";
    case OrganManual::Pedal:   return "Pedal";
  }
  return "Unknown";  // Unreachable for valid enum values
}

// ---------------------------------------------------------------------------
// OrganModel
// ---------------------------------------------------------------------------

OrganModel::OrganModel(const OrganConfig& config)
    : PianoModel(KeyboardSpanConstraints::virtuoso(),
                 KeyboardHandPhysics::virtuoso()),
      config_(config) {}

uint8_t OrganModel::getManualLow(OrganManual manual) const {
  switch (manual) {
    case OrganManual::Great:   return config_.great_low;
    case OrganManual::Swell:   return config_.swell_low;
    case OrganManual::Positiv: return config_.positiv_low;
    case OrganManual::Pedal:   return config_.pedal_low;
  }
  return 0;  // Unreachable for valid enum values
}

uint8_t OrganModel::getManualHigh(OrganManual manual) const {
  switch (manual) {
    case OrganManual::Great:   return config_.great_high;
    case OrganManual::Swell:   return config_.swell_high;
    case OrganManual::Positiv: return config_.positiv_high;
    case OrganManual::Pedal:   return config_.pedal_high;
  }
  return 0;  // Unreachable for valid enum values
}

bool OrganModel::isInManualRange(uint8_t pitch, OrganManual manual) const {
  return pitch >= getManualLow(manual) && pitch <= getManualHigh(manual);
}

float OrganModel::pedalPenalty(uint8_t pitch) const {
  return calculatePedalPenalty(pitch);
}

uint8_t OrganModel::channelForManual(OrganManual manual) {
  return static_cast<uint8_t>(manual);
}

uint8_t OrganModel::programForManual(OrganManual manual) {
  // Church Organ (GM 19) for Great, Positiv, Pedal
  // Reed Organ (GM 20) for Swell
  switch (manual) {
    case OrganManual::Swell: return 20;
    default:                 return 19;
  }
}

}  // namespace bach
