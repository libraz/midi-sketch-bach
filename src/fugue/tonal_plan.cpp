// Implementation of tonal plan: key modulation schedule for fugue development.

#include "fugue/tonal_plan.h"

#include <algorithm>

#include "core/json_helpers.h"

namespace bach {

// ---------------------------------------------------------------------------
// Key relationship functions
// ---------------------------------------------------------------------------

Key getDominantKey(Key key) {
  // Dominant = perfect 5th above = +7 semitones.
  return static_cast<Key>((static_cast<uint8_t>(key) + 7) % 12);
}

Key getSubdominantKey(Key key) {
  // Subdominant = perfect 4th above = +5 semitones.
  return static_cast<Key>((static_cast<uint8_t>(key) + 5) % 12);
}

Key getRelativeKey(Key key, bool is_home_minor) {
  if (is_home_minor) {
    // Relative major of minor key: 3 semitones UP.
    return static_cast<Key>((static_cast<uint8_t>(key) + 3) % 12);
  }
  // Relative minor of major key: 3 semitones DOWN (= +9 mod 12).
  return static_cast<Key>((static_cast<uint8_t>(key) + 9) % 12);
}

Key getParallelKey(Key key, bool /*is_home_minor*/) {
  // Parallel major/minor share the same tonic pitch class.
  // The Key enum represents pitch class only, so the value is the same.
  // The mode difference (major vs minor) is tracked externally.
  return key;
}

std::vector<Key> getNearRelatedKeys(Key key, bool is_minor) {
  std::vector<Key> result;
  result.reserve(4);

  Key dominant = getDominantKey(key);
  Key subdominant = getSubdominantKey(key);
  Key relative = getRelativeKey(key, is_minor);

  result.push_back(dominant);
  result.push_back(subdominant);
  result.push_back(relative);

  // Add parallel key only if it differs from the ones already present.
  // Since parallel key has the same tonic, it equals home key in our enum,
  // so we skip it if it duplicates home or existing entries. Instead, add
  // the dominant of the relative key for a richer set.
  Key dominant_of_relative = getDominantKey(relative);
  bool already_present = (dominant_of_relative == dominant ||
                          dominant_of_relative == subdominant ||
                          dominant_of_relative == relative ||
                          dominant_of_relative == key);
  if (!already_present) {
    result.push_back(dominant_of_relative);
  }

  return result;
}

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
  Key dominant = getDominantKey(config.key);
  Key subdominant = getSubdominantKey(config.key);
  Key relative = getRelativeKey(config.key, is_minor);

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

}  // namespace bach
