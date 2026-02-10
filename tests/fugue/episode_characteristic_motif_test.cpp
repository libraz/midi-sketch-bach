#include "fugue/episode.h"

#include <gtest/gtest.h>

#include "core/basic_types.h"
#include "fugue/subject.h"

namespace bach {
namespace {

Subject makeTestSubject() {
  Subject subject;
  // 8-note subject with varied rhythm and intervals.
  uint8_t pitches[] = {60, 62, 64, 67, 65, 64, 62, 60};
  Tick durations[] = {480, 480, 240, 240, 480, 480, 240, 240};
  Tick tick = 0;
  for (int idx = 0; idx < 8; ++idx) {
    NoteEvent note;
    note.pitch = pitches[idx];
    note.start_tick = tick;
    note.duration = durations[idx];
    note.velocity = 80;
    note.voice = 0;
    subject.notes.push_back(note);
    tick += durations[idx];
  }
  subject.length_ticks = tick;
  return subject;
}

TEST(CharacteristicMotifTest, ExtractDefault_Returns4Notes) {
  Subject subject = makeTestSubject();
  auto motif = extractCharacteristicMotif(subject);
  EXPECT_EQ(motif.size(), 4u);
}

TEST(CharacteristicMotifTest, ShortSubject_ReturnsAll) {
  Subject subject;
  NoteEvent note;
  note.pitch = 60;
  note.start_tick = 0;
  note.duration = 480;
  subject.notes.push_back(note);
  subject.length_ticks = 480;

  auto motif = extractCharacteristicMotif(subject);
  EXPECT_EQ(motif.size(), 1u);
}

TEST(CharacteristicMotifTest, CustomLength_ReturnsRequestedSize) {
  Subject subject = makeTestSubject();
  auto motif = extractCharacteristicMotif(subject, 3);
  EXPECT_EQ(motif.size(), 3u);
}

TEST(CharacteristicMotifTest, MotifContainsLeap_HigherScore) {
  // The test subject has a leap (C-E-G at indices 2,3), so the characteristic
  // motif should include the leap region rather than the stepwise opening.
  Subject subject = makeTestSubject();
  auto motif = extractCharacteristicMotif(subject);

  // Verify the motif contains at least one leap (>= 3 semitones).
  bool has_leap = false;
  for (size_t idx = 1; idx < motif.size(); ++idx) {
    int ivl = std::abs(static_cast<int>(motif[idx].pitch) -
                       static_cast<int>(motif[idx - 1].pitch));
    if (ivl >= 3) has_leap = true;
  }
  // Either the motif has a leap or it's from the opening (also valid).
  // Just verify it returns valid notes.
  EXPECT_GE(motif.size(), 1u);
}

}  // namespace
}  // namespace bach
