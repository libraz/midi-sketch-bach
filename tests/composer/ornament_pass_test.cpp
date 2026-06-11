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

// --- Trill shape: upper-note start + Nachschlag ------------------------------

namespace {

// Collect one voice's notes in list order (the pass keeps onset order per voice).
std::vector<NoteEvent> voiceNotes(const ComposeResult& r, VoiceId voice) {
  std::vector<NoteEvent> out;
  for (const auto& n : r.notes)
    if (n.voice == voice)
      out.push_back(n);
  return out;
}

// Locate the first contiguous ornament run of length >= min_len in `notes`.
// Returns {begin, end} indices, or {0, 0} when none exists.
std::pair<std::size_t, std::size_t> firstOrnamentRun(const std::vector<NoteEvent>& notes,
                                                     std::size_t min_len) {
  for (std::size_t i = 0; i < notes.size(); ++i) {
    if (notes[i].source != BachNoteSource::Ornament)
      continue;
    std::size_t j = i;
    while (j < notes.size() && notes[j].source == BachNoteSource::Ornament &&
           (j == i || notes[j].start_tick == notes[j - 1].start_tick + notes[j - 1].duration))
      ++j;
    if (j - i >= min_len)
      return {i, j};
    i = j - 1;
  }
  return {0, 0};
}

}  // namespace

TEST(OrnamentPassTest, TrillStartsOnUpperAndEndsWithLowerNeighbourThenMain) {
  ComposeResult r = twoVoiceFixture(8);
  applyOrnamentPass(r, baseParams());

  ASSERT_GT(ornamentNoteCount(r), 0u);
  const std::vector<NoteEvent> v0 = voiceNotes(r, 0);
  const auto [i, j] = firstOrnamentRun(v0, 4);
  ASSERT_LT(i, j) << "no trill run found to validate the trill shape";

  const auto& first = v0[i];
  const auto& penult = v0[j - 2];
  const auto& last = v0[j - 1];
  // The final tone is the main tone; the run opens on its upper auxiliary
  // (a diatonic step above the main tone).
  EXPECT_GT(first.pitch, last.pitch) << "trill must start on the upper auxiliary";
  EXPECT_LE(first.pitch - last.pitch, 2) << "upper auxiliary must be a diatonic step";
  EXPECT_LT(penult.pitch, last.pitch) << "Nachschlag lower neighbour before main";
}

// --- Final-note protection ---------------------------------------------------

// The last attack of every voice is the resolution tone: it must stay one
// plain note at every density, even when it sits on the most tempting trill
// site (a strong-beat long note inside the cadence window).
TEST(OrnamentPassTest, FinalNotePerVoiceNeverOrnamented) {
  for (auto character :
       {SubjectCharacter::Severe, SubjectCharacter::Noble, SubjectCharacter::Playful}) {
    ComposeResult r;
    for (int bar = 0; bar < 8; ++bar) {
      r.notes.push_back(makeNote(barToTick(bar), kTicksPerBar, 36, 1));
      r.provenance.push_back(composeProv(1));
    }
    const std::uint8_t scale[4] = {72, 74, 76, 77};
    for (int bar = 0; bar < 7; ++bar) {
      for (int beat = 0; beat < 4; ++beat) {
        r.notes.push_back(
            makeNote(barToTick(bar) + beat * kTicksPerBeat, kTicksPerBeat, scale[beat], 0));
        r.provenance.push_back(composeProv(0));
      }
    }
    // Final attack: a whole-note tonic on the last bar's downbeat.
    r.notes.push_back(makeNote(barToTick(7), kTicksPerBar, 72, 0));
    r.provenance.push_back(composeProv(0));
    r.tracks = Renderer{}.render(r.notes);

    OrnamentParams p = baseParams();
    p.character = character;
    applyOrnamentPass(r, p);

    std::size_t final_attacks = 0;
    for (const auto& n : r.notes) {
      if (n.voice != 0 || n.start_tick < barToTick(7))
        continue;
      ++final_attacks;
      EXPECT_NE(n.source, BachNoteSource::Ornament) << "final note was ornamented";
      EXPECT_EQ(n.duration, kTicksPerBar) << "final note was subdivided";
    }
    EXPECT_EQ(final_attacks, 1u);
  }
}

// --- Long-trill openings: appuy / von-unten ----------------------------------

// Build an 8-bar fixture whose penultimate bar opens with a half-note trill
// site (strong beat inside the cadence window, not the voice's final attack).
ComposeResult longCadenceNoteFixture() {
  ComposeResult r;
  for (int bar = 0; bar < 8; ++bar) {
    r.notes.push_back(makeNote(barToTick(bar), kTicksPerBar, 36, 1));
    r.provenance.push_back(composeProv(1));
  }
  const std::uint8_t scale[4] = {72, 74, 76, 77};
  for (int bar = 0; bar < 6; ++bar) {
    for (int beat = 0; beat < 4; ++beat) {
      r.notes.push_back(
          makeNote(barToTick(bar) + beat * kTicksPerBeat, kTicksPerBeat, scale[beat], 0));
      r.provenance.push_back(composeProv(0));
    }
  }
  // Bar 6 (cadence window): half-note trill site, then two quarters.
  r.notes.push_back(makeNote(barToTick(6), kTicksPerBeat * 2, 74, 0));
  r.provenance.push_back(composeProv(0));
  r.notes.push_back(makeNote(barToTick(6) + kTicksPerBeat * 2, kTicksPerBeat, 74, 0));
  r.provenance.push_back(composeProv(0));
  r.notes.push_back(makeNote(barToTick(6) + kTicksPerBeat * 3, kTicksPerBeat, 71, 0));
  r.provenance.push_back(composeProv(0));
  // Bar 7: whole-note tonic (the protected final attack).
  r.notes.push_back(makeNote(barToTick(7), kTicksPerBar, 72, 0));
  r.provenance.push_back(composeProv(0));
  r.tracks = Renderer{}.render(r.notes);
  return r;
}

