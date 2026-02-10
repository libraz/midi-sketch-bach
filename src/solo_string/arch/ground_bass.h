// Immutable ground bass for chaconne structure (Arch system).

#ifndef BACH_SOLO_STRING_ARCH_GROUND_BASS_H
#define BACH_SOLO_STRING_ARCH_GROUND_BASS_H

#include <vector>

#include "core/basic_types.h"
#include "harmony/key.h"

namespace bach {

/// @brief Immutable ground bass theme for chaconne structure.
///
/// The ground bass is the fixed harmonic foundation that repeats unchanged
/// throughout all variations. It MUST NOT be modified by any generation logic.
/// After generation, verifyIntegrity() checks that the bass was preserved.
///
/// Design principle: GroundBass notes are immutable. Any attempt to modify
/// them constitutes a STRUCTURAL_FAIL. The generation logic works around
/// the bass, never altering it.
///
/// Typical length: 4 bars (7680 ticks in 4/4 at 480 ticks/beat).
class GroundBass {
 public:
  /// @brief Construct with explicit bass notes.
  /// @param bass_notes The sequence of notes forming the ground bass.
  ///        These are copied and stored immutably.
  explicit GroundBass(std::vector<NoteEvent> bass_notes);

  /// @brief Default constructor creates an empty ground bass.
  GroundBass() = default;

  /// @brief Get the immutable bass notes.
  /// @return Const reference to the internal note vector.
  const std::vector<NoteEvent>& getNotes() const;

  /// @brief Get the bass note active at a given tick.
  ///
  /// Searches for the note whose time span [start_tick, start_tick + duration)
  /// covers the queried tick. If no note covers the tick, returns a rest
  /// (pitch = 0, velocity = 0).
  ///
  /// @param tick The tick position to query.
  /// @return The bass note active at that tick, or a rest note.
  NoteEvent getBassAt(Tick tick) const;

  /// @brief Get the total length of the ground bass in ticks.
  ///
  /// Calculated as the maximum of (start_tick + duration) across all notes.
  /// Typically 4 bars = 4 * 1920 = 7680 ticks.
  ///
  /// @return Total length in ticks, or 0 if empty.
  Tick getLengthTicks() const;

  /// @brief Get the number of notes in the ground bass.
  /// @return Number of NoteEvent entries.
  size_t noteCount() const;

  /// @brief Verify that generated bass notes match the original exactly.
  ///
  /// Compares pitch, start_tick, and duration of each note in sequence.
  /// Returns false immediately if any note differs (instant STRUCTURAL_FAIL).
  ///
  /// @param generated_bass The bass notes extracted from generated output.
  /// @return true if every note matches exactly, false if any modification detected.
  bool verifyIntegrity(const std::vector<NoteEvent>& generated_bass) const;

  /// @brief Check if the ground bass has any notes.
  /// @return true if at least one note is present.
  bool isEmpty() const;

  /// @brief Create a standard D minor chaconne ground bass (BWV1004 style).
  ///
  /// 4-bar theme in D minor:
  /// - Bar 1: D3 (50) whole note
  /// - Bar 2: C#3 (49) half, D3 (50) half
  /// - Bar 3: Bb2 (46) half, G2 (43) half
  /// - Bar 4: A2 (45) half, D3 (50) half
  ///
  /// @return A GroundBass initialized with the standard D minor pattern.
  static GroundBass createStandardDMinor();

  /// @brief Create a ground bass for a given key.
  ///
  /// Generates a standard 4-bar chaconne bass pattern transposed from the
  /// D minor template to the specified key. The intervallic structure is
  /// preserved; only the absolute pitches change.
  ///
  /// @param key_sig The target key signature.
  /// @return A GroundBass in the specified key.
  static GroundBass createForKey(const KeySignature& key_sig);

 private:
  std::vector<NoteEvent> bass_notes_;
};

}  // namespace bach

#endif  // BACH_SOLO_STRING_ARCH_GROUND_BASS_H
