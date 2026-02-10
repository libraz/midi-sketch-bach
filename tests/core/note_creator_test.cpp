// Tests for createBachNote (Phase 0 stub implementation).

#include "core/note_creator.h"

#include <gtest/gtest.h>

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

}  // namespace
}  // namespace bach
