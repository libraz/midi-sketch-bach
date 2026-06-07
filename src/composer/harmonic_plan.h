#ifndef BACH_COMPOSER_HARMONIC_PLAN_H
#define BACH_COMPOSER_HARMONIC_PLAN_H

#include <cstdint>
#include <vector>

#include "core/basic_types.h"
#include "harmony/harmonic_timeline.h"

namespace bach::composer {

// Chord quality. P3-P6 used Major/Minor/Diminished only; P7 adds
// Augmented and the four 7th-chord qualities the Bach functional
// vocabulary needs (Major7, minor7, half-dim7=min7b5, dim7,
// dominant7).
enum class ChordQuality : std::uint8_t {
  Major = 0,
  Minor = 1,
  Diminished = 2,
  Augmented = 3,
  Major7 = 4,
  Minor7 = 5,
  HalfDiminished7 = 6,
  Diminished7 = 7,
  Dominant7 = 8,
};

// Diatonic scale degree (Roman numeral) of the chord root. Encodes
// case implicitly via separate values (we do not lowercase i/ii etc.
// because ChordQuality already carries the major/minor flavor).
enum class RomanNumeral : std::uint8_t {
  I = 0,
  II = 1,
  III = 2,
  IV = 3,
  V = 4,
  VI = 5,
  VII = 6,
};

// Bass position. P3-P6 produced root-position triads implicitly; P7
// surfaces inversion as first-class data so doubling/spacing rules
// and 6/4 type recognition can read it.
enum class ChordInversion : std::uint8_t {
  Root = 0,
  First = 1,   // 3rd in bass (6 chord).
  Second = 2,  // 5th in bass (6/4 chord).
  Third = 3,   // 7th in bass (4/2 chord).
};

// Functional-harmony category. Pred (Predominant) collects ii/IV/vi
// and secondary dominants; T = Tonic, S = Subdominant region, D =
// Dominant. P7 uses this for cadence labeling and 6/4 recognition.
enum class HarmonicFunction : std::uint8_t {
  T = 0,     // Tonic.
  S = 1,     // Subdominant.
  D = 2,     // Dominant.
  Pred = 3,  // Predominant (ii / IV / vi / secondary dominant).
};

// 6/4 chord taxonomy. A Second-inversion chord is one of three idioms
// in Bach's vocabulary; the type drives the doubling/voice-leading
// rules.
enum class SixFourType : std::uint8_t {
  Cadential = 0,    // I6/4 → V on the same dominant beat.
  Passing = 1,      // Bass passes by step (e.g. I → V6/4 → I6).
  Neighboring = 2,  // Upper voices neighbor against a stationary bass.
};

// Modulation taxonomy. P8 surfaces three Bach idioms for moving from
// one key area to another:
//
//   Pivot     — a single chord belongs to both keys (e.g. ii in C =
//               vi in F); the listener re-hears it in the new key
//               retrospectively. Pivot chords are diatonic in both
//               from_key and to_key.
//   Phrase    — the new key arrives at the start of a fresh phrase
//               (cadence + restart). No pivot is needed because the
//               break in continuity carries the modulation.
//   CommonTone — a single pitch class held across the boundary
//               provides the perceptual bridge. Used for distant
//               modulations.
enum class ModulationType : std::uint8_t {
  Pivot = 0,
  Phrase = 1,
  CommonTone = 2,
};

// Single source of truth for cadence taxonomy: harmony::CadenceType.
// Re-exported here so composer code keeps the bach::composer::CadenceType
// spelling without maintaining a parallel enum (see plan §12 H1).
using CadenceType = bach::CadenceType;

// Chord with a start tick. The chord runs until the next ChordEvent or
// the piece end. Root is expressed as a pitch class (0=C, 1=C#, ..., 11=B).
//
// P7 fields (degree, inversion, function) are optional in the sense
// that pre-P7 fixtures may leave them at their defaults; rules that
// require them (chord_tone_roman, doubling, spacing) skip silently
// when degree is unset for a particular event by checking
// `has_degree`. Callers that supply degree/inversion/function are
// expected to set `has_degree = true` so the rule engine knows the
// data is meaningful.
struct ChordEvent {
  Tick start_tick = 0;
  std::uint8_t root_pc = 0;
  ChordQuality quality = ChordQuality::Major;
  RomanNumeral degree = RomanNumeral::I;
  ChordInversion inversion = ChordInversion::Root;
  HarmonicFunction function = HarmonicFunction::T;
  bool has_degree = false;

