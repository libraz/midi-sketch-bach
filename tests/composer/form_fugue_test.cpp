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

TEST(FormFugueTest, SubjectRhythmProfileIsApplied) {
  for (std::uint32_t seed : kSeeds) {
    const HarnessFixture fx =
        buildFixture(FormType::Fugue, seed, false, naturalBars(FormType::Fugue));
    const std::uint8_t slot = detail::subjectSlotFor(SubjectCharacter::Severe, seed);
    const auto& expected = detail::kPhase14SubjectRhythms[slot];

    ASSERT_GE(fx.material.subject.size(), expected.size());
    Tick cursor = 0;
    bool has_non_quarter = false;
    for (std::size_t i = 0; i < expected.size(); ++i) {
      EXPECT_EQ(fx.material.subject[i].start_tick, cursor) << "subject rhythm index " << i;
      EXPECT_EQ(fx.material.subject[i].duration, expected[i]) << "subject rhythm index " << i;
      has_non_quarter = has_non_quarter || expected[i] != kTicksPerBeat;
      cursor += expected[i];
    }
    EXPECT_TRUE(has_non_quarter);
    EXPECT_EQ(cursor, 4 * kTicksPerBar);
  }
}

TEST(FormFugueTest, DominantHeadSubjectUsesTonalAnswerInRealization) {
  const HarnessFixture fx = buildFixture(FormType::Fugue, 5, false, naturalBars(FormType::Fugue));
  EXPECT_TRUE(fx.material.use_tonal_answer);
  ASSERT_FALSE(fx.material.tonal_answer.empty());

  const ComposeResult r = Composer{}.run(fx.material, fx.harmony, fx.voice_plan);
  bool tonal_answer_bit = false;
  for (const auto& prov : r.provenance) {
    if (prov.voice_intent == VoiceIntent::AnswerCarrier &&
        (prov.satisfied_rules & ruleBitMask(RuleBit::TonalAnswerMapped)) != 0u) {
      tonal_answer_bit = true;
      break;
    }
  }
  EXPECT_TRUE(tonal_answer_bit);
}

TEST(FormFugueTest, CountersubjectAccompaniesAnswerAndThirdEntry) {
  const HarnessFixture fx = buildFixture(FormType::Fugue, 42, false, 64);
  ASSERT_FALSE(fx.material.countersubject.empty());

  bool answer_cs_span = false;
  bool third_entry_cs_span = false;
  for (const auto& span : fx.voice_plan.spans) {
    const int start_bar = static_cast<int>(span.start_tick / kTicksPerBar);
    if (span.intent == VoiceIntent::CountersubjectCarrier && span.voice == 0 && start_bar == 4) {
      answer_cs_span = true;
    }
    if (span.intent == VoiceIntent::CountersubjectCarrier && span.voice == 1 && start_bar == 8) {
      third_entry_cs_span = true;
    }
  }
  EXPECT_TRUE(answer_cs_span);
  EXPECT_TRUE(third_entry_cs_span);

  const ComposeResult r = Composer{}.run(fx.material, fx.harmony, fx.voice_plan);
  bool countersubject_bit = false;
  for (const auto& prov : r.provenance) {
    if (prov.voice_intent == VoiceIntent::CountersubjectCarrier &&
        (prov.satisfied_rules & ruleBitMask(RuleBit::CountersubjectActive)) != 0u) {
      countersubject_bit = true;
      break;
    }
  }
  EXPECT_TRUE(countersubject_bit);
  ASSERT_FALSE(r.validation.texture_metrics.empty());
  EXPECT_EQ(r.validation.texture_metrics[0].max_active_voices, 3);
}

