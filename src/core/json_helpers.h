// Minimal JSON serialization writer (no external dependencies).
//
// Builds JSON output via a string-builder approach. Designed for the
// output.json provenance and analysis reports. Does not parse JSON.

#ifndef BACH_CORE_JSON_HELPERS_H
#define BACH_CORE_JSON_HELPERS_H

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace bach {

/// @brief Simple JSON writer that builds a JSON string incrementally.
///
/// Usage:
/// @code
///   JsonWriter writer;
///   writer.beginObject();
///   writer.key("pitch");
///   writer.value(60);
///   writer.key("name");
///   writer.value("C4");
///   writer.endObject();
///   std::string json = writer.toString();
///   // -> {"pitch":60,"name":"C4"}
/// @endcode
///
/// Supports nested objects and arrays. Tracks comma insertion automatically.
/// Does not validate structure (caller must match begin/end pairs).
class JsonWriter {
 public:
  JsonWriter() = default;

  /// @brief Begin a JSON object '{'.
  void beginObject();

  /// @brief End a JSON object '}'.
  void endObject();

  /// @brief Begin a JSON array '['.
  void beginArray();

  /// @brief End a JSON array ']'.
  void endArray();

  /// @brief Write an object key (must be followed by a value call).
  /// @param name Key string.
  void key(std::string_view name);

  /// @brief Write a string value.
  /// @param val String to write (will be JSON-escaped).
  void value(std::string_view val);

  /// @brief Write an integer value.
  /// @param val Integer value.
  void value(int val);

  /// @brief Write an unsigned integer value.
  /// @param val Unsigned integer value.
  void value(uint32_t val);

  /// @brief Write a floating-point value.
  /// @param val Double value.
  void value(double val);

  /// @brief Write a boolean value.
  /// @param val Boolean value.
  void value(bool val);

  /// @brief Write a null value.
  void valueNull();

  /// @brief Get the accumulated JSON string.
  /// @return Complete JSON string built so far.
  std::string toString() const;

  /// @brief Get the accumulated JSON string with pretty-print indentation.
  /// @param indent_size Number of spaces per indent level (default: 2).
  /// @return Formatted JSON string.
  std::string toPrettyString(int indent_size = 2) const;

 private:
  /// Write a comma if needed before the next value/key.
  void maybeComma();

  /// Escape special characters in a string for JSON output.
  static std::string escapeString(std::string_view input);

  std::string buffer_;

  // Track whether we need a comma before the next element.
  // Each nesting level pushes a new entry.
  std::vector<bool> needs_comma_;
};

}  // namespace bach

#endif  // BACH_CORE_JSON_HELPERS_H
