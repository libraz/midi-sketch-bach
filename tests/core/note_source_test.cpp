// Tests for BachNoteSource, BachTransformStep, and NoteProvenance.

#include "core/note_source.h"

#include <cstring>

#include <gtest/gtest.h>

namespace bach {
namespace {

// ---------------------------------------------------------------------------
// BachNoteSource string conversion tests
// ---------------------------------------------------------------------------

TEST(BachNoteSourceTest, AllSourcesHaveStringRepresentation) {
  // Every defined source must produce a non-empty, non-null string.
  const BachNoteSource all_sources[] = {
      BachNoteSource::Unknown,
      BachNoteSource::FugueSubject,
      BachNoteSource::FugueAnswer,
      BachNoteSource::Countersubject,
      BachNoteSource::EpisodeMaterial,
      BachNoteSource::FreeCounterpoint,
      BachNoteSource::CantusFixed,
      BachNoteSource::Ornament,
      BachNoteSource::PedalPoint,
      BachNoteSource::ArpeggioFlow,
      BachNoteSource::TextureNote,
      BachNoteSource::GroundBass,
      BachNoteSource::CollisionAvoid,
      BachNoteSource::PostProcess,
      BachNoteSource::ChromaticPassing,
      BachNoteSource::FalseEntry,
      BachNoteSource::Coda,
  };

  for (auto src : all_sources) {
    const char* str = bachNoteSourceToString(src);
    ASSERT_NE(str, nullptr) << "Source " << static_cast<int>(src) << " returned nullptr";
    EXPECT_GT(std::strlen(str), 0u)
        << "Source " << static_cast<int>(src) << " returned empty string";
  }
}

TEST(BachNoteSourceTest, SpecificSourceStringsMatchJsonFormat) {
  // These strings must match the provenance.source values in output.json.
  EXPECT_STREQ(bachNoteSourceToString(BachNoteSource::Unknown), "unknown");
  EXPECT_STREQ(bachNoteSourceToString(BachNoteSource::FugueSubject), "fugue_subject");
  EXPECT_STREQ(bachNoteSourceToString(BachNoteSource::FugueAnswer), "fugue_answer");
  EXPECT_STREQ(bachNoteSourceToString(BachNoteSource::Countersubject), "countersubject");
  EXPECT_STREQ(bachNoteSourceToString(BachNoteSource::EpisodeMaterial), "episode_material");
  EXPECT_STREQ(bachNoteSourceToString(BachNoteSource::FreeCounterpoint), "free_counterpoint");
  EXPECT_STREQ(bachNoteSourceToString(BachNoteSource::CantusFixed), "cantus_fixed");
  EXPECT_STREQ(bachNoteSourceToString(BachNoteSource::Ornament), "ornament");
  EXPECT_STREQ(bachNoteSourceToString(BachNoteSource::PedalPoint), "pedal_point");
  EXPECT_STREQ(bachNoteSourceToString(BachNoteSource::ArpeggioFlow), "arpeggio_flow");
  EXPECT_STREQ(bachNoteSourceToString(BachNoteSource::TextureNote), "texture_note");
  EXPECT_STREQ(bachNoteSourceToString(BachNoteSource::GroundBass), "ground_bass");
  EXPECT_STREQ(bachNoteSourceToString(BachNoteSource::CollisionAvoid), "collision_avoid");
  EXPECT_STREQ(bachNoteSourceToString(BachNoteSource::PostProcess), "post_process");
  EXPECT_STREQ(bachNoteSourceToString(BachNoteSource::ChromaticPassing), "chromatic_passing");
  EXPECT_STREQ(bachNoteSourceToString(BachNoteSource::FalseEntry), "false_entry");
  EXPECT_STREQ(bachNoteSourceToString(BachNoteSource::Coda), "coda");
}

// ---------------------------------------------------------------------------
// BachTransformStep string conversion tests
// ---------------------------------------------------------------------------

TEST(BachTransformStepTest, AllStepsHaveStringRepresentation) {
  const BachTransformStep all_steps[] = {
      BachTransformStep::None,
      BachTransformStep::TonalAnswer,
      BachTransformStep::RealAnswer,
      BachTransformStep::Inversion,
      BachTransformStep::Retrograde,
      BachTransformStep::Augmentation,
      BachTransformStep::Diminution,
      BachTransformStep::Sequence,
      BachTransformStep::CollisionAvoid,
      BachTransformStep::RangeClamp,
      BachTransformStep::OctaveAdjust,
      BachTransformStep::KeyTranspose,
  };

  for (auto step : all_steps) {
    const char* str = bachTransformStepToString(step);
    ASSERT_NE(str, nullptr) << "Step " << static_cast<int>(step) << " returned nullptr";
    EXPECT_GT(std::strlen(str), 0u)
        << "Step " << static_cast<int>(step) << " returned empty string";
  }
}

TEST(BachTransformStepTest, SpecificStepStringsMatchJsonFormat) {
  EXPECT_STREQ(bachTransformStepToString(BachTransformStep::None), "none");
  EXPECT_STREQ(bachTransformStepToString(BachTransformStep::TonalAnswer), "tonal_answer");
  EXPECT_STREQ(bachTransformStepToString(BachTransformStep::RealAnswer), "real_answer");
  EXPECT_STREQ(bachTransformStepToString(BachTransformStep::Inversion), "inversion");
  EXPECT_STREQ(bachTransformStepToString(BachTransformStep::Retrograde), "retrograde");
  EXPECT_STREQ(bachTransformStepToString(BachTransformStep::Augmentation), "augmentation");
  EXPECT_STREQ(bachTransformStepToString(BachTransformStep::Diminution), "diminution");
  EXPECT_STREQ(bachTransformStepToString(BachTransformStep::Sequence), "sequence");
  EXPECT_STREQ(bachTransformStepToString(BachTransformStep::CollisionAvoid), "collision_avoid");
  EXPECT_STREQ(bachTransformStepToString(BachTransformStep::RangeClamp), "range_clamp");
  EXPECT_STREQ(bachTransformStepToString(BachTransformStep::OctaveAdjust), "octave_adjust");
  EXPECT_STREQ(bachTransformStepToString(BachTransformStep::KeyTranspose), "key_transpose");
}

// ---------------------------------------------------------------------------
// NoteProvenance tests
// ---------------------------------------------------------------------------

TEST(NoteProvenanceTest, DefaultConstructionHasNoProvenance) {
  NoteProvenance prov;
  EXPECT_FALSE(prov.hasProvenance());
  EXPECT_EQ(prov.source, BachNoteSource::Unknown);
  EXPECT_EQ(prov.original_pitch, 0);
  EXPECT_EQ(prov.chord_degree, -1);
  EXPECT_EQ(prov.lookup_tick, 0u);
  EXPECT_EQ(prov.entry_number, 0);
  EXPECT_EQ(prov.step_count, 0);
}

TEST(NoteProvenanceTest, HasProvenanceReturnsTrueWhenSourceSet) {
  NoteProvenance prov;
  prov.source = BachNoteSource::FugueSubject;
  EXPECT_TRUE(prov.hasProvenance());
}

TEST(NoteProvenanceTest, AddStepAppendsToTrail) {
  NoteProvenance prov;

  EXPECT_TRUE(prov.addStep(BachTransformStep::TonalAnswer));
  EXPECT_EQ(prov.step_count, 1);
  EXPECT_EQ(prov.steps[0], BachTransformStep::TonalAnswer);

  EXPECT_TRUE(prov.addStep(BachTransformStep::CollisionAvoid));
  EXPECT_EQ(prov.step_count, 2);
  EXPECT_EQ(prov.steps[1], BachTransformStep::CollisionAvoid);

  EXPECT_TRUE(prov.addStep(BachTransformStep::RangeClamp));
  EXPECT_EQ(prov.step_count, 3);
  EXPECT_EQ(prov.steps[2], BachTransformStep::RangeClamp);
}

TEST(NoteProvenanceTest, AddStepRejectsWhenFull) {
  NoteProvenance prov;

  // Fill all kMaxSteps slots.
  for (size_t idx = 0; idx < NoteProvenance::kMaxSteps; ++idx) {
    EXPECT_TRUE(prov.addStep(BachTransformStep::Sequence))
        << "Failed to add step at index " << idx;
  }
  EXPECT_EQ(prov.step_count, NoteProvenance::kMaxSteps);

  // Next add must fail.
  EXPECT_FALSE(prov.addStep(BachTransformStep::KeyTranspose));
  EXPECT_EQ(prov.step_count, NoteProvenance::kMaxSteps);
}

TEST(NoteProvenanceTest, MaxStepsIsEight) {
  EXPECT_EQ(NoteProvenance::kMaxSteps, 8u);
}

TEST(NoteProvenanceTest, StepOrderPreserved) {
  NoteProvenance prov;
  prov.addStep(BachTransformStep::RealAnswer);
  prov.addStep(BachTransformStep::Inversion);
  prov.addStep(BachTransformStep::OctaveAdjust);
  prov.addStep(BachTransformStep::KeyTranspose);

  EXPECT_EQ(prov.steps[0], BachTransformStep::RealAnswer);
  EXPECT_EQ(prov.steps[1], BachTransformStep::Inversion);
  EXPECT_EQ(prov.steps[2], BachTransformStep::OctaveAdjust);
  EXPECT_EQ(prov.steps[3], BachTransformStep::KeyTranspose);
  EXPECT_EQ(prov.step_count, 4);
}

// ---------------------------------------------------------------------------
// ProtectionLevel mapping tests
// ---------------------------------------------------------------------------

TEST(ProtectionLevelTest, ImmutableSources) {
  EXPECT_EQ(getProtectionLevel(BachNoteSource::FugueSubject),
            ProtectionLevel::Immutable);
  EXPECT_EQ(getProtectionLevel(BachNoteSource::CantusFixed),
            ProtectionLevel::Immutable);
  EXPECT_EQ(getProtectionLevel(BachNoteSource::GroundBass),
            ProtectionLevel::Immutable);
}

TEST(ProtectionLevelTest, StructuralSources) {
  EXPECT_EQ(getProtectionLevel(BachNoteSource::FugueAnswer),
            ProtectionLevel::Structural);
  EXPECT_EQ(getProtectionLevel(BachNoteSource::Countersubject),
            ProtectionLevel::Structural);
  EXPECT_EQ(getProtectionLevel(BachNoteSource::PedalPoint),
            ProtectionLevel::Structural);
  EXPECT_EQ(getProtectionLevel(BachNoteSource::FalseEntry),
            ProtectionLevel::Structural);
  EXPECT_EQ(getProtectionLevel(BachNoteSource::Coda),
            ProtectionLevel::Structural);
}

TEST(ProtectionLevelTest, FlexibleSources) {
  EXPECT_EQ(getProtectionLevel(BachNoteSource::EpisodeMaterial),
            ProtectionLevel::Flexible);
  EXPECT_EQ(getProtectionLevel(BachNoteSource::FreeCounterpoint),
            ProtectionLevel::Flexible);
  EXPECT_EQ(getProtectionLevel(BachNoteSource::Ornament),
            ProtectionLevel::Flexible);
  EXPECT_EQ(getProtectionLevel(BachNoteSource::ArpeggioFlow),
            ProtectionLevel::Flexible);
  EXPECT_EQ(getProtectionLevel(BachNoteSource::TextureNote),
            ProtectionLevel::Flexible);
  EXPECT_EQ(getProtectionLevel(BachNoteSource::CollisionAvoid),
            ProtectionLevel::Flexible);
  EXPECT_EQ(getProtectionLevel(BachNoteSource::PostProcess),
            ProtectionLevel::Flexible);
  EXPECT_EQ(getProtectionLevel(BachNoteSource::ChromaticPassing),
            ProtectionLevel::Flexible);
  EXPECT_EQ(getProtectionLevel(BachNoteSource::Unknown),
            ProtectionLevel::Flexible);
}

TEST(ProtectionLevelTest, AllSourcesCovered) {
  // Ensure every source maps to a valid protection level.
  const BachNoteSource all[] = {
      BachNoteSource::Unknown,          BachNoteSource::FugueSubject,
      BachNoteSource::FugueAnswer,      BachNoteSource::Countersubject,
      BachNoteSource::EpisodeMaterial,   BachNoteSource::FreeCounterpoint,
      BachNoteSource::CantusFixed,       BachNoteSource::Ornament,
      BachNoteSource::PedalPoint,        BachNoteSource::ArpeggioFlow,
      BachNoteSource::TextureNote,       BachNoteSource::GroundBass,
      BachNoteSource::CollisionAvoid,    BachNoteSource::PostProcess,
      BachNoteSource::ChromaticPassing,  BachNoteSource::FalseEntry,
      BachNoteSource::Coda,
  };
  for (auto src : all) {
    auto level = getProtectionLevel(src);
    EXPECT_TRUE(level == ProtectionLevel::Immutable ||
                level == ProtectionLevel::Structural ||
                level == ProtectionLevel::Flexible)
        << "Source " << bachNoteSourceToString(src) << " has invalid level";
  }
}

}  // namespace
}  // namespace bach
