// Implementation of the organ passacaglia generator (BWV 582 style).

#include "forms/passacaglia.h"

#include <algorithm>
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
#include "harmony/chord_types.h"
#include "harmony/harmonic_event.h"
#include "forms/form_utils.h"
#include "core/figure_injector.h"
#include "counterpoint/leap_resolution.h"
#include "counterpoint/parallel_repair.h"
#include "counterpoint/vertical_safe.h"
#include "counterpoint/repeated_note_repair.h"
#include "organ/organ_techniques.h"

namespace bach {

namespace {

using namespace duration;

// ---------------------------------------------------------------------------
// Pitch range helpers
// ---------------------------------------------------------------------------

/// @brief Get the organ manual low pitch for a voice index.
/// @param voice_idx Voice index (0-based).
/// @return Low MIDI pitch bound for the manual.
uint8_t getVoiceLowPitch(uint8_t voice_idx) {
  // Tightened voice ranges for minimal overlap (max ~16st soprano-alto,
  // ~7st alto-tenor) while ensuring climax headroom in soprano.
  switch (voice_idx) {
    case 0: return 60;                        // C4 — soprano
    case 1: return 55;                        // G3 — alto
    case 2: return 48;                        // C3 — tenor
    case 3: return organ_range::kPedalLow;    // 24 (C1) — bass (unchanged)
    default: return 60;
  }
}

/// @brief Get the organ manual high pitch for a voice index.
/// @param voice_idx Voice index (0-based).
/// @return High MIDI pitch bound for the manual.
uint8_t getVoiceHighPitch(uint8_t voice_idx) {
  // Tightened voice ranges — matches getVoiceLowPitch().
  switch (voice_idx) {
    case 0: return 88;                        // E6 — soprano
    case 1: return 76;                        // E5 — alto
    case 2: return 69;                        // A4 — tenor
    case 3: return organ_range::kPedalHigh;   // 50 (D3) — bass (unchanged)
    default: return 88;
  }
}

// ---------------------------------------------------------------------------
// Ground bass generation (Baroque template-based)
// ---------------------------------------------------------------------------

/// @brief 6 Baroque-style ground bass templates (scale degrees, 0=tonic, 7=octave).
///
/// Each template defines 8 bars of melodic motion. The last 2 bars are
/// overwritten by enforceCadentialTail() to guarantee V-I closure.
/// Templates are drawn from common Baroque passacaglia/chaconne patterns.
static constexpr int kGroundBassTemplates[][8] = {
    {0, 0, 2, 3, 4, 5, 6, 7},  // 0: Ascending scale (BWV 582-inspired)
    {7, 6, 5, 4, 3, 2, 1, 0},  // 1: Descending octave scale (lamento)
    {0, 1, 2, 3, 4, 3, 4, 0},  // 2: Arch with neighbor-tone (Buxtehude)
    {7, 6, 5, 4, 3, 2, 4, 0},  // 3: Descending with cadential leap (Handel)
    {0, 2, 4, 2, 0, 2, 4, 7},  // 4: Triadic outline (French Baroque)
    {0, 1, 2, 3, 4, 2, 1, 0},  // 5: Half-scale ascent with return
};
static constexpr int kNumGroundBassTemplates = 6;

/// @brief Enforce cadential closure on the last 2 notes of a ground bass.
///
/// bar[n-2] receives a dominant-area degree (4, 6, or 2, weighted toward 4).
/// bar[n-1] receives the tonic (degree 0).
///
/// @param degrees Mutable vector of scale degrees.
/// @param rng Random number generator for cadential pre-dominant selection.
void enforceCadentialTail(std::vector<int>& degrees, std::mt19937& rng) {
  if (degrees.size() < 2) return;
  size_t n = degrees.size();

  // Dominant-area candidate degrees for penultimate bar.
  static constexpr int kCandidates[] = {4, 5, 6, 3, 2};
  static constexpr int kNumCandidates = 5;

  int best = 4;  // Default: dominant.
  if (n >= 3) {
    int preceding = degrees[n - 3];

    // Find candidates within 2 steps of preceding (stepwise or 3rd leap max).
    int close[kNumCandidates];
    int close_count = 0;
    for (int i = 0; i < kNumCandidates; ++i) {
      if (std::abs(kCandidates[i] - preceding) <= 2) {
        close[close_count++] = kCandidates[i];
      }
    }
    if (close_count > 0) {
      best = close[rng::rollRange(rng, 0, close_count - 1)];
    }
  }

  degrees[n - 2] = best;
  degrees[n - 1] = 0;  // Tonic.
}

/// @brief Ensure same-pitch repetition only at bar 0-1 (opening emphasis).
///
/// If consecutive degrees match at any position other than index 1,
/// nudge the later degree up by 1 to break the repetition.
///
/// @param degrees Mutable vector of scale degrees.
void sanitizeConsecutivePitches(std::vector<int>& degrees) {
  for (size_t idx = 1; idx < degrees.size(); ++idx) {
    if (degrees[idx] == degrees[idx - 1] && idx != 1) {
      // Nudge in the direction of the prevailing melodic motion.
      int nudge = 1;  // Default: ascending.
      if (idx >= 2 && degrees[idx - 1] < degrees[idx - 2]) {
        nudge = -1;  // Descending contour: nudge downward.
      }
      degrees[idx] = degrees[idx] + nudge;
      // Normalize to [0, 7] range to avoid relying on degreeToPitch wrapping.
      if (degrees[idx] < 0) degrees[idx] += 7;
      if (degrees[idx] > 7) degrees[idx] -= 7;
    }
  }
}

/// @brief Select a ground bass template index with key-dependent weighting.
///
/// Minor keys favor lamento/descending patterns; major keys favor
/// arch/triadic patterns.
///
/// @param rng Random number generator.
/// @param is_minor True for minor key.
/// @return Template index in [0, kNumGroundBassTemplates).
int selectGroundBassTemplate(std::mt19937& rng, bool is_minor) {
  // Weights for each of the 6 templates (sum = 100).
  static constexpr int kMinorWeights[] = {25, 25, 15, 15, 10, 10};
  static constexpr int kMajorWeights[] = {15, 15, 20, 20, 15, 15};

  const int* weights = is_minor ? kMinorWeights : kMajorWeights;
  int roll = rng::rollRange(rng, 1, 100);
  int cumulative = 0;
  for (int idx = 0; idx < kNumGroundBassTemplates; ++idx) {
    cumulative += weights[idx];
    if (roll <= cumulative) return idx;
  }
  return 0;
}

/// @brief Build ground bass pitches using Baroque-style templates.
///
/// Selects a pattern template weighted by key mode, applies cadential
/// tail enforcement (V-I), sanitizes consecutive-pitch violations,
/// and converts scale degrees to MIDI pitches in pedal range.
///
/// @param key Key signature (tonic + mode).
/// @param num_notes Number of notes (1 per bar).
/// @param rng Random number generator.
/// @return Vector of MIDI pitches for the ground bass, within pedal range.
std::vector<uint8_t> buildGroundBassPitches(const KeySignature& key, int num_notes,
                                            std::mt19937& rng) {
  std::vector<uint8_t> pitches;
  if (num_notes <= 0) return pitches;
  pitches.reserve(static_cast<size_t>(num_notes));

  // Single note: just the tonic.
  if (num_notes == 1) {
    int tonic = static_cast<int>(tonicPitch(key.tonic, 2));
    pitches.push_back(clampPitch(tonic, organ_range::kPedalLow,
                                 organ_range::kPedalHigh));
    return pitches;
  }

  // Scale types: NaturalMinor for body, HarmonicMinor for cadential degree 6.
  ScaleType body_scale = key.is_minor ? ScaleType::NaturalMinor : ScaleType::Major;
  ScaleType cadence_scale = key.is_minor ? ScaleType::HarmonicMinor : ScaleType::Major;

  // Octave placement: ensure degree 7 (tonic + octave) fits in pedal range.
  int tonic_pitch = static_cast<int>(tonicPitch(key.tonic, 2));
  if (tonic_pitch + 12 > static_cast<int>(organ_range::kPedalHigh)) {
    tonic_pitch = static_cast<int>(tonicPitch(key.tonic, 1));
  }
  int key_offset = getPitchClass(static_cast<uint8_t>(tonic_pitch));
  int base_note = tonic_pitch - key_offset;

  // Select template and build degree sequence.
  int tmpl_idx = selectGroundBassTemplate(rng, key.is_minor);
  std::vector<int> degrees;
  degrees.reserve(static_cast<size_t>(num_notes));

  if (num_notes <= 8) {
    // Take the first num_notes degrees from the template.
    for (int idx = 0; idx < num_notes; ++idx) {
      degrees.push_back(kGroundBassTemplates[tmpl_idx][idx]);
    }
  } else {
    // For >8 bars: simple cyclic repetition of the full template.
    // Baroque passacaglia ground bass is a strict ostinato (exact repetition).
    for (int idx = 0; idx < num_notes; ++idx) {
      degrees.push_back(kGroundBassTemplates[tmpl_idx][idx % 8]);
    }
  }

  // Enforce cadential closure (last 2 bars: V-preparation -> I).
  enforceCadentialTail(degrees, rng);

  // Sanitize consecutive same-degree outside opening.
  sanitizeConsecutivePitches(degrees);

  // Convert degrees to MIDI pitches.
  for (size_t idx = 0; idx < degrees.size(); ++idx) {
    int degree = degrees[idx];
    // Use cadence_scale for the penultimate bar's degree 6 (leading tone).
    ScaleType scale = body_scale;
    if (idx == degrees.size() - 2 && degree == 6) {
      scale = cadence_scale;
    }
    int midi_pitch = degreeToPitch(degree, base_note, key_offset, scale);
    pitches.push_back(
        clampPitch(midi_pitch, organ_range::kPedalLow, organ_range::kPedalHigh));
  }

  // Smooth leaps in the body. Leave cadential tail (last 2 notes) unsmoothed:
  // leading-tone resolution (e.g. B->C in C minor, 11 semitones) is idiomatic.
  size_t smooth_end = pitches.size() > 2 ? pitches.size() - 2 : pitches.size();
  for (size_t idx = 1; idx < smooth_end; ++idx) {
    int interval = static_cast<int>(pitches[idx]) - static_cast<int>(pitches[idx - 1]);
    // Ground bass tolerates wider leaps than upper voices (structural role).
    // Threshold: major 6th (9 semitones). Perfect 5th and minor 6th are idiomatic.
    if (std::abs(interval) > 9) {
      int adjusted = static_cast<int>(pitches[idx]);
      if (interval > 0) {
        adjusted -= 12;  // Leap up too large: bring down an octave.
      } else {
        adjusted += 12;  // Leap down too large: bring up an octave.
      }
      pitches[idx] = clampPitch(adjusted, organ_range::kPedalLow,
                                organ_range::kPedalHigh);
    }
  }

  return pitches;
}

// ---------------------------------------------------------------------------
// Resolution target for non-harmonic tones
// ---------------------------------------------------------------------------

/// @brief Compute resolution target pitch for a non-harmonic tone.
///
/// Returns 0 if the current pitch is already a chord tone (no resolution needed).
/// Otherwise returns a diatonic pitch toward which the next note should resolve,
/// based on the non-harmonic tone classification:
///   - Suspension (strong beat, stepwise approach): resolve down by step.
///   - Passing tone (stepwise approach): continue in same direction.
///   - Cambiata (skip approach, 3-5 semitones): resolve by contrary step.
///   - Default: neighbor resolution (step down).
///
/// @param current_pitch Current non-harmonic MIDI pitch.
/// @param prev_pitch Previous MIDI pitch (for approach direction).
/// @param event Current harmonic event (chord context).
/// @param key Key (tonic pitch class).
/// @param is_minor True if minor mode.
/// @param tick Current tick position for metric-level classification.
/// @return Resolution target pitch, or 0 if no resolution needed.
uint8_t findResolutionTarget(uint8_t current_pitch, uint8_t prev_pitch,
                             const HarmonicEvent& event, Key key,
                             bool is_minor, Tick tick) {
  if (isChordTone(current_pitch, event)) return 0;

  int interval = static_cast<int>(current_pitch) - static_cast<int>(prev_pitch);
  int abs_interval = std::abs(interval);
  int direction = (interval > 0) ? 1 : -1;
  bool on_strong = (metricLevel(tick) == MetricLevel::Bar);

  ScaleType scale = is_minor ? ScaleType::HarmonicMinor : ScaleType::Major;
  int degree = scale_util::pitchToAbsoluteDegree(current_pitch, key, scale);

  if (on_strong && abs_interval <= 2) {
    // Suspension-like: resolve down by step.
    return scale_util::absoluteDegreeToPitch(degree - 1, key, scale);
  }
  if (abs_interval <= 2) {
    // Passing tone: continue in same direction.
    return scale_util::absoluteDegreeToPitch(degree + direction, key, scale);
  }
  if (abs_interval >= 3 && abs_interval <= 5) {
    // Cambiata: resolve by contrary step.
    return scale_util::absoluteDegreeToPitch(degree - direction, key, scale);
  }
  // Default: step down (neighbor resolution).
  return scale_util::absoluteDegreeToPitch(degree - 1, key, scale);
}

// ---------------------------------------------------------------------------
// Scored pitch selection helper
// ---------------------------------------------------------------------------

/// @brief Select next scale tone index using multi-candidate scoring.
/// Generates candidates at offsets ±1..±3 from current index, scores each
/// using the voice dynamics model, and returns the selected index.
size_t selectNextToneIdx(const std::vector<uint8_t>& scale_tones,
                         size_t current_idx, const MelodicState& mel_state,
                         uint8_t prev_pitch, Tick tick,
                         const HarmonicEvent& event, std::mt19937& rng) {
  std::vector<uint8_t> candidates;
  std::vector<size_t> indices;
  for (int offset : {1, -1, 2, -2, 3, -3}) {
    int idx = static_cast<int>(current_idx) + offset;
    if (idx < 0 || idx >= static_cast<int>(scale_tones.size())) continue;
    uint8_t c = scale_tones[idx];
    if (c == prev_pitch) continue;
    candidates.push_back(c);
    indices.push_back(static_cast<size_t>(idx));
  }
  if (candidates.empty()) return current_idx;

  // Score each candidate with proper chord tone awareness.
  std::vector<float> weights;
  weights.reserve(candidates.size());
  float max_s = -100.0f;
  for (uint8_t c : candidates) {
    bool ct = isChordTone(c, event);
    float s = scoreCandidatePitch(mel_state, prev_pitch, c, tick, ct);
    weights.push_back(s);
    if (s > max_s) max_s = s;
  }

  // Shift to positive range for weighted selection.
  float shift = (max_s < 0.0f) ? (-max_s + 0.1f) : 0.1f;
  for (float& w : weights) w = std::max(0.01f, w + shift);

  uint8_t selected = rng::selectWeighted(rng, candidates, weights);
  // Find matching index.
  for (size_t i = 0; i < candidates.size(); ++i) {
    if (candidates[i] == selected) return indices[i];
  }
  return current_idx;
}

// ---------------------------------------------------------------------------
// Strong-beat bass consonance enforcement (generation-time)
// ---------------------------------------------------------------------------

/// @brief NCT tolerance level for variation types.
///
/// Controls how strictly strong-beat notes must be consonant with the ground
/// bass. Strict = bar + beat heads enforced. Moderate = bar heads only.
enum class NctTolerance : uint8_t {
  Strict,    ///< Bar heads + beat heads must be consonant with bass.
  Moderate,  ///< Bar heads must be consonant; beat heads prefer chord tones.
};

/// @brief Ensure a candidate pitch forms a consonant interval with the
///        sounding ground bass pitch.
///
/// When on a strong beat, checks the vertical interval between the candidate
/// and the bass. If dissonant, searches nearby scale tones (within the voice
/// range) for a consonant alternative. The search tries small adjustments
/// first (+/-1, +/-2 semitones) to preserve melodic flow.
///
/// On weak beats or when no bass is sounding, returns the candidate unchanged.
///
/// @param candidate Current candidate pitch for the upper voice.
/// @param tick Current tick position.
/// @param event Current harmonic event (provides bass_pitch).
/// @param tolerance NCT tolerance level for this variation type.
/// @param scale_tones Available scale tones in the voice range.
/// @param tone_idx Current index into scale_tones (updated if pitch changes).
/// @param low_pitch Voice range lower bound.
/// @param high_pitch Voice range upper bound.
/// @return Adjusted pitch that is consonant with bass, or original if already
///         consonant, no bass is sounding, or no adjustment found.
uint8_t enforceStrongBeatBassConsonance(uint8_t candidate, Tick tick,
                                         const HarmonicEvent& event,
                                         NctTolerance tolerance,
                                         const std::vector<uint8_t>& scale_tones,
                                         size_t& tone_idx,
                                         uint8_t low_pitch, uint8_t high_pitch) {
  // No bass pitch available — nothing to check against.
  if (event.bass_pitch == 0) return candidate;

  // Determine if this tick requires consonance enforcement.
  bool is_bar_head = (tick % kTicksPerBar == 0);
  bool is_beat_head = (tick % kTicksPerBeat == 0);

  bool must_enforce = false;
  if (tolerance == NctTolerance::Strict) {
    must_enforce = is_bar_head || is_beat_head;
  } else {
    // Moderate: enforce on bar heads; beat heads only get a soft preference
    // which is already handled by chord-tone snapping in the caller.
    must_enforce = is_bar_head;
  }

  if (!must_enforce) return candidate;

  // Check current interval with bass.
  int interval_to_bass = std::abs(static_cast<int>(candidate) -
                                  static_cast<int>(event.bass_pitch));
  if (interval_util::isConsonance(interval_to_bass)) return candidate;

  // Dissonant with bass on a strong beat — find the nearest consonant scale tone.
  // Search outward from current position in scale_tones array.
  size_t best_idx = tone_idx;
  int best_distance = 999;
  bool found = false;

  for (int offset : {1, -1, 2, -2, 3, -3, 4, -4}) {
    int idx = static_cast<int>(tone_idx) + offset;
    if (idx < 0 || idx >= static_cast<int>(scale_tones.size())) continue;
    uint8_t cand = scale_tones[idx];
    if (cand < low_pitch || cand > high_pitch) continue;

    int bass_interval = std::abs(static_cast<int>(cand) -
                                 static_cast<int>(event.bass_pitch));
    if (!interval_util::isConsonance(bass_interval)) continue;

    // Prefer minimal movement from the original candidate.
    int melodic_dist = std::abs(static_cast<int>(cand) -
                                static_cast<int>(candidate));
    if (melodic_dist < best_distance) {
      best_distance = melodic_dist;
      best_idx = static_cast<size_t>(idx);
      found = true;
    }
  }

  if (found) {
    tone_idx = best_idx;
    return scale_tones[best_idx];
  }

  // Fallback: try direct semitone search (not restricted to scale_tones array
  // positions, but still must be a scale tone in range).
  for (int delta : {1, -1, 2, -2, 3, -3, 4, -4}) {
    int adjusted = static_cast<int>(candidate) + delta;
    if (adjusted < static_cast<int>(low_pitch) ||
        adjusted > static_cast<int>(high_pitch)) {
      continue;
    }
    uint8_t adj_pitch = static_cast<uint8_t>(adjusted);

    int bass_interval = std::abs(adjusted - static_cast<int>(event.bass_pitch));
    if (!interval_util::isConsonance(bass_interval)) continue;

    // Verify it is a scale tone by checking against the scale_tones vector.
    bool in_scale = false;
    size_t closest_idx = tone_idx;
    int closest_dist = 999;
    for (size_t idx = 0; idx < scale_tones.size(); ++idx) {
      if (scale_tones[idx] == adj_pitch) {
        in_scale = true;
        closest_idx = idx;
        break;
      }
      int dist = std::abs(static_cast<int>(scale_tones[idx]) -
                          static_cast<int>(adj_pitch));
      if (dist < closest_dist) {
        closest_dist = dist;
        closest_idx = idx;
      }
    }
    if (in_scale) {
      tone_idx = closest_idx;
      return adj_pitch;
    }
  }

  return candidate;  // No consonant alternative found — keep original.
}

/// @brief Variant of bass consonance enforcement for arpeggio-based generators
///        that use chord-tone arrays instead of scale-tone arrays.
///
/// @param candidate Current candidate pitch.
/// @param tick Current tick position.
/// @param event Current harmonic event.
/// @param arp_pitches Available arpeggio pitches.
/// @param arp_idx Current index into arp_pitches (updated if pitch changes).
/// @return Adjusted pitch consonant with bass, or original.
uint8_t enforceArpeggioBassConsonance(uint8_t candidate, Tick tick,
                                       const HarmonicEvent& event,
                                       const std::vector<uint8_t>& arp_pitches,
                                       size_t& arp_idx) {
  if (event.bass_pitch == 0) return candidate;
  if (tick % kTicksPerBar != 0) return candidate;  // Arpeggios: bar heads only.

  int interval_to_bass = std::abs(static_cast<int>(candidate) -
                                  static_cast<int>(event.bass_pitch));
  if (interval_util::isConsonance(interval_to_bass)) return candidate;

  // Search arpeggio pitches for consonant alternative.
  size_t best_idx = arp_idx;
  int best_distance = 999;
  bool found = false;

  for (size_t idx = 0; idx < arp_pitches.size(); ++idx) {
    int bass_interval = std::abs(static_cast<int>(arp_pitches[idx]) -
                                 static_cast<int>(event.bass_pitch));
    if (!interval_util::isConsonance(bass_interval)) continue;

    int melodic_dist = std::abs(static_cast<int>(arp_pitches[idx]) -
                                static_cast<int>(candidate));
    if (melodic_dist < best_distance) {
      best_distance = melodic_dist;
      best_idx = idx;
      found = true;
    }
  }

  if (found) {
    arp_idx = best_idx;
    return arp_pitches[best_idx];
  }

  return candidate;
}

// ---------------------------------------------------------------------------
// Vocabulary injection helper
// ---------------------------------------------------------------------------

/// @brief Try vocabulary figure injection and return next pitch if successful.
///
/// Calls tryInjectFigure() with the current melodic state, then scores the
/// first target pitch against the existing melodic scoring system.
/// Returns 0 if injection fails, the figure is too short, or the candidate
/// scores below a minimum threshold.
///
/// @param mel_state Current melodic state for phrase progress.
/// @param prev_pitch Previous MIDI pitch.
/// @param key Current musical key.
/// @param is_minor True if minor mode.
/// @param tick Current tick position.
/// @param low_pitch Voice range lower bound.
/// @param high_pitch Voice range upper bound.
/// @param rng Random number generator.
/// @param inject_prob Base injection probability.
/// @return Target pitch from vocabulary figure, or 0 if not adopted.
uint8_t tryVocabularyNote(const MelodicState& mel_state,
                          uint8_t prev_pitch, Key key, bool is_minor,
                          Tick tick, uint8_t low_pitch, uint8_t high_pitch,
                          std::mt19937& rng, float inject_prob) {
  ScaleType scale = is_minor ? ScaleType::HarmonicMinor : ScaleType::Major;
  auto candidate = tryInjectFigure(mel_state, prev_pitch, key, scale, tick,
                                   low_pitch, high_pitch, rng, inject_prob);
  if (!candidate.has_value() || candidate->pitches.size() < 2) return 0;

  // Score the first target pitch using existing scoring.
  uint8_t target = candidate->pitches[1];  // First note after current.
  bool is_chord_tone = true;  // Approximation: figure patterns are mostly scale tones.
  float score = scoreCandidatePitch(mel_state, prev_pitch, target, tick,
                                    is_chord_tone);
  // Minimum score threshold: reject if melodically very poor.
  if (score < -0.12f) return 0;

  return target;
}

// ---------------------------------------------------------------------------
// Variation stage generators
// ---------------------------------------------------------------------------

/// @brief Generate quarter-note chord tones for the Establish stage (variations 0-2).
///
/// Creates simple quarter-note lines using chord tones from the harmonic
/// timeline, providing a stable harmonic foundation.
///
/// @param start_tick Starting tick of this variation.
/// @param bars Number of bars in one ground bass cycle.
/// @param voice_idx Voice/track index for this part.
/// @param timeline Harmonic timeline for chord context.
/// @param rng Random number generator.
/// @return Vector of NoteEvents.
std::vector<NoteEvent> generateEstablishVariation(Tick start_tick, int bars,
                                                  uint8_t voice_idx,
                                                  const HarmonicTimeline& timeline,
                                                  std::mt19937& rng) {
  std::vector<NoteEvent> notes;
  uint8_t low_pitch = getVoiceLowPitch(voice_idx);
  uint8_t high_pitch = getVoiceHighPitch(voice_idx);

  Tick end_tick = start_tick + static_cast<Tick>(bars) * kTicksPerBar;
  Tick total_duration = end_tick - start_tick;
  Tick current_tick = start_tick;

  // Voice dynamics model for direction persistence and interval distribution.
  MelodicState mel_state;
  mel_state.contour = {PhraseContour::Arch, 0.40f, 0.25f};
  uint8_t pending_resolution = 0;  // Non-zero: bias next note toward this pitch.

  // Initial scale tones for stepwise motion.
  const HarmonicEvent& first_event = timeline.getAt(start_tick);
  auto scale_tones = getScaleTones(first_event.key, first_event.is_minor,
                                   low_pitch, high_pitch);
  if (scale_tones.empty()) return notes;

  size_t tone_idx = scale_tones.size() / 2;
  bool ascending = rng::rollProbability(rng, 0.5f);
  bool has_prev = false;
  uint8_t prev_pitch = 0;

  while (current_tick < end_tick) {
    mel_state.phrase_progress =
        static_cast<float>(current_tick - start_tick) / static_cast<float>(total_duration);

    const HarmonicEvent& event = timeline.getAt(current_tick);
    auto new_tones = getScaleTones(event.key, event.is_minor,
                                   low_pitch, high_pitch);
    if (new_tones.empty()) { current_tick += kQuarterNote; continue; }

    // Re-map position if scale changed.
    if (new_tones != scale_tones) {
      scale_tones = std::move(new_tones);
      if (!notes.empty()) {
        tone_idx = findClosestToneIndex(scale_tones, notes.back().pitch);
      }
    }

    // Strong-beat chord tone snapping.
    Tick relative_tick = current_tick - start_tick;
    if (relative_tick % kTicksPerBar == 0) {
      // Bar head: find closest chord tone by pitch distance, excluding
      // prev_pitch to avoid repeated notes.
      size_t best_idx = tone_idx;
      int best_pitch_dist = 999;
      bool found_non_repeat = false;
      for (size_t idx = 0; idx < scale_tones.size(); ++idx) {
        if (!isChordTone(scale_tones[idx], event)) continue;
        if (has_prev && scale_tones[idx] == prev_pitch) continue;
        int pitch_dist = std::abs(static_cast<int>(scale_tones[idx]) -
                                  static_cast<int>(scale_tones[tone_idx]));
        // Resolution bonus: favor candidates near pending resolution target.
        if (pending_resolution > 0 &&
            std::abs(static_cast<int>(scale_tones[idx]) -
                     static_cast<int>(pending_resolution)) <= 2) {
          pitch_dist -= 5;  // Strong bias toward resolution target.
        }
        if (pitch_dist < best_pitch_dist) {
          best_pitch_dist = pitch_dist;
          best_idx = idx;
          found_non_repeat = true;
        }
      }
      if (!found_non_repeat) {
        // Fallback: allow same pitch (all chord tones == prev_pitch).
        for (size_t idx = 0; idx < scale_tones.size(); ++idx) {
          if (!isChordTone(scale_tones[idx], event)) continue;
          int pitch_dist = std::abs(static_cast<int>(scale_tones[idx]) -
                                    static_cast<int>(scale_tones[tone_idx]));
          if (pitch_dist < best_pitch_dist) {
            best_pitch_dist = pitch_dist;
            best_idx = idx;
          }
        }
      }
      tone_idx = best_idx;
    } else if (relative_tick % kTicksPerBeat == 0) {
      // Beat head: prefer chord tone — try ±1, ±2 with prev_pitch filter.
      if (!isChordTone(scale_tones[tone_idx], event)) {
        size_t best_idx = tone_idx;
        bool found = false;
        for (int offset : {1, -1, 2, -2}) {
          int idx = static_cast<int>(tone_idx) + offset;
          if (idx < 0 || idx >= static_cast<int>(scale_tones.size())) continue;
          if (!isChordTone(scale_tones[idx], event)) continue;
          if (has_prev && scale_tones[idx] == prev_pitch) continue;
          best_idx = static_cast<size_t>(idx);
          found = true;
          break;
        }
        if (!found) {
          // Retry without prev_pitch filter.
          for (int offset : {1, -1, 2, -2}) {
            int idx = static_cast<int>(tone_idx) + offset;
            if (idx < 0 || idx >= static_cast<int>(scale_tones.size())) continue;
            if (!isChordTone(scale_tones[idx], event)) continue;
            best_idx = static_cast<size_t>(idx);
            break;
          }
        }
        tone_idx = best_idx;
      }
    }

    uint8_t candidate = scale_tones[tone_idx];

    // Vocabulary injection attempt (Establish: conservative 0.15).
    if (has_prev && prev_pitch > 0) {
      uint8_t vocab_pitch = tryVocabularyNote(
          mel_state, prev_pitch, event.key, event.is_minor,
          current_tick, low_pitch, high_pitch, rng, 0.15f);
      if (vocab_pitch > 0) {
        candidate = vocab_pitch;
        tone_idx = findClosestToneIndex(scale_tones, candidate);
      }
    }

    // Strong-beat bass consonance: Establish uses strict tolerance
    // (bar heads + beat heads must be consonant with ground bass).
    candidate = enforceStrongBeatBassConsonance(
        candidate, current_tick, event, NctTolerance::Strict,
        scale_tones, tone_idx, low_pitch, high_pitch);

    // 2-consecutive repetition guard.
    if (has_prev && candidate == prev_pitch) {
      // Step ±1 in ascending direction, fallback to reverse.
      if (ascending && tone_idx + 1 < scale_tones.size()) {
        ++tone_idx;
        candidate = scale_tones[tone_idx];
      } else if (!ascending && tone_idx > 0) {
        --tone_idx;
        candidate = scale_tones[tone_idx];
      } else if (tone_idx + 1 < scale_tones.size()) {
        ++tone_idx;
        candidate = scale_tones[tone_idx];
      } else if (tone_idx > 0) {
        --tone_idx;
        candidate = scale_tones[tone_idx];
      }
      // If still same pitch: fallback allow — post-processing catches 3+.
    }

    Tick dur = kQuarterNote;
    Tick remaining = end_tick - current_tick;
    if (dur > remaining) dur = remaining;
    if (dur == 0) break;

    NoteEvent note;
    note.start_tick = current_tick;
    note.duration = dur;
    note.pitch = candidate;
    note.velocity = kOrganVelocity;
    note.voice = voice_idx;
    note.source = BachNoteSource::FreeCounterpoint;
    notes.push_back(note);

    // Update melodic state tracking before overwriting prev_pitch.
    uint8_t prev_pitch_before = prev_pitch;
    has_prev = true;
    prev_pitch = candidate;
    if (has_prev && prev_pitch_before > 0) {
      updateMelodicState(mel_state, prev_pitch_before, candidate);
    }

    // Compute resolution target for non-chord-tone notes (outer voice bias).
    if (has_prev && !isChordTone(candidate, event)) {
      uint8_t res = findResolutionTarget(candidate, prev_pitch_before, event,
                                         event.key, event.is_minor,
                                         current_tick);
      if (res > 0) pending_resolution = res;
    } else {
      pending_resolution = 0;  // Chord tone placed: resolution satisfied.
    }

    current_tick += dur;

    // Stepwise advancement with direction persistence.
    ascending = (chooseMelodicDirection(mel_state, rng) > 0);
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
        if (tone_idx + 1 < scale_tones.size()) ++tone_idx;
      }
    }
  }