TEST(FormFugueTest, ShortEpisodeAddsMiddleAndBassAccompaniment) {
  // The lone half-cycle episode of a short fugue is accompanied by a V1
  // figuration and a V2 bass (both verbatim Material), so the compact episode
  // exposes a full three-voice texture before the cadence.
  const HarnessFixture fx = buildFixture(FormType::Fugue, 42, false, 16);

  bool middle_span = false;
  bool bass_span = false;
  for (const auto& span : fx.voice_plan.spans) {
    const int start_bar = static_cast<int>(span.start_tick / kBar);
    if (span.intent != VoiceIntent::FigurationCarrier || start_bar != 8) {
      continue;
    }
    if (span.voice == 1) {
      middle_span = true;
    }
    if (span.voice == 2) {
      bass_span = true;
    }
  }
  EXPECT_TRUE(middle_span);
  EXPECT_TRUE(bass_span);

  const ComposeResult r = Composer{}.run(fx.material, fx.harmony, fx.voice_plan);
  EXPECT_TRUE(r.validation.failures.empty())
      << (r.validation.failures.empty() ? "" : r.validation.failures.front().rule_id);
  ASSERT_FALSE(r.validation.texture_metrics.empty());
  EXPECT_EQ(r.validation.texture_metrics[0].max_active_voices, 3);
}

TEST(FormFugueTest, EpisodesAccompanyFortspinnungWithBothMiddleAndBassVoices) {
  // Every full-length episode (a V0 Fortspinnung) is accompanied by BOTH a V1
  // figuration and a V2 bass over the same window, so all three voices sound
  // through the development instead of leaving the middle and/or bass register
  // empty. The accompaniment voices are verbatim Material confined to disjoint
  // bands, so the piece still validates clean.
  const HarnessFixture fx = buildFixture(FormType::Fugue, 42, false, 84);

  int episode_windows = 0;
  for (const auto& span : fx.voice_plan.spans) {
    if (span.intent != VoiceIntent::FortspinnungSpan) {
      continue;
    }
    ++episode_windows;
    bool middle_figuration = false;
    bool bass_figuration = false;
    for (const auto& peer : fx.voice_plan.spans) {
      if (peer.start_tick != span.start_tick || peer.end_tick != span.end_tick) {
        continue;
      }
      if (peer.intent == VoiceIntent::FigurationCarrier && peer.voice == 1) {
        middle_figuration = true;
      }
      if (peer.intent == VoiceIntent::FigurationCarrier && peer.voice == 2) {
        bass_figuration = true;
      }
    }
    EXPECT_TRUE(middle_figuration)
        << "episode at bar " << (span.start_tick / kBar) << " lacks a V1 accompaniment";
    EXPECT_TRUE(bass_figuration) << "episode at bar " << (span.start_tick / kBar)
                                 << " lacks a V2 bass";
  }
  EXPECT_GT(episode_windows, 0) << "no Fortspinnung episodes in an 84-bar fugue";

  const ComposeResult r = Composer{}.run(fx.material, fx.harmony, fx.voice_plan);
  EXPECT_TRUE(r.validation.failures.empty())
      << (r.validation.failures.empty() ? "" : r.validation.failures.front().rule_id);
}

// Active chord root pitch class at `tick` from the harmonic plan.
namespace {
int chordRootAt(const HarnessFixture& fx, Tick tick) {
  int root = -1;
  for (const auto& chord : fx.harmony.chords) {
    if (chord.start_tick <= tick) {
      root = static_cast<int>(chord.root_pc);
    }
  }
  return root;
}

// Collect the notes of the FigurationSection whose window matches [start_tick,
// end_tick) on the given voice (the bass support is a verbatim Material section).
std::vector<MaterialNote> bassSupportNotes(const HarnessFixture& fx, VoiceId voice, Tick start_tick,
                                           Tick end_tick) {
  for (const auto& section : fx.material.figuration_sections) {
    if (section.voice == voice && section.start_tick == start_tick &&
        section.end_tick == end_tick) {
      return section.notes;
    }
  }
  return {};
}
}  // namespace

