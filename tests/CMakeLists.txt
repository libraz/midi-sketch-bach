# ============================================================================
# bach_tests - Google Test suite
# ============================================================================

include(GoogleTest)

set(BACH_TEST_SOURCES
  # core/
  core/basic_types_test.cpp
  core/pitch_utils_test.cpp
  core/interval_test.cpp
  core/scale_test.cpp
  core/json_helpers_test.cpp
  core/note_source_test.cpp
  core/note_creator_test.cpp
  core/rng_util_test.cpp

  # midi/
  midi/midi_writer_test.cpp
  midi/midi_reader_test.cpp

  # instrument/
  instrument/organ_model_test.cpp
  instrument/harpsichord_model_test.cpp
  instrument/piano_model_test.cpp
  instrument/playability_test.cpp

  # counterpoint/
  counterpoint/counterpoint_state_test.cpp
  counterpoint/i_rule_evaluator_test.cpp
  counterpoint/voice_manager_test.cpp
  counterpoint/fux_rule_evaluator_test.cpp
  counterpoint/species_rules_test.cpp
  counterpoint/motion_analyzer_test.cpp
  counterpoint/collision_resolver_test.cpp
  counterpoint/counterpoint_validator_test.cpp

  # counterpoint/ (cantus firmus)
  counterpoint/cantus_firmus_test.cpp

  # fugue/
  fugue/fugue_config_test.cpp
  fugue/fugue_structure_test.cpp
  fugue/subject_test.cpp
  fugue/subject_validator_test.cpp
  fugue/answer_test.cpp
  fugue/countersubject_test.cpp
  fugue/tonal_plan_test.cpp
  fugue/episode_test.cpp
  fugue/exposition_test.cpp
  fugue/middle_entry_test.cpp
  fugue/stretto_test.cpp
  fugue/fugue_generator_test.cpp

  # transform/
  transform/motif_transform_test.cpp

  # Future test sources added here as modules are implemented:
  # harmony/
  # organ/
  # ornament/
  # forms/
  # solo_string/flow/
  # solo_string/arch/
  # analysis/
)

add_executable(bach_tests ${BACH_TEST_SOURCES})

target_link_libraries(bach_tests
  PRIVATE
    bach_lib
    GTest::gtest
    GTest::gtest_main
)

target_include_directories(bach_tests
  PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}/generated
)

gtest_discover_tests(bach_tests)
