// Tests for the analysis runner -- routing and unified report generation.

#include "analysis/analysis_runner.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "core/basic_types.h"
#include "harmony/chord_types.h"
#include "harmony/harmonic_event.h"
#include "harmony/harmonic_timeline.h"
#include "harmony/key.h"

namespace bach {
namespace {

// ===========================================================================
// Helpers
// ===========================================================================

NoteEvent qn(Tick tick, uint8_t pitch, VoiceId voice) {
  return {tick, kTicksPerBeat, pitch, 80, voice};
}

HarmonicTimeline makeCMajorTimeline(int num_bars) {
  HarmonicTimeline tl;
  for (int bar = 0; bar < num_bars; ++bar) {
    HarmonicEvent ev;
    ev.tick = static_cast<Tick>(bar) * kTicksPerBar;
    ev.end_tick = ev.tick + kTicksPerBar;
    ev.key = Key::C;
    ev.is_minor = false;
    ev.chord.degree = (bar % 2 == 0) ? ChordDegree::I : ChordDegree::V;
    ev.chord.quality = ChordQuality::Major;
    ev.chord.root_pitch = (bar % 2 == 0) ? 60 : 67;
    tl.addEvent(ev);
  }
  return tl;
}

std::vector<Track> makeCleanOrganTracks() {
  // Use consonant intervals: C5-E4 (m6), C5-C4 (P8), E4-C4 (M3).
  Track t0;
  t0.name = "Soprano";
  t0.notes = {qn(0, 72, 0), qn(kTicksPerBeat, 72, 0)};  // C5

  Track t1;
  t1.name = "Alto";
  t1.notes = {qn(0, 64, 1), qn(kTicksPerBeat, 64, 1)};  // E4

  Track t2;
  t2.name = "Bass";
  t2.notes = {qn(0, 60, 2), qn(kTicksPerBeat, 60, 2)};  // C4

  return {t0, t1, t2};
}

std::vector<Track> makeCleanSoloStringTracks() {
  Track t0;
  t0.name = "Cello";
  // All C major chord tones.
  t0.notes = {
      qn(0, 48, 0),               // C3
      qn(kTicksPerBeat, 52, 0),   // E3
      qn(kTicksPerBeat * 2, 55, 0),  // G3
      qn(kTicksPerBeat * 3, 48, 0),  // C3
  };
  return {t0};
}

// ===========================================================================
// System routing
// ===========================================================================

TEST(AnalysisRunner, OrganFormRouting) {
  EXPECT_EQ(analysisSystemForForm(FormType::Fugue), AnalysisSystem::Organ);
  EXPECT_EQ(analysisSystemForForm(FormType::PreludeAndFugue), AnalysisSystem::Organ);
  EXPECT_EQ(analysisSystemForForm(FormType::TrioSonata), AnalysisSystem::Organ);
  EXPECT_EQ(analysisSystemForForm(FormType::ChoralePrelude), AnalysisSystem::Organ);
  EXPECT_EQ(analysisSystemForForm(FormType::ToccataAndFugue), AnalysisSystem::Organ);
  EXPECT_EQ(analysisSystemForForm(FormType::Passacaglia), AnalysisSystem::Organ);
  EXPECT_EQ(analysisSystemForForm(FormType::FantasiaAndFugue), AnalysisSystem::Organ);
}

TEST(AnalysisRunner, SoloStringFormRouting) {
  EXPECT_EQ(analysisSystemForForm(FormType::CelloPrelude), AnalysisSystem::SoloString);
  EXPECT_EQ(analysisSystemForForm(FormType::Chaconne), AnalysisSystem::SoloString);
}

// ===========================================================================
// Organ analysis
// ===========================================================================

TEST(AnalysisRunner, OrganClean_HasCounterpoint) {
  auto tracks = makeCleanOrganTracks();
  auto tl = makeCMajorTimeline(1);
  KeySignature ks = {Key::C, false};

  auto report = runAnalysis(tracks, FormType::Fugue, 3, tl, ks);
  EXPECT_TRUE(report.has_counterpoint);
}

TEST(AnalysisRunner, OrganClean_OverallPass) {
  auto tracks = makeCleanOrganTracks();
  auto tl = makeCMajorTimeline(1);
  KeySignature ks = {Key::C, false};

  auto report = runAnalysis(tracks, FormType::Fugue, 3, tl, ks);
  EXPECT_TRUE(report.overall_pass);
}

TEST(AnalysisRunner, OrganDissonant_OverallFail) {
  Track t0;
  t0.notes = {qn(0, 60, 0)};  // C4
  Track t1;
  t1.notes = {qn(0, 61, 1)};  // C#4 -- clash, non-chord, non-diatonic
  auto tl = makeCMajorTimeline(1);
  KeySignature ks = {Key::C, false};

  auto report = runAnalysis({t0, t1}, FormType::Fugue, 2, tl, ks);
  EXPECT_FALSE(report.overall_pass);  // High severity events present.
}

// ===========================================================================
// Solo String analysis
// ===========================================================================

TEST(AnalysisRunner, SoloStringClean_NoCounterpoint) {
  auto tracks = makeCleanSoloStringTracks();
  auto tl = makeCMajorTimeline(1);
  KeySignature ks = {Key::C, false};

  auto report = runAnalysis(tracks, FormType::CelloPrelude, 1, tl, ks);
  EXPECT_FALSE(report.has_counterpoint);
}

TEST(AnalysisRunner, SoloStringClean_OverallPass) {
  auto tracks = makeCleanSoloStringTracks();
  auto tl = makeCMajorTimeline(1);
  KeySignature ks = {Key::C, false};

  auto report = runAnalysis(tracks, FormType::CelloPrelude, 1, tl, ks);
  EXPECT_TRUE(report.overall_pass);
}

// ===========================================================================
// Text output
// ===========================================================================

TEST(AnalysisRunner, TextSummary_ContainsOrganSection) {
  auto tracks = makeCleanOrganTracks();
  auto tl = makeCMajorTimeline(1);
  KeySignature ks = {Key::C, false};

  auto report = runAnalysis(tracks, FormType::Fugue, 3, tl, ks);
  std::string text = report.toTextSummary(FormType::Fugue, 3);
  EXPECT_NE(text.find("Organ"), std::string::npos);
  EXPECT_NE(text.find("Counterpoint"), std::string::npos);
}

TEST(AnalysisRunner, TextSummary_SoloStringNoCounterpoint) {
  auto tracks = makeCleanSoloStringTracks();
  auto tl = makeCMajorTimeline(1);
  KeySignature ks = {Key::C, false};

  auto report = runAnalysis(tracks, FormType::CelloPrelude, 1, tl, ks);
  std::string text = report.toTextSummary(FormType::CelloPrelude, 1);
  EXPECT_NE(text.find("Solo String"), std::string::npos);
  EXPECT_EQ(text.find("Counterpoint"), std::string::npos);
}

// ===========================================================================
// JSON output
// ===========================================================================

TEST(AnalysisRunner, JsonOutput_ValidStructure) {
  auto tracks = makeCleanOrganTracks();
  auto tl = makeCMajorTimeline(1);
  KeySignature ks = {Key::C, false};

  auto report = runAnalysis(tracks, FormType::Fugue, 3, tl, ks);
  std::string json = report.toJson(FormType::Fugue, 3);
  EXPECT_NE(json.find("\"system\""), std::string::npos);
  EXPECT_NE(json.find("\"Organ\""), std::string::npos);
  EXPECT_NE(json.find("\"overall_pass\""), std::string::npos);
  EXPECT_NE(json.find("\"dissonance\""), std::string::npos);
  EXPECT_NE(json.find("\"counterpoint\""), std::string::npos);
}

TEST(AnalysisRunner, JsonOutput_SoloStringNoCounterpoint) {
  auto tracks = makeCleanSoloStringTracks();
  auto tl = makeCMajorTimeline(1);
  KeySignature ks = {Key::C, false};

  auto report = runAnalysis(tracks, FormType::Chaconne, 1, tl, ks);
  std::string json = report.toJson(FormType::Chaconne, 1);
  EXPECT_NE(json.find("\"SoloString\""), std::string::npos);
  // Should not have counterpoint section.
  EXPECT_EQ(json.find("\"counterpoint\""), std::string::npos);
}

}  // namespace
}  // namespace bach
