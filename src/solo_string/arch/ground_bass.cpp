// Implementation of immutable ground bass for chaconne structure.

#include "solo_string/arch/ground_bass.h"

#include <algorithm>
#include <string>

#include "analysis/fail_report.h"
#include "core/pitch_utils.h"

namespace bach {

GroundBass::GroundBass(std::vector<NoteEvent> bass_notes)
    : bass_notes_(std::move(bass_notes)) {}

const std::vector<NoteEvent>& GroundBass::getNotes() const {
  return bass_notes_;
}

NoteEvent GroundBass::getBassAt(Tick tick) const {
  // Normalize tick to position within the ground bass cycle.
  Tick length = getLengthTicks();
  if (length == 0) {
    NoteEvent rest{};
    rest.start_tick = tick;
    rest.velocity = 0;
    return rest;  // Rest sentinel: pitch=0, duration=0
  }

  Tick position = tick % length;

  for (const auto& note : bass_notes_) {
    Tick note_end = note.start_tick + note.duration;
    if (position >= note.start_tick && position < note_end) {
      // Return a copy with the queried tick as context, preserving original pitch/duration.
      NoteEvent result = note;
      return result;
    }
  }

  // No note covers this position -- return a rest.
  NoteEvent rest{};
  rest.start_tick = tick;
  rest.velocity = 0;
  return rest;  // Rest sentinel: pitch=0, duration=0
}

Tick GroundBass::getLengthTicks() const {
  if (bass_notes_.empty()) {
    return 0;
  }

  Tick max_end = 0;
  for (const auto& note : bass_notes_) {
    Tick note_end = note.start_tick + note.duration;
    if (note_end > max_end) {
      max_end = note_end;
    }
  }
  return max_end;
}

size_t GroundBass::noteCount() const {
  return bass_notes_.size();
}

bool GroundBass::verifyIntegrity(const std::vector<NoteEvent>& generated_bass) const {
  return !verifyIntegrityReport(generated_bass).hasCritical();
}

FailReport GroundBass::verifyIntegrityReport(
    const std::vector<NoteEvent>& generated_bass) const {
  FailReport report;

  // Check 1: Note count mismatch.
  if (generated_bass.size() != bass_notes_.size()) {
    FailIssue issue;
    issue.kind = FailKind::StructuralFail;
    issue.severity = FailSeverity::Critical;
    issue.rule = "note_count_mismatch";
    issue.description = "Expected " + std::to_string(bass_notes_.size()) +
                        " notes, found " + std::to_string(generated_bass.size());
    report.addIssue(issue);
    return report;
  }

  // Check 2-4: Per-note property mismatches.
  for (size_t idx = 0; idx < bass_notes_.size(); ++idx) {
    const auto& original = bass_notes_[idx];
    const auto& generated = generated_bass[idx];

    if (original.pitch != generated.pitch) {
      FailIssue issue;
      issue.kind = FailKind::StructuralFail;
      issue.severity = FailSeverity::Critical;
      issue.tick = original.start_tick;
      issue.bar = static_cast<uint8_t>(tickToBar(original.start_tick));
      issue.beat = beatInBar(original.start_tick);
      issue.rule = "pitch_mismatch";
      issue.description = "Note " + std::to_string(idx) + ": expected pitch " +
                          std::to_string(original.pitch) + ", found " +
                          std::to_string(generated.pitch);
      report.addIssue(issue);
    }

    if (original.start_tick != generated.start_tick) {
      FailIssue issue;
      issue.kind = FailKind::StructuralFail;
      issue.severity = FailSeverity::Critical;
      issue.tick = original.start_tick;
      issue.bar = static_cast<uint8_t>(tickToBar(original.start_tick));
      issue.beat = beatInBar(original.start_tick);
      issue.rule = "timing_mismatch";
      issue.description = "Note " + std::to_string(idx) + ": expected start_tick " +
                          std::to_string(original.start_tick) + ", found " +
                          std::to_string(generated.start_tick);
      report.addIssue(issue);
    }

    if (original.duration != generated.duration) {
      FailIssue issue;
      issue.kind = FailKind::StructuralFail;
      issue.severity = FailSeverity::Critical;
      issue.tick = original.start_tick;
      issue.bar = static_cast<uint8_t>(tickToBar(original.start_tick));
      issue.beat = beatInBar(original.start_tick);
      issue.rule = "duration_mismatch";
      issue.description = "Note " + std::to_string(idx) + ": expected duration " +
                          std::to_string(original.duration) + ", found " +
                          std::to_string(generated.duration);
      report.addIssue(issue);
    }
  }

  return report;
}

bool GroundBass::isEmpty() const {
  return bass_notes_.empty();
}

GroundBass GroundBass::createStandardDMinor() {
  // BWV1004-style 4-bar ground bass in D minor.
  // Each bar is kTicksPerBar (1920) ticks.
  // Half note = kTicksPerBar / 2 = 960 ticks.
  // Whole note = kTicksPerBar = 1920 ticks.
  constexpr Tick kHalfNote = kTicksPerBar / 2;
  constexpr Tick kWholeNote = kTicksPerBar;
  constexpr uint8_t kBassVelocity = 80;
  constexpr VoiceId kBassVoice = 0;

  // Pitch constants (MIDI note numbers).
  constexpr uint8_t kD3 = 50;
  constexpr uint8_t kCs3 = 49;
  constexpr uint8_t kBb2 = 46;
  constexpr uint8_t kG2 = 43;
  constexpr uint8_t kA2 = 45;

  std::vector<NoteEvent> notes;
  notes.reserve(7);

  // Bar 1: D3 whole note
  notes.push_back({0, kWholeNote, kD3, kBassVelocity, kBassVoice});

  // Bar 2: C#3 half, D3 half
  notes.push_back({kTicksPerBar, kHalfNote, kCs3, kBassVelocity, kBassVoice});
  notes.push_back({kTicksPerBar + kHalfNote, kHalfNote, kD3, kBassVelocity, kBassVoice});

  // Bar 3: Bb2 half, G2 half
  notes.push_back({2 * kTicksPerBar, kHalfNote, kBb2, kBassVelocity, kBassVoice});
  notes.push_back({2 * kTicksPerBar + kHalfNote, kHalfNote, kG2, kBassVelocity, kBassVoice});

  // Bar 4: A2 half, D3 half
  notes.push_back({3 * kTicksPerBar, kHalfNote, kA2, kBassVelocity, kBassVoice});
  notes.push_back({3 * kTicksPerBar + kHalfNote, kHalfNote, kD3, kBassVelocity, kBassVoice});

  for (auto& note : notes) {
    note.source = BachNoteSource::GroundBass;
  }

  return GroundBass(std::move(notes));
}

GroundBass GroundBass::createForKey(const KeySignature& key_sig,
                                   uint8_t register_low) {
  // Start from the standard D minor pattern and transpose.
  GroundBass standard = createStandardDMinor();

  // D minor tonic in octave 3 = MIDI 50 (D3).
  constexpr uint8_t kDMinorTonic = 50;

  // Calculate the target tonic pitch in the same octave range.
  // Use tonicPitch at octave 3 for the bass register.
  uint8_t target_tonic = tonicPitch(key_sig.tonic, 3);

  // Transposition interval in semitones.
  int transpose = static_cast<int>(target_tonic) - static_cast<int>(kDMinorTonic);

  // Transpose all notes by the interval.
  std::vector<NoteEvent> transposed_notes;
  transposed_notes.reserve(standard.noteCount());

  for (const auto& note : standard.getNotes()) {
    NoteEvent transposed = note;
    int new_pitch = clampPitch(static_cast<int>(note.pitch) + transpose, 0, 127);
    transposed.pitch = static_cast<uint8_t>(new_pitch);
    transposed_notes.push_back(transposed);
  }

  // Octave-shift the entire bass up so the lowest note >= register_low.
  if (register_low > 0) {
    uint8_t lowest = 127;
    for (const auto& note : transposed_notes) {
      if (note.pitch < lowest) lowest = note.pitch;
    }
    if (lowest < register_low) {
      int shift = ((static_cast<int>(register_low) - static_cast<int>(lowest) + 11) / 12) * 12;
      for (auto& note : transposed_notes) {
        int new_pitch = static_cast<int>(note.pitch) + shift;
        note.pitch = clampPitch(new_pitch, 0, 127);
      }
    }
  }

  return GroundBass(std::move(transposed_notes));
}

}  // namespace bach
