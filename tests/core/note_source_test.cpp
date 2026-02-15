// Tests for BachNoteSource, BachTransformStep, NoteProvenance, and NoteModifiedBy.

#include "core/note_source.h"

#include <cstring>

#include <gtest/gtest.h>

#include "core/basic_types.h"

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
      BachNoteSource::SequenceNote,
      BachNoteSource::CanonDux,
      BachNoteSource::CanonComes,
      BachNoteSource::CanonFreeBass,
      BachNoteSource::GoldbergAria,
      BachNoteSource::GoldbergBass,
      BachNoteSource::QuodlibetMelody,
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
  EXPECT_STREQ(bachNoteSourceToString(BachNoteSource::SequenceNote), "sequence_note");
  EXPECT_STREQ(bachNoteSourceToString(BachNoteSource::CanonDux), "canon_dux");
  EXPECT_STREQ(bachNoteSourceToString(BachNoteSource::CanonComes), "canon_comes");
  EXPECT_STREQ(bachNoteSourceToString(BachNoteSource::CanonFreeBass), "canon_free_bass");
  EXPECT_STREQ(bachNoteSourceToString(BachNoteSource::GoldbergAria), "goldberg_aria");
  EXPECT_STREQ(bachNoteSourceToString(BachNoteSource::GoldbergBass), "goldberg_bass");
  EXPECT_STREQ(bachNoteSourceToString(BachNoteSource::QuodlibetMelody), "quodlibet_melody");
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
  EXPECT_EQ(getProtectionLevel(BachNoteSource::SubjectCore),
            ProtectionLevel::Immutable);
  EXPECT_EQ(getProtectionLevel(BachNoteSource::CantusFixed),
            ProtectionLevel::Immutable);
  EXPECT_EQ(getProtectionLevel(BachNoteSource::GroundBass),
            ProtectionLevel::Immutable);
  EXPECT_EQ(getProtectionLevel(BachNoteSource::GoldbergBass),
            ProtectionLevel::Immutable);
  EXPECT_EQ(getProtectionLevel(BachNoteSource::QuodlibetMelody),
            ProtectionLevel::Immutable);
}

