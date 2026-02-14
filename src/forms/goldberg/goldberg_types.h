// Goldberg Variations type definitions and descriptors.

#ifndef BACH_FORMS_GOLDBERG_GOLDBERG_TYPES_H
#define BACH_FORMS_GOLDBERG_GOLDBERG_TYPES_H

#include <cstdint>
#include <utility>

#include "core/basic_types.h"
#include "harmony/key.h"

namespace bach {

/// Goldberg variation type classification (Design Value).
enum class GoldbergVariationType : uint8_t {
  Aria,            ///< Sarabande theme.
  Ornamental,      ///< Ornamental variation.
  Invention,       ///< 2-3 voice invention/sinfonia.
  Fughetta,        ///< 3-4 voice fughetta.
  Canon,           ///< Strict canon.
  Dance,           ///< Dance variation.
  FrenchOverture,  ///< French overture.
  HandCrossing,    ///< Hand crossing technique.
  Toccata,         ///< Toccata-style.
  TrillEtude,      ///< Trill etude.
  ScalePassage,    ///< Scale passage work.
  BravuraChordal,  ///< Chordal bravura.
  BlackPearl,      ///< Var 25 Adagio.
  AllaBreveFugal,  ///< Alla breve fugal.
  Quodlibet        ///< Quodlibet (folk melody combination).
};

/// @brief Convert GoldbergVariationType to string.
/// @param type The variation type to convert.
/// @return Null-terminated string representation.
const char* goldbergVariationTypeToString(GoldbergVariationType type);

/// Minor mode profile for variations in G minor (Var 15, 21, 25).
enum class MinorModeProfile : uint8_t {
  NaturalMinor,      ///< Natural minor (descending melodic lines).
  HarmonicMinor,     ///< Harmonic minor (leading tone secured).
  MixedBaroqueMinor  ///< Baroque mixed minor (ascending=melodic, descending=natural).
};

/// Articulation profile for each variation type.
enum class ArticulationProfile : uint8_t {
  Legato,        ///< Legato (Aria, BlackPearl, Fughetta subject presentation).
  Moderato,      ///< Moderate (default for most variations).
  Detache,       ///< Detache (Gigue, Passepied dance types).
  FrenchDotted,  ///< French dotted style (Overture Grave).
  Brillante      ///< Brillante: chords heavy, passages light (BravuraChordal).
};

/// Tempo reference unit for different time signatures.
enum class TempoReferenceUnit : uint8_t {
  Eighth,         ///< Eighth note (3/8 variations).
  Quarter,        ///< Quarter note (standard 3/4).
  DottedQuarter,  ///< Dotted quarter (6/8 Gigue).
  Half            ///< Half note (alla breve 2/2).
};

/// Melody generation mode (two-layer architecture).
enum class MelodyMode : uint8_t {
  Inventio,    ///< Generate short soggetto, then develop (Canon, Fughetta, Invention).
  Elaboratio,  ///< Place figura patterns on structural grid (Ornamental, Dance).
  Special      ///< Individual logic (BlackPearl: suspension-driven, Quodlibet: design value).
};

/// Baroque figurae (Figurenlehre) patterns.
enum class FiguraType : uint8_t {
  Circulatio,   ///< Circular pattern: upper neighbor + lower neighbor rotation.
  Tirata,       ///< Scale run: rapid scalar passage.
  Batterie,     ///< Broken chord alternation: rapid register alternation.
  Arpeggio,     ///< Chord arpeggiation: chord tone expansion.
  Suspirans,    ///< Sigh motif: rest + descending second.
  Trillo,       ///< Sustained trill: main note + upper second alternation.
  DottedGrave,  ///< French dotted: grave dotted rhythm.
  Bariolage,    ///< Register alternation: rapid up/down broken chords.
  Sarabande,    ///< Sarabande pattern: beat 2 emphasis with dotted melody.
  Passepied,    ///< Light triple: light eighth-note dominated.
  Gigue         ///< Compound meter: third leaps + stepwise motion.
};

/// Direction bias for figura patterns.
enum class DirectionBias : uint8_t {
  Ascending,    ///< Upward dominant.
  Descending,   ///< Downward dominant.
  Symmetric,    ///< Symmetric (circular patterns).
  Alternating   ///< Alternating (Batterie patterns).
};

/// @brief Figura profile: concrete parameters for a figura type.
struct FiguraProfile {
  FiguraType primary;           ///< Primary figura type.
  FiguraType secondary;         ///< Secondary figura (phrase contrast).
  uint8_t notes_per_beat;       ///< Density (1=quarter, 2=eighth, 4=sixteenth).
  DirectionBias direction;      ///< Direction bias.
  float chord_tone_ratio;       ///< Chord tone ratio (0.7=stable, 0.4=more passing).
  float sequence_probability;   ///< Probability of sequential repetition.
};

/// @brief Canon interval and inversion descriptor.
struct CanonDescriptor {
  int interval_semitones = 0;  ///< Canon interval in semitones (0=unison).
  bool is_inverted = false;    ///< Whether the comes is inverted.
  uint8_t delay_beats = 1;     ///< Delay between dux and comes entries.
};

/// @brief Complete descriptor for a single Goldberg variation (Design Value).
/// Tempo character for harmonic time warp curvature control.
enum class GoldbergTempoCharacter : uint8_t {
  Stable,      ///< Canon/Fughetta: minimal warp, phase coherence.
  Dance,       ///< Dance/Ornamental: moderate swing.
  Expressive,  ///< Aria/Overture: lyrical rubato.
  Virtuosic,   ///< Toccata/HandCrossing: aggressive agogic.
  Lament       ///< BlackPearl: deep expressive warp.
};

struct GoldbergVariationDescriptor {
  int variation_number;                 ///< 0=Aria, 1-30, 31=da capo.
  GoldbergVariationType type;
  MelodyMode melody_mode;
  KeySignature key;                     ///< G major or G minor.
  TimeSignature time_sig;               ///< Default 3/4.
  MeterProfile meter_profile;
  uint8_t num_voices;                   ///< 2-4.
  CanonDescriptor canon;                ///< Canon parameters (Canon type only).
  uint16_t bpm_override;               ///< 0 = use global BPM.
  std::pair<int, int> tempo_ratio;      ///< Integer ratio to Aria (Proportionslehre).
  TempoReferenceUnit tempo_reference;
  MinorModeProfile minor_profile;       ///< Minor key variations only.
  ArticulationProfile articulation;
  float grid_strictness = 1.0f;         ///< Grid dominance (0.0-1.0).
  FiguraProfile figura;                 ///< Elaboratio mode parameters.
  SubjectCharacter soggetto_character;  ///< Inventio mode parameters.
  bool ornament_variation_on_repeat;    ///< Ornament variation on repeat.
  GoldbergTempoCharacter tempo_character = GoldbergTempoCharacter::Dance;  ///< Warp curvature.
};

/// Manual assignment policy for Goldberg voicing checks.
enum class ManualPolicy : uint8_t {
  Standard,       ///< voice 0,1=Upper, 2,3,4=Lower (most variations).
  HandCrossing,   ///< Hand crossing: skip one-hand playability checks.
  SingleManual,   ///< All voices on single manual: full isVoicingPlayable().
};

/// Get the manual policy for a given variation type.
inline ManualPolicy getManualPolicy(GoldbergVariationType type) {
  switch (type) {
    case GoldbergVariationType::HandCrossing:
      return ManualPolicy::HandCrossing;
    case GoldbergVariationType::Toccata:
    case GoldbergVariationType::BravuraChordal:
      return ManualPolicy::SingleManual;
    default:
      return ManualPolicy::Standard;
  }
}

}  // namespace bach

#endif  // BACH_FORMS_GOLDBERG_GOLDBERG_TYPES_H
