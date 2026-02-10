// Interface for bowed string instrument models (violin, cello).

#ifndef BACH_INSTRUMENT_BOWED_BOWED_STRING_INSTRUMENT_H
#define BACH_INSTRUMENT_BOWED_BOWED_STRING_INSTRUMENT_H

#include <cstdint>
#include <vector>

#include "core/basic_types.h"
#include "instrument/common/performer_types.h"

namespace bach {

/// @brief Bow stroke direction for bowed string instruments.
enum class BowDirection : uint8_t {
  Natural = 0,  ///< Let performer decide (e.g., large string crossing)
  Down = 1,     ///< Downbow (typically stronger, used on strong beats)
  Up = 2        ///< Upbow (typically lighter, used on weak beats)
};

/// @brief Convert BowDirection to human-readable string.
/// @param direction The bow direction to convert.
/// @return "Natural", "Down", or "Up".
inline const char* bowDirectionToString(BowDirection direction) {
  switch (direction) {
    case BowDirection::Natural: return "Natural";
    case BowDirection::Down:    return "Down";
    case BowDirection::Up:      return "Up";
  }
  return "Unknown";  // NOLINT(clang-diagnostic-covered-switch-default): safety for cast values
}

/// @brief Finger position on a bowed string instrument.
///
/// Describes where a pitch is produced: which string, which position (shift),
/// and any enharmonic offset from the default fingering.
struct FingerPosition {
  uint8_t string_idx = 0;    // String index (0 = lowest string)
  uint8_t position = 0;      // Position/shift number (0 = open, 1 = 1st, etc.)
  int8_t pitch_offset = 0;   // Offset from open string pitch in semitones
};

/// @brief Ergonomic cost breakdown for a bowed string note or transition.
///
/// Used by the flow and arch generators to penalize difficult left-hand
/// shifts and bow movements on bowed instruments.
struct BowedPlayabilityCost {
  float total = 0.0f;              // Overall playability cost
  float left_hand_cost = 0.0f;     // Fingering difficulty (position, stretch)
  float string_crossing_cost = 0.0f;  // Cost of crossing strings with the bow
  float shift_cost = 0.0f;         // Cost of left-hand position shift
  bool is_playable = true;         // Whether the note is physically possible
};

/// @brief Mutable state of a bowed string performer during generation.
///
/// Extends PerformerState with bow direction, current string, and
/// left-hand position tracking.
struct BowedPerformerState : PerformerState {
  BowDirection bow_direction = BowDirection::Down;
  uint8_t current_string = 0;    // Index of the string currently under the bow
  uint8_t current_position = 0;  // Left-hand position (0 = open/1st pos, 1 = 2nd, etc.)

  /// @brief Reset to initial (rested) state with downbow on lowest string.
  void reset() override {
    PerformerState::reset();
    bow_direction = BowDirection::Down;
    current_string = 0;
    current_position = 0;
  }
};

/// @brief Abstract interface for bowed string instrument models.
///
/// Provides physical playability constraints for bowed string instruments
/// (violin, cello, viola, etc.). Evaluates whether pitches are reachable
/// given string tuning and position, and calculates ergonomic costs for
/// string crossings, position shifts, and double stops.
///
/// Bowed instruments have unique constraints compared to keyboard or fretted:
///   - Only one or two adjacent strings can be sustained simultaneously
///   - Three or more string chords must be arpeggiated
///   - Bow direction alternates and affects phrasing
///   - Left-hand position shifts are costly
///   - Open strings have special timbral character
class IBowedStringInstrument {
 public:
  virtual ~IBowedStringInstrument() = default;

  // -------------------------------------------------------------------------
  // Range and tuning
  // -------------------------------------------------------------------------

  /// @brief Get the number of strings on this instrument.
  /// @return Number of strings (typically 4).
  virtual uint8_t getStringCount() const = 0;

  /// @brief Get the open string tuning as MIDI pitch values.
  /// @return Vector of open string MIDI pitches, ordered from lowest to highest.
  virtual const std::vector<uint8_t>& getTuning() const = 0;

  /// @brief Get the lowest playable MIDI pitch.
  virtual uint8_t getLowestPitch() const = 0;

  /// @brief Get the highest playable MIDI pitch.
  virtual uint8_t getHighestPitch() const = 0;

  /// @brief Check if a pitch is within the instrument's playable range.
  /// @param pitch MIDI pitch number (0-127).
  /// @return True if the pitch can be produced on at least one string.
  virtual bool isPitchPlayable(uint8_t pitch) const = 0;

