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
//     statements <= 2 bars apart, vetted for sustained dissonance).
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
#include <map>
#include <set>
#include <vector>

#include "composer/character_profile.h"
#include "composer/composer.h"
#include "composer/figuration.h"
#include "composer/figuration_palette.h"
#include "composer/form_director.h"
#include "composer/harmonic_plan.h"
#include "composer/harness_fixture.h"
#include "composer/material.h"
#include "composer/minor_material.h"
#include "composer/ornament_pass.h"
#include "composer/subject_catalog.h"
#include "composer/validator.h"
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
      const std::uint8_t slot = detail::subjectIndexFor(SubjectCharacter::Severe, minor, seed);
      const std::array<std::uint8_t, 16>& expected =
          minor ? tables::kSubjectCatalogMinor[slot] : tables::kSubjectCatalogMajor[slot];

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
    const std::uint8_t slot = detail::subjectIndexFor(SubjectCharacter::Severe, false, seed);
    const auto& expected = tables::kSubjectCatalogMajorRhythms[slot];

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

  ASSERT_FALSE(fx.material.imitation_entries.empty());
  const ImitationEntry& entry = fx.material.imitation_entries.front();
  EXPECT_EQ(entry.follower_fragment, MaterialFragment::TonalAnswer);
  EXPECT_EQ(entry.distance_ticks,
            fx.material.tonal_answer.front().start_tick - fx.material.subject.front().start_tick);
  EXPECT_EQ(entry.interval_semis, static_cast<int>(fx.material.tonal_answer.front().pitch) -
                                      static_cast<int>(fx.material.subject.front().pitch));

  bool subject_head_stamped = false;
  bool tonal_head_stamped = false;
  for (std::size_t i = 0; i < r.notes.size(); ++i) {
    const bool stamped = static_cast<bool>(r.provenance[i].satisfied_rules &
                                           ruleBitMask(RuleBit::ImitationEntryMatched));
    if (r.notes[i].voice == entry.leader_voice &&
        r.notes[i].start_tick == fx.material.subject.front().start_tick) {
      subject_head_stamped = stamped;
    }
    if (r.notes[i].voice == entry.follower_voice &&
        r.notes[i].start_tick == fx.material.tonal_answer.front().start_tick) {
      tonal_head_stamped = stamped;
    }
  }
  EXPECT_TRUE(subject_head_stamped);
  EXPECT_TRUE(tonal_head_stamped);

  Material unused_real_mutation = fx.material;
  ASSERT_GT(unused_real_mutation.answer.size(), 5u);
  ++unused_real_mutation.answer[5].pitch;
  ValidationReport unused_report =
      Validator{}.validate(r.notes, r.provenance, fx.harmony, unused_real_mutation);
  EXPECT_FALSE(hasRule(unused_report, "imitation_entry_match"));
  EXPECT_FALSE(hasRule(unused_report, "imitation_entry_realization"));

  ComposeResult sounding_mutation = r;
  bool mutated = false;
  for (auto& note : sounding_mutation.notes) {
    if (note.voice == entry.follower_voice &&
        note.start_tick == fx.material.tonal_answer[5].start_tick) {
      ++note.pitch;
      mutated = true;
      break;
    }
  }
  ASSERT_TRUE(mutated);
  ValidationReport sounding_report = Validator{}.validate(
      sounding_mutation.notes, sounding_mutation.provenance, fx.harmony, fx.material);
  EXPECT_TRUE(hasRule(sounding_report, "imitation_entry_realization"));
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

// Sign (+1 / 0 / -1) of each consecutive melodic step in a pitch line.
std::vector<int> stepSigns(const std::vector<int>& pitches) {
  std::vector<int> signs;
  for (std::size_t idx = 1; idx < pitches.size(); ++idx) {
    const int diff = pitches[idx] - pitches[idx - 1];
    signs.push_back(diff > 0 ? 1 : (diff < 0 ? -1 : 0));
  }
  return signs;
}

// Negate a step-sign sequence: the diatonic inversion flips every step's
// direction while a repeated pitch (sign 0) stays a repeat.
std::vector<int> negateSigns(const std::vector<int>& signs) {
  std::vector<int> out;
  out.reserve(signs.size());
  for (int sign : signs) {
    out.push_back(-sign);
  }
  return out;
}

// Middle entries always restate the MAJOR subject catalog row (even in minor
// pieces the related keys are major), so its step signs are the upright
// reference every carrier window must match unless it is the inverted cycle.
std::vector<int> middleEntryCatalogSigns(std::uint32_t seed, bool minor) {
  const std::uint8_t slot = detail::subjectIndexFor(SubjectCharacter::Severe, minor, seed);
  const std::array<std::uint8_t, 16>& line = tables::kSubjectCatalogMajor[slot];
  std::vector<int> pitches(line.begin(), line.end());
  return stepSigns(pitches);
}

// Per-window step-sign sequences of the middle-entry carriers: each
// MiddleEntryCarrier span is sliced from its carrying voice's decl (a single
// decl per voice accumulates that voice's entries across cycles).
std::vector<std::vector<int>> middleEntryWindowSigns(const HarnessFixture& fx) {
  std::vector<std::vector<int>> windows;
  for (const auto& span : fx.voice_plan.spans) {
    if (span.intent != VoiceIntent::MiddleEntryCarrier) {
      continue;
    }
    const MiddleEntryDecl* decl = nullptr;
    for (const auto& candidate : fx.material.middle_entries) {
      if (candidate.voice == span.voice) {
        decl = &candidate;
        break;
      }
    }
    if (decl == nullptr) {
      continue;
    }
    std::vector<int> pitches;
    for (const auto& note : decl->notes) {
      if (note.start_tick >= span.start_tick && note.start_tick < span.end_tick) {
        pitches.push_back(static_cast<int>(note.pitch));
      }
    }
    if (pitches.size() >= 2) {
      windows.push_back(stepSigns(pitches));
    }
  }
  return windows;
}
}  // namespace

