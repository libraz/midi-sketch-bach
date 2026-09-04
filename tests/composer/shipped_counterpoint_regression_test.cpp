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
  std::size_t battuta = 0;  // contrary-motion arrival, upper voice leaping down

  std::size_t strict() const { return parallel_fifth + parallel_octave; }
  std::size_t hidden() const { return hidden_fifth + hidden_octave; }

  void add(const PerfectMotionCounts& other) {
    parallel_fifth += other.parallel_fifth;
    parallel_octave += other.parallel_octave;
    hidden_fifth += other.hidden_fifth;
    hidden_octave += other.hidden_octave;
    battuta += other.battuta;
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
          // Battuta is contrary motion, so classifyPerfectMotion -- which only
          // ever reports same-direction arrivals -- necessarily returns None for
          // it. Counting it in the same sweep rather than a second one keeps all
          // three ways of reaching a perfect interval on one sampling grid.
          if (isBattutaMotion(prev_upper, upper_pitch, prev_lower, lower_pitch)) {
            ++counts.battuta;
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
  std::size_t max_strict;   // parallel fifths + parallel octaves/unisons
  std::size_t max_hidden;   // hidden fifths + hidden octaves
  std::size_t max_battuta;  // contrary-motion octave arrivals by downward leap
};

// Per-form ceilings on perfect-motion events found across the whole
// seed x character x mode sweep.
//
// The mode axis is half the product and is not optional here: minor draws on its
// own subjects, resolves its own leading tone and closes on its own final chord,
// so a ceiling measured over the major surface alone describes half of what
// ships and leaves the other half free to regress silently.
//
// RATCHET: these numbers may only ever be LOWERED, never raised. They are the
// counts measured from current shipped output, not a target, and the sweep is
// fully deterministic (fixed seeds, fixed characters, natural bar counts), so
// there is no run-to-run noise for a margin to absorb: they are pinned exactly.
// A form that reaches 0 stays pinned at 0. Raising a ceiling to make a change
// pass would throw away the only regression signal this file provides -- the
// fix belongs in the form builder's material derivation instead.
//
// ONE EXCEPTION, and it is narrow. A guard that is band-pinned against an
// immutable voice sometimes has no candidate left that is free of every perfect
// approach, and can then only choose WHICH of the three it commits. Such a
// change may raise one ceiling while lowering another, but only under both of
// these conditions:
//
//   1. The fault being REMOVED is one this generator commits close to or beyond
//      what the reference corpus of Bach's own writing does, and the fault being
//      ACCEPTED is one the corpus commits far more freely than this generator.
//   2. The trade is recorded, per form, in the row comment below, with the
//      measured counts -- a raise with no named trade is a regression.
//
// The counts here are raw, but the authority is the corpus envelope: a rate
// already deep inside it has room the ear does not miss, while a rate at its
// edge does not. That is why a parallel octave -- heard as one voice vanishing,
// and the rarest of the three in the corpus -- is never an acceptable payment,
// in any quantity, for either of the others.
//
// cello_prelude is monophonic, so it has no voice pair and is pinned at 0
// permanently.
constexpr std::array<FormCeiling, 10> kFormCeilings = {{
    {FormType::Fugue, 27, 12, 145},
    {FormType::PreludeAndFugue, 15, 4, 80},
    // Its hidden column is the one with room: the corpus writes hidden perfects
    // in this texture more than twice as freely as this form does, while its
    // fifths sit at the ninetieth percentile and its battuta past the
    // ninety-fifth. A trade out of either of those into hidden is payable.
    {FormType::TrioSonata, 33, 68, 61},
    // Both true-parallel classes reach zero. The tone before an arrival is
    // re-aimed over a bass pinned to a single octave, and where the consonant
    // window for that re-aim comes back empty it widens to admit a passing
    // dissonance rather than let the parallel ship; the cadential figure that
    // pins the bass under its own resolution is chosen against the three-line
    // surface it produces instead of installed over one settled without it.
    // Three true parallels left the strict column and one contrary-motion
    // arrival entered the battuta one -- a trade out of the fault the corpus
    // almost never writes and into the one it writes most freely.
    {FormType::ChoralePrelude, 0, 14, 13},
    // Most of this form's parallel octaves are deliberate: the opening octave
    // cascade states its gesture high, an octave lower, then doubled in V0 and
    // V1 across a descending scale, which is a parallel octave on every one of
    // its sixteenths by design. The ceiling therefore cannot approach zero, and
    // a drop here means the surrounding figuration improved, not the cascade.
    // Escaping a same-direction perfect by reversing the wave turns similar
    // motion into contrary motion, so what leaves the strict column here tends
    // to arrive in the battuta one.
    {FormType::ToccataAndFugue, 56, 27, 85},
    // The counter figuration is pinned to a one-octave band under an immutable
    // ground whose pitch class the chord root tracks, so its octave companion is
    // often the only chord tone in reach and every approach to it is at least
    // hidden. Strict faults here are payable in hidden ones for that reason.
    {FormType::Passacaglia, 18, 26, 29},
    // Almost all of what remains in the strict column is fifths; the parallel
    // octaves this form used to carry are gone, bought with the two milder
    // approaches, which is why its hidden and battuta columns are the widest.
    {FormType::FantasiaAndFugue, 14, 89, 111},
    {FormType::CelloPrelude, 0, 0, 0},
    // Two voices only, so an arrival on a perfect interval meets a fixed bass
    // with no third part to hide behind. No true parallel of either class
    // survives; the remaining ways in are upward leaps, which is ordinary
    // cadential writing, so hidden carries the whole residue by design.
    {FormType::Chaconne, 0, 23, 0},
    // Nothing here is repaired after the fact: the aria bass is immutable by
    // contract and a canon's two lines cannot be re-aimed one end at a time. The
    // strict column reaches zero because the imitative blocks are instead
    // assembled and read while their one free choice is still open -- a canon's
    // leader tones, the quodlibet tune's rotation -- and the free figuration
    // between them is relieved arrival by arrival. Hidden approaches are what
    // that choice pays with: the leader window of a wide canon is about a fifth
    // deep, so an arrival it can reach cleanly is often still approached by leap.
    {FormType::GoldbergVariations, 0, 8, 3},
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
        // Both modes, because they are different music: the minor material has
        // its own subjects, its own leading tone and its own final chord, and a
        // ceiling measured over one of them describes half of what ships.
        request.is_minor = (offset % 2) == 1;

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
    std::printf("[counterpoint] %-20s par5=%zu par8=%zu hidden=%zu battuta=%zu\n",
                formLabel(entry.form), total.parallel_fifth, total.parallel_octave, total.hidden(),
                total.battuta);
    EXPECT_LE(total.strict(), entry.max_strict)
        << formLabel(entry.form) << ": parallel perfect intervals in shipped output rose above the "
        << "ratchet (par5=" << total.parallel_fifth << " par8=" << total.parallel_octave << ")";
    EXPECT_LE(total.battuta, entry.max_battuta)
        << formLabel(entry.form) << ": ottava battuta in shipped output rose above the ratchet";
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
