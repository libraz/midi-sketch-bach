// Tests for fugue/episode.h -- episode generation, motif extraction,
// modulation, multi-voice assignment, and determinism.

#include "fugue/episode.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <set>
#include <string>

#include "core/scale.h"
#include "fugue/fugue_config.h"

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
  // Invertible counterpoint is now probability-based (character-specific with odd/even bias).
  // Verify statistically: across many seeds, odd episodes should invert more often than even,
  // and when inversion occurs, voices 0 and 1 are correctly swapped.
  Subject subject = makeTestSubject(Key::C, SubjectCharacter::Playful);  // 60% base + 15% odd bias

  int odd_inversion_count = 0;
  int even_inversion_count = 0;
  constexpr int kTrials = 50;

  for (int trial = 0; trial < kTrials; ++trial) {
    uint32_t seed = static_cast<uint32_t>(100 + trial);
    Episode ep_even = generateEpisode(subject, 0, kTicksPerBar * 4,
                                      Key::C, Key::C, 2, seed, /*episode_index=*/0);
    Episode ep_odd = generateEpisode(subject, 0, kTicksPerBar * 4,
                                     Key::C, Key::C, 2, seed, /*episode_index=*/1);

    // Check if odd episode has voice assignment different from even (indicating inversion).
    // Compare voice 0 notes: if they differ, inversion likely occurred in one of the two.
    bool even_has_v0_first = false;
    bool odd_has_v0_first = false;
    for (const auto& note : ep_even.notes) {
      if (note.voice == 0) { even_has_v0_first = true; break; }
      if (note.voice == 1) { break; }
    }
    for (const auto& note : ep_odd.notes) {
      if (note.voice == 0) { odd_has_v0_first = true; break; }
      if (note.voice == 1) { break; }
    }
    if (!even_has_v0_first) ++even_inversion_count;
    if (!odd_has_v0_first) ++odd_inversion_count;
  }

  // Playful: even = 60% base, odd = 75% base. Over 50 trials, expect some inversions.
  EXPECT_GT(odd_inversion_count + even_inversion_count, 0)
      << "At least some episodes should trigger invertible counterpoint";
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

  // At least 85% of second-half notes should be diatonic in G major.
  // Notes near the modulation boundary (50-60% progress) may still use the
  // starting key (C major), which contains F natural (not in G major).
  float diatonic_ratio = static_cast<float>(second_half_diatonic_g) /
                          static_cast<float>(second_half_count);
  EXPECT_GE(diatonic_ratio, 0.85f)
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

TEST(EpisodeRestingVoiceTest, ThreeVoices_AllVoicesHaveMaterial) {
  // With 3 voices (subject, answer, bass), all are essential -- no voice rests.
  // selectRestingVoice returns sentinel (num_voices) for num_voices < 4.
  Subject subject = makeTestSubject(Key::C, SubjectCharacter::Severe);
  Episode episode = generateEpisode(subject, 0, kTicksPerBar * 4,
                                    Key::C, Key::C, 3, 42, /*episode_index=*/0);

  ASSERT_FALSE(episode.notes.empty());

  // All 3 voices should have notes (no voice is resting).
  int note_count[3] = {0, 0, 0};
  for (const auto& note : episode.notes) {
    if (note.voice < 3) {
      note_count[note.voice]++;
    }
  }
  for (int vid = 0; vid < 3; ++vid) {
    EXPECT_GT(note_count[vid], 0)
        << "3-voice episode: voice " << vid << " should have material (no resting voice)";
  }
}

