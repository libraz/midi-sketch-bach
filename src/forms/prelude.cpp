// Implementation of the organ prelude generator.

#include "forms/prelude.h"

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <random>
#include <tuple>
#include <unordered_map>
#include <vector>

#include "core/bach_vocabulary.h"
#include "core/note_creator.h"
#include "core/note_source.h"
#include "core/pitch_utils.h"
#include "core/rng_util.h"
#include "core/scale.h"
#include "forms/form_constraint_setup.h"
#include "harmony/chord_tone_utils.h"
#include "harmony/chord_types.h"
#include "harmony/chord_voicer.h"
#include "harmony/harmonic_event.h"
#include "forms/prelude_figuration.h"
#include "forms/form_utils.h"
#include "solo_string/solo_vocabulary.h"
#include "organ/organ_techniques.h"

namespace bach {

namespace {

using namespace duration;

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

/// @brief Clamp voice count to valid range [2, 5].
/// @param num_voices Raw voice count from configuration.
/// @return Clamped voice count.
uint8_t clampPreludeVoiceCount(uint8_t num_voices) {
  if (num_voices < 2) return 2;
  if (num_voices > 5) return 5;
  return num_voices;
}

/// @brief Enforce prelude voice-duration constraints on a slot-pattern template.
///
/// Slot patterns produce sixteenth-note (120-tick) durations for all voices,
/// but prelude middle voice requires >= eighth (240) and bass >= quarter (480).
/// This collapses duplicate voice steps, moves onset to beat start for voices
/// that need long durations, and extends durations accordingly.
///
/// @param tmpl Template to adjust (modified in place).
void enforcePreludeSlotDurations(FigurationTemplate& tmpl) {
  if (tmpl.steps.empty()) return;

  // Minimum duration per voice index in prelude context.
  // Voice 0 (soprano): sixteenth is fine (test allows any).
  // Voice 1 (middle): minimum eighth note (240).
  // Voice 2+ (bass/lower): minimum quarter note (480).
  auto minDurationForVoice = [](uint8_t voice) -> Tick {
    if (voice == 0) return duration::kSixteenthNote;  // 120
    if (voice == 1) return duration::kEighthNote;      // 240
    return duration::kQuarterNote;                     // 480
  };

  // Keep only the first step for each voice, extend its duration.
  std::vector<FigurationStep> deduped;
  deduped.reserve(tmpl.steps.size());
  // Track which voices we've already seen.
  uint8_t seen_mask = 0;  // Bit mask, supports up to 8 voices.

  for (const auto& step : tmpl.steps) {
    uint8_t vid = step.voice_index;
    if (vid >= 8) continue;  // Safety.
    uint8_t bit = static_cast<uint8_t>(1u << vid);
    if (seen_mask & bit) continue;  // Skip duplicate voice steps.
    seen_mask |= bit;

    FigurationStep adjusted = step;
    Tick min_dur = minDurationForVoice(vid);

    if (adjusted.duration < min_dur) {
      // If the remaining beat time from current offset is insufficient,
      // move onset to beat start so the voice can sustain properly.
      Tick remaining = kTicksPerBeat - adjusted.relative_tick;
      if (remaining < min_dur) {
        adjusted.relative_tick = 0;
      }
      adjusted.duration = min_dur;
    }
    deduped.push_back(adjusted);
  }

  tmpl.steps = std::move(deduped);
}

// ---------------------------------------------------------------------------
// Texture thinning for boundary-driven density reduction
// ---------------------------------------------------------------------------

/// @brief Texture thinning strategy at phrase/harmonic boundaries.
enum class TextureThinning : uint8_t {
  None,             ///< No thinning applied.
  ShortenDuration,  ///< Shorten inner voice durations (most conservative).
  DropDoubling,     ///< Remove octave doublings at phrase boundaries.
};

/// @brief Determine texture thinning at a given bar and beat position.
/// @param bar_idx Zero-based bar index of the note.
/// @param beat_in_bar Zero-based beat index within the bar.
/// @param total_bars Total number of bars in the prelude (reserved for future cadence logic).
/// @param beats_per_bar Number of beats per bar.
/// @param num_voices Number of voices in the prelude.
/// @param timeline Harmonic timeline for event boundary detection.
/// @param beat_tick Absolute tick of the beat position.
/// @return TextureThinning strategy to apply at this position.
TextureThinning thinningAt(int bar_idx, int beat_in_bar,
                           int /*total_bars*/,
                           int beats_per_bar, uint8_t num_voices,
                           const HarmonicTimeline& timeline, Tick beat_tick) {
  if (num_voices < 3) return TextureThinning::None;

  bool is_phrase_end = (bar_idx % 2 == 1) && (beat_in_bar == beats_per_bar - 1);

  // Check if next beat starts a new harmonic event.
  bool is_event_boundary = false;
  Tick next_beat_tick = beat_tick + kTicksPerBeat;
  const auto& events = timeline.events();
  for (const auto& evt : events) {
    if (evt.tick > beat_tick && evt.tick <= next_beat_tick) {
      is_event_boundary = true;
      break;
    }
  }

  if (is_phrase_end) return TextureThinning::ShortenDuration;
  if (is_event_boundary) return TextureThinning::ShortenDuration;
  return TextureThinning::None;
}

// ---------------------------------------------------------------------------
// JSD diagnostic: interval distribution statistics
// ---------------------------------------------------------------------------

/// @brief Compute interval distribution from notes (simplified JSD proxy).
/// @param notes Vector of NoteEvents to analyze.
/// @param num_voices Number of voices in the prelude.
/// @return Tuple of {stepwise_ratio, leap_ratio, avg_interval}.
std::tuple<float, float, float> computeIntervalStats(
    const std::vector<NoteEvent>& notes, uint8_t /*num_voices*/) {
  // Sort by (voice, tick) for sequential interval computation.
  std::vector<const NoteEvent*> sorted;
  sorted.reserve(notes.size());
  for (const auto& note : notes) sorted.push_back(&note);
  std::sort(sorted.begin(), sorted.end(),
            [](const NoteEvent* lhs, const NoteEvent* rhs) {
              if (lhs->voice != rhs->voice) return lhs->voice < rhs->voice;
              return lhs->start_tick < rhs->start_tick;
            });

  int step_count = 0;
  int leap_count = 0;
  float interval_sum = 0.0f;
  int total_intervals = 0;
  uint8_t prev_voice = 255;
  uint8_t prev_pitch = 0;

  for (const auto* nptr : sorted) {
    if (nptr->voice == prev_voice && prev_pitch > 0) {
      int interval = std::abs(static_cast<int>(nptr->pitch) -
                              static_cast<int>(prev_pitch));
      if (interval > 0) {
        interval_sum += static_cast<float>(interval);
        total_intervals++;
        if (interval <= 2) {
          step_count++;
        } else {
          leap_count++;
        }
      }
    }
    prev_voice = nptr->voice;
    prev_pitch = nptr->pitch;
  }

  float stepwise = total_intervals > 0
                       ? static_cast<float>(step_count) / total_intervals
                       : 0.0f;
  float leap = total_intervals > 0
                   ? static_cast<float>(leap_count) / total_intervals
                   : 0.0f;
  float avg = total_intervals > 0 ? interval_sum / total_intervals : 0.0f;
  return {stepwise, leap, avg};
}

// ---------------------------------------------------------------------------
// Harmony-first prelude generation
// ---------------------------------------------------------------------------

// Harmony-first prelude generation: voice chords, then figurate.
std::vector<NoteEvent> generateHarmonicPreludeNotes(
    const HarmonicTimeline& timeline, uint8_t num_voices,
    FigurationType fig_type, std::mt19937& rng,
    PreludeType prelude_type) {
  // Section-level vocabulary slot pattern selection (position-dependent).
  std::vector<NoteEvent> all_notes;
  const auto& events = timeline.events();
  if (events.empty()) return all_notes;

  VoiceRangeFn voice_range = [](uint8_t v) -> std::pair<uint8_t, uint8_t> {
    return {getVoiceLowPitch(v), getVoiceHighPitch(v)};
  };

  // Step 1: Create voicings for each harmonic event.
  // Apply register shift every 4 events for variety (plan G: anti-monotony).
  std::vector<ChordVoicing> voicings;
  voicings.reserve(events.size());

  // Register shift patterns for variety (plan G: anti-monotony).
  // Soprano: ±3st cycle; Mid: counter-motion ∓2st on alternate events.
  constexpr int kSopShiftPattern[] = {0, 3, 0, -3};
  constexpr int kMidShiftPattern[] = {0, -2, 0, 2};
  constexpr int kShiftPatternSize = 4;

  auto shiftVoicePitch = [&voice_range](ChordVoicing& voicing, uint8_t vid,
                                        int shift, const HarmonicEvent& ev) {
    if (shift == 0 || vid >= voicing.num_voices) return;
    auto chord_pcs = getChordPitchClasses(ev.chord.quality,
                                          getPitchClass(ev.chord.root_pitch));
    int pitch = static_cast<int>(voicing.pitches[vid]);
    auto [lo, hi] = voice_range(vid);
    int step_dir = (shift > 0) ? 1 : -1;
    for (int offset = shift; std::abs(offset) <= 8; offset += step_dir) {
      int cand = pitch + offset;
      if (cand < lo || cand > hi) break;
      int cand_pc = ((cand % 12) + 12) % 12;
      for (int pc : chord_pcs) {
        if (cand_pc == pc) {
          // Ensure no voice crossing.
          bool ok = true;
          if (vid > 0 && cand > static_cast<int>(voicing.pitches[vid - 1])) ok = false;
          if (vid + 1 < voicing.num_voices &&
              cand < static_cast<int>(voicing.pitches[vid + 1])) ok = false;
          if (ok) {
            voicing.pitches[vid] = static_cast<uint8_t>(cand);
            return;
          }
        }
      }
    }
  };

  for (size_t i = 0; i < events.size(); ++i) {
    int shift_idx = static_cast<int>(i) % kShiftPatternSize;

    if (i == 0) {
      voicings.push_back(voiceChord(events[0], num_voices, voice_range));
    } else {
      voicings.push_back(
          smoothVoiceLeading(voicings[i - 1], events[i], num_voices, voice_range));
    }

    // Apply register shifts for variety.
    shiftVoicePitch(voicings[i], 0, kSopShiftPattern[shift_idx], events[i]);
    if (num_voices >= 2) {
      shiftVoicePitch(voicings[i], 1, kMidShiftPattern[shift_idx], events[i]);
    }
  }

  // Step 2: Apply figuration to each event.
  FigurationTemplate base_tmpl = createFigurationTemplate(fig_type, num_voices);
  int beats_per_bar = kTicksPerBar / kTicksPerBeat;
  Tick total_duration = events.empty() ? 0 :
      (events.back().end_tick - events.front().tick);

  // Rhythm variation probability depends on prelude type.
  float rhythm_variation_prob =
      (prelude_type == PreludeType::Perpetual) ? 0.05f : 0.15f;

  // Available new figuration types for phrase-level variety.
  constexpr FigurationType kNewFigTypes[] = {
      FigurationType::Falling3v, FigurationType::Arch3v,
      FigurationType::Mixed3v, FigurationType::Falling4v,
  };
  constexpr int kNewFigTypeCount = 4;

  // 2-bar phrase-level figuration type selection state.
  FigurationType current_phrase_type = fig_type;
  int phrase_idx = -1;
  int total_bars = static_cast<int>(total_duration / kTicksPerBar);

  // Inter-beat melodic memory: track soprano pitch and last note pitch across
  // beats so that each beat's figuration can connect smoothly to the previous.
  uint8_t prev_beat_soprano = 0;
  uint8_t prev_beat_last = 0;

  for (size_t i = 0; i < events.size(); ++i) {
    const auto& ev = events[i];
    const auto& voicing = voicings[i];
    Tick event_duration = ev.end_tick - ev.tick;

    // Position-dependent vocabulary slot pattern selection.
    // Section start: 60%, middle: 30%, pre-cadence: 50%.
    Tick event_start = ev.tick - events.front().tick;
    float progress = total_duration > 0
        ? static_cast<float>(event_start) / static_cast<float>(total_duration)
        : 0.5f;
    float slot_prob;
    if (progress < 0.1f) {
      slot_prob = 0.60f;  // Section opening.
    } else if (progress > 0.85f) {
      slot_prob = 0.50f;  // Pre-cadential.
    } else {
      slot_prob = 0.30f;  // Middle.
    }

    FigurationTemplate tmpl = base_tmpl;
    if (rng::rollProbability(rng, slot_prob)) {
      // Select a random vocabulary slot pattern matching voice count.
      std::vector<const FigurationSlotPattern*> slot_candidates;
      for (int si = 0; si < kSoloFigurationCount; ++si) {
        if (kSoloFigurations[si]->slot_count <= num_voices) {
          slot_candidates.push_back(kSoloFigurations[si]);
        }
      }
      if (!slot_candidates.empty()) {
        int pick = rng::rollRange(rng, 0, static_cast<int>(slot_candidates.size()) - 1);
        tmpl = createFigurationTemplateFromSlot(*slot_candidates[pick], num_voices);
        enforcePreludeSlotDurations(tmpl);
      }
    }

    // Apply figuration for each beat within this event.
    int num_beats = static_cast<int>(event_duration / kTicksPerBeat);
    for (int b = 0; b < num_beats; ++b) {
      Tick beat_tick = ev.tick + static_cast<Tick>(b) * kTicksPerBeat;

      // 2-bar phrase-level figuration type selection.
      int bar_idx = static_cast<int>(beat_tick / kTicksPerBar);
      int new_phrase_idx = bar_idx / 2;
      if (new_phrase_idx != phrase_idx) {
        phrase_idx = new_phrase_idx;
        // 40% chance to use a new figuration type for variety.
        if (rng::rollProbability(rng, 0.40f)) {
          int pick = rng::rollRange(rng, 0, kNewFigTypeCount - 1);
          current_phrase_type = kNewFigTypes[pick];
        } else {
          current_phrase_type = fig_type;  // Revert to base type.
        }
      }

      // Section-progress-dependent rhythm density shaping:
      //   Opening (0-15%):  simpler rhythm, low variation prob.
      //   Development (15-80%): more active, higher variation prob.
      //   Pre-cadence (80-90%): rhythmic augmentation begins.
      //   Cadence (last 2 bars): minimal variation for convergence.
      float effective_rhythm_var = rhythm_variation_prob;
      float beat_progress = total_duration > 0
          ? static_cast<float>(beat_tick - events.front().tick) /
            static_cast<float>(total_duration)
          : 0.5f;

      if (bar_idx >= total_bars - 2) {
        // Cadence: minimal variation for convergence.
        effective_rhythm_var = 0.05f;
      } else if (beat_progress < 0.15f) {
        // Opening: simpler, more predictable rhythm.
        effective_rhythm_var *= 0.3f;
      } else if (beat_progress > 0.80f) {
        // Pre-cadence: augmentation, reduce active variation.
        effective_rhythm_var *= 0.5f;
      } else if (beat_progress > 0.30f && beat_progress < 0.70f) {
        // Development peak: increase rhythm variation for activity.
        effective_rhythm_var = std::min(effective_rhythm_var * 1.5f, 0.25f);
      }

      // Variation: on second half of bar, shift mid voice for variety.
      ChordVoicing beat_voicing = voicing;
      if (b >= beats_per_bar / 2 && num_beats >= beats_per_bar) {
        auto chord_pcs = getChordPitchClasses(ev.chord.quality,
                                              getPitchClass(ev.chord.root_pitch));
        // Shift mid voice (voice 1) up by one chord tone step.
        if (num_voices >= 2) {
          int mid = static_cast<int>(beat_voicing.pitches[1]);
          auto [mid_low, mid_high] = voice_range(1);
          for (int offset = 1; offset <= 7; ++offset) {
            int cand = mid + offset;
            if (cand > mid_high) break;
            int cand_pc = ((cand % 12) + 12) % 12;
            for (int pc : chord_pcs) {
              if (cand_pc == pc) {
                // Ensure no voice crossing with soprano.
                if (cand <= static_cast<int>(beat_voicing.pitches[0])) {
                  beat_voicing.pitches[1] = static_cast<uint8_t>(cand);
                }
                goto mid_shift_done;
              }
            }
          }
        mid_shift_done:;
        }
      }

      // Select figuration template: use phrase-level new type or slot pattern.
      // When the phrase selected a new figuration type, use the rng overload.
      // Otherwise, use the existing slot-pattern-aware template from the event.
      FigurationTemplate beat_tmpl;
      if (current_phrase_type != fig_type) {
        beat_tmpl = createFigurationTemplate(current_phrase_type, num_voices,
                                             rng, effective_rhythm_var);
        // Enforce minimum voice durations (middle >= eighth, bass >= quarter).
        enforcePreludeSlotDurations(beat_tmpl);
      } else {
        beat_tmpl = tmpl;
      }
      // Compute section progress for NCT direction bias.
      float section_progress = total_duration > 0
          ? static_cast<float>(beat_tick - events.front().tick) /
            static_cast<float>(total_duration)
          : 0.5f;
      auto fig_notes = applyFiguration(beat_voicing, beat_tmpl, beat_tick,
                                       ev, voice_range, section_progress,
                                       prev_beat_soprano);

      // Inject passing and neighbor tones into weak sub-beats.
      // NCT density shaped by section progress:
      //   Opening: fewer NCTs for harmonic clarity.
      //   Development: higher NCT density for scalar motion.
      //   Pre-cadence: reduced NCTs for harmonic convergence.
      float base_nct_prob = (prelude_type == PreludeType::Perpetual) ? 0.50f : 0.35f;
      float nct_prob = base_nct_prob;
      if (beat_progress < 0.15f) {
        nct_prob *= 0.5f;  // Opening: half the NCT density.
      } else if (beat_progress > 0.30f && beat_progress < 0.70f) {
        nct_prob = std::min(base_nct_prob * 1.3f, 0.60f);  // Development peak.
      } else if (beat_progress > 0.85f) {
        nct_prob *= 0.4f;  // Pre-cadence: reduced for convergence.
      }
      injectNonChordTones(fig_notes, beat_tmpl, beat_tick, ev, voice_range,
                          rng, nct_prob, section_progress, prev_beat_last);

      // Update inter-beat melodic memory for the next beat's continuity.
      // Extract soprano pitch (voice 0 at beat start) and last note pitch.
      for (const auto& fig_note : fig_notes) {
        if (fig_note.voice == 0 && fig_note.start_tick == beat_tick) {
          prev_beat_soprano = fig_note.pitch;
        }
      }
      if (!fig_notes.empty()) {
        prev_beat_last = fig_notes.back().pitch;
      }

      all_notes.insert(all_notes.end(), fig_notes.begin(), fig_notes.end());
    }
  }

  return all_notes;
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
      config.key, target_duration, HarmonicResolution::Bar, prog_type);
  timeline.applyCadence(CadenceType::Perfect, config.key);

