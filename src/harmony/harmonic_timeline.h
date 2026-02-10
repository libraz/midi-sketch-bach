// Harmonic timeline -- variable-density sequence of harmonic events.

#ifndef BACH_HARMONY_HARMONIC_TIMELINE_H
#define BACH_HARMONY_HARMONIC_TIMELINE_H

#include <vector>

#include "core/basic_types.h"
#include "harmony/chord_types.h"
#include "harmony/harmonic_event.h"
#include "harmony/key.h"

namespace bach {

/// Resolution of harmonic events in the timeline.
enum class HarmonicResolution : uint8_t {
  Beat,     // One event per beat (Organ system -- dense)
  Bar,      // One event per bar (Solo String Flow -- moderate)
  Section   // One event per section (coarse)
};

/// @brief A time-ordered sequence of harmonic events.
///
/// The HarmonicTimeline is the central harmonic data structure shared by
/// both Organ and Solo String systems. It provides lookup by tick position
/// and supports variable event densities (beat, bar, or section level).
///
/// Events must be added in chronological order. The timeline is immutable
/// once built for a given generation pass.
class HarmonicTimeline {
 public:
  /// @brief Construct an empty timeline.
  HarmonicTimeline();

  /// @brief Add a harmonic event to the timeline.
  /// @param event The event to add. Must have tick >= last event's tick.
  /// @note Events should be added in chronological order.
  void addEvent(const HarmonicEvent& event);

  /// @brief Get the harmonic event active at a given tick.
  /// @param tick The tick position to query.
  /// @return Reference to the active event. If the timeline is empty,
  ///         returns a static default event (C major, I chord).
  const HarmonicEvent& getAt(Tick tick) const;

  /// @brief Get the current key at a given tick.
  /// @param tick The tick position to query.
  /// @return The Key in effect at that tick.
  Key getKeyAt(Tick tick) const;

  /// @brief Get the current chord at a given tick.
  /// @param tick The tick position to query.
  /// @return Reference to the Chord in effect at that tick.
  const Chord& getChordAt(Tick tick) const;

  /// @brief Check if a tick falls on a key change boundary.
  /// @param tick The tick position to check.
  /// @return True if the event at this tick has a different key than the
  ///         preceding event (or is the first event with a non-default key).
  bool isKeyChange(Tick tick) const;

  /// @brief Get all events in the timeline.
  /// @return Const reference to the internal event vector.
  const std::vector<HarmonicEvent>& events() const;

  /// @brief Get the total duration covered by the timeline in ticks.
  /// @return The end_tick of the last event, or 0 if empty.
  Tick totalDuration() const;

  /// @brief Get the number of events in the timeline.
  /// @return Number of HarmonicEvent entries.
  size_t size() const;

  /// @brief Generate a standard diatonic progression for a given key and duration.
  ///
  /// Creates a I-IV-V-I progression distributed across the specified duration
  /// at the given harmonic resolution. Useful as a default harmonic backdrop
  /// for generation.
  ///
  /// @param key_sig Key signature (tonic + mode).
  /// @param duration Total duration in ticks.
  /// @param resolution Harmonic event density.
  /// @return A new HarmonicTimeline with the generated progression.
  static HarmonicTimeline createStandard(const KeySignature& key_sig, Tick duration,
                                         HarmonicResolution resolution);

 private:
  std::vector<HarmonicEvent> events_;

  /// @brief Default event returned when the timeline is empty or queried before first event.
  static const HarmonicEvent kDefaultEvent;

  /// @brief Find the index of the event active at a given tick using linear scan.
  /// @param tick The tick position to find.
  /// @return Index into events_, or -1 if no event covers this tick.
  int findEventIndex(Tick tick) const;
};

}  // namespace bach

#endif  // BACH_HARMONY_HARMONIC_TIMELINE_H
