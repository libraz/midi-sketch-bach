#include "composer/counterpoint_budget.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <string>
#include <vector>

#include "application/composition_service.h"
#include "composer/form_director.h"
#include "composer/validation.h"
#include "composer/validator.h"
#include "core/basic_types.h"

namespace bach::application {
namespace {

// Every form the product path ships.
constexpr FormType kShippedForms[] = {
    FormType::Fugue,
    FormType::PreludeAndFugue,
    FormType::TrioSonata,
    FormType::ChoralePrelude,
    FormType::ToccataAndFugue,
    FormType::Passacaglia,
    FormType::FantasiaAndFugue,
    FormType::CelloPrelude,
    FormType::Chaconne,
    FormType::GoldbergVariations,
};

constexpr SubjectCharacter kCharacters[] = {
    SubjectCharacter::Severe,
    SubjectCharacter::Playful,
    SubjectCharacter::Noble,
    SubjectCharacter::Restless,
};

// Every counterpoint rule that relates two voices sounding together, sorted by
// rule id. The geometry assertion below keeps this list honest.
constexpr const char* kVerticalRules[] = {
    "anti_parallel_perfect",  "battuta",
    "cross_relation",         "doubling_no_leading_tone",
    "doubling_no_seventh",    "hidden_parallel_fifth",
    "hidden_parallel_octave", "invertible_at_octave",
    "parallel_fifth",         "parallel_octave",
    "strong_beat_dissonance", "unprepared_dissonance",
    "vertical_dissonance",
};

// Every counterpoint rule that describes one voice on its own.
constexpr const char* kLinearRules[] = {
    "augmented_melodic", "consecutive_leaps",       "diminished_melodic",
    "tritone_melodic",   "leading_tone_resolution",
};

composer::RuleObservation observation(const std::string& rule_id, int total) {
  composer::RuleObservation result;
  result.rule_id = rule_id;
  result.total = total;
  result.gated = 0;
  result.exempted = total;
  return result;
}

std::vector<std::string> closedRulesFor(FormType form) {
  std::vector<std::string> closed;
  for (const char* rule_id : kVerticalRules) {
    if (composer::counterpointRuleIsClosed(form, rule_id)) {
      closed.emplace_back(rule_id);
    }
  }
  return closed;
}

// ---------------------------------------------------------------------------
// The open-rule table itself
// ---------------------------------------------------------------------------

TEST(CounterpointBudgetTest, TableHoldsOnlyVerticalRulesInStrictOrder) {
  std::size_t count = 0;
  const composer::CounterpointBudgetEntry* table = composer::counterpointBudgetTable(&count);
  ASSERT_NE(table, nullptr);
  ASSERT_GT(count, 0u);

  for (std::size_t idx = 0; idx < count; ++idx) {
    const std::string rule_id = table[idx].rule_id == nullptr ? std::string() : table[idx].rule_id;
    SCOPED_TRACE(formTypeToString(table[idx].form));
    SCOPED_TRACE(rule_id);
    EXPECT_FALSE(rule_id.empty());
    // Gating a linear rule would describe nothing the composer chose, so no
    // linear rule may appear here even as an exception.
    EXPECT_EQ(composer::counterpointRuleGeometry(rule_id), composer::RuleGeometry::Vertical);
    // A rule the table opens but kVerticalRules omits would leave a hole in the
    // pinned closure sets below.
    EXPECT_TRUE(std::any_of(std::begin(kVerticalRules), std::end(kVerticalRules),
                            [&rule_id](const char* known) { return rule_id == known; }))
        << "rule is missing from the pinned vertical-rule list";
    if (idx == 0) {
      continue;
    }
    // Strict ordering by (form ordinal, rule_id) also rules out duplicates.
    const int previous_form = static_cast<int>(table[idx - 1].form);
    const int current_form = static_cast<int>(table[idx].form);
    const std::string previous_rule = table[idx - 1].rule_id;
    EXPECT_TRUE(previous_form < current_form ||
                (previous_form == current_form && previous_rule < rule_id))
        << "row " << idx << " breaks the (form, rule_id) ordering";
  }
}

TEST(CounterpointBudgetTest, RuleListsMatchTheValidatorGeometryTable) {
  for (const char* rule_id : kVerticalRules) {
    SCOPED_TRACE(rule_id);
    EXPECT_EQ(composer::counterpointRuleGeometry(rule_id), composer::RuleGeometry::Vertical);
  }
  for (const char* rule_id : kLinearRules) {
    SCOPED_TRACE(rule_id);
    EXPECT_EQ(composer::counterpointRuleGeometry(rule_id), composer::RuleGeometry::Linear);
  }
}

TEST(CounterpointBudgetTest, LinearAndUnknownRulesAreNeverClosed) {
  for (FormType form : kShippedForms) {
    SCOPED_TRACE(formTypeToString(form));
    for (const char* rule_id : kLinearRules) {
      SCOPED_TRACE(rule_id);
      EXPECT_FALSE(composer::counterpointRuleIsClosed(form, rule_id));
    }
    EXPECT_FALSE(composer::counterpointRuleIsClosed(form, "no_such_rule"));
    EXPECT_FALSE(composer::counterpointRuleIsClosed(form, std::string()));
  }
}

TEST(CounterpointBudgetTest, ClosedRulesPerFormArePinned) {
  // Written out literally so any edit to the open-rule table shows up here as a
  // change of closure state rather than passing silently.
  const std::vector<std::string> keyboard_polyphony = {"doubling_no_seventh"};
  const std::vector<std::string> with_invertible_counterpoint = {"doubling_no_seventh",
                                                                 "invertible_at_octave"};

  EXPECT_EQ(closedRulesFor(FormType::Fugue), keyboard_polyphony);
  EXPECT_EQ(closedRulesFor(FormType::PreludeAndFugue), keyboard_polyphony);
  EXPECT_EQ(closedRulesFor(FormType::TrioSonata), with_invertible_counterpoint);
  EXPECT_EQ(closedRulesFor(FormType::ChoralePrelude),
            (std::vector<std::string>{"doubling_no_leading_tone", "doubling_no_seventh",
                                      "parallel_fifth"}));
  EXPECT_EQ(closedRulesFor(FormType::ToccataAndFugue), keyboard_polyphony);
  // The passacaglia is the one form that still breaks every vertical rule.
  EXPECT_EQ(closedRulesFor(FormType::Passacaglia), std::vector<std::string>{});
  EXPECT_EQ(closedRulesFor(FormType::FantasiaAndFugue), with_invertible_counterpoint);
  // Monophonic: every rule comparing two lines is closed permanently, and only
  // the two judged against the harmonic plan stay open.
  EXPECT_EQ(
      closedRulesFor(FormType::CelloPrelude),
      (std::vector<std::string>{
          "anti_parallel_perfect", "battuta", "cross_relation", "doubling_no_leading_tone",
          "doubling_no_seventh", "hidden_parallel_fifth", "hidden_parallel_octave",
          "invertible_at_octave", "parallel_fifth", "parallel_octave", "vertical_dissonance"}));
  // Its upper line is ranked against the ground before it ships, at every onset
  // the variation owns and at both ends of the cadential coda.
  EXPECT_EQ(closedRulesFor(FormType::Chaconne),
            (std::vector<std::string>{"battuta", "doubling_no_seventh", "invertible_at_octave",
                                      "parallel_fifth", "parallel_octave"}));
  EXPECT_EQ(closedRulesFor(FormType::GoldbergVariations), with_invertible_counterpoint);
}

// ---------------------------------------------------------------------------
// applyCounterpointBudget on a hand-built report
// ---------------------------------------------------------------------------

TEST(CounterpointBudgetTest, ClosedRuleWithAMatchFailsThePiece) {
  composer::ValidationReport report;
  report.observations.push_back(observation("doubling_no_seventh", 1));

  composer::applyCounterpointBudget(FormType::Fugue, &report);

  ASSERT_EQ(report.failures.size(), 1u);
  EXPECT_EQ(report.failures[0].rule_id, "doubling_no_seventh");
  EXPECT_EQ(report.failures[0].kind, FailKind::MusicalFail);
  // Routing the status is the caller's job, not the budget's.
  EXPECT_EQ(report.status, composer::ValidationStatus::Ok);
}

TEST(CounterpointBudgetTest, ClosedRuleWithoutAMatchAppendsNothing) {
  composer::ValidationReport report;
  report.observations.push_back(observation("doubling_no_seventh", 0));

  composer::applyCounterpointBudget(FormType::Fugue, &report);

  EXPECT_TRUE(report.failures.empty());
}

TEST(CounterpointBudgetTest, OpenAndLinearRulesNeverAppendHoweverOftenTheyMatch) {
  composer::ValidationReport report;
  report.observations.push_back(observation("parallel_fifth", 500));
  report.observations.push_back(observation("strong_beat_dissonance", 500));
  report.observations.push_back(observation("tritone_melodic", 500));

  composer::applyCounterpointBudget(FormType::Fugue, &report);

  EXPECT_TRUE(report.failures.empty());
}

TEST(CounterpointBudgetTest, AppendsOneFailurePerClosedRuleAndPreservesExistingOnes) {
  composer::ValidationReport report;
  composer::ValidationFailure existing;
  existing.rule_id = "suspension_resolution_step_down";
  existing.kind = FailKind::StructuralFail;
  report.failures.push_back(existing);
  report.observations.push_back(observation("doubling_no_leading_tone", 3));
  report.observations.push_back(observation("doubling_no_seventh", 7));
  report.observations.push_back(observation("parallel_octave", 9));

  // The chorale prelude closes both doubling rules and leaves parallel_octave open.
  composer::applyCounterpointBudget(FormType::ChoralePrelude, &report);

  ASSERT_EQ(report.failures.size(), 3u);
  EXPECT_EQ(report.failures[0].rule_id, "suspension_resolution_step_down");
  EXPECT_EQ(report.failures[0].kind, FailKind::StructuralFail);
  EXPECT_EQ(report.failures[1].rule_id, "doubling_no_leading_tone");
  EXPECT_EQ(report.failures[2].rule_id, "doubling_no_seventh");
}

TEST(CounterpointBudgetTest, NullReportIsANoOp) {
  composer::applyCounterpointBudget(FormType::Fugue, nullptr);
}

// ---------------------------------------------------------------------------
// The gate over the shipped request surface
// ---------------------------------------------------------------------------

// One request variation applied to a form/character pair.
struct RequestVariation {
  bool minor = false;
  DurationScale scale = DurationScale::Short;
  std::uint16_t target_bars = 0;
  std::uint32_t seed = 1;
};

std::vector<RequestVariation> requestVariations() {
  constexpr bool modes[] = {false, true};
  constexpr std::uint32_t seeds[] = {1, 3, 7};
  std::vector<RequestVariation> variations;
  for (bool minor : modes) {
    for (int length = 0; length < 3; ++length) {
      for (std::uint32_t seed : seeds) {
        RequestVariation variation;
        variation.minor = minor;
        variation.seed = seed;
        if (length == 0) {
          variation.scale = DurationScale::Short;
        } else if (length == 1) {
          variation.scale = DurationScale::Full;
        } else {
          // target_bars overrides the scale, so the scale carried here is inert.
          variation.scale = DurationScale::Short;
          variation.target_bars = 128;
        }
        variations.push_back(variation);
      }
    }
  }
  return variations;
}

CompositionRequest buildRequest(FormType form, SubjectCharacter character,
                                const RequestVariation& variation) {
  CompositionRequest request;
  request.form = form;
  request.character = character;
  request.key = {variation.minor ? Key::G : Key::C, variation.minor};
  request.scale = variation.scale;
  request.target_bars = variation.target_bars;
  request.seed = variation.seed;
  request.bpm = 100;
  return request;
}

void expectComposesClean(const CompositionRequest& request) {
  SCOPED_TRACE(formTypeToString(request.form));
  SCOPED_TRACE(subjectCharacterToString(request.character));
  SCOPED_TRACE(keyToString(request.key.tonic));
  SCOPED_TRACE(request.key.is_minor ? "minor" : "major");
  SCOPED_TRACE(durationScaleToString(request.scale));
  SCOPED_TRACE(request.target_bars);
  SCOPED_TRACE(request.seed);
  CompositionProduct product;
  ASSERT_EQ(compose(request, &product), CompositionStatus::Ok);
  EXPECT_TRUE(product.final_validation.failures.empty())
      << "first failure: "
      << (product.final_validation.failures.empty()
              ? std::string()
              : product.final_validation.failures.front().rule_id);
  EXPECT_EQ(product.final_validation.status, composer::ValidationStatus::Ok);
  EXPECT_FALSE(product.midi_bytes.empty());
}

TEST(CounterpointBudgetTest, GateIsOutputNeutralOverTheShippedRequestSurface) {
  const std::vector<RequestVariation> variations = requestVariations();
  ASSERT_EQ(variations.size(), 18u);
  std::vector<bool> variation_used(variations.size(), false);

  std::size_t rotation = 0;
  for (FormType form : kShippedForms) {
    for (SubjectCharacter character : kCharacters) {
      if (!composer::isFormCharacterCompatible(form, character)) {
        continue;
      }
      const std::size_t index = rotation % variations.size();
      variation_used[index] = true;
      ++rotation;
      expectComposesClean(buildRequest(form, character, variations[index]));

      // The configuration that exposed rules a narrower sample reported as
      // closed: minor mode at the maximum bar count on the first seed.
      RequestVariation exposing;
      exposing.minor = true;
      exposing.scale = DurationScale::Short;
      exposing.target_bars = 128;
      exposing.seed = 1;
      expectComposesClean(buildRequest(form, character, exposing));
    }
  }

  EXPECT_EQ(rotation, 37u) << "the compatible form/character cross changed size";
  for (std::size_t idx = 0; idx < variation_used.size(); ++idx) {
    EXPECT_TRUE(variation_used[idx]) << "request variation " << idx << " was never exercised";
  }
}

}  // namespace
}  // namespace bach::application
