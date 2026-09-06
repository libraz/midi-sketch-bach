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
// note array the shipped path emits, bypassing the validator and its exemptions
// entirely, and holds each form to a per-form ceiling. The judge of a single
// motion pair is the shared classifyPerfectMotion; only the sampling and
// bookkeeping around it are written here.
//
// TWO SURFACES, because the piece has two and neither one describes it alone.
//
// Composer::run produces the counterpoint; the ornament pass then rewrites the
// same array, and it is the pass's output that ships. A trill, a turn or a
// slide replaces one held tone with several moving ones, so the pass both adds
// motion of its own and -- because the two structural tones are no longer
// consecutive onsets -- hides motion that was there before it. Decoration
// hiding a parallel is not the same as a form not writing one, and a ratchet
// that reads only the decorated array would let a form close a gate by
// ornamenting over the fault instead of removing it.
//
// So both are counted:
//
//   structural : Composer::run's own output. The strict column only. This is
//                the counterpoint the form builders actually wrote, and no
//                amount of decoration may be what brings it to zero.
//   shipped    : after the ornament pass. All four columns. Everything past the
//                pass -- velocity curve, renderer, MIDI writer -- leaves pitch
//                and onset alone, so this is the final contrapuntal surface,
//                and it is the surface the product's own gate reads
//                (applyCounterpointBudget over FinalScore validation).
//
// The ornament configuration is not rebuilt here. resolveFixtureOrnamentContext
// is the product's own derivation, called with the product's own instrument
// default and tempo, because a decoration density chosen locally would put this
// file back to describing a note array of its own.

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

// For kDefaultBpm alone: trill pacing is tempo-dependent, so the shipped note
// array is the one decorated at the tempo the product defaults to. This is a
// compile-time constant, not a link dependency -- bach_composer_tests still
// links bach_composer_lib only.
#include "application/composition_service.h"
#include "composer/composer.h"
#include "composer/form_director.h"
#include "composer/harness_fixture.h"
#include "composer/ornament_pass.h"
#include "core/basic_types.h"
#include "core/pitch_utils.h"

namespace bach::composer {
namespace {

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
  // Consecutive pair successions examined: every place two voices both sound at
  // an onset and both sounded at the onset before it, which is exactly the set
  // of chances any of the columns above had to find something. Without it the
  // columns are numerators with no denominator, and a count that falls cannot be
  // told from a count that falls because there is less music to count in.
  std::size_t successions = 0;

  std::size_t strict() const { return parallel_fifth + parallel_octave; }
  std::size_t hidden() const { return hidden_fifth + hidden_octave; }