  // Step 3: Generate notes via harmony-first pipeline.
  FigurationType fig_type = (config.type == PreludeType::Perpetual)
                                ? FigurationType::ScaleConnect
                                : FigurationType::BrokenChord;
  std::vector<NoteEvent> all_notes =
      generateHarmonicPreludeNotes(timeline, num_voices, fig_type, rng,
                                   config.type);

  // Tag untagged notes with source for counterpoint protection levels.
  assert(countUnknownSource(all_notes) == 0 &&
         "All notes should have source set by generators");

  // Opening tonic pedal for Perpetual preludes (BWV543 style).
  // Bass voice sustains tonic for first 3 bars while upper voices figurate,
  // creating oblique motion and harmonic stability.
  if (config.type == PreludeType::Perpetual && num_voices >= 3) {
    uint8_t bass_voice = static_cast<uint8_t>(num_voices - 1);
    Tick pedal_end = std::min(kTicksPerBar * 3, target_duration);

    // Remove existing bass voice notes in pedal region.
    all_notes.erase(
        std::remove_if(all_notes.begin(), all_notes.end(),
                       [bass_voice, pedal_end](const NoteEvent& note) {
                         return note.voice == bass_voice &&
                                note.start_tick < pedal_end;
                       }),
        all_notes.end());

    // Find closest tonic pitch to center of bass range.
    auto [bass_lo, bass_hi] =
        std::make_pair(getVoiceLowPitch(bass_voice), getVoiceHighPitch(bass_voice));
    int tonic_pc = static_cast<int>(config.key.tonic);
    int center = (static_cast<int>(bass_lo) + static_cast<int>(bass_hi)) / 2;
    uint8_t tonic_pitch = bass_lo;
    for (int pitch = static_cast<int>(bass_lo); pitch <= static_cast<int>(bass_hi);
         ++pitch) {
      if (((pitch % 12) + 12) % 12 == tonic_pc) {
        if (std::abs(pitch - center) <
            std::abs(static_cast<int>(tonic_pitch) - center)) {
          tonic_pitch = static_cast<uint8_t>(pitch);
        }
      }
    }

    // Generate tonic pedal notes (one per bar for clean overlap).
    for (Tick bar_start = 0; bar_start < pedal_end; bar_start += kTicksPerBar) {
      NoteEvent pedal;
      pedal.start_tick = bar_start;
      pedal.duration = kTicksPerBar;
      pedal.pitch = tonic_pitch;
      pedal.velocity = kOrganVelocity;
      pedal.voice = bass_voice;
      pedal.source = BachNoteSource::PedalPoint;
      all_notes.push_back(pedal);
    }
  }

