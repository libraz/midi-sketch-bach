#ifndef BACH_COMPOSER_FORM_FUGUE_H
#define BACH_COMPOSER_FORM_FUGUE_H

/// @file
/// @brief Layout rules the fugue-family builders share, and the fugue section
///        every builder that states one assembles through.

#include "composer/form_builders.h"
#include "composer/harness_fixture.h"
#include "composer/span.h"
#include "composer/texture_helpers.h"
#include "core/basic_types.h"

namespace bach::composer {

/// @brief Bars a related-key approach (V/X -> X) occupies before its entry.
constexpr int kRelatedKeyApproachBars = 2;

/// @brief Shortest fugue section that carries a three-entry exposition and its coda.
constexpr int kMinFugueSectionBars = 20;

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

/// @brief Accumulator every builder that states a fugue section writes into.
///
/// Span ids and the next-id counter are shared, so sections concatenated into
/// one fixture keep unique ids.
struct FugueAssembly {
  HarnessFixture* out = nullptr;
  SpanId* next_id = nullptr;
  /// Every already-placed note (thematic statements and figuration), so a line
  /// built later in the deterministic voice order reads what every earlier
  /// voice sounds at a tick: the figuration anchor picks a tone consonant with
  /// the concurrent theme tone and avoids parallel fifths and octaves against it.
  ThemeToneRegistry theme_tones;
};

/// @brief Position of a form among the forms that attach a fugue section.
///
/// The section's seed-driven layout reads the request seed advanced by one
/// subject period per ordinal, so two forms with equal section lengths never
/// state the same fugue at one seed.
///
/// @param form The requested form.
/// @return 1 for ToccataAndFugue, 2 for FantasiaAndFugue, 0 for every other form.
int fugueSectionOrdinal(FormType form);

/// @brief Append a three-voice fugue: exposition, entry/episode cycles on the
///        corpus interval table, coda.
///
/// Emits its own harmony, key areas and spans for bars
/// [first_bar, first_bar + bars); the home key is C in req.mode.
///
/// @param asm_ctx Assembly the section is written into.
/// @param first_bar First bar of the section in the piece.
/// @param bars Section length; at least kMinFugueSectionBars for a three-entry exposition.
/// @param req Resolved request; its form selects the section's seed rotation.
void appendFugueSection(FugueAssembly& asm_ctx, int first_bar, int bars,
                        const ResolvedRequest& req);

/// @brief Settle the free lines against the statements: local-key spelling,
///        theme inflections, handoffs, seams, parallels and unsupported
///        sevenths.
///
/// @param asm_ctx Assembly whose free lines and harmony are settled in place.
/// @param first_bar Material and chords starting before this bar are left as
///        their builder wrote them.
void settleFugueSection(FugueAssembly& asm_ctx, int first_bar);

}  // namespace bach::composer

#endif  // BACH_COMPOSER_FORM_FUGUE_H
