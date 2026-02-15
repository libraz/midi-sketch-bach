// Tests for solo_string/arch/bass_realizer.h -- role-based bass line realization.

#include "solo_string/arch/bass_realizer.h"

#include <algorithm>
#include <vector>

#include <gtest/gtest.h>

#include "core/basic_types.h"
#include "core/note_source.h"
#include "harmony/chord_types.h"
#include "harmony/key.h"
#include "solo_string/arch/chaconne_scheme.h"

namespace bach {
namespace {

// ===========================================================================
// Common test fixtures
// ===========================================================================

/// Approximate violin range for register testing.
constexpr uint8_t kViolinLow = 55;
constexpr uint8_t kViolinHigh = 96;

/// Default D minor key.
const KeySignature kDMinor{Key::D, true};

// ===========================================================================
// Simple realization: 1 note per scheme entry
// ===========================================================================

TEST(BassRealizerTest, SimpleProducesOneNotePerEntry) {
  auto scheme = ChaconneScheme::createStandardDMinor();
  auto notes = realizeBass(scheme, kDMinor, VariationRole::Establish,
                           kViolinLow, kViolinHigh, 42);

  // Standard scheme has 7 entries; Simple should produce exactly 7 notes.
  EXPECT_EQ(notes.size(), 7u);
}

TEST(BassRealizerTest, ResolveAlsoProducesOneNotePerEntry) {
  auto scheme = ChaconneScheme::createStandardDMinor();
  auto notes = realizeBass(scheme, kDMinor, VariationRole::Resolve,
                           kViolinLow, kViolinHigh, 42);
  EXPECT_EQ(notes.size(), 7u);
}

// ===========================================================================
// Walking bass: strong beat is a chord tone
// ===========================================================================

TEST(BassRealizerTest, WalkingStrongBeatMatchesSimpleRoot) {
  auto scheme = ChaconneScheme::createStandardDMinor();

  // Simple realization gives the canonical root pitch per entry (after register
  // clamping).  Walking bass should place the same pitch on each entry's first
  // beat, because the code path is identical: computeRootPitch + clampToProfile.
  auto simple_notes = realizeBass(scheme, kDMinor, VariationRole::Establish,
                                  kViolinLow, kViolinHigh, 42);
  auto walking_notes = realizeBass(scheme, kDMinor, VariationRole::Develop,
                                   kViolinLow, kViolinHigh, 42);

  const auto& entries = scheme.entries();
  ASSERT_EQ(simple_notes.size(), entries.size());

  for (size_t eidx = 0; eidx < entries.size(); ++eidx) {
    Tick entry_start = static_cast<Tick>(entries[eidx].position_beats) * kTicksPerBeat;

    // Find walking bass note at this entry's strong beat.
    bool found = false;
    for (const auto& note : walking_notes) {
      if (note.start_tick == entry_start) {
        found = true;
        EXPECT_EQ(note.pitch, simple_notes[eidx].pitch)
            << "Entry " << eidx << " walking strong beat pitch "
            << static_cast<int>(note.pitch) << " differs from simple root pitch "
            << static_cast<int>(simple_notes[eidx].pitch);
        break;
      }
    }
    EXPECT_TRUE(found)
        << "No walking note found at entry " << eidx << " start tick " << entry_start;
  }
}

// ===========================================================================
// Syncopated: syncopation density <= 40% of total beats
// ===========================================================================

TEST(BassRealizerTest, SyncopatedDensityWithinLimit) {
  auto scheme = ChaconneScheme::createStandardDMinor();
  auto notes = realizeBass(scheme, kDMinor, VariationRole::Destabilize,
                           kViolinLow, kViolinHigh, 42);

  // Count notes that start on an offbeat (not aligned to beat boundary).
  int syncopated_count = 0;
  int total_notes = static_cast<int>(notes.size());
  for (const auto& note : notes) {
    if (note.start_tick % kTicksPerBeat != 0) {
      ++syncopated_count;
    }
  }

  // Syncopation density should not exceed 40%.
  if (total_notes > 0) {
    float synco_ratio = static_cast<float>(syncopated_count) / static_cast<float>(total_notes);
    EXPECT_LE(synco_ratio, 0.40f)
        << "Syncopation density " << synco_ratio << " exceeds 40% limit";
  }
}

// ===========================================================================
// Elaborate produces more notes than Simple
// ===========================================================================

TEST(BassRealizerTest, ElaborateProducesMoreNotesThanSimple) {
  auto scheme = ChaconneScheme::createStandardDMinor();
  auto simple_notes = realizeBass(scheme, kDMinor, VariationRole::Establish,
                                  kViolinLow, kViolinHigh, 42);
  auto elaborate_notes = realizeBass(scheme, kDMinor, VariationRole::Accumulate,
                                     kViolinLow, kViolinHigh, 42);

  EXPECT_GT(elaborate_notes.size(), simple_notes.size())
      << "Elaborate (" << elaborate_notes.size() << " notes) should produce more notes "
      << "than Simple (" << simple_notes.size() << " notes)";
}

// ===========================================================================
// All notes within register bounds
// ===========================================================================

TEST(BassRealizerTest, AllNotesWithinRegisterBounds) {
  auto scheme = ChaconneScheme::createStandardDMinor();

  // Test all roles.
  std::vector<VariationRole> roles = {
      VariationRole::Establish, VariationRole::Develop,
      VariationRole::Destabilize, VariationRole::Illuminate,
      VariationRole::Accumulate, VariationRole::Resolve};

  for (auto role : roles) {
    auto notes = realizeBass(scheme, kDMinor, role,
                             kViolinLow, kViolinHigh, 42);

    BassRegisterProfile profile =
        getBassRegisterProfile(role, kViolinLow, kViolinHigh);

    for (const auto& note : notes) {
      EXPECT_GE(note.pitch, profile.effective_low)
          << "Note pitch " << static_cast<int>(note.pitch) << " below register low "
          << static_cast<int>(profile.effective_low)
          << " for role " << variationRoleToString(role)
          << " at tick " << note.start_tick;
      EXPECT_LE(note.pitch, profile.effective_high)
          << "Note pitch " << static_cast<int>(note.pitch) << " above register high "
          << static_cast<int>(profile.effective_high)
          << " for role " << variationRoleToString(role)
          << " at tick " << note.start_tick;
    }
  }
}

// ===========================================================================
// All notes have BachNoteSource::ChaconneBass
// ===========================================================================

TEST(BassRealizerTest, AllNotesHaveChaconneBassSource) {
  auto scheme = ChaconneScheme::createStandardDMinor();

  std::vector<VariationRole> roles = {
      VariationRole::Establish, VariationRole::Develop,
      VariationRole::Destabilize, VariationRole::Illuminate,
      VariationRole::Accumulate, VariationRole::Resolve};

  for (auto role : roles) {
    auto notes = realizeBass(scheme, kDMinor, role,
                             kViolinLow, kViolinHigh, 42);

    for (const auto& note : notes) {
      EXPECT_EQ(note.source, BachNoteSource::ChaconneBass)
          << "Note at tick " << note.start_tick
          << " has wrong source for role " << variationRoleToString(role);
    }
  }
}

// ===========================================================================
// Determinism: same seed produces same result
// ===========================================================================

TEST(BassRealizerTest, SameSeedProducesSameResult) {
  auto scheme = ChaconneScheme::createStandardDMinor();

  // Test with Walking style (uses RNG).
  auto notes_a = realizeBass(scheme, kDMinor, VariationRole::Develop,
                             kViolinLow, kViolinHigh, 42);
  auto notes_b = realizeBass(scheme, kDMinor, VariationRole::Develop,
                             kViolinLow, kViolinHigh, 42);

  ASSERT_EQ(notes_a.size(), notes_b.size());
  for (size_t i = 0; i < notes_a.size(); ++i) {
    EXPECT_EQ(notes_a[i].pitch, notes_b[i].pitch)
        << "Pitch mismatch at note " << i;
    EXPECT_EQ(notes_a[i].start_tick, notes_b[i].start_tick)
        << "Start tick mismatch at note " << i;
    EXPECT_EQ(notes_a[i].duration, notes_b[i].duration)
        << "Duration mismatch at note " << i;
  }
}

// ===========================================================================
// Different seeds produce different results (for RNG-dependent styles)
// ===========================================================================

TEST(BassRealizerTest, DifferentSeedsProduceDifferentResults) {
  auto scheme = ChaconneScheme::createStandardDMinor();

  // Test Walking, Syncopated, and Elaborate (all use RNG).
  std::vector<VariationRole> rng_roles = {
      VariationRole::Develop, VariationRole::Destabilize, VariationRole::Accumulate};

  for (auto role : rng_roles) {
    auto notes_a = realizeBass(scheme, kDMinor, role,
                               kViolinLow, kViolinHigh, 42);
    auto notes_b = realizeBass(scheme, kDMinor, role,
                               kViolinLow, kViolinHigh, 99);

    // At least one difference should exist.
    bool any_diff = false;
    if (notes_a.size() != notes_b.size()) {
      any_diff = true;
    } else {
      for (size_t i = 0; i < notes_a.size(); ++i) {
        if (notes_a[i].pitch != notes_b[i].pitch ||
            notes_a[i].start_tick != notes_b[i].start_tick ||
            notes_a[i].duration != notes_b[i].duration) {
          any_diff = true;
          break;
        }
      }
    }
    EXPECT_TRUE(any_diff)
        << "Different seeds produced identical output for role "
        << variationRoleToString(role);
  }
}

// ===========================================================================
// getRealizationStyle mapping correctness
// ===========================================================================

TEST(BassRealizerTest, GetRealizationStyleMapping) {
  EXPECT_EQ(getRealizationStyle(VariationRole::Establish), BassRealizationStyle::Simple);
  EXPECT_EQ(getRealizationStyle(VariationRole::Develop), BassRealizationStyle::Walking);
  EXPECT_EQ(getRealizationStyle(VariationRole::Destabilize), BassRealizationStyle::Syncopated);
  EXPECT_EQ(getRealizationStyle(VariationRole::Illuminate), BassRealizationStyle::Lyrical);
  EXPECT_EQ(getRealizationStyle(VariationRole::Accumulate), BassRealizationStyle::Elaborate);
  EXPECT_EQ(getRealizationStyle(VariationRole::Resolve), BassRealizationStyle::Simple);
}

// ===========================================================================
// getBassRegisterProfile: Accumulate expansion
// ===========================================================================

TEST(BassRealizerTest, AccumulateRegisterExpandsWithIndex) {
  auto profile_0 = getBassRegisterProfile(VariationRole::Accumulate,
                                          kViolinLow, kViolinHigh, 0);
  auto profile_1 = getBassRegisterProfile(VariationRole::Accumulate,
                                          kViolinLow, kViolinHigh, 1);
  auto profile_2 = getBassRegisterProfile(VariationRole::Accumulate,
                                          kViolinLow, kViolinHigh, 2);

  // Each successive accumulate index should have a higher effective_high.
  EXPECT_LT(profile_0.effective_high, profile_1.effective_high)
      << "Accumulate index 1 should have higher effective_high than index 0";
  EXPECT_LT(profile_1.effective_high, profile_2.effective_high)
      << "Accumulate index 2 should have higher effective_high than index 1";
}

TEST(BassRealizerTest, AccumulateRegisterSameLow) {
  auto profile_0 = getBassRegisterProfile(VariationRole::Accumulate,
                                          kViolinLow, kViolinHigh, 0);
  auto profile_2 = getBassRegisterProfile(VariationRole::Accumulate,
                                          kViolinLow, kViolinHigh, 2);

  // Low bound should be the same across accumulate indices.
  EXPECT_EQ(profile_0.effective_low, profile_2.effective_low);
}

// ===========================================================================
// Empty scheme returns empty notes
// ===========================================================================

TEST(BassRealizerTest, EmptySchemeReturnsNoNotes) {
  ChaconneScheme empty_scheme;
  auto notes = realizeBass(empty_scheme, kDMinor, VariationRole::Establish,
                           kViolinLow, kViolinHigh, 42);
  EXPECT_TRUE(notes.empty());
}

// ===========================================================================
// All notes have non-zero duration
// ===========================================================================

TEST(BassRealizerTest, AllNotesHaveNonZeroDuration) {
  auto scheme = ChaconneScheme::createStandardDMinor();

  std::vector<VariationRole> roles = {
      VariationRole::Establish, VariationRole::Develop,
      VariationRole::Destabilize, VariationRole::Illuminate,
      VariationRole::Accumulate, VariationRole::Resolve};

  for (auto role : roles) {
    auto notes = realizeBass(scheme, kDMinor, role,
                             kViolinLow, kViolinHigh, 42);

    for (const auto& note : notes) {
      EXPECT_GT(note.duration, 0u)
          << "Note with zero duration at tick " << note.start_tick
          << " for role " << variationRoleToString(role);
    }
  }
}

// ===========================================================================
// Walking produces more notes than Simple
// ===========================================================================

TEST(BassRealizerTest, WalkingProducesMoreNotesThanSimple) {
  auto scheme = ChaconneScheme::createStandardDMinor();
  auto simple = realizeBass(scheme, kDMinor, VariationRole::Establish,
                            kViolinLow, kViolinHigh, 42);
  auto walking = realizeBass(scheme, kDMinor, VariationRole::Develop,
                             kViolinLow, kViolinHigh, 42);

  // Walking bass fills weak beats, so it should have more notes.
  EXPECT_GT(walking.size(), simple.size())
      << "Walking (" << walking.size() << ") should have more notes than Simple ("
      << simple.size() << ")";
}

// ===========================================================================
// Illuminate (Lyrical) produces notes
// ===========================================================================

TEST(BassRealizerTest, LyricalProducesNotes) {
  auto scheme = ChaconneScheme::createStandardDMinor();
  auto notes = realizeBass(scheme, kDMinor, VariationRole::Illuminate,
                           kViolinLow, kViolinHigh, 42);

  // Lyrical should produce at least as many notes as scheme entries.
  EXPECT_GE(notes.size(), scheme.size());
}

// ===========================================================================
// Notes are sorted by start_tick
// ===========================================================================

TEST(BassRealizerTest, NotesAreSortedByStartTick) {
  auto scheme = ChaconneScheme::createStandardDMinor();

  std::vector<VariationRole> roles = {
      VariationRole::Establish, VariationRole::Develop,
      VariationRole::Destabilize, VariationRole::Illuminate,
      VariationRole::Accumulate, VariationRole::Resolve};

  for (auto role : roles) {
    auto notes = realizeBass(scheme, kDMinor, role,
                             kViolinLow, kViolinHigh, 42);

    for (size_t i = 1; i < notes.size(); ++i) {
      EXPECT_GE(notes[i].start_tick, notes[i - 1].start_tick)
          << "Notes not sorted at index " << i
          << " for role " << variationRoleToString(role);
    }
  }
}

}  // namespace
}  // namespace bach
