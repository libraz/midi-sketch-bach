cmake_minimum_required(VERSION 3.15)

project(midi-sketch-bach
  VERSION 0.1.0
  DESCRIPTION "Bach Instrumental MIDI Generator"
  LANGUAGES CXX
)

# ============================================================================
# Build options
# ============================================================================

option(BUILD_CLI "Build the bach_cli executable" ON)
option(BUILD_WASM "Build WebAssembly module" OFF)
option(BUILD_TESTING "Build test suite" ON)

# ============================================================================
# Global settings
# ============================================================================

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# ============================================================================
# Compiler warnings
# ============================================================================

add_compile_options(
  -Wall
  -Wextra
  -Wpedantic
)

# ============================================================================
# Version info generation
# ============================================================================

# Retrieve git commit hash (short)
execute_process(
  COMMAND git rev-parse --short HEAD
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  OUTPUT_VARIABLE GIT_COMMIT_HASH
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_QUIET
)
if(NOT GIT_COMMIT_HASH)
  set(GIT_COMMIT_HASH "unknown")
endif()

# Build timestamp (UTC, compact format)
string(TIMESTAMP BUILD_TIMESTAMP "%Y%m%d%H%M" UTC)

configure_file(
  ${CMAKE_SOURCE_DIR}/src/core/version_info.h.in
  ${CMAKE_BINARY_DIR}/generated/core/version_info.h
  @ONLY
)

# Make generated headers available
include_directories(${CMAKE_BINARY_DIR}/generated)

# ============================================================================
# WASM configuration
# ============================================================================

if(BUILD_WASM)
  message(STATUS "Building for WebAssembly")
  set(CMAKE_EXECUTABLE_SUFFIX ".js")

  add_compile_options(-Os -flto)
  add_link_options(-Os -flto)

  add_link_options(
    -sWASM=1
    -sMODULARIZE=1
    -sEXPORT_ES6=1
    "-sEXPORTED_FUNCTIONS=['_malloc','_free','_emscripten_stack_get_current','_bach_create','_bach_destroy','_bach_generate_from_json','_bach_get_midi','_bach_free_midi','_bach_get_events','_bach_free_events','_bach_get_info','_bach_form_count','_bach_form_name','_bach_form_display','_bach_instrument_count','_bach_instrument_name','_bach_character_count','_bach_character_name','_bach_key_count','_bach_key_name','_bach_scale_count','_bach_scale_name','_bach_default_instrument_for_form','_bach_error_string','_bach_version']"
    "-sEXPORTED_RUNTIME_METHODS=['cwrap','ccall','UTF8ToString','HEAPU8','HEAPU32']"
    -sALLOW_MEMORY_GROWTH=1
    -sSTACK_SIZE=1048576
  )
endif()

# ============================================================================
# Source tree
# ============================================================================

include_directories(${CMAKE_SOURCE_DIR}/src)

add_subdirectory(src)

# ============================================================================
# Testing (Google Test via FetchContent)
# ============================================================================

if(BUILD_TESTING AND NOT BUILD_WASM)
  include(FetchContent)
  FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.14.0
  )
  # Prevent GoogleTest from overriding our compiler/linker options
  set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
  set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(googletest)

  enable_testing()
  add_subdirectory(tests)
endif()
