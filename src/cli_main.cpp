/// @file
/// @brief CLI entry point for the Bach MIDI generator.

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

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
};

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
  std::printf("  --json           JSON output\n");
  std::printf("  --analyze        Generate + analysis\n");
  std::printf("  --strict         No retry\n");
  std::printf("  --verbose-retry  Log retry process\n");
  std::printf("  -o FILE          Output file path\n");
  std::printf("  --help           Show this help\n");
  std::printf("\nForms:\n");
  std::printf("  fugue, prelude_and_fugue, trio_sonata, chorale_prelude\n");
  std::printf("  toccata_and_fugue, passacaglia, fantasia_and_fugue\n");
  std::printf("  cello_prelude, chaconne\n");
}

/// @brief Parse command-line arguments into CliOptions.
/// @param argc Argument count from main().
/// @param argv Argument vector from main().
/// @param opts Output structure populated with parsed values.
/// @return False if --help was requested (caller should exit cleanly).
bool parseArgs(int argc, char* argv[], CliOptions& opts) {
  for (int idx = 1; idx < argc; ++idx) {
    if (std::strcmp(argv[idx], "--help") == 0 ||
        std::strcmp(argv[idx], "-h") == 0) {
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

  return config;
}

}  // namespace

int main(int argc, char* argv[]) {
  CliOptions opts;
  if (!parseArgs(argc, argv, opts)) {
    return 0;
  }

  bach::GeneratorConfig config = buildGeneratorConfig(opts);

  std::printf("bach_cli v0.2.0\n");
  std::printf("Form:       %s\n", bach::formTypeToString(config.form));
  std::printf("Key:        %s\n", bach::keySignatureToString(config.key).c_str());
  std::printf("Voices:     %d\n", config.num_voices);
  std::printf("BPM:        %d\n", config.bpm);
  std::printf("Character:  %s\n", bach::subjectCharacterToString(config.character));
  std::printf("Instrument: %s\n", bach::instrumentTypeToString(config.instrument));
  std::printf("Seed:       %u%s\n", config.seed, config.seed == 0 ? " (auto)" : "");
  std::printf("\n");

  bach::GeneratorResult result = bach::generate(config);

  if (result.success) {
    std::printf("Generated: %s\n", result.form_description.c_str());
    std::printf("Seed used: %u\n", result.seed_used);
    std::printf("Duration:  %u ticks (%.1f bars)\n",
                result.total_duration_ticks,
                static_cast<float>(result.total_duration_ticks) /
                    static_cast<float>(bach::kTicksPerBar));
    std::printf("Tracks:    %zu\n", result.tracks.size());

    size_t total_notes = 0;
    for (const auto& track : result.tracks) {
      total_notes += track.notes.size();
    }
    std::printf("Notes:     %zu\n", total_notes);

    bach::MidiWriter writer;
    writer.build(result.tracks, config.bpm, config.key.tonic);
    if (writer.writeToFile(opts.output)) {
      std::printf("\nOutput:    %s\n", opts.output.c_str());
    } else {
      std::fprintf(stderr, "Error: failed to write %s\n", opts.output.c_str());
      return 1;
    }
  } else {
    std::fprintf(stderr, "Error: %s\n", result.error_message.c_str());
    return 1;
  }

  return 0;
}