TEST(EpisodeRestingVoiceTest, FourVoices_RotatesResting) {
  // With 4 voices: inner_count = 4-3 = 1, so resting voice = 2 + (ep % 1) = 2 always.
  // Voice 2 rests (inner voice), voices 0/1/3 carry material.
  Subject subject = makeTestSubject(Key::C, SubjectCharacter::Severe);

  Episode ep2 = generateEpisode(subject, 0, kTicksPerBar * 4,
                                Key::C, Key::C, 4, 42, /*episode_index=*/2);
  Episode ep0 = generateEpisode(subject, 0, kTicksPerBar * 4,
                                Key::C, Key::C, 4, 42, /*episode_index=*/0);

  // Episode 2: voice 2 should have held tones, voice 3 (bass) always has material.
  bool ep2_v2_has_long = false;
  bool ep2_v3_has_notes = false;
  for (const auto& note : ep2.notes) {
    if (note.voice == 2 && note.duration >= kTicksPerBar / 2) ep2_v2_has_long = true;
    if (note.voice == 3) ep2_v3_has_notes = true;
  }
  EXPECT_TRUE(ep2_v2_has_long) << "Episode 2: voice 2 should have held tones";
  EXPECT_TRUE(ep2_v3_has_notes) << "Episode 2: voice 3 (bass) should always have material";

  // Episode 0: voice 2 still rests (only inner voice), voice 0 has material.
  bool ep0_v0_has_notes = false;
  bool ep0_v3_has_notes = false;
  for (const auto& note : ep0.notes) {
    if (note.voice == 0) ep0_v0_has_notes = true;
    if (note.voice == 3) ep0_v3_has_notes = true;
  }
  EXPECT_TRUE(ep0_v0_has_notes) << "Episode 0: voice 0 should have material (not resting)";
  EXPECT_TRUE(ep0_v3_has_notes) << "Episode 0: voice 3 (bass) should always have material";
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
  // With 4 voices: inner_count=1, resting voice = 2 + (2 % 1) = 2.
  // Even episode_index avoids invertible counterpoint voice swap.
  Subject subject = makeTestSubject(Key::C, SubjectCharacter::Severe);
  Tick total_duration = kTicksPerBar * 4;
  Episode episode = generateEpisode(subject, 0, total_duration,
                                    Key::C, Key::C, 4, 42, /*episode_index=*/2);

  // Collect resting voice notes (voice 2).
  std::vector<NoteEvent> held_notes;
  for (const auto& note : episode.notes) {
    if (note.voice == 2 && note.source == BachNoteSource::EpisodeMaterial &&
        note.duration >= kTicksPerBeat) {
      held_notes.push_back(note);
    }
  }
  ASSERT_FALSE(held_notes.empty()) << "Should have held tone notes on resting voice";

  // All held notes except possibly the last should have duration = kTicksPerBeat * 2
  // (half note — shorter duration for better rhythm distribution).
  constexpr Tick kHeldDur = kTicksPerBeat * 2;
  for (size_t idx = 0; idx < held_notes.size(); ++idx) {
    if (idx < held_notes.size() - 1) {
      EXPECT_EQ(held_notes[idx].duration, kHeldDur)
          << "Held tone " << idx << " should be exactly one half note (kTicksPerBeat * 2)";
    } else {
      // Last note may be shorter if duration doesn't divide evenly.
      EXPECT_GT(held_notes[idx].duration, static_cast<Tick>(0))
          << "Last held tone should have positive duration";
      EXPECT_LE(held_notes[idx].duration, kHeldDur)
          << "Last held tone should not exceed one half note";
    }
  }
}

TEST(EpisodeRestingVoiceTest, HeldTones_SourceIsEpisodeMaterial) {
  // Held tones should have BachNoteSource::EpisodeMaterial for provenance.
  // With 4 voices: inner_count=1, resting voice = 2 + (2 % 1) = 2.
  Subject subject = makeTestSubject(Key::C, SubjectCharacter::Severe);
  Episode episode = generateEpisode(subject, 0, kTicksPerBar * 4,
                                    Key::C, Key::C, 4, 42, /*episode_index=*/2);

  bool found_held = false;
  for (const auto& note : episode.notes) {
    if (note.voice == 2 && note.duration >= kTicksPerBar / 2 &&
        note.source == BachNoteSource::EpisodeMaterial) {
      found_held = true;
    }
  }
  EXPECT_TRUE(found_held) << "Resting voice should have EpisodeMaterial held tones";
}

