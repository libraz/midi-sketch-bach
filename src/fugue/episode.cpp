// Fugue episode: modulatory development of subject material.

#include "fugue/episode.h"

#include <algorithm>
#include <cstdint>
#include <random>

#include "core/note_creator.h"
#include "core/pitch_utils.h"
#include "core/rng_util.h"
#include "core/scale.h"
#include "counterpoint/collision_resolver.h"
#include "counterpoint/counterpoint_state.h"
#include "counterpoint/i_rule_evaluator.h"
#include "counterpoint/melodic_context.h"
#include "fugue/fortspinnung.h"
#include "fugue/fugue_config.h"
#include "fugue/motif_pool.h"
#include "fugue/voice_registers.h"
#include "harmony/chord_tone_utils.h"
#include "harmony/chord_types.h"
#include "harmony/harmonic_timeline.h"
#include "harmony/key.h"
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
      return rng::rollProbability(rng, 0.85f) ? -1 : -2;  // Mostly stepwise, occasional skip
    case SubjectCharacter::Playful:
      return rng::rollRange(rng, -2, -1);  // Step or skip
    case SubjectCharacter::Noble:
      return rng::rollProbability(rng, 0.90f) ? -1 : -2;  // Stately stepwise, rare skip
    case SubjectCharacter::Restless:
      return rng::rollRange(rng, -2, -1);  // Varied
    default:
      return -1;
  }
}

