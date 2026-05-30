/// @file
/// @brief CLI entry point for the Bach MIDI generator.

#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include "analysis/analysis_runner.h"
#include "composer/composer.h"
#include "composer/harness_fixture.h"
#include "composer/json_export.h"
#include "core/basic_types.h"
#include "generator.h"
#include "harmony/key.h"
#include "midi/midi_writer.h"

namespace {

/// @brief Command-line options parsed from argv.
struct CliOptions {
  uint32_t seed = 0;
  bach::KeySignature key = {bach::Key::C, false};
  bach::FormType form = bach::FormType::PreludeAndFugue;
  bach::SubjectCharacter character = bach::SubjectCharacter::Severe;
  bach::InstrumentType instrument = bach::InstrumentType::Organ;
  uint8_t voices = 3;
  uint16_t bpm = 72;
  std::string output = "output.mid";
  bool json_output = false;
  bool analyze = false;
  bool strict = false;
  bool verbose = false;
  bool instrument_specified = false;
  bach::DurationScale scale = bach::DurationScale::Short;
  bach::ToccataArchetype toccata_archetype = bach::ToccataArchetype::Dramaticus;
  bool toccata_style_specified = false;
  uint16_t target_bars = 0;
  bool scale_specified = false;
  // When set, bypass the legacy generator and run the new Composer
  // engine against the harness fixture catalog. The phase governs the
  // layout (voices / bars / subject / answer). Seed is reused.
  bool composer_mode = false;
  bach::composer::HarnessPhase composer_phase = bach::composer::HarnessPhase::Phase6;
};

bach::composer::HarnessPhase parseComposerPhase(const char* val) {
  if (std::strcmp(val, "Phase3") == 0 || std::strcmp(val, "phase3") == 0 ||
      std::strcmp(val, "3") == 0) {
    return bach::composer::HarnessPhase::Phase3;
  }
  if (std::strcmp(val, "Phase35") == 0 || std::strcmp(val, "phase35") == 0 ||
      std::strcmp(val, "3.5") == 0) {
    return bach::composer::HarnessPhase::Phase35;
  }
  if (std::strcmp(val, "Phase4") == 0 || std::strcmp(val, "phase4") == 0 ||
      std::strcmp(val, "4") == 0) {
    return bach::composer::HarnessPhase::Phase4;
  }
  if (std::strcmp(val, "Phase4Sus") == 0 || std::strcmp(val, "phase4sus") == 0 ||
      std::strcmp(val, "4sus") == 0) {
    return bach::composer::HarnessPhase::Phase4Sus;
  }
  if (std::strcmp(val, "Phase5") == 0 || std::strcmp(val, "phase5") == 0 ||
      std::strcmp(val, "5") == 0) {
    return bach::composer::HarnessPhase::Phase5;
  }
  if (std::strcmp(val, "Phase6Episode") == 0 || std::strcmp(val, "phase6episode") == 0 ||
      std::strcmp(val, "6ep") == 0 || std::strcmp(val, "6episode") == 0) {
    return bach::composer::HarnessPhase::Phase6Episode;
  }
  if (std::strcmp(val, "Phase6Tonal") == 0 || std::strcmp(val, "phase6tonal") == 0 ||
      std::strcmp(val, "6tonal") == 0 || std::strcmp(val, "p6tonal") == 0) {
    return bach::composer::HarnessPhase::Phase6Tonal;
  }
  if (std::strcmp(val, "Phase7") == 0 || std::strcmp(val, "phase7") == 0 ||
      std::strcmp(val, "7") == 0 || std::strcmp(val, "p7") == 0) {
    return bach::composer::HarnessPhase::Phase7;
  }
  if (std::strcmp(val, "Phase8") == 0 || std::strcmp(val, "phase8") == 0 ||
      std::strcmp(val, "8") == 0 || std::strcmp(val, "p8") == 0) {
    return bach::composer::HarnessPhase::Phase8;
  }
  if (std::strcmp(val, "Phase9") == 0 || std::strcmp(val, "phase9") == 0 ||
      std::strcmp(val, "9") == 0 || std::strcmp(val, "p9") == 0) {
    return bach::composer::HarnessPhase::Phase9;
  }
  if (std::strcmp(val, "Phase10") == 0 || std::strcmp(val, "phase10") == 0 ||
      std::strcmp(val, "10") == 0 || std::strcmp(val, "p10") == 0) {
    return bach::composer::HarnessPhase::Phase10;
  }
  if (std::strcmp(val, "Phase11") == 0 || std::strcmp(val, "phase11") == 0 ||
      std::strcmp(val, "11") == 0 || std::strcmp(val, "p11") == 0) {
    return bach::composer::HarnessPhase::Phase11;
  }
  if (std::strcmp(val, "Phase12") == 0 || std::strcmp(val, "phase12") == 0 ||
      std::strcmp(val, "12") == 0 || std::strcmp(val, "p12") == 0) {
    return bach::composer::HarnessPhase::Phase12;
  }
  if (std::strcmp(val, "Phase13") == 0 || std::strcmp(val, "phase13") == 0 ||
      std::strcmp(val, "13") == 0 || std::strcmp(val, "p13") == 0) {
    return bach::composer::HarnessPhase::Phase13;
  }
  if (std::strcmp(val, "Phase14") == 0 || std::strcmp(val, "phase14") == 0 ||
      std::strcmp(val, "14") == 0 || std::strcmp(val, "p14") == 0) {
    return bach::composer::HarnessPhase::Phase14;
  }
  return bach::composer::HarnessPhase::Phase6;
}

/// @brief Print usage information to stdout.
void printUsage() {
  std::printf("bach_cli - J.S. Bach Instrumental MIDI Generator\n\n");
  std::printf("Usage: bach_cli [options]\n\n");
  std::printf("Options:\n");
  std::printf("  --seed N         Random seed (0 = auto)\n");
  std::printf("  --key KEY        Key (e.g. g_minor, C_major, d_minor, F_major)\n");
  std::printf("  --form FORM      Form type\n");
  std::printf("  --character CH   Subject character: severe, playful, noble, restless\n");
  std::printf("  --instrument INS Instrument: organ, harpsichord, piano, violin, cello, guitar\n");
  std::printf("  --voices N       Number of voices (2-5)\n");
  std::printf("  --bpm N          BPM (40-200)\n");
  std::printf("  --scale SCALE    Duration scale: short, medium, long, full\n");
  std::printf("                   Default: medium for fugue, short otherwise\n");
  std::printf("  --bars N         Target bar count (overrides --scale)\n");
  std::printf(
      "  --composer-phase P  Bypass legacy generator; run Composer with phase\n"
      "                   {Phase3|Phase35|Phase4|Phase4Sus|Phase5|Phase6|Phase6Episode|\n"
      "                    Phase6Tonal|Phase7|Phase8|Phase9|Phase10|Phase11|Phase12|\n"
      "                    Phase13|Phase14}. Seed reused.\n");
  std::printf(
      "  --toccata-style  Toccata archetype: dramaticus, perpetuus, concertato, sectionalis\n");
  std::printf("  --json           JSON output\n");
  std::printf("  --analyze        Generate + analysis\n");
  std::printf("  --strict         No retry\n");
  std::printf("  --verbose-retry  Log retry process\n");
  std::printf("  -o FILE          Output file path\n");
  std::printf("  --help           Show this help\n");
  std::printf("\nForms:\n");
  std::printf("  fugue, prelude_and_fugue, trio_sonata, chorale_prelude\n");
  std::printf("  toccata_and_fugue, passacaglia, fantasia_and_fugue\n");
  std::printf("  cello_prelude, chaconne, goldberg_variations\n");
}

/// @brief Parse command-line arguments into CliOptions.
/// @param argc Argument count from main().
/// @param argv Argument vector from main().
/// @param opts Output structure populated with parsed values.
/// @return False if --help was requested (caller should exit cleanly).
bool parseArgs(int argc, char* argv[], CliOptions& opts) {
  for (int idx = 1; idx < argc; ++idx) {
    if (std::strcmp(argv[idx], "--help") == 0 || std::strcmp(argv[idx], "-h") == 0) {
      printUsage();
      return false;
    }
    if (std::strcmp(argv[idx], "--seed") == 0 && idx + 1 < argc) {
      opts.seed = static_cast<uint32_t>(std::atoi(argv[++idx]));
    } else if (std::strcmp(argv[idx], "--bpm") == 0 && idx + 1 < argc) {
      opts.bpm = static_cast<uint16_t>(std::atoi(argv[++idx]));
    } else if (std::strcmp(argv[idx], "--voices") == 0 && idx + 1 < argc) {
      opts.voices = static_cast<uint8_t>(std::atoi(argv[++idx]));
    } else if (std::strcmp(argv[idx], "-o") == 0 && idx + 1 < argc) {
      opts.output = argv[++idx];
    } else if (std::strcmp(argv[idx], "--json") == 0) {
      opts.json_output = true;
    } else if (std::strcmp(argv[idx], "--analyze") == 0) {
      opts.analyze = true;
    } else if (std::strcmp(argv[idx], "--strict") == 0) {
      opts.strict = true;
    } else if (std::strcmp(argv[idx], "--verbose-retry") == 0) {
      opts.verbose = true;
    } else if (std::strcmp(argv[idx], "--form") == 0 && idx + 1 < argc) {
      opts.form = bach::formTypeFromString(argv[++idx]);
    } else if (std::strcmp(argv[idx], "--character") == 0 && idx + 1 < argc) {
      ++idx;
      const char* val = argv[idx];
      if (std::strcmp(val, "severe") == 0) {
        opts.character = bach::SubjectCharacter::Severe;
      } else if (std::strcmp(val, "playful") == 0) {
        opts.character = bach::SubjectCharacter::Playful;
      } else if (std::strcmp(val, "noble") == 0) {
        opts.character = bach::SubjectCharacter::Noble;
      } else if (std::strcmp(val, "restless") == 0) {
        opts.character = bach::SubjectCharacter::Restless;
      }
    } else if (std::strcmp(argv[idx], "--key") == 0 && idx + 1 < argc) {
      opts.key = bach::keySignatureFromString(argv[++idx]);
    } else if (std::strcmp(argv[idx], "--instrument") == 0 && idx + 1 < argc) {
      opts.instrument = bach::instrumentTypeFromString(argv[++idx]);
      opts.instrument_specified = true;
    } else if (std::strcmp(argv[idx], "--scale") == 0 && idx + 1 < argc) {
      opts.scale = bach::durationScaleFromString(argv[++idx]);
      opts.scale_specified = true;
    } else if (std::strcmp(argv[idx], "--bars") == 0 && idx + 1 < argc) {
      opts.target_bars = static_cast<uint16_t>(std::atoi(argv[++idx]));
    } else if (std::strcmp(argv[idx], "--toccata-style") == 0 && idx + 1 < argc) {
      opts.toccata_archetype = bach::toccataArchetypeFromString(argv[++idx]);
      opts.toccata_style_specified = true;
    } else if (std::strcmp(argv[idx], "--composer-phase") == 0 && idx + 1 < argc) {
      opts.composer_mode = true;
      opts.composer_phase = parseComposerPhase(argv[++idx]);
    }
  }
  return true;
}

/// @brief Build a GeneratorConfig from parsed CLI options.
/// @param opts Parsed command-line options.
/// @return GeneratorConfig ready for generation.
bach::GeneratorConfig buildGeneratorConfig(const CliOptions& opts) {
  bach::GeneratorConfig config;
  config.form = opts.form;
  config.key = opts.key;
  config.num_voices = opts.voices;
  config.bpm = opts.bpm;
  config.seed = opts.seed;
  config.character = opts.character;
  config.json_output = opts.json_output;
  config.analyze = opts.analyze;
  config.strict = opts.strict;

  // Auto-detect instrument from form if not explicitly specified.
  if (opts.instrument_specified) {
    config.instrument = opts.instrument;
  } else {
    config.instrument = bach::defaultInstrumentForForm(opts.form);
  }

  config.scale = opts.scale;
  if (!opts.scale_specified && opts.form == bach::FormType::Fugue) {
    config.scale = bach::DurationScale::Medium;
  }
  config.target_bars = opts.target_bars;
  config.toccata_archetype = opts.toccata_archetype;
  config.toccata_archetype_auto = !opts.toccata_style_specified;

  return config;
}

const char* harnessPhaseToString(bach::composer::HarnessPhase p) {
  switch (p) {
    case bach::composer::HarnessPhase::Phase3:
      return "Phase3";
    case bach::composer::HarnessPhase::Phase35:
      return "Phase3.5";
    case bach::composer::HarnessPhase::Phase4:
      return "Phase4";
    case bach::composer::HarnessPhase::Phase4Sus:
      return "Phase4Sus";
    case bach::composer::HarnessPhase::Phase5:
      return "Phase5";
    case bach::composer::HarnessPhase::Phase6:
      return "Phase6";
    case bach::composer::HarnessPhase::Phase6Episode:
      return "Phase6Episode";
    case bach::composer::HarnessPhase::Phase6Tonal:
      return "Phase6Tonal";
    case bach::composer::HarnessPhase::Phase7:
      return "Phase7";
    case bach::composer::HarnessPhase::Phase8:
      return "Phase8";
    case bach::composer::HarnessPhase::Phase9:
      return "Phase9";
    case bach::composer::HarnessPhase::Phase10:
      return "Phase10";
    case bach::composer::HarnessPhase::Phase11:
      return "Phase11";
    case bach::composer::HarnessPhase::Phase12:
      return "Phase12";
    case bach::composer::HarnessPhase::Phase13:
      return "Phase13";
    case bach::composer::HarnessPhase::Phase14:
      return "Phase14";
  }
  return "Phase?";
}

int runComposerMode(const CliOptions& opts) {
  // Composer mode: harness fixture catalog drives Material / HarmonicPlan /
  // VoicePlan. Output goes through the same MidiWriter as the legacy path so
  // BPM / key transposition / file write are uniform.
  const auto layout = bach::composer::phaseSpec(opts.composer_phase);
  const char* phase_name = harnessPhaseToString(opts.composer_phase);
  std::printf("bach_cli v0.2.0 (composer mode)\n");
  std::printf("Phase:      %s (%uv / %u bar)\n", phase_name, layout.voices, layout.bars);
  std::printf("Seed:       %u\n\n", opts.seed);

  const bach::composer::HarnessFixture fx =
      bach::composer::buildHarnessFixture(opts.composer_phase, static_cast<int>(opts.seed));
  const auto result = bach::composer::Composer{}.run(fx.material, fx.harmony, fx.voice_plan);

  if (result.validation.status != bach::composer::ValidationStatus::Ok) {
    std::fprintf(stderr, "Composer validation failed: %zu rule violations\n",
                 result.validation.failures.size());
    for (const auto& f : result.validation.failures) {
      std::fprintf(stderr, "  - %s (span %u)\n", f.rule_id.c_str(),
                   static_cast<unsigned>(f.span_id));
    }
    return 1;
  }

  std::printf("Generated: Composer %s exposition\n", phase_name);
  std::printf("Notes:     %zu  Tracks: %zu\n", result.notes.size(), result.tracks.size());

  std::vector<bach::TempoEvent> tempo_events;
  tempo_events.push_back({0, opts.bpm});

  bach::MidiWriter writer;
  writer.build(result.tracks, tempo_events, opts.key.tonic);
  if (!writer.writeToFile(opts.output)) {
    std::fprintf(stderr, "Error: failed to write %s\n", opts.output.c_str());
    return 1;
  }
  std::printf("Output:    %s\n", opts.output.c_str());

  if (opts.json_output) {
    std::string json_path = opts.output;
    auto dot_pos = json_path.rfind('.');
    if (dot_pos != std::string::npos) {
      json_path = json_path.substr(0, dot_pos) + ".json";
    } else {
      json_path += ".json";
    }
    const std::string generated = bach::composer::emitGeneratedJson(result.notes);
    const std::string provenance = bach::composer::emitProvenanceJson(result.provenance);
    std::ofstream f(json_path);
    if (f.is_open()) {
      f << generated;
      f.close();
      std::printf("JSON:      %s\n", json_path.c_str());
    } else {
      std::fprintf(stderr, "Warning: failed to write %s\n", json_path.c_str());
    }
    std::string provenance_path = json_path;
    const auto provenance_dot_pos = provenance_path.rfind('.');
    if (provenance_dot_pos != std::string::npos) {
      provenance_path = provenance_path.substr(0, provenance_dot_pos) + ".provenance.json";
    } else {
      provenance_path += ".provenance.json";
    }
    std::ofstream pf(provenance_path);
    if (pf.is_open()) {
      pf << provenance;
      pf.close();
      std::printf("Provenance:%s\n", provenance_path.c_str());
    } else {
      std::fprintf(stderr, "Warning: failed to write %s\n", provenance_path.c_str());
    }
  }
  return 0;
}

}  // namespace