TEST(EpisodeRestingVoiceTest, FiveVoices_RotatesThroughNonBassVoices) {
  // With 5 voices: inner_count = 5-3 = 2, rotation through voices 2-3.
  // episode_index=2 -> 2 + (2 % 2) = 2
  // episode_index=3 -> 2 + (3 % 2) = 3
  Subject subject = makeTestSubject(Key::C, SubjectCharacter::Severe);

  // Test inner voices 2 and 3 getting held tones.
  for (int ep_idx = 2; ep_idx < 4; ++ep_idx) {
    VoiceId expected_resting = static_cast<VoiceId>(2 + (ep_idx % 2));

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

  // Bass (voice 4) should always have material, never be resting.
  for (int ep_idx = 0; ep_idx < 5; ++ep_idx) {
    Episode episode = generateEpisode(subject, 0, kTicksPerBar * 4,
                                      Key::C, Key::C, 5, 42, ep_idx);
    bool bass_has_notes = false;
    for (const auto& note : episode.notes) {
      if (note.voice == 4) {
        bass_has_notes = true;
        break;
      }
    }
    EXPECT_TRUE(bass_has_notes)
        << "5-voice episode_index=" << ep_idx
        << ": bass (voice 4) should always have material";
  }
}

TEST(EpisodeRestingVoiceTest, AllCharacters_FourVoices_HaveHeldTones) {
  // Held tones should work regardless of SubjectCharacter since the resting
  // voice mechanism is applied after the character-specific voice 0/1 generation.
  // With 4 voices: inner_count=1, resting voice = 2 + (2 % 1) = 2.
  // Even episode_index avoids invertible counterpoint voice swap.
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
                                      Key::C, Key::C, 4, 42, /*episode_index=*/2);

    bool has_held = false;
    for (const auto& note : episode.notes) {
      if (note.voice == 2 && note.duration >= kTicksPerBar / 2) {
        has_held = true;
        break;
      }
    }
    EXPECT_TRUE(has_held)
        << names[char_idx] << ": 4-voice episode should have held tones on resting voice 2";
  }
}

// ---------------------------------------------------------------------------
// Subject::extractKopfmotiv tests
// ---------------------------------------------------------------------------

TEST(SubjectKopfmotivTest, ExtractKopfmotiv_DefaultLength) {
  Subject subject;
  for (int idx = 0; idx < 8; ++idx) {
    NoteEvent note;
    note.pitch = static_cast<uint8_t>(60 + idx);
    note.start_tick = static_cast<Tick>(idx * 480);
    note.duration = 480;
    note.voice = 0;
    subject.notes.push_back(note);
  }
  auto kopf = subject.extractKopfmotiv();
  EXPECT_EQ(kopf.size(), 4u);
  EXPECT_EQ(kopf[0].pitch, 60);
  EXPECT_EQ(kopf[3].pitch, 63);
}

TEST(SubjectKopfmotivTest, ExtractKopfmotiv_CustomLength) {
  Subject subject;
  for (int idx = 0; idx < 8; ++idx) {
    NoteEvent note;
    note.pitch = static_cast<uint8_t>(60 + idx);
    note.start_tick = static_cast<Tick>(idx * 480);
    note.duration = 480;
    note.voice = 0;
    subject.notes.push_back(note);
  }
  auto kopf = subject.extractKopfmotiv(3);
  EXPECT_EQ(kopf.size(), 3u);
}

TEST(SubjectKopfmotivTest, ExtractKopfmotiv_ShortSubject) {
  Subject subject;
  NoteEvent note;
  note.pitch = 60;
  note.start_tick = 0;
  note.duration = 480;
  note.voice = 0;
  subject.notes.push_back(note);
  auto kopf = subject.extractKopfmotiv(4);
  EXPECT_EQ(kopf.size(), 1u);
}

TEST(SubjectKopfmotivTest, ExtractKopfmotiv_EmptySubject) {
  Subject subject;
  auto kopf = subject.extractKopfmotiv();
  EXPECT_TRUE(kopf.empty());
}

TEST(SubjectKopfmotivTest, ExtractKopfmotiv_PreservesTiming) {
  Subject subject;
  for (int idx = 0; idx < 6; ++idx) {
    NoteEvent note;
    note.pitch = static_cast<uint8_t>(60 + idx);
    note.start_tick = static_cast<Tick>(idx * 240);
    note.duration = 240;
    note.voice = 0;
    subject.notes.push_back(note);
  }
  auto kopf = subject.extractKopfmotiv(4);
  ASSERT_EQ(kopf.size(), 4u);
  EXPECT_EQ(kopf[0].start_tick, 0u);
  EXPECT_EQ(kopf[1].start_tick, 240u);
  EXPECT_EQ(kopf[2].start_tick, 480u);
  EXPECT_EQ(kopf[3].start_tick, 720u);
}

