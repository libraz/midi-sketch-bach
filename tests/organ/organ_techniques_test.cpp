// Tests for shared organ performance techniques.

#include "organ/organ_techniques.h"

#include <gtest/gtest.h>

#include "core/basic_types.h"
#include "core/pitch_utils.h"
#include "harmony/key.h"

namespace bach {
namespace {

// ---------------------------------------------------------------------------
// Cadential pedal point
// ---------------------------------------------------------------------------

TEST(CadentialPedalTest, TonicProducesCorrectPitch) {
  KeySignature key = {Key::C, false};
  auto notes = generateCadentialPedal(key, 0, kTicksPerBar * 2,
                                      PedalPointType::Tonic, 3);
  ASSERT_FALSE(notes.empty());
  for (const auto& n : notes) {
    EXPECT_EQ(n.pitch % 12, 0) << "Tonic pedal in C should be pitch class C";
    EXPECT_EQ(n.voice, 3);
    EXPECT_EQ(n.source, BachNoteSource::PedalPoint);
  }
}

TEST(CadentialPedalTest, DominantProducesCorrectPitch) {
  KeySignature key = {Key::C, false};
  auto notes = generateCadentialPedal(key, 0, kTicksPerBar * 2,
                                      PedalPointType::Dominant, 3);
  ASSERT_FALSE(notes.empty());
  for (const auto& n : notes) {
    EXPECT_EQ(n.pitch % 12, 7) << "Dominant pedal in C should be pitch class G";
  }
}

TEST(CadentialPedalTest, MinorKeyTonicPitch) {
  KeySignature key = {Key::D, true};  // D minor.
  auto notes = generateCadentialPedal(key, 0, kTicksPerBar,
                                      PedalPointType::Tonic, 3);
  ASSERT_FALSE(notes.empty());
  for (const auto& n : notes) {
    EXPECT_EQ(n.pitch % 12, 2) << "Tonic pedal in D minor should be pitch class D";
  }
}

TEST(CadentialPedalTest, NoOverlapBetweenRearticulatedNotes) {
  KeySignature key = {Key::C, false};
  auto notes = generateCadentialPedal(key, 0, kTicksPerBar * 4,
                                      PedalPointType::Tonic, 3);
  for (size_t i = 1; i < notes.size(); ++i) {
    Tick prev_end = notes[i - 1].start_tick + notes[i - 1].duration;
    EXPECT_LE(prev_end, notes[i].start_tick)
        << "Pedal notes must not overlap";
  }
}

TEST(CadentialPedalTest, EmptyWhenStartEqualsEnd) {
  KeySignature key = {Key::C, false};
  auto notes = generateCadentialPedal(key, 1000, 1000,
                                      PedalPointType::Tonic, 3);
  EXPECT_TRUE(notes.empty());
}

// ---------------------------------------------------------------------------
// Picardy third
// ---------------------------------------------------------------------------

TEST(PicardyTest, MinorKeyRaisesThird) {
  KeySignature key = {Key::C, true};  // C minor.
  std::vector<NoteEvent> notes;

  // Create a chord in the final bar with Eb (minor 3rd of C).
  NoteEvent root;
  root.start_tick = kTicksPerBar * 10;
  root.duration = kTicksPerBar;
  root.pitch = 48;  // C3
  root.velocity = kOrganVelocity;
  notes.push_back(root);

  NoteEvent third;
  third.start_tick = kTicksPerBar * 10;
  third.duration = kTicksPerBar;
  third.pitch = 51;  // Eb3 (minor 3rd)
  third.velocity = kOrganVelocity;
  notes.push_back(third);

  NoteEvent fifth;
  fifth.start_tick = kTicksPerBar * 10;
  fifth.duration = kTicksPerBar;
  fifth.pitch = 55;  // G3
  fifth.velocity = kOrganVelocity;
  notes.push_back(fifth);

  applyPicardyToFinalChord(notes, key, kTicksPerBar * 10);

  // Eb3 (51) should become E3 (52).
  EXPECT_EQ(notes[0].pitch, 48) << "Root should not change";
  EXPECT_EQ(notes[1].pitch, 52) << "Minor 3rd should be raised to major 3rd";
  EXPECT_EQ(notes[2].pitch, 55) << "Fifth should not change";
}

TEST(PicardyTest, MajorKeyNoChange) {
  KeySignature key = {Key::C, false};  // C major.
  std::vector<NoteEvent> notes;

  NoteEvent note;
  note.start_tick = kTicksPerBar * 10;
  note.duration = kTicksPerBar;
  note.pitch = 52;  // E3 (already major 3rd)
  note.velocity = kOrganVelocity;
  notes.push_back(note);

  applyPicardyToFinalChord(notes, key, kTicksPerBar * 10);

  EXPECT_EQ(notes[0].pitch, 52) << "Major key should not change any pitch";
}

TEST(PicardyTest, DMinorRaisesF) {
  KeySignature key = {Key::D, true};  // D minor.
  std::vector<NoteEvent> notes;

  NoteEvent note;
  note.start_tick = kTicksPerBar * 5;
  note.duration = kTicksPerBar;
  note.pitch = 53;  // F3 (minor 3rd of D)
  note.velocity = kOrganVelocity;
  notes.push_back(note);

  applyPicardyToFinalChord(notes, key, kTicksPerBar * 5);

  EXPECT_EQ(notes[0].pitch, 54) << "F should be raised to F# in D minor Picardy";
}

// ---------------------------------------------------------------------------
// Block chord
// ---------------------------------------------------------------------------

TEST(BlockChordTest, VoiceCountMatchesInput) {
  KeySignature key = {Key::C, false};
  std::vector<std::pair<uint8_t, uint8_t>> ranges = {
      {36, 96}, {36, 96}, {48, 96}, {24, 50}};

  auto notes = generateBlockChord(key, 0, kTicksPerBar, 4, ranges);
  EXPECT_EQ(notes.size(), 4u);

  for (size_t i = 0; i < notes.size(); ++i) {
    EXPECT_EQ(notes[i].voice, static_cast<uint8_t>(i));
    EXPECT_GE(notes[i].pitch, ranges[i].first);
    EXPECT_LE(notes[i].pitch, ranges[i].second);
  }
}

TEST(BlockChordTest, AllNotesAreChordTones) {
  KeySignature key = {Key::C, false};
  std::vector<std::pair<uint8_t, uint8_t>> ranges = {
      {36, 96}, {36, 96}};

  auto notes = generateBlockChord(key, 0, kTicksPerBar, 2, ranges);
  for (const auto& n : notes) {
    int pc = n.pitch % 12;
    // C major tonic triad: C(0), E(4), G(7).
    EXPECT_TRUE(pc == 0 || pc == 4 || pc == 7)
        << "Pitch class " << pc << " is not in C major triad";
  }
}

// ---------------------------------------------------------------------------
// Registration presets
// ---------------------------------------------------------------------------

TEST(RegistrationPresetsTest, FixedValues) {
  auto piano = OrganRegistrationPresets::piano();
  auto mezzo = OrganRegistrationPresets::mezzo();
  auto forte = OrganRegistrationPresets::forte();
  auto pleno = OrganRegistrationPresets::pleno();
  auto tutti = OrganRegistrationPresets::tutti();

  EXPECT_EQ(piano.velocity_hint, 60);
  EXPECT_EQ(mezzo.velocity_hint, 75);
  EXPECT_EQ(forte.velocity_hint, 90);
  EXPECT_EQ(pleno.velocity_hint, 100);
  EXPECT_EQ(tutti.velocity_hint, 110);

  // Ascending order.
  EXPECT_LT(piano.velocity_hint, mezzo.velocity_hint);
  EXPECT_LT(mezzo.velocity_hint, forte.velocity_hint);
  EXPECT_LT(forte.velocity_hint, pleno.velocity_hint);
  EXPECT_LT(pleno.velocity_hint, tutti.velocity_hint);
}

// ---------------------------------------------------------------------------
// Registration plans
// ---------------------------------------------------------------------------

TEST(RegistrationPlanTest, SimpleHasThreePoints) {
  auto plan = createSimpleRegistrationPlan(0, kTicksPerBar * 20);
  EXPECT_EQ(plan.size(), 3u);
}

TEST(RegistrationPlanTest, VariationHasCorrectCount) {
  auto plan = createVariationRegistrationPlan(8, kTicksPerBar * 4);
  EXPECT_EQ(plan.size(), 8u);
}

TEST(RegistrationPlanTest, VariationProgressivelyLouder) {
  auto plan = createVariationRegistrationPlan(8, kTicksPerBar * 4);
  // First variation should be quieter than last.
  EXPECT_LT(plan.points.front().registration.velocity_hint,
             plan.points.back().registration.velocity_hint);
}

}  // namespace
}  // namespace bach