  return notes;
}

/// @brief Generate eighth-note scale passages for early Develop stage (variations 3-5).
///
/// Creates flowing eighth-note lines moving stepwise through scale tones,
/// providing melodic interest against the ground bass.
///
/// @param start_tick Starting tick of this variation.
/// @param bars Number of bars in one ground bass cycle.
/// @param voice_idx Voice/track index for this part.
/// @param timeline Harmonic timeline for chord context.
/// @param rng Random number generator.
/// @return Vector of NoteEvents.
std::vector<NoteEvent> generateDevelopEarlyVariation(Tick start_tick, int bars,
                                                     uint8_t voice_idx,
                                                     const HarmonicTimeline& timeline,
                                                     std::mt19937& rng) {
  std::vector<NoteEvent> notes;
  uint8_t low_pitch = getVoiceLowPitch(voice_idx);
  uint8_t high_pitch = getVoiceHighPitch(voice_idx);

  Tick end_tick = start_tick + static_cast<Tick>(bars) * kTicksPerBar;
  Tick total_duration = end_tick - start_tick;
  Tick current_tick = start_tick;

  // Voice dynamics model for direction persistence and interval distribution.
  MelodicState mel_state;
  mel_state.contour = {PhraseContour::Arch, 0.33f, 0.25f};
  uint8_t pending_resolution = 0;

  // Initial scale tones for starting position.
  const HarmonicEvent& first_event = timeline.getAt(start_tick);
  auto scale_tones = getScaleTones(first_event.key, first_event.is_minor,
                                   low_pitch, high_pitch);
  if (scale_tones.empty()) return notes;

  // Start roughly in the middle of the scale range.
  size_t tone_idx = scale_tones.size() / 2;
  bool ascending = rng::rollProbability(rng, 0.5f);
  bool has_prev = false;
  uint8_t prev_pitch = 0;

  while (current_tick < end_tick) {
    mel_state.phrase_progress =
        static_cast<float>(current_tick - start_tick) / static_cast<float>(total_duration);

    // Re-acquire scale tones per beat from timeline (harmony may change).
    const HarmonicEvent& event = timeline.getAt(current_tick);
    auto new_tones = getScaleTones(event.key, event.is_minor,
                                   low_pitch, high_pitch);
    if (new_tones.empty()) { current_tick += kEighthNote; continue; }

    // Re-map position if scale changed.
    if (new_tones != scale_tones) {
      uint8_t old_pitch = scale_tones.empty() ? 0 : scale_tones[tone_idx];
      scale_tones = std::move(new_tones);
      if (!notes.empty()) {
        tone_idx = findClosestToneIndex(scale_tones, notes.back().pitch);
      } else {
        tone_idx = findClosestToneIndex(scale_tones, old_pitch);
      }
    }

    uint8_t candidate = scale_tones[tone_idx];

    // Resolution bias: if pending resolution target, prefer candidates near it.
    if (pending_resolution > 0 && has_prev) {
      size_t best_res_idx = tone_idx;
      int best_res_dist = 999;
      for (size_t idx = 0; idx < scale_tones.size(); ++idx) {
        int dist = std::abs(static_cast<int>(scale_tones[idx]) -
                            static_cast<int>(pending_resolution));
        // Only consider candidates within a 3rd of the resolution target.
        if (dist > 4) continue;
        if (scale_tones[idx] == prev_pitch) continue;
        if (dist < best_res_dist) {
          best_res_dist = dist;
          best_res_idx = idx;
        }
      }
      if (best_res_dist <= 4) {
        tone_idx = best_res_idx;
        candidate = scale_tones[tone_idx];
        pending_resolution = 0;
      }
    }

    // Vocabulary injection attempt (DevelopEarly: moderate 0.25).
    if (pending_resolution == 0 && has_prev && prev_pitch > 0) {
      uint8_t vocab_pitch = tryVocabularyNote(
          mel_state, prev_pitch, event.key, event.is_minor,
          current_tick, low_pitch, high_pitch, rng, 0.25f);
      if (vocab_pitch > 0) {
        candidate = vocab_pitch;
        tone_idx = findClosestToneIndex(scale_tones, candidate);
      }
    }

    // Strong-beat bass consonance: DevelopEarly uses moderate tolerance
    // (bar heads enforced, beat heads handled by existing chord-tone preference).
    candidate = enforceStrongBeatBassConsonance(
        candidate, current_tick, event, NctTolerance::Moderate,
        scale_tones, tone_idx, low_pitch, high_pitch);

    // 2-consecutive repetition guard with direction-based search.
    if (has_prev && candidate == prev_pitch) {
      const int offsets_asc[] = {1, -1, 2, -2, 3, -3};
      const int offsets_desc[] = {-1, 1, -2, 2, -3, 3};
      const int* offsets = ascending ? offsets_asc : offsets_desc;
      for (int idx = 0; idx < 6; ++idx) {
        int off = static_cast<int>(tone_idx) + offsets[idx];
        if (off < 0 || off >= static_cast<int>(scale_tones.size())) continue;
        uint8_t cnd = scale_tones[off];
        if (cnd == prev_pitch) continue;
        if (std::abs(static_cast<int>(cnd) - static_cast<int>(prev_pitch)) > 7)
          continue;
        tone_idx = static_cast<size_t>(off);
        candidate = cnd;
        break;
      }
      // Fallback: allow same pitch — post-processing catches 3+.
    }

    Tick dur = kEighthNote;
    Tick remaining = end_tick - current_tick;
    if (dur > remaining) dur = remaining;
    if (dur == 0) break;

    NoteEvent note;
    note.start_tick = current_tick;
    note.duration = dur;
    note.pitch = candidate;
    note.velocity = kOrganVelocity;
    note.voice = voice_idx;
    note.source = BachNoteSource::FreeCounterpoint;
    notes.push_back(note);

    // Update melodic state tracking before overwriting prev_pitch.
    uint8_t prev_pitch_before = prev_pitch;
    has_prev = true;
    prev_pitch = candidate;
    if (prev_pitch_before > 0) {
      updateMelodicState(mel_state, prev_pitch_before, candidate);
    }

    // Compute resolution target for non-chord-tone notes.
    if (prev_pitch_before > 0 && !isChordTone(candidate, event)) {
      uint8_t res = findResolutionTarget(candidate, prev_pitch_before, event,
                                         event.key, event.is_minor,
                                         current_tick);
      if (res > 0) pending_resolution = res;
    } else {
      pending_resolution = 0;
    }

    current_tick += dur;

    // Stepwise advancement with direction persistence.
    ascending = (chooseMelodicDirection(mel_state, rng) > 0);
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
        if (tone_idx + 1 < scale_tones.size()) ++tone_idx;
      }
    }
  }

  return notes;
}