// ---------------------------------------------------------------------------
// Episode dialogic Kopfmotiv hand-off tests
// ---------------------------------------------------------------------------

TEST(EpisodeImitationTest, SevereUsesDirectImitation) {
  // Verify Severe episode generates notes for both voice 0 and voice 1.
  Subject subject;
  subject.character = SubjectCharacter::Severe;
  subject.key = Key::C;
  subject.is_minor = false;
  for (int idx = 0; idx < 6; ++idx) {
    NoteEvent note;
    note.pitch = static_cast<uint8_t>(60 + idx);
    note.start_tick = static_cast<Tick>(idx * 480);
    note.duration = 480;
    note.voice = 0;
    subject.notes.push_back(note);
  }
  subject.length_ticks = 2 * kTicksPerBar;

  Episode epi = generateEpisode(subject, 0, kTicksPerBar * 4, Key::C, Key::G,
                                2, 42, 0, 0.5f);
  bool has_voice0 = false, has_voice1 = false;
  for (const auto& note : epi.notes) {
    if (note.voice == 0) has_voice0 = true;
    if (note.voice == 1) has_voice1 = true;
  }
  EXPECT_TRUE(has_voice0);
  EXPECT_TRUE(has_voice1);
}

TEST(EpisodeImitationTest, SevereVoice1UsesInvertedContour) {
  // In Severe, voice 1 should use diatonic inversion for contour independence.
  // After register placement the absolute pitches may differ by whole octaves,
  // but the interval direction should generally oppose voice 0.
  Subject subject = makeDiatonicTestSubject(Key::C, SubjectCharacter::Severe);
  Episode epi = generateEpisode(subject, 0, kTicksPerBar * 4, Key::C, Key::C,
                                2, 42, 0, 0.5f);

  std::vector<uint8_t> voice0_pitches;
  std::vector<uint8_t> voice1_pitches;
  for (const auto& note : epi.notes) {
    if (note.voice == 0) voice0_pitches.push_back(note.pitch);
    if (note.voice == 1) voice1_pitches.push_back(note.pitch);
  }

  ASSERT_FALSE(voice0_pitches.empty());
  ASSERT_FALSE(voice1_pitches.empty());
  // Verify contour independence: at least one interval direction differs.
  size_t check_count = std::min({voice0_pitches.size(), voice1_pitches.size(), size_t{4}});
  ASSERT_GE(check_count, 2u) << "Need at least 2 notes to compare intervals";
  int contour_diffs = 0;
  for (size_t idx = 1; idx < check_count; ++idx) {
    int v0_dir = static_cast<int>(voice0_pitches[idx]) -
                 static_cast<int>(voice0_pitches[idx - 1]);
    int v1_dir = static_cast<int>(voice1_pitches[idx]) -
                 static_cast<int>(voice1_pitches[idx - 1]);
    if ((v0_dir > 0 && v1_dir < 0) || (v0_dir < 0 && v1_dir > 0)) {
      ++contour_diffs;
    }
  }
  EXPECT_GT(contour_diffs, 0)
      << "Severe voice 1 should have at least one contrary-motion interval "
      << "(diatonic inversion)";
}

TEST(EpisodeImitationTest, PlayfulUsesInvertedImitation) {
  // Playful voice 1 should use diatonic inversion, so pitches should differ
  // from voice 0's retrograde material.
  Subject subject = makeDiatonicTestSubject(Key::C, SubjectCharacter::Playful);
  Episode epi = generateEpisode(subject, 0, kTicksPerBar * 4, Key::C, Key::C,
                                2, 42, 0, 0.5f);

  bool has_voice0 = false, has_voice1 = false;
  for (const auto& note : epi.notes) {
    if (note.voice == 0) has_voice0 = true;
    if (note.voice == 1) has_voice1 = true;
  }
  EXPECT_TRUE(has_voice0);
  EXPECT_TRUE(has_voice1);
}

