#include "eval_client/external_evaluator.h"

#include <sys/wait.h>

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>

namespace bach::eval_client {

namespace {

std::string resolveIndexJs(const std::string& explicit_path) {
  if (!explicit_path.empty())
    return explicit_path;
  if (const char* env = std::getenv("BACH_MCP_INDEX_JS"); env != nullptr) {
    return env;
  }
  return "";
}

std::string shellQuote(const std::string& s) {
  std::string out = "\"";
  for (char c : s) {
    if (c == '"' || c == '\\' || c == '$' || c == '`')
      out.push_back('\\');
    out.push_back(c);
  }
  out.push_back('"');
  return out;
}

void skipWs(std::string_view s, std::size_t* pos) {
  while (*pos < s.size() && std::isspace(static_cast<unsigned char>(s[*pos]))) {
    ++(*pos);
  }
}

// Locate the byte just after `"<key>":` at any depth. Returns
// string_view::npos if absent. Naive — does not skip nested objects,
// but the bach-mcp top-level fields we read appear before any nested
// "metrics"/"diagnostics" sections, so a first-occurrence scan is
// sufficient without pulling in a JSON dependency.
std::size_t findKey(std::string_view json, std::string_view key) {
  std::string needle;
  needle.reserve(key.size() + 3);
  needle.push_back('"');
  needle.append(key);
  needle.push_back('"');
  needle.push_back(':');
  auto pos = json.find(needle);
  if (pos == std::string_view::npos)
    return std::string_view::npos;
  pos += needle.size();
  return pos;
}

std::string extractStringField(std::string_view json, std::string_view key) {
  auto pos = findKey(json, key);
  if (pos == std::string_view::npos)
    return "";
  skipWs(json, &pos);
  if (pos >= json.size() || json[pos] != '"')
    return "";
  ++pos;
  auto end = json.find('"', pos);
  if (end == std::string_view::npos)
    return "";
  return std::string(json.substr(pos, end - pos));
}

bool extractNumberField(std::string_view json, std::string_view key, double* out) {
  auto pos = findKey(json, key);
  if (pos == std::string_view::npos)
    return false;
  skipWs(json, &pos);
  auto begin = pos;
  while (pos < json.size()) {
    const char c = json[pos];
    if (std::isdigit(static_cast<unsigned char>(c)) || c == '.' || c == '-' || c == '+' ||
        c == 'e' || c == 'E') {
      ++pos;
    } else {
      break;
    }
  }
  if (pos == begin)
    return false;
  try {
    *out = std::stod(std::string(json.substr(begin, pos - begin)));
    return true;
  } catch (...) {
    return false;
  }
}

bool extractBoolField(std::string_view json, std::string_view key, bool* out) {
  auto pos = findKey(json, key);
  if (pos == std::string_view::npos)
    return false;
  skipWs(json, &pos);
  if (json.compare(pos, 4, "true") == 0) {
    *out = true;
    return true;
  }
  if (json.compare(pos, 5, "false") == 0) {
    *out = false;
    return true;
  }
  return false;
}

// Return the substring of the object literal value associated with `key`,
// including the surrounding braces. Used to scope nested-field lookups so
// `model_score.probability` is not confused with
// `bootstrap_model_score.probability`. Returns empty view on absence or
// when the value is not an object literal.
std::string_view objectValue(std::string_view json, std::string_view key) {
  auto pos = findKey(json, key);
  if (pos == std::string_view::npos)
    return {};
  skipWs(json, &pos);
  if (pos >= json.size() || json[pos] != '{')
    return {};
  const auto start = pos;
  int depth = 0;
  for (; pos < json.size(); ++pos) {
    const char c = json[pos];
    if (c == '{') {
      ++depth;
    } else if (c == '}') {
      if (--depth == 0) {
        return json.substr(start, pos - start + 1);
      }
    }
  }
  return {};
}

}  // namespace

EvaluatorResult evaluateGeneratedJson(const std::string& generated_json_path,
                                      const EvaluatorConfig& config) {
  EvaluatorResult result;

  const std::string node = config.node_binary.empty() ? "node" : config.node_binary;
  const std::string index_js = resolveIndexJs(config.bach_mcp_index_js);

  if (index_js.empty()) {
    result.error =
        "bach-mcp index.js path not configured (set EvaluatorConfig::"
        "bach_mcp_index_js or BACH_MCP_INDEX_JS env var)";
    return result;
  }
  if (generated_json_path.empty()) {
    result.error = "generated_json_path is empty";
    return result;
  }

  // Stderr is folded into stdout so failure messages from node /
  // bach-mcp surface in EvaluatorResult::error / raw_stdout.
  const std::string cmd = shellQuote(node) + " " + shellQuote(index_js) + " score " +
                          shellQuote(generated_json_path) + " 2>&1";

  FILE* pipe = ::popen(cmd.c_str(), "r");
  if (pipe == nullptr) {
    result.error = "popen failed: " + cmd;
    return result;
  }
  std::string buffer;
  char chunk[4096];
  while (std::size_t n = std::fread(chunk, 1, sizeof(chunk), pipe)) {
    buffer.append(chunk, n);
  }
  const int status = ::pclose(pipe);
  if (status == -1) {
    result.error = "pclose failed; raw stdout/stderr:\n" + buffer;
    return result;
  }
  if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
    result.error = "bach-mcp exited nonzero; raw stdout/stderr:\n" + buffer;
    return result;
  }

  result.raw_stdout = buffer;
  result.invoked = true;
  result.schema_version = extractStringField(buffer, "schema_version");
  extractNumberField(buffer, "score", &result.score);
  extractBoolField(buffer, "pass", &result.pass);
  if (const auto model_obj = objectValue(buffer, "model_score"); !model_obj.empty()) {
    extractNumberField(model_obj, "probability", &result.model_probability);
    extractBoolField(model_obj, "pass", &result.model_pass);
  }
  return result;
}

bool isMcpClientLibLinked() {
  return true;
}

}  // namespace bach::eval_client
