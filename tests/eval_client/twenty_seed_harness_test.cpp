#include <gtest/gtest.h>
#include <sys/stat.h>

#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "composer/composer.h"
#include "composer/harmonic_plan.h"
#include "composer/harness_fixture.h"
#include "composer/json_export.h"
#include "composer/material.h"
#include "composer/voice_plan.h"
#include "eval_client/external_evaluator.h"

#ifndef BACH_SOURCE_DIR
#error "BACH_SOURCE_DIR must be defined via target_compile_definitions"
#endif

namespace bach::eval_client {

namespace {

// PhaseSpec carries closure thresholds and the log tag the harness
// emits. Catalog data (subject patterns, harmony patterns, layout
// rules) lives in src/composer/harness_fixture.{h,cpp} so the CLI
// dispatch path can build byte-identical fixtures via
// buildHarnessFixture(). Only the model threshold / min_pass / log tag
// are test-local concerns.
struct PhaseSpec {
  const char* name;
  bach::composer::HarnessPhase phase;
  double model_threshold;
  std::uint32_t min_pass;
};

struct SeedFixture {
  bach::composer::Material material;
  bach::composer::HarmonicPlan harmony;
  bach::composer::VoicePlan voice_plan;
};

SeedFixture buildFixture(int seed, const PhaseSpec& spec) {
  const bach::composer::HarnessFixture fx =
      bach::composer::buildHarnessFixture(spec.phase, seed);
  return SeedFixture{fx.material, fx.harmony, fx.voice_plan};
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

std::string siblingIndexJs() {
  return std::string(BACH_SOURCE_DIR) + "/../bach-mcp/dist/index.js";
}

bool evaluatorAvailable() {
  return fileExists(siblingIndexJs()) || std::getenv("BACH_MCP_INDEX_JS") != nullptr;
}

// Verifies that every note in `source` appears in `result.notes` at the
// expected voice with matching pitch/duration. Other notes in the same
// voice (counterlines flanking the carrier) are tolerated; the check is
// existence-based, not exclusivity-based. `tick_lo` / `tick_hi` scope
// the comparison to a time window so multi-entry materials (Phase 6's
// V0+V2 subject) can be checked one entry at a time. Pass `tick_hi = 0`
// for an unbounded window.
bool materialPreserved(const bach::composer::ComposeResult& result,
                       const std::vector<bach::composer::MaterialNote>& source,
                       bach::VoiceId expected_voice, bach::Tick tick_lo = 0,
                       bach::Tick tick_hi = 0) {
  for (const auto& mn : source) {
    if (mn.start_tick < tick_lo)
      continue;
    if (tick_hi != 0 && mn.start_tick >= tick_hi)
      continue;
    bool found = false;
    for (const auto& n : result.notes) {
      if (n.voice != expected_voice)
        continue;
      if (n.start_tick == mn.start_tick && n.duration == mn.duration && n.pitch == mn.pitch) {
        found = true;
        break;
      }
    }
    if (!found)
      return false;
  }
  return true;
}

// Runs Composer + bach-mcp for one seed and returns the metrics the
// harness aggregates. tmp_path is the path generated.json is written to.
struct SeedOutcome {
  bool composer_ok = false;
  bool subject_ok = false;
  bool answer_ok = false;  // true when no answer is expected
  double model_prob = 0.0;
  double heuristic = 0.0;
  bool evaluator_invoked = false;
  std::string evaluator_error;
  std::string failure_rules;  // semicolon-joined rule_ids when validation fails
};

SeedOutcome runOneSeed(int seed, const PhaseSpec& spec, const std::string& tmp_path) {
  SeedOutcome o;
  const SeedFixture fx = buildFixture(seed, spec);
  const auto layout = bach::composer::phaseSpec(spec.phase);
  const auto result = bach::composer::Composer{}.run(fx.material, fx.harmony, fx.voice_plan);

  o.composer_ok = (result.validation.status == bach::composer::ValidationStatus::Ok);
  const bach::Tick v0_hi = static_cast<bach::Tick>(layout.subject_bars) * bach::kTicksPerBar;
  o.subject_ok = materialPreserved(result, fx.material.subject, 0,
                                   /*tick_lo=*/0, /*tick_hi=*/v0_hi);
  if (layout.with_third_entry) {
    const bach::Tick v2_lo =
        static_cast<bach::Tick>(2 * layout.subject_bars) * bach::kTicksPerBar;
    const bach::Tick v2_hi =
        static_cast<bach::Tick>(3 * layout.subject_bars) * bach::kTicksPerBar;
    o.subject_ok = o.subject_ok && materialPreserved(result, fx.material.subject, 2,
                                                     /*tick_lo=*/v2_lo, /*tick_hi=*/v2_hi);
  }
  o.answer_ok = !layout.with_answer || materialPreserved(result, fx.material.answer, 1);
  if (!o.composer_ok) {
    for (std::size_t i = 0; i < result.validation.failures.size() && i < 8; ++i) {
      if (!o.failure_rules.empty())
        o.failure_rules += ";";
      o.failure_rules += result.validation.failures[i].rule_id;
    }
    return o;
  }

  const std::string generated = bach::composer::emitGeneratedJson(result.notes);
  {
    std::ofstream f(tmp_path);
    if (!f.is_open())
      return o;
    f << generated;
  }

  EvaluatorConfig cfg;
  cfg.bach_mcp_index_js = siblingIndexJs();
  const EvaluatorResult er = evaluateGeneratedJson(tmp_path, cfg);
  o.evaluator_invoked = er.invoked;
  if (!er.invoked) {
    o.evaluator_error = er.error;
    return o;
  }
  o.model_prob = er.model_probability;
  o.heuristic = er.score;
  return o;
}

// Drives 20 seeds for `spec`, asserts >= spec.min_pass seeds clear the
// model threshold, and emits one summary line per seed on stderr.
void runHarness(const PhaseSpec& spec) {
  if (!evaluatorAvailable()) {
    GTEST_SKIP() << "bach-mcp not available at " << siblingIndexJs()
                 << " and BACH_MCP_INDEX_JS unset";
  }
  const auto layout = bach::composer::phaseSpec(spec.phase);

  constexpr int kSeedCount = 20;
  int composer_ok_count = 0;
  int subject_ok_count = 0;
  int answer_ok_count = 0;
  int model_pass_count = 0;
  double model_prob_sum = 0.0;
  double model_prob_min = 1.0;
  double model_prob_max = 0.0;

  for (int seed = 0; seed < kSeedCount; ++seed) {
    std::ostringstream path_ss;
    path_ss << tempDir() << "/bach_harness_" << spec.name << "_seed" << seed << ".json";
    const std::string path = path_ss.str();

    const SeedOutcome o = runOneSeed(seed, spec, path);
    if (o.composer_ok)
      ++composer_ok_count;
    if (o.subject_ok)
      ++subject_ok_count;
    if (o.answer_ok)
      ++answer_ok_count;
    const bool model_pass = o.model_prob >= spec.model_threshold;
    if (o.composer_ok && o.evaluator_invoked && model_pass)
      ++model_pass_count;
    if (o.evaluator_invoked) {
      model_prob_sum += o.model_prob;
      if (o.model_prob < model_prob_min)
        model_prob_min = o.model_prob;
      if (o.model_prob > model_prob_max)
        model_prob_max = o.model_prob;
    }

    std::cerr << "[harness][" << spec.name << "][seed=" << seed
              << "] composer_ok=" << (o.composer_ok ? "true" : "false")
              << " subject_ok=" << (o.subject_ok ? "true" : "false");
    if (layout.with_answer) {
      std::cerr << " answer_ok=" << (o.answer_ok ? "true" : "false");
    }
    std::cerr << " heur=" << o.heuristic << " model_prob=" << o.model_prob
              << " model_pass=" << (model_pass ? "true" : "false");
    if (!o.evaluator_error.empty()) {
      std::cerr << " evaluator_error=\"" << o.evaluator_error << "\"";
    }
    if (!o.failure_rules.empty()) {
      std::cerr << " failures=" << o.failure_rules;
    }
    std::cerr << "\n";
  }

  const double model_prob_mean = composer_ok_count > 0 ? model_prob_sum / composer_ok_count : 0.0;
  std::cerr << "[harness][" << spec.name << "][summary]"
            << " composer_ok=" << composer_ok_count << "/" << kSeedCount
            << " subject_ok=" << subject_ok_count << "/" << kSeedCount;
  if (layout.with_answer) {
    std::cerr << " answer_ok=" << answer_ok_count << "/" << kSeedCount;
  }
  std::cerr << " model_pass=" << model_pass_count << "/" << kSeedCount
            << " model_prob[min/mean/max]=" << model_prob_min << "/" << model_prob_mean << "/"
            << model_prob_max << "\n";

  // Phase closure assertions. Subject preservation is a hard invariant
  // whenever the spec includes a SubjectCarrier (all phases). Answer
  // preservation is a hard invariant whenever spec.with_answer is true.
  // Model-pass floor is the formal closure condition.
  EXPECT_EQ(subject_ok_count, kSeedCount)
      << spec.name << ": subject pitch/duration must be preserved for "
      << "every seed.";
  EXPECT_EQ(answer_ok_count, kSeedCount)
      << spec.name << ": answer pitch/duration must be preserved for "
      << "every seed (vacuous when no answer is expected).";
  EXPECT_GE(static_cast<std::uint32_t>(model_pass_count), spec.min_pass)
      << spec.name << ": fewer than " << spec.min_pass << "/" << kSeedCount
      << " seeds cleared model_prob >= " << spec.model_threshold;
}

}  // namespace

// Phase 3 closure: 2-voice 8-bar piece, model_prob >= 0.45 for >= 8/20
// seeds. See backup/rebuild_plan_2026-05-28.md Phase 3 completion
// condition.
TEST(TwentySeedHarness, Phase3) {
  runHarness({"p3", bach::composer::HarnessPhase::Phase3,
              /*model_threshold=*/0.45, /*min_pass=*/8});
}

// Phase 3.5 closure: 2-voice 4-bar piece, subject pitch/duration
// preserved on every seed, model_prob threshold informally tracked
// (Phase 3 floor of 0.45 reused — Phase 3.5's plan does not redeclare
// a probability gate, only a structural preservation gate).
TEST(TwentySeedHarness, Phase35) {
  runHarness({"p3.5", bach::composer::HarnessPhase::Phase35,
              /*model_threshold=*/0.45, /*min_pass=*/8});
}

// Phase 5 closure: 3-voice 12-bar piece, model_prob >= 0.60 for >= 8/20
// seeds. See backup/rebuild_plan_2026-05-28.md Phase 5 completion
// condition.
TEST(TwentySeedHarness, Phase5) {
  runHarness({"p5", bach::composer::HarnessPhase::Phase5,
              /*model_threshold=*/0.60, /*min_pass=*/8});
}

// Phase 4 closure: 2-voice 8-bar piece, V0 = subject + counterline, V1
// = counterline + real answer (subject transposed -P4). model_prob >=
// 0.55 for >= 8/20 seeds. Subject and answer pitch/duration must both
// be preserved on every seed. See backup/rebuild_plan_2026-05-28.md
// Phase 4 completion condition.
TEST(TwentySeedHarness, Phase4) {
  runHarness({"p4", bach::composer::HarnessPhase::Phase4,
              /*model_threshold=*/0.55, /*min_pass=*/8});
}

// Phase 6 closure: 3-voice 16-bar small fugue. V0 subject bars 0-3,
// V1 answer bars 4-7, V2 subject (transposed down an octave) bars 8-11,
// all three voices counterline bars 12-15. model_prob >= 0.65 for >=
// 6/20 seeds. Subject pitch/duration preserved in voices 0 AND 2; answer
// preserved in voice 1. See backup/rebuild_plan_2026-05-28.md Phase 6
// completion condition.
TEST(TwentySeedHarness, Phase6) {
  runHarness({"p6", bach::composer::HarnessPhase::Phase6,
              /*model_threshold=*/0.65, /*min_pass=*/6});
}

}  // namespace bach::eval_client
