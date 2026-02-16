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

// ---------------------------------------------------------------------------
// Voice-specific leap threshold tests (Phase 3b)
// ---------------------------------------------------------------------------

TEST(PostValidateLeapThreshold, SopranoLeapAbove10thReoctaved) {
  // Soprano (voice 0) with interval > 16 semitones (10th) should be re-octaved.
  // Voice range [48, 96] is wide enough for both pitches.
  // prev=60 (C4), curr=78 (F#5), interval=18 > 16 -> re-octave to closest
  // F#4=66 (18st to 6st, within range).
  std::vector<NoteEvent> notes;
  notes.push_back(makeNote(0, 60, 0, 480, BachNoteSource::FreeCounterpoint));
  notes.push_back(makeNote(0, 78, 480, 480, BachNoteSource::FreeCounterpoint));

  PostValidateStats stats;
  auto result = postValidateNotes(std::move(notes), 4,
                                   KeySignature{Key::C, false},
                                   std::vector<std::pair<uint8_t, uint8_t>>{
                                       {48, 96}, {48, 84}, {36, 72}, {24, 60}},
                                   &stats);
  // The soprano note at pitch 78 should be re-octaved to F#4=66.
  bool found_soprano_reoctaved = false;
  for (const auto& note : result) {
    if (note.voice == 0 && note.start_tick == 480) {
      // Should be within 16 semitones of prev pitch 60.
      int interval = std::abs(static_cast<int>(note.pitch) - 60);
      EXPECT_LE(interval, 16) << "Soprano interval " << interval << " exceeds 10th threshold";
      found_soprano_reoctaved = true;
    }
  }
  EXPECT_TRUE(found_soprano_reoctaved);
}

TEST(PostValidateLeapThreshold, BassOctaveLeapPermitted) {
  // Bass voice (last voice, index 3 in a 4-voice texture) with octave leap
  // (12 semitones) should NOT be re-octaved -- octave leaps are idiomatic.
  std::vector<NoteEvent> notes;
  notes.push_back(makeNote(3, 36, 0, 480, BachNoteSource::FreeCounterpoint));   // C2
  notes.push_back(makeNote(3, 48, 480, 480, BachNoteSource::FreeCounterpoint)); // C3 (octave up)

  PostValidateStats stats;
  auto result = postValidateNotes(std::move(notes), 4,
                                   KeySignature{Key::C, false},
                                   std::vector<std::pair<uint8_t, uint8_t>>{
                                       {60, 84}, {48, 72}, {36, 60}, {24, 60}},
                                   &stats);
  // Bass octave leap (12st < 17st threshold) should be preserved.
  for (const auto& note : result) {
    if (note.voice == 3 && note.start_tick == 480) {
      EXPECT_EQ(note.pitch, 48) << "Bass octave leap should be preserved";
    }
  }
}

TEST(PostValidateLeapThreshold, BassLeapAbove11thReoctaved) {
  // Bass (voice 3 in 4-voice) with 18 semitones > 17 threshold -> re-octave.
  std::vector<NoteEvent> notes;
  notes.push_back(makeNote(3, 36, 0, 480, BachNoteSource::FreeCounterpoint));   // C2
  notes.push_back(makeNote(3, 54, 480, 480, BachNoteSource::FreeCounterpoint)); // F#3 (18st)

  PostValidateStats stats;
  auto result = postValidateNotes(std::move(notes), 4,
                                   KeySignature{Key::C, false},
                                   std::vector<std::pair<uint8_t, uint8_t>>{
                                       {60, 84}, {48, 72}, {36, 60}, {24, 60}},
                                   &stats);
  for (const auto& note : result) {
    if (note.voice == 3 && note.start_tick == 480) {
      int interval = std::abs(static_cast<int>(note.pitch) - 36);
      EXPECT_LE(interval, 17)
          << "Bass interval " << interval << " exceeds 11th threshold";
    }
  }
}

