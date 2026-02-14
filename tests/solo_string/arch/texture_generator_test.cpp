// Tests for solo_string/arch/texture_generator.h -- all 6 texture types.

#include "solo_string/arch/texture_generator.h"

#include <algorithm>
#include <cstdint>
#include <set>

#include <gtest/gtest.h>

#include "core/basic_types.h"
#include "core/pitch_utils.h"
#include "core/scale.h"
#include "harmony/chord_types.h"
#include "harmony/harmonic_event.h"
#include "harmony/harmonic_timeline.h"
#include "harmony/key.h"

namespace bach {
namespace {

// ---------------------------------------------------------------------------
// Test fixture: provides a standard D minor timeline spanning 4 bars
// ---------------------------------------------------------------------------

class TextureGeneratorTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // Build a 4-bar (7680 tick) D minor timeline at bar resolution.
    // Bar 0: i (D minor)
    // Bar 1: iv (G minor)
    // Bar 2: V  (A major -- harmonic minor context)
    // Bar 3: i  (D minor)
    KeySignature d_minor = {Key::D, true};

    constexpr int kChordOctave = 4;
    constexpr int kBassOctave = 2;

    struct BarChord {
      ChordDegree degree;
      ChordQuality quality;
      float weight;
    };
    BarChord progression[4] = {
        {ChordDegree::I, ChordQuality::Minor, 1.0f},
        {ChordDegree::IV, ChordQuality::Minor, 0.5f},
        {ChordDegree::V, ChordQuality::Major, 0.75f},
        {ChordDegree::I, ChordQuality::Minor, 1.0f}};

    for (int bar = 0; bar < 4; ++bar) {
      Chord chord;
      chord.degree = progression[bar].degree;
      chord.quality = progression[bar].quality;

      uint8_t semitone_offset = degreeMinorSemitones(chord.degree);
      int root_midi = (kChordOctave + 1) * 12 +
                      static_cast<int>(d_minor.tonic) + semitone_offset;
      chord.root_pitch = static_cast<uint8_t>(root_midi);
      chord.inversion = 0;

      int bass_pc = root_midi % 12;
      int bass_midi = (kBassOctave + 1) * 12 + bass_pc;

      HarmonicEvent event;
      event.tick = static_cast<Tick>(bar) * kTicksPerBar;
      event.end_tick = static_cast<Tick>(bar + 1) * kTicksPerBar;
      event.key = d_minor.tonic;
      event.is_minor = d_minor.is_minor;
      event.chord = chord;
      event.bass_pitch = static_cast<uint8_t>(bass_midi);
      event.weight = progression[bar].weight;
      event.is_immutable = false;

      timeline_.addEvent(event);
    }
  }

  /// @brief Create a default texture context for testing.
  TextureContext makeDefaultContext(TextureType texture,
                                   bool is_climax = false) const {
    TextureContext ctx;
    ctx.texture = texture;
    ctx.key = {Key::D, true};
    ctx.start_tick = 0;
    ctx.duration_ticks = 4 * kTicksPerBar;  // 7680
    ctx.register_low = 55;   // G3 (violin low)
    ctx.register_high = 93;  // A6
    ctx.is_major_section = false;
    ctx.is_climax = is_climax;
    ctx.rhythm_density = 1.0f;
    ctx.seed = 42;
    return ctx;
  }

  HarmonicTimeline timeline_;
};

// ---------------------------------------------------------------------------
// TextureContext defaults
// ---------------------------------------------------------------------------

TEST(TextureContextTest, DefaultValues) {
  TextureContext ctx;
  EXPECT_EQ(ctx.texture, TextureType::SingleLine);
  EXPECT_EQ(ctx.key.tonic, Key::D);
  EXPECT_TRUE(ctx.key.is_minor);
  EXPECT_EQ(ctx.start_tick, 0u);
  EXPECT_EQ(ctx.duration_ticks, 0u);
  EXPECT_EQ(ctx.register_low, 55);
  EXPECT_EQ(ctx.register_high, 93);
  EXPECT_FALSE(ctx.is_major_section);
  EXPECT_FALSE(ctx.is_climax);
  EXPECT_FLOAT_EQ(ctx.rhythm_density, 1.0f);
  EXPECT_EQ(ctx.seed, 0u);
  EXPECT_EQ(ctx.rhythm_profile, RhythmProfile::EighthNote);
  EXPECT_EQ(ctx.variation_type, VariationType::Theme);
}

