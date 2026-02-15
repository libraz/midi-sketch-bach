// Tests for ImpossibilityGuard: physical impossibility detection and repair.

#include "instrument/common/impossibility_guard.h"

#include <gtest/gtest.h>

#include "core/note_source.h"
#include "core/pitch_utils.h"
#include "forms/goldberg/goldberg_types.h"

namespace bach {
namespace {

// ---------------------------------------------------------------------------
// Helper: create a NoteEvent with given properties.
// ---------------------------------------------------------------------------

NoteEvent makeNote(uint8_t pitch, Tick start, Tick duration,
                   BachNoteSource source = BachNoteSource::FreeCounterpoint,
                   VoiceId voice = 0) {
  NoteEvent note;
  note.pitch = pitch;
  note.start_tick = start;
  note.duration = duration;
  note.velocity = 80;
  note.voice = voice;
  note.source = source;
  return note;
}

// ---------------------------------------------------------------------------
// createGuard: basic guard creation for all instrument types.
// ---------------------------------------------------------------------------

TEST(ImpossibilityGuardTest, CreateGuardForAllInstruments) {
  for (auto type : {InstrumentType::Organ, InstrumentType::Harpsichord,
                    InstrumentType::Piano, InstrumentType::Violin,
                    InstrumentType::Cello, InstrumentType::Guitar}) {
    auto guard = createGuard(type);
    EXPECT_TRUE(guard.isPitchPlayable != nullptr);
    EXPECT_TRUE(guard.fixPitchRange != nullptr);
    EXPECT_TRUE(guard.checkSounding != nullptr);
    EXPECT_TRUE(guard.repairSounding != nullptr);
  }
}

// ---------------------------------------------------------------------------
// isPitchPlayable: range checks.
// ---------------------------------------------------------------------------

TEST(ImpossibilityGuardTest, OrganAcceptsWideRange) {
  auto guard = createGuard(InstrumentType::Organ);
  EXPECT_TRUE(guard.isPitchPlayable(36));   // C2
  EXPECT_TRUE(guard.isPitchPlayable(60));   // C4
  EXPECT_TRUE(guard.isPitchPlayable(96));   // C7
  EXPECT_FALSE(guard.isPitchPlayable(10));  // Below organ range
}

TEST(ImpossibilityGuardTest, ViolinRejectsLowPitches) {
  auto guard = createGuard(InstrumentType::Violin);
  EXPECT_FALSE(guard.isPitchPlayable(40));  // Well below G3 (55)
  EXPECT_TRUE(guard.isPitchPlayable(55));   // G3 (lowest string)
  EXPECT_TRUE(guard.isPitchPlayable(76));   // E5 (highest open string)
}

TEST(ImpossibilityGuardTest, CelloRejectsHighPitches) {
  auto guard = createGuard(InstrumentType::Cello);
  EXPECT_TRUE(guard.isPitchPlayable(36));   // C2 (lowest string)
  EXPECT_TRUE(guard.isPitchPlayable(60));   // C4
  EXPECT_FALSE(guard.isPitchPlayable(100)); // Above cello range
}

TEST(ImpossibilityGuardTest, GuitarRange) {
  auto guard = createGuard(InstrumentType::Guitar);
  EXPECT_TRUE(guard.isPitchPlayable(40));   // E2 (lowest string)
  EXPECT_TRUE(guard.isPitchPlayable(64));   // E4
  EXPECT_FALSE(guard.isPitchPlayable(30));  // Below guitar range
}

TEST(ImpossibilityGuardTest, HarpsichordRange) {
  auto guard = createGuard(InstrumentType::Harpsichord);
  EXPECT_TRUE(guard.isPitchPlayable(29));    // F1 (harpsichord low)
  EXPECT_TRUE(guard.isPitchPlayable(89));    // F6 (harpsichord high)
  EXPECT_FALSE(guard.isPitchPlayable(20));   // Below harpsichord range
  EXPECT_FALSE(guard.isPitchPlayable(100));  // Above harpsichord range
}

TEST(ImpossibilityGuardTest, HarpsichordFixPitchUsesNarrowRange) {
  auto guard = createGuard(InstrumentType::Harpsichord);
  // Pitch 100 is above harpsichord range (29-89). fixPitchRange should correct it.
  uint8_t fixed = guard.fixPitchRange(100, ProtectionLevel::Flexible, 0);
  // 100-12=88, which is within [29,89]. Octave shift preferred.
  EXPECT_EQ(fixed, 88);
}

// ---------------------------------------------------------------------------
// fixPitchRange: ProtectionLevel-aware repair.
// ---------------------------------------------------------------------------

TEST(ImpossibilityGuardTest, ImmutableNeverModified) {
  auto guard = createGuard(InstrumentType::Violin);
  // Pitch 40 is below violin range (55), but Immutable = no touch.
  uint8_t result = guard.fixPitchRange(40, ProtectionLevel::Immutable, 0);
  EXPECT_EQ(result, 40);
}

TEST(ImpossibilityGuardTest, StructuralOctaveShift) {
  auto guard = createGuard(InstrumentType::Violin);
  // Pitch 43 (G2) is below violin range. +12 = 55 (G3, in range).
  uint8_t result = guard.fixPitchRange(43, ProtectionLevel::Structural, 0);
  EXPECT_EQ(result, 55);
}

TEST(ImpossibilityGuardTest, StructuralNoClamp) {
  auto guard = createGuard(InstrumentType::Violin);
  // Pitch 30 is far below range. +12 = 42, still below. Structural = don't clamp.
  uint8_t result = guard.fixPitchRange(30, ProtectionLevel::Structural, 0);
  EXPECT_EQ(result, 30);  // Unchanged (structural won't clamp).
}

TEST(ImpossibilityGuardTest, FlexibleClampsAsLastResort) {
  auto guard = createGuard(InstrumentType::Violin);
  // Pitch 30 is far below range (55-96). Octave shifts won't help.
  // Flexible = clamp to range.
  uint8_t result = guard.fixPitchRange(30, ProtectionLevel::Flexible, 0);
  EXPECT_EQ(result, 55);  // Clamped to lowest.
}

TEST(ImpossibilityGuardTest, InRangePitchUnchanged) {
  auto guard = createGuard(InstrumentType::Piano);
  // Pitch 60 (C4) is well within piano range.
  uint8_t result = guard.fixPitchRange(60, ProtectionLevel::Flexible, 0);
  EXPECT_EQ(result, 60);
}

TEST(ImpossibilityGuardTest, OctaveShiftPreservesMelodicContour) {
  auto guard = createGuard(InstrumentType::Violin);
  // Pitch 43 below range. prev_pitch = 60 (above). +12=55 is closer to 60.
  uint8_t result = guard.fixPitchRange(43, ProtectionLevel::Flexible, 60);
  EXPECT_EQ(result, 55);  // Shifted up (closer to prev).
}

// ---------------------------------------------------------------------------
// checkSounding: simultaneous note detection.
// ---------------------------------------------------------------------------

TEST(ImpossibilityGuardTest, OrganAlwaysValid) {
  auto guard = createGuard(InstrumentType::Organ);
  NoteEvent n1 = makeNote(60, 0, 480);
  NoteEvent n2 = makeNote(64, 0, 480);
  NoteEvent n3 = makeNote(67, 0, 480);
  SoundingGroup group{0, {&n1, &n2, &n3}};
  EXPECT_EQ(guard.checkSounding(group), Violation::None);
}

TEST(ImpossibilityGuardTest, ViolinThreeNotesViolation) {
  auto guard = createGuard(InstrumentType::Violin);
  NoteEvent n1 = makeNote(55, 0, 480);
  NoteEvent n2 = makeNote(62, 0, 480);
  NoteEvent n3 = makeNote(69, 0, 480);
  SoundingGroup group{0, {&n1, &n2, &n3}};
  EXPECT_EQ(guard.checkSounding(group), Violation::SimultaneousExceedsLimit);
}

TEST(ImpossibilityGuardTest, GuitarTwoNotesViolation) {
  auto guard = createGuard(InstrumentType::Guitar);
  NoteEvent n1 = makeNote(40, 0, 480);
  NoteEvent n2 = makeNote(55, 0, 480);
  SoundingGroup group{0, {&n1, &n2}};
  EXPECT_EQ(guard.checkSounding(group), Violation::SimultaneousExceedsLimit);
}

TEST(ImpossibilityGuardTest, ViolinSingleNoteValid) {
  auto guard = createGuard(InstrumentType::Violin);
  NoteEvent n1 = makeNote(60, 0, 480);
  SoundingGroup group{0, {&n1}};
  EXPECT_EQ(guard.checkSounding(group), Violation::None);
}

// ---------------------------------------------------------------------------
// repairSounding: bowed instruments.
// ---------------------------------------------------------------------------

TEST(ImpossibilityGuardTest, BowedRepairDropsFlexible) {
  auto guard = createGuard(InstrumentType::Violin);
  // Two notes on non-adjacent strings: impossible double stop.
  NoteEvent immutable = makeNote(55, 0, 480, BachNoteSource::FugueSubject);
  NoteEvent flexible = makeNote(76, 0, 480, BachNoteSource::FreeCounterpoint);
  SoundingGroup group{0, {&immutable, &flexible}};

  Violation v = guard.checkSounding(group);
  if (v != Violation::None) {
    guard.repairSounding(group);
  }
  // Flexible note should be dropped (duration=0) or modified.
  // Either dropped or octave shifted is acceptable.
  EXPECT_TRUE(flexible.duration == 0 || flexible.pitch != 76);
}

// ---------------------------------------------------------------------------
// enforceImpossibilityGuard: full pipeline.
// ---------------------------------------------------------------------------

TEST(ImpossibilityGuardTest, EnforceFixesOutOfRange) {
  auto guard = createGuard(InstrumentType::Violin);
  Track track;
  track.channel = 0;
  // Note below violin range (55).
  track.notes.push_back(makeNote(40, 0, 480, BachNoteSource::FreeCounterpoint));
  // Note in range.
  track.notes.push_back(makeNote(60, 480, 480, BachNoteSource::FreeCounterpoint));

  std::vector<Track> tracks = {track};
  uint32_t changes = enforceImpossibilityGuard(tracks, guard);

  EXPECT_GT(changes, 0u);
  // First note should have been adjusted into range.
  EXPECT_GE(tracks[0].notes[0].pitch, 55);
}

TEST(ImpossibilityGuardTest, EnforcePreservesImmutable) {
  auto guard = createGuard(InstrumentType::Violin);
  Track track;
  track.channel = 0;
  // Immutable note below range: should NOT be modified.
  track.notes.push_back(makeNote(40, 0, 480, BachNoteSource::SubjectCore));

  std::vector<Track> tracks = {track};
  enforceImpossibilityGuard(tracks, guard);

  // Immutable pitch must be preserved.
  EXPECT_EQ(tracks[0].notes[0].pitch, 40);
}

TEST(ImpossibilityGuardTest, EnforceOrganNoChanges) {
  auto guard = createGuard(InstrumentType::Organ);
  Track track;
  track.channel = 0;
  track.notes.push_back(makeNote(60, 0, 480));
  track.notes.push_back(makeNote(64, 0, 480));
  track.notes.push_back(makeNote(67, 0, 480));

  std::vector<Track> tracks = {track};
  uint32_t changes = enforceImpossibilityGuard(tracks, guard);

  // All notes in range, organ has no sounding limits.
  EXPECT_EQ(changes, 0u);
  EXPECT_EQ(tracks[0].notes.size(), 3u);
}

TEST(ImpossibilityGuardTest, EnforceDroppedNotesRemoved) {
  auto guard = createGuard(InstrumentType::Guitar);
  Track track;
  track.channel = 0;
  // Two simultaneous notes on guitar (single-voice instrument).
  track.notes.push_back(makeNote(50, 0, 480, BachNoteSource::FreeCounterpoint));
  track.notes.push_back(makeNote(55, 0, 480, BachNoteSource::FreeCounterpoint));

  std::vector<Track> tracks = {track};
  enforceImpossibilityGuard(tracks, guard);

  // At least one note should remain; dropped notes (duration=0) removed.
  for (const auto& note : tracks[0].notes) {
    EXPECT_GT(note.duration, 0u);
  }
}

// ---------------------------------------------------------------------------
// ManualPolicy for Goldberg.
// ---------------------------------------------------------------------------

TEST(ManualPolicyTest, StandardForCanon) {
  EXPECT_EQ(getManualPolicy(GoldbergVariationType::Canon),
            ManualPolicy::Standard);
}

TEST(ManualPolicyTest, HandCrossingForHandCrossing) {
  EXPECT_EQ(getManualPolicy(GoldbergVariationType::HandCrossing),
            ManualPolicy::HandCrossing);
}

TEST(ManualPolicyTest, SingleManualForToccata) {
  EXPECT_EQ(getManualPolicy(GoldbergVariationType::Toccata),
            ManualPolicy::SingleManual);
}

TEST(ManualPolicyTest, SingleManualForBravuraChordal) {
  EXPECT_EQ(getManualPolicy(GoldbergVariationType::BravuraChordal),
            ManualPolicy::SingleManual);
}

TEST(ManualPolicyTest, StandardIsDefault) {
  EXPECT_EQ(getManualPolicy(GoldbergVariationType::Ornamental),
            ManualPolicy::Standard);
  EXPECT_EQ(getManualPolicy(GoldbergVariationType::Fughetta),
            ManualPolicy::Standard);
  EXPECT_EQ(getManualPolicy(GoldbergVariationType::AllaBreveFugal),
            ManualPolicy::Standard);
}

}  // namespace
}  // namespace bach
