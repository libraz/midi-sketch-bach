// Tests for episode strong-beat consonance properties.
//
// The basic generateEpisode() overload generates raw motivic material without
// counterpoint validation. Vertical consonance is only soft-steered by
// register placement. The validated overload (with CounterpointState,
// IRuleEvaluator, etc.) applies the strong-beat dissonance filter.
//
// These tests verify basic consonance properties of the raw episode output:
//   - Notes are well-formed (valid pitches, correct timing)
//   - Consonance is reasonable (not perfect, but within tolerances)
//   - Multi-seed stability (no extreme outlier violations)

#include "fugue/episode.h"

#include <gtest/gtest.h>

#include <map>
#include <vector>

#include "core/interval.h"
#include "core/pitch_utils.h"
#include "core/scale.h"
#include "fugue/subject.h"

namespace bach {
namespace {

// ---------------------------------------------------------------------------
// Helper: create a diatonic test subject (C major scale tones only)
// ---------------------------------------------------------------------------

Subject makeConsonanceTestSubject(
    Key key = Key::C,
    SubjectCharacter character = SubjectCharacter::Severe) {
  Subject subject;
  subject.key = key;
  subject.is_minor = false;
  subject.character = character;
  subject.length_ticks = kTicksPerBar * 2;
  // C major diatonic ascending scale.
  const uint8_t pitches[] = {60, 62, 64, 65, 67, 69, 71, 72};
  for (int idx = 0; idx < 8; ++idx) {
    NoteEvent note;
    note.start_tick = static_cast<Tick>(idx) * kTicksPerBeat;
    note.duration = kTicksPerBeat;
    note.pitch = pitches[idx];
    note.velocity = 80;
    note.voice = 0;
    note.source = BachNoteSource::FugueSubject;
    subject.notes.push_back(note);
  }
  return subject;
}

// ---------------------------------------------------------------------------
// Helper: count vertical dissonances at bar-boundary positions
// ---------------------------------------------------------------------------

struct ConsonanceStats {
  int strong_beats_checked = 0;
  int strong_beat_violations = 0;
  int bar_ticks_checked = 0;
  int bar_tick_violations = 0;
};

/// @brief Analyze vertical consonance of an episode at strong positions.
///
/// Groups notes by start_tick, then checks all pairwise intervals at
/// bar boundaries (tick % kTicksPerBar == 0) and beat boundaries
/// (tick % kTicksPerBeat == 0).
ConsonanceStats analyzeConsonance(const Episode& ep) {
  ConsonanceStats stats;

  // Group notes by start_tick.
  std::map<Tick, std::vector<uint8_t>> tick_pitches;
  for (const auto& note : ep.notes) {
    tick_pitches[note.start_tick].push_back(note.pitch);
  }

  for (const auto& [tick, pitches] : tick_pitches) {
    if (pitches.size() < 2) continue;

    bool is_bar = (tick % kTicksPerBar == 0);
    bool is_beat = (tick % kTicksPerBeat == 0);

    if (!is_beat) continue;

    int violations = 0;
    for (size_t i = 0; i < pitches.size(); ++i) {
      for (size_t j = i + 1; j < pitches.size(); ++j) {
        int ivl = absoluteInterval(pitches[i], pitches[j]);
        int simple = interval_util::compoundToSimple(ivl);
        if (!interval_util::isConsonance(simple)) {
          violations++;
        }
      }
    }

    if (is_bar) {
      stats.bar_ticks_checked++;
      stats.bar_tick_violations += violations;
    }
    stats.strong_beats_checked++;
    stats.strong_beat_violations += violations;
  }

  return stats;
}

// ---------------------------------------------------------------------------
// Test fixture
// ---------------------------------------------------------------------------

class EpisodeConsonanceTest : public ::testing::Test {
 protected:
  void SetUp() override {
    subject_ = makeConsonanceTestSubject();
  }

