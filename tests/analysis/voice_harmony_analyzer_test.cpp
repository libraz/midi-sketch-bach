// Tests for read-only voice-harmony alignment analysis.

#include "analysis/voice_harmony_analyzer.h"

#include <gtest/gtest.h>

#include <vector>

#include "core/basic_types.h"
#include "harmony/chord_types.h"
#include "harmony/harmonic_event.h"
#include "harmony/harmonic_timeline.h"
#include "harmony/key.h"

namespace bach {
namespace {

// ---------------------------------------------------------------------------
// analyzeVoiceHarmony: empty input
// ---------------------------------------------------------------------------

TEST(VoiceHarmonyAnalyzerTest, EmptyTracksReturnZeroCoverage) {
  std::vector<Track> tracks;
  HarmonicTimeline timeline;
  auto report = analyzeVoiceHarmony(tracks, timeline);
  EXPECT_EQ(report.total_notes, 0);
  EXPECT_FLOAT_EQ(report.chord_tone_coverage, 0.0f);
  EXPECT_FLOAT_EQ(report.voice_leading_quality, 0.0f);
  EXPECT_FLOAT_EQ(report.contrary_motion_ratio, 0.0f);
  EXPECT_EQ(report.suspension_count, 0);
}

// ---------------------------------------------------------------------------
// analyzeTrackHarmony: chord tone coverage
// ---------------------------------------------------------------------------

TEST(VoiceHarmonyAnalyzerTest, AllChordTonesGiveFullCoverage) {
  // Create C major chord (C, E, G) as notes.
  std::vector<NoteEvent> notes;
  notes.push_back({0, 480, 60, 80, 0});    // C4
  notes.push_back({480, 480, 64, 80, 0});   // E4
  notes.push_back({960, 480, 67, 80, 0});   // G4

  // Create timeline with C major I chord spanning the whole bar.
  KeySignature key_sig = {Key::C, false};
  auto timeline = HarmonicTimeline::createStandard(key_sig, kTicksPerBar,
                                                    HarmonicResolution::Bar);

  auto report = analyzeTrackHarmony(notes, timeline);
  EXPECT_EQ(report.total_notes, 3);
  EXPECT_FLOAT_EQ(report.chord_tone_coverage, 1.0f);
}

TEST(VoiceHarmonyAnalyzerTest, NonChordTonesReduceCoverage) {
  // C major context: C is chord tone, F# is not.
  std::vector<NoteEvent> notes;
  notes.push_back({0, 480, 60, 80, 0});     // C4 (chord tone)
  notes.push_back({480, 480, 66, 80, 0});    // F#4 (NOT a chord tone of C major)

  KeySignature key_sig = {Key::C, false};
  auto timeline = HarmonicTimeline::createStandard(key_sig, kTicksPerBar,
                                                    HarmonicResolution::Bar);

  auto report = analyzeTrackHarmony(notes, timeline);
  EXPECT_EQ(report.total_notes, 2);
  EXPECT_EQ(report.chord_tone_notes, 1);
  EXPECT_FLOAT_EQ(report.chord_tone_coverage, 0.5f);
}

// ---------------------------------------------------------------------------
// analyzeTrackHarmony: stepwise motion
// ---------------------------------------------------------------------------

TEST(VoiceHarmonyAnalyzerTest, StepwiseMotionDetected) {
  std::vector<NoteEvent> notes;
  notes.push_back({0, 480, 60, 80, 0});     // C4
  notes.push_back({480, 480, 62, 80, 0});    // D4 (step = 2 semitones)
  notes.push_back({960, 480, 64, 80, 0});    // E4 (step = 2 semitones)

  KeySignature key_sig = {Key::C, false};
  auto timeline = HarmonicTimeline::createStandard(key_sig, kTicksPerBar,
                                                    HarmonicResolution::Bar);

  auto report = analyzeTrackHarmony(notes, timeline);
  // 2 out of 2 transitions are stepwise.
  EXPECT_EQ(report.stepwise_notes, 2);
  EXPECT_FLOAT_EQ(report.voice_leading_quality, 1.0f);
}

TEST(VoiceHarmonyAnalyzerTest, LeapReducesVoiceLeadingQuality) {
  std::vector<NoteEvent> notes;
  notes.push_back({0, 480, 60, 80, 0});     // C4
  notes.push_back({480, 480, 72, 80, 0});    // C5 (octave leap = 12 semitones)

  KeySignature key_sig = {Key::C, false};
  auto timeline = HarmonicTimeline::createStandard(key_sig, kTicksPerBar,
                                                    HarmonicResolution::Bar);

  auto report = analyzeTrackHarmony(notes, timeline);
  EXPECT_EQ(report.stepwise_notes, 0);
  EXPECT_FLOAT_EQ(report.voice_leading_quality, 0.0f);
}

TEST(VoiceHarmonyAnalyzerTest, EmptyNotesReturnZero) {
  std::vector<NoteEvent> notes;
  HarmonicTimeline timeline;

  auto report = analyzeTrackHarmony(notes, timeline);
  EXPECT_EQ(report.total_notes, 0);
  EXPECT_FLOAT_EQ(report.chord_tone_coverage, 0.0f);
  EXPECT_FLOAT_EQ(report.voice_leading_quality, 0.0f);
}

TEST(VoiceHarmonyAnalyzerTest, SingleNoteHasNoTransitions) {
  std::vector<NoteEvent> notes;
  notes.push_back({0, 480, 60, 80, 0});

  KeySignature key_sig = {Key::C, false};
  auto timeline = HarmonicTimeline::createStandard(key_sig, kTicksPerBar,
                                                    HarmonicResolution::Bar);

  auto report = analyzeTrackHarmony(notes, timeline);
  EXPECT_EQ(report.total_notes, 1);
  EXPECT_FLOAT_EQ(report.voice_leading_quality, 0.0f);  // No transitions.
  EXPECT_FLOAT_EQ(report.chord_tone_coverage, 1.0f);    // C4 is chord tone of I.
}

// ---------------------------------------------------------------------------
// Suspension detection
// ---------------------------------------------------------------------------

TEST(VoiceHarmonyAnalyzerTest, SuspensionDetectionBasic) {
  // Create a suspension: note held over chord change, then resolves down by step.
  // Bar structure: I chord for first half, V chord for second half.
  HarmonicTimeline timeline;

  HarmonicEvent ev_one;
  ev_one.tick = 0;
  ev_one.end_tick = 960;
  ev_one.key = Key::C;
  ev_one.is_minor = false;
  ev_one.chord = {ChordDegree::I, ChordQuality::Major, 60, 0};
  ev_one.bass_pitch = 48;
  ev_one.weight = 1.0f;
  timeline.addEvent(ev_one);

  HarmonicEvent ev_two;
  ev_two.tick = 960;
  ev_two.end_tick = 1920;
  ev_two.key = Key::C;
  ev_two.is_minor = false;
  ev_two.chord = {ChordDegree::V, ChordQuality::Major, 67, 0};
  ev_two.bass_pitch = 43;
  ev_two.weight = 0.75f;
  timeline.addEvent(ev_two);

  // Note sequence: C4 held across chord boundary, then resolves to B3.
  // At tick 960 (V chord: G-B-D), C is NOT a chord tone = dissonant.
  // Resolution: C4(60) -> B3(59) = downward by 1 semitone.
  std::vector<NoteEvent> notes;
  notes.push_back({0, 1200, 60, 80, 0});     // C4 held past tick 960
  notes.push_back({1200, 480, 59, 80, 0});    // B3 resolution

  int suspensions = countSuspensions(notes, timeline);
  EXPECT_GE(suspensions, 1);
}

TEST(VoiceHarmonyAnalyzerTest, NoSuspensionWhenConsonant) {
  // Notes are all chord tones -- no suspension should be detected.
  KeySignature key_sig = {Key::C, false};
  auto timeline = HarmonicTimeline::createStandard(key_sig, kTicksPerBar,
                                                    HarmonicResolution::Bar);

  std::vector<NoteEvent> notes;
  notes.push_back({0, 480, 60, 80, 0});     // C4 (chord tone of I)
  notes.push_back({480, 480, 64, 80, 0});    // E4 (chord tone of I)

  int suspensions = countSuspensions(notes, timeline);
  EXPECT_EQ(suspensions, 0);
}

TEST(VoiceHarmonyAnalyzerTest, SuspensionRequiresDownwardResolution) {
  // Dissonant held note but resolves UP -- should NOT count as suspension.
  HarmonicTimeline timeline;

  HarmonicEvent ev_one;
  ev_one.tick = 0;
  ev_one.end_tick = 480;
  ev_one.key = Key::C;
  ev_one.is_minor = false;
  ev_one.chord = {ChordDegree::I, ChordQuality::Major, 60, 0};
  ev_one.bass_pitch = 48;
  ev_one.weight = 1.0f;
  timeline.addEvent(ev_one);

  HarmonicEvent ev_two;
  ev_two.tick = 480;
  ev_two.end_tick = 1920;
  ev_two.key = Key::C;
  ev_two.is_minor = false;
  ev_two.chord = {ChordDegree::V, ChordQuality::Major, 67, 0};
  ev_two.bass_pitch = 43;
  ev_two.weight = 0.75f;
  timeline.addEvent(ev_two);

  // F4 held into V chord context (F is not a chord tone of G-B-D).
  // Then resolves UP to G4 instead of down -- not a proper suspension.
  std::vector<NoteEvent> notes;
  notes.push_back({0, 720, 65, 80, 0});     // F4 held past tick 480
  notes.push_back({720, 480, 67, 80, 0});    // G4 (upward = not suspension resolution)

  int suspensions = countSuspensions(notes, timeline);
  EXPECT_EQ(suspensions, 0);
}

// ---------------------------------------------------------------------------
// Multi-track analysis
// ---------------------------------------------------------------------------

TEST(VoiceHarmonyAnalyzerTest, ObservationsGenerated) {
  Track track;
  track.channel = 0;
  track.notes.push_back({0, 480, 60, 80, 0});
  std::vector<Track> tracks = {track};

  KeySignature key_sig = {Key::C, false};
  auto timeline = HarmonicTimeline::createStandard(key_sig, kTicksPerBar,
                                                    HarmonicResolution::Bar);

  auto report = analyzeVoiceHarmony(tracks, timeline);
  EXPECT_FALSE(report.observations.empty());
}

TEST(VoiceHarmonyAnalyzerTest, MultiTrackAggregation) {
  // Two tracks: one with all chord tones, one with no chord tones.
  Track track_a;
  track_a.channel = 0;
  track_a.notes.push_back({0, 480, 60, 80, 0});   // C4 (chord tone)
  track_a.notes.push_back({480, 480, 64, 80, 0});  // E4 (chord tone)

  Track track_b;
  track_b.channel = 1;
  track_b.notes.push_back({0, 480, 66, 80, 1});    // F#4 (not chord tone)
  track_b.notes.push_back({480, 480, 68, 80, 1});   // G#4 (not chord tone)

  KeySignature key_sig = {Key::C, false};
  auto timeline = HarmonicTimeline::createStandard(key_sig, kTicksPerBar,
                                                    HarmonicResolution::Bar);

  std::vector<Track> tracks = {track_a, track_b};
  auto report = analyzeVoiceHarmony(tracks, timeline);

  EXPECT_EQ(report.total_notes, 4);
  EXPECT_EQ(report.chord_tone_notes, 2);
  EXPECT_FLOAT_EQ(report.chord_tone_coverage, 0.5f);
}

TEST(VoiceHarmonyAnalyzerTest, ContraryMotionDetected) {
  // Track A: ascending C4->D4. Track B: descending G4->F4.
  // This creates contrary motion.
  Track track_a;
  track_a.channel = 0;
  track_a.notes.push_back({0, 480, 60, 80, 0});     // C4
  track_a.notes.push_back({480, 480, 62, 80, 0});    // D4 (ascending)

  Track track_b;
  track_b.channel = 1;
  track_b.notes.push_back({0, 480, 67, 80, 1});     // G4
  track_b.notes.push_back({480, 480, 65, 80, 1});    // F4 (descending)

  KeySignature key_sig = {Key::C, false};
  auto timeline = HarmonicTimeline::createStandard(key_sig, kTicksPerBar,
                                                    HarmonicResolution::Bar);

  std::vector<Track> tracks = {track_a, track_b};
  auto report = analyzeVoiceHarmony(tracks, timeline);

  // One comparison point, both voices moving in opposite directions.
  EXPECT_GT(report.contrary_motion_ratio, 0.0f);
}

TEST(VoiceHarmonyAnalyzerTest, ParallelMotionGivesZeroContraryRatio) {
  // Both tracks ascending in parallel.
  Track track_a;
  track_a.channel = 0;
  track_a.notes.push_back({0, 480, 60, 80, 0});     // C4
  track_a.notes.push_back({480, 480, 62, 80, 0});    // D4

  Track track_b;
  track_b.channel = 1;
  track_b.notes.push_back({0, 480, 72, 80, 1});     // C5
  track_b.notes.push_back({480, 480, 74, 80, 1});    // D5

  KeySignature key_sig = {Key::C, false};
  auto timeline = HarmonicTimeline::createStandard(key_sig, kTicksPerBar,
                                                    HarmonicResolution::Bar);

  std::vector<Track> tracks = {track_a, track_b};
  auto report = analyzeVoiceHarmony(tracks, timeline);

  EXPECT_FLOAT_EQ(report.contrary_motion_ratio, 0.0f);
}

// ---------------------------------------------------------------------------
// Edge cases
// ---------------------------------------------------------------------------

TEST(VoiceHarmonyAnalyzerTest, EmptyTimelineUsesDefaultEvent) {
  // When timeline is empty, getAt returns C major I chord by default.
  std::vector<NoteEvent> notes;
  notes.push_back({0, 480, 60, 80, 0});   // C4 is chord tone of default C major
  notes.push_back({480, 480, 66, 80, 0});  // F#4 is not

  HarmonicTimeline empty_timeline;
  auto report = analyzeTrackHarmony(notes, empty_timeline);

  EXPECT_EQ(report.total_notes, 2);
  EXPECT_EQ(report.chord_tone_notes, 1);
}

TEST(VoiceHarmonyAnalyzerTest, CountSuspensionsEmptyNotes) {
  HarmonicTimeline timeline;
  std::vector<NoteEvent> notes;
  EXPECT_EQ(countSuspensions(notes, timeline), 0);
}

TEST(VoiceHarmonyAnalyzerTest, CountSuspensionsSingleNote) {
  KeySignature key_sig = {Key::C, false};
  auto timeline = HarmonicTimeline::createStandard(key_sig, kTicksPerBar,
                                                    HarmonicResolution::Bar);
  std::vector<NoteEvent> notes;
  notes.push_back({0, 480, 60, 80, 0});
  EXPECT_EQ(countSuspensions(notes, timeline), 0);
}

}  // namespace
}  // namespace bach
