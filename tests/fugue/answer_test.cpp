// Tests for fugue/answer.h -- real answer transposition, tonal answer
// mutation, and auto-detection.

#include "fugue/answer.h"

#include <gtest/gtest.h>

#include "core/pitch_utils.h"

namespace bach {
namespace {

// Helper: build a subject from pitches (all quarter notes).
Subject makeSubjectQuarters(const std::vector<uint8_t>& pitches,
                            Key key = Key::C) {
  Subject subject;
  subject.key = key;
  Tick current_tick = 0;
  for (const auto& pitch : pitches) {
    subject.notes.push_back({current_tick, kTicksPerBeat, pitch, 80, 0});
    current_tick += kTicksPerBeat;
  }
  subject.length_ticks = current_tick;
  return subject;
}

// ---------------------------------------------------------------------------
// Real Answer
// ---------------------------------------------------------------------------

TEST(AnswerTest, RealAnswerTransposesUpPerfectFifth) {
  // C4 D4 E4 F4 -> G4 A4 B4 C5
  Subject subject = makeSubjectQuarters({60, 62, 64, 65});
  Answer answer = generateAnswer(subject, AnswerType::Real);

  ASSERT_EQ(answer.notes.size(), 4u);
  EXPECT_EQ(answer.notes[0].pitch, 67);  // C4+7 = G4
  EXPECT_EQ(answer.notes[1].pitch, 69);  // D4+7 = A4
  EXPECT_EQ(answer.notes[2].pitch, 71);  // E4+7 = B4
  EXPECT_EQ(answer.notes[3].pitch, 72);  // F4+7 = C5
}

TEST(AnswerTest, RealAnswerType) {
  Subject subject = makeSubjectQuarters({60, 62, 64});
  Answer answer = generateAnswer(subject, AnswerType::Real);
  EXPECT_EQ(answer.type, AnswerType::Real);
}

TEST(AnswerTest, RealAnswerDominantKey) {
  Subject subject = makeSubjectQuarters({60, 62, 64});
  subject.key = Key::C;
  Answer answer = generateAnswer(subject, AnswerType::Real);

  // Dominant of C = G.
  EXPECT_EQ(answer.key, Key::G);
}

TEST(AnswerTest, RealAnswerPreservesTiming) {
  Subject subject = makeSubjectQuarters({60, 62, 64, 65});
  Answer answer = generateAnswer(subject, AnswerType::Real);

  ASSERT_EQ(answer.notes.size(), subject.notes.size());
  for (size_t idx = 0; idx < answer.notes.size(); ++idx) {
    EXPECT_EQ(answer.notes[idx].start_tick, subject.notes[idx].start_tick);
    EXPECT_EQ(answer.notes[idx].duration, subject.notes[idx].duration);
  }
}

TEST(AnswerTest, RealAnswerDifferentKey) {
  // Subject in G: G A B C
  Subject subject = makeSubjectQuarters({67, 69, 71, 72});
  subject.key = Key::G;
  Answer answer = generateAnswer(subject, AnswerType::Real);

  // Real answer: all up P5.
  EXPECT_EQ(answer.notes[0].pitch, 74);  // G4+7 = D5
  EXPECT_EQ(answer.notes[1].pitch, 76);  // A4+7 = E5
  EXPECT_EQ(answer.notes[2].pitch, 78);  // B4+7 = F#5
  EXPECT_EQ(answer.notes[3].pitch, 79);  // C5+7 = G5

  // Dominant of G = D.
  EXPECT_EQ(answer.key, Key::D);
}

// ---------------------------------------------------------------------------
// Tonal Answer
// ---------------------------------------------------------------------------

TEST(AnswerTest, TonalAnswerType) {
  Subject subject = makeSubjectQuarters({60, 67, 64, 62});
  Answer answer = generateAnswer(subject, AnswerType::Tonal);
  EXPECT_EQ(answer.type, AnswerType::Tonal);
}

TEST(AnswerTest, TonalAnswerTonicGetsPerfectFourth) {
  // Subject starts C (tonic), which is in the mutation zone.
  // Tonic in mutation zone -> transpose up P4 instead of P5.
  Subject subject = makeSubjectQuarters({60, 64, 65, 60});
  Answer answer = generateAnswer(subject, AnswerType::Tonal);

  ASSERT_GE(answer.notes.size(), 1u);
  // First note is C (tonic) in mutation zone -> up P4 = F4 (65).
  EXPECT_EQ(answer.notes[0].pitch, 65);
}

TEST(AnswerTest, TonalAnswerDominantGetsP5) {
  // Subject: G (dominant), then non-tonic/dominant note.
  // G is dominant -> in mutation zone, transpose up P5.
  Subject subject = makeSubjectQuarters({67, 64, 62, 60});
  Answer answer = generateAnswer(subject, AnswerType::Tonal);

  ASSERT_GE(answer.notes.size(), 1u);
  // G (dominant) in mutation zone -> up P5 = D5 (74).
  EXPECT_EQ(answer.notes[0].pitch, 74);
}

TEST(AnswerTest, TonalAnswerSameCountAsSubject) {
  Subject subject = makeSubjectQuarters({60, 67, 64, 62, 60});
  Answer answer = generateAnswer(subject, AnswerType::Tonal);
  EXPECT_EQ(answer.notes.size(), subject.notes.size());
}

// ---------------------------------------------------------------------------
// Auto-detect
// ---------------------------------------------------------------------------

TEST(AnswerTest, AutoDetectRealForStepwiseSubject) {
  // C D E F G: stepwise, no tonic-dominant leap.
  Subject subject = makeSubjectQuarters({60, 62, 64, 65, 67});
  AnswerType detected = autoDetectAnswerType(subject);
  EXPECT_EQ(detected, AnswerType::Real);
}

TEST(AnswerTest, AutoDetectTonalForTonicDominantLeap) {
  // C G E D C: starts with tonic-dominant leap (C->G = P5).
  Subject subject = makeSubjectQuarters({60, 67, 64, 62, 60});
  AnswerType detected = autoDetectAnswerType(subject);
  EXPECT_EQ(detected, AnswerType::Tonal);
}

TEST(AnswerTest, AutoDetectTonalForDominantTonicLeap) {
  // G C E D: dominant-to-tonic leap at start.
  Subject subject = makeSubjectQuarters({67, 60, 64, 62});
  AnswerType detected = autoDetectAnswerType(subject);
  EXPECT_EQ(detected, AnswerType::Tonal);
}

TEST(AnswerTest, AutoDetectWithSingleNote) {
  Subject subject = makeSubjectQuarters({60});
  AnswerType detected = autoDetectAnswerType(subject);
  EXPECT_EQ(detected, AnswerType::Real);
}

TEST(AnswerTest, AutoDetectUsedByDefault) {
  // generateAnswer with Auto type should call autoDetect internally.
  Subject subject = makeSubjectQuarters({60, 62, 64, 65, 67});
  Answer answer = generateAnswer(subject, AnswerType::Auto);

  // Stepwise subject -> should detect Real.
  EXPECT_EQ(answer.type, AnswerType::Real);
}

TEST(AnswerTest, AutoDetectTonalUsedByDefault) {
  Subject subject = makeSubjectQuarters({60, 67, 64, 62, 60});
  Answer answer = generateAnswer(subject, AnswerType::Auto);
  EXPECT_EQ(answer.type, AnswerType::Tonal);
}

// ---------------------------------------------------------------------------
// Edge cases
// ---------------------------------------------------------------------------

TEST(AnswerTest, EmptySubjectProducesEmptyAnswer) {
  Subject subject;
  subject.key = Key::C;
  Answer answer = generateAnswer(subject, AnswerType::Real);
  EXPECT_TRUE(answer.notes.empty());
}

TEST(AnswerTest, HighPitchClampedToValidRange) {
  // Subject near top of MIDI range.
  Subject subject = makeSubjectQuarters({122, 124, 126});
  Answer answer = generateAnswer(subject, AnswerType::Real);

  for (const auto& note : answer.notes) {
    EXPECT_LE(note.pitch, 127) << "Pitch should be clamped to 127";
  }
}

// ---------------------------------------------------------------------------
// Archetype preferred_answer connection (Step 1)
// ---------------------------------------------------------------------------

TEST(AnswerTest, PreferredAnswerOverridesAutoDetection) {
  // Subject with tonic-dominant leap at start: C4(60) -> G4(67) = P5.
  // Auto-detection would classify this as Tonal, but archetype_preference=Real
  // should override and produce a Real answer.
  Subject subject = makeSubjectQuarters({60, 67, 64, 62, 60});
  AnswerType auto_detected = autoDetectAnswerType(subject);
  ASSERT_EQ(auto_detected, AnswerType::Tonal)
      << "Precondition: subject should auto-detect as Tonal";

  Answer answer = generateAnswer(subject, AnswerType::Auto, AnswerType::Real);
  EXPECT_EQ(answer.type, AnswerType::Real)
      << "Archetype preference Real should override auto-detected Tonal";
}

TEST(AnswerTest, PreferredAnswerAutoFallsThrough) {
  // Same tonic-dominant leap subject. When archetype_preference=Auto,
  // auto-detection should work normally and produce a Tonal answer.
  Subject subject = makeSubjectQuarters({60, 67, 64, 62, 60});
  Answer answer = generateAnswer(subject, AnswerType::Auto, AnswerType::Auto);
  EXPECT_EQ(answer.type, AnswerType::Tonal)
      << "With archetype_preference=Auto, auto-detection should select Tonal "
         "for tonic-dominant leap";
}

TEST(AnswerTest, ExplicitTypeOverridesPreference) {
  // When the caller explicitly sets type=Tonal, the archetype_preference
  // should be ignored even if it says Real.
  Subject subject = makeSubjectQuarters({60, 67, 64, 62, 60});
  Answer answer = generateAnswer(subject, AnswerType::Tonal, AnswerType::Real);
  EXPECT_EQ(answer.type, AnswerType::Tonal)
      << "Explicit type=Tonal should take precedence over "
         "archetype_preference=Real";
}

}  // namespace
}  // namespace bach
