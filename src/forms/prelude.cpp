// Implementation of the organ prelude generator.

#include "forms/prelude.h"

#include <algorithm>
#include <random>
#include <vector>

#include "core/gm_program.h"
#include "core/note_creator.h"
#include "core/pitch_utils.h"
#include "core/rng_util.h"
#include "core/scale.h"
#include "harmony/chord_tone_utils.h"
#include "harmony/chord_types.h"
#include "harmony/harmonic_event.h"
#include "organ/organ_techniques.h"

namespace bach {

namespace {

using namespace duration;

/// @brief Default prelude length in bars when fugue length is unknown.
constexpr Tick kDefaultPreludeBars = 12;

/// @brief Prelude-to-fugue length ratio (midpoint of 60-80% range).
constexpr float kPreludeFugueRatio = 0.70f;

// ---------------------------------------------------------------------------
// Track creation (mirrors fugue_generator organ track setup)
// ---------------------------------------------------------------------------

/// @brief Create MIDI tracks for an organ prelude.
///
/// Channel/program mapping per the organ system spec:
///   Voice 0 -> Ch 0, Church Organ (Manual I / Great)
///   Voice 1 -> Ch 1, Reed Organ   (Manual II / Swell)
///   Voice 2 -> Ch 2, Church Organ (Manual III / Positiv)
///   Voice 3 -> Ch 3, Church Organ (Pedal)
///
/// @param num_voices Number of voices (2-5).
/// @return Vector of Track objects with channel/program/name configured.
std::vector<Track> createPreludeTracks(uint8_t num_voices) {
  std::vector<Track> tracks;
  tracks.reserve(num_voices);

  struct TrackSpec {
    uint8_t channel;
    uint8_t program;
    const char* name;
  };

  static constexpr TrackSpec kSpecs[] = {
      {0, GmProgram::kChurchOrgan, "Manual I (Great)"},
      {1, GmProgram::kReedOrgan, "Manual II (Swell)"},
      {2, GmProgram::kChurchOrgan, "Manual III (Positiv)"},
      {3, GmProgram::kChurchOrgan, "Pedal"},
      {4, GmProgram::kChurchOrgan, "Manual IV"},
  };

  for (uint8_t idx = 0; idx < num_voices && idx < 5; ++idx) {
    Track track;
    track.channel = kSpecs[idx].channel;
    track.program = kSpecs[idx].program;
    track.name = kSpecs[idx].name;
    tracks.push_back(track);
  }

  return tracks;
}

// ---------------------------------------------------------------------------
// Pitch range helpers
// ---------------------------------------------------------------------------

/// @brief Get the organ manual low pitch for a voice index.
/// @param voice_idx Voice index (0-based).
/// @return Low MIDI pitch bound for the manual.
uint8_t getVoiceLowPitch(uint8_t voice_idx) {
  switch (voice_idx) {
    case 0: return organ_range::kManual1Low;   // 36
    case 1: return organ_range::kManual2Low;   // 36
    case 2: return organ_range::kManual3Low;   // 48
    case 3: return organ_range::kPedalLow;     // 24
    default: return organ_range::kManual1Low;
  }
}

/// @brief Get the organ manual high pitch for a voice index.
/// @param voice_idx Voice index (0-based).
/// @return High MIDI pitch bound for the manual.
uint8_t getVoiceHighPitch(uint8_t voice_idx) {
  switch (voice_idx) {
    case 0: return organ_range::kManual1High;  // 96
    case 1: return organ_range::kManual2High;  // 96
    case 2: return organ_range::kManual3High;  // 96
    case 3: return organ_range::kPedalHigh;    // 50
    default: return organ_range::kManual1High;
  }
}

/// @brief Get the register center range for a voice to ensure separation.
///
/// Returns (low, high) bounds for the register center pitch.
/// Voice 0 (soprano): G4-G5 region for passage work.
/// Voice 1 (alto): G3-E4 region for middle voice.
/// Voice 2+ (bass): handled separately by generateBassVoice.
///
/// @param voice_idx Voice index (0-based).
/// @param num_voices Total voice count.
/// @return Pair of (center_low, center_high) MIDI pitches.
std::pair<int, int> getVoiceCenterRange(uint8_t voice_idx, uint8_t num_voices) {
  if (num_voices <= 2) {
    // 2-voice: voice 0 higher, voice 1 lower.
    if (voice_idx == 0) return {64, 79};  // E4-G5
    return {48, 62};                       // C3-D4
  }
  // 3+ voices: clear register separation.
  switch (voice_idx) {
    case 0: return {67, 79};   // G4-G5 (soprano passage)
    case 1: return {55, 65};   // G3-F4 (alto middle)
    default: return {48, 60};  // C3-C4 (tenor/bass, if used for passage)
  }
}

// ---------------------------------------------------------------------------
// Strong beat detection
// ---------------------------------------------------------------------------

/// @brief Check if a tick position falls on a strong beat (beat 1 or 3 in 4/4).
/// @param tick Absolute tick position.
/// @return True if on beat 1 or beat 3 of the current bar.
bool isStrongBeat(Tick tick) {
  Tick in_bar = tick % kTicksPerBar;
  return (in_bar == 0 || in_bar == kTicksPerBar / 2);
}

// ---------------------------------------------------------------------------
// Pattern generators
// ---------------------------------------------------------------------------

/// @brief Generate ascending/descending scale passage notes for one event span.
///
/// Creates a sequence of notes using scale tones, alternating between
/// ascending and descending patterns based on the pattern_index.
///
/// @param event Current harmonic event.
/// @param voice_idx Voice/track index.
/// @param note_duration Duration per note in ticks.
/// @param pattern_index Selects ascending (even) or descending (odd).
/// @param rng Random number generator for starting pitch selection.
/// @return Vector of NoteEvent entries.
std::vector<NoteEvent> generateScalePassage(const HarmonicEvent& event,
                                            uint8_t voice_idx,
                                            Tick note_duration,
                                            int pattern_index,
                                            std::mt19937& rng,
                                            int hint_center = -1) {
  std::vector<NoteEvent> notes;
  uint8_t low_pitch = getVoiceLowPitch(voice_idx);
  uint8_t high_pitch = getVoiceHighPitch(voice_idx);

  auto all_scale_tones = getScaleTones(event.key, event.is_minor, low_pitch, high_pitch);
  if (all_scale_tones.empty()) {
    return notes;
  }

  // Local register window: restrict scale runs to ~1.5 octaves around a center.
  constexpr int kRegisterWindow = 9;

  int center;
  if (hint_center >= 0) {
    auto [cr_lo, cr_hi] = getVoiceCenterRange(voice_idx, 3);
    center = std::clamp(hint_center, cr_lo, cr_hi);
  } else {
    auto [cr_lo, cr_hi] = getVoiceCenterRange(voice_idx, 3);
    center = rng::rollRange(rng, cr_lo, cr_hi);
  }

  int win_lo = std::max(static_cast<int>(low_pitch), center - kRegisterWindow);
  int win_hi = std::min(static_cast<int>(high_pitch), center + kRegisterWindow);

  std::vector<uint8_t> scale_tones;
  for (auto t : all_scale_tones) {
    if (t >= win_lo && t <= win_hi) {
      scale_tones.push_back(t);
    }
  }
  if (scale_tones.empty()) {
    scale_tones = all_scale_tones;
  }

  Tick event_duration = event.end_tick - event.tick;
  Tick current_tick = event.tick;
  bool ascending = (pattern_index % 2 == 0);

  size_t start_idx;
  if (ascending) {
    start_idx = rng::rollRange(rng, 0, static_cast<int>(scale_tones.size()) / 3);
  } else {
    int high_start = static_cast<int>(scale_tones.size()) - 1;
    int low_start = static_cast<int>(scale_tones.size()) * 2 / 3;
    if (low_start > high_start) low_start = high_start;
    start_idx = static_cast<size_t>(rng::rollRange(rng, low_start, high_start));
  }

  size_t tone_idx = start_idx;

  while (current_tick < event.tick + event_duration) {
    Tick remaining = (event.tick + event_duration) - current_tick;
    Tick dur = (remaining < note_duration) ? remaining : note_duration;
    if (dur == 0) break;

    NoteEvent note;
    note.start_tick = current_tick;
    note.duration = dur;
    note.pitch = scale_tones[tone_idx];
    note.velocity = kOrganVelocity;
    note.voice = voice_idx;
    note.source = BachNoteSource::FreeCounterpoint;
    notes.push_back(note);

    current_tick += dur;

    if (ascending) {
      if (tone_idx + 1 < scale_tones.size()) {
        ++tone_idx;
      } else {
        ascending = false;
        if (tone_idx > 0) --tone_idx;
      }
    } else {
      if (tone_idx > 0) {
        --tone_idx;
      } else {
        ascending = true;
        ++tone_idx;
      }
    }
  }

  return notes;
}

/// @brief Generate arpeggio pattern notes for one event span.
///
/// Creates notes that arpeggiate through chord tones, optionally including
/// passing tones between them.
///
/// @param event Current harmonic event.
/// @param voice_idx Voice/track index.
/// @param note_duration Duration per note in ticks.
/// @param rng Random number generator.
/// @return Vector of NoteEvent entries.
std::vector<NoteEvent> generateArpeggioPassage(const HarmonicEvent& event,
                                               uint8_t voice_idx,
                                               Tick note_duration,
                                               std::mt19937& rng,
                                               int hint_center = -1) {
  std::vector<NoteEvent> notes;
  uint8_t low_pitch = getVoiceLowPitch(voice_idx);
  uint8_t high_pitch = getVoiceHighPitch(voice_idx);

  // Build arpeggio pitches spanning the voice range.
  int root_pc = static_cast<int>(event.chord.root_pitch) % 12;
  int third_offset = 4;
  if (event.chord.quality == ChordQuality::Minor ||
      event.chord.quality == ChordQuality::Diminished ||
      event.chord.quality == ChordQuality::Minor7) {
    third_offset = 3;
  }
  int fifth_offset = 7;
  if (event.chord.quality == ChordQuality::Diminished) {
    fifth_offset = 6;
  }

  // Collect all chord tones across octaves within the voice range.
  std::vector<uint8_t> all_arp_pitches;
  for (int octave = 1; octave <= 8; ++octave) {
    int base = octave * 12 + root_pc;
    int pitches[] = {base, base + third_offset, base + fifth_offset};
    for (int pitch : pitches) {
      if (pitch >= static_cast<int>(low_pitch) &&
          pitch <= static_cast<int>(high_pitch) && pitch <= 127) {
        all_arp_pitches.push_back(static_cast<uint8_t>(pitch));
      }
    }
  }

  if (all_arp_pitches.empty()) {
    return notes;
  }

  std::sort(all_arp_pitches.begin(), all_arp_pitches.end());

  // Local register window: restrict arpeggio to ±9 semitones around a center.
  constexpr int kRegisterWindow = 9;
  constexpr int kMaxLeap = 8;

  int center;
  if (hint_center >= 0) {
    // Use the provided hint (previous passage's last pitch) for continuity,
    // but clamp to the voice's register range to prevent drift.
    auto [cr_lo, cr_hi] = getVoiceCenterRange(voice_idx, 3);
    center = std::clamp(hint_center, cr_lo, cr_hi);
  } else {
    auto [cr_lo, cr_hi] = getVoiceCenterRange(voice_idx, 3);
    center = rng::rollRange(rng, cr_lo, cr_hi);
  }

  // Filter chord tones to the register window.
  auto filterToWindow = [&](int c) -> std::vector<uint8_t> {
    std::vector<uint8_t> filtered;
    int win_lo = std::max(static_cast<int>(low_pitch), c - kRegisterWindow);
    int win_hi = std::min(static_cast<int>(high_pitch), c + kRegisterWindow);
    for (auto p : all_arp_pitches) {
      if (p >= win_lo && p <= win_hi) {
        filtered.push_back(p);
      }
    }
    return filtered;
  };

  std::vector<uint8_t> arp_pitches = filterToWindow(center);
  if (arp_pitches.empty()) {
    arp_pitches = all_arp_pitches;
  }

  Tick event_duration = event.end_tick - event.tick;
  Tick current_tick = event.tick;

  size_t arp_idx = arp_pitches.size() / 2;
  bool going_up = rng::rollProbability(rng, 0.6f);

  uint8_t prev_pitch = arp_pitches[arp_idx];

  while (current_tick < event.tick + event_duration) {
    Tick remaining = (event.tick + event_duration) - current_tick;
    Tick dur = (remaining < note_duration) ? remaining : note_duration;
    if (dur == 0) break;

    uint8_t candidate = arp_pitches[arp_idx];
    if (absoluteInterval(candidate, prev_pitch) > kMaxLeap) {
      int best_dist = 999;
      size_t best_idx = arp_idx;
      for (size_t i = 0; i < arp_pitches.size(); ++i) {
        int dist = absoluteInterval(arp_pitches[i], prev_pitch);
        if (dist <= kMaxLeap && dist < best_dist && arp_pitches[i] != prev_pitch) {
          best_dist = dist;
          best_idx = i;
        }
      }
      arp_idx = best_idx;
      candidate = arp_pitches[arp_idx];
    }

    NoteEvent note;
    note.start_tick = current_tick;
    note.duration = dur;
    note.pitch = candidate;
    note.velocity = kOrganVelocity;
    note.voice = voice_idx;
    note.source = BachNoteSource::FreeCounterpoint;
    notes.push_back(note);

    prev_pitch = candidate;
    current_tick += dur;

    if (going_up) {
      if (arp_idx + 1 < arp_pitches.size()) {
        ++arp_idx;
      } else {
        going_up = false;
        if (arp_idx > 0) --arp_idx;
      }
    } else {
      if (arp_idx > 0) {
        --arp_idx;
      } else {
        going_up = true;
        ++arp_idx;
      }
    }
  }

  return notes;
}

// ---------------------------------------------------------------------------
// Full-timeline middle voice generation
// ---------------------------------------------------------------------------

/// @brief Generate a melodic middle voice across the full harmonic timeline.
///
/// Produces a Baroque-style inner voice with mixed durations (half, quarter,
/// eighth notes), strong-beat chord tones, weak-beat passing/neighbor tones,
/// directional persistence, and cadential chord-tone enforcement.
///
/// @param timeline Full harmonic timeline for chord context lookups.
/// @param start_tick Start tick of the generation span.
/// @param end_tick End tick of the generation span.
/// @param voice_idx Voice/track index for the middle voice.
/// @param num_voices Total number of voices (for register separation).
/// @param rng Random number generator.
/// @return Vector of NoteEvent entries.
std::vector<NoteEvent> generateMiddleVoice(const HarmonicTimeline& timeline,
                                           Tick start_tick, Tick end_tick,
                                           uint8_t voice_idx, uint8_t num_voices,
                                           std::mt19937& rng) {
  std::vector<NoteEvent> notes;
  if (start_tick >= end_tick) return notes;

  uint8_t low_pitch = getVoiceLowPitch(voice_idx);
  uint8_t high_pitch = getVoiceHighPitch(voice_idx);
  auto [cr_lo, cr_hi] = getVoiceCenterRange(voice_idx, num_voices);

  // Allowed durations for the middle voice (quantized, never clamped to remainder).
  static constexpr Tick kMidDurs[] = {kHalfNote, kQuarterNote, kEighthNote};
  static constexpr int kNumMidDurs = 3;

  // Register window around center range for scale/chord tone filtering.
  constexpr int kMiddleWindow = 9;
  int reg_lo = std::max(static_cast<int>(low_pitch), cr_lo - kMiddleWindow);
  int reg_hi = std::min(static_cast<int>(high_pitch), cr_hi + kMiddleWindow);

  // Initialize previous pitch near the center of the register using the first chord.
  const auto& first_event = timeline.getAt(start_tick);
  ScaleType scale_type =
      first_event.is_minor ? ScaleType::HarmonicMinor : ScaleType::Major;
  uint8_t prev_pitch = nearestChordTone(
      static_cast<uint8_t>((cr_lo + cr_hi) / 2), first_event);
  prev_pitch = clampPitch(static_cast<int>(prev_pitch),
                          static_cast<uint8_t>(reg_lo),
                          static_cast<uint8_t>(reg_hi));

  // Directional persistence: maintain direction for 3-4 notes before reversal.
  int direction = 1;  // +1 ascending, -1 descending
  int notes_in_direction = 0;
  int direction_limit = rng::rollRange(rng, 3, 4);

  // Cadence region: last 2 beats of the piece.
  Tick cadence_start = (end_tick > kHalfNote) ? end_tick - kHalfNote : start_tick;

  Tick current_tick = start_tick;

  while (current_tick < end_tick) {
    Tick remaining = end_tick - current_tick;

    // Select duration: pick from allowed durations that fit.
    // Weight: prefer quarter (50%), then half (30%), then eighth (20%).
    Tick dur = 0;
    float dur_roll = rng::rollFloat(rng, 0.0f, 1.0f);
    int dur_preference;
    if (dur_roll < 0.30f) {
      dur_preference = 0;  // half
    } else if (dur_roll < 0.80f) {
      dur_preference = 1;  // quarter
    } else {
      dur_preference = 2;  // eighth
    }

    // Try preferred duration first, then fall back to shorter ones.
    for (int idx = dur_preference; idx < kNumMidDurs; ++idx) {
      if (kMidDurs[idx] <= remaining) {
        dur = kMidDurs[idx];
        break;
      }
    }
    // If nothing fits (remaining < eighth note), stop generating.
    if (dur == 0) break;

    // Look up harmonic context at this tick.
    const auto& event = timeline.getAt(current_tick);
    scale_type = event.is_minor ? ScaleType::HarmonicMinor : ScaleType::Major;

    bool strong = isStrongBeat(current_tick);
    bool in_cadence = (current_tick >= cadence_start);

    uint8_t pitch;

    if (strong || in_cadence) {
      // Strong beat or cadence: target nearest chord tone to prev_pitch.
      pitch = nearestChordTone(prev_pitch, event);
      pitch = clampPitch(static_cast<int>(pitch),
                         static_cast<uint8_t>(reg_lo),
                         static_cast<uint8_t>(reg_hi));
    } else {
      // Weak beat: diatonic step in the current direction (~80%) or
      // neighbor tone (~20%).
      bool use_neighbor = rng::rollProbability(rng, 0.20f);

      // Compute the next scale degree in the current direction.
      int abs_deg =
          scale_util::pitchToAbsoluteDegree(prev_pitch, event.key, scale_type);
      int target_deg = abs_deg + direction;
      uint8_t step_pitch =
          scale_util::absoluteDegreeToPitch(target_deg, event.key, scale_type);
      step_pitch = clampPitch(static_cast<int>(step_pitch),
                              static_cast<uint8_t>(reg_lo),
                              static_cast<uint8_t>(reg_hi));

      if (use_neighbor) {
        // Neighbor tone: step away in the opposite direction.
        int neighbor_deg = abs_deg - direction;
        uint8_t neighbor_pitch =
            scale_util::absoluteDegreeToPitch(neighbor_deg, event.key, scale_type);
        neighbor_pitch = clampPitch(static_cast<int>(neighbor_pitch),
                                    static_cast<uint8_t>(reg_lo),
                                    static_cast<uint8_t>(reg_hi));
        pitch = neighbor_pitch;
      } else {
        pitch = step_pitch;
      }
    }

    NoteEvent note;
    note.start_tick = current_tick;
    note.duration = dur;
    note.pitch = pitch;
    note.velocity = kOrganVelocity;
    note.voice = voice_idx;
    note.source = BachNoteSource::FreeCounterpoint;
    notes.push_back(note);

    // Update direction tracking.
    ++notes_in_direction;
    if (notes_in_direction >= direction_limit) {
      direction = -direction;
      notes_in_direction = 0;
      direction_limit = rng::rollRange(rng, 3, 4);
    }

    // If we hit the register boundary, reverse direction to stay in range.
    if (static_cast<int>(pitch) <= reg_lo + 2 && direction < 0) {
      direction = 1;
      notes_in_direction = 0;
    } else if (static_cast<int>(pitch) >= reg_hi - 2 && direction > 0) {
      direction = -1;
      notes_in_direction = 0;
    }

    prev_pitch = pitch;
    current_tick += dur;
  }

  return notes;
}

// ---------------------------------------------------------------------------
// Full-timeline bass voice generation
// ---------------------------------------------------------------------------

/// @brief Fit a bass pitch into the voice register, minimizing distance from prev_pitch.
///
/// Octave-adjusts the target pitch to be as close as possible to prev_pitch
/// while remaining within [low, high].
///
/// @param target_pitch Target pitch to adjust (e.g., chord root from event).
/// @param prev_pitch Previous bass pitch for proximity.
/// @param low Lowest allowed MIDI pitch.
/// @param high Highest allowed MIDI pitch.
/// @return Octave-adjusted MIDI pitch.
uint8_t fitBassRegister(int target_pitch, uint8_t prev_pitch,
                        uint8_t low, uint8_t high) {
  int target_pc = ((target_pitch % 12) + 12) % 12;
  int best = -1;
  int best_dist = 999;
  // Try all octave placements of this pitch class within the range.
  for (int octave = 0; octave <= 10; ++octave) {
    int candidate = octave * 12 + target_pc;
    if (candidate < static_cast<int>(low) || candidate > static_cast<int>(high)) continue;
    int dist = std::abs(candidate - static_cast<int>(prev_pitch));
    if (dist < best_dist) {
      best_dist = dist;
      best = candidate;
    }
  }
  if (best < 0) {
    return clampPitch(target_pitch, low, high);
  }
  return static_cast<uint8_t>(best);
}

/// @brief Generate a melodic bass voice across the full harmonic timeline.
///
/// Produces half-note primary durations with quarter-note scale passages
/// connecting chord roots. Strong beats use the harmonic event's bass pitch,
/// weak beats use diatonic steps approaching the next chord root. Enforces
/// same-pitch limits and cadential tonic hold.
///
/// @param timeline Full harmonic timeline for chord context lookups.
/// @param start_tick Start tick of the generation span.
/// @param end_tick End tick of the generation span.
/// @param voice_idx Voice/track index for the bass voice.
/// @param num_voices Total number of voices (kept for API symmetry).
/// @param rng Random number generator.
/// @return Vector of NoteEvent entries.
std::vector<NoteEvent> generateBassVoice(const HarmonicTimeline& timeline,
                                         Tick start_tick, Tick end_tick,
                                         uint8_t voice_idx,
                                         uint8_t num_voices,
                                         std::mt19937& rng) {
  (void)num_voices;  // Kept for API symmetry with generateMiddleVoice.
  std::vector<NoteEvent> notes;
  if (start_tick >= end_tick) return notes;

  uint8_t low_pitch = getVoiceLowPitch(voice_idx);
  uint8_t high_pitch = getVoiceHighPitch(voice_idx);

  // Restrict bass to at most ~1 octave above the low boundary for tight register.
  uint8_t bass_high = static_cast<uint8_t>(
      std::min(static_cast<int>(high_pitch), static_cast<int>(low_pitch) + 14));

  // Allowed durations for the bass voice (quantized).
  static constexpr Tick kBassDurs[] = {kHalfNote, kQuarterNote};

  // Initialize from the first event's bass pitch.
  const auto& first_event = timeline.getAt(start_tick);
  ScaleType scale_type =
      first_event.is_minor ? ScaleType::HarmonicMinor : ScaleType::Major;
  uint8_t prev_pitch = fitBassRegister(
      static_cast<int>(first_event.bass_pitch), first_event.bass_pitch,
      low_pitch, bass_high);

  int same_pitch_count = 0;

  // Cadence region: last 2 beats hold the tonic root.
  Tick cadence_start = (end_tick > kHalfNote) ? end_tick - kHalfNote : start_tick;

  Tick current_tick = start_tick;

  while (current_tick < end_tick) {
    Tick remaining = end_tick - current_tick;

    // Select duration: prefer half notes (60%) over quarter notes (40%).
    Tick dur = 0;
    bool prefer_half = rng::rollProbability(rng, 0.60f);
    if (prefer_half && kBassDurs[0] <= remaining) {
      dur = kBassDurs[0];  // half note
    } else if (kBassDurs[1] <= remaining) {
      dur = kBassDurs[1];  // quarter note
    } else {
      break;  // Remaining time too short for any quantized duration.
    }

    const auto& event = timeline.getAt(current_tick);
    scale_type = event.is_minor ? ScaleType::HarmonicMinor : ScaleType::Major;

    bool strong = isStrongBeat(current_tick);
    bool in_cadence = (current_tick >= cadence_start);

    uint8_t pitch;

    if (in_cadence) {
      // Cadence: hold the tonic root.
      pitch = fitBassRegister(
          static_cast<int>(first_event.bass_pitch), prev_pitch,
          low_pitch, bass_high);
    } else if (strong) {
      // Strong beat: use the event's bass_pitch (respects chord inversions).
      pitch = fitBassRegister(
          static_cast<int>(event.bass_pitch), prev_pitch,
          low_pitch, bass_high);
    } else {
      // Weak beat: diatonic step toward the next strong-beat root.
      Tick next_strong = current_tick + dur;
      // Round up to next strong beat (beat 1 or 3).
      Tick next_bar_pos = next_strong % kTicksPerBar;
      if (next_bar_pos != 0 && next_bar_pos != kTicksPerBar / 2) {
        if (next_bar_pos < kTicksPerBar / 2) {
          next_strong = next_strong - next_bar_pos + kTicksPerBar / 2;
        } else {
          next_strong = next_strong - next_bar_pos + kTicksPerBar;
        }
      }
      if (next_strong >= end_tick) next_strong = end_tick - 1;
      const auto& next_event = timeline.getAt(next_strong);
      uint8_t target = fitBassRegister(
          static_cast<int>(next_event.bass_pitch), prev_pitch,
          low_pitch, bass_high);

      // Step toward target via scale degree.
      int prev_deg =
          scale_util::pitchToAbsoluteDegree(prev_pitch, event.key, scale_type);
      int tgt_deg =
          scale_util::pitchToAbsoluteDegree(target, event.key, scale_type);
      int step_dir = (tgt_deg > prev_deg) ? 1 : (tgt_deg < prev_deg ? -1 : 0);
      if (step_dir == 0) {
        // Already at target; use a neighbor step for motion.
        step_dir = rng::rollProbability(rng, 0.5f) ? 1 : -1;
      }
      int step_deg = prev_deg + step_dir;
      pitch = scale_util::absoluteDegreeToPitch(step_deg, event.key, scale_type);
      pitch = clampPitch(static_cast<int>(pitch), low_pitch, bass_high);
    }

    // Prevent same pitch more than 2 consecutive times.
    if (pitch == prev_pitch) {
      ++same_pitch_count;
      if (same_pitch_count >= 2) {
        int abs_deg =
            scale_util::pitchToAbsoluteDegree(prev_pitch, event.key, scale_type);
        int alt_dir = rng::rollProbability(rng, 0.5f) ? 1 : -1;
        uint8_t alt = scale_util::absoluteDegreeToPitch(
            abs_deg + alt_dir, event.key, scale_type);
        alt = clampPitch(static_cast<int>(alt), low_pitch, bass_high);
        if (alt != prev_pitch) {
          pitch = alt;
        }
        same_pitch_count = 0;
      }
    } else {
      same_pitch_count = 0;
    }

    NoteEvent note;
    note.start_tick = current_tick;
    note.duration = dur;
    note.pitch = pitch;
    note.velocity = kOrganVelocity;
    note.voice = voice_idx;
    note.source = BachNoteSource::FreeCounterpoint;
    notes.push_back(note);

    prev_pitch = pitch;
    current_tick += dur;
  }

  return notes;
}

// ---------------------------------------------------------------------------
// FreeForm generation
// ---------------------------------------------------------------------------

/// @brief Generate all notes for a FreeForm prelude.
///
/// Voice 0 (top): Passage work -- alternating between scale and arpeggio
/// patterns using 8th notes (per-event, unchanged).
/// Voice 1 (middle): Full-timeline Baroque middle voice with mixed durations.
/// Voice 2 (bottom/pedal): Full-timeline stepwise bass voice.
/// Additional middle voices use full-timeline generation.
///
/// @param timeline Harmonic timeline providing chord context.
/// @param num_voices Number of organ voices.
/// @param rng Random number generator.
/// @return Vector of all generated NoteEvents.
std::vector<NoteEvent> generateFreeFormNotes(const HarmonicTimeline& timeline,
                                             uint8_t num_voices,
                                             std::mt19937& rng) {
  std::vector<NoteEvent> all_notes;
  const auto& events = timeline.events();
  Tick total = timeline.totalDuration();

  // Track voice0 hint for inter-event continuity.
  int voice0_hint = -1;

  // Voice 0: per-event passage work with 8th notes (scale and arpeggio patterns).
  for (size_t event_idx = 0; event_idx < events.size(); ++event_idx) {
    const auto& event = events[event_idx];
    int pattern_index = static_cast<int>(event_idx);

    if (num_voices >= 1) {
      std::vector<NoteEvent> voice0_notes;
      if (rng::rollProbability(rng, 0.5f)) {
        voice0_notes =
            generateScalePassage(event, 0, kEighthNote, pattern_index, rng, voice0_hint);
      } else {
        voice0_notes = generateArpeggioPassage(event, 0, kEighthNote, rng, voice0_hint);
      }
      if (!voice0_notes.empty()) {
        voice0_hint = static_cast<int>(voice0_notes.back().pitch);
      }
      all_notes.insert(all_notes.end(), voice0_notes.begin(), voice0_notes.end());
    }
  }

  // Voice 1: full-timeline Baroque middle voice.
  if (num_voices >= 2) {
    auto voice1 = generateMiddleVoice(timeline, 0, total, 1, num_voices, rng);
    all_notes.insert(all_notes.end(), voice1.begin(), voice1.end());
  }

  // Bass voice (last voice): full-timeline stepwise bass.
  if (num_voices >= 3) {
    uint8_t bass_voice = static_cast<uint8_t>(num_voices - 1);
    auto bass = generateBassVoice(timeline, 0, total, bass_voice, num_voices, rng);
    all_notes.insert(all_notes.end(), bass.begin(), bass.end());
  }

  // Additional middle voices (4+ voices): full-timeline middle voice.
  for (uint8_t vid = 2; vid < num_voices - 1 && vid < 4; ++vid) {
    auto extra = generateMiddleVoice(timeline, 0, total, vid, num_voices, rng);
    all_notes.insert(all_notes.end(), extra.begin(), extra.end());
  }

  return all_notes;
}

// ---------------------------------------------------------------------------
// Perpetual motion generation
// ---------------------------------------------------------------------------

/// @brief Generate all notes for a Perpetual motion prelude.
///
/// Voice 0 (top): Continuous 16th notes arpeggiating through chord tones
/// and passing tones (per-event, unchanged).
/// Voice 1 (middle): Full-timeline Baroque middle voice.
/// Voice 2 (bottom/pedal): Full-timeline stepwise bass voice.
///
/// @param timeline Harmonic timeline providing chord context.
/// @param num_voices Number of organ voices.
/// @param rng Random number generator.
/// @return Vector of all generated NoteEvents.
std::vector<NoteEvent> generatePerpetualNotes(const HarmonicTimeline& timeline,
                                              uint8_t num_voices,
                                              std::mt19937& rng) {
  std::vector<NoteEvent> all_notes;
  const auto& events = timeline.events();
  Tick total = timeline.totalDuration();

  // Track voice0 hint for inter-event continuity.
  int voice0_hint = -1;

  // Voice 0: per-event continuous 16th notes (arpeggio pattern).
  for (size_t event_idx = 0; event_idx < events.size(); ++event_idx) {
    const auto& event = events[event_idx];

    if (num_voices >= 1) {
      auto voice0_notes =
          generateArpeggioPassage(event, 0, kSixteenthNote, rng, voice0_hint);
      if (!voice0_notes.empty()) {
        voice0_hint = static_cast<int>(voice0_notes.back().pitch);
      }
      all_notes.insert(all_notes.end(), voice0_notes.begin(), voice0_notes.end());
    }
  }

  // Voice 1: full-timeline Baroque middle voice.
  if (num_voices >= 2) {
    auto voice1 = generateMiddleVoice(timeline, 0, total, 1, num_voices, rng);
    all_notes.insert(all_notes.end(), voice1.begin(), voice1.end());
  }

  // Bass voice (last voice): full-timeline stepwise bass.
  if (num_voices >= 3) {
    uint8_t bass_voice = static_cast<uint8_t>(num_voices - 1);
    auto bass = generateBassVoice(timeline, 0, total, bass_voice, num_voices, rng);
    all_notes.insert(all_notes.end(), bass.begin(), bass.end());
  }

  // Additional middle voices (4+ voices): full-timeline middle voice.
  for (uint8_t vid = 2; vid < num_voices - 1 && vid < 4; ++vid) {
    auto extra = generateMiddleVoice(timeline, 0, total, vid, num_voices, rng);
    all_notes.insert(all_notes.end(), extra.begin(), extra.end());
  }

  return all_notes;
}

/// @brief Sort notes in each track by start_tick for MIDI output.
/// @param tracks Tracks whose notes will be sorted in place.
void sortPreludeTrackNotes(std::vector<Track>& tracks) {
  for (auto& track : tracks) {
    std::sort(track.notes.begin(), track.notes.end(),
              [](const NoteEvent& lhs, const NoteEvent& rhs) {
                if (lhs.start_tick != rhs.start_tick) {
                  return lhs.start_tick < rhs.start_tick;
                }
                return lhs.pitch < rhs.pitch;
              });
  }
}

/// @brief Clamp voice count to valid range [2, 5].
/// @param num_voices Raw voice count from configuration.
/// @return Clamped voice count.
uint8_t clampPreludeVoiceCount(uint8_t num_voices) {
  if (num_voices < 2) return 2;
  if (num_voices > 5) return 5;
  return num_voices;
}

}  // namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

Tick calculatePreludeLength(Tick fugue_length_ticks) {
  if (fugue_length_ticks > 0) {
    return static_cast<Tick>(static_cast<float>(fugue_length_ticks) * kPreludeFugueRatio);
  }
  return kDefaultPreludeBars * kTicksPerBar;
}

PreludeResult generatePrelude(const PreludeConfig& config) {
  PreludeResult result;
  result.success = false;

  uint8_t num_voices = clampPreludeVoiceCount(config.num_voices);
  std::mt19937 rng(config.seed);

  // Step 1: Calculate target duration.
  Tick target_duration = calculatePreludeLength(config.fugue_length_ticks);

  // Quantize to bar boundaries (round up to the nearest full bar).
  if (target_duration % kTicksPerBar != 0) {
    target_duration = ((target_duration / kTicksPerBar) + 1) * kTicksPerBar;
  }

  // Step 2: Create harmonic timeline with Baroque-favored progression.
  ProgressionType prog_type;
  float prog_roll = rng::rollFloat(rng, 0.0f, 1.0f);
  if (prog_roll < 0.55f)
    prog_type = ProgressionType::DescendingFifths;
  else if (prog_roll < 0.85f)
    prog_type = ProgressionType::CircleOfFifths;
  else
    prog_type = ProgressionType::ChromaticCircle;

  HarmonicTimeline timeline = HarmonicTimeline::createProgression(
      config.key, target_duration, HarmonicResolution::Beat, prog_type);
  timeline.applyCadence(CadenceType::Perfect, config.key);

  // Step 3: Generate notes based on PreludeType.
  std::vector<NoteEvent> all_notes;
  if (config.type == PreludeType::Perpetual) {
    all_notes = generatePerpetualNotes(timeline, num_voices, rng);
  } else {
    all_notes = generateFreeFormNotes(timeline, num_voices, rng);
  }

  // Tag untagged notes with source for counterpoint protection levels.
  for (auto& n : all_notes) {
    if (n.source == BachNoteSource::Unknown) {
      n.source = (num_voices >= 4 && isPedalVoice(n.voice, num_voices))
                     ? BachNoteSource::PedalPoint
                     : BachNoteSource::FreeCounterpoint;
    }
  }

  // Build per-voice pitch ranges for counterpoint validation.
  std::vector<std::pair<uint8_t, uint8_t>> voice_ranges;
  for (uint8_t v = 0; v < num_voices; ++v) {
    voice_ranges.push_back({getVoiceLowPitch(v), getVoiceHighPitch(v)});
  }

  // Post-validate through counterpoint engine (parallel 5ths/8ths repair).
  PostValidateStats pv_stats;
  all_notes = postValidateNotes(
      std::move(all_notes), num_voices, config.key, voice_ranges, &pv_stats);

  // Step 4: Create tracks and assign notes by voice_id.
  std::vector<Track> tracks = createPreludeTracks(num_voices);
  for (const auto& note : all_notes) {
    if (note.voice < tracks.size()) {
      tracks[note.voice].notes.push_back(note);
    }
  }

  // ---------------------------------------------------------------------------
  // Shared organ techniques: pedal point, Picardy, registration
  // ---------------------------------------------------------------------------

  // Cadential pedal point on tonic (last 2 bars, only if pedal voice exists).
  if (num_voices >= 4 && target_duration > kTicksPerBar * 2) {
    Tick pedal_start = target_duration - kTicksPerBar * 2;
    auto pedal_notes = generateCadentialPedal(
        config.key, pedal_start, target_duration,
        PedalPointType::Tonic, num_voices - 1);
    for (auto& n : pedal_notes) {
      if (n.voice < tracks.size()) {
        tracks[n.voice].notes.push_back(n);
      }
    }
  }

  // Picardy third (minor keys only).
  if (config.enable_picardy && config.key.is_minor) {
    for (auto& track : tracks) {
      applyPicardyToFinalChord(track.notes, config.key,
                               target_duration - kTicksPerBar);
    }
  }

  // Simple 3-point registration plan.
  auto reg_plan = createSimpleRegistrationPlan(0, target_duration);
  applyExtendedRegistrationPlan(tracks, reg_plan);

  // Step 5: Sort notes within each track.
  sortPreludeTrackNotes(tracks);

  result.tracks = std::move(tracks);
  result.timeline = std::move(timeline);
  result.total_duration_ticks = target_duration;
  result.success = true;

  return result;
}

}  // namespace bach
