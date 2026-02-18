// 3-layer figuration engine for toccata generation.
// Header-only, consistent with figure_injector.h.
//
// Layer 1 (bottom): selectToccataPitch() -- Markov + harmonic scoring for bridge notes.
// Layer 2 (middle): generateFigurationSpan() -- figure injection loop over a time span.
// Layer 3 (top):    ToccataPhaseProfile table -- per-phase figure/density/contour config.

#ifndef BACH_FORMS_TOCCATA_FIGURATION_H
#define BACH_FORMS_TOCCATA_FIGURATION_H

#include <algorithm>
#include <cstdint>
#include <random>
#include <vector>

#include "core/bach_vocabulary.h"
#include "core/basic_types.h"
#include "core/figure_injector.h"
#include "core/markov_tables.h"
#include "core/melodic_state.h"
#include "core/note_source.h"
#include "core/pitch_utils.h"
#include "core/rng_util.h"
#include "core/scale.h"
#include "harmony/chord_types.h"
#include "harmony/harmonic_event.h"
#include "harmony/harmonic_tension.h"
#include "harmony/harmonic_timeline.h"

namespace bach {
namespace toccata_figuration {

// ---------------------------------------------------------------------------
// Layer 3: Phase profile table
// ---------------------------------------------------------------------------

/// @brief Per-phase figuration parameters for toccata generation.
///
/// Each ToccataPhaseProfile configures the figuration engine for one phase
/// of the Dramaticus archetype (8 phases A-H). Controls figure selection,
/// injection density, Markov bridge weighting, and voice ceilings.
struct ToccataPhaseProfile {
  const MelodicFigure* primary_figures[4];  ///< Figures available for injection.
  int primary_count;                        ///< Number of valid entries in primary_figures.
  float figure_density;         ///< Probability of figure injection per beat (0.0-1.0).
  float markov_bridge_weight;   ///< Markov weight for bridge notes ONLY, always < 0.4.
  float harmonic_tension_gate;  ///< Minimum harmonic tension for 32nd injection.
  PhraseContour::Shape contour; ///< Phrase-level directional contour shape.
  bool free_rhythm;             ///< If true, disable uniform durations (for recitative).
  uint8_t voice_ceiling[2];    ///< Max pitch per voice (v0 Great, v1 Swell).
  float max_32nd_prob;          ///< 32nd probability cap (G/H=0.50, D/F=0.15, others=0.0).
};

namespace detail {

// Dramaticus archetype phase profiles (8 phases A-H).
// NOLINT(cert-err58-cpp): static constexpr arrays are safe here.
inline constexpr ToccataPhaseProfile kDramaticusProfiles[8] = {
    // Phase A: Gesture -- dramatic descending runs and trills.
    {{&kDescRun5, &kTrill5, &kBrechungDesc, nullptr},
     3, 0.60f, 0.30f, 0.3f, PhraseContour::Descent, false, {84, 72}, 0.0f},

    // Phase B: Echo -- quiet lower neighbor oscillation.
    {{&kLowerNbr, &kCambiataDown, nullptr, nullptr},
     2, 0.20f, 0.25f, 1.0f, PhraseContour::Neutral, false, {84, 72}, 0.0f},

    // Phase C: Recitative -- free declamatory style with turns and ornaments.
    {{&kTurnDown, &kEchappee, &kCambiataNbr, &kTurnUpNbr},
     4, 0.35f, 0.20f, 0.8f, PhraseContour::Wave, true, {86, 74}, 0.0f},

    // Phase D: Climb1 -- ascending sequential development.
    {{&kAscRun5, &kTurnUp, &kStepDownLeapUp, nullptr},
     3, 0.50f, 0.30f, 0.5f, PhraseContour::Arch, false, {89, 77}, 0.15f},

    // Phase E: Break -- harmonic interruption with wide oscillation.
    {{&kWideOsc5, &kLowerNbr, nullptr, nullptr},
     2, 0.25f, 0.25f, 1.0f, PhraseContour::Neutral, false, {84, 72}, 0.0f},

    // Phase F: Climb2 -- intensified ascending motion.
    {{&kAscRun5, &kAscRun4, &kLeapUpStepDown, nullptr},
     3, 0.55f, 0.35f, 0.4f, PhraseContour::Arch, false, {93, 81}, 0.15f},

    // Phase G: Dominant Obsession -- dominant prolongation with chromaticism.
    {{&kDescRun5, &kChromaticDesc, &kTrill5, &kDescRun4},
     4, 0.60f, 0.35f, 0.2f, PhraseContour::Descent, false, {96, 84}, 0.50f},

    // Phase H: Final Explosion -- climactic descending gestures.
    {{&kBrechungDesc, &kWideOsc5, &kDescRun5, nullptr},
     3, 0.70f, 0.35f, 0.15f, PhraseContour::Descent, false, {96, 84}, 0.50f},
};

}  // namespace detail

/// @brief Get the Dramaticus archetype phase profile for a given phase index.
/// @param phase_idx Phase index (0-7, corresponding to phases A-H).
/// @return Reference to the phase profile. Out-of-range indices are clamped.
inline const ToccataPhaseProfile& getDramaticusPhaseProfile(int phase_idx) {
  int clamped = std::clamp(phase_idx, 0, 7);
  return detail::kDramaticusProfiles[clamped];
}

// ---------------------------------------------------------------------------
// 32nd-note probability
// ---------------------------------------------------------------------------

/// @brief Compute probability of using 32nd-note rhythm cells.
///
/// Combines energy level and harmonic tension, gated by the phase's tension
/// threshold. Returns 0.0 when conditions are calm, up to 0.5 at maximum
/// energy and tension.
///
/// @param energy Current energy level [0.0, 1.0].
/// @param harmonic_tension Harmonic tension from computeHarmonicTension().
/// @param tension_gate Phase-specific minimum tension threshold.
/// @return Probability in [0.0, 0.5].
inline float compute32ndProbability(float energy, float harmonic_tension,
                                    float tension_gate) {
  return std::clamp(energy * harmonic_tension - tension_gate, 0.0f, 0.5f);
}

// ---------------------------------------------------------------------------
// Context and result structs
// ---------------------------------------------------------------------------

/// @brief Context for figuration generation, carrying state across a span.
struct ToccataFigurationContext {
  const ToccataPhaseProfile* profile;   ///< Current phase profile.
  const MarkovModel* markov_model;      ///< Markov model (typically kToccataUpperMarkov).
  Key key;                              ///< Current musical key.
  ScaleType scale;                      ///< Current scale type.
  uint8_t low_pitch;                    ///< Voice range lower bound (MIDI).
  uint8_t high_pitch;                   ///< Voice range upper bound (MIDI).
  float energy;                         ///< Current energy level [0.0, 1.0].
  MelodicState mel_state;              ///< Melodic state for scoring and direction.
  DegreeStep prev_degree_step;          ///< Previous degree step for Markov context.
  uint8_t initial_pitch = 0;            ///< Starting pitch hint (0 = use range midpoint).
};

/// @brief Result of a figuration span generation.
struct FigurationResult {
  std::vector<NoteEvent> notes;  ///< Generated notes.
  Tick end_tick;                 ///< Tick position after the last generated note.
};

// ---------------------------------------------------------------------------
// Helper: DegreeClass from pitch
// ---------------------------------------------------------------------------

/// @brief Classify a MIDI pitch into a scale degree class.
/// @param pitch MIDI note number.
/// @param key Musical key.
/// @param scale Scale type.
/// @return DegreeClass (Stable, Dominant, or Motion).
inline DegreeClass pitchToDegreeClass(uint8_t pitch, Key key, ScaleType scale) {
  int deg = scale_util::pitchToAbsoluteDegree(pitch, key, scale) % 7;
  if (deg == 0 || deg == 2) return DegreeClass::Stable;
  if (deg == 4 || deg == 6) return DegreeClass::Dominant;
  return DegreeClass::Motion;
}

// ---------------------------------------------------------------------------
// Helper: NoteEvent creation
// ---------------------------------------------------------------------------

/// @brief Create a NoteEvent with organ velocity for toccata figuration.
/// @param tick Start tick.
/// @param dur Duration in ticks.
/// @param pitch MIDI pitch.
/// @param voice Voice index.
/// @param source Provenance source tag.
/// @return Configured NoteEvent.
inline NoteEvent makeFigNote(Tick tick, Tick dur, uint8_t pitch, uint8_t voice,
                             BachNoteSource source) {
  NoteEvent note;
  note.start_tick = tick;
  note.duration = dur;
  note.pitch = pitch;
  note.velocity = 80;  // kOrganVelocity (pipe organ fixed velocity).
  note.voice = voice;
  note.source = source;
  return note;
}

// ---------------------------------------------------------------------------
// Helper: Brechung chord-tone snapping utilities
// ---------------------------------------------------------------------------

namespace detail {

/// @brief Find the closest chord tone to a target pitch, preserving melodic direction.
///
/// When two candidates are equidistant, prefers the one matching the original
/// melodic direction (ascending or descending from prev_pitch to original_pitch).
/// Falls back to the lower pitch on true ties with no directional preference.
///
/// @param original_pitch The pitch before snapping (used for distance calculation).
/// @param prev_pitch The preceding pitch in the figure (for contour direction).
/// @param chord_tones Non-empty vector of chord tone MIDI pitches in range.
/// @return Closest chord tone with direction preference.
inline uint8_t findClosestChordTone(uint8_t original_pitch, uint8_t prev_pitch,
                                    const std::vector<uint8_t>& chord_tones) {
  if (chord_tones.empty()) return original_pitch;
  if (chord_tones.size() == 1) return chord_tones[0];

  int direction = static_cast<int>(original_pitch) - static_cast<int>(prev_pitch);
  // direction > 0 means ascending, < 0 descending, == 0 neutral.

  uint8_t best = chord_tones[0];
  int best_dist = std::abs(static_cast<int>(chord_tones[0])
                           - static_cast<int>(original_pitch));

  for (size_t idx = 1; idx < chord_tones.size(); ++idx) {
    int dist = std::abs(static_cast<int>(chord_tones[idx])
                        - static_cast<int>(original_pitch));
    if (dist < best_dist) {
      best = chord_tones[idx];
      best_dist = dist;
    } else if (dist == best_dist) {
      // Tie-break: prefer the candidate matching the melodic direction.
      int cand_dir = static_cast<int>(chord_tones[idx])
                     - static_cast<int>(original_pitch);
      int best_dir = static_cast<int>(best) - static_cast<int>(original_pitch);
      bool cand_matches = (direction > 0 && cand_dir > 0) ||
                          (direction < 0 && cand_dir < 0);
      bool best_matches = (direction > 0 && best_dir > 0) ||
                          (direction < 0 && best_dir < 0);
      if (cand_matches && !best_matches) {
        best = chord_tones[idx];
        best_dist = dist;
      }
    }
  }
  return best;
}

/// @brief Check if a MIDI pitch is among the collected chord tones.
/// @param pitch MIDI pitch to check.
/// @param chord_tones Vector of chord tone MIDI pitches.
/// @return True if pitch matches any entry in chord_tones.
inline bool isPitchInChordTones(uint8_t pitch,
                                const std::vector<uint8_t>& chord_tones) {
  return std::find(chord_tones.begin(), chord_tones.end(), pitch)
         != chord_tones.end();
}

}  // namespace detail

// ---------------------------------------------------------------------------
// Layer 1: selectToccataPitch()
// ---------------------------------------------------------------------------

/// @brief Select a pitch for a bridge note using Markov model and harmonic scoring.
///
/// Retrieves top Markov candidates, scores each with the soprano voice profile,
/// blends Markov probability with melodic scoring, and adds chord attraction
/// bonus on dominant harmonies. Final selection is weighted random.
///
/// @param ctx Figuration context (carries Markov model, key, scale, mel_state).
/// @param prev_pitch Previous MIDI pitch (for interval computation).
/// @param harm Current harmonic event (for chord tone attraction).
/// @param tick Current tick position (for beat-position scoring).
/// @param rng Random number generator.
/// @return Selected MIDI pitch.
inline uint8_t selectToccataPitch(
    ToccataFigurationContext& ctx,
    uint8_t prev_pitch, const HarmonicEvent& harm,
    Tick tick, std::mt19937& rng) {
  // Effective ceiling from profile.
  uint8_t effective_high = ctx.high_pitch;
  if (ctx.profile != nullptr) {
    // Voice 0 ceiling used as default (conservative).
    effective_high = std::min(ctx.high_pitch, ctx.profile->voice_ceiling[0]);
  }

  // Get top 6 Markov candidates.
  constexpr int kMaxCandidates = 6;
  OracleCandidate candidates[kMaxCandidates];
  DegreeClass deg_class = pitchToDegreeClass(prev_pitch, ctx.key, ctx.scale);
  BeatPos beat = tickToBeatPos(tick);

  int num_candidates = getTopMelodicCandidates(
      *ctx.markov_model, ctx.prev_degree_step, deg_class, beat,
      prev_pitch, ctx.key, ctx.scale,
      ctx.low_pitch, effective_high,
      candidates, kMaxCandidates);

  if (num_candidates == 0) {
    // Fallback: return previous pitch.
    return prev_pitch;
  }

  // Score each candidate with soprano profile, blending with Markov weight.
  float bridge_weight = 0.30f;
  if (ctx.profile != nullptr) {
    bridge_weight = ctx.profile->markov_bridge_weight;
  }

  // Chord attraction bonus for dominant harmonies.
  bool is_dominant = (harm.chord.degree == ChordDegree::V ||
                      harm.chord.quality == ChordQuality::Dominant7);
  constexpr float kChordAttractionBonus = 0.25f;

  std::vector<uint8_t> pitches;
  std::vector<float> weights;
  pitches.reserve(num_candidates);
  weights.reserve(num_candidates);

  for (int idx = 0; idx < num_candidates; ++idx) {
    uint8_t cand_pitch = candidates[idx].pitch;
    bool is_chord = isChordTone(cand_pitch, harm);

    // Melodic score from voice profile.
    float melodic_score = scoreCandidatePitch(
        ctx.mel_state, prev_pitch, cand_pitch, tick,
        is_chord, voice_profiles::kSoprano);

    // Blend Markov probability with melodic score.
    float markov_score = candidates[idx].prob;
    float blended = (1.0f - bridge_weight) * melodic_score
                    + bridge_weight * markov_score;

    // Chord attraction on dominant harmonies.
    if (is_dominant && is_chord) {
      blended += kChordAttractionBonus;
    }

    pitches.push_back(cand_pitch);
    weights.push_back(std::max(0.01f, blended + 1.0f));  // Shift to positive.
  }

  return rng::selectWeighted(rng, pitches, weights);
}

// ---------------------------------------------------------------------------
// Free rhythm duration table (for recitative phases)
// ---------------------------------------------------------------------------

namespace detail {

/// @brief Asymmetric duration table for free-rhythm (recitative) phases.
/// No 32nds; weighted toward 8th and quarter note values.
/// Index: cumulative probability boundary.
/// Returns duration in ticks.
inline Tick selectFreeRhythmDuration(std::mt19937& rng) {
  float roll = rng::rollFloat(rng, 0.0f, 1.0f);
  if (roll < 0.15f) return duration::kSixteenthNote;  // 15% 16th (120 ticks).
  if (roll < 0.40f) return duration::kEighthNote;      // 25% 8th (240 ticks).
  if (roll < 0.65f) return duration::kQuarterNote;     // 25% quarter (480 ticks).
  if (roll < 0.80f) return duration::kDottedQuarter;   // 15% dotted quarter (720 ticks).
  if (roll < 0.92f) return duration::kHalfNote;        // 12% half (960 ticks).
  return duration::kWholeNote;                          // 8% whole (1920 ticks).
}

}  // namespace detail

// ---------------------------------------------------------------------------
// Layer 2: generateFigurationSpan()
// ---------------------------------------------------------------------------

/// @brief Generate figuration notes across a time span.
///
/// The main generation loop. For each beat position within [tick, end_tick):
/// 1. Rolls figure_density to decide figure injection vs bridge note.
/// 2. On figure hit: selects from profile's primary_figures, resolves intervals,
///    validates range, and emits tagged notes (BachNoteSource::ToccataFigure).
/// 3. On miss: emits a Markov-scored bridge note (BachNoteSource::FreeCounterpoint).
/// 4. Tracks consecutive bridge notes, forcing a figure attempt after 2 bridges.
///
/// @param ctx Figuration context (modified: mel_state and prev_degree_step updated).
/// @param tick Start tick of the span.
/// @param end_tick End tick of the span (exclusive).
/// @param voice Voice index for generated notes.
/// @param timeline Harmonic timeline for chord context lookup.
/// @param rng Random number generator.
/// @return FigurationResult with generated notes and final tick position.
inline FigurationResult generateFigurationSpan(
    ToccataFigurationContext& ctx,
    Tick tick, Tick end_tick, uint8_t voice,
    const HarmonicTimeline& timeline,
    std::mt19937& rng) {
  FigurationResult result;
  result.end_tick = tick;

  if (ctx.profile == nullptr || ctx.markov_model == nullptr) {
    return result;
  }

  const ToccataPhaseProfile& profile = *ctx.profile;

  // --- Change 2: Per-span v1 ceiling fluctuation (+/-2 semitones, 20%/20%/60%).
  // Disabled in phases G/H (detected by max_32nd_prob > 0.30).
  uint8_t adjusted_ceiling = profile.voice_ceiling[voice < 2 ? voice : 0];
  if (voice == 1 && profile.max_32nd_prob <= 0.30f) {
    float ceil_roll = rng::rollFloat(rng, 0.0f, 1.0f);
    if (ceil_roll < 0.20f) {
      adjusted_ceiling = static_cast<uint8_t>(
          std::min(127, static_cast<int>(adjusted_ceiling) + 2));
    } else if (ceil_roll < 0.40f) {
      adjusted_ceiling = static_cast<uint8_t>(
          std::max(0, static_cast<int>(adjusted_ceiling) - 2));
    }
    // else: 60% no change.
  }

  // Effective pitch ceiling per voice.
  uint8_t effective_high = std::min(ctx.high_pitch, adjusted_ceiling);

  // --- Change 3: Phase-based figure_density cap.
  // Phases A-F (max_32nd_prob <= 0.30): cap at 0.55.
  // Phase C (Recit, detected by free_rhythm): cap at 0.40.
  // Phases G/H (max_32nd_prob > 0.30): no additional cap.
  float effective_density = profile.figure_density;
  if (profile.max_32nd_prob <= 0.30f) {
    float cap = profile.free_rhythm ? 0.40f : 0.55f;
    effective_density = std::min(effective_density, cap);
  }

  // Track consecutive bridge notes (force figure after 2).
  int consecutive_bridges = 0;

  // Base duration for non-free-rhythm: 16th note.
  constexpr Tick kBase16thDuration = duration::kSixteenthNote;  // 120 ticks.
  constexpr Tick k32ndDuration = duration::kThirtySecondNote;    // 60 ticks.

  // Previous pitch for interval tracking.
  // Use initial_pitch hint if provided, otherwise start from middle of range.
  uint8_t prev_pitch = 0;
  if (!result.notes.empty()) {
    prev_pitch = result.notes.back().pitch;
  } else if (ctx.initial_pitch > 0 &&
             ctx.initial_pitch >= ctx.low_pitch &&
             ctx.initial_pitch <= effective_high) {
    prev_pitch = ctx.initial_pitch;
  } else {
    prev_pitch = static_cast<uint8_t>((ctx.low_pitch + effective_high) / 2);
    prev_pitch = scale_util::nearestScaleTone(prev_pitch, ctx.key, ctx.scale);
  }

  Tick current_tick = tick;
  while (current_tick < end_tick) {
    const HarmonicEvent& harm = timeline.getAt(current_tick);
    float tension = computeHarmonicTension(harm);

    // Determine whether to attempt figure injection.
    bool try_figure = rng::rollProbability(rng, effective_density);

    // Force figure attempt after 2 consecutive bridge notes.
    if (consecutive_bridges >= 2) {
      try_figure = true;
    }

    bool figure_emitted = false;

    if (try_figure && profile.primary_count > 0) {
      // Select a random figure from the profile's primary figures.
      int fig_idx = rng::rollRange(rng, 0, profile.primary_count - 1);
      const MelodicFigure* figure = profile.primary_figures[fig_idx];

      if (figure != nullptr) {
        // Determine note duration for figure notes.
        Tick fig_note_dur = kBase16thDuration;
        if (profile.free_rhythm) {
          fig_note_dur = detail::selectFreeRhythmDuration(rng);
        } else {
          // Check for 32nd-note upgrade, capped by profile max_32nd_prob.
          float prob_32nd = compute32ndProbability(ctx.energy, tension,
                                                   profile.harmonic_tension_gate);
          prob_32nd = std::min(prob_32nd, profile.max_32nd_prob);
          if (rng::rollProbability(rng, prob_32nd)) {
            fig_note_dur = k32ndDuration;
          }
        }

        // Check if figure fits within remaining span.
        Tick figure_total = fig_note_dur * figure->note_count;
        if (current_tick + figure_total <= end_tick) {
          // Resolve figure pitches.
          bool is_semitone_mode = (figure->primary_mode == IntervalMode::Semitone);
          std::vector<uint8_t> fig_pitches;
          fig_pitches.reserve(figure->note_count);
          fig_pitches.push_back(prev_pitch);

          bool valid = true;
          for (int pidx = 0; pidx < figure->note_count - 1; ++pidx) {
            uint8_t next_pitch = 0;

            if (is_semitone_mode && figure->semitone_intervals != nullptr) {
              // Semitone mode: raw semitone offsets (trills, wide oscillations).
              int raw = static_cast<int>(fig_pitches.back())
                        + figure->semitone_intervals[pidx];
              if (raw < 0 || raw > 127) { valid = false; break; }
              next_pitch = static_cast<uint8_t>(raw);
            } else if (figure->degree_intervals != nullptr) {
              // Degree mode: resolve via scale.
              next_pitch = resolveDegreeInterval(
                  fig_pitches.back(), figure->degree_intervals[pidx],
                  ctx.key, ctx.scale);
              if (next_pitch == 0) { valid = false; break; }
            } else {
              valid = false;
              break;
            }

            // Validate range.
            if (next_pitch < ctx.low_pitch || next_pitch > effective_high) {
              valid = false;
              break;
            }

            fig_pitches.push_back(next_pitch);
          }

          if (valid && fig_pitches.size() == figure->note_count) {
            // Strong-beat chord tone snap for all figure types.
            // Figure classification determines snap threshold:
            //   - Pure diatonic stepwise / chromatic run: Bar only
            //   - Leap/semitone mode figures: Beat or higher
            bool is_diatonic_run = false;
            bool is_chromatic_run = false;
            if (figure->degree_intervals != nullptr) {
              bool all_stepwise = true;
              bool has_chroma = false;
              for (int pidx = 0;
                   pidx < static_cast<int>(figure->note_count) - 1; ++pidx) {
                if (std::abs(figure->degree_intervals[pidx].degree_diff) > 1) {
                  all_stepwise = false;
                  break;
                }
                if (figure->degree_intervals[pidx].chroma_offset != 0) {
                  has_chroma = true;
                }
              }
              if (all_stepwise) {
                is_diatonic_run = !has_chroma;
                is_chromatic_run = has_chroma;
              }
            }
            bool is_any_run = is_diatonic_run || is_chromatic_run;
            MetricLevel snap_threshold =
                is_any_run ? MetricLevel::Bar : MetricLevel::Beat;
            auto chord_tones = collectChordTonesInRange(
                harm.chord, ctx.low_pitch, effective_high);
            if (!chord_tones.empty()) {
              constexpr int kLargeLeapThreshold = 7;  // > perfect 5th
              int resolved_count = static_cast<int>(fig_pitches.size());
              for (int idx = 0; idx < resolved_count; ++idx) {
                Tick note_tick = current_tick
                                 + static_cast<Tick>(idx) * fig_note_dur;
                if (metricLevel(note_tick) >= snap_threshold) {
                  if (!detail::isPitchInChordTones(
                          fig_pitches[idx], chord_tones)) {
                    // NCT resolution tolerance: allow appoggiatura / retard.
                    bool has_resolution = false;
                    if (idx + 1 < resolved_count) {
                      int step = std::abs(
                          static_cast<int>(fig_pitches[idx + 1])
                          - static_cast<int>(fig_pitches[idx]));
                      if (step <= 2 && detail::isPitchInChordTones(
                              fig_pitches[idx + 1], chord_tones)) {
                        has_resolution = true;
                      }
                      if (!has_resolution && step == 0
                          && idx + 2 < resolved_count) {
                        int step2 = std::abs(
                            static_cast<int>(fig_pitches[idx + 2])
                            - static_cast<int>(fig_pitches[idx]));
                        has_resolution = (step2 <= 2)
                            && detail::isPitchInChordTones(
                                fig_pitches[idx + 2], chord_tones);
                      }
                    }
                    if (!has_resolution) {
                      uint8_t ref_pitch = (idx > 0)
                          ? fig_pitches[idx - 1]
                          : prev_pitch;
                      uint8_t snapped = detail::findClosestChordTone(
                          fig_pitches[idx], ref_pitch, chord_tones);
                      fig_pitches[idx] = snapped;
                      // Suppress large leap to the next note after snapping.
                      if (idx + 1 < resolved_count) {
                        int leap = std::abs(
                            static_cast<int>(fig_pitches[idx + 1])
                            - static_cast<int>(snapped));
                        if (leap > kLargeLeapThreshold) {
                          fig_pitches[idx + 1] =
                              detail::findClosestChordTone(
                                  fig_pitches[idx + 1], snapped,
                                  chord_tones);
                        }
                      }
                    }
                  }
                }
              }
            }

            // Emit figure notes.
            for (size_t nidx = 0; nidx < fig_pitches.size(); ++nidx) {
              result.notes.push_back(makeFigNote(
                  current_tick + static_cast<Tick>(nidx) * fig_note_dur,
                  fig_note_dur, fig_pitches[nidx], voice,
                  BachNoteSource::ToccataFigure));
            }

            // Update melodic state through the figure.
            for (size_t nidx = 1; nidx < fig_pitches.size(); ++nidx) {
              updateMelodicState(ctx.mel_state,
                                 fig_pitches[nidx - 1], fig_pitches[nidx]);
            }

            // Update degree step for Markov context.
            if (fig_pitches.size() >= 2) {
              ctx.prev_degree_step = computeDegreeStep(
                  fig_pitches[fig_pitches.size() - 2],
                  fig_pitches[fig_pitches.size() - 1],
                  ctx.key, ctx.scale);
            }

            prev_pitch = fig_pitches.back();
            current_tick += figure_total;
            consecutive_bridges = 0;
            figure_emitted = true;
          }
        }
      }
    }

    if (!figure_emitted) {
      // Emit a bridge note using Markov model + melodic scoring.
      uint8_t bridge_pitch = selectToccataPitch(
          ctx, prev_pitch, harm, current_tick, rng);

      // Strong-beat chord tone enforcement for bridge notes.
      {
        MetricLevel ml = metricLevel(current_tick);
        if (ml >= MetricLevel::Beat) {
          auto bridge_cts = collectChordTonesInRange(
              harm.chord, ctx.low_pitch, effective_high);
          if (!bridge_cts.empty() &&
              !detail::isPitchInChordTones(bridge_pitch, bridge_cts)) {
            uint8_t snapped = detail::findClosestChordTone(
                bridge_pitch, prev_pitch, bridge_cts);
            int snap_dist = std::abs(
                static_cast<int>(snapped) - static_cast<int>(bridge_pitch));
            // Bar: unconditional snap. Beat: snap only within 2 semitones.
            if (ml == MetricLevel::Bar || snap_dist <= 2) {
              bridge_pitch = snapped;
            }
          }
        }
      }

      // Determine bridge note duration.
      Tick bridge_dur = kBase16thDuration;
      if (profile.free_rhythm) {
        bridge_dur = detail::selectFreeRhythmDuration(rng);
      } else {
        float prob_32nd = compute32ndProbability(ctx.energy, tension,
                                                 profile.harmonic_tension_gate);
        prob_32nd = std::min(prob_32nd, profile.max_32nd_prob);
        if (rng::rollProbability(rng, prob_32nd)) {
          bridge_dur = k32ndDuration;
        }
      }

      // Clamp duration to not exceed span end.
      if (current_tick + bridge_dur > end_tick) {
        bridge_dur = end_tick - current_tick;
      }
      if (bridge_dur == 0) break;

      result.notes.push_back(makeFigNote(
          current_tick, bridge_dur, bridge_pitch, voice,
          BachNoteSource::FreeCounterpoint));

      // Update state.
      updateMelodicState(ctx.mel_state, prev_pitch, bridge_pitch);
      ctx.prev_degree_step = computeDegreeStep(
          prev_pitch, bridge_pitch, ctx.key, ctx.scale);

      prev_pitch = bridge_pitch;
      current_tick += bridge_dur;
      consecutive_bridges++;
    }
  }

  result.end_tick = current_tick;
  return result;
}

}  // namespace toccata_figuration
}  // namespace bach

#endif  // BACH_FORMS_TOCCATA_FIGURATION_H
