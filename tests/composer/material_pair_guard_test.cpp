// Shared material-pair guard: the strict-parallel and crossing judgement every
// builder applies to a tone placed against voices already sounding, and the
// requests whose builders now reach it. The request regressions run through
// the product gate (decoration, compass placement, FinalScore validation and
// the counterpoint budget), which is the surface that refused them.

#include "composer/material_pair_guard.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <vector>

#include "composer/texture_helpers.h"
#include "product_gate_test_util.h"

namespace bach::composer {
namespace {

std::string describe(const std::vector<ValidationFailure>& failures) {
  std::string out;
  for (const ValidationFailure& failure : failures)
    out += failure.rule_id + " ";
  return out;
}

ComposeRequest request(FormType form, SubjectCharacter character, bool minor, std::uint16_t bars,
                       std::uint32_t seed) {
  ComposeRequest req;
  req.form = form;
  req.character = character;
  req.is_minor = minor;
  req.target_bars = bars;
  req.seed = seed;
  return req;
}

void expectShipsClean(const ComposeRequest& req) {
  const test::ProductGateResult gate = test::runProductGate(req);
  ASSERT_TRUE(gate.built);
  EXPECT_TRUE(gate.placed) << "seed " << req.seed << " bars " << req.target_bars;
  EXPECT_TRUE(gate.failures.empty())
      << "form " << static_cast<int>(req.form) << " character " << static_cast<int>(req.character)
      << " minor " << req.is_minor << " bars " << req.target_bars << " seed " << req.seed << ": "
      << describe(gate.failures);
}

// --- Predicates ------------------------------------------------------------

TEST(MaterialPairGuard, ClassifiesFaultsInRankOrder) {
  // 67 -> 69 over 60 -> 62: a fifth to a fifth in similar motion.
  EXPECT_EQ(classifyPairFault(67, 69, 60, 62), PairFault::kStrict);
  EXPECT_TRUE(isStrictParallelPerfect(67, 69, 60, 62));
  // Line below the other voice: the orientation is taken from the arrival.
  EXPECT_TRUE(isStrictParallelPerfect(60, 62, 67, 69));
  // A third into a fifth with the upper voice leaping: hidden, not strict.
  EXPECT_EQ(classifyPairFault(64, 69, 60, 62), PairFault::kHidden);
  EXPECT_FALSE(isStrictParallelPerfect(64, 69, 60, 62));
  // A fifth to a fifth by contrary motion.
  EXPECT_EQ(classifyPairFault(67, 74, 60, 55), PairFault::kAntiParallel);
  // Upper voice leaps down onto an octave against a rising voice.
  EXPECT_EQ(classifyPairFault(76, 72, 57, 60), PairFault::kBattuta);
  // Oblique motion and a silent voice are never faults.
  EXPECT_EQ(classifyPairFault(67, 67, 60, 62), PairFault::kNone);
  EXPECT_EQ(classifyPairFault(67, 69, -1, 62), PairFault::kNone);
  EXPECT_LT(PairFault::kHidden, PairFault::kStrict);
}

TEST(MaterialPairGuard, UnionOnsetMotionsReadTheLastToneBeforeTheOnset) {
  // Voice 1 moves in sixteenths under a line that will hold across them; the
  // pair the validator judges at tick 480 is voice 1's tone at 360, not at 0.
  ThemeToneRegistry registry;
  for (int idx = 0; idx < 4; ++idx)
    registry.record(static_cast<Tick>(idx) * 120, 1, 50 + idx, 120);
  registry.record(480, 1, 60, 480);
  std::vector<ConcurrentMotion> motions;
  unionOnsetMotions(registry, 480, /*voice=*/0, /*num_voices=*/2, motions);
  ASSERT_EQ(motions.size(), 1u);
  EXPECT_EQ(motions[0].prev, 53);
  EXPECT_EQ(motions[0].curr, 60);
}

TEST(MaterialPairGuard, CrossingIsJudgedOverTheWholeSustain) {
  ThemeToneRegistry registry;
  registry.record(0, 0, 67, 240);
  registry.record(240, 0, 60, 240);  // V0 drops under the held middle tone.
  EXPECT_TRUE(crossesSoundingVoices(registry, 1, 3, 62, 0, 480));
  EXPECT_FALSE(crossesSoundingVoices(registry, 1, 3, 62, 0, 240));
  EXPECT_FALSE(crossesSoundingVoices(registry, 1, 3, 60, 0, 480));  // a unison is not a crossing.
}

TEST(MaterialPairGuard, ChooserRanksStrictParallelBelowEveryCost) {
  const std::vector<ConcurrentMotion> motions = {ConcurrentMotion{1, 60, 62}};
  MaterialPairChooser chooser(67, motions);
  chooser.offer(69, 0);    // strict parallel fifth, otherwise preferred.
  chooser.offer(71, 100);  // dissonant or far: still strict-free.
  EXPECT_EQ(chooser.best(), 71);
  EXPECT_NE(chooser.bestFault(), PairFault::kStrict);
  chooser.offer(67, 50);  // holding is oblique and cheaper than 71.
  EXPECT_EQ(chooser.best(), 67);
  chooser.offer(65, 0, /*crosses=*/true);  // a crossing ranks below everything.
  EXPECT_EQ(chooser.best(), 67);
}

// --- Builder sites ---------------------------------------------------------

// Goldberg running-arrival relief: a minor free variation over an F ground
// bar used to keep a true parallel octave when every consonant step was out of
// scale, at these lengths and seeds.
TEST(MaterialPairGuardRegression, GoldbergRunningArrivalShipsNoParallelOctave) {
  struct Cell {
    std::uint16_t bars;
    std::uint32_t seed;
  };
  for (const Cell cell : {Cell{12, 1}, Cell{28, 1}, Cell{28, 3}, Cell{64, 4}, Cell{64, 6}}) {
    expectShipsClean(request(FormType::GoldbergVariations, SubjectCharacter::Severe,
                             /*minor=*/true, cell.bars, cell.seed));
  }
}

// Fugue-family figuration wave: the escape had no rung below its three-tone
// vocabulary and kept a step that formed a true parallel octave.
TEST(MaterialPairGuardRegression, FigurationWaveShipsNoParallelInFugueTails) {
  for (FormType form : {FormType::ToccataAndFugue, FormType::FantasiaAndFugue}) {
    for (SubjectCharacter character : {SubjectCharacter::Playful, SubjectCharacter::Restless}) {
      expectShipsClean(request(form, character, /*minor=*/false, 36, 21));
      expectShipsClean(request(form, character, /*minor=*/false, 36, 20));
      expectShipsClean(request(form, character, /*minor=*/false, 128, 20));
    }
  }
}

// Reactive countersubject repair: judged against the entry's tone at the union
// onset before each anchor, not at the counterline's own previous attack.
TEST(MaterialPairGuardRegression, CountersubjectRepairJudgesTheUnionOnsetPair) {
  for (FormType form : {FormType::Fugue, FormType::PreludeAndFugue}) {
    for (SubjectCharacter character : {SubjectCharacter::Playful, SubjectCharacter::Restless})
      expectShipsClean(request(form, character, /*minor=*/false, 128, 24));
  }
  expectShipsClean(request(FormType::Fugue, SubjectCharacter::Playful, false, 128, 25));
  expectShipsClean(request(FormType::Fugue, SubjectCharacter::Severe, false, 128, 36));
}

// Chorale walking bass: a true parallel into the bar head is replaced by a
// strict-free tone even when every alternative costs a hidden fifth or a
// passing dissonance.
TEST(MaterialPairGuardRegression, ChoraleWalkingBassTradesTrueParallelAway) {
  for (bool minor : {false, true}) {
    for (std::uint32_t seed : {2u, 26u})
      expectShipsClean(
          request(FormType::ChoralePrelude, SubjectCharacter::Severe, minor, 128, seed));
  }
}

// Free counterpoint: a search tone may not be crossed by another voice at any
// onset while it sounds, whatever its intent.
TEST(MaterialPairGuardRegression, FreeCounterpointNeverCrossesWhileSounding) {
  for (SubjectCharacter character : {SubjectCharacter::Severe, SubjectCharacter::Playful,
                                     SubjectCharacter::Noble, SubjectCharacter::Restless}) {
    for (bool minor : {false, true}) {
      for (std::uint16_t bars : {std::uint16_t{0}, std::uint16_t{96}}) {
        for (std::uint32_t seed = 1; seed <= 6; ++seed) {
          ComposeRequest req = request(FormType::Passacaglia, character, minor, bars, seed);
          req.enable_free_counterpoint = true;
          const test::ProductGateResult gate = test::runProductGate(req);
          ASSERT_TRUE(gate.built);
          for (const ValidationFailure& failure : gate.composed.validation.failures)
            EXPECT_NE(failure.rule_id, "voice_crossing") << "seed " << seed << " bars " << bars;
          for (const ValidationFailure& failure : gate.failures)
            EXPECT_NE(failure.rule_id, "voice_crossing") << "seed " << seed << " bars " << bars;
        }
      }
    }
  }
}

}  // namespace
}  // namespace bach::composer