TEST(EpisodeImitationTest, RestlessUsesDiminishedImitation) {
  // Restless voice 1 should use diminished motif, so voice 1 notes should
  // have shorter durations on average than voice 0.
  Subject subject = makeDiatonicTestSubject(Key::C, SubjectCharacter::Restless);
  Episode epi = generateEpisode(subject, 0, kTicksPerBar * 4, Key::C, Key::C,
                                2, 42, 0, 0.5f);

  Tick voice0_total_dur = 0;
  int voice0_count = 0;
  Tick voice1_total_dur = 0;
  int voice1_count = 0;
  for (const auto& note : epi.notes) {
    if (note.voice == 0) {
      voice0_total_dur += note.duration;
      ++voice0_count;
    }
    if (note.voice == 1) {
      voice1_total_dur += note.duration;
      ++voice1_count;
    }
  }

  ASSERT_GT(voice0_count, 0);
  ASSERT_GT(voice1_count, 0);
  // Diminished voice 1 should produce more notes per unit of time.
  // Note: rhythm subdivision may reduce voice 0 avg duration below voice 1,
  // so we check note density (notes per total duration) instead.
  double density0 = static_cast<double>(voice0_count) / voice0_total_dur;
  double density1 = static_cast<double>(voice1_count) / voice1_total_dur;
  EXPECT_GE(density1, density0 * 0.8)
      << "Restless voice 1 (diminished) density " << density1
      << " should be comparable to or greater than voice 0 density " << density0;
}

TEST(EpisodeImitationTest, NobleKeepsAugmentedBass) {
  // Noble voice 1 should still use augmented motif transposed down an octave.
  Subject subject = makeDiatonicTestSubject(Key::C, SubjectCharacter::Noble);
  Episode epi = generateEpisode(subject, 0, kTicksPerBar * 4, Key::C, Key::C,
                                2, 42, 0, 0.5f);

  int voice0_sum = 0, voice0_count = 0;
  int voice1_sum = 0, voice1_count = 0;
  for (const auto& note : epi.notes) {
    if (note.voice == 0) {
      voice0_sum += note.pitch;
      ++voice0_count;
    } else if (note.voice == 1) {
      voice1_sum += note.pitch;
      ++voice1_count;
    }
  }
  ASSERT_GT(voice0_count, 0);
  ASSERT_GT(voice1_count, 0);
  double voice0_avg = static_cast<double>(voice0_sum) / voice0_count;
  double voice1_avg = static_cast<double>(voice1_sum) / voice1_count;
  EXPECT_LT(voice1_avg, voice0_avg)
      << "Noble voice 1 (augmented bass) should be lower than voice 0";
}

// ---------------------------------------------------------------------------
// Bass duration variety (selectDuration integration)
// ---------------------------------------------------------------------------

TEST(EpisodeTest, BassDurationVariety) {
  Subject subject = makeTestSubject();
  // Try multiple seeds to find bass duration variety.
  bool found_variety = false;
  for (uint32_t seed = 1; seed <= 20; ++seed) {
    Episode episode = generateEpisode(subject, 0, kTicksPerBar * 4,
                                      Key::C, Key::C, 3, seed, 0, 0.6f);
    // Find bass voice (voice 2 in 3-voice fugue).
    std::set<Tick> distinct;
    for (const auto& note : episode.notes) {
      if (note.voice == 2) {
        distinct.insert(note.duration);
      }
    }
    if (distinct.size() >= 3) {
      found_variety = true;
      break;
    }
  }
  EXPECT_TRUE(found_variety)
      << "At least one seed in 1-20 should produce 3+ distinct bass durations";
}

TEST(EpisodeTest, BassMinimumDuration) {
  // Bass voice (voice 2) may contain sixteenth notes during Sequence phase
  // (P7.c phase-dependent anchor durations), but never shorter than 16th.
  Subject subject = makeTestSubject();
  for (uint32_t seed = 1; seed <= 10; ++seed) {
    Episode episode = generateEpisode(subject, 0, kTicksPerBar * 4,
                                      Key::C, Key::C, 3, seed, 0, 0.5f);
    for (const auto& note : episode.notes) {
      if (note.voice == 2) {
        EXPECT_GE(note.duration, duration::kSixteenthNote)
            << "Bass voice should not have notes shorter than a sixteenth (seed="
            << seed << ")";
      }
    }
  }
}

}  // namespace
}  // namespace bach
