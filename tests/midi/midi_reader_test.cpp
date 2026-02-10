// Tests for MidiReader -- SMF parsing, round-trip with MidiWriter.

#include "midi/midi_reader.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <vector>

#include "core/basic_types.h"
#include "midi/midi_writer.h"

namespace bach {
namespace {

// ---------------------------------------------------------------------------
// Reading from non-existent file
// ---------------------------------------------------------------------------

TEST(MidiReaderTest, ReadNonExistentFileReturnsFalse) {
  MidiReader reader;
  bool result = reader.read("/tmp/bach_nonexistent_file_12345.mid");
  EXPECT_FALSE(result);
  EXPECT_FALSE(reader.getError().empty());
}

TEST(MidiReaderTest, ReadNonExistentFileErrorContainsPath) {
  MidiReader reader;
  std::string path = "/tmp/bach_nonexistent_file_12345.mid";
  reader.read(path);
  EXPECT_NE(reader.getError().find(path), std::string::npos);
}

// ---------------------------------------------------------------------------
// Reading from invalid data
// ---------------------------------------------------------------------------

TEST(MidiReaderTest, ReadEmptyDataReturnsFalse) {
  MidiReader reader;
  std::vector<uint8_t> empty_data;
  EXPECT_FALSE(reader.read(empty_data));
  EXPECT_FALSE(reader.getError().empty());
}

TEST(MidiReaderTest, ReadTooSmallDataReturnsFalse) {
  MidiReader reader;
  // Less than 14 bytes (minimum MIDI header size).
  std::vector<uint8_t> small_data = {0x4D, 0x54, 0x68, 0x64, 0x00};
  EXPECT_FALSE(reader.read(small_data));
}

TEST(MidiReaderTest, ReadInvalidMagicBytesReturnsFalse) {
  MidiReader reader;
  // Valid size but wrong magic bytes (not "MThd").
  std::vector<uint8_t> bad_header = {
      0x00, 0x00, 0x00, 0x00,  // Not "MThd"
      0x00, 0x00, 0x00, 0x06,  // Header length = 6
      0x00, 0x01,              // Format 1
      0x00, 0x00,              // 0 tracks
      0x01, 0xE0               // Division = 480
  };
  EXPECT_FALSE(reader.read(bad_header));
  EXPECT_NE(reader.getError().find("MThd"), std::string::npos);
}

// ---------------------------------------------------------------------------
// Parse basic MIDI header
// ---------------------------------------------------------------------------

TEST(MidiReaderTest, ParseValidHeaderFormat1) {
  MidiReader reader;
  // Minimal valid SMF Type 1 header with 0 tracks.
  std::vector<uint8_t> header_only = {
      0x4D, 0x54, 0x68, 0x64,  // "MThd"
      0x00, 0x00, 0x00, 0x06,  // Header length = 6
      0x00, 0x01,              // Format 1
      0x00, 0x00,              // 0 tracks
      0x01, 0xE0               // Division = 480
  };
  EXPECT_TRUE(reader.read(header_only));

  const auto& midi = reader.getParsedMidi();
  EXPECT_EQ(midi.format, 1);
  EXPECT_EQ(midi.num_tracks, 0);
  EXPECT_EQ(midi.division, 480);
}

TEST(MidiReaderTest, ParseValidHeaderFormat0) {
  MidiReader reader;
  std::vector<uint8_t> header_only = {
      0x4D, 0x54, 0x68, 0x64,  // "MThd"
      0x00, 0x00, 0x00, 0x06,  // Header length = 6
      0x00, 0x00,              // Format 0
      0x00, 0x00,              // 0 tracks
      0x00, 0xF0               // Division = 240
  };
  EXPECT_TRUE(reader.read(header_only));

  const auto& midi = reader.getParsedMidi();
  EXPECT_EQ(midi.format, 0);
  EXPECT_EQ(midi.division, 240);
}

TEST(MidiReaderTest, UnsupportedFormatReturnsError) {
  MidiReader reader;
  std::vector<uint8_t> header_only = {
      0x4D, 0x54, 0x68, 0x64,  // "MThd"
      0x00, 0x00, 0x00, 0x06,  // Header length = 6
      0x00, 0x03,              // Format 3 (unsupported)
      0x00, 0x00,              // 0 tracks
      0x01, 0xE0               // Division = 480
  };
  EXPECT_FALSE(reader.read(header_only));
  EXPECT_FALSE(reader.getError().empty());
}

// ---------------------------------------------------------------------------
// ParsedMidi structure
// ---------------------------------------------------------------------------

TEST(MidiReaderTest, DefaultParsedMidiValues) {
  ParsedMidi midi;
  EXPECT_EQ(midi.format, 0);
  EXPECT_EQ(midi.num_tracks, 0);
  EXPECT_EQ(midi.division, 480);
  EXPECT_EQ(midi.bpm, 120);
  EXPECT_TRUE(midi.tracks.empty());
  EXPECT_TRUE(midi.metadata.empty());
  EXPECT_FALSE(midi.hasBachMetadata());
}

TEST(MidiReaderTest, GetTrackByNameReturnsNullptrWhenNotFound) {
  ParsedMidi midi;
  EXPECT_EQ(midi.getTrack("nonexistent"), nullptr);
}

// ---------------------------------------------------------------------------
// Round-trip: write with MidiWriter, read back with MidiReader
// ---------------------------------------------------------------------------

TEST(MidiReaderTest, RoundTripSingleTrackSingleNote) {
  // Build a simple MIDI file with one track and one note.
  Track track;
  track.channel = 0;
  track.program = 19;  // Church Organ
  track.name = "Great";

  NoteEvent note;
  note.start_tick = 0;
  note.duration = 480;
  note.pitch = 60;      // C4
  note.velocity = 80;
  note.voice = 0;
  track.notes.push_back(note);

  std::vector<Track> tracks = {track};

  MidiWriter writer;
  writer.build(tracks, {{0, 120}}, Key::C);
  std::vector<uint8_t> midi_bytes = writer.toBytes();

  // Read it back.
  MidiReader reader;
  ASSERT_TRUE(reader.read(midi_bytes)) << "Read failed: " << reader.getError();

  const auto& parsed = reader.getParsedMidi();
  EXPECT_EQ(parsed.format, 1);
  EXPECT_EQ(parsed.division, 480);

  // Should have at least one track with notes.
  bool found_note = false;
  for (const auto& parsed_track : parsed.tracks) {
    for (const auto& parsed_note : parsed_track.notes) {
      if (parsed_note.pitch == 60 && parsed_note.duration == 480) {
        found_note = true;
        EXPECT_EQ(parsed_note.velocity, 80);
      }
    }
  }
  EXPECT_TRUE(found_note) << "Expected C4 note not found in parsed output";
}

TEST(MidiReaderTest, RoundTripMultipleNotes) {
  Track track;
  track.channel = 0;
  track.program = 19;
  track.name = "TestTrack";

  // Three ascending notes: C4, E4, G4 (C major triad spread across time).
  NoteEvent note_c4;
  note_c4.start_tick = 0;
  note_c4.duration = 480;
  note_c4.pitch = 60;
  note_c4.velocity = 80;
  note_c4.voice = 0;

  NoteEvent note_e4;
  note_e4.start_tick = 480;
  note_e4.duration = 480;
  note_e4.pitch = 64;
  note_e4.velocity = 80;
  note_e4.voice = 0;

  NoteEvent note_g4;
  note_g4.start_tick = 960;
  note_g4.duration = 480;
  note_g4.pitch = 67;
  note_g4.velocity = 80;
  note_g4.voice = 0;

  track.notes = {note_c4, note_e4, note_g4};

  MidiWriter writer;
  writer.build({track}, {{0, 120}}, Key::C);
  std::vector<uint8_t> midi_bytes = writer.toBytes();

  MidiReader reader;
  ASSERT_TRUE(reader.read(midi_bytes)) << "Read failed: " << reader.getError();

  const auto& parsed = reader.getParsedMidi();

  // Count total notes across all tracks.
  size_t total_notes = 0;
  for (const auto& parsed_track : parsed.tracks) {
    total_notes += parsed_track.notes.size();
  }
  EXPECT_EQ(total_notes, 3u);
}

TEST(MidiReaderTest, RoundTripPreservesTempo) {
  Track track;
  track.channel = 0;
  track.program = 19;
  track.name = "TempoTest";

  NoteEvent note;
  note.start_tick = 0;
  note.duration = 480;
  note.pitch = 60;
  note.velocity = 80;
  note.voice = 0;
  track.notes.push_back(note);

  constexpr uint16_t kTestBpm = 96;

  MidiWriter writer;
  writer.build({track}, {{0, kTestBpm}}, Key::C);
  std::vector<uint8_t> midi_bytes = writer.toBytes();

  MidiReader reader;
  ASSERT_TRUE(reader.read(midi_bytes)) << "Read failed: " << reader.getError();

  const auto& parsed = reader.getParsedMidi();
  EXPECT_EQ(parsed.bpm, kTestBpm);
}

TEST(MidiReaderTest, RoundTripWithMetadata) {
  Track track;
  track.channel = 0;
  track.program = 19;
  track.name = "MetaTest";

  NoteEvent note;
  note.start_tick = 0;
  note.duration = 480;
  note.pitch = 60;
  note.velocity = 80;
  note.voice = 0;
  track.notes.push_back(note);

  std::string metadata = R"({"form":"fugue","seed":42})";

  MidiWriter writer;
  writer.build({track}, {{0, 120}}, Key::C, metadata);
  std::vector<uint8_t> midi_bytes = writer.toBytes();

  MidiReader reader;
  ASSERT_TRUE(reader.read(midi_bytes)) << "Read failed: " << reader.getError();

  const auto& parsed = reader.getParsedMidi();
  EXPECT_TRUE(parsed.hasBachMetadata());
  EXPECT_EQ(parsed.metadata, metadata);
}

TEST(MidiReaderTest, RoundTripMultipleTracks) {
  Track track_great;
  track_great.channel = 0;
  track_great.program = 19;
  track_great.name = "Great";

  NoteEvent soprano_note;
  soprano_note.start_tick = 0;
  soprano_note.duration = 960;
  soprano_note.pitch = 72;  // C5
  soprano_note.velocity = 80;
  soprano_note.voice = 0;
  track_great.notes.push_back(soprano_note);

  Track track_pedal;
  track_pedal.channel = 3;
  track_pedal.program = 19;
  track_pedal.name = "Pedal";

  NoteEvent bass_note;
  bass_note.start_tick = 0;
  bass_note.duration = 1920;
  bass_note.pitch = 36;  // C2
  bass_note.velocity = 80;
  bass_note.voice = 3;
  track_pedal.notes.push_back(bass_note);

  MidiWriter writer;
  writer.build({track_great, track_pedal}, {{0, 120}}, Key::C);
  std::vector<uint8_t> midi_bytes = writer.toBytes();

  MidiReader reader;
  ASSERT_TRUE(reader.read(midi_bytes)) << "Read failed: " << reader.getError();

  const auto& parsed = reader.getParsedMidi();
  // At least 2 data tracks (writer may add a metadata track as well).
  EXPECT_GE(parsed.tracks.size(), 2u);
}

// ---------------------------------------------------------------------------
// Re-read resets state
// ---------------------------------------------------------------------------

TEST(MidiReaderTest, SecondReadResetsState) {
  MidiReader reader;

  // First read: failure.
  EXPECT_FALSE(reader.read(std::vector<uint8_t>{}));
  EXPECT_FALSE(reader.getError().empty());

  // Second read: valid header-only file.
  std::vector<uint8_t> valid_header = {
      0x4D, 0x54, 0x68, 0x64,
      0x00, 0x00, 0x00, 0x06,
      0x00, 0x01,
      0x00, 0x00,
      0x01, 0xE0
  };
  EXPECT_TRUE(reader.read(valid_header));
  EXPECT_TRUE(reader.getError().empty());
  EXPECT_EQ(reader.getParsedMidi().division, 480);
}

}  // namespace
}  // namespace bach
