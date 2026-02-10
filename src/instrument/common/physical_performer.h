// Interface for physical instrument performers.

#ifndef BACH_INSTRUMENT_COMMON_PHYSICAL_PERFORMER_H
#define BACH_INSTRUMENT_COMMON_PHYSICAL_PERFORMER_H

#include <cstdint>
#include <memory>
#include <vector>

#include "instrument/common/performer_types.h"

namespace bach {

/// @brief Abstract interface for physical instrument performer models.
///
/// Each instrument (piano, guitar, violin, etc.) implements this interface
/// to provide physical playability constraints, cost estimation, and
/// alternative pitch suggestions. The performer model does not generate
/// music -- it evaluates whether generated music is physically realizable.
class IPhysicalPerformer {
 public:
  virtual ~IPhysicalPerformer() = default;

  /// @brief Return the performer type (Keyboard, Bowed, Fretted).
  virtual PerformerType getType() const = 0;

  /// @brief Check whether a note is physically performable.
  /// @param pitch MIDI pitch number (0-127).
  /// @param start Absolute tick position.
  /// @param duration Duration in ticks.
  /// @return True if the note can be performed.
  virtual bool canPerform(uint8_t pitch, Tick start, Tick duration) const = 0;

  /// @brief Calculate the ergonomic cost of performing a note.
  /// @param pitch MIDI pitch number.
  /// @param start Absolute tick position.
  /// @param duration Duration in ticks.
  /// @param state Current performer state (hand position, fatigue, etc.).
  /// @return Cost value where 0.0 = effortless, higher = more difficult.
  virtual float calculateCost(uint8_t pitch, Tick start, Tick duration,
                              const PerformerState& state) const = 0;

  /// @brief Suggest alternative pitches when the desired pitch is unplayable.
  /// @param desired_pitch The pitch that was requested.
  /// @param start Absolute tick position.
  /// @param duration Duration in ticks.
  /// @param range_low Lowest acceptable alternative pitch.
  /// @param range_high Highest acceptable alternative pitch.
  /// @return Vector of playable alternative pitches, sorted by preference.
  virtual std::vector<uint8_t> suggestAlternatives(uint8_t desired_pitch, Tick start,
                                                   Tick duration, uint8_t range_low,
                                                   uint8_t range_high) const = 0;

  /// @brief Update performer state after performing a note.
  /// @param state Performer state to mutate.
  /// @param pitch MIDI pitch that was performed.
  /// @param start Absolute tick position.
  /// @param duration Duration in ticks.
  virtual void updateState(PerformerState& state, uint8_t pitch, Tick start,
                           Tick duration) const = 0;

  /// @brief Create a fresh initial performer state.
  /// @return Unique pointer to a new state appropriate for this performer type.
  virtual std::unique_ptr<PerformerState> createInitialState() const = 0;

  /// @brief Get the lowest playable MIDI pitch.
  virtual uint8_t getMinPitch() const = 0;

  /// @brief Get the highest playable MIDI pitch.
  virtual uint8_t getMaxPitch() const = 0;
};

}  // namespace bach

#endif  // BACH_INSTRUMENT_COMMON_PHYSICAL_PERFORMER_H
