// Compound ornament generation implementation.

#include "ornament/compound_ornament.h"

#include "ornament/nachschlag.h"
#include "ornament/trill.h"
#include "ornament/turn.h"

namespace bach {

namespace {

/// @brief Generate a TrillWithNachschlag compound ornament.
///
/// Splits the note 85% trill / 15% nachschlag. The trill portion uses
/// upper-note start (C.P.E. Bach convention). The nachschlag portion
/// provides a clean resolution with lower-then-main ending.
///
/// @param note Original note.
/// @param upper_pitch Upper neighbor MIDI pitch.
/// @param lower_pitch Lower neighbor MIDI pitch (used as nachschlag resolution).
/// @param speed Trill speed.
/// @return Vector of sub-notes.
std::vector<NoteEvent> generateTrillWithNachschlag(const NoteEvent& note, uint8_t upper_pitch,
                                                   uint8_t lower_pitch, uint8_t speed) {
  // Split: 85% for trill, 15% for nachschlag.
  const Tick trill_duration = (note.duration * 85) / 100;
  const Tick nachschlag_duration = note.duration - trill_duration;

  // Build a temporary note for the trill portion.
  NoteEvent trill_note = note;
  trill_note.duration = trill_duration;

  auto result = generateTrill(trill_note, upper_pitch, speed, true);

  // Build a temporary note for the nachschlag portion.
  NoteEvent nachschlag_note;
  nachschlag_note.start_tick = note.start_tick + trill_duration;
  nachschlag_note.duration = nachschlag_duration;
  nachschlag_note.pitch = note.pitch;
  nachschlag_note.velocity = note.velocity;
  nachschlag_note.voice = note.voice;
  nachschlag_note.source = note.source;

  auto ending = generateNachschlag(nachschlag_note, lower_pitch);
  for (const auto& sub : ending) {
    result.push_back(sub);
  }

  return result;
}

/// @brief Generate a TurnThenTrill compound ornament.
///
/// Splits the note 25% turn / 75% trill. The turn provides an elegant
/// opening gesture before the sustained trill.
///
/// @param note Original note.
/// @param upper_pitch Upper neighbor MIDI pitch.
/// @param lower_pitch Lower neighbor MIDI pitch.
/// @param speed Trill speed.
/// @return Vector of sub-notes.
std::vector<NoteEvent> generateTurnThenTrill(const NoteEvent& note, uint8_t upper_pitch,
                                             uint8_t lower_pitch, uint8_t speed) {
  // Split: 25% for turn, 75% for trill.
  const Tick turn_duration = note.duration / 4;
  const Tick trill_duration = note.duration - turn_duration;

  // Build a temporary note for the turn portion.
  NoteEvent turn_note = note;
  turn_note.duration = turn_duration;

  auto result = generateTurn(turn_note, upper_pitch, lower_pitch);

  // Build a temporary note for the trill portion.
  NoteEvent trill_note;
  trill_note.start_tick = note.start_tick + turn_duration;
  trill_note.duration = trill_duration;
  trill_note.pitch = note.pitch;
  trill_note.velocity = note.velocity;
  trill_note.voice = note.voice;
  trill_note.source = note.source;

  auto trill_notes = generateTrill(trill_note, upper_pitch, speed, true);
  for (const auto& sub : trill_notes) {
    result.push_back(sub);
  }

  return result;
}

}  // namespace

std::vector<NoteEvent> generateCompoundOrnament(const NoteEvent& note,
                                                CompoundOrnamentType type,
                                                uint8_t upper_pitch, uint8_t lower_pitch,
                                                uint8_t speed) {
  // Minimum duration for compound ornaments: one full beat.
  if (note.duration < kTicksPerBeat) {
    return {note};
  }

  switch (type) {
    case CompoundOrnamentType::TrillWithNachschlag:
      return generateTrillWithNachschlag(note, upper_pitch, lower_pitch, speed);
    case CompoundOrnamentType::TurnThenTrill:
      return generateTurnThenTrill(note, upper_pitch, lower_pitch, speed);
  }

  return {note};
}

}  // namespace bach
