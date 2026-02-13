// Goldberg Variations 32-bar structural grid (Design Value).

#ifndef BACH_FORMS_GOLDBERG_GOLDBERG_STRUCTURAL_GRID_H
#define BACH_FORMS_GOLDBERG_GOLDBERG_STRUCTURAL_GRID_H

#include <array>
#include <cstdint>
#include <optional>

#include "core/basic_types.h"
#include "forms/goldberg/goldberg_types.h"
#include "harmony/chord_types.h"
#include "harmony/harmonic_event.h"
#include "harmony/harmonic_function.h"
#include "harmony/harmonic_timeline.h"
#include "harmony/key.h"

namespace bach {

/// Bar position within a 4-bar phrase (1-4).
using BarInPhrase = uint8_t;

/// @brief Abstract position within a phrase.
enum class PhrasePosition : uint8_t {
  Opening,          ///< Bar 1: phrase presentation, stability.
  Expansion,        ///< Bar 2: decorative expansion, harmonic development.
  Intensification,  ///< Bar 3: tension accumulation, melodic peak.
  Cadence           ///< Bar 4: resolution.
};

/// @brief Convert BarInPhrase (1-4) to PhrasePosition.
constexpr PhrasePosition toPhrasePosition(BarInPhrase bar_in_phrase) {
  switch (bar_in_phrase) {
    case 1: return PhrasePosition::Opening;
    case 2: return PhrasePosition::Expansion;
    case 3: return PhrasePosition::Intensification;
    default: return PhrasePosition::Cadence;
  }
}

/// @brief Structural hierarchy level.
enum class StructuralLevel : uint8_t {
  BarLevel,   ///< Individual bar.
  Phrase4,    ///< 4-bar phrase boundary (bars 4, 8, 12, ..., 32).
  Phrase8,    ///< 8-bar section boundary (bars 8, 16, 24, 32).
  Section16,  ///< 16-bar half boundary (bars 16, 32).
  Global32    ///< 32-bar global boundary (bar 32 only).
};

/// @brief Bass motion for structural grid (bass-driven principle).
struct StructuralBassMotion {
  uint8_t primary_pitch;                    ///< Harmonic pivot pitch (MIDI).
  std::optional<uint8_t> resolution_pitch;  ///< Resolution at cadence bars.
  bool is_descending_scale_member;          ///< Part of descending bass scale.
  uint8_t scale_degree_index;               ///< Index in descending scale (0-7).
};

/// @brief Multi-dimensional tension profile.
struct TensionProfile {
  float harmonic = 0.0f;   ///< Harmonic tension (dissonance, modulation distance).
  float melodic = 0.0f;    ///< Melodic tension (range expansion, leap frequency).
  float rhythmic = 0.0f;   ///< Rhythmic tension (note density, syncopation).
  float textural = 0.0f;   ///< Textural tension (voice density, register spread).

  /// @brief Weighted aggregate tension.
  float aggregate() const {
    return harmonic * 0.4f + melodic * 0.25f + rhythmic * 0.2f + textural * 0.15f;
  }
};

/// @brief Structural information for one bar of the 32-bar grid.
struct StructuralBarInfo {
  // Layer 1: Bass motion (primary structural foundation).
  StructuralBassMotion bass_motion;

  // Layer 1b: Harmonic function (derived from bass_motion).
  HarmonicFunction function;
  ChordDegree chord_degree;

  // Layer 3: Phrase structure.
  BarInPhrase bar_in_phrase;           ///< 1-4.
  PhrasePosition phrase_pos;
  std::optional<CadenceType> cadence;  ///< std::nullopt if no cadence.
  bool is_structural_bar;              ///< 4th bar of each phrase.
  uint8_t phrase_group;                ///< 0-7.

  // Layer 4: Structural waveform.
  StructuralLevel highest_level;
  TensionProfile tension;
};

/// @brief 32-bar structural grid for Goldberg Variations (Design Value).
///
/// The grid encodes the 4-layer structural foundation shared by all variations:
/// bass motion, harmonic function, phrase structure, and tension waveform.
/// Bass-driven principle: bass_motion is the primary structure; function and
/// harmony are derived from it.
class GoldbergStructuralGrid {
 public:
  /// @brief Create the G major structural grid.
  static GoldbergStructuralGrid createMajor();