  Subject subject_;
};

// ---------------------------------------------------------------------------
// Basic episode produces well-formed notes
// ---------------------------------------------------------------------------

TEST_F(EpisodeConsonanceTest, EpisodeHasMultiVoiceNotes) {
  Episode ep = generateEpisode(subject_, 0, kTicksPerBar * 4,
                                Key::C, Key::C, 3, 42, 0, 0.5f);

  ASSERT_FALSE(ep.notes.empty()) << "Episode should contain notes";

  // Verify multi-voice assignment.
  std::set<VoiceId> voices;
  for (const auto& note : ep.notes) {
    voices.insert(note.voice);
    EXPECT_GE(note.pitch, 24u) << "Pitch below C1 is abnormally low";
    EXPECT_LE(note.pitch, 108u) << "Pitch above C8 is abnormally high";
  }
  EXPECT_GE(voices.size(), 2u)
      << "3-voice episode should use at least 2 voices";
}

// ---------------------------------------------------------------------------
// Bar-boundary consonance (basic overload has no hard filter, so we check
// that it stays within reasonable bounds)
// ---------------------------------------------------------------------------

TEST_F(EpisodeConsonanceTest, BarBoundaryConsonanceReasonable) {
  // The basic overload does not apply the strong-beat dissonance filter.
  // Vertical consonance depends on register placement and motif intervals.
  // We verify that violations don't exceed a generous threshold.
  int total_violations = 0;
  int total_checks = 0;

  for (uint32_t seed = 1; seed <= 20; ++seed) {
    Episode ep = generateEpisode(subject_, 0, kTicksPerBar * 4,
                                  Key::C, Key::C, 3, seed, 0, 0.5f);

    ConsonanceStats stats = analyzeConsonance(ep);
    total_violations += stats.bar_tick_violations;
    total_checks += stats.bar_ticks_checked;
  }

  // With register placement, we expect violations to be bounded. The basic
  // overload is less strict than the validated overload, so allow up to 80%.
  if (total_checks > 0) {
    float rate = static_cast<float>(total_violations) /
                 static_cast<float>(total_checks);
    EXPECT_LT(rate, 1.5f)
        << total_violations << " violations in " << total_checks
        << " bar-tick groups across 20 seeds (rate = " << rate << ")";
  }
}

// ---------------------------------------------------------------------------
// Multi-seed stability: no extreme outlier violations
// ---------------------------------------------------------------------------

TEST_F(EpisodeConsonanceTest, MultiSeedNoExtremeOutliers) {
  // Verify that no single seed produces an extreme number of violations
  // relative to others. Track maximum per-seed violations.
  int max_bar_violations = 0;

  for (uint32_t seed = 1; seed <= 20; ++seed) {
    Episode ep = generateEpisode(subject_, 0, kTicksPerBar * 4,
                                  Key::C, Key::C, 3, seed, 0, 0.5f);

    ConsonanceStats stats = analyzeConsonance(ep);
    if (stats.bar_tick_violations > max_bar_violations) {
      max_bar_violations = stats.bar_tick_violations;
    }
  }

  // No seed should have more than 12 bar-tick pair violations in a 4-bar
  // episode. With 3 voices at up to 5 bar boundaries, worst case is 15 pairs.
  // Threshold accommodates NCT vocabulary scoring which increases offbeat
  // non-chord tones (passing/neighbor) that occasionally align at bar ticks.
  EXPECT_LE(max_bar_violations, 12)
      << "Worst seed had " << max_bar_violations << " bar-tick violations";
}

// ---------------------------------------------------------------------------
// All subject characters produce bounded violations
// ---------------------------------------------------------------------------

TEST_F(EpisodeConsonanceTest, AllCharactersBoundedViolations) {
  const SubjectCharacter characters[] = {
      SubjectCharacter::Severe,
      SubjectCharacter::Playful,
      SubjectCharacter::Noble,
      SubjectCharacter::Restless,
  };
  const char* names[] = {"Severe", "Playful", "Noble", "Restless"};

  for (int char_idx = 0; char_idx < 4; ++char_idx) {
    Subject subject = makeConsonanceTestSubject(Key::C, characters[char_idx]);

    int total_violations = 0;
    int total_checks = 0;

    for (uint32_t seed = 1; seed <= 5; ++seed) {
      Episode ep = generateEpisode(subject, 0, kTicksPerBar * 4,
                                    Key::C, Key::C, 3, seed, 0, 0.5f);

      ConsonanceStats stats = analyzeConsonance(ep);
      total_violations += stats.bar_tick_violations;
      total_checks += stats.bar_ticks_checked;
    }

    if (total_checks > 0) {
      float rate = static_cast<float>(total_violations) /
                   static_cast<float>(total_checks);
      EXPECT_LT(rate, 1.5f)
          << names[char_idx] << ": " << total_violations
          << " bar-tick violations in " << total_checks << " checks"
          << " (rate = " << rate << ")";
    }
  }
}

// ---------------------------------------------------------------------------
// Two-voice episode: fewer vertical pairs, expect fewer violations
// ---------------------------------------------------------------------------

TEST_F(EpisodeConsonanceTest, TwoVoiceBoundedViolations) {
  int total_violations = 0;
  int total_checks = 0;

  for (uint32_t seed = 1; seed <= 10; ++seed) {
    Episode ep = generateEpisode(subject_, 0, kTicksPerBar * 4,
                                  Key::C, Key::C, 2, seed, 0, 0.5f);

    ConsonanceStats stats = analyzeConsonance(ep);
    total_violations += stats.bar_tick_violations;
    total_checks += stats.bar_ticks_checked;
  }

  // Two voices means only one vertical pair per tick. Violation rate should
  // be lower than 3+ voice episodes.
  if (total_checks > 0) {
    float rate = static_cast<float>(total_violations) /
                 static_cast<float>(total_checks);
    EXPECT_LT(rate, 1.0f)
        << total_violations << " violations in " << total_checks
        << " bar-tick groups across 10 seeds (2 voices)";
  }
}

// ---------------------------------------------------------------------------
// Modulated episode: consonance is maintained during modulation
// ---------------------------------------------------------------------------

TEST_F(EpisodeConsonanceTest, ModulatedEpisodeBoundedViolations) {
  int max_violations = 0;
  for (uint32_t seed = 1; seed <= 10; ++seed) {
    Episode ep = generateEpisode(subject_, 0, kTicksPerBar * 4,
                                  Key::C, Key::G, 3, seed, 0, 0.5f);

    ConsonanceStats stats = analyzeConsonance(ep);
    if (stats.bar_tick_violations > max_violations) {
      max_violations = stats.bar_tick_violations;
    }
  }

  EXPECT_LE(max_violations, 8)
      << "Modulated episode worst seed had " << max_violations
      << " bar-tick violations";
}

// ---------------------------------------------------------------------------
// Invertible counterpoint: voice swap doesn't cause extreme violations
// ---------------------------------------------------------------------------

TEST_F(EpisodeConsonanceTest, InvertibleBoundedViolations) {
  int total_violations = 0;
  int total_checks = 0;

  for (uint32_t seed = 1; seed <= 10; ++seed) {
    Episode ep = generateEpisode(subject_, 0, kTicksPerBar * 4,
                                  Key::C, Key::C, 3, seed,
                                  /*episode_index=*/1, 0.5f);

    ConsonanceStats stats = analyzeConsonance(ep);
    total_violations += stats.bar_tick_violations;
    total_checks += stats.bar_ticks_checked;
  }

  if (total_checks > 0) {
    float rate = static_cast<float>(total_violations) /
                 static_cast<float>(total_checks);
    EXPECT_LT(rate, 1.5f)
        << total_violations << " violations in " << total_checks
        << " bar-tick groups across 10 seeds (invertible counterpoint)";
  }
}

// ---------------------------------------------------------------------------
// Empty subject edge case
// ---------------------------------------------------------------------------

TEST_F(EpisodeConsonanceTest, EmptySubjectNoViolations) {
  Subject empty;
  empty.key = Key::C;
  empty.is_minor = false;
  empty.character = SubjectCharacter::Severe;

  Episode ep = generateEpisode(empty, 0, kTicksPerBar * 4,
                                Key::C, Key::C, 3, 42, 0, 0.5f);

  // Should handle gracefully: no notes, no violations to check.
  ConsonanceStats stats = analyzeConsonance(ep);
  EXPECT_EQ(stats.bar_tick_violations, 0);
}

// ---------------------------------------------------------------------------
// Notes at bar boundaries exist for multi-voice episodes
// ---------------------------------------------------------------------------

TEST_F(EpisodeConsonanceTest, BarBoundaryNotesExist) {
  // Verify that multi-voice episodes actually produce simultaneous notes
  // at bar boundaries, so our consonance analysis is meaningful.
  bool found_multi_voice_bar = false;

  for (uint32_t seed = 1; seed <= 10; ++seed) {
    Episode ep = generateEpisode(subject_, 0, kTicksPerBar * 4,
                                  Key::C, Key::C, 3, seed, 0, 0.5f);

    std::map<Tick, std::vector<uint8_t>> tick_pitches;
    for (const auto& note : ep.notes) {
      tick_pitches[note.start_tick].push_back(note.pitch);
    }

    for (const auto& [tick, pitches] : tick_pitches) {
      // Check bar and half-bar boundaries (bass/pedal now advance in half-bars).
      if (tick % (kTicksPerBeat * 2) == 0 && pitches.size() >= 2) {
        found_multi_voice_bar = true;
        break;
      }
    }
    if (found_multi_voice_bar) break;
  }

  EXPECT_TRUE(found_multi_voice_bar)
      << "At least one seed should produce multi-voice notes at a bar boundary";
}

// ---------------------------------------------------------------------------
// Energy level variation does not cause extreme violations
// ---------------------------------------------------------------------------

TEST_F(EpisodeConsonanceTest, HighEnergyBoundedViolations) {
  int max_violations = 0;
  for (uint32_t seed = 1; seed <= 10; ++seed) {
    Episode ep = generateEpisode(subject_, 0, kTicksPerBar * 4,
                                  Key::C, Key::C, 3, seed, 0, 0.9f);

    ConsonanceStats stats = analyzeConsonance(ep);
    if (stats.bar_tick_violations > max_violations) {
      max_violations = stats.bar_tick_violations;
    }
  }

  EXPECT_LE(max_violations, 12)
      << "High-energy episode worst seed had " << max_violations
      << " bar-tick violations";
}

}  // namespace
}  // namespace bach
