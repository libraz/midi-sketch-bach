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

void appendBE16(std::vector<uint8_t>* bytes, uint16_t value) {
  bytes->push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
  bytes->push_back(static_cast<uint8_t>(value & 0xFF));
}

void appendBE32(std::vector<uint8_t>* bytes, uint32_t value) {
  bytes->push_back(static_cast<uint8_t>((value >> 24) & 0xFF));
  bytes->push_back(static_cast<uint8_t>((value >> 16) & 0xFF));
  bytes->push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
  bytes->push_back(static_cast<uint8_t>(value & 0xFF));
}

std::vector<uint8_t> makeType0Midi(const std::vector<uint8_t>& track_data,
                                   const std::vector<uint8_t>& extended_header = {}) {
  std::vector<uint8_t> bytes = {'M', 'T', 'h', 'd'};
  appendBE32(&bytes, static_cast<uint32_t>(6 + extended_header.size()));
  appendBE16(&bytes, 0);
  appendBE16(&bytes, 1);
  appendBE16(&bytes, 480);
  bytes.insert(bytes.end(), extended_header.begin(), extended_header.end());
  bytes.insert(bytes.end(), {'M', 'T', 'r', 'k'});
  appendBE32(&bytes, static_cast<uint32_t>(track_data.size()));
  bytes.insert(bytes.end(), track_data.begin(), track_data.end());
  return bytes;
}

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

TEST(MidiReaderTest, ExtendedHeaderLengthMovesTrackStart) {
  const std::vector<uint8_t> track = {0x00, 0xFF, 0x2F, 0x00};
  const std::vector<uint8_t> extra_header = {0x12, 0x34, 0x56, 0x78};

  MidiReader reader;
  ASSERT_TRUE(reader.read(makeType0Midi(track, extra_header))) << reader.getError();
  EXPECT_EQ(reader.getParsedMidi().tracks.size(), 1u);
}

TEST(MidiReaderTest, Type0PairsSamePitchByChannelAndSupportsOverlap) {
  const std::vector<uint8_t> track = {
      0x00, 0x90, 0x3C, 0x64,  // ch0 C4 on at 0
      0x0A, 0x91, 0x3C, 0x50,  // ch1 C4 on at 10
      0x0A, 0x90, 0x3C, 0x40,  // second ch0 C4 on at 20
      0x0A, 0x80, 0x3C, 0x00,  // oldest ch0 off at 30
      0x0A, 0x81, 0x3C, 0x00,  // ch1 off at 40
      0x0A, 0x80, 0x3C, 0x00,  // second ch0 off at 50
      0x00, 0xFF, 0x2F, 0x00,
  };

  MidiReader reader;
  ASSERT_TRUE(reader.read(makeType0Midi(track))) << reader.getError();
  const auto& notes = reader.getParsedMidi().tracks.at(0).notes;
  ASSERT_EQ(notes.size(), 3u);
  EXPECT_EQ(notes[0].start_tick, 0u);
  EXPECT_EQ(notes[0].duration, 30u);
  EXPECT_EQ(notes[0].voice, 0u);
  EXPECT_EQ(notes[1].start_tick, 10u);
  EXPECT_EQ(notes[1].duration, 30u);
  EXPECT_EQ(notes[1].voice, 1u);
  EXPECT_EQ(notes[2].start_tick, 20u);
  EXPECT_EQ(notes[2].duration, 30u);
  EXPECT_EQ(notes[2].voice, 0u);
}

TEST(MidiReaderTest, KeySignatureRoundTripsAllSupportedMajorAndMinorKeys) {
  for (uint8_t tonic = 0; tonic < 12; ++tonic) {
    for (bool is_minor : {false, true}) {
      SCOPED_TRACE(static_cast<unsigned>(tonic));
      SCOPED_TRACE(is_minor ? "minor" : "major");
      const KeySignature expected{static_cast<Key>(tonic), is_minor};
      MidiWriter writer;
      ASSERT_EQ(writer.build({}, {{0, 120}}, expected), MidiWriterStatus::Ok);

      MidiReader reader;
      ASSERT_TRUE(reader.read(writer.toBytes())) << reader.getError();
      EXPECT_TRUE(reader.getParsedMidi().has_key_signature);
      EXPECT_EQ(reader.getParsedMidi().key_signature, expected);
    }
  }
}

TEST(MidiReaderTest, RejectsTruncatedAndStructurallyInvalidTrackEvents) {
  const std::vector<std::vector<uint8_t>> invalid_tracks = {
      {0x81},                                            // truncated delta VLQ
      {0x00, 0x90, 0x3C},                                // truncated note
      {0x00, 0xFF, 0x01, 0x02, 'x'},                     // meta payload exceeds track
      {0x00, 0xF0, 0x02, 0x7D},                          // SysEx payload exceeds track
      {0x00, 0x3C, 0x40},                                // running status without status
      {0x00, 0xFF, 0x2F, 0x01, 0x00},                    // invalid EOT length
      {0x00, 0xFF, 0x2F, 0x00, 0x00},                    // bytes after EOT
      {0x00, 0x90, 0x3C, 0x40},                          // missing EOT and note-off
      {0x00, 0x90, 0x3C, 0x40, 0x00, 0xFF, 0x2F, 0x00},  // unmatched note-on
      {0x00, 0x80, 0x3C, 0x00, 0x00, 0xFF, 0x2F, 0x00},  // unmatched note-off
      {0x00, 0xFF, 0x59, 0x02, 0x08, 0x00, 0x00, 0xFF, 0x2F, 0x00},  // bad sf
      {0x00, 0xFF, 0x59, 0x02, 0x00, 0x02, 0x00, 0xFF, 0x2F, 0x00},  // bad mode
  };

  for (const auto& track : invalid_tracks) {
    MidiReader reader;
    EXPECT_FALSE(reader.read(makeType0Midi(track)));
    EXPECT_FALSE(reader.getError().empty());
    EXPECT_TRUE(reader.getParsedMidi().tracks.empty());
  }
}

TEST(MidiReaderTest, RejectsInputAboveDocumentedLimit) {
  std::vector<uint8_t> oversized(kMaxMidiInputBytes + 1, 0);
  MidiReader reader;
  EXPECT_FALSE(reader.read(oversized));
  EXPECT_NE(reader.getError().find("16 MiB"), std::string::npos);
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
  note.pitch = 60;  // C4
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
  std::vector<uint8_t> valid_header = {0x4D, 0x54, 0x68, 0x64, 0x00, 0x00, 0x00,
                                       0x06, 0x00, 0x01, 0x00, 0x00, 0x01, 0xE0};
  EXPECT_TRUE(reader.read(valid_header));
  EXPECT_TRUE(reader.getError().empty());
  EXPECT_EQ(reader.getParsedMidi().division, 480);
}

}  // namespace
}  // namespace bach
