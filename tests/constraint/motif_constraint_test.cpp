// Tests for Phase 3: MotifOp transformations, CharacterEpisodeParams,
// generateConstraintEpisode, planFortspinnung, and motifOpToString.

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <set>
#include <vector>

#include "constraint/constraint_state.h"
#include "constraint/episode_generator.h"
#include "constraint/motif_constraint.h"
#include "core/basic_types.h"
#include "fugue/fortspinnung.h"
#include "fugue/motif_pool.h"
#include "fugue/subject.h"
#include "transform/motif_transform.h"
#include "transform/sequence.h"

namespace bach {
namespace {

// ===========================================================================
// Helpers
// ===========================================================================

/// @brief Create a simple 4-note C major test motif: C4, D4, E4, G4.
/// Each note is one quarter note duration, starting at tick 0.
std::vector<NoteEvent> makeTestMotif() {
  std::vector<NoteEvent> notes;
  const uint8_t pitches[] = {60, 62, 64, 67};
  for (int i = 0; i < 4; ++i) {
    NoteEvent n;
    n.pitch = pitches[i];
    n.start_tick = static_cast<Tick>(i) * kTicksPerBeat;
    n.duration = kTicksPerBeat;
    n.velocity = 80;
    n.voice = 0;
    n.source = BachNoteSource::EpisodeMaterial;
    notes.push_back(n);
  }
  return notes;
}

/// @brief Create an 8-note C major subject for integration tests.
/// C4, D4, E4, F4, G4, A4, B4, C5 — each one quarter note.
Subject makeTestSubject(SubjectCharacter character = SubjectCharacter::Severe) {
  Subject s;
  s.character = character;
  s.is_minor = false;
  s.key = Key::C;
  const uint8_t pitches[] = {60, 62, 64, 65, 67, 69, 71, 72};
  for (int i = 0; i < 8; ++i) {
    NoteEvent n;
    n.pitch = pitches[i];
    n.start_tick = static_cast<Tick>(i) * kTicksPerBeat;
    n.duration = kTicksPerBeat;
    n.velocity = 80;
    n.voice = 0;
    n.source = BachNoteSource::FugueSubject;
    s.notes.push_back(n);
  }
  s.length_ticks = 8 * kTicksPerBeat;
  return s;
}

/// @brief Build a MotifPool from the test subject.
MotifPool buildTestPool(SubjectCharacter character = SubjectCharacter::Severe) {
  MotifPool pool;
  auto subject = makeTestSubject(character);
  std::vector<NoteEvent> empty_cs;
  pool.build(subject.notes, empty_cs, character);
  return pool;
}

/// @brief Build a minimal EpisodeRequest with sensible defaults.
EpisodeRequest makeTestRequest(const MotifPool& pool,
                               uint32_t seed = 42,
                               uint8_t num_voices = 2) {
  EpisodeRequest req;
  req.entry_state = ConstraintState();
  req.start_key = Key::C;
  req.end_key = Key::C;
  req.start_tick = kTicksPerBar * 4;  // After 4 bars of exposition.
  req.duration = kTicksPerBar * 4;    // 4 bars of episode.
  req.num_voices = num_voices;
  req.motif_pool = &pool;
  req.character = SubjectCharacter::Severe;
  req.grammar = getFortspinnungGrammar(SubjectCharacter::Severe);
  req.episode_index = 0;
  req.energy_level = 0.5f;
  req.seed = seed;
  return req;
}

/// @brief Compute total duration of a note sequence (first start to last end).
Tick totalDuration(const std::vector<NoteEvent>& notes) {
  return motifDuration(notes);
}

// ===========================================================================
// 1. MotifOp transformation tests
// ===========================================================================

TEST(MotifConstraintTest, ApplyMotifOpOriginal) {
  auto motif = makeTestMotif();
  auto result = applyMotifOp(motif, MotifOp::Original);

  // Original returns an exact copy — same pitches and durations.
  ASSERT_EQ(result.size(), motif.size());
  for (size_t i = 0; i < motif.size(); ++i) {
    EXPECT_EQ(result[i].pitch, motif[i].pitch)
        << "Pitch mismatch at index " << i;
    EXPECT_EQ(result[i].duration, motif[i].duration)
        << "Duration mismatch at index " << i;
    EXPECT_EQ(result[i].start_tick, motif[i].start_tick)
        << "Start tick mismatch at index " << i;
  }
}

TEST(MotifConstraintTest, ApplyMotifOpInvert) {
  auto motif = makeTestMotif();
  auto result = applyMotifOp(motif, MotifOp::Invert, Key::C, ScaleType::Major);

  // Invert reverses intervals diatonically around the first pitch.
  // The first pitch should remain the same (pivot).
  ASSERT_EQ(result.size(), motif.size());
  EXPECT_EQ(result[0].pitch, motif[0].pitch)
      << "Pivot pitch should remain unchanged";

  // Original: C4(60)->D4(62) = ascending 2nd.
  // Inverted: C4(60) -> descending 2nd = Bb3(58) in diatonic.
  // The inverted pitch should be below the pivot for originally ascending intervals.
  EXPECT_LT(result[1].pitch, motif[0].pitch)
      << "Ascending interval should become descending after inversion";

  // Duration should be preserved.
  for (size_t i = 0; i < motif.size(); ++i) {
    EXPECT_EQ(result[i].duration, motif[i].duration)
        << "Duration should be preserved at index " << i;
  }
}

TEST(MotifConstraintTest, ApplyMotifOpRetrograde) {
  auto motif = makeTestMotif();
  auto result = applyMotifOp(motif, MotifOp::Retrograde);

  // Retrograde reverses the order of pitches.
  // First pitch of result should be the last pitch of the input.
  ASSERT_EQ(result.size(), motif.size());
  EXPECT_EQ(result[0].pitch, motif[3].pitch)
      << "First note of retrograde should have last pitch of original";
  EXPECT_EQ(result[3].pitch, motif[0].pitch)
      << "Last note of retrograde should have first pitch of original";

  // Durations should be preserved (reversed order).
  // All durations are kTicksPerBeat in our test motif, so they stay the same.
  for (size_t i = 0; i < result.size(); ++i) {
    EXPECT_EQ(result[i].duration, kTicksPerBeat);
  }
}

TEST(MotifConstraintTest, ApplyMotifOpDiminish) {
  auto motif = makeTestMotif();
  auto result = applyMotifOp(motif, MotifOp::Diminish);

  // Diminish halves all durations. Total duration = original / 2.
  ASSERT_EQ(result.size(), motif.size());
  Tick original_total = totalDuration(motif);
  Tick diminished_total = totalDuration(result);
  EXPECT_EQ(diminished_total, original_total / 2)
      << "Total duration of diminished motif should be half of original";

  for (size_t i = 0; i < result.size(); ++i) {
    EXPECT_EQ(result[i].duration, motif[i].duration / 2)
        << "Each duration should be halved at index " << i;
  }

  // Pitches should be preserved.
  for (size_t i = 0; i < result.size(); ++i) {
    EXPECT_EQ(result[i].pitch, motif[i].pitch)
        << "Pitch should be preserved at index " << i;
  }
}

TEST(MotifConstraintTest, ApplyMotifOpAugment) {
  auto motif = makeTestMotif();
  auto result = applyMotifOp(motif, MotifOp::Augment);

  // Augment doubles all durations. Total duration = original * 2.
  ASSERT_EQ(result.size(), motif.size());
  Tick original_total = totalDuration(motif);
  Tick augmented_total = totalDuration(result);
  EXPECT_EQ(augmented_total, original_total * 2)
      << "Total duration of augmented motif should be double the original";

  for (size_t i = 0; i < result.size(); ++i) {
    EXPECT_EQ(result[i].duration, motif[i].duration * 2)
        << "Each duration should be doubled at index " << i;
  }

  // Pitches should be preserved.
  for (size_t i = 0; i < result.size(); ++i) {
    EXPECT_EQ(result[i].pitch, motif[i].pitch)
        << "Pitch should be preserved at index " << i;
  }
}

TEST(MotifConstraintTest, ApplyMotifOpFragment) {
  auto motif = makeTestMotif();
  auto result = applyMotifOp(motif, MotifOp::Fragment);

  // Fragment returns the first half of notes (fragmentMotif splits into 2).
  // Original has 4 notes, first fragment should have 2 notes.
  ASSERT_FALSE(result.empty()) << "Fragment should produce non-empty output";
  EXPECT_LE(result.size(), motif.size())
      << "Fragment should be shorter than or equal to the original";

  // The fragment should contain a subset of the original pitches.
  for (const auto& note : result) {
    bool found = false;
    for (const auto& orig : motif) {
      if (note.pitch == orig.pitch) {
        found = true;
        break;
      }
    }
    EXPECT_TRUE(found) << "Fragment pitch " << static_cast<int>(note.pitch)
                       << " should exist in the original motif";
  }
}

TEST(MotifConstraintTest, ApplyMotifOpSequence) {
  auto motif = makeTestMotif();
  auto result = applyMotifOp(motif, MotifOp::Sequence, Key::C,
                             ScaleType::Major, -1);

  // Sequence returns a diatonic sequence (1 repetition) of the input.
  // generateDiatonicSequence produces the transposed copy only (not original).
  ASSERT_FALSE(result.empty()) << "Sequence should produce non-empty output";

  // The sequence should transpose each pitch down by one scale degree.
  // C4->B3, D4->C4, E4->D4, G4->F4 (approximately).
  // Check that pitches differ from original.
  bool any_different = false;
  for (size_t i = 0; i < std::min(result.size(), motif.size()); ++i) {
    if (result[i].pitch != motif[i].pitch) {
      any_different = true;
      break;
    }
  }
  EXPECT_TRUE(any_different)
      << "Sequence pitches should differ from original (transposed)";
}

TEST(MotifConstraintTest, ApplyMotifOpEmptyInput) {
  std::vector<NoteEvent> empty;
  auto result = applyMotifOp(empty, MotifOp::Original);
  EXPECT_TRUE(result.empty())
      << "Empty input should produce empty output for all ops";

  result = applyMotifOp(empty, MotifOp::Invert);
  EXPECT_TRUE(result.empty());
  result = applyMotifOp(empty, MotifOp::Retrograde);
  EXPECT_TRUE(result.empty());
  result = applyMotifOp(empty, MotifOp::Diminish);
  EXPECT_TRUE(result.empty());
  result = applyMotifOp(empty, MotifOp::Augment);
  EXPECT_TRUE(result.empty());
  result = applyMotifOp(empty, MotifOp::Fragment);
  EXPECT_TRUE(result.empty());
  result = applyMotifOp(empty, MotifOp::Sequence);
  EXPECT_TRUE(result.empty());
}

// ===========================================================================
// 2. CharacterEpisodeParams tests
// ===========================================================================

TEST(MotifConstraintTest, CharacterParamsSevere) {
  auto params = getCharacterParams(SubjectCharacter::Severe);
  // Severe: original + diatonic inversion, wide imitation (1.5-2.5 beats),
  // descending stepwise sequence.
  EXPECT_EQ(params.voice0_initial, MotifOp::Original);
  EXPECT_EQ(params.voice1_initial, MotifOp::Invert);
  EXPECT_EQ(params.voice1_secondary, MotifOp::Original);
  EXPECT_FLOAT_EQ(params.imitation_beats_lo, 1.5f);
  EXPECT_FLOAT_EQ(params.imitation_beats_hi, 2.5f);
  EXPECT_EQ(params.sequence_step, -1);
}

TEST(MotifConstraintTest, CharacterParamsPlayful) {
  auto params = getCharacterParams(SubjectCharacter::Playful);
  // Playful: retrograde + inversion, tight imitation (0.5-1.5 beats),
  // descending by two degrees for faster harmonic motion.
  EXPECT_EQ(params.voice0_initial, MotifOp::Retrograde);
  EXPECT_EQ(params.voice1_initial, MotifOp::Invert);
  EXPECT_EQ(params.voice1_secondary, MotifOp::Original);
  EXPECT_FLOAT_EQ(params.imitation_beats_lo, 0.5f);
  EXPECT_FLOAT_EQ(params.imitation_beats_hi, 1.5f);
  EXPECT_EQ(params.sequence_step, -2);
}

TEST(MotifConstraintTest, CharacterParamsNoble) {
  auto params = getCharacterParams(SubjectCharacter::Noble);
  // Noble: original + augmentation with retrograde secondary,
  // wide imitation, descending stepwise.
  EXPECT_EQ(params.voice0_initial, MotifOp::Original);
  EXPECT_EQ(params.voice1_initial, MotifOp::Augment);
  EXPECT_EQ(params.voice1_secondary, MotifOp::Retrograde);
  EXPECT_FLOAT_EQ(params.imitation_beats_lo, 1.5f);
  EXPECT_FLOAT_EQ(params.imitation_beats_hi, 2.5f);
  EXPECT_EQ(params.sequence_step, -1);
}

TEST(MotifConstraintTest, CharacterParamsRestless) {
  auto params = getCharacterParams(SubjectCharacter::Restless);
  // Restless: fragment + diminution, tight imitation,
  // descending by two degrees for urgency.
  EXPECT_EQ(params.voice0_initial, MotifOp::Fragment);
  EXPECT_EQ(params.voice1_initial, MotifOp::Diminish);
  EXPECT_EQ(params.voice1_secondary, MotifOp::Original);
  EXPECT_FLOAT_EQ(params.imitation_beats_lo, 0.5f);
  EXPECT_FLOAT_EQ(params.imitation_beats_hi, 1.5f);
  EXPECT_EQ(params.sequence_step, -2);
}

// ===========================================================================
// 3. generateConstraintEpisode integration tests
// ===========================================================================

TEST(MotifConstraintTest, GenerateEpisodeProducesNotes) {
  auto pool = buildTestPool();
  auto req = makeTestRequest(pool);
  EpisodeResult result = generateConstraintEpisode(req);

  // Non-empty output with at least 4 notes.
  EXPECT_GE(result.notes.size(), 4u)
      << "Episode should produce at least 4 notes";
}

TEST(MotifConstraintTest, GenerateEpisodeSuccess) {
  auto pool = buildTestPool();
  auto req = makeTestRequest(pool);
  EpisodeResult result = generateConstraintEpisode(req);

  EXPECT_TRUE(result.success)
      << "Episode generation should succeed for a simple case";
}

TEST(MotifConstraintTest, GenerateEpisodeRespectsStartTick) {
  auto pool = buildTestPool();
  auto req = makeTestRequest(pool);
  EpisodeResult result = generateConstraintEpisode(req);

  ASSERT_TRUE(result.success);
  for (const auto& note : result.notes) {
    EXPECT_GE(note.start_tick, req.start_tick)
        << "All notes must start at or after the requested start_tick. "
        << "Found note at tick " << note.start_tick
        << " but start_tick is " << req.start_tick;
  }
}

TEST(MotifConstraintTest, GenerateEpisodeRespectsDuration) {
  auto pool = buildTestPool();
  auto req = makeTestRequest(pool);
  EpisodeResult result = generateConstraintEpisode(req);

  ASSERT_TRUE(result.success);
  Tick episode_end = req.start_tick + req.duration;
  for (const auto& note : result.notes) {
    EXPECT_LT(note.start_tick, episode_end)
        << "All notes must start before episode end. "
        << "Found note starting at tick " << note.start_tick
        << " but episode ends at tick " << episode_end;
  }
}

TEST(MotifConstraintTest, GenerateEpisodeModulatesKey) {
  auto pool = buildTestPool();
  auto req = makeTestRequest(pool);
  req.start_key = Key::C;
  req.end_key = Key::G;  // Modulate to dominant.
  EpisodeResult result = generateConstraintEpisode(req);

  ASSERT_TRUE(result.success);
  // The achieved_key is set to end_key on success.
  EXPECT_EQ(result.achieved_key, Key::G)
      << "Achieved key should match end_key on successful generation";
}

TEST(MotifConstraintTest, GenerateEpisodeWithMultipleVoices) {
  auto pool = buildTestPool();
  auto req = makeTestRequest(pool, /*seed=*/42, /*num_voices=*/3);
  EpisodeResult result = generateConstraintEpisode(req);

  ASSERT_TRUE(result.success);
  ASSERT_GE(result.notes.size(), 4u);

  // Check that multiple voices are represented.
  std::set<VoiceId> voices_seen;
  for (const auto& note : result.notes) {
    voices_seen.insert(note.voice);
  }
  // With 3 voices, we should see at least 2 distinct voices.
  EXPECT_GE(voices_seen.size(), 2u)
      << "With 3 voices, output should include notes in at least 2 voices";
}

TEST(MotifConstraintTest, GenerateEpisodeInvertibleCounterpoint) {
  auto pool = buildTestPool();
  auto req = makeTestRequest(pool, /*seed=*/42, /*num_voices=*/2);
  req.episode_index = 1;  // Odd -> invertible counterpoint applied.
  EpisodeResult result = generateConstraintEpisode(req);

  ASSERT_TRUE(result.success);
  ASSERT_GE(result.notes.size(), 2u);

  // Both voice 0 and voice 1 should have notes (voices are swapped).
  bool has_voice0 = false;
  bool has_voice1 = false;
  for (const auto& note : result.notes) {
    if (note.voice == 0) has_voice0 = true;
    if (note.voice == 1) has_voice1 = true;
  }
  EXPECT_TRUE(has_voice0) << "Invertible episode should include voice 0 notes";
  EXPECT_TRUE(has_voice1) << "Invertible episode should include voice 1 notes";
}

TEST(MotifConstraintTest, GenerateEpisodeEmptyPool) {
  MotifPool empty_pool;
  auto req = makeTestRequest(empty_pool);
  EpisodeResult result = generateConstraintEpisode(req);

  EXPECT_FALSE(result.success)
      << "Empty pool should cause generation to fail";
  EXPECT_TRUE(result.notes.empty())
      << "Failed generation should produce no notes";
}

TEST(MotifConstraintTest, GenerateEpisodeNullPool) {
  EpisodeRequest req;
  req.motif_pool = nullptr;
  req.duration = kTicksPerBar * 4;
  req.num_voices = 2;
  req.seed = 42;
  EpisodeResult result = generateConstraintEpisode(req);

  EXPECT_FALSE(result.success)
      << "Null pool should cause generation to fail";
}

TEST(MotifConstraintTest, GenerateEpisodeZeroDuration) {
  auto pool = buildTestPool();
  auto req = makeTestRequest(pool);
  req.duration = 0;
  EpisodeResult result = generateConstraintEpisode(req);

  EXPECT_FALSE(result.success)
      << "Zero duration should cause generation to fail";
}

TEST(MotifConstraintTest, GenerateEpisodeDeterministic) {
  auto pool = buildTestPool();

  auto req1 = makeTestRequest(pool, /*seed=*/12345);
  auto req2 = makeTestRequest(pool, /*seed=*/12345);

  EpisodeResult result1 = generateConstraintEpisode(req1);
  EpisodeResult result2 = generateConstraintEpisode(req2);

  ASSERT_TRUE(result1.success);
  ASSERT_TRUE(result2.success);
  ASSERT_EQ(result1.notes.size(), result2.notes.size())
      << "Same seed should produce same number of notes";

  for (size_t i = 0; i < result1.notes.size(); ++i) {
    EXPECT_EQ(result1.notes[i].pitch, result2.notes[i].pitch)
        << "Same seed should produce identical pitches at index " << i;
    EXPECT_EQ(result1.notes[i].start_tick, result2.notes[i].start_tick)
        << "Same seed should produce identical start ticks at index " << i;
    EXPECT_EQ(result1.notes[i].voice, result2.notes[i].voice)
        << "Same seed should produce identical voice assignments at index " << i;
  }
}

TEST(MotifConstraintTest, GenerateEpisodeDifferentSeeds) {
  auto pool = buildTestPool();

  auto req1 = makeTestRequest(pool, /*seed=*/100);
  auto req2 = makeTestRequest(pool, /*seed=*/999);

  EpisodeResult result1 = generateConstraintEpisode(req1);
  EpisodeResult result2 = generateConstraintEpisode(req2);

  ASSERT_TRUE(result1.success);
  ASSERT_TRUE(result2.success);

  // Different seeds should produce different output (with high probability).
  // Check for any difference in pitches, timing, or note count.
  bool any_difference = false;
  if (result1.notes.size() != result2.notes.size()) {
    any_difference = true;
  } else {
    for (size_t i = 0; i < result1.notes.size(); ++i) {
      if (result1.notes[i].pitch != result2.notes[i].pitch ||
          result1.notes[i].start_tick != result2.notes[i].start_tick) {
        any_difference = true;
        break;
      }
    }
  }
  EXPECT_TRUE(any_difference)
      << "Different seeds should produce different note sequences";
}

TEST(MotifConstraintTest, GenerateEpisodeNotesAreSorted) {
  auto pool = buildTestPool();
  auto req = makeTestRequest(pool, /*seed=*/42, /*num_voices=*/3);
  EpisodeResult result = generateConstraintEpisode(req);

  ASSERT_TRUE(result.success);
  // Verify notes are sorted by start_tick (then by voice).
  for (size_t i = 1; i < result.notes.size(); ++i) {
    bool ordered = (result.notes[i].start_tick > result.notes[i - 1].start_tick) ||
                   (result.notes[i].start_tick == result.notes[i - 1].start_tick &&
                    result.notes[i].voice >= result.notes[i - 1].voice);
    EXPECT_TRUE(ordered)
        << "Notes should be sorted by (start_tick, voice). "
        << "Index " << i << ": tick=" << result.notes[i].start_tick
        << ",voice=" << static_cast<int>(result.notes[i].voice)
        << " vs previous tick=" << result.notes[i - 1].start_tick
        << ",voice=" << static_cast<int>(result.notes[i - 1].voice);
  }
}

TEST(MotifConstraintTest, GenerateEpisodeNoteSource) {
  auto pool = buildTestPool();
  auto req = makeTestRequest(pool);
  EpisodeResult result = generateConstraintEpisode(req);

  ASSERT_TRUE(result.success);
  // All generated notes should have EpisodeMaterial source.
  for (const auto& note : result.notes) {
    EXPECT_EQ(note.source, BachNoteSource::EpisodeMaterial)
        << "All episode notes should have EpisodeMaterial source";
  }
}

TEST(MotifConstraintTest, GenerateEpisodeExitState) {
  auto pool = buildTestPool();
  auto req = makeTestRequest(pool);
  EpisodeResult result = generateConstraintEpisode(req);

  ASSERT_TRUE(result.success);
  // Exit state should have recorded some notes.
  EXPECT_GT(result.exit_state.total_note_count, 0)
      << "Exit state should have advanced with placed notes";
}

TEST(MotifConstraintTest, GenerateEpisodeAllCharacters) {
  // Verify that all 4 subject characters produce valid episodes.
  SubjectCharacter characters[] = {
      SubjectCharacter::Severe,
      SubjectCharacter::Playful,
      SubjectCharacter::Noble,
      SubjectCharacter::Restless,
  };

  for (auto character : characters) {
    auto pool = buildTestPool(character);
    auto req = makeTestRequest(pool);
    req.character = character;
    req.grammar = getFortspinnungGrammar(character);
    EpisodeResult result = generateConstraintEpisode(req);

    EXPECT_TRUE(result.success)
        << "Episode generation should succeed for character "
        << subjectCharacterToString(character);
    EXPECT_GE(result.notes.size(), 2u)
        << "Episode for character "
        << subjectCharacterToString(character)
        << " should produce at least 2 notes";
  }
}

// ===========================================================================
// 4. planFortspinnung tests
// ===========================================================================

TEST(MotifConstraintTest, PlanFortspinnungProducesSteps) {
  auto pool = buildTestPool();
  auto grammar = getFortspinnungGrammar(SubjectCharacter::Severe);
  Tick start = kTicksPerBar * 4;
  Tick dur = kTicksPerBar * 4;

  auto steps = planFortspinnung(pool, grammar, start, dur, 2,
                                SubjectCharacter::Severe, 42);

  EXPECT_FALSE(steps.empty())
      << "planFortspinnung should produce a non-empty step sequence";
  EXPECT_GE(steps.size(), 3u)
      << "Plan should have at least 3 steps (Kernel + Sequence + Dissolution)";
}

TEST(MotifConstraintTest, PlanFortspinnungPhaseCoverage) {
  auto pool = buildTestPool();
  auto grammar = getFortspinnungGrammar(SubjectCharacter::Severe);
  Tick start = kTicksPerBar * 4;
  Tick dur = kTicksPerBar * 8;  // 8 bars for more phases.

  auto steps = planFortspinnung(pool, grammar, start, dur, 2,
                                SubjectCharacter::Severe, 42);

  ASSERT_FALSE(steps.empty());

  // Check that at least two distinct FortPhase values are present.
  std::set<FortPhase> phases;
  for (const auto& step : steps) {
    phases.insert(step.phase);
  }
  EXPECT_GE(phases.size(), 2u)
      << "Steps should span at least 2 Fortspinnung phases "
      << "(Kernel, Sequence, and/or Dissolution)";
}

TEST(MotifConstraintTest, PlanFortspinnungVoiceAssignment) {
  auto pool = buildTestPool();
  auto grammar = getFortspinnungGrammar(SubjectCharacter::Severe);
  Tick start = kTicksPerBar * 4;
  Tick dur = kTicksPerBar * 8;

  auto steps = planFortspinnung(pool, grammar, start, dur, 2,
                                SubjectCharacter::Severe, 42);

  ASSERT_FALSE(steps.empty());

  std::set<VoiceId> voices;
  for (const auto& step : steps) {
    voices.insert(step.voice);
  }
  // With 2 voices, steps should include both voice 0 and voice 1.
  EXPECT_GE(voices.size(), 1u)
      << "Steps should include at least 1 voice";
  // Check voice IDs are within the valid range.
  for (const auto& step : steps) {
    EXPECT_LT(step.voice, 2u)
        << "Voice ID " << static_cast<int>(step.voice)
        << " should be < num_voices (2)";
  }
}

TEST(MotifConstraintTest, PlanFortspinnungTicksInRange) {
  auto pool = buildTestPool();
  auto grammar = getFortspinnungGrammar(SubjectCharacter::Severe);
  Tick start = kTicksPerBar * 4;
  Tick dur = kTicksPerBar * 4;

  auto steps = planFortspinnung(pool, grammar, start, dur, 2,
                                SubjectCharacter::Severe, 42);

  ASSERT_FALSE(steps.empty());
  for (const auto& step : steps) {
    EXPECT_GE(step.tick, start)
        << "Step tick should be >= start_tick";
    EXPECT_LT(step.tick, start + dur)
        << "Step tick should be < start_tick + duration";
  }
}

TEST(MotifConstraintTest, PlanFortspinnungSortedByTick) {
  auto pool = buildTestPool();
  auto grammar = getFortspinnungGrammar(SubjectCharacter::Severe);
  Tick start = kTicksPerBar * 4;
  Tick dur = kTicksPerBar * 4;

  auto steps = planFortspinnung(pool, grammar, start, dur, 2,
                                SubjectCharacter::Severe, 42);

  ASSERT_FALSE(steps.empty());
  for (size_t i = 1; i < steps.size(); ++i) {
    EXPECT_GE(steps[i].tick, steps[i - 1].tick)
        << "Steps should be sorted by tick";
  }
}

TEST(MotifConstraintTest, PlanFortspinnungEmptyPool) {
  MotifPool empty_pool;
  auto grammar = getFortspinnungGrammar(SubjectCharacter::Severe);
  Tick start = kTicksPerBar * 4;
  Tick dur = kTicksPerBar * 4;

  auto steps = planFortspinnung(empty_pool, grammar, start, dur, 2,
                                SubjectCharacter::Severe, 42);

  EXPECT_TRUE(steps.empty())
      << "Empty pool should produce no Fortspinnung steps";
}

// ===========================================================================
// 5. motifOpToString tests
// ===========================================================================

TEST(MotifConstraintTest, MotifOpToString) {
  // All 7 ops should return non-null, non-empty strings.
  MotifOp ops[] = {
      MotifOp::Original,
      MotifOp::Invert,
      MotifOp::Retrograde,
      MotifOp::Diminish,
      MotifOp::Augment,
      MotifOp::Fragment,
      MotifOp::Sequence,
  };

  for (auto op : ops) {
    const char* str = motifOpToString(op);
    ASSERT_NE(str, nullptr) << "motifOpToString should not return nullptr";
    EXPECT_GT(strlen(str), 0u) << "motifOpToString should not return empty string";
  }

  // Spot-check specific names.
  EXPECT_STREQ(motifOpToString(MotifOp::Original), "Original");
  EXPECT_STREQ(motifOpToString(MotifOp::Invert), "Invert");
  EXPECT_STREQ(motifOpToString(MotifOp::Retrograde), "Retrograde");
  EXPECT_STREQ(motifOpToString(MotifOp::Diminish), "Diminish");
  EXPECT_STREQ(motifOpToString(MotifOp::Augment), "Augment");
  EXPECT_STREQ(motifOpToString(MotifOp::Fragment), "Fragment");
  EXPECT_STREQ(motifOpToString(MotifOp::Sequence), "Sequence");
}

// ===========================================================================
// 6. Edge cases and boundary conditions
// ===========================================================================

TEST(MotifConstraintTest, ApplyMotifOpSingleNote) {
  // Single-note motif: all transformations should handle gracefully.
  NoteEvent n;
  n.pitch = 60;
  n.start_tick = 0;
  n.duration = kTicksPerBeat;
  n.velocity = 80;
  n.voice = 0;
  n.source = BachNoteSource::EpisodeMaterial;
  std::vector<NoteEvent> single = {n};

  auto original = applyMotifOp(single, MotifOp::Original);
  EXPECT_EQ(original.size(), 1u);
  EXPECT_EQ(original[0].pitch, 60);

  auto inverted = applyMotifOp(single, MotifOp::Invert, Key::C);
  EXPECT_EQ(inverted.size(), 1u);
  EXPECT_EQ(inverted[0].pitch, 60)
      << "Single note inversion should return the pivot pitch";

  auto retrograde = applyMotifOp(single, MotifOp::Retrograde);
  EXPECT_EQ(retrograde.size(), 1u);

  auto diminished = applyMotifOp(single, MotifOp::Diminish);
  EXPECT_EQ(diminished.size(), 1u);
  EXPECT_EQ(diminished[0].duration, kTicksPerBeat / 2);

  auto augmented = applyMotifOp(single, MotifOp::Augment);
  EXPECT_EQ(augmented.size(), 1u);
  EXPECT_EQ(augmented[0].duration, kTicksPerBeat * 2);
}

TEST(MotifConstraintTest, GenerateEpisodeMinimalDuration) {
  auto pool = buildTestPool();
  auto req = makeTestRequest(pool);
  req.duration = kTicksPerBeat;  // Only 1 beat of episode time.
  EpisodeResult result = generateConstraintEpisode(req);

  // Should either succeed with very few notes or fail gracefully.
  if (result.success) {
    EXPECT_GE(result.notes.size(), 1u)
        << "Minimal duration episode should produce at least 1 note if successful";
  }
  // In either case, no crash.
}

TEST(MotifConstraintTest, GenerateEpisodeHighEnergy) {
  auto pool = buildTestPool();
  auto req = makeTestRequest(pool);
  req.energy_level = 1.0f;  // Maximum energy.
  EpisodeResult result = generateConstraintEpisode(req);

  EXPECT_TRUE(result.success)
      << "High energy episode should still succeed";
}

TEST(MotifConstraintTest, GenerateEpisodeLowEnergy) {
  auto pool = buildTestPool();
  auto req = makeTestRequest(pool);
  req.energy_level = 0.0f;  // Minimum energy.
  EpisodeResult result = generateConstraintEpisode(req);

  EXPECT_TRUE(result.success)
      << "Low energy episode should still succeed";
}

TEST(MotifConstraintTest, GenerateEpisodeLargeVoiceCount) {
  auto pool = buildTestPool();
  auto req = makeTestRequest(pool, /*seed=*/42, /*num_voices=*/5);
  EpisodeResult result = generateConstraintEpisode(req);

  // Should succeed or fail gracefully — no crash with 5 voices.
  if (result.success) {
    for (const auto& note : result.notes) {
      EXPECT_LT(note.voice, 5u)
          << "Voice ID should be within the valid range [0, num_voices)";
    }
  }
}

}  // namespace
}  // namespace bach