// A plain middle entry is accompanied by the recurring countersubject in the
// highest non-carrying voice (CountersubjectCarrier -- the designed
// consonant/contrary line, not a reactive wave), and the V2 bass support
// (FigurationCarrier) is placed after it so its span index follows. The whole
// piece must validate clean.
TEST(FormFugueTest, SelectedMiddleEntriesAddBassHarmonicSupportAfterAccompaniment) {
  const HarnessFixture fx = buildFixture(FormType::Fugue, 2, false, 84);
  const int middle_entry_bar = 12;

  int accompaniment_index = -1;
  int support_index = -1;
  for (std::size_t i = 0; i < fx.voice_plan.spans.size(); ++i) {
    const auto& span = fx.voice_plan.spans[i];
    const int start_bar = static_cast<int>(span.start_tick / kBar);
    const int end_bar = static_cast<int>(span.end_tick / kBar);
    if (start_bar != middle_entry_bar || end_bar != middle_entry_bar + 4) {
      continue;
    }
    if (span.intent == VoiceIntent::CountersubjectCarrier && span.voice == 1) {
      accompaniment_index = static_cast<int>(i);
    }
    if (span.intent == VoiceIntent::FigurationCarrier && span.voice == 2) {
      support_index = static_cast<int>(i);
    }
  }

  ASSERT_GE(accompaniment_index, 0);
  ASSERT_GE(support_index, 0);
  EXPECT_GT(support_index, accompaniment_index);

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
  // The follower enters strictly inside the leader's window (genuine overlap).
  // The delay is 1 or 2 bars -- the builder vets each canon configuration for
  // sustained dissonance and widens the delay when the 1-bar canon clashes.
  EXPECT_GT(stretto.follower_entry_tick, stretto.leader_entry_tick);
  EXPECT_LT(stretto.follower_entry_tick, stretto.leader_entry_tick + stretto.leader_length_ticks);
  EXPECT_LE(stretto.follower_entry_tick - stretto.leader_entry_tick, 2 * kBar)
      << "stretto delay exceeds two bars";

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

// The episode Fortspinnung is a 2-bar model that FILLS its stride (no
// one-motif-per-2-bars sputter) and is restated one diatonic step down per
// stride, in lockstep with the per-bar descending-fifths chord chain (a chord
// pair descends by a second every 2 bars). Consecutive strides of the same
// episode therefore open a step lower than the previous one.
TEST(FormFugueTest, EpisodesUseChainLockedFortspinnungSequences) {
  const HarnessFixture fx = buildFixture(FormType::Fugue, 42, false, 84);
  ASSERT_GE(fx.material.sequence_templates.size(), 2u);

  // Episode windows: consecutive strides are compared only within ONE episode
  // (adjacent windows can put two episodes back to back, and a new episode
  // re-aims its seed at its own target station).
  const auto same_episode = [&fx](Tick a, Tick b) {
    for (const auto& span : fx.voice_plan.spans) {
      if (span.intent == VoiceIntent::FortspinnungSpan && span.start_tick <= a &&
          b < span.end_tick) {
        return true;
      }
    }
    return false;
  };

  const Tick stride = 2 * kBar;
  for (std::size_t i = 0; i < fx.material.sequence_templates.size(); ++i) {
    const auto& tmpl = fx.material.sequence_templates[i];
    EXPECT_EQ(tmpl.voice, 0);
    EXPECT_GE(tmpl.num_steps, 1);
    ASSERT_EQ(tmpl.seed_pitches.size(), tmpl.seed_durations.size());
    Tick covered = 0;
    for (const Tick dur : tmpl.seed_durations) {
      covered += dur;
    }
    EXPECT_EQ(covered, stride) << "template " << i << " does not fill its 2-bar stride";
    if (i > 0) {
      const auto& prev = fx.material.sequence_templates[i - 1];
      if (prev.target_start_tick + stride == tmpl.target_start_tick &&
          same_episode(prev.target_start_tick, tmpl.target_start_tick)) {
        const int drop = static_cast<int>(prev.seed_pitches.front()) -
                         static_cast<int>(tmpl.seed_pitches.front());
        EXPECT_GE(drop, 1) << "stride at tick " << tmpl.target_start_tick
                           << " does not descend against the previous stride";
        EXPECT_LE(drop, 4) << "stride at tick " << tmpl.target_start_tick
                           << " leaps instead of stepping down";
      }
    }
  }

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

// --- 4b. Three-voice stretto pile-up at the climax --------------------------

namespace {

// A climax that piled a second follower into the third voice shows up as two
// StrettoDecls sharing one leader entry tick with distinct follower voices and
// distinct entry delays. Reports the shared leader tick via `out` and returns
// true when such a window exists.
bool threeVoiceStrettoLeaderTick(const HarnessFixture& fx, Tick* out) {
  const auto& strettos = fx.material.stretto_entries;
  for (std::size_t idx = 0; idx < strettos.size(); ++idx) {
    for (std::size_t peer = idx + 1; peer < strettos.size(); ++peer) {
      if (strettos[idx].leader_entry_tick != strettos[peer].leader_entry_tick) {
        continue;
      }
      if (strettos[idx].follower_voice != strettos[peer].follower_voice &&
          strettos[idx].follower_entry_tick != strettos[peer].follower_entry_tick) {
        *out = strettos[idx].leader_entry_tick;
        return true;
      }
    }
  }
  return false;
}

}  // namespace

// The climax cycle can state the subject in ALL THREE voices: the middle-entry
// leader plus two stretto followers, at staggered delays. At least one seed in
// 1..40 must commit a second follower at both 64 and 96 bars (otherwise the
// three-voice pile-up feature is dead), and every committing build must
// validate with three overlapping subject statements sharing the leader window.
TEST(FormFugueTest, ThreeVoiceStrettoPilesUpAtClimax) {
  for (std::uint16_t bars : {static_cast<std::uint16_t>(64), static_cast<std::uint16_t>(96)}) {
    bool committed = false;
    for (std::uint32_t seed = 1; seed <= 40 && !committed; ++seed) {
      for (bool minor : {false, true}) {
        const HarnessFixture fx = buildFixture(FormType::Fugue, seed, minor, bars);
        Tick leader_tick = 0;
        if (!threeVoiceStrettoLeaderTick(fx, &leader_tick)) {
          continue;
        }
        committed = true;

        // The two followers share the leader window with distinct voices/delays.
        std::set<VoiceId> follower_voices;
        std::set<Tick> follower_ticks;
        VoiceId leader_voice = 0;
        Tick leader_len = 0;
        int followers_here = 0;
        for (const auto& stretto : fx.material.stretto_entries) {
          if (stretto.leader_entry_tick != leader_tick) {
            continue;
          }
          ++followers_here;
          leader_voice = stretto.leader_voice;
          leader_len = stretto.leader_length_ticks;
          follower_voices.insert(stretto.follower_voice);
          follower_ticks.insert(stretto.follower_entry_tick);
          // Each follower enters strictly inside the leader window (overlap).
          EXPECT_GT(stretto.follower_entry_tick, leader_tick);
          EXPECT_LT(stretto.follower_entry_tick, leader_tick + stretto.leader_length_ticks);
        }
        EXPECT_EQ(followers_here, 2) << "seed " << seed << " bars " << bars;
        EXPECT_EQ(follower_voices.size(), 2u) << "followers must occupy distinct voices";
        EXPECT_EQ(follower_ticks.size(), 2u) << "followers must enter at distinct delays";
        EXPECT_EQ(follower_voices.count(leader_voice), 0u) << "a follower doubled the leader voice";

        // The leader is a genuine middle-entry subject statement in the window,
        // so leader + two followers = three overlapping subject statements.
        bool leader_statement = false;
        for (const auto& decl : fx.material.middle_entries) {
          if (decl.voice != leader_voice) {
            continue;
          }
          for (const auto& note : decl.notes) {
            if (note.start_tick >= leader_tick && note.start_tick < leader_tick + leader_len) {
              leader_statement = true;
            }
          }
        }
        EXPECT_TRUE(leader_statement) << "no leader subject statement in the climax window";

        const ComposeResult r = Composer{}.run(fx.material, fx.harmony, fx.voice_plan);
        EXPECT_TRUE(r.validation.failures.empty())
            << "seed " << seed << (minor ? " minor" : " major") << " bars " << bars
            << " first failure="
            << (r.validation.failures.empty() ? "" : r.validation.failures.front().rule_id);
        break;
      }
    }
    EXPECT_TRUE(committed) << "no seed in 1..40 committed a second stretto follower at " << bars
                           << " bars; the three-voice pile-up feature is dead";
  }
}

// A development with >= 4 entry cycles that passes the corpus-rate gate restates
// a single-follower stretto in the last entry cycle before the coda -- a SECOND
// stretto moment, distinct from the climax cycle. Seed 1 (seed % 23 = 1, under
// the kStrettoRate threshold) at 64 bars has six entry cycles, so it carries the
// climax stretto AND a pre-coda stretto at two distinct leader ticks.
TEST(FormFugueTest, SecondStrettoMomentRestatesBeforeCoda) {
  const HarnessFixture fx = buildFixture(FormType::Fugue, 1, false, 64);
  ASSERT_GE(fx.material.stretto_entries.size(), 2u);

  std::set<Tick> leader_ticks;
  for (const auto& stretto : fx.material.stretto_entries) {
    leader_ticks.insert(stretto.leader_entry_tick);
    // Every follower still overlaps its own leader window.
    EXPECT_GT(stretto.follower_entry_tick, stretto.leader_entry_tick);
    EXPECT_LT(stretto.follower_entry_tick, stretto.leader_entry_tick + stretto.leader_length_ticks);
  }
  // Two distinct leader ticks = the climax cycle plus a separate pre-coda cycle
  // (a three-voice pile-up shares ONE leader tick, so this is a second moment).
  EXPECT_GE(leader_ticks.size(), 2u) << "no stretto restated outside the climax cycle";

  const ComposeResult r = Composer{}.run(fx.material, fx.harmony, fx.voice_plan);
  EXPECT_TRUE(r.validation.failures.empty())
      << (r.validation.failures.empty() ? "" : r.validation.failures.front().rule_id);
  EXPECT_FALSE(hasRule(r.validation, "stretto_overlap_valid"));
}

// The dominant pedal is proportional to development weight: any development with
// >= 2 entry-carrying cycles admits it. A 30-bar fugue (below the former N >= 32
// gate, so previously pedal-less) has two entry cycles, so a suitable seed now
// declares a dominant pedal; a fugue with < 2 entry cycles (16 or 24 bars) still
// declares none.
TEST(FormFugueTest, PedalPointIsProportionalToEntryCycles) {
  // 30 bars, seed 4 minor: two entry cycles, the climax stretto vacates its
  // slot, and the dominant pedal lands on the last entry cycle before the coda.
  const HarnessFixture with_pedal = buildFixture(FormType::Fugue, 4, true, 30);
  EXPECT_FALSE(with_pedal.material.pedal_points.empty())
      << "30-bar fugue with two entry cycles declared no dominant pedal";
  const ComposeResult r_with =
      Composer{}.run(with_pedal.material, with_pedal.harmony, with_pedal.voice_plan);
  EXPECT_TRUE(r_with.validation.failures.empty())
      << (r_with.validation.failures.empty() ? "" : r_with.validation.failures.front().rule_id);

  // Fewer than two entry cycles: no pedal regardless of seed/mode.
  for (std::uint16_t bars : {static_cast<std::uint16_t>(16), static_cast<std::uint16_t>(24)}) {
    for (std::uint32_t seed = 1; seed <= 6; ++seed) {
      for (bool minor : {false, true}) {
        const HarnessFixture fx = buildFixture(FormType::Fugue, seed, minor, bars);
        EXPECT_TRUE(fx.material.pedal_points.empty())
            << "bars " << bars << " seed " << seed << (minor ? " minor" : " major")
            << " declared a pedal with < 2 entry cycles";
      }
    }
  }
}

// The stretto-density and pedal changes are all development-section, so the
// whole fugue family stays valid across the seed / length / mode matrix.
TEST(FormFugueTest, StrettoDensityAndPedalStayValidAcrossMatrix) {
  for (FormType form : {FormType::Fugue, FormType::PreludeAndFugue}) {
    for (std::uint32_t seed = 1; seed <= 10; ++seed) {
      for (std::uint16_t bars : {static_cast<std::uint16_t>(24), static_cast<std::uint16_t>(64),
                                 static_cast<std::uint16_t>(96)}) {
        for (bool minor : {false, true}) {
          const HarnessFixture fx = buildFixture(form, seed, minor, bars);
          const ComposeResult r = Composer{}.run(fx.material, fx.harmony, fx.voice_plan);
          EXPECT_TRUE(r.validation.failures.empty())
              << "form " << static_cast<int>(form) << " seed " << seed
              << (minor ? " minor" : " major") << " bars " << bars << " first failure="
              << (r.validation.failures.empty() ? "" : r.validation.failures.front().rule_id);
        }
      }
    }
  }
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

      // Picardy raises the major third (E, pc 4) into the close when minor +
      // even seed; the V1 inner voice holds the closing third (the V0 landing
      // holds only the tonic), so the scan covers the figuration sections.
      if (minor && detail::usePicardy(seed)) {
        bool saw_major_third = false;
        for (const auto& section : fx.material.figuration_sections) {
          for (const auto& note : section.notes) {
            if (note.start_tick >= last_bar_tick && note.pitch % 12 == 4) {
              saw_major_third = true;
            }
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

// The dominant-pedal cycle must stay a three-voice texture: held pedal,
// subject statement, and a figuration counterline in the remaining voice.
// Without the counterline the cycle is two voices, and once the subject
// reaches its long-note tail the pedal bars decay to one or two attacks per
// bar -- an audible collapse at the very spot that should build to the coda.
TEST(FormFugueTest, PedalCycleKeepsThreeVoiceTexture) {
  for (std::uint16_t bars : {static_cast<std::uint16_t>(84), static_cast<std::uint16_t>(128)}) {
    for (std::uint32_t seed : {8u, 42u}) {
      const HarnessFixture fx = buildFixture(FormType::Fugue, seed, false, bars);
      ASSERT_FALSE(fx.material.pedal_points.empty())
          << "seed " << seed << " bars " << bars << ": no dominant pedal declared";
      const PedalPointDecl& pedal = fx.material.pedal_points.front();
      const Tick window_lo = pedal.start_tick;
      const Tick window_hi = pedal.start_tick + pedal.duration;
      const ComposeResult r = Composer{}.run(fx.material, fx.harmony, fx.voice_plan);
      std::array<int, 3> onsets = {0, 0, 0};
      for (const auto& note : r.notes) {
        if (note.start_tick >= window_lo && note.start_tick < window_hi && note.voice < 3) {
          ++onsets[note.voice];
        }
      }
      int free_voice_onsets = 0;
      for (int v = 0; v < 3; ++v) {
        EXPECT_GE(onsets[static_cast<std::size_t>(v)], 1)
            << "seed " << seed << " bars " << bars << ": voice " << v
            << " is silent through the pedal cycle";
        if (v != static_cast<int>(pedal.voice)) {
          free_voice_onsets = std::max(free_voice_onsets, onsets[static_cast<std::size_t>(v)]);
        }
      }
      // The busier non-pedal voice must be a genuine counterline (the entry
      // voice thins to long notes in the subject tail, so the floor sits on
      // the figuration voice).
      EXPECT_GE(free_voice_onsets, 6)
          << "seed " << seed << " bars " << bars << ": pedal cycle has no counterline";
    }
  }
}

// A figuration line must never STAY collapsed into a two-pitch oscillation:
// the consonance / harshness / parallel vetoes against a sounding theme entry
// can shrink the wave's admissible set to two tones, and an a-b-a-b wobble
// that persists across bars reads as a stuck mechanism rather than a line
// (the wave's wobble breaker escapes to a farther chord tone instead). A
// SINGLE such bar is admissible -- a one-bar written-out alternation over a
// stable chord is an idiomatic trill figure -- so only two or more
// consecutive collapsed bars fail.
TEST(FormFugueTest, FigurationBarsNeverStayCollapsedOnTwoPitchOscillation) {
  for (FormType form : {FormType::Fugue, FormType::PreludeAndFugue}) {
    for (std::uint32_t seed : {1u, 8u, 12u, 42u, 99u}) {
      for (std::uint16_t bars : {static_cast<std::uint16_t>(84), static_cast<std::uint16_t>(128)}) {
        const HarnessFixture fx = buildFixture(form, seed, false, bars);
        for (const auto& section : fx.material.figuration_sections) {
          std::map<Tick, std::set<int>> bar_pitches;
          std::map<Tick, int> bar_counts;
          for (const auto& note : section.notes) {
            const Tick bar = note.start_tick / kBar;
            bar_pitches[bar].insert(static_cast<int>(note.pitch));
            ++bar_counts[bar];
          }
          Tick prev_collapsed_bar = -2;
          for (const auto& [bar, pitches] : bar_pitches) {
            if (bar_counts[bar] < 5 || pitches.size() >= 3u) {
              continue;  // sparse bars (quarter waves, section edges) cannot wobble audibly.
            }
            EXPECT_NE(bar, prev_collapsed_bar + 1)
                << "form " << static_cast<int>(form) << " seed " << seed << " bars " << bars
                << ": figuration voice " << static_cast<int>(section.voice) << " bars " << bar
                << "-" << bar + 1 << " stay collapsed on a two-pitch oscillation";
            prev_collapsed_bar = bar;
          }
        }
      }
    }
  }
}

// The vi middle entry is the relative NATURAL minor — a diatonic degree shift
// whose every pitch stays inside the home (C-major) scale. A real +9
// transposition would state it in A MAJOR, whose C#/F#/G# read as a second key
// against the home-key figuration around the entry.
TEST(FormFugueTest, ViMiddleEntryStaysInsideHomeScale) {
  const std::set<int> home_scale = {0, 2, 4, 5, 7, 9, 11};
  for (std::uint32_t seed : {8u, 42u, 99u}) {
    const HarnessFixture fx = buildFixture(FormType::Fugue, seed, false, 128);
    bool saw_vi = false;
    for (const auto& decl : fx.material.middle_entries) {
      if (decl.related_key_pc != 9) {
        continue;
      }
      saw_vi = true;
      for (const auto& note : decl.notes) {
        EXPECT_TRUE(home_scale.count(note.pitch % 12) > 0)
            << "seed " << seed << ": vi middle-entry pitch " << static_cast<int>(note.pitch)
            << " (pc " << static_cast<int>(note.pitch % 12) << ") leaves the home scale";
      }
    }
    EXPECT_TRUE(saw_vi) << "seed " << seed << ": 128-bar fugue declared no vi middle entry";
  }
}

// A development long enough to spare a canonical restatement (>= 3 entry
// cycles) states the subject inverted in exactly ONE middle entry -- the
// melodic mirror in scale-degree space. A 64-bar fugue uses the uniform
// 8-bar schedule (six entry cycles), so the inverted cycle is deterministic
// across seeds and modes. Every other carrier window stays upright.
TEST(FormFugueTest, OneMiddleEntryIsDiatonicInversion) {
  for (std::uint32_t seed : kSeeds) {
    for (bool minor : {false, true}) {
      const HarnessFixture fx = buildFixture(FormType::Fugue, seed, minor, 64);
      const std::vector<int> upright = middleEntryCatalogSigns(seed, minor);
      const std::vector<int> inverted = negateSigns(upright);
      const std::vector<std::vector<int>> windows = middleEntryWindowSigns(fx);
      ASSERT_GE(windows.size(), 3u)
          << "seed " << seed << (minor ? " minor" : " major")
          << ": 64-bar fugue has too few middle-entry cycles to invert one";
      int inverted_count = 0;
      int upright_count = 0;
      for (const std::vector<int>& signs : windows) {
        if (signs == upright) {
          ++upright_count;
        } else if (signs == inverted) {
          ++inverted_count;
        } else {
          ADD_FAILURE() << "seed " << seed << (minor ? " minor" : " major")
                        << ": a middle-entry window is neither the upright subject "
                           "nor its diatonic inversion";
        }
      }
      EXPECT_EQ(inverted_count, 1) << "seed " << seed << (minor ? " minor" : " major")
                                   << ": expected exactly one inverted middle entry";
      EXPECT_EQ(upright_count, static_cast<int>(windows.size()) - 1)
          << "seed " << seed << (minor ? " minor" : " major")
          << ": non-inverted middle entries must stay upright";
    }
  }
}

// The inverted middle entry must not break validation: the diatonic mirror
// stays inside the declared related key, so middle_entry_in_related_key and
// every other rule stay clean for development-heavy fugues and prelude+fugue
// pairs across a broad seed range.
TEST(FormFugueTest, InvertedMiddleEntryValidatesAcrossSeeds) {
  for (FormType form : {FormType::Fugue, FormType::PreludeAndFugue}) {
    for (std::uint32_t seed = 1; seed <= 10; ++seed) {
      for (bool minor : {false, true}) {
        const HarnessFixture fx = buildFixture(form, seed, minor, 96);
        const ComposeResult r = Composer{}.run(fx.material, fx.harmony, fx.voice_plan);
        EXPECT_TRUE(r.validation.failures.empty())
            << "form " << static_cast<int>(form) << " seed " << seed
            << (minor ? " minor" : " major") << " first failure="
            << (r.validation.failures.empty() ? "" : r.validation.failures.front().rule_id);
        EXPECT_FALSE(hasRule(r.validation, "middle_entry_in_related_key"))
            << "form " << static_cast<int>(form) << " seed " << seed
            << ": inverted entry left the related key";
      }
    }
  }
}

// --- 6b. Recurring countersubject identity + fallback safety ---------------

namespace {

// Scale-degree index of a home-scale (C major) diatonic pitch: seven degrees
// per octave plus the pitch class's position in the scale. Both committed
// restatements and reactive fallbacks are home-diatonic, so every
// countersubject note maps to a degree.
int majorDegreeIndex(int pitch) {
  static const std::array<int, 12> kPosInScale = {0, -1, 1, -1, 2, 3, -1, 4, -1, 5, -1, 6};
  const int pc = ((pitch % 12) + 12) % 12;
  const int pos = kPosInScale[static_cast<std::size_t>(pc)];
  if (pos < 0) {
    return -1;
  }
  return 7 * (pitch / 12) + pos;
}

// Degree-space interval sequence between consecutive notes. Degree shifting the
// canonical line by a constant number of degrees plus whole octaves preserves
// this sequence, so a committed restatement matches the canonical exactly.
std::vector<int> degreeIntervals(const std::vector<int>& pitches) {
  std::vector<int> intervals;
  for (std::size_t idx = 1; idx < pitches.size(); ++idx) {
    intervals.push_back(majorDegreeIndex(pitches[idx]) - majorDegreeIndex(pitches[idx - 1]));
  }
  return intervals;
}

// Countersubject notes whose onset falls in [start, end).
std::vector<int> countersubjectPitchesIn(const HarnessFixture& fx, Tick start, Tick end) {
  std::vector<int> pitches;
  for (const auto& note : fx.material.countersubject) {
    if (note.start_tick >= start && note.start_tick < end) {
      pitches.push_back(static_cast<int>(note.pitch));
    }
  }
  return pitches;
}

}  // namespace

// The fugue states ONE canonical countersubject (the exposition answer
// counterline) and restates it against later entries by octave-invertible
// degree shift into the entry key. Every committed middle-entry restatement
// therefore reproduces the canonical line's degree-space interval sequence
// exactly. At least one window must commit across the tested seeds -- if all
// fall back, the recurring-identity feature is dead.
TEST(FormFugueTest, RecurringCountersubjectRestatesCanonicalIdentity) {
  int total_committed = 0;
  int total_windows = 0;
  for (std::uint32_t seed : {1u, 5u, 42u}) {
    const HarnessFixture fx = buildFixture(FormType::Fugue, seed, false, 64);
    // Canonical countersubject = the exposition answer counterline (V0, bars 4-8).
    const std::vector<int> canonical_intervals =
        degreeIntervals(countersubjectPitchesIn(fx, 4 * kBar, 8 * kBar));
    ASSERT_FALSE(canonical_intervals.empty()) << "seed " << seed << ": no canonical countersubject";

    for (const auto& span : fx.voice_plan.spans) {
      if (span.intent != VoiceIntent::CountersubjectCarrier || span.start_tick < 12 * kBar) {
        continue;  // only development (middle-entry) countersubject windows.
      }
      ++total_windows;
      const std::vector<int> window_intervals =
          degreeIntervals(countersubjectPitchesIn(fx, span.start_tick, span.end_tick));
      if (window_intervals == canonical_intervals) {
        ++total_committed;
      }
    }
  }
  EXPECT_GT(total_windows, 0) << "no middle-entry countersubject windows found";
  EXPECT_GT(total_committed, 0)
      << "recurring countersubject never committed across seeds {1,5,42}; the feature is dead";
}

// The reactive fallback keeps every combination valid: whenever a restatement
// would combine dissonantly with the entry line the window falls back to a
// consonant/contrary counterline, so validation stays Ok across a broad seed
// range for both fugue forms in major and minor at development length.
TEST(FormFugueTest, RecurringCountersubjectFallbackStaysValid) {
  for (FormType form : {FormType::Fugue, FormType::PreludeAndFugue}) {
    for (std::uint32_t seed = 1; seed <= 10; ++seed) {
      for (bool minor : {false, true}) {
        const HarnessFixture fx = buildFixture(form, seed, minor, 64);
        const ComposeResult r = Composer{}.run(fx.material, fx.harmony, fx.voice_plan);
        EXPECT_TRUE(r.validation.failures.empty())
            << "form " << static_cast<int>(form) << " seed " << seed
            << (minor ? " minor" : " major") << " first failure="
            << (r.validation.failures.empty() ? "" : r.validation.failures.front().rule_id);
      }
    }
  }
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

// Latest-onset pitch of `voice` sounding at `tick`, or -1 when silent. Mirrors
// ThemeToneRegistry::soundingPitchInVoice over the composed NoteEvent stream so
// the consonance probes read exactly what plays at a beat.
int latestSoundingPitch(const std::vector<NoteEvent>& notes, VoiceId voice, Tick tick) {
  int pitch = -1;
  Tick best_start = 0;
  bool found = false;
  for (const auto& note : notes) {
    if (note.voice != voice) {
      continue;
    }
    if (note.start_tick <= tick && tick < note.start_tick + note.duration &&
        (!found || note.start_tick >= best_start)) {
      best_start = note.start_tick;
      pitch = note.pitch;
      found = true;
    }
  }
  return pitch;
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

TEST(FormFuguePreludeAndFugueTest, FigurationStaysDiatonic) {
  // The scalar-wave figuration walks scaleUp / scaleDown, so every figuration
  // note must stay inside the diatonic set. A descending walk expressed as the
  // negation trick -scaleUp(-pitch) walked the inverted (non-diatonic) pitch
  // class set and emitted chromatic runs (D#/C#/A#/G# in C major); this pins
  // the diatonic contract across seeds and modes. In minor the bar chords come
  // from kHarmonyPatternsMinor whose major dominant contributes the harmonic
  // leading tone (pc 11), so that single chromatic degree is admitted; the
  // held Picardy third (pc 4) in the final bar's inner voice is likewise a
  // sanctioned chromatic colour.
  for (std::uint32_t seed : kSeeds) {
    for (bool minor : {false, true}) {
      const std::uint16_t bars = naturalBars(FormType::PreludeAndFugue);
      const HarnessFixture fx = buildFixture(FormType::PreludeAndFugue, seed, minor, bars);
      const detail::Mode mode = minor ? detail::Mode::Minor : detail::Mode::Major;
      const Tick last_bar_tick = static_cast<Tick>(bars - 1) * kBar;
      for (const auto& section : fx.material.figuration_sections) {
        for (const auto& note : section.notes) {
          const bool picardy_third =
              minor && note.pitch % 12 == 4 && note.start_tick >= last_bar_tick;
          const bool diatonic = detail::inScale(note.pitch, mode) ||
                                (minor && note.pitch % 12 == 11) || picardy_third;
          EXPECT_TRUE(diatonic) << "seed " << seed << (minor ? " minor" : " major")
                                << " figuration note pitch " << note.pitch << " (pc "
                                << note.pitch % 12 << ") at tick " << note.start_tick
                                << " is outside the diatonic set";
        }
      }
    }
  }
}

// --- 9. Dissonance-fix regressions ------------------------------------------
// These lock in three fixes to the figuration/canon machinery:
//   1. the climax stretto follower is transposed into the leader's related key
//      so the overlap is a single-key octave canon (form_fugue.cpp);
//   2. the coda replays the V0 subject head into theme_tones, adds a V1 alto
//      figuration, and anchors the V2 bass consonantly under both;
//   3. consonantChordTone's window_pitches tie-breaker keeps a sustained anchor
//      consonant against mid-beat attacks of faster lines already placed
//      (texture_helpers.cpp).

// The climax-cycle stretto follower restates the subject transposed by exactly
// interval_semis (the validated verbatim relation), and because the follower
// sits in the leader's canon key the leader/follower form a single-key canon:
// strong-beat (downbeat) overlaps are consonant apart from the couple of
// passing seconds a self-canon legitimately produces.
TEST(FormFugueTest, StrettoFollowerSharesLeaderKey) {
  const HarnessFixture fx = buildFixture(FormType::Fugue, 42, false, 84);
  ASSERT_FALSE(fx.material.stretto_entries.empty()) << "no stretto declared";
  const StrettoDecl& stretto = fx.material.stretto_entries.front();

  // (a) Every follower note is the subject transposed by interval_semis exactly
  //     (the relation the validator's stretto_overlap_valid rule enforces).
  ASSERT_GE(fx.material.subject.size(), stretto.follower_notes.size());
  for (std::size_t i = 0; i < stretto.follower_notes.size(); ++i) {
    EXPECT_EQ(static_cast<int>(stretto.follower_notes[i].pitch),
              static_cast<int>(fx.material.subject[i].pitch) + stretto.interval_semis)
        << "follower note " << i << " is not the subject transposed by interval_semis";
  }

  // (b) On strong beats where leader and follower both sound, the vertical
  //     interval class is consonant. A self-canon legitimately yields a couple
  //     of passing seconds, so allow at most two dissonant overlaps rather
  //     than demanding 100%.
  const ComposeResult r = Composer{}.run(fx.material, fx.harmony, fx.voice_plan);
  const Tick overlap_end = stretto.leader_entry_tick + stretto.leader_length_ticks;
  int overlaps = 0;
  int dissonant = 0;
  for (Tick tick = (stretto.follower_entry_tick / kBar) * kBar; tick < overlap_end; tick += kBar) {
    if (tick < stretto.follower_entry_tick) {
      continue;  // before the follower enters there is no overlap.
    }
    const int leader = latestSoundingPitch(r.notes, stretto.leader_voice, tick);
    const int follower = latestSoundingPitch(r.notes, stretto.follower_voice, tick);
    if (leader < 0 || follower < 0) {
      continue;
    }
    ++overlaps;
    if (!consonantInterval(leader - follower)) {
      ++dissonant;
    }
  }
  EXPECT_GT(overlaps, 0) << "no strong-beat leader/follower overlaps in the stretto";
  EXPECT_LE(dissonant, 2) << "stretto has " << dissonant << " dissonant strong-beat overlaps of "
                          << overlaps << " (follower not in the leader's key?)";
}

// The committed stretto canon never sustains a sharp dissonance (interval
// class 1, 6 or 11) between leader and follower for a quarter note or longer:
// the builder vets every (delay, interval) canon configuration on a sixteenth
// grid and drops the stretto when none is clean. Both lines are verbatim
// Material -- the validator skips every dissonance rule on Material x Material
// pairs -- so this build-time vet is the only guard against a beat-long m2/M7
// between the two theme statements (the audible "wrong note" in the climax).
TEST(FormFugueTest, StrettoOverlapNeverSustainsSharpDissonance) {
  constexpr std::array<SubjectCharacter, 4> kCharacters = {
      SubjectCharacter::Severe, SubjectCharacter::Playful, SubjectCharacter::Noble,
      SubjectCharacter::Restless};
  const Tick sixteenth = kBar / 16;
  const int sustain_limit = 4;  // four sixteenth slots = one quarter.
  for (const SubjectCharacter character : kCharacters) {
    for (const std::uint32_t seed : kSeeds) {
      for (const bool minor : {false, true}) {
        ComposeRequest req;
        req.form = FormType::Fugue;
        req.seed = seed;
        req.is_minor = minor;
        req.character = character;
        req.target_bars = 84;
        HarnessFixture fx;
        buildFormFixture(req, &fx);
        const ComposeResult r = Composer{}.run(fx.material, fx.harmony, fx.voice_plan);
        for (const StrettoDecl& stretto : fx.material.stretto_entries) {
          const Tick overlap_end = stretto.leader_entry_tick + stretto.leader_length_ticks;
          int run = 0;
          for (Tick tick = stretto.follower_entry_tick; tick < overlap_end; tick += sixteenth) {
            const int leader = latestSoundingPitch(r.notes, stretto.leader_voice, tick);
            const int follower = latestSoundingPitch(r.notes, stretto.follower_voice, tick);
            bool sharp = false;
            if (leader >= 0 && follower >= 0) {
              const int ic = std::abs(leader - follower) % 12;
              sharp = (ic == 1 || ic == 6 || ic == 11);
            }
            run = sharp ? run + 1 : 0;
            ASSERT_LT(run, sustain_limit)
                << "sustained sharp dissonance in the stretto overlap at tick " << tick << " (seed "
                << seed << ", character " << static_cast<int>(character) << ", minor " << minor
                << ")";
          }
        }
      }
    }
  }
}

// The coda keeps a full three-voice texture under the final V0 subject head: a
// V1 alto figuration sounds over the first two coda bars, and the V2 bass stays
// consonant with V0 -- never the sustained minor-ninth / major-seventh (ic 1/11)
// that the pre-fix coda produced when the bass ignored the subject head.
TEST(FormFugueTest, CodaKeepsThreeVoiceTextureAndConsonantBass) {
  // Seed 42 carries a V1 alto across the whole 2-bar coda-subject window; the
  // consonant-bass assertion below holds for every seed regardless.
  const std::uint16_t bars = 84;
  const HarnessFixture fx = buildFixture(FormType::Fugue, 42, false, bars);
  const ComposeResult r = Composer{}.run(fx.material, fx.harmony, fx.voice_plan);

  const int coda_bars = 4;
  const int coda_start_bar = bars - coda_bars;
  const Tick window_lo = static_cast<Tick>(coda_start_bar) * kBar;
  const Tick window_hi = static_cast<Tick>(coda_start_bar + 2) * kBar;

  // (a) The V1 alto sounds across the whole two coda-subject bars: some V1
  //     pitch is sounding at every beat onset of the window. Consecutive
  //     same-pitch figuration quarters coalesce into held notes, so sounding
  //     coverage (not onset count) is the three-voice-texture invariant.
  for (Tick tick = window_lo; tick < window_hi; tick += kTicksPerBeat) {
    EXPECT_GE(latestSoundingPitch(r.notes, 1, tick), 0)
        << "coda subject window lacks the V1 alto at tick " << tick;
  }

  // (b) Every strong beat where V0 and V2 both sound is consonant, and in
  //     particular never the old sustained m9/M7 (interval class 1 or 11).
  int strong_beats = 0;
  for (Tick tick = window_lo; tick < window_hi; tick += kBar) {
    const int upper = latestSoundingPitch(r.notes, 0, tick);
    const int bass = latestSoundingPitch(r.notes, 2, tick);
    if (upper < 0 || bass < 0) {
      continue;
    }
    ++strong_beats;
    const int ic = ((std::abs(upper - bass) % 12) + 12) % 12;
    EXPECT_NE(ic, 1) << "coda V0xV2 minor ninth at tick " << tick;
    EXPECT_NE(ic, 11) << "coda V0xV2 major seventh at tick " << tick;
  }
  EXPECT_GT(strong_beats, 0) << "no V0/V2 overlap in the coda subject window";
}

// The window-aware anchor (consonantChordTone's window_pitches tie-breaker)
// keeps a sustained per-beat bass consonant against the mid-beat attacks of the
// faster figuration placed above it, so the development never accumulates a run
// of consecutive dissonant V1xV2 slots on the eighth-note grid. The pre-fix
// build produced 4-slot dissonant chains; the bound below is tight enough to
// have caught that (observed max on this seed is 1).
TEST(FormFugueTest, DevelopmentAvoidsParallelDissonanceChains) {
  const HarnessFixture fx = buildFixture(FormType::Fugue, 42, false, 84);
  const ComposeResult r = Composer{}.run(fx.material, fx.harmony, fx.voice_plan);

  Tick total = 0;
  for (const auto& note : r.notes) {
    total = std::max(total, note.start_tick + note.duration);
  }

  constexpr Tick kEighth = kTicksPerBeat / 2;  // eighth-note sampling grid.
  int run = 0;
  int max_run = 0;
  for (Tick tick = 0; tick < total; tick += kEighth) {
    const int alto = latestSoundingPitch(r.notes, 1, tick);
    const int bass = latestSoundingPitch(r.notes, 2, tick);
    if (alto >= 0 && bass >= 0 && !consonantInterval(alto - bass)) {
      ++run;
      max_run = std::max(max_run, run);
    } else {
      run = 0;
    }
  }
  // Bound is observed_max (1) + 1; well below the pre-fix 4-slot chains, so a
  // regression that restores them fails here.
  EXPECT_LE(max_run, 2) << "V1xV2 dissonant chain of length " << max_run
                        << " (window-aware anchor regressed?)";
}

// The canonical countersubject is derived against the ANSWER -- the subject a
// fifth above the home statement -- so restating it against the home-pitch
// third entry shifts it up three scale degrees (the diatonic equivalent of a
// fifth) plus one whole-line octave fit, which preserves the vertical relations
// already sounded in the answer window. Contract: over the third-entry window
// [bar 8, bar 12) no countersubject note may sustain a minor second / major
// seventh (interval class 1 or 11) against the third-entry subject for a
// quarter note or longer -- true on every seed, whether the committed
// restatement or the reactive fallback counterline is chosen. A degree-0
// restatement (keeping the countersubject's absolute pitch) would instead flip
// half those consonances into sustained sevenths.
TEST(FormFugueTest, CountersubjectThirdEntryRestatementAvoidsSustainedSevenths) {
  const Tick answer_lo = 4 * kBar;
  const Tick answer_hi = 8 * kBar;
  const Tick window_lo = 8 * kBar;
  const Tick window_hi = 12 * kBar;
  const auto windowNotes = [](const std::vector<MaterialNote>& src, Tick lo, Tick hi) {
    std::vector<MaterialNote> out;
    for (const auto& note : src) {
      if (note.start_tick >= lo && note.start_tick < hi) {
        out.push_back(note);
      }
    }
    return out;
  };

  int committed_seeds = 0;
  for (std::uint32_t seed = 1; seed <= 8; ++seed) {
    const HarnessFixture fx = buildFixture(FormType::Fugue, seed, /*is_minor=*/false, 64);
    const std::vector<MaterialNote> cs_window =
        windowNotes(fx.material.countersubject, window_lo, window_hi);
    const std::vector<MaterialNote> entry_window =
        windowNotes(fx.material.subject, window_lo, window_hi);
    ASSERT_FALSE(cs_window.empty())
        << "seed " << seed << ": no countersubject note in the third-entry window";
    ASSERT_FALSE(entry_window.empty())
        << "seed " << seed << ": no third-entry subject note in the window";

    // No countersubject/entry pair overlaps in time by a quarter note or more
    // with an ic-1/ic-11 (minor second / major seventh) vertical.
    for (const auto& cs : cs_window) {
      const long long cs_start = static_cast<long long>(cs.start_tick);
      const long long cs_end = cs_start + static_cast<long long>(cs.duration);
      for (const auto& entry : entry_window) {
        const long long entry_start = static_cast<long long>(entry.start_tick);
        const long long entry_end = entry_start + static_cast<long long>(entry.duration);
        const long long lo = std::max(cs_start, entry_start);
        const long long hi = std::min(cs_end, entry_end);
        if (hi <= lo) {
          continue;  // no temporal overlap.
        }
        const int ic =
            ((std::abs(static_cast<int>(cs.pitch) - static_cast<int>(entry.pitch)) % 12) + 12) % 12;
        if (ic == 1 || ic == 11) {
          EXPECT_LT(hi - lo, static_cast<long long>(kTicksPerBeat))
              << "seed " << seed << ": countersubject pitch " << static_cast<int>(cs.pitch)
              << " sustains ic " << ic << " against third-entry pitch "
              << static_cast<int>(entry.pitch) << " for " << (hi - lo) << " ticks";
        }
      }
    }

    // Feature-alive guard: recognise the committed degree-3 restatement. The
    // answer-window countersubject is the canonical line; the committed third
    // entry restates it up three scale degrees plus one constant whole-line
    // octave shift (the same k*12 for every note), preserving the note count and
    // duration sequence.
    const std::vector<MaterialNote> answer_cs =
        windowNotes(fx.material.countersubject, answer_lo, answer_hi);
    bool committed = !answer_cs.empty() && answer_cs.size() == cs_window.size();
    if (committed) {
      const int shift =
          static_cast<int>(cs_window.front().pitch) -
          detail::scaleUp(static_cast<int>(answer_cs.front().pitch), 3, detail::Mode::Major);
      committed = (shift % 12 == 0);
      for (std::size_t i = 0; committed && i < cs_window.size(); ++i) {
        const int expected =
            detail::scaleUp(static_cast<int>(answer_cs[i].pitch), 3, detail::Mode::Major) + shift;
        if (cs_window[i].duration != answer_cs[i].duration ||
            static_cast<int>(cs_window[i].pitch) != expected) {
          committed = false;
        }
      }
    }
    if (committed) {
      ++committed_seeds;
    }
  }
  EXPECT_GE(committed_seeds, 4) << "the degree-3 countersubject restatement is dead: only "
                                << committed_seeds << " of seeds 1..8 committed it";
}

// --- Episode counterline vocabulary rotation ---------------------------------

// Successive development episodes alternate the V1 counterline's subdivision
// tier (eighths / sixteenths), locked from the material: among the voice-1
// figuration sections of a long fugue, both a uniform-eighth section and a
// uniform-sixteenth section must exist.
// The V1 episode counterlines alternate their subdivision tier across
// episodes (eighths / sixteenths), keeping the development's counterlines
// audibly varied and the piece's sixteenth-note duration mass intact under
// the full-coverage V0 sequence.
TEST(FormFugueTest, EpisodeCounterlinesAlternateSubdivisionTiers) {
  for (std::uint32_t seed : {1u, 5u, 42u}) {
    const HarnessFixture fx = buildFixture(FormType::Fugue, seed, /*is_minor=*/false, 64);
    bool saw_eighths = false;
    bool saw_sixteenths = false;
    for (const auto& span : fx.voice_plan.spans) {
      if (span.intent != VoiceIntent::FortspinnungSpan) {
        continue;
      }
      for (const auto& section : fx.material.figuration_sections) {
        if (section.voice != 1 || section.notes.empty() || section.start_tick != span.start_tick ||
            section.end_tick != span.end_tick) {
          continue;
        }
        // Coalescing only lengthens notes, so the subdivision tier survives
        // as the section's MINIMUM duration.
        Tick min_dur = section.notes.front().duration;
        for (const auto& n : section.notes) {
          min_dur = std::min(min_dur, n.duration);
        }
        if (min_dur == kTicksPerBeat / 2) {
          saw_eighths = true;
        }
        if (min_dur == kTicksPerBeat / 4) {
          saw_sixteenths = true;
        }
      }
    }
    EXPECT_TRUE(saw_eighths) << "seed " << seed;
    EXPECT_TRUE(saw_sixteenths) << "seed " << seed;
  }
}

// --- Long-form parallel ceiling (full pipeline incl. ornament pass) ----------

namespace {

// Sounding pitch of `voice` at `tick` (latest onset wins), or -1 when silent.
int pfSoundingPitch(const std::vector<NoteEvent>& notes, int voice, Tick tick) {
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
// sampling, identical to the texture-gate's compute_parallel_counts.
int pfCountParallelPerfects(const std::vector<NoteEvent>& notes) {
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
        const int pitch_a = pfSoundingPitch(notes, voices[lo], tick);
        const int pitch_b = pfSoundingPitch(notes, voices[up], tick);
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

// A 64-bar prelude-and-fugue keeps its union-onset parallel perfect count near
// zero through the FULL production pipeline (Composer::run + ornament pass).
// This pins four parallel-avoidance behaviours at once: the figuration wave's
// sixteenth-grain motion sampling, the section-seam anchor seeding from the
// registry, the parallel-free-over-consonance anchor tier, and the ornament
// pass's motion-level suppression (an ornament run must not track another
// voice into a parallel chain).
TEST(FormFuguePreludeAndFugueTest, LongFormStaysParallelFreeThroughOrnamentPass) {
  for (std::uint32_t seed : {1u, 2u, 3u, 4u, 5u}) {
    const HarnessFixture fx = buildFixture(FormType::PreludeAndFugue, seed, /*is_minor=*/false, 64);
    ComposeResult r = Composer{}.run(fx.material, fx.harmony, fx.voice_plan);
    ASSERT_TRUE(r.validation.failures.empty()) << "seed " << seed;
    OrnamentParams params;
    params.character = SubjectCharacter::Severe;
    params.mode = detail::Mode::Major;
    params.seed = seed;
    params.ticks_per_bar = fx.harmony.ticksPerBar();
    applyOrnamentPass(r, params);
    EXPECT_LE(pfCountParallelPerfects(r.notes), 2) << "seed " << seed;
  }
}

// --- 12. Wave reactive-layer budget -------------------------------------------
// The figuration wave's reactive layers (parallel displacement, wobble
// breaker, harsh/parallel step adjustment, order clamp) are escape hatches: a
// layer that fires often means the DESIGN handed the wave a hostile context.
// This pins the per-piece firing budget so design changes that starve the
// wave are caught, and documents the baseline the cell-realization work
// shrinks toward zero.
TEST(FormFugueTest, WaveVetoLayersStayWithinDesignBudget) {
  for (FormType form : {FormType::Fugue, FormType::PreludeAndFugue}) {
    for (std::uint32_t seed : {1u, 8u, 12u, 42u, 99u}) {
      waveVetoStats().reset();
      const HarnessFixture fx = buildFixture(form, seed, /*is_minor=*/false, 128);
      ASSERT_FALSE(fx.material.figuration_sections.empty());
      const WaveVetoStats stats = waveVetoStats();
      // Budgets are the measured per-piece maxima (128 bars) plus ~30%
      // headroom. The anchor-displacement and order-clamp layers measure
      // ZERO under the designed chord plans -- pinned tight so any design
      // change that re-awakens them is flagged.
      EXPECT_LE(stats.anchor_parallel_displaced, 2)
          << "form " << static_cast<int>(form) << " seed " << seed;
      EXPECT_LE(stats.order_clamp_changed, 2)
          << "form " << static_cast<int>(form) << " seed " << seed;
      EXPECT_LE(stats.wobble_breaker_fired, 30)
          << "form " << static_cast<int>(form) << " seed " << seed;
      EXPECT_LE(stats.step_parallel_adjusted, 55)
          << "form " << static_cast<int>(form) << " seed " << seed;
      EXPECT_LE(stats.step_harsh_adjusted, 175)
          << "form " << static_cast<int>(form) << " seed " << seed;
      EXPECT_LE(stats.window_expanded, 175)
          << "form " << static_cast<int>(form) << " seed " << seed;
    }
  }
  waveVetoStats().reset();
}

// --- 13. Tonal-plan regressions ----------------------------------------------
// The fugue's per-bar chord plan is a piece-level tonal design: middle-entry
// bars open on the entry's related-key chord (dominant prolongation in the
// pedal cycle), and every episode bar is one link of a diatonic
// descending-fifths chain built backward from the chord on the bar right
// after the episode, so each episode drives into the next station.
TEST(FormFugueTest, TonalPlanChainsEpisodesIntoStations) {
  // Predecessor (a diatonic fifth above) of a chain chord root; mirrors the
  // form's chain vocabulary (major folds the B-rooted diminished link out,
  // minor walks the lament circle with the harmonic-minor dominant).
  const auto fifth_above = [](int root, bool minor) {
    if (minor) {
      switch (root) {
        case 0:
          return 7;
        case 7:
          return 8;
        case 8:
          return 3;
        case 3:
          return 10;
        case 10:
          return 5;
        case 5:
          return 0;
        default:
          return 7;
      }
    }
    switch (root) {
      case 0:
        return 7;
      case 7:
        return 2;
      case 2:
        return 9;
      case 9:
        return 4;
      case 4:
        return 5;
      case 5:
        return 0;
      default:
        return 7;
    }
  };
  constexpr std::array<std::uint8_t, 3> voice_keys = {7, 9, 5};  // V0->V, V1->vi, V2->IV.

  for (bool minor : {false, true}) {
    for (std::uint32_t seed : {8u, 42u}) {
      for (std::uint16_t bars : {static_cast<std::uint16_t>(84), static_cast<std::uint16_t>(128)}) {
        const HarnessFixture fx = buildFixture(FormType::Fugue, seed, minor, bars);
        const Tick pedal_window_lo =
            fx.material.pedal_points.empty() ? 0 : fx.material.pedal_points.front().start_tick;

        // The answer bars sit on the dominant.
        EXPECT_EQ(chordRootAt(fx, 4 * kBar), 7)
            << "seed " << seed << (minor ? " minor" : " major") << ": answer bars not dominant";

        for (const auto& span : fx.voice_plan.spans) {
          if (span.intent == VoiceIntent::MiddleEntryCarrier) {
            const bool pedal_window =
                !fx.material.pedal_points.empty() && span.start_tick == pedal_window_lo;
            const int expected =
                pedal_window
                    ? 7
                    : ((minor && span.voice != 2)
                           ? 7  // minor V and vi entries take dominant-set support.
                           : static_cast<int>(voice_keys[static_cast<std::size_t>(span.voice)]));
            EXPECT_EQ(chordRootAt(fx, span.start_tick), expected)
                << "seed " << seed << (minor ? " minor" : " major") << " bars " << bars
                << ": entry at bar " << (span.start_tick / kBar)
                << " does not open on its station chord";
          }
          if (span.intent == VoiceIntent::FortspinnungSpan) {
            const int first_bar = static_cast<int>(span.start_tick / kBar);
            const int last_bar = static_cast<int>(span.end_tick / kBar) - 1;
            for (int bar = first_bar; bar <= last_bar; ++bar) {
              const int next_root = chordRootAt(fx, static_cast<Tick>(bar + 1) * kBar);
              const int expected = fifth_above(next_root, minor);
              EXPECT_EQ(chordRootAt(fx, static_cast<Tick>(bar) * kBar), expected)
                  << "seed " << seed << (minor ? " minor" : " major") << " bars " << bars
                  << ": episode bar " << bar << " breaks the descending-fifths chain";
            }
          }
        }
      }
    }
  }
}

// --- Ornament/expression fixture metadata ----------------------------------

// A value-initialised HarnessFixture carries no ornament metadata: the fields
// are defaulted so fixture builders that only fill material/harmony/voice_plan
// stay byte-identical.
TEST(FormFugueTest, ValueInitialisedFixtureHasEmptyOrnamentMetadata) {
  const HarnessFixture fx;
  EXPECT_TRUE(fx.section_cadence_ticks.empty());
  EXPECT_EQ(fx.climax_start_tick, 0u);
  EXPECT_EQ(fx.climax_end_tick, 0u);
}

// The fugue builder resolves the exposition close as a section cadence and
// the climax cycle as the climax window, both inside the piece span.
TEST(FormFugueTest, FugueFixtureCarriesSectionCadenceAndClimaxWindow) {
  for (std::uint32_t seed : kSeeds) {
    const std::uint16_t bars = naturalBars(FormType::Fugue);
    const HarnessFixture fx = buildFixture(FormType::Fugue, seed, /*is_minor=*/false, bars);
    const Tick piece_end = static_cast<Tick>(bars) * kBar;

    ASSERT_FALSE(fx.section_cadence_ticks.empty()) << "seed " << seed;
    // The exposition of a natural-length fugue spans 12 bars; its final bar
    // (bar 11) is the declared section cadence.
    EXPECT_EQ(fx.section_cadence_ticks.front(), 11u * kBar) << "seed " << seed;

    EXPECT_GT(fx.climax_end_tick, fx.climax_start_tick) << "seed " << seed;
    EXPECT_LE(fx.climax_end_tick, piece_end) << "seed " << seed;
    // The climax window sits past the exposition (inside the development).
    EXPECT_GE(fx.climax_start_tick, 12u * kBar) << "seed " << seed;
  }
}

// The prelude+fugue pair offsets the fugue's section cadence by the prelude
// length: the declared bar lies inside the fugue half.
TEST(FormFugueTest, PreludeAndFugueSectionCadenceSitsInFugueHalf) {
  for (std::uint32_t seed : kSeeds) {
    const std::uint16_t bars = naturalBars(FormType::PreludeAndFugue);
    const HarnessFixture fx =
        buildFixture(FormType::PreludeAndFugue, seed, /*is_minor=*/false, bars);
    ASSERT_FALSE(fx.section_cadence_ticks.empty()) << "seed " << seed;
    // The pair's prelude is at least 4 bars, so the fugue exposition's close
    // lies strictly past the prelude opening and inside the piece.
    EXPECT_GE(fx.section_cadence_ticks.front(), 4u * kBar) << "seed " << seed;
    EXPECT_LT(fx.section_cadence_ticks.front(), static_cast<Tick>(bars) * kBar) << "seed " << seed;
  }
}

// The exposition's section cadence lands on the V0 figuration span's final bar
// (bars 8-11 of a full-form fugue). That bar's second half closes on a held
// mid-bar anchor (a half note on the strong beat) so the ornament pass has a
// strong-beat quarter-or-longer top note for the mandatory section-cadence
// trill; a running eighth wave alone carries no such note. The held tone falls
// back to the normal wave when no consonant candidate exists (graceful
// degradation), so the assertion is a "feature is alive" guard: the held tone
// must appear for at least half of the tested seeds.
TEST(FormFugueTest, ExpositionFiguresCloseOnStrongBeatHalfNote) {
  const std::uint16_t bars = naturalBars(FormType::Fugue);
  // The plain fugue starts at bar 0, so the V0 exposition figuration span is
  // [bar 8, bar 12) on voice 0 and the exposition's final bar is bar 11.
  const Tick span_start = 8u * kBar;
  const Tick span_end = 12u * kBar;
  const Tick final_bar_start = 11u * kBar;
  int held_seen = 0;
  int seeds_tested = 0;
  for (std::uint32_t seed = 1; seed <= 8; ++seed) {
    const HarnessFixture fx = buildFixture(FormType::Fugue, seed, /*is_minor=*/false, bars);
    const std::vector<MaterialNote> notes = bassSupportNotes(fx, /*voice=*/0, span_start, span_end);
    ASSERT_FALSE(notes.empty()) << "seed " << seed << " has no V0 exposition figuration";
    ++seeds_tested;
    bool held = false;
    bool has_final_bar_notes = false;
    for (const auto& note : notes) {
      if (note.start_tick < final_bar_start || note.start_tick >= span_end) {
        continue;
      }
      has_final_bar_notes = true;
      const Tick pos_in_bar = note.start_tick % kBar;
      if (pos_in_bar == kBar / 2 && note.duration >= static_cast<Tick>(kTicksPerBeat)) {
        held = true;
      }
    }
    // Whether or not the held tone fired, the final bar must still be voiced
    // (the graceful-degradation fallback re-emits the running wave).
    EXPECT_TRUE(has_final_bar_notes) << "seed " << seed << " has an empty exposition final bar";
    if (held) {
      ++held_seen;
    }
  }
  EXPECT_GE(held_seen, (seeds_tested + 1) / 2)
      << "cadential-close half note is dead: only " << held_seen << " of " << seeds_tested
      << " seeds produced a strong-beat held tone in the exposition final bar";
}

}  // namespace bach::composer
