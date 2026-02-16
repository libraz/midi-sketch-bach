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

// ---------------------------------------------------------------------------
// PostValidatePolicy + PostValidateStats: policy-aware overloads
// ---------------------------------------------------------------------------

/// Helper: create a NoteEvent with specified fields.
NoteEvent makeNote(uint8_t voice, uint8_t pitch, Tick start, Tick dur,
                   BachNoteSource source = BachNoteSource::FreeCounterpoint) {
  NoteEvent note;
  note.voice = voice;
  note.pitch = pitch;
  note.start_tick = start;
  note.duration = dur;
  note.velocity = 80;
  note.source = source;
  return note;
}

TEST(PostValidatePolicyTest, DefaultPolicyValues) {
  PostValidatePolicy policy;
  EXPECT_TRUE(policy.fix_parallel_perfect);
  EXPECT_TRUE(policy.fix_voice_crossing);
  EXPECT_TRUE(policy.fix_strong_beat_dissonance);
  EXPECT_FALSE(policy.fix_weak_beat_nct);
  EXPECT_FALSE(policy.fix_hidden_perfect);
  EXPECT_EQ(policy.cadence_protection_ticks, 0u);
}

TEST(PostValidatePolicyTest, EmptyInputReturnsEmpty) {
  std::vector<NoteEvent> empty;
  PostValidatePolicy policy;
  PostValidateStats stats;
  auto result = postValidateNotes(std::move(empty), 4,
                                   KeySignature{Key::C, false},
                                   std::vector<std::pair<uint8_t, uint8_t>>{{48, 84}, {48, 84}, {36, 72}, {24, 60}},
                                   &stats, {}, policy);
  EXPECT_TRUE(result.empty());
  EXPECT_EQ(stats.total_input, 0u);
  EXPECT_EQ(stats.dropped, 0u);
}

TEST(PostValidatePolicyTest, NeverDropsNotes) {
  // The policy-aware overload is a targeted safety net that never drops notes.
  std::vector<NoteEvent> notes;
  notes.push_back(makeNote(0, 72, 0, 480, BachNoteSource::FreeCounterpoint));
  notes.push_back(makeNote(1, 60, 0, 480, BachNoteSource::EpisodeMaterial));
  notes.push_back(makeNote(2, 48, 0, 480, BachNoteSource::FreeCounterpoint));
  notes.push_back(makeNote(0, 74, 480, 480, BachNoteSource::FreeCounterpoint));
  notes.push_back(makeNote(1, 62, 480, 480, BachNoteSource::EpisodeMaterial));
  notes.push_back(makeNote(2, 50, 480, 480, BachNoteSource::FreeCounterpoint));

  PostValidatePolicy policy;
  PostValidateStats stats;
  auto result = postValidateNotes(std::move(notes), 3,
                                   KeySignature{Key::C, false},
                                   std::vector<std::pair<uint8_t, uint8_t>>{{60, 84}, {48, 72}, {36, 60}},
                                   &stats, {}, policy);
  EXPECT_EQ(result.size(), 6u);
  EXPECT_EQ(stats.dropped, 0u);
  EXPECT_EQ(stats.total_input, 6u);
}

TEST(PostValidatePolicyTest, SubjectPitchesProtected) {
  // Subject notes must never have their pitches modified.
  std::vector<NoteEvent> notes;
  notes.push_back(makeNote(0, 72, 0, 480, BachNoteSource::FugueSubject));
  notes.push_back(makeNote(0, 74, 480, 480, BachNoteSource::FugueSubject));
  notes.push_back(makeNote(1, 60, 0, 480, BachNoteSource::FreeCounterpoint));

  PostValidatePolicy policy;
  PostValidateStats stats;
  auto result = postValidateNotes(std::move(notes), 2,
                                   KeySignature{Key::C, false},
                                   std::vector<std::pair<uint8_t, uint8_t>>{{60, 84}, {48, 72}},
                                   &stats, {}, policy);
  // Subject pitches must be preserved.
  bool found_subject_0 = false;
  bool found_subject_1 = false;
  for (const auto& note : result) {
    if (note.source == BachNoteSource::FugueSubject && note.start_tick == 0) {
      EXPECT_EQ(note.pitch, 72u);
      found_subject_0 = true;
    }
    if (note.source == BachNoteSource::FugueSubject && note.start_tick == 480) {
      EXPECT_EQ(note.pitch, 74u);
      found_subject_1 = true;
    }
  }
  EXPECT_TRUE(found_subject_0);
  EXPECT_TRUE(found_subject_1);
}