/// @brief Generate eighth-note arpeggios for late Develop stage (variations 6-8).
///
/// Creates arpeggio patterns using chord tones spanning the voice range,
/// providing harmonic clarity with faster motion.
///
/// @param start_tick Starting tick of this variation.
/// @param bars Number of bars in one ground bass cycle.
/// @param voice_idx Voice/track index for this part.
/// @param timeline Harmonic timeline for chord context.
/// @param rng Random number generator.
/// @return Vector of NoteEvents.
std::vector<NoteEvent> generateDevelopLateVariation(Tick start_tick, int bars,
                                                    uint8_t voice_idx,
                                                    const HarmonicTimeline& timeline,
                                                    std::mt19937& rng) {
  std::vector<NoteEvent> notes;
  uint8_t low_pitch = getVoiceLowPitch(voice_idx);
  uint8_t high_pitch = getVoiceHighPitch(voice_idx);

  Tick end_tick = start_tick + static_cast<Tick>(bars) * kTicksPerBar;
  Tick total_duration = end_tick - start_tick;
  Tick current_tick = start_tick;

  // Voice dynamics model for direction persistence.
  MelodicState mel_state;
  mel_state.contour = {PhraseContour::Wave, 0.4f, 0.25f};
  uint8_t pending_resolution = 0;

  // Build initial arpeggio pitches and initialize traversal state ONCE.
  const HarmonicEvent& first_event = timeline.getAt(start_tick);
  std::vector<uint8_t> arp_pitches =
      collectChordTonesInRange(first_event.chord, low_pitch, high_pitch);
  if (arp_pitches.empty()) return notes;
  std::sort(arp_pitches.begin(), arp_pitches.end());

  size_t arp_idx = static_cast<size_t>(
      rng::rollRange(rng, 0, static_cast<int>(arp_pitches.size()) - 1));
  bool going_up = rng::rollProbability(rng, 0.6f);
  bool has_prev = false;
  uint8_t prev_pitch = 0;

  while (current_tick < end_tick) {
    mel_state.phrase_progress =
        static_cast<float>(current_tick - start_tick) / static_cast<float>(total_duration);

    const HarmonicEvent& event = timeline.getAt(current_tick);

    // Rebuild arpeggio pitches on harmony change.
    auto new_arp = collectChordTonesInRange(event.chord, low_pitch, high_pitch);
    if (new_arp.empty()) {
      current_tick += kEighthNote;
      continue;
    }
    std::sort(new_arp.begin(), new_arp.end());

    if (new_arp != arp_pitches) {
      arp_pitches = std::move(new_arp);
      if (!notes.empty()) {
        arp_idx = findClosestToneIndex(arp_pitches, notes.back().pitch);
      } else {
        arp_idx = arp_pitches.size() / 2;
      }
    }

    // Fill one beat at a time to allow chord changes.
    Tick beat_end = current_tick + kTicksPerBeat;
    if (beat_end > end_tick) beat_end = end_tick;

    while (current_tick < beat_end) {
      mel_state.phrase_progress =
          static_cast<float>(current_tick - start_tick) / static_cast<float>(total_duration);

      Tick dur = kEighthNote;
      Tick remaining = beat_end - current_tick;
      if (dur > remaining) dur = remaining;
      if (dur == 0) break;

      uint8_t candidate = arp_pitches[arp_idx];

      // Resolution bias: if pending, prefer arpeggio tone nearest target.
      if (pending_resolution > 0 && has_prev && arp_pitches.size() > 1) {
        size_t best_res_idx = arp_idx;
        int best_res_dist = 999;
        for (size_t idx = 0; idx < arp_pitches.size(); ++idx) {
          if (arp_pitches[idx] == prev_pitch) continue;
          int dist = std::abs(static_cast<int>(arp_pitches[idx]) -
                              static_cast<int>(pending_resolution));
          if (dist < best_res_dist) {
            best_res_dist = dist;
            best_res_idx = idx;
          }
        }
        if (best_res_dist <= 4) {
          arp_idx = best_res_idx;
          candidate = arp_pitches[arp_idx];
          pending_resolution = 0;
        }
      }

      // Vocabulary injection attempt (DevelopLate: elevated 0.30).
      if (pending_resolution == 0 && has_prev && prev_pitch > 0) {
        uint8_t vocab_pitch = tryVocabularyNote(
            mel_state, prev_pitch, event.key, event.is_minor,
            current_tick, low_pitch, high_pitch, rng, 0.30f);
        if (vocab_pitch > 0) {
          candidate = vocab_pitch;
          arp_idx = findClosestToneIndex(arp_pitches, candidate);
        }
      }

      // Strong-beat bass consonance: DevelopLate uses arpeggio enforcement
      // (bar heads must be consonant with ground bass).
      candidate = enforceArpeggioBassConsonance(
          candidate, current_tick, event, arp_pitches, arp_idx);

      // 2-consecutive repetition guard for arpeggio.
      if (has_prev && candidate == prev_pitch) {
        if (arp_pitches.size() == 1) {
          // Fallback: allow same pitch — post-processing catches 3+.
        } else if (arp_pitches.size() == 2) {
          // Pick the other index directly.
          size_t other = (arp_idx == 0) ? 1 : 0;
          candidate = arp_pitches[other];
          arp_idx = other;
          // Safety: if still same pitch after switch, fallback.
        } else {
          // Step in going_up direction; if at boundary, reverse once.
          bool resolved = false;
          if (going_up && arp_idx + 1 < arp_pitches.size()) {
            ++arp_idx;
            candidate = arp_pitches[arp_idx];
            resolved = (candidate != prev_pitch);
          } else if (!going_up && arp_idx > 0) {
            --arp_idx;
            candidate = arp_pitches[arp_idx];
            resolved = (candidate != prev_pitch);
          }
          if (!resolved) {
            // Reverse direction once and try.
            going_up = !going_up;
            if (going_up && arp_idx + 1 < arp_pitches.size()) {
              ++arp_idx;
              candidate = arp_pitches[arp_idx];
            } else if (!going_up && arp_idx > 0) {
              --arp_idx;
              candidate = arp_pitches[arp_idx];
            }
            // If still same pitch: fallback allow.
          }
        }
      }

      NoteEvent note;
      note.start_tick = current_tick;
      note.duration = dur;
      note.pitch = candidate;
      note.velocity = kOrganVelocity;
      note.voice = voice_idx;
      note.source = BachNoteSource::FreeCounterpoint;
      notes.push_back(note);

      // Update melodic state tracking before overwriting prev_pitch.
      uint8_t prev_pitch_before = prev_pitch;
      has_prev = true;
      prev_pitch = candidate;
      if (prev_pitch_before > 0) {
        updateMelodicState(mel_state, prev_pitch_before, candidate);
      }

      // Compute resolution target for non-chord-tone notes.
      if (prev_pitch_before > 0 && !isChordTone(candidate, event)) {
        uint8_t res = findResolutionTarget(candidate, prev_pitch_before, event,
                                           event.key, event.is_minor,
                                           current_tick);
        if (res > 0) pending_resolution = res;
      } else {
        pending_resolution = 0;
      }

      current_tick += dur;

      // Zigzag through arpeggio pitches, using MelodicState for direction.
      going_up = (chooseMelodicDirection(mel_state, rng) > 0);
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
          if (arp_idx + 1 < arp_pitches.size()) ++arp_idx;
        }
      }
    }
  }

  return notes;
}

