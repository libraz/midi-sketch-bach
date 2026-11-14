// Ground-bass variation form-builder tests (chaconne + passacaglia).
//
// These cover the real form builders in src/composer/form_ground_variations.cpp
// (which replace the placeholder phase-fixture replay). Both forms are 3/4
// ground-bass variations: an immutable ground bass (V1) period-tiled under one
// arc-driven scalar-wave variation block per cycle (V0).
//
// For both forms, across seeds {1,5,42,99} x modes {Major,Minor} x
// bars {natural, 2x natural, 128}:
//   1. buildFormFixture -> Composer::run validates Ok (no failures), and the
//      decorated, placed array passes the product's FinalScore gate, for every
//      character.
//   2. Determinism: identical request -> byte-identical notes.
//   3. Ground immutability: the canonical ground events repeat each cycle;
//      Chaconne's declared terminal coda is checked separately.
//   4. Bar math is 1440 ticks (last note end ~= bars * 1440).
//   5. Climax block lands at the arc climax (~80%) and stamps ClimaxPlaced.
//   6. Density rises toward the climax (V0 note count per cycle).
//   7. Minor: no Ab->B (pc 8 -> pc 11) adjacency in any V0 line.
//   8. Picardy: minor + even seed final chord carries a major third.

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <tuple>
#include <vector>

#include "composer/chord_voicing.h"
#include "composer/composer.h"
#include "composer/figuration.h"
#include "composer/form_director.h"
#include "composer/harmonic_plan.h"
#include "composer/harness_fixture.h"
#include "composer/material_pair_guard.h"
#include "composer/minor_material.h"
#include "composer/provenance.h"
#include "composer/rule_helpers.h"
#include "composer/texture_helpers.h"
#include "composer/voice_intent.h"
#include "core/basic_types.h"
#include "core/instrument_program.h"
#include "core/pitch_utils.h"
#include "product_gate_test_util.h"

