// Implementation of texture generation for chaconne variations (Arch system).

#include "solo_string/arch/texture_generator.h"

#include <algorithm>
#include <cmath>
#include <random>

#include "core/note_source.h"
#include "core/pitch_utils.h"
#include "core/rng_util.h"
#include "core/scale.h"
#include "harmony/chord_types.h"
#include "harmony/harmonic_event.h"
#include "harmony/scale_degree_utils.h"
#include "solo_string/flow/arpeggio_pattern.h"

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
constexpr Tick kEighthDuration = kTicksPerBeat / 2;  // 240

/// @brief Duration of a 16th note in ticks.
constexpr Tick kSixteenthDuration = kTicksPerBeat / 4;  // 120

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
    int pitch_class = ((raw_pitch % 12) + 12) % 12;
    int best_pitch = -1;
    int best_distance = 999;

    for (int oct = 0; oct <= 10; ++oct) {
      int candidate = (oct + 1) * 12 + pitch_class;
      if (candidate >= static_cast<int>(reg_low) &&
          candidate <= static_cast<int>(reg_high)) {
        int distance = std::abs(candidate - raw_pitch);
        if (distance < best_distance) {
          best_distance = distance;
          best_pitch = candidate;
        }
      }
    }

    if (best_pitch >= 0) {
      pitches.push_back(static_cast<uint8_t>(best_pitch));
    } else {
      // Clamp to boundary if no octave fits.
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

  int pitch_class = static_cast<int>(pitch) % 12;
  int best_pitch = -1;
  int best_distance = 999;

  for (int oct = 0; oct <= 10; ++oct) {
    int candidate = (oct + 1) * 12 + pitch_class;
    if (candidate >= static_cast<int>(reg_low) &&
        candidate <= static_cast<int>(reg_high)) {
      int distance = std::abs(candidate - static_cast<int>(pitch));
      if (distance < best_distance) {
        best_distance = distance;
        best_pitch = candidate;
      }
    }
  }

  if (best_pitch >= 0) {
    return static_cast<uint8_t>(best_pitch);
  }

  // No octave placement works -- clamp.
  if (pitch < reg_low) return reg_low;
  return reg_high;
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

/// @brief Select the chord tone nearest to a target pitch from a set of chord tones.
/// @param target Target MIDI pitch.
/// @param chord_pitches Available chord tones.
/// @return Nearest chord tone, or target if chord_pitches is empty.
uint8_t nearestChordTone(uint8_t target, const std::vector<uint8_t>& chord_pitches) {
  if (chord_pitches.empty()) {
    return target;
  }

  uint8_t best = chord_pitches[0];
  int best_dist = absoluteInterval(target, best);

  for (size_t idx = 1; idx < chord_pitches.size(); ++idx) {
    int dist = absoluteInterval(target, chord_pitches[idx]);
    if (dist < best_dist) {
      best_dist = dist;
      best = chord_pitches[idx];
    }
  }

  return best;
}

/// @brief Compute velocity with beat-position-aware accents.
/// @param tick_in_bar Position within the bar (0 to kTicksPerBar-1).
/// @param base_velocity Base velocity value.
/// @param is_climax True for additional climax accent.
/// @return Adjusted velocity value clamped to [1, 127].
uint8_t computeVelocity(Tick tick_in_bar, uint8_t base_velocity, bool is_climax) {
  int vel = static_cast<int>(base_velocity);

  // Strong beat accent (beat 0 and beat 2 in 4/4).
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
  note.voice = 0;  // Solo instrument
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
    int pc = static_cast<int>(notes[i].pitch) % 12;
    int prev = static_cast<int>(notes[i - 1].pitch);
    int best = static_cast<int>(notes[i].pitch);
    int best_dist = leap;

    for (int oct = 0; oct <= 10; ++oct) {
      int cand = oct * 12 + pc;
      if (cand < static_cast<int>(reg_low) || cand > static_cast<int>(reg_high)) continue;
      int dist = std::abs(cand - prev);
      if (dist < best_dist) {
        best_dist = dist;
        best = cand;
      }
    }
    notes[i].pitch = static_cast<uint8_t>(best);
  }
}

/// @brief Enforce leap resolution on a note sequence.
///
/// For leaps >= 7 semitones (P5), attempts to resolve the following note by
/// contrary stepwise motion. Modifies notes in place.
void enforceLeapResolution(std::vector<NoteEvent>& notes, Key key, bool is_minor) {
  if (notes.size() < 3) return;

  ScaleType scale = is_minor ? ScaleType::NaturalMinor : ScaleType::Major;

  for (size_t i = 1; i + 1 < notes.size(); ++i) {
    int leap = static_cast<int>(notes[i].pitch) - static_cast<int>(notes[i - 1].pitch);
    int abs_leap = std::abs(leap);

    if (abs_leap < 7) continue;  // Only resolve P5+ leaps.

    // Check if next note already resolves by contrary step.
    int next_motion = static_cast<int>(notes[i + 1].pitch) - static_cast<int>(notes[i].pitch);
    bool is_contrary = (leap > 0 && next_motion < 0) || (leap < 0 && next_motion > 0);
    bool is_step = std::abs(next_motion) <= 2;

    if (is_contrary && is_step) continue;  // Already resolved.

    // Try to resolve: move the next note to a contrary-step scale tone.
    int resolve_dir = (leap > 0) ? -1 : 1;
    for (int offset = 1; offset <= 2; ++offset) {
      int cand = static_cast<int>(notes[i].pitch) + resolve_dir * offset;
      if (cand < 0 || cand > 127) continue;
      auto cand_u8 = static_cast<uint8_t>(cand);
      if (scale_util::isScaleTone(cand_u8, key, scale)) {
        notes[i + 1].pitch = cand_u8;
        break;
      }
    }
  }
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
    enforceLeapResolution(notes, ctx.key.tonic, ctx.key.is_minor);
  }

  return notes;
}

