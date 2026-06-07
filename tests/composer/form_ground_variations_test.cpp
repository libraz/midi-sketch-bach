// Ground-bass variation form-builder tests (chaconne + passacaglia).
//
// These cover the real form builders in src/composer/form_ground_variations.cpp
// (which replace the placeholder phase-fixture replay). Both forms are 3/4
// ground-bass variations: an immutable ground bass (V1) period-tiled under one
// arc-driven scalar-wave variation block per cycle (V0).
//
// For both forms, across seeds {1,5,42,99} x modes {Major,Minor} x
// bars {natural, 2x natural, 128}:
//   1. buildFormFixture -> Composer::run validates Ok (no failures).
//   2. Determinism: identical request -> byte-identical notes.
//   3. Ground immutability: V1 notes repeat exactly every ground period.
//   4. Bar math is 1440 ticks (last note end ~= bars * 1440).
//   5. Climax block lands at the arc climax (~80%) and stamps ClimaxPlaced.
//   6. Density rises toward the climax (V0 note count per cycle).
//   7. Minor: no Ab->B (pc 8 -> pc 11) adjacency in any V0 line.
//   8. Picardy: minor + even seed final chord carries a major third.

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <vector>

#include "composer/composer.h"
#include "composer/form_director.h"
#include "composer/harmonic_plan.h"
#include "composer/harness_fixture.h"
#include "composer/minor_material.h"
#include "composer/provenance.h"
#include "core/basic_types.h"

