// Shipped-output counterpoint ratchet.
//
// Every shipped form emits carrier spans only, so both operands of a
// counterpoint finding are fixed (Material/Ornament) sources and the
// validator's fixed-source exemption suppresses the finding: the composer
// cannot rewrite an immutable carrier, so the rule is recorded as diagnostic
// information rather than a failure. A test that only asserts
// ValidationStatus::Ok therefore says nothing at all about the contrapuntal
// quality of the notes that actually ship.
//
// This file counts parallel and hidden perfect intervals directly from the
// note array produced by the shipped path (ComposeRequest ->
// buildFormFixture -> Composer::run), bypassing the validator and its
// exemptions entirely, and holds each form to a per-form ceiling. The judge of
// a single motion pair is the shared classifyPerfectMotion; only the sampling
// and bookkeeping around it are written here.

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "composer/composer.h"
#include "composer/form_director.h"
#include "composer/harness_fixture.h"
#include "core/basic_types.h"
#include "core/pitch_utils.h"

namespace bach::composer {
namespace {

// --- Local naming ------------------------------------------------------------
//
// bach_composer_tests links bach_composer_lib only, so the display helpers in
// core/basic_types.cpp are out of reach. These labels exist for failure
// messages and for the skipped-cell expectation.

const char* formLabel(FormType form) {
  switch (form) {
    case FormType::Fugue:
      return "fugue";
    case FormType::PreludeAndFugue:
      return "prelude_and_fugue";
    case FormType::TrioSonata:
      return "trio_sonata";
    case FormType::ChoralePrelude:
      return "chorale_prelude";
    case FormType::ToccataAndFugue:
      return "toccata_and_fugue";
    case FormType::Passacaglia:
      return "passacaglia";
    case FormType::FantasiaAndFugue:
      return "fantasia_and_fugue";
    case FormType::CelloPrelude:
      return "cello_prelude";
    case FormType::Chaconne:
      return "chaconne";
    case FormType::GoldbergVariations:
      return "goldberg_variations";
  }
  return "unknown_form";
}

const char* characterLabel(SubjectCharacter character) {
  switch (character) {
    case SubjectCharacter::Severe:
      return "severe";
    case SubjectCharacter::Playful:
      return "playful";
    case SubjectCharacter::Noble:
      return "noble";
    case SubjectCharacter::Restless:
      return "restless";
  }
  return "unknown_character";
}

// --- Independent perfect-motion counter -------------------------------------

struct PerfectMotionCounts {
  std::size_t parallel_fifth = 0;
  std::size_t parallel_octave = 0;  // includes unisons (interval class 0)
  std::size_t hidden_fifth = 0;
  std::size_t hidden_octave = 0;

  std::size_t strict() const { return parallel_fifth + parallel_octave; }
  std::size_t hidden() const { return hidden_fifth + hidden_octave; }

  void add(const PerfectMotionCounts& other) {
    parallel_fifth += other.parallel_fifth;
    parallel_octave += other.parallel_octave;
    hidden_fifth += other.hidden_fifth;
    hidden_octave += other.hidden_octave;
  }
};

// Per-voice note lookup with the same window semantics the validator uses: a
// note sounds over [start_tick, start_tick + duration), and when several notes
// of one voice overlap a tick the latest-starting one wins (ties inside one
// start_tick resolve to the last note in score order). Written out here rather
// than shared so that a change to the validator's sampling cannot silently
// move this gate.
class SoundingPitchIndex {
 public:
  explicit SoundingPitchIndex(const std::vector<NoteEvent>& notes) : notes_(notes), by_voice_(256) {
    for (std::size_t index = 0; index < notes.size(); ++index)
      by_voice_[notes[index].voice].push_back(index);
    for (auto& indices : by_voice_) {
      std::stable_sort(indices.begin(), indices.end(), [&](std::size_t left, std::size_t right) {
        return notes_[left].start_tick < notes_[right].start_tick;
      });
    }
  }

  // Returns 0 when the voice rests at this tick.
  std::uint8_t pitchAt(VoiceId voice, Tick tick) const {
    const auto& indices = by_voice_[voice];
    auto iter = std::upper_bound(
        indices.begin(), indices.end(), tick,
        [&](Tick value, std::size_t index) { return value < notes_[index].start_tick; });
    while (iter != indices.begin()) {
      --iter;
      const NoteEvent& note = notes_[*iter];
      if (note.start_tick <= tick && tick < note.start_tick + note.duration)
        return note.pitch;
    }
    return 0;
  }

