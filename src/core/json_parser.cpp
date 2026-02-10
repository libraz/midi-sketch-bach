// Implementation of minimal flat-object JSON parser.

#include "core/json_parser.h"

#include <cctype>

namespace bach {

int JsonValue::asInt(int default_val) const {
  if (type == Number) return static_cast<int>(number_val);
  return default_val;
}

uint32_t JsonValue::asUint(uint32_t default_val) const {
  if (type == Number) return static_cast<uint32_t>(number_val);
  return default_val;
}

bool JsonValue::asBool(bool default_val) const {
  if (type == Bool) return bool_val;
  return default_val;
}

std::string JsonValue::asString(const std::string& default_val) const {
  if (type == String) return string_val;
  return default_val;
}

namespace {

/// @brief Skip whitespace in JSON string.
void skipWhitespace(const char* json, size_t length, size_t& pos) {
  while (pos < length && std::isspace(static_cast<unsigned char>(json[pos]))) {
    ++pos;
  }
}

/// @brief Parse a JSON string literal (expects pos at opening quote).
/// @return Parsed string, pos advanced past closing quote.
std::string parseString(const char* json, size_t length, size_t& pos) {
  if (pos >= length || json[pos] != '"') return "";
  ++pos;  // skip opening quote

  std::string result;
  while (pos < length && json[pos] != '"') {
    if (json[pos] == '\\' && pos + 1 < length) {
      ++pos;
      switch (json[pos]) {
        case '"':  result += '"'; break;
        case '\\': result += '\\'; break;
        case '/':  result += '/'; break;
        case 'n':  result += '\n'; break;
        case 't':  result += '\t'; break;
        case 'r':  result += '\r'; break;
        default:   result += json[pos]; break;
      }
    } else {
      result += json[pos];
    }
    ++pos;
  }

  if (pos < length) ++pos;  // skip closing quote
  return result;
}

/// @brief Parse a JSON number (integer or floating point).
JsonValue parseNumber(const char* json, size_t length, size_t& pos) {
  JsonValue val;
  val.type = JsonValue::Number;

  size_t start = pos;
  if (pos < length && json[pos] == '-') ++pos;
  while (pos < length && std::isdigit(static_cast<unsigned char>(json[pos]))) ++pos;
  if (pos < length && json[pos] == '.') {
    ++pos;
    while (pos < length && std::isdigit(static_cast<unsigned char>(json[pos]))) ++pos;
  }

  std::string num_str(json + start, pos - start);
  val.number_val = std::stod(num_str);
  return val;
}

/// @brief Skip a JSON value (for nested objects/arrays we don't parse).
void skipValue(const char* json, size_t length, size_t& pos) {
  skipWhitespace(json, length, pos);
  if (pos >= length) return;

  if (json[pos] == '"') {
    parseString(json, length, pos);
  } else if (json[pos] == '{' || json[pos] == '[') {
    char open = json[pos];
    char close = (open == '{') ? '}' : ']';
    int depth = 1;
    ++pos;
    while (pos < length && depth > 0) {
      if (json[pos] == '"') {
        parseString(json, length, pos);
        continue;
      }
      if (json[pos] == open) ++depth;
      if (json[pos] == close) --depth;
      ++pos;
    }
  } else {
    // number, true, false, null
    while (pos < length && json[pos] != ',' && json[pos] != '}' && json[pos] != ']' &&
           !std::isspace(static_cast<unsigned char>(json[pos]))) {
      ++pos;
    }
  }
}

}  // namespace

std::map<std::string, JsonValue> parseJsonObject(const char* json, size_t length) {
  std::map<std::string, JsonValue> result;
  if (!json || length == 0) return result;

  size_t pos = 0;
  skipWhitespace(json, length, pos);
  if (pos >= length || json[pos] != '{') return result;
  ++pos;  // skip '{'

  while (pos < length) {
    skipWhitespace(json, length, pos);
    if (pos >= length || json[pos] == '}') break;

    // Skip comma between entries
    if (json[pos] == ',') {
      ++pos;
      skipWhitespace(json, length, pos);
    }

    if (pos >= length || json[pos] == '}') break;

    // Parse key
    if (json[pos] != '"') break;
    std::string key = parseString(json, length, pos);

    // Skip colon
    skipWhitespace(json, length, pos);
    if (pos >= length || json[pos] != ':') break;
    ++pos;
    skipWhitespace(json, length, pos);

    if (pos >= length) break;

    // Parse value
    if (json[pos] == '"') {
      JsonValue val;
      val.type = JsonValue::String;
      val.string_val = parseString(json, length, pos);
      result[key] = val;
    } else if (json[pos] == 't' || json[pos] == 'f') {
      JsonValue val;
      val.type = JsonValue::Bool;
      if (json[pos] == 't') {
        val.bool_val = true;
        pos += 4;  // skip "true"
      } else {
        val.bool_val = false;
        pos += 5;  // skip "false"
      }
      result[key] = val;
    } else if (json[pos] == 'n') {
      JsonValue val;
      val.type = JsonValue::Null;
      pos += 4;  // skip "null"
      result[key] = val;
    } else if (json[pos] == '{' || json[pos] == '[') {
      // Nested structure - skip it
      skipValue(json, length, pos);
    } else {
      // Number
      result[key] = parseNumber(json, length, pos);
    }
  }

  return result;
}

}  // namespace bach