namespace bach::composer {
namespace {

constexpr Tick kTicksPerBar34 = 3 * kTicksPerBeat;  // 1440.

constexpr RuleIdMask bit(RuleBit b) {
  return ruleBitMask(b);
}

// Build a fixture for a ground-variation form, asserting the director succeeds.
HarnessFixture build(FormType form, std::uint32_t seed, bool is_minor, std::uint16_t target_bars) {
  ComposeRequest req;
  req.form = form;
  req.is_minor = is_minor;
  req.character = SubjectCharacter::Severe;
  req.target_bars = target_bars;
  req.seed = seed;
  HarnessFixture fx;
  const FormDirectorStatus status = buildFormFixture(req, &fx);
  EXPECT_EQ(status, FormDirectorStatus::Ok);
  return fx;
}

struct Case {
  FormType form;
  std::uint32_t seed;
  bool is_minor;
  std::uint16_t target_bars;
};

// All seed x mode x length combinations for one form. target_bars values:
//   0      -> natural length (resolved by the director).
//   2*nat  -> double natural (computed from the FormSpec natural_bars).
//   128    -> global cap.
std::vector<Case> casesFor(FormType form) {
  const std::uint16_t natural = formSpec(form).natural_bars;
  std::vector<Case> cases;
  for (std::uint32_t seed : {1u, 5u, 42u, 99u}) {
    for (bool minor : {false, true}) {
      cases.push_back({form, seed, minor, 0});
      cases.push_back({form, seed, minor, static_cast<std::uint16_t>(2 * natural)});
      cases.push_back({form, seed, minor, 128});
    }
  }
  return cases;
}

// Resolved bar count for a request (mirrors the director's snap/clamp).
std::uint16_t resolvedBars(FormType form, std::uint16_t target_bars) {
  return resolveBars(form, DurationScale::Short, target_bars);
}

// The ground bass lives on a different voice per form: the chaconne keeps the
// 2-voice layout (ground = V1), the passacaglia is the 3-voice uplift (V0
// principal variation, V1 counter-figuration, V2 = ground).
VoiceId groundVoice(FormType form) {
  return form == FormType::Passacaglia ? 2 : 1;
}

// Collect the ground note pitches (on `ground_voice`) in onset order.
std::vector<std::uint8_t> groundPitches(const ComposeResult& r, VoiceId ground_voice) {
  std::vector<std::pair<Tick, std::uint8_t>> ground;
  for (const auto& n : r.notes) {
    if (n.voice == ground_voice)
      ground.push_back({n.start_tick, n.pitch});
  }
  std::sort(ground.begin(), ground.end());
  std::vector<std::uint8_t> out;
  for (const auto& g : ground)
    out.push_back(g.second);
  return out;
}

// --- 1. Validation Ok for every case ---------------------------------------

void expectValidatesOk(FormType form) {
  for (const Case& c : casesFor(form)) {
    const HarnessFixture fx = build(c.form, c.seed, c.is_minor, c.target_bars);
    const ComposeResult r = Composer{}.run(fx.material, fx.harmony, fx.voice_plan);
    EXPECT_TRUE(r.validation.failures.empty())
        << "form " << static_cast<int>(form) << " seed " << c.seed << " minor " << c.is_minor
        << " bars " << c.target_bars << " first failure="
        << (r.validation.failures.empty() ? "" : r.validation.failures.front().rule_id);
    EXPECT_EQ(r.validation.status, ValidationStatus::Ok);
    // Chaconne keeps the 2-voice layout; the passacaglia is the 3-voice uplift.
    EXPECT_EQ(fx.voice_plan.num_voices, form == FormType::Passacaglia ? 3 : 2);
    ASSERT_FALSE(r.notes.empty());
  }
}

TEST(GroundVariationChaconne, ValidatesOkAcrossSeedsModesLengths) {
  expectValidatesOk(FormType::Chaconne);
}

TEST(GroundVariationPassacaglia, ValidatesOkAcrossSeedsModesLengths) {
  expectValidatesOk(FormType::Passacaglia);
}

// --- 2. Determinism ---------------------------------------------------------

void expectDeterministic(FormType form) {
  for (const Case& c : casesFor(form)) {
    const HarnessFixture a = build(c.form, c.seed, c.is_minor, c.target_bars);
    const HarnessFixture b = build(c.form, c.seed, c.is_minor, c.target_bars);
    const ComposeResult ra = Composer{}.run(a.material, a.harmony, a.voice_plan);
    const ComposeResult rb = Composer{}.run(b.material, b.harmony, b.voice_plan);
    ASSERT_EQ(ra.notes.size(), rb.notes.size());
    for (std::size_t i = 0; i < ra.notes.size(); ++i) {
      EXPECT_EQ(ra.notes[i].start_tick, rb.notes[i].start_tick);
      EXPECT_EQ(ra.notes[i].duration, rb.notes[i].duration);
      EXPECT_EQ(ra.notes[i].pitch, rb.notes[i].pitch);
      EXPECT_EQ(ra.notes[i].voice, rb.notes[i].voice);
    }
  }
}

TEST(GroundVariationChaconne, IsDeterministic) {
  expectDeterministic(FormType::Chaconne);
}

TEST(GroundVariationPassacaglia, IsDeterministic) {
  expectDeterministic(FormType::Passacaglia);
}

// --- 3. Ground immutability -------------------------------------------------

void expectGroundImmutable(FormType form, int cycle_bars) {
  for (const Case& c : casesFor(form)) {
    const HarnessFixture fx = build(c.form, c.seed, c.is_minor, c.target_bars);
    const ComposeResult r = Composer{}.run(fx.material, fx.harmony, fx.voice_plan);
    const std::vector<std::uint8_t> ground = groundPitches(r, groundVoice(c.form));
    const std::size_t period = static_cast<std::size_t>(cycle_bars);
    ASSERT_GE(ground.size(), period);
    EXPECT_EQ(ground.size() % period, 0u) << "ground note count must be a whole number of cycles";
    for (std::size_t i = period; i < ground.size(); ++i) {
      EXPECT_EQ(ground[i], ground[i % period])
          << "ground note " << i << " differs from cycle-0 slot " << (i % period);
    }
  }
}

TEST(GroundVariationChaconne, GroundRepeatsEveryPeriod) {
  expectGroundImmutable(FormType::Chaconne, 4);
}

TEST(GroundVariationPassacaglia, GroundRepeatsEveryPeriod) {
  expectGroundImmutable(FormType::Passacaglia, 8);
}

// --- 3b. Seed-selected ground variants ---------------------------------------

// The ground table is a seed-selected design variant (seed % kGroundVariantCount,
// fixed for the whole piece). Probe one seed per variant: seed 3 -> variant 0
// (the historical table; seed 0 is reserved for CLI auto so the variant-0 probe
// uses 3), seed 1 -> variant 1, seed 2 -> variant 2.
void expectGroundFollowsSeedVariant(FormType form) {
  const std::array<std::uint32_t, 3> probe_seed = {3u, 1u, 2u};
  const std::size_t period = form == FormType::Passacaglia ? 8u : 4u;
  for (bool minor : {false, true}) {
    for (std::size_t variant = 0; variant < detail::kGroundVariantCount; ++variant) {
      const HarnessFixture fx = build(form, probe_seed[variant], minor, 0);
      const ComposeResult r = Composer{}.run(fx.material, fx.harmony, fx.voice_plan);
      const std::vector<std::uint8_t> pitches = groundPitches(r, groundVoice(form));
      ASSERT_GE(pitches.size(), period) << "minor " << minor << " variant " << variant;
      for (std::size_t bar = 0; bar < period; ++bar) {
        const std::uint8_t expected = form == FormType::Passacaglia
                                          ? (minor ? detail::kPassacagliaGroundsMinor[variant][bar]
                                                   : detail::kPassacagliaGroundsMajor[variant][bar])
                                          : (minor ? detail::kChaconneGroundsMinor[variant][bar]
                                                   : detail::kChaconneGroundsMajor[variant][bar]);
        EXPECT_EQ(pitches[bar], expected) << "form " << static_cast<int>(form) << " minor " << minor
                                          << " variant " << variant << " bar " << bar;
      }
    }
  }
}

TEST(GroundVariationChaconne, GroundFollowsSeedVariant) {
  expectGroundFollowsSeedVariant(FormType::Chaconne);
}

TEST(GroundVariationPassacaglia, GroundFollowsSeedVariant) {
  expectGroundFollowsSeedVariant(FormType::Passacaglia);
}

// Seeds congruent mod kGroundVariantCount pick the same ground variant, so the
// variant family wraps (seed 4 sounds the same ground as seed 1).
void expectCongruentSeedsShareGround(FormType form) {
  for (bool minor : {false, true}) {
    const HarnessFixture fx_a = build(form, 1, minor, 0);
    const HarnessFixture fx_b = build(form, 4, minor, 0);
    const ComposeResult r_a = Composer{}.run(fx_a.material, fx_a.harmony, fx_a.voice_plan);
    const ComposeResult r_b = Composer{}.run(fx_b.material, fx_b.harmony, fx_b.voice_plan);
    EXPECT_EQ(groundPitches(r_a, groundVoice(form)), groundPitches(r_b, groundVoice(form)))
        << "form " << static_cast<int>(form) << " minor " << minor;
  }
}

TEST(GroundVariationChaconne, CongruentSeedsShareGround) {
  expectCongruentSeedsShareGround(FormType::Chaconne);
}

TEST(GroundVariationPassacaglia, CongruentSeedsShareGround) {
  expectCongruentSeedsShareGround(FormType::Passacaglia);
}

// --- 4. Bar math (1440 ticks) -----------------------------------------------

void expectBarMath(FormType form) {
  for (const Case& c : casesFor(form)) {
    const HarnessFixture fx = build(c.form, c.seed, c.is_minor, c.target_bars);
    EXPECT_EQ(fx.harmony.ticksPerBar(), kTicksPerBar34)
        << "form " << static_cast<int>(form) << " must derive 1440-tick bars";
    const ComposeResult r = Composer{}.run(fx.material, fx.harmony, fx.voice_plan);
    const std::uint16_t bars = resolvedBars(form, c.target_bars);
    const Tick piece_end = static_cast<Tick>(bars) * kTicksPerBar34;
    Tick last_end = 0;
    for (const auto& n : r.notes)
      last_end = std::max(last_end, n.start_tick + n.duration);
    EXPECT_EQ(last_end, piece_end)
        << "form " << static_cast<int>(form) << " bars " << bars << " last note end mismatch";
  }
}

TEST(GroundVariationChaconne, BarMathIs1440) {
  expectBarMath(FormType::Chaconne);
}

TEST(GroundVariationPassacaglia, BarMathIs1440) {
  expectBarMath(FormType::Passacaglia);
}

// --- 5. Climax position + ClimaxPlaced --------------------------------------

void expectClimaxAtArc(FormType form, int cycle_bars) {
  for (const Case& c : casesFor(form)) {
    const HarnessFixture fx = build(c.form, c.seed, c.is_minor, c.target_bars);
    const ComposeResult r = Composer{}.run(fx.material, fx.harmony, fx.voice_plan);

    const std::uint16_t bars = resolvedBars(form, c.target_bars);
    const std::size_t cycle_count = static_cast<std::size_t>(bars) / cycle_bars;
    // The arc places the single climax at ~80% (climax_idx = cycle_count*4/5,
    // clamped to the last cycle).
    std::size_t climax_idx = (cycle_count * 4) / 5;
    if (climax_idx > cycle_count - 1)
      climax_idx = cycle_count - 1;
    const Tick climax_start =
        static_cast<Tick>(climax_idx) * static_cast<Tick>(cycle_bars) * kTicksPerBar34;
    const Tick climax_end = climax_start + static_cast<Tick>(cycle_bars) * kTicksPerBar34;

    bool saw_climax = false;
    bool climax_in_window = true;
    for (std::size_t i = 0; i < r.notes.size(); ++i) {
      if (i >= r.provenance.size())
        break;
      if (r.provenance[i].satisfied_rules & bit(RuleBit::ClimaxPlaced)) {
        saw_climax = true;
        if (r.notes[i].start_tick < climax_start || r.notes[i].start_tick >= climax_end)
          climax_in_window = false;
      }
    }
    EXPECT_TRUE(saw_climax) << "form " << static_cast<int>(form) << " seed " << c.seed
                            << " missing ClimaxPlaced";
    EXPECT_TRUE(climax_in_window) << "form " << static_cast<int>(form)
                                  << " climax notes outside cycle " << climax_idx;
  }
}

// The chaconne carriers (VariationCarrier) do not stamp ClimaxPlaced (that bit
// is a passacaglia-carrier feature), so only the passacaglia asserts the bit
// here; the chaconne climax placement is covered structurally below by the
// density-rise test (the densest block sits at the climax cycle).
TEST(GroundVariationPassacaglia, ClimaxStampedAtArcPosition) {
  expectClimaxAtArc(FormType::Passacaglia, 8);
}

// --- 6. Density rises toward the climax -------------------------------------

void expectDensityRises(FormType form, int cycle_bars) {
  for (const Case& c : casesFor(form)) {
    const HarnessFixture fx = build(c.form, c.seed, c.is_minor, c.target_bars);
    const ComposeResult r = Composer{}.run(fx.material, fx.harmony, fx.voice_plan);

    const std::uint16_t bars = resolvedBars(form, c.target_bars);
    const std::size_t cycle_count = static_cast<std::size_t>(bars) / cycle_bars;
    const Tick cycle_ticks = static_cast<Tick>(cycle_bars) * kTicksPerBar34;
    std::size_t climax_idx = (cycle_count * 4) / 5;
    if (climax_idx > cycle_count - 1)
      climax_idx = cycle_count - 1;

    // Count V0 notes per cycle.
    std::vector<std::size_t> per_cycle(cycle_count, 0);
    for (const auto& n : r.notes) {
      if (n.voice != 0)
        continue;
      const std::size_t cyc = static_cast<std::size_t>(n.start_tick / cycle_ticks);
      if (cyc < cycle_count)
        ++per_cycle[cyc];
    }
    // The climax cycle is at least as dense as the establishing cycle 0.
    EXPECT_GE(per_cycle[climax_idx], per_cycle[0])
        << "form " << static_cast<int>(form) << " seed " << c.seed
        << " climax cycle not denser than the establishing cycle";
    // The climax cycle is the (joint) densest block in the piece.
    for (std::size_t cyc = 0; cyc < cycle_count; ++cyc) {
      EXPECT_LE(per_cycle[cyc], per_cycle[climax_idx])
          << "form " << static_cast<int>(form) << " cycle " << cyc << " denser than climax";
    }
  }
}

TEST(GroundVariationChaconne, DensityRisesTowardClimax) {
  expectDensityRises(FormType::Chaconne, 4);
}

TEST(GroundVariationPassacaglia, DensityRisesTowardClimax) {
  expectDensityRises(FormType::Passacaglia, 8);
}

// --- 7. Minor: no Ab->B augmented 2nd in V0 ---------------------------------

void expectNoAugmentedSecond(FormType form) {
  for (const Case& c : casesFor(form)) {
    if (!c.is_minor)
      continue;
    const HarnessFixture fx = build(c.form, c.seed, c.is_minor, c.target_bars);
    const ComposeResult r = Composer{}.run(fx.material, fx.harmony, fx.voice_plan);
    // Collect the V0 line in onset order.
    std::vector<std::pair<Tick, std::uint8_t>> line;
    for (const auto& n : r.notes) {
      if (n.voice == 0)
        line.push_back({n.start_tick, n.pitch});
    }
    std::sort(line.begin(), line.end());
    for (std::size_t i = 1; i < line.size(); ++i) {
      const int prev_pc = line[i - 1].second % 12;
      const int cur_pc = line[i].second % 12;
      const bool ab_to_b = (prev_pc == 8 && cur_pc == 11) || (prev_pc == 11 && cur_pc == 8);
      EXPECT_FALSE(ab_to_b) << "form " << static_cast<int>(form) << " seed " << c.seed
                            << " Ab->B augmented 2nd at V0 index " << i;
    }
  }
}

TEST(GroundVariationChaconne, MinorHasNoAugmentedSecond) {
  expectNoAugmentedSecond(FormType::Chaconne);
}

TEST(GroundVariationPassacaglia, MinorHasNoAugmentedSecond) {
  expectNoAugmentedSecond(FormType::Passacaglia);
}

// --- 8. Picardy: minor + even seed final chord major ------------------------

void expectPicardyFinalChord(FormType form) {
  for (const Case& c : casesFor(form)) {
    if (!c.is_minor)
      continue;
    const HarnessFixture fx = build(c.form, c.seed, c.is_minor, c.target_bars);
    ASSERT_FALSE(fx.harmony.chords.empty());
    const ChordEvent& last = fx.harmony.chords.back();
    if ((c.seed & 1u) == 0u) {
      // Even seed: usePicardy -> the closing tonic chord is C major.
      EXPECT_EQ(last.root_pc, 0u) << "form " << static_cast<int>(form) << " seed " << c.seed
                                  << " Picardy root";
      EXPECT_EQ(last.quality, ChordQuality::Major)
          << "form " << static_cast<int>(form) << " seed " << c.seed << " Picardy quality";
    } else {
      // Odd seed: no Picardy, the closing chord stays the diatonic minor tonic.
      EXPECT_EQ(last.quality, ChordQuality::Minor)
          << "form " << static_cast<int>(form) << " seed " << c.seed << " non-Picardy quality";
    }
  }
}

TEST(GroundVariationChaconne, PicardyFinalChordOnEvenSeed) {
  expectPicardyFinalChord(FormType::Chaconne);
}

TEST(GroundVariationPassacaglia, PicardyFinalChordOnEvenSeed) {
  expectPicardyFinalChord(FormType::Passacaglia);
}

// The 3-voice passacaglia uplift must NOT bleed into the chaconne: the chaconne
// keeps its 2-voice layout (V0 variation over V1 ground) on every seed/mode/
// length, so its output stays byte-stable against the passacaglia change.
TEST(GroundVariationChaconne, StaysTwoVoiceWithGroundOnVoiceOne) {
  for (const Case& c : casesFor(FormType::Chaconne)) {
    const HarnessFixture fx = build(c.form, c.seed, c.is_minor, c.target_bars);
    EXPECT_EQ(fx.voice_plan.num_voices, 2);
    const ComposeResult r = Composer{}.run(fx.material, fx.harmony, fx.voice_plan);
    bool saw_voice_two = false;
    bool saw_ground_v1 = false;
    for (const auto& n : r.notes) {
      if (n.voice >= 2)
        saw_voice_two = true;
      if (n.voice == 1)
        saw_ground_v1 = true;
    }
    EXPECT_FALSE(saw_voice_two) << "chaconne must not introduce a third voice";
    EXPECT_TRUE(saw_ground_v1) << "chaconne ground must stay on voice 1";
  }
}

// --- 9. Passacaglia terraced growth schedule (period-derived) ---------------
//
// The 3-voice passacaglia derives its per-cycle voice-presence schedule purely
// from the period (cycle) count:
//   periods == 3: cycle 0 = V0 + ground (no intro), cycle 1 adds V1, the climax
//                 cycle 2 is the full texture.
//   periods >= 4: cycle 0 = ground-solo intro (V0 and V1 rest), one receding
//                 cycle rests V1, the climax cycle sounds all three.
// The ground (V2) sounds in every cycle. This locks the design table directly
// from the composed output (which 8-bar period each voice sounds in).

// Per-cycle voice presence for a 3-voice passacaglia: presence[cycle][voice].
std::vector<std::array<bool, 3>> passacagliaPresence(std::uint32_t seed, std::uint16_t target_bars,
                                                     std::size_t& out_cycles) {
  const HarnessFixture fx = build(FormType::Passacaglia, seed, /*minor=*/false, target_bars);
  const ComposeResult r = Composer{}.run(fx.material, fx.harmony, fx.voice_plan);
  const Tick period = 8 * kTicksPerBar34;  // passacaglia ground = 8 bars.
  const std::uint16_t bars = resolvedBars(FormType::Passacaglia, target_bars);
  const std::size_t cycles = static_cast<std::size_t>(bars) / 8;
  out_cycles = cycles;
  std::vector<std::array<bool, 3>> presence(cycles, {false, false, false});
  for (const auto& n : r.notes) {
    const std::size_t cyc = static_cast<std::size_t>(n.start_tick / period);
    if (cyc < cycles && n.voice < 3)
      presence[cyc][n.voice] = true;
  }
  return presence;
}

TEST(GroundVariationPassacaglia, ThreePeriodScheduleHasNoIntroAndTerracesV1) {
  // Default 24-bar passacaglia = exactly 3 periods.
  std::size_t cycles = 0;
  const auto presence = passacagliaPresence(/*seed=*/3, /*bars=*/0, cycles);
  ASSERT_EQ(cycles, 3u);
  // Cycle 0: V0 + ground, V1 rests (no intro terrace at 3 periods).
  EXPECT_TRUE(presence[0][0]) << "3-period cycle 0 must carry V0";
  EXPECT_FALSE(presence[0][1]) << "3-period cycle 0 must rest V1";
  EXPECT_TRUE(presence[0][2]) << "ground must sound in cycle 0";
  // Cycle 1 adds V1.
  EXPECT_TRUE(presence[1][0]);
  EXPECT_TRUE(presence[1][1]) << "3-period cycle 1 must add V1";
  EXPECT_TRUE(presence[1][2]);
  // Climax cycle 2: full texture.
  EXPECT_TRUE(presence[2][0]);
  EXPECT_TRUE(presence[2][1]) << "3-period climax must sound all three voices";
  EXPECT_TRUE(presence[2][2]);
}

TEST(GroundVariationPassacaglia, LongScheduleHasGroundSoloIntroAndRecedingCycle) {
  // 40-bar passacaglia = 5 periods (exercises the intro + receding-V1 path).
  std::size_t cycles = 0;
  const auto presence = passacagliaPresence(/*seed=*/1, /*bars=*/40, cycles);
  ASSERT_EQ(cycles, 5u);
  // Cycle 0 is a ground-solo intro: only V2 (ground) sounds.
  EXPECT_FALSE(presence[0][0]) << "intro cycle must rest V0";
  EXPECT_FALSE(presence[0][1]) << "intro cycle must rest V1";
  EXPECT_TRUE(presence[0][2]) << "intro cycle must carry the ground";
  // The ground sounds in every cycle.
  for (std::size_t cyc = 0; cyc < cycles; ++cyc)
    EXPECT_TRUE(presence[cyc][2]) << "ground missing in cycle " << cyc;
  // The climax cycle (~80% -> cycle 4) sounds all three voices.
  const std::size_t climax_idx = 4;
  EXPECT_TRUE(presence[climax_idx][0]);
  EXPECT_TRUE(presence[climax_idx][1]) << "climax must sound all three voices";
  EXPECT_TRUE(presence[climax_idx][2]);
  // Exactly one middle cycle (1..climax-1) recedes (rests V1) -- the receding
  // terrace before the climax restores the full texture.
  int receding = 0;
  for (std::size_t cyc = 1; cyc < climax_idx; ++cyc) {
    EXPECT_TRUE(presence[cyc][0]) << "post-intro cycle " << cyc << " must carry V0";
    if (!presence[cyc][1])
      ++receding;
  }
  EXPECT_EQ(receding, 1) << "exactly one middle cycle must rest V1 (receding terrace)";
}

}  // namespace
}  // namespace bach::composer
