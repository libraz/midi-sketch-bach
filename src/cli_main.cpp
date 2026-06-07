/// @file
/// @brief CLI entry point for the Bach MIDI generator.

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include "composer/composer.h"
#include "composer/expression_events.h"
#include "composer/figuration.h"
#include "composer/form_director.h"
#include "composer/harness_fixture.h"
#include "composer/json_export.h"
#include "composer/ornament_pass.h"
#include "core/basic_types.h"
#include "core/instrument_program.h"
#include "core/rng_util.h"
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
  uint16_t bpm = 72;
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
  bach::composer::HarnessPhase composer_phase = bach::composer::HarnessPhase::Phase6;
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
  if (std::strcmp(val, "Phase15") == 0 || std::strcmp(val, "phase15") == 0 ||
      std::strcmp(val, "15") == 0 || std::strcmp(val, "p15") == 0) {
    return bach::composer::HarnessPhase::Phase15;
  }
  if (std::strcmp(val, "Phase16") == 0 || std::strcmp(val, "phase16") == 0 ||
      std::strcmp(val, "16") == 0 || std::strcmp(val, "p16") == 0) {
    return bach::composer::HarnessPhase::Phase16;
  }
  if (std::strcmp(val, "Phase17") == 0 || std::strcmp(val, "phase17") == 0 ||
      std::strcmp(val, "17") == 0 || std::strcmp(val, "p17") == 0) {
    return bach::composer::HarnessPhase::Phase17;
  }
  if (std::strcmp(val, "Phase18") == 0 || std::strcmp(val, "phase18") == 0 ||
      std::strcmp(val, "18") == 0 || std::strcmp(val, "p18") == 0) {
    return bach::composer::HarnessPhase::Phase18;
  }
  if (std::strcmp(val, "Phase19") == 0 || std::strcmp(val, "phase19") == 0 ||
      std::strcmp(val, "19") == 0 || std::strcmp(val, "p19") == 0) {
    return bach::composer::HarnessPhase::Phase19;
  }
  if (std::strcmp(val, "Phase20") == 0 || std::strcmp(val, "phase20") == 0 ||
      std::strcmp(val, "20") == 0 || std::strcmp(val, "p20") == 0) {
    return bach::composer::HarnessPhase::Phase20;
  }
  if (std::strcmp(val, "Phase21") == 0 || std::strcmp(val, "phase21") == 0 ||
      std::strcmp(val, "21") == 0 || std::strcmp(val, "p21") == 0) {
    return bach::composer::HarnessPhase::Phase21;
  }
  if (std::strcmp(val, "Phase22") == 0 || std::strcmp(val, "phase22") == 0 ||
      std::strcmp(val, "22") == 0 || std::strcmp(val, "p22") == 0) {
    return bach::composer::HarnessPhase::Phase22;
  }
  if (std::strcmp(val, "Phase23") == 0 || std::strcmp(val, "phase23") == 0 ||
      std::strcmp(val, "23") == 0 || std::strcmp(val, "p23") == 0) {
    return bach::composer::HarnessPhase::Phase23;
  }
  if (std::strcmp(val, "Phase24") == 0 || std::strcmp(val, "phase24") == 0 ||
      std::strcmp(val, "24") == 0 || std::strcmp(val, "p24") == 0) {
    return bach::composer::HarnessPhase::Phase24;
  }
  if (std::strcmp(val, "Phase25") == 0 || std::strcmp(val, "phase25") == 0 ||
      std::strcmp(val, "25") == 0 || std::strcmp(val, "p25") == 0) {
    return bach::composer::HarnessPhase::Phase25;
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
  std::printf("  --bpm N          BPM (40-200, default 72)\n");
  std::printf("  --scale SCALE    Duration scale: short, medium, long, full\n");
  std::printf("                   Default: medium for fugue, short otherwise\n");
  std::printf("  --bars N         Target bar count (overrides --scale)\n");
  std::printf(
      "  --composer-phase P  Run the closure harness layout with phase\n"
      "                   {Phase3|Phase35|Phase4|Phase4Sus|Phase5|Phase6|Phase6Episode|\n"
      "                    Phase6Tonal|Phase7|Phase8|Phase9|Phase10|Phase11|Phase12|\n"
      "                    Phase13|Phase14|Phase15|Phase16|Phase17|Phase18|Phase19|\n"
      "                    Phase20|Phase21|Phase22|Phase23|Phase24|Phase25}. Seed reused.\n");
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
    if (std::strcmp(argv[idx], "--help") == 0 || std::strcmp(argv[idx], "-h") == 0) {
      printUsage();
      return false;
    }
    if (std::strcmp(argv[idx], "--seed") == 0 && idx + 1 < argc) {
      opts.seed = static_cast<uint32_t>(std::atoi(argv[++idx]));
    } else if (std::strcmp(argv[idx], "--bpm") == 0 && idx + 1 < argc) {
      const int bpm = std::atoi(argv[++idx]);
      if (bpm < 40 || bpm > 200) {
        std::fprintf(stderr, "Error: --bpm must be in [40, 200] (got %d)\n", bpm);
        ok = false;
        return false;
      }
      opts.bpm = static_cast<uint16_t>(bpm);
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
      opts.target_bars = static_cast<uint16_t>(std::atoi(argv[++idx]));
    } else if (std::strcmp(argv[idx], "--composer-phase") == 0 && idx + 1 < argc) {
      opts.composer_mode = true;
      opts.composer_phase = parseComposerPhase(argv[++idx]);
    } else {
      // No recognized flag matched (unknown option, or a known flag missing its
      // required value). Reject rather than silently falling through to
      // generation. There is no positional-argument syntax to preserve.
      std::fprintf(stderr, "Error: unknown argument '%s'\n", argv[idx]);
      ok = false;
      return false;
    }
  }
  return true;
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
    case bach::composer::HarnessPhase::Phase15:
      return "Phase15";
    case bach::composer::HarnessPhase::Phase16:
      return "Phase16";
    case bach::composer::HarnessPhase::Phase17:
      return "Phase17";
    case bach::composer::HarnessPhase::Phase18:
      return "Phase18";
    case bach::composer::HarnessPhase::Phase19:
      return "Phase19";
    case bach::composer::HarnessPhase::Phase20:
      return "Phase20";
    case bach::composer::HarnessPhase::Phase21:
      return "Phase21";
    case bach::composer::HarnessPhase::Phase22:
      return "Phase22";
    case bach::composer::HarnessPhase::Phase23:
      return "Phase23";
    case bach::composer::HarnessPhase::Phase24:
      return "Phase24";
    case bach::composer::HarnessPhase::Phase25:
      return "Phase25";
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
    const std::string generated =
        bach::composer::emitGeneratedJson(result.notes, result.validation);
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

