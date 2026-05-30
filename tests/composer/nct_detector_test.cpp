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

// One eighth-note material figure note at an absolute tick.
MaterialNote fig(Tick start, std::uint8_t pitch) {
  MaterialNote n;
  n.start_tick = start;
  n.duration = kTicksPerBeat / 2;
  n.pitch = pitch;
  return n;
}

ChordEvent chordAt(Tick tick, std::uint8_t root_pc, ChordQuality quality) {
  ChordEvent c;
  c.start_tick = tick;
  c.root_pc = root_pc;
  c.quality = quality;
  return c;
}

// The exact bar-12..15 modulation context the Phase14 fixture authors the
// four NCT figures against: V/V (D major) -> V (G major) -> borrowed iv
// (F minor) -> Picardy I (C major), one chord per bar.
HarmonicPlan modulationBars12To15() {
  HarmonicPlan plan;
  plan.tonic_pc = 0;
  plan.is_minor = false;
  plan.chords.push_back(chordAt(12 * kTicksPerBar, 2, ChordQuality::Major));  // V/V
  plan.chords.push_back(chordAt(13 * kTicksPerBar, 7, ChordQuality::Major));  // V
  plan.chords.push_back(chordAt(14 * kTicksPerBar, 5, ChordQuality::Minor));  // iv
  plan.chords.push_back(chordAt(15 * kTicksPerBar, 0, ChordQuality::Major));  // I
  return plan;
}

// The 15 authored Phase14 NCT-figure notes (V2, bars 12-15) as one
// time-ordered single-voice list — exactly what the Composer's NCT
// post-pass sees for voice 2. Pitches mirror harness_fixture.cpp.
std::vector<MaterialNote> authoredPhase14Figures() {
  const Tick b12 = 12 * kTicksPerBar;
  const Tick b13 = 13 * kTicksPerBar;
  const Tick b14 = 14 * kTicksPerBar;
  const Tick b15 = 15 * kTicksPerBar;
  return {
      // Cambiata, bar 12 (V/V = D F# A): A4 G4 D4 E4 F#4.
      fig(b12 + 0, 69),
      fig(b12 + 240, 67),
      fig(b12 + 480, 62),
      fig(b12 + 720, 64),
      fig(b12 + 960, 66),
      // Nota cambiata, bar 13 (V = G B D): D4 C4 A3 B3.
      fig(b13 + 0, 62),
      fig(b13 + 240, 60),
      fig(b13 + 480, 57),
      fig(b13 + 720, 59),
      // Echappee, bar 14 first half (iv = F Ab C): C4 D4 Ab3.
      fig(b14 + 0, 60),
      fig(b14 + 240, 62),
      fig(b14 + 480, 56),
      // Anticipation, bar 14 beat 3 -> bar 15 (I = C E G): F4 E4 ... E4.
      fig(b14 + 960, 65),
      fig(b14 + 1440, 64),
      mn(b15 + 0, 64),
  };
}

