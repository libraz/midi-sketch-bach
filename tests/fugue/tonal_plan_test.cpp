// Tests for fugue/tonal_plan.h -- key relationships, tonal plan generation,
// and modulation schedule validation.

#include "fugue/tonal_plan.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <string>

namespace bach {
namespace {

// ---------------------------------------------------------------------------
// getDominantKey
// ---------------------------------------------------------------------------

TEST(TonalPlanTest, GetDominantKey_CMajor) {
  // C -> G (dominant)
  EXPECT_EQ(getDominantKey(Key::C), Key::G);
}

TEST(TonalPlanTest, GetDominantKey_FMajor) {
  // F -> C (dominant)
  EXPECT_EQ(getDominantKey(Key::F), Key::C);
}

TEST(TonalPlanTest, GetDominantKey_AllKeys) {
  // Circle of fifths: C->G->D->A->E->B->F#->C#->Ab->Eb->Bb->F->C
  const Key expected[] = {
      Key::G,   // C -> G
      Key::Ab,  // C# -> Ab (G#)
      Key::A,   // D -> A
      Key::Bb,  // Eb -> Bb
      Key::B,   // E -> B
      Key::C,   // F -> C
      Key::Cs,  // F# -> C#
      Key::D,   // G -> D
      Key::Eb,  // Ab -> Eb
      Key::E,   // A -> E
      Key::F,   // Bb -> F
      Key::Fs,  // B -> F#
  };

  for (uint8_t idx = 0; idx < 12; ++idx) {
    Key input = static_cast<Key>(idx);
    EXPECT_EQ(getDominantKey(input), expected[idx])
        << "Failed for key " << keyToString(input);
  }
}

// ---------------------------------------------------------------------------
// getSubdominantKey
// ---------------------------------------------------------------------------

TEST(TonalPlanTest, GetSubdominantKey_CMajor) {
  // C -> F (subdominant)
  EXPECT_EQ(getSubdominantKey(Key::C), Key::F);
}

TEST(TonalPlanTest, GetSubdominantKey_GMajor) {
  // G -> C (subdominant)
  EXPECT_EQ(getSubdominantKey(Key::G), Key::C);
}

TEST(TonalPlanTest, GetSubdominantKey_FMajor) {
  // F -> Bb (subdominant)
  EXPECT_EQ(getSubdominantKey(Key::F), Key::Bb);
}

TEST(TonalPlanTest, GetSubdominantKey_InverseOfDominant) {
  // Subdominant of dominant should return home key for all keys.
  for (uint8_t idx = 0; idx < 12; ++idx) {
    Key key = static_cast<Key>(idx);
    Key dominant = getDominantKey(key);
    EXPECT_EQ(getSubdominantKey(dominant), key)
        << "Subdominant of dominant should be home for key " << keyToString(key);
  }
}

// ---------------------------------------------------------------------------
// getRelativeKey
// ---------------------------------------------------------------------------

TEST(TonalPlanTest, GetRelativeKey_MajorToMinor) {
  // C major -> A minor (3 semitones down)
  EXPECT_EQ(getRelativeKey(Key::C, false), Key::A);
}

TEST(TonalPlanTest, GetRelativeKey_MinorToMajor) {
  // A minor -> C major (3 semitones up)
  EXPECT_EQ(getRelativeKey(Key::A, true), Key::C);
}

TEST(TonalPlanTest, GetRelativeKey_GMajor) {
  // G major -> E minor (3 semitones down)
  EXPECT_EQ(getRelativeKey(Key::G, false), Key::E);
}

TEST(TonalPlanTest, GetRelativeKey_DMinor) {
  // D minor -> F major (3 semitones up)
  EXPECT_EQ(getRelativeKey(Key::D, true), Key::F);
}

TEST(TonalPlanTest, GetRelativeKey_RoundTrip) {
  // Going from major to relative minor and back should return original.
  for (uint8_t idx = 0; idx < 12; ++idx) {
    Key key = static_cast<Key>(idx);
    Key relative_minor = getRelativeKey(key, false);
    Key back_to_major = getRelativeKey(relative_minor, true);
    EXPECT_EQ(back_to_major, key)
        << "Round trip failed for key " << keyToString(key);
  }
}

// ---------------------------------------------------------------------------
// getNearRelatedKeys
// ---------------------------------------------------------------------------

TEST(TonalPlanTest, GetNearRelatedKeys_CMajor) {
  auto keys = getNearRelatedKeys(Key::C, false);
  // Should include G (dominant), F (subdominant), A (relative minor).
  EXPECT_GE(keys.size(), 3u);

  auto contains = [&](Key target) {
    return std::find(keys.begin(), keys.end(), target) != keys.end();
  };

  EXPECT_TRUE(contains(Key::G)) << "Should include dominant G";
  EXPECT_TRUE(contains(Key::F)) << "Should include subdominant F";
  EXPECT_TRUE(contains(Key::A)) << "Should include relative minor A";
}

TEST(TonalPlanTest, GetNearRelatedKeys_DMinor) {
  auto keys = getNearRelatedKeys(Key::D, true);
  // Should include A (dominant), G (subdominant), F (relative major).
  EXPECT_GE(keys.size(), 3u);

  auto contains = [&](Key target) {
    return std::find(keys.begin(), keys.end(), target) != keys.end();
  };

  EXPECT_TRUE(contains(Key::A)) << "Should include dominant A";
  EXPECT_TRUE(contains(Key::G)) << "Should include subdominant G";
  EXPECT_TRUE(contains(Key::F)) << "Should include relative major F";
}

TEST(TonalPlanTest, GetNearRelatedKeys_DoesNotContainHome) {
  // Near-related keys should not include the home key itself.
  for (uint8_t idx = 0; idx < 12; ++idx) {
    Key key = static_cast<Key>(idx);
    auto keys = getNearRelatedKeys(key, false);
    auto found = std::find(keys.begin(), keys.end(), key);
    EXPECT_EQ(found, keys.end())
        << "Near-related keys should not contain home key " << keyToString(key);
  }
}

// ---------------------------------------------------------------------------
// TonalPlan::keyAtTick
// ---------------------------------------------------------------------------

TEST(TonalPlanTest, KeyAtTick_BeforeFirstModulation) {
  TonalPlan plan;
  plan.home_key = Key::D;
  // No modulations at all.
  EXPECT_EQ(plan.keyAtTick(0), Key::D);
  EXPECT_EQ(plan.keyAtTick(1000), Key::D);
}

TEST(TonalPlanTest, KeyAtTick_AtModulation) {
  TonalPlan plan;
  plan.home_key = Key::C;
  plan.modulations.push_back({Key::C, 0, FuguePhase::Establish});
  plan.modulations.push_back({Key::G, kTicksPerBar * 4, FuguePhase::Develop});

  EXPECT_EQ(plan.keyAtTick(kTicksPerBar * 4), Key::G);
}

TEST(TonalPlanTest, KeyAtTick_AfterModulation) {
  TonalPlan plan;
  plan.home_key = Key::C;
  plan.modulations.push_back({Key::C, 0, FuguePhase::Establish});
  plan.modulations.push_back({Key::G, kTicksPerBar * 4, FuguePhase::Develop});

  EXPECT_EQ(plan.keyAtTick(kTicksPerBar * 6), Key::G);
}

TEST(TonalPlanTest, KeyAtTick_BetweenModulations) {
  TonalPlan plan;
  plan.home_key = Key::C;
  plan.modulations.push_back({Key::C, 0, FuguePhase::Establish});
  plan.modulations.push_back({Key::G, kTicksPerBar * 4, FuguePhase::Develop});
  plan.modulations.push_back({Key::A, kTicksPerBar * 8, FuguePhase::Develop});

  // Between first and second development modulation.
  EXPECT_EQ(plan.keyAtTick(kTicksPerBar * 6), Key::G);
}

TEST(TonalPlanTest, KeyAtTick_ReturnsHomeAfterResolve) {
  TonalPlan plan;
  plan.home_key = Key::C;
  plan.modulations.push_back({Key::C, 0, FuguePhase::Establish});
  plan.modulations.push_back({Key::G, kTicksPerBar * 4, FuguePhase::Develop});
  plan.modulations.push_back({Key::C, kTicksPerBar * 8, FuguePhase::Resolve});

  EXPECT_EQ(plan.keyAtTick(kTicksPerBar * 10), Key::C);
}

// ---------------------------------------------------------------------------
// TonalPlan::keySequence
// ---------------------------------------------------------------------------

TEST(TonalPlanTest, KeySequence_NoDuplicatesInSuccession) {
  TonalPlan plan;
  plan.home_key = Key::C;
  plan.modulations.push_back({Key::C, 0, FuguePhase::Establish});
  plan.modulations.push_back({Key::C, kTicksPerBar, FuguePhase::Establish});
  plan.modulations.push_back({Key::G, kTicksPerBar * 4, FuguePhase::Develop});
  plan.modulations.push_back({Key::C, kTicksPerBar * 8, FuguePhase::Resolve});

  auto sequence = plan.keySequence();
  // Should be [C, G, C] -- no duplicate C at the start.
  ASSERT_EQ(sequence.size(), 3u);
  EXPECT_EQ(sequence[0], Key::C);
  EXPECT_EQ(sequence[1], Key::G);
  EXPECT_EQ(sequence[2], Key::C);
}

TEST(TonalPlanTest, KeySequence_EmptyModulations) {
  TonalPlan plan;
  plan.home_key = Key::D;
  auto sequence = plan.keySequence();
  ASSERT_EQ(sequence.size(), 1u);
  EXPECT_EQ(sequence[0], Key::D);
}

TEST(TonalPlanTest, KeySequence_SingleModulation) {
  TonalPlan plan;
  plan.home_key = Key::C;
  plan.modulations.push_back({Key::G, kTicksPerBar * 4, FuguePhase::Develop});

  auto sequence = plan.keySequence();
  // Home key (C) at start, then G from modulation.
  ASSERT_EQ(sequence.size(), 2u);
  EXPECT_EQ(sequence[0], Key::C);
  EXPECT_EQ(sequence[1], Key::G);
}

// ---------------------------------------------------------------------------
// TonalPlan::modulationCount
// ---------------------------------------------------------------------------

TEST(TonalPlanTest, ModulationCount_ReturnsCorrectCount) {
  TonalPlan plan;
  plan.modulations.push_back({Key::C, 0, FuguePhase::Establish});
  plan.modulations.push_back({Key::G, 1920, FuguePhase::Develop});
  plan.modulations.push_back({Key::C, 3840, FuguePhase::Resolve});
  EXPECT_EQ(plan.modulationCount(), 3u);
}

TEST(TonalPlanTest, ModulationCount_EmptyIsZero) {
  TonalPlan plan;
  EXPECT_EQ(plan.modulationCount(), 0u);
}

// ---------------------------------------------------------------------------
// generateTonalPlan -- Major key
// ---------------------------------------------------------------------------

TEST(TonalPlanTest, GenerateTonalPlan_StartsInHomeKey) {
  FugueConfig config;
  config.key = Key::C;
  Tick duration = kTicksPerBar * 24;  // 24 bars

  TonalPlan plan = generateTonalPlan(config, false, duration);
  ASSERT_FALSE(plan.modulations.empty());
  EXPECT_EQ(plan.modulations[0].target_key, Key::C);
  EXPECT_EQ(plan.modulations[0].tick, 0u);
  EXPECT_EQ(plan.modulations[0].phase, FuguePhase::Establish);
}

TEST(TonalPlanTest, GenerateTonalPlan_ReturnsToHomeKey) {
  FugueConfig config;
  config.key = Key::C;
  Tick duration = kTicksPerBar * 24;

  TonalPlan plan = generateTonalPlan(config, false, duration);
  ASSERT_FALSE(plan.modulations.empty());

  // Last modulation should be back to home key in Resolve phase.
  const auto& last = plan.modulations.back();
  EXPECT_EQ(last.target_key, Key::C);
  EXPECT_EQ(last.phase, FuguePhase::Resolve);
}

TEST(TonalPlanTest, GenerateTonalPlan_DevelopHasModulations) {
  FugueConfig config;
  config.key = Key::C;
  Tick duration = kTicksPerBar * 24;

  TonalPlan plan = generateTonalPlan(config, false, duration);

  // Count modulations in Develop phase.
  int develop_count = 0;
  for (const auto& mod : plan.modulations) {
    if (mod.phase == FuguePhase::Develop) {
      ++develop_count;
    }
  }
  EXPECT_GE(develop_count, 1) << "Develop phase should have at least one modulation";
}

TEST(TonalPlanTest, GenerateTonalPlan_MajorKeyVisitsDominant) {
  FugueConfig config;
  config.key = Key::C;
  Tick duration = kTicksPerBar * 24;

  TonalPlan plan = generateTonalPlan(config, false, duration);
  auto sequence = plan.keySequence();

  auto contains = [&](Key target) {
    return std::find(sequence.begin(), sequence.end(), target) != sequence.end();
  };

  // Major fugue should visit dominant (G).
  EXPECT_TRUE(contains(Key::G)) << "C major fugue should visit dominant G";
}

TEST(TonalPlanTest, GenerateTonalPlan_MajorKeyVisitsRelativeMinor) {
  FugueConfig config;
  config.key = Key::C;
  Tick duration = kTicksPerBar * 24;

  TonalPlan plan = generateTonalPlan(config, false, duration);
  auto sequence = plan.keySequence();

  auto contains = [&](Key target) {
    return std::find(sequence.begin(), sequence.end(), target) != sequence.end();
  };

  // Major fugue should visit relative minor (A).
  EXPECT_TRUE(contains(Key::A)) << "C major fugue should visit relative minor A";
}

// ---------------------------------------------------------------------------
// generateTonalPlan -- Minor key
// ---------------------------------------------------------------------------

TEST(TonalPlanTest, GenerateTonalPlan_MinorKey) {
  FugueConfig config;
  config.key = Key::D;
  Tick duration = kTicksPerBar * 24;

  TonalPlan plan = generateTonalPlan(config, true, duration);

  EXPECT_EQ(plan.home_key, Key::D);
  EXPECT_TRUE(plan.is_minor);

  // Should start and end in D.
  ASSERT_FALSE(plan.modulations.empty());
  EXPECT_EQ(plan.modulations.front().target_key, Key::D);
  EXPECT_EQ(plan.modulations.back().target_key, Key::D);
}

TEST(TonalPlanTest, GenerateTonalPlan_MinorKeyVisitsRelativeMajor) {
  FugueConfig config;
  config.key = Key::D;
  Tick duration = kTicksPerBar * 24;

  TonalPlan plan = generateTonalPlan(config, true, duration);
  auto sequence = plan.keySequence();

  auto contains = [&](Key target) {
    return std::find(sequence.begin(), sequence.end(), target) != sequence.end();
  };

  // D minor should visit relative major (F).
  EXPECT_TRUE(contains(Key::F)) << "D minor fugue should visit relative major F";
}

TEST(TonalPlanTest, GenerateTonalPlan_ModulationsChronological) {
  FugueConfig config;
  config.key = Key::G;
  Tick duration = kTicksPerBar * 30;

  TonalPlan plan = generateTonalPlan(config, false, duration);

  for (size_t idx = 1; idx < plan.modulations.size(); ++idx) {
    EXPECT_GE(plan.modulations[idx].tick, plan.modulations[idx - 1].tick)
        << "Modulations must be in chronological order at index " << idx;
  }
}

TEST(TonalPlanTest, GenerateTonalPlan_PhaseOrderCorrect) {
  FugueConfig config;
  config.key = Key::C;
  Tick duration = kTicksPerBar * 24;

  TonalPlan plan = generateTonalPlan(config, false, duration);
  ASSERT_GE(plan.modulations.size(), 3u);

  // First should be Establish, last should be Resolve.
  EXPECT_EQ(plan.modulations.front().phase, FuguePhase::Establish);
  EXPECT_EQ(plan.modulations.back().phase, FuguePhase::Resolve);

  // Phases should never go backwards.
  int prev_phase = 0;
  for (const auto& mod : plan.modulations) {
    int cur_phase = static_cast<int>(mod.phase);
    EXPECT_GE(cur_phase, prev_phase) << "Phase order violation at tick " << mod.tick;
    prev_phase = cur_phase;
  }
}

// ---------------------------------------------------------------------------
// generateTonalPlan -- Edge cases
// ---------------------------------------------------------------------------

TEST(TonalPlanTest, GenerateTonalPlan_ShortDuration) {
  FugueConfig config;
  config.key = Key::C;
  // Very short: just 3 bars.
  Tick duration = kTicksPerBar * 3;

  TonalPlan plan = generateTonalPlan(config, false, duration);

  // Should still start in home key and end in home key.
  ASSERT_FALSE(plan.modulations.empty());
  EXPECT_EQ(plan.modulations.front().target_key, Key::C);
  EXPECT_EQ(plan.modulations.back().target_key, Key::C);
}

TEST(TonalPlanTest, GenerateTonalPlan_AllBarAligned) {
  FugueConfig config;
  config.key = Key::C;
  Tick duration = kTicksPerBar * 24;

  TonalPlan plan = generateTonalPlan(config, false, duration);

  for (const auto& mod : plan.modulations) {
    EXPECT_EQ(mod.tick % kTicksPerBar, 0u)
        << "Modulation at tick " << mod.tick << " is not bar-aligned";
  }
}

// ---------------------------------------------------------------------------
// toJson
// ---------------------------------------------------------------------------

TEST(TonalPlanTest, ToJson_ContainsHomeKey) {
  TonalPlan plan;
  plan.home_key = Key::C;
  plan.is_minor = false;
  plan.modulations.push_back({Key::C, 0, FuguePhase::Establish});

  std::string json = plan.toJson();
  EXPECT_NE(json.find("\"home_key\""), std::string::npos);
  EXPECT_NE(json.find("\"C\""), std::string::npos);
}

TEST(TonalPlanTest, ToJson_ContainsIsMinor) {
  TonalPlan plan;
  plan.home_key = Key::D;
  plan.is_minor = true;

  std::string json = plan.toJson();
  EXPECT_NE(json.find("\"is_minor\""), std::string::npos);
  EXPECT_NE(json.find("true"), std::string::npos);
}

TEST(TonalPlanTest, ToJson_ContainsModulations) {
  TonalPlan plan;
  plan.home_key = Key::C;
  plan.modulations.push_back({Key::C, 0, FuguePhase::Establish});
  plan.modulations.push_back({Key::G, 1920, FuguePhase::Develop});

  std::string json = plan.toJson();
  EXPECT_NE(json.find("\"modulations\""), std::string::npos);
  EXPECT_NE(json.find("\"G\""), std::string::npos);
  EXPECT_NE(json.find("\"Develop\""), std::string::npos);
}

TEST(TonalPlanTest, ToJson_ContainsKeySequence) {
  TonalPlan plan;
  plan.home_key = Key::C;
  plan.modulations.push_back({Key::C, 0, FuguePhase::Establish});
  plan.modulations.push_back({Key::G, 1920, FuguePhase::Develop});

  std::string json = plan.toJson();
  EXPECT_NE(json.find("\"key_sequence\""), std::string::npos);
}

}  // namespace
}  // namespace bach
