#include <gtest/gtest.h>
#include <sys/wait.h>

#include <array>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <string>

#include "composer/composer.h"
#include "composer/form_director.h"
#include "composer/json_export.h"

#ifndef BACH_SOURCE_DIR
#error "BACH_SOURCE_DIR must be defined via target_compile_definitions"
#endif

namespace bach::composer {
namespace {

constexpr std::array<FormType, 10> kAllForms = {FormType::Fugue,
                                                FormType::PreludeAndFugue,
                                                FormType::TrioSonata,
                                                FormType::ChoralePrelude,
                                                FormType::ToccataAndFugue,
                                                FormType::Passacaglia,
                                                FormType::FantasiaAndFugue,
                                                FormType::CelloPrelude,
                                                FormType::Chaconne,
                                                FormType::GoldbergVariations};

std::string tempDir() {
  if (const char* env = std::getenv("TMPDIR"); env != nullptr) {
    return env;
  }
  return "/tmp";
}

}  // namespace

// Schema-conformance criterion: this repository's JSON output must validate
// against the vendored generated.v1.json schema. Every shipped form uses the
// same three-argument exporter as the production service, including tempos.
TEST(SchemaValidation, EveryShippedFormWithTempoConformsToVendoredSchema) {
  const std::string script = std::string(BACH_SOURCE_DIR) + "/scripts/validate_generated_json.py";
  const std::string schema = std::string(BACH_SOURCE_DIR) + "/schema/generated.v1.json";
  for (FormType form : kAllForms) {
    ComposeRequest request;
    request.form = form;
    request.seed = 1;
    HarnessFixture fixture;
    ASSERT_EQ(buildFormFixture(request, &fixture), FormDirectorStatus::Ok)
        << static_cast<int>(form);
    const ComposeResult result =
        Composer{}.run(fixture.material, fixture.harmony, fixture.voice_plan);
    ASSERT_EQ(result.validation.status, ValidationStatus::Ok) << static_cast<int>(form);
    const std::vector<TempoEvent> tempos = {{0, 100}, {fixture.harmony.ticksPerBar(), 96}};
    const std::string generated = emitGeneratedJson(result.notes, result.validation, tempos);

    const std::string out_path = tempDir() + "/bach_generated_validation_" +
                                 std::to_string(static_cast<int>(form)) + ".json";
    {
      std::ofstream output(out_path);
      ASSERT_TRUE(output.is_open()) << "failed to open " << out_path;
      output << generated;
    }
    const std::string command =
        "python3 \"" + script + "\" \"" + out_path + "\" \"" + schema + "\"";
    const int raw = std::system(command.c_str());
    ASSERT_NE(raw, -1) << "std::system() failed to spawn shell";
    const int exit_code = WIFEXITED(raw) ? WEXITSTATUS(raw) : -1;
    EXPECT_EQ(exit_code, 0) << "form=" << static_cast<int>(form);
  }
}

}  // namespace bach::composer
