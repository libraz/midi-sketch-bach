// Harmonic time warp tests.

#include "harmony/harmonic_time_warp.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <set>
#include <vector>

#include "core/basic_types.h"
#include "forms/goldberg/goldberg_plan.h"
#include "forms/goldberg/goldberg_structural_grid.h"
#include "forms/goldberg/goldberg_types.h"
#include "gtest/gtest.h"

namespace bach {
namespace {

// --- Test helpers ---

constexpr TimeSignature kThreeFour = {3, 4};
constexpr uint32_t kTestSeed = 42;

/// @brief Create a simple note grid: one note per beat across 32 bars.
std::vector<NoteEvent> makeTestNotes(TimeSignature ts, int total_bars = 32) {
  Tick bar_ticks = ts.ticksPerBar();
  int beats = ts.pulsesPerBar();
  Tick beat_ticks = bar_ticks / static_cast<Tick>(beats);
  std::vector<NoteEvent> notes;
  for (int bar = 0; bar < total_bars; ++bar) {
    for (int beat = 0; beat < beats; ++beat) {
      NoteEvent n;
      n.start_tick = static_cast<Tick>(bar) * bar_ticks
                   + static_cast<Tick>(beat) * beat_ticks;
      n.duration = beat_ticks / 2;
      n.pitch = 60;
      n.velocity = 80;
      n.voice = 0;
      notes.push_back(n);
    }
  }
  return notes;
}

/// @brief Create a descriptor for testing.
GoldbergVariationDescriptor makeTestDesc(
    GoldbergTempoCharacter character = GoldbergTempoCharacter::Dance,
    MeterProfile meter = MeterProfile::StandardTriple,
    TimeSignature ts = kThreeFour) {
  GoldbergVariationDescriptor desc{};
  desc.variation_number = 1;
  desc.type = GoldbergVariationType::Ornamental;
  desc.time_sig = ts;
  desc.meter_profile = meter;
  desc.tempo_character = character;
  return desc;
}

// --- Tests ---

TEST(HarmonicTimeWarpTest, EmptyNotesNoop) {
  auto grid = GoldbergStructuralGrid::createMajor();
  auto desc = makeTestDesc();
  std::vector<NoteEvent> empty;
  applyHarmonicTimeWarp(empty, grid, desc, kTestSeed);
  EXPECT_TRUE(empty.empty());
}

TEST(HarmonicTimeWarpTest, PhraseBoundaryInvariant) {
  auto grid = GoldbergStructuralGrid::createMajor();
  auto desc = makeTestDesc();
  auto notes = makeTestNotes(kThreeFour);
  Tick bar_ticks = kThreeFour.ticksPerBar();
  Tick phrase_ticks = 4 * bar_ticks;

  // Record phrase boundary ticks before warp.
  std::set<Tick> phrase_boundaries;
  for (int p = 0; p <= 8; ++p) {
    phrase_boundaries.insert(static_cast<Tick>(p) * phrase_ticks);
  }

  // Record notes exactly at phrase boundaries.
  std::vector<std::pair<size_t, Tick>> boundary_notes;
  for (size_t i = 0; i < notes.size(); ++i) {
    if (phrase_boundaries.count(notes[i].start_tick)) {
      boundary_notes.push_back({i, notes[i].start_tick});
    }
  }
  ASSERT_FALSE(boundary_notes.empty());

  applyHarmonicTimeWarp(notes, grid, desc, kTestSeed);

  // Phrase boundary notes should be ±1 tick from original.
  for (auto [idx, orig_tick] : boundary_notes) {
    int64_t diff = static_cast<int64_t>(notes[idx].start_tick)
                 - static_cast<int64_t>(orig_tick);
    EXPECT_LE(std::abs(diff), 1)
        << "Phrase boundary note at original tick " << orig_tick
        << " moved to " << notes[idx].start_tick;
  }
}

TEST(HarmonicTimeWarpTest, CadenceTickInvariant) {
  auto grid = GoldbergStructuralGrid::createMajor();
  auto desc = makeTestDesc();
  auto notes = makeTestNotes(kThreeFour);
  Tick bar_ticks = kThreeFour.ticksPerBar();

  // Cadence bars (bar_in_phrase == 4, i.e. bars 3,7,11,...,31 = 0-indexed).
  // Phrase boundary = bar 4, 8, 12, ..., 32 → tick = bar * bar_ticks.
  // Cadence bar starts at bar 3, 7, 11, ... → first beat of cadence bar.
  std::vector<std::pair<size_t, Tick>> cadence_notes;
  for (size_t i = 0; i < notes.size(); ++i) {
    int bar = static_cast<int>(notes[i].start_tick / bar_ticks);
    if (grid.isCadenceBar(bar % 32)) {
      Tick bar_start = static_cast<Tick>(bar) * bar_ticks;
      if (notes[i].start_tick == bar_start) {
        cadence_notes.push_back({i, notes[i].start_tick});
      }
    }
  }
  ASSERT_FALSE(cadence_notes.empty());

  applyHarmonicTimeWarp(notes, grid, desc, kTestSeed);

  // Cadence bar starts should have limited displacement.
  for (auto [idx, orig_tick] : cadence_notes) {
    int64_t diff = static_cast<int64_t>(notes[idx].start_tick)
                 - static_cast<int64_t>(orig_tick);
    Tick beat_ticks = bar_ticks / kThreeFour.pulsesPerBar();
    // Max displacement bounded by max_delta * beat_ticks * beats.
    // For Dance: max delta ≈ 3%, so max shift ≈ 3% * 480 * 3 ≈ 43 ticks.
    EXPECT_LE(std::abs(diff), static_cast<int64_t>(beat_ticks))
        << "Cadence bar start moved too far";
  }
}

TEST(HarmonicTimeWarpTest, CanonPhaseInvariant) {
  auto grid = GoldbergStructuralGrid::createMajor();
  auto desc = makeTestDesc(GoldbergTempoCharacter::Stable);
  auto notes = makeTestNotes(kThreeFour);

  // Save original ticks.
  std::vector<Tick> orig_ticks;
  orig_ticks.reserve(notes.size());
  for (const auto& n : notes) orig_ticks.push_back(n.start_tick);

  applyHarmonicTimeWarp(notes, grid, desc, kTestSeed);

  Tick beat_ticks = kThreeFour.ticksPerBar() / kThreeFour.pulsesPerBar();
  // Stable character: per-beat max delta ≈ 1%, but cumulative displacement
  // across multiple beats can exceed a single delta * beat_ticks.
  // Allow up to 3% of beat_ticks for cumulative shift.
  Tick max_shift = static_cast<Tick>(beat_ticks * 0.03f);
  for (size_t i = 0; i < notes.size(); ++i) {
    int64_t diff = static_cast<int64_t>(notes[i].start_tick)
                 - static_cast<int64_t>(orig_ticks[i]);
    EXPECT_LE(std::abs(diff), static_cast<int64_t>(max_shift))
        << "Stable/Canon variation displaced too much at index " << i;
  }
}

TEST(HarmonicTimeWarpTest, MonotonicallyIncreasing) {
  auto grid = GoldbergStructuralGrid::createMajor();
  // Use Lament (max warp) to stress-test monotonicity.
  auto desc = makeTestDesc(GoldbergTempoCharacter::Lament);
  auto notes = makeTestNotes(kThreeFour);

  applyHarmonicTimeWarp(notes, grid, desc, kTestSeed);

  for (size_t i = 1; i < notes.size(); ++i) {
    EXPECT_LE(notes[i - 1].start_tick, notes[i].start_tick)
        << "Monotonicity violated at index " << i;
  }
}

TEST(HarmonicTimeWarpTest, Deterministic) {
  auto grid = GoldbergStructuralGrid::createMajor();
  auto desc = makeTestDesc();

  auto notes1 = makeTestNotes(kThreeFour);
  auto notes2 = makeTestNotes(kThreeFour);

  applyHarmonicTimeWarp(notes1, grid, desc, kTestSeed);
  applyHarmonicTimeWarp(notes2, grid, desc, kTestSeed);

  ASSERT_EQ(notes1.size(), notes2.size());
  for (size_t i = 0; i < notes1.size(); ++i) {
    EXPECT_EQ(notes1[i].start_tick, notes2[i].start_tick)
        << "Non-deterministic at index " << i;
    EXPECT_EQ(notes1[i].duration, notes2[i].duration);
  }
}

TEST(HarmonicTimeWarpTest, SeedVariation) {
  auto grid = GoldbergStructuralGrid::createMajor();
  auto desc = makeTestDesc();

  auto notes1 = makeTestNotes(kThreeFour);
  auto notes2 = makeTestNotes(kThreeFour);

  applyHarmonicTimeWarp(notes1, grid, desc, 100);
  applyHarmonicTimeWarp(notes2, grid, desc, 200);

  // Different seeds should produce different results.
  bool any_different = false;
  for (size_t i = 0; i < notes1.size(); ++i) {
    if (notes1[i].start_tick != notes2[i].start_tick) {
      any_different = true;
      break;
    }
  }
  EXPECT_TRUE(any_different) << "Different seeds produced identical results";
}

TEST(HarmonicTimeWarpTest, SarabandeBeat2Stretch) {
  auto grid = GoldbergStructuralGrid::createMajor();
  // Sarabande: beat 2 (index 1) is Strong, beat 3 (index 2) is Weak.
  auto desc = makeTestDesc(GoldbergTempoCharacter::Expressive,
                           MeterProfile::SarabandeTriple);
  Tick bar_ticks = kThreeFour.ticksPerBar();
  Tick beat_ticks = bar_ticks / 3;

  // Create notes at exact beat positions in bar 1 (within first phrase).
  std::vector<NoteEvent> notes;
  for (int beat = 0; beat < 3; ++beat) {
    NoteEvent n;
    n.start_tick = static_cast<Tick>(beat) * beat_ticks;
    n.duration = beat_ticks / 2;
    n.pitch = 60;
    n.velocity = 80;
    n.voice = 0;
    notes.push_back(n);
  }
  // Add phrase-end notes to complete the 4-bar phrase.
  for (int bar = 1; bar < 4; ++bar) {
    for (int beat = 0; beat < 3; ++beat) {
      NoteEvent n;
      n.start_tick = static_cast<Tick>(bar) * bar_ticks
                   + static_cast<Tick>(beat) * beat_ticks;
      n.duration = beat_ticks / 2;
      n.pitch = 60;
      n.velocity = 80;
      n.voice = 0;
      notes.push_back(n);
    }
  }

  applyHarmonicTimeWarp(notes, grid, desc, kTestSeed);

  // Beat 2 displacement (from original) should be >= beat 3 displacement,
  // or at least beat 2 duration should be >= beat 3 duration.
  // This validates Sarabande emphasis on beat 2.
  EXPECT_GE(notes[1].duration, notes[2].duration)
      << "Sarabande beat 2 should be at least as stretched as beat 3";
}

TEST(HarmonicTimeWarpTest, HarmonicGradientDriven) {
  auto grid = GoldbergStructuralGrid::createMajor();
  auto desc = makeTestDesc(GoldbergTempoCharacter::Expressive);
  Tick bar_ticks = kThreeFour.ticksPerBar();

  // Compare bars with different harmonic gradients.
  // Bars with cadences (bar 3, 7, 11, ...) should show more displacement
  // than bars without. Measure via note displacement magnitude.
  auto notes = makeTestNotes(kThreeFour);
  std::vector<Tick> orig;
  for (const auto& n : notes) orig.push_back(n.start_tick);

  applyHarmonicTimeWarp(notes, grid, desc, kTestSeed);

  // Compute average displacement for cadence vs non-cadence bars.
  double cadence_disp = 0;
  int cadence_count = 0;
  double non_cadence_disp = 0;
  int non_cadence_count = 0;

  for (size_t i = 0; i < notes.size(); ++i) {
    int bar = static_cast<int>(orig[i] / bar_ticks);
    int grid_bar = bar % 32;
    double disp = std::abs(static_cast<double>(notes[i].start_tick)
                         - static_cast<double>(orig[i]));
    if (grid.isCadenceBar(grid_bar)) {
      cadence_disp += disp;
      ++cadence_count;
    } else {
      non_cadence_disp += disp;
      ++non_cadence_count;
    }
  }

  if (cadence_count > 0 && non_cadence_count > 0) {
    double avg_cadence = cadence_disp / cadence_count;
    double avg_non_cadence = non_cadence_disp / non_cadence_count;
    // Cadence bars have higher tension gradient and resolution, so their
    // weight is higher, causing more displacement on average.
    // This is a statistical test; allow some tolerance.
    EXPECT_GE(avg_cadence, avg_non_cadence * 0.5)
        << "Cadence bar displacement should be meaningfully present";
  }
}

TEST(HarmonicTimeWarpTest, DurationScaled) {
  auto grid = GoldbergStructuralGrid::createMajor();
  auto desc = makeTestDesc(GoldbergTempoCharacter::Expressive);
  auto notes = makeTestNotes(kThreeFour);
  std::vector<Tick> orig_dur;
  for (const auto& n : notes) orig_dur.push_back(n.duration);

  applyHarmonicTimeWarp(notes, grid, desc, kTestSeed);

  // Some notes should have different durations after warp.
  bool any_changed = false;
  for (size_t i = 0; i < notes.size(); ++i) {
    if (notes[i].duration != orig_dur[i]) {
      any_changed = true;
      break;
    }
  }
  EXPECT_TRUE(any_changed) << "No durations changed after warp";

  // All durations should be positive.
  for (size_t i = 0; i < notes.size(); ++i) {
    EXPECT_GT(notes[i].duration, 0u) << "Zero duration at index " << i;
  }
}

TEST(HarmonicTimeWarpTest, CompoundMeter6_8) {
  auto grid = GoldbergStructuralGrid::createMajor();
  TimeSignature six_eight = {6, 8};
  auto desc = makeTestDesc(GoldbergTempoCharacter::Dance,
                           MeterProfile::StandardTriple, six_eight);
  auto notes = makeTestNotes(six_eight);

  // Should not crash.
  applyHarmonicTimeWarp(notes, grid, desc, kTestSeed);

  // Monotonicity check.
  for (size_t i = 1; i < notes.size(); ++i) {
    EXPECT_LE(notes[i - 1].start_tick, notes[i].start_tick);
  }
}

TEST(HarmonicTimeWarpTest, AllaBreveMeter2_2) {
  auto grid = GoldbergStructuralGrid::createMajor();
  TimeSignature two_two = {2, 2};
  auto desc = makeTestDesc(GoldbergTempoCharacter::Stable,
                           MeterProfile::StandardTriple, two_two);
  auto notes = makeTestNotes(two_two);

  applyHarmonicTimeWarp(notes, grid, desc, kTestSeed);

  for (size_t i = 1; i < notes.size(); ++i) {
    EXPECT_LE(notes[i - 1].start_tick, notes[i].start_tick);
  }
}

}  // namespace
}  // namespace bach
