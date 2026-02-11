// Implementation of tonal plan: key modulation schedule for fugue development.

#include "fugue/tonal_plan.h"

#include <algorithm>

#include "core/json_helpers.h"
#include "core/pitch_utils.h"
#include "harmony/chord_types.h"
#include "harmony/harmonic_event.h"
#include "harmony/key.h"

namespace bach {



// ---------------------------------------------------------------------------
// TonalPlan member functions
// ---------------------------------------------------------------------------

Key TonalPlan::keyAtTick(Tick tick) const {
  // Find the last modulation whose tick <= query tick.
  Key active_key = home_key;
  for (const auto& mod : modulations) {
    if (mod.tick <= tick) {
      active_key = mod.target_key;
    } else {
      break;  // Modulations are in chronological order.
    }
  }
  return active_key;
}

std::vector<Key> TonalPlan::keySequence() const {
  std::vector<Key> sequence;

  // Always start with home key if no modulations, or if first modulation
  // is not at tick 0.
  if (modulations.empty() || modulations[0].tick > 0) {
    sequence.push_back(home_key);
  }

  for (const auto& mod : modulations) {
    if (sequence.empty() || mod.target_key != sequence.back()) {
      sequence.push_back(mod.target_key);
    }
  }

  return sequence;
}

size_t TonalPlan::modulationCount() const {
  return modulations.size();
}

HarmonicTimeline TonalPlan::toDetailedTimeline(Tick total_duration) const {
  HarmonicTimeline timeline;
  if (total_duration == 0) return timeline;

  // Build list of region boundaries: each modulation starts a new key region.
  struct Region {
    Key key;
    Tick start;
    Tick end;
  };
  std::vector<Region> regions;

  if (modulations.empty()) {
    regions.push_back({home_key, 0, total_duration});
  } else {
    for (size_t idx = 0; idx < modulations.size(); ++idx) {
      Tick region_start = modulations[idx].tick;
      Tick region_end = (idx + 1 < modulations.size()) ? modulations[idx + 1].tick
                                                        : total_duration;
      if (region_end > region_start) {
        regions.push_back({modulations[idx].target_key, region_start, region_end});
      }
    }
    // If first modulation doesn't start at tick 0, add home key region at front.
    if (!modulations.empty() && modulations[0].tick > 0) {
      regions.insert(regions.begin(), {home_key, 0, modulations[0].tick});
    }
  }

  // For each key region, create a beat-resolution progression and merge.
  for (const auto& region : regions) {
    Tick region_duration = region.end - region.start;
    if (region_duration == 0) continue;

    KeySignature key_sig;
    key_sig.tonic = region.key;
    key_sig.is_minor = is_minor;

    // Alternate between CircleOfFifths and DescendingFifths for harmonic variety.
    // Develop phase regions use DescendingFifths on even indices.
    ProgressionType prog_type = ProgressionType::CircleOfFifths;
    size_t region_idx = static_cast<size_t>(&region - &regions[0]);
    if (region_idx > 0 && region_idx % 2 == 0) {
      prog_type = ProgressionType::DescendingFifths;
    }
    HarmonicTimeline region_timeline = HarmonicTimeline::createProgression(
        key_sig, region_duration, HarmonicResolution::Beat, prog_type);

    // Offset region timeline events to the region start and merge.
    for (const auto& ev : region_timeline.events()) {
      HarmonicEvent offset_ev = ev;
      offset_ev.tick += region.start;
      offset_ev.end_tick += region.start;
      timeline.addEvent(offset_ev);
    }
  }

  return timeline;
}

HarmonicTimeline TonalPlan::toHarmonicTimeline(Tick total_duration) const {
  HarmonicTimeline timeline;
  if (total_duration == 0) return timeline;

  // Generate bar-resolution events. Each bar gets an I-chord in the active key.
  for (Tick bar_tick = 0; bar_tick < total_duration; bar_tick += kTicksPerBar) {
    Key active_key = keyAtTick(bar_tick);

    Chord chord;
    chord.degree = ChordDegree::I;
    chord.quality = is_minor ? ChordQuality::Minor : ChordQuality::Major;
    int root_midi = 4 * 12 + static_cast<int>(active_key);  // Octave 3 (C3 base)
    chord.root_pitch = static_cast<uint8_t>(
        std::min(std::max(root_midi, 0), 127));
    chord.inversion = 0;

    HarmonicEvent ev;
    ev.tick = bar_tick;
    ev.end_tick = bar_tick + kTicksPerBar;
    ev.key = active_key;
    ev.is_minor = is_minor;
    ev.chord = chord;
    ev.bass_pitch = chord.root_pitch;
    ev.weight = 1.0f;
    ev.is_immutable = false;

    timeline.addEvent(ev);
  }

  return timeline;
}

std::string TonalPlan::toJson() const {
  JsonWriter writer;
  writer.beginObject();

  writer.key("home_key");
  writer.value(std::string(keyToString(home_key)));

  writer.key("is_minor");
  writer.value(is_minor);

  writer.key("modulation_count");
  writer.value(static_cast<int>(modulations.size()));

  writer.key("modulations");
  writer.beginArray();
  for (const auto& mod : modulations) {
    writer.beginObject();
    writer.key("target_key");
    writer.value(std::string(keyToString(mod.target_key)));
    writer.key("tick");
    writer.value(mod.tick);
    writer.key("phase");
    writer.value(std::string(fuguePhaseToString(mod.phase)));
    writer.endObject();
  }
  writer.endArray();

  writer.key("key_sequence");
  writer.beginArray();
  for (const auto& key : keySequence()) {
    writer.value(std::string(keyToString(key)));
  }
  writer.endArray();

  writer.endObject();
  return writer.toString();
}

// ---------------------------------------------------------------------------
// Tonal plan generation
// ---------------------------------------------------------------------------

TonalPlan generateTonalPlan(const FugueConfig& config, bool is_minor,
                            Tick total_duration_ticks) {
  TonalPlan plan;
  plan.home_key = config.key;
  plan.is_minor = is_minor;

  // Phase boundaries: roughly 1/3 each.
  // Establish: [0, establish_end)
  // Develop:   [establish_end, resolve_start)
  // Resolve:   [resolve_start, total_duration_ticks)
  Tick establish_end = total_duration_ticks / 3;
  Tick resolve_start = (total_duration_ticks * 2) / 3;

  // Snap to bar boundaries for clean modulation points.
  establish_end = (establish_end / kTicksPerBar) * kTicksPerBar;
  resolve_start = (resolve_start / kTicksPerBar) * kTicksPerBar;

  // Ensure valid boundaries (at least 1 bar per section).
  if (establish_end < kTicksPerBar) {
    establish_end = kTicksPerBar;
  }
  if (resolve_start <= establish_end) {
    resolve_start = establish_end + kTicksPerBar;
  }
  if (resolve_start >= total_duration_ticks && total_duration_ticks > kTicksPerBar * 2) {
    resolve_start = total_duration_ticks - kTicksPerBar;
  }

  // 1. Establish phase: home key at tick 0.
  plan.modulations.push_back({config.key, 0, FuguePhase::Establish});

  // 2. Develop phase: modulations through near-related keys.
  KeySignature home{config.key, is_minor};
  Key dominant = getDominant(home).tonic;
  Key subdominant = getSubdominant(home).tonic;
  Key relative = getRelative(home).tonic;

  // Build the modulation sequence based on mode.
  std::vector<Key> develop_keys;
  if (is_minor) {
    // Minor: home -> relative major -> dominant -> subdominant
    develop_keys = {relative, dominant, subdominant};
  } else {
    // Major: home -> dominant -> relative minor -> subdominant
    develop_keys = {dominant, relative, subdominant};
  }

  // Distribute development modulations evenly across the Develop phase.
  Tick develop_duration = resolve_start - establish_end;
  size_t num_develop_keys = develop_keys.size();

  if (num_develop_keys > 0 && develop_duration >= kTicksPerBar * num_develop_keys) {
    Tick step = develop_duration / static_cast<Tick>(num_develop_keys);
    // Snap step to bar boundaries.
    step = (step / kTicksPerBar) * kTicksPerBar;
    if (step < kTicksPerBar) {
      step = kTicksPerBar;
    }

    Tick current_tick = establish_end;
    for (size_t idx = 0; idx < num_develop_keys; ++idx) {
      plan.modulations.push_back({develop_keys[idx], current_tick, FuguePhase::Develop});
      current_tick += step;
    }
  } else if (num_develop_keys > 0) {
    // Not enough space for all modulations; place at least the first one.
    plan.modulations.push_back({develop_keys[0], establish_end, FuguePhase::Develop});
  }

  // 3. Resolve phase: return to home key.
  plan.modulations.push_back({config.key, resolve_start, FuguePhase::Resolve});

  return plan;
}

TonalPlan generateStructureAlignedTonalPlan(const FugueConfig& config,
                                            const ModulationPlan& mod_plan,
                                            Tick subject_length_ticks,
                                            Tick estimated_duration) {
  TonalPlan plan;
  plan.home_key = config.key;
  plan.is_minor = config.is_minor;

  // Structural section boundaries.
  Tick expo_end = static_cast<Tick>(config.num_voices) * subject_length_ticks;
  Tick episode_duration = kTicksPerBar * static_cast<Tick>(config.episode_bars);
  Tick develop_pair_size = episode_duration + subject_length_ticks;
  int develop_pairs = config.develop_pairs;

  // 1. Establish phase: home key at tick 0.
  plan.modulations.push_back({config.key, 0, FuguePhase::Establish});

  // 2. Develop phase: place key changes at each episode's midpoint.
  for (int pair_idx = 0; pair_idx < develop_pairs; ++pair_idx) {
    Tick episode_start = expo_end + static_cast<Tick>(pair_idx) * develop_pair_size;
    Tick midpoint = episode_start + episode_duration / 2;
    // Snap to bar boundary.
    midpoint = (midpoint / kTicksPerBar) * kTicksPerBar;
    if (midpoint < kTicksPerBar) midpoint = kTicksPerBar;

    Key target_key = mod_plan.getTargetKey(pair_idx, config.key);
    plan.modulations.push_back({target_key, midpoint, FuguePhase::Develop});
  }

  // 3. Return episode: transition back to home key.
  Tick return_start = expo_end + static_cast<Tick>(develop_pairs) * develop_pair_size;
  Tick return_midpoint = return_start + episode_duration / 2;
  return_midpoint = (return_midpoint / kTicksPerBar) * kTicksPerBar;
  if (return_midpoint <= plan.modulations.back().tick) {
    return_midpoint = plan.modulations.back().tick + kTicksPerBar;
  }

  // 4. Resolve phase: home key from the return episode onward.
  plan.modulations.push_back({config.key, return_midpoint, FuguePhase::Resolve});

  // Ensure all ticks are within estimated_duration.
  for (auto& mod : plan.modulations) {
    if (mod.tick >= estimated_duration && estimated_duration > kTicksPerBar) {
      mod.tick = estimated_duration - kTicksPerBar;
    }
  }

  return plan;
}

}  // namespace bach
