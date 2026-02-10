// Tests for fugue/fugue_structure.h -- section types, phase ordering,
// structure validation, filtering, and JSON serialization.

#include "fugue/fugue_structure.h"

#include <gtest/gtest.h>

#include <string>

namespace bach {
namespace {

// ---------------------------------------------------------------------------
// sectionTypeToString
// ---------------------------------------------------------------------------

TEST(FugueStructureTest, SectionTypeToStringExposition) {
  EXPECT_STREQ(sectionTypeToString(SectionType::Exposition), "Exposition");
}

TEST(FugueStructureTest, SectionTypeToStringEpisode) {
  EXPECT_STREQ(sectionTypeToString(SectionType::Episode), "Episode");
}

TEST(FugueStructureTest, SectionTypeToStringMiddleEntry) {
  EXPECT_STREQ(sectionTypeToString(SectionType::MiddleEntry), "MiddleEntry");
}

TEST(FugueStructureTest, SectionTypeToStringStretto) {
  EXPECT_STREQ(sectionTypeToString(SectionType::Stretto), "Stretto");
}

TEST(FugueStructureTest, SectionTypeToStringCoda) {
  EXPECT_STREQ(sectionTypeToString(SectionType::Coda), "Coda");
}

// ---------------------------------------------------------------------------
// FugueSection::durationTicks
// ---------------------------------------------------------------------------

TEST(FugueStructureTest, FugueSectionDuration) {
  FugueSection section;
  section.start_tick = kTicksPerBar;      // 1920
  section.end_tick = kTicksPerBar * 5;    // 9600
  EXPECT_EQ(section.durationTicks(), kTicksPerBar * 4);  // 7680
}

TEST(FugueStructureTest, FugueSectionDurationZero) {
  FugueSection section;
  section.start_tick = 1000;
  section.end_tick = 1000;
  EXPECT_EQ(section.durationTicks(), 0u);
}

// ---------------------------------------------------------------------------
// addSection -- valid phase ordering
// ---------------------------------------------------------------------------

TEST(FugueStructureTest, AddSectionValidOrder) {
  FugueStructure structure;

  // Establish -> Develop -> Resolve
  EXPECT_TRUE(structure.addSection(
      SectionType::Exposition, FuguePhase::Establish, 0, kTicksPerBar * 4, Key::C));
  EXPECT_TRUE(structure.addSection(
      SectionType::Episode, FuguePhase::Develop, kTicksPerBar * 4, kTicksPerBar * 8, Key::G));
  EXPECT_TRUE(structure.addSection(
      SectionType::Coda, FuguePhase::Resolve, kTicksPerBar * 8, kTicksPerBar * 12, Key::C));

  EXPECT_EQ(structure.sectionCount(), 3u);
}

TEST(FugueStructureTest, AddSectionSamePhase) {
  FugueStructure structure;

  // Multiple sections in the same phase is allowed.
  EXPECT_TRUE(structure.addSection(
      SectionType::Exposition, FuguePhase::Establish, 0, kTicksPerBar * 4));
  EXPECT_TRUE(structure.addSection(
      SectionType::Episode, FuguePhase::Develop, kTicksPerBar * 4, kTicksPerBar * 8));
  EXPECT_TRUE(structure.addSection(
      SectionType::MiddleEntry, FuguePhase::Develop, kTicksPerBar * 8, kTicksPerBar * 12));
  EXPECT_TRUE(structure.addSection(
      SectionType::Episode, FuguePhase::Develop, kTicksPerBar * 12, kTicksPerBar * 16));

  EXPECT_EQ(structure.sectionCount(), 4u);
}

// ---------------------------------------------------------------------------
// addSection -- invalid phase ordering
// ---------------------------------------------------------------------------

TEST(FugueStructureTest, AddSectionInvalidOrderResolveToEstablish) {
  FugueStructure structure;

  EXPECT_TRUE(structure.addSection(
      SectionType::Coda, FuguePhase::Resolve, 0, kTicksPerBar * 4));
  // Trying to go backward: Resolve -> Establish should fail.
  EXPECT_FALSE(structure.addSection(
      SectionType::Exposition, FuguePhase::Establish,
      kTicksPerBar * 4, kTicksPerBar * 8));

  EXPECT_EQ(structure.sectionCount(), 1u);
}

TEST(FugueStructureTest, AddSectionInvalidOrderDevelopToEstablish) {
  FugueStructure structure;

  EXPECT_TRUE(structure.addSection(
      SectionType::Exposition, FuguePhase::Establish, 0, kTicksPerBar * 4));
  EXPECT_TRUE(structure.addSection(
      SectionType::Episode, FuguePhase::Develop, kTicksPerBar * 4, kTicksPerBar * 8));
  // Trying to go backward: Develop -> Establish should fail.
  EXPECT_FALSE(structure.addSection(
      SectionType::MiddleEntry, FuguePhase::Establish,
      kTicksPerBar * 8, kTicksPerBar * 12));

  EXPECT_EQ(structure.sectionCount(), 2u);
}

TEST(FugueStructureTest, AddSectionFirstSectionAlwaysSucceeds) {
  // Any phase for first section should be accepted.
  FugueStructure structure;
  EXPECT_TRUE(structure.addSection(
      SectionType::Stretto, FuguePhase::Develop, 0, kTicksPerBar * 4));
  EXPECT_EQ(structure.sectionCount(), 1u);
}

// ---------------------------------------------------------------------------
// validate -- empty structure
// ---------------------------------------------------------------------------

TEST(FugueStructureTest, ValidateEmptyStructure) {
  FugueStructure structure;
  auto violations = structure.validate();
  EXPECT_FALSE(violations.empty());
  // Should mention empty structure.
  EXPECT_NE(violations[0].find("empty"), std::string::npos);
}

// ---------------------------------------------------------------------------
// validate -- valid structure
// ---------------------------------------------------------------------------

TEST(FugueStructureTest, ValidateValidStructure) {
  FugueStructure structure;
  structure.addSection(
      SectionType::Exposition, FuguePhase::Establish, 0, kTicksPerBar * 4, Key::C);
  structure.addSection(
      SectionType::Episode, FuguePhase::Develop,
      kTicksPerBar * 4, kTicksPerBar * 8, Key::G);
  structure.addSection(
      SectionType::MiddleEntry, FuguePhase::Develop,
      kTicksPerBar * 8, kTicksPerBar * 12, Key::D);
  structure.addSection(
      SectionType::Stretto, FuguePhase::Resolve,
      kTicksPerBar * 12, kTicksPerBar * 16, Key::C);
  structure.addSection(
      SectionType::Coda, FuguePhase::Resolve,
      kTicksPerBar * 16, kTicksPerBar * 20, Key::C);

  auto violations = structure.validate();
  EXPECT_TRUE(violations.empty())
      << "Expected no violations, got: " << violations.front();
}

// ---------------------------------------------------------------------------
// validate -- missing exposition
// ---------------------------------------------------------------------------

TEST(FugueStructureTest, ValidateMissingExposition) {
  FugueStructure structure;
  // Start with Episode instead of Exposition.
  structure.addSection(
      SectionType::Episode, FuguePhase::Establish, 0, kTicksPerBar * 4);

  auto violations = structure.validate();
  EXPECT_FALSE(violations.empty());

  // Should mention that first section must be Exposition.
  bool found_exposition_error = false;
  for (const auto& violation : violations) {
    if (violation.find("Exposition") != std::string::npos) {
      found_exposition_error = true;
      break;
    }
  }
  EXPECT_TRUE(found_exposition_error);
}

TEST(FugueStructureTest, ValidateFirstSectionWrongPhase) {
  FugueStructure structure;
  // Exposition but in wrong phase.
  structure.addSection(
      SectionType::Exposition, FuguePhase::Develop, 0, kTicksPerBar * 4);

  auto violations = structure.validate();
  EXPECT_FALSE(violations.empty());

  // Should mention Establish phase requirement.
  bool found_phase_error = false;
  for (const auto& violation : violations) {
    if (violation.find("Establish") != std::string::npos) {
      found_phase_error = true;
      break;
    }
  }
  EXPECT_TRUE(found_phase_error);
}

TEST(FugueStructureTest, ValidateOverlappingSections) {
  FugueStructure structure;
  // Sections overlap in tick space.
  structure.addSection(
      SectionType::Exposition, FuguePhase::Establish, 0, kTicksPerBar * 6);
  structure.addSection(
      SectionType::Episode, FuguePhase::Develop,
      kTicksPerBar * 4, kTicksPerBar * 10);  // Overlaps previous

  auto violations = structure.validate();
  EXPECT_FALSE(violations.empty());

  bool found_overlap_error = false;
  for (const auto& violation : violations) {
    if (violation.find("starts before") != std::string::npos) {
      found_overlap_error = true;
      break;
    }
  }
  EXPECT_TRUE(found_overlap_error);
}

// ---------------------------------------------------------------------------
// getSectionsByPhase
// ---------------------------------------------------------------------------

TEST(FugueStructureTest, GetSectionsByPhase) {
  FugueStructure structure;
  structure.addSection(
      SectionType::Exposition, FuguePhase::Establish, 0, kTicksPerBar * 4);
  structure.addSection(
      SectionType::Episode, FuguePhase::Develop,
      kTicksPerBar * 4, kTicksPerBar * 8);
  structure.addSection(
      SectionType::MiddleEntry, FuguePhase::Develop,
      kTicksPerBar * 8, kTicksPerBar * 12);
  structure.addSection(
      SectionType::Coda, FuguePhase::Resolve,
      kTicksPerBar * 12, kTicksPerBar * 16);

  auto establish = structure.getSectionsByPhase(FuguePhase::Establish);
  EXPECT_EQ(establish.size(), 1u);
  EXPECT_EQ(establish[0].type, SectionType::Exposition);

  auto develop = structure.getSectionsByPhase(FuguePhase::Develop);
  EXPECT_EQ(develop.size(), 2u);
  EXPECT_EQ(develop[0].type, SectionType::Episode);
  EXPECT_EQ(develop[1].type, SectionType::MiddleEntry);

  auto resolve = structure.getSectionsByPhase(FuguePhase::Resolve);
  EXPECT_EQ(resolve.size(), 1u);
  EXPECT_EQ(resolve[0].type, SectionType::Coda);
}

TEST(FugueStructureTest, GetSectionsByPhaseEmpty) {
  FugueStructure structure;
  structure.addSection(
      SectionType::Exposition, FuguePhase::Establish, 0, kTicksPerBar * 4);

  auto develop = structure.getSectionsByPhase(FuguePhase::Develop);
  EXPECT_TRUE(develop.empty());
}

// ---------------------------------------------------------------------------
// getSectionsByType
// ---------------------------------------------------------------------------

TEST(FugueStructureTest, GetSectionsByType) {
  FugueStructure structure;
  structure.addSection(
      SectionType::Exposition, FuguePhase::Establish, 0, kTicksPerBar * 4);
  structure.addSection(
      SectionType::Episode, FuguePhase::Develop,
      kTicksPerBar * 4, kTicksPerBar * 8);
  structure.addSection(
      SectionType::MiddleEntry, FuguePhase::Develop,
      kTicksPerBar * 8, kTicksPerBar * 12);
  structure.addSection(
      SectionType::Episode, FuguePhase::Develop,
      kTicksPerBar * 12, kTicksPerBar * 16);

  auto episodes = structure.getSectionsByType(SectionType::Episode);
  EXPECT_EQ(episodes.size(), 2u);
  EXPECT_EQ(episodes[0].start_tick, kTicksPerBar * 4);
  EXPECT_EQ(episodes[1].start_tick, kTicksPerBar * 12);

  auto expositions = structure.getSectionsByType(SectionType::Exposition);
  EXPECT_EQ(expositions.size(), 1u);

  auto strettos = structure.getSectionsByType(SectionType::Stretto);
  EXPECT_TRUE(strettos.empty());
}

// ---------------------------------------------------------------------------
// totalDurationTicks
// ---------------------------------------------------------------------------

TEST(FugueStructureTest, TotalDurationEmpty) {
  FugueStructure structure;
  EXPECT_EQ(structure.totalDurationTicks(), 0u);
}

TEST(FugueStructureTest, TotalDuration) {
  FugueStructure structure;
  structure.addSection(
      SectionType::Exposition, FuguePhase::Establish, 0, kTicksPerBar * 4);
  structure.addSection(
      SectionType::Episode, FuguePhase::Develop,
      kTicksPerBar * 4, kTicksPerBar * 10);

  EXPECT_EQ(structure.totalDurationTicks(), kTicksPerBar * 10);
}

// ---------------------------------------------------------------------------
// sectionCount
// ---------------------------------------------------------------------------

TEST(FugueStructureTest, SectionCountEmpty) {
  FugueStructure structure;
  EXPECT_EQ(structure.sectionCount(), 0u);
}

TEST(FugueStructureTest, SectionCountAfterAdds) {
  FugueStructure structure;
  structure.addSection(
      SectionType::Exposition, FuguePhase::Establish, 0, kTicksPerBar * 4);
  EXPECT_EQ(structure.sectionCount(), 1u);

  structure.addSection(
      SectionType::Episode, FuguePhase::Develop,
      kTicksPerBar * 4, kTicksPerBar * 8);
  EXPECT_EQ(structure.sectionCount(), 2u);
}

// ---------------------------------------------------------------------------
// toJson
// ---------------------------------------------------------------------------

TEST(FugueStructureTest, ToJsonContainsExpectedFields) {
  FugueStructure structure;
  structure.addSection(
      SectionType::Exposition, FuguePhase::Establish,
      0, kTicksPerBar * 4, Key::C);
  structure.addSection(
      SectionType::Episode, FuguePhase::Develop,
      kTicksPerBar * 4, kTicksPerBar * 8, Key::G);

  std::string json = structure.toJson();

  // Check top-level fields.
  EXPECT_NE(json.find("\"section_count\":2"), std::string::npos);
  EXPECT_NE(json.find("\"total_duration_ticks\":"), std::string::npos);
  EXPECT_NE(json.find("\"sections\":["), std::string::npos);

  // Check section fields.
  EXPECT_NE(json.find("\"type\":\"Exposition\""), std::string::npos);
  EXPECT_NE(json.find("\"type\":\"Episode\""), std::string::npos);
  EXPECT_NE(json.find("\"phase\":\"Establish\""), std::string::npos);
  EXPECT_NE(json.find("\"phase\":\"Develop\""), std::string::npos);
  EXPECT_NE(json.find("\"key\":\"C\""), std::string::npos);
  EXPECT_NE(json.find("\"key\":\"G\""), std::string::npos);
  EXPECT_NE(json.find("\"start_tick\":"), std::string::npos);
  EXPECT_NE(json.find("\"end_tick\":"), std::string::npos);
  EXPECT_NE(json.find("\"duration_ticks\":"), std::string::npos);
}

TEST(FugueStructureTest, ToJsonEmptyStructure) {
  FugueStructure structure;
  std::string json = structure.toJson();

  EXPECT_NE(json.find("\"section_count\":0"), std::string::npos);
  EXPECT_NE(json.find("\"total_duration_ticks\":0"), std::string::npos);
  EXPECT_NE(json.find("\"sections\":[]"), std::string::npos);
}

// ---------------------------------------------------------------------------
// FugueSection defaults
// ---------------------------------------------------------------------------

TEST(FugueStructureTest, FugueSectionDefaults) {
  FugueSection section;
  EXPECT_EQ(section.type, SectionType::Exposition);
  EXPECT_EQ(section.phase, FuguePhase::Establish);
  EXPECT_EQ(section.start_tick, 0u);
  EXPECT_EQ(section.end_tick, 0u);
  EXPECT_EQ(section.key, Key::C);
  EXPECT_EQ(section.durationTicks(), 0u);
}

// ---------------------------------------------------------------------------
// addSection preserves key
// ---------------------------------------------------------------------------

TEST(FugueStructureTest, AddSectionPreservesKey) {
  FugueStructure structure;
  structure.addSection(
      SectionType::Exposition, FuguePhase::Establish,
      0, kTicksPerBar * 4, Key::G);

  EXPECT_EQ(structure.sections[0].key, Key::G);
}

TEST(FugueStructureTest, AddSectionDefaultKey) {
  FugueStructure structure;
  structure.addSection(
      SectionType::Exposition, FuguePhase::Establish, 0, kTicksPerBar * 4);

  // Default key parameter is Key::C.
  EXPECT_EQ(structure.sections[0].key, Key::C);
}

}  // namespace
}  // namespace bach
