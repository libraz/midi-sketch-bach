/// @file
/// @brief Implementation of CounterpointState - voice registration, note storage, and query.

#include "counterpoint/counterpoint_state.h"

#include <algorithm>

namespace bach {

const std::vector<NoteEvent> CounterpointState::kEmptyNotes;

// ---------------------------------------------------------------------------
// Voice registration
// ---------------------------------------------------------------------------

void CounterpointState::registerVoice(VoiceId voice_id, uint8_t low_pitch,
                                      uint8_t high_pitch) {
  // Avoid duplicate registration in the ordered list.
  if (voices_.find(voice_id) == voices_.end()) {
    active_voices_.push_back(voice_id);
  }
  VoiceData& data = voices_[voice_id];
  data.range.low = low_pitch;
  data.range.high = high_pitch;
}

// ---------------------------------------------------------------------------
// Note management
// ---------------------------------------------------------------------------

void CounterpointState::addNote(VoiceId voice_id, const NoteEvent& note) {
  auto iter = voices_.find(voice_id);
  if (iter == voices_.end()) {
    return;  // Voice not registered -- silently ignore.
  }

  auto& notes = iter->second.notes;

  // Insert in sorted order by start_tick.  Most notes are appended in
  // chronological order, so check the back first for O(1) typical case.
  if (notes.empty() || notes.back().start_tick <= note.start_tick) {
    notes.push_back(note);
  } else {
    auto pos = std::lower_bound(
        notes.begin(), notes.end(), note,
        [](const NoteEvent& lhs, const NoteEvent& rhs) {
          return lhs.start_tick < rhs.start_tick;
        });
    notes.insert(pos, note);
  }
}

// ---------------------------------------------------------------------------
// Tick management
// ---------------------------------------------------------------------------

void CounterpointState::setCurrentTick(Tick tick) { current_tick_ = tick; }

Tick CounterpointState::getCurrentTick() const { return current_tick_; }

// ---------------------------------------------------------------------------
// Query
// ---------------------------------------------------------------------------

const std::vector<NoteEvent>& CounterpointState::getVoiceNotes(
    VoiceId voice_id) const {
  auto iter = voices_.find(voice_id);
  if (iter == voices_.end()) {
    return kEmptyNotes;
  }
  return iter->second.notes;
}

const NoteEvent* CounterpointState::getLastNote(VoiceId voice_id) const {
  auto iter = voices_.find(voice_id);
  if (iter == voices_.end() || iter->second.notes.empty()) {
    return nullptr;
  }
  return &iter->second.notes.back();
}

const NoteEvent* CounterpointState::getNoteAt(VoiceId voice_id,
                                              Tick tick) const {
  auto iter = voices_.find(voice_id);
  if (iter == voices_.end()) {
    return nullptr;
  }

  const auto& notes = iter->second.notes;

  // Binary search: find the last note whose start_tick <= tick.
  auto pos = std::upper_bound(
      notes.begin(), notes.end(), tick,
      [](Tick target, const NoteEvent& note) {
        return target < note.start_tick;
      });

  // Walk backwards to find a note that is still sounding at `tick`.
  while (pos != notes.begin()) {
    --pos;
    if (pos->start_tick <= tick &&
        tick < pos->start_tick + pos->duration) {
      return &(*pos);
    }
    // If the note starts before the tick but has already ended, earlier
    // notes with even smaller start_tick will also have ended (assuming
    // non-overlapping notes in the same voice), so stop.
    if (pos->start_tick + pos->duration <= tick) {
      break;
    }
  }
  return nullptr;
}

bool CounterpointState::updateNotePitchAt(VoiceId voice_id, Tick tick,
                                          uint8_t new_pitch) {
  auto iter = voices_.find(voice_id);
  if (iter == voices_.end()) {
    return false;
  }

  auto& notes = iter->second.notes;

  auto pos = std::upper_bound(
      notes.begin(), notes.end(), tick,
      [](Tick target, const NoteEvent& note) {
        return target < note.start_tick;
      });

  while (pos != notes.begin()) {
    --pos;
    if (pos->start_tick <= tick &&
        tick < pos->start_tick + pos->duration) {
      pos->pitch = new_pitch;
      return true;
    }
    if (pos->start_tick + pos->duration <= tick) {
      break;
    }
  }
  return false;
}

const CounterpointState::VoiceRange* CounterpointState::getVoiceRange(
    VoiceId voice_id) const {
  auto iter = voices_.find(voice_id);
  if (iter == voices_.end()) {
    return nullptr;
  }
  return &iter->second.range;
}

const std::vector<VoiceId>& CounterpointState::getActiveVoices() const {
  return active_voices_;
}

size_t CounterpointState::voiceCount() const { return voices_.size(); }

// ---------------------------------------------------------------------------
// Key
// ---------------------------------------------------------------------------

Key CounterpointState::getKey() const { return key_; }

void CounterpointState::setKey(Key key) { key_ = key; }

// ---------------------------------------------------------------------------
// Reset
// ---------------------------------------------------------------------------

void CounterpointState::clear() {
  voices_.clear();
  active_voices_.clear();
  current_tick_ = 0;
  key_ = Key::C;
}

}  // namespace bach
