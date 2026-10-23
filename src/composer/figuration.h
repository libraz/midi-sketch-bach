#ifndef BACH_COMPOSER_FIGURATION_H
#define BACH_COMPOSER_FIGURATION_H

#include <array>
#include <cstddef>
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

// FugueComplete-only subject catalog. The all-technique fugue permeates every bar
// with the subject (exposition, answer, V2 re-entry, middle entry, diminution,
// stretto, episode), so a statistically weak subject drags the whole piece's
// model_prob. Slots 0/1/4 keep the kSubjectPatterns melodies; slots 2/3 replace
// the two lowest-scoring patterns (the "broken triad" and "stepwise sequence"
// melodies scored ~0.86 / ~0.91 in isolation vs ~0.95 for the others) with
// higher-probability diatonic subjects. Both replacements keep the same
// register envelope (71-81) and the mandatory B->C (71,72) leading-tone tail
// so the cadence / leading-tone provenance bits still fire and the
// answer(-5) / V2(-12) / stretto(-24) transposes stay voice-crossing-safe.
// This catalog is referenced ONLY by buildFugueCompleteFixture, so the other fugue
// layouts stay byte-identical.
inline constexpr std::array<std::array<std::uint8_t, 16>, 5> kFugueCompleteSubjects = {{
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
// kFugueCompleteSubjects, but durations now define the 4-bar subject span. The rows
// deliberately mix eighths, quarters, dotted quarters, halves, and one
// sixteenth figure while summing to exactly four 4/4 bars.
inline constexpr std::array<std::array<Tick, 16>, 5> kFugueCompleteSubjectRhythms = {{
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
  // The chord sounds its minor seventh (root + 10) as a fourth chord tone: a
  // dominant seventh over a major triad, a minor seventh over a minor one. The
  // seventh is a tone the anchor selector may take, and the tritone it makes
  // with the third of a dominant is the harmony itself rather than a clash
  // against it -- without this flag the accented dissonance that defines the
  // dominant is unreachable, because every selector below reads a bare triad.
  bool seventh = false;
};

// Pitch class of the chord's seventh. Only meaningful when `seventh` is set.
constexpr int chordSeventhPc(const ChordSpec& chord) {
  return (chord.root_pc + 10) % 12;
}

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

// C natural-minor scale membership (pitch class), used to build the Chaconne
// chaconne variations' stepwise figuration.
constexpr bool chaconneInScale(int pc) {
  const int p = ((pc % 12) + 12) % 12;
  return p == 0 || p == 2 || p == 3 || p == 5 || p == 7 || p == 8 || p == 10;
}

// Walk `steps` scale degrees upward from `midi` within C natural minor.
inline int chaconneScaleUp(int midi, int steps) {
  int cur = midi;
  for (int s = 0; s < steps; ++s) {
    for (int add = 1; add <= 12; ++add) {
      if (chaconneInScale(cur + add)) {
        cur += add;
        break;
      }
    }
  }
  return cur;
}

// C major scale membership (pitch class), used to build the OrganPrelude organ-
// prelude figuration's stepwise runs.
constexpr bool organPreludeInScale(int pc) {
  const int p = ((pc % 12) + 12) % 12;
  return p == 0 || p == 2 || p == 4 || p == 5 || p == 7 || p == 9 || p == 11;
}

// Walk `steps` scale degrees upward from `midi` within C major.
inline int organPreludeScaleUp(int midi, int steps) {
  int cur = midi;
  for (int s = 0; s < steps; ++s) {
    for (int add = 1; add <= 12; ++add) {
      if (organPreludeInScale(cur + add)) {
        cur += add;
        break;
      }
    }
  }
  return cur;
}

// Diatonic scale selector for the dispatch helpers below.
enum class Mode : std::uint8_t { Major, Minor };

// A pitch-class set: bit n is pitch class n.
using PcMask = std::uint16_t;

constexpr PcMask pcBit(int pitch_class) {
  return static_cast<PcMask>(1u << static_cast<unsigned>(((pitch_class % 12) + 12) % 12));
}

// Tones of a chord as a pitch-class set, its seventh included when declared.
constexpr PcMask chordPcMask(const ChordSpec& chord) {
  const int root = chord.root_pc;
  PcMask mask =
      static_cast<PcMask>(pcBit(root) | pcBit(root + (chord.minor ? 3 : 4)) | pcBit(root + 7));
  if (chord.seventh)
    mask = static_cast<PcMask>(mask | pcBit(chordSeventhPc(chord)));
  return mask;
}

// Semitone offset above the tonic of each of the seven letters, tonic first.
using ScaleSpelling = std::array<int, 7>;

/**
 * @brief Spell the seven letters of a key under the harmony sounding over it.
 *
 * This is the one place that decides whether minor degrees 6 and 7 are raised.
 * Major is the plain major scale whatever the chord. Minor starts from the
 * natural minor; a chord tone outside it replaces the letter it raises, and a
 * non-chord letter left an augmented second below its neighbour is raised too.
 * Over the major dominant that gives the raised 6th and leading tone in either
 * direction, so no line meets its own chord third with the lowered 7th; with no
 * chord, or over a chord inside the natural minor, it is the natural minor.
 *
 * @param tonic_pc Tonic pitch class of the local key.
 * @param mode Mode of the local key.
 * @param chord_pcs Tones of the harmony at the tick (0 = no harmony known).
 * @return Offsets above the tonic, strictly increasing, one per letter.
 */
constexpr ScaleSpelling spellScale(int tonic_pc, Mode mode, PcMask chord_pcs) {
  ScaleSpelling spelled = mode == Mode::Major ? ScaleSpelling{0, 2, 4, 5, 7, 9, 11}
                                              : ScaleSpelling{0, 2, 3, 5, 7, 8, 10};
  if (mode == Mode::Major || chord_pcs == 0)
    return spelled;
  const auto is_chord_offset = [&](int offset) {
    return (chord_pcs & pcBit(tonic_pc + offset)) != 0;
  };
  // Every pitch class outside the natural minor sits a semitone above a member,
  // so a chromatic chord tone always reads as that member's letter raised.
  for (int offset = 1; offset < 12; ++offset) {
    if (!is_chord_offset(offset))
      continue;
    bool member = false;
    for (int letter : spelled)
      member = member || letter == offset;
    for (int& letter : spelled) {
      if (!member && letter == offset - 1)
        letter = offset;
    }
  }
  // Top-down, so a raise that closes one gap is seen by the pair below it.
  for (int letter = 6; letter >= 0; --letter) {
    const int upper = letter == 6 ? spelled[0] + 12 : spelled[static_cast<std::size_t>(letter + 1)];
    int& lower = spelled[static_cast<std::size_t>(letter)];
    if (upper - lower == 3 && !is_chord_offset(lower) && letter != 0)
      ++lower;
  }
  return spelled;
}

/**
 * @brief Test whether a pitch belongs to a key as spelled under a harmony.
 * @param midi MIDI pitch (or pitch class).
 * @param tonic_pc Tonic pitch class of the local key.
 * @param mode Mode of the local key.
 * @param chord_pcs Tones of the harmony at the tick (0 = no harmony known).
 * @return True when the pitch is one of the seven spelled degrees.
 */
constexpr bool inSpelledScale(int midi, int tonic_pc, Mode mode, PcMask chord_pcs) {
  const int offset = (((midi - tonic_pc) % 12) + 12) % 12;
  for (int letter : spellScale(tonic_pc, mode, chord_pcs)) {
    if (letter == offset)
      return true;
  }
  return false;
}

/**
 * @brief Tones of a ChordSpec as a key holds them.
 *
 * ChordSpec has no diminished quality, so a minor key's ii and vii arrive as
 * minor triads with a perfect fifth the key does not have; that fifth is read
 * as the diminished one. Every other chord is its own tones (chordPcMask).
 *
 * @param chord Chord to read.
 * @param tonic_pc Tonic pitch class of the local key.
 * @param mode Mode of the local key.
 * @return The chord's tones as a pitch-class set.
 */
constexpr PcMask chordPcMaskInKey(const ChordSpec& chord, int tonic_pc, Mode mode) {
  PcMask mask = chordPcMask(chord);
  const int fifth = chord.root_pc + 7;
  if (mode == Mode::Minor && chord.minor && !inSpelledScale(fifth, tonic_pc, mode, 0)) {
    mask = static_cast<PcMask>((mask & ~pcBit(fifth)) | pcBit(fifth - 1));
  }
  return mask;
}

/**
 * @brief Letter a pitch is written on in a key, counted upward across octaves.
 *
 * A pitch outside the plain scale reads as the letter below it raised, so in
 * minor B and B-flat share a letter, and so do A and A-flat.
 *
 * @param midi MIDI pitch.
 * @param tonic_pc Tonic pitch class of the local key.
 * @param mode Mode of the local key.
 * @return 7 * octave + letter, letter 0 being the tonic.
 */
constexpr int diatonicLetter(int midi, int tonic_pc, Mode mode) {
  const int relative = midi - (((tonic_pc % 12) + 12) % 12);
  const int octave = relative >= 0 ? relative / 12 : -((11 - relative) / 12);
  const int offset = relative - 12 * octave;
  const ScaleSpelling plain = spellScale(tonic_pc, mode, 0);
  int letter = 6;
  while (letter > 0 && plain[static_cast<std::size_t>(letter)] > offset)
    --letter;
  return 7 * octave + letter;
}

/**
 * @brief Move a minor-key pitch onto the inflection its letter takes under a
 *        harmony; major pitches and letters spelled the same are returned as is.
 * @param midi MIDI pitch.
 * @param tonic_pc Tonic pitch class of the local key.
 * @param mode Mode of the local key.
 * @param chord_pcs Tones of the harmony at the pitch (0 = none known).
 * @return The pitch on the same letter, spelled by spellScale.
 */
constexpr int respellInScale(int midi, int tonic_pc, Mode mode, PcMask chord_pcs) {
  if (mode == Mode::Major)
    return midi;
  const int relative = midi - (((tonic_pc % 12) + 12) % 12);
  const int octave = relative >= 0 ? relative / 12 : -((11 - relative) / 12);
  const int offset = relative - 12 * octave;
  const int letter = diatonicLetter(midi, tonic_pc, mode) - 7 * octave;
  const int spelled = spellScale(tonic_pc, mode, chord_pcs)[static_cast<std::size_t>(letter)];
  return spelled - offset == 1 || offset - spelled == 1 ? midi - offset + spelled : midi;
}

/**
 * @brief Add a sounding tone to a harmony, displacing the chord's other
 *        inflection of the same letter: what sounds outranks what is declared.
 * @param chord_pcs Tones of the declared harmony.
 * @param sounding MIDI pitch actually sounding (negative = none).
 * @param tonic_pc Tonic pitch class of the local key.
 * @param mode Mode of the local key.
 * @return The harmony to spell under (spellScale).
 */
constexpr PcMask withSoundingTone(PcMask chord_pcs, int sounding, int tonic_pc, Mode mode) {
  if (sounding < 0)
    return chord_pcs;
  const int sounding_pc = sounding % 12;
  const int letter = diatonicLetter(sounding_pc + 60, tonic_pc, mode) % 7;
  for (int pc = 0; pc < 12; ++pc) {
    if (pc != sounding_pc && diatonicLetter(pc + 60, tonic_pc, mode) % 7 == letter)
      chord_pcs = static_cast<PcMask>(chord_pcs & ~pcBit(pc));
  }
  return static_cast<PcMask>(chord_pcs | pcBit(sounding_pc));
}

/// @brief True when two pitches a minor third apart sit on adjacent letters.
constexpr bool isAugmentedSecond(int from, int to, int tonic_pc, Mode mode) {
  const int letters = diatonicLetter(to, tonic_pc, mode) - diatonicLetter(from, tonic_pc, mode);
  return (to - from == 3 && letters == 1) || (from - to == 3 && letters == -1);
}

/// @brief isAugmentedSecond with `to` folded to the octave nearest `from`, so
///        the diminished seventh that inverts the interval is caught too.
constexpr bool isAugmentedSecondClass(int from, int to, int tonic_pc, Mode mode) {
  int folded = to;
  while (folded - from > 6)
    folded -= 12;
  while (from - folded > 6)
    folded += 12;
  return isAugmentedSecond(from, folded, tonic_pc, mode);
}

/**
 * @brief Step one letter up or down in a key as spelled under a harmony.
 *
 * A pitch the spelling (or the plain scale) names is moved to the next letter,
 * so the step never returns the same letter inflected: B steps down to A over
 * the minor dominant, never to B-flat. A minor pitch neither names is read as
 * the raised letter below it and spelled around like a chord tone. In major such
 * a pitch has no single reading (A-sharp or B-flat), so it moves to the nearest
 * degree in the direction asked.
 *
 * @param midi Starting MIDI pitch.
 * @param direction Negative to step down, otherwise up.
 * @param tonic_pc Tonic pitch class of the local key.
 * @param mode Mode of the local key.
 * @param chord_pcs Tones of the harmony at the tick (0 = no harmony known).
 * @return The MIDI pitch one degree away.
 */
inline int spelledScaleStep(int midi, int direction, int tonic_pc, Mode mode, PcMask chord_pcs) {
  const int sign = direction < 0 ? -1 : 1;
  PcMask context = chord_pcs;
  if (!inSpelledScale(midi, tonic_pc, mode, chord_pcs) &&
      !inSpelledScale(midi, tonic_pc, mode, 0)) {
    if (mode == Mode::Major) {
      for (int distance = 1; distance <= 12; ++distance) {
        if (inSpelledScale(midi + sign * distance, tonic_pc, mode, chord_pcs))
          return midi + sign * distance;
      }
      return midi;
    }
    context = static_cast<PcMask>(context | pcBit(midi));
  }
  const ScaleSpelling spelled = spellScale(tonic_pc, mode, context);
  const ScaleSpelling plain = spellScale(tonic_pc, mode, 0);
  const int relative = midi - (((tonic_pc % 12) + 12) % 12);
  const int octave = relative >= 0 ? relative / 12 : -((11 - relative) / 12);
  const int offset = relative - 12 * octave;
  int letter = -1;
  for (int index = 0; index < 7 && letter < 0; ++index) {
    if (spelled[static_cast<std::size_t>(index)] == offset)
      letter = index;
  }
  for (int index = 0; index < 7 && letter < 0; ++index) {
    if (plain[static_cast<std::size_t>(index)] == offset)
      letter = index;
  }
  if (letter < 0)
    return midi;
  int target = letter + sign;
  int target_octave = octave;
  if (target > 6) {
    target = 0;
    ++target_octave;
  } else if (target < 0) {
    target = 6;
    --target_octave;
  }
  return midi - offset - 12 * (octave - target_octave) + spelled[static_cast<std::size_t>(target)];
}

// Walk one scale degree of the home key under the bar's chord (spellScale).
inline int melodicScaleStep(int midi, int direction, Mode mode, const ChordSpec* chord = nullptr) {
  return spelledScaleStep(midi, direction, 0, mode,
                          chord != nullptr ? chordPcMaskInKey(*chord, 0, mode) : 0);
}

/**
 * @brief Walk `steps` diatonic scale degrees upward from `midi`.
 * @param midi Starting MIDI pitch.
 * @param steps Number of scale degrees to ascend.
 * @param mode Diatonic mode selecting the scale (Major = C major, Minor = C
 *             minor as spellScale spells it under `chord`).
 * @param chord Harmony the walk sounds over (nullptr = none known).
 * @return The MIDI pitch `steps` scale degrees above `midi`.
 */
inline int scaleUp(int midi, int steps, Mode mode, const ChordSpec* chord = nullptr) {
  int cur = midi;
  for (int step = 0; step < steps; ++step)
    cur = melodicScaleStep(cur, 1, mode, chord);
  return cur;
}

/**
 * @brief Test diatonic scale membership of a pitch class.
 * @param midi MIDI pitch (or pitch class) to test.
 * @param mode Diatonic mode selecting the scale (Major = C major, Minor = C
 *             minor as spellScale spells it under `chord`).
 * @param chord Harmony the pitch sounds over (nullptr = none known).
 * @return True if the pitch class belongs to the selected scale.
 */
inline bool inScale(int midi, Mode mode, const ChordSpec* chord = nullptr) {
  return inSpelledScale(midi, 0, mode, chord != nullptr ? chordPcMaskInKey(*chord, 0, mode) : 0);
}

/**
 * @brief Walk `steps` diatonic scale degrees downward from `midi`.
 *
 * Note this cannot be expressed as `-scaleUp(-midi, steps, mode)`: pitch-class
 * negation maps p to (12 - p) % 12, and the diatonic sets are not symmetric
 * under that inversion, so the negation trick walks a different (non-diatonic)
 * scale and lands on chromatic pitches.
 *
 * @param midi Starting MIDI pitch.
 * @param steps Number of scale degrees to descend.
 * @param mode Diatonic mode selecting the scale (Major = C major, Minor = C
 *             minor as spellScale spells it under `chord`).
 * @param chord Harmony the walk sounds over (nullptr = none known).
 * @return The MIDI pitch `steps` scale degrees below `midi`.
 */
inline int scaleDown(int midi, int steps, Mode mode, const ChordSpec* chord = nullptr) {
  int cur = midi;
  for (int step = 0; step < steps; ++step)
    cur = melodicScaleStep(cur, -1, mode, chord);
  return cur;
}

}  // namespace bach::composer::detail

#endif  // BACH_COMPOSER_FIGURATION_H