  // Bass voice harmonic constraint ceiling (kOrganPedalConstraint).
  // Ensure bass voice notes are predominantly chord tones. Only intervene
  // if the ratio drops below the ceiling threshold (0.85 from BWV578 pedal).
  if (num_voices >= 3) {
    uint8_t bass_voice = static_cast<uint8_t>(num_voices - 1);
    int bass_total = 0;
    int bass_chord_tones = 0;
    for (const auto& note : all_notes) {
      if (note.voice != bass_voice) continue;
      if (note.source == BachNoteSource::PedalPoint) continue;
      bass_total++;
      if (isChordTone(note.pitch, timeline.getAt(note.start_tick))) {
        bass_chord_tones++;
      }
    }
    float ct_ratio = bass_total > 0
        ? static_cast<float>(bass_chord_tones) / bass_total
        : 1.0f;

    // Only intervene if below ceiling.
    if (ct_ratio < kOrganPedalConstraint.chord_tone_ratio) {
      for (auto& note : all_notes) {
        if (note.voice != bass_voice) continue;
        if (note.source == BachNoteSource::PedalPoint) continue;
        if (isChordTone(note.pitch, timeline.getAt(note.start_tick))) continue;

        // Strong-beat notes get priority snapping.
        bool is_strong = (note.start_tick % kTicksPerBeat == 0);
        if (!is_strong) continue;  // Only fix strong-beat non-chord tones.

        const auto& harm_ev = timeline.getAt(note.start_tick);
        uint8_t bass_lo = getVoiceLowPitch(bass_voice);
        uint8_t bass_hi = getVoiceHighPitch(bass_voice);

        // Find nearest chord tone within bass range.
        auto chord_pcs = getChordPitchClasses(harm_ev.chord.quality,
                                              getPitchClass(harm_ev.chord.root_pitch));
        int best_dist = 999;
        uint8_t best_pitch = note.pitch;
        for (int cand = static_cast<int>(bass_lo);
             cand <= static_cast<int>(bass_hi); ++cand) {
          int cand_pc = ((cand % 12) + 12) % 12;
          for (int pitch_class : chord_pcs) {
            if (cand_pc == pitch_class) {
              int dist = std::abs(cand - static_cast<int>(note.pitch));
              if (dist < best_dist) {
                best_dist = dist;
                best_pitch = static_cast<uint8_t>(cand);
              }
            }
          }
        }
        if (best_dist <= 3) {  // Only snap if within minor 3rd.
          note.pitch = best_pitch;
        }
      }
    }
  }

