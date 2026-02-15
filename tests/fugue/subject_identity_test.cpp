// Tests for fugue/subject_identity.h -- Kerngestalt identity computation,
// transformation derivation, classification, and drift detection.

#include "fugue/subject_identity.h"

#include <algorithm>
#include <cstdlib>
#include <vector>

#include <gtest/gtest.h>

#include "core/basic_types.h"
#include "core/note_source.h"
#include "counterpoint/collision_resolver.h"
#include "counterpoint/counterpoint_state.h"
#include "counterpoint/fux_rule_evaluator.h"
#include "counterpoint/parallel_repair.h"

namespace bach {
namespace {

// ---------------------------------------------------------------------------
// Helpers: construct NoteEvent sequences for identity tests
// ---------------------------------------------------------------------------

/// @brief Build a note sequence from pitches and durations, starting at tick 0.
/// All notes are assigned voice 0 and FugueSubject source.
std::vector<NoteEvent> makeSubjectNotes(const std::vector<uint8_t>& pitches,
                                        const std::vector<Tick>& durations) {
  std::vector<NoteEvent> notes;
  Tick current_tick = 0;
  for (size_t idx = 0; idx < pitches.size(); ++idx) {
    Tick dur = (idx < durations.size()) ? durations[idx] : kTicksPerBeat;
    NoteEvent note;
    note.start_tick = current_tick;
    note.duration = dur;
    note.pitch = pitches[idx];
    note.velocity = 80;
    note.voice = 0;
    note.source = BachNoteSource::FugueSubject;
    notes.push_back(note);
    current_tick += dur;
  }
  return notes;
}

/// @brief Build a note sequence from pitches with all quarter-note durations.
/// Notes start on beat boundaries (multiples of kTicksPerBeat) to get
/// AccentPosition::Strong.
std::vector<NoteEvent> makeSubjectQuarters(const std::vector<uint8_t>& pitches) {
  std::vector<Tick> durations(pitches.size(), kTicksPerBeat);
  return makeSubjectNotes(pitches, durations);
}

// ===========================================================================
// Test 1: BuildEssentialIdentity_BasicProperties
// ===========================================================================

TEST(SubjectIdentityTest, BuildEssentialIdentity_BasicProperties) {
  // C4 -> E4 -> G4 -> A4 -> F4 (all quarter notes on beat boundaries).
  // Intervals: +4, +3, +2, -5
  // Rhythm: 480, 480, 480, 480, 480 (5 notes)
  // Accent: all Strong (on beat boundaries)
  auto notes = makeSubjectQuarters({60, 64, 67, 69, 65});
  auto identity = buildEssentialIdentity(notes, Key::C, false);

  ASSERT_TRUE(identity.isValid());

  // Core intervals: consecutive pitch differences.
  ASSERT_EQ(identity.core_intervals.size(), 4u);
  EXPECT_EQ(identity.core_intervals[0], 4);   // C->E = +4
  EXPECT_EQ(identity.core_intervals[1], 3);   // E->G = +3
  EXPECT_EQ(identity.core_intervals[2], 2);   // G->A = +2
  EXPECT_EQ(identity.core_intervals[3], -4);  // A->F = -4

  // Core rhythm: one duration per note.
  ASSERT_EQ(identity.core_rhythm.size(), 5u);
  for (size_t idx = 0; idx < 5; ++idx) {
    EXPECT_EQ(identity.core_rhythm[idx], kTicksPerBeat)
        << "Note " << idx << " rhythm mismatch";
  }

  // Accent pattern: all notes start on beat boundaries -> Strong.
  ASSERT_EQ(identity.accent_pattern.size(), 5u);
  for (size_t idx = 0; idx < 5; ++idx) {
    EXPECT_EQ(identity.accent_pattern[idx], AccentPosition::Strong)
        << "Note " << idx << " should be Strong";
  }
}

// ===========================================================================
// Test 2: BuildEssentialIdentity_SignatureInterval
// ===========================================================================

TEST(SubjectIdentityTest, BuildEssentialIdentity_SignatureInterval) {
  // Subject with intervals: +2, +7, -1, +3.
  // Largest abs >= 3 is +7.
  auto notes = makeSubjectQuarters({60, 62, 69, 68, 71});
  auto identity = buildEssentialIdentity(notes, Key::C, false);

  ASSERT_TRUE(identity.isValid());
  EXPECT_EQ(identity.signature_interval, 7);
}

TEST(SubjectIdentityTest, BuildEssentialIdentity_SignatureIntervalFallback) {
  // Subject with only stepwise motion: +1, +2, -1, -2.
  // No interval with abs >= 3, so fallback to first non-zero interval (+1).
  auto notes = makeSubjectQuarters({60, 61, 63, 62, 60});
  auto identity = buildEssentialIdentity(notes, Key::C, false);

  ASSERT_TRUE(identity.isValid());
  // Intervals: +1, +2, -1, -2. No abs >= 3. First non-zero = +1.
  EXPECT_EQ(identity.signature_interval, 1);
}

TEST(SubjectIdentityTest, BuildEssentialIdentity_SignatureIntervalNegative) {
  // Subject where largest abs interval is negative: +2, -5, +1.
  // -5 has abs 5 >= 3, and is the largest. signature_interval = -5.
  auto notes = makeSubjectQuarters({60, 62, 57, 58});
  auto identity = buildEssentialIdentity(notes, Key::C, false);

  ASSERT_TRUE(identity.isValid());
  EXPECT_EQ(identity.signature_interval, -5);
}

// ===========================================================================
// Test 3: BuildEssentialIdentity_HeadTailFragments
// ===========================================================================

TEST(SubjectIdentityTest, BuildEssentialIdentity_HeadTailFragments) {
  // 6-note subject: C4 E4 G4 A4 F4 D4.
  // Intervals: +4, +3, +2, -5, -2 (5 intervals).
  // Head fragment: first 3 intervals = {+4, +3, +2}.
  // Tail fragment: last 3 intervals = {+2, -5, -2}.
  // Head rhythm: first 3 durations.
  // Tail rhythm: last 3 durations.
  auto notes = makeSubjectQuarters({60, 64, 67, 69, 65, 62});
  auto identity = buildEssentialIdentity(notes, Key::C, false);

  ASSERT_TRUE(identity.isValid());

  // Head fragment intervals.
  ASSERT_EQ(identity.head_fragment_intervals.size(), 3u);
  EXPECT_EQ(identity.head_fragment_intervals[0], 4);
  EXPECT_EQ(identity.head_fragment_intervals[1], 3);
  EXPECT_EQ(identity.head_fragment_intervals[2], 2);

  // Tail fragment intervals.
  ASSERT_EQ(identity.tail_fragment_intervals.size(), 3u);
  EXPECT_EQ(identity.tail_fragment_intervals[0], 2);   // G->A = +2
  EXPECT_EQ(identity.tail_fragment_intervals[1], -4);  // A->F = -4
  EXPECT_EQ(identity.tail_fragment_intervals[2], -3);  // F->D = -3

  // Head fragment rhythm (first 3 durations).
  ASSERT_EQ(identity.head_fragment_rhythm.size(), 3u);
  for (size_t idx = 0; idx < 3; ++idx) {
    EXPECT_EQ(identity.head_fragment_rhythm[idx], kTicksPerBeat);
  }

  // Tail fragment rhythm (last 3 durations).
  ASSERT_EQ(identity.tail_fragment_rhythm.size(), 3u);
  for (size_t idx = 0; idx < 3; ++idx) {
    EXPECT_EQ(identity.tail_fragment_rhythm[idx], kTicksPerBeat);
  }
}

TEST(SubjectIdentityTest, BuildEssentialIdentity_ShortSubjectFragments) {
  // 3-note subject: C4 E4 G4.
  // Intervals: +4, +3 (2 intervals).
  // Head = tail = all intervals (min(3, 2) = 2).
  auto notes = makeSubjectQuarters({60, 64, 67});
  auto identity = buildEssentialIdentity(notes, Key::C, false);

  ASSERT_TRUE(identity.isValid());

  ASSERT_EQ(identity.head_fragment_intervals.size(), 2u);
  EXPECT_EQ(identity.head_fragment_intervals[0], 4);
  EXPECT_EQ(identity.head_fragment_intervals[1], 3);

  ASSERT_EQ(identity.tail_fragment_intervals.size(), 2u);
  EXPECT_EQ(identity.tail_fragment_intervals[0], 4);
  EXPECT_EQ(identity.tail_fragment_intervals[1], 3);
}

// ===========================================================================
// Test 4: BuildDerivedTransformations_Inversion
// ===========================================================================

TEST(SubjectIdentityTest, BuildDerivedTransformations_Inversion) {
  // Intervals: +4, +3, -2, -5.
  // Inversion: -4, -3, +2, +5.
  // Rhythm is preserved under inversion.
  auto notes = makeSubjectQuarters({60, 64, 67, 65, 60});
  auto essential = buildEssentialIdentity(notes, Key::C, false);
  auto derived = buildDerivedTransformations(essential);

  ASSERT_TRUE(derived.isValid());

  ASSERT_EQ(derived.inverted_intervals.size(), essential.core_intervals.size());
  for (size_t idx = 0; idx < essential.core_intervals.size(); ++idx) {
    EXPECT_EQ(derived.inverted_intervals[idx], -essential.core_intervals[idx])
        << "Inversion mismatch at index " << idx;
  }

  // Inverted rhythm = core rhythm (identical).
  ASSERT_EQ(derived.inverted_rhythm.size(), essential.core_rhythm.size());
  for (size_t idx = 0; idx < essential.core_rhythm.size(); ++idx) {
    EXPECT_EQ(derived.inverted_rhythm[idx], essential.core_rhythm[idx]);
  }
}

// ===========================================================================
// Test 5: BuildDerivedTransformations_Retrograde
// ===========================================================================

TEST(SubjectIdentityTest, BuildDerivedTransformations_Retrograde) {
  // Intervals: +4, +3, -2, -5.
  // Retrograde: reversed AND negated => +5, +2, -3, -4.
  auto notes = makeSubjectQuarters({60, 64, 67, 65, 60});
  auto essential = buildEssentialIdentity(notes, Key::C, false);
  auto derived = buildDerivedTransformations(essential);

  ASSERT_TRUE(derived.isValid());

  ASSERT_EQ(derived.retrograde_intervals.size(), essential.core_intervals.size());
  size_t n = essential.core_intervals.size();
  for (size_t idx = 0; idx < n; ++idx) {
    int expected = -essential.core_intervals[n - 1 - idx];
    EXPECT_EQ(derived.retrograde_intervals[idx], expected)
        << "Retrograde mismatch at index " << idx;
  }

  // Retrograde rhythm = reversed core rhythm.
  ASSERT_EQ(derived.retrograde_rhythm.size(), essential.core_rhythm.size());
  size_t nr = essential.core_rhythm.size();
  for (size_t idx = 0; idx < nr; ++idx) {
    EXPECT_EQ(derived.retrograde_rhythm[idx], essential.core_rhythm[nr - 1 - idx])
        << "Retrograde rhythm mismatch at index " << idx;
  }
}

// ===========================================================================
// Test 6: BuildDerivedTransformations_AugmentedDiminished
// ===========================================================================

TEST(SubjectIdentityTest, BuildDerivedTransformations_AugmentedDiminished) {
  // Use mixed durations: quarter (480), eighth (240), sixteenth (120).
  auto notes = makeSubjectNotes(
      {60, 64, 67, 65},
      {kTicksPerBeat, duration::kEighthNote, duration::kSixteenthNote, kTicksPerBeat});
  auto essential = buildEssentialIdentity(notes, Key::C, false);
  auto derived = buildDerivedTransformations(essential);

  ASSERT_TRUE(derived.isValid());

  // Augmented: each duration doubled.
  ASSERT_EQ(derived.augmented_rhythm.size(), essential.core_rhythm.size());
  for (size_t idx = 0; idx < essential.core_rhythm.size(); ++idx) {
    EXPECT_EQ(derived.augmented_rhythm[idx], essential.core_rhythm[idx] * 2)
        << "Augmented rhythm mismatch at index " << idx;
  }

  // Diminished: each duration halved, minimum kThirtySecondNote (60 ticks).
  ASSERT_EQ(derived.diminished_rhythm.size(), essential.core_rhythm.size());
  for (size_t idx = 0; idx < essential.core_rhythm.size(); ++idx) {
    Tick expected = essential.core_rhythm[idx] / 2;
    if (expected < duration::kThirtySecondNote) {
      expected = duration::kThirtySecondNote;
    }
    EXPECT_EQ(derived.diminished_rhythm[idx], expected)
        << "Diminished rhythm mismatch at index " << idx;
  }
}

TEST(SubjectIdentityTest, BuildDerivedTransformations_DiminishedMinimumClamped) {
  // Use a very short duration (kThirtySecondNote = 60 ticks).
  // Halved = 30, but clamped to 60.
  auto notes = makeSubjectNotes(
      {60, 64, 67},
      {duration::kThirtySecondNote, duration::kThirtySecondNote, duration::kThirtySecondNote});
  auto essential = buildEssentialIdentity(notes, Key::C, false);
  auto derived = buildDerivedTransformations(essential);

  ASSERT_TRUE(derived.isValid());

  for (size_t idx = 0; idx < derived.diminished_rhythm.size(); ++idx) {
    EXPECT_GE(derived.diminished_rhythm[idx], duration::kThirtySecondNote)
        << "Diminished rhythm should not go below 32nd note at index " << idx;
  }
}

// ===========================================================================
// Test 7: KerngestaltType_Classification
// ===========================================================================

TEST(SubjectIdentityTest, KerngestaltType_IntervalDriven) {
  // Subject with a leap >= 3 semitones: C4 -> G4 (leap of +7).
  // signature_interval = +7 (abs >= 3), has_leap = true.
  auto notes = makeSubjectQuarters({60, 67, 65, 64, 60});
  auto identity = buildEssentialIdentity(notes, Key::C, false);

  ASSERT_TRUE(identity.isValid());
  EXPECT_EQ(identity.kerngestalt_type, KerngestaltType::IntervalDriven);
}

TEST(SubjectIdentityTest, KerngestaltType_ChromaticCell) {
  // Subject with semitone motion in head (first 4 intervals) and no leap >= 3.
  // All intervals small: +1, +2, -1, +2.
  auto notes = makeSubjectQuarters({60, 61, 63, 62, 64});
  auto identity = buildEssentialIdentity(notes, Key::C, false);

  ASSERT_TRUE(identity.isValid());
  // No abs interval >= 3 => no leap, so not IntervalDriven.
  // Has semitone (abs==1) in first 4 intervals => ChromaticCell.
  EXPECT_EQ(identity.kerngestalt_type, KerngestaltType::ChromaticCell);
}

TEST(SubjectIdentityTest, KerngestaltType_Arpeggio) {
  // After Phase A1 fix, Arpeggio is checked first (highest priority).
  // Subject: C4 Eb4 G4 C5 G4.
  // Intervals: +3, +4, +5, -5.
  // isArpeggioPattern: +3 and +4 are consecutive same-direction 3rd-class
  // intervals (both abs 3 or 4, both positive). max_consecutive = 2,
  // abs(3) != abs(4) so has_distinct_triads = true. Arpeggio wins.
  auto notes = makeSubjectQuarters({60, 63, 67, 72, 67});
  auto identity = buildEssentialIdentity(notes, Key::C, false);

  ASSERT_TRUE(identity.isValid());
  // Arpeggio has priority over IntervalDriven in classifyKerngestalt.
  EXPECT_EQ(identity.kerngestalt_type, KerngestaltType::Arpeggio);
}

TEST(SubjectIdentityTest, KerngestaltType_Linear) {
  // Subject with only step motion (abs <= 2) and no semitone in head.
  // Intervals: +2, +2, -2, -2. No leap, no semitone, no chord-tone.
  // Falls to Linear.
  auto notes = makeSubjectQuarters({60, 62, 64, 62, 60});
  auto identity = buildEssentialIdentity(notes, Key::C, false);

  ASSERT_TRUE(identity.isValid());
  EXPECT_EQ(identity.kerngestalt_type, KerngestaltType::Linear);
}

// ===========================================================================
// Test 8: IsValidKerngestalt_RejectsFlat
// ===========================================================================

TEST(SubjectIdentityTest, IsValidKerngestalt_RejectsFlat) {
  // Purely stepwise subject: +1, +1, +1, +1.
  // No interval with abs >= 3 => common check 1 fails => invalid.
  auto notes = makeSubjectQuarters({60, 61, 62, 63, 64});
  auto identity = buildEssentialIdentity(notes, Key::C, false);

  ASSERT_TRUE(identity.isValid());
  EXPECT_FALSE(isValidKerngestalt(identity));
}

TEST(SubjectIdentityTest, IsValidKerngestalt_RejectsPurelyStepwise) {
  // All whole-step motion: +2, +2, -2, -2.
  // No abs >= 3 => fails common check 1.
  auto notes = makeSubjectQuarters({60, 62, 64, 62, 60});
  auto identity = buildEssentialIdentity(notes, Key::C, false);

  ASSERT_TRUE(identity.isValid());
  EXPECT_FALSE(isValidKerngestalt(identity));
}

TEST(SubjectIdentityTest, IsValidKerngestalt_InvalidOnEmptyIdentity) {
  EssentialIdentity empty;
  EXPECT_FALSE(isValidKerngestalt(empty));
}

// ===========================================================================
// Test 9: SubjectDrift_IntervalPreserved
// ===========================================================================
// The key test: build a subject, record its identity, run through the
// collision resolution pipeline, and verify identity is preserved.

class SubjectDriftTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // Register 2 voices with wide ranges to avoid range-rejection artifacts.
    state_.registerVoice(0, 48, 84);  // Subject voice: C3-C6
    state_.registerVoice(1, 36, 60);  // Bass voice: C2-C4