/// @brief Generate sixteenth-note figurations for Accumulate/Resolve stage
///        (variations 9-11).
///
/// Creates rapid sixteenth-note passages mixing scale tones and chord tones,
/// providing the climactic intensity before the final resolution.
///
/// @param start_tick Starting tick of this variation.
/// @param bars Number of bars in one ground bass cycle.
/// @param voice_idx Voice/track index for this part.
/// @param timeline Harmonic timeline for chord context.
/// @param rng Random number generator.
/// @return Vector of NoteEvents.
std::vector<NoteEvent> generateAccumulateVariation(Tick start_tick, int bars,
                                                   uint8_t voice_idx,
                                                   const HarmonicTimeline& timeline,
                                                   std::mt19937& rng) {
  std::vector<NoteEvent> notes;
  uint8_t low_pitch = getVoiceLowPitch(voice_idx);
  uint8_t high_pitch = getVoiceHighPitch(voice_idx);

  Tick end_tick = start_tick + static_cast<Tick>(bars) * kTicksPerBar;
  Tick total_duration = end_tick - start_tick;
  Tick current_tick = start_tick;

  // Voice dynamics model for direction persistence and interval distribution.
  MelodicState mel_state;
  mel_state.contour = {PhraseContour::Arch, 0.66f, 0.25f};
  uint8_t pending_resolution = 0;

  // Initial scale tones for starting position.
  const HarmonicEvent& first_event = timeline.getAt(start_tick);
  auto scale_tones = getScaleTones(first_event.key, first_event.is_minor,
                                   low_pitch, high_pitch);
  if (scale_tones.empty()) return notes;

  // Start in the upper portion of the range for intensity.
  size_t tone_idx = scale_tones.size() * 2 / 3;
  bool ascending = rng::rollProbability(rng, 0.5f);
  bool has_prev = false;
  uint8_t prev_pitch = 0;

  while (current_tick < end_tick) {
    mel_state.phrase_progress =
        static_cast<float>(current_tick - start_tick) / static_cast<float>(total_duration);

    // Re-acquire scale tones per beat from timeline (harmony may change).
    const HarmonicEvent& event = timeline.getAt(current_tick);
    auto new_tones = getScaleTones(event.key, event.is_minor,
                                   low_pitch, high_pitch);
    if (new_tones.empty()) { current_tick += kSixteenthNote; continue; }

    // Re-map position if scale changed.
    if (new_tones != scale_tones) {
      uint8_t old_pitch = scale_tones.empty() ? 0 : scale_tones[tone_idx];
      scale_tones = std::move(new_tones);
      if (!notes.empty()) {
        tone_idx = findClosestToneIndex(scale_tones, notes.back().pitch);
      } else {
        tone_idx = findClosestToneIndex(scale_tones, old_pitch);
      }
    }

    uint8_t candidate = scale_tones[tone_idx];

    // Resolution bias: if pending resolution target, prefer candidates near it.
    if (pending_resolution > 0 && has_prev) {
      size_t best_res_idx = tone_idx;
      int best_res_dist = 999;
      for (size_t idx = 0; idx < scale_tones.size(); ++idx) {
        int dist = std::abs(static_cast<int>(scale_tones[idx]) -
                            static_cast<int>(pending_resolution));
        if (dist > 4) continue;
        if (scale_tones[idx] == prev_pitch) continue;
        if (dist < best_res_dist) {
          best_res_dist = dist;
          best_res_idx = idx;
        }
      }
      if (best_res_dist <= 4) {
        tone_idx = best_res_idx;
        candidate = scale_tones[tone_idx];
        pending_resolution = 0;
      }
    }

    // Vocabulary injection attempt (Accumulate: aggressive 0.35).
    if (pending_resolution == 0 && has_prev && prev_pitch > 0) {
      uint8_t vocab_pitch = tryVocabularyNote(
          mel_state, prev_pitch, event.key, event.is_minor,
          current_tick, low_pitch, high_pitch, rng, 0.35f);
      if (vocab_pitch > 0) {
        candidate = vocab_pitch;
        tone_idx = findClosestToneIndex(scale_tones, candidate);
      }
    }

    // Strong-beat bass consonance: Accumulate uses moderate tolerance
    // (bar heads enforced; sixteenth-note density makes beat-level
    // enforcement too restrictive for melodic flow).
    candidate = enforceStrongBeatBassConsonance(
        candidate, current_tick, event, NctTolerance::Moderate,
        scale_tones, tone_idx, low_pitch, high_pitch);

    // 2-consecutive repetition guard with direction-based search.
    if (has_prev && candidate == prev_pitch) {
      const int offsets_asc[] = {1, -1, 2, -2, 3, -3};
      const int offsets_desc[] = {-1, 1, -2, 2, -3, 3};
      const int* offsets = ascending ? offsets_asc : offsets_desc;
      for (int idx = 0; idx < 6; ++idx) {
        int off = static_cast<int>(tone_idx) + offsets[idx];
        if (off < 0 || off >= static_cast<int>(scale_tones.size())) continue;
        uint8_t cnd = scale_tones[off];
        if (cnd == prev_pitch) continue;
        if (std::abs(static_cast<int>(cnd) - static_cast<int>(prev_pitch)) > 7)
          continue;
        tone_idx = static_cast<size_t>(off);
        candidate = cnd;
        break;
      }
      // Fallback: allow same pitch — post-processing catches 3+.
    }

    Tick dur = kSixteenthNote;
    Tick remaining = end_tick - current_tick;
    if (dur > remaining) dur = remaining;
    if (dur == 0) break;

    NoteEvent note;
    note.start_tick = current_tick;
    note.duration = dur;
    note.pitch = candidate;
    note.velocity = kOrganVelocity;
    note.voice = voice_idx;
    note.source = BachNoteSource::FreeCounterpoint;
    notes.push_back(note);

    // Update melodic state tracking before overwriting prev_pitch.
    uint8_t prev_pitch_before = prev_pitch;
    has_prev = true;
    prev_pitch = candidate;
    if (prev_pitch_before > 0) {
      updateMelodicState(mel_state, prev_pitch_before, candidate);
    }

    // Compute resolution target for non-chord-tone notes.
    if (prev_pitch_before > 0 && !isChordTone(candidate, event)) {
      uint8_t res = findResolutionTarget(candidate, prev_pitch_before, event,
                                         event.key, event.is_minor,
                                         current_tick);
      if (res > 0) pending_resolution = res;
    } else {
      pending_resolution = 0;
    }

    current_tick += dur;

    // Stepwise advancement with direction persistence.
    // Accumulate stage: allow occasional skip (20%) for intensity.
    ascending = (chooseMelodicDirection(mel_state, rng) > 0);
    int step = rng::rollProbability(rng, 0.20f) ? 2 : 1;
    if (ascending) {
      if (tone_idx + static_cast<size_t>(step) < scale_tones.size()) {
        tone_idx += static_cast<size_t>(step);
      } else {
        ascending = false;
        if (tone_idx > 0) --tone_idx;
      }
    } else {
      if (tone_idx >= static_cast<size_t>(step)) {
        tone_idx -= static_cast<size_t>(step);
      } else {
        ascending = true;
        if (tone_idx + 1 < scale_tones.size()) ++tone_idx;
      }
    }
  }

  return notes;
}

