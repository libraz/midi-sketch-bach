// Tests for solo_string/flow/flow_analyzer.h -- flow quality analysis.

#include "solo_string/flow/flow_analyzer.h"

#include <vector>

#include <gtest/gtest.h>

#include "core/basic_types.h"
#include "harmony/harmonic_event.h"
#include "harmony/harmonic_timeline.h"
#include "solo_string/flow/arpeggio_flow_config.h"

namespace bach {
namespace {

// ===========================================================================
// Test helpers
// ===========================================================================

/// @brief Create a default ArpeggioFlowConfig with valid arc for testing.
ArpeggioFlowConfig createTestConfig(int num_sections = 6, int bars_per_section = 4) {
  ArpeggioFlowConfig config;
  config.num_sections = num_sections;
  config.bars_per_section = bars_per_section;
  config.arc = createDefaultArcConfig(num_sections);
  config.cadence.cadence_bars = bars_per_section;  // Last section is cadence
  return config;
}

/// @brief Create a HarmonicTimeline with one event per bar, alternating I and V.
/// @param num_bars Total number of bars.
/// @param root_pitch MIDI pitch of the tonic root.
/// @return Timeline covering the specified bars.
HarmonicTimeline createTestTimeline(int num_bars, uint8_t root_pitch = 48) {
  HarmonicTimeline timeline;
  for (int bar = 0; bar < num_bars; ++bar) {
    HarmonicEvent event;
    event.tick = static_cast<Tick>(bar) * kTicksPerBar;
    event.end_tick = event.tick + kTicksPerBar;
    event.key = Key::C;
    event.is_minor = false;

    if (bar % 2 == 0) {
      // I chord (C major)
      event.chord.degree = ChordDegree::I;
      event.chord.quality = ChordQuality::Major;
      event.chord.root_pitch = root_pitch;
    } else {
      // V chord (G major)
      event.chord.degree = ChordDegree::V;
      event.chord.quality = ChordQuality::Major;
      event.chord.root_pitch = static_cast<uint8_t>(root_pitch + 7);
    }

    event.bass_pitch = event.chord.root_pitch;
    event.weight = (bar % 4 == 0) ? 1.5f : ((bar % 4 == 2) ? 0.5f : 1.0f);
    event.is_immutable = false;
    timeline.addEvent(event);
  }
  return timeline;
}

/// @brief Generate a continuous stream of 16th notes (4 per beat) across sections.
///
/// Creates notes that are chord tones of the provided root_pitch,
/// with gradually expanding register for Ascent bars and contracting
/// for Descent bars. Uses bar-level granularity to ensure bar-to-bar
/// register range is monotonically non-decreasing in Ascent and
/// non-increasing in Descent (satisfying arc prohibition rules).
///
/// @param config Config to generate notes for.
/// @param timeline Timeline used for chord tone alignment.
/// @return Single track with notes.
Track createIdealTrack(const ArpeggioFlowConfig& config,
                       const HarmonicTimeline& timeline) {
  Track track;
  track.channel = 0;
  track.program = 42;  // Cello
  track.name = "Cello";

  Tick note_duration = kTicksPerBeat / 4;  // 16th note = 120 ticks
  int total_bars = config.num_sections * config.bars_per_section;

  // Find peak bar index for register arc shaping.
  int peak_bar_start = 0;
  int peak_bar_end = total_bars;
  for (const auto& [sec_id, sec_phase] : config.arc.phase_assignment) {
    if (sec_phase == ArcPhase::Peak) {
      peak_bar_start = static_cast<int>(sec_id) * config.bars_per_section;
      peak_bar_end = peak_bar_start + config.bars_per_section;
      break;
    }
  }

  for (int bar = 0; bar < total_bars; ++bar) {
    // Get the section this bar belongs to.
    int section = bar / config.bars_per_section;
    ArcPhase phase = ArcPhase::Ascent;
    for (const auto& [sec_id, sec_phase] : config.arc.phase_assignment) {
      if (static_cast<int>(sec_id) == section) {
        phase = sec_phase;
        break;
      }
    }

    // Determine register range using bar-level granularity.
    // This ensures monotonic non-decreasing in Ascent and non-increasing in Descent.
    // Base range: 12 semitones (one octave) centered around C3-C4.
    // Ascent: expand by 1 semitone per bar (both directions).
    // Peak: maximum range (36 semitones = 3 octaves).
    // Descent: contract by 1 semitone per bar from peak range.
    uint8_t base_low = 48;   // C3
    uint8_t base_high = 60;  // C4

    switch (phase) {
      case ArcPhase::Ascent: {
        // Expand gradually per bar: bar 0 = 12 semitones, bar N = 12 + N*2.
        int expansion = bar;
        base_low = static_cast<uint8_t>(48 - expansion);
        base_high = static_cast<uint8_t>(60 + expansion);
        break;
      }
      case ArcPhase::Peak:
        // Maximum range.
        base_low = 36;   // C2
        base_high = 72;  // C5
        break;
      case ArcPhase::Descent: {
        // Contract gradually per bar from the start of descent.
        int bars_into_descent = bar - peak_bar_end;
        if (bars_into_descent < 0) bars_into_descent = 0;
        base_low = static_cast<uint8_t>(38 + bars_into_descent);
        base_high = static_cast<uint8_t>(70 - bars_into_descent);
        break;
      }
    }

    if (base_low > base_high) {
      base_low = 48;
      base_high = 60;
    }

    // Get chord root for this bar.
    Tick bar_start = static_cast<Tick>(bar) * kTicksPerBar;
    const HarmonicEvent& harm_event = timeline.getAt(bar_start);
    uint8_t root = harm_event.chord.root_pitch;
    int root_pc = root % 12;

    // Generate 16 notes per bar (4 per beat * 4 beats).
    // First and last notes explicitly use base_low and base_high to guarantee
    // the exact register range.
    for (int note_idx = 0; note_idx < 16; ++note_idx) {
      NoteEvent note;
      note.start_tick = bar_start + static_cast<Tick>(note_idx) * note_duration;
      note.duration = note_duration;
      note.velocity = 80;
      note.voice = 0;

      if (note_idx == 0) {
        // Force the lowest note to pin the register minimum.
        note.pitch = base_low;
      } else if (note_idx == 15) {
        // Force the highest note to pin the register maximum.
        note.pitch = base_high;
      } else {
        // Cycle through chord tones: root, 3rd, 5th, root-octave.
        int intervals[] = {0, 4, 7, 12};
        if (harm_event.chord.quality == ChordQuality::Minor) {
          intervals[1] = 3;  // Minor 3rd
        }
        int interval = intervals[note_idx % 4];
        int pitch = root_pc + interval;

        // Place in register range.
        while (pitch < static_cast<int>(base_low)) pitch += 12;
        while (pitch > static_cast<int>(base_high)) pitch -= 12;
        if (pitch < static_cast<int>(base_low)) pitch = base_low;

        note.pitch = static_cast<uint8_t>(pitch);
      }

      track.notes.push_back(note);
    }
  }

  return track;
}

/// @brief Create a track with a single note per bar at the same pitch (minimal content).
Track createMinimalTrack(const ArpeggioFlowConfig& config) {
  Track track;
  track.channel = 0;
  track.name = "Minimal";

  int total_bars = config.num_sections * config.bars_per_section;
  for (int bar = 0; bar < total_bars; ++bar) {
    NoteEvent note;
    note.start_tick = static_cast<Tick>(bar) * kTicksPerBar;
    note.duration = kTicksPerBar;
    note.pitch = 60;  // Middle C, always the same
    note.velocity = 80;
    note.voice = 0;
    track.notes.push_back(note);
  }

  return track;
}

/// @brief Create a track with proper cadence characteristics.
///
/// Generates an ideal flow track but modifies the final cadence_bars to have
/// a lower register and narrower range.
Track createTrackWithGoodCadence(const ArpeggioFlowConfig& config,
                                 const HarmonicTimeline& timeline) {
  auto track = createIdealTrack(config, timeline);
  int total_bars = config.num_sections * config.bars_per_section;
  int cadence_start_bar = total_bars - config.cadence.cadence_bars;

  // Lower all cadence notes.
  for (auto& note : track.notes) {
    int bar = static_cast<int>(note.start_tick / kTicksPerBar);
    if (bar >= cadence_start_bar) {
      // Move to lower register.
      while (note.pitch > 55) note.pitch -= 12;
      if (note.pitch < 36) note.pitch = 36;
    }
  }

  return track;
}

// ===========================================================================
// Structural metrics (Instant-FAIL) tests
// ===========================================================================

TEST(FlowAnalyzerTest, GlobalArcScorePassesWithValidConfig) {
  auto config = createTestConfig();
  auto timeline = createTestTimeline(config.num_sections * config.bars_per_section);
  auto track = createIdealTrack(config, timeline);

  auto result = analyzeFlow({track}, config, timeline);
  EXPECT_FLOAT_EQ(result.global_arc_score, 1.0f);
}

TEST(FlowAnalyzerTest, GlobalArcScoreFailsWithEmptyArc) {
  auto config = createTestConfig();
  config.arc.phase_assignment.clear();  // Break the arc

  auto timeline = createTestTimeline(config.num_sections * config.bars_per_section);
  auto track = createMinimalTrack(config);

  auto result = analyzeFlow({track}, config, timeline);
  EXPECT_FLOAT_EQ(result.global_arc_score, 0.0f);
  EXPECT_FALSE(result.isPass());
}

TEST(FlowAnalyzerTest, GlobalArcScoreFailsWithWrongSectionCount) {
  auto config = createTestConfig();
  // Remove one assignment to create mismatch.
  config.arc.phase_assignment.pop_back();

  auto timeline = createTestTimeline(config.num_sections * config.bars_per_section);
  auto track = createMinimalTrack(config);

  auto result = analyzeFlow({track}, config, timeline);
  EXPECT_FLOAT_EQ(result.global_arc_score, 0.0f);
}

TEST(FlowAnalyzerTest, PeakUniquenessPassesWithExactlyOnePeak) {
  auto config = createTestConfig();
  auto timeline = createTestTimeline(config.num_sections * config.bars_per_section);
  auto track = createIdealTrack(config, timeline);

  auto result = analyzeFlow({track}, config, timeline);
  EXPECT_FLOAT_EQ(result.peak_uniqueness_score, 1.0f);
}

TEST(FlowAnalyzerTest, PeakUniquenessFailsWithNoPeak) {
  auto config = createTestConfig();
  // Replace all phases with Ascent (no Peak).
  for (auto& [sec_id, phase] : config.arc.phase_assignment) {
    phase = ArcPhase::Ascent;
  }

  auto timeline = createTestTimeline(config.num_sections * config.bars_per_section);
  auto track = createMinimalTrack(config);

  auto result = analyzeFlow({track}, config, timeline);
  EXPECT_FLOAT_EQ(result.peak_uniqueness_score, 0.0f);
  EXPECT_FALSE(result.isPass());
}

TEST(FlowAnalyzerTest, PeakUniquenessFailsWithTwoPeaks) {
  auto config = createTestConfig();
  // Set two sections to Peak.
  int peak_set = 0;
  for (auto& [sec_id, phase] : config.arc.phase_assignment) {
    if (phase == ArcPhase::Ascent && peak_set == 0) {
      // Keep first Ascent
    } else if (phase == ArcPhase::Peak || (peak_set < 2 && sec_id >= 2)) {
      phase = ArcPhase::Peak;
      ++peak_set;
    }
    if (peak_set >= 2) break;
  }

  auto timeline = createTestTimeline(config.num_sections * config.bars_per_section);
  auto track = createMinimalTrack(config);

  auto result = analyzeFlow({track}, config, timeline);
  EXPECT_FLOAT_EQ(result.peak_uniqueness_score, 0.0f);
}

TEST(FlowAnalyzerTest, DramaturgicOrderPassesWithValidPhaseOrder) {
  auto config = createTestConfig();
  auto timeline = createTestTimeline(config.num_sections * config.bars_per_section);
  auto track = createIdealTrack(config, timeline);

  auto result = analyzeFlow({track}, config, timeline);
  EXPECT_FLOAT_EQ(result.dramaturgic_order_score, 1.0f);
}

TEST(FlowAnalyzerTest, DramaturgicOrderFailsWithReversedPhases) {
  auto config = createTestConfig(3, 4);
  // Reverse: Descent, Peak, Ascent
  config.arc.phase_assignment = {
    {0, ArcPhase::Descent},
    {1, ArcPhase::Peak},
    {2, ArcPhase::Ascent}
  };

  auto timeline = createTestTimeline(12);
  auto track = createMinimalTrack(config);

  auto result = analyzeFlow({track}, config, timeline);
  EXPECT_FLOAT_EQ(result.dramaturgic_order_score, 0.0f);
  EXPECT_FALSE(result.isPass());
}

// ===========================================================================
// Threshold metric tests
// ===========================================================================

TEST(FlowAnalyzerTest, HarmonicMotionScoreWithAllChordTones) {
  auto config = createTestConfig();
  auto timeline = createTestTimeline(config.num_sections * config.bars_per_section, 48);
  auto track = createIdealTrack(config, timeline);

  auto result = analyzeFlow({track}, config, timeline);
  // Ideal track uses chord tones, should score well.
  EXPECT_GE(result.harmonic_motion_score, 0.5f);
}

TEST(FlowAnalyzerTest, HarmonicMotionScoreWithNonChordTones) {
  auto config = createTestConfig();
  auto timeline = createTestTimeline(config.num_sections * config.bars_per_section, 48);

  // Create a track where all notes are non-chord tones (pitch class 1 = C#).
  Track track;
  track.channel = 0;
  int total_bars = config.num_sections * config.bars_per_section;
  for (int bar = 0; bar < total_bars; ++bar) {
    NoteEvent note;
    note.start_tick = static_cast<Tick>(bar) * kTicksPerBar;
    note.duration = kTicksPerBar;
    note.pitch = 49;  // C#3, not a chord tone of C major or G major
    note.velocity = 80;
    note.voice = 0;
    track.notes.push_back(note);
  }

  auto result = analyzeFlow({track}, config, timeline);
  EXPECT_LT(result.harmonic_motion_score, 0.5f);
}

TEST(FlowAnalyzerTest, PhraseContinuityScoreWithContinuousStream) {
  auto config = createTestConfig();
  auto timeline = createTestTimeline(config.num_sections * config.bars_per_section);
  auto track = createIdealTrack(config, timeline);

  auto result = analyzeFlow({track}, config, timeline);
  // Ideal track has no gaps (16th notes back to back).
  EXPECT_GE(result.phrase_continuity_score, 0.95f);
}

TEST(FlowAnalyzerTest, PhraseContinuityScoreWithLargeGaps) {
  auto config = createTestConfig();
  auto timeline = createTestTimeline(config.num_sections * config.bars_per_section);

  // Create a track with large gaps: one short note per bar.
  Track track;
  track.channel = 0;
  int total_bars = config.num_sections * config.bars_per_section;
  for (int bar = 0; bar < total_bars; ++bar) {
    NoteEvent note;
    note.start_tick = static_cast<Tick>(bar) * kTicksPerBar;
    note.duration = kTicksPerBeat / 4;  // 120 ticks only
    note.pitch = 60;
    note.velocity = 80;
    note.voice = 0;
    track.notes.push_back(note);
  }

  auto result = analyzeFlow({track}, config, timeline);
  // Massive gaps between notes -- should fail continuity.
  EXPECT_LT(result.phrase_continuity_score, 0.95f);
}

TEST(FlowAnalyzerTest, PatternNaturalnessWithVariedIntervals) {
  auto config = createTestConfig();
  auto timeline = createTestTimeline(config.num_sections * config.bars_per_section);
  auto track = createIdealTrack(config, timeline);

  auto result = analyzeFlow({track}, config, timeline);
  // Ideal track cycles through chord tones giving interval variety.
  EXPECT_GT(result.pattern_naturalness_score, 0.0f);
}

TEST(FlowAnalyzerTest, PatternNaturalnessWithMonotonePitch) {
  auto config = createTestConfig();
  auto timeline = createTestTimeline(config.num_sections * config.bars_per_section);

  // All notes at the same pitch -- zero interval variety.
  Track track;
  track.channel = 0;
  Tick note_dur = kTicksPerBeat / 4;
  int total_notes = config.num_sections * config.bars_per_section * 16;
  for (int idx = 0; idx < total_notes; ++idx) {
    NoteEvent note;
    note.start_tick = static_cast<Tick>(idx) * note_dur;
    note.duration = note_dur;
    note.pitch = 60;  // Always the same
    note.velocity = 80;
    note.voice = 0;
    track.notes.push_back(note);
  }

  auto result = analyzeFlow({track}, config, timeline);
  // Only interval 0 repeated -- should score poorly (only 1 distinct interval out of 6).
  EXPECT_LT(result.pattern_naturalness_score, 0.3f);
}

TEST(FlowAnalyzerTest, CadenceScoreWithLowerFinalBars) {
  auto config = createTestConfig();
  auto timeline = createTestTimeline(config.num_sections * config.bars_per_section);
  auto track = createTrackWithGoodCadence(config, timeline);

  auto result = analyzeFlow({track}, config, timeline);
  // Good cadence: lower register in final bars.
  EXPECT_GE(result.cadence_score, 0.7f);
}

TEST(FlowAnalyzerTest, WeightUtilizationWithNoExtremeWeights) {
  auto config = createTestConfig();

  // Create timeline with all weights = 1.0 (no extreme weights).
  HarmonicTimeline timeline;
  int total_bars = config.num_sections * config.bars_per_section;
  for (int bar = 0; bar < total_bars; ++bar) {
    HarmonicEvent event;
    event.tick = static_cast<Tick>(bar) * kTicksPerBar;
    event.end_tick = event.tick + kTicksPerBar;
    event.key = Key::C;
    event.chord.degree = ChordDegree::I;
    event.chord.quality = ChordQuality::Major;
    event.chord.root_pitch = 48;
    event.bass_pitch = 48;
    event.weight = 1.0f;  // All normal weight
    timeline.addEvent(event);
  }

  auto track = createIdealTrack(config, timeline);
  auto result = analyzeFlow({track}, config, timeline);
  // No extreme weights -> neutral credit (0.8).
  EXPECT_GE(result.weight_utilization_score, 0.6f);
}

// ===========================================================================
// Arc prohibition tests
// ===========================================================================

TEST(FlowAnalyzerTest, ArcProhibitionZeroWithWellBehavedTrack) {
  auto config = createTestConfig();
  auto timeline = createTestTimeline(config.num_sections * config.bars_per_section);
  auto track = createIdealTrack(config, timeline);

  auto result = analyzeFlow({track}, config, timeline);
  // Ideal track has expanding register in Ascent and contracting in Descent.
  EXPECT_EQ(result.arc_prohibition_score, 0.0f);
}

TEST(FlowAnalyzerTest, ArcProhibitionDetectsRegisterShrinkInAscent) {
  auto config = createTestConfig(3, 2);  // 3 sections, 2 bars each
  config.arc = createDefaultArcConfig(3);
  auto timeline = createTestTimeline(6);

  // Create a track where Ascent section has shrinking register between bars.
  Track track;
  track.channel = 0;
  Tick note_dur = kTicksPerBeat / 4;

  // Section 0 (Ascent), Bar 0: wide register (C3-C5 = 24 semitones).
  for (int idx = 0; idx < 16; ++idx) {
    NoteEvent note;
    note.start_tick = static_cast<Tick>(idx) * note_dur;
    note.duration = note_dur;
    note.pitch = (idx % 2 == 0) ? 48 : 72;  // C3 and C5
    note.velocity = 80;
    note.voice = 0;
    track.notes.push_back(note);
  }

  // Section 0 (Ascent), Bar 1: narrow register (E3-G3 = 3 semitones).
  for (int idx = 0; idx < 16; ++idx) {
    NoteEvent note;
    note.start_tick = kTicksPerBar + static_cast<Tick>(idx) * note_dur;
    note.duration = note_dur;
    note.pitch = (idx % 2 == 0) ? 52 : 55;  // E3 and G3
    note.velocity = 80;
    note.voice = 0;
    track.notes.push_back(note);
  }

  // Sections 1 and 2 (Peak, Descent): fill with basic notes.
  for (int bar = 2; bar < 6; ++bar) {
    for (int idx = 0; idx < 16; ++idx) {
      NoteEvent note;
      note.start_tick = static_cast<Tick>(bar) * kTicksPerBar +
                         static_cast<Tick>(idx) * note_dur;
      note.duration = note_dur;
      note.pitch = 60;
      note.velocity = 80;
      note.voice = 0;
      track.notes.push_back(note);
    }
  }

  auto result = analyzeFlow({track}, config, timeline);
  // Bar 1 has narrower register than Bar 0 in Ascent -- violation.
  EXPECT_GT(result.arc_prohibition_score, 0.0f);
}

// ===========================================================================
// isPass / getFailures / summary tests
// ===========================================================================

TEST(FlowAnalysisResultTest, DefaultResultFails) {
  FlowAnalysisResult result;
  EXPECT_FALSE(result.isPass());
}

TEST(FlowAnalysisResultTest, GetFailuresListsAllDefaults) {
  FlowAnalysisResult result;
  auto failures = result.getFailures();
  // All metrics at 0.0 should be failing.
  EXPECT_GE(failures.size(), 8u);  // At least the 4 instant-FAIL + 4 threshold
}

TEST(FlowAnalysisResultTest, PassingResultHasNoFailures) {
  FlowAnalysisResult result;
  result.harmonic_motion_score = 0.8f;
  result.register_expansion_score = 0.5f;
  result.phrase_continuity_score = 0.99f;
  result.pattern_naturalness_score = 0.85f;
  result.cadence_score = 0.9f;
  result.weight_utilization_score = 0.75f;
  result.global_arc_score = 1.0f;
  result.peak_uniqueness_score = 1.0f;
  result.arc_prohibition_score = 0.0f;
  result.dramaturgic_order_score = 1.0f;

  EXPECT_TRUE(result.isPass());
  EXPECT_TRUE(result.getFailures().empty());
}

TEST(FlowAnalysisResultTest, SingleThresholdFailure) {
  FlowAnalysisResult result;
  result.harmonic_motion_score = 0.4f;  // Below 0.5 threshold
  result.register_expansion_score = 0.5f;
  result.phrase_continuity_score = 0.99f;
  result.pattern_naturalness_score = 0.85f;
  result.cadence_score = 0.9f;
  result.weight_utilization_score = 0.75f;
  result.global_arc_score = 1.0f;
  result.peak_uniqueness_score = 1.0f;
  result.arc_prohibition_score = 0.0f;
  result.dramaturgic_order_score = 1.0f;

  EXPECT_FALSE(result.isPass());
  auto failures = result.getFailures();
  ASSERT_EQ(failures.size(), 1u);
  EXPECT_NE(failures[0].find("harmonic_motion_score"), std::string::npos);
}

TEST(FlowAnalysisResultTest, InstantFailOverridesThresholds) {
  FlowAnalysisResult result;
  // All thresholds pass.
  result.harmonic_motion_score = 0.8f;
  result.register_expansion_score = 0.5f;
  result.phrase_continuity_score = 0.99f;
  result.pattern_naturalness_score = 0.85f;
  result.cadence_score = 0.9f;
  result.weight_utilization_score = 0.75f;
  // But instant-FAIL.
  result.global_arc_score = 0.0f;
  result.peak_uniqueness_score = 1.0f;
  result.arc_prohibition_score = 0.0f;
  result.dramaturgic_order_score = 1.0f;

  EXPECT_FALSE(result.isPass());
  auto failures = result.getFailures();
  ASSERT_EQ(failures.size(), 1u);
  EXPECT_NE(failures[0].find("global_arc_score"), std::string::npos);
}

TEST(FlowAnalysisResultTest, ArcProhibitionGreaterThanZeroFails) {
  FlowAnalysisResult result;
  result.harmonic_motion_score = 0.8f;
  result.register_expansion_score = 0.5f;
  result.phrase_continuity_score = 0.99f;
  result.pattern_naturalness_score = 0.85f;
  result.cadence_score = 0.9f;
  result.weight_utilization_score = 0.75f;
  result.global_arc_score = 1.0f;
  result.peak_uniqueness_score = 1.0f;
  result.arc_prohibition_score = 3.0f;  // 3 violations
  result.dramaturgic_order_score = 1.0f;

  EXPECT_FALSE(result.isPass());
}

TEST(FlowAnalysisResultTest, SummaryContainsPassOrFail) {
  FlowAnalysisResult result;
  std::string sum = result.summary();
  EXPECT_NE(sum.find("FAIL"), std::string::npos);

  // Set all passing.
  result.harmonic_motion_score = 0.8f;
  result.register_expansion_score = 0.5f;
  result.phrase_continuity_score = 0.99f;
  result.pattern_naturalness_score = 0.85f;
  result.cadence_score = 0.9f;
  result.weight_utilization_score = 0.75f;
  result.global_arc_score = 1.0f;
  result.peak_uniqueness_score = 1.0f;
  result.arc_prohibition_score = 0.0f;
  result.dramaturgic_order_score = 1.0f;

  sum = result.summary();
  EXPECT_NE(sum.find("PASS"), std::string::npos);
}

// ===========================================================================
// Edge case tests
// ===========================================================================

TEST(FlowAnalyzerTest, EmptyTracksReturnZeroScores) {
  auto config = createTestConfig();
  auto timeline = createTestTimeline(config.num_sections * config.bars_per_section);

  auto result = analyzeFlow({}, config, timeline);
  EXPECT_FLOAT_EQ(result.harmonic_motion_score, 0.0f);
  EXPECT_FLOAT_EQ(result.phrase_continuity_score, 0.0f);
  EXPECT_FLOAT_EQ(result.pattern_naturalness_score, 0.0f);
}

TEST(FlowAnalyzerTest, EmptyTimelineGivesZeroHarmonicScores) {
  auto config = createTestConfig();
  HarmonicTimeline empty_timeline;
  auto track = createMinimalTrack(config);

  auto result = analyzeFlow({track}, config, empty_timeline);
  EXPECT_FLOAT_EQ(result.harmonic_motion_score, 0.0f);
}

TEST(FlowAnalyzerTest, ThreeSectionConfigWorks) {
  auto config = createTestConfig(3, 4);
  auto timeline = createTestTimeline(12);
  auto track = createIdealTrack(config, timeline);

  auto result = analyzeFlow({track}, config, timeline);
  EXPECT_FLOAT_EQ(result.global_arc_score, 1.0f);
  EXPECT_FLOAT_EQ(result.peak_uniqueness_score, 1.0f);
  EXPECT_FLOAT_EQ(result.dramaturgic_order_score, 1.0f);
}

TEST(FlowAnalyzerTest, MultipleTracksAreMerged) {
  auto config = createTestConfig(3, 2);
  auto timeline = createTestTimeline(6);

  // Create two tracks that together cover the piece.
  Track track_a;
  track_a.channel = 0;
  Track track_b;
  track_b.channel = 0;

  Tick note_dur = kTicksPerBeat / 4;
  for (int bar = 0; bar < 6; ++bar) {
    for (int idx = 0; idx < 16; ++idx) {
      NoteEvent note;
      note.start_tick = static_cast<Tick>(bar) * kTicksPerBar +
                         static_cast<Tick>(idx) * note_dur;
      note.duration = note_dur;
      note.pitch = 60;
      note.velocity = 80;
      note.voice = 0;

      if (bar < 3) {
        track_a.notes.push_back(note);
      } else {
        track_b.notes.push_back(note);
      }
    }
  }

  auto result = analyzeFlow({track_a, track_b}, config, timeline);
  // Should analyze all notes from both tracks.
  EXPECT_GE(result.phrase_continuity_score, 0.95f);
}

TEST(FlowAnalyzerTest, RegisterExpansionScoreWithFlatRegister) {
  auto config = createTestConfig();
  auto timeline = createTestTimeline(config.num_sections * config.bars_per_section);

  // All notes at the same pitch -- register range is 0 for all sections.
  auto track = createMinimalTrack(config);
  auto result = analyzeFlow({track}, config, timeline);

  // Flat register across all sections: peak is not "widest" (tied at 0),
  // but monotonic checks pass trivially (0 >= 0). Score depends on details.
  // Just verify it does not crash and returns a valid float.
  EXPECT_GE(result.register_expansion_score, 0.0f);
  EXPECT_LE(result.register_expansion_score, 1.0f);
}

}  // namespace
}  // namespace bach
