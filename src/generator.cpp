// Implementation of the unified generator that routes to form-specific generators.

#include "generator.h"

#include <algorithm>
#include <array>
#include <cstdio>

#include "core/json_helpers.h"

#include "core/rng_util.h"

#include "forms/chorale_prelude.h"
#include "forms/fantasia.h"
#include "forms/passacaglia.h"
#include "forms/prelude.h"
#include "forms/toccata.h"
#include "forms/trio_sonata.h"
#include "fugue/fugue_config.h"
#include "fugue/fugue_generator.h"
#include "harmony/harmonic_timeline.h"
#include "harmony/modulation_plan.h"
#include "harmony/tempo_map.h"
#include "expression/articulation.h"
#include "instrument/common/impossibility_guard.h"
#include "midi/velocity_curve.h"
#include "forms/goldberg/goldberg_config.h"
#include "solo_string/arch/chaconne_engine.h"
#include "solo_string/flow/harmonic_arpeggio_engine.h"

namespace bach {

namespace {

/// @brief Seed offset applied to the prelude seed so it differs from the fugue seed.
constexpr uint32_t kPreludeSeedOffset = 7919u;



/// @brief Get fugue develop_pairs and episode_bars for a DurationScale.
/// @param scale The duration scale.
/// @param[out] pairs Output: number of Episode+MiddleEntry pairs.
/// @param[out] ep_bars Output: bars per episode.
void fugueDurationParams(DurationScale scale, int& pairs, int& ep_bars) {
  switch (scale) {
    case DurationScale::Short:
      pairs = 2; ep_bars = 2; break;   // 2 pairs for ~18 bars develop section
    case DurationScale::Medium:
      pairs = 3; ep_bars = 3; break;
    case DurationScale::Long:
      pairs = 5; ep_bars = 3; break;   // Reduced from 6 for quality stability
    case DurationScale::Full:
      pairs = 8; ep_bars = 4; break;   // Reduced from 10 for quality stability
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

/// @brief Remove within-track overlaps after merging compound forms.
///
/// Sorts notes by start_tick, removes same-tick duplicates (keeping the longer
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

/// @brief Remove duplicate notes and truncate overlapping notes within each track.
///
/// Sorts notes by (start_tick, voice, duration DESC), deduplicates same-tick +
/// same-voice + same-pitch notes (keeping the longest), and truncates notes
/// that extend past the next same-voice note's start.
///
/// Voice-aware: notes in different voices at the same tick are preserved
/// (e.g., ground bass voice=0 + texture voice=1 in chaconne).
///
/// @param tracks Tracks to clean up (modified in place).
void cleanupTrackOverlaps(std::vector<Track>& tracks) {
  for (auto& track : tracks) {
    auto& notes = track.notes;
    if (notes.size() < 2) continue;

    // Sort by start_tick, then voice, then duration descending.
    std::sort(notes.begin(), notes.end(),
              [](const NoteEvent& a, const NoteEvent& b) {
                if (a.start_tick != b.start_tick) return a.start_tick < b.start_tick;
                if (a.voice != b.voice) return a.voice < b.voice;
                return a.duration > b.duration;
              });

    // Remove same-tick + same-voice + same-pitch duplicates (keep longer).
    notes.erase(
        std::unique(notes.begin(), notes.end(),
                    [](const NoteEvent& a, const NoteEvent& b) {
                      return a.start_tick == b.start_tick &&
                             a.voice == b.voice &&
                             a.pitch == b.pitch;
                    }),
        notes.end());

    // Truncate overlapping notes within the same voice.
    // Per-voice tracking with fixed array (VoiceId is uint8_t, solo string uses 0-1).
    constexpr size_t kMaxCleanupVoices = 8;
    std::array<size_t, kMaxCleanupVoices> prev_index{};
    std::array<bool, kMaxCleanupVoices> has_prev{};
    has_prev.fill(false);

    for (size_t i = 0; i < notes.size(); ++i) {
      VoiceId v = notes[i].voice;
      if (v >= kMaxCleanupVoices) continue;
      if (has_prev[v]) {
        size_t prev_i = prev_index[v];
        // Skip same-tick chord tones (simultaneous notes in the same voice).
        if (notes[prev_i].start_tick != notes[i].start_tick) {
          Tick end_tick = notes[prev_i].start_tick + notes[prev_i].duration;
          if (end_tick > notes[i].start_tick) {
            notes[prev_i].duration = notes[i].start_tick - notes[prev_i].start_tick;
            notes[prev_i].modified_by |=
                static_cast<uint8_t>(NoteModifiedBy::OverlapTrim);
            if (notes[prev_i].duration == 0) notes[prev_i].duration = 1;
          }
        }
      }
      prev_index[v] = i;
      has_prev[v] = true;
    }
  }
}

GeneratorResult generate(const GeneratorConfig& config) {
  GeneratorResult result;

  // Auto-select seed if 0.
  GeneratorConfig effective_config = config;
  if (effective_config.seed == 0) {
    effective_config.seed = rng::generateRandomSeed();
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
      if (effective_config.toccata_archetype_auto) {
        // Auto-select archetype from seed for structural variety.
        constexpr ToccataArchetype kArchetypes[] = {
            ToccataArchetype::Dramaticus,
            ToccataArchetype::Perpetuus,
            ToccataArchetype::Concertato,
            ToccataArchetype::Sectionalis,
        };
        tconfig.archetype = kArchetypes[effective_config.seed % 4];
      } else {
        tconfig.archetype = effective_config.toccata_archetype;
      }
      tconfig.key = effective_config.key;
      tconfig.bpm = effective_config.bpm;
      tconfig.seed = effective_config.seed;
      tconfig.num_voices = effective_config.num_voices;
      tconfig.total_bars = 24;

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
        uint32_t toc_bars = tconfig.total_bars;
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
      cleanupTrackOverlaps(result.tracks);

      result.total_duration_ticks = toc_duration + fugue_duration;

      // Toccata tempo map + offset fugue tempo map.
      result.tempo_events = generateToccataTempoMap(
          toc_result.archetype, toc_result.sections,
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
          "Toccata and Fugue in " + keySignatureToString(effective_config.key) +
          " (" + toccataArchetypeToString(tconfig.archetype) + "), " +
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
      cleanupTrackOverlaps(result.tracks);

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
      // Passacaglia requires at least 4 voices (3 manuals + pedal).
      pconfig.num_voices = std::max(effective_config.num_voices, static_cast<uint8_t>(4));

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
      TrioSonataConfig ts_config;
      ts_config.key = effective_config.key;
      ts_config.seed = effective_config.seed;
      // Fast movements use the user BPM; slow movement uses ~55% (Baroque practice).
      ts_config.bpm_fast = effective_config.bpm;
      ts_config.bpm_slow = std::max(static_cast<uint16_t>(40),
          static_cast<uint16_t>(effective_config.bpm * 55 / 100));

      TrioSonataResult ts_result = generateTrioSonata(ts_config);
      if (!ts_result.success || ts_result.movements.empty()) {
        result.success = false;
        result.seed_used = effective_config.seed;
        result.error_message = "Trio Sonata generation failed";
        break;
      }

      // Concatenate 3 movements sequentially: offset and merge tracks.
      Tick accumulated_ticks = 0;
      result.tracks = std::move(ts_result.movements[0].tracks);
      result.tempo_events.push_back({0, ts_result.movements[0].bpm});
      accumulated_ticks = ts_result.movements[0].total_duration_ticks;

      for (size_t mov = 1; mov < ts_result.movements.size(); ++mov) {
        auto& movement = ts_result.movements[mov];
        result.tempo_events.push_back({accumulated_ticks, movement.bpm});
        offsetTrackNotes(movement.tracks, accumulated_ticks);
        mergeTracksInPlace(result.tracks, movement.tracks);
        accumulated_ticks += movement.total_duration_ticks;
      }
      cleanupTrackOverlaps(result.tracks);

      result.total_duration_ticks = accumulated_ticks;
      result.timeline = HarmonicTimeline::createStandard(
          effective_config.key, result.total_duration_ticks, HarmonicResolution::Bar);
      result.success = true;
      result.seed_used = effective_config.seed;
      result.form_description =
          "Trio Sonata in " + keySignatureToString(effective_config.key);
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

    case FormType::GoldbergVariations: {
      GoldbergConfig gconfig;
      gconfig.key = effective_config.key;
      gconfig.bpm = effective_config.bpm;
      gconfig.seed = effective_config.seed;
      gconfig.instrument = effective_config.instrument;
      gconfig.scale = effective_config.scale;

      GoldbergResult gold_result = generateGoldbergVariations(gconfig);

      if (!gold_result.success) {
        result.success = false;
        result.seed_used = effective_config.seed;
        result.error_message = gold_result.error_message;
        break;
      }

      result.tracks = std::move(gold_result.tracks);
      result.total_duration_ticks = gold_result.total_duration_ticks;
      result.tempo_events = std::move(gold_result.tempo_events);
      result.timeline = gold_result.timeline.size() > 0
          ? std::move(gold_result.timeline)
          : HarmonicTimeline::createStandard(
                effective_config.key, result.total_duration_ticks, HarmonicResolution::Bar);
      result.success = true;
      result.seed_used = gold_result.seed_used;
      result.form_description =
          "Goldberg Variations in " + keySignatureToString(effective_config.key);
      break;
    }
  }

  // Apply articulation as the final processing step before returning.
  // This adjusts note durations (gate ratio) and adds phrase breathing at cadences.
  // Skipped automatically for failed results (applyArticulationToResult checks success).
  applyArticulationToResult(result, effective_config.instrument);

  // Enforce physical impossibility constraints for the target instrument.
  if (result.success) {
    auto guard = createGuard(effective_config.instrument);
    enforceImpossibilityGuard(result.tracks, guard);
  }

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

  // Final overlap cleanup: catches any overlaps introduced by articulation,
  // merging, or other post-processing steps.
  if (result.success) {
    cleanupTrackOverlaps(result.tracks);
  }

  // Pipeline exit: warn if any notes still have Unknown source.
  if (result.success) {
    for (const auto& track : result.tracks) {
      int unknown_count = countUnknownSource(track.notes);
      if (unknown_count > 0) {
        std::fprintf(stderr, "[%s] WARNING: %d notes with Unknown source\n",
                     formTypeToString(effective_config.form), unknown_count);
      }
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

    case FormType::GoldbergVariations:
      return InstrumentType::Harpsichord;
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

std::string buildEventsJson(const GeneratorResult& result, const GeneratorConfig& config) {
  JsonWriter writer;
  writer.beginObject();

  writer.key("form");
  writer.value(std::string(formTypeToString(config.form)));
  writer.key("key");
  writer.value(keySignatureToString(config.key));
  writer.key("bpm");
  writer.value(static_cast<int>(config.bpm));
  writer.key("seed");
  writer.value(result.seed_used);
  writer.key("total_ticks");
  writer.value(result.total_duration_ticks);
  writer.key("total_bars");
  writer.value(static_cast<int>(result.total_duration_ticks / kTicksPerBar));
  writer.key("description");
  writer.value(result.form_description);

  writer.key("tracks");
  writer.beginArray();
  for (const auto& track : result.tracks) {
    writer.beginObject();
    writer.key("name");
    writer.value(track.name);
    writer.key("channel");
    writer.value(static_cast<int>(track.channel));
    writer.key("program");
    writer.value(static_cast<int>(track.program));
    writer.key("note_count");
    writer.value(static_cast<int>(track.notes.size()));

    writer.key("notes");
    writer.beginArray();
    for (const auto& note : track.notes) {
      writer.beginObject();
      writer.key("pitch");
      writer.value(static_cast<int>(note.pitch));
      writer.key("velocity");
      writer.value(static_cast<int>(note.velocity));
      writer.key("start_tick");
      writer.value(note.start_tick);
      writer.key("duration");
      writer.value(note.duration);
      writer.key("voice");
      writer.value(static_cast<int>(note.voice));
      writer.key("source");
      writer.value(std::string(bachNoteSourceToString(note.source)));
      if (note.modified_by != 0) {
        writer.key("modified_by");
        writer.value(noteModifiedByToString(note.modified_by));
      }
      writer.endObject();
    }
    writer.endArray();

    writer.endObject();
  }
  writer.endArray();

  writer.endObject();
  return writer.toString();
}

}  // namespace bach