/// @brief Generate half/quarter-note lines for the Resolve stage (final 1-2 variations).
///
/// Creates a simplified texture with broader rhythmic values (half and quarter
/// notes), bringing the passacaglia to a dignified conclusion. This mirrors
/// Baroque practice where the final variations often return to simpler motion
/// after the climactic accumulation.
///
/// @param start_tick Starting tick of this variation.
/// @param bars Number of bars in one ground bass cycle.
/// @param voice_idx Voice/track index for this part.
/// @param timeline Harmonic timeline for chord context.
/// @param rng Random number generator.
/// @return Vector of NoteEvents.
std::vector<NoteEvent> generateResolveVariation(Tick start_tick, int bars,
                                                uint8_t voice_idx,
                                                const HarmonicTimeline& timeline,
                                                std::mt19937& rng) {
  std::vector<NoteEvent> notes;
  uint8_t low_pitch = getVoiceLowPitch(voice_idx);
  uint8_t high_pitch = getVoiceHighPitch(voice_idx);

  Tick end_tick = start_tick + static_cast<Tick>(bars) * kTicksPerBar;
  Tick total_duration = end_tick - start_tick;
  Tick current_tick = start_tick;

  MelodicState mel_state;
  mel_state.contour = {PhraseContour::Descent, 0.4f, 0.20f};

  const HarmonicEvent& first_event = timeline.getAt(start_tick);
  auto scale_tones = getScaleTones(first_event.key, first_event.is_minor,
                                   low_pitch, high_pitch);
  if (scale_tones.empty()) return notes;

  size_t tone_idx = scale_tones.size() / 2;
  bool ascending = rng::rollProbability(rng, 0.5f);
  bool has_prev = false;
  uint8_t prev_pitch = 0;

  // Duration weights for Resolve: favor half and quarter notes.
  constexpr float kResolveHalfWeight = 0.55f;

  while (current_tick < end_tick) {
    mel_state.phrase_progress =
        static_cast<float>(current_tick - start_tick) / static_cast<float>(total_duration);

    const HarmonicEvent& event = timeline.getAt(current_tick);
    auto new_tones = getScaleTones(event.key, event.is_minor,
                                   low_pitch, high_pitch);
    if (new_tones.empty()) { current_tick += kQuarterNote; continue; }

    if (new_tones != scale_tones) {
      scale_tones = std::move(new_tones);
      if (!notes.empty()) {
        tone_idx = findClosestToneIndex(scale_tones, notes.back().pitch);
      }
    }

    // Strong chord-tone snapping on bar and beat heads.
    Tick relative_tick = current_tick - start_tick;
    if (relative_tick % kTicksPerBeat == 0) {
      size_t best_idx = tone_idx;
      int best_dist = 999;
      for (size_t idx = 0; idx < scale_tones.size(); ++idx) {
        if (!isChordTone(scale_tones[idx], event)) continue;
        if (has_prev && scale_tones[idx] == prev_pitch) continue;
        int dist = std::abs(static_cast<int>(scale_tones[idx]) -
                            static_cast<int>(scale_tones[tone_idx]));
        if (dist < best_dist) {
          best_dist = dist;
          best_idx = idx;
        }
      }
      tone_idx = best_idx;
    }

    uint8_t candidate = scale_tones[tone_idx];

    // Vocabulary injection attempt (Resolve: conservative 0.15).
    if (has_prev && prev_pitch > 0) {
      uint8_t vocab_pitch = tryVocabularyNote(
          mel_state, prev_pitch, event.key, event.is_minor,
          current_tick, low_pitch, high_pitch, rng, 0.15f);
      if (vocab_pitch > 0) {
        candidate = vocab_pitch;
        tone_idx = findClosestToneIndex(scale_tones, candidate);
      }
    }

    // Strong-beat bass consonance: Resolve uses strict tolerance
    // (broad rhythms — every bar and beat head must be consonant with bass).
    candidate = enforceStrongBeatBassConsonance(
        candidate, current_tick, event, NctTolerance::Strict,
        scale_tones, tone_idx, low_pitch, high_pitch);

    // 2-consecutive repetition guard.
    if (has_prev && candidate == prev_pitch) {
      if (ascending && tone_idx + 1 < scale_tones.size()) {
        ++tone_idx;
        candidate = scale_tones[tone_idx];
      } else if (!ascending && tone_idx > 0) {
        --tone_idx;
        candidate = scale_tones[tone_idx];
      } else if (tone_idx + 1 < scale_tones.size()) {
        ++tone_idx;
        candidate = scale_tones[tone_idx];
      } else if (tone_idx > 0) {
        --tone_idx;
        candidate = scale_tones[tone_idx];
      }
    }

    // Resolve uses half and quarter notes.
    Tick dur = rng::rollProbability(rng, kResolveHalfWeight) ? kHalfNote : kQuarterNote;
    Tick remaining = end_tick - current_tick;
    if (dur > remaining) dur = remaining;
    if (dur == 0) break;

    NoteEvent note;
    note.start_tick = current_tick;
    note.duration = dur;
    note.pitch = candidate;
    note.velocity = kOrganVelocity;
    note.voice = voice_idx;
    note.source = BachNoteSource::FreeCounterpoint;
    notes.push_back(note);

    uint8_t prev_pitch_before = prev_pitch;
    has_prev = true;
    prev_pitch = candidate;
    if (has_prev && prev_pitch_before > 0) {
      updateMelodicState(mel_state, prev_pitch_before, candidate);
    }

    current_tick += dur;

    // Stepwise advancement.
    ascending = (chooseMelodicDirection(mel_state, rng) > 0);
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
        if (tone_idx + 1 < scale_tones.size()) ++tone_idx;
      }
    }
  }

  return notes;
}

// ---------------------------------------------------------------------------
// Strong-beat vertical consonance repair
// ---------------------------------------------------------------------------