    // Build a subject that uses only chord tones of C major so each note
    // is consonant with a C3 bass pedal. All on strong beats (quarter notes).
    // Pitches: C4(60) E4(64) G4(67) C5(72) G4(67) E4(64) C4(60) E4(64)
    // Intervals: +4, +3, +5, -5, -3, -4, +4
    // Every pitch forms a consonance with C3(48):
    //   60-48=12(P8), 64-48=16%12=4(M3), 67-48=19%12=7(P5), 72-48=24%12=0(P8)
    subject_notes_ = makeSubjectNotes(
        {60, 64, 67, 72, 67, 64, 60, 64},
        {kTicksPerBeat, kTicksPerBeat, kTicksPerBeat, kTicksPerBeat,
         kTicksPerBeat, kTicksPerBeat, kTicksPerBeat, kTicksPerBeat});

    // Compute pre-pipeline identity.
    pre_identity_ = buildSubjectIdentity(subject_notes_, Key::C, false);
  }

  /// @brief Place subject notes through collision resolver in voice 0.
  /// Also place a held bass note in voice 1 to create a realistic multi-voice
  /// context (C3 pedal point for 8 beats). All subject pitches are consonant
  /// with C3 (P8, M3, P5, P8) so the "original" strategy should accept them.
  std::vector<NoteEvent> runThroughPipeline() {
    // Add bass pedal note (voice 1, C3, held for 8 beats).
    NoteEvent bass;
    bass.start_tick = 0;
    bass.duration = 8 * kTicksPerBeat;
    bass.pitch = 48;
    bass.velocity = 80;
    bass.voice = 1;
    bass.source = BachNoteSource::PedalPoint;
    state_.addNote(1, bass);

    // Place each subject note through the collision resolver.
    std::vector<NoteEvent> placed_notes;
    for (const auto& note : subject_notes_) {
      auto result = resolver_.findSafePitch(
          state_, rules_, note.voice, note.pitch,
          note.start_tick, note.duration,
          BachNoteSource::FugueSubject);

      NoteEvent placed = note;
      if (result.accepted) {
        placed.pitch = result.pitch;
      }
      // If not accepted (rest), keep original pitch. This shouldn't happen
      // with our consonant setup, but guards against regression.
      state_.addNote(note.voice, placed);
      placed_notes.push_back(placed);
    }

    return placed_notes;
  }