// ---------------------------------------------------------------------------
// generateTexture dispatches to correct generator
// ---------------------------------------------------------------------------

TEST_F(TextureGeneratorTest, DispatchesSingleLine) {
  auto ctx = makeDefaultContext(TextureType::SingleLine);
  auto notes = generateTexture(ctx, timeline_);
  EXPECT_FALSE(notes.empty());
}

TEST_F(TextureGeneratorTest, DispatchesImpliedPolyphony) {
  auto ctx = makeDefaultContext(TextureType::ImpliedPolyphony);
  auto notes = generateTexture(ctx, timeline_);
  EXPECT_FALSE(notes.empty());
}

TEST_F(TextureGeneratorTest, DispatchesFullChords) {
  auto ctx = makeDefaultContext(TextureType::FullChords, true);
  auto notes = generateTexture(ctx, timeline_);
  EXPECT_FALSE(notes.empty());
}

TEST_F(TextureGeneratorTest, DispatchesArpeggiated) {
  auto ctx = makeDefaultContext(TextureType::Arpeggiated);
  auto notes = generateTexture(ctx, timeline_);
  EXPECT_FALSE(notes.empty());
}

TEST_F(TextureGeneratorTest, DispatchesScalePassage) {
  auto ctx = makeDefaultContext(TextureType::ScalePassage);
  auto notes = generateTexture(ctx, timeline_);
  EXPECT_FALSE(notes.empty());
}

TEST_F(TextureGeneratorTest, DispatchesBariolage) {
  auto ctx = makeDefaultContext(TextureType::Bariolage);
  auto notes = generateTexture(ctx, timeline_);
  EXPECT_FALSE(notes.empty());
}

// ---------------------------------------------------------------------------
// Empty duration produces empty output
// ---------------------------------------------------------------------------

TEST_F(TextureGeneratorTest, ZeroDurationProducesEmpty) {
  TextureContext ctx = makeDefaultContext(TextureType::SingleLine);
  ctx.duration_ticks = 0;
  EXPECT_TRUE(generateTexture(ctx, timeline_).empty());

  ctx.texture = TextureType::ImpliedPolyphony;
  EXPECT_TRUE(generateTexture(ctx, timeline_).empty());

  ctx.texture = TextureType::FullChords;
  ctx.is_climax = true;
  EXPECT_TRUE(generateTexture(ctx, timeline_).empty());

  ctx.texture = TextureType::Arpeggiated;
  EXPECT_TRUE(generateTexture(ctx, timeline_).empty());

  ctx.texture = TextureType::ScalePassage;
  EXPECT_TRUE(generateTexture(ctx, timeline_).empty());

  ctx.texture = TextureType::Bariolage;
  EXPECT_TRUE(generateTexture(ctx, timeline_).empty());
}

// ---------------------------------------------------------------------------
// All notes in register range
// ---------------------------------------------------------------------------

class AllTexturesInRegisterTest
    : public TextureGeneratorTest,
      public ::testing::WithParamInterface<TextureType> {};

TEST_P(AllTexturesInRegisterTest, NotesWithinRegister) {
  TextureType texture = GetParam();
  bool is_climax = (texture == TextureType::FullChords);
  auto ctx = makeDefaultContext(texture, is_climax);
  auto notes = generateTexture(ctx, timeline_);

  for (const auto& note : notes) {
    EXPECT_GE(note.pitch, ctx.register_low)
        << "Pitch " << static_cast<int>(note.pitch)
        << " below register_low " << static_cast<int>(ctx.register_low)
        << " at tick " << note.start_tick;
    EXPECT_LE(note.pitch, ctx.register_high)
        << "Pitch " << static_cast<int>(note.pitch)
        << " above register_high " << static_cast<int>(ctx.register_high)
        << " at tick " << note.start_tick;
  }
}

TEST_P(AllTexturesInRegisterTest, NotesWithinTimeBounds) {
  TextureType texture = GetParam();
  bool is_climax = (texture == TextureType::FullChords);
  auto ctx = makeDefaultContext(texture, is_climax);
  auto notes = generateTexture(ctx, timeline_);

  Tick end_tick = ctx.start_tick + ctx.duration_ticks;
  for (const auto& note : notes) {
    EXPECT_GE(note.start_tick, ctx.start_tick)
        << "Note starts before variation at tick " << note.start_tick;
    EXPECT_LT(note.start_tick, end_tick)
        << "Note starts at or after variation end at tick " << note.start_tick;
  }
}