/// @brief Find the nearest consonant pitch relative to a bass note, constrained
///        to scale tones within a voice range.
///
/// Searches outward from the current pitch by +/- 1..4 semitones, checking each
/// candidate for: (1) consonance with bass_pitch, (2) membership in the scale,
/// (3) containment within voice range. Returns 0 if no adjustment is found.
///
/// @param current_pitch Current non-consonant pitch.
/// @param bass_pitch Ground bass MIDI pitch to measure interval against.
/// @param key Musical key.
/// @param scale Scale type.
/// @param low_pitch Voice range lower bound.
/// @param high_pitch Voice range upper bound.
/// @return Adjusted pitch, or 0 if no consonant scale tone found nearby.
uint8_t findNearestConsonantScaleTone(uint8_t current_pitch, uint8_t bass_pitch,
                                       Key key, ScaleType scale,
                                       uint8_t low_pitch, uint8_t high_pitch) {
  // Search outward: +-1, +-2, +-3, +-4 semitones.
  for (int delta : {1, -1, 2, -2, 3, -3, 4, -4}) {
    int candidate = static_cast<int>(current_pitch) + delta;
    if (candidate < static_cast<int>(low_pitch) ||
        candidate > static_cast<int>(high_pitch)) {
      continue;
    }
    uint8_t cand_pitch = static_cast<uint8_t>(candidate);

    // Must be a scale tone.
    if (!scale_util::isScaleTone(cand_pitch, key, scale)) continue;

    // Must be consonant with the bass.
    int interval_to_bass = std::abs(static_cast<int>(cand_pitch) -
                                    static_cast<int>(bass_pitch));
    if (interval_util::isConsonance(interval_to_bass)) {
      return cand_pitch;
    }
  }
  return 0;  // No suitable adjustment found.
}

/// @brief Repair strong-beat vertical dissonances between upper voices and
///        the ground bass within a single variation.
///
/// For each strong beat (bar head: tick % kTicksPerBar == 0, beat head:
/// tick % kTicksPerBeat == 0), finds all sounding upper-voice pitches and
/// checks their interval against the ground bass note at that tick. If the
/// interval is dissonant, adjusts the upper-voice pitch to the nearest
/// consonant scale tone.
///
/// Ground bass notes (source == GroundBass) are NEVER modified.
///
/// @param variation_notes All notes for one variation (upper voices + bass).
/// @param key Key signature for scale-tone validation.
/// @return Number of pitches adjusted.
int repairStrongBeatConsonance(std::vector<NoteEvent>& variation_notes,
                                const KeySignature& key) {
  int repairs = 0;
  ScaleType scale = key.is_minor ? ScaleType::HarmonicMinor : ScaleType::Major;

  // Build a tick-indexed lookup for bass pitches (ground bass notes).
  // Ground bass uses whole notes (one per bar), so we only need bar-level lookup.
  // Collect all ground bass notes for range queries.
  struct BassPitchRange {
    Tick start;
    Tick end;
    uint8_t pitch;
  };
  std::vector<BassPitchRange> bass_ranges;
  for (const auto& note : variation_notes) {
    if (note.source == BachNoteSource::GroundBass) {
      bass_ranges.push_back({note.start_tick, note.start_tick + note.duration,
                             note.pitch});
    }
  }

  if (bass_ranges.empty()) return 0;

  // For each upper-voice note on a strong beat, check consonance with bass.
  for (auto& note : variation_notes) {
    // Never modify ground bass notes.
    if (note.source == BachNoteSource::GroundBass) continue;

    // Only check strong beats (bar heads and beat heads).
    if (note.start_tick % kTicksPerBeat != 0) continue;

    // Find the bass pitch sounding at this tick.
    uint8_t bass_pitch = 0;
    bool found_bass = false;
    for (const auto& bass : bass_ranges) {
      if (note.start_tick >= bass.start && note.start_tick < bass.end) {
        bass_pitch = bass.pitch;
        found_bass = true;
        break;
      }
    }
    if (!found_bass) continue;

    // Check interval with bass.
    int interval_to_bass = std::abs(static_cast<int>(note.pitch) -
                                    static_cast<int>(bass_pitch));
    if (interval_util::isConsonance(interval_to_bass)) continue;

    // Dissonant on strong beat: adjust to nearest consonant scale tone.
    uint8_t adjusted = findNearestConsonantScaleTone(
        note.pitch, bass_pitch, key.tonic, scale,
        getVoiceLowPitch(note.voice), getVoiceHighPitch(note.voice));
    if (adjusted > 0 && adjusted != note.pitch) {
      note.pitch = adjusted;
      note.modified_by |= static_cast<uint8_t>(NoteModifiedBy::ChordToneSnap);
      ++repairs;
    }
  }

  return repairs;
}

/// @brief Generate upper voice notes for a single variation at the appropriate
///        complexity stage.
///
/// Routes to one of five stage generators based on variation index and total
/// variation count:
///   - Variations 0-2:              Quarter note chord tones (Establish)
///   - Variations 3-5:              Eighth note scale passages (Develop early)
///   - Variations 6-8:              Eighth note arpeggios (Develop late)
///   - Middle variations (9 to N-2): Sixteenth note figurations (Accumulate)
///   - Final 1-2 variations:        Half/quarter notes (Resolve)
///
/// @param variation_idx Zero-based variation index.
/// @param num_variations Total number of variations.
/// @param start_tick Starting tick of this variation.
/// @param bars Number of bars in one ground bass cycle.
/// @param voice_idx Voice/track index.
/// @param timeline Harmonic timeline.
/// @param rng Random number generator.
/// @return Vector of NoteEvents for this voice in this variation.
std::vector<NoteEvent> generateVariationNotes(int variation_idx,
                                              int num_variations,
                                              Tick start_tick,
                                              int bars, uint8_t voice_idx,
                                              const HarmonicTimeline& timeline,
                                              std::mt19937& rng) {
  // Final 1-2 variations: Resolve (simplify rhythm for conclusion).
  // Only applies when there are enough variations for a meaningful arc.
  if (num_variations >= 6 && variation_idx >= num_variations - 2) {
    return generateResolveVariation(start_tick, bars, voice_idx, timeline, rng);
  }

  if (variation_idx < 3) {
    return generateEstablishVariation(start_tick, bars, voice_idx, timeline, rng);
  } else if (variation_idx < 6) {
    return generateDevelopEarlyVariation(start_tick, bars, voice_idx, timeline, rng);
  } else if (variation_idx < 9) {
    return generateDevelopLateVariation(start_tick, bars, voice_idx, timeline, rng);
  } else {
    return generateAccumulateVariation(start_tick, bars, voice_idx, timeline, rng);
  }
}

// ---------------------------------------------------------------------------
// Voice density arch — controls how many upper voices are active per variation
// ---------------------------------------------------------------------------

/// @brief Determine the target number of active upper voices for a given
///        variation, creating a density arch across the passacaglia.
///
/// The density arch follows the 5-stage structure:
///   - Establish (vars 0-2):    1-2 active upper voices (thin, expository)
///   - Develop Early (vars 3-5): gradual build to 2-3 upper voices
///   - Develop Late (vars 6-8):  3 upper voices (full development)
///   - Accumulate (vars 9-N-2):  all upper voices active (climactic density)
///   - Resolve (final 1-2):      reduce to 1-2 upper voices (denouement)
///
/// When total_upper_voices is 2 (3-voice passacaglia), the arch is flattened
/// since there are not enough voices to thin.
///
/// @param variation_idx Zero-based variation index.
/// @param num_variations Total number of variations.
/// @param total_upper_voices Total upper voice count (num_voices - 1, excluding bass).
/// @return Target number of active upper voices for this variation.
uint8_t getTargetActiveUpperVoices(int variation_idx, int num_variations,
                                   uint8_t total_upper_voices) {
  // With only 1 upper voice, arch is not meaningful.
  if (total_upper_voices <= 1) return total_upper_voices;

  // Resolve stage: final 1-2 variations.
  if (num_variations >= 6 && variation_idx >= num_variations - 2) {
    // Final variation: 1 voice (thin conclusion — bass + soprano only).
    if (variation_idx == num_variations - 1) {
      return 1;
    }
    // Penultimate variation: 2 voices (gradual reduction).
    return std::min(total_upper_voices, static_cast<uint8_t>(2));
  }

  // Establish stage: first 3 variations.
  if (variation_idx < 3) {
    // Var 0: solo soprano over bass (expose theme).
    if (variation_idx == 0) return 1;
    // Vars 1-2: 2 upper voices.
    return std::min(total_upper_voices, static_cast<uint8_t>(2));
  }

  // Develop Early: gradual build.
  if (variation_idx < 6) {
    // Var 3: 2 voices; Vars 4-5: build to 3 if available.
    if (variation_idx == 3) return std::min(total_upper_voices, static_cast<uint8_t>(2));
    return std::min(total_upper_voices, static_cast<uint8_t>(3));
  }

  // Develop Late: full 3-voice texture.
  if (variation_idx < 9) {
    return std::min(total_upper_voices, static_cast<uint8_t>(3));
  }

  // Accumulate: all voices active (climactic density).
  return total_upper_voices;
}

/// @brief Density level controlling within-voice note thinning.
///
/// Determines how aggressively notes within a voice are thinned to create
/// texture variation. Used in conjunction with thinVoiceNotes().
enum class VoiceDensityLevel : uint8_t {
  Full,          ///< No thinning — all generated notes kept.
  ModerateEven,  ///< Thin even-numbered bars (0, 2, 4...), keep odd bars.
  ModerateOdd,   ///< Thin odd-numbered bars (1, 3, 5...), keep even bars.
  Sparse,        ///< Active only on phrase boundaries (first 2 + last 2 bars).
};

/// @brief Get the density level for a specific voice within a variation.
///
/// Controls within-voice note thinning to create texture variation matching
/// the BWV 578 reference distribution (9% 1-voice, 24% 2-voice, 56% 3-voice,
/// 11% 4-voice). The dominant texture should be 3-voice, achieved by:
///   - Soprano thinned (Sparse) in var 0 to create 1-voice bass-only sections.
///   - Non-soprano voices thinned (Moderate) in most stages.
///   - Full density only in Accumulate (climax) stage.
///   - Develop Late: highest voice gets Moderate thinning to favor 3-voice.
///
/// @param voice_idx Voice index within the active voices.
/// @param target_active Total number of active upper voices this variation.
/// @param variation_idx Zero-based variation index.
/// @param num_variations Total number of variations.
/// @return VoiceDensityLevel for this voice in this variation.
VoiceDensityLevel getVoiceDensityLevel(uint8_t voice_idx, uint8_t target_active,
                                        int variation_idx, int num_variations) {
  // Resolve stage: thin all non-soprano voices aggressively.
  if (num_variations >= 6 && variation_idx >= num_variations - 2) {
    if (voice_idx == 0) return VoiceDensityLevel::ModerateEven;
    if (voice_idx >= 2) return VoiceDensityLevel::Sparse;
    return VoiceDensityLevel::ModerateOdd;
  }

  // Establish stage (vars 0-2):
  //   - Var 0: soprano gets Sparse (creates bass-only = 1-voice periods).
  //   - Vars 1-2: soprano full, second voice moderate.
  if (variation_idx < 3) {
    if (voice_idx == 0) {
      return (variation_idx == 0) ? VoiceDensityLevel::Sparse
                                  : VoiceDensityLevel::Full;
    }
    return VoiceDensityLevel::ModerateEven;
  }

  // Develop Early (vars 3-5): last active voice gets moderate thinning.
  if (variation_idx < 6) {
    if (voice_idx == 0) return VoiceDensityLevel::Full;
    if (voice_idx == target_active - 1 && target_active >= 2) {
      return VoiceDensityLevel::ModerateEven;
    }
    return VoiceDensityLevel::Full;
  }

  // Develop Late (vars 6-8): stagger thinning across voices so at most
  // one non-soprano voice rests per bar, keeping 3-voice as dominant texture.
  // Voice 1 (alto) thins even bars, voice 2 (tenor) thins odd bars.
  if (variation_idx < 9) {
    if (voice_idx == 0) return VoiceDensityLevel::Full;
    if (voice_idx % 2 == 1) return VoiceDensityLevel::ModerateEven;
    return VoiceDensityLevel::ModerateOdd;
  }

  // Accumulate: full density for climactic intensity.
  return VoiceDensityLevel::Full;
}