  CounterpointState state_;
  FuxRuleEvaluator rules_;
  CollisionResolver resolver_;
  std::vector<NoteEvent> subject_notes_;
  SubjectIdentity pre_identity_;
};

TEST_F(SubjectDriftTest, IntervalPatternPreservedAfterCollisionResolution) {
  auto placed_notes = runThroughPipeline();

  // Compute post-pipeline identity.
  auto post_identity = buildSubjectIdentity(placed_notes, Key::C, false);

  ASSERT_TRUE(pre_identity_.isValid());
  ASSERT_TRUE(post_identity.isValid());

  // Core intervals must be identical: FugueSubject has SemiImmutable protection,
  // which only allows octave shifts. Interval pattern (mod 12) should be preserved.
  ASSERT_EQ(pre_identity_.essential.core_intervals.size(),
            post_identity.essential.core_intervals.size())
      << "Interval count changed after collision resolution";

  for (size_t idx = 0; idx < pre_identity_.essential.core_intervals.size(); ++idx) {
    // SemiImmutable allows octave shift (+/-12), so directed intervals may
    // differ by multiples of 12. Check that the interval mod 12 is preserved.
    int pre = pre_identity_.essential.core_intervals[idx];
    int post = post_identity.essential.core_intervals[idx];
    int diff = pre - post;

    // Either identical, or differing by a multiple of 12 (octave transposition).
    EXPECT_TRUE(diff % 12 == 0)
        << "Interval drift at index " << idx
        << ": pre=" << pre << " post=" << post
        << " (diff=" << diff << ", not a multiple of 12)";
  }
}

