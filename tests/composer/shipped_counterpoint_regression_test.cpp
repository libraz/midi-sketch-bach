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

// --- Unresolved second inversions -------------------------------------------
//
// The perfect fourth is the one interval whose consonance depends on where it
// sits: a consonance between upper voices, a dissonance above the bass. The
// grammar licenses it above the bass in three ways, and all three MOVE -- it
// resolves down by step over a held bass, the bass itself steps away under it,
// or it stands over a pedal. What is not licensed is a bass that simply parks
// under one: it holds into the next beat and the fourth above it neither
// resolves nor is left. That is the count below.
//
// Sampled on the beat grid, not at note onsets, because a vertical is heard for
// as long as it sounds: a whole-bar fourth is four beats of it, and an onset
// sweep would read the same thing as one event and rank it with a passing
// sixteenth. The interval classes the perfect-motion counter above reads are
// motions between two onsets, which is why the two use different grids.
struct BassFourthCounts {
  // Every beat sample of a voice sounding above the lowest one: the chances the
  // count below had. Same role as PerfectMotionCounts::successions.
  std::size_t upper_samples = 0;
  std::size_t unresolved = 0;

  void add(const BassFourthCounts& other) {
    upper_samples += other.upper_samples;
    unresolved += other.unresolved;
  }
};

