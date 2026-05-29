#ifndef BACH_EVAL_CLIENT_EXTERNAL_EVALUATOR_H
#define BACH_EVAL_CLIENT_EXTERNAL_EVALUATOR_H

#include <string>

namespace bach::eval_client {

// Configuration for the external bach-mcp evaluator client.
// All fields are optional; empty values trigger fallback lookups.
struct EvaluatorConfig {
  // Absolute path to bach-mcp dist/index.js. If empty, falls back to the
  // BACH_MCP_INDEX_JS environment variable. There is no default sibling
  // lookup at runtime — callers must specify the path explicitly to avoid
  // surprising relative-path resolution in deployed contexts.
  std::string bach_mcp_index_js;

  // Node binary. If empty, "node" is used (PATH lookup).
  std::string node_binary;
};

// Result of a single bach-mcp invocation.
struct EvaluatorResult {
  bool invoked = false;  // Subprocess started and exited 0.
  bool pass = false;     // top-level "pass" field (heuristic gate).
  double score = 0.0;    // top-level "score" field (heuristic 0..1).
  // model_score.probability and model_score.pass from the response.
  // The model probability is the metric phase closure conditions cite as
  // "Bach-side 確率"; the heuristic score above is the looser fast gate.
  double model_probability = 0.0;
  bool model_pass = false;
  std::string schema_version;  // "schema_version" field from response.
  std::string raw_stdout;      // Full subprocess stdout (audit log).
  std::string error;           // Populated when invoked == false.
};

// Synchronously runs `node <bach_mcp_index_js> score <generated_json_path>`,
// collects stdout, and extracts the top-level pass/score/schema_version
// fields. Stderr is folded into stdout for diagnostic surfacing. Never
// throws; failures land in EvaluatorResult::error.
//
// The caller MUST pass a path to a generated.v1 JSON file (the evaluator
// schema). Passing any audit-log JSON is a contract violation enforced
// by ci/check_composer_isolation.sh rule 4.
EvaluatorResult evaluateGeneratedJson(const std::string& generated_json_path,
                                      const EvaluatorConfig& config = {});

// Sentinel symbol matching composer.h. Kept for skeleton-era tests.
bool isMcpClientLibLinked();

}  // namespace bach::eval_client

#endif  // BACH_EVAL_CLIENT_EXTERNAL_EVALUATOR_H