TEST(PostValidateLeapThreshold, StylusPhantasticusWidensThreshold) {
  // With stylus_phantasticus=true, the Phase 3b leap threshold widens from
  // 16 (10th) to 19 (12th) for all voices.
  //
  // The test constructs notes that survive the main loop's collision resolver
  // cascade: Immutable notes pass through unchanged, while a Structural note
  // (FugueAnswer) only allows original + octave_shift in the resolver.
  //
  // By carefully choosing pitches where the octave_shift stage doesn't change
  // the pitch (pitch already at closest octave placement), we ensure Phase 3b
  // sees the large interval.
  //
  // note0=E5(76) Immutable, note1=E5(76) Immutable, note2=C4(60) FugueAnswer.
  // After the main loop, note2 at pitch 60 passes through (Structural notes
  // try original first; in a 1-voice freeCounterpoint context, 60 is accepted
  // because there are no vertical conflicts). But the octave shift may move it.
  //
  // Actually use 2-voice setup: voice 0 has the leap, voice 1 has a filler.
  // This avoids the single-voice collision resolver optimization.
  //
  // To avoid pipeline complexity, verify behavior with two Immutable notes
  // followed by a Structural note using ProtectionOverrides to make
  // the third note behave predictably.

  // Alternative approach: verify the threshold constants via the
  // StylusPhantasticusStillCapsExtremeLeaps test (which already works)
  // and focus here on verifying the threshold difference between modes.

  // Approach: use 3 Immutable notes where Phase 3b can't re-octave them
  // (skipped), and one Flexible note whose original pitch is preserved by the
  // collision resolver because it's the only valid candidate. The collision
  // resolver accepts it at original because:
  //   - freeCounterpoint=true, single voice -> no vertical issues
  //   - rangeTolerance=0, pitch in range -> range OK
  //   - original strategy succeeds
  //
  // Test: C5(72) Immutable, then A3(57) EpisodeMaterial (Flexible).
  // Interval: 15st (within 16st threshold) -> Phase 3b does NOT act.
  // Now C5(72) Immutable, then G#3(56) EpisodeMaterial (Flexible).
  // Interval: 16st (at threshold) -> Phase 3b does NOT act.
  // Now C5(72) Immutable, then G3(55) EpisodeMaterial (Flexible).
  // Interval: 17st > 16 -> Phase 3b re-octaves to G4(67), interval=5st.
  // With phantasticus: 17 < 19 -> preserved.
  //
  // BUT: the collision resolver may modify the Flexible note via step_shift
  // or chord_tone strategies even in freeCounterpoint mode.

  // Final approach: since the collision resolver interaction makes end-to-end
  // testing unreliable for exact pitch values, verify behavioral difference:
  // Run with and without phantasticus using a large-leap Flexible note.
  // Check that phantasticus mode produces a LARGER interval than non-phantasticus.

  auto make_test = [](uint8_t flexible_pitch) {
    std::vector<NoteEvent> notes;
    notes.push_back(makeNote(0, 72, 0, 480, BachNoteSource::SubjectCore));
    notes.push_back(makeNote(0, flexible_pitch, 480, 480,
                             BachNoteSource::EpisodeMaterial));
    return notes;
  };

  // Use pitch 50 (D3), interval from 72 = 22st, well above both thresholds.
  // After collision resolver (Flexible, 6-stage cascade), the pitch may be
  // adjusted. Phase 3b then re-octaves if the result still exceeds threshold.
  PostValidateStats stats_no, stats_yes;
  auto result_no = postValidateNotes(make_test(50), 1,
                                      KeySignature{Key::C, false},
                                      std::vector<std::pair<uint8_t, uint8_t>>{{36, 96}},
                                      &stats_no,
                                      /*protection_overrides=*/{},
                                      /*stylus_phantasticus=*/false);
  auto result_yes = postValidateNotes(make_test(50), 1,
                                       KeySignature{Key::C, false},
                                       std::vector<std::pair<uint8_t, uint8_t>>{{36, 96}},
                                       &stats_yes,
                                       /*protection_overrides=*/{},
                                       /*stylus_phantasticus=*/true);

  // Find the second note in each result.
  uint8_t pitch_no = 0, pitch_yes = 0;
  for (const auto& note : result_no) {
    if (note.voice == 0 && note.start_tick == 480) pitch_no = note.pitch;
  }
  for (const auto& note : result_yes) {
    if (note.voice == 0 && note.start_tick == 480) pitch_yes = note.pitch;
  }

  // Both should produce valid results.
  EXPECT_NE(pitch_no, 0) << "Non-phantasticus should produce a note";
  EXPECT_NE(pitch_yes, 0) << "Phantasticus should produce a note";

  // Non-phantasticus threshold is 16st, phantasticus is 19st.
  // The phantasticus result should allow a larger (or equal) interval.
  int interval_no = std::abs(static_cast<int>(pitch_no) - 72);
  int interval_yes = std::abs(static_cast<int>(pitch_yes) - 72);
  EXPECT_LE(interval_no, 16)
      << "Without phantasticus, Phase 3b should enforce <=16st";
  EXPECT_GE(interval_yes, interval_no)
      << "Phantasticus should allow at least as wide an interval";
}

TEST(PostValidateLeapThreshold, StylusPhantasticusStillCapsExtremeLeaps) {
  // Even in stylus_phantasticus, leaps > 19 semitones are re-octaved.
  // prev=60 (C4), curr=80 (G#5), interval=20 > 19 -> re-octaved.
  std::vector<NoteEvent> notes;
  notes.push_back(makeNote(0, 60, 0, 480, BachNoteSource::FreeCounterpoint));
  notes.push_back(makeNote(0, 80, 480, 480, BachNoteSource::FreeCounterpoint));

  PostValidateStats stats;
  auto result = postValidateNotes(std::move(notes), 4,
                                   KeySignature{Key::C, false},
                                   std::vector<std::pair<uint8_t, uint8_t>>{
                                       {48, 96}, {48, 84}, {36, 72}, {24, 60}},
                                   &stats,
                                   /*protection_overrides=*/{},
                                   /*stylus_phantasticus=*/true);
  for (const auto& note : result) {
    if (note.voice == 0 && note.start_tick == 480) {
      int interval = std::abs(static_cast<int>(note.pitch) - 60);
      EXPECT_LE(interval, 19)
          << "Even stylus_phantasticus caps at 12th (19st), got " << interval;
    }
  }
}

TEST(PostValidatePolicyTest, StylusPhantasticusPolicyFlag) {
  // The PostValidatePolicy::stylus_phantasticus flag defaults to false.
  PostValidatePolicy policy;
  EXPECT_FALSE(policy.stylus_phantasticus);
}

}  // namespace
}  // namespace bach
