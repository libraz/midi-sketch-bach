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
    generated_ = emitGeneratedJson(result_.notes, result_.validation);
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

TEST_F(JsonExportTest, GeneratedJsonCarriesInfoMetricsWhenAvailable) {
  EXPECT_TRUE(contains(generated_, "\"info\":"));
  EXPECT_TRUE(contains(generated_, "\"subject_features\":"));
  EXPECT_TRUE(contains(generated_, "\"length\":4"));
  EXPECT_TRUE(contains(generated_, "\"range_semitones\":6"));
}

TEST(JsonExportInfoTest, GeneratedJsonCarriesStreamCellDivergenceMetrics) {
  ValidationReport report;
  StreamSegregationSpan span;
  span.detected_stream_count = 1;
  span.cell_based_stream_count = 2;
  span.cell_count = 4;
  span.disagrees_with_cell_counterpoint = true;
  span.transition_note_indices = {2, 5};
  report.stream_segregation.push_back(span);

  const std::string json = emitGeneratedJson({}, report);
  EXPECT_TRUE(contains(json, "\"stream_segregation\":"));
  EXPECT_TRUE(contains(json, "\"cell_based_stream_count\":2"));
  EXPECT_TRUE(contains(json, "\"cell_count\":4"));
  EXPECT_TRUE(contains(json, "\"disagrees_with_cell_counterpoint\":true"));
  EXPECT_TRUE(contains(json, "\"transition_note_indices\":[2,5]"));
}

TEST_F(JsonExportTest, GeneratedJsonOmitsInternalFields) {
  // generated.json must not carry composer-internal metadata.
  EXPECT_FALSE(contains(generated_, "voice_intent"));
  EXPECT_FALSE(contains(generated_, "candidate_score"));
  EXPECT_FALSE(contains(generated_, "shadow_score"));
  EXPECT_FALSE(contains(generated_, "shadow_winning_pitch"));
  EXPECT_FALSE(contains(generated_, "shadow_winning_pitch_without_markov"));
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
  EXPECT_TRUE(contains(provenance_, "shadow_score"));
  EXPECT_TRUE(contains(provenance_, "shadow_winning_pitch"));
  EXPECT_TRUE(contains(provenance_, "shadow_winning_pitch_without_markov"));
  EXPECT_TRUE(contains(provenance_, "satisfied_rules"));
  EXPECT_TRUE(contains(provenance_, "source"));
  EXPECT_TRUE(contains(provenance_, "span_id"));
}