/// @brief Friendly display name for a form, used in the JSON description.
/// @param form The form type.
/// @return A human-readable phrase such as "Toccata and Fugue".
const char* formDisplayName(bach::FormType form) {
  switch (form) {
    case bach::FormType::Fugue:
      return "Fugue";
    case bach::FormType::PreludeAndFugue:
      return "Prelude and Fugue";
    case bach::FormType::TrioSonata:
      return "Trio Sonata";
    case bach::FormType::ChoralePrelude:
      return "Chorale Prelude";
    case bach::FormType::ToccataAndFugue:
      return "Toccata and Fugue";
    case bach::FormType::Passacaglia:
      return "Passacaglia";
    case bach::FormType::FantasiaAndFugue:
      return "Fantasia and Fugue";
    case bach::FormType::CelloPrelude:
      return "Cello Prelude";
    case bach::FormType::Chaconne:
      return "Chaconne";
    case bach::FormType::GoldbergVariations:
      return "Goldberg Variations";
  }
  return "Work";
}

/// @brief Collect voices carrying immutable ground / cantus-firmus material.
///
/// Scans the resolved VoicePlan for spans whose intent replays an immutable
/// foundation line (ground bass, passacaglia ground, cantus firmus). Those
/// voices are exempt from the ornament pass so the immutable material stays
/// un-decorated. The scan is intent-based (no per-form hardcode), so it covers
/// every form whose builder declares one of those carriers.
///
/// @param plan The resolved voice plan.
/// @return Sorted, de-duplicated list of exempt voice ids.
std::vector<bach::VoiceId> collectExemptVoices(const bach::composer::VoicePlan& plan) {
  std::vector<bach::VoiceId> exempt;
  for (const auto& span : plan.spans) {
    const bool is_foundation = span.intent == bach::composer::VoiceIntent::GroundCarrier ||
                               span.intent == bach::composer::VoiceIntent::PassacagliaGround ||
                               span.intent == bach::composer::VoiceIntent::CantusFirmusCarrier;
    if (is_foundation) {
      exempt.push_back(span.voice);
    }
  }
  std::sort(exempt.begin(), exempt.end());
  exempt.erase(std::unique(exempt.begin(), exempt.end()), exempt.end());
  return exempt;
}

/// @brief Derive the output JSON path from the MIDI output path.
/// @param output The MIDI output path (e.g. "song.mid").
/// @return The sibling JSON path (e.g. "song.json").
std::string deriveJsonPath(const std::string& output) {
  std::string json_path = output;
  const auto dot_pos = json_path.rfind('.');
  if (dot_pos != std::string::npos) {
    json_path = json_path.substr(0, dot_pos) + ".json";
  } else {
    json_path += ".json";
  }
  return json_path;
}

