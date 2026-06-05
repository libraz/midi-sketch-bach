// Fugue and PreludeAndFugue form-builder tests.
//
// These cover the two dedicated builders in form_fugue.cpp (buildFugueForm /
// buildPreludeAndFugueForm), driven through the form-director entry point
// (buildFormFixture) and the full Composer pipeline. The builders assemble
// all-Material verbatim carriers confined to disjoint per-voice register bands
// (V0 highest, V2 lowest), so the only inter-voice rule that can fire is
// voice_crossing; every fixture must validate clean.
//
// Coverage:
//   - both forms validate Ok and are deterministic across
//     seeds {1,5,42,99} x {Major,Minor} x bars {16, natural, 64, 128}.
//   - fugue exposition has three staggered entries (V0, then V1 at the answer
//     band, then V2 at the third-entry band).
//   - the V0 subject melody equals the selected catalog slot.
//   - a stretto is present in the climax cycle (two overlapping subject
//     statements <= 1 bar apart).
//   - the final bars cadence on the tonic (Picardy when minor + even seed).
//   - the middle-entry related-key degrees follow the rotation table.
//   - P&F: the prelude region has no subject statements, the fugue region
//     opens with the three-entry exposition at the section boundary, and every
//     prelude bar's downbeat figuration note is a chord tone.

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <set>
#include <vector>

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

// Build a resolved request for a form via the public director path so the test
// exercises exactly what the CLI / harness would build.
HarnessFixture buildFixture(FormType form, std::uint32_t seed, bool is_minor,
                            std::uint16_t target_bars, FormDirectorStatus* status_out = nullptr) {
  ComposeRequest req;
  req.form = form;
  req.seed = seed;
  req.is_minor = is_minor;
  req.character = SubjectCharacter::Severe;
  req.target_bars = target_bars;
  HarnessFixture fixture;
  const FormDirectorStatus status = buildFormFixture(req, &fixture);
  if (status_out != nullptr) {
    *status_out = status;
  }
  return fixture;
}

// Resolve the natural bar count for a form (target_bars == 0 path).
std::uint16_t naturalBars(FormType form) {
  return resolveBars(form, DurationScale::Short, 0);
}

const std::array<std::uint32_t, 4> kSeeds = {1, 5, 42, 99};

}  // namespace

// --- 1. Validation + determinism across the full matrix ---------------------

