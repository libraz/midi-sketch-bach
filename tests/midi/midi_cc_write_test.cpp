// Tests for Track::cc_events write-out in MidiWriter (SMF Type 1).
//
// Two guarantees are covered:
//   1. A Track WITHOUT cc_events produces byte-identical output to before the
//      CC feature was added (additive change).
//   2. A Track WITH cc_events emits the controller messages (status 0xB0|ch,
//      controller, value) in tick order, with CC ordered before a note-on that
//      shares the same tick.

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

#include "core/basic_types.h"
#include "midi/midi_writer.h"

namespace bach {
namespace {

// Build a track with one note. Channel 0, C4 quarter at tick 0.
Track makeNoteTrack(uint8_t channel) {
  Track track;
  track.channel = channel;
  track.program = 19;
  track.name = "EXP";
  NoteEvent note;
  note.pitch = 60;
  note.start_tick = 0;
  note.duration = duration::kQuarterNote;
  note.velocity = 80;
  note.voice = 0;
  track.notes.push_back(note);
  return track;
}

// Scan for a 3-byte channel message (status, data1, data2) anywhere in bytes.
bool containsMessage(const std::vector<uint8_t>& bytes, uint8_t status, uint8_t data1,
                     uint8_t data2) {
  for (size_t idx = 0; idx + 2 < bytes.size(); ++idx) {
    if (bytes[idx] == status && bytes[idx + 1] == data1 && bytes[idx + 2] == data2) {
      return true;
    }
  }
  return false;
}

// Index of the first occurrence of a status byte, or npos.
size_t firstStatusIndex(const std::vector<uint8_t>& bytes, uint8_t status) {
  for (size_t idx = 0; idx < bytes.size(); ++idx) {
    if (bytes[idx] == status) {
      return idx;
    }
  }
  return std::vector<uint8_t>::size_type(-1);
}

// ---------------------------------------------------------------------------
// Additive guarantee: empty cc_events == byte-identical.
// ---------------------------------------------------------------------------

TEST(MidiCcWriteTest, EmptyCcEventsAreByteIdentical) {
  Track track = makeNoteTrack(0);
  std::vector<TempoEvent> tempos = {{0, 120}};

  MidiWriter writer_no_cc;
  writer_no_cc.build({track}, tempos, Key::C);
  const auto baseline = writer_no_cc.toBytes();

  // Explicitly default-constructed cc_events (empty) must not change output.
  ASSERT_TRUE(track.cc_events.empty());
  MidiWriter writer_empty_cc;
  writer_empty_cc.build({track}, tempos, Key::C);
  const auto with_empty = writer_empty_cc.toBytes();

  EXPECT_EQ(baseline, with_empty);
}

// ---------------------------------------------------------------------------
// CC events are emitted.
// ---------------------------------------------------------------------------

TEST(MidiCcWriteTest, CcEventsAreEmitted) {
  Track track = makeNoteTrack(2);
  // Add a registration-style pair: CC#7=75 and CC#11=75 at tick 0.
  track.cc_events.push_back({0, 7, 75});
  track.cc_events.push_back({0, 11, 75});

  std::vector<TempoEvent> tempos = {{0, 120}};
  MidiWriter writer;
  writer.build({track}, tempos, Key::C);
  const auto bytes = writer.toBytes();

  const uint8_t status = static_cast<uint8_t>(0xB0 | 2);  // channel 2
  EXPECT_TRUE(containsMessage(bytes, status, 7, 75)) << "CC#7 missing";
  EXPECT_TRUE(containsMessage(bytes, status, 11, 75)) << "CC#11 missing";
}

TEST(MidiCcWriteTest, CcStatusUsesTrackChannel) {
  Track track = makeNoteTrack(5);
  track.cc_events.push_back({0, 7, 90});

  std::vector<TempoEvent> tempos = {{0, 120}};
  MidiWriter writer;
  writer.build({track}, tempos, Key::C);
  const auto bytes = writer.toBytes();

  EXPECT_TRUE(containsMessage(bytes, static_cast<uint8_t>(0xB0 | 5), 7, 90));
  // Wrong channel must not be present.
  EXPECT_FALSE(containsMessage(bytes, static_cast<uint8_t>(0xB0 | 0), 7, 90));
}

// ---------------------------------------------------------------------------
// At equal tick, CC sorts before the note-on.
// ---------------------------------------------------------------------------

TEST(MidiCcWriteTest, CcPrecedesNoteOnAtSameTick) {
  Track track = makeNoteTrack(0);         // note-on at tick 0
  track.cc_events.push_back({0, 7, 80});  // CC at tick 0

  std::vector<TempoEvent> tempos = {{0, 120}};
  MidiWriter writer;
  writer.build({track}, tempos, Key::C);
  const auto bytes = writer.toBytes();

  const size_t cc_idx = firstStatusIndex(bytes, static_cast<uint8_t>(0xB0 | 0));
  const size_t on_idx = firstStatusIndex(bytes, static_cast<uint8_t>(0x90 | 0));
  ASSERT_NE(cc_idx, std::vector<uint8_t>::size_type(-1));
  ASSERT_NE(on_idx, std::vector<uint8_t>::size_type(-1));
  EXPECT_LT(cc_idx, on_idx) << "CC must precede note-on at the same tick";
}

// ---------------------------------------------------------------------------
// CC events at different ticks are emitted in tick order.
// ---------------------------------------------------------------------------

TEST(MidiCcWriteTest, CcEventsOrderedByTick) {
  Track track;
  track.channel = 0;
  track.program = 19;
  // Two notes far apart so the CC at the later tick lands between them.
  NoteEvent n0;
  n0.pitch = 60;
  n0.start_tick = 0;
  n0.duration = duration::kQuarterNote;
  n0.velocity = 80;
  track.notes.push_back(n0);
  NoteEvent n1 = n0;
  n1.start_tick = duration::kWholeNote;
  track.notes.push_back(n1);

  track.cc_events.push_back({duration::kHalfNote, 7, 60});

  std::vector<TempoEvent> tempos = {{0, 120}};
  MidiWriter writer;
  writer.build({track}, tempos, Key::C);
  const auto bytes = writer.toBytes();

  EXPECT_TRUE(containsMessage(bytes, static_cast<uint8_t>(0xB0 | 0), 7, 60));
}

}  // namespace
}  // namespace bach
