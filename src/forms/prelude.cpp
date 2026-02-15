// Implementation of the organ prelude generator.

#include "forms/prelude.h"

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <random>
#include <vector>

#include "core/gm_program.h"
#include "core/melodic_state.h"
#include "core/note_creator.h"
#include "core/note_source.h"
#include "core/pitch_utils.h"
#include "core/rng_util.h"
#include "core/scale.h"
#include "counterpoint/coordinate_voices.h"
#include "counterpoint/leap_resolution.h"
#include "counterpoint/parallel_repair.h"
#include "counterpoint/repeated_note_repair.h"
#include "counterpoint/vertical_context.h"
#include "counterpoint/vertical_safe.h"
#include "harmony/chord_tone_utils.h"
#include "harmony/chord_types.h"
#include "harmony/harmonic_event.h"
#include "forms/form_utils.h"
#include "fugue/fugue_config.h"
#include "organ/organ_techniques.h"

namespace bach {

namespace {

using namespace duration;

/// Prelude energy curve: 8th note center with quarter/dotted-quarter mix.
/// Energy 0.55-0.65 -> minDuration = eighth note floor, suppresses 16th notes.
static float preludeEnergy(Tick tick, Tick total_duration) {
  if (total_duration == 0) return 0.55f;
  float pos = static_cast<float>(tick) / static_cast<float>(total_duration);
  pos = std::clamp(pos, 0.0f, 1.0f);
  if (pos < 0.20f) return 0.55f;
  if (pos < 0.80f) return 0.55f + ((pos - 0.20f) / 0.60f) * 0.10f;
  return 0.60f;
}

/// @brief Default prelude length in bars when fugue length is unknown.
constexpr Tick kDefaultPreludeBars = 12;

/// @brief Prelude-to-fugue length ratio (midpoint of 60-80% range).
constexpr float kPreludeFugueRatio = 0.70f;

// ---------------------------------------------------------------------------
// Pitch range helpers
// ---------------------------------------------------------------------------

/// @brief Get the organ manual low pitch for a voice index.
/// @param voice_idx Voice index (0-based).
/// @return Low MIDI pitch bound for the manual.
uint8_t getVoiceLowPitch(uint8_t voice_idx) {
  switch (voice_idx) {
    case 0: return 60;                         // C4 (Great)
    case 1: return 52;                         // E3 (Swell)
    case 2: return organ_range::kManual3Low;   // C3 (Positiv standard)
    case 3: return organ_range::kPedalLow;     // 24 (Pedal unchanged)
    default: return 52;
  }
}

/// @brief Get the organ manual high pitch for a voice index.
/// @param voice_idx Voice index (0-based).
/// @return High MIDI pitch bound for the manual.
uint8_t getVoiceHighPitch(uint8_t voice_idx) {
  switch (voice_idx) {
    case 0: return 88;                         // E6 (Great)
    case 1: return 76;                         // E5 (Swell)
    case 2: return 64;                         // E4 (Positiv)
    case 3: return organ_range::kPedalHigh;    // 50 (Pedal unchanged)
    default: return 76;
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
    if (voice_idx == 0) return {67, 82};  // G4-Bb5
    return {52, 68};                       // E3-Ab4
  }
  // 3+ voices: clear register separation with 7-semitone gap between voices 0 and 1.
  switch (voice_idx) {
    case 0: return {74, 84};   // D5-C6 (Great: 60-88)
    case 1: return {55, 67};   // G3-G4 (Swell: 52-76)
    default: return {48, 58};  // C3-Bb3 (Positiv: 43-64)
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
/// @param energy Energy level [0,1] for duration selection.
/// @param pattern_index Selects ascending (even) or descending (odd).
/// @param rng Random number generator for starting pitch selection.
/// @return Vector of NoteEvent entries.
std::vector<NoteEvent> generateScalePassage(const HarmonicEvent& event,
                                            uint8_t voice_idx,
                                            float energy,
                                            int pattern_index,
                                            std::mt19937& rng,
                                            int hint_center = -1,
                                            const VerticalContext* vctx = nullptr) {
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

  // Start from the scale tone nearest to hint_center for continuity.
  // Only fall back to random region when no hint is available.
  size_t start_idx;
  if (hint_center >= 0) {
    // Find the scale tone closest to the hint pitch.
    int best_dist = 999;
    size_t best_idx = 0;
    for (size_t idx = 0; idx < scale_tones.size(); ++idx) {
      int dist = std::abs(static_cast<int>(scale_tones[idx]) - hint_center);
      if (dist < best_dist) {
        best_dist = dist;
        best_idx = idx;
      }
    }
    start_idx = best_idx;
  } else if (ascending) {
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
    Tick dur = FugueEnergyCurve::selectDuration(energy, current_tick, rng, 0);
    if (dur > remaining) dur = remaining;
    if (dur == 0) break;

    uint8_t pitch = scale_tones[tone_idx];

    // Category B: nudge pitch if vertically unsafe against placed voices.
    if (vctx && !vctx->isSafe(current_tick, voice_idx, pitch)) {
      ScaleType sc = event.is_minor ? ScaleType::HarmonicMinor : ScaleType::Major;
      for (int dlt : {1, -1, 2, -2}) {
        uint8_t alt = clampPitch(static_cast<int>(pitch) + dlt, low_pitch, high_pitch);
        if (scale_util::isScaleTone(alt, event.key, sc) &&
            vctx->isSafe(current_tick, voice_idx, alt)) {
          pitch = alt;
          break;
        }
      }
    }

    NoteEvent note;
    note.start_tick = current_tick;
    note.duration = dur;
    note.pitch = pitch;
    note.velocity = kOrganVelocity;
    note.voice = voice_idx;
    note.source = BachNoteSource::EpisodeMaterial;
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
/// @param energy Energy level [0,1] for duration selection.
/// @param rng Random number generator.
/// @return Vector of NoteEvent entries.
std::vector<NoteEvent> generateArpeggioPassage(const HarmonicEvent& event,
                                               uint8_t voice_idx,
                                               float energy,
                                               std::mt19937& rng,
                                               int hint_center = -1,
                                               const VerticalContext* vctx = nullptr) {
  std::vector<NoteEvent> notes;
  uint8_t low_pitch = getVoiceLowPitch(voice_idx);
  uint8_t high_pitch = getVoiceHighPitch(voice_idx);

  // Build arpeggio pitches spanning the voice range.
  int root_pc = getPitchClass(event.chord.root_pitch);
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
  constexpr int kMaxLeap = 5;  // Perfect 4th max; was 8 (minor 6th).

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

  // Start from the arpeggio pitch closest to hint for continuity.
  size_t arp_idx;
  if (hint_center >= 0) {
    int best_dist = 999;
    size_t best_idx = 0;
    for (size_t idx = 0; idx < arp_pitches.size(); ++idx) {
      int dist = std::abs(static_cast<int>(arp_pitches[idx]) - hint_center);
      if (dist < best_dist) {
        best_dist = dist;
        best_idx = idx;
      }
    }
    arp_idx = best_idx;
  } else {
    arp_idx = arp_pitches.size() / 2;
  }
  bool going_up = rng::rollProbability(rng, 0.6f);

  uint8_t prev_pitch = arp_pitches[arp_idx];

  // Leap resolution state: when a leap > 4 semitones occurs, force stepwise
  // resolution in the opposite direction on the next note.
  bool leap_pending = false;
  int leap_direction = 0;  // +1 if the leap went up, -1 if down.

  // Determine scale type for leap resolution lookups.
  ScaleType arp_scale = event.is_minor ? ScaleType::HarmonicMinor : ScaleType::Major;

  while (current_tick < event.tick + event_duration) {
    bool was_resolution = false;
    Tick remaining = (event.tick + event_duration) - current_tick;
    Tick dur = FugueEnergyCurve::selectDuration(energy, current_tick, rng, 0);
    if (dur > remaining) dur = remaining;
    if (dur == 0) break;

    uint8_t candidate;

    if (leap_pending) {
      // Resolve the leap: step in the opposite direction to the nearest scale tone.
      int resolve_dir = -leap_direction;
      int abs_deg = scale_util::pitchToAbsoluteDegree(prev_pitch, event.key, arp_scale);
      uint8_t resolve_pitch =
          scale_util::absoluteDegreeToPitch(abs_deg + resolve_dir, event.key, arp_scale);
      resolve_pitch = clampPitch(static_cast<int>(resolve_pitch),
                                 static_cast<int>(arp_pitches.front()),
                                 static_cast<int>(arp_pitches.back()));
      candidate = resolve_pitch;
      leap_pending = false;
      was_resolution = true;

      // Update arp_idx to the nearest arpeggio pitch for subsequent navigation.
      int best_dist = 999;
      for (size_t idx = 0; idx < arp_pitches.size(); ++idx) {
        int dist = std::abs(static_cast<int>(arp_pitches[idx])
                            - static_cast<int>(candidate));
        if (dist < best_dist) {
          best_dist = dist;
          arp_idx = idx;
        }
      }
    } else {
      candidate = arp_pitches[arp_idx];
      if (absoluteInterval(candidate, prev_pitch) > kMaxLeap) {
        // Try to find a chord tone within kMaxLeap of prev_pitch.
        bool found = false;
        int best_dist = 999;
        size_t best_idx = arp_idx;
        for (size_t idx = 0; idx < arp_pitches.size(); ++idx) {
          int dist = absoluteInterval(arp_pitches[idx], prev_pitch);
          if (dist <= kMaxLeap && dist < best_dist && arp_pitches[idx] != prev_pitch) {
            best_dist = dist;
            best_idx = idx;
            found = true;
          }
        }
        if (found) {
          arp_idx = best_idx;
          candidate = arp_pitches[arp_idx];
        } else {
          // All chord tones exceed kMaxLeap. Find nearest scale tone by degree.
          int abs_deg = scale_util::pitchToAbsoluteDegree(prev_pitch, event.key, arp_scale);
          int search_dir = going_up ? 1 : -1;
          bool scale_found = false;
          for (int delta = 1; delta <= 3 && !scale_found; ++delta) {
            for (int dir : {search_dir, -search_dir}) {
              uint8_t st = scale_util::absoluteDegreeToPitch(
                  abs_deg + dir * delta, event.key, arp_scale);
              if (st >= arp_pitches.front() && st <= arp_pitches.back() &&
                  st != prev_pitch &&
                  absoluteInterval(st, prev_pitch) <= kMaxLeap) {
                candidate = st;
                scale_found = true;
                break;
              }
            }
          }
          if (!scale_found) {
            candidate = prev_pitch;  // Last resort.
          }
        }
      }
    }

    // Category B: nudge candidate if vertically unsafe against placed voices.
    if (vctx && !vctx->isSafe(current_tick, voice_idx, candidate)) {
      ScaleType sc_arp = event.is_minor ? ScaleType::HarmonicMinor : ScaleType::Major;
      for (int dlt : {1, -1, 2, -2}) {
        uint8_t alt = clampPitch(static_cast<int>(candidate) + dlt,
                                 static_cast<int>(arp_pitches.front()),
                                 static_cast<int>(arp_pitches.back()));
        if (scale_util::isScaleTone(alt, event.key, sc_arp) &&
            vctx->isSafe(current_tick, voice_idx, alt)) {
          candidate = alt;
          break;
        }
      }
    }

    NoteEvent note;
    note.start_tick = current_tick;
    note.duration = dur;
    note.pitch = candidate;
    note.velocity = kOrganVelocity;
    note.voice = voice_idx;
    note.source = BachNoteSource::ArpeggioFlow;
    notes.push_back(note);

    // Check if this note created a leap that needs resolution.
    if (absoluteInterval(candidate, prev_pitch) > 4) {
      leap_pending = true;
      leap_direction = (candidate > prev_pitch) ? 1 : -1;
    }

    prev_pitch = candidate;
    current_tick += dur;

    // Skip arp_idx advance after leap resolution to let the resolution breathe.
    if (was_resolution) continue;

    // Advance arp_idx for next iteration.
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
                                           std::mt19937& rng,
                                           const VerticalContext* vctx = nullptr) {
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

  // Directional persistence via MelodicState model.
  int direction = 1;  // +1 ascending, -1 descending
  MelodicState mel_state;

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
      // Weak beat: multi-candidate scoring with direction as prior.
      int abs_deg =
          scale_util::pitchToAbsoluteDegree(prev_pitch, event.key, scale_type);

      // Generate candidates: steps in both directions + neighbor.
      struct Candidate {
        uint8_t p;
        float score;
      };
      Candidate candidates[4];
      int n_cand = 0;

      for (int delta : {direction, -direction, direction * 2, -direction * 2}) {
        int deg = abs_deg + delta;
        uint8_t cp = scale_util::absoluteDegreeToPitch(deg, event.key, scale_type);
        cp = clampPitch(static_cast<int>(cp),
                        static_cast<uint8_t>(reg_lo),
                        static_cast<uint8_t>(reg_hi));
        if (n_cand < 4) {
          float s = 0.0f;
          // (1) Direction prior: bonus for matching current direction.
          int motion = static_cast<int>(cp) - static_cast<int>(prev_pitch);
          if ((direction > 0 && motion > 0) || (direction < 0 && motion < 0)) {
            s += 0.30f;
          }
          // Chord tone bonus.
          if (isChordTone(cp, event)) s += 0.25f;
          // Stepwise bonus (prefer small intervals).
          int semitones = std::abs(motion);
          if (semitones <= 2) s += 0.20f;
          else if (semitones <= 4) s += 0.10f;
          // (2) Register boundary penalty.
          if (static_cast<int>(cp) <= reg_lo + 2 ||
              static_cast<int>(cp) >= reg_hi - 2) {
            s -= 0.15f;
          }
          // (4) Same-pitch penalty (repeated_pitch_rate protection).
          if (cp == prev_pitch) s -= 0.05f;
          // (5) Vertical context scoring: reward vertically consonant candidates.
          if (vctx) s += 0.25f * vctx->score(current_tick, voice_idx, cp);
          candidates[n_cand++] = {cp, s};
        }
      }

      if (n_cand > 0) {
        // Sort candidates by score descending.
        for (int a = 0; a < n_cand - 1; ++a) {
          for (int b = a + 1; b < n_cand; ++b) {
            if (candidates[b].score > candidates[a].score) {
              std::swap(candidates[a], candidates[b]);
            }
          }
        }

        // (3) Inertia: if score difference < 0.08, prefer current direction.
        if (n_cand >= 2 &&
            candidates[0].score - candidates[1].score < 0.08f) {
          // Find the candidate matching current direction.
          for (int ci = 0; ci < n_cand; ++ci) {
            int m = static_cast<int>(candidates[ci].p) -
                    static_cast<int>(prev_pitch);
            if ((direction > 0 && m > 0) || (direction < 0 && m < 0)) {
              pitch = candidates[ci].p;
              goto scored_done;
            }
          }
        }
        pitch = candidates[0].p;
      scored_done:;
      } else {
        // Fallback: step in current direction.
        pitch = scale_util::absoluteDegreeToPitch(abs_deg + direction,
                                                  event.key, scale_type);
        pitch = clampPitch(static_cast<int>(pitch),
                           static_cast<uint8_t>(reg_lo),
                           static_cast<uint8_t>(reg_hi));
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

    // Update melodic state and choose next direction.
    updateMelodicState(mel_state, prev_pitch, pitch);
    direction = chooseMelodicDirection(mel_state, rng);

    // If we hit the register boundary, override direction to stay in range.
    if (static_cast<int>(pitch) <= reg_lo + 2 && direction < 0) {
      direction = 1;
    } else if (static_cast<int>(pitch) >= reg_hi - 2 && direction > 0) {
      direction = -1;
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
  int target_pc = getPitchClassSigned(target_pitch);
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
                                         std::mt19937& rng,
                                         const VerticalContext* vctx = nullptr) {
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

    // Category B: nudge pitch if vertically unsafe against placed voices.
    if (vctx && !vctx->isSafe(current_tick, voice_idx, pitch)) {
      for (int dlt : {1, -1, 2, -2}) {
        uint8_t alt = clampPitch(static_cast<int>(pitch) + dlt, low_pitch, bass_high);
        if (scale_util::isScaleTone(alt, event.key, scale_type) &&
            vctx->isSafe(current_tick, voice_idx, alt)) {
          pitch = alt;
          break;
        }
      }
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
/// Generation order: bass first (harmonic foundation), then middle voices,
/// then voice 0 (gestural scale/arpeggio passages). VerticalContext grows
/// as voices are added so subsequent generators see previously placed notes.
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

  // Bass voice first (harmonic foundation).
  if (num_voices >= 3) {
    uint8_t bass_voice = static_cast<uint8_t>(num_voices - 1);
    auto bass = generateBassVoice(timeline, 0, total, bass_voice, num_voices, rng);
    all_notes.insert(all_notes.end(), bass.begin(), bass.end());
  }

  // VerticalContext grows as voices are added; subsequent generators see
  // previously placed notes for vertical safety checks.
  VerticalContext vctx{&all_notes, &timeline, num_voices};

  // Voice 1: full-timeline Baroque middle voice.
  if (num_voices >= 2) {
    auto voice1 = generateMiddleVoice(timeline, 0, total, 1, num_voices, rng, &vctx);
    all_notes.insert(all_notes.end(), voice1.begin(), voice1.end());
  }

  // Additional middle voices (4+ voices): full-timeline middle voice.
  for (uint8_t vid = 2; vid < num_voices - 1 && vid < 4; ++vid) {
    auto extra = generateMiddleVoice(timeline, 0, total, vid, num_voices, rng, &vctx);
    all_notes.insert(all_notes.end(), extra.begin(), extra.end());
  }

  // Voice 0 (scale/arpeggio passages) last -- gestural, per-event.
  int voice0_hint = -1;
  for (size_t event_idx = 0; event_idx < events.size(); ++event_idx) {
    const auto& event = events[event_idx];
    int pattern_index = static_cast<int>(event_idx);

    if (num_voices >= 1) {
      float energy = preludeEnergy(event.tick, total);
      std::vector<NoteEvent> voice0_notes;
      if (rng::rollProbability(rng, 0.5f)) {
        voice0_notes = generateScalePassage(event, 0, energy, pattern_index, rng,
                                            voice0_hint, &vctx);
      } else {
        voice0_notes = generateArpeggioPassage(event, 0, energy, rng,
                                               voice0_hint, &vctx);
      }
      if (!voice0_notes.empty()) {
        voice0_hint = static_cast<int>(voice0_notes.back().pitch);
      }
      all_notes.insert(all_notes.end(), voice0_notes.begin(), voice0_notes.end());
    }
  }

  return all_notes;
}

// ---------------------------------------------------------------------------
// Perpetual motion generation
// ---------------------------------------------------------------------------

/// @brief Generate all notes for a Perpetual motion prelude.
///
/// Generation order: bass first (harmonic foundation), then middle voices,
/// then voice 0 (continuous arpeggio). VerticalContext grows as voices are
/// added so subsequent generators see previously placed notes.
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

  // Bass voice first (harmonic foundation).
  if (num_voices >= 3) {
    uint8_t bass_voice = static_cast<uint8_t>(num_voices - 1);
    auto bass = generateBassVoice(timeline, 0, total, bass_voice, num_voices, rng);
    all_notes.insert(all_notes.end(), bass.begin(), bass.end());
  }

  // VerticalContext grows as voices are added; subsequent generators see
  // previously placed notes for vertical safety checks.
  VerticalContext vctx{&all_notes, &timeline, num_voices};

  // Voice 1: full-timeline Baroque middle voice.
  if (num_voices >= 2) {
    auto voice1 = generateMiddleVoice(timeline, 0, total, 1, num_voices, rng, &vctx);
    all_notes.insert(all_notes.end(), voice1.begin(), voice1.end());
  }

  // Additional middle voices (4+ voices): full-timeline middle voice.
  for (uint8_t vid = 2; vid < num_voices - 1 && vid < 4; ++vid) {
    auto extra = generateMiddleVoice(timeline, 0, total, vid, num_voices, rng, &vctx);
    all_notes.insert(all_notes.end(), extra.begin(), extra.end());
  }

  // Voice 0 (continuous arpeggio) last -- gestural, per-event.
  int voice0_hint = -1;
  for (size_t event_idx = 0; event_idx < events.size(); ++event_idx) {
    const auto& event = events[event_idx];

    if (num_voices >= 1) {
      auto voice0_notes =
          generateArpeggioPassage(event, 0, 0.90f, rng, voice0_hint, &vctx);
      if (!voice0_notes.empty()) {
        voice0_hint = static_cast<int>(voice0_notes.back().pitch);
      }
      all_notes.insert(all_notes.end(), voice0_notes.begin(), voice0_notes.end());
    }
  }

  return all_notes;
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
  assert(countUnknownSource(all_notes) == 0 &&
         "All notes should have source set by generators");

  // ---- Unified coordination pass (vertical dissonance control) ----
  {
    CoordinationConfig coord_config;
    coord_config.num_voices = num_voices;
    coord_config.tonic = config.key.tonic;
    coord_config.timeline = &timeline;
    coord_config.voice_range = [](uint8_t v) -> std::pair<uint8_t, uint8_t> {
      return {getVoiceLowPitch(v), getVoiceHighPitch(v)};
    };
    coord_config.immutable_sources = {BachNoteSource::PedalPoint};
    coord_config.lightweight_sources = {BachNoteSource::ArpeggioFlow,
                                        BachNoteSource::EpisodeMaterial};
    coord_config.use_next_pitch_map = true;
    coord_config.check_cross_relations = true;
    coord_config.form_name = "Prelude";
    all_notes = coordinateVoices(std::move(all_notes), coord_config);
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

  // Leap resolution: fix unresolved melodic leaps (contrary step rule).
  {
    LeapResolutionParams lr_params;
    lr_params.num_voices = num_voices;
    lr_params.key_at_tick = [&](Tick) { return config.key.tonic; };
    lr_params.scale_at_tick = [&](Tick t) {
      const auto& ev = timeline.getAt(t);
      return ev.is_minor ? ScaleType::HarmonicMinor : ScaleType::Major;
    };
    lr_params.voice_range_static = [&](uint8_t v) -> std::pair<uint8_t, uint8_t> {
      return {getVoiceLowPitch(v), getVoiceHighPitch(v)};
    };
    lr_params.is_chord_tone = [&](Tick t, uint8_t p) {
      return isChordTone(p, timeline.getAt(t));
    };
    lr_params.vertical_safe =
        makeVerticalSafeWithParallelCheck(timeline, all_notes, num_voices);
    resolveLeaps(all_notes, lr_params);

    // Second parallel-perfect repair pass after leap resolution.
    {
      ParallelRepairParams pp_params;
      pp_params.num_voices = num_voices;
      pp_params.scale = config.key.is_minor ? ScaleType::HarmonicMinor : ScaleType::Major;
      pp_params.key_at_tick = lr_params.key_at_tick;
      pp_params.voice_range_static = lr_params.voice_range_static;
      pp_params.max_iterations = 2;
      repairParallelPerfect(all_notes, pp_params);
    }
  }

  // Repeated note repair: safety net for remaining consecutive repeated pitches.
  {
    RepeatedNoteRepairParams repair_params;
    repair_params.num_voices = num_voices;
    repair_params.key_at_tick = [&](Tick) { return config.key.tonic; };
    repair_params.scale_at_tick = [&](Tick t) {
      const auto& ev = timeline.getAt(t);
      return ev.is_minor ? ScaleType::HarmonicMinor : ScaleType::Major;
    };
    repair_params.voice_range = [&](uint8_t v) -> std::pair<uint8_t, uint8_t> {
      return {getVoiceLowPitch(v), getVoiceHighPitch(v)};
    };
    repair_params.vertical_safe =
        makeVerticalSafeWithParallelCheck(timeline, all_notes, num_voices);
    repairRepeatedNotes(all_notes, repair_params);
  }

  // Step 4: Create tracks and assign notes by voice_id.
  std::vector<Track> tracks = form_utils::createOrganTracks(num_voices);
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
  form_utils::sortTrackNotes(tracks);

  result.tracks = std::move(tracks);
  result.timeline = std::move(timeline);
  result.total_duration_ticks = target_duration;
  result.success = true;

  return result;
}

}  // namespace bach