TEST(PostValidatePolicyTest, SubjectCoreProtected) {
  std::vector<NoteEvent> notes;
  notes.push_back(makeNote(0, 65, 0, 480, BachNoteSource::SubjectCore));

  PostValidatePolicy policy;
  PostValidateStats stats;
  auto result = postValidateNotes(std::move(notes), 2,
                                   KeySignature{Key::C, false},
                                   std::vector<std::pair<uint8_t, uint8_t>>{{60, 84}, {48, 72}},
                                   &stats, {}, policy);
  ASSERT_EQ(result.size(), 1u);
  EXPECT_EQ(result[0].pitch, 65u);
}

TEST(PostValidatePolicyTest, AnswerPitchProtected) {
  std::vector<NoteEvent> notes;
  notes.push_back(makeNote(1, 67, 960, 480, BachNoteSource::FugueAnswer));

  PostValidatePolicy policy;
  PostValidateStats stats;
  auto result = postValidateNotes(std::move(notes), 2,
                                   KeySignature{Key::C, false},
                                   std::vector<std::pair<uint8_t, uint8_t>>{{60, 84}, {48, 72}},
                                   &stats, {}, policy);
  ASSERT_EQ(result.size(), 1u);
  EXPECT_EQ(result[0].pitch, 67u);
}

TEST(PostValidatePolicyTest, CountersubjectProtected) {
  std::vector<NoteEvent> notes;
  notes.push_back(makeNote(1, 64, 0, 480, BachNoteSource::Countersubject));

  PostValidatePolicy policy;
  PostValidateStats stats;
  auto result = postValidateNotes(std::move(notes), 2,
                                   KeySignature{Key::C, false},
                                   std::vector<std::pair<uint8_t, uint8_t>>{{60, 84}, {48, 72}},
                                   &stats, {}, policy);
  ASSERT_EQ(result.size(), 1u);
  EXPECT_EQ(result[0].pitch, 64u);
}

TEST(PostValidatePolicyTest, RangeClampingApplied) {
  // A note outside range should be clamped.
  std::vector<NoteEvent> notes;
  // Pitch 90 is above the range [60, 84] for voice 0.
  notes.push_back(makeNote(0, 90, 0, 480, BachNoteSource::FreeCounterpoint));
  // Pitch 30 is below the range [48, 72] for voice 1.
  notes.push_back(makeNote(1, 30, 0, 480, BachNoteSource::EpisodeMaterial));

  PostValidatePolicy policy;
  PostValidateStats stats;
  auto result = postValidateNotes(std::move(notes), 2,
                                   KeySignature{Key::C, false},
                                   std::vector<std::pair<uint8_t, uint8_t>>{{60, 84}, {48, 72}},
                                   &stats, {}, policy);
  ASSERT_EQ(result.size(), 2u);
  EXPECT_EQ(result[0].pitch, 84u);  // Clamped to upper bound.
  EXPECT_EQ(result[1].pitch, 48u);  // Clamped to lower bound.
  EXPECT_GE(stats.repaired, 2u);
}

TEST(PostValidatePolicyTest, ImmutableNotesNotRangeClamped) {
  // Immutable notes (SubjectCore, CantusFixed, GroundBass) should not be clamped.
  std::vector<NoteEvent> notes;
  notes.push_back(makeNote(0, 90, 0, 480, BachNoteSource::SubjectCore));

  PostValidatePolicy policy;
  PostValidateStats stats;
  auto result = postValidateNotes(std::move(notes), 2,
                                   KeySignature{Key::C, false},
                                   std::vector<std::pair<uint8_t, uint8_t>>{{60, 84}, {48, 72}},
                                   &stats, {}, policy);
  ASSERT_EQ(result.size(), 1u);
  EXPECT_EQ(result[0].pitch, 90u);  // Not clamped: immutable.
}

TEST(PostValidatePolicyTest, ParallelRepairDisabled) {
  // When policy disables parallel repair, no parallel perfect fixes are applied.
  PostValidatePolicy policy;
  policy.fix_parallel_perfect = false;

  std::vector<NoteEvent> notes;
  notes.push_back(makeNote(0, 72, 0, 480, BachNoteSource::FreeCounterpoint));
  notes.push_back(makeNote(1, 60, 0, 480, BachNoteSource::FreeCounterpoint));
  notes.push_back(makeNote(0, 79, 480, 480, BachNoteSource::FreeCounterpoint));
  notes.push_back(makeNote(1, 67, 480, 480, BachNoteSource::FreeCounterpoint));

  PostValidateStats stats;
  auto result = postValidateNotes(std::move(notes), 2,
                                   KeySignature{Key::C, false},
                                   std::vector<std::pair<uint8_t, uint8_t>>{{60, 84}, {48, 72}},
                                   &stats, {}, policy);
  // With repair disabled, parallel 5ths may remain. No notes dropped.
  EXPECT_EQ(result.size(), 4u);
  EXPECT_EQ(stats.dropped, 0u);
}

