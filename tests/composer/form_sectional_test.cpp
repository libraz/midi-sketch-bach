// ToccataAndFugue and FantasiaAndFugue form-builder tests.
//
// These cover the two sectional builders in form_sectional.cpp
// (buildToccataAndFugueForm / buildFantasiaAndFugueForm), driven through the
// form-director entry point (buildFormFixture) and the full Composer pipeline.
// Each form is a FREE opening section (toccata or fantasia) led by V0, supported
// by a V2 pedal-point layer and a V1 head-punctuation layer (BWV565 / BWV538
// pedal idiom), then a 3-voice fugue tail confined to disjoint per-voice
// register bands; the only inter-voice rule that can fire is voice_crossing.
//
// Coverage:
//   - both forms validate Ok and are deterministic across
//     seeds {1,5,42,99} x {Major,Minor} x bars {16, 32, 64, 128}.
//   - the free section carries V2 pedal + V1 punctuation before the fugue
//     boundary, and the exposition entries appear after it (V0 subject, V1
//     answer -5, V2 re-entry -12).
//   - the V0 subject first 16 quarters == the selected catalog slot.
//   - a stretto is present when fugue_bars >= 12.
//   - the final ChordEvent / coda lands on the tonic (Picardy when minor + even
//     seed).
//   - toccata: the Material ToccataSection archetype == seed % 4.
//   - fantasia: adjacent FantasiaSection density / register contrast clears the
//     section_contrast_required threshold.
//   - the arc climax bar index lands inside the fugue part for N >= 24.

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <set>
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
#include "composer/subject_catalog.h"
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

// --- Registration terrace (fixture metadata only, never a note) -------------

TEST(FormSectionalTest, ToccataDeclaresOneRegistrationTerraceAtFugueBoundary) {
  for (std::uint16_t bars : kBarLengths) {
    const HarnessFixture fx = buildFixture(FormType::ToccataAndFugue, 5, false, bars);
    ASSERT_EQ(fx.registration_step_ticks.size(), 1u) << "bars " << bars;
    const Tick boundary = static_cast<Tick>(freeBarsFor(bars)) * kBar;
    EXPECT_EQ(fx.registration_step_ticks.front(), boundary) << "bars " << bars;
  }
}

TEST(FormSectionalTest, FantasiaDeclaresSectionCadenceAndRegistrationAtFreeBoundary) {
  for (std::uint32_t seed : {3u, 42u}) {
    for (std::uint16_t bars : kBarLengths) {
      const HarnessFixture fx = buildFixture(FormType::FantasiaAndFugue, seed, false, bars);
      const int free_bars = freeBarsFor(bars);
      // The free fantasia section closes at its final bar before the fugue.
      ASSERT_EQ(fx.section_cadence_ticks.size(), 1u) << "seed " << seed << " bars " << bars;
      EXPECT_EQ(fx.section_cadence_ticks.front(), static_cast<Tick>(free_bars - 1) * kBar)
          << "seed " << seed << " bars " << bars;
      // The organ steps up a stop at the fantasia->fugue boundary.
      ASSERT_EQ(fx.registration_step_ticks.size(), 1u) << "seed " << seed << " bars " << bars;
      EXPECT_EQ(fx.registration_step_ticks.front(), static_cast<Tick>(free_bars) * kBar)
          << "seed " << seed << " bars " << bars;
      // The declared cadence sits strictly inside the piece, before the fugue.
      EXPECT_GT(fx.section_cadence_ticks.front(), 0);
      EXPECT_LT(fx.section_cadence_ticks.front(), fx.registration_step_ticks.front());
    }
  }
}

// --- 2. Free section is multi-voice (V0 lead + V2 pedal + V1 punctuation) and
//        the fugue exposition still enters after the boundary ----------------