/// @brief Thin generated voice notes according to a density level.
///
/// Removes notes from specific bar positions to create texture variation:
///   - Full: no changes.
///   - ModerateEven: remove notes in even-numbered bars (0, 2, 4...).
///   - ModerateOdd: remove notes in odd-numbered bars (1, 3, 5...).
///   - Sparse: keep only notes in the first 2 bars and last 2 bars.
///
/// The ModerateEven/ModerateOdd split allows staggering across voices so
/// that different voices rest on different bars, maintaining 3-voice texture
/// as the dominant density.
///
/// Ground bass notes (Immutable) are never thinned.
///
/// @param notes The generated notes for one voice in one variation.
/// @param level Density level to apply.
/// @param var_start Start tick of this variation.
/// @param bars Number of bars per variation.
/// @return Thinned note vector.
std::vector<NoteEvent> thinVoiceNotes(const std::vector<NoteEvent>& notes,
                                       VoiceDensityLevel level,
                                       Tick var_start, int bars) {
  if (level == VoiceDensityLevel::Full) return notes;
  if (notes.empty()) return notes;

  std::vector<NoteEvent> result;
  result.reserve(notes.size());

  for (const auto& note : notes) {
    // Never thin immutable notes.
    if (note.source == BachNoteSource::GroundBass ||
        note.source == BachNoteSource::CantusFixed) {
      result.push_back(note);
      continue;
    }

    // Determine which bar within the variation this note falls in.
    int bar_in_var = static_cast<int>((note.start_tick - var_start) / kTicksPerBar);

    if (level == VoiceDensityLevel::ModerateEven) {
      // Remove even-numbered bars: rest / active / rest / active / ...
      if (bar_in_var % 2 == 0) continue;
    } else if (level == VoiceDensityLevel::ModerateOdd) {
      // Remove odd-numbered bars: active / rest / active / rest / ...
      if (bar_in_var % 2 == 1) continue;
    } else if (level == VoiceDensityLevel::Sparse) {
      // Keep only first 2 bars and last 2 bars.
      bool in_opening = (bar_in_var < 2);
      bool in_closing = (bar_in_var >= bars - 2);
      if (!in_opening && !in_closing) continue;
    }

    result.push_back(note);
  }

  return result;
}

/// @brief Clamp voice count to valid range [3, 5].
/// @param num_voices Raw voice count from configuration.
/// @return Clamped voice count.
uint8_t clampVoiceCount(uint8_t num_voices) {
  if (num_voices < 3) return 3;
  if (num_voices > 5) return 5;
  return num_voices;
}

/// Create a HarmonicTimeline derived from the ground bass pitches.
/// Each bass note maps to one bar of harmony, repeated for all variations.
/// @param ground_bass The immutable ground bass note sequence.
/// @param key Key signature.
/// @param num_variations Number of variations.
/// @return A HarmonicTimeline with bar-level resolution driven by the bass.
HarmonicTimeline createPassacagliaTimeline(
    const std::vector<NoteEvent>& ground_bass,
    const KeySignature& key,
    int num_variations) {
  HarmonicTimeline timeline;
  ScaleType scale_type = key.is_minor ? ScaleType::HarmonicMinor
                                      : ScaleType::Major;
  int bass_octave = 2;  // Bass pitch octave for chord construction.

  // Build one variation's worth of harmonic events from ground bass.
  std::vector<HarmonicEvent> var_template;
  var_template.reserve(ground_bass.size());

  for (const auto& bass_note : ground_bass) {
    int degree = 0;  // Default to tonic for non-diatonic pitches.
    scale_util::pitchToScaleDegree(bass_note.pitch, key.tonic, scale_type,
                                   degree);
    ChordDegree chord_degree = scaleDegreeToChordDegree(degree, key.is_minor);

    // Build chord from degree.
    Chord chord;
    chord.degree = chord_degree;
    chord.quality = key.is_minor ? minorKeyQuality(chord_degree)
                                 : majorKeyQuality(chord_degree);

    // Force V to Major quality in minor keys (harmonic minor convention).
    if (key.is_minor && chord_degree == ChordDegree::V) {
      chord.quality = ChordQuality::Major;
    }

    uint8_t semitone_offset = key.is_minor
                                  ? degreeMinorSemitones(chord_degree)
                                  : degreeSemitones(chord_degree);
    int root_midi = (bass_octave + 1) * 12 +
                    static_cast<int>(key.tonic) + semitone_offset;
    chord.root_pitch = static_cast<uint8_t>(
        root_midi > 127 ? 127 : (root_midi < 0 ? 0 : root_midi));

    HarmonicEvent event;
    event.tick = bass_note.start_tick;
    event.end_tick = bass_note.start_tick + bass_note.duration;
    event.key = key.tonic;
    event.is_minor = key.is_minor;
    event.chord = chord;
    event.bass_pitch = bass_note.pitch;
    event.weight = 1.0f;

    var_template.push_back(event);
  }

  // Replicate the template for each variation with time offset.
  Tick variation_duration = ground_bass.empty()
                                ? 0
                                : ground_bass.back().start_tick +
                                      ground_bass.back().duration;

  for (int var_idx = 0; var_idx < num_variations; ++var_idx) {
    Tick offset = static_cast<Tick>(var_idx) * variation_duration;
    for (const auto& tmpl : var_template) {
      HarmonicEvent event = tmpl;
      event.tick = tmpl.tick + offset;
      event.end_tick = tmpl.end_tick + offset;
      timeline.addEvent(event);
    }
  }

  return timeline;
}

}  // namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

std::vector<NoteEvent> generatePassacagliaGroundBass(const KeySignature& key,
                                                     int bars, uint32_t seed) {
  std::vector<NoteEvent> notes;

  if (bars <= 0) return notes;

  std::mt19937 rng(seed);

  int num_notes = bars;  // 1 whole note per bar.
  auto pitches = buildGroundBassPitches(key, num_notes, rng);

  Tick current_tick = 0;
  for (int idx = 0; idx < num_notes; ++idx) {
    NoteEvent note;
    note.start_tick = current_tick;
    note.duration = kWholeNote;
    note.pitch = pitches[static_cast<size_t>(idx)];
    note.velocity = kOrganVelocity;
    // TODO: ground bass voice index should be injected, not hardcoded.
    note.voice = 3;  // Pedal voice.
    note.source = BachNoteSource::GroundBass;
    notes.push_back(note);

    current_tick += kWholeNote;
  }

  return notes;
}

