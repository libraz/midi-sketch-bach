#include "composer/rule_helpers.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

#include "composer/harmonic_plan.h"
#include "core/basic_types.h"

namespace bach::composer::rule_helpers {
namespace {

HarmonicPlan cMajor() {
  HarmonicPlan plan;
  plan.tonic_pc = 0;
  plan.is_minor = false;
  return plan;
}

HarmonicPlan aMinor() {
  HarmonicPlan plan;
  plan.tonic_pc = 9;
  plan.is_minor = true;
  return plan;
}

NoteEvent makeNote(Tick start, Tick dur, std::uint8_t pitch, VoiceId voice) {
  NoteEvent n;
  n.start_tick = start;
  n.duration = dur;
  n.pitch = pitch;
  n.voice = voice;
  n.velocity = 80;
  return n;
}

}  // namespace

TEST(RuleHelpersTest, IsStrongBeatDetectsBarDownbeats) {
  EXPECT_TRUE(isStrongBeat(0));
  EXPECT_TRUE(isStrongBeat(kTicksPerBar));
  EXPECT_FALSE(isStrongBeat(kTicksPerBeat));
  EXPECT_FALSE(isStrongBeat(kTicksPerBeat * 2));
}

TEST(RuleHelpersTest, ActiveChordUsesSafeFallbackForEmptyPlan) {
  HarmonicPlan plan;
  const ChordEvent& chord = activeChord(plan, 0);
  EXPECT_EQ(chord.start_tick, 0u);
  EXPECT_EQ(chord.root_pc, 0u);
  EXPECT_EQ(chord.quality, ChordQuality::Major);
}

TEST(RuleHelpersTest, ActiveChordSelectsLatestChordAtEveryBoundary) {
  HarmonicPlan plan = cMajor();
  plan.chords = {
      {0, 0, ChordQuality::Major},
      {kTicksPerBeat, 7, ChordQuality::Major},
      {2 * kTicksPerBeat, 9, ChordQuality::Minor},
  };

  EXPECT_EQ(activeChord(plan, 0).root_pc, 0u);
  EXPECT_EQ(activeChord(plan, kTicksPerBeat - 1).root_pc, 0u);
  EXPECT_EQ(activeChord(plan, kTicksPerBeat).root_pc, 7u);
  EXPECT_EQ(activeChord(plan, 2 * kTicksPerBeat - 1).root_pc, 7u);
  EXPECT_EQ(activeChord(plan, 2 * kTicksPerBeat).root_pc, 9u);
  EXPECT_EQ(activeChord(plan, 3 * kTicksPerBeat).root_pc, 9u);
}

TEST(RuleHelpersTest, MetricalStrengthRecognizesCommonTimeSecondaryAccent) {
  HarmonicPlan plan = cMajor();
  plan.ts_numerator = 4;
  plan.ts_denominator = 4;
  EXPECT_EQ(metricalStrengthAt(plan, 0), MetricalStrength::Strong);
  EXPECT_EQ(metricalStrengthAt(plan, kTicksPerBeat), MetricalStrength::Weak);
  EXPECT_EQ(metricalStrengthAt(plan, 2 * kTicksPerBeat), MetricalStrength::Medium);
  EXPECT_EQ(metricalStrengthAt(plan, 3 * kTicksPerBeat), MetricalStrength::Weak);
  EXPECT_TRUE(isStructuralAccent(plan, 2 * kTicksPerBeat));
}

TEST(RuleHelpersTest, MetricalStrengthDistinguishesSarabandeBeatTwo) {
  HarmonicPlan standard = cMajor();
  standard.ts_numerator = 3;
  standard.ts_denominator = 4;
  HarmonicPlan sarabande = standard;
  sarabande.meter_profile = MeterProfile::SarabandeTriple;
  EXPECT_EQ(metricalStrengthAt(standard, kTicksPerBeat), MetricalStrength::Weak);
  EXPECT_EQ(metricalStrengthAt(sarabande, kTicksPerBeat), MetricalStrength::Medium);
  EXPECT_FALSE(isStructuralAccent(standard, kTicksPerBeat));
  EXPECT_TRUE(isStructuralAccent(sarabande, kTicksPerBeat));
}

TEST(RuleHelpersTest, CompoundMeterUsesDottedPulseGrid) {
  HarmonicPlan plan = cMajor();
  plan.ts_numerator = 6;
  plan.ts_denominator = 8;
  const Tick eighth = kTicksPerBeat / 2;
  EXPECT_EQ(metricalStrengthAt(plan, eighth), MetricalStrength::Weak);
  EXPECT_EQ(metricalStrengthAt(plan, 3 * eighth), MetricalStrength::Medium);
  EXPECT_TRUE(isStructuralAccent(plan, 3 * eighth));
}

TEST(RuleHelpersTest, IsLeadingToneRecognizesSeventhDegreeInBothModes) {
  // In C major (tonic 0): leading tone is B (pc 11).
  EXPECT_TRUE(isLeadingTone(71, cMajor()));   // B4
  EXPECT_TRUE(isLeadingTone(83, cMajor()));   // B5
  EXPECT_FALSE(isLeadingTone(72, cMajor()));  // C5 (tonic)
  // In A minor (tonic 9): leading tone is G# (pc 8).
  EXPECT_TRUE(isLeadingTone(68, aMinor()));   // G#4
  EXPECT_FALSE(isLeadingTone(67, aMinor()));  // G natural
}

TEST(RuleHelpersTest, IsLeadingToneResolutionRequiresStepwiseUpwardToTonic) {
  HarmonicPlan plan = cMajor();
  // B4 → C5 = upward semitone to tonic. OK.
  EXPECT_TRUE(isLeadingToneResolution(71, 72, plan));
  // B4 → D5 = leap, not stepwise. Not a resolution.
  EXPECT_FALSE(isLeadingToneResolution(71, 74, plan));
  // B4 → A4 = downward. Not a resolution.
  EXPECT_FALSE(isLeadingToneResolution(71, 69, plan));
  // B4 → C#5 (73) — not tonic pc. Reject.
  EXPECT_FALSE(isLeadingToneResolution(71, 73, plan));
  // Out-of-range candidate rejected.
  EXPECT_FALSE(isLeadingToneResolution(71, 128, plan));
  EXPECT_FALSE(isLeadingToneResolution(71, -1, plan));
}

TEST(RuleHelpersTest, IsPerfectIntervalMatchesUnisonAndFifth) {
  EXPECT_TRUE(isPerfectInterval(0));
  EXPECT_TRUE(isPerfectInterval(7));
  EXPECT_TRUE(isPerfectInterval(-7));
  EXPECT_TRUE(isPerfectInterval(12));  // octave reduces to unison
  EXPECT_FALSE(isPerfectInterval(3));
  EXPECT_FALSE(isPerfectInterval(5));  // P4 (we treat as consonant but not "perfect" for parallel
                                       // rule symmetry — see validator)
}

TEST(RuleHelpersTest, IsConsonantIntervalMatchesBaroqueSet) {
  // Consonant: 0, 3, 4, 5, 7, 8, 9 (mod 12).
  for (int s : {0, 3, 4, 5, 7, 8, 9}) {
    EXPECT_TRUE(isConsonantInterval(s)) << s;
  }
  for (int s : {1, 2, 6, 10, 11}) {
    EXPECT_FALSE(isConsonantInterval(s)) << s;
  }
}

TEST(RuleHelpersTest, FourthIsDissonantAboveActualBass) {
  EXPECT_FALSE(isConsonantAboveBass(65, 60));
  EXPECT_FALSE(isBassSensitiveConsonance(65, 60, 60));
}

TEST(RuleHelpersTest, UpperVoiceFourthIsValidOverConsonantBass) {
  // F4 and C4 form a fourth, but both are consonant over F3.
  EXPECT_TRUE(isBassSensitiveConsonance(65, 60, 53));
}

TEST(RuleHelpersTest, ContextualMinorPolicyDistinguishesNaturalAndRaisedUpperDegrees) {
  HarmonicPlan plan;
  plan.tonic_pc = 0;
  plan.is_minor = true;
  ChordEvent tonic;
  tonic.start_tick = 0;
  tonic.root_pc = 0;
  tonic.quality = ChordQuality::Minor;
  tonic.degree = RomanNumeral::I;
  tonic.function = HarmonicFunction::T;
  tonic.has_degree = true;
  plan.chords.push_back(tonic);

  EXPECT_TRUE(isContextualScalePitch(70, plan, 0, -2));  // Bb descending
  EXPECT_TRUE(isContextualScalePitch(68, plan, 0, -2));  // Ab descending
  EXPECT_FALSE(isContextualScalePitch(70, plan, 0, 2));  // Bb ascending
  EXPECT_FALSE(isContextualScalePitch(68, plan, 0, 1));  // Ab ascending
  EXPECT_TRUE(isContextualScalePitch(69, plan, 0, 2));   // A natural ascending
  EXPECT_TRUE(isContextualScalePitch(71, plan, 0, 1));   // B natural ascending
  EXPECT_FALSE(isContextualLeadingTone(70, plan, 0));    // modal subtonic
}

TEST(RuleHelpersTest, ContextualLeadingToneFollowsDominantAndSecondaryTarget) {
  HarmonicPlan plan;
  plan.tonic_pc = 0;
  plan.is_minor = true;
  ChordEvent dominant;
  dominant.start_tick = 0;
  dominant.root_pc = 7;
  dominant.quality = ChordQuality::Major;
  dominant.degree = RomanNumeral::V;
  dominant.function = HarmonicFunction::D;
  dominant.has_degree = true;
  plan.chords.push_back(dominant);
  EXPECT_TRUE(isContextualLeadingTone(71, plan, 0));
  EXPECT_TRUE(isContextualLeadingToneResolution(71, 72, plan, 0));

  ChordEvent secondary;
  secondary.start_tick = kTicksPerBeat;
  secondary.root_pc = 2;  // D major = V/V; F# resolves to G.
  secondary.quality = ChordQuality::Major;
  secondary.function = HarmonicFunction::D;
  secondary.has_secondary_of = true;
  secondary.secondary_of = RomanNumeral::V;
  plan.chords.push_back(secondary);
  const TonalContext context = tonalContextAt(plan, kTicksPerBeat);
  EXPECT_TRUE(context.is_secondary_dominant);
  EXPECT_EQ(context.leading_tone_pc, 6);
  EXPECT_EQ(context.resolution_pc, 7);
  EXPECT_TRUE(isContextualLeadingTone(66, plan, kTicksPerBeat));
  EXPECT_TRUE(isContextualLeadingToneResolution(66, 67, plan, kTicksPerBeat));
}

TEST(RuleHelpersTest, ContextualPolicyUsesLatestLocalKey) {
  HarmonicPlan plan;
  plan.tonic_pc = 0;
  ChordEvent g_dominant;
  g_dominant.start_tick = kTicksPerBeat;
  g_dominant.root_pc = 2;  // D dominant of local G.
  g_dominant.quality = ChordQuality::Major;
  g_dominant.degree = RomanNumeral::V;
  g_dominant.function = HarmonicFunction::D;
  g_dominant.has_degree = true;
  plan.chords.push_back(g_dominant);
  plan.modulations.push_back({kTicksPerBeat, 0, 7, false, false, ModulationType::Phrase});
  const TonalContext context = tonalContextAt(plan, kTicksPerBeat);
  EXPECT_EQ(context.tonic_pc, 7);
  EXPECT_EQ(context.leading_tone_pc, 6);
}

TEST(RuleHelpersTest, KeyAtSwitchesOnTheModulationBoundary) {
  HarmonicPlan plan = cMajor();
  plan.modulations.push_back({kTicksPerBar, 0, 7, false, false, ModulationType::Phrase});
  plan.modulations.push_back({2 * kTicksPerBar, 7, 9, false, true, ModulationType::Phrase});

  EXPECT_EQ(keyAt(plan, 0).tonic_pc, 0);
  EXPECT_FALSE(keyAt(plan, 0).is_minor);
  EXPECT_EQ(keyAt(plan, kTicksPerBar - 1).tonic_pc, 0);
  EXPECT_EQ(keyAt(plan, kTicksPerBar).tonic_pc, 7);
  EXPECT_FALSE(keyAt(plan, kTicksPerBar).is_minor);
  EXPECT_EQ(keyAt(plan, 2 * kTicksPerBar - 1).tonic_pc, 7);
  EXPECT_EQ(keyAt(plan, 2 * kTicksPerBar).tonic_pc, 9);
  EXPECT_TRUE(keyAt(plan, 2 * kTicksPerBar).is_minor);
  EXPECT_EQ(keyAt(plan, 8 * kTicksPerBar).tonic_pc, 9);
}

TEST(RuleHelpersTest, IsCrossRelationPcDetectsInflectedDegreesInMajor) {
  // C major: each chromatic pitch class reads as the raised form of the degree
  // below it, so it clashes with that degree and with nothing else.
  const auto major = [](std::uint8_t pc_a, std::uint8_t pc_b) {
    return isCrossRelationPc(pc_a, pc_b, /*tonic_pc=*/0, /*is_minor=*/false);
  };
  EXPECT_TRUE(major(0, 1));   // C / C#
  EXPECT_TRUE(major(2, 3));   // D / D#
  EXPECT_TRUE(major(5, 6));   // F / F#
  EXPECT_TRUE(major(7, 8));   // G / G#
  EXPECT_TRUE(major(9, 10));  // A / A#
  // Half-steps between two distinct degrees are not cross relations.
  EXPECT_FALSE(major(4, 5));    // E / F
  EXPECT_FALSE(major(11, 0));   // B / C
  EXPECT_FALSE(major(6, 7));    // F# is the raised 4th, not a lowered 5th
  EXPECT_FALSE(major(1, 2));    // C# against the natural 2nd
  EXPECT_FALSE(major(3, 4));    // D# against the natural 3rd
  EXPECT_FALSE(major(8, 9));    // G# against the natural 6th
  EXPECT_FALSE(major(10, 11));  // A# against the natural 7th
}

TEST(RuleHelpersTest, IsCrossRelationPcFollowsTheMinorScaleDegrees) {
  // C minor: the harmonic minor owns Eb and Ab outright, so those half-steps
  // are ordinary adjacent degrees; the alterations that DO clash are the
  // melodic-minor 6th, the natural 7th, and mode mixture on the 3rd and 4th.
  const auto minor = [](std::uint8_t pc_a, std::uint8_t pc_b) {
    return isCrossRelationPc(pc_a, pc_b, /*tonic_pc=*/0, /*is_minor=*/true);
  };
  EXPECT_FALSE(minor(2, 3));   // D / Eb — the 2nd and the minor 3rd
  EXPECT_FALSE(minor(7, 8));   // G / Ab — the 5th and the minor 6th
  EXPECT_TRUE(minor(8, 9));    // Ab / A natural — the melodic-minor 6th
  EXPECT_TRUE(minor(10, 11));  // Bb / B natural — the raised 7th
  EXPECT_TRUE(minor(3, 4));    // Eb / E natural — mode mixture
  EXPECT_TRUE(minor(5, 6));    // F / F# — the raised 4th
  EXPECT_FALSE(minor(1, 2));   // Db against the natural 2nd
}

TEST(RuleHelpersTest, IsCrossRelationPcIsSymmetricAndIgnoresUnisons) {
  for (std::uint8_t pc_a = 0; pc_a < 12; ++pc_a) {
    EXPECT_FALSE(isCrossRelationPc(pc_a, pc_a, 0, false)) << "major unison pc " << int{pc_a};
    EXPECT_FALSE(isCrossRelationPc(pc_a, pc_a, 0, true)) << "minor unison pc " << int{pc_a};
    for (std::uint8_t pc_b = 0; pc_b < 12; ++pc_b) {
      EXPECT_EQ(isCrossRelationPc(pc_a, pc_b, 0, false), isCrossRelationPc(pc_b, pc_a, 0, false))
          << "major pair " << int{pc_a} << "," << int{pc_b};
      EXPECT_EQ(isCrossRelationPc(pc_a, pc_b, 0, true), isCrossRelationPc(pc_b, pc_a, 0, true))
          << "minor pair " << int{pc_a} << "," << int{pc_b};
    }
  }
}

TEST(RuleHelpersTest, VoicePitchAtReturnsLastSoundingPitch) {
  std::vector<NoteEvent> notes = {
      makeNote(0, kTicksPerBeat * 2, 72, 0),
      makeNote(kTicksPerBeat * 2, kTicksPerBeat, 74, 0),
  };
  EXPECT_EQ(voicePitchAt(notes, 0, 0), 72);
  EXPECT_EQ(voicePitchAt(notes, 0, kTicksPerBeat), 72);
  EXPECT_EQ(voicePitchAt(notes, 0, kTicksPerBeat * 2), 74);
  EXPECT_EQ(voicePitchAt(notes, 1, 0), 0);
}

TEST(RuleHelpersTest, CreatesVoiceCrossingRejectsLowerVoiceAboveUpper) {
  // Placed: voice 0 (soprano) at C4 (60). New voice 1 (alto) attempts E4 (64).
  // Voice 1 must stay below voice 0; 64 > 60 is a crossing.
  std::vector<NoteEvent> placed = {makeNote(0, kTicksPerBeat, 60, 0)};
  EXPECT_TRUE(createsVoiceCrossing(placed, 1, 64, 0));
  EXPECT_FALSE(createsVoiceCrossing(placed, 1, 55, 0));
}

TEST(RuleHelpersTest, CreatesParallelPerfectDetectsP5InOuterVoices) {
  // Voice 0 (upper): G4 (67) → A4 (69). Voice 1 (lower): C4 (60) → D4 (62).
  // Both moved up; interval 7→7 (perfect fifth). Parallel.
  std::vector<NoteEvent> placed = {
      makeNote(0, kTicksPerBeat, 67, 0),
      makeNote(kTicksPerBeat, kTicksPerBeat, 69, 0),
      makeNote(0, kTicksPerBeat, 60, 1),
  };
  EXPECT_TRUE(createsParallelPerfect(placed, /*candidate_voice=*/1,
                                     /*candidate_pitch=*/62, /*cur_tick=*/kTicksPerBeat,
                                     /*prev_pitch=*/60, /*prev_tick=*/0));
}

TEST(RuleHelpersTest, CreatesHiddenParallelDetectsSimilarMotionToFifth) {
  // Hidden P5: both voices move upward, the upper voice leaps, and the
  // resulting interval is a perfect fifth while the previous interval was
  // not perfect. P4 is NOT treated as perfect here (only unison/octave/P5).
  //
  // Upper (voice 0): D#5 (75) → G5 (79).
  // Lower (voice 1): A3 (57) → C4 (60).
  // Prev interval (upper - lower) = 75-57 = 18 → mod 12 = 6 (not perfect).
  // Now interval = 79-60 = 19 → mod 12 = 7 (P5, perfect). Similar motion.
  std::vector<NoteEvent> placed = {
      makeNote(0, kTicksPerBeat, 75, 0),
      makeNote(kTicksPerBeat, kTicksPerBeat, 79, 0),
      makeNote(0, kTicksPerBeat, 57, 1),
  };
  EXPECT_TRUE(createsHiddenParallelPerfect(placed, 1, 60, kTicksPerBeat, 57, 0));
}

TEST(RuleHelpersTest, CreatesHiddenOctaveWhenPreviousIntervalWasPerfectFifth) {
  // The shared perfect-motion classifier treats P5 -> P8 by similar motion
  // with an upper-voice leap as a direct (hidden) octave.  The candidate
  // pre-filter must use the same definition as the final Validator.
  std::vector<NoteEvent> placed = {
      makeNote(0, kTicksPerBeat, 65, 2),
      makeNote(kTicksPerBeat, kTicksPerBeat, 67, 2),
  };
  EXPECT_TRUE(createsHiddenParallelPerfect(placed, /*candidate_voice=*/1,
                                           /*candidate_pitch=*/79,
                                           /*cur_tick=*/kTicksPerBeat,
                                           /*prev_pitch=*/72, /*prev_tick=*/0));
}

TEST(RuleHelpersTest, CreatesParallelOctaveDetectsLockstepOctaves) {
  // Upper (voice 0): C5 (72) → D5 (74). Lower (voice 1): C4 (60) → D4 (62).
  // Both move up by the same amount; intervals are perfect octave then
  // perfect octave again. Parallel octave.
  std::vector<NoteEvent> placed = {
      makeNote(0, kTicksPerBeat, 72, 0),
      makeNote(kTicksPerBeat, kTicksPerBeat, 74, 0),
      makeNote(0, kTicksPerBeat, 60, 1),
  };
  EXPECT_TRUE(createsParallelOctave(placed, /*candidate_voice=*/1,
                                    /*candidate_pitch=*/62, /*cur_tick=*/kTicksPerBeat,
                                    /*prev_pitch=*/60, /*prev_tick=*/0));
}

TEST(RuleHelpersTest, CreatesParallelOctaveDoesNotFireOnParallelFifth) {
  // Upper (voice 0): G4 (67) → A4 (69). Lower (voice 1): C4 (60) → D4 (62).
  // Both move up by the same amount; intervals are perfect fifth then
  // perfect fifth. Parallel fifth, NOT octave — must return false.
  std::vector<NoteEvent> placed = {
      makeNote(0, kTicksPerBeat, 67, 0),
      makeNote(kTicksPerBeat, kTicksPerBeat, 69, 0),
      makeNote(0, kTicksPerBeat, 60, 1),
  };
  EXPECT_FALSE(createsParallelOctave(placed, /*candidate_voice=*/1,
                                     /*candidate_pitch=*/62, /*cur_tick=*/kTicksPerBeat,
                                     /*prev_pitch=*/60, /*prev_tick=*/0));
}

TEST(RuleHelpersTest, CreatesCrossRelationDetectsChromaticConflictInOtherVoice) {
  // Voice 0 sounds C natural (60) at tick 0..2. Voice 1 candidate is C#
  // (61) at tick 0. Cross-relation since lower voice's note is sounding.
  std::vector<NoteEvent> placed = {makeNote(0, kTicksPerBeat * 2, 60, 0)};
  EXPECT_TRUE(createsCrossRelation(placed, 1, 61, 0, cMajor()));
  // Adjacent degrees E↔F: not a cross relation.
  std::vector<NoteEvent> placed_ef = {makeNote(0, kTicksPerBeat * 2, 64, 0)};
  EXPECT_FALSE(createsCrossRelation(placed_ef, 1, 65, 0, cMajor()));
}

TEST(RuleHelpersTest, CreatesCrossRelationJudgesInTheLocalKey) {
  // G natural (67) against Ab (68) is the 5th against the minor 6th of C
  // minor, but the tonic against its raised form in Ab major.
  std::vector<NoteEvent> placed = {makeNote(0, kTicksPerBeat * 2, 67, 0)};
  HarmonicPlan c_minor;
  c_minor.tonic_pc = 0;
  c_minor.is_minor = true;
  EXPECT_FALSE(createsCrossRelation(placed, 1, 68, 0, c_minor));
  EXPECT_TRUE(createsCrossRelation(placed, 1, 68, 0, cMajor()));

  // After a modulation to C minor the same pair is judged in the new key.
  HarmonicPlan modulating = cMajor();
  modulating.modulations.push_back({kTicksPerBeat, 0, 0, false, true, ModulationType::Phrase});
  EXPECT_TRUE(createsCrossRelation(placed, 1, 68, 0, modulating));
  EXPECT_FALSE(createsCrossRelation(placed, 1, 68, kTicksPerBeat, modulating));
}

// Melodic-interval rules (shared with the CandidateSearch pre-filter and the
// Validator). The union helper must flag exactly the leaps the Validator
// forbids for Compose notes.
TEST(RuleHelpersTest, ForbiddenMelodicLeapFlagsTritoneAugmentedDiminished) {
  const HarmonicPlan plan = cMajor();
  // Tritone F4(65) -> B4(71): 6 semis. Forbidden (aug 4th / dim 5th).
  EXPECT_TRUE(isForbiddenMelodicLeap(65, 71, plan));
  EXPECT_TRUE(isAugmentedMelodicInterval(65, 71, plan));
  EXPECT_TRUE(isDiminishedMelodicInterval(65, 71));
  // Major 7th C4(60) -> B4(71): 11 semis. Diminished-octave spelling, forbidden.
  EXPECT_TRUE(isForbiddenMelodicLeap(60, 71, plan));
  EXPECT_TRUE(isDiminishedMelodicInterval(60, 71));
  // Augmented 2nd Ab4(68) -> B4(71) in C major: 3 semis with a non-diatonic
  // endpoint (Ab -> scaleIndex == -1), which the rule treats as forbidden.
  EXPECT_TRUE(isAugmentedMelodicInterval(68, 71, plan));
  EXPECT_TRUE(isForbiddenMelodicLeap(68, 71, plan));
}

TEST(RuleHelpersTest, ForbiddenMelodicLeapAllowsConsonantSteps) {
  const HarmonicPlan plan = cMajor();
  EXPECT_FALSE(isForbiddenMelodicLeap(60, 62, plan));  // whole step C->D
  EXPECT_FALSE(isForbiddenMelodicLeap(69, 72, plan));  // diatonic m3 A->C
  EXPECT_FALSE(isForbiddenMelodicLeap(67, 72, plan));  // P4 G->C
  EXPECT_FALSE(isForbiddenMelodicLeap(60, 67, plan));  // P5 C->G
  EXPECT_FALSE(isForbiddenMelodicLeap(60, 72, plan));  // octave
}

TEST(RuleHelpersTest, ForbiddenMelodicLeapKeepsNaturalMinorDescendingSubtonicLegal) {
  HarmonicPlan plan;
  plan.tonic_pc = 0;
  plan.is_minor = true;
  // Bb4 -> G4 is a natural-minor descending third. The old fixed
  // harmonic-minor scale rejected its Bb endpoint as an augmented interval.
  EXPECT_FALSE(isForbiddenMelodicLeap(70, 67, plan));
  // Ab4 -> B4 remains the actual harmonic-minor augmented second.
  EXPECT_TRUE(isForbiddenMelodicLeap(68, 71, plan));
}

}  // namespace bach::composer::rule_helpers
