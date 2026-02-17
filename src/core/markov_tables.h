// Markov transition tables for Bach melodic modeling.

#ifndef BACH_CORE_MARKOV_TABLES_H
#define BACH_CORE_MARKOV_TABLES_H

#include <cstdint>

#include "core/basic_types.h"

namespace bach {

// --- Constants ---

using DegreeStep = int8_t;           // [-8, +8] + LargeLeapUp(+9) / LargeLeapDown(-9)
constexpr int kDegreeStepCount = 19;  // -9..+9
constexpr int kDegreeOffset = 9;      // Index = step + 9

// --- Enums ---

/// @brief Metric position for Markov state (finer than MetricLevel).
enum class BeatPos : uint8_t {
  Bar = 0,   ///< Bar start (beat 0).
  Beat = 1,  ///< Main beat (beats 1, 2, 3 in 4/4).
  Off8 = 2,  ///< 8th-note offbeat.
  Off16 = 3  ///< 16th-note offbeat.
};
constexpr int kBeatPosCount = 4;

/// @brief Scale degree class for harmonic context.
enum class DegreeClass : uint8_t {
  Stable = 0,    ///< Degrees 0, 2 (tonic, mediant -- harmonic rest).
  Dominant = 1,  ///< Degrees 4, 6 (dominant, leading tone -- tension).
  Motion = 2     ///< Degrees 1, 3, 5 (supertonic, subdominant, submediant).
};
constexpr int kDegreeClassCount = 3;

/// @brief Duration category for rhythm transitions.
enum class DurCategory : uint8_t {
  S16 = 0,       ///< Sixteenth note (~120 ticks, < 180 ticks).
  S8 = 1,        ///< Eighth note (~240 ticks, 180-299 ticks).
  Dot8 = 2,      ///< Dotted eighth (~360 ticks, 300-479 ticks).
  Qtr = 3,       ///< Quarter note (~480 ticks, 480-959 ticks).
  HalfPlus = 4   ///< Half note or longer (>= 960 ticks).
};
constexpr int kDurCatCount = 5;

/// @brief Directed interval class for rhythm conditioning.
enum class DirIntervalClass : uint8_t {
  StepUp = 0,    ///< Degree step +1 or +2.
  StepDown = 1,  ///< Degree step -1 or -2.
  SkipUp = 2,    ///< Degree step +3 or +4.
  SkipDown = 3,  ///< Degree step -3 or -4.
  LeapUp = 4,    ///< Degree step >= +5.
  LeapDown = 5   ///< Degree step <= -5.
};
constexpr int kDirIvlClassCount = 6;

// --- Vertical interval oracle ---

constexpr int kBassDegreeCount = 7;
constexpr int kVoiceBinCount = 3;
constexpr int kHarmFuncCount = 3;
constexpr int kPcOffsetCount = 12;
constexpr int kVerticalRows = kBassDegreeCount * kBeatPosCount
                            * kVoiceBinCount * kHarmFuncCount;  // 252

/// @brief Harmonic function classification.
enum class HarmFunc : uint8_t {
  Tonic = 0,        ///< I, vi, iii (degrees 0, 5, 2).
  Subdominant = 1,  ///< IV, ii (degrees 3, 1).
  Dominant = 2      ///< V, vii (degrees 4, 6).
};

/// @brief Vertical interval probability table.
/// Row: bass_degree(7) x beat_pos(4) x voice_bin(3) x harm_func(3) = 252 rows.
/// Col: pitch_class_offset(12) from bass, mod 12.
/// Values: probability x 10000 (uint16_t).
struct VerticalIntervalTable {
  uint16_t prob[kVerticalRows][kPcOffsetCount];
};

/// @brief Oracle candidate for two-oracle intersection.
struct OracleCandidate {
  uint8_t pitch;  ///< MIDI pitch (horizontal) or pitch class 0-11 (vertical).
  float prob;     ///< Normalized probability (0-1).
};

// --- Transition tables ---

/// @brief Pitch transition probability table.
/// Row: prev_step(19) x degree_class(3) x beat_pos(4) = 228 rows.
/// Col: next_step(19).
/// Values: probability x 10000 (uint16_t).
struct PitchTransitionTable {
  uint16_t prob[kDegreeStepCount * kDegreeClassCount * kBeatPosCount]
               [kDegreeStepCount];
};

/// @brief Duration transition probability table.
/// Row: prev_dur(5) x dir_ivl_class(6) = 30 rows.
/// Col: next_dur(5).
struct DurTransitionTable {
  uint16_t prob[kDurCatCount * kDirIvlClassCount][kDurCatCount];
};

/// @brief A complete Markov model for a voice category.
struct MarkovModel {
  const char* name;
  const PitchTransitionTable& pitch;
  const DurTransitionTable& duration;
};

// --- Model instances (defined in markov_tables.cpp) ---

extern const MarkovModel kFugueUpperMarkov;
extern const MarkovModel kFuguePedalMarkov;
extern const MarkovModel kCelloMarkov;
extern const MarkovModel kViolinMarkov;
extern const MarkovModel kToccataUpperMarkov;
extern const MarkovModel kToccataPedalMarkov;
extern const VerticalIntervalTable kFugueVerticalTable;

// --- Utility functions ---

/// @brief Convert degree step to array index.
/// @param step Degree step in [-9, +9].
/// @return Clamped index in [0, kDegreeStepCount - 1].
inline constexpr int degreeStepToIndex(DegreeStep step) {
  int idx = static_cast<int>(step) + kDegreeOffset;
  if (idx < 0) return 0;
  if (idx >= kDegreeStepCount) return kDegreeStepCount - 1;
  return idx;
}

/// @brief Convert tick position to 4-level beat position.
/// Assumes 4/4 meter with kTicksPerBeat = 480.
/// @param tick Absolute tick position.
/// @return BeatPos classification.
inline BeatPos tickToBeatPos(Tick tick) {
  Tick in_bar = tick % kTicksPerBar;
  if (in_bar == 0) return BeatPos::Bar;
  if (in_bar % kTicksPerBeat == 0) return BeatPos::Beat;
  if (in_bar % (kTicksPerBeat / 2) == 0) return BeatPos::Off8;
  return BeatPos::Off16;
}

/// @brief Convert duration in ticks to category.
/// @param dur Duration in ticks.
/// @return DurCategory classification.
inline DurCategory ticksToDurCategory(Tick dur) {
  if (dur < kTicksPerBeat * 3 / 8) return DurCategory::S16;       // < 180 ticks
  if (dur < kTicksPerBeat * 5 / 8) return DurCategory::S8;        // < 300 ticks
  if (dur < kTicksPerBeat) return DurCategory::Dot8;               // < 480 ticks
  if (dur < kTicksPerBeat * 2) return DurCategory::Qtr;            // < 960 ticks
  return DurCategory::HalfPlus;
}

/// @brief Convert scale degree (0-6) to degree class.
/// @param deg Scale degree (may be outside 0-6; normalized via modulo).
/// @return DegreeClass classification.
inline DegreeClass scaleDegreeToClass(int deg) {
  deg = ((deg % 7) + 7) % 7;  // Normalize to 0-6.
  if (deg == 0 || deg == 2) return DegreeClass::Stable;
  if (deg == 4 || deg == 6) return DegreeClass::Dominant;
  return DegreeClass::Motion;
}

/// @brief Convert voice count to voice bin index.
/// @return 0 for 2 voices, 1 for 3, 2 for 4+.
inline constexpr int voiceCountToBin(int n) {
  return n <= 2 ? 0 : n == 3 ? 1 : 2;
}

/// @brief Compute row index into the vertical interval table.
inline constexpr int verticalRowIndex(int bass_deg, BeatPos bp,
                                       int vbin, HarmFunc hf) {
  return bass_deg * kBeatPosCount * kVoiceBinCount * kHarmFuncCount
       + static_cast<int>(bp) * kVoiceBinCount * kHarmFuncCount
       + vbin * kHarmFuncCount
       + static_cast<int>(hf);
}

/// @brief Classify scale degree to harmonic function.
/// @param degree Scale degree (may be outside 0-6; normalized via modulo).
/// @return HarmFunc classification.
inline HarmFunc degreeToHarmFunc(int degree) {
  degree = ((degree % 7) + 7) % 7;
  if (degree == 0 || degree == 5 || degree == 2) return HarmFunc::Tonic;
  if (degree == 3 || degree == 1) return HarmFunc::Subdominant;
  return HarmFunc::Dominant;
}

/// @brief Convert signed degree step to directed interval class.
/// @param step Signed degree step (0 treated as StepUp/unison).
/// @return DirIntervalClass classification.
inline DirIntervalClass toDirIvlClass(DegreeStep step) {
  int stp = static_cast<int>(step);
  if (stp >= 1 && stp <= 2) return DirIntervalClass::StepUp;
  if (stp >= -2 && stp <= -1) return DirIntervalClass::StepDown;
  if (stp >= 3 && stp <= 4) return DirIntervalClass::SkipUp;
  if (stp >= -4 && stp <= -3) return DirIntervalClass::SkipDown;
  if (stp >= 5) return DirIntervalClass::LeapUp;
  if (stp <= -5) return DirIntervalClass::LeapDown;
  // step == 0: treat as StepUp (unison, rare).
  return DirIntervalClass::StepUp;
}

/// @brief Compute degree step between two pitches in a given key/scale context.
/// Uses pitchToAbsoluteDegree from scale.h. Large leaps are clamped to +/-9.
/// @param from_pitch Starting MIDI pitch.
/// @param to_pitch Ending MIDI pitch.
/// @param key Musical key (tonic pitch class).
/// @param scale Scale type.
/// @return Signed degree step in [-9, +9].
DegreeStep computeDegreeStep(uint8_t from_pitch, uint8_t to_pitch,
                              Key key, ScaleType scale);

// --- Scoring constants ---

/// @brief Weight constants for Markov scoring.
constexpr float kMarkovPitchWeight = 0.45f;         ///< Organ system pitch weight.
constexpr float kMarkovPitchWeightSolo = 0.45f;      ///< Solo string system pitch weight.
constexpr float kMarkovDurWeight = 0.20f;             ///< Duration weight (both systems).
constexpr float kMarkovCadenceAttenuation = 0.5f;    ///< Cadence proximity weight reduction.
constexpr float kMarkovPhraseStartBoost = 1.2f;      ///< Phrase start weight increase.
constexpr float kVerticalAlpha = 0.65f;      ///< Vertical oracle weight in combined score.
constexpr float kVerticalMinGate = 0.05f;     ///< Minimum vertical probability to pass gate.
constexpr float kVerticalMinGateCadence = 0.10f;  ///< Stricter gate for cadence zone.

// --- Scoring functions ---

/// @brief Score a pitch transition using the Markov model.
///
/// Returns log(p / p_uniform) soft-clipped to approximately [-0.46, +0.46]
/// using tanhf. Positive values indicate transitions more common than uniform,
/// negative values indicate rarer transitions.
///
/// @param model The Markov model to use.
/// @param prev_step Previous degree step (from prev-prev to prev note).
/// @param deg_class Degree class of the previous note.
/// @param beat Beat position of the next note.
/// @param next_step Degree step from prev note to candidate note.
/// @return Score in approximately [-0.46, +0.46].
float scoreMarkovPitch(const MarkovModel& model,
                       DegreeStep prev_step, DegreeClass deg_class,
                       BeatPos beat, DegreeStep next_step);

/// @brief Score a duration transition using the Markov model.
///
/// Returns log(p / p_uniform) soft-clipped to approximately [-0.46, +0.46].
///
/// @param model The Markov model to use.
/// @param prev_dur Duration category of the previous note.
/// @param dir_class Directed interval class of the current interval.
/// @param next_dur Duration category of the candidate note.
/// @return Score in approximately [-0.46, +0.46].
float scoreMarkovDuration(const MarkovModel& model,
                          DurCategory prev_dur, DirIntervalClass dir_class,
                          DurCategory next_dur);

/// @brief Score a vertical interval using the vertical table.
///
/// Returns log(p / p_uniform) soft-clipped to approximately [-0.46, +0.46]
/// using tanhf. Same formula as scoreMarkovPitch.
///
/// @param table The vertical interval table to use.
/// @param bass_degree Bass scale degree (0-6).
/// @param beat Beat position of the note.
/// @param voice_bin Voice count bin (0=2v, 1=3v, 2=4+v).
/// @param hf Harmonic function classification.
/// @param pc_offset Pitch class offset from bass (0-11).
/// @return Score in approximately [-0.46, +0.46].
float scoreVerticalInterval(const VerticalIntervalTable& table,
                            int bass_degree, BeatPos beat,
                            int voice_bin, HarmFunc hf, int pc_offset);

/// @brief Get top-N melodic (horizontal) oracle candidates.
///
/// Reads the pitch transition table for the given context, converts degree steps
/// to absolute MIDI pitches, filters by voice range, and returns sorted by
/// probability descending.
///
/// @param model Markov model to use.
/// @param prev_step Previous degree step.
/// @param deg_class Degree class of the previous note.
/// @param beat Beat position.
/// @param from_pitch Previous MIDI pitch.
/// @param key Current key.
/// @param scale Current scale type.
/// @param range_lo Voice range lower bound (MIDI).
/// @param range_hi Voice range upper bound (MIDI).
/// @param out Output array for candidates.
/// @param max_count Maximum candidates to return.
/// @return Number of candidates written to out.
int getTopMelodicCandidates(
    const MarkovModel& model,
    DegreeStep prev_step, DegreeClass deg_class, BeatPos beat,
    uint8_t from_pitch, Key key, ScaleType scale,
    uint8_t range_lo, uint8_t range_hi,
    OracleCandidate* out, int max_count);

/// @brief Get top-N vertical oracle candidates.
///
/// Reads the vertical interval table for the given context and returns
/// pitch classes (0-11) sorted by probability descending.
///
/// @param table Vertical interval table.
/// @param bass_degree Bass scale degree (0-6).
/// @param beat Beat position.
/// @param voice_bin Voice count bin.
/// @param hf Harmonic function.
/// @param out Output array for candidates.
/// @param max_count Maximum candidates to return.
/// @return Number of candidates written to out.
int getTopVerticalCandidates(
    const VerticalIntervalTable& table,
    int bass_degree, BeatPos beat, int voice_bin, HarmFunc hf,
    OracleCandidate* out, int max_count);

}  // namespace bach

#endif  // BACH_CORE_MARKOV_TABLES_H
