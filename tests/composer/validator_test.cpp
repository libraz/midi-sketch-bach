#include "composer/validator.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <vector>

#include "composer/harmonic_plan.h"
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

}  // namespace bach::composer
