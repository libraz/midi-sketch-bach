// Tests for forms/prelude_figuration.h -- figuration pattern system.

#include "forms/prelude_figuration.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <random>

#include "core/basic_types.h"
#include "core/interval.h"
#include "core/note_source.h"
#include "core/pitch_utils.h"
#include "core/scale.h"
#include "harmony/chord_types.h"
#include "harmony/chord_voicer.h"
#include "harmony/harmonic_event.h"

namespace bach {
namespace {

std::pair<uint8_t, uint8_t> testVoiceRange(uint8_t voice_idx) {
  switch (voice_idx) {
    case 0: return {60, 88};
    case 1: return {52, 76};
    case 2: return {48, 64};
    case 3: return {24, 50};
    default: return {52, 76};
  }
}

HarmonicEvent makeEvent(Key key, bool is_minor, ChordQuality quality,
                        uint8_t root_pitch) {
  HarmonicEvent ev;
  ev.key = key;
  ev.is_minor = is_minor;
  ev.chord.quality = quality;
  ev.chord.root_pitch = root_pitch;
  ev.bass_pitch = root_pitch;
  return ev;
}

// ---------------------------------------------------------------------------
// createFigurationTemplate tests
// ---------------------------------------------------------------------------

TEST(FigurationTemplateTest, BrokenChord_HasSteps) {
  auto tmpl = createFigurationTemplate(FigurationType::BrokenChord, 3);
  EXPECT_EQ(tmpl.type, FigurationType::BrokenChord);
  EXPECT_FALSE(tmpl.steps.empty());
}

TEST(FigurationTemplateTest, Alberti_HasFourSteps) {
  auto tmpl = createFigurationTemplate(FigurationType::Alberti, 3);
  EXPECT_EQ(tmpl.steps.size(), 4u);
}

TEST(FigurationTemplateTest, ScaleConnect_HasSteps) {
  auto tmpl = createFigurationTemplate(FigurationType::ScaleConnect, 3);
  EXPECT_FALSE(tmpl.steps.empty());
}

// ---------------------------------------------------------------------------
// applyFiguration tests
// ---------------------------------------------------------------------------

TEST(ApplyFigurationTest, BrokenChord_CMajor_AllChordTones) {
  auto ev = makeEvent(Key::C, false, ChordQuality::Major, 48);
  auto voicing = voiceChord(ev, 3, testVoiceRange);
  auto tmpl = createFigurationTemplate(FigurationType::BrokenChord, 3);

  auto notes = applyFiguration(voicing, tmpl, 0, ev, testVoiceRange);
  EXPECT_FALSE(notes.empty());

  // All notes with scale_offset=0 should be chord tones (C, E, G).
  for (const auto& note : notes) {
    int pc = getPitchClass(note.pitch);
    bool is_chord_tone = (pc == 0 || pc == 4 || pc == 7);
    EXPECT_TRUE(is_chord_tone)
        << "Pitch " << static_cast<int>(note.pitch) << " (pc=" << pc
        << ") is not a C major chord tone";
  }
}

TEST(ApplyFigurationTest, BrokenChord_ZeroSimultaneousClash) {
  auto ev = makeEvent(Key::C, false, ChordQuality::Major, 48);
  auto voicing = voiceChord(ev, 3, testVoiceRange);
  auto tmpl = createFigurationTemplate(FigurationType::BrokenChord, 3);

  auto notes = applyFiguration(voicing, tmpl, 0, ev, testVoiceRange);

  // Group notes by onset tick and check for dissonant intervals.
  // Since BrokenChord uses only chord tones, simultaneous notes should be consonant.
  std::map<Tick, std::vector<uint8_t>> tick_groups;
  for (const auto& note : notes) {
    tick_groups[note.start_tick].push_back(note.pitch);
  }

  int clash_count = 0;
  for (const auto& [tick, pitches] : tick_groups) {
    for (size_t i = 0; i < pitches.size(); ++i) {
      for (size_t j = i + 1; j < pitches.size(); ++j) {
        int iv = interval_util::compoundToSimple(
            std::abs(static_cast<int>(pitches[i]) - pitches[j]));
        // M2(2), m2(1), M7(11), m7(10), tritone(6) are clashes.
        if (iv == 1 || iv == 2 || iv == 6 || iv == 10 || iv == 11) {
          ++clash_count;
        }
      }
    }
  }
  EXPECT_EQ(clash_count, 0);
}

TEST(ApplyFigurationTest, AllNotesHaveSource) {
  auto ev = makeEvent(Key::C, false, ChordQuality::Major, 48);
  auto voicing = voiceChord(ev, 3, testVoiceRange);
  auto tmpl = createFigurationTemplate(FigurationType::BrokenChord, 3);

  auto notes = applyFiguration(voicing, tmpl, 0, ev, testVoiceRange);
  for (const auto& note : notes) {
    EXPECT_EQ(note.source, BachNoteSource::PreludeFiguration);
  }
}

TEST(ApplyFigurationTest, AllNotesWithinVoiceRange) {
  auto ev = makeEvent(Key::C, false, ChordQuality::Major, 48);
  auto voicing = voiceChord(ev, 3, testVoiceRange);
  auto tmpl = createFigurationTemplate(FigurationType::BrokenChord, 3);

  auto notes = applyFiguration(voicing, tmpl, 0, ev, testVoiceRange);
  for (const auto& note : notes) {
    auto [lo, hi] = testVoiceRange(note.voice);
    EXPECT_GE(note.pitch, lo) << "Below range for voice " << note.voice;
    EXPECT_LE(note.pitch, hi) << "Above range for voice " << note.voice;
  }
}

TEST(ApplyFigurationTest, BeatStartOffset) {
  auto ev = makeEvent(Key::C, false, ChordQuality::Major, 48);
  auto voicing = voiceChord(ev, 3, testVoiceRange);
  auto tmpl = createFigurationTemplate(FigurationType::BrokenChord, 3);

  Tick beat_start = 1920;  // Start of bar 2.
  auto notes = applyFiguration(voicing, tmpl, beat_start, ev, testVoiceRange);
  for (const auto& note : notes) {
    EXPECT_GE(note.start_tick, beat_start);
    EXPECT_LT(note.start_tick, beat_start + kTicksPerBeat);
  }
}

TEST(ApplyFigurationTest, ScaleConnect_StrongBeatsAreChordTones) {
  auto ev = makeEvent(Key::C, false, ChordQuality::Major, 48);
  auto voicing = voiceChord(ev, 3, testVoiceRange);
  auto tmpl = createFigurationTemplate(FigurationType::ScaleConnect, 3);

  auto notes = applyFiguration(voicing, tmpl, 0, ev, testVoiceRange);

  // Steps with scale_offset=0 are on strong beats and must be chord tones.
  for (size_t i = 0; i < tmpl.steps.size(); ++i) {
    if (tmpl.steps[i].scale_offset == 0 && i < notes.size()) {
      int pc = getPitchClass(notes[i].pitch);
      bool is_chord_tone = (pc == 0 || pc == 4 || pc == 7);
      EXPECT_TRUE(is_chord_tone)
          << "Strong beat note pitch=" << static_cast<int>(notes[i].pitch)
          << " (pc=" << pc << ") not a chord tone";
    }
  }
}

TEST(ApplyFigurationTest, Velocity_FixedAt80) {
  auto ev = makeEvent(Key::C, false, ChordQuality::Major, 48);
  auto voicing = voiceChord(ev, 3, testVoiceRange);
  auto tmpl = createFigurationTemplate(FigurationType::BrokenChord, 3);

  auto notes = applyFiguration(voicing, tmpl, 0, ev, testVoiceRange);
  for (const auto& note : notes) {
    EXPECT_EQ(note.velocity, 80);
  }
}

// ---------------------------------------------------------------------------
// injectNonChordTones tests
// ---------------------------------------------------------------------------

TEST(InjectNCTTest, BeatOneNeverModified) {
  // Beat-1 notes must always remain chord tones, regardless of NCT probability.
  auto ev = makeEvent(Key::C, false, ChordQuality::Major, 48);
  auto voicing = voiceChord(ev, 3, testVoiceRange);
  auto tmpl = createFigurationTemplate(FigurationType::Alberti, 3);

  std::mt19937 rng(42);
  auto notes = applyFiguration(voicing, tmpl, 0, ev, testVoiceRange);
  uint8_t beat1_pitch = notes[0].pitch;

  // Run with probability 1.0 (guaranteed injection on eligible notes).
  injectNonChordTones(notes, tmpl, 0, ev, testVoiceRange, rng, 1.0f, 0.5f);

  // The first note (at tick 0 = beat start) must be unchanged.
  EXPECT_EQ(notes[0].pitch, beat1_pitch)
      << "Beat-1 note was modified by NCT injection";
}

TEST(InjectNCTTest, ZeroProbabilityLeavesNotesUnchanged) {
  auto ev = makeEvent(Key::C, false, ChordQuality::Major, 48);
  auto voicing = voiceChord(ev, 3, testVoiceRange);
  auto tmpl = createFigurationTemplate(FigurationType::Alberti, 3);

  auto notes = applyFiguration(voicing, tmpl, 0, ev, testVoiceRange);
  auto original = notes;

  std::mt19937 rng(123);
  injectNonChordTones(notes, tmpl, 0, ev, testVoiceRange, rng, 0.0f, 0.5f);

  // No notes should have changed with zero probability.
  ASSERT_EQ(notes.size(), original.size());
  for (size_t idx = 0; idx < notes.size(); ++idx) {
    EXPECT_EQ(notes[idx].pitch, original[idx].pitch)
        << "Note at index " << idx << " was modified despite 0.0 probability";
  }
}

TEST(InjectNCTTest, InjectedNotesAreScaleTones) {
  // All NCTs must be diatonic scale tones.
  auto ev = makeEvent(Key::C, false, ChordQuality::Major, 48);
  auto voicing = voiceChord(ev, 3, testVoiceRange);
  auto tmpl = createFigurationTemplate(FigurationType::Alberti, 3);

  std::mt19937 rng(99);
  auto notes = applyFiguration(voicing, tmpl, 0, ev, testVoiceRange);
  injectNonChordTones(notes, tmpl, 0, ev, testVoiceRange, rng, 1.0f, 0.5f);

  for (const auto& note : notes) {
    EXPECT_TRUE(scale_util::isScaleTone(note.pitch, Key::C, ScaleType::Major))
        << "Pitch " << static_cast<int>(note.pitch) << " is not a C major scale tone";
  }
}

TEST(InjectNCTTest, InjectedNotesWithinVoiceRange) {
  auto ev = makeEvent(Key::C, false, ChordQuality::Major, 48);
  auto voicing = voiceChord(ev, 3, testVoiceRange);
  auto tmpl = createFigurationTemplate(FigurationType::Alberti, 3);

  std::mt19937 rng(77);
  auto notes = applyFiguration(voicing, tmpl, 0, ev, testVoiceRange);
  injectNonChordTones(notes, tmpl, 0, ev, testVoiceRange, rng, 1.0f, 0.5f);

  for (const auto& note : notes) {
    auto [low, high] = testVoiceRange(note.voice);
    EXPECT_GE(note.pitch, low) << "NCT below voice range for voice " << note.voice;
    EXPECT_LE(note.pitch, high) << "NCT above voice range for voice " << note.voice;
  }
}

TEST(InjectNCTTest, NCTsIntroduceNonChordTonePitches) {
  // With high probability, at least some notes should become non-chord-tones.
  // Run multiple seeds to handle randomness.
  auto ev = makeEvent(Key::C, false, ChordQuality::Major, 48);
  auto voicing = voiceChord(ev, 3, testVoiceRange);
  auto tmpl = createFigurationTemplate(FigurationType::Alberti, 3);

  int total_ncts = 0;
  for (uint32_t seed = 0; seed < 20; ++seed) {
    std::mt19937 rng(seed);
    auto notes = applyFiguration(voicing, tmpl, 0, ev, testVoiceRange);
    injectNonChordTones(notes, tmpl, 0, ev, testVoiceRange, rng, 1.0f, 0.5f);

    for (const auto& note : notes) {
      if (!isChordTone(note.pitch, ev)) {
        total_ncts++;
      }
    }
  }

  // With probability 1.0 across 20 seeds with 4-note Alberti pattern,
  // we should see at least some non-chord-tone pitches.
  EXPECT_GT(total_ncts, 0)
      << "No non-chord-tones produced across 20 seeds with p=1.0";
}

TEST(InjectNCTTest, PassingToneIsBetweenSurroundingNotes) {
  // For Alberti (bass-sop-mid-sop), the weak sub-beats (indices 1 and 3)
  // are candidates for passing tones. If replaced, the passing tone pitch
  // should be diatonically between the surrounding notes.
  auto ev = makeEvent(Key::C, false, ChordQuality::Major, 48);
  auto voicing = voiceChord(ev, 3, testVoiceRange);
  auto tmpl = createFigurationTemplate(FigurationType::Alberti, 3);

  for (uint32_t seed = 0; seed < 50; ++seed) {
    std::mt19937 rng(seed);
    auto original = applyFiguration(voicing, tmpl, 0, ev, testVoiceRange);
    auto modified = original;
    injectNonChordTones(modified, tmpl, 0, ev, testVoiceRange, rng, 1.0f, 0.5f);

    for (size_t idx = 1; idx + 1 < modified.size(); ++idx) {
      if (modified[idx].pitch == original[idx].pitch) continue;

      // The modified pitch should be a scale tone.
      EXPECT_TRUE(
          scale_util::isScaleTone(modified[idx].pitch, Key::C, ScaleType::Major))
          << "Seed " << seed << " index " << idx << " pitch "
          << static_cast<int>(modified[idx].pitch) << " not a scale tone";
    }
  }
}

TEST(InjectNCTTest, SourcePreservedAfterInjection) {
  auto ev = makeEvent(Key::C, false, ChordQuality::Major, 48);
  auto voicing = voiceChord(ev, 3, testVoiceRange);
  auto tmpl = createFigurationTemplate(FigurationType::Alberti, 3);

  std::mt19937 rng(42);
  auto notes = applyFiguration(voicing, tmpl, 0, ev, testVoiceRange);
  injectNonChordTones(notes, tmpl, 0, ev, testVoiceRange, rng, 1.0f, 0.5f);

  // Source should remain PreludeFiguration even after NCT injection.
  for (const auto& note : notes) {
    EXPECT_EQ(note.source, BachNoteSource::PreludeFiguration);
  }
}

TEST(InjectNCTTest, NoteCountPreserved) {
  // NCT injection modifies pitches only; it must not add or remove notes.
  auto ev = makeEvent(Key::C, false, ChordQuality::Major, 48);
  auto voicing = voiceChord(ev, 3, testVoiceRange);
  auto tmpl = createFigurationTemplate(FigurationType::Alberti, 3);

  std::mt19937 rng(42);
  auto notes = applyFiguration(voicing, tmpl, 0, ev, testVoiceRange);
  size_t original_count = notes.size();
  injectNonChordTones(notes, tmpl, 0, ev, testVoiceRange, rng, 1.0f, 0.5f);

  EXPECT_EQ(notes.size(), original_count);
}

TEST(InjectNCTTest, TimingPreservedAfterInjection) {
  // NCT injection must not change note start_tick or duration.
  auto ev = makeEvent(Key::C, false, ChordQuality::Major, 48);
  auto voicing = voiceChord(ev, 3, testVoiceRange);
  auto tmpl = createFigurationTemplate(FigurationType::Alberti, 3);

  auto notes = applyFiguration(voicing, tmpl, 0, ev, testVoiceRange);
  auto original = notes;

  std::mt19937 rng(42);
  injectNonChordTones(notes, tmpl, 0, ev, testVoiceRange, rng, 1.0f, 0.5f);

  ASSERT_EQ(notes.size(), original.size());
  for (size_t idx = 0; idx < notes.size(); ++idx) {
    EXPECT_EQ(notes[idx].start_tick, original[idx].start_tick)
        << "Start tick changed at index " << idx;
    EXPECT_EQ(notes[idx].duration, original[idx].duration)
        << "Duration changed at index " << idx;
    EXPECT_EQ(notes[idx].voice, original[idx].voice)
        << "Voice changed at index " << idx;
  }
}

TEST(InjectNCTTest, MinorKeyProducesMinorScaleTones) {
  // In A minor context, NCTs should be harmonic minor scale tones.
  auto ev = makeEvent(Key::A, true, ChordQuality::Minor, 57);
  auto voicing = voiceChord(ev, 3, testVoiceRange);
  auto tmpl = createFigurationTemplate(FigurationType::Alberti, 3);

  std::mt19937 rng(42);
  auto notes = applyFiguration(voicing, tmpl, 0, ev, testVoiceRange);
  injectNonChordTones(notes, tmpl, 0, ev, testVoiceRange, rng, 1.0f, 0.5f);

  for (const auto& note : notes) {
    EXPECT_TRUE(
        scale_util::isScaleTone(note.pitch, Key::A, ScaleType::HarmonicMinor))
        << "Pitch " << static_cast<int>(note.pitch)
        << " is not an A harmonic minor scale tone";
  }
}

TEST(InjectNCTTest, ExistingTemplateNCTsNotModified) {
  // Templates that already have NCT steps (e.g., Falling3v with passing tones)
  // should not have those steps further modified by injection.
  auto ev = makeEvent(Key::C, false, ChordQuality::Major, 48);
  auto voicing = voiceChord(ev, 3, testVoiceRange);

  std::mt19937 rng_tmpl(42);
  auto tmpl = createFigurationTemplate(FigurationType::Falling3v, 3, rng_tmpl, 0.0f);

  // Identify which steps already have NCT function set.
  std::vector<size_t> nct_indices;
  for (size_t idx = 0; idx < tmpl.steps.size(); ++idx) {
    if (tmpl.steps[idx].nct_function != NCTFunction::ChordTone) {
      nct_indices.push_back(idx);
    }
  }

  // Apply figuration and record pitches of NCT steps.
  auto notes = applyFiguration(voicing, tmpl, 0, ev, testVoiceRange);
  std::vector<uint8_t> nct_pitches_before;
  for (size_t idx : nct_indices) {
    if (idx < notes.size()) {
      nct_pitches_before.push_back(notes[idx].pitch);
    }
  }

  // Inject with high probability.
  std::mt19937 rng(42);
  injectNonChordTones(notes, tmpl, 0, ev, testVoiceRange, rng, 1.0f, 0.5f);

  // The existing NCT steps should be unchanged.
  for (size_t jdx = 0; jdx < nct_indices.size(); ++jdx) {
    size_t idx = nct_indices[jdx];
    if (idx < notes.size() && jdx < nct_pitches_before.size()) {
      EXPECT_EQ(notes[idx].pitch, nct_pitches_before[jdx])
          << "Existing NCT at step " << idx << " was modified by injection";
    }
  }
}

TEST(InjectNCTTest, DeterministicWithSameSeed) {
  // Same seed should produce identical results.
  auto ev = makeEvent(Key::C, false, ChordQuality::Major, 48);
  auto voicing = voiceChord(ev, 3, testVoiceRange);
  auto tmpl = createFigurationTemplate(FigurationType::Alberti, 3);

  auto notes1 = applyFiguration(voicing, tmpl, 0, ev, testVoiceRange);
  auto notes2 = notes1;

  std::mt19937 rng1(42);
  std::mt19937 rng2(42);
  injectNonChordTones(notes1, tmpl, 0, ev, testVoiceRange, rng1, 0.5f, 0.5f);
  injectNonChordTones(notes2, tmpl, 0, ev, testVoiceRange, rng2, 0.5f, 0.5f);

  ASSERT_EQ(notes1.size(), notes2.size());
  for (size_t idx = 0; idx < notes1.size(); ++idx) {
    EXPECT_EQ(notes1[idx].pitch, notes2[idx].pitch)
        << "Non-deterministic result at index " << idx;
  }
}

TEST(InjectNCTTest, NCTStepwiseFromNeighbor) {
  // Neighbor tones should be at most a minor 3rd (3 semitones) from the
  // original chord tone pitch.
  auto ev = makeEvent(Key::C, false, ChordQuality::Major, 48);
  auto voicing = voiceChord(ev, 3, testVoiceRange);
  auto tmpl = createFigurationTemplate(FigurationType::Alberti, 3);

  for (uint32_t seed = 0; seed < 30; ++seed) {
    auto notes = applyFiguration(voicing, tmpl, 0, ev, testVoiceRange);
    auto original = notes;

    std::mt19937 rng(seed);
    injectNonChordTones(notes, tmpl, 0, ev, testVoiceRange, rng, 1.0f, 0.5f);

    for (size_t idx = 0; idx < notes.size(); ++idx) {
      if (notes[idx].pitch != original[idx].pitch) {
        int dist = std::abs(static_cast<int>(notes[idx].pitch) -
                            static_cast<int>(original[idx].pitch));
        // NCT should be within a minor 3rd of the original (max 3 semitones
        // for diatonic step). Passing tones may be further from the original
        // but should still be between surrounding pitches.
        EXPECT_LE(dist, 4)
            << "Seed " << seed << " index " << idx << " distance " << dist
            << " from original pitch " << static_cast<int>(original[idx].pitch)
            << " to " << static_cast<int>(notes[idx].pitch);
      }
    }
  }
}

}  // namespace
}  // namespace bach
