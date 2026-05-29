#include "composer/nct_detector.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

#include "composer/harmonic_plan.h"
#include "composer/material.h"

namespace bach::composer::nct_detector {
namespace {

HarmonicPlan cMajorWhole() {
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

MaterialNote mn(Tick start, std::uint8_t pitch) {
  MaterialNote n;
  n.start_tick = start;
  n.duration = kTicksPerBeat;
  n.pitch = pitch;
  return n;
}

}  // namespace

TEST(NctDetectorTest, CambiataDetectsClassicBachShape) {
  // C major triad {C, E, G}. Cambiata example: E5 → D5 → B4 → C5 → D5
  // No — let me reconstruct: chord → step-down NCT → leap-down → step-up
  // → step-up chord-tone. Use C(60) → B4(59) NCT → G4(55) leap-down →
  // A4(57) step-up → B4(59) step-up... but B4 is NOT a chord tone of C.
  // Need a closing chord-tone. Try E5(64) → D5(62) → A4(57) → B4(59) →
  // C5(60): n0=E5 chord, n1=D5 NCT step-down (2 semis), n2=A4 leap-down
  // (5 semis from D5), n3=B4 step-up (2 semis), n4=C5 chord-tone
  // step-up (1 semi). ✓
  std::vector<MaterialNote> notes = {
      mn(0, 64),                  // E5 chord
      mn(kTicksPerBeat, 62),      // D5 NCT step-down
      mn(kTicksPerBeat * 2, 57),  // A4 leap-down
      mn(kTicksPerBeat * 3, 59),  // B4 step-up
      mn(kTicksPerBeat * 4, 60),  // C5 chord step-up
  };
  auto hits = detectCambiata(notes, cMajorWhole());
  ASSERT_EQ(hits.size(), 1u);
  EXPECT_EQ(hits[0].nct_index, 1u);
}

TEST(NctDetectorTest, CambiataRejectsAllChordToneSequence) {
  // C, E, G, C, E — all chord tones, no NCT.
  std::vector<MaterialNote> notes = {
      mn(0, 60),                  // C5
      mn(kTicksPerBeat, 64),      // E5
      mn(kTicksPerBeat * 2, 67),  // G5
      mn(kTicksPerBeat * 3, 60),  // C5
      mn(kTicksPerBeat * 4, 64),  // E5
  };
  auto hits = detectCambiata(notes, cMajorWhole());
  EXPECT_TRUE(hits.empty());
}

TEST(NctDetectorTest, EchappeeDetectsStepUpThenLeapDown) {
  // C5 → D5 NCT step-up → G4 chord leap-down.
  std::vector<MaterialNote> notes = {
      mn(0, 60),                  // C5 chord
      mn(kTicksPerBeat, 62),      // D5 NCT step-up
      mn(kTicksPerBeat * 2, 55),  // G4 chord leap-down (7 semis)
  };
  auto hits = detectEchappee(notes, cMajorWhole());
  ASSERT_EQ(hits.size(), 1u);
  EXPECT_EQ(hits[0].nct_index, 1u);
}

TEST(NctDetectorTest, AnticipationDetectsStepDownToNextChordTone) {
  // C5 chord → B4 NCT (step-down, not in C-triad) → B4 stays as the
  // next chord-tone of a G-major chord active at t=2. The detector
  // checks that n2 is a chord-tone at its own onset; we shift to G
  // major mid-stream.
  HarmonicPlan plan = cMajorWhole();
  ChordEvent g;
  g.start_tick = kTicksPerBeat * 2;
  g.root_pc = 7;
  g.quality = ChordQuality::Major;
  plan.chords.push_back(g);

  std::vector<MaterialNote> notes = {
      mn(0, 60),                  // C5 chord-tone of C major
      mn(kTicksPerBeat, 59),      // B4 step-down NCT vs C major
      mn(kTicksPerBeat * 2, 59),  // B4 still — chord-tone of G major (third)
  };
  auto hits = detectAnticipation(notes, plan);
  ASSERT_EQ(hits.size(), 1u);
  EXPECT_EQ(hits[0].nct_index, 1u);
}

TEST(NctDetectorTest, NotaCambiataDetectsCompressedFourNoteShape) {
  // E5 chord → D5 NCT step-down → A4 NCT leap-down (5 semis) → B4
  // chord-tone? B4 is NOT in C-triad. Use closing C5 (chord tone).
  // E5 → D5 (step) → A4 (leap-down 5) → ? need step-up to chord-tone.
  // A4 → B4 (step-up 2) — but B4 isn't chord. Try E5 → D5 → A4 → G4
  // chord step-down (no, NotaCambiata requires step-UP closing).
  // Use E5(64) → D5(62) → A4(57) → C5(60)? Step-up from A4 to C5 is
  // 3 semis = leap, not step. Need step. Use E5 → D5 → A4 → B4? B4
  // not chord. The closing chord-tone must be a step from the leap-NCT.
  // Let me restructure: E5(64) → D5(62) NCT → A4(57) NCT → G4(55) is
  // a step-down so wrong direction. Try a higher leap landing point:
  // E5(64) → D5(62) → A4(57) → G4(55) is wrong direction. Let's flip:
  // G5(67) → F5(65) NCT → C5(60) leap-down NCT → D5(62)? D5 is NCT in
  // C-major triad. So closing isn't chord. Use G5(67) → F5(65) → C5(60)
  // → B4(59) — both C5 and B4 not chord... actually C5 IS in C major
  // triad (root). And step from C5 to B4 is down, not up.
  //
  // Bach's compressed cambiata commonly goes: tonic → leading-step-down
  // → leap-down a third → chord-tone-step-up. C major: C5 → B4 → G4 → A4?
  // A4 not chord. Try: E5 → D5 → A4 → G4 closing — G4 IS chord (fifth).
  // But A4→G4 is step-DOWN. The detector requires step-UP at close.
  //
  // Use D minor segment instead? Stay in C major. Try ascending: G4 →
  // A4 (step-up NCT) → D5 (leap-up 5 semis NCT) → E5 chord step-up.
  // But Nota cambiata is descending shape. Let me use a fresh window:
  //
  // E5 → D5 → A4 → ... need step-up from A4 to a chord. A4 → B4 is step
  // but B4 not chord. A4 → C5 is 3 semis = leap. So no closing.
  //
  // Reformulate: G5 → F5 NCT → B4 NCT (leap 6 semis) → C5 chord (step-up).
  // G5(67) → F5(65) step (2) → B4(59) leap-down (6) → C5(60) step-up (1).
  // F5 NCT? F is in C major scale but NOT in C-triad. Confirmed NCT.
  // B4 NCT? B in C major scale, NOT in C-triad. Confirmed.
  // C5 closing chord. ✓
  std::vector<MaterialNote> notes = {
      mn(0, 67),                  // G5 chord (P5 of C)
      mn(kTicksPerBeat, 65),      // F5 NCT step-down (2 semis)
      mn(kTicksPerBeat * 2, 59),  // B4 NCT leap-down (6 semis)
      mn(kTicksPerBeat * 3, 60),  // C5 chord step-up (1 semi)
  };
  auto hits = detectNotaCambiata(notes, cMajorWhole());
  ASSERT_EQ(hits.size(), 1u);
  EXPECT_EQ(hits[0].nct_index, 1u);
}

TEST(NctDetectorTest, NotaCambiataRejectsLeapClosingInsteadOfStep) {
  // G5 → F5 → B4 → A4 (leap-down at close, not step-up). Reject.
  std::vector<MaterialNote> notes = {
      mn(0, 67), mn(kTicksPerBeat, 65), mn(kTicksPerBeat * 2, 59),
      mn(kTicksPerBeat * 3, 57),  // A4 — step-DOWN from B4, wrong direction
  };
  auto hits = detectNotaCambiata(notes, cMajorWhole());
  EXPECT_TRUE(hits.empty());
}

TEST(NctDetectorTest, EmptyHarmonicPlanReturnsNoHits) {
  HarmonicPlan empty_plan;
  std::vector<MaterialNote> notes = {mn(0, 60), mn(kTicksPerBeat, 62)};
  EXPECT_TRUE(detectCambiata(notes, empty_plan).empty());
  EXPECT_TRUE(detectEchappee(notes, empty_plan).empty());
  EXPECT_TRUE(detectAnticipation(notes, empty_plan).empty());
  EXPECT_TRUE(detectNotaCambiata(notes, empty_plan).empty());
}

}  // namespace bach::composer::nct_detector
