// Tests for voice register ranges and fitToRegister.

#include "fugue/voice_registers.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

#include "core/basic_types.h"

namespace bach {
namespace {

// ---------------------------------------------------------------------------
// getFugueVoiceRange tests (existing)
// ---------------------------------------------------------------------------

TEST(VoiceRegistersTest, ThreeVoiceRanges) {
  auto [lo0, hi0] = getFugueVoiceRange(0, 3);
  EXPECT_EQ(lo0, 60u);
  EXPECT_EQ(hi0, 96u);

  auto [lo1, hi1] = getFugueVoiceRange(1, 3);
  EXPECT_EQ(lo1, 55u);
  EXPECT_EQ(hi1, 79u);

  auto [lo2, hi2] = getFugueVoiceRange(2, 3);
  EXPECT_EQ(lo2, 48u);
  EXPECT_EQ(hi2, 72u);
}

TEST(VoiceRegistersTest, TwoVoiceRanges) {
  auto [lo0, hi0] = getFugueVoiceRange(0, 2);
  EXPECT_EQ(lo0, 55u);
  EXPECT_EQ(hi0, 84u);

  auto [lo1, hi1] = getFugueVoiceRange(1, 2);
  EXPECT_EQ(lo1, 36u);
  EXPECT_EQ(hi1, 67u);
}

TEST(VoiceRegistersTest, FourVoiceRanges) {
  auto [lo0, hi0] = getFugueVoiceRange(0, 4);
  EXPECT_EQ(lo0, 60u);
  EXPECT_EQ(hi0, 96u);

  auto [lo1, hi1] = getFugueVoiceRange(1, 4);
  EXPECT_EQ(lo1, 55u);
  EXPECT_EQ(hi1, 79u);

  auto [lo2, hi2] = getFugueVoiceRange(2, 4);
  EXPECT_EQ(lo2, 48u);
  EXPECT_EQ(hi2, 72u);

  auto [lo3, hi3] = getFugueVoiceRange(3, 4);
  EXPECT_EQ(lo3, 24u);
  EXPECT_EQ(hi3, 50u);
}

TEST(VoiceRegistersTest, FiveVoiceUseFourVoiceRanges) {
  // Voice 4 should clamp to voice 3 ranges (bass).
  auto [lo4, hi4] = getFugueVoiceRange(4, 5);
  EXPECT_EQ(lo4, 24u);
  EXPECT_EQ(hi4, 50u);
}

TEST(VoiceRegistersTest, VoiceRangesNonOverlapping) {
  // For 3 voices, ranges should have minimal overlap.
  auto [lo0, hi0] = getFugueVoiceRange(0, 3);
  auto [lo1, hi1] = getFugueVoiceRange(1, 3);
  auto [lo2, hi2] = getFugueVoiceRange(2, 3);

  // Upper voice should be higher than lower voice ranges.
  EXPECT_GT(lo0, lo1);
  EXPECT_GT(lo1, lo2);
}

// ---------------------------------------------------------------------------
// VoiceRegister struct tests
// ---------------------------------------------------------------------------

TEST(VoiceRegisterTest, StructInitialization) {
  VoiceRegister reg{48, 72};
  EXPECT_EQ(reg.low, 48u);
  EXPECT_EQ(reg.high, 72u);
}

// ---------------------------------------------------------------------------
// Helper: build a pitch vector from initializer_list.
// ---------------------------------------------------------------------------

std::vector<uint8_t> makePitches(std::initializer_list<uint8_t> vals) {
  return std::vector<uint8_t>(vals);
}

// ---------------------------------------------------------------------------
// fitToRegister: basic placement tests
// ---------------------------------------------------------------------------

TEST(FitToRegisterTest, PerfectFitShiftZero) {
  // Pitches already in range [60, 96] -- shift=0 is optimal.
  auto pts = makePitches({72, 74, 76});
  int shift = fitToRegister(pts.data(), pts.size(), 60, 96);
  EXPECT_EQ(shift, 0);
}

TEST(FitToRegisterTest, PerfectFitShiftDown) {
  // Pitches at 84,86,88 -- range [36,60]. Need shift to place inside.
  // shift=-36: [48,52] -- fits with no overflow.
  auto pts = makePitches({84, 86, 88});
  int shift = fitToRegister(pts.data(), pts.size(), 36, 60);
  EXPECT_EQ(shift, -36);
}

TEST(FitToRegisterTest, ImperfectFitMinimalOverflow) {
  // Wide melody [30,90] -- no single shift fits in [48,72].
  // shift=0 and shift=12 have same overflow (36 each), but shift=12 wins
  // because it reduces the clarity penalty (distance from characteristic range).
  auto pts = makePitches({30, 60, 90});
  int shift = fitToRegister(pts.data(), pts.size(), 48, 72);
  EXPECT_EQ(shift, 12);
}

TEST(FitToRegisterTest, ShiftUpIntoRange) {
  // Low pitches [24,28,31] with soprano range [60,96].
  // shift=+36: [60,67] overflow=0, center_dist=15.
  // shift=+48: [72,79] overflow=0, center_dist=3 -- closer to voice center.
  // Both fit with zero overflow; shift=48 wins on center_distance.
  auto pts = makePitches({24, 28, 31});
  int shift = fitToRegister(pts.data(), pts.size(), 60, 96);
  EXPECT_EQ(shift, 48);
}

// ---------------------------------------------------------------------------
// fitToRegister: reference pitch (melodic distance)
// ---------------------------------------------------------------------------

TEST(FitToRegisterTest, ReferencePitchSelectsCloserShift) {
  // Two valid shifts: shift=0 (first=60) and shift=-12 (first=48).
  // Range [48,84] accommodates both. reference_pitch=50 favors shift=-12 (dist=2).
  auto pts = makePitches({60, 64, 67});
  int shift = fitToRegister(pts.data(), pts.size(), 48, 84, /*ref=*/50);
  EXPECT_EQ(shift, -12);
}

TEST(FitToRegisterTest, ReferencePitchNearZeroShift) {
  // reference_pitch=65 should prefer shift=0 (first=60, dist=5)
  // over shift=12 (first=72, dist=7) or shift=-12 (first=48, dist=17).
  auto pts = makePitches({60, 64, 67});
  int shift = fitToRegister(pts.data(), pts.size(), 48, 84, /*ref=*/65);
  EXPECT_EQ(shift, 0);
}

// ---------------------------------------------------------------------------
// fitToRegister: parallel risk
// ---------------------------------------------------------------------------

TEST(FitToRegisterTest, ParallelRiskAvoidance) {
  // Adjacent voice moved from 55->62 (up 7).
  // Pitches [55,62]: with shift=0, entry_motion=+7, same direction.
  // compoundToSimple(|62-62|) = 0 (P1) -- parallel risk triggers.
  // shift=0 gets 20-point penalty. Another shift avoids it.
  auto pts = makePitches({55, 62});
  int shift = fitToRegister(pts.data(), pts.size(), 48, 84,
                            /*ref=*/0, /*prev_ref=*/0,
                            /*adj_last=*/62, /*adj_prev=*/55,
                            /*adj_lo=*/48, /*adj_hi=*/84);
  EXPECT_NE(shift, 0);
}

TEST(FitToRegisterTest, ParallelRiskContraryMotionAllowed) {
  // Adjacent moves up (55->62), entry moves down (67->60) -- contrary motion.
  // No parallel risk, so shift=0 is fine.
  auto pts = makePitches({67, 60});
  int shift = fitToRegister(pts.data(), pts.size(), 48, 84,
                            /*ref=*/0, /*prev_ref=*/0,
                            /*adj_last=*/62, /*adj_prev=*/55,
                            /*adj_lo=*/48, /*adj_hi=*/84);
  EXPECT_EQ(shift, 0);
}

TEST(FitToRegisterTest, ParallelRiskObliqueMotionAllowed) {
  // Adjacent upper voice stationary (68->68), entry moves -- oblique motion.
  // signOf(0) == 0 never matches, so no parallel penalty.
  // This voice range [48,72], adjacent is upper [60,96].
  // shift=0: first=60. adj_lo(60) > range_lo(48) -> upper voice.
  // 60 < adj_last(68) -> no crossing. No parallel risk. No order violation.
  auto pts = makePitches({60, 64});
  int shift_oblique = fitToRegister(pts.data(), pts.size(), 48, 72,
                                    /*ref=*/0, /*prev_ref=*/0,
                                    /*adj_last=*/68, /*adj_prev=*/68,
                                    /*adj_lo=*/60, /*adj_hi=*/96);
  EXPECT_EQ(shift_oblique, 0);
}

// ---------------------------------------------------------------------------
// fitToRegister: instant cross
// ---------------------------------------------------------------------------

TEST(FitToRegisterTest, InstantCrossAvoidance) {
  // Adjacent upper voice (adj_lo=60, adj_hi=96) has adj_last=65.
  // This voice range [48,72]. shift=0: first=70 > adj_last=65 -- crossing.
  auto pts = makePitches({70, 68, 65});
  int shift = fitToRegister(pts.data(), pts.size(), 48, 72,
                            /*ref=*/0, /*prev_ref=*/0,
                            /*adj_last=*/65, /*adj_prev=*/0,
                            /*adj_lo=*/60, /*adj_hi=*/96);
  // shift=-12: first=58, no crossing.
  EXPECT_EQ(shift, -12);
}

TEST(FitToRegisterTest, InstantCrossSubjectReducedWeight) {
  // Same setup as above but is_subject_voice=true -- cross_weight=15 instead of 50.
  auto pts = makePitches({70, 68, 65});
  int shift = fitToRegister(pts.data(), pts.size(), 48, 72,
                            /*ref=*/0, /*prev_ref=*/0,
                            /*adj_last=*/65, /*adj_prev=*/0,
                            /*adj_lo=*/60, /*adj_hi=*/96,
                            /*is_subject=*/true);
  // Result must be a multiple of 12 regardless.
  EXPECT_EQ(shift % 12, 0);
}

TEST(FitToRegisterTest, InstantCrossLowerVoice) {
  // Adjacent is lower voice (adj_lo=36, adj_hi=60) with adj_last=55.
  // This voice range [48,72]. shift=0: first=50 < adj_last=55 -- crossing lower.
  auto pts = makePitches({50, 55, 60});
  int shift = fitToRegister(pts.data(), pts.size(), 48, 72,
                            /*ref=*/0, /*prev_ref=*/0,
                            /*adj_last=*/55, /*adj_prev=*/0,
                            /*adj_lo=*/36, /*adj_hi=*/60);
  // shift=12: first=62, safely above 55.
  EXPECT_EQ(shift, 12);
}

// ---------------------------------------------------------------------------
// fitToRegister: order violation
// ---------------------------------------------------------------------------

TEST(FitToRegisterTest, OrderViolationPenalty) {
  // This voice range [48,72], adjacent range [60,96] (upper voice).
  // adj_last=70. Pitches [75,73] -- both above 70 at shift=0 -- order violation.
  auto pts = makePitches({75, 73});
  int shift = fitToRegister(pts.data(), pts.size(), 48, 72,
                            /*ref=*/0, /*prev_ref=*/0,
                            /*adj_last=*/70, /*adj_prev=*/0,
                            /*adj_lo=*/60, /*adj_hi=*/96);
  // shift=-12: first=63, second=61. Both below 70. No violation.
  EXPECT_EQ(shift, -12);
}

TEST(FitToRegisterTest, OrderViolationFirstOnlyRecoveryAllowed) {
  // First note above adj_last but second returns below -- no sustained violation.
  auto pts = makePitches({75, 60});
  int shift = fitToRegister(pts.data(), pts.size(), 48, 72,
                            /*ref=*/0, /*prev_ref=*/0,
                            /*adj_last=*/70, /*adj_prev=*/0,
                            /*adj_lo=*/60, /*adj_hi=*/96);
  // The crossing penalty still applies but order_violation does not.
  EXPECT_EQ(shift % 12, 0);
}

// ---------------------------------------------------------------------------
// fitToRegister: register drift (exposition only)
// ---------------------------------------------------------------------------

TEST(FitToRegisterTest, RegisterDriftExpositionPenalty) {
  // Exposition with last_subject_pitch=72.
  // shift=0: first=60, drift=12 -- no penalty (<=12 threshold).
  // shift=-12: first=48, drift=24 -- register_drift=12 -- penalized.
  auto pts = makePitches({60, 64, 67});
  int shift = fitToRegister(pts.data(), pts.size(), 36, 84,
                            /*ref=*/0, /*prev_ref=*/0,
                            /*adj_last=*/0, /*adj_prev=*/0,
                            /*adj_lo=*/0, /*adj_hi=*/0,
                            /*is_subject=*/false,
                            /*last_subj=*/72,
                            /*is_expo=*/true);
  EXPECT_EQ(shift, 0);
}

TEST(FitToRegisterTest, RegisterDriftNotAppliedOutsideExposition) {
  // Same parameters but is_exposition=false -- drift penalty not applied.
  auto pts = makePitches({60, 64, 67});
  int shift = fitToRegister(pts.data(), pts.size(), 36, 84,
                            /*ref=*/0, /*prev_ref=*/0,
                            /*adj_last=*/0, /*adj_prev=*/0,
                            /*adj_lo=*/0, /*adj_hi=*/0,
                            /*is_subject=*/false,
                            /*last_subj=*/72,
                            /*is_expo=*/false);
  EXPECT_EQ(shift, 0);
}

// ---------------------------------------------------------------------------
// fitToRegister: clarity penalty (characteristic range)
// ---------------------------------------------------------------------------

TEST(FitToRegisterTest, ClarityPenaltyKeepsInCharacteristicRange) {
  // Voice range [48,72] -> tenor characteristic range [48,67].
  // Pitches [64,66] with shift=0: first=64, in char range. clarity=0.
  // shift=12: first=76, outside char range by 76-67=9. clarity penalty=18.
  auto pts = makePitches({64, 66});
  int shift = fitToRegister(pts.data(), pts.size(), 48, 72);
  EXPECT_EQ(shift, 0);
}

// ---------------------------------------------------------------------------
// fitToRegister: center distance tiebreaker
// ---------------------------------------------------------------------------

TEST(FitToRegisterTest, CenterDistanceTiebreaker) {
  // When all other factors are equal, prefer shift closer to voice center.
  // Range [48,72], center=60. Pitches [60] at shift=0 is exactly centered.
  auto pts = makePitches({60});
  int shift = fitToRegister(pts.data(), pts.size(), 48, 72);
  EXPECT_EQ(shift, 0);
}

// ---------------------------------------------------------------------------
// fitToRegister: pedal voice
// ---------------------------------------------------------------------------

TEST(FitToRegisterTest, PedalVoiceNarrowRange) {
  // Pedal: range [24,50]. Pitches already inside.
  auto pts = makePitches({36, 40, 43});
  int shift = fitToRegister(pts.data(), pts.size(), 24, 50);
  EXPECT_EQ(shift, 0);
}

TEST(FitToRegisterTest, PedalVoiceNeedsShiftDown) {
  // Pitches [72,76,79] far above pedal range [24,50].
  // shift=-36: [36,43] -- fits. shift=-24: [48,55] -- overflow by 5.
  auto pts = makePitches({72, 76, 79});
  int shift = fitToRegister(pts.data(), pts.size(), 24, 50);
  EXPECT_EQ(shift, -36);
}

// ---------------------------------------------------------------------------
// fitToRegister: edge cases
// ---------------------------------------------------------------------------

TEST(FitToRegisterTest, EmptyPitchesReturnsZero) {
  int shift = fitToRegister(static_cast<const uint8_t*>(nullptr), 0, 48, 72);
  EXPECT_EQ(shift, 0);
}

TEST(FitToRegisterTest, SingleNote) {
  auto pts = makePitches({60});
  int shift = fitToRegister(pts.data(), pts.size(), 48, 72);
  EXPECT_EQ(shift, 0);
}

TEST(FitToRegisterTest, SingleNoteNeedsShift) {
  // Single note at 96, range [48,72]. shift=-36: 60 (centered). shift=-24: 72 (edge).
  auto pts = makePitches({96});
  int shift = fitToRegister(pts.data(), pts.size(), 48, 72);
  EXPECT_EQ(shift, -36);
}

TEST(FitToRegisterTest, AlwaysReturnsMultipleOf12) {
  // Test various configurations to ensure the result is always a multiple of 12.
  auto pts = makePitches({63, 67, 70, 74, 77});
  for (uint8_t lo : {24, 36, 48, 55, 60}) {
    for (uint8_t hi : {50, 60, 72, 79, 96}) {
      if (hi <= lo) continue;
      int shift = fitToRegister(pts.data(), pts.size(), lo, hi);
      EXPECT_EQ(shift % 12, 0) << "lo=" << int(lo) << " hi=" << int(hi);
    }
  }
}

TEST(FitToRegisterTest, SmallerAbsoluteShiftWinsOnTie) {
  // Symmetric situation: shift=+12 and shift=-12 produce identical scores.
  // shift=0 should be preferred over both (smallest absolute shift).
  auto pts = makePitches({60});
  int shift = fitToRegister(pts.data(), pts.size(), 48, 72);
  EXPECT_EQ(shift, 0);
}

// ---------------------------------------------------------------------------
// fitToRegister: NoteEvent vector overload
// ---------------------------------------------------------------------------

TEST(FitToRegisterTest, NoteEventOverload) {
  std::vector<NoteEvent> notes;
  NoteEvent note;
  note.pitch = 72;
  note.start_tick = 0;
  note.duration = 480;
  note.velocity = 80;
  note.voice = 0;
  notes.push_back(note);
  note.pitch = 74;
  note.start_tick = 480;
  notes.push_back(note);

  int shift = fitToRegister(notes, 60, 96);
  EXPECT_EQ(shift, 0);
}

TEST(FitToRegisterTest, NoteEventOverloadNeedsShift) {
  std::vector<NoteEvent> notes;
  NoteEvent note;
  note.pitch = 84;
  note.start_tick = 0;
  note.duration = 480;
  note.velocity = 80;
  note.voice = 0;
  notes.push_back(note);
  note.pitch = 88;
  note.start_tick = 480;
  notes.push_back(note);

  int shift = fitToRegister(notes, 36, 60);
  EXPECT_EQ(shift, -36);
}

TEST(FitToRegisterTest, NoteEventOverloadEmpty) {
  std::vector<NoteEvent> notes;
  int shift = fitToRegister(notes, 48, 72);
  EXPECT_EQ(shift, 0);
}

TEST(FitToRegisterTest, NoteEventOverloadWithReference) {
  std::vector<NoteEvent> notes;
  NoteEvent note;
  note.pitch = 60;
  note.start_tick = 0;
  note.duration = 480;
  note.velocity = 80;
  note.voice = 0;
  notes.push_back(note);
  note.pitch = 64;
  note.start_tick = 480;
  notes.push_back(note);

  // reference_pitch=50 should pull shift toward -12.
  int shift = fitToRegister(notes, 48, 84, /*ref=*/50);
  EXPECT_EQ(shift, -12);
}

// ---------------------------------------------------------------------------
// fitToRegister: combined criteria
// ---------------------------------------------------------------------------

TEST(FitToRegisterTest, OverflowDominatesAllOtherFactors) {
  // Overflow has weight 100 -- it should dominate even if all other factors
  // favor a different shift.
  // Pitches [60,64] with range [60,65]. shift=0: overflow=0. shift=12: overflow=11.
  // Even with reference_pitch far from shift=0, overflow wins.
  auto pts = makePitches({60, 64});
  int shift = fitToRegister(pts.data(), pts.size(), 60, 65, /*ref=*/120);
  EXPECT_EQ(shift, 0);
}

TEST(FitToRegisterTest, CrossPenaltyDominatesLowerWeights) {
  // instant_cross (w=50) should override melodic_distance (w=10)
  // when both compete but overflow is equal.
  // Range [48,84], adjacent upper at adj_lo=60.
  // Pitches [70]: shift=0, first=70. adj_last=65, adj_lo=60 (upper).
  // instant_cross = 70-65 = 5. 50*5=250.
  // reference_pitch=70 favors shift=0 (dist=0, w=10 -> 0).
  // shift=-12: first=58, no crossing. melodic_dist=|58-70|=12. 10*12=120.
  // 250 vs 120 -- shift=-12 wins despite melodic distance.
  auto pts = makePitches({70});
  int shift = fitToRegister(pts.data(), pts.size(), 48, 84,
                            /*ref=*/70, /*prev_ref=*/0,
                            /*adj_last=*/65, /*adj_prev=*/0,
                            /*adj_lo=*/60, /*adj_hi=*/96);
  EXPECT_EQ(shift, -12);
}

TEST(FitToRegisterTest, SopranoVoiceCharacteristicRange) {
  // Range [60,96] -> soprano characteristic range [60,84].
  // Pitches [90] at shift=0: clarity = 90-84 = 6, penalty = 12.
  // shift=-12: first=78, in char range. center_dist smaller too.
  auto pts = makePitches({90});
  int shift = fitToRegister(pts.data(), pts.size(), 60, 96);
  EXPECT_EQ(shift, -12);
}

}  // namespace
}  // namespace bach