// The V2 bass support under a middle entry is a verbatim Material scalar-wave
// figuration (FigurationCarrier), placed after the V1 accompaniment so its span
// index follows it. The whole piece must validate clean.
TEST(FormFugueTest, SelectedMiddleEntriesAddBassHarmonicSupportAfterAccompaniment) {
  const HarnessFixture fx = buildFixture(FormType::Fugue, 2, false, 84);
  const int middle_entry_bar = 12;

  int figuration_index = -1;
  int support_index = -1;
  for (std::size_t i = 0; i < fx.voice_plan.spans.size(); ++i) {
    const auto& span = fx.voice_plan.spans[i];
    const int start_bar = static_cast<int>(span.start_tick / kBar);
    const int end_bar = static_cast<int>(span.end_tick / kBar);
    if (start_bar != middle_entry_bar || end_bar != middle_entry_bar + 4) {
      continue;
    }
    if (span.intent == VoiceIntent::FigurationCarrier && span.voice == 1) {
      figuration_index = static_cast<int>(i);
    }
    if (span.intent == VoiceIntent::FigurationCarrier && span.voice == 2) {
      support_index = static_cast<int>(i);
    }
  }

  ASSERT_GE(figuration_index, 0);
  ASSERT_GE(support_index, 0);
  EXPECT_GT(support_index, figuration_index);

  const ComposeResult r = Composer{}.run(fx.material, fx.harmony, fx.voice_plan);
  EXPECT_TRUE(r.validation.failures.empty())
      << (r.validation.failures.empty() ? "" : r.validation.failures.front().rule_id);
}

// Every middle-entry V2 bass-support figuration stays inside the V2 register
// band and opens each bar on a genuine chord tone (the scalar wave anchors on
// chord tones), so the bass supports the active harmony without crossing V1.
TEST(FormFugueTest, MiddleEntrySupportCenterTracksChordRoot) {
  const HarnessFixture fx = buildFixture(FormType::Fugue, 6, false, 84);
  constexpr int kV2BandFloor = 33;
  constexpr int kV2BandCeil = 50;
  bool found_support = false;

  for (const auto& span : fx.voice_plan.spans) {
    if (span.intent != VoiceIntent::FigurationCarrier || span.voice != 2) {
      continue;
    }
    const int start_bar = static_cast<int>(span.start_tick / kBar);
    if (start_bar < 12 || start_bar >= 80) {
      continue;  // limit to the development middle-entry supports.
    }
    const std::vector<MaterialNote> notes = bassSupportNotes(fx, 2, span.start_tick, span.end_tick);
    if (notes.empty()) {
      continue;
    }
    found_support = true;
    const int root_pc = chordRootAt(fx, span.start_tick);
    ASSERT_GE(root_pc, 0);
    for (const auto& note : notes) {
      EXPECT_GE(static_cast<int>(note.pitch), kV2BandFloor)
          << "bass below band at bar " << start_bar;
      EXPECT_LE(static_cast<int>(note.pitch), kV2BandCeil)
          << "bass above band at bar " << start_bar;
    }
    // The bar downbeat anchor is a chord tone (figuration_harmonic_consistency).
    const int pc = ((static_cast<int>(notes.front().pitch) % 12) + 12) % 12;
    const int third = (pc == (root_pc + 3) % 12) ? 3 : 4;  // major or minor third.
    const bool is_chord_tone =
        pc == root_pc % 12 || pc == (root_pc + third) % 12 || pc == (root_pc + 7) % 12;
    EXPECT_TRUE(is_chord_tone) << "bass downbeat not a chord tone at bar " << start_bar;
  }

  EXPECT_TRUE(found_support);
  const ComposeResult r = Composer{}.run(fx.material, fx.harmony, fx.voice_plan);
  EXPECT_TRUE(r.validation.failures.empty())
      << (r.validation.failures.empty() ? "" : r.validation.failures.front().rule_id);
}