std::string deriveSuffixedJsonPath(const std::string& output, const char* suffix) {
  std::string json_path = output;
  const auto dot_pos = json_path.rfind('.');
  if (dot_pos != std::string::npos) {
    json_path = json_path.substr(0, dot_pos) + suffix;
  } else {
    json_path += suffix;
  }
  return json_path;
}

/// @brief Run the default composer pipeline (form director) and emit output.
/// @param opts Parsed command-line options.
/// @return 0 on success, 1 on any failure.
int runDefaultMode(const CliOptions& opts) {
  // Resolve the seed: 0 means "auto", drawn from the system entropy source.
  const uint32_t seed_resolved = opts.seed == 0 ? bach::rng::generateRandomSeed() : opts.seed;

  // Resolve the instrument: form-default unless the user pinned one.
  const bach::InstrumentType instrument =
      opts.instrument_specified ? opts.instrument : bach::defaultInstrumentForForm(opts.form);

  // Character / form compatibility is a CONFIG_FAIL: reject before composing.
  if (!bach::composer::isFormCharacterCompatible(opts.form, opts.character)) {
    std::fprintf(stderr, "Error: character '%s' is incompatible with form '%s'\n",
                 bach::subjectCharacterToString(opts.character), bach::formTypeToString(opts.form));
    return 1;
  }

  // Resolve the bar count. CLI nicety: fugue defaults to Medium scale when no
  // explicit --scale is given (mirrors the historical generator behavior).
  bach::DurationScale scale = opts.scale;
  if (!opts.scale_specified && opts.form == bach::FormType::Fugue) {
    scale = bach::DurationScale::Medium;
  }
  const uint16_t bars = bach::composer::resolveBars(opts.form, scale, opts.target_bars);

  // Build the fixture for the resolved request, then run the composer.
  bach::composer::ComposeRequest request;
  request.form = opts.form;
  request.is_minor = opts.key.is_minor;
  request.character = opts.character;
  request.target_bars = bars;
  request.seed = seed_resolved;

  bach::composer::HarnessFixture fixture;
  const auto status = bach::composer::buildFormFixture(request, &fixture);
  if (status != bach::composer::FormDirectorStatus::Ok) {
    std::fprintf(stderr, "Error: form director rejected the request (status %u)\n",
                 static_cast<unsigned>(status));
    return 1;
  }

  auto result =
      bach::composer::Composer{}.run(fixture.material, fixture.harmony, fixture.voice_plan);

  if (result.validation.status != bach::composer::ValidationStatus::Ok) {
    std::fprintf(stderr, "Error: composer validation failed: %zu rule violations\n",
                 result.validation.failures.size());
    for (const auto& f : result.validation.failures) {
      std::fprintf(stderr, "  - %s (span %u)\n", f.rule_id.c_str(),
                   static_cast<unsigned>(f.span_id));
    }
    return 1;
  }

  const bach::Tick ticks_per_bar = fixture.harmony.ticksPerBar();

  // Ornament post-pass. Ground / cantus-firmus voices are exempt so immutable
  // material stays un-decorated.
  bach::composer::OrnamentParams ornament_params;
  ornament_params.character = opts.character;
  ornament_params.instrument = instrument;
  ornament_params.mode =
      opts.key.is_minor ? bach::composer::detail::Mode::Minor : bach::composer::detail::Mode::Major;
  ornament_params.seed = seed_resolved;
  ornament_params.ticks_per_bar = ticks_per_bar;
  ornament_params.bpm = opts.bpm;
  ornament_params.exempt_voices = collectExemptVoices(fixture.voice_plan);
  bach::composer::applyOrnamentPass(result, ornament_params);

  // Apply the instrument to every track (GM program + default names).
  bach::applyInstrument(result.tracks, instrument);

  // Total length in ticks = the last note-off across the whole result.
  bach::Tick total_ticks = 0;
  for (const auto& note : result.notes) {
    total_ticks = std::max(total_ticks, note.start_tick + note.duration);
  }

  // Arc cycle count drives the registration plan's number of points.
  const auto& spec = bach::composer::formSpec(opts.form);
  const uint16_t snap = spec.snap_bars == 0 ? 1 : spec.snap_bars;
  std::size_t cycle_count = bars / snap;
  if (cycle_count == 0) {
    cycle_count = 1;
  }

  // Organ registration: clone the arc-driven CC plan onto every voice channel.
  if (instrument == bach::InstrumentType::Organ) {
    const std::vector<bach::CcEvent> plan =
        bach::composer::buildRegistrationPlan(bars, cycle_count, ticks_per_bar, total_ticks);
    for (auto& track : result.tracks) {
      track.cc_events.insert(track.cc_events.end(), plan.begin(), plan.end());
    }
  }

  // Tempo map: base tempo at tick 0 plus the closing ritardando.
  std::vector<bach::TempoEvent> tempo_events;
  tempo_events.push_back({0, opts.bpm});
  const std::vector<bach::TempoEvent> ritard =
      bach::composer::buildFinalRitardando(opts.bpm, total_ticks, ticks_per_bar);
  tempo_events.insert(tempo_events.end(), ritard.begin(), ritard.end());

  // Time signature at tick 0 from the form's meter.
  std::vector<bach::TimeSignatureEvent> time_sig_events;
  bach::TimeSignatureEvent ts_event;
  ts_event.tick = 0;
  ts_event.time_sig = {spec.ts_numerator, spec.ts_denominator};
  time_sig_events.push_back(ts_event);

  // Console summary.
  const uint16_t total_bars =
      ticks_per_bar > 0 ? static_cast<uint16_t>((total_ticks + ticks_per_bar - 1) / ticks_per_bar)
                        : 0;
  std::printf("bach_cli v0.2.0\n");
  std::printf("Form:       %s\n", bach::formTypeToString(opts.form));
  std::printf("Key:        %s\n", bach::keySignatureToString(opts.key).c_str());
  std::printf("Voices:     %u\n", spec.num_voices);
  std::printf("BPM:        %u\n", opts.bpm);
  std::printf("Character:  %s\n", bach::subjectCharacterToString(opts.character));
  std::printf("Instrument: %s\n", bach::instrumentTypeToString(instrument));
  if (opts.target_bars > 0) {
    std::printf("Bars:       %u (override)\n", bars);
  } else {
    std::printf("Scale:      %s (%u bars)\n", bach::durationScaleToString(scale), bars);
  }
  std::printf("Seed:       %u%s\n", seed_resolved, opts.seed == 0 ? " (auto)" : "");
  std::printf("\n");
  std::printf("Generated: %s in %s\n", formDisplayName(opts.form),
              bach::keySignatureToString(opts.key).c_str());
  std::printf("Notes:     %zu\n", result.notes.size());
  std::printf("Tracks:    %zu\n", result.tracks.size());
  std::printf("Duration:  %u ticks (%u bars)\n", total_ticks, total_bars);

  // Write the MIDI file.
  bach::MidiWriter writer;
  writer.build(result.tracks, tempo_events, time_sig_events, opts.key.tonic);
  if (!writer.writeToFile(opts.output)) {
    std::fprintf(stderr, "Error: failed to write %s\n", opts.output.c_str());
    return 1;
  }
  std::printf("\nOutput:    %s\n", opts.output.c_str());

  // Write the homepage events JSON if requested.
  if (opts.json_output) {
    bach::composer::HomepageMeta meta;
    meta.form_name = bach::formTypeToString(opts.form);
    meta.key_name = bach::keySignatureToString(opts.key);
    meta.bpm = opts.bpm;
    meta.seed = seed_resolved;
    meta.total_ticks = total_ticks;
    meta.total_bars = total_bars;
    meta.description =
        std::string(formDisplayName(opts.form)) + " in " + bach::keySignatureToString(opts.key);

    const std::string json_path = deriveJsonPath(opts.output);
    const std::string json_str = bach::composer::buildHomepageEventsJson(result, meta);
    std::ofstream json_file(json_path);
    if (json_file.is_open()) {
      json_file << json_str;
      json_file.close();
      std::printf("JSON:      %s\n", json_path.c_str());
    } else {
      std::fprintf(stderr, "Warning: failed to write %s\n", json_path.c_str());
    }
  }

  // Write scorer-facing generated.v1 and provenance.v1 JSON if requested.
  if (opts.generated_json_output) {
    const std::string generated_path = deriveSuffixedJsonPath(opts.output, ".generated.json");
    const std::string provenance_path = deriveSuffixedJsonPath(opts.output, ".provenance.json");

    std::ofstream generated_file(generated_path);
    if (generated_file.is_open()) {
      generated_file << bach::composer::emitGeneratedJson(result.notes, result.validation);
      generated_file.close();
      std::printf("Generated JSON:%s\n", generated_path.c_str());
    } else {
      std::fprintf(stderr, "Warning: failed to write %s\n", generated_path.c_str());
    }

    std::ofstream provenance_file(provenance_path);
    if (provenance_file.is_open()) {
      provenance_file << bach::composer::emitProvenanceJson(result.provenance);
      provenance_file.close();
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
  bool ok = true;
  if (!parseArgs(argc, argv, opts, ok)) {
    return ok ? 0 : 1;
  }

  if (opts.composer_mode) {
    return runComposerMode(opts);
  }

  return runDefaultMode(opts);
}
