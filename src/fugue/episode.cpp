// Fugue episode: modulatory development of subject material.

#include "fugue/episode.h"

#include <algorithm>
#include <cstdint>
#include <random>

#include "core/rng_util.h"
#include "core/scale.h"
#include "fugue/fugue_config.h"
#include "transform/motif_transform.h"
#include "transform/sequence.h"

namespace bach {

namespace {

/// @brief Determine the sequence degree step based on subject character.
///
/// Returns a diatonic degree step (not semitones). Standard Baroque practice
/// is descending by one scale degree (-1). Playful/Restless may use -2.
///
/// @param character Subject character influencing episode style.
/// @param rng Mersenne Twister instance for Playful/Restless randomization.
/// @return Degree step (typically -1 for stepwise descending sequence).
int sequenceDegreeStepForCharacter(SubjectCharacter character, std::mt19937& rng) {
  switch (character) {
    case SubjectCharacter::Severe:
      return -1;  // Strict descending by step (standard Baroque Zeugma)
    case SubjectCharacter::Playful:
      return rng::rollRange(rng, -2, -1);  // Step or skip
    case SubjectCharacter::Noble:
      return -1;  // Moderate, stepwise
    case SubjectCharacter::Restless:
      return rng::rollRange(rng, -2, -1);  // Varied
    default:
      return -1;
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

/// @brief Apply invertible counterpoint by swapping voice 0 and voice 1 IDs.
///
/// This implements double counterpoint at the octave: material originally in
/// the upper voice moves to the lower voice and vice versa, a standard
/// Baroque compositional technique used in recurring episodes.
///
/// @param notes Episode notes to modify in place.
void applyInvertibleCounterpoint(std::vector<NoteEvent>& notes) {
  for (auto& note : notes) {
    if (note.voice == 0) {
      note.voice = 1;
    } else if (note.voice == 1) {
      note.voice = 0;
    }
  }
}

/// @brief Generate voice 0 and voice 1 for Severe character.
///
/// Severe episodes use strict sequential patterns: the original motif in
/// voice 0 with a diatonically inverted imitation in voice 1.
///
/// @param episode Episode to append notes to.
/// @param motif Normalized motif.
/// @param start_tick Episode start tick.
/// @param motif_dur Duration of the motif.
/// @param deg_step Diatonic degree step for sequence.
/// @param seq_reps Number of sequence repetitions.
/// @param imitation_offset Offset for voice 1 entry.
/// @param num_voices Number of active voices.
/// @param key Musical key for diatonic operations.
/// @param scale Scale type for diatonic operations.
void generateSevereEpisode(Episode& episode, const std::vector<NoteEvent>& motif,
                           Tick start_tick, Tick motif_dur, int deg_step,
                           int seq_reps, Tick imitation_offset, uint8_t num_voices,
                           Key key, ScaleType scale) {
  // Voice 0: Original motif + diatonic sequence.
  for (const auto& note : motif) {
    NoteEvent placed = note;
    placed.start_tick += start_tick;
    placed.voice = 0;
    episode.notes.push_back(placed);
  }
  auto seq_notes =
      generateDiatonicSequence(motif, seq_reps, deg_step, start_tick + motif_dur, key, scale);
  for (auto& note : seq_notes) {
    note.voice = 0;
    episode.notes.push_back(note);
  }

  // Voice 1: Diatonic inverted imitation + diatonic sequence.
  if (num_voices >= 2) {
    uint8_t pivot = motif[0].pitch;
    auto inverted = invertMelodyDiatonic(motif, pivot, key, scale);
    Tick voice1_start = start_tick + imitation_offset;
    for (const auto& note : inverted) {
      NoteEvent placed = note;
      placed.start_tick += voice1_start;
      placed.voice = 1;
      episode.notes.push_back(placed);
    }
    int inv_reps = std::max(1, seq_reps - 1);
    auto inv_seq = generateDiatonicSequence(inverted, inv_reps, deg_step,
                                            voice1_start + motif_dur, key, scale);
    for (auto& note : inv_seq) {
      note.voice = 1;
      episode.notes.push_back(note);
    }
  }
}

/// @brief Generate voice 0 and voice 1 for Playful character.
///
/// Playful episodes use retrograde of the motif for voice 0, and diminished
/// fragments of the motif for voice 1 (contrasting rhythmic energy).
///
/// @param episode Episode to append notes to.
/// @param motif Normalized motif.
/// @param start_tick Episode start tick.
/// @param motif_dur Duration of the motif.
/// @param deg_step Diatonic degree step for sequence.
/// @param seq_reps Number of sequence repetitions.
/// @param imitation_offset Offset for voice 1 entry.
/// @param num_voices Number of active voices.
/// @param key Musical key for diatonic operations.
/// @param scale Scale type for diatonic operations.
void generatePlayfulEpisode(Episode& episode, const std::vector<NoteEvent>& motif,
                            Tick start_tick, Tick motif_dur, int deg_step,
                            int seq_reps, Tick imitation_offset, uint8_t num_voices,
                            Key key, ScaleType scale) {
  // Voice 0: Retrograde of motif + diatonic sequence of retrograde.
  auto retrograde = retrogradeMelody(motif, start_tick);
  for (auto& note : retrograde) {
    note.voice = 0;
    episode.notes.push_back(note);
  }
  auto seq_notes = generateDiatonicSequence(retrograde, seq_reps, deg_step,
                                            start_tick + motif_dur, key, scale);
  for (auto& note : seq_notes) {
    note.voice = 0;
    episode.notes.push_back(note);
  }

  // Voice 1: Diminished fragments for rhythmic contrast.
  if (num_voices >= 2) {
    Tick voice1_start = start_tick + imitation_offset;
    auto diminished = diminishMelody(motif, voice1_start);
    for (auto& note : diminished) {
      note.voice = 1;
      episode.notes.push_back(note);
    }
    // Diatonic sequence of diminished motif (fits more reps due to shorter duration).
    Tick dim_dur = motifDuration(diminished);
    if (dim_dur == 0) dim_dur = motif_dur / 2;
    int dim_reps = calculateSequenceRepetitions(motif_dur * 2, dim_dur);
    auto dim_seq = generateDiatonicSequence(diminished, dim_reps, deg_step,
                                            voice1_start + dim_dur, key, scale);
    for (auto& note : dim_seq) {
      note.voice = 1;
      episode.notes.push_back(note);
    }
  }
}

/// @brief Generate voice 0 and voice 1 for Noble character.
///
/// Noble episodes use the augmented motif (doubled duration) in the lowest
/// voice, creating a stately bass foundation, with the original motif
/// providing melodic interest in the upper voice.
///
/// @param episode Episode to append notes to.
/// @param motif Normalized motif.
/// @param start_tick Episode start tick.
/// @param motif_dur Duration of the motif.
/// @param deg_step Diatonic degree step for sequence.
/// @param seq_reps Number of sequence repetitions.
/// @param imitation_offset Offset for voice 1 entry.
/// @param num_voices Number of active voices.
/// @param key Musical key for diatonic operations.
/// @param scale Scale type for diatonic operations.
void generateNobleEpisode(Episode& episode, const std::vector<NoteEvent>& motif,
                          Tick start_tick, Tick motif_dur, int deg_step,
                          int seq_reps, Tick imitation_offset, uint8_t num_voices,
                          Key key, ScaleType scale) {
  // Voice 0: Original motif + diatonic sequence (upper melodic line).
  for (const auto& note : motif) {
    NoteEvent placed = note;
    placed.start_tick += start_tick;
    placed.voice = 0;
    episode.notes.push_back(placed);
  }
  auto seq_notes =
      generateDiatonicSequence(motif, seq_reps, deg_step, start_tick + motif_dur, key, scale);
  for (auto& note : seq_notes) {
    note.voice = 0;
    episode.notes.push_back(note);
  }

  // Voice 1: Augmented motif in the bass (doubled duration for stately motion).
  if (num_voices >= 2) {
    Tick voice1_start = start_tick + imitation_offset;
    auto augmented = augmentMelody(motif, voice1_start);
    // Transpose down an octave for bass register placement.
    augmented = transposeMelody(augmented, -12);
    for (auto& note : augmented) {
      note.voice = 1;
      episode.notes.push_back(note);
    }
  }
}

/// @brief Generate voice 0 and voice 1 for Restless character.
///
/// Restless episodes fragment the motif into 2 pieces and use overlapping
/// imitation with a shorter offset, creating rhythmic urgency.
///
/// @param episode Episode to append notes to.
/// @param motif Normalized motif.
/// @param start_tick Episode start tick.
/// @param motif_dur Duration of the motif.
/// @param deg_step Diatonic degree step for sequence.
/// @param seq_reps Number of sequence repetitions.
/// @param imitation_offset Offset for voice 1 entry.
/// @param num_voices Number of active voices.
/// @param key Musical key for diatonic operations.
/// @param scale Scale type for diatonic operations.
void generateRestlessEpisode(Episode& episode, const std::vector<NoteEvent>& motif,
                             Tick start_tick, Tick motif_dur, int deg_step,
                             int seq_reps, Tick imitation_offset, uint8_t num_voices,
                             Key key, ScaleType scale) {
  // Fragment the motif into 2 pieces for tight imitation.
  auto fragments = fragmentMotif(motif, 2);

  // Voice 0: First fragment + diatonic sequence.
  std::vector<NoteEvent> frag0 = fragments.empty() ? motif : fragments[0];
  normalizeMotifToZero(frag0);
  Tick frag0_dur = motifDuration(frag0);
  if (frag0_dur == 0) frag0_dur = motif_dur / 2;

  for (const auto& note : frag0) {
    NoteEvent placed = note;
    placed.start_tick += start_tick;
    placed.voice = 0;
    episode.notes.push_back(placed);
  }
  // More repetitions since fragments are shorter.
  int frag_reps = calculateSequenceRepetitions(motif_dur * 2, frag0_dur);
  auto seq_notes =
      generateDiatonicSequence(frag0, frag_reps, deg_step, start_tick + frag0_dur, key, scale);
  for (auto& note : seq_notes) {
    note.voice = 0;
    episode.notes.push_back(note);
  }

  // Voice 1: Second fragment (or diatonically inverted first) with tight overlap.
  if (num_voices >= 2) {
    std::vector<NoteEvent> frag1;
    if (fragments.size() >= 2) {
      frag1 = fragments[1];
    } else {
      // Fallback: diatonic inversion of the first fragment.
      uint8_t pivot = frag0.empty() ? kMidiC4 : frag0[0].pitch;
      frag1 = invertMelodyDiatonic(frag0, pivot, key, scale);
    }
    normalizeMotifToZero(frag1);
    Tick frag1_dur = motifDuration(frag1);
    if (frag1_dur == 0) frag1_dur = frag0_dur;

    // Restless uses tighter imitation offset (already computed).
    Tick voice1_start = start_tick + imitation_offset;
    for (const auto& note : frag1) {
      NoteEvent placed = note;
      placed.start_tick += voice1_start;
      placed.voice = 1;
      episode.notes.push_back(placed);
    }
    int frag1_reps = calculateSequenceRepetitions(motif_dur * 2, frag1_dur);
    auto frag1_seq = generateDiatonicSequence(frag1, frag1_reps, deg_step,
                                              voice1_start + frag1_dur, key, scale);
    for (auto& note : frag1_seq) {
      note.voice = 1;
      episode.notes.push_back(note);
    }
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

std::vector<NoteEvent> extractTailMotif(const std::vector<NoteEvent>& notes, size_t num_notes) {
  if (num_notes >= notes.size()) return notes;
  return std::vector<NoteEvent>(notes.end() - static_cast<int>(num_notes), notes.end());
}

std::vector<std::vector<NoteEvent>> fragmentMotif(const std::vector<NoteEvent>& notes,
                                                   size_t num_fragments) {
  std::vector<std::vector<NoteEvent>> fragments;
  if (num_fragments == 0 || notes.empty()) return fragments;
  size_t frag_size = notes.size() / num_fragments;
  if (frag_size == 0) frag_size = 1;
  for (size_t idx = 0; idx < notes.size(); idx += frag_size) {
    size_t end = std::min(idx + frag_size, notes.size());
    fragments.emplace_back(notes.begin() + static_cast<int>(idx),
                           notes.begin() + static_cast<int>(end));
    if (fragments.size() >= num_fragments) break;
  }
  return fragments;
}

Episode generateEpisode(const Subject& subject, Tick start_tick, Tick duration_ticks,
                        Key start_key, Key target_key, uint8_t num_voices, uint32_t seed,
                        int episode_index, float energy_level) {
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
  int deg_step = sequenceDegreeStepForCharacter(subject.character, rng);
  int seq_reps = calculateSequenceRepetitions(duration_ticks, motif_dur);
  Tick imitation_offset = imitationOffsetForCharacter(motif_dur, subject.character);

  // Diatonic context: use major scale for the starting key.
  ScaleType scale = ScaleType::Major;

  // Calculate transposition for key modulation.
  int key_diff = static_cast<int>(target_key) - static_cast<int>(start_key);

  // --- Character-specific voice generation ---
  switch (subject.character) {
    case SubjectCharacter::Playful:
      generatePlayfulEpisode(episode, motif, start_tick, motif_dur, deg_step, seq_reps,
                             imitation_offset, num_voices, start_key, scale);
      break;
    case SubjectCharacter::Noble:
      generateNobleEpisode(episode, motif, start_tick, motif_dur, deg_step, seq_reps,
                           imitation_offset, num_voices, start_key, scale);
      break;
    case SubjectCharacter::Restless:
      generateRestlessEpisode(episode, motif, start_tick, motif_dur, deg_step, seq_reps,
                              imitation_offset, num_voices, start_key, scale);
      break;
    case SubjectCharacter::Severe:
    default:
      generateSevereEpisode(episode, motif, start_tick, motif_dur, deg_step, seq_reps,
                            imitation_offset, num_voices, start_key, scale);
      break;
  }

  // --- Voice 2: Diminished motif (rhythmic contrast, shared across characters) ---
  if (num_voices >= 3) {
    auto diminished = diminishMelody(motif, start_tick + motif_dur);
    for (auto& note : diminished) {
      note.voice = 2;
      episode.notes.push_back(note);
    }
  }

  // --- Voice 3: Augmented tail motif as slow bass foundation (4+ voices) ---
  if (num_voices >= 4) {
    auto tail = extractTailMotif(subject.notes, 3);
    if (!tail.empty()) {
      normalizeMotifToZero(tail);
      auto augmented = augmentMelody(tail, start_tick);
      augmented = transposeMelody(augmented, -12);
      for (auto& note : augmented) {
        note.voice = 3;
        episode.notes.push_back(note);
      }
    }
  }

  // --- Voice 4: Diatonically inverted tail motif with sequence (5 voices) ---
  if (num_voices >= 5) {
    auto tail = extractTailMotif(subject.notes, 3);
    if (!tail.empty()) {
      normalizeMotifToZero(tail);
      uint8_t pivot = tail[0].pitch;
      auto inverted = invertMelodyDiatonic(tail, pivot, start_key, scale);
      Tick voice4_start = start_tick + imitation_offset;
      for (const auto& note : inverted) {
        NoteEvent placed = note;
        placed.start_tick += voice4_start;
        placed.voice = 4;
        episode.notes.push_back(placed);
      }
      Tick inv_dur = motifDuration(inverted);
      if (inv_dur == 0) inv_dur = motif_dur;
      int inv_reps = std::max(1, seq_reps - 1);
      auto inv_seq = generateDiatonicSequence(inverted, inv_reps, deg_step,
                                              voice4_start + inv_dur, start_key, scale);
      for (auto& note : inv_seq) {
        note.voice = 4;
        episode.notes.push_back(note);
      }
    }
  }

  // --- Apply gradual key modulation ---
  // Transpose the second half of all notes toward the target key, then snap to
  // the target key's diatonic scale to avoid chromatic artifacts.
  if (key_diff != 0) {
    Tick midpoint = start_tick + duration_ticks / 2;
    for (auto& note : episode.notes) {
      if (note.start_tick >= midpoint) {
        int new_pitch = static_cast<int>(note.pitch) + key_diff;
        note.pitch = scale_util::nearestScaleTone(clampMidiPitch(new_pitch), target_key, scale);
      }
    }
  }

  // --- Apply energy-based rhythm density floor ---
  // Clamp note durations to the minimum allowed by the current energy level.
  Tick min_dur = FugueEnergyCurve::minDuration(energy_level);
  for (auto& note : episode.notes) {
    if (note.duration < min_dur) {
      note.duration = min_dur;
    }
  }

  // --- Invertible counterpoint for odd-indexed episodes ---
  // Swap voice 0 and voice 1 material (double counterpoint at the octave).
  if (episode_index % 2 != 0) {
    applyInvertibleCounterpoint(episode.notes);
  }

  return episode;
}

std::vector<NoteEvent> extractCharacteristicMotif(const Subject& subject,
                                                   size_t motif_length) {
  if (subject.notes.size() <= motif_length) {
    return subject.notes;
  }

  float best_score = -1.0f;
  size_t best_start = 0;

  size_t window_count = subject.notes.size() - motif_length + 1;
  for (size_t start = 0; start < window_count; ++start) {
    float score = 0.0f;

    // Rhythmic diversity: count distinct durations in window.
    std::vector<Tick> durations;
    for (size_t idx = start; idx < start + motif_length; ++idx) {
      bool found = false;
      for (Tick dur : durations) {
        if (dur == subject.notes[idx].duration) {
          found = true;
          break;
        }
      }
      if (!found) durations.push_back(subject.notes[idx].duration);
    }
    score += 0.3f * static_cast<float>(durations.size()) /
             static_cast<float>(motif_length);

    // Intervallic interest: contains a leap (>= 3 semitones).
    bool has_leap = false;
    for (size_t idx = start + 1; idx < start + motif_length; ++idx) {
      int ivl = std::abs(static_cast<int>(subject.notes[idx].pitch) -
                          static_cast<int>(subject.notes[idx - 1].pitch));
      if (ivl >= 3) {
        has_leap = true;
        break;
      }
    }
    if (has_leap) score += 0.3f;

    // Proximity to opening.
    float proximity = 1.0f - static_cast<float>(start) / static_cast<float>(window_count);
    score += 0.2f * proximity;

    // Tonal stability: contains root (pitch class 0 in subject context).
    uint8_t root_pc = subject.notes[0].pitch % 12;
    bool has_root = false;
    for (size_t idx = start; idx < start + motif_length; ++idx) {
      if (subject.notes[idx].pitch % 12 == root_pc) {
        has_root = true;
        break;
      }
    }
    if (has_root) score += 0.2f;

    if (score > best_score) {
      best_score = score;
      best_start = start;
    }
  }

  return std::vector<NoteEvent>(subject.notes.begin() + best_start,
                                 subject.notes.begin() + best_start + motif_length);
}

}  // namespace bach
