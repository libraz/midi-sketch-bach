// Keyboard suite (five-movement dance suite) tests.
//
// This fixture is a reuse-only assembly: it introduces no new VoiceIntent /
// RuleBit / validator rule / Material type. It stitches the existing
// Arch / Prelude / Fantasia carriers into a single 20-bar, two-voice keyboard
// suite:
//
//   V0 dance line: five contiguous 4-bar movements (Prelude / Allemande /
//   Sarabande / Courante / Gigue) alternating FigurationCarrier (movements 1 & 4,
//   stamping FigurationCommitted) and FantasiaCarrier (movements 2/3/5, stamping
//   FantasiaSectionContrast). All five are C-major scalar waves chord-tone-
//   anchored on each bar downbeat.
//   V1 ground bass: a single immutable GroundCarrier line (a 4-bar C-major bass
//   figure tiled 5x, stamping GroundBassReplayed).
//
// These tests cover the integration end:
//   1. buildHarnessFixture(KeyboardSuite, seed) -> Composer::run validates Ok for every
//      seed family (seed % 4 selects the scalar-wave start offset).
//   2. The three reused RuleBits (FigurationCommitted=52, FantasiaSectionContrast
//      =63, GroundBassReplayed=49) all appear across the provenance.
//   3. The five movements are present (2 FigurationCarrier + 3 FantasiaCarrier
//      dance groups by note count) and the ground bass replays exactly 20 notes.
//   4. The reused validator rules section_contrast_required and
//      ground_bass_immutable do NOT fire on the fixture.

#include <gtest/gtest.h>

#include <cstdint>

#include "composer/composer.h"
#include "composer/harness_fixture.h"
#include "composer/provenance.h"
#include "composer/voice_intent.h"
#include "core/basic_types.h"

