// Tests for harmony/chord_voicer.h -- chord voicing and voice leading.

#include "harmony/chord_voicer.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>

#include "core/basic_types.h"
#include "core/interval.h"
#include "core/pitch_utils.h"
#include "harmony/chord_types.h"
#include "harmony/harmonic_event.h"

namespace bach {
namespace {

// Standard voice ranges matching prelude.cpp organ registers.
std::pair<uint8_t, uint8_t> testVoiceRange(uint8_t voice_idx) {
  switch (voice_idx) {
    case 0: return {60, 88};   // C4-E6 (Great)
    case 1: return {52, 76};   // E3-E5 (Swell)
    case 2: return {48, 64};   // C3-E4 (Positiv)
    case 3: return {24, 50};   // C1-D3 (Pedal)
    case 4: return {24, 50};   // Same as pedal
    default: return {52, 76};
  }
}

HarmonicEvent makeEvent(Key key, bool is_minor, ChordQuality quality,
                        uint8_t root_pitch, uint8_t bass_pitch = 0) {
  HarmonicEvent ev;
  ev.key = key;
  ev.is_minor = is_minor;
  ev.chord.quality = quality;
  ev.chord.root_pitch = root_pitch;
  ev.bass_pitch = (bass_pitch > 0) ? bass_pitch : root_pitch;
  return ev;
}

// ---------------------------------------------------------------------------
// voiceChord tests
// ---------------------------------------------------------------------------

TEST(ChordVoicerTest, CMajorTriad_3Voices_NoVoiceCrossing) {
  auto ev = makeEvent(Key::C, false, ChordQuality::Major, 48);  // C3 root
  auto voicing = voiceChord(ev, 3, testVoiceRange);

  EXPECT_EQ(voicing.num_voices, 3);
  // Descending order: pitches[0] >= pitches[1] >= pitches[2].
  EXPECT_GE(voicing.pitches[0], voicing.pitches[1]);
  EXPECT_GE(voicing.pitches[1], voicing.pitches[2]);
}

TEST(ChordVoicerTest, CMajorTriad_3Voices_AllChordTones) {
  auto ev = makeEvent(Key::C, false, ChordQuality::Major, 48);
  auto voicing = voiceChord(ev, 3, testVoiceRange);

  // All pitches should be chord tones (C, E, G).
  for (uint8_t i = 0; i < voicing.num_voices; ++i) {
    int pc = getPitchClass(voicing.pitches[i]);
    bool is_chord_tone = (pc == 0 || pc == 4 || pc == 7);  // C, E, G
    EXPECT_TRUE(is_chord_tone)
        << "Voice " << static_cast<int>(i) << " pitch="
        << static_cast<int>(voicing.pitches[i]) << " pc=" << pc;
  }
}

TEST(ChordVoicerTest, CMajorTriad_4Voices_AllChordTones) {
  auto ev = makeEvent(Key::C, false, ChordQuality::Major, 48);
  auto voicing = voiceChord(ev, 4, testVoiceRange);

  EXPECT_EQ(voicing.num_voices, 4);
  for (uint8_t i = 0; i < voicing.num_voices; ++i) {
    int pc = getPitchClass(voicing.pitches[i]);
    bool is_chord_tone = (pc == 0 || pc == 4 || pc == 7);
    EXPECT_TRUE(is_chord_tone)
        << "Voice " << static_cast<int>(i) << " pitch="
        << static_cast<int>(voicing.pitches[i]);
  }
}

TEST(ChordVoicerTest, MinorTriad_3Voices) {
  auto ev = makeEvent(Key::A, true, ChordQuality::Minor, 57);  // A minor
  auto voicing = voiceChord(ev, 3, testVoiceRange);

  for (uint8_t i = 0; i < voicing.num_voices; ++i) {
    int pc = getPitchClass(voicing.pitches[i]);
    bool is_chord_tone = (pc == 9 || pc == 0 || pc == 4);  // A, C, E
    EXPECT_TRUE(is_chord_tone)
        << "Voice " << static_cast<int>(i) << " pitch="
        << static_cast<int>(voicing.pitches[i]);
  }
}

TEST(ChordVoicerTest, Dominant7_4Voices_AllChordTones) {
  // G7 = G, B, D, F
  auto ev = makeEvent(Key::C, false, ChordQuality::Dominant7, 55);
  auto voicing = voiceChord(ev, 4, testVoiceRange);

  for (uint8_t i = 0; i < voicing.num_voices; ++i) {
    int pc = getPitchClass(voicing.pitches[i]);
    bool is_chord_tone = (pc == 7 || pc == 11 || pc == 2 || pc == 5);
    EXPECT_TRUE(is_chord_tone)
        << "Voice " << static_cast<int>(i) << " pitch="
        << static_cast<int>(voicing.pitches[i]);
  }
}

TEST(ChordVoicerTest, VoicesWithinRange) {
  auto ev = makeEvent(Key::C, false, ChordQuality::Major, 48);
  auto voicing = voiceChord(ev, 3, testVoiceRange);

  for (uint8_t i = 0; i < voicing.num_voices; ++i) {
    auto [lo, hi] = testVoiceRange(i);
    EXPECT_GE(voicing.pitches[i], lo)
        << "Voice " << static_cast<int>(i) << " below range";
    EXPECT_LE(voicing.pitches[i], hi)
        << "Voice " << static_cast<int>(i) << " above range";
  }
}

TEST(ChordVoicerTest, LeadingToneNotDoubled_CMajor) {
  // B (pc=11) is the leading tone in C major. With 4 voices on a G major
  // chord (G,B,D), B should not appear twice.
  auto ev = makeEvent(Key::C, false, ChordQuality::Major, 55);  // G major
  auto voicing = voiceChord(ev, 4, testVoiceRange);

  int b_count = 0;
  for (uint8_t i = 0; i < voicing.num_voices; ++i) {
    if (getPitchClass(voicing.pitches[i]) == 11) ++b_count;
  }
  EXPECT_LE(b_count, 1) << "Leading tone B should not be doubled";
}

TEST(ChordVoicerTest, DiminishedTriad_3Voices) {
  // B dim = B, D, F
  auto ev = makeEvent(Key::C, false, ChordQuality::Diminished, 59);
  auto voicing = voiceChord(ev, 3, testVoiceRange);

  for (uint8_t i = 0; i < voicing.num_voices; ++i) {
    int pc = getPitchClass(voicing.pitches[i]);
    bool is_chord_tone = (pc == 11 || pc == 2 || pc == 5);
    EXPECT_TRUE(is_chord_tone)
        << "Voice " << static_cast<int>(i) << " pitch="
        << static_cast<int>(voicing.pitches[i]);
  }
}

TEST(ChordVoicerTest, AllQualities_3Voices_NoVoiceCrossing) {
  ChordQuality qualities[] = {
      ChordQuality::Major, ChordQuality::Minor,
      ChordQuality::Diminished, ChordQuality::Augmented,
      ChordQuality::Dominant7, ChordQuality::Minor7,
      ChordQuality::MajorMajor7, ChordQuality::Diminished7,
      ChordQuality::HalfDiminished7,
  };

  for (auto q : qualities) {
    auto ev = makeEvent(Key::C, false, q, 48);
    auto voicing = voiceChord(ev, 3, testVoiceRange);

    EXPECT_GE(voicing.pitches[0], voicing.pitches[1])
        << "Voice crossing for quality " << static_cast<int>(q);
    EXPECT_GE(voicing.pitches[1], voicing.pitches[2])
        << "Voice crossing for quality " << static_cast<int>(q);
  }
}

// ---------------------------------------------------------------------------
// smoothVoiceLeading tests
// ---------------------------------------------------------------------------

TEST(SmoothVoiceLeadingTest, I_IV_MinimalMotion) {
  // C major → F major. Voices should move minimally.
  auto ev_c = makeEvent(Key::C, false, ChordQuality::Major, 48);
  auto ev_f = makeEvent(Key::C, false, ChordQuality::Major, 53);  // F major

  auto v1 = voiceChord(ev_c, 3, testVoiceRange);
  auto v2 = smoothVoiceLeading(v1, ev_f, 3, testVoiceRange);

  EXPECT_EQ(v2.num_voices, 3);
  // No voice crossing.
  EXPECT_GE(v2.pitches[0], v2.pitches[1]);
  EXPECT_GE(v2.pitches[1], v2.pitches[2]);

  // All pitches should be F major chord tones (F, A, C).
  for (uint8_t i = 0; i < v2.num_voices; ++i) {
    int pc = getPitchClass(v2.pitches[i]);
    bool is_f_chord = (pc == 5 || pc == 9 || pc == 0);
    EXPECT_TRUE(is_f_chord)
        << "Voice " << static_cast<int>(i) << " pitch="
        << static_cast<int>(v2.pitches[i]);
  }
}

TEST(SmoothVoiceLeadingTest, NoParallelFifths) {
  // I→IV→V→I progression — check no parallel perfect intervals.
  auto ev_c = makeEvent(Key::C, false, ChordQuality::Major, 48);
  auto ev_f = makeEvent(Key::C, false, ChordQuality::Major, 53);
  auto ev_g = makeEvent(Key::C, false, ChordQuality::Major, 55);

  auto v1 = voiceChord(ev_c, 3, testVoiceRange);
  auto v2 = smoothVoiceLeading(v1, ev_f, 3, testVoiceRange);
  auto v3 = smoothVoiceLeading(v2, ev_g, 3, testVoiceRange);
  auto v4 = smoothVoiceLeading(v3, ev_c, 3, testVoiceRange);

  // Check each transition for parallel P5/P8.
  auto check_no_parallel = [](const ChordVoicing& prev, const ChordVoicing& curr,
                              uint8_t nv) {
    for (uint8_t i = 0; i < nv; ++i) {
      for (uint8_t j = i + 1; j < nv; ++j) {
        int prev_iv = interval_util::compoundToSimple(
            std::abs(static_cast<int>(prev.pitches[i]) - prev.pitches[j]));
        int curr_iv = interval_util::compoundToSimple(
            std::abs(static_cast<int>(curr.pitches[i]) - curr.pitches[j]));

        if ((prev_iv == 7 && curr_iv == 7) || (prev_iv == 0 && curr_iv == 0)) {
          int mi = curr.pitches[i] - prev.pitches[i];
          int mj = curr.pitches[j] - prev.pitches[j];
          bool parallel = (mi > 0 && mj > 0) || (mi < 0 && mj < 0);
          EXPECT_FALSE(parallel)
              << "Parallel perfect between voices " << static_cast<int>(i)
              << " and " << static_cast<int>(j);
        }
      }
    }
  };

  check_no_parallel(v1, v2, 3);
  check_no_parallel(v2, v3, 3);
  check_no_parallel(v3, v4, 3);
}

TEST(SmoothVoiceLeadingTest, V7toI_LeadingToneResolvesUp) {
  // G7 → C. Leading tone B should resolve up to C.
  auto ev_g7 = makeEvent(Key::C, false, ChordQuality::Dominant7, 55);
  auto ev_c = makeEvent(Key::C, false, ChordQuality::Major, 48);

  auto v1 = voiceChord(ev_g7, 4, testVoiceRange);
  auto v2 = smoothVoiceLeading(v1, ev_c, 4, testVoiceRange);

  // Find which voice had B (pc=11) in v1.
  for (uint8_t i = 0; i < v1.num_voices; ++i) {
    if (getPitchClass(v1.pitches[i]) == 11) {
      // Should resolve to C (pc=0), one semitone up.
      int motion = static_cast<int>(v2.pitches[i]) - v1.pitches[i];
      EXPECT_EQ(getPitchClass(v2.pitches[i]), 0)
          << "Leading tone in voice " << static_cast<int>(i)
          << " should resolve to tonic";
      EXPECT_GT(motion, 0)
          << "Leading tone should resolve upward";
    }
  }
}

TEST(SmoothVoiceLeadingTest, NoVoiceCrossing_AfterLeading) {
  auto ev_g7 = makeEvent(Key::C, false, ChordQuality::Dominant7, 55);
  auto ev_c = makeEvent(Key::C, false, ChordQuality::Major, 48);

  auto v1 = voiceChord(ev_g7, 4, testVoiceRange);
  auto v2 = smoothVoiceLeading(v1, ev_c, 4, testVoiceRange);

  for (uint8_t i = 0; i + 1 < v2.num_voices; ++i) {
    EXPECT_GE(v2.pitches[i], v2.pitches[i + 1])
        << "Voice crossing between " << static_cast<int>(i)
        << " and " << static_cast<int>(i + 1);
  }
}

// ---------------------------------------------------------------------------
// getChordPitchClasses tests
// ---------------------------------------------------------------------------

TEST(GetChordPitchClassesTest, MajorTriad) {
  auto pcs = getChordPitchClasses(ChordQuality::Major, 0);  // C major
  ASSERT_EQ(pcs.size(), 3u);
  EXPECT_EQ(pcs[0], 0);  // C
  EXPECT_EQ(pcs[1], 4);  // E
  EXPECT_EQ(pcs[2], 7);  // G
}

TEST(GetChordPitchClassesTest, Dominant7) {
  auto pcs = getChordPitchClasses(ChordQuality::Dominant7, 7);  // G7
  ASSERT_EQ(pcs.size(), 4u);
  EXPECT_EQ(pcs[0], 7);   // G
  EXPECT_EQ(pcs[1], 11);  // B
  EXPECT_EQ(pcs[2], 2);   // D
  EXPECT_EQ(pcs[3], 5);   // F
}

TEST(GetChordPitchClassesTest, Diminished7) {
  auto pcs = getChordPitchClasses(ChordQuality::Diminished7, 11);  // B dim7
  ASSERT_EQ(pcs.size(), 4u);
  EXPECT_EQ(pcs[0], 11);  // B
  EXPECT_EQ(pcs[1], 2);   // D
  EXPECT_EQ(pcs[2], 5);   // F
  EXPECT_EQ(pcs[3], 8);   // Ab
}

}  // namespace
}  // namespace bach
