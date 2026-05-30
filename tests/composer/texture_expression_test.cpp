// P13 texture / instrument / expression integration tests.
//
// Exercises the Composer's texture-expression post-pass end to end via the
// Phase13 harness fixture: every emitted note must carry the four P13
// provenance bits, the Affekt velocity curve must produce a non-flat
// dynamic arch, and voice density must vary over time (the lowest voice
// enters only at bar 8). The default-constructed TexturePlan no-op
// behavior is covered against a non-texture phase.

#include <gtest/gtest.h>

#include <cstdint>
#include <set>
#include <vector>

#include "composer/composer.h"
#include "composer/harness_fixture.h"
#include "composer/provenance.h"
#include "core/basic_types.h"

namespace bach::composer {
namespace {

bool hasBit(const NoteProvenance& p, RuleBit bit) {
  return (p.satisfied_rules & (RuleIdMask{1} << bit)) != 0;
}

ComposeResult runPhase(HarnessPhase phase, int seed) {
  HarnessFixture fx = buildHarnessFixture(phase, seed);
  return Composer{}.run(fx.material, fx.harmony, fx.voice_plan);
}

}  // namespace

TEST(TextureExpressionTest, EveryNoteCarriesAllFourP13Bits) {
  const ComposeResult r = runPhase(HarnessPhase::Phase13, /*seed=*/0);
  ASSERT_FALSE(r.provenance.empty());
  for (const auto& p : r.provenance) {
    EXPECT_TRUE(hasBit(p, RuleBit::VoiceRangeKept));
    EXPECT_TRUE(hasBit(p, RuleBit::ManualAssigned));
    EXPECT_TRUE(hasBit(p, RuleBit::ArticulationApplied));
    EXPECT_TRUE(hasBit(p, RuleBit::AffektCurveApplied));
  }
}

TEST(TextureExpressionTest, AffektCurveProducesNonFlatVelocity) {
  const ComposeResult r = runPhase(HarnessPhase::Phase13, /*seed=*/0);
  ASSERT_FALSE(r.notes.empty());
  std::set<std::uint8_t> velocities;
  for (const auto& n : r.notes) {
    velocities.insert(n.velocity);
    // Affekt velocities stay in an organ-plausible band near the default 80.
    EXPECT_GE(n.velocity, 70);
    EXPECT_LE(n.velocity, 95);
  }
  // The triangular arch must yield more than one distinct velocity.
  EXPECT_GT(velocities.size(), 1u);
}

TEST(TextureExpressionTest, VoiceDensityVariesOverTime) {
  const ComposeResult r = runPhase(HarnessPhase::Phase13, /*seed=*/0);
  ASSERT_FALSE(r.notes.empty());
  // V2 (the third voice) enters only at bar 8, so the first bar sounds two
  // voices and a later bar sounds three: density genuinely varies.
  auto voicesInBar = [&](int bar) {
    std::set<VoiceId> voices;
    const Tick lo = static_cast<Tick>(bar) * kTicksPerBar;
    const Tick hi = lo + kTicksPerBar;
    for (const auto& n : r.notes) {
      if (n.start_tick >= lo && n.start_tick < hi)
        voices.insert(n.voice);
    }
    return voices.size();
  };
  EXPECT_EQ(voicesInBar(0), 2u);
  EXPECT_EQ(voicesInBar(9), 3u);
}

TEST(TextureExpressionTest, NoP13BitsWithoutTexturePlan) {
  // Phase7 shares Phase13's exposition layout but declares no TexturePlan,
  // so the post-pass is a no-op: none of the four P13 bits appear, and the
  // velocity stays at the renderer default.
  const ComposeResult r = runPhase(HarnessPhase::Phase7, /*seed=*/0);
  ASSERT_FALSE(r.provenance.empty());
  for (const auto& p : r.provenance) {
    EXPECT_FALSE(hasBit(p, RuleBit::VoiceRangeKept));
    EXPECT_FALSE(hasBit(p, RuleBit::ManualAssigned));
    EXPECT_FALSE(hasBit(p, RuleBit::ArticulationApplied));
    EXPECT_FALSE(hasBit(p, RuleBit::AffektCurveApplied));
  }
  for (const auto& n : r.notes) {
    EXPECT_EQ(n.velocity, 80);
  }
}

TEST(TextureExpressionTest, ValidatorAcceptsPhase13Output) {
  // Generous declared ranges bound every candidate pitch, so
  // voice_range_integrity must not fire on a valid seed.
  const ComposeResult r = runPhase(HarnessPhase::Phase13, /*seed=*/1);
  for (const auto& f : r.validation.failures) {
    EXPECT_NE(f.rule_id, "voice_range_integrity");
    EXPECT_NE(f.rule_id, "pedal_range_soft_penalty");
  }
}

}  // namespace bach::composer
