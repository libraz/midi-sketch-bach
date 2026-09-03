// Counterpoint observation channel.
//
// The validator exempts a counterpoint finding whose every operand is an
// immutable input, because the composer cannot repair material it replays
// verbatim. Every shipped form emits carrier spans only, so that exemption
// covers almost the whole output and `failures` alone says nothing about how
// much counterpoint was actually found. ValidationReport::observations counts
// each match before the routing decision, which is what these tests pin: the
// counts exist, they add up, every rule that reaches the recorder has a
// geometry, and recording them changed no verdict.

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "composer/composer.h"
#include "composer/form_director.h"
#include "composer/harmonic_plan.h"
#include "composer/harness_fixture.h"
#include "composer/provenance.h"
#include "composer/validation.h"
#include "composer/validator.h"
#include "core/basic_types.h"

namespace bach::composer {
namespace {

constexpr std::array<FormType, 10> kShippedForms = {{
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
}};

constexpr std::uint32_t kFirstSeed = 1;
constexpr std::uint32_t kSeedCount = 8;

struct ExpectedGeometry {
  const char* rule_id;
  RuleGeometry geometry;
};

// Every rule id routed through the finding recorder in validator.cpp, with the
// geometry the rule table must report for it. Kept here as an independent
// second copy on purpose: a rule added to the recorder without a table entry
// makes one of these two lists wrong, and the sweep below catches the case
// where both lists are out of date but the rule fires in shipped output.
constexpr std::array<ExpectedGeometry, 16> kRecordedRules = {{
    {"augmented_melodic", RuleGeometry::Linear},
    {"consecutive_leaps", RuleGeometry::Linear},
    {"cross_relation", RuleGeometry::Vertical},
    {"diminished_melodic", RuleGeometry::Linear},
    {"doubling_no_leading_tone", RuleGeometry::Vertical},
    {"doubling_no_seventh", RuleGeometry::Vertical},
    {"hidden_parallel_fifth", RuleGeometry::Vertical},
    {"hidden_parallel_octave", RuleGeometry::Vertical},
    {"invertible_at_octave", RuleGeometry::Vertical},
    {"leading_tone_resolution", RuleGeometry::Linear},
    {"parallel_fifth", RuleGeometry::Vertical},
    {"parallel_octave", RuleGeometry::Vertical},
    {"strong_beat_dissonance", RuleGeometry::Vertical},
    {"tritone_melodic", RuleGeometry::Linear},
    {"unprepared_dissonance", RuleGeometry::Vertical},
    {"vertical_dissonance", RuleGeometry::Vertical},
}};

NoteEvent makeNote(Tick start, Tick duration, std::uint8_t pitch, VoiceId voice) {
  NoteEvent note;
  note.start_tick = start;
  note.duration = duration;
  note.pitch = pitch;
  note.voice = voice;
  note.velocity = 80;
  return note;
}

NoteProvenance makeProv(SpanId span_id, NoteSource source) {
  NoteProvenance prov;
  prov.span_id = span_id;
  prov.source = source;
  return prov;
}

void bindAuthoredNote(const NoteEvent& note, NoteProvenance* provenance) {
  provenance->has_authored_note = true;
  provenance->authored_start_tick = note.start_tick;
  provenance->authored_duration = note.duration;
  provenance->authored_pitch = note.pitch;
}

HarmonicPlan cMajorWhole() {
  HarmonicPlan plan;
  plan.tonic_pc = 0;
  plan.is_minor = false;
  ChordEvent chord;
  chord.start_tick = 0;
  chord.root_pc = 0;
  chord.quality = ChordQuality::Major;
  plan.chords.push_back(chord);
  return plan;
}

// Voice 0 (upper) C5 -> D5 over voice 1 (lower) F4 -> G4: a perfect fifth on
// both onsets with both voices rising, i.e. parallel fifths.
std::vector<NoteEvent> parallelFifthNotes() {
  return {
      makeNote(0, kTicksPerBeat, 60, 0),
      makeNote(kTicksPerBeat, kTicksPerBeat, 62, 0),
      makeNote(0, kTicksPerBeat, 53, 1),
      makeNote(kTicksPerBeat, kTicksPerBeat, 55, 1),
  };
}

const RuleObservation* findObservation(const ValidationReport& report, const std::string& rule_id) {
  for (const RuleObservation& entry : report.observations) {
    if (entry.rule_id == rule_id)
      return &entry;
  }
  return nullptr;
}

int countByRule(const std::vector<ValidationFailure>& entries, const std::string& rule_id) {
  int count = 0;
  for (const ValidationFailure& entry : entries) {
    if (entry.rule_id == rule_id)
      ++count;
  }
  return count;
}

// total = gated + exempted + informational, for every rule in the report.
void expectAccountingHolds(const ValidationReport& report) {
  for (const RuleObservation& entry : report.observations) {
    EXPECT_EQ(entry.gated, countByRule(report.failures, entry.rule_id))
        << entry.rule_id << ": gated count disagrees with the failures it claims to describe";
    const int informational = countByRule(report.informational, entry.rule_id);
    EXPECT_EQ(entry.total, entry.gated + entry.exempted + informational)
        << entry.rule_id << ": total (" << entry.total << ") is not gated (" << entry.gated
        << ") + exempted (" << entry.exempted << ") + informational (" << informational << ")";
  }
}

}  // namespace

// --- Rule geometry table -----------------------------------------------------

TEST(CounterpointGeometryTest, EveryRuleReachingTheRecorderIsClassified) {
  for (const ExpectedGeometry& expected : kRecordedRules) {
    EXPECT_EQ(counterpointRuleGeometry(expected.rule_id), expected.geometry)
        << expected.rule_id << " has the wrong geometry (or no table entry at all)";
  }
}

TEST(CounterpointGeometryTest, RulesOutsideTheRecorderAreNotClassified) {
  // voice_crossing, cadence_voice_leading and trio_upper_register_overlap push
  // straight to failures and never reach the finding recorder, so the geometry
  // table deliberately has no entry for them.
  EXPECT_EQ(counterpointRuleGeometry("voice_crossing"), RuleGeometry::Unclassified);
  EXPECT_EQ(counterpointRuleGeometry("cadence_voice_leading"), RuleGeometry::Unclassified);
  EXPECT_EQ(counterpointRuleGeometry("trio_upper_register_overlap"), RuleGeometry::Unclassified);
}

// --- Accounting --------------------------------------------------------------

TEST(CounterpointObservationTest, ExemptedFindingIsStillCounted) {
  // Both voices replay immutable material, so the composer cannot repair the
  // parallel and it never becomes a failure -- but it happened, so it counts.
  const std::vector<NoteEvent> notes = parallelFifthNotes();
  const std::vector<NoteProvenance> prov(notes.size(), makeProv(0, NoteSource::Material));

  const ValidationReport report = Validator{}.validate(notes, prov, cMajorWhole());
  ASSERT_EQ(report.status, ValidationStatus::Ok);
  ASSERT_TRUE(report.failures.empty());
  const RuleObservation* observation = findObservation(report, "parallel_fifth");
  ASSERT_NE(observation, nullptr) << "the exempted parallel left no trace in the report";
  EXPECT_EQ(observation->total, 1);
  EXPECT_EQ(observation->gated, 0);
  EXPECT_EQ(observation->exempted, 1);
  expectAccountingHolds(report);
}

TEST(CounterpointObservationTest, GatedFindingIsCountedAsGated) {
  // A Compose upper voice against a Material lower voice is composer-fixable,
  // so the same parallel fires as a failure.
  const std::vector<NoteEvent> notes = parallelFifthNotes();
  const std::vector<NoteProvenance> prov = {
      makeProv(0, NoteSource::Compose),
      makeProv(0, NoteSource::Compose),
      makeProv(1, NoteSource::Material),
      makeProv(1, NoteSource::Material),
  };

  const ValidationReport report = Validator{}.validate(notes, prov, cMajorWhole());
  ASSERT_EQ(report.status, ValidationStatus::FailedSpan);
  const RuleObservation* observation = findObservation(report, "parallel_fifth");
  ASSERT_NE(observation, nullptr);
  EXPECT_EQ(observation->total, 1);
  EXPECT_EQ(observation->gated, 1);
  EXPECT_EQ(observation->exempted, 0);
  expectAccountingHolds(report);
}

TEST(CounterpointObservationTest, InformationalFindingIsCountedButNeitherGatedNorExempted) {
  // Final-score audit of fully authored material: recorded as informational
  // evidence, which the accounting identity must absorb.
  std::vector<NoteEvent> notes = parallelFifthNotes();
  std::vector<NoteProvenance> prov(notes.size(), makeProv(0, NoteSource::Material));
  for (std::size_t index = 0; index < notes.size(); ++index)
    bindAuthoredNote(notes[index], &prov[index]);

  const ValidationReport report =
      Validator{}.validate(notes, prov, cMajorWhole(), Material{}, ValidationScope::FinalScore);
  ASSERT_EQ(report.status, ValidationStatus::Ok);
  const RuleObservation* observation = findObservation(report, "parallel_fifth");
  ASSERT_NE(observation, nullptr);
  EXPECT_EQ(observation->total, 1);
  EXPECT_EQ(observation->gated, 0);
  EXPECT_EQ(observation->exempted, 0);
  expectAccountingHolds(report);
}

TEST(CounterpointObservationTest, EntriesAreSortedAndOnlyPresentWhenTheRuleMatched) {
  const ValidationReport clean =
      Validator{}.validate({}, {}, cMajorWhole(), Material{}, ValidationScope::Generation);
  EXPECT_TRUE(clean.observations.empty()) << "a piece with no notes observed something";

  const std::vector<NoteEvent> notes = parallelFifthNotes();
  const std::vector<NoteProvenance> prov(notes.size(), makeProv(0, NoteSource::Material));
  const ValidationReport report = Validator{}.validate(notes, prov, cMajorWhole());
  for (std::size_t index = 1; index < report.observations.size(); ++index) {
    EXPECT_LT(report.observations[index - 1].rule_id, report.observations[index].rule_id)
        << "observations must be sorted by rule_id to stay diff-stable";
  }
  for (const RuleObservation& entry : report.observations)
    EXPECT_GT(entry.total, 0) << entry.rule_id << ": an entry exists with nothing counted";
}

// --- Shipped output ----------------------------------------------------------

TEST(CounterpointObservationTest, ShippedFormsObserveVerticalFindingsWithoutGatingThem) {
  // trio_sonata and passacaglia both write real three-voice counterpoint over
  // fixed material, so vertical findings are expected. They must be counted
  // (the channel is wired) and none of them may gate (every operand is an
  // immutable carrier, which is the pre-existing exemption).
  std::map<std::string, RuleObservation> totals;
  std::size_t composed = 0;
  for (const FormType form : {FormType::TrioSonata, FormType::Passacaglia}) {
    for (std::uint32_t offset = 0; offset < kSeedCount; ++offset) {
      ComposeRequest request;
      request.form = form;
      request.seed = kFirstSeed + offset;
      HarnessFixture fixture;
      ASSERT_EQ(buildFormFixture(request, &fixture), FormDirectorStatus::Ok);
      const ComposeResult result =
          Composer{}.run(fixture.material, fixture.harmony, fixture.voice_plan);
      ++composed;
      for (const RuleObservation& entry : result.validation.observations) {
        RuleObservation& sum = totals[entry.rule_id];
        sum.rule_id = entry.rule_id;
        sum.total += entry.total;
        sum.gated += entry.gated;
        sum.exempted += entry.exempted;
      }
      expectAccountingHolds(result.validation);
    }
  }
  ASSERT_EQ(composed, 2 * kSeedCount);

  int vertical_total = 0;
  for (const auto& [rule_id, sum] : totals) {
    const RuleGeometry geometry = counterpointRuleGeometry(rule_id);
    EXPECT_NE(geometry, RuleGeometry::Unclassified)
        << rule_id << " reached the recorder without a geometry table entry";
    if (geometry != RuleGeometry::Vertical)
      continue;
    vertical_total += sum.total;
    EXPECT_EQ(sum.gated, 0) << rule_id << " gated a shipped carrier finding";
    EXPECT_EQ(sum.exempted, sum.total)
        << rule_id << ": a shipped finding was neither gated nor exempted";
  }
  EXPECT_GT(vertical_total, 0)
      << "no vertical counterpoint finding was observed across trio_sonata and passacaglia, "
         "which means the observation channel is not wired rather than that the output is clean";
}

TEST(CounterpointObservationTest, EveryObservedShippedRuleHasAGeometry) {
  for (const FormType form : kShippedForms) {
    for (std::uint32_t offset = 0; offset < kSeedCount; ++offset) {
      ComposeRequest request;
      request.form = form;
      request.seed = kFirstSeed + offset;
      HarnessFixture fixture;
      ASSERT_EQ(buildFormFixture(request, &fixture), FormDirectorStatus::Ok)
          << static_cast<int>(form) << " seed " << request.seed;
      const ComposeResult result =
          Composer{}.run(fixture.material, fixture.harmony, fixture.voice_plan);
      for (const RuleObservation& entry : result.validation.observations) {
        EXPECT_NE(counterpointRuleGeometry(entry.rule_id), RuleGeometry::Unclassified)
            << entry.rule_id << " reached the recorder without a geometry table entry";
      }
    }
  }
}

// --- Output neutrality -------------------------------------------------------

TEST(CounterpointObservationTest, CountingFindingsLeavesEveryShippedVerdictClean) {
  // Counting is not gating: recording an exempted finding must not turn it into
  // a failure. `status == Ok` with empty failures stays the shipped contract.
  for (const FormType form : kShippedForms) {
    for (std::uint32_t offset = 0; offset < kSeedCount; ++offset) {
      ComposeRequest request;
      request.form = form;
      request.seed = kFirstSeed + offset;
      HarnessFixture fixture;
      ASSERT_EQ(buildFormFixture(request, &fixture), FormDirectorStatus::Ok)
          << static_cast<int>(form) << " seed " << request.seed;
      const ComposeResult result =
          Composer{}.run(fixture.material, fixture.harmony, fixture.voice_plan);
      EXPECT_EQ(result.validation.status, ValidationStatus::Ok)
          << static_cast<int>(form) << " seed " << request.seed;
      EXPECT_TRUE(result.validation.failures.empty())
          << static_cast<int>(form) << " seed " << request.seed << " reported "
          << result.validation.failures.size() << " failures, first "
          << result.validation.failures.front().rule_id;
    }
  }
}

}  // namespace bach::composer
