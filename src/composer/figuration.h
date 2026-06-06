#ifndef BACH_COMPOSER_FIGURATION_H
#define BACH_COMPOSER_FIGURATION_H

#include <array>
#include <cstdint>

#include "composer/span.h"
#include "composer/voice_plan.h"

namespace bach::composer::detail {

// 5 subject patterns × 16 quarter-note pitches each. Diatonic to C
// major / A natural-minor. Same catalog the gtest harness uses; the
// canonical copy lives here so the harness test and the CLI dispatch
// path stay byte-identical.
inline constexpr std::array<std::array<std::uint8_t, 16>, 5> kSubjectPatterns = {{
    // 0: original arch
    {72, 74, 76, 77, 79, 81, 79, 77, 76, 74, 76, 77, 79, 77, 71, 72},
    // 1: descent then ascent (start high)
    {84, 83, 84, 79, 77, 76, 77, 79, 81, 79, 77, 76, 74, 76, 71, 72},
    // 2: broken triad outline
    {79, 76, 79, 84, 76, 79, 76, 72, 74, 77, 74, 71, 72, 76, 71, 72},
    // 3: stepwise sequence
    {71, 72, 76, 77, 74, 76, 77, 79, 76, 77, 79, 81, 77, 79, 71, 72},
    // 4: upper-arch
    {76, 77, 79, 81, 79, 77, 76, 74, 72, 74, 76, 77, 79, 77, 71, 72},
}};

// Phase14-only subject catalog. The all-technique fugue permeates every bar
// with the subject (exposition, answer, V2 re-entry, middle entry, diminution,
// stretto, episode), so a statistically weak subject drags the whole piece's
// model_prob. Slots 0/1/4 keep the kSubjectPatterns melodies; slots 2/3 replace
// the two lowest-scoring patterns (the "broken triad" and "stepwise sequence"
// melodies scored ~0.86 / ~0.91 in isolation vs ~0.95 for the others) with
// higher-probability diatonic subjects. Both replacements keep the same
// register envelope (71-81) and the mandatory B->C (71,72) leading-tone tail
// so the cadence / leading-tone provenance bits still fire and the
// answer(-5) / V2(-12) / stretto(-24) transposes stay voice-crossing-safe.
// This catalog is referenced ONLY by buildPhase14Fixture, so the other fugue
// layouts stay byte-identical.
inline constexpr std::array<std::array<std::uint8_t, 16>, 5> kPhase14Subjects = {{
    // 0: original arch (unchanged)
    {72, 74, 76, 77, 79, 81, 79, 77, 76, 74, 76, 77, 79, 77, 71, 72},
    // 1: gentle wave (replaces the original high 84-83-84 head, which scored
    // lowest in-context of the kept subjects; this diatonic wave keeps the
    // 71,72 leading-tone tail and a 72-79 register that stays voice-safe)
    {76, 74, 72, 74, 76, 77, 79, 77, 76, 79, 77, 76, 74, 72, 71, 72},
    // 2: neighbour-rich arch (replaces the weak broken-triad subject)
    {79, 77, 76, 77, 79, 81, 79, 77, 76, 74, 72, 74, 76, 74, 71, 72},
    // 3: varied scalar arch. Opens on the same 72,74,76,77 head as slot 0 so
    // the V1 counterline search space over bars 0-3 stays in the validated,
    // diminished-melodic-free region. The body climbs to 81 and then descends
    // with a varied conjunct contour instead of restating the opening 72-77
    // cell verbatim; that de-repetition lifts the model_prob of the seeds that
    // select this slot. Keeps the mandatory 71,72 leading-tone tail.
    {72, 74, 76, 77, 79, 77, 79, 81, 79, 77, 76, 74, 76, 74, 71, 72},
    // 4: varied upper-arch. Same idea as slot 3: the body folds back through
    // 77-79 rather than running a single long descent, so the contour is less
    // predictable. Register 72-81 and the 71,72 leading-tone tail are kept.
    {76, 77, 79, 81, 79, 77, 76, 77, 79, 77, 76, 74, 72, 74, 71, 72},
}};

// Fugue subject rhythm profiles. Each row has the same 16 pitch positions as
// kPhase14Subjects, but durations now define the 4-bar subject span. The rows
// deliberately mix eighths, quarters, dotted quarters, halves, and one
// sixteenth figure while summing to exactly four 4/4 bars.
inline constexpr std::array<std::array<Tick, 16>, 5> kPhase14SubjectRhythms = {{
    {kTicksPerBeat, kTicksPerBeat / 2, kTicksPerBeat / 2, kTicksPerBeat, kTicksPerBeat,
     kTicksPerBeat / 2, kTicksPerBeat / 2, kTicksPerBeat, kTicksPerBeat, kTicksPerBeat,
     kTicksPerBeat / 2, kTicksPerBeat / 2, kTicksPerBeat, kTicksPerBeat, kTicksPerBeat,
     4 * kTicksPerBeat},
    {kTicksPerBeat / 2, kTicksPerBeat / 2, kTicksPerBeat, 2 * kTicksPerBeat, kTicksPerBeat,
     kTicksPerBeat / 2, kTicksPerBeat / 2, kTicksPerBeat, kTicksPerBeat, kTicksPerBeat,
     kTicksPerBeat / 2, kTicksPerBeat / 2, kTicksPerBeat, kTicksPerBeat, kTicksPerBeat,
     4 * kTicksPerBeat},
    {kTicksPerBeat, kTicksPerBeat, kTicksPerBeat, kTicksPerBeat, kTicksPerBeat, kTicksPerBeat,
     kTicksPerBeat, kTicksPerBeat, kTicksPerBeat, kTicksPerBeat, kTicksPerBeat / 2,
     kTicksPerBeat / 2, kTicksPerBeat, kTicksPerBeat, kTicksPerBeat, 2 * kTicksPerBeat},
    {kTicksPerBeat, kTicksPerBeat / 2, kTicksPerBeat / 4, kTicksPerBeat / 4, kTicksPerBeat,
     kTicksPerBeat, kTicksPerBeat / 2, kTicksPerBeat / 2, kTicksPerBeat, kTicksPerBeat / 2,
     kTicksPerBeat / 2, kTicksPerBeat, kTicksPerBeat, kTicksPerBeat, kTicksPerBeat,
     4 * kTicksPerBeat},
    {3 * kTicksPerBeat / 2, kTicksPerBeat / 2, kTicksPerBeat, kTicksPerBeat / 2, kTicksPerBeat / 2,
     kTicksPerBeat, kTicksPerBeat, kTicksPerBeat, kTicksPerBeat / 2, kTicksPerBeat / 2,
     kTicksPerBeat, kTicksPerBeat, kTicksPerBeat, kTicksPerBeat, kTicksPerBeat, 4 * kTicksPerBeat},
}};

struct ChordSpec {
  std::uint8_t root_pc;
  bool minor;
};

// 4 harmony patterns × 4 chords each. Roman numerals for reference:
// 0=I-IV-V-I, 1=I-vi-IV-V, 2=I-IV-I-V, 3=I-V-vi-I (deceptive resolved).
inline constexpr std::array<std::array<ChordSpec, 4>, 4> kHarmonyPatterns = {{
    {{{0, false}, {5, false}, {7, false}, {0, false}}},
    {{{0, false}, {9, true}, {5, false}, {7, false}}},
    {{{0, false}, {5, false}, {0, false}, {7, false}}},
    {{{0, false}, {7, false}, {9, true}, {0, false}}},
}};

/**
 * @brief Append one SequentialCounterline span covering a single bar.
 * @param vp Voice plan receiving the new span.
 * @param next_id Span id counter, post-incremented for the appended span.
 * @param voice Voice index the span is assigned to.
 * @param bar Bar index whose [bar, bar+1) tick window the span covers.
 * @param subdivision Note-placement granularity for the span.
 * @param voice_center Tessitura anchor for the counterline (0 = default).
 */
void pushCounterlineBar(VoicePlan& vp, SpanId& next_id, std::uint8_t voice, int bar,
                        Subdivision subdivision, std::uint8_t voice_center = 0);

// C natural-minor scale membership (pitch class), used to build the Phase16
// chaconne variations' stepwise figuration.
constexpr bool phase16InScale(int pc) {
  const int p = ((pc % 12) + 12) % 12;
  return p == 0 || p == 2 || p == 3 || p == 5 || p == 7 || p == 8 || p == 10;
}

// Walk `steps` scale degrees upward from `midi` within C natural minor.
inline int phase16ScaleUp(int midi, int steps) {
  int cur = midi;
  for (int s = 0; s < steps; ++s) {
    for (int add = 1; add <= 12; ++add) {
      if (phase16InScale(cur + add)) {
        cur += add;
        break;
      }
    }
  }
  return cur;
}

// C major scale membership (pitch class), used to build the Phase17 organ-
// prelude figuration's stepwise runs.
constexpr bool phase17InScale(int pc) {
  const int p = ((pc % 12) + 12) % 12;
  return p == 0 || p == 2 || p == 4 || p == 5 || p == 7 || p == 9 || p == 11;
}

// Walk `steps` scale degrees upward from `midi` within C major.
inline int phase17ScaleUp(int midi, int steps) {
  int cur = midi;
  for (int s = 0; s < steps; ++s) {
    for (int add = 1; add <= 12; ++add) {
      if (phase17InScale(cur + add)) {
        cur += add;
        break;
      }
    }
  }
  return cur;
}

// Diatonic scale selector for the dispatch helpers below.
enum class Mode : std::uint8_t { Major, Minor };

/**
 * @brief Walk `steps` diatonic scale degrees upward from `midi`.
 * @param midi Starting MIDI pitch.
 * @param steps Number of scale degrees to ascend.
 * @param mode Diatonic mode selecting the scale (Major = C major, Minor = C
 *             natural minor).
 * @return The MIDI pitch `steps` scale degrees above `midi`.
 */
inline int scaleUp(int midi, int steps, Mode mode) {
  return mode == Mode::Major ? phase17ScaleUp(midi, steps) : phase16ScaleUp(midi, steps);
}

/**
 * @brief Test diatonic scale membership of a pitch class.
 * @param midi MIDI pitch (or pitch class) to test.
 * @param mode Diatonic mode selecting the scale (Major = C major, Minor = C
 *             natural minor).
 * @return True if the pitch class belongs to the selected scale.
 */
inline bool inScale(int midi, Mode mode) {
  return mode == Mode::Major ? phase17InScale(midi) : phase16InScale(midi);
}

}  // namespace bach::composer::detail

#endif  // BACH_COMPOSER_FIGURATION_H
