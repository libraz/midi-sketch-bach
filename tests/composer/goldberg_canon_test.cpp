// Goldberg full 30-variation set with canons. These tests cover the
// canon-extended buildGoldbergVariationsForm builder (form_cantus.cpp), which
// realizes a BWV988-style aria + N variations (+ da capo) with every third
// variation (3, 6, ..., 27) a canon at a rising imitation interval, the final
// variation (30) the densest figuration peak, and a three-voice layout
// (V0 principal / V1 canon follower / V2 immutable ground) strictly ordered by
// register.
//
// Canon correctness is guaranteed by construction and asserted STRUCTURALLY
// (the follower is a diatonic transposition of the leader, delayed one bar and
// truncated at the block end). There is no canon identity provenance bit
// (RuleIdMask is full): the follower carries TrioVoiceIndependent (its reused
// carrier's bit), and the structural assertions below pin the canon shape.

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <map>
#include <vector>

#include "composer/composer.h"
#include "composer/figuration.h"
#include "composer/form_builders.h"
#include "composer/form_director.h"
#include "core/basic_types.h"

namespace bach::composer {
namespace {

constexpr RuleIdMask bit(RuleBit b) {
  return RuleIdMask{1} << b;
}

HarnessFixture build(bool minor, std::uint16_t target_bars, std::uint32_t seed) {
  ComposeRequest req;
  req.form = FormType::GoldbergVariations;
  req.is_minor = minor;
  req.character = SubjectCharacter::Severe;
  req.target_bars = target_bars;
  req.seed = seed;
  HarnessFixture fx;
  EXPECT_EQ(buildFormFixture(req, &fx), FormDirectorStatus::Ok);
  return fx;
}

constexpr std::array<std::uint32_t, 4> kSeeds = {{1, 5, 42, 99}};

// detail::scaleUp mirror for the structural canon-transposition check.
int transposeUp(int pitch, int degrees, detail::Mode mode) {
  return degrees <= 0 ? pitch : detail::scaleUp(pitch, degrees, mode);
}

// Block index of the b-th 4-bar block (0 = aria).
constexpr int kCycleBars = 4;
Tick blockStart(int blk) { return static_cast<Tick>(blk * kCycleBars) * kTicksPerBar; }

// --- Full-set structure -----------------------------------------------------

// N = 128 (the cap) yields exactly aria + 30 variations + da capo = 32 blocks,
// 4 + 120 + 4 bars. The canons sit at variations 3, 6, ..., 27; variation 30 is
// the figuration peak.
TEST(GoldbergCanon, FullSetIsThirtyTwoBlocksWithCanonsAtEveryThird) {
  for (bool minor : {false, true}) {
    const HarnessFixture fx = build(minor, 128, 1);
    const auto& blocks = fx.material.passacaglia_variations;
    // 128 / 4 = 32 blocks: aria (block 0) + 30 variations (blocks 1..30) + da
    // capo (block 31).
    ASSERT_EQ(blocks.size(), 32u) << "minor=" << minor;

    // Aria and da capo are the m=2 layout (8 half notes, density 0).
    EXPECT_EQ(blocks.front().notes.size(), 8u);
    EXPECT_EQ(blocks.front().density_level, 0);
    EXPECT_EQ(blocks.back().notes.size(), 8u);
    EXPECT_EQ(blocks.back().density_level, 0);

    // Variation v (1-based) == block v. v % 3 == 0 && v < 30 is a canon.
    for (int v = 1; v <= 30; ++v) {
      const GoldbergVariationKind kind = goldbergVariationKind(static_cast<std::size_t>(v - 1));
      if (v % 3 == 0 && v < 30) {
        EXPECT_EQ(kind, GoldbergVariationKind::Canon) << "variation " << v;
      } else {
        EXPECT_EQ(kind, GoldbergVariationKind::Figuration) << "variation " << v;
      }
    }
  }
}

// Variation 30 is the densest figuration tier (design secondary peak), distinct
// from the arc climax block.
TEST(GoldbergCanon, Variation30IsFigurationPeak) {
  const HarnessFixture fx = build(false, 128, 1);
  const auto& blocks = fx.material.passacaglia_variations;
  ASSERT_EQ(blocks.size(), 32u);
  // Block 30 == variation 30: figuration, density tier 2 (sixteenths).
  const auto& v30 = blocks[30];
  EXPECT_EQ(v30.density_level, 2) << "variation 30 must be the densest figuration tier";
  // 4 bars x 4 beats x 4 sixteenths = 64 notes.
  EXPECT_EQ(v30.notes.size(), 64u);
}

// Full set validates cleanly and is deterministic across the seed x mode matrix.
TEST(GoldbergCanon, FullSetValidatesCleanAndDeterministic) {
  for (bool minor : {false, true}) {
    for (std::uint32_t seed : kSeeds) {
      const HarnessFixture a = build(minor, 128, seed);
      const HarnessFixture b = build(minor, 128, seed);
      const ComposeResult ra = Composer{}.run(a.material, a.harmony, a.voice_plan);
      const ComposeResult rb = Composer{}.run(b.material, b.harmony, b.voice_plan);
      EXPECT_EQ(ra.validation.status, ValidationStatus::Ok)
          << "minor=" << minor << " seed=" << seed << " first="
          << (ra.validation.failures.empty() ? "" : ra.validation.failures.front().rule_id);
      EXPECT_TRUE(ra.validation.failures.empty());
      ASSERT_EQ(ra.notes.size(), rb.notes.size()) << "minor=" << minor << " seed=" << seed;
      for (std::size_t i = 0; i < ra.notes.size(); ++i) {
        EXPECT_EQ(ra.notes[i].pitch, rb.notes[i].pitch) << "note " << i;
        EXPECT_EQ(ra.notes[i].start_tick, rb.notes[i].start_tick) << "note " << i;
        EXPECT_EQ(ra.notes[i].duration, rb.notes[i].duration) << "note " << i;
        EXPECT_EQ(ra.notes[i].voice, rb.notes[i].voice) << "note " << i;
      }
    }
  }
}

// --- Canon structural assertions --------------------------------------------

// For every canon variation: the follower (V1) note k pitch equals the diatonic
// transposition of the corresponding leader (V0) note by (canon_number - 1)
// degrees, folded down into the V1 band; the follower onset is delayed exactly
// one bar; and the follower is truncated at the block end (3 of the leader's
// 4 bars sound).
TEST(GoldbergCanon, FollowerIsDiatonicTransposeDelayedAndTruncated) {
  for (bool minor : {false, true}) {
    const detail::Mode mode = minor ? detail::Mode::Minor : detail::Mode::Major;
    const HarnessFixture fx = build(minor, 128, 1);
    const auto& blocks = fx.material.passacaglia_variations;
    ASSERT_EQ(blocks.size(), 32u);
    ASSERT_EQ(fx.material.trio_voices.size(), 1u) << "minor=" << minor;
    const auto& follower = fx.material.trio_voices.front().notes;
    EXPECT_EQ(fx.material.trio_voices.front().voice, 1);

    for (int v = 3; v < 30; v += 3) {
      const int blk = v;  // block index == variation number.
      const auto& leader = blocks[static_cast<std::size_t>(blk)].notes;
      const int canon_number = v / 3;
      const int imitation_degrees = canon_number - 1;
      const Tick block_end = blockStart(blk + 1);

      // The leader notes whose delayed (+1 bar) onset stays inside the block are
      // the ones the follower restates, in order.
      std::vector<const MaterialNote*> expected_leaders;
      for (const auto& ln : leader) {
        if (ln.start_tick + kTicksPerBar < block_end)
          expected_leaders.push_back(&ln);
      }
      ASSERT_FALSE(expected_leaders.empty()) << "variation " << v;

      // The follower notes for this block, in order.
      std::vector<const MaterialNote*> block_follower;
      for (const auto& fn : follower) {
        if (fn.start_tick >= blockStart(blk) && fn.start_tick < block_end)
          block_follower.push_back(&fn);
      }
      ASSERT_EQ(block_follower.size(), expected_leaders.size())
          << "variation " << v << " follower note count";

      // Last leader bar is dropped (truncation): no follower onset in bar 0 of
      // the block, and the follower spans bars 1..3 only.
      EXPECT_GE(block_follower.front()->start_tick, blockStart(blk) + kTicksPerBar)
          << "variation " << v << " follower must start one bar late";

      for (std::size_t k = 0; k < expected_leaders.size(); ++k) {
        const MaterialNote& ld = *expected_leaders[k];
        const MaterialNote& fo = *block_follower[k];
        // Delay: exactly one bar.
        EXPECT_EQ(fo.start_tick, ld.start_tick + kTicksPerBar)
            << "variation " << v << " note " << k << " onset delay";
        EXPECT_EQ(fo.duration, ld.duration) << "variation " << v << " note " << k << " duration";
        // Diatonic transpose + octave fold: the follower pitch class equals the
        // transposed leader pitch class, and the follower sits below the leader.
        const int transposed = transposeUp(static_cast<int>(ld.pitch), imitation_degrees, mode);
        EXPECT_EQ(fo.pitch % 12, transposed % 12)
            << "variation " << v << " note " << k << " follower pitch class";
        EXPECT_LT(fo.pitch, ld.pitch) << "variation " << v << " note " << k
                                      << " follower must be below the leader";
      }
    }
  }
}

// The follower line is the ONLY material in V1: V1 is silent in every non-canon
// (aria / figuration / da capo) block. No V1 note onset falls outside a canon
// block window.
TEST(GoldbergCanon, V1SilentOutsideCanonBlocks) {
  const HarnessFixture fx = build(false, 128, 1);
  const ComposeResult r = Composer{}.run(fx.material, fx.harmony, fx.voice_plan);

  // Canon block windows (variations 3,6,...,27 => blocks 3,6,...,27).
  auto inCanonBlock = [](Tick t) -> bool {
    for (int v = 3; v < 30; v += 3) {
      if (t >= blockStart(v) && t < blockStart(v + 1))
        return true;
    }
    return false;
  };
  int v1_notes = 0;
  for (const auto& n : r.notes) {
    if (n.voice != 1)
      continue;
    ++v1_notes;
    EXPECT_TRUE(inCanonBlock(n.start_tick)) << "V1 note outside a canon block at tick "
                                            << n.start_tick;
  }
  EXPECT_GT(v1_notes, 0) << "the full set must contain canon-follower notes on V1";
}

// The immutable ground (V2) tiles exactly with a 4-bar period through all 32
// blocks: one ground note per bar, repeating period-4, for the whole 128 bars.
TEST(GoldbergCanon, GroundTilesExactlyAcrossAllBlocks) {
  for (bool minor : {false, true}) {
    const HarnessFixture fx = build(minor, 128, 1);
    const ComposeResult r = Composer{}.run(fx.material, fx.harmony, fx.voice_plan);
    std::map<Tick, std::uint8_t> ground;
    for (std::size_t i = 0; i < r.notes.size(); ++i) {
      if (r.provenance[i].satisfied_rules & bit(RuleBit::PassacagliaGroundReplayed)) {
        ground[r.notes[i].start_tick] = r.notes[i].pitch;
        EXPECT_EQ(r.notes[i].voice, 2) << "ground must be the lowest voice V2";
      }
    }
    ASSERT_EQ(static_cast<int>(ground.size()), 128) << "minor=" << minor;
    ASSERT_EQ(fx.material.passacaglia_ground.size(), 4u);
    for (int bar = 0; bar < 128; ++bar) {
      const auto it = ground.find(static_cast<Tick>(bar) * kTicksPerBar);
      ASSERT_NE(it, ground.end()) << "bar " << bar;
      EXPECT_EQ(it->second, fx.material.passacaglia_ground[static_cast<std::size_t>(bar % 4)].pitch)
          << "minor=" << minor << " bar " << bar;
    }
  }
}

// --- Voice ordering ---------------------------------------------------------

// At every shared onset tick the voices are strictly register-ordered
// V0 >= V1 >= V2, so the validator's voice_crossing rule never fires.
TEST(GoldbergCanon, VoicesStrictlyOrderedByRegister) {
  for (bool minor : {false, true}) {
    for (std::uint32_t seed : kSeeds) {
      const HarnessFixture fx = build(minor, 128, seed);
      const ComposeResult r = Composer{}.run(fx.material, fx.harmony, fx.voice_plan);
      // Group sounding pitches by onset tick per voice.
      std::map<Tick, std::array<int, 3>> sounding;  // -1 = silent.
      for (auto& kv : sounding)
        (void)kv;
      for (const auto& n : r.notes) {
        if (n.voice > 2)
          continue;
        auto& slot = sounding[n.start_tick];
        // Initialize lazily on first touch.
        if (slot[0] == 0 && slot[1] == 0 && slot[2] == 0) {
          slot = {-1, -1, -1};
        }
        slot[n.voice] = n.pitch;
      }
      for (const auto& [tick, pitches] : sounding) {
        if (pitches[0] >= 0 && pitches[1] >= 0)
          EXPECT_GT(pitches[0], pitches[1]) << "V0<=V1 at tick " << tick;
        if (pitches[1] >= 0 && pitches[2] >= 0)
          EXPECT_GT(pitches[1], pitches[2]) << "V1<=V2 at tick " << tick;
        if (pitches[0] >= 0 && pitches[2] >= 0)
          EXPECT_GT(pitches[0], pitches[2]) << "V0<=V2 at tick " << tick;
      }
    }
  }
}

// --- Mid sizes and minimum --------------------------------------------------

// N = 64: 16 blocks (aria + 14 variations + da capo). Canons at variations 3,
// 6, 9, 12 (all < 30 and divisible by 3 within the 14 variations); da capo
// present (>= 24).
TEST(GoldbergCanon, MidSizeKindsAndDaCapo) {
  const HarnessFixture fx = build(false, 64, 1);
  const auto& blocks = fx.material.passacaglia_variations;
  ASSERT_EQ(blocks.size(), 16u);  // 64 / 4.
  // Da capo present: last block is the m=2 aria.
  EXPECT_EQ(blocks.back().notes.size(), 8u);
  EXPECT_EQ(blocks.back().density_level, 0);
  // Variations 1..14 (blocks 1..14); da capo block is block 15.
  for (int v = 1; v <= 14; ++v) {
    const bool expect_canon = (v % 3 == 0 && v < 30);
    EXPECT_EQ(goldbergVariationKind(static_cast<std::size_t>(v - 1)) ==
                  GoldbergVariationKind::Canon,
              expect_canon)
        << "variation " << v;
  }
  // The realized canon follower exists (variations 3,6,9,12 are canons).
  ASSERT_EQ(fx.material.trio_voices.size(), 1u);
  EXPECT_FALSE(fx.material.trio_voices.front().notes.empty());

  const ComposeResult r = Composer{}.run(fx.material, fx.harmony, fx.voice_plan);
  EXPECT_EQ(r.validation.status, ValidationStatus::Ok)
      << (r.validation.failures.empty() ? "" : r.validation.failures.front().rule_id);
}

// N = 12 (the minimum): aria + 2 variations, no da capo (< 24), and NO canon
// (the first canon is variation 3, which does not exist for K = 2). The builder
// degrades gracefully: no trio_voices, clean validation.
TEST(GoldbergCanon, MinimumSizeHasNoCanonAndGracefulDegrade) {
  for (bool minor : {false, true}) {
    const HarnessFixture fx = build(minor, 12, 1);
    const auto& blocks = fx.material.passacaglia_variations;
    ASSERT_EQ(blocks.size(), 3u);  // 12 / 4 = aria + 2 variations.
    // No da capo (< 24): the last block is a figuration variation, not the aria.
    EXPECT_GT(blocks.back().notes.size(), 8u) << "no da capo below 24 bars";
    // Variations 1 and 2 only; neither is a canon.
    EXPECT_EQ(goldbergVariationKind(0u), GoldbergVariationKind::Figuration);
    EXPECT_EQ(goldbergVariationKind(1u), GoldbergVariationKind::Figuration);
    // No canon => no follower line.
    EXPECT_TRUE(fx.material.trio_voices.empty()) << "minor=" << minor;

    const ComposeResult r = Composer{}.run(fx.material, fx.harmony, fx.voice_plan);
    EXPECT_EQ(r.validation.status, ValidationStatus::Ok)
        << "minor=" << minor << " first="
        << (r.validation.failures.empty() ? "" : r.validation.failures.front().rule_id);
    EXPECT_TRUE(r.validation.failures.empty());
  }
}

}  // namespace
}  // namespace bach::composer
