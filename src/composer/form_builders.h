#ifndef BACH_COMPOSER_FORM_BUILDERS_H
#define BACH_COMPOSER_FORM_BUILDERS_H

#include <cstddef>
#include <cstdint>

#include "composer/arc.h"
#include "composer/figuration.h"
#include "composer/form_director.h"
#include "composer/harness_fixture.h"
#include "core/basic_types.h"

namespace bach::composer {

// Fully-resolved input handed to a per-form builder. All length/meter snapping,
// clamping, and compatibility checks have already happened in buildFormFixture,
// so a builder only assembles material. The arc accessor exposes the
// deterministic design-value curve (arcPoint) bound to this piece's cycle
// count, so builders need not recompute cycle_count themselves.
struct ResolvedRequest {
  std::uint16_t bars;
  std::uint32_t seed;
  detail::Mode mode;  // Major / Minor scale selector
  SubjectCharacter character;
  const FormSpec& spec;
  std::size_t cycle_count;  // arc cycles for this piece (>= 1)

  /// @brief Resolve the arc design-values for a cycle of this piece.
  /// @param cycle_index Cycle index in [0, cycle_count).
  /// @return The ArcPoint for the cycle.
  ArcPoint arc(std::size_t cycle_index) const { return arcPoint(cycle_index, cycle_count); }
};

// One builder per FormType. Each assembles the (Material, HarmonicPlan,
// VoicePlan) triple plus meter for its form.
//
// Every declaration below has a dedicated builder that honours ResolvedRequest
// length, mode, character, and the arc curve; forms are not collapsed into a
// shared generator.
HarnessFixture buildFugueForm(const ResolvedRequest& req);
HarnessFixture buildPreludeAndFugueForm(const ResolvedRequest& req);
HarnessFixture buildTrioSonataForm(const ResolvedRequest& req);
HarnessFixture buildChoralePreludeForm(const ResolvedRequest& req);
HarnessFixture buildToccataAndFugueForm(const ResolvedRequest& req);
HarnessFixture buildPassacagliaForm(const ResolvedRequest& req);
HarnessFixture buildFantasiaAndFugueForm(const ResolvedRequest& req);
HarnessFixture buildCelloPreludeForm(const ResolvedRequest& req);
HarnessFixture buildChaconneForm(const ResolvedRequest& req);
HarnessFixture buildGoldbergVariationsForm(const ResolvedRequest& req);

// Per-variation realization kind for the Goldberg variation framework. The
// variation COUNT is length-driven (one block per 4-bar aria-bass cycle after
// the aria). The per-variation dispatch distinguishes figuration, the nine
// rising canons, and the final Quodlibet slot.
enum class GoldbergVariationKind : std::uint8_t {
  Figuration = 0,
  Canon = 1,
  Quodlibet = 2,
};

/**
 * @brief Classify the variation block at a given variation index.
 * @param variation_index Zero-based index of the variation block (the aria is
 *        not a variation and is never passed here; index 0 is the first
 *        post-aria variation).
 * @return Figuration, Canon (variations 3..27 by threes), or Quodlibet
 *         (variation 30).
 * @note Pure function of the index, exposed so the dispatch table is unit
 *       testable without reaching into the builder.
 */
GoldbergVariationKind goldbergVariationKind(std::size_t variation_index);

}  // namespace bach::composer

#endif  // BACH_COMPOSER_FORM_BUILDERS_H
