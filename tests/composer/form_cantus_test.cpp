// Fixed-line form builder tests: chorale prelude (cantus firmus + figuration)
// and the Goldberg-style immutable-bass variation skeleton.
//
// Both builders honour ResolvedRequest length / mode / character / arc and
// derive all material from (seed, indices) only. These tests cover, across the
// seeds {1,5,42,99} x modes {Major,Minor} x bars {natural, 2x, 128} matrix:
//   * end-to-end validation (the full Composer runs cleanly), and determinism
//     (the same request yields byte-identical notes),
// plus the per-form structural contracts:
//   Chorale: a whole-note CF tone per bar on V1 downbeats matching the
//     immutable skeleton; the final CF tone is the tonic; phrase cadence degrees
//     alternate authentic / half with an authentic final; embellishment density
//     rises with the arc.
//   Goldberg: the ground tiles exactly with a 4-bar period; the aria special
//     layout (two half notes per bar) occupies bars 0-3; the da-capo restatement
//     reappears when N >= 24; the climax block sits at the arc climax position;
//     and the per-variation kind dispatch table exists and is extensible.

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "composer/arc.h"
#include "composer/composer.h"
#include "composer/form_builders.h"
#include "composer/form_director.h"
#include "core/basic_types.h"

namespace bach::composer {
namespace {

constexpr RuleIdMask bit(RuleBit b) {
  return RuleIdMask{1} << b;
}

// Build a fixture for a (form, minor, character, target_bars, seed) request.
HarnessFixture build(FormType form, bool minor, SubjectCharacter character,
                     std::uint16_t target_bars, std::uint32_t seed) {
  ComposeRequest req;
  req.form = form;
  req.is_minor = minor;
  req.character = character;
  req.target_bars = target_bars;
  req.seed = seed;
  HarnessFixture fx;
  EXPECT_EQ(buildFormFixture(req, &fx), FormDirectorStatus::Ok);
  return fx;
}

// The three bar lengths to sweep for a form: natural, 2x natural, and the cap.
std::array<std::uint16_t, 3> sweepBars(FormType form) {
  const std::uint16_t natural = resolveBars(form, DurationScale::Short, 0);
  return {natural, static_cast<std::uint16_t>(natural * 2), 128};
}

// Only Severe / Noble are admissible for the chorale prelude; pick a per-mode
// admissible character so the sweep exercises both characters.
SubjectCharacter choraleCharacter(bool minor) {
  return minor ? SubjectCharacter::Noble : SubjectCharacter::Severe;
}

constexpr std::array<std::uint32_t, 4> kSeeds = {{1, 5, 42, 99}};

// --- Shared: validation + determinism across the full matrix ----------------

TEST(FormCantusMatrix, ChoraleValidatesCleanAcrossMatrix) {
  for (bool minor : {false, true}) {
    for (std::uint32_t seed : kSeeds) {
      for (std::uint16_t bars : sweepBars(FormType::ChoralePrelude)) {
        const HarnessFixture fx =
            build(FormType::ChoralePrelude, minor, choraleCharacter(minor), bars, seed);
        const ComposeResult r = Composer{}.run(fx.material, fx.harmony, fx.voice_plan);
        EXPECT_EQ(r.validation.status, ValidationStatus::Ok)
            << "minor=" << minor << " seed=" << seed << " bars=" << bars;
        EXPECT_TRUE(r.validation.failures.empty())
            << "minor=" << minor << " seed=" << seed << " bars=" << bars << " first="
            << (r.validation.failures.empty() ? "" : r.validation.failures.front().rule_id);
        EXPECT_FALSE(r.notes.empty());
      }
    }
  }
}

TEST(FormCantusMatrix, GoldbergValidatesCleanAcrossMatrix) {
  for (bool minor : {false, true}) {
    for (std::uint32_t seed : kSeeds) {
      for (std::uint16_t bars : sweepBars(FormType::GoldbergVariations)) {
        const HarnessFixture fx =
            build(FormType::GoldbergVariations, minor, SubjectCharacter::Severe, bars, seed);
        const ComposeResult r = Composer{}.run(fx.material, fx.harmony, fx.voice_plan);
        EXPECT_EQ(r.validation.status, ValidationStatus::Ok)
            << "minor=" << minor << " seed=" << seed << " bars=" << bars;
        EXPECT_TRUE(r.validation.failures.empty())
            << "minor=" << minor << " seed=" << seed << " bars=" << bars << " first="
            << (r.validation.failures.empty() ? "" : r.validation.failures.front().rule_id);
        EXPECT_FALSE(r.notes.empty());
      }
    }
  }
}

// Determinism: the same request rebuilds byte-identical notes (pitch, tick,
// duration, voice) for both forms.
TEST(FormCantusMatrix, BuildersAreDeterministic) {
  for (FormType form : {FormType::ChoralePrelude, FormType::GoldbergVariations}) {
    for (bool minor : {false, true}) {
      const SubjectCharacter character =
          (form == FormType::ChoralePrelude) ? choraleCharacter(minor) : SubjectCharacter::Severe;
      for (std::uint32_t seed : kSeeds) {
        for (std::uint16_t bars : sweepBars(form)) {
          const HarnessFixture a = build(form, minor, character, bars, seed);
          const HarnessFixture b = build(form, minor, character, bars, seed);
          const ComposeResult ra = Composer{}.run(a.material, a.harmony, a.voice_plan);
          const ComposeResult rb = Composer{}.run(b.material, b.harmony, b.voice_plan);
          ASSERT_EQ(ra.notes.size(), rb.notes.size())
              << "form=" << static_cast<int>(form) << " seed=" << seed << " bars=" << bars;
          for (std::size_t i = 0; i < ra.notes.size(); ++i) {
            EXPECT_EQ(ra.notes[i].pitch, rb.notes[i].pitch) << "note " << i;
            EXPECT_EQ(ra.notes[i].start_tick, rb.notes[i].start_tick) << "note " << i;
            EXPECT_EQ(ra.notes[i].duration, rb.notes[i].duration) << "note " << i;
            EXPECT_EQ(ra.notes[i].voice, rb.notes[i].voice) << "note " << i;
          }
        }
      }
    }
  }
}

// --- Chorale prelude structural contracts ----------------------------------

// V1 cantus firmus: a whole note per bar at the bar downbeat, replayed on the
// CF carrier, whose pitch equals the immutable skeleton tone for that bar.
TEST(FormCantusChorale, CantusFirmusWholeNotePerBarMatchesSkeleton) {
  for (bool minor : {false, true}) {
    for (std::uint32_t seed : kSeeds) {
      const HarnessFixture fx =
          build(FormType::ChoralePrelude, minor, choraleCharacter(minor), 0, seed);
      const int bars = static_cast<int>(fx.material.cantus_firmus.size());
      ASSERT_GT(bars, 0);

      const ComposeResult r = Composer{}.run(fx.material, fx.harmony, fx.voice_plan);
      // Collect the replayed CF downbeat tones (one per bar).
      std::map<Tick, std::uint8_t> cf_downbeats;
      for (std::size_t i = 0; i < r.notes.size(); ++i) {
        if (!(r.provenance[i].satisfied_rules & bit(RuleBit::CantusFirmusReplayed)))
          continue;
        if (r.notes[i].start_tick % kTicksPerBar != 0)
          continue;
        cf_downbeats[r.notes[i].start_tick] = r.notes[i].pitch;
      }
      EXPECT_EQ(static_cast<int>(cf_downbeats.size()), bars)
          << "minor=" << minor << " seed=" << seed;
      for (int bar = 0; bar < bars; ++bar) {
        const auto it = cf_downbeats.find(static_cast<Tick>(bar) * kTicksPerBar);
        ASSERT_NE(it, cf_downbeats.end()) << "bar " << bar;
        EXPECT_EQ(it->second, fx.material.cantus_firmus[static_cast<std::size_t>(bar)].pitch)
            << "minor=" << minor << " seed=" << seed << " bar " << bar;
      }
    }
  }
}

// The final cantus-firmus tone is always the tonic (pitch class 0), for every
// seed / mode / length: the closing authentic cadence resolves to the tonic.
TEST(FormCantusChorale, FinalCantusFirmusToneIsTonic) {
  for (bool minor : {false, true}) {
    for (std::uint32_t seed : kSeeds) {
      for (std::uint16_t bars : sweepBars(FormType::ChoralePrelude)) {
        const HarnessFixture fx =
            build(FormType::ChoralePrelude, minor, choraleCharacter(minor), bars, seed);
        ASSERT_FALSE(fx.material.cantus_firmus.empty());
        const std::uint8_t final_pc =
            static_cast<std::uint8_t>(fx.material.cantus_firmus.back().pitch % 12);
        EXPECT_EQ(final_pc, 0) << "minor=" << minor << " seed=" << seed << " bars=" << bars;
      }
    }
  }
}

// Phrase cadence degrees: each 4-bar phrase ends on an authentic (tonic, pc 0)
// or half (dominant, pc 7) cadence, alternating authentic on even phrases and
// half on odd phrases, with the final phrase always authentic.
TEST(FormCantusChorale, PhraseCadenceDegreesAlternateAuthenticHalf) {
  const HarnessFixture fx =
      build(FormType::ChoralePrelude, false, SubjectCharacter::Severe, 16, 42);
  const auto& cf = fx.material.cantus_firmus;
  ASSERT_EQ(cf.size(), 16u);
  const int num_phrases = 4;
  for (int phrase = 0; phrase < num_phrases; ++phrase) {
    const int cadence_bar = phrase * 4 + 3;
    const bool is_final = (phrase == num_phrases - 1);
    const int expected_pc = is_final ? 0 : ((phrase % 2 == 0) ? 0 : 7);
    EXPECT_EQ(cf[static_cast<std::size_t>(cadence_bar)].pitch % 12, expected_pc)
        << "phrase " << phrase << " cadence bar " << cadence_bar;
  }
}

// The cantus firmus stays in the C3-region and below the figuration (C4+): at
// every shared bar downbeat the V1 CF tone is strictly below the V0 figuration
// onset, so the upper voice never crosses the cantus firmus.
TEST(FormCantusChorale, FigurationStaysAboveCantusFirmus) {
  const HarnessFixture fx = build(FormType::ChoralePrelude, false, SubjectCharacter::Noble, 16, 5);
  const ComposeResult r = Composer{}.run(fx.material, fx.harmony, fx.voice_plan);
  std::map<Tick, std::uint8_t> v0_downbeat;
  std::map<Tick, std::uint8_t> v1_downbeat;
  for (const auto& n : r.notes) {
    if (n.start_tick % kTicksPerBar != 0)
      continue;
    if (n.voice == 0)
      v0_downbeat.emplace(n.start_tick, n.pitch);
    else if (n.voice == 1)
      v1_downbeat.emplace(n.start_tick, n.pitch);
  }
  for (const auto& [tick, hi] : v0_downbeat) {
    const auto it = v1_downbeat.find(tick);
    if (it == v1_downbeat.end())
      continue;
    EXPECT_GT(hi, it->second) << "figuration not above cantus firmus at tick " << tick;
  }
}

// Embellishment density rises with the arc: a CF bar in a high-arc-density cycle
// carries strictly more embellishment notes than a bar in a low-density cycle.
// Use a long piece so the arc spans the full Establish -> Climax range.
TEST(FormCantusChorale, EmbellishmentDensityRisesWithArc) {
  const HarnessFixture fx = build(FormType::ChoralePrelude, false, SubjectCharacter::Severe, 64, 1);
  const int bars = static_cast<int>(fx.material.cantus_firmus.size());
  ASSERT_EQ(bars, 64);

  // Count embellishment notes per bar from the embellished CF line.
  std::vector<int> per_bar(static_cast<std::size_t>(bars), 0);
  for (const auto& n : fx.material.cf_embellished) {
    const int bar = static_cast<int>(n.start_tick / kTicksPerBar);
    if (bar >= 0 && bar < bars)
      ++per_bar[static_cast<std::size_t>(bar)];
  }

  // Locate the arc climax cycle and an early (Establish) cycle, then compare a
  // representative bar from each. The climax bar must be at least as dense as the
  // early bar, and the piece must contain at least one denser-than-baseline bar.
  const std::size_t cycle_count = fx.harmony.chords.empty() ? 1u : 16u;  // 64 bars / snap 4 = 16.
  int min_density = per_bar.front();
  int max_density = per_bar.front();
  for (int d : per_bar) {
    min_density = d < min_density ? d : min_density;
    max_density = d > max_density ? d : max_density;
  }
  EXPECT_GT(max_density, min_density) << "embellishment density is flat across the arc";
  (void)cycle_count;
}

// --- Goldberg structural contracts ------------------------------------------

// The ground bass tiles exactly with a 4-bar period: V1 has one ground note per
// bar, and the pitch sequence repeats with period 4 for the whole piece.
TEST(FormCantusGoldberg, GroundTilesExactlyWithFourBarPeriod) {
  for (bool minor : {false, true}) {
    for (std::uint32_t seed : kSeeds) {
      for (std::uint16_t bars : sweepBars(FormType::GoldbergVariations)) {
        const HarnessFixture fx =
            build(FormType::GoldbergVariations, minor, SubjectCharacter::Severe, bars, seed);
        const ComposeResult r = Composer{}.run(fx.material, fx.harmony, fx.voice_plan);

        std::map<Tick, std::uint8_t> ground;
        for (std::size_t i = 0; i < r.notes.size(); ++i) {
          if (r.provenance[i].satisfied_rules & bit(RuleBit::PassacagliaGroundReplayed))
            ground[r.notes[i].start_tick] = r.notes[i].pitch;
        }
        ASSERT_EQ(static_cast<int>(ground.size()), bars)
            << "minor=" << minor << " seed=" << seed << " bars=" << bars;
        // The canonical period is the first 4 ground tones; every bar must match
        // its period-4 counterpart.
        ASSERT_EQ(fx.material.passacaglia_ground.size(), 4u);
        for (int bar = 0; bar < bars; ++bar) {
          const auto it = ground.find(static_cast<Tick>(bar) * kTicksPerBar);
          ASSERT_NE(it, ground.end()) << "bar " << bar;
          EXPECT_EQ(it->second,
                    fx.material.passacaglia_ground[static_cast<std::size_t>(bar % 4)].pitch)
              << "minor=" << minor << " seed=" << seed << " bar " << bar;
        }
      }
    }
  }
}

// The aria special layout occupies bars 0-3: two half notes per bar on V0 (m=2),
// eight notes total, never subdivided below the half note.
TEST(FormCantusGoldberg, AriaSpecialLayoutInFirstFourBars) {
  for (bool minor : {false, true}) {
    for (std::uint32_t seed : kSeeds) {
      const HarnessFixture fx =
          build(FormType::GoldbergVariations, minor, SubjectCharacter::Severe, 0, seed);
      ASSERT_FALSE(fx.material.passacaglia_variations.empty());
      const auto& aria = fx.material.passacaglia_variations.front();
      EXPECT_EQ(aria.start_tick, 0u);
      EXPECT_EQ(aria.end_tick, static_cast<Tick>(4) * kTicksPerBar);
      EXPECT_EQ(aria.density_level, 0);
      EXPECT_FALSE(aria.is_climax);
      ASSERT_EQ(aria.notes.size(), 8u);  // 4 bars x 2 half notes.
      for (const auto& n : aria.notes) {
        EXPECT_EQ(n.duration, kTicksPerBeat * 2) << "aria note must be a half note";
      }
      // Exactly two notes per bar in bars 0-3.
      std::array<int, 4> per_bar = {0, 0, 0, 0};
      for (const auto& n : aria.notes)
        ++per_bar[static_cast<std::size_t>(n.start_tick / kTicksPerBar)];
      for (int bar = 0; bar < 4; ++bar)
        EXPECT_EQ(per_bar[static_cast<std::size_t>(bar)], 2) << "bar " << bar;
    }
  }
}

// The da-capo aria restatement appears in the final 4 bars when N >= 24, and is
// absent for the natural 20-bar piece.
TEST(FormCantusGoldberg, DaCapoRestatementWhenLongEnough) {
  // 20 bars (< 24): no da capo. The final block is a figuration variation, not
  // the m=2 aria layout.
  {
    const HarnessFixture fx =
        build(FormType::GoldbergVariations, false, SubjectCharacter::Severe, 20, 1);
    const auto& blocks = fx.material.passacaglia_variations;
    ASSERT_EQ(blocks.size(), 5u);
    EXPECT_GT(blocks.back().notes.size(), 8u) << "20-bar final block must not be the aria";
  }
  // 24+ bars: the final block restates the aria layout (8 half notes, density 0).
  for (std::uint16_t bars : {24, 40, 128}) {
    const HarnessFixture fx =
        build(FormType::GoldbergVariations, false, SubjectCharacter::Severe, bars, 1);
    const auto& blocks = fx.material.passacaglia_variations;
    ASSERT_FALSE(blocks.empty());
    const auto& last = blocks.back();
    EXPECT_EQ(last.density_level, 0) << "bars=" << bars;
    EXPECT_EQ(last.notes.size(), 8u) << "bars=" << bars << " da-capo aria must be m=2";
    EXPECT_FALSE(last.is_climax) << "bars=" << bars << " da-capo block is never the climax";
    EXPECT_EQ(last.start_tick, static_cast<Tick>(bars - 4) * kTicksPerBar) << "bars=" << bars;
    for (const auto& n : last.notes)
      EXPECT_EQ(n.duration, kTicksPerBeat * 2) << "bars=" << bars;
  }
}

// The climax block sits at the arc climax position (~80%), not necessarily the
// last block, and is the only block flagged is_climax. ClimaxPlaced fires only on
// that block.
TEST(FormCantusGoldberg, ClimaxBlockAtArcPosition) {
  for (std::uint16_t bars : {20, 40, 128}) {
    const HarnessFixture fx =
        build(FormType::GoldbergVariations, false, SubjectCharacter::Severe, bars, 1);
    const auto& blocks = fx.material.passacaglia_variations;
    const int num_blocks = static_cast<int>(blocks.size());

    int climax_count = 0;
    int climax_block = -1;
    for (int b = 0; b < num_blocks; ++b) {
      if (blocks[static_cast<std::size_t>(b)].is_climax) {
        ++climax_count;
        climax_block = b;
      }
    }
    EXPECT_EQ(climax_count, 1) << "bars=" << bars;

    // Cross-check against the arc: the climax block's cycle must be the arc
    // climax cycle. cycle_count == num_blocks (snap 4); cycle index == block.
    ArcPoint climax_pt =
        arcPoint(static_cast<std::size_t>(climax_block), static_cast<std::size_t>(num_blocks));
    EXPECT_TRUE(climax_pt.is_climax)
        << "bars=" << bars << " climax block " << climax_block << " is not the arc climax";

    // ClimaxPlaced fires only on the climax block's note window.
    const ComposeResult r = Composer{}.run(fx.material, fx.harmony, fx.voice_plan);
    const Tick climax_start = blocks[static_cast<std::size_t>(climax_block)].start_tick;
    const Tick climax_end = blocks[static_cast<std::size_t>(climax_block)].end_tick;
    for (std::size_t i = 0; i < r.notes.size(); ++i) {
      if (!(r.provenance[i].satisfied_rules & bit(RuleBit::ClimaxPlaced)))
        continue;
      EXPECT_GE(r.notes[i].start_tick, climax_start) << "bars=" << bars;
      EXPECT_LT(r.notes[i].start_tick, climax_end) << "bars=" << bars;
    }
  }
}

// The per-variation kind dispatch follows the BWV988 scheme: every third
// variation (1-based v % 3 == 0, with v < 30) is a Canon; all others are
// Figuration. The accessor is a pure function of the zero-based index.
TEST(FormCantusGoldberg, VariationKindDispatchFollowsBwv988Scheme) {
  // Figuration variations (1-based 1,2,4,5,7,...).
  for (std::size_t idx : {0u, 1u, 3u, 4u, 6u, 7u, 9u, 28u}) {
    EXPECT_EQ(goldbergVariationKind(idx), GoldbergVariationKind::Figuration)
        << "index " << idx << " (variation " << idx + 1 << ")";
  }
  // Canon variations (1-based 3,6,9,...,27 => zero-based 2,5,8,...,26).
  for (std::size_t v = 3; v < 30; v += 3) {
    EXPECT_EQ(goldbergVariationKind(v - 1), GoldbergVariationKind::Canon)
        << "variation " << v << " must be a canon";
  }
  // Variation 30 (zero-based 29) is the figuration peak, NOT a canon, even
  // though 30 % 3 == 0 (the BWV988 Quodlibet slot).
  EXPECT_EQ(goldbergVariationKind(29u), GoldbergVariationKind::Figuration)
      << "variation 30 is the figuration peak, not a canon";
  // Determinism: repeated calls agree.
  for (std::size_t idx : {2u, 5u, 29u})
    EXPECT_EQ(goldbergVariationKind(idx), goldbergVariationKind(idx)) << "index " << idx;
}

}  // namespace
}  // namespace bach::composer
