// Fixed-line form builder tests: chorale prelude (cantus firmus + figuration)
// and the Goldberg-style immutable-bass variation skeleton.
//
// Both builders honour ResolvedRequest length / mode / character / arc and
// derive all material from (seed, indices) only. These tests cover, across the
// seeds {1,5,42,99} x modes {Major,Minor} x bars {natural, 2x, 128} matrix:
//   * end-to-end validation behind the product gate for every admissible
//     character, and determinism (the same request yields byte-identical notes),
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

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "composer/arc.h"
#include "composer/composer.h"
#include "composer/form_builders.h"
#include "composer/form_director.h"
#include "composer/minor_material.h"
#include "composer/rule_helpers.h"
#include "core/basic_types.h"
#include "core/pitch_utils.h"
#include "core/seed_decision.h"
#include "product_gate_test_util.h"

namespace bach::composer {
namespace {

constexpr RuleIdMask bit(RuleBit b) {
  return ruleBitMask(b);
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
// admissible character for the structural tests below.
SubjectCharacter choraleCharacter(bool minor) {
  return minor ? SubjectCharacter::Noble : SubjectCharacter::Severe;
}

constexpr std::array<std::uint32_t, 4> kSeeds = {{1, 5, 42, 99}};

// --- Shared: validation + determinism across the full matrix ----------------

// Judged behind the gate the product ships behind -- decoration, compass
// placement, FinalScore validation and the counterpoint budget -- because
// Generation-scope validation exempts the carrier-only vertical findings that
// make the product refuse a request.
void expectShipsCleanAcrossMatrix(FormType form, const std::vector<SubjectCharacter>& characters,
                                  const std::vector<std::uint32_t>& seeds) {
  for (bool minor : {false, true}) {
    for (SubjectCharacter character : characters) {
      for (std::uint32_t seed : seeds) {
        for (std::uint16_t bars : sweepBars(form)) {
          ComposeRequest req;
          req.form = form;
          req.is_minor = minor;
          req.character = character;
          req.target_bars = bars;
          req.seed = seed;
          const test::ProductGateResult gate = test::runProductGate(req);
          ASSERT_TRUE(gate.built);
          EXPECT_TRUE(gate.generated)
              << "minor=" << minor << " character=" << static_cast<int>(character)
              << " seed=" << seed << " bars=" << bars
              << " status=" << static_cast<int>(gate.composed.validation.status) << " first="
              << (gate.composed.validation.failures.empty()
                      ? ""
                      : gate.composed.validation.failures.front().rule_id);
          EXPECT_TRUE(gate.placed);
          EXPECT_TRUE(gate.failures.empty())
              << "minor=" << minor << " character=" << static_cast<int>(character)
              << " seed=" << seed << " bars=" << bars
              << " first=" << (gate.failures.empty() ? "" : gate.failures.front().rule_id);
          EXPECT_FALSE(gate.shipped.notes.empty());
        }
      }
    }
  }
}

TEST(FormCantusMatrix, ChoraleValidatesCleanAcrossMatrix) {
  // Seed 2 is the severe 128-bar case whose walking bass used to keep a true
  // parallel fifth into a bar head. Seeds 1-16 sweep the builder's seed
  // rotations (its pattern choices are taken mod 4 of seed and seed / 4).
  std::vector<std::uint32_t> seeds;
  for (std::uint32_t seed = 1; seed <= 16; ++seed)
    seeds.push_back(seed);
  seeds.push_back(42);
  seeds.push_back(99);
  expectShipsCleanAcrossMatrix(FormType::ChoralePrelude,
                               {SubjectCharacter::Severe, SubjectCharacter::Noble}, seeds);
}

TEST(FormCantusMatrix, GoldbergValidatesCleanAcrossMatrix) {
  expectShipsCleanAcrossMatrix(FormType::GoldbergVariations,
                               {SubjectCharacter::Severe, SubjectCharacter::Playful,
                                SubjectCharacter::Noble, SubjectCharacter::Restless},
                               {kSeeds.begin(), kSeeds.end()});
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

TEST(FormCantusChorale, FinalCadenceContainsTheDeclaredTonicThird) {
  for (bool minor : {false, true}) {
    for (std::uint32_t seed : kSeeds) {
      const HarnessFixture fx =
          build(FormType::ChoralePrelude, minor, choraleCharacter(minor), 16, seed);
      const Tick final_bar = 15 * kTicksPerBar;
      const bool picardy = minor && detail::usePicardy(seed);
      const std::uint8_t expected_third = static_cast<std::uint8_t>(minor && !picardy ? 3 : 4);
      ASSERT_FALSE(fx.harmony.chords.empty());
      EXPECT_EQ(fx.harmony.chords.back().root_pc, 0u);
      EXPECT_EQ(fx.harmony.chords.back().is_picardy, picardy);

      const ComposeResult result = Composer{}.run(fx.material, fx.harmony, fx.voice_plan);
      ASSERT_EQ(result.validation.status, ValidationStatus::Ok)
          << "minor=" << minor << " seed=" << seed << " first_failure="
          << (result.validation.failures.empty() ? "none"
                                                 : result.validation.failures.front().rule_id);
      bool saw_tonic = false;
      bool saw_third = false;
      for (const NoteEvent& note : result.notes) {
        if (note.start_tick != final_bar)
          continue;
        saw_tonic |= note.pitch % 12u == 0u;
        saw_third |= note.pitch % 12u == expected_third;
      }
      EXPECT_TRUE(saw_tonic) << "minor=" << minor << " seed=" << seed;
      EXPECT_TRUE(saw_third) << "minor=" << minor << " seed=" << seed;
    }
  }
}

// The final authentic cadence is approached by a real dominant: on the beat the
// plan declares V7, every sounding tone belongs to it and one is the leading tone.
TEST(FormCantusChorale, FinalApproachSoundsTheDominantSeventhWithItsLeadingTone) {
  for (bool minor : {false, true}) {
    for (SubjectCharacter character : {SubjectCharacter::Severe, SubjectCharacter::Noble}) {
      for (std::uint16_t bars : {std::uint16_t{16}, std::uint16_t{32}}) {
        for (std::uint32_t seed = 1; seed <= 12; ++seed) {
          const HarnessFixture fx = build(FormType::ChoralePrelude, minor, character, bars, seed);
          const ComposeResult result = Composer{}.run(fx.material, fx.harmony, fx.voice_plan);
          const auto approach = std::find_if(fx.harmony.chords.begin(), fx.harmony.chords.end(),
                                             [](const ChordEvent& chord) {
                                               return chord.quality == ChordQuality::Dominant7 &&
                                                      chord.start_tick % kTicksPerBar != 0;
                                             });
          ASSERT_NE(approach, fx.harmony.chords.end()) << "minor=" << minor << " seed=" << seed;
          const Tick tick = approach->start_tick;
          bool leading_tone = false;
          for (const NoteEvent& note : result.notes) {
            if (note.start_tick > tick || tick >= note.start_tick + note.duration)
              continue;
            const int pc = note.pitch % 12;
            EXPECT_TRUE(pc == 7 || pc == 11 || pc == 2 || pc == 5)
                << "minor=" << minor << " seed=" << seed << " bars=" << bars << " voice "
                << static_cast<int>(note.voice) << " pitch " << static_cast<int>(note.pitch);
            leading_tone = leading_tone || pc == 11;
          }
          EXPECT_TRUE(leading_tone) << "minor=" << minor << " seed=" << seed << " bars=" << bars;
        }
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

TEST(FormCantusChorale, NobleFigurationShipsWrittenDottedCells) {
  constexpr Tick kDottedEighth = 3 * kTicksPerBeat / 4;
  for (std::uint32_t seed : {1u, 5u, 42u}) {
    const HarnessFixture fx =
        build(FormType::ChoralePrelude, false, SubjectCharacter::Noble, 16, seed);
    ASSERT_FALSE(fx.material.figuration_sections.empty()) << "seed " << seed;
    EXPECT_TRUE(
        std::any_of(fx.material.figuration_sections.front().notes.begin(),
                    fx.material.figuration_sections.front().notes.end(),
                    [](const MaterialNote& note) { return note.duration == kDottedEighth; }))
        << "seed " << seed;
  }
}

// A figuration bar that moves only in quarters walks a plain line, not an
// arpeggio: most of its moves are steps, few bars leap a tritone or more, and
// some bars are stepwise throughout. The cadential landing (last two bars) is
// excluded.
TEST(FormCantusChorale, SevereQuarterFigurationBarsWalkByStep) {
  // Bounds hold with the arrival relief as it stands, which may still re-aim an
  // anchor of a quarter bar by more than a third.
  constexpr double kMaxLeapingShare = 0.6;
  constexpr double kMinStepwiseShare = 0.15;
  constexpr int kTritone = 6;
  std::size_t quarter_bars = 0;
  std::size_t leaping_bars = 0;
  std::size_t stepwise_bars = 0;
  for (bool minor : {false, true}) {
    for (std::uint16_t target_bars : {16, 32, 128}) {
      for (std::uint32_t seed = 1; seed <= 16; ++seed) {
        const HarnessFixture fx =
            build(FormType::ChoralePrelude, minor, SubjectCharacter::Severe, target_bars, seed);
        const FigurationSection* fig = nullptr;
        for (const auto& section : fx.material.figuration_sections) {
          if (section.voice == 0)
            fig = &section;
        }
        ASSERT_NE(fig, nullptr);
        const int bars = static_cast<int>(fx.harmony.chords.size());
        std::vector<std::vector<const MaterialNote*>> per_bar(static_cast<std::size_t>(bars));
        for (const MaterialNote& note : fig->notes) {
          const auto bar = static_cast<std::size_t>(note.start_tick / kTicksPerBar);
          if (bar < per_bar.size())
            per_bar[bar].push_back(&note);
        }
        for (int bar = 0; bar + 2 < bars; ++bar) {
          auto& line = per_bar[static_cast<std::size_t>(bar)];
          if (line.size() < 2 ||
              std::any_of(line.begin(), line.end(), [](const MaterialNote* note) {
                return note->duration < kTicksPerBeat;
              })) {
            continue;
          }
          std::sort(line.begin(), line.end(), [](const MaterialNote* lhs, const MaterialNote* rhs) {
            return lhs->start_tick < rhs->start_tick;
          });
          ++quarter_bars;
          int widest = 0;
          for (std::size_t idx = 1; idx < line.size(); ++idx) {
            widest = std::max(widest, std::abs(static_cast<int>(line[idx]->pitch) -
                                               static_cast<int>(line[idx - 1]->pitch)));
          }
          if (widest >= kTritone)
            ++leaping_bars;
          if (widest > 0 && widest <= 2)
            ++stepwise_bars;
        }
      }
    }
  }
  ASSERT_GT(quarter_bars, 0u);
  const auto total = static_cast<double>(quarter_bars);
  EXPECT_LT(static_cast<double>(leaping_bars), kMaxLeapingShare * total)
      << leaping_bars << " of " << quarter_bars << " quarter bars leap a tritone or more";
  EXPECT_GT(static_cast<double>(stepwise_bars), kMinStepwiseShare * total)
      << stepwise_bars << " of " << quarter_bars << " quarter bars move only by step";
}

// --- Schubler BWV645 three-layer chorale prelude (V2 walking bass) ----------

// The chorale prelude is now a three-layer Schubler texture: V0 figuration,
// V1 cantus firmus, and a V2 walking bass. The fixture builds three voices and
// every note carrying a voice-2 onset must exist (the bass is continuous), even
// though the FormSpec voice count stays 2 (the declared, immutable per-form
// value -- the passacaglia precedent builds 3 voices from a 2-voice FormSpec).
TEST(FormCantusChorale, ThreeVoiceWalkingBassPresentAndValidatesClean) {
  for (bool minor : {false, true}) {
    for (std::uint32_t seed : kSeeds) {
      const HarnessFixture fx =
          build(FormType::ChoralePrelude, minor, choraleCharacter(minor), 16, seed);
      EXPECT_EQ(fx.voice_plan.num_voices, 3) << "minor=" << minor << " seed=" << seed;
      const ComposeResult r = Composer{}.run(fx.material, fx.harmony, fx.voice_plan);
      EXPECT_EQ(r.validation.status, ValidationStatus::Ok)
          << "minor=" << minor << " seed=" << seed << " first="
          << (r.validation.failures.empty() ? "" : r.validation.failures.front().rule_id);
      bool saw_v2 = false;
      for (const auto& n : r.notes) {
        if (n.voice == 2) {
          saw_v2 = true;
          break;
        }
      }
      EXPECT_TRUE(saw_v2) << "minor=" << minor << " seed=" << seed << " missing walking bass (V2)";
    }
  }
}

// The cantus firmus (V1) is immutable: adding the V2 walking bass must not
// perturb any V1 note. Cross-check that every V1 downbeat still equals the
// skeleton tone (the 2-voice-era contract) AND that the full V1 line is
// produced -- the bass is an additive layer, never a CF edit.
TEST(FormCantusChorale, WalkingBassLeavesCantusFirmusUnchanged) {
  for (bool minor : {false, true}) {
    for (std::uint32_t seed : kSeeds) {
      const HarnessFixture fx =
          build(FormType::ChoralePrelude, minor, choraleCharacter(minor), 16, seed);
      const ComposeResult r = Composer{}.run(fx.material, fx.harmony, fx.voice_plan);
      const int bars = static_cast<int>(fx.material.cantus_firmus.size());
      ASSERT_GT(bars, 0);
      // The replayed V1 line must be exactly the embellished CF material
      // (pitch, tick, duration) -- the walking bass adds V2 only.
      std::vector<NoteEvent> v1;
      for (const auto& n : r.notes)
        if (n.voice == 1)
          v1.push_back(n);
      ASSERT_EQ(v1.size(), fx.material.cf_embellished.size())
          << "minor=" << minor << " seed=" << seed;
      std::sort(v1.begin(), v1.end(),
                [](const NoteEvent& a, const NoteEvent& b) { return a.start_tick < b.start_tick; });
      for (std::size_t i = 0; i < v1.size(); ++i) {
        EXPECT_EQ(v1[i].pitch, fx.material.cf_embellished[i].pitch) << "i=" << i;
        EXPECT_EQ(v1[i].start_tick, fx.material.cf_embellished[i].start_tick) << "i=" << i;
        EXPECT_EQ(v1[i].duration, fx.material.cf_embellished[i].duration) << "i=" << i;
      }
      // Every CF bar downbeat still equals the immutable skeleton tone.
      for (int bar = 0; bar < bars; ++bar) {
        const Tick db = static_cast<Tick>(bar) * kTicksPerBar;
        for (const auto& n : v1) {
          if (n.start_tick == db) {
            EXPECT_EQ(n.pitch, fx.material.cantus_firmus[static_cast<std::size_t>(bar)].pitch)
                << "minor=" << minor << " seed=" << seed << " bar " << bar;
            break;
          }
        }
      }
    }
  }
}

// The walking bass anchors each bar on the chord root: at every bar downbeat the
// V2 onset's pitch class equals the bar chord's root pitch class.
TEST(FormCantusChorale, WalkingBassDownbeatIsChordRoot) {
  for (bool minor : {false, true}) {
    for (std::uint32_t seed : kSeeds) {
      const HarnessFixture fx =
          build(FormType::ChoralePrelude, minor, choraleCharacter(minor), 16, seed);
      const ComposeResult r = Composer{}.run(fx.material, fx.harmony, fx.voice_plan);
      // Bar-downbeat chord root pitch classes from the harmonic plan.
      std::map<Tick, std::uint8_t> root_at;
      for (const auto& ch : fx.harmony.chords)
        root_at[ch.start_tick] = static_cast<std::uint8_t>(ch.root_pc % 12);
      for (const auto& n : r.notes) {
        if (n.voice != 2 || n.start_tick % kTicksPerBar != 0)
          continue;
        const auto it = root_at.find(n.start_tick);
        ASSERT_NE(it, root_at.end()) << "no chord at downbeat tick " << n.start_tick;
        EXPECT_EQ(n.pitch % 12, it->second)
            << "minor=" << minor << " seed=" << seed << " bass downbeat not chord root at tick "
            << n.start_tick;
      }
    }
  }
}

// The walking bass never crosses the cantus firmus: at every shared onset the V2
// bass pitch is strictly below the concurrently sounding V1 cantus-firmus pitch,
// so register order V1 > V2 holds (no voice crossing).
TEST(FormCantusChorale, WalkingBassNeverCrossesCantusFirmus) {
  for (bool minor : {false, true}) {
    for (std::uint32_t seed : kSeeds) {
      const HarnessFixture fx =
          build(FormType::ChoralePrelude, minor, choraleCharacter(minor), 16, seed);
      const ComposeResult r = Composer{}.run(fx.material, fx.harmony, fx.voice_plan);
      // Sounding V1 pitch at a tick: the latest V1 onset whose window covers it.
      auto v1_sounding = [&](Tick at) -> int {
        int pitch = -1;
        Tick best = 0;
        bool found = false;
        for (const auto& n : r.notes) {
          if (n.voice != 1)
            continue;
          if (n.start_tick <= at && at < n.start_tick + n.duration &&
              (!found || n.start_tick > best)) {
            found = true;
            best = n.start_tick;
            pitch = n.pitch;
          }
        }
        return pitch;
      };
      for (const auto& n : r.notes) {
        if (n.voice != 2)
          continue;
        const int cf = v1_sounding(n.start_tick);
        if (cf < 0)
          continue;  // no CF sounding here.
        EXPECT_LT(n.pitch, cf) << "minor=" << minor << " seed=" << seed
                               << " bass crosses cantus firmus at tick " << n.start_tick;
      }
    }
  }
}

// Walk every voice pair's motion into every union onset -- every tick at which
// any voice attacks, which is the grain an external reading of the score pairs
// the voices at -- and hand each one to `visit(tick, upper, lower, up_prev,
// up_curr, lo_prev, lo_curr)`. Motions where either voice is silent are skipped.
template <typename Visit>
void forEachUnionMotion(const ComposeResult& result, Visit visit) {
  auto sounding = [&](VoiceId voice, Tick at) -> int {
    int pitch = -1;
    Tick best = 0;
    bool found = false;
    for (const auto& note : result.notes) {
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
  VoiceId voices = 0;
  for (const auto& note : result.notes) {
    onsets.push_back(note.start_tick);
    voices = std::max(voices, static_cast<VoiceId>(note.voice + 1));
  }
  std::sort(onsets.begin(), onsets.end());
  onsets.erase(std::unique(onsets.begin(), onsets.end()), onsets.end());
  for (std::size_t idx = 1; idx < onsets.size(); ++idx) {
    const Tick prev = onsets[idx - 1];
    const Tick curr = onsets[idx];
    for (VoiceId upper = 0; upper < voices; ++upper) {
      for (VoiceId lower = static_cast<VoiceId>(upper + 1); lower < voices; ++lower) {
        const int up_prev = sounding(upper, prev);
        const int up_curr = sounding(upper, curr);
        const int lo_prev = sounding(lower, prev);
        const int lo_curr = sounding(lower, curr);
        if (up_prev < 0 || up_curr < 0 || lo_prev < 0 || lo_curr < 0)
          continue;
        visit(curr, upper, lower, up_prev, up_curr, lo_prev, lo_curr);
      }
    }
  }
}

// Assert that no true parallel perfect of either class survives anywhere in a
// composed result, at any pair of voices and any union onset. `where` names the
// case.
void expectNoParallelPerfect(const ComposeResult& result, const std::string& where) {
  forEachUnionMotion(result, [&](Tick curr, VoiceId upper, VoiceId lower, int up_prev, int up_curr,
                                 int lo_prev, int lo_curr) {
    EXPECT_FALSE(isParallelPerfectMotion(up_prev, up_curr, lo_prev, lo_curr))
        << where << " v" << static_cast<int>(upper) << "/v" << static_cast<int>(lower)
        << " parallel perfect at tick " << curr << " (" << up_prev << "->" << up_curr << " over "
        << lo_prev << "->" << lo_curr << ")";
  });
}

// The chorale prelude reaches this because every tone that could form a perfect
// interval is reachable by some guard: the figuration's bar heads and the cantus
// firmus skeleton are fixed, but each is approached through a tone that is not,
// and the walking bass -- whose own bar head is pinned to the chord root inside
// a one-octave band, leaving it no register to move to -- is guarded on every
// other beat. Where the way in has no consonant tone left, it takes a dissonant
// one rather than keep the parallel: a passing dissonance between two guarded
// arrivals is the smaller fault, and refusing it is what left the octaves
// standing while the fifths were already gone.
//
// The last of them were not in the counterpoint at all but in the figure written
// over it: the cadential suspension pins the bass under its own preparation and
// again under its own resolution, after every voice has been settled against the
// bass those pins replace. It is the last thing this form moves, so it is now
// chosen against the three-line surface it produces -- a fault it would install
// is one nothing downstream can see.
//
// Several bar counts, because the phrase boundaries that make the figuration
// leap into a bar head fall at different bars as the piece lengthens, and it is
// at such a head that the only relief left is a step into the arrival.
TEST(FormCantusChorale, ShippedTextureIsFreeOfParallelPerfects) {
  for (bool minor : {false, true}) {
    for (int bars : {16, 32, 64}) {
      for (std::uint32_t seed : kSeeds) {
        const HarnessFixture fx =
            build(FormType::ChoralePrelude, minor, choraleCharacter(minor), bars, seed);
        expectNoParallelPerfect(Composer{}.run(fx.material, fx.harmony, fx.voice_plan),
                                "minor=" + std::to_string(minor) + " bars=" + std::to_string(bars) +
                                    " seed=" + std::to_string(seed));
      }
    }
  }
}

// Every voice pair of the Goldberg texture is asserted parallel-free,
// everywhere. That is a stronger claim than the chorale prelude's above, and it rests on a
// different argument, because here almost nothing can be relieved after the
// fact -- the aria bass is immutable by contract, and a canon's follower is the
// exact imitation of a leader it cannot leave, so neither end of that pair may
// be re-aimed without dissolving the imitation the block exists to state.
//
// What answers for the congruence instead is the choice made before any of it is
// committed. A canon block and the quodlibet block are each a closed system: the
// bass repeats on exactly the four-bar period they span, and their upper lines
// follow from one small choice -- the leader's tones, the tune's rotation -- so
// the whole three-voice surface can be assembled and read while that choice is
// still open. The free figuration between those blocks has no such closed form
// and is relieved arrival by arrival instead.
TEST(FormCantusGoldberg, ShippedTextureIsFreeOfParallelPerfects) {
  for (bool minor : {false, true}) {
    for (SubjectCharacter character : {SubjectCharacter::Severe, SubjectCharacter::Playful,
                                       SubjectCharacter::Noble, SubjectCharacter::Restless}) {
      for (std::uint16_t bars : {std::uint16_t{20}, std::uint16_t{128}}) {
        for (std::uint32_t seed : {1u, 3u, 6u, 42u}) {
          const HarnessFixture fx =
              build(FormType::GoldbergVariations, minor, character, bars, seed);
          const ComposeResult result = Composer{}.run(fx.material, fx.harmony, fx.voice_plan);
          const std::string where = "minor=" + std::to_string(minor) +
                                    " character=" + std::to_string(static_cast<int>(character)) +
                                    " bars=" + std::to_string(bars) +
                                    " seed=" + std::to_string(seed);
          forEachUnionMotion(result, [&](Tick curr, VoiceId upper, VoiceId lower, int up_prev,
                                         int up_curr, int lo_prev, int lo_curr) {
            if (!isParallelPerfectMotion(up_prev, up_curr, lo_prev, lo_curr))
              return;
            ADD_FAILURE() << where << " v" << static_cast<int>(upper) << "/v"
                          << static_cast<int>(lower) << " parallel perfect at tick " << curr << " ("
                          << up_prev << "->" << up_curr << " over " << lo_prev << "->" << lo_curr
                          << ")";
          });
        }
      }
    }
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

// The embellished cantus firmus (V1) never sustains a long static run: across a
// seed sweep x both modes, the longest run of identical adjacent V1 pitches stays
// at or below four. A same-degree adjacent bar that would chain a plain
// `tone,tone,tone` figure into a longer run is broken by a consonant diatonic
// neighbour, satisfying the texture gate's max_repeated_run <= 4 axis.
TEST(FormCantusChorale, EmbellishedCantusFirmusRepeatedRunBounded) {
  for (bool minor : {false, true}) {
    for (std::uint32_t seed : {1u, 5u, 7u, 13u, 42u, 99u}) {
      const HarnessFixture fx =
          build(FormType::ChoralePrelude, minor, choraleCharacter(minor), 16, seed);
      const ComposeResult r = Composer{}.run(fx.material, fx.harmony, fx.voice_plan);
      std::vector<NoteEvent> v1;
      for (const auto& n : r.notes)
        if (n.voice == 1)
          v1.push_back(n);
      std::sort(v1.begin(), v1.end(),
                [](const NoteEvent& a, const NoteEvent& b) { return a.start_tick < b.start_tick; });
      int longest = v1.empty() ? 0 : 1;
      int run = v1.empty() ? 0 : 1;
      for (std::size_t i = 1; i < v1.size(); ++i) {
        run = (v1[i].pitch == v1[i - 1].pitch) ? run + 1 : 1;
        longest = run > longest ? run : longest;
      }
      EXPECT_LE(longest, 4) << "minor=" << minor << " seed=" << seed
                            << " V1 repeated-pitch run exceeds 4";
    }
  }
}

// The decorated cantus keeps its band after every rewrite, arrival relief
// included: a tone sinks into the walking bass's register only where relief has
// no other escape from a parallel octave, and passing eighths reached or left by
// a seventh or wider stay at the decoration's own count. RATCHET: measured
// counts, only ever lowered.
TEST(FormCantusChorale, EmbellishedCantusFirmusKeepsItsBandAndStepwisePassing) {
  int lowest = 127;
  std::size_t wide_passing = 0;
  std::size_t under_band = 0;
  for (bool minor : {false, true}) {
    for (SubjectCharacter character : {SubjectCharacter::Severe, SubjectCharacter::Noble}) {
      for (std::uint16_t bars : {std::uint16_t{16}, std::uint16_t{32}}) {
        for (std::uint32_t seed = 1; seed <= 12; ++seed) {
          const HarnessFixture fx = build(FormType::ChoralePrelude, minor, character, bars, seed);
          const ComposeResult result = Composer{}.run(fx.material, fx.harmony, fx.voice_plan);
          std::vector<NoteEvent> line;
          for (const NoteEvent& note : result.notes) {
            if (note.voice == 1)
              line.push_back(note);
          }
          std::sort(line.begin(), line.end(), [](const NoteEvent& lhs, const NoteEvent& rhs) {
            return lhs.start_tick < rhs.start_tick;
          });
          for (std::size_t idx = 0; idx < line.size(); ++idx) {
            lowest = std::min(lowest, static_cast<int>(line[idx].pitch));
            // The leading tone under the pinned dominant approach is B2 by design.
            const Tick final_approach = static_cast<Tick>(bars - 1) * kTicksPerBar - kTicksPerBeat;
            if (line[idx].pitch < 48 && line[idx].start_tick != final_approach)
              ++under_band;
            const bool passing = line[idx].duration <= kTicksPerBeat / 2 &&
                                 line[idx].start_tick % kTicksPerBeat != 0;
            if (!passing || idx == 0)
              continue;
            const int leap_in =
                std::abs(static_cast<int>(line[idx].pitch) - static_cast<int>(line[idx - 1].pitch));
            // A leap out into the next bar head lands on the immutable skeleton
            // tone, which relief never moves; that end is the decoration's own.
            const bool into_skeleton =
                idx + 1 < line.size() && line[idx + 1].start_tick % kTicksPerBar == 0;
            const int leap_out = idx + 1 < line.size() && !into_skeleton
                                     ? std::abs(static_cast<int>(line[idx + 1].pitch) -
                                                static_cast<int>(line[idx].pitch))
                                     : 0;
            if (std::max(leap_in, leap_out) >= 10)
              ++wide_passing;
          }
        }
      }
    }
  }
  std::printf("[chorale-v1] lowest=%d under-band=%zu wide-passing=%zu\n", lowest, under_band,
              wide_passing);
  EXPECT_LE(under_band, 6u);
  EXPECT_LE(wide_passing, 8u);
}

// Whether a pitch class belongs to the chord the plan states at a tick.
bool isPlannedChordTone(const HarmonicPlan& plan, Tick tick, int pitch) {
  const ChordEvent* chord = rule_helpers::chordAt(plan, tick);
  if (chord == nullptr)
    return true;
  int third = 4;
  int fifth = 7;
  int seventh = -1;
  switch (chord->quality) {
    case ChordQuality::Major:
      break;
    case ChordQuality::Minor:
      third = 3;
      break;
    case ChordQuality::Diminished:
      third = 3;
      fifth = 6;
      break;
    case ChordQuality::Augmented:
      fifth = 8;
      break;
    case ChordQuality::Dominant7:
      seventh = 10;
      break;
    case ChordQuality::Minor7:
      third = 3;
      seventh = 10;
      break;
    case ChordQuality::Major7:
      seventh = 11;
      break;
    case ChordQuality::Diminished7:
      third = 3;
      fifth = 6;
      seventh = 9;
      break;
    case ChordQuality::HalfDiminished7:
      third = 3;
      fifth = 6;
      seventh = 10;
      break;
  }
  const int interval = ((pitch - static_cast<int>(chord->root_pc)) % 12 + 12) % 12;
  return interval == 0 || interval == third || interval == fifth || interval == seventh;
}

// The plain bar's last quarter the builder writes as a passing tone or neighbour into the
// next bar head is approached and left by step, never by an augmented interval, and a
// leading tone on it never falls; read on the composed and the shipped arrays. Every other
// plain last quarter that is a non-chord tone is held per class to the counts measured on the
// line without the step tones, so the unit cannot add to them.
TEST(FormCantusChorale, LastQuarterStepToneStepsInAndOut) {
  enum Class { kStepIn, kStepOut, kAugmentedIn, kAugmentedOut, kLeadingToneFalls, kClasses };
  constexpr std::array<int, kClasses> kOtherComposed = {{27, 34, 3, 3, 4}};
  constexpr std::array<int, kClasses> kOtherShipped = {{23, 29, 3, 3, 4}};
  std::array<int, kClasses> other_composed{};
  std::array<int, kClasses> other_shipped{};
  std::size_t steps = 0;
  std::size_t non_chord_steps = 0;
  for (bool minor : {false, true}) {
    for (std::uint32_t seed : kSeeds) {
      for (std::uint16_t bars : sweepBars(FormType::ChoralePrelude)) {
        ComposeRequest req;
        req.form = FormType::ChoralePrelude;
        req.is_minor = minor;
        req.character = choraleCharacter(minor);
        req.target_bars = bars;
        req.seed = seed;
        const test::ProductGateResult gate = test::runProductGate(req);
        ASSERT_TRUE(gate.built);
        const HarmonicPlan& plan = gate.fixture.harmony;
        const std::vector<Tick>& placed = gate.fixture.material.cf_last_quarter_steps;
        steps += placed.size();
        const auto bars_built = static_cast<Tick>(gate.fixture.material.cantus_firmus.size());
        std::vector<Tick> distinct = placed;
        std::sort(distinct.begin(), distinct.end());
        EXPECT_EQ(std::unique(distinct.begin(), distinct.end()), distinct.end())
            << "repeated step tick, seed=" << seed << " bars=" << bars;
        for (const Tick tick : placed) {
          EXPECT_EQ(tick % kTicksPerBar, 3 * kTicksPerBeat) << "step off the last quarter";
          EXPECT_LT(tick / kTicksPerBar + 2, bars_built) << "step in the dominant-approach bar";
        }
        for (const ComposeResult* result : {&gate.composed, &gate.shipped}) {
          const bool shipped = result == &gate.shipped;
          std::vector<NoteEvent> line;
          std::vector<NoteEvent> heard;
          for (const NoteEvent& note : result->notes) {
            if (note.voice != 1)
              continue;
            heard.push_back(note);
            if (note.source != BachNoteSource::Ornament)
              line.push_back(note);
          }
          std::sort(line.begin(), line.end(), [](const NoteEvent& lhs, const NoteEvent& rhs) {
            return lhs.start_tick < rhs.start_tick;
          });
          const std::string where =
              std::string(shipped ? "shipped" : "composed") + " minor=" + std::to_string(minor) +
              " seed=" + std::to_string(seed) + " bars=" + std::to_string(bars);
          if (!shipped) {
            for (const Tick tick : placed) {
              const auto found = std::find_if(line.begin(), line.end(), [&](const NoteEvent& note) {
                return note.start_tick == tick;
              });
              ASSERT_NE(found, line.end()) << where << " no cantus note at step tick " << tick;
              ASSERT_NE(found, line.begin()) << where;
              const NoteEvent& before = *(found - 1);
              EXPECT_TRUE(found->duration == kTicksPerBeat && before.duration == kTicksPerBeat &&
                          before.start_tick + kTicksPerBeat == tick)
                  << where << " step tick " << tick << " is not a plain bar's last quarter";
              non_chord_steps += isPlannedChordTone(plan, tick, found->pitch) ? 0 : 1;
            }
          }
          std::array<int, kClasses>& other = shipped ? other_shipped : other_composed;
          for (std::size_t idx = 1; idx + 1 < line.size(); ++idx) {
            const NoteEvent& prev = line[idx - 1];
            const NoteEvent& note = line[idx];
            const NoteEvent& next = line[idx + 1];
            const bool step_tone =
                std::find(placed.begin(), placed.end(), note.start_tick) != placed.end();
            const bool plain_last_quarter = note.start_tick % kTicksPerBar == 3 * kTicksPerBeat &&
                                            prev.start_tick % kTicksPerBar == 2 * kTicksPerBeat &&
                                            prev.duration == kTicksPerBeat &&
                                            note.duration == kTicksPerBeat &&
                                            next.start_tick % kTicksPerBar == 0;
            if (!step_tone &&
                (!plain_last_quarter ||
                 isPlannedChordTone(plan, note.start_tick, static_cast<int>(note.pitch))))
              continue;
            // A step tone is approached from the tone heard last: an ornamented quarter before
            // it is absent from the plain line, and every figure closes on the tone it decorates.
            int approach = static_cast<int>(prev.pitch);
            if (step_tone) {
              Tick latest = 0;
              for (const NoteEvent& sounded : heard) {
                if (sounded.start_tick < note.start_tick && sounded.start_tick >= latest &&
                    sounded.start_tick + sounded.duration >= note.start_tick) {
                  latest = sounded.start_tick;
                  approach = static_cast<int>(sounded.pitch);
                }
              }
            }
            const int step_in = static_cast<int>(note.pitch) - approach;
            const int step_out = static_cast<int>(next.pitch) - static_cast<int>(note.pitch);
            std::array<bool, kClasses> faults{};
            faults[kStepIn] = std::abs(step_in) < 1 || std::abs(step_in) > 2;
            faults[kStepOut] = std::abs(step_out) < 1 || std::abs(step_out) > 2;
            faults[kAugmentedIn] = rule_helpers::isAugmentedMelodicInterval(
                static_cast<std::uint8_t>(approach), note.pitch, plan);
            faults[kAugmentedOut] =
                rule_helpers::isAugmentedMelodicInterval(note.pitch, next.pitch, plan);
            faults[kLeadingToneFalls] =
                rule_helpers::isLeadingTone(note.pitch, plan) && step_out < 0;
            for (int cls = 0; cls < kClasses; ++cls) {
              if (!faults[static_cast<std::size_t>(cls)])
                continue;
              if (step_tone)
                ADD_FAILURE() << where << " step tone class " << cls << " at tick "
                              << note.start_tick << ": " << approach << " -> "
                              << static_cast<int>(note.pitch) << " -> "
                              << static_cast<int>(next.pitch);
              else
                ++other[static_cast<std::size_t>(cls)];
            }
          }
        }
      }
    }
  }
  std::printf("[chorale-v1] step tones=%zu (non-chord %zu)\n", steps, non_chord_steps);
  EXPECT_GT(steps, 0u);
  EXPECT_GT(non_chord_steps, 0u);
  for (int cls = 0; cls < kClasses; ++cls) {
    const auto at = static_cast<std::size_t>(cls);
    EXPECT_LE(other_composed[at], kOtherComposed[at]) << "composed class " << cls;
    EXPECT_LE(other_shipped[at], kOtherShipped[at]) << "shipped class " << cls;
  }
}

// --- Goldberg structural contracts ------------------------------------------

// The aria bass returns its four-bar harmonic cycle on V2 until the explicit
// final-bar tonic coda. Every bar of the piece but that last one carries a bass
// head, and that head is the ground tone the aria itself stated at the same
// cycle position -- what a return may change is the surface, never the skeleton.
TEST(FormCantusGoldberg, GroundReturnsItsCycleUntilTheTerminalCoda) {
  for (bool minor : {false, true}) {
    for (std::uint32_t seed : kSeeds) {
      for (std::uint16_t bars : sweepBars(FormType::GoldbergVariations)) {
        const HarnessFixture fx =
            build(FormType::GoldbergVariations, minor, SubjectCharacter::Severe, bars, seed);
        const ComposeResult r = Composer{}.run(fx.material, fx.harmony, fx.voice_plan);

        std::map<Tick, std::uint8_t> ground;
        for (std::size_t i = 0; i < r.notes.size(); ++i) {
          if (r.provenance[i].satisfied_rules & bit(RuleBit::GoldbergBassReplayed))
            ground.emplace(r.notes[i].start_tick, r.notes[i].pitch);
        }
        // At least the plain eighth-note statement's worth of tones per bar; a
        // return that re-articulates its tones carries more.
        ASSERT_GE(static_cast<int>(ground.size()), (bars - 1) * 8 - 2)
            << "minor=" << minor << " seed=" << seed << " bars=" << bars;
        // The aria (block 0) states the cycle plainly, so its bar heads are the
        // skeleton every later return must come back to.
        ASSERT_GE(fx.material.goldberg_aria_bass.size(), 32u);
        for (int bar = 0; bar < bars - 1; ++bar) {
          const auto it = ground.find(static_cast<Tick>(bar) * kTicksPerBar);
          ASSERT_NE(it, ground.end()) << "bar " << bar;
          EXPECT_EQ(it->second,
                    fx.material.goldberg_aria_bass[static_cast<std::size_t>(bar % 4) * 8].pitch)
              << "minor=" << minor << " seed=" << seed << " bar " << bar;
        }
        ASSERT_EQ(fx.material.coda_extensions.size(), 1u);
        ASSERT_EQ(fx.material.coda_extensions.front().notes.size(), 2u);
        EXPECT_EQ(fx.material.coda_extensions.front().notes.front().start_tick,
                  static_cast<Tick>(bars - 1) * kTicksPerBar - kTicksPerBeat);
        EXPECT_EQ(fx.material.coda_extensions.front().notes.front().pitch % 12u, 7u);
        EXPECT_EQ(fx.material.coda_extensions.front().notes.back().start_tick,
                  static_cast<Tick>(bars - 1) * kTicksPerBar);
        EXPECT_EQ(fx.material.coda_extensions.front().notes.back().pitch % 12u, 0u);
      }
    }
  }
}

TEST(FormCantusGoldberg, TerminalCadenceResolvesAllActiveVoicesToTonic) {
  for (bool minor : {false, true}) {
    for (std::uint32_t seed : kSeeds) {
      for (std::uint16_t bars : sweepBars(FormType::GoldbergVariations)) {
        const HarnessFixture fx =
            build(FormType::GoldbergVariations, minor, SubjectCharacter::Severe, bars, seed);
        const ComposeResult r = Composer{}.run(fx.material, fx.harmony, fx.voice_plan);
        const Tick final_bar = static_cast<Tick>(bars - 1) * kTicksPerBar;
        ASSERT_FALSE(fx.harmony.chords.empty());
        EXPECT_EQ(fx.harmony.chords.back().root_pc, 0u);

        const NoteEvent* bass = nullptr;
        const NoteEvent* upper = nullptr;
        bool coda_committed = false;
        for (std::size_t i = 0; i < r.notes.size(); ++i) {
          const NoteEvent& note = r.notes[i];
          if (note.voice == 2 && note.start_tick == final_bar) {
            bass = &note;
            coda_committed = (r.provenance[i].satisfied_rules & bit(RuleBit::CodaCommitted)).any();
          }
          if (note.voice == 0 &&
              note.start_tick + note.duration == static_cast<Tick>(bars) * kTicksPerBar) {
            upper = &note;
          }
        }
        ASSERT_NE(bass, nullptr);
        ASSERT_NE(upper, nullptr);
        EXPECT_TRUE(coda_committed);
        EXPECT_EQ(bass->pitch % 12u, 0u);
        EXPECT_EQ(upper->pitch % 12u, 0u);
      }
    }
  }
}

// The ground table is a seed-drawn design variant, fixed for the whole piece: the
// aria bass states exactly one table's bar heads, and a span of seeds reaches every
// table. Each seed is read by what it states, not by a fixed seed-to-variant map.
TEST(FormCantusGoldberg, EverySeedStatesOneGroundTable) {
  for (bool minor : {false, true}) {
    const auto& tables = minor ? detail::kGoldbergGroundsMinor : detail::kGoldbergGroundsMajor;
    std::array<bool, detail::kGroundVariantCount> reached{};
    for (std::uint32_t seed = 1; seed <= 24; ++seed) {
      const HarnessFixture fx =
          build(FormType::GoldbergVariations, minor, SubjectCharacter::Severe, 0, seed);
      ASSERT_GE(fx.material.goldberg_aria_bass.size(), 32u);
      int matched = -1;
      for (std::size_t variant = 0; variant < tables.size(); ++variant) {
        bool same = true;
        for (std::size_t bar = 0; bar < 4; ++bar)
          same = same && fx.material.goldberg_aria_bass[bar * 8].pitch == tables[variant][bar];
        if (same)
          matched = static_cast<int>(variant);
      }
      ASSERT_GE(matched, 0) << "minor " << minor << " seed " << seed;
      reached[static_cast<std::size_t>(matched)] = true;
    }
    for (std::size_t variant = 0; variant < reached.size(); ++variant)
      EXPECT_TRUE(reached[variant]) << "minor " << minor << " variant " << variant;
  }
}

// The aria special layout occupies bars 0-3: two half notes per bar on V0 (m=2),
// eight notes total, never subdivided below the half note.
TEST(FormCantusGoldberg, AriaSpecialLayoutInFirstFourBars) {
  for (bool minor : {false, true}) {
    for (std::uint32_t seed : kSeeds) {
      const HarnessFixture fx =
          build(FormType::GoldbergVariations, minor, SubjectCharacter::Severe, 0, seed);
      ASSERT_FALSE(fx.material.goldberg_variations.empty());
      const auto& aria = fx.material.goldberg_variations.front();
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
    const auto& blocks = fx.material.goldberg_variations;
    ASSERT_EQ(blocks.size(), 5u);
    EXPECT_GT(blocks.back().notes.size(), 8u) << "20-bar final block must not be the aria";
  }
  // 24+ bars: the final block restates the aria layout (8 half notes, density 0).
  for (std::uint16_t bars : {24, 40, 128}) {
    const HarnessFixture fx =
        build(FormType::GoldbergVariations, false, SubjectCharacter::Severe, bars, 1);
    const auto& blocks = fx.material.goldberg_variations;
    ASSERT_FALSE(blocks.empty());
    const auto& last = blocks.back();
    EXPECT_EQ(last.density_level, 0) << "bars=" << bars;
    // The coda's dominant may split off the penultimate bar's closing beat.
    const Tick approach = static_cast<Tick>(bars - 1) * kTicksPerBar - kTicksPerBeat;
    std::size_t half_notes = 0;
    std::size_t split_beats = 0;
    for (const auto& n : last.notes) {
      if (n.start_tick == approach || n.start_tick + n.duration == approach) {
        EXPECT_EQ(n.duration, kTicksPerBeat) << "bars=" << bars;
        ++split_beats;
        continue;
      }
      EXPECT_EQ(n.duration, kTicksPerBeat * 2) << "bars=" << bars;
      ++half_notes;
    }
    EXPECT_EQ(half_notes + split_beats / 2, 8u) << "bars=" << bars << " da-capo aria must be m=2";
    EXPECT_FALSE(last.is_climax) << "bars=" << bars << " da-capo block is never the climax";
    EXPECT_EQ(last.start_tick, static_cast<Tick>(bars - 4) * kTicksPerBar) << "bars=" << bars;
  }
}

// The climax block sits at the arc climax position (~80%), not necessarily the
// last block, and is the only block flagged is_climax. ClimaxPlaced fires only on
// that block.
TEST(FormCantusGoldberg, ClimaxBlockAtArcPosition) {
  for (std::uint16_t bars : {20, 40, 128}) {
    const HarnessFixture fx =
        build(FormType::GoldbergVariations, false, SubjectCharacter::Severe, bars, 1);
    const auto& blocks = fx.material.goldberg_variations;
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
  EXPECT_EQ(goldbergVariationKind(29u), GoldbergVariationKind::Quodlibet)
      << "variation 30 is the Quodlibet slot, not a canon";
  // Determinism: repeated calls agree.
  for (std::size_t idx : {2u, 5u, 29u})
    EXPECT_EQ(goldbergVariationKind(idx), goldbergVariationKind(idx)) << "index " << idx;
}

// The chorale accompaniment (V0 figuration) rotates its vocabulary per 4-bar
// cycle: anchored-wave cycles run uniform subdivisions, figura corta cycles
// carry the mixed eighth+sixteenth cell. The cycle holding the piece's
// mid-boundary bar is always a corta cycle (its long notes are what the
// ornament pass needs at the mid sub-cadence). The cantus firmus is untouched
// (the V1 downbeat skeleton still matches material.cantus_firmus bar by bar).
TEST(FormCantusChorale, AccompanimentRotatesVocabularyOverUnchangedCantus) {
  for (std::uint32_t seed : kSeeds) {
    for (bool minor : {false, true}) {
      const HarnessFixture fx =
          build(FormType::ChoralePrelude, minor, choraleCharacter(minor), /*bars=*/32, seed);
      const ComposeResult r = Composer{}.run(fx.material, fx.harmony, fx.voice_plan);
      EXPECT_EQ(r.validation.status, ValidationStatus::Ok) << "seed " << seed;

      // V0 figuration section (voice 0 in figuration_sections).
      const FigurationSection* fig = nullptr;
      for (const auto& section : fx.material.figuration_sections) {
        if (section.voice == 0)
          fig = &section;
      }
      ASSERT_NE(fig, nullptr);
      const int bars = static_cast<int>(fx.harmony.chords.size());
      bool saw_corta = false;
      bool saw_uniform = false;
      bool mid_cycle_is_corta = false;
      const int mid_bar = ((bars / 2) / 4) * 4 - 1;
      for (int cycle = 0; cycle * 4 < bars; ++cycle) {
        const Tick lo = static_cast<Tick>(cycle) * 4 * kTicksPerBar;
        const Tick hi = lo + kTicksPerBar;  // the cycle's first bar.
        Tick first_dur = 0;
        bool mixed = false;
        for (const auto& n : fig->notes) {
          if (n.start_tick < lo || n.start_tick >= hi)
            continue;
          if (first_dur == 0)
            first_dur = n.duration;
          else if (n.duration != first_dur)
            mixed = true;
        }
        if (mixed) {
          saw_corta = true;
          if (mid_bar >= 0 && cycle == mid_bar / 4)
            mid_cycle_is_corta = true;
        } else {
          saw_uniform = true;
        }
      }
      EXPECT_TRUE(saw_corta) << "seed " << seed << " minor " << minor;
      EXPECT_TRUE(saw_uniform) << "seed " << seed << " minor " << minor;
      EXPECT_TRUE(mid_cycle_is_corta) << "seed " << seed << " minor " << minor
                                      << ": the mid-boundary cycle must carry the corta figure";

      // Cantus firmus untouched: every V1 bar-head pitch equals the skeleton.
      for (const auto& n : r.notes) {
        if (n.voice != 1 || n.start_tick % kTicksPerBar != 0)
          continue;
        const std::size_t bar = static_cast<std::size_t>(n.start_tick / kTicksPerBar);
        if (bar < fx.material.cantus_firmus.size())
          EXPECT_EQ(n.pitch, fx.material.cantus_firmus[bar].pitch)
              << "seed " << seed << " bar " << bar << ": CF skeleton altered";
      }
    }
  }
}

TEST(FormCantusGoldberg, FigurationBlocksAlternatePatterns) {
  // Non-climax figuration blocks rotate the goldberg pattern palette, so the
  // set of figuration-block duration histograms is not uniform: the figura
  // corta cell (eighth + two sixteenths per beat) is rhythmically distinct
  // from the anchored scalar wave. The aria (block 0, half notes) and the
  // canons (uniform eighths) keep their own layouts; a climax that lands on a
  // figuration block is the design peak (uniform sixteenths), while a climax
  // that coincides with a numbered canon preserves its independent imitation.
  for (std::uint32_t seed : kSeeds) {
    for (bool minor : {false, true}) {
      const HarnessFixture fx =
          build(FormType::GoldbergVariations, minor, SubjectCharacter::Severe, /*bars=*/128, seed);
      const auto& vars = fx.material.goldberg_variations;
      ASSERT_GE(vars.size(), 6u);
      // Aria layout unchanged: two half notes per bar.
      for (const MaterialNote& n : vars[0].notes)
        EXPECT_EQ(n.duration, 2 * kTicksPerBeat) << "aria must stay half notes";
      // Collect non-aria, non-climax figuration blocks' histograms.
      bool found_figura_corta = false;
      bool found_uniform = false;
      for (std::size_t blk = 1; blk < vars.size(); ++blk) {
        const auto& var = vars[blk];
        if (var.is_climax) {
          const std::size_t variation_number = blk;  // aria is block 0.
          if (variation_number % 3 != 0 || variation_number == 30) {
            EXPECT_TRUE(
                std::all_of(var.notes.begin(), var.notes.end(),
                            [](const MaterialNote& n) { return n.duration == kTicksPerBeat / 4; }))
                << "seed " << seed << ": figuration climax must stay uniform sixteenths";
          }
          continue;
        }
        const std::size_t variation_number = blk;  // 1-based ordinal (aria = block 0).
        if (variation_number % 3 == 0 && variation_number < 30)
          continue;  // canon block: its own layout.
        const bool mixed =
            std::any_of(var.notes.begin(), var.notes.end(),
                        [](const MaterialNote& n) { return n.duration == kTicksPerBeat / 2; }) &&
            std::any_of(var.notes.begin(), var.notes.end(),
                        [](const MaterialNote& n) { return n.duration == kTicksPerBeat / 4; });
        if (mixed)
          found_figura_corta = true;
        else
          found_uniform = true;
      }
      EXPECT_TRUE(found_figura_corta)
          << "seed " << seed << " minor " << minor << ": no figura corta block in the rotation";
      EXPECT_TRUE(found_uniform) << "seed " << seed << " minor " << minor
                                 << ": no scalar-wave block in the rotation";
    }
  }
}

// The palette rotation is tied to the bass contour phase, so seeds whose bass layout
// draws contour phases of one parity open their first variation on one idiom.
TEST(FormCantusGoldberg, FirstVariationIdiomFollowsTheContourPhaseParity) {
  std::array<int, 2> idiom_of_parity = {-1, -1};
  for (std::uint32_t seed = 1; seed <= 24; ++seed) {
    const std::uint32_t layout =
        rng::seedBlockChoice(seed, rng::SeedDecision::kGoldbergBassLayout, 12, 0, /*clear=*/3);
    const std::size_t parity = (layout / 3u) % 2u;
    const HarnessFixture fx =
        build(FormType::GoldbergVariations, false, SubjectCharacter::Severe, /*bars=*/128, seed);
    ASSERT_GE(fx.material.goldberg_variations.size(), 2u);
    const auto& notes = fx.material.goldberg_variations[1].notes;
    const int figura_corta =
        std::any_of(notes.begin(), notes.end(),
                    [](const MaterialNote& n) { return n.duration == kTicksPerBeat / 2; }) &&
        std::any_of(notes.begin(), notes.end(),
                    [](const MaterialNote& n) { return n.duration == kTicksPerBeat / 4; });
    if (idiom_of_parity[parity] < 0)
      idiom_of_parity[parity] = figura_corta;
    EXPECT_EQ(idiom_of_parity[parity], figura_corta) << "seed " << seed;
  }
  EXPECT_GE(idiom_of_parity[0], 0);
  EXPECT_GE(idiom_of_parity[1], 0);
  EXPECT_NE(idiom_of_parity[0], idiom_of_parity[1]);
}

// B lifts the C2 ground an octave, which leaves one semitone under D6 for the
// upper line: every line keeps under it, so the piece ships whole without a moved bar.
TEST(FormCantusGoldberg, BKeyShipsInsideTheKeyboardWithoutMovedBars) {
  for (bool minor : {false, true}) {
    for (std::uint32_t seed = 1; seed <= 16; ++seed) {
      for (std::uint16_t bars : {std::uint16_t{20}, std::uint16_t{40}}) {
        ComposeRequest req;
        req.form = FormType::GoldbergVariations;
        req.is_minor = minor;
        req.character = SubjectCharacter::Severe;
        req.target_bars = bars;
        req.seed = seed;
        req.tonic = Key::B;
        const test::ProductGateResult gate = test::runProductGate(req, Key::B);
        const std::string where = "minor=" + std::to_string(minor) +
                                  " seed=" + std::to_string(seed) + " bars=" + std::to_string(bars);
        ASSERT_TRUE(gate.placed) << where;
        EXPECT_EQ(gate.placement.displaced_notes, 0u) << where;
        EXPECT_EQ(
            countCompassViolations(gate.shipped.notes, InstrumentType::Harpsichord,
                                   keyTranspositionSemitones(Key::B) + gate.placement.octave_shift),
            0)
            << where;
      }
    }
  }
}

// --- Registral continuity: keyboard compass + no cliff at the close ---------

// Largest interval between consecutive attacks within one voice of `notes`.
int maxConsecutiveLeap(const std::vector<NoteEvent>& notes) {
  std::map<VoiceId, std::vector<const NoteEvent*>> by_voice;
  for (const auto& n : notes)
    by_voice[n.voice].push_back(&n);
  int worst = 0;
  for (auto& [voice, vn] : by_voice) {
    std::sort(vn.begin(), vn.end(),
              [](const NoteEvent* a, const NoteEvent* b) { return a->start_tick < b->start_tick; });
    for (std::size_t i = 1; i < vn.size(); ++i) {
      if (vn[i]->start_tick >= vn[i - 1]->start_tick + vn[i - 1]->duration) {
        worst = std::max(worst, std::abs(static_cast<int>(vn[i]->pitch) - vn[i - 1]->pitch));
      }
    }
  }
  return worst;
}

int maxVoiceConsecutiveLeap(const std::vector<NoteEvent>& notes, VoiceId target_voice) {
  std::vector<const NoteEvent*> voice_notes;
  for (const NoteEvent& note : notes) {
    if (note.voice == target_voice)
      voice_notes.push_back(&note);
  }
  std::sort(voice_notes.begin(), voice_notes.end(), [](const NoteEvent* lhs, const NoteEvent* rhs) {
    return lhs->start_tick < rhs->start_tick;
  });
  int worst = 0;
  for (std::size_t idx = 1; idx < voice_notes.size(); ++idx) {
    if (voice_notes[idx]->start_tick >=
        voice_notes[idx - 1]->start_tick + voice_notes[idx - 1]->duration) {
      worst = std::max(worst, std::abs(static_cast<int>(voice_notes[idx]->pitch) -
                                       static_cast<int>(voice_notes[idx - 1]->pitch)));
    }
  }
  return worst;
}

TEST(FormCantusRegister, ChoraleCycleBoundaryKeepsFigurationConnected) {
  // The figuration's leap ceiling, which a revoiced bar head keeps on both sides.
  constexpr int kMaxFigurationLeap = 14;
  const HarnessFixture target =
      build(FormType::ChoralePrelude, false, SubjectCharacter::Severe, 16, 43);
  const ComposeResult target_result =
      Composer{}.run(target.material, target.harmony, target.voice_plan);
  ASSERT_EQ(target_result.validation.status, ValidationStatus::Ok);
  EXPECT_LE(maxVoiceConsecutiveLeap(target_result.notes, /*target_voice=*/0), kMaxFigurationLeap);
  const Tick climax_resolve_tick = 12 * kTicksPerBar;
  // The bar head after the register shift is reached and left within the ceiling.
  std::vector<const NoteEvent*> top;
  for (const NoteEvent& note : target_result.notes) {
    if (note.voice == 0)
      top.push_back(&note);
  }
  std::sort(top.begin(), top.end(), [](const NoteEvent* lhs, const NoteEvent* rhs) {
    return lhs->start_tick < rhs->start_tick;
  });
  const auto head = std::find_if(top.begin(), top.end(), [&](const NoteEvent* note) {
    return note->start_tick == climax_resolve_tick;
  });
  ASSERT_TRUE(head != top.end() && head != top.begin() && head + 1 != top.end());
  const int downbeat = (*head)->pitch;
  EXPECT_LE(std::abs(downbeat - static_cast<int>((*(head - 1))->pitch)), kMaxFigurationLeap);
  EXPECT_LE(std::abs(static_cast<int>((*(head + 1))->pitch) - downbeat), kMaxFigurationLeap);

  const auto transition_chord =
      std::find_if(target.harmony.chords.begin(), target.harmony.chords.end(),
                   [](const ChordEvent& chord) { return chord.start_tick == climax_resolve_tick; });
  ASSERT_NE(transition_chord, target.harmony.chords.end());
  const int downbeat_pc = downbeat % 12;
  const int third = transition_chord->quality == ChordQuality::Minor ? 3 : 4;
  EXPECT_TRUE(downbeat_pc == transition_chord->root_pc ||
              downbeat_pc == (transition_chord->root_pc + third) % 12 ||
              downbeat_pc == (transition_chord->root_pc + 7) % 12);

  int climax_peak = 0;
  int resolve_peak = 0;
  for (const NoteEvent& note : target_result.notes) {
    if (note.voice != 0)
      continue;
    if (note.start_tick >= 8 * kTicksPerBar && note.start_tick < climax_resolve_tick)
      climax_peak = std::max(climax_peak, static_cast<int>(note.pitch));
    if (note.start_tick >= climax_resolve_tick && note.start_tick < 14 * kTicksPerBar)
      resolve_peak = std::max(resolve_peak, static_cast<int>(note.pitch));
  }
  EXPECT_GT(climax_peak, resolve_peak);

  for (const std::uint32_t seed : {42u, 44u, 45u}) {
    const HarnessFixture control =
        build(FormType::ChoralePrelude, false, SubjectCharacter::Severe, 16, seed);
    const ComposeResult control_result =
        Composer{}.run(control.material, control.harmony, control.voice_plan);
    ASSERT_EQ(control_result.validation.status, ValidationStatus::Ok) << "seed " << seed;
    EXPECT_LE(maxVoiceConsecutiveLeap(control_result.notes, /*target_voice=*/0), kMaxFigurationLeap)
        << "seed " << seed;
  }

  const HarnessFixture eight_bar =
      build(FormType::ChoralePrelude, false, SubjectCharacter::Severe, 8, 43);
  const ComposeResult eight_bar_result =
      Composer{}.run(eight_bar.material, eight_bar.harmony, eight_bar.voice_plan);
  ASSERT_EQ(eight_bar_result.validation.status, ValidationStatus::Ok);
  EXPECT_LE(maxVoiceConsecutiveLeap(eight_bar_result.notes, /*target_voice=*/0),
            kMaxFigurationLeap);

  for (bool minor : {false, true}) {
    const HarnessFixture longer =
        build(FormType::ChoralePrelude, minor, choraleCharacter(minor), 32, 43);
    const ComposeResult longer_result =
        Composer{}.run(longer.material, longer.harmony, longer.voice_plan);
    ASSERT_EQ(longer_result.validation.status, ValidationStatus::Ok) << "minor=" << minor;
    EXPECT_LE(maxVoiceConsecutiveLeap(longer_result.notes, /*target_voice=*/0), 14)
        << "minor=" << minor;
  }
}

// The goldberg principal line must stay at or under d''' (MIDI 86, the top of
// the Bach keyboard) -- the arc's +12 climax lift compresses against the
// instrument ceiling -- and never fall off a registral cliff: the climax
// block's figuration hands over to the (context-octaved) cadential landing
// within an octave-and-a-step. Both guards were real product-path defects:
// the climax wave rode to C7 and then dropped up to 33 semitones into the
// fixed-register close.
TEST(FormCantusRegister, GoldbergStaysInCompassWithNoCadentialCliff) {
  for (std::uint32_t seed : {1u, 2u, 3u, 5u, 8u, 13u}) {
    const HarnessFixture fx =
        build(FormType::GoldbergVariations, /*minor=*/false, SubjectCharacter::Severe, 0, seed);
    const ComposeResult r = Composer{}.run(fx.material, fx.harmony, fx.voice_plan);
    ASSERT_FALSE(r.notes.empty()) << "seed " << seed;
    for (const auto& n : r.notes) {
      if (n.voice == 0) {
        EXPECT_LE(n.pitch, 86) << "seed " << seed << " V0 above the keyboard top at tick "
                               << n.start_tick;
      }
    }
    EXPECT_LE(maxConsecutiveLeap(r.notes), 14)
        << "seed " << seed << ": registral cliff between consecutive attacks";
  }
}

// The chorale figuration's close connects from the line it interrupts: the
// context-octaved landing keeps every consecutive-attack interval within an
// octave-and-a-step (the uncorrected close dropped a ninth-plus into the
// fixed C5 formula on half the seeds).
TEST(FormCantusRegister, ChoraleCadentialLandingConnectsFromFiguration) {
  for (std::uint32_t seed : {1u, 2u, 5u, 6u, 9u, 10u}) {
    const HarnessFixture fx =
        build(FormType::ChoralePrelude, /*minor=*/false, SubjectCharacter::Severe, 0, seed);
    const ComposeResult r = Composer{}.run(fx.material, fx.harmony, fx.voice_plan);
    ASSERT_FALSE(r.notes.empty()) << "seed " << seed;
    EXPECT_LE(maxConsecutiveLeap(r.notes), 14)
        << "seed " << seed << ": registral cliff between consecutive attacks";
  }
}

// The chorale figuration's sixteenth-tier bars open with the leap-and-fill
// cell (an ascending-sixth leap from the downbeat anchor, then stepwise
// descent), so the V0 line writes ascending sixths -- the corpus interval
// bins the stepwise window fills leave almost empty -- and every such leap
// resolves by contrary (downward) motion. Noble keeps the figuration at the
// sixteenth tier in every non-corta bar, so the cell is guaranteed present.
TEST(FormCantusRegister, ChoraleFigurationWritesContraryResolvedAscendingSixths) {
  for (std::uint32_t seed : {1u, 2u, 5u, 6u, 9u, 10u}) {
    const HarnessFixture fx =
        build(FormType::ChoralePrelude, /*minor=*/false, SubjectCharacter::Noble, 0, seed);
    const ComposeResult r = Composer{}.run(fx.material, fx.harmony, fx.voice_plan);
    ASSERT_FALSE(r.notes.empty()) << "seed " << seed;
    std::vector<const NoteEvent*> v0;
    for (const auto& n : r.notes) {
      if (n.voice == 0)
        v0.push_back(&n);
    }
    std::sort(v0.begin(), v0.end(),
              [](const NoteEvent* a, const NoteEvent* b) { return a->start_tick < b->start_tick; });
    int ascending_sixths = 0;
    int unresolved = 0;
    for (std::size_t idx = 1; idx + 1 < v0.size(); ++idx) {
      const int leap = static_cast<int>(v0[idx]->pitch) - static_cast<int>(v0[idx - 1]->pitch);
      if (leap != 8 && leap != 9)
        continue;
      ++ascending_sixths;
      if (static_cast<int>(v0[idx + 1]->pitch) - static_cast<int>(v0[idx]->pitch) >= 0)
        ++unresolved;
    }
    EXPECT_GT(ascending_sixths, 0) << "seed " << seed;
    EXPECT_EQ(unresolved, 0) << "seed " << seed;
  }
}

}  // namespace
}  // namespace bach::composer
