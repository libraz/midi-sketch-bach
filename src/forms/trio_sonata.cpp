// Implementation of the trio sonata generator (BWV 525-530 style).
// Phrase-based three-voice imitative counterpoint with walking bass.

#include "forms/trio_sonata.h"

#include <algorithm>
#include <cmath>
#include <random>
#include <vector>

#include "analysis/counterpoint_analyzer.h"
#include "core/gm_program.h"
#include "core/interval.h"
#include "core/melodic_state.h"
#include "core/note_creator.h"
#include "core/pitch_utils.h"
#include "core/rng_util.h"
#include "core/scale.h"
#include "counterpoint/bach_rule_evaluator.h"
#include "counterpoint/collision_resolver.h"
#include "counterpoint/counterpoint_state.h"
#include "counterpoint/leap_resolution.h"
#include "counterpoint/parallel_repair.h"
#include "counterpoint/species_rules.h"
#include "harmony/chord_tone_utils.h"
#include "harmony/chord_types.h"
#include "harmony/harmonic_event.h"
#include "harmony/harmonic_timeline.h"
#include "organ/organ_techniques.h"
#include "ornament/ornament_engine.h"
#include "transform/motif_transform.h"
#include "transform/sequence.h"

namespace bach {

namespace {

using namespace duration;

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

constexpr Tick kFastMovementBars = 16;
constexpr Tick kSlowMovementBars = 12;
constexpr uint32_t kMovement2SeedOffset = 1000;
constexpr uint32_t kMovement3SeedOffset = 2000;
constexpr uint8_t kTrioVoiceCount = 3;

constexpr uint8_t kRhChannel = 0;
constexpr uint8_t kLhChannel = 1;
constexpr uint8_t kPedalChannel = 3;

/// @brief Phrase length in bars.
constexpr Tick kPhraseBars = 4;
constexpr Tick kPhraseTicks = kPhraseBars * kTicksPerBar;  // 7680

/// @brief Right hand register bounds.
constexpr uint8_t kRhLow = 64;
constexpr uint8_t kRhHigh = 84;
constexpr uint8_t kRhCenter = 74;

/// @brief Left hand register bounds.
constexpr uint8_t kLhLow = 48;
constexpr uint8_t kLhHigh = 72;
constexpr uint8_t kLhCenter = 60;

// ---------------------------------------------------------------------------
// Movement character
// ---------------------------------------------------------------------------

/// @brief Movement character determines rhythmic and melodic parameters.
enum class TrioMovementCharacter : uint8_t { Allegro, Adagio, Vivace };

/// @brief Design values for each movement character.
struct CharacterParams {
  Tick primary_dur;        ///< Primary note duration.
  Tick secondary_dur;      ///< Secondary (shorter) note duration.
  float secondary_prob;    ///< Probability of secondary duration.
  int motif_len_min;       ///< Minimum motif length (notes).
  int motif_len_max;       ///< Maximum motif length (notes).
  float step_ratio;        ///< Probability of stepwise motion in motif.
  float skip_ratio;        ///< Probability of 3rd skip in motif.
  float pedal_dur;         ///< Primary pedal note duration.
  float thematic_bass_prob;  ///< Probability of thematic bass phrase.

