// Tests for BowedNoteFactory.

#include "instrument/bowed/bowed_note_factory.h"

#include <gtest/gtest.h>

#include "instrument/bowed/cello_model.h"
#include "instrument/bowed/violin_model.h"

namespace bach {
namespace {

// ---------------------------------------------------------------------------
// BowedNoteFactory with CelloModel
// ---------------------------------------------------------------------------

TEST(BowedNoteFactoryTest, CelloCreatesNoteWithinRange) {
  CelloModel cello;
  BowedNoteFactory factory(cello);

  NoteEvent note = factory.createNote(60, 480, 240, 80, BachNoteSource::Unknown);
  EXPECT_EQ(note.pitch, 60);      // C4, within cello range
  EXPECT_EQ(note.start_tick, 480u);
  EXPECT_EQ(note.duration, 240u);
  EXPECT_EQ(note.velocity, 80);
  EXPECT_EQ(note.voice, 0);
}

TEST(BowedNoteFactoryTest, CelloClampsAboveRange) {
  CelloModel cello;
  BowedNoteFactory factory(cello);

  NoteEvent note = factory.createNote(100, 0, 480, 80, BachNoteSource::Unknown);  // Above A5
  EXPECT_EQ(note.pitch, 81);  // Clamped to A5
}

TEST(BowedNoteFactoryTest, CelloClampsBelowRange) {
  CelloModel cello;
  BowedNoteFactory factory(cello);

  NoteEvent note = factory.createNote(20, 0, 480, 80, BachNoteSource::Unknown);  // Below C2
  EXPECT_EQ(note.pitch, 36);  // Clamped to C2
}

TEST(BowedNoteFactoryTest, CelloVelocityClampsAt127) {
  CelloModel cello;
  BowedNoteFactory factory(cello);

  NoteEvent note = factory.createNote(60, 0, 480, 200, BachNoteSource::Unknown);
  EXPECT_EQ(note.velocity, 127);
}

// ---------------------------------------------------------------------------
// BowedNoteFactory with ViolinModel
// ---------------------------------------------------------------------------

TEST(BowedNoteFactoryTest, ViolinCreatesNoteWithinRange) {
  ViolinModel violin;
  BowedNoteFactory factory(violin);

  NoteEvent note = factory.createNote(72, 960, 480, 90, BachNoteSource::Unknown);
  EXPECT_EQ(note.pitch, 72);       // C5, within violin range
  EXPECT_EQ(note.start_tick, 960u);
  EXPECT_EQ(note.duration, 480u);
  EXPECT_EQ(note.velocity, 90);
}

TEST(BowedNoteFactoryTest, ViolinClampsAboveRange) {
  ViolinModel violin;
  BowedNoteFactory factory(violin);

  NoteEvent note = factory.createNote(110, 0, 480, 80, BachNoteSource::Unknown);  // Above C7
  EXPECT_EQ(note.pitch, 96);  // Clamped to C7
}

TEST(BowedNoteFactoryTest, ViolinClampsBelowRange) {
  ViolinModel violin;
  BowedNoteFactory factory(violin);

  NoteEvent note = factory.createNote(40, 0, 480, 80, BachNoteSource::Unknown);  // Below G3
  EXPECT_EQ(note.pitch, 55);  // Clamped to G3
}

// ---------------------------------------------------------------------------
// BowedNoteOptions
// ---------------------------------------------------------------------------

TEST(BowedNoteFactoryTest, CreatesNoteFromOptions) {
  CelloModel cello;
  BowedNoteFactory factory(cello);

  BowedNoteOptions opts;
  opts.pitch = 60;
  opts.tick = 1920;
  opts.duration = 960;
  opts.velocity = 75;
  opts.prefer_open_string = false;

  NoteEvent note = factory.createNote(opts);
  EXPECT_EQ(note.pitch, 60);
  EXPECT_EQ(note.start_tick, 1920u);
  EXPECT_EQ(note.duration, 960u);
  EXPECT_EQ(note.velocity, 75);
}

TEST(BowedNoteFactoryTest, OpenStringVelocityBoost) {
  CelloModel cello;
  BowedNoteFactory factory(cello);

  BowedNoteOptions opts;
  opts.pitch = 36;  // C2 -- open C string
  opts.tick = 0;
  opts.duration = 480;
  opts.velocity = 80;
  opts.prefer_open_string = true;

  NoteEvent note = factory.createNote(opts);
  EXPECT_EQ(note.pitch, 36);
  // Velocity should be boosted slightly for open string resonance.
  EXPECT_GT(note.velocity, 80);
  EXPECT_LE(note.velocity, 127);
}

TEST(BowedNoteFactoryTest, NoBoostWhenPreferOpenStringFalse) {
  CelloModel cello;
  BowedNoteFactory factory(cello);

  BowedNoteOptions opts;
  opts.pitch = 36;  // C2 -- open C string
  opts.tick = 0;
  opts.duration = 480;
  opts.velocity = 80;
  opts.prefer_open_string = false;

  NoteEvent note = factory.createNote(opts);
  EXPECT_EQ(note.velocity, 80);  // No boost
}

TEST(BowedNoteFactoryTest, NoBoostForNonOpenString) {
  CelloModel cello;
  BowedNoteFactory factory(cello);

  BowedNoteOptions opts;
  opts.pitch = 60;  // C4 -- not an open string
  opts.tick = 0;
  opts.duration = 480;
  opts.velocity = 80;
  opts.prefer_open_string = true;

  NoteEvent note = factory.createNote(opts);
  EXPECT_EQ(note.velocity, 80);  // No boost
}

TEST(BowedNoteFactoryTest, OpenStringBoostClampsAt127) {
  CelloModel cello;
  BowedNoteFactory factory(cello);

  BowedNoteOptions opts;
  opts.pitch = 36;  // C2 -- open C string
  opts.tick = 0;
  opts.duration = 480;
  opts.velocity = 125;
  opts.prefer_open_string = true;

  NoteEvent note = factory.createNote(opts);
  EXPECT_LE(note.velocity, 127);
}

TEST(BowedNoteFactoryTest, ConvenienceOverloadMatchesOptions) {
  ViolinModel violin;
  BowedNoteFactory factory(violin);

  NoteEvent from_params = factory.createNote(72, 480, 240, 80, BachNoteSource::Unknown);

  BowedNoteOptions opts;
  opts.pitch = 72;
  opts.tick = 480;
  opts.duration = 240;
  opts.velocity = 80;
  opts.prefer_open_string = true;

  NoteEvent from_opts = factory.createNote(opts);

  EXPECT_EQ(from_params.pitch, from_opts.pitch);
  EXPECT_EQ(from_params.start_tick, from_opts.start_tick);
  EXPECT_EQ(from_params.duration, from_opts.duration);
  // Velocity may differ slightly if pitch is an open string.
  // For MIDI 72 (C5), it is NOT an open violin string, so velocity matches.
  EXPECT_EQ(from_params.velocity, from_opts.velocity);
}

// ---------------------------------------------------------------------------
// BowedNoteOptions defaults
// ---------------------------------------------------------------------------

TEST(BowedNoteOptionsTest, DefaultValues) {
  BowedNoteOptions opts;
  EXPECT_EQ(opts.pitch, 0);
  EXPECT_EQ(opts.tick, 0u);
  EXPECT_EQ(opts.duration, 0u);
  EXPECT_EQ(opts.velocity, 80);
  EXPECT_TRUE(opts.prefer_open_string);
}

}  // namespace
}  // namespace bach
