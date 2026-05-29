# ============================================================================
# bach_mcp_client_tests - test suite for the external evaluator thin client
# ============================================================================

set(BACH_MCP_CLIENT_TEST_SOURCES
  external_evaluator_test.cpp
  twenty_seed_harness_test.cpp
)

add_executable(bach_mcp_client_tests ${BACH_MCP_CLIENT_TEST_SOURCES})

target_link_libraries(bach_mcp_client_tests
  PRIVATE
    bach_mcp_client_lib
    GTest::gtest_main
)

target_include_directories(bach_mcp_client_tests
  PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}/generated
    ${CMAKE_SOURCE_DIR}/tests
)

# Used by external_evaluator_test to locate the sibling bach-mcp repo
# (../bach-mcp/dist/index.js). If not present the test is SKIPPED.
target_compile_definitions(bach_mcp_client_tests
  PRIVATE
    BACH_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)

gtest_discover_tests(bach_mcp_client_tests)
