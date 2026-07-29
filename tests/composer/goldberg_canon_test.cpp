// Goldberg full 30-variation set with canons. These tests cover the
// canon-extended buildGoldbergVariationsForm builder (form_cantus.cpp), which
// realizes a BWV988-style aria + N variations (+ da capo) with every third
// variation (3, 6, ..., 27) a canon at a rising imitation interval, the final
// variation (30) a two-tune Quodlibet, and a three-voice layout
// (V0 principal / V1 canon follower / V2 immutable ground) strictly ordered by
// register.
//
// Canon correctness is guaranteed by construction and asserted STRUCTURALLY
// (the comes is a constant-interval copy of the dux, delayed one bar and
// truncated at the block end). There is no canon identity provenance bit with
// dedicated Goldberg carrier provenance.

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <map>
#include <set>
#include <vector>

#include "composer/composer.h"
#include "composer/figuration.h"
#include "composer/form_builders.h"
#include "composer/form_director.h"
#include "composer/validator.h"
#include "core/basic_types.h"

namespace bach::composer {
namespace {

constexpr RuleIdMask bit(RuleBit b) {
  return ruleBitMask(b);
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
Tick blockStart(int blk) {
  return static_cast<Tick>(blk * kCycleBars) * kTicksPerBar;
}

// --- Full-set structure -----------------------------------------------------

// N = 128 (the cap) yields exactly aria + 30 variations + da capo = 32 blocks,
// 4 + 120 + 4 bars. The canons sit at variations 3, 6, ..., 27; variation 30 is
// the figuration peak.
TEST(GoldbergCanon, FullSetIsThirtyTwoBlocksWithCanonsAtEveryThird) {
  for (bool minor : {false, true}) {
    const HarnessFixture fx = build(minor, 128, 1);
    const auto& blocks = fx.material.goldberg_variations;
    // 128 / 4 = 32 blocks: aria (block 0) + 30 variations (blocks 1..30) + da
    // capo (block 31).
    ASSERT_EQ(blocks.size(), 32u) << "minor=" << minor;

    // Aria and da capo are the m=2 layout (8 half notes, density 0).
    EXPECT_EQ(blocks.front().notes.size(), 8u);
    EXPECT_EQ(blocks.front().density_level, 0);
    EXPECT_EQ(blocks.back().notes.size(), 8u);
    EXPECT_EQ(blocks.back().density_level, 0);
    EXPECT_TRUE(fx.material.passacaglia_ground.empty());
    EXPECT_TRUE(fx.material.passacaglia_variations.empty());
    EXPECT_TRUE(fx.material.trio_voices.empty());
    ASSERT_EQ(fx.material.goldberg_aria_bass.size(), 32u);

    // Variation v (1-based) == block v. v % 3 == 0 && v < 30 is a canon.
    for (int v = 1; v <= 30; ++v) {
      const GoldbergVariationKind kind = goldbergVariationKind(static_cast<std::size_t>(v - 1));
      if (v % 3 == 0 && v < 30) {
        EXPECT_EQ(kind, GoldbergVariationKind::Canon) << "variation " << v;
      } else if (v == 30) {
        EXPECT_EQ(kind, GoldbergVariationKind::Quodlibet);
      } else {
        EXPECT_EQ(kind, GoldbergVariationKind::Figuration) << "variation " << v;
      }
    }
  }
}

TEST(GoldbergCanon, DaCapoRestatesAriaUntilItsExplicitTerminalCoda) {
  const HarnessFixture fx = build(false, 128, 1);
  const auto& aria = fx.material.goldberg_variations.front().notes;
  const auto& da_capo = fx.material.goldberg_variations.back().notes;
  ASSERT_EQ(aria.size(), da_capo.size());
  const Tick shift = blockStart(31);
  ASSERT_GE(aria.size(), 2u);
  for (std::size_t i = 0; i + 2 < aria.size(); ++i) {
    EXPECT_EQ(da_capo[i].pitch, aria[i].pitch);
    EXPECT_EQ(da_capo[i].duration, aria[i].duration);
    EXPECT_EQ(da_capo[i].start_tick, aria[i].start_tick + shift);
  }
  // The da capo retains the aria's m=2 rhythm, but its final bar is the
  // explicit terminal coda: dominant G resolves to tonic C rather than
  // repeating a ground that may end on V or vi.
  EXPECT_EQ(da_capo[aria.size() - 2].start_tick, aria[aria.size() - 2].start_tick + shift);
  EXPECT_EQ(da_capo.back().start_tick, aria.back().start_tick + shift);
  EXPECT_EQ(da_capo[aria.size() - 2].pitch % 12, 7);
  EXPECT_EQ(da_capo.back().pitch % 12, 0);
}

TEST(GoldbergCanon, AriaBassMutationFailsDedicatedImmutableRule) {
  const HarnessFixture fx = build(false, 128, 1);
  ComposeResult result = Composer{}.run(fx.material, fx.harmony, fx.voice_plan);
  bool mutated = false;
  for (std::size_t i = 0; i < result.notes.size(); ++i) {
    if (result.provenance[i].satisfied_rules & bit(RuleBit::GoldbergBassReplayed)) {
      ++result.notes[i].pitch;
      mutated = true;
      break;
    }
  }
  ASSERT_TRUE(mutated);
  const ValidationReport report =
      Validator{}.validate(result.notes, result.provenance, fx.harmony, fx.material);
  bool found = false;
  for (const auto& failure : report.failures)
    found = found || failure.rule_id == "goldberg_aria_bass_immutable";
  EXPECT_TRUE(found);
}

TEST(GoldbergCanon, MissingAriaBassCarrierFailsClosed) {
  const HarnessFixture fx = build(false, 128, 1);
  std::vector<NoteEvent> notes;
  std::vector<NoteProvenance> provenance;
  const ValidationReport report = Validator{}.validate(notes, provenance, fx.harmony, fx.material);
  bool found = false;
  for (const auto& failure : report.failures)
    found = found || failure.rule_id == "goldberg_aria_bass_immutable";
  EXPECT_TRUE(found);
}

// Variation 30 is the densest figuration tier (design secondary peak), distinct
// from the arc climax block.
TEST(GoldbergCanon, Variation30IsQuodlibetPeak) {
  const HarnessFixture fx = build(false, 128, 1);
  const auto& blocks = fx.material.goldberg_variations;
  ASSERT_EQ(blocks.size(), 32u);
  EXPECT_EQ(goldbergVariationKind(29), GoldbergVariationKind::Quodlibet);
  // Block 30 combines dense principal figuration with a middle-register tune.
  const auto& v30 = blocks[30];
  EXPECT_EQ(v30.density_level, 2) << "variation 30 must be the densest figuration tier";
  // 4 bars x 4 beats x 4 sixteenths = 64 notes.
  EXPECT_EQ(v30.notes.size(), 64u);
  std::size_t inner_notes = 0;
  for (const auto& note : fx.material.goldberg_inner_voice) {
    if (note.start_tick >= blockStart(30) && note.start_tick < blockStart(31))
      ++inner_notes;
  }
  EXPECT_EQ(inner_notes, 16u);
}

TEST(GoldbergCanon, ArcDensityIncludesQuarterEighthAndSixteenthFiguration) {
  const HarnessFixture fx = build(false, 128, 1);
  bool found_quarters = false;
  bool found_eighths = false;
  bool found_sixteenths = false;
  for (int v = 1; v <= 30; ++v) {
    if (goldbergVariationKind(static_cast<std::size_t>(v - 1)) == GoldbergVariationKind::Canon)
      continue;
    const auto& notes = fx.material.goldberg_variations[static_cast<std::size_t>(v)].notes;
    ASSERT_FALSE(notes.empty()) << "variation " << v;
    found_quarters = found_quarters || notes.front().duration == kTicksPerBeat;
    found_eighths = found_eighths || notes.front().duration == kTicksPerBeat / 2;
    found_sixteenths = found_sixteenths || notes.front().duration == kTicksPerBeat / 4;
  }
  EXPECT_TRUE(found_quarters);
  EXPECT_TRUE(found_eighths);
  EXPECT_TRUE(found_sixteenths);
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

// For every canon variation, the comes is a constant-semitone copy of the dux,
// delayed exactly one bar and truncated at the block end. Canons through the
// fourth imitate below; fifth-and-wider canons imitate above.
TEST(GoldbergCanon, ComesUsesOneFixedIntervalDelayDirectionAndTruncation) {
  for (bool minor : {false, true}) {
    const detail::Mode mode = minor ? detail::Mode::Minor : detail::Mode::Major;
    const HarnessFixture fx = build(minor, 128, 1);
    const auto& blocks = fx.material.goldberg_variations;
    ASSERT_EQ(blocks.size(), 32u);
    const auto& inner = fx.material.goldberg_inner_voice;
    ASSERT_FALSE(inner.empty()) << "minor=" << minor;

    for (int v = 3; v < 30; v += 3) {
      const int blk = v;  // block index == variation number.
      const auto& principal = blocks[static_cast<std::size_t>(blk)].notes;
      const int canon_number = v / 3;
      const int imitation_degrees = canon_number - 1;
      const bool imitate_above = imitation_degrees >= 4;
      const Tick block_end = blockStart(blk + 1);

      std::vector<const MaterialNote*> block_inner;
      for (const auto& note : inner) {
        if (note.start_tick >= blockStart(blk) && note.start_tick < block_end)
          block_inner.push_back(&note);
      }
      const std::vector<const MaterialNote*>* dux = nullptr;
      const std::vector<const MaterialNote*>* comes = nullptr;
      std::vector<const MaterialNote*> block_principal;
      for (const auto& note : principal)
        block_principal.push_back(&note);
      if (imitate_above) {
        dux = &block_inner;
        comes = &block_principal;
      } else {
        dux = &block_principal;
        comes = &block_inner;
      }
      ASSERT_EQ(dux->size(), 24u) << "variation " << v << " dux note count";
      ASSERT_EQ(comes->size(), 18u) << "variation " << v << " comes note count";
      EXPECT_GE(comes->front()->start_tick, blockStart(blk) + kTicksPerBar)
          << "variation " << v << " comes must start one bar late";

      const int diatonic_semitones = transposeUp(72, imitation_degrees, mode) - 72;
      const int expected_shift = imitate_above ? diatonic_semitones + 12 : diatonic_semitones - 24;
      for (std::size_t k = 0; k < comes->size(); ++k) {
        const MaterialNote& ld = *(*dux)[k];
        const MaterialNote& fo = *(*comes)[k];
        // Delay: exactly one bar.
        EXPECT_EQ(fo.start_tick, ld.start_tick + kTicksPerBar)
            << "variation " << v << " note " << k << " onset delay";
        EXPECT_EQ(fo.duration, ld.duration) << "variation " << v << " note " << k << " duration";
        EXPECT_EQ(static_cast<int>(fo.pitch) - static_cast<int>(ld.pitch), expected_shift)
            << "variation " << v << " note " << k << " fixed imitation interval";
        if (imitate_above)
          EXPECT_GT(fo.pitch, ld.pitch) << "variation " << v << " comes must be above";
        else
          EXPECT_LT(fo.pitch, ld.pitch) << "variation " << v << " comes must be below";
      }
    }
  }
}

TEST(GoldbergCanon, EveryCanonHasADistinctPitchSequence) {
  const HarnessFixture fx = build(false, 128, 1);
  std::set<std::vector<int>> pitch_sequences;
  for (int v = 3; v < 30; v += 3) {
    const Tick end = blockStart(v + 1);
    std::vector<int> sequence;
    for (const auto& note : fx.material.goldberg_variations[static_cast<std::size_t>(v)].notes)
      sequence.push_back(static_cast<int>(note.pitch));
    sequence.push_back(-1);  // preserve the physical voice boundary.
    for (const auto& note : fx.material.goldberg_inner_voice) {
      if (note.start_tick >= blockStart(v) && note.start_tick < end)
        sequence.push_back(static_cast<int>(note.pitch));
    }
    EXPECT_TRUE(pitch_sequences.insert(sequence).second)
        << "canon variation " << v << " duplicates an earlier pitch sequence";
  }
  EXPECT_EQ(pitch_sequences.size(), 9u);
}

// V1 sounds only in canon blocks and the dedicated Quodlibet slot.
TEST(GoldbergCanon, V1SoundsOnlyInCanonAndQuodlibetBlocks) {
  const HarnessFixture fx = build(false, 128, 1);
  const ComposeResult r = Composer{}.run(fx.material, fx.harmony, fx.voice_plan);

  // Canon block windows (variations 3,6,...,27 => blocks 3,6,...,27).
  auto inInnerVoiceBlock = [](Tick t) -> bool {
    for (int v = 3; v < 30; v += 3) {
      if (t >= blockStart(v) && t < blockStart(v + 1))
        return true;
    }
    return t >= blockStart(30) && t < blockStart(31);
  };
  int v1_notes = 0;
  for (const auto& n : r.notes) {
    if (n.voice != 1)
      continue;
    ++v1_notes;
    EXPECT_TRUE(inInnerVoiceBlock(n.start_tick))
        << "V1 note outside canon/Quodlibet blocks at tick " << n.start_tick;
  }
  EXPECT_GT(v1_notes, 0) << "the full set must contain canon-follower notes on V1";
}

// The immutable aria ground (V2) tiles its four-bar period until the final
// bar, which is deliberately replaced by the terminal tonic coda.
TEST(GoldbergCanon, GroundTilesExactlyUntilTerminalCoda) {
  for (bool minor : {false, true}) {
    const HarnessFixture fx = build(minor, 128, 1);
    const ComposeResult r = Composer{}.run(fx.material, fx.harmony, fx.voice_plan);
    std::map<Tick, std::uint8_t> ground;
    for (std::size_t i = 0; i < r.notes.size(); ++i) {
      if (r.provenance[i].satisfied_rules & bit(RuleBit::GoldbergBassReplayed)) {
        ground[r.notes[i].start_tick] = r.notes[i].pitch;
        EXPECT_EQ(r.notes[i].voice, 2) << "ground must be the lowest voice V2";
      }
    }
    ASSERT_EQ(static_cast<int>(ground.size()), 127 * 8 - 2) << "minor=" << minor;
    ASSERT_EQ(fx.material.goldberg_aria_bass.size(), 32u);
    for (int bar = 0; bar < 127; ++bar) {
      const auto it = ground.find(static_cast<Tick>(bar) * kTicksPerBar);
      ASSERT_NE(it, ground.end()) << "bar " << bar;
      EXPECT_EQ(it->second,
                fx.material.goldberg_aria_bass[static_cast<std::size_t>(bar % 4) * 8].pitch)
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
  const auto& blocks = fx.material.goldberg_variations;
  ASSERT_EQ(blocks.size(), 16u);  // 64 / 4.
  // Da capo present: last block is the m=2 aria.
  EXPECT_EQ(blocks.back().notes.size(), 8u);
  EXPECT_EQ(blocks.back().density_level, 0);
  // Variations 1..14 (blocks 1..14); da capo block is block 15.
  for (int v = 1; v <= 14; ++v) {
    const bool expect_canon = (v % 3 == 0 && v < 30);
    EXPECT_EQ(
        goldbergVariationKind(static_cast<std::size_t>(v - 1)) == GoldbergVariationKind::Canon,
        expect_canon)
        << "variation " << v;
  }
  // The realized canon follower exists (variations 3,6,9,12 are canons).
  EXPECT_FALSE(fx.material.goldberg_inner_voice.empty());

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
    const auto& blocks = fx.material.goldberg_variations;
    ASSERT_EQ(blocks.size(), 3u);  // 12 / 4 = aria + 2 variations.
    // No da capo (< 24): the last block is a figuration variation, not the aria.
    EXPECT_GT(blocks.back().notes.size(), 8u) << "no da capo below 24 bars";
    // Variations 1 and 2 only; neither is a canon.
    EXPECT_EQ(goldbergVariationKind(0u), GoldbergVariationKind::Figuration);
    EXPECT_EQ(goldbergVariationKind(1u), GoldbergVariationKind::Figuration);
    // No canon => no follower line.
    EXPECT_TRUE(fx.material.goldberg_inner_voice.empty()) << "minor=" << minor;

    const ComposeResult r = Composer{}.run(fx.material, fx.harmony, fx.voice_plan);
    EXPECT_EQ(r.validation.status, ValidationStatus::Ok)
        << "minor=" << minor << " first="
        << (r.validation.failures.empty() ? "" : r.validation.failures.front().rule_id);
    EXPECT_TRUE(r.validation.failures.empty());
  }
}

}  // namespace
}  // namespace bach::composer
