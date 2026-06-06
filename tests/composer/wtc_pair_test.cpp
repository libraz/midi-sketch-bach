// WTC Prelude+Fugue pair tests.
//
// This fixture is a reuse-only assembly: it introduces no new VoiceIntent /
// RuleBit / validator rule / Material type. It pairs the two organ/keyboard
// idioms covered separately -- the free-figuration prelude and the scalar fugue
// exposition -- into one continuous 24-bar, 3-voice C-major movement:
//
//   PRELUDE (bars 0-7): three FigurationCarrier spans. V0 carries two sections
//   (bars 0-3 and bars 4-7), V1 carries one bass-support section (bars 0-7); all
//   are the per-beat chord-tone-anchored scalar wave. Every prelude note stamps
//   FigurationCommitted (52); the SECOND V0 section (bars 4-7) is is_pedal_prep,
//   so ONLY its notes additionally stamp PedalPreparation (54) -- the
//   prelude->fugue link.
//   FUGUE (bars 8-23): a compact exposition. V0 SubjectCarrier (bars 8-11),
//   V1 AnswerCarrier (bars 12-15, real answer = subject - P4), V2 SubjectCarrier
//   re-entry (bars 16-19, subject - P8), V0 SubjectCarrier stretto leader (bars
//   20-23, subject verbatim). The fugue carriers have NO identity RuleBit (they
//   carry only ChordTone), so the fugue half is asserted STRUCTURALLY: by span
//   intent + bar windows, not by a bit.
//
// These tests cover the integration end:
//   1. buildHarnessFixture(Phase24, seed) -> Composer::run validates Ok for every
//      seed family (seed % 4 selects the prelude scalar offset; seed/4 % 5 the
//      subject slot).
//   2. The two reused prelude RuleBits (FigurationCommitted=52, PedalPreparation
//      =54) appear; bit 54 appears ONLY on the pedal-prep (second V0) section.
//   3. The fugue spans are present with the correct intents (SubjectCarrier bars
//      8-11 / 20-23, AnswerCarrier 12-15, SubjectCarrier re-entry 16-19), placed
//      in the right bar windows.

#include <gtest/gtest.h>

#include <cstdint>

#include "composer/composer.h"
#include "composer/harness_fixture.h"
#include "composer/provenance.h"
#include "composer/voice_intent.h"
#include "core/basic_types.h"

