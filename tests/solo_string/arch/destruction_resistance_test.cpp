// Destruction resistance test -- 100 seeds, ALL must maintain structural integrity.
//
// This is the critical Phase 8 test ensuring no seed can destroy the chaconne
// structural invariants: ground bass immutability, role order, climax count,
// and instrument range compliance.

#include "solo_string/arch/chaconne_analyzer.h"
#include "solo_string/arch/chaconne_config.h"
#include "solo_string/arch/chaconne_engine.h"
#include "solo_string/arch/chaconne_scheme.h"

#include <cstdint>
#include <random>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "core/basic_types.h"
#include "harmony/key.h"
#include "instrument/bowed/violin_model.h"

namespace bach {
namespace {

// ===========================================================================
// Core destruction resistance: 100 seeds, all structural invariants
// ===========================================================================

TEST(DestructionResistanceTest, HundredSeedsAllMaintainStructure) {
  for (uint32_t seed = 1; seed <= 100; ++seed) {
    ChaconneConfig config;
    config.key = {Key::D, true};
    config.seed = seed;
    config.instrument = InstrumentType::Violin;

    ChaconneResult result = generateChaconne(config);
    ASSERT_TRUE(result.success)
        << "Seed " << seed << " failed generation: " << result.error_message;

    // Verify structural integrity via analyzer.
    auto scheme = ChaconneScheme::createForKey(config.key);
    // The analyzer needs the variation plan in config. Since we left
    // config.variations empty, the engine used createStandardVariationPlan
    // internally. We must populate it for the analyzer.
    std::mt19937 rng(42);
    config.variations = createStandardVariationPlan(config.key, rng);

    auto analysis = analyzeChaconne(result.tracks, config, scheme);

    EXPECT_FLOAT_EQ(analysis.harmonic_scheme_integrity, 1.0f)
        << "Seed " << seed << ": harmonic scheme integrity violated";
    EXPECT_FLOAT_EQ(analysis.role_order_score, 1.0f)
        << "Seed " << seed << ": role order violated";
    EXPECT_FLOAT_EQ(analysis.climax_presence_score, 1.0f)
        << "Seed " << seed << ": climax presence violated";
    EXPECT_EQ(analysis.accumulate_count, 3)
        << "Seed " << seed << ": expected 3 Accumulate, got "
        << analysis.accumulate_count;
  }
}

// ===========================================================================
// 100 seeds all produce non-empty tracks
// ===========================================================================

TEST(DestructionResistanceTest, HundredSeedsAllProduceNonEmptyTracks) {
  for (uint32_t seed = 1; seed <= 100; ++seed) {
    ChaconneConfig config;
    config.key = {Key::D, true};
    config.seed = seed;
    config.instrument = InstrumentType::Violin;

    ChaconneResult result = generateChaconne(config);
    ASSERT_TRUE(result.success)
        << "Seed " << seed << " failed: " << result.error_message;
    ASSERT_EQ(result.tracks.size(), 1u)
        << "Seed " << seed << ": expected 1 track";
    EXPECT_FALSE(result.tracks[0].notes.empty())
        << "Seed " << seed << ": track has no notes";
    EXPECT_GT(result.total_duration_ticks, 0u)
        << "Seed " << seed << ": total duration is zero";
  }
}

// ===========================================================================
// 100 seeds all have notes within violin range
// ===========================================================================

TEST(DestructionResistanceTest, HundredSeedsViolinNotesInRange) {
  // With instrument-aware bass, all notes (including ground bass) should
  // now be within the violin's playable range.
  ViolinModel violin;
  const uint8_t kLowestNote = violin.getLowestPitch();
  const uint8_t kHighestNote = violin.getHighestPitch();

  for (uint32_t seed = 1; seed <= 100; ++seed) {
    ChaconneConfig config;
    config.key = {Key::D, true};
    config.seed = seed;
    config.instrument = InstrumentType::Violin;

    ChaconneResult result = generateChaconne(config);
    ASSERT_TRUE(result.success)
        << "Seed " << seed << " failed: " << result.error_message;
    ASSERT_EQ(result.tracks.size(), 1u);

    for (const auto& note : result.tracks[0].notes) {
      EXPECT_GE(note.pitch, kLowestNote)
          << "Seed " << seed << ": note pitch " << static_cast<int>(note.pitch)
          << " below range at tick " << note.start_tick;
      EXPECT_LE(note.pitch, kHighestNote)
          << "Seed " << seed << ": note pitch " << static_cast<int>(note.pitch)
          << " above range at tick " << note.start_tick;
    }
  }
}

// ===========================================================================
// 50 seeds with 5 different keys
// ===========================================================================

TEST(DestructionResistanceTest, FiftySeedsAcrossFiveKeys) {
  struct KeyTestCase {
    Key tonic;
    const char* name;
  };

  KeyTestCase keys[] = {
      {Key::D, "D minor"},
      {Key::C, "C minor"},
      {Key::G, "G minor"},
      {Key::A, "A minor"},
      {Key::E, "E minor"},
  };

  for (const auto& key_case : keys) {
    for (uint32_t seed = 1; seed <= 10; ++seed) {
      ChaconneConfig config;
      config.key = {key_case.tonic, true};
      config.seed = seed;
      config.instrument = InstrumentType::Violin;

      ChaconneResult result = generateChaconne(config);
      ASSERT_TRUE(result.success)
          << key_case.name << " seed " << seed
          << " failed: " << result.error_message;
      ASSERT_EQ(result.tracks.size(), 1u)
          << key_case.name << " seed " << seed << ": expected 1 track";
      EXPECT_FALSE(result.tracks[0].notes.empty())
          << key_case.name << " seed " << seed << ": no notes generated";

      // Verify structural invariants.
      auto scheme = ChaconneScheme::createForKey(config.key);
      std::mt19937 rng(42);
      config.variations = createStandardVariationPlan(config.key, rng);
      auto analysis = analyzeChaconne(result.tracks, config, scheme);

      EXPECT_FLOAT_EQ(analysis.harmonic_scheme_integrity, 1.0f)
          << key_case.name << " seed " << seed << ": harmonic scheme integrity failed";
      EXPECT_FLOAT_EQ(analysis.role_order_score, 1.0f)
          << key_case.name << " seed " << seed << ": role order failed";
      EXPECT_FLOAT_EQ(analysis.climax_presence_score, 1.0f)
          << key_case.name << " seed " << seed << ": climax presence failed";
      EXPECT_EQ(analysis.accumulate_count, 3)
          << key_case.name << " seed " << seed << ": wrong accumulate count";
    }
  }
}

// ===========================================================================
// Multi-instrument destruction resistance
// ===========================================================================

TEST(DestructionResistanceTest, ThirtySeeds_ThreeInstruments) {
  struct InstrumentTestCase {
    InstrumentType instrument;
    const char* name;
    uint8_t expected_program;
  };

  InstrumentTestCase instruments[] = {
      {InstrumentType::Violin, "Violin", 40},
      {InstrumentType::Cello, "Cello", 42},
      {InstrumentType::Guitar, "Guitar", 24},
  };

  for (const auto& inst_case : instruments) {
    for (uint32_t seed = 1; seed <= 10; ++seed) {
      ChaconneConfig config;
      config.key = {Key::D, true};
      config.seed = seed;
      config.instrument = inst_case.instrument;

      ChaconneResult result = generateChaconne(config);
      ASSERT_TRUE(result.success)
          << inst_case.name << " seed " << seed
          << " failed: " << result.error_message;
      ASSERT_EQ(result.tracks.size(), 1u);
      EXPECT_FALSE(result.tracks[0].notes.empty())
          << inst_case.name << " seed " << seed << ": no notes";
      EXPECT_EQ(result.tracks[0].program, inst_case.expected_program)
          << inst_case.name << " seed " << seed << ": wrong GM program";

      // All notes must have non-zero duration.
      for (const auto& note : result.tracks[0].notes) {
        EXPECT_GT(note.duration, 0u)
            << inst_case.name << " seed " << seed
            << ": zero-duration note at tick " << note.start_tick;
      }
    }
  }
}

// ===========================================================================
// Seed determinism across 20 seeds
// ===========================================================================

TEST(DestructionResistanceTest, TwentySeedsAreDeterministic) {
  for (uint32_t seed = 1; seed <= 20; ++seed) {
    ChaconneConfig config_a;
    config_a.key = {Key::D, true};
    config_a.seed = seed;
    config_a.instrument = InstrumentType::Violin;

    ChaconneConfig config_b = config_a;

    auto result_a = generateChaconne(config_a);
    auto result_b = generateChaconne(config_b);

    ASSERT_TRUE(result_a.success) << "Seed " << seed << " run A failed";
    ASSERT_TRUE(result_b.success) << "Seed " << seed << " run B failed";
    ASSERT_EQ(result_a.tracks.size(), 1u);
    ASSERT_EQ(result_b.tracks.size(), 1u);

    const auto& notes_a = result_a.tracks[0].notes;
    const auto& notes_b = result_b.tracks[0].notes;

    ASSERT_EQ(notes_a.size(), notes_b.size())
        << "Seed " << seed << ": note count mismatch between runs";

    for (size_t idx = 0; idx < notes_a.size(); ++idx) {
      EXPECT_EQ(notes_a[idx].pitch, notes_b[idx].pitch)
          << "Seed " << seed << ": pitch mismatch at note " << idx;
      EXPECT_EQ(notes_a[idx].start_tick, notes_b[idx].start_tick)
          << "Seed " << seed << ": timing mismatch at note " << idx;
      EXPECT_EQ(notes_a[idx].duration, notes_b[idx].duration)
          << "Seed " << seed << ": duration mismatch at note " << idx;
    }
  }
}

}  // namespace
}  // namespace bach