TEST_F(SubjectDriftTest, RhythmPatternPreservedAfterCollisionResolution) {
  auto placed_notes = runThroughPipeline();

  auto post_identity = buildSubjectIdentity(placed_notes, Key::C, false);

  ASSERT_TRUE(pre_identity_.isValid());
  ASSERT_TRUE(post_identity.isValid());

  // Collision resolution never modifies duration, so rhythm must be identical.
  ASSERT_EQ(pre_identity_.essential.core_rhythm.size(),
            post_identity.essential.core_rhythm.size())
      << "Rhythm note count changed after collision resolution";

  for (size_t idx = 0; idx < pre_identity_.essential.core_rhythm.size(); ++idx) {
    EXPECT_EQ(pre_identity_.essential.core_rhythm[idx],
              post_identity.essential.core_rhythm[idx])
        << "Rhythm drift at index " << idx;
  }
}

TEST_F(SubjectDriftTest, AccentPatternPreservedAfterCollisionResolution) {
  auto placed_notes = runThroughPipeline();

  auto post_identity = buildSubjectIdentity(placed_notes, Key::C, false);

  ASSERT_TRUE(pre_identity_.isValid());
  ASSERT_TRUE(post_identity.isValid());

  // Collision resolution never modifies start_tick, so accent pattern must be identical.
  ASSERT_EQ(pre_identity_.essential.accent_pattern.size(),
            post_identity.essential.accent_pattern.size())
      << "Accent pattern size changed after collision resolution";

  for (size_t idx = 0; idx < pre_identity_.essential.accent_pattern.size(); ++idx) {
    EXPECT_EQ(pre_identity_.essential.accent_pattern[idx],
              post_identity.essential.accent_pattern[idx])
        << "Accent drift at index " << idx;
  }
}