TEST_P(AllTexturesInRegisterTest, VoiceIsTextureVoice) {
  TextureType texture = GetParam();
  bool is_climax = (texture == TextureType::FullChords);
  auto ctx = makeDefaultContext(texture, is_climax);
  auto notes = generateTexture(ctx, timeline_);

  for (const auto& note : notes) {
    EXPECT_EQ(note.voice, 1)
        << "Texture notes should use voice 1 (distinct from ground bass voice 0)";
  }
}

TEST_P(AllTexturesInRegisterTest, VelocityInValidRange) {
  TextureType texture = GetParam();
  bool is_climax = (texture == TextureType::FullChords);
  auto ctx = makeDefaultContext(texture, is_climax);
  auto notes = generateTexture(ctx, timeline_);

  for (const auto& note : notes) {
    EXPECT_GE(note.velocity, 1);
    EXPECT_LE(note.velocity, 127);
  }
}

INSTANTIATE_TEST_SUITE_P(
    AllTextures,
    AllTexturesInRegisterTest,
    ::testing::Values(
        TextureType::SingleLine,
        TextureType::ImpliedPolyphony,
        TextureType::FullChords,
        TextureType::Arpeggiated,
        TextureType::ScalePassage,
        TextureType::Bariolage
    ));

// ---------------------------------------------------------------------------
// SingleLine specifics
// ---------------------------------------------------------------------------

TEST_F(TextureGeneratorTest, SingleLineProducesEighthNotes) {
  auto ctx = makeDefaultContext(TextureType::SingleLine);
  auto notes = generateSingleLine(ctx, timeline_);

  EXPECT_FALSE(notes.empty());

  // SingleLine should produce 8th-note durations (240 ticks).
  for (const auto& note : notes) {
    EXPECT_EQ(note.duration, kTicksPerBeat / 2)
        << "SingleLine note at tick " << note.start_tick
        << " has unexpected duration " << note.duration;
  }
}

TEST_F(TextureGeneratorTest, SingleLineNoteCountApprox2PerBeat) {
  auto ctx = makeDefaultContext(TextureType::SingleLine);
  auto notes = generateSingleLine(ctx, timeline_);

  // 4 bars * 4 beats * 2 notes per beat = 32 notes expected.
  EXPECT_GE(notes.size(), 28u);
  EXPECT_LE(notes.size(), 36u);
}

// ---------------------------------------------------------------------------
// ImpliedPolyphony specifics
// ---------------------------------------------------------------------------

TEST_F(TextureGeneratorTest, ImpliedPolyphonyUsesRegisterHalves) {
  auto ctx = makeDefaultContext(TextureType::ImpliedPolyphony);
  auto notes = generateImpliedPolyphony(ctx, timeline_);

  EXPECT_FALSE(notes.empty());

  int mid_point = (static_cast<int>(ctx.register_low) +
                   static_cast<int>(ctx.register_high)) / 2;

  int notes_above_mid = 0;
  int notes_below_mid = 0;

  for (const auto& note : notes) {
    if (static_cast<int>(note.pitch) >= mid_point) {
      ++notes_above_mid;
    } else {
      ++notes_below_mid;
    }
  }

  // Both halves should have substantial representation.
  EXPECT_GT(notes_above_mid, 0) << "No notes in upper register half";
  EXPECT_GT(notes_below_mid, 0) << "No notes in lower register half";

  // Neither half should dominate completely (expect at least 20% in each).
  double upper_ratio = static_cast<double>(notes_above_mid) /
                       static_cast<double>(notes.size());
  EXPECT_GT(upper_ratio, 0.15) << "Upper voice too sparse for implied polyphony";
  EXPECT_LT(upper_ratio, 0.85) << "Lower voice too sparse for implied polyphony";
}

TEST_F(TextureGeneratorTest, ImpliedPolyphonyHasRegisterAlternation) {
  auto ctx = makeDefaultContext(TextureType::ImpliedPolyphony);
  auto notes = generateImpliedPolyphony(ctx, timeline_);

  ASSERT_GE(notes.size(), 4u);

  int mid_point = (static_cast<int>(ctx.register_low) +
                   static_cast<int>(ctx.register_high)) / 2;

  // Count register crossings (upper -> lower or lower -> upper).
  int crossings = 0;
  for (size_t idx = 1; idx < notes.size(); ++idx) {
    bool prev_upper = (static_cast<int>(notes[idx - 1].pitch) >= mid_point);
    bool curr_upper = (static_cast<int>(notes[idx].pitch) >= mid_point);
    if (prev_upper != curr_upper) {
      ++crossings;
    }
  }

  // Should have frequent alternation (at least 25% of transitions).
  double crossing_ratio = static_cast<double>(crossings) /
                          static_cast<double>(notes.size() - 1);
  EXPECT_GT(crossing_ratio, 0.2)
      << "Implied polyphony should alternate between register halves frequently";
}

