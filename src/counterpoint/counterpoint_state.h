// Counterpoint state container -- voice registration, note storage, and
// lookup.  NO rule logic lives here; rules are in IRuleEvaluator.

#ifndef BACH_COUNTERPOINT_COUNTERPOINT_STATE_H
#define BACH_COUNTERPOINT_COUNTERPOINT_STATE_H

#include <cstdint>
#include <map>
#include <vector>

#include "core/basic_types.h"

namespace bach {

/// @brief Pure state container for multi-voice counterpoint.
///
/// Stores registered voices with their pitch ranges and the notes generated
/// so far.  Notes within each voice are kept sorted by start_tick so that
/// tick-based lookups are efficient.  This class is intentionally rule-free;
/// all validation logic is delegated to IRuleEvaluator implementations.
class CounterpointState {
 public:
  /// Pitch range limits for a single voice.
  struct VoiceRange {
    uint8_t low = 0;
    uint8_t high = 127;
  };

  // -- Voice registration ---------------------------------------------------

  /// @brief Register a voice with its playable pitch range.
  /// @param voice_id Unique voice identifier.
  /// @param low_pitch Lowest playable MIDI pitch (inclusive).
  /// @param high_pitch Highest playable MIDI pitch (inclusive).
  void registerVoice(VoiceId voice_id, uint8_t low_pitch, uint8_t high_pitch);

  // -- Note management ------------------------------------------------------

  /// @brief Add a note to a voice's history (insertion keeps sort order).
  /// @param voice_id Target voice (must be registered).
  /// @param note The note event to add.
  void addNote(VoiceId voice_id, const NoteEvent& note);

  // -- Tick management ------------------------------------------------------

  /// @brief Set the current generation tick (global clock).
  void setCurrentTick(Tick tick);

  /// @brief Get the current generation tick.
  Tick getCurrentTick() const;

  // -- Query ----------------------------------------------------------------

  /// @brief Get all notes for a voice (sorted by start_tick).
  /// @param voice_id Voice to query.
  /// @return Reference to sorted note vector.  Empty if voice unregistered.
  const std::vector<NoteEvent>& getVoiceNotes(VoiceId voice_id) const;

  /// @brief Get the most recently added note for a voice.
  /// @return Pointer to last note, or nullptr if the voice has no notes.
  const NoteEvent* getLastNote(VoiceId voice_id) const;

  /// @brief Find a note sounding at a specific tick for a voice.
  /// @return Pointer to matching note, or nullptr if none found.
  const NoteEvent* getNoteAt(VoiceId voice_id, Tick tick) const;

  /// @brief Update the pitch of a note sounding at a specific tick for a voice.
  /// @return True if a matching note was found and updated.
  bool updateNotePitchAt(VoiceId voice_id, Tick tick, uint8_t new_pitch);

  /// @brief Get the pitch range for a registered voice.
  /// @return Pointer to VoiceRange, or nullptr if voice is not registered.
  const VoiceRange* getVoiceRange(VoiceId voice_id) const;

  /// @brief Get all registered voice IDs (in registration order).
  const std::vector<VoiceId>& getActiveVoices() const;

  /// @brief Number of registered voices.
  size_t voiceCount() const;

  // -- Key ------------------------------------------------------------------

  /// @brief Get the current musical key.
  Key getKey() const;

  /// @brief Set the current musical key.
  void setKey(Key key);

  // -- Reset ----------------------------------------------------------------

  /// @brief Clear all voices, notes, and reset tick/key to defaults.
  void clear();

 private:
  struct VoiceData {
    VoiceRange range;
    std::vector<NoteEvent> notes;
  };

  std::map<VoiceId, VoiceData> voices_;
  std::vector<VoiceId> active_voices_;
  Tick current_tick_ = 0;
  Key key_ = Key::C;

  /// Returned by getVoiceNotes() when the voice is not registered.
  static const std::vector<NoteEvent> kEmptyNotes;
};

}  // namespace bach

#endif  // BACH_COUNTERPOINT_COUNTERPOINT_STATE_H