  // P8 (Modulation / Tonicization / Borrowed chords). All P8 fields
  // default to "not in effect" so existing fixtures keep behaving
  // exactly as in P3-P7.
  //
  // secondary_of:    the secondary key this chord tonicizes. For V/V
  //                  in C major, root_pc=2 (D), quality=Major (or
  //                  Dominant7), degree=V (relative to the secondary
  //                  key), and secondary_of=V (the secondary key
  //                  itself, also V relative to the home key). Set
  //                  has_secondary_of=true to mark the chord as a
  //                  secondary dominant.
  // is_borrowed:     this chord is borrowed from the parallel mode
  //                  (modal mixture). Used for borrowed iv in major
  //                  keys and other parallel-mode loans.
  // is_picardy:      this chord is the final tonic of a minor-key
  //                  piece with the third raised to major (the
  //                  Picardy third). The Validator uses this to
  //                  verify the third is indeed major.
  RomanNumeral secondary_of = RomanNumeral::I;
  bool has_secondary_of = false;
  bool is_borrowed = false;
  bool is_picardy = false;
};

struct CadenceEvent {
  Tick tick = 0;
  CadenceType type = CadenceType::Perfect;
};

// One 6/4 chord occurrence in the plan. `type` is one of the three
// idioms (Cadential / Passing / Neighboring); `tick` is the strong
// beat the 6/4 sounds on; `resolution_tick` is where the dissonance
// resolves (e.g. into V for Cadential).
struct CadentialSixFour {
  Tick tick = 0;
  Tick resolution_tick = 0;
  SixFourType type = SixFourType::Cadential;
};

// One modulation in the plan. `tick` is the boundary where the new
// key takes effect (Pivot type: the pivot chord's tick; Phrase type:
// the first chord of the new phrase; CommonTone type: the chord
// where the common pc bridges over).
struct ModulationEvent {
  Tick tick = 0;
  std::uint8_t from_tonic_pc = 0;
  std::uint8_t to_tonic_pc = 0;
  bool from_is_minor = false;
  bool to_is_minor = false;
  ModulationType type = ModulationType::Pivot;
};

// Harmonic plan: piece-wide chord progression and tonic anchor.
//
// HarmonicPlan is produced before CandidateSearch and is immutable for the
// rest of the pipeline. Cadence locations and modulation joins both surface
// as ChordEvent boundaries.
struct HarmonicPlan {
  // Tonic pitch class of the piece. The composer engine runs C-internal
  // (C major internally); transposition (if any) happens at
  // MIDI output. tonic_pc therefore is usually 0.
  std::uint8_t tonic_pc = 0;
  bool is_minor = false;
  std::vector<ChordEvent> chords;
  std::vector<CadenceEvent> cadences;
  std::vector<CadentialSixFour> cadential_six_fours;
  // Piece-wide modulation boundaries. Empty when the piece stays
  // in the home key.
  std::vector<ModulationEvent> modulations;

  // Time signature carried alongside the harmony so every meter-sensitive
  // validator / candidate-search site can derive the bar length from the
  // plan rather than the global kTicksPerBar (which is fixed at 4/4 = 1920).
  // The form-director copies these from the per-form FormSpec; all phase
  // fixtures leave them at the 4/4 default, so existing outputs stay
  // byte-identical (ticksPerBar() == kTicksPerBar == 1920).
  std::uint8_t ts_numerator = 4;
  std::uint8_t ts_denominator = 4;

  /**
   * @brief Ticks per bar implied by the plan's time signature.
   * @return numerator * (4 * kTicksPerBeat / denominator). For 4/4 this is
   *         the global kTicksPerBar (1920); for 3/4 at 480 tpq it is 1440.
   * @note Single canonical bar-length accessor for the composer pipeline.
   *       Defaults to 1920 so any plan that does not set ts_* explicitly
   *       reproduces the pre-meter behavior exactly.
   */
  Tick ticksPerBar() const {
    const Tick quarter = kTicksPerBeat;              // ticks per quarter note.
    const Tick beat = quarter * 4 / ts_denominator;  // ticks per one beat.
    return beat * ts_numerator;
  }
};

}  // namespace bach::composer

#endif  // BACH_COMPOSER_HARMONIC_PLAN_H
