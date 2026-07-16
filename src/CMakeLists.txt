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
  core/instrument_program.cpp

  # harmony/
  harmony/chord_types.cpp
  harmony/key.cpp
  harmony/harmonic_timeline.cpp

  # midi/
  midi/midi_writer.cpp
  midi/midi_reader.cpp
  midi/midi_stream.cpp
  midi/velocity_curve.cpp
)

add_library(bach_lib STATIC ${BACH_LIB_SOURCES})

target_include_directories(bach_lib
  PUBLIC
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}/generated
)

target_compile_features(bach_lib PUBLIC cxx_std_17)

# ============================================================================
# Composer rebuild targets (isolated from bach_lib)
# ============================================================================
#
# Boundaries enforced by ci/check_composer_isolation.sh (registered as a CTest
# in tests/CMakeLists.txt) and by per-target CMakeLists.txt guards.

add_subdirectory(composer)

# ============================================================================
# Shared shipped-product application service
# ============================================================================

add_library(bach_application_lib STATIC
  application/composition_service.cpp
)
target_include_directories(bach_application_lib
  PUBLIC
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}/generated
)
target_link_libraries(bach_application_lib
  PUBLIC
    bach_lib
    bach_composer_lib
)
target_compile_features(bach_application_lib PUBLIC cxx_std_17)

# ============================================================================
# WASM executable
# ============================================================================

if(BUILD_WASM)
  add_executable(bach bach_c.cpp)
  target_include_directories(bach PUBLIC
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}/generated
  )
  target_compile_definitions(bach PRIVATE BACH_WASM)
  target_compile_features(bach PUBLIC cxx_std_17)

  # bach_c.cpp drives the composer subsystem; link the composer library.
  target_link_libraries(bach PRIVATE bach_application_lib)

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
  target_link_libraries(bach_cli PRIVATE bach_application_lib)
endif()
