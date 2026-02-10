// Implementation of middle entry generation for fugue development.

#include "fugue/middle_entry.h"

#include "core/note_creator.h"
#include "core/note_source.h"
#include "transform/motif_transform.h"

namespace bach {

MiddleEntry generateMiddleEntry(const Subject& subject, Key target_key, Tick start_tick,
                                VoiceId voice_id) {
  MiddleEntry entry;
  entry.key = target_key;
  entry.start_tick = start_tick;
  entry.voice_id = voice_id;
  entry.end_tick = start_tick + subject.length_ticks;

  if (subject.notes.empty()) {
    return entry;
  }

  // Calculate transposition interval from subject key to target key.
  int semitones = static_cast<int>(target_key) - static_cast<int>(subject.key);

  // Use transposeMelody from the transform module.
  entry.notes = transposeMelody(subject.notes, semitones);

  // Offset tick positions so the entry starts at start_tick.
  Tick original_start = subject.notes[0].start_tick;
  for (auto& note : entry.notes) {
    note.start_tick = note.start_tick - original_start + start_tick;
    note.voice = voice_id;
  }

  return entry;
}

MiddleEntry generateMiddleEntry(const Subject& subject, Key target_key, Tick start_tick,
                                VoiceId voice_id,
                                CounterpointState& cp_state, IRuleEvaluator& cp_rules,
                                CollisionResolver& cp_resolver,
                                const HarmonicTimeline& /*timeline*/) {
  // Generate raw middle entry notes.
  MiddleEntry entry = generateMiddleEntry(subject, target_key, start_tick, voice_id);

  // Validate each note through createBachNote with Immutable protection.
  std::vector<NoteEvent> validated;
  validated.reserve(entry.notes.size());

  for (const auto& note : entry.notes) {
    BachNoteOptions opts;
    opts.voice = note.voice;
    opts.desired_pitch = note.pitch;
    opts.tick = note.start_tick;
    opts.duration = note.duration;
    opts.velocity = note.velocity;
    opts.source = BachNoteSource::FugueSubject;

    BachCreateNoteResult result = createBachNote(&cp_state, &cp_rules, &cp_resolver, opts);
    if (result.accepted) {
      validated.push_back(result.note);
    }
    // Rejected notes become rests (not added) -- subject pitches never altered.
  }

  entry.notes = std::move(validated);
  return entry;
}

MiddleEntry generateFalseEntry(const Subject& subject, Key target_key,
                               Tick start_tick, VoiceId voice_id,
                               uint8_t quote_notes) {
  MiddleEntry entry;
  entry.key = target_key;
  entry.start_tick = start_tick;
  entry.voice_id = voice_id;

  if (subject.notes.empty()) {
    entry.end_tick = start_tick;
    return entry;
  }

  // Clamp quote_notes to [2, min(4, subject note count)].
  uint8_t max_quote = static_cast<uint8_t>(
      subject.notes.size() < 4 ? subject.notes.size() : 4);
  if (quote_notes < 2) quote_notes = 2;
  if (quote_notes > max_quote) quote_notes = max_quote;

  // If subject has fewer than 2 notes, return empty.
  if (subject.notes.size() < 2) {
    entry.end_tick = start_tick;
    return entry;
  }

  // Calculate transposition interval from subject key to target key.
  int semitones = static_cast<int>(target_key) - static_cast<int>(subject.key);

  // Transpose entire subject, then take only the quoted portion.
  std::vector<NoteEvent> transposed = transposeMelody(subject.notes, semitones);

  // Offset tick positions so the entry starts at start_tick.
  Tick original_start = transposed[0].start_tick;

  // Compute average note duration from the subject for divergent notes.
  Tick total_duration = 0;
  for (const auto& note : subject.notes) {
    total_duration += note.duration;
  }
  Tick avg_duration = total_duration / static_cast<Tick>(subject.notes.size());
  if (avg_duration == 0) avg_duration = kTicksPerBeat;

  // Build quoted portion.
  Tick current_tick = start_tick;
  for (uint8_t idx = 0; idx < quote_notes; ++idx) {
    NoteEvent note = transposed[idx];
    note.start_tick = transposed[idx].start_tick - original_start + start_tick;
    note.voice = voice_id;
    note.source = BachNoteSource::FalseEntry;
    current_tick = note.start_tick + note.duration;
    entry.notes.push_back(note);
  }

  // Determine the divergence direction: opposite of the subject's final quoted interval.
  int last_interval = 0;
  if (quote_notes >= 2) {
    last_interval = static_cast<int>(transposed[quote_notes - 1].pitch) -
                    static_cast<int>(transposed[quote_notes - 2].pitch);
  }
  // Opposite direction: if subject went up, diverge down (and vice versa).
  // If the interval was zero, default to descending.
  int diverge_direction = (last_interval >= 0) ? -1 : 1;

  // Generate 2-3 divergent notes moving by step (1-2 semitones) in opposite direction.
  constexpr uint8_t kMinDivergent = 2;
  constexpr uint8_t kMaxDivergent = 3;
  // Use a fixed count of 3 for determinism (Principle 3: fewer choices).
  uint8_t num_divergent = kMaxDivergent;
  if (subject.notes.size() <= 3) {
    num_divergent = kMinDivergent;
  }

  uint8_t last_pitch = entry.notes.back().pitch;
  for (uint8_t div_idx = 0; div_idx < num_divergent; ++div_idx) {
    // Step size alternates 2, 1, 2 semitones for natural-sounding stepwise motion.
    int step = (div_idx % 2 == 0) ? 2 : 1;
    int new_pitch = static_cast<int>(last_pitch) + diverge_direction * step;

    // Clamp to valid MIDI range.
    if (new_pitch < 0) new_pitch = 0;
    if (new_pitch > 127) new_pitch = 127;

    NoteEvent div_note;
    div_note.start_tick = current_tick;
    div_note.duration = avg_duration;
    div_note.pitch = static_cast<uint8_t>(new_pitch);
    div_note.velocity = 80;
    div_note.voice = voice_id;
    div_note.source = BachNoteSource::FalseEntry;
    entry.notes.push_back(div_note);

    last_pitch = div_note.pitch;
    current_tick += avg_duration;
  }

  entry.end_tick = current_tick;
  return entry;
}

}  // namespace bach
