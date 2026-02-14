// Internal header for testing generator post-processing functions.
// Not part of the public API. Include only from generator.cpp and tests.

#ifndef BACH_GENERATOR_INTERNAL_H
#define BACH_GENERATOR_INTERNAL_H

#include <vector>

#include "core/basic_types.h"

namespace bach {

/// @brief Remove duplicate notes and truncate overlapping notes within each
/// track, respecting voice boundaries.
void cleanupTrackOverlaps(std::vector<Track>& tracks);

}  // namespace bach

#endif  // BACH_GENERATOR_INTERNAL_H