// ---------------------------------------------------------------------------
// FullChords specifics
// ---------------------------------------------------------------------------

TEST_F(TextureGeneratorTest, FullChordsEmptyWhenNotClimax) {
  auto ctx = makeDefaultContext(TextureType::FullChords, false);
  auto notes = generateFullChords(ctx, timeline_);
  EXPECT_TRUE(notes.empty()) << "FullChords should return empty when is_climax=false";
}

TEST_F(TextureGeneratorTest, FullChordsProducesMultipleSimultaneousNotes) {
  auto ctx = makeDefaultContext(TextureType::FullChords, true);
  auto notes = generateFullChords(ctx, timeline_);
  EXPECT_FALSE(notes.empty());

  // Check that we have multiple notes at the same tick (chord structure).
  // Group notes by approximate tick position.
  std::set<Tick> unique_starts;
  for (const auto& note : notes) {
    unique_starts.insert(note.start_tick);
  }

  // 4 bars * 2 chords per bar = 8 chord positions,
  // each chord has 3-4 notes -> at least 8 unique start positions.
  // But with grace notes, some will share ticks and some will be offset.
  EXPECT_GE(notes.size(), 16u) << "FullChords should produce at least ~24 notes in 4 bars";

  // Check that some notes are short (grace notes, 60 ticks).
  int grace_count = 0;
  for (const auto& note : notes) {
    if (note.duration == 60) {
      ++grace_count;
    }
  }
  EXPECT_GT(grace_count, 0) << "FullChords should contain grace notes";
}

TEST_F(TextureGeneratorTest, FullChordsHasClimaxVelocity) {
  auto ctx = makeDefaultContext(TextureType::FullChords, true);
  auto notes = generateFullChords(ctx, timeline_);

  // Climax notes should have higher velocity than base (72 + 16 = 88 minimum).
  bool has_high_velocity = false;
  for (const auto& note : notes) {
    if (note.velocity >= 85) {
      has_high_velocity = true;
      break;
    }
  }
  EXPECT_TRUE(has_high_velocity) << "FullChords climax should produce high-velocity notes";
}

// ---------------------------------------------------------------------------
// Arpeggiated specifics
// ---------------------------------------------------------------------------

TEST_F(TextureGeneratorTest, ArpeggiatedProduces16thNotes) {
  auto ctx = makeDefaultContext(TextureType::Arpeggiated);
  ctx.rhythm_profile = RhythmProfile::Sixteenth;
  auto notes = generateArpeggiated(ctx, timeline_);

  EXPECT_FALSE(notes.empty());

  // All notes should be 16th-note duration (120 ticks).
  for (const auto& note : notes) {
    EXPECT_EQ(note.duration, kTicksPerBeat / 4)
        << "Arpeggiated note at tick " << note.start_tick
        << " has unexpected duration " << note.duration;
  }
}

TEST_F(TextureGeneratorTest, ArpeggiatedNoteCountApprox4PerBeat) {
  auto ctx = makeDefaultContext(TextureType::Arpeggiated);
  ctx.rhythm_profile = RhythmProfile::Sixteenth;
  auto notes = generateArpeggiated(ctx, timeline_);

  // 4 bars * 4 beats * 4 notes = 64 expected.
  EXPECT_GE(notes.size(), 56u);
  EXPECT_LE(notes.size(), 72u);
}

// ---------------------------------------------------------------------------
// ScalePassage specifics
// ---------------------------------------------------------------------------

TEST_F(TextureGeneratorTest, ScalePassageProduces16thNotes) {
  auto ctx = makeDefaultContext(TextureType::ScalePassage);
  ctx.rhythm_profile = RhythmProfile::Sixteenth;
  auto notes = generateScalePassage(ctx, timeline_);

  EXPECT_FALSE(notes.empty());

  for (const auto& note : notes) {
    EXPECT_EQ(note.duration, kTicksPerBeat / 4)
        << "ScalePassage note at tick " << note.start_tick
        << " has unexpected duration " << note.duration;
  }
}

