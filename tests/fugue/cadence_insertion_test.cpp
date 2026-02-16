// Tests for fugue/cadence_insertion.h -- cadence detection, deficiency scanning,
// and minimal cadential formula insertion.

#include "fugue/cadence_insertion.h"

#include <gtest/gtest.h>

#include <vector>

#include "core/basic_types.h"
#include "core/note_source.h"
#include "core/pitch_utils.h"
#include "fugue/fugue_structure.h"

namespace bach {
namespace {

// ---------------------------------------------------------------------------
// Helper: create a note at a given tick, pitch, and voice.
// ---------------------------------------------------------------------------
NoteEvent makeNote(Tick tick, uint8_t pitch, VoiceId voice,
                   Tick dur = kTicksPerBeat,
                   BachNoteSource src = BachNoteSource::EpisodeMaterial) {
  NoteEvent note;
  note.start_tick = tick;
  note.duration = dur;
  note.pitch = pitch;
  note.velocity = 80;
  note.voice = voice;
  note.source = src;
  return note;
}

// ---------------------------------------------------------------------------
// hasCadentialResolution
// ---------------------------------------------------------------------------

class CadentialResolutionTest : public ::testing::Test {};

TEST_F(CadentialResolutionTest, DetectsLeadingToneToTonicInCMajor) {
  // C major: leading tone = B (pitch class 11), tonic = C (pitch class 0).
  std::vector<NoteEvent> notes;
  // B4 at tick 480, then C5 at tick 960.
  notes.push_back(makeNote(480, 71, 0));   // B4
  notes.push_back(makeNote(960, 72, 0));   // C5

  // Check at tick 720 (between the two notes).
  EXPECT_TRUE(hasCadentialResolution(notes, 720, Key::C));
}

TEST_F(CadentialResolutionTest, DetectsResolutionInGMinor) {
  // G minor: leading tone = F# (pitch class 6), tonic = G (pitch class 7).
  std::vector<NoteEvent> notes;
  notes.push_back(makeNote(1000, 66, 1));  // F#4
  notes.push_back(makeNote(1480, 67, 1));  // G4

  EXPECT_TRUE(hasCadentialResolution(notes, 1200, Key::G));
}

TEST_F(CadentialResolutionTest, DetectsCrossVoiceResolution) {
  // Leading tone in voice 0, tonic in voice 2.
  std::vector<NoteEvent> notes;
  notes.push_back(makeNote(500, 71, 0));   // B4 in voice 0
  notes.push_back(makeNote(800, 60, 2));   // C4 in voice 2

  EXPECT_TRUE(hasCadentialResolution(notes, 650, Key::C));
}

TEST_F(CadentialResolutionTest, RejectsTonicBeforeLeadingTone) {
  // Resolution must be leading tone THEN tonic, not reversed.
  std::vector<NoteEvent> notes;
  notes.push_back(makeNote(500, 60, 0));   // C4 (tonic first)
  notes.push_back(makeNote(1000, 71, 0));  // B4 (leading tone after)

  EXPECT_FALSE(hasCadentialResolution(notes, 750, Key::C));
}

TEST_F(CadentialResolutionTest, RejectsOutsideWindow) {
  // Notes too far apart (more than 4 beats window).
  std::vector<NoteEvent> notes;
  notes.push_back(makeNote(0, 71, 0));       // B4 at tick 0
  notes.push_back(makeNote(5000, 60, 0));    // C4 at tick 5000

  // Check at tick 2500 -- B is within window but C is 2500 ticks after B,
  // exceeding the 960-tick radius from B.
  EXPECT_FALSE(hasCadentialResolution(notes, 2500, Key::C));
}

TEST_F(CadentialResolutionTest, RejectsNoLeadingTone) {
  // No leading tone present.
  std::vector<NoteEvent> notes;
  notes.push_back(makeNote(500, 60, 0));  // C4 (tonic only)
  notes.push_back(makeNote(700, 67, 0));  // G4 (dominant)

  EXPECT_FALSE(hasCadentialResolution(notes, 600, Key::C));
}

TEST_F(CadentialResolutionTest, EmptyNotesReturnsFalse) {
  std::vector<NoteEvent> notes;
  EXPECT_FALSE(hasCadentialResolution(notes, 1000, Key::C));
}

// ---------------------------------------------------------------------------
// isInSubjectEntry
// ---------------------------------------------------------------------------

class SubjectEntryDetectionTest : public ::testing::Test {
 protected:
  FugueStructure buildStructure() {
    FugueStructure structure;
    structure.addSection(SectionType::Exposition, FuguePhase::Establish,
                         0, kTicksPerBar * 4, Key::C);
    structure.addSection(SectionType::Episode, FuguePhase::Develop,
                         kTicksPerBar * 4, kTicksPerBar * 8, Key::G);
    structure.addSection(SectionType::MiddleEntry, FuguePhase::Develop,
                         kTicksPerBar * 8, kTicksPerBar * 12, Key::G);
    structure.addSection(SectionType::Episode, FuguePhase::Develop,
                         kTicksPerBar * 12, kTicksPerBar * 16, Key::C);
    structure.addSection(SectionType::Stretto, FuguePhase::Resolve,
                         kTicksPerBar * 16, kTicksPerBar * 20, Key::C);
    structure.addSection(SectionType::Coda, FuguePhase::Resolve,
                         kTicksPerBar * 20, kTicksPerBar * 22, Key::C);
    return structure;
  }
};

TEST_F(SubjectEntryDetectionTest, ExpositionIsSubjectEntry) {
  auto structure = buildStructure();
  EXPECT_TRUE(isInSubjectEntry(structure, kTicksPerBar * 2));
}

TEST_F(SubjectEntryDetectionTest, EpisodeIsNotSubjectEntry) {
  auto structure = buildStructure();
  EXPECT_FALSE(isInSubjectEntry(structure, kTicksPerBar * 6));
}

TEST_F(SubjectEntryDetectionTest, MiddleEntryIsSubjectEntry) {
  auto structure = buildStructure();
  EXPECT_TRUE(isInSubjectEntry(structure, kTicksPerBar * 10));
}

TEST_F(SubjectEntryDetectionTest, StrettoIsSubjectEntry) {
  auto structure = buildStructure();
  EXPECT_TRUE(isInSubjectEntry(structure, kTicksPerBar * 18));
}

TEST_F(SubjectEntryDetectionTest, CodaIsNotSubjectEntry) {
  auto structure = buildStructure();
  EXPECT_FALSE(isInSubjectEntry(structure, kTicksPerBar * 21));
}

TEST_F(SubjectEntryDetectionTest, EmptyStructureReturnsFalse) {
  FugueStructure structure;
  EXPECT_FALSE(isInSubjectEntry(structure, 1000));
}

// ---------------------------------------------------------------------------
// detectCadenceDeficiencies
// ---------------------------------------------------------------------------

class CadenceDeficiencyTest : public ::testing::Test {
 protected:
  FugueStructure buildLongStructure() {
    FugueStructure structure;
    structure.addSection(SectionType::Exposition, FuguePhase::Establish,
                         0, kTicksPerBar * 4, Key::C);
    // Long episode: 4-40 bars (no subject entry).
    structure.addSection(SectionType::Episode, FuguePhase::Develop,
                         kTicksPerBar * 4, kTicksPerBar * 40, Key::G);
    structure.addSection(SectionType::Stretto, FuguePhase::Resolve,
                         kTicksPerBar * 40, kTicksPerBar * 44, Key::C);
    return structure;
  }
};

TEST_F(CadenceDeficiencyTest, NoCadenceInLongStretch_FlagsDeficiency) {
  auto structure = buildLongStructure();
  Tick total_duration = kTicksPerBar * 44;

  // Create notes with NO leading-tone -> tonic resolution.
  std::vector<NoteEvent> notes;
  for (Tick tick = 0; tick < total_duration; tick += kTicksPerBeat) {
    notes.push_back(makeNote(tick, 60, 0));  // All C4 -- no leading tone.
  }

  CadenceDetectionConfig config;
  config.max_bars_without_cadence = 16;

  auto deficiencies = detectCadenceDeficiencies(notes, structure, Key::C,
                                                total_duration, config);
  EXPECT_GE(deficiencies.size(), 1u);
}

TEST_F(CadenceDeficiencyTest, FrequentCadences_NoDeficiency) {
  auto structure = buildLongStructure();
  Tick total_duration = kTicksPerBar * 44;

  // Create notes with cadential resolutions every 8 bars.
  std::vector<NoteEvent> notes;
  for (Tick bar = 0; bar < 44; ++bar) {
    Tick bar_start = bar * kTicksPerBar;
    notes.push_back(makeNote(bar_start, 60, 0));  // C4 filler.

    // Every 8 bars, add a B->C resolution.
    if (bar % 8 == 7) {
      notes.push_back(makeNote(bar_start + kTicksPerBeat, 71, 1));      // B4
      notes.push_back(makeNote(bar_start + kTicksPerBeat * 2, 72, 1));  // C5
    }
  }

  CadenceDetectionConfig config;
  config.max_bars_without_cadence = 16;

  auto deficiencies = detectCadenceDeficiencies(notes, structure, Key::C,
                                                total_duration, config);
  EXPECT_EQ(deficiencies.size(), 0u);
}

TEST_F(CadenceDeficiencyTest, SubjectEntrySkipped) {
  // Structure where the long gap is entirely within a subject entry.
  FugueStructure structure;
  structure.addSection(SectionType::Exposition, FuguePhase::Establish,
                       0, kTicksPerBar * 30, Key::C);
  structure.addSection(SectionType::Coda, FuguePhase::Resolve,
                       kTicksPerBar * 30, kTicksPerBar * 32, Key::C);
  Tick total_duration = kTicksPerBar * 32;

  // No cadences at all, but the gap is within an Exposition.
  std::vector<NoteEvent> notes;
  for (Tick tick = 0; tick < total_duration; tick += kTicksPerBeat) {
    notes.push_back(makeNote(tick, 60, 0));
  }

  CadenceDetectionConfig config;
  config.max_bars_without_cadence = 16;

  auto deficiencies = detectCadenceDeficiencies(notes, structure, Key::C,
                                                total_duration, config);
  // Deficiencies should be filtered: the insertion tick falls within Exposition.
  for (const auto& def : deficiencies) {
    EXPECT_FALSE(isInSubjectEntry(structure, def.insertion_tick));
  }
}

TEST_F(CadenceDeficiencyTest, EmptyNotesReturnsEmpty) {
  FugueStructure structure;
  std::vector<NoteEvent> notes;

  auto deficiencies = detectCadenceDeficiencies(notes, structure, Key::C, 0);
  EXPECT_TRUE(deficiencies.empty());
}

TEST_F(CadenceDeficiencyTest, FantasiaRelaxedThreshold) {
  // With 24-bar threshold (fantasia), a 20-bar gap should NOT flag deficiency.
  FugueStructure structure;
  // No sections (fantasia has no FugueStructure).
  Tick total_duration = kTicksPerBar * 20;

  std::vector<NoteEvent> notes;
  for (Tick tick = 0; tick < total_duration; tick += kTicksPerBeat) {
    notes.push_back(makeNote(tick, 60, 0));
  }

  CadenceDetectionConfig config;
  config.max_bars_without_cadence = 24;

  auto deficiencies = detectCadenceDeficiencies(notes, structure, Key::C,
                                                total_duration, config);
  EXPECT_EQ(deficiencies.size(), 0u);
}

// ---------------------------------------------------------------------------
// insertCadentialFormulas
// ---------------------------------------------------------------------------

class CadenceInsertionTest : public ::testing::Test {};

TEST_F(CadenceInsertionTest, InsertsNotesAtDeficiencyPoint) {
  std::vector<NoteEvent> notes;
  notes.push_back(makeNote(0, 60, 0));  // Filler.

  CadenceDeficiency deficiency;
  deficiency.region_start = kTicksPerBar * 4;
  deficiency.region_end = kTicksPerBar * 24;
  deficiency.insertion_tick = kTicksPerBar * 14;

  std::vector<CadenceDeficiency> deficiencies = {deficiency};

  int count = insertCadentialFormulas(notes, deficiencies, Key::C, false,
                                     3, 4, 42);
  EXPECT_EQ(count, 1);

  // Should have added 2 notes (dominant + resolution).
  EXPECT_EQ(notes.size(), 3u);  // 1 original + 2 cadence notes.

  // Verify the inserted notes have correct source.
  for (size_t idx = 1; idx < notes.size(); ++idx) {
    EXPECT_EQ(notes[idx].source, BachNoteSource::EpisodeMaterial);
    EXPECT_EQ(notes[idx].voice, 3u);
  }
}

TEST_F(CadenceInsertionTest, DominantPrecedesResolution) {
  std::vector<NoteEvent> notes;

  CadenceDeficiency deficiency;
  deficiency.region_start = kTicksPerBar * 4;
  deficiency.region_end = kTicksPerBar * 24;
  deficiency.insertion_tick = kTicksPerBar * 14;

  int count = insertCadentialFormulas(notes, {deficiency}, Key::C, false,
                                     3, 4, 42);
  EXPECT_EQ(count, 1);
  ASSERT_EQ(notes.size(), 2u);

  // First note (dominant) should start before the second (resolution).
  EXPECT_LT(notes[0].start_tick, notes[1].start_tick);

  // Dominant should be V (G in C major, pitch class 7).
  EXPECT_EQ(getPitchClass(notes[0].pitch), 7);  // G

  // Resolution should be I (C, pitch class 0) or vi (A, pitch class 9).
  int res_pc = getPitchClass(notes[1].pitch);
  EXPECT_TRUE(res_pc == 0 || res_pc == 9);  // C or A
}

TEST_F(CadenceInsertionTest, PitchesInBassRange) {
  std::vector<NoteEvent> notes;

  CadenceDeficiency deficiency;
  deficiency.region_start = 0;
  deficiency.region_end = kTicksPerBar * 30;
  deficiency.insertion_tick = kTicksPerBar * 15;

  insertCadentialFormulas(notes, {deficiency}, Key::C, false, 3, 4, 42);

  // Bass voice (voice 3 in 4-voice fugue) range is typically 24-50.
  for (const auto& note : notes) {
    EXPECT_GE(note.pitch, 24u);
    EXPECT_LE(note.pitch, 50u);
  }
}

TEST_F(CadenceInsertionTest, NoDeficienciesNoInsertion) {
  std::vector<NoteEvent> notes;
  notes.push_back(makeNote(0, 60, 0));

  int count = insertCadentialFormulas(notes, {}, Key::C, false, 3, 4, 42);
  EXPECT_EQ(count, 0);
  EXPECT_EQ(notes.size(), 1u);
}

TEST_F(CadenceInsertionTest, DeceptiveCadenceProducesViResolution) {
  // Force deceptive cadence by setting probability to 1.0.
  CadenceDetectionConfig config;
  config.deceptive_cadence_probability = 1.0f;

  std::vector<NoteEvent> notes;
  CadenceDeficiency deficiency;
  deficiency.region_start = 0;
  deficiency.region_end = kTicksPerBar * 30;
  deficiency.insertion_tick = kTicksPerBar * 15;

  insertCadentialFormulas(notes, {deficiency}, Key::C, false, 3, 4, 42, config);

  ASSERT_EQ(notes.size(), 2u);
  // Resolution of deceptive cadence: vi = A (pitch class 9 in C major).
  EXPECT_EQ(getPitchClass(notes[1].pitch), 9);
}

TEST_F(CadenceInsertionTest, PerfectCadenceProducesTonicResolution) {
  // Force perfect cadence by setting deceptive probability to 0.0.
  CadenceDetectionConfig config;
  config.deceptive_cadence_probability = 0.0f;

  std::vector<NoteEvent> notes;
  CadenceDeficiency deficiency;
  deficiency.region_start = 0;
  deficiency.region_end = kTicksPerBar * 30;
  deficiency.insertion_tick = kTicksPerBar * 15;

  insertCadentialFormulas(notes, {deficiency}, Key::C, false, 3, 4, 42, config);

  ASSERT_EQ(notes.size(), 2u);
  // Resolution of perfect cadence: I = C (pitch class 0).
  EXPECT_EQ(getPitchClass(notes[1].pitch), 0);
}

TEST_F(CadenceInsertionTest, MinorKeyDeceptiveCadenceUsesFlat6) {
  // C minor deceptive: V -> bVI. bVI = Ab (pitch class 8).
  CadenceDetectionConfig config;
  config.deceptive_cadence_probability = 1.0f;

  std::vector<NoteEvent> notes;
  CadenceDeficiency deficiency;
  deficiency.region_start = 0;
  deficiency.region_end = kTicksPerBar * 30;
  deficiency.insertion_tick = kTicksPerBar * 15;

  insertCadentialFormulas(notes, {deficiency}, Key::C, true, 3, 4, 42, config);

  ASSERT_EQ(notes.size(), 2u);
  // Minor key vi = Ab (pitch class 8).
  EXPECT_EQ(getPitchClass(notes[1].pitch), 8);
}

// ---------------------------------------------------------------------------
// ensureCadentialCoverage (integration test)
// ---------------------------------------------------------------------------

class CadentialCoverageTest : public ::testing::Test {};

TEST_F(CadentialCoverageTest, InsertsWhenDeficient) {
  FugueStructure structure;
  structure.addSection(SectionType::Exposition, FuguePhase::Establish,
                       0, kTicksPerBar * 4, Key::C);
  structure.addSection(SectionType::Episode, FuguePhase::Develop,
                       kTicksPerBar * 4, kTicksPerBar * 40, Key::G);
  structure.addSection(SectionType::Stretto, FuguePhase::Resolve,
                       kTicksPerBar * 40, kTicksPerBar * 44, Key::C);
  Tick total_duration = kTicksPerBar * 44;

  // Notes with no cadences in the episode region.
  std::vector<NoteEvent> notes;
  for (Tick tick = 0; tick < total_duration; tick += kTicksPerBeat * 2) {
    notes.push_back(makeNote(tick, 60, 0));
  }
  size_t original_count = notes.size();

  int count = ensureCadentialCoverage(notes, structure, Key::C, false,
                                      3, 4, total_duration, 42);
  EXPECT_GT(count, 0);
  EXPECT_GT(notes.size(), original_count);
}

TEST_F(CadentialCoverageTest, NoInsertionWhenCovered) {
  FugueStructure structure;
  structure.addSection(SectionType::Exposition, FuguePhase::Establish,
                       0, kTicksPerBar * 4, Key::C);
  structure.addSection(SectionType::Episode, FuguePhase::Develop,
                       kTicksPerBar * 4, kTicksPerBar * 20, Key::G);
  structure.addSection(SectionType::Stretto, FuguePhase::Resolve,
                       kTicksPerBar * 20, kTicksPerBar * 24, Key::C);
  Tick total_duration = kTicksPerBar * 24;

  // Notes with cadential resolutions every 4 bars.
  std::vector<NoteEvent> notes;
  for (Tick bar = 0; bar < 24; ++bar) {
    Tick bar_start = bar * kTicksPerBar;
    notes.push_back(makeNote(bar_start, 60, 0));
    if (bar % 4 == 3) {
      notes.push_back(makeNote(bar_start + kTicksPerBeat, 71, 1));      // B4
      notes.push_back(makeNote(bar_start + kTicksPerBeat * 2, 72, 1));  // C5
    }
  }
  size_t original_count = notes.size();

  int count = ensureCadentialCoverage(notes, structure, Key::C, false,
                                      3, 4, total_duration, 42);
  EXPECT_EQ(count, 0);
  EXPECT_EQ(notes.size(), original_count);
}

TEST_F(CadentialCoverageTest, DeterministicWithSameSeed) {
  FugueStructure structure;
  structure.addSection(SectionType::Exposition, FuguePhase::Establish,
                       0, kTicksPerBar * 4, Key::C);
  structure.addSection(SectionType::Episode, FuguePhase::Develop,
                       kTicksPerBar * 4, kTicksPerBar * 40, Key::G);
  structure.addSection(SectionType::Stretto, FuguePhase::Resolve,
                       kTicksPerBar * 40, kTicksPerBar * 44, Key::C);
  Tick total_duration = kTicksPerBar * 44;

  auto makeNotes = [&]() {
    std::vector<NoteEvent> notes;
    for (Tick tick = 0; tick < total_duration; tick += kTicksPerBeat * 2) {
      notes.push_back(makeNote(tick, 60, 0));
    }
    return notes;
  };

  std::vector<NoteEvent> notes1 = makeNotes();
  std::vector<NoteEvent> notes2 = makeNotes();

  ensureCadentialCoverage(notes1, structure, Key::C, false, 3, 4,
                          total_duration, 42);
  ensureCadentialCoverage(notes2, structure, Key::C, false, 3, 4,
                          total_duration, 42);

  ASSERT_EQ(notes1.size(), notes2.size());
  for (size_t idx = 0; idx < notes1.size(); ++idx) {
    EXPECT_EQ(notes1[idx].pitch, notes2[idx].pitch);
    EXPECT_EQ(notes1[idx].start_tick, notes2[idx].start_tick);
    EXPECT_EQ(notes1[idx].voice, notes2[idx].voice);
  }
}

}  // namespace
}  // namespace bach
