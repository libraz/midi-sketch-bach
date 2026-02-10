/// @file
/// @brief Implementation of the minimal JSON writer for structured output.

#include "core/json_helpers.h"

#include <cmath>
#include <sstream>

namespace bach {

void JsonWriter::beginObject() {
  maybeComma();
  buffer_ += '{';
  needs_comma_.push_back(false);
}

void JsonWriter::endObject() {
  buffer_ += '}';
  if (!needs_comma_.empty()) {
    needs_comma_.pop_back();
  }
  if (!needs_comma_.empty()) {
    needs_comma_.back() = true;
  }
}

void JsonWriter::beginArray() {
  maybeComma();
  buffer_ += '[';
  needs_comma_.push_back(false);
}

void JsonWriter::endArray() {
  buffer_ += ']';
  if (!needs_comma_.empty()) {
    needs_comma_.pop_back();
  }
  if (!needs_comma_.empty()) {
    needs_comma_.back() = true;
  }
}

void JsonWriter::key(std::string_view name) {
  maybeComma();
  buffer_ += '"';
  buffer_ += escapeString(name);
  buffer_ += "\":";
  // After writing a key, the next element should NOT get a comma prefix
  // (it is the value for this key). Mark as not needing comma.
  if (!needs_comma_.empty()) {
    needs_comma_.back() = false;
  }
}

void JsonWriter::value(std::string_view val) {
  maybeComma();
  buffer_ += '"';
  buffer_ += escapeString(val);
  buffer_ += '"';
  if (!needs_comma_.empty()) {
    needs_comma_.back() = true;
  }
}

void JsonWriter::value(int val) {
  maybeComma();
  buffer_ += std::to_string(val);
  if (!needs_comma_.empty()) {
    needs_comma_.back() = true;
  }
}

void JsonWriter::value(uint32_t val) {
  maybeComma();
  buffer_ += std::to_string(val);
  if (!needs_comma_.empty()) {
    needs_comma_.back() = true;
  }
}

void JsonWriter::value(double val) {
  maybeComma();
  // Handle special floating-point values.
  if (std::isnan(val)) {
    buffer_ += "null";
  } else if (std::isinf(val)) {
    buffer_ += "null";
  } else {
    std::ostringstream oss;
    oss << val;
    buffer_ += oss.str();
  }
  if (!needs_comma_.empty()) {
    needs_comma_.back() = true;
  }
}

void JsonWriter::value(bool val) {
  maybeComma();
  buffer_ += val ? "true" : "false";
  if (!needs_comma_.empty()) {
    needs_comma_.back() = true;
  }
}

void JsonWriter::valueNull() {
  maybeComma();
  buffer_ += "null";
  if (!needs_comma_.empty()) {
    needs_comma_.back() = true;
  }
}

std::string JsonWriter::toString() const {
  return buffer_;
}

std::string JsonWriter::toPrettyString(int indent_size) const {
  // Simple pretty-printer: add newlines and indentation after structural chars.
  std::string result;
  result.reserve(buffer_.size() * 2);

  int depth = 0;
  bool in_string = false;
  bool escaped = false;

  auto indent = [&]() {
    result += '\n';
    for (int idx = 0; idx < depth * indent_size; ++idx) {
      result += ' ';
    }
  };

  for (size_t pos = 0; pos < buffer_.size(); ++pos) {
    char chr = buffer_[pos];

    if (escaped) {
      result += chr;
      escaped = false;
      continue;
    }

    if (chr == '\\' && in_string) {
      result += chr;
      escaped = true;
      continue;
    }

    if (chr == '"') {
      in_string = !in_string;
      result += chr;
      continue;
    }

    if (in_string) {
      result += chr;
      continue;
    }

    switch (chr) {
      case '{':
      case '[':
        result += chr;
        ++depth;
        // Check if next char is closing bracket (empty container).
        if (pos + 1 < buffer_.size() &&
            (buffer_[pos + 1] == '}' || buffer_[pos + 1] == ']')) {
          // Keep empty containers compact: {} or [].
        } else {
          indent();
        }
        break;

      case '}':
      case ']':
        // Check if previous non-space char was opening bracket.
        if (!result.empty() && (result.back() == '{' || result.back() == '[')) {
          result += chr;
        } else {
          --depth;
          indent();
          result += chr;
        }
        break;

      case ',':
        result += chr;
        indent();
        break;

      case ':':
        result += ": ";
        break;

      default:
        result += chr;
        break;
    }
  }

  return result;
}

void JsonWriter::maybeComma() {
  if (!needs_comma_.empty() && needs_comma_.back()) {
    buffer_ += ',';
    needs_comma_.back() = false;
  }
}

std::string JsonWriter::escapeString(std::string_view input) {
  std::string result;
  result.reserve(input.size());

  for (char chr : input) {
    switch (chr) {
      case '"':  result += "\\\""; break;
      case '\\': result += "\\\\"; break;
      case '\b': result += "\\b";  break;
      case '\f': result += "\\f";  break;
      case '\n': result += "\\n";  break;
      case '\r': result += "\\r";  break;
      case '\t': result += "\\t";  break;
      default:
        // Escape control characters (0x00-0x1F) as \u00XX.
        if (static_cast<unsigned char>(chr) < 0x20) {
          char hex_buf[8];
          snprintf(hex_buf, sizeof(hex_buf), "\\u%04x",
                   static_cast<unsigned>(static_cast<unsigned char>(chr)));
          result += hex_buf;
        } else {
          result += chr;
        }
        break;
    }
  }

  return result;
}

}  // namespace bach
