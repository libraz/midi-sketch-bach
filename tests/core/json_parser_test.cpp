#include "core/json_parser.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace bach {
namespace {

JsonParseStatus parse(const std::string& json, std::map<std::string, JsonValue>* out) {
  return parseJsonObject(json.data(), json.size(), out);
}

TEST(JsonParserTest, ParsesEmptyAndTypedFlatObjectsWithTrailingWhitespace) {
  std::map<std::string, JsonValue> values;
  ASSERT_EQ(parse("  {\"form\":\"fugue\",\"seed\":42,\"minor\":true,\"none\":null} \n", &values),
            JsonParseStatus::Ok);
  ASSERT_EQ(values.size(), 4u);
  EXPECT_EQ(values["form"].asString(), "fugue");
  std::uint32_t seed = 0;
  EXPECT_TRUE(values["seed"].asUint32(&seed));
  EXPECT_EQ(seed, 42u);
  EXPECT_TRUE(values["minor"].asBool());
  EXPECT_EQ(values["none"].type, JsonValue::Null);

  ASSERT_EQ(parse("{}", &values), JsonParseStatus::Ok);
  EXPECT_TRUE(values.empty());
}

TEST(JsonParserTest, DecodesSupportedEscapesAndUnicode) {
  std::map<std::string, JsonValue> values;
  ASSERT_EQ(parse(R"({"text":"A\n\t\"\\\/\b\f\r\u00e9"})", &values), JsonParseStatus::Ok);
  EXPECT_EQ(values["text"].string_val, std::string("A\n\t\"\\/\b\f\ré"));
}

TEST(JsonParserTest, DecodesUnicodeSurrogatePairAndRejectsLoneSurrogates) {
  std::map<std::string, JsonValue> values;
  ASSERT_EQ(parse(R"({"text":"G clef \uD834\uDD1E"})", &values), JsonParseStatus::Ok);
  EXPECT_EQ(values["text"].string_val, std::string("G clef \xF0\x9D\x84\x9E"));

  for (const std::string& json : {
           R"({"text":"\uD834"})",
           R"({"text":"\uDD1E"})",
           R"({"text":"\uD834\u0041"})",
       }) {
    SCOPED_TRACE(json);
    EXPECT_EQ(parse(json, &values), JsonParseStatus::SyntaxError);
  }
}

TEST(JsonParserTest, IntegralConversionsRejectFractionAndBounds) {
  std::map<std::string, JsonValue> values;
  ASSERT_EQ(parse(R"({"fraction":1.5,"negative":-1,"maximum":4294967295})", &values),
            JsonParseStatus::Ok);
  std::int64_t integer = 0;
  std::uint32_t unsigned_integer = 0;
  EXPECT_FALSE(values["fraction"].asInt64(&integer));
  EXPECT_FALSE(values["negative"].asUint32(&unsigned_integer));
  EXPECT_TRUE(values["maximum"].asUint32(&unsigned_integer));
  EXPECT_EQ(unsigned_integer, UINT32_MAX);
}

TEST(JsonParserTest, RejectsMalformedAndPartiallyConsumedInput) {
  const std::vector<std::string> cases = {
      "",
      "{",
      R"({"a":1)",
      R"({"a":1}tail)",
      R"({"a":})",
      R"({"a":1,})",
      R"({"a":truex})",
      R"({"a":01})",
      R"({"a":1.})",
      R"({"a":1e})",
      R"({"a":"unterminated})",
      R"({"a":"\q"})",
      R"({"a":1,"a":2})",
  };
  for (const auto& json : cases) {
    SCOPED_TRACE(json);
    std::map<std::string, JsonValue> values = {{"stale", JsonValue{}}};
    EXPECT_NE(parse(json, &values), JsonParseStatus::Ok);
    EXPECT_TRUE(values.empty());
  }
}

TEST(JsonParserTest, RejectsNestedValuesAndOversizedInput) {
  std::map<std::string, JsonValue> values;
  EXPECT_EQ(parse(R"({"nested":{}})", &values), JsonParseStatus::UnsupportedValue);
  EXPECT_EQ(parse(R"({"array":[]})", &values), JsonParseStatus::UnsupportedValue);

  std::string oversized(kMaxJsonConfigBytes + 1, ' ');
  EXPECT_EQ(parse(oversized, &values), JsonParseStatus::TooLarge);
}

TEST(JsonParserTest, RejectsNumbersOutsideFiniteRepresentation) {
  std::map<std::string, JsonValue> values;
  EXPECT_EQ(parse(R"({"seed":1e400})", &values), JsonParseStatus::NumberOutOfRange);
}

TEST(JsonParserTest, RejectsNullPointers) {
  std::map<std::string, JsonValue> values;
  EXPECT_EQ(parseJsonObject(nullptr, 1, &values), JsonParseStatus::InvalidArgument);
  EXPECT_EQ(parseJsonObject("{}", 2, nullptr), JsonParseStatus::InvalidArgument);
}

}  // namespace
}  // namespace bach
