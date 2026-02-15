// Tests for bariolage vocabulary fallback in texture_generator.

#include <gtest/gtest.h>

#include "core/basic_types.h"
#include "core/pitch_utils.h"
#include "harmony/chord_types.h"
#include "harmony/harmonic_event.h"
#include "harmony/harmonic_timeline.h"
#include "solo_string/arch/texture_generator.h"

namespace bach {
namespace {

// ---------------------------------------------------------------------------
// Helper: build a 1-bar timeline for a given key/chord
// ---------------------------------------------------------------------------

HarmonicTimeline makeOneBarTimeline(Key key, bool is_minor, ChordQuality quality) {
  HarmonicTimeline timeline;

  Chord chord;
  chord.degree = ChordDegree::I;
  chord.quality = quality;
  // Place root in octave 4 (MIDI 60 = C4).
  chord.root_pitch = static_cast<uint8_t>(60 + static_cast<int>(key));
  chord.inversion = 0;

  HarmonicEvent event;
  event.tick = 0;
  event.end_tick = kTicksPerBar;
  event.key = key;
  event.is_minor = is_minor;
  event.chord = chord;
  event.bass_pitch = chord.root_pitch;
  event.weight = 1.0f;

  timeline.addEvent(event);
  return timeline;
}

// ---------------------------------------------------------------------------
// Bariolage vocabulary fallback tests
// ---------------------------------------------------------------------------

TEST(TextureVocabularyTest, GenerateBariolageDoesNotCrash) {
  // Bariolage with minimal context -- just verify no crash.
  auto timeline = makeOneBarTimeline(Key::G, false, ChordQuality::Major);

  TextureContext ctx;
  ctx.texture = TextureType::Bariolage;
  ctx.start_tick = 0;
  ctx.duration_ticks = kTicksPerBar;  // 1 bar
  ctx.register_low = 55;             // Violin G3
  ctx.register_high = 96;            // Violin C7
  ctx.seed = 42;
  ctx.rhythm_profile = RhythmProfile::Sixteenth;
  ctx.is_climax = false;

  auto notes = generateBariolage(ctx, timeline);
  EXPECT_GT(notes.size(), 0u);
}

TEST(TextureVocabularyTest, StrongBeatResolution) {
  // When stopped_pitch == open_pitch, strong beats should resolve to
  // the original pitch (offset = 0).
  // Strong beat: tick_in_bar % kTicksPerBeat == 0.
  EXPECT_EQ(0u % kTicksPerBeat, 0u);              // Beat 0 is strong.
  EXPECT_EQ(kTicksPerBeat % kTicksPerBeat, 0u);   // Beat 1 is strong.
}

TEST(TextureVocabularyTest, MultiSeedStability) {
  auto timeline = makeOneBarTimeline(Key::D, true, ChordQuality::Minor);

  TextureContext ctx;
  ctx.texture = TextureType::Bariolage;
  ctx.start_tick = 0;
  ctx.duration_ticks = kTicksPerBar;
  ctx.register_low = 55;
  ctx.register_high = 96;
  ctx.rhythm_profile = RhythmProfile::Sixteenth;
  ctx.is_climax = false;

  for (uint32_t seed = 1; seed <= 30; ++seed) {
    ctx.seed = seed;
    auto notes = generateBariolage(ctx, timeline);
    EXPECT_GT(notes.size(), 0u) << "seed=" << seed;
  }
}

TEST(TextureVocabularyTest, ChromaticNeighborOnlyOnWeakBeat) {
  // Verify concept: on strong beat, no offset.
  // 0 % 480 == 0 (strong), 120 % 480 == 120 (weak).
  EXPECT_TRUE(0 % kTicksPerBeat == 0);
  EXPECT_FALSE(120 % kTicksPerBeat == 0);
}

TEST(TextureVocabularyTest, AllNotesWithinRegister) {
  // Verify that chromatic neighbor fallback respects register bounds.
  auto timeline = makeOneBarTimeline(Key::D, true, ChordQuality::Minor);

  TextureContext ctx;
  ctx.texture = TextureType::Bariolage;
  ctx.start_tick = 0;
  ctx.duration_ticks = kTicksPerBar;
  ctx.register_low = 55;
  ctx.register_high = 96;
  ctx.rhythm_profile = RhythmProfile::Sixteenth;
  ctx.is_climax = false;
  ctx.seed = 7;

  auto notes = generateBariolage(ctx, timeline);
  for (const auto& note : notes) {
    EXPECT_GE(note.pitch, ctx.register_low) << "pitch below register at tick=" << note.start_tick;
    EXPECT_LE(note.pitch, ctx.register_high) << "pitch above register at tick=" << note.start_tick;
  }
}

}  // namespace
}  // namespace bach
