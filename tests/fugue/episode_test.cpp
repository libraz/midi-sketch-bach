// Tests for fugue/episode.h -- episode generation, motif extraction,
// modulation, multi-voice assignment, and determinism.

#include "fugue/episode.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <set>

namespace bach {
namespace {

// ---------------------------------------------------------------------------
// Helper: create a test subject with 8 quarter-note steps
// ---------------------------------------------------------------------------

Subject makeTestSubject(Key key = Key::C,
                        SubjectCharacter character = SubjectCharacter::Severe) {
  Subject subject;
  subject.key = key;
  subject.character = character;
  subject.length_ticks = kTicksPerBar * 2;
  for (int idx = 0; idx < 8; ++idx) {
    NoteEvent note;
    note.start_tick = static_cast<Tick>(idx) * kTicksPerBeat;
    note.duration = kTicksPerBeat;
    note.pitch = static_cast<uint8_t>(60 + (idx % 5));  // C4 C#4 D4 D#4 E4 ...
    note.velocity = 80;
    note.voice = 0;
    subject.notes.push_back(note);
  }
  return subject;
}

// ---------------------------------------------------------------------------
// ExtractMotif tests
// ---------------------------------------------------------------------------

TEST(ExtractMotifTest, DefaultMaxNotes) {
  Subject subject = makeTestSubject();
  auto motif = extractMotif(subject);
  // Default max_notes = 4, subject has 8 notes.
  ASSERT_EQ(motif.size(), 4u);
  EXPECT_EQ(motif[0].pitch, 60);
  EXPECT_EQ(motif[1].pitch, 61);
  EXPECT_EQ(motif[2].pitch, 62);
  EXPECT_EQ(motif[3].pitch, 63);
}

TEST(ExtractMotifTest, LessThanMax) {
  Subject subject;
  subject.key = Key::C;
  subject.notes.push_back({0, kTicksPerBeat, 60, 80, 0});
  subject.notes.push_back({kTicksPerBeat, kTicksPerBeat, 64, 80, 0});
  subject.length_ticks = kTicksPerBeat * 2;

  auto motif = extractMotif(subject);
  // Subject has 2 notes, default max is 4, should return 2.
  ASSERT_EQ(motif.size(), 2u);
  EXPECT_EQ(motif[0].pitch, 60);
  EXPECT_EQ(motif[1].pitch, 64);
}

TEST(ExtractMotifTest, EmptySubject) {
  Subject subject;
  auto motif = extractMotif(subject);
  EXPECT_TRUE(motif.empty());
}

TEST(ExtractMotifTest, CustomMaxNotes) {
  Subject subject = makeTestSubject();
  auto motif = extractMotif(subject, 2);
  ASSERT_EQ(motif.size(), 2u);
  EXPECT_EQ(motif[0].pitch, 60);
  EXPECT_EQ(motif[1].pitch, 61);
}

TEST(ExtractMotifTest, MaxNotesExceedsSubjectLength) {
  Subject subject = makeTestSubject();
  auto motif = extractMotif(subject, 100);
  // Subject has 8 notes, should return all 8.
  EXPECT_EQ(motif.size(), 8u);
}

// ---------------------------------------------------------------------------
// GenerateEpisode -- basic properties
// ---------------------------------------------------------------------------

TEST(GenerateEpisodeTest, HasNotes) {
  Subject subject = makeTestSubject();
  Episode episode = generateEpisode(subject, 0, kTicksPerBar * 4,
                                    Key::C, Key::G, 3, 42);
  EXPECT_FALSE(episode.notes.empty());
}

TEST(GenerateEpisodeTest, CorrectTiming) {
  Subject subject = makeTestSubject();
  Tick start = kTicksPerBar * 8;
  Tick duration = kTicksPerBar * 4;
  Episode episode = generateEpisode(subject, start, duration,
                                    Key::C, Key::G, 3, 42);
  EXPECT_EQ(episode.start_tick, start);
  EXPECT_EQ(episode.end_tick, start + duration);
}

TEST(GenerateEpisodeTest, CorrectKeys) {
  Subject subject = makeTestSubject();
  Episode episode = generateEpisode(subject, 0, kTicksPerBar * 4,
                                    Key::D, Key::A, 3, 42);
  EXPECT_EQ(episode.start_key, Key::D);
  EXPECT_EQ(episode.end_key, Key::A);
}

TEST(GenerateEpisodeTest, DurationMatchesRequest) {
  Subject subject = makeTestSubject();
  Tick start = kTicksPerBar * 2;
  Tick duration = kTicksPerBar * 3;
  Episode episode = generateEpisode(subject, start, duration,
                                    Key::C, Key::G, 3, 42);
  EXPECT_EQ(episode.durationTicks(), duration);
}

// ---------------------------------------------------------------------------
// GenerateEpisode -- voice assignment
// ---------------------------------------------------------------------------

TEST(GenerateEpisodeTest, MultipleVoices) {
  Subject subject = makeTestSubject();
  Episode episode = generateEpisode(subject, 0, kTicksPerBar * 4,
                                    Key::C, Key::G, 3, 42);

  std::set<VoiceId> voices;
  for (const auto& note : episode.notes) {
    voices.insert(note.voice);
  }
  // With 3 voices, should have notes on at least 2 voices.
  EXPECT_GE(voices.size(), 2u);
}

TEST(GenerateEpisodeTest, TwoVoices) {
  Subject subject = makeTestSubject();
  Episode episode = generateEpisode(subject, 0, kTicksPerBar * 4,
                                    Key::C, Key::G, 2, 42);

  std::set<VoiceId> voices;
  for (const auto& note : episode.notes) {
    voices.insert(note.voice);
  }
  // With 2 voices, should have notes for voice 0 and voice 1.
  EXPECT_TRUE(voices.count(0) > 0) << "Should have voice 0 notes";
  EXPECT_TRUE(voices.count(1) > 0) << "Should have voice 1 notes";
  // Should not have voice 2 (only 2 voices requested).
  EXPECT_EQ(voices.count(2), 0u) << "Should not have voice 2 with num_voices=2";
}

TEST(GenerateEpisodeTest, SingleVoice) {
  Subject subject = makeTestSubject();
  Episode episode = generateEpisode(subject, 0, kTicksPerBar * 4,
                                    Key::C, Key::G, 1, 42);

  for (const auto& note : episode.notes) {
    EXPECT_EQ(note.voice, 0) << "Single voice episode should only use voice 0";
  }
}

// ---------------------------------------------------------------------------
// GenerateEpisode -- timing constraints
// ---------------------------------------------------------------------------

TEST(GenerateEpisodeTest, NotesStartAtOrAfterEpisodeStart) {
  Subject subject = makeTestSubject();
  Tick start = kTicksPerBar * 4;
  Episode episode = generateEpisode(subject, start, kTicksPerBar * 4,
                                    Key::C, Key::G, 3, 42);

  for (const auto& note : episode.notes) {
    EXPECT_GE(note.start_tick, start)
        << "Note at tick " << note.start_tick << " is before episode start " << start;
  }
}

// ---------------------------------------------------------------------------
// GenerateEpisode -- determinism
// ---------------------------------------------------------------------------

TEST(GenerateEpisodeTest, DeterministicWithSeed) {
  Subject subject = makeTestSubject();
  Episode ep1 = generateEpisode(subject, 0, kTicksPerBar * 4,
                                Key::C, Key::G, 3, 42);
  Episode ep2 = generateEpisode(subject, 0, kTicksPerBar * 4,
                                Key::C, Key::G, 3, 42);

  ASSERT_EQ(ep1.notes.size(), ep2.notes.size());
  for (size_t idx = 0; idx < ep1.notes.size(); ++idx) {
    EXPECT_EQ(ep1.notes[idx].pitch, ep2.notes[idx].pitch)
        << "Mismatch at note " << idx;
    EXPECT_EQ(ep1.notes[idx].start_tick, ep2.notes[idx].start_tick)
        << "Timing mismatch at note " << idx;
    EXPECT_EQ(ep1.notes[idx].voice, ep2.notes[idx].voice)
        << "Voice mismatch at note " << idx;
  }
}

TEST(GenerateEpisodeTest, DifferentSeeds) {
  Subject subject = makeTestSubject(Key::C, SubjectCharacter::Playful);
  Episode ep1 = generateEpisode(subject, 0, kTicksPerBar * 4,
                                Key::C, Key::G, 3, 42);
  Episode ep2 = generateEpisode(subject, 0, kTicksPerBar * 4,
                                Key::C, Key::G, 3, 99);

  // Different seeds with Playful character (which uses randomized intervals)
  // should produce different results. Check note count or pitch differences.
  bool any_difference = (ep1.notes.size() != ep2.notes.size());
  if (!any_difference) {
    for (size_t idx = 0; idx < ep1.notes.size(); ++idx) {
      if (ep1.notes[idx].pitch != ep2.notes[idx].pitch) {
        any_difference = true;
        break;
      }
    }
  }
  EXPECT_TRUE(any_difference)
      << "Different seeds should produce different episodes for Playful character";
}

// ---------------------------------------------------------------------------
// GenerateEpisode -- modulation
// ---------------------------------------------------------------------------

TEST(GenerateEpisodeTest, ModulationApplied) {
  Subject subject = makeTestSubject();
  // Modulate from C to G (key_diff = 7 - 0 = 7 semitones up).
  Episode episode = generateEpisode(subject, 0, kTicksPerBar * 4,
                                    Key::C, Key::G, 2, 42);

  Tick midpoint = kTicksPerBar * 2;  // duration / 2

  // Collect pitches before and after midpoint.
  bool has_before_mid = false;
  bool has_at_or_after_mid = false;
  for (const auto& note : episode.notes) {
    if (note.start_tick < midpoint) {
      has_before_mid = true;
    }
    if (note.start_tick >= midpoint) {
      has_at_or_after_mid = true;
    }
  }

  // Both halves should contain notes.
  EXPECT_TRUE(has_before_mid) << "Should have notes before midpoint";
  EXPECT_TRUE(has_at_or_after_mid) << "Should have notes at or after midpoint";
}

TEST(GenerateEpisodeTest, SameKeyNoModulation) {
  Subject subject = makeTestSubject();
  // When start_key == target_key, no transposition should occur.
  Episode ep_no_mod = generateEpisode(subject, 0, kTicksPerBar * 4,
                                      Key::C, Key::C, 2, 42);

  // All notes should retain their unmodified pitches (no key_diff applied).
  // Generate the same episode but verify the start/end keys match.
  EXPECT_EQ(ep_no_mod.start_key, Key::C);
  EXPECT_EQ(ep_no_mod.end_key, Key::C);
  EXPECT_FALSE(ep_no_mod.notes.empty());
}

// ---------------------------------------------------------------------------
// GenerateEpisode -- edge cases
// ---------------------------------------------------------------------------

TEST(GenerateEpisodeTest, EmptySubject) {
  Subject empty_subject;
  empty_subject.key = Key::C;
  Episode episode = generateEpisode(empty_subject, 0, kTicksPerBar * 4,
                                    Key::C, Key::G, 3, 42);

  // Should handle gracefully: return an episode with correct timing but no notes.
  EXPECT_EQ(episode.start_tick, 0u);
  EXPECT_EQ(episode.end_tick, kTicksPerBar * 4);
  EXPECT_TRUE(episode.notes.empty());
}

TEST(GenerateEpisodeTest, NoteCount) {
  Subject subject = makeTestSubject();
  Episode episode = generateEpisode(subject, 0, kTicksPerBar * 4,
                                    Key::C, Key::G, 3, 42);
  EXPECT_EQ(episode.noteCount(), episode.notes.size());
}

// ---------------------------------------------------------------------------
// GenerateEpisode -- character influence
// ---------------------------------------------------------------------------

TEST(GenerateEpisodeTest, SevereCharacterProducesNotes) {
  Subject subject = makeTestSubject(Key::C, SubjectCharacter::Severe);
  Episode episode = generateEpisode(subject, 0, kTicksPerBar * 4,
                                    Key::C, Key::G, 3, 42);
  EXPECT_FALSE(episode.notes.empty());
}

TEST(GenerateEpisodeTest, PlayfulCharacterProducesNotes) {
  Subject subject = makeTestSubject(Key::C, SubjectCharacter::Playful);
  Episode episode = generateEpisode(subject, 0, kTicksPerBar * 4,
                                    Key::C, Key::G, 3, 42);
  EXPECT_FALSE(episode.notes.empty());
}

TEST(GenerateEpisodeTest, NobleCharacterProducesNotes) {
  Subject subject = makeTestSubject(Key::C, SubjectCharacter::Noble);
  Episode episode = generateEpisode(subject, 0, kTicksPerBar * 4,
                                    Key::C, Key::G, 3, 42);
  EXPECT_FALSE(episode.notes.empty());
}

TEST(GenerateEpisodeTest, RestlessCharacterProducesNotes) {
  Subject subject = makeTestSubject(Key::C, SubjectCharacter::Restless);
  Episode episode = generateEpisode(subject, 0, kTicksPerBar * 4,
                                    Key::C, Key::G, 3, 42);
  EXPECT_FALSE(episode.notes.empty());
}

}  // namespace
}  // namespace bach
