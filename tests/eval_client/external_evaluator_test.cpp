#include "eval_client/external_evaluator.h"

#include <gtest/gtest.h>
#include <sys/stat.h>

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

namespace bach::eval_client {

namespace {

bach::composer::Material trivialMaterial() {
  bach::composer::Material m;
  for (int i = 0; i < 4; ++i) {
    bach::composer::MaterialNote n;
    n.start_tick = static_cast<bach::Tick>(i) * bach::kTicksPerBeat;
    n.duration = bach::kTicksPerBeat;
    n.pitch = static_cast<std::uint8_t>(72 + (i * 2));
    m.subject.push_back(n);
  }
  return m;
}

bach::composer::HarmonicPlan trivialHarmony() {
  bach::composer::HarmonicPlan p;
  p.tonic_pc = 0;
  bach::composer::ChordEvent c;
  c.start_tick = 0;
  c.root_pc = 0;
  c.quality = bach::composer::ChordQuality::Major;
  p.chords.push_back(c);
  return p;
}

bach::composer::VoicePlan trivialVoicePlan() {
  bach::composer::VoicePlan vp;
  vp.num_voices = 2;
  bach::composer::Span s0;
  s0.id = 0;
  s0.start_tick = 0;
  s0.end_tick = bach::kTicksPerBar;
  s0.voice = 0;
  s0.intent = bach::composer::VoiceIntent::SubjectCarrier;
  vp.spans.push_back(s0);
  bach::composer::Span s1;
  s1.id = 1;
  s1.start_tick = 0;
  s1.end_tick = bach::kTicksPerBar;
  s1.voice = 1;
  s1.intent = bach::composer::VoiceIntent::SequentialCounterline;
  vp.spans.push_back(s1);
  return vp;
}

// Phase 3 quality probe: the same 4-bar I-IV-V-I fixture used in
// composer_two_voice_test.cpp. Larger and more harmonically structured
// than the wiring fixture above.
bach::composer::Material phase3Material() {
  bach::composer::Material m;
  static constexpr std::uint8_t kPitches[] = {
      72, 74, 76, 77, 79, 81, 79, 77, 76, 74, 76, 77, 79, 77, 76, 72,
  };
  for (std::size_t i = 0; i < sizeof(kPitches) / sizeof(kPitches[0]); ++i) {
    bach::composer::MaterialNote n;
    n.start_tick = static_cast<bach::Tick>(i) * bach::kTicksPerBeat;
    n.duration = bach::kTicksPerBeat;
    n.pitch = kPitches[i];
    m.subject.push_back(n);
  }
  return m;
}

bach::composer::HarmonicPlan phase3Harmony() {
  bach::composer::HarmonicPlan p;
  p.tonic_pc = 0;
  p.is_minor = false;
  for (std::uint8_t bar = 0; bar < 4; ++bar) {
    bach::composer::ChordEvent c;
    c.start_tick = static_cast<bach::Tick>(bar) * bach::kTicksPerBar;
    c.quality = bach::composer::ChordQuality::Major;
    switch (bar) {
      case 0:
        c.root_pc = 0;  // C
        break;
      case 1:
        c.root_pc = 5;  // F
        break;
      case 2:
        c.root_pc = 7;  // G
        break;
      case 3:
        c.root_pc = 0;  // C
        break;
    }
    p.chords.push_back(c);
  }
  return p;
}

bach::composer::VoicePlan phase3VoicePlan() {
  bach::composer::VoicePlan vp;
  vp.num_voices = 2;
  bach::composer::SpanId next_id = 0;
  bach::composer::Span s0;
  s0.id = next_id++;
  s0.start_tick = 0;
  s0.end_tick = 4 * bach::kTicksPerBar;
  s0.voice = 0;
  s0.intent = bach::composer::VoiceIntent::SubjectCarrier;
  vp.spans.push_back(s0);
  for (std::uint8_t bar = 0; bar < 4; ++bar) {
    bach::composer::Span s;
    s.id = next_id++;
    s.start_tick = static_cast<bach::Tick>(bar) * bach::kTicksPerBar;
    s.end_tick = s.start_tick + bach::kTicksPerBar;
    s.voice = 1;
    s.intent = bach::composer::VoiceIntent::SequentialCounterline;
    vp.spans.push_back(s);
  }
  return vp;
}

// Eighth-note variant: same subject + harmony as phase3VoicePlan but
// the counterline voice is opted into Subdivision::Eighth so the
// composer emits two notes per beat instead of one. Exercises the
// rule cascade at finer rhythmic resolution and exposes the model's
// duration/melodic_interval components to richer rhythmic content.
bach::composer::VoicePlan phase3VoicePlanEighth() {
  bach::composer::VoicePlan vp = phase3VoicePlan();
  for (auto& s : vp.spans) {
    if (s.intent == bach::composer::VoiceIntent::SequentialCounterline) {
      s.subdivision = bach::composer::Subdivision::Eighth;
    }
  }
  return vp;
}

// Three-voice variant: same subject + harmony, with an extra
// SequentialCounterline voice 2 (tenor). Span order is voice 0 ->
// voice 1 (bars 0..3) -> voice 2 (bars 0..3) so the composer commits
// upper voices before lower ones and the vertical rules see them.
bach::composer::VoicePlan phase3ThreeVoicePlan() {
  bach::composer::VoicePlan vp;
  vp.num_voices = 3;
  bach::composer::SpanId next_id = 0;
  bach::composer::Span subject;
  subject.id = next_id++;
  subject.start_tick = 0;
  subject.end_tick = 4 * bach::kTicksPerBar;
  subject.voice = 0;
  subject.intent = bach::composer::VoiceIntent::SubjectCarrier;
  vp.spans.push_back(subject);
  for (std::uint8_t voice = 1; voice <= 2; ++voice) {
    for (std::uint8_t bar = 0; bar < 4; ++bar) {
      bach::composer::Span s;
      s.id = next_id++;
      s.start_tick = static_cast<bach::Tick>(bar) * bach::kTicksPerBar;
      s.end_tick = s.start_tick + bach::kTicksPerBar;
      s.voice = voice;
      s.intent = bach::composer::VoiceIntent::SequentialCounterline;
      vp.spans.push_back(s);
    }
  }
  return vp;
}

// Minor mode probe: A-minor i-iv-V-i progression. Same shape as
// phase3Material but transposed into A natural minor and built note by
// note to stay diatonic. Subject ascends the natural minor scale, peaks
// at F5, then descends back to A4. The progression uses the harmonic
// minor V (E major), so the chromatic-penalty path runs against a
// natural-minor diatonic mask while bar 2 demands a leading-tone G#
// the mask does not include — an interesting stress test for the rule
// cascade in minor key.
bach::composer::Material phase3MinorMaterial() {
  bach::composer::Material m;
  static constexpr std::uint8_t kPitches[] = {
      69, 71, 72, 74,  // A4 B4 C5 D5 (i)
      76, 77, 76, 74,  // E5 F5 E5 D5 (iv)
      72, 71, 72, 74,  // C5 B4 C5 D5 (V)
      76, 74, 72, 69,  // E5 D5 C5 A4 (i)
  };
  for (std::size_t i = 0; i < sizeof(kPitches) / sizeof(kPitches[0]); ++i) {
    bach::composer::MaterialNote n;
    n.start_tick = static_cast<bach::Tick>(i) * bach::kTicksPerBeat;
    n.duration = bach::kTicksPerBeat;
    n.pitch = kPitches[i];
    m.subject.push_back(n);
  }
  return m;
}

bach::composer::HarmonicPlan phase3MinorHarmony() {
  bach::composer::HarmonicPlan p;
  p.tonic_pc = 9;  // A
  p.is_minor = true;
  for (std::uint8_t bar = 0; bar < 4; ++bar) {
    bach::composer::ChordEvent c;
    c.start_tick = static_cast<bach::Tick>(bar) * bach::kTicksPerBar;
    switch (bar) {
      case 0:
        c.root_pc = 9;  // A minor (i)
        c.quality = bach::composer::ChordQuality::Minor;
        break;
      case 1:
        c.root_pc = 2;  // D minor (iv)
        c.quality = bach::composer::ChordQuality::Minor;
        break;
      case 2:
        c.root_pc = 4;  // E major (V, harmonic minor)
        c.quality = bach::composer::ChordQuality::Major;
        break;
      case 3:
        c.root_pc = 9;  // A minor (i)
        c.quality = bach::composer::ChordQuality::Minor;
        break;
    }
    p.chords.push_back(c);
  }
  return p;
}

// Four-voice + Eighth plan. SATB texture: V0 carries the subject
// (quarters), V1/V2/V3 are SequentialCounterline at Eighth. Span order
// is voice 0 -> voice 1 (4 bars) -> voice 2 -> voice 3 so the composer
// commits soprano-down and lower voices see every higher voice.
bach::composer::VoicePlan phase3FourVoicePlanEighth() {
  bach::composer::VoicePlan vp;
  vp.num_voices = 4;
  bach::composer::SpanId next_id = 0;
  bach::composer::Span subject;
  subject.id = next_id++;
  subject.start_tick = 0;
  subject.end_tick = 4 * bach::kTicksPerBar;
  subject.voice = 0;
  subject.intent = bach::composer::VoiceIntent::SubjectCarrier;
  vp.spans.push_back(subject);
  for (std::uint8_t voice = 1; voice <= 3; ++voice) {
    for (std::uint8_t bar = 0; bar < 4; ++bar) {
      bach::composer::Span s;
      s.id = next_id++;
      s.start_tick = static_cast<bach::Tick>(bar) * bach::kTicksPerBar;
      s.end_tick = s.start_tick + bach::kTicksPerBar;
      s.voice = voice;
      s.intent = bach::composer::VoiceIntent::SequentialCounterline;
      s.subdivision = bach::composer::Subdivision::Eighth;
      vp.spans.push_back(s);
    }
  }
  return vp;
}

// Three-voice + Eighth variant: combines phase3ThreeVoicePlan() with
// Subdivision::Eighth on both counterlines so the composer emits eight
// notes per bar in voices 1 and 2 while the subject voice 0 keeps
// quarters. Tests whether the eighth-note biases (step preference,
// chromatic penalty) hold up under three-voice texture where the
// vertical-rule cascade is tighter.
bach::composer::VoicePlan phase3ThreeVoicePlanEighth() {
  bach::composer::VoicePlan vp = phase3ThreeVoicePlan();
  for (auto& s : vp.spans) {
    if (s.intent == bach::composer::VoiceIntent::SequentialCounterline) {
      s.subdivision = bach::composer::Subdivision::Eighth;
    }
  }
  return vp;
}

std::string tempDir() {
  if (const char* env = std::getenv("TMPDIR"); env != nullptr) {
    return env;
  }
  return "/tmp";
}

bool fileExists(const std::string& path) {
  struct stat st {};
  return ::stat(path.c_str(), &st) == 0;
}

}  // namespace

TEST(ExternalEvaluatorTest, LibraryLinks) {
  EXPECT_TRUE(isMcpClientLibLinked());
}

// Phase 2 task 19: end-to-end subprocess invocation.
// Spawns the real bach-mcp `score` CLI mode and parses pass / score /
// schema_version from the response. If bach-mcp dist/index.js is not
// found alongside the repo (or BACH_MCP_INDEX_JS is unset and the
// sibling repo isn't present), the test is SKIPPED rather than failed.
TEST(ExternalEvaluatorTest, ScoresTrivialComposerOutput) {
  const std::string sibling_index_js = std::string(BACH_SOURCE_DIR) + "/../bach-mcp/dist/index.js";
  if (!fileExists(sibling_index_js) && std::getenv("BACH_MCP_INDEX_JS") == nullptr) {
    GTEST_SKIP() << "bach-mcp dist/index.js not found at " << sibling_index_js
                 << " and BACH_MCP_INDEX_JS unset";
  }

  const auto result =
      bach::composer::Composer{}.run(trivialMaterial(), trivialHarmony(), trivialVoicePlan());
  const std::string generated = bach::composer::emitGeneratedJson(result.notes);

  const std::string path = tempDir() + "/bach_eval_client_test.json";
  {
    std::ofstream f(path);
    ASSERT_TRUE(f.is_open()) << "cannot open " << path;
    f << generated;
  }

  EvaluatorConfig config;
  config.bach_mcp_index_js = sibling_index_js;
  const EvaluatorResult eval_result = evaluateGeneratedJson(path, config);

  ASSERT_TRUE(eval_result.invoked)
      << "evaluator invocation failed: " << eval_result.error << "\nraw:\n"
      << eval_result.raw_stdout;
  EXPECT_EQ(eval_result.schema_version, "bach-score.v1");
  EXPECT_GE(eval_result.score, 0.0);
  EXPECT_LE(eval_result.score, 1.0);
  // This test validates eval_client wiring and JSON field extraction only.
  // Musical pass/fail thresholds belong to bach-mcp calibration tests; this
  // tiny deterministic composer fixture is not a quality golden sample.
  EXPECT_FALSE(eval_result.raw_stdout.empty());
}

// Phase 3 quality probe. Runs the full 4-bar 2-voice fixture through
// bach-mcp and surfaces the actual numeric score on stdout. The
// thresholds asserted here are the loose Phase 3 gate (score in
// [0, 1], schema present) — quality calibration belongs to bach-mcp's
// own test suite.
TEST(ExternalEvaluatorTest, ScoresPhase3Fixture) {
  const std::string sibling_index_js = std::string(BACH_SOURCE_DIR) + "/../bach-mcp/dist/index.js";
  if (!fileExists(sibling_index_js) && std::getenv("BACH_MCP_INDEX_JS") == nullptr) {
    GTEST_SKIP() << "bach-mcp dist/index.js not found at " << sibling_index_js
                 << " and BACH_MCP_INDEX_JS unset";
  }

  const auto result =
      bach::composer::Composer{}.run(phase3Material(), phase3Harmony(), phase3VoicePlan());
  const std::string generated = bach::composer::emitGeneratedJson(result.notes);

  const std::string path = tempDir() + "/bach_eval_phase3_fixture.json";
  {
    std::ofstream f(path);
    ASSERT_TRUE(f.is_open()) << "cannot open " << path;
    f << generated;
  }

  EvaluatorConfig config;
  config.bach_mcp_index_js = sibling_index_js;
  const EvaluatorResult eval_result = evaluateGeneratedJson(path, config);

  ASSERT_TRUE(eval_result.invoked)
      << "evaluator invocation failed: " << eval_result.error << "\nraw:\n"
      << eval_result.raw_stdout;
  EXPECT_EQ(eval_result.schema_version, "bach-score.v1");
  EXPECT_GE(eval_result.score, 0.0);
  EXPECT_LE(eval_result.score, 1.0);

  // Surface the observed score so the test log captures Phase 3
  // baseline numbers as the calibration matures.
  std::cerr << "[phase3-probe] score=" << eval_result.score
            << " pass=" << (eval_result.pass ? "true" : "false") << " json=" << path << "\n";
}

// Eighth-note probe. Same 2-voice fixture as ScoresPhase3Fixture but
// the counterline runs at eighth-note subdivision, doubling its note
// count. Verifies the rule cascade stays clean at finer rhythmic
// resolution and surfaces the observed score for calibration tracking.
TEST(ExternalEvaluatorTest, ScoresPhase3EighthFixture) {
  const std::string sibling_index_js = std::string(BACH_SOURCE_DIR) + "/../bach-mcp/dist/index.js";
  if (!fileExists(sibling_index_js) && std::getenv("BACH_MCP_INDEX_JS") == nullptr) {
    GTEST_SKIP() << "bach-mcp dist/index.js not found at " << sibling_index_js
                 << " and BACH_MCP_INDEX_JS unset";
  }

  const auto result =
      bach::composer::Composer{}.run(phase3Material(), phase3Harmony(), phase3VoicePlanEighth());
  const std::string generated = bach::composer::emitGeneratedJson(result.notes);

  const std::string path = tempDir() + "/bach_eval_phase3_eighth_fixture.json";
  {
    std::ofstream f(path);
    ASSERT_TRUE(f.is_open()) << "cannot open " << path;
    f << generated;
  }

  EvaluatorConfig config;
  config.bach_mcp_index_js = sibling_index_js;
  const EvaluatorResult eval_result = evaluateGeneratedJson(path, config);

  ASSERT_TRUE(eval_result.invoked)
      << "evaluator invocation failed: " << eval_result.error << "\nraw:\n"
      << eval_result.raw_stdout;
  EXPECT_EQ(eval_result.schema_version, "bach-score.v1");
  EXPECT_GE(eval_result.score, 0.0);
  EXPECT_LE(eval_result.score, 1.0);

  std::cerr << "[phase3-eighth-probe] score=" << eval_result.score
            << " pass=" << (eval_result.pass ? "true" : "false") << " json=" << path << "\n";
}

// Three-voice probe. Same fixed subject + I-IV-V-I harmony, but now
// two SequentialCounterline voices are composed under a single
// SubjectCarrier. Verifies the vertical-rule cascade scales past two
// voices and surfaces the observed score for calibration tracking.
TEST(ExternalEvaluatorTest, ScoresPhase3ThreeVoiceFixture) {
  const std::string sibling_index_js = std::string(BACH_SOURCE_DIR) + "/../bach-mcp/dist/index.js";
  if (!fileExists(sibling_index_js) && std::getenv("BACH_MCP_INDEX_JS") == nullptr) {
    GTEST_SKIP() << "bach-mcp dist/index.js not found at " << sibling_index_js
                 << " and BACH_MCP_INDEX_JS unset";
  }

  const auto result =
      bach::composer::Composer{}.run(phase3Material(), phase3Harmony(), phase3ThreeVoicePlan());
  const std::string generated = bach::composer::emitGeneratedJson(result.notes);

  const std::string path = tempDir() + "/bach_eval_phase3_three_voice_fixture.json";
  {
    std::ofstream f(path);
    ASSERT_TRUE(f.is_open()) << "cannot open " << path;
    f << generated;
  }

  EvaluatorConfig config;
  config.bach_mcp_index_js = sibling_index_js;
  const EvaluatorResult eval_result = evaluateGeneratedJson(path, config);

  ASSERT_TRUE(eval_result.invoked)
      << "evaluator invocation failed: " << eval_result.error << "\nraw:\n"
      << eval_result.raw_stdout;
  EXPECT_EQ(eval_result.schema_version, "bach-score.v1");
  EXPECT_GE(eval_result.score, 0.0);
  EXPECT_LE(eval_result.score, 1.0);

  std::cerr << "[phase3-three-voice-probe] score=" << eval_result.score
            << " pass=" << (eval_result.pass ? "true" : "false") << " json=" << path << "\n";
}

// Three-voice + Eighth probe. Combines the three-voice fixture with
// Eighth-subdivision counterlines so the rule cascade sees both a
// denser vertical texture (3 simultaneous voices) and a finer rhythmic
// resolution (8 notes per bar in counterlines).
TEST(ExternalEvaluatorTest, ScoresPhase3ThreeVoiceEighthFixture) {
  const std::string sibling_index_js = std::string(BACH_SOURCE_DIR) + "/../bach-mcp/dist/index.js";
  if (!fileExists(sibling_index_js) && std::getenv("BACH_MCP_INDEX_JS") == nullptr) {
    GTEST_SKIP() << "bach-mcp dist/index.js not found at " << sibling_index_js
                 << " and BACH_MCP_INDEX_JS unset";
  }

  const auto result = bach::composer::Composer{}.run(phase3Material(), phase3Harmony(),
                                                     phase3ThreeVoicePlanEighth());
  const std::string generated = bach::composer::emitGeneratedJson(result.notes);

  const std::string path = tempDir() + "/bach_eval_phase3_three_voice_eighth_fixture.json";
  {
    std::ofstream f(path);
    ASSERT_TRUE(f.is_open()) << "cannot open " << path;
    f << generated;
  }

  EvaluatorConfig config;
  config.bach_mcp_index_js = sibling_index_js;
  const EvaluatorResult eval_result = evaluateGeneratedJson(path, config);

  ASSERT_TRUE(eval_result.invoked)
      << "evaluator invocation failed: " << eval_result.error << "\nraw:\n"
      << eval_result.raw_stdout;
  EXPECT_EQ(eval_result.schema_version, "bach-score.v1");
  EXPECT_GE(eval_result.score, 0.0);
  EXPECT_LE(eval_result.score, 1.0);

  std::cerr << "[phase3-three-voice-eighth-probe] score=" << eval_result.score
            << " pass=" << (eval_result.pass ? "true" : "false") << " json=" << path << "\n";
}

// Minor-mode probe. A-minor i-iv-V-i progression. Eighth subdivision on
// the counterline so the same biases (step preference, chromatic
// penalty) are exercised against a minor diatonic mask, including bar
// 2 where the harmonic-minor V chord (E major) contains G# — a pitch
// outside the natural-minor diatonic mask the search uses for the
// chromatic penalty.
TEST(ExternalEvaluatorTest, ScoresPhase3MinorEighthFixture) {
  const std::string sibling_index_js = std::string(BACH_SOURCE_DIR) + "/../bach-mcp/dist/index.js";
  if (!fileExists(sibling_index_js) && std::getenv("BACH_MCP_INDEX_JS") == nullptr) {
    GTEST_SKIP() << "bach-mcp dist/index.js not found at " << sibling_index_js
                 << " and BACH_MCP_INDEX_JS unset";
  }

  const auto result = bach::composer::Composer{}.run(phase3MinorMaterial(), phase3MinorHarmony(),
                                                     phase3VoicePlanEighth());
  const std::string generated = bach::composer::emitGeneratedJson(result.notes);

  const std::string path = tempDir() + "/bach_eval_phase3_minor_eighth_fixture.json";
  {
    std::ofstream f(path);
    ASSERT_TRUE(f.is_open()) << "cannot open " << path;
    f << generated;
  }

  EvaluatorConfig config;
  config.bach_mcp_index_js = sibling_index_js;
  const EvaluatorResult eval_result = evaluateGeneratedJson(path, config);

  ASSERT_TRUE(eval_result.invoked)
      << "evaluator invocation failed: " << eval_result.error << "\nraw:\n"
      << eval_result.raw_stdout;
  EXPECT_EQ(eval_result.schema_version, "bach-score.v1");
  EXPECT_GE(eval_result.score, 0.0);
  EXPECT_LE(eval_result.score, 1.0);

  std::cerr << "[phase3-minor-eighth-probe] score=" << eval_result.score
            << " pass=" << (eval_result.pass ? "true" : "false") << " json=" << path << "\n";
}

// Four-voice (SATB) + Eighth probe. Composes the same C-major
// I-IV-V-I subject under three SequentialCounterline voices (alto,
// tenor, bass) at Eighth subdivision. Stresses the vertical rule
// cascade at four-voice density.
TEST(ExternalEvaluatorTest, ScoresPhase3FourVoiceEighthFixture) {
  const std::string sibling_index_js = std::string(BACH_SOURCE_DIR) + "/../bach-mcp/dist/index.js";
  if (!fileExists(sibling_index_js) && std::getenv("BACH_MCP_INDEX_JS") == nullptr) {
    GTEST_SKIP() << "bach-mcp dist/index.js not found at " << sibling_index_js
                 << " and BACH_MCP_INDEX_JS unset";
  }

  const auto result = bach::composer::Composer{}.run(phase3Material(), phase3Harmony(),
                                                     phase3FourVoicePlanEighth());
  const std::string generated = bach::composer::emitGeneratedJson(result.notes);

  const std::string path = tempDir() + "/bach_eval_phase3_four_voice_eighth_fixture.json";
  {
    std::ofstream f(path);
    ASSERT_TRUE(f.is_open()) << "cannot open " << path;
    f << generated;
  }

  EvaluatorConfig config;
  config.bach_mcp_index_js = sibling_index_js;
  const EvaluatorResult eval_result = evaluateGeneratedJson(path, config);

  ASSERT_TRUE(eval_result.invoked)
      << "evaluator invocation failed: " << eval_result.error << "\nraw:\n"
      << eval_result.raw_stdout;
  EXPECT_EQ(eval_result.schema_version, "bach-score.v1");
  EXPECT_GE(eval_result.score, 0.0);
  EXPECT_LE(eval_result.score, 1.0);

  std::cerr << "[phase3-four-voice-eighth-probe] score=" << eval_result.score
            << " pass=" << (eval_result.pass ? "true" : "false") << " json=" << path << "\n";
}

TEST(ExternalEvaluatorTest, ReportsErrorWhenIndexJsMissing) {
  EvaluatorConfig config;
  config.bach_mcp_index_js = "/definitely/not/a/real/path/index.js";
  const EvaluatorResult result = evaluateGeneratedJson("/tmp/does_not_matter.json", config);
  EXPECT_FALSE(result.invoked);
  EXPECT_FALSE(result.error.empty());
}

TEST(ExternalEvaluatorTest, ReportsErrorWhenIndexJsUnconfigured) {
  // Save and clear the env var so we exercise the empty-config path.
  const char* prior_env = std::getenv("BACH_MCP_INDEX_JS");
  ::unsetenv("BACH_MCP_INDEX_JS");

  EvaluatorConfig config;  // no path, no env
  const EvaluatorResult result = evaluateGeneratedJson("/tmp/whatever.json", config);
  EXPECT_FALSE(result.invoked);
  EXPECT_NE(result.error.find("not configured"), std::string::npos);

  if (prior_env != nullptr)
    ::setenv("BACH_MCP_INDEX_JS", prior_env, 1);
}

}  // namespace bach::eval_client