  // --- Constraint-driven finalize: overlap dedup + range clamp + repeat break ---
  {
    auto voice_range = [](uint8_t vid) -> std::pair<uint8_t, uint8_t> {
      return {getVoiceLowPitch(vid), getVoiceHighPitch(vid)};
    };
    ScaleType pre_scale = config.key.is_minor ? ScaleType::HarmonicMinor
                                              : ScaleType::Major;
    finalizeFormNotes(all_notes, num_voices, voice_range, config.key.tonic,
                      pre_scale, /*max_consecutive=*/2);
  }

  // Boundary-driven texture thinning: reduce inner voice density at phrase
  // and harmonic event boundaries for avg_active 2.0-2.5.
  if (num_voices >= 3) {
    int total_bars = static_cast<int>(target_duration / kTicksPerBar);
    int beats_per_bar = static_cast<int>(kTicksPerBar / kTicksPerBeat);

    for (auto& note : all_notes) {
      // Only thin inner voices (not soprano or bass).
      if (note.voice == 0 || note.voice == num_voices - 1) continue;
      if (note.source == BachNoteSource::PedalPoint) continue;

      int bar_idx = static_cast<int>(note.start_tick / kTicksPerBar);
      int beat_in_bar = static_cast<int>(
          (note.start_tick % kTicksPerBar) / kTicksPerBeat);

      TextureThinning thin = thinningAt(bar_idx, beat_in_bar, total_bars,
                                        beats_per_bar, num_voices, timeline,
                                        note.start_tick);

      if (thin == TextureThinning::ShortenDuration) {
        // Shorten to half duration, minimum eighth note (preserving
        // quantized duration constraints for inner voices).
        Tick shortened = std::max(note.duration / 2, duration::kEighthNote);
        // Near cadence (last 2 bars): further shorten.
        if (bar_idx >= total_bars - 2) {
          shortened = std::max(shortened / 2, duration::kEighthNote);
        }
        note.duration = shortened;
      }
    }
  }

