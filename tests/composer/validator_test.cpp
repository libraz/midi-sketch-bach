#include "composer/validator.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <vector>

#include "composer/harmonic_plan.h"
#include "composer/material.h"
#include "composer/motif_ops.h"
#include "composer/provenance.h"
#include "composer/span.h"
#include "composer/validation.h"
#include "core/basic_types.h"

namespace bach::composer {
namespace {

// Single C major chord covering the whole piece.
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

NoteEvent makeNote(Tick start, Tick dur, std::uint8_t pitch, VoiceId voice) {
  NoteEvent n;
  n.start_tick = start;
  n.duration = dur;
  n.pitch = pitch;
  n.voice = voice;
  n.velocity = 80;
  return n;
}

NoteProvenance makeProv(SpanId sid, NoteSource src) {
  NoteProvenance p;
  p.span_id = sid;
  p.source = src;
  return p;
}

bool hasRule(const ValidationReport& r, const std::string& rule_id) {
  for (const auto& f : r.failures) {
    if (f.rule_id == rule_id)
      return true;
  }
  return false;
}

HarmonicPlan cMajorWithCadence(CadenceType type) {
  HarmonicPlan plan = cMajorWhole();
  CadenceEvent cadence;
  cadence.tick = kTicksPerBeat;
  cadence.type = type;
  plan.cadences.push_back(cadence);
  return plan;
}

}  // namespace

TEST(ValidatorTest, EmptyInputPasses) {
  Validator v;
  ValidationReport r = v.validate({}, {}, cMajorWhole());
  EXPECT_EQ(r.status, ValidationStatus::Ok);
  EXPECT_TRUE(r.failures.empty());
}

TEST(ValidatorTest, StrongBeatConsonancePasses) {
  // Compose note on bar 0 beat 1, pitch = G (triad fifth of C). OK.
  std::vector<NoteEvent> notes = {makeNote(0, kTicksPerBeat, 67, 0)};
  std::vector<NoteProvenance> prov = {makeProv(0, NoteSource::Compose)};
  ValidationReport r = Validator{}.validate(notes, prov, cMajorWhole());
  EXPECT_EQ(r.status, ValidationStatus::Ok);
}

TEST(ValidatorTest, StrongBeatDissonanceFailsForComposeSource) {
  // F# (pc 6) on bar 0 beat 1 over C major triad {0,4,7}. Dissonant.
  std::vector<NoteEvent> notes = {makeNote(0, kTicksPerBeat, 66, 0)};
  std::vector<NoteProvenance> prov = {makeProv(0, NoteSource::Compose)};
  ValidationReport r = Validator{}.validate(notes, prov, cMajorWhole());
  EXPECT_EQ(r.status, ValidationStatus::FailedSpan);
  EXPECT_TRUE(hasRule(r, "strong_beat_dissonance"));
}

TEST(ValidatorTest, StrongBeatDissonanceIgnoredForMaterialSource) {
  // Same dissonance as above, but the note is sourced from Material. The
  // validator must not blame Material (those pitches are inputs, not search
  // outputs).
  std::vector<NoteEvent> notes = {makeNote(0, kTicksPerBeat, 66, 0)};
  std::vector<NoteProvenance> prov = {makeProv(0, NoteSource::Material)};
  ValidationReport r = Validator{}.validate(notes, prov, cMajorWhole());
  EXPECT_EQ(r.status, ValidationStatus::Ok);
}

TEST(ValidatorTest, ParallelFifthFails) {
  // Two voices, two beats. Voice 0: C5 → D5. Voice 1: F4 → G4.
  // Intervals: 60-53=7 (P5), 62-55=7 (P5). Identical perfect, both moved.
  std::vector<NoteEvent> notes = {
      makeNote(0, kTicksPerBeat, 60, 0),
      makeNote(0, kTicksPerBeat, 53, 1),
      makeNote(kTicksPerBeat, kTicksPerBeat, 62, 0),
      makeNote(kTicksPerBeat, kTicksPerBeat, 55, 1),
  };
  std::vector<NoteProvenance> prov(notes.size(), makeProv(0, NoteSource::Compose));
  ValidationReport r = Validator{}.validate(notes, prov, cMajorWhole());
  EXPECT_EQ(r.status, ValidationStatus::FailedSpan);
  EXPECT_TRUE(hasRule(r, "parallel_fifth"));
}

TEST(ValidatorTest, ParallelOctaveFails) {
  // Voice 0: C5 → D5 (60→62). Voice 1: C4 → D4 (48→50).
  // Intervals: 12, 12. Same perfect octave, both moved.
  std::vector<NoteEvent> notes = {
      makeNote(0, kTicksPerBeat, 60, 0),
      makeNote(0, kTicksPerBeat, 48, 1),
      makeNote(kTicksPerBeat, kTicksPerBeat, 62, 0),
      makeNote(kTicksPerBeat, kTicksPerBeat, 50, 1),
  };
  std::vector<NoteProvenance> prov(notes.size(), makeProv(0, NoteSource::Compose));
  ValidationReport r = Validator{}.validate(notes, prov, cMajorWhole());
  EXPECT_EQ(r.status, ValidationStatus::FailedSpan);
  EXPECT_TRUE(hasRule(r, "parallel_octave"));
}

TEST(ValidatorTest, OneStationaryVoicePasses) {
  // P5 → P5 but lower voice does not move (interval stays at 7 only because
  // the upper one moves to a different note that recreates a P5 — but here
  // the lower is held, so the interval changes). Specifically: upper C5→C5
  // (held), lower F3→F3 → interval is the same but neither voice moved.
  // That's a held P5, not a parallel motion. Detection requires *both* to
  // move; the current validator's heuristic (identical interval with both
  // changed positions) does not flag this.
  //
  // We model this by having upper hold a C5 across two beats while lower
  // holds an F3. Two distinct notes per voice with the same pitch — the
  // pitch-at-tick lookup yields {C5,F3} then {C5,F3}, identical interval.
  // Both endpoints contribute the SAME interval; the validator must not
  // confuse "held = parallel motion".
  std::vector<NoteEvent> notes = {
      makeNote(0, kTicksPerBeat, 60, 0),
      makeNote(0, kTicksPerBeat, 53, 1),
      makeNote(kTicksPerBeat, kTicksPerBeat, 60, 0),
      makeNote(kTicksPerBeat, kTicksPerBeat, 53, 1),
  };
  std::vector<NoteProvenance> prov(notes.size(), makeProv(0, NoteSource::Compose));
  // Use bar 2 onwards so strong-beat consonance is fine for both notes;
  // we already start at tick 0 which is also a strong beat. C5 (pc 0) is
  // root of C major triad, so consonant. F3 (pc 5) however is NOT in the
  // triad — to isolate the parallel-motion check, mark F3 voice as
  // Material so strong-beat-dissonance doesn't fire.
  prov[1].source = NoteSource::Material;
  prov[3].source = NoteSource::Material;
  ValidationReport r = Validator{}.validate(notes, prov, cMajorWhole());
  EXPECT_FALSE(hasRule(r, "parallel_fifth"));
  EXPECT_FALSE(hasRule(r, "parallel_octave"));
}

TEST(ValidatorTest, CleanVoiceOrderingPasses) {
  // Voice 0 (soprano) above voice 1 (alto): C5 over E4.
  std::vector<NoteEvent> notes = {
      makeNote(0, kTicksPerBeat, 60, 0),
      makeNote(0, kTicksPerBeat, 52, 1),
  };
  std::vector<NoteProvenance> prov(notes.size(), makeProv(0, NoteSource::Compose));
  ValidationReport r = Validator{}.validate(notes, prov, cMajorWhole());
  EXPECT_FALSE(hasRule(r, "voice_crossing"));
}

TEST(ValidatorTest, VoiceCrossingFails) {
  // Voice 0 (soprano) BELOW voice 1 (alto): E4 (52) under G4 (55).
  // By convention voice 0 should be above voice 1.
  std::vector<NoteEvent> notes = {
      makeNote(0, kTicksPerBeat, 52, 0),
      makeNote(0, kTicksPerBeat, 55, 1),
  };
  std::vector<NoteProvenance> prov(notes.size(), makeProv(0, NoteSource::Compose));
  ValidationReport r = Validator{}.validate(notes, prov, cMajorWhole());
  EXPECT_EQ(r.status, ValidationStatus::FailedSpan);
  EXPECT_TRUE(hasRule(r, "voice_crossing"));
}

TEST(ValidatorTest, VoiceCrossingIgnoredWhenOneVoiceSilent) {
  // Only voice 0 sounds at tick 0. No crossing to detect.
  std::vector<NoteEvent> notes = {
      makeNote(0, kTicksPerBeat, 60, 0),
      makeNote(kTicksPerBeat, kTicksPerBeat, 55, 1),
  };
  std::vector<NoteProvenance> prov(notes.size(), makeProv(0, NoteSource::Compose));
  ValidationReport r = Validator{}.validate(notes, prov, cMajorWhole());
  EXPECT_FALSE(hasRule(r, "voice_crossing"));
}

TEST(ValidatorTest, SingleLeapAlonePasses) {
  // C4 -> G4 is a P5 (7 semis), but it's the only motion. No
  // consecutive_leaps trigger.
  std::vector<NoteEvent> notes = {
      makeNote(0, kTicksPerBeat, 60, 0),
      makeNote(kTicksPerBeat, kTicksPerBeat, 67, 0),
  };
  std::vector<NoteProvenance> prov(notes.size(), makeProv(0, NoteSource::Compose));
  ValidationReport r = Validator{}.validate(notes, prov, cMajorWhole());
  EXPECT_FALSE(hasRule(r, "consecutive_leaps"));
}

TEST(ValidatorTest, LeapFollowedByStepPasses) {
  // C4 -> G4 (P5) -> A4 (step). Leap then step. OK.
  std::vector<NoteEvent> notes = {
      makeNote(0, kTicksPerBeat, 60, 0),
      makeNote(kTicksPerBeat, kTicksPerBeat, 67, 0),
      makeNote(2 * kTicksPerBeat, kTicksPerBeat, 69, 0),
  };
  std::vector<NoteProvenance> prov(notes.size(), makeProv(0, NoteSource::Compose));
  ValidationReport r = Validator{}.validate(notes, prov, cMajorWhole());
  EXPECT_FALSE(hasRule(r, "consecutive_leaps"));
}

TEST(ValidatorTest, ConsecutiveLeapsFails) {
  // C4 -> G4 (P5, 7 semis) -> D5 (P5, 7 semis). Two leaps in a row.
  std::vector<NoteEvent> notes = {
      makeNote(0, kTicksPerBeat, 60, 0),
      makeNote(kTicksPerBeat, kTicksPerBeat, 67, 0),
      makeNote(2 * kTicksPerBeat, kTicksPerBeat, 74, 0),
  };
  std::vector<NoteProvenance> prov(notes.size(), makeProv(0, NoteSource::Compose));
  ValidationReport r = Validator{}.validate(notes, prov, cMajorWhole());
  EXPECT_EQ(r.status, ValidationStatus::FailedSpan);
  EXPECT_TRUE(hasRule(r, "consecutive_leaps"));
}

TEST(ValidatorTest, ConsecutiveLeapsAcrossVoicesNotAggregated) {
  // Each voice has only one note, so no triple exists. No leap rule
  // can fire even though pitch differences span two voices.
  std::vector<NoteEvent> notes = {
      makeNote(0, kTicksPerBeat, 60, 0),
      makeNote(0, kTicksPerBeat, 53, 1),
  };
  std::vector<NoteProvenance> prov(notes.size(), makeProv(0, NoteSource::Compose));
  ValidationReport r = Validator{}.validate(notes, prov, cMajorWhole());
  EXPECT_FALSE(hasRule(r, "consecutive_leaps"));
}

TEST(ValidatorTest, WeakBeatPassingToneStepApproachAndLeavePasses) {
  // Voice 0: C5 (60, triad) -> D5 (62, non-triad on weak beat) -> E5
  // (64, triad). Both intervals are 2 semis (steps). D5 is a clean
  // passing tone — must not be flagged.
  std::vector<NoteEvent> notes = {
      makeNote(0, kTicksPerBeat, 60, 0),
      makeNote(kTicksPerBeat, kTicksPerBeat, 62, 0),
      makeNote(2 * kTicksPerBeat, kTicksPerBeat, 64, 0),
  };
  std::vector<NoteProvenance> prov(notes.size(), makeProv(0, NoteSource::Compose));
  ValidationReport r = Validator{}.validate(notes, prov, cMajorWhole());
  EXPECT_FALSE(hasRule(r, "unprepared_dissonance"));
}

TEST(ValidatorTest, WeakBeatNonChordToneLeapApproachFails) {
  // Voice 0: C5 (60, triad) -> A5 (69, non-triad on weak beat) -> G5
  // (67, triad). Approach is a leap of 9 semis (M6). The non-triad
  // middle note is not properly prepared.
  std::vector<NoteEvent> notes = {
      makeNote(0, kTicksPerBeat, 60, 0),
      makeNote(kTicksPerBeat, kTicksPerBeat, 69, 0),
      makeNote(2 * kTicksPerBeat, kTicksPerBeat, 67, 0),
  };
  std::vector<NoteProvenance> prov(notes.size(), makeProv(0, NoteSource::Compose));
  ValidationReport r = Validator{}.validate(notes, prov, cMajorWhole());
  EXPECT_EQ(r.status, ValidationStatus::FailedSpan);
  EXPECT_TRUE(hasRule(r, "unprepared_dissonance"));
}

TEST(ValidatorTest, WeakBeatNonChordToneLeapLeaveFails) {
  // Approach is OK (step), but leave is a leap. Still flagged.
  std::vector<NoteEvent> notes = {
      makeNote(0, kTicksPerBeat, 60, 0),
      makeNote(kTicksPerBeat, kTicksPerBeat, 62, 0),
      makeNote(2 * kTicksPerBeat, kTicksPerBeat, 69, 0),
  };
  std::vector<NoteProvenance> prov(notes.size(), makeProv(0, NoteSource::Compose));
  ValidationReport r = Validator{}.validate(notes, prov, cMajorWhole());
  EXPECT_TRUE(hasRule(r, "unprepared_dissonance"));
}

TEST(ValidatorTest, VoiceBoundaryNonChordToneExempt) {
  // The last note in a voice has no next; the passing-tone rule must
  // not fire when either prev or next is missing.
  std::vector<NoteEvent> notes = {
      makeNote(0, kTicksPerBeat, 60, 0),
      makeNote(kTicksPerBeat, kTicksPerBeat, 62, 0),
  };
  std::vector<NoteProvenance> prov(notes.size(), makeProv(0, NoteSource::Compose));
  ValidationReport r = Validator{}.validate(notes, prov, cMajorWhole());
  EXPECT_FALSE(hasRule(r, "unprepared_dissonance"));
}

TEST(ValidatorTest, WeakBeatNonChordToneOnMaterialSourceIgnored) {
  // Material-source notes are inputs, not search outputs. They must
  // not be subjected to candidate-search rules even when a dissonance
  // would otherwise trigger.
  std::vector<NoteEvent> notes = {
      makeNote(0, kTicksPerBeat, 60, 0),
      makeNote(kTicksPerBeat, kTicksPerBeat, 69, 0),
      makeNote(2 * kTicksPerBeat, kTicksPerBeat, 67, 0),
  };
  std::vector<NoteProvenance> prov(notes.size(), makeProv(0, NoteSource::Compose));
  prov[1].source = NoteSource::Material;  // the offending middle note
  ValidationReport r = Validator{}.validate(notes, prov, cMajorWhole());
  EXPECT_FALSE(hasRule(r, "unprepared_dissonance"));
}

TEST(ValidatorTest, VerticalConsonancePasses) {
  // Voice 0 = C5 (60), voice 1 = E4 (52). Interval 8 (m6) is
  // consonant. Strong beat.
  std::vector<NoteEvent> notes = {
      makeNote(0, kTicksPerBeat, 60, 0),
      makeNote(0, kTicksPerBeat, 52, 1),
  };
  std::vector<NoteProvenance> prov(notes.size(), makeProv(0, NoteSource::Compose));
  ValidationReport r = Validator{}.validate(notes, prov, cMajorWhole());
  EXPECT_FALSE(hasRule(r, "vertical_dissonance"));
}

TEST(ValidatorTest, VerticalDissonanceOnStrongBeatFails) {
  // Voice 0 = G5 (79, Material), voice 1 = F4 (65, Compose). Interval
  // 14 -> 2 (M2) is dissonant. Strong beat. The Compose side gets the
  // blame.
  std::vector<NoteEvent> notes = {
      makeNote(0, kTicksPerBeat, 79, 0),
      makeNote(0, kTicksPerBeat, 65, 1),
  };
  std::vector<NoteProvenance> prov = {
      makeProv(0, NoteSource::Material),
      makeProv(1, NoteSource::Compose),
  };
  ValidationReport r = Validator{}.validate(notes, prov, cMajorWhole());
  EXPECT_EQ(r.status, ValidationStatus::FailedSpan);
  EXPECT_TRUE(hasRule(r, "vertical_dissonance"));
  // The flagged failure must reference the Compose-source span (id 1).
  bool found_compose_blame = false;
  for (const auto& f : r.failures) {
    if (f.rule_id == "vertical_dissonance" && f.span_id == 1) {
      found_compose_blame = true;
    }
  }
  EXPECT_TRUE(found_compose_blame);
}

TEST(ValidatorTest, VerticalDissonanceBothMaterialNotFlagged) {
  // Both notes are Material. Even though they form M2, the composer
  // cannot fix fixed inputs, so no failure is emitted.
  std::vector<NoteEvent> notes = {
      makeNote(0, kTicksPerBeat, 79, 0),
      makeNote(0, kTicksPerBeat, 65, 1),
  };
  std::vector<NoteProvenance> prov = {
      makeProv(0, NoteSource::Material),
      makeProv(1, NoteSource::Material),
  };
  ValidationReport r = Validator{}.validate(notes, prov, cMajorWhole());
  EXPECT_FALSE(hasRule(r, "vertical_dissonance"));
}

TEST(ValidatorTest, VerticalDissonanceOnWeakBeatNotFlagged) {
  // Dissonant pair on a weak beat is not flagged by the vertical
  // rule (weak-beat dissonance is governed by rule 4, not rule 5).
  std::vector<NoteEvent> notes = {
      makeNote(kTicksPerBeat, kTicksPerBeat, 79, 0),
      makeNote(kTicksPerBeat, kTicksPerBeat, 65, 1),
  };
  std::vector<NoteProvenance> prov(notes.size(), makeProv(0, NoteSource::Compose));
  ValidationReport r = Validator{}.validate(notes, prov, cMajorWhole());
  EXPECT_FALSE(hasRule(r, "vertical_dissonance"));
}

TEST(ValidatorTest, ContraryMotionThroughPerfectIsClean) {
  // Voice 0: C5 → G5 (60→67, +7). Voice 1: G3 → C3 (55→48, -7).
  // Intervals at the two ticks: 60-55=5 (P4), 67-48=19 ≡ 7 mod 12 (P5).
  // Different intervals → no parallel.
  std::vector<NoteEvent> notes = {
      makeNote(0, kTicksPerBeat, 60, 0),
      makeNote(0, kTicksPerBeat, 55, 1),
      makeNote(kTicksPerBeat, kTicksPerBeat, 67, 0),
      makeNote(kTicksPerBeat, kTicksPerBeat, 48, 1),
  };
  std::vector<NoteProvenance> prov(notes.size(), makeProv(0, NoteSource::Compose));
  ValidationReport r = Validator{}.validate(notes, prov, cMajorWhole());
  EXPECT_FALSE(hasRule(r, "parallel_fifth"));
  EXPECT_FALSE(hasRule(r, "parallel_octave"));
}

TEST(ValidatorTest, AugmentedMelodicIntervalFails) {
  std::vector<NoteEvent> notes = {
      makeNote(0, kTicksPerBeat, 60, 0),
      makeNote(kTicksPerBeat, kTicksPerBeat, 63, 0),
  };
  std::vector<NoteProvenance> prov(notes.size(), makeProv(0, NoteSource::Compose));
  ValidationReport r = Validator{}.validate(notes, prov, cMajorWhole());
  EXPECT_TRUE(hasRule(r, "augmented_melodic"));
}

TEST(ValidatorTest, TritoneMelodicFails) {
  std::vector<NoteEvent> notes = {
      makeNote(0, kTicksPerBeat, 60, 0),
      makeNote(kTicksPerBeat, kTicksPerBeat, 66, 0),
  };
  std::vector<NoteProvenance> prov(notes.size(), makeProv(0, NoteSource::Compose));
  ValidationReport r = Validator{}.validate(notes, prov, cMajorWhole());
  EXPECT_TRUE(hasRule(r, "tritone_melodic"));
}

TEST(ValidatorTest, DiminishedMelodicIntervalFails) {
  std::vector<NoteEvent> notes = {
      makeNote(0, kTicksPerBeat, 72, 0),
      makeNote(kTicksPerBeat, kTicksPerBeat, 61, 0),
  };
  std::vector<NoteProvenance> prov(notes.size(), makeProv(0, NoteSource::Compose));
  ValidationReport r = Validator{}.validate(notes, prov, cMajorWhole());
  EXPECT_TRUE(hasRule(r, "diminished_melodic"));
}

TEST(ValidatorTest, LeadingToneResolutionPassesWhenRaisedToTonic) {
  std::vector<NoteEvent> notes = {
      makeNote(0, kTicksPerBeat, 71, 0),
      makeNote(kTicksPerBeat, kTicksPerBeat, 72, 0),
  };
  std::vector<NoteProvenance> prov(notes.size(), makeProv(0, NoteSource::Compose));
  ValidationReport r = Validator{}.validate(notes, prov, cMajorWhole());
  EXPECT_FALSE(hasRule(r, "leading_tone_resolution"));
}

TEST(ValidatorTest, LeadingToneResolutionFailsWhenUnresolved) {
  std::vector<NoteEvent> notes = {
      makeNote(0, kTicksPerBeat, 71, 0),
      makeNote(kTicksPerBeat, kTicksPerBeat, 69, 0),
  };
  std::vector<NoteProvenance> prov(notes.size(), makeProv(0, NoteSource::Compose));
  ValidationReport r = Validator{}.validate(notes, prov, cMajorWhole());
  EXPECT_TRUE(hasRule(r, "leading_tone_resolution"));
}

TEST(ValidatorTest, HiddenParallelFifthFails) {
  std::vector<NoteEvent> notes = {
      makeNote(0, kTicksPerBeat, 60, 0),
      makeNote(0, kTicksPerBeat, 52, 1),
      makeNote(kTicksPerBeat, kTicksPerBeat, 62, 0),
      makeNote(kTicksPerBeat, kTicksPerBeat, 55, 1),
  };
  std::vector<NoteProvenance> prov(notes.size(), makeProv(0, NoteSource::Compose));
  ValidationReport r = Validator{}.validate(notes, prov, cMajorWhole());
  EXPECT_TRUE(hasRule(r, "hidden_parallel_fifth"));
}

TEST(ValidatorTest, HiddenParallelOctaveFails) {
  std::vector<NoteEvent> notes = {
      makeNote(0, kTicksPerBeat, 60, 0),
      makeNote(0, kTicksPerBeat, 45, 1),
      makeNote(kTicksPerBeat, kTicksPerBeat, 62, 0),
      makeNote(kTicksPerBeat, kTicksPerBeat, 50, 1),
  };
  std::vector<NoteProvenance> prov(notes.size(), makeProv(0, NoteSource::Compose));
  ValidationReport r = Validator{}.validate(notes, prov, cMajorWhole());
  EXPECT_TRUE(hasRule(r, "hidden_parallel_octave"));
}

TEST(ValidatorTest, ContraryMotionIntoPerfectIsNotHiddenParallel) {
  std::vector<NoteEvent> notes = {
      makeNote(0, kTicksPerBeat, 60, 0),
      makeNote(0, kTicksPerBeat, 52, 1),
      makeNote(kTicksPerBeat, kTicksPerBeat, 62, 0),
      makeNote(kTicksPerBeat, kTicksPerBeat, 55, 1),
  };
  notes[3].pitch =
      67;  // lower-index convention aside, the pair no longer approaches by same direction.
  std::vector<NoteProvenance> prov(notes.size(), makeProv(0, NoteSource::Compose));
  ValidationReport r = Validator{}.validate(notes, prov, cMajorWhole());
  EXPECT_FALSE(hasRule(r, "hidden_parallel_fifth"));
  EXPECT_FALSE(hasRule(r, "hidden_parallel_octave"));
}

TEST(ValidatorTest, CrossRelationSimultaneousFails) {
  std::vector<NoteEvent> notes = {
      makeNote(0, kTicksPerBeat, 65, 0),  // F
      makeNote(0, kTicksPerBeat, 66, 1),  // F#
  };
  std::vector<NoteProvenance> prov(notes.size(), makeProv(0, NoteSource::Compose));
  ValidationReport r = Validator{}.validate(notes, prov, cMajorWhole());
  EXPECT_TRUE(hasRule(r, "cross_relation"));
}

TEST(ValidatorTest, CrossRelationAdjacentFails) {
  std::vector<NoteEvent> notes = {
      makeNote(0, kTicksPerBeat, 65, 0),
      makeNote(kTicksPerBeat, kTicksPerBeat, 66, 1),
  };
  std::vector<NoteProvenance> prov(notes.size(), makeProv(0, NoteSource::Compose));
  ValidationReport r = Validator{}.validate(notes, prov, cMajorWhole());
  EXPECT_TRUE(hasRule(r, "cross_relation"));
}

TEST(ValidatorTest, NaturalHalfStepIsNotCrossRelation) {
  std::vector<NoteEvent> notes = {
      makeNote(0, kTicksPerBeat, 64, 0),  // E
      makeNote(0, kTicksPerBeat, 65, 1),  // F
  };
  std::vector<NoteProvenance> prov(notes.size(), makeProv(0, NoteSource::Compose));
  ValidationReport r = Validator{}.validate(notes, prov, cMajorWhole());
  EXPECT_FALSE(hasRule(r, "cross_relation"));
}

TEST(ValidatorTest, CadenceVoiceLeadingAcceptsAllCadenceTypes) {
  struct Case {
    CadenceType type;
    std::uint8_t upper_prev;
    std::uint8_t upper_now;
    std::uint8_t bass_prev;
    std::uint8_t bass_now;
  };
  const std::vector<Case> cases = {
      {CadenceType::Perfect, 71, 72, 55, 48},   {CadenceType::ImperfectAuthentic, 62, 64, 55, 48},
      {CadenceType::Plagal, 65, 64, 53, 48},    {CadenceType::Half, 64, 62, 48, 55},
      {CadenceType::Deceptive, 71, 72, 55, 57}, {CadenceType::Phrygian, 61, 62, 56, 55},
  };

  for (const auto& c : cases) {
    std::vector<NoteEvent> notes = {
        makeNote(0, kTicksPerBeat, c.upper_prev, 0),
        makeNote(0, kTicksPerBeat, c.bass_prev, 1),
        makeNote(kTicksPerBeat, kTicksPerBeat, c.upper_now, 0),
        makeNote(kTicksPerBeat, kTicksPerBeat, c.bass_now, 1),
    };
    std::vector<NoteProvenance> prov(notes.size(), makeProv(0, NoteSource::Material));
    ValidationReport r = Validator{}.validate(notes, prov, cMajorWithCadence(c.type));
    EXPECT_FALSE(hasRule(r, "cadence_voice_leading")) << static_cast<int>(c.type);
  }
}

TEST(ValidatorTest, CadenceVoiceLeadingFailsBrokenPac) {
  std::vector<NoteEvent> notes = {
      makeNote(0, kTicksPerBeat, 69, 0),
      makeNote(0, kTicksPerBeat, 55, 1),
      makeNote(kTicksPerBeat, kTicksPerBeat, 72, 0),
      makeNote(kTicksPerBeat, kTicksPerBeat, 48, 1),
  };
  std::vector<NoteProvenance> prov(notes.size(), makeProv(0, NoteSource::Material));
  ValidationReport r = Validator{}.validate(notes, prov, cMajorWithCadence(CadenceType::Perfect));
  EXPECT_EQ(r.status, ValidationStatus::FailedSpan);
  EXPECT_TRUE(hasRule(r, "cadence_voice_leading"));
}

TEST(ValidatorTest, CadenceMissingVoicesClassifiedStructural) {
  // Single-voice piece with a cadence event triggers the
  // voices.size() < 2 precondition path, which is StructuralFail.
  std::vector<NoteEvent> notes = {
      makeNote(0, kTicksPerBeat, 72, 0),
      makeNote(kTicksPerBeat, kTicksPerBeat, 71, 0),
  };
  std::vector<NoteProvenance> prov(notes.size(), makeProv(0, NoteSource::Material));
  ValidationReport r = Validator{}.validate(notes, prov, cMajorWithCadence(CadenceType::Perfect));
  EXPECT_EQ(r.status, ValidationStatus::FailedSpan);
  bool found_structural = false;
  for (const auto& f : r.failures) {
    if (f.rule_id == "cadence_voice_leading") {
      EXPECT_EQ(f.kind, FailKind::StructuralFail);
      found_structural = true;
    }
  }
  EXPECT_TRUE(found_structural);
}

TEST(ValidatorTest, MusicalViolationClassifiedMusical) {
  // F# (pc 6) on strong beat over C major is a strong_beat_dissonance.
  // Default classification is MusicalFail.
  std::vector<NoteEvent> notes = {makeNote(0, kTicksPerBeat, 66, 0)};
  std::vector<NoteProvenance> prov = {makeProv(0, NoteSource::Compose)};
  ValidationReport r = Validator{}.validate(notes, prov, cMajorWhole());
  ASSERT_FALSE(r.failures.empty());
  bool found_musical = false;
  for (const auto& f : r.failures) {
    if (f.rule_id == "strong_beat_dissonance") {
      EXPECT_EQ(f.kind, FailKind::MusicalFail);
      found_musical = true;
    }
  }
  EXPECT_TRUE(found_musical);
}

namespace {

// Build a minimal two-voice (V0 upper, V1 bass) layout with a single
// Sus4_3 in V0 against a C in V1. Beats: prep=beat0 (G over C = P5
// consonant), suspension=beat1 (G held = 4th over C = dissonant),
// resolution=beat2 (F-down... actually 4-3 means 4→3 so G→F-natural is
// step-down of -2 semis, F over C = 4th). For simplicity place V1's
// pitch class at G's preparation point so the preparation is consonant.
HarmonicPlan cMajorTwoBars() {
  HarmonicPlan plan;
  plan.tonic_pc = 0;
  plan.is_minor = false;
  ChordEvent c0;
  c0.start_tick = 0;
  c0.root_pc = 0;
  c0.quality = ChordQuality::Major;
  plan.chords.push_back(c0);
  return plan;
}

}  // namespace

TEST(ValidatorTest, SuspensionPreparationAndResolutionAcceptValidPattern) {
  // Sus7_6 in V0 over V1 bass at C: prep=B (M7-ish? we use a consonance
  // for prep). Concretely: V0 plays D5 (62) at beat 0 over C3 (48): M10
  // (semis = 14, mod 12 = 2)... pick consonant. Use V0=A4 (69) over
  // V1=C3 (48): interval 21 mod 12 = 9 (M6, consonant) — OK prep.
  // Tie A4 into beat 1 = suspension over the bass C3 → m7-ish (21 →
  // dissonant). Resolution: A4 → G4 (67) — step down of 2 semis. This
  // is a 7-6 pattern (m7 → m6).
  std::vector<NoteEvent> notes = {
      makeNote(0, kTicksPerBeat, 69, 0),                  // V0 prep A4
      makeNote(kTicksPerBeat, kTicksPerBeat, 69, 0),      // V0 suspension A4
      makeNote(kTicksPerBeat * 2, kTicksPerBeat, 67, 0),  // V0 resolution G4
      makeNote(0, kTicksPerBeat * 3, 48, 1),              // V1 bass C3 (held)
  };
  std::vector<NoteProvenance> prov(notes.size(), makeProv(0, NoteSource::Material));
  Material material;
  SuspensionPattern sp;
  sp.type = SuspensionType::Sus7_6;
  sp.preparation_tick = 0;
  sp.suspension_tick = kTicksPerBeat;
  sp.resolution_tick = kTicksPerBeat * 2;
  sp.preparation_pitch = 69;
  sp.suspension_pitch = 69;
  sp.resolution_pitch = 67;
  sp.voice = 0;
  material.suspension_patterns.push_back(sp);
  ValidationReport r = Validator{}.validate(notes, prov, cMajorTwoBars(), material);
  EXPECT_FALSE(hasRule(r, "suspension_preparation"));
  EXPECT_FALSE(hasRule(r, "suspension_resolution_step_down"));
}

TEST(ValidatorTest, SuspensionResolutionFailsOnLeap) {
  // Sus7_6 with V0 prep=A4 → sus=A4 → resolution=D4 (62), a 7-semi leap
  // instead of a step. Resolution rule must fire.
  std::vector<NoteEvent> notes = {
      makeNote(0, kTicksPerBeat, 69, 0),
      makeNote(kTicksPerBeat, kTicksPerBeat, 69, 0),
      makeNote(kTicksPerBeat * 2, kTicksPerBeat, 62, 0),
      makeNote(0, kTicksPerBeat * 3, 48, 1),
  };
  std::vector<NoteProvenance> prov(notes.size(), makeProv(0, NoteSource::Material));
  Material material;
  SuspensionPattern sp;
  sp.type = SuspensionType::Sus7_6;
  sp.preparation_tick = 0;
  sp.suspension_tick = kTicksPerBeat;
  sp.resolution_tick = kTicksPerBeat * 2;
  sp.preparation_pitch = 69;
  sp.suspension_pitch = 69;
  sp.resolution_pitch = 62;
  sp.voice = 0;
  material.suspension_patterns.push_back(sp);
  ValidationReport r = Validator{}.validate(notes, prov, cMajorTwoBars(), material);
  EXPECT_EQ(r.status, ValidationStatus::FailedSpan);
  EXPECT_TRUE(hasRule(r, "suspension_resolution_step_down"));
}

TEST(ValidatorTest, SuspensionPreparationFailsWhenNotTied) {
  // Sus7_6 declared prep=A4 but the V0 prep note actually plays G4 (67)
  // and only jumps to A4 on the suspension — no tie. Preparation rule
  // must fire because prep_actual (G4) != sus_actual (A4).
  std::vector<NoteEvent> notes = {
      makeNote(0, kTicksPerBeat, 67, 0),                  // prep G4 (NOT tied)
      makeNote(kTicksPerBeat, kTicksPerBeat, 69, 0),      // sus A4
      makeNote(kTicksPerBeat * 2, kTicksPerBeat, 67, 0),  // res G4
      makeNote(0, kTicksPerBeat * 3, 48, 1),
  };
  std::vector<NoteProvenance> prov(notes.size(), makeProv(0, NoteSource::Material));
  Material material;
  SuspensionPattern sp;
  sp.type = SuspensionType::Sus7_6;
  sp.preparation_tick = 0;
  sp.suspension_tick = kTicksPerBeat;
  sp.resolution_tick = kTicksPerBeat * 2;
  sp.preparation_pitch = 67;
  sp.suspension_pitch = 69;
  sp.resolution_pitch = 67;
  sp.voice = 0;
  material.suspension_patterns.push_back(sp);
  ValidationReport r = Validator{}.validate(notes, prov, cMajorTwoBars(), material);
  EXPECT_EQ(r.status, ValidationStatus::FailedSpan);
  EXPECT_TRUE(hasRule(r, "suspension_preparation"));
}

TEST(ValidatorTest, Sus2_3RequiresUpwardStep) {
  // Sus2_3 is the bass-rising flavor: suspension → resolution moves UP
  // by step. A downward resolution must fail. V0=C5 (held), V1 prep=B3
  // (59) → sus=B3 → res=C4 (60, upward step). prep B3 against C5 →
  // m7 ish; we pick a consonance for prep: V0 plays D5 (62) instead so
  // interval(B3, D5) = 38 → mod 12 = 2 (M2 = dissonant).
  // Simpler: V0=E5 (76) over V1 prep B3 (59): interval 17 mod 12 = 5
  // (P4) — also context-sensitive consonant. Use V0=G5 (79) → interval
  // 20 mod 12 = 8 (m6, consonant). OK.
  std::vector<NoteEvent> notes = {
      makeNote(0, kTicksPerBeat * 3, 79, 0),              // V0 G5 held
      makeNote(0, kTicksPerBeat, 59, 1),                  // V1 prep B3
      makeNote(kTicksPerBeat, kTicksPerBeat, 59, 1),      // V1 sus B3
      makeNote(kTicksPerBeat * 2, kTicksPerBeat, 60, 1),  // V1 res C4 (up)
  };
  std::vector<NoteProvenance> prov(notes.size(), makeProv(0, NoteSource::Material));
  Material material;
  SuspensionPattern sp;
  sp.type = SuspensionType::Sus2_3;
  sp.preparation_tick = 0;
  sp.suspension_tick = kTicksPerBeat;
  sp.resolution_tick = kTicksPerBeat * 2;
  sp.preparation_pitch = 59;
  sp.suspension_pitch = 59;
  sp.resolution_pitch = 60;
  sp.voice = 1;
  material.suspension_patterns.push_back(sp);
  ValidationReport r = Validator{}.validate(notes, prov, cMajorTwoBars(), material);
  EXPECT_FALSE(hasRule(r, "suspension_resolution_step_down"));
}

// P5 Episode motif-derivation tests. Subject = C4-D4-E4-F4 in voice 0;
// Episode span in voice 1 is expected to replay one of the five
// transforms. Validator must accept byte-matching notes and fail on
// any mismatch.

namespace {

MaterialNote subjectNote(Tick start, Tick dur, std::uint8_t pitch) {
  MaterialNote n;
  n.start_tick = start;
  n.duration = dur;
  n.pitch = pitch;
  return n;
}

Material subjectAscendingScale() {
  Material m;
  m.subject = {subjectNote(0, kTicksPerBeat, 60), subjectNote(kTicksPerBeat, kTicksPerBeat, 62),
               subjectNote(2 * kTicksPerBeat, kTicksPerBeat, 64),
               subjectNote(3 * kTicksPerBeat, kTicksPerBeat, 65)};
  return m;
}

EpisodeFragment makeEpisode(motif_ops::EpisodeMotifTransform transform, VoiceId voice,
                            Tick target_start) {
  EpisodeFragment f;
  f.transform = static_cast<std::uint8_t>(transform);
  f.source_start_index = 0;
  f.source_count = 0;
  f.voice = voice;
  f.target_start_tick = target_start;
  f.invert_pivot = 60;
  f.augment_factor = 2;
  f.diminish_factor = 2;
  return f;
}

}  // namespace

TEST(ValidatorTest, EpisodeMotifDerivedOriginalPasses) {
  Material material = subjectAscendingScale();
  material.episodes.push_back(
      makeEpisode(motif_ops::EpisodeMotifTransform::Original, 1, 4 * kTicksPerBeat));

  std::vector<NoteEvent> notes;
  // Subject in V0 bars 0-3.
  for (const auto& m : material.subject)
    notes.push_back(makeNote(m.start_tick, m.duration, m.pitch, 0));
  // Episode in V1 bars 4-7 = subject pitches at offset 4q (Original).
  for (std::size_t i = 0; i < material.subject.size(); ++i) {
    const auto& m = material.subject[i];
    notes.push_back(makeNote(m.start_tick + 4 * kTicksPerBeat, m.duration, m.pitch, 1));
  }
  std::vector<NoteProvenance> prov(notes.size(), makeProv(0, NoteSource::Material));

  ValidationReport r = Validator{}.validate(notes, prov, cMajorWhole(), material);
  EXPECT_FALSE(hasRule(r, "episode_motif_derived"));
}

TEST(ValidatorTest, EpisodeMotifDerivedInvertPasses) {
  Material material = subjectAscendingScale();
  material.episodes.push_back(
      makeEpisode(motif_ops::EpisodeMotifTransform::Invert, 1, 4 * kTicksPerBeat));

  std::vector<NoteEvent> notes;
  // Subject in V0.
  for (const auto& m : material.subject)
    notes.push_back(makeNote(m.start_tick, m.duration, m.pitch, 0));
  // Invert around 60: 60→60, 62→58, 64→56, 65→55.
  std::vector<std::uint8_t> inverted = {60, 58, 56, 55};
  for (std::size_t i = 0; i < inverted.size(); ++i) {
    notes.push_back(makeNote((i + 4) * kTicksPerBeat, kTicksPerBeat, inverted[i], 1));
  }
  std::vector<NoteProvenance> prov(notes.size(), makeProv(0, NoteSource::Material));

  ValidationReport r = Validator{}.validate(notes, prov, cMajorWhole(), material);
  EXPECT_FALSE(hasRule(r, "episode_motif_derived"));
}

TEST(ValidatorTest, EpisodeMotifDerivedRetrogradePasses) {
  Material material = subjectAscendingScale();
  material.episodes.push_back(
      makeEpisode(motif_ops::EpisodeMotifTransform::Retrograde, 1, 4 * kTicksPerBeat));

  std::vector<NoteEvent> notes;
  for (const auto& m : material.subject)
    notes.push_back(makeNote(m.start_tick, m.duration, m.pitch, 0));
  // Retrograde: 65, 64, 62, 60 starting at bar 4.
  std::vector<std::uint8_t> retro = {65, 64, 62, 60};
  for (std::size_t i = 0; i < retro.size(); ++i) {
    notes.push_back(makeNote((i + 4) * kTicksPerBeat, kTicksPerBeat, retro[i], 1));
  }
  std::vector<NoteProvenance> prov(notes.size(), makeProv(0, NoteSource::Material));

  ValidationReport r = Validator{}.validate(notes, prov, cMajorWhole(), material);
  EXPECT_FALSE(hasRule(r, "episode_motif_derived"));
}

TEST(ValidatorTest, EpisodeMotifDerivedAugmentPasses) {
  Material material = subjectAscendingScale();
  material.episodes.push_back(
      makeEpisode(motif_ops::EpisodeMotifTransform::Augment, 1, 8 * kTicksPerBeat));

  std::vector<NoteEvent> notes;
  for (const auto& m : material.subject)
    notes.push_back(makeNote(m.start_tick, m.duration, m.pitch, 0));
  // Augment by 2: pitches 60, 62, 64, 65; duration 2q each; start at bar 8.
  std::vector<std::uint8_t> aug_pitch = {60, 62, 64, 65};
  for (std::size_t i = 0; i < aug_pitch.size(); ++i) {
    notes.push_back(
        makeNote(8 * kTicksPerBeat + 2 * i * kTicksPerBeat, 2 * kTicksPerBeat, aug_pitch[i], 1));
  }
  std::vector<NoteProvenance> prov(notes.size(), makeProv(0, NoteSource::Material));

  ValidationReport r = Validator{}.validate(notes, prov, cMajorWhole(), material);
  EXPECT_FALSE(hasRule(r, "episode_motif_derived"));
}

TEST(ValidatorTest, EpisodeMotifDerivedDiminishPasses) {
  Material material;
  // Source with 2q durations so diminish-by-2 yields 1q durations.
  material.subject = {subjectNote(0, 2 * kTicksPerBeat, 60),
                      subjectNote(2 * kTicksPerBeat, 2 * kTicksPerBeat, 62),
                      subjectNote(4 * kTicksPerBeat, 2 * kTicksPerBeat, 64)};
  material.episodes.push_back(
      makeEpisode(motif_ops::EpisodeMotifTransform::Diminish, 1, 8 * kTicksPerBeat));

  std::vector<NoteEvent> notes;
  for (const auto& m : material.subject)
    notes.push_back(makeNote(m.start_tick, m.duration, m.pitch, 0));
  // Diminish by 2: pitches 60, 62, 64; duration q each; start at bar 8.
  std::vector<std::uint8_t> dim_pitch = {60, 62, 64};
  for (std::size_t i = 0; i < dim_pitch.size(); ++i) {
    notes.push_back(
        makeNote(8 * kTicksPerBeat + i * kTicksPerBeat, kTicksPerBeat, dim_pitch[i], 1));
  }
  std::vector<NoteProvenance> prov(notes.size(), makeProv(0, NoteSource::Material));

  ValidationReport r = Validator{}.validate(notes, prov, cMajorWhole(), material);
  EXPECT_FALSE(hasRule(r, "episode_motif_derived"));
}

TEST(ValidatorTest, EpisodeMotifDerivedPitchMismatchFails) {
  Material material = subjectAscendingScale();
  material.episodes.push_back(
      makeEpisode(motif_ops::EpisodeMotifTransform::Original, 1, 4 * kTicksPerBeat));

  std::vector<NoteEvent> notes;
  for (const auto& m : material.subject)
    notes.push_back(makeNote(m.start_tick, m.duration, m.pitch, 0));
  // Inject wrong second pitch (62 → 67).
  notes.push_back(makeNote(4 * kTicksPerBeat, kTicksPerBeat, 60, 1));
  notes.push_back(makeNote(5 * kTicksPerBeat, kTicksPerBeat, 67, 1));  // mismatch
  notes.push_back(makeNote(6 * kTicksPerBeat, kTicksPerBeat, 64, 1));
  notes.push_back(makeNote(7 * kTicksPerBeat, kTicksPerBeat, 65, 1));
  std::vector<NoteProvenance> prov(notes.size(), makeProv(0, NoteSource::Material));

  ValidationReport r = Validator{}.validate(notes, prov, cMajorWhole(), material);
  EXPECT_EQ(r.status, ValidationStatus::FailedSpan);
  EXPECT_TRUE(hasRule(r, "episode_motif_derived"));
  // Mismatch failure must carry MusicalFail FailKind.
  bool kind_ok = false;
  for (const auto& f : r.failures) {
    if (f.rule_id == "episode_motif_derived") {
      kind_ok = f.kind == FailKind::MusicalFail;
      break;
    }
  }
  EXPECT_TRUE(kind_ok);
}

TEST(ValidatorTest, EpisodeMotifDerivedMissingNoteFails) {
  Material material = subjectAscendingScale();
  material.episodes.push_back(
      makeEpisode(motif_ops::EpisodeMotifTransform::Original, 1, 4 * kTicksPerBeat));

  std::vector<NoteEvent> notes;
  for (const auto& m : material.subject)
    notes.push_back(makeNote(m.start_tick, m.duration, m.pitch, 0));
  // Episode emits only the first two notes (rest absent → mismatch on
  // tick 6q / 7q because no note matches the expected start_tick).
  notes.push_back(makeNote(4 * kTicksPerBeat, kTicksPerBeat, 60, 1));
  notes.push_back(makeNote(5 * kTicksPerBeat, kTicksPerBeat, 62, 1));
  std::vector<NoteProvenance> prov(notes.size(), makeProv(0, NoteSource::Material));

  ValidationReport r = Validator{}.validate(notes, prov, cMajorWhole(), material);
  EXPECT_EQ(r.status, ValidationStatus::FailedSpan);
  EXPECT_TRUE(hasRule(r, "episode_motif_derived"));
}

// P6 Tonal answer + Countersubject Validator tests.

TEST(ValidatorTest, TonalAnswerDominantMappingPassesForTonicHeadToDominantPc) {
  Material material;
  // Subject opens on C4 (60, tonic). Tonal answer head G4 (67, dominant pc).
  material.subject = {subjectNote(0, kTicksPerBeat, 60)};
  material.tonal_answer = {subjectNote(0, kTicksPerBeat, 67)};
  material.use_tonal_answer = true;
  std::vector<NoteEvent> notes = {makeNote(0, kTicksPerBeat, 60, 0)};
  std::vector<NoteProvenance> prov(notes.size(), makeProv(0, NoteSource::Material));
  ValidationReport r = Validator{}.validate(notes, prov, cMajorWhole(), material);
  EXPECT_FALSE(hasRule(r, "tonal_answer_dominant_mapping"));
}

TEST(ValidatorTest, TonalAnswerDominantMappingFailsForTonicHeadToWrongPc) {
  Material material;
  // Tonal answer head is D4 (62, supertonic) — wrong. Expected dominant pc.
  material.subject = {subjectNote(0, kTicksPerBeat, 60)};
  material.tonal_answer = {subjectNote(0, kTicksPerBeat, 62)};
  material.use_tonal_answer = true;
  std::vector<NoteEvent> notes = {makeNote(0, kTicksPerBeat, 60, 0)};
  std::vector<NoteProvenance> prov(notes.size(), makeProv(0, NoteSource::Material));
  ValidationReport r = Validator{}.validate(notes, prov, cMajorWhole(), material);
  EXPECT_EQ(r.status, ValidationStatus::FailedSpan);
  EXPECT_TRUE(hasRule(r, "tonal_answer_dominant_mapping"));
}

TEST(ValidatorTest, TonalAnswerDominantMappingPassesForDominantHeadToTonicPc) {
  Material material;
  // Subject opens on G4 (67, dominant). Tonal answer head C5 (72, tonic pc).
  material.subject = {subjectNote(0, kTicksPerBeat, 67)};
  material.tonal_answer = {subjectNote(0, kTicksPerBeat, 72)};
  material.use_tonal_answer = true;
  std::vector<NoteEvent> notes = {makeNote(0, kTicksPerBeat, 67, 0)};
  std::vector<NoteProvenance> prov(notes.size(), makeProv(0, NoteSource::Material));
  ValidationReport r = Validator{}.validate(notes, prov, cMajorWhole(), material);
  EXPECT_FALSE(hasRule(r, "tonal_answer_dominant_mapping"));
}

TEST(ValidatorTest, TonalAnswerDominantMappingVacuousForMiddleDegreeHead) {
  Material material;
  // Subject opens on E4 (64, mediant). Mapping rule is silent.
  material.subject = {subjectNote(0, kTicksPerBeat, 64)};
  material.tonal_answer = {subjectNote(0, kTicksPerBeat, 59)};  // real answer base
  material.use_tonal_answer = true;
  std::vector<NoteEvent> notes = {makeNote(0, kTicksPerBeat, 64, 0)};
  std::vector<NoteProvenance> prov(notes.size(), makeProv(0, NoteSource::Material));
  ValidationReport r = Validator{}.validate(notes, prov, cMajorWhole(), material);
  EXPECT_FALSE(hasRule(r, "tonal_answer_dominant_mapping"));
}

TEST(ValidatorTest, TonalAnswerDominantMappingSkippedWhenFlagUnset) {
  Material material;
  material.subject = {subjectNote(0, kTicksPerBeat, 60)};
  // Wrong mapping but flag is off → rule must NOT fire.
  material.tonal_answer = {subjectNote(0, kTicksPerBeat, 62)};
  material.use_tonal_answer = false;
  std::vector<NoteEvent> notes = {makeNote(0, kTicksPerBeat, 60, 0)};
  std::vector<NoteProvenance> prov(notes.size(), makeProv(0, NoteSource::Material));
  ValidationReport r = Validator{}.validate(notes, prov, cMajorWhole(), material);
  EXPECT_FALSE(hasRule(r, "tonal_answer_dominant_mapping"));
}

TEST(ValidatorTest, CountersubjectContinuousPassesForFullCoverage) {
  Material material;
  material.subject = {subjectNote(0, kTicksPerBeat, 60)};
  // Answer occupies bars 4-7 (16 beats). CS in V0 covers all 16 beats.
  for (int i = 0; i < 16; ++i) {
    material.answer.push_back(subjectNote(4 * kTicksPerBar + i * kTicksPerBeat, kTicksPerBeat, 60));
  }
  for (int i = 0; i < 16; ++i) {
    material.countersubject.push_back(
        subjectNote(4 * kTicksPerBar + i * kTicksPerBeat, kTicksPerBeat, 71));
  }
  std::vector<NoteEvent> notes;
  for (const auto& m : material.countersubject) {
    notes.push_back(makeNote(m.start_tick, m.duration, m.pitch, 0));
  }
  for (const auto& m : material.answer) {
    notes.push_back(makeNote(m.start_tick, m.duration, m.pitch, 1));
  }
  std::vector<NoteProvenance> prov(notes.size(), makeProv(0, NoteSource::Material));
  ValidationReport r = Validator{}.validate(notes, prov, cMajorWhole(), material);
  EXPECT_FALSE(hasRule(r, "countersubject_continuous"));
}

TEST(ValidatorTest, CountersubjectContinuousFailsForGap) {
  Material material;
  material.subject = {subjectNote(0, kTicksPerBeat, 60)};
  for (int i = 0; i < 16; ++i) {
    material.answer.push_back(subjectNote(4 * kTicksPerBar + i * kTicksPerBeat, kTicksPerBeat, 60));
  }
  // CS declared as 16 beats but emit only 8 → gap at beat 9..
  for (int i = 0; i < 16; ++i) {
    material.countersubject.push_back(
        subjectNote(4 * kTicksPerBar + i * kTicksPerBeat, kTicksPerBeat, 71));
  }
  std::vector<NoteEvent> notes;
  for (int i = 0; i < 8; ++i) {
    notes.push_back(makeNote(4 * kTicksPerBar + i * kTicksPerBeat, kTicksPerBeat, 71, 0));
  }
  for (const auto& m : material.answer) {
    notes.push_back(makeNote(m.start_tick, m.duration, m.pitch, 1));
  }
  std::vector<NoteProvenance> prov(notes.size(), makeProv(0, NoteSource::Material));
  ValidationReport r = Validator{}.validate(notes, prov, cMajorWhole(), material);
  EXPECT_EQ(r.status, ValidationStatus::FailedSpan);
  EXPECT_TRUE(hasRule(r, "countersubject_continuous"));
}

TEST(ValidatorTest, CountersubjectContinuousSkippedWhenCSEmpty) {
  Material material;
  for (int i = 0; i < 4; ++i) {
    material.answer.push_back(subjectNote(i * kTicksPerBeat, kTicksPerBeat, 60));
  }
  // No CS declared → rule is vacuously satisfied.
  std::vector<NoteEvent> notes;
  std::vector<NoteProvenance> prov;
  ValidationReport r = Validator{}.validate(notes, prov, cMajorWhole(), material);
  EXPECT_FALSE(hasRule(r, "countersubject_continuous"));
}

TEST(ValidatorTest, EpisodeMotifDerivedEmptyEpisodesListsPasses) {
  // No EpisodeFragments => rule is vacuously satisfied even with
  // arbitrary content.
  Material material;
  material.subject = subjectAscendingScale().subject;
  std::vector<NoteEvent> notes;
  for (const auto& m : material.subject)
    notes.push_back(makeNote(m.start_tick, m.duration, m.pitch, 0));
  std::vector<NoteProvenance> prov(notes.size(), makeProv(0, NoteSource::Material));

  ValidationReport r = Validator{}.validate(notes, prov, cMajorWhole(), material);
  EXPECT_FALSE(hasRule(r, "episode_motif_derived"));
}

// ===================================================================
// P7: Functional-harmony doubling / spacing rules.
// ===================================================================
//
// The three rules (doubling_no_leading_tone, doubling_no_seventh,
// spacing_adjacent_voices_within_octave) only fire when the active
// ChordEvent declares `has_degree = true` — that's how callers opt
// into the P7 stricter regime. Tests below build small HarmonicPlans
// inline so each scenario is self-contained.

namespace {

HarmonicPlan gMajorDominantWithDegree() {
  // Single G-major (dominant of C) chord region with has_degree set.
  HarmonicPlan plan;
  plan.tonic_pc = 0;
  plan.is_minor = false;
  ChordEvent v;
  v.start_tick = 0;
  v.root_pc = 7;
  v.quality = ChordQuality::Major;
  v.degree = RomanNumeral::V;
  v.inversion = ChordInversion::Root;
  v.function = HarmonicFunction::D;
  v.has_degree = true;
  plan.chords.push_back(v);
  return plan;
}

HarmonicPlan gDominant7WithDegree() {
  HarmonicPlan plan;
  plan.tonic_pc = 0;
  plan.is_minor = false;
  ChordEvent v7;
  v7.start_tick = 0;
  v7.root_pc = 7;
  v7.quality = ChordQuality::Dominant7;
  v7.degree = RomanNumeral::V;
  v7.inversion = ChordInversion::Root;
  v7.function = HarmonicFunction::D;
  v7.has_degree = true;
  plan.chords.push_back(v7);
  return plan;
}

HarmonicPlan cTonicWithDegree() {
  HarmonicPlan plan;
  plan.tonic_pc = 0;
  plan.is_minor = false;
  ChordEvent i;
  i.start_tick = 0;
  i.root_pc = 0;
  i.quality = ChordQuality::Major;
  i.degree = RomanNumeral::I;
  i.inversion = ChordInversion::Root;
  i.function = HarmonicFunction::T;
  i.has_degree = true;
  plan.chords.push_back(i);
  return plan;
}

}  // namespace

TEST(ValidatorTest, DoublingNoLeadingToneFailsForDoubledB) {
  // V chord in C major: G/B/D. Doubling B (the leading tone) across
  // two voices fires the rule.
  const Tick beat = kTicksPerBeat;
  std::vector<NoteEvent> notes = {
      makeNote(0, beat, 83, 0),  // B5
      makeNote(0, beat, 71, 1),  // B4 — duplicate leading tone
      makeNote(0, beat, 67, 2),  // G4
  };
  std::vector<NoteProvenance> prov(notes.size(), makeProv(0, NoteSource::Compose));
  ValidationReport r = Validator{}.validate(notes, prov, gMajorDominantWithDegree(), Material{});
  EXPECT_TRUE(hasRule(r, "doubling_no_leading_tone"));
}

TEST(ValidatorTest, DoublingNoLeadingTonePassesForSingleLeadingTone) {
  const Tick beat = kTicksPerBeat;
  std::vector<NoteEvent> notes = {
      makeNote(0, beat, 79, 0),  // G5
      makeNote(0, beat, 71, 1),  // B4 — only leading tone
      makeNote(0, beat, 67, 2),  // G4 (root double, allowed)
  };
  std::vector<NoteProvenance> prov(notes.size(), makeProv(0, NoteSource::Compose));
  ValidationReport r = Validator{}.validate(notes, prov, gMajorDominantWithDegree(), Material{});
  EXPECT_FALSE(hasRule(r, "doubling_no_leading_tone"));
}

TEST(ValidatorTest, DoublingNoLeadingToneSkippedOnChordsWithoutDegree) {
  // Same doubled B, but plan defaults (has_degree=false) — rule must
  // skip silently for pre-P7 fixtures.
  HarmonicPlan plan = gMajorDominantWithDegree();
  plan.chords.front().has_degree = false;
  const Tick beat = kTicksPerBeat;
  std::vector<NoteEvent> notes = {
      makeNote(0, beat, 83, 0),
      makeNote(0, beat, 71, 1),
      makeNote(0, beat, 67, 2),
  };
  std::vector<NoteProvenance> prov(notes.size(), makeProv(0, NoteSource::Compose));
  ValidationReport r = Validator{}.validate(notes, prov, plan, Material{});
  EXPECT_FALSE(hasRule(r, "doubling_no_leading_tone"));
}

TEST(ValidatorTest, DoublingNoLeadingToneIgnoredOnTonicChord) {
  // I chord does not contain the leading tone, so doubling B has no
  // bearing on this rule.
  const Tick beat = kTicksPerBeat;
  std::vector<NoteEvent> notes = {
      makeNote(0, beat, 72, 0),  // C5
      makeNote(0, beat, 71, 1),  // B4 — non-chord-tone, but rule is silent
      makeNote(0, beat, 67, 2),  // G4
  };
  std::vector<NoteProvenance> prov(notes.size(), makeProv(0, NoteSource::Compose));
  ValidationReport r = Validator{}.validate(notes, prov, cTonicWithDegree(), Material{});
  EXPECT_FALSE(hasRule(r, "doubling_no_leading_tone"));
}

TEST(ValidatorTest, DoublingNoSeventhFailsForDoubledF) {
  // V7 in C: G B D F. Doubling F (the chord seventh) fires the rule.
  const Tick beat = kTicksPerBeat;
  std::vector<NoteEvent> notes = {
      makeNote(0, beat, 77, 0),  // F5 — seventh
      makeNote(0, beat, 71, 1),  // B4
      makeNote(0, beat, 65, 2),  // F4 — duplicate seventh
      makeNote(0, beat, 55, 3),  // G3
  };
  std::vector<NoteProvenance> prov(notes.size(), makeProv(0, NoteSource::Compose));
  ValidationReport r = Validator{}.validate(notes, prov, gDominant7WithDegree(), Material{});
  EXPECT_TRUE(hasRule(r, "doubling_no_seventh"));
}

TEST(ValidatorTest, DoublingNoSeventhPassesForSingleSeventh) {
  const Tick beat = kTicksPerBeat;
  std::vector<NoteEvent> notes = {
      makeNote(0, beat, 77, 0),  // F5 — seventh
      makeNote(0, beat, 74, 1),  // D5
      makeNote(0, beat, 71, 2),  // B4
      makeNote(0, beat, 55, 3),  // G3
  };
  std::vector<NoteProvenance> prov(notes.size(), makeProv(0, NoteSource::Compose));
  ValidationReport r = Validator{}.validate(notes, prov, gDominant7WithDegree(), Material{});
  EXPECT_FALSE(hasRule(r, "doubling_no_seventh"));
}

TEST(ValidatorTest, DoublingNoSeventhSkippedOnPlainTriad) {
  // V triad: no 7th, rule must not fire even if F sounds (which would
  // be a NCT for the triad).
  const Tick beat = kTicksPerBeat;
  std::vector<NoteEvent> notes = {
      makeNote(0, beat, 77, 0),
      makeNote(0, beat, 65, 1),
      makeNote(0, beat, 55, 2),
  };
  std::vector<NoteProvenance> prov(notes.size(), makeProv(0, NoteSource::Compose));
  ValidationReport r = Validator{}.validate(notes, prov, gMajorDominantWithDegree(), Material{});
  EXPECT_FALSE(hasRule(r, "doubling_no_seventh"));
}

TEST(ValidatorTest, SpacingFailsForUpperVoicesOverOctave) {
  // V chord; V0=G5 (79), V1=B3 (59), V2=G3 (55). V0-V1 gap = 20
  // semitones > 12 → spacing rule fires.
  const Tick beat = kTicksPerBeat;
  std::vector<NoteEvent> notes = {
      makeNote(0, beat, 79, 0),
      makeNote(0, beat, 59, 1),
      makeNote(0, beat, 55, 2),
  };
  std::vector<NoteProvenance> prov(notes.size(), makeProv(0, NoteSource::Compose));
  ValidationReport r = Validator{}.validate(notes, prov, gMajorDominantWithDegree(), Material{});
  EXPECT_TRUE(hasRule(r, "spacing_adjacent_voices_within_octave"));
}

TEST(ValidatorTest, SpacingPassesForTightUpperVoices) {
  // V0=G5 (79), V1=D5 (74), V2=B4 (71). All adjacent gaps within an
  // octave.
  const Tick beat = kTicksPerBeat;
  std::vector<NoteEvent> notes = {
      makeNote(0, beat, 79, 0),
      makeNote(0, beat, 74, 1),
      makeNote(0, beat, 71, 2),
  };
  std::vector<NoteProvenance> prov(notes.size(), makeProv(0, NoteSource::Compose));
  ValidationReport r = Validator{}.validate(notes, prov, gMajorDominantWithDegree(), Material{});
  EXPECT_FALSE(hasRule(r, "spacing_adjacent_voices_within_octave"));
}

TEST(ValidatorTest, SpacingAllowsLargeBassTenorGap) {
  // 4-voice spacing: upper pairs S/A and A/T must stay within an
  // octave; the T/B pair (V2-V3) may exceed. Here the bass sits very
  // low (G2 = 43) but A/T (V1-V2) stays compact.
  const Tick beat = kTicksPerBeat;
  std::vector<NoteEvent> notes = {
      makeNote(0, beat, 79, 0),  // G5
      makeNote(0, beat, 74, 1),  // D5
      makeNote(0, beat, 71, 2),  // B4
      makeNote(0, beat, 43, 3),  // G2 — wide gap below tenor
  };
  std::vector<NoteProvenance> prov(notes.size(), makeProv(0, NoteSource::Compose));
  ValidationReport r = Validator{}.validate(notes, prov, gMajorDominantWithDegree(), Material{});
  EXPECT_FALSE(hasRule(r, "spacing_adjacent_voices_within_octave"));
}

TEST(ValidatorTest, SpacingSkippedForTwoVoiceTexture) {
  // Only 2 voices → spacing rule has no upper-pair triple to check.
  const Tick beat = kTicksPerBeat;
  std::vector<NoteEvent> notes = {
      makeNote(0, beat, 79, 0),
      makeNote(0, beat, 43, 1),
  };
  std::vector<NoteProvenance> prov(notes.size(), makeProv(0, NoteSource::Compose));
  ValidationReport r = Validator{}.validate(notes, prov, gMajorDominantWithDegree(), Material{});
  EXPECT_FALSE(hasRule(r, "spacing_adjacent_voices_within_octave"));
}

}  // namespace bach::composer
