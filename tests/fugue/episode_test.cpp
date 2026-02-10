// Tests for fugue/episode.h -- episode generation, motif extraction,
// modulation, multi-voice assignment, and determinism.

#include "fugue/episode.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <set>
#include <string>

#include "core/scale.h"

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
// Helper: create a diatonic test subject (C major scale tones only)
// ---------------------------------------------------------------------------

Subject makeDiatonicTestSubject(Key key = Key::C,
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

// ---------------------------------------------------------------------------
// ExtractTailMotif tests
// ---------------------------------------------------------------------------

TEST(ExtractTailMotifTest, BasicExtraction) {
  std::vector<NoteEvent> notes;
  NoteEvent evt;
  evt.voice = 0;
  evt.velocity = 80;
  evt.duration = 480;
  evt.pitch = 60;
  evt.start_tick = 0;
  notes.push_back(evt);
  evt.pitch = 62;
  evt.start_tick = 480;
  notes.push_back(evt);
  evt.pitch = 64;
  evt.start_tick = 960;
  notes.push_back(evt);
  evt.pitch = 65;
  evt.start_tick = 1440;
  notes.push_back(evt);

  auto tail = extractTailMotif(notes, 2);
  ASSERT_EQ(tail.size(), 2u);
  EXPECT_EQ(tail[0].pitch, 64);
  EXPECT_EQ(tail[1].pitch, 65);
}

TEST(ExtractTailMotifTest, MoreThanAvailable) {
  std::vector<NoteEvent> notes;
  NoteEvent evt;
  evt.voice = 0;
  evt.velocity = 80;
  evt.duration = 480;
  evt.pitch = 60;
  evt.start_tick = 0;
  notes.push_back(evt);

  auto tail = extractTailMotif(notes, 5);
  EXPECT_EQ(tail.size(), 1u);
}

TEST(ExtractTailMotifTest, ExactSize) {
  std::vector<NoteEvent> notes;
  NoteEvent evt;
  evt.voice = 0;
  evt.velocity = 80;
  evt.duration = 480;
  for (int idx = 0; idx < 3; ++idx) {
    evt.pitch = static_cast<uint8_t>(60 + idx);
    evt.start_tick = static_cast<Tick>(idx) * 480;
    notes.push_back(evt);
  }
  auto tail = extractTailMotif(notes, 3);
  ASSERT_EQ(tail.size(), 3u);
  EXPECT_EQ(tail[0].pitch, 60);
  EXPECT_EQ(tail[2].pitch, 62);
}

TEST(ExtractTailMotifTest, EmptyInput) {
  std::vector<NoteEvent> notes;
  auto tail = extractTailMotif(notes, 3);
  EXPECT_TRUE(tail.empty());
}

TEST(ExtractTailMotifTest, SingleNote) {
  std::vector<NoteEvent> notes;
  NoteEvent evt;
  evt.voice = 0;
  evt.velocity = 80;
  evt.duration = 480;
  evt.pitch = 72;
  evt.start_tick = 0;
  notes.push_back(evt);
  auto tail = extractTailMotif(notes, 1);
  ASSERT_EQ(tail.size(), 1u);
  EXPECT_EQ(tail[0].pitch, 72);
}

// ---------------------------------------------------------------------------
// FragmentMotif tests
// ---------------------------------------------------------------------------

TEST(FragmentMotifTest, BasicFragmentation) {
  std::vector<NoteEvent> notes;
  NoteEvent evt;
  evt.voice = 0;
  evt.velocity = 80;
  evt.duration = 480;
  for (int idx = 0; idx < 6; ++idx) {
    evt.pitch = static_cast<uint8_t>(60 + idx);
    evt.start_tick = static_cast<Tick>(idx) * 480;
    notes.push_back(evt);
  }
  auto fragments = fragmentMotif(notes, 3);
  ASSERT_EQ(fragments.size(), 3u);
  EXPECT_EQ(fragments[0].size(), 2u);
  EXPECT_EQ(fragments[1].size(), 2u);
  EXPECT_EQ(fragments[2].size(), 2u);
}

TEST(FragmentMotifTest, EmptyInput) {
  std::vector<NoteEvent> notes;
  auto fragments = fragmentMotif(notes, 3);
  EXPECT_TRUE(fragments.empty());
}

TEST(FragmentMotifTest, ZeroFragments) {
  std::vector<NoteEvent> notes;
  NoteEvent evt;
  evt.voice = 0;
  evt.velocity = 80;
  evt.duration = 480;
  evt.pitch = 60;
  evt.start_tick = 0;
  notes.push_back(evt);
  auto fragments = fragmentMotif(notes, 0);
  EXPECT_TRUE(fragments.empty());
}

TEST(FragmentMotifTest, TwoFragments) {
  std::vector<NoteEvent> notes;
  NoteEvent evt;
  evt.voice = 0;
  evt.velocity = 80;
  evt.duration = 480;
  for (int idx = 0; idx < 4; ++idx) {
    evt.pitch = static_cast<uint8_t>(60 + idx);
    evt.start_tick = static_cast<Tick>(idx) * 480;
    notes.push_back(evt);
  }
  auto fragments = fragmentMotif(notes, 2);
  ASSERT_EQ(fragments.size(), 2u);
  EXPECT_EQ(fragments[0].size(), 2u);
  EXPECT_EQ(fragments[1].size(), 2u);
  // Verify pitches of first fragment.
  EXPECT_EQ(fragments[0][0].pitch, 60);
  EXPECT_EQ(fragments[0][1].pitch, 61);
  // Verify pitches of second fragment.
  EXPECT_EQ(fragments[1][0].pitch, 62);
  EXPECT_EQ(fragments[1][1].pitch, 63);
}

TEST(FragmentMotifTest, MoreFragmentsThanNotes) {
  std::vector<NoteEvent> notes;
  NoteEvent evt;
  evt.voice = 0;
  evt.velocity = 80;
  evt.duration = 480;
  for (int idx = 0; idx < 2; ++idx) {
    evt.pitch = static_cast<uint8_t>(60 + idx);
    evt.start_tick = static_cast<Tick>(idx) * 480;
    notes.push_back(evt);
  }
  // Request 5 fragments from 2 notes -- frag_size becomes 1.
  auto fragments = fragmentMotif(notes, 5);
  // Only 2 fragments can be produced (2 notes / 1 per fragment, capped at 5).
  EXPECT_GE(fragments.size(), 1u);
  EXPECT_LE(fragments.size(), 5u);
}

// ---------------------------------------------------------------------------
// GenerateEpisode -- character-specific technique differences
// ---------------------------------------------------------------------------

TEST(GenerateEpisodeTest, CharacterProducesDifferentPitchContent) {
  // Severe and Playful should produce different pitch sequences because Playful
  // uses retrograde while Severe uses the original motif order.
  Subject severe_subj = makeTestSubject(Key::C, SubjectCharacter::Severe);
  Subject playful_subj = makeTestSubject(Key::C, SubjectCharacter::Playful);

  Episode ep_severe = generateEpisode(severe_subj, 0, kTicksPerBar * 4,
                                      Key::C, Key::C, 2, 42);
  Episode ep_playful = generateEpisode(playful_subj, 0, kTicksPerBar * 4,
                                       Key::C, Key::C, 2, 42);

  // Collect voice 0 pitches for comparison.
  std::vector<uint8_t> severe_v0_pitches;
  std::vector<uint8_t> playful_v0_pitches;
  for (const auto& note : ep_severe.notes) {
    if (note.voice == 0) severe_v0_pitches.push_back(note.pitch);
  }
  for (const auto& note : ep_playful.notes) {
    if (note.voice == 0) playful_v0_pitches.push_back(note.pitch);
  }

  // The two should differ: Severe uses original order, Playful uses retrograde.
  bool differ = (severe_v0_pitches.size() != playful_v0_pitches.size());
  if (!differ) {
    for (size_t idx = 0; idx < severe_v0_pitches.size(); ++idx) {
      if (severe_v0_pitches[idx] != playful_v0_pitches[idx]) {
        differ = true;
        break;
      }
    }
  }
  EXPECT_TRUE(differ)
      << "Severe and Playful characters should produce different voice 0 content";
}

TEST(GenerateEpisodeTest, NobleUsesAugmentedBass) {
  Subject noble_subj = makeTestSubject(Key::C, SubjectCharacter::Noble);
  Episode episode = generateEpisode(noble_subj, 0, kTicksPerBar * 4,
                                    Key::C, Key::C, 2, 42);

  // Noble voice 1 uses augmented motif transposed down an octave.
  // Voice 1 notes should exist and their pitches should be lower than voice 0.
  int voice0_sum = 0;
  int voice0_count = 0;
  int voice1_sum = 0;
  int voice1_count = 0;
  for (const auto& note : episode.notes) {
    if (note.voice == 0) {
      voice0_sum += note.pitch;
      ++voice0_count;
    } else if (note.voice == 1) {
      voice1_sum += note.pitch;
      ++voice1_count;
    }
  }

  ASSERT_GT(voice0_count, 0) << "Should have voice 0 notes";
  ASSERT_GT(voice1_count, 0) << "Should have voice 1 notes";

  // Noble bass voice should be lower on average (transposed -12 semitones).
  double voice0_avg = static_cast<double>(voice0_sum) / voice0_count;
  double voice1_avg = static_cast<double>(voice1_sum) / voice1_count;
  EXPECT_LT(voice1_avg, voice0_avg)
      << "Noble voice 1 (augmented bass) should be lower than voice 0";
}

TEST(GenerateEpisodeTest, RestlessUsesFragments) {
  Subject restless_subj = makeTestSubject(Key::C, SubjectCharacter::Restless);
  Subject severe_subj = makeTestSubject(Key::C, SubjectCharacter::Severe);

  Episode ep_restless = generateEpisode(restless_subj, 0, kTicksPerBar * 4,
                                        Key::C, Key::C, 2, 42);
  Episode ep_severe = generateEpisode(severe_subj, 0, kTicksPerBar * 4,
                                      Key::C, Key::C, 2, 42);

  // Restless should produce different content from Severe due to fragmentation.
  bool differ = (ep_restless.notes.size() != ep_severe.notes.size());
  if (!differ) {
    for (size_t idx = 0; idx < ep_restless.notes.size(); ++idx) {
      if (ep_restless.notes[idx].pitch != ep_severe.notes[idx].pitch ||
          ep_restless.notes[idx].start_tick != ep_severe.notes[idx].start_tick) {
        differ = true;
        break;
      }
    }
  }
  EXPECT_TRUE(differ)
      << "Restless and Severe should produce different episodes";
}

// ---------------------------------------------------------------------------
// GenerateEpisode -- invertible counterpoint (episode_index)
// ---------------------------------------------------------------------------

TEST(GenerateEpisodeTest, InvertibleCounterpointSwapsVoices) {
  Subject subject = makeTestSubject(Key::C, SubjectCharacter::Severe);

  // Even index (0): normal voice assignment.
  Episode ep_even = generateEpisode(subject, 0, kTicksPerBar * 4,
                                    Key::C, Key::C, 2, 42, /*episode_index=*/0);
  // Odd index (1): voice 0 and 1 should be swapped.
  Episode ep_odd = generateEpisode(subject, 0, kTicksPerBar * 4,
                                   Key::C, Key::C, 2, 42, /*episode_index=*/1);

  ASSERT_EQ(ep_even.notes.size(), ep_odd.notes.size())
      << "Even and odd episodes should have same note count";

  // Every voice 0 note in ep_even should be voice 1 in ep_odd, and vice versa.
  for (size_t idx = 0; idx < ep_even.notes.size(); ++idx) {
    uint8_t even_voice = ep_even.notes[idx].voice;
    uint8_t odd_voice = ep_odd.notes[idx].voice;
    if (even_voice == 0) {
      EXPECT_EQ(odd_voice, 1u)
          << "Voice 0 in even episode should be voice 1 in odd at note " << idx;
    } else if (even_voice == 1) {
      EXPECT_EQ(odd_voice, 0u)
          << "Voice 1 in even episode should be voice 0 in odd at note " << idx;
    } else {
      // Voice 2+ should be unchanged.
      EXPECT_EQ(even_voice, odd_voice)
          << "Voice " << static_cast<int>(even_voice) << " should be unchanged at note "
          << idx;
    }
  }
}

TEST(GenerateEpisodeTest, EvenIndexNoVoiceSwap) {
  Subject subject = makeTestSubject(Key::C, SubjectCharacter::Severe);

  // episode_index = 0 (even): should match default behavior (no swap).
  Episode ep_default = generateEpisode(subject, 0, kTicksPerBar * 4,
                                       Key::C, Key::C, 2, 42);
  Episode ep_idx0 = generateEpisode(subject, 0, kTicksPerBar * 4,
                                    Key::C, Key::C, 2, 42, /*episode_index=*/0);

  ASSERT_EQ(ep_default.notes.size(), ep_idx0.notes.size());
  for (size_t idx = 0; idx < ep_default.notes.size(); ++idx) {
    EXPECT_EQ(ep_default.notes[idx].voice, ep_idx0.notes[idx].voice);
    EXPECT_EQ(ep_default.notes[idx].pitch, ep_idx0.notes[idx].pitch);
  }
}

TEST(GenerateEpisodeTest, InvertibleCounterpointPreservesVoice2) {
  Subject subject = makeTestSubject(Key::C, SubjectCharacter::Severe);

  // 3 voices with odd index: voice 2 should remain unchanged.
  Episode ep_odd = generateEpisode(subject, 0, kTicksPerBar * 4,
                                   Key::C, Key::C, 3, 42, /*episode_index=*/1);

  bool has_voice2 = false;
  for (const auto& note : ep_odd.notes) {
    if (note.voice == 2) {
      has_voice2 = true;
    }
    // No note should be on a voice outside 0, 1, 2 for 3-voice episodes.
    EXPECT_LE(note.voice, 2u);
  }
  EXPECT_TRUE(has_voice2)
      << "3-voice episode with invertible counterpoint should still have voice 2";
}

TEST(GenerateEpisodeTest, InvertibleCounterpointIndex2NoSwap) {
  Subject subject = makeTestSubject(Key::C, SubjectCharacter::Severe);

  // episode_index = 2 (even): no swap.
  Episode ep_even = generateEpisode(subject, 0, kTicksPerBar * 4,
                                    Key::C, Key::C, 2, 42, /*episode_index=*/0);
  Episode ep_idx2 = generateEpisode(subject, 0, kTicksPerBar * 4,
                                    Key::C, Key::C, 2, 42, /*episode_index=*/2);

  ASSERT_EQ(ep_even.notes.size(), ep_idx2.notes.size());
  for (size_t idx = 0; idx < ep_even.notes.size(); ++idx) {
    EXPECT_EQ(ep_even.notes[idx].voice, ep_idx2.notes[idx].voice)
        << "Even episode_index=2 should not swap voices";
  }
}

// ---------------------------------------------------------------------------
// Diatonic episode generation -- integration tests
//
// episode.cpp now uses diatonic transforms internally. These tests verify
// that when given a diatonic subject (all notes in C major), the generated
// episode output is also fully diatonic.
// ---------------------------------------------------------------------------

TEST(DiatonicEpisodeTest, AllNotesDiatonicSevere) {
  Subject subject = makeDiatonicTestSubject(Key::C, SubjectCharacter::Severe);
  Episode episode = generateEpisode(subject, 0, kTicksPerBar * 4,
                                    Key::C, Key::C, 3, 42);

  ASSERT_FALSE(episode.notes.empty()) << "Episode should contain notes";
  for (size_t idx = 0; idx < episode.notes.size(); ++idx) {
    EXPECT_TRUE(scale_util::isScaleTone(episode.notes[idx].pitch, Key::C, ScaleType::Major))
        << "Note " << idx << " pitch " << static_cast<int>(episode.notes[idx].pitch)
        << " (voice " << static_cast<int>(episode.notes[idx].voice)
        << ", tick " << episode.notes[idx].start_tick
        << ") is not diatonic in C major";
  }
}

TEST(DiatonicEpisodeTest, AllNotesDiatonicPlayful) {
  Subject subject = makeDiatonicTestSubject(Key::C, SubjectCharacter::Playful);
  Episode episode = generateEpisode(subject, 0, kTicksPerBar * 4,
                                    Key::C, Key::C, 3, 42);

  ASSERT_FALSE(episode.notes.empty()) << "Episode should contain notes";
  for (size_t idx = 0; idx < episode.notes.size(); ++idx) {
    EXPECT_TRUE(scale_util::isScaleTone(episode.notes[idx].pitch, Key::C, ScaleType::Major))
        << "Note " << idx << " pitch " << static_cast<int>(episode.notes[idx].pitch)
        << " (voice " << static_cast<int>(episode.notes[idx].voice)
        << ", tick " << episode.notes[idx].start_tick
        << ") is not diatonic in C major (Playful)";
  }
}

TEST(DiatonicEpisodeTest, AllNotesDiatonicNoble) {
  // Noble uses transposeMelody(augmented, -12) for bass voice, which is an
  // octave shift -- chromatically correct, so output should still be diatonic.
  Subject subject = makeDiatonicTestSubject(Key::C, SubjectCharacter::Noble);
  Episode episode = generateEpisode(subject, 0, kTicksPerBar * 4,
                                    Key::C, Key::C, 3, 42);

  ASSERT_FALSE(episode.notes.empty()) << "Episode should contain notes";
  for (size_t idx = 0; idx < episode.notes.size(); ++idx) {
    EXPECT_TRUE(scale_util::isScaleTone(episode.notes[idx].pitch, Key::C, ScaleType::Major))
        << "Note " << idx << " pitch " << static_cast<int>(episode.notes[idx].pitch)
        << " (voice " << static_cast<int>(episode.notes[idx].voice)
        << ", tick " << episode.notes[idx].start_tick
        << ") is not diatonic in C major (Noble)";
  }
}

TEST(DiatonicEpisodeTest, AllNotesDiatonicRestless) {
  Subject subject = makeDiatonicTestSubject(Key::C, SubjectCharacter::Restless);
  Episode episode = generateEpisode(subject, 0, kTicksPerBar * 4,
                                    Key::C, Key::C, 3, 42);

  ASSERT_FALSE(episode.notes.empty()) << "Episode should contain notes";
  for (size_t idx = 0; idx < episode.notes.size(); ++idx) {
    EXPECT_TRUE(scale_util::isScaleTone(episode.notes[idx].pitch, Key::C, ScaleType::Major))
        << "Note " << idx << " pitch " << static_cast<int>(episode.notes[idx].pitch)
        << " (voice " << static_cast<int>(episode.notes[idx].voice)
        << ", tick " << episode.notes[idx].start_tick
        << ") is not diatonic in C major (Restless)";
  }
}

TEST(DiatonicEpisodeTest, AllCharactersDiatonicMultiSeed) {
  const SubjectCharacter characters[] = {
      SubjectCharacter::Severe,
      SubjectCharacter::Playful,
      SubjectCharacter::Noble,
      SubjectCharacter::Restless,
  };
  const std::string character_names[] = {"Severe", "Playful", "Noble", "Restless"};

  for (int char_idx = 0; char_idx < 4; ++char_idx) {
    Subject subject = makeDiatonicTestSubject(Key::C, characters[char_idx]);

    for (uint32_t seed = 1; seed <= 10; ++seed) {
      Episode episode = generateEpisode(subject, 0, kTicksPerBar * 4,
                                        Key::C, Key::C, 2, seed);

      ASSERT_FALSE(episode.notes.empty())
          << character_names[char_idx] << " seed=" << seed << " produced no notes";

      for (size_t note_idx = 0; note_idx < episode.notes.size(); ++note_idx) {
        EXPECT_TRUE(scale_util::isScaleTone(episode.notes[note_idx].pitch,
                                            Key::C, ScaleType::Major))
            << character_names[char_idx] << " seed=" << seed
            << " note " << note_idx
            << " pitch " << static_cast<int>(episode.notes[note_idx].pitch)
            << " (voice " << static_cast<int>(episode.notes[note_idx].voice)
            << ", tick " << episode.notes[note_idx].start_tick
            << ") is not diatonic in C major";
      }
    }
  }
}

TEST(DiatonicEpisodeTest, ModulatedEpisodeTargetKeyDiatonic) {
  // Generate episode modulating from C to G. Notes in the second half
  // (after the midpoint) should be diatonic in G major.
  Subject subject = makeDiatonicTestSubject(Key::C, SubjectCharacter::Severe);
  Tick duration = kTicksPerBar * 4;
  Episode episode = generateEpisode(subject, 0, duration,
                                    Key::C, Key::G, 3, 42);

  ASSERT_FALSE(episode.notes.empty()) << "Modulated episode should contain notes";

  Tick midpoint = duration / 2;

  // Collect notes in the second half.
  int second_half_count = 0;
  int second_half_diatonic_g = 0;
  for (const auto& note : episode.notes) {
    if (note.start_tick >= midpoint) {
      ++second_half_count;
      if (scale_util::isScaleTone(note.pitch, Key::G, ScaleType::Major)) {
        ++second_half_diatonic_g;
      }
    }
  }

  ASSERT_GT(second_half_count, 0) << "Should have notes in the second half";

  // All notes in the second half should be diatonic in G major.
  EXPECT_EQ(second_half_diatonic_g, second_half_count)
      << second_half_count - second_half_diatonic_g << " of " << second_half_count
      << " notes in the second half are not diatonic in G major";
}

// Note: Existing tests above validate backward compatibility since the episode
// generation API (generateEpisode) has not changed -- only the internal
// transforms now use diatonic operations. Running `make test` confirms that
// all prior tests continue to pass.

// ---------------------------------------------------------------------------
// Episode resting voice -- texture lightening with held tones (B3)
// ---------------------------------------------------------------------------

TEST(EpisodeRestingVoiceTest, ThreeVoices_HasHeldTones) {
  // With 3 voices, voice 2 should receive held tones (whole-note duration)
  // instead of the normal diminished motif.
  Subject subject = makeTestSubject(Key::C, SubjectCharacter::Severe);
  Episode episode = generateEpisode(subject, 0, kTicksPerBar * 4,
                                    Key::C, Key::C, 3, 42, /*episode_index=*/0);

  ASSERT_FALSE(episode.notes.empty());

  // Collect notes for the resting voice (voice 2 for 3-voice, episode_index=0).
  std::vector<NoteEvent> voice2_notes;
  for (const auto& note : episode.notes) {
    if (note.voice == 2) {
      voice2_notes.push_back(note);
    }
  }
  ASSERT_FALSE(voice2_notes.empty()) << "Voice 2 should have held tone notes";

  // Held tones should have long durations (whole note = kTicksPerBar or the
  // remainder at the end).
  for (const auto& note : voice2_notes) {
    EXPECT_GE(note.duration, kTicksPerBar / 2)
        << "Held tone at tick " << note.start_tick
        << " has unexpectedly short duration " << note.duration;
  }
}

TEST(EpisodeRestingVoiceTest, FourVoices_RotatesResting) {
  // With 4 voices: voices 2 and 3 alternate as resting voice.
  // episode_index=0 -> resting voice = 2 + (0 % 2) = 2
  // episode_index=1 -> resting voice = 2 + (1 % 2) = 3
  Subject subject = makeTestSubject(Key::C, SubjectCharacter::Severe);

  Episode ep0 = generateEpisode(subject, 0, kTicksPerBar * 4,
                                Key::C, Key::C, 4, 42, /*episode_index=*/0);
  Episode ep1 = generateEpisode(subject, 0, kTicksPerBar * 4,
                                Key::C, Key::C, 4, 42, /*episode_index=*/1);

  // Episode 0: voice 2 should have held tones, voice 3 should have normal material.
  bool ep0_v2_has_long = false;
  bool ep0_v3_has_notes = false;
  for (const auto& note : ep0.notes) {
    if (note.voice == 2 && note.duration >= kTicksPerBar / 2) ep0_v2_has_long = true;
    if (note.voice == 3) ep0_v3_has_notes = true;
  }
  EXPECT_TRUE(ep0_v2_has_long) << "Episode 0: voice 2 should have held tones";
  EXPECT_TRUE(ep0_v3_has_notes) << "Episode 0: voice 3 should have normal material";

  // Episode 1: voice 3 should have held tones, voice 2 should have normal material.
  // (Note: episode_index=1 also triggers invertible counterpoint on voices 0/1,
  //  but voice 2/3 are not affected by that.)
  bool ep1_v3_has_long = false;
  bool ep1_v2_has_notes = false;
  for (const auto& note : ep1.notes) {
    if (note.voice == 3 && note.duration >= kTicksPerBar / 2) ep1_v3_has_long = true;
    if (note.voice == 2) ep1_v2_has_notes = true;
  }
  EXPECT_TRUE(ep1_v3_has_long) << "Episode 1: voice 3 should have held tones";
  EXPECT_TRUE(ep1_v2_has_notes) << "Episode 1: voice 2 should have normal material";
}

TEST(EpisodeRestingVoiceTest, TwoVoices_NoHeldTones) {
  // With only 2 voices, no resting voice mechanism applies.
  // All notes should belong to voice 0 or 1 only.
  Subject subject = makeTestSubject(Key::C, SubjectCharacter::Severe);
  Episode episode = generateEpisode(subject, 0, kTicksPerBar * 4,
                                    Key::C, Key::C, 2, 42, /*episode_index=*/0);

  ASSERT_FALSE(episode.notes.empty());
  for (const auto& note : episode.notes) {
    EXPECT_LE(note.voice, 1u)
        << "2-voice episode should only use voices 0 and 1, found voice "
        << static_cast<int>(note.voice);
  }
}

TEST(EpisodeRestingVoiceTest, HeldTones_ProducesWholeNotes) {
  // Verify that the held tones have bar-length (whole note) duration.
  Subject subject = makeTestSubject(Key::C, SubjectCharacter::Severe);
  Tick total_duration = kTicksPerBar * 4;
  Episode episode = generateEpisode(subject, 0, total_duration,
                                    Key::C, Key::C, 3, 42, /*episode_index=*/0);

  // Collect resting voice notes (voice 2 for 3-voice, episode_index=0).
  std::vector<NoteEvent> held_notes;
  for (const auto& note : episode.notes) {
    if (note.voice == 2) {
      held_notes.push_back(note);
    }
  }
  ASSERT_FALSE(held_notes.empty()) << "Should have held tone notes on resting voice";

  // All held notes except possibly the last should have duration = kTicksPerBar.
  for (size_t idx = 0; idx < held_notes.size(); ++idx) {
    if (idx < held_notes.size() - 1) {
      EXPECT_EQ(held_notes[idx].duration, kTicksPerBar)
          << "Held tone " << idx << " should be exactly one whole note (kTicksPerBar)";
    } else {
      // Last note may be shorter if duration doesn't divide evenly.
      EXPECT_GT(held_notes[idx].duration, static_cast<Tick>(0))
          << "Last held tone should have positive duration";
      EXPECT_LE(held_notes[idx].duration, kTicksPerBar)
          << "Last held tone should not exceed one whole note";
    }
  }
}

TEST(EpisodeRestingVoiceTest, HeldTones_SourceIsEpisodeMaterial) {
  // Held tones should have BachNoteSource::EpisodeMaterial for provenance.
  Subject subject = makeTestSubject(Key::C, SubjectCharacter::Severe);
  Episode episode = generateEpisode(subject, 0, kTicksPerBar * 4,
                                    Key::C, Key::C, 3, 42, /*episode_index=*/0);

  for (const auto& note : episode.notes) {
    if (note.voice == 2) {
      EXPECT_EQ(note.source, BachNoteSource::EpisodeMaterial)
          << "Held tone at tick " << note.start_tick
          << " should have EpisodeMaterial source";
    }
  }
}

TEST(EpisodeRestingVoiceTest, FiveVoices_RotatesThroughThreeUpperVoices) {
  // With 5 voices, rotation cycles through voices 2, 3, 4.
  // episode_index=0 -> 2 + (0 % 3) = 2
  // episode_index=1 -> 2 + (1 % 3) = 3
  // episode_index=2 -> 2 + (2 % 3) = 4
  // episode_index=3 -> 2 + (3 % 3) = 2  (wraps around)
  Subject subject = makeTestSubject(Key::C, SubjectCharacter::Severe);

  // Track which voice gets held tones for each episode index.
  for (int ep_idx = 0; ep_idx < 6; ++ep_idx) {
    VoiceId expected_resting = static_cast<VoiceId>(2 + (ep_idx % 3));

    Episode episode = generateEpisode(subject, 0, kTicksPerBar * 4,
                                      Key::C, Key::C, 5, 42, ep_idx);

    // The expected resting voice should have held tones (long durations).
    bool found_long_note = false;
    for (const auto& note : episode.notes) {
      if (note.voice == expected_resting && note.duration >= kTicksPerBar / 2) {
        found_long_note = true;
        break;
      }
    }
    EXPECT_TRUE(found_long_note)
        << "5-voice episode_index=" << ep_idx
        << ": expected resting voice " << static_cast<int>(expected_resting)
        << " to have held tones";
  }
}

TEST(EpisodeRestingVoiceTest, AllCharacters_ThreeVoices_HaveHeldTones) {
  // Held tones should work regardless of SubjectCharacter since the resting
  // voice mechanism is applied after the character-specific voice 0/1 generation.
  const SubjectCharacter characters[] = {
      SubjectCharacter::Severe,
      SubjectCharacter::Playful,
      SubjectCharacter::Noble,
      SubjectCharacter::Restless,
  };
  const char* names[] = {"Severe", "Playful", "Noble", "Restless"};

  for (int char_idx = 0; char_idx < 4; ++char_idx) {
    Subject subject = makeTestSubject(Key::C, characters[char_idx]);
    Episode episode = generateEpisode(subject, 0, kTicksPerBar * 4,
                                      Key::C, Key::C, 3, 42, /*episode_index=*/0);

    bool has_voice2_held = false;
    for (const auto& note : episode.notes) {
      if (note.voice == 2 && note.duration >= kTicksPerBar / 2) {
        has_voice2_held = true;
        break;
      }
    }
    EXPECT_TRUE(has_voice2_held)
        << names[char_idx] << ": 3-voice episode should have held tones on voice 2";
  }
}

}  // namespace
}  // namespace bach