namespace bach::composer {
namespace {

constexpr Tick kTicksPerBar34 = 3 * kTicksPerBeat;  // 1440.

constexpr RuleIdMask bit(RuleBit b) {
  return ruleBitMask(b);
}

// Every character the ground forms admit; the builder reads it (the figuration
// density tier), so a sweep over one character covers one quarter of the notes.
constexpr std::array<SubjectCharacter, 4> kCharacters = {
    {SubjectCharacter::Severe, SubjectCharacter::Playful, SubjectCharacter::Noble,
     SubjectCharacter::Restless}};

// Build a fixture for a ground-variation form, asserting the director succeeds.
HarnessFixture build(FormType form, std::uint32_t seed, bool is_minor, std::uint16_t target_bars,
                     SubjectCharacter character = SubjectCharacter::Severe) {
  ComposeRequest req;
  req.form = form;
  req.is_minor = is_minor;
  req.character = character;
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

// Both ground forms are laid out V0 principal variation, V1 middle voice,
// V2 ground, so the ground is the same voice in either.
VoiceId groundVoice(FormType) {
  return 2;
}

// Collect the ground note pitches (on `ground_voice`) in onset order.
// Bar-head tones of the ground voice: the skeleton the ground is recognised by,
// independent of how a statement decorates the rest of each bar.
std::vector<std::uint8_t> groundBarHeads(const ComposeResult& r, VoiceId ground_voice) {
  std::vector<std::pair<Tick, std::uint8_t>> heads;
  for (const auto& n : r.notes) {
    if (n.voice == ground_voice && n.start_tick % kTicksPerBar34 == 0)
      heads.push_back({n.start_tick, n.pitch});
  }
  std::sort(heads.begin(), heads.end());
  std::vector<std::uint8_t> out;
  for (const auto& head : heads)
    out.push_back(head.second);
  return out;
}

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

ComposeRequest requestFor(const Case& c, SubjectCharacter character) {
  ComposeRequest req;
  req.form = c.form;
  req.is_minor = c.is_minor;
  req.character = character;
  req.target_bars = c.target_bars;
  req.seed = c.seed;
  return req;
}

// Generation scope plus the gate the product ships behind, for every character.
void expectValidatesOk(FormType form, std::vector<Case> cases) {
  for (const Case& c : cases) {
    for (SubjectCharacter character : kCharacters) {
      const test::ProductGateResult gate = test::runProductGate(requestFor(c, character));
      ASSERT_TRUE(gate.built);
      const ComposeResult& r = gate.composed;
      EXPECT_TRUE(r.validation.failures.empty())
          << "form " << static_cast<int>(form) << " character " << static_cast<int>(character)
          << " seed " << c.seed << " minor " << c.is_minor << " bars " << c.target_bars
          << " first failure="
          << (r.validation.failures.empty() ? "" : r.validation.failures.front().rule_id);
      EXPECT_EQ(r.validation.status, ValidationStatus::Ok);
      EXPECT_TRUE(gate.placed);
      EXPECT_TRUE(gate.failures.empty())
          << "form " << static_cast<int>(form) << " character " << static_cast<int>(character)
          << " seed " << c.seed << " minor " << c.is_minor << " bars " << c.target_bars
          << " shipped failure=" << (gate.failures.empty() ? "" : gate.failures.front().rule_id);
      // Both ground forms realise their harmony in three voices.
      EXPECT_EQ(gate.fixture.voice_plan.num_voices, 3);
      ASSERT_FALSE(r.notes.empty());
    }
  }
}

void expectValidatesOk(FormType form) {
  expectValidatesOk(form, casesFor(form));
}

TEST(GroundVariationChaconne, ValidatesOkAcrossSeedsModesLengths) {
  expectValidatesOk(FormType::Chaconne);
}

// The middle voice's cadential approach tone holds until the final bar, so it
// must stay under every variation tone over that sustain; the descending run
// of these requests used to cross it.
TEST(GroundVariationChaconne, CadentialApproachStaysUnderTheVariationRun) {
  expectValidatesOk(FormType::Chaconne, {{FormType::Chaconne, 3, true, 128}});
}

TEST(GroundVariationPassacaglia, ValidatesOkAcrossSeedsModesLengths) {
  expectValidatesOk(FormType::Passacaglia);
}

TEST(GroundVariationPassacaglia, ClosingSuspensionKeepsItsUpperLineConjunct) {
  const std::array<Case, 4> cases = {{{FormType::Passacaglia, 42, false, 24},
                                      {FormType::Passacaglia, 1, false, 24},
                                      {FormType::Passacaglia, 1, true, 24},
                                      {FormType::Passacaglia, 42, false, 8}}};
  for (const Case& test_case : cases) {
    const HarnessFixture fx =
        build(test_case.form, test_case.seed, test_case.is_minor, test_case.target_bars);
    const ComposeResult result = Composer{}.run(fx.material, fx.harmony, fx.voice_plan);
    ASSERT_EQ(result.validation.status, ValidationStatus::Ok)
        << "seed " << test_case.seed << " minor " << test_case.is_minor << " bars "
        << test_case.target_bars;
    ASSERT_EQ(result.notes.size(), result.provenance.size());

    const Tick suspension_tick =
        static_cast<Tick>(resolvedBars(test_case.form, test_case.target_bars) - 2) * kTicksPerBar34;
    const Tick preparation_tick = suspension_tick - kTicksPerBeat;
    const Tick resolution_tick = suspension_tick + kTicksPerBeat;
    ASSERT_TRUE(
        std::any_of(fx.material.suspension_patterns.begin(), fx.material.suspension_patterns.end(),
                    [suspension_tick](const SuspensionPattern& pattern) {
                      return pattern.voice == 1 && pattern.suspension_tick == suspension_tick &&
                             pattern.preparation_pitch == pattern.suspension_pitch;
                    }))
        << "the closing dissonance must retain a prepared suspension";

    auto material_pitch_at = [&](Tick tick) {
      int pitch = -1;
      Tick latest = 0;
      for (const PassacagliaVariation& variation : fx.material.passacaglia_variations) {
        for (const MaterialNote& note : variation.notes) {
          if (note.start_tick <= tick && tick < note.start_tick + note.duration &&
              (pitch < 0 || note.start_tick >= latest)) {
            latest = note.start_tick;
            pitch = note.pitch;
          }
        }
      }
      return pitch;
    };
    const int upper_preparation = material_pitch_at(preparation_tick);
    const int upper_suspension = material_pitch_at(suspension_tick);
    const int upper_resolution = material_pitch_at(resolution_tick);
    ASSERT_GE(upper_preparation, 0);
    ASSERT_GE(upper_suspension, 0);
    ASSERT_GE(upper_resolution, 0);
    const bool eligible_cadence = !test_case.is_minor && test_case.target_bars == 24;
    if (eligible_cadence) {
      EXPECT_LE(std::abs(upper_suspension - upper_preparation), 7)
          << "the approach to the final suspension should stay within a fifth";
      EXPECT_LE(std::abs(upper_resolution - upper_suspension), 7)
          << "the suspension's resolution should stay within a fifth";
    }

    const Tick ground_period = fx.material.passacaglia_ground_cycle;
    ASSERT_GT(ground_period, 0u);
    const int ground_preparation =
        soundingMaterialPitch(fx.material.passacaglia_ground, preparation_tick % ground_period);
    const int ground_suspension =
        soundingMaterialPitch(fx.material.passacaglia_ground, suspension_tick % ground_period);
    ASSERT_GE(ground_preparation, 0);
    ASSERT_GE(ground_suspension, 0);
    if (eligible_cadence) {
      EXPECT_FALSE(formsAntiParallelPerfect(upper_preparation, upper_suspension, ground_preparation,
                                            ground_suspension))
          << "the final upper approach should not exchange one perfect interval for another";
    }

    auto result_pitch_at = [&](VoiceId voice, Tick tick) {
      int pitch = -1;
      Tick latest = 0;
      for (const NoteEvent& note : result.notes) {
        if (note.voice == voice && note.start_tick <= tick &&
            tick < note.start_tick + note.duration && (pitch < 0 || note.start_tick >= latest)) {
          latest = note.start_tick;
          pitch = note.pitch;
        }
      }
      return pitch;
    };
    EXPECT_EQ(result_pitch_at(groundVoice(test_case.form), preparation_tick), ground_preparation);
    EXPECT_EQ(result_pitch_at(groundVoice(test_case.form), suspension_tick), ground_suspension)
        << "the closing voicing must preserve the ground at the cadence";
  }
}

TEST(GroundVariationPassacaglia, MinimumLengthPreservesTheRunningSixteenthFigure) {
  const HarnessFixture fx = build(FormType::Passacaglia, 42, false, 8);
  const Tick figure_start = 41 * kTicksPerBeat;
  constexpr std::array<std::uint8_t, 8> kFigure = {67, 69, 71, 72, 77, 76, 77, 79};
  constexpr Tick kSixteenth = kTicksPerBeat / 4;
  std::array<std::uint8_t, kFigure.size()> found{};
  std::size_t count = 0;
  for (const PassacagliaVariation& variation : fx.material.passacaglia_variations) {
    for (const MaterialNote& note : variation.notes) {
      if (figure_start <= note.start_tick && note.start_tick < figure_start + kTicksPerBeat * 2) {
        ASSERT_LT(count, found.size());
        EXPECT_EQ(note.start_tick, figure_start + static_cast<Tick>(count) * kSixteenth);
        found[count++] = note.pitch;
      }
    }
  }
  ASSERT_EQ(count, found.size());
  EXPECT_EQ(found, kFigure);
}

TEST(GroundVariationPassacaglia, UnavailableSuspensionDoesNotRewriteTheUpperVoice) {
  const HarnessFixture fx = build(FormType::Passacaglia, 42, false, 8);
  const Tick suspension_tick = 6 * kTicksPerBar34;
  ASSERT_FALSE(std::any_of(
      fx.material.suspension_patterns.begin(), fx.material.suspension_patterns.end(),
      [](const SuspensionPattern& pattern) { return pattern.suspension_tick == suspension_tick; }));
  // The middle voice rests through the first cycle, so its unavailable
  // suspension must leave the variation's original bar-head tone intact.
  for (const PassacagliaVariation& variation : fx.material.passacaglia_variations) {
    if (variation.start_tick <= suspension_tick && suspension_tick < variation.end_tick) {
      EXPECT_EQ(soundingMaterialPitch(variation.notes, suspension_tick), 71);
      return;
    }
  }
  FAIL() << "missing upper variation at the interior cadence";
}

// --- Registration terraces (fixture metadata only, never a note) ------------

TEST(GroundVariationPassacaglia, LongFormDeclaresRegistrationTerraces) {
  // A long passacaglia (128 bars = 16 cycles) terraces the organ on each voice
  // addition plus the intermediate-swell cycle: at least two structural steps.
  const HarnessFixture fx = build(FormType::Passacaglia, 3, false, 128);
  EXPECT_GE(fx.registration_step_ticks.size(), 2u);
  const Tick period = 8 * kTicksPerBar34;  // passacaglia ground period.
  for (const Tick step : fx.registration_step_ticks) {
    EXPECT_GT(step, 0u);
    EXPECT_EQ(step % period, 0u) << "a terrace must fall on a ground-cycle boundary";
  }
  // The metadata does not change the composed note stream (validation covered
  // by ValidatesOkAcrossSeedsModesLengths, which spans the 128-bar case).
}

TEST(GroundVariationChaconne, LongFormDeclaresOneMidWaveTerrace) {
  // A long chaconne (32 bars = 8 cycles) terraces only at the intermediate-swell
  // cycle; its climax is already the macro arc's peak.
  const HarnessFixture fx = build(FormType::Chaconne, 3, false, 32);
  ASSERT_EQ(fx.registration_step_ticks.size(), 1u);
  const Tick period = 4 * kTicksPerBar34;  // chaconne ground period.
  EXPECT_EQ(fx.registration_step_ticks.front() % period, 0u);
}

TEST(GroundVariationChaconne, ShortFormDeclaresNoTerrace) {
  // A short chaconne (16 bars = 4 cycles, below the second-wave threshold) has
  // no intermediate swell, so no terrace step.
  const HarnessFixture fx = build(FormType::Chaconne, 3, false, 16);
  EXPECT_TRUE(fx.registration_step_ticks.empty());
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
    // Check the bar-head pitch skeleton here. Chaconne's exact event stream up
    // to its declared coda is checked by GroundEventsRepeatExactlyUntilDeclaredCoda;
    // the final two bars are handled by the coda assertions below.
    const VoiceId gv = groundVoice(c.form);
    std::vector<int> head(static_cast<std::size_t>(resolvedBars(c.form, c.target_bars)), -1);
    for (const auto& n : r.notes) {
      if (n.voice != gv)
        continue;
      const std::size_t bar = static_cast<std::size_t>(n.start_tick / kTicksPerBar34);
      ASSERT_LT(bar, head.size());
      if (n.start_tick % kTicksPerBar34 == 0)
        head[bar] = n.pitch;
      else if (!(c.form == FormType::Chaconne && bar + 2 >= head.size()))
        EXPECT_LE(std::abs(static_cast<int>(n.pitch) - head[bar]), 2)
            << "ground note inside bar " << bar << " is neither its bar-head pitch nor a"
            << " neighbour of it";
    }
    const std::size_t period = static_cast<std::size_t>(cycle_bars);
    for (std::size_t bar = 0; bar < head.size(); ++bar) {
      ASSERT_NE(head[bar], -1) << "bar " << bar << " has no ground bar-head note";
      if (c.form == FormType::Chaconne && bar + 2 >= head.size()) {
        ASSERT_EQ(fx.material.coda_extensions.size(), 1u);
        ASSERT_EQ(fx.material.coda_extensions.front().notes.size(), 3u);
        if (bar + 2 == head.size())
          EXPECT_EQ(head[bar], fx.material.coda_extensions.front().notes.front().pitch);
        else
          EXPECT_EQ(head[bar], fx.material.coda_extensions.front().notes.back().pitch);
      } else {
        EXPECT_EQ(head[bar], head[bar % period])
            << "ground bar " << bar << " differs from cycle-0 bar " << (bar % period);
      }
    }
  }
}

TEST(GroundVariationChaconne, GroundRepeatsEveryPeriod) {
  expectGroundImmutable(FormType::Chaconne, 4);
}

TEST(GroundVariationPassacaglia, GroundRepeatsEveryPeriod) {
  expectGroundImmutable(FormType::Passacaglia, 8);
}

using GroundEvent = std::tuple<Tick, Tick, std::uint8_t>;

std::vector<GroundEvent> materialGroundCycle(const std::vector<MaterialNote>& notes,
                                             Tick cycle_start, Tick cycle_ticks) {
  std::vector<GroundEvent> events;
  const Tick cycle_end = cycle_start + cycle_ticks;
  for (const MaterialNote& note : notes) {
    if (cycle_start <= note.start_tick && note.start_tick < cycle_end)
      events.emplace_back(note.start_tick - cycle_start, note.duration, note.pitch);
  }
  std::sort(events.begin(), events.end());
  return events;
}

TEST(GroundVariationPassacaglia, GroundEventsRepeatExactlyAcrossCycles) {
  constexpr std::array<std::uint32_t, 4> kSeeds = {1u, 2u, 5u, 42u};
  constexpr std::array<std::uint16_t, 3> kTargetBars = {8u, 24u, 72u};
  const Tick cycle_ticks = 8 * kTicksPerBar34;
  for (const std::uint32_t seed : kSeeds) {
    for (const bool minor : {false, true}) {
      const std::size_t variant = seed % detail::kGroundVariantCount;
      const auto& pitches = minor ? detail::kPassacagliaGroundsMinor[variant]
                                  : detail::kPassacagliaGroundsMajor[variant];
      std::vector<GroundEvent> expected;
      for (std::size_t bar = 0; bar < pitches.size(); ++bar)
        expected.emplace_back(static_cast<Tick>(bar) * kTicksPerBar34, kTicksPerBar34,
                              pitches[bar]);

      for (const std::uint16_t target_bars : kTargetBars) {
        const HarnessFixture fx = build(FormType::Passacaglia, seed, minor, target_bars);
        const ComposeResult result = Composer{}.run(fx.material, fx.harmony, fx.voice_plan);
        ASSERT_EQ(result.validation.status, ValidationStatus::Ok)
            << "seed " << seed << " minor " << minor << " bars " << target_bars;
        ASSERT_EQ(result.notes.size(), result.provenance.size());
        const std::size_t total_bars = resolvedBars(FormType::Passacaglia, target_bars);
        ASSERT_EQ(total_bars % 8u, 0u);
        const Tick total_ticks = static_cast<Tick>(total_bars) * kTicksPerBar34;
        EXPECT_EQ(fx.material.passacaglia_ground_period, total_ticks);
        EXPECT_EQ(fx.material.passacaglia_ground_cycle, cycle_ticks);
        EXPECT_EQ(fx.material.passacaglia_ground_split_from, 0u);
        EXPECT_EQ(fx.material.passacaglia_ground.size(), total_bars)
            << "material must contain exactly one event per bar";
        EXPECT_EQ(std::count_if(result.notes.begin(), result.notes.end(),
                                [](const NoteEvent& note) { return note.voice == 2; }),
                  total_bars)
            << "the emitted ground must contain exactly one event per bar";

        for (std::size_t cycle = 0; cycle < total_bars / 8; ++cycle) {
          const Tick cycle_start = static_cast<Tick>(cycle) * cycle_ticks;
          const std::vector<GroundEvent> material_events =
              materialGroundCycle(fx.material.passacaglia_ground, cycle_start, cycle_ticks);
          EXPECT_EQ(material_events, expected)
              << "material seed " << seed << " minor " << minor << " cycle " << cycle;

          std::vector<GroundEvent> emitted_events;
          for (std::size_t index = 0; index < result.notes.size(); ++index) {
            const NoteEvent& note = result.notes[index];
            if (note.voice != 2 || note.start_tick < cycle_start ||
                note.start_tick >= cycle_start + cycle_ticks)
              continue;
            emitted_events.emplace_back(note.start_tick - cycle_start, note.duration, note.pitch);
            EXPECT_EQ(result.provenance[index].source, NoteSource::Material);
            EXPECT_EQ(result.provenance[index].voice_intent, VoiceIntent::PassacagliaGround);
            EXPECT_EQ(result.provenance[index].span_id, 0u);
            EXPECT_TRUE((result.provenance[index].satisfied_rules &
                         ruleBitMask(RuleBit::PassacagliaGroundReplayed))
                            .any());
          }
          std::sort(emitted_events.begin(), emitted_events.end());
          EXPECT_EQ(emitted_events, expected)
              << "emitted seed " << seed << " minor " << minor << " cycle " << cycle;
        }
      }
    }
  }
}

TEST(GroundVariationChaconne, GroundEventsRepeatExactlyUntilDeclaredCoda) {
  constexpr std::array<std::uint32_t, 4> kSeeds = {1u, 2u, 5u, 42u};
  constexpr std::array<std::uint16_t, 5> kTargetBars = {8u, 16u, 48u, 64u, 128u};
  constexpr Tick kCycleTicks = 4 * kTicksPerBar34;

  for (const std::uint32_t seed : kSeeds) {
    const std::size_t variant = detail::groundVariantIndex(seed);
    for (const bool minor : {false, true}) {
      const auto& pitches =
          minor ? detail::kChaconneGroundsMinor[variant] : detail::kChaconneGroundsMajor[variant];
      for (const std::uint16_t target_bars : kTargetBars) {
        const HarnessFixture fx = build(FormType::Chaconne, seed, minor, target_bars);
        const ComposeResult result = Composer{}.run(fx.material, fx.harmony, fx.voice_plan);
        ASSERT_EQ(result.validation.status, ValidationStatus::Ok)
            << "seed " << seed << " minor " << minor << " bars " << target_bars;
        ASSERT_EQ(result.notes.size(), result.provenance.size());

        const std::size_t total_bars = resolvedBars(FormType::Chaconne, target_bars);
        const Tick total_ticks = static_cast<Tick>(total_bars) * kTicksPerBar34;
        EXPECT_EQ(fx.material.ground_bass_period, total_ticks);
        EXPECT_EQ(fx.material.ground_bass_cycle, kCycleTicks);
        ASSERT_EQ(fx.material.coda_extensions.size(), 1u);
        const CodaDecl& coda = fx.material.coda_extensions.front();
        ASSERT_EQ(coda.voice, 2u);
        ASSERT_FALSE(coda.notes.empty());
        Tick coda_start = coda.notes.front().start_tick;
        for (const MaterialNote& note : coda.notes)
          coda_start = std::min(coda_start, note.start_tick);
        EXPECT_EQ(coda_start, total_ticks - 2 * kTicksPerBar34)
            << "the declared coda occupies the final two bars";
        ASSERT_EQ((total_bars - 2) % 4u, 2u)
            << "the ground-bearing portion ends partway through its final cycle";

        const Span* ground_span = nullptr;
        const Span* coda_span = nullptr;
        for (const Span& span : fx.voice_plan.spans) {
          if (span.voice != 2)
            continue;
          if (span.intent == VoiceIntent::GroundCarrier)
            ground_span = &span;
          else if (span.intent == VoiceIntent::CodaCarrier)
            coda_span = &span;
        }
        ASSERT_NE(ground_span, nullptr);
        ASSERT_NE(coda_span, nullptr);
        EXPECT_EQ(ground_span->start_tick, 0u);
        EXPECT_EQ(ground_span->end_tick, coda_start);
        EXPECT_EQ(coda_span->start_tick, coda_start);
        EXPECT_EQ(coda_span->end_tick, total_ticks);

        std::vector<GroundEvent> expected;
        for (std::size_t bar = 0; bar < total_bars; ++bar) {
          const Tick onset = static_cast<Tick>(bar) * kTicksPerBar34;
          if (onset >= coda_start)
            continue;
          expected.emplace_back(onset, kTicksPerBar34, pitches[bar % pitches.size()]);
        }

        std::vector<GroundEvent> material_events;
        for (const MaterialNote& note : fx.material.ground_bass) {
          if (note.start_tick < coda_start)
            material_events.emplace_back(note.start_tick, note.duration, note.pitch);
        }
        std::sort(material_events.begin(), material_events.end());
        EXPECT_EQ(material_events, expected)
            << "material seed " << seed << " minor " << minor << " bars " << target_bars;

        std::vector<GroundEvent> emitted_events;
        for (std::size_t index = 0; index < result.notes.size(); ++index) {
          const NoteEvent& note = result.notes[index];
          if (note.voice != 2 || note.start_tick >= coda_start)
            continue;
          emitted_events.emplace_back(note.start_tick, note.duration, note.pitch);
          EXPECT_EQ(result.provenance[index].source, NoteSource::Material);
          EXPECT_EQ(result.provenance[index].voice_intent, VoiceIntent::GroundCarrier);
          EXPECT_EQ(result.provenance[index].span_id, ground_span->id);
          EXPECT_TRUE(
              (result.provenance[index].satisfied_rules & ruleBitMask(RuleBit::GroundBassReplayed))
                  .any());
        }
        std::sort(emitted_events.begin(), emitted_events.end());
        EXPECT_EQ(emitted_events, expected)
            << "emitted seed " << seed << " minor " << minor << " bars " << target_bars;
      }
    }
  }
}

TEST(GroundVariationChaconne, TerminalCadenceResolvesGroundToTonic) {
  for (const Case& c : casesFor(FormType::Chaconne)) {
    const HarnessFixture fx = build(c.form, c.seed, c.is_minor, c.target_bars);
    const ComposeResult r = Composer{}.run(fx.material, fx.harmony, fx.voice_plan);
    const Tick final_bar =
        static_cast<Tick>(resolvedBars(c.form, c.target_bars) - 1) * kTicksPerBar34;
    ASSERT_EQ(fx.material.coda_extensions.size(), 1u);
    ASSERT_EQ(fx.material.coda_extensions.front().notes.size(), 3u);
    EXPECT_EQ(fx.material.coda_extensions.front().notes[1].pitch % 12u, 7u);
    EXPECT_EQ(fx.material.coda_extensions.front().notes.back().pitch % 12u, 0u);
    ASSERT_FALSE(fx.harmony.chords.empty());
    EXPECT_EQ(fx.harmony.chords.back().root_pc, 0u);

    const NoteEvent* bass = nullptr;
    const NoteEvent* upper = nullptr;
    for (const auto& note : r.notes) {
      if (note.voice == 2 && note.start_tick == final_bar)
        bass = &note;
      if (note.voice == 0 && note.start_tick <= final_bar + kTicksPerBar34 / 2 &&
          note.start_tick + note.duration > final_bar + kTicksPerBar34 / 2)
        upper = &note;
    }
    ASSERT_NE(bass, nullptr);
    ASSERT_NE(upper, nullptr);
    EXPECT_EQ(bass->pitch % 12u, 0u);
    bool coda_committed = false;
    for (std::size_t idx = 0; idx < r.notes.size() && idx < r.provenance.size(); ++idx) {
      if (r.notes[idx].voice == 2 && r.notes[idx].start_tick == final_bar)
        coda_committed =
            (r.provenance[idx].satisfied_rules & ruleBitMask(RuleBit::CodaCommitted)).any();
    }
    EXPECT_TRUE(coda_committed);
    const int interval =
        std::abs(static_cast<int>(upper->pitch) - static_cast<int>(bass->pitch)) % 12;
    EXPECT_TRUE(interval == 0 || interval == 7) << "seed " << c.seed << " minor " << c.is_minor;
  }
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
    EXPECT_EQ(groundBarHeads(r_a, groundVoice(form)), groundBarHeads(r_b, groundVoice(form)))
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
    const std::size_t climax_idx = cycle_count <= 1 ? 0 : ((cycle_count - 1) * 4) / 5;
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
    const std::size_t climax_idx = cycle_count <= 1 ? 0 : ((cycle_count - 1) * 4) / 5;

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
//
// V0 keeps the minor dominant's leading tone, so a bar line between the lowered
// 6th's chord and V can join A-flat and B. The anchor chain avoids it unless
// every other anchor on both sides lands on a perfect interval with the moving
// ground, where the parallel outranks the augmented second; the ceiling counts
// those bar lines and may only be lowered.

void expectNoAugmentedSecond(FormType form, std::size_t ceiling) {
  std::size_t count = 0;
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
      if ((prev_pc == 8 && cur_pc == 11) || (prev_pc == 11 && cur_pc == 8)) {
        ++count;
        std::printf("[augmented-second] form %d seed %u bars %u tick %u (%d -> %d)\n",
                    static_cast<int>(form), c.seed, static_cast<unsigned>(c.target_bars),
                    static_cast<unsigned>(line[i].first), static_cast<int>(line[i - 1].second),
                    static_cast<int>(line[i].second));
      }
    }
  }
  EXPECT_LE(count, ceiling) << "form " << static_cast<int>(form)
                            << ": Ab/B augmented seconds in V0 rose above the ceiling";
}

TEST(GroundVariationChaconne, MinorHasNoAugmentedSecond) {
  expectNoAugmentedSecond(FormType::Chaconne, 0);
}

TEST(GroundVariationPassacaglia, MinorHasNoAugmentedSecond) {
  expectNoAugmentedSecond(FormType::Passacaglia, 1);
}

// --- 8. No downward leap onto a perfect interval over the ground ------------

// The chaconne is two voices over an immutable ground, so a perfect interval
// arriving there has exactly one line that may move. Over a ground that rises
// while the variation falls the two almost never travel together, which is why
// the same-direction test alone leaves this form's characteristic fault behind:
// the variation leaping DOWN onto an octave or a fifth above a rising bass.
// That is the ottava battuta, and in a two-voice texture it is bare.
//
// Asserted for the chaconne only. The passacaglia has a third voice its ground
// scrub cannot see, so a repair there is unverified against the counter line
// and its residue is held by the shipped-counterpoint ratchet instead.
// Every (variation, ground) motion the chaconne ships, read the way the audit
// reads it: at each union onset, whatever sounds in the two voices.
void expectNoChaconneMotion(bool (*forbidden)(int, int, int, int), const char* label,
                            const std::vector<Case>& cases = casesFor(FormType::Chaconne)) {
  for (const Case& c : cases) {
    const HarnessFixture fx = build(c.form, c.seed, c.is_minor, c.target_bars);
    const ComposeResult r = Composer{}.run(fx.material, fx.harmony, fx.voice_plan);
    auto sounding = [&](VoiceId voice, Tick at) -> int {
      int pitch = -1;
      Tick best = 0;
      bool found = false;
      for (const auto& note : r.notes) {
        if (note.voice != voice)
          continue;
        if (note.start_tick <= at && at < note.start_tick + note.duration &&
            (!found || note.start_tick > best)) {
          found = true;
          best = note.start_tick;
          pitch = note.pitch;
        }
      }
      return pitch;
    };
    std::vector<Tick> onsets;
    for (const auto& note : r.notes)
      onsets.push_back(note.start_tick);
    std::sort(onsets.begin(), onsets.end());
    onsets.erase(std::unique(onsets.begin(), onsets.end()), onsets.end());
    const VoiceId ground = groundVoice(FormType::Chaconne);
    for (std::size_t idx = 1; idx < onsets.size(); ++idx) {
      const int up_prev = sounding(0, onsets[idx - 1]);
      const int up_curr = sounding(0, onsets[idx]);
      const int lo_prev = sounding(ground, onsets[idx - 1]);
      const int lo_curr = sounding(ground, onsets[idx]);
      if (up_prev < 0 || up_curr < 0 || lo_prev < 0 || lo_curr < 0)
        continue;
      EXPECT_FALSE(forbidden(up_prev, up_curr, lo_prev, lo_curr))
          << "seed " << c.seed << " minor " << c.is_minor << " bars " << c.target_bars << " "
          << label << " at tick " << onsets[idx] << " (" << up_prev << "->" << up_curr << " over "
          << lo_prev << "->" << lo_curr << ")";
    }
  }
}

TEST(GroundVariationChaconne, NoBattutaOverTheGround) {
  expectNoChaconneMotion(isBattutaMotion, "battuta");
}

// The cadence is where the last of these lived: the coda states the tonic under
// a landing that spells the dominant, so the arrival is a fifth however either
// is spelt, and the two tones the variation still owns on the way in are what
// carry the whole repair.
TEST(GroundVariationChaconne, NoTrueParallelOverTheGround) {
  expectNoChaconneMotion(isParallelPerfectMotion, "true parallel perfect");
}

// A ground decoration on a statement's last bar moves the bass under the next
// statement's first onset, so the diminution scrub reads each upper voice as
// one continuous line across the statement boundary.
TEST(GroundVariationChaconne, NoContraryPerfectArrivalOverTheGround) {
  // 64 bars, seed 5: the statement seam where a lower-neighbour decoration
  // once met the next statement's first onset in contrary octaves.
  std::vector<Case> cases = casesFor(FormType::Chaconne);
  cases.push_back({FormType::Chaconne, 5, false, 64});
  cases.push_back({FormType::Chaconne, 5, true, 64});
  expectNoChaconneMotion(formsAntiParallelPerfect, "contrary-motion perfect arrival", cases);
}

// --- 9. Picardy: minor + even seed final chord major ------------------------

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

// --- 9. Beat-onset vertical consonance --------------------------------------
//
// Every quarter-beat onset's sounding pitch set must be mutually consonant: no
// m2/M2/tritone/m7/M7 interval class between any sounding pair. Passing tones
// BETWEEN beat onsets may be dissonant (normal figuration); the sampled beat
// grid is what the ear locks onto over the held ground, and an unanchored
// variation line once put non-chord tones on the beats of entire late cycles
// (audible as sustained dissonance from the climax onward).

// Chord-tone membership as the Validator spells it, seventh included.
bool isChordTone(const HarmonicPlan& plan, Tick tick, int pitch) {
  const ChordEvent* active = nullptr;
  for (const ChordEvent& chord : plan.chords) {
    if (chord.start_tick > tick)
      break;
    active = &chord;
  }
  if (active == nullptr)
    return false;
  std::size_t count = 0;
  const auto tones = chordPitchClasses(*active, &count);
  const std::uint8_t pitch_class = static_cast<std::uint8_t>(((pitch % 12) + 12) % 12);
  for (std::size_t tone = 0; tone < count; ++tone) {
    if (tones[tone] == pitch_class)
      return true;
  }
  return false;
}

void expectBeatOnsetsConsonant(FormType form) {
  for (const Case& c : casesFor(form)) {
    const HarnessFixture fx = build(c.form, c.seed, c.is_minor, c.target_bars);
    const ComposeResult r = Composer{}.run(fx.material, fx.harmony, fx.voice_plan);
    Tick end = 0;
    for (const auto& n : r.notes)
      end = std::max(end, n.start_tick + n.duration);
    for (Tick t = 0; t < end; t += kTicksPerBeat) {
      const bool declared_suspension = std::any_of(
          fx.material.suspension_patterns.begin(), fx.material.suspension_patterns.end(),
          [&](const SuspensionPattern& pattern) { return pattern.suspension_tick == t; });
      if (declared_suspension)
        continue;  // the prepared accented dissonance is verified by Validator.
      std::vector<int> sounding;
      for (const auto& n : r.notes) {
        if (n.start_tick <= t && t < n.start_tick + n.duration)
          sounding.push_back(static_cast<int>(n.pitch));
      }
      for (std::size_t i = 0; i < sounding.size(); ++i) {
        for (std::size_t j = i + 1; j < sounding.size(); ++j) {
          // A pair whose BOTH tones belong to the chord in force is not a clash
          // within it: over a dominant seventh the third and the seventh sound a
          // tritone, and the seventh a minor seventh against the ground that is
          // the chord's own bass, for as long as the harmony lasts. Reading only
          // the interval here, while the selector and the Validator both read
          // the chord, would leave this form unable to state the one accented
          // dissonance its harmony is built on. Anything else stays absolute:
          // this is a bar-rate ground form, and outside its own harmony a
          // dissonant beat onset has no preparation to be judged by.
          if (isChordTone(fx.harmony, t, sounding[i]) && isChordTone(fx.harmony, t, sounding[j])) {
            continue;
          }
          EXPECT_TRUE(isConsonantIc(sounding[i] - sounding[j]))
              << "form " << static_cast<int>(form) << " seed " << c.seed << " minor " << c.is_minor
              << " bars " << c.target_bars << " tick " << t << " pitches " << sounding[i] << "/"
              << sounding[j];
        }
      }
    }
  }
}

TEST(GroundVariationChaconne, BeatOnsetsAreVerticallyConsonant) {
  expectBeatOnsetsConsonant(FormType::Chaconne);
}

TEST(GroundVariationPassacaglia, BeatOnsetsAreVerticallyConsonant) {
  expectBeatOnsetsConsonant(FormType::Passacaglia);
}

// The ground is immutable, so a parallel perfect at adjacent downbeats must be
// repaired in V0.  Deliberately sample the two bar-head events rather than the
// immediately preceding ornament: figuration may end a bar on a passing tone,
// but it is the downbeat-to-downbeat move that exposes the structural parallel.
void expectNoGroundBarHeadParallels(FormType form) {
  for (const Case& c : casesFor(form)) {
    const HarnessFixture fx = build(c.form, c.seed, c.is_minor, c.target_bars);
    const ComposeResult r = Composer{}.run(fx.material, fx.harmony, fx.voice_plan);
    const std::uint16_t bars = resolvedBars(c.form, c.target_bars);
    const VoiceId ground_voice = groundVoice(c.form);
    auto pitch_at = [&](VoiceId voice, Tick tick) {
      for (const NoteEvent& note : r.notes) {
        if (note.voice == voice && note.start_tick == tick)
          return static_cast<int>(note.pitch);
      }
      return -1;
    };
    // The final bar is replaced by the form's explicit compact cadence, whose
    // tonic resolution is covered independently by TerminalCadenceResolvesGroundToTonic.
    for (std::uint16_t bar = 1; bar + 1 < bars; ++bar) {
      const Tick prev_tick = static_cast<Tick>(bar - 1) * kTicksPerBar34;
      const Tick tick = static_cast<Tick>(bar) * kTicksPerBar34;
      const int previous_v0 = pitch_at(/*V0=*/0, prev_tick);
      const int v0 = pitch_at(/*V0=*/0, tick);
      // The passacaglia's ground-solo introduction has no V0 to compare.
      if (previous_v0 < 0 || v0 < 0)
        continue;
      const int previous_ground = pitch_at(ground_voice, prev_tick);
      const int ground = pitch_at(ground_voice, tick);
      ASSERT_GE(previous_ground, 0);
      ASSERT_GE(ground, 0);
      EXPECT_FALSE(formsPerfectParallel(previous_v0, v0, previous_ground, ground))
          << "form " << static_cast<int>(form) << " seed " << c.seed << " minor " << c.is_minor
          << " bars " << c.target_bars << " bar " << bar;
    }
  }
}

// The passacaglia's counter-figuration alternates a beat anchor with a
// companion tone, and every one of those tones moves while V0's figuration is
// also moving, so each needs judging at the grain the ear samples. The beat
// head is judged where it is chosen; what this pins is everything AFTER it in
// the beat -- including the anchor's own return in the second half, which is
// approached from the companion tone and is therefore a different motion from
// the one the head was judged on. Leaving that return unjudged shipped the
// largest single group of parallel octaves this form produced.
//
// True parallels only: the anchor is often pinned to the ground's octave
// companion (the chord root tracks the ground's pitch class every bar and the
// counter-figuration's band is one octave wide), and where every reachable tone
// is at least a hidden perfect the line is allowed to take the hidden one.
TEST(GroundVariationPassacaglia, CounterFigurationOffBeatsAreJudgedAgainstMovingVoices) {
  std::size_t judged = 0;
  std::size_t parallel = 0;
  for (const Case& c : casesFor(FormType::Passacaglia)) {
    const HarnessFixture fx = build(c.form, c.seed, c.is_minor, c.target_bars);
    const ComposeResult r = Composer{}.run(fx.material, fx.harmony, fx.voice_plan);
    std::vector<NoteEvent> counter;
    for (const NoteEvent& note : r.notes) {
      if (note.voice == 1)
        counter.push_back(note);
    }
    std::stable_sort(
        counter.begin(), counter.end(),
        [](const NoteEvent& lhs, const NoteEvent& rhs) { return lhs.start_tick < rhs.start_tick; });
    // Latest-starting note covering the tick wins, matching how the builder
    // reads the registry it judges against.
    const auto sounding = [&](VoiceId voice, Tick tick) {
      int pitch = -1;
      for (const NoteEvent& note : r.notes) {
        if (note.voice != voice || note.start_tick > tick)
          continue;
        if (tick < note.start_tick + note.duration)
          pitch = note.pitch;
      }
      return pitch;
    };
    for (std::size_t idx = 1; idx < counter.size(); ++idx) {
      const NoteEvent& note = counter[idx];
      if (note.start_tick % kTicksPerBeat == 0)
        continue;  // beat heads are judged where the anchor is chosen.
      const int line_prev = counter[idx - 1].pitch;
      ++judged;
      for (VoiceId other : {VoiceId{0}, VoiceId{2}}) {
        const int other_prev = sounding(other, note.start_tick - 1);
        const int other_curr = sounding(other, note.start_tick);
        if (other_prev < 0 || other_curr < 0)
          continue;
        if (formsStrictPerfectParallel(line_prev, note.pitch, other_prev, other_curr)) {
          ++parallel;
          break;
        }
      }
    }
  }
  ASSERT_GT(judged, 0u) << "no off-beat counter-figuration tone was reached";
  // The companion vocabulary is deliberately narrow -- a stepwise neighbour of
  // the anchor or the nearest other triad tone, each of which must also stay
  // consonant against the held ground -- because widening it was tried and
  // rejected: a free diatonic oscillation hammers a sustained seventh against
  // the bar-long ground. The bind where all of them clash is instead answered by
  // holding the tone already sounding, which is oblique and so can form no
  // parallel at all. Like the shipped ratchet, this ceiling may only ever be
  // LOWERED.
  EXPECT_LE(parallel, 0u) << parallel << " of " << judged
                          << " off-beat counter-figuration tones move in a true parallel perfect "
                             "with a concurrently moving voice";
}

// The closing suspension resolves onto a tone of the declared chord it resolves
// under (the dominant), and neither it nor its dissonance spells the leading
// tone against the minor third with V0 -- in both modes, every character and
// every length the director resolves.
TEST(GroundVariationPassacaglia, ClosingSuspensionResolvesToADominantChordTone) {
  for (bool minor : {false, true}) {
    for (SubjectCharacter character : kCharacters) {
      for (std::uint16_t bars : {24, 54, 72, 96, 128}) {
        for (std::uint32_t seed = 1; seed <= 4; ++seed) {
          const HarnessFixture fx = build(FormType::Passacaglia, seed, minor, bars, character);
          const ComposeResult r = Composer{}.run(fx.material, fx.harmony, fx.voice_plan);
          Tick end = 0;
          for (const NoteEvent& note : r.notes)
            end = std::max(end, note.start_tick + note.duration);
          const Tick resolution_tick = end - 2 * kTicksPerBar34 + kTicksPerBeat;
          const detail::PcMask chord = rule_helpers::chordPcsAt(fx.harmony, resolution_tick);
          for (std::size_t idx = 0; idx < r.notes.size(); ++idx) {
            const NoteEvent& note = r.notes[idx];
            if (r.provenance[idx].voice_intent != VoiceIntent::SuspensionCarrier ||
                note.start_tick != resolution_tick)
              continue;
            EXPECT_NE(chord & (1u << (note.pitch % 12)), 0u)
                << "minor " << minor << " character " << static_cast<int>(character) << " bars "
                << bars << " seed " << seed << " resolution " << static_cast<int>(note.pitch);
            for (const NoteEvent& other : r.notes) {
              if (other.voice != 0 || other.start_tick >= resolution_tick + kTicksPerBeat ||
                  other.start_tick + other.duration <= resolution_tick)
                continue;
              const int pcs[2] = {note.pitch % 12, other.pitch % 12};
              EXPECT_FALSE((pcs[0] == interval::kMajor7th && pcs[1] == interval::kMinor3rd) ||
                           (pcs[1] == interval::kMajor7th && pcs[0] == interval::kMinor3rd))
                  << "minor " << minor << " character " << static_cast<int>(character) << " bars "
                  << bars << " seed " << seed << " tick " << other.start_tick;
            }
          }
        }
      }
    }
  }
}

// The octave gate is closed for this form, so the shipped surface must carry no
// true parallel octave anywhere -- not only at the bar heads the test above
// samples. Walk every voice pair over the union of onsets, which is the reading
// the counterpoint budget and the ear both use: the last tone that sounded
// paired with the next one that starts. Fifths are deliberately not asserted
// here; a handful still reach bar heads the variation itself owns, and the
// shipped ratchet is what tracks them.
TEST(GroundVariationPassacaglia, ShippedTextureIsFreeOfParallelOctaves) {
  for (const Case& c : casesFor(FormType::Passacaglia)) {
    const test::ProductGateResult gate =
        test::runProductGate(requestFor(c, SubjectCharacter::Severe));
    ASSERT_TRUE(gate.built);
    const HarnessFixture& fx = gate.fixture;
    const ComposeResult& r = gate.shipped;
    std::vector<Tick> onsets;
    for (const NoteEvent& note : r.notes)
      onsets.push_back(note.start_tick);
    std::sort(onsets.begin(), onsets.end());
    onsets.erase(std::unique(onsets.begin(), onsets.end()), onsets.end());
    const auto sounding = [&](VoiceId voice, Tick tick) {
      int pitch = -1;
      for (const NoteEvent& note : r.notes) {
        if (note.voice != voice || note.start_tick > tick)
          continue;
        if (tick < note.start_tick + note.duration)
          pitch = note.pitch;
      }
      return pitch;
    };
    for (VoiceId upper = 0; upper + 1 < fx.voice_plan.num_voices; ++upper) {
      for (VoiceId lower = upper + 1; lower < fx.voice_plan.num_voices; ++lower) {
        int upper_prev = -1;
        int lower_prev = -1;
        for (Tick tick : onsets) {
          const int upper_now = sounding(upper, tick);
          const int lower_now = sounding(lower, tick);
          if (upper_now < 0 || lower_now < 0) {
            upper_prev = -1;
            lower_prev = -1;
            continue;
          }
          if (upper_prev >= 0 && lower_prev >= 0 && std::abs(upper_now - lower_now) % 12 == 0) {
            EXPECT_FALSE(isStrictParallelPerfect(upper_prev, upper_now, lower_prev, lower_now))
                << "seed " << c.seed << " minor " << c.is_minor << " bars " << c.target_bars
                << " tick " << tick << " v" << static_cast<int>(upper) << "/v"
                << static_cast<int>(lower);
          }
          upper_prev = upper_now;
          lower_prev = lower_now;
        }
      }
    }
  }
}

TEST(GroundVariationChaconne, HasNoGroundBarHeadParallelPerfects) {
  expectNoGroundBarHeadParallels(FormType::Chaconne);
}

TEST(GroundVariationPassacaglia, HasNoGroundBarHeadParallelPerfects) {
  expectNoGroundBarHeadParallels(FormType::Passacaglia);
}

// A chaconne is a chordal variation form: the ground carries a progression that
// wants realising, and two voices cannot state it. Every seed, mode and length
// therefore ships three voices -- the variation on V0, the middle voice that
// realises the harmony on V1, the ground on V2 -- and the middle voice sounds
// for a substantial part of the piece rather than a token entry.
//
// The share is bounded rather than pinned exactly: the middle voice withdraws
// from a statement whose register offers it no fault-free placement, so how much
// it sounds is a property of the statement, not a constant.
TEST(GroundVariationChaconne, ShipsThreeVoicesWithASoundingMiddleVoice) {
  for (const Case& c : casesFor(FormType::Chaconne)) {
    const HarnessFixture fx = build(c.form, c.seed, c.is_minor, c.target_bars);
    EXPECT_EQ(fx.voice_plan.num_voices, 3);
    const ComposeResult r = Composer{}.run(fx.material, fx.harmony, fx.voice_plan);
    ASSERT_FALSE(r.notes.empty());
    Tick piece_end = 0;
    Tick middle_sounding = 0;
    bool saw_ground_v2 = false;
    for (const auto& note : r.notes) {
      piece_end = std::max(piece_end, note.start_tick + note.duration);
      if (note.voice == 1)
        middle_sounding += note.duration;
      if (note.voice == 2)
        saw_ground_v2 = true;
    }
    EXPECT_TRUE(saw_ground_v2) << "chaconne ground must sit under both upper voices on voice 2";
    ASSERT_GT(piece_end, 0);
    const double share = static_cast<double>(middle_sounding) / static_cast<double>(piece_end);
    EXPECT_GE(share, 0.25) << "seed " << c.seed << " minor " << c.is_minor << " bars "
                           << c.target_bars << " middle-voice share " << share;
    EXPECT_LE(share, 1.0) << "seed " << c.seed << " minor " << c.is_minor << " bars "
                          << c.target_bars << " middle-voice share " << share;
  }
}

// The middle voice must be a voice, not a doubling: it never states the tone
// either neighbour is stating at the same onset, and it moves less often than
// the variation above it.
TEST(GroundVariationChaconne, MiddleVoiceIsNeitherADoublingNorTheBusiestLine) {
  for (const Case& c : casesFor(FormType::Chaconne)) {
    const HarnessFixture fx = build(c.form, c.seed, c.is_minor, c.target_bars);
    const ComposeResult r = Composer{}.run(fx.material, fx.harmony, fx.voice_plan);
    std::size_t variation_onsets = 0;
    std::size_t middle_onsets = 0;
    for (const auto& note : r.notes) {
      if (note.voice == 0)
        ++variation_onsets;
      if (note.voice == 1)
        ++middle_onsets;
    }
    EXPECT_LT(middle_onsets, variation_onsets)
        << "seed " << c.seed << " minor " << c.is_minor << " bars " << c.target_bars;
    const auto pitch_at = [&r](VoiceId voice, Tick tick) {
      int pitch = -1;
      for (const auto& note : r.notes) {
        if (note.voice == voice && note.start_tick <= tick &&
            tick < note.start_tick + note.duration)
          pitch = static_cast<int>(note.pitch);
      }
      return pitch;
    };
    for (const auto& note : r.notes) {
      if (note.voice != 1)
        continue;
      const int middle = static_cast<int>(note.pitch);
      const int above = pitch_at(0, note.start_tick);
      const int below = pitch_at(2, note.start_tick);
      if (above >= 0)
        EXPECT_NE(middle, above) << "middle voice doubles the variation at " << note.start_tick;
      if (below >= 0)
        EXPECT_NE(middle, below) << "middle voice doubles the ground at " << note.start_tick;
    }
  }
}

// The middle voice of both ground forms is a line, not a broken chord. Its
// resting tones are consonant over the held ground; between two of them a third
// apart it may sound the scale tone that joins them, and that tone is the only
// dissonance over the ground the fill writes. It therefore falls off the beat
// and is approached and left by step, from and onto tones that are themselves
// consonant over the ground.

// The middle line as it is composed, in onset order.
std::vector<NoteEvent> middleLine(const ComposeResult& r) {
  std::vector<NoteEvent> line;
  for (std::size_t idx = 0; idx < r.notes.size() && idx < r.provenance.size(); ++idx) {
    if (r.provenance[idx].voice_intent == VoiceIntent::TrioVoiceCarrier)
      line.push_back(r.notes[idx]);
  }
  std::stable_sort(line.begin(), line.end(), [](const NoteEvent& lhs, const NoteEvent& rhs) {
    return lhs.start_tick < rhs.start_tick;
  });
  return line;
}

void expectMiddleVoiceDissonanceIsPassing(FormType form) {
  std::size_t passing = 0;
  for (const Case& c : casesFor(form)) {
    for (SubjectCharacter character : kCharacters) {
      const HarnessFixture fx = build(c.form, c.seed, c.is_minor, c.target_bars, character);
      const ComposeResult r = Composer{}.run(fx.material, fx.harmony, fx.voice_plan);
      ASSERT_EQ(r.provenance.size(), r.notes.size());
      const std::vector<NoteEvent> line = middleLine(r);
      // The ground tone each bar holds: its bar-head tone, which is what the
      // middle voice is written over however the statement decorates the bar.
      const std::uint16_t bars = resolvedBars(c.form, c.target_bars);
      std::vector<int> ground(bars, -1);
      for (const NoteEvent& note : r.notes) {
        if (note.voice == groundVoice(c.form) && note.start_tick % kTicksPerBar34 == 0 &&
            note.start_tick / kTicksPerBar34 < bars)
          ground[note.start_tick / kTicksPerBar34] = note.pitch;
      }
      const auto restsOverGround = [&](const NoteEvent& note) {
        const std::size_t bar = note.start_tick / kTicksPerBar34;
        return bar < ground.size() && ground[bar] >= 0 &&
               rule_helpers::isConsonantAboveBass(note.pitch,
                                                  static_cast<std::uint8_t>(ground[bar]));
      };
      // The closing two bars are the cadence formula, not the fill.
      const Tick fill_end = static_cast<Tick>(bars - 2) * kTicksPerBar34;
      for (std::size_t idx = 0; idx < line.size(); ++idx) {
        const NoteEvent& note = line[idx];
        if (note.start_tick >= fill_end || note.start_tick % kTicksPerBeat == 0)
          continue;
        if (ground[note.start_tick / kTicksPerBar34] < 0 || restsOverGround(note))
          continue;
        // A tone held over from the onset before it adds no interval of its own.
        if (idx > 0 && line[idx - 1].pitch == note.pitch)
          continue;
        ++passing;
        const auto where = [&]() {
          return ::testing::Message()
                 << "form " << static_cast<int>(form) << " character "
                 << static_cast<int>(character) << " seed " << c.seed << " minor " << c.is_minor
                 << " bars " << c.target_bars << " tick " << note.start_tick << " pitch "
                 << static_cast<int>(note.pitch);
        };
        ASSERT_GT(idx, 0u) << where();
        ASSERT_LT(idx + 1, line.size()) << where();
        const NoteEvent& before = line[idx - 1];
        const NoteEvent& after = line[idx + 1];
        EXPECT_EQ(before.start_tick + before.duration, note.start_tick) << where();
        EXPECT_EQ(note.start_tick + note.duration, after.start_tick) << where();
        const int approach = std::abs(static_cast<int>(note.pitch) - before.pitch);
        const int departure = std::abs(static_cast<int>(after.pitch) - note.pitch);
        EXPECT_TRUE(approach == 1 || approach == 2) << where() << " approached by " << approach;
        EXPECT_TRUE(departure == 1 || departure == 2) << where() << " left by " << departure;
        EXPECT_TRUE(restsOverGround(before)) << where() << " from a dissonance";
        EXPECT_TRUE(restsOverGround(after)) << where() << " onto a dissonance";
      }
    }
  }
  EXPECT_GT(passing, 0u) << "the sweep reached no passing tone in the middle voice";
}

TEST(GroundVariationChaconne, MiddleVoiceDissonanceOverTheGroundIsPassing) {
  expectMiddleVoiceDissonanceIsPassing(FormType::Chaconne);
}

TEST(GroundVariationPassacaglia, MiddleVoiceDissonanceOverTheGroundIsPassing) {
  expectMiddleVoiceDissonanceIsPassing(FormType::Passacaglia);
}

// A passing figure that the line carries on past by step, in the same
// direction and onto another tone consonant over the ground, is a four-note
// run. That run never spans a tritone: a line that climbs F G A B has nowhere
// to go but back across the interval it has just outlined.
//
// Only runs through a passing tone are read. The leap between two resting
// tones, and the leap into a beat anchor, can be a tritone on their own; the
// passing figure does not write those and is not allowed to add to them.
void expectPassingRunOutlinesNoTritone(FormType form) {
  std::size_t runs = 0;
  for (const Case& c : casesFor(form)) {
    for (SubjectCharacter character : kCharacters) {
      const HarnessFixture fx = build(c.form, c.seed, c.is_minor, c.target_bars, character);
      const ComposeResult r = Composer{}.run(fx.material, fx.harmony, fx.voice_plan);
      ASSERT_EQ(r.provenance.size(), r.notes.size());
      const std::vector<NoteEvent> line = middleLine(r);
      const std::uint16_t bars = resolvedBars(c.form, c.target_bars);
      std::vector<int> ground(bars, -1);
      for (const NoteEvent& note : r.notes) {
        if (note.voice == groundVoice(c.form) && note.start_tick % kTicksPerBar34 == 0 &&
            note.start_tick / kTicksPerBar34 < bars)
          ground[note.start_tick / kTicksPerBar34] = note.pitch;
      }
      const auto restsOverGround = [&](const NoteEvent& note) {
        const std::size_t bar = note.start_tick / kTicksPerBar34;
        return bar < ground.size() && ground[bar] >= 0 &&
               rule_helpers::isConsonantAboveBass(note.pitch,
                                                  static_cast<std::uint8_t>(ground[bar]));
      };
      const auto isStep = [](int motion) { return motion == 1 || motion == 2; };
      const Tick fill_end = static_cast<Tick>(bars - 2) * kTicksPerBar34;
      for (std::size_t idx = 1; idx + 2 < line.size(); ++idx) {
        const NoteEvent& from = line[idx - 1];
        const NoteEvent& pass = line[idx];
        const NoteEvent& land = line[idx + 1];
        const NoteEvent& beyond = line[idx + 2];
        if (beyond.start_tick >= fill_end || pass.start_tick % kTicksPerBeat == 0)
          continue;
        if (from.start_tick / kTicksPerBar34 != beyond.start_tick / kTicksPerBar34)
          continue;
        if (from.start_tick + from.duration != pass.start_tick ||
            pass.start_tick + pass.duration != land.start_tick ||
            land.start_tick + land.duration != beyond.start_tick)
          continue;
        if (!restsOverGround(from) || restsOverGround(pass) || !restsOverGround(land) ||
            !restsOverGround(beyond))
          continue;
        const int first = static_cast<int>(pass.pitch) - from.pitch;
        const int second = static_cast<int>(land.pitch) - pass.pitch;
        const int third = static_cast<int>(beyond.pitch) - land.pitch;
        const bool one_way = (first > 0) == (second > 0) && (second > 0) == (third > 0);
        if (!one_way || !isStep(std::abs(first)) || !isStep(std::abs(second)) ||
            !isStep(std::abs(third)))
          continue;
        ++runs;
        EXPECT_NE(std::abs(static_cast<int>(beyond.pitch) - from.pitch), 6)
            << "form " << static_cast<int>(form) << " character " << static_cast<int>(character)
            << " seed " << c.seed << " minor " << c.is_minor << " bars " << c.target_bars
            << " run from tick " << from.start_tick << " pitch " << static_cast<int>(from.pitch)
            << " to " << static_cast<int>(beyond.pitch);
      }
    }
  }
  EXPECT_GT(runs, 0u) << "the sweep reached no stepwise run through a passing tone";
}

TEST(GroundVariationChaconne, MiddleVoicePassingRunOutlinesNoTritone) {
  expectPassingRunOutlinesNoTritone(FormType::Chaconne);
}

TEST(GroundVariationPassacaglia, MiddleVoicePassingRunOutlinesNoTritone) {
  expectPassingRunOutlinesNoTritone(FormType::Passacaglia);
}

// Share of the middle voice's successions that move by step, over the whole
// sweep. A line confined to the tones consonant over the ground steps in about
// one succession in five, because only the fifth and sixth above the ground are
// adjacent; the passing tones lift that. The floor sits between the two, well
// under the reference inner voices, which step in about three of five.
void expectMiddleVoiceMovesByStep(FormType form) {
  std::size_t successions = 0;
  std::size_t steps = 0;
  for (const Case& c : casesFor(form)) {
    for (SubjectCharacter character : kCharacters) {
      const HarnessFixture fx = build(c.form, c.seed, c.is_minor, c.target_bars, character);
      const ComposeResult r = Composer{}.run(fx.material, fx.harmony, fx.voice_plan);
      const std::vector<NoteEvent> line = middleLine(r);
      for (std::size_t idx = 1; idx < line.size(); ++idx) {
        if (line[idx - 1].start_tick + line[idx - 1].duration != line[idx].start_tick)
          continue;  // a rest separates two phrases, not two tones of one.
        const int motion = std::abs(static_cast<int>(line[idx].pitch) - line[idx - 1].pitch);
        ++successions;
        if (motion == 1 || motion == 2)
          ++steps;
      }
    }
  }
  ASSERT_GT(successions, 0u);
  const double share = static_cast<double>(steps) / static_cast<double>(successions);
  EXPECT_GE(share, 0.30) << "form " << static_cast<int>(form) << " step share " << share << " ("
                         << steps << " of " << successions << ")";
}

TEST(GroundVariationChaconne, MiddleVoiceStepShareClearsTheBrokenChordFloor) {
  expectMiddleVoiceMovesByStep(FormType::Chaconne);
}

TEST(GroundVariationPassacaglia, MiddleVoiceStepShareClearsTheBrokenChordFloor) {
  expectMiddleVoiceMovesByStep(FormType::Passacaglia);
}

// The composition stays in C and the output is transposed to the request key,
// so a register that fits the instrument in C need not fit it a fourth lower or
// a tritone higher. The ground is immutable and fixes the octave the piece is
// placed in; every voice written over it has to sound inside the instrument's
// compass at that octave, in every key.
//
// Placement is asked for the octave and refused every note move, so a voice
// that only fits once single bars are folded inward fails here rather than
// being rescued. The ground itself is not read: it is not the builder's to move.
void expectUpperVoicesSoundInsideCompassInEveryKey(FormType form) {
  const InstrumentType instrument = defaultInstrumentForForm(form);
  const InstrumentPitchRange range = pitchRangeFor(instrument);
  for (int tonic_pc = 0; tonic_pc < 12; ++tonic_pc) {
    const Key tonic = static_cast<Key>(tonic_pc);
    for (bool minor : {false, true}) {
      for (SubjectCharacter character : kCharacters) {
        // Three consecutive seeds reach every ground variant.
        for (std::uint32_t seed : {1u, 2u, 3u}) {
          for (std::uint16_t target_bars : {std::uint16_t{0}, std::uint16_t{128}}) {
            ComposeRequest req = requestFor({form, seed, minor, target_bars}, character);
            req.tonic = tonic;
            HarnessFixture fx;
            ASSERT_EQ(buildFormFixture(req, &fx), FormDirectorStatus::Ok);
            ComposeResult r = Composer{}.run(fx.material, fx.harmony, fx.voice_plan);
            const auto placement = placeInOutputCompass(&r, fx.material, fx.harmony, tonic,
                                                        instrument, fx.harmony.ticksPerBar(),
                                                        [](const ComposeResult&) { return false; });
            ASSERT_TRUE(placement.has_value());
            const int sounding = keyTranspositionSemitones(tonic) + placement->octave_shift;
            int lowest = 127;
            int highest = 0;
            for (const NoteEvent& note : r.notes) {
              if (note.voice == groundVoice(form))
                continue;
              lowest = std::min(lowest, static_cast<int>(note.pitch) + sounding);
              highest = std::max(highest, static_cast<int>(note.pitch) + sounding);
            }
            const auto where = [&]() {
              return ::testing::Message()
                     << "form " << static_cast<int>(form) << " tonic " << tonic_pc << " minor "
                     << minor << " character " << static_cast<int>(character) << " seed " << seed
                     << " bars " << target_bars << " octave " << placement->octave_shift;
            };
            EXPECT_GE(lowest, static_cast<int>(range.low)) << where();
            EXPECT_LE(highest, static_cast<int>(range.high)) << where();
          }
        }
      }
    }
  }
}

TEST(GroundVariationChaconne, UpperVoicesSoundInsideTheCompassInEveryKey) {
  expectUpperVoicesSoundInsideCompassInEveryKey(FormType::Chaconne);
}

TEST(GroundVariationPassacaglia, UpperVoicesSoundInsideTheCompassInEveryKey) {
  expectUpperVoicesSoundInsideCompassInEveryKey(FormType::Passacaglia);
}

// Bounding the register by key narrows what a repair may reach, so a tone that
// was clean in C can be out of reach a semitone lower. Every key therefore has
// to ship at the longest length, where the arc lifts the variation highest: the
// product gate is run as it is for a request, in the request's own key.
void expectShipsInEveryKeyAtFullLength(FormType form) {
  for (int tonic_pc = 0; tonic_pc < 12; ++tonic_pc) {
    const Key tonic = static_cast<Key>(tonic_pc);
    for (bool minor : {false, true}) {
      for (SubjectCharacter character : kCharacters) {
        for (std::uint32_t seed : {1u, 2u, 3u, 5u}) {
          ComposeRequest req = requestFor({form, seed, minor, 128}, character);
          req.tonic = tonic;
          const test::ProductGateResult gate = test::runProductGate(req, tonic);
          EXPECT_TRUE(gate.ships())
              << "form " << static_cast<int>(form) << " tonic " << tonic_pc << " minor " << minor
              << " character " << static_cast<int>(character) << " seed " << seed << " built "
              << gate.built << " generated " << gate.generated << " placed " << gate.placed
              << " first final failure "
              << (gate.failures.empty() ? "none" : gate.failures.front().rule_id);
        }
      }
    }
  }
}

TEST(GroundVariationChaconne, ShipsInEveryKeyAtFullLength) {
  expectShipsInEveryKeyAtFullLength(FormType::Chaconne);
}

TEST(GroundVariationPassacaglia, ShipsInEveryKeyAtFullLength) {
  expectShipsInEveryKeyAtFullLength(FormType::Passacaglia);
}

// A passing figure that lands on the last onset of a bar hands the line to a
// tone the next bar chooses for itself. In minor that tone may not lie an
// augmented second from the landing: the raised seventh closing a dominant bar
// has to be free to go anywhere but down to the lowered sixth.
void expectBarClosingLandingIsLeftWithoutAugmentedSecond(FormType form) {
  for (const Case& c : casesFor(form)) {
    if (!c.is_minor)
      continue;
    for (SubjectCharacter character : kCharacters) {
      const HarnessFixture fx = build(c.form, c.seed, c.is_minor, c.target_bars, character);
      const ComposeResult r = Composer{}.run(fx.material, fx.harmony, fx.voice_plan);
      ASSERT_EQ(r.provenance.size(), r.notes.size());
      const std::vector<NoteEvent> line = middleLine(r);
      const std::uint16_t bars = resolvedBars(c.form, c.target_bars);
      std::vector<int> ground(bars, -1);
      for (const NoteEvent& note : r.notes) {
        if (note.voice == groundVoice(c.form) && note.start_tick % kTicksPerBar34 == 0 &&
            note.start_tick / kTicksPerBar34 < bars)
          ground[note.start_tick / kTicksPerBar34] = note.pitch;
      }
      const auto restsOverGround = [&](const NoteEvent& note) {
        const std::size_t bar = note.start_tick / kTicksPerBar34;
        return bar < ground.size() && ground[bar] >= 0 &&
               rule_helpers::isConsonantAboveBass(note.pitch,
                                                  static_cast<std::uint8_t>(ground[bar]));
      };
      const Tick fill_end = static_cast<Tick>(bars - 2) * kTicksPerBar34;
      for (std::size_t idx = 1; idx + 1 < line.size(); ++idx) {
        const NoteEvent& pass = line[idx - 1];
        const NoteEvent& land = line[idx];
        const NoteEvent& next = line[idx + 1];
        if (next.start_tick >= fill_end || next.start_tick % kTicksPerBar34 != 0)
          continue;
        if (pass.start_tick + pass.duration != land.start_tick ||
            land.start_tick + land.duration != next.start_tick)
          continue;
        if (pass.start_tick % kTicksPerBeat == 0 || restsOverGround(pass))
          continue;  // not a passing figure.
        EXPECT_FALSE(detail::isAugmentedSecond(land.pitch, next.pitch, 0, detail::Mode::Minor))
            << "form " << static_cast<int>(form) << " character " << static_cast<int>(character)
            << " seed " << c.seed << " bars " << c.target_bars << " tick " << land.start_tick << " "
            << static_cast<int>(land.pitch) << " -> " << static_cast<int>(next.pitch);
      }
    }
  }
}

TEST(GroundVariationChaconne, BarClosingLandingIsLeftWithoutAugmentedSecond) {
  expectBarClosingLandingIsLeftWithoutAugmentedSecond(FormType::Chaconne);
}

TEST(GroundVariationPassacaglia, BarClosingLandingIsLeftWithoutAugmentedSecond) {
  expectBarClosingLandingIsLeftWithoutAugmentedSecond(FormType::Passacaglia);
}

// A middle-line tone closing the bar off the beat forms no cross relation with the
// next bar's chord: no chord tone there inflects the same degree the other way.
// `pins_kept_leading_tone` also asserts the leading tone closing a bar before the minor
// tonic still occurs; both forms share the walk, and only the chaconne closes on it often.
void expectBarClosingToneIsNotRespelledByTheNextChord(FormType form, bool pins_kept_leading_tone) {
  std::size_t closings = 0;
  std::size_t kept_leading_tones = 0;
  for (const Case& c : casesFor(form)) {
    for (SubjectCharacter character : kCharacters) {
      const HarnessFixture fx = build(c.form, c.seed, c.is_minor, c.target_bars, character);
      const ComposeResult r = Composer{}.run(fx.material, fx.harmony, fx.voice_plan);
      ASSERT_EQ(r.provenance.size(), r.notes.size());
      const detail::Mode mode = c.is_minor ? detail::Mode::Minor : detail::Mode::Major;
      const std::uint16_t bars = resolvedBars(c.form, c.target_bars);
      // The approach and final bars take cadence spellings of their own.
      const Tick fill_end = static_cast<Tick>(bars - 2) * kTicksPerBar34;
      for (const NoteEvent& note : middleLine(r)) {
        const Tick end = note.start_tick + note.duration;
        if (end % kTicksPerBar34 != 0 || note.start_tick % kTicksPerBeat == 0 || end >= fill_end)
          continue;
        const ChordEvent* next = rule_helpers::chordAt(fx.harmony, end);
        ASSERT_NE(next, nullptr);
        const bool minor_third =
            next->quality == ChordQuality::Minor || next->quality == ChordQuality::Minor7;
        const bool seventh =
            next->quality == ChordQuality::Minor7 || next->quality == ChordQuality::Dominant7;
        const detail::ChordSpec spec{next->root_pc, minor_third, seventh};
        const detail::PcMask tones = detail::chordPcMaskInKey(spec, fx.harmony.tonic_pc, mode);
        ++closings;
        const int leading_pc = (fx.harmony.tonic_pc + interval::kMajor7th) % 12;
        if (c.is_minor && next->root_pc == fx.harmony.tonic_pc && note.pitch % 12 == leading_pc)
          ++kept_leading_tones;
        for (int pcl = 0; pcl < 12; ++pcl) {
          if ((tones & detail::pcBit(pcl)) == 0)
            continue;
          EXPECT_FALSE(rule_helpers::isCrossRelationPc(
              note.pitch, static_cast<std::uint8_t>(pcl),
              static_cast<std::uint8_t>(fx.harmony.tonic_pc), c.is_minor))
              << "form " << static_cast<int>(form) << " character " << static_cast<int>(character)
              << " seed " << c.seed << " minor " << c.is_minor << " bars " << c.target_bars
              << " tick " << note.start_tick << " pitch " << static_cast<int>(note.pitch)
              << " next chord tone " << pcl;
        }
      }
    }
  }
  EXPECT_GT(closings, 0u);
  if (pins_kept_leading_tone)
    EXPECT_GT(kept_leading_tones, 0u);
}

TEST(GroundVariationChaconne, BarClosingToneIsNotRespelledByTheNextChord) {
  expectBarClosingToneIsNotRespelledByTheNextChord(FormType::Chaconne, true);
}

TEST(GroundVariationPassacaglia, BarClosingToneIsNotRespelledByTheNextChord) {
  expectBarClosingToneIsNotRespelledByTheNextChord(FormType::Passacaglia, false);
}

// A middle-line tone closing a bar off the beat leads into the next bar's head by no
// augmented second or fifth and no diminished third or seventh. Inside a statement only:
// the head after a statement's last bar belongs to the next statement's line.
void expectBarClosingToneLeadsIntoTheHeadWithoutAnExcludedLeap(FormType form) {
  // Ground bars per statement: the chaconne's ground is four bars, the passacaglia's eight.
  const Tick statement = (form == FormType::Chaconne ? 4 : 8) * kTicksPerBar34;
  std::size_t pairs = 0;
  for (const Case& c : casesFor(form)) {
    for (SubjectCharacter character : kCharacters) {
      const HarnessFixture fx = build(c.form, c.seed, c.is_minor, c.target_bars, character);
      const ComposeResult r = Composer{}.run(fx.material, fx.harmony, fx.voice_plan);
      const detail::Mode mode = c.is_minor ? detail::Mode::Minor : detail::Mode::Major;
      const std::uint16_t bars = resolvedBars(c.form, c.target_bars);
      // The approach and final bars take cadence spellings of their own.
      const Tick fill_end = static_cast<Tick>(bars - 2) * kTicksPerBar34;
      const std::vector<NoteEvent> line = middleLine(r);
      for (std::size_t idx = 0; idx + 1 < line.size(); ++idx) {
        const NoteEvent& close = line[idx];
        const NoteEvent& head = line[idx + 1];
        const Tick end = close.start_tick + close.duration;
        if (end % kTicksPerBar34 != 0 || close.start_tick % kTicksPerBeat == 0 || end >= fill_end ||
            end % statement == 0 || head.start_tick != end)
          continue;
        ++pairs;
        const detail::SpelledInterval leap =
            detail::spelledInterval(close.pitch, head.pitch, fx.harmony.tonic_pc, mode);
        const bool excluded = (leap.quality == detail::SpelledQuality::Augmented &&
                               (leap.number == 2 || leap.number == 5)) ||
                              (leap.quality == detail::SpelledQuality::Diminished &&
                               (leap.number == 3 || leap.number == 7));
        EXPECT_FALSE(excluded) << "form " << static_cast<int>(form) << " character "
                               << static_cast<int>(character) << " seed " << c.seed << " minor "
                               << c.is_minor << " bars " << c.target_bars << " tick "
                               << close.start_tick << " pitches " << static_cast<int>(close.pitch)
                               << " -> " << static_cast<int>(head.pitch);
      }
    }
  }
  EXPECT_GT(pairs, 0u);
}

TEST(GroundVariationChaconne, BarClosingToneLeadsIntoTheHeadWithoutAnExcludedLeap) {
  expectBarClosingToneLeadsIntoTheHeadWithoutAnExcludedLeap(FormType::Chaconne);
}

TEST(GroundVariationPassacaglia, BarClosingToneLeadsIntoTheHeadWithoutAnExcludedLeap) {
  expectBarClosingToneLeadsIntoTheHeadWithoutAnExcludedLeap(FormType::Passacaglia);
}

// --- 9. Passacaglia terraced growth schedule (period-derived) ---------------
//
// The 3-voice passacaglia derives its per-cycle voice-presence schedule purely
// from the period (cycle) count:
//   periods >= 3: cycle 0 = ground-solo intro (V0 and V1 rest), one receding
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

TEST(GroundVariationPassacaglia, DefaultThreePeriodScheduleStartsWithGroundSoloIntro) {
  // Default 24-bar passacaglia = exactly 3 periods.
  std::size_t cycles = 0;
  const auto presence = passacagliaPresence(/*seed=*/3, /*bars=*/0, cycles);
  ASSERT_EQ(cycles, 3u);
  // Cycle 0: the defining ground-solo opening.
  EXPECT_FALSE(presence[0][0]) << "3-period intro must rest V0";
  EXPECT_FALSE(presence[0][1]) << "3-period intro must rest V1";
  EXPECT_TRUE(presence[0][2]) << "ground must sound in cycle 0";
  // The arc climax at cycle 1 restores the full texture.
  EXPECT_TRUE(presence[1][0]);
  EXPECT_TRUE(presence[1][1]) << "3-period climax must sound V1";
  EXPECT_TRUE(presence[1][2]);
  // The final resolve cycle keeps the full texture above the ground.
  EXPECT_TRUE(presence[2][0]);
  EXPECT_TRUE(presence[2][1]);
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
  // The climax is before the final resolve cycle (5 cycles -> cycle 3).
  const std::size_t climax_idx = 3;
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

// ---------------------------------------------------------------------------
// Variation pattern rotation: non-climax variation cycles rotate through the
// form's figuration palette, so consecutive cycles alternate idioms. Locked
// from the material's per-cycle duration histograms:
//   - cycle 0 stays the Ground-role / establishing statement (quarters only),
//   - at least one adjacent non-special cycle pair differs in its duration
//     histogram (the figura corta cell is rhythmically distinct),
//   - the climax cycle stays the design peak (uniform sixteenths).
// ---------------------------------------------------------------------------

// Sorted duration multiset of one variation block's notes.
std::vector<Tick> durationHistogram(const std::vector<MaterialNote>& notes) {
  std::vector<Tick> durations;
  durations.reserve(notes.size());
  for (const MaterialNote& n : notes)
    durations.push_back(n.duration);
  std::sort(durations.begin(), durations.end());
  return durations;
}

bool allDurationsAre(const std::vector<MaterialNote>& notes, Tick dur) {
  for (const MaterialNote& n : notes) {
    if (n.duration != dur)
      return false;
  }
  return true;
}

TEST(GroundVariationChaconne, VariationCyclesAlternatePatterns) {
  for (std::uint32_t seed : {1u, 2u, 3u, 7u}) {
    for (bool minor : {false, true}) {
      const HarnessFixture fx = build(FormType::Chaconne, seed, minor, /*bars=*/128);
      const auto& vars = fx.material.variations;
      ASSERT_GE(vars.size(), 4u);
      // Cycle 0: Ground-role establishing statement, quarters only.
      EXPECT_TRUE(allDurationsAre(vars[0].notes, kTicksPerBeat))
          << "seed " << seed << ": cycle 0 must stay quarters";
      EXPECT_EQ(vars[0].role, VariationRole::Ground);
      // Adjacent non-special cycles: at least one pair differs in rhythm.
      int differing_pairs = 0;
      for (std::size_t cyc = 1; cyc + 1 < vars.size(); ++cyc) {
        if (durationHistogram(vars[cyc].notes) != durationHistogram(vars[cyc + 1].notes))
          ++differing_pairs;
      }
      EXPECT_GT(differing_pairs, 0)
          << "seed " << seed << " minor " << minor << ": no rhythmic alternation across cycles";
      // The design peak: at least one peak-tier cycle of uniform sixteenths
      // (the climax cycle bypasses the rotation; the character density bias may
      // shift the recorded tier, so the rhythm itself is what is locked).
      bool found_peak = false;
      for (const auto& var : vars) {
        if (var.density_level >= 2 && allDurationsAre(var.notes, kTicksPerBeat / 4))
          found_peak = true;
      }
      EXPECT_TRUE(found_peak) << "seed " << seed << ": climax must stay uniform sixteenths";
    }
  }
}

TEST(GroundVariationPassacaglia, VariationCyclesAlternatePatterns) {
  for (std::uint32_t seed : {1u, 2u, 3u, 7u}) {
    for (bool minor : {false, true}) {
      const HarnessFixture fx = build(FormType::Passacaglia, seed, minor, /*bars=*/128);
      const auto& vars = fx.material.passacaglia_variations;
      ASSERT_GE(vars.size(), 4u);
      int differing_pairs = 0;
      for (std::size_t cyc = 0; cyc + 1 < vars.size(); ++cyc) {
        if (durationHistogram(vars[cyc].notes) != durationHistogram(vars[cyc + 1].notes))
          ++differing_pairs;
      }
      EXPECT_GT(differing_pairs, 0)
          << "seed " << seed << " minor " << minor << ": no rhythmic alternation across cycles";
      // The climax block stays the design peak: uniform sixteenths.
      bool found_climax = false;
      for (const auto& var : vars) {
        if (var.is_climax) {
          found_climax = true;
          EXPECT_TRUE(allDurationsAre(var.notes, kTicksPerBeat / 4))
              << "seed " << seed << ": climax must stay uniform sixteenths";
        }
      }
      EXPECT_TRUE(found_climax);
    }
  }
}

TEST(GroundVariationPassacaglia, PatternedCyclesStayAboveCounterFigurationBand) {
  // The center-anchored patterns (sawtooth / figura corta) are lifted an octave
  // so V0 never dips into the V1 counter-figuration band (C3-B3 + shift); a dip
  // would trip voice_crossing across the all-Material texture.
  for (std::uint32_t seed : {1u, 2u, 3u, 7u, 9u}) {
    const HarnessFixture fx = build(FormType::Passacaglia, seed, /*minor=*/true, /*bars=*/128);
    for (const auto& var : fx.material.passacaglia_variations) {
      for (const MaterialNote& n : var.notes)
        EXPECT_GE(static_cast<int>(n.pitch), 60)
            << "seed " << seed << ": V0 dipped below the scalar-wave band floor";
    }
  }
}

// The seed-selected eight-bar ground pitch line repeats exactly, with one
// sustained event per bar. Adjacent equal pitches, including at cycle seams,
// must stay within the corpus repeated-note envelope (max run 4) for every
// seed-variant ground and both modes.
TEST(GroundVariationPassacaglia, GroundSplitSeamStaysInRepeatedRunEnvelope) {
  for (std::uint32_t seed : {1u, 2u, 3u, 4u, 5u, 6u}) {
    for (bool minor : {false, true}) {
      const HarnessFixture fx = build(FormType::Passacaglia, seed, minor, 64);
      const ComposeResult r = Composer{}.run(fx.material, fx.harmony, fx.voice_plan);
      const std::vector<std::uint8_t> ground = groundPitches(r, groundVoice(FormType::Passacaglia));
      ASSERT_FALSE(ground.empty()) << "seed " << seed << " minor " << minor;
      int run = 1;
      int worst = 1;
      for (std::size_t i = 1; i < ground.size(); ++i) {
        run = ground[i] == ground[i - 1] ? run + 1 : 1;
        worst = std::max(worst, run);
      }
      EXPECT_LE(worst, 4) << "seed " << seed << " minor " << minor;
    }
  }
}

// ---------------------------------------------------------------------------
// Multi-wave energy arch (chaconne + passacaglia).
//
// The shared arc draws one climax swell at ~80% with the density tier rising
// then falling monotonically. The ground-variation builders add a SECOND wave
// locally: an intermediate swell at ~60% of the span, then a terraced (stepped,
// non-decreasing) buildup into the final arch. Both are design values, active
// only for long grounds (>= 8 cycles); shorter pieces keep the single arc.
// ---------------------------------------------------------------------------

// Cycle bars per ground form (chaconne = 4-bar ground, passacaglia = 8-bar).
int cycleBarsFor(FormType form) {
  return form == FormType::Passacaglia ? 8 : 4;
}

// The builders' own index arithmetic, mirrored so the test targets the same
// cycles the source shapes (climax at ~80%, swell at ~60% but >= 2 cycles below
// the climax so the pre-climax receding cycle stays a dip).
std::size_t climaxCycle(std::size_t cycles) {
  if (cycles <= 1)
    return 0;
  const std::size_t last = cycles - 1;
  std::size_t idx = (last * 4) / 5;
  if (idx >= last)
    idx = last - 1;
  return idx;
}
std::size_t midWaveCycleForTest(std::size_t cycles, std::size_t climax_idx) {
  std::size_t idx = (cycles * 3) / 5;
  if (climax_idx >= 2 && idx > climax_idx - 2)
    idx = climax_idx - 2;
  return idx;
}

// Voice-0 (figuration) note count per ground cycle from the composed output.
std::vector<std::size_t> voice0PerCycle(const ComposeResult& r, int cycle_bars,
                                        std::size_t cycles) {
  const Tick cycle_ticks = static_cast<Tick>(cycle_bars) * kTicksPerBar34;
  std::vector<std::size_t> per_cycle(cycles, 0);
  for (const auto& n : r.notes) {
    if (n.voice != 0)
      continue;
    const std::size_t cyc = static_cast<std::size_t>(n.start_tick / cycle_ticks);
    if (cyc < cycles)
      ++per_cycle[cyc];
  }
  return per_cycle;
}

// The mid-wave cycle carries strictly more figuration notes than both of its
// (non-climax) neighbours -- the audible intermediate swell. Measured on the
// 128-bar cap, where the swell rides a tier-respecting pattern for every seed.
void expectMidWaveSwell(FormType form) {
  const int cycle_bars = cycleBarsFor(form);
  const std::size_t cycles = 128u / static_cast<std::size_t>(cycle_bars);
  const std::size_t climax_idx = climaxCycle(cycles);
  const std::size_t mid = midWaveCycleForTest(cycles, climax_idx);
  ASSERT_GE(cycles, 8u);
  ASSERT_GT(mid, 0u);
  ASSERT_LT(mid + 1, cycles);
  ASSERT_NE(mid, climax_idx);
  ASSERT_NE(mid + 1, climax_idx);
  ASSERT_NE(mid - 1, climax_idx);
  for (std::uint32_t seed : {1u, 2u, 3u, 4u, 5u}) {
    for (bool minor : {false, true}) {
      const HarnessFixture fx = build(form, seed, minor, 128);
      const ComposeResult r = Composer{}.run(fx.material, fx.harmony, fx.voice_plan);
      const std::vector<std::size_t> per_cycle = voice0PerCycle(r, cycle_bars, cycles);
      EXPECT_GT(per_cycle[mid], per_cycle[mid - 1])
          << "form " << static_cast<int>(form) << " seed " << seed << " minor " << minor
          << ": mid-wave cycle " << mid << " not denser than the earlier neighbour";
      EXPECT_GT(per_cycle[mid], per_cycle[mid + 1])
          << "form " << static_cast<int>(form) << " seed " << seed << " minor " << minor
          << ": mid-wave cycle " << mid << " not denser than the later neighbour";
    }
  }
}

TEST(GroundVariationChaconne, MidWaveSwellIsLocalPeak) {
  expectMidWaveSwell(FormType::Chaconne);
}

TEST(GroundVariationPassacaglia, MidWaveSwellIsLocalPeak) {
  expectMidWaveSwell(FormType::Passacaglia);
}

// Resolved density tier per ground cycle, read from the recorded variation
// blocks (density_level). The tier -- not the raw note count -- is the density
// the terraced clamp controls: the figura-corta cell carries a fixed
// subdivision regardless of tier, and the cadential landing thins the final
// bars, so both confound a raw note count. Cycles with no variation block (the
// long-form ground-solo intro) are left at -1.
std::vector<int> tierPerCycle(const HarnessFixture& fx, FormType form, int cycle_bars,
                              std::size_t cycles) {
  const Tick period = static_cast<Tick>(cycle_bars) * kTicksPerBar34;
  std::vector<int> tiers(cycles, -1);
  if (form == FormType::Passacaglia) {
    for (const auto& var : fx.material.passacaglia_variations) {
      const std::size_t cyc = static_cast<std::size_t>(var.start_tick / period);
      if (cyc < cycles)
        tiers[cyc] = var.density_level;
    }
  } else {
    for (const auto& var : fx.material.variations) {
      const std::size_t cyc = static_cast<std::size_t>(var.start_tick / period);
      if (cyc < cycles)
        tiers[cyc] = var.density_level;
    }
  }
  return tiers;
}

// The final buildup terraces up: the resolved density tier is non-decreasing
// across the last three ground cycles (the arc's resolve limb would otherwise
// fall). Measured at a length where the climax sits inside that trailing window
// (10 cycles: chaconne = 40 bars, passacaglia = 80 bars), so the clamp visibly
// carries the peak tier forward instead of receding.
void expectTerracedTail(FormType form) {
  const int cycle_bars = cycleBarsFor(form);
  const std::uint16_t bars = static_cast<std::uint16_t>(10 * cycle_bars);
  const std::size_t cycles = 10u;
  for (std::uint32_t seed : {1u, 2u, 3u, 4u, 5u, 6u}) {
    for (bool minor : {false, true}) {
      const HarnessFixture fx = build(form, seed, minor, bars);
      const std::vector<int> tiers = tierPerCycle(fx, form, cycle_bars, cycles);
      for (std::size_t cyc = cycles - 3; cyc + 1 < cycles; ++cyc) {
        ASSERT_GE(tiers[cyc], 0) << "form " << static_cast<int>(form) << " cycle " << cyc;
        ASSERT_GE(tiers[cyc + 1], 0) << "form " << static_cast<int>(form) << " cycle " << (cyc + 1);
        EXPECT_GE(tiers[cyc + 1], tiers[cyc])
            << "form " << static_cast<int>(form) << " seed " << seed << " minor " << minor
            << ": density tier fell from cycle " << cyc << " (" << tiers[cyc] << ") to "
            << (cyc + 1) << " (" << tiers[cyc + 1] << ")";
      }
    }
  }
}

TEST(GroundVariationChaconne, FinalBuildupTerracesUp) {
  expectTerracedTail(FormType::Chaconne);
}

TEST(GroundVariationPassacaglia, FinalBuildupTerracesUp) {
  expectTerracedTail(FormType::Passacaglia);
}

// The two-wave shaping must not break validation: every long-form ground piece
// across seeds 1..10, both modes, both forms, still composes clean.
void expectMultiWaveValidatesOk(FormType form) {
  for (std::uint32_t seed = 1; seed <= 10; ++seed) {
    for (bool minor : {false, true}) {
      const HarnessFixture fx = build(form, seed, minor, 128);
      const ComposeResult r = Composer{}.run(fx.material, fx.harmony, fx.voice_plan);
      EXPECT_TRUE(r.validation.failures.empty())
          << "form " << static_cast<int>(form) << " seed " << seed << " minor " << minor
          << " first failure="
          << (r.validation.failures.empty() ? "" : r.validation.failures.front().rule_id);
      EXPECT_EQ(r.validation.status, ValidationStatus::Ok);
    }
  }
}

// Share of a line's notes lying inside a two-pitch alternation of four or more
// notes -- a line that keeps returning to the tone before last is shaking
// rather than moving. A tone restated on the next subdivision is one sound, so
// a repeat does not advance the figure.
double shakeShare(const std::vector<MaterialNote>& notes) {
  std::vector<int> heard;
  for (const MaterialNote& note : notes) {
    const int pitch = static_cast<int>(note.pitch);
    if (heard.empty() || heard.back() != pitch) {
      heard.push_back(pitch);
    }
  }
  if (heard.size() < 4) {
    return 0.0;
  }
  std::vector<bool> shaken(heard.size(), false);
  auto mark = [&shaken](std::size_t end, std::size_t run) {
    if (run + 2 < 4) {
      return;
    }
    for (std::size_t idx = end - run - 2; idx < end; ++idx) {
      shaken[idx] = true;
    }
  };
  std::size_t run = 0;
  for (std::size_t idx = 2; idx < heard.size(); ++idx) {
    if (heard[idx] == heard[idx - 2] && heard[idx] != heard[idx - 1]) {
      ++run;
      continue;
    }
    mark(idx, run);
    run = 0;
  }
  mark(heard.size(), run);
  return static_cast<double>(std::count(shaken.begin(), shaken.end(), true)) /
         static_cast<double>(heard.size());
}

// The middle voice realises the harmony the ground implies. Written as a beat
// anchor and one companion tone struck alternately it states two pitches for
// the whole beat, and because the anchor selector prefers the tone nearest the
// last one the same pair outlives the beat: whole bars settle onto a single
// interval. It walks the tones the held ground admits instead.
//
// The ceiling is what the reference corpus's own three-voice middle voices
// write over a line of this length -- roughly a hundred notes, where their
// ninety-fifth percentile share is 0.396. The share depends on how long the
// line is, so a shorter line would be judged against a larger figure.
void expectMiddleVoiceWalks(FormType form) {
  constexpr double kCorpusShakeShare = 0.396;
  for (std::uint32_t seed = 1; seed <= 8; ++seed) {
    for (bool minor : {false, true}) {
      const HarnessFixture fx = build(form, seed, minor, 0);
      for (const TrioVoiceLine& line : fx.material.trio_voices) {
        if (line.notes.size() < 24) {
          continue;
        }
        EXPECT_LE(shakeShare(line.notes), kCorpusShakeShare)
            << "form " << static_cast<int>(form) << " seed " << seed << " minor " << minor
            << ": the middle voice oscillates between two pitches instead of walking";
      }
    }
  }
}

TEST(GroundVariationChaconne, MiddleVoiceWalksInsteadOfShaking) {
  expectMiddleVoiceWalks(FormType::Chaconne);
}

TEST(GroundVariationPassacaglia, MiddleVoiceWalksInsteadOfShaking) {
  expectMiddleVoiceWalks(FormType::Passacaglia);
}

TEST(GroundVariationChaconne, MultiWaveValidatesAcrossSeeds) {
  expectMultiWaveValidatesOk(FormType::Chaconne);
}

TEST(GroundVariationPassacaglia, MultiWaveValidatesAcrossSeeds) {
  expectMultiWaveValidatesOk(FormType::Passacaglia);
}

}  // namespace
}  // namespace bach::composer
