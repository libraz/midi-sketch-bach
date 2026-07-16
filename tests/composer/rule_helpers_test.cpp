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

TEST(RuleHelpersTest, IsCrossRelationPcDetectsChromaticPairs) {
  EXPECT_TRUE(isCrossRelationPc(0, 1));
  EXPECT_TRUE(isCrossRelationPc(9, 10));
  // Natural half-steps E/F and B/C are not cross-relations.
  EXPECT_FALSE(isCrossRelationPc(4, 5));
  EXPECT_FALSE(isCrossRelationPc(11, 0));
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
  // Hidden P5: both voices move upward; the resulting interval is a
  // perfect fifth (mod 12 = 7) while the previous interval was not
  // perfect. P4 is NOT treated as perfect here (only unison/octave/P5).
  //
  // Upper (voice 0): F5 (77) → G5 (79).
  // Lower (voice 1): A3 (57) → C4 (60).
  // Prev interval (upper - lower) = 77-57 = 20 → mod 12 = 8 (m6, not perfect).
  // Now interval = 79-60 = 19 → mod 12 = 7 (P5, perfect). Similar motion.
  std::vector<NoteEvent> placed = {
      makeNote(0, kTicksPerBeat, 77, 0),
      makeNote(kTicksPerBeat, kTicksPerBeat, 79, 0),
      makeNote(0, kTicksPerBeat, 57, 1),
  };
  EXPECT_TRUE(createsHiddenParallelPerfect(placed, 1, 60, kTicksPerBeat, 57, 0));
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
  EXPECT_TRUE(createsCrossRelation(placed, 1, 61, 0));
  // Different letter half-step E↔F: not cross-relation.
  std::vector<NoteEvent> placed_ef = {makeNote(0, kTicksPerBeat * 2, 64, 0)};
  EXPECT_FALSE(createsCrossRelation(placed_ef, 1, 65, 0));
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

}  // namespace bach::composer::rule_helpers
