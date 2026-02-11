// Implementation of fugue answer generation (real and tonal).

#include "fugue/answer.h"

#include <cstdlib>

#include "core/pitch_utils.h"
#include "fugue/subject.h"

namespace bach {

namespace {

/// @brief Get the dominant key for a given tonic key.
/// @param tonic_key The tonic key.
/// @return The dominant key (a perfect 5th above).
Key getDominantKey(Key tonic_key) {
  int dominant = (static_cast<int>(tonic_key) + interval::kPerfect5th) % 12;
  return static_cast<Key>(dominant);
}

/// @brief Generate a real answer by transposing all notes up a perfect 5th.
/// @param subject The source subject.
/// @param dominant_key The dominant key.
/// @return Answer with transposed notes.
Answer generateRealAnswer(const Subject& subject, Key dominant_key) {
  Answer answer;
  answer.type = AnswerType::Real;
  answer.key = dominant_key;

  for (const auto& note : subject.notes) {
    NoteEvent transposed = note;
    int new_pitch = static_cast<int>(note.pitch) + interval::kPerfect5th;

    // Clamp to valid MIDI range.
    if (new_pitch > 127) new_pitch = 127;
    if (new_pitch < 0) new_pitch = 0;

    transposed.pitch = static_cast<uint8_t>(new_pitch);
    transposed.source = BachNoteSource::FugueAnswer;
    answer.notes.push_back(transposed);
  }

  return answer;
}

/// @brief Find the mutation point in a subject for tonal answer construction.
///
/// The mutation point is the index of the first note where a
/// tonic-dominant (or dominant-tonic) relationship should be reversed.
/// Typically this is at the start of the subject where the leap between
/// tonic and dominant occurs.
///
/// @param subject The subject to analyze.
/// @param tonic_class Pitch class of the tonic.
/// @param dominant_class Pitch class of the dominant.
/// @return Index of the mutation boundary (0-based, exclusive). Notes
///         before this index get the tonal adjustment; notes at and
///         after this index get a straight real-answer transposition.
size_t findMutationPoint(const Subject& subject,
                         int tonic_class, int dominant_class) {
  // Scan forward from the beginning. The mutation zone covers the
  // initial notes that move between tonic and dominant pitch classes.
  for (size_t idx = 0; idx < subject.notes.size(); ++idx) {
    int pitch_class = getPitchClass(subject.notes[idx].pitch);
    bool is_tonic = (pitch_class == tonic_class);
    bool is_dominant = (pitch_class == dominant_class);
    if (!is_tonic && !is_dominant) {
      // First note that is neither tonic nor dominant ends the mutation
      // zone.
      return idx;
    }
  }
  // All notes are tonic/dominant: the entire subject is in the mutation
  // zone.
  return subject.notes.size();
}

/// @brief Apply tonal adjustment to a single note.
///
/// Within the mutation zone, tonic notes become dominant notes and
/// dominant notes become tonic notes (in the answer key). Outside the
/// mutation zone, notes are simply transposed up a perfect 5th (real
/// answer).
///
/// @param note The original subject note.
/// @param tonic_class Tonic pitch class.
/// @param dominant_class Dominant pitch class.
/// @param in_mutation_zone Whether this note is within the mutation zone.
/// @return The adjusted note for the answer.
NoteEvent applyTonalAdjustment(const NoteEvent& note,
                               int tonic_class, int dominant_class,
                               bool in_mutation_zone) {
  NoteEvent result = note;
  result.source = BachNoteSource::FugueAnswer;

  if (in_mutation_zone) {
    int pitch_class = getPitchClass(note.pitch);

    if (pitch_class == tonic_class) {
      // Tonic in subject -> dominant in answer: tonic goes up a P4 instead
      // of P5 to invert the tonic-dominant relationship.
      // C -> G in subject becomes G -> C' in answer.
      int new_pitch = static_cast<int>(note.pitch) + interval::kPerfect4th;
      result.pitch = clampPitch(new_pitch, 0, 127);
    } else if (pitch_class == dominant_class) {
      // Dominant in subject -> tonic in answer.
      // G in subject -> C' in answer.
      // So dominant goes up a P4 (to reach the octave's tonic).
      int new_pitch = static_cast<int>(note.pitch) + interval::kPerfect5th;
      result.pitch = clampPitch(new_pitch, 0, 127);
    } else {
      // Non-tonic/dominant in mutation zone: standard P5 transposition.
      int new_pitch = static_cast<int>(note.pitch) + interval::kPerfect5th;
      result.pitch = clampPitch(new_pitch, 0, 127);
    }
  } else {
    // Outside mutation zone: real answer transposition.
    int new_pitch = static_cast<int>(note.pitch) + interval::kPerfect5th;
    result.pitch = clampPitch(new_pitch, 0, 127);
  }

  return result;
}

/// @brief Generate a tonal answer with mutation of tonic-dominant relations.
/// @param subject The source subject.
/// @param dominant_key The dominant key.
/// @return Answer with tonal adjustments in the mutation zone.
Answer generateTonalAnswer(const Subject& subject, Key dominant_key) {
  Answer answer;
  answer.type = AnswerType::Tonal;
  answer.key = dominant_key;

  int tonic_class = static_cast<int>(subject.key) % 12;
  int dominant_class = (tonic_class + interval::kPerfect5th) % 12;

  size_t mutation_end = findMutationPoint(subject, tonic_class, dominant_class);

  for (size_t idx = 0; idx < subject.notes.size(); ++idx) {
    bool in_mutation = (idx < mutation_end);
    NoteEvent adjusted = applyTonalAdjustment(
        subject.notes[idx], tonic_class, dominant_class, in_mutation);
    answer.notes.push_back(adjusted);
  }

  return answer;
}

}  // namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

AnswerType autoDetectAnswerType(const Subject& subject) {
  if (subject.noteCount() < 2) return AnswerType::Real;

  int tonic_class = static_cast<int>(subject.key) % 12;
  int dominant_class = (tonic_class + interval::kPerfect5th) % 12;

  // Check the first few notes for a tonic-dominant leap.
  // A leap between tonic and dominant pitch classes at the beginning
  // suggests a tonal answer is needed.
  size_t check_limit = std::min(subject.noteCount(), static_cast<size_t>(3));
  for (size_t idx = 0; idx + 1 < check_limit; ++idx) {
    int pc_curr = getPitchClass(subject.notes[idx].pitch);
    int pc_next = getPitchClass(subject.notes[idx + 1].pitch);

    bool curr_is_tonic = (pc_curr == tonic_class);
    bool curr_is_dominant = (pc_curr == dominant_class);
    bool next_is_tonic = (pc_next == tonic_class);
    bool next_is_dominant = (pc_next == dominant_class);

    // Tonic -> Dominant or Dominant -> Tonic leap at the start.
    if ((curr_is_tonic && next_is_dominant) ||
        (curr_is_dominant && next_is_tonic)) {
      int abs_int = absoluteInterval(subject.notes[idx].pitch,
                                     subject.notes[idx + 1].pitch);
      // Must be a significant interval (at least a 4th) to qualify as a
      // "tonic-dominant leap".
      if (abs_int >= interval::kPerfect4th) {
        return AnswerType::Tonal;
      }
    }
  }

  return AnswerType::Real;
}

Answer generateAnswer(const Subject& subject, AnswerType type) {
  Key dominant_key = getDominantKey(subject.key);

  if (type == AnswerType::Auto) {
    type = autoDetectAnswerType(subject);
  }

  Answer answer;
  if (type == AnswerType::Tonal) {
    answer = generateTonalAnswer(subject, dominant_key);
  } else {
    answer = generateRealAnswer(subject, dominant_key);
  }

  // Normalize the ending pitch so the answer's last interval doesn't diverge
  // wildly from the subject's. Uses the shared normalizer from subject.h.
  if (answer.notes.size() >= 2) {
    int prev_pitch = static_cast<int>(answer.notes[answer.notes.size() - 2].pitch);
    int max_leap = maxLeapForCharacter(subject.character);

    // Target: dominant of the answer key (tonic of the subject key).
    int target_pc = static_cast<int>(subject.key) % 12;
    ScaleType scale =
        subject.is_minor ? ScaleType::HarmonicMinor : ScaleType::Major;

    // Pitch bounds from subject.
    int floor_pitch = static_cast<int>(subject.lowestPitch());
    int ceil_pitch = static_cast<int>(subject.highestPitch()) + 12;
    if (ceil_pitch > 127) ceil_pitch = 127;

    int ending = normalizeEndingPitch(target_pc, prev_pitch, max_leap,
                                      dominant_key, scale,
                                      floor_pitch, ceil_pitch);
    answer.notes.back().pitch = static_cast<uint8_t>(ending);
  }

  return answer;
}

}  // namespace bach