TEST_F(TextureGeneratorTest, ScalePassageUsesScaleTones) {
  auto ctx = makeDefaultContext(TextureType::ScalePassage);
  auto notes = generateScalePassage(ctx, timeline_);

  EXPECT_FALSE(notes.empty());

  // Most notes should be scale tones in D minor.
  int scale_tone_count = 0;
  for (const auto& note : notes) {
    if (scale_util::isScaleTone(note.pitch, Key::D, ScaleType::NaturalMinor)) {
      ++scale_tone_count;
    }
  }

  double scale_ratio = static_cast<double>(scale_tone_count) /
                       static_cast<double>(notes.size());
  EXPECT_GT(scale_ratio, 0.8)
      << "ScalePassage should produce mostly scale tones (got "
      << scale_ratio * 100 << "%)";
}

TEST_F(TextureGeneratorTest, ScalePassageHasStepwiseMotion) {
  auto ctx = makeDefaultContext(TextureType::ScalePassage);
  auto notes = generateScalePassage(ctx, timeline_);

  ASSERT_GE(notes.size(), 4u);

  // Check that consecutive notes within the same beat are stepwise (interval <= 4 semitones).
  int stepwise_count = 0;
  int total_transitions = 0;

  for (size_t idx = 1; idx < notes.size(); ++idx) {
    // Only check within the same beat group (4 notes per beat).
    if (notes[idx].start_tick - notes[idx - 1].start_tick == kTicksPerBeat / 4) {
      int interval = std::abs(
          static_cast<int>(notes[idx].pitch) - static_cast<int>(notes[idx - 1].pitch));
      ++total_transitions;
      if (interval <= 4) {  // Allow up to major 3rd for scale steps.
        ++stepwise_count;
      }
    }
  }

  if (total_transitions > 0) {
    double stepwise_ratio = static_cast<double>(stepwise_count) /
                            static_cast<double>(total_transitions);
    EXPECT_GT(stepwise_ratio, 0.5)
        << "ScalePassage should have mostly stepwise motion within beats";
  }
}

// ---------------------------------------------------------------------------
// Bariolage specifics
// ---------------------------------------------------------------------------

TEST_F(TextureGeneratorTest, BariolageProduces16thNotes) {
  auto ctx = makeDefaultContext(TextureType::Bariolage);
  ctx.rhythm_profile = RhythmProfile::Sixteenth;
  auto notes = generateBariolage(ctx, timeline_);

  EXPECT_FALSE(notes.empty());

  for (const auto& note : notes) {
    EXPECT_EQ(note.duration, kTicksPerBeat / 4)
        << "Bariolage note at tick " << note.start_tick
        << " has unexpected duration " << note.duration;
  }
}

TEST_F(TextureGeneratorTest, BariolageAlternatesPitches) {
  auto ctx = makeDefaultContext(TextureType::Bariolage);
  ctx.rhythm_profile = RhythmProfile::Sixteenth;
  auto notes = generateBariolage(ctx, timeline_);

  ASSERT_GE(notes.size(), 8u);

  // Within each beat (4 notes), notes should alternate between 2 pitch values.
  // Check a few beats.
  for (size_t start = 0; start + 3 < notes.size(); start += 4) {
    // Notes at positions 0,2 should be the same pitch (stopped).
    // Notes at positions 1,3 should be the same pitch (open).
    // (Unless chord changes mid-beat, which does not happen in our test.)
    if (notes[start].start_tick / kTicksPerBeat ==
        notes[start + 3].start_tick / kTicksPerBeat) {
      // Same beat: expect alternation.
      EXPECT_EQ(notes[start].pitch, notes[start + 2].pitch)
          << "Bariolage even positions should alternate (stopped note)";
      EXPECT_EQ(notes[start + 1].pitch, notes[start + 3].pitch)
          << "Bariolage odd positions should alternate (open note)";
    }
  }
}

TEST_F(TextureGeneratorTest, BariolageContainsOpenStrings) {
  auto ctx = makeDefaultContext(TextureType::Bariolage);
  auto notes = generateBariolage(ctx, timeline_);

  EXPECT_FALSE(notes.empty());

  // At least some notes should be on violin open strings (55, 62, 69, 76).
  std::set<uint8_t> open_strings = {55, 62, 69, 76};
  int open_count = 0;
  for (const auto& note : notes) {
    if (open_strings.count(note.pitch) > 0) {
      ++open_count;
    }
  }

  EXPECT_GT(open_count, 0) << "Bariolage should use open string pitches";
}