// A long cadence trill opens either with the held upper appoggiatura (appuy)
// or with the von-unten doppelt-cadence prefix; across a seed family BOTH
// openings must appear (the placement hash mixes them deterministically).
TEST(OrnamentPassTest, LongCadenceTrillMixesAppuyAndVonUntenOpenings) {
  bool saw_appuy = false;
  bool saw_von_unten = false;
  for (std::uint32_t seed = 1; seed <= 16; ++seed) {
    ComposeResult r = longCadenceNoteFixture();
    OrnamentParams p = baseParams();
    p.seed = seed;
    applyOrnamentPass(r, p);

    // Collect the ornament run replacing the bar-6 half note (pitch 74).
    std::vector<NoteEvent> run;
    for (const auto& n : r.notes) {
      if (n.voice == 0 && n.source == BachNoteSource::Ornament && n.start_tick >= barToTick(6) &&
          n.start_tick < barToTick(6) + kTicksPerBeat * 2)
        run.push_back(n);
    }
    ASSERT_GE(run.size(), 4u) << "mandatory long cadence trill missing, seed " << seed;
    const auto& first = run.front();
    if (first.pitch > 74) {
      // Appuy: a HELD upper neighbour (longer than the alternation pacing).
      EXPECT_GE(first.duration, duration::kEighthNote) << "appuy opening not held, seed " << seed;
      saw_appuy = true;
    } else {
      // Von-unten: lower -> main two-note prefix at alternation pacing.
      ASSERT_GE(run.size(), 6u);
      EXPECT_LT(first.pitch, 74) << "seed " << seed;
      EXPECT_EQ(run[1].pitch, 74) << "von-unten prefix must step lower -> main, seed " << seed;
      saw_von_unten = true;
    }
    // Both openings share the Nachschlag tail: lower neighbour then main.
    EXPECT_LT(run[run.size() - 2].pitch, 74);
    EXPECT_EQ(run.back().pitch, 74);
  }
  EXPECT_TRUE(saw_appuy) << "appuy opening never selected across the seed family";
  EXPECT_TRUE(saw_von_unten) << "von-unten opening never selected across the seed family";
}

// A long note outside the cadence window always opens with the appuy: a single
// held upper-neighbour note of max(span/4, eighth) capped at a half note,
// followed by the alternation. Playful's interior inner trill carries the
// shape; the 2-bar note sits on an even non-boundary bar and the seed family
// guarantees at least one open gate.
TEST(OrnamentPassTest, LongNoteOpensWithHeldUpperAppoggiatura) {
  bool found = false;
  for (std::uint32_t seed = 1; seed <= 8 && !found; ++seed) {
    ComposeResult r;
    const int bars = 12;
    for (int bar = 0; bar < bars; ++bar) {
      r.notes.push_back(makeNote(barToTick(bar), kTicksPerBar, 36, 1));
      r.provenance.push_back(composeProv(1));
    }
    for (int bar = 0; bar < bars; ++bar) {
      if (bar == 4) {
        // Two-bar held note (bars 4-5) on an even, non-boundary downbeat.
        r.notes.push_back(makeNote(barToTick(4), kTicksPerBar * 2, 76, 0));
        r.provenance.push_back(composeProv(0));
        continue;
      }
      if (bar == 5)
        continue;  // covered by the held note.
      r.notes.push_back(makeNote(barToTick(bar), kTicksPerBeat * 2, 72, 0));
      r.provenance.push_back(composeProv(0));
      r.notes.push_back(makeNote(barToTick(bar) + kTicksPerBeat * 2, kTicksPerBeat * 2, 74, 0));
      r.provenance.push_back(composeProv(0));
    }
    r.tracks = Renderer{}.render(r.notes);

    OrnamentParams p = baseParams();  // Playful: density 2, interior inner trills.
    p.seed = seed;
    applyOrnamentPass(r, p);

    std::vector<NoteEvent> run;
    for (const auto& n : r.notes) {
      if (n.voice == 0 && n.source == BachNoteSource::Ornament && n.start_tick >= barToTick(4) &&
          n.start_tick < barToTick(6))
        run.push_back(n);
    }
    if (run.size() < 4)
      continue;  // the generic gate was closed for this seed; try the next.
    found = true;

    // Appuy head: held upper neighbour, span/4 capped at a half note.
    const auto& head = run.front();
    EXPECT_EQ(head.pitch, 77) << "appuy must hold the upper neighbour";
    EXPECT_EQ(head.duration, duration::kHalfNote) << "appuy length = max(span/4, eighth) cap half";
    // The alternation resumes on the main tone and ends lower -> main.
    EXPECT_EQ(run[1].pitch, 76);
    EXPECT_LT(run[run.size() - 2].pitch, 76);
    EXPECT_EQ(run.back().pitch, 76);
  }
  EXPECT_TRUE(found) << "no inner-trill appuy opening fired across the seed family";
}

// --- Trill pacing follows tempo ----------------------------------------------

TEST(OrnamentPassTest, TrillPacingFollowsBpm) {
  for (const std::uint16_t bpm :
       {static_cast<std::uint16_t>(72), static_cast<std::uint16_t>(120)}) {
    ComposeResult r = twoVoiceFixture(8);
    OrnamentParams p = baseParams();
    p.bpm = bpm;
    applyOrnamentPass(r, p);

    const std::vector<NoteEvent> v0 = voiceNotes(r, 0);
    const auto [i, j] = firstOrnamentRun(v0, 4);
    ASSERT_LT(i, j) << "no trill run found at bpm " << bpm;

    const Tick expected_sub = bpm <= 100 ? duration::kThirtySecondNote : duration::kSixteenthNote;
    // Every alternation tone except the remainder-absorbing final one runs at
    // the tempo-selected pacing.
    for (std::size_t k = i; k + 1 < j; ++k)
      EXPECT_EQ(v0[k].duration, expected_sub) << "bpm " << bpm << " slot " << (k - i);
  }
}

// --- New vocabulary: appoggiatura / turn / slide ------------------------------

