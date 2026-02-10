// Tests for GlobalArc integration -- arc structure validation, register evolution,
// and how the harmonic arpeggio engine uses the global arc during generation.

#include "solo_string/flow/arpeggio_flow_config.h"
#include "solo_string/flow/harmonic_arpeggio_engine.h"

#include <algorithm>
#include <cstdint>
#include <map>
#include <vector>

#include <gtest/gtest.h>

#include "core/basic_types.h"

namespace bach {
namespace {

// ===========================================================================
// Test helpers
// ===========================================================================

/// @brief Build a valid ArpeggioFlowConfig with given parameters.
ArpeggioFlowConfig makeArcTestConfig(int num_sections = 6,
                                      int bars_per_section = 4,
                                      uint32_t seed = 42) {
  ArpeggioFlowConfig config;
  config.num_sections = num_sections;
  config.bars_per_section = bars_per_section;
  config.seed = seed;
  config.instrument = InstrumentType::Cello;
  config.key = {Key::G, true};
  config.bpm = 66;
  config.arc = createDefaultArcConfig(num_sections);
  config.cadence.cadence_bars = bars_per_section;
  return config;
}

/// @brief Compute the register range (max - min pitch) for notes in a given section.
///
/// @param notes All notes from the generated track.
/// @param section_idx 0-based section index.
/// @param bars_per_section Number of bars per section.
/// @return The register range in semitones, or -1 if no notes in the section.
int computeSectionRegisterRange(const std::vector<NoteEvent>& notes,
                                int section_idx, int bars_per_section) {
  Tick section_start = static_cast<Tick>(section_idx * bars_per_section) * kTicksPerBar;
  Tick section_end = section_start +
                     static_cast<Tick>(bars_per_section) * kTicksPerBar;

  uint8_t min_pitch = 127;
  uint8_t max_pitch = 0;
  bool found = false;

  for (const auto& note : notes) {
    if (note.start_tick >= section_start && note.start_tick < section_end) {
      if (note.pitch < min_pitch) min_pitch = note.pitch;
      if (note.pitch > max_pitch) max_pitch = note.pitch;
      found = true;
    }
  }

  if (!found) return -1;
  return static_cast<int>(max_pitch) - static_cast<int>(min_pitch);
}

/// @brief Get the maximum pitch within a section.
uint8_t getMaxPitchInSection(const std::vector<NoteEvent>& notes,
                             int section_idx, int bars_per_section) {
  Tick section_start = static_cast<Tick>(section_idx * bars_per_section) * kTicksPerBar;
  Tick section_end = section_start +
                     static_cast<Tick>(bars_per_section) * kTicksPerBar;

  uint8_t max_pitch = 0;
  for (const auto& note : notes) {
    if (note.start_tick >= section_start && note.start_tick < section_end) {
      if (note.pitch > max_pitch) max_pitch = note.pitch;
    }
  }
  return max_pitch;
}

// ===========================================================================
// createDefaultArcConfig structure tests
// ===========================================================================

TEST(GlobalArcTest, DefaultArcConfigHasCorrectStructure) {
  auto config = createDefaultArcConfig(6);

  ASSERT_EQ(config.phase_assignment.size(), 6u);
  EXPECT_TRUE(validateGlobalArcConfig(config));

  // Should have at least one of each phase.
  bool has_ascent = false;
  bool has_peak = false;
  bool has_descent = false;
  for (const auto& [sec_id, phase] : config.phase_assignment) {
    if (phase == ArcPhase::Ascent) has_ascent = true;
    if (phase == ArcPhase::Peak) has_peak = true;
    if (phase == ArcPhase::Descent) has_descent = true;
  }
  EXPECT_TRUE(has_ascent);
  EXPECT_TRUE(has_peak);
  EXPECT_TRUE(has_descent);
}

// ===========================================================================
// Peak uniqueness tests across section counts
// ===========================================================================

TEST(GlobalArcTest, PeakIsAlwaysExactlyOneSectionForVariousCounts) {
  for (int num_sections = 3; num_sections <= 10; ++num_sections) {
    auto config = createDefaultArcConfig(num_sections);
    ASSERT_EQ(config.phase_assignment.size(), static_cast<size_t>(num_sections))
        << "Wrong size for num_sections=" << num_sections;

    int peak_count = 0;
    for (const auto& [sec_id, phase] : config.phase_assignment) {
      if (phase == ArcPhase::Peak) {
        ++peak_count;
      }
    }

    EXPECT_EQ(peak_count, 1)
        << "Expected exactly 1 Peak section for num_sections=" << num_sections
        << ", got " << peak_count;
  }
}

// ===========================================================================
// Phase order monotonicity tests
// ===========================================================================

TEST(GlobalArcTest, PhaseOrderIsMonotonic) {
  for (int num_sections = 3; num_sections <= 10; ++num_sections) {
    auto config = createDefaultArcConfig(num_sections);
    ASSERT_TRUE(validateGlobalArcConfig(config))
        << "Invalid config for num_sections=" << num_sections;

    // Verify that Ascent sections come before Peak, and Peak comes before Descent.
    bool seen_peak = false;
    bool seen_descent = false;

    for (const auto& [sec_id, phase] : config.phase_assignment) {
      if (phase == ArcPhase::Peak) {
        EXPECT_FALSE(seen_descent)
            << "Peak found after Descent for num_sections=" << num_sections;
        seen_peak = true;
      } else if (phase == ArcPhase::Descent) {
        EXPECT_TRUE(seen_peak)
            << "Descent found before Peak for num_sections=" << num_sections;
        seen_descent = true;
      } else if (phase == ArcPhase::Ascent) {
        EXPECT_FALSE(seen_peak)
            << "Ascent found after Peak for num_sections=" << num_sections;
        EXPECT_FALSE(seen_descent)
            << "Ascent found after Descent for num_sections=" << num_sections;
      }
    }
  }
}

// ===========================================================================
// Engine rejects invalid arc tests
// ===========================================================================

TEST(GlobalArcTest, EngineRejectsInvalidArcInConfig) {
  ArpeggioFlowConfig config;
  config.num_sections = 4;
  config.bars_per_section = 4;
  config.seed = 42;

  // Two peaks -- invalid.
  config.arc.phase_assignment = {
      {0, ArcPhase::Ascent},
      {1, ArcPhase::Peak},
      {2, ArcPhase::Peak},
      {3, ArcPhase::Descent}
  };

  auto result = generateArpeggioFlow(config);
  EXPECT_FALSE(result.success);
  EXPECT_FALSE(result.error_message.empty());
}

TEST(GlobalArcTest, EngineRejectsReversedArc) {
  ArpeggioFlowConfig config;
  config.num_sections = 3;
  config.bars_per_section = 4;
  config.seed = 42;

  config.arc.phase_assignment = {
      {0, ArcPhase::Descent},
      {1, ArcPhase::Peak},
      {2, ArcPhase::Ascent}
  };

  auto result = generateArpeggioFlow(config);
  EXPECT_FALSE(result.success);
}

TEST(GlobalArcTest, EngineUsesDefaultArcWhenConfigArcIsEmpty) {
  ArpeggioFlowConfig config;
  config.num_sections = 6;
  config.bars_per_section = 4;
  config.seed = 42;
  config.arc = {};  // Empty

  auto result = generateArpeggioFlow(config);
  EXPECT_TRUE(result.success) << result.error_message;
  EXPECT_EQ(result.tracks.size(), 1u);
  EXPECT_FALSE(result.tracks[0].notes.empty());
}

// ===========================================================================
// Register follows arc shape tests
// ===========================================================================

TEST(GlobalArcTest, PeakSectionHasWidestRegister) {
  auto config = makeArcTestConfig(6, 4, 42);
  auto result = generateArpeggioFlow(config);
  ASSERT_TRUE(result.success) << result.error_message;
  ASSERT_EQ(result.tracks.size(), 1u);

  const auto& notes = result.tracks[0].notes;

  // Find which section is Peak.
  int peak_section = -1;
  for (const auto& [sec_id, phase] : config.arc.phase_assignment) {
    if (phase == ArcPhase::Peak) {
      peak_section = static_cast<int>(sec_id);
      break;
    }
  }
  ASSERT_GE(peak_section, 0);

  int peak_range = computeSectionRegisterRange(notes, peak_section,
                                                config.bars_per_section);
  ASSERT_GE(peak_range, 0) << "No notes in peak section";

  // The peak section should have a register range at least as wide as every
  // non-cadence section. We exclude the last section since the final bar has
  // a special cadence generator that may use a different register.
  for (int sec = 0; sec < config.num_sections - 1; ++sec) {
    if (sec == peak_section) continue;

    int sec_range = computeSectionRegisterRange(notes, sec, config.bars_per_section);
    if (sec_range < 0) continue;  // No notes

    EXPECT_GE(peak_range, sec_range)
        << "Peak section (" << peak_section << ") register range (" << peak_range
        << ") is narrower than section " << sec << " range (" << sec_range << ")";
  }
}

TEST(GlobalArcTest, AscentMaxPitchGenerallyIncreases) {
  // Use a larger piece for a clearer trend.
  auto config = makeArcTestConfig(8, 4, 42);
  auto result = generateArpeggioFlow(config);
  ASSERT_TRUE(result.success) << result.error_message;
  ASSERT_EQ(result.tracks.size(), 1u);

  const auto& notes = result.tracks[0].notes;

  // Collect Ascent sections.
  std::vector<int> ascent_sections;
  for (const auto& [sec_id, phase] : config.arc.phase_assignment) {
    if (phase == ArcPhase::Ascent) {
      ascent_sections.push_back(static_cast<int>(sec_id));
    }
  }

  // With a well-formed arc, the max pitch should generally trend upward.
  // We check that the last Ascent section's max pitch is at least as high
  // as the first Ascent section's max pitch.
  if (ascent_sections.size() >= 2) {
    uint8_t first_max = getMaxPitchInSection(notes, ascent_sections.front(),
                                              config.bars_per_section);
    uint8_t last_max = getMaxPitchInSection(notes, ascent_sections.back(),
                                             config.bars_per_section);

    EXPECT_GE(last_max, first_max)
        << "Max pitch in last Ascent section (" << static_cast<int>(last_max)
        << ") should be >= first Ascent section (" << static_cast<int>(first_max) << ")";
  }
}

TEST(GlobalArcTest, DescentMaxPitchGenerallyDecreases) {
  // Use a piece with enough descent sections.
  auto config = makeArcTestConfig(8, 4, 42);
  auto result = generateArpeggioFlow(config);
  ASSERT_TRUE(result.success) << result.error_message;
  ASSERT_EQ(result.tracks.size(), 1u);

  const auto& notes = result.tracks[0].notes;

  // Collect Descent sections (excluding the last one which has the final bar).
  std::vector<int> descent_sections;
  for (const auto& [sec_id, phase] : config.arc.phase_assignment) {
    if (phase == ArcPhase::Descent &&
        static_cast<int>(sec_id) < config.num_sections - 1) {
      descent_sections.push_back(static_cast<int>(sec_id));
    }
  }

  // The max pitch should generally trend downward in Descent.
  // We check that the first Descent section has higher max pitch
  // than the last non-final Descent section.
  if (descent_sections.size() >= 2) {
    uint8_t first_max = getMaxPitchInSection(notes, descent_sections.front(),
                                              config.bars_per_section);
    uint8_t last_max = getMaxPitchInSection(notes, descent_sections.back(),
                                             config.bars_per_section);

    EXPECT_GE(first_max, last_max)
        << "Max pitch in first Descent section (" << static_cast<int>(first_max)
        << ") should be >= last Descent section (" << static_cast<int>(last_max) << ")";
  }
}

// ===========================================================================
// Register range follows arc -- numeric verification
// ===========================================================================

TEST(GlobalArcTest, RegisterRangeNarrowerAtExtremesThanAtPeak) {
  auto config = makeArcTestConfig(6, 4, 42);
  auto result = generateArpeggioFlow(config);
  ASSERT_TRUE(result.success) << result.error_message;
  ASSERT_EQ(result.tracks.size(), 1u);

  const auto& notes = result.tracks[0].notes;

  // Find phase for each section.
  int peak_section = -1;
  int first_ascent = -1;
  int last_descent = -1;

  for (const auto& [sec_id, phase] : config.arc.phase_assignment) {
    if (phase == ArcPhase::Peak) {
      peak_section = static_cast<int>(sec_id);
    }
    if (phase == ArcPhase::Ascent && first_ascent < 0) {
      first_ascent = static_cast<int>(sec_id);
    }
    if (phase == ArcPhase::Descent) {
      last_descent = static_cast<int>(sec_id);
    }
  }

  ASSERT_GE(peak_section, 0);
  ASSERT_GE(first_ascent, 0);
  ASSERT_GE(last_descent, 0);

  int peak_range = computeSectionRegisterRange(notes, peak_section,
                                                config.bars_per_section);
  int first_range = computeSectionRegisterRange(notes, first_ascent,
                                                 config.bars_per_section);

  // Skip last_descent check if it is the final section (cadence bar interference).
  int last_range = -1;
  if (last_descent < config.num_sections - 1) {
    last_range = computeSectionRegisterRange(notes, last_descent,
                                              config.bars_per_section);
  }

  // The first Ascent section should have a narrower register than Peak.
  if (peak_range > 0 && first_range >= 0) {
    EXPECT_GE(peak_range, first_range)
        << "Peak range (" << peak_range << ") should be >= first Ascent range ("
        << first_range << ")";
  }

  // If the last non-final Descent section exists, it should be narrower than Peak.
  if (peak_range > 0 && last_range >= 0) {
    EXPECT_GE(peak_range, last_range)
        << "Peak range (" << peak_range << ") should be >= last Descent range ("
        << last_range << ")";
  }
}

// ===========================================================================
// Arc config-fixed (seed-independent) tests
// ===========================================================================

TEST(GlobalArcTest, DefaultArcIsConfigFixedNotSeedDependent) {
  // The default arc for a given num_sections should always be the same,
  // regardless of seed. This is a design principle: "Meaning axis never
  // changes on regeneration."
  auto config_a = createDefaultArcConfig(6);
  auto config_b = createDefaultArcConfig(6);

  ASSERT_EQ(config_a.phase_assignment.size(), config_b.phase_assignment.size());

  for (size_t idx = 0; idx < config_a.phase_assignment.size(); ++idx) {
    EXPECT_EQ(config_a.phase_assignment[idx].first,
              config_b.phase_assignment[idx].first);
    EXPECT_EQ(config_a.phase_assignment[idx].second,
              config_b.phase_assignment[idx].second);
  }
}

// ===========================================================================
// Edge case: three sections (minimum)
// ===========================================================================

TEST(GlobalArcTest, ThreeSectionArcHasOneOfEachPhase) {
  auto config = createDefaultArcConfig(3);
  ASSERT_EQ(config.phase_assignment.size(), 3u);

  // With only 3 sections, we must have exactly Ascent, Peak, Descent.
  EXPECT_EQ(config.phase_assignment[0].second, ArcPhase::Ascent);
  EXPECT_EQ(config.phase_assignment[1].second, ArcPhase::Peak);
  EXPECT_EQ(config.phase_assignment[2].second, ArcPhase::Descent);

  EXPECT_TRUE(validateGlobalArcConfig(config));
}

TEST(GlobalArcTest, ThreeSectionGenerationWorks) {
  auto config = makeArcTestConfig(3, 4, 42);
  auto result = generateArpeggioFlow(config);
  ASSERT_TRUE(result.success) << result.error_message;
  EXPECT_EQ(result.tracks.size(), 1u);
  EXPECT_FALSE(result.tracks[0].notes.empty());
}

// ===========================================================================
// Edge case: large section count
// ===========================================================================

TEST(GlobalArcTest, TenSectionsProducesValidArc) {
  auto config = createDefaultArcConfig(10);
  ASSERT_EQ(config.phase_assignment.size(), 10u);
  EXPECT_TRUE(validateGlobalArcConfig(config));

  // Verify peak is around 65% position.
  int peak_idx = -1;
  for (size_t idx = 0; idx < config.phase_assignment.size(); ++idx) {
    if (config.phase_assignment[idx].second == ArcPhase::Peak) {
      peak_idx = static_cast<int>(idx);
      break;
    }
  }
  ASSERT_GE(peak_idx, 0);

  // Peak should be at index 6 (ceil(10 * 0.65) - 1 = ceil(6.5) - 1 = 7 - 1 = 6).
  EXPECT_EQ(peak_idx, 6);
}

TEST(GlobalArcTest, TenSectionGenerationWorks) {
  auto config = makeArcTestConfig(10, 2, 42);
  auto result = generateArpeggioFlow(config);
  ASSERT_TRUE(result.success) << result.error_message;
  EXPECT_EQ(result.tracks.size(), 1u);
  EXPECT_FALSE(result.tracks[0].notes.empty());
}

}  // namespace
}  // namespace bach
