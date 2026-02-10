/// @file
/// @brief CLI entry point for the Bach MIDI generator.

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "core/basic_types.h"
#include "fugue/fugue_config.h"
#include "fugue/fugue_generator.h"
#include "midi/midi_writer.h"

namespace {

/// @brief Command-line options parsed from argv.
struct CliOptions {
  uint32_t seed = 0;
  bach::Key key = bach::Key::C;
  bach::FormType form = bach::FormType::PreludeAndFugue;
  bach::SubjectCharacter character = bach::SubjectCharacter::Severe;
  uint8_t voices = 3;
  uint16_t bpm = 72;
  std::string output = "output.mid";
  bool json_output = false;
  bool analyze = false;
  bool strict = false;
  bool verbose = false;
};

/// @brief Print usage information to stdout.
void printUsage() {
  std::printf("bach_cli - J.S. Bach Instrumental MIDI Generator\n\n");
  std::printf("Usage: bach_cli [options]\n\n");
  std::printf("Options:\n");
  std::printf("  --seed N       Random seed (0 = auto)\n");
  std::printf("  --key KEY      Key (e.g. g_minor, C_major)\n");
  std::printf("  --form FORM    Form type\n");
  std::printf("  --character CH Subject character: severe, playful, noble, restless\n");
  std::printf("  --voices N     Number of voices (2-5)\n");
  std::printf("  --bpm N        BPM (40-200)\n");
  std::printf("  --json         JSON output\n");
  std::printf("  --analyze      Generate + analysis\n");
  std::printf("  --strict       No retry\n");
  std::printf("  --verbose-retry Log retry process\n");
  std::printf("  -o FILE        Output file path\n");
  std::printf("  --help         Show this help\n");
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
      // TODO: parse key string (e.g. "g_minor", "C_major")
      ++idx;
    }
  }
  return true;
}

}  // namespace

int main(int argc, char* argv[]) {
  CliOptions opts;
  if (!parseArgs(argc, argv, opts)) {
    return 0;
  }

  std::printf("bach_cli v0.1.0\n");
  std::printf("Form: %s, Key: %s, BPM: %d, Voices: %d\n",
              bach::formTypeToString(opts.form),
              bach::keyToString(opts.key),
              opts.bpm, opts.voices);

  if (opts.form == bach::FormType::Fugue ||
      opts.form == bach::FormType::PreludeAndFugue) {
    bach::FugueConfig fconfig;
    fconfig.key = opts.key;
    fconfig.num_voices = opts.voices;
    fconfig.bpm = opts.bpm;
    fconfig.seed = opts.seed;
    fconfig.character = opts.character;

    bach::FugueResult result = bach::generateFugue(fconfig);

    if (result.success) {
      std::printf("Generated %zu-voice fugue (%d attempts)\n",
                  result.tracks.size(), result.attempts);
      bach::MidiWriter writer;
      writer.build(result.tracks, opts.bpm, opts.key);
      if (writer.writeToFile(opts.output)) {
        std::printf("Output: %s\n", opts.output.c_str());
      } else {
        std::fprintf(stderr, "Error: failed to write %s\n", opts.output.c_str());
        return 1;
      }
    } else {
      std::fprintf(stderr, "Error: %s\n", result.error_message.c_str());
      return 1;
    }
  } else {
    // Other forms: placeholder (generate empty MIDI).
    std::vector<bach::Track> tracks;
    bach::MidiWriter writer;
    writer.build(tracks, opts.bpm, opts.key);
    if (writer.writeToFile(opts.output)) {
      std::printf("Output: %s\n", opts.output.c_str());
    } else {
      std::fprintf(stderr, "Error: failed to write %s\n", opts.output.c_str());
      return 1;
    }
  }

  return 0;
}