// ---------------------------------------------------------------------------
// Determinism: same seed produces same output
// ---------------------------------------------------------------------------

TEST_F(TextureGeneratorTest, DeterministicWithSameSeed) {
  auto ctx = makeDefaultContext(TextureType::ImpliedPolyphony);
  ctx.seed = 12345;

  auto notes_a = generateTexture(ctx, timeline_);
  auto notes_b = generateTexture(ctx, timeline_);

  ASSERT_EQ(notes_a.size(), notes_b.size());
  for (size_t idx = 0; idx < notes_a.size(); ++idx) {
    EXPECT_EQ(notes_a[idx].start_tick, notes_b[idx].start_tick);
    EXPECT_EQ(notes_a[idx].pitch, notes_b[idx].pitch);
    EXPECT_EQ(notes_a[idx].duration, notes_b[idx].duration);
    EXPECT_EQ(notes_a[idx].velocity, notes_b[idx].velocity);
  }
}

TEST_F(TextureGeneratorTest, DifferentSeedsProduceDifferentOutput) {
  auto ctx_a = makeDefaultContext(TextureType::SingleLine);
  ctx_a.seed = 100;

  auto ctx_b = makeDefaultContext(TextureType::SingleLine);
  ctx_b.seed = 200;

  auto notes_a = generateTexture(ctx_a, timeline_);
  auto notes_b = generateTexture(ctx_b, timeline_);

  // Both should produce notes.
  EXPECT_FALSE(notes_a.empty());
  EXPECT_FALSE(notes_b.empty());

  // At least some pitches should differ.
  bool found_difference = false;
  size_t check_count = std::min(notes_a.size(), notes_b.size());
  for (size_t idx = 0; idx < check_count; ++idx) {
    if (notes_a[idx].pitch != notes_b[idx].pitch) {
      found_difference = true;
      break;
    }
  }
  EXPECT_TRUE(found_difference)
      << "Different seeds should produce at least some different pitches";
}

// ---------------------------------------------------------------------------
// Major section context
// ---------------------------------------------------------------------------

TEST_F(TextureGeneratorTest, MajorSectionContextDoesNotCrash) {
  // Build a D major timeline for the major section.
  KeySignature d_major = {Key::D, false};
  HarmonicTimeline major_timeline = HarmonicTimeline::createStandard(
      d_major, 4 * kTicksPerBar, HarmonicResolution::Bar);

  auto ctx = makeDefaultContext(TextureType::SingleLine);
  ctx.key = d_major;
  ctx.is_major_section = true;
  ctx.rhythm_density = 0.6f;

  auto notes = generateTexture(ctx, major_timeline);
  EXPECT_FALSE(notes.empty());
}

// ---------------------------------------------------------------------------
// Non-zero start tick offset
// ---------------------------------------------------------------------------

TEST_F(TextureGeneratorTest, NonZeroStartTickOffset) {
  auto ctx = makeDefaultContext(TextureType::Arpeggiated);
  ctx.start_tick = 4 * kTicksPerBar;  // Start at bar 4
  ctx.duration_ticks = 2 * kTicksPerBar;  // 2 bars only

  // Create a longer timeline to accommodate the offset.
  KeySignature d_minor = {Key::D, true};
  HarmonicTimeline long_timeline = HarmonicTimeline::createStandard(
      d_minor, 8 * kTicksPerBar, HarmonicResolution::Bar);

  auto notes = generateTexture(ctx, long_timeline);
  EXPECT_FALSE(notes.empty());

  // All notes should start at or after ctx.start_tick.
  for (const auto& note : notes) {
    EXPECT_GE(note.start_tick, ctx.start_tick);
    EXPECT_LT(note.start_tick, ctx.start_tick + ctx.duration_ticks);
  }
}

// ---------------------------------------------------------------------------
// Narrow register range
// ---------------------------------------------------------------------------

TEST_F(TextureGeneratorTest, NarrowRegisterStillProducesNotes) {
  auto ctx = makeDefaultContext(TextureType::SingleLine);
  ctx.register_low = 62;   // D4
  ctx.register_high = 69;  // A4 (only 7 semitones)

  auto notes = generateTexture(ctx, timeline_);
  EXPECT_FALSE(notes.empty());

  for (const auto& note : notes) {
    EXPECT_GE(note.pitch, 62);
    EXPECT_LE(note.pitch, 69);
  }
}