// ===========================================================================
// SingleLine -- simple melody following chord tones
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

  // Track previous pitch for stepwise preference.
  uint8_t prev_pitch = 0;
  bool just_leaped = false;
  int last_leap_direction = 0;  // +1 = up, -1 = down

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
          // First note: pick a chord tone in the middle of the register.
          uint8_t mid = static_cast<uint8_t>(
              (static_cast<int>(ctx.register_low) + static_cast<int>(ctx.register_high)) / 2);
          pitch = nearestChordTone(mid, chord_pitches);
        } else if (just_leaped) {
          // Compensation after leap: move in the opposite direction.
          // Compensation amount scales with leap size (min 1, max 4 semitones).
          int compensation_dir = (last_leap_direction > 0) ? -1 : 1;
          int leap_size = absoluteInterval(prev_pitch,
                                          notes.empty() ? prev_pitch
                                              : notes.back().pitch);
          int comp_amount = std::min(std::max(leap_size / 2, 1), 4);
          // Prefer 1-2 semitone compensation first.
          bool found = false;
          for (int try_amt = std::min(comp_amount, 2); try_amt <= comp_amount; ++try_amt) {
            int target = static_cast<int>(prev_pitch) + compensation_dir * try_amt;
            if (target >= static_cast<int>(ctx.register_low) &&
                target <= static_cast<int>(ctx.register_high)) {
              pitch = nearestChordTone(static_cast<uint8_t>(target), chord_pitches);
              found = true;
              break;
            }
          }
          if (!found) {
            pitch = nearestChordTone(prev_pitch, chord_pitches);
          }
          just_leaped = false;
        } else {
          pitch = nearestChordTone(prev_pitch, chord_pitches);

          // 15% chance of leap to a non-nearest chord tone for melodic interest.
          if (chord_pitches.size() > 1 && rng::rollProbability(rng, 0.15f)) {
            // Pick a chord tone that is NOT the nearest.
            uint8_t nearest = pitch;
            std::vector<uint8_t> leap_candidates;
            for (uint8_t cp : chord_pitches) {
              if (cp != nearest) {
                leap_candidates.push_back(cp);
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
          } else if (chord_pitches.size() > 1 && rng::rollProbability(rng, 0.3f)) {
            // Small random variation (step up or down by a 2nd).
            int direction = rng::rollProbability(rng, 0.5f) ? 1 : -1;
            int target = static_cast<int>(pitch) + direction * 2;
            if (target >= static_cast<int>(ctx.register_low) &&
                target <= static_cast<int>(ctx.register_high)) {
              pitch = nearestChordTone(static_cast<uint8_t>(target), chord_pitches);
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
// ImpliedPolyphony -- alternating upper/lower voices
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

  // Divide register into upper and lower halves with seed-dependent offset.
  int full_range = static_cast<int>(ctx.register_high) - static_cast<int>(ctx.register_low);
  int split_offset = rng::rollRange(rng, -1, 1);  // ±1 semitone (reduced from ±3)
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

  bool use_upper = true;

  // Voice alternation probability depends on rhythm profile.
  float alt_prob = 0.8f;
  if (ctx.rhythm_profile == RhythmProfile::QuarterNote) {
    alt_prob = 1.0f;  // Always alternate with 1 note/beat.
  } else if (ctx.rhythm_profile == RhythmProfile::Triplet) {
    alt_prob = 0.75f;
  } else if (ctx.rhythm_profile == RhythmProfile::Sixteenth) {
    alt_prob = 0.85f;
  }

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

        uint8_t pitch;
        if (use_upper) {
          if (upper_pitches.empty()) {
            std::vector<uint8_t> fallback = chordToPitches(
                harm.chord, harm.is_minor, ctx.register_low, ctx.register_high);
            pitch = fallback.empty() ? ctx.register_high : fallback.back();
          } else if (upper_prev == 0) {
            pitch = upper_pitches[upper_pitches.size() / 2];
          } else {
            pitch = nearestChordTone(upper_prev, upper_pitches);
            // Limit leap within implied voice to octave (12 semitones).
            int leap = absoluteInterval(pitch, upper_prev);
            if (leap > 12) {
              // Find chord tone within 7 semitones of previous pitch.
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
            pitch = lower_pitches[lower_pitches.size() / 2];
          } else {
            pitch = nearestChordTone(lower_prev, lower_pitches);
            // Limit leap within implied voice to octave (12 semitones).
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

      ArpeggioPattern pattern = generatePattern(
          chord_degrees, phase, role, false);

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
            static_cast<uint8_t>(std::max(0, std::min(127, raw_pitch))),
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

        int scale_idx;
        if (ascending) {
          scale_idx = start_idx + static_cast<int>(sub_idx);
        } else {
          scale_idx = start_idx - static_cast<int>(sub_idx);
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

      // Markov chain direction: 70% continue, 30% reverse.
      if (rng::rollProbability(rng, 0.3f)) {
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

        uint8_t pitch = (sub_idx % 2 == 0) ? stopped_pitch : open_pitch;

        notes.push_back(makeTextureNote(
            note_tick, sub_duration, pitch, note_tick_in_bar, ctx.is_climax));
      }
    }
  }

  return notes;
}

}  // namespace bach
