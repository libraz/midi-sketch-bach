#include <gtest/gtest.h>
#include <sys/wait.h>

#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <string>

#include "composer/composer.h"
#include "composer/harmonic_plan.h"
#include "composer/json_export.h"
#include "composer/material.h"
#include "composer/span.h"
#include "composer/voice_intent.h"
#include "composer/voice_plan.h"

#ifndef BACH_SOURCE_DIR
#error "BACH_SOURCE_DIR must be defined via target_compile_definitions"
#endif

namespace bach::composer {
namespace {

Material trivialMaterial() {
  Material m;
  for (int i = 0; i < 4; ++i) {
    MaterialNote n;
    n.start_tick = static_cast<Tick>(i) * kTicksPerBeat;
    n.duration = kTicksPerBeat;
    n.pitch = static_cast<std::uint8_t>(72 + (i * 2));
    m.subject.push_back(n);
  }
  return m;
}

HarmonicPlan trivialHarmony() {
  HarmonicPlan p;
  p.tonic_pc = 0;
  ChordEvent c;
  c.start_tick = 0;
  c.root_pc = 0;
  c.quality = ChordQuality::Major;
  p.chords.push_back(c);
  return p;
}

VoicePlan trivialVoicePlan() {
  VoicePlan vp;
  vp.num_voices = 2;
  Span s0;
  s0.id = 0;
  s0.start_tick = 0;
  s0.end_tick = kTicksPerBar;
  s0.voice = 0;
  s0.intent = VoiceIntent::SubjectCarrier;
  vp.spans.push_back(s0);
  Span s1;
  s1.id = 1;
  s1.start_tick = 0;
  s1.end_tick = kTicksPerBar;
  s1.voice = 1;
  s1.intent = VoiceIntent::SequentialCounterline;
  vp.spans.push_back(s1);
  return vp;
}

std::string tempDir() {
  if (const char* env = std::getenv("TMPDIR"); env != nullptr) {
    return env;
  }
  return "/tmp";
}

}  // namespace

// Schema-conformance criterion: this repository's JSON output must validate
// against the bach-mcp generated.v1.json schema.
//
// This test writes Composer's generated.json to disk and invokes a Python
// validator that checks it against bach-mcp/schema/generated.v1.json. If
// bach-mcp is not present alongside this repo (sibling directory), or if
// python3 is unavailable, the validator exits with status 2 and the test
// is SKIPPED — failing the test would be a false alarm in those cases.
TEST(SchemaValidation, GeneratedJsonConformsToBachMcpSchema) {
  ComposeResult result = Composer{}.run(trivialMaterial(), trivialHarmony(), trivialVoicePlan());
  const std::string generated = emitGeneratedJson(result.notes);

  const std::string out_path = tempDir() + "/bach_generated_validation.json";
  {
    std::ofstream f(out_path);
    ASSERT_TRUE(f.is_open()) << "failed to open " << out_path;
    f << generated;
  }

  const std::string script = std::string(BACH_SOURCE_DIR) + "/scripts/validate_generated_json.py";
  const std::string cmd = "python3 \"" + script + "\" \"" + out_path + "\"";
  const int raw = std::system(cmd.c_str());
  ASSERT_NE(raw, -1) << "std::system() failed to spawn shell";
  const int exit_code = WIFEXITED(raw) ? WEXITSTATUS(raw) : -1;

  if (exit_code == 2) {
    GTEST_SKIP() << "validator unavailable (python3 missing or "
                    "../bach-mcp/schema/generated.v1.json absent)";
  }
  EXPECT_EQ(exit_code, 0) << "generated.json does not conform to bach-mcp schema; see "
                             "stderr above for details";
}

}  // namespace bach::composer
