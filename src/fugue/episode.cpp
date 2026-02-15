// Fugue episode: modulatory development of subject material.

#include "fugue/episode.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <random>

#include "core/note_creator.h"
#include "core/interval.h"
#include "core/pitch_utils.h"
#include "core/rng_util.h"
#include "core/scale.h"
#include "counterpoint/collision_resolver.h"
#include "counterpoint/leap_resolution.h"
#include "counterpoint/counterpoint_state.h"
#include "counterpoint/i_rule_evaluator.h"
#include "counterpoint/melodic_context.h"
#include "counterpoint/vertical_safe.h"
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
/// @brief Pitch candidate for episode validated loop.
/// Candidates are scored by harmonic + melodic quality and tried sequentially
/// through createBachNote. Maximum 3 candidates per note (Reduce Generation).
struct PitchCandidate {
  uint8_t pitch = 0;
  float score = 0.0f;        ///< Composite melodic + harmony score (higher = better).
  bool is_chord_tone = false;
  bool has_vertical_dissonance = false;  ///< True if dissonant with any sounding voice.
  bool is_suspension_like = false;       ///< True if pitch == previous note in voice (held).
};

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

/// @brief Calculate median pitch of the first 3 notes in a motif.
///
/// Episode reference uses the opening pitch (median of first 3) rather than
/// the final pitch, because sequences cause register drift when tracking
/// the arrival pitch of each unit.
///
/// @param notes The motif to analyze.
/// @return Median of first 3 pitches, or 0 if empty.
static uint8_t medianOfFirst3(const std::vector<NoteEvent>& notes) {
  if (notes.empty()) return 0;
  size_t count = std::min(notes.size(), size_t{3});
  std::array<uint8_t, 3> heads = {};
  for (size_t idx = 0; idx < count; ++idx) {
    heads[idx] = notes[idx].pitch;
  }
  std::sort(heads.begin(), heads.begin() + count);
  return heads[count / 2];
}