PassacagliaResult generatePassacaglia(const PassacagliaConfig& config) {
  PassacagliaResult result;
  result.success = false;

  // Validate configuration.
  if (config.num_variations <= 0 || config.ground_bass_bars <= 0) {
    result.error_message = "Invalid variation or bar count";
    return result;
  }

  uint8_t num_voices = clampVoiceCount(config.num_voices);
  std::mt19937 rng(config.seed);

  // Step 1: Generate immutable ground bass theme.
  std::vector<NoteEvent> ground_bass =
      generatePassacagliaGroundBass(config.key, config.ground_bass_bars, config.seed);

  if (ground_bass.empty()) {
    result.error_message = "Failed to generate ground bass";
    return result;
  }

  Tick variation_duration =
      static_cast<Tick>(config.ground_bass_bars) * kTicksPerBar;
  Tick total_duration =
      static_cast<Tick>(config.num_variations) * variation_duration;

  // Step 2: Create bass-driven harmonic timeline (1 chord per bar, derived
  // from ground bass pitches via scale degree → chord degree mapping).
  HarmonicTimeline timeline = createPassacagliaTimeline(
      ground_bass, config.key, config.num_variations);

  // Step 3: Create tracks.
  std::vector<Track> tracks = form_utils::createOrganTracks(num_voices);

  // Step 4: For each variation, place ground bass and generate upper voices
  // through createBachNote() for vertical coordination.
  uint8_t pedal_track_idx = static_cast<uint8_t>(num_voices - 1);

  // Shared counterpoint infrastructure (one evaluator/resolver for all variations).
  BachRuleEvaluator cp_rules(num_voices);
  cp_rules.setFreeCounterpoint(true);  // Allow weak-beat non-harmonic tones.
  CollisionResolver cp_resolver;
  cp_resolver.setHarmonicTimeline(&timeline);

  for (int var_idx = 0; var_idx < config.num_variations; ++var_idx) {
    Tick var_start = static_cast<Tick>(var_idx) * variation_duration;

    // Fresh CounterpointState per variation (resets voice interactions).
    CounterpointState cp_state;
    cp_state.setKey(config.key.tonic);
    for (uint8_t v = 0; v < num_voices; ++v) {
      cp_state.registerVoice(v, getVoiceLowPitch(v), getVoiceHighPitch(v));
    }

    // Place ground bass as immutable (register in cp_state for coordination).
    for (const auto& bass_note : ground_bass) {
      NoteEvent shifted_note = bass_note;
      shifted_note.start_tick = var_start + bass_note.start_tick;
      shifted_note.voice = pedal_track_idx;
      cp_state.addNote(pedal_track_idx, shifted_note);
      tracks[pedal_track_idx].notes.push_back(shifted_note);
    }

    // Determine target voice count for density arch.
    uint8_t upper_voice_count = num_voices - 1;  // Exclude pedal.
    uint8_t target_active = getTargetActiveUpperVoices(
        var_idx, config.num_variations, upper_voice_count);

    // Generate upper voices through createBachNote for vertical coordination.
    // Only generate notes for voices within the target active count.
    // Voice 0 (soprano) is always active; higher-numbered upper voices are
    // added and thinned as the density arch permits.
    for (uint8_t voice_idx = 0; voice_idx < num_voices - 1; ++voice_idx) {
      // Skip this voice if it exceeds the target active count.
      if (voice_idx >= target_active) continue;

      auto raw_notes = generateVariationNotes(
          var_idx, config.num_variations, var_start, config.ground_bass_bars,
          voice_idx, timeline, rng);

      // Apply within-voice density thinning based on variation stage.
      VoiceDensityLevel density_level = getVoiceDensityLevel(
          voice_idx, target_active, var_idx, config.num_variations);
      raw_notes = thinVoiceNotes(raw_notes, density_level, var_start,
                                 config.ground_bass_bars);

      uint8_t prev_pitch = 0;
      for (const auto& note : raw_notes) {
        BachNoteOptions opts;
        opts.voice = voice_idx;
        opts.desired_pitch = note.pitch;
        opts.tick = note.start_tick;
        opts.duration = note.duration;
        opts.velocity = note.velocity;
        opts.source = note.source;
        if (prev_pitch > 0) {
          opts.prev_pitches[0] = prev_pitch;
          opts.prev_count = 1;
        }

        auto result_note = createBachNote(&cp_state, &cp_rules, &cp_resolver,
                                          opts);
        if (result_note.accepted) {
          tracks[voice_idx].notes.push_back(result_note.note);
          prev_pitch = result_note.final_pitch;
        }
      }
    }
    // cp_state is destroyed at scope end (variation boundary reset).
  }

  // Step 4b: Strong-beat vertical consonance repair.
  // For each variation, check that upper-voice notes on strong beats form
  // consonant intervals with the ground bass. Adjust non-consonant pitches
  // to the nearest consonant scale tone. Ground bass is never modified.
  {
    // Collect all notes into a single vector for the repair pass.
    std::vector<NoteEvent> all_var_notes;
    for (const auto& track : tracks) {
      all_var_notes.insert(all_var_notes.end(), track.notes.begin(),
                           track.notes.end());
    }

    int consonance_repairs = repairStrongBeatConsonance(all_var_notes, config.key);

    if (consonance_repairs > 0) {
      // Redistribute repaired notes back to tracks.
      for (auto& track : tracks) {
        track.notes.clear();
      }
      for (auto& note : all_var_notes) {
        if (note.voice < num_voices) {
          tracks[note.voice].notes.push_back(std::move(note));
        }
      }
    }
  }

  // Step 5: Post-validate through counterpoint engine.
  if (num_voices >= 2) {
    // Collect all notes from all tracks.
    std::vector<NoteEvent> all_notes;
    for (const auto& track : tracks) {
      all_notes.insert(all_notes.end(), track.notes.begin(), track.notes.end());
    }

    // Build voice ranges from the hierarchical range functions.
    std::vector<std::pair<uint8_t, uint8_t>> voice_ranges;
    for (uint8_t v = 0; v < num_voices; ++v) {
      voice_ranges.emplace_back(getVoiceLowPitch(v), getVoiceHighPitch(v));
    }

    PostValidateStats stats;
    auto validated = postValidateNotes(
        std::move(all_notes), num_voices, config.key, voice_ranges, &stats);

    // Leap resolution: fix unresolved melodic leaps (contrary step rule).
    {
      LeapResolutionParams lr_params;
      lr_params.num_voices = num_voices;
      lr_params.key_at_tick = [&](Tick) { return config.key.tonic; };
      lr_params.scale_at_tick = [&](Tick) {
        return config.key.is_minor ? ScaleType::HarmonicMinor : ScaleType::Major;
      };
      lr_params.voice_range_static = [&](uint8_t v) -> std::pair<uint8_t, uint8_t> {
        return {getVoiceLowPitch(v), getVoiceHighPitch(v)};
      };
      lr_params.is_chord_tone = [&](Tick t, uint8_t p) {
        return isChordTone(p, timeline.getAt(t));
      };
      lr_params.vertical_safe =
          makeVerticalSafeWithParallelCheck(timeline, validated, num_voices);
      resolveLeaps(validated, lr_params);

      // Second parallel-perfect repair pass: fix parallels introduced by leap resolution.
      // Conservative safety net (max 2 iterations) -- primary defense is at generation time.
      {
        ParallelRepairParams pp_params;
        pp_params.num_voices = num_voices;
        pp_params.scale = config.key.is_minor ? ScaleType::HarmonicMinor : ScaleType::Major;
        pp_params.key_at_tick = lr_params.key_at_tick;
        pp_params.voice_range_static = lr_params.voice_range_static;
        pp_params.max_iterations = 2;
        repairParallelPerfect(validated, pp_params);
      }
    }

    // Repeated note repair: replace 4th+ same-pitch notes with step-adjacent
    // scale tones. Passacaglia uses fixed key (no modulation within a piece).
    {
      RepeatedNoteRepairParams repair_params;
      repair_params.max_consecutive = 2;
      repair_params.num_voices = num_voices;
      repair_params.key_at_tick = [&](Tick) { return config.key.tonic; };
      repair_params.scale_at_tick = [&](Tick) {
        return config.key.is_minor ? ScaleType::HarmonicMinor : ScaleType::Major;
      };
      repair_params.voice_range = [&](uint8_t v) -> std::pair<uint8_t, uint8_t> {
        return {getVoiceLowPitch(v), getVoiceHighPitch(v)};
      };
      // Repair in a loop until convergence: repairing a note can create a new
      // same-pitch boundary with the next note, requiring another pass.
      for (int pass = 0; pass < 5; ++pass) {
        int modified = repairRepeatedNotes(validated, repair_params);
        if (modified == 0) break;
      }
    }

    // Redistribute validated notes back to tracks.
    for (auto& track : tracks) {
      track.notes.clear();
    }
    for (auto& note : validated) {
      if (note.voice < num_voices) {
        tracks[note.voice].notes.push_back(std::move(note));
      }
    }
  }

  // Step 6: Sort notes within each track.
  form_utils::sortTrackNotes(tracks);

  // Step 7: Run pairwise counterpoint check and log violations as warnings.
  if (num_voices >= 2) {
    std::vector<NoteEvent> all_notes;
    for (const auto& track : tracks) {
      all_notes.insert(all_notes.end(), track.notes.begin(), track.notes.end());
    }
    auto cp_result = analyzeCounterpoint(all_notes, num_voices);
    if (cp_result.parallel_perfect_count > 0 ||
        cp_result.voice_crossing_count > 0) {
      result.counterpoint_violations =
          cp_result.parallel_perfect_count + cp_result.voice_crossing_count;
    }
  }

  // ---------------------------------------------------------------------------
  // Shared organ techniques: Picardy, variation registration (no pedal point
  // since GroundBass already serves as pedal foundation)
  // ---------------------------------------------------------------------------

  // Picardy third (minor keys only, final variation).
  if (config.enable_picardy && config.key.is_minor) {
    for (auto& track : tracks) {
      applyPicardyToFinalChord(track.notes, config.key,
                               total_duration - kTicksPerBar);
    }
  }

  // Variation registration plan (gradual crescendo).
  Tick var_dur = static_cast<Tick>(config.ground_bass_bars) * kTicksPerBar;
  auto reg_plan = createVariationRegistrationPlan(
      config.num_variations, var_dur);
  applyExtendedRegistrationPlan(tracks, reg_plan);

  // Final repeated-note repair pass: Picardy and other post-processing steps
  // may introduce new same-pitch runs after the main repair.
  if (num_voices >= 2) {
    std::vector<NoteEvent> final_notes;
    for (const auto& track : tracks) {
      final_notes.insert(final_notes.end(), track.notes.begin(),
                         track.notes.end());
    }

    RepeatedNoteRepairParams final_repair;
    final_repair.max_consecutive = 2;
    final_repair.num_voices = num_voices;
    final_repair.key_at_tick = [&](Tick) { return config.key.tonic; };
    final_repair.scale_at_tick = [&](Tick tick) {
      // Picardy region uses major scale for valid candidates.
      if (config.enable_picardy && config.key.is_minor &&
          tick >= total_duration - kTicksPerBar) {
        return ScaleType::Major;
      }
      return config.key.is_minor ? ScaleType::HarmonicMinor : ScaleType::Major;
    };
    final_repair.voice_range = [&](uint8_t v) -> std::pair<uint8_t, uint8_t> {
      return {getVoiceLowPitch(v), getVoiceHighPitch(v)};
    };

    for (int pass = 0; pass < 3; ++pass) {
      int modified = repairRepeatedNotes(final_notes, final_repair);
      if (modified == 0) break;
    }

    // Second parallel repair: catch violations introduced by post-processing
    // (repeated-note repair, Picardy third, registration changes).
    // Ground bass is ProtectionLevel::Immutable — never modified.
    {
      // Count melodic direction changes before repair for sanity check.
      auto countDirectionChanges = [](const std::vector<NoteEvent>& notes,
                                      uint8_t nv) -> int {
        std::vector<std::vector<uint8_t>> voice_pitches(nv);
        std::vector<std::vector<Tick>> voice_ticks(nv);
        for (const auto& note : notes) {
          if (note.voice < nv) {
            voice_pitches[note.voice].push_back(note.pitch);
            voice_ticks[note.voice].push_back(note.start_tick);
          }
        }
        int changes = 0;
        for (uint8_t vid = 0; vid < nv; ++vid) {
          auto& pitches = voice_pitches[vid];
          auto& ticks = voice_ticks[vid];
          if (pitches.size() < 3) continue;
          std::vector<size_t> order(pitches.size());
          for (size_t idx = 0; idx < order.size(); ++idx) order[idx] = idx;
          std::sort(order.begin(), order.end(),
                    [&ticks](size_t lhs, size_t rhs) {
                      return ticks[lhs] < ticks[rhs];
                    });
          int prev_dir = 0;
          for (size_t idx = 1; idx < order.size(); ++idx) {
            int diff = static_cast<int>(pitches[order[idx]]) -
                       static_cast<int>(pitches[order[idx - 1]]);
            int dir = (diff > 0) ? 1 : ((diff < 0) ? -1 : 0);
            if (dir != 0 && prev_dir != 0 && dir != prev_dir) ++changes;
            if (dir != 0) prev_dir = dir;
          }
        }
        return changes;
      };

      int dir_changes_before = countDirectionChanges(final_notes, num_voices);

      ParallelRepairParams pp2;
      pp2.num_voices = num_voices;
      pp2.scale = config.key.is_minor ? ScaleType::HarmonicMinor : ScaleType::Major;
      pp2.key_at_tick = [&](Tick) { return config.key.tonic; };
      pp2.voice_range_static = [&](uint8_t v) -> std::pair<uint8_t, uint8_t> {
        return {getVoiceLowPitch(v), getVoiceHighPitch(v)};
      };
      pp2.max_iterations = 2;
      repairParallelPerfect(final_notes, pp2);

      int dir_changes_after = countDirectionChanges(final_notes, num_voices);
      if (dir_changes_before > 0 &&
          dir_changes_after > dir_changes_before * 120 / 100) {
        fprintf(stderr,
                "[Passacaglia] WARNING: final parallel repair increased "
                "direction changes %d -> %d (+%.0f%%)\n",
                dir_changes_before, dir_changes_after,
                100.0f * (dir_changes_after - dir_changes_before) /
                    dir_changes_before);
      }
    }

    // Post-parallel repeated-note repair: the second parallel repair may have
    // moved pitches to avoid parallel 5ths/8ves, reintroducing same-pitch runs.
    // Reuse final_repair params (same key, scale, voice ranges).
    for (int pass = 0; pass < 3; ++pass) {
      int modified = repairRepeatedNotes(final_notes, final_repair);
      if (modified == 0) break;
    }

    // Redistribute repaired notes back to tracks.
    for (auto& track : tracks) {
      track.notes.clear();
    }
    for (auto& note : final_notes) {
      if (note.voice < num_voices) {
        tracks[note.voice].notes.push_back(std::move(note));
      }
    }
    form_utils::sortTrackNotes(tracks);
  }

  // Post-sort repeated-note repair: Sparse thinning can create cross-gap
  // triples where bars 0-1 end with pitch P and bars 6-7 resume with pitch P.
  // After thinning removes bars 2-5, these become consecutive in track output.
  // Use an extended gap threshold spanning a full variation to catch these.
  {
    Tick var_dur = static_cast<Tick>(config.ground_bass_bars) * kTicksPerBar;
    std::vector<NoteEvent> all_notes;
    for (const auto& track : tracks) {
      all_notes.insert(all_notes.end(), track.notes.begin(), track.notes.end());
    }

    RepeatedNoteRepairParams gap_repair;
    gap_repair.max_consecutive = 2;
    gap_repair.num_voices = num_voices;
    gap_repair.run_gap_threshold = var_dur;  // Cover full variation gaps.
    gap_repair.key_at_tick = [&](Tick) { return config.key.tonic; };
    gap_repair.scale_at_tick = [&](Tick tick) {
      if (config.enable_picardy && config.key.is_minor &&
          tick >= total_duration - kTicksPerBar) {
        return ScaleType::Major;
      }
      return config.key.is_minor ? ScaleType::HarmonicMinor : ScaleType::Major;
    };
    gap_repair.voice_range = [&](uint8_t v) -> std::pair<uint8_t, uint8_t> {
      return {getVoiceLowPitch(v), getVoiceHighPitch(v)};
    };

    bool any_modified = false;
    for (int pass = 0; pass < 3; ++pass) {
      int modified = repairRepeatedNotes(all_notes, gap_repair);
      if (modified == 0) break;
      any_modified = true;
    }

    if (any_modified) {
      for (auto& track : tracks) {
        track.notes.clear();
      }
      for (auto& note : all_notes) {
        if (note.voice < num_voices) {
          tracks[note.voice].notes.push_back(std::move(note));
        }
      }
      form_utils::sortTrackNotes(tracks);
    }
  }

  result.tracks = std::move(tracks);
  result.timeline = std::move(timeline);
  result.total_duration_ticks = total_duration;
  result.success = true;

  return result;
}

}  // namespace bach
