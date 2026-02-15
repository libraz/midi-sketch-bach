// Tests for solo_string/arch/chaconne_scheme.h -- harmonic scheme for chaconne form.

#include "solo_string/arch/chaconne_scheme.h"

#include <gtest/gtest.h>

#include "core/basic_types.h"
#include "harmony/chord_types.h"
#include "harmony/harmonic_timeline.h"
#include "harmony/key.h"

namespace bach {
namespace {

// ===========================================================================
// Standard D minor scheme properties
// ===========================================================================

TEST(ChaconneSchemeTest, StandardDMinorHasSevenEntries) {
  auto scheme = ChaconneScheme::createStandardDMinor();
  EXPECT_EQ(scheme.size(), 7u);
}

TEST(ChaconneSchemeTest, StandardDMinorHasSixteenBeatTotal) {
  auto scheme = ChaconneScheme::createStandardDMinor();
  // 16 beats * 480 ticks/beat = 7680 ticks.
  EXPECT_EQ(scheme.getLengthTicks(), 16u * kTicksPerBeat);
}

TEST(ChaconneSchemeTest, GetLengthTicksReturns7680) {
  auto scheme = ChaconneScheme::createStandardDMinor();
  EXPECT_EQ(scheme.getLengthTicks(), 7680u);
}

// ===========================================================================
// Determinism: toTimeline produces identical results on repeated calls
// ===========================================================================

TEST(ChaconneSchemeTest, ToTimelineIsDeterministic) {
  auto scheme = ChaconneScheme::createStandardDMinor();
  KeySignature key{Key::D, true};
  Tick duration = scheme.getLengthTicks();

  auto timeline_a = scheme.toTimeline(key, duration);
  auto timeline_b = scheme.toTimeline(key, duration);

  ASSERT_EQ(timeline_a.size(), timeline_b.size());
  for (size_t i = 0; i < timeline_a.size(); ++i) {
    EXPECT_EQ(timeline_a.events()[i].chord.degree, timeline_b.events()[i].chord.degree)
        << "Degree mismatch at event " << i;
    EXPECT_EQ(timeline_a.events()[i].chord.quality, timeline_b.events()[i].chord.quality)
        << "Quality mismatch at event " << i;
    EXPECT_EQ(timeline_a.events()[i].tick, timeline_b.events()[i].tick)
        << "Tick mismatch at event " << i;
    EXPECT_EQ(timeline_a.events()[i].end_tick, timeline_b.events()[i].end_tick)
        << "End tick mismatch at event " << i;
  }
}

// ===========================================================================
// Integrity verification: correct timeline passes
// ===========================================================================

TEST(ChaconneSchemeTest, VerifyIntegrityPassesForCorrectTimeline) {
  auto scheme = ChaconneScheme::createStandardDMinor();
  KeySignature key{Key::D, true};
  auto timeline = scheme.toTimeline(key, scheme.getLengthTicks());

  EXPECT_TRUE(scheme.verifyIntegrity(timeline));
}

// ===========================================================================
// Integrity verification: degree change causes failure
// ===========================================================================

TEST(ChaconneSchemeTest, VerifyIntegrityFailsWhenDegreeChanged) {
  auto scheme = ChaconneScheme::createStandardDMinor();
  KeySignature key{Key::D, true};
  auto timeline = scheme.toTimeline(key, scheme.getLengthTicks());

  // Mutate the third event's degree (index 2: i -> something else).
  ASSERT_GT(timeline.size(), 2u);
  auto& events = timeline.mutableEvents();
  ChordDegree original = events[2].chord.degree;
  // Pick a different degree.
  events[2].chord.degree = (original == ChordDegree::IV) ? ChordDegree::V : ChordDegree::IV;

  EXPECT_FALSE(scheme.verifyIntegrity(timeline));

  // Also verify that the FailReport has critical issues.
  auto report = scheme.verifyIntegrityReport(timeline);
  EXPECT_TRUE(report.hasCritical());

  // The report should contain a degree mismatch rule.
  bool found_degree_mismatch = false;
  for (const auto& issue : report.issues) {
    if (issue.rule == "scheme_degree_mismatch") {
      found_degree_mismatch = true;
      break;
    }
  }
  EXPECT_TRUE(found_degree_mismatch) << "Expected 'scheme_degree_mismatch' in report";
}

// ===========================================================================
// Integrity verification: inversion change does NOT cause failure
// ===========================================================================

TEST(ChaconneSchemeTest, VerifyIntegrityPassesWhenInversionChanged) {
  auto scheme = ChaconneScheme::createStandardDMinor();
  KeySignature key{Key::D, true};
  auto timeline = scheme.toTimeline(key, scheme.getLengthTicks());

  // Modify one event's chord inversion to first inversion.
  ASSERT_GT(timeline.size(), 0u);
  auto& events = timeline.mutableEvents();
  events[0].chord.inversion = 1;

  // Integrity should still pass since inversion is not checked.
  EXPECT_TRUE(scheme.verifyIntegrity(timeline));
}

// ===========================================================================
// createForKey produces same degree structure as createStandardDMinor
// ===========================================================================

TEST(ChaconneSchemeTest, CreateForKeyHasSameDegreeStructure) {
  auto standard = ChaconneScheme::createStandardDMinor();
  auto transposed = ChaconneScheme::createForKey(KeySignature{Key::G, true});

  // Same number of entries.
  ASSERT_EQ(standard.size(), transposed.size());

  // Same degree and quality for each entry.
  for (size_t i = 0; i < standard.size(); ++i) {
    EXPECT_EQ(standard.entries()[i].degree, transposed.entries()[i].degree)
        << "Degree mismatch at entry " << i;
    EXPECT_EQ(standard.entries()[i].quality, transposed.entries()[i].quality)
        << "Quality mismatch at entry " << i;
    EXPECT_EQ(standard.entries()[i].position_beats, transposed.entries()[i].position_beats)
        << "Position mismatch at entry " << i;
    EXPECT_EQ(standard.entries()[i].duration_beats, transposed.entries()[i].duration_beats)
        << "Duration mismatch at entry " << i;
  }
}

// ===========================================================================
// Custom SchemeEntry list constructs successfully
// ===========================================================================

TEST(ChaconneSchemeTest, CustomSchemeConstructsSuccessfully) {
  std::vector<SchemeEntry> custom_entries;
  custom_entries.push_back({ChordDegree::I, ChordQuality::Major, 0, 1.0f, 0, 4});
  custom_entries.push_back({ChordDegree::V, ChordQuality::Major, 0, 0.75f, 4, 4});

  ChaconneScheme scheme(custom_entries);
  EXPECT_EQ(scheme.size(), 2u);
  EXPECT_EQ(scheme.getLengthTicks(), 8u * kTicksPerBeat);  // 8 beats total.
  EXPECT_EQ(scheme.entries()[0].degree, ChordDegree::I);
  EXPECT_EQ(scheme.entries()[1].degree, ChordDegree::V);
}

// ===========================================================================
// Empty scheme edge case
// ===========================================================================

TEST(ChaconneSchemeTest, EmptySchemeHasZeroLength) {
  ChaconneScheme scheme;
  EXPECT_EQ(scheme.size(), 0u);
  EXPECT_EQ(scheme.getLengthTicks(), 0u);
}

TEST(ChaconneSchemeTest, EmptySchemeToTimelineReturnsEmpty) {
  ChaconneScheme scheme;
  KeySignature key{Key::C, false};
  auto timeline = scheme.toTimeline(key, 1920);
  EXPECT_EQ(timeline.size(), 0u);
}

// ===========================================================================
// Timeline content verification
// ===========================================================================

TEST(ChaconneSchemeTest, TimelineEventCountMatchesSchemeSize) {
  auto scheme = ChaconneScheme::createStandardDMinor();
  KeySignature key{Key::D, true};
  auto timeline = scheme.toTimeline(key, scheme.getLengthTicks());
  EXPECT_EQ(timeline.size(), scheme.size());
}

TEST(ChaconneSchemeTest, TimelineEventsAreImmutable) {
  auto scheme = ChaconneScheme::createStandardDMinor();
  KeySignature key{Key::D, true};
  auto timeline = scheme.toTimeline(key, scheme.getLengthTicks());

  for (const auto& event : timeline.events()) {
    EXPECT_TRUE(event.is_immutable) << "Event at tick " << event.tick << " should be immutable";
  }
}

TEST(ChaconneSchemeTest, TimelineEventsHaveCorrectKey) {
  auto scheme = ChaconneScheme::createStandardDMinor();
  KeySignature key{Key::D, true};
  auto timeline = scheme.toTimeline(key, scheme.getLengthTicks());

  for (const auto& event : timeline.events()) {
    EXPECT_EQ(event.key, Key::D) << "Event at tick " << event.tick << " has wrong key";
    EXPECT_TRUE(event.is_minor) << "Event at tick " << event.tick << " should be minor";
  }
}

TEST(ChaconneSchemeTest, TimelineEventsHaveRootPositionInversion) {
  auto scheme = ChaconneScheme::createStandardDMinor();
  KeySignature key{Key::D, true};
  auto timeline = scheme.toTimeline(key, scheme.getLengthTicks());

  for (const auto& event : timeline.events()) {
    EXPECT_EQ(event.chord.inversion, 0u)
        << "Event at tick " << event.tick << " should be root position";
  }
}

// ===========================================================================
// Degree sequence matches BWV1004: i - V - i - iv - VII - III - V
// ===========================================================================

TEST(ChaconneSchemeTest, StandardDMinorDegreeSequenceIsCorrect) {
  auto scheme = ChaconneScheme::createStandardDMinor();
  ASSERT_EQ(scheme.size(), 7u);

  const auto& entries = scheme.entries();
  EXPECT_EQ(entries[0].degree, ChordDegree::I);
  EXPECT_EQ(entries[1].degree, ChordDegree::V);
  EXPECT_EQ(entries[2].degree, ChordDegree::I);
  EXPECT_EQ(entries[3].degree, ChordDegree::IV);
  EXPECT_EQ(entries[4].degree, ChordDegree::bVII);
  EXPECT_EQ(entries[5].degree, ChordDegree::iii);
  EXPECT_EQ(entries[6].degree, ChordDegree::V);
}

TEST(ChaconneSchemeTest, StandardDMinorQualitySequenceIsCorrect) {
  auto scheme = ChaconneScheme::createStandardDMinor();
  ASSERT_EQ(scheme.size(), 7u);

  const auto& entries = scheme.entries();
  EXPECT_EQ(entries[0].quality, ChordQuality::Minor);   // i
  EXPECT_EQ(entries[1].quality, ChordQuality::Major);   // V
  EXPECT_EQ(entries[2].quality, ChordQuality::Minor);   // i
  EXPECT_EQ(entries[3].quality, ChordQuality::Minor);   // iv
  EXPECT_EQ(entries[4].quality, ChordQuality::Major);   // VII
  EXPECT_EQ(entries[5].quality, ChordQuality::Major);   // III
  EXPECT_EQ(entries[6].quality, ChordQuality::Major);   // V
}

// ===========================================================================
// Quality change also triggers integrity failure
// ===========================================================================

TEST(ChaconneSchemeTest, VerifyIntegrityFailsWhenQualityChanged) {
  auto scheme = ChaconneScheme::createStandardDMinor();
  KeySignature key{Key::D, true};
  auto timeline = scheme.toTimeline(key, scheme.getLengthTicks());

  ASSERT_GT(timeline.size(), 0u);
  auto& events = timeline.mutableEvents();
  // Change the first event's quality from Minor to Major.
  events[0].chord.quality = ChordQuality::Major;

  EXPECT_FALSE(scheme.verifyIntegrity(timeline));

  auto report = scheme.verifyIntegrityReport(timeline);
  EXPECT_TRUE(report.hasCritical());

  bool found_quality_mismatch = false;
  for (const auto& issue : report.issues) {
    if (issue.rule == "scheme_quality_mismatch") {
      found_quality_mismatch = true;
      break;
    }
  }
  EXPECT_TRUE(found_quality_mismatch) << "Expected 'scheme_quality_mismatch' in report";
}

// ===========================================================================
// Timeline with different key produces different root pitches
// ===========================================================================

TEST(ChaconneSchemeTest, DifferentKeyProducesDifferentRootPitches) {
  auto scheme = ChaconneScheme::createStandardDMinor();
  Tick duration = scheme.getLengthTicks();

  auto tl_d = scheme.toTimeline(KeySignature{Key::D, true}, duration);
  auto tl_g = scheme.toTimeline(KeySignature{Key::G, true}, duration);

  ASSERT_EQ(tl_d.size(), tl_g.size());

  // Root pitches should differ because the tonic pitch class differs.
  bool any_root_diff = false;
  for (size_t i = 0; i < tl_d.size(); ++i) {
    if (tl_d.events()[i].chord.root_pitch != tl_g.events()[i].chord.root_pitch) {
      any_root_diff = true;
      break;
    }
  }
  EXPECT_TRUE(any_root_diff) << "Different keys should produce different root pitches";
}

}  // namespace
}  // namespace bach