BassFourthCounts countUnresolvedBassFourths(const std::vector<NoteEvent>& notes) {
  BassFourthCounts counts;
  if (notes.empty())
    return counts;

  Tick end = 0;
  std::vector<VoiceId> voices;
  for (const NoteEvent& note : notes) {
    end = std::max(end, note.start_tick + note.duration);
    if (std::find(voices.begin(), voices.end(), note.voice) == voices.end())
      voices.push_back(note.voice);
  }
  const SoundingPitchIndex index(notes);
  std::vector<std::uint8_t> sounding;
  std::vector<std::uint8_t> next_sounding;
  for (Tick tick = 0; tick < end; tick += kTicksPerBeat) {
    sounding.clear();
    next_sounding.clear();
    for (VoiceId voice : voices) {
      const std::uint8_t pitch = index.pitchAt(voice, tick);
      if (pitch != 0)
        sounding.push_back(pitch);
      const std::uint8_t next_pitch = index.pitchAt(voice, tick + kTicksPerBeat);
      if (next_pitch != 0)
        next_sounding.push_back(next_pitch);
    }
    if (sounding.size() < 2)
      continue;
    const std::uint8_t bass = *std::min_element(sounding.begin(), sounding.end());
    const bool bass_holds = !next_sounding.empty() &&
                            *std::min_element(next_sounding.begin(), next_sounding.end()) == bass;
    for (std::uint8_t pitch : sounding) {
      if (pitch == bass)
        continue;
      ++counts.upper_samples;
      if ((pitch - bass) % 12 != 5)
        continue;
      if (!bass_holds)
        continue;  // the bass steps away: the fourth was passing.
      const bool resolves =
          std::any_of(next_sounding.begin(), next_sounding.end(),
                      [&](std::uint8_t next) { return next + 1 == pitch || next + 2 == pitch; });
      if (!resolves)
        ++counts.unresolved;
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

TEST(BassFourthCounter, ReportsAFourthTheBassParksUnder) {
  // C3 held three beats with F4 above it, and the F stays where it is. The last
  // beat has no successor to hold into, so two of the three sound the fault.
  const std::vector<NoteEvent> notes = {
      makeNote(1, 0, 1440, 48),
      makeNote(0, 0, 1440, 65),
  };
  const BassFourthCounts counts = countUnresolvedBassFourths(notes);
  EXPECT_EQ(counts.unresolved, 2u);
  EXPECT_EQ(counts.upper_samples, 3u);
}

TEST(BassFourthCounter, AResolvingFourthIsLicensed) {
  // The same fourth, but the upper voice steps down to the third on beat two.
  const std::vector<NoteEvent> notes = {
      makeNote(1, 0, 960, 48),
      makeNote(0, 0, 480, 65),
      makeNote(0, 480, 480, 64),
  };
  EXPECT_EQ(countUnresolvedBassFourths(notes).unresolved, 0u);
}

TEST(BassFourthCounter, AFourthTheBassLeavesIsLicensed) {
  // The fourth stands, but the bass steps away under it: a passing vertical.
  const std::vector<NoteEvent> notes = {
      makeNote(1, 0, 480, 48),
      makeNote(1, 480, 480, 50),
      makeNote(0, 0, 960, 65),
  };
  EXPECT_EQ(countUnresolvedBassFourths(notes).unresolved, 0u);
}

TEST(BassFourthCounter, AFourthBetweenUPPERVoicesIsNotCounted) {
  // C2 in the bass, with G3 and C4 above it: the upper pair spans a fourth, and
  // over a chord-tone bass that is a consonance.
  const std::vector<NoteEvent> notes = {
      makeNote(2, 0, 960, 36),
      makeNote(1, 0, 960, 55),
      makeNote(0, 0, 960, 60),
  };
  EXPECT_EQ(countUnresolvedBassFourths(notes).unresolved, 0u);
  EXPECT_EQ(countUnresolvedBassFourths(notes).upper_samples, 4u);
}

TEST(BassFourthCounter, DegenerateVoicingsCountNothing) {
  EXPECT_EQ(countUnresolvedBassFourths({}).unresolved, 0u);
  const std::vector<NoteEvent> single_note = {makeNote(0, 0, 480, 60)};
  EXPECT_EQ(countUnresolvedBassFourths(single_note).upper_samples, 0u);
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
  // Fourths above the bass the bass then parks under, on the beat grid. The
  // columns above are all motions between two onsets; this one is a vertical,
  // and no amount of clean motion says anything about it. Its own denominator
  // is the beat samples of a voice sounding above the lowest one, which moves
  // with min_successions closely enough that the floor above guards both.
  std::size_t max_bass_fourth;
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
    // The contrary column is the shared beat-anchor selector's. That selector
    // ranks its candidates by what each one sounds against the voices already
    // moving, and the one motion it could not see was a perfect interval
    // repeated by contrary motion -- so a tone answering a rising line by
    // leaping down onto the octave it had just left scored as fully clean. The
    // tier that sees the class ranks it at every beat anchor, but what an anchor
    // will PAY to avoid one depends on where it falls: a bar head, held to the
    // chord, will leave the chord for it; a beat inside the bar, where the whole
    // scale is admissible, only reorders among tones it already had. The tier
    // sits BELOW the battuta
    // rather than above it, which is that selector's one inversion of "the
    // harsher-sounding fault is the worse one": measured as overshoot scaled by
    // the spread each class occupies in the reference corpus, the contrary
    // repeat costs several times what the battuta does, because the works
    // themselves write it far more sparingly.
    // It also sits below the selector's parallel-free escape, and that is what
    // makes it reach anything at all here. A bar head offers three chord pitch
    // classes and a sounding theme tone routinely leaves exactly one of them
    // consonant, so above the escape the tier would return the contrary arrival
    // every time it was the consonant one; below it the anchor takes a diatonic
    // tone that brushes the theme instead. Inside the bar the order is reversed:
    // there the escape would be paid for a fault that only arises once the chord
    // is spent, and an anchor is the register the bars after it start from, so
    // the tier ranks but never displaces off the chord. This column is what that
    // buys, and the battuta and hidden columns beside it are what it costs.
    //
    // The seam that hands a figuration span over to whatever follows it is
    // relieved by its own pass, and that pass reads the same ranking. It used to
    // fire on the true parallel alone, so a span could close by answering the
    // voice beside it with the perfect interval they had just left, reached the
    // other way round, and nothing downstream looked at the handover again. The
    // two classes are ranked there rather than pooled, which is what lets a
    // parallel seam still be relieved onto a contrary arrival -- strictly better
    // than what it replaces -- while a contrary seam may only be relieved onto a
    // tone free of both.
    // The succession floor sits below where it once did because the ornament
    // pass no longer decorates a subject statement, and every fault column above
    // it held at the value it had while those notes were still there. The notes
    // that left carried no perfect motion at all: they were surface. Removing
    // notes cannot invent a pitch, so the only way a column can rise afterwards
    // is by making two skeleton tones adjacent that an ornament had been sitting
    // between -- an unmasking, not a new fault, which is why the floor and the
    // columns have to be read together rather than either one alone.
    //
    // The chord this form's accompaniment is anchored on grew a fourth tone. A
    // bar acting as the dominant of the bar after it is spelled with its seventh,
    // and the anchor selector may take that tone and no longer reads the tritone
    // it makes with the third as a clash to be avoided. The contrary column is
    // where that shows: a bar head offering four pitch classes instead of three
    // far less often finds its consonant set collapsed to the one tone the line
    // just left, which is the condition that produced those arrivals. The battuta
    // column is unmoved and one cell of it gains a single arrival; against the
    // reference corpus, which prices a contrary arrival at several times a
    // battuta, that is the cheap end of the trade. The floor drops by a handful
    // of successions where a changed anchor lets a tone be sustained rather than
    // restruck.
    //
    // The ornament pass now suppresses an expansion whose transitions arrive on
    // a perfect interval by contrary motion, not similar motion alone. An
    // expansion that opens on the upper neighbour replaces the arrival tone the
    // builder chose, so the leap into it belongs to the ornament and the two
    // contrary classes are as much its doing as the similar one. Both of those
    // columns fall here, and the worst cell falls with them; the floor drops
    // because a suppressed expansion is notes that are no longer there. Nothing
    // rises, which is the shape a suppression should have: it can only remove
    // tones, and the only way it could add a fault is by making two skeleton
    // tones adjacent that an ornament had been sitting between.
    //
    // The bass now ranks tones that leave a fourth above it below those that do
    // not. Three columns rise for it -- thirteen hidden approaches, three
    // contrary repeats, one battuta and one worst battuta cell -- and this is the
    // first column that says what they buy: the same sweep run through the CLI
    // drops from 1142 unresolved second inversions to 188. The perfect approach
    // classes here are motions the ear follows and forgets; the fourth is a
    // vertical it sits inside for as long as the bass holds, which is why the
    // trade goes this way at seventeen faults gained against nine hundred lost.
    // The free lines inside a stated key area are now spelled in that key rather
    // than always in the home one, so the episode figuration, the Fortspinnung
    // sequences and the countersubject carry the accidentals the area names. A
    // line that changes a tone changes where it meets the other voices, so the
    // approach columns move; the cardinal ones do not, and neither does the
    // reference-corpus reading -- this form's counterpoint profile sits at zero
    // excess before and after. The bend is refused wherever it would worsen the
    // perfect-motion class at either end of the tone, so what remains here is
    // the residue of pairs that guard cannot see: two accompanying lines that
    // were each chosen against the home spelling of the other.
    //
    // The floor falls by a few dozen successions because a re-spelled tone
    // sometimes lands on the pitch its neighbour already sounds, and a repeat
    // is one onset where there were two.
    //
    // Two returns are now stated differently from the material they return to.
    // A returning episode spins the next four-note limb of the subject instead
    // of the opening one, and the final entry states the head in augmentation
    // rather than replaying the exposition's first bars note for note. The
    // floor drops by about a thousand successions, and that fall is the
    // augmentation itself: doubled note values are half as many onsets, so the
    // coda offers fewer pairs to examine. Battuta and the unresolved fourths
    // fall well past what that loss accounts for -- a fresh limb opens each
    // episode on a different chord tone, so the sequences stop arriving at the
    // same approach four times over. Hidden approaches and the worst hidden
    // cell rise: a subject tone held twice as long sits across more of the
    // accompaniment's motion beneath it, and every one of those crossings is a
    // new pair. The cardinal columns stay at zero and the rate stays an order
    // of magnitude under the reference corpus, which is what makes the weightier
    // close worth its approaches.
    //
    // The exposition now accumulates its voices: a voice that has not yet
    // stated the subject does not sound, so the third voice's four bars of free
    // figuration under the answer are gone. The floor falls by those bars. What
    // that filler was doing is visible in the columns it took with it -- the
    // contrary repeat drops by nearly half and battuta by a tenth, because the
    // voice that formed them was one the exposition should never have had.
    //
    // It also uncovered something the filler had been hiding. With nothing
    // under it, the answer becomes the lowest sounding voice, and the fourths
    // the countersubject leaves above it stop being fourths between upper
    // voices and become second inversions. The countersubject derivation is now
    // told when its source is the lowest voice and ranks that fourth below a
    // consonance, which takes the column past where it stood before the
    // exposition rule at the price of six hidden approaches.
    {FormType::Fugue, 0, 0, 77, 71, 4, 0, 3, 4, 1, 74479, 180},
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
    // the fugue row is that residue and little else.
    // Its prelude half is figuration over a moving bass throughout, so nearly
    // every one of its onsets is chosen against two lines that are both moving
    // and the beat anchor's contrary tier reaches most of them -- this is the
    // form where extending that tier past the bar head does the most work, and
    // it is also the form that pays the most for it. The contrary column falls
    // by nearly half and the hidden column rises by most of what it sheds; the
    // trade is worth taking because the reference corpus prices a contrary
    // arrival at several times a hidden one, writing the first far more
    // sparingly than the second in the same texture.
    // Its own contrary column is what the seam pass empties almost entirely: the
    // form's remaining arrivals were one shape repeated across the sweep, the
    // last figuration bar before the closing cadence handing over to it, and the
    // cadence tones themselves cannot move -- each voice's band holds exactly one
    // dominant, so the approach is the only end with a choice. The battuta and
    // hidden columns take what it sheds, and the succession floor drops a little
    // because a relieved tail sometimes lands where the next tone can be
    // sustained rather than restruck.
    // Its floor drops for the same reason as the fugue row's, and like that row
    // every fault column is unmoved: the exposition's subject and answer are the
    // densest ornament sites in the form, and none of what they carried was
    // counterpoint.
    //
    // Both halves take the dominant seventh described on the fugue row, and the
    // prelude half takes it on its own repeating pattern, where no related-key
    // approach has to be held back to a triad. Here the trade runs the other way
    // from the fugue's: the hidden column falls by nearly half and the battuta
    // column takes most of what it sheds. That is payable in the same terms --
    // the corpus writes a battuta more freely than a hidden perfect, so a rise
    // in the cheaper class against a fall in the dearer one is a net gain -- and
    // it is the prelude's thin two-voice texture that makes it happen: with only
    // one line to answer, an anchor stepping off a hidden approach has few places
    // to land that are not the octave below.
    //
    // The ornament suppression described on the fugue row reaches both halves.
    // Here it takes only the battuta column and the floor: this form's contrary
    // arrivals are already down to the residue the seam pass leaves, and none of
    // that residue is an ornament's doing.
    //
    // The bass ranking against the fourth costs this row seven hidden approaches
    // and returns thirty of the battuta arrivals plus 284 second inversions
    // across the CLI sweep. Its bass-fourth column ends at the lowest rate of any
    // form here, which is what a fugue whose bass is free to move should look
    // like.
    // Same spelling change as the fugue above, and this form absorbs it almost
    // entirely: every perfect-motion column holds. Only the bass fourth and the
    // floor move, and both for the same reason -- a bass tone re-spelled for the
    // area it sounds in resolves its fourth one step later, or lands on the
    // pitch it already sounded and stops being a second onset.
    //
    // The episode and coda devices from the fugue row reach this form's fugue
    // half too, and here they only give: hidden falls by a fifth, battuta by a
    // quarter, the fourths with them. The one column that rises is the worst
    // hidden cell, which is the augmented final entry concentrated into the one
    // configuration whose coda is longest -- the total fell while its worst cell
    // rose, so the approaches did not multiply, they gathered. The floor drops
    // with the doubled note values, as on the fugue.
    //
    // The exposition rule and the bass-aware countersubject reach this form's
    // fugue half unchanged. Hidden falls by a third and the fourths by nearly a
    // quarter of what they were before the exposition was corrected at all: a
    // form whose fugue half is short spends proportionally more of itself in
    // the exposition, so a rule that only touches the exposition shows up here
    // most. The contrary repeat is the one column that rises, by two, and it is
    // the same trade the fugue row pays -- a countersubject kept off the fourth
    // sometimes reaches its tone from the other side.
    {FormType::PreludeAndFugue, 0, 0, 12, 32, 5, 0, 2, 2, 1, 31885, 53},
    // Its hidden column is the one with room, and with a denominator in the row
    // that can be said as a rate rather than as a ratio to some other form. Both
    // operands are the columns below rather than figures restated here: a
    // measurement copied into prose beside the table it came from is one nothing
    // re-reads. A trade into hidden is payable; a trade into either true class is
    // not, at any distance, and neither survives here. The pedal is the voice
    // that pays -- written last against two settled manuals, once it ranks a
    // hidden perfect below a true one it steps onto the hidden approach rather
    // than keep the parallel it began with.
    //
    // That pedal ranks the battuta ABOVE the hidden perfect, inverting for this
    // one voice the order the rest of the product uses, and the hidden ceiling
    // here is deliberately loosened for it. Its band spans a thirteenth and a
    // triad puts three tones in it, so the guard is choosing between faults far
    // more often than it is finding a clean tone, which makes the order it
    // chooses by the thing that decides this form's profile. The reference
    // corpus does not settle which way the two should sit: measured across every
    // character, mode and seed, the sign of the comparison changes with which
    // stratum of the same repertoire is read, so the order stands on the ranking
    // argument rather than a measurement. Where the pedal was not making the
    // trade the output is byte-identical, so the loosening buys the arrivals and
    // nothing else.
    //
    // The contrary repeat -- leaving a perfect interval and reaching the same
    // one again the other way round -- is the one class the corpus is not
    // divided about, and the pedal ranks it at the bottom of what it will pay
    // rather than the first thing it reaches for. It is the last thing the ladder
    // spends, so the column reads two across the whole sweep and one in the
    // worst configuration.
    //
    // The other two then fall together, because the pedal stopped being boxed.
    // Its escape had only the bar's three chord tones to offer and the band
    // holds every octave of each, so when they were all faulty the guard was out
    // of chord rather than out of judgement -- and at about one such onset in
    // six a diatonic tone between them would have been clean. It may now take
    // one, held only to a fifth at either end so that a borrowed tone is walked
    // to and walked away from rather than jumped at.
    //
    // It is NOT held to consonance against the manuals. That condition was what
    // bound this row: the onsets where the chord is spent are the same onsets
    // where a manual sounds across the beat, so the tone that would have been
    // clean is usually the one that brushes it, and requiring consonance kept
    // nearly the whole column. Dropping it halves the column and takes the worst
    // configuration down with it. The trade was measured on the axis it moves --
    // the share of beat onsets carrying a sounding second, seventh or tritone --
    // and this voice runs at half the rate of the sparest three-voice organ work
    // in the reference corpus with the condition dropped, well inside the ceiling
    // the form's own vertical test holds it to. The perfect approach is the fault
    // the corpus is strict about; the passing second is one it writes constantly
    // in the same texture.
    //
    // Both manual voices now zigzag through four chord tones instead of three on
    // a bar acting as the dominant of the bar after it, the seventh being offered
    // on the accented beats alone. Every fault column falls, and the floor RISES
    // -- the only row here where it does. Both follow from the same fact: a
    // fourth tone in the set puts the next anchor a smaller interval away, so the
    // guard finds a clean rung more often and the line restrikes where it used to
    // sustain across a wider skip.
    //
    // The pedal now walks the bar instead of marking its chord, and the whole row
    // moves with it. It reaches down to the pedalboard's own low C rather than
    // stopping a fourth above: with the roots sitting near the bottom of a
    // thirteenth almost every tone the guard could offer approached them from
    // ABOVE, so the arrival was similar motion whenever the manuals descended,
    // which is where this voice spent its hidden perfects. An octave of room
    // underneath is what lets it arrive from below. Two conditions follow from
    // the walk itself: the connecting eighth is optional, so it is held to the
    // plain quarter it replaces and judged over both of its motions rather than
    // only the one into it; and a repair that would reach past an octave has
    // stopped repairing the line and started replacing it, so the band's new
    // width is not spendable as a leap. Where the walk still ends up boxed the
    // onset is handed to the middle manual, which is the voice still free to move
    // there -- and it is handed over for a hidden perfect and not only for a true
    // parallel, because a pedal written last against two settled lines is out of
    // room in both cases alike.
    //
    // The bass fourth column is the largest fall in this table and the plainest:
    // a bass that restates its tone under a changing upper pair leaves the fourth
    // standing, and one that steps walks out from underneath it. Two columns rise
    // in exchange. Contrary octave arrivals by downward leap nearly double,
    // which is what a bass that moves is for -- the reference corpus writes them
    // at a median rate this form stays under in every configuration but one, and
    // that one sits just past the ninety-fifth percentile while its counterpoint
    // profile as a whole reads inside the envelope. The contrary repeat leaves
    // zero for the first time, at a rate the corpus tolerates several times over.
    // The floor falls by a few dozen successions because the eighth that used to
    // be written wherever it fit is now refused wherever it costs more than the
    // quarter, so there are marginally fewer onsets to examine.
    {FormType::TrioSonata, 0, 0, 119, 31, 2, 0, 4, 2, 1, 34800, 9},
    // The tone before an arrival is re-aimed over a bass pinned to a single
    // octave, and where the consonant window for that re-aim comes back empty it
    // widens to admit a passing dissonance rather than let the parallel ship;
    // the cadential figure that pins the bass under its own resolution is chosen
    // against the three-line surface it produces instead of installed over one
    // settled without it. What that re-aim accepts is a weaker approach in place
    // of a worse one, which is why the residue sits in hidden and battuta.
    //
    // This is the one row where the ornament suppression raises a column: the
    // contrary column empties and the hidden column takes one arrival. That is
    // the unmasking the fugue row describes -- the suppressed expansion had been
    // sitting between two skeleton tones that read as a hidden perfect once they
    // became adjacent, so the fault was already in the line and the ornament was
    // covering it. The trade is still the right way round by a wide margin: the
    // reference corpus prices a contrary arrival at several times a hidden one,
    // so paying one of the cheap class to be rid of one of the dear class lowers
    // the weighted cost even as the raw count moves the other way.
    //
    // Nothing rises here: the bass ranking against the fourth takes four hidden
    // approaches, one battuta, one worst hidden cell and half the second
    // inversions with it. The cantus bass moves with the harmony rather than
    // being sustained under running voices, so the ranking has somewhere to go on
    // almost every onset.
    //
    // Its bass now leaves the chord on the weak beats. Every anchor of the
    // walking bass is a tone of its bar chord, so the line used to move anchor
    // to anchor by the intervals a triad offers and almost never by a step;
    // beats two and four are metrically weak and carry a passing or neighbour
    // tone between them instead, which is how a continuo bass fills the thirds
    // of its own harmony. A bass that steps where it used to leap arrives at
    // the same pitches as the voices above it more often, and the hidden
    // column is where that lands -- ten more approaches, no true parallel of
    // either class, and the contrary column still empty. The unresolved
    // fourths fall by six with it: a fourth above the bass resolves by step,
    // and a bass that is already stepping supplies the resolution.
    //
    // The floor drops by nine. Two Noble configurations lose three notes each,
    // and the notes are one ornament apiece: an expansion that had split a
    // quarter into four is no longer eligible over the changed bass, so six
    // fewer onsets are examined. No voice fell silent and no pair stopped
    // sounding together.
    {FormType::ChoralePrelude, 0, 0, 35, 21, 0, 0, 2, 2, 0, 17012, 47},
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
    // What was left after that was almost entirely one gesture -- the same two
    // wave lines, the same bar, the same pair of pitches, in every major-mode
    // configuration of this form and the fantasia. Its cause was an inversion
    // rather than an exhausted vocabulary: the wave's harshness escape, which
    // moves a passing tone off a minor 2nd / tritone / major 7th, would accept
    // any replacement short of a hidden perfect, so it could take a tone the
    // perfect-motion ranking had just settled as clean and hand back a battuta
    // in exchange for a passing second. Holding that escape to a replacement no
    // worse than what it replaces empties this column without moving any other
    // one: the two faults were never in competition, one was simply outranking
    // the other by omission.
    // Its contrary column falls furthest in proportion of any form's when the
    // shared beat anchor is given the tier described on the fugue row, and
    // nothing else in the row moves at all -- this form's bar heads were
    // producing that class and only that class.
    // Its battuta column tightens by one and its floor drops when the ornament
    // pass stops decorating subject statements: the arrival that leaves was an
    // ornament's own tone answering a manual, not a skeleton motion, so it is the
    // one class here that the surface was contributing rather than exposing.
    // Its contrary column tightens by one for the same reason and with the same
    // floor: the ornament suppression on the fugue row now reads that class too,
    // and one expansion was leaving a perfect interval only to reach it again the
    // other way round.
    //
    // The bass-fourth column nearly halves. This form's lowest voice is struck
    // once or twice a bar and holds while the manuals run, so almost every
    // interval it forms arrives after its own onset -- and it used to be ranked
    // against the one beat it was struck on. Read against the whole span it
    // holds, it stops taking the chord's fifth where a running voice will state
    // the root over it, which is the second inversion that column counts.
    //
    // The hidden column pays three for it, and the succession floor drops by
    // twenty-one. Both are the same tone: a bass that declines the fifth arrives
    // at the root more often, which is one more way to reach a perfect interval
    // by similar motion, and it sustains a little longer where it used to
    // re-articulate. The trade is priced against the reference corpus, where a
    // hidden perfect costs a small fraction of what an unresolved second
    // inversion does, and taken at a ratio of eighty-five to three.
    //
    // The size of the octave column is checked against the gesture's own model
    // rather than against the form. The archetype is picked by the seed, so
    // twelve of this sweep's forty-eight composed configurations reach the
    // cascade and each contributes exactly the seven sixteenths of its one
    // doubled bar; no other configuration contributes a single one. Read as a
    // rate over pair motions, a reaching configuration sounds seventy-six
    // parallel octaves per thousand where the organ corpus's upper envelope is
    // fifteen -- and where BWV 565, the work whose cascade this is, sounds a
    // hundred and three, the most extreme reading that corpus holds on this
    // rule. A gesture cannot be held to an envelope its own model sits outside
    // of, so this column is pinned against the model and the envelope is what
    // the rest of the row answers to. What that settles is the rate inside a
    // configuration that states the cascade, and only that: how often a form
    // should reach for the gesture at all is a question about the archetype's
    // share of the seeds, which no column here measures.
    // This form travels furthest of the four, and the row moves most. The chord
    // plan itself is now restated bar by bar in the key sounding at it, so the
    // local spelling reaches every tone derived from harmony -- the anchors, the
    // pedal, the punctuation, the chord blocks -- without any of those selectors
    // knowing a key area exists. The scalar fills between them are bent
    // afterwards. Almost the whole note mass of the piece therefore changes
    // pitch somewhere, and the columns that count how one line approaches
    // another change with it.
    //
    // The doubled line is untouched: the structural and strict columns stand at
    // the same 84, which is the octave doubling this form writes deliberately
    // and not counterpoint at all. Read against the reference corpus the profile
    // is where it was -- excess distance unmoved, the contrary octave arrival at
    // zero in every configuration sampled, and the hidden rate an order below
    // the corpus ninety-fifth percentile. What rose is a raw count over the
    // sweep, not a rate the style objects to.
    //
    // The floor falls because a re-spelled tone sometimes lands on the pitch its
    // neighbour already sounds, which merges two onsets into one.
    //
    // Two returns are now stated on a different surface. The free section's
    // pedal alternates its two densities so a returning pedal tone is not the
    // same bar twice, and the stretto leader treads a dotted derivation of the
    // subject rhythm rather than the exposition's own values. The floor rises,
    // because a struck pedal is two onsets where a held one was one, and the
    // hidden column falls with the pedal that stops answering the same tone the
    // same way. The unresolved fourths rise: dotting the leader holds its longer
    // value across the bass's next move, so a fourth that used to resolve within
    // the pair now waits for the following one. The doubled line and both
    // cardinal columns are untouched at 84.
    //
    // The exposition rule reaches the fugue half here too, and this form had the
    // most to gain from it: its third voice was sounding from the second bar of
    // the opening statement, six bars before its own entry, and its coverage was
    // ragged besides. Hidden falls to a third of what it was, the contrary
    // repeat by half, the worst contrary cell to one, and the fourths by a
    // third. The floor falls by the filler bars. Both cardinal columns stand at
    // the same 84, which remains the deliberate octave doubling.
    {FormType::ToccataAndFugue, 84, 84, 13, 12, 6, 7, 2, 1, 1, 28019, 210},
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
    // Its counter figuration's beat anchor is displaced off a contrary octave
    // arrival as well as off a same-direction one, but only toward a tone free
    // of every approach fault -- the relaxing pass that accepts a hidden perfect
    // stays reserved for stepping off a true parallel, because this anchor is
    // load-bearing and every off-beat tone of its beat derives from it. Both
    // strict columns stay empty with it in, and the hidden and contrary columns
    // fall alongside the battuta rather than paying for it. Its contrary column
    // then falls again with the shared beat anchor's new tier, and the worst
    // cell falls with it, which is what says the arrivals removed were reachable
    // rather than merely numerous.
    // The cadential suspension is now stated at the close of every ground cycle
    // rather than only the last, so the floor drops -- the figure ends in a rest
    // that replaces onsets the variation would otherwise have struck. The
    // contrary column falls again with it, because the suspension's own three
    // motions against the immutable ground are vetted where the variation tone
    // they replace was not. Its battuta column holds only because those motions
    // are read for that class too: a rewrite that decides a bar head against a
    // bass which cannot answer reaches a contrary unison as readily as it
    // reaches a perfect interval in similar motion, and the interior cycles have
    // no closing-gesture claim that would justify paying for one.
    //
    // This form takes no dominant seventh, and the exclusion is a decision rather
    // than an omission. Its grounds have exactly one bar per cycle whose root
    // falls a fifth, and that bar is the one the cadential suspension is
    // installed on -- the suspension being an authored dissonance with its own
    // preparation and resolution, chosen against the figuration it will sound
    // with. Spelling that bar with a seventh moves the figuration the suspension
    // search reads and can leave a cycle close with no admissible suspension at
    // all, which trades a prepared accented dissonance for an unprepared one. The
    // two columns that fall here and the floor with them are the ornament
    // suppression described on the fugue row, and nothing else.
    //
    // Byte-identical under the bass ranking: this form's lowest voice is the
    // ground, and the ground is immutable. Its bass-fourth column is pinned at
    // what the ground table itself produces.
    // The ground now decorates the last beat of a bar with a diatonic neighbour,
    // rotating the figure so no two statements come back on the same surface.
    // Battuta halves: the contrary arrival by downward leap was the bass leaping
    // from one bar's held tone into the next, and a neighbour on the way out of
    // the bar turns that leap into a step. The bass repair pass that vets the
    // voices above now reads the ground at beat grain rather than one tone per
    // bar, which is what lets it see the decorated tone at all -- at bar grain it
    // was vetting against a tone the bass had already left. The floor gives up
    // forty pairs where a decoration is withdrawn for reaching a perfect interval
    // and the bar falls back to its plain statement.
    {FormType::Passacaglia, 0, 0, 48, 11, 5, 0, 3, 1, 1, 27298, 190},
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
    // register ranking steps when clean is unreachable, and this form keeps the
    // largest one in the table even after the shared beat anchor is taught to
    // see the class -- the register ranking is a separate site from that anchor
    // and still trades into it. It shares the fugue tail's battuta-scored
    // countersubject with the toccata and pays for it in the same two columns,
    // for the reason given there, and takes the realization-time repair with it.
    // Its succession floor rises rather than falling: the avoiding line's wider
    // intervals split sustains that had read as one motion.
    // It then falls again, further than it rose, when subject statements stop
    // being ornamented, and every fault column holds -- including the contrary
    // one, which is a register decision made before the surface exists and so is
    // untouched by what the surface stops adding.
    //
    // Its bass is the same sustained pedal the toccata uses and it answers the
    // same way once that pedal is ranked against the whole span it holds rather
    // than the beat it is struck on: the bass-fourth column falls by more than
    // four hundred, and the hidden column pays three for it while the succession
    // floor drops by nine. The reasoning and the price are the toccata's,
    // recorded there.
    // The same per-bar restatement of the chord plan, and here it TIGHTENS more
    // than it loosens. The contrary repeat falls by a third and the contrary
    // octave arrival by a downward leap leaves entirely: a bass that changes its
    // spelling for the area it sounds in stops arriving at the same octave from
    // the same side, which is the figure both columns are made of. Measured per
    // thousand against the reference corpus the contrary repeat also concentrates
    // -- it now appears in a handful of configurations instead of most of them,
    // and the summed rate across the sample is roughly half what it was.
    //
    // The bass fourth rises for the plainest reason: the tone that resolves a
    // fourth is sometimes the one the local key re-spells, so the resolution
    // arrives a step later than the sample window looks. The hidden column moves
    // with the same three tones.
    //
    // The free-section pedal and the stretto leader now vary their surface on a
    // return, the same two devices the toccata row describes. This form takes
    // only gains from them: hidden falls by a sixth, the contrary repeat by one,
    // and the floor rises with the struck pedal's extra onsets. The fourths hold
    // exactly where they were, so the dotted leader costs this form nothing --
    // its bass moves under the leader more often than the toccata's does, and a
    // fourth that has somewhere to go resolves inside the pair.
    //
    // Same exposition rule, and this row moves furthest of the four. Hidden
    // falls to a quarter, the contrary repeat by a third, both worst cells to
    // one, and the fourths by two fifths -- this form kept the largest contrary
    // column in the table, and a good part of it turns out to have been the
    // third voice answering an entry it had no business accompanying.
    {FormType::FantasiaAndFugue, 0, 0, 7, 0, 14, 0, 1, 0, 1, 28643, 290},
    {FormType::CelloPrelude, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    // Two voices only, so an arrival on a perfect interval meets a fixed bass
    // with no third part to hide behind. No true parallel of either class
    // survives; the remaining ways in are upward leaps, which is ordinary
    // cadential writing, so hidden carries the whole residue by design.
    //
    // Its cycle now spells the bar whose root falls a fifth into the next
    // statement's tonic as a dominant seventh, and the variation states that
    // seventh on the bar's last beat. Nothing forces the pairing: read as a
    // straight line the plan has no dominant at all, because a ground's dominant
    // is always the bar the cycle wraps from. The hidden column falls because the
    // seventh is the one chord tone that cannot itself arrive as a perfect
    // interval over a bass tracking the chord root, so every beat it occupies is
    // a beat the hidden approach had no way to reach. The floor rises by two
    // where a changed anchor restrikes.
    // The same decorated returns as the passacaglia row, and this form takes
    // them for nothing: every fault column holds and the floor rises by the
    // onsets the decorations add. Its ground is short enough that a statement
    // returns four times in a piece, so the rotation of figures is heard as a
    // rotation rather than as four unrelated bars.
    {FormType::Chaconne, 0, 0, 43, 0, 7, 0, 1, 0, 1, 7644, 31},
    // Nothing here is repaired after the fact: the aria bass is immutable by
    // contract and a canon's two lines cannot be re-aimed one end at a time. The
    // strict columns are zero because the imitative blocks are instead assembled
    // and read while their one free choice is still open -- a canon's leader
    // tones, the quodlibet tune's rotation -- and the free figuration between
    // them is relieved arrival by arrival. Hidden approaches are what that
    // choice pays with: the leader window of a wide canon is about a fifth deep,
    // so an arrival it can reach cleanly is often still approached by leap.
    //
    // The aria bass now walks. Its bar used to be built from the root, third
    // and fifth alone, which moves by a third or a fifth at every one of its
    // eight positions and made a chain of same-direction thirds the bass's
    // whole vocabulary; the two weak positions of the first half now carry the
    // scale degree between root and third, read under the bar's own harmony so
    // a minor-key dominant takes the raised sixth rather than an augmented
    // second. The strict columns stay at zero. What the step costs is the
    // approaches: four more hidden and the contrary column opening at eight,
    // one of them in a single cell. A stepping bass under a figuration that is
    // itself predominantly stepwise reaches a perfect interval from a step on
    // both sides, which is the contrary class by definition, and the relief
    // pass answers for it at the eighth the bass actually moves in rather than
    // at the beat. The floor RISES by twelve, because a bass that changes tone
    // where it used to restate one gives the pair something to be examined on.
    //
    // Where the ground leaves no canon interval writable against the walking
    // bass the plain triad statement ships instead, for the whole piece: the
    // answer follows from the ground alone, so it is settled once and every
    // reader of the bass agrees, and a canon whose two voices can clear neither
    // a crossing nor a true parallel is the harder constraint.
    //
    // The bass now states its four-bar cycle on a different surface each time it
    // returns -- plainly in the aria, then dotted, then with its beat heads
    // re-articulated, then gathering into the canon block -- while the cycle's
    // pitches and bar heads stay exactly what the aria laid down. The floor
    // rises by a sixth because re-articulating a tone is two onsets where there
    // was one, and hidden falls by a third: the returns no longer arrive at the
    // same approach from the same rhythmic place four times over. Both contrary
    // columns hold, and every cardinal column stays at zero.
    {FormType::GoldbergVariations, 0, 0, 8, 8, 8, 0, 1, 1, 1, 17919, 0},
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
    BassFourthCounts bass_fourths;
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
          bass_fourths.add(countUnresolvedBassFourths(notes));
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
    std::printf("[bass-fourth] %-20s unresolved=%zu / upper-samples=%zu\n",
                formTypeToString(entry.form), bass_fourths.unresolved, bass_fourths.upper_samples);
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
    EXPECT_LE(bass_fourths.unresolved, entry.max_bass_fourth)
        << formTypeToString(entry.form)
        << ": fourths above a bass that then parks under them rose above the "
        << "ratchet (upper samples=" << bass_fourths.upper_samples << ")";
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
  // Second inversions the bass parks on, for the reason given on FormCeiling.
  std::size_t max_bass_fourth;
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
    // The hidden column is one higher than the shorter axis leaves it, and that
    // one is the unmasking the form row describes rather than a fault the piece
    // gained: a stretched fugue holds more long subject tones, so it had more
    // ornaments sitting between skeleton pitches, and one of those pairs is a
    // hidden approach that only becomes adjacent once its ornament is gone. It is
    // the cheapest of the four classes and the form scores clear of the reference
    // envelope with it, which is why the exposure is worth more than the cover.
    // The dominant seventh pays off hardest on this axis, because a stretched
    // fugue passes through more dominants: all three fault columns fall together,
    // and the contrary one -- the dearest of the three by the corpus -- falls by
    // a fifth. Its worst cell comes down with it. What the form row pays as a
    // single extra battuta arrival is not paid here at all.
    // The ornament suppression on the form row is worth most on this axis for
    // the same reason: more length means more ornament sites, so more of them
    // were leaping onto a perfect interval. The battuta and contrary columns fall
    // by a tenth and a fifth of themselves, the worst battuta cell with them, and
    // the floor drops by the notes the suppressed expansions were.
    // The bass ranking against the fourth pays for itself hardest on this axis
    // in both directions: the hidden column rises by a third, and the battuta,
    // contrary and both worst cells all fall -- the contrary column by nearly
    // half. A stretched fugue passes through more bars where the bass is free to
    // choose, so both sides of the trade are larger, and by the corpus weighting
    // the two classes that fall cost several times what the one that rises does.
    // Reading a sustained bass across the span it holds rather than the beat it
    // is struck on reaches this form only through the countersubject's seam
    // arrival, which was the one anchor of a span that nothing judged. Every
    // fault column holds; the second inversions come down and the floor moves by
    // six pairs in three quarters of a million, which is the seam tone landing
    // where the next one can be sustained instead of restruck.
    // Across every length the spelling change costs proportionally less than it
    // does at the natural length: six more hidden approaches in three hundred,
    // and the contrary octave arrival tightens by twenty-five. A longer piece
    // spends more of itself in the home key, because the key areas are anchored
    // to the exposition and the development rather than scaled with the form.
    //
    // The varied returns read differently across the lengths than they do at the
    // natural one. Hidden and the fourths fall here where they rose there,
    // because a stretched fugue holds more episodes than the subject has limbs,
    // so the rotation spreads over material that used to repeat many times over
    // rather than four. What rises is the contrary pair -- battuta, the contrary
    // repeat, and both their worst cells -- which is the augmented final entry:
    // one long tone per length, and the longer the piece the more accompaniment
    // moves beneath it. The floor drops by the onsets that augmentation removes.
    //
    // Across the lengths the exposition rule gives and takes nothing: every
    // column falls, the contrary repeat by nearly a third and both worst cells
    // with it. A stretched fugue has the same one exposition as a short one, so
    // the rule's reach does not scale with the form -- what scales is the
    // development around it, which is why the proportional gain here is smaller
    // than on the form row while the direction is the same.
    {FormType::Fugue, 0, 0, 311, 803, 28, 0, 5, 8, 1, 752412, 2210},
    // The fugue half carries the same choices and the prelude half adds no true
    // parallel of its own at any length. Its hidden column is the one that rises
    // with the beat anchor's contrary tier reaching past the bar head, and the
    // succession floor drops with it: an anchor that moves off a contrary
    // arrival sometimes lands where the next tone can be sustained rather than
    // restruck, which is one fewer pair of consecutive onsets.
    // It drops again with the subject statements left bare, and unlike the fugue
    // row nothing is unmasked: this form's stretched fault columns all hold.
    // With the dominant spelled as a seventh the contrary column falls by nearly
    // two thirds and both worst cells come down, at the price of nine battuta
    // arrivals -- the same trade as the form row, at a far better rate, because
    // length gives the prelude's anchor chain more places to break a hidden
    // approach without answering it in the octave. The floor rises here rather
    // than dropping: fewer sustained repeats means more onset pairs to judge.
    // The ornament suppression then takes the battuta arrivals that trade bought
    // and a third of the contrary column besides, so both columns end below where
    // they stood before the seventh was spelled at all. The floor drops back for
    // the notes the suppressed expansions were.
    // The bass ranking takes every total column down here and the floor up with
    // them, at the price of one stretched configuration's battuta count. That
    // single worst cell is the whole cost on this row.
    // The countersubject's seam arrival then reaches this form the way it
    // reaches the fugue, and with the same shape: no fault column moves, the
    // second inversions fall, and the floor gives up six pairs in half a
    // million.
    // Same across lengths, and smaller still: two more hidden approaches in a
    // hundred and eighty, and the contrary octave arrival tightens by seven. The
    // prelude half carries no key area at all, so half of every piece here is
    // untouched however long it runs.
    //
    // The varied returns take hidden down by a twelfth and battuta by a twentieth
    // across the lengths, and leave the contrary repeat and every worst cell
    // where they stood. The fourths are the one column that rises, and by the
    // same amount the form row's does: the augmented final entry holds its tone
    // across the bass's next move, so a fourth beneath it waits a pair longer to
    // resolve. The floor falls with the onsets augmentation removes.
    //
    // Same again, and every column falls here too. The fourths come down by
    // seven per cent of where they stood, which is the bass-aware
    // countersubject reaching every length rather than only the natural one:
    // the exposition it is derived in is the same exposition however long the
    // piece runs, and the line it produces is restated at every later entry.
    {FormType::PreludeAndFugue, 0, 0, 150, 320, 9, 0, 3, 5, 1, 478198, 1110},
}};

TEST(ShippedCounterpointRatchet, PerfectMotionStaysUnderCeilingAtEveryLength) {
  for (const LengthCeiling& entry : kLengthCeilings) {
    PerfectMotionCounts structural_total;
    PerfectMotionCounts total;
    std::size_t worst_strict = 0;
    std::size_t worst_hidden = 0;
    std::size_t worst_battuta = 0;
    std::size_t worst_anti = 0;
    BassFourthCounts bass_fourths;
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
            bass_fourths.add(countUnresolvedBassFourths(notes));
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
    std::printf("[bass-fourth] %-20s unresolved=%zu / upper-samples=%zu\n",
                formTypeToString(entry.form), bass_fourths.unresolved, bass_fourths.upper_samples);
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
    EXPECT_LE(bass_fourths.unresolved, entry.max_bass_fourth)
        << formTypeToString(entry.form)
        << ": fourths above a parked bass rose at some length (upper samples="
        << bass_fourths.upper_samples << ")";
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
