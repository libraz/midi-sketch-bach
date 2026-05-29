#include <gtest/gtest.h>

#include <algorithm>

#include "composer/composer.h"
#include "composer/harmonic_plan.h"
#include "composer/material.h"
#include "composer/provenance.h"
#include "composer/span.h"
#include "composer/validation.h"
#include "composer/voice_intent.h"
#include "composer/voice_plan.h"
#include "core/basic_types.h"

namespace bach::composer {

namespace {

// Helper: build a fixed subject covering 4 bars in voice 0 (soprano).
// One quarter note per beat. Pitches stay within the major scale of C so
// the HarmonicPlan triads admit the strong-beat pitches.
Material makeFixedSubject() {
  Material m;
  static constexpr std::uint8_t kPitches[] = {
      // bar 0 over C major (I): root, second, third, fourth
      72,
      74,
      76,
      77,
      // bar 1 over F major (IV): fifth, sixth, fifth, fourth
      79,
      81,
      79,
      77,
      // bar 2 over G major (V): third, second, third, fourth
      76,
      74,
      76,
      77,
      // bar 3 over C major (I): fifth, fourth, third, root
      79,
      77,
      76,
      72,
  };
  for (std::size_t i = 0; i < sizeof(kPitches) / sizeof(kPitches[0]); ++i) {
    MaterialNote n;
    n.start_tick = static_cast<Tick>(i) * kTicksPerBeat;
    n.duration = kTicksPerBeat;
    n.pitch = kPitches[i];
    m.subject.push_back(n);
  }
  return m;
}

// Helper: build a 4-bar I-IV-V-I progression in C major.
HarmonicPlan makeProgression() {
  HarmonicPlan p;
  p.tonic_pc = 0;
  p.is_minor = false;
  for (std::uint8_t bar = 0; bar < 4; ++bar) {
    ChordEvent c;
    c.start_tick = static_cast<Tick>(bar) * kTicksPerBar;
    c.quality = ChordQuality::Major;
    switch (bar) {
      case 0:
        c.root_pc = 0;
        break;  // C
      case 1:
        c.root_pc = 5;
        break;  // F
      case 2:
        c.root_pc = 7;
        break;  // G
      case 3:
        c.root_pc = 0;
        break;  // C
    }
    p.chords.push_back(c);
  }
  return p;
}

// Helper: 2 voices, 5 spans total.
//   voice 0: 1 span over bars 0-3 (SubjectCarrier)
//   voice 1: 4 spans, one per bar (SequentialCounterline)
VoicePlan makeTwoVoicePlan() {
  VoicePlan vp;
  vp.num_voices = 2;
  SpanId next_id = 0;
  Span s0;
  s0.id = next_id++;
  s0.start_tick = 0;
  s0.end_tick = 4 * kTicksPerBar;
  s0.voice = 0;
  s0.intent = VoiceIntent::SubjectCarrier;
  vp.spans.push_back(s0);
  for (std::uint8_t bar = 0; bar < 4; ++bar) {
    Span s;
    s.id = next_id++;
    s.start_tick = static_cast<Tick>(bar) * kTicksPerBar;
    s.end_tick = s.start_tick + kTicksPerBar;
    s.voice = 1;
    s.intent = VoiceIntent::SequentialCounterline;
    vp.spans.push_back(s);
  }
  return vp;
}

}  // namespace

class ComposerTwoVoiceTest : public ::testing::Test {
 protected:
  void SetUp() override {
    material_ = makeFixedSubject();
    harmonic_ = makeProgression();
    voice_plan_ = makeTwoVoicePlan();
    result_ = Composer{}.run(material_, harmonic_, voice_plan_);
  }

  Material material_;
  HarmonicPlan harmonic_;
  VoicePlan voice_plan_;
  ComposeResult result_;
};

TEST_F(ComposerTwoVoiceTest, ProducesNonEmptyOutput) {
  EXPECT_FALSE(result_.notes.empty());
  EXPECT_EQ(result_.notes.size(), result_.provenance.size());
  EXPECT_FALSE(result_.tracks.empty());
}

TEST_F(ComposerTwoVoiceTest, ValidatorReportsOk) {
  EXPECT_EQ(result_.validation.status, ValidationStatus::Ok)
      << "validator found " << result_.validation.failures.size() << " failures; first rule_id="
      << (result_.validation.failures.empty()
              ? "<none>"
              : result_.validation.failures.front().rule_id.c_str());
  EXPECT_TRUE(result_.validation.failures.empty());
}

TEST_F(ComposerTwoVoiceTest, EveryNoteHasProvenance) {
  for (std::size_t i = 0; i < result_.notes.size(); ++i) {
    const auto& prov = result_.provenance[i];
    EXPECT_NE(prov.span_id, kInvalidSpanId) << "note index " << i << " has no span_id";
  }
}

TEST_F(ComposerTwoVoiceTest, SubjectNotesPreservePitchExactly) {
  // Every voice-0 note must have the same pitch as the corresponding
  // material entry. Renderer is forbidden from modifying pitch.
  std::vector<std::uint8_t> voice0_pitches;
  for (std::size_t i = 0; i < result_.notes.size(); ++i) {
    if (result_.notes[i].voice == 0) {
      voice0_pitches.push_back(result_.notes[i].pitch);
      EXPECT_EQ(result_.provenance[i].source, NoteSource::Material);
      EXPECT_EQ(result_.provenance[i].voice_intent, VoiceIntent::SubjectCarrier);
    }
  }
  ASSERT_EQ(voice0_pitches.size(), material_.subject.size());
  for (std::size_t i = 0; i < voice0_pitches.size(); ++i) {
    EXPECT_EQ(voice0_pitches[i], material_.subject[i].pitch);
  }
}

TEST_F(ComposerTwoVoiceTest, CounterlineNotesAreComposeSourced) {
  std::size_t compose_count = 0;
  for (std::size_t i = 0; i < result_.notes.size(); ++i) {
    if (result_.notes[i].voice == 1) {
      EXPECT_EQ(result_.provenance[i].source, NoteSource::Compose);
      EXPECT_EQ(result_.provenance[i].voice_intent, VoiceIntent::SequentialCounterline);
      ++compose_count;
    }
  }
  EXPECT_GT(compose_count, 0u) << "voice 1 should have at least one Compose-sourced note";
}

TEST_F(ComposerTwoVoiceTest, RendererPreservesNoteCountAndOrder) {
  // tracks group by voice; total note count must match the flat list.
  std::size_t track_note_total = 0;
  for (const auto& t : result_.tracks)
    track_note_total += t.notes.size();
  EXPECT_EQ(track_note_total, result_.notes.size());

  // tracks ordered by ascending voice id.
  std::vector<VoiceId> track_voices;
  for (const auto& t : result_.tracks)
    track_voices.push_back(t.channel);
  auto sorted_voices = track_voices;
  std::sort(sorted_voices.begin(), sorted_voices.end());
  EXPECT_EQ(track_voices, sorted_voices);
}

TEST_F(ComposerTwoVoiceTest, NoNoteOriginatesFromRepair) {
  // The composer pipeline has no repair stage. Every note's source must
  // be Material or Compose; no other value exists in NoteSource. This is
  // a regression guard if NoteSource is widened.
  for (const auto& prov : result_.provenance) {
    EXPECT_TRUE(prov.source == NoteSource::Material || prov.source == NoteSource::Compose);
  }
}

}  // namespace bach::composer
