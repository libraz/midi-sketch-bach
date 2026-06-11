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

#include <algorithm>
#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "composer/arc.h"
#include "composer/composer.h"
#include "composer/form_builders.h"
#include "composer/form_director.h"
#include "composer/minor_material.h"
#include "core/basic_types.h"

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

// The ground table is a seed-selected design variant (seed % kGroundVariantCount,
// fixed for the whole piece). Probe one seed per variant: seed 3 -> variant 0
// (the historical table; seed 0 is reserved for CLI auto so the variant-0 probe
// uses 3), seed 1 -> variant 1, seed 2 -> variant 2.
TEST(FormCantusGoldberg, GroundFollowsSeedVariant) {
  const std::array<std::uint32_t, 3> probe_seed = {3u, 1u, 2u};
  for (bool minor : {false, true}) {
    for (std::size_t variant = 0; variant < detail::kGroundVariantCount; ++variant) {
      const HarnessFixture fx = build(FormType::GoldbergVariations, minor, SubjectCharacter::Severe,
                                      0, probe_seed[variant]);
      ASSERT_EQ(fx.material.passacaglia_ground.size(), 4u);
      const auto& expected =
          minor ? detail::kGoldbergGroundsMinor[variant] : detail::kGoldbergGroundsMajor[variant];
      for (std::size_t bar = 0; bar < 4; ++bar) {
        EXPECT_EQ(fx.material.passacaglia_ground[bar].pitch, expected[bar])
            << "minor " << minor << " variant " << variant << " bar " << bar;
      }
    }
  }
}

// Seeds congruent mod kGroundVariantCount pick the same goldberg ground.
TEST(FormCantusGoldberg, CongruentSeedsShareGround) {
  for (bool minor : {false, true}) {
    const HarnessFixture fx_a =
        build(FormType::GoldbergVariations, minor, SubjectCharacter::Severe, 0, 1);
    const HarnessFixture fx_b =
        build(FormType::GoldbergVariations, minor, SubjectCharacter::Severe, 0, 4);
    ASSERT_EQ(fx_a.material.passacaglia_ground.size(), fx_b.material.passacaglia_ground.size());
    for (std::size_t bar = 0; bar < fx_a.material.passacaglia_ground.size(); ++bar) {
      EXPECT_EQ(fx_a.material.passacaglia_ground[bar].pitch,
                fx_b.material.passacaglia_ground[bar].pitch)
          << "minor " << minor << " bar " << bar;
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
  // canons (uniform eighths) keep their own layouts; the climax block stays
  // the design peak (uniform sixteenths).
  for (std::uint32_t seed : kSeeds) {
    for (bool minor : {false, true}) {
      const HarnessFixture fx =
          build(FormType::GoldbergVariations, minor, SubjectCharacter::Severe, /*bars=*/128, seed);
      const auto& vars = fx.material.passacaglia_variations;
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
          EXPECT_TRUE(
              std::all_of(var.notes.begin(), var.notes.end(),
                          [](const MaterialNote& n) { return n.duration == kTicksPerBeat / 4; }))
              << "seed " << seed << ": climax block must stay uniform sixteenths";
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

}  // namespace
}  // namespace bach::composer
