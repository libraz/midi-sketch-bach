#ifndef BACH_COMPOSER_FORM_FUGUE_H
#define BACH_COMPOSER_FORM_FUGUE_H

/// @file
/// @brief Layout rules the fugue-family builders share.

namespace bach::composer {

/// @brief Bars a related-key approach (V/X -> X) occupies before its entry.
constexpr int kRelatedKeyApproachBars = 2;

/// @brief Whether an entry may be prepared by a related-key approach.
///
/// The approach is written over the bars in front of the entry, so it is
/// admitted only where those bars lie past the exposition: an exposition's
/// bars hold its own statements in the home key, and an approach written over
/// them would replace material already emitted there.
///
/// @param entry_bar First bar of the entry the approach prepares.
/// @param exposition_end_bar First bar after the exposition.
bool relatedKeyApproachClearsExposition(int entry_bar, int exposition_end_bar);

}  // namespace bach::composer

#endif  // BACH_COMPOSER_FORM_FUGUE_H