  /// @brief Create a G minor structural grid.
  /// @param profile Minor mode profile (currently placeholder; returns major grid).
  static GoldbergStructuralGrid createMinor(MinorModeProfile profile);

  /// @brief Get bar info (0-indexed, 0-31).
  /// @param bar Bar index, clamped to [0, 31].
  /// @return Reference to the StructuralBarInfo for the given bar.
  const StructuralBarInfo& getBar(int bar) const;

  /// @brief Convert to HarmonicTimeline for generation use.
  /// @param key Key signature for the timeline events.
  /// @param time_sig Time signature for bar duration calculation.
  /// @return HarmonicTimeline with one event per bar.
  HarmonicTimeline toTimeline(const KeySignature& key,
                              const TimeSignature& time_sig) const;

  // Bass queries.

  /// @brief Get bass motion info for a bar.
  /// @param bar Bar index, clamped to [0, 31].
  const StructuralBassMotion& getBassMotion(int bar) const;

  /// @brief Get structural bass pitch (MIDI) for a bar.
  /// @param bar Bar index, clamped to [0, 31].
  uint8_t getStructuralBassPitch(int bar) const;

  /// @brief Check if the bar has a bass resolution pitch.
  /// @param bar Bar index, clamped to [0, 31].
  bool hasBassResolution(int bar) const;

  /// @brief Check if the bar contains a cadence.
  /// @param bar Bar index, clamped to [0, 31].
  bool isCadenceBar(int bar) const;

  // Phrase queries.

  /// @brief Get bar position within its 4-bar phrase (1-4).
  /// @param bar Bar index, clamped to [0, 31].
  BarInPhrase getBarInPhrase(int bar) const;

  /// @brief Get abstract phrase position for a bar.
  /// @param bar Bar index, clamped to [0, 31].
  PhrasePosition getPhrasePosition(int bar) const;

  /// @brief Get cadence type if present.
  /// @param bar Bar index, clamped to [0, 31].
  /// @return CadenceType or std::nullopt if no cadence at this bar.
  std::optional<CadenceType> getCadenceType(int bar) const;

  // Structural level queries.

  /// @brief Get the highest structural level for a bar.
  /// @param bar Bar index, clamped to [0, 31].
  StructuralLevel getStructuralLevel(int bar) const;

  /// @brief Get the tension profile for a bar.
  /// @param bar Bar index, clamped to [0, 31].
  const TensionProfile& getTension(int bar) const;

  /// @brief Get the weighted aggregate tension for a bar.
  /// @param bar Bar index, clamped to [0, 31].
  float getAggregateTension(int bar) const;

  /// @brief Check if the bar is a section boundary (Phrase8 or higher).
  /// @param bar Bar index, clamped to [0, 31].
  bool isSectionBoundary(int bar) const;

  // Hierarchical views.

  /// @brief View of a 4-bar phrase.
  struct Phrase4View {
    int start_bar;
    std::optional<CadenceType> cadence;
    TensionProfile peak_tension;
  };

  /// @brief Get a 4-bar phrase view.
  /// @param phrase_index Phrase index (0-7), clamped.
  Phrase4View getPhrase4(int phrase_index) const;

  /// @brief View of an 8-bar section.
  struct Phrase8View {
    int start_bar;
    std::optional<CadenceType> final_cadence;
    std::array<Phrase4View, 2> phrases;
  };

  /// @brief Get an 8-bar section view.
  /// @param section_index Section index (0-3), clamped.
  Phrase8View getPhrase8(int section_index) const;

  /// @brief View of a 16-bar half.
  struct Section16View {
    int start_bar;
    std::optional<CadenceType> section_cadence;
    std::array<Phrase8View, 2> phrases;
  };

  /// @brief Get a 16-bar half view.
  /// @param half Half index (0-1), clamped.
  Section16View getSection16(int half) const;

 private:
  std::array<StructuralBarInfo, 32> bars_;
};

}  // namespace bach

#endif  // BACH_FORMS_GOLDBERG_GOLDBERG_STRUCTURAL_GRID_H
