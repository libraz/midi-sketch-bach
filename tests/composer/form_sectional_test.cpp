// ToccataAndFugue and FantasiaAndFugue form-builder tests.
//
// These cover the two sectional builders in form_sectional.cpp
// (buildToccataAndFugueForm / buildFantasiaAndFugueForm), driven through the
// form-director entry point (buildFormFixture) and the full Composer pipeline.
// Each form is a FREE opening section (toccata or fantasia) on V0 with V1/V2
// resting, then a 3-voice fugue tail confined to disjoint per-voice register
// bands; the only inter-voice rule that can fire is voice_crossing.
//
// Coverage:
//   - both forms validate Ok and are deterministic across
//     seeds {1,5,42,99} x {Major,Minor} x bars {16, 32, 64, 128}.
//   - V1/V2 emit no notes before the fugue boundary bar; the exposition entries
//     appear after it (V0 subject, V1 answer -5, V2 re-entry -12).
//   - the V0 subject first 16 quarters == the selected catalog slot.
//   - a stretto is present when fugue_bars >= 12.
//   - the final ChordEvent / coda lands on the tonic (Picardy when minor + even
//     seed).
//   - toccata: the Material ToccataSection archetype == seed % 4.
//   - fantasia: adjacent FantasiaSection density / register contrast clears the
//     section_contrast_required threshold.
//   - the arc climax bar index lands inside the fugue part for N >= 24.

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

#include "composer/arc.h"
#include "composer/character_profile.h"
#include "composer/composer.h"
#include "composer/figuration.h"
#include "composer/form_director.h"
#include "composer/harmonic_plan.h"
#include "composer/harness_fixture.h"
#include "composer/material.h"
#include "composer/minor_material.h"
#include "composer/voice_intent.h"
#include "core/basic_types.h"

