// Tests for solo_string/arch/ground_bass.h -- immutable ground bass.

#include "solo_string/arch/ground_bass.h"

#include <vector>

#include <gtest/gtest.h>

#include "analysis/fail_report.h"

#include "core/basic_types.h"
#include "harmony/key.h"

namespace bach {
namespace {

// ===========================================================================
// Standard D minor creation
// ===========================================================================

TEST(GroundBassTest, StandardDMinorHasSevenNotes) {
  auto bass = GroundBass::createStandardDMinor();
  EXPECT_EQ(bass.noteCount(), 7u);
}

TEST(GroundBassTest, StandardDMinorStartsOnD3) {
  auto bass = GroundBass::createStandardDMinor();
  const auto& notes = bass.getNotes();
  ASSERT_FALSE(notes.empty());
  // D3 = MIDI 50
  EXPECT_EQ(notes[0].pitch, 50u);
}

TEST(GroundBassTest, StandardDMinorLengthIsFourBars) {
  auto bass = GroundBass::createStandardDMinor();
  // 4 bars = 4 * kTicksPerBar = 4 * 1920 = 7680
  EXPECT_EQ(bass.getLengthTicks(), 4u * kTicksPerBar);
  EXPECT_EQ(bass.getLengthTicks(), 7680u);
}

TEST(GroundBassTest, StandardDMinorBar1IsWholeNote) {
  auto bass = GroundBass::createStandardDMinor();
  const auto& notes = bass.getNotes();
  ASSERT_GE(notes.size(), 1u);
  // Bar 1: D3 whole note starting at tick 0
  EXPECT_EQ(notes[0].start_tick, 0u);
  EXPECT_EQ(notes[0].duration, kTicksPerBar);
  EXPECT_EQ(notes[0].pitch, 50u);
}

TEST(GroundBassTest, StandardDMinorBar2HasTwoHalfNotes) {
  auto bass = GroundBass::createStandardDMinor();
  const auto& notes = bass.getNotes();
  ASSERT_GE(notes.size(), 3u);

  Tick half_note = kTicksPerBar / 2;  // 960

  // Bar 2: C#3 (49) half, D3 (50) half
  EXPECT_EQ(notes[1].start_tick, kTicksPerBar);
  EXPECT_EQ(notes[1].duration, half_note);
  EXPECT_EQ(notes[1].pitch, 49u);

  EXPECT_EQ(notes[2].start_tick, kTicksPerBar + half_note);
  EXPECT_EQ(notes[2].duration, half_note);
  EXPECT_EQ(notes[2].pitch, 50u);
}

TEST(GroundBassTest, StandardDMinorBar3HasTwoHalfNotes) {
  auto bass = GroundBass::createStandardDMinor();
  const auto& notes = bass.getNotes();
  ASSERT_GE(notes.size(), 5u);

  Tick half_note = kTicksPerBar / 2;

  // Bar 3: Bb2 (46) half, G2 (43) half
  EXPECT_EQ(notes[3].start_tick, 2u * kTicksPerBar);
  EXPECT_EQ(notes[3].duration, half_note);
  EXPECT_EQ(notes[3].pitch, 46u);

  EXPECT_EQ(notes[4].start_tick, 2u * kTicksPerBar + half_note);
  EXPECT_EQ(notes[4].duration, half_note);
  EXPECT_EQ(notes[4].pitch, 43u);
}

TEST(GroundBassTest, StandardDMinorBar4HasTwoHalfNotes) {
  auto bass = GroundBass::createStandardDMinor();
  const auto& notes = bass.getNotes();
  ASSERT_GE(notes.size(), 7u);

  Tick half_note = kTicksPerBar / 2;

  // Bar 4: A2 (45) half, D3 (50) half
  EXPECT_EQ(notes[5].start_tick, 3u * kTicksPerBar);
  EXPECT_EQ(notes[5].duration, half_note);
  EXPECT_EQ(notes[5].pitch, 45u);

  EXPECT_EQ(notes[6].start_tick, 3u * kTicksPerBar + half_note);
  EXPECT_EQ(notes[6].duration, half_note);
  EXPECT_EQ(notes[6].pitch, 50u);
}

// ===========================================================================
// noteCount and isEmpty
// ===========================================================================

TEST(GroundBassTest, DefaultConstructedIsEmpty) {
  GroundBass bass;
  EXPECT_TRUE(bass.isEmpty());
  EXPECT_EQ(bass.noteCount(), 0u);
  EXPECT_EQ(bass.getLengthTicks(), 0u);
}

TEST(GroundBassTest, NonEmptyBassIsNotEmpty) {
  auto bass = GroundBass::createStandardDMinor();
  EXPECT_FALSE(bass.isEmpty());
}

// ===========================================================================
// getBassAt
// ===========================================================================

TEST(GroundBassTest, GetBassAtReturnsCorrectNoteAtStart) {
  auto bass = GroundBass::createStandardDMinor();
  // Tick 0 falls in bar 1: D3 whole note
  NoteEvent note = bass.getBassAt(0);
  EXPECT_EQ(note.pitch, 50u);
}

TEST(GroundBassTest, GetBassAtReturnsCorrectNoteInMiddleOfBar1) {
  auto bass = GroundBass::createStandardDMinor();
  // Tick 480 (beat 2) still within bar 1's whole note
  NoteEvent note = bass.getBassAt(480);
  EXPECT_EQ(note.pitch, 50u);
}

TEST(GroundBassTest, GetBassAtReturnsCorrectNoteInBar2) {
  auto bass = GroundBass::createStandardDMinor();
  // Tick 1920 = start of bar 2 -> C#3 (49) half note
  NoteEvent note = bass.getBassAt(kTicksPerBar);
  EXPECT_EQ(note.pitch, 49u);
}

TEST(GroundBassTest, GetBassAtReturnsSecondHalfOfBar2) {
  auto bass = GroundBass::createStandardDMinor();
  // Tick 2880 (bar 2, beat 3) = second half of bar 2 -> D3 (50)
  NoteEvent note = bass.getBassAt(kTicksPerBar + kTicksPerBar / 2);
  EXPECT_EQ(note.pitch, 50u);
}

TEST(GroundBassTest, GetBassAtWrapsAroundCyclically) {
  auto bass = GroundBass::createStandardDMinor();
  Tick length = bass.getLengthTicks();
  ASSERT_GT(length, 0u);

  // Tick at exactly the length should wrap to tick 0 -> D3 (50)
  NoteEvent wrapped = bass.getBassAt(length);
  NoteEvent original = bass.getBassAt(0);
  EXPECT_EQ(wrapped.pitch, original.pitch);

  // Also test one cycle later + offset into bar 2
  NoteEvent wrapped2 = bass.getBassAt(length + kTicksPerBar);
  NoteEvent original2 = bass.getBassAt(kTicksPerBar);
  EXPECT_EQ(wrapped2.pitch, original2.pitch);
}

TEST(GroundBassTest, GetBassAtEmptyReturnsRest) {
  GroundBass empty_bass;
  NoteEvent note = empty_bass.getBassAt(0);
  EXPECT_EQ(note.pitch, 0u);
  EXPECT_EQ(note.velocity, 0u);
}

// ===========================================================================
// verifyIntegrity
// ===========================================================================

TEST(GroundBassTest, VerifyIntegrityPassesWithExactCopy) {
  auto bass = GroundBass::createStandardDMinor();
  // Make an exact copy of the notes
  std::vector<NoteEvent> copy = bass.getNotes();
  EXPECT_TRUE(bass.verifyIntegrity(copy));
}

TEST(GroundBassTest, VerifyIntegrityFailsWithPitchModification) {
  auto bass = GroundBass::createStandardDMinor();
  std::vector<NoteEvent> modified = bass.getNotes();
  ASSERT_FALSE(modified.empty());
  modified[0].pitch += 1;  // Corrupt first note's pitch
  EXPECT_FALSE(bass.verifyIntegrity(modified));
}

TEST(GroundBassTest, VerifyIntegrityFailsWithTimingModification) {
  auto bass = GroundBass::createStandardDMinor();
  std::vector<NoteEvent> modified = bass.getNotes();
  ASSERT_FALSE(modified.empty());
  modified[0].start_tick += 1;  // Shift first note's timing
  EXPECT_FALSE(bass.verifyIntegrity(modified));
}

TEST(GroundBassTest, VerifyIntegrityFailsWithDurationModification) {
  auto bass = GroundBass::createStandardDMinor();
  std::vector<NoteEvent> modified = bass.getNotes();
  ASSERT_FALSE(modified.empty());
  modified[0].duration += 1;  // Change first note's duration
  EXPECT_FALSE(bass.verifyIntegrity(modified));
}

TEST(GroundBassTest, VerifyIntegrityFailsWithAddedNote) {
  auto bass = GroundBass::createStandardDMinor();
  std::vector<NoteEvent> modified = bass.getNotes();
  // Add an extra note
  NoteEvent extra{0, 480, 60, 80, 0};
  modified.push_back(extra);
  EXPECT_FALSE(bass.verifyIntegrity(modified));
}

TEST(GroundBassTest, VerifyIntegrityFailsWithRemovedNote) {
  auto bass = GroundBass::createStandardDMinor();
  std::vector<NoteEvent> modified = bass.getNotes();
  ASSERT_GT(modified.size(), 1u);
  modified.pop_back();  // Remove the last note
  EXPECT_FALSE(bass.verifyIntegrity(modified));
}

TEST(GroundBassTest, VerifyIntegrityIgnoresVelocityChanges) {
  // verifyIntegrity checks pitch, start_tick, duration -- but NOT velocity.
  auto bass = GroundBass::createStandardDMinor();
  std::vector<NoteEvent> modified = bass.getNotes();
  ASSERT_FALSE(modified.empty());
  modified[0].velocity = 127;  // Change velocity (should not affect integrity)
  // Integrity checks pitch, start_tick, duration only.
  EXPECT_TRUE(bass.verifyIntegrity(modified));
}

// ===========================================================================
// createForKey transposition
// ===========================================================================

TEST(GroundBassTest, CreateForKeyDMinorMatchesStandard) {
  KeySignature d_minor = {Key::D, true};
  auto from_key = GroundBass::createForKey(d_minor);
  auto standard = GroundBass::createStandardDMinor();

  EXPECT_EQ(from_key.noteCount(), standard.noteCount());
  EXPECT_EQ(from_key.getLengthTicks(), standard.getLengthTicks());

  // Notes should match exactly.
  EXPECT_TRUE(standard.verifyIntegrity(from_key.getNotes()));
}

TEST(GroundBassTest, CreateForKeyCMinorTransposesCorrectly) {
  KeySignature c_minor = {Key::C, true};
  auto bass = GroundBass::createForKey(c_minor);

  // D minor tonic at octave 3 = 50, C tonic at octave 3 = 48.
  // Transpose interval = 48 - 50 = -2 semitones.
  // First note (D3 = 50) -> C3 = 48
  const auto& notes = bass.getNotes();
  ASSERT_FALSE(notes.empty());
  EXPECT_EQ(notes[0].pitch, 48u);

  // Same number of notes and same timing structure.
  EXPECT_EQ(bass.noteCount(), 7u);
  EXPECT_EQ(bass.getLengthTicks(), 4u * kTicksPerBar);
}

TEST(GroundBassTest, CreateForKeyGMinorTransposesCorrectly) {
  KeySignature g_minor = {Key::G, true};
  auto bass = GroundBass::createForKey(g_minor);

  // G at octave 3 = (3+1)*12 + 7 = 55. D at octave 3 = 50.
  // Transpose = 55 - 50 = +5 semitones.
  // First note: 50 + 5 = 55
  const auto& notes = bass.getNotes();
  ASSERT_FALSE(notes.empty());
  EXPECT_EQ(notes[0].pitch, 55u);

  EXPECT_EQ(bass.noteCount(), 7u);
  EXPECT_EQ(bass.getLengthTicks(), 4u * kTicksPerBar);
}

TEST(GroundBassTest, CreateForKeyAMinorTransposesCorrectly) {
  KeySignature a_minor = {Key::A, true};
  auto bass = GroundBass::createForKey(a_minor);

  // A at octave 3 = (3+1)*12 + 9 = 57. D at octave 3 = 50.
  // Transpose = 57 - 50 = +7 semitones.
  // First note: 50 + 7 = 57
  const auto& notes = bass.getNotes();
  ASSERT_FALSE(notes.empty());
  EXPECT_EQ(notes[0].pitch, 57u);
}

TEST(GroundBassTest, CreateForKeyEMinorTransposesCorrectly) {
  KeySignature e_minor = {Key::E, true};
  auto bass = GroundBass::createForKey(e_minor);

  // E at octave 3 = (3+1)*12 + 4 = 52. D at octave 3 = 50.
  // Transpose = 52 - 50 = +2 semitones.
  // First note: 50 + 2 = 52
  const auto& notes = bass.getNotes();
  ASSERT_FALSE(notes.empty());
  EXPECT_EQ(notes[0].pitch, 52u);
}

TEST(GroundBassTest, CreateForKeyPreservesTimingStructure) {
  // All transpositions must preserve the rhythmic structure.
  auto standard = GroundBass::createStandardDMinor();
  KeySignature g_minor = {Key::G, true};
  auto transposed = GroundBass::createForKey(g_minor);

  ASSERT_EQ(transposed.noteCount(), standard.noteCount());

  const auto& std_notes = standard.getNotes();
  const auto& trn_notes = transposed.getNotes();

  for (size_t idx = 0; idx < std_notes.size(); ++idx) {
    EXPECT_EQ(trn_notes[idx].start_tick, std_notes[idx].start_tick)
        << "Timing mismatch at note " << idx;
    EXPECT_EQ(trn_notes[idx].duration, std_notes[idx].duration)
        << "Duration mismatch at note " << idx;
  }
}

TEST(GroundBassTest, CreateForKeyPreservesIntervalStructure) {
  // The interval relationships between notes should be identical.
  auto standard = GroundBass::createStandardDMinor();
  KeySignature c_minor = {Key::C, true};
  auto transposed = GroundBass::createForKey(c_minor);

  const auto& std_notes = standard.getNotes();
  const auto& trn_notes = transposed.getNotes();
  ASSERT_EQ(std_notes.size(), trn_notes.size());

  for (size_t idx = 1; idx < std_notes.size(); ++idx) {
    int std_interval = static_cast<int>(std_notes[idx].pitch) -
                       static_cast<int>(std_notes[idx - 1].pitch);
    int trn_interval = static_cast<int>(trn_notes[idx].pitch) -
                       static_cast<int>(trn_notes[idx - 1].pitch);
    EXPECT_EQ(std_interval, trn_interval)
        << "Interval mismatch between notes " << (idx - 1) << " and " << idx;
  }
}

// ===========================================================================
// Custom construction
// ===========================================================================

TEST(GroundBassTest, CustomNotesAreStoredCorrectly) {
  std::vector<NoteEvent> custom_notes = {
      {0, 1920, 60, 80, 0},
      {1920, 1920, 62, 80, 0},
  };
  GroundBass bass(custom_notes);

  EXPECT_EQ(bass.noteCount(), 2u);
  EXPECT_EQ(bass.getLengthTicks(), 3840u);
  EXPECT_FALSE(bass.isEmpty());
}


// ===========================================================================
// verifyIntegrityReport
// ===========================================================================

TEST(GroundBassTest, IntegrityReportExactCopyHasNoIssues) {
  auto bass = GroundBass::createStandardDMinor();
  auto copy = bass.getNotes();
  auto report = bass.verifyIntegrityReport(copy);
  EXPECT_FALSE(report.hasCritical());
  EXPECT_TRUE(report.issues.empty());
}

TEST(GroundBassTest, IntegrityReportNoteCountMismatch) {
  auto bass = GroundBass::createStandardDMinor();
  auto modified = bass.getNotes();
  modified.pop_back();
  auto report = bass.verifyIntegrityReport(modified);
  EXPECT_TRUE(report.hasCritical());
  ASSERT_GE(report.issues.size(), 1u);
  EXPECT_EQ(report.issues[0].rule, "note_count_mismatch");
  EXPECT_EQ(report.issues[0].kind, FailKind::StructuralFail);
}

TEST(GroundBassTest, IntegrityReportPitchMismatch) {
  auto bass = GroundBass::createStandardDMinor();
  auto modified = bass.getNotes();
  modified[0].pitch += 1;
  auto report = bass.verifyIntegrityReport(modified);
  EXPECT_TRUE(report.hasCritical());
  bool found = false;
  for (const auto& issue : report.issues) {
    if (issue.rule == "pitch_mismatch") { found = true; break; }
  }
  EXPECT_TRUE(found);
}

TEST(GroundBassTest, IntegrityReportTimingMismatch) {
  auto bass = GroundBass::createStandardDMinor();
  auto modified = bass.getNotes();
  modified[0].start_tick += 1;
  auto report = bass.verifyIntegrityReport(modified);
  EXPECT_TRUE(report.hasCritical());
  bool found = false;
  for (const auto& issue : report.issues) {
    if (issue.rule == "timing_mismatch") { found = true; break; }
  }
  EXPECT_TRUE(found);
}

TEST(GroundBassTest, IntegrityReportDurationMismatch) {
  auto bass = GroundBass::createStandardDMinor();
  auto modified = bass.getNotes();
  modified[0].duration += 1;
  auto report = bass.verifyIntegrityReport(modified);
  EXPECT_TRUE(report.hasCritical());
  bool found = false;
  for (const auto& issue : report.issues) {
    if (issue.rule == "duration_mismatch") { found = true; break; }
  }
  EXPECT_TRUE(found);
}

// ===========================================================================
// createForKey with register_low (instrument-aware octave shift)
// ===========================================================================

TEST(GroundBassTest, CreateForKeyRespectsRegisterLow) {
  // D minor on violin (BWV1004-style): register_low = 55 (G3).
  auto bass = GroundBass::createForKey({Key::D, true}, 55);
  for (const auto& note : bass.getNotes()) {
    EXPECT_GE(note.pitch, 55u) << "Bass note below violin range";
  }
  // D minor standard bass lowest is G2(43). Shift = ceil((55-43)/12)*12 = 12.
  // G2(43)+12 = G3(55). Lowest should be exactly G3(55).
  uint8_t lowest = 127;
  for (const auto& note : bass.getNotes()) {
    if (note.pitch < lowest) lowest = note.pitch;
  }
  EXPECT_EQ(lowest, 55u);
}

TEST(GroundBassTest, CreateForKeyNoShiftWhenInRange) {
  // D minor on cello (register_low = 36): all notes already >= 36.
  auto bass_default = GroundBass::createForKey({Key::D, true});
  auto bass_cello = GroundBass::createForKey({Key::D, true}, 36);
  // Both should produce identical pitches (all notes already >= 36).
  ASSERT_EQ(bass_default.noteCount(), bass_cello.noteCount());
  for (size_t i = 0; i < bass_default.noteCount(); ++i) {
    EXPECT_EQ(bass_default.getNotes()[i].pitch, bass_cello.getNotes()[i].pitch);
  }
}

TEST(GroundBassTest, CreateForKeyDefaultBackwardCompatible) {
  // register_low=0 (default) should produce same result as before.
  auto bass_old = GroundBass::createForKey({Key::D, true});
  auto bass_new = GroundBass::createForKey({Key::D, true}, 0);
  ASSERT_EQ(bass_old.noteCount(), bass_new.noteCount());
  for (size_t i = 0; i < bass_old.noteCount(); ++i) {
    EXPECT_EQ(bass_old.getNotes()[i].pitch, bass_new.getNotes()[i].pitch);
  }
}

TEST(GroundBassTest, CreateForKeyPreservesIntervalsAfterShift) {
  // Interval structure should be preserved even after octave shift.
  auto bass_no_shift = GroundBass::createForKey({Key::D, true});
  auto bass_shifted = GroundBass::createForKey({Key::D, true}, 55);
  ASSERT_EQ(bass_no_shift.noteCount(), bass_shifted.noteCount());
  for (size_t i = 1; i < bass_no_shift.noteCount(); ++i) {
    int interval_orig = static_cast<int>(bass_no_shift.getNotes()[i].pitch) -
                        static_cast<int>(bass_no_shift.getNotes()[i - 1].pitch);
    int interval_shifted = static_cast<int>(bass_shifted.getNotes()[i].pitch) -
                           static_cast<int>(bass_shifted.getNotes()[i - 1].pitch);
    EXPECT_EQ(interval_orig, interval_shifted)
        << "Interval mismatch at note " << i;
  }
}

TEST(GroundBassTest, CreateForKeyCMajorOnViolinShiftsUp) {
  // C major bass lowest = F2(41). Violin register_low=55.
  // Shift = ceil((55-41)/12)*12 = ceil(14/12)*12 = 24.
  auto bass = GroundBass::createForKey({Key::C, false}, 55);
  for (const auto& note : bass.getNotes()) {
    EXPECT_GE(note.pitch, 55u) << "Bass note below violin range";
  }
}

}  // namespace
}  // namespace bach
