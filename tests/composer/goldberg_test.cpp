// Dedicated Goldberg immutable-bass closure-fixture tests.
//
// An Aria (bars 0-3) + four variations (bars 4-19) over an immutable 4-bar
// Goldberg-style bass tiled 5x, 3 planned voices (V1 rests), C major.
// Passacaglia payloads and intents deliberately remain empty: the miniature uses
// GoldbergBassCarrier V2 + GoldbergVariationCarrier V0 and their dedicated bits.
//
// These tests cover the small closure fixture; the public 128-bar form and its
// canon/Quodlibet structure are covered by form_cantus/goldberg_canon tests:
//   1. The GoldbergVariations fixture runs through the full Composer cleanly for every seed
//      family (seed % 4 selects the scalar-wave start offset): no validator
//      failure, 172 notes (V0 variation 152 + V2 bass 20).
//   2. Dedicated Goldberg bass/variation bits and the climax marker appear.
//   3. ClimaxPlaced fires ONLY on block 4 (bars 16-19) notes -- never on the
//      Aria or the first three variations.
//   4. The Goldberg bass is immutable across all 5 cycles (the
//      goldberg_aria_bass_immutable rule stays silent on the clean fixture, and
//      each replayed 4-bar cycle reproduces the canonical ground pitches).
//   5. Five dedicated Goldberg variation blocks are present with densities 0/1/2/2/3.

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <map>
#include <set>
#include <vector>

#include "composer/composer.h"
#include "composer/harness_fixture.h"
#include "composer/material.h"
#include "composer/provenance.h"
#include "composer/validator.h"
#include "composer/voice_intent.h"
#include "core/basic_types.h"