TEST(JsonExportHighBitTest, SatisfiedRulesAboveBit31SurviveSerialization) {
  // Regression: satisfied_rules is a 64-bit RuleIdMask. P9/P10 use rule
  // bits >= 32 (ImitationEntryMatched=32, InvertibleAt8va=33). A 32-bit
  // cast on export would truncate those bits AND, because bit 31 is the
  // sign bit, make downstream bitset masking sign-extend. The exporter
  // must emit the full unsigned 64-bit integer verbatim.
  std::vector<NoteProvenance> prov(1);
  prov[0].span_id = 0;
  prov[0].voice_intent = VoiceIntent::SubjectCarrier;
  prov[0].source = NoteSource::Compose;
  // Set bit 33 (InvertibleAt8va) plus bit 0 (ChordTone). The decimal
  // value is (1<<33)|1 = 8589934593, which a 32-bit cast would collapse
  // to 1 (losing bit 33) — so we assert the full value is present.
  prov[0].satisfied_rules = (1ull << 33) | 1ull;
  const std::string json = emitProvenanceJson(prov);
  EXPECT_NE(json.find("\"satisfied_rules\":8589934593"), std::string::npos)
      << "high bit (>=32) truncated in export: " << json;
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

namespace {

// Build a tiny hand-rolled ComposeResult: 2 voices, 4 notes total, with
// provenance index-parallel to the flat notes vector and tracks partitioning
// the same notes by voice in order.
ComposeResult buildHomepageFixture() {
  ComposeResult res;

  auto make_note = [](VoiceId voice, Tick start, std::uint8_t pitch, std::uint8_t velocity) {
    NoteEvent n;
    n.voice = voice;
    n.start_tick = start;
    n.duration = kTicksPerBeat;
    n.pitch = pitch;
    n.velocity = velocity;
    return n;
  };
  auto make_prov = [](NoteSource source) {
    NoteProvenance p;
    p.source = source;
    return p;
  };

  // Flat notes + parallel provenance (voice 0 then voice 1).
  res.notes.push_back(make_note(0, 0, 60, 90));
  res.provenance.push_back(make_prov(NoteSource::Material));
  res.notes.push_back(make_note(0, kTicksPerBeat, 64, 91));
  res.provenance.push_back(make_prov(NoteSource::Compose));
  res.notes.push_back(make_note(1, 0, 48, 70));
  res.provenance.push_back(make_prov(NoteSource::Compose));
  res.notes.push_back(make_note(1, kTicksPerBeat, 50, 71));
  res.provenance.push_back(make_prov(NoteSource::Ornament));

  Track t0;
  t0.name = "Voice 1";
  t0.channel = 0;
  t0.program = 6;
  t0.notes.push_back(res.notes[0]);
  t0.notes.push_back(res.notes[1]);
  res.tracks.push_back(t0);

  Track t1;
  t1.name = "Voice 2";
  t1.channel = 1;
  t1.program = 42;
  t1.notes.push_back(res.notes[2]);
  t1.notes.push_back(res.notes[3]);
  res.tracks.push_back(t1);

  return res;
}

HomepageMeta buildHomepageMeta() {
  HomepageMeta meta;
  meta.form_name = "fugue";
  meta.key_name = "G minor";
  meta.bpm = 96;
  meta.seed = 12345;
  meta.total_ticks = 2 * kTicksPerBeat;
  meta.total_bars = 1;
  meta.description = "Fugue in G minor";
  return meta;
}

}  // namespace

TEST(HomepageEventsJsonTest, EmitsContractFieldsAndValues) {
  const ComposeResult res = buildHomepageFixture();
  const HomepageMeta meta = buildHomepageMeta();
  const std::string json = buildHomepageEventsJson(res, meta);

  // Top-level metadata.
  EXPECT_TRUE(contains(json, "\"form\":\"fugue\""));
  EXPECT_TRUE(contains(json, "\"key\":\"G minor\""));
  EXPECT_TRUE(contains(json, "\"bpm\":96"));
  EXPECT_TRUE(contains(json, "\"seed\":12345"));
  EXPECT_TRUE(contains(json, "\"total_ticks\":960"));
  EXPECT_TRUE(contains(json, "\"total_bars\":1"));
  EXPECT_TRUE(contains(json, "\"description\":\"Fugue in G minor\""));

  // Tracks block.
  EXPECT_TRUE(contains(json, "\"tracks\":"));
  EXPECT_TRUE(contains(json, "\"name\":\"Voice 1\""));
  EXPECT_TRUE(contains(json, "\"name\":\"Voice 2\""));
  EXPECT_TRUE(contains(json, "\"channel\":1"));
  EXPECT_TRUE(contains(json, "\"program\":6"));
  EXPECT_TRUE(contains(json, "\"program\":42"));
  EXPECT_TRUE(contains(json, "\"note_count\":2"));

  // Per-note fields.
  EXPECT_TRUE(contains(json, "\"pitch\":60"));
  EXPECT_TRUE(contains(json, "\"velocity\":90"));
  EXPECT_TRUE(contains(json, "\"start_tick\":0"));
  EXPECT_TRUE(contains(json, "\"duration\":480"));
  EXPECT_TRUE(contains(json, "\"voice\":1"));
  EXPECT_TRUE(contains(json, "\"source\":\"material\""));
  EXPECT_TRUE(contains(json, "\"source\":\"compose\""));
}

TEST(HomepageEventsJsonTest, OrnamentSourceMapsToLowercaseString) {
  const ComposeResult res = buildHomepageFixture();
  const HomepageMeta meta = buildHomepageMeta();
  const std::string json = buildHomepageEventsJson(res, meta);

  // The voice-1 second note carries NoteSource::Ornament.
  EXPECT_TRUE(contains(json, "\"source\":\"ornament\""));
}

TEST(HomepageEventsJsonTest, DeterministicAcrossCalls) {
  const ComposeResult res = buildHomepageFixture();
  const HomepageMeta meta = buildHomepageMeta();
  const std::string first = buildHomepageEventsJson(res, meta);
  const std::string second = buildHomepageEventsJson(res, meta);
  EXPECT_EQ(first, second);
}

}  // namespace bach::composer
