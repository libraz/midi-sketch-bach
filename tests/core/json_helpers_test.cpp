// Tests for core/json_helpers.h -- JsonWriter serialization.

#include "core/json_helpers.h"

#include <gtest/gtest.h>

#include <string>

namespace bach {
namespace {

// ---------------------------------------------------------------------------
// Simple values
// ---------------------------------------------------------------------------

TEST(JsonWriterTest, EmptyObject) {
  JsonWriter writer;
  writer.beginObject();
  writer.endObject();
  EXPECT_EQ(writer.toString(), "{}");
}

TEST(JsonWriterTest, EmptyArray) {
  JsonWriter writer;
  writer.beginArray();
  writer.endArray();
  EXPECT_EQ(writer.toString(), "[]");
}

TEST(JsonWriterTest, SingleIntValue) {
  JsonWriter writer;
  writer.beginObject();
  writer.key("pitch");
  writer.value(60);
  writer.endObject();
  EXPECT_EQ(writer.toString(), R"({"pitch":60})");
}

TEST(JsonWriterTest, SingleStringValue) {
  JsonWriter writer;
  writer.beginObject();
  writer.key("name");
  writer.value(std::string_view("C4"));
  writer.endObject();
  EXPECT_EQ(writer.toString(), R"({"name":"C4"})");
}

TEST(JsonWriterTest, BooleanValues) {
  JsonWriter writer;
  writer.beginObject();
  writer.key("valid");
  writer.value(true);
  writer.key("error");
  writer.value(false);
  writer.endObject();
  EXPECT_EQ(writer.toString(), R"({"valid":true,"error":false})");
}

TEST(JsonWriterTest, NullValue) {
  JsonWriter writer;
  writer.beginObject();
  writer.key("data");
  writer.valueNull();
  writer.endObject();
  EXPECT_EQ(writer.toString(), R"({"data":null})");
}

TEST(JsonWriterTest, UnsignedIntValue) {
  JsonWriter writer;
  writer.beginObject();
  writer.key("tick");
  writer.value(static_cast<uint32_t>(1920));
  writer.endObject();
  EXPECT_EQ(writer.toString(), R"({"tick":1920})");
}

TEST(JsonWriterTest, DoubleValue) {
  JsonWriter writer;
  writer.beginObject();
  writer.key("score");
  writer.value(0.95);
  writer.endObject();
  std::string result = writer.toString();
  // Floating-point formatting may vary; check it starts correctly.
  EXPECT_TRUE(result.find("\"score\":0.95") != std::string::npos ||
              result.find("\"score\":0.950") != std::string::npos);
}

// ---------------------------------------------------------------------------
// Multiple keys
// ---------------------------------------------------------------------------

TEST(JsonWriterTest, MultipleKeys) {
  JsonWriter writer;
  writer.beginObject();
  writer.key("pitch");
  writer.value(62);
  writer.key("velocity");
  writer.value(80);
  writer.key("voice");
  writer.value(std::string_view("alto"));
  writer.endObject();
  EXPECT_EQ(writer.toString(), R"({"pitch":62,"velocity":80,"voice":"alto"})");
}

// ---------------------------------------------------------------------------
// Nested objects
// ---------------------------------------------------------------------------

TEST(JsonWriterTest, NestedObject) {
  JsonWriter writer;
  writer.beginObject();
  writer.key("note");
  writer.beginObject();
  writer.key("pitch");
  writer.value(60);
  writer.key("name");
  writer.value(std::string_view("C4"));
  writer.endObject();
  writer.endObject();
  EXPECT_EQ(writer.toString(), R"({"note":{"pitch":60,"name":"C4"}})");
}

// ---------------------------------------------------------------------------
// Arrays
// ---------------------------------------------------------------------------

TEST(JsonWriterTest, ArrayOfInts) {
  JsonWriter writer;
  writer.beginObject();
  writer.key("scale");
  writer.beginArray();
  writer.value(0);
  writer.value(2);
  writer.value(4);
  writer.value(5);
  writer.value(7);
  writer.value(9);
  writer.value(11);
  writer.endArray();
  writer.endObject();
  EXPECT_EQ(writer.toString(), R"({"scale":[0,2,4,5,7,9,11]})");
}

TEST(JsonWriterTest, ArrayOfStrings) {
  JsonWriter writer;
  writer.beginArray();
  writer.value(std::string_view("soprano"));
  writer.value(std::string_view("alto"));
  writer.value(std::string_view("tenor"));
  writer.value(std::string_view("bass"));
  writer.endArray();
  EXPECT_EQ(writer.toString(), R"(["soprano","alto","tenor","bass"])");
}

TEST(JsonWriterTest, ArrayOfObjects) {
  JsonWriter writer;
  writer.beginArray();

  writer.beginObject();
  writer.key("pitch");
  writer.value(60);
  writer.endObject();

  writer.beginObject();
  writer.key("pitch");
  writer.value(64);
  writer.endObject();

  writer.endArray();
  EXPECT_EQ(writer.toString(), R"([{"pitch":60},{"pitch":64}])");
}

// ---------------------------------------------------------------------------
// String escaping
// ---------------------------------------------------------------------------

TEST(JsonWriterTest, EscapeQuotes) {
  JsonWriter writer;
  writer.beginObject();
  writer.key("text");
  writer.value(std::string_view("say \"hello\""));
  writer.endObject();
  EXPECT_EQ(writer.toString(), R"({"text":"say \"hello\""})");
}

TEST(JsonWriterTest, EscapeBackslash) {
  JsonWriter writer;
  writer.beginObject();
  writer.key("path");
  writer.value(std::string_view("a\\b"));
  writer.endObject();
  EXPECT_EQ(writer.toString(), R"({"path":"a\\b"})");
}

TEST(JsonWriterTest, EscapeNewlineAndTab) {
  JsonWriter writer;
  writer.beginObject();
  writer.key("text");
  writer.value(std::string_view("line1\nline2\ttab"));
  writer.endObject();
  EXPECT_EQ(writer.toString(), R"({"text":"line1\nline2\ttab"})");
}

// ---------------------------------------------------------------------------
// Complex nested structure (provenance-style)
// ---------------------------------------------------------------------------

TEST(JsonWriterTest, ProvenanceStyleOutput) {
  JsonWriter writer;
  writer.beginObject();

  writer.key("pitch");
  writer.value(62);

  writer.key("velocity");
  writer.value(80);

  writer.key("start_ticks");
  writer.value(static_cast<uint32_t>(1920));

  writer.key("provenance");
  writer.beginObject();
  writer.key("source");
  writer.value(std::string_view("fugue_answer"));
  writer.key("entry_number");
  writer.value(2);
  writer.key("transform_steps");
  writer.beginArray();
  writer.value(std::string_view("tonal_answer"));
  writer.value(std::string_view("collision_avoid"));
  writer.endArray();
  writer.endObject();

  writer.endObject();

  std::string expected =
      R"({"pitch":62,"velocity":80,"start_ticks":1920,"provenance":)"
      R"({"source":"fugue_answer","entry_number":2,"transform_steps":)"
      R"(["tonal_answer","collision_avoid"]}})";
  EXPECT_EQ(writer.toString(), expected);
}

// ---------------------------------------------------------------------------
// Pretty-print
// ---------------------------------------------------------------------------

TEST(JsonWriterTest, PrettyPrintBasic) {
  JsonWriter writer;
  writer.beginObject();
  writer.key("pitch");
  writer.value(60);
  writer.key("name");
  writer.value(std::string_view("C4"));
  writer.endObject();

  std::string pretty = writer.toPrettyString(2);
  // Should contain newlines and indentation.
  EXPECT_TRUE(pretty.find('\n') != std::string::npos);
  EXPECT_TRUE(pretty.find("  ") != std::string::npos);
  // Should still contain the data.
  EXPECT_TRUE(pretty.find("\"pitch\"") != std::string::npos);
  EXPECT_TRUE(pretty.find("60") != std::string::npos);
  EXPECT_TRUE(pretty.find("\"C4\"") != std::string::npos);
}

TEST(JsonWriterTest, PrettyPrintEmptyObject) {
  JsonWriter writer;
  writer.beginObject();
  writer.endObject();
  std::string pretty = writer.toPrettyString();
  EXPECT_EQ(pretty, "{}");
}

// ---------------------------------------------------------------------------
// Special double values
// ---------------------------------------------------------------------------

TEST(JsonWriterTest, NanBecomesNull) {
  JsonWriter writer;
  writer.beginObject();
  writer.key("val");
  writer.value(std::numeric_limits<double>::quiet_NaN());
  writer.endObject();
  EXPECT_EQ(writer.toString(), R"({"val":null})");
}

TEST(JsonWriterTest, InfBecomesNull) {
  JsonWriter writer;
  writer.beginObject();
  writer.key("val");
  writer.value(std::numeric_limits<double>::infinity());
  writer.endObject();
  EXPECT_EQ(writer.toString(), R"({"val":null})");
}

}  // namespace
}  // namespace bach
