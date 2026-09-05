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
  // Leaving a perfect class and reaching the same class again in contrary
  // motion. Counted because an outside reading that pools it with the true
  // parallel reports an octave column an order of magnitude worse than the
  // counterpoint it describes, and a class nobody counts cannot be shown to be
  // holding steady.
  std::size_t anti_parallel = 0;

  std::size_t strict() const { return parallel_fifth + parallel_octave; }
  std::size_t hidden() const { return hidden_fifth + hidden_octave; }

  void add(const PerfectMotionCounts& other) {
    parallel_fifth += other.parallel_fifth;
    parallel_octave += other.parallel_octave;
    hidden_fifth += other.hidden_fifth;
    hidden_octave += other.hidden_octave;
    battuta += other.battuta;
    anti_parallel += other.anti_parallel;
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
    const NoteEvent* note = noteAt(voice, tick);
    return note != nullptr ? note->pitch : 0;
  }

  // Tick this voice's sound at `tick` runs out at, or `tick` itself when it is
  // silent there. Sampling pitches at onsets alone cannot see a rest that opens
  // and closes strictly between two onsets -- no note starts inside it, so the
  // walk steps straight over the silence and reads the tones on either side as
  // consecutive motion.
  Tick soundEnd(VoiceId voice, Tick tick) const {
    const NoteEvent* note = noteAt(voice, tick);
    return note != nullptr ? note->start_tick + note->duration : tick;
  }

 private:
  const NoteEvent* noteAt(VoiceId voice, Tick tick) const {
    const auto& indices = by_voice_[voice];
    auto iter = std::upper_bound(
        indices.begin(), indices.end(), tick,
        [&](Tick value, std::size_t index) { return value < notes_[index].start_tick; });
    while (iter != indices.begin()) {
      --iter;
      const NoteEvent& note = notes_[*iter];
      if (note.start_tick <= tick && tick < note.start_tick + note.duration)
        return &note;
    }
    return nullptr;
  }

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
      Tick prev_tick = 0;
      for (Tick tick : ticks) {
        const std::uint8_t upper_pitch = index.pitchAt(voices[upper], tick);
        const std::uint8_t lower_pitch = index.pitchAt(voices[lower], tick);
        if (upper_pitch == 0 || lower_pitch == 0) {
          prev_upper = 0;
          prev_lower = 0;
          prev_tick = tick;
          continue;
        }
        // A rest between the two sampled onsets breaks the succession just as a
        // rest ON one of them does. Sampling only at onsets cannot see it --
        // nothing starts inside a silence -- so the sound reaching from the
        // previous onset has to be asked whether it lasted all the way here.
        if (index.soundEnd(voices[upper], prev_tick) < tick ||
            index.soundEnd(voices[lower], prev_tick) < tick) {
          prev_upper = 0;
          prev_lower = 0;
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
          // Battuta and the anti-parallel are both contrary motion, so
          // classifyPerfectMotion -- which only ever reports same-direction
          // arrivals -- necessarily returns None for them. Counting them in the
          // same sweep rather than separate ones keeps every way of reaching a
          // perfect interval on one sampling grid.
          if (isBattutaMotion(prev_upper, upper_pitch, prev_lower, lower_pitch)) {
            ++counts.battuta;
          }
          if (isAntiParallelPerfectMotion(prev_upper, upper_pitch, prev_lower, lower_pitch)) {
            ++counts.anti_parallel;
          }
        }
        prev_upper = upper_pitch;
        prev_lower = lower_pitch;
        prev_tick = tick;
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

TEST(PerfectMotionCounter, RestBetweenTwoOnsetsBreaksTheSuccession) {
  // Both voices fall silent for a beat and re-enter together a bar later. No
  // note starts inside that silence, so an onset-only walk steps straight over
  // it and reads two octaves a bar apart as consecutive motion. The counter has
  // to ask whether the sound from the previous onset lasted until this one.
  const std::vector<NoteEvent> notes = {
      makeNote(0, 0, 480, 72),
      makeNote(0, 1440, 480, 74),
      makeNote(1, 0, 480, 60),
      makeNote(1, 1440, 480, 62),
  };
  const PerfectMotionCounts counts = countPerfectMotion(notes);
  EXPECT_EQ(counts.strict(), 0u);
  EXPECT_EQ(counts.hidden(), 0u);
}

TEST(PerfectMotionCounter, OneVoiceRestingAloneBreaksTheSuccession) {
  // The upper voice sustains across; the lower one drops out for a beat. The
  // succession is broken by either voice's silence, not only by both.
  const std::vector<NoteEvent> notes = {
      makeNote(0, 0, 1920, 72),
      makeNote(0, 1920, 480, 74),
      makeNote(1, 0, 480, 60),
      makeNote(1, 1920, 480, 62),
  };
  EXPECT_EQ(countPerfectMotion(notes).strict(), 0u);
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

TEST(PerfectMotionCounter, AntiParallelIsCountedApartFromStrictParallels) {
  // The voices leave an octave and reach an octave again by moving apart. An
  // outside reading that asks only whether a perfect class recurred calls this
  // a parallel octave; it is contrary motion, and the two belong in separate
  // columns or the octave count reads far worse than the counterpoint is.
  const std::vector<NoteEvent> notes = {
      makeNote(0, 0, 480, 72),
      makeNote(0, 480, 480, 74),
      makeNote(1, 0, 480, 60),
      makeNote(1, 480, 480, 50),
  };
  const PerfectMotionCounts counts = countPerfectMotion(notes);
  EXPECT_EQ(counts.anti_parallel, 1u);
  EXPECT_EQ(counts.strict(), 0u);
  EXPECT_EQ(counts.hidden(), 0u);
}

TEST(PerfectMotionCounter, SimilarMotionIntoAnOctaveIsNotAnAntiParallel) {
  // The counterpart: both voices rise from an octave to an octave. This is the
  // true parallel, and the anti-parallel column must stay empty for it.
  const std::vector<NoteEvent> notes = {
      makeNote(0, 0, 480, 72),
      makeNote(0, 480, 480, 74),
      makeNote(1, 0, 480, 60),
      makeNote(1, 480, 480, 62),
  };
  const PerfectMotionCounts counts = countPerfectMotion(notes);
  EXPECT_EQ(counts.parallel_octave, 1u);
  EXPECT_EQ(counts.anti_parallel, 0u);
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
  std::size_t max_anti;     // a perfect class left and reached again in contrary motion
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
// approach, and can then only choose WHICH of the four columns it commits to.
// Such a change may raise one ceiling while lowering another, but only under
// both of these conditions:
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
// and the rarest of the four in the corpus -- is never an acceptable payment,
// in any quantity, for any of the others.
//
// The anti-parallel column exists because it is the class an outside reading
// pools with the true parallel: a predicate that asks only whether a perfect
// class recurred counts both, and the octave figure it prints then reads an
// order of magnitude worse than the counterpoint it describes. Kept in its own
// column, it is measurable on its own terms -- a milder fault than a parallel,
// firmer than a battuta, and one the guards deliberately step onto rather than
// ship a parallel.
//
// cello_prelude is monophonic, so it has no voice pair and is pinned at 0
// permanently.
constexpr std::array<FormCeiling, 10> kFormCeilings = {{
    // The stretto lays two verbatim theme statements against each other, so its
    // canon configuration is the one choice in this form that decides a parallel
    // outright, and it is made while the whole overlap is still readable. A
    // configuration that sounds a true parallel is now refused rather than
    // ranked below a sustained dissonance: the dissonance is a matter of degree
    // and the parallel is the prohibition. The strict column falls from
    // twenty-three to three and the fifth reaches zero, paid for with four
    // hidden perfects and four contrary-motion octave arrivals; two
    // anti-parallels leave with the parallels.
    //
    // The remaining three then go, and the strict column reaches zero. Two of
    // them came from the coda: its cadence voicing was the one figuration
    // section never written into the tone registry, so the seam that hands the
    // wave over to it read as a rest and no guard downstream could see the
    // arrival at all. The third came from the bar-head escape, which vetoed on a
    // sustain-window clash and therefore handed the onset back to the parallel
    // wherever the escape vocabulary was clash-free nowhere. Both cost two
    // hidden perfects between them and nothing in the other two columns.
    //
    // The trade is payable in one direction only. In the reference corpus the
    // similar-motion parallel is the rarest thing measured -- across the
    // three-voice works its octave rate is zero at every percentile including
    // the maximum, and its fifth rate is zero through the ninety-fifth -- while
    // hidden perfects are written freely in exactly this texture. There is no
    // quantity of true parallel that buys anything back.
    {FormType::Fugue, 0, 17, 149, 59},
    // Its bass support tone is read against the running voices at the grain they
    // actually move at rather than a bar back, and ranks a hidden perfect below
    // a true one; two true parallels left the strict column and two hidden ones
    // entered -- the corpus writes hidden perfects in this texture far more
    // freely than it writes either true class. Its figuration then reached the
    // bar heads where no chord tone was playable at all and left the chord for a
    // free diatonic tone: both remaining fifths and two octaves went with it, at
    // no cost to the hidden or battuta columns. The fugue half is assembled by
    // the same section builder as the bare fugue, so the registered coda voicing
    // and the ranked bar-head escape close the strict column here too: seven to
    // zero against four hidden perfects, with battuta and anti-parallel unmoved.
    {FormType::PreludeAndFugue, 0, 9, 80, 34},
    // Its hidden column is the one with room: the corpus writes hidden perfects
    // in this texture more than twice as freely as this form does, while its
    // fifths sit at the ninetieth percentile and its battuta past the
    // ninety-fifth. A trade out of either of those into hidden is payable, and
    // that is the trade taken: both true-parallel classes reach zero, paid for
    // with fourteen hidden perfects and two contrary-motion octave arrivals.
    // The pedal is the voice that pays -- it is written last against two settled
    // manuals, and once it ranks a hidden perfect below a true one it will step
    // onto the hidden approach rather than keep the parallel it began with.
    {FormType::TrioSonata, 0, 82, 63, 8},
    // Both true-parallel classes reach zero. The tone before an arrival is
    // re-aimed over a bass pinned to a single octave, and where the consonant
    // window for that re-aim comes back empty it widens to admit a passing
    // dissonance rather than let the parallel ship; the cadential figure that
    // pins the bass under its own resolution is chosen against the three-line
    // surface it produces instead of installed over one settled without it.
    // Three true parallels left the strict column and one contrary-motion
    // arrival entered the battuta one -- a trade out of the fault the corpus
    // almost never writes and into the one it writes most freely.
    {FormType::ChoralePrelude, 0, 14, 13, 0},
    // Most of this form's parallel octaves are deliberate: the opening octave
    // cascade states its gesture high, an octave lower, then doubled in V0 and
    // V1 across a descending scale, which is a parallel octave on every one of
    // its sixteenths by design. The ceiling therefore cannot approach zero, and
    // a drop here means the surrounding figuration improved, not the cascade.
    // Escaping a same-direction perfect by reversing the wave turns similar
    // motion into contrary motion, so what leaves the strict column here tends
    // to arrive in the battuta one.
    // It shares its section builder with the fantasia, so every closure listed
    // for that form reaches this one too: the fifth column empties and hidden
    // halves, with the battuta column level and two anti-parallels leaving. The
    // octave column is unmoved and stays open -- its remaining forty-two come
    // from the toccata half, which this builder writes through a different path.
    {FormType::ToccataAndFugue, 42, 10, 84, 3},
    // The counter figuration is one continuous voice across the ground cycles
    // and is read as one at every seam; its oscillation tones rank a hidden
    // perfect below a true one; the cadential suspension is chosen against the
    // figuration it will sound with; and the closing trill takes its
    // termination, so the tonic is reached contrary to the ground rather than
    // beside it. What survives is a single fifth at a bar head whose repair band
    // holds no admissible tone, and one contrary-motion arrival that entered the
    // battuta column in exchange.
    {FormType::Passacaglia, 1, 20, 30, 9},
    // Almost all of what remains in the strict column is fifths, and they come
    // from the one place selection cannot reach: a stretto whose follower is the
    // leader's exact imitation an octave away, entering a whole bar later, so
    // the two lines attack together and the subject's own intervals decide what
    // sounds. Everything the builders do choose -- the bass support under the
    // running voices, the pedal under the free section's figuration -- is now
    // read at the grain those voices move at, which is what emptied the octave
    // column and took most of the hidden one with it.
    // Its stretto used to state the follower an octave below the leader at a
    // one-bar delay with nothing read first, so every place the subject's own
    // contour repeated a bar later was a parallel by construction; it now reads
    // four canon configurations and refuses one that sounds a true parallel. The
    // fill running up to that block is written before it rather than after, so
    // the block's lines have a preceding bar to be judged against instead of
    // reporting no motion at all. The half-cadence bass and the coda's inner
    // voice rank the register of a tone whose pitch class is the design value.
    // And the sustained support leaves the chord for a free diatonic tone once
    // no triad tone in the band would do. Nothing is traded here: both true
    // classes empty and the hidden column falls with them.
    {FormType::FantasiaAndFugue, 0, 11, 109, 6},
    {FormType::CelloPrelude, 0, 0, 0, 0},
    // Two voices only, so an arrival on a perfect interval meets a fixed bass
    // with no third part to hide behind. No true parallel of either class
    // survives; the remaining ways in are upward leaps, which is ordinary
    // cadential writing, so hidden carries the whole residue by design.
    {FormType::Chaconne, 0, 23, 0, 5},
    // Nothing here is repaired after the fact: the aria bass is immutable by
    // contract and a canon's two lines cannot be re-aimed one end at a time. The
    // strict column reaches zero because the imitative blocks are instead
    // assembled and read while their one free choice is still open -- a canon's
    // leader tones, the quodlibet tune's rotation -- and the free figuration
    // between them is relieved arrival by arrival. Hidden approaches are what
    // that choice pays with: the leader window of a wide canon is about a fifth
    // deep, so an arrival it can reach cleanly is often still approached by leap.
    {FormType::GoldbergVariations, 0, 8, 3, 0},
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
    std::printf("[counterpoint] %-20s par5=%zu par8=%zu hidden=%zu battuta=%zu anti=%zu\n",
                formLabel(entry.form), total.parallel_fifth, total.parallel_octave, total.hidden(),
                total.battuta, total.anti_parallel);
    EXPECT_LE(total.strict(), entry.max_strict)
        << formLabel(entry.form) << ": parallel perfect intervals in shipped output rose above the "
        << "ratchet (par5=" << total.parallel_fifth << " par8=" << total.parallel_octave << ")";
    EXPECT_LE(total.battuta, entry.max_battuta)
        << formLabel(entry.form) << ": ottava battuta in shipped output rose above the ratchet";
    EXPECT_LE(total.hidden(), entry.max_hidden)
        << formLabel(entry.form) << ": hidden perfect intervals in shipped output rose above the "
        << "ratchet (hidden5=" << total.hidden_fifth << " hidden8=" << total.hidden_octave << ")";
    EXPECT_LE(total.anti_parallel, entry.max_anti)
        << formLabel(entry.form)
        << ": anti-parallel perfect intervals in shipped output rose above "
        << "the ratchet";
  }

  std::sort(skipped.begin(), skipped.end());
  std::vector<std::string> expected = expectedSkippedCells();
  std::sort(expected.begin(), expected.end());
  EXPECT_EQ(skipped, expected) << "the set of form x character pairs that fail to generate changed";
  // Guards against a partial skip hiding inside an already-expected pair.
  EXPECT_EQ(composed_cells,
            kFormCeilings.size() * kCharacters.size() * kSeedCount - expected.size() * kSeedCount);
}

// --- Length axis -------------------------------------------------------------
//
// The sweep above composes every form at its natural length, and that one
// setting is not the shipped surface: a longer piece runs more episodes, more
// entries and more figuration, and its guards meet motion the natural length
// never produces. A fault that only appears once a form is stretched is
// therefore invisible to the ceilings above no matter how tight they are.
//
// This sweep re-measures the two forms whose figuration runs against theme
// entries -- the pairing that generates the length-dependent faults -- across
// every DurationScale, and holds them to their own ceilings. It is a second
// axis over the same counter, not a second counter.

constexpr std::array<DurationScale, 4> kScales = {{
    DurationScale::Short,
    DurationScale::Medium,
    DurationScale::Long,
    DurationScale::Full,
}};

struct LengthCeiling {
  FormType form;
  std::size_t max_strict;
  std::size_t max_hidden;
  std::size_t max_battuta;
  std::size_t max_anti;
};

// RATCHET: as above, these may only ever be LOWERED. Measured across
// 4 scales x 4 characters x 8 seeds x both modes.
constexpr std::array<LengthCeiling, 2> kLengthCeilings = {{
    // The stretto choice is worth far more on this axis than on the one above,
    // because a longer fugue states more strettos: the strict column falls from
    // ninety to eighteen with the fifth at zero throughout, against ten more
    // contrary-motion octave arrivals and four more anti-parallels. The
    // registered coda voicing and the ranked bar-head escape take the remaining
    // eighteen to zero for two more hidden perfects, and nothing else moves.
    {FormType::Fugue, 0, 98, 1270, 432},
    // The fugue half carries the same choices, and the prelude half adds no true
    // parallel of its own: thirty-six to zero, paid with six hidden perfects and
    // four anti-parallels, with the battuta column unmoved throughout.
    {FormType::PreludeAndFugue, 0, 69, 489, 223},
}};

TEST(ShippedCounterpointRatchet, PerfectMotionStaysUnderCeilingAtEveryLength) {
  for (const LengthCeiling& entry : kLengthCeilings) {
    PerfectMotionCounts total;
    for (DurationScale scale : kScales) {
      const std::uint16_t bars = resolveBars(entry.form, scale, /*target_bars=*/0);
      for (SubjectCharacter character : kCharacters) {
        for (std::uint32_t offset = 0; offset < kSeedCount; ++offset) {
          ComposeRequest request;
          request.form = entry.form;
          request.character = character;
          request.seed = kFirstSeed + offset;
          request.is_minor = (offset % 2) == 1;
          request.target_bars = bars;

          HarnessFixture fixture;
          if (buildFormFixture(request, &fixture) != FormDirectorStatus::Ok)
            continue;
          const ComposeResult result =
              Composer{}.run(fixture.material, fixture.harmony, fixture.voice_plan);
          ASSERT_FALSE(result.notes.empty());
          total.add(countPerfectMotion(result.notes));
        }
      }
    }
    std::printf("[counterpoint/length] %-20s par5=%zu par8=%zu hidden=%zu battuta=%zu anti=%zu\n",
                formLabel(entry.form), total.parallel_fifth, total.parallel_octave, total.hidden(),
                total.battuta, total.anti_parallel);
    EXPECT_LE(total.strict(), entry.max_strict)
        << formLabel(entry.form) << ": parallel perfect intervals rose above the ratchet once the "
        << "form is stretched (par5=" << total.parallel_fifth << " par8=" << total.parallel_octave
        << ")";
    EXPECT_LE(total.battuta, entry.max_battuta) << formLabel(entry.form) << ": battuta rose";
    EXPECT_LE(total.hidden(), entry.max_hidden) << formLabel(entry.form) << ": hidden rose";
    EXPECT_LE(total.anti_parallel, entry.max_anti)
        << formLabel(entry.form) << ": anti-parallel rose";
  }
}

}  // namespace
}  // namespace bach::composer
