// Tests for the 4-phase dissonance analysis pipeline.

#include "analysis/dissonance_analyzer.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "core/basic_types.h"
#include "core/note_source.h"
#include "core/pitch_utils.h"
#include "harmony/chord_types.h"
#include "harmony/harmonic_event.h"
#include "harmony/harmonic_timeline.h"
#include "harmony/key.h"

namespace bach {
namespace {

// ===========================================================================
// Helpers
// ===========================================================================

/// @brief Create a quarter-note NoteEvent.
NoteEvent qn(Tick tick, uint8_t pitch, VoiceId voice) {
  return {tick, kTicksPerBeat, pitch, 80, voice};
}

/// @brief Create a whole-note NoteEvent.
NoteEvent wn(Tick tick, uint8_t pitch, VoiceId voice) {
  return {tick, kTicksPerBar, pitch, 80, voice};
}

/// @brief Build a simple I-V-I timeline in C major, each bar-length.
HarmonicTimeline makeCMajorTimeline(int num_bars) {
  HarmonicTimeline tl;
  for (int bar = 0; bar < num_bars; ++bar) {
    HarmonicEvent ev;
    ev.tick = static_cast<Tick>(bar) * kTicksPerBar;
    ev.end_tick = ev.tick + kTicksPerBar;
    ev.key = Key::C;
    ev.is_minor = false;
    // Alternate I and V.
    if (bar % 2 == 0) {
      ev.chord.degree = ChordDegree::I;
      ev.chord.quality = ChordQuality::Major;
      ev.chord.root_pitch = 60;  // C4
    } else {
      ev.chord.degree = ChordDegree::V;
      ev.chord.quality = ChordQuality::Major;
      ev.chord.root_pitch = 67;  // G4
    }
    tl.addEvent(ev);
  }
  return tl;
}

// ===========================================================================
// Phase 1: Simultaneous Clashes
// ===========================================================================

TEST(DissonancePhase1, CleanConsonance_ZeroDetections) {
  // Two voices in parallel 3rds -- no clashes.
  std::vector<NoteEvent> notes = {
      qn(0, 60, 0),               // C4
      qn(kTicksPerBeat, 62, 0),   // D4
      qn(0, 64, 1),               // E4 (M3 above C)
      qn(kTicksPerBeat, 65, 1),   // F4 (m3 above D)
  };
  auto result = detectSimultaneousClashes(notes, 2);
  EXPECT_EQ(result.size(), 0u);
}

TEST(DissonancePhase1, MinorSecondClash_Detected) {
  // Voice 0: C4, Voice 1: C#4 -- minor 2nd clash.
  std::vector<NoteEvent> notes = {
      qn(0, 60, 0),
      qn(0, 61, 1),
  };
  auto result = detectSimultaneousClashes(notes, 2);
  EXPECT_GE(result.size(), 1u);
  EXPECT_EQ(result[0].type, DissonanceType::SimultaneousClash);
  EXPECT_EQ(result[0].interval, 1);
}

TEST(DissonancePhase1, TritoneClash_Detected) {
  // Voice 0: C4, Voice 1: F#4 -- tritone.
  std::vector<NoteEvent> notes = {
      qn(0, 60, 0),
      qn(0, 66, 1),
  };
  auto result = detectSimultaneousClashes(notes, 2);
  EXPECT_GE(result.size(), 1u);
  EXPECT_EQ(result[0].interval, 6);
}

TEST(DissonancePhase1, StrongBeat_HighSeverity) {
  // Clash on beat 0 (strong) -- should be High.
  std::vector<NoteEvent> notes = {
      qn(0, 60, 0),
      qn(0, 61, 1),
  };
  auto result = detectSimultaneousClashes(notes, 2);
  ASSERT_GE(result.size(), 1u);
  EXPECT_EQ(result[0].severity, DissonanceSeverity::High);
}

TEST(DissonancePhase1, WeakBeat_MediumSeverity) {
  // Clash on beat 1 (weak) -- should be Medium.
  std::vector<NoteEvent> notes = {
      qn(kTicksPerBeat, 60, 0),
      qn(kTicksPerBeat, 61, 1),
  };
  auto result = detectSimultaneousClashes(notes, 2);
  ASSERT_GE(result.size(), 1u);
  EXPECT_EQ(result[0].severity, DissonanceSeverity::Medium);
}

TEST(DissonancePhase1, ThreeVoices_AllPairsChecked) {
  // All three voices clashing: C4, C#4, D4.
  std::vector<NoteEvent> notes = {
      qn(0, 60, 0),
      qn(0, 61, 1),
      qn(0, 62, 2),
  };
  auto result = detectSimultaneousClashes(notes, 3);
  // Pairs: (0,1)=m2, (0,2)=M2, (1,2)=m2 -- all dissonant.
  EXPECT_EQ(result.size(), 3u);
}

TEST(DissonancePhase1, SingleVoice_NoClashes) {
  std::vector<NoteEvent> notes = {
      qn(0, 60, 0),
      qn(kTicksPerBeat, 62, 0),
  };
  auto result = detectSimultaneousClashes(notes, 1);
  EXPECT_EQ(result.size(), 0u);
}

TEST(DissonancePhase1, EmptyInput_NoClashes) {
  std::vector<NoteEvent> notes;
  auto result = detectSimultaneousClashes(notes, 2);
  EXPECT_EQ(result.size(), 0u);
}

TEST(DissonancePhase1, PerfectFifth_NoClash) {
  // C4 + G4 = perfect 5th, consonant.
  std::vector<NoteEvent> notes = {
      qn(0, 60, 0),
      qn(0, 67, 1),
  };
  auto result = detectSimultaneousClashes(notes, 2);
  EXPECT_EQ(result.size(), 0u);
}

TEST(DissonancePhase1, PerfectFourth_Dissonant) {
  // C4 + F4 = perfect 4th, classified as dissonant.
  std::vector<NoteEvent> notes = {
      qn(0, 60, 0),
      qn(0, 65, 1),
  };
  auto result = detectSimultaneousClashes(notes, 2);
  EXPECT_GE(result.size(), 1u);
}

// ===========================================================================
// Phase 2: Non-Chord Tones
// ===========================================================================

TEST(DissonancePhase2, ChordTone_ZeroDetections) {
  auto tl = makeCMajorTimeline(2);
  // C major chord tones: C, E, G.
  std::vector<NoteEvent> notes = {
      qn(0, 60, 0),  // C4 -- chord tone of I
      qn(0, 64, 0),  // E4
      qn(0, 67, 0),  // G4
  };
  auto result = detectNonChordTones(notes, tl);
  EXPECT_EQ(result.size(), 0u);
}

TEST(DissonancePhase2, NonChordTone_Detected) {
  auto tl = makeCMajorTimeline(2);
  // D4 is not in C major triad (C, E, G).
  std::vector<NoteEvent> notes = {
      qn(0, 62, 0),  // D4
  };
  auto result = detectNonChordTones(notes, tl);
  EXPECT_GE(result.size(), 1u);
  EXPECT_EQ(result[0].type, DissonanceType::NonChordTone);
}

TEST(DissonancePhase2, StrongBeatNonChord_HighSeverity) {
  auto tl = makeCMajorTimeline(2);
  // F4 on beat 0 -- not in C major triad, strong beat.
  std::vector<NoteEvent> notes = {
      qn(0, 65, 0),  // F4
  };
  auto result = detectNonChordTones(notes, tl);
  ASSERT_GE(result.size(), 1u);
  EXPECT_EQ(result[0].severity, DissonanceSeverity::High);
}

TEST(DissonancePhase2, WeakBeatNonChord_MediumSeverity) {
  auto tl = makeCMajorTimeline(2);
  // F4 on beat 1 -- not in C major triad, weak beat.
  std::vector<NoteEvent> notes = {
      qn(kTicksPerBeat, 65, 0),  // F4
  };
  auto result = detectNonChordTones(notes, tl);
  ASSERT_GE(result.size(), 1u);
  EXPECT_EQ(result[0].severity, DissonanceSeverity::Medium);
}

TEST(DissonancePhase2, PassingTone_LowSeverity) {
  auto tl = makeCMajorTimeline(2);
  // C4 -> D4 -> E4 stepwise: D4 is a passing tone.
  std::vector<NoteEvent> notes = {
      qn(kTicksPerBeat * 0, 60, 0),  // C4 (chord tone)
      qn(kTicksPerBeat * 1, 62, 0),  // D4 (passing tone, weak beat)
      qn(kTicksPerBeat * 2, 64, 0),  // E4 (chord tone)
  };
  auto result = detectNonChordTones(notes, tl);
  ASSERT_GE(result.size(), 1u);
  // D4 should be detected but with Low severity (stepwise passing).
  bool found_low_d = false;
  for (const auto& ev : result) {
    if (ev.pitch == 62 && ev.severity == DissonanceSeverity::Low) {
      found_low_d = true;
    }
  }
  EXPECT_TRUE(found_low_d);
}

TEST(DissonancePhase2, EmptyTimeline_NoDetections) {
  HarmonicTimeline tl;
  std::vector<NoteEvent> notes = {qn(0, 62, 0)};
  auto result = detectNonChordTones(notes, tl);
  EXPECT_EQ(result.size(), 0u);
}

TEST(DissonancePhase2, VChord_ChordTones) {
  auto tl = makeCMajorTimeline(2);
  // In bar 1 (V chord): G, B, D are chord tones.
  std::vector<NoteEvent> notes = {
      qn(kTicksPerBar, 67, 0),      // G4
      qn(kTicksPerBar, 71, 1),      // B4
      qn(kTicksPerBar, 62, 2),      // D4
  };
  auto result = detectNonChordTones(notes, tl);
  EXPECT_EQ(result.size(), 0u);
}

// ===========================================================================
// Phase 3: Sustained Over Chord Change
// ===========================================================================

TEST(DissonancePhase3, NoSustainedNotes_ZeroDetections) {
  auto tl = makeCMajorTimeline(2);
  // Notes don't cross the bar 0->1 boundary.
  std::vector<NoteEvent> notes = {
      qn(0, 60, 0),            // C4 in bar 0 only
      qn(kTicksPerBar, 67, 0), // G4 in bar 1 only
  };
  auto result = detectSustainedOverChordChange(notes, 1, tl);
  EXPECT_EQ(result.size(), 0u);
}

TEST(DissonancePhase3, SustainedChordTone_ZeroDetections) {
  auto tl = makeCMajorTimeline(2);
  // G4 sustained from bar 0 into bar 1: G is chord tone of both I and V.
  std::vector<NoteEvent> notes = {
      wn(0, 67, 0),  // G4, lasts entire bar 0 into bar 1.
  };
  // Actually wn = 1 bar exactly, so it ends at bar boundary. Make it longer.
  notes[0].duration = kTicksPerBar + kTicksPerBeat;  // Extends into bar 1.
  auto result = detectSustainedOverChordChange(notes, 1, tl);
  // G is in V chord (G major) -- should be fine.
  EXPECT_EQ(result.size(), 0u);
}

TEST(DissonancePhase3, SustainedClash_Detected) {
  auto tl = makeCMajorTimeline(2);
  // E4 sustained from bar 0 into bar 1: E is NOT a chord tone of V (G, B, D).
  NoteEvent note = {0, kTicksPerBar + kTicksPerBeat, 64, 80, 0};  // E4
  std::vector<NoteEvent> notes = {note};
  auto result = detectSustainedOverChordChange(notes, 1, tl);
  EXPECT_GE(result.size(), 1u);
  EXPECT_EQ(result[0].type, DissonanceType::SustainedOverChordChange);
}

TEST(DissonancePhase3, SingleEvent_NoDetections) {
  // Need at least 2 timeline events for a chord change.
  HarmonicTimeline tl;
  HarmonicEvent ev;
  ev.tick = 0;
  ev.end_tick = kTicksPerBar * 4;
  ev.chord.degree = ChordDegree::I;
  ev.chord.quality = ChordQuality::Major;
  ev.chord.root_pitch = 60;
  tl.addEvent(ev);

  std::vector<NoteEvent> notes = {
      {0, kTicksPerBar * 4, 62, 80, 0},
  };
  auto result = detectSustainedOverChordChange(notes, 1, tl);
  EXPECT_EQ(result.size(), 0u);
}

// ===========================================================================
// Phase 4: Non-Diatonic Notes
// ===========================================================================

TEST(DissonancePhase4, DiatonicNotes_ZeroDetections) {
  KeySignature ks = {Key::C, false};
  // All C major diatonic: C D E F G A B
  std::vector<NoteEvent> notes = {
      qn(0, 60, 0),               // C4
      qn(kTicksPerBeat, 62, 0),   // D4
      qn(kTicksPerBeat * 2, 64, 0),  // E4
      qn(kTicksPerBeat * 3, 65, 0),  // F4
  };
  auto result = detectNonDiatonicNotes(notes, ks);
  EXPECT_EQ(result.size(), 0u);
}

TEST(DissonancePhase4, Chromatic_Detected) {
  KeySignature ks = {Key::C, false};
  // C#4 is not in C major.
  std::vector<NoteEvent> notes = {
      qn(0, 61, 0),  // C#4
  };
  auto result = detectNonDiatonicNotes(notes, ks);
  EXPECT_GE(result.size(), 1u);
  EXPECT_EQ(result[0].type, DissonanceType::NonDiatonicNote);
}

TEST(DissonancePhase4, ChromaticPassingTone_LowSeverity) {
  KeySignature ks = {Key::C, false};
  // C4 -> C#4 -> D4 stepwise chromatic passing, weak beat.
  std::vector<NoteEvent> notes = {
      qn(kTicksPerBeat * 0, 60, 0),  // C4 (diatonic)
      qn(kTicksPerBeat * 1, 61, 0),  // C#4 (chromatic, weak beat)
      qn(kTicksPerBeat * 2, 62, 0),  // D4 (diatonic)
  };
  auto result = detectNonDiatonicNotes(notes, ks);
  ASSERT_GE(result.size(), 1u);
  EXPECT_EQ(result[0].severity, DissonanceSeverity::Low);
}

TEST(DissonancePhase4, ChromaticStrongBeat_HighSeverity) {
  KeySignature ks = {Key::C, false};
  // F#4 on beat 0 (strong) with no passing context.
  std::vector<NoteEvent> notes = {
      qn(0, 66, 0),  // F#4
  };
  auto result = detectNonDiatonicNotes(notes, ks);
  ASSERT_GE(result.size(), 1u);
  EXPECT_EQ(result[0].severity, DissonanceSeverity::High);
}

TEST(DissonancePhase4, MinorKey_DiatonicCheck) {
  KeySignature ks = {Key::A, true};  // A minor: A B C D E F G
  std::vector<NoteEvent> notes = {
      qn(0, 57, 0),   // A3 (diatonic in A minor)
      qn(kTicksPerBeat, 59, 0),  // B3
      qn(kTicksPerBeat * 2, 60, 0),  // C4
  };
  auto result = detectNonDiatonicNotes(notes, ks);
  EXPECT_EQ(result.size(), 0u);
}

TEST(DissonancePhase4, MinorKey_ChromaticDetected) {
  KeySignature ks = {Key::A, true};  // A minor
  // D#3 (MIDI 51) is not in any A minor scale form (natural/harmonic/melodic).
  std::vector<NoteEvent> notes = {
      qn(0, 51, 0),  // D#3
  };
  auto result = detectNonDiatonicNotes(notes, ks);
  EXPECT_GE(result.size(), 1u);
}

TEST(DissonancePhase4, EmptyInput_NoDetections) {
  KeySignature ks = {Key::C, false};
  std::vector<NoteEvent> notes;
  auto result = detectNonDiatonicNotes(notes, ks);
  EXPECT_EQ(result.size(), 0u);
}

TEST(DissonancePhase4, DMinorDiatonic) {
  KeySignature ks = {Key::D, true};  // D minor: D E F G A Bb C
  std::vector<NoteEvent> notes = {
      qn(0, 62, 0),   // D4
      qn(kTicksPerBeat, 64, 0),   // E4
      qn(kTicksPerBeat * 2, 65, 0),  // F4
      qn(kTicksPerBeat * 3, 67, 0),  // G4
  };
  auto result = detectNonDiatonicNotes(notes, ks);
  EXPECT_EQ(result.size(), 0u);
}

// ===========================================================================
// Orchestrators
// ===========================================================================

TEST(DissonanceOrgan, RunsAllFourPhases) {
  auto tl = makeCMajorTimeline(2);
  KeySignature ks = {Key::C, false};
  std::vector<NoteEvent> notes = {
      qn(0, 60, 0),  // C4 (clean)
      qn(0, 61, 1),  // C#4 (clash + non-chord + non-diatonic)
  };
  auto result = analyzeOrganDissonance(notes, 2, tl, ks);
  EXPECT_GT(result.summary.total, 0u);
  // Should have at least: 1 SimultaneousClash, 1 NonChordTone, 1 NonDiatonicNote.
  EXPECT_GE(result.summary.simultaneous_clash_count, 1u);
  EXPECT_GE(result.summary.non_chord_tone_count, 1u);
  EXPECT_GE(result.summary.non_diatonic_note_count, 1u);
}

TEST(DissonanceSoloString, RunsPhases2And4Only) {
  auto tl = makeCMajorTimeline(2);
  KeySignature ks = {Key::C, false};
  // F#4 is non-chord-tone + non-diatonic.
  std::vector<NoteEvent> notes = {
      qn(0, 66, 0),  // F#4
  };
  auto result = analyzeSoloStringDissonance(notes, tl, ks);
  EXPECT_GT(result.summary.total, 0u);
  // Solo String should NOT have SimultaneousClash or SustainedOverChordChange.
  EXPECT_EQ(result.summary.simultaneous_clash_count, 0u);
  EXPECT_EQ(result.summary.sustained_over_chord_change_count, 0u);
  // Should have NonChordTone + NonDiatonicNote.
  EXPECT_GE(result.summary.non_chord_tone_count, 1u);
  EXPECT_GE(result.summary.non_diatonic_note_count, 1u);
}

TEST(DissonanceOrgan, CleanInput_ZeroEvents) {
  auto tl = makeCMajorTimeline(2);
  KeySignature ks = {Key::C, false};
  // Perfect 5ths, all chord tones, all diatonic.
  std::vector<NoteEvent> notes = {
      qn(0, 60, 0),  // C4
      qn(0, 67, 1),  // G4
  };
  auto result = analyzeOrganDissonance(notes, 2, tl, ks);
  EXPECT_EQ(result.summary.total, 0u);
}

// ===========================================================================
// Summary and density
// ===========================================================================

TEST(DissonanceSummary, DensityCalculation) {
  auto tl = makeCMajorTimeline(4);
  KeySignature ks = {Key::C, false};
  // Create 4 bars of notes with some non-diatonic.
  std::vector<NoteEvent> notes;
  for (int bar = 0; bar < 4; ++bar) {
    // One non-diatonic note per bar.
    notes.push_back(qn(static_cast<Tick>(bar) * kTicksPerBar, 61, 0));  // C#4
  }
  auto result = analyzeSoloStringDissonance(notes, tl, ks);
  // Each C#4 produces at least NonDiatonicNote and possibly NonChordTone.
  EXPECT_GT(result.summary.total, 0u);
  EXPECT_GT(result.summary.density_per_beat, 0.0f);
}

// ===========================================================================
// Serialization
// ===========================================================================

TEST(DissonanceJson, ValidJsonStructure) {
  auto tl = makeCMajorTimeline(1);
  KeySignature ks = {Key::C, false};
  std::vector<NoteEvent> notes = {
      qn(0, 61, 0),  // C#4
  };
  auto result = analyzeSoloStringDissonance(notes, tl, ks);
  std::string json = result.toJson();
  // Basic structure checks.
  EXPECT_NE(json.find("\"summary\""), std::string::npos);
  EXPECT_NE(json.find("\"events\""), std::string::npos);
  EXPECT_NE(json.find("\"total\""), std::string::npos);
  EXPECT_NE(json.find("\"high\""), std::string::npos);
  EXPECT_NE(json.find("\"density_per_beat\""), std::string::npos);
}

TEST(DissonanceTextSummary, ContainsKey) {
  auto tl = makeCMajorTimeline(1);
  KeySignature ks = {Key::C, false};
  std::vector<NoteEvent> notes = {qn(0, 61, 0)};
  auto result = analyzeSoloStringDissonance(notes, tl, ks);
  std::string text = result.toTextSummary("Solo String", 1);
  EXPECT_NE(text.find("=== Dissonance Analysis ==="), std::string::npos);
  EXPECT_NE(text.find("Solo String"), std::string::npos);
  EXPECT_NE(text.find("Total:"), std::string::npos);
}

// ===========================================================================
// Severity escalation / downgrade
// ===========================================================================

TEST(DissonanceSeverity, WideRegisterClash_LowSeverity) {
  // Voices 3+ octaves apart: C1 + C#4 -- should downgrade to Low.
  std::vector<NoteEvent> notes = {
      qn(0, 24, 0),  // C1
      qn(0, 61, 1),  // C#4 (37 semitones apart)
  };
  auto result = detectSimultaneousClashes(notes, 2);
  ASSERT_GE(result.size(), 1u);
  EXPECT_EQ(result[0].severity, DissonanceSeverity::Low);
}

TEST(DissonanceBarBeat, BarAndBeatCorrect) {
  // Note at bar 2, beat 3.
  Tick tick = kTicksPerBar * 2 + kTicksPerBeat * 2;  // Bar 2 (0-based), beat 2 (0-based)
  std::vector<NoteEvent> notes = {
      qn(tick, 61, 0),
  };
  KeySignature ks = {Key::C, false};
  auto result = detectNonDiatonicNotes(notes, ks);
  ASSERT_GE(result.size(), 1u);
  EXPECT_EQ(result[0].bar, 3u);   // 1-based
  EXPECT_EQ(result[0].beat, 3u);  // 1-based
}

// ===========================================================================
// A4: Perfect 4th upper voice skip
// ===========================================================================

TEST(DissonancePhase1, P4UpperVoices_Skipped) {
  // 3 voices: bass C3, upper E4, upper A4 (P4 between E4-A4).
  // P4 between upper voices should NOT be flagged.
  std::vector<NoteEvent> notes = {
      qn(0, 48, 0),  // C3 (bass)
      qn(0, 64, 1),  // E4 (upper)
      qn(0, 69, 2),  // A4 (upper, P4 above E4)
  };
  auto result = detectSimultaneousClashes(notes, 3);
  // Check that no P4 clash is reported between voices 1 and 2.
  for (const auto& ev : result) {
    if (ev.voice_a == 1 && ev.voice_b == 2) {
      // If a clash between voices 1 and 2 is reported, its interval
      // should not be a P4 (5 semitones).
      EXPECT_NE(ev.interval % 12, 5)
          << "P4 between upper voices should not be flagged";
    }
  }
}

TEST(DissonancePhase1, P4WithBass_StillDetected) {
  // 2 voices: C4 and F4 (P4 involving the bass).
  std::vector<NoteEvent> notes = {
      qn(0, 60, 0),  // C4 (bass)
      qn(0, 65, 1),  // F4 (P4 above bass)
  };
  auto result = detectSimultaneousClashes(notes, 2);
  EXPECT_GE(result.size(), 1u);
}

// ===========================================================================
// A5: Suspension recognition
// ===========================================================================

TEST(DissonancePhase3, Suspension_DowngradeToLow) {
  // Set up: I chord -> V chord boundary.
  // Voice holds E4 (chord tone of I=C major) across boundary into V (G major).
  // E4 is NOT a chord tone of G major triad (G, B, D).
  // Next note in same voice: D4 (chord tone of V) = descending step resolution.
  HarmonicTimeline tl;
  HarmonicEvent ev1;
  ev1.tick = 0;
  ev1.end_tick = kTicksPerBar;
  ev1.key = Key::C;
  ev1.chord.degree = ChordDegree::I;
  ev1.chord.quality = ChordQuality::Major;
  ev1.chord.root_pitch = 60;
  tl.addEvent(ev1);

  HarmonicEvent ev2;
  ev2.tick = kTicksPerBar;
  ev2.end_tick = kTicksPerBar * 2;
  ev2.key = Key::C;
  ev2.chord.degree = ChordDegree::V;
  ev2.chord.quality = ChordQuality::Major;
  ev2.chord.root_pitch = 67;
  tl.addEvent(ev2);

  // E4 held across boundary, then resolves to D4.
  NoteEvent held = {0, kTicksPerBar + kTicksPerBeat, 64, 80, 0};  // E4
  NoteEvent resolution = {kTicksPerBar + kTicksPerBeat, kTicksPerBeat, 62, 80, 0};  // D4
  std::vector<NoteEvent> notes = {held, resolution};

  auto result = detectSustainedOverChordChange(notes, 1, tl);
  ASSERT_GE(result.size(), 1u);
  // The suspension should be downgraded to Low.
  EXPECT_EQ(result[0].severity, DissonanceSeverity::Low);
}

TEST(DissonancePhase3, NonSuspension_NotDowngraded) {
  // Held note was NOT a chord tone in the previous chord -> not a suspension.
  HarmonicTimeline tl;
  HarmonicEvent ev1;
  ev1.tick = 0;
  ev1.end_tick = kTicksPerBar;
  ev1.key = Key::C;
  ev1.chord.degree = ChordDegree::I;
  ev1.chord.quality = ChordQuality::Major;
  ev1.chord.root_pitch = 60;
  tl.addEvent(ev1);

  HarmonicEvent ev2;
  ev2.tick = kTicksPerBar;
  ev2.end_tick = kTicksPerBar * 2;
  ev2.key = Key::C;
  ev2.chord.degree = ChordDegree::V;
  ev2.chord.quality = ChordQuality::Major;
  ev2.chord.root_pitch = 67;
  tl.addEvent(ev2);

  // F4 is NOT a chord tone of I chord, so no preparation -> not a suspension.
  NoteEvent held = {0, kTicksPerBar + kTicksPerBeat, 65, 80, 0};  // F4
  NoteEvent next = {kTicksPerBar + kTicksPerBeat, kTicksPerBeat, 64, 80, 0};  // E4
  std::vector<NoteEvent> notes = {held, next};

  auto result = detectSustainedOverChordChange(notes, 1, tl);
  ASSERT_GE(result.size(), 1u);
  // Should NOT be downgraded (not a valid suspension pattern).
  EXPECT_NE(result[0].severity, DissonanceSeverity::Low);
}

// ===========================================================================
// A6: Neighbor tone recognition
// ===========================================================================

TEST(DissonancePhase2, NeighborTone_LowSeverity) {
  auto tl = makeCMajorTimeline(2);
  // C4 -> D4 -> C4: D4 is a neighbor tone (step away and return).
  std::vector<NoteEvent> notes = {
      qn(kTicksPerBeat * 0, 60, 0),  // C4 (chord tone)
      qn(kTicksPerBeat * 1, 62, 0),  // D4 (neighbor tone, weak beat)
      qn(kTicksPerBeat * 2, 60, 0),  // C4 (returns to original)
  };
  auto result = detectNonChordTones(notes, tl);
  // D4 should be detected but with Low severity (neighbor tone).
  bool found_low_d = false;
  for (const auto& ev : result) {
    if (ev.pitch == 62 && ev.severity == DissonanceSeverity::Low) {
      found_low_d = true;
    }
  }
  EXPECT_TRUE(found_low_d) << "Neighbor tone D4 should be Low severity";
}

TEST(DissonancePhase4, MinorKey_LeadingToneNowDiatonic) {
  // With the scale union fix, G# in A minor should now be diatonic
  // (it's in harmonic/melodic minor).
  KeySignature ks = {Key::A, true};
  std::vector<NoteEvent> notes = {
      qn(0, 56, 0),  // G#3 = raised 7th in A minor
  };
  auto result = detectNonDiatonicNotes(notes, ks);
  EXPECT_EQ(result.size(), 0u) << "G# should be diatonic in A minor (harmonic/melodic)";
}

// ===========================================================================
// Phase A-2: Weighted density
// ===========================================================================

TEST(WeightedDensity, HighOnly) {
  // 4 high-severity events over 4 beats -> weighted = 4/4 = 1.0.
  auto tl = makeCMajorTimeline(1);
  KeySignature ks = {Key::C, false};
  // 4 non-diatonic notes on strong beats (High severity).
  std::vector<NoteEvent> notes = {
      qn(0, 61, 0),                  // C#4 (strong beat -> High)
      qn(kTicksPerBeat * 2, 66, 0),  // F#4 (strong beat -> High)
  };
  auto result = analyzeSoloStringDissonance(notes, tl, ks);
  // All events should be High (strong beat, non-diatonic + non-chord-tone).
  EXPECT_GT(result.summary.weighted_density_per_beat, 0.0f);
  // Weighted >= raw since High=1.0 and these are all High.
  EXPECT_GE(result.summary.weighted_density_per_beat, result.summary.density_per_beat * 0.5f);
}

TEST(WeightedDensity, MixedSeverity) {
  // Mix of high and low severity events.
  auto tl = makeCMajorTimeline(2);
  KeySignature ks = {Key::C, false};
  // Strong beat non-diatonic (High) + weak beat passing tone (Low).
  std::vector<NoteEvent> notes = {
      qn(0, 66, 0),                  // F#4 on beat 0 (High)
      qn(kTicksPerBeat * 0, 60, 1),  // C4 (chord tone, no event)
      qn(kTicksPerBeat * 1, 62, 1),  // D4 (passing, Low)
      qn(kTicksPerBeat * 2, 64, 1),  // E4 (chord tone, no event)
  };
  auto result = analyzeSoloStringDissonance(notes, tl, ks);
  // Weighted should be less than raw since Low events contribute 0.
  EXPECT_LT(result.summary.weighted_density_per_beat, result.summary.density_per_beat);
}

TEST(WeightedDensity, AllLow) {
  auto tl = makeCMajorTimeline(2);
  KeySignature ks = {Key::C, false};
  // Passing tone on weak beat -> Low severity for both NCT and NonDiatonic.
  // Use beats 1-3 (all weak) to avoid strong-beat High severity.
  std::vector<NoteEvent> notes = {
      qn(kTicksPerBeat * 1, 60, 0),  // C4 (chord tone)
      qn(kTicksPerBeat * 3, 61, 0),  // C#4 (weak beat, passing tone between C4 and D4)
      qn(kTicksPerBar + kTicksPerBeat * 1, 62, 0),  // D4 (weak beat, continuation)
  };
  auto result = analyzeSoloStringDissonance(notes, tl, ks);
  // All non-chord/non-diatonic events should be Low severity.
  for (const auto& ev : result.events) {
    EXPECT_NE(ev.severity, DissonanceSeverity::High)
        << "All events should be Low or Medium, got High at tick " << ev.tick;
  }
  EXPECT_LT(result.summary.weighted_density_per_beat, result.summary.density_per_beat);
}

// ===========================================================================
// Phase B: Escape tone and anticipation detection
// ===========================================================================

TEST(DissonancePhase2, EscapeTone_StepLeap_DowngradedToLow) {
  auto tl = makeCMajorTimeline(2);
  // C4 -> D4 -> G4: D4 is escape tone (step approach, leap departure).
  std::vector<NoteEvent> notes = {
      qn(kTicksPerBeat * 0, 60, 0),  // C4 (chord tone)
      qn(kTicksPerBeat * 1, 62, 0),  // D4 (escape tone, weak beat)
      qn(kTicksPerBeat * 2, 67, 0),  // G4 (chord tone, leap from D4)
  };
  auto result = detectNonChordTones(notes, tl);
  // D4 should be detected as escape tone with Low severity.
  bool found_low_d = false;
  for (const auto& ev : result) {
    if (ev.pitch == 62 && ev.severity == DissonanceSeverity::Low) {
      found_low_d = true;
    }
  }
  EXPECT_TRUE(found_low_d) << "Escape tone D4 should be Low severity";
}

TEST(DissonancePhase2, NonEscapeTone_LeapLeap_StaysMedium) {
  auto tl = makeCMajorTimeline(2);
  // G4 -> D4 -> A3: D4 approached by leap, left by leap -- NOT escape tone.
  // D4 is not a chord tone of C major (I chord).
  std::vector<NoteEvent> notes = {
      qn(kTicksPerBeat * 0, 67, 0),  // G4 (chord tone)
      qn(kTicksPerBeat * 1, 62, 0),  // D4 (leap approach, leap departure)
      qn(kTicksPerBeat * 2, 57, 0),  // A3 (non-chord-tone)
  };
  auto result = detectNonChordTones(notes, tl);
  // D4 should NOT be downgraded (leap-leap is not an escape tone pattern).
  bool found_medium_d = false;
  for (const auto& ev : result) {
    if (ev.pitch == 62 && ev.severity == DissonanceSeverity::Medium) {
      found_medium_d = true;
    }
  }
  EXPECT_TRUE(found_medium_d) << "Leap-leap D4 should remain Medium severity";
}

TEST(DissonancePhase2, Anticipation_ChordToneOfNextChord_DowngradedToLow) {
  // Build a 2-bar timeline: bar 0 = I (C major), bar 1 = V (G major).
  auto tl = makeCMajorTimeline(2);
  // D4 on beat 3 of bar 0: D is NOT a chord tone of I (C, E, G)
  // but IS a chord tone of V (G, B, D) which comes next.
  std::vector<NoteEvent> notes = {
      qn(kTicksPerBeat * 3, 62, 0),  // D4 (anticipation of V chord)
  };
  auto result = detectNonChordTones(notes, tl);
  // D4 should be downgraded to Low as anticipation.
  bool found_low_d = false;
  for (const auto& ev : result) {
    if (ev.pitch == 62 && ev.severity == DissonanceSeverity::Low) {
      found_low_d = true;
    }
  }
  EXPECT_TRUE(found_low_d) << "Anticipation D4 should be Low severity";
}

TEST(DissonancePhase2, Appoggiatura_StrongBeatResolvesToChordTone_DowngradedToLow) {
  auto tl = makeCMajorTimeline(2);
  // D4 on beat 0 (strong beat): not a chord tone of I (C, E, G).
  // Resolves by step down to C4 (chord tone) = appoggiatura.
  std::vector<NoteEvent> notes = {
      qn(0, 62, 0),               // D4 (appoggiatura, strong beat)
      qn(kTicksPerBeat, 60, 0),   // C4 (resolution, chord tone)
  };
  auto result = detectNonChordTones(notes, tl);
  // D4 should be downgraded from High to Low (recognized appoggiatura).
  bool found_low_d = false;
  for (const auto& ev : result) {
    if (ev.pitch == 62 && ev.severity == DissonanceSeverity::Low) {
      found_low_d = true;
    }
  }
  EXPECT_TRUE(found_low_d) << "Appoggiatura D4 should be Low severity";
}

TEST(DissonancePhase2, NonAppoggiatura_StrongBeatLeapResolution_StaysHigh) {
  auto tl = makeCMajorTimeline(2);
  // D4 on beat 0 (strong beat): not a chord tone.
  // Followed by G4 (leap, not stepwise) = NOT appoggiatura.
  std::vector<NoteEvent> notes = {
      qn(0, 62, 0),               // D4 (strong beat)
      qn(kTicksPerBeat, 67, 0),   // G4 (leap resolution)
  };
  auto result = detectNonChordTones(notes, tl);
  bool found_high_d = false;
  for (const auto& ev : result) {
    if (ev.pitch == 62 && ev.severity == DissonanceSeverity::High) {
      found_high_d = true;
    }
  }
  EXPECT_TRUE(found_high_d) << "Non-appoggiatura D4 should remain High severity";
}

// ===========================================================================
// Phase A-1: Bar-resolution timeline verification
// ===========================================================================

TEST(WeightedDensity, JsonContainsWeightedField) {
  auto tl = makeCMajorTimeline(1);
  KeySignature ks = {Key::C, false};
  std::vector<NoteEvent> notes = {qn(0, 61, 0)};
  auto result = analyzeSoloStringDissonance(notes, tl, ks);
  std::string json = result.toJson();
  EXPECT_NE(json.find("\"weighted_density_per_beat\""), std::string::npos);
}

TEST(WeightedDensity, TextContainsWeightedField) {
  auto tl = makeCMajorTimeline(1);
  KeySignature ks = {Key::C, false};
  std::vector<NoteEvent> notes = {qn(0, 61, 0)};
  auto result = analyzeSoloStringDissonance(notes, tl, ks);
  std::string text = result.toTextSummary("Solo String", 1);
  EXPECT_NE(text.find("weighted:"), std::string::npos);
}

// ===========================================================================
// Helper for sourced notes
// ===========================================================================

/// @brief Create a quarter-note with a specific BachNoteSource.
NoteEvent qn_src(Tick tick, uint8_t pitch, VoiceId voice, BachNoteSource src) {
  NoteEvent ev = {tick, kTicksPerBeat, pitch, 80, voice};
  ev.source = src;
  return ev;
}

/// @brief Build a beat-resolution timeline: each beat gets its own chord.
/// Bar 0: beat 0 = I, beat 1 = vi, beat 2 = IV, beat 3 = V.
/// Bar 1: same pattern.
HarmonicTimeline makeBeatResolutionTimeline(int num_bars) {
  HarmonicTimeline tl;
  for (int bar = 0; bar < num_bars; ++bar) {
    Tick bar_start = static_cast<Tick>(bar) * kTicksPerBar;

    // Beat 0: I (C, E, G)
    {
      HarmonicEvent ev;
      ev.tick = bar_start;
      ev.end_tick = bar_start + kTicksPerBeat;
      ev.key = Key::C;
      ev.is_minor = false;
      ev.chord.degree = ChordDegree::I;
      ev.chord.quality = ChordQuality::Major;
      ev.chord.root_pitch = 60;
      tl.addEvent(ev);
    }
    // Beat 1: vi (A, C, E)
    {
      HarmonicEvent ev;
      ev.tick = bar_start + kTicksPerBeat;
      ev.end_tick = bar_start + kTicksPerBeat * 2;
      ev.key = Key::C;
      ev.is_minor = false;
      ev.chord.degree = ChordDegree::vi;
      ev.chord.quality = ChordQuality::Minor;
      ev.chord.root_pitch = 69;
      tl.addEvent(ev);
    }
    // Beat 2: IV (F, A, C)
    {
      HarmonicEvent ev;
      ev.tick = bar_start + kTicksPerBeat * 2;
      ev.end_tick = bar_start + kTicksPerBeat * 3;
      ev.key = Key::C;
      ev.is_minor = false;
      ev.chord.degree = ChordDegree::IV;
      ev.chord.quality = ChordQuality::Major;
      ev.chord.root_pitch = 65;
      tl.addEvent(ev);
    }
    // Beat 3: V (G, B, D)
    {
      HarmonicEvent ev;
      ev.tick = bar_start + kTicksPerBeat * 3;
      ev.end_tick = bar_start + kTicksPerBar;
      ev.key = Key::C;
      ev.is_minor = false;
      ev.chord.degree = ChordDegree::V;
      ev.chord.quality = ChordQuality::Major;
      ev.chord.root_pitch = 67;
      tl.addEvent(ev);
    }
  }
  return tl;
}

// ===========================================================================
// Phase F: Dual-timeline NCT downgrade
// ===========================================================================

TEST(DualTimeline, ChordToneOfGeneration_DowngradedToLow) {
  // Bar-level timeline: I chord all bar (C, E, G).
  // Beat-level generation_timeline: beat 1 = vi chord (A, C, E).
  // A4 (MIDI 69) on beat 1: NCT of I (bar-level) but chord tone of vi (gen).
  // Should be downgraded to Low.
  auto bar_tl = makeCMajorTimeline(2);
  auto gen_tl = makeBeatResolutionTimeline(2);

  std::vector<NoteEvent> notes = {
      qn(kTicksPerBeat, 69, 0),  // A4 on beat 1
  };
  auto result = detectNonChordTones(notes, bar_tl, &gen_tl);
  ASSERT_GE(result.size(), 1u);
  EXPECT_EQ(result[0].severity, DissonanceSeverity::Low)
      << "A4 is chord tone of vi in generation timeline -- should be Low";
}

TEST(DualTimeline, NotChordToneOfEither_StaysHigh) {
  // F#4 (MIDI 66) on beat 0 (strong): not in I (C,E,G) bar-level,
  // not in I (C,E,G) generation beat 0 either.
  auto bar_tl = makeCMajorTimeline(2);
  auto gen_tl = makeBeatResolutionTimeline(2);

  std::vector<NoteEvent> notes = {
      qn(0, 66, 0),  // F#4 on beat 0
  };
  auto result = detectNonChordTones(notes, bar_tl, &gen_tl);
  ASSERT_GE(result.size(), 1u);
  EXPECT_EQ(result[0].severity, DissonanceSeverity::High)
      << "F#4 is not in either timeline -- should stay High";
}

TEST(DualTimeline, NullGenTimeline_NoEffect) {
  // Same as ChordToneOfGeneration but with nullptr -> no downgrade.
  auto bar_tl = makeCMajorTimeline(2);

  std::vector<NoteEvent> notes = {
      qn(kTicksPerBeat, 69, 0),  // A4 on beat 1 (weak)
  };
  auto with_null = detectNonChordTones(notes, bar_tl, nullptr);
  auto without = detectNonChordTones(notes, bar_tl);
  ASSERT_EQ(with_null.size(), without.size());
  for (size_t i = 0; i < with_null.size(); ++i) {
    EXPECT_EQ(with_null[i].severity, without[i].severity)
        << "nullptr generation_timeline should be identical to single-timeline";
  }
}

// ===========================================================================
// Phase G: FalseEntry structural downgrade
// ===========================================================================

TEST(FalseEntryStructural, SC_Clash_DowngradedToLow) {
  // FalseEntry source note clashing with another voice -> downgraded to Low.
  std::vector<NoteEvent> notes = {
      qn_src(0, 60, 0, BachNoteSource::FreeCounterpoint),  // C4
      qn_src(0, 61, 1, BachNoteSource::FalseEntry),        // C#4 (clash)
  };
  auto result = detectSimultaneousClashes(notes, 2);
  ASSERT_GE(result.size(), 1u);
  EXPECT_EQ(result[0].severity, DissonanceSeverity::Low)
      << "FalseEntry SC clash should be downgraded to Low";
}

TEST(FalseEntryStructural, NCT_DowngradedToLow) {
  // FalseEntry note that is NCT of the bar timeline.
  auto tl = makeCMajorTimeline(2);
  std::vector<NoteEvent> notes = {
      qn_src(0, 62, 0, BachNoteSource::FalseEntry),  // D4 (NCT of I chord)
  };
  auto result = detectNonChordTones(notes, tl);
  ASSERT_GE(result.size(), 1u);
  EXPECT_EQ(result[0].severity, DissonanceSeverity::Low)
      << "FalseEntry NCT should be downgraded to Low (structural)";
}

// ===========================================================================
// Gap: Subject structural downgrade (existing behavior, untested)
// ===========================================================================

TEST(SubjectStructural, SC_Clash_DowngradedToLow) {
  // Subject source note clashing with another voice -> downgraded to Low.
  std::vector<NoteEvent> notes = {
      qn_src(0, 60, 0, BachNoteSource::FreeCounterpoint),
      qn_src(0, 61, 1, BachNoteSource::FugueSubject),
  };
  auto result = detectSimultaneousClashes(notes, 2);
  ASSERT_GE(result.size(), 1u);
  EXPECT_EQ(result[0].severity, DissonanceSeverity::Low)
      << "FugueSubject SC clash should be downgraded to Low";
}

TEST(SubjectStructural, NCT_DowngradedToLow) {
  auto tl = makeCMajorTimeline(2);
  std::vector<NoteEvent> notes = {
      qn_src(0, 62, 0, BachNoteSource::FugueSubject),  // D4 (NCT of I chord)
  };
  auto result = detectNonChordTones(notes, tl);
  ASSERT_GE(result.size(), 1u);
  EXPECT_EQ(result[0].severity, DissonanceSeverity::Low)
      << "FugueSubject NCT should be downgraded to Low (structural)";
}

}  // namespace
}  // namespace bach
