#include "composer/texture_helpers.h"

#include <gtest/gtest.h>

#include <cstdlib>
#include <vector>

#include "composer/figuration.h"
#include "core/basic_types.h"

namespace bach::composer {
namespace {

// Consonant interval-class set the form builders share: unison/octave, m3/M3,
// P4, P5, m6/M6 -> {0,3,4,5,7,8,9}; dissonant complement {1,2,6,10,11}.
TEST(TextureHelpersTest, IsConsonantIcTruthTable) {
  const bool expected[12] = {
      true,   // 0  unison/octave
      false,  // 1  m2
      false,  // 2  M2
      true,   // 3  m3
      true,   // 4  M3
      true,   // 5  P4
      false,  // 6  TT
      true,   // 7  P5
      true,   // 8  m6
      true,   // 9  M6
      false,  // 10 m7
      false,  // 11 M7
  };
  for (int ic = 0; ic < 12; ++ic) {
    EXPECT_EQ(isConsonantIc(ic), expected[ic]) << "interval class " << ic;
  }
}

TEST(TextureHelpersTest, IsConsonantIcFoldsLargeAndNegativeIntervals) {
  // The classification is closed under the x -> 12-x complement, so an interval
  // and its negation (or any octave displacement) classify identically.
  for (int ic = 0; ic < 12; ++ic) {
    EXPECT_EQ(isConsonantIc(ic), isConsonantIc(-ic)) << "interval class " << ic;
    EXPECT_EQ(isConsonantIc(ic), isConsonantIc(ic + 12)) << "interval class " << ic;
    EXPECT_EQ(isConsonantIc(ic), isConsonantIc(ic - 24)) << "interval class " << ic;
  }
}

TEST(TextureHelpersTest, ScaleDownWalksDiatonicDegrees) {
  // C major descent from C5: C-B-A-G-F-E-D-C, one degree per step.
  const int expected_major[] = {72, 71, 69, 67, 65, 64, 62, 60};
  for (int i = 0; i + 1 < 8; ++i) {
    EXPECT_EQ(detail::scaleDown(expected_major[i], 1, detail::Mode::Major), expected_major[i + 1]);
  }
  // Multi-step walk matches repeated single steps.
  EXPECT_EQ(detail::scaleDown(72, 7, detail::Mode::Major), 60);
  // C natural-minor descent from C5: C-Bb-Ab-G-F-Eb-D-C.
  const int expected_minor[] = {72, 70, 68, 67, 65, 63, 62, 60};
  for (int i = 0; i + 1 < 8; ++i) {
    EXPECT_EQ(detail::scaleDown(expected_minor[i], 1, detail::Mode::Minor), expected_minor[i + 1]);
  }
}

TEST(TextureHelpersTest, ScaleDownIsNotNegatedScaleUp) {
  // The diatonic sets are not symmetric under pitch-class negation, so the
  // -scaleUp(-midi) trick walks a different (chromatic-producing) set. F5 down
  // one C-major degree is E5; the negation trick lands D#5.
  EXPECT_EQ(detail::scaleDown(77, 1, detail::Mode::Major), 76);
  EXPECT_EQ(-detail::scaleUp(-77, 1, detail::Mode::Major), 75);
}

TEST(TextureHelpersTest, ScaleDownInvertsScaleUp) {
  // Down-then-up returns to the start from any in-scale pitch.
  for (int midi = 48; midi <= 84; ++midi) {
    for (detail::Mode mode : {detail::Mode::Major, detail::Mode::Minor}) {
      if (!detail::inScale(midi, mode)) {
        continue;
      }
      EXPECT_EQ(detail::scaleUp(detail::scaleDown(midi, 1, mode), 1, mode), midi)
          << "midi " << midi << " mode " << static_cast<int>(mode);
    }
  }
}

TEST(TextureHelpersTest, IsConsonantPairMatchesIc) {
  // A perfect fifth (7 semitones) is consonant; a tritone (6) is not. The pair
  // form is order-independent.
  EXPECT_TRUE(isConsonantPair(60, 67));   // P5
  EXPECT_TRUE(isConsonantPair(67, 60));   // P5, swapped
  EXPECT_TRUE(isConsonantPair(60, 64));   // M3
  EXPECT_FALSE(isConsonantPair(60, 66));  // TT
  EXPECT_FALSE(isConsonantPair(60, 61));  // m2
  EXPECT_TRUE(isConsonantPair(60, 60));   // unison
}

TEST(TextureHelpersTest, FormsPerfectParallelDetectsParallelFifth) {
  // Both voices rise a step into a fifth, from a fifth -> parallel fifths.
  // line: 60 -> 62, other: 67 -> 69 (a fifth above at both onsets).
  EXPECT_TRUE(formsPerfectParallel(/*line_prev=*/60, /*cand=*/62, /*other_prev=*/67,
                                   /*other_curr=*/69));
}

TEST(TextureHelpersTest, FormsPerfectParallelDetectsParallelOctave) {
  // Both voices rise a step into an octave, from an octave -> parallel octaves.
  EXPECT_TRUE(formsPerfectParallel(/*line_prev=*/60, /*cand=*/62, /*other_prev=*/72,
                                   /*other_curr=*/74));
}

TEST(TextureHelpersTest, FormsPerfectParallelAllowsContraryMotion) {
  // Contrary motion into a fifth is always allowed.
  EXPECT_FALSE(formsPerfectParallel(/*line_prev=*/60, /*cand=*/62, /*other_prev=*/71,
                                    /*other_curr=*/69));
}

TEST(TextureHelpersTest, FormsPerfectParallelAllowsObliqueMotion) {
  // One voice holds (oblique motion) -> never a parallel.
  EXPECT_FALSE(formsPerfectParallel(/*line_prev=*/60, /*cand=*/60, /*other_prev=*/67,
                                    /*other_curr=*/69));
  EXPECT_FALSE(formsPerfectParallel(/*line_prev=*/60, /*cand=*/62, /*other_prev=*/67,
                                    /*other_curr=*/67));
}

TEST(TextureHelpersTest, FormsPerfectParallelAllowsArrivalOnImperfect) {
  // Same-direction step arriving on a third (not a perfect) is allowed.
  EXPECT_FALSE(formsPerfectParallel(/*line_prev=*/60, /*cand=*/62, /*other_prev=*/64,
                                    /*other_curr=*/66));
}

TEST(TextureHelpersTest, FormsPerfectParallelHiddenPerfectOnLeap) {
  // Same-direction arrival on a fifth from a different interval, with the upper
  // voice (the other voice, 64 -> 69) leaping (> 2 semitones) -> hidden perfect.
  EXPECT_TRUE(formsPerfectParallel(/*line_prev=*/60, /*cand=*/62, /*other_prev=*/64,
                                   /*other_curr=*/69));
  // The same arrival reached by a step in the upper voice (<= 2) is allowed.
  EXPECT_FALSE(formsPerfectParallel(/*line_prev=*/60, /*cand=*/62, /*other_prev=*/68,
                                    /*other_curr=*/69));
}

TEST(TextureHelpersTest, FormsPerfectParallelNeedsBothVoicesTwoOnsets) {
  // A missing previous onset (sentinel -1) cannot judge motion.
  EXPECT_FALSE(formsPerfectParallel(/*line_prev=*/-1, /*cand=*/62, /*other_prev=*/67,
                                    /*other_curr=*/69));
  EXPECT_FALSE(formsPerfectParallel(/*line_prev=*/60, /*cand=*/62, /*other_prev=*/-1,
                                    /*other_curr=*/69));
  EXPECT_FALSE(formsPerfectParallel(/*line_prev=*/60, /*cand=*/62, /*other_prev=*/67,
                                    /*other_curr=*/-1));
}

TEST(TextureHelpersTest, ThemeToneRegistryRecordAndLookup) {
  ThemeToneRegistry registry;
  registry.record(/*tick=*/480, /*voice=*/0, /*pitch=*/72, /*duration=*/480);
  registry.record(/*tick=*/960, /*voice=*/0, /*pitch=*/74, /*duration=*/480);
  registry.record(/*tick=*/480, /*voice=*/1, /*pitch=*/60, /*duration=*/480);

  // Inside the [start, start+dur) window the recorded pitch is returned.
  EXPECT_EQ(registry.soundingPitchInVoice(0, 480), 72);
  EXPECT_EQ(registry.soundingPitchInVoice(0, 700), 72);
  EXPECT_EQ(registry.soundingPitchInVoice(0, 960), 74);
  EXPECT_EQ(registry.soundingPitchInVoice(1, 480), 60);

  // The window is half-open: the end tick belongs to the next note's window.
  EXPECT_EQ(registry.soundingPitchInVoice(0, 959), 72);

  // A different voice with no note at the tick is silent.
  EXPECT_EQ(registry.soundingPitchInVoice(2, 480), -1);
}

TEST(TextureHelpersTest, ThemeToneRegistryUnsignedSentinelEdge) {
  ThemeToneRegistry registry;
  // No record yet: any lookup is silent.
  EXPECT_EQ(registry.soundingPitchInVoice(0, 0), -1);

  // A note that starts at tick 0 must still be found at tick 0. The internal
  // best-start sentinel is signed (-1), so a zero start does not lose to it.
  registry.record(/*tick=*/0, /*voice=*/0, /*pitch=*/64, /*duration=*/480);
  EXPECT_EQ(registry.soundingPitchInVoice(0, 0), 64);

  // Before any onset of the queried voice there is no sounding pitch.
  registry.record(/*tick=*/960, /*voice=*/1, /*pitch=*/55, /*duration=*/480);
  EXPECT_EQ(registry.soundingPitchInVoice(1, 0), -1);
  EXPECT_EQ(registry.soundingPitchInVoice(1, 959), -1);
  EXPECT_EQ(registry.soundingPitchInVoice(1, 960), 55);
}

TEST(TextureHelpersTest, ThemeToneRegistryLatestOnsetWins) {
  ThemeToneRegistry registry;
  // Two overlapping notes in one voice: the later-starting one wins inside the
  // overlap (it is the most recent onset whose window contains the tick).
  registry.record(/*tick=*/0, /*voice=*/0, /*pitch=*/60, /*duration=*/960);
  registry.record(/*tick=*/480, /*voice=*/0, /*pitch=*/67, /*duration=*/480);
  EXPECT_EQ(registry.soundingPitchInVoice(0, 240), 60);
  EXPECT_EQ(registry.soundingPitchInVoice(0, 480), 67);
  EXPECT_EQ(registry.soundingPitchInVoice(0, 700), 67);
}

TEST(TextureHelpersTest, ThemeToneRegistryConcurrentThemePitches) {
  ThemeToneRegistry registry;
  registry.record(/*tick=*/0, /*voice=*/0, /*pitch=*/72, /*duration=*/480);
  registry.record(/*tick=*/0, /*voice=*/1, /*pitch=*/60, /*duration=*/480);
  registry.record(/*tick=*/0, /*voice=*/2, /*pitch=*/48, /*duration=*/480);

  std::vector<int> pitches;
  // Exclude voice 1: voices 0 and 2 sound concurrently at tick 0.
  registry.concurrentThemePitches(/*tick=*/0, /*voice=*/1, pitches);
  ASSERT_EQ(pitches.size(), 2u);
  EXPECT_EQ(pitches[0], 72);
  EXPECT_EQ(pitches[1], 48);
}

TEST(TextureHelpersTest, ThemeToneRegistryConcurrentMotions) {
  ThemeToneRegistry registry;
  // Voice 0 moves 72 -> 74 across two beats; voice 1 holds 60.
  registry.record(/*tick=*/0, /*voice=*/0, /*pitch=*/72, /*duration=*/480);
  registry.record(/*tick=*/480, /*voice=*/0, /*pitch=*/74, /*duration=*/480);
  registry.record(/*tick=*/0, /*voice=*/1, /*pitch=*/60, /*duration=*/960);

  std::vector<ConcurrentMotion> motions;
  // From the line on voice 2, gather motions of voices 0 and 1 around tick 480.
  registry.concurrentMotions(/*prev_tick=*/0, /*tick=*/480, /*voice=*/2, /*num_voices=*/3, motions);
  ASSERT_EQ(motions.size(), 2u);
  EXPECT_EQ(motions[0].voice, 0);
  EXPECT_EQ(motions[0].prev, 72);
  EXPECT_EQ(motions[0].curr, 74);
  EXPECT_EQ(motions[1].voice, 1);
  EXPECT_EQ(motions[1].prev, 60);
  EXPECT_EQ(motions[1].curr, 60);
}

TEST(TextureHelpersTest, ConsonantChordToneReturnsChordToneOnDownbeat) {
  // C major triad chord; an empty texture; the downbeat anchor must be a genuine
  // chord tone (pitch class in {0,4,7}) nearest the target.
  detail::ChordSpec chord{0, false};
  std::vector<int> theme_pitches;
  std::vector<ConcurrentMotion> motions;
  const int anchor = consonantChordTone(chord, /*voice=*/0, /*band_lo=*/60, /*band_hi=*/84,
                                        /*target=*/72, theme_pitches, /*line_prev=*/-1, motions,
                                        detail::Mode::Major, /*downbeat=*/true);
  const int pc = anchor % 12;
  EXPECT_TRUE(pc == 0 || pc == 4 || pc == 7) << "anchor " << anchor;
  EXPECT_GE(anchor, 60);
  EXPECT_LE(anchor, 84);
}

TEST(TextureHelpersTest, ConsonantChordToneWindowEmptyKeepsNearestConsonant) {
  // With an empty window the tie-breaker is inert: the selector returns the
  // nearest onset-consonant chord tone to the target, exactly as before the
  // window_pitches parameter was added. C major triad, target G5 (67): G is a
  // chord tone in band and nearest, so it is chosen.
  detail::ChordSpec chord{0, false};
  const std::vector<int> theme_pitches;
  const std::vector<ConcurrentMotion> motions;
  const std::vector<int> empty_window;
  const int anchor = consonantChordTone(chord, /*voice=*/0, /*band_lo=*/60, /*band_hi=*/84,
                                        /*target=*/67, theme_pitches, /*line_prev=*/-1, motions,
                                        detail::Mode::Major, /*downbeat=*/true, empty_window);
  EXPECT_EQ(anchor, 67);
}

TEST(TextureHelpersTest, ConsonantChordToneWindowAvoidsMidBeatClash) {
  // Same chord/target, but a sustained-window pitch (F5 = 65) is dissonant with
  // the nearest chord tone G5 (67, a M2) yet consonant with C6 (72, a P5).
  // Among equally onset-consonant chord tones the window tie-breaker prefers the
  // one with no mid-window clash, so the anchor moves off G to C.
  detail::ChordSpec chord{0, false};
  const std::vector<int> theme_pitches;
  const std::vector<ConcurrentMotion> motions;

  // Confirm the premise: the empty-window choice (67) clashes with the window
  // pitch while the alternative chord tone (72) does not.
  EXPECT_FALSE(isConsonantPair(67, 65));  // G vs F -> M2, dissonant.
  EXPECT_TRUE(isConsonantPair(72, 65));   // C vs F -> P5, consonant.

  const std::vector<int> window = {65};
  const int anchor = consonantChordTone(chord, /*voice=*/0, /*band_lo=*/60, /*band_hi=*/84,
                                        /*target=*/67, theme_pitches, /*line_prev=*/-1, motions,
                                        detail::Mode::Major, /*downbeat=*/true, window);
  // Still a genuine chord tone, but the window-consonant one rather than 67.
  const int pc = anchor % 12;
  EXPECT_TRUE(pc == 0 || pc == 4 || pc == 7) << "anchor " << anchor;
  EXPECT_NE(anchor, 67) << "window clash with G5 not avoided";
  EXPECT_TRUE(isConsonantPair(anchor, 65)) << "chosen anchor still clashes with the window pitch";
}

// A conjunct anchor within a fifth of the running target outranks a clash-free
// chord tone a sixth or more away: the far candidate is demoted below every
// near admissible candidate regardless of its mid-window clash count, so the
// per-beat anchor chain stays conjunct instead of lurching a tenth away to
// dodge a passing clash near the line.
TEST(TextureHelpersTest, ConsonantChordToneDemotesFarClashFreeTone) {
  detail::ChordSpec chord{0, false};  // C major triad, pitch classes {0,4,7}.
  const std::vector<int> theme_pitches;
  const std::vector<ConcurrentMotion> motions;
  // Band [37,49] admits exactly E2=40, G2=43, C3=48 on the downbeat. Target 48
  // makes E2 the sole far tone (dist 8 > a fifth); G2 (dist 5) and C3 (dist 0)
  // are near. The window pitch C#3=49 is dissonant with G (tritone) and with C
  // (m2) yet consonant with E (M6), so the FAR tone is the only clash-free
  // candidate -- the pre-demotion ordering (window clashes * 128 + dist) would
  // pick it (key 8) over the near clashing tones (keys 128 and 133).
  EXPECT_FALSE(isConsonantPair(43, 49));  // G vs C# -> tritone clash.
  EXPECT_FALSE(isConsonantPair(48, 49));  // C vs C# -> m2 clash.
  EXPECT_TRUE(isConsonantPair(40, 49));   // E vs C# -> M6, clash-free.
  const std::vector<int> window = {49};

  const int anchor = consonantChordTone(chord, /*voice=*/2, /*band_lo=*/37, /*band_hi=*/49,
                                        /*target=*/48, theme_pitches, /*line_prev=*/-1, motions,
                                        detail::Mode::Major, /*downbeat=*/true, window);
  // A near chord tone wins over the far clash-free E2 despite E2's cleaner
  // window profile: the returned anchor stays within a fifth of the target.
  const int pc = anchor % 12;
  EXPECT_TRUE(pc == 0 || pc == 4 || pc == 7) << "anchor " << anchor;
  EXPECT_LE(std::abs(anchor - 48), 7)
      << "anchor " << anchor << " fled beyond a fifth of the target";
}

// The far-anchor demotion is a preference, never an admissibility override:
// when the near chord tones are excluded by the voice-ordering window (a
// concurrent lower-index voice sounding just above the far tone), the far tone
// is still returned rather than crashing or clamping to the target.
TEST(TextureHelpersTest, ConsonantChordToneKeepsFarToneWhenNearOnesCrossVoiceOrder) {
  detail::ChordSpec chord{0, false};
  const std::vector<int> theme_pitches;
  // A lower-index voice (voice 1) sounds at F#2=42, so voice 2's anchor must
  // stay at or below 42 to preserve the V1 >= V2 register order. G2=43 and
  // C3=48 are now inadmissible, leaving only the far tone E2=40.
  const std::vector<ConcurrentMotion> motions = {
      ConcurrentMotion{/*voice=*/1, /*prev=*/42, /*curr=*/42}};
  const std::vector<int> window = {49};

  const int anchor = consonantChordTone(chord, /*voice=*/2, /*band_lo=*/37, /*band_hi=*/49,
                                        /*target=*/48, theme_pitches, /*line_prev=*/-1, motions,
                                        detail::Mode::Major, /*downbeat=*/true, window);
  EXPECT_EQ(anchor, 40) << "far tone not returned when the near tones cross the voice order";
}

}  // namespace
}  // namespace bach::composer