TEST_F(SubjectDriftTest, IdentityPreservedAfterParallelRepair) {
  // Place subject notes directly (without collision resolver first).
  for (const auto& note : subject_notes_) {
    state_.addNote(note.voice, note);
  }

  // Also add a second voice (voice 1) with notes that could cause parallels.
  std::vector<NoteEvent> bass_notes = makeSubjectNotes(
      {48, 50, 52, 48, 55, 52, 50, 48},
      {kTicksPerBeat, kTicksPerBeat, kTicksPerBeat, kTicksPerBeat,
       kTicksPerBeat, kTicksPerBeat, kTicksPerBeat, kTicksPerBeat});
  for (auto& note : bass_notes) {
    note.voice = 1;
    note.source = BachNoteSource::FreeCounterpoint;
    state_.addNote(1, note);
  }

  // Combine all notes for parallel repair.
  std::vector<NoteEvent> all_notes;
  all_notes.insert(all_notes.end(), subject_notes_.begin(), subject_notes_.end());
  all_notes.insert(all_notes.end(), bass_notes.begin(), bass_notes.end());

  // Run parallel repair.
  ParallelRepairParams params;
  params.num_voices = 2;
  params.scale = ScaleType::Major;
  params.key_at_tick = [](Tick) { return Key::C; };
  params.voice_range_static = [this](uint8_t voice) -> std::pair<uint8_t, uint8_t> {
    auto range = state_.getVoiceRange(voice);
    return range ? std::make_pair(range->low, range->high)
                 : std::make_pair(uint8_t(36), uint8_t(84));
  };

  repairParallelPerfect(all_notes, params);

  // Extract subject notes (voice 0) after repair.
  std::vector<NoteEvent> repaired_subject;
  for (const auto& note : all_notes) {
    if (note.voice == 0) {
      repaired_subject.push_back(note);
    }
  }

  // Sort by start_tick for identity computation.
  std::sort(repaired_subject.begin(), repaired_subject.end(),
            [](const NoteEvent& a, const NoteEvent& b) {
              return a.start_tick < b.start_tick;
            });

  auto post_identity = buildSubjectIdentity(repaired_subject, Key::C, false);

  ASSERT_TRUE(pre_identity_.isValid());
  ASSERT_TRUE(post_identity.isValid());

  // FugueSubject has SemiImmutable protection. Parallel repair should not
  // modify these notes (Immutable/SemiImmutable notes are skipped).
  // Therefore the identity must be exactly preserved.
  ASSERT_EQ(pre_identity_.essential.core_intervals.size(),
            post_identity.essential.core_intervals.size());

  for (size_t idx = 0; idx < pre_identity_.essential.core_intervals.size(); ++idx) {
    EXPECT_EQ(pre_identity_.essential.core_intervals[idx],
              post_identity.essential.core_intervals[idx])
        << "Subject interval drifted after parallel repair at index " << idx;
  }

  ASSERT_EQ(pre_identity_.essential.core_rhythm.size(),
            post_identity.essential.core_rhythm.size());
  for (size_t idx = 0; idx < pre_identity_.essential.core_rhythm.size(); ++idx) {
    EXPECT_EQ(pre_identity_.essential.core_rhythm[idx],
              post_identity.essential.core_rhythm[idx])
        << "Subject rhythm drifted after parallel repair at index " << idx;
  }
}

