// Tests for counterpoint/parallel_repair.h -- parallel perfect consonance
// detection and repair (step shifts, octave shifts, adjacent repair).

#include "counterpoint/parallel_repair.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <vector>

#include "core/basic_types.h"
#include "core/interval.h"
#include "core/note_source.h"
#include "core/scale.h"

namespace bach {
namespace {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

/// @brief Build default params with fixed C major key and wide voice range.
ParallelRepairParams makeDefaultParams(uint8_t num_voices) {
  ParallelRepairParams params;
  params.num_voices = num_voices;
  params.scale = ScaleType::Major;
  params.key_at_tick = [](Tick) { return Key::C; };
  // Wide range: C1(36) to C7(96) for all voices.
  params.voice_range_static = [](uint8_t) -> std::pair<uint8_t, uint8_t> {
    return {36, 96};
  };
  params.max_iterations = 3;
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

/// @brief Check whether the ParallelRepair flag is set on a note.
bool hasRepairFlag(const NoteEvent& note) {
  return (note.modified_by &
          static_cast<uint8_t>(NoteModifiedBy::ParallelRepair)) != 0;
}

/// @brief Find the note at a given (voice, index) in chronological order.
const NoteEvent& findNote(const std::vector<NoteEvent>& notes,
                          uint8_t voice, int index) {
  std::vector<const NoteEvent*> voice_notes;
  for (const auto& n : notes) {
    if (n.voice == voice) voice_notes.push_back(&n);
  }
  std::sort(voice_notes.begin(), voice_notes.end(),
            [](const NoteEvent* a, const NoteEvent* b) {
              return a->start_tick < b->start_tick;
            });
  return *voice_notes[static_cast<size_t>(index)];
}

/// @brief Check whether two consecutive tick positions form a parallel perfect
/// consonance between two voices.
bool hasParallelPerfect(const std::vector<NoteEvent>& notes,
                        uint8_t va, uint8_t vb,
                        Tick tick_prev, Tick tick_curr) {
  int pa = -1, pb = -1, ca = -1, cb = -1;
  for (const auto& n : notes) {
    Tick end = n.start_tick + n.duration;
    if (n.voice == va && n.start_tick <= tick_prev && end > tick_prev)
      pa = n.pitch;
    if (n.voice == vb && n.start_tick <= tick_prev && end > tick_prev)
      pb = n.pitch;
    if (n.voice == va && n.start_tick <= tick_curr && end > tick_curr)
      ca = n.pitch;
    if (n.voice == vb && n.start_tick <= tick_curr && end > tick_curr)
      cb = n.pitch;
  }
  if (pa < 0 || pb < 0 || ca < 0 || cb < 0) return false;
  int pi = std::abs(pa - pb), ci = std::abs(ca - cb);
  if (!interval_util::isPerfectConsonance(pi) ||
      !interval_util::isPerfectConsonance(ci))
    return false;
  int ps = interval_util::compoundToSimple(pi);
  int cs = interval_util::compoundToSimple(ci);
  if (ps != cs) return false;
  int ma = ca - pa, mb = cb - pb;
  return (ma != 0 && mb != 0 && (ma > 0) == (mb > 0));
}

// ---------------------------------------------------------------------------
// Test: No parallels present -- notes should be unchanged.
// ---------------------------------------------------------------------------

TEST(ParallelRepair, NoParallelsUnchanged) {
  // Voice 0: C4(60), D4(62), E4(64) -- ascending by step.
  // Voice 1: E4(64), F4(65), G4(67) -- ascending by step.
  // Intervals: M3(4), m3(3), m3(3) -- no perfect consonance parallels.
  std::vector<NoteEvent> notes = {
      makeNote(0, 60, 0),   makeNote(kTicksPerBeat, 62, 0),
      makeNote(kTicksPerBeat * 2, 64, 0),
      makeNote(0, 64, 1),   makeNote(kTicksPerBeat, 65, 1),
      makeNote(kTicksPerBeat * 2, 67, 1),
  };
  auto params = makeDefaultParams(2);

  int fixed = repairParallelPerfect(notes, params);
  EXPECT_EQ(fixed, 0);
  // All pitches unchanged.
  EXPECT_EQ(findNote(notes, 0, 0).pitch, 60);
  EXPECT_EQ(findNote(notes, 0, 1).pitch, 62);
  EXPECT_EQ(findNote(notes, 0, 2).pitch, 64);
  EXPECT_EQ(findNote(notes, 1, 0).pitch, 64);
  EXPECT_EQ(findNote(notes, 1, 1).pitch, 65);
  EXPECT_EQ(findNote(notes, 1, 2).pitch, 67);
}

// ---------------------------------------------------------------------------
// Test: Direct fix on Flexible notes (parallel 5ths).
// ---------------------------------------------------------------------------

TEST(ParallelRepair, DirectFixFlexible) {
  // Voice 0: C4(60) at tick 0, D4(62) at tick 480.
  // Voice 1: G4(67) at tick 0, A4(69) at tick 480.
  // Interval at tick 0: |60-67| = 7 (P5). Interval at tick 480: |62-69| = 7 (P5).
  // Both voices move up by 2 semitones: parallel P5.
  std::vector<NoteEvent> notes = {
      makeNote(0, 60, 0), makeNote(kTicksPerBeat, 62, 0),
      makeNote(0, 67, 1), makeNote(kTicksPerBeat, 69, 1),
  };
  auto params = makeDefaultParams(2);

  int fixed = repairParallelPerfect(notes, params);
  EXPECT_GE(fixed, 1);

  // At least one note should have the repair flag.
  bool any_flagged = false;
  for (const auto& n : notes) {
    if (hasRepairFlag(n)) any_flagged = true;
  }
  EXPECT_TRUE(any_flagged);

  // The parallel P5 at tick 480 should be broken.
  EXPECT_FALSE(hasParallelPerfect(notes, 0, 1, 0, kTicksPerBeat));
}

// ---------------------------------------------------------------------------
// Test: Direct fix on Structural notes uses octave shifts only.
// ---------------------------------------------------------------------------

TEST(ParallelRepair, DirectFixStructural) {
  // FugueAnswer -> Structural: only +-12 shifts allowed.
  // Voice 0: C4(60), D4(62) -- FugueAnswer (Structural).
  // Voice 1: G4(67), A4(69) -- FugueAnswer (Structural).
  // Parallel P5 at tick 480.
  std::vector<NoteEvent> notes = {
      makeNote(0, 60, 0, BachNoteSource::FugueAnswer),
      makeNote(kTicksPerBeat, 62, 0, BachNoteSource::FugueAnswer),
      makeNote(0, 67, 1, BachNoteSource::FugueAnswer),
      makeNote(kTicksPerBeat, 69, 1, BachNoteSource::FugueAnswer),
  };
  auto params = makeDefaultParams(2);

  repairParallelPerfect(notes, params);

  // If a fix was applied, verify it used octave shifts only (pitch delta = +-12).
  for (const auto& n : notes) {
    if (hasRepairFlag(n)) {
      // Original pitches were 60, 62, 67, 69.
      // A structural fix must be +-12 from one of those.
      bool valid_octave_shift = false;
      for (uint8_t orig : {60, 62, 67, 69}) {
        int diff = static_cast<int>(n.pitch) - static_cast<int>(orig);
        if (diff == 12 || diff == -12) valid_octave_shift = true;
      }
      EXPECT_TRUE(valid_octave_shift)
          << "Structural note shifted by non-octave amount to pitch "
          << static_cast<int>(n.pitch);
    }
  }
}

// ---------------------------------------------------------------------------
// Test: Immutable notes are never modified.
// ---------------------------------------------------------------------------

TEST(ParallelRepair, ImmutableProtected) {
  // Voice 0 (Immutable -- SubjectCore): C4(60), D4(62).
  // Voice 1 (Flexible -- FreeCounterpoint): G4(67), A4(69).
  // Parallel P5.
  std::vector<NoteEvent> notes = {
      makeNote(0, 60, 0, BachNoteSource::SubjectCore),
      makeNote(kTicksPerBeat, 62, 0, BachNoteSource::SubjectCore),
      makeNote(0, 67, 1),
      makeNote(kTicksPerBeat, 69, 1),
  };
  auto params = makeDefaultParams(2);

  repairParallelPerfect(notes, params);

  // Voice 0 pitches must be unchanged (immutable).
  EXPECT_EQ(findNote(notes, 0, 0).pitch, 60);
  EXPECT_EQ(findNote(notes, 0, 1).pitch, 62);
  EXPECT_FALSE(hasRepairFlag(findNote(notes, 0, 0)));
  EXPECT_FALSE(hasRepairFlag(findNote(notes, 0, 1)));
}

// ---------------------------------------------------------------------------
// Test: Adjacent repair when current-tick notes are Immutable.
// ---------------------------------------------------------------------------

TEST(ParallelRepair, AdjacentFix) {
  // Voice 0: C4(60) tick 0 [Flexible], D4(62) tick 480 [Immutable].
  // Voice 1: G4(67) tick 0 [Flexible], A4(69) tick 480 [Immutable].
  // Both notes at tick 480 are Immutable, so direct fix fails.
  // Adjacent repair should shift one of the tick-0 notes instead.
  std::vector<NoteEvent> notes = {
      makeNote(0, 60, 0, BachNoteSource::FreeCounterpoint),
      makeNote(kTicksPerBeat, 62, 0, BachNoteSource::SubjectCore),
      makeNote(0, 67, 1, BachNoteSource::FreeCounterpoint),
      makeNote(kTicksPerBeat, 69, 1, BachNoteSource::SubjectCore),
  };
  auto params = makeDefaultParams(2);

  int fixed = repairParallelPerfect(notes, params);

  // Immutable notes at tick 480 must be unchanged.
  EXPECT_EQ(findNote(notes, 0, 1).pitch, 62);
  EXPECT_EQ(findNote(notes, 1, 1).pitch, 69);

  // If a fix was applied, it must be on a tick-0 note.
  if (fixed > 0) {
    bool tick0_modified = hasRepairFlag(findNote(notes, 0, 0)) ||
                          hasRepairFlag(findNote(notes, 1, 0));
    EXPECT_TRUE(tick0_modified);
  }
}

// ---------------------------------------------------------------------------
// Test: Strong-beat consonance check prevents dissonant repairs.
// ---------------------------------------------------------------------------

TEST(ParallelRepair, StrongBeatConsonance) {
  // Voice 0: C4(60), E4(64) at ticks 0 and 960 (strong beats 0 and 2).
  // Voice 1: G4(67), B4(71) at same ticks -- parallel P5.
  // Voice 2: G4(67) at tick 960 -- consonant with voice 0 (M3) and voice 1 (M3).
  // The repair at tick 960 must not create dissonance with voice 2 on the
  // strong beat. The algorithm's step-shift candidate validation checks this.
  std::vector<NoteEvent> notes = {
      makeNote(0, 60, 0),
      makeNote(kTicksPerBeat * 2, 64, 0),
      makeNote(0, 67, 1),
      makeNote(kTicksPerBeat * 2, 71, 1),
      makeNote(kTicksPerBeat * 2, 67, 2),
  };
  auto params = makeDefaultParams(3);

  repairParallelPerfect(notes, params);

  // Verify that any repaired note at tick 960 is consonant with all other
  // voices at that tick (strong-beat consonance constraint).
  for (const auto& n : notes) {
    if (!hasRepairFlag(n)) continue;
    if (n.start_tick != kTicksPerBeat * 2) continue;
    for (const auto& other : notes) {
      if (other.start_tick != kTicksPerBeat * 2) continue;
      if (other.voice == n.voice) continue;
      int ivl = interval_util::compoundToSimple(
          std::abs(static_cast<int>(n.pitch) - static_cast<int>(other.pitch)));
      // In 3+ voices, P4 (5) is acceptable.
      bool consonant = interval_util::isConsonance(ivl) || ivl == 5;
      EXPECT_TRUE(consonant)
          << "Repair created dissonance on strong beat between voice "
          << static_cast<int>(n.voice)
          << " (pitch " << static_cast<int>(n.pitch) << ") and voice "
          << static_cast<int>(other.voice) << " (pitch "
          << static_cast<int>(other.pitch) << "), interval = " << ivl;
    }
  }
}

// ---------------------------------------------------------------------------
// Test: Repair does not create new parallel perfect consonances.
// ---------------------------------------------------------------------------

TEST(ParallelRepair, NoNewParallel) {
  // Voice 0: C4(60), D4(62) at ticks 0, 480.
  // Voice 1: G4(67), A4(69) at same ticks -- parallel P5 with voice 0.
  // Voice 2: E4(64), F4(65) at same ticks.
  // The test verifies the algorithm's no-new-parallel check exists by ensuring
  // no parallel perfects remain between any voice pair after repair.
  std::vector<NoteEvent> notes = {
      makeNote(0, 60, 0), makeNote(kTicksPerBeat, 62, 0),
      makeNote(0, 67, 1), makeNote(kTicksPerBeat, 69, 1),
      makeNote(0, 64, 2), makeNote(kTicksPerBeat, 65, 2),
  };
  auto params = makeDefaultParams(3);

  repairParallelPerfect(notes, params);

  // No parallel perfect consonance should remain between any voice pair.
  for (uint8_t va = 0; va < 3; ++va) {
    for (uint8_t vb = va + 1; vb < 3; ++vb) {
      EXPECT_FALSE(hasParallelPerfect(notes, va, vb, 0, kTicksPerBeat))
          << "Remaining parallel between voice " << static_cast<int>(va)
          << " and voice " << static_cast<int>(vb);
    }
  }
}

// ---------------------------------------------------------------------------
// Test: Oblique motion is not treated as parallel (no repair needed).
// ---------------------------------------------------------------------------

TEST(ParallelRepair, ObliqueMotionNotRepaired) {
  // Voice 0: C4(60) at tick 0, C4(60) at tick 480 (stationary).
  // Voice 1: G4(67) at tick 0, G4(67) at tick 480 (stationary).
  // Same P5 interval at both ticks, but both voices are static: oblique motion.
  // The algorithm skips when ca == pp && cb == pb2.
  std::vector<NoteEvent> notes = {
      makeNote(0, 60, 0), makeNote(kTicksPerBeat, 60, 0),
      makeNote(0, 67, 1), makeNote(kTicksPerBeat, 67, 1),
  };
  auto params = makeDefaultParams(2);

  int fixed = repairParallelPerfect(notes, params);
  EXPECT_EQ(fixed, 0);
  // All pitches unchanged.
  EXPECT_EQ(findNote(notes, 0, 0).pitch, 60);
  EXPECT_EQ(findNote(notes, 0, 1).pitch, 60);
  EXPECT_EQ(findNote(notes, 1, 0).pitch, 67);
  EXPECT_EQ(findNote(notes, 1, 1).pitch, 67);
}

// ---------------------------------------------------------------------------
// Test: Rest (gap) prevents parallel detection -- no previous pitch exists.
// ---------------------------------------------------------------------------

TEST(ParallelRepair, RestSkipsParallelDetection) {
  // Voice 0 has no note at tick 0 -- only at tick 480.
  // Voice 1: G4(67) at tick 0, A4(69) at tick 480.
  // No previous pitch for voice 0 at tick 0, so no parallel can be detected.
  std::vector<NoteEvent> notes = {
      makeNote(kTicksPerBeat, 62, 0),
      makeNote(0, 67, 1), makeNote(kTicksPerBeat, 69, 1),
  };
  auto params = makeDefaultParams(2);

  int fixed = repairParallelPerfect(notes, params);
  EXPECT_EQ(fixed, 0);
}

// ---------------------------------------------------------------------------
// Test: Adjacent repair rejects shifts that create leap > 12 to next tick.
// ---------------------------------------------------------------------------

TEST(ParallelRepair, AdjacentLeapToNextTick) {
  // Voice 0: C4(60) tick 0 [Flexible], C5(72) tick 480 [Immutable].
  // Voice 1: G4(67) tick 0 [Flexible], G5(79) tick 480 [Immutable].
  // Parallel P5: |60-67| = 7 at tick 0, |72-79| = 7 at tick 480.
  // Both move up by 12 and 12 respectively, same direction.
  // Current-tick notes are Immutable -> adjacent repair on tick 0.
  // If voice 0 shifted to 48 (C3, -12), leap from 48 to 72 = 24 > 12 -> rejected.
  // If voice 0 shifted to 72 (C5, +12), leap from 72 to 72 = 0 -> OK.
  std::vector<NoteEvent> notes = {
      makeNote(0, 60, 0, BachNoteSource::FreeCounterpoint),
      makeNote(kTicksPerBeat, 72, 0, BachNoteSource::SubjectCore),
      makeNote(0, 67, 1, BachNoteSource::FreeCounterpoint),
      makeNote(kTicksPerBeat, 79, 1, BachNoteSource::SubjectCore),
  };
  auto params = makeDefaultParams(2);

  repairParallelPerfect(notes, params);

  // Immutable notes must not change.
  EXPECT_EQ(findNote(notes, 0, 1).pitch, 72);
  EXPECT_EQ(findNote(notes, 1, 1).pitch, 79);

  // If any tick-0 note was shifted, verify the leap to the next tick <= 12.
  for (const auto& n : notes) {
    if (!hasRepairFlag(n)) continue;
    // Find the next note in the same voice.
    for (const auto& next : notes) {
      if (next.voice == n.voice && next.start_tick > n.start_tick) {
        int leap = std::abs(static_cast<int>(n.pitch) -
                            static_cast<int>(next.pitch));
        EXPECT_LE(leap, 12)
            << "Adjacent repair created leap of " << leap
            << " in voice " << static_cast<int>(n.voice);
        break;
      }
    }
  }
}

// ---------------------------------------------------------------------------
// Test: Parallel unison (P1) is detected and repaired.
// ---------------------------------------------------------------------------

TEST(ParallelRepair, UnisonNotConfusedWithChordVoice) {
  // Voice 0: C4(60), D4(62) at ticks 0, 480.
  // Voice 1: C4(60), D4(62) at same ticks -- parallel unison (P1).
  // compoundToSimple(|60-60|) = 0 (P1), isPerfectConsonance(0) = true.
  // Both move up by 2: parallel unison.
  std::vector<NoteEvent> notes = {
      makeNote(0, 60, 0), makeNote(kTicksPerBeat, 62, 0),
      makeNote(0, 60, 1), makeNote(kTicksPerBeat, 62, 1),
  };
  auto params = makeDefaultParams(2);

  int fixed = repairParallelPerfect(notes, params);
  EXPECT_GE(fixed, 1);

  // Verify the parallel unison is broken.
  EXPECT_FALSE(hasParallelPerfect(notes, 0, 1, 0, kTicksPerBeat));
}

// ---------------------------------------------------------------------------
// Test: Empty notes vector returns 0.
// ---------------------------------------------------------------------------

TEST(ParallelRepair, EmptyNotes) {
  std::vector<NoteEvent> notes;
  auto params = makeDefaultParams(2);
  EXPECT_EQ(repairParallelPerfect(notes, params), 0);
}

// ---------------------------------------------------------------------------
// Test: Single voice returns 0 (need at least 2 voices for parallels).
// ---------------------------------------------------------------------------

TEST(ParallelRepair, SingleVoiceNoRepair) {
  std::vector<NoteEvent> notes = {
      makeNote(0, 60, 0), makeNote(kTicksPerBeat, 67, 0),
  };
  auto params = makeDefaultParams(1);
  EXPECT_EQ(repairParallelPerfect(notes, params), 0);
}

// ---------------------------------------------------------------------------
// Test: Parallel octaves are detected and repaired.
// ---------------------------------------------------------------------------

TEST(ParallelRepair, ParallelOctavesRepaired) {
  // Voice 0: C4(60), D4(62) at ticks 0, 480.
  // Voice 1: C5(72), D5(74) at same ticks.
  // Interval at tick 0: |60-72| = 12 (P8). At tick 480: |62-74| = 12 (P8).
  // Both move up by 2: parallel octaves.
  std::vector<NoteEvent> notes = {
      makeNote(0, 60, 0), makeNote(kTicksPerBeat, 62, 0),
      makeNote(0, 72, 1), makeNote(kTicksPerBeat, 74, 1),
  };
  auto params = makeDefaultParams(2);

  int fixed = repairParallelPerfect(notes, params);
  EXPECT_GE(fixed, 1);
  EXPECT_FALSE(hasParallelPerfect(notes, 0, 1, 0, kTicksPerBeat));
}

// ---------------------------------------------------------------------------
// Test: Contrary motion at perfect interval is not a parallel.
// ---------------------------------------------------------------------------

TEST(ParallelRepair, ContraryMotionNotParallel) {
  // Voice 0: C4(60), D4(62) -- moves up.
  // Voice 1: G4(67), F4(65) -- moves down.
  // Intervals: P5(7), m3(3). Not same simple interval, and contrary motion.
  std::vector<NoteEvent> notes = {
      makeNote(0, 60, 0), makeNote(kTicksPerBeat, 62, 0),
      makeNote(0, 67, 1), makeNote(kTicksPerBeat, 65, 1),
  };
  auto params = makeDefaultParams(2);

  int fixed = repairParallelPerfect(notes, params);
  EXPECT_EQ(fixed, 0);
}

// ---------------------------------------------------------------------------
// Test: Voice range constraint is respected by repairs.
// ---------------------------------------------------------------------------

TEST(ParallelRepair, VoiceRangeRespected) {
  // Voice 0: C4(60), D4(62) -- parallel P5 with voice 1.
  // Voice 1: G4(67), A4(69).
  // Tight voice range: 60-70. Octave shifts would go out of range.
  std::vector<NoteEvent> notes = {
      makeNote(0, 60, 0), makeNote(kTicksPerBeat, 62, 0),
      makeNote(0, 67, 1), makeNote(kTicksPerBeat, 69, 1),
  };
  auto params = makeDefaultParams(2);
  params.voice_range_static = [](uint8_t) -> std::pair<uint8_t, uint8_t> {
    return {60, 70};
  };

  repairParallelPerfect(notes, params);

  // All notes must remain within the voice range.
  for (const auto& n : notes) {
    EXPECT_GE(n.pitch, 60) << "Pitch below voice range in voice "
                           << static_cast<int>(n.voice);
    EXPECT_LE(n.pitch, 70) << "Pitch above voice range in voice "
                           << static_cast<int>(n.voice);
  }
}

// ---------------------------------------------------------------------------
// Test: Modified notes have the ParallelRepair flag set.
// ---------------------------------------------------------------------------

TEST(ParallelRepair, ModifiedByFlagSet) {
  // Parallel P5 that should be fixed.
  std::vector<NoteEvent> notes = {
      makeNote(0, 60, 0), makeNote(kTicksPerBeat, 62, 0),
      makeNote(0, 67, 1), makeNote(kTicksPerBeat, 69, 1),
  };
  auto params = makeDefaultParams(2);

  int fixed = repairParallelPerfect(notes, params);
  ASSERT_GE(fixed, 1);

  // At least one modified note should carry the flag.
  int flagged_count = 0;
  for (const auto& n : notes) {
    if (hasRepairFlag(n)) ++flagged_count;
  }
  EXPECT_GE(flagged_count, 1);
}

// ---------------------------------------------------------------------------
// Test: Diatonic scale constraint -- shifts produce scale tones.
// ---------------------------------------------------------------------------

TEST(ParallelRepair, ShiftProducesScaleTone) {
  // Parallel P5 in C major.
  std::vector<NoteEvent> notes = {
      makeNote(0, 60, 0), makeNote(kTicksPerBeat, 62, 0),
      makeNote(0, 67, 1), makeNote(kTicksPerBeat, 69, 1),
  };
  auto params = makeDefaultParams(2);

  repairParallelPerfect(notes, params);

  // All modified notes should be diatonic in C major (step shifts are
  // validated against the scale; octave shifts preserve pitch class).
  for (const auto& n : notes) {
    if (!hasRepairFlag(n)) continue;
    EXPECT_TRUE(scale_util::isScaleTone(n.pitch, Key::C, ScaleType::Major))
        << "Non-diatonic pitch " << static_cast<int>(n.pitch)
        << " produced by repair in voice " << static_cast<int>(n.voice);
  }
}

// ---------------------------------------------------------------------------
// Test: SemiImmutable (FugueSubject) allows only octave shifts.
// ---------------------------------------------------------------------------

TEST(ParallelRepair, SemiImmutableOctaveShiftOnly) {
  // FugueSubject -> SemiImmutable: only +-12 shifts (pitch class preserved).
  std::vector<NoteEvent> notes = {
      makeNote(0, 60, 0, BachNoteSource::FugueSubject),
      makeNote(kTicksPerBeat, 62, 0, BachNoteSource::FugueSubject),
      makeNote(0, 67, 1, BachNoteSource::FugueSubject),
      makeNote(kTicksPerBeat, 69, 1, BachNoteSource::FugueSubject),
  };
  auto params = makeDefaultParams(2);

  repairParallelPerfect(notes, params);

  // Any modified note must have its pitch class preserved.
  uint8_t originals[] = {60, 62, 67, 69};
  for (size_t i = 0; i < notes.size(); ++i) {
    if (hasRepairFlag(notes[i])) {
      uint8_t orig_pc = originals[i] % 12;
      uint8_t new_pc = notes[i].pitch % 12;
      EXPECT_EQ(new_pc, orig_pc)
          << "SemiImmutable note pitch class changed from "
          << static_cast<int>(orig_pc) << " to " << static_cast<int>(new_pc);
    }
  }
}

// ---------------------------------------------------------------------------
// Test: Tick-aware voice range takes priority over static range.
// ---------------------------------------------------------------------------

TEST(ParallelRepair, TickAwareVoiceRangePriority) {
  // Parallel P5. Set a tick-aware range that is more restrictive.
  std::vector<NoteEvent> notes = {
      makeNote(0, 60, 0), makeNote(kTicksPerBeat, 62, 0),
      makeNote(0, 67, 1), makeNote(kTicksPerBeat, 69, 1),
  };
  auto params = makeDefaultParams(2);
  // Static range is wide.
  params.voice_range_static = [](uint8_t) -> std::pair<uint8_t, uint8_t> {
    return {36, 96};
  };
  // Tick-aware range is tight: [60, 70].
  params.voice_range = [](uint8_t, Tick) -> std::pair<uint8_t, uint8_t> {
    return {60, 70};
  };

  repairParallelPerfect(notes, params);

  // All notes must respect the tick-aware range, not the wider static range.
  for (const auto& n : notes) {
    EXPECT_GE(n.pitch, 60);
    EXPECT_LE(n.pitch, 70);
  }
}

// ---------------------------------------------------------------------------
// Test: max_iterations = 0 disables repair entirely.
// ---------------------------------------------------------------------------

TEST(ParallelRepair, ZeroIterationsNoRepair) {
  std::vector<NoteEvent> notes = {
      makeNote(0, 60, 0), makeNote(kTicksPerBeat, 62, 0),
      makeNote(0, 67, 1), makeNote(kTicksPerBeat, 69, 1),
  };
  auto params = makeDefaultParams(2);
  params.max_iterations = 0;

  int fixed = repairParallelPerfect(notes, params);
  EXPECT_EQ(fixed, 0);
  EXPECT_EQ(findNote(notes, 0, 1).pitch, 62);
  EXPECT_EQ(findNote(notes, 1, 1).pitch, 69);
}

// ---------------------------------------------------------------------------
// Test: Both voices Immutable -> no fix possible.
// ---------------------------------------------------------------------------

TEST(ParallelRepair, BothImmutableNoFix) {
  std::vector<NoteEvent> notes = {
      makeNote(0, 60, 0, BachNoteSource::SubjectCore),
      makeNote(kTicksPerBeat, 62, 0, BachNoteSource::SubjectCore),
      makeNote(0, 67, 1, BachNoteSource::SubjectCore),
      makeNote(kTicksPerBeat, 69, 1, BachNoteSource::SubjectCore),
  };
  auto params = makeDefaultParams(2);

  int fixed = repairParallelPerfect(notes, params);
  EXPECT_EQ(fixed, 0);
  // All pitches unchanged.
  EXPECT_EQ(findNote(notes, 0, 0).pitch, 60);
  EXPECT_EQ(findNote(notes, 0, 1).pitch, 62);
  EXPECT_EQ(findNote(notes, 1, 0).pitch, 67);
  EXPECT_EQ(findNote(notes, 1, 1).pitch, 69);
}

// ---------------------------------------------------------------------------
// Test: Melodic leap constraint prevents shifts > octave from prev note.
// ---------------------------------------------------------------------------

TEST(ParallelRepair, MelodicLeapConstraint) {
  // Voice 0: C4(60) tick 0, C4(60) tick 480, D4(62) tick 960.
  // Voice 1: G4(67) tick 0, G4(67) tick 480, A4(69) tick 960.
  // Parallel P5 at tick 960 (previous tick = 480).
  std::vector<NoteEvent> notes = {
      makeNote(0, 60, 0),
      makeNote(kTicksPerBeat, 60, 0),
      makeNote(kTicksPerBeat * 2, 62, 0),
      makeNote(0, 67, 1),
      makeNote(kTicksPerBeat, 67, 1),
      makeNote(kTicksPerBeat * 2, 69, 1),
  };
  auto params = makeDefaultParams(2);

  repairParallelPerfect(notes, params);

  // Any modified note should not create a leap > 12 from its predecessor.
  for (uint8_t v = 0; v < 2; ++v) {
    std::vector<const NoteEvent*> voice_notes;
    for (const auto& n : notes) {
      if (n.voice == v) voice_notes.push_back(&n);
    }
    std::sort(voice_notes.begin(), voice_notes.end(),
              [](const NoteEvent* a, const NoteEvent* b) {
                return a->start_tick < b->start_tick;
              });
    for (size_t i = 1; i < voice_notes.size(); ++i) {
      int leap = std::abs(static_cast<int>(voice_notes[i]->pitch) -
                          static_cast<int>(voice_notes[i - 1]->pitch));
      EXPECT_LE(leap, 12)
          << "Repair created leap of " << leap << " in voice "
          << static_cast<int>(v) << " between ticks "
          << voice_notes[i - 1]->start_tick << " and "
          << voice_notes[i]->start_tick;
    }
  }
}

}  // namespace
}  // namespace bach