TEST(FormSectionalTest, FreeSectionCarriesPedalAndPunctuationThenExpositionEntersAfterBoundary) {
  for (FormType form : kForms) {
    for (std::uint16_t bars : kBarLengths) {
      const HarnessFixture fx = buildFixture(form, 42, false, bars);
      const Tick boundary = static_cast<Tick>(freeBarsFor(bars)) * kBar;

      // The free section is now accompanied: at least one V2 pedal note and one
      // V1 punctuation note sound before the fugue boundary (BWV565 / BWV538
      // pedal idiom). The accompaniment layers carry the FigurationCommitted
      // bit (FigurationCarrier dispatch); V0 carries the toccata / fantasia
      // section bit.
      const ComposeResult r = Composer{}.run(fx.material, fx.harmony, fx.voice_plan);
      bool v1_before = false;
      bool v2_before = false;
      for (const auto& note : r.notes) {
        if (note.start_tick < boundary) {
          if (note.voice == 1) {
            v1_before = true;
          } else if (note.voice == 2) {
            v2_before = true;
          }
          // Strict register order V0 >= V1 >= V2 is enforced by the validator's
          // voice_crossing rule (asserted absent below); no extra check here.
        }
      }
      EXPECT_TRUE(v2_before) << formName(form) << " bars " << bars
                             << " has no V2 pedal in the free section";
      EXPECT_TRUE(v1_before) << formName(form) << " bars " << bars
                             << " has no V1 punctuation in the free section";

      // The added layers must not introduce any validation failure, in
      // particular no voice crossing between the new free-section voices.
      EXPECT_TRUE(r.validation.failures.empty())
          << formName(form) << " bars " << bars << " free-section layers fail validation: "
          << (r.validation.failures.empty() ? "" : r.validation.failures.front().rule_id);

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
      const std::uint8_t slot = detail::subjectIndexFor(SubjectCharacter::Severe, minor, 42);
      const std::array<std::uint8_t, 16>& expected =
          minor ? tables::kSubjectCatalogMinor[slot] : tables::kSubjectCatalogMajor[slot];

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
        const std::uint8_t slot = detail::subjectIndexFor(SubjectCharacter::Severe, minor, seed);
        const std::array<std::uint8_t, 16>& expected =
            minor ? tables::kSubjectCatalogMinor[slot] : tables::kSubjectCatalogMajor[slot];

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

TEST(FormSectionalTest, SubjectRhythmProfileIsAppliedInFugueTail) {
  for (FormType form : kForms) {
    const HarnessFixture fx = buildFixture(form, 42, false, 64);
    const std::uint8_t slot = detail::subjectIndexFor(SubjectCharacter::Severe, false, 42);
    const auto& expected = tables::kSubjectCatalogMajorRhythms[slot];

    ASSERT_GE(fx.material.subject.size(), expected.size());
    Tick cursor = fx.material.subject.front().start_tick;
    const Tick subject_start = cursor;
    bool has_non_quarter = false;
    for (std::size_t i = 0; i < expected.size(); ++i) {
      EXPECT_EQ(fx.material.subject[i].start_tick, cursor) << formName(form) << " index " << i;
      EXPECT_EQ(fx.material.subject[i].duration, expected[i]) << formName(form) << " index " << i;
      has_non_quarter = has_non_quarter || expected[i] != kTicksPerBeat;
      cursor += expected[i];
    }
    EXPECT_TRUE(has_non_quarter) << formName(form);
    EXPECT_EQ(cursor - subject_start, 4 * kTicksPerBar) << formName(form);
  }
}

TEST(FormSectionalTest, DominantHeadSubjectUsesTonalAnswerInRealization) {
  for (FormType form : kForms) {
    const HarnessFixture fx = buildFixture(form, 5, false, 64);
    EXPECT_TRUE(fx.material.use_tonal_answer) << formName(form);
    ASSERT_FALSE(fx.material.tonal_answer.empty()) << formName(form);

    const ComposeResult r = Composer{}.run(fx.material, fx.harmony, fx.voice_plan);
    bool tonal_answer_bit = false;
    for (const auto& prov : r.provenance) {
      if (prov.voice_intent == VoiceIntent::AnswerCarrier &&
          (prov.satisfied_rules & ruleBitMask(RuleBit::TonalAnswerMapped)) != 0u) {
        tonal_answer_bit = true;
        break;
      }
    }
    EXPECT_TRUE(tonal_answer_bit) << formName(form);
    ASSERT_FALSE(fx.material.imitation_entries.empty()) << formName(form);
    const ImitationEntry& entry = fx.material.imitation_entries.front();
    EXPECT_EQ(entry.follower_fragment, MaterialFragment::TonalAnswer) << formName(form);
    EXPECT_EQ(entry.interval_semis, static_cast<int>(fx.material.tonal_answer.front().pitch) -
                                        static_cast<int>(fx.material.subject.front().pitch))
        << formName(form);
  }
}

// The fugue tail keeps a full three-voice texture through its development
// fill (V0 running line + V1 quarter-anchor counterline + V2 sustained
// support). Long fills previously rested V1 entirely, thinning the second
// half of the piece to two voices between the exposition and the stretto.
// Allowed thin bars in the second half: the stretto leader bar (the follower
// enters at a 1-bar delay by design) plus one boundary bar of slack.
TEST(FormSectionalTest, FugueTailDevelopmentKeepsThreeVoiceTexture) {
  for (FormType form : kForms) {
    for (std::uint32_t seed : {1u, 9u, 17u}) {
      const std::uint16_t bars = 64;
      const HarnessFixture fx = buildFixture(form, seed, false, bars);
      const ComposeResult r = Composer{}.run(fx.material, fx.harmony, fx.voice_plan);

      std::array<std::set<int>, 64> sounding{};
      for (const auto& note : r.notes) {
        const int first = static_cast<int>(note.start_tick / kBar);
        const int last = static_cast<int>((note.start_tick + note.duration - 1) / kBar);
        for (int bar = first; bar <= std::min(last, static_cast<int>(bars) - 1); ++bar) {
          sounding[static_cast<std::size_t>(bar)].insert(static_cast<int>(note.voice));
        }
      }
      int thin_bars = 0;
      for (int bar = bars / 2; bar < bars; ++bar) {
        if (sounding[static_cast<std::size_t>(bar)].size() < 3u) {
          ++thin_bars;
        }
      }
      EXPECT_LE(thin_bars, 2) << formName(form) << " seed " << seed
                              << " second half rests a voice for " << thin_bars << " bars";
    }
  }
}

TEST(FormSectionalTest, CountersubjectAccompaniesAnswerAndThirdEntry) {
  for (FormType form : kForms) {
    const HarnessFixture fx = buildFixture(form, 42, false, 64);
    ASSERT_FALSE(fx.material.countersubject.empty()) << formName(form);

    bool answer_cs_span = false;
    bool third_entry_cs_span = false;
    for (const auto& span : fx.voice_plan.spans) {
      const int start_bar = static_cast<int>(span.start_tick / kTicksPerBar);
      if (span.intent == VoiceIntent::CountersubjectCarrier && span.voice == 0 && start_bar >= 4) {
        answer_cs_span = true;
      }
      if (span.intent == VoiceIntent::CountersubjectCarrier && span.voice == 1 && start_bar >= 8) {
        third_entry_cs_span = true;
      }
    }
    EXPECT_TRUE(answer_cs_span) << formName(form);
    EXPECT_TRUE(third_entry_cs_span) << formName(form);

    const ComposeResult r = Composer{}.run(fx.material, fx.harmony, fx.voice_plan);
    bool countersubject_bit = false;
    for (const auto& prov : r.provenance) {
      if (prov.voice_intent == VoiceIntent::CountersubjectCarrier &&
          (prov.satisfied_rules & ruleBitMask(RuleBit::CountersubjectActive)) != 0u) {
        countersubject_bit = true;
        break;
      }
    }
    EXPECT_TRUE(countersubject_bit) << formName(form);
    ASSERT_FALSE(r.validation.texture_metrics.empty()) << formName(form);
    EXPECT_EQ(r.validation.texture_metrics[0].max_active_voices, 3) << formName(form);
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
        // even seed; the V1 inner voice holds the closing third (the V0
        // landing holds only the tonic), so the scan covers the figuration
        // sections.
        if (minor && detail::usePicardy(seed)) {
          bool saw_major_third = false;
          for (const auto& section : fx.material.figuration_sections) {
            for (const auto& note : section.notes) {
              if (note.start_tick >= last_bar_tick && note.pitch % 12 == 4) {
                saw_major_third = true;
              }
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

// --- 7b. Toccata Dramaticus BWV565 upgrades ---------------------------------

namespace {

// Composed notes in [bar, bar+1) for the given voice, sorted by (tick, pitch).
std::vector<NoteEvent> barVoiceNotes(const std::vector<NoteEvent>& notes, int bar, VoiceId voice) {
  const Tick lo = static_cast<Tick>(bar) * kBar;
  const Tick hi = lo + kBar;
  std::vector<NoteEvent> out;
  for (const auto& note : notes) {
    if (note.voice == voice && note.start_tick >= lo && note.start_tick < hi)
      out.push_back(note);
  }
  std::sort(out.begin(), out.end(), [](const NoteEvent& a, const NoteEvent& b) {
    if (a.start_tick != b.start_tick)
      return a.start_tick < b.start_tick;
    return a.pitch < b.pitch;
  });
  return out;
}

// A Dramaticus seed (seed % 4 == 0) whose free section is long enough (default
// 32 bars -> 12 free bars) to carry the octave cascade + fermata breath.
constexpr std::uint32_t kDramaticusSeed = 4;

}  // namespace

TEST(FormSectionalTest, ToccataDramaticusOctaveCascadeAndUnisonDoubling) {
  const HarnessFixture fx = buildFixture(FormType::ToccataAndFugue, kDramaticusSeed, false, 32);
  const ComposeResult r = Composer{}.run(fx.material, fx.harmony, fx.voice_plan);
  ASSERT_TRUE(r.validation.failures.empty())
      << "Dramaticus fails: "
      << (r.validation.failures.empty() ? "" : r.validation.failures.front().rule_id);

  const std::vector<NoteEvent> bar0 = barVoiceNotes(r.notes, 0, 0);
  const std::vector<NoteEvent> bar1 = barVoiceNotes(r.notes, 1, 0);
  ASSERT_FALSE(bar0.empty());
  ASSERT_EQ(bar0.size(), bar1.size());
  // bar 1 is the opening gesture stated exactly one octave lower than bar 0.
  for (std::size_t i = 0; i < bar0.size(); ++i) {
    EXPECT_EQ(static_cast<int>(bar1[i].pitch), static_cast<int>(bar0[i].pitch) - 12);
    EXPECT_EQ(bar1[i].start_tick - kBar, bar0[i].start_tick);
  }

  // bar 2 is the unison-gesture bar: V0 and V1 both sound, V1 exactly 12 below
  // V0 at equal ticks (the deliberate BWV565 octave doubling).
  const std::vector<NoteEvent> bar2v0 = barVoiceNotes(r.notes, 2, 0);
  const std::vector<NoteEvent> bar2v1 = barVoiceNotes(r.notes, 2, 1);
  ASSERT_FALSE(bar2v0.empty());
  ASSERT_EQ(bar2v0.size(), bar2v1.size());
  for (std::size_t i = 0; i < bar2v0.size(); ++i) {
    EXPECT_EQ(static_cast<int>(bar2v1[i].pitch), static_cast<int>(bar2v0[i].pitch) - 12);
    EXPECT_EQ(bar2v1[i].start_tick, bar2v0[i].start_tick);
  }

  // bar 3 is the declamatory chord block: three voices sound (V0 block over the
  // homophonic V1 + V2 strike).
  std::set<VoiceId> bar3_voices;
  for (const auto& note : r.notes) {
    if (note.start_tick >= 3 * kBar && note.start_tick < 4 * kBar)
      bar3_voices.insert(note.voice);
  }
  EXPECT_EQ(bar3_voices.size(), 3u) << "bar 3 is not a full-texture chord block";
}

TEST(FormSectionalTest, ToccataDramaticusFermataBreath) {
  const HarnessFixture fx = buildFixture(FormType::ToccataAndFugue, kDramaticusSeed, false, 32);
  const ComposeResult r = Composer{}.run(fx.material, fx.harmony, fx.voice_plan);
  ASSERT_TRUE(r.validation.failures.empty());

  // The fermata bar is the head of the second window: flourish snapped to the
  // 4-bar grid, clamped into [4, free_bars - 4].
  const int free_bars = freeBarsFor(32);
  int flourish = ((free_bars / 4 + 3) / 4) * 4;
  flourish = std::max(4, std::min(flourish, free_bars - 4));

  // The fermata bar strikes a single whole note in each of V0, V1, V2.
  for (VoiceId voice : {VoiceId{0}, VoiceId{1}, VoiceId{2}}) {
    const std::vector<NoteEvent> notes = barVoiceNotes(r.notes, flourish, voice);
    ASSERT_EQ(notes.size(), 1u) << "fermata voice " << static_cast<int>(voice)
                                << " is not a single strike";
    EXPECT_EQ(notes.front().duration, static_cast<Tick>(kTicksPerBar));
    EXPECT_EQ(notes.front().start_tick, static_cast<Tick>(flourish) * kBar);
  }
}

TEST(FormSectionalTest, ToccataTripletDriveInSecondHalf) {
  // Dramaticus and Perpetuus tighten to sixteenth triplets (80-tick notes) in
  // the section's last half. At least one V0 wave bar in [free_bars/2, ...)
  // carries 80-tick durations, and its beat onsets are diatonic chord anchors.
  for (std::uint32_t seed : {std::uint32_t{4}, std::uint32_t{5}}) {  // Dramaticus, Perpetuus.
    const HarnessFixture fx = buildFixture(FormType::ToccataAndFugue, seed, false, 64);
    const ComposeResult r = Composer{}.run(fx.material, fx.harmony, fx.voice_plan);
    ASSERT_TRUE(r.validation.failures.empty())
        << "seed " << seed << " fails: "
        << (r.validation.failures.empty() ? "" : r.validation.failures.front().rule_id);
    const int free_bars = freeBarsFor(64);
    bool found_triplet = false;
    for (int bar = free_bars / 2; bar < free_bars; ++bar) {
      const std::vector<NoteEvent> notes = barVoiceNotes(r.notes, bar, 0);
      const bool triplet =
          !notes.empty() && std::all_of(notes.begin(), notes.end(), [](const NoteEvent& n) {
            return n.duration == static_cast<Tick>(80);
          });
      if (triplet) {
        found_triplet = true;
        EXPECT_EQ(notes.size(), 24u) << "seed " << seed << " bar " << bar << " triplet count";
      }
    }
    EXPECT_TRUE(found_triplet) << "seed " << seed << " has no triplet wave bar in the last half";
  }
}

TEST(FormSectionalTest, ToccataAllArchetypesValidateAcrossSeeds) {
  // Seeds 1..12 cover every archetype (seed % 4) in both modes; the Dramaticus
  // upgrades and the triplet drive must never introduce a validation failure.
  for (std::uint32_t seed = 1; seed <= 12; ++seed) {
    for (bool minor : {false, true}) {
      const HarnessFixture fx = buildFixture(FormType::ToccataAndFugue, seed, minor, 32);
      const ComposeResult r = Composer{}.run(fx.material, fx.harmony, fx.voice_plan);
      EXPECT_TRUE(r.validation.failures.empty())
          << "toccata seed " << seed << (minor ? " minor" : " major") << " first failure="
          << (r.validation.failures.empty() ? "" : r.validation.failures.front().rule_id);
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

// --- 10. Dramaticus keeps its opening flourish solo ------------------------

// A Dramaticus toccata (archetype = seed % 4 == 0) opens with a solo flourish:
// the first two free bars carry V0 only, then the V2 pedal / V1 punctuation
// layers enter. The solo rhetoric is intentional (BWV565 opening); this verifies
// the layers respect the exemption rather than filling every free bar.
TEST(FormSectionalTest, DramaticusOpeningFlourishStaysSolo) {
  for (std::uint32_t seed : {std::uint32_t{4}, std::uint32_t{8}, std::uint32_t{12}}) {
    ASSERT_EQ(seed % 4u, 0u) << "seed " << seed << " is not a Dramaticus archetype";
    const HarnessFixture fx = buildFixture(FormType::ToccataAndFugue, seed, false, 32);
    const ComposeResult r = Composer{}.run(fx.material, fx.harmony, fx.voice_plan);

    // Bars 0-1 (the solo flourish) carry V0 only; some later free bar carries an
    // accompaniment voice.
    bool accompaniment_in_first_two_bars = false;
    bool accompaniment_later_in_free = false;
    const Tick free_boundary = static_cast<Tick>(freeBarsFor(32)) * kBar;
    for (const auto& note : r.notes) {
      if (note.voice == 0) {
        continue;
      }
      if (note.start_tick < 2 * kBar) {
        accompaniment_in_first_two_bars = true;
      } else if (note.start_tick < free_boundary) {
        accompaniment_later_in_free = true;
      }
    }
    EXPECT_FALSE(accompaniment_in_first_two_bars)
        << "Dramaticus seed " << seed << " accompanies the opening solo flourish";
    EXPECT_TRUE(accompaniment_later_in_free)
        << "Dramaticus seed " << seed << " never enters the accompaniment after the flourish";
  }
}

// --- 11. Fantasia pedal covers every free bar; Chordal sections are homophonic

// The fantasia free section carries a V2 pedal under every bar (no solo
// exemption for the fantasia), and a Chordal-style section strikes V1 and V2
// together (half-note homophony). This locks the per-style accompaniment matrix.
TEST(FormSectionalTest, FantasiaPedalCoversEveryFreeBar) {
  for (std::uint32_t seed : kSeeds) {
    const std::uint16_t bars = 32;
    const HarnessFixture fx = buildFixture(FormType::FantasiaAndFugue, seed, false, bars);
    const ComposeResult r = Composer{}.run(fx.material, fx.harmony, fx.voice_plan);
    const int free_bars = freeBarsFor(bars);

    // Every free bar has a V2 (pedal) note sounding at its downbeat.
    for (int bar = 0; bar < free_bars; ++bar) {
      const Tick downbeat = static_cast<Tick>(bar) * kBar;
      bool v2_sounding = false;
      for (const auto& note : r.notes) {
        if (note.voice == 2 && note.start_tick <= downbeat &&
            downbeat < note.start_tick + note.duration) {
          v2_sounding = true;
          break;
        }
      }
      EXPECT_TRUE(v2_sounding) << "fantasia seed " << seed << " bar " << bar << " has no V2 pedal";
    }
  }
}

// --- 12. Fugue tail is parallel-free and texture-thickened -------------------

namespace {

// Pitch of the latest-onset note of `voice` covering `tick`, or -1 if silent.
// Mirrors the texture-gate's union-onset sounding-pitch sampling.
int soundingPitch(const std::vector<NoteEvent>& notes, int voice, Tick tick) {
  int pitch = -1;
  long long best_start = -1;
  for (const auto& note : notes) {
    if (static_cast<int>(note.voice) != voice) {
      continue;
    }
    if (note.start_tick <= tick &&
        tick<note.start_tick + note.duration&& static_cast<long long>(note.start_tick)>
            best_start) {
      best_start = static_cast<long long>(note.start_tick);
      pitch = static_cast<int>(note.pitch);
    }
  }
  return pitch;
}

// Count parallel perfect fifths/octaves across all voice pairs by union-onset
// sampling, identical to the texture-gate's compute_parallel_counts: at each
// onset where both voices sound, a parallel = both voices moved in the same
// direction since the previous sampled onset onto interval class 0/7 that was
// already 0/7 at the previous onset.
int countParallelPerfects(const std::vector<NoteEvent>& notes) {
  std::vector<int> voices;
  std::vector<Tick> onsets;
  for (const auto& note : notes) {
    const int voice = static_cast<int>(note.voice);
    if (std::find(voices.begin(), voices.end(), voice) == voices.end()) {
      voices.push_back(voice);
    }
    if (std::find(onsets.begin(), onsets.end(), note.start_tick) == onsets.end()) {
      onsets.push_back(note.start_tick);
    }
  }
  std::sort(voices.begin(), voices.end());
  std::sort(onsets.begin(), onsets.end());
  int parallel = 0;
  for (std::size_t lo = 0; lo < voices.size(); ++lo) {
    for (std::size_t up = lo + 1; up < voices.size(); ++up) {
      bool have_prev = false;
      int prev_a = 0;
      int prev_b = 0;
      for (Tick tick : onsets) {
        const int pitch_a = soundingPitch(notes, voices[lo], tick);
        const int pitch_b = soundingPitch(notes, voices[up], tick);
        if (pitch_a < 0 || pitch_b < 0) {
          have_prev = false;
          continue;
        }
        if (have_prev) {
          const int delta_a = pitch_a - prev_a;
          const int delta_b = pitch_b - prev_b;
          const bool same_dir = (delta_a > 0 && delta_b > 0) || (delta_a < 0 && delta_b < 0);
          const int curr_ic = std::abs(pitch_a - pitch_b) % 12;
          if (delta_a != 0 && delta_b != 0 && same_dir && (curr_ic == 0 || curr_ic == 7) &&
              std::abs(prev_a - prev_b) % 12 == curr_ic) {
            ++parallel;
          }
        }
        have_prev = true;
        prev_a = pitch_a;
        prev_b = pitch_b;
      }
    }
  }
  return parallel;
}

}  // namespace

// The rewired fugue tail draws every accompaniment pitch through the shared
// parallel-avoidance machinery (ThemeToneRegistry + consonantChordTone), so the
// whole piece's parallel perfect-fifth/octave count stays within the corpus
// ceiling (12), well below the pre-rewiring count produced by the old octave-
// shifted countersubject clone.
TEST(FormSectionalTest, FugueTailStaysWithinParallelCeiling) {
  constexpr int kCorpusParallelCeiling = 12;
  for (FormType form : kForms) {
    for (std::uint32_t seed : kSeeds) {
      for (bool is_minor : {false, true}) {
        const HarnessFixture fx = buildFixture(form, seed, is_minor, 32);
        const ComposeResult r = Composer{}.run(fx.material, fx.harmony, fx.voice_plan);
        EXPECT_LE(countParallelPerfects(r.notes), kCorpusParallelCeiling)
            << formName(form) << " seed " << seed << (is_minor ? " minor" : " major")
            << " exceeds the corpus parallel ceiling";
      }
    }
  }
}

// The rewired tail caps the monophonic solo to the opening subject-entry bars
// only: every later tail bar sounds at least two voices (between-stretto fills
// carry a V2 support, the answer entry a V2 support, the cadence a V1 inner
// voice). At most the first two tail bars (the subject head's solo gesture) are
// monophonic.
TEST(FormSectionalTest, FugueTailIsAtLeastTwoVoicesExceptOpeningEntry) {
  for (FormType form : kForms) {
    for (std::uint32_t seed : kSeeds) {
      const std::uint16_t bars = 32;
      const HarnessFixture fx = buildFixture(form, seed, false, bars);
      const ComposeResult r = Composer{}.run(fx.material, fx.harmony, fx.voice_plan);
      const int free_bars = freeBarsFor(bars);
      int thin_tail_bars = 0;
      for (int bar = free_bars; bar < bars; ++bar) {
        const Tick downbeat = static_cast<Tick>(bar) * kBar;
        int active = 0;
        for (int voice = 0; voice < 3; ++voice) {
          if (soundingPitch(r.notes, voice, downbeat) >= 0) {
            ++active;
          }
        }
        if (active < 2) {
          ++thin_tail_bars;
        }
      }
      // Only the opening subject-entry gesture (capped at two bars) may be thin.
      EXPECT_LE(thin_tail_bars, 2)
          << formName(form) << " seed " << seed << " has " << thin_tail_bars
          << " monophonic tail bars (expected <= 2 opening-entry bars)";
    }
  }
}

// --- Archetype material plans --------------------------------------------------

// Per-bar, per-voice notes of the free section.
std::vector<std::array<std::vector<const NoteEvent*>, 3>> freeBarsByVoice(const ComposeResult& r,
                                                                          int free_bars) {
  std::vector<std::array<std::vector<const NoteEvent*>, 3>> bars(
      static_cast<std::size_t>(free_bars));
  for (const auto& n : r.notes) {
    const int bar = static_cast<int>(n.start_tick / kBar);
    if (bar < free_bars && n.voice < 3)
      bars[static_cast<std::size_t>(bar)][n.voice].push_back(&n);
  }
  return bars;
}

bool allDurationsAre(const std::vector<const NoteEvent*>& notes, Tick dur) {
  for (const NoteEvent* n : notes) {
    if (n->duration != dur)
      return false;
  }
  return true;
}

// Concertato: forte 4-bar windows run sixteenths over the full pedal +
// punctuation texture; the odd (piano) windows answer in eighths with the V1
// punctuation only (no V2 pedal).
TEST(FormSectionalTest, ConcertatoAlternatesForteAndPianoWindows) {
  for (std::uint32_t seed : {2u, 6u, 10u}) {  // seed % 4 == 2 -> Concertato.
    const HarnessFixture fx = buildFixture(FormType::ToccataAndFugue, seed, false, 32);
    const ComposeResult r = Composer{}.run(fx.material, fx.harmony, fx.voice_plan);
    ASSERT_EQ(r.validation.status, ValidationStatus::Ok) << "seed " << seed;
    const int free_bars = freeBarsFor(32);
    const auto bars = freeBarsByVoice(r, free_bars);
    for (int bar = 0; bar < free_bars; ++bar) {
      const auto& by_voice = bars[static_cast<std::size_t>(bar)];
      const bool piano = (bar / 4) % 2 == 1;
      const std::string ctx = "seed " + std::to_string(seed) + " bar " + std::to_string(bar);
      ASSERT_FALSE(by_voice[0].empty()) << ctx;
      if (piano) {
        EXPECT_TRUE(allDurationsAre(by_voice[0], kTicksPerBeat / 2))
            << ctx << ": piano echo must run eighths";
        EXPECT_TRUE(by_voice[2].empty()) << ctx << ": piano echo must rest the pedal";
      } else {
        EXPECT_TRUE(allDurationsAre(by_voice[0], kTicksPerBeat / 4))
            << ctx << ": forte window must run sixteenths";
        EXPECT_FALSE(by_voice[2].empty()) << ctx << ": forte window carries the pedal";
      }
    }
  }
}

// Sectionalis: the first half runs the wave; the second half alternates
// declamatory chord blocks (three-voice half-note strikes) with running bars.
TEST(FormSectionalTest, SectionalisSecondHalfAlternatesChordBlocks) {
  for (std::uint32_t seed : {3u, 7u, 11u}) {  // seed % 4 == 3 -> Sectionalis.
    const HarnessFixture fx = buildFixture(FormType::ToccataAndFugue, seed, false, 32);
    const ComposeResult r = Composer{}.run(fx.material, fx.harmony, fx.voice_plan);
    ASSERT_EQ(r.validation.status, ValidationStatus::Ok) << "seed " << seed;
    const int free_bars = freeBarsFor(32);
    // Mirror the builder's split point (half on a 4-bar grid, clamped).
    const int split_bar = std::clamp(((free_bars / 2 + 3) / 4) * 4, 4, free_bars - 4);
    const auto bars = freeBarsByVoice(r, free_bars);
    int block_bars = 0;
    for (int bar = 0; bar < free_bars; ++bar) {
      const auto& by_voice = bars[static_cast<std::size_t>(bar)];
      const std::string ctx = "seed " + std::to_string(seed) + " bar " + std::to_string(bar);
      if (bar < split_bar) {
        EXPECT_FALSE(by_voice[0].empty()) << ctx;
        for (const NoteEvent* n : by_voice[0])
          EXPECT_LE(n->duration, kTicksPerBeat / 2) << ctx << ": first half runs the wave";
      } else if ((bar - split_bar) % 2 == 0) {
        ++block_bars;
        for (int voice = 0; voice < 3; ++voice) {
          ASSERT_FALSE(by_voice[static_cast<std::size_t>(voice)].empty()) << ctx;
          EXPECT_TRUE(allDurationsAre(by_voice[static_cast<std::size_t>(voice)], 2 * kTicksPerBeat))
              << ctx << " voice " << voice << ": chord-block bar must strike half notes";
        }
      }
    }
    EXPECT_GT(block_bars, 1) << "seed " << seed;
  }
}

// Perpetuus: continuous figuration closed by a single chord-block bar.
TEST(FormSectionalTest, PerpetuusClosesWithOneChordBlock) {
  for (std::uint32_t seed : {1u, 5u, 9u}) {  // seed % 4 == 1 -> Perpetuus.
    const HarnessFixture fx = buildFixture(FormType::ToccataAndFugue, seed, false, 32);
    const ComposeResult r = Composer{}.run(fx.material, fx.harmony, fx.voice_plan);
    ASSERT_EQ(r.validation.status, ValidationStatus::Ok) << "seed " << seed;
    const int free_bars = freeBarsFor(32);
    const auto bars = freeBarsByVoice(r, free_bars);
    for (int bar = 0; bar < free_bars; ++bar) {
      const auto& by_voice = bars[static_cast<std::size_t>(bar)];
      const std::string ctx = "seed " + std::to_string(seed) + " bar " + std::to_string(bar);
      ASSERT_FALSE(by_voice[0].empty()) << ctx;
      if (bar == free_bars - 1) {
        EXPECT_TRUE(allDurationsAre(by_voice[0], 2 * kTicksPerBeat))
            << ctx << ": the closing bar is a chord block";
      } else {
        for (const NoteEvent* n : by_voice[0])
          EXPECT_LE(n->duration, kTicksPerBeat / 2) << ctx << ": running figuration";
      }
    }
  }
}

// Fantasia: a Chordal section alternates half-note chord blocks with running
// bars, and a Free section opens with the rhetorical gesture (sixteenths, the
// bar tail silent).
TEST(FormSectionalTest, FantasiaChordalBlocksAndFreeGesture) {
  for (std::uint32_t seed : {1u, 2u, 3u, 4u, 5u}) {
    // 64 bars -> a 24-bar free section (six 4-bar sections), so every style of
    // the rotation appears regardless of the seed.
    const HarnessFixture fx = buildFixture(FormType::FantasiaAndFugue, seed, false, 64);
    const ComposeResult r = Composer{}.run(fx.material, fx.harmony, fx.voice_plan);
    ASSERT_EQ(r.validation.status, ValidationStatus::Ok) << "seed " << seed;
    const int free_bars = freeBarsFor(64);
    const auto bars = freeBarsByVoice(r, free_bars);
    bool saw_chordal_block = false;
    bool saw_gesture = false;
    for (const auto& section : fx.material.fantasia_sections) {
      const int sec_start = static_cast<int>(section.start_tick / kBar);
      if (sec_start >= free_bars)
        continue;
      const auto& head = bars[static_cast<std::size_t>(sec_start)];
      if (section.style == FantasiaStyle::Chordal && !head[0].empty()) {
        saw_chordal_block = true;
        EXPECT_TRUE(allDurationsAre(head[0], 2 * kTicksPerBeat))
            << "seed " << seed << ": Chordal section head must strike half-note blocks";
      }
      if (section.style == FantasiaStyle::Free && !head[0].empty()) {
        saw_gesture = true;
        Tick last_end = 0;
        for (const NoteEvent* n : head[0]) {
          EXPECT_EQ(n->duration, kTicksPerBeat / 4)
              << "seed " << seed << ": Free section opens with the sixteenth gesture";
          last_end = std::max(last_end, n->start_tick + n->duration);
        }
        EXPECT_LE(last_end, static_cast<Tick>(sec_start) * kBar + 2 * kTicksPerBeat)
            << "seed " << seed << ": the gesture leaves the bar tail silent";
      }
    }
    EXPECT_TRUE(saw_chordal_block) << "seed " << seed;
    EXPECT_TRUE(saw_gesture) << "seed " << seed;
  }
}

// --- Dramaticus material plan ------------------------------------------------

// The Dramaticus toccata's free section carries the designed materials, locked
// per bar from the composed output. When the section is long enough (>= 12 free
// bars) the BWV565 opening runs an octave cascade: bars 0-1 = the V0 solo
// gesture (sixteenths, bar tail silent) stated high then an octave lower, bar 2
// = the unison-gesture doubling (V0 + V1 sixteenths, V2 silent), bar 3 = the
// declamatory chord block, and the second window opens with the whole-note
// fermata breath. The minimal 8-bar section keeps the older two-bar gesture +
// bar-2 chord block. In every case the final free bar is a chord block, the two
// bars before it are the V2 walking-pedal solo, and the rest run the figuration.
TEST(FormSectionalTest, DramaticusFreeSectionCarriesDesignedMaterials) {
  for (std::uint32_t seed : {4u, 8u, 12u}) {  // seed % 4 == 0 -> Dramaticus.
    for (std::uint16_t total : {16, 32, 64}) {
      const HarnessFixture fx = buildFixture(FormType::ToccataAndFugue, seed, false, total);
      const ComposeResult r = Composer{}.run(fx.material, fx.harmony, fx.voice_plan);
      ASSERT_EQ(r.validation.status, ValidationStatus::Ok)
          << "seed " << seed << " total " << total << " first rule "
          << (r.validation.failures.empty() ? "" : r.validation.failures.front().rule_id);

      const int free_bars = freeBarsFor(total);
      const int pedal_solo = free_bars - 5;
      const bool cascade = free_bars >= 12;
      int flourish = ((free_bars / 4 + 3) / 4) * 4;
      flourish = std::max(4, std::min(flourish, free_bars - 4));
      // Per-bar, per-voice notes of the free section.
      std::vector<std::array<std::vector<const NoteEvent*>, 3>> bars(
          static_cast<std::size_t>(free_bars));
      for (const auto& n : r.notes) {
        const int bar = static_cast<int>(n.start_tick / kBar);
        if (bar < free_bars && n.voice < 3)
          bars[static_cast<std::size_t>(bar)][n.voice].push_back(&n);
      }

      auto expect_solo_gesture = [&](const std::array<std::vector<const NoteEvent*>, 3>& by_voice,
                                     int bar, const std::string& ctx) {
        EXPECT_FALSE(by_voice[0].empty()) << ctx;
        EXPECT_TRUE(by_voice[1].empty()) << ctx;
        EXPECT_TRUE(by_voice[2].empty()) << ctx;
        Tick last_end = 0;
        for (const NoteEvent* n : by_voice[0]) {
          EXPECT_EQ(n->duration, kTicksPerBeat / 4) << ctx;
          last_end = std::max(last_end, n->start_tick + n->duration);
        }
        EXPECT_LE(last_end, static_cast<Tick>(bar) * kBar + 2 * kTicksPerBeat)
            << ctx << ": gesture must leave the bar tail silent";
      };
      auto expect_chord_block = [&](const std::array<std::vector<const NoteEvent*>, 3>& by_voice,
                                    int bar, const std::string& ctx) {
        for (int voice = 0; voice < 3; ++voice) {
          ASSERT_FALSE(by_voice[static_cast<std::size_t>(voice)].empty()) << ctx;
          EXPECT_EQ(by_voice[static_cast<std::size_t>(voice)].front()->start_tick,
                    static_cast<Tick>(bar) * kBar)
              << ctx << " voice " << voice;
          for (const NoteEvent* n : by_voice[static_cast<std::size_t>(voice)])
            EXPECT_EQ(n->duration, 2 * kTicksPerBeat) << ctx << " voice " << voice;
        }
      };

      for (int bar = 0; bar < free_bars; ++bar) {
        const auto& by_voice = bars[static_cast<std::size_t>(bar)];
        const std::string ctx = "seed " + std::to_string(seed) + " total " + std::to_string(total) +
                                " bar " + std::to_string(bar);
        if (bar <= 1) {
          expect_solo_gesture(by_voice, bar, ctx);
        } else if (cascade && bar == 2) {
          // Unison-gesture doubling: V0 and V1 sixteenths, V1 exactly 12 below
          // V0 at equal ticks, V2 silent, the bar tail left silent.
          ASSERT_FALSE(by_voice[0].empty()) << ctx;
          ASSERT_EQ(by_voice[0].size(), by_voice[1].size()) << ctx;
          EXPECT_TRUE(by_voice[2].empty()) << ctx;
          for (std::size_t i = 0; i < by_voice[0].size(); ++i) {
            EXPECT_EQ(by_voice[0][i]->duration, kTicksPerBeat / 4) << ctx;
            EXPECT_EQ(static_cast<int>(by_voice[1][i]->pitch),
                      static_cast<int>(by_voice[0][i]->pitch) - 12)
                << ctx;
            EXPECT_EQ(by_voice[1][i]->start_tick, by_voice[0][i]->start_tick) << ctx;
          }
        } else if (cascade && bar == 3) {
          expect_chord_block(by_voice, bar, ctx);
        } else if (!cascade && bar == 2) {
          expect_chord_block(by_voice, bar, ctx);
        } else if (bar == free_bars - 1) {
          expect_chord_block(by_voice, bar, ctx);
        } else if (cascade && bar == flourish) {
          // Fermata breath: a single whole note struck in all three voices.
          for (int voice = 0; voice < 3; ++voice) {
            ASSERT_EQ(by_voice[static_cast<std::size_t>(voice)].size(), 1u)
                << ctx << " voice " << voice;
            EXPECT_EQ(by_voice[static_cast<std::size_t>(voice)].front()->duration,
                      static_cast<Tick>(kTicksPerBar))
                << ctx << " voice " << voice;
          }
        } else if (bar == pedal_solo || bar == pedal_solo + 1) {
          // Pedal solo: V2 walking quarters alone.
          EXPECT_TRUE(by_voice[0].empty()) << ctx;
          EXPECT_TRUE(by_voice[1].empty()) << ctx;
          ASSERT_EQ(by_voice[2].size(), 4u) << ctx;
          for (const NoteEvent* n : by_voice[2])
            EXPECT_EQ(n->duration, kTicksPerBeat) << ctx;
        } else {
          // Wave bar: running V0 figuration over both accompaniment layers.
          // The last-half bars tighten to sixteenth triplets (80 ticks), still
          // shorter than an eighth, so the density bound below holds either way.
          EXPECT_FALSE(by_voice[0].empty()) << ctx;
          EXPECT_FALSE(by_voice[1].empty()) << ctx;
          EXPECT_FALSE(by_voice[2].empty()) << ctx;
          for (const NoteEvent* n : by_voice[0])
            EXPECT_LE(n->duration, kTicksPerBeat / 2) << ctx << ": wave runs eighths/sixteenths";
        }
      }
    }
  }
}

// --- Dramaticus minor-mode diminished-seventh sweep (BWV565) -----------------

namespace {

// The internal leading-tone diminished-seventh pitch classes of the C-minor
// tonic used internally by every builder (transpose happens only at MIDI
// output): leading tone B (11) plus its stacked minor thirds -> {2, 5, 8, 11}.
// The set is symmetric under transposition by a minor third, so it is identical
// for any minor tonic.
const std::set<int> kDim7PitchClasses = {2, 5, 8, 11};

// Mirror the builder's Dramaticus sweep placement: with the cascade layout
// active (free_bars >= 12) the first sweep is the first wave bar after the
// fermata breath (flourish + 1) and the second is the last wave bar before the
// closing chord block (free_bars - 2). The two bars before that closing block
// are the V2 pedal-solo pair.
struct SweepBars {
  int first_bar;
  int last_bar;
};
SweepBars dramaticusSweepBars(int free_bars) {
  int flourish = ((free_bars / 4 + 3) / 4) * 4;
  flourish = std::clamp(flourish, 4, free_bars - 4);
  return {flourish + 1, free_bars - 2};
}

}  // namespace

// A minor-mode Dramaticus toccata (seed % 4 == 0) with the cascade layout
// injects two BWV565 diminished-seventh sweep bars: they contain ONLY the four
// leading-tone dim7 pitch classes, and each matches its neighbouring wave bar's
// subdivision (sixteenths, tightening to sixteenth triplets in the section's
// second half).
TEST(FormSectionalTest, DramaticusMinorDim7SweepBars) {
  for (std::uint32_t seed : {4u, 8u, 12u}) {
    ASSERT_EQ(seed % 4u, 0u) << "seed " << seed << " is not a Dramaticus archetype";
    for (std::uint16_t total : {std::uint16_t{32}, std::uint16_t{64}}) {
      const HarnessFixture fx = buildFixture(FormType::ToccataAndFugue, seed, true, total);
      const ComposeResult r = Composer{}.run(fx.material, fx.harmony, fx.voice_plan);
      ASSERT_TRUE(r.validation.failures.empty())
          << "seed " << seed << " total " << total << " first failure="
          << (r.validation.failures.empty() ? "" : r.validation.failures.front().rule_id);

      const int free_bars = freeBarsFor(total);
      ASSERT_GE(free_bars, 12) << "total " << total << " must use the cascade layout";
      const SweepBars sweep = dramaticusSweepBars(free_bars);

      for (int bar : {sweep.first_bar, sweep.last_bar}) {
        const std::vector<NoteEvent> notes = barVoiceNotes(r.notes, bar, 0);
        ASSERT_FALSE(notes.empty())
            << "seed " << seed << " total " << total << " sweep bar " << bar << " is empty";
        // Only the four leading-tone dim7 pitch classes sound.
        for (const NoteEvent& note : notes) {
          EXPECT_TRUE(kDim7PitchClasses.count(note.pitch % 12) == 1)
              << "seed " << seed << " total " << total << " sweep bar " << bar << " pitch "
              << static_cast<int>(note.pitch) << " (pc " << (note.pitch % 12)
              << ") is not a dim7 tone";
        }
        // Subdivision matches the neighbouring wave bars: sixteenths (120 ticks,
        // 16 notes) before the drive half, sixteenth triplets (80 ticks, 24
        // notes) at or after it.
        const bool triplet = bar >= free_bars / 2;
        const Tick expected_dur = triplet ? 80 : (kTicksPerBeat / 4);
        const std::size_t expected_count = triplet ? 24u : 16u;
        EXPECT_EQ(notes.size(), expected_count)
            << "seed " << seed << " total " << total << " sweep bar " << bar << " note count";
        for (const NoteEvent& note : notes) {
          EXPECT_EQ(note.duration, expected_dur)
              << "seed " << seed << " total " << total << " sweep bar " << bar << " subdivision";
        }
      }
    }
  }
}

// The major-mode Dramaticus plan is untouched: the two sweep-bar positions run
// the ordinary scalar-wave figuration (the major vii dim7 would need a pitch
// class outside the major scale), so each carries at least one non-dim7 tone.
TEST(FormSectionalTest, DramaticusMajorHasNoDim7Sweep) {
  for (std::uint32_t seed : {4u, 8u, 12u}) {
    for (std::uint16_t total : {std::uint16_t{32}, std::uint16_t{64}}) {
      const HarnessFixture fx = buildFixture(FormType::ToccataAndFugue, seed, false, total);
      const ComposeResult r = Composer{}.run(fx.material, fx.harmony, fx.voice_plan);
      ASSERT_TRUE(r.validation.failures.empty()) << "seed " << seed << " total " << total;

      const int free_bars = freeBarsFor(total);
      const SweepBars sweep = dramaticusSweepBars(free_bars);
      for (int bar : {sweep.first_bar, sweep.last_bar}) {
        const std::vector<NoteEvent> notes = barVoiceNotes(r.notes, bar, 0);
        ASSERT_FALSE(notes.empty()) << "seed " << seed << " total " << total << " bar " << bar;
        const bool all_dim7 = std::all_of(notes.begin(), notes.end(), [](const NoteEvent& note) {
          return kDim7PitchClasses.count(note.pitch % 12) == 1;
        });
        EXPECT_FALSE(all_dim7)
            << "seed " << seed << " total " << total << " major bar " << bar
            << " unexpectedly consists solely of dim7 tones (plan should be unchanged)";
      }
    }
  }
}

// A short Dramaticus form (free_bars < 12) uses no cascade layout, so it carries
// no dim7 sweep bar and still validates in minor mode.
TEST(FormSectionalTest, DramaticusShortFormHasNoDim7Sweep) {
  for (std::uint32_t seed : {4u, 8u, 12u}) {
    const std::uint16_t total = 16;  // free_bars == 8 (< 12): no cascade.
    ASSERT_LT(freeBarsFor(total), 12);
    const HarnessFixture fx = buildFixture(FormType::ToccataAndFugue, seed, true, total);
    const ComposeResult r = Composer{}.run(fx.material, fx.harmony, fx.voice_plan);
    ASSERT_TRUE(r.validation.failures.empty()) << "seed " << seed;

    const int free_bars = freeBarsFor(total);
    for (int bar = 0; bar < free_bars; ++bar) {
      const std::vector<NoteEvent> notes = barVoiceNotes(r.notes, bar, 0);
      // The sweep signature is a running V0 bar (>= 16 short notes) built solely
      // from dim7 tones; a chord block's two/three triad tones are excluded by
      // the count so a triad that happens to be a dim7 subset is not mistaken
      // for a sweep.
      const bool sweep_signature =
          notes.size() >= 16 && std::all_of(notes.begin(), notes.end(), [](const NoteEvent& note) {
            return kDim7PitchClasses.count(note.pitch % 12) == 1;
          });
      EXPECT_FALSE(sweep_signature)
          << "seed " << seed << " short-form bar " << bar << " is a dim7 sweep (should not exist)";
    }
  }
}

// The minor sweep must never introduce a validation failure across the archetype
// sweep in both modes (seeds 1..12 cover every seed % 4).
TEST(FormSectionalTest, ToccataDim7SweepValidatesAcrossSeedsBothModes) {
  for (std::uint32_t seed = 1; seed <= 12; ++seed) {
    for (bool minor : {false, true}) {
      for (std::uint16_t total : {std::uint16_t{32}, std::uint16_t{64}}) {
        const HarnessFixture fx = buildFixture(FormType::ToccataAndFugue, seed, minor, total);
        const ComposeResult r = Composer{}.run(fx.material, fx.harmony, fx.voice_plan);
        EXPECT_TRUE(r.validation.failures.empty())
            << "toccata seed " << seed << (minor ? " minor" : " major") << " total " << total
            << " first failure="
            << (r.validation.failures.empty() ? "" : r.validation.failures.front().rule_id);
      }
    }
  }
}

}  // namespace bach::composer
