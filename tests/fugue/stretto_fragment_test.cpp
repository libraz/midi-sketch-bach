#include "fugue/stretto.h"

#include <gtest/gtest.h>

#include "core/basic_types.h"
#include "fugue/subject.h"

namespace bach {
namespace {

Subject makeSubject8Notes() {
  Subject subject;
  for (int idx = 0; idx < 8; ++idx) {
    NoteEvent note;
    note.pitch = static_cast<uint8_t>(60 + idx);
    note.start_tick = static_cast<Tick>(idx * kTicksPerBeat);
    note.duration = kTicksPerBeat;
    note.velocity = 80;
    note.voice = 0;
    subject.notes.push_back(note);
  }
  subject.length_ticks = kTicksPerBeat * 8;
  return subject;
}

TEST(StrettoFragmentTest, HalfFragment_Returns4Notes) {
  Subject subject = makeSubject8Notes();
  auto fragment = createStrettoFragment(subject, 0.5f);
  EXPECT_EQ(fragment.size(), 4u);
}

TEST(StrettoFragmentTest, FullRatio_ReturnsAllNotes) {
  Subject subject = makeSubject8Notes();
  auto fragment = createStrettoFragment(subject, 1.0f);
  EXPECT_EQ(fragment.size(), 8u);
}

TEST(StrettoFragmentTest, SmallRatio_ReturnsAtLeastOne) {
  Subject subject = makeSubject8Notes();
  auto fragment = createStrettoFragment(subject, 0.05f);
  EXPECT_GE(fragment.size(), 1u);
}

TEST(StrettoFragmentTest, EmptySubject_ReturnsEmpty) {
  Subject subject;
  auto fragment = createStrettoFragment(subject, 0.5f);
  EXPECT_TRUE(fragment.empty());
}

TEST(StrettoFragmentTest, FragmentPreservesPitches) {
  Subject subject = makeSubject8Notes();
  auto fragment = createStrettoFragment(subject, 0.5f);

  ASSERT_EQ(fragment.size(), 4u);
  for (size_t idx = 0; idx < fragment.size(); ++idx) {
    EXPECT_EQ(fragment[idx].pitch, subject.notes[idx].pitch);
  }
}

TEST(StrettoFragmentTest, RatioClamped_BelowMinimum) {
  Subject subject = makeSubject8Notes();
  auto fragment = createStrettoFragment(subject, -1.0f);  // Should clamp to 0.1
  EXPECT_GE(fragment.size(), 1u);
}

}  // namespace
}  // namespace bach
