// Harpsichord instrument model implementation.

#include "instrument/keyboard/harpsichord_model.h"

namespace bach {

HarpsichordModel::HarpsichordModel(const HarpsichordConfig& config)
    : PianoModel(KeyboardSpanConstraints::advanced(),
                 KeyboardHandPhysics::advanced()),
      config_(config) {}

}  // namespace bach
