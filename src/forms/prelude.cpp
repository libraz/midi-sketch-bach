// Implementation of the organ prelude generator.

#include "forms/prelude.h"

#include <algorithm>
#include <cassert>
#include <random>
#include <vector>

#include "core/note_creator.h"
#include "core/note_source.h"
#include "core/pitch_utils.h"
#include "core/rng_util.h"
#include "core/scale.h"
#include "counterpoint/coordinate_voices.h"
#include "counterpoint/leap_resolution.h"
#include "counterpoint/parallel_repair.h"
#include "counterpoint/repeated_note_repair.h"
#include "counterpoint/vertical_safe.h"
#include "harmony/chord_tone_utils.h"
#include "harmony/chord_types.h"
#include "harmony/chord_voicer.h"
#include "harmony/harmonic_event.h"
#include "forms/prelude_figuration.h"
#include "forms/form_utils.h"
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

// Harmony-first prelude generation: voice chords, then figurate.
std::vector<NoteEvent> generateHarmonicPreludeNotes(
    const HarmonicTimeline& timeline, uint8_t num_voices,
    FigurationType fig_type, std::mt19937& rng) {
  (void)rng;  // Reserved for future section-level figuration selection.
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
  FigurationTemplate tmpl = createFigurationTemplate(fig_type, num_voices);
  int beats_per_bar = kTicksPerBar / kTicksPerBeat;

  for (size_t i = 0; i < events.size(); ++i) {
    const auto& ev = events[i];
    const auto& voicing = voicings[i];
    Tick event_duration = ev.end_tick - ev.tick;

    // Apply figuration for each beat within this event.
    int num_beats = static_cast<int>(event_duration / kTicksPerBeat);
    for (int b = 0; b < num_beats; ++b) {
      Tick beat_tick = ev.tick + static_cast<Tick>(b) * kTicksPerBeat;

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

      auto fig_notes = applyFiguration(beat_voicing, tmpl, beat_tick, ev, voice_range);
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
      generateHarmonicPreludeNotes(timeline, num_voices, fig_type, rng);

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
                                        BachNoteSource::EpisodeMaterial,
                                        BachNoteSource::PreludeFiguration};
    coord_config.use_next_pitch_map = true;
    coord_config.check_cross_relations = true;
    coord_config.form_name = "Prelude";
    auto form_profile = getFormProfile(FormType::PreludeAndFugue);
    coord_config.dissonance_policy = form_profile.dissonance_policy;
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