// ===========================================================================
// Test 10: BuildSubjectIdentity_EmptyNotes
// ===========================================================================

TEST(SubjectIdentityTest, BuildSubjectIdentity_EmptyNotes) {
  // Empty input.
  std::vector<NoteEvent> empty;
  auto identity = buildSubjectIdentity(empty, Key::C, false);

  EXPECT_FALSE(identity.isValid());
  EXPECT_FALSE(identity.essential.isValid());
  EXPECT_FALSE(identity.derived.isValid());
}

TEST(SubjectIdentityTest, BuildSubjectIdentity_SingleNote) {
  // Single note: no intervals to compute.
  auto notes = makeSubjectQuarters({60});
  auto identity = buildSubjectIdentity(notes, Key::C, false);

  // Single note yields no core_intervals (needs at least 2 notes).
  EXPECT_FALSE(identity.isValid());
  EXPECT_FALSE(identity.essential.isValid());
}

TEST(SubjectIdentityTest, BuildSubjectIdentity_TwoNotes) {
  // Two notes: minimal valid input. One interval.
  auto notes = makeSubjectQuarters({60, 67});
  auto identity = buildSubjectIdentity(notes, Key::C, false);

  EXPECT_TRUE(identity.isValid());
  ASSERT_EQ(identity.essential.core_intervals.size(), 1u);
  EXPECT_EQ(identity.essential.core_intervals[0], 7);  // C4->G4 = +7
}

// ===========================================================================
// Additional edge case tests
// ===========================================================================

TEST(SubjectIdentityTest, AccentPattern_MixedStrongWeak) {
  // Notes with offbeat starts should get Weak accent.
  // Note 0: tick 0 (Strong), Note 1: tick 240 (Weak: not on beat boundary),
  // Note 2: tick 480 (Strong), Note 3: tick 600 (Weak).
  std::vector<NoteEvent> notes;
  NoteEvent n;
  n.velocity = 80;
  n.voice = 0;
  n.source = BachNoteSource::FugueSubject;

  n.start_tick = 0;
  n.duration = duration::kEighthNote;
  n.pitch = 60;
  notes.push_back(n);

  n.start_tick = duration::kEighthNote;  // 240
  n.duration = duration::kEighthNote;
  n.pitch = 62;
  notes.push_back(n);

  n.start_tick = kTicksPerBeat;  // 480
  n.duration = duration::kSixteenthNote;
  n.pitch = 64;
  notes.push_back(n);

  n.start_tick = kTicksPerBeat + duration::kSixteenthNote;  // 600
  n.duration = duration::kSixteenthNote;
  n.pitch = 65;
  notes.push_back(n);

  auto identity = buildEssentialIdentity(notes, Key::C, false);

  ASSERT_TRUE(identity.isValid());
  ASSERT_EQ(identity.accent_pattern.size(), 4u);
  EXPECT_EQ(identity.accent_pattern[0], AccentPosition::Strong);   // tick 0
  EXPECT_EQ(identity.accent_pattern[1], AccentPosition::Weak);     // tick 240
  EXPECT_EQ(identity.accent_pattern[2], AccentPosition::Strong);   // tick 480
  EXPECT_EQ(identity.accent_pattern[3], AccentPosition::Weak);     // tick 600
}

TEST(SubjectIdentityTest, BuildDerivedTransformations_InvalidEssential) {
  // Derived transformations from an invalid essential identity should be invalid.
  EssentialIdentity empty;
  auto derived = buildDerivedTransformations(empty);
  EXPECT_FALSE(derived.isValid());
}

TEST(SubjectIdentityTest, KerngestaltTypeToString_AllValues) {
  EXPECT_STREQ(kerngestaltTypeToString(KerngestaltType::IntervalDriven), "interval_driven");
  EXPECT_STREQ(kerngestaltTypeToString(KerngestaltType::ChromaticCell), "chromatic_cell");
  EXPECT_STREQ(kerngestaltTypeToString(KerngestaltType::Arpeggio), "arpeggio");
  EXPECT_STREQ(kerngestaltTypeToString(KerngestaltType::Linear), "linear");
}

