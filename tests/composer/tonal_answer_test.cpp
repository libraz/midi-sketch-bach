#include "composer/tonal_answer.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

#include "composer/material.h"

namespace bach::composer::tonal_answer {
namespace {

MaterialNote mn(Tick start, std::uint8_t pitch) {
  MaterialNote n;
  n.start_tick = start;
  n.duration = kTicksPerBeat;
  n.pitch = pitch;
  return n;
}

}  // namespace

TEST(TonalAnswerTest, EmptySubjectReturnsEmpty) {
  const auto out = deriveTonalAnswer({}, 0, 0);
  EXPECT_TRUE(out.empty());
}

TEST(TonalAnswerTest, HeadMutatesTonicToDominant) {
  // C major, subject head opens on C4 (60) = tonic. Real answer would
  // give G3 (55, base = 60 - 5). Tonal answer mutates head to G3 too
  // (dominant pc). For tonic head, base already lands on dominant pc,
  // so the closest-pc helper returns base unchanged.
  std::vector<MaterialNote> subj = {mn(0, 60), mn(kTicksPerBeat, 64), mn(2 * kTicksPerBeat, 67)};
  const auto out = deriveTonalAnswer(subj, /*tonic_pc=*/0, /*target_start=*/0, /*head=*/3);
  ASSERT_EQ(out.size(), 3u);
  // Head note: subject pc 0 (tonic) → answer pc 7 (dominant).
  EXPECT_EQ(out[0].pitch % 12, 7);
  // Note index 2 in head: subject 67 (pc 7 = dominant) → answer pc 0 (tonic).
  EXPECT_EQ(out[2].pitch % 12, 0);
}

TEST(TonalAnswerTest, NonHeadFollowsRealAnswerTransposition) {
  // head_length=1 means only first note mutates; rest are subject - P4.
  std::vector<MaterialNote> subj = {mn(0, 60), mn(kTicksPerBeat, 64), mn(2 * kTicksPerBeat, 67)};
  const auto out = deriveTonalAnswer(subj, /*tonic_pc=*/0, /*target_start=*/0, /*head=*/1);
  ASSERT_EQ(out.size(), 3u);
  // Note 1 (E4 = 64): real answer = 64 - 5 = 59 (B3).
  EXPECT_EQ(out[1].pitch, 59);
  // Note 2 (G4 = 67): real answer = 67 - 5 = 62 (D4).
  EXPECT_EQ(out[2].pitch, 62);
}

TEST(TonalAnswerTest, ReanchorsAtTargetStartTick) {
  std::vector<MaterialNote> subj = {mn(0, 60), mn(kTicksPerBeat, 62)};
  const auto out = deriveTonalAnswer(subj, 0, 4 * kTicksPerBeat, 1);
  ASSERT_EQ(out.size(), 2u);
  EXPECT_EQ(out[0].start_tick, 4u * kTicksPerBeat);
  EXPECT_EQ(out[1].start_tick, 5u * kTicksPerBeat);
}

TEST(TonalAnswerTest, MiddleScaleDegreeNotMutated) {
  // Subject opens on E4 (pc 4 = mediant) — neither tonic nor dominant.
  // Even with head_length covering it, the head note must be real-answer
  // base (E4 - 5 = B3).
  std::vector<MaterialNote> subj = {mn(0, 64)};
  const auto out = deriveTonalAnswer(subj, 0, 0, 4);
  ASSERT_EQ(out.size(), 1u);
  EXPECT_EQ(out[0].pitch, 59);
}

TEST(TonalAnswerTest, ClosestPcOctaveKeepsHeadInRegister) {
  // Subject head dominant G5 (79). Real answer = 79 - 5 = 74 (D5).
  // Tonal answer mutates pc 7 → pc 0 (tonic). Closest pc-0 to D5 (74)
  // is C5 (72). Direct map without octave alignment could land on C4
  // (60), which would jar the answer register. Verify we land on C5.
  std::vector<MaterialNote> subj = {mn(0, 79)};
  const auto out = deriveTonalAnswer(subj, 0, 0, 1);
  ASSERT_EQ(out.size(), 1u);
  EXPECT_EQ(out[0].pitch, 72);
}

}  // namespace bach::composer::tonal_answer