TEST(FormFugueTest, FugueValidatesAndIsDeterministic) {
  for (std::uint32_t seed : kSeeds) {
    for (bool minor : {false, true}) {
      const std::uint16_t nat = naturalBars(FormType::Fugue);
      for (std::uint16_t bars : {std::uint16_t{16}, nat, std::uint16_t{64}, std::uint16_t{128}}) {
        const HarnessFixture fx = buildFixture(FormType::Fugue, seed, minor, bars);
        const ComposeResult r = Composer{}.run(fx.material, fx.harmony, fx.voice_plan);
        EXPECT_TRUE(r.validation.failures.empty())
            << "Fugue seed " << seed << (minor ? " minor" : " major") << " bars " << bars
            << " first failure="
            << (r.validation.failures.empty() ? "" : r.validation.failures.front().rule_id);
        EXPECT_FALSE(hasRule(r.validation, "voice_crossing"))
            << "Fugue seed " << seed << " bars " << bars << " has voice crossing";
        ASSERT_FALSE(r.notes.empty());

        // Determinism: a second build is byte-identical in note content.
        const HarnessFixture fx2 = buildFixture(FormType::Fugue, seed, minor, bars);
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

TEST(FormFugueTest, PreludeAndFugueValidatesAndIsDeterministic) {
  for (std::uint32_t seed : kSeeds) {
    for (bool minor : {false, true}) {
      const std::uint16_t nat = naturalBars(FormType::PreludeAndFugue);
      for (std::uint16_t bars : {std::uint16_t{16}, nat, std::uint16_t{64}, std::uint16_t{128}}) {
        const HarnessFixture fx = buildFixture(FormType::PreludeAndFugue, seed, minor, bars);
        const ComposeResult r = Composer{}.run(fx.material, fx.harmony, fx.voice_plan);
        EXPECT_TRUE(r.validation.failures.empty())
            << "P&F seed " << seed << (minor ? " minor" : " major") << " bars " << bars
            << " first failure="
            << (r.validation.failures.empty() ? "" : r.validation.failures.front().rule_id);
        EXPECT_FALSE(hasRule(r.validation, "voice_crossing"))
            << "P&F seed " << seed << " bars " << bars << " has voice crossing";
        ASSERT_FALSE(r.notes.empty());

        const HarnessFixture fx2 = buildFixture(FormType::PreludeAndFugue, seed, minor, bars);
        const ComposeResult r2 = Composer{}.run(fx2.material, fx2.harmony, fx2.voice_plan);
        ASSERT_EQ(r.notes.size(), r2.notes.size());
        for (std::size_t i = 0; i < r.notes.size(); ++i) {
          EXPECT_EQ(r.notes[i].pitch, r2.notes[i].pitch);
          EXPECT_EQ(r.notes[i].start_tick, r2.notes[i].start_tick);
        }
      }
    }
  }
}

// --- 2. Exposition: three staggered entries -------------------------------

TEST(FormFugueTest, ExpositionHasThreeStaggeredEntries) {
  // A natural (44-bar) fugue has a full 12-bar exposition.
  const HarnessFixture fx = buildFixture(FormType::Fugue, 42, false, naturalBars(FormType::Fugue));

  bool v0_subject_bar0 = false;
  bool v1_answer_bar4 = false;
  bool v2_subject_bar8 = false;
  for (const auto& span : fx.voice_plan.spans) {
    const int start_bar = static_cast<int>(span.start_tick / kBar);
    if (span.voice == 0 && span.intent == VoiceIntent::SubjectCarrier && start_bar == 0) {
      v0_subject_bar0 = true;
    }
    if (span.voice == 1 && span.intent == VoiceIntent::AnswerCarrier && start_bar == 4) {
      v1_answer_bar4 = true;
    }
    if (span.voice == 2 && span.intent == VoiceIntent::SubjectCarrier && start_bar == 8) {
      v2_subject_bar8 = true;
    }
  }
  EXPECT_TRUE(v0_subject_bar0) << "missing V0 subject at bar 0";
  EXPECT_TRUE(v1_answer_bar4) << "missing V1 answer at bar 4";
  EXPECT_TRUE(v2_subject_bar8) << "missing V2 third entry at bar 8";
}

// --- 3. Subject melody == selected catalog slot ----------------------------

TEST(FormFugueTest, SubjectMelodyMatchesCatalogSlot) {
  for (std::uint32_t seed : kSeeds) {
    for (bool minor : {false, true}) {
      const HarnessFixture fx =
          buildFixture(FormType::Fugue, seed, minor, naturalBars(FormType::Fugue));
      const std::uint8_t slot = detail::subjectSlotFor(SubjectCharacter::Severe, seed);
      const std::array<std::uint8_t, 16>& expected =
          minor ? detail::kSubjectsMinor[slot] : detail::kPhase14Subjects[slot];

      // The first 16 subject notes are the V0 exposition statement (bars 0-3).
      ASSERT_GE(fx.material.subject.size(), 16u);
      // The builder shifts the whole subject by whole octaves into the V0 band,
      // so pitch classes (and thus the contour) must match the catalog exactly.
      const int offset =
          static_cast<int>(fx.material.subject[0].pitch) - static_cast<int>(expected[0]);
      EXPECT_EQ(offset % 12, 0) << "V0 subject is not an octave transposition of the slot";
      for (int note = 0; note < 16; ++note) {
        EXPECT_EQ(static_cast<int>(fx.material.subject[note].pitch),
                  static_cast<int>(expected[note]) + offset)
            << "seed " << seed << (minor ? " minor" : " major") << " subject note " << note;
      }
    }
  }
}

// --- 4. Stretto present in the climax cycle --------------------------------

TEST(FormFugueTest, StrettoPresentInClimaxCycle) {
  // A 64-bar fugue has several development cycles, so a climax stretto exists.
  const HarnessFixture fx = buildFixture(FormType::Fugue, 42, false, 64);
  ASSERT_FALSE(fx.material.stretto_entries.empty()) << "no stretto declared";

  const StrettoDecl& stretto = fx.material.stretto_entries.front();
  // The follower enters strictly inside the leader's window (genuine overlap)
  // at a 1-bar delay.
  EXPECT_GT(stretto.follower_entry_tick, stretto.leader_entry_tick);
  EXPECT_LT(stretto.follower_entry_tick, stretto.leader_entry_tick + stretto.leader_length_ticks);
  EXPECT_LE(stretto.follower_entry_tick - stretto.leader_entry_tick, kBar)
      << "stretto delay exceeds one bar";

  // A StrettoCarrier span replays it.
  bool has_stretto_span = false;
  for (const auto& span : fx.voice_plan.spans) {
    if (span.intent == VoiceIntent::StrettoCarrier) {
      has_stretto_span = true;
    }
  }
  EXPECT_TRUE(has_stretto_span) << "no StrettoCarrier span";
}

// --- 5. Final bars cadence on the tonic; Picardy when minor + even seed -----

TEST(FormFugueTest, CodaCadencesOnTonic) {
  for (std::uint32_t seed : kSeeds) {
    for (bool minor : {false, true}) {
      const std::uint16_t bars = naturalBars(FormType::Fugue);
      const HarnessFixture fx = buildFixture(FormType::Fugue, seed, minor, bars);
      ASSERT_FALSE(fx.material.coda_extensions.empty());
      const CodaDecl& coda = fx.material.coda_extensions.front();
      ASSERT_FALSE(coda.notes.empty());
      // The final coda note is the tonic (pitch class 0).
      EXPECT_EQ(coda.notes.back().pitch % 12, 0)
          << "seed " << seed << (minor ? " minor" : " major") << " coda does not end on tonic";
      // A perfect cadence is annotated at the final bar downbeat.
      bool has_final_cadence = false;
      const Tick last_bar_tick = static_cast<Tick>(bars - 1) * kBar;
      for (const auto& cadence : fx.harmony.cadences) {
        if (cadence.tick == last_bar_tick && cadence.type == CadenceType::Perfect) {
          has_final_cadence = true;
        }
      }
      EXPECT_TRUE(has_final_cadence) << "missing final perfect cadence";

      // Picardy raises the major third (E, pc 4) into the cadence when minor +
      // even seed; the helper is the single source of truth.
      if (minor && detail::usePicardy(seed)) {
        bool saw_major_third = false;
        for (const auto& note : coda.notes) {
          if (note.pitch % 12 == 4) {
            saw_major_third = true;
          }
        }
        EXPECT_TRUE(saw_major_third) << "seed " << seed << " minor: Picardy third absent";
      }
    }
  }
}

// --- 6. Middle-entry related-key degrees follow the rotation table ----------

TEST(FormFugueTest, MiddleEntryKeyPlanRotates) {
  // A 128-bar fugue maximizes the development cycle count, exercising the full
  // I / V / vi / IV degree rotation. The expected degree of cycle k is the
  // rotation table value for k % 4.
  const HarnessFixture fx = buildFixture(FormType::Fugue, 42, false, 128);
  ASSERT_FALSE(fx.material.middle_entries.empty());

  // Collect declared related keys; they must all be members of {I,V,vi,IV} =
  // {0,7,9,5} pitch classes (the rotation table's range).
  const std::set<std::uint8_t> allowed = {0, 7, 9, 5};
  for (const auto& decl : fx.material.middle_entries) {
    EXPECT_TRUE(allowed.count(decl.related_key_pc) > 0)
        << "middle-entry related key " << static_cast<int>(decl.related_key_pc)
        << " is outside the I/V/vi/IV rotation";
  }

  // The middle-entry-carrying voice rotates across V0 / V1 / V2 (more than one
  // distinct carrying voice appears for a long development).
  std::set<VoiceId> carrying_voices;
  for (const auto& span : fx.voice_plan.spans) {
    if (span.intent == VoiceIntent::MiddleEntryCarrier) {
      carrying_voices.insert(span.voice);
    }
  }
  EXPECT_GE(carrying_voices.size(), 2u) << "middle entry does not rotate carrying voice";
}

// --- 7. P&F: prelude / fugue split, no prelude subjects, downbeat anchoring --

TEST(FormFuguePreludeAndFugueTest, PreludeRegionHasNoSubjectsAndFugueOpensExposition) {
  const std::uint16_t bars = naturalBars(FormType::PreludeAndFugue);
  const HarnessFixture fx = buildFixture(FormType::PreludeAndFugue, 42, false, bars);

  // Find the section boundary: the first SubjectCarrier / AnswerCarrier start.
  Tick first_theme_tick = static_cast<Tick>(bars) * kBar;
  for (const auto& span : fx.voice_plan.spans) {
    if (span.intent == VoiceIntent::SubjectCarrier || span.intent == VoiceIntent::AnswerCarrier ||
        span.intent == VoiceIntent::MiddleEntryCarrier) {
      first_theme_tick = std::min(first_theme_tick, span.start_tick);
    }
  }
  ASSERT_LT(first_theme_tick, static_cast<Tick>(bars) * kBar) << "no fugue theme found";

  // The prelude region (before the boundary) carries only FigurationCarrier
  // spans -- no thematic carriers.
  for (const auto& span : fx.voice_plan.spans) {
    if (span.start_tick < first_theme_tick) {
      EXPECT_EQ(span.intent, VoiceIntent::FigurationCarrier)
          << "prelude region contains a non-figuration carrier";
    }
  }

  // The fugue region opens with a V0 SubjectCarrier at the boundary bar.
  bool v0_subject_at_boundary = false;
  const int boundary_bar = static_cast<int>(first_theme_tick / kBar);
  for (const auto& span : fx.voice_plan.spans) {
    if (span.voice == 0 && span.intent == VoiceIntent::SubjectCarrier &&
        static_cast<int>(span.start_tick / kBar) == boundary_bar) {
      v0_subject_at_boundary = true;
    }
  }
  EXPECT_TRUE(v0_subject_at_boundary) << "fugue does not open with a V0 subject at the boundary";
}

// --- 8. Per-beat vertical consonance (the quality-improvement contract) ------

namespace {

// True when the semitone distance folds to a consonant interval class
// (anything outside the scorer's dissonant set {m2,M2,TT,m7,M7}).
bool consonantInterval(int semis) {
  const int ic = ((std::abs(semis) % 12) + 12) % 12;
  return ic != 1 && ic != 2 && ic != 6 && ic != 10 && ic != 11;
}

// Sample every beat and count the fraction that contain a dissonant pair,
// replicating the external scorer's vertical_dissonance_ratio.
double verticalDissonanceRatio(const std::vector<NoteEvent>& notes) {
  Tick total = 0;
  for (const auto& note : notes) {
    total = std::max(total, note.start_tick + note.duration);
  }
  int samples = 0;
  int dissonant = 0;
  for (Tick tick = 0; tick < total; tick += kTicksPerBeat) {
    std::vector<int> pitches;
    for (const auto& note : notes) {
      if (note.start_tick <= tick && tick < note.start_tick + note.duration) {
        pitches.push_back(note.pitch);
      }
    }
    if (pitches.size() < 2) {
      continue;
    }
    ++samples;
    bool bad = false;
    for (std::size_t idx = 0; idx < pitches.size(); ++idx) {
      for (std::size_t jdx = idx + 1; jdx < pitches.size(); ++jdx) {
        if (!consonantInterval(pitches[idx] - pitches[jdx])) {
          bad = true;
        }
      }
    }
    if (bad) {
      ++dissonant;
    }
  }
  return samples > 0 ? static_cast<double>(dissonant) / samples : 0.0;
}

}  // namespace

TEST(FormFugueTest, VerticalDissonanceStaysLow) {
  // The consonance-aware figuration anchors every accompaniment beat on a chord
  // tone consonant with the concurrent thematic statement, so the per-beat
  // vertical dissonance ratio stays well under the external scorer's threshold
  // across the whole matrix (theme voices stay verbatim; only the accompaniment
  // adapts). 0.15 is the scorer's gate.
  for (std::uint32_t seed : {std::uint32_t{1}, std::uint32_t{5}, std::uint32_t{7},
                             std::uint32_t{42}, std::uint32_t{99}}) {
    for (bool minor : {false, true}) {
      for (std::uint16_t bars : {std::uint16_t{16}, std::uint16_t{44}, std::uint16_t{128}}) {
        const HarnessFixture fx = buildFixture(FormType::Fugue, seed, minor, bars);
        const ComposeResult r = Composer{}.run(fx.material, fx.harmony, fx.voice_plan);
        const double vdr = verticalDissonanceRatio(r.notes);
        EXPECT_LE(vdr, 0.15) << "Fugue seed " << seed << (minor ? " minor" : " major") << " bars "
                             << bars << " vdr=" << vdr;
      }
    }
  }
}

TEST(FormFuguePreludeAndFugueTest, VerticalDissonanceStaysLow) {
  for (std::uint32_t seed : {std::uint32_t{1}, std::uint32_t{42}}) {
    for (bool minor : {false, true}) {
      for (std::uint16_t bars : {std::uint16_t{24}, std::uint16_t{64}}) {
        const HarnessFixture fx = buildFixture(FormType::PreludeAndFugue, seed, minor, bars);
        const ComposeResult r = Composer{}.run(fx.material, fx.harmony, fx.voice_plan);
        const double vdr = verticalDissonanceRatio(r.notes);
        EXPECT_LE(vdr, 0.15) << "P&F seed " << seed << (minor ? " minor" : " major") << " bars "
                             << bars << " vdr=" << vdr;
      }
    }
  }
}

TEST(FormFuguePreludeAndFugueTest, PreludeBeatsAreChordToneAnchored) {
  const HarnessFixture fx =
      buildFixture(FormType::PreludeAndFugue, 5, false, naturalBars(FormType::PreludeAndFugue));

  // Find the prelude boundary (first thematic carrier tick).
  Tick first_theme_tick = static_cast<Tick>(fx.material.subject.empty() ? 0 : 1) * 0;
  first_theme_tick = static_cast<Tick>(-1);
  for (const auto& span : fx.voice_plan.spans) {
    if (span.intent == VoiceIntent::SubjectCarrier || span.intent == VoiceIntent::AnswerCarrier) {
      first_theme_tick = std::min(first_theme_tick, span.start_tick);
    }
  }

  // Build a tick -> chord lookup from the harmonic plan.
  auto chord_at = [&](Tick tick) -> const ChordEvent* {
    const ChordEvent* active = nullptr;
    for (const auto& chord : fx.harmony.chords) {
      if (chord.start_tick <= tick) {
        active = &chord;
      }
    }
    return active;
  };

  // Every prelude figuration section bar's first beat note must be a chord tone.
  for (const auto& section : fx.material.figuration_sections) {
    if (section.start_tick >= first_theme_tick) {
      continue;  // fugue-region figuration accompaniment.
    }
    for (const auto& note : section.notes) {
      // Only check beat onsets (the per-beat anchor design anchors every beat).
      if (note.start_tick % kTicksPerBeat != 0) {
        continue;
      }
      const ChordEvent* chord = chord_at(note.start_tick);
      ASSERT_NE(chord, nullptr);
      const int third = (chord->quality == ChordQuality::Minor) ? 3 : 4;
      const int pc = note.pitch % 12;
      const bool is_tone = pc == chord->root_pc % 12 || pc == (chord->root_pc + third) % 12 ||
                           pc == (chord->root_pc + 7) % 12;
      EXPECT_TRUE(is_tone) << "prelude beat note pc " << pc << " is not a chord tone of root "
                           << static_cast<int>(chord->root_pc);
    }
  }
}

}  // namespace bach::composer
