# ============================================================================
# bach_lib - Core static library
# ============================================================================

set(BACH_LIB_SOURCES
  # core/
  core/basic_types.cpp
  core/pitch_utils.cpp
  core/interval.cpp
  core/scale.cpp
  core/json_helpers.cpp
  core/json_parser.cpp
  core/note_source.cpp
  core/note_creator.cpp
  core/rhythm_pattern.cpp

  # midi/
  midi/midi_writer.cpp
  midi/midi_reader.cpp
  midi/midi_stream.cpp
  midi/velocity_curve.cpp

  # instrument/keyboard/
  instrument/keyboard/piano_model.cpp
  instrument/keyboard/keyboard_note_factory.cpp
  instrument/keyboard/organ_model.cpp
  instrument/keyboard/harpsichord_model.cpp

  # instrument/fretted/
  instrument/fretted/fretted_instrument_base.cpp
  instrument/fretted/guitar_model.cpp
  instrument/fretted/fretted_note_factory.cpp

  # instrument/bowed/
  instrument/bowed/bowed_string_instrument_base.cpp
  instrument/bowed/cello_model.cpp
  instrument/bowed/violin_model.cpp
  instrument/bowed/bowed_note_factory.cpp
  instrument/bowed/bow_direction.cpp
  instrument/bowed/harmonics.cpp

  # counterpoint/
  counterpoint/counterpoint_state.cpp
  counterpoint/i_rule_evaluator.cpp
  counterpoint/fux_rule_evaluator.cpp
  counterpoint/bach_rule_evaluator.cpp
  counterpoint/species_rules.cpp
  counterpoint/collision_resolver.cpp
  counterpoint/parallel_repair.cpp
  counterpoint/suspension_chain.cpp
  counterpoint/voice_manager.cpp
  counterpoint/motion_analyzer.cpp
  counterpoint/counterpoint_validator.cpp

  # counterpoint/ (cantus firmus)
  counterpoint/cantus_firmus.cpp
  counterpoint/melodic_context.cpp

  # fugue/
  fugue/fugue_config.cpp
  fugue/archetype_policy.cpp
  fugue/archetype_scorer.cpp
  fugue/fugue_structure.cpp
  fugue/subject.cpp
  fugue/subject_params.cpp
  fugue/subject_validator.cpp
  fugue/answer.cpp
  fugue/countersubject.cpp
  fugue/tonal_plan.cpp
  fugue/episode.cpp
  fugue/exposition.cpp
  fugue/middle_entry.cpp
  fugue/stretto.cpp
  fugue/cadence_plan.cpp
  fugue/voice_registers.cpp
  fugue/motif_pool.cpp
  fugue/fortspinnung.cpp
  fugue/motif_template.cpp
  fugue/fugue_generator.cpp

  # transform/
  transform/motif_transform.cpp
  transform/sequence.cpp

  # organ/
  organ/manual.cpp
  organ/organ_techniques.cpp
  organ/registration.cpp

  # analysis/
  analysis/analysis_utils.cpp
  analysis/voice_independence.cpp
  analysis/fail_report.cpp
  analysis/counterpoint_analyzer.cpp
  analysis/fugue_analyzer.cpp
  analysis/dissonance_analyzer.cpp
  analysis/analysis_runner.cpp
  analysis/implied_voice_analyzer.cpp
  analysis/voice_harmony_analyzer.cpp
  analysis/cadence_detector.cpp

  # harmony/
  harmony/chord_types.cpp
  harmony/key.cpp
  harmony/harmonic_timeline.cpp
  harmony/harmonic_function.cpp
  harmony/modulation_plan.cpp
  harmony/voice_leading_planner.cpp
  harmony/chord_tone_utils.cpp
  harmony/tempo_map.cpp
  harmony/harmonic_rhythm.cpp
  harmony/scale_degree_utils.cpp

  # forms/
  forms/prelude.cpp
  forms/trio_sonata.cpp
  forms/chorale_prelude.cpp
  forms/toccata.cpp
  forms/toccata_perpetuus.cpp
  forms/toccata_concertato.cpp
  forms/toccata_sectionalis.cpp
  forms/passacaglia.cpp
  forms/fantasia.cpp

  # core/ (phrase)
  core/phrase_structure.cpp

  # generator (unified entry point)
  generator.cpp

  # ornament/
  ornament/ornament_types.cpp
  ornament/trill.cpp
  ornament/mordent.cpp
  ornament/turn.cpp
  ornament/appoggiatura.cpp
  ornament/pralltriller.cpp
  ornament/ornament_engine.cpp
  ornament/schleifer.cpp
  ornament/vorschlag.cpp
  ornament/nachschlag.cpp
  ornament/compound_ornament.cpp

  # solo_string/flow/
  solo_string/flow/arpeggio_flow_config.cpp
  solo_string/flow/arpeggio_pattern.cpp
  solo_string/flow/flow_analyzer.cpp
  solo_string/flow/harmonic_arpeggio_engine.cpp
  solo_string/flow/chromatic_passing.cpp

  # solo_string/arch/
  solo_string/arch/variation_types.cpp
  solo_string/arch/ground_bass.cpp
  solo_string/arch/chaconne_config.cpp
  solo_string/arch/texture_generator.cpp
  solo_string/arch/chaconne_analyzer.cpp
  solo_string/arch/chaconne_engine.cpp

  # expression/
  expression/articulation.cpp
  expression/phrase_detector.cpp
  expression/expression_curve.cpp
)

add_library(bach_lib STATIC ${BACH_LIB_SOURCES})

target_include_directories(bach_lib
  PUBLIC
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}/generated
)

target_compile_features(bach_lib PUBLIC cxx_std_17)

# ============================================================================
# WASM executable
# ============================================================================

if(BUILD_WASM)
  add_executable(bach ${BACH_LIB_SOURCES} bach_c.cpp)
  target_include_directories(bach PUBLIC
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}/generated
  )
  target_compile_definitions(bach PRIVATE BACH_WASM)
  target_compile_features(bach PUBLIC cxx_std_17)

  # Copy to dist directory
  add_custom_command(TARGET bach POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_SOURCE_DIR}/dist
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:bach> ${CMAKE_SOURCE_DIR}/dist/
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_BINARY_DIR}/bin/bach.wasm ${CMAKE_SOURCE_DIR}/dist/
    COMMENT "Copying WASM files to dist/"
  )

# ============================================================================
# bach_cli - Command-line interface
# ============================================================================

elseif(BUILD_CLI)
  add_executable(bach_cli cli_main.cpp)
  target_link_libraries(bach_cli PRIVATE bach_lib)
endif()