namespace {

// 16-bar two-voice fixture whose phrase-boundary bars (3, 7, 11) open with a
// falling third (E -> C on the boundary downbeat): the appoggiatura's primary
// gap-fill site. Other bars walk quarters stepwise.
ComposeResult fallingThirdBoundaryFixture(Tick boundary_dur) {
  ComposeResult r;
  const int bars = 16;
  for (int bar = 0; bar < bars; ++bar) {
    r.notes.push_back(makeNote(barToTick(bar), kTicksPerBar, 36, 1));
    r.provenance.push_back(composeProv(1));
  }
  for (int bar = 0; bar < bars; ++bar) {
    if (bar % 4 == 3 && bar != bars - 1) {
      // Approach tone E ends the previous beat; boundary downbeat C takes the
      // configured duration, the rest of the bar holds another tone.
      r.notes.push_back(makeNote(barToTick(bar), boundary_dur, 72, 0));
      r.provenance.push_back(composeProv(0));
      if (boundary_dur < kTicksPerBar) {
        r.notes.push_back(
            makeNote(barToTick(bar) + boundary_dur, kTicksPerBar - boundary_dur, 74, 0));
        r.provenance.push_back(composeProv(0));
      }
      continue;
    }
    // Each plain bar ends on E (76), so a following boundary C is always
    // entered by a falling third.
    const std::uint8_t walk[4] = {74, 76, 77, 76};
    for (int beat = 0; beat < 4; ++beat) {
      r.notes.push_back(
          makeNote(barToTick(bar) + beat * kTicksPerBeat, kTicksPerBeat, walk[beat], 0));
      r.provenance.push_back(composeProv(0));
    }
  }
  r.tracks = Renderer{}.render(r.notes);
  return r;
}

}  // namespace

// The appoggiatura's primary site: a phrase-boundary strong-beat tone entered
// by a falling third. The lean takes the upper neighbour for HALF the value,
// then resolves to the main tone (two sub-notes, exact span coverage).
TEST(OrnamentPassTest, AppoggiaturaFillsFallingThirdGapAtPhraseBoundary) {
  bool found = false;
  for (std::uint32_t seed = 1; seed <= 16 && !found; ++seed) {
    ComposeResult r = fallingThirdBoundaryFixture(kTicksPerBeat);
    OrnamentParams p = baseParams();
    p.character = SubjectCharacter::Noble;  // density 1.
    p.seed = seed;
    applyOrnamentPass(r, p);

    for (int bar : {3, 7, 11}) {
      std::vector<const NoteEvent*> group;
      for (const auto& n : r.notes) {
        if (n.voice == 0 && n.source == BachNoteSource::Ornament &&
            n.start_tick >= barToTick(bar) && n.start_tick < barToTick(bar) + kTicksPerBeat)
          group.push_back(&n);
      }
      if (group.size() == 2) {
        // Lean = upper neighbour (D over the C main tone), half the value.
        EXPECT_EQ(group[0]->pitch, 74);
        EXPECT_EQ(group[0]->duration, kTicksPerBeat / 2);
        EXPECT_EQ(group[1]->pitch, 72);
        EXPECT_EQ(group[1]->duration, kTicksPerBeat / 2);
        found = true;
      }
    }
  }
  EXPECT_TRUE(found) << "no falling-third appoggiatura fired across the seed family";
}

// The Explication's dotted rule: on a dotted value the lean takes two thirds.
TEST(OrnamentPassTest, DottedAppoggiaturaLeansTwoThirds) {
  constexpr Tick kDottedHalf = kTicksPerBeat * 3;
  bool found = false;
  for (std::uint32_t seed = 1; seed <= 16 && !found; ++seed) {
    ComposeResult r = fallingThirdBoundaryFixture(kDottedHalf);
    OrnamentParams p = baseParams();
    p.character = SubjectCharacter::Noble;
    p.seed = seed;
    applyOrnamentPass(r, p);

    for (int bar : {3, 7, 11}) {
      std::vector<const NoteEvent*> group;
      for (const auto& n : r.notes) {
        if (n.voice == 0 && n.source == BachNoteSource::Ornament &&
            n.start_tick >= barToTick(bar) && n.start_tick < barToTick(bar) + kDottedHalf)
          group.push_back(&n);
      }
      if (group.size() == 2 && group[0]->pitch == 74) {
        EXPECT_EQ(group[0]->duration, kDottedHalf * 2 / 3) << "dotted lean must take two thirds";
        EXPECT_EQ(group[1]->duration, kDottedHalf / 3);
        EXPECT_EQ(group[1]->pitch, 72);
        found = true;
      }
    }
  }
  EXPECT_TRUE(found) << "no dotted appoggiatura fired across the seed family";
}

// A turn decorates an isolated mid-phrase tone (longer than its predecessor):
// upper - main - lower in 32nds, then the held main tone, covering the span.
TEST(OrnamentPassTest, TurnAnimatesIsolatedMidPhraseLongNote) {
  bool found = false;
  for (std::uint32_t seed = 1; seed <= 16 && !found; ++seed) {
    ComposeResult r;
    const int bars = 16;
    for (int bar = 0; bar < bars; ++bar) {
      r.notes.push_back(makeNote(barToTick(bar), kTicksPerBar, 36, 1));
      r.provenance.push_back(composeProv(1));
    }
    for (int bar = 0; bar < bars; ++bar) {
      // Two eighths then an isolated half note mid-bar (beat 2): the half is
      // longer than its eighth-note predecessor, off the downbeat, and away
      // from phrase boundaries on even bars.
      r.notes.push_back(makeNote(barToTick(bar), kTicksPerBeat / 2, 72, 0));
      r.provenance.push_back(composeProv(0));
      r.notes.push_back(makeNote(barToTick(bar) + kTicksPerBeat / 2, kTicksPerBeat / 2, 74, 0));
      r.provenance.push_back(composeProv(0));
      r.notes.push_back(makeNote(barToTick(bar) + kTicksPerBeat, kTicksPerBeat * 2, 76, 0));
      r.provenance.push_back(composeProv(0));
      r.notes.push_back(makeNote(barToTick(bar) + kTicksPerBeat * 3, kTicksPerBeat, 74, 0));
      r.provenance.push_back(composeProv(0));
    }
    r.tracks = Renderer{}.render(r.notes);

    OrnamentParams p = baseParams();
    p.character = SubjectCharacter::Noble;  // density 1.
    p.seed = seed;
    applyOrnamentPass(r, p);

    for (int bar = 0; bar < 13; ++bar) {
      if (bar % 4 == 3)
        continue;  // phrase boundaries excluded from the turn site.
      std::vector<const NoteEvent*> group;
      for (const auto& n : r.notes) {
        if (n.voice == 0 && n.source == BachNoteSource::Ornament &&
            n.start_tick >= barToTick(bar) + kTicksPerBeat &&
            n.start_tick < barToTick(bar) + kTicksPerBeat * 3)
          group.push_back(&n);
      }
      if (group.size() == 4) {
        EXPECT_EQ(group[0]->pitch, 77) << "turn opens on the upper neighbour";
        EXPECT_EQ(group[1]->pitch, 76);
        EXPECT_EQ(group[2]->pitch, 74) << "turn dips to the lower neighbour";
        EXPECT_EQ(group[3]->pitch, 76);
        EXPECT_EQ(group[0]->duration, duration::kThirtySecondNote);
        Tick covered = 0;
        for (const auto* n : group)
          covered += n->duration;
        EXPECT_EQ(covered, kTicksPerBeat * 2) << "turn must cover the span exactly";
        found = true;
        break;
      }
    }
  }
  EXPECT_TRUE(found) << "no turn fired across the seed family";
}

