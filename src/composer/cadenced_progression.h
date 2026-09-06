#ifndef BACH_COMPOSER_CADENCED_PROGRESSION_H
#define BACH_COMPOSER_CADENCED_PROGRESSION_H

#include <cstdint>
#include <vector>

#include "composer/figuration.h"
#include "composer/harness_fixture.h"

/// @file
/// @brief The per-bar chord progression a form writes when it has no thematic
///        material to take its harmony from, and the design-valued cadence that
///        closes it.
///
/// A form built from a subject takes its harmony from wherever the subject
/// goes. A form written bar by bar against a plan has no such source: it has to
/// decide what every bar states before a single note exists, and it has to
/// arrive at the perfect-authentic close at whatever length the request
/// resolved to. Both answers depend only on the piece's length, seed and mode
/// -- never on the texture laid over them -- so a solo running line and three
/// independent organ voices reach for exactly the same progression. Stating it
/// once is what keeps two such forms from drifting into different harmonic
/// languages while both claim to be diatonic.
///
/// The blocks are drawn from the shared diatonic catalogs (kHarmonyPatterns /
/// kHarmonyPatternsMinor), so a piece assembled this way speaks the same
/// harmony as the rest of the composer; and carrying the catalog's own
/// ChordSpec through means a dominant that markDominantSevenths spells with its
/// seventh reaches the caller's anchor selector rather than being flattened
/// back to a triad on the way in.

namespace bach::composer {

/// @brief Build an N-bar per-bar progression from the shared 4-chord harmony
///        catalogs, ending on a design-valued V -> I(i) cadence.
///
/// Each 4-bar block cycles a catalog pattern selected by (seed, block) so
/// successive blocks differ; the final two bars are overwritten with a half
/// cadence (V) then the tonic (I / i, or a Picardy I in minor on the elected
/// seeds), the perfect-authentic close every length lands on.
///
/// @param bars Total bar count (>= 8).
/// @param seed Piece seed (selects which catalog pattern each block uses).
/// @param mode Major selects kHarmonyPatterns, Minor selects kHarmonyPatternsMinor.
/// @param cello_implicit_safe Restrict the minor catalog to patterns whose every
///        root is a harmonic-minor degree. The cello prelude's implicit-voice
///        shapes need a dedicated migration before they can safely admit every
///        natural-minor root; forms that state their voices explicitly do not.
/// @return Per-bar chord list of length `bars`.
std::vector<detail::ChordSpec> buildCadencedProgression(int bars, std::uint32_t seed,
                                                        detail::Mode mode,
                                                        bool cello_implicit_safe = false);

/// @brief Emit one ChordEvent per bar into a HarmonicPlan from a progression.
/// @param out Fixture whose harmony plan receives the chords.
/// @param chords Per-bar progression.
/// @param mode Selects the tonic minor flag for the plan.
void writeBarChords(HarnessFixture& out, const std::vector<detail::ChordSpec>& chords,
                    detail::Mode mode);

}  // namespace bach::composer

#endif  // BACH_COMPOSER_CADENCED_PROGRESSION_H
