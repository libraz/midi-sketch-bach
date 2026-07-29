// Implementation of minimal flat-object JSON parser.

#include "core/json_parser.h"

#include <cctype>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <limits>

namespace bach {

bool JsonValue::asInt64(std::int64_t* out) const {
  if (out == nullptr || type != Number || !std::isfinite(number_val) ||
      std::trunc(number_val) != number_val || number_val < -9223372036854775808.0 ||
      number_val >= 9223372036854775808.0) {
    return false;
  }
  *out = static_cast<std::int64_t>(number_val);
  return true;
}

bool JsonValue::asUint32(std::uint32_t* out) const {
  if (out == nullptr || type != Number || !std::isfinite(number_val) ||
      std::trunc(number_val) != number_val || number_val < 0.0 ||
      number_val > static_cast<double>(std::numeric_limits<std::uint32_t>::max())) {
    return false;
  }
  *out = static_cast<std::uint32_t>(number_val);
  return true;
}

bool JsonValue::asBool(bool default_val) const {
  if (type == Bool)
    return bool_val;
  return default_val;
}

std::string JsonValue::asString(const std::string& default_val) const {
  if (type == String)
    return string_val;
  return default_val;
}

namespace {

/// @brief Skip whitespace in JSON string.
void skipWhitespace(const char* json, size_t length, size_t& pos) {
  while (pos < length && std::isspace(static_cast<unsigned char>(json[pos]))) {
    ++pos;
  }
}

bool appendUtf8(std::uint32_t code_point, std::string* out) {
  if ((code_point >= 0xD800 && code_point <= 0xDFFF) || code_point > 0x10FFFF) {
    return false;
  }
  if (code_point <= 0x7F) {
    out->push_back(static_cast<char>(code_point));
  } else if (code_point <= 0x7FF) {
    out->push_back(static_cast<char>(0xC0 | (code_point >> 6)));
    out->push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
  } else if (code_point <= 0xFFFF) {
    out->push_back(static_cast<char>(0xE0 | (code_point >> 12)));
    out->push_back(static_cast<char>(0x80 | ((code_point >> 6) & 0x3F)));
    out->push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
  } else {
    out->push_back(static_cast<char>(0xF0 | (code_point >> 18)));
    out->push_back(static_cast<char>(0x80 | ((code_point >> 12) & 0x3F)));
    out->push_back(static_cast<char>(0x80 | ((code_point >> 6) & 0x3F)));
    out->push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
  }
  return true;
}

int hexDigit(char chr) {
  if (chr >= '0' && chr <= '9')
    return chr - '0';
  if (chr >= 'a' && chr <= 'f')
    return chr - 'a' + 10;
  if (chr >= 'A' && chr <= 'F')
    return chr - 'A' + 10;
  return -1;
}

bool parseString(const char* json, std::size_t length, std::size_t* pos, std::string* out) {
  if (*pos >= length || json[*pos] != '"')
    return false;
  ++(*pos);
  out->clear();
  while (*pos < length) {
    const unsigned char current = static_cast<unsigned char>(json[*pos]);
    if (current == '"') {
      ++(*pos);
      return true;
    }
    if (current < 0x20) {
      return false;
    }
    if (current == '\\') {
      ++(*pos);
      if (*pos >= length)
        return false;
      switch (json[*pos]) {
        case '"':
          out->push_back('"');
          break;
        case '\\':
          out->push_back('\\');
          break;
        case '/':
          out->push_back('/');
          break;
        case 'b':
          out->push_back('\b');
          break;
        case 'f':
          out->push_back('\f');
          break;
        case 'n':
          out->push_back('\n');
          break;
        case 't':
          out->push_back('\t');
          break;
        case 'r':
          out->push_back('\r');
          break;
        case 'u': {
          if (*pos + 4 >= length)
            return false;
          std::uint32_t code_point = 0;
          for (std::size_t offset = 1; offset <= 4; ++offset) {
            const int digit = hexDigit(json[*pos + offset]);
            if (digit < 0)
              return false;
            code_point = (code_point << 4) | static_cast<std::uint32_t>(digit);
          }
          *pos += 4;
          if (code_point >= 0xD800 && code_point <= 0xDBFF) {
            // RFC 8259 represents non-BMP code points as one high/low UTF-16
            // surrogate pair. Consume the second escape as part of this
            // scalar; lone or mismatched surrogates remain syntax errors.
            if (*pos + 6 >= length || json[*pos + 1] != '\\' || json[*pos + 2] != 'u')
              return false;
            std::uint32_t low = 0;
            for (std::size_t offset = 3; offset <= 6; ++offset) {
              const int digit = hexDigit(json[*pos + offset]);
              if (digit < 0)
                return false;
              low = (low << 4) | static_cast<std::uint32_t>(digit);
            }
            if (low < 0xDC00 || low > 0xDFFF)
              return false;
            code_point =
                0x10000 + ((code_point - 0xD800) << 10) + static_cast<std::uint32_t>(low - 0xDC00);
            *pos += 6;
          } else if (code_point >= 0xDC00 && code_point <= 0xDFFF) {
            return false;
          }
          if (!appendUtf8(code_point, out))
            return false;
          break;
        }
        default:
          return false;
      }
    } else {
      out->push_back(static_cast<char>(current));
    }
    ++(*pos);
  }
  return false;
}

JsonParseStatus parseNumber(const char* json, std::size_t length, std::size_t* pos,
                            JsonValue* out) {
  const std::size_t start = *pos;
  if (json[*pos] == '-')
    ++(*pos);
  if (*pos >= length)
    return JsonParseStatus::SyntaxError;
  if (json[*pos] == '0') {
    ++(*pos);
    if (*pos < length && std::isdigit(static_cast<unsigned char>(json[*pos])))
      return JsonParseStatus::SyntaxError;
  } else if (json[*pos] >= '1' && json[*pos] <= '9') {
    while (*pos < length && std::isdigit(static_cast<unsigned char>(json[*pos])))
      ++(*pos);
  } else {
    return JsonParseStatus::SyntaxError;
  }
  if (*pos < length && json[*pos] == '.') {
    ++(*pos);
    const std::size_t fraction_start = *pos;
    while (*pos < length && std::isdigit(static_cast<unsigned char>(json[*pos])))
      ++(*pos);
    if (*pos == fraction_start)
      return JsonParseStatus::SyntaxError;
  }
  if (*pos < length && (json[*pos] == 'e' || json[*pos] == 'E')) {
    ++(*pos);
    if (*pos < length && (json[*pos] == '+' || json[*pos] == '-'))
      ++(*pos);
    const std::size_t exponent_start = *pos;
    while (*pos < length && std::isdigit(static_cast<unsigned char>(json[*pos])))
      ++(*pos);
    if (*pos == exponent_start)
      return JsonParseStatus::SyntaxError;
  }

  const std::string number_text(json + start, *pos - start);
  char* end = nullptr;
  errno = 0;
  const double number = std::strtod(number_text.c_str(), &end);
  if (errno == ERANGE || end != number_text.c_str() + number_text.size() || !std::isfinite(number))
    return JsonParseStatus::NumberOutOfRange;
  out->type = JsonValue::Number;
  out->number_val = number;
  return JsonParseStatus::Ok;
}

}  // namespace

JsonParseStatus parseJsonObject(const char* json, std::size_t length,
                                std::map<std::string, JsonValue>* out) {
  if (out == nullptr)
    return JsonParseStatus::InvalidArgument;
  out->clear();
  if (json == nullptr || length == 0)
    return JsonParseStatus::InvalidArgument;
  if (length > kMaxJsonConfigBytes)
    return JsonParseStatus::TooLarge;
  std::map<std::string, JsonValue> result;

  std::size_t pos = 0;
  skipWhitespace(json, length, pos);
  if (pos >= length || json[pos] != '{')
    return JsonParseStatus::SyntaxError;
  ++pos;
  skipWhitespace(json, length, pos);
  if (pos < length && json[pos] == '}') {
    ++pos;
    skipWhitespace(json, length, pos);
    return pos == length ? JsonParseStatus::Ok : JsonParseStatus::SyntaxError;
  }

  while (pos < length) {
    skipWhitespace(json, length, pos);
    if (json[pos] != '"')
      return JsonParseStatus::SyntaxError;
    std::string key;
    if (!parseString(json, length, &pos, &key))
      return JsonParseStatus::SyntaxError;
    if (result.find(key) != result.end())
      return JsonParseStatus::SyntaxError;

    skipWhitespace(json, length, pos);
    if (pos >= length || json[pos] != ':')
      return JsonParseStatus::SyntaxError;
    ++pos;
    skipWhitespace(json, length, pos);
    if (pos >= length)
      return JsonParseStatus::SyntaxError;

    JsonValue value;
    if (json[pos] == '"') {
      value.type = JsonValue::String;
      if (!parseString(json, length, &pos, &value.string_val))
        return JsonParseStatus::SyntaxError;
    } else if (length - pos >= 4 && std::memcmp(json + pos, "true", 4) == 0) {
      value.type = JsonValue::Bool;
      value.bool_val = true;
      pos += 4;
    } else if (length - pos >= 5 && std::memcmp(json + pos, "false", 5) == 0) {
      value.type = JsonValue::Bool;
      value.bool_val = false;
      pos += 5;
    } else if (length - pos >= 4 && std::memcmp(json + pos, "null", 4) == 0) {
      value.type = JsonValue::Null;
      pos += 4;
    } else if (json[pos] == '{' || json[pos] == '[') {
      return JsonParseStatus::UnsupportedValue;
    } else {
      const JsonParseStatus number_status = parseNumber(json, length, &pos, &value);
      if (number_status != JsonParseStatus::Ok)
        return number_status;
    }
    result.emplace(std::move(key), std::move(value));

    skipWhitespace(json, length, pos);
    if (pos >= length)
      return JsonParseStatus::SyntaxError;
    if (json[pos] == '}') {
      ++pos;
      skipWhitespace(json, length, pos);
      if (pos != length)
        return JsonParseStatus::SyntaxError;
      *out = std::move(result);
      return JsonParseStatus::Ok;
    }
    if (json[pos] != ',')
      return JsonParseStatus::SyntaxError;
    ++pos;
    skipWhitespace(json, length, pos);
    if (pos >= length || json[pos] == '}')
      return JsonParseStatus::SyntaxError;
  }
  return JsonParseStatus::SyntaxError;
}

}  // namespace bach