  // -------------------------------------------------------------------------
  // String and fingering queries
  // -------------------------------------------------------------------------

  /// @brief Check whether a pitch corresponds to an open string.
  /// @param pitch MIDI pitch number.
  /// @return True if the pitch matches any open string.
  virtual bool isOpenString(uint8_t pitch) const = 0;

  /// @brief Get all possible finger positions that produce a given pitch.
  /// @param pitch MIDI pitch number.
  /// @return Vector of FingerPosition options, sorted by ergonomic preference.
  ///         Empty if the pitch is unplayable.
  virtual std::vector<FingerPosition> getPositionsForPitch(uint8_t pitch) const = 0;

  // -------------------------------------------------------------------------
  // Double stops and chords
  // -------------------------------------------------------------------------

  /// @brief Check if two simultaneous pitches form a feasible double stop.
  ///
  /// Double stops require two adjacent strings. Both pitches must be
  /// reachable on adjacent strings in the same left-hand position.
  ///
  /// @param pitch_a First MIDI pitch.
  /// @param pitch_b Second MIDI pitch.
  /// @return True if the two pitches can be sustained simultaneously.
  virtual bool isDoubleStopFeasible(uint8_t pitch_a, uint8_t pitch_b) const = 0;

  /// @brief Calculate the ergonomic cost of a double stop.
  /// @param pitch_a First MIDI pitch.
  /// @param pitch_b Second MIDI pitch.
  /// @return Cost value where 0.0 = easy, higher = more difficult.
  ///         Returns a very high cost if the double stop is infeasible.
  virtual float doubleStopCost(uint8_t pitch_a, uint8_t pitch_b) const = 0;

  /// @brief Check if a multi-note chord requires arpeggiation.
  ///
  /// On bowed instruments, only 2 adjacent strings can sustain simultaneously.
  /// Chords of 3 or more notes must be arpeggiated (rolled).
  ///
  /// @param pitches Vector of simultaneous MIDI pitches.
  /// @return True if the chord must be arpeggiated (3+ strings needed).
  virtual bool requiresArpeggiation(const std::vector<uint8_t>& pitches) const = 0;

  // -------------------------------------------------------------------------
  // Bow and string crossing
  // -------------------------------------------------------------------------

  /// @brief Calculate the cost of crossing from one string to another.
  ///
  /// Adjacent string crossings are natural and low cost. Skipping strings
  /// is progressively more difficult and unusual in Bach's writing.
  ///
  /// @param from_string Source string index (0 = lowest).
  /// @param to_string Destination string index.
  /// @return Cost value where 0.0 = same string, increasing with distance.
  virtual float stringCrossingCost(uint8_t from_string,
                                   uint8_t to_string) const = 0;

  /// @brief Check if this instrument supports bariolage technique.
  ///
  /// Bariolage: rapid alternation between a stopped note and an adjacent
  /// open string at the same or similar pitch. Common in Bach violin
  /// and cello writing (e.g., BWV 1004 Chaconne, BWV 1007 Prelude).
  ///
  /// @return True if the instrument supports bariolage.
  virtual bool supportsBariolage() const = 0;

  // -------------------------------------------------------------------------
  // Playability cost
  // -------------------------------------------------------------------------

  /// @brief Calculate the ergonomic cost of playing a single pitch.
  /// @param pitch MIDI pitch number.
  /// @return BowedPlayabilityCost with component breakdown.
  virtual BowedPlayabilityCost calculateCost(uint8_t pitch) const = 0;

  /// @brief Calculate the transition cost between two consecutive notes.
  /// @param from_pitch Previous MIDI pitch (0 if first note).
  /// @param to_pitch Next MIDI pitch.
  /// @param state Current performer state (bow, string, position).
  /// @return BowedPlayabilityCost with transition-specific component breakdown.
  virtual BowedPlayabilityCost calculateTransitionCost(
      uint8_t from_pitch, uint8_t to_pitch,
      const BowedPerformerState& state) const = 0;

  /// @brief Update performer state after playing a note.
  /// @param state Performer state to mutate.
  /// @param pitch MIDI pitch that was performed.
  virtual void updateState(BowedPerformerState& state, uint8_t pitch) const = 0;

  /// @brief Create a fresh initial performer state.
  /// @return BowedPerformerState in starting configuration.
  virtual BowedPerformerState createInitialState() const = 0;
};

}  // namespace bach

#endif  // BACH_INSTRUMENT_BOWED_BOWED_STRING_INSTRUMENT_H