namespace bach::composer {
namespace {

constexpr Tick kBar = static_cast<Tick>(kTicksPerBar);

bool hasRule(const ValidationReport& report, const std::string& rule_id) {
  for (const auto& failure : report.failures) {
    if (failure.rule_id == rule_id) {
      return true;
    }
  }
  return false;
}

// Build a resolved request for a form via the public director path.
HarnessFixture buildFixture(FormType form, std::uint32_t seed, bool is_minor,
                            std::uint16_t target_bars, FormDirectorStatus* status_out = nullptr) {
  ComposeRequest req;
  req.form = form;
  req.seed = seed;
  req.is_minor = is_minor;
  req.character = SubjectCharacter::Severe;  // Severe is admissible for both forms.
  req.target_bars = target_bars;
  HarnessFixture fixture;
  const FormDirectorStatus status = buildFormFixture(req, &fixture);
  if (status_out != nullptr) {
    *status_out = status;
  }
  return fixture;
}

// Mirror the builder's split policy so tests can locate the fugue boundary.
int freeBarsFor(int total) {
  int free_bars = ((total * 3 / 8 + 2) / 4) * 4;
  free_bars = std::max(8, free_bars);
  if (total - free_bars < 8) {
    free_bars = total - 8;
    free_bars = std::max(8, (free_bars / 4) * 4);
  }
  return free_bars;
}

const std::array<std::uint32_t, 4> kSeeds = {1, 5, 42, 99};
const std::array<std::uint16_t, 4> kBarLengths = {16, 32, 64, 128};
const std::array<FormType, 2> kForms = {FormType::ToccataAndFugue, FormType::FantasiaAndFugue};

const char* formName(FormType form) {
  return form == FormType::ToccataAndFugue ? "toccata" : "fantasia";
}

}  // namespace

// --- 1. Validation + determinism across the full matrix ---------------------

TEST(FormSectionalTest, BothFormsValidateAndAreDeterministic) {
  for (FormType form : kForms) {
    for (std::uint32_t seed : kSeeds) {
      for (bool minor : {false, true}) {
        for (std::uint16_t bars : kBarLengths) {
          FormDirectorStatus status = FormDirectorStatus::UnknownForm;
          const HarnessFixture fx = buildFixture(form, seed, minor, bars, &status);
          ASSERT_EQ(status, FormDirectorStatus::Ok)
              << formName(form) << " seed " << seed << " bars " << bars << " director not Ok";
          const ComposeResult r = Composer{}.run(fx.material, fx.harmony, fx.voice_plan);
          EXPECT_TRUE(r.validation.failures.empty())
              << formName(form) << " seed " << seed << (minor ? " minor" : " major") << " bars "
              << bars << " first failure="
              << (r.validation.failures.empty() ? "" : r.validation.failures.front().rule_id);
          EXPECT_FALSE(hasRule(r.validation, "voice_crossing"))
              << formName(form) << " seed " << seed << " bars " << bars << " has voice crossing";
          ASSERT_FALSE(r.notes.empty());

          // Determinism: a second build is byte-identical in note content.
          const HarnessFixture fx2 = buildFixture(form, seed, minor, bars);
          const ComposeResult r2 = Composer{}.run(fx2.material, fx2.harmony, fx2.voice_plan);
          ASSERT_EQ(r.notes.size(), r2.notes.size());
          for (std::size_t i = 0; i < r.notes.size(); ++i) {
            EXPECT_EQ(r.notes[i].pitch, r2.notes[i].pitch);
            EXPECT_EQ(r.notes[i].start_tick, r2.notes[i].start_tick);
            EXPECT_EQ(r.notes[i].voice, r2.notes[i].voice);
          }
        }
      }
    }
  }
}

// --- 2. Free section is single-voice; V1/V2 rest before the boundary --------

TEST(FormSectionalTest, FreeSectionIsSingleVoiceThenExpositionEntersAfterBoundary) {
  for (FormType form : kForms) {
    for (std::uint16_t bars : kBarLengths) {
      const HarnessFixture fx = buildFixture(form, 42, false, bars);
      const Tick boundary = static_cast<Tick>(freeBarsFor(bars)) * kBar;

      // No V1 / V2 span starts before the boundary (the free section is V0 only).
      for (const auto& span : fx.voice_plan.spans) {
        if (span.voice != 0) {
          EXPECT_GE(span.start_tick, boundary)
              << formName(form) << " bars " << bars << " voice " << static_cast<int>(span.voice)
              << " span starts before the fugue boundary";
        }
      }

      // The realized notes also confirm V1 / V2 are silent before the boundary.
      const ComposeResult r = Composer{}.run(fx.material, fx.harmony, fx.voice_plan);
      for (const auto& note : r.notes) {
        if (note.start_tick < boundary) {
          EXPECT_EQ(note.voice, 0)
              << formName(form) << " bars " << bars << " non-V0 note before the boundary";
        }
      }

      // The exposition opens with V0 subject / V1 answer / V2 re-entry after the
      // boundary, at the proven 4-bar stagger.
      const int free_bars = freeBarsFor(bars);
      bool v0_subject = false;
      bool v1_answer = false;
      bool v2_subject = false;
      for (const auto& span : fx.voice_plan.spans) {
        const int start_bar = static_cast<int>(span.start_tick / kBar);
        if (span.voice == 0 && span.intent == VoiceIntent::SubjectCarrier &&
            start_bar == free_bars) {
          v0_subject = true;
        }
        if (span.voice == 1 && span.intent == VoiceIntent::AnswerCarrier &&
            start_bar == free_bars + 4) {
          v1_answer = true;
        }
        if (span.voice == 2 && span.intent == VoiceIntent::SubjectCarrier &&
            start_bar == free_bars + 8) {
          v2_subject = true;
        }
      }
      EXPECT_TRUE(v0_subject) << formName(form) << " bars " << bars << " missing V0 subject";
      EXPECT_TRUE(v1_answer) << formName(form) << " bars " << bars << " missing V1 answer";
      // V2 re-entry only when the fugue tail carries the full 12-bar exposition.
      if (bars - free_bars >= 12) {
        EXPECT_TRUE(v2_subject) << formName(form) << " bars " << bars << " missing V2 re-entry";
      }
    }
  }
}

// --- 3. Entry transposition relationships (-5 answer, -12 re-entry) ----------

TEST(FormSectionalTest, ExpositionEntryTranspositionsAreCorrect) {
  for (FormType form : kForms) {
    for (bool minor : {false, true}) {
      const HarnessFixture fx = buildFixture(form, 42, minor, 64);
      const std::uint8_t slot = detail::subjectSlotFor(SubjectCharacter::Severe, 42);
      const std::array<std::uint8_t, 16>& expected =
          minor ? detail::kSubjectsMinor[slot] : detail::kPhase14Subjects[slot];

      // The V0 subject (first 16 subject notes) is an octave transposition of the
      // catalog slot.
      ASSERT_GE(fx.material.subject.size(), 16u);
      const int v0_off =
          static_cast<int>(fx.material.subject[0].pitch) - static_cast<int>(expected[0]);
      EXPECT_EQ(v0_off % 12, 0) << formName(form) << " V0 subject is not an octave transposition";

      // The V1 answer is the subject - P4 (octave-shifted into the V1 band): each
      // answer note minus its subject note (modulo octaves) is -5 mod 12.
      ASSERT_GE(fx.material.answer.size(), 16u);
      for (int note = 0; note < 16; ++note) {
        const int diff = static_cast<int>(fx.material.answer[note].pitch) -
                         (static_cast<int>(expected[note]) + v0_off);
        EXPECT_EQ(((diff % 12) + 12) % 12, ((-5 % 12) + 12) % 12)
            << formName(form) << " answer note " << note << " not a -P4 transposition";
      }
    }
  }
}

// --- 4. Subject first 16 quarters == selected catalog slot ------------------

TEST(FormSectionalTest, SubjectMelodyMatchesCatalogSlot) {
  for (FormType form : kForms) {
    for (std::uint32_t seed : kSeeds) {
      for (bool minor : {false, true}) {
        const HarnessFixture fx = buildFixture(form, seed, minor, 32);
        const std::uint8_t slot = detail::subjectSlotFor(SubjectCharacter::Severe, seed);
        const std::array<std::uint8_t, 16>& expected =
            minor ? detail::kSubjectsMinor[slot] : detail::kPhase14Subjects[slot];

        ASSERT_GE(fx.material.subject.size(), 16u);
        const int offset =
            static_cast<int>(fx.material.subject[0].pitch) - static_cast<int>(expected[0]);
        EXPECT_EQ(offset % 12, 0) << formName(form) << " subject not an octave transposition";
        for (int note = 0; note < 16; ++note) {
          EXPECT_EQ(static_cast<int>(fx.material.subject[note].pitch),
                    static_cast<int>(expected[note]) + offset)
              << formName(form) << " seed " << seed << " subject note " << note;
        }
      }
    }
  }
}

// --- 5. Stretto present when fugue_bars >= 12 --------------------------------

TEST(FormSectionalTest, StrettoPresentWhenFugueIsLongEnough) {
  for (FormType form : kForms) {
    for (std::uint16_t bars : kBarLengths) {
      const HarnessFixture fx = buildFixture(form, 42, false, bars);
      const int fugue_bars = bars - freeBarsFor(bars);
      if (fugue_bars >= 12) {
        ASSERT_FALSE(fx.material.stretto_entries.empty())
            << formName(form) << " bars " << bars << " (fugue " << fugue_bars << ") has no stretto";
        const StrettoDecl& stretto = fx.material.stretto_entries.front();
        // Follower enters strictly inside the leader window at a <= 1-bar delay.
        EXPECT_GT(stretto.follower_entry_tick, stretto.leader_entry_tick);
        EXPECT_LT(stretto.follower_entry_tick,
                  stretto.leader_entry_tick + stretto.leader_length_ticks);
        EXPECT_LE(stretto.follower_entry_tick - stretto.leader_entry_tick, kBar);

        bool has_stretto_span = false;
        for (const auto& span : fx.voice_plan.spans) {
          if (span.intent == VoiceIntent::StrettoCarrier) {
            has_stretto_span = true;
          }
        }
        EXPECT_TRUE(has_stretto_span) << formName(form) << " bars " << bars << " no StrettoCarrier";
      }
    }
  }
}

// --- 6. Final chord / coda lands on the tonic; Picardy when minor + even -----

TEST(FormSectionalTest, FinalCadenceLandsOnTonic) {
  for (FormType form : kForms) {
    for (std::uint32_t seed : kSeeds) {
      for (bool minor : {false, true}) {
        const std::uint16_t bars = 32;
        const HarnessFixture fx = buildFixture(form, seed, minor, bars);

        // The coda ends on the tonic pitch class.
        ASSERT_FALSE(fx.material.coda_extensions.empty());
        const CodaDecl& coda = fx.material.coda_extensions.front();
        ASSERT_FALSE(coda.notes.empty());
        EXPECT_EQ(coda.notes.back().pitch % 12, 0)
            << formName(form) << " seed " << seed << " coda does not end on tonic";

        // The final ChordEvent (last in tick order) is the tonic (root pc 0).
        ASSERT_FALSE(fx.harmony.chords.empty());
        Tick last_tick = 0;
        const ChordEvent* last_chord = nullptr;
        for (const auto& chord : fx.harmony.chords) {
          if (chord.start_tick >= last_tick) {
            last_tick = chord.start_tick;
            last_chord = &chord;
          }
        }
        ASSERT_NE(last_chord, nullptr);
        EXPECT_EQ(last_chord->root_pc % 12, 0)
            << formName(form) << " seed " << seed << " final chord is not the tonic";

        // A perfect cadence is annotated at the last bar downbeat.
        bool has_final_cadence = false;
        const Tick last_bar_tick = static_cast<Tick>(bars - 1) * kBar;
        for (const auto& cadence : fx.harmony.cadences) {
          if (cadence.tick == last_bar_tick && cadence.type == CadenceType::Perfect) {
            has_final_cadence = true;
          }
        }
        EXPECT_TRUE(has_final_cadence) << formName(form) << " missing final perfect cadence";

        // Picardy: the major third (E, pc 4) appears in the close when minor +
        // even seed; the helper is the single source of truth.
        if (minor && detail::usePicardy(seed)) {
          bool saw_major_third = false;
          for (const auto& note : coda.notes) {
            if (note.pitch % 12 == 4) {
              saw_major_third = true;
            }
          }
          EXPECT_TRUE(saw_major_third)
              << formName(form) << " seed " << seed << " minor: Picardy third absent";
        }
      }
    }
  }
}

// --- 7. Toccata: Material archetype == seed % 4 -----------------------------

TEST(FormSectionalTest, ToccataArchetypeMatchesSeed) {
  for (std::uint32_t seed : kSeeds) {
    for (std::uint16_t bars : kBarLengths) {
      const HarnessFixture fx = buildFixture(FormType::ToccataAndFugue, seed, false, bars);
      ASSERT_FALSE(fx.material.toccata_sections.empty());
      const ToccataArchetype expected = static_cast<ToccataArchetype>(seed % 4);
      for (const auto& section : fx.material.toccata_sections) {
        EXPECT_EQ(section.archetype, expected)
            << "toccata seed " << seed << " bars " << bars << " archetype mismatch";
        // The director blocks Noble for this form; Severe is always compatible.
        EXPECT_EQ(section.character, SubjectCharacter::Severe);
      }
    }
  }
}

// --- 8. Fantasia: adjacent sections satisfy section_contrast_required --------

TEST(FormSectionalTest, FantasiaAdjacentSectionsContrast) {
  constexpr int kMinDensityMargin = 2;
  constexpr int kMinRegisterMargin = 5;
  for (std::uint32_t seed : kSeeds) {
    for (std::uint16_t bars : kBarLengths) {
      const HarnessFixture fx = buildFixture(FormType::FantasiaAndFugue, seed, false, bars);
      ASSERT_GE(fx.material.fantasia_sections.size(), 2u);

      // Realized density (notes/bar) + mean register per section, mirroring the
      // validator's section_contrast_required measurement.
      struct Stat {
        int density;
        int mean_register;
      };
      std::vector<Stat> stats;
      for (const auto& section : fx.material.fantasia_sections) {
        const Tick window = section.end_tick - section.start_tick;
        long pitch_sum = 0;
        for (const auto& note : section.notes) {
          pitch_sum += note.pitch;
        }
        const int count = static_cast<int>(section.notes.size());
        ASSERT_GT(count, 0);
        Stat stat;
        stat.density = static_cast<int>(static_cast<long>(count) * kBar / window);
        stat.mean_register = static_cast<int>(pitch_sum / count);
        stats.push_back(stat);
      }
      for (std::size_t i = 1; i < stats.size(); ++i) {
        const int density_diff = std::abs(stats[i].density - stats[i - 1].density);
        const int register_diff = std::abs(stats[i].mean_register - stats[i - 1].mean_register);
        EXPECT_TRUE(density_diff >= kMinDensityMargin || register_diff >= kMinRegisterMargin)
            << "fantasia seed " << seed << " bars " << bars << " sections " << (i - 1) << "/" << i
            << " do not contrast (density " << density_diff << ", register " << register_diff
            << ")";
      }
    }
  }
}

// --- 9. Arc climax lands inside the fugue part for N >= 24 -------------------

TEST(FormSectionalTest, ArcClimaxIsInsideTheFuguePartForLargeForms) {
  for (FormType form : kForms) {
    for (std::uint16_t bars : {std::uint16_t{32}, std::uint16_t{64}, std::uint16_t{128}}) {
      // The director uses snap_bars == 4, so cycle_count == bars / 4 and cycle k
      // covers bars [4k, 4k+4). Find the climax cycle.
      const std::size_t cycle_count = static_cast<std::size_t>(bars) / 4;
      int climax_first_bar = -1;
      for (std::size_t cyc = 0; cyc < cycle_count; ++cyc) {
        if (arcPoint(cyc, cycle_count).is_climax) {
          climax_first_bar = static_cast<int>(cyc) * 4;
          break;
        }
      }
      ASSERT_GE(climax_first_bar, 0) << formName(form) << " bars " << bars << " no climax cycle";
      EXPECT_GE(climax_first_bar, freeBarsFor(bars))
          << formName(form) << " bars " << bars << " climax bar " << climax_first_bar
          << " is not inside the fugue part (boundary " << freeBarsFor(bars) << ")";
    }
  }
}

}  // namespace bach::composer
