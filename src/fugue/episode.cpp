// Fugue episode: modulatory development of subject material.

#include "fugue/episode.h"

#include <algorithm>
#include <cstdint>
#include <random>

#include "core/rng_util.h"
#include "transform/motif_transform.h"
#include "transform/sequence.h"

namespace bach {

namespace {

/// @brief Determine the sequence interval step based on subject character.
/// @param character Subject character influencing episode style.
/// @param rng Mersenne Twister instance for Playful/Restless randomization.
/// @return Interval step in semitones (typically negative for descending).
int sequenceIntervalForCharacter(SubjectCharacter character, std::mt19937& rng) {
  switch (character) {
    case SubjectCharacter::Severe:
      return -2;  // Strict descending 2nds (Baroque convention)
    case SubjectCharacter::Playful:
      return rng::rollRange(rng, -3, -1);  // Varied descending steps
    case SubjectCharacter::Noble:
      return -2;  // Moderate, like Severe but with different voicing
    case SubjectCharacter::Restless:
      return rng::rollRange(rng, -3, -1);  // Chromatic tendency
    default:
      return -2;
  }
}

/// @brief Determine the imitation offset for the second voice.
///
/// In Baroque fugue episodes, voices often enter in staggered imitation.
/// The offset controls how far behind voice 1 enters relative to voice 0.
///
/// @param motif_dur Duration of the motif in ticks.
/// @param character Subject character.
/// @return Imitation offset in ticks.
Tick imitationOffsetForCharacter(Tick motif_dur, SubjectCharacter character) {
  switch (character) {
    case SubjectCharacter::Restless:
      // Tighter imitation (quarter of motif)
      return std::max(motif_dur / 4, static_cast<Tick>(kTicksPerBeat));
    case SubjectCharacter::Playful:
      // Slightly offset (third of motif)
      return std::max(motif_dur / 3, static_cast<Tick>(kTicksPerBeat));
    case SubjectCharacter::Severe:
    case SubjectCharacter::Noble:
    default:
      // Standard half-motif offset
      return std::max(motif_dur / 2, static_cast<Tick>(kTicksPerBeat));
  }
}

/// @brief Calculate the number of sequence repetitions that fit in the duration.
/// @param duration_ticks Total available duration.
/// @param motif_dur Duration of one motif instance.
/// @return Number of repetitions, clamped to [1, 4].
int calculateSequenceRepetitions(Tick duration_ticks, Tick motif_dur) {
  if (motif_dur == 0) return 1;
  int reps = static_cast<int>(duration_ticks / motif_dur);
  if (reps < 1) reps = 1;
  if (reps > 4) reps = 4;
  return reps;
}

/// @brief Clamp a pitch value to valid MIDI range [0, 127].
/// @param value Pitch value that may be out of range.
/// @return Clamped uint8_t pitch.
uint8_t clampMidiPitch(int value) {
  if (value < 0) return 0;
  if (value > 127) return 127;
  return static_cast<uint8_t>(value);
}

/// @brief Normalize a motif so that the first note starts at tick 0.
/// @param motif Input motif (modified in place).
void normalizeMotifToZero(std::vector<NoteEvent>& motif) {
  if (motif.empty()) return;
  Tick offset = motif[0].start_tick;
  for (auto& note : motif) {
    note.start_tick -= offset;
  }
}

}  // namespace

std::vector<NoteEvent> extractMotif(const Subject& subject, size_t max_notes) {
  std::vector<NoteEvent> motif;
  size_t count = std::min(max_notes, subject.noteCount());
  motif.reserve(count);
  for (size_t idx = 0; idx < count; ++idx) {
    motif.push_back(subject.notes[idx]);
  }
  return motif;
}

Episode generateEpisode(const Subject& subject, Tick start_tick, Tick duration_ticks,
                        Key start_key, Key target_key, uint8_t num_voices, uint32_t seed) {
  std::mt19937 rng(seed);

  Episode episode;
  episode.start_tick = start_tick;
  episode.end_tick = start_tick + duration_ticks;
  episode.start_key = start_key;
  episode.end_key = target_key;

  // Extract motif from subject head (first 3-4 notes).
  auto motif = extractMotif(subject, 4);
  if (motif.empty()) {
    return episode;
  }

  // Normalize motif timing to start at tick 0 for transformation operations.
  normalizeMotifToZero(motif);

  Tick motif_dur = motifDuration(motif);
  if (motif_dur == 0) {
    motif_dur = kTicksPerBar;
  }

  // Determine transformation parameters based on character.
  int seq_interval = sequenceIntervalForCharacter(subject.character, rng);
  int seq_reps = calculateSequenceRepetitions(duration_ticks, motif_dur);
  Tick imitation_offset = imitationOffsetForCharacter(motif_dur, subject.character);

  // Calculate transposition for key modulation.
  int key_diff = static_cast<int>(target_key) - static_cast<int>(start_key);

  // --- Voice 0: Primary sequential pattern (Zeugma) ---
  // Place original motif at start_tick.
  for (const auto& note : motif) {
    NoteEvent placed = note;
    placed.start_tick += start_tick;
    placed.voice = 0;
    episode.notes.push_back(placed);
  }

  // Generate sequence repetitions following the initial motif statement.
  auto seq_notes = generateSequence(motif, seq_reps, seq_interval, start_tick + motif_dur);
  for (auto& note : seq_notes) {
    note.voice = 0;
    episode.notes.push_back(note);
  }

  // --- Voice 1: Inverted imitation ---
  if (num_voices >= 2) {
    uint8_t pivot = motif[0].pitch;
    auto inverted = invertMelody(motif, pivot);

    // Place inverted motif at the imitation offset.
    Tick voice1_start = start_tick + imitation_offset;
    for (const auto& note : inverted) {
      NoteEvent placed = note;
      placed.start_tick += voice1_start;
      placed.voice = 1;
      episode.notes.push_back(placed);
    }

    // Sequence of inverted motif (one fewer rep to avoid overrunning duration).
    int inv_reps = std::max(1, seq_reps - 1);
    auto inv_seq = generateSequence(inverted, inv_reps, seq_interval,
                                    voice1_start + motif_dur);
    for (auto& note : inv_seq) {
      note.voice = 1;
      episode.notes.push_back(note);
    }
  }

  // --- Voice 2: Diminished motif (rhythmic contrast) ---
  if (num_voices >= 3) {
    auto diminished = diminishMelody(motif, start_tick + motif_dur);
    for (auto& note : diminished) {
      note.voice = 2;
      episode.notes.push_back(note);
    }
  }

  // --- Apply gradual key modulation ---
  // Transpose the second half of all notes toward the target key.
  if (key_diff != 0) {
    Tick midpoint = start_tick + duration_ticks / 2;
    for (auto& note : episode.notes) {
      if (note.start_tick >= midpoint) {
        int new_pitch = static_cast<int>(note.pitch) + key_diff;
        note.pitch = clampMidiPitch(new_pitch);
      }
    }
  }

  return episode;
}

}  // namespace bach