  void add(const PerfectMotionCounts& other) {
    parallel_fifth += other.parallel_fifth;
    parallel_octave += other.parallel_octave;
    hidden_fifth += other.hidden_fifth;
    hidden_octave += other.hidden_octave;
    battuta += other.battuta;
    anti_parallel += other.anti_parallel;
    successions += other.successions;
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
          ++counts.successions;
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

// Major and minor are different music -- the minor material has its own
// subjects, its own leading tone and its own final chord -- so both are swept
// against every seed rather than interleaved across them. Tying the mode to the
// seed's parity halves the grid and leaves each seed measured in one mode only,
// which is a regression surface no ceiling can describe.
constexpr std::array<bool, 2> kModes = {{false, true}};

constexpr std::uint32_t kFirstSeed = 1;
constexpr std::uint32_t kSeedCount = 8;

std::uint32_t totalTicks(const std::vector<NoteEvent>& notes) {
  std::uint32_t last = 0;
  for (const NoteEvent& note : notes)
    last = std::max(last, note.start_tick + note.duration);
  return last;
}

// Both note arrays for one request: the composed counterpoint, and that same
// counterpoint decorated exactly as the product decorates it. Returns false
// when the form director refuses the request, which is the only reason a cell
// is allowed to be missing.
bool composeSurfaces(const ComposeRequest& request, std::vector<NoteEvent>* structural,
                     std::vector<NoteEvent>* shipped) {
  HarnessFixture fixture;
  if (buildFormFixture(request, &fixture) != FormDirectorStatus::Ok)
    return false;
  ComposeResult result = Composer{}.run(fixture.material, fixture.harmony, fixture.voice_plan);
  *structural = result.notes;
  OrnamentParams ornament;
  ornament.character = request.character;
  ornament.instrument = defaultInstrumentForForm(request.form);
  ornament.mode = request.is_minor ? detail::Mode::Minor : detail::Mode::Major;
  ornament.seed = request.seed;
  ornament.bpm = application::kDefaultBpm;
  resolveFixtureOrnamentContext(fixture, request.form, totalTicks(result.notes), &ornament);
  applyOrnamentPass(result, ornament);
  *shipped = std::move(result.notes);
  return true;
}

struct FormCeiling {
  FormType form;
  std::size_t max_structural_strict;  // parallel fifths + octaves before decoration
  std::size_t max_strict;             // the same two classes on the array that ships
  std::size_t max_hidden;             // hidden fifths + hidden octaves, shipped
  std::size_t max_battuta;            // contrary-motion octave arrivals by downward leap
  std::size_t max_anti;               // a perfect class left and reached again in contrary motion
  // The worst single configuration of the sweep, per shipped column. A sum
  // cannot separate a fault population that was REMOVED from one that merely
  // MOVED: redistributing findings across cells leaves the total wherever it
  // was, and so does removing some from one cell while another gains the same
  // number. A worst case does not survive removal, so the two statistics
  // together say which happened. One accumulator, not a second instrument.
  std::size_t max_cell_strict;
  std::size_t max_cell_hidden;
  std::size_t max_cell_battuta;
  std::size_t max_cell_anti;
  // FLOOR, not a ceiling: the pair successions the sweep must still examine.
  // Every column above is a numerator over this. A change that lowers a fault
  // count by writing less music lowers this with it, and a fault count read
  // alone cannot tell that from a repair -- a rate and a count can even move in
  // opposite directions, since one has a denominator and the other does not.
  // This may only ever be RAISED, and lowering it is a deliberate statement that
  // the form now has less counterpoint in it.
  std::size_t min_successions;
};

// Per-form ceilings on perfect-motion events found across the whole
// character x mode x seed sweep.
//
// The mode axis is half the product and is not optional here: minor draws on its
// own subjects, resolves its own leading tone and closes on its own final chord,
// so a ceiling measured over the major surface alone describes half of what
// ships and leaves the other half free to regress silently. It is swept as a
// full product against the seeds rather than interleaved across them, because
// interleaving measures each seed in one mode only and calls the result both.
//
// The two strict columns are the same two rules read on the two surfaces the
// piece has. Where they differ, decoration is standing between the structural
// tones: the ornamented array has onsets between them, so the pair is no longer
// consecutive and the counter no longer reads it. That is a real difference in
// what the product ships and a real difference in what its gate sees, but it is
// not the form builder writing better counterpoint, which is why the structural
// column is pinned separately and is the one that may never be closed by
// decoration.
//
// RATCHET: these numbers may only ever be LOWERED, never raised. They are the
// counts measured from current output, not a target, and the sweep is fully
// deterministic (fixed seeds, fixed characters, natural bar counts), so there is
// no run-to-run noise for a margin to absorb: they are pinned exactly. A form
// that reaches 0 stays pinned at 0. Raising a ceiling to make a change pass
// would throw away the only regression signal this file provides -- the fix
// belongs in the form builder's material derivation instead.
//
// The grid and the two arrays are part of the measurement, not incidental to
// it: a count taken over a different seed range, a different mode axis or a
// different point in the pipeline is a different quantity and cannot be
// compared with these or pinned in their place.
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
//
// Row order, since ten bare numbers do not read on their own:
//   form,
//   structural strict, then the sweep totals: strict, hidden, battuta, anti,
//   then the worst single configuration: strict, hidden, battuta, anti,
//   then the succession floor -- the only number here that may not FALL.
constexpr std::array<FormCeiling, 10> kFormCeilings = {{
    // The stretto lays two verbatim theme statements against each other, so its
    // canon configuration is the one choice in this form that decides a parallel
    // outright, and it is made while the whole overlap is still readable: a
    // configuration that sounds a true parallel is refused even when it is the
    // only quiet one on offer, because the dissonance is a matter of degree and
    // the parallel is the prohibition. The coda's cadence voicing is written
    // into the tone registry like any other figuration, so the seam handing the
    // wave over to it reads as a hand-over rather than as a rest. And the
    // bar-head escape ranks a sustain-window clash below the parallel instead of
    // vetoing on it, since against a theme walking in seconds the escape
    // vocabulary is regularly clash-free nowhere.
    //
    // Both strict columns are zero and the residue is hidden and contrary
    // motion. That direction is the only payable one: in the reference corpus
    // the similar-motion parallel is the rarest thing measured -- across the
    // three-voice works its octave rate is zero at every percentile including
    // the maximum, and its fifth rate is zero through the ninety-fifth -- while
    // hidden perfects are written freely in exactly this texture. There is no
    // quantity of true parallel that buys anything back.
    // The battuta column is the one the countersubject builder now reads at the
    // grain it ships at. That builder scores one anchor per source note and then
    // realizes a beat as four sixteenths, so the tone preceding an anchor in the
    // output is often one the scoring never saw: an arpeggio returning from its
    // fifth can leap into an octave that the anchor it came from approached by
    // step. Judging the emitted tone instead closes that gap here and in every
    // form that shares the builder, and it moves the worst cell rather than only
    // the total -- which is what says the arrivals removed were the reachable
    // ones and not just the plentiful ones. The repair ranks and never pools:
    // a candidate sounding a true parallel is refused whatever else it is clean
    // of, and when nothing clears both the parallel-free tone stands and keeps
    // its battuta. The hidden column pays a little for it, in the direction
    // already argued above.
    {FormType::Fugue, 0, 0, 37, 101, 123, 0, 3, 4, 5, 80723},
    // The fugue half is assembled by the same section builder as the bare fugue,
    // so every closure above holds here unchanged. The prelude half writes its
    // two voices through the same parallel-aware wave: its bass support tone is
    // read against the running voices at the grain they actually move at rather
    // than a bar back and ranks a hidden perfect below a true one, and its
    // figuration leaves the chord for a free diatonic tone at bar heads where no
    // chord tone is playable at all.
    // Its battuta column carries the canonical countersubject's own arrivals:
    // the exposition derives that line twice and keeps the battuta-avoiding
    // derivation whenever the avoiding line still restates over the third
    // entry, so the arrivals that survive are the ones a restatable line cannot
    // trade away. What is left after the realization-time repair described on
    // the fugue row is that residue and little else, and here the hidden and
    // contrary columns fall with it rather than paying for it.
    {FormType::PreludeAndFugue, 0, 0, 15, 46, 58, 0, 1, 2, 3, 38130},
    // Its hidden column is the one with room, and with a denominator in the row
    // that can be said as a rate rather than as a ratio to some other form. Its
    // hidden total over its succession floor is the densest hidden writing in
    // the product, and still a lower rate than a typical work of the reference
    // corpus's THREE-VOICE group, which writes hidden perfects freely in exactly
    // this texture. Both operands are the columns below rather than figures
    // restated here: a measurement copied into prose beside the table it came
    // from is one nothing re-reads. That comparison is a sweep-wide
    // rate against a per-work distribution and is not like for like, but the gap
    // is not one a units mismatch closes. A trade into hidden is payable at that
    // distance; a trade into either true class is not, at any distance, and
    // neither survives here. The pedal is the voice that pays -- written last
    // against two settled manuals, once it ranks a hidden perfect below a true
    // one it steps onto the hidden approach rather than keep the parallel it
    // began with.
    {FormType::TrioSonata, 0, 0, 184, 135, 8, 0, 7, 4, 2, 34770},
    // The tone before an arrival is re-aimed over a bass pinned to a single
    // octave, and where the consonant window for that re-aim comes back empty it
    // widens to admit a passing dissonance rather than let the parallel ship;
    // the cadential figure that pins the bass under its own resolution is chosen
    // against the three-line surface it produces instead of installed over one
    // settled without it. What that re-aim accepts is a weaker approach in place
    // of a worse one, which is why the residue sits in hidden and battuta.
    {FormType::ChoralePrelude, 0, 0, 28, 22, 1, 0, 3, 2, 1, 16992},
    // Most of this form's parallel octaves are deliberate: the opening octave
    // cascade states its gesture high, an octave lower, then doubled in V0 and
    // V1 across a descending scale, which is a parallel octave on every one of
    // its sixteenths by design. The ceiling therefore cannot approach zero, and
    // a drop here means the surrounding figuration improved, not the cascade.
    // Escaping a same-direction perfect by reversing the wave turns similar
    // motion into contrary motion, so what leaves the strict column here tends
    // to arrive in the battuta one.
    // It shares its section builder with the fantasia, so every closure listed
    // for that form reaches this one too and the fifth column is empty. The
    // octave column stays open: all of it comes from the toccata half, which
    // this builder writes through a different path, and it is the largest
    // similar-motion population left anywhere in the product. Its worst-cell
    // strict figure below is the entire content of that one bar, which is what
    // says the octave column is one gesture repeated across the configurations
    // that reach it rather than a fault distributed over the form.
    // Its countersubject is scored against the ottava battuta as well as the
    // similar-motion approach, which this tail can afford and the fugue family
    // cannot: no later entry restates that line at a degree shift, so the wider
    // ambit the avoidance costs has nothing downstream that must still
    // octave-fit it. The battuta column buys the hidden and contrary ones, and
    // those are the two the reference corpus prices lowest of the three -- it
    // puts several of this form's configurations outside its battuta envelope,
    // measures a hidden rate this form writes well under, and measures no
    // contrary-arrival class at all. With the realization-time repair on the
    // fugue row applied on top, the configurations that sat outside that
    // envelope are inside it, and the hidden and contrary columns paid nothing
    // further for the second step.
    {FormType::ToccataAndFugue, 84, 84, 28, 22, 18, 7, 2, 3, 2, 33426},
    // The counter figuration is one continuous voice across the ground cycles
    // and is read as one at every seam; its oscillation tones rank a hidden
    // perfect below a true one; the cadential suspension is chosen against the
    // figuration it will sound with; and the closing trill takes its
    // termination, so the tonic is reached contrary to the ground rather than
    // beside it. The ground is immutable, so the variation is the only side of
    // the pair that can move, and every onset it owns is read against the ground
    // at the grain the ground actually moves at: a cycle that states it in
    // quarters moves three times inside a bar, so the cadential suspension that
    // rewrites one of those tones after the scrub has passed it re-reads the
    // beat-grain reference rather than the bar head it would otherwise inherit.
    {FormType::Passacaglia, 0, 0, 56, 65, 13, 0, 3, 3, 1, 27735},
    // Its stretto reads four canon configurations and refuses one that sounds a
    // true parallel, where the follower would otherwise be the leader's exact
    // imitation an octave away at a fixed one-bar delay -- the subject's own
    // contour repeating a bar later is a parallel by construction. The fill
    // running up to that block is written before it rather than after, so the
    // block's lines have a preceding bar to be judged against instead of
    // reporting no motion at all. The half-cadence bass and the coda's inner
    // voice rank the register of a tone whose pitch class is the design value,
    // since walking each voice up from its own band floor puts them a fixed
    // perfect interval apart by construction. And the sustained support leaves
    // the chord for a free diatonic tone once no triad tone in the band would
    // do. Both strict columns are empty; the anti-parallel column is where the
    // register ranking steps when clean is unreachable, and the corpus writes
    // that class freely. It shares the fugue tail's battuta-scored
    // countersubject with the toccata and pays for it in the same two columns,
    // for the reason given there, and takes the realization-time repair with it.
    // Its succession floor rises rather than falling: the avoiding line's wider
    // intervals split sustains that had read as one motion.
    {FormType::FantasiaAndFugue, 0, 0, 31, 31, 39, 0, 2, 3, 2, 34908},
    {FormType::CelloPrelude, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    // Two voices only, so an arrival on a perfect interval meets a fixed bass
    // with no third part to hide behind. No true parallel of either class
    // survives; the remaining ways in are upward leaps, which is ordinary
    // cadential writing, so hidden carries the whole residue by design.
    {FormType::Chaconne, 0, 0, 47, 0, 7, 0, 1, 0, 1, 7507},
    // Nothing here is repaired after the fact: the aria bass is immutable by
    // contract and a canon's two lines cannot be re-aimed one end at a time. The
    // strict columns are zero because the imitative blocks are instead assembled
    // and read while their one free choice is still open -- a canon's leader
    // tones, the quodlibet tune's rotation -- and the free figuration between
    // them is relieved arrival by arrival. Hidden approaches are what that
    // choice pays with: the leader window of a wide canon is about a fifth deep,
    // so an arrival it can reach cleanly is often still approached by leap.
    {FormType::GoldbergVariations, 0, 0, 8, 8, 0, 0, 1, 1, 0, 15354},
}};

// Form x character pairs the form director refuses by design: the chorale
// prelude carries a cantus firmus that the light characters cannot sustain,
// and the dramatic toccata is antithetical to the dignified Noble affect.
// Held as an explicit expectation so a NEW generation failure surfaces as a
// diff in the skipped set rather than as a silently smaller sweep.
std::vector<std::string> expectedSkippedCells() {
  return {
      std::string(formTypeToString(FormType::ChoralePrelude)) + " x " +
          subjectCharacterToString(SubjectCharacter::Playful),
      std::string(formTypeToString(FormType::ChoralePrelude)) + " x " +
          subjectCharacterToString(SubjectCharacter::Restless),
      std::string(formTypeToString(FormType::ToccataAndFugue)) + " x " +
          subjectCharacterToString(SubjectCharacter::Noble),
  };
}

TEST(ShippedCounterpointRatchet, PerfectMotionStaysUnderPerFormCeiling) {
  std::vector<std::string> skipped;
  std::size_t composed_cells = 0;

  for (const FormCeiling& entry : kFormCeilings) {
    PerfectMotionCounts structural_total;
    PerfectMotionCounts total;
    // Composite maxima, not per-sub-column ones: the largest fifth count and the
    // largest octave count can belong to different configurations, so adding
    // their maxima would name a cell that does not exist and loosen the ceiling.
    std::size_t worst_strict = 0;
    std::size_t worst_hidden = 0;
    std::size_t worst_battuta = 0;
    std::size_t worst_anti = 0;
    for (SubjectCharacter character : kCharacters) {
      bool character_skipped = false;
      for (bool is_minor : kModes) {
        for (std::uint32_t offset = 0; offset < kSeedCount; ++offset) {
          ComposeRequest request;
          request.form = entry.form;
          request.character = character;
          request.seed = kFirstSeed + offset;
          request.is_minor = is_minor;

          std::vector<NoteEvent> structural;
          std::vector<NoteEvent> notes;
          if (!composeSurfaces(request, &structural, &notes)) {
            character_skipped = true;
            continue;
          }
          ASSERT_FALSE(notes.empty())
              << formTypeToString(entry.form) << " x " << subjectCharacterToString(character)
              << " seed " << request.seed;
          ++composed_cells;
          structural_total.add(countPerfectMotion(structural));
          const PerfectMotionCounts cell = countPerfectMotion(notes);
          total.add(cell);
          worst_strict = std::max(worst_strict, cell.strict());
          worst_hidden = std::max(worst_hidden, cell.hidden());
          worst_battuta = std::max(worst_battuta, cell.battuta);
          worst_anti = std::max(worst_anti, cell.anti_parallel);
        }
      }
      if (character_skipped) {
        skipped.push_back(std::string(formTypeToString(entry.form)) + " x " +
                          subjectCharacterToString(character));
      }
    }

    // Emitted on every run so the current measurement is visible when the
    // ratchet is tightened after a counterpoint fix.
    std::printf(
        "[counterpoint] %-20s structural=%zu par5=%zu par8=%zu hidden=%zu battuta=%zu "
        "anti=%zu | worst-cell strict=%zu hidden=%zu battuta=%zu anti=%zu | successions=%zu\n",
        formTypeToString(entry.form), structural_total.strict(), total.parallel_fifth,
        total.parallel_octave, total.hidden(), total.battuta, total.anti_parallel, worst_strict,
        worst_hidden, worst_battuta, worst_anti, total.successions);
    EXPECT_LE(structural_total.strict(), entry.max_structural_strict)
        << formTypeToString(entry.form)
        << ": parallel perfect intervals in the composed counterpoint rose "
        << "above the ratchet (par5=" << structural_total.parallel_fifth
        << " par8=" << structural_total.parallel_octave << ")";
    EXPECT_LE(total.strict(), entry.max_strict)
        << formTypeToString(entry.form)
        << ": parallel perfect intervals in shipped output rose above the "
        << "ratchet (par5=" << total.parallel_fifth << " par8=" << total.parallel_octave << ")";
    EXPECT_LE(total.battuta, entry.max_battuta)
        << formTypeToString(entry.form)
        << ": ottava battuta in shipped output rose above the ratchet";
    EXPECT_LE(total.hidden(), entry.max_hidden)
        << formTypeToString(entry.form)
        << ": hidden perfect intervals in shipped output rose above the "
        << "ratchet (hidden5=" << total.hidden_fifth << " hidden8=" << total.hidden_octave << ")";
    EXPECT_LE(total.anti_parallel, entry.max_anti)
        << formTypeToString(entry.form)
        << ": anti-parallel perfect intervals in shipped output rose above "
        << "the ratchet";
    // The worst single configuration, held beside the totals above. A change
    // that leaves a total where it was while moving findings between cells is a
    // redistribution, not a repair, and only this reads it.
    EXPECT_LE(worst_strict, entry.max_cell_strict)
        << formTypeToString(entry.form) << ": one configuration's parallel perfect count rose";
    EXPECT_LE(worst_hidden, entry.max_cell_hidden)
        << formTypeToString(entry.form) << ": one configuration's hidden perfect count rose";
    EXPECT_LE(worst_battuta, entry.max_cell_battuta)
        << formTypeToString(entry.form) << ": one configuration's battuta count rose";
    EXPECT_LE(worst_anti, entry.max_cell_anti)
        << formTypeToString(entry.form) << ": one configuration's anti-parallel count rose";
    EXPECT_GE(total.successions, entry.min_successions)
        << formTypeToString(entry.form) << ": the sweep examines fewer pair successions than it "
        << "did, so any column that fell may have fallen because there is less music in the form";
  }

  std::sort(skipped.begin(), skipped.end());
  std::vector<std::string> expected = expectedSkippedCells();
  std::sort(expected.begin(), expected.end());
  EXPECT_EQ(skipped, expected) << "the set of form x character pairs that fail to generate changed";
  // Guards against a partial skip hiding inside an already-expected pair.
  const std::size_t cells_per_pair = kSeedCount * kModes.size();
  EXPECT_EQ(composed_cells,
            (kFormCeilings.size() * kCharacters.size() - expected.size()) * cells_per_pair);
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
  std::size_t max_structural_strict;
  std::size_t max_strict;
  std::size_t max_hidden;
  std::size_t max_battuta;
  std::size_t max_anti;
  // The worst single configuration, for the reason given on FormCeiling.
  std::size_t max_cell_strict;
  std::size_t max_cell_hidden;
  std::size_t max_cell_battuta;
  std::size_t max_cell_anti;
  // FLOOR, for the reason given on FormCeiling.
  std::size_t min_successions;
};

// RATCHET: as above, these may only ever be LOWERED. Measured across
// 4 scales x 4 characters x 2 modes x 8 seeds.
constexpr std::array<LengthCeiling, 2> kLengthCeilings = {{
    // The stretto choice is worth more on this axis than on the one above,
    // because a longer fugue states more strettos and every one of them is a
    // place where a canon configuration decides a parallel outright. Both
    // strict columns stay empty however far the form is stretched; the battuta
    // and anti-parallel columns grow with the length, which is what a longer
    // piece of the same counterpoint looks like.
    // Both columns fall here for the reason given on the form axis, and the
    // longer the piece the more of the fall is the realization-time repair:
    // subdividing a beat into sixteenths is what a longer span gives the
    // countersubject builder more room to do, so it is also where more of the
    // unjudged arrivals were.
    {FormType::Fugue, 0, 0, 205, 1014, 864, 0, 6, 13, 9, 776477},
    // The fugue half carries the same choices and the prelude half adds no true
    // parallel of its own at any length.
    {FormType::PreludeAndFugue, 0, 0, 162, 355, 484, 0, 3, 5, 8, 504121},
}};

TEST(ShippedCounterpointRatchet, PerfectMotionStaysUnderCeilingAtEveryLength) {
  for (const LengthCeiling& entry : kLengthCeilings) {
    PerfectMotionCounts structural_total;
    PerfectMotionCounts total;
    std::size_t worst_strict = 0;
    std::size_t worst_hidden = 0;
    std::size_t worst_battuta = 0;
    std::size_t worst_anti = 0;
    for (DurationScale scale : kScales) {
      const std::uint16_t bars = resolveBars(entry.form, scale, /*target_bars=*/0);
      for (SubjectCharacter character : kCharacters) {
        for (bool is_minor : kModes) {
          for (std::uint32_t offset = 0; offset < kSeedCount; ++offset) {
            ComposeRequest request;
            request.form = entry.form;
            request.character = character;
            request.seed = kFirstSeed + offset;
            request.is_minor = is_minor;
            request.target_bars = bars;

            std::vector<NoteEvent> structural;
            std::vector<NoteEvent> notes;
            if (!composeSurfaces(request, &structural, &notes))
              continue;
            ASSERT_FALSE(notes.empty());
            structural_total.add(countPerfectMotion(structural));
            const PerfectMotionCounts cell = countPerfectMotion(notes);
            total.add(cell);
            worst_strict = std::max(worst_strict, cell.strict());
            worst_hidden = std::max(worst_hidden, cell.hidden());
            worst_battuta = std::max(worst_battuta, cell.battuta);
            worst_anti = std::max(worst_anti, cell.anti_parallel);
          }
        }
      }
    }
    std::printf(
        "[counterpoint/length] %-20s structural=%zu par5=%zu par8=%zu hidden=%zu "
        "battuta=%zu anti=%zu | worst-cell strict=%zu hidden=%zu battuta=%zu anti=%zu | "
        "successions=%zu\n",
        formTypeToString(entry.form), structural_total.strict(), total.parallel_fifth,
        total.parallel_octave, total.hidden(), total.battuta, total.anti_parallel, worst_strict,
        worst_hidden, worst_battuta, worst_anti, total.successions);
    EXPECT_LE(structural_total.strict(), entry.max_structural_strict)
        << formTypeToString(entry.form)
        << ": parallel perfect intervals in the composed counterpoint rose "
        << "above the ratchet once the form is stretched (par5=" << structural_total.parallel_fifth
        << " par8=" << structural_total.parallel_octave << ")";
    EXPECT_LE(total.strict(), entry.max_strict)
        << formTypeToString(entry.form)
        << ": parallel perfect intervals rose above the ratchet once the "
        << "form is stretched (par5=" << total.parallel_fifth << " par8=" << total.parallel_octave
        << ")";
    EXPECT_LE(total.battuta, entry.max_battuta) << formTypeToString(entry.form) << ": battuta rose";
    EXPECT_LE(total.hidden(), entry.max_hidden) << formTypeToString(entry.form) << ": hidden rose";
    EXPECT_LE(total.anti_parallel, entry.max_anti)
        << formTypeToString(entry.form) << ": anti-parallel rose";
    EXPECT_LE(worst_strict, entry.max_cell_strict)
        << formTypeToString(entry.form) << ": one stretched configuration's parallel count rose";
    EXPECT_LE(worst_hidden, entry.max_cell_hidden)
        << formTypeToString(entry.form) << ": one stretched configuration's hidden count rose";
    EXPECT_LE(worst_battuta, entry.max_cell_battuta)
        << formTypeToString(entry.form) << ": one stretched configuration's battuta count rose";
    EXPECT_LE(worst_anti, entry.max_cell_anti)
        << formTypeToString(entry.form) << ": one stretched configuration's anti-parallel rose";
    EXPECT_GE(total.successions, entry.min_successions)
        << formTypeToString(entry.form) << ": the stretched sweep examines fewer pair successions";
  }
}

}  // namespace
}  // namespace bach::composer
