// Tests for solo_string/arch/chaconne_analyzer.h -- chaconne quality analysis.

#include "solo_string/arch/chaconne_analyzer.h"

#include <random>
#include <vector>

#include <gtest/gtest.h>

#include "core/basic_types.h"
#include "solo_string/arch/chaconne_config.h"
#include "solo_string/arch/chaconne_scheme.h"
#include "solo_string/arch/variation_types.h"

namespace bach {
namespace {

// ===========================================================================
// Test helpers
// ===========================================================================

/// @brief Create a standard ChaconneConfig with D minor key and default variation plan.
/// @return Fully initialized ChaconneConfig.
ChaconneConfig createTestChaconneConfig() {
  ChaconneConfig config;
  config.key = {Key::D, true};  // D minor
  config.bpm = 60;
  config.seed = 42;
  config.instrument = InstrumentType::Violin;
  std::mt19937 rng(42);
  config.variations = createStandardVariationPlan(config.key, rng);
  return config;
}

/// @brief Create bass notes (ChaconneBass-sourced) at each scheme entry position.
///
/// For each variation, places a ChaconneBass note at each SchemeEntry's beat
/// position. Uses D3 (50) as the bass pitch.
///
/// @param config Chaconne config with variation plan.
/// @param scheme The harmonic scheme defining bass positions.
/// @param[out] track Track to append bass notes to.
void addSchemeBassTicks(const ChaconneConfig& config,
                        const ChaconneScheme& scheme,
                        Track& track) {
  Tick cycle_length = scheme.getLengthTicks();
  if (cycle_length == 0) return;

  int num_variations = static_cast<int>(config.variations.size());
  const auto& entries = scheme.entries();

  for (int var_idx = 0; var_idx < num_variations; ++var_idx) {
    Tick var_start = static_cast<Tick>(var_idx) * cycle_length;

    for (const auto& entry : entries) {
      NoteEvent note;
      note.start_tick =
          var_start + static_cast<Tick>(entry.position_beats) * kTicksPerBeat;
      note.duration =
          static_cast<Tick>(entry.duration_beats) * kTicksPerBeat;
      note.pitch = 50;  // D3 -- typical bass range for violin chaconne
      note.velocity = 80;
      note.voice = 0;
      note.source = BachNoteSource::ChaconneBass;
      track.notes.push_back(note);
    }
  }
}

/// @brief Create a track containing ChaconneBass notes for every variation.
///
/// For each variation, places ChaconneBass-sourced notes at each SchemeEntry's
/// beat position. Also adds upper voice notes with varied pitches to simulate
/// a real piece.
///
/// @param config Chaconne config with variation plan.
/// @param scheme The harmonic scheme defining bass positions.
/// @return Track with bass + upper voice notes.
Track createIdealChaconneTrack(const ChaconneConfig& config,
                               const ChaconneScheme& scheme) {
  Track track;
  track.channel = 0;
  track.program = 40;  // Violin
  track.name = "Violin";

  Tick cycle_length = scheme.getLengthTicks();
  if (cycle_length == 0) return track;

  int num_variations = static_cast<int>(config.variations.size());

  // Place ChaconneBass notes at scheme entry positions.
  addSchemeBassTicks(config, scheme, track);

  // Collect bass note ticks for skip logic.
  const auto& entries = scheme.entries();

  for (int var_idx = 0; var_idx < num_variations; ++var_idx) {
    Tick var_start = static_cast<Tick>(var_idx) * cycle_length;
    const auto& variation = config.variations[static_cast<size_t>(var_idx)];

    // Add upper voice notes with register variation based on the variation.
    // Use different pitch ranges per variation role to create distinct sections.
    uint8_t base_pitch = 67;  // G4 default

    switch (variation.role) {
      case VariationRole::Establish:
        base_pitch = 67;  // G4
        break;
      case VariationRole::Develop:
        base_pitch = 72;  // C5
        break;
      case VariationRole::Destabilize:
        base_pitch = 76;  // E5
        break;
      case VariationRole::Illuminate:
        // Major section: higher register, different character.
        base_pitch = 79;  // G5
        break;
      case VariationRole::Accumulate:
        base_pitch = 84;  // C6 (high climax)
        break;
      case VariationRole::Resolve:
        base_pitch = 62;  // D4 (return to tonic)
        break;
    }

    // Generate upper voice notes: 8th notes across the variation.
    Tick eighth_note = kTicksPerBeat / 2;  // 240 ticks
    for (Tick tick = var_start; tick < var_start + cycle_length; tick += eighth_note) {
      // Skip ticks where a bass note starts (to avoid duplicate ticks).
      bool is_bass_tick = false;
      for (const auto& entry : entries) {
        Tick bass_tick =
            var_start + static_cast<Tick>(entry.position_beats) * kTicksPerBeat;
        if (tick == bass_tick) {
          is_bass_tick = true;
          break;
        }
      }
      if (is_bass_tick) continue;

      NoteEvent note;
      note.start_tick = tick;
      note.duration = eighth_note;
      note.velocity = 80;
      note.voice = 0;

      // Alternate pitches around the base for voice switching behavior.
      int offset_idx = static_cast<int>((tick - var_start) / eighth_note);
      int offsets[] = {0, -5, 7, -3, 4, -7, 2, -4};
      int pitch_offset = offsets[offset_idx % 8];
      int pitch = static_cast<int>(base_pitch) + pitch_offset;
      if (pitch < 55) pitch = 55;  // Violin low limit
      if (pitch > 96) pitch = 96;  // Violin high limit
      note.pitch = static_cast<uint8_t>(pitch);

      track.notes.push_back(note);
    }
  }

  // Sort notes by start_tick for consistency.
  std::sort(track.notes.begin(), track.notes.end(),
            [](const NoteEvent& lhs, const NoteEvent& rhs) {
              return lhs.start_tick < rhs.start_tick;
            });

  return track;
}

/// @brief Create a track with ImpliedPolyphony-style notes for polyphony testing.
///
/// Generates notes in multiple register bands (octaves) to simulate
/// implied polyphony. Each beat has notes in 2-3 distinct octave bands.
///
/// @param config Chaconne config.
/// @param scheme Harmonic scheme for variation length.
/// @return Track with implied polyphony patterns.
Track createImpliedPolyphonyTrack(const ChaconneConfig& config,
                                  const ChaconneScheme& scheme) {
  Track track;
  track.channel = 0;
  track.program = 40;  // Violin
  track.name = "Violin";

  Tick cycle_length = scheme.getLengthTicks();
  if (cycle_length == 0) return track;

  int num_variations = static_cast<int>(config.variations.size());
  const auto& entries = scheme.entries();

  for (int var_idx = 0; var_idx < num_variations; ++var_idx) {
    Tick var_start = static_cast<Tick>(var_idx) * cycle_length;

    // Place ChaconneBass notes at scheme positions.
    for (const auto& entry : entries) {
      NoteEvent note;
      note.start_tick =
          var_start + static_cast<Tick>(entry.position_beats) * kTicksPerBeat;
      note.duration =
          static_cast<Tick>(entry.duration_beats) * kTicksPerBeat;
      note.pitch = 50;  // D3
      note.velocity = 80;
      note.voice = 0;
      note.source = BachNoteSource::ChaconneBass;
      track.notes.push_back(note);
    }

    // For ImpliedPolyphony variations, create notes in distinct octave bands.
    if (config.variations[static_cast<size_t>(var_idx)].primary_texture ==
        TextureType::ImpliedPolyphony) {
      // Place notes in 3 register bands: bass (48-59), mid (60-71), high (72-83).
      Tick sixteenth = kTicksPerBeat / 4;  // 120 ticks
      for (Tick tick = var_start; tick < var_start + cycle_length; tick += sixteenth) {
        // Skip bass note ticks.
        bool is_bass_tick = false;
        for (const auto& entry : entries) {
          Tick bass_tick =
              var_start + static_cast<Tick>(entry.position_beats) * kTicksPerBeat;
          if (tick == bass_tick) {
            is_bass_tick = true;
            break;
          }
        }
        if (is_bass_tick) continue;

        int beat_pos = static_cast<int>((tick - var_start) / sixteenth) % 4;

        // Alternate between register bands: low, high, mid, high.
        uint8_t pitches[] = {55, 79, 67, 79};  // G3, G5, G4, G5
        NoteEvent note;
        note.start_tick = tick;
        note.duration = sixteenth;
        note.velocity = 80;
        note.voice = 0;
        note.pitch = pitches[beat_pos];
        track.notes.push_back(note);
      }
    } else {
      // Non-implied-polyphony: single line.
      Tick eighth = kTicksPerBeat / 2;
      for (Tick tick = var_start; tick < var_start + cycle_length; tick += eighth) {
        bool is_bass_tick = false;
        for (const auto& entry : entries) {
          Tick bass_tick =
              var_start + static_cast<Tick>(entry.position_beats) * kTicksPerBeat;
          if (tick == bass_tick) {
            is_bass_tick = true;
            break;
          }
        }
        if (is_bass_tick) continue;

        NoteEvent note;
        note.start_tick = tick;
        note.duration = eighth;
        note.velocity = 80;
        note.voice = 0;
        note.pitch = 67;  // G4 single line
        track.notes.push_back(note);
      }
    }
  }

  std::sort(track.notes.begin(), track.notes.end(),
            [](const NoteEvent& lhs, const NoteEvent& rhs) {
              return lhs.start_tick < rhs.start_tick;
            });

  return track;
}

/// @brief Create a track with missing bass at variation 0 (no ChaconneBass notes).
/// @param config Chaconne config.
/// @param scheme Harmonic scheme.
/// @return Track with missing bass coverage in variation 0.
Track createMissingBassTrack(const ChaconneConfig& config,
                              const ChaconneScheme& scheme) {
  Track track = createIdealChaconneTrack(config, scheme);

  // Remove all ChaconneBass notes from variation 0.
  Tick cycle_length = scheme.getLengthTicks();
  track.notes.erase(
      std::remove_if(track.notes.begin(), track.notes.end(),
                     [cycle_length](const NoteEvent& note) {
                       return note.start_tick < cycle_length &&
                              note.source == BachNoteSource::ChaconneBass;
                     }),
      track.notes.end());

  return track;
}

// ===========================================================================
// Instant-FAIL metric tests
// ===========================================================================

TEST(ChaconneAnalyzerTest, HarmonicSchemeIntegrityPassesWithCorrectBass) {
  auto config = createTestChaconneConfig();
  auto scheme = ChaconneScheme::createStandardDMinor();
  auto track = createIdealChaconneTrack(config, scheme);

  auto result = analyzeChaconne({track}, config, scheme);
  EXPECT_FLOAT_EQ(result.harmonic_scheme_integrity, 1.0f);
}

TEST(ChaconneAnalyzerTest, HarmonicSchemeIntegrityFailsWithMissingBass) {
  auto config = createTestChaconneConfig();
  auto scheme = ChaconneScheme::createStandardDMinor();
  auto track = createMissingBassTrack(config, scheme);

  auto result = analyzeChaconne({track}, config, scheme);
  EXPECT_FLOAT_EQ(result.harmonic_scheme_integrity, 0.0f);
  EXPECT_FALSE(result.isPass());
}

TEST(ChaconneAnalyzerTest, HarmonicSchemeIntegrityFailsWithEmptyScheme) {
  auto config = createTestChaconneConfig();
  ChaconneScheme empty_scheme;
  Track track;
  track.channel = 0;

  auto result = analyzeChaconne({track}, config, empty_scheme);
  EXPECT_FLOAT_EQ(result.harmonic_scheme_integrity, 0.0f);
}

TEST(ChaconneAnalyzerTest, RoleOrderScorePassesWithStandardPlan) {
  auto config = createTestChaconneConfig();
  auto scheme = ChaconneScheme::createStandardDMinor();
  auto track = createIdealChaconneTrack(config, scheme);

  auto result = analyzeChaconne({track}, config, scheme);
  EXPECT_FLOAT_EQ(result.role_order_score, 1.0f);
}

TEST(ChaconneAnalyzerTest, RoleOrderScoreFailsWithReversedRoles) {
  auto config = createTestChaconneConfig();
  // Reverse the variation plan to break role order.
  std::reverse(config.variations.begin(), config.variations.end());

  auto scheme = ChaconneScheme::createStandardDMinor();
  auto track = createIdealChaconneTrack(config, scheme);

  auto result = analyzeChaconne({track}, config, scheme);
  EXPECT_FLOAT_EQ(result.role_order_score, 0.0f);
  EXPECT_FALSE(result.isPass());
}

TEST(ChaconneAnalyzerTest, RoleOrderScoreFailsWithEmptyPlan) {
  ChaconneConfig config;
  config.variations.clear();

  auto scheme = ChaconneScheme::createStandardDMinor();
  auto result = analyzeChaconne({}, config, scheme);
  EXPECT_FLOAT_EQ(result.role_order_score, 0.0f);
}

TEST(ChaconneAnalyzerTest, ClimaxPresencePassesWithThreeAccumulate) {
  auto config = createTestChaconneConfig();
  auto scheme = ChaconneScheme::createStandardDMinor();
  auto track = createIdealChaconneTrack(config, scheme);

  auto result = analyzeChaconne({track}, config, scheme);
  EXPECT_FLOAT_EQ(result.climax_presence_score, 1.0f);
  EXPECT_EQ(result.accumulate_count, 3);
}

TEST(ChaconneAnalyzerTest, ClimaxPresenceFailsWithTwoAccumulate) {
  auto config = createTestChaconneConfig();

  // Remove one Accumulate variation.
  auto iter = std::find_if(config.variations.begin(), config.variations.end(),
                           [](const ChaconneVariation& var) {
                             return var.role == VariationRole::Accumulate;
                           });
  if (iter != config.variations.end()) {
    config.variations.erase(iter);
  }

  auto scheme = ChaconneScheme::createStandardDMinor();
  auto result = analyzeChaconne({}, config, scheme);
  EXPECT_FLOAT_EQ(result.climax_presence_score, 0.0f);
}

TEST(ChaconneAnalyzerTest, ClimaxPresenceFailsWithFourAccumulate) {
  auto config = createTestChaconneConfig();

  // Add an extra Accumulate variation before Resolve.
  auto resolve_iter = std::find_if(config.variations.begin(), config.variations.end(),
                                   [](const ChaconneVariation& var) {
                                     return var.role == VariationRole::Resolve;
                                   });
  ChaconneVariation extra_acc;
  extra_acc.role = VariationRole::Accumulate;
  extra_acc.type = VariationType::Virtuosic;
  extra_acc.primary_texture = TextureType::FullChords;
  config.variations.insert(resolve_iter, extra_acc);

  auto scheme = ChaconneScheme::createStandardDMinor();
  auto result = analyzeChaconne({}, config, scheme);
  EXPECT_FLOAT_EQ(result.climax_presence_score, 0.0f);
  EXPECT_EQ(result.accumulate_count, 4);
}

TEST(ChaconneAnalyzerTest, ImpliedPolyphonyScorePassesWithNoImpliedPolyphony) {
  // If there are no ImpliedPolyphony textures, score = 1.0 (not applicable).
  auto config = createTestChaconneConfig();
  // Replace all ImpliedPolyphony textures with SingleLine.
  for (auto& var : config.variations) {
    if (var.primary_texture == TextureType::ImpliedPolyphony) {
      var.primary_texture = TextureType::SingleLine;
    }
  }

  auto scheme = ChaconneScheme::createStandardDMinor();
  auto track = createIdealChaconneTrack(config, scheme);

  auto result = analyzeChaconne({track}, config, scheme);
  EXPECT_FLOAT_EQ(result.implied_polyphony_score, 1.0f);
}

TEST(ChaconneAnalyzerTest, ImpliedPolyphonyTrackPopulatesDiagnostic) {
  auto config = createTestChaconneConfig();
  auto scheme = ChaconneScheme::createStandardDMinor();
  auto track = createImpliedPolyphonyTrack(config, scheme);

  auto result = analyzeChaconne({track}, config, scheme);
  // The implied polyphony track has notes in multiple register bands,
  // so the diagnostic average should be populated.
  EXPECT_GT(result.implied_voice_count_avg, 0.0f);
}

// ===========================================================================
// Threshold metric tests
// ===========================================================================

TEST(ChaconneAnalyzerTest, VariationDiversityWithStandardPlan) {
  auto config = createTestChaconneConfig();
  auto scheme = ChaconneScheme::createStandardDMinor();
  auto track = createIdealChaconneTrack(config, scheme);

  auto result = analyzeChaconne({track}, config, scheme);
  // Standard plan uses 5 distinct textures: SingleLine, ImpliedPolyphony,
  // ScalePassage, Arpeggiated, FullChords.
  EXPECT_GE(result.variation_diversity, 0.7f);
}

TEST(ChaconneAnalyzerTest, VariationDiversityFailsWithSingleTexture) {
  auto config = createTestChaconneConfig();
  // Set all variations to the same texture.
  for (auto& var : config.variations) {
    var.primary_texture = TextureType::SingleLine;
  }

  auto scheme = ChaconneScheme::createStandardDMinor();
  auto track = createIdealChaconneTrack(config, scheme);

  auto result = analyzeChaconne({track}, config, scheme);
  EXPECT_FLOAT_EQ(result.variation_diversity, 0.0f);
  EXPECT_LT(result.variation_diversity, 0.7f);
}

TEST(ChaconneAnalyzerTest, TextureTransitionScoreWithStandardPlan) {
  auto config = createTestChaconneConfig();
  auto scheme = ChaconneScheme::createStandardDMinor();
  auto track = createIdealChaconneTrack(config, scheme);

  auto result = analyzeChaconne({track}, config, scheme);
  // Standard plan has varied textures with smooth transitions.
  EXPECT_GE(result.texture_transition_score, 0.5f);
}

TEST(ChaconneAnalyzerTest, TextureTransitionScoreFailsWithIdenticalAdjacent) {
  auto config = createTestChaconneConfig();
  // Set all variations to identical textures.
  for (auto& var : config.variations) {
    var.primary_texture = TextureType::SingleLine;
  }

  auto scheme = ChaconneScheme::createStandardDMinor();
  auto track = createIdealChaconneTrack(config, scheme);

  auto result = analyzeChaconne({track}, config, scheme);
  // All identical: every transition scores 0.
  EXPECT_FLOAT_EQ(result.texture_transition_score, 0.0f);
}

TEST(ChaconneAnalyzerTest, SectionBalanceWithStandardPlan) {
  auto config = createTestChaconneConfig();
  auto scheme = ChaconneScheme::createStandardDMinor();
  auto track = createIdealChaconneTrack(config, scheme);

  auto result = analyzeChaconne({track}, config, scheme);
  // Standard plan: 3 front + 2 major + 5 back = 10 variations.
  // Proportions: 30% / 20% / 50% -- exactly ideal.
  EXPECT_GE(result.section_balance, 0.7f);
}

TEST(ChaconneAnalyzerTest, VoiceSwitchFrequencyWithVariedPitches) {
  auto config = createTestChaconneConfig();
  auto scheme = ChaconneScheme::createStandardDMinor();
  auto track = createIdealChaconneTrack(config, scheme);

  auto result = analyzeChaconne({track}, config, scheme);
  // Ideal track alternates register positions, creating switches.
  EXPECT_GT(result.voice_switch_frequency, 0.0f);
}

TEST(ChaconneAnalyzerTest, VoiceSwitchFrequencyFailsWithMonotonePitch) {
  auto config = createTestChaconneConfig();
  auto scheme = ChaconneScheme::createStandardDMinor();

  // Create a track with all notes at the same pitch.
  Track track;
  track.channel = 0;
  track.name = "Monotone";

  Tick cycle_length = scheme.getLengthTicks();
  int num_variations = static_cast<int>(config.variations.size());
  Tick total_ticks = static_cast<Tick>(num_variations) * cycle_length;

  for (Tick tick = 0; tick < total_ticks; tick += kTicksPerBeat) {
    NoteEvent note;
    note.start_tick = tick;
    note.duration = kTicksPerBeat;
    note.pitch = 67;  // Always G4
    note.velocity = 80;
    note.voice = 0;
    track.notes.push_back(note);
  }

  auto result = analyzeChaconne({track}, config, scheme);
  // No pitch variation => no switches.
  EXPECT_FLOAT_EQ(result.voice_switch_frequency, 0.0f);
}

// ===========================================================================
// isPass / getFailures / summary tests
// ===========================================================================

TEST(ChaconneAnalysisResultTest, DefaultResultFails) {
  ChaconneAnalysisResult result;
  EXPECT_FALSE(result.isPass());
}

TEST(ChaconneAnalysisResultTest, GetFailuresListsAllDefaults) {
  ChaconneAnalysisResult result;
  auto failures = result.getFailures();
  // All metrics at 0.0 should be failing.
  EXPECT_GE(failures.size(), 7u);  // 4 instant-FAIL + 3 threshold at minimum
}

TEST(ChaconneAnalysisResultTest, PassingResultHasNoFailures) {
  ChaconneAnalysisResult result;
  // Set all instant-FAIL metrics to passing values.
  result.harmonic_scheme_integrity = 1.0f;
  result.role_order_score = 1.0f;
  result.climax_presence_score = 1.0f;
  result.implied_polyphony_score = 1.0f;
  // Set all threshold metrics to passing values.
  result.variation_diversity = 0.8f;
  result.texture_transition_score = 0.6f;
  result.section_balance = 0.8f;
  result.major_section_separation = 0.7f;
  result.voice_switch_frequency = 0.5f;

  EXPECT_TRUE(result.isPass());
  EXPECT_TRUE(result.getFailures().empty());
}

TEST(ChaconneAnalysisResultTest, SingleInstantFailCausesOverallFail) {
  ChaconneAnalysisResult result;
  result.harmonic_scheme_integrity = 0.0f;  // FAIL
  result.role_order_score = 1.0f;
  result.climax_presence_score = 1.0f;
  result.implied_polyphony_score = 1.0f;
  result.variation_diversity = 0.8f;
  result.texture_transition_score = 0.6f;
  result.section_balance = 0.8f;
  result.major_section_separation = 0.7f;
  result.voice_switch_frequency = 0.5f;

  EXPECT_FALSE(result.isPass());
  auto failures = result.getFailures();
  ASSERT_EQ(failures.size(), 1u);
  EXPECT_NE(failures[0].find("harmonic_scheme_integrity"), std::string::npos);
}

TEST(ChaconneAnalysisResultTest, SingleThresholdFailCausesOverallFail) {
  ChaconneAnalysisResult result;
  result.harmonic_scheme_integrity = 1.0f;
  result.role_order_score = 1.0f;
  result.climax_presence_score = 1.0f;
  result.implied_polyphony_score = 1.0f;
  result.variation_diversity = 0.5f;  // Below 0.7 threshold
  result.texture_transition_score = 0.6f;
  result.section_balance = 0.8f;
  result.major_section_separation = 0.7f;
  result.voice_switch_frequency = 0.5f;

  EXPECT_FALSE(result.isPass());
  auto failures = result.getFailures();
  ASSERT_EQ(failures.size(), 1u);
  EXPECT_NE(failures[0].find("variation_diversity"), std::string::npos);
}

TEST(ChaconneAnalysisResultTest, SummaryContainsPassOrFail) {
  ChaconneAnalysisResult result;
  std::string sum = result.summary();
  EXPECT_NE(sum.find("FAIL"), std::string::npos);

  // Set all passing.
  result.harmonic_scheme_integrity = 1.0f;
  result.role_order_score = 1.0f;
  result.climax_presence_score = 1.0f;
  result.implied_polyphony_score = 1.0f;
  result.variation_diversity = 0.8f;
  result.texture_transition_score = 0.6f;
  result.section_balance = 0.8f;
  result.major_section_separation = 0.7f;
  result.voice_switch_frequency = 0.5f;

  sum = result.summary();
  EXPECT_NE(sum.find("PASS"), std::string::npos);
}

TEST(ChaconneAnalysisResultTest, SummaryContainsDiagnostics) {
  ChaconneAnalysisResult result;
  result.implied_voice_count_avg = 2.5f;
  result.accumulate_count = 3;

  std::string sum = result.summary();
  EXPECT_NE(sum.find("implied_voice_count_avg"), std::string::npos);
  EXPECT_NE(sum.find("accumulate_count"), std::string::npos);
}

// ===========================================================================
// Edge case tests
// ===========================================================================

TEST(ChaconneAnalyzerTest, EmptyTracksReturnZeroScores) {
  auto config = createTestChaconneConfig();
  auto scheme = ChaconneScheme::createStandardDMinor();

  auto result = analyzeChaconne({}, config, scheme);
  EXPECT_FLOAT_EQ(result.harmonic_scheme_integrity, 0.0f);
  EXPECT_FLOAT_EQ(result.voice_switch_frequency, 0.0f);
}

TEST(ChaconneAnalyzerTest, EmptyConfigReturnsZeroScores) {
  ChaconneConfig empty_config;
  auto scheme = ChaconneScheme::createStandardDMinor();
  Track track;

  auto result = analyzeChaconne({track}, empty_config, scheme);
  EXPECT_FLOAT_EQ(result.role_order_score, 0.0f);
  EXPECT_FLOAT_EQ(result.climax_presence_score, 0.0f);
  EXPECT_FLOAT_EQ(result.variation_diversity, 0.0f);
}

TEST(ChaconneAnalyzerTest, MultipleTracksAreMerged) {
  auto config = createTestChaconneConfig();
  auto scheme = ChaconneScheme::createStandardDMinor();

  // Split the ideal track into two tracks.
  auto full_track = createIdealChaconneTrack(config, scheme);

  Track track_a;
  track_a.channel = 0;
  Track track_b;
  track_b.channel = 0;

  for (size_t idx = 0; idx < full_track.notes.size(); ++idx) {
    if (idx % 2 == 0) {
      track_a.notes.push_back(full_track.notes[idx]);
    } else {
      track_b.notes.push_back(full_track.notes[idx]);
    }
  }

  auto result = analyzeChaconne({track_a, track_b}, config, scheme);
  // The merged tracks should still contain all notes.
  // Role order and climax presence are config-derived, so they should pass.
  EXPECT_FLOAT_EQ(result.role_order_score, 1.0f);
  EXPECT_FLOAT_EQ(result.climax_presence_score, 1.0f);
}

TEST(ChaconneAnalyzerTest, AccumulateCountDiagnosticIsCorrect) {
  auto config = createTestChaconneConfig();
  auto scheme = ChaconneScheme::createStandardDMinor();
  auto track = createIdealChaconneTrack(config, scheme);

  auto result = analyzeChaconne({track}, config, scheme);
  EXPECT_EQ(result.accumulate_count, 3);
}

TEST(ChaconneAnalyzerTest, TextureTransitionWithSingleVariation) {
  ChaconneConfig config;
  config.key = {Key::D, true};
  // Single variation: Resolve with Theme.
  config.variations.push_back(
      {0, VariationRole::Resolve, VariationType::Theme,
       TextureType::SingleLine, {Key::D, true}, false});

  auto scheme = ChaconneScheme::createStandardDMinor();
  auto result = analyzeChaconne({}, config, scheme);
  // Single variation has no transitions; should return 1.0 (no transitions to fail).
  EXPECT_FLOAT_EQ(result.texture_transition_score, 1.0f);
}

}  // namespace
}  // namespace bach