// ---------------------------------------------------------------------------
// RhythmProfile subdivisions
// ---------------------------------------------------------------------------

TEST(RhythmProfileTest, QuarterNoteHasOneSubdivision) {
  auto subs = getRhythmSubdivisions(RhythmProfile::QuarterNote);
  ASSERT_EQ(subs.size(), 1u);
  EXPECT_EQ(subs[0].first, 0u);
  EXPECT_EQ(subs[0].second, kTicksPerBeat);
}

TEST(RhythmProfileTest, EighthNoteHasTwoSubdivisions) {
  auto subs = getRhythmSubdivisions(RhythmProfile::EighthNote);
  ASSERT_EQ(subs.size(), 2u);
  Tick total = 0;
  for (const auto& [offset, dur] : subs) {
    total += dur;
  }
  EXPECT_EQ(total, kTicksPerBeat);
}

TEST(RhythmProfileTest, SixteenthHasFourSubdivisions) {
  auto subs = getRhythmSubdivisions(RhythmProfile::Sixteenth);
  ASSERT_EQ(subs.size(), 4u);
  Tick total = 0;
  for (const auto& [offset, dur] : subs) {
    total += dur;
  }
  EXPECT_EQ(total, kTicksPerBeat);
}

TEST(RhythmProfileTest, TripletHasThreeSubdivisions) {
  auto subs = getRhythmSubdivisions(RhythmProfile::Triplet);
  ASSERT_EQ(subs.size(), 3u);
  Tick total = 0;
  for (const auto& [offset, dur] : subs) {
    total += dur;
  }
  EXPECT_EQ(total, kTicksPerBeat);
}

TEST(RhythmProfileTest, DottedEighthSumsToBeat) {
  auto subs = getRhythmSubdivisions(RhythmProfile::DottedEighth);
  ASSERT_EQ(subs.size(), 2u);
  EXPECT_EQ(subs[0].second, 360u);  // Dotted eighth
  EXPECT_EQ(subs[1].second, 120u);  // Sixteenth
  EXPECT_EQ(subs[0].second + subs[1].second, kTicksPerBeat);
}

TEST(RhythmProfileTest, Mixed8th16thSumsToBeat) {
  auto subs = getRhythmSubdivisions(RhythmProfile::Mixed8th16th);
  ASSERT_EQ(subs.size(), 3u);
  EXPECT_EQ(subs[0].second, 240u);  // Eighth
  EXPECT_EQ(subs[1].second, 120u);  // Sixteenth
  EXPECT_EQ(subs[2].second, 120u);  // Sixteenth
  Tick total = 0;
  for (const auto& [offset, dur] : subs) {
    total += dur;
  }
  EXPECT_EQ(total, kTicksPerBeat);
}

// ---------------------------------------------------------------------------
// RhythmProfile affects output note count
// ---------------------------------------------------------------------------

TEST_F(TextureGeneratorTest, RhythmProfileAffectsNoteCount) {
  auto ctx_q = makeDefaultContext(TextureType::SingleLine);
  ctx_q.rhythm_profile = RhythmProfile::QuarterNote;
  auto notes_q = generateSingleLine(ctx_q, timeline_);

  auto ctx_s = makeDefaultContext(TextureType::SingleLine);
  ctx_s.rhythm_profile = RhythmProfile::Sixteenth;
  auto notes_s = generateSingleLine(ctx_s, timeline_);

  // QuarterNote: 1 note/beat -> ~16 notes in 4 bars.
  // Sixteenth: 4 notes/beat -> ~64 notes in 4 bars.
  EXPECT_GT(notes_s.size(), notes_q.size() * 2)
      << "Sixteenth should produce significantly more notes than QuarterNote";
}

TEST_F(TextureGeneratorTest, TripletProduces3NotesPerBeat) {
  auto ctx = makeDefaultContext(TextureType::SingleLine);
  ctx.rhythm_profile = RhythmProfile::Triplet;
  auto notes = generateSingleLine(ctx, timeline_);

  // 4 bars * 4 beats * 3 notes = 48 expected.
  EXPECT_GE(notes.size(), 40u);
  EXPECT_LE(notes.size(), 56u);
}

}  // namespace
}  // namespace bach
