// Tests for episode melodic contour deviation detection.
// Verifies: direction changes in the melodic line are within [3, 15]
// per 8 bars (not too smooth, not too jagged).

#include "fugue/episode.h"

#include <algorithm>
#include <cstdlib>
#include <vector>

#include <gtest/gtest.h>

#include "core/basic_types.h"
#include "core/scale.h"
#include "fugue/fugue_config.h"

namespace bach {
namespace {

// ---------------------------------------------------------------------------
// Helper: create a diatonic test subject
// ---------------------------------------------------------------------------

Subject makeContourTestSubject(Key key = Key::C,
                               SubjectCharacter character = SubjectCharacter::Severe) {
  Subject subject;
  subject.key = key;
  subject.character = character;
  subject.length_ticks = kTicksPerBar * 2;
  // C major diatonic: C4, D4, E4, F4, G4, A4, B4, C5
  const uint8_t pitches[] = {60, 62, 64, 65, 67, 69, 71, 72};
  for (int idx = 0; idx < 8; ++idx) {
    NoteEvent note;
    note.start_tick = static_cast<Tick>(idx) * kTicksPerBeat;
    note.duration = kTicksPerBeat;
    note.pitch = pitches[idx];
    note.velocity = 80;
    note.voice = 0;
    subject.notes.push_back(note);
  }
  return subject;
}

/// @brief Count direction changes in a melodic line for a specific voice.
/// @param notes All notes in the episode.
/// @param voice_id The voice to analyze.
/// @return Number of direction changes (ascending -> descending or vice versa).
int countDirectionChanges(const std::vector<NoteEvent>& notes, VoiceId voice_id) {
  // Extract pitches for the specified voice, sorted by start_tick.
  std::vector<uint8_t> pitches;
  for (const auto& note : notes) {
    if (note.voice == voice_id) {
      pitches.push_back(note.pitch);
    }
  }

  if (pitches.size() < 3) return 0;

  int changes = 0;
  int prev_direction = 0;  // -1 = descending, 0 = same, +1 = ascending

  for (size_t i = 1; i < pitches.size(); ++i) {
    int diff = static_cast<int>(pitches[i]) - static_cast<int>(pitches[i - 1]);
    int direction = (diff > 0) ? 1 : (diff < 0) ? -1 : 0;

    if (direction != 0 && prev_direction != 0 && direction != prev_direction) {
      ++changes;
    }
    if (direction != 0) {
      prev_direction = direction;
    }
  }

  return changes;
}

// ---------------------------------------------------------------------------
// Episode melodic contour tests
// ---------------------------------------------------------------------------

TEST(EpisodeMelodicContourTest, DirectionChangesInRange_Severe) {
  Subject subject = makeContourTestSubject(Key::C, SubjectCharacter::Severe);

  for (uint32_t seed = 1; seed <= 10; ++seed) {
    // Generate a 4-bar episode (half of our 8-bar reference range).
    Episode episode = generateEpisode(subject, 0, kTicksPerBar * 8,
                                      Key::C, Key::G, 3, seed);
    ASSERT_FALSE(episode.notes.empty())
        << "Seed " << seed << ": episode has no notes";

    int changes = countDirectionChanges(episode.notes, 0);

    // Per 8 bars: direction changes should be in [3, 15].
    // This range allows for melodic variety without excessive zig-zagging.
    EXPECT_GE(changes, 3)
        << "Seed " << seed << ": only " << changes
        << " direction changes in voice 0 (too smooth, expected >= 3)";
    EXPECT_LE(changes, 15)
        << "Seed " << seed << ": " << changes
        << " direction changes in voice 0 (too jagged, expected <= 15)";
  }
}

TEST(EpisodeMelodicContourTest, DirectionChangesInRange_Playful) {
  Subject subject = makeContourTestSubject(Key::C, SubjectCharacter::Playful);

  for (uint32_t seed = 1; seed <= 10; ++seed) {
    Episode episode = generateEpisode(subject, 0, kTicksPerBar * 8,
                                      Key::C, Key::G, 3, seed);
    ASSERT_FALSE(episode.notes.empty())
        << "Seed " << seed << ": episode has no notes";

    int changes = countDirectionChanges(episode.notes, 0);

    EXPECT_GE(changes, 3)
        << "Playful seed " << seed << ": " << changes
        << " direction changes (too smooth)";
    EXPECT_LE(changes, 15)
        << "Playful seed " << seed << ": " << changes
        << " direction changes (too jagged)";
  }
}

TEST(EpisodeMelodicContourTest, DirectionChangesInRange_AllCharacters) {
  const SubjectCharacter characters[] = {
      SubjectCharacter::Severe,
      SubjectCharacter::Playful,
      SubjectCharacter::Noble,
      SubjectCharacter::Restless,
  };
  const char* names[] = {"Severe", "Playful", "Noble", "Restless"};

  for (int char_idx = 0; char_idx < 4; ++char_idx) {
    Subject subject = makeContourTestSubject(Key::C, characters[char_idx]);

    // Test 3 seeds per character.
    for (uint32_t seed : {1u, 42u, 99u}) {
      Episode episode = generateEpisode(subject, 0, kTicksPerBar * 8,
                                        Key::C, Key::G, 3, seed);
      if (episode.notes.empty()) continue;

      int changes = countDirectionChanges(episode.notes, 0);

      // Wider range for all characters: [2, 20] to accommodate character
      // diversity while still catching degenerate output.
      EXPECT_GE(changes, 2)
          << names[char_idx] << " seed " << seed << ": " << changes
          << " direction changes (too smooth)";
      EXPECT_LE(changes, 20)
          << names[char_idx] << " seed " << seed << ": " << changes
          << " direction changes (too jagged)";
    }
  }
}

// ---------------------------------------------------------------------------
// Voice 1 also has contour variation
// ---------------------------------------------------------------------------

TEST(EpisodeMelodicContourTest, Voice1_HasContourVariation) {
  Subject subject = makeContourTestSubject(Key::C, SubjectCharacter::Severe);
  Episode episode = generateEpisode(subject, 0, kTicksPerBar * 8,
                                    Key::C, Key::G, 3, 42);

  // Voice 1 should also have at least some direction changes.
  int changes_v1 = countDirectionChanges(episode.notes, 1);

  EXPECT_GE(changes_v1, 1)
      << "Voice 1 should have at least 1 direction change for melodic interest";
}

}  // namespace
}  // namespace bach