// A slide fills the gap under a tone entered by a rising leap of a fourth or
// more: two rising 32nds from the diatonic third below, then the held main.
TEST(OrnamentPassTest, SlideFillsRisingLeapArrival) {
  bool found = false;
  for (std::uint32_t seed = 1; seed <= 16 && !found; ++seed) {
    ComposeResult r;
    const int bars = 16;
    for (int bar = 0; bar < bars; ++bar) {
      r.notes.push_back(makeNote(barToTick(bar), kTicksPerBar, 36, 1));
      r.provenance.push_back(composeProv(1));
    }
    for (int bar = 0; bar < bars; ++bar) {
      // Quarter C, then a rising fifth to a half-note G mid-bar, then back.
      r.notes.push_back(makeNote(barToTick(bar), kTicksPerBeat, 72, 0));
      r.provenance.push_back(composeProv(0));
      r.notes.push_back(makeNote(barToTick(bar) + kTicksPerBeat, kTicksPerBeat * 2, 79, 0));
      r.provenance.push_back(composeProv(0));
      r.notes.push_back(makeNote(barToTick(bar) + kTicksPerBeat * 3, kTicksPerBeat, 76, 0));
      r.provenance.push_back(composeProv(0));
    }
    r.tracks = Renderer{}.render(r.notes);

    OrnamentParams p = baseParams();  // Playful: the slide belongs to its vocabulary.
    p.seed = seed;
    applyOrnamentPass(r, p);

    for (int bar = 0; bar < 13; ++bar) {
      std::vector<const NoteEvent*> group;
      for (const auto& n : r.notes) {
        if (n.voice == 0 && n.source == BachNoteSource::Ornament &&
            n.start_tick >= barToTick(bar) + kTicksPerBeat &&
            n.start_tick < barToTick(bar) + kTicksPerBeat * 3)
          group.push_back(&n);
      }
      if (group.size() == 3) {
        EXPECT_EQ(group[0]->pitch, 76) << "slide starts a diatonic third below";
        EXPECT_EQ(group[1]->pitch, 77) << "slide steps up through the second below";
        EXPECT_EQ(group[2]->pitch, 79) << "slide resolves into the arrival tone";
        EXPECT_EQ(group[0]->duration, duration::kThirtySecondNote);
        EXPECT_EQ(group[1]->duration, duration::kThirtySecondNote);
        Tick covered = 0;
        for (const auto* n : group)
          covered += n->duration;
        EXPECT_EQ(covered, kTicksPerBeat * 2) << "slide must cover the span exactly";
        found = true;
        break;
      }
    }
  }
  EXPECT_TRUE(found) << "no slide fired across the seed family";
}

// The character x vocabulary design table, observed at phrase-boundary sites
// across a seed family. Group sizes identify the figure: 2 = appoggiatura,
// 3 = mordent (or slide -- distinguished by motion), 4+ = turn or trill.
//   Severe   : appoggiatura only (and only on its sparse 8-bar sites).
//   Noble    : appoggiatura or turn -- never a mordent.
//   Restless : mordent (quarters) -- never a lean.
//   Playful  : mordent on the unmatched quarter approach.
TEST(OrnamentPassTest, CharacterVocabularyGrammarAtBoundaries) {
  struct Row {
    SubjectCharacter character;
    bool allow2;  // appoggiatura
    bool allow3;  // mordent
    bool allow4;  // turn
  };
  const Row rows[] = {
      {SubjectCharacter::Severe, true, false, false},
      {SubjectCharacter::Noble, true, false, true},
      {SubjectCharacter::Restless, false, true, false},
      {SubjectCharacter::Playful, false, true, false},
  };
  for (const Row& row : rows) {
    for (std::uint32_t seed = 1; seed <= 8; ++seed) {
      ComposeResult r = fallingThirdBoundaryFixture(kTicksPerBeat);
      OrnamentParams p = baseParams();
      p.character = row.character;
      p.seed = seed;
      applyOrnamentPass(r, p);
      for (int bar : {3, 7, 11}) {
        std::size_t group = 0;
        for (const auto& n : r.notes) {
          if (n.voice == 0 && n.source == BachNoteSource::Ornament &&
              n.start_tick >= barToTick(bar) && n.start_tick < barToTick(bar) + kTicksPerBeat)
            ++group;
        }
        if (group == 0)
          continue;  // gate closed (or off this character's sites).
        const bool allowed =
            (group == 2 && row.allow2) || (group == 3 && row.allow3) || (group == 4 && row.allow4);
        EXPECT_TRUE(allowed) << "character " << static_cast<int>(row.character) << " seed " << seed
                             << " bar " << bar << " produced a foreign figure of " << group
                             << " sub-notes";
      }
    }
  }
}

// The new vocabulary is self-gated, never mandatory: across a seed family at
// least one matching site must stay plain (the quantile gate was closed).
TEST(OrnamentPassTest, NewVocabularySitesAreNotMandatory) {
  bool found_plain_site = false;
  for (std::uint32_t seed = 1; seed <= 8 && !found_plain_site; ++seed) {
    ComposeResult r = fallingThirdBoundaryFixture(kTicksPerBeat);
    OrnamentParams p = baseParams();
    p.character = SubjectCharacter::Noble;
    p.seed = seed;
    applyOrnamentPass(r, p);
    for (int bar : {3, 7, 11}) {
      bool ornamented = false;
      for (const auto& n : r.notes) {
        if (n.voice == 0 && n.source == BachNoteSource::Ornament &&
            n.start_tick >= barToTick(bar) && n.start_tick < barToTick(bar) + kTicksPerBeat)
          ornamented = true;
      }
      if (!ornamented)
        found_plain_site = true;
    }
  }
  EXPECT_TRUE(found_plain_site) << "every appoggiatura site fired for every seed";
}

// --- Cadence trill at density 0 --------------------------------------------

