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
  core/json_parser_test.cpp
  core/note_source_test.cpp
  core/instrument_program_test.cpp
  core/rng_util_test.cpp

  # midi/
  midi/midi_writer_test.cpp
  midi/midi_reader_test.cpp
  midi/midi_cc_write_test.cpp
  midi/velocity_curve_test.cpp

  # harmony/
  harmony/chord_types_test.cpp
  harmony/key_test.cpp
  harmony/harmonic_event_test.cpp
  harmony/harmonic_timeline_test.cpp
  harmony/extended_chord_test.cpp
)

add_executable(bach_tests ${BACH_TEST_SOURCES})

target_link_libraries(bach_tests
  PRIVATE
    bach_lib
    GTest::gtest_main
)

target_include_directories(bach_tests
  PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}/generated
    ${CMAKE_SOURCE_DIR}/tests
)

gtest_discover_tests(bach_tests)

# ============================================================================
# Native C API product-path tests
# ============================================================================

find_package(Threads REQUIRED)

add_executable(bach_c_api_tests
  c_api/bach_c_api_test.cpp
  ${CMAKE_SOURCE_DIR}/src/bach_c.cpp
)

target_link_libraries(bach_c_api_tests
  PRIVATE
    bach_application_lib
    GTest::gtest_main
    Threads::Threads
)

target_include_directories(bach_c_api_tests
  PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}/generated
    ${CMAKE_SOURCE_DIR}/tests
)

gtest_discover_tests(bach_c_api_tests)

add_executable(bach_application_tests
  application/composition_service_test.cpp
)
target_link_libraries(bach_application_tests
  PRIVATE
    bach_application_lib
    GTest::gtest_main
)
target_include_directories(bach_application_tests
  PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}/generated
    ${CMAKE_SOURCE_DIR}/tests
)
target_compile_definitions(bach_application_tests
  PRIVATE
    BACH_CLI_PATH="$<TARGET_FILE:bach_cli>"
)
add_dependencies(bach_application_tests bach_cli)
gtest_discover_tests(bach_application_tests)

# ============================================================================
# Composer rebuild test targets (isolated from bach_lib)
# ============================================================================

add_subdirectory(composer)

# ============================================================================
# Composer isolation guard (registered as a CTest)
# ============================================================================

add_test(
  NAME composer_isolation_guard
  COMMAND ${CMAKE_SOURCE_DIR}/ci/check_composer_isolation.sh
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)