TEST(PostValidatePolicyTest, StatsTrackShiftMagnitude) {
  // Out-of-range notes get clamped, producing measurable shifts.
  std::vector<NoteEvent> notes;
  notes.push_back(makeNote(0, 90, 0, 480, BachNoteSource::FreeCounterpoint));  // +6 shift
  notes.push_back(makeNote(1, 60, 0, 480, BachNoteSource::FreeCounterpoint));  // No shift

  PostValidatePolicy policy;
  PostValidateStats stats;
  auto result = postValidateNotes(std::move(notes), 2,
                                   KeySignature{Key::C, false},
                                   std::vector<std::pair<uint8_t, uint8_t>>{{60, 84}, {48, 72}},
                                   &stats, {}, policy);
  EXPECT_GE(stats.max_shift_semitones, 6);
  EXPECT_GT(stats.avg_shift_semitones, 0.0f);
}

TEST(PostValidatePolicyTest, FunctionBasedRangeOverload) {
  std::vector<NoteEvent> notes;
  notes.push_back(makeNote(0, 72, 0, 480, BachNoteSource::FreeCounterpoint));
  notes.push_back(makeNote(1, 60, 0, 480, BachNoteSource::FreeCounterpoint));

  PostValidatePolicy policy;
  PostValidateStats stats;
  auto range_fn = [](uint8_t voice,
                     Tick /*tick*/) -> std::pair<uint8_t, uint8_t> {
    if (voice == 0) return {60, 84};
    return {48, 72};
  };
  auto result = postValidateNotes(std::move(notes), 2,
                                   KeySignature{Key::C, false},
                                   range_fn, &stats, {}, policy);
  EXPECT_EQ(result.size(), 2u);
  EXPECT_EQ(stats.dropped, 0u);
}

TEST(PostValidatePolicyTest, VectorAndFunctionOverloadsProduceSameResult) {
  // Both policy-aware overloads should produce identical results.
  auto make_test_notes = []() {
    std::vector<NoteEvent> notes;
    notes.push_back(makeNote(0, 72, 0, 480, BachNoteSource::FreeCounterpoint));
    notes.push_back(makeNote(1, 60, 0, 480, BachNoteSource::FreeCounterpoint));
    notes.push_back(makeNote(0, 74, 480, 480, BachNoteSource::FugueSubject));
    notes.push_back(makeNote(1, 62, 480, 480, BachNoteSource::Countersubject));
    return notes;
  };

  std::vector<std::pair<uint8_t, uint8_t>> ranges = {{60, 84}, {48, 72}};
  PostValidatePolicy policy;
  PostValidateStats stats_vec, stats_fn;

  auto result_vec = postValidateNotes(make_test_notes(), 2,
                                       KeySignature{Key::C, false},
                                       ranges, &stats_vec, {}, policy);
  auto range_fn = [&ranges](uint8_t voice,
                             Tick /*tick*/) -> std::pair<uint8_t, uint8_t> {
    if (voice < ranges.size()) return ranges[voice];
    return {0, 127};
  };
  auto result_fn = postValidateNotes(make_test_notes(), 2,
                                      KeySignature{Key::C, false},
                                      range_fn, &stats_fn, {}, policy);

  ASSERT_EQ(result_vec.size(), result_fn.size());
  for (size_t idx = 0; idx < result_vec.size(); ++idx) {
    EXPECT_EQ(result_vec[idx].pitch, result_fn[idx].pitch) << "idx=" << idx;
    EXPECT_EQ(result_vec[idx].voice, result_fn[idx].voice) << "idx=" << idx;
    EXPECT_EQ(result_vec[idx].start_tick, result_fn[idx].start_tick) << "idx=" << idx;
  }
  EXPECT_EQ(stats_vec.total_input, stats_fn.total_input);
  EXPECT_EQ(stats_vec.dropped, stats_fn.dropped);
}

TEST(PostValidateStatsTest, ExpandedFieldsDefaultToZero) {
  PostValidateStats stats;
  EXPECT_EQ(stats.parallel_fixes, 0u);
  EXPECT_EQ(stats.crossing_fixes, 0u);
  EXPECT_EQ(stats.dissonance_fixes, 0u);
  EXPECT_FLOAT_EQ(stats.avg_shift_semitones, 0.0f);
  EXPECT_EQ(stats.max_shift_semitones, 0);
  EXPECT_EQ(stats.subject_touches, 0u);
  EXPECT_EQ(stats.countersubject_touches, 0u);
  EXPECT_EQ(stats.stretto_section_touches, 0u);
}

TEST(PostValidateStatsTest, DropRateCalculation) {
  PostValidateStats stats;
  stats.total_input = 100;
  stats.dropped = 5;
  EXPECT_NEAR(stats.drop_rate(), 0.05f, 0.001f);

  stats.total_input = 0;
  EXPECT_FLOAT_EQ(stats.drop_rate(), 0.0f);
}

}  // namespace
}  // namespace bach
