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
};

namespace detail {

// Dramaticus archetype phase profiles (8 phases A-H).
// NOLINT(cert-err58-cpp): static constexpr arrays are safe here.
inline constexpr ToccataPhaseProfile kDramaticusProfiles[8] = {
    // Phase A: Gesture -- dramatic descending runs and trills.
    {{&kDescRun5, &kTrill5, &kBrechungDesc, nullptr},
     3, 0.60f, 0.30f, 0.3f, PhraseContour::Descent, false, {84, 72}},

    // Phase B: Echo -- quiet lower neighbor oscillation.
    {{&kLowerNbr, &kCambiataDown, nullptr, nullptr},
     2, 0.20f, 0.25f, 1.0f, PhraseContour::Neutral, false, {84, 72}},

    // Phase C: Recitative -- free declamatory style with turns and ornaments.
    {{&kTurnDown, &kEchappee, &kCambiataNbr, &kTurnUpNbr},
     4, 0.35f, 0.20f, 0.8f, PhraseContour::Wave, true, {86, 74}},

    // Phase D: Climb1 -- ascending sequential development.
    {{&kAscRun5, &kTurnUp, &kStepDownLeapUp, nullptr},
     3, 0.50f, 0.30f, 0.5f, PhraseContour::Arch, false, {89, 77}},

    // Phase E: Break -- harmonic interruption with wide oscillation.
    {{&kWideOsc5, &kLowerNbr, nullptr, nullptr},
     2, 0.25f, 0.25f, 1.0f, PhraseContour::Neutral, false, {84, 72}},

    // Phase F: Climb2 -- intensified ascending motion.
    {{&kAscRun5, &kAscRun4, &kLeapUpStepDown, nullptr},
     3, 0.55f, 0.35f, 0.4f, PhraseContour::Arch, false, {93, 81}},

    // Phase G: Dominant Obsession -- dominant prolongation with chromaticism.
    {{&kDescRun5, &kChromaticDesc, &kTrill5, &kDescRun4},
     4, 0.60f, 0.35f, 0.2f, PhraseContour::Descent, false, {96, 84}},

    // Phase H: Final Explosion -- climactic descending gestures.
    {{&kBrechungDesc, &kWideOsc5, &kDescRun5, nullptr},
     3, 0.70f, 0.35f, 0.15f, PhraseContour::Descent, false, {96, 84}},
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
// Harmonic tension computation
// ---------------------------------------------------------------------------

/// @brief Compute harmonic tension level from a harmonic event.
///
/// Maps chord degree and quality to a tension value in [0.0, 1.0].
/// Tonic/stable chords return low tension; dominant and diminished chords
/// return high tension. Used as a gate for 32nd-note injection.
///
/// @param harm The harmonic event to evaluate.
/// @return Tension level in [0.0, 1.0].
inline float computeHarmonicTension(const HarmonicEvent& harm) {
  ChordDegree deg = harm.chord.degree;
  ChordQuality qual = harm.chord.quality;

  switch (deg) {
    case ChordDegree::I:
      return 0.0f;
    case ChordDegree::vi:
      return 0.0f;
    case ChordDegree::iii:
      return 0.2f;
    case ChordDegree::IV:
      return 0.3f;
    case ChordDegree::ii:
      return 0.3f;
    case ChordDegree::V:
      if (qual == ChordQuality::Dominant7) return 0.8f;
      return 0.6f;
    case ChordDegree::V_of_V:
    case ChordDegree::V_of_vi:
    case ChordDegree::V_of_IV:
    case ChordDegree::V_of_ii:
    case ChordDegree::V_of_iii:
      return 0.7f;
    case ChordDegree::viiDim:
      if (qual == ChordQuality::Diminished7) return 1.0f;
      return 0.9f;
    case ChordDegree::bII:
      return 0.6f;
    case ChordDegree::bVI:
    case ChordDegree::bVII:
    case ChordDegree::bIII:
      return 0.4f;
    default:
      return 0.5f;
  }
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

  // Effective pitch ceiling per voice.
  uint8_t effective_high = std::min(
      ctx.high_pitch,
      profile.voice_ceiling[voice < 2 ? voice : 0]);

  // Track consecutive bridge notes (force figure after 2).
  int consecutive_bridges = 0;

  // Base duration for non-free-rhythm: 16th note.
  constexpr Tick kBase16thDuration = duration::kSixteenthNote;  // 120 ticks.
  constexpr Tick k32ndDuration = duration::kThirtySecondNote;    // 60 ticks.

  // Previous pitch for interval tracking (start from middle of range).
  uint8_t prev_pitch = 0;
  if (!result.notes.empty()) {
    prev_pitch = result.notes.back().pitch;
  } else {
    prev_pitch = static_cast<uint8_t>((ctx.low_pitch + effective_high) / 2);
    prev_pitch = scale_util::nearestScaleTone(prev_pitch, ctx.key, ctx.scale);
  }

  Tick current_tick = tick;
  while (current_tick < end_tick) {
    const HarmonicEvent& harm = timeline.getAt(current_tick);
    float tension = computeHarmonicTension(harm);

    // Determine whether to attempt figure injection.
    bool try_figure = rng::rollProbability(rng, profile.figure_density);

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
          // Check for 32nd-note upgrade.
          float prob_32nd = compute32ndProbability(ctx.energy, tension,
                                                   profile.harmonic_tension_gate);
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

      // Determine bridge note duration.
      Tick bridge_dur = kBase16thDuration;
      if (profile.free_rhythm) {
        bridge_dur = detail::selectFreeRhythmDuration(rng);
      } else {
        float prob_32nd = compute32ndProbability(ctx.energy, tension,
                                                 profile.harmonic_tension_gate);
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
