// Tests for ornament type definitions and string conversion.

#include "ornament/ornament_types.h"

#include <gtest/gtest.h>

namespace bach {
namespace {

// ---------------------------------------------------------------------------
// OrnamentType string conversion
// ---------------------------------------------------------------------------

TEST(OrnamentTypesTest, TrillToString) {
  EXPECT_STREQ(ornamentTypeToString(OrnamentType::Trill), "trill");
}

TEST(OrnamentTypesTest, MordentToString) {
  EXPECT_STREQ(ornamentTypeToString(OrnamentType::Mordent), "mordent");
}

TEST(OrnamentTypesTest, PralltrillerToString) {
  EXPECT_STREQ(ornamentTypeToString(OrnamentType::Pralltriller), "pralltriller");
}

TEST(OrnamentTypesTest, TurnToString) {
  EXPECT_STREQ(ornamentTypeToString(OrnamentType::Turn), "turn");
}

TEST(OrnamentTypesTest, AppoggiaturaToString) {
  EXPECT_STREQ(ornamentTypeToString(OrnamentType::Appoggiatura), "appoggiatura");
}

TEST(OrnamentTypesTest, SchleiferToString) {
  EXPECT_STREQ(ornamentTypeToString(OrnamentType::Schleifer), "schleifer");
}

TEST(OrnamentTypesTest, VorschlagToString) {
  EXPECT_STREQ(ornamentTypeToString(OrnamentType::Vorschlag), "vorschlag");
}

TEST(OrnamentTypesTest, NachschlagToString) {
  EXPECT_STREQ(ornamentTypeToString(OrnamentType::Nachschlag), "nachschlag");
}

TEST(OrnamentTypesTest, CompoundTrillNachschlagToString) {
  EXPECT_STREQ(ornamentTypeToString(OrnamentType::CompoundTrillNachschlag),
               "compound_trill_nachschlag");
}

TEST(OrnamentTypesTest, CompoundTurnTrillToString) {
  EXPECT_STREQ(ornamentTypeToString(OrnamentType::CompoundTurnTrill),
               "compound_turn_trill");
}

// ---------------------------------------------------------------------------
// OrnamentConfig defaults
// ---------------------------------------------------------------------------

TEST(OrnamentConfigTest, DefaultValues) {
  OrnamentConfig config;
  EXPECT_TRUE(config.enable_trill);
  EXPECT_TRUE(config.enable_mordent);
  EXPECT_TRUE(config.enable_turn);
  EXPECT_TRUE(config.enable_appoggiatura);
  EXPECT_TRUE(config.enable_pralltriller);
  EXPECT_TRUE(config.enable_vorschlag);
  EXPECT_TRUE(config.enable_nachschlag);
  EXPECT_TRUE(config.enable_compound);
  EXPECT_FLOAT_EQ(config.ornament_density, 0.15f);
}

TEST(OrnamentConfigTest, CustomValues) {
  OrnamentConfig config;
  config.enable_trill = false;
  config.enable_mordent = false;
  config.enable_turn = true;
  config.ornament_density = 0.3f;

  EXPECT_FALSE(config.enable_trill);
  EXPECT_FALSE(config.enable_mordent);
  EXPECT_TRUE(config.enable_turn);
  EXPECT_FLOAT_EQ(config.ornament_density, 0.3f);
}

}  // namespace
}  // namespace bach
