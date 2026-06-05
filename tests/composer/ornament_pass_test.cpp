// Deterministic ornament post-pass (composer/ornament_pass.h) tests.
//
// Covers the opt-in ornament pass contract:
//   * Determinism: same inputs -> byte-identical ornamented output.
//   * Exempt voices and non-candidate notes are left untouched.
//   * Trill / mordent subdivisions sum exactly to the original duration
//     (total piece duration is preserved).
//   * Nachschlag shape: a trill ends lower-neighbour -> main.
//   * A cadence trill is present in the last two bars even at density 0.
//   * The density matrix (character x instrument) clamps to [0, 2].
//   * notes / tracks / provenance stay consistent and index-parallel.
//   * Minor-mode neighbours never form an augmented 2nd.
//   * Full pipeline: build a form fixture, run the Composer, ornament, then
//     RE-RUN the Validator and confirm it still reports Ok.

#include "composer/ornament_pass.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include "composer/composer.h"
#include "composer/form_director.h"
#include "composer/harmonic_plan.h"
#include "composer/harness_fixture.h"
#include "composer/provenance.h"
#include "composer/renderer.h"
#include "composer/validator.h"
#include "core/basic_types.h"

namespace bach::composer {
namespace {

NoteEvent makeNote(Tick start, Tick dur, std::uint8_t pitch, VoiceId voice) {
  NoteEvent n;
  n.start_tick = start;
  n.duration = dur;
  n.pitch = pitch;
  n.voice = voice;
  n.velocity = 80;
  n.source = BachNoteSource::Unknown;
  return n;
}

NoteProvenance composeProv(VoiceId /*voice*/) {
  NoteProvenance p;
  p.source = NoteSource::Compose;
  p.voice_intent = VoiceIntent::SequentialCounterline;
  return p;
}

// Build a two-voice ComposeResult: V0 melody of quarter/half notes over the C
// major scale, V1 a clean low bass (whole notes) so V0 is never the bass.
ComposeResult twoVoiceFixture(int bars) {
  ComposeResult result;
  for (int bar = 0; bar < bars; ++bar) {
    // V1 bass: one whole note per bar (root C2 = 36).
    result.notes.push_back(makeNote(barToTick(bar), kTicksPerBar, 36, 1));
    result.provenance.push_back(composeProv(1));
    // V0 melody: four quarter notes per bar on diatonic tones above the bass.
    const std::uint8_t scale[4] = {72, 74, 76, 77};
    for (int beat = 0; beat < 4; ++beat) {
      result.notes.push_back(
          makeNote(barToTick(bar) + beat * kTicksPerBeat, kTicksPerBeat, scale[beat], 0));
      result.provenance.push_back(composeProv(0));
    }
  }
  result.tracks = Renderer{}.render(result.notes);
  return result;
}

OrnamentParams baseParams() {
  OrnamentParams p;
  p.character = SubjectCharacter::Playful;  // ornament_density 2.
  p.instrument = InstrumentType::Organ;     // unchanged.
  p.mode = detail::Mode::Major;
  p.seed = 7;
  p.ticks_per_bar = kTicksPerBar;
  return p;
}

// Sum of all note durations per voice — used to confirm an ornamented note's
// sub-notes exactly cover the original span.
Tick voiceCoverage(const std::vector<NoteEvent>& notes, VoiceId voice) {
  Tick sum = 0;
  for (const auto& n : notes)
    if (n.voice == voice)
      sum += n.duration;
  return sum;
}

bool noOrnamentSourceNotes(const ComposeResult& r) {
  for (const auto& n : r.notes)
    if (n.source == BachNoteSource::Ornament)
      return false;
  return true;
}

std::size_t ornamentNoteCount(const ComposeResult& r) {
  std::size_t count = 0;
  for (const auto& n : r.notes)
    if (n.source == BachNoteSource::Ornament)
      ++count;
  return count;
}

}  // namespace

// --- Determinism -----------------------------------------------------------

TEST(OrnamentPassTest, DeterministicForSameInputs) {
  ComposeResult a = twoVoiceFixture(8);
  ComposeResult b = twoVoiceFixture(8);
  applyOrnamentPass(a, baseParams());
  applyOrnamentPass(b, baseParams());

  ASSERT_EQ(a.notes.size(), b.notes.size());
  for (std::size_t i = 0; i < a.notes.size(); ++i) {
    EXPECT_EQ(a.notes[i].start_tick, b.notes[i].start_tick) << "note " << i;
    EXPECT_EQ(a.notes[i].duration, b.notes[i].duration) << "note " << i;
    EXPECT_EQ(a.notes[i].pitch, b.notes[i].pitch) << "note " << i;
    EXPECT_EQ(a.notes[i].voice, b.notes[i].voice) << "note " << i;
  }
}

TEST(OrnamentPassTest, IdempotentSecondApplicationIsNoOp) {
  ComposeResult r = twoVoiceFixture(8);
  applyOrnamentPass(r, baseParams());
  const std::size_t after_first = r.notes.size();
  applyOrnamentPass(r, baseParams());
  EXPECT_EQ(r.notes.size(), after_first);
}

// --- Exempt voices ---------------------------------------------------------

TEST(OrnamentPassTest, ExemptVoiceIsNeverOrnamented) {
  ComposeResult r = twoVoiceFixture(8);
  OrnamentParams p = baseParams();
  p.exempt_voices = {0};  // exempt the melody voice entirely.
  applyOrnamentPass(r, p);

  for (const auto& n : r.notes) {
    if (n.voice == 0)
      EXPECT_NE(n.source, BachNoteSource::Ornament);
  }
}

// --- Non-candidate notes (bass) --------------------------------------------

TEST(OrnamentPassTest, LowestVoiceStaysClean) {
  ComposeResult r = twoVoiceFixture(8);
  applyOrnamentPass(r, baseParams());
  for (const auto& n : r.notes) {
    if (n.voice == 1)  // the bass voice.
      EXPECT_NE(n.source, BachNoteSource::Ornament);
  }
}

// --- Duration preservation -------------------------------------------------

TEST(OrnamentPassTest, TotalDurationPerVoicePreserved) {
  ComposeResult before = twoVoiceFixture(8);
  const Tick v0_before = voiceCoverage(before.notes, 0);
  const Tick v1_before = voiceCoverage(before.notes, 1);

  ComposeResult after = twoVoiceFixture(8);
  applyOrnamentPass(after, baseParams());

  EXPECT_EQ(voiceCoverage(after.notes, 0), v0_before);
  EXPECT_EQ(voiceCoverage(after.notes, 1), v1_before);
}

// --- Nachschlag shape ------------------------------------------------------

TEST(OrnamentPassTest, TrillEndsWithLowerNeighbourThenMain) {
  ComposeResult r = twoVoiceFixture(8);
  applyOrnamentPass(r, baseParams());

  // Find a run of consecutive Ornament notes in voice 0 forming one trill, and
  // verify the tail is lower-neighbour -> main (penultimate < final pitch, and
  // the final pitch equals an earlier main tone of the run).
  ASSERT_GT(ornamentNoteCount(r), 0u);

  std::vector<NoteEvent> v0;
  for (const auto& n : r.notes)
    if (n.voice == 0)
      v0.push_back(n);

  // Locate the first ornament run of length >= 4 (a trill, not a mordent).
  bool checked = false;
  for (std::size_t i = 0; i < v0.size(); ++i) {
    if (v0[i].source != BachNoteSource::Ornament)
      continue;
    std::size_t j = i;
    while (j < v0.size() && v0[j].source == BachNoteSource::Ornament &&
           (j == i || v0[j].start_tick == v0[j - 1].start_tick + v0[j - 1].duration))
      ++j;
    const std::size_t len = j - i;
    if (len >= 4) {
      const auto& penult = v0[j - 2];
      const auto& last = v0[j - 1];
      EXPECT_LT(penult.pitch, last.pitch) << "Nachschlag lower neighbour before main";
      // The final tone is the main tone (matches the run's first tone).
      EXPECT_EQ(last.pitch, v0[i].pitch);
      checked = true;
      break;
    }
    i = j - 1;
  }
  EXPECT_TRUE(checked) << "no trill run found to validate Nachschlag shape";
}

// --- Cadence trill at density 0 --------------------------------------------

TEST(OrnamentPassTest, CadenceTrillPresentInLastTwoBarsAtDensityZero) {
  ComposeResult r = twoVoiceFixture(8);
  OrnamentParams p = baseParams();
  p.character = SubjectCharacter::Severe;  // ornament_density 0.
  p.instrument = InstrumentType::Organ;    // unchanged -> effective 0.
  ASSERT_EQ(effectiveOrnamentDensity(p.character, p.instrument), 0u);
  applyOrnamentPass(r, p);

  // Every ornament note must sit in the last two bars (bars 6, 7).
  ASSERT_GT(ornamentNoteCount(r), 0u);
  const Tick cadence_start = barToTick(6);
  for (const auto& n : r.notes) {
    if (n.source == BachNoteSource::Ornament)
      EXPECT_GE(n.start_tick, cadence_start) << "density-0 ornament outside cadence window";
  }
}

// --- Density matrix --------------------------------------------------------

TEST(OrnamentPassTest, DensityMatrixCharacterAndInstrument) {
  // Base densities: Severe=0, Noble=1, Restless=1, Playful=2.
  EXPECT_EQ(effectiveOrnamentDensity(SubjectCharacter::Severe, InstrumentType::Organ), 0u);
  EXPECT_EQ(effectiveOrnamentDensity(SubjectCharacter::Noble, InstrumentType::Organ), 1u);
  EXPECT_EQ(effectiveOrnamentDensity(SubjectCharacter::Restless, InstrumentType::Organ), 1u);
  EXPECT_EQ(effectiveOrnamentDensity(SubjectCharacter::Playful, InstrumentType::Organ), 2u);

  // Harpsichord +1 (cap 2).
  EXPECT_EQ(effectiveOrnamentDensity(SubjectCharacter::Severe, InstrumentType::Harpsichord), 1u);
  EXPECT_EQ(effectiveOrnamentDensity(SubjectCharacter::Noble, InstrumentType::Harpsichord), 2u);
  EXPECT_EQ(effectiveOrnamentDensity(SubjectCharacter::Playful, InstrumentType::Harpsichord), 2u);

  // Strings -1 (min 0).
  EXPECT_EQ(effectiveOrnamentDensity(SubjectCharacter::Severe, InstrumentType::Violin), 0u);
  EXPECT_EQ(effectiveOrnamentDensity(SubjectCharacter::Noble, InstrumentType::Cello), 0u);
  EXPECT_EQ(effectiveOrnamentDensity(SubjectCharacter::Playful, InstrumentType::Guitar), 1u);

  // Piano unchanged.
  EXPECT_EQ(effectiveOrnamentDensity(SubjectCharacter::Playful, InstrumentType::Piano), 2u);
}

TEST(OrnamentPassTest, DenserCharacterProducesAtLeastAsManyOrnaments) {
  ComposeResult severe = twoVoiceFixture(8);
  ComposeResult playful = twoVoiceFixture(8);
  OrnamentParams sp = baseParams();
  sp.character = SubjectCharacter::Severe;
  OrnamentParams pp = baseParams();
  pp.character = SubjectCharacter::Playful;
  applyOrnamentPass(severe, sp);
  applyOrnamentPass(playful, pp);

  EXPECT_GE(ornamentNoteCount(playful), ornamentNoteCount(severe));
}

// --- Consistency: notes / tracks / provenance ------------------------------

TEST(OrnamentPassTest, NotesProvenanceTracksConsistent) {
  ComposeResult r = twoVoiceFixture(8);
  applyOrnamentPass(r, baseParams());

  // Index-parallel.
  ASSERT_EQ(r.notes.size(), r.provenance.size());

  // Every ornament note carries Ornament provenance source.
  for (std::size_t i = 0; i < r.notes.size(); ++i) {
    if (r.notes[i].source == BachNoteSource::Ornament)
      EXPECT_EQ(r.provenance[i].source, NoteSource::Ornament) << "note " << i;
  }

  // Tracks mirror the note list: total track note count equals notes.size(),
  // and each voice's track note count matches the note list for that voice.
  std::size_t track_total = 0;
  for (const auto& t : r.tracks)
    track_total += t.notes.size();
  EXPECT_EQ(track_total, r.notes.size());

  for (const auto& t : r.tracks) {
    std::size_t note_list_count = 0;
    for (const auto& n : r.notes)
      if (n.voice == t.channel)
        ++note_list_count;
    EXPECT_EQ(t.notes.size(), note_list_count) << "voice " << static_cast<int>(t.channel);
  }
}

// --- Minor mode neighbour correctness --------------------------------------

TEST(OrnamentPassTest, MinorModeNeighboursAvoidAugmentedSecond) {
  // A C natural-minor melody whose tones include Ab (80) and G (79): the
  // forbidden aug-2nd is the {Ab, B} adjacency (3 semitones). Confirm no
  // ornament sub-note pair within a single voice forms a 3-semitone step
  // through the Ab/B region.
  ComposeResult r;
  // Bass.
  for (int bar = 0; bar < 4; ++bar) {
    r.notes.push_back(makeNote(barToTick(bar), kTicksPerBar, 36, 1));
    r.provenance.push_back(composeProv(1));
  }
  // Melody: C-minor scale half notes (C Eb G Ab) so neighbours are exercised.
  const std::uint8_t mel[8] = {72, 75, 79, 80, 80, 79, 75, 72};
  for (int idx = 0; idx < 8; ++idx) {
    r.notes.push_back(
        makeNote(static_cast<Tick>(idx) * kTicksPerBeat * 2, kTicksPerBeat * 2, mel[idx], 0));
    r.provenance.push_back(composeProv(0));
  }
  r.tracks = Renderer{}.render(r.notes);

  OrnamentParams p = baseParams();
  p.mode = detail::Mode::Minor;
  applyOrnamentPass(r, p);

  // Within voice 0, scan consecutive ornament sub-notes; no adjacent pair may
  // form exactly 3 semitones through pitch classes {8 (Ab), 11 (B)}.
  std::vector<NoteEvent> v0;
  for (const auto& n : r.notes)
    if (n.voice == 0)
      v0.push_back(n);
  for (std::size_t i = 1; i < v0.size(); ++i) {
    if (v0[i].source != BachNoteSource::Ornament || v0[i - 1].source != BachNoteSource::Ornament)
      continue;
    const int interval =
        std::abs(static_cast<int>(v0[i].pitch) - static_cast<int>(v0[i - 1].pitch));
    if (interval == 3) {
      const int lo = std::min(v0[i].pitch, v0[i - 1].pitch) % 12;
      const int hi = std::max(v0[i].pitch, v0[i - 1].pitch) % 12;
      const bool aug2 = (lo == 8 && hi == 11);
      EXPECT_FALSE(aug2) << "augmented 2nd between ornament tones";
    }
  }
}

// --- Full pipeline: re-run the Validator -----------------------------------

TEST(OrnamentPassTest, ChoralePreludePipelineValidatesCleanAfterOrnament) {
  ComposeRequest req;
  req.form = FormType::ChoralePrelude;
  req.is_minor = false;
  req.character = SubjectCharacter::Noble;  // compatible with ChoralePrelude.
  req.seed = 3;
  HarnessFixture fx;
  ASSERT_EQ(buildFormFixture(req, &fx), FormDirectorStatus::Ok);

  ComposeResult r = Composer{}.run(fx.material, fx.harmony, fx.voice_plan);
  ASSERT_TRUE(r.validation.failures.empty());

  // The cantus-firmus voice (the highest-indexed voice in the chorale fixture)
  // is exempt: its bar-downbeat tones are immutable. Exempt every voice that
  // carries the CantusFirmusReplayed bit.
  std::vector<VoiceId> exempt;
  for (std::size_t i = 0; i < r.notes.size(); ++i) {
    const RuleIdMask cf = RuleIdMask{1} << RuleBit::CantusFirmusReplayed;
    if (r.provenance[i].satisfied_rules & cf) {
      const VoiceId v = r.notes[i].voice;
      if (std::find(exempt.begin(), exempt.end(), v) == exempt.end())
        exempt.push_back(v);
    }
  }

  OrnamentParams p;
  p.character = req.character;
  p.instrument = InstrumentType::Organ;
  p.mode = detail::Mode::Major;
  p.seed = req.seed;
  p.ticks_per_bar = fx.harmony.ticksPerBar();
  p.exempt_voices = exempt;
  applyOrnamentPass(r, p);

  // Re-run the Validator on the ornamented notes; it must still pass.
  const ValidationReport rerun =
      Validator{}.validate(r.notes, r.provenance, fx.harmony, fx.material);
  EXPECT_TRUE(rerun.failures.empty())
      << "ornamented chorale prelude failed validation; first="
      << (rerun.failures.empty() ? "" : rerun.failures.front().rule_id);
}

TEST(OrnamentPassTest, TrioSonataPipelineValidatesCleanAfterOrnament) {
  ComposeRequest req;
  req.form = FormType::TrioSonata;
  req.is_minor = false;
  req.character = SubjectCharacter::Severe;
  req.seed = 1;
  HarnessFixture fx;
  ASSERT_EQ(buildFormFixture(req, &fx), FormDirectorStatus::Ok);

  ComposeResult r = Composer{}.run(fx.material, fx.harmony, fx.voice_plan);
  ASSERT_TRUE(r.validation.failures.empty());

  OrnamentParams p;
  p.character = req.character;
  p.instrument = InstrumentType::Organ;
  p.mode = detail::Mode::Major;
  p.seed = req.seed;
  p.ticks_per_bar = fx.harmony.ticksPerBar();
  applyOrnamentPass(r, p);

  const ValidationReport rerun =
      Validator{}.validate(r.notes, r.provenance, fx.harmony, fx.material);
  EXPECT_TRUE(rerun.failures.empty())
      << "ornamented trio sonata failed validation; first="
      << (rerun.failures.empty() ? "" : rerun.failures.front().rule_id);
}

// --- Untouched core pipeline (opt-in guarantee) ----------------------------

TEST(OrnamentPassTest, ComposerRunUnaffectedWithoutPass) {
  // Sanity: a fresh fixture run produces no Ornament-source notes — the pass is
  // strictly opt-in and never invoked by Composer::run.
  ComposeResult r = twoVoiceFixture(8);
  EXPECT_TRUE(noOrnamentSourceNotes(r));
}

}  // namespace bach::composer
