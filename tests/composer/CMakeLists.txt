# ============================================================================
# bach_composer_tests - test suite for the composer rebuild
# ============================================================================
#
# This test target links bach_composer_lib only. It must NOT link bach_lib
# (the legacy library). A legacy test regression must never block composer CI.

set(BACH_COMPOSER_TEST_SOURCES
  composer_skeleton_test.cpp
  voice_intent_test.cpp
  intent_descriptor_test.cpp
  composer_two_voice_test.cpp
  material_test.cpp
  json_export_test.cpp
  schema_validation_test.cpp
  rule_helpers_test.cpp
  stream_segregation_test.cpp
  nct_detector_test.cpp
  motif_ops_test.cpp
  tonal_answer_test.cpp
  chord_voicing_test.cpp
  validator_test.cpp
  candidate_search_test.cpp
  texture_expression_test.cpp
  composer_nct_test.cpp
  arpeggio_flow_test.cpp
  chaconne_arch_test.cpp
  organ_prelude_test.cpp
  organ_toccata_test.cpp
  chorale_prelude_test.cpp
  passacaglia_test.cpp
  trio_sonata_test.cpp
  fantasia_test.cpp
  suite_test.cpp
  wtc_pair_test.cpp
  goldberg_test.cpp
  form_director_test.cpp
  form_fugue_test.cpp
  form_sectional_test.cpp
  form_ground_variations_test.cpp
  form_linear_test.cpp
  form_cantus_test.cpp
  goldberg_canon_test.cpp
  minor_material_test.cpp
  character_profile_test.cpp
  meter_test.cpp
  expression_events_test.cpp
  ornament_pass_test.cpp
)

add_executable(bach_composer_tests ${BACH_COMPOSER_TEST_SOURCES})

target_link_libraries(bach_composer_tests
  PRIVATE
    bach_composer_lib
    GTest::gtest_main
)

target_include_directories(bach_composer_tests
  PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}/generated
    ${CMAKE_SOURCE_DIR}/tests
)

# schema_validation_test resolves scripts/validate_generated_json.py via
# this macro. The validator in turn resolves ../bach-mcp/schema/ relative
# to itself, so no schema path is baked into the binary.
target_compile_definitions(bach_composer_tests
  PRIVATE
    BACH_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)

gtest_discover_tests(bach_composer_tests)