TEST(ProtectionLevelTest, SemiImmutableSources) {
  EXPECT_EQ(getProtectionLevel(BachNoteSource::FugueSubject),
            ProtectionLevel::SemiImmutable);
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
  EXPECT_EQ(getProtectionLevel(BachNoteSource::SequenceNote),
            ProtectionLevel::Structural);
  EXPECT_EQ(getProtectionLevel(BachNoteSource::CanonDux),
            ProtectionLevel::Structural);
  EXPECT_EQ(getProtectionLevel(BachNoteSource::CanonComes),
            ProtectionLevel::Structural);
  EXPECT_EQ(getProtectionLevel(BachNoteSource::GoldbergAria),
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
  EXPECT_EQ(getProtectionLevel(BachNoteSource::CanonFreeBass),
            ProtectionLevel::Flexible);
}

TEST(ProtectionLevelTest, AllSourcesCovered) {
  // Ensure every source maps to a valid protection level.
  const BachNoteSource all[] = {
      BachNoteSource::Unknown,          BachNoteSource::FugueSubject,
      BachNoteSource::SubjectCore,       BachNoteSource::FugueAnswer,
      BachNoteSource::Countersubject,
      BachNoteSource::EpisodeMaterial,   BachNoteSource::FreeCounterpoint,
      BachNoteSource::CantusFixed,       BachNoteSource::Ornament,
      BachNoteSource::PedalPoint,        BachNoteSource::ArpeggioFlow,
      BachNoteSource::TextureNote,       BachNoteSource::GroundBass,
      BachNoteSource::CollisionAvoid,    BachNoteSource::PostProcess,
      BachNoteSource::ChromaticPassing,  BachNoteSource::FalseEntry,
      BachNoteSource::Coda,              BachNoteSource::SequenceNote,
      BachNoteSource::CanonDux,          BachNoteSource::CanonComes,
      BachNoteSource::CanonFreeBass,     BachNoteSource::GoldbergAria,
      BachNoteSource::GoldbergBass,      BachNoteSource::QuodlibetMelody,
  };
  for (auto src : all) {
    auto level = getProtectionLevel(src);
    EXPECT_TRUE(level == ProtectionLevel::Immutable ||
                level == ProtectionLevel::SemiImmutable ||
                level == ProtectionLevel::Structural ||
                level == ProtectionLevel::Flexible)
        << "Source " << bachNoteSourceToString(src) << " has invalid level";
  }
}

// ---------------------------------------------------------------------------
// isStructuralSource tests
// ---------------------------------------------------------------------------

TEST(IsStructuralSourceTest, CanonDuxIsStructural) {
  EXPECT_TRUE(isStructuralSource(BachNoteSource::CanonDux));
}

TEST(IsStructuralSourceTest, CanonComesIsStructural) {
  EXPECT_TRUE(isStructuralSource(BachNoteSource::CanonComes));
}

TEST(IsStructuralSourceTest, GoldbergAriaIsStructural) {
  EXPECT_TRUE(isStructuralSource(BachNoteSource::GoldbergAria));
}

TEST(IsStructuralSourceTest, CanonFreeBassIsNotStructural) {
  EXPECT_FALSE(isStructuralSource(BachNoteSource::CanonFreeBass));
}

TEST(IsStructuralSourceTest, GoldbergBassIsNotStructural) {
  EXPECT_FALSE(isStructuralSource(BachNoteSource::GoldbergBass));
}

TEST(IsStructuralSourceTest, QuodlibetMelodyIsNotStructural) {
  EXPECT_FALSE(isStructuralSource(BachNoteSource::QuodlibetMelody));
}

// ---------------------------------------------------------------------------
// NoteModifiedBy string conversion tests
// ---------------------------------------------------------------------------

TEST(NoteModifiedByTest, NoneReturnsNone) {
  EXPECT_EQ(noteModifiedByToString(0), "none");
}

TEST(NoteModifiedByTest, SingleFlags) {
  EXPECT_EQ(noteModifiedByToString(static_cast<uint8_t>(NoteModifiedBy::ParallelRepair)),
            "parallel_repair");
  EXPECT_EQ(noteModifiedByToString(static_cast<uint8_t>(NoteModifiedBy::ChordToneSnap)),
            "chord_tone_snap");
  EXPECT_EQ(noteModifiedByToString(static_cast<uint8_t>(NoteModifiedBy::LeapResolution)),
            "leap_resolution");
  EXPECT_EQ(noteModifiedByToString(static_cast<uint8_t>(NoteModifiedBy::OverlapTrim)),
            "overlap_trim");
  EXPECT_EQ(noteModifiedByToString(static_cast<uint8_t>(NoteModifiedBy::OctaveAdjust)),
            "octave_adjust");
  EXPECT_EQ(noteModifiedByToString(static_cast<uint8_t>(NoteModifiedBy::Articulation)),
            "articulation");
  EXPECT_EQ(noteModifiedByToString(static_cast<uint8_t>(NoteModifiedBy::RepeatedNoteRep)),
            "repeated_note_rep");
}

TEST(NoteModifiedByTest, CombinedFlags) {
  uint8_t flags = static_cast<uint8_t>(NoteModifiedBy::ParallelRepair) |
                  static_cast<uint8_t>(NoteModifiedBy::OctaveAdjust);
  EXPECT_EQ(noteModifiedByToString(flags), "parallel_repair,octave_adjust");
}

TEST(NoteModifiedByTest, AllFlags) {
  uint8_t flags = 0x7F;  // All 7 flags set
  EXPECT_EQ(noteModifiedByToString(flags),
            "parallel_repair,chord_tone_snap,leap_resolution,"
            "overlap_trim,octave_adjust,articulation,repeated_note_rep");
}

TEST(NoteModifiedByTest, BitwiseOrOperator) {
  NoteModifiedBy combined = NoteModifiedBy::ParallelRepair | NoteModifiedBy::ChordToneSnap;
  EXPECT_EQ(static_cast<uint8_t>(combined), 0x03);
}

TEST(NoteModifiedByTest, BitwiseOrAssignOperator) {
  NoteModifiedBy flags = NoteModifiedBy::None;
  flags |= NoteModifiedBy::OverlapTrim;
  flags |= NoteModifiedBy::Articulation;
  EXPECT_EQ(static_cast<uint8_t>(flags), 0x28);
}

TEST(NoteModifiedByTest, UnknownHighBitIgnored) {
  // Bit 7 (0x80) is not defined; verify it produces no named flag.
  uint8_t flags = 0x80;
  EXPECT_EQ(noteModifiedByToString(flags), "");
}

TEST(NoteModifiedByTest, EnumValuesArePowersOfTwo) {
  EXPECT_EQ(static_cast<uint8_t>(NoteModifiedBy::ParallelRepair),  1);
  EXPECT_EQ(static_cast<uint8_t>(NoteModifiedBy::ChordToneSnap),   2);
  EXPECT_EQ(static_cast<uint8_t>(NoteModifiedBy::LeapResolution),  4);
  EXPECT_EQ(static_cast<uint8_t>(NoteModifiedBy::OverlapTrim),     8);
  EXPECT_EQ(static_cast<uint8_t>(NoteModifiedBy::OctaveAdjust),   16);
  EXPECT_EQ(static_cast<uint8_t>(NoteModifiedBy::Articulation),   32);
  EXPECT_EQ(static_cast<uint8_t>(NoteModifiedBy::RepeatedNoteRep), 64);
}

// ---------------------------------------------------------------------------
// countUnknownSource tests
// ---------------------------------------------------------------------------

TEST(CountUnknownSourceTest, EmptyVectorReturnsZero) {
  std::vector<NoteEvent> notes;
  EXPECT_EQ(countUnknownSource(notes), 0);
}

TEST(CountUnknownSourceTest, AllKnownSourcesReturnsZero) {
  std::vector<NoteEvent> notes(3);
  notes[0].source = BachNoteSource::FugueSubject;
  notes[1].source = BachNoteSource::FreeCounterpoint;
  notes[2].source = BachNoteSource::PedalPoint;
  EXPECT_EQ(countUnknownSource(notes), 0);
}

TEST(CountUnknownSourceTest, AllUnknownReturnsCount) {
  std::vector<NoteEvent> notes(5);
  // Default source is Unknown.
  EXPECT_EQ(countUnknownSource(notes), 5);
}

TEST(CountUnknownSourceTest, MixedSourcesCountsOnlyUnknown) {
  std::vector<NoteEvent> notes(4);
  notes[0].source = BachNoteSource::Unknown;
  notes[1].source = BachNoteSource::FugueSubject;
  notes[2].source = BachNoteSource::Unknown;
  notes[3].source = BachNoteSource::PedalPoint;
  EXPECT_EQ(countUnknownSource(notes), 2);
}

// ---------------------------------------------------------------------------
// BachNoteSource completeness: all non-Unknown values must not map to "unknown"
// ---------------------------------------------------------------------------

TEST(BachNoteSourceTest, NonUnknownSourcesNeverReturnUnknownString) {
  const BachNoteSource non_unknown[] = {
      BachNoteSource::FugueSubject,     BachNoteSource::SubjectCore,
      BachNoteSource::FugueAnswer,      BachNoteSource::Countersubject,
      BachNoteSource::EpisodeMaterial,   BachNoteSource::FreeCounterpoint,
      BachNoteSource::CantusFixed,       BachNoteSource::Ornament,
      BachNoteSource::PedalPoint,        BachNoteSource::ArpeggioFlow,
      BachNoteSource::TextureNote,       BachNoteSource::GroundBass,
      BachNoteSource::CollisionAvoid,    BachNoteSource::PostProcess,
      BachNoteSource::ChromaticPassing,  BachNoteSource::FalseEntry,
      BachNoteSource::Coda,              BachNoteSource::SequenceNote,
      BachNoteSource::CanonDux,          BachNoteSource::CanonComes,
      BachNoteSource::CanonFreeBass,     BachNoteSource::GoldbergAria,
      BachNoteSource::GoldbergBass,      BachNoteSource::GoldbergFigura,
      BachNoteSource::GoldbergSoggetto,  BachNoteSource::GoldbergDance,
      BachNoteSource::GoldbergFughetta,  BachNoteSource::GoldbergInvention,
      BachNoteSource::QuodlibetMelody,   BachNoteSource::GoldbergOverture,
      BachNoteSource::GoldbergSuspension,
  };
  for (auto src : non_unknown) {
    const char* str = bachNoteSourceToString(src);
    EXPECT_STRNE(str, "unknown")
        << "Source " << static_cast<int>(src) << " incorrectly maps to \"unknown\"";
  }
}

}  // namespace
}  // namespace bach
