// Minimal flat-object JSON parser for config input (no external dependencies).
//
// Handles only the subset needed for config input: flat object with
// string, number, and boolean values. Does not support nested objects or arrays.

#ifndef BACH_CORE_JSON_PARSER_H
#define BACH_CORE_JSON_PARSER_H

#include <cstddef>
#include <cstdint>
#include <limits>
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

  /// @brief Convert a finite integral JSON number without truncation.
  bool asInt64(std::int64_t* out) const;

  /// @brief Convert a finite integral JSON number to uint32 without wrapping.
  bool asUint32(std::uint32_t* out) const;

  /// @brief Get value as boolean, with default.
  bool asBool(bool default_val = false) const;

  /// @brief Get value as string, with default.
  std::string asString(const std::string& default_val = "") const;
};

enum class JsonParseStatus : std::uint8_t {
  Ok = 0,
  InvalidArgument = 1,
  TooLarge = 2,
  SyntaxError = 3,
  NumberOutOfRange = 4,
  UnsupportedValue = 5,
};

constexpr std::size_t kMaxJsonConfigBytes = 64 * 1024;

/// @brief Parse a flat JSON object into a key-value map.
///
/// Only handles top-level keys with string, number, or boolean values.
/// Nested objects and arrays are rejected because config fields are flat.
///
/// @param json Pointer to JSON string.
/// @param length Length of JSON string.
/// @param out Receives the parsed map only on success; cleared on failure.
/// @return Explicit parse status. Success requires a closing brace and full
///         consumption except for trailing JSON whitespace.
JsonParseStatus parseJsonObject(const char* json, std::size_t length,
                                std::map<std::string, JsonValue>* out);

}  // namespace bach

#endif  // BACH_CORE_JSON_PARSER_H