TEST(OrnamentPassTest, CadenceTrillPresentInLastTwoBarsAtDensityZero) {
  ComposeResult r = twoVoiceFixture(8);
  OrnamentParams p = baseParams();
  p.character = SubjectCharacter::Severe;  // ornament_density 0.
  p.instrument = InstrumentType::Organ;    // unchanged -> effective 0.
  ASSERT_EQ(effectiveOrnamentDensity(p.character, p.instrument), 0u);
  applyOrnamentPass(r, p);

  // The mandatory cadence trill must land in the last two bars (bars 6, 7),
  // and the only other density-0 site is the designed mid-piece boundary
  // (bar 3 in an 8-bar piece).
  const Tick cadence_start = barToTick(6);
  bool cadence_ornament = false;
  for (const auto& n : r.notes) {
    if (n.source != BachNoteSource::Ornament)
      continue;
    if (n.start_tick >= cadence_start) {
      cadence_ornament = true;
      continue;
    }
    const int bar = static_cast<int>(n.start_tick / kTicksPerBar);
    EXPECT_EQ(bar, 3) << "density-0 ornament outside cadence window and mid boundary";
  }
  EXPECT_TRUE(cadence_ornament) << "mandatory cadence trill missing";
}

TEST(OrnamentPassTest, OrganOrnamentsDoNotExceedManualCompass) {
  ComposeResult r;
  r.notes.push_back(makeNote(0, kTicksPerBar, 36, 1));
  r.provenance.push_back(composeProv(1));
  r.notes.push_back(makeNote(0, kTicksPerBar, 84, 0));
  r.provenance.push_back(composeProv(0));
  r.tracks = Renderer{}.render(r.notes);

  OrnamentParams p = baseParams();
  p.character = SubjectCharacter::Severe;  // mandatory cadence trill if eligible.
  p.instrument = InstrumentType::Organ;
  applyOrnamentPass(r, p);

  for (const auto& note : r.notes) {
    EXPECT_LE(note.pitch, 84) << "organ ornament exceeded manual compass";
  }
}

// --- Mid-piece distribution -------------------------------------------------

// A density-0 character is no longer bare until the final cadence: every other
// phrase boundary (bar % 8 == 7) outside the cadence window is a boundary-
// figure site, and the designed mid-piece boundary (the phrase boundary
// nearest the midpoint -- bar 11 in a 24-bar piece) fires for EVERY seed; all
// mid-piece ornaments must sit on such boundaries. The suspension-leaning
// Severe takes the appoggiatura there (a two-note lean resolving down).
TEST(OrnamentPassTest, SparseCharacterGetsPhraseBoundaryAppoggiaturasMidPiece) {
  for (std::uint32_t seed = 1; seed <= 8; ++seed) {
    ComposeResult r = twoVoiceFixture(24);  // cadence window = bars 22-23.
    OrnamentParams p = baseParams();
    p.character = SubjectCharacter::Severe;  // ornament_density 0.
    p.seed = seed;
    applyOrnamentPass(r, p);
    bool mid_boundary_fired = false;
    std::vector<const NoteEvent*> mid_group;
    for (const auto& n : r.notes) {
      if (n.source != BachNoteSource::Ornament)
        continue;
      const int bar = static_cast<int>(n.start_tick / kTicksPerBar);
      if (bar >= 22)
        continue;  // cadence trill, covered elsewhere.
      EXPECT_TRUE(bar % 8 == 7 || bar == 11)
          << "density-0 mid-piece ornament off the phrase boundary, bar " << bar;
      if (bar == 11) {
        mid_boundary_fired = true;
        mid_group.push_back(&n);
      }
    }
    ASSERT_TRUE(mid_boundary_fired)
        << "seed " << seed << " missed the designed mid-piece boundary appoggiatura";
    // Appoggiatura shape: lean on the upper neighbour, resolve to the main.
    ASSERT_EQ(mid_group.size(), 2u);
    EXPECT_EQ(mid_group[0]->pitch, mid_group[1]->pitch + 2) << "lean must be the upper neighbour";
  }
}

// Sub-cadence phrase-boundary ornaments are gated, never mandatory: across a
// small seed family there must be at least one phrase-boundary bar whose
// strong-beat note stayed plain (the deterministic gate was closed).
TEST(OrnamentPassTest, PhraseBoundaryOrnamentsAreNotMandatory) {
  bool found_plain_boundary = false;
  for (std::uint32_t seed = 1; seed <= 8 && !found_plain_boundary; ++seed) {
    ComposeResult r = twoVoiceFixture(16);  // phrase boundaries: bars 3, 7, 11.
    OrnamentParams p = baseParams();
    p.character = SubjectCharacter::Noble;  // ornament_density 1.
    p.seed = seed;
    applyOrnamentPass(r, p);
    for (int bar : {3, 7, 11}) {
      bool ornamented = false;
      for (const auto& n : r.notes) {
        if (n.source != BachNoteSource::Ornament)
          continue;
        const int nbar = static_cast<int>(n.start_tick / kTicksPerBar);
        if (nbar == bar)
          ornamented = true;
      }
      if (!ornamented)
        found_plain_boundary = true;
    }
  }
  EXPECT_TRUE(found_plain_boundary) << "every phrase boundary was ornamented for every seed";
}