namespace bach::composer {
namespace {

bool hasRule(const ValidationReport& r, const std::string& rule_id) {
  for (const auto& f : r.failures) {
    if (f.rule_id == rule_id)
      return true;
  }
  return false;
}

constexpr RuleIdMask bit(RuleBit b) {
  return ruleBitMask(b);
}

}  // namespace

// --- KeyboardSuite fixture integration -------------------------------------------

// The keyboard-suite fixture must run through the full Composer cleanly for
// every seed family (seed % 4 selects the scalar-wave start offset):
// no validator failure, two voices, all three reused RuleBits stamped, the five
// movements present (96 FigurationCarrier + 104 FantasiaCarrier dance notes),
// and the ground bass replaying 20 notes. The reused section_contrast_required
// and ground_bass_immutable rules must stay silent.
TEST(SuiteTest, KeyboardSuiteFixtureValidatesCleanAndStampsReusedBits) {
  for (int seed : {0, 1, 2, 3}) {
    const HarnessFixture fx = buildHarnessFixture(HarnessPhase::KeyboardSuite, seed);
    const ComposeResult r = Composer{}.run(fx.material, fx.harmony, fx.voice_plan);

    EXPECT_TRUE(r.validation.failures.empty())
        << "seed " << seed << " produced " << r.validation.failures.size() << " failure(s); first="
        << (r.validation.failures.empty() ? "" : r.validation.failures.front().rule_id);
    // The two reused validator rules must not soft-fail the contrasting,
    // clean-tiling fixture.
    EXPECT_FALSE(hasRule(r.validation, "section_contrast_required"))
        << "seed " << seed << " soft-failed section_contrast_required";
    EXPECT_FALSE(hasRule(r.validation, "ground_bass_immutable"))
        << "seed " << seed << " soft-failed ground_bass_immutable";

    ASSERT_FALSE(r.notes.empty());
    ASSERT_EQ(r.notes.size(), r.provenance.size());
    EXPECT_EQ(fx.voice_plan.num_voices, 2);

    int figuration_notes = 0;
    int fantasia_notes = 0;
    int ground_notes = 0;
    bool saw_figuration_bit = false;
    bool saw_fantasia_bit = false;
    bool saw_ground_bit = false;
    bool all_figuration_stamped = true;
    bool all_fantasia_stamped = true;
    bool all_ground_stamped = true;

    for (std::size_t i = 0; i < r.notes.size(); ++i) {
      const RuleIdMask rules = r.provenance[i].satisfied_rules;
      switch (r.provenance[i].voice_intent) {
        case VoiceIntent::FigurationCarrier:
          ++figuration_notes;
          if (rules & bit(RuleBit::FigurationCommitted))
            saw_figuration_bit = true;
          else
            all_figuration_stamped = false;
          break;
        case VoiceIntent::FantasiaCarrier:
          ++fantasia_notes;
          if (rules & bit(RuleBit::FantasiaSectionContrast))
            saw_fantasia_bit = true;
          else
            all_fantasia_stamped = false;
          break;
        case VoiceIntent::GroundCarrier:
          ++ground_notes;
          if (rules & bit(RuleBit::GroundBassReplayed))
            saw_ground_bit = true;
          else
            all_ground_stamped = false;
          break;
        default:
          break;
      }
    }

    // V0 dance lines: FigurationCarrier = Prelude (16 sixteenths/bar x 4 bars =
    // 64) + Courante (8 eighths/bar x 4 = 32) = 96. FantasiaCarrier = Allemande
    // (8 eighths/bar x 4 = 32) + Sarabande (2 halves/bar x 4 = 8) + Gigue (16
    // sixteenths/bar x 4 = 64) = 104.
    EXPECT_EQ(figuration_notes, 96) << "seed " << seed << " figuration dance notes";
    EXPECT_EQ(fantasia_notes, 104) << "seed " << seed << " fantasia dance notes";
    // V1 ground bass: 4-bar figure tiled 5x = 20 notes.
    EXPECT_EQ(ground_notes, 20) << "seed " << seed << " ground notes";

    // All three reused RuleBits must fire, and every carrier note of each kind
    // must carry its bit.
    EXPECT_TRUE(saw_figuration_bit) << "seed " << seed << " missing FigurationCommitted";
    EXPECT_TRUE(saw_fantasia_bit) << "seed " << seed << " missing FantasiaSectionContrast";
    EXPECT_TRUE(saw_ground_bit) << "seed " << seed << " missing GroundBassReplayed";
    EXPECT_TRUE(all_figuration_stamped)
        << "seed " << seed << " has a FigurationCarrier note without FigurationCommitted";
    EXPECT_TRUE(all_fantasia_stamped)
        << "seed " << seed << " has a FantasiaCarrier note without FantasiaSectionContrast";
    EXPECT_TRUE(all_ground_stamped)
        << "seed " << seed << " has a GroundCarrier note without GroundBassReplayed";
  }
}

// The fixture declares exactly five V0 movement carriers (2 figuration + 3
// fantasia) plus one V1 ground carrier, and the ground material tiles its 4-bar
// period 5x to fill the 20-bar suite.
TEST(SuiteTest, KeyboardSuiteFixtureHasFiveMovementsAndTiledGround) {
  const HarnessFixture fx = buildHarnessFixture(HarnessPhase::KeyboardSuite, 0);

  int figuration_sections = static_cast<int>(fx.material.figuration_sections.size());
  int fantasia_sections = static_cast<int>(fx.material.fantasia_sections.size());
  EXPECT_EQ(figuration_sections, 2) << "Prelude + Courante figuration movements";
  EXPECT_EQ(fantasia_sections, 3) << "Allemande + Sarabande + Gigue fantasia movements";

  // Ground material: a 4-bar period (4 whole-note source notes) tiled across the
  // 20-bar suite (5 clean cycles).
  EXPECT_EQ(fx.material.ground_bass.size(), 4u) << "4-bar ground period";
  EXPECT_EQ(fx.material.ground_bass_period, static_cast<Tick>(4) * kTicksPerBar);
  EXPECT_EQ(static_cast<int>(fx.material.ground_bass_period) / static_cast<int>(kTicksPerBar), 4);

  // Span plan: one V1 ground span + five V0 movement spans.
  int v0_spans = 0;
  int v1_spans = 0;
  for (const auto& span : fx.voice_plan.spans) {
    if (span.voice == 0)
      ++v0_spans;
    else if (span.voice == 1)
      ++v1_spans;
  }
  EXPECT_EQ(v0_spans, 5) << "five dance movement spans on V0";
  EXPECT_EQ(v1_spans, 1) << "single ground span on V1";
}

}  // namespace bach::composer