// Index of the first note whose pitch equals `pitch` in `notes`.
std::size_t indexOfPitch(const std::vector<MaterialNote>& notes, std::uint8_t pitch) {
  for (std::size_t i = 0; i < notes.size(); ++i) {
    if (notes[i].pitch == pitch)
      return i;
  }
  return notes.size();
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

// --- Authored Phase14 figures (the exact bar 12-15 V2 context). Each test
// drives the standalone detector with the real fixture notes + modulation
// chords and asserts the figure is detected at the correct NCT note. ---

TEST(NctDetectorTest, CambiataDetectedOnAuthoredG4) {
  const auto notes = authoredPhase14Figures();
  const auto hits = detectCambiata(notes, modulationBars12To15());
  ASSERT_GE(hits.size(), 1u);
  // The authored cambiata's NCT is G4 (67) at figure index 1.
  const std::size_t want = indexOfPitch(notes, 67);
  bool found = false;
  for (const auto& h : hits) {
    if (h.nct_index == want) {
      found = true;
      break;
    }
  }
  EXPECT_TRUE(found) << "no cambiata hit landed on the authored G4 (index " << want << ")";
}

TEST(NctDetectorTest, NotaCambiataDetectedOnAuthoredC4) {
  const auto notes = authoredPhase14Figures();
  const auto hits = detectNotaCambiata(notes, modulationBars12To15());
  ASSERT_EQ(hits.size(), 1u);
  // The authored nota cambiata's NCT is C4 (60) — the first C4 in the list.
  EXPECT_EQ(hits[0].nct_index, indexOfPitch(notes, 60));
  EXPECT_EQ(notes[hits[0].nct_index].pitch, 60u);
}

TEST(NctDetectorTest, EchappeeDetectedOnAuthoredD4) {
  const auto notes = authoredPhase14Figures();
  const auto hits = detectEchappee(notes, modulationBars12To15());
  ASSERT_EQ(hits.size(), 1u);
  // The authored echappee's NCT is the bar-14 D4 (62) — the second D4 in
  // the list (the first D4 opens the nota cambiata). Assert by pitch+window.
  EXPECT_EQ(notes[hits[0].nct_index].pitch, 62u);
  EXPECT_GE(hits[0].nct_index, 10u) << "echappee NCT must be the bar-14 D4, not the bar-13 D4";
}

TEST(NctDetectorTest, AnticipationDetectedOnAuthoredE4) {
  const auto notes = authoredPhase14Figures();
  const auto hits = detectAnticipation(notes, modulationBars12To15());
  ASSERT_EQ(hits.size(), 1u);
  // The authored anticipation's NCT is E4 (64) at bar 14 beat 3, whose
  // pitch equals the bar-15 Picardy-I chord tone E4 it anticipates.
  EXPECT_EQ(notes[hits[0].nct_index].pitch, 64u);
  // The anticipation E4 is the LAST eighth in the list (index 13), not the
  // earlier bar-12 cambiata E4 (index 3); pin it by window position.
  EXPECT_GE(hits[0].nct_index, 12u) << "anticipation NCT must be the bar-14 E4, not the bar-12 E4";
}

// --- M4: temporal-contiguity guard. A rest/gap mid-window must suppress
// the figure even when the pitch shape is otherwise a valid figure. ---

TEST(NctDetectorTest, CambiataRejectsGapMidWindow) {
  // Same pitch shape as CambiataDetectsClassicBachShape, but n2 (A4) starts
  // one beat late, opening a rest between n1 and n2. The window is no longer
  // melodically contiguous, so no figure may fire.
  std::vector<MaterialNote> notes = {
      mn(0, 64),                  // E5 chord (ends at 480)
      mn(kTicksPerBeat, 62),      // D5 NCT step-down (ends at 960)
      mn(kTicksPerBeat * 3, 57),  // A4 leap-down — starts late (gap at 960)
      mn(kTicksPerBeat * 4, 59),  // B4 step-up
      mn(kTicksPerBeat * 5, 60),  // C5 chord step-up
  };
  auto hits = detectCambiata(notes, cMajorWhole());
  EXPECT_TRUE(hits.empty()) << "cambiata must not fire across a mid-window rest";
}

TEST(NctDetectorTest, EchappeeRejectsGapMidWindow) {
  // C5 -> D5 NCT -> G4 chord, but the D5 ends before G4 begins (rest).
  std::vector<MaterialNote> notes = {
      mn(0, 60),                  // C5 chord (ends 480)
      mn(kTicksPerBeat, 62),      // D5 NCT step-up (ends 960)
      mn(kTicksPerBeat * 3, 55),  // G4 chord leap-down — starts late
  };
  auto hits = detectEchappee(notes, cMajorWhole());
  EXPECT_TRUE(hits.empty()) << "echappee must not fire across a mid-window rest";
}

TEST(NctDetectorTest, NotaCambiataRejectsGapMidWindow) {
  // G5 -> F5 -> B4 -> C5 compressed shape, but B4 starts a beat late.
  std::vector<MaterialNote> notes = {
      mn(0, 67),                  // G5 chord (ends 480)
      mn(kTicksPerBeat, 65),      // F5 NCT (ends 960)
      mn(kTicksPerBeat * 3, 59),  // B4 NCT — starts late (gap at 960)
      mn(kTicksPerBeat * 4, 60),  // C5 chord step-up
  };
  auto hits = detectNotaCambiata(notes, cMajorWhole());
  EXPECT_TRUE(hits.empty()) << "nota cambiata must not fire across a mid-window rest";
}

// --- M5: a figure straddling a chord boundary is classified against the
// chord active at its FIRST note, not a per-note re-query. ---

TEST(NctDetectorTest, EchappeeAnchorsToFirstNoteChordAcrossBoundary) {
  // C major active at t=0, then a spurious chord change to D major at t=2.
  // The echappee window (C5 -> D5 NCT -> G4) must be classified entirely
  // against the anchor C-major chord. Under per-note re-query the closing
  // G4 (pc 7) would be measured against D major (D F# A) where it is NOT a
  // chord tone, suppressing the figure. Anchoring fixes it.
  HarmonicPlan plan = cMajorWhole();
  ChordEvent d_maj;
  d_maj.start_tick = kTicksPerBeat * 2;  // boundary lands on the closing note
  d_maj.root_pc = 2;
  d_maj.quality = ChordQuality::Major;
  plan.chords.push_back(d_maj);

  std::vector<MaterialNote> notes = {
      mn(0, 60),                  // C5 chord (C major)
      mn(kTicksPerBeat, 62),      // D5 NCT step-up vs C major
      mn(kTicksPerBeat * 2, 55),  // G4 leap-down — chord tone of the ANCHOR
  };
  auto hits = detectEchappee(notes, plan);
  ASSERT_EQ(hits.size(), 1u) << "echappee must anchor classification to the first note's chord";
  EXPECT_EQ(hits[0].nct_index, 1u);
}

// --- M6: an anticipation requires a real chord change between the NCT and
// its resolution. Identical pitch under the SAME chord is not an
// anticipation; under a CHANGED chord it is. ---

TEST(NctDetectorTest, AnticipationRejectsNoChordChange) {
  // Exact pitch shape of AnticipationFiresWithChordChange (C5 -> B4 -> B4),
  // but the plan has a SINGLE C-major chord spanning the whole window: n1 and
  // n2 fall under the same chord event, so there is no next chord to
  // anticipate and the figure must not fire.
  std::vector<MaterialNote> notes = {
      mn(0, 60),                  // C5 chord-tone of C major
      mn(kTicksPerBeat, 59),      // B4 step-down NCT vs C major
      mn(kTicksPerBeat * 2, 59),  // B4 again — still under the same C major
  };
  auto hits = detectAnticipation(notes, cMajorWhole());
  EXPECT_TRUE(hits.empty()) << "anticipation must not fire without a real chord change";
}

TEST(NctDetectorTest, AnticipationFiresWithChordChange) {
  // Same melodic shape, but a G-major chord begins at t=2 so the held B4
  // anticipates the next chord's third. A genuine chord change is present.
  HarmonicPlan plan = cMajorWhole();
  ChordEvent g_maj;
  g_maj.start_tick = kTicksPerBeat * 2;
  g_maj.root_pc = 7;
  g_maj.quality = ChordQuality::Major;
  plan.chords.push_back(g_maj);

  std::vector<MaterialNote> notes = {
      mn(0, 60),                  // C5 chord-tone of C major
      mn(kTicksPerBeat, 59),      // B4 step-down NCT vs C major
      mn(kTicksPerBeat * 2, 59),  // B4 — chord tone of the NEW G major chord
  };
  auto hits = detectAnticipation(notes, plan);
  ASSERT_EQ(hits.size(), 1u) << "anticipation must fire across a real chord change";
  EXPECT_EQ(hits[0].nct_index, 1u);
}

}  // namespace bach::composer::nct_detector