  // Harmonic progression weights: DescFifths, CircleOfFifths, ChromCircle, Subdominant, Borrowed.
  float harm_weights[5];
};

/// @brief Get character parameters for a movement type.
const CharacterParams& getCharacterParams(TrioMovementCharacter ch) {
  static const CharacterParams kAllegro = {
      kEighthNote, kSixteenthNote, 0.30f,
      6, 8,
      0.55f, 0.28f,
      static_cast<float>(kQuarterNote),
      0.20f,
      {0.30f, 0.25f, 0.20f, 0.15f, 0.10f}};

  static const CharacterParams kAdagio = {
      kQuarterNote, kEighthNote, 0.50f,
      5, 7,
      0.65f, 0.25f,
      static_cast<float>(kHalfNote),
      0.30f,
      {0.20f, 0.25f, 0.10f, 0.30f, 0.15f}};

  static const CharacterParams kVivace = {
      kEighthNote, kSixteenthNote, 0.40f,
      4, 6,
      0.55f, 0.28f,
      static_cast<float>(kQuarterNote),
      0.25f,
      {0.20f, 0.20f, 0.30f, 0.15f, 0.15f}};

  switch (ch) {
    case TrioMovementCharacter::Adagio: return kAdagio;
    case TrioMovementCharacter::Vivace: return kVivace;
    default: return kAllegro;
  }
}

// ---------------------------------------------------------------------------
// Track creation
// ---------------------------------------------------------------------------

std::vector<Track> createTrioSonataTracks() {
  std::vector<Track> tracks;
  tracks.reserve(kTrioVoiceCount);

  Track rh_track;
  rh_track.channel = kRhChannel;
  rh_track.program = GmProgram::kChurchOrgan;
  rh_track.name = "Right Hand (Great)";
  tracks.push_back(rh_track);

  Track lh_track;
  lh_track.channel = kLhChannel;
  lh_track.program = GmProgram::kReedOrgan;
  lh_track.name = "Left Hand (Swell)";
  tracks.push_back(lh_track);

  Track pedal_track;
  pedal_track.channel = kPedalChannel;
  pedal_track.program = GmProgram::kChurchOrgan;
  pedal_track.name = "Pedal";
  tracks.push_back(pedal_track);

  return tracks;
}

// ---------------------------------------------------------------------------
// Step 1: Harmonic timeline with weighted progression selection
// ---------------------------------------------------------------------------

/// @brief Build a harmonic timeline with varied progressions and cadences.
///
/// Each 4-bar phrase gets a progression type chosen by weighted probability.
/// The final phrase always ends with a cadence. Modulations to dominant or
/// relative key may occur in middle phrases.
HarmonicTimeline buildMovementTimeline(const KeySignature& key_sig, Tick duration,
                                       const CharacterParams& params,
                                       std::mt19937& rng) {
  HarmonicTimeline combined;
  Tick num_phrases = duration / kPhraseTicks;
  if (num_phrases == 0) num_phrases = 1;

  static const std::vector<ProgressionType> kProgTypes = {
      ProgressionType::DescendingFifths, ProgressionType::CircleOfFifths,
      ProgressionType::ChromaticCircle, ProgressionType::Subdominant,
      ProgressionType::BorrowedChord};

  std::vector<float> weights(params.harm_weights, params.harm_weights + 5);

  // For major keys: disable chromatic progressions to keep tonality clean.
  if (!key_sig.is_minor) {
    // Indices: 0=DescFifths, 1=CircleOfFifths, 2=ChromCircle, 3=Subdominant, 4=Borrowed.
    float chromatic_weight = weights[2];
    float borrowed_weight = weights[4];
    weights[2] = 0.0f;
    weights[4] = 0.0f;
    // Redistribute to diatonic progressions.
    float redistribute = chromatic_weight + borrowed_weight;
    weights[0] += redistribute * 0.40f;  // DescendingFifths
    weights[1] += redistribute * 0.35f;  // CircleOfFifths
    weights[3] += redistribute * 0.25f;  // Subdominant
  }

  for (Tick p = 0; p < num_phrases; ++p) {
    Tick phrase_start = p * kPhraseTicks;
    Tick phrase_dur = kPhraseTicks;
    if (phrase_start + phrase_dur > duration) {
      phrase_dur = duration - phrase_start;
    }

    // Choose key for this phrase: home key, with 25% chance of dominant in mid phrases.
    KeySignature phrase_key = key_sig;
    if (p > 0 && p < num_phrases - 1 && rng::rollProbability(rng, 0.25f)) {
      phrase_key = rng::rollProbability(rng, 0.6f) ? getDominant(key_sig)
                                                   : getRelative(key_sig);
    }

    // Final phrase: use Subdominant or CircleOfFifths for stable ending.
    ProgressionType prog;
    if (p == num_phrases - 1) {
      prog = rng::rollProbability(rng, 0.5f) ? ProgressionType::Subdominant
                                             : ProgressionType::CircleOfFifths;
    } else {
      prog = rng::selectWeighted(rng, kProgTypes, weights);
    }

    HarmonicTimeline phrase_tl =
        HarmonicTimeline::createProgression(phrase_key, phrase_dur,
                                            HarmonicResolution::Bar, prog);

    // Apply cadence to non-first phrases.
    if (p == num_phrases - 1) {
      phrase_tl.applyCadence(CadenceType::Perfect, phrase_key);
    } else if (p > 0 && rng::rollProbability(rng, 0.60f)) {
      CadenceType cad = rng::selectWeighted(
          rng,
          std::vector<CadenceType>{CadenceType::Half, CadenceType::Deceptive,
                                   CadenceType::Perfect},
          std::vector<float>{0.40f, 0.30f, 0.30f});
      phrase_tl.applyCadence(cad, phrase_key);
    }

    // Offset events to absolute position and add to combined timeline.
    for (const auto& ev : phrase_tl.events()) {
      HarmonicEvent shifted = ev;
      shifted.tick += phrase_start;
      shifted.end_tick += phrase_start;
      combined.addEvent(shifted);
    }
  }

  return combined;
}

// ---------------------------------------------------------------------------
// Step 2: Motif generation
// ---------------------------------------------------------------------------

/// @brief Generate a short melodic motif (4-8 notes).
///
/// The motif starts and ends on chord tones and uses stepwise/skip/leap
/// motion according to the movement character. A leap is always followed
/// by contrary stepwise recovery.
///
/// @param event Harmonic context at motif start.
/// @param params Movement character parameters.
/// @param key Current key context.
/// @param is_minor Mode.
/// @param rng RNG.
/// @return Vector of NoteEvents with tick starting at 0 and pitches in C4-C5 range.
std::vector<NoteEvent> generateMotif(const HarmonicEvent& event,
                                     const CharacterParams& params,
                                     Key key, bool is_minor,
                                     std::mt19937& rng) {
  std::vector<NoteEvent> motif;
  ScaleType scale = is_minor ? ScaleType::HarmonicMinor : ScaleType::Major;

  int num_notes = rng::rollRange(rng, params.motif_len_min, params.motif_len_max);

  // Start on a chord tone near C4.
  uint8_t start_pitch = nearestChordTone(kMidiC4, event);
  // Clamp to a singable range around C4.
  start_pitch = clampPitch(static_cast<int>(start_pitch), 55, 72);

  Tick current_tick = 0;
  uint8_t prev_pitch = start_pitch;
  bool need_recovery = false;
  bool recovery_descend = false;

  for (int i = 0; i < num_notes; ++i) {
    // Choose duration.
    Tick dur = rng::rollProbability(rng, params.secondary_prob)
                   ? params.secondary_dur
                   : params.primary_dur;

    uint8_t pitch;
    if (i == 0) {
      pitch = start_pitch;
    } else if (need_recovery) {
      // After a leap, move stepwise in opposite direction.
      int abs_deg = scale_util::pitchToAbsoluteDegree(prev_pitch, key, scale);
      int step = recovery_descend ? -1 : 1;
      pitch = scale_util::absoluteDegreeToPitch(abs_deg + step, key, scale);
      need_recovery = false;
    } else {
      // Decide motion type: step, skip, or leap.
      float roll = rng::rollFloat(rng, 0.0f, 1.0f);
      int abs_deg = scale_util::pitchToAbsoluteDegree(prev_pitch, key, scale);
      bool ascending = rng::rollProbability(rng, 0.5f);
      int direction = ascending ? 1 : -1;

      if (roll < params.step_ratio) {
        // Stepwise (1 degree).
        pitch = scale_util::absoluteDegreeToPitch(abs_deg + direction, key, scale);
      } else if (roll < params.step_ratio + params.skip_ratio) {
        // Skip (2 degrees = 3rd).
        pitch = scale_util::absoluteDegreeToPitch(abs_deg + 2 * direction, key, scale);
      } else {
        // Leap (3-4 degrees = 4th/5th).
        int leap = rng::rollRange(rng, 3, 4) * direction;
        pitch = scale_util::absoluteDegreeToPitch(abs_deg + leap, key, scale);
        need_recovery = true;
        recovery_descend = ascending;  // Recover in opposite direction.
      }
    }

    // Clamp to a reasonable motif range.
    pitch = clampPitch(static_cast<int>(pitch), 48, 84);

    NoteEvent note;
    note.start_tick = current_tick;
    note.duration = dur;
    note.pitch = pitch;
    note.velocity = kOrganVelocity;
    note.voice = 0;
    note.source = BachNoteSource::FreeCounterpoint;
    motif.push_back(note);

    current_tick += dur;
    prev_pitch = pitch;
  }

  // Snap last note to chord tone for harmonic stability.
  if (!motif.empty()) {
    motif.back().pitch = nearestChordTone(motif.back().pitch, event);
    motif.back().modified_by |= static_cast<uint8_t>(NoteModifiedBy::ChordToneSnap);
    motif.back().pitch =
        clampPitch(static_cast<int>(motif.back().pitch), 48, 84);
    motif.back().modified_by |= static_cast<uint8_t>(NoteModifiedBy::OctaveAdjust);
  }

  return motif;
}

// ---------------------------------------------------------------------------
// Step 3: Upper voice phrase generation (RH/LH)
// ---------------------------------------------------------------------------

/// @brief Transpose a motif to a target register center.
std::vector<NoteEvent> placeInRegister(const std::vector<NoteEvent>& motif,
                                       uint8_t target_center, uint8_t range_low,
                                       uint8_t range_high) {
  if (motif.empty()) return motif;

  // Find average pitch of motif.
  int sum = 0;
  for (const auto& n : motif) sum += n.pitch;
  int avg = sum / static_cast<int>(motif.size());
  int shift = static_cast<int>(target_center) - avg;

  auto placed = transposeMelody(motif, shift);
  for (auto& n : placed) {
    n.pitch = clampPitch(static_cast<int>(n.pitch), range_low, range_high);
    n.modified_by |= static_cast<uint8_t>(NoteModifiedBy::OctaveAdjust);
  }
  return placed;
}

/// @brief Clamp excessive leaps (>12 semitones) in a note sequence.
///
/// Adjusts notes that create intervals larger than the threshold by shifting
/// them closer by octave. Cadence-window notes and notes resolving a
/// previous large leap are exempt.
///
/// @param notes Notes to clamp (modified in-place).
/// @param threshold Maximum allowed interval in semitones (default 12).
/// @param center Register center pitch for voice role preservation.
/// @param phrase_end End tick of the current phrase (for cadence window).
void clampExcessiveLeaps(std::vector<NoteEvent>& notes, int threshold,
                         uint8_t center, Tick phrase_end) {
  if (notes.size() < 2) return;

  // Cadence window: last 2 beats of phrase are exempt.
  Tick cadence_start = (phrase_end > kTicksPerBeat * 2)
                           ? (phrase_end - kTicksPerBeat * 2) : 0;

  bool prev_was_large = false;

  for (size_t idx = 1; idx < notes.size(); ++idx) {
    int prev_p = static_cast<int>(notes[idx - 1].pitch);
    int cur_p = static_cast<int>(notes[idx].pitch);
    int interval = std::abs(cur_p - prev_p);

    // Cadence window exemption.
    if (notes[idx].start_tick >= cadence_start) {
      prev_was_large = (interval > threshold);
      continue;
    }

    // Skip if previous leap is being resolved (preserve resolution direction).
    if (prev_was_large) {
      prev_was_large = (interval > threshold);
      continue;
    }

    if (interval > threshold) {
      // Shift current note by octave toward previous note.
      int shift = nearestOctaveShift(prev_p - cur_p);
      int new_pitch = cur_p + shift;

      // Guard: don't cross register center (voice role preservation).
      bool cur_above_center = (cur_p >= static_cast<int>(center));
      bool new_above_center = (new_pitch >= static_cast<int>(center));
      if (cur_above_center != new_above_center) {
        // Crossing register center -- abort this correction.
        prev_was_large = true;
        continue;
      }

      // Clamp to valid MIDI range.
      new_pitch = std::max(0, std::min(127, new_pitch));

      // Only apply if the new interval is actually smaller.
      if (std::abs(new_pitch - prev_p) < interval) {
        notes[idx].pitch = static_cast<uint8_t>(new_pitch);
        notes[idx].modified_by |= static_cast<uint8_t>(NoteModifiedBy::OctaveAdjust);
      }

      prev_was_large = true;
    } else {
      prev_was_large = false;
    }
  }
}

/// @brief Shift note start ticks by an offset.
void shiftTicks(std::vector<NoteEvent>& notes, Tick offset) {
  for (auto& n : notes) {
    n.start_tick += offset;
  }
}

/// @brief Set voice ID on all notes.
void setVoice(std::vector<NoteEvent>& notes, uint8_t voice) {
  for (auto& n : notes) {
    n.voice = voice;
  }
}

/// @brief Choose a duration with dotted rhythm variety.
Tick chooseDuration(const CharacterParams& params, std::mt19937& rng,
                    Tick remaining = 0) {
  // Allowed duration sets based on movement tempo.
  // Fast (Allegro/Vivace): {120, 240, 480} — 16th, 8th, quarter.
  // Slow (Adagio): {240, 480, 960} — 8th, quarter, half.
  bool is_slow = (params.primary_dur >= kQuarterNote);
  static const Tick kFastDurs[] = {kSixteenthNote, kEighthNote, kQuarterNote};
  static const Tick kSlowDurs[] = {kEighthNote, kQuarterNote, kHalfNote};
  const Tick* durs = is_slow ? kSlowDurs : kFastDurs;

  // Filter to durations that fit within remaining time.
  Tick candidates[3];
  int count = 0;
  for (int i = 0; i < 3; ++i) {
    if (remaining == 0 || durs[i] <= remaining) {
      candidates[count++] = durs[i];
    }
  }
  if (count == 0) return 0;  // No duration fits — signal rest/skip.

  // Weighted selection: prefer primary duration.
  float roll = rng::rollFloat(rng, 0.0f, 1.0f);
  if (roll < params.secondary_prob && count >= 1) {
    return candidates[0];  // Shortest (secondary).
  } else if (count >= 3 && roll > 1.0f - 0.15f) {
    return candidates[2];  // Longest.
  }
  // Default: middle duration (or primary).
  return candidates[count >= 2 ? 1 : 0];
}

/// @brief Generate free figuration over harmonic events for the second half of a phrase.
///
/// Strong beats snap to chord tones; weak beats mix stepwise motion with
/// occasional skips (3rds) for interval variety. Contrary motion to the
/// previous melodic direction is preferred.
std::vector<NoteEvent> generateFiguration(Tick start_tick, Tick end_tick,
                                          const HarmonicTimeline& timeline,
                                          const CharacterParams& params,
                                          uint8_t range_low, uint8_t range_high,
                                          uint8_t voice, uint8_t last_pitch,
                                          Key key, bool is_minor,
                                          std::mt19937& rng) {
  std::vector<NoteEvent> notes;
  ScaleType scale = is_minor ? ScaleType::HarmonicMinor : ScaleType::Major;
  Tick current = start_tick;

  uint8_t prev0 = last_pitch;
  uint8_t prev1 = last_pitch;
  MelodicState mel_state;
  Tick total_duration = end_tick - start_tick;

  while (current < end_tick) {
    Tick remaining = end_tick - current;
    Tick dur = chooseDuration(params, rng, remaining);
    if (dur == 0) break;  // No valid duration fits — rest/skip.

    const HarmonicEvent& ev = timeline.getAt(current);
    // Only bar downbeats are "strong" (chord tone anchor).
    bool is_downbeat = (positionInBar(current) == 0);

    // Update phrase progress.
    if (total_duration > 0) {
      mel_state.phrase_progress =
          static_cast<float>(current - start_tick) / static_cast<float>(total_duration);
    }

    int direction = chooseMelodicDirection(mel_state, rng);
    bool ascending = (direction > 0);

    int abs_deg = scale_util::pitchToAbsoluteDegree(prev0, key, scale);

    uint8_t pitch;
    if (is_downbeat) {
      // Snap to chord tone, but avoid exact repetition.
      pitch = nearestChordTone(prev0, ev);
      if (pitch == prev0) {
        auto chord_tones = collectChordTonesInRange(ev.chord, range_low, range_high);
        if (chord_tones.size() > 1) {
          // Score candidates for best voice-leading.
          pitch = selectBestPitch(mel_state, prev0, chord_tones, current, true, rng);
        } else {
          // Shift by one scale step to avoid repetition.
          pitch = scale_util::absoluteDegreeToPitch(abs_deg + direction, key, scale);
        }
      }
    } else {
      int interval_size = chooseMelodicInterval(mel_state, rng);
      int step = interval_size * direction;
      pitch = scale_util::absoluteDegreeToPitch(abs_deg + step, key, scale);
    }

    // Anti-repetition: if pitch equals previous, force a step.
    if (pitch == prev0) {
      pitch = scale_util::absoluteDegreeToPitch(abs_deg + direction, key, scale);
      pitch = clampPitch(static_cast<int>(pitch), range_low, range_high);
      // If still same (at range boundary), try opposite direction.
      if (pitch == prev0) {
        pitch = scale_util::absoluteDegreeToPitch(abs_deg - direction, key, scale);
        pitch = clampPitch(static_cast<int>(pitch), range_low, range_high);
      }
    }
    pitch = clampPitch(static_cast<int>(pitch), range_low, range_high);

    NoteEvent note;
    note.start_tick = current;
    note.duration = dur;
    note.pitch = pitch;
    note.velocity = kOrganVelocity;
    note.voice = voice;
    note.source = BachNoteSource::FreeCounterpoint;
    notes.push_back(note);

    current += dur;
    updateMelodicState(mel_state, prev0, pitch);
    prev1 = prev0;
    prev0 = pitch;
  }
  return notes;
}

/// @brief Generate upper voice phrase (leader + follower with imitation).
///
/// Bars 1-2: Leader presents motif + diatonic sequence (Fortspinnung).
///           Follower enters with delayed imitation (transformed).
/// Bars 3-4: Free figuration anchored to chord tones.
///
/// @param phrase_start Start tick of the phrase.
/// @param motif The motivic material.
/// @param timeline Harmonic timeline.
/// @param params Character parameters.
/// @param leader_voice Voice ID for leader (0=RH, 1=LH).
/// @param key Current key.
/// @param is_minor Mode.
/// @param rng RNG.
/// @param out_leader Output: leader notes.
/// @param out_follower Output: follower notes.
void generateUpperVoicePhrase(Tick phrase_start, const std::vector<NoteEvent>& motif,
                              const HarmonicTimeline& timeline,
                              const CharacterParams& params, uint8_t leader_voice,
                              Key key, bool is_minor, std::mt19937& rng,
                              std::vector<NoteEvent>& out_leader,
                              std::vector<NoteEvent>& out_follower) {
  ScaleType scale = is_minor ? ScaleType::HarmonicMinor : ScaleType::Major;

  bool leader_is_rh = (leader_voice == 0);
  uint8_t leader_center = leader_is_rh ? kRhCenter : kLhCenter;
  uint8_t leader_low = leader_is_rh ? kRhLow : kLhLow;
  uint8_t leader_high = leader_is_rh ? kRhHigh : kLhHigh;

  uint8_t follower_center = leader_is_rh ? kLhCenter : kRhCenter;
  uint8_t follower_low = leader_is_rh ? kLhLow : kRhLow;
  uint8_t follower_high = leader_is_rh ? kLhHigh : kRhHigh;
  uint8_t follower_voice = leader_is_rh ? 1 : 0;

  // --- Leader: motif + Fortspinnung (bars 1-2) ---
  auto leader_motif = placeInRegister(motif, leader_center, leader_low, leader_high);
  clampExcessiveLeaps(leader_motif, 12, leader_center, phrase_start + kPhraseTicks);
  setVoice(leader_motif, leader_voice);
  shiftTicks(leader_motif, phrase_start);

  // Diatonic sequence (Fortspinnung) continuing after motif.
  Tick motif_dur = motifDuration(motif);
  Tick seq_start = phrase_start + motif_dur;
  Tick half_phrase = phrase_start + kPhraseTicks / 2;
  // Extend Fortspinnung boundary to 3/4 of phrase (compress figuration section).
  Tick fortspinnung_end = phrase_start + kPhraseTicks * 3 / 4;

  std::vector<NoteEvent> leader_seq;
  if (seq_start < fortspinnung_end) {
    // Allegro/Vivace: 45% chance of 2 repetitions; Adagio: always 1.
    bool is_adagio = (params.primary_dur >= kQuarterNote);
    int reps = 1;
    if (!is_adagio && rng::rollProbability(rng, 0.45f)) {
      reps = 2;
    }

    // Direction: 65% descending, 35% ascending.
    int direction = rng::rollProbability(rng, 0.65f) ? -1 : 1;

    auto seq = generateDiatonicSequence(leader_motif, reps, direction, seq_start, key, scale);

    for (auto& n : seq) {
      n.pitch = clampPitch(static_cast<int>(n.pitch), leader_low, leader_high);
      n.modified_by |= static_cast<uint8_t>(NoteModifiedBy::OctaveAdjust);
      n.voice = leader_voice;

      if (n.start_tick < fortspinnung_end) {
        if (n.start_tick + n.duration > fortspinnung_end) {
          n.duration = fortspinnung_end - n.start_tick;
          n.modified_by |= static_cast<uint8_t>(NoteModifiedBy::OverlapTrim);
        }
        leader_seq.push_back(n);
      }
    }

    // Parallel P5/P8 heuristic: check consecutive pairs in sequence for same interval.
    if (leader_seq.size() >= 4) {
      bool has_parallel = false;
      for (size_t i = 2; i < leader_seq.size(); ++i) {
        int ivl_prev = interval_util::compoundToSimple(
            static_cast<int>(leader_seq[i - 1].pitch) -
            static_cast<int>(leader_seq[i - 2].pitch));
        int ivl_curr = interval_util::compoundToSimple(
            static_cast<int>(leader_seq[i].pitch) -
            static_cast<int>(leader_seq[i - 1].pitch));
        if ((ivl_prev == interval::kPerfect5th && ivl_curr == interval::kPerfect5th) ||
            (ivl_prev == interval::kUnison && ivl_curr == interval::kUnison) ||
            (ivl_prev == interval::kOctave && ivl_curr == interval::kOctave)) {
          // Truncate from this point.
          leader_seq.resize(i);
          has_parallel = true;
          break;
        }
      }
      (void)has_parallel;
    }
  }

  // --- Follower: delayed imitation (bars 1-2) ---
  // Compute imitation offset from motif duration.
  Tick raw_offset = motif_dur / 2;
  Tick imitation_offset =
      ((raw_offset + kEighthNote) / kQuarterNote) * kQuarterNote;
  if (imitation_offset < kQuarterNote) imitation_offset = kQuarterNote;

  // Choose transformation.
  enum TransformType { Direct, Inversion, Diminution, Retrograde };
  TransformType transform = rng::selectWeighted(
      rng,
      std::vector<TransformType>{Direct, Inversion, Diminution, Retrograde},
      std::vector<float>{0.40f, 0.30f, 0.15f, 0.15f});

  std::vector<NoteEvent> follower_imitation;
  switch (transform) {
    case Direct:
      follower_imitation = transposeMelodyDiatonic(motif, 0, key, scale);
      break;
    case Inversion: {
      uint8_t pivot = motif.empty() ? 60 : motif[0].pitch;
      follower_imitation = invertMelodyDiatonic(motif, pivot, key, scale);
      break;
    }
    case Diminution:
      follower_imitation = diminishMelody(motif, 0);
      break;
    case Retrograde:
      follower_imitation = retrogradeMelody(motif, 0);
      break;
  }

  follower_imitation =
      placeInRegister(follower_imitation, follower_center, follower_low, follower_high);
  clampExcessiveLeaps(follower_imitation, 12, follower_center, phrase_start + kPhraseTicks);
  setVoice(follower_imitation, follower_voice);
  shiftTicks(follower_imitation, phrase_start + imitation_offset);

  // Trim follower notes that exceed half phrase.
  for (auto& n : follower_imitation) {
    if (n.start_tick + n.duration > half_phrase) {
      n.duration = (n.start_tick < half_phrase) ? (half_phrase - n.start_tick) : 0;
      n.modified_by |= static_cast<uint8_t>(NoteModifiedBy::OverlapTrim);
    }
  }
  follower_imitation.erase(
      std::remove_if(follower_imitation.begin(), follower_imitation.end(),
                     [half_phrase](const NoteEvent& n) {
                       return n.start_tick >= half_phrase || n.duration == 0;
                     }),
      follower_imitation.end());

  // --- Free figuration for remaining phrase ---
  // Leader figuration starts after Fortspinnung; follower after imitation half.
  Tick leader_fig_start = fortspinnung_end;
  if (!leader_seq.empty()) {
    Tick seq_end = leader_seq.back().start_tick + leader_seq.back().duration;
    if (seq_end > leader_fig_start) leader_fig_start = seq_end;
  }

  uint8_t leader_last = leader_motif.empty() ? leader_center : leader_motif.back().pitch;
  if (!leader_seq.empty()) leader_last = leader_seq.back().pitch;

  uint8_t follower_last =
      follower_imitation.empty() ? follower_center : follower_imitation.back().pitch;

  Tick phrase_end = phrase_start + kPhraseTicks;

  auto leader_fig = generateFiguration(leader_fig_start, phrase_end, timeline, params,
                                       leader_low, leader_high, leader_voice,
                                       leader_last, key, is_minor, rng);

  auto follower_fig = generateFiguration(half_phrase, phrase_end, timeline, params,
                                         follower_low, follower_high, follower_voice,
                                         follower_last, key, is_minor, rng);

  // Assemble leader output.
  out_leader.insert(out_leader.end(), leader_motif.begin(), leader_motif.end());
  out_leader.insert(out_leader.end(), leader_seq.begin(), leader_seq.end());
  out_leader.insert(out_leader.end(), leader_fig.begin(), leader_fig.end());

  // Assemble follower output.
  out_follower.insert(out_follower.end(), follower_imitation.begin(),
                      follower_imitation.end());
  out_follower.insert(out_follower.end(), follower_fig.begin(), follower_fig.end());
}

// ---------------------------------------------------------------------------
// Step 4: Walking bass / thematic bass
// ---------------------------------------------------------------------------

/// @brief Generate a walking bass line for one phrase.
///
/// Beat 1: chord root. Beat 2: passing tone toward beat 3 target.
/// Beat 3: 5th (60%) / 3rd (25%) / passing (15%).
/// Beat 4: approach tone toward next chord root.
///
/// @param phrase_start Start tick of phrase.
/// @param phrase_end End tick of phrase.
/// @param timeline Harmonic timeline.
/// @param params Character parameters.
/// @param key Current key.
/// @param is_minor Mode.
/// @param rng RNG.
/// @return Pedal voice notes.
std::vector<NoteEvent> generateWalkingBass(Tick phrase_start, Tick phrase_end,
                                           const HarmonicTimeline& timeline,
                                           const CharacterParams& params,
                                           Key key, bool is_minor,
                                           std::mt19937& rng) {
  std::vector<NoteEvent> notes;
  ScaleType scale = is_minor ? ScaleType::HarmonicMinor : ScaleType::Major;
  Tick pedal_dur = static_cast<Tick>(params.pedal_dur);

  for (Tick bar_start = phrase_start; bar_start < phrase_end;
       bar_start += kTicksPerBar) {
    const HarmonicEvent& ev = timeline.getAt(bar_start);
    uint8_t root = clampPitch(static_cast<int>(ev.bass_pitch),
                              organ_range::kPedalLow, organ_range::kPedalHigh);

    // Compute 5th and 3rd within pedal range.
    auto chord_tones = collectChordTonesInRange(
        ev.chord, organ_range::kPedalLow, organ_range::kPedalHigh);
    uint8_t fifth = root;
    uint8_t third = root;
    if (!chord_tones.empty()) {
      // Find the tone closest to root+7 (perfect 5th).
      int target5 = static_cast<int>(root) + interval::kPerfect5th;
      int target3 = static_cast<int>(root) + interval::kMajor3rd;
      int best5_dist = 127;
      int best3_dist = 127;
      for (uint8_t ct : chord_tones) {
        int d5 = std::abs(static_cast<int>(ct) - target5);
        if (d5 < best5_dist) { best5_dist = d5; fifth = ct; }
        int d3 = std::abs(static_cast<int>(ct) - target3);
        if (d3 < best3_dist) { best3_dist = d3; third = ct; }
      }
    }

    if (pedal_dur >= kHalfNote) {
      // Slow movement: 20% chance of 4 quarter notes, 80% chance of 2 half notes.
      if (rng::rollProbability(rng, 0.20f)) {
        // Use 4 quarter notes for variety (same logic as fast movement).
        NoteEvent bq1;
        bq1.start_tick = bar_start;
        bq1.duration = kQuarterNote;
        bq1.pitch = root;
        bq1.velocity = kOrganVelocity;
        bq1.voice = 2;
        bq1.source = BachNoteSource::PedalPoint;
        notes.push_back(bq1);

        // Beat 2: scale step between root and target.
        int abs_root_q = scale_util::pitchToAbsoluteDegree(root, key, scale);
        uint8_t bq2_pitch = scale_util::absoluteDegreeToPitch(abs_root_q + 1, key, scale);
        bq2_pitch = clampPitch(static_cast<int>(bq2_pitch),
                               organ_range::kPedalLow, organ_range::kPedalHigh);
        NoteEvent bq2;
        bq2.start_tick = bar_start + kQuarterNote;
        bq2.duration = kQuarterNote;
        bq2.pitch = bq2_pitch;
        bq2.velocity = kOrganVelocity;
        bq2.voice = 2;
        bq2.source = BachNoteSource::PedalPoint;
        notes.push_back(bq2);

        NoteEvent bq3;
        bq3.start_tick = bar_start + 2 * kQuarterNote;
        bq3.duration = kQuarterNote;
        bq3.pitch = fifth;
        bq3.velocity = kOrganVelocity;
        bq3.voice = 2;
        bq3.source = BachNoteSource::PedalPoint;
        notes.push_back(bq3);

        // Beat 4: approach.
        uint8_t approach_q = root;
        Tick next_bar_q = bar_start + kTicksPerBar;
        if (next_bar_q < phrase_end) {
          const HarmonicEvent& next_ev_q = timeline.getAt(next_bar_q);
          uint8_t next_root_q = clampPitch(static_cast<int>(next_ev_q.bass_pitch),
                                           organ_range::kPedalLow,
                                           organ_range::kPedalHigh);
          int next_abs_q = scale_util::pitchToAbsoluteDegree(next_root_q, key, scale);
          approach_q = scale_util::absoluteDegreeToPitch(next_abs_q - 1, key, scale);
          approach_q = clampPitch(static_cast<int>(approach_q),
                                  organ_range::kPedalLow, organ_range::kPedalHigh);
        }
        NoteEvent bq4;
        bq4.start_tick = bar_start + 3 * kQuarterNote;
        bq4.duration = kQuarterNote;
        bq4.pitch = approach_q;
        bq4.velocity = kOrganVelocity;
        bq4.voice = 2;
        bq4.source = BachNoteSource::PedalPoint;
        notes.push_back(bq4);
      } else {
        // Standard 2 half notes.
        NoteEvent n1;
        n1.start_tick = bar_start;
        n1.duration = kHalfNote;
        n1.pitch = root;
        n1.velocity = kOrganVelocity;
        n1.voice = 2;
        n1.source = BachNoteSource::PedalPoint;
        notes.push_back(n1);

        // Second half: 5th or approach.
        uint8_t p2 = fifth;
        Tick next_bar = bar_start + kTicksPerBar;
        if (next_bar < phrase_end) {
          const HarmonicEvent& next_ev = timeline.getAt(next_bar);
          uint8_t next_root = clampPitch(static_cast<int>(next_ev.bass_pitch),
                                         organ_range::kPedalLow,
                                         organ_range::kPedalHigh);
          if (next_root != root) {
            int abs_deg = scale_util::pitchToAbsoluteDegree(next_root, key, scale);
            p2 = scale_util::absoluteDegreeToPitch(abs_deg - 1, key, scale);
            p2 = clampPitch(static_cast<int>(p2),
                            organ_range::kPedalLow, organ_range::kPedalHigh);
          }
        }

        NoteEvent n2;
        n2.start_tick = bar_start + kHalfNote;
        n2.duration = kHalfNote;
        n2.pitch = p2;
        n2.velocity = kOrganVelocity;
        n2.voice = 2;
        n2.source = BachNoteSource::PedalPoint;
        notes.push_back(n2);
      }
    } else {
      // Fast movement: 4 quarter notes.
      // Beat 1: root.
      NoteEvent b1;
      b1.start_tick = bar_start;
      b1.duration = kQuarterNote;
      b1.pitch = root;
      b1.velocity = kOrganVelocity;
      b1.voice = 2;
      b1.source = BachNoteSource::PedalPoint;
      notes.push_back(b1);

      // Beat 3 target.
      float r3 = rng::rollFloat(rng, 0.0f, 1.0f);
      uint8_t beat3_pitch;
      if (r3 < 0.60f) {
        beat3_pitch = fifth;
      } else if (r3 < 0.85f) {
        beat3_pitch = third;
      } else {
        // Passing tone.
        int abs_root = scale_util::pitchToAbsoluteDegree(root, key, scale);
        beat3_pitch =
            scale_util::absoluteDegreeToPitch(abs_root + 2, key, scale);
        beat3_pitch = clampPitch(static_cast<int>(beat3_pitch),
                                 organ_range::kPedalLow, organ_range::kPedalHigh);
      }

      // Beat 2: passing tone between root and beat 3 target.
      int abs_root = scale_util::pitchToAbsoluteDegree(root, key, scale);
      int abs_b3 = scale_util::pitchToAbsoluteDegree(beat3_pitch, key, scale);
      int mid_deg = (abs_root + abs_b3) / 2;
      if (mid_deg == abs_root) mid_deg = abs_root + 1;
      uint8_t beat2_pitch = scale_util::absoluteDegreeToPitch(mid_deg, key, scale);
      beat2_pitch = clampPitch(static_cast<int>(beat2_pitch),
                               organ_range::kPedalLow, organ_range::kPedalHigh);

      NoteEvent b2;
      b2.start_tick = bar_start + kQuarterNote;
      b2.duration = kQuarterNote;
      b2.pitch = beat2_pitch;
      b2.velocity = kOrganVelocity;
      b2.voice = 2;
      b2.source = BachNoteSource::PedalPoint;
      notes.push_back(b2);

      NoteEvent b3;
      b3.start_tick = bar_start + 2 * kQuarterNote;
      b3.duration = kQuarterNote;
      b3.pitch = beat3_pitch;
      b3.velocity = kOrganVelocity;
      b3.voice = 2;
      b3.source = BachNoteSource::PedalPoint;
      notes.push_back(b3);

      // Beat 4: approach tone toward next bar's root.
      uint8_t approach = root;
      Tick next_bar = bar_start + kTicksPerBar;
      if (next_bar < phrase_end) {
        const HarmonicEvent& next_ev = timeline.getAt(next_bar);
        uint8_t next_root = clampPitch(static_cast<int>(next_ev.bass_pitch),
                                       organ_range::kPedalLow,
                                       organ_range::kPedalHigh);
        // Approach from one scale step below.
        int next_abs = scale_util::pitchToAbsoluteDegree(next_root, key, scale);
        approach = scale_util::absoluteDegreeToPitch(next_abs - 1, key, scale);
        approach = clampPitch(static_cast<int>(approach),
                              organ_range::kPedalLow, organ_range::kPedalHigh);
      } else {
        // Last bar: use leading tone approach.
        int root_abs = scale_util::pitchToAbsoluteDegree(root, key, scale);
        approach = scale_util::absoluteDegreeToPitch(root_abs - 1, key, scale);
        approach = clampPitch(static_cast<int>(approach),
                              organ_range::kPedalLow, organ_range::kPedalHigh);
      }

      NoteEvent b4;
      b4.start_tick = bar_start + 3 * kQuarterNote;
      b4.duration = kQuarterNote;
      b4.pitch = approach;
      b4.velocity = kOrganVelocity;
      b4.voice = 2;
      b4.source = BachNoteSource::PedalPoint;
      notes.push_back(b4);
    }
  }
  return notes;
}

/// @brief Generate thematic bass: augmented motif in pedal register.
std::vector<NoteEvent> generateThematicBass(Tick phrase_start, Tick phrase_end,
                                            const std::vector<NoteEvent>& motif,
                                            const HarmonicTimeline& timeline,
                                            Key /*key*/, bool /*is_minor*/,
                                            std::mt19937& /*rng*/) {
  // Augment motif by factor 2 and transpose to pedal register.
  auto bass_motif = augmentMelody(motif, 0, 2);
  bass_motif = transposeMelody(bass_motif, -24);  // 2 octaves down.

  // Place in pedal register.
  for (auto& n : bass_motif) {
    n.pitch = clampPitch(static_cast<int>(n.pitch),
                         organ_range::kPedalLow, organ_range::kPedalHigh);
    n.modified_by |= static_cast<uint8_t>(NoteModifiedBy::OctaveAdjust);
    n.voice = 2;
    n.source = BachNoteSource::PedalPoint;
  }
  shiftTicks(bass_motif, phrase_start);

  // Trim to phrase boundary.
  bass_motif.erase(
      std::remove_if(bass_motif.begin(), bass_motif.end(),
                     [phrase_end](const NoteEvent& n) {
                       return n.start_tick >= phrase_end;
                     }),
      bass_motif.end());

  for (auto& n : bass_motif) {
    if (n.start_tick + n.duration > phrase_end) {
      n.duration = phrase_end - n.start_tick;
      n.modified_by |= static_cast<uint8_t>(NoteModifiedBy::OverlapTrim);
    }
  }

  // If thematic bass is too short, fill remaining with walking bass pattern.
  Tick thematic_end = phrase_start;
  if (!bass_motif.empty()) {
    const auto& last = bass_motif.back();
    thematic_end = last.start_tick + last.duration;
  }

  if (thematic_end < phrase_end) {
    // Simple fill: hold root.
    const HarmonicEvent& ev = timeline.getAt(thematic_end);
    uint8_t root = clampPitch(static_cast<int>(ev.bass_pitch),
                              organ_range::kPedalLow, organ_range::kPedalHigh);
    NoteEvent fill;
    fill.start_tick = thematic_end;
    fill.duration = phrase_end - thematic_end;
    fill.pitch = root;
    fill.velocity = kOrganVelocity;
    fill.voice = 2;
    fill.source = BachNoteSource::PedalPoint;
    bass_motif.push_back(fill);
  }

  return bass_motif;
}

// ---------------------------------------------------------------------------
// Step 5b: Cadential suspension insertion
// ---------------------------------------------------------------------------

/// @brief Attempt to insert a cadential suspension before a cadence point.
///
/// Tries Sus4_3 first, then Sus7_6 as fallback. Each candidate is checked for
/// melodic legality (resolution within 4 semitones of original) and voice
/// crossing. Returns true if a suspension was inserted.
///
/// @param tracks All 3 tracks (modified in-place).
/// @param cadence_tick Tick position of the cadence.
/// @param leader_voice Voice index for the suspension (0=RH, 1=LH).
/// @param key Current key.
/// @param scale Scale type.
/// @return True if suspension was inserted.
bool insertCadentialSuspension(std::vector<Track>& tracks, Tick cadence_tick,
                                uint8_t leader_voice, Key key, ScaleType scale) {
  if (cadence_tick < kTicksPerBar) return false;
  Tick search_start = cadence_tick - kTicksPerBar;

  auto& notes = tracks[leader_voice].notes;
  if (notes.empty()) return false;

  // Find the last note in the search window.
  int target_idx = -1;
  for (int i = static_cast<int>(notes.size()) - 1; i >= 0; --i) {
    if (notes[i].start_tick >= search_start && notes[i].start_tick < cadence_tick) {
      target_idx = i;
      break;
    }
  }
  if (target_idx < 0) return false;

  uint8_t orig_pitch = notes[target_idx].pitch;
  uint8_t range_low = (leader_voice == 0) ? kRhLow : kLhLow;
  uint8_t range_high = (leader_voice == 0) ? kRhHigh : kLhHigh;

  // Find the other upper voice pitch at the same tick for crossing check.
  uint8_t other_voice = (leader_voice == 0) ? 1 : 0;
  uint8_t other_pitch = 0;
  bool has_other = false;
  for (const auto& n : tracks[other_voice].notes) {
    if (n.start_tick <= notes[target_idx].start_tick &&
        n.start_tick + n.duration > notes[target_idx].start_tick) {
      other_pitch = n.pitch;
      has_other = true;
      break;
    }
  }

  // Suspension candidates: Sus4_3 (up 1 degree, resolve down), Sus7_6 (up 4 degrees, resolve down).
  struct SusCand {
    int sus_degrees;   // Scale degrees above original for suspension.
    int res_degrees;   // Scale degrees for resolution (negative = down).
  };
  static constexpr SusCand kCandidates[] = {{1, -1}, {4, -1}};

  int abs_deg = scale_util::pitchToAbsoluteDegree(orig_pitch, key, scale);

  for (const auto& cand : kCandidates) {
    uint8_t sus_pitch = scale_util::absoluteDegreeToPitch(abs_deg + cand.sus_degrees, key, scale);
    uint8_t res_pitch = scale_util::absoluteDegreeToPitch(abs_deg + cand.sus_degrees + cand.res_degrees, key, scale);

    // Melodic legality: resolution within 4 semitones of original.
    if (absoluteInterval(res_pitch, orig_pitch) > 4) continue;

    // Range check.
    if (sus_pitch < range_low || sus_pitch > range_high) continue;
    if (res_pitch < range_low || res_pitch > range_high) continue;

    // Voice crossing check.
    if (has_other) {
      if (leader_voice == 0 && sus_pitch < other_pitch) continue;  // RH below LH.
      if (leader_voice == 1 && sus_pitch > other_pitch) continue;  // LH above RH.
    }

    // Apply: extend note duration to cadence_tick, set pitch to sus_pitch.
    notes[target_idx].pitch = sus_pitch;
    notes[target_idx].modified_by |= static_cast<uint8_t>(NoteModifiedBy::ChordToneSnap);
    Tick new_dur = cadence_tick - notes[target_idx].start_tick;
    if (new_dur > 0) {
      notes[target_idx].duration = new_dur;
      notes[target_idx].modified_by |= static_cast<uint8_t>(NoteModifiedBy::OverlapTrim);
    }

    // Insert resolution note at cadence_tick.
    NoteEvent res_note;
    res_note.start_tick = cadence_tick;
    res_note.duration = kQuarterNote;
    res_note.pitch = res_pitch;
    res_note.velocity = kOrganVelocity;
    res_note.voice = leader_voice;
    res_note.source = BachNoteSource::FreeCounterpoint;
    notes.push_back(res_note);
    return true;
  }

  return false;  // No candidate succeeded — safe no-op.
}

// ---------------------------------------------------------------------------
// Step 5c: Breathing rest insertion
// ---------------------------------------------------------------------------

/// @brief Insert breathing rests at phrase boundaries for upper voices.
///
/// Truncates upper voice notes (voice 0 and 1) that extend into the last
/// sixteenth note before each phrase boundary. Pedal (voice 2) is exempt
/// (organ pedal sustains across phrase boundaries per BWV 525-530 practice).
void insertBreathingRests(std::vector<Track>& tracks, Tick num_phrases, Tick duration) {
  for (Tick p = 1; p < num_phrases; ++p) {
    Tick boundary = p * kPhraseTicks;
    if (boundary > duration) break;
    Tick breath_start = boundary - kSixteenthNote;

    // Only upper voices (tracks 0 and 1).
    for (size_t trk = 0; trk < 2 && trk < tracks.size(); ++trk) {
      for (auto& note : tracks[trk].notes) {
        if (note.start_tick < breath_start &&
            note.start_tick + note.duration > breath_start) {
          note.duration = breath_start - note.start_tick;
          note.modified_by |= static_cast<uint8_t>(NoteModifiedBy::Articulation);
        }
      }
    }
  }
}

// ---------------------------------------------------------------------------
// Step 5d: Non-harmonic tone validation (post-processing)
// ---------------------------------------------------------------------------

/// @brief Check if a pitch is a chord tone of the given harmonic event.
bool isChordToneAt(uint8_t pitch, const HarmonicEvent& ev) {
  int pc = getPitchClass(pitch);
  auto tones = collectChordTonesInRange(ev.chord, 0, 127);
  for (uint8_t ct : tones) {
    if (getPitchClass(ct) == pc) return true;
  }
  return false;
}

/// @brief Validate non-harmonic tones and snap invalid weak-beat dissonances.
///
/// For each upper voice track:
/// - Strong beats (0, 2): non-chord tones are snapped to nearest chord tone.
/// - Weak beats: checked via SpeciesRules (Fifth species) for valid passing/neighbor.
///   If invalid and harmonic context is unstable, snap to chord tone.
void validateNonHarmonicTones(std::vector<Track>& tracks,
                               const HarmonicTimeline& timeline,
                               Key /*key*/, ScaleType /*scale*/) {

  for (size_t trk = 0; trk < 2 && trk < tracks.size(); ++trk) {
    auto& notes = tracks[trk].notes;
    for (size_t i = 0; i < notes.size(); ++i) {
      const HarmonicEvent& ev = timeline.getAt(notes[i].start_tick);
      bool is_ct = isChordToneAt(notes[i].pitch, ev);

      if (is_ct) continue;  // Chord tones are always fine.

      uint8_t beat = beatInBar(notes[i].start_tick);

      if (beat == 0) {
        // Downbeat: snap only for harsh bass dissonances (2nd, tritone).
        for (const auto& bn : tracks[2].notes) {
          if (bn.start_tick <= notes[i].start_tick &&
              bn.start_tick + bn.duration > notes[i].start_tick) {
            int ivl = interval_util::compoundToSimple(
                static_cast<int>(notes[i].pitch) - static_cast<int>(bn.pitch));
            if (ivl == interval::kMinor2nd || ivl == interval::kMajor2nd ||
                ivl == interval::kTritone) {
              notes[i].pitch = nearestChordTone(notes[i].pitch, ev);
              notes[i].modified_by |= static_cast<uint8_t>(NoteModifiedBy::ChordToneSnap);
            }
            break;
          }
        }
        continue;
      }

      // Weak beat: validate with species rules. Snap only unclassified
      // dissonances that are also dissonant with the bass.
      uint8_t prev_pitch = (i > 0) ? notes[i - 1].pitch : notes[i].pitch;
      uint8_t next_pitch = (i + 1 < notes.size()) ? notes[i + 1].pitch : notes[i].pitch;

      bool prev_is_ct = (i > 0) ? isChordToneAt(prev_pitch, ev) : true;
      bool next_is_ct = (i + 1 < notes.size())
                             ? isChordToneAt(next_pitch, timeline.getAt(notes[i + 1].start_tick))
                             : true;

      auto nht_type = classifyNonHarmonicTone(prev_pitch, notes[i].pitch, next_pitch,
                                               false, prev_is_ct, next_is_ct);

      if (nht_type == NonHarmonicToneType::Unknown) {
        // Only snap if also dissonant with bass (2nd, tritone).
        for (const auto& bn : tracks[2].notes) {
          if (bn.start_tick <= notes[i].start_tick &&
              bn.start_tick + bn.duration > notes[i].start_tick) {
            int ivl = interval_util::compoundToSimple(
                static_cast<int>(notes[i].pitch) - static_cast<int>(bn.pitch));
            if (ivl == interval::kMinor2nd || ivl == interval::kMajor2nd ||
                ivl == interval::kTritone) {
              notes[i].pitch = nearestChordTone(notes[i].pitch, ev);
              notes[i].modified_by |= static_cast<uint8_t>(NoteModifiedBy::ChordToneSnap);
            }
            break;
          }
        }
      }
    }
  }
}

// ---------------------------------------------------------------------------
// Step 6: Invertible counterpoint
// ---------------------------------------------------------------------------

/// @brief Swap registers of two voice groups for invertible counterpoint.
void swapVoiceRegisters(std::vector<NoteEvent>& upper_notes,
                        std::vector<NoteEvent>& lower_notes,
                        uint8_t upper_center, uint8_t upper_low,
                        uint8_t upper_high, uint8_t lower_center,
                        uint8_t lower_low, uint8_t lower_high) {
  // Move upper notes to lower register and vice versa.
  for (auto& n : upper_notes) {
    int offset = static_cast<int>(n.pitch) - static_cast<int>(upper_center);
    int new_pitch = static_cast<int>(lower_center) + offset;
    n.pitch = clampPitch(new_pitch, lower_low, lower_high);
    n.modified_by |= static_cast<uint8_t>(NoteModifiedBy::OctaveAdjust);
  }
  for (auto& n : lower_notes) {
    int offset = static_cast<int>(n.pitch) - static_cast<int>(lower_center);
    int new_pitch = static_cast<int>(upper_center) + offset;
    n.pitch = clampPitch(new_pitch, upper_low, upper_high);
    n.modified_by |= static_cast<uint8_t>(NoteModifiedBy::OctaveAdjust);
  }
}

// ---------------------------------------------------------------------------
// Step 6a: Pedal lead simplification
// ---------------------------------------------------------------------------

/// @brief Simplify upper voice notes during thematic bass (pedal lead) phrases.
///
/// Replaces the first 2 bars of upper voice material with half-note chord tone
/// outlines, letting the pedal lead be heard clearly.
void simplifyForPedalLead(std::vector<NoteEvent>& upper_notes, Tick phrase_start,
                           Tick phrase_end, const HarmonicTimeline& timeline,
                           uint8_t range_low, uint8_t range_high, uint8_t voice) {
  Tick simplify_end = phrase_start + 2 * kTicksPerBar;
  if (simplify_end > phrase_end) simplify_end = phrase_end;

  // Remove upper voice notes in the first 2 bars.
  upper_notes.erase(
      std::remove_if(upper_notes.begin(), upper_notes.end(),
                     [phrase_start, simplify_end, voice](const NoteEvent& n) {
                       return n.voice == voice &&
                              n.start_tick >= phrase_start &&
                              n.start_tick < simplify_end;
                     }),
      upper_notes.end());

  // Insert half-note chord tone outlines.
  uint8_t center = (range_low + range_high) / 2;
  for (Tick t = phrase_start; t < simplify_end; t += kHalfNote) {
    const HarmonicEvent& ev = timeline.getAt(t);
    uint8_t pitch = nearestChordTone(center, ev);
    pitch = clampPitch(static_cast<int>(pitch), range_low, range_high);

    // Vary pitch: alternate between two nearby chord tones.
    if (((t - phrase_start) / kHalfNote) % 2 == 1) {
      auto chord_tones = collectChordTonesInRange(ev.chord, range_low, range_high);
      for (uint8_t ct : chord_tones) {
        if (ct != pitch) {
          pitch = ct;
          break;
        }
      }
    }

    Tick dur = kHalfNote;
    if (t + dur > simplify_end) dur = simplify_end - t;

    NoteEvent note;
    note.start_tick = t;
    note.duration = dur;
    note.pitch = pitch;
    note.velocity = kOrganVelocity;
    note.voice = voice;
    note.source = BachNoteSource::FreeCounterpoint;
    upper_notes.push_back(note);
  }
}

// ---------------------------------------------------------------------------
// Step 6b: Strong-beat P4 over bass counting
// ---------------------------------------------------------------------------

/// @brief Count strong-beat perfect 4ths between upper voices and bass.
///
/// In trio sonata texture, a perfect 4th between an upper voice and the bass
/// on a strong beat sounds unstable and should be minimized.
// ---------------------------------------------------------------------------
// Post-pass: Minimum voice separation between RH and LH
// ---------------------------------------------------------------------------

/// @brief Enforce minimum semitone separation between simultaneously sounding
///        RH and LH notes. Shifts RH up or LH down by an octave when too close.
/// @param tracks The 3-element track vector (RH=0, LH=1, Pedal=2).
/// @param min_semitones Minimum interval in semitones (default 12).
void enforceMinimumVoiceSeparation(std::vector<Track>& tracks,
                                   int min_semitones = 12) {
  if (tracks.size() < 2) return;
  auto& rh_notes = tracks[0].notes;
  auto& lh_notes = tracks[1].notes;

  // Max 2 iterations to converge.
  for (int iteration = 0; iteration < 2; ++iteration) {
    bool any_changed = false;
    for (auto& rh : rh_notes) {
      Tick rh_end = rh.start_tick + rh.duration;
      for (auto& lh : lh_notes) {
        Tick lh_end = lh.start_tick + lh.duration;
        // Check if notes overlap in time.
        if (lh.start_tick >= rh_end || rh.start_tick >= lh_end) continue;

        int interval = static_cast<int>(rh.pitch) - static_cast<int>(lh.pitch);
        if (interval >= min_semitones) continue;

        // RH should be above LH; try shifting RH up first.
        int rh_candidate = static_cast<int>(rh.pitch) + 12;
        if (rh_candidate <= static_cast<int>(kRhHigh)) {
          rh.pitch = static_cast<uint8_t>(rh_candidate);
          rh.modified_by |= static_cast<uint8_t>(NoteModifiedBy::OctaveAdjust);
          any_changed = true;
        } else {
          // RH at ceiling — shift LH down.
          int lh_candidate = static_cast<int>(lh.pitch) - 12;
          if (lh_candidate >= static_cast<int>(kLhLow)) {
            lh.pitch = static_cast<uint8_t>(lh_candidate);
            lh.modified_by |= static_cast<uint8_t>(NoteModifiedBy::OctaveAdjust);
            any_changed = true;
          }
        }
      }
    }
    if (!any_changed) break;
  }
}

// ---------------------------------------------------------------------------
// Post-pass: Enforce diatonic pitches in major movements
// ---------------------------------------------------------------------------

/// @brief Snap non-diatonic pitches to the nearest diatonic pitch in major
///        movements. Skipped for minor movements (G# leading tone is valid).
/// @param tracks The 3-element track vector.
/// @param key The tonic key.
/// @param is_minor Whether the movement is in a minor key.
void enforceDiatonicPitches(std::vector<Track>& tracks, Key key, bool is_minor) {
  if (is_minor) return;  // Minor keys allow chromatic alterations (leading tone).

  ScaleType scale = ScaleType::Major;
  for (auto& track : tracks) {
    for (auto& note : track.notes) {
      if (!scale_util::isScaleTone(note.pitch, key, scale)) {
        uint8_t snapped = scale_util::nearestScaleTone(note.pitch, key, scale);
        // Clamp to voice range.
        uint8_t low, high;
        if (note.voice == 0) {
          low = kRhLow; high = kRhHigh;
        } else if (note.voice == 1) {
          low = kLhLow; high = kLhHigh;
        } else {
          low = organ_range::kPedalLow; high = organ_range::kPedalHigh;
        }
        note.pitch = clampPitch(static_cast<int>(snapped), low, high);
        note.modified_by |= static_cast<uint8_t>(NoteModifiedBy::ChordToneSnap);
      }
    }
  }
}

// ---------------------------------------------------------------------------
// Post-pass: Enforce consonance on strong beats
// ---------------------------------------------------------------------------



/// @brief Enforce consonance on strong beats (bar downbeat and beat 3).
///
/// Two-stage fix:
/// 1. Chord tone snap: if nearest chord tone is ≤2 semitones away, snap to it.
/// 2. Simultaneous clash resolution: fix {m2, tritone, M7} between voice pairs.
void enforceStrongBeatConsonance(std::vector<Track>& tracks,
                                 const HarmonicTimeline& timeline,
                                 Key /*key*/, bool /*is_minor*/) {
  if (tracks.size() < 2) return;

  // Collect all strong beat ticks up to max duration.
  Tick max_dur = 0;
  for (const auto& track : tracks) {
    for (const auto& n : track.notes) {
      Tick end = n.start_tick + n.duration;
      if (end > max_dur) max_dur = end;
    }
  }

  for (Tick tick = 0; tick < max_dur; tick += kTicksPerBar / 2) {
    const HarmonicEvent& ev = timeline.getAt(tick);

    // Collect sounding notes at this tick for each track.
    struct SoundingNote {
      NoteEvent* note;
      size_t track_idx;
    };
    std::vector<SoundingNote> sounding;

    for (size_t trk = 0; trk < tracks.size() && trk < 3; ++trk) {
      for (auto& n : tracks[trk].notes) {
        if (n.start_tick <= tick && n.start_tick + n.duration > tick) {
          sounding.push_back({&n, trk});
          break;  // One note per track at this tick.
        }
      }
    }

    if (sounding.size() < 2) continue;

    // Check all pairs for dissonance and fix.
    for (size_t i = 0; i < sounding.size(); ++i) {
      for (size_t j = i + 1; j < sounding.size(); ++j) {
        int interval = interval_util::compoundToSimple(
            static_cast<int>(sounding[i].note->pitch) -
            static_cast<int>(sounding[j].note->pitch));
        if (interval_util::isConsonance(interval)) continue;

        // Fix the upper voice (lower track index = higher register in trio).
        // Try: snap to nearest chord tone, then octave shift.
        size_t fix_idx = (sounding[i].track_idx < sounding[j].track_idx) ? i : j;
        size_t other_idx = (fix_idx == i) ? j : i;
        NoteEvent* fix_note = sounding[fix_idx].note;
        NoteEvent* other_note = sounding[other_idx].note;
        size_t fix_trk = sounding[fix_idx].track_idx;

        uint8_t low = (fix_trk == 0) ? kRhLow
                     : (fix_trk == 1) ? kLhLow
                                      : organ_range::kPedalLow;
        uint8_t high = (fix_trk == 0) ? kRhHigh
                      : (fix_trk == 1) ? kLhHigh
                                       : organ_range::kPedalHigh;

        // Strategy 1: Snap to nearest chord tone within ±3 semitones.
        uint8_t nearest = nearestChordTone(fix_note->pitch, ev);
        int dist = absoluteInterval(nearest, fix_note->pitch);
        if (dist <= 3 && nearest >= low && nearest <= high) {
          int new_ivl = interval_util::compoundToSimple(
              static_cast<int>(nearest) - static_cast<int>(other_note->pitch));
          if (interval_util::isConsonance(new_ivl)) {
            fix_note->pitch = nearest;
            continue;
          }
        }

        // Strategy 2: Try shifting by ±1, ±2 scale steps to find consonance.
        bool resolved = false;
        for (int delta : {1, -1, 2, -2, 3, -3}) {
          int cand = static_cast<int>(fix_note->pitch) + delta;
          if (cand < low || cand > high) continue;
          int new_ivl = interval_util::compoundToSimple(
              cand - static_cast<int>(other_note->pitch));
          if (interval_util::isConsonance(new_ivl) &&
              isChordTone(static_cast<uint8_t>(cand), ev)) {
            fix_note->pitch = static_cast<uint8_t>(cand);
            resolved = true;
            break;
          }
        }
        if (resolved) continue;

        // Strategy 3: Any consonant pitch within ±3 (not necessarily chord tone).
        for (int delta : {1, -1, 2, -2, 3, -3}) {
          int cand = static_cast<int>(fix_note->pitch) + delta;
          if (cand < low || cand > high) continue;
          int new_ivl = interval_util::compoundToSimple(
              cand - static_cast<int>(other_note->pitch));
          if (interval_util::isConsonance(new_ivl)) {
            fix_note->pitch = static_cast<uint8_t>(cand);
            resolved = true;
            break;
          }
        }
        if (resolved) continue;

        // Strategy 4: Octave shift.
        for (int shift : {12, -12}) {
          int cand = static_cast<int>(fix_note->pitch) + shift;
          if (cand >= low && cand <= high) {
            int new_ivl = interval_util::compoundToSimple(
                cand - static_cast<int>(other_note->pitch));
            if (interval_util::isConsonance(new_ivl)) {
              fix_note->pitch = static_cast<uint8_t>(cand);
              resolved = true;
              break;
            }
          }
        }
      }
    }
  }
}

uint32_t countStrongBeatP4OverBass(const std::vector<NoteEvent>& all_notes) {
  // Separate bass notes (voice 2) and upper notes (voice 0, 1).
  std::vector<const NoteEvent*> bass_notes;
  std::vector<const NoteEvent*> upper_notes;
  for (const auto& n : all_notes) {
    if (isPedalVoice(n.voice, kTrioVoiceCount)) {
      bass_notes.push_back(&n);
    } else {
      upper_notes.push_back(&n);
    }
  }

  uint32_t count = 0;
  for (const auto* upper : upper_notes) {
    // Only check strong beats (beat 0 and 2 in 4/4).
    uint8_t beat = beatInBar(upper->start_tick);
    if (beat != 0 && beat != 2) continue;

    // Find bass note sounding at this tick.
    for (const auto* bass : bass_notes) {
      if (bass->start_tick <= upper->start_tick &&
          bass->start_tick + bass->duration > upper->start_tick) {
        int ivl = interval_util::compoundToSimple(
            static_cast<int>(upper->pitch) - static_cast<int>(bass->pitch));
        if (ivl == interval::kPerfect4th) {
          ++count;
        }
        break;
      }
    }
  }
  return count;
}

/// @brief Build a TrioSonataCPReport from all notes in a movement.
TrioSonataCPReport buildTrioCPReport(const std::vector<Track>& tracks) {
  // Collect all notes.
  std::vector<NoteEvent> all_notes;
  for (const auto& track : tracks) {
    all_notes.insert(all_notes.end(), track.notes.begin(), track.notes.end());
  }

  TrioSonataCPReport report;
  auto cp = analyzeCounterpoint(all_notes, kTrioVoiceCount);
  report.parallel_perfect = cp.parallel_perfect_count;
  report.voice_crossing = cp.voice_crossing_count;
  report.strong_beat_P4 = countStrongBeatP4OverBass(all_notes);
  return report;
}

// ---------------------------------------------------------------------------
// Step 7: Movement generation
// ---------------------------------------------------------------------------

TrioSonataMovement generateMovement(const KeySignature& key_sig, Tick num_bars,
                                    uint16_t bpm, uint32_t seed,
                                    TrioMovementCharacter character) {
  TrioSonataMovement movement;
  movement.bpm = bpm;
  movement.key = key_sig;

  Tick duration = num_bars * kTicksPerBar;
  movement.total_duration_ticks = duration;

  std::mt19937 rng(seed);
  const CharacterParams& params = getCharacterParams(character);

  // 1. Build harmonic timeline with varied progressions.
  HarmonicTimeline timeline =
      buildMovementTimeline(key_sig, duration, params, rng);

  // 2. Generate motif for this movement.
  const HarmonicEvent& first_event = timeline.getAt(0);
  auto motif = generateMotif(first_event, params, key_sig.tonic,
                             key_sig.is_minor, rng);

  // 3. Create tracks.
  std::vector<Track> tracks = createTrioSonataTracks();

  // 4. Generate phrase by phrase.
  Tick num_phrases = duration / kPhraseTicks;
  if (num_phrases == 0) num_phrases = 1;

  ScaleType scale = key_sig.is_minor ? ScaleType::HarmonicMinor : ScaleType::Major;

  for (Tick p = 0; p < num_phrases; ++p) {
    Tick phrase_start = p * kPhraseTicks;
    Tick phrase_end = phrase_start + kPhraseTicks;
    if (phrase_end > duration) phrase_end = duration;

    // Leader/follower alternation.
    uint8_t leader_voice = (p % 2 == 0) ? 0 : 1;  // RH=0, LH=1.

    // Vary motif per phrase: transpose diatonically by 1-2 degrees.
    auto phrase_motif = motif;
    if (p > 0) {
      int shift = rng::rollRange(rng, -2, 2);
      if (shift == 0) shift = 1;
      phrase_motif = transposeMelodyDiatonic(motif, shift, key_sig.tonic, scale);
    }

    // Generate upper voices.
    std::vector<NoteEvent> leader_notes, follower_notes;
    generateUpperVoicePhrase(phrase_start, phrase_motif, timeline, params,
                             leader_voice, key_sig.tonic, key_sig.is_minor,
                             rng, leader_notes, follower_notes);

    // Invertible counterpoint: 12% chance.
    if (rng::rollProbability(rng, 0.12f)) {
      swapVoiceRegisters(leader_notes, follower_notes,
                         leader_voice == 0 ? kRhCenter : kLhCenter,
                         leader_voice == 0 ? kRhLow : kLhLow,
                         leader_voice == 0 ? kRhHigh : kLhHigh,
                         leader_voice == 0 ? kLhCenter : kRhCenter,
                         leader_voice == 0 ? kLhLow : kRhLow,
                         leader_voice == 0 ? kLhHigh : kRhHigh);
    }

    // Route to tracks.
    for (auto& n : leader_notes) {
      tracks[n.voice].notes.push_back(n);
    }
    for (auto& n : follower_notes) {
      tracks[n.voice].notes.push_back(n);
    }

    // Generate pedal line.
    std::vector<NoteEvent> pedal_notes;
    bool is_thematic_bass = rng::rollProbability(rng, params.thematic_bass_prob);
    if (is_thematic_bass) {
      pedal_notes = generateThematicBass(phrase_start, phrase_end, motif,
                                         timeline, key_sig.tonic,
                                         key_sig.is_minor, rng);
    } else {
      pedal_notes = generateWalkingBass(phrase_start, phrase_end, timeline,
                                        params, key_sig.tonic,
                                        key_sig.is_minor, rng);
    }
    for (auto& n : pedal_notes) {
      tracks[2].notes.push_back(n);
    }

    // Pedal lead: simplify upper voices when thematic bass is active.
    // Skip first phrase (p == 0) to preserve motif establishment.
    if (is_thematic_bass && p > 0) {
      simplifyForPedalLead(tracks[0].notes, phrase_start, phrase_end, timeline,
                           kRhLow, kRhHigh, 0);
      simplifyForPedalLead(tracks[1].notes, phrase_start, phrase_end, timeline,
                           kLhLow, kLhHigh, 1);
    }
  }

  // 5. Sort notes within each track.
  auto sortTracks = [](std::vector<Track>& trks) {
    for (auto& track : trks) {
      std::sort(track.notes.begin(), track.notes.end(),
                [](const NoteEvent& lhs, const NoteEvent& rhs) {
                  if (lhs.start_tick != rhs.start_tick) {
                    return lhs.start_tick < rhs.start_tick;
                  }
                  return lhs.pitch < rhs.pitch;
                });
    }
  };
  sortTracks(tracks);

  // 5b. Post-validate through counterpoint engine.
  {
    std::vector<NoteEvent> all_notes;
    for (const auto& track : tracks) {
      all_notes.insert(all_notes.end(), track.notes.begin(), track.notes.end());
    }

    std::vector<std::pair<uint8_t, uint8_t>> voice_ranges = {
        {kRhLow, kRhHigh},
        {kLhLow, kLhHigh},
        {organ_range::kPedalLow, organ_range::kPedalHigh}};

    // Pedal notes are Structural (PedalPoint), upper voices are Flexible.
    for (auto& n : all_notes) {
      if (isPedalVoice(n.voice, kTrioVoiceCount) && n.source == BachNoteSource::Unknown) {
        n.source = BachNoteSource::PedalPoint;
      } else if (n.source == BachNoteSource::Unknown) {
        n.source = BachNoteSource::FreeCounterpoint;
      }
    }

    // ---- createBachNote coordination pass ----
    {
      BachRuleEvaluator cp_rules(kTrioVoiceCount);
      cp_rules.setFreeCounterpoint(true);
      CollisionResolver cp_resolver;
      cp_resolver.setHarmonicTimeline(&timeline);
      CounterpointState cp_state;
      cp_state.setKey(key_sig.tonic);
      for (uint8_t v = 0; v < kTrioVoiceCount; ++v) {
        cp_state.registerVoice(v, voice_ranges[v].first, voice_ranges[v].second);
      }

      std::sort(all_notes.begin(), all_notes.end(),
                [](const NoteEvent& a, const NoteEvent& b) {
                  return a.start_tick < b.start_tick;
                });

      std::vector<NoteEvent> coordinated;
      coordinated.reserve(all_notes.size());
      int accepted_count = 0;
      int total_count = 0;

      size_t idx = 0;
      while (idx < all_notes.size()) {
        Tick current_tick = all_notes[idx].start_tick;
        size_t group_end = idx;
        while (group_end < all_notes.size() &&
               all_notes[group_end].start_tick == current_tick) {
          ++group_end;
        }

        // Priority: pedal (immutable) → LH → RH.
        std::sort(all_notes.begin() + static_cast<ptrdiff_t>(idx),
                  all_notes.begin() + static_cast<ptrdiff_t>(group_end),
                  [](const NoteEvent& a, const NoteEvent& b) {
                    bool a_pedal = (a.source == BachNoteSource::PedalPoint);
                    bool b_pedal = (b.source == BachNoteSource::PedalPoint);
                    if (a_pedal != b_pedal) return a_pedal;
                    return a.voice > b.voice;  // LH (1) before RH (0)
                  });

        for (size_t j = idx; j < group_end; ++j) {
          const auto& note = all_notes[j];
          ++total_count;

          if (note.source == BachNoteSource::PedalPoint) {
            cp_state.addNote(note.voice, note);
            coordinated.push_back(note);
            ++accepted_count;
            continue;
          }

          BachNoteOptions opts;
          opts.voice = note.voice;
          opts.desired_pitch = note.pitch;
          opts.tick = note.start_tick;
          opts.duration = note.duration;
          opts.velocity = note.velocity;
          opts.source = note.source;

          auto result = createBachNote(&cp_state, &cp_rules, &cp_resolver, opts);
          if (result.accepted) {
            coordinated.push_back(result.note);
            ++accepted_count;
          }
        }
        idx = group_end;
      }

      fprintf(stderr, "[TrioSonata] createBachNote: accepted %d/%d (%.0f%%)\n",
              accepted_count, total_count,
              total_count > 0 ? 100.0 * accepted_count / total_count : 0.0);
      all_notes = std::move(coordinated);
    }

    PostValidateStats stats;
    auto validated = postValidateNotes(
        std::move(all_notes), kTrioVoiceCount, key_sig, voice_ranges, &stats);

    // Leap resolution: fix unresolved melodic leaps.
    {
      LeapResolutionParams lr_params;
      lr_params.num_voices = kTrioVoiceCount;
      lr_params.key_at_tick = [&](Tick) { return key_sig.tonic; };
      lr_params.scale_at_tick = [&](Tick) { return scale; };
      lr_params.voice_range = [&](uint8_t v) -> std::pair<uint8_t, uint8_t> {
        if (v < voice_ranges.size()) return voice_ranges[v];
        return {0, 127};
      };
      lr_params.is_chord_tone = [&](Tick t, uint8_t p) {
        return isChordTone(p, timeline.getAt(t));
      };
      resolveLeaps(validated, lr_params);

      // Second parallel-perfect repair pass after leap resolution.
      {
        ParallelRepairParams pp_params;
        pp_params.num_voices = kTrioVoiceCount;
        pp_params.scale = scale;
        pp_params.key_at_tick = lr_params.key_at_tick;
        pp_params.voice_range = lr_params.voice_range;
        pp_params.max_iterations = 3;
        repairParallelPerfect(validated, pp_params);
      }
    }

    for (auto& track : tracks) {
      track.notes.clear();
    }
    for (auto& note : validated) {
      if (note.voice < kTrioVoiceCount) {
        tracks[note.voice].notes.push_back(std::move(note));
      }
    }
    sortTracks(tracks);
  }

  // 6. Validate non-harmonic tones on upper voices.
  validateNonHarmonicTones(tracks, timeline, key_sig.tonic, scale);

  // 7. Cadential suspensions at phrase boundaries.
  for (Tick p = 1; p <= num_phrases; ++p) {
    Tick cadence_tick = p * kPhraseTicks;
    if (cadence_tick > duration) cadence_tick = duration;
    // Alternate leader voice for suspension placement.
    uint8_t sus_voice = ((p - 1) % 2 == 0) ? 0 : 1;
    insertCadentialSuspension(tracks, cadence_tick, sus_voice, key_sig.tonic, scale);
  }

  // 8. Breathing rests at phrase boundaries (upper voices only, pedal exempt).
  insertBreathingRests(tracks, num_phrases, duration);

  // 9. Re-sort after suspension and breathing modifications.
  sortTracks(tracks);

  // 10. Post-process: eliminate consecutive repeated pitches.
  // When a note has the same pitch as the previous note, shift it by 1 or 2
  // scale degrees. Direction chosen to move toward the range center.
  for (auto& track : tracks) {
    auto& notes = track.notes;
    for (size_t i = 1; i < notes.size(); ++i) {
      if (notes[i].pitch == notes[i - 1].pitch) {
        uint8_t low, high, center;
        if (notes[i].voice == 0) {
          low = kRhLow; high = kRhHigh; center = kRhCenter;
        } else if (notes[i].voice == 1) {
          low = kLhLow; high = kLhHigh; center = kLhCenter;
        } else {
          low = organ_range::kPedalLow; high = organ_range::kPedalHigh;
          center = (organ_range::kPedalLow + organ_range::kPedalHigh) / 2;
        }

        int abs_deg = scale_util::pitchToAbsoluteDegree(notes[i].pitch,
                                                         key_sig.tonic, scale);
        // Move toward range center to avoid boundary issues.
        int dir = (notes[i].pitch < center) ? 1 : -1;

        // Try shifts of 1 and 2 degrees in primary and reverse direction.
        bool fixed = false;
        for (int try_shift : {dir, -dir, 2 * dir, -2 * dir}) {
          uint8_t cand = scale_util::absoluteDegreeToPitch(
              abs_deg + try_shift, key_sig.tonic, scale);
          cand = clampPitch(static_cast<int>(cand), low, high);
          if (cand != notes[i - 1].pitch) {
            notes[i].pitch = cand;
            notes[i].modified_by |= static_cast<uint8_t>(NoteModifiedBy::RepeatedNoteRep);
            fixed = true;
            break;
          }
        }
        if (!fixed) {
          // Last resort: shift by 3 degrees.
          notes[i].pitch = clampPitch(
              static_cast<int>(scale_util::absoluteDegreeToPitch(
                  abs_deg + 3 * dir, key_sig.tonic, scale)),
              low, high);
          notes[i].modified_by |= static_cast<uint8_t>(NoteModifiedBy::RepeatedNoteRep);
        }
      }
    }
  }

  // 10b. Second parallel repair: steps 6-10 may have re-introduced parallel
  // perfect consonances. Run postValidateNotes before ornaments and quality
  // passes so that diatonic enforcement and voice separation still apply.
  {
    std::vector<NoteEvent> all_notes;
    for (const auto& track : tracks) {
      all_notes.insert(all_notes.end(), track.notes.begin(), track.notes.end());
    }
    for (auto& n : all_notes) {
      if (isPedalVoice(n.voice, kTrioVoiceCount) && n.source == BachNoteSource::Unknown) {
        n.source = BachNoteSource::PedalPoint;
      } else if (n.source == BachNoteSource::Unknown) {
        n.source = BachNoteSource::FreeCounterpoint;
      }
    }
    std::vector<std::pair<uint8_t, uint8_t>> vr = {
        {kRhLow, kRhHigh},
        {kLhLow, kLhHigh},
        {organ_range::kPedalLow, organ_range::kPedalHigh}};
    auto validated = postValidateNotes(
        std::move(all_notes), kTrioVoiceCount, key_sig, vr);

    // Leap resolution: fix unresolved melodic leaps (second pass).
    {
      LeapResolutionParams lr_params;
      lr_params.num_voices = kTrioVoiceCount;
      lr_params.key_at_tick = [&](Tick) { return key_sig.tonic; };
      lr_params.scale_at_tick = [&](Tick) { return scale; };
      lr_params.voice_range = [&](uint8_t v) -> std::pair<uint8_t, uint8_t> {
        if (v < vr.size()) return vr[v];
        return {0, 127};
      };
      lr_params.is_chord_tone = [&](Tick t, uint8_t p) {
        return isChordTone(p, timeline.getAt(t));
      };
      resolveLeaps(validated, lr_params);

      // Second parallel-perfect repair pass after leap resolution.
      {
        ParallelRepairParams pp_params;
        pp_params.num_voices = kTrioVoiceCount;
        pp_params.scale = scale;
        pp_params.key_at_tick = lr_params.key_at_tick;
        pp_params.voice_range = lr_params.voice_range;
        pp_params.max_iterations = 3;
        repairParallelPerfect(validated, pp_params);
      }
    }

    // Resolve excessive melodic leaps (> 13 semitones) using octave shift priority.
    // Bach's trio sonatas maintain independent registers per voice; octave
    // displacement is musically correct for resolving large leaps.  We generate
    // three candidates, score them with a 3-point contour check (prev, curr,
    // next), and pick the best one that avoids voice crossing.
    for (uint8_t voice = 0; voice < kTrioVoiceCount - 1; ++voice) {  // pedal excluded
      std::vector<size_t> voice_indices;
      for (size_t idx = 0; idx < validated.size(); ++idx) {
        if (validated[idx].voice == voice) voice_indices.push_back(idx);
      }
      for (size_t cur = 1; cur < voice_indices.size(); ++cur) {
        auto& curr = validated[voice_indices[cur]];
        const auto& prev_note = validated[voice_indices[cur - 1]];
        int leap = static_cast<int>(curr.pitch) - static_cast<int>(prev_note.pitch);
        if (std::abs(leap) <= 13) continue;

        const uint8_t range_low = vr[voice].first;
        const uint8_t range_high = vr[voice].second;

        // Generate three octave-shift candidates.
        int shift1 = nearestOctaveShift(leap);
        int cand1 = static_cast<int>(curr.pitch) - shift1;
        int shift2 = (shift1 > 0) ? shift1 - 12 : shift1 + 12;  // opposite shift
        int cand2 = static_cast<int>(curr.pitch) - shift2;
        int dir = (leap > 0) ? 1 : -1;
        int cand3 = static_cast<int>(prev_note.pitch) + dir * 12;  // fallback

        // Clamp all candidates to voice range.
        uint8_t candidates[3] = {
            clampPitch(cand1, range_low, range_high),
            clampPitch(cand2, range_low, range_high),
            clampPitch(cand3, range_low, range_high),
        };

        // Look up the next note pitch for continuity scoring (if available).
        bool has_next = (cur + 1 < voice_indices.size());
        int next_pitch = has_next
            ? static_cast<int>(validated[voice_indices[cur + 1]].pitch)
            : static_cast<int>(curr.pitch);

        // Score each candidate.
        constexpr double kRejectScore = 1000.0;
        constexpr double kCrossingPenalty = 900.0;
        constexpr double kNextWeight = 0.75;
        double best_score = kRejectScore;
        uint8_t best_pitch = curr.pitch;

        for (int cdx = 0; cdx < 3; ++cdx) {
          int cand_pitch = static_cast<int>(candidates[cdx]);
          int new_leap = cand_pitch - static_cast<int>(prev_note.pitch);

          // Reject if leap from prev still exceeds threshold.
          if (std::abs(new_leap) > 13) continue;

          double score = static_cast<double>(std::abs(new_leap));

          // Voice crossing penalty: check against other voices at same tick.
          bool crossing = false;
          for (size_t odx = 0; odx < validated.size(); ++odx) {
            const auto& other = validated[odx];
            if (other.voice == voice) continue;
            if (other.voice >= kTrioVoiceCount) continue;
            // Check temporal overlap (same tick window).
            if (other.start_tick + other.duration <= curr.start_tick) continue;
            if (other.start_tick >= curr.start_tick + curr.duration) continue;
            int other_pitch = static_cast<int>(other.pitch);
            // Voice 0 (upper) must not go below other voices.
            if (voice == 0 && cand_pitch < other_pitch) { crossing = true; break; }
            // Voice 1 (middle) must not go above voice 0.
            if (voice == 1 && other.voice == 0 && cand_pitch > other_pitch) {
              crossing = true;
              break;
            }
          }
          if (crossing) {
            score = kCrossingPenalty;
          } else {
            // Next note continuity bonus.
            score += std::abs(cand_pitch - next_pitch) * kNextWeight;
          }

          if (score < best_score) {
            best_score = score;
            best_pitch = candidates[cdx];
          }
        }

        // Apply only if a valid candidate was found (score < crossing penalty).
        if (best_score < kCrossingPenalty) {
          curr.pitch = best_pitch;
        }
      }
    }

    for (auto& track : tracks) {
      track.notes.clear();
    }
    for (auto& note : validated) {
      if (note.voice < kTrioVoiceCount) {
        tracks[note.voice].notes.push_back(std::move(note));
      }
    }
    sortTracks(tracks);
  }

  // 11. Apply ornaments with counterpoint verification.
  {
    // Determine ornament density based on movement character.
    float density;
    switch (character) {
      case TrioMovementCharacter::Adagio: density = 0.08f; break;
      case TrioMovementCharacter::Vivace: density = 0.06f; break;
      default: density = 0.08f; break;  // Allegro.
    }

    OrnamentConfig orn_config;
    orn_config.ornament_density = density;

    // Collect all voice notes for counterpoint verification.
    std::vector<std::vector<NoteEvent>> all_voice_notes(kTrioVoiceCount);
    for (const auto& track : tracks) {
      for (const auto& n : track.notes) {
        if (n.voice < kTrioVoiceCount) {
          all_voice_notes[n.voice].push_back(n);
        }
      }
    }

    // Apply ornaments to upper voices only (pedal is Ground — no ornaments).
    for (size_t trk = 0; trk < 2 && trk < tracks.size(); ++trk) {
      OrnamentContext ctx;
      ctx.config = orn_config;
      ctx.role = VoiceRole::Respond;  // 3 voices = equal treatment.
      ctx.seed = seed + static_cast<uint32_t>(trk) * 100;
      ctx.timeline = &timeline;

      auto ornamented = applyOrnaments(tracks[trk].notes, ctx, all_voice_notes);

      // Pitch clamping: prevent voice crossing from ornament expansion.
      uint8_t range_low = (trk == 0) ? kRhLow : kLhLow;
      uint8_t range_high = (trk == 0) ? kRhHigh : kLhHigh;
      for (auto& n : ornamented) {
        n.pitch = clampPitch(static_cast<int>(n.pitch), range_low, range_high);
        n.modified_by |= static_cast<uint8_t>(NoteModifiedBy::OctaveAdjust);
      }

      tracks[trk].notes = std::move(ornamented);
    }

    // Re-sort after ornament expansion.
    sortTracks(tracks);
  }

  // 12. Quality post-passes (after ornaments, ensuring final output quality).

  // 12a. Quantize all upper voice durations to allowed set.
  {
    bool is_slow = (params.primary_dur >= kQuarterNote);
    // Fast: {120, 240, 480}; Slow: {240, 480, 960}.
    const Tick allowed[3] = {
        is_slow ? kEighthNote : kSixteenthNote,
        is_slow ? kQuarterNote : kEighthNote,
        is_slow ? kHalfNote : kQuarterNote};
    for (size_t trk = 0; trk < 2 && trk < tracks.size(); ++trk) {
      for (auto& note : tracks[trk].notes) {
        bool in_set = (note.duration == allowed[0] ||
                       note.duration == allowed[1] ||
                       note.duration == allowed[2]);
        if (!in_set) {
          // Snap to nearest allowed duration.
          Tick best = allowed[0];
          int best_dist = std::abs(static_cast<int>(note.duration) -
                                   static_cast<int>(best));
          for (int i = 1; i < 3; ++i) {
            int dist = std::abs(static_cast<int>(note.duration) -
                                static_cast<int>(allowed[i]));
            if (dist < best_dist) {
              best_dist = dist;
              best = allowed[i];
            }
          }
          note.duration = best;
          note.modified_by |= static_cast<uint8_t>(NoteModifiedBy::Articulation);
        }
      }
    }
  }

  // 12b. Enforce consonance on strong beats (may introduce chord tones).
  enforceStrongBeatConsonance(tracks, timeline, key_sig.tonic, key_sig.is_minor);

  // 12c. Enforce diatonic pitches for major movements (after consonance fix).
  enforceDiatonicPitches(tracks, key_sig.tonic, key_sig.is_minor);

  // 12d. Enforce minimum voice separation (must be last pitch modifier).
  enforceMinimumVoiceSeparation(tracks);

  // 12e. Re-sort after quality post-passes.
  sortTracks(tracks);

  // 13. Counterpoint analysis (after all modifications).
  movement.cp_report = buildTrioCPReport(tracks);

  movement.tracks = std::move(tracks);
  return movement;
}

}  // namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

TrioSonataResult generateTrioSonata(const TrioSonataConfig& config) {
  TrioSonataResult result;
  result.success = false;

  // Movement 1: Allegro, home key.
  TrioSonataMovement mov1 = generateMovement(
      config.key, kFastMovementBars, config.bpm_fast, config.seed,
      TrioMovementCharacter::Allegro);

  // Movement 2: Adagio, related key.
  KeySignature slow_key = getRelative(config.key);
  TrioSonataMovement mov2 = generateMovement(
      slow_key, kSlowMovementBars, config.bpm_slow,
      config.seed + kMovement2SeedOffset, TrioMovementCharacter::Adagio);

  // Movement 3: Vivace, home key.
  TrioSonataMovement mov3 = generateMovement(
      config.key, kFastMovementBars, config.bpm_fast,
      config.seed + kMovement3SeedOffset, TrioMovementCharacter::Vivace);

  // Registration and Picardy (shared organ techniques).
  Registration mov_regs[] = {
      OrganRegistrationPresets::mezzo(),
      OrganRegistrationPresets::piano(),
      OrganRegistrationPresets::forte(),
  };
  TrioSonataMovement* movs[] = {&mov1, &mov2, &mov3};
  KeySignature mov_keys[] = {config.key, slow_key, config.key};

  for (int m = 0; m < 3; ++m) {
    ExtendedRegistrationPlan reg_plan;
    reg_plan.addPoint(0, mov_regs[m], "movement_" + std::to_string(m + 1));
    applyExtendedRegistrationPlan(movs[m]->tracks, reg_plan);

    if (config.enable_picardy && mov_keys[m].is_minor &&
        movs[m]->total_duration_ticks > kTicksPerBar) {
      for (auto& track : movs[m]->tracks) {
        applyPicardyToFinalChord(
            track.notes, mov_keys[m],
            movs[m]->total_duration_ticks - kTicksPerBar);
      }
    }
  }

  result.movements.push_back(std::move(mov1));
  result.movements.push_back(std::move(mov2));
  result.movements.push_back(std::move(mov3));

  // Aggregate counterpoint reports across all movements.
  for (const auto& mov : result.movements) {
    result.counterpoint_report.accumulate(mov.cp_report);
  }

  result.success = true;
  return result;
}

}  // namespace bach
