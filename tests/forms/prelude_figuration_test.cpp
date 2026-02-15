// Tests for forms/prelude_figuration.h -- figuration pattern system.

#include "forms/prelude_figuration.h"

#include <gtest/gtest.h>

#include <algorithm>

#include "core/basic_types.h"
#include "core/interval.h"
#include "core/note_source.h"
#include "core/pitch_utils.h"
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

}  // namespace
}  // namespace bach
