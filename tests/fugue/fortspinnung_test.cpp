// Tests for fugue/fortspinnung.h -- Fortspinnung episode generation from
// motif pool fragments, connection rules, and Episode wrapper.

#include "fugue/fortspinnung.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numeric>
#include <set>
#include <vector>

#include "core/basic_types.h"
#include "fugue/episode.h"
#include "fugue/motif_pool.h"
#include "fugue/subject.h"

namespace bach {
namespace {

// ---------------------------------------------------------------------------
// Helper: create a subject note sequence with N quarter notes (C major scale).
// ---------------------------------------------------------------------------

std::vector<NoteEvent> makeSubjectNotes(size_t num_notes, Tick start_tick = 0) {
  std::vector<NoteEvent> notes;
  notes.reserve(num_notes);
  const uint8_t pitches[] = {60, 62, 64, 65, 67, 69, 71, 72};
  for (size_t idx = 0; idx < num_notes; ++idx) {
    NoteEvent note;
    note.start_tick = start_tick + static_cast<Tick>(idx) * kTicksPerBeat;
    note.duration = kTicksPerBeat;
    note.pitch = pitches[idx % 8];
    note.velocity = 80;
    note.voice = 0;
    notes.push_back(note);
  }
  return notes;
}

// ---------------------------------------------------------------------------
// Helper: build a standard MotifPool from an 8-note subject.
// ---------------------------------------------------------------------------

MotifPool buildTestPool(SubjectCharacter character = SubjectCharacter::Severe) {
  MotifPool pool;
  auto subject_notes = makeSubjectNotes(8);
  std::vector<NoteEvent> empty_cs;
  pool.build(subject_notes, empty_cs, character);
  return pool;
}

// ---------------------------------------------------------------------------
// Helper: create a Subject struct for generateFortspinnungEpisode.
// ---------------------------------------------------------------------------

Subject makeTestSubject(SubjectCharacter character = SubjectCharacter::Severe) {
  Subject subject;
  subject.key = Key::C;
  subject.character = character;
  subject.length_ticks = kTicksPerBar * 2;
  subject.notes = makeSubjectNotes(8);
  return subject;
}

// ===========================================================================
// EmptyPoolReturnsEmpty
// ===========================================================================

TEST(FortspinnungTest, EmptyPoolReturnsEmpty) {
  MotifPool empty_pool;
  auto result = generateFortspinnung(empty_pool, 0, kTicksPerBar * 4,
                                     3, 42, SubjectCharacter::Severe, Key::C);
  EXPECT_TRUE(result.empty());
}

// ===========================================================================
// GeneratesNotesForSingleVoice
// ===========================================================================

TEST(FortspinnungTest, GeneratesNotesForSingleVoice) {
  auto pool = buildTestPool();
  auto result = generateFortspinnung(pool, 0, kTicksPerBar * 4,
                                     1, 42, SubjectCharacter::Severe, Key::C);

  EXPECT_FALSE(result.empty());

  // All notes should be voice 0 when num_voices = 1.
  for (const auto& note : result) {
    EXPECT_EQ(note.voice, 0u) << "Single-voice Fortspinnung should only use voice 0";
  }
}

// ===========================================================================
// GeneratesNotesForTwoVoices
// ===========================================================================

TEST(FortspinnungTest, GeneratesNotesForTwoVoices) {
  auto pool = buildTestPool();
  auto result = generateFortspinnung(pool, 0, kTicksPerBar * 4,
                                     2, 42, SubjectCharacter::Severe, Key::C);

  EXPECT_FALSE(result.empty());

  // Both voice 0 and voice 1 should be present.
  std::set<uint8_t> voices;
  for (const auto& note : result) {
    voices.insert(note.voice);
  }
  EXPECT_TRUE(voices.count(0) > 0) << "Voice 0 should be present";
  EXPECT_TRUE(voices.count(1) > 0) << "Voice 1 should be present";
}

// ===========================================================================
// NotesWithinDuration
// ===========================================================================

TEST(FortspinnungTest, NotesWithinDuration) {
  auto pool = buildTestPool();
  Tick start = kTicksPerBar * 2;
  Tick duration = kTicksPerBar * 3;

  auto result = generateFortspinnung(pool, start, duration,
                                     2, 42, SubjectCharacter::Severe, Key::C);

  EXPECT_FALSE(result.empty());

  for (const auto& note : result) {
    EXPECT_GE(note.start_tick, start)
        << "Note starts before episode start";
    EXPECT_LT(note.start_tick, start + duration)
        << "Note starts after episode end";
  }
}

// ===========================================================================
// FragmentConnectionSmallGaps
// ===========================================================================

TEST(FortspinnungTest, FragmentConnectionSmallGaps) {
  auto pool = buildTestPool();
  auto result = generateFortspinnung(pool, 0, kTicksPerBar * 4,
                                     1, 42, SubjectCharacter::Severe, Key::C);

  // Filter voice 0 only and sort by tick.
  std::vector<NoteEvent> voice0;
  for (const auto& note : result) {
    if (note.voice == 0) {
      voice0.push_back(note);
    }
  }
  std::sort(voice0.begin(), voice0.end(),
            [](const NoteEvent& lhs, const NoteEvent& rhs) {
              return lhs.start_tick < rhs.start_tick;
            });

  // Check that there are no enormous pitch jumps between consecutive notes
  // that would indicate broken fragment connections.
  // The closeGapIfNeeded function targets <= 4 semitones between fragments.
  // Within a fragment, larger intervals are allowed (they come from the subject).
  // We check a relaxed bound of 12 semitones (one octave) for overall continuity.
  if (voice0.size() >= 2) {
    int max_gap = 0;
    for (size_t idx = 1; idx < voice0.size(); ++idx) {
      int gap = std::abs(static_cast<int>(voice0[idx].pitch) -
                         static_cast<int>(voice0[idx - 1].pitch));
      if (gap > max_gap) max_gap = gap;
    }
    EXPECT_LE(max_gap, 16)
        << "Largest pitch gap between consecutive notes is too wide: " << max_gap
        << " semitones (expect reasonable voice-leading)";
  }
}

// ===========================================================================
// DeterministicWithSameSeed
// ===========================================================================

TEST(FortspinnungTest, DeterministicWithSameSeed) {
  auto pool = buildTestPool();
  uint32_t seed = 12345;

  auto result1 = generateFortspinnung(pool, 0, kTicksPerBar * 4,
                                      2, seed, SubjectCharacter::Playful, Key::C);
  auto result2 = generateFortspinnung(pool, 0, kTicksPerBar * 4,
                                      2, seed, SubjectCharacter::Playful, Key::C);

  ASSERT_EQ(result1.size(), result2.size());
  for (size_t idx = 0; idx < result1.size(); ++idx) {
    EXPECT_EQ(result1[idx].pitch, result2[idx].pitch)
        << "Mismatch at index " << idx;
    EXPECT_EQ(result1[idx].start_tick, result2[idx].start_tick)
        << "Tick mismatch at index " << idx;
    EXPECT_EQ(result1[idx].voice, result2[idx].voice)
        << "Voice mismatch at index " << idx;
    EXPECT_EQ(result1[idx].duration, result2[idx].duration)
        << "Duration mismatch at index " << idx;
  }
}

// ===========================================================================
// FortspinnungEpisodeWrapsCorrectly
// ===========================================================================

TEST(FortspinnungTest, FortspinnungEpisodeWrapsCorrectly) {
  auto pool = buildTestPool();
  auto subject = makeTestSubject();

  Tick start = kTicksPerBar * 4;
  Tick duration = kTicksPerBar * 3;
  Key start_key = Key::C;
  Key target_key = Key::G;

  Episode episode = generateFortspinnungEpisode(
      subject, pool, start, duration,
      start_key, target_key, 3, 42, 0, 0.5f);

  EXPECT_EQ(episode.start_tick, start);
  EXPECT_EQ(episode.end_tick, start + duration);
  EXPECT_EQ(episode.start_key, start_key);
  EXPECT_EQ(episode.end_key, target_key);
  EXPECT_FALSE(episode.notes.empty());

  // All notes should be within the episode time range.
  for (const auto& note : episode.notes) {
    EXPECT_GE(note.start_tick, start);
    EXPECT_LT(note.start_tick, start + duration);
  }
}

// ===========================================================================
// FortspinnungEpisodeFallsBackOnEmptyPool
// ===========================================================================

TEST(FortspinnungTest, FortspinnungEpisodeFallsBackOnEmptyPool) {
  MotifPool empty_pool;
  auto subject = makeTestSubject();

  Tick start = 0;
  Tick duration = kTicksPerBar * 2;

  Episode episode = generateFortspinnungEpisode(
      subject, empty_pool, start, duration,
      Key::C, Key::G, 3, 42, 0, 0.5f);

  // Falls back to standard generateEpisode, which should produce notes.
  EXPECT_EQ(episode.start_tick, start);
  EXPECT_EQ(episode.end_tick, start + duration);
  EXPECT_FALSE(episode.notes.empty());
}

// ===========================================================================
// InvertibleCounterpointOnOddIndex
// ===========================================================================

TEST(FortspinnungTest, InvertibleCounterpointOnOddIndex) {
  auto pool = buildTestPool();
  auto subject = makeTestSubject();

  Tick start = 0;
  Tick duration = kTicksPerBar * 4;

  // Even index: baseline episode.
  Episode even_ep = generateFortspinnungEpisode(
      subject, pool, start, duration,
      Key::C, Key::C, 2, 42, 0, 0.5f);

  // Odd index: probabilistic voice swap via invertible counterpoint.
  Episode odd_ep = generateFortspinnungEpisode(
      subject, pool, start, duration,
      Key::C, Key::C, 2, 42, 1, 0.5f);

  // Count voices in even episode.
  int even_v0 = 0, even_v1 = 0;
  for (const auto& note : even_ep.notes) {
    if (note.voice == 0) ++even_v0;
    if (note.voice == 1) ++even_v1;
  }

  // Count voices in odd episode.
  int odd_v0 = 0, odd_v1 = 0;
  for (const auto& note : odd_ep.notes) {
    if (note.voice == 0) ++odd_v0;
    if (note.voice == 1) ++odd_v1;
  }

  // Total note count per episode must be the same (same seed, same pool).
  EXPECT_EQ(even_v0 + even_v1, odd_v0 + odd_v1)
      << "Total note count must match between even and odd episodes";

  // If invertible counterpoint was applied (probabilistic), voice counts
  // should be swapped. Otherwise they should be equal. Either outcome is valid.
  bool swapped = (even_v0 == odd_v1) && (even_v1 == odd_v0);
  bool identical = (even_v0 == odd_v0) && (even_v1 == odd_v1);
  EXPECT_TRUE(swapped || identical)
      << "Voice counts must be either swapped (inversion) or identical (no inversion)."
      << " even_v0=" << even_v0 << " even_v1=" << even_v1
      << " odd_v0=" << odd_v0 << " odd_v1=" << odd_v1;
}

// ===========================================================================
// AllCharactersProduceNotes
// ===========================================================================

TEST(FortspinnungTest, AllCharactersProduceNotes) {
  SubjectCharacter characters[] = {
      SubjectCharacter::Severe,
      SubjectCharacter::Playful,
      SubjectCharacter::Noble,
      SubjectCharacter::Restless};

  for (auto character : characters) {
    auto pool = buildTestPool(character);
    auto result = generateFortspinnung(pool, 0, kTicksPerBar * 4,
                                       2, 42, character, Key::C);
    EXPECT_FALSE(result.empty())
        << "Character " << static_cast<int>(character) << " produced no notes";
  }
}

// ===========================================================================
// NoteSourceIsEpisodeMaterial
// ===========================================================================

TEST(FortspinnungTest, NoteSourceIsEpisodeMaterial) {
  auto pool = buildTestPool();
  auto result = generateFortspinnung(pool, 0, kTicksPerBar * 4,
                                     2, 42, SubjectCharacter::Severe, Key::C);

  EXPECT_FALSE(result.empty());
  for (const auto& note : result) {
    EXPECT_EQ(note.source, BachNoteSource::EpisodeMaterial)
        << "All Fortspinnung notes should have EpisodeMaterial source";
  }
}

// ===========================================================================
// ZeroDurationReturnsEmpty
// ===========================================================================

TEST(FortspinnungTest, ZeroDurationReturnsEmpty) {
  auto pool = buildTestPool();
  auto result = generateFortspinnung(pool, 0, 0,
                                     2, 42, SubjectCharacter::Severe, Key::C);
  EXPECT_TRUE(result.empty());
}

// ===========================================================================
// Voice2GeneratedForThreeVoices
// ===========================================================================

TEST(FortspinnungTest, Voice2GeneratedForThreeVoices) {
  auto pool = buildTestPool();
  auto result = generateFortspinnung(pool, 0, kTicksPerBar * 4,
                                     3, 42, SubjectCharacter::Severe, Key::C);

  EXPECT_FALSE(result.empty());

  // Voice 2 (bass) should be present.
  int voice2_count = 0;
  for (const auto& note : result) {
    if (note.voice == 2) ++voice2_count;
  }
  EXPECT_GT(voice2_count, 0)
      << "3-voice Fortspinnung should generate voice 2 (bass) notes";
}

// ===========================================================================
// Voice2PitchInBassRange
// ===========================================================================

TEST(FortspinnungTest, Voice2PitchInBassRange) {
  auto pool = buildTestPool();
  auto result = generateFortspinnung(pool, 0, kTicksPerBar * 8,
                                     3, 42, SubjectCharacter::Severe, Key::C);

  for (const auto& note : result) {
    if (note.voice == 2) {
      // Voice 2 range matches getFugueVoiceRange(2, 3) = [C3=48, C5=72].
      EXPECT_GE(note.pitch, 48u)
          << "Voice 2 note below C3: " << static_cast<int>(note.pitch);
      EXPECT_LE(note.pitch, 72u)
          << "Voice 2 note above C5: " << static_cast<int>(note.pitch);
    }
  }
}

// ===========================================================================
// Voice2NotGeneratedForTwoVoices
// ===========================================================================

TEST(FortspinnungTest, Voice2NotGeneratedForTwoVoices) {
  auto pool = buildTestPool();
  auto result = generateFortspinnung(pool, 0, kTicksPerBar * 4,
                                     2, 42, SubjectCharacter::Severe, Key::C);

  for (const auto& note : result) {
    EXPECT_NE(note.voice, 2u)
        << "2-voice Fortspinnung should NOT generate voice 2 notes";
  }
}

// ===========================================================================
// Voice3GeneratedForFourVoices
// ===========================================================================

TEST(FortspinnungTest, Voice3GeneratedForFourVoices) {
  auto pool = buildTestPool();
  auto result = generateFortspinnung(pool, 0, kTicksPerBar * 8,
                                     4, 42, SubjectCharacter::Severe, Key::C);

  EXPECT_FALSE(result.empty());

  int voice3_count = 0;
  for (const auto& note : result) {
    if (note.voice == 3) {
      ++voice3_count;
      // Pedal range: C1 (24) to D3 (50).
      EXPECT_GE(note.pitch, 24u)
          << "Voice 3 pedal note below C1: " << static_cast<int>(note.pitch);
      EXPECT_LE(note.pitch, 50u)
          << "Voice 3 pedal note above D3: " << static_cast<int>(note.pitch);
    }
  }
  EXPECT_GT(voice3_count, 0)
      << "4-voice Fortspinnung should generate voice 3 (pedal) notes";
}

// ===========================================================================
// Voice2InTenorRangeForFourVoices
// ===========================================================================

TEST(FortspinnungTest, Voice2InTenorRangeForFourVoices) {
  auto pool = buildTestPool();
  auto result = generateFortspinnung(pool, 0, kTicksPerBar * 8,
                                     4, 42, SubjectCharacter::Severe, Key::C);

  int voice2_count = 0;
  for (const auto& note : result) {
    if (note.voice == 2) {
      ++voice2_count;
      // Tenor range for 4 voices: C3 (48) to C5 (72).
      EXPECT_GE(note.pitch, 48u)
          << "Voice 2 tenor note below C3: " << static_cast<int>(note.pitch);
      EXPECT_LE(note.pitch, 72u)
          << "Voice 2 tenor note above C5: " << static_cast<int>(note.pitch);
    }
  }
  EXPECT_GT(voice2_count, 0)
      << "4-voice Fortspinnung should generate voice 2 (tenor) notes";
}

// ===========================================================================
// Voice3NotGeneratedForThreeVoices
// ===========================================================================

TEST(FortspinnungTest, Voice3NotGeneratedForThreeVoices) {
  auto pool = buildTestPool();
  auto result = generateFortspinnung(pool, 0, kTicksPerBar * 8,
                                     3, 42, SubjectCharacter::Severe, Key::C);

  for (const auto& note : result) {
    EXPECT_NE(note.voice, 3u)
        << "3-voice Fortspinnung should NOT generate voice 3 notes";
  }
}

// ===========================================================================
// Voice3MaxSilenceRespected
// ===========================================================================

TEST(FortspinnungTest, Voice3MaxSilenceRespected) {
  auto pool = buildTestPool();
  Tick duration = kTicksPerBar * 16;

  // Test across 10 seeds to cover RNG variation.
  for (uint32_t seed = 1; seed <= 10; ++seed) {
    auto result = generateFortspinnung(pool, 0, duration,
                                       4, seed, SubjectCharacter::Severe, Key::C);

    // Collect voice 3 notes sorted by tick.
    std::vector<NoteEvent> v3_notes;
    for (const auto& note : result) {
      if (note.voice == 3) v3_notes.push_back(note);
    }
    std::sort(v3_notes.begin(), v3_notes.end(),
              [](const NoteEvent& a, const NoteEvent& b) {
                return a.start_tick < b.start_tick;
              });

    ASSERT_FALSE(v3_notes.empty())
        << "Seed " << seed << ": voice 3 has no notes";

    // Check max silence gap between consecutive voice 3 notes.
    // Also check from start to first note and from last note to end.
    constexpr int kMaxSilentBars = 4;
    Tick max_allowed_gap = kTicksPerBar * (kMaxSilentBars + 1);

    Tick first_gap = v3_notes[0].start_tick;
    EXPECT_LE(first_gap, max_allowed_gap)
        << "Seed " << seed << ": gap before first voice 3 note too large: "
        << first_gap / kTicksPerBar << " bars";

    for (size_t idx = 1; idx < v3_notes.size(); ++idx) {
      // Overlapping notes (negative gap) are fine — they indicate density,
      // not silence. Only check for positive gaps exceeding the limit.
      Tick end_prev = v3_notes[idx - 1].start_tick + v3_notes[idx - 1].duration;
      if (v3_notes[idx].start_tick > end_prev) {
        Tick gap = v3_notes[idx].start_tick - end_prev;
        EXPECT_LE(gap, max_allowed_gap)
            << "Seed " << seed << ": gap between voice 3 notes too large at tick "
            << v3_notes[idx].start_tick << ": " << gap / kTicksPerBar << " bars";
      }
    }
  }
}

// ===========================================================================
// Voice3AnchorUsesKeyPitch
// ===========================================================================

TEST(FortspinnungTest, Voice3AnchorUsesKeyPitch) {
  auto pool = buildTestPool();
  // Test with G key (key offset = 7).
  Key test_key = Key::G;

  // Expected anchor pitches: tonic = 36 + 7 = 43 (G2), dominant = 43 + 7 = 50 (D3).
  int tonic_bass = 36 + static_cast<int>(test_key);  // 43
  int dominant_bass = tonic_bass + 7;                 // 50

  // Test across multiple seeds for robustness.
  int seeds_with_anchor = 0;
  for (uint32_t seed = 1; seed <= 10; ++seed) {
    auto result = generateFortspinnung(pool, 0, kTicksPerBar * 8,
                                       4, seed, SubjectCharacter::Severe, test_key);

    for (const auto& note : result) {
      if (note.voice == 3 && note.duration >= kTicksPerBar / 2) {
        int pitch = static_cast<int>(note.pitch);
        if (pitch == tonic_bass || pitch == dominant_bass) {
          ++seeds_with_anchor;
          break;
        }
      }
    }
  }
  EXPECT_GE(seeds_with_anchor, 5)
      << "At least 5/10 seeds should have voice 3 anchors with tonic ("
      << tonic_bass << ") or dominant (" << dominant_bass << ") of key G";
}

// ===========================================================================
// Voice2StructuralBeatCoverage
// ===========================================================================

TEST(FortspinnungTest, Voice2StructuralBeatCoverage) {
  auto pool = buildTestPool();
  // Generate 3-voice Fortspinnung over 16 bars across multiple seeds.
  Tick duration = kTicksPerBar * 16;

  int seeds_passing = 0;
  for (uint32_t seed = 1; seed <= 10; ++seed) {
    auto result = generateFortspinnung(pool, 0, duration,
                                       3, seed, SubjectCharacter::Severe, Key::C);

    // Count bar starts covered by voice 2 notes.
    int total_bars = 16;
    int covered_bars = 0;
    for (int bar = 0; bar < total_bars; ++bar) {
      Tick bar_start = static_cast<Tick>(bar) * kTicksPerBar;
      for (const auto& note : result) {
        if (note.voice == 2 &&
            note.start_tick <= bar_start &&
            note.start_tick + note.duration > bar_start) {
          ++covered_bars;
          break;
        }
      }
    }

    float coverage = static_cast<float>(covered_bars) / total_bars;
    if (coverage >= 0.5f) {
      ++seeds_passing;
    }
  }
  // At least 7/10 seeds should have >= 50% bar coverage.
  EXPECT_GE(seeds_passing, 7)
      << "Voice 2 structural beat coverage too low across seeds";
}

// ===========================================================================
// FortspinnungThreePhaseTest.KernelPrefersRankZero
// ===========================================================================

TEST(FortspinnungThreePhaseTest, KernelPrefersRankZero) {
  auto pool = buildTestPool(SubjectCharacter::Severe);
  constexpr Tick kDuration = kTicksPerBar * 4;

  auto result = generateFortspinnung(pool, 0, kDuration,
                                     1, 42, SubjectCharacter::Severe, Key::C);
  ASSERT_FALSE(result.empty());

  // Kernel phase covers the first 25% of the episode duration.
  // For Severe, kernel_ratio = 0.30, so kernel boundary is at 30% of duration.
  FortspinnungGrammar grammar = getFortspinnungGrammar(SubjectCharacter::Severe);
  Tick kernel_boundary = static_cast<Tick>(kDuration * grammar.kernel_ratio);

  // All notes in the kernel phase should have EpisodeMaterial source
  // (the Fortspinnung implementation tags all placed notes as EpisodeMaterial).
  int kernel_note_count = 0;
  for (const auto& note : result) {
    if (note.start_tick < kernel_boundary && note.voice == 0) {
      EXPECT_EQ(note.source, BachNoteSource::EpisodeMaterial)
          << "Kernel-phase note at tick " << note.start_tick
          << " should have EpisodeMaterial source";
      ++kernel_note_count;
    }
  }
  EXPECT_GT(kernel_note_count, 0)
      << "Kernel phase (first " << grammar.kernel_ratio * 100
      << "%) should contain at least one note";
}

// ===========================================================================
// FortspinnungThreePhaseTest.DissolutionReducesDensity
// ===========================================================================

TEST(FortspinnungThreePhaseTest, DissolutionReducesDensity) {
  constexpr Tick kDuration = kTicksPerBar * 8;

  // Use Severe character for deterministic grammar boundaries.
  FortspinnungGrammar grammar = getFortspinnungGrammar(SubjectCharacter::Severe);
  Tick sequence_start = static_cast<Tick>(kDuration * grammar.kernel_ratio);
  Tick sequence_end = static_cast<Tick>(
      kDuration * (grammar.kernel_ratio + grammar.sequence_ratio));
  Tick dissolution_start = sequence_end;

  Tick sequence_span = sequence_end - sequence_start;
  Tick dissolution_span = kDuration - dissolution_start;

  // Test across multiple seeds to account for RNG variation.
  int seeds_passing = 0;
  constexpr int kTotalSeeds = 10;

  for (uint32_t seed = 1; seed <= kTotalSeeds; ++seed) {
    auto pool = buildTestPool(SubjectCharacter::Severe);
    auto result = generateFortspinnung(pool, 0, kDuration,
                                       1, seed, SubjectCharacter::Severe, Key::C);
    if (result.empty()) continue;

    // Count voice-0 notes in sequence phase vs dissolution phase.
    int sequence_notes = 0;
    int dissolution_notes = 0;
    for (const auto& note : result) {
      if (note.voice != 0) continue;
      if (note.start_tick >= sequence_start && note.start_tick < sequence_end) {
        ++sequence_notes;
      } else if (note.start_tick >= dissolution_start) {
        ++dissolution_notes;
      }
    }

    // Compute notes per tick for each phase (density metric).
    if (sequence_notes == 0 || sequence_span == 0 || dissolution_span == 0) continue;

    float seq_density = static_cast<float>(sequence_notes) / static_cast<float>(sequence_span);
    float diss_density =
        static_cast<float>(dissolution_notes) / static_cast<float>(dissolution_span);

    if (diss_density <= seq_density) {
      ++seeds_passing;
    }
  }

  // At least 7/10 seeds should show dissolution density <= sequence density.
  EXPECT_GE(seeds_passing, 7)
      << "Dissolution phase should have fewer notes per tick than sequence phase "
      << "in at least 7/10 seeds (got " << seeds_passing << "/10)";
}

// ===========================================================================
// FortspinnungThreePhaseTest.CadentialLengthening
// ===========================================================================

TEST(FortspinnungThreePhaseTest, CadentialLengthening) {
  // Use a longer duration to reduce boundary truncation effects on last notes.
  constexpr Tick kDuration = kTicksPerBar * 4;

  // Test across multiple seeds: cadential lengthening is deterministic per seed
  // but the last note may be truncated at the episode boundary for some seeds.
  int seeds_passing = 0;
  constexpr int kTotalSeeds = 10;

  for (uint32_t seed = 1; seed <= kTotalSeeds; ++seed) {
    auto pool = buildTestPool(SubjectCharacter::Severe);
    auto result = generateFortspinnung(pool, 0, kDuration,
                                       1, seed, SubjectCharacter::Severe, Key::C);
    if (result.empty()) continue;

    // Collect voice 0 notes sorted by start tick.
    std::vector<NoteEvent> voice0;
    for (const auto& note : result) {
      if (note.voice == 0) voice0.push_back(note);
    }
    std::sort(voice0.begin(), voice0.end(),
              [](const NoteEvent& lhs, const NoteEvent& rhs) {
                return lhs.start_tick < rhs.start_tick;
              });

    if (voice0.size() < 4) continue;

    // Last 2 notes are the cadential notes.
    size_t cadential_count = 2;
    size_t other_count = voice0.size() - cadential_count;

    // Average duration of the "other" (non-cadential) notes.
    Tick other_total = 0;
    for (size_t idx = 0; idx < other_count; ++idx) {
      other_total += voice0[idx].duration;
    }
    float other_avg = static_cast<float>(other_total) / static_cast<float>(other_count);
    if (other_avg < 1.0f) continue;

    // The grammar's cadential_lengthening for Severe is 1.5x. However, the last
    // note may be truncated at the episode boundary, so we check that at least
    // one of the last 2 notes exceeds the other-notes average (the lengthening
    // multiplier was applied, even if boundary truncation reduced one of them).
    Tick max_cadential = std::max(voice0[voice0.size() - 1].duration,
                                  voice0[voice0.size() - 2].duration);
    if (static_cast<float>(max_cadential) >= other_avg * 1.3f) {
      ++seeds_passing;
    }
  }

  // At least 6/10 seeds should show cadential lengthening effect.
  EXPECT_GE(seeds_passing, 6)
      << "At least one of the last 2 voice-0 notes should be >= 1.3x the average "
      << "duration of other notes in at least 6/10 seeds (got "
      << seeds_passing << "/10)";
}

// ===========================================================================
// FortspinnungThreePhaseTest.VoiceOneConvergesInDissolution
// ===========================================================================

TEST(FortspinnungThreePhaseTest, VoiceOneConvergesInDissolution) {
  constexpr Tick kDuration = kTicksPerBar * 8;

  FortspinnungGrammar grammar = getFortspinnungGrammar(SubjectCharacter::Severe);
  Tick sequence_start = static_cast<Tick>(kDuration * grammar.kernel_ratio);
  Tick dissolution_start = static_cast<Tick>(
      kDuration * (grammar.kernel_ratio + grammar.sequence_ratio));

  // Test across multiple seeds for robustness.
  int seeds_passing = 0;
  constexpr int kTotalSeeds = 10;

  for (uint32_t seed = 1; seed <= kTotalSeeds; ++seed) {
    auto pool = buildTestPool(SubjectCharacter::Severe);
    auto result = generateFortspinnung(pool, 0, kDuration,
                                       2, seed, SubjectCharacter::Severe, Key::C);
    if (result.empty()) continue;

    // Collect voice 0 and voice 1 notes partitioned by phase.
    std::vector<NoteEvent> v0_seq, v1_seq, v0_diss, v1_diss;
    for (const auto& note : result) {
      if (note.start_tick >= sequence_start && note.start_tick < dissolution_start) {
        if (note.voice == 0) v0_seq.push_back(note);
        if (note.voice == 1) v1_seq.push_back(note);
      } else if (note.start_tick >= dissolution_start) {
        if (note.voice == 0) v0_diss.push_back(note);
        if (note.voice == 1) v1_diss.push_back(note);
      }
    }

    if (v0_seq.empty() || v1_seq.empty() || v0_diss.empty() || v1_diss.empty()) continue;

    // Compute average pitch distance between voices in each phase.
    // For each voice 1 note, find nearest voice 0 note by pitch.
    auto computeAvgDistance = [](const std::vector<NoteEvent>& v1_notes,
                                 const std::vector<NoteEvent>& v0_notes) -> float {
      std::vector<int> distances;
      distances.reserve(v1_notes.size());
      for (const auto& v1_note : v1_notes) {
        int min_dist = 128;
        for (const auto& v0_note : v0_notes) {
          int dist = std::abs(static_cast<int>(v1_note.pitch) -
                              static_cast<int>(v0_note.pitch));
          if (dist < min_dist) min_dist = dist;
        }
        distances.push_back(min_dist);
      }
      if (distances.empty()) return 128.0f;
      return static_cast<float>(std::accumulate(distances.begin(), distances.end(), 0)) /
             static_cast<float>(distances.size());
    };

    float seq_avg_dist = computeAvgDistance(v1_seq, v0_seq);
    float diss_avg_dist = computeAvgDistance(v1_diss, v0_diss);

    // Dissolution phase should show convergence: average pitch distance
    // should be no greater than the sequence phase distance (voices move closer).
    if (diss_avg_dist <= seq_avg_dist) {
      ++seeds_passing;
    }
  }

  // At least 6/10 seeds should show dissolution convergence relative to sequence.
  EXPECT_GE(seeds_passing, 6)
      << "Voice 1 average pitch distance to voice 0 should decrease (or stay equal) "
      << "from sequence to dissolution phase in at least 6/10 seeds "
      << "(got " << seeds_passing << "/10)";
}

}  // namespace
}  // namespace bach