namespace bach::composer {
namespace {

constexpr RuleIdMask bit(RuleBit b) {
  return ruleBitMask(b);
}

bool hasRule(const ValidationReport& r, const std::string& rule_id) {
  for (const auto& f : r.failures) {
    if (f.rule_id == rule_id)
      return true;
  }
  return false;
}

}  // namespace

// --- 1 & 2. GoldbergVariations fixture integration + dedicated bits -------------

// The Goldberg-style fixture must run through the full Composer cleanly for
// every seed family (seed % 4 selects the scalar-wave start offset): no
// validator failure, 172 notes, and the dedicated Goldberg
// bits present somewhere in the provenance.
TEST(GoldbergTest, GoldbergVariationsFixtureValidatesCleanAndStampsDedicatedBits) {
  for (int seed : {0, 1, 2, 3}) {
    const HarnessFixture fx = buildHarnessFixture(HarnessPhase::GoldbergVariations, seed);
    const ComposeResult r = Composer{}.run(fx.material, fx.harmony, fx.voice_plan);

    EXPECT_TRUE(r.validation.failures.empty())
        << "seed " << seed << " produced " << r.validation.failures.size() << " failure(s); first="
        << (r.validation.failures.empty() ? "" : r.validation.failures.front().rule_id);
    ASSERT_FALSE(r.notes.empty());
    ASSERT_EQ(r.notes.size(), r.provenance.size());
    EXPECT_EQ(fx.voice_plan.num_voices, 3);

    // V0 variation 152 + V2 bass 20 = 172 notes.
    EXPECT_EQ(r.notes.size(), 172u) << "seed " << seed;
    std::size_t v0 = 0;
    std::size_t v1 = 0;
    for (const auto& n : r.notes) {
      if (n.voice == 0)
        ++v0;
      else if (n.voice == 2)
        ++v1;
    }
    EXPECT_EQ(v0, 152u) << "seed " << seed << " V0 variation note count";
    EXPECT_EQ(v1, 20u) << "seed " << seed << " V2 bass note count";

    bool saw_ground = false;
    bool saw_variation = false;
    bool saw_climax = false;
    for (const auto& p : r.provenance) {
      if (p.satisfied_rules & bit(RuleBit::GoldbergBassReplayed))
        saw_ground = true;
      if (p.satisfied_rules & bit(RuleBit::GoldbergVariationRealized))
        saw_variation = true;
      if (p.satisfied_rules & bit(RuleBit::ClimaxPlaced))
        saw_climax = true;
    }
    EXPECT_TRUE(saw_ground) << "seed " << seed << " missing GoldbergBassReplayed";
    EXPECT_TRUE(saw_variation) << "seed " << seed << " missing GoldbergVariationRealized";
    EXPECT_TRUE(saw_climax) << "seed " << seed << " missing ClimaxPlaced";
  }
}

// --- 3. ClimaxPlaced fires only on the climax block (bars 16-19) ------------

// Block 4 is the only is_climax variation block; every ClimaxPlaced note must
// fall in bars 16-19 (ticks 30720..38400) and NONE earlier. Conversely, the
// climax block's notes must all carry ClimaxPlaced (it is the dynamic peak).
TEST(GoldbergTest, ClimaxPlacedOnlyOnFinalVariationBlock) {
  const HarnessFixture fx = buildHarnessFixture(HarnessPhase::GoldbergVariations, 0);
  const ComposeResult r = Composer{}.run(fx.material, fx.harmony, fx.voice_plan);
  ASSERT_EQ(r.notes.size(), r.provenance.size());

  const Tick climax_start = static_cast<Tick>(16) * kTicksPerBar;  // 30720.
  const Tick climax_end = static_cast<Tick>(20) * kTicksPerBar;    // 38400.

  std::size_t climax_notes = 0;
  std::size_t variation_in_climax_window = 0;
  for (std::size_t i = 0; i < r.notes.size(); ++i) {
    const bool is_climax = (r.provenance[i].satisfied_rules & bit(RuleBit::ClimaxPlaced)) != 0;
    const bool is_variation =
        (r.provenance[i].satisfied_rules & bit(RuleBit::GoldbergVariationRealized)) != 0;
    const Tick t = r.notes[i].start_tick;
    if (is_climax) {
      ++climax_notes;
      EXPECT_GE(t, climax_start) << "ClimaxPlaced note before the climax block";
      EXPECT_LT(t, climax_end) << "ClimaxPlaced note after the climax block";
    }
    if (is_variation && t >= climax_start && t < climax_end) {
      ++variation_in_climax_window;
      EXPECT_TRUE(is_climax) << "climax-block variation note missing ClimaxPlaced at tick " << t;
    }
  }
  // The climax block (bars 16-19) is 16 sixteenths/bar = 64 variation notes.
  EXPECT_EQ(climax_notes, 64u);
  EXPECT_EQ(variation_in_climax_window, 64u);
}

// --- 4. Ground immutable across all 5 cycles --------------------------------

// The dedicated goldberg_aria_bass_immutable rule stays silent on the clean
// fixture, and the V2 GoldbergBassCarrier notes replay the canonical 4-bar
// Goldberg bass (C2 F2 G2 A2) verbatim for all 5 cycles.
TEST(GoldbergTest, GroundImmutableAcrossAllFiveCycles) {
  const HarnessFixture fx = buildHarnessFixture(HarnessPhase::GoldbergVariations, 1);
  const ComposeResult r = Composer{}.run(fx.material, fx.harmony, fx.voice_plan);
  ASSERT_EQ(r.notes.size(), r.provenance.size());

  EXPECT_FALSE(hasRule(r.validation, "goldberg_aria_bass_immutable"));

  // Collect the ground notes in tick order.
  std::map<Tick, std::uint8_t> ground;
  for (std::size_t i = 0; i < r.notes.size(); ++i) {
    if (r.provenance[i].satisfied_rules & bit(RuleBit::GoldbergBassReplayed))
      ground[r.notes[i].start_tick] = r.notes[i].pitch;
  }
  ASSERT_EQ(ground.size(), 20u);  // 4 bars x 5 cycles.

  const std::array<std::uint8_t, 4> canonical = {36, 41, 43, 45};  // C2 F2 G2 A2.
  for (int cycle = 0; cycle < 5; ++cycle) {
    for (int bar = 0; bar < 4; ++bar) {
      const Tick tick = static_cast<Tick>(cycle * 4 + bar) * kTicksPerBar;
      const auto it = ground.find(tick);
      ASSERT_NE(it, ground.end()) << "missing ground note at cycle " << cycle << " bar " << bar;
      EXPECT_EQ(it->second, canonical[static_cast<std::size_t>(bar)])
          << "ground mutated at cycle " << cycle << " bar " << bar;
    }
  }
}

// --- 5. Five variation blocks with densities 0/1/2/2/3 ----------------------

// The fixture material must declare exactly five dedicated Goldberg variation blocks
// (Aria + four variations) with the documented rising density tiers and only
// block 4 flagged is_climax.
TEST(GoldbergTest, FiveVariationBlocksWithRisingDensity) {
  const HarnessFixture fx = buildHarnessFixture(HarnessPhase::GoldbergVariations, 2);
  EXPECT_TRUE(fx.material.passacaglia_ground.empty());
  EXPECT_TRUE(fx.material.passacaglia_variations.empty());
  const auto& blocks = fx.material.goldberg_variations;
  ASSERT_EQ(blocks.size(), 5u);

  const std::array<int, 5> expected_density = {0, 1, 2, 2, 3};
  const std::array<bool, 5> expected_climax = {false, false, false, false, true};
  // Notes per block: Aria m=2 (8) / Var1 m=4 (16) / Var2 m=8 (32) / Var3 m=8 (32)
  // / Var4 m=16 (64). All on voice 0, contiguous 4-bar windows.
  const std::array<std::size_t, 5> expected_notes = {8u, 16u, 32u, 32u, 64u};

  for (std::size_t b = 0; b < blocks.size(); ++b) {
    EXPECT_EQ(blocks[b].voice, 0) << "block " << b;
    EXPECT_EQ(blocks[b].density_level, expected_density[b]) << "block " << b;
    EXPECT_EQ(blocks[b].is_climax, expected_climax[b]) << "block " << b;
    EXPECT_EQ(blocks[b].notes.size(), expected_notes[b]) << "block " << b;
    EXPECT_EQ(blocks[b].start_tick, static_cast<Tick>(b * 4) * kTicksPerBar) << "block " << b;
    EXPECT_EQ(blocks[b].end_tick, static_cast<Tick>(b * 4 + 4) * kTicksPerBar) << "block " << b;
  }
}

}  // namespace bach::composer