 private:
  const std::vector<NoteEvent>& notes_;
  std::vector<std::vector<std::size_t>> by_voice_;
};

// Counts perfect-interval motion over every voice pair at the union of note
// onsets. A rest in either voice breaks the succession: the pitches on either
// side of a gap are not consecutive motion, so remembering them across the gap
// would report a parallel that nobody hears.
PerfectMotionCounts countPerfectMotion(const std::vector<NoteEvent>& notes) {
  PerfectMotionCounts counts;
  if (notes.empty())
    return counts;

  std::vector<Tick> ticks;
  ticks.reserve(notes.size());
  std::vector<VoiceId> voices;
  for (const NoteEvent& note : notes) {
    ticks.push_back(note.start_tick);
    if (std::find(voices.begin(), voices.end(), note.voice) == voices.end())
      voices.push_back(note.voice);
  }
  std::sort(ticks.begin(), ticks.end());
  ticks.erase(std::unique(ticks.begin(), ticks.end()), ticks.end());
  std::sort(voices.begin(), voices.end());

  const SoundingPitchIndex index(notes);
  // Voice 0 is the highest part by convention, so the lower voice index is the
  // "upper" operand of classifyPerfectMotion (the leap side of the
  // hidden-perfect rule).
  for (std::size_t upper = 0; upper < voices.size(); ++upper) {
    for (std::size_t lower = upper + 1; lower < voices.size(); ++lower) {
      std::uint8_t prev_upper = 0;
      std::uint8_t prev_lower = 0;
      for (Tick tick : ticks) {
        const std::uint8_t upper_pitch = index.pitchAt(voices[upper], tick);
        const std::uint8_t lower_pitch = index.pitchAt(voices[lower], tick);
        if (upper_pitch == 0 || lower_pitch == 0) {
          prev_upper = 0;
          prev_lower = 0;
          continue;
        }
        if (prev_upper != 0 && prev_lower != 0) {
          switch (classifyPerfectMotion(prev_upper, upper_pitch, prev_lower, lower_pitch)) {
            case PerfectMotionKind::ParallelFifth:
              ++counts.parallel_fifth;
              break;
            case PerfectMotionKind::ParallelOctave:
              ++counts.parallel_octave;
              break;
            case PerfectMotionKind::HiddenFifth:
              ++counts.hidden_fifth;
              break;
            case PerfectMotionKind::HiddenOctave:
              ++counts.hidden_octave;
              break;
            case PerfectMotionKind::None:
              break;
          }
        }
        prev_upper = upper_pitch;
        prev_lower = lower_pitch;
      }
    }
  }
  return counts;
}

// --- Counter self-check ------------------------------------------------------
//
// Without these, a bug in countPerfectMotion would make every ceiling below
// trivially satisfiable and the whole ratchet would pass while measuring
// nothing. They construct the motion by hand and assert the counter sees it.

NoteEvent makeNote(VoiceId voice, Tick start, Tick duration, std::uint8_t pitch) {
  NoteEvent note;
  note.voice = voice;
  note.start_tick = start;
  note.duration = duration;
  note.pitch = pitch;
  return note;
}

TEST(PerfectMotionCounter, ReportsARealParallelOctave) {
  // C4/C5 -> D4/D5 -> E4/E5: two octaves in a row, both voices rising.
  const std::vector<NoteEvent> notes = {
      makeNote(0, 0, 480, 72), makeNote(0, 480, 480, 74), makeNote(0, 960, 480, 76),
      makeNote(1, 0, 480, 60), makeNote(1, 480, 480, 62), makeNote(1, 960, 480, 64),
  };
  const PerfectMotionCounts counts = countPerfectMotion(notes);
  EXPECT_EQ(counts.parallel_octave, 2u);
  EXPECT_EQ(counts.parallel_fifth, 0u);
  EXPECT_EQ(counts.hidden(), 0u);
}

TEST(PerfectMotionCounter, ReportsARealParallelFifth) {
  // C4/G4 -> D4/A4: a fifth on both onsets, both voices rising by step.
  const std::vector<NoteEvent> notes = {
      makeNote(0, 0, 480, 67),
      makeNote(0, 480, 480, 69),
      makeNote(1, 0, 480, 60),
      makeNote(1, 480, 480, 62),
  };
  const PerfectMotionCounts counts = countPerfectMotion(notes);
  EXPECT_EQ(counts.parallel_fifth, 1u);
  EXPECT_EQ(counts.strict(), 1u);
}

TEST(PerfectMotionCounter, ContraryMotionIntoAnOctaveIsClean) {
  // The voices converge, so no perfect interval is approached in parallel.
  const std::vector<NoteEvent> notes = {
      makeNote(0, 0, 480, 74),
      makeNote(0, 480, 480, 72),
      makeNote(1, 0, 480, 59),
      makeNote(1, 480, 480, 60),
  };
  const PerfectMotionCounts counts = countPerfectMotion(notes);
  EXPECT_EQ(counts.strict(), 0u);
  EXPECT_EQ(counts.hidden(), 0u);
}

TEST(PerfectMotionCounter, RestBreaksTheSuccession) {
  // Octave, silence in the lower voice, then another octave a step higher.
  // The two sonorities are not consecutive motion, so nothing is reported.
  const std::vector<NoteEvent> notes = {
      makeNote(0, 0, 480, 72), makeNote(0, 480, 480, 74), makeNote(0, 960, 480, 76),
      makeNote(1, 0, 480, 60), makeNote(1, 960, 480, 64),
  };
  const PerfectMotionCounts counts = countPerfectMotion(notes);
  EXPECT_EQ(counts.strict(), 0u);
  EXPECT_EQ(counts.hidden(), 0u);
}

TEST(PerfectMotionCounter, HiddenOctaveIsCountedApartFromStrictParallels) {
  // Both voices rise into an octave and the upper voice leaps a fourth to get
  // there, which is a hidden octave rather than a parallel one.
  const std::vector<NoteEvent> notes = {
      makeNote(0, 0, 480, 67),
      makeNote(0, 480, 480, 72),
      makeNote(1, 0, 480, 59),
      makeNote(1, 480, 480, 60),
  };
  const PerfectMotionCounts counts = countPerfectMotion(notes);
  EXPECT_EQ(counts.hidden_octave, 1u);
  EXPECT_EQ(counts.strict(), 0u);
}

TEST(PerfectMotionCounter, ObliqueMotionUnderASustainedVoiceIsClean) {
  // The upper voice holds through both onsets, so there is no similar motion
  // even though the interval is a fifth at each onset.
  const std::vector<NoteEvent> notes = {
      makeNote(0, 0, 960, 67),
      makeNote(1, 0, 480, 60),
      makeNote(1, 480, 480, 48),
  };
  EXPECT_EQ(countPerfectMotion(notes).strict(), 0u);
}

TEST(PerfectMotionCounter, DegenerateVoicingsCountNothing) {
  EXPECT_EQ(countPerfectMotion({}).strict(), 0u);
  const std::vector<NoteEvent> single_note = {makeNote(0, 0, 480, 60)};
  EXPECT_EQ(countPerfectMotion(single_note).strict(), 0u);
  // A single voice has no pair to compare, however long its line.
  const std::vector<NoteEvent> monophonic = {
      makeNote(0, 0, 480, 60),
      makeNote(0, 480, 480, 62),
      makeNote(0, 960, 480, 64),
  };
  EXPECT_EQ(countPerfectMotion(monophonic).strict(), 0u);
  EXPECT_EQ(countPerfectMotion(monophonic).hidden(), 0u);
}

// --- Shipped-output sweep ----------------------------------------------------

constexpr std::array<SubjectCharacter, 4> kCharacters = {{
    SubjectCharacter::Severe,
    SubjectCharacter::Playful,
    SubjectCharacter::Noble,
    SubjectCharacter::Restless,
}};

constexpr std::uint32_t kFirstSeed = 1;
constexpr std::uint32_t kSeedCount = 8;

struct FormCeiling {
  FormType form;
  std::size_t max_strict;  // parallel fifths + parallel octaves/unisons
  std::size_t max_hidden;  // hidden fifths + hidden octaves
};

// Per-form ceilings on perfect-motion events found across the whole
// seed x character sweep.
//
// RATCHET: these numbers may only ever be LOWERED, never raised. They are the
// counts measured from current shipped output, not a target, and the sweep is
// fully deterministic (fixed seeds, fixed characters, natural bar counts), so
// there is no run-to-run noise for a margin to absorb: they are pinned exactly.
// A form that reaches 0 stays pinned at 0. Raising a ceiling to make a change
// pass would throw away the only regression signal this file provides -- the
// fix belongs in the form builder's material derivation instead.
//
// cello_prelude is monophonic, so it has no voice pair and is pinned at 0
// permanently.
constexpr std::array<FormCeiling, 10> kFormCeilings = {{
    {FormType::Fugue, 16, 9},
    {FormType::PreludeAndFugue, 8, 3},
    {FormType::TrioSonata, 127, 97},
    {FormType::ChoralePrelude, 44, 77},
    {FormType::ToccataAndFugue, 73, 35},
    {FormType::Passacaglia, 111, 63},
    {FormType::FantasiaAndFugue, 71, 77},
    {FormType::CelloPrelude, 0, 0},
    {FormType::Chaconne, 22, 29},
    {FormType::GoldbergVariations, 35, 48},
}};

// Form x character pairs the form director refuses by design: the chorale
// prelude carries a cantus firmus that the light characters cannot sustain,
// and the dramatic toccata is antithetical to the dignified Noble affect.
// Held as an explicit expectation so a NEW generation failure surfaces as a
// diff in the skipped set rather than as a silently smaller sweep.
std::vector<std::string> expectedSkippedCells() {
  return {
      std::string(formLabel(FormType::ChoralePrelude)) + " x " +
          characterLabel(SubjectCharacter::Playful),
      std::string(formLabel(FormType::ChoralePrelude)) + " x " +
          characterLabel(SubjectCharacter::Restless),
      std::string(formLabel(FormType::ToccataAndFugue)) + " x " +
          characterLabel(SubjectCharacter::Noble),
  };
}

TEST(ShippedCounterpointRatchet, PerfectMotionStaysUnderPerFormCeiling) {
  std::vector<std::string> skipped;
  std::size_t composed_cells = 0;

  for (const FormCeiling& entry : kFormCeilings) {
    PerfectMotionCounts total;
    for (SubjectCharacter character : kCharacters) {
      bool character_skipped = false;
      for (std::uint32_t offset = 0; offset < kSeedCount; ++offset) {
        ComposeRequest request;
        request.form = entry.form;
        request.character = character;
        request.seed = kFirstSeed + offset;

        HarnessFixture fixture;
        if (buildFormFixture(request, &fixture) != FormDirectorStatus::Ok) {
          character_skipped = true;
          continue;
        }
        const ComposeResult result =
            Composer{}.run(fixture.material, fixture.harmony, fixture.voice_plan);
        ASSERT_FALSE(result.notes.empty()) << formLabel(entry.form) << " x "
                                           << characterLabel(character) << " seed " << request.seed;
        ++composed_cells;
        total.add(countPerfectMotion(result.notes));
      }
      if (character_skipped) {
        skipped.push_back(std::string(formLabel(entry.form)) + " x " + characterLabel(character));
      }
    }

    // Emitted on every run so the current measurement is visible when the
    // ratchet is tightened after a counterpoint fix.
    std::printf("[counterpoint] %-20s par5=%zu par8=%zu hidden=%zu\n", formLabel(entry.form),
                total.parallel_fifth, total.parallel_octave, total.hidden());
    EXPECT_LE(total.strict(), entry.max_strict)
        << formLabel(entry.form) << ": parallel perfect intervals in shipped output rose above the "
        << "ratchet (par5=" << total.parallel_fifth << " par8=" << total.parallel_octave << ")";
    EXPECT_LE(total.hidden(), entry.max_hidden)
        << formLabel(entry.form) << ": hidden perfect intervals in shipped output rose above the "
        << "ratchet (hidden5=" << total.hidden_fifth << " hidden8=" << total.hidden_octave << ")";
  }

  std::sort(skipped.begin(), skipped.end());
  std::vector<std::string> expected = expectedSkippedCells();
  std::sort(expected.begin(), expected.end());
  EXPECT_EQ(skipped, expected) << "the set of form x character pairs that fail to generate changed";
  // Guards against a partial skip hiding inside an already-expected pair.
  EXPECT_EQ(composed_cells,
            kFormCeilings.size() * kCharacters.size() * kSeedCount - expected.size() * kSeedCount);
}

}  // namespace
}  // namespace bach::composer
