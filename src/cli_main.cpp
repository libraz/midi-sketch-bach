/// @file
/// @brief CLI entry point for the Bach MIDI generator.

#include <cctype>
#include <charconv>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

#include "application/composition_service.h"
#include "composer/composer.h"
#include "composer/harness_fixture.h"
#include "composer/json_export.h"
#include "core/basic_types.h"
#include "core/version_info.h"
#include "harmony/key.h"
#include "midi/midi_writer.h"

namespace {

constexpr int kExitUsage = 2;
constexpr int kExitGeneration = 3;
constexpr int kExitOutput = 4;

/// @brief Command-line options parsed from argv.
struct CliOptions {
  uint32_t seed = 0;
  bach::KeySignature key = {bach::Key::C, false};
  bach::FormType form = bach::FormType::Fugue;
  bach::SubjectCharacter character = bach::SubjectCharacter::Severe;
  bach::InstrumentType instrument = bach::InstrumentType::Organ;
  uint16_t bpm = 0;
  std::string output = "output.mid";
  bool json_output = false;
  bool generated_json_output = false;
  bool instrument_specified = false;
  bach::DurationScale scale = bach::DurationScale::Short;
  uint16_t target_bars = 0;
  bool scale_specified = false;
  // When set, bypass the default composer path and run the new Composer engine
  // against the harness fixture catalog. The phase governs the layout (voices /
  // bars / subject / answer). Seed is reused. This mode is a pinned
  // byte-stability contract with the Python closure harness
  // (scripts/bachlib/mirror.py + predictors.py, driven by
  // `python3 scripts/bach_tools.py closure`).
  bool composer_mode = false;
  bool composer_phase_product_option = false;
  bach::composer::HarnessPhase composer_phase = bach::composer::HarnessPhase::FugueExposition3v;
  // Opt-in: route accompanimental inner-voice spans through the scored
  // candidate search instead of replaying their designed counter-line. A
  // measurement knob (off by default); see ComposeRequest::enable_free_counterpoint.
  bool free_counterpoint = false;
};

/// @brief Case-insensitive ASCII string equality.
/// @param lhs Left-hand string.
/// @param rhs Right-hand C string.
/// @return True if the two strings are equal ignoring ASCII letter case.
bool equalsIgnoreCase(const std::string& lhs, const char* rhs) {
  const std::size_t rhs_len = std::strlen(rhs);
  if (lhs.size() != rhs_len) {
    return false;
  }
  for (std::size_t idx = 0; idx < rhs_len; ++idx) {
    const unsigned char left = static_cast<unsigned char>(lhs[idx]);
    const unsigned char right = static_cast<unsigned char>(rhs[idx]);
    if (std::tolower(left) != std::tolower(right)) {
      return false;
    }
  }
  return true;
}

bool parseUnsignedArgument(const char* text, std::uint64_t maximum, std::uint64_t* out) {
  if (text == nullptr || *text == '\0' || out == nullptr) {
    return false;
  }
  std::uint64_t value = 0;
  const char* end = text + std::strlen(text);
  const auto result = std::from_chars(text, end, value, 10);
  if (result.ec != std::errc{} || result.ptr != end || value > maximum) {
    return false;
  }
  *out = value;
  return true;
}

/// @brief Parse a --composer-phase value into a HarnessPhase.
/// @param val The raw flag value.
/// @param recognized Set to true when @p val names a known phase, false
///        otherwise (the return value is then an unused sentinel).
/// @return The matching HarnessPhase, or HarnessPhase::FugueExposition3v when unrecognized.
bach::composer::HarnessPhase parseComposerPhase(const char* val, bool& recognized) {
  recognized = true;
  if (std::strcmp(val, "FugueSubject2v") == 0 || std::strcmp(val, "fugueSubject2v") == 0 ||
      std::strcmp(val, "3") == 0) {
    return bach::composer::HarnessPhase::FugueSubject2v;
  }
  if (std::strcmp(val, "FugueSubject2vShort") == 0 ||
      std::strcmp(val, "fugueSubject2vShort") == 0 || std::strcmp(val, "3.5") == 0) {
    return bach::composer::HarnessPhase::FugueSubject2vShort;
  }
  if (std::strcmp(val, "FugueAnswer2v") == 0 || std::strcmp(val, "fugueAnswer2v") == 0 ||
      std::strcmp(val, "4") == 0) {
    return bach::composer::HarnessPhase::FugueAnswer2v;
  }
  if (std::strcmp(val, "FugueAnswerSuspension") == 0 ||
      std::strcmp(val, "fugueAnswerSuspension") == 0 || std::strcmp(val, "4sus") == 0) {
    return bach::composer::HarnessPhase::FugueAnswerSuspension;
  }
  if (std::strcmp(val, "FugueSubject3v") == 0 || std::strcmp(val, "fugueSubject3v") == 0 ||
      std::strcmp(val, "5") == 0) {
    return bach::composer::HarnessPhase::FugueSubject3v;
  }
  if (std::strcmp(val, "FugueExpositionEpisode") == 0 ||
      std::strcmp(val, "fugueExpositionEpisode") == 0 || std::strcmp(val, "6ep") == 0 ||
      std::strcmp(val, "6episode") == 0) {
    return bach::composer::HarnessPhase::FugueExpositionEpisode;
  }
  if (std::strcmp(val, "FugueExposition3v") == 0 || std::strcmp(val, "fugueExposition3v") == 0 ||
      std::strcmp(val, "6") == 0 || std::strcmp(val, "p6") == 0) {
    return bach::composer::HarnessPhase::FugueExposition3v;
  }
  if (std::strcmp(val, "FugueExpositionTonalAnswer") == 0 ||
      std::strcmp(val, "fugueExpositionTonalAnswer") == 0 || std::strcmp(val, "6tonal") == 0 ||
      std::strcmp(val, "p6tonal") == 0) {
    return bach::composer::HarnessPhase::FugueExpositionTonalAnswer;
  }
  if (std::strcmp(val, "FugueHarmonized") == 0 || std::strcmp(val, "fugueHarmonized") == 0 ||
      std::strcmp(val, "7") == 0 || std::strcmp(val, "p7") == 0) {
    return bach::composer::HarnessPhase::FugueHarmonized;
  }
  if (std::strcmp(val, "FugueModulating") == 0 || std::strcmp(val, "fugueModulating") == 0 ||
      std::strcmp(val, "8") == 0 || std::strcmp(val, "p8") == 0) {
    return bach::composer::HarnessPhase::FugueModulating;
  }
  if (std::strcmp(val, "FugueFortspinnung") == 0 || std::strcmp(val, "fugueFortspinnung") == 0 ||
      std::strcmp(val, "9") == 0 || std::strcmp(val, "p9") == 0) {
    return bach::composer::HarnessPhase::FugueFortspinnung;
  }
  if (std::strcmp(val, "FugueThirdEntry") == 0 || std::strcmp(val, "fugueThirdEntry") == 0 ||
      std::strcmp(val, "10") == 0 || std::strcmp(val, "p10") == 0) {
    return bach::composer::HarnessPhase::FugueThirdEntry;
  }
  if (std::strcmp(val, "FugueDevelopment") == 0 || std::strcmp(val, "fugueDevelopment") == 0 ||
      std::strcmp(val, "11") == 0 || std::strcmp(val, "p11") == 0) {
    return bach::composer::HarnessPhase::FugueDevelopment;
  }
  if (std::strcmp(val, "FugueRhythmic") == 0 || std::strcmp(val, "fugueRhythmic") == 0 ||
      std::strcmp(val, "12") == 0 || std::strcmp(val, "p12") == 0) {
    return bach::composer::HarnessPhase::FugueRhythmic;
  }
  if (std::strcmp(val, "FugueTextured") == 0 || std::strcmp(val, "fugueTextured") == 0 ||
      std::strcmp(val, "13") == 0 || std::strcmp(val, "p13") == 0) {
    return bach::composer::HarnessPhase::FugueTextured;
  }
  if (std::strcmp(val, "FugueComplete") == 0 || std::strcmp(val, "fugueComplete") == 0 ||
      std::strcmp(val, "14") == 0 || std::strcmp(val, "p14") == 0) {
    return bach::composer::HarnessPhase::FugueComplete;
  }
  if (std::strcmp(val, "CelloPrelude") == 0 || std::strcmp(val, "celloPrelude") == 0 ||
      std::strcmp(val, "15") == 0 || std::strcmp(val, "p15") == 0) {
    return bach::composer::HarnessPhase::CelloPrelude;
  }
  if (std::strcmp(val, "Chaconne") == 0 || std::strcmp(val, "chaconne") == 0 ||
      std::strcmp(val, "16") == 0 || std::strcmp(val, "p16") == 0) {
    return bach::composer::HarnessPhase::Chaconne;
  }
  if (std::strcmp(val, "OrganPrelude") == 0 || std::strcmp(val, "organPrelude") == 0 ||
      std::strcmp(val, "17") == 0 || std::strcmp(val, "p17") == 0) {
    return bach::composer::HarnessPhase::OrganPrelude;
  }
  if (std::strcmp(val, "OrganToccata") == 0 || std::strcmp(val, "organToccata") == 0 ||
      std::strcmp(val, "18") == 0 || std::strcmp(val, "p18") == 0) {
    return bach::composer::HarnessPhase::OrganToccata;
  }
  if (std::strcmp(val, "ChoralePrelude") == 0 || std::strcmp(val, "choralePrelude") == 0 ||
      std::strcmp(val, "19") == 0 || std::strcmp(val, "p19") == 0) {
    return bach::composer::HarnessPhase::ChoralePrelude;
  }
  if (std::strcmp(val, "Passacaglia") == 0 || std::strcmp(val, "passacaglia") == 0 ||
      std::strcmp(val, "20") == 0 || std::strcmp(val, "p20") == 0) {
    return bach::composer::HarnessPhase::Passacaglia;
  }
  if (std::strcmp(val, "TrioSonata") == 0 || std::strcmp(val, "trioSonata") == 0 ||
      std::strcmp(val, "21") == 0 || std::strcmp(val, "p21") == 0) {
    return bach::composer::HarnessPhase::TrioSonata;
  }
  if (std::strcmp(val, "Fantasia") == 0 || std::strcmp(val, "fantasia") == 0 ||
      std::strcmp(val, "22") == 0 || std::strcmp(val, "p22") == 0) {
    return bach::composer::HarnessPhase::Fantasia;
  }
  if (std::strcmp(val, "KeyboardSuite") == 0 || std::strcmp(val, "keyboardSuite") == 0 ||
      std::strcmp(val, "23") == 0 || std::strcmp(val, "p23") == 0) {
    return bach::composer::HarnessPhase::KeyboardSuite;
  }
  if (std::strcmp(val, "PreludeAndFugue") == 0 || std::strcmp(val, "preludeAndFugue") == 0 ||
      std::strcmp(val, "24") == 0 || std::strcmp(val, "p24") == 0) {
    return bach::composer::HarnessPhase::PreludeAndFugue;
  }
  if (std::strcmp(val, "GoldbergVariations") == 0 || std::strcmp(val, "goldbergVariations") == 0 ||
      std::strcmp(val, "25") == 0 || std::strcmp(val, "p25") == 0) {
    return bach::composer::HarnessPhase::GoldbergVariations;
  }
  recognized = false;
  return bach::composer::HarnessPhase::FugueExposition3v;
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
  std::printf("  --bpm N          BPM (40-200, default 100)\n");
  std::printf("  --scale SCALE    Duration scale: short, medium, long, full\n");
  std::printf("                   Default: short for every form\n");
  std::printf("  --bars N         Target bar count (overrides --scale)\n");
  std::printf(
      "  --free-counterpoint  Experimental: generate the inner accompaniment\n"
      "                   voice via the scored candidate search instead of its\n"
      "                   designed counter-line (off by default; lowers quality)\n");
  std::printf(
      "  --composer-phase P  (internal) Run the closure harness layout with phase\n"
      "                   "
      "{FugueSubject2v|FugueSubject2vShort|FugueAnswer2v|FugueAnswerSuspension|FugueSubject3v|"
      "FugueExposition3v|FugueExpositionEpisode|\n"
      "                    "
      "FugueExpositionTonalAnswer|FugueHarmonized|FugueModulating|FugueFortspinnung|"
      "FugueThirdEntry|FugueDevelopment|FugueRhythmic|\n"
      "                    "
      "FugueTextured|FugueComplete|CelloPrelude|Chaconne|OrganPrelude|OrganToccata|ChoralePrelude|"
      "\n"
      "                    "
      "Passacaglia|TrioSonata|Fantasia|KeyboardSuite|PreludeAndFugue|GoldbergVariations}. Seed "
      "reused.\n");
  std::printf("  --json           JSON output\n");
  std::printf("  --generated-json Emit generated.v1 + provenance.v1 JSON for scoring\n");
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
/// @param ok Set to false when a parse error occurs (caller exits non-zero).
/// @return False if parsing stopped early (--help requested or a hard error).
bool parseArgs(int argc, char* argv[], CliOptions& opts, bool& ok) {
  ok = true;
  for (int idx = 1; idx < argc; ++idx) {
    const bool requires_value =
        std::strcmp(argv[idx], "--seed") == 0 || std::strcmp(argv[idx], "--bpm") == 0 ||
        std::strcmp(argv[idx], "-o") == 0 || std::strcmp(argv[idx], "--form") == 0 ||
        std::strcmp(argv[idx], "--character") == 0 || std::strcmp(argv[idx], "--key") == 0 ||
        std::strcmp(argv[idx], "--instrument") == 0 || std::strcmp(argv[idx], "--scale") == 0 ||
        std::strcmp(argv[idx], "--bars") == 0 || std::strcmp(argv[idx], "--composer-phase") == 0;
    if (requires_value && idx + 1 >= argc) {
      std::fprintf(stderr, "Error: option '%s' requires a value\n", argv[idx]);
      ok = false;
      return false;
    }
    const bool product_only_option =
        std::strcmp(argv[idx], "--form") == 0 || std::strcmp(argv[idx], "--character") == 0 ||
        std::strcmp(argv[idx], "--instrument") == 0 || std::strcmp(argv[idx], "--bpm") == 0 ||
        std::strcmp(argv[idx], "--scale") == 0 || std::strcmp(argv[idx], "--bars") == 0 ||
        std::strcmp(argv[idx], "--free-counterpoint") == 0;
    opts.composer_phase_product_option = opts.composer_phase_product_option || product_only_option;
    if (std::strcmp(argv[idx], "--help") == 0 || std::strcmp(argv[idx], "-h") == 0) {
      printUsage();
      return false;
    }
    if (std::strcmp(argv[idx], "--seed") == 0 && idx + 1 < argc) {
      const char* value = argv[++idx];
      std::uint64_t seed = 0;
      if (!parseUnsignedArgument(value, std::numeric_limits<std::uint32_t>::max(), &seed)) {
        std::fprintf(stderr, "Error: --seed must be an integer in [0, %u] (got '%s')\n",
                     std::numeric_limits<std::uint32_t>::max(), value);
        ok = false;
        return false;
      }
      opts.seed = static_cast<std::uint32_t>(seed);
    } else if (std::strcmp(argv[idx], "--bpm") == 0 && idx + 1 < argc) {
      const char* value = argv[++idx];
      std::uint64_t bpm = 0;
      if (!parseUnsignedArgument(value, 200, &bpm) || bpm < 40) {
        std::fprintf(stderr, "Error: --bpm must be an integer in [40, 200] (got '%s')\n", value);
        ok = false;
        return false;
      }
      opts.bpm = static_cast<std::uint16_t>(bpm);
    } else if (std::strcmp(argv[idx], "-o") == 0 && idx + 1 < argc) {
      opts.output = argv[++idx];
    } else if (std::strcmp(argv[idx], "--json") == 0) {
      opts.json_output = true;
    } else if (std::strcmp(argv[idx], "--generated-json") == 0) {
      opts.generated_json_output = true;
    } else if (std::strcmp(argv[idx], "--form") == 0 && idx + 1 < argc) {
      const char* val = argv[++idx];
      opts.form = bach::formTypeFromString(val);
      // formTypeFromString silently defaults to Fugue on unknown input; reject
      // any string that does not round-trip back to its canonical form name.
      if (std::strcmp(bach::formTypeToString(opts.form), val) != 0) {
        std::fprintf(stderr, "Error: unknown --form '%s'\n", val);
        ok = false;
        return false;
      }
    } else if (std::strcmp(argv[idx], "--character") == 0 && idx + 1 < argc) {
      const char* val = argv[++idx];
      if (std::strcmp(val, "severe") == 0) {
        opts.character = bach::SubjectCharacter::Severe;
      } else if (std::strcmp(val, "playful") == 0) {
        opts.character = bach::SubjectCharacter::Playful;
      } else if (std::strcmp(val, "noble") == 0) {
        opts.character = bach::SubjectCharacter::Noble;
      } else if (std::strcmp(val, "restless") == 0) {
        opts.character = bach::SubjectCharacter::Restless;
      } else {
        std::fprintf(stderr, "Error: unknown --character '%s'\n", val);
        ok = false;
        return false;
      }
    } else if (std::strcmp(argv[idx], "--key") == 0 && idx + 1 < argc) {
      const char* val = argv[++idx];
      opts.key = bach::keySignatureFromString(val);
      // keySignatureFromString silently defaults to C major on an unrecognized
      // tonic name (the only detectable failure mode: the mode suffix simply
      // selects minor when it reads "minor", major otherwise). Reject input that
      // does not round-trip back to a canonical name, compared case-insensitively
      // so "g_minor" and "C_major" are both accepted.
      if (!equalsIgnoreCase(bach::keySignatureToString(opts.key), val)) {
        std::fprintf(stderr, "Error: unknown --key '%s' (expected e.g. c_major, g_minor)\n", val);
        ok = false;
        return false;
      }
    } else if (std::strcmp(argv[idx], "--instrument") == 0 && idx + 1 < argc) {
      const char* val = argv[++idx];
      opts.instrument = bach::instrumentTypeFromString(val);
      // instrumentTypeFromString silently defaults to Organ on unknown input;
      // reject any string that does not round-trip back to its canonical name.
      if (std::strcmp(bach::instrumentTypeToString(opts.instrument), val) != 0) {
        std::fprintf(stderr, "Error: unknown --instrument '%s'\n", val);
        ok = false;
        return false;
      }
      opts.instrument_specified = true;
    } else if (std::strcmp(argv[idx], "--scale") == 0 && idx + 1 < argc) {
      const char* val = argv[++idx];
      opts.scale = bach::durationScaleFromString(val);
      // durationScaleFromString silently defaults to Short on unknown input;
      // reject any string that does not round-trip back to its canonical name.
      if (std::strcmp(bach::durationScaleToString(opts.scale), val) != 0) {
        std::fprintf(stderr, "Error: unknown --scale '%s'\n", val);
        ok = false;
        return false;
      }
      opts.scale_specified = true;
    } else if (std::strcmp(argv[idx], "--bars") == 0 && idx + 1 < argc) {
      const char* value = argv[++idx];
      std::uint64_t bars = 0;
      if (!parseUnsignedArgument(value, std::numeric_limits<std::uint16_t>::max(), &bars)) {
        std::fprintf(stderr, "Error: --bars must be an integer in [0, %u] (got '%s')\n",
                     std::numeric_limits<std::uint16_t>::max(), value);
        ok = false;
        return false;
      }
      opts.target_bars = static_cast<std::uint16_t>(bars);
    } else if (std::strcmp(argv[idx], "--free-counterpoint") == 0) {
      opts.free_counterpoint = true;
    } else if (std::strcmp(argv[idx], "--composer-phase") == 0 && idx + 1 < argc) {
      bool recognized = false;
      const char* phase_val = argv[++idx];
      opts.composer_phase = parseComposerPhase(phase_val, recognized);
      if (!recognized) {
        std::fprintf(stderr, "Error: unknown --composer-phase value '%s'\n", phase_val);
        ok = false;
        return false;
      }
      opts.composer_mode = true;
    } else {
      // No recognized flag matched (unknown option, or a known flag missing its
      // required value). Reject rather than silently falling through to
      // generation. There is no positional-argument syntax to preserve.
      std::fprintf(stderr, "Error: unknown argument '%s'\n", argv[idx]);
      ok = false;
      return false;
    }
  }
  if (opts.composer_mode && opts.composer_phase_product_option) {
    std::fprintf(
        stderr,
        "Error: --composer-phase is internal and cannot be combined with product options\n");
    ok = false;
    return false;
  }
  return true;
}

const char* harnessPhaseToString(bach::composer::HarnessPhase p) {
  switch (p) {
    case bach::composer::HarnessPhase::FugueSubject2v:
      return "FugueSubject2v";
    case bach::composer::HarnessPhase::FugueSubject2vShort:
      return "FugueSubject2v.5";
    case bach::composer::HarnessPhase::FugueAnswer2v:
      return "FugueAnswer2v";
    case bach::composer::HarnessPhase::FugueAnswerSuspension:
      return "FugueAnswerSuspension";
    case bach::composer::HarnessPhase::FugueSubject3v:
      return "FugueSubject3v";
    case bach::composer::HarnessPhase::FugueExposition3v:
      return "FugueExposition3v";
    case bach::composer::HarnessPhase::FugueExpositionEpisode:
      return "FugueExpositionEpisode";
    case bach::composer::HarnessPhase::FugueExpositionTonalAnswer:
      return "FugueExpositionTonalAnswer";
    case bach::composer::HarnessPhase::FugueHarmonized:
      return "FugueHarmonized";
    case bach::composer::HarnessPhase::FugueModulating:
      return "FugueModulating";
    case bach::composer::HarnessPhase::FugueFortspinnung:
      return "FugueFortspinnung";
    case bach::composer::HarnessPhase::FugueThirdEntry:
      return "FugueThirdEntry";
    case bach::composer::HarnessPhase::FugueDevelopment:
      return "FugueDevelopment";
    case bach::composer::HarnessPhase::FugueRhythmic:
      return "FugueRhythmic";
    case bach::composer::HarnessPhase::FugueTextured:
      return "FugueTextured";
    case bach::composer::HarnessPhase::FugueComplete:
      return "FugueComplete";
    case bach::composer::HarnessPhase::CelloPrelude:
      return "CelloPrelude";
    case bach::composer::HarnessPhase::Chaconne:
      return "Chaconne";
    case bach::composer::HarnessPhase::OrganPrelude:
      return "OrganPrelude";
    case bach::composer::HarnessPhase::OrganToccata:
      return "OrganToccata";
    case bach::composer::HarnessPhase::ChoralePrelude:
      return "ChoralePrelude";
    case bach::composer::HarnessPhase::Passacaglia:
      return "Passacaglia";
    case bach::composer::HarnessPhase::TrioSonata:
      return "TrioSonata";
    case bach::composer::HarnessPhase::Fantasia:
      return "Fantasia";
    case bach::composer::HarnessPhase::KeyboardSuite:
      return "KeyboardSuite";
    case bach::composer::HarnessPhase::PreludeAndFugue:
      return "PreludeAndFugue";
    case bach::composer::HarnessPhase::GoldbergVariations:
      return "GoldbergVariations";
  }
  return "Phase?";
}

std::string deriveJsonPath(const std::string& output);
std::string deriveSuffixedJsonPath(const std::string& output, const char* suffix);

int runComposerMode(const CliOptions& opts) {
  // Composer mode: harness fixture catalog drives Material / HarmonicPlan /
  // VoicePlan. Output goes through the same MidiWriter as the legacy path so
  // BPM / key transposition / file write are uniform.
  const auto layout = bach::composer::phaseSpec(opts.composer_phase);
  const char* phase_name = harnessPhaseToString(opts.composer_phase);
  std::printf("bach_cli v%s (composer mode)\n", BACH_VERSION);
  std::printf("Phase:      %s (%uv / %u bar)\n", phase_name, layout.voices, layout.bars);
  std::printf("Seed:       %u\n\n", opts.seed);

  const bach::composer::HarnessFixture fx =
      bach::composer::buildHarnessFixture(opts.composer_phase, static_cast<int>(opts.seed));
  const auto result = bach::composer::Composer{}.run(fx.material, fx.harmony, fx.voice_plan);

  // --bpm is a product-only option (rejected alongside --composer-phase), so
  // opts.bpm is always the surface-neutral zero sentinel here; resolve it to
  // the shared default rather than handing MidiWriter a rejected tempo of 0.
  const std::uint16_t effective_bpm = opts.bpm != 0 ? opts.bpm : bach::application::kDefaultBpm;

  // Writes generated + provenance JSON next to opts.output. Also used on
  // validation failure so failing spans can be located from the note dump.
  const auto dump_json = [&result, &opts, effective_bpm]() {
    const std::string json_path = deriveJsonPath(opts.output);
    const std::vector<bach::TempoEvent> tempo_events = {{0, effective_bpm}};
    const std::string generated =
        bach::composer::emitGeneratedJson(result.notes, result.validation, tempo_events);
    const std::string provenance = bach::composer::emitProvenanceJson(result.provenance);
    std::ofstream f(json_path);
    if (f.is_open()) {
      f << generated;
      f.close();
      std::printf("JSON:      %s\n", json_path.c_str());
    } else {
      std::fprintf(stderr, "Warning: failed to write %s\n", json_path.c_str());
    }
    const std::string provenance_path = deriveSuffixedJsonPath(opts.output, ".provenance.json");
    std::ofstream pf(provenance_path);
    if (pf.is_open()) {
      pf << provenance;
      pf.close();
      std::printf("Provenance:%s\n", provenance_path.c_str());
    } else {
      std::fprintf(stderr, "Warning: failed to write %s\n", provenance_path.c_str());
    }

    if (result.validation.status != bach::composer::ValidationStatus::Ok) {
      const std::string diagnostic_path = deriveSuffixedJsonPath(opts.output, ".diagnostic.json");
      std::ofstream diagnostic_file(diagnostic_path);
      if (diagnostic_file.is_open()) {
        diagnostic_file << bach::composer::emitDiagnosticJson(result.notes, result.provenance,
                                                              result.validation);
        diagnostic_file.close();
        std::fprintf(stderr, "Diagnostic:%s\n", diagnostic_path.c_str());
      } else {
        std::fprintf(stderr, "Warning: failed to write %s\n", diagnostic_path.c_str());
      }
    }
  };

  if (result.validation.status != bach::composer::ValidationStatus::Ok) {
    std::fprintf(stderr, "Composer validation failed: %zu rule violations\n",
                 result.validation.failures.size());
    for (const auto& f : result.validation.failures) {
      std::fprintf(stderr, "  - %s (span %u)\n", f.rule_id.c_str(),
                   static_cast<unsigned>(f.span_id));
    }
    if (opts.json_output) {
      dump_json();
    }
    return kExitGeneration;
  }

  std::printf("Generated: Composer %s exposition\n", phase_name);
  std::printf("Notes:     %zu  Tracks: %zu\n", result.notes.size(), result.tracks.size());

  std::vector<bach::TempoEvent> tempo_events;
  tempo_events.push_back({0, effective_bpm});

  bach::MidiWriter writer;
  if (writer.build(result.tracks, tempo_events, opts.key) != bach::MidiWriterStatus::Ok) {
    std::fprintf(stderr, "Invalid tempo or meter for MIDI output\n");
    return kExitGeneration;
  }
  if (!writer.writeToFile(opts.output)) {
    std::fprintf(stderr, "Error: failed to write %s\n", opts.output.c_str());
    return kExitOutput;
  }
  std::printf("Output:    %s\n", opts.output.c_str());

  if (opts.json_output) {
    dump_json();
  }
  return 0;
}

/// @brief Derive the output JSON path from the MIDI output path.
/// @param output The MIDI output path (e.g. "song.mid").
/// @return The sibling JSON path (e.g. "song.json").
namespace {

std::string replaceFilenameExtension(const std::string& output, const char* suffix) {
  const std::size_t filename_start = output.find_last_of("/\\") + 1;
  const std::size_t dot_pos = output.find_last_of('.');
  if (dot_pos != std::string::npos && dot_pos > filename_start) {
    return output.substr(0, dot_pos) + suffix;
  }
  return output + suffix;
}

bool hasJsonExtension(const std::string& output) {
  constexpr std::string_view kJsonExtension = ".json";
  return output.size() >= kJsonExtension.size() &&
         output.compare(output.size() - kJsonExtension.size(), kJsonExtension.size(),
                        kJsonExtension) == 0;
}

}  // namespace

std::string deriveJsonPath(const std::string& output) {
  // `-o song.json --json` must not reopen the active MIDI path for JSON: keep
  // the caller's requested output intact and make the event sidecar explicit.
  if (hasJsonExtension(output)) {
    return output + ".events.json";
  }
  return replaceFilenameExtension(output, ".json");
}

std::string deriveSuffixedJsonPath(const std::string& output, const char* suffix) {
  // As above, never let a generated/provenance sidecar collide with an output
  // whose user-provided filename already ends in `.json`.
  if (hasJsonExtension(output)) {
    return output + suffix;
  }
  return replaceFilenameExtension(output, suffix);
}

/// @brief Run the default composer pipeline (form director) and emit output.
/// @param opts Parsed command-line options.
/// @return 0 on success, 3 on generation failure, or 4 on output failure.
int runDefaultMode(const CliOptions& opts) {
  bach::DurationScale scale = opts.scale;

  bach::application::CompositionRequest request;
  request.form = opts.form;
  request.key = opts.key;
  request.character = opts.character;
  request.instrument = opts.instrument;
  request.instrument_specified = opts.instrument_specified;
  request.scale = scale;
  request.target_bars = opts.target_bars;
  request.bpm = opts.bpm;
  request.seed = opts.seed;
  request.enable_free_counterpoint = opts.free_counterpoint;

  bach::application::CompositionProduct product;
  const auto status = bach::application::compose(request, &product);
  if (status == bach::application::CompositionStatus::IncompatibleCharacter) {
    std::fprintf(stderr, "Error: character '%s' is incompatible with form '%s'\n",
                 bach::subjectCharacterToString(opts.character), bach::formTypeToString(opts.form));
    return kExitGeneration;
  }
  if (status == bach::application::CompositionStatus::IncompatibleInstrument) {
    std::fprintf(stderr, "Error: instrument '%s' is incompatible with form '%s'\n",
                 bach::instrumentTypeToString(opts.instrument), bach::formTypeToString(opts.form));
    return kExitGeneration;
  }
  if (status == bach::application::CompositionStatus::FreeCounterpointUnavailable) {
    std::fprintf(
        stderr,
        "Error: free counterpoint is unavailable for form '%s' (no eligible secondary voice)\n",
        bach::formTypeToString(opts.form));
    return kExitGeneration;
  }
  if (status == bach::application::CompositionStatus::GenerationFailed ||
      status == bach::application::CompositionStatus::FinalValidationFailed) {
    const auto& report = status == bach::application::CompositionStatus::GenerationFailed
                             ? product.composition.validation
                             : product.final_validation;
    std::fprintf(stderr, "Error: composer validation failed: %zu rule violations\n",
                 report.failures.size());
    for (const auto& f : report.failures) {
      std::fprintf(stderr, "  - %s (span %u)\n", f.rule_id.c_str(),
                   static_cast<unsigned>(f.span_id));
    }
    if (opts.generated_json_output && !product.diagnostic_json.empty()) {
      const std::string diagnostic_path = deriveSuffixedJsonPath(opts.output, ".diagnostic.json");
      std::ofstream diagnostic_file(diagnostic_path);
      if (diagnostic_file.is_open()) {
        diagnostic_file << product.diagnostic_json;
        std::fprintf(stderr, "Diagnostic (failed run):%s\n", diagnostic_path.c_str());
      }
    }
    return kExitGeneration;
  }
  if (status != bach::application::CompositionStatus::Ok) {
    std::fprintf(stderr, "Error: composition service failed (status %u)\n",
                 static_cast<unsigned>(status));
    return kExitGeneration;
  }

  std::printf("bach_cli v%s\n", BACH_VERSION);
  std::printf("Form:       %s\n", bach::formTypeToString(product.form));
  std::printf("Key:        %s\n", bach::keySignatureToString(product.key).c_str());
  std::printf("Voices:     %zu\n", product.composition.tracks.size());
  std::printf("BPM:        %u\n", product.bpm);
  std::printf("Character:  %s\n", bach::subjectCharacterToString(product.character));
  std::printf("Instrument: %s\n", bach::instrumentTypeToString(product.instrument));
  if (opts.target_bars > 0) {
    std::printf("Bars:       %u (override)\n", product.resolved_bars);
  } else {
    std::printf("Scale:      %s (%u bars)\n", bach::durationScaleToString(product.scale),
                product.resolved_bars);
  }
  std::printf("Seed:       %u%s\n", product.seed, opts.seed == 0 ? " (auto)" : "");
  std::printf("\n");
  std::printf("Generated: %s in %s\n", product.form_display.c_str(),
              bach::keySignatureToString(product.key).c_str());
  std::printf("Notes:     %zu\n", product.composition.notes.size());
  std::printf("Tracks:    %zu\n", product.composition.tracks.size());
  std::printf("Duration:  %u ticks (%u bars)\n", product.total_ticks, product.total_bars);

  std::ofstream midi_file(opts.output, std::ios::binary);
  if (!midi_file.is_open()) {
    std::fprintf(stderr, "Error: failed to write %s\n", opts.output.c_str());
    return kExitOutput;
  }
  midi_file.write(reinterpret_cast<const char*>(product.midi_bytes.data()),
                  static_cast<std::streamsize>(product.midi_bytes.size()));
  if (!midi_file.good()) {
    std::fprintf(stderr, "Error: failed to write complete MIDI file %s\n", opts.output.c_str());
    return kExitOutput;
  }
  midi_file.close();
  std::printf("\nOutput:    %s\n", opts.output.c_str());

  bool sidecar_ok = true;
  if (opts.json_output) {
    const std::string json_path = deriveJsonPath(opts.output);
    std::ofstream json_file(json_path);
    if (json_file.is_open()) {
      json_file << product.homepage_events_json;
      std::printf("JSON:      %s\n", json_path.c_str());
    } else {
      std::fprintf(stderr, "Error: failed to write %s\n", json_path.c_str());
      sidecar_ok = false;
    }
  }

  if (opts.generated_json_output) {
    const std::string generated_path = deriveSuffixedJsonPath(opts.output, ".generated.json");
    const std::string provenance_path = deriveSuffixedJsonPath(opts.output, ".provenance.json");

    std::ofstream generated_file(generated_path);
    if (generated_file.is_open()) {
      generated_file << product.generated_json;
      std::printf("Generated JSON:%s\n", generated_path.c_str());
    } else {
      std::fprintf(stderr, "Error: failed to write %s\n", generated_path.c_str());
      sidecar_ok = false;
    }

    std::ofstream provenance_file(provenance_path);
    if (provenance_file.is_open()) {
      provenance_file << product.provenance_json;
      std::printf("Provenance:%s\n", provenance_path.c_str());
    } else {
      std::fprintf(stderr, "Error: failed to write %s\n", provenance_path.c_str());
      sidecar_ok = false;
    }
  }

  return sidecar_ok ? 0 : kExitOutput;
}

}  // namespace

int main(int argc, char* argv[]) {
  CliOptions opts;
  bool ok = true;
  if (!parseArgs(argc, argv, opts, ok)) {
    return ok ? 0 : kExitUsage;
  }

  if (opts.composer_mode) {
    return runComposerMode(opts);
  }

  return runDefaultMode(opts);
}
