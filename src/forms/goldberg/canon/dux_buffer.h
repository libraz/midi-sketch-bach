// Bidirectional constraint buffer for canon generation.

#ifndef BACH_FORMS_GOLDBERG_CANON_DUX_BUFFER_H
#define BACH_FORMS_GOLDBERG_CANON_DUX_BUFFER_H

#include <cstdint>
#include <optional>
#include <vector>

#include "core/basic_types.h"
#include "forms/goldberg/canon/canon_types.h"
#include "forms/goldberg/goldberg_structural_grid.h"

namespace bach {

/// @brief Bidirectional constraint buffer for canon generation.
///
/// Records dux notes and derives comes notes via diatonic transformation.
/// Provides two constraint directions:
///   - Backward: deriveComes(t) = transform(dux(t - delay)) -- known from past dux.
///   - Forward: previewFutureComes(candidate) -- predicts what a candidate dux pitch
///     will produce as future comes, enabling lookahead scoring.
class DuxBuffer {
 public:
  /// @brief Construct a DuxBuffer for the given canon specification.
  /// @param spec Canon specification (interval, transform, key, delay).
  /// @param time_sig Time signature for delay-to-beat conversion.
  DuxBuffer(const CanonSpec& spec, const TimeSignature& time_sig);

  /// @brief Record a dux note at the given beat index.
  /// @param beat_index Beat position (0-based) within the variation.
  /// @param note The dux NoteEvent to record.
  void recordDux(int beat_index, const NoteEvent& note);

  /// @brief Derive the comes note at the given beat index.
  ///
  /// Looks up the dux note at (beat_index - delay_beats_), transforms its
  /// pitch, offsets its tick by the delay, and sets source to CanonComes.
  ///
  /// @param beat_index Beat position (0-based) within the variation.
  /// @return Transformed comes NoteEvent, or nullopt if the comes has not
  ///         entered yet or no dux note exists at the source beat.
  std::optional<NoteEvent> deriveComes(int beat_index) const;

  /// @brief Preview what pitch a candidate dux pitch would produce as future comes.
  ///
  /// Stateless transform: simply applies transformPitch to the candidate.
  /// Use this during dux note selection to score future comes viability.
  ///
  /// @param candidate_dux_pitch Candidate MIDI pitch for the dux.
  /// @return The MIDI pitch that would result as the comes note.
  uint8_t previewFutureComes(uint8_t candidate_dux_pitch) const;

  /// @brief Score climax alignment for a candidate dux pitch.
  ///
  /// Evaluates how well placing a new melodic climax at the current beat
  /// aligns with the grid's Intensification position. Returns a bonus
  /// (negative penalty) when the climax falls at Intensification, or a
  /// distance-based penalty otherwise.
  ///
  /// @param candidate_pitch Candidate MIDI pitch for the dux.
  /// @param current_beat Current beat index (0-based).
  /// @param grid The structural grid for phrase position lookup.
  /// @return Alignment score (negative = bonus, positive = penalty, 0 = neutral).
  float scoreClimaxAlignment(uint8_t candidate_pitch, int current_beat,
                             const GoldbergStructuralGrid& grid) const;

  /// @brief Get the delay in beats.
  /// @return Number of beats between dux and comes entry.
  int delayBeats() const { return delay_beats_; }

  /// @brief Get the canon specification.
  /// @return Const reference to the CanonSpec used to construct this buffer.
  const CanonSpec& spec() const { return spec_; }

 private:
  /// @brief Transform a dux pitch to comes pitch via diatonic transposition/inversion.
  ///
  /// For Regular transform: diatonic transposition by canon_interval scale degrees.
  /// For Inverted transform: diatonic inversion around tonic, then transposition.
  ///
  /// @param dux_pitch MIDI pitch of the dux note.
  /// @return Transformed MIDI pitch for the comes note.
  uint8_t transformPitch(uint8_t dux_pitch) const;

  /// @brief Get the ScaleType corresponding to the canon's key and minor profile.
  /// @return ScaleType for diatonic operations.
  ScaleType getScaleType() const;

  CanonSpec spec_;
  int delay_beats_;
  std::vector<std::optional<NoteEvent>> dux_notes_;  ///< Indexed by beat.
  uint8_t current_max_pitch_ = 0;  ///< Tracks melodic climax pitch.
  int climax_beat_ = -1;           ///< Beat index of current climax.
};

}  // namespace bach

#endif  // BACH_FORMS_GOLDBERG_CANON_DUX_BUFFER_H
