# ============================================================================
# bach_tests - Google Test suite
# ============================================================================

include(GoogleTest)

set(BACH_TEST_SOURCES
  # core/
  core/basic_types_test.cpp
  core/duration_scale_test.cpp
  core/pitch_utils_test.cpp
  core/interval_test.cpp
  core/scale_test.cpp
  core/json_helpers_test.cpp
  core/note_source_test.cpp
  core/note_creator_test.cpp
  core/rng_util_test.cpp
  core/phrase_structure_test.cpp

  # midi/
  midi/midi_writer_test.cpp
  midi/midi_reader_test.cpp

  # instrument/
  instrument/organ_model_test.cpp
  instrument/harpsichord_model_test.cpp
  instrument/piano_model_test.cpp
  instrument/playability_test.cpp
  instrument/cello_model_test.cpp
  instrument/violin_model_test.cpp
  instrument/bowed_note_factory_test.cpp

  # counterpoint/
  counterpoint/counterpoint_state_test.cpp
  counterpoint/i_rule_evaluator_test.cpp
  counterpoint/voice_manager_test.cpp
  counterpoint/fux_rule_evaluator_test.cpp
  counterpoint/bach_rule_evaluator_test.cpp
  counterpoint/species_rules_test.cpp
  counterpoint/species_rules_nht_test.cpp
  counterpoint/motion_analyzer_test.cpp
  counterpoint/collision_resolver_test.cpp
  counterpoint/counterpoint_validator_test.cpp
  counterpoint/melodic_context_test.cpp

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
  fugue/fugue_duration_scale_test.cpp
  fugue/fugue_energy_curve_test.cpp
  fugue/cadence_plan_test.cpp
  fugue/episode_characteristic_motif_test.cpp
  fugue/stretto_fragment_test.cpp

  # transform/
  transform/motif_transform_test.cpp

  # organ/
  organ/manual_test.cpp
  organ/registration_test.cpp

  # analysis/
  analysis/voice_independence_test.cpp
  analysis/fail_report_test.cpp
  analysis/counterpoint_analyzer_test.cpp
  analysis/fugue_analyzer_test.cpp
  analysis/dissonance_analyzer_test.cpp
  analysis/analysis_runner_test.cpp
  analysis/implied_voice_analyzer_test.cpp
  analysis/cross_relation_test.cpp

  # harmony/
  harmony/chord_types_test.cpp
  harmony/key_test.cpp
  harmony/harmonic_event_test.cpp
  harmony/harmonic_function_test.cpp
  harmony/harmonic_timeline_test.cpp

  # forms/
  forms/prelude_test.cpp
  forms/trio_sonata_test.cpp
  forms/chorale_prelude_test.cpp

  # generator (unified entry point)
  generator_test.cpp

  # ornament/
  ornament/ornament_types_test.cpp
  ornament/trill_test.cpp
  ornament/mordent_test.cpp
  ornament/turn_test.cpp
  ornament/ornament_engine_test.cpp
  ornament/schleifer_test.cpp

  # solo_string/flow/
  solo_string/flow/arpeggio_flow_config_test.cpp
  solo_string/flow/arpeggio_pattern_test.cpp
  solo_string/flow/flow_analyzer_test.cpp
  solo_string/flow/harmonic_arpeggio_engine_test.cpp
  solo_string/flow/global_arc_test.cpp

  # solo_string/arch/
  solo_string/arch/texture_generator_test.cpp
  solo_string/arch/chaconne_analyzer_test.cpp
  solo_string/arch/ground_bass_test.cpp
  solo_string/arch/variation_types_test.cpp
  solo_string/arch/chaconne_config_test.cpp
  solo_string/arch/scaled_variation_plan_test.cpp
  solo_string/arch/chaconne_engine_test.cpp
  solo_string/arch/destruction_resistance_test.cpp
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
