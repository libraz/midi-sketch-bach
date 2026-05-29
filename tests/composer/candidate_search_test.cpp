#include "composer/candidate_search.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

#include "composer/harmonic_plan.h"
#include "composer/material.h"
#include "composer/span.h"
#include "composer/voice_intent.h"
#include "core/basic_types.h"

namespace bach::composer {
namespace {

HarmonicPlan singleCMajor() {
  HarmonicPlan plan;
  plan.tonic_pc = 0;
  plan.is_minor = false;
  ChordEvent c;
  c.start_tick = 0;
  c.root_pc = 0;
  c.quality = ChordQuality::Major;
  plan.chords.push_back(c);
  return plan;
}

NoteEvent makePlaced(Tick start, Tick dur, std::uint8_t pitch, VoiceId voice) {
  NoteEvent n;
  n.start_tick = start;
  n.duration = dur;
  n.pitch = pitch;
  n.voice = voice;
  n.velocity = 80;
  return n;
}

Span makeComposeSpan(Tick start, Tick end, VoiceId voice) {
  Span s;
  s.id = 1;
  s.start_tick = start;
  s.end_tick = end;
  s.voice = voice;
  s.intent = VoiceIntent::SequentialCounterline;
  return s;
}

}  // namespace

// Without a vertical-check context, the search picks the highest-score
// admissible pitch near the voice center. With prev_pitch unset and no
// placed voice, G4 (pitch 55) — a C-major triad tone at the voice
// center — should win the strong-beat choice.
TEST(CandidateSearchTest, BaselinePicksTriadToneAtCenter) {
  CandidateSearch search;
  CandidateContext ctx;
  ctx.voice_center = 55;
  ctx.prev_pitch = 0;
  ctx.placed_notes = nullptr;

  Material empty;
  Span span = makeComposeSpan(0, kTicksPerBeat, 1);
  const auto cands = search.enumerate(span, singleCMajor(), empty, ctx);

  ASSERT_EQ(cands.size(), 1u);
  EXPECT_EQ(cands.front().pitch, 55u);
}

// Direct test of the vertical rule. Set up a placed voice 0 that moves
// C5 -> D5 across a beat. Run the search on voice 1 at the weak beat
// (tick = one beat) with prev_pitch = F4. Without the vertical check
// the best-scoring candidate would be G4 (55), which yields P5+P5
// against voice 0. The check must reject G4 and let a different pitch
// win.
TEST(CandidateSearchTest, RejectsParallelFifthAgainstPlacedVoice) {
  std::vector<NoteEvent> placed = {
      makePlaced(0, kTicksPerBeat, 60, 0),
      makePlaced(kTicksPerBeat, kTicksPerBeat, 62, 0),
  };

  CandidateContext ctx;
  ctx.voice_center = 55;
  ctx.prev_pitch = 53;                // F4
  ctx.prev_end_tick = kTicksPerBeat;  // anchors parallel_prev_tick to 0
  ctx.placed_notes = &placed;

  Material empty;
  // Span covers just one beat at the weak beat (tick = one quarter).
  Span span = makeComposeSpan(kTicksPerBeat, 2 * kTicksPerBeat, 1);

  CandidateSearch search;
  const auto cands = search.enumerate(span, singleCMajor(), empty, ctx);
  ASSERT_EQ(cands.size(), 1u);
  EXPECT_NE(cands.front().pitch, 55u)
      << "G4 forms a parallel 5th against the placed voice; it must be "
         "rejected by the vertical rule.";
}

// Same fixture as above but with placed_notes left null — the vertical
// rule is skipped and the score-winning candidate (G4) is committed.
// This pins down the "no placed view, no rejection" branch.
TEST(CandidateSearchTest, NoPlacedNotesSkipsVerticalCheck) {
  CandidateContext ctx;
  ctx.voice_center = 55;
  ctx.prev_pitch = 53;
  ctx.prev_end_tick = kTicksPerBeat;
  ctx.placed_notes = nullptr;

  Material empty;
  Span span = makeComposeSpan(kTicksPerBeat, 2 * kTicksPerBeat, 1);

  CandidateSearch search;
  const auto cands = search.enumerate(span, singleCMajor(), empty, ctx);
  ASSERT_EQ(cands.size(), 1u);
  EXPECT_EQ(cands.front().pitch, 55u);
}

// Holding the prev pitch is not parallel motion, even when the placed
// voice moves through a perfect interval to a new pitch that would
// otherwise look symptomatic. Use a fixture where the score-winning
// candidate IS the held pitch (center == prev == triad tone).
TEST(CandidateSearchTest, StationaryThisVoiceIsNotParallel) {
  // Placed voice 0: C5 -> G5 across the beat boundary. Voice 1 prev
  // pitch = C5 (60), center = 60. The pitch with highest score under
  // the (triad + center + prev) weighting is C5 itself, so voice 1
  // holds. Even though voice 0 moves 60->67 (P8 -> P5), voice 1 did
  // not move, so the parallel-perfect predicate must not fire.
  std::vector<NoteEvent> placed = {
      makePlaced(0, kTicksPerBeat, 60, 0),
      makePlaced(kTicksPerBeat, kTicksPerBeat, 67, 0),
  };

  CandidateContext ctx;
  ctx.voice_center = 60;
  ctx.prev_pitch = 60;
  ctx.prev_end_tick = kTicksPerBeat;
  ctx.placed_notes = &placed;

  Material empty;
  Span span = makeComposeSpan(kTicksPerBeat, 2 * kTicksPerBeat, 1);

  CandidateSearch search;
  const auto cands = search.enumerate(span, singleCMajor(), empty, ctx);
  ASSERT_EQ(cands.size(), 1u);
  EXPECT_EQ(cands.front().pitch, 60u);
}

// Voice 0 is placed at C4 (60). Voice 1's center is G4 (67) — the
// natural high-score pitch — but committing G4 would cross above
// voice 0. The vertical rule must reject every pitch above 60 so the
// search settles on a non-crossing alternative.
TEST(CandidateSearchTest, RejectsCandidateAbovePlacedHigherVoice) {
  std::vector<NoteEvent> placed = {
      makePlaced(0, kTicksPerBeat, 60, 0),  // C4 in voice 0
  };

  CandidateContext ctx;
  ctx.voice_center = 67;
  ctx.prev_pitch = 0;
  ctx.placed_notes = &placed;

  Material empty;
  Span span = makeComposeSpan(0, kTicksPerBeat, 1);

  CandidateSearch search;
  const auto cands = search.enumerate(span, singleCMajor(), empty, ctx);
  ASSERT_EQ(cands.size(), 1u);
  EXPECT_LE(cands.front().pitch, 60u) << "candidate must not cross above placed voice 0 (pitch 60)";
}

// Symmetric: voice 0 is the candidate; voice 1 is placed at G4 (67).
// Voice 0 must stay at or above 67 — anything below crosses.
TEST(CandidateSearchTest, RejectsCandidateBelowPlacedLowerVoice) {
  std::vector<NoteEvent> placed = {
      makePlaced(0, kTicksPerBeat, 67, 1),  // G4 in voice 1
  };

  CandidateContext ctx;
  ctx.voice_center = 60;  // would naturally prefer C4
  ctx.prev_pitch = 0;
  ctx.placed_notes = &placed;

  Material empty;
  Span span = makeComposeSpan(0, kTicksPerBeat, 0);

  CandidateSearch search;
  const auto cands = search.enumerate(span, singleCMajor(), empty, ctx);
  ASSERT_EQ(cands.size(), 1u);
  EXPECT_GE(cands.front().pitch, 67u) << "candidate must not cross below placed voice 1 (pitch 67)";
}

// Without a placed view, the search returns its score-winning pitch
// even when that pitch would have been rejected as a voice crossing.
// This pins down the null-context branch for the new rule.
TEST(CandidateSearchTest, NoPlacedNotesSkipsVoiceCrossingCheck) {
  CandidateContext ctx;
  ctx.voice_center = 67;
  ctx.prev_pitch = 0;
  ctx.placed_notes = nullptr;

  Material empty;
  Span span = makeComposeSpan(0, kTicksPerBeat, 1);

  CandidateSearch search;
  const auto cands = search.enumerate(span, singleCMajor(), empty, ctx);
  ASSERT_EQ(cands.size(), 1u);
  EXPECT_EQ(cands.front().pitch, 67u);
}

// Seed the context with pre_prev=53 and prev=60 (an upward P5 leap).
// The candidate at the next tick must not be another wide leap.
// Center is set to prev so step-distance triad tones (C5 stay, E5 via
// step) are inside the search range and the rule does not starve the
// candidate set.
TEST(CandidateSearchTest, RejectsLeapAfterLeap) {
  CandidateContext ctx;
  ctx.voice_center = 60;
  ctx.pre_prev_pitch = 53;  // F4
  ctx.prev_pitch = 60;      // C5 — leap of P5 from F4
  ctx.prev_end_tick = kTicksPerBeat;
  ctx.placed_notes = nullptr;

  Material empty;
  // Weak beat so the strong-beat triad gate does not interact.
  Span span = makeComposeSpan(kTicksPerBeat, 2 * kTicksPerBeat, 0);

  CandidateSearch search;
  const auto cands = search.enumerate(span, singleCMajor(), empty, ctx);
  ASSERT_EQ(cands.size(), 1u);
  const int delta = std::abs(static_cast<int>(cands.front().pitch) - 60);
  EXPECT_LT(delta, 7) << "candidate after a P5 leap must not extend the leap; got pitch "
                      << static_cast<int>(cands.front().pitch);
}

// With pre_prev unset (only one prior note), the leap-resolution rule
// must not fire. This pins down the "first leap is unconstrained"
// boundary case.
TEST(CandidateSearchTest, SingleLeapAloneIsUnconstrained) {
  CandidateContext ctx;
  ctx.voice_center = 72;
  ctx.pre_prev_pitch = 0;  // unavailable
  ctx.prev_pitch = 60;
  ctx.prev_end_tick = kTicksPerBeat;
  ctx.placed_notes = nullptr;

  Material empty;
  Span span = makeComposeSpan(kTicksPerBeat, 2 * kTicksPerBeat, 0);

  CandidateSearch search;
  const auto cands = search.enumerate(span, singleCMajor(), empty, ctx);
  ASSERT_EQ(cands.size(), 1u);
  // With center=72 and prev=60, the unconstrained score pulls toward
  // C5 (72). Confirm the search picks a pitch >= 64 (the search would
  // otherwise be hugging prev=60).
  EXPECT_GE(cands.front().pitch, 64u);
}

// Weak-beat non-triad candidate must be approached by step. Here
// prev=60 (C5, triad of C). For a candidate at the weak beat:
//   - Triad tones near prev are always allowed.
//   - Non-triad tones are allowed only when within 2 semis of prev.
// Set center far from prev so the search would naturally pull toward
// a non-triad tone outside step distance; the rule forces a triad
// pick instead.
TEST(CandidateSearchTest, RejectsLeapApproachedNonTriad) {
  CandidateContext ctx;
  ctx.voice_center = 75;  // pulls toward D#5/E5 area
  ctx.pre_prev_pitch = 0;
  ctx.prev_pitch = 60;  // C5
  ctx.prev_end_tick = kTicksPerBeat;
  ctx.placed_notes = nullptr;

  Material empty;
  Span span = makeComposeSpan(kTicksPerBeat, 2 * kTicksPerBeat, 0);

  CandidateSearch search;
  const auto cands = search.enumerate(span, singleCMajor(), empty, ctx);
  ASSERT_EQ(cands.size(), 1u);
  const std::uint8_t pc = static_cast<std::uint8_t>(cands.front().pitch % 12);
  const bool is_triad = pc == 0u || pc == 4u || pc == 7u;
  const int delta = std::abs(static_cast<int>(cands.front().pitch) - 60);
  EXPECT_TRUE(is_triad || delta <= 2)
      << "non-triad candidate must be within step distance of prev; "
         "got pitch "
      << static_cast<int>(cands.front().pitch);
}

// Voice 0 is placed at G5 (79). Voice 1's candidate at the strong
// beat must form a consonant interval with 79. With center 64, the
// score-winning triad pitch would be E4 (64) — but 79-64=15 (mod 12
// =3, m3) is consonant, so E4 should still be admitted. We then test
// with the subject set to a value where the natural pick would be
// dissonant.
TEST(CandidateSearchTest, AcceptsConsonantVerticalAtStrongBeat) {
  std::vector<NoteEvent> placed = {
      makePlaced(0, kTicksPerBeat, 79, 0),  // G5 in voice 0
  };

  CandidateContext ctx;
  ctx.voice_center = 64;
  ctx.prev_pitch = 0;
  ctx.placed_notes = &placed;

  Material empty;
  Span span = makeComposeSpan(0, kTicksPerBeat, 1);

  CandidateSearch search;
  const auto cands = search.enumerate(span, singleCMajor(), empty, ctx);
  ASSERT_EQ(cands.size(), 1u);
  // 79 - chosen pitch must be consonant. With center=64 and C-major
  // triad gate, the search will pick a C-triad tone whose interval
  // with 79 lands in the consonant set.
  const int interval = std::abs(static_cast<int>(cands.front().pitch) - 79);
  const int pc = interval % 12;
  const bool consonant = pc == 0 || pc == 3 || pc == 4 || pc == 5 || pc == 7 || pc == 8 || pc == 9;
  EXPECT_TRUE(consonant) << "candidate " << static_cast<int>(cands.front().pitch)
                         << " vs placed 79 is dissonant";
}

// Placed voice 0 at A4 (69). C-major triad tones near center=72 are
// 72 (C5) and 76 (E5). Intervals against 69:
//   72-69 = 3 (m3, consonant)
//   76-69 = 7 (P5, consonant)
//   67-69 = -2 (M2, dissonant)
//   64-69 = -5 (P4, consonant in this model)
// Pick must be consonant. We verify the search does not return a
// dissonant pick, regardless of which consonant alternative wins.
TEST(CandidateSearchTest, RejectsDissonantVerticalAtStrongBeat) {
  std::vector<NoteEvent> placed = {
      makePlaced(0, kTicksPerBeat, 69, 0),
  };

  CandidateContext ctx;
  ctx.voice_center = 67;  // would naturally pull toward G4
  ctx.prev_pitch = 0;
  ctx.placed_notes = &placed;

  Material empty;
  Span span = makeComposeSpan(0, kTicksPerBeat, 1);

  CandidateSearch search;
  const auto cands = search.enumerate(span, singleCMajor(), empty, ctx);
  ASSERT_EQ(cands.size(), 1u);
  const int interval = std::abs(static_cast<int>(cands.front().pitch) - 69);
  const int pc = interval % 12;
  const bool consonant = pc == 0 || pc == 3 || pc == 4 || pc == 5 || pc == 7 || pc == 8 || pc == 9;
  EXPECT_TRUE(consonant);
  EXPECT_NE(cands.front().pitch, 67u)
      << "G4 (67) forms M2 against placed A4 (69); rule must reject it.";
}

// Without placed_notes the strong-beat consonance gate is skipped.
TEST(CandidateSearchTest, NoPlacedNotesSkipsVerticalConsonance) {
  CandidateContext ctx;
  ctx.voice_center = 67;
  ctx.prev_pitch = 0;
  ctx.placed_notes = nullptr;

  Material empty;
  Span span = makeComposeSpan(0, kTicksPerBeat, 1);

  CandidateSearch search;
  const auto cands = search.enumerate(span, singleCMajor(), empty, ctx);
  ASSERT_EQ(cands.size(), 1u);
  EXPECT_EQ(cands.front().pitch, 67u);
}

// Weak-beat soft preference. Placed voice 0 sustains B4 (71) across the
// weak beat. Voice 1 has center=60, prev=60. Without the soft penalty
// the score-winner is C5 (60) — but 60 vs 71 is M7, dissonant. The
// 0.15 penalty should drop 60 below E4 (64), which is P5 against 71
// and consonant.
TEST(CandidateSearchTest, PrefersConsonantVerticalAtWeakBeat) {
  std::vector<NoteEvent> placed = {
      makePlaced(0, 2 * kTicksPerBeat, 71, 0),
  };

  CandidateContext ctx;
  ctx.voice_center = 60;
  ctx.prev_pitch = 60;
  ctx.prev_end_tick = kTicksPerBeat;
  ctx.placed_notes = &placed;

  Material empty;
  Span span = makeComposeSpan(kTicksPerBeat, 2 * kTicksPerBeat, 1);

  CandidateSearch search;
  const auto cands = search.enumerate(span, singleCMajor(), empty, ctx);
  ASSERT_EQ(cands.size(), 1u);
  EXPECT_NE(cands.front().pitch, 60u)
      << "C5 vs placed B4 is M7 (dissonant); the weak-beat soft penalty "
         "should shift the choice to a consonant pitch.";
  const int interval = std::abs(static_cast<int>(cands.front().pitch) - 71);
  const int pc = interval % 12;
  const bool consonant = pc == 0 || pc == 3 || pc == 4 || pc == 5 || pc == 7 || pc == 8 || pc == 9;
  EXPECT_TRUE(consonant) << "shifted pitch " << static_cast<int>(cands.front().pitch)
                         << " must be consonant with placed 71";
}

// Eighth-subdivision step bias. A two-position eighth span with
// prev=64 and center=64 would, under Quarter-mode scoring, lock both
// positions onto E4 (unison repetition: 64-64). With Subdivision::
// Eighth, the unison penalty pushes the second eighth off 64 and the
// non-triad step bonus lifts F4 (65, delta=1 from prev) over the
// available triad-tone leaps. The result is a stepwise pair, not a
// repeated pitch.
TEST(CandidateSearchTest, EighthSubdivisionBreaksUnisonAndPrefersStep) {
  CandidateContext ctx;
  ctx.voice_center = 64;
  ctx.prev_pitch = 64;  // seeded so the bias fires on the first 8th
  ctx.prev_end_tick = kTicksPerBeat / 2;
  ctx.placed_notes = nullptr;

  Material empty;
  // Span spans one quarter at eighth subdivision = two positions.
  Span span = makeComposeSpan(kTicksPerBeat, 2 * kTicksPerBeat, 1);
  span.subdivision = Subdivision::Eighth;

  CandidateSearch search;
  const auto cands = search.enumerate(span, singleCMajor(), empty, ctx);
  ASSERT_EQ(cands.size(), 2u);

  // The two eighths must not repeat the same pitch, and the second
  // eighth should land within a whole step of the first (step motion
  // beating triad leaps thanks to the non-triad step bonus).
  EXPECT_NE(cands[0].pitch, cands[1].pitch)
      << "eighth-subdivision span produced unison " << static_cast<int>(cands[0].pitch) << " -> "
      << static_cast<int>(cands[1].pitch);
  const int delta_within_span =
      std::abs(static_cast<int>(cands[1].pitch) - static_cast<int>(cands[0].pitch));
  EXPECT_LE(delta_within_span, 2)
      << "second eighth landed " << delta_within_span
      << " semis from the first; step preference should keep it within a whole step";
}

// Quarter subdivision remains unaffected: a single-position quarter
// span with prev == center == triad tone still picks the triad tone
// (no unison penalty fires, the original score weighting wins).
TEST(CandidateSearchTest, QuarterSubdivisionKeepsHeldPitch) {
  CandidateContext ctx;
  ctx.voice_center = 64;
  ctx.prev_pitch = 64;
  ctx.prev_end_tick = kTicksPerBeat;
  ctx.placed_notes = nullptr;

  Material empty;
  Span span = makeComposeSpan(kTicksPerBeat, 2 * kTicksPerBeat, 1);
  // Subdivision defaults to Quarter.

  CandidateSearch search;
  const auto cands = search.enumerate(span, singleCMajor(), empty, ctx);
  ASSERT_EQ(cands.size(), 1u);
  EXPECT_EQ(cands.front().pitch, 64u)
      << "Quarter subdivision must keep the original held-pitch behavior";
}

// Soft-penalty fallback. If no consonant candidate is reachable (the
// search range only contains pitches that form dissonance against the
// placed voice), the penalty does not turn into a hard reject — the
// least-penalised dissonant pitch should still be returned. We arrange
// for every C-major triad pitch in the search range to be dissonant
// against the placed voice so the search must accept some dissonance.
// Regression test: the composer commits one span at a time and appends
// each span's notes to placed_notes, so the resulting list is voice-
// grouped (V0's notes contiguous, then V1's, then V2's) rather than
// globally sorted by start_tick. Earlier versions of the vertical-rule
// helpers broke out of their iteration on the first `note.start_tick >
// cur_tick`, which meant once they hit a later V0 note, they never
// reached V1's entries — letting V1 dissonances slip past silently.
//
// Fixture: V2 candidate at strong beat tick 0.
//   V0 = [start=0, dur=480, p=72], [start=480, dur=480, p=74]
//   V1 = [start=0,   dur=480, p=65]
// Without the fix, V0's second entry (start=480 > 0) terminates the
// loop early and V1's F4 (65) is never checked. The search would then
// pick E4 (64) at center=64 — m2 against the hidden V1. With the fix,
// the loop continues past V0's later entries to V1's F4, rejects every
// candidate above V1 by voice-crossing and 64 by vertical dissonance,
// and the only surviving triad tone is C4 (60).
TEST(CandidateSearchTest, VerticalRuleSeesAllPlacedVoicesUnsorted) {
  std::vector<NoteEvent> placed = {
      makePlaced(0, kTicksPerBeat, 72, 0),
      makePlaced(kTicksPerBeat, kTicksPerBeat, 74, 0),
      makePlaced(0, kTicksPerBeat, 65, 1),
  };

  CandidateContext ctx;
  ctx.voice_center = 64;
  ctx.prev_pitch = 0;
  ctx.placed_notes = &placed;

  Material empty;
  Span span = makeComposeSpan(0, kTicksPerBeat, 2);

  CandidateSearch search;
  const auto cands = search.enumerate(span, singleCMajor(), empty, ctx);
  ASSERT_EQ(cands.size(), 1u);
  EXPECT_EQ(cands.front().pitch, 60u) << "search must see V1=65 even though a later V0 entry has "
                                         "start_tick > cur_tick; expected C4 (only consonant non-"
                                         "crossing triad), got "
                                      << static_cast<int>(cands.front().pitch);
}

TEST(CandidateSearchTest, WeakBeatPenaltyDoesNotHardReject) {
  // Placed voice 0 at D5 (62). Against this, the C-major triad pitch
  // classes (C/E/G) form: C-D=M2, E-D=M2, G-D=P4 (consonant). To
  // starve out the P4, narrow the search by setting center=61 and
  // prev=61 so the search range [54..73] still has triad tones but
  // the score-winning region falls into the dissonant set.
  std::vector<NoteEvent> placed = {
      makePlaced(0, 2 * kTicksPerBeat, 62, 0),
  };

  CandidateContext ctx;
  ctx.voice_center = 60;
  ctx.prev_pitch = 60;
  ctx.prev_end_tick = kTicksPerBeat;
  ctx.placed_notes = &placed;

  Material empty;
  Span span = makeComposeSpan(kTicksPerBeat, 2 * kTicksPerBeat, 1);

  CandidateSearch search;
  const auto cands = search.enumerate(span, singleCMajor(), empty, ctx);
  // We expect SOMETHING to be returned (no starvation). The point of
  // this test is that the soft penalty never wipes the candidate set
  // empty even when every pitch in range would be dissonant.
  ASSERT_EQ(cands.size(), 1u);
}

}  // namespace bach::composer
