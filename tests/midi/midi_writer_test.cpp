// Tests for midi/midi_writer.h -- SMF Type 1 MIDI output.

#include "midi/midi_writer.h"

#include <gtest/gtest.h>

#include <cstdio>
#include <cstdint>
#include <string>
#include <vector>

#include "core/basic_types.h"
#include "midi/midi_stream.h"

namespace bach {
namespace {

// ---------------------------------------------------------------------------
// Helper: create a Track with a single note for reuse across tests.
// ---------------------------------------------------------------------------

Track makeSimpleTrack(uint8_t channel, uint8_t program, const std::string& name,
                      uint8_t pitch, Tick start, Tick duration) {
  Track track;
  track.channel = channel;
  track.program = program;
  track.name = name;

  NoteEvent note;
  note.pitch = pitch;
  note.start_tick = start;
  note.duration = duration;
  note.velocity = 80;
  note.voice = 0;
  track.notes.push_back(note);

  return track;
}

// ---------------------------------------------------------------------------
// Default construction
// ---------------------------------------------------------------------------

TEST(MidiWriterTest, DefaultConstructionProducesEmptyData) {
  MidiWriter writer;
  auto bytes = writer.toBytes();
  EXPECT_TRUE(bytes.empty()) << "Freshly constructed MidiWriter should have no data";
}

// ---------------------------------------------------------------------------
// Build with empty tracks
// ---------------------------------------------------------------------------

TEST(MidiWriterTest, BuildWithEmptyTracksProducesValidMidi) {
  MidiWriter writer;
  std::vector<Track> tracks;  // No tracks with content

  writer.build(tracks, {{0, 120}});

  auto bytes = writer.toBytes();
  // Even with no content tracks, the metadata track is always present.
  // MThd (14 bytes) + at least one MTrk chunk.
  EXPECT_FALSE(bytes.empty()) << "build() with empty tracks should still produce MIDI data";
  EXPECT_GE(bytes.size(), 14u) << "MIDI data must include at least the MThd header (14 bytes)";
}

// ---------------------------------------------------------------------------
// MIDI header verification -- "MThd" magic bytes
// ---------------------------------------------------------------------------

TEST(MidiWriterTest, HeaderStartsWithMThd) {
  MidiWriter writer;
  std::vector<Track> tracks;
  writer.build(tracks, {{0, 120}});

  auto bytes = writer.toBytes();
  ASSERT_GE(bytes.size(), 4u) << "Output too small to contain MThd header";

  EXPECT_EQ(bytes[0], 'M');
  EXPECT_EQ(bytes[1], 'T');
  EXPECT_EQ(bytes[2], 'h');
  EXPECT_EQ(bytes[3], 'd');
}

TEST(MidiWriterTest, HeaderFormatIsType1) {
  MidiWriter writer;
  std::vector<Track> tracks;
  writer.build(tracks, {{0, 120}});

  auto bytes = writer.toBytes();
  ASSERT_GE(bytes.size(), 14u) << "Output too small to contain full MThd header";

  // Bytes 4-7: header length = 6
  uint32_t header_length = readBE32(bytes.data(), 4);
  EXPECT_EQ(header_length, 6u);

  // Bytes 8-9: format type = 1 (multi-track)
  uint16_t format = readBE16(bytes.data(), 8);
  EXPECT_EQ(format, 1u);

  // Bytes 12-13: division = kTicksPerBeat (480)
  uint16_t division = readBE16(bytes.data(), 12);
  EXPECT_EQ(division, kTicksPerBeat);
}

TEST(MidiWriterTest, HeaderTrackCountMatchesTracks) {
  MidiWriter writer;
  Track track_one = makeSimpleTrack(0, 19, "Great", 60, 0, 480);
  Track track_two = makeSimpleTrack(1, 20, "Swell", 64, 0, 480);
  std::vector<Track> tracks = {track_one, track_two};

  writer.build(tracks, {{0, 120}});

  auto bytes = writer.toBytes();
  ASSERT_GE(bytes.size(), 14u);

  // Bytes 10-11: number of tracks = 2 content tracks + 1 metadata track = 3
  uint16_t num_tracks = readBE16(bytes.data(), 10);
  EXPECT_EQ(num_tracks, 3u) << "Expected 2 content tracks + 1 metadata track";
}

// ---------------------------------------------------------------------------
// Build with one track containing notes
// ---------------------------------------------------------------------------

TEST(MidiWriterTest, BuildWithOneTrackContainingNotes) {
  MidiWriter writer;
  Track track = makeSimpleTrack(0, 19, "Great", kMidiC4, 0, kTicksPerBeat);
  std::vector<Track> tracks = {track};

  writer.build(tracks, {{0, 120}});

  auto bytes = writer.toBytes();
  EXPECT_GT(bytes.size(), 14u) << "Output must be larger than just the MThd header";

  // Verify track count in header: 1 content track + 1 metadata track = 2
  uint16_t num_tracks = readBE16(bytes.data(), 10);
  EXPECT_EQ(num_tracks, 2u);
}

TEST(MidiWriterTest, BuildWithMultipleNotesInTrack) {
  MidiWriter writer;
  Track track;
  track.channel = 0;
  track.program = 19;
  track.name = "Great";

  // Add three notes at different positions (C4, E4, G4 -- a C major triad)
  NoteEvent note_c4;
  note_c4.pitch = 60;
  note_c4.start_tick = 0;
  note_c4.duration = kTicksPerBeat;
  note_c4.velocity = 80;
  track.notes.push_back(note_c4);

  NoteEvent note_e4;
  note_e4.pitch = 64;
  note_e4.start_tick = kTicksPerBeat;
  note_e4.duration = kTicksPerBeat;
  note_e4.velocity = 80;
  track.notes.push_back(note_e4);

  NoteEvent note_g4;
  note_g4.pitch = 67;
  note_g4.start_tick = kTicksPerBeat * 2;
  note_g4.duration = kTicksPerBeat;
  note_g4.velocity = 80;
  track.notes.push_back(note_g4);

  std::vector<Track> tracks = {track};
  writer.build(tracks, {{0, 120}});

  auto bytes = writer.toBytes();
  // 3 notes produce 6 events (3 on + 3 off), so output should be substantial.
  EXPECT_GT(bytes.size(), 50u) << "Three notes should produce meaningful MIDI data";
}

// ---------------------------------------------------------------------------
// Empty tracks are excluded from content track count
// ---------------------------------------------------------------------------

TEST(MidiWriterTest, EmptyTracksAreSkipped) {
  MidiWriter writer;
  Track empty_track;  // No notes, no events
  Track content_track = makeSimpleTrack(0, 19, "Great", 60, 0, 480);
  std::vector<Track> tracks = {empty_track, content_track};

  writer.build(tracks, {{0, 120}});

  auto bytes = writer.toBytes();
  ASSERT_GE(bytes.size(), 14u);

  // Only 1 non-empty content track + 1 metadata track = 2
  uint16_t num_tracks = readBE16(bytes.data(), 10);
  EXPECT_EQ(num_tracks, 2u) << "Empty tracks should not be counted in the track total";
}

// ---------------------------------------------------------------------------
// toBytes returns data after build
// ---------------------------------------------------------------------------

TEST(MidiWriterTest, ToBytesReturnsDataAfterBuild) {
  MidiWriter writer;

  // Before build: empty
  EXPECT_TRUE(writer.toBytes().empty());

  // After build: non-empty
  std::vector<Track> tracks;
  writer.build(tracks, {{0, 120}});
  auto bytes = writer.toBytes();
  EXPECT_FALSE(bytes.empty());
}

TEST(MidiWriterTest, ConsecutiveBuildsOverwritePreviousData) {
  MidiWriter writer;

  // First build with one track
  Track track_one = makeSimpleTrack(0, 19, "Great", 60, 0, 480);
  writer.build({track_one}, {{0, 120}});
  auto first_bytes = writer.toBytes();

  // Second build with two tracks -- data should be different
  Track track_two = makeSimpleTrack(1, 20, "Swell", 64, 0, 480);
  writer.build({track_one, track_two}, {{0, 120}});
  auto second_bytes = writer.toBytes();

  EXPECT_NE(first_bytes.size(), second_bytes.size())
      << "Rebuilding with different tracks should produce different output";
}

// ---------------------------------------------------------------------------
// writeToFile produces a file on disk
// ---------------------------------------------------------------------------

TEST(MidiWriterTest, WriteToFileCreatesFile) {
  MidiWriter writer;
  Track track = makeSimpleTrack(0, 19, "Great", kMidiC4, 0, kTicksPerBeat);
  writer.build({track}, {{0, 120}});

  const std::string path = "/tmp/bach_test_output.mid";

  // Clean up from any previous run
  std::remove(path.c_str());

  bool result = writer.writeToFile(path);
  EXPECT_TRUE(result) << "writeToFile should succeed for a valid path";

  // Verify the file exists and is non-empty by opening it
  FILE* file = std::fopen(path.c_str(), "rb");
  ASSERT_NE(file, nullptr) << "Output file should exist at " << path;

  std::fseek(file, 0, SEEK_END);
  long file_size = std::ftell(file);
  std::fclose(file);

  EXPECT_GT(file_size, 0) << "Output file should not be empty";

  // Clean up
  std::remove(path.c_str());
}

TEST(MidiWriterTest, WriteToFileContentMatchesToBytes) {
  MidiWriter writer;
  Track track = makeSimpleTrack(0, 19, "Great", kMidiC4, 0, kTicksPerBeat);
  writer.build({track}, {{0, 120}});

  const std::string path = "/tmp/bach_test_output_match.mid";
  std::remove(path.c_str());

  writer.writeToFile(path);

  // Read the file back and compare with toBytes()
  auto expected = writer.toBytes();

  FILE* file = std::fopen(path.c_str(), "rb");
  ASSERT_NE(file, nullptr);

  std::fseek(file, 0, SEEK_END);
  long file_size = std::ftell(file);
  std::fseek(file, 0, SEEK_SET);

  std::vector<uint8_t> file_content(static_cast<size_t>(file_size));
  std::fread(file_content.data(), 1, file_content.size(), file);
  std::fclose(file);

  EXPECT_EQ(file_content, expected)
      << "File content should exactly match toBytes() output";

  std::remove(path.c_str());
}

TEST(MidiWriterTest, WriteToFileFailsForInvalidPath) {
  MidiWriter writer;
  writer.build({}, {{0, 120}});

  // Writing to a non-existent directory should fail
  bool result = writer.writeToFile("/nonexistent_dir_abc123/output.mid");
  EXPECT_FALSE(result) << "writeToFile should return false for an invalid path";
}

// ---------------------------------------------------------------------------
// Key transposition
// ---------------------------------------------------------------------------

TEST(MidiWriterTest, BuildWithKeyTranspositionProducesDifferentOutput) {
  MidiWriter writer_c;
  MidiWriter writer_g;

  Track track = makeSimpleTrack(0, 19, "Great", kMidiC4, 0, kTicksPerBeat);

  writer_c.build({track}, {{0, 120}}, Key::C);
  writer_g.build({track}, {{0, 120}}, Key::G);

  auto bytes_c = writer_c.toBytes();
  auto bytes_g = writer_g.toBytes();

  // Both should be valid (non-empty), but differ due to transposition
  EXPECT_FALSE(bytes_c.empty());
  EXPECT_FALSE(bytes_g.empty());
  EXPECT_NE(bytes_c, bytes_g)
      << "Transposing to G should produce different output than C";
}

// ---------------------------------------------------------------------------
// Metadata embedding
// ---------------------------------------------------------------------------

TEST(MidiWriterTest, MetadataEmbeddedInOutput) {
  MidiWriter writer_no_meta;
  MidiWriter writer_with_meta;

  writer_no_meta.build({}, {{0, 120}}, Key::C, "");
  writer_with_meta.build({}, {{0, 120}}, Key::C, "{\"seed\":42}");

  auto bytes_no_meta = writer_no_meta.toBytes();
  auto bytes_with_meta = writer_with_meta.toBytes();

  // With metadata should be larger than without
  EXPECT_GT(bytes_with_meta.size(), bytes_no_meta.size())
      << "Metadata should increase the output size";
}

// ---------------------------------------------------------------------------
// MTrk chunk presence
// ---------------------------------------------------------------------------

TEST(MidiWriterTest, OutputContainsMTrkChunks) {
  MidiWriter writer;
  Track track = makeSimpleTrack(0, 19, "Great", 60, 0, 480);
  writer.build({track}, {{0, 120}});

  auto bytes = writer.toBytes();

  // Count "MTrk" occurrences in the byte stream.
  // Expected: 2 (1 metadata track + 1 content track)
  int mtrk_count = 0;
  for (size_t idx = 0; idx + 3 < bytes.size(); ++idx) {
    if (bytes[idx] == 'M' && bytes[idx + 1] == 'T' &&
        bytes[idx + 2] == 'r' && bytes[idx + 3] == 'k') {
      ++mtrk_count;
    }
  }
  EXPECT_EQ(mtrk_count, 2) << "Expected 1 metadata track + 1 content track = 2 MTrk chunks";
}

// ---------------------------------------------------------------------------
// BPM affects tempo meta-event
// ---------------------------------------------------------------------------

TEST(MidiWriterTest, DifferentBpmProducesDifferentOutput) {
  MidiWriter writer_slow;
  MidiWriter writer_fast;

  writer_slow.build({}, {{0, 60}});
  writer_fast.build({}, {{0, 180}});

  auto bytes_slow = writer_slow.toBytes();
  auto bytes_fast = writer_fast.toBytes();

  // Both should be valid, but differ due to different tempo meta-events
  EXPECT_FALSE(bytes_slow.empty());
  EXPECT_FALSE(bytes_fast.empty());
  EXPECT_NE(bytes_slow, bytes_fast) << "Different BPM values should produce different output";
}

// ---------------------------------------------------------------------------
// Time signature support (Goldberg Variations infrastructure)
// ---------------------------------------------------------------------------

TEST(MidiWriterTest, BackwardCompatibleBuildStillWorks) {
  // The old build() signature should still produce valid MIDI.
  MidiWriter writer;
  Track track;
  track.channel = 0;
  track.program = 0;
  track.name = "Test";
  NoteEvent note;
  note.start_tick = 0;
  note.duration = 480;
  note.pitch = 60;
  note.velocity = 80;
  track.notes.push_back(note);

  std::vector<TempoEvent> tempos = {{0, 120}};
  writer.build({track}, tempos, Key::C);
  auto bytes = writer.toBytes();
  EXPECT_GT(bytes.size(), 0u);
}

TEST(MidiWriterTest, TimeSignature3_4InMetadata) {
  MidiWriter writer;
  Track track;
  track.channel = 0;
  track.program = 0;
  NoteEvent note;
  note.start_tick = 0;
  note.duration = 480;
  note.pitch = 60;
  note.velocity = 80;
  track.notes.push_back(note);

  std::vector<TempoEvent> tempos = {{0, 120}};
  std::vector<TimeSignatureEvent> time_sigs = {{0, {3, 4}}};
  writer.build({track}, tempos, time_sigs, Key::C);
  auto bytes = writer.toBytes();

  // Search for FF 58 04 03 02 (3/4 time: numerator=3, denom=log2(4)=2)
  bool found = false;
  for (size_t idx = 0; idx + 4 < bytes.size(); ++idx) {
    if (bytes[idx] == 0xFF && bytes[idx + 1] == 0x58 && bytes[idx + 2] == 0x04 &&
        bytes[idx + 3] == 0x03 && bytes[idx + 4] == 0x02) {
      found = true;
      break;
    }
  }
  EXPECT_TRUE(found) << "Expected 3/4 time signature meta-event in MIDI output";
}

TEST(MidiWriterTest, EmptyTimeSigFallsBackTo4_4) {
  MidiWriter writer;
  Track track;
  track.channel = 0;
  track.program = 0;
  NoteEvent note;
  note.start_tick = 0;
  note.duration = 480;
  note.pitch = 60;
  note.velocity = 80;
  track.notes.push_back(note);

  std::vector<TempoEvent> tempos = {{0, 120}};
  std::vector<TimeSignatureEvent> empty_ts;
  writer.build({track}, tempos, empty_ts, Key::C);
  auto bytes = writer.toBytes();

  // Should have default 4/4: FF 58 04 04 02
  bool found = false;
  for (size_t idx = 0; idx + 4 < bytes.size(); ++idx) {
    if (bytes[idx] == 0xFF && bytes[idx + 1] == 0x58 && bytes[idx + 2] == 0x04 &&
        bytes[idx + 3] == 0x04 && bytes[idx + 4] == 0x02) {
      found = true;
      break;
    }
  }
  EXPECT_TRUE(found) << "Expected default 4/4 time signature in MIDI output";
}

TEST(MidiWriterTest, TimeSignature6_8DenominatorEncoding) {
  MidiWriter writer;
  Track track;
  track.channel = 0;
  track.program = 0;
  NoteEvent note;
  note.start_tick = 0;
  note.duration = 480;
  note.pitch = 60;
  note.velocity = 80;
  track.notes.push_back(note);

  std::vector<TempoEvent> tempos = {{0, 120}};
  std::vector<TimeSignatureEvent> time_sigs = {{0, {6, 8}}};
  writer.build({track}, tempos, time_sigs, Key::C);
  auto bytes = writer.toBytes();

  // 6/8: FF 58 04 06 03 (numerator=6, denom=log2(8)=3)
  bool found = false;
  for (size_t idx = 0; idx + 4 < bytes.size(); ++idx) {
    if (bytes[idx] == 0xFF && bytes[idx + 1] == 0x58 && bytes[idx + 2] == 0x04 &&
        bytes[idx + 3] == 0x06 && bytes[idx + 4] == 0x03) {
      found = true;
      break;
    }
  }
  EXPECT_TRUE(found) << "Expected 6/8 time signature (denom=3) in MIDI output";
}

}  // namespace
}  // namespace bach