namespace bach::composer {
namespace {

bool hasRule(const ValidationReport& r, const std::string& rule_id) {
  for (const auto& f : r.failures) {
    if (f.rule_id == rule_id)
      return true;
  }
  return false;
}

constexpr RuleIdMask bit(RuleBit b) {
  return ruleBitMask(b);
}

constexpr Tick kBar = static_cast<Tick>(1920);

}  // namespace

// --- Phase24 fixture integration -------------------------------------------

// The WTC-pair fixture must run through the full Composer cleanly for every
// seed family: no validator failure, three voices, both reused prelude
// RuleBits stamped, and the fugue carriers present. PedalPreparation (54) must
// fire ONLY on the pedal-prep section (bars 4-7), never on the plain figuration
// or the fugue.
TEST(WtcPairTest, Phase24FixtureValidatesCleanAndStampsReusedBits) {
  for (int seed : {0, 1, 2, 3}) {
    const HarnessFixture fx = buildHarnessFixture(HarnessPhase::Phase24, seed);
    const ComposeResult r = Composer{}.run(fx.material, fx.harmony, fx.voice_plan);

    EXPECT_TRUE(r.validation.failures.empty())
        << "seed " << seed << " produced " << r.validation.failures.size() << " failure(s); first="
        << (r.validation.failures.empty() ? "" : r.validation.failures.front().rule_id);

    ASSERT_FALSE(r.notes.empty());
    ASSERT_EQ(r.notes.size(), r.provenance.size());
    EXPECT_EQ(fx.voice_plan.num_voices, 3);

    int figuration_notes = 0;
    int subject_notes = 0;
    int answer_notes = 0;
    bool saw_figuration_bit = false;
    bool saw_pedal_prep_bit = false;
    bool all_figuration_stamped = true;
    // bit 54 must appear ONLY inside bars 4-7 (the pedal-prep section).
    bool pedal_prep_outside_window = false;

    for (std::size_t i = 0; i < r.notes.size(); ++i) {
      const RuleIdMask rules = r.provenance[i].satisfied_rules;
      const Tick start = r.notes[i].start_tick;
      const bool has_pedal_prep = (rules & bit(RuleBit::PedalPreparation)) != 0;
      if (has_pedal_prep) {
        saw_pedal_prep_bit = true;
        if (start < 4 * kBar || start >= 8 * kBar)
          pedal_prep_outside_window = true;
      }
      switch (r.provenance[i].voice_intent) {
        case VoiceIntent::FigurationCarrier:
          ++figuration_notes;
          if (rules & bit(RuleBit::FigurationCommitted))
            saw_figuration_bit = true;
          else
            all_figuration_stamped = false;
          break;
        case VoiceIntent::SubjectCarrier:
          ++subject_notes;
          break;
        case VoiceIntent::AnswerCarrier:
          ++answer_notes;
          break;
        default:
          break;
      }
    }

    // Prelude: V0 two sections (64 + 64) + V1 bass (64) = 192 FigurationCarrier
    // notes. Fugue: V0 subject (bars 8-11 + 20-23 = 32) + V2 re-entry (16) = 48
    // SubjectCarrier notes; V1 answer = 16 AnswerCarrier notes.
    EXPECT_EQ(figuration_notes, 192) << "seed " << seed << " prelude figuration notes";
    EXPECT_EQ(subject_notes, 48) << "seed " << seed << " subject + re-entry notes";
    EXPECT_EQ(answer_notes, 16) << "seed " << seed << " answer notes";

    // Both reused prelude bits must fire; every FigurationCarrier note must
    // carry FigurationCommitted; PedalPreparation must
    // never leak outside the bars-4-7 pedal-prep section.
    EXPECT_TRUE(saw_figuration_bit) << "seed " << seed << " missing FigurationCommitted";
    EXPECT_TRUE(saw_pedal_prep_bit) << "seed " << seed << " missing PedalPreparation";
    EXPECT_TRUE(all_figuration_stamped)
        << "seed " << seed << " has a FigurationCarrier note without FigurationCommitted";
    EXPECT_FALSE(pedal_prep_outside_window)
        << "seed " << seed << " stamped PedalPreparation outside the bars-4-7 section";
    // The fugue carriers must carry NO identity bit: neither bit 52 nor 54.
    for (std::size_t i = 0; i < r.notes.size(); ++i) {
      const VoiceIntent vi = r.provenance[i].voice_intent;
      if (vi == VoiceIntent::SubjectCarrier || vi == VoiceIntent::AnswerCarrier) {
        const RuleIdMask rules = r.provenance[i].satisfied_rules;
        EXPECT_EQ(rules & bit(RuleBit::FigurationCommitted), RuleIdMask{0})
            << "seed " << seed << " fugue note carries FigurationCommitted";
        EXPECT_EQ(rules & bit(RuleBit::PedalPreparation), RuleIdMask{0})
            << "seed " << seed << " fugue note carries PedalPreparation";
      }
    }
  }
}

// The fixture declares exactly three prelude FigurationCarrier spans (V0 x2 + V1)
// and four fugue spans with the correct intents in the correct bar windows. The
// fugue half is asserted structurally here (no identity bit).
TEST(WtcPairTest, Phase24FixtureHasPreludeAndFugueSpans) {
  const HarnessFixture fx = buildHarnessFixture(HarnessPhase::Phase24, 0);

  // Three prelude figuration sections: V0 bars 0-3, V0 bars 4-7, V1 bars 0-7.
  EXPECT_EQ(fx.material.figuration_sections.size(), 3u)
      << "two V0 prelude sections + one V1 bass support";

  int figuration_spans = 0;
  int subject_spans = 0;
  int answer_spans = 0;
  // Track the structural fugue windows by (voice, intent, start_bar).
  bool v0_subject_8 = false;
  bool v0_subject_20 = false;
  bool v1_answer_12 = false;
  bool v2_subject_16 = false;

  for (const auto& span : fx.voice_plan.spans) {
    const int start_bar = static_cast<int>(span.start_tick / kBar);
    switch (span.intent) {
      case VoiceIntent::FigurationCarrier:
        ++figuration_spans;
        break;
      case VoiceIntent::SubjectCarrier:
        ++subject_spans;
        if (span.voice == 0 && start_bar == 8)
          v0_subject_8 = true;
        if (span.voice == 0 && start_bar == 20)
          v0_subject_20 = true;
        if (span.voice == 2 && start_bar == 16)
          v2_subject_16 = true;
        break;
      case VoiceIntent::AnswerCarrier:
        ++answer_spans;
        if (span.voice == 1 && start_bar == 12)
          v1_answer_12 = true;
        break;
      default:
        break;
    }
  }

  EXPECT_EQ(figuration_spans, 3) << "three prelude figuration spans";
  EXPECT_EQ(subject_spans, 3) << "V0 subject (bars 8-11), V2 re-entry, V0 leader (bars 20-23)";
  EXPECT_EQ(answer_spans, 1) << "single V1 answer span";

  // Structural fugue placement (no identity bit asserts these windows).
  EXPECT_TRUE(v0_subject_8) << "missing V0 subject at bar 8";
  EXPECT_TRUE(v1_answer_12) << "missing V1 answer at bar 12";
  EXPECT_TRUE(v2_subject_16) << "missing V2 re-entry at bar 16";
  EXPECT_TRUE(v0_subject_20) << "missing V0 stretto leader at bar 20";
}

// The fugue material reuses the proven scalar subject content: the answer is the
// subject transposed down a perfect fourth, and the re-entry down an octave.
TEST(WtcPairTest, Phase24FugueUsesScalarSubjectTranspositions) {
  const HarnessFixture fx = buildHarnessFixture(HarnessPhase::Phase24, 0);

  // Subject material holds the V0 subject (16) + V2 re-entry (16) + V0 leader (16)
  // = 48 notes; the answer material holds 16.
  EXPECT_EQ(fx.material.subject.size(), 48u);
  EXPECT_EQ(fx.material.answer.size(), 16u);

  // The first 16 subject notes (bars 8-11) and the answer (bars 12-15) share the
  // -P4 relationship note-for-note.
  for (int n = 0; n < 16; ++n) {
    const int subject_pitch = fx.material.subject[n].pitch;
    const int answer_pitch = fx.material.answer[n].pitch;
    EXPECT_EQ(answer_pitch, subject_pitch - 5) << "answer note " << n << " is not subject - P4";
  }
}

}  // namespace bach::composer
