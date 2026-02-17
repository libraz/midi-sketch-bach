// Implementation of texture generation for chaconne variations (Arch system).

#include "solo_string/arch/texture_generator.h"

#include <algorithm>
#include <cmath>
#include <random>

#include "core/markov_tables.h"
#include "core/note_source.h"
#include "core/pitch_utils.h"
#include "core/rng_util.h"
#include "core/scale.h"
#include "counterpoint/leap_resolution.h"
#include "harmony/chord_tone_utils.h"
#include "harmony/chord_types.h"
#include "harmony/harmonic_event.h"
#include "harmony/scale_degree_utils.h"
#include "solo_string/flow/arpeggio_pattern.h"
#include "solo_string/solo_vocabulary.h"

namespace bach {

// ===========================================================================
// getRhythmSubdivisions -- public
// ===========================================================================

std::vector<std::pair<Tick, Tick>> getRhythmSubdivisions(
    RhythmProfile profile, Tick beat_ticks) {
  switch (profile) {
    case RhythmProfile::QuarterNote:
      return {{0, beat_ticks}};
    case RhythmProfile::EighthNote:
      return {{0, beat_ticks / 2}, {beat_ticks / 2, beat_ticks / 2}};
    case RhythmProfile::DottedEighth:
      // Dotted-8th (360) + 16th (120) = 480
      return {{0, beat_ticks * 3 / 4}, {beat_ticks * 3 / 4, beat_ticks / 4}};
    case RhythmProfile::Triplet:
      // 3 equal subdivisions: 160 + 160 + 160 = 480
      return {{0, beat_ticks / 3},
              {beat_ticks / 3, beat_ticks / 3},
              {beat_ticks * 2 / 3, beat_ticks - beat_ticks / 3 - beat_ticks / 3}};
    case RhythmProfile::Sixteenth:
      return {{0, beat_ticks / 4},
              {beat_ticks / 4, beat_ticks / 4},
              {beat_ticks / 2, beat_ticks / 4},
              {beat_ticks * 3 / 4, beat_ticks / 4}};
    case RhythmProfile::Mixed8th16th:
      // 8th (240) + 16th (120) + 16th (120) = 480
      return {{0, beat_ticks / 2},
              {beat_ticks / 2, beat_ticks / 4},
              {beat_ticks * 3 / 4, beat_ticks / 4}};
  }
  return {{0, beat_ticks}};
}

namespace {

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

/// @brief Duration of an 8th note in ticks.
/// NOTE: The chaconne is historically in 3/4 time, but this implementation
/// uses the global kTicksPerBar=1920 and kBeatsPerBar=4 (4/4) constants
/// throughout. The beat grid (kTicksPerBeat=480) is correct regardless of
/// time signature, but bar-level calculations assume 4 beats per bar.
/// Changing to 3/4 would require a larger refactor beyond S3 scope.
constexpr Tick kEighthDuration = kTicksPerBeat / 2;  // 240

/// @brief Duration of a chord grace note in ticks (used in FullChords).
constexpr Tick kGraceNoteDuration = 60;

/// @brief Default velocity for texture notes (expressive solo string).
constexpr uint8_t kBaseVelocity = 72;

/// @brief Velocity boost for notes on strong beats (beat 1 and 3).
constexpr uint8_t kStrongBeatBoost = 6;

/// @brief Velocity boost for climax chords.
constexpr uint8_t kClimaxVelocityBoost = 16;

/// @brief Violin open string MIDI pitches: G3, D4, A4, E5.
constexpr uint8_t kViolinOpenStrings[] = {55, 62, 69, 76};
constexpr int kViolinOpenStringCount = 4;

// ---------------------------------------------------------------------------
// Helper: place a pitch class in a register, nearest to a reference pitch
// ---------------------------------------------------------------------------

/// @brief Place a pitch class within [reg_low, reg_high] closest to ref_pitch.
///
/// Uses nearestOctaveShift to find the best octave placement for a pitch class,
/// then checks +/-12 alternatives to pick the one nearest to ref_pitch.
///
/// @param pitch_class Pitch class in [0, 11].
/// @param reg_low Lowest allowed MIDI pitch.
/// @param reg_high Highest allowed MIDI pitch.
/// @param ref_pitch Reference pitch to minimize distance to.
/// @return MIDI pitch within [reg_low, reg_high], or clamped if no exact fit.
int fitPitchClassToRegister(int pitch_class, uint8_t reg_low,
                            uint8_t reg_high, int ref_pitch) {
  int center = (static_cast<int>(reg_low) + static_cast<int>(reg_high)) / 2;
  int shift = nearestOctaveShift(center - pitch_class);
  int candidate = pitch_class + shift;

  // If the nearest-octave candidate is out of range, clamp.
  if (candidate < static_cast<int>(reg_low) || candidate > static_cast<int>(reg_high)) {
    return static_cast<int>(clampPitch(candidate, reg_low, reg_high));
  }

  // Among in-range octave placements, pick nearest to ref_pitch.
  int best = candidate;
  int best_dist = std::abs(candidate - ref_pitch);
  for (int alt : {candidate - 12, candidate + 12}) {
    if (alt >= static_cast<int>(reg_low) && alt <= static_cast<int>(reg_high)) {
      int dist = std::abs(alt - ref_pitch);
      if (dist < best_dist) {
        best_dist = dist;
        best = alt;
      }
    }
  }
  return best;
}

// ---------------------------------------------------------------------------
// Helper: convert chord to MIDI pitches within a register range
// ---------------------------------------------------------------------------

/// @brief Convert a chord to concrete MIDI pitches within a register range.
///
/// Builds a triad (root, 3rd, 5th) or seventh chord (root, 3rd, 5th, 7th)
/// from the chord's root pitch and quality, then octave-adjusts each pitch
/// to fall within [reg_low, reg_high].
///
/// @param chord The chord to voice.
/// @param is_minor Whether the current key context is minor.
/// @param reg_low Lowest allowed MIDI pitch.
/// @param reg_high Highest allowed MIDI pitch.
/// @return Vector of 3 or 4 MIDI pitches, sorted low to high.
std::vector<uint8_t> chordToPitches(const Chord& chord, bool is_minor,
                                    uint8_t reg_low, uint8_t reg_high) {
  // Determine chord tone degrees based on quality.
  bool is_seventh = (chord.quality == ChordQuality::Dominant7 ||
                     chord.quality == ChordQuality::Minor7 ||
                     chord.quality == ChordQuality::MajorMajor7);

  // Scale degrees relative to chord root: 0=root, 2=3rd, 4=5th, 6=7th.
  std::vector<int> chord_degrees = {0, 2, 4};
  if (is_seventh) {
    chord_degrees.push_back(6);
  }

  std::vector<uint8_t> pitches;
  pitches.reserve(chord_degrees.size());

  for (int deg : chord_degrees) {
    int offset = degreeToPitchOffset(deg, is_minor);
    int raw_pitch = static_cast<int>(chord.root_pitch) + offset;

    // Octave-adjust to fit register.
    int pitch_class = getPitchClassSigned(raw_pitch);
    int fitted = fitPitchClassToRegister(pitch_class, reg_low, reg_high, raw_pitch);
    if (fitted >= static_cast<int>(reg_low) && fitted <= static_cast<int>(reg_high)) {
      pitches.push_back(static_cast<uint8_t>(fitted));
    } else {
      pitches.push_back(raw_pitch < static_cast<int>(reg_low) ? reg_low : reg_high);
    }
  }

  std::sort(pitches.begin(), pitches.end());
  return pitches;
}

/// @brief Octave-transpose a pitch to fit within [reg_low, reg_high].
///
/// Preserves the pitch class and finds the nearest octave placement that
/// falls within the given register. Falls back to clamping if no octave works.
///
/// @param pitch Input MIDI pitch.
/// @param reg_low Minimum allowed pitch.
/// @param reg_high Maximum allowed pitch.
/// @return Adjusted pitch within the register.
uint8_t fitPitchToRegister(uint8_t pitch, uint8_t reg_low, uint8_t reg_high) {
  if (pitch >= reg_low && pitch <= reg_high) {
    return pitch;
  }

  int pitch_class = getPitchClass(pitch);
  int fitted = fitPitchClassToRegister(pitch_class, reg_low, reg_high,
                                       static_cast<int>(pitch));
  return static_cast<uint8_t>(fitted);
}

/// @brief Get all scale pitches within a register range for a given key.
///
/// Enumerates every scale tone between reg_low and reg_high (inclusive),
/// returning them sorted in ascending order.
///
/// @param key The tonic pitch class.
/// @param is_minor True for natural minor scale.
/// @param reg_low Lowest MIDI pitch.
/// @param reg_high Highest MIDI pitch.
/// @return Vector of all scale-tone MIDI pitches in [reg_low, reg_high].
std::vector<uint8_t> getScalePitches(Key key, bool is_minor,
                                     uint8_t reg_low, uint8_t reg_high) {
  ScaleType scale_type = is_minor ? ScaleType::NaturalMinor : ScaleType::Major;
  std::vector<uint8_t> pitches;
  pitches.reserve(64);

  for (int midi = static_cast<int>(reg_low); midi <= static_cast<int>(reg_high); ++midi) {
    auto midi_u8 = static_cast<uint8_t>(midi);
    if (scale_util::isScaleTone(midi_u8, key, scale_type)) {
      pitches.push_back(midi_u8);
    }
  }

  return pitches;
}


/// @brief Compute velocity with beat-position-aware accents.
/// @param tick_in_bar Position within the bar (0 to kTicksPerBar-1).
/// @param base_velocity Base velocity value.
/// @param is_climax True for additional climax accent.
/// @return Adjusted velocity value clamped to [1, 127].
/// @note Assumes 4/4 time (kBeatsPerBar=4). For a historically-accurate
/// chaconne in 3/4, beat 2 accent would need adjustment.
uint8_t computeVelocity(Tick tick_in_bar, uint8_t base_velocity, bool is_climax) {
  int vel = static_cast<int>(base_velocity);

  // Strong beat accent (beat 0 and beat 2 in 4/4).
  // NOTE: In 3/4 chaconne, beat 0 is the only strong beat.
  uint8_t beat = static_cast<uint8_t>(tick_in_bar / kTicksPerBeat);
  if (tick_in_bar % kTicksPerBeat == 0 && (beat == 0 || beat == 2)) {
    vel += kStrongBeatBoost;
  }

  if (is_climax) {
    vel += kClimaxVelocityBoost;
  }

  if (vel > 127) vel = 127;
  if (vel < 1) vel = 1;
  return static_cast<uint8_t>(vel);
}

/// @brief Create a NoteEvent for texture generation.
/// @param tick Absolute start tick.
/// @param duration Note duration in ticks.
/// @param pitch MIDI pitch.
/// @param tick_in_bar Position within bar for velocity computation.
/// @param is_climax Whether this variation is the climax.
/// @return Configured NoteEvent with voice=0.
NoteEvent makeTextureNote(Tick tick, Tick duration, uint8_t pitch,
                          Tick tick_in_bar, bool is_climax) {
  NoteEvent note;
  note.start_tick = tick;
  note.duration = duration;
  note.pitch = pitch;
  note.velocity = computeVelocity(tick_in_bar, kBaseVelocity, is_climax);
  note.voice = 1;  // Texture voice (distinct from ground bass voice=0)
  note.source = BachNoteSource::TextureNote;
  return note;
}

/// @brief Find the nearest open string pitch within register.
///
/// Searches the violin open strings for the one closest to the target pitch
/// that falls within the register bounds.
///
/// @param target Target MIDI pitch.
/// @param reg_low Register lower bound.
/// @param reg_high Register upper bound.
/// @return Nearest open string pitch in register, or 0 if none found.
uint8_t findNearestOpenString(uint8_t target, uint8_t reg_low, uint8_t reg_high) {
  uint8_t best = 0;
  int best_dist = 999;

  for (int idx = 0; idx < kViolinOpenStringCount; ++idx) {
    uint8_t open_pitch = kViolinOpenStrings[idx];
    if (open_pitch >= reg_low && open_pitch <= reg_high) {
      int dist = absoluteInterval(target, open_pitch);
      if (dist < best_dist) {
        best_dist = dist;
        best = open_pitch;
      }
    }
  }

  return best;
}

/// @brief Clamp excessive leaps (>12 semitones) by octave-adjusting the target note.
///
/// When a note creates a leap >12 semitones from the previous note, adjust it
/// to the nearest octave placement that keeps the interval within 12 semitones.
void clampExcessiveLeaps(std::vector<NoteEvent>& notes,
                         uint8_t reg_low, uint8_t reg_high) {
  if (notes.size() < 2) return;

  for (size_t i = 1; i < notes.size(); ++i) {
    int leap = absoluteInterval(notes[i].pitch, notes[i - 1].pitch);
    if (leap <= 12) continue;

    // Find the octave-transposition of notes[i] closest to notes[i-1].
    int pc = getPitchClass(notes[i].pitch);
    int prev = static_cast<int>(notes[i - 1].pitch);
    int fitted = fitPitchClassToRegister(pc, reg_low, reg_high, prev);
    notes[i].pitch = static_cast<uint8_t>(fitted);
    notes[i].modified_by |= static_cast<uint8_t>(NoteModifiedBy::OctaveAdjust);
  }
}


// ---------------------------------------------------------------------------
// Helper: find the nearest diatonic step from a pitch toward a direction
// ---------------------------------------------------------------------------

/// @brief Find a pitch that is one diatonic scale step from `from_pitch`.
///
/// Steps up or down by one scale degree, preferring the direction of
/// `target_pitch`. If the step in the preferred direction is out of range,
/// tries the opposite direction. Ensures the result is a scale tone within
/// the register bounds.
///
/// @param from_pitch Starting MIDI pitch.
/// @param target_pitch Target direction pitch (step toward this).
/// @param key Scale key context.
/// @param scale Scale type (Major, NaturalMinor, etc.).
/// @param range_low Lower register bound.
/// @param range_high Upper register bound.
/// @return A pitch one diatonic step from from_pitch, or from_pitch if stuck.
uint8_t nearestDiatonicStep(uint8_t from_pitch, uint8_t target_pitch,
                            Key key, ScaleType scale,
                            uint8_t range_low, uint8_t range_high) {
  int abs_deg = scale_util::pitchToAbsoluteDegree(from_pitch, key, scale);

  // Determine preferred direction: toward target_pitch.
  int direction = (target_pitch >= from_pitch) ? 1 : -1;
  if (target_pitch == from_pitch) {
    direction = 1;  // Default to ascending when equal.
  }

  // Try the preferred direction first.
  int step_deg = abs_deg + direction;
  uint8_t step_pitch = scale_util::absoluteDegreeToPitch(step_deg, key, scale);
  if (step_pitch >= range_low && step_pitch <= range_high) {
    return step_pitch;
  }

  // Try the opposite direction.
  step_deg = abs_deg - direction;
  step_pitch = scale_util::absoluteDegreeToPitch(step_deg, key, scale);
  if (step_pitch >= range_low && step_pitch <= range_high) {
    return step_pitch;
  }

  // Stuck at register boundary -- stay put.
  return from_pitch;
}

/// @brief Check if the given beat position is on a strong beat.
///
/// Strong beats in 4/4: beat 0 and beat 2 (tick offsets 0 and 960).
/// Strong beats are where chord tones should anchor.
///
/// @param tick_in_bar Tick offset within the bar.
/// @return True if on beat 0 or beat 2.
bool isStrongBeatPosition(Tick tick_in_bar) {
  Tick beat_offset = tick_in_bar % kTicksPerBeat;
  if (beat_offset != 0) return false;
  uint8_t beat = static_cast<uint8_t>(tick_in_bar / kTicksPerBeat);
  return (beat == 0 || beat == 2);
}

}  // namespace

// ===========================================================================
// Public API: texture dispatch
// ===========================================================================

std::vector<NoteEvent> generateTexture(const TextureContext& ctx,
                                       const HarmonicTimeline& timeline) {
  std::vector<NoteEvent> notes;
  switch (ctx.texture) {
    case TextureType::SingleLine:
      notes = generateSingleLine(ctx, timeline);
      break;
    case TextureType::ImpliedPolyphony:
      notes = generateImpliedPolyphony(ctx, timeline);
      break;
    case TextureType::FullChords:
      notes = generateFullChords(ctx, timeline);
      break;
    case TextureType::Arpeggiated:
      notes = generateArpeggiated(ctx, timeline);
      break;
    case TextureType::ScalePassage:
      notes = generateScalePassage(ctx, timeline);
      break;
    case TextureType::Bariolage:
      notes = generateBariolage(ctx, timeline);
      break;
  }

  // Clamp excessive leaps (>12 semitones) then apply leap resolution.
  if (ctx.texture != TextureType::FullChords) {
    clampExcessiveLeaps(notes, ctx.register_low, ctx.register_high);
    {
      LeapResolutionParams lr_params;
      lr_params.num_voices = 1;  // Solo string.
      lr_params.leap_threshold = 7;  // P5+ only (existing behavior).
      lr_params.key_at_tick = [&](Tick) { return ctx.key.tonic; };
      lr_params.scale_at_tick = [&](Tick) {
        return ctx.key.is_minor ? ScaleType::NaturalMinor : ScaleType::Major;
      };
      lr_params.voice_range_static = [&](uint8_t) -> std::pair<uint8_t, uint8_t> {
        return {ctx.register_low, ctx.register_high};
      };
      resolveLeaps(notes, lr_params);
    }
  }

  return notes;
}

// ===========================================================================
// SingleLine -- melody with diatonic stepwise motion between chord tones
// ===========================================================================

std::vector<NoteEvent> generateSingleLine(const TextureContext& ctx,
                                          const HarmonicTimeline& timeline) {
  std::vector<NoteEvent> notes;
  if (ctx.duration_ticks == 0) {
    return notes;
  }

  std::mt19937 rng(ctx.seed);

  auto subdivisions = getRhythmSubdivisions(ctx.rhythm_profile);
  Tick num_bars = ctx.duration_ticks / kTicksPerBar;
  notes.reserve(static_cast<size_t>(num_bars) * subdivisions.size() * kBeatsPerBar);

  ScaleType scale_type = ctx.key.is_minor ? ScaleType::NaturalMinor : ScaleType::Major;

  // Track previous pitch for stepwise preference.
  uint8_t prev_pitch = 0;
  bool just_leaped = false;
  int last_leap_direction = 0;  // +1 = up, -1 = down
  int melodic_direction = 1;    // Current melodic direction for step continuation.

  for (Tick bar_offset = 0; bar_offset < ctx.duration_ticks; bar_offset += kTicksPerBar) {
    Tick bar_tick = ctx.start_tick + bar_offset;

    for (int beat_idx = 0; beat_idx < kBeatsPerBar; ++beat_idx) {
      Tick beat_tick = bar_tick + static_cast<Tick>(beat_idx) * kTicksPerBeat;
      Tick tick_in_bar = static_cast<Tick>(beat_idx) * kTicksPerBeat;

      const HarmonicEvent& harm = timeline.getAt(beat_tick);
      std::vector<uint8_t> chord_pitches = chordToPitches(
          harm.chord, harm.is_minor, ctx.register_low, ctx.register_high);

      if (chord_pitches.empty()) continue;

      for (const auto& [sub_offset, sub_duration] : subdivisions) {
        Tick note_tick = beat_tick + sub_offset;
        Tick note_tick_in_bar = tick_in_bar + sub_offset;

        uint8_t pitch;
        if (prev_pitch == 0) {
          // First note: pick a chord tone near the middle of the register.
          int center = (static_cast<int>(ctx.register_low) +
                        static_cast<int>(ctx.register_high)) / 2;
          int range = static_cast<int>(ctx.register_high) -
                      static_cast<int>(ctx.register_low);
          int offset = rng::rollRange(rng, -range / 4, range / 4);
          uint8_t mid = static_cast<uint8_t>(clampPitch(
              center + offset, ctx.register_low, ctx.register_high));
          pitch = nearestChordTone(mid, chord_pitches);
          melodic_direction = rng::rollProbability(rng, 0.5f) ? 1 : -1;
        } else if (just_leaped) {
          // After a leap, resolve by stepping in the opposite direction.
          int comp_dir = (last_leap_direction > 0) ? -1 : 1;
          uint8_t comp_target = clampPitch(
              static_cast<int>(prev_pitch) + comp_dir * 7,
              ctx.register_low, ctx.register_high);
          pitch = nearestDiatonicStep(prev_pitch, comp_target,
                                      ctx.key.tonic, scale_type,
                                      ctx.register_low, ctx.register_high);
          melodic_direction = comp_dir;
          just_leaped = false;
        } else {
          bool is_strong = isStrongBeatPosition(note_tick_in_bar);
          bool is_downbeat = (sub_offset == 0);

          if (is_strong && is_downbeat) {
            // Strong beat downbeat: anchor on chord tone.
            pitch = nearestChordTone(prev_pitch, chord_pitches);

            // Occasional leap to non-nearest chord tone for melodic interest (15%).
            if (chord_pitches.size() > 1 && rng::rollProbability(rng, 0.15f)) {
              uint8_t nearest = pitch;
              std::vector<uint8_t> leap_candidates;
              for (uint8_t cpt : chord_pitches) {
                if (cpt != nearest) {
                  leap_candidates.push_back(cpt);
                }
              }
              if (!leap_candidates.empty()) {
                pitch = rng::selectRandom(rng, leap_candidates);
                int interval = static_cast<int>(pitch) - static_cast<int>(prev_pitch);
                if (std::abs(interval) > 4) {
                  just_leaped = true;
                  last_leap_direction = (interval > 0) ? 1 : -1;
                }
              }
            }
          } else {
            // Weak beat or subdivision: prefer diatonic step from previous note.
            // Find the nearest chord tone as a target to step toward.
            uint8_t target_ct = nearestChordTone(prev_pitch, chord_pitches);

            // Determine step direction: toward the target chord tone.
            int step_dir;
            if (target_ct > prev_pitch) {
              step_dir = 1;
            } else if (target_ct < prev_pitch) {
              step_dir = -1;
            } else {
              // Already on chord tone -- continue in current melodic direction.
              step_dir = melodic_direction;
            }

            // Calculate diatonic step.
            uint8_t step_target = clampPitch(
                static_cast<int>(prev_pitch) + step_dir * 3,
                ctx.register_low, ctx.register_high);
            uint8_t step_pitch = nearestDiatonicStep(
                prev_pitch, step_target,
                ctx.key.tonic, scale_type,
                ctx.register_low, ctx.register_high);

            // Check if the step is also a chord tone (ideal).
            bool step_is_chord_tone = false;
            for (uint8_t cpt : chord_pitches) {
              if (cpt == step_pitch) {
                step_is_chord_tone = true;
                break;
              }
            }

            if (step_is_chord_tone) {
              // Step is a chord tone -- always use it (harmonic + stepwise).
              pitch = step_pitch;
            } else {
              // Compute Markov-informed probability for step vs chord-tone snap.
              // When sufficient history exists, use Markov scores to bias the
              // choice; otherwise fall back to fixed probabilities.
              uint8_t chord_snap = nearestChordTone(prev_pitch, chord_pitches);
              float step_prob = is_downbeat ? 0.75f : 0.90f;

              if (notes.size() >= 2 && step_pitch != chord_snap) {
                ScaleType mk_scale = ctx.key.is_minor ? ScaleType::HarmonicMinor
                                                      : ScaleType::Major;
                Key mk_key = ctx.key.tonic;
                uint8_t prev2 = notes[notes.size() - 2].pitch;
                DegreeStep prev_ivl = computeDegreeStep(
                    prev2, prev_pitch, mk_key, mk_scale);
                int prev_sd = 0;
                scale_util::pitchToScaleDegree(
                    prev_pitch, mk_key, mk_scale, prev_sd);
                DegreeClass deg_cls = scaleDegreeToClass(prev_sd);
                BeatPos beat_pos = tickToBeatPos(note_tick);

                DegreeStep step_ivl = computeDegreeStep(
                    prev_pitch, step_pitch, mk_key, mk_scale);
                DegreeStep snap_ivl = computeDegreeStep(
                    prev_pitch, chord_snap, mk_key, mk_scale);

                float mk_step = scoreMarkovPitch(
                    kViolinMarkov, prev_ivl, deg_cls, beat_pos, step_ivl);
                float mk_snap = scoreMarkovPitch(
                    kViolinMarkov, prev_ivl, deg_cls, beat_pos, snap_ivl);

                // Adjust step_prob by the Markov score differential, scaled
                // by kMarkovPitchWeightSolo. Positive diff favors step.
                float mk_diff = mk_step - mk_snap;
                step_prob += mk_diff * kMarkovPitchWeightSolo;
                step_prob = std::max(0.10f, std::min(0.95f, step_prob));
              }

              if (rng::rollProbability(rng, step_prob)) {
                pitch = step_pitch;
              } else {
                pitch = chord_snap;
              }
            }

            // Update melodic direction based on actual motion.
            int actual_dir = static_cast<int>(pitch) - static_cast<int>(prev_pitch);
            if (actual_dir > 0) {
              melodic_direction = 1;
            } else if (actual_dir < 0) {
              melodic_direction = -1;
            }
            // On unison, keep previous direction.

            // Occasional direction reversal for variety (20%).
            if (rng::rollProbability(rng, 0.20f)) {
              melodic_direction = -melodic_direction;
            }
          }
        }

        notes.push_back(makeTextureNote(
            note_tick, sub_duration, pitch, note_tick_in_bar, ctx.is_climax));
        prev_pitch = pitch;
      }
    }
  }

  return notes;
}

// ===========================================================================
// ImpliedPolyphony -- alternating upper/lower voices with stepwise connection
// ===========================================================================

std::vector<NoteEvent> generateImpliedPolyphony(const TextureContext& ctx,
                                                const HarmonicTimeline& timeline) {
  std::vector<NoteEvent> notes;
  if (ctx.duration_ticks == 0) {
    return notes;
  }

  std::mt19937 rng(ctx.seed);

  auto subdivisions = getRhythmSubdivisions(ctx.rhythm_profile);
  Tick num_bars = ctx.duration_ticks / kTicksPerBar;
  notes.reserve(static_cast<size_t>(num_bars) * subdivisions.size() * kBeatsPerBar);

  ScaleType scale_type = ctx.key.is_minor ? ScaleType::NaturalMinor : ScaleType::Major;

  // Divide register into upper and lower halves with seed-dependent offset.
  int full_range = static_cast<int>(ctx.register_high) - static_cast<int>(ctx.register_low);
  int split_offset = rng::rollRange(rng, -4, 4);  // +-4 semitones for seed diversity
  int mid_point = static_cast<int>(ctx.register_low) + full_range / 2 + split_offset;
  mid_point = std::max(static_cast<int>(ctx.register_low) + 4, mid_point);
  mid_point = std::min(static_cast<int>(ctx.register_high) - 4, mid_point);

  constexpr int kOverlap = 2;
  uint8_t lower_low = ctx.register_low;
  uint8_t lower_high = static_cast<uint8_t>(std::min(mid_point + kOverlap, 127));
  uint8_t upper_low = static_cast<uint8_t>(std::max(mid_point - kOverlap, 0));
  uint8_t upper_high = ctx.register_high;

  uint8_t upper_prev = 0;
  uint8_t lower_prev = 0;
  bool upper_just_switched = false;  // True when we just jumped to upper voice.
  bool lower_just_switched = false;  // True when we just jumped to lower voice.

  bool use_upper = true;
  bool was_upper = true;  // Track the voice from the previous note.

  // Voice alternation probability depends on rhythm profile.
  float alt_prob = 0.8f;
  if (ctx.rhythm_profile == RhythmProfile::QuarterNote) {
    alt_prob = 1.0f;  // Always alternate with 1 note/beat.
  } else if (ctx.rhythm_profile == RhythmProfile::Triplet) {
    alt_prob = 0.75f;
  } else if (ctx.rhythm_profile == RhythmProfile::Sixteenth) {
    alt_prob = 0.85f;
  }

  // Seed-dependent variation of alternation probability.
  float alt_variation = rng::rollFloat(rng, -0.10f, 0.10f);
  alt_prob = std::max(0.5f, std::min(1.0f, alt_prob + alt_variation));

  for (Tick bar_offset = 0; bar_offset < ctx.duration_ticks; bar_offset += kTicksPerBar) {
    Tick bar_tick = ctx.start_tick + bar_offset;

    for (int beat_idx = 0; beat_idx < kBeatsPerBar; ++beat_idx) {
      Tick beat_tick = bar_tick + static_cast<Tick>(beat_idx) * kTicksPerBeat;
      Tick tick_in_bar = static_cast<Tick>(beat_idx) * kTicksPerBeat;

      const HarmonicEvent& harm = timeline.getAt(beat_tick);

      std::vector<uint8_t> upper_pitches = chordToPitches(
          harm.chord, harm.is_minor, upper_low, upper_high);
      std::vector<uint8_t> lower_pitches = chordToPitches(
          harm.chord, harm.is_minor, lower_low, lower_high);

      for (const auto& [sub_offset, sub_duration] : subdivisions) {
        Tick note_tick = beat_tick + sub_offset;
        Tick note_tick_in_bar = tick_in_bar + sub_offset;

        // Detect voice switch: we just moved to a different voice register.
        bool voice_switched = (use_upper != was_upper);

        uint8_t pitch;
        if (use_upper) {
          if (upper_pitches.empty()) {
            std::vector<uint8_t> fallback = chordToPitches(
                harm.chord, harm.is_minor, ctx.register_low, ctx.register_high);
            pitch = fallback.empty() ? ctx.register_high : fallback.back();
          } else if (upper_prev == 0) {
            // First note in upper voice: pick a chord tone.
            pitch = upper_pitches[upper_pitches.size() / 2];
            upper_just_switched = false;
          } else if (voice_switched) {
            // Just switched to upper voice. The leap to this register IS the
            // polyphonic jump. Use a chord tone near the previous upper pitch.
            pitch = nearestChordTone(upper_prev, upper_pitches);
            upper_just_switched = true;
          } else if (upper_just_switched) {
            // First continuation note after switching to upper voice.
            // Connect by diatonic step to the previous note in this voice.
            uint8_t target_ct = nearestChordTone(upper_prev, upper_pitches);
            pitch = nearestDiatonicStep(upper_prev, target_ct,
                                        ctx.key.tonic, scale_type,
                                        upper_low, upper_high);
            upper_just_switched = false;
          } else {
            // Continuing in upper voice. Prefer stepwise motion.
            bool is_strong = isStrongBeatPosition(note_tick_in_bar);
            if (is_strong && sub_offset == 0) {
              // Strong beat: chord tone anchor.
              pitch = nearestChordTone(upper_prev, upper_pitches);
            } else {
              // Weak beat/subdivision: diatonic step toward nearest chord tone.
              uint8_t target_ct = nearestChordTone(upper_prev, upper_pitches);
              pitch = nearestDiatonicStep(upper_prev, target_ct,
                                          ctx.key.tonic, scale_type,
                                          upper_low, upper_high);

              // 8% chance of chord-tone snap for variety (reduced from 15%
              // to increase within-voice stepwise continuity).
              if (upper_pitches.size() > 1 && rng::rollProbability(rng, 0.08f)) {
                pitch = nearestChordTone(upper_prev, upper_pitches);
              }
            }
          }
          // Limit leap within implied voice to octave (12 semitones).
          if (upper_prev > 0) {
            int leap = absoluteInterval(pitch, upper_prev);
            if (leap > 12) {
              uint8_t close_target = clampPitch(
                  static_cast<int>(upper_prev) + ((pitch > upper_prev) ? 7 : -7),
                  upper_low, upper_high);
              pitch = nearestChordTone(close_target, upper_pitches);
            }
          }
          upper_prev = pitch;
        } else {
          if (lower_pitches.empty()) {
            std::vector<uint8_t> fallback = chordToPitches(
                harm.chord, harm.is_minor, ctx.register_low, ctx.register_high);
            pitch = fallback.empty() ? ctx.register_low : fallback.front();
          } else if (lower_prev == 0) {
            // First note in lower voice: pick a chord tone.
            pitch = lower_pitches[lower_pitches.size() / 2];
            lower_just_switched = false;
          } else if (voice_switched) {
            // Just switched to lower voice. Use chord tone near previous lower pitch.
            pitch = nearestChordTone(lower_prev, lower_pitches);
            lower_just_switched = true;
          } else if (lower_just_switched) {
            // First continuation after switching to lower voice: connect by step.
            uint8_t target_ct = nearestChordTone(lower_prev, lower_pitches);
            pitch = nearestDiatonicStep(lower_prev, target_ct,
                                        ctx.key.tonic, scale_type,
                                        lower_low, lower_high);
            lower_just_switched = false;
          } else {
            // Continuing in lower voice. Prefer stepwise motion.
            bool is_strong = isStrongBeatPosition(note_tick_in_bar);
            if (is_strong && sub_offset == 0) {
              // Strong beat: chord tone anchor.
              pitch = nearestChordTone(lower_prev, lower_pitches);
            } else {
              // Weak beat/subdivision: diatonic step toward nearest chord tone.
              uint8_t target_ct = nearestChordTone(lower_prev, lower_pitches);
              pitch = nearestDiatonicStep(lower_prev, target_ct,
                                          ctx.key.tonic, scale_type,
                                          lower_low, lower_high);

              // 8% chance of chord-tone snap for variety (reduced from 15%
              // to increase within-voice stepwise continuity).
              if (lower_pitches.size() > 1 && rng::rollProbability(rng, 0.08f)) {
                pitch = nearestChordTone(lower_prev, lower_pitches);
              }
            }
          }
          // Limit leap within implied voice to octave (12 semitones).
          if (lower_prev > 0) {
            int leap = absoluteInterval(pitch, lower_prev);
            if (leap > 12) {
              uint8_t close_target = clampPitch(
                  static_cast<int>(lower_prev) + ((pitch > lower_prev) ? 7 : -7),
                  lower_low, lower_high);
              pitch = nearestChordTone(close_target, lower_pitches);
            }
          }
          lower_prev = pitch;
        }

        notes.push_back(makeTextureNote(
            note_tick, sub_duration, pitch, note_tick_in_bar, ctx.is_climax));

        was_upper = use_upper;
        if (rng::rollProbability(rng, alt_prob)) {
          use_upper = !use_upper;
        }
      }
    }
  }

  return notes;
}

// ===========================================================================
// FullChords -- 3-4 note simultaneous chords (climax only)
// ===========================================================================

std::vector<NoteEvent> generateFullChords(const TextureContext& ctx,
                                          const HarmonicTimeline& timeline) {
  // FullChords texture is only permitted during the climax.
  // Returning empty for non-climax variations is a design decision.
  if (!ctx.is_climax) {
    return {};
  }

  std::vector<NoteEvent> notes;
  if (ctx.duration_ticks == 0) {
    return notes;
  }

  Tick num_bars = ctx.duration_ticks / kTicksPerBar;
  notes.reserve(static_cast<size_t>(num_bars) * 16);

  for (Tick bar_offset = 0; bar_offset < ctx.duration_ticks; bar_offset += kTicksPerBar) {
    Tick bar_tick = ctx.start_tick + bar_offset;

    // Place a chord at each half-bar (2 chords per bar).
    for (int half_idx = 0; half_idx < 2; ++half_idx) {
      Tick chord_tick = bar_tick + static_cast<Tick>(half_idx) * (kTicksPerBar / 2);
      Tick tick_in_bar = static_cast<Tick>(half_idx) * (kTicksPerBar / 2);

      const HarmonicEvent& harm = timeline.getAt(chord_tick);
      std::vector<uint8_t> chord_pitches = chordToPitches(
          harm.chord, harm.is_minor, ctx.register_low, ctx.register_high);

      if (chord_pitches.empty()) continue;

      // Ensure at least 3 notes. If we only have a triad, that is fine.
      // If we have fewer than 3, duplicate the root in another octave.
      while (chord_pitches.size() < 3) {
        int doubled = static_cast<int>(chord_pitches[0]) + 12;
        if (doubled <= static_cast<int>(ctx.register_high)) {
          chord_pitches.push_back(static_cast<uint8_t>(doubled));
        } else {
          break;
        }
      }
      std::sort(chord_pitches.begin(), chord_pitches.end());

      // Duration for the sustained notes (half bar minus grace note time).
      Tick half_bar_duration = kTicksPerBar / 2;
      Tick sustained_duration = half_bar_duration - kGraceNoteDuration * 2;
      if (sustained_duration < kEighthDuration) {
        sustained_duration = kEighthDuration;
      }

      // Build the arpeggiated chord roll:
      // - First 2 notes: short grace notes (kGraceNoteDuration each)
      // - Remaining notes: sustained for the rest of the half-bar
      size_t num_chord_notes = chord_pitches.size();

      for (size_t note_idx = 0; note_idx < num_chord_notes; ++note_idx) {
        Tick note_tick;
        Tick duration;
        Tick note_tick_in_bar;

        if (note_idx < 2) {
          // Grace notes: arpeggiated "roll" preceding the sustained notes.
          note_tick = chord_tick + static_cast<Tick>(note_idx) * kGraceNoteDuration;
          duration = kGraceNoteDuration;
          note_tick_in_bar = tick_in_bar + static_cast<Tick>(note_idx) * kGraceNoteDuration;
        } else {
          // Sustained notes: start after the grace notes, ring for the rest.
          note_tick = chord_tick + 2 * kGraceNoteDuration;
          duration = sustained_duration;
          note_tick_in_bar = tick_in_bar + 2 * kGraceNoteDuration;
        }

        notes.push_back(makeTextureNote(
            note_tick, duration, chord_pitches[note_idx],
            note_tick_in_bar, true));
      }
    }
  }

  return notes;
}

// ===========================================================================
// Arpeggiated -- broken chord patterns reusing Flow ArpeggioPattern
// ===========================================================================

std::vector<NoteEvent> generateArpeggiated(const TextureContext& ctx,
                                           const HarmonicTimeline& timeline) {
  std::vector<NoteEvent> notes;
  if (ctx.duration_ticks == 0) {
    return notes;
  }

  std::mt19937 rng(ctx.seed);

  auto subdivisions = getRhythmSubdivisions(ctx.rhythm_profile);
  Tick num_bars = ctx.duration_ticks / kTicksPerBar;
  notes.reserve(static_cast<size_t>(num_bars) * subdivisions.size() * kBeatsPerBar);

  Tick half_duration = ctx.duration_ticks / 2;

  // Seed-based starting pitch offset within chord voicing.
  int start_offset = rng::rollRange(rng, 0, 2);

  // Track previous pattern type for persistence; reset at half-section boundaries.
  ArpeggioPatternType prev_pattern = ArpeggioPatternType::Rising;

  for (Tick bar_offset = 0; bar_offset < ctx.duration_ticks; bar_offset += kTicksPerBar) {
    Tick bar_tick = ctx.start_tick + bar_offset;

    ArcPhase phase = (bar_offset < half_duration) ? ArcPhase::Ascent : ArcPhase::Descent;

    Tick bars_in_half = half_duration / kTicksPerBar;
    Tick bar_in_half = (bar_offset < half_duration)
        ? bar_offset / kTicksPerBar
        : (bar_offset - half_duration) / kTicksPerBar;

    PatternRole role;
    if (bars_in_half <= 1) {
      role = PatternRole::Drive;
    } else if (bar_in_half == 0) {
      role = PatternRole::Drive;
    } else if (bar_in_half >= bars_in_half - 1) {
      role = PatternRole::Release;
    } else if (bar_in_half < bars_in_half / 2) {
      role = PatternRole::Expand;
    } else {
      role = PatternRole::Sustain;
    }

    for (int beat_idx = 0; beat_idx < kBeatsPerBar; ++beat_idx) {
      Tick beat_tick = bar_tick + static_cast<Tick>(beat_idx) * kTicksPerBeat;
      Tick tick_in_bar = static_cast<Tick>(beat_idx) * kTicksPerBeat;

      const HarmonicEvent& harm = timeline.getAt(beat_tick);
      std::vector<int> chord_degrees = getChordDegrees(harm.chord.quality);

      bool is_section_start = (bar_in_half == 0 && beat_idx == 0);

      ArpeggioPattern pattern = generatePattern(
          chord_degrees, phase, role, false,
          rng, prev_pattern, is_section_start);
      prev_pattern = pattern.type;

      if (pattern.degrees.empty()) continue;

      // Per-beat 20% chance to reverse arpeggio direction.
      std::vector<int> degrees = pattern.degrees;
      if (rng::rollProbability(rng, 0.2f)) {
        std::reverse(degrees.begin(), degrees.end());
      }

      int degree_count = static_cast<int>(degrees.size());

      for (size_t sub_idx = 0; sub_idx < subdivisions.size(); ++sub_idx) {
        const auto& [sub_offset, sub_duration] = subdivisions[sub_idx];
        Tick note_tick = beat_tick + sub_offset;
        Tick note_tick_in_bar = tick_in_bar + sub_offset;

        int deg_idx = (static_cast<int>(sub_idx) + start_offset) % degree_count;
        int pattern_degree = degrees[deg_idx];
        int offset = degreeToPitchOffset(pattern_degree, harm.is_minor);
        int raw_pitch = static_cast<int>(harm.chord.root_pitch) + offset;

        uint8_t pitch = fitPitchToRegister(
            clampPitch(raw_pitch, 0, 127),
            ctx.register_low, ctx.register_high);

        notes.push_back(makeTextureNote(
            note_tick, sub_duration, pitch, note_tick_in_bar, ctx.is_climax));
      }
    }
  }

  return notes;
}

// ===========================================================================
// ScalePassage -- scale runs connecting chord tones
// ===========================================================================

std::vector<NoteEvent> generateScalePassage(const TextureContext& ctx,
                                            const HarmonicTimeline& timeline) {
  std::vector<NoteEvent> notes;
  if (ctx.duration_ticks == 0) {
    return notes;
  }

  std::mt19937 rng(ctx.seed);

  auto subdivisions = getRhythmSubdivisions(ctx.rhythm_profile);
  Tick num_bars = ctx.duration_ticks / kTicksPerBar;
  notes.reserve(static_cast<size_t>(num_bars) * subdivisions.size() * kBeatsPerBar);

  std::vector<uint8_t> scale_pitches = getScalePitches(
      ctx.key.tonic, ctx.key.is_minor, ctx.register_low, ctx.register_high);

  if (scale_pitches.empty()) {
    return notes;
  }

  // Seed-dependent initial direction (Markov chain approach).
  bool ascending = rng::rollProbability(rng, 0.5f);

  // Seed-dependent starting pitch offset within register.
  int register_range = static_cast<int>(ctx.register_high) - static_cast<int>(ctx.register_low);
  int pitch_offset = rng::rollRange(rng, -register_range / 4, register_range / 4);

  // Seed-dependent direction reversal probability.
  float reversal_prob = rng::rollFloat(rng, 0.15f, 0.50f);

  for (Tick bar_offset = 0; bar_offset < ctx.duration_ticks; bar_offset += kTicksPerBar) {
    Tick bar_tick = ctx.start_tick + bar_offset;

    for (int beat_idx = 0; beat_idx < kBeatsPerBar; ++beat_idx) {
      Tick beat_tick = bar_tick + static_cast<Tick>(beat_idx) * kTicksPerBeat;
      Tick tick_in_bar = static_cast<Tick>(beat_idx) * kTicksPerBeat;

      const HarmonicEvent& harm = timeline.getAt(beat_tick);

      std::vector<uint8_t> chord_pitches = chordToPitches(
          harm.chord, harm.is_minor, ctx.register_low, ctx.register_high);

      if (chord_pitches.empty()) continue;

      uint8_t mid = static_cast<uint8_t>(std::max(
          static_cast<int>(ctx.register_low),
          std::min(static_cast<int>(ctx.register_high),
                   (static_cast<int>(ctx.register_low) +
                    static_cast<int>(ctx.register_high)) / 2 + pitch_offset)));
      uint8_t start_pitch = nearestChordTone(mid, chord_pitches);

      int start_idx = 0;
      int best_dist = 999;
      for (int idx = 0; idx < static_cast<int>(scale_pitches.size()); ++idx) {
        int dist = std::abs(
            static_cast<int>(scale_pitches[idx]) - static_cast<int>(start_pitch));
        if (dist < best_dist) {
          best_dist = dist;
          start_idx = idx;
        }
      }

      for (size_t sub_idx = 0; sub_idx < subdivisions.size(); ++sub_idx) {
        const auto& [sub_offset, sub_duration] = subdivisions[sub_idx];
        Tick note_tick = beat_tick + sub_offset;
        Tick note_tick_in_bar = tick_in_bar + sub_offset;

        // Occasional step skipping for melodic variety (10% chance to skip 1 step).
        int step = static_cast<int>(sub_idx);
        if (sub_idx > 0 && rng::rollProbability(rng, 0.10f)) {
          step += 1;  // Skip one scale step.
        }
        int scale_idx;
        if (ascending) {
          scale_idx = start_idx + step;
        } else {
          scale_idx = start_idx - step;
        }

        if (scale_idx < 0) {
          scale_idx = 0;
        }
        if (scale_idx >= static_cast<int>(scale_pitches.size())) {
          scale_idx = static_cast<int>(scale_pitches.size()) - 1;
        }

        uint8_t pitch = scale_pitches[scale_idx];
        notes.push_back(makeTextureNote(
            note_tick, sub_duration, pitch, note_tick_in_bar, ctx.is_climax));
      }

      // Markov chain direction reversal (seed-dependent probability).
      if (rng::rollProbability(rng, reversal_prob)) {
        ascending = !ascending;
      }
    }
  }

  return notes;
}

// ===========================================================================
// Bariolage -- open string alternation
// ===========================================================================

std::vector<NoteEvent> generateBariolage(const TextureContext& ctx,
                                         const HarmonicTimeline& timeline) {
  std::vector<NoteEvent> notes;
  if (ctx.duration_ticks == 0) {
    return notes;
  }

  std::mt19937 rng(ctx.seed);

  auto subdivisions = getRhythmSubdivisions(ctx.rhythm_profile);
  Tick num_bars = ctx.duration_ticks / kTicksPerBar;
  notes.reserve(static_cast<size_t>(num_bars) * subdivisions.size() * kBeatsPerBar);

  for (Tick bar_offset = 0; bar_offset < ctx.duration_ticks; bar_offset += kTicksPerBar) {
    Tick bar_tick = ctx.start_tick + bar_offset;

    for (int beat_idx = 0; beat_idx < kBeatsPerBar; ++beat_idx) {
      Tick beat_tick = bar_tick + static_cast<Tick>(beat_idx) * kTicksPerBeat;
      Tick tick_in_bar = static_cast<Tick>(beat_idx) * kTicksPerBeat;

      const HarmonicEvent& harm = timeline.getAt(beat_tick);
      std::vector<uint8_t> chord_pitches = chordToPitches(
          harm.chord, harm.is_minor, ctx.register_low, ctx.register_high);

      if (chord_pitches.empty()) continue;

      // Select chord tone using RNG for variety across seeds.
      uint8_t stopped_pitch;
      if (chord_pitches.size() > 1) {
        // Build candidates: chord tones near open strings.
        std::vector<uint8_t> candidates;
        for (uint8_t cp : chord_pitches) {
          uint8_t nearest_open = findNearestOpenString(cp, ctx.register_low, ctx.register_high);
          if (nearest_open > 0) {
            int dist = absoluteInterval(cp, nearest_open);
            if (dist > 0 && dist <= 7) {
              candidates.push_back(cp);
            }
          }
        }
        if (!candidates.empty()) {
          stopped_pitch = rng::selectRandom(rng, candidates);
        } else {
          stopped_pitch = rng::selectRandom(rng, chord_pitches);
        }
      } else {
        stopped_pitch = chord_pitches[0];
      }

      uint8_t open_pitch = findNearestOpenString(
          stopped_pitch, ctx.register_low, ctx.register_high);

      if (open_pitch == 0) {
        open_pitch = chord_pitches.front();
      }

      for (size_t sub_idx = 0; sub_idx < subdivisions.size(); ++sub_idx) {
        const auto& [sub_offset, sub_duration] = subdivisions[sub_idx];
        Tick note_tick = beat_tick + sub_offset;
        Tick note_tick_in_bar = tick_in_bar + sub_offset;

        // 25% chance of double stop (both pitches simultaneously).
        if (stopped_pitch != open_pitch && rng::rollProbability(rng, 0.25f)) {
          notes.push_back(makeTextureNote(
              note_tick, sub_duration, stopped_pitch, note_tick_in_bar, ctx.is_climax));
          notes.push_back(makeTextureNote(
              note_tick, sub_duration, open_pitch, note_tick_in_bar, ctx.is_climax));
        } else if (stopped_pitch == open_pitch) {
          // Vocabulary fallback: when no open string contrast available,
          // use kBariolage figure's chromatic neighbor alternation pattern.
          // kBariolage encodes a 4-note figure with degree_intervals:
          //   {+1,-1}, {-1,+1}, {+1,-1} -- alternating neighbor directions.
          // Strong beat guarantee: always resolve to stopped_pitch on strong beats.
          bool is_strong_beat = (note_tick_in_bar % kTicksPerBeat == 0);
          int offset = 0;
          if (!is_strong_beat && kBariolage.degree_intervals != nullptr) {
            // Cycle through kBariolage degree intervals for alternation direction.
            // note_count-1 intervals for note_count notes; sub_idx 0 is the
            // anchor note, so intervals start at sub_idx 1.
            int interval_count = static_cast<int>(kBariolage.note_count) - 1;
            if (interval_count > 0 && sub_idx > 0) {
              int ivl_idx = (static_cast<int>(sub_idx) - 1) % interval_count;
              offset = kBariolage.degree_intervals[ivl_idx].degree_diff;
            }
          }
          uint8_t alt_pitch = clampPitch(
              static_cast<int>(stopped_pitch) + offset,
              ctx.register_low, ctx.register_high);
          notes.push_back(makeTextureNote(
              note_tick, sub_duration, alt_pitch, note_tick_in_bar, ctx.is_climax));
        } else {
          uint8_t pitch = (sub_idx % 2 == 0) ? stopped_pitch : open_pitch;
          notes.push_back(makeTextureNote(
              note_tick, sub_duration, pitch, note_tick_in_bar, ctx.is_climax));
        }
      }
    }
  }

  return notes;
}

}  // namespace bach
