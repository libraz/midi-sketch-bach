// Implementation of the unified generator that routes to form-specific generators.

#include "generator.h"

#include <algorithm>
#include <random>

#include "forms/chorale_prelude.h"
#include "forms/fantasia.h"
#include "forms/passacaglia.h"
#include "forms/prelude.h"
#include "forms/toccata.h"
#include "fugue/fugue_config.h"
#include "fugue/fugue_generator.h"
#include "harmony/harmonic_timeline.h"
#include "harmony/modulation_plan.h"
#include "harmony/tempo_map.h"
#include "expression/articulation.h"
#include "midi/velocity_curve.h"
#include "solo_string/arch/chaconne_engine.h"
#include "solo_string/flow/harmonic_arpeggio_engine.h"

namespace bach {

namespace {

/// @brief Seed offset applied to the prelude seed so it differs from the fugue seed.
constexpr uint32_t kPreludeSeedOffset = 7919u;

/// @brief Generate a random seed using the system random device.
/// @return A non-zero random seed.
uint32_t generateRandomSeed() {
  std::random_device device;
  uint32_t result = device();
  // Ensure non-zero so we can distinguish "auto" from explicit 0.
  if (result == 0) result = 1;
  return result;
}

/// @brief Get fugue develop_pairs and episode_bars for a DurationScale.
/// @param scale The duration scale.
/// @param[out] pairs Output: number of Episode+MiddleEntry pairs.
/// @param[out] ep_bars Output: bars per episode.
void fugueDurationParams(DurationScale scale, int& pairs, int& ep_bars) {
  switch (scale) {
    case DurationScale::Short:
      pairs = 1; ep_bars = 2; break;
    case DurationScale::Medium:
      pairs = 3; ep_bars = 3; break;
    case DurationScale::Long:
      pairs = 6; ep_bars = 3; break;
    case DurationScale::Full:
      pairs = 10; ep_bars = 4; break;
  }
}

/// @brief Get chaconne target_variations for a DurationScale.
/// @param scale The duration scale.
/// @return Target number of variations.
int chaconneVariationsForScale(DurationScale scale) {
  switch (scale) {
    case DurationScale::Short:  return 0;   // Use standard plan
    case DurationScale::Medium: return 24;
    case DurationScale::Long:   return 40;
    case DurationScale::Full:   return 64;
  }
  return 0;
}

/// @brief Get flow section count for a DurationScale.
/// @param scale The duration scale.
/// @return Number of sections.
int flowSectionsForScale(DurationScale scale) {
  switch (scale) {
    case DurationScale::Short:  return 6;
    case DurationScale::Medium: return 10;
    case DurationScale::Long:   return 16;
    case DurationScale::Full:   return 20;
  }
  return 6;
}

/// @brief Map GeneratorConfig to FugueConfig for fugue-based forms.
/// @param config The unified generator configuration.
/// @return FugueConfig populated from the generator config.
FugueConfig toFugueConfig(const GeneratorConfig& config) {
  FugueConfig fconfig;
  fconfig.key = config.key.tonic;
  fconfig.is_minor = config.key.is_minor;
  fconfig.num_voices = config.num_voices;
  fconfig.bpm = config.bpm;
  fconfig.seed = config.seed;
  fconfig.character = config.character;

  if (config.target_bars > 0) {
    // Estimate: each pair adds ~episode_bars + subject_bars bars.
    // Baseline without pairs: ~10 bars (exposition + return episode + stretto + coda).
    int baseline = 10;
    int bars_left = static_cast<int>(config.target_bars) - baseline;
    int pair_cost = fconfig.episode_bars + fconfig.subject_bars;
    if (pair_cost < 2) pair_cost = 2;
    fconfig.develop_pairs = std::max(1, bars_left / pair_cost);
  } else {
    fugueDurationParams(config.scale, fconfig.develop_pairs, fconfig.episode_bars);
  }

  // Create modulation plan based on key mode (Principle 4: design values).
  if (config.key.is_minor) {
    fconfig.modulation_plan = ModulationPlan::createForMinor(config.key.tonic);
  } else {
    fconfig.modulation_plan = ModulationPlan::createForMajor(config.key.tonic);
  }
  fconfig.has_modulation_plan = true;

  return fconfig;
}

/// @brief Map GeneratorConfig to PreludeConfig for prelude generation.
/// @param config The unified generator configuration.
/// @param fugue_length_ticks Duration of the paired fugue (used to scale prelude length).
/// @return PreludeConfig populated from the generator config.
PreludeConfig toPreludeConfig(const GeneratorConfig& config, Tick fugue_length_ticks) {
  PreludeConfig pconfig;
  pconfig.key = config.key;
  pconfig.num_voices = config.num_voices;
  pconfig.bpm = config.bpm;
  pconfig.seed = config.seed + kPreludeSeedOffset;
  pconfig.fugue_length_ticks = fugue_length_ticks;
  return pconfig;
}

/// @brief Calculate the total duration of a set of tracks.
/// @param tracks The tracks to measure.
/// @return The maximum end tick across all notes in all tracks.
Tick calculateTotalDuration(const std::vector<Track>& tracks) {
  Tick max_end = 0;
  for (const auto& track : tracks) {
    for (const auto& note : track.notes) {
      Tick end_tick = note.start_tick + note.duration;
      if (end_tick > max_end) {
        max_end = end_tick;
      }
    }
  }
  return max_end;
}

/// @brief Offset all note start_ticks in the given tracks by a fixed amount.
/// @param tracks Tracks whose notes will be shifted forward in time.
/// @param offset_ticks Number of ticks to add to each note's start_tick.
void offsetTrackNotes(std::vector<Track>& tracks, Tick offset_ticks) {
  for (auto& track : tracks) {
    for (auto& note : track.notes) {
      note.start_tick += offset_ticks;
    }
  }
}

/// @brief Merge two sets of tracks, offsetting the second by a duration.
///
/// The second set of tracks (fugue/suffix) is offset in time and merged
/// into the first set (prelude/prefix) by track index. Tracks beyond
/// the first set's size inherit channel/program from the second set.
///
/// @param prefix_tracks The first form's tracks (modified in-place to receive merged result).
/// @param suffix_tracks The second form's tracks (notes already offset).
void mergeTracksInPlace(std::vector<Track>& prefix_tracks,
                        std::vector<Track>& suffix_tracks) {
  size_t track_count = std::max(prefix_tracks.size(), suffix_tracks.size());
  prefix_tracks.resize(track_count);
  for (size_t idx = 0; idx < track_count; ++idx) {
    if (idx < suffix_tracks.size()) {
      auto& dest = prefix_tracks[idx].notes;
      auto& src = suffix_tracks[idx].notes;
      dest.insert(dest.end(), src.begin(), src.end());
      if (prefix_tracks[idx].name.empty()) {
        prefix_tracks[idx].name = suffix_tracks[idx].name;
        prefix_tracks[idx].channel = suffix_tracks[idx].channel;
        prefix_tracks[idx].program = suffix_tracks[idx].program;
      }
    }
  }
}

/// @brief Map a track index to a VoiceRole for articulation purposes.
///
/// For organ-system multi-voice forms the mapping follows exposition entry order:
///   0 -> Assert (subject voice)
///   1 -> Respond (answer voice)
///   2 -> Propel (free counterpoint)
///   3+ -> Ground (pedal / bass foundation)
///
/// For solo-string forms there is typically a single track; Assert is used
/// to give a moderate non-legato articulation.
///
/// @param track_index Zero-based index of the track.
/// @param is_organ True for organ-system forms, false for solo-string.
/// @return VoiceRole suitable for articulation rule lookup.
VoiceRole voiceRoleForTrack(size_t track_index, bool is_organ) {
  if (!is_organ) {
    return VoiceRole::Assert;
  }
  switch (track_index) {
    case 0:  return VoiceRole::Assert;
    case 1:  return VoiceRole::Respond;
    case 2:  return VoiceRole::Propel;
    default: return VoiceRole::Ground;
  }
}

/// @brief Apply articulation to every track in a GeneratorResult.
///
/// This must be called as the final processing step before the result is
/// returned to the caller (and subsequently written to MIDI).  It adjusts
/// note durations via gate ratios and adds phrase breathing at cadence points.
///
/// @param result The generation result whose tracks will be modified in place.
/// @param instrument The instrument type (organ vs non-organ affects velocity).
void applyArticulationToResult(GeneratorResult& result, InstrumentType instrument) {
  if (!result.success) {
    return;
  }

  bool is_organ = (instrument == InstrumentType::Organ);
  const HarmonicTimeline* timeline_ptr =
      result.timeline.size() > 0 ? &result.timeline : nullptr;

  for (size_t idx = 0; idx < result.tracks.size(); ++idx) {
    VoiceRole role = voiceRoleForTrack(idx, is_organ);
    applyArticulation(result.tracks[idx].notes, role, timeline_ptr, is_organ);
  }
}

/// @brief Generate a fugue-only form (Fugue, ToccataAndFugue, etc.).
/// @param config Unified generator configuration.
/// @return GeneratorResult with fugue tracks.
GeneratorResult generateFugueForm(const GeneratorConfig& config) {
  GeneratorResult result;
  FugueConfig fconfig = toFugueConfig(config);

  FugueResult fugue_result = generateFugue(fconfig);

  if (!fugue_result.success) {
    result.success = false;
    result.error_message = fugue_result.error_message;
    return result;
  }

  result.tracks = std::move(fugue_result.tracks);
  result.total_duration_ticks = calculateTotalDuration(result.tracks);
  result.tempo_events = generateFugueTempoMap(fugue_result.structure, config.bpm);
  result.timeline = fugue_result.timeline.size() > 0
      ? std::move(fugue_result.timeline)
      : HarmonicTimeline::createStandard(
            config.key, result.total_duration_ticks, HarmonicResolution::Bar);
  result.generation_timeline = std::move(fugue_result.generation_timeline);
  result.success = true;
  result.form_description =
      std::string(formTypeToString(config.form)) + " in " +
      keySignatureToString(config.key) + ", " +
      std::to_string(config.num_voices) + " voices, " +
      subjectCharacterToString(config.character) + " character";

  return result;
}

/// @brief Generate a prelude-and-fugue form (prelude + fugue concatenated).
/// @param config Unified generator configuration.
/// @return GeneratorResult with prelude and fugue tracks merged.
GeneratorResult generatePreludeAndFugueForm(const GeneratorConfig& config) {
  GeneratorResult result;

  // Step 1: Generate the fugue first to know its length.
  FugueConfig fconfig = toFugueConfig(config);
  FugueResult fugue_result = generateFugue(fconfig);

  if (!fugue_result.success) {
    result.success = false;
    result.error_message = "Fugue generation failed: " + fugue_result.error_message;
    return result;
  }

  Tick fugue_duration = calculateTotalDuration(fugue_result.tracks);

  // Step 2: Generate the prelude, scaled to fugue length.
  PreludeConfig pconfig = toPreludeConfig(config, fugue_duration);
  PreludeResult prelude_result = generatePrelude(pconfig);

  if (!prelude_result.success) {
    result.success = false;
    result.error_message = "Prelude generation failed";
    return result;
  }

  Tick prelude_duration = prelude_result.total_duration_ticks;

  // Step 3: Offset all fugue notes by prelude duration.
  offsetTrackNotes(fugue_result.tracks, prelude_duration);

  // Step 4: Merge prelude and fugue tracks (same channel/voice mapping).
  // Both generators produce tracks indexed by voice, so we merge by index.
  size_t track_count = prelude_result.tracks.size();
  if (fugue_result.tracks.size() > track_count) {
    track_count = fugue_result.tracks.size();
  }

  result.tracks.resize(track_count);
  for (size_t idx = 0; idx < track_count; ++idx) {
    if (idx < prelude_result.tracks.size()) {
      result.tracks[idx] = std::move(prelude_result.tracks[idx]);
    }
    if (idx < fugue_result.tracks.size()) {
      // Append fugue notes to the track.
      auto& dest_notes = result.tracks[idx].notes;
      auto& src_notes = fugue_result.tracks[idx].notes;
      dest_notes.insert(dest_notes.end(), src_notes.begin(), src_notes.end());

      // If the prelude track was empty (fewer voices in prelude), copy metadata.
      if (result.tracks[idx].name.empty()) {
        result.tracks[idx].name = fugue_result.tracks[idx].name;
        result.tracks[idx].channel = fugue_result.tracks[idx].channel;
        result.tracks[idx].program = fugue_result.tracks[idx].program;
      }
    }
  }

  // Step 5: Build tempo events (prelude tempo at tick 0, fugue tempo map offset).
  result.tempo_events.push_back({0, config.bpm});
  auto fugue_tempo = generateFugueTempoMap(fugue_result.structure, config.bpm);
  for (auto& evt : fugue_tempo) {
    evt.tick += prelude_duration;
    result.tempo_events.push_back(evt);
  }

  result.total_duration_ticks = prelude_duration + fugue_duration;
  // Use the fugue's tonal plan timeline (offset by prelude duration) if available.
  if (fugue_result.timeline.size() > 0) {
    // Build a combined timeline: prelude uses standard, fugue uses tonal plan.
    HarmonicTimeline combined = HarmonicTimeline::createStandard(
        config.key, prelude_duration, HarmonicResolution::Bar);
    for (const auto& ev : fugue_result.timeline.events()) {
      HarmonicEvent offset_ev = ev;
      offset_ev.tick += prelude_duration;
      offset_ev.end_tick += prelude_duration;
      combined.addEvent(offset_ev);
    }
    result.timeline = std::move(combined);
  } else {
    result.timeline = HarmonicTimeline::createStandard(
        config.key, result.total_duration_ticks, HarmonicResolution::Bar);
  }
  // Offset generation_timeline by prelude duration for dual-timeline analysis.
  if (fugue_result.generation_timeline.size() > 0) {
    HarmonicTimeline offset_gen_tl;
    for (const auto& ev : fugue_result.generation_timeline.events()) {
      HarmonicEvent offset_ev = ev;
      offset_ev.tick += prelude_duration;
      offset_ev.end_tick += prelude_duration;
      offset_gen_tl.addEvent(offset_ev);
    }
    result.generation_timeline = std::move(offset_gen_tl);
  }
  result.success = true;
  result.form_description =
      "Prelude and Fugue in " + keySignatureToString(config.key) + ", " +
      std::to_string(config.num_voices) + " voices, " +
      subjectCharacterToString(config.character) + " character";

  return result;
}

}  // namespace

GeneratorResult generate(const GeneratorConfig& config) {
  GeneratorResult result;

  // Auto-select seed if 0.
  GeneratorConfig effective_config = config;
  if (effective_config.seed == 0) {
    effective_config.seed = generateRandomSeed();
  }
  result.seed_used = effective_config.seed;

  switch (effective_config.form) {
    case FormType::Fugue: {
      result = generateFugueForm(effective_config);
      result.seed_used = effective_config.seed;
      break;
    }

    case FormType::ToccataAndFugue: {
      // Generate toccata free section, then append a fugue.
      ToccataConfig tconfig;
      tconfig.key = effective_config.key;
      tconfig.bpm = effective_config.bpm;
      tconfig.seed = effective_config.seed;
      tconfig.num_voices = effective_config.num_voices;
      tconfig.section_bars = 24;

      ToccataResult toc_result = generateToccata(tconfig);
      if (!toc_result.success) {
        result.success = false;
        result.seed_used = effective_config.seed;
        result.error_message = "Toccata generation failed: " + toc_result.error_message;
        break;
      }

      Tick toc_duration = toc_result.total_duration_ticks;

      // Generate the fugue section, subtracting toccata bars from target.
      GeneratorConfig fugue_gen_config = effective_config;
      if (fugue_gen_config.target_bars > 0) {
        uint32_t toc_bars = tconfig.section_bars;
        fugue_gen_config.target_bars =
            (fugue_gen_config.target_bars > toc_bars)
                ? fugue_gen_config.target_bars - toc_bars
                : 12;  // minimum fugue baseline
      }
      FugueConfig fconfig = toFugueConfig(fugue_gen_config);
      FugueResult fugue_result = generateFugue(fconfig);
      if (!fugue_result.success) {
        result.success = false;
        result.seed_used = effective_config.seed;
        result.error_message = "Fugue generation failed: " + fugue_result.error_message;
        break;
      }

      // Compute fugue duration BEFORE offset (avoid double-counting).
      Tick fugue_duration = calculateTotalDuration(fugue_result.tracks);

      // Offset fugue notes by toccata duration and merge tracks.
      offsetTrackNotes(fugue_result.tracks, toc_duration);
      result.tracks = std::move(toc_result.tracks);
      mergeTracksInPlace(result.tracks, fugue_result.tracks);

      result.total_duration_ticks = toc_duration + fugue_duration;

      // Toccata tempo map + offset fugue tempo map.
      result.tempo_events = generateToccataTempoMap(
          toc_result.opening_start, toc_result.opening_end,
          toc_result.recit_start, toc_result.recit_end,
          toc_result.drive_start, toc_result.drive_end,
          effective_config.bpm);
      auto fugue_tempo = generateFugueTempoMap(fugue_result.structure, effective_config.bpm);
      for (auto& evt : fugue_tempo) {
        evt.tick += toc_duration;
        result.tempo_events.push_back(evt);
      }

      result.timeline = HarmonicTimeline::createStandard(
          effective_config.key, result.total_duration_ticks, HarmonicResolution::Bar);
      // Offset generation_timeline by toccata duration for dual-timeline analysis.
      if (fugue_result.generation_timeline.size() > 0) {
        HarmonicTimeline offset_gen_tl;
        for (const auto& ev : fugue_result.generation_timeline.events()) {
          HarmonicEvent offset_ev = ev;
          offset_ev.tick += toc_duration;
          offset_ev.end_tick += toc_duration;
          offset_gen_tl.addEvent(offset_ev);
        }
        result.generation_timeline = std::move(offset_gen_tl);
      }
      result.success = true;
      result.seed_used = effective_config.seed;
      result.form_description =
          "Toccata and Fugue in " + keySignatureToString(effective_config.key) + ", " +
          std::to_string(effective_config.num_voices) + " voices";
      break;
    }

    case FormType::FantasiaAndFugue: {
      // Generate fantasia free section, then append a fugue.
      FantasiaConfig fant_config;
      fant_config.key = effective_config.key;
      fant_config.bpm = effective_config.bpm;
      fant_config.seed = effective_config.seed;
      fant_config.num_voices = effective_config.num_voices;
      fant_config.section_bars = 32;

      FantasiaResult fant_result = generateFantasia(fant_config);
      if (!fant_result.success) {
        result.success = false;
        result.seed_used = effective_config.seed;
        result.error_message = "Fantasia generation failed: " + fant_result.error_message;
        break;
      }

      Tick fant_duration = fant_result.total_duration_ticks;

      // Generate the fugue section, subtracting fantasia bars from target.
      GeneratorConfig fugue_gen_config = effective_config;
      if (fugue_gen_config.target_bars > 0) {
        uint32_t fant_bars = fant_config.section_bars;
        fugue_gen_config.target_bars =
            (fugue_gen_config.target_bars > fant_bars)
                ? fugue_gen_config.target_bars - fant_bars
                : 12;  // minimum fugue baseline
      }
      FugueConfig fconfig = toFugueConfig(fugue_gen_config);
      FugueResult fugue_result = generateFugue(fconfig);
      if (!fugue_result.success) {
        result.success = false;
        result.seed_used = effective_config.seed;
        result.error_message = "Fugue generation failed: " + fugue_result.error_message;
        break;
      }

      // Compute fugue duration BEFORE offset (avoid double-counting).
      Tick fugue_duration = calculateTotalDuration(fugue_result.tracks);

      // Offset fugue notes and merge tracks.
      offsetTrackNotes(fugue_result.tracks, fant_duration);
      result.tracks = std::move(fant_result.tracks);
      mergeTracksInPlace(result.tracks, fugue_result.tracks);

      result.total_duration_ticks = fant_duration + fugue_duration;

      // Fantasia tempo map + offset fugue tempo map.
      result.tempo_events = generateFantasiaTempoMap(
          fant_duration, fant_config.section_bars, effective_config.bpm);
      auto fugue_tempo = generateFugueTempoMap(fugue_result.structure, effective_config.bpm);
      for (auto& evt : fugue_tempo) {
        evt.tick += fant_duration;
        result.tempo_events.push_back(evt);
      }

      result.timeline = HarmonicTimeline::createStandard(
          effective_config.key, result.total_duration_ticks, HarmonicResolution::Bar);
      // Offset generation_timeline by fantasia duration for dual-timeline analysis.
      if (fugue_result.generation_timeline.size() > 0) {
        HarmonicTimeline offset_gen_tl;
        for (const auto& ev : fugue_result.generation_timeline.events()) {
          HarmonicEvent offset_ev = ev;
          offset_ev.tick += fant_duration;
          offset_ev.end_tick += fant_duration;
          offset_gen_tl.addEvent(offset_ev);
        }
        result.generation_timeline = std::move(offset_gen_tl);
      }
      result.success = true;
      result.seed_used = effective_config.seed;
      result.form_description =
          "Fantasia and Fugue in " + keySignatureToString(effective_config.key) + ", " +
          std::to_string(effective_config.num_voices) + " voices";
      break;
    }

    case FormType::Passacaglia: {
      PassacagliaConfig pconfig;
      pconfig.key = effective_config.key;
      pconfig.bpm = effective_config.bpm;
      pconfig.seed = effective_config.seed;
      pconfig.num_voices = effective_config.num_voices;

      PassacagliaResult pass_result = generatePassacaglia(pconfig);
      if (!pass_result.success) {
        result.success = false;
        result.seed_used = effective_config.seed;
        result.error_message = "Passacaglia generation failed: " + pass_result.error_message;
        break;
      }

      result.tracks = std::move(pass_result.tracks);
      result.total_duration_ticks = pass_result.total_duration_ticks;
      result.tempo_events = generatePassacagliaTempoMap(
          pconfig.num_variations, pconfig.ground_bass_bars, effective_config.bpm);
      result.timeline = pass_result.timeline.size() > 0
          ? std::move(pass_result.timeline)
          : HarmonicTimeline::createStandard(
                effective_config.key, result.total_duration_ticks, HarmonicResolution::Bar);
      result.success = true;
      result.seed_used = effective_config.seed;
      result.form_description =
          "Passacaglia in " + keySignatureToString(effective_config.key);
      break;
    }

    case FormType::PreludeAndFugue: {
      result = generatePreludeAndFugueForm(effective_config);
      result.seed_used = effective_config.seed;
      break;
    }

    case FormType::TrioSonata: {
      result.success = false;
      result.seed_used = effective_config.seed;
      result.error_message = "TrioSonata generation not yet implemented";
      result.form_description = "Trio Sonata (stub)";
      break;
    }

    case FormType::ChoralePrelude: {
      // Validate character-form compatibility before generation.
      if (!isCharacterFormCompatible(effective_config.character, FormType::ChoralePrelude)) {
        result.success = false;
        result.seed_used = effective_config.seed;
        result.error_message =
            "Incompatible character for ChoralePrelude: " +
            std::string(subjectCharacterToString(effective_config.character));
        break;
      }

      ChoralePreludeConfig cpconfig;
      cpconfig.key = effective_config.key;
      cpconfig.bpm = effective_config.bpm;
      cpconfig.seed = effective_config.seed;

      ChoralePreludeResult cp_result = generateChoralePrelude(cpconfig);

      if (!cp_result.success) {
        result.success = false;
        result.seed_used = effective_config.seed;
        result.error_message = "ChoralePrelude generation failed";
        break;
      }

      result.tracks = std::move(cp_result.tracks);
      result.total_duration_ticks = cp_result.total_duration_ticks;
      result.tempo_events.push_back({0, effective_config.bpm});
      result.timeline = cp_result.timeline.size() > 0
          ? std::move(cp_result.timeline)
          : HarmonicTimeline::createStandard(
                effective_config.key, result.total_duration_ticks, HarmonicResolution::Bar);
      result.success = true;
      result.seed_used = effective_config.seed;
      result.form_description =
          "Chorale Prelude in " + keySignatureToString(effective_config.key);
      break;
    }

    case FormType::CelloPrelude: {
      ArpeggioFlowConfig flow_config;
      flow_config.key = effective_config.key;
      flow_config.bpm = effective_config.bpm;
      flow_config.seed = effective_config.seed;
      flow_config.instrument = effective_config.instrument;

      if (effective_config.target_bars > 0) {
        flow_config.num_sections = std::max(3, static_cast<int>(
            effective_config.target_bars / flow_config.bars_per_section));
      } else {
        flow_config.num_sections = flowSectionsForScale(effective_config.scale);
      }

      ArpeggioFlowResult flow_result = generateArpeggioFlow(flow_config);

      if (!flow_result.success) {
        result.success = false;
        result.seed_used = effective_config.seed;
        result.error_message = "CelloPrelude generation failed: " +
                               flow_result.error_message;
        break;
      }

      result.tracks = std::move(flow_result.tracks);
      result.total_duration_ticks = flow_result.total_duration_ticks;
      result.tempo_events.push_back({0, effective_config.bpm});
      result.timeline = flow_result.timeline.size() > 0
          ? std::move(flow_result.timeline)
          : HarmonicTimeline::createStandard(
                effective_config.key, result.total_duration_ticks, HarmonicResolution::Bar);
      result.success = true;
      result.seed_used = effective_config.seed;
      result.form_description =
          "Cello Prelude in " + keySignatureToString(effective_config.key);
      break;
    }

    case FormType::Chaconne: {
      ChaconneConfig ch_config;
      ch_config.key = effective_config.key;
      ch_config.bpm = effective_config.bpm;
      ch_config.seed = effective_config.seed;
      ch_config.instrument = effective_config.instrument;

      if (effective_config.target_bars > 0) {
        // Each variation = 4 bars (ground bass cycle).
        ch_config.target_variations = std::max(10, static_cast<int>(
            effective_config.target_bars / 4));
      } else {
        ch_config.target_variations = chaconneVariationsForScale(effective_config.scale);
      }

      ChaconneResult ch_result = generateChaconne(ch_config);

      if (!ch_result.success) {
        result.success = false;
        result.seed_used = effective_config.seed;
        result.error_message = ch_result.error_message;
        break;
      }

      result.tracks = std::move(ch_result.tracks);
      result.total_duration_ticks = ch_result.total_duration_ticks;
      result.tempo_events.push_back({0, effective_config.bpm});
      result.timeline = ch_result.timeline.size() > 0
          ? std::move(ch_result.timeline)
          : HarmonicTimeline::createStandard(
                effective_config.key, result.total_duration_ticks, HarmonicResolution::Bar);
      result.success = true;
      result.seed_used = ch_result.seed_used;
      result.form_description = "Chaconne in " + keySignatureToString(effective_config.key);
      break;
    }
  }

  // Apply articulation as the final processing step before returning.
  // This adjusts note durations (gate ratio) and adds phrase breathing at cadences.
  // Skipped automatically for failed results (applyArticulationToResult checks success).
  applyArticulationToResult(result, effective_config.instrument);

  // Apply velocity curves for non-organ instruments.
  if (result.success && effective_config.instrument != InstrumentType::Organ) {
    std::vector<Tick> cadence_ticks;
    // Extract cadence ticks from timeline if available.
    if (result.timeline.size() > 0) {
      const auto& events = result.timeline.events();
      for (size_t idx = 1; idx < events.size(); ++idx) {
        if (events[idx].chord.degree == ChordDegree::I &&
            events[idx - 1].chord.degree == ChordDegree::V) {
          cadence_ticks.push_back(events[idx].tick);
        }
      }
    }
    for (auto& track : result.tracks) {
      applyVelocityCurve(track.notes, effective_config.instrument, cadence_ticks);
    }
  }

  return result;
}

InstrumentType defaultInstrumentForForm(FormType form) {
  switch (form) {
    case FormType::Fugue:
    case FormType::PreludeAndFugue:
    case FormType::TrioSonata:
    case FormType::ChoralePrelude:
    case FormType::ToccataAndFugue:
    case FormType::Passacaglia:
    case FormType::FantasiaAndFugue:
      return InstrumentType::Organ;

    case FormType::CelloPrelude:
      return InstrumentType::Cello;

    case FormType::Chaconne:
      return InstrumentType::Violin;
  }

  return InstrumentType::Organ;
}

InstrumentType instrumentTypeFromString(const std::string& str) {
  if (str == "organ") return InstrumentType::Organ;
  if (str == "harpsichord") return InstrumentType::Harpsichord;
  if (str == "piano") return InstrumentType::Piano;
  if (str == "violin") return InstrumentType::Violin;
  if (str == "cello") return InstrumentType::Cello;
  if (str == "guitar") return InstrumentType::Guitar;
  return InstrumentType::Organ;
}

}  // namespace bach