int main(int argc, char* argv[]) {
  CliOptions opts;
  if (!parseArgs(argc, argv, opts)) {
    return 0;
  }

  if (opts.composer_mode) {
    return runComposerMode(opts);
  }

  bach::GeneratorConfig config = buildGeneratorConfig(opts);

  std::printf("bach_cli v0.2.0\n");
  std::printf("Form:       %s\n", bach::formTypeToString(config.form));
  std::printf("Key:        %s\n", bach::keySignatureToString(config.key).c_str());
  std::printf("Voices:     %d\n", config.num_voices);
  std::printf("BPM:        %d\n", config.bpm);
  std::printf("Character:  %s\n", bach::subjectCharacterToString(config.character));
  std::printf("Instrument: %s\n", bach::instrumentTypeToString(config.instrument));
  std::printf("Scale:      %s\n", bach::durationScaleToString(config.scale));
  if (config.target_bars > 0) {
    std::printf("Bars:       %u (override)\n", config.target_bars);
  }
  std::printf("Seed:       %u%s\n", config.seed, config.seed == 0 ? " (auto)" : "");
  std::printf("\n");

  bach::GeneratorResult result = bach::generate(config);

  if (result.success) {
    std::printf("Generated: %s\n", result.form_description.c_str());
    std::printf("Seed used: %u\n", result.seed_used);
    std::printf(
        "Duration:  %u ticks (%.1f bars)\n", result.total_duration_ticks,
        static_cast<float>(result.total_duration_ticks) / static_cast<float>(bach::kTicksPerBar));
    std::printf("Tracks:    %zu\n", result.tracks.size());

    size_t total_notes = 0;
    for (const auto& track : result.tracks) {
      total_notes += track.notes.size();
    }
    std::printf("Notes:     %zu\n", total_notes);

    bach::MidiWriter writer;
    writer.build(result.tracks, result.tempo_events, config.key.tonic);
    if (writer.writeToFile(opts.output)) {
      std::printf("\nOutput:    %s\n", opts.output.c_str());
    } else {
      std::fprintf(stderr, "Error: failed to write %s\n", opts.output.c_str());
      return 1;
    }

    // Write events JSON if requested.
    if (opts.json_output) {
      std::string json_path = opts.output;
      auto dot_pos = json_path.rfind('.');
      if (dot_pos != std::string::npos) {
        json_path = json_path.substr(0, dot_pos) + ".json";
      } else {
        json_path += ".json";
      }

      std::string json_str = bach::buildEventsJson(result, config);
      std::ofstream json_file(json_path);
      if (json_file.is_open()) {
        json_file << json_str;
        json_file.close();
        std::printf("JSON:      %s\n", json_path.c_str());
      } else {
        std::fprintf(stderr, "Warning: failed to write %s\n", json_path.c_str());
      }
    }

    // Run analysis if requested.
    if (opts.analyze) {
      const bach::HarmonicTimeline* gen_tl =
          result.generation_timeline.size() > 0 ? &result.generation_timeline : nullptr;
      bach::AnalysisReport analysis = bach::runAnalysis(
          result.tracks, config.form, config.num_voices, result.timeline, config.key, gen_tl);

      std::printf("\n%s", analysis.toTextSummary(config.form, config.num_voices).c_str());

      if (opts.json_output) {
        // Write analysis JSON alongside the MIDI output.
        std::string json_path = opts.output;
        auto dot_pos = json_path.rfind('.');
        if (dot_pos != std::string::npos) {
          json_path = json_path.substr(0, dot_pos) + "_analysis.json";
        } else {
          json_path += "_analysis.json";
        }

        std::ofstream json_file(json_path);
        if (json_file.is_open()) {
          json_file << analysis.toJson(config.form, config.num_voices);
          json_file.close();
          std::printf("Analysis:  %s\n", json_path.c_str());
        } else {
          std::fprintf(stderr, "Warning: failed to write %s\n", json_path.c_str());
        }
      }
    }
  } else {
    std::fprintf(stderr, "Error: %s\n", result.error_message.c_str());
    return 1;
  }

  return 0;
}