TEST(SubjectIdentityTest, NaturalBreakPoint_ClampedToValidRange) {
  // 3-note subject: break point = climax_index + 1, clamped to [1, size-1].
  // Pitches: 60, 72, 64. Climax at index 1 (pitch 72).
  // natural_break_point = 1 + 1 = 2, which is valid for 3-note subject.
  auto notes = makeSubjectQuarters({60, 72, 64});
  auto identity = buildEssentialIdentity(notes, Key::C, false);

  ASSERT_TRUE(identity.isValid());
  EXPECT_GE(identity.natural_break_point, 1u);
  EXPECT_LT(identity.natural_break_point, notes.size());
  EXPECT_EQ(identity.natural_break_point, 2u);
}

TEST(SubjectIdentityTest, NaturalBreakPoint_ClimaxAtEnd) {
  // Climax at last note: break_point = last_idx + 1, clamped to size - 1.
  // Pitches: 60, 62, 64, 67. Climax at index 3. break = 3 + 1 = 4 >= size(4),
  // clamped to 3.
  auto notes = makeSubjectQuarters({60, 62, 64, 67});
  auto identity = buildEssentialIdentity(notes, Key::C, false);

  ASSERT_TRUE(identity.isValid());
  EXPECT_EQ(identity.natural_break_point, 3u);
}

TEST(SubjectIdentityTest, BuildSubjectIdentity_CompleteRoundTrip) {
  // Full integration: build identity, verify all layers are populated.
  auto notes = makeSubjectNotes(
      {60, 64, 67, 72, 71, 67, 64, 60},
      {kTicksPerBeat, duration::kEighthNote, duration::kEighthNote, kTicksPerBeat,
       kTicksPerBeat, duration::kEighthNote, duration::kEighthNote, kTicksPerBeat});

  auto identity = buildSubjectIdentity(notes, Key::C, false);

  ASSERT_TRUE(identity.isValid());

  // Layer 1 checks.
  EXPECT_EQ(identity.essential.core_intervals.size(), 7u);
  EXPECT_EQ(identity.essential.core_rhythm.size(), 8u);
  EXPECT_EQ(identity.essential.accent_pattern.size(), 8u);
  EXPECT_FALSE(identity.essential.head_fragment_intervals.empty());
  EXPECT_FALSE(identity.essential.tail_fragment_intervals.empty());
  EXPECT_NE(identity.essential.signature_interval, 0);

  // Layer 2 checks.
  EXPECT_EQ(identity.derived.inverted_intervals.size(), 7u);
  EXPECT_EQ(identity.derived.retrograde_intervals.size(), 7u);
  EXPECT_EQ(identity.derived.inverted_rhythm.size(), 8u);
  EXPECT_EQ(identity.derived.retrograde_rhythm.size(), 8u);
  EXPECT_EQ(identity.derived.augmented_rhythm.size(), 8u);
  EXPECT_EQ(identity.derived.diminished_rhythm.size(), 8u);
}

// ===========================================================================
// Test 11: Classification boundary tests
// ===========================================================================

TEST(SubjectIdentityTest, KerngestaltType_ArpeggioRequiresConsecutiveThirds) {
  // Single 3rd-class intervals separated by a step should NOT trigger Arpeggio.
  // Subject: C4 Eb4 F4 A4 G4.
  // Intervals: +3, +2, +4, -2. The +3 and +4 are NOT consecutive (separated
  // by +2, which is not abs 3 or 4). isArpeggioPattern returns false.
  // Has leap >= 3 and signature >= 3 (signature=+4), so IntervalDriven.
  auto notes = makeSubjectQuarters({60, 63, 65, 69, 67});
  auto identity = buildEssentialIdentity(notes, Key::C, false);
  ASSERT_TRUE(identity.isValid());
  EXPECT_EQ(identity.kerngestalt_type, KerngestaltType::IntervalDriven);
}

TEST(SubjectIdentityTest, KerngestaltType_ChromaticRequiresTwoSemitones) {
  // Head with exactly 1 semitone should be Linear, not ChromaticCell.
  // Subject: C4 Db4 Eb4 Db4 B3.
  // Intervals: +1, +2, -2, -2. Only 1 semitone (index 0: abs(+1)==1) in
  // the first 4 intervals. No abs >= 3 so not IntervalDriven. Not Arpeggio.
  // chromatic_in_head_count = 1 < 2 so not ChromaticCell. Falls to Linear.
  auto notes = makeSubjectQuarters({60, 61, 63, 61, 59});
  auto identity = buildEssentialIdentity(notes, Key::C, false);
  ASSERT_TRUE(identity.isValid());
  EXPECT_EQ(identity.kerngestalt_type, KerngestaltType::Linear);
}

TEST(SubjectIdentityTest, KerngestaltType_ChromaticWithTwoSemitones) {
  // Head with 2+ semitones should be ChromaticCell.
  // Subject: C4 Db4 C4 D4 C4.
  // Intervals: +1, -1, +2, -2. Two semitones in the first 4 intervals
  // (index 0: abs(+1)==1, index 1: abs(-1)==1). No abs >= 3, not Arpeggio,
  // not IntervalDriven. chromatic_in_head_count = 2 so ChromaticCell.
  auto notes = makeSubjectQuarters({60, 61, 60, 62, 60});
  auto identity = buildEssentialIdentity(notes, Key::C, false);
  ASSERT_TRUE(identity.isValid());
  EXPECT_EQ(identity.kerngestalt_type, KerngestaltType::ChromaticCell);
}

