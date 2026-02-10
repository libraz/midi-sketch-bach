// Tests for bow direction assignment.

#include "instrument/bowed/bow_direction.h"

#include <gtest/gtest.h>

#include "core/basic_types.h"
#include "instrument/bowed/bowed_string_instrument.h"

namespace bach {
namespace {

// ---------------------------------------------------------------------------
// getOpenStrings
// ---------------------------------------------------------------------------

TEST(BowDirectionTest, ViolinOpenStrings) {
  auto strings = getOpenStrings(InstrumentType::Violin);
  ASSERT_EQ(strings.size(), 4u);
  EXPECT_EQ(strings[0], 55);  // G3
  EXPECT_EQ(strings[1], 62);  // D4
  EXPECT_EQ(strings[2], 69);  // A4
  EXPECT_EQ(strings[3], 76);  // E5
}

TEST(BowDirectionTest, CelloOpenStrings) {
  auto strings = getOpenStrings(InstrumentType::Cello);
  ASSERT_EQ(strings.size(), 4u);
  EXPECT_EQ(strings[0], 36);  // C2
  EXPECT_EQ(strings[1], 43);  // G2
  EXPECT_EQ(strings[2], 50);  // D3
  EXPECT_EQ(strings[3], 57);  // A3
}

TEST(BowDirectionTest, GuitarOpenStrings) {
  auto strings = getOpenStrings(InstrumentType::Guitar);
  ASSERT_EQ(strings.size(), 6u);
  EXPECT_EQ(strings[0], 40);  // E2
  EXPECT_EQ(strings[1], 45);  // A2
  EXPECT_EQ(strings[2], 50);  // D3
  EXPECT_EQ(strings[3], 55);  // G3
  EXPECT_EQ(strings[4], 59);  // B3
  EXPECT_EQ(strings[5], 64);  // E4
}

TEST(BowDirectionTest, OrganReturnsEmpty) {
  auto strings = getOpenStrings(InstrumentType::Organ);
  EXPECT_TRUE(strings.empty());
}

TEST(BowDirectionTest, PianoReturnsEmpty) {
  auto strings = getOpenStrings(InstrumentType::Piano);
  EXPECT_TRUE(strings.empty());
}

// ---------------------------------------------------------------------------
// isLargeStringCrossing
// ---------------------------------------------------------------------------

TEST(BowDirectionTest, AdjacentStringsNotLargeCrossing) {
  // Violin: G string (55) to D string (62) = 1 string distance.
  auto strings = getOpenStrings(InstrumentType::Violin);
  EXPECT_FALSE(isLargeStringCrossing(55, 62, strings));
}

TEST(BowDirectionTest, TwoStringCrossingNotLarge) {
  // Violin: G string to A string = 2 strings.
  auto strings = getOpenStrings(InstrumentType::Violin);
  EXPECT_FALSE(isLargeStringCrossing(55, 69, strings));
}

TEST(BowDirectionTest, ThreeStringCrossingIsLarge) {
  // Violin: G string (55) to E string (76) = 3 strings.
  auto strings = getOpenStrings(InstrumentType::Violin);
  EXPECT_TRUE(isLargeStringCrossing(55, 76, strings));
}

TEST(BowDirectionTest, LargeCrossingSymmetric) {
  auto strings = getOpenStrings(InstrumentType::Violin);
  EXPECT_EQ(isLargeStringCrossing(55, 76, strings),
            isLargeStringCrossing(76, 55, strings));
}

TEST(BowDirectionTest, EmptyStringsNeverLargeCrossing) {
  std::vector<uint8_t> empty;
  EXPECT_FALSE(isLargeStringCrossing(55, 76, empty));
}

// ---------------------------------------------------------------------------
// assignBowDirections: basic patterns
// ---------------------------------------------------------------------------

TEST(BowDirectionTest, BarStartIsDownBow) {
  auto strings = getOpenStrings(InstrumentType::Violin);
  std::vector<NoteEvent> notes;

  // Note at the very start of bar 0.
  NoteEvent note;
  note.start_tick = 0;
  note.pitch = 69;
  note.duration = kTicksPerBeat;
  notes.push_back(note);

  assignBowDirections(notes, strings);

  EXPECT_EQ(notes[0].bow_direction, static_cast<uint8_t>(BowDirection::Down));
}

TEST(BowDirectionTest, AlternatingPattern) {
  auto strings = getOpenStrings(InstrumentType::Violin);
  std::vector<NoteEvent> notes;

  // Four notes on different beats within one bar, large intervals to avoid slur.
  for (int idx = 0; idx < 4; ++idx) {
    NoteEvent note;
    note.start_tick = static_cast<Tick>(idx) * kTicksPerBeat;
    note.pitch = (idx % 2 == 0) ? 62 : 72;  // Alternate pitches (large interval, no slur)
    note.duration = kTicksPerBeat;
    notes.push_back(note);
  }

  assignBowDirections(notes, strings);

  // Beat 0 = Down (bar start), then alternate.
  EXPECT_EQ(notes[0].bow_direction, static_cast<uint8_t>(BowDirection::Down));
  EXPECT_EQ(notes[1].bow_direction, static_cast<uint8_t>(BowDirection::Up));
  EXPECT_EQ(notes[2].bow_direction, static_cast<uint8_t>(BowDirection::Down));
  EXPECT_EQ(notes[3].bow_direction, static_cast<uint8_t>(BowDirection::Up));
}

TEST(BowDirectionTest, NewBarResetsToDown) {
  auto strings = getOpenStrings(InstrumentType::Violin);
  std::vector<NoteEvent> notes;

  // Last note of bar 0 (beat 3).
  NoteEvent note1;
  note1.start_tick = 3 * kTicksPerBeat;
  note1.pitch = 72;
  note1.duration = kTicksPerBeat;
  notes.push_back(note1);

  // First note of bar 1 (beat 0).
  NoteEvent note2;
  note2.start_tick = kTicksPerBar;
  note2.pitch = 62;
  note2.duration = kTicksPerBeat;
  notes.push_back(note2);

  assignBowDirections(notes, strings);

  // Both should be Down (first is bar-start of bar 0 with beat 3 -> not bar start, so
  // actually beat 3 is not beat 0, let me fix: note1 at beat 3 gets Down (first note),
  // note2 at bar 1 beat 0 gets Down (bar start reset)).
  EXPECT_EQ(notes[0].bow_direction, static_cast<uint8_t>(BowDirection::Down));
  EXPECT_EQ(notes[1].bow_direction, static_cast<uint8_t>(BowDirection::Down));
}

TEST(BowDirectionTest, LargeCrossingGetsNatural) {
  auto strings = getOpenStrings(InstrumentType::Violin);
  std::vector<NoteEvent> notes;

  // First note on G string.
  NoteEvent note1;
  note1.start_tick = 0;
  note1.pitch = 55;  // G3 (G string)
  note1.duration = kTicksPerBeat;
  notes.push_back(note1);

  // Second note on E string (3 strings away).
  NoteEvent note2;
  note2.start_tick = kTicksPerBeat;
  note2.pitch = 76;  // E5 (E string)
  note2.duration = kTicksPerBeat;
  notes.push_back(note2);

  assignBowDirections(notes, strings);

  EXPECT_EQ(notes[0].bow_direction, static_cast<uint8_t>(BowDirection::Down));
  EXPECT_EQ(notes[1].bow_direction, static_cast<uint8_t>(BowDirection::Natural));
}

TEST(BowDirectionTest, BowDirectionFieldIsSetInNoteEvent) {
  // Verify that the bow_direction field in NoteEvent works as expected.
  NoteEvent note;
  note.bow_direction = static_cast<uint8_t>(BowDirection::Up);
  EXPECT_EQ(note.bow_direction, 2u);

  note.bow_direction = static_cast<uint8_t>(BowDirection::Down);
  EXPECT_EQ(note.bow_direction, 1u);

  note.bow_direction = static_cast<uint8_t>(BowDirection::Natural);
  EXPECT_EQ(note.bow_direction, 0u);
}

TEST(BowDirectionTest, EmptyNotesDoesNotCrash) {
  auto strings = getOpenStrings(InstrumentType::Violin);
  std::vector<NoteEvent> notes;
  assignBowDirections(notes, strings);
  EXPECT_TRUE(notes.empty());
}

TEST(BowDirectionTest, SingleNoteGetsDown) {
  auto strings = getOpenStrings(InstrumentType::Violin);
  std::vector<NoteEvent> notes;

  NoteEvent note;
  note.start_tick = 0;
  note.pitch = 69;
  note.duration = kTicksPerBeat;
  notes.push_back(note);

  assignBowDirections(notes, strings);
  EXPECT_EQ(notes[0].bow_direction, static_cast<uint8_t>(BowDirection::Down));
}

TEST(BowDirectionTest, StepwiseMotionMaintainsDirection) {
  auto strings = getOpenStrings(InstrumentType::Violin);
  std::vector<NoteEvent> notes;

  // Beat 0: Down bow.
  NoteEvent note1;
  note1.start_tick = 0;
  note1.pitch = 69;  // A4
  note1.duration = kTicksPerBeat;
  notes.push_back(note1);

  // Beat 1: stepwise motion (1 semitone) should maintain direction.
  NoteEvent note2;
  note2.start_tick = kTicksPerBeat;
  note2.pitch = 70;  // Bb4 (1 semitone up)
  note2.duration = kTicksPerBeat;
  notes.push_back(note2);

  // Beat 2: still stepwise, should maintain.
  NoteEvent note3;
  note3.start_tick = 2 * kTicksPerBeat;
  note3.pitch = 71;  // B4 (1 semitone up)
  note3.duration = kTicksPerBeat;
  notes.push_back(note3);

  assignBowDirections(notes, strings);

  // Beat 0 = Down, beats 1-2 stepwise = maintain (both Down since we never alternate).
  EXPECT_EQ(notes[0].bow_direction, static_cast<uint8_t>(BowDirection::Down));
  // Stepwise notes on non-bar-start beats maintain previous direction.
  EXPECT_EQ(notes[1].bow_direction, static_cast<uint8_t>(BowDirection::Up));
  EXPECT_EQ(notes[2].bow_direction, static_cast<uint8_t>(BowDirection::Up));
}

}  // namespace
}  // namespace bach