/// @brief Determine the imitation offset for the second voice.
///
/// In Baroque fugue episodes, voices enter in staggered imitation (dialogic
/// hand-off). The offset is character-specific to match the rhetorical pacing:
///   - Severe/Noble: ~1.5-2.5 beat delay (stately, measured discourse)
///   - Playful/Restless: ~0.5-1.5 beat delay (tighter, more energetic exchange)
///
/// @param motif_dur Duration of the motif in ticks (unused, kept for API stability).
/// @param character Subject character.
/// @param rng Mersenne Twister instance for offset randomization.
/// @return Imitation offset in ticks.
Tick imitationOffsetForCharacter(Tick motif_dur, SubjectCharacter character, std::mt19937& rng) {
  (void)motif_dur;  // Retained for API compatibility; offsets are now RNG-driven per character.
  switch (character) {
    case SubjectCharacter::Severe:
      return static_cast<Tick>(rng::rollFloat(rng, 1.5f, 2.5f) * kTicksPerBeat);
    case SubjectCharacter::Playful:
      return static_cast<Tick>(rng::rollFloat(rng, 0.5f, 1.5f) * kTicksPerBeat);
    case SubjectCharacter::Noble:
      return static_cast<Tick>(rng::rollFloat(rng, 1.5f, 2.5f) * kTicksPerBeat);
    case SubjectCharacter::Restless:
      return static_cast<Tick>(rng::rollFloat(rng, 0.5f, 1.5f) * kTicksPerBeat);
    default:
      return static_cast<Tick>(rng::rollFloat(rng, 1.5f, 2.5f) * kTicksPerBeat);
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

/// @brief Shift notes to fit within the appropriate voice register.
///
/// Calculates the average pitch of the notes, compares it to the center
/// of the target voice's range (from getFugueVoiceRange()), and shifts
/// all notes by the nearest whole octave. Final pitches are clamped to
/// the voice range.
///
/// @param notes Notes to adjust (modified in place).
/// @param voice_id Target voice.
/// @param num_voices Total voices in the fugue.
static void fitToVoiceRegister(std::vector<NoteEvent>& notes, VoiceId voice_id,
                               uint8_t num_voices) {
  if (notes.empty()) return;

  auto [range_low, range_high] = getFugueVoiceRange(voice_id, num_voices);
  int range_center = (static_cast<int>(range_low) + static_cast<int>(range_high)) / 2;

  // Calculate average pitch of notes.
  int sum = 0;
  for (const auto& note : notes) {
    sum += static_cast<int>(note.pitch);
  }
  int avg_pitch = sum / static_cast<int>(notes.size());

  // Round shift to nearest octave (positive or negative).
  int diff = range_center - avg_pitch;
  int octave_shift = 0;
  octave_shift = nearestOctaveShift(diff);

  // Apply shift and clamp to voice range.
  for (auto& note : notes) {
    int new_pitch = static_cast<int>(note.pitch) + octave_shift;
    if (new_pitch < static_cast<int>(range_low)) new_pitch = static_cast<int>(range_low);
    if (new_pitch > static_cast<int>(range_high)) new_pitch = static_cast<int>(range_high);
    note.pitch = static_cast<uint8_t>(new_pitch);
  }
}

}  // namespace (anonymous -- reopened below after shared helpers)



/// @brief Normalize a motif so that the first note starts at tick 0.
/// @param motif Input motif (modified in place).
static void normalizeMotifToZero(std::vector<NoteEvent>& motif) {
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
static void applyInvertibleCounterpoint(std::vector<NoteEvent>& notes) {
  for (auto& note : notes) {
    if (note.voice == 0) {
      note.voice = 1;
    } else if (note.voice == 1) {
      note.voice = 0;
    }
  }
}

namespace {  // NOLINT(google-build-namespaces) reopened for character-specific generators

/// @brief Generate voice 0 and voice 1 for Severe character.
///
/// Severe episodes use strict sequential patterns: the original motif in
/// voice 0 with direct (uninverted) Kopfmotiv imitation in voice 1 at a
/// 2-beat delay. This dialogic hand-off preserves the subject's identity
/// in both voices (Baroque "strict" imitation practice).
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
  std::vector<NoteEvent> v0_notes;
  for (const auto& note : motif) {
    NoteEvent placed = note;
    placed.start_tick += start_tick;
    placed.voice = 0;
    v0_notes.push_back(placed);
  }
  auto seq_notes =
      generateDiatonicSequence(motif, seq_reps, deg_step, start_tick + motif_dur, key, scale);
  for (auto& note : seq_notes) {
    note.source = BachNoteSource::SequenceNote;
  }
  for (auto& note : seq_notes) {
    note.voice = 0;
    v0_notes.push_back(note);
  }
  fitToVoiceRegister(v0_notes, 0, num_voices);
  for (auto& note : v0_notes) {
    episode.notes.push_back(note);
  }

  // Voice 1: Diatonic inversion of Kopfmotiv for contour independence with
  // diatonic sequence. Severe character uses strict contrary motion where
  // voice 1 inverts the motif at a 2-beat delay for melodic contrast.
  if (num_voices >= 2) {
    std::vector<NoteEvent> v1_notes;
    Tick voice1_start = start_tick + imitation_offset;
    // Diatonic inversion for contour independence (strict contrary motion).
    uint8_t pivot = motif.empty() ? 60 : motif[0].pitch;
    auto inverted = invertMelodyDiatonic(motif, pivot, key, scale);
    for (auto& note : inverted) {
      note.start_tick += voice1_start;
      note.voice = 1;
      v1_notes.push_back(note);
    }
    int inv_reps = std::max(1, seq_reps - 1);
    auto seq = generateDiatonicSequence(inverted, inv_reps, deg_step,
                                        voice1_start + motif_dur, key, scale);
    for (auto& note : seq) {
      note.source = BachNoteSource::SequenceNote;
    }
    for (auto& note : seq) {
      note.voice = 1;
      v1_notes.push_back(note);
    }
    fitToVoiceRegister(v1_notes, 1, num_voices);
    for (auto& note : v1_notes) {
      episode.notes.push_back(note);
    }
  }
}

/// @brief Generate voice 0 and voice 1 for Playful character.
///
/// Playful episodes use retrograde of the motif for voice 0, and a
/// diatonically inverted Kopfmotiv in voice 1 at a 1-beat delay.
/// The inversion against the retrograde creates melodic contrast while
/// maintaining motivic coherence in the dialogic hand-off.
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
  std::vector<NoteEvent> v0_notes;
  auto retrograde = retrogradeMelody(motif, start_tick);
  for (auto& note : retrograde) {
    note.voice = 0;
    v0_notes.push_back(note);
  }
  auto seq_notes = generateDiatonicSequence(retrograde, seq_reps, deg_step,
                                            start_tick + motif_dur, key, scale);
  for (auto& note : seq_notes) {
    note.source = BachNoteSource::SequenceNote;
  }
  for (auto& note : seq_notes) {
    note.voice = 0;
    v0_notes.push_back(note);
  }
  fitToVoiceRegister(v0_notes, 0, num_voices);
  for (auto& note : v0_notes) {
    episode.notes.push_back(note);
  }

  // Voice 1: Diatonic inversion of Kopfmotiv with tighter imitation.
  // Playful character uses inverted hand-off for melodic contrast against
  // the retrograde voice 0 material.
  if (num_voices >= 2) {
    std::vector<NoteEvent> v1_notes;
    uint8_t pivot = motif[0].pitch;
    auto inverted = invertMelodyDiatonic(motif, pivot, key, scale);
    Tick voice1_start = start_tick + imitation_offset;
    for (const auto& note : inverted) {
      NoteEvent placed = note;
      placed.start_tick += voice1_start;
      placed.voice = 1;
      v1_notes.push_back(placed);
    }
    int inv_reps = std::max(1, seq_reps - 1);
    auto inv_seq = generateDiatonicSequence(inverted, inv_reps, deg_step,
                                            voice1_start + motif_dur, key, scale);
    for (auto& note : inv_seq) {
      note.source = BachNoteSource::SequenceNote;
    }
    for (auto& note : inv_seq) {
      note.voice = 1;
      v1_notes.push_back(note);
    }
    fitToVoiceRegister(v1_notes, 1, num_voices);
    for (auto& note : v1_notes) {
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
  std::vector<NoteEvent> v0_notes;
  for (const auto& note : motif) {
    NoteEvent placed = note;
    placed.start_tick += start_tick;
    placed.voice = 0;
    v0_notes.push_back(placed);
  }
  auto seq_notes =
      generateDiatonicSequence(motif, seq_reps, deg_step, start_tick + motif_dur, key, scale);
  for (auto& note : seq_notes) {
    note.source = BachNoteSource::SequenceNote;
  }
  for (auto& note : seq_notes) {
    note.voice = 0;
    v0_notes.push_back(note);
  }
  fitToVoiceRegister(v0_notes, 0, num_voices);
  for (auto& note : v0_notes) {
    episode.notes.push_back(note);
  }

  // Voice 1: Retrograde + augmented motif in the bass (reversed stately motion).
  if (num_voices >= 2) {
    std::vector<NoteEvent> v1_notes;
    Tick voice1_start = start_tick + imitation_offset;
    // Retrograde + augmentation for contour independence (reversed stately motion).
    auto retrograded = retrogradeMelody(motif, 0);
    auto augmented = augmentMelody(retrograded, voice1_start);
    // Transpose down an octave for bass register placement.
    augmented = transposeMelody(augmented, -12);
    for (auto& note : augmented) {
      note.voice = 1;
      v1_notes.push_back(note);
    }
    fitToVoiceRegister(v1_notes, 1, num_voices);
    for (auto& note : v1_notes) {
      episode.notes.push_back(note);
    }
  }
}

/// @brief Generate voice 0 and voice 1 for Restless character.
///
/// Restless episodes fragment the motif into 2 pieces for voice 0, and use
/// a diminished (half-speed) Kopfmotiv in voice 1 at a 1-beat delay. The
/// compressed imitation creates rhythmic urgency and overlapping density.
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

  std::vector<NoteEvent> v0_notes;
  for (const auto& note : frag0) {
    NoteEvent placed = note;
    placed.start_tick += start_tick;
    placed.voice = 0;
    v0_notes.push_back(placed);
  }
  // More repetitions since fragments are shorter, capped by overall sequence structure.
  int frag_reps = std::min(
      calculateSequenceRepetitions(motif_dur * 2, frag0_dur),
      seq_reps + 1);
  auto seq_notes =
      generateDiatonicSequence(frag0, frag_reps, deg_step, start_tick + frag0_dur, key, scale);
  for (auto& note : seq_notes) {
    note.source = BachNoteSource::SequenceNote;
  }
  for (auto& note : seq_notes) {
    note.voice = 0;
    v0_notes.push_back(note);
  }
  fitToVoiceRegister(v0_notes, 0, num_voices);
  for (auto& note : v0_notes) {
    episode.notes.push_back(note);
  }

  // Voice 1: Diminished Kopfmotiv (half speed) for rhythmic urgency.
  // Restless character uses diminution to compress the motif, creating
  // overlapping imitation with tighter rhythmic density.
  if (num_voices >= 2) {
    std::vector<NoteEvent> v1_notes;
    Tick voice1_start = start_tick + imitation_offset;
    auto diminished = diminishMelody(motif, voice1_start);
    for (auto& note : diminished) {
      note.voice = 1;
      v1_notes.push_back(note);
    }
    Tick dim_dur = motifDuration(diminished);
    if (dim_dur == 0) dim_dur = motif_dur / 2;
    int dim_reps = std::min(
        calculateSequenceRepetitions(motif_dur * 2, dim_dur),
        seq_reps + 1);
    auto dim_seq = generateDiatonicSequence(diminished, dim_reps, deg_step,
                                            voice1_start + dim_dur, key, scale);
    for (auto& note : dim_seq) {
      note.source = BachNoteSource::SequenceNote;
    }
    for (auto& note : dim_seq) {
      note.voice = 1;
      v1_notes.push_back(note);
    }
    fitToVoiceRegister(v1_notes, 1, num_voices);
    for (auto& note : v1_notes) {
      episode.notes.push_back(note);
    }
  }
}

/// @brief Select which voice should "rest" (hold long tones) in this episode.
///
/// Rotates through voices 2+ based on episode_index, ensuring variety across
/// successive episodes. Never selects voice 0 or 1 (they carry the primary
/// motivic material in the sequence/imitation texture).
///
/// @param num_voices Total voices (must be >= 3 for a resting voice to exist).
/// @param episode_index Episode ordinal for rotation.
/// @return Voice ID of the resting voice, or num_voices if no resting voice.
static VoiceId selectRestingVoice(uint8_t num_voices, int episode_index) {
  if (num_voices < 3) return num_voices;  // No resting voice possible
  // Bass (lowest voice = num_voices-1) never rests — it provides harmonic foundation.
  // Rotate through voices 0..(num_voices-2) only.
  uint8_t rotatable = num_voices - 1;
  if (rotatable < 2) return num_voices;  // Only bass exists beyond 0/1.
  return static_cast<VoiceId>(episode_index % rotatable);
}

/// @brief Generate sustained held tones for a resting voice.
///
/// Creates whole-note held tones that alternate between the tonic and
/// the fifth of the current key. The pitch is derived from the center
/// of the target voice's register (via getFugueVoiceRange()), ensuring
/// proper vertical separation. These long tones provide a harmonic
/// foundation while reducing textural density, a standard Baroque
/// episode technique for creating breathing space.
///
/// @param episode Episode to append notes to.
/// @param start_tick Episode start tick.
/// @param duration_ticks Total episode duration.
/// @param key Current key context.
/// @param voice_id Voice ID for the held tones.
/// @param num_voices Total number of voices in the fugue.
static void generateHeldTones(Episode& episode,
                               Tick start_tick, Tick duration_ticks,
                               Key key, VoiceId voice_id, uint8_t num_voices) {
  constexpr Tick kHeldDuration = kTicksPerBar;

  auto [range_low, range_high] = getFugueVoiceRange(voice_id, num_voices);
  int register_center = (static_cast<int>(range_low) + static_cast<int>(range_high)) / 2;

  // Snap to nearest tonic pitch class in the voice register center.
  int key_pc = static_cast<int>(key);
  int center_pc = register_center % 12;
  int pc_diff = key_pc - center_pc;
  if (pc_diff > 6) pc_diff -= 12;
  if (pc_diff < -6) pc_diff += 12;
  int current_pitch = register_center + pc_diff;

  // Bass progression: I-IV-V-I pattern (root motion by 4th/5th, Bach standard).
  // Scale degree offsets relative to tonic (in semitones).
  static constexpr int kBassProgression[] = {0, 5, 7, 0, 4, 5, 7, 0};
  static constexpr int kProgressionLen = 8;

  Tick tick = start_tick;
  int idx = 0;
  while (tick < start_tick + duration_ticks) {
    Tick dur = std::min(kHeldDuration, start_tick + duration_ticks - tick);
    if (dur == 0) break;

    int degree_offset = kBassProgression[idx % kProgressionLen];
    int pitch = current_pitch + degree_offset;
    // Keep within voice range.
    while (pitch < static_cast<int>(range_low)) pitch += 12;
    while (pitch > static_cast<int>(range_high)) pitch -= 12;

    NoteEvent note;
    note.start_tick = tick;
    note.duration = dur;
    note.pitch = static_cast<uint8_t>(pitch);
    note.velocity = 80;
    note.voice = voice_id;
    note.source = BachNoteSource::EpisodeMaterial;
    episode.notes.push_back(note);

    tick += dur;
    ++idx;
  }
}

/// @brief Generate bass sequence pattern for episode sections.
///
/// Creates 2-4 note stepwise/arpeggiated patterns with strict leap limits,
/// suitable for bass voice episodic material. Uses I-IV-V-I harmonic
/// foundation with shorter note values than held tones.
///
/// @param subject Source subject for motivic reference.
/// @param start_tick Start position.
/// @param duration Duration of the episode section.
/// @param key Current key context.
/// @param scale Scale type.
/// @param voice Target voice ID.
/// @param num_voices Total number of voices.
/// @param gen RNG engine.
/// @return Vector of bass sequence notes.
static std::vector<NoteEvent> generateBassSequencePattern(
    const Subject& /* subject */, Tick start_tick, Tick duration,
    Key key, ScaleType scale, VoiceId voice, uint8_t num_voices,
    std::mt19937& /* gen */) {
  std::vector<NoteEvent> result;
  auto [range_low, range_high] = getFugueVoiceRange(voice, num_voices);

  // Build bass pattern from stepwise motion + arpeggiated figures.
  int key_pc = static_cast<int>(key);
  int register_center =
      (static_cast<int>(range_low) + static_cast<int>(range_high)) / 2;
  int center_pc = register_center % 12;
  int pc_diff = key_pc - center_pc;
  if (pc_diff > 6) pc_diff -= 12;
  if (pc_diff < -6) pc_diff += 12;
  int bass_root = register_center + pc_diff;

  // Pattern: 2-bar unit with half-note bass motion followed by quarter-note
  // passing tones. Alternates between held tonic and moving patterns.
  constexpr Tick kHalfNote = kTicksPerBeat * 2;
  constexpr Tick kQuarterNote = kTicksPerBeat;
  constexpr int kMaxBassLeap = 5;  // P4 max for bass

  // Bass degree offsets for a 4-note sequential pattern (I-down-down-up).
  static constexpr int kBassPatternA[] = {0, -1, -2, -1};
  static constexpr int kBassPatternB[] = {0, 2, 1, 0};
  static constexpr int kPatternLen = 4;

  int abs_tonic = scale_util::pitchToAbsoluteDegree(
      static_cast<uint8_t>(std::max(0, std::min(127, bass_root))), key, scale);

  Tick tick = start_tick;
  int pattern_idx = 0;
  int sequence_offset = 0;  // Descending by 1 degree per 2-bar unit.

  while (tick < start_tick + duration) {
    const int* pattern =
        (pattern_idx % 2 == 0) ? kBassPatternA : kBassPatternB;

    for (int i = 0; i < kPatternLen && tick < start_tick + duration; ++i) {
      Tick dur = (i == 0) ? kHalfNote : kQuarterNote;
      if (tick + dur > start_tick + duration) {
        dur = start_tick + duration - tick;
        if (dur < kQuarterNote / 2) break;
      }

      int degree = abs_tonic + sequence_offset + pattern[i];
      int pitch = static_cast<int>(
          scale_util::absoluteDegreeToPitch(degree, key, scale));

      // Enforce bass leap limit.
      if (!result.empty()) {
        int prev = static_cast<int>(result.back().pitch);
        if (std::abs(pitch - prev) > kMaxBassLeap) {
          int dir = (pitch > prev) ? 1 : -1;
          pitch = prev + dir * kMaxBassLeap;
          pitch = static_cast<int>(scale_util::nearestScaleTone(
              static_cast<uint8_t>(std::max(0, std::min(127, pitch))),
              key, scale));
        }
      }

      // Keep within voice range.
      while (pitch < static_cast<int>(range_low)) pitch += 12;
      while (pitch > static_cast<int>(range_high)) pitch -= 12;

      NoteEvent note;
      note.start_tick = tick;
      note.duration = dur;
      note.pitch = static_cast<uint8_t>(std::max(0, std::min(127, pitch)));
      note.velocity = 80;
      note.voice = voice;
      note.source = BachNoteSource::EpisodeMaterial;
      result.push_back(note);

      tick += dur;
    }

    ++pattern_idx;
    sequence_offset -= 1;  // Descend by 1 degree per pattern unit.
  }

  return result;
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
  Tick imitation_offset = imitationOffsetForCharacter(motif_dur, subject.character, rng);

  // Diatonic context: use harmonic minor for minor keys.
  ScaleType scale = subject.is_minor ? ScaleType::HarmonicMinor : ScaleType::Major;

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

  // --- Texture lightening: one voice uses sustained tones instead of motivic development ---
  // When num_voices >= 3, designate one voice (besides 0 and 1) to hold long tones.
  // This creates breathing space in the texture, a standard Baroque episode technique.
  VoiceId resting_voice = (num_voices >= 3)
                              ? selectRestingVoice(num_voices, episode_index)
                              : num_voices;  // Invalid = no resting voice

  // Generate held tones for the resting voice.
  if (resting_voice < num_voices) {
    generateHeldTones(episode, start_tick, duration_ticks, start_key, resting_voice, num_voices);
  }

  // Bass voice (lowest): use motivic bass sequence pattern for density,
  // with held tones filling any remaining gaps.
  VoiceId bass_voice = num_voices - 1;
  if (num_voices >= 3 && bass_voice != resting_voice) {
    auto bass_notes = generateBassSequencePattern(
        subject, start_tick, duration_ticks, start_key, scale,
        bass_voice, num_voices, rng);
    for (auto& n : bass_notes) {
      episode.notes.push_back(n);
    }
    // Fill remaining time with held tones if bass pattern didn't cover it all.
    Tick last_end = bass_notes.empty()
                        ? start_tick
                        : bass_notes.back().start_tick + bass_notes.back().duration;
    if (last_end < start_tick + duration_ticks) {
      generateHeldTones(episode, last_end, start_tick + duration_ticks - last_end,
                        start_key, bass_voice, num_voices);
    }
  }

  // --- Voice 2: Diminished motif (rhythmic contrast, shared across characters) ---
  if (num_voices >= 3 && resting_voice != 2) {
    std::vector<NoteEvent> v2_notes;
    auto diminished = diminishMelody(motif, start_tick + motif_dur);
    for (auto& note : diminished) {
      note.voice = 2;
      v2_notes.push_back(note);
    }
    fitToVoiceRegister(v2_notes, 2, num_voices);
    for (auto& note : v2_notes) {
      episode.notes.push_back(note);
    }
  }

  // --- Voice 3: Augmented tail motif as slow bass foundation (4+ voices) ---
  if (num_voices >= 4 && resting_voice != 3) {
    auto tail = extractTailMotif(subject.notes, 3);
    if (!tail.empty()) {
      std::vector<NoteEvent> v3_notes;
      normalizeMotifToZero(tail);
      auto augmented = augmentMelody(tail, start_tick);
      augmented = transposeMelody(augmented, -12);
      for (auto& note : augmented) {
        note.voice = 3;
        v3_notes.push_back(note);
      }
      fitToVoiceRegister(v3_notes, 3, num_voices);
      for (auto& note : v3_notes) {
        episode.notes.push_back(note);
      }
    }
  }

  // --- Voice 4: Diatonically inverted tail motif with sequence (5 voices) ---
  if (num_voices >= 5 && resting_voice != 4) {
    auto tail = extractTailMotif(subject.notes, 3);
    if (!tail.empty()) {
      std::vector<NoteEvent> v4_notes;
      normalizeMotifToZero(tail);
      uint8_t pivot = tail[0].pitch;
      auto inverted = invertMelodyDiatonic(tail, pivot, start_key, scale);
      Tick voice4_start = start_tick + imitation_offset;
      for (const auto& note : inverted) {
        NoteEvent placed = note;
        placed.start_tick += voice4_start;
        placed.voice = 4;
        v4_notes.push_back(placed);
      }
      Tick inv_dur = motifDuration(inverted);
      if (inv_dur == 0) inv_dur = motif_dur;
      int inv_reps = std::max(1, seq_reps - 1);
      auto inv_seq = generateDiatonicSequence(inverted, inv_reps, deg_step,
                                              voice4_start + inv_dur, start_key, scale);
      for (auto& note : inv_seq) {
        note.source = BachNoteSource::SequenceNote;
      }
      for (auto& note : inv_seq) {
        note.voice = 4;
        v4_notes.push_back(note);
      }
      fitToVoiceRegister(v4_notes, 4, num_voices);
      for (auto& note : v4_notes) {
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
        note.pitch = scale_util::nearestScaleTone(clampPitch(new_pitch, 0, 127), target_key, scale);
      }
    }
  }

  // --- Apply energy-based rhythm density floor ---
  // Clamp note durations to the minimum allowed by the current energy level.
  // Also cap excessively long notes to preserve rhythmic diversity.
  Tick min_dur = FugueEnergyCurve::minDuration(energy_level);
  for (auto& note : episode.notes) {
    if (note.duration < min_dur) {
      note.duration = min_dur;
    }
    // Cap notes that are much longer than the rhythmic context.
    // Exempt resting voice held tones — they are intentionally whole notes.
    if (note.duration > min_dur * 4 &&
        (resting_voice >= num_voices || note.voice != resting_voice)) {
      note.duration = std::max(min_dur * 2, note.duration / 2);
    }
  }

  // --- Invertible counterpoint with character-specific probability ---
  // Swap voice 0 and voice 1 material (double counterpoint at the octave).
  // Only applicable when at least 2 voices are present.
  // Character-specific invertible counterpoint probability with odd/even bias.
  float invert_base = 0.30f;
  switch (subject.character) {
    case SubjectCharacter::Severe:  invert_base = 0.30f; break;
    case SubjectCharacter::Playful: invert_base = 0.60f; break;
    case SubjectCharacter::Noble:   invert_base = 0.20f; break;
    case SubjectCharacter::Restless: invert_base = 0.70f; break;
  }
  // Odd episodes get a +0.15 bias toward inversion.
  float invert_prob = invert_base + ((episode_index % 2 != 0) ? 0.15f : 0.0f);
  if (rng::rollProbability(rng, invert_prob) && num_voices >= 2) {
    applyInvertibleCounterpoint(episode.notes);
  }

  // Snap start_ticks to 8th-note grid for metric integrity.
  constexpr Tick kTickQuantum = kTicksPerBeat / 2;  // 240
  for (auto& note : episode.notes) {
    note.start_tick = (note.start_tick / kTickQuantum) * kTickQuantum;
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
      int ivl = absoluteInterval(subject.notes[idx].pitch, subject.notes[idx - 1].pitch);
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

Episode generateEpisode(const Subject& subject, Tick start_tick, Tick duration_ticks,
                        Key start_key, Key target_key, uint8_t num_voices, uint32_t seed,
                        int episode_index, float energy_level,
                        CounterpointState& cp_state, IRuleEvaluator& cp_rules,
                        CollisionResolver& cp_resolver, const HarmonicTimeline& timeline) {
  // Step 1: Generate unvalidated episode using existing character-specific logic.
  Episode raw = generateEpisode(subject, start_tick, duration_ticks,
                                start_key, target_key, num_voices, seed,
                                episode_index, energy_level);

  // Step 2: Create a PhraseGoal targeting the tonic of the target key at episode end.
  // The target pitch is a DESIGN VALUE (Principle 4): derived deterministically from
  // the cadence tone (tonic of the target key) in octave 4. The goal influences
  // melodic scoring but does not force any notes.
  Tick episode_end = start_tick + duration_ticks;
  PhraseGoal phrase_goal;
  phrase_goal.target_pitch = tonicPitch(target_key, 4);
  phrase_goal.target_tick = episode_end;
  phrase_goal.bonus = 0.3f;

  // Step 3: Post-validate each note through createBachNote().
  Episode validated;
  validated.start_tick = raw.start_tick;
  validated.end_tick = raw.end_tick;
  validated.start_key = raw.start_key;
  validated.end_key = raw.end_key;
  validated.notes.reserve(raw.notes.size());

  // Sort by tick for chronological processing.
  std::sort(raw.notes.begin(), raw.notes.end(),
            [](const NoteEvent& a, const NoteEvent& b) {
              if (a.start_tick != b.start_tick) return a.start_tick < b.start_tick;
              return a.voice < b.voice;
            });

  // Diatonic context for pre-snapping.
  ScaleType scale = subject.is_minor ? ScaleType::HarmonicMinor : ScaleType::Major;

  for (const auto& note : raw.notes) {
    // Query harmonic context at this tick.
    const auto& harm_ev = timeline.getAt(note.start_tick);

    // Pre-snap to diatonic scale (prevents chromatic artifacts from collision resolver).
    Key current_key = (note.start_tick >= start_tick + duration_ticks / 2 &&
                       target_key != start_key)
                          ? target_key
                          : start_key;
    uint8_t diatonic_pitch = scale_util::nearestScaleTone(note.pitch, current_key, scale);

    // Determine desired pitch: snap non-chord tones on strong beats.
    uint8_t desired_pitch = diatonic_pitch;
    bool is_strong = (note.start_tick % kTicksPerBeat == 0);
    if (is_strong && !isChordTone(diatonic_pitch, harm_ev)) {
      desired_pitch = nearestChordTone(diatonic_pitch, harm_ev);
    }

    // Route through createBachNote for counterpoint validation.
    BachNoteOptions opts;
    opts.voice = note.voice;
    opts.desired_pitch = desired_pitch;
    opts.tick = note.start_tick;
    opts.duration = note.duration;
    opts.velocity = note.velocity;
    opts.source = BachNoteSource::EpisodeMaterial;

    BachCreateNoteResult result = createBachNote(&cp_state, &cp_rules, &cp_resolver, opts);
    if (result.accepted) {
      validated.notes.push_back(result.note);
    }
    // Rejected notes become rests (omitted).
  }

  return validated;
}

Episode generateFortspinnungEpisode(const Subject& subject, const MotifPool& pool,
                                    Tick start_tick, Tick duration_ticks,
                                    Key start_key, Key target_key,
                                    uint8_t num_voices, uint32_t seed,
                                    int episode_index, float energy_level) {
  // Fall back to standard generation if the pool is empty.
  if (pool.empty()) {
    return generateEpisode(subject, start_tick, duration_ticks,
                           start_key, target_key, num_voices, seed,
                           episode_index, energy_level);
  }

  Episode episode;
  episode.start_tick = start_tick;
  episode.end_tick = start_tick + duration_ticks;
  episode.start_key = start_key;
  episode.end_key = target_key;

  // Generate Fortspinnung material from the motif pool.
  episode.notes = generateFortspinnung(pool, start_tick, duration_ticks,
                                       num_voices, seed, subject.character,
                                       start_key);

  // Apply gradual key modulation: transpose the second half toward the target key.
  int key_diff = static_cast<int>(target_key) - static_cast<int>(start_key);
  if (key_diff != 0) {
    Tick midpoint = start_tick + duration_ticks / 2;
    ScaleType scale = subject.is_minor ? ScaleType::HarmonicMinor : ScaleType::Major;
    for (auto& note : episode.notes) {
      if (note.start_tick >= midpoint) {
        int new_pitch = static_cast<int>(note.pitch) + key_diff;
        note.pitch = scale_util::nearestScaleTone(clampPitch(new_pitch, 0, 127), target_key, scale);
      }
    }
  }

  // Apply energy-based rhythm density floor.
  // Also cap excessively long notes to preserve rhythmic diversity.
  Tick min_dur = FugueEnergyCurve::minDuration(energy_level);
  for (auto& note : episode.notes) {
    if (note.duration < min_dur) {
      note.duration = min_dur;
    }
    if (note.duration > min_dur * 4) {
      note.duration = std::max(min_dur * 2, note.duration / 2);
    }
  }

  // Invertible counterpoint with character-specific probability.
  // Only applicable when at least 2 voices are present.
  std::mt19937 invert_rng(seed ^ 0x696E7600u);
  // Character-specific invertible counterpoint probability with odd/even bias.
  float fort_invert_base = 0.30f;
  switch (subject.character) {
    case SubjectCharacter::Severe:  fort_invert_base = 0.30f; break;
    case SubjectCharacter::Playful: fort_invert_base = 0.60f; break;
    case SubjectCharacter::Noble:   fort_invert_base = 0.20f; break;
    case SubjectCharacter::Restless: fort_invert_base = 0.70f; break;
  }
  // Odd episodes get a +0.15 bias toward inversion.
  float fort_invert_prob = fort_invert_base + ((episode_index % 2 != 0) ? 0.15f : 0.0f);
  if (rng::rollProbability(invert_rng, fort_invert_prob) && num_voices >= 2) {
    applyInvertibleCounterpoint(episode.notes);
  }

  // Snap start_ticks to 8th-note grid for metric integrity.
  constexpr Tick kFortTickQuantum = kTicksPerBeat / 2;  // 240
  for (auto& note : episode.notes) {
    note.start_tick = (note.start_tick / kFortTickQuantum) * kFortTickQuantum;
  }

  return episode;
}

Episode generateFortspinnungEpisode(const Subject& subject, const MotifPool& pool,
                                    Tick start_tick, Tick duration_ticks,
                                    Key start_key, Key target_key,
                                    uint8_t num_voices, uint32_t seed,
                                    int episode_index, float energy_level,
                                    CounterpointState& cp_state,
                                    IRuleEvaluator& cp_rules,
                                    CollisionResolver& cp_resolver,
                                    const HarmonicTimeline& timeline) {
  // Step 1: Generate unvalidated Fortspinnung episode.
  Episode raw = generateFortspinnungEpisode(subject, pool, start_tick, duration_ticks,
                                            start_key, target_key, num_voices, seed,
                                            episode_index, energy_level);

  // Step 2: Create a PhraseGoal targeting the tonic of the target key at episode end.
  Tick episode_end = start_tick + duration_ticks;
  PhraseGoal phrase_goal;
  phrase_goal.target_pitch = tonicPitch(target_key, 4);
  phrase_goal.target_tick = episode_end;
  phrase_goal.bonus = 0.3f;

  // Step 3: Post-validate each note through createBachNote().
  Episode validated;
  validated.start_tick = raw.start_tick;
  validated.end_tick = raw.end_tick;
  validated.start_key = raw.start_key;
  validated.end_key = raw.end_key;
  validated.notes.reserve(raw.notes.size());

  // Sort by tick for chronological processing.
  std::sort(raw.notes.begin(), raw.notes.end(),
            [](const NoteEvent& a, const NoteEvent& b) {
              if (a.start_tick != b.start_tick) return a.start_tick < b.start_tick;
              return a.voice < b.voice;
            });

  // Diatonic context for pre-snapping.
  ScaleType scale = subject.is_minor ? ScaleType::HarmonicMinor : ScaleType::Major;

  for (size_t note_idx = 0; note_idx < raw.notes.size(); ++note_idx) {
    const auto& note = raw.notes[note_idx];
    // Query harmonic context at this tick.
    const auto& harm_ev = timeline.getAt(note.start_tick);

    // Pre-snap to diatonic scale (Phase 4: prevents chromatic artifacts).
    Key current_key = (note.start_tick >= start_tick + duration_ticks / 2 &&
                       target_key != start_key)
                          ? target_key
                          : start_key;
    uint8_t diatonic_pitch = scale_util::nearestScaleTone(note.pitch, current_key, scale);

    // Determine desired pitch: snap non-chord tones on strong beats.
    uint8_t desired_pitch = diatonic_pitch;
    bool is_strong = (note.start_tick % kTicksPerBeat == 0);
    if (is_strong && !isChordTone(diatonic_pitch, harm_ev)) {
      desired_pitch = nearestChordTone(diatonic_pitch, harm_ev);
    }

    // Lookahead: find next pitch in the same voice for NHT validation.
    uint8_t lookahead_pitch = 0;
    for (size_t nxt = note_idx + 1; nxt < raw.notes.size(); ++nxt) {
      if (raw.notes[nxt].voice == note.voice &&
          raw.notes[nxt].start_tick > note.start_tick) {
        lookahead_pitch = raw.notes[nxt].pitch;
        break;
      }
    }

    // Route through createBachNote for counterpoint validation.
    BachNoteOptions opts;
    opts.voice = note.voice;
    opts.desired_pitch = desired_pitch;
    opts.tick = note.start_tick;
    opts.duration = note.duration;
    opts.velocity = note.velocity;
    opts.source = BachNoteSource::EpisodeMaterial;
    opts.next_pitch = lookahead_pitch;

    BachCreateNoteResult result = createBachNote(&cp_state, &cp_rules, &cp_resolver, opts);
    if (result.accepted) {
      validated.notes.push_back(result.note);
    }
  }

  return validated;
}

}  // namespace bach
