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
  core/note_source.cpp
  core/note_creator.cpp

  # midi/
  midi/midi_writer.cpp
  midi/midi_reader.cpp
  midi/midi_stream.cpp

  # instrument/keyboard/
  instrument/keyboard/piano_model.cpp
  instrument/keyboard/keyboard_note_factory.cpp
  instrument/keyboard/organ_model.cpp
  instrument/keyboard/harpsichord_model.cpp

  # instrument/fretted/
  instrument/fretted/fretted_instrument_base.cpp
  instrument/fretted/guitar_model.cpp
  instrument/fretted/fretted_note_factory.cpp

  # counterpoint/
  counterpoint/counterpoint_state.cpp
  counterpoint/i_rule_evaluator.cpp
  counterpoint/fux_rule_evaluator.cpp
  counterpoint/species_rules.cpp
  counterpoint/collision_resolver.cpp
  counterpoint/voice_manager.cpp
  counterpoint/motion_analyzer.cpp
  counterpoint/counterpoint_validator.cpp

  # counterpoint/ (cantus firmus)
  counterpoint/cantus_firmus.cpp

  # fugue/
  fugue/fugue_config.cpp
  fugue/subject.cpp
  fugue/subject_validator.cpp
  fugue/answer.cpp

  # Future sources added here as modules are implemented:
  # harmony/
  # organ/
  # ornament/
  # transform/
  # forms/
  # solo_string/flow/
  # solo_string/arch/
  # analysis/
)

add_library(bach_lib STATIC ${BACH_LIB_SOURCES})

target_include_directories(bach_lib
  PUBLIC
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}/generated
)

target_compile_features(bach_lib PUBLIC cxx_std_17)

# ============================================================================
# bach_cli - Command-line interface
# ============================================================================

if(BUILD_CLI)
  add_executable(bach_cli cli_main.cpp)
  target_link_libraries(bach_cli PRIVATE bach_lib)
endif()