// With fig_offset == 1 the early development middle entries omit the V2 bass
// support; only the later cycles (cycle >= 2) carry a V2 FigurationCarrier bass.
TEST(FormFugueTest, FigOffsetOneAddsLateMiddleEntryBassSupportOnly) {
  const HarnessFixture fx = buildFixture(FormType::Fugue, 1, false, 84);
  bool early_support = false;
  bool late_support = false;
  std::vector<int> middle_entry_starts;

  for (const auto& span : fx.voice_plan.spans) {
    if (span.intent == VoiceIntent::MiddleEntryCarrier) {
      middle_entry_starts.push_back(static_cast<int>(span.start_tick / kBar));
    }
  }
  ASSERT_GE(middle_entry_starts.size(), 2u);
  const int first_middle_entry =
      *std::min_element(middle_entry_starts.begin(), middle_entry_starts.end());

  for (const auto& span : fx.voice_plan.spans) {
    if (span.intent != VoiceIntent::FigurationCarrier || span.voice != 2) {
      continue;
    }
    const int start_bar = static_cast<int>(span.start_tick / kBar);
    const bool is_middle_entry_bar =
        std::find(middle_entry_starts.begin(), middle_entry_starts.end(), start_bar) !=
        middle_entry_starts.end();
    if (!is_middle_entry_bar) {
      continue;  // episode-bar bass supports are unconditional; ignore here.
    }
    if (start_bar == first_middle_entry) {
      early_support = true;
    }
    if (start_bar > first_middle_entry) {
      late_support = true;
    }
  }

  EXPECT_FALSE(early_support);
  EXPECT_TRUE(late_support);
  const ComposeResult r = Composer{}.run(fx.material, fx.harmony, fx.voice_plan);
  EXPECT_TRUE(r.validation.failures.empty())
      << (r.validation.failures.empty() ? "" : r.validation.failures.front().rule_id);
}

// The final coda subject (V0) is accompanied by a V2 FigurationCarrier bass over
// the same two-bar window.
TEST(FormFugueTest, CodaSubjectAddsBassHarmonicSupport) {
  const HarnessFixture fx = buildFixture(FormType::Fugue, 42, false, 84);
  const int coda_subject_bar = 80;
  bool found_coda_support = false;

  for (const auto& span : fx.voice_plan.spans) {
    const int start_bar = static_cast<int>(span.start_tick / kBar);
    const int end_bar = static_cast<int>(span.end_tick / kBar);
    if (span.intent == VoiceIntent::FigurationCarrier && span.voice == 2 &&
        start_bar == coda_subject_bar && end_bar == coda_subject_bar + 2) {
      found_coda_support = true;
      break;
    }
  }

  EXPECT_TRUE(found_coda_support);
}

