#include "composer/json_export.h"

#include <gtest/gtest.h>

#include <string>

#include "composer/composer.h"
#include "composer/harmonic_plan.h"
#include "composer/material.h"
#include "composer/span.h"
#include "composer/voice_intent.h"
#include "composer/voice_plan.h"

namespace bach::composer {

namespace {

Material trivialMaterial() {
  Material m;
  for (int i = 0; i < 4; ++i) {
    MaterialNote n;
    n.start_tick = static_cast<Tick>(i) * kTicksPerBeat;
    n.duration = kTicksPerBeat;
    n.pitch = static_cast<std::uint8_t>(72 + (i * 2));
    m.subject.push_back(n);
  }
  return m;
}

HarmonicPlan trivialHarmony() {
  HarmonicPlan p;
  p.tonic_pc = 0;
  ChordEvent c;
  c.start_tick = 0;
  c.root_pc = 0;
  c.quality = ChordQuality::Major;
  p.chords.push_back(c);
  return p;
}

VoicePlan trivialVoicePlan() {
  VoicePlan vp;
  vp.num_voices = 2;
  Span s0;
  s0.id = 0;
  s0.start_tick = 0;
  s0.end_tick = kTicksPerBar;
  s0.voice = 0;
  s0.intent = VoiceIntent::SubjectCarrier;
  vp.spans.push_back(s0);
  Span s1;
  s1.id = 1;
  s1.start_tick = 0;
  s1.end_tick = kTicksPerBar;
  s1.voice = 1;
  s1.intent = VoiceIntent::SequentialCounterline;
  vp.spans.push_back(s1);
  return vp;
}

bool contains(const std::string& haystack, const std::string& needle) {
  return haystack.find(needle) != std::string::npos;
}

}  // namespace

class JsonExportTest : public ::testing::Test {
 protected:
  void SetUp() override {
    result_ = Composer{}.run(trivialMaterial(), trivialHarmony(), trivialVoicePlan());
    generated_ = emitGeneratedJson(result_.notes);
    provenance_ = emitProvenanceJson(result_.provenance);
  }

  ComposeResult result_;
  std::string generated_;
  std::string provenance_;
};

TEST_F(JsonExportTest, GeneratedJsonHasMinimalShape) {
  EXPECT_TRUE(contains(generated_, "\"schema_version\":\"generated.v1\""));
  EXPECT_TRUE(contains(generated_, "\"ticks_per_beat\":480"));
  EXPECT_TRUE(contains(generated_, "\"notes\":"));
}

TEST_F(JsonExportTest, GeneratedJsonOmitsInternalFields) {
  // generated.json must not carry composer-internal metadata.
  EXPECT_FALSE(contains(generated_, "voice_intent"));
  EXPECT_FALSE(contains(generated_, "candidate_score"));
  EXPECT_FALSE(contains(generated_, "satisfied_rules"));
  EXPECT_FALSE(contains(generated_, "span_id"));
  EXPECT_FALSE(contains(generated_, "source"));
  EXPECT_FALSE(contains(generated_, "rejected_alternatives"));
  EXPECT_FALSE(contains(generated_, "Material"));
  EXPECT_FALSE(contains(generated_, "Compose"));
  EXPECT_FALSE(contains(generated_, "SubjectCarrier"));
}

TEST_F(JsonExportTest, ProvenanceJsonCarriesAuditFields) {
  EXPECT_TRUE(contains(provenance_, "\"schema_version\":\"provenance.v1\""));
  EXPECT_TRUE(contains(provenance_, "voice_intent"));
  EXPECT_TRUE(contains(provenance_, "candidate_score"));
  EXPECT_TRUE(contains(provenance_, "satisfied_rules"));
  EXPECT_TRUE(contains(provenance_, "source"));
  EXPECT_TRUE(contains(provenance_, "span_id"));
}

TEST_F(JsonExportTest, ParallelIndicesAlignBetweenFiles) {
  // generated.json.notes[i].index == provenance.json.notes[i].index for
  // all i. Implemented by counting "index":N occurrences in lockstep.
  // Both files emit one "index" key per note; the integers must increase
  // monotonically from 0.
  std::size_t expected_count = result_.notes.size();
  ASSERT_EQ(expected_count, result_.provenance.size());

  std::size_t gen_count = 0;
  for (std::size_t i = 0; i < expected_count; ++i) {
    const std::string needle = "\"index\":" + std::to_string(i);
    if (contains(generated_, needle))
      ++gen_count;
    EXPECT_TRUE(contains(provenance_, needle)) << "provenance.json missing index=" << i;
  }
  EXPECT_EQ(gen_count, expected_count);
}

TEST_F(JsonExportTest, BothFilesShareNoteCount) {
  std::size_t expected = result_.notes.size();
  std::size_t gen_indices = 0;
  std::size_t prov_indices = 0;
  for (std::size_t i = 0; i < expected; ++i) {
    const std::string needle = "\"index\":" + std::to_string(i);
    if (contains(generated_, needle))
      ++gen_indices;
    if (contains(provenance_, needle))
      ++prov_indices;
  }
  EXPECT_EQ(gen_indices, prov_indices);
  EXPECT_EQ(gen_indices, expected);
}

}  // namespace bach::composer