/// @brief Shift notes to fit within the appropriate voice register.
///
/// Uses fitToRegister for optimal octave shift calculation with
/// reference pitch support. Final pitches are clamped to the voice range.
///
/// @param notes Notes to adjust (modified in place).
/// @param voice_id Target voice.
/// @param num_voices Total voices in the fugue.
/// @param reference_pitch Optional reference pitch for continuity.
static void fitToVoiceRegister(std::vector<NoteEvent>& notes, VoiceId voice_id,
                               uint8_t num_voices,
                               uint8_t reference_pitch = 0,
                               uint8_t prev_pitch = 0) {
  if (notes.empty()) return;

  auto [lo, hi] = getFugueVoiceRange(voice_id, num_voices);
  uint8_t adj_lo = 0, adj_hi = 0;
  if (voice_id > 0) {
    std::tie(adj_lo, adj_hi) = getFugueVoiceRange(voice_id - 1, num_voices);
  }
  // When prev_pitch is available, use it as the primary reference for
  // melodic_dist in fitToRegister. This penalizes octave shifts that create
  // large intervals (including tritones) from the previous section.
  uint8_t effective_ref = (prev_pitch > 0) ? prev_pitch : reference_pitch;
  int shift = fitToRegister(notes, lo, hi,
                             effective_ref, 0, 0, 0,
                             adj_lo, adj_hi,
                             false, 0, false);
  for (auto& note : notes) {
    int shifted = static_cast<int>(note.pitch) + shift;
    if (shifted < static_cast<int>(lo) || shifted > static_cast<int>(hi)) {
      note.pitch = clampPitch(shifted, lo, hi);
    } else {
      note.pitch = static_cast<uint8_t>(shifted);
    }
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

/// @brief Count harsh dissonance pairs (m2/TT/M7) among simultaneously sounding notes.
///
/// Counts inter-voice note pairs whose simple interval is a minor 2nd (1 semitone),
/// tritone (6 semitones), or major 7th (11 semitones) and that overlap in time.
/// Used as a quality metric for invertible counterpoint guard: if voice swapping
/// increases harsh dissonances, the swap should be reverted.
///
/// @param notes Episode notes to analyze.
/// @return Number of harsh dissonance pairs found.
static int countHarshDissonances(const std::vector<NoteEvent>& notes) {
  int count = 0;
  for (size_t idx = 0; idx < notes.size(); ++idx) {
    for (size_t jdx = idx + 1; jdx < notes.size(); ++jdx) {
      if (notes[idx].voice == notes[jdx].voice) continue;
      // Check if overlapping in time.
      Tick end_i = notes[idx].start_tick + notes[idx].duration;
      Tick end_j = notes[jdx].start_tick + notes[jdx].duration;
      if (notes[idx].start_tick >= end_j || notes[jdx].start_tick >= end_i) continue;
      // Check interval for harsh dissonance: m2(1), TT(6), M7(11).
      int simple = interval_util::compoundToSimple(
          absoluteInterval(notes[idx].pitch, notes[jdx].pitch));
      if (simple == 1 || simple == 6 || simple == 11) ++count;
    }
  }
  return count;
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
                           Key key, ScaleType scale,
                           const uint8_t* prev_pitches = nullptr) {
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
  fitToVoiceRegister(v0_notes, 0, num_voices, medianOfFirst3(motif),
                     prev_pitches ? prev_pitches[0] : 0);
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
    fitToVoiceRegister(v1_notes, 1, num_voices, medianOfFirst3(inverted),
                       prev_pitches ? prev_pitches[1] : 0);
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
                            Key key, ScaleType scale,
                            const uint8_t* prev_pitches = nullptr) {
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
  fitToVoiceRegister(v0_notes, 0, num_voices, medianOfFirst3(retrograde),
                     prev_pitches ? prev_pitches[0] : 0);
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
    fitToVoiceRegister(v1_notes, 1, num_voices, medianOfFirst3(inverted),
                       prev_pitches ? prev_pitches[1] : 0);
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
                          Key key, ScaleType scale,
                          const uint8_t* prev_pitches = nullptr) {
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
  fitToVoiceRegister(v0_notes, 0, num_voices, medianOfFirst3(motif),
                     prev_pitches ? prev_pitches[0] : 0);
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
    fitToVoiceRegister(v1_notes, 1, num_voices, medianOfFirst3(retrograded),
                       prev_pitches ? prev_pitches[1] : 0);
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
                             Key key, ScaleType scale,
                             const uint8_t* prev_pitches = nullptr) {
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
  fitToVoiceRegister(v0_notes, 0, num_voices, medianOfFirst3(frag0),
                     prev_pitches ? prev_pitches[0] : 0);
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
    fitToVoiceRegister(v1_notes, 1, num_voices, medianOfFirst3(diminished),
                       prev_pitches ? prev_pitches[1] : 0);
    for (auto& note : v1_notes) {
      episode.notes.push_back(note);
    }
  }
}

/// @brief Select which voice should "rest" (hold long tones) in this episode.
///
/// With fewer than 4 voices, all voices are essential (subject, answer, bass)
/// so no voice rests. With 4+ voices, inner voices (indices 2..num_voices-2)
/// rotate based on episode_index. Voices 0 and 1 carry the primary motivic
/// material and are never selected. The bass (num_voices-1) provides harmonic
/// foundation and is never selected.
///
/// @param num_voices Total voices (must be >= 4 for a resting voice to exist).
/// @param episode_index Episode ordinal for rotation.
/// @return Voice ID of the resting voice, or num_voices (sentinel) if no rest.
static VoiceId selectRestingVoice(uint8_t num_voices, int episode_index) {
  // 3 or fewer voices: all voices required (subject + answer + bass). No rest.
  if (num_voices < 4) return num_voices;  // sentinel = no rest
  // 4+ voices: rotate through inner voices (2..num_voices-2).
  // Voice 0/1 = primary motivic, bass (num_voices-1) = harmonic foundation.
  uint8_t inner_count = num_voices - 3;
  if (inner_count == 0) return num_voices;
  return static_cast<VoiceId>(2 + (episode_index % inner_count));
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
  int center_pc = getPitchClass(static_cast<uint8_t>(register_center));
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
    std::mt19937& gen, float energy) {
  std::vector<NoteEvent> result;
  auto [range_low, range_high] = getFugueVoiceRange(voice, num_voices);

  // Build bass pattern from stepwise motion + arpeggiated figures.
  int key_pc = static_cast<int>(key);
  int register_center =
      (static_cast<int>(range_low) + static_cast<int>(range_high)) / 2;
  int center_pc = getPitchClass(static_cast<uint8_t>(register_center));
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
      clampPitch(bass_root, 0, 127), key, scale);

  Tick tick = start_tick;
  int pattern_idx = 0;
  int sequence_offset = 0;  // Descending by 1 degree per 2-bar unit.

  while (tick < start_tick + duration) {
    const int* pattern =
        (pattern_idx % 2 == 0) ? kBassPatternA : kBassPatternB;

    for (int i = 0; i < kPatternLen && tick < start_tick + duration; ++i) {
      Tick raw_dur;
      if (i == 0) {
        // Pattern head: harmonic rhythm anchor. Lower energy for longer notes.
        raw_dur = FugueEnergyCurve::selectDuration(
            std::max(0.0f, energy - 0.15f), tick, gen, kQuarterNote,
            voice_profiles::kBassLine);
        if (raw_dur < kQuarterNote) raw_dur = kQuarterNote;  // Floor: quarter.
      } else {
        raw_dur = FugueEnergyCurve::selectDuration(energy, tick, gen, kHalfNote,
                                                    voice_profiles::kBassLine);
        // Bass sixteenths are a style violation; floor at eighth note.
        if (raw_dur < kTicksPerBeat / 2) raw_dur = kTicksPerBeat / 2;
      }

      // Low energy: reinforce half-note bias for stable harmonic rhythm.
      if (energy < 0.5f && i != 0 && raw_dur < kHalfNote) {
        raw_dur = kHalfNote;
      }

      Tick dur = raw_dur;
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
              clampPitch(pitch, 0, 127),
              key, scale));
        }
      }

      // Keep within voice range.
      while (pitch < static_cast<int>(range_low)) pitch += 12;
      while (pitch > static_cast<int>(range_high)) pitch -= 12;

      NoteEvent note;
      note.start_tick = tick;
      note.duration = dur;
      note.pitch = clampPitch(pitch, 0, 127);
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
                        int episode_index, float energy_level,
                        const uint8_t* prev_pitches) {
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
                             imitation_offset, num_voices, start_key, scale, prev_pitches);
      break;
    case SubjectCharacter::Noble:
      generateNobleEpisode(episode, motif, start_tick, motif_dur, deg_step, seq_reps,
                           imitation_offset, num_voices, start_key, scale, prev_pitches);
      break;
    case SubjectCharacter::Restless:
      generateRestlessEpisode(episode, motif, start_tick, motif_dur, deg_step, seq_reps,
                              imitation_offset, num_voices, start_key, scale, prev_pitches);
      break;
    case SubjectCharacter::Severe:
    default:
      generateSevereEpisode(episode, motif, start_tick, motif_dur, deg_step, seq_reps,
                            imitation_offset, num_voices, start_key, scale, prev_pitches);
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
        bass_voice, num_voices, rng, energy_level);
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
    fitToVoiceRegister(v2_notes, 2, num_voices, medianOfFirst3(diminished),
                       prev_pitches ? prev_pitches[2] : 0);
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
      fitToVoiceRegister(v3_notes, 3, num_voices, medianOfFirst3(augmented),
                         prev_pitches ? prev_pitches[3] : 0);
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
      fitToVoiceRegister(v4_notes, 4, num_voices, medianOfFirst3(inverted),
                         prev_pitches ? prev_pitches[4] : 0);
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
    auto notes_before = episode.notes;
    int harsh_before = countHarshDissonances(notes_before);
    applyInvertibleCounterpoint(episode.notes);
    int harsh_after = countHarshDissonances(episode.notes);
    if (harsh_after > harsh_before) {
      episode.notes = std::move(notes_before);  // Revert: swap increased dissonance.
    }
  }

  // Snap start_ticks to 8th-note grid for metric integrity.
  constexpr Tick kTickQuantum = kTicksPerBeat / 2;  // 240
  for (auto& note : episode.notes) {
    note.start_tick = (note.start_tick / kTickQuantum) * kTickQuantum;
  }

  // Per-voice tritone sweep: fix tritone leaps within each voice.
  // Process each voice independently, sorting by tick order.
  // Determine the modulation midpoint for correct key context.
  Tick mod_midpoint = start_tick + duration_ticks / 2;
  for (VoiceId vid = 0; vid < num_voices; ++vid) {
    // Collect indices for this voice.
    std::vector<size_t> voice_indices;
    for (size_t idx = 0; idx < episode.notes.size(); ++idx) {
      if (episode.notes[idx].voice == vid) {
        voice_indices.push_back(idx);
      }
    }
    // Sort indices by start_tick to ensure tick-order within voice.
    std::sort(voice_indices.begin(), voice_indices.end(),
              [&episode](size_t lhs, size_t rhs) {
                return episode.notes[lhs].start_tick < episode.notes[rhs].start_tick;
              });

    auto [voice_lo, voice_hi] = getFugueVoiceRange(vid, num_voices);

    // Check consecutive note pairs for tritone intervals.
    for (size_t pos = 1; pos < voice_indices.size(); ++pos) {
      size_t prev_idx = voice_indices[pos - 1];
      size_t cur_idx = voice_indices[pos];
      auto& prev_note = episode.notes[prev_idx];
      auto& cur_note = episode.notes[cur_idx];

      int prev_p = static_cast<int>(prev_note.pitch);
      int cur_p = static_cast<int>(cur_note.pitch);
      int simple = interval_util::compoundToSimple(std::abs(cur_p - prev_p));
      if (simple != 6) continue;

      // Short note exemption: both notes <= 240 ticks.
      if (prev_note.duration <= 240 && cur_note.duration <= 240) continue;

      // Determine the diatonic key for this note (respects modulation midpoint).
      Key note_key = (key_diff != 0 && cur_note.start_tick >= mod_midpoint)
                         ? target_key
                         : start_key;

      // Try deltas {1, -1, 2, -2}, snap to scale, pick best non-tritone.
      int best_cand = cur_p;
      int best_cost = 9999;
      for (int delta : {1, -1, 2, -2}) {
        int shifted = cur_p + delta;
        uint8_t snapped = scale_util::nearestScaleTone(
            clampPitch(shifted, voice_lo, voice_hi), note_key, scale);
        int cand = static_cast<int>(snapped);
        int new_simple = interval_util::compoundToSimple(
            std::abs(cand - prev_p));
        if (new_simple == 6) continue;  // Still a tritone.
        // Check forward: avoid creating tritone with next note in voice.
        if (pos + 1 < voice_indices.size()) {
          int next_p = static_cast<int>(
              episode.notes[voice_indices[pos + 1]].pitch);
          int fwd_simple = interval_util::compoundToSimple(
              std::abs(cand - next_p));
          if (fwd_simple == 6) continue;
        }
        int cost = std::abs(cand - prev_p) + ((cand == prev_p) ? 30 : 0);
        if (cost < best_cost) {
          best_cost = cost;
          best_cand = cand;
        }
      }
      if (best_cand != cur_p) {
        cur_note.pitch = clampPitch(best_cand, 0, 127);
      }
    }
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
    int root_pc = getPitchClass(subject.notes[0].pitch);
    bool has_root = false;
    for (size_t idx = start; idx < start + motif_length; ++idx) {
      if (getPitchClass(subject.notes[idx].pitch) == root_pc) {
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
                        CollisionResolver& cp_resolver, const HarmonicTimeline& timeline,
                        uint8_t pedal_pitch) {
  // Step 1: Generate unvalidated episode using existing character-specific logic.
  // Extract per-voice previous pitches from cp_state for tritone avoidance.
  uint8_t episode_prev_pitches[5] = {0, 0, 0, 0, 0};
  for (uint8_t v = 0; v < num_voices && v < 5; ++v) {
    const NoteEvent* prev = cp_state.getLastNote(v);
    if (prev) episode_prev_pitches[v] = prev->pitch;
  }
  Episode raw = generateEpisode(subject, start_tick, duration_ticks,
                                start_key, target_key, num_voices, seed,
                                episode_index, energy_level, episode_prev_pitches);

  // Step 2: Create a PhraseGoal targeting the tonic of the target key at episode end.
  // The target pitch is a DESIGN VALUE (Principle 4): derived deterministically from
  // the cadence tone (tonic of the target key) in octave 4. The goal influences
  // melodic scoring but does not force any notes.
  Tick episode_end = start_tick + duration_ticks;
  PhraseGoal phrase_goal;
  phrase_goal.target_pitch = tonicPitch(target_key, 4);
  phrase_goal.target_tick = episode_end;
  phrase_goal.bonus = 0.3f;

  // Step 3: Prepare validated episode.
  Episode validated;
  validated.start_tick = raw.start_tick;
  validated.end_tick = raw.end_tick;
  validated.start_key = raw.start_key;
  validated.end_key = raw.end_key;
  validated.notes.reserve(raw.notes.size());

  // Sort by tick then voice for tick-group processing.
  std::sort(raw.notes.begin(), raw.notes.end(),
            [](const NoteEvent& a, const NoteEvent& b) {
              if (a.start_tick != b.start_tick) return a.start_tick < b.start_tick;
              return a.voice < b.voice;
            });

  ScaleType scale = subject.is_minor ? ScaleType::HarmonicMinor : ScaleType::Major;
  Tick final_bar_start = (episode_end > kTicksPerBar) ? episode_end - kTicksPerBar : 0;

  // Step 4: Process notes in tick groups.
  // Same start_tick events form a group; within each group, voices are processed
  // in order (0→1→2→...→bass) so that earlier voices are in cp_state when later
  // voices are evaluated.
  size_t group_start = 0;
  while (group_start < raw.notes.size()) {
    Tick group_tick = raw.notes[group_start].start_tick;
    size_t group_end = group_start + 1;
    while (group_end < raw.notes.size() &&
           raw.notes[group_end].start_tick == group_tick) {
      ++group_end;
    }

    for (size_t idx = group_start; idx < group_end; ++idx) {
      const auto& note = raw.notes[idx];
      MetricLevel ml = metricLevel(note.start_tick);
      const auto& harm_ev = timeline.getAt(note.start_tick);

      // Current key for diatonic snapping (midpoint switch).
      Key current_key = (note.start_tick >= start_tick + duration_ticks / 2 &&
                         target_key != start_key)
                            ? target_key
                            : start_key;

      // --- Lookahead: find next pitch in the same voice for NHT validation. ---
      std::optional<uint8_t> lookahead_pitch;
      for (size_t nxt = idx + 1; nxt < raw.notes.size(); ++nxt) {
        if (raw.notes[nxt].voice == note.voice &&
            raw.notes[nxt].start_tick > note.start_tick) {
          lookahead_pitch = raw.notes[nxt].pitch;
          break;
        }
      }

      // --- Candidate generation (harmony-first, max 3). ---
      // Search center uses raw pitch to preserve the intended melodic contour.
      // Scoring uses mel_ctx (from cp_state) to evaluate voice leading quality.
      std::vector<PitchCandidate> candidates;
      candidates.reserve(4);

      // (1) Nearest chord tone to raw pitch (contour-preserving).
      uint8_t ct_near = nearestChordTone(note.pitch, harm_ev);
      candidates.push_back({ct_near, 0.0f, true});

      // (2) Chord tone on the opposite side of raw pitch.
      uint8_t probe = (ct_near >= note.pitch)
                          ? static_cast<uint8_t>(note.pitch >= 4 ? note.pitch - 4 : 0)
                          : static_cast<uint8_t>(note.pitch <= 123 ? note.pitch + 4 : 127);
      uint8_t ct_other = nearestChordTone(probe, harm_ev);
      if (ct_other != ct_near) {
        candidates.push_back({ct_other, 0.0f, true});
      }

      // (3) Raw pitch diatonic snap (if not already a candidate).
      uint8_t raw_diatonic = scale_util::nearestScaleTone(note.pitch, current_key, scale);
      bool raw_is_ct = isChordTone(raw_diatonic, harm_ev);
      bool already_present = false;
      for (const auto& c : candidates) {
        if (c.pitch == raw_diatonic) {
          already_present = true;
          break;
        }
      }
      if (!already_present) {
        // MetricLevel enforcement:
        //   Bar: chord tones only (NHT forbidden).
        //   Beat: NHT only with resolution (lookahead_pitch available).
        //   Offbeat: unrestricted.
        if (ml == MetricLevel::Bar && !raw_is_ct) {
          // Skip non-chord tone on bar start.
        } else if (ml == MetricLevel::Beat && !raw_is_ct && !lookahead_pitch.has_value()) {
          // Skip unresolvable NHT on beat.
        } else {
          candidates.push_back({raw_diatonic, 0.0f, raw_is_ct, false, false});
        }
      }

      // (2b) Guaranteed vertical-consonant candidate: if both chord-tone
      // candidates are vertically dissonant with other voices, add a
      // chord tone that is consonant (imperfect preferred).
      {
        bool all_ct_dissonant = true;
        for (const auto& cnd : candidates) {
          if (!cnd.is_chord_tone) continue;
          // Quick check: is this candidate consonant with all sounding voices?
          bool cnd_ok = true;
          for (VoiceId ov_id : cp_state.getActiveVoices()) {
            if (ov_id == note.voice) continue;
            const NoteEvent* other = cp_state.getNoteAt(ov_id, note.start_tick);
            if (!other || other->start_tick + other->duration <= note.start_tick)
              continue;
            int ivl = interval_util::compoundToSimple(
                absoluteInterval(cnd.pitch, other->pitch));
            if (!interval_util::isConsonance(ivl)) {
              cnd_ok = false;
              break;
            }
          }
          if (cnd_ok) {
            all_ct_dissonant = false;
            break;
          }
        }

        if (all_ct_dissonant && ml >= MetricLevel::Beat) {
          // Search for a chord tone that is consonant with all voices.
          // Prefer imperfect consonances to avoid hollow sound.
          uint8_t best_vc = 0;
          int best_vc_score = -1;
          for (int search = -12; search <= 12; ++search) {
            int cand_p = static_cast<int>(note.pitch) + search;
            if (cand_p < 0 || cand_p > 127) continue;
            if (!isChordTone(static_cast<uint8_t>(cand_p), harm_ev)) continue;
            bool vc_ok = true;
            int vc_score = 0;
            for (VoiceId ov_id : cp_state.getActiveVoices()) {
              if (ov_id == note.voice) continue;
              const NoteEvent* other = cp_state.getNoteAt(ov_id, note.start_tick);
              if (!other || other->start_tick + other->duration <= note.start_tick)
                continue;
              int ivl = interval_util::compoundToSimple(
                  absoluteInterval(static_cast<uint8_t>(cand_p), other->pitch));
              if (!interval_util::isConsonance(ivl)) {
                vc_ok = false;
                break;
              }
              // Prefer imperfect consonances.
              if (ivl == 3 || ivl == 4 || ivl == 8 || ivl == 9) {
                vc_score += 2;
              } else {
                vc_score += 1;
              }
            }
            if (vc_ok && vc_score > best_vc_score) {
              best_vc_score = vc_score;
              best_vc = static_cast<uint8_t>(cand_p);
            }
          }
          if (best_vc_score > 0) {
            bool dup = false;
            for (const auto& cnd : candidates) {
              if (cnd.pitch == best_vc) {
                dup = true;
                break;
              }
            }
            if (!dup) {
              candidates.push_back({best_vc, 0.0f, true, false, false});
            }
          }
        }
      }

      // --- Scoring: melodic quality + PhraseGoal. ---
      MelodicContext mel_ctx = buildMelodicContextFromState(cp_state, note.voice);
      for (auto& cand : candidates) {
        float mel_score = MelodicContext::scoreMelodicQuality(
            mel_ctx, cand.pitch, &phrase_goal, note.start_tick);

        // Additional PhraseGoal bonus on Bar/Beat; doubled in final bar.
        if (ml >= MetricLevel::Beat) {
          float goal_bonus =
              computeGoalApproachBonus(cand.pitch, note.start_tick, phrase_goal);
          if (note.start_tick >= final_bar_start) goal_bonus *= 2.0f;
          mel_score += goal_bonus;
        }

        // Chord tone affinity on structural beats.
        if (cand.is_chord_tone && ml >= MetricLevel::Beat) {
          mel_score += 0.15f;
        }

        // Pedal consonance: light bias against strong-beat pedal dissonance.
        // CollisionResolver handles hard rejection; this only steers ranking.
        if (pedal_pitch > 0 && ml >= MetricLevel::Beat) {
          int pedal_ivl = interval_util::compoundToSimple(
              absoluteInterval(cand.pitch, pedal_pitch));
          if (!interval_util::isConsonance(pedal_ivl)) {
            mel_score += (ml == MetricLevel::Bar) ? -0.4f : -0.2f;
          }
        }

        // Vertical consonance: light bias toward consonant vertical intervals.
        // Final safety is CollisionResolver's responsibility.
        if (ml >= MetricLevel::Beat) {
          bool has_dissonance = false;
          bool has_any_other = false;
          for (VoiceId other_v : cp_state.getActiveVoices()) {
            if (other_v == note.voice) continue;
            const NoteEvent* other = cp_state.getNoteAt(other_v, note.start_tick);
            if (!other || other->start_tick + other->duration <= note.start_tick)
              continue;
            has_any_other = true;
            int ivl = interval_util::compoundToSimple(
                absoluteInterval(cand.pitch, other->pitch));
            if (!interval_util::isConsonance(ivl)) {
              has_dissonance = true;
              break;
            }
          }
          if (has_any_other) {
            if (has_dissonance) {
              cand.has_vertical_dissonance = true;
              mel_score += (ml == MetricLevel::Bar) ? -0.3f : -0.15f;
            } else {
              mel_score += 0.15f;
            }
          }
        }

        // A held pitch (same as previous note in this voice) is suspension-like
        // and may be exempted from the strong-beat dissonance filter.
        const NoteEvent* prev_in_voice = cp_state.getLastNote(note.voice);
        if (prev_in_voice && cand.pitch == prev_in_voice->pitch) {
          cand.is_suspension_like = true;
        }

        // Voice reentry penalty: if this voice is returning after a rest gap,
        // penalize dissonant candidates more strongly.
        {
          const NoteEvent* last_in_voice = cp_state.getLastNote(note.voice);
          if (last_in_voice) {
            Tick last_end = last_in_voice->start_tick + last_in_voice->duration;
            bool is_reentry = (last_end + kTicksPerBeat <= note.start_tick) &&
                              (note.start_tick % kTicksPerBeat == 0);
            if (is_reentry && cand.has_vertical_dissonance) {
              mel_score -= 0.5f;
            }
          }
        }

        cand.score = mel_score;
      }

      // Sort candidates by score descending.
      std::sort(candidates.begin(), candidates.end(),
                [](const PitchCandidate& lhs, const PitchCandidate& rhs) {
                  return lhs.score > rhs.score;
                });

      // Strong-beat dissonance filter: on beats 1 and 3, remove dissonant
      // candidates that are not suspension-like.
      {
        Tick beat_in_bar = (note.start_tick % kTicksPerBar) / kTicksPerBeat;
        bool is_strong_beat = (beat_in_bar == 0 || beat_in_bar == 2);
        if (is_strong_beat) {
          auto disqualify_it = std::remove_if(
              candidates.begin(), candidates.end(),
              [](const PitchCandidate& cnd) {
                return cnd.has_vertical_dissonance && !cnd.is_suspension_like;
              });
          // Safety valve: never remove ALL candidates.
          if (disqualify_it == candidates.begin()) {
            disqualify_it = candidates.begin() + 1;
          }
          candidates.erase(disqualify_it, candidates.end());
        }
      }

      // --- Sequential trial: best → 2nd → 3rd → omit. ---
      bool placed = false;
      for (const auto& cand : candidates) {
        BachNoteOptions opts;
        opts.voice = note.voice;
        opts.desired_pitch = cand.pitch;
        opts.tick = note.start_tick;
        opts.duration = note.duration;
        opts.velocity = note.velocity;
        opts.source = BachNoteSource::EpisodeMaterial;
        opts.next_pitch = lookahead_pitch;
        opts.phrase_goal = &phrase_goal;
        opts.prev_pitches[0] = mel_ctx.prev_pitches[0];
        opts.prev_pitches[1] = mel_ctx.prev_pitches[1];
        opts.prev_pitches[2] = mel_ctx.prev_pitches[2];
        opts.prev_count = mel_ctx.prev_count;
        opts.prev_direction = mel_ctx.prev_direction;

        BachCreateNoteResult result =
            createBachNote(&cp_state, &cp_rules, &cp_resolver, opts);
        if (result.accepted) {
          validated.notes.push_back(result.note);
          placed = true;
          break;
        }
      }
      // All candidates failed → rest (omitted).
      (void)placed;
    }

    group_start = group_end;
  }

  // Leap resolution: fix unresolved P5+ leaps in episode.
  {
    LeapResolutionParams lr_params;
    lr_params.num_voices = num_voices;
    lr_params.leap_threshold = 7;  // Episode: P5+ only.
    lr_params.key_at_tick = [&](Tick t) { return timeline.getKeyAt(t); };
    lr_params.scale_at_tick = [&](Tick) { return scale; };
    lr_params.is_chord_tone = [&](Tick t, uint8_t p) {
      return isChordTone(p, timeline.getAt(t));
    };
    lr_params.voice_range_static = [num_voices](uint8_t v) {
      return getFugueVoiceRange(v, num_voices);
    };
    lr_params.vertical_safe =
        makeVerticalSafeWithParallelCheck(timeline, validated.notes,
                                          num_voices);
    resolveLeaps(validated.notes, lr_params);
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
    auto notes_before = episode.notes;
    int harsh_before = countHarshDissonances(notes_before);
    applyInvertibleCounterpoint(episode.notes);
    int harsh_after = countHarshDissonances(episode.notes);
    if (harsh_after > harsh_before) {
      episode.notes = std::move(notes_before);  // Revert: swap increased dissonance.
    }
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
                                    const HarmonicTimeline& timeline,
                                    uint8_t pedal_pitch) {
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

  // Step 3: Prepare validated episode.
  Episode validated;
  validated.start_tick = raw.start_tick;
  validated.end_tick = raw.end_tick;
  validated.start_key = raw.start_key;
  validated.end_key = raw.end_key;
  validated.notes.reserve(raw.notes.size());

  // Sort by tick then voice for tick-group processing.
  std::sort(raw.notes.begin(), raw.notes.end(),
            [](const NoteEvent& a, const NoteEvent& b) {
              if (a.start_tick != b.start_tick) return a.start_tick < b.start_tick;
              return a.voice < b.voice;
            });

  ScaleType scale = subject.is_minor ? ScaleType::HarmonicMinor : ScaleType::Major;
  Tick final_bar_start = (episode_end > kTicksPerBar) ? episode_end - kTicksPerBar : 0;

  // --- Harmonic projection bass (basso fondamentale) ---
  // For 3-voice fugues without pedal, replace melodic bass with harmonic projection.
  VoiceId bass_voice_id = num_voices - 1;
  bool is_harmonic_bass = (num_voices >= 3 && pedal_pitch == 0);
  if (is_harmonic_bass) {
    // Remove raw melodic bass.
    raw.notes.erase(
        std::remove_if(raw.notes.begin(), raw.notes.end(),
                       [bass_voice_id](const NoteEvent& n) {
                         return n.voice == bass_voice_id;
                       }),
        raw.notes.end());

    auto [bass_lo_u, bass_hi_u] = getFugueVoiceRange(bass_voice_id, num_voices);
    int bass_lo = static_cast<int>(bass_lo_u);
    int bass_hi = static_cast<int>(bass_hi_u);
    auto [adj_lo_u, adj_hi_u] = getFugueVoiceRange(bass_voice_id - 1, num_voices);
    int adj_lo = static_cast<int>(adj_lo_u);
    Tick ep_end = start_tick + duration_ticks;

    // Build structural ticks from timeline (harmonic root changes + bar fallback).
    const auto& events = timeline.events();
    std::vector<Tick> structural_ticks;
    int prev_root_pc = -1;

    for (const auto& ev : events) {
      if (ev.end_tick <= start_tick) {
        prev_root_pc = getPitchClass(ev.chord.root_pitch);
        continue;
      }
      if (ev.tick >= ep_end) break;

      Tick t = std::max(ev.tick, start_tick);
      int curr_root_pc = getPitchClass(ev.chord.root_pitch);

      if (curr_root_pc != prev_root_pc) {
        structural_ticks.push_back(t);
      } else if (t % kTicksPerBar == 0) {
        structural_ticks.push_back(t);
      }
      prev_root_pc = curr_root_pc;
    }
    if (structural_ticks.empty() || structural_ticks[0] != start_tick) {
      structural_ticks.insert(structural_ticks.begin(), start_tick);
    }

    // Place bass notes at structural ticks.
    uint8_t prev_bass_pitch = 0;

    for (size_t si = 0; si < structural_ticks.size(); ++si) {
      Tick tick = structural_ticks[si];
      const auto& harm_ev = timeline.getAt(tick);

      Tick dur = (si + 1 < structural_ticks.size())
                     ? structural_ticks[si + 1] - tick
                     : ep_end - tick;
      if (dur == 0) continue;

      // Candidate pitches: root -> 5th -> inversion bass.
      uint8_t targets[3];
      int num_targets;

      bool is_dominant = (harm_ev.chord.degree == ChordDegree::V ||
                          harm_ev.chord.degree == ChordDegree::V_of_V);
      bool is_tonic = (harm_ev.chord.degree == ChordDegree::I);

      if (is_dominant || is_tonic) {
        targets[0] = harm_ev.chord.root_pitch;
        num_targets = 1;
      } else {
        targets[0] = harm_ev.chord.root_pitch;
        targets[1] = static_cast<uint8_t>(
            std::min(127, static_cast<int>(harm_ev.chord.root_pitch) + 7));
        targets[2] = (harm_ev.bass_pitch > 0) ? harm_ev.bass_pitch
                                               : harm_ev.chord.root_pitch;
        num_targets = 3;
      }

      // Octave-fit with hard crossing constraint.
      uint8_t placed_pitch = 0;
      bool found = false;
      for (int ci = 0; ci < num_targets && !found; ++ci) {
        int p = static_cast<int>(targets[ci]);
        int best = -1;
        int best_cost = 999;
        for (int shift = -48; shift <= 48; shift += 12) {
          int cand = p + shift;
          if (cand < bass_lo || cand > bass_hi) continue;
          if (cand >= adj_lo) continue;  // Hard crossing reject.
          int cost = (cand > (bass_lo + bass_hi) / 2) ? 2 : 0;
          if (cost < best_cost) {
            best_cost = cost;
            best = cand;
          }
        }
        if (best >= 0) {
          placed_pitch = static_cast<uint8_t>(best);
          found = true;
        }
      }

      // Relax crossing constraint if no candidate fits.
      if (!found) {
        for (int ci = 0; ci < num_targets && !found; ++ci) {
          int p = static_cast<int>(targets[ci]);
          for (int shift = -48; shift <= 48; shift += 12) {
            int cand = p + shift;
            if (cand < bass_lo || cand > bass_hi) continue;
            placed_pitch = static_cast<uint8_t>(cand);
            found = true;
            break;
          }
        }
      }
      if (!found) continue;

      // Lightweight counterpoint pre-check.
      bool cp_ok = true;
      if (prev_bass_pitch > 0) {
        int melodic_interval = std::abs(
            static_cast<int>(placed_pitch) - static_cast<int>(prev_bass_pitch));

        // Tritone prohibition in bass melodic motion.
        if (melodic_interval == 6) cp_ok = false;

        // Leading tone resolution.
        int tonic_pc = static_cast<int>(harm_ev.key);
        int prev_pc = getPitchClass(prev_bass_pitch);
        int leading_pc = (tonic_pc + 11) % 12;
        if (prev_pc == leading_pc && getPitchClass(placed_pitch) != tonic_pc) {
          cp_ok = false;
        }
      }

      if (!cp_ok) {
        // Try nearby scale tones as fallback.
        Key ck = harm_ev.key;
        for (int delta : {2, -2, 1, -1}) {
          int alt = static_cast<int>(placed_pitch) + delta;
          if (alt < bass_lo || alt > bass_hi) continue;
          if (alt >= adj_lo) continue;
          uint8_t ualt = static_cast<uint8_t>(alt);
          if (!scale_util::isScaleTone(ualt, ck, scale)) continue;
          int mi = std::abs(alt - static_cast<int>(prev_bass_pitch));
          if (mi == 6) continue;
          placed_pitch = ualt;
          cp_ok = true;
          break;
        }
        if (!cp_ok) continue;
      }

      NoteEvent bass_note;
      bass_note.start_tick = tick;
      bass_note.duration = dur;
      bass_note.pitch = placed_pitch;
      bass_note.velocity = 80;
      bass_note.voice = bass_voice_id;
      bass_note.source = BachNoteSource::EpisodeMaterial;
      raw.notes.push_back(bass_note);
      prev_bass_pitch = placed_pitch;
    }

    // Re-sort after bass insertion.
    std::sort(raw.notes.begin(), raw.notes.end(),
              [](const NoteEvent& a, const NoteEvent& b) {
                if (a.start_tick != b.start_tick) return a.start_tick < b.start_tick;
                return a.voice < b.voice;
              });
  }

  // Step 4: Process notes in tick groups.
  // Same start_tick events form a group; within each group, voices are processed
  // in order so earlier voices are in cp_state when later voices are evaluated.
  size_t group_start = 0;
  while (group_start < raw.notes.size()) {
    Tick group_tick = raw.notes[group_start].start_tick;
    size_t group_end = group_start + 1;
    while (group_end < raw.notes.size() &&
           raw.notes[group_end].start_tick == group_tick) {
      ++group_end;
    }

    for (size_t idx = group_start; idx < group_end; ++idx) {
      const auto& note = raw.notes[idx];
      MetricLevel ml = metricLevel(note.start_tick);
      const auto& harm_ev = timeline.getAt(note.start_tick);

      // Current key for diatonic snapping (midpoint switch).
      Key current_key = (note.start_tick >= start_tick + duration_ticks / 2 &&
                         target_key != start_key)
                            ? target_key
                            : start_key;

      // Lookahead: find next pitch in the same voice for NHT validation.
      std::optional<uint8_t> lookahead_pitch;
      for (size_t nxt = idx + 1; nxt < raw.notes.size(); ++nxt) {
        if (raw.notes[nxt].voice == note.voice &&
            raw.notes[nxt].start_tick > note.start_tick) {
          lookahead_pitch = raw.notes[nxt].pitch;
          break;
        }
      }

      // --- Candidate generation (harmony-first, max 3). ---
      // Search center uses raw pitch to preserve the intended melodic contour.
      // Scoring uses mel_ctx (from cp_state) to evaluate voice leading quality.
      std::vector<PitchCandidate> candidates;
      candidates.reserve(4);

      // (1) Nearest chord tone to raw pitch (contour-preserving).
      uint8_t ct_near = nearestChordTone(note.pitch, harm_ev);
      candidates.push_back({ct_near, 0.0f, true});

      // (2) Chord tone on the opposite side of raw pitch.
      uint8_t probe = (ct_near >= note.pitch)
                          ? static_cast<uint8_t>(note.pitch >= 4 ? note.pitch - 4 : 0)
                          : static_cast<uint8_t>(note.pitch <= 123 ? note.pitch + 4 : 127);
      uint8_t ct_other = nearestChordTone(probe, harm_ev);
      if (ct_other != ct_near) {
        candidates.push_back({ct_other, 0.0f, true});
      }

      // (3) Raw pitch diatonic snap (if not already a candidate).
      uint8_t raw_diatonic = scale_util::nearestScaleTone(note.pitch, current_key, scale);
      bool raw_is_ct = isChordTone(raw_diatonic, harm_ev);
      bool already_present = false;
      for (const auto& c : candidates) {
        if (c.pitch == raw_diatonic) {
          already_present = true;
          break;
        }
      }
      if (!already_present) {
        // MetricLevel enforcement:
        //   Bar: chord tones only.
        //   Beat: NHT only with resolution (lookahead available).
        //   Offbeat: unrestricted.
        if (ml == MetricLevel::Bar && !raw_is_ct) {
          // Skip non-chord tone on bar start.
        } else if (ml == MetricLevel::Beat && !raw_is_ct && !lookahead_pitch.has_value()) {
          // Skip unresolvable NHT on beat.
        } else {
          candidates.push_back({raw_diatonic, 0.0f, raw_is_ct});
        }
      }

      // --- Scoring: melodic quality + PhraseGoal. ---
      MelodicContext mel_ctx = buildMelodicContextFromState(cp_state, note.voice);
      for (auto& cand : candidates) {
        float mel_score = MelodicContext::scoreMelodicQuality(
            mel_ctx, cand.pitch, &phrase_goal, note.start_tick);

        // Additional PhraseGoal bonus on Bar/Beat; doubled in final bar.
        if (ml >= MetricLevel::Beat) {
          float goal_bonus =
              computeGoalApproachBonus(cand.pitch, note.start_tick, phrase_goal);
          if (note.start_tick >= final_bar_start) goal_bonus *= 2.0f;
          mel_score += goal_bonus;
        }

        // Chord tone affinity on structural beats.
        if (cand.is_chord_tone && ml >= MetricLevel::Beat) {
          mel_score += 0.15f;
        }

        // Pedal consonance: light bias against strong-beat pedal dissonance.
        // CollisionResolver handles hard rejection; this only steers ranking.
        if (pedal_pitch > 0 && ml >= MetricLevel::Beat) {
          int pedal_ivl = interval_util::compoundToSimple(
              absoluteInterval(cand.pitch, pedal_pitch));
          if (!interval_util::isConsonance(pedal_ivl)) {
            mel_score += (ml == MetricLevel::Bar) ? -0.4f : -0.2f;
          }
        }

        // Vertical consonance: light bias toward consonant vertical intervals.
        // Final safety is CollisionResolver's responsibility.
        if (ml >= MetricLevel::Beat) {
          bool has_dissonance = false;
          bool has_any_other = false;
          for (VoiceId other_v : cp_state.getActiveVoices()) {
            if (other_v == note.voice) continue;
            const NoteEvent* other = cp_state.getNoteAt(other_v, note.start_tick);
            if (!other || other->start_tick + other->duration <= note.start_tick)
              continue;
            has_any_other = true;
            int ivl = interval_util::compoundToSimple(
                absoluteInterval(cand.pitch, other->pitch));
            if (!interval_util::isConsonance(ivl)) {
              has_dissonance = true;
              break;
            }
          }
          if (has_any_other) {
            if (has_dissonance) {
              cand.has_vertical_dissonance = true;
              mel_score += (ml == MetricLevel::Bar) ? -0.3f : -0.15f;
            } else {
              mel_score += 0.15f;
            }
          }
        }

        cand.score = mel_score;

        // Suspension-like detection: held pitch from previous note in voice.
        const NoteEvent* prev_in_voice = cp_state.getLastNote(note.voice);
        if (prev_in_voice && cand.pitch == prev_in_voice->pitch) {
          cand.is_suspension_like = true;
        }
      }

      // Sort candidates by score descending.
      std::sort(candidates.begin(), candidates.end(),
                [](const PitchCandidate& a, const PitchCandidate& b) {
                  return a.score > b.score;
                });

      // Strong-beat dissonance filter: on beats 1 and 3, remove dissonant
      // candidates that are not suspension-like.
      Tick beat_in_bar = (note.start_tick % kTicksPerBar) / kTicksPerBeat;
      bool is_strong_beat = (beat_in_bar == 0 || beat_in_bar == 2);
      if (is_strong_beat) {
        auto disqualify_it = std::remove_if(
            candidates.begin(), candidates.end(),
            [](const PitchCandidate& cnd) {
              return cnd.has_vertical_dissonance && !cnd.is_suspension_like;
            });
        // Safety valve: never remove ALL candidates.
        if (disqualify_it == candidates.begin()) {
          disqualify_it = candidates.begin() + 1;
        }
        candidates.erase(disqualify_it, candidates.end());
      }

      // --- Sequential trial: best → 2nd → 3rd → omit. ---
      bool placed = false;
      for (const auto& cand : candidates) {
        BachNoteOptions opts;
        opts.voice = note.voice;
        opts.desired_pitch = cand.pitch;
        opts.tick = note.start_tick;
        opts.duration = note.duration;
        opts.velocity = note.velocity;
        opts.source = BachNoteSource::EpisodeMaterial;
        opts.next_pitch = lookahead_pitch;
        opts.phrase_goal = &phrase_goal;
        opts.prev_pitches[0] = mel_ctx.prev_pitches[0];
        opts.prev_pitches[1] = mel_ctx.prev_pitches[1];
        opts.prev_pitches[2] = mel_ctx.prev_pitches[2];
        opts.prev_count = mel_ctx.prev_count;
        opts.prev_direction = mel_ctx.prev_direction;

        BachCreateNoteResult result =
            createBachNote(&cp_state, &cp_rules, &cp_resolver, opts);
        if (result.accepted) {
          validated.notes.push_back(result.note);
          placed = true;
          break;
        }
      }
      // All candidates failed → rest (omitted).
      (void)placed;
    }

    group_start = group_end;
  }

  // Leap resolution: fix unresolved P5+ leaps in episode.
  {
    LeapResolutionParams lr_params;
    lr_params.num_voices = num_voices;
    lr_params.leap_threshold = 7;  // Episode: P5+ only.
    lr_params.key_at_tick = [&](Tick t) { return timeline.getKeyAt(t); };
    lr_params.scale_at_tick = [&](Tick) { return scale; };
    lr_params.is_chord_tone = [&](Tick t, uint8_t p) {
      return isChordTone(p, timeline.getAt(t));
    };
    lr_params.voice_range_static = [num_voices](uint8_t v) {
      return getFugueVoiceRange(v, num_voices);
    };
    lr_params.vertical_safe =
        makeVerticalSafeWithParallelCheck(timeline, validated.notes,
                                          num_voices);
    resolveLeaps(validated.notes, lr_params);
  }

  return validated;
}

}  // namespace bach
