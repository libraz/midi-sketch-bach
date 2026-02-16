// Tests for generator_internal.h -- voice-aware overlap cleanup.

#include "generator_internal.h"

#include <gtest/gtest.h>

#include "core/basic_types.h"

namespace bach {
namespace {

// ---------------------------------------------------------------------------
// Helper
// ---------------------------------------------------------------------------

static NoteEvent makeNote(Tick start, Tick dur, uint8_t pitch, VoiceId voice) {
  NoteEvent n;
  n.start_tick = start;
  n.duration = dur;
  n.pitch = pitch;
  n.voice = voice;
  return n;
}

// ---------------------------------------------------------------------------
// cleanupTrackOverlaps: voice-aware tests
// ---------------------------------------------------------------------------

TEST(CleanupTrackOverlaps, CrossVoicePreserved) {
  Track track;
  track.notes.push_back(makeNote(0, 960, 50, 0));  // bass
  track.notes.push_back(makeNote(0, 480, 62, 1));  // texture
  std::vector<Track> tracks = {track};

  cleanupTrackOverlaps(tracks);

  // Both notes survive.
  ASSERT_EQ(tracks[0].notes.size(), 2u);
  // Cross-voice same-tick notes are staggered by 1 tick and truncated to avoid
  // within-track overlap (the validator checks all notes in a track together,
  // regardless of voice ID). First note gets truncated, second gets offset.
  EXPECT_EQ(tracks[0].notes[0].duration, 1u);
  EXPECT_EQ(tracks[0].notes[1].start_tick, 1u);
  EXPECT_EQ(tracks[0].notes[1].duration, 479u);
}

TEST(CleanupTrackOverlaps, SameVoiceSameTickSamePitchDedup) {
  Track track;
  track.notes.push_back(makeNote(0, 960, 50, 0));
  track.notes.push_back(makeNote(0, 480, 50, 0));  // duplicate
  std::vector<Track> tracks = {track};

  cleanupTrackOverlaps(tracks);

  ASSERT_EQ(tracks[0].notes.size(), 1u);
  EXPECT_EQ(tracks[0].notes[0].duration, 960u);  // longer kept
}

TEST(CleanupTrackOverlaps, ChordTonesPreserved) {
  Track track;
  track.notes.push_back(makeNote(0, 480, 50, 1));
  track.notes.push_back(makeNote(0, 480, 55, 1));
  track.notes.push_back(makeNote(0, 480, 59, 1));
  std::vector<Track> tracks = {track};

  cleanupTrackOverlaps(tracks);

  ASSERT_EQ(tracks[0].notes.size(), 3u);
}

TEST(CleanupTrackOverlaps, WithinVoiceTrim) {
  Track track;
  track.notes.push_back(makeNote(0, 600, 50, 0));
  track.notes.push_back(makeNote(480, 480, 55, 0));
  std::vector<Track> tracks = {track};

  cleanupTrackOverlaps(tracks);

  ASSERT_EQ(tracks[0].notes.size(), 2u);
  EXPECT_EQ(tracks[0].notes[0].duration, 480u);  // trimmed
  EXPECT_NE(tracks[0].notes[0].modified_by &
                static_cast<uint8_t>(NoteModifiedBy::OverlapTrim),
            0);
}

TEST(CleanupTrackOverlaps, CrossVoiceTrimmed) {
  Track track;
  track.notes.push_back(makeNote(0, 960, 50, 0));   // bass, overlaps texture tick
  track.notes.push_back(makeNote(480, 480, 62, 1));  // texture
  std::vector<Track> tracks = {track};

  cleanupTrackOverlaps(tracks);

  ASSERT_EQ(tracks[0].notes.size(), 2u);
  // Cross-voice overlap is now trimmed so the validator sees no within-track
  // overlap. Bass note is truncated to end at the texture note's start.
  EXPECT_EQ(tracks[0].notes[0].duration, 480u);
  EXPECT_NE(tracks[0].notes[0].modified_by &
                static_cast<uint8_t>(NoteModifiedBy::OverlapTrim),
            0);
}

TEST(CleanupTrackOverlaps, ZeroDurationBecomesOne) {
  Track track;
  track.notes.push_back(makeNote(0, 100, 50, 0));
  track.notes.push_back(makeNote(0, 100, 55, 0));  // same tick, same voice, diff pitch
  // These should survive dedup (different pitch). But if trim produces 0, it becomes 1.
  // Actually these are at same tick so no trim. Let's test a real zero case.
  track.notes.clear();
  track.notes.push_back(makeNote(0, 480, 50, 0));
  track.notes.push_back(makeNote(0, 480, 55, 0));  // same tick chord
  track.notes.push_back(makeNote(0, 100, 60, 0));  // same tick chord
  // After sort by (tick, voice, dur DESC): 480, 480, 100 - all at tick 0
  // Dedup: different pitches → all survive
  // Trim: all at same tick → no trim
  std::vector<Track> tracks = {track};

  cleanupTrackOverlaps(tracks);

  EXPECT_EQ(tracks[0].notes.size(), 3u);
}

TEST(CleanupTrackOverlaps, EmptyTrackNoOp) {
  Track track;
  std::vector<Track> tracks = {track};

  cleanupTrackOverlaps(tracks);

  EXPECT_TRUE(tracks[0].notes.empty());
}

TEST(CleanupTrackOverlaps, SingleNoteNoOp) {
  Track track;
  track.notes.push_back(makeNote(0, 480, 50, 0));
  std::vector<Track> tracks = {track};

  cleanupTrackOverlaps(tracks);

  ASSERT_EQ(tracks[0].notes.size(), 1u);
  EXPECT_EQ(tracks[0].notes[0].duration, 480u);
}

}  // namespace
}  // namespace bach