TEST(FormFugueTest, TextureGateRunLimitHoldsOnRepresentativeSeeds) {
  for (std::uint32_t seed : {std::uint32_t{1}, std::uint32_t{4}, std::uint32_t{6}}) {
    const HarnessFixture fx = buildFixture(FormType::Fugue, seed, false, 84);
    const ComposeResult r = Composer{}.run(fx.material, fx.harmony, fx.voice_plan);
    ASSERT_FALSE(r.validation.texture_metrics.empty());
    int max_run = 0;
    for (const auto& voice : r.validation.texture_metrics[0].voices) {
      max_run = std::max(max_run, voice.max_repeated_run);
    }
    EXPECT_LE(max_run, 4) << "seed " << seed;
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

TEST(FormFugueTest, EntrySchedulerUsesDecileIntervalsForLongFugue) {
  const HarnessFixture fx = buildFixture(FormType::Fugue, 42, false, 84);
  std::vector<int> starts;
  for (const auto& span : fx.voice_plan.spans) {
    if (span.intent == VoiceIntent::MiddleEntryCarrier) {
      starts.push_back(static_cast<int>(span.start_tick / kBar));
    }
  }
  std::sort(starts.begin(), starts.end());
  starts.erase(std::unique(starts.begin(), starts.end()), starts.end());
  ASSERT_GE(starts.size(), 3u);

  std::vector<int> intervals;
  for (std::size_t i = 1; i < starts.size(); ++i) {
    intervals.push_back(starts[i] - starts[i - 1]);
  }
  EXPECT_TRUE(std::all_of(intervals.begin(), intervals.end(),
                          [](int interval) { return interval == 8 || interval == 10; }));
  EXPECT_TRUE(std::any_of(intervals.begin(), intervals.end(), [](int interval) {
    return interval != 8;
  })) << "entry scheduler remained fixed at 8 bars";
}

TEST(FormFugueTest, EpisodesUseAlternatingFortspinnungSequences) {
  const HarnessFixture fx = buildFixture(FormType::Fugue, 42, false, 84);
  ASSERT_GE(fx.material.sequence_templates.size(), 2u);

  bool saw_ascending = false;
  bool saw_descending = false;
  for (const auto& tmpl : fx.material.sequence_templates) {
    EXPECT_EQ(tmpl.voice, 0);
    EXPECT_EQ(tmpl.seed_pitches.size(), 4u);
    EXPECT_EQ(tmpl.seed_durations.size(), 4u);
    EXPECT_GE(tmpl.num_steps, 1);
    saw_ascending = saw_ascending || tmpl.pattern == SequencePattern::AscendingStep;
    saw_descending = saw_descending || tmpl.pattern == SequencePattern::DescendingStep;
  }
  EXPECT_TRUE(saw_ascending);
  EXPECT_TRUE(saw_descending);

  const ComposeResult r = Composer{}.run(fx.material, fx.harmony, fx.voice_plan);
  EXPECT_TRUE(r.validation.failures.empty())
      << (r.validation.failures.empty() ? "" : r.validation.failures.front().rule_id);
  EXPECT_FALSE(hasRule(r.validation, "sequence_pattern_consistency"));
}

TEST(FormFugueTest, StrettoOverlapRuleStaysSatisfied) {
  const HarnessFixture fx = buildFixture(FormType::Fugue, 42, false, 84);
  ASSERT_FALSE(fx.material.stretto_entries.empty());

  const ComposeResult r = Composer{}.run(fx.material, fx.harmony, fx.voice_plan);
  EXPECT_TRUE(r.validation.failures.empty())
      << (r.validation.failures.empty() ? "" : r.validation.failures.front().rule_id);
  EXPECT_FALSE(hasRule(r.validation, "stretto_overlap_valid"));

  bool emitted_stretto = false;
  for (const auto& prov : r.provenance) {
    if (prov.voice_intent == VoiceIntent::StrettoCarrier) {
      emitted_stretto = true;
      break;
    }
  }
  EXPECT_TRUE(emitted_stretto);
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

  // Every prelude figuration section bar's downbeat note must be a chord tone.
  // The parallel-aware scalar-wave figuration anchors the bar downbeat on a
  // genuine chord tone (as figuration_harmonic_consistency requires) while
  // off-downbeat beats relax to consonant diatonic tones, which widens the
  // parallel-free anchor options so the three figuration voices over one triad
  // are not forced into parallel fifths/octaves.
  for (const auto& section : fx.material.figuration_sections) {
    if (section.start_tick >= first_theme_tick) {
      continue;  // fugue-region figuration accompaniment.
    }
    for (const auto& note : section.notes) {
      // Only the bar downbeat is harmonically anchored to a chord tone.
      if (note.start_tick % kTicksPerBar != 0) {
        continue;
      }
      const ChordEvent* chord = chord_at(note.start_tick);
      ASSERT_NE(chord, nullptr);
      const int third = (chord->quality == ChordQuality::Minor) ? 3 : 4;
      const int pc = note.pitch % 12;
      const bool is_tone = pc == chord->root_pc % 12 || pc == (chord->root_pc + third) % 12 ||
                           pc == (chord->root_pc + 7) % 12;
      EXPECT_TRUE(is_tone) << "prelude downbeat note pc " << pc << " is not a chord tone of root "
                           << static_cast<int>(chord->root_pc);
    }
  }
}

}  // namespace bach::composer
