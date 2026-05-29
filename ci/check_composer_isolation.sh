#!/usr/bin/env bash
# ============================================================================
# Composer isolation guard
# ============================================================================
#
# Asserts the boundaries declared in backup/reuse_contract.md:
#
#   1. src/composer/ MUST NOT #include from legacy trees
#      (fugue, constraint, forms, organ, ornament, solo_string,
#       core/note_creator, core/note_source, core/markov_tables,
#       core/figure_*, core/melodic_state, core/bach_vocabulary,
#       counterpoint/collision_resolver, counterpoint/leap_resolution,
#       counterpoint/repeated_note_repair, counterpoint/counterpoint_state,
#       generator.h, bach_c.cpp).
#
#   2. Legacy trees MUST NOT #include from composer/.
#
# Exit 0 on clean, nonzero on any violation.

set -euo pipefail

REPO_ROOT="$(git rev-parse --show-toplevel)"
cd "${REPO_ROOT}"

FAIL=0

emit_failure() {
  echo "FAIL: $1" >&2
  FAIL=1
}

# -----------------------------------------------------------------------------
# Rule 1: composer/ forbidden legacy includes
# -----------------------------------------------------------------------------

FORBIDDEN_INCLUDE_PATTERNS=(
  '#include "fugue/'
  '#include "constraint/'
  '#include "forms/'
  '#include "organ/'
  '#include "ornament/'
  '#include "solo_string/'
  '#include "core/note_creator'
  '#include "core/note_source'
  '#include "core/markov_tables'
  '#include "core/figure_'
  '#include "core/melodic_state'
  '#include "core/bach_vocabulary'
  '#include "counterpoint/collision_resolver'
  '#include "counterpoint/leap_resolution'
  '#include "counterpoint/repeated_note_repair'
  '#include "counterpoint/counterpoint_state'
  '#include "generator.h'
  '#include "bach_c.'
)

if [ -d "src/composer" ]; then
  for pattern in "${FORBIDDEN_INCLUDE_PATTERNS[@]}"; do
    if grep -rln --include='*.h' --include='*.cpp' -F "${pattern}" src/composer/ 2>/dev/null; then
      emit_failure "src/composer/ contains forbidden include: ${pattern}"
    fi
  done
fi

# -----------------------------------------------------------------------------
# Rule 2: legacy trees must not include from composer/
# -----------------------------------------------------------------------------

LEGACY_TREES=(
  src/fugue
  src/constraint
  src/forms
  src/organ
  src/ornament
  src/solo_string
)

for tree in "${LEGACY_TREES[@]}"; do
  if [ -d "${tree}" ]; then
    if grep -rln --include='*.h' --include='*.cpp' -E '#include "composer/' "${tree}" 2>/dev/null; then
      emit_failure "${tree}/ includes from composer/ (reverse direction forbidden)"
    fi
  fi
done

if [ -f "src/generator.cpp" ]; then
  if grep -ln -E '#include "composer/' src/generator.{h,cpp} 2>/dev/null; then
    emit_failure "src/generator.{h,cpp} includes from composer/"
  fi
fi

# -----------------------------------------------------------------------------
# Rule 3: CMake source list isolation (defense in depth)
# -----------------------------------------------------------------------------

if [ -f "src/composer/CMakeLists.txt" ]; then
  if grep -nE '(fugue|constraint|forms|organ|ornament|solo_string)/.*\.(cpp|h)' src/composer/CMakeLists.txt; then
    emit_failure "src/composer/CMakeLists.txt lists a legacy-tree source"
  fi
fi

# -----------------------------------------------------------------------------

if [ "${FAIL}" -ne 0 ]; then
  echo "composer isolation: VIOLATIONS DETECTED" >&2
  exit 1
fi

echo "composer isolation: OK"
exit 0
