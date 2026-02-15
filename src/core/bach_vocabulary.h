// Bach Pattern Vocabulary: melodic figures, rhythm cells, contour templates,
// and voice profiles extracted from Bach reference data (270 works).

#ifndef BACH_CORE_BACH_VOCABULARY_H
#define BACH_CORE_BACH_VOCABULARY_H

#include <cstdint>

namespace bach {

// ---------------------------------------------------------------------------
// Interval representation
// ---------------------------------------------------------------------------

/// @brief Interval encoding mode for melodic figures.
///
/// Semitone mode encodes exact chromatic intervals (useful for ornamental
/// figures like mordents where pitch identity matters). Degree mode encodes
/// scale-relative intervals as (degree_diff, chroma_offset) pairs, enabling
/// transposition to any key while preserving diatonic relationships.
enum class IntervalMode : uint8_t {
  Semitone,  ///< Semitone count. For ornamental figures.
  Degree,    ///< (degree_diff, chroma_offset) pair. For scale patterns.
};

/// @brief Scale-degree-relative interval.
///
/// Encodes an interval as a signed degree difference plus chromatic offset.
/// degree_diff is the real directed interval including octaves
/// (e.g., C4->D5 = +8, E4->C4 = -2).
/// chroma_offset adjusts for accidentals relative to the diatonic scale:
/// -1 = flat, 0 = natural, +1 = sharp.
struct DegreeInterval {
  int8_t degree_diff;     ///< Signed scale degree difference (includes octaves).
  int8_t chroma_offset;   ///< -1=flat, 0=natural, +1=sharp vs diatonic.
};

// ---------------------------------------------------------------------------
// Melodic figures
// ---------------------------------------------------------------------------

/// @brief A named melodic figure extracted from Bach reference works.
///
/// Figures can be encoded in either Semitone or Degree mode (or both).
/// Ornamental figures (mordent, trill prefix) use Semitone mode and are
/// not transposable. Scale-derived patterns use Degree mode and are
/// freely transposable across keys.
///
/// Arrays are NOT owned by this struct; they must point to static constexpr
/// data with lifetime >= the figure itself.
///
/// @note rhythm_ratios and onset_ratios are relative to the figure's total
///       duration. rhythm_ratios[i] is the duration proportion of note i.
///       onset_ratios[i] is the onset position of note i (0.0 = start).
struct MelodicFigure {
  const char* name;               ///< Human-readable identifier (e.g., "mordent").
  IntervalMode primary_mode;      ///< Which interval array is authoritative.
  bool allow_transposition;       ///< false for ornaments, true for scale runs.
  const int8_t* semitone_intervals;        ///< Nullable. Length = note_count - 1.
  const DegreeInterval* degree_intervals;  ///< Nullable. Length = note_count - 1.
  const float* rhythm_ratios;     ///< Duration proportions. Length = note_count.
  const float* onset_ratios;      ///< Onset positions (0.0-1.0). Length = note_count.
  uint8_t note_count;             ///< Number of notes in the figure.
  const char* provenance;         ///< Source references (e.g., "BWV578:v1:b3").
};

// ---------------------------------------------------------------------------
// Rhythm cells
// ---------------------------------------------------------------------------

/// @brief A named rhythmic cell extracted from Bach reference works.
///
/// Rhythm cells capture beat-level patterns independently of pitch content.
/// They can be combined with MelodicFigure or ContourTemplate to build
/// complete musical gestures.
///
/// @note beat_ratios[i] is the duration of note i as a fraction of total_beats.
///       onset_in_beat[i] is the onset position within its beat (0.0-1.0).
struct RhythmCell {
  const char* name;             ///< Human-readable identifier (e.g., "lombard").
  const float* beat_ratios;     ///< Duration of each note in beats. Length = note_count.
  const float* onset_in_beat;   ///< Onset within beat (0.0-1.0). Length = note_count.
  uint8_t note_count;           ///< Number of notes in the cell.
  float total_beats;            ///< Total duration in beats.
  const char* provenance;       ///< Source references.
};

// ---------------------------------------------------------------------------
// Contour templates
// ---------------------------------------------------------------------------

/// @brief Directional step in a contour template.
enum class ContourStep : int8_t {
  Down = -1,  ///< Pitch descends.
  Same = 0,   ///< Pitch repeats.
  Up = 1      ///< Pitch ascends.
};

/// @brief A named pitch contour shape abstracted from specific intervals.
///
/// Contour templates capture the directional "shape" of a melodic line
/// without specifying exact intervals. They are combined with interval
/// profiles to generate concrete pitch sequences.
struct ContourTemplate {
  const char* name;              ///< Human-readable identifier (e.g., "arch").
  const ContourStep* steps;      ///< Directional steps. Length = note_count - 1.
  uint8_t note_count;            ///< Number of notes in the contour.
};

// ---------------------------------------------------------------------------
// Voice interval profiles
// ---------------------------------------------------------------------------

/// @brief Target interval distribution for a voice type.
///
/// Calibrated from Bach reference data. Used for JSD monitoring and weak
/// scoring bias during generation. Values represent statistical targets,
/// not hard constraints.
///
/// @note stepwise_ratio + leap_ratio may not sum to 1.0; the remainder is
///       unison (repeated notes).
struct VoiceIntervalProfile {
  const char* name;            ///< Profile identifier (e.g., "organ_soprano").
  float stepwise_ratio;        ///< Target stepwise motion ratio (intervals 1-2 semitones).
  float leap_ratio;            ///< Target leap ratio (intervals >= 3 semitones).
  float avg_interval;          ///< Average interval size in semitones.
  uint8_t max_leap;            ///< Recommended maximum leap in semitones.
  const char* provenance;      ///< Source references.
};

// ---------------------------------------------------------------------------
// Bass harmonic constraints
// ---------------------------------------------------------------------------

/// @brief Harmonic anchoring constraints for bass voices.
///
/// Applied BEFORE VoiceIntervalProfile in the generation pipeline.
/// Bass voices have fundamentally different melodic behavior from upper voices:
/// they emphasize harmonic function over melodic independence.
///
/// @note Derived from BWV 578 pedal analysis: 65% root agreement,
///       85% chord tone, with larger average intervals (3.9 semitones).
struct BassHarmonicConstraint {
  float harmonic_anchor_ratio;   ///< Root agreement target (~0.65 for BWV578 pedal).
  float chord_tone_ratio;        ///< Chord tone target (~0.85 for BWV578 pedal).
  uint8_t max_non_chord_beats;   ///< Max consecutive non-chord-tone beats.
  const char* provenance;        ///< Source references.
};

// ---------------------------------------------------------------------------
// Shared arrays for equal-duration figures
// ---------------------------------------------------------------------------

namespace detail {
inline constexpr float kEqR3[] = {1.0f, 1.0f, 1.0f};
inline constexpr float kEqO3[] = {0.0f, 1.0f, 2.0f};
inline constexpr float kEqR4[] = {1.0f, 1.0f, 1.0f, 1.0f};
inline constexpr float kEqO4[] = {0.0f, 1.0f, 2.0f, 3.0f};
inline constexpr float kEqR5[] = {1.0f, 1.0f, 1.0f, 1.0f, 1.0f};
inline constexpr float kEqO5[] = {0.0f, 1.0f, 2.0f, 3.0f, 4.0f};
}  // namespace detail

// ---------------------------------------------------------------------------
// Common melodic figures — degree mode (transposable)
// Appear in 3+ categories across 270 Bach reference works.
// Frequency in per-mille: organ_fugue / wtc1 / solo_cello / goldberg
// ---------------------------------------------------------------------------

// 1. Descending scale run (freq: 35 / 39 / 51 / 52 ‰)
inline constexpr DegreeInterval kDescRun4_dg[] = {{-1, 0}, {-1, 0}, {-1, 0}};
inline constexpr MelodicFigure kDescRun4 = {
    "desc_run_4", IntervalMode::Degree, true,
    nullptr, kDescRun4_dg, detail::kEqR4, detail::kEqO4,
    4, "BWV574:upper:b33, BWV846f:upper:b3, BWV1007_1:solo:b8"};

// 2. Ascending scale run (freq: 15 / 22 / 34 / 29 ‰)
inline constexpr DegreeInterval kAscRun4_dg[] = {{1, 0}, {1, 0}, {1, 0}};
inline constexpr MelodicFigure kAscRun4 = {
    "asc_run_4", IntervalMode::Degree, true,
    nullptr, kAscRun4_dg, detail::kEqR4, detail::kEqO4,
    4, "BWV574:upper:b69, BWV846f:upper:b3, BWV1007_1:solo:b19"};

// 3. Lower turn / cambiata (freq: 22 / 19 / 25 / 28 ‰)
inline constexpr DegreeInterval kCambiataDown_dg[] = {{-1, 0}, {-1, 0}, {1, 0}};
inline constexpr MelodicFigure kCambiataDown = {
    "cambiata_down", IntervalMode::Degree, true,
    nullptr, kCambiataDown_dg, detail::kEqR4, detail::kEqO4,
    4, "BWV574:upper:b2, BWV846f:upper:b5, BWV1007_1:solo:b20"};

// 4. Upper escape + descend (freq: 16 / 18 / 17 / 21 ‰)
inline constexpr DegreeInterval kTurnDown_dg[] = {{1, 0}, {-1, 0}, {-1, 0}};
inline constexpr MelodicFigure kTurnDown = {
    "turn_down", IntervalMode::Degree, true,
    nullptr, kTurnDown_dg, detail::kEqR4, detail::kEqO4,
    4, "BWV574:upper:b7, BWV846f:upper:b2, BWV1007_1:solo:b5"};

// 5. Lower neighbor oscillation (freq: 14 / 17 / 15 / 21 ‰)
inline constexpr DegreeInterval kLowerNbr_dg[] = {{-1, 0}, {1, 0}, {-1, 0}};
inline constexpr MelodicFigure kLowerNbr = {
    "lower_nbr", IntervalMode::Degree, true,
    nullptr, kLowerNbr_dg, detail::kEqR4, detail::kEqO4,
    4, "BWV574:upper:b13, BWV846f:upper:b9, BWV1007_1:solo:b5"};

// 6. Upper neighbor oscillation (freq: 13 / 16 / 15 / 19 ‰)
inline constexpr DegreeInterval kUpperNbr_dg[] = {{1, 0}, {-1, 0}, {1, 0}};
inline constexpr MelodicFigure kUpperNbr = {
    "upper_nbr", IntervalMode::Degree, true,
    nullptr, kUpperNbr_dg, detail::kEqR4, detail::kEqO4,
    4, "BWV574:upper:b42, BWV846f:upper:b3, BWV1007_1:solo:b5"};

// 7. Dip then ascend (freq: 11 / 13 / 15 / 18 ‰)
inline constexpr DegreeInterval kTurnUp_dg[] = {{-1, 0}, {1, 0}, {1, 0}};
inline constexpr MelodicFigure kTurnUp = {
    "turn_up", IntervalMode::Degree, true,
    nullptr, kTurnUp_dg, detail::kEqR4, detail::kEqO4,
    4, "BWV574:upper:b11, BWV846f:upper:b3, BWV1007_1:solo:b11"};

// 8. Ascend then turn back (freq: 10 / 15 / 13 / 16 ‰)
inline constexpr DegreeInterval kEscapeDown_dg[] = {{1, 0}, {1, 0}, {-1, 0}};
inline constexpr MelodicFigure kEscapeDown = {
    "escape_down", IntervalMode::Degree, true,
    nullptr, kEscapeDown_dg, detail::kEqR4, detail::kEqO4,
    4, "BWV574:upper:b11, BWV846f:upper:b2, BWV1007_2:solo:b33"};

// 9. Leap 3rd up then step down (freq: 9 / 7 / 7 / 8 ‰)
inline constexpr DegreeInterval kLeapUpStepDown_dg[] = {{2, 0}, {-1, 0}, {-1, 0}};
inline constexpr MelodicFigure kLeapUpStepDown = {
    "leap_up_step_down", IntervalMode::Degree, true,
    nullptr, kLeapUpStepDown_dg, detail::kEqR4, detail::kEqO4,
    4, "BWV574:upper:b2, BWV846f:lower:b5, BWV1007_1:solo:b6"};

// 10. Step down then leap 3rd up (freq: 8 / 8 / 6 / 7 ‰)
inline constexpr DegreeInterval kStepDownLeapUp_dg[] = {{-1, 0}, {-1, 0}, {2, 0}};
inline constexpr MelodicFigure kStepDownLeapUp = {
    "step_down_leap_up", IntervalMode::Degree, true,
    nullptr, kStepDownLeapUp_dg, detail::kEqR4, detail::kEqO4,
    4, "BWV574:upper:b14, BWV846f:upper:b4, BWV1007_1:solo:b5"};

// 11. Step up then leap 3rd down (freq: — / 8 / 11 / 7 ‰)
inline constexpr DegreeInterval kStepUpLeapDown_dg[] = {{1, 0}, {1, 0}, {-2, 0}};
inline constexpr MelodicFigure kStepUpLeapDown = {
    "step_up_leap_down", IntervalMode::Degree, true,
    nullptr, kStepUpLeapDown_dg, detail::kEqR4, detail::kEqO4,
    4, "BWV846f:upper:b21, BWV1007_1:solo:b9, BWV988_01:upper:b2"};

// 12. Échappée: neighbor + drop (freq: 8 / 6 / 11 / 8 ‰)
inline constexpr DegreeInterval kEchappee_dg[] = {{-1, 0}, {1, 0}, {-2, 0}};
inline constexpr MelodicFigure kEchappee = {
    "echappee", IntervalMode::Degree, true,
    nullptr, kEchappee_dg, detail::kEqR4, detail::kEqO4,
    4, "BWV574:upper:b14, BWV846f:upper:b5, BWV1007_1:solo:b3"};

// 13. Cambiata neighbor: step-leap-step (freq: 7 / 7 / 5 / 9 ‰)
inline constexpr DegreeInterval kCambiataNbr_dg[] = {{-1, 0}, {2, 0}, {-1, 0}};
inline constexpr MelodicFigure kCambiataNbr = {
    "cambiata_nbr", IntervalMode::Degree, true,
    nullptr, kCambiataNbr_dg, detail::kEqR4, detail::kEqO4,
    4, "BWV574:upper:b33, BWV846f:upper:b14, BWV1007_1:solo:b11"};

// 14. Leap recovery: step-leap3rd-step (freq: 7 / 5 / 10 / 7 ‰)
inline constexpr DegreeInterval kLeapRecovery_dg[] = {{1, 0}, {-2, 0}, {1, 0}};
inline constexpr MelodicFigure kLeapRecovery = {
    "leap_recovery", IntervalMode::Degree, true,
    nullptr, kLeapRecovery_dg, detail::kEqR4, detail::kEqO4,
    4, "BWV574:lower:b26, BWV846f:lower:b8, BWV1007_1:solo:b11"};

// 15. Chromatic descent — structural: lamento bass motif (freq: 6 / 5 / 5 / 6 ‰)
inline constexpr DegreeInterval kChromaticDesc_dg[] = {{-1, 0}, {-1, 0}, {-1, 1}};
inline constexpr MelodicFigure kChromaticDesc = {
    "chromatic_desc", IntervalMode::Degree, true,
    nullptr, kChromaticDesc_dg, detail::kEqR4, detail::kEqO4,
    4, "BWV574:upper:b35, BWV846f:upper:b16, BWV1007_3:solo:b10"};

// 16. Leap down 3rd then ascend (freq: — / 4 / 7 / — ‰; 39+24 works)
inline constexpr DegreeInterval kLeapDownAscend_dg[] = {{-2, 0}, {1, 0}, {1, 0}};
inline constexpr MelodicFigure kLeapDownAscend = {
    "leap_down_ascend", IntervalMode::Degree, true,
    nullptr, kLeapDownAscend_dg, detail::kEqR4, detail::kEqO4,
    4, "BWV846f:lower:b8, BWV1007_1:solo:b16, BWV988_00:lower:b9"};

// ---------------------------------------------------------------------------
// Common melodic figures — semitone mode (ornaments, non-transposable)
// ---------------------------------------------------------------------------

// 17. Lower mordent (organ: 337 occ, 8/8 works)
inline constexpr int8_t kMordent_st[] = {-1, 1};
inline constexpr MelodicFigure kMordent = {
    "mordent", IntervalMode::Semitone, false,
    kMordent_st, nullptr, detail::kEqR3, detail::kEqO3,
    3, "BWV574:upper:b3, BWV578:v1:b2"};

// 18. Inverted mordent (organ: 239 occ, 8/8 works)
inline constexpr int8_t kInvMordent_st[] = {1, -1};
inline constexpr MelodicFigure kInvMordent = {
    "inv_mordent", IntervalMode::Semitone, false,
    kInvMordent_st, nullptr, detail::kEqR3, detail::kEqO3,
    3, "BWV574:upper:b3, BWV578:v1:b3"};

// 19. Trill fragment (organ: 91 occ, 7/8 works)
inline constexpr int8_t kTrill3_st[] = {1, -1, 1};
inline constexpr MelodicFigure kTrill3 = {
    "trill_3", IntervalMode::Semitone, false,
    kTrill3_st, nullptr, detail::kEqR4, detail::kEqO4,
    4, "BWV574:upper:b3"};

// 20. Inverted trill fragment (organ: 91 occ, 8/8 works)
inline constexpr int8_t kInvTrill3_st[] = {-1, 1, -1};
inline constexpr MelodicFigure kInvTrill3 = {
    "inv_trill_3", IntervalMode::Semitone, false,
    kInvTrill3_st, nullptr, detail::kEqR4, detail::kEqO4,
    4, "BWV574:upper:b3"};

// ---------------------------------------------------------------------------
// Aggregate figure tables
// ---------------------------------------------------------------------------

inline constexpr const MelodicFigure* kCommonFigures[] = {
    &kDescRun4, &kAscRun4, &kCambiataDown, &kTurnDown,
    &kLowerNbr, &kUpperNbr, &kTurnUp, &kEscapeDown,
    &kLeapUpStepDown, &kStepDownLeapUp, &kStepUpLeapDown, &kEchappee,
    &kCambiataNbr, &kLeapRecovery, &kChromaticDesc, &kLeapDownAscend,
    &kMordent, &kInvMordent, &kTrill3, &kInvTrill3,
};
inline constexpr int kCommonFigureCount = 20;

// ---------------------------------------------------------------------------
// Rhythm cells — universal patterns across all categories
// ---------------------------------------------------------------------------

// Running sixteenths (freq: 239-333 ‰, top rhythm in all categories)
inline constexpr float kRunning16th_r[] = {0.25f, 0.25f, 0.25f, 0.25f};
inline constexpr float kRunning16th_o[] = {0.0f, 0.25f, 0.5f, 0.75f};
inline constexpr RhythmCell kRunning16th = {
    "running_16th", kRunning16th_r, kRunning16th_o, 4, 1.0f, "all categories"};

// Running eighths (freq: 142-188 ‰, 2nd most common)
inline constexpr float kRunning8th_r[] = {0.5f, 0.5f, 0.5f, 0.5f};
inline constexpr float kRunning8th_o[] = {0.0f, 0.5f, 1.0f, 1.5f};
inline constexpr RhythmCell kRunning8th = {
    "running_8th", kRunning8th_r, kRunning8th_o, 4, 2.0f, "all categories"};

// Running quarters (freq: 7-10 ‰, sustained motion)
inline constexpr float kRunningQtr_r[] = {1.0f, 1.0f, 1.0f, 1.0f};
inline constexpr float kRunningQtr_o[] = {0.0f, 1.0f, 2.0f, 3.0f};
inline constexpr RhythmCell kRunningQtr = {
    "running_qtr", kRunningQtr_r, kRunningQtr_o, 4, 4.0f, "all categories"};

// Lombardic: 16th-8th-16th-16th (freq: 18-21 ‰)
inline constexpr float kLombardic_r[] = {0.25f, 0.5f, 0.25f, 0.25f};
inline constexpr float kLombardic_o[] = {0.0f, 0.25f, 0.75f, 1.0f};
inline constexpr RhythmCell kLombardic = {
    "lombardic", kLombardic_r, kLombardic_o, 4, 1.25f, "all categories"};

// Dotted pair: 8th-16th-16th-8th (freq: 10-18 ‰)
inline constexpr float kDottedPair_r[] = {0.5f, 0.25f, 0.25f, 0.5f};
inline constexpr float kDottedPair_o[] = {0.0f, 0.5f, 0.75f, 1.0f};
inline constexpr RhythmCell kDottedPair = {
    "dotted_pair", kDottedPair_r, kDottedPair_o, 4, 1.5f, "all categories"};

// Short-long: 16th-16th-16th-8th (freq: 12-25 ‰)
inline constexpr float kShortLong_r[] = {0.25f, 0.25f, 0.25f, 0.5f};
inline constexpr float kShortLong_o[] = {0.0f, 0.25f, 0.5f, 0.75f};
inline constexpr RhythmCell kShortLong = {
    "short_long", kShortLong_r, kShortLong_o, 4, 1.25f, "all categories"};

// Long-short: 8th-16th-16th-16th (freq: 12-22 ‰)
inline constexpr float kLongShort_r[] = {0.5f, 0.25f, 0.25f, 0.25f};
inline constexpr float kLongShort_o[] = {0.0f, 0.5f, 0.75f, 1.0f};
inline constexpr RhythmCell kLongShort = {
    "long_short", kLongShort_r, kLongShort_o, 4, 1.25f, "all categories"};

// Quarter start: qtr-8th-8th-8th (freq: 7-15 ‰)
inline constexpr float kQtrStart_r[] = {1.0f, 0.5f, 0.5f, 0.5f};
inline constexpr float kQtrStart_o[] = {0.0f, 1.0f, 1.5f, 2.0f};
inline constexpr RhythmCell kQtrStart = {
    "qtr_start", kQtrStart_r, kQtrStart_o, 4, 2.5f, "all categories"};

inline constexpr const RhythmCell* kCommonRhythms[] = {
    &kRunning16th, &kRunning8th, &kRunningQtr, &kLombardic,
    &kDottedPair, &kShortLong, &kLongShort, &kQtrStart,
};
inline constexpr int kCommonRhythmCount = 8;

// ---------------------------------------------------------------------------
// Voice interval profiles (from CLAUDE.md Section 2b benchmarks)
// ---------------------------------------------------------------------------

inline constexpr VoiceIntervalProfile kOrganUpperProfile = {
    "organ_upper", 0.63f, 0.33f, 2.75f, 12, "BWV578:v1-v3 avg"};
inline constexpr VoiceIntervalProfile kOrganBassProfile = {
    "organ_bass", 0.44f, 0.52f, 3.9f, 19, "BWV578:pedal"};
inline constexpr VoiceIntervalProfile kCelloProfile = {
    "cello_suite", 0.55f, 0.38f, 2.9f, 12, "BWV1007-1012 avg"};
inline constexpr VoiceIntervalProfile kWTCProfile = {
    "wtc1", 0.49f, 0.47f, 3.4f, 12, "BWV846-869 avg"};
inline constexpr VoiceIntervalProfile kGoldbergProfile = {
    "goldberg", 0.53f, 0.43f, 3.2f, 12, "BWV988 avg"};

inline constexpr const VoiceIntervalProfile* kVoiceProfiles[] = {
    &kOrganUpperProfile, &kOrganBassProfile, &kCelloProfile,
    &kWTCProfile, &kGoldbergProfile,
};
inline constexpr int kVoiceProfileCount = 5;

// ---------------------------------------------------------------------------
// Bass harmonic constraints (from BWV578 pedal analysis)
// ---------------------------------------------------------------------------

inline constexpr BassHarmonicConstraint kOrganPedalConstraint = {
    0.65f, 0.85f, 2, "BWV578:pedal"};

}  // namespace bach

#endif  // BACH_CORE_BACH_VOCABULARY_H
