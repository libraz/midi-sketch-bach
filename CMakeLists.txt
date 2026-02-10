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
# Source tree
# ============================================================================

include_directories(${CMAKE_SOURCE_DIR}/src)

add_subdirectory(src)

# ============================================================================
# Testing (Google Test via FetchContent)
# ============================================================================

if(BUILD_TESTING)
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
