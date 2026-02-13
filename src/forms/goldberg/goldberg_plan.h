// Goldberg Variations plan: fixed 32-entry table of variation descriptors.

#ifndef BACH_FORMS_GOLDBERG_GOLDBERG_PLAN_H
#define BACH_FORMS_GOLDBERG_GOLDBERG_PLAN_H

#include <vector>

#include "forms/goldberg/goldberg_types.h"

namespace bach {

/// @brief Create the complete Goldberg Variations plan (Design Value).
/// @return Vector of 32 descriptors: Aria(0) + 30 variations + da capo(31).
std::vector<GoldbergVariationDescriptor> createGoldbergPlan();

}  // namespace bach

#endif  // BACH_FORMS_GOLDBERG_GOLDBERG_PLAN_H