// ===========================================================================
// Test 12: isValidKerngestalt extended tests
// ===========================================================================

TEST(SubjectIdentityTest, IsValidKerngestalt_IntervalRecurrence) {
  // 11-note subject where the head interval pattern {+7, -2, -1} recurs
  // at index 5 in the core intervals, well within the search window.
  // Subject: C4 G4 F4 E4 D4 C4 G4 F4 E4 G4 C4.
  //          60  67  65  64  62  60  67  65  64  67  60
  // Intervals: +7, -2, -1, -2, -2, +7, -2, -1, +3, -7 (10 intervals).
  // Head fragment: first 3 = {+7, -2, -1}.
  // Climax at index 1 (pitch 67). break = 2.
  // interval_search_end = 10 - 2 = 8. Search pos = 2..5.
  // At pos=5: core[5..7] = {+7, -2, -1} matches head exactly.
  // All quarter notes, so rhythm (Medium class) and accent (Strong) trivially match.
  // 11 notes >= 8, so all 3 conditions must pass (AND). All do.
  auto notes = makeSubjectQuarters({60, 67, 65, 64, 62, 60, 67, 65, 64, 67, 60});
  auto identity = buildEssentialIdentity(notes, Key::C, false);
  ASSERT_TRUE(identity.isValid());
  EXPECT_EQ(identity.kerngestalt_type, KerngestaltType::IntervalDriven);
  EXPECT_TRUE(isValidKerngestalt(identity));
}

TEST(SubjectIdentityTest, IsValidKerngestalt_ShortSubjectNoRecurrence) {
  // Short subject (< 8 notes) without head recurrence returns false even
  // though only 1 of 3 conditions (OR) is needed. The search window
  // (natural_break_point to total_intervals - 2) is too narrow for a
  // 3-interval head fragment to fit in a 5-note subject.
  // Subject: C4 G4 F4 E4 G4.
  // Intervals: +7, -2, -1, +3.
  // signature = +7 (abs 7 >= 3), has_leap = true. IntervalDriven.
  auto notes = makeSubjectQuarters({60, 67, 65, 64, 67});
  auto identity = buildEssentialIdentity(notes, Key::C, false);
  ASSERT_TRUE(identity.isValid());
  EXPECT_EQ(identity.kerngestalt_type, KerngestaltType::IntervalDriven);
  // The function should not crash on short subjects.
  // With only 4 intervals and a 3-interval head fragment, no room for
  // recurrence after the break point while excluding the last 2 intervals.
  EXPECT_FALSE(isValidKerngestalt(identity));
}

TEST(SubjectIdentityTest, IsValidKerngestalt_ShortSubjectWithRecurrence) {
  // 7-note subject (< 8) where head pattern recurs. OR logic applies.
  // Subject: C4 G4 F4 E4 G4 F4 E4.
  //          60  67  65  64  67  65  64
  // Intervals: +7, -2, -1, +3, -2, -1 (6 intervals).
  // Head fragment: first 3 = {+7, -2, -1}.
  // Climax at index 1 (pitch 67). break = 2.
  // interval_search_end = 6 - 2 = 4. Search pos = 2..1 (pos+3 <= 4).
  // pos=2: core[2..4] = {-1, +3, -2} -- try inversion of head {-7, +2, +1}: no.
  //        try retro-inversion = reverse(+7,-2,-1) = (-1,-2,+7), negate = (+1,+2,-7): no.
  // No interval match. But rhythm: all quarter notes, search_start=2,
  // rhythm_search_end = 7-2 = 5. pos=2, 2+3=5 <= 5, rhythm[2..4] all 480 = head
  // rhythm [480,480,480] -> DurationClass::Medium for all. Match!
  // 7 notes < 8 so OR logic: rhythm match alone suffices.
  auto notes = makeSubjectQuarters({60, 67, 65, 64, 67, 65, 64});
  auto identity = buildEssentialIdentity(notes, Key::C, false);
  ASSERT_TRUE(identity.isValid());
  EXPECT_EQ(identity.kerngestalt_type, KerngestaltType::IntervalDriven);
  EXPECT_TRUE(isValidKerngestalt(identity));
}

// ===========================================================================
// Test 13: CellWindow tests
// ===========================================================================

TEST(SubjectIdentityTest, CellWindow_DefaultInvalid) {
  CellWindow cw;
  EXPECT_FALSE(cw.valid);
  EXPECT_EQ(cw.start_idx, 0u);
  EXPECT_EQ(cw.end_idx, 0u);
}

TEST(SubjectIdentityTest, EssentialIdentity_CellWindowPreserved) {
  // Build identity and then set cell_window. Verify the assignment persists.
  auto notes = makeSubjectQuarters({60, 64, 67, 72, 67, 64, 60});
  auto identity = buildEssentialIdentity(notes, Key::C, false);
  ASSERT_TRUE(identity.isValid());

  // Assign cell_window after buildEssentialIdentity (as the generator would).
  identity.cell_window = {0, 2, true};

  EXPECT_TRUE(identity.cell_window.valid);
  EXPECT_EQ(identity.cell_window.start_idx, 0u);
  EXPECT_EQ(identity.cell_window.end_idx, 2u);
}

}  // namespace
}  // namespace bach
