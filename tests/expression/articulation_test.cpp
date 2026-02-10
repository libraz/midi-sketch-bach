// Tests for expression/articulation.h -- articulation rules, gate ratios,
// beat accents, and phrase breathing at cadence points.

#include "expression/articulation.h"

#include <gtest/gtest.h>

#include "core/basic_types.h"
#include "harmony/harmonic_event.h"
#include "harmony/harmonic_timeline.h"

namespace bach {
namespace {

// ---------------------------------------------------------------------------
// Helper: create a note event
// ---------------------------------------------------------------------------

static NoteEvent makeNote(Tick start, uint8_t pitch, Tick duration,
                          uint8_t velocity = 80, uint8_t voice = 0) {
  NoteEvent note;
  note.start_tick = start;
  note.pitch = pitch;
  note.duration = duration;
  note.velocity = velocity;
  note.voice = voice;
  return note;
}

/// Helper: create a harmonic event with specified chord degree.
static HarmonicEvent makeHarmEvent(Tick tick, Tick end_tick, ChordDegree degree,
                                   Key key = Key::C, bool is_minor = false) {
  HarmonicEvent evt;
  evt.tick = tick;
  evt.end_tick = end_tick;
  evt.key = key;
  evt.is_minor = is_minor;
  evt.chord.degree = degree;
  evt.chord.quality = is_minor ? minorKeyQuality(degree) : majorKeyQuality(degree);
  evt.chord.root_pitch = 60;
  evt.bass_pitch = 48;
  evt.weight = 1.0f;
  return evt;
}

// ===========================================================================
// getDefaultArticulation tests
// ===========================================================================

TEST(ArticulationDefaultTest, GroundIsLegato95) {
  auto rule = getDefaultArticulation(VoiceRole::Ground);
  EXPECT_EQ(rule.type, ArticulationType::Legato);
  EXPECT_FLOAT_EQ(rule.gate_ratio, 0.95f);
  EXPECT_EQ(rule.velocity_offset, 0);
}

TEST(ArticulationDefaultTest, AssertIsNonLegato85) {
  auto rule = getDefaultArticulation(VoiceRole::Assert);
  EXPECT_EQ(rule.type, ArticulationType::NonLegato);
  EXPECT_FLOAT_EQ(rule.gate_ratio, 0.85f);
}

TEST(ArticulationDefaultTest, RespondIsNonLegato87) {
  auto rule = getDefaultArticulation(VoiceRole::Respond);
  EXPECT_EQ(rule.type, ArticulationType::NonLegato);
  EXPECT_FLOAT_EQ(rule.gate_ratio, 0.87f);
}

TEST(ArticulationDefaultTest, PropelIsNonLegato85) {
  auto rule = getDefaultArticulation(VoiceRole::Propel);
  EXPECT_EQ(rule.type, ArticulationType::NonLegato);
  EXPECT_FLOAT_EQ(rule.gate_ratio, 0.85f);
}

// ===========================================================================
// applyArticulation -- gate ratio reduction
// ===========================================================================

TEST(ArticulationApplyTest, GateRatioReducesDuration) {
  std::vector<NoteEvent> notes;
  notes.push_back(makeNote(0, 60, kTicksPerBeat));  // 480 ticks at beat 0

  applyArticulation(notes, VoiceRole::Assert, nullptr, true);

  // Assert gate ratio = 0.85 -> 480 * 0.85 = 408
  EXPECT_EQ(notes[0].duration, static_cast<Tick>(480 * 0.85f));
}

TEST(ArticulationApplyTest, GroundLegato95) {
  std::vector<NoteEvent> notes;
  notes.push_back(makeNote(0, 48, kTicksPerBar));  // 1920 ticks

  applyArticulation(notes, VoiceRole::Ground, nullptr, true);

  // Ground gate ratio = 0.95 -> 1920 * 0.95 = 1824
  EXPECT_EQ(notes[0].duration, static_cast<Tick>(1920 * 0.95f));
}

TEST(ArticulationApplyTest, EmptyNotesNoOp) {
  std::vector<NoteEvent> notes;
  applyArticulation(notes, VoiceRole::Assert, nullptr, true);
  EXPECT_TRUE(notes.empty());
}

TEST(ArticulationApplyTest, MinimumDurationPreserved) {
  std::vector<NoteEvent> notes;
  // Very short note: 50 ticks. After 0.85 gate = 42, which is below minimum 60.
  notes.push_back(makeNote(0, 60, 50));

  applyArticulation(notes, VoiceRole::Assert, nullptr, true);

  // Should be clamped to minimum duration (60 ticks).
  EXPECT_GE(notes[0].duration, static_cast<Tick>(60));
}

// ===========================================================================
// applyArticulation -- beat-position velocity accents (non-organ)
// ===========================================================================

TEST(ArticulationAccentTest, DownbeatAccentNonOrgan) {
  std::vector<NoteEvent> notes;
  // Beat 0 of bar 0 (tick 0)
  notes.push_back(makeNote(0, 60, kTicksPerBeat, 80));

  applyArticulation(notes, VoiceRole::Assert, nullptr, false);

  // Non-organ, beat 0: velocity += 8 -> 88
  EXPECT_EQ(notes[0].velocity, 88);
}

TEST(ArticulationAccentTest, SecondaryStressNonOrgan) {
  std::vector<NoteEvent> notes;
  // Beat 2 of bar 0 (tick 960)
  notes.push_back(makeNote(kTicksPerBeat * 2, 60, kTicksPerBeat, 80));

  applyArticulation(notes, VoiceRole::Assert, nullptr, false);

  // Non-organ, beat 2: velocity += 4 -> 84
  EXPECT_EQ(notes[0].velocity, 84);
}

TEST(ArticulationAccentTest, WeakBeatNoAccentNonOrgan) {
  std::vector<NoteEvent> notes;
  // Beat 1 of bar 0 (tick 480)
  notes.push_back(makeNote(kTicksPerBeat, 60, kTicksPerBeat, 80));

  applyArticulation(notes, VoiceRole::Assert, nullptr, false);

  // Non-organ, beat 1: no accent, velocity stays 80
  EXPECT_EQ(notes[0].velocity, 80);
}

TEST(ArticulationAccentTest, OrganVelocityUnchanged) {
  std::vector<NoteEvent> notes;
  // Beat 0 (would get +8 accent on non-organ)
  notes.push_back(makeNote(0, 60, kTicksPerBeat, 80));

  applyArticulation(notes, VoiceRole::Assert, nullptr, true);

  // Organ: velocity fixed at 80, no modification.
  EXPECT_EQ(notes[0].velocity, 80);
}

TEST(ArticulationAccentTest, VelocityClampedAt127) {
  std::vector<NoteEvent> notes;
  // Very high velocity note on a downbeat.
  notes.push_back(makeNote(0, 60, kTicksPerBeat, 125));

  applyArticulation(notes, VoiceRole::Assert, nullptr, false);

  // 125 + 8 = 133, clamped to 127.
  EXPECT_LE(notes[0].velocity, 127);
}

// ===========================================================================
// applyArticulation -- phrase breathing at cadence points
// ===========================================================================

TEST(ArticulationBreathingTest, PerfectCadenceReducesPrecedingNote) {
  // Build a V -> I cadence in the timeline.
  HarmonicTimeline timeline;
  timeline.addEvent(makeHarmEvent(0, kTicksPerBar, ChordDegree::V));
  timeline.addEvent(makeHarmEvent(kTicksPerBar, kTicksPerBar * 2, ChordDegree::I));

  std::vector<NoteEvent> notes;
  // Note spanning the V chord region (before the cadence at kTicksPerBar).
  notes.push_back(makeNote(0, 60, kTicksPerBar));

  // Get the duration after gate ratio but before breathing.
  auto rule = getDefaultArticulation(VoiceRole::Assert);
  Tick gated_duration = static_cast<Tick>(static_cast<float>(kTicksPerBar) * rule.gate_ratio);
  // After breathing: gated_duration * 0.80
  Tick expected_breathed = static_cast<Tick>(static_cast<float>(gated_duration) * 0.80f);

  applyArticulation(notes, VoiceRole::Assert, &timeline, true);

  // The note should be shorter than the gate-only duration due to breathing.
  EXPECT_EQ(notes[0].duration, expected_breathed);
}

TEST(ArticulationBreathingTest, NoCadenceNoBreathingReduction) {
  // Timeline with I -> IV (no cadence pattern).
  HarmonicTimeline timeline;
  timeline.addEvent(makeHarmEvent(0, kTicksPerBar, ChordDegree::I));
  timeline.addEvent(makeHarmEvent(kTicksPerBar, kTicksPerBar * 2, ChordDegree::IV));

  std::vector<NoteEvent> notes;
  notes.push_back(makeNote(0, 60, kTicksPerBar));

  auto rule = getDefaultArticulation(VoiceRole::Assert);
  Tick expected = static_cast<Tick>(static_cast<float>(kTicksPerBar) * rule.gate_ratio);

  applyArticulation(notes, VoiceRole::Assert, &timeline, true);

  // No cadence, so duration should only reflect gate ratio.
  EXPECT_EQ(notes[0].duration, expected);
}

TEST(ArticulationBreathingTest, KeyChangeTriggersBreathing) {
  // Timeline with key change (C major -> G major).
  HarmonicTimeline timeline;
  timeline.addEvent(makeHarmEvent(0, kTicksPerBar, ChordDegree::I, Key::C));
  timeline.addEvent(makeHarmEvent(kTicksPerBar, kTicksPerBar * 2, ChordDegree::I, Key::G));

  std::vector<NoteEvent> notes;
  notes.push_back(makeNote(0, 60, kTicksPerBar));

  auto rule = getDefaultArticulation(VoiceRole::Assert);
  Tick gated = static_cast<Tick>(static_cast<float>(kTicksPerBar) * rule.gate_ratio);
  Tick expected = static_cast<Tick>(static_cast<float>(gated) * 0.80f);

  applyArticulation(notes, VoiceRole::Assert, &timeline, true);

  EXPECT_EQ(notes[0].duration, expected);
}

TEST(ArticulationBreathingTest, NullTimelineNoBreathing) {
  std::vector<NoteEvent> notes;
  notes.push_back(makeNote(0, 60, kTicksPerBar));

  auto rule = getDefaultArticulation(VoiceRole::Assert);
  Tick expected = static_cast<Tick>(static_cast<float>(kTicksPerBar) * rule.gate_ratio);

  applyArticulation(notes, VoiceRole::Assert, nullptr, true);

  EXPECT_EQ(notes[0].duration, expected);
}

// ===========================================================================
// Multiple notes
// ===========================================================================

TEST(ArticulationMultiNoteTest, AllNotesDurationsReduced) {
  std::vector<NoteEvent> notes;
  notes.push_back(makeNote(0, 60, kTicksPerBeat));
  notes.push_back(makeNote(kTicksPerBeat, 62, kTicksPerBeat));
  notes.push_back(makeNote(kTicksPerBeat * 2, 64, kTicksPerBeat));
  notes.push_back(makeNote(kTicksPerBeat * 3, 65, kTicksPerBeat));

  applyArticulation(notes, VoiceRole::Assert, nullptr, true);

  Tick expected = static_cast<Tick>(480.0f * 0.85f);
  for (const auto& note : notes) {
    EXPECT_EQ(note.duration, expected);
  }
}

}  // namespace
}  // namespace bach
