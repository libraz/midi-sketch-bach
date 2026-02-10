// Implementation of the unified generator that routes to form-specific generators.

#include "generator.h"

#include <random>

#include "forms/chorale_prelude.h"
#include "forms/prelude.h"
#include "fugue/fugue_config.h"
#include "fugue/fugue_generator.h"
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

/// @brief Map GeneratorConfig to FugueConfig for fugue-based forms.
/// @param config The unified generator configuration.
/// @return FugueConfig populated from the generator config.
FugueConfig toFugueConfig(const GeneratorConfig& config) {
  FugueConfig fconfig;
  fconfig.key = config.key.tonic;
  fconfig.num_voices = config.num_voices;
  fconfig.bpm = config.bpm;
  fconfig.seed = config.seed;
  fconfig.character = config.character;
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
  result.tempo_events.push_back({0, config.bpm});
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

  // Step 5: Build tempo events (prelude tempo at tick 0, fugue tempo at prelude end).
  result.tempo_events.push_back({0, config.bpm});
  result.tempo_events.push_back({prelude_duration, config.bpm});

  result.total_duration_ticks = prelude_duration + fugue_duration;
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
    case FormType::Fugue:
    case FormType::ToccataAndFugue:
    case FormType::FantasiaAndFugue:
    case FormType::Passacaglia: {
      result = generateFugueForm(effective_config);
      result.seed_used = effective_config.seed;
      return result;
    }

    case FormType::PreludeAndFugue: {
      result = generatePreludeAndFugueForm(effective_config);
      result.seed_used = effective_config.seed;
      return result;
    }

    case FormType::TrioSonata: {
      result.success = false;
      result.seed_used = effective_config.seed;
      result.error_message = "TrioSonata generation not yet implemented";
      result.form_description = "Trio Sonata (stub)";
      return result;
    }

    case FormType::ChoralePrelude: {
      // Validate character-form compatibility before generation.
      if (!isCharacterFormCompatible(effective_config.character, FormType::ChoralePrelude)) {
        result.success = false;
        result.seed_used = effective_config.seed;
        result.error_message =
            "Incompatible character for ChoralePrelude: " +
            std::string(subjectCharacterToString(effective_config.character));
        return result;
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
        return result;
      }

      result.tracks = std::move(cp_result.tracks);
      result.total_duration_ticks = cp_result.total_duration_ticks;
      result.tempo_events.push_back({0, effective_config.bpm});
      result.success = true;
      result.seed_used = effective_config.seed;
      result.form_description =
          "Chorale Prelude in " + keySignatureToString(effective_config.key);
      return result;
    }

    case FormType::CelloPrelude: {
      ArpeggioFlowConfig flow_config;
      flow_config.key = effective_config.key;
      flow_config.bpm = effective_config.bpm;
      flow_config.seed = effective_config.seed;
      flow_config.instrument = effective_config.instrument;

      ArpeggioFlowResult flow_result = generateArpeggioFlow(flow_config);

      if (!flow_result.success) {
        result.success = false;
        result.seed_used = effective_config.seed;
        result.error_message = "CelloPrelude generation failed: " +
                               flow_result.error_message;
        return result;
      }

      result.tracks = std::move(flow_result.tracks);
      result.total_duration_ticks = flow_result.total_duration_ticks;
      result.tempo_events.push_back({0, effective_config.bpm});
      result.success = true;
      result.seed_used = effective_config.seed;
      result.form_description =
          "Cello Prelude in " + keySignatureToString(effective_config.key);
      return result;
    }

    case FormType::Chaconne: {
      result.success = false;
      result.seed_used = effective_config.seed;
      result.error_message = "Chaconne generation not yet implemented";
      result.form_description = "Chaconne (stub)";
      return result;
    }
  }

  // Fallback for unknown form types.
  result.success = false;
  result.seed_used = effective_config.seed;
  result.error_message = "Unknown form type";
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
