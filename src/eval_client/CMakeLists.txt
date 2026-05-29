# ============================================================================
# bach_mcp_client_lib - thin client for external bach-mcp evaluator
# ============================================================================
#
# This library invokes the external bach-mcp CLI as a subprocess and parses
# the JSON result. It does NOT contain evaluation logic, MCP protocol code,
# or ML inference. Total source size capped at ~300 lines (per rebuild plan).
#
# Allowed dependencies: composer/, core/json_*, standard library.
# Forbidden: any legacy src/fugue/, src/constraint/, src/forms/.

set(BACH_MCP_CLIENT_SOURCES
  external_evaluator.cpp
)

foreach(_src ${BACH_MCP_CLIENT_SOURCES})
  if(_src MATCHES "(^|/)(fugue|constraint|forms|organ|ornament|solo_string)/.*\\.cpp$")
    message(FATAL_ERROR
      "bach_mcp_client_lib forbidden legacy source: ${_src}")
  endif()
endforeach()

add_library(bach_mcp_client_lib STATIC ${BACH_MCP_CLIENT_SOURCES})

target_include_directories(bach_mcp_client_lib
  PUBLIC
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}/generated
)

target_compile_features(bach_mcp_client_lib PUBLIC cxx_std_17)

# bach_mcp_client_lib may depend on composer types (it consumes generated.json
# produced from composer output), but not on legacy bach_lib.
target_link_libraries(bach_mcp_client_lib PUBLIC bach_composer_lib)