// Noble's interior vocabulary is the turn on half-or-longer tones: an even-bar
// downbeat half note expands to the four-tone gruppetto (upper - main - lower
// - held main) covering the original span exactly.
TEST(OrnamentPassTest, NobleHalfNoteDownbeatTakesTurn) {
  bool found_turn = false;
  for (std::uint32_t seed = 1; seed <= 8 && !found_turn; ++seed) {
    // V0: two half notes per bar; V1 whole-note bass below.
    ComposeResult r;
    const int bars = 12;
    for (int bar = 0; bar < bars; ++bar) {
      r.notes.push_back(makeNote(barToTick(bar), kTicksPerBar, 36, 1));
      r.provenance.push_back(composeProv(1));
      r.notes.push_back(makeNote(barToTick(bar), kTicksPerBeat * 2, 72, 0));
      r.provenance.push_back(composeProv(0));
      r.notes.push_back(makeNote(barToTick(bar) + kTicksPerBeat * 2, kTicksPerBeat * 2, 74, 0));
      r.provenance.push_back(composeProv(0));
    }
    r.tracks = Renderer{}.render(r.notes);

    OrnamentParams p = baseParams();
    p.character = SubjectCharacter::Noble;  // ornament_density 1.
    p.seed = seed;
    applyOrnamentPass(r, p);

    // Look for the 4-note turn on an even-bar downbeat half note outside both
    // the phrase boundaries (bar % 4 == 3) and the cadence window (bars
    // 10-11).
    for (int bar = 0; bar < 10; bar += 2) {
      if (bar % 4 == 3)
        continue;
      std::vector<const NoteEvent*> group;
      for (const auto& n : r.notes) {
        if (n.source == BachNoteSource::Ornament && n.start_tick >= barToTick(bar) &&
            n.start_tick < barToTick(bar) + kTicksPerBeat * 2)
          group.push_back(&n);
      }
      if (group.size() == 4) {
        // upper - main - lower - held main, covering the half note exactly.
        EXPECT_EQ(group[0]->pitch, 74);
        EXPECT_EQ(group[1]->pitch, 72);
        EXPECT_EQ(group[2]->pitch, 71);
        EXPECT_EQ(group[3]->pitch, 72);
        Tick covered = 0;
        for (const auto* n : group)
          covered += n->duration;
        EXPECT_EQ(covered, kTicksPerBeat * 2);
        found_turn = true;
        break;
      }
    }
  }
  EXPECT_TRUE(found_turn) << "no Noble half-note downbeat turn fired";
}

