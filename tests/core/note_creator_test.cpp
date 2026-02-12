// Tests for createBachNote (Phase 0 stub implementation) and
// buildMelodicContextFromState.

#include "core/note_creator.h"

#include <gtest/gtest.h>

#include "counterpoint/counterpoint_state.h"
#include "counterpoint/melodic_context.h"

namespace bach {
namespace {

// ---------------------------------------------------------------------------
// Phase 0 stub: createBachNote always accepts
// ---------------------------------------------------------------------------

TEST(CreateBachNoteTest, StubAlwaysAccepts) {
  BachNoteOptions opts;
  opts.voice = 0;
  opts.desired_pitch = 60;
  opts.tick = 0;
  opts.duration = kTicksPerBeat;
  opts.velocity = 80;
  opts.source = BachNoteSource::FugueSubject;

  auto result = createBachNote(nullptr, nullptr, nullptr, opts);
  EXPECT_TRUE(result.accepted);
}

TEST(CreateBachNoteTest, NoteFieldsMatchOptions) {
  BachNoteOptions opts;
  opts.voice = 2;
  opts.desired_pitch = 67;  // G4
  opts.tick = 1920;         // Bar 1
  opts.duration = 960;      // Half note
  opts.velocity = 80;
  opts.source = BachNoteSource::FugueAnswer;

  auto result = createBachNote(nullptr, nullptr, nullptr, opts);

  EXPECT_EQ(result.note.pitch, 67);
  EXPECT_EQ(result.note.start_tick, 1920u);
  EXPECT_EQ(result.note.duration, 960u);
  EXPECT_EQ(result.note.velocity, 80);
  EXPECT_EQ(result.note.voice, 2);
}

TEST(CreateBachNoteTest, FinalPitchEqualDesiredInPhase0) {
  BachNoteOptions opts;
  opts.desired_pitch = 72;  // C5

  auto result = createBachNote(nullptr, nullptr, nullptr, opts);

  EXPECT_EQ(result.final_pitch, 72);
  EXPECT_FALSE(result.was_adjusted);
}

TEST(CreateBachNoteTest, ProvenanceRecordsSource) {
  BachNoteOptions opts;
  opts.source = BachNoteSource::Countersubject;
  opts.desired_pitch = 64;  // E4
  opts.tick = 480;
  opts.entry_number = 3;

  auto result = createBachNote(nullptr, nullptr, nullptr, opts);

  EXPECT_EQ(result.provenance.source, BachNoteSource::Countersubject);
  EXPECT_EQ(result.provenance.original_pitch, 64);
  EXPECT_EQ(result.provenance.lookup_tick, 480u);
  EXPECT_EQ(result.provenance.entry_number, 3);
  EXPECT_TRUE(result.provenance.hasProvenance());
}

TEST(CreateBachNoteTest, ProvenanceUnknownWhenSourceNotSet) {
  BachNoteOptions opts;
  // source defaults to Unknown.
  opts.desired_pitch = 60;

  auto result = createBachNote(nullptr, nullptr, nullptr, opts);

  EXPECT_EQ(result.provenance.source, BachNoteSource::Unknown);
  EXPECT_FALSE(result.provenance.hasProvenance());
}

TEST(CreateBachNoteTest, NoTransformStepsInPhase0) {
  BachNoteOptions opts;
  opts.desired_pitch = 60;
  opts.source = BachNoteSource::FugueSubject;

  auto result = createBachNote(nullptr, nullptr, nullptr, opts);

  EXPECT_EQ(result.provenance.step_count, 0);
}

TEST(CreateBachNoteTest, DefaultOptionsProduceValidNote) {
  BachNoteOptions opts;  // All defaults

  auto result = createBachNote(nullptr, nullptr, nullptr, opts);

  EXPECT_TRUE(result.accepted);
  EXPECT_EQ(result.note.pitch, 60);           // Default: C4
  EXPECT_EQ(result.note.start_tick, 0u);
  EXPECT_EQ(result.note.duration, kTicksPerBeat);  // 480
  EXPECT_EQ(result.note.velocity, 80);
  EXPECT_EQ(result.note.voice, 0);
  EXPECT_EQ(result.final_pitch, 60);
  EXPECT_FALSE(result.was_adjusted);
}

TEST(CreateBachNoteTest, DifferentVoicesAccepted) {
  // Phase 0: all voices should be accepted.
  for (VoiceId vid = 0; vid < 5; ++vid) {
    BachNoteOptions opts;
    opts.voice = vid;
    opts.desired_pitch = 60 + vid;
    opts.source = BachNoteSource::FreeCounterpoint;

    auto result = createBachNote(nullptr, nullptr, nullptr, opts);

    EXPECT_TRUE(result.accepted) << "Voice " << static_cast<int>(vid) << " rejected";
    EXPECT_EQ(result.note.voice, vid);
    EXPECT_EQ(result.note.pitch, 60 + vid);
  }
}

TEST(CreateBachNoteTest, AllSourceTypesAccepted) {
  // Phase 0 stub must accept notes regardless of source.
  const BachNoteSource sources[] = {
      BachNoteSource::FugueSubject,
      BachNoteSource::FugueAnswer,
      BachNoteSource::Countersubject,
      BachNoteSource::EpisodeMaterial,
      BachNoteSource::FreeCounterpoint,
      BachNoteSource::CantusFixed,
      BachNoteSource::Ornament,
      BachNoteSource::PedalPoint,
      BachNoteSource::ArpeggioFlow,
      BachNoteSource::TextureNote,
      BachNoteSource::GroundBass,
      BachNoteSource::CollisionAvoid,
      BachNoteSource::PostProcess,
  };

  for (auto src : sources) {
    BachNoteOptions opts;
    opts.source = src;
    opts.desired_pitch = 65;

    auto result = createBachNote(nullptr, nullptr, nullptr, opts);

    EXPECT_TRUE(result.accepted)
        << "Source " << bachNoteSourceToString(src) << " rejected";
    EXPECT_EQ(result.provenance.source, src);
  }
}

TEST(CreateBachNoteTest, ExtremeTickAndDurationValues) {
  BachNoteOptions opts;
  opts.tick = 1920 * 100;     // Bar 100
  opts.duration = 1920 * 4;   // 4 bars (whole note in 4/4)
  opts.desired_pitch = 36;    // C2 (low pedal range)
  opts.source = BachNoteSource::PedalPoint;

  auto result = createBachNote(nullptr, nullptr, nullptr, opts);

  EXPECT_TRUE(result.accepted);
  EXPECT_EQ(result.note.start_tick, 1920u * 100);
  EXPECT_EQ(result.note.duration, 1920u * 4);
  EXPECT_EQ(result.note.pitch, 36);
}

// ---------------------------------------------------------------------------
// buildMelodicContextFromState
// ---------------------------------------------------------------------------

TEST(BuildMelodicContextTest, EmptyState_ReturnsDefault) {
  CounterpointState state;
  state.registerVoice(0, 48, 84);
  auto ctx = buildMelodicContextFromState(state, 0);
  EXPECT_EQ(ctx.prev_count, 0);
  EXPECT_FALSE(ctx.leap_needs_resolution);
  EXPECT_FALSE(ctx.is_leading_tone);
}

TEST(BuildMelodicContextTest, OneNote_SetsPrevCount1) {
  CounterpointState state;
  state.registerVoice(0, 48, 84);
  state.setKey(Key::C);
  NoteEvent note;
  note.voice = 0;
  note.pitch = 60;
  note.start_tick = 0;
  note.duration = 480;
  state.addNote(0, note);

  auto ctx = buildMelodicContextFromState(state, 0);
  EXPECT_EQ(ctx.prev_count, 1);
  EXPECT_EQ(ctx.prev_pitches[0], 60);
  EXPECT_FALSE(ctx.leap_needs_resolution);
}

TEST(BuildMelodicContextTest, TwoNotes_SetsPrevCount2) {
  CounterpointState state;
  state.registerVoice(0, 48, 84);
  state.setKey(Key::C);

  NoteEvent note1;
  note1.voice = 0;
  note1.pitch = 60;
  note1.start_tick = 0;
  note1.duration = 480;
  state.addNote(0, note1);

  NoteEvent note2;
  note2.voice = 0;
  note2.pitch = 67;
  note2.start_tick = 480;
  note2.duration = 480;
  state.addNote(0, note2);

  auto ctx = buildMelodicContextFromState(state, 0);
  EXPECT_EQ(ctx.prev_count, 2);
  EXPECT_EQ(ctx.prev_pitches[0], 67);  // Most recent
  EXPECT_EQ(ctx.prev_pitches[1], 60);  // Previous
  EXPECT_EQ(ctx.prev_direction, 1);    // Ascending (60->67)
  EXPECT_TRUE(ctx.leap_needs_resolution);  // 7 semitones >= 5
}

TEST(BuildMelodicContextTest, StepMotion_NoLeapResolution) {
  CounterpointState state;
  state.registerVoice(0, 48, 84);
  state.setKey(Key::C);

  NoteEvent note1;
  note1.voice = 0;
  note1.pitch = 60;
  note1.start_tick = 0;
  note1.duration = 480;
  state.addNote(0, note1);

  NoteEvent note2;
  note2.voice = 0;
  note2.pitch = 62;
  note2.start_tick = 480;
  note2.duration = 480;
  state.addNote(0, note2);

  auto ctx = buildMelodicContextFromState(state, 0);
  EXPECT_FALSE(ctx.leap_needs_resolution);  // 2 semitones < 5
}

TEST(BuildMelodicContextTest, LeadingTone_Detected) {
  CounterpointState state;
  state.registerVoice(0, 48, 84);
  state.setKey(Key::C);  // Leading tone = B (pitch class 11)

  NoteEvent note;
  note.voice = 0;
  note.pitch = 71;  // B4
  note.start_tick = 0;
  note.duration = 480;
  state.addNote(0, note);

  auto ctx = buildMelodicContextFromState(state, 0);
  EXPECT_TRUE(ctx.is_leading_tone);
}

TEST(BuildMelodicContextTest, NonLeadingTone_NotDetected) {
  CounterpointState state;
  state.registerVoice(0, 48, 84);
  state.setKey(Key::C);

  NoteEvent note;
  note.voice = 0;
  note.pitch = 60;  // C4
  note.start_tick = 0;
  note.duration = 480;
  state.addNote(0, note);

  auto ctx = buildMelodicContextFromState(state, 0);
  EXPECT_FALSE(ctx.is_leading_tone);
}

}  // namespace
}  // namespace bach
