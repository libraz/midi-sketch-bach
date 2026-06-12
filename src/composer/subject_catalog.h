#ifndef BACH_COMPOSER_SUBJECT_CATALOG_H
#define BACH_COMPOSER_SUBJECT_CATALOG_H

#include <array>    // IWYU pragma: keep (used by the textual .inc below)
#include <cstdint>  // IWYU pragma: keep (used by the textual .inc below)

#include "core/basic_types.h"  // IWYU pragma: keep (Tick, used by the .inc below)

namespace bach::composer::tables {

// Qualified fugue-subject catalog (pitches + per-subject rhythm rows) plus
// the per-character kSubjectClass* index arrays consumed by subjectIndexFor.
// Entries 0-4 of each mode mirror the shipped 5-subject catalogs
// (kFugueCompleteSubjects / kSubjectsMinor) as backward-compatible anchors;
// the remaining entries were synthesized offline from corpus subject-window
// statistics and qualified through the full production pipeline (validation
// plus the texture-gate axes on every fugue-family form). Regenerate with
// `python3 scripts/bach_tools.py qualify-subjects --catalog-out
// src/composer/tables/subject_catalog.inc`.
#include "composer/tables/subject_catalog.inc"

}  // namespace bach::composer::tables

#endif  // BACH_COMPOSER_SUBJECT_CATALOG_H