// Density 2 keeps its inner trills on long notes: a half-note downbeat on an
// even bar still expands to a trill (>= 4 sub-notes), not a 3-note mordent.
TEST(OrnamentPassTest, DensityTwoHalfNoteDownbeatKeepsInnerTrill) {
  ComposeResult r;
  const int bars = 12;
  for (int bar = 0; bar < bars; ++bar) {
    r.notes.push_back(makeNote(barToTick(bar), kTicksPerBar, 36, 1));
    r.provenance.push_back(composeProv(1));
    r.notes.push_back(makeNote(barToTick(bar), kTicksPerBeat * 2, 72, 0));
    r.provenance.push_back(composeProv(0));
    r.notes.push_back(makeNote(barToTick(bar) + kTicksPerBeat * 2, kTicksPerBeat * 2, 74, 0));
    r.provenance.push_back(composeProv(0));
  }
  r.tracks = Renderer{}.render(r.notes);

  OrnamentParams p = baseParams();  // Playful: ornament_density 2.
  applyOrnamentPass(r, p);

  for (int bar = 0; bar < 10; bar += 2) {
    if (bar % 4 == 3)
      continue;
    std::size_t group_size = 0;
    for (const auto& n : r.notes) {
      if (n.source == BachNoteSource::Ornament && n.start_tick >= barToTick(bar) &&
          n.start_tick < barToTick(bar) + kTicksPerBeat * 2)
        ++group_size;
    }
    if (group_size > 0)
      EXPECT_GE(group_size, 4u) << "density-2 half-note downbeat lost its trill, bar " << bar;
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

// A minor-cadence trill on the raised leading tone (B natural) must use the
// melodic-minor membrane on BOTH sides: upper auxiliary = C (the tonic, a half
// step up) and Nachschlag lower neighbour = A natural (not the natural-minor
// Bb, which would put a chromatic Bb-B step inside the figure).
TEST(OrnamentPassTest, MinorCadenceLeadingToneTrillUsesRaisedScaleMembrane) {
  ComposeResult r;
  for (int bar = 0; bar < 4; ++bar) {
    r.notes.push_back(makeNote(barToTick(bar), kTicksPerBar, 36, 1));
    r.provenance.push_back(composeProv(1));
  }
  // Bars 0-1: plain C-minor quarters; bar 2 (cadence window): half-note B
  // natural (the raised leading tone) then two quarters; bar 3: the protected
  // final tonic.
  const std::uint8_t scale[4] = {72, 74, 75, 79};
  for (int bar = 0; bar < 2; ++bar) {
    for (int beat = 0; beat < 4; ++beat) {
      r.notes.push_back(
          makeNote(barToTick(bar) + beat * kTicksPerBeat, kTicksPerBeat, scale[beat], 0));
      r.provenance.push_back(composeProv(0));
    }
  }
  r.notes.push_back(makeNote(barToTick(2), kTicksPerBeat * 2, 71, 0));
  r.provenance.push_back(composeProv(0));
  r.notes.push_back(makeNote(barToTick(2) + kTicksPerBeat * 2, kTicksPerBeat, 74, 0));
  r.provenance.push_back(composeProv(0));
  r.notes.push_back(makeNote(barToTick(2) + kTicksPerBeat * 3, kTicksPerBeat, 71, 0));
  r.provenance.push_back(composeProv(0));
  r.notes.push_back(makeNote(barToTick(3), kTicksPerBar, 72, 0));
  r.provenance.push_back(composeProv(0));
  r.tracks = Renderer{}.render(r.notes);

  OrnamentParams p = baseParams();
  p.mode = detail::Mode::Minor;
  applyOrnamentPass(r, p);

  // Collect the mandatory cadence-trill run on the bar-2 half note.
  std::vector<NoteEvent> run;
  for (const auto& n : r.notes) {
    if (n.voice == 0 && n.source == BachNoteSource::Ornament && n.start_tick >= barToTick(2) &&
        n.start_tick < barToTick(2) + kTicksPerBeat * 2)
      run.push_back(n);
  }
  ASSERT_GE(run.size(), 4u) << "leading-tone cadence trill missing";

  bool has_upper_c = false;
  for (const auto& n : run) {
    EXPECT_NE(n.pitch, 70) << "natural-minor Bb leaked into the leading-tone trill";
    if (n.pitch == 72)
      has_upper_c = true;
  }
  EXPECT_TRUE(has_upper_c) << "upper auxiliary C missing from the leading-tone trill";
  EXPECT_EQ(run[run.size() - 2].pitch, 69) << "Nachschlag must be the melodic-minor A natural";
  EXPECT_EQ(run.back().pitch, 71);
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

  // The cantus-firmus voice keeps its bar-downbeat skeleton immutable but may
  // carry within-bar embellishment (Noble = the embellished subtype): wire
  // every voice carrying the CantusFirmusReplayed bit to the skeleton list.
  std::vector<VoiceId> skeleton;
  for (std::size_t i = 0; i < r.notes.size(); ++i) {
    const RuleIdMask cf = ruleBitMask(RuleBit::CantusFirmusReplayed);
    if (r.provenance[i].satisfied_rules & cf) {
      const VoiceId v = r.notes[i].voice;
      if (std::find(skeleton.begin(), skeleton.end(), v) == skeleton.end())
        skeleton.push_back(v);
    }
  }

  OrnamentParams p;
  p.character = req.character;
  p.instrument = InstrumentType::Organ;
  p.mode = detail::Mode::Major;
  p.seed = req.seed;
  p.ticks_per_bar = fx.harmony.ticksPerBar();
  p.skeleton_exempt_voices = skeleton;
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

// --- Per-form exceptions ------------------------------------------------------

namespace {

// 12-bar fixture for the embellished cantus firmus: V0 is the CF (a bar-head
// half note -- the immutable skeleton onset -- plus a within-bar half note),
// V1 a whole-note bass below.
ComposeResult cantusFirmusFixture() {
  ComposeResult r;
  const int bars = 12;
  for (int bar = 0; bar < bars; ++bar) {
    r.notes.push_back(makeNote(barToTick(bar), kTicksPerBar, 36, 1));
    r.provenance.push_back(composeProv(1));
    r.notes.push_back(makeNote(barToTick(bar), kTicksPerBeat * 2, 72, 0));
    r.provenance.push_back(composeProv(0));
    r.notes.push_back(makeNote(barToTick(bar) + kTicksPerBeat * 2, kTicksPerBeat * 2, 74, 0));
    r.provenance.push_back(composeProv(0));
  }
  r.tracks = Renderer{}.render(r.notes);
  return r;
}

}  // namespace

// An embellished cantus firmus keeps every bar-head onset plain (the
// validator's skeleton) while its within-bar long tones may take a turn.
TEST(OrnamentPassTest, EmbellishedCantusFirmusKeepsBarHeadSkeleton) {
  bool found_embellishment = false;
  for (std::uint32_t seed = 1; seed <= 8; ++seed) {
    ComposeResult r = cantusFirmusFixture();
    OrnamentParams p = baseParams();
    p.character = SubjectCharacter::Noble;  // the embellished subtype.
    p.seed = seed;
    p.skeleton_exempt_voices = {0};
    applyOrnamentPass(r, p);

    for (const auto& n : r.notes) {
      if (n.voice != 0)
        continue;
      if (n.start_tick % kTicksPerBar == 0) {
        // Bar-head skeleton: the original attack, never an ornament sub-note.
        EXPECT_NE(n.source, BachNoteSource::Ornament)
            << "bar-head CF onset ornamented at tick " << n.start_tick;
      } else if (n.source == BachNoteSource::Ornament) {
        found_embellishment = true;
      }
    }
  }
  EXPECT_TRUE(found_embellishment) << "no within-bar CF embellishment fired";
}

// Severe keeps the WHOLE cantus firmus plain (the plain-CF chorale subtype).
TEST(OrnamentPassTest, SevereKeepsCantusFirmusPlain) {
  for (std::uint32_t seed = 1; seed <= 8; ++seed) {
    ComposeResult r = cantusFirmusFixture();
    OrnamentParams p = baseParams();
    p.character = SubjectCharacter::Severe;
    p.seed = seed;
    p.skeleton_exempt_voices = {0};
    applyOrnamentPass(r, p);
    for (const auto& n : r.notes) {
      if (n.voice == 0)
        EXPECT_NE(n.source, BachNoteSource::Ornament) << "Severe CF ornamented, seed " << seed;
    }
  }
}

// The Goldberg opening aria is the ornament showcase: with the aria uplift
// active, the aria bars carry more ornament notes than the same material in
// the variation region, summed across a seed family.
TEST(OrnamentPassTest, GoldbergAriaUpliftOutpacesVariations) {
  std::size_t aria_total = 0;
  std::size_t variation_total = 0;
  for (std::uint32_t seed = 1; seed <= 8; ++seed) {
    // V0: two half notes per bar throughout, so bars 0-3 (aria) and bars 4-7
    // (variation region) carry identical material.
    ComposeResult r;
    const int bars = 16;
    for (int bar = 0; bar < bars; ++bar) {
      r.notes.push_back(makeNote(barToTick(bar), kTicksPerBar, 36, 1));
      r.provenance.push_back(composeProv(1));
      r.notes.push_back(makeNote(barToTick(bar), kTicksPerBeat * 2, 72, 0));
      r.provenance.push_back(composeProv(0));
      r.notes.push_back(makeNote(barToTick(bar) + kTicksPerBeat * 2, kTicksPerBeat * 2, 74, 0));
      r.provenance.push_back(composeProv(0));
    }
    r.tracks = Renderer{}.render(r.notes);

    OrnamentParams p = baseParams();
    p.character = SubjectCharacter::Severe;  // sparsest grammar: the uplift dominates.
    p.seed = seed;
    p.aria_end_tick = barToTick(4);
    applyOrnamentPass(r, p);

    for (const auto& n : r.notes) {
      if (n.voice != 0 || n.source != BachNoteSource::Ornament)
        continue;
      if (n.start_tick < barToTick(4))
        ++aria_total;
      else if (n.start_tick < barToTick(8))
        ++variation_total;
    }
  }
  EXPECT_GT(aria_total, variation_total) << "the aria uplift did not outpace the variation region";
}

// A solo pedal entry (no other voice sounding at the onset) may take a
// downbeat mordent; the same bass under ensemble texture stays plain.
TEST(OrnamentPassTest, PedalSoloEntryTakesMordentEnsembleBassStaysPlain) {
  bool solo_mordent_fired = false;
  for (std::uint32_t seed = 1; seed <= 8; ++seed) {
    ComposeResult r;
    const int bars = 10;
    // V1 pedal: a half + two quarters per bar across the whole piece.
    for (int bar = 0; bar < bars; ++bar) {
      r.notes.push_back(makeNote(barToTick(bar), kTicksPerBeat * 2, 36, 1));
      r.provenance.push_back(composeProv(1));
      r.notes.push_back(makeNote(barToTick(bar) + kTicksPerBeat * 2, kTicksPerBeat, 38, 1));
      r.provenance.push_back(composeProv(1));
      r.notes.push_back(makeNote(barToTick(bar) + kTicksPerBeat * 3, kTicksPerBeat, 40, 1));
      r.provenance.push_back(composeProv(1));
    }
    // V0 melody sounds only from bar 4 on: bars 0-3 are the pedal solo.
    const std::uint8_t scale[4] = {72, 74, 76, 77};
    for (int bar = 4; bar < bars; ++bar) {
      for (int beat = 0; beat < 4; ++beat) {
        r.notes.push_back(
            makeNote(barToTick(bar) + beat * kTicksPerBeat, kTicksPerBeat, scale[beat], 0));
        r.provenance.push_back(composeProv(0));
      }
    }
    r.tracks = Renderer{}.render(r.notes);

    OrnamentParams p = baseParams();
    p.seed = seed;
    applyOrnamentPass(r, p);

    for (const auto& n : r.notes) {
      if (n.voice != 1 || n.source != BachNoteSource::Ornament)
        continue;
      // Every pedal ornament must sit inside the solo region (bars 0-3).
      EXPECT_LT(n.start_tick, barToTick(4))
          << "ensemble bass ornamented at tick " << n.start_tick << ", seed " << seed;
      solo_mordent_fired = true;
    }
  }
  EXPECT_TRUE(solo_mordent_fired) << "no pedal-solo mordent fired across the seed family";
}

// The solo cello prelude stays sparse: its only ornament is the cadential
// trill (the unaccompanied-string sources carry almost no ornament signs).
TEST(OrnamentPassTest, CelloPreludeOrnamentsAreCadenceTrillOnly) {
  for (std::uint32_t seed = 1; seed <= 5; ++seed) {
    ComposeRequest req;
    req.form = FormType::CelloPrelude;
    req.is_minor = false;
    req.character = SubjectCharacter::Noble;
    req.seed = seed;
    HarnessFixture fx;
    ASSERT_EQ(buildFormFixture(req, &fx), FormDirectorStatus::Ok);

    ComposeResult r = Composer{}.run(fx.material, fx.harmony, fx.voice_plan);
    ASSERT_TRUE(r.validation.failures.empty());

    OrnamentParams p;
    p.character = req.character;
    p.instrument = InstrumentType::Cello;  // density scales down to 0.
    p.mode = detail::Mode::Major;
    p.seed = req.seed;
    p.ticks_per_bar = fx.harmony.ticksPerBar();
    applyOrnamentPass(r, p);

    const Tick piece_end = [&] {
      Tick end = 0;
      for (const auto& n : r.notes)
        end = std::max(end, n.start_tick + n.duration);
      return end;
    }();
    const Tick cadence_start = piece_end - 2 * fx.harmony.ticksPerBar();

    std::size_t runs = 0;
    bool in_run = false;
    for (const auto& n : r.notes) {
      if (n.source == BachNoteSource::Ornament) {
        EXPECT_GE(n.start_tick, cadence_start)
            << "cello ornament outside the cadence window, seed " << seed;
        if (!in_run)
          ++runs;
        in_run = true;
      } else {
        in_run = false;
      }
    }
    EXPECT_LE(runs, 1u) << "more than the single cadence trill, seed " << seed;
  }
}

// --- Climax uplift window ---------------------------------------------------

TEST(OrnamentPassTest, ClimaxWindowIntensifiesDecorationAcrossSeeds) {
  // Inside the window a density-0 character gains its full boundary
  // vocabulary and the generic gate opens more often, so summed over a seed
  // sweep the windowed runs must place strictly more ornament notes.
  std::size_t plain_total = 0;
  std::size_t windowed_total = 0;
  for (std::uint32_t seed = 1; seed <= 10; ++seed) {
    OrnamentParams params = baseParams();
    params.character = SubjectCharacter::Severe;  // ornament_density 0.
    params.seed = seed;

    ComposeResult plain = twoVoiceFixture(16);
    applyOrnamentPass(plain, params);
    plain_total += ornamentNoteCount(plain);

    params.climax_start_tick = barToTick(8);
    params.climax_end_tick = barToTick(12);
    ComposeResult windowed = twoVoiceFixture(16);
    applyOrnamentPass(windowed, params);
    windowed_total += ornamentNoteCount(windowed);

    // Every extra decoration must live inside the window: notes strictly
    // before the window stay byte-identical between the two runs.
    std::size_t plain_idx = 0;
    std::size_t windowed_idx = 0;
    while (plain_idx < plain.notes.size() && windowed_idx < windowed.notes.size() &&
           plain.notes[plain_idx].start_tick < params.climax_start_tick &&
           windowed.notes[windowed_idx].start_tick < params.climax_start_tick) {
      EXPECT_EQ(plain.notes[plain_idx].start_tick, windowed.notes[windowed_idx].start_tick);
      EXPECT_EQ(plain.notes[plain_idx].pitch, windowed.notes[windowed_idx].pitch);
      EXPECT_EQ(plain.notes[plain_idx].duration, windowed.notes[windowed_idx].duration);
      ++plain_idx;
      ++windowed_idx;
    }
  }
  EXPECT_GT(windowed_total, plain_total)
      << "the climax window should add decoration over a 10-seed sweep";
}

TEST(OrnamentPassTest, ZeroClimaxWindowIsInert) {
  // climax_end_tick == 0 (the default) must reproduce the pre-window output.
  OrnamentParams params = baseParams();
  ComposeResult a = twoVoiceFixture(12);
  applyOrnamentPass(a, params);

  params.climax_start_tick = 0;
  params.climax_end_tick = 0;
  ComposeResult b = twoVoiceFixture(12);
  applyOrnamentPass(b, params);

  ASSERT_EQ(a.notes.size(), b.notes.size());
  for (std::size_t i = 0; i < a.notes.size(); ++i) {
    EXPECT_EQ(a.notes[i].start_tick, b.notes[i].start_tick) << "note " << i;
    EXPECT_EQ(a.notes[i].pitch, b.notes[i].pitch) << "note " << i;
    EXPECT_EQ(a.notes[i].duration, b.notes[i].duration) << "note " << i;
  }
}

// --- Untouched core pipeline (opt-in guarantee) ----------------------------

TEST(OrnamentPassTest, ComposerRunUnaffectedWithoutPass) {
  // Sanity: a fresh fixture run produces no Ornament-source notes — the pass is
  // strictly opt-in and never invoked by Composer::run.
  ComposeResult r = twoVoiceFixture(8);
  EXPECT_TRUE(noOrnamentSourceNotes(r));
}

}  // namespace bach::composer
