# ============================================================================
# bach_composer_lib - composer engine (rebuild target, isolated from legacy)
# ============================================================================
#
# This library MUST NOT include or link any source from src/fugue/,
# src/constraint/, src/forms/, src/organ/, src/ornament/, src/solo_string/,
# or src/generator.cpp. The boundary is enforced both at CMake-configure time
# below and at CTest time via ci/check_composer_isolation.sh.
#
# Cross-tree reuse is limited to core/ helpers (see the json_helpers
# note at the bottom of this file).

set(BACH_COMPOSER_SOURCES
  composer.cpp
  harmonic_plan.cpp
  material.cpp
  voice_intent.cpp
  candidate_search.cpp
  free_counterpoint_search.cpp
  texture_helpers.cpp
  rule_helpers.cpp
  stream_segregation.cpp
  nct_detector.cpp
  motif_ops.cpp
  tonal_answer.cpp
  chord_voicing.cpp
  validator.cpp
  form_idiom_rules.cpp
  counterpoint_budget.cpp
  renderer.cpp
  json_export.cpp
  ornament_pass.cpp
  figuration.cpp
  figuration_palette.cpp
  harness_fixture.cpp
  harness_phase_spec.cpp
  form_director.cpp
  form_ground_variations.cpp
  form_linear.cpp
  form_cantus.cpp
  form_goldberg.cpp
  bar_material.cpp
  arrival_relief.cpp
  form_fugue.cpp
  form_sectional.cpp
  minor_material.cpp
  character_profile.cpp
  expression_events.cpp
)

# CMake-time isolation guard: refuse any forbidden legacy source.
foreach(_src ${BACH_COMPOSER_SOURCES})
  if(_src MATCHES "(^|/)(fugue|constraint|forms|organ|ornament|solo_string)/.*\\.cpp$")
    message(FATAL_ERROR
      "bach_composer_lib forbidden legacy source: ${_src}")
  endif()
  if(_src MATCHES "^generator\\.cpp$" OR _src MATCHES "^bach_c\\.cpp$")
    message(FATAL_ERROR
      "bach_composer_lib forbidden legacy entry point: ${_src}")
  endif()
endforeach()

add_library(bach_composer_lib STATIC ${BACH_COMPOSER_SOURCES})

target_include_directories(bach_composer_lib
  PUBLIC
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}/generated
)

target_compile_features(bach_composer_lib PUBLIC cxx_std_17)

# bach_composer_lib emits its own JSON via core/json_helpers.h. The helper
# is deliberately shared, but linking against bach_lib would drag in the
# legacy fugue/constraint trees. Compile the helper directly into
# bach_composer_lib so the legacy lib stays out.
target_sources(bach_composer_lib PRIVATE
  ${CMAKE_SOURCE_DIR}/src/core/json_helpers.cpp
  ${CMAKE_SOURCE_DIR}/src/core/perfect_motion.cpp
  # basic_types.cpp carries the form -> default-instrument mapping. A test that
  # measures the shipped note array has to ornament it at the density the
  # product will, and instrument selects that density: without this the only
  # way to name an instrument here is to copy the mapping, which is how a
  # measurement drifts away from the music it claims to describe.
  ${CMAKE_SOURCE_DIR}/src/core/basic_types.cpp
)