  // Drop octave doublings in inner voices at phrase boundaries.
  if (num_voices >= 3) {
    // Build per-tick pitch set for outer voices (soprano + bass).
    std::unordered_map<Tick, std::vector<uint8_t>> outer_pitches;
    for (const auto& note : all_notes) {
      if (note.voice == 0 || note.voice == num_voices - 1) {
        outer_pitches[note.start_tick].push_back(note.pitch);
      }
    }

    constexpr int kBeatsPerBarLocal = kTicksPerBar / kTicksPerBeat;

    all_notes.erase(
        std::remove_if(
            all_notes.begin(), all_notes.end(),
            [&](const NoteEvent& note) {
              // Only remove inner voice doublings.
              if (note.voice == 0 || note.voice == num_voices - 1) return false;
              if (note.source == BachNoteSource::PedalPoint) return false;

              int bar_idx = static_cast<int>(note.start_tick / kTicksPerBar);
              int beat_in_bar = static_cast<int>(
                  (note.start_tick % kTicksPerBar) / kTicksPerBeat);

              // Only at phrase boundaries (end of 2-bar phrase).
              bool is_phrase_boundary =
                  (bar_idx % 2 == 1) && (beat_in_bar >= kBeatsPerBarLocal - 1);
              if (!is_phrase_boundary) return false;

              auto iter = outer_pitches.find(note.start_tick);
              if (iter == outer_pitches.end()) return false;

              int note_pc = note.pitch % 12;
              for (uint8_t outer_p : iter->second) {
                if (outer_p % 12 == note_pc && outer_p != note.pitch) {
                  return true;  // Octave doubling -- remove.
                }
              }
              return false;
            }),
        all_notes.end());
  }

