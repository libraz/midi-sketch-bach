// Tests for counterpoint/repeated_note_repair.h -- repeated pitch run
// detection and scale-aware replacement.

#include "counterpoint/repeated_note_repair.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <vector>

#include "core/basic_types.h"
#include "core/note_source.h"
#include "core/scale.h"
#include "harmony/key.h"

namespace bach {
namespace {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

/// @brief Build default params with fixed C major key, no voice range or
/// vertical safety constraints.
RepeatedNoteRepairParams makeDefaultParams(uint8_t num_voices) {
  RepeatedNoteRepairParams params;
  params.num_voices = num_voices;
  params.key_at_tick = [](Tick) { return Key::C; };
  params.scale_at_tick = [](Tick) { return ScaleType::Major; };
  return params;
}

/// @brief Create a note with the given tick, pitch, voice, and source.
NoteEvent makeNote(Tick tick, uint8_t pitch, uint8_t voice,
                   BachNoteSource source = BachNoteSource::FreeCounterpoint) {
  NoteEvent n;
  n.start_tick = tick;
  n.duration = kTicksPerBeat;
  n.pitch = pitch;
  n.voice = voice;
  n.source = source;
  return n;
}

/// @brief Create a run of consecutive notes at quarter-note spacing with the
/// same pitch in a single voice.
std::vector<NoteEvent> makeRun(int count, uint8_t pitch, uint8_t voice,
                               Tick start_tick = 0,
                               BachNoteSource source = BachNoteSource::FreeCounterpoint) {
  std::vector<NoteEvent> notes;
  notes.reserve(static_cast<size_t>(count));
  for (int i = 0; i < count; ++i) {
    notes.push_back(
        makeNote(start_tick + static_cast<Tick>(i) * kTicksPerBeat,
                 pitch, voice, source));
  }
  return notes;
}

/// @brief Check if the RepeatedNoteRep flag is set on a note.
bool hasRepairFlag(const NoteEvent& note) {
  return (note.modified_by &
          static_cast<uint8_t>(NoteModifiedBy::RepeatedNoteRep)) != 0;
}

// ---------------------------------------------------------------------------
// Test: RunBelowThreshold_NoChange
// ---------------------------------------------------------------------------

TEST(RepeatedNoteRepairTest, RunBelowThreshold_NoChange) {
  // 3 consecutive same-pitch notes with max_consecutive=3 should NOT trigger
  // repair.
  auto notes = makeRun(3, 60, 0);
  auto params = makeDefaultParams(1);
  params.max_consecutive = 3;

  int modified = repairRepeatedNotes(notes, params);

  EXPECT_EQ(modified, 0);
  for (const auto& n : notes) {
    EXPECT_EQ(n.pitch, 60);
    EXPECT_FALSE(hasRepairFlag(n));
  }
}

// ---------------------------------------------------------------------------
// Test: RunAboveThreshold_RepairsExcess
// ---------------------------------------------------------------------------

TEST(RepeatedNoteRepairTest, RunAboveThreshold_RepairsExcess) {
  // 6 consecutive C4 notes, max_consecutive=3 -> notes 3-5 repaired.
  auto notes = makeRun(6, 60, 0);
  auto params = makeDefaultParams(1);
  params.max_consecutive = 3;

  int modified = repairRepeatedNotes(notes, params);

  EXPECT_EQ(modified, 3);

  // First 3 notes unchanged.
  for (int i = 0; i < 3; ++i) {
    EXPECT_EQ(notes[static_cast<size_t>(i)].pitch, 60)
        << "Note " << i << " should be unchanged";
    EXPECT_FALSE(hasRepairFlag(notes[static_cast<size_t>(i)]));
  }

  // Last 3 notes modified: different pitch and flag set.
  for (int i = 3; i < 6; ++i) {
    EXPECT_NE(notes[static_cast<size_t>(i)].pitch, 60)
        << "Note " << i << " should have been repaired";
    EXPECT_TRUE(hasRepairFlag(notes[static_cast<size_t>(i)]))
        << "Note " << i << " missing RepeatedNoteRep flag";
  }
}

// ---------------------------------------------------------------------------
// Test: StructuralSourceProtected
// ---------------------------------------------------------------------------

TEST(RepeatedNoteRepairTest, StructuralSourceProtected) {
  // 6 notes, same pitch. Notes 3-5 are structural sources and must not be
  // modified.
  auto notes = makeRun(6, 60, 0);
  notes[3].source = BachNoteSource::FugueSubject;
  notes[4].source = BachNoteSource::Coda;
  notes[5].source = BachNoteSource::SequenceNote;

  auto params = makeDefaultParams(1);
  params.max_consecutive = 3;

  int modified = repairRepeatedNotes(notes, params);

  // All three excess notes are protected, so nothing should change.
  EXPECT_EQ(modified, 0);
  for (const auto& n : notes) {
    EXPECT_EQ(n.pitch, 60);
    EXPECT_FALSE(hasRepairFlag(n));
  }
}

// ---------------------------------------------------------------------------
// Test: GroundBassProtected
// ---------------------------------------------------------------------------

TEST(RepeatedNoteRepairTest, GroundBassProtected) {
  // 5 notes same pitch. Notes 3-4 are GroundBass and PedalPoint (both
  // protected).
  auto notes = makeRun(5, 60, 0);
  notes[3].source = BachNoteSource::GroundBass;
  notes[4].source = BachNoteSource::PedalPoint;

  auto params = makeDefaultParams(1);
  params.max_consecutive = 3;

  int modified = repairRepeatedNotes(notes, params);

  // GroundBass is protected via isProtectedSource(). PedalPoint is protected
  // via isStructuralSource().
  EXPECT_EQ(modified, 0);
  EXPECT_EQ(notes[3].pitch, 60);
  EXPECT_EQ(notes[4].pitch, 60);
}

// ---------------------------------------------------------------------------
// Test: MultiVoiceIndependent
// ---------------------------------------------------------------------------

TEST(RepeatedNoteRepairTest, MultiVoiceIndependent) {
  // Voice 0: 5 notes of pitch 60; Voice 1: 5 notes of pitch 67.
  // Both have max_consecutive=3 -> 2 excess notes per voice = 4 repairs.
  std::vector<NoteEvent> notes;
  auto v0 = makeRun(5, 60, 0);
  auto v1 = makeRun(5, 67, 1);
  notes.insert(notes.end(), v0.begin(), v0.end());
  notes.insert(notes.end(), v1.begin(), v1.end());

  auto params = makeDefaultParams(2);
  params.max_consecutive = 3;

  int modified = repairRepeatedNotes(notes, params);

  EXPECT_EQ(modified, 4);

  // Check voice 0: first 3 unchanged, last 2 modified.
  for (int i = 0; i < 3; ++i) {
    EXPECT_EQ(notes[static_cast<size_t>(i)].pitch, 60);
  }
  for (int i = 3; i < 5; ++i) {
    EXPECT_NE(notes[static_cast<size_t>(i)].pitch, 60);
    EXPECT_TRUE(hasRepairFlag(notes[static_cast<size_t>(i)]));
  }

  // Check voice 1: first 3 unchanged, last 2 modified.
  for (int i = 5; i < 8; ++i) {
    EXPECT_EQ(notes[static_cast<size_t>(i)].pitch, 67);
  }
  for (int i = 8; i < 10; ++i) {
    EXPECT_NE(notes[static_cast<size_t>(i)].pitch, 67);
    EXPECT_TRUE(hasRepairFlag(notes[static_cast<size_t>(i)]));
  }
}

// ---------------------------------------------------------------------------
// Test: ScaleDegreeReplacement
// ---------------------------------------------------------------------------

TEST(RepeatedNoteRepairTest, ScaleDegreeReplacement) {
  // 5 notes of C4(60) in C major. Verify that replacements are C major scale
  // tones within +/-3 semitones.
  auto notes = makeRun(5, 60, 0);
  auto params = makeDefaultParams(1);
  params.max_consecutive = 3;

  repairRepeatedNotes(notes, params);

  for (int i = 3; i < 5; ++i) {
    uint8_t p = notes[static_cast<size_t>(i)].pitch;

    // Must be a C major scale tone.
    EXPECT_TRUE(scale_util::isScaleTone(p, Key::C, ScaleType::Major))
        << "Replacement pitch " << static_cast<int>(p)
        << " is not a C major scale tone";

    // Must be within 3 semitones of base pitch 60.
    int diff = std::abs(static_cast<int>(p) - 60);
    EXPECT_LE(diff, 3)
        << "Replacement pitch " << static_cast<int>(p)
        << " is " << diff << " semitones from base (max 3)";
  }
}

// ---------------------------------------------------------------------------
// Test: BarHeadChordToneTolerance (placeholder)
// ---------------------------------------------------------------------------

TEST(RepeatedNoteRepairTest, BarHeadChordToneTolerance_Placeholder) {
  // TODO: Test bar-head chord-tone tolerance once effective_max enhancement
  // is implemented. For now, verify basic repair does not crash when notes
  // fall on bar boundaries.
  auto notes = makeRun(5, 60, 0);
  // Place notes on bar boundaries.
  for (size_t i = 0; i < notes.size(); ++i) {
    notes[i].start_tick = static_cast<Tick>(i) * kTicksPerBar;
  }

  auto params = makeDefaultParams(1);
  params.max_consecutive = 3;

  int modified = repairRepeatedNotes(notes, params);

  // Should repair 2 notes (index 3 and 4) without crashing.
  EXPECT_EQ(modified, 2);
}

// ---------------------------------------------------------------------------
// Test: GapThresholdBreaksRun
// ---------------------------------------------------------------------------

TEST(RepeatedNoteRepairTest, GapThresholdBreaksRun) {
  // 6 notes of same pitch with a large gap between notes 2 and 3.
  // This splits into two runs of 3 each, both at the threshold -> no repair.
  std::vector<NoteEvent> notes;
  // First run: ticks 0, 480, 960
  for (int i = 0; i < 3; ++i) {
    notes.push_back(makeNote(static_cast<Tick>(i) * kTicksPerBeat, 60, 0));
  }
  // Second run: starts well beyond the gap threshold (2 * kTicksPerBar = 3840).
  Tick second_start = 3 * kTicksPerBar;  // 5760, gap from tick 960 = 4800 > 3840
  for (int i = 0; i < 3; ++i) {
    notes.push_back(makeNote(second_start + static_cast<Tick>(i) * kTicksPerBeat, 60, 0));
  }

  auto params = makeDefaultParams(1);
  params.max_consecutive = 3;

  int modified = repairRepeatedNotes(notes, params);

  EXPECT_EQ(modified, 0);
  for (const auto& n : notes) {
    EXPECT_EQ(n.pitch, 60);
  }
}

// ---------------------------------------------------------------------------
// Test: DirectionAlternation
// ---------------------------------------------------------------------------

TEST(RepeatedNoteRepairTest, DirectionAlternation) {
  // Approach from below (D4=62 -> C4=60... repeated). Excess notes should
  // alternate direction relative to approach.
  std::vector<NoteEvent> notes;
  // Approach note: pitch 58 (Bb3) in voice 0, before the run.
  notes.push_back(makeNote(0, 58, 0));
  // Run of 8 C4 notes.
  for (int i = 0; i < 8; ++i) {
    notes.push_back(
        makeNote(kTicksPerBeat + static_cast<Tick>(i) * kTicksPerBeat, 60, 0));
  }

  auto params = makeDefaultParams(1);
  params.max_consecutive = 3;

  repairRepeatedNotes(notes, params);

  // Collect directions of excess notes (indices 4..8 in our vector, which are
  // run positions 3..7).
  std::vector<int> directions;
  for (size_t i = 4; i <= 8; ++i) {
    if (notes[i].pitch != 60) {
      int dir = (notes[i].pitch > 60) ? 1 : -1;
      directions.push_back(dir);
    }
  }

  // At least some notes should go up AND some down (alternation).
  if (directions.size() >= 2) {
    bool has_up = std::any_of(directions.begin(), directions.end(),
                              [](int d) { return d > 0; });
    bool has_down = std::any_of(directions.begin(), directions.end(),
                                [](int d) { return d < 0; });
    EXPECT_TRUE(has_up && has_down)
        << "Expected direction alternation among repaired notes";
  }
}

// ---------------------------------------------------------------------------
// Test: VerticalSafetyCheck
// ---------------------------------------------------------------------------

TEST(RepeatedNoteRepairTest, VerticalSafetyCheck) {
  // Provide a vertical_safe callback that rejects pitches 59 and 62 (the
  // two closest C major scale tones to 60). The repair should fall back to
  // other candidates (57=A3 or 64=E4).
  auto notes = makeRun(5, 60, 0);
  auto params = makeDefaultParams(1);
  params.max_consecutive = 3;
  params.vertical_safe = [](Tick, uint8_t, uint8_t pitch) -> bool {
    // Reject B3(59) and D4(62).
    return pitch != 59 && pitch != 62;
  };

  int modified = repairRepeatedNotes(notes, params);

  // Should still find valid replacements (e.g., 57=A3 or 64=E4).
  EXPECT_GT(modified, 0);
  for (int i = 3; i < 5; ++i) {
    uint8_t p = notes[static_cast<size_t>(i)].pitch;
    if (hasRepairFlag(notes[static_cast<size_t>(i)])) {
      EXPECT_NE(p, 59) << "Should not use rejected pitch B3";
      EXPECT_NE(p, 62) << "Should not use rejected pitch D4";
      EXPECT_NE(p, 60) << "Should differ from base pitch";
      EXPECT_TRUE(scale_util::isScaleTone(p, Key::C, ScaleType::Major));
    }
  }
}

// ---------------------------------------------------------------------------
// Test: EmptyNotes_NoChange
// ---------------------------------------------------------------------------

TEST(RepeatedNoteRepairTest, EmptyNotes_NoChange) {
  std::vector<NoteEvent> notes;
  auto params = makeDefaultParams(1);

  int modified = repairRepeatedNotes(notes, params);

  EXPECT_EQ(modified, 0);
  EXPECT_TRUE(notes.empty());
}

// ---------------------------------------------------------------------------
// Test: ZeroVoices_NoChange
// ---------------------------------------------------------------------------

TEST(RepeatedNoteRepairTest, ZeroVoices_NoChange) {
  auto notes = makeRun(5, 60, 0);
  auto params = makeDefaultParams(0);  // num_voices=0

  int modified = repairRepeatedNotes(notes, params);

  EXPECT_EQ(modified, 0);
}

// ---------------------------------------------------------------------------
// Test: SingleNote_NoChange
// ---------------------------------------------------------------------------

TEST(RepeatedNoteRepairTest, SingleNote_NoChange) {
  std::vector<NoteEvent> notes = {makeNote(0, 60, 0)};
  auto params = makeDefaultParams(1);

  int modified = repairRepeatedNotes(notes, params);

  EXPECT_EQ(modified, 0);
  EXPECT_EQ(notes[0].pitch, 60);
}

// ---------------------------------------------------------------------------
// Test: MaxConsecutiveOne_RepairsSecondNote
// ---------------------------------------------------------------------------

TEST(RepeatedNoteRepairTest, MaxConsecutiveOne_RepairsSecondNote) {
  // With max_consecutive=1, even the 2nd repeated note is repaired.
  auto notes = makeRun(3, 60, 0);
  auto params = makeDefaultParams(1);
  params.max_consecutive = 1;

  int modified = repairRepeatedNotes(notes, params);

  EXPECT_EQ(modified, 2);
  EXPECT_EQ(notes[0].pitch, 60);
  EXPECT_NE(notes[1].pitch, 60);
  EXPECT_NE(notes[2].pitch, 60);
}

// ---------------------------------------------------------------------------
// Test: VoiceRangeConstraint
// ---------------------------------------------------------------------------

TEST(RepeatedNoteRepairTest, VoiceRangeConstraint) {
  // Voice range [58, 63]: only accepts pitches 58-63. With base pitch 60,
  // replacements must stay in [57,63] (from +/-3) intersected with [58,63].
  auto notes = makeRun(5, 60, 0);
  auto params = makeDefaultParams(1);
  params.max_consecutive = 3;
  params.voice_range = [](uint8_t) -> std::pair<uint8_t, uint8_t> {
    return {58, 63};
  };

  int modified = repairRepeatedNotes(notes, params);

  EXPECT_GT(modified, 0);
  for (int i = 3; i < 5; ++i) {
    uint8_t p = notes[static_cast<size_t>(i)].pitch;
    if (hasRepairFlag(notes[static_cast<size_t>(i)])) {
      EXPECT_GE(p, 58) << "Pitch below voice range lower bound";
      EXPECT_LE(p, 63) << "Pitch above voice range upper bound";
    }
  }
}

// ---------------------------------------------------------------------------
// Test: MixedProtectedAndFlexible
// ---------------------------------------------------------------------------

TEST(RepeatedNoteRepairTest, MixedProtectedAndFlexible) {
  // 7 notes same pitch. Notes 3=FugueSubject (protected), 4-6=Free.
  // Only notes 4, 5, 6 should be modified.
  auto notes = makeRun(7, 60, 0);
  notes[3].source = BachNoteSource::FugueSubject;

  auto params = makeDefaultParams(1);
  params.max_consecutive = 3;

  int modified = repairRepeatedNotes(notes, params);

  EXPECT_EQ(modified, 3);
  EXPECT_EQ(notes[3].pitch, 60);  // Protected, unchanged.
  EXPECT_FALSE(hasRepairFlag(notes[3]));

  for (int i = 4; i < 7; ++i) {
    EXPECT_NE(notes[static_cast<size_t>(i)].pitch, 60)
        << "Note " << i << " should be repaired";
    EXPECT_TRUE(hasRepairFlag(notes[static_cast<size_t>(i)]));
  }
}

// ---------------------------------------------------------------------------
// Test: ExactlyAtThreshold_NoRepair
// ---------------------------------------------------------------------------

TEST(RepeatedNoteRepairTest, ExactlyAtThreshold_NoRepair) {
  // max_consecutive=5, run length=5 -> should NOT repair (length <= threshold).
  auto notes = makeRun(5, 72, 0);
  auto params = makeDefaultParams(1);
  params.max_consecutive = 5;

  int modified = repairRepeatedNotes(notes, params);

  EXPECT_EQ(modified, 0);
}

// ---------------------------------------------------------------------------
// Test: DifferentPitchesNotARun
// ---------------------------------------------------------------------------

TEST(RepeatedNoteRepairTest, DifferentPitchesNotARun) {
  // All notes have different pitches -> no run detected, no repair.
  std::vector<NoteEvent> notes;
  uint8_t pitches[] = {60, 62, 64, 65, 67, 69};
  for (int i = 0; i < 6; ++i) {
    notes.push_back(makeNote(static_cast<Tick>(i) * kTicksPerBeat,
                             pitches[i], 0));
  }

  auto params = makeDefaultParams(1);
  params.max_consecutive = 3;

  int modified = repairRepeatedNotes(notes, params);

  EXPECT_EQ(modified, 0);
}

// ---------------------------------------------------------------------------
// Test: UnsortedNotes_StillWorks
// ---------------------------------------------------------------------------

TEST(RepeatedNoteRepairTest, UnsortedNotes_StillWorks) {
  // Notes inserted out of tick order. The implementation sorts per-voice
  // indices by start_tick, so it should still detect the run.
  std::vector<NoteEvent> notes;
  notes.push_back(makeNote(4 * kTicksPerBeat, 60, 0));  // index 0: tick 1920
  notes.push_back(makeNote(0, 60, 0));                   // index 1: tick 0
  notes.push_back(makeNote(2 * kTicksPerBeat, 60, 0));   // index 2: tick 960
  notes.push_back(makeNote(3 * kTicksPerBeat, 60, 0));   // index 3: tick 1440
  notes.push_back(makeNote(kTicksPerBeat, 60, 0));       // index 4: tick 480

  auto params = makeDefaultParams(1);
  params.max_consecutive = 3;

  int modified = repairRepeatedNotes(notes, params);

  // Run of 5 same-pitch notes, max_consecutive=3 -> 2 repairs.
  EXPECT_EQ(modified, 2);
}

// ---------------------------------------------------------------------------
// Test: VoiceOutOfRange_Ignored
// ---------------------------------------------------------------------------

TEST(RepeatedNoteRepairTest, VoiceOutOfRange_Ignored) {
  // Notes in voice 3 with num_voices=2 should be completely ignored.
  auto notes = makeRun(5, 60, 3);
  auto params = makeDefaultParams(2);
  params.max_consecutive = 3;

  int modified = repairRepeatedNotes(notes, params);

  EXPECT_EQ(modified, 0);
  for (const auto& n : notes) {
    EXPECT_EQ(n.pitch, 60);
  }
}

}  // namespace
}  // namespace bach
