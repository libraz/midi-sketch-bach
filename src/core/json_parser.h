// Minimal flat-object JSON parser for config input (no external dependencies).
//
// Handles only the subset needed for GeneratorConfig: flat object with
// string, number, and boolean values. Does not support nested objects or arrays.

#ifndef BACH_CORE_JSON_PARSER_H
#define BACH_CORE_JSON_PARSER_H

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>

namespace bach {

/// @brief A single JSON value (string, number, or boolean).
struct JsonValue {
  enum Type { String, Number, Bool, Null };
  Type type = Null;
  std::string string_val;
  double number_val = 0.0;
  bool bool_val = false;

  /// @brief Get value as integer, with default.
  int asInt(int default_val = 0) const;

  /// @brief Get value as unsigned integer, with default.
  uint32_t asUint(uint32_t default_val = 0) const;

  /// @brief Get value as boolean, with default.
  bool asBool(bool default_val = false) const;

  /// @brief Get value as string, with default.
  std::string asString(const std::string& default_val = "") const;
};

/// @brief Parse a flat JSON object into a key-value map.
///
/// Only handles top-level keys with string, number, or boolean values.
/// Ignores nested objects and arrays (skips them).
///
/// @param json Pointer to JSON string.
/// @param length Length of JSON string.
/// @return Map of key-value pairs. Empty map on parse error.
std::map<std::string, JsonValue> parseJsonObject(const char* json, size_t length);

}  // namespace bach

#endif  // BACH_CORE_JSON_PARSER_H