  // Diagnostic: interval distribution vs Bach reference profile.
  {
    auto [stepwise, leap, avg_interval] =
        computeIntervalStats(all_notes, num_voices);
    const auto& ref = kOrganUpperProfile;
    float step_diff = std::abs(stepwise - ref.stepwise_ratio);
    float avg_diff = std::abs(avg_interval - ref.avg_interval);

    // PreludeType-dependent thresholds.
    float step_thresh = (config.type == PreludeType::Perpetual) ? 0.12f : 0.20f;
    float avg_thresh = (config.type == PreludeType::Perpetual) ? 1.0f : 1.5f;

    if (step_diff > step_thresh || avg_diff > avg_thresh) {
      std::fprintf(stderr,
                   "[Prelude] interval profile: stepwise=%.2f (ref %.2f, diff %.2f), "
                   "leap=%.2f (ref %.2f), avg=%.1f (ref %.1f)\n",
                   stepwise, ref.stepwise_ratio, step_diff,
                   leap, ref.leap_ratio,
                   avg_interval, ref.avg_interval);
    }
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

  // Step 5: Final overlap dedup + sort after Picardy/registration post-processing.
  form_utils::normalizeAndRedistribute(tracks, num_voices);

  result.tracks = std::move(tracks);
  result.timeline = std::move(timeline);
  result.total_duration_ticks = target_duration;
  result.success = true;

  return result;
}

}  // namespace bach
