#include "composer/harness_fixture.h"

#include <array>
#include <cstdint>

#include "composer/motif_ops.h"
#include "composer/span.h"
#include "composer/tonal_answer.h"
#include "composer/voice_intent.h"
#include "core/basic_types.h"

namespace bach::composer {

namespace {

// 5 subject patterns × 16 quarter-note pitches each. Diatonic to C
// major / A natural-minor. Same catalog the gtest harness uses; the
// canonical copy lives here so the harness test and the CLI dispatch
// path stay byte-identical.
constexpr std::array<std::array<std::uint8_t, 16>, 5> kSubjectPatterns = {{
    // 0: original Phase 3 arch
    {72, 74, 76, 77, 79, 81, 79, 77, 76, 74, 76, 77, 79, 77, 71, 72},
    // 1: descent then ascent (start high)
    {84, 83, 84, 79, 77, 76, 77, 79, 81, 79, 77, 76, 74, 76, 71, 72},
    // 2: broken triad outline
    {79, 76, 79, 84, 76, 79, 76, 72, 74, 77, 74, 71, 72, 76, 71, 72},
    // 3: stepwise sequence
    {71, 72, 76, 77, 74, 76, 77, 79, 76, 77, 79, 81, 77, 79, 71, 72},
    // 4: upper-arch
    {76, 77, 79, 81, 79, 77, 76, 74, 72, 74, 76, 77, 79, 77, 71, 72},
}};

// Phase14-only subject catalog. The milestone fugue permeates every bar with
// the subject (exposition, answer, V2 re-entry, middle entry, diminution,
// stretto, episode), so a statistically weak subject drags the whole piece's
// model_prob. Slots 0/1/4 keep the proven P3-P13 patterns; slots 2/3 replace
// the two lowest-scoring patterns (the "broken triad" and "stepwise sequence"
// melodies scored ~0.86 / ~0.91 in isolation vs ~0.95 for the others) with
// higher-probability diatonic subjects. Both replacements keep the same
// register envelope (71-81) and the mandatory B->C (71,72) leading-tone tail
// so the cadence / leading-tone provenance bits still fire and the
// answer(-5) / V2(-12) / stretto(-24) transposes stay voice-crossing-safe.
// This catalog is referenced ONLY by buildPhase14Fixture, so Phase3-13 stay
// byte-identical.
constexpr std::array<std::array<std::uint8_t, 16>, 5> kPhase14Subjects = {{
    // 0: original Phase 3 arch (unchanged)
    {72, 74, 76, 77, 79, 81, 79, 77, 76, 74, 76, 77, 79, 77, 71, 72},
    // 1: gentle wave (replaces the original high 84-83-84 head, which scored
    // lowest in-context of the kept subjects; this diatonic wave keeps the
    // 71,72 leading-tone tail and a 72-79 register that stays voice-safe)
    {76, 74, 72, 74, 76, 77, 79, 77, 76, 79, 77, 76, 74, 72, 71, 72},
    // 2: neighbour-rich arch (replaces the weak broken-triad subject)
    {79, 77, 76, 77, 79, 81, 79, 77, 76, 74, 72, 74, 76, 74, 71, 72},
    // 3: varied scalar arch. Opens on the same 72,74,76,77 head as slot 0 so
    // the V1 counterline search space over bars 0-3 stays in the validated,
    // diminished-melodic-free region. The body climbs to 81 and then descends
    // with a varied conjunct contour instead of restating the opening 72-77
    // cell verbatim; that de-repetition lifts the model_prob of the seeds that
    // select this slot. Keeps the mandatory 71,72 leading-tone tail.
    {72, 74, 76, 77, 79, 77, 79, 81, 79, 77, 76, 74, 76, 74, 71, 72},
    // 4: varied upper-arch. Same idea as slot 3: the body folds back through
    // 77-79 rather than running a single long descent, so the contour is less
    // predictable. Register 72-81 and the 71,72 leading-tone tail are kept.
    {76, 77, 79, 81, 79, 77, 76, 77, 79, 77, 76, 74, 72, 74, 71, 72},
}};

struct ChordSpec {
  std::uint8_t root_pc;
  bool minor;
};

// 4 harmony patterns × 4 chords each. Roman numerals for reference:
// 0=I-IV-V-I, 1=I-vi-IV-V, 2=I-IV-I-V, 3=I-V-vi-I (deceptive resolved).
constexpr std::array<std::array<ChordSpec, 4>, 4> kHarmonyPatterns = {{
    {{{0, false}, {5, false}, {7, false}, {0, false}}},
    {{{0, false}, {9, true}, {5, false}, {7, false}}},
    {{{0, false}, {5, false}, {0, false}, {7, false}}},
    {{{0, false}, {7, false}, {9, true}, {0, false}}},
}};

void pushCounterlineBar(VoicePlan& vp, SpanId& next_id, std::uint8_t voice, int bar,
                        Subdivision subdivision) {
  Span s;
  s.id = next_id++;
  s.start_tick = static_cast<Tick>(bar) * kTicksPerBar;
  s.end_tick = s.start_tick + kTicksPerBar;
  s.voice = voice;
  s.intent = VoiceIntent::SequentialCounterline;
  s.subdivision = subdivision;
  vp.spans.push_back(s);
}

// Build the Phase14 fixture: a single self-contained 42-bar, 3-voice
// all-technique fugue (C major). Every device P3-P14 is exercised in one
// continuous layout. The builder is deliberately self-contained (it does
// NOT share the generic P3-P13 assembly cascade) so the earlier phases stay
// byte-identical and Phase14's intricate, hand-tuned register layout cannot
// regress them.
//
// Register invariant V0 >= V1 >= V2 holds at every shared tick. Each device
// reuses its proven P3-P13 transpose, register-shifted where two carriers
// would otherwise collide.
//
// Seed derivation matches the generic path: subj_a = (seed/4)%5,
// harm_a = seed%4, eighth = (seed%2)==1 (used only for the two Compose
// counterline windows).
HarnessFixture buildPhase14Fixture(int seed) {
  HarnessFixture out;

  constexpr int kBars = 42;
  constexpr int kSubjectBars = 4;
  // Stretto follower length. The full 16-note follower (bars 26-29) is kept:
  // an ablation showed its low bars-28-29 tail is NOT a model-scorer drag, and
  // dropping it (to an 8-note head stretto) slightly lowers model_prob, so the
  // complete follower stays. The carrier span end is derived from this count so
  // every replayed note lands inside the window (no silent truncation).
  constexpr int kStrettoFollowerNotes = 16;
  constexpr int kStrettoLastBar = 26 + (kStrettoFollowerNotes - 1) / 4;
  const int subj_a = (seed / 4) % 5;
  const int harm_a = seed % 4;
  const bool eighth = (seed % 2) == 1;
  const Subdivision subdivision = eighth ? Subdivision::Eighth : Subdivision::Quarter;

  auto bar_tick = [](int bar) { return static_cast<Tick>(bar) * kTicksPerBar; };
  auto add_note = [](std::vector<MaterialNote>& dst, Tick tick, Tick dur, std::uint8_t pitch) {
    MaterialNote mn;
    mn.start_tick = tick;
    mn.duration = dur;
    mn.pitch = pitch;
    dst.push_back(mn);
  };

  const auto& subj_pat = kPhase14Subjects[subj_a];

  // --- Material: V0 subject (bars 0-3). ---
  for (int n = 0; n < 16; ++n) {
    const int bar = n / 4;
    const int beat = n % 4;
    add_note(out.material.subject, bar_tick(bar) + static_cast<Tick>(beat) * kTicksPerBeat,
             kTicksPerBeat, subj_pat[n]);
  }

  // --- Material: V1 real answer (bars 4-7) = subject -P4 (real answer). ---
  for (int n = 0; n < 16; ++n) {
    const int bar = 4 + n / 4;
    const int beat = n % 4;
    add_note(out.material.answer, bar_tick(bar) + static_cast<Tick>(beat) * kTicksPerBeat,
             kTicksPerBeat, static_cast<std::uint8_t>(subj_pat[n] - 5));
  }

  // --- Material: V2 subject re-entry (bars 8-11) = subject -P8. ---
  for (int n = 0; n < 16; ++n) {
    const int bar = 8 + n / 4;
    const int beat = n % 4;
    add_note(out.material.subject, bar_tick(bar) + static_cast<Tick>(beat) * kTicksPerBeat,
             kTicksPerBeat, static_cast<std::uint8_t>(subj_pat[n] - 12));
  }

  // --- Harmony: 4-chord blocks (bars 0-39) + tonic close (bars 40-41). ---
  // Mirrors the generic degree-tagging map (Phase7) so the P10 strong-4th
  // pre-filter stays active for the Compose counterlines.
  out.harmony.tonic_pc = 0;
  out.harmony.is_minor = false;
  auto harm_idx_for = [&](int blk) { return (harm_a + blk) % 4; };
  const int num_blocks = kBars / 4;  // 10 full 4-bar blocks (bars 0-39).
  for (int blk = 0; blk < num_blocks; ++blk) {
    const auto& pattern = kHarmonyPatterns[harm_idx_for(blk)];
    for (int b = 0; b < 4; ++b) {
      ChordEvent chord;
      chord.start_tick = bar_tick(blk * 4 + b);
      chord.root_pc = pattern[b].root_pc;
      chord.quality = pattern[b].minor ? ChordQuality::Minor : ChordQuality::Major;
      if (pattern[b].root_pc == 0) {
        chord.degree = RomanNumeral::I;
        chord.function = HarmonicFunction::T;
      } else if (pattern[b].root_pc == 5) {
        chord.degree = RomanNumeral::IV;
        chord.function = HarmonicFunction::S;
      } else if (pattern[b].root_pc == 7) {
        chord.degree = RomanNumeral::V;
        chord.function = HarmonicFunction::D;
      } else if (pattern[b].root_pc == 9 && pattern[b].minor) {
        chord.degree = RomanNumeral::VI;
        chord.function = HarmonicFunction::Pred;
      }
      chord.inversion = ChordInversion::Root;
      chord.has_degree = true;
      out.harmony.chords.push_back(chord);
    }
  }
  for (int b = 40; b < kBars; ++b) {
    ChordEvent chord;
    chord.start_tick = bar_tick(b);
    chord.root_pc = 0;
    chord.quality = ChordQuality::Major;
    chord.degree = RomanNumeral::I;
    chord.function = HarmonicFunction::T;
    chord.inversion = ChordInversion::Root;
    chord.has_degree = true;
    out.harmony.chords.push_back(chord);
  }

  // --- Modulation (bars 12-15): pivot at bar 8 (I-of-C = IV-of-G) plus a
  // V/V -> V -> borrowed iv -> Picardy-I chromatic close. Bars 12-15 are
  // all-Material in every voice, so the chromatic chord tones never clash
  // with a Compose note. Verbatim reuse of the generic with_modulation
  // block (only the surrounding layout differs). ---
  {
    ModulationEvent mod;
    mod.tick = bar_tick(8);
    mod.from_tonic_pc = 0;
    mod.from_is_minor = false;
    mod.to_tonic_pc = 7;
    mod.to_is_minor = false;
    mod.type = ModulationType::Pivot;
    out.harmony.modulations.push_back(mod);
    for (auto& chord : out.harmony.chords) {
      const int b = static_cast<int>(chord.start_tick / kTicksPerBar);
      if (b == 12) {  // V/V — D major secondary dominant of V (G major).
        chord.root_pc = 2;
        chord.quality = ChordQuality::Major;
        chord.degree = RomanNumeral::V;
        chord.function = HarmonicFunction::Pred;
        chord.has_degree = true;
        chord.has_secondary_of = true;
        chord.secondary_of = RomanNumeral::V;
        chord.is_borrowed = false;
        chord.is_picardy = false;
        chord.inversion = ChordInversion::Root;
      } else if (b == 13) {  // V — G major resolves the secondary dominant.
        chord.root_pc = 7;
        chord.quality = ChordQuality::Major;
        chord.degree = RomanNumeral::V;
        chord.function = HarmonicFunction::D;
        chord.has_degree = true;
        chord.has_secondary_of = false;
        chord.is_borrowed = false;
        chord.is_picardy = false;
        chord.inversion = ChordInversion::Root;
      } else if (b == 14) {  // Borrowed iv — F minor loan from C parallel-minor.
        chord.root_pc = 5;
        chord.quality = ChordQuality::Minor;
        chord.degree = RomanNumeral::IV;
        chord.function = HarmonicFunction::S;
        chord.has_degree = true;
        chord.has_secondary_of = false;
        chord.is_borrowed = true;
        chord.is_picardy = false;
        chord.inversion = ChordInversion::Root;
      } else if (b == 15) {  // Picardy 3rd — final I (C major).
        chord.root_pc = 0;
        chord.quality = ChordQuality::Major;
        chord.degree = RomanNumeral::I;
        chord.function = HarmonicFunction::T;
        chord.has_degree = true;
        chord.has_secondary_of = false;
        chord.is_borrowed = false;
        chord.is_picardy = true;
        chord.inversion = ChordInversion::Root;
      }
    }
  }

  // --- Tonal answer + countersubject (P6). AnswerCarrier (bars 4-7) reads
  // tonal_answer; the imitation_entry validator reads the real answer above,
  // so both TonalAnswerMapped and ImitationEntryMatched can fire. ---
  {
    std::vector<MaterialNote> subj_head(out.material.subject.begin(),
                                        out.material.subject.begin() + 16);
    out.material.tonal_answer =
        tonal_answer::deriveTonalAnswer(subj_head, out.harmony.tonic_pc, bar_tick(kSubjectBars),
                                        /*head_length=*/4);
    out.material.use_tonal_answer = true;
  }
  // Countersubject (V0, bars 8-15): a gentle G5-A5-B5 line (register 79-84).
  // It sits above the V1 counterline / suspension (<= 74) and the V2 re-entry
  // / NCT figures (<= 72), so V0 >= V1 >= V2 holds across bars 8-15. Across
  // the chromatic modulation (bars 12-15) the CS holds chord tones of each
  // chromatic chord, deliberately avoiding the cross-relation partners of the
  // borrowed tones (no F natural under the V/V F#; no G natural under the
  // borrowed-iv Ab) so the all-voices cross_relation rule stays clear.
  {
    // [bar][beat] CS pitches. A continuously moving G5-A5-B5/C6 line, never
    // holding (a static repeated pitch is what the model scorer penalises).
    // Bars 8-11 trace a varied diatonic wave (G-A-B-C / B-A-G-A / B-C-B-A /
    // G-A-B-A) instead of a four-times-identical cell: removing that repetition
    // measurably lifts the model_prob of the seeds that select this region.
    // Across bars 12-15 every beat still avoids the cross-relation partner of
    // that bar's borrowed tone: bar 12 omits F natural (vs the V/V F#), bar 14
    // omits both G and A natural (vs the borrowed-iv Ab), so the all-voices
    // cross_relation rule stays clear.
    static constexpr std::array<std::array<std::uint8_t, 4>, 8> kCs = {{
        {79, 81, 83, 84},  // bar 8: G-A-B-C, a rising diatonic line.
        {83, 81, 79, 81},  // bar 9: B-A-G-A.
        {83, 84, 83, 81},  // bar 10: B-C-B-A.
        {79, 81, 83, 81},  // bar 11: G-A-B-A.
        {79, 81, 83, 81},  // bar 12: V/V (D F# A) -> G-A-B-A (no F natural).
        {81, 83, 84, 83},  // bar 13: V (G B D) -> A-B-C-B.
        {84, 83, 84, 83},  // bar 14: iv (F Ab C) -> C-B-C-B (no G/A natural).
        {81, 83, 81, 79},  // bar 15: I-Picardy (C E G) -> A-B-A-G.
    }};
    for (int bar = 8; bar <= 15; ++bar) {
      const auto& row = kCs[static_cast<std::size_t>(bar - 8)];
      for (int beat = 0; beat < 4; ++beat) {
        add_note(out.material.countersubject,
                 bar_tick(bar) + static_cast<Tick>(beat) * kTicksPerBeat, kTicksPerBeat,
                 row[static_cast<std::size_t>(beat)]);
      }
    }
  }

  // --- Suspension (P4). One genuine 7-6 in V1 across bars 12-13: prep F#5
  // (78) on the bar-12 downbeat, suspended F#5 (78) held to the bar-13
  // downbeat, resolving down a step to E5 (76) on bar-13 beat 2. F#5 (pc 6)
  // is a chord tone of bar-12 V/V (D F# A) and consonant (M6) against the
  // lowest sounding voice at prep (the V2 NCT A4 = 69), so the preparation
  // ties cleanly into the dissonance.
  //
  // The figure forms a REAL 7-6 against the bass at bar 13 (V = G major):
  // the V2 NCT line below (see the NCT block) sounds the chord root G3 (55)
  // at the bar-13 downbeat AND at beat 2. On the dissonance beat the
  // interval is (78 - 55) % 12 = 11 -> a MAJOR SEVENTH; after the
  // step-down resolution it is (76 - 55) % 12 = 9 -> a MAJOR SIXTH. This is
  // the textbook leading-tone (F#) suspension over the dominant root
  // resolving to the sixth, matching the validator's suspension_seventh_sixth
  // rule (seventh ic in {10,11}, sixth ic in {8,9}). Register at bars 12-13:
  // V0 countersubject (79-84) >= V1 suspension (76-78) >= V2 bass / NCT
  // (55-62), so V0 >= V1 >= V2 holds at every shared tick.
  {
    SuspensionPattern sus;
    sus.type = SuspensionType::Sus7_6;
    sus.preparation_tick = bar_tick(12);
    sus.suspension_tick = bar_tick(13);
    sus.resolution_tick = sus.suspension_tick + kTicksPerBeat;
    sus.preparation_pitch = 78;
    sus.suspension_pitch = 78;
    sus.resolution_pitch = 76;
    sus.voice = 1;
    out.material.suspension_patterns.push_back(sus);
  }

  // --- NCT figures (P14, V2, bars 12-15, register 55-69). Four single-bar
  // figures so each note's active chord is unambiguous. The bits are stamped
  // later by the Composer's NCT post-pass; here we only supply the notes. ---
  {
    const Tick d8 = kTicksPerBeat / 2;
    // Cambiata, bar 12 (V/V = D F# A): A4 G4 D4 E4 F#4.
    add_note(out.material.nct_figures, bar_tick(12) + 0, d8, 69);
    add_note(out.material.nct_figures, bar_tick(12) + 240, d8, 67);
    add_note(out.material.nct_figures, bar_tick(12) + 480, d8, 62);
    add_note(out.material.nct_figures, bar_tick(12) + 720, d8, 64);
    add_note(out.material.nct_figures, bar_tick(12) + 960, d8, 66);
    // Bar 13 (V = G B D). Beats 1-2 sustain the chord ROOT G3 (55) as the
    // bass under the V1 7-6 suspension above (the validator's
    // suspension_seventh_sixth rule measures the lowest sounding voice at the
    // suspension downbeat and at the beat-2 resolution; G3 makes F#5 a 7th and
    // E5 a 6th). The nota-cambiata figure then runs in beats 3-4, decoupled
    // from the suspension beats (a single moving figure cannot present the same
    // bass pitch at both checked beats, since its notes are all distinct, so
    // the steady bass and the cambiata are split in time).
    add_note(out.material.nct_figures, bar_tick(13) + 0, kTicksPerBeat, 55);
    add_note(out.material.nct_figures, bar_tick(13) + 480, kTicksPerBeat, 55);
    // Nota cambiata, bar 13 beats 3-4 (V = G B D): D4 C4 A3 B3 (chord tone ->
    // step-down NCT -> leap-down NCT -> step-up chord tone).
    add_note(out.material.nct_figures, bar_tick(13) + 960, d8, 62);
    add_note(out.material.nct_figures, bar_tick(13) + 1200, d8, 60);
    add_note(out.material.nct_figures, bar_tick(13) + 1440, d8, 57);
    add_note(out.material.nct_figures, bar_tick(13) + 1680, d8, 59);
    // Echappee, bar 14 first half (iv = F Ab C): C4 D4 Ab3.
    add_note(out.material.nct_figures, bar_tick(14) + 0, d8, 60);
    add_note(out.material.nct_figures, bar_tick(14) + 240, d8, 62);
    add_note(out.material.nct_figures, bar_tick(14) + 480, d8, 56);
    // Anticipation, bar 14 beat 3 -> bar 15 (I-Picardy = C E G): F4 E4 ... E4.
    add_note(out.material.nct_figures, bar_tick(14) + 960, d8, 65);
    add_note(out.material.nct_figures, bar_tick(14) + 1440, d8, 64);
    add_note(out.material.nct_figures, bar_tick(15) + 0, kTicksPerBeat, 64);
  }

  // --- Imitation entry (P9): subject (V0) leads, answer (V1) follows a bar
  // later at -P4 (real answer). Documentary; validated against material.answer. ---
  {
    ImitationEntry entry;
    entry.leader_fragment = MaterialFragment::Subject;
    entry.follower_fragment = MaterialFragment::Answer;
    entry.distance_ticks = bar_tick(kSubjectBars);
    entry.interval_semis = -5;
    out.material.imitation_entries.push_back(entry);
  }

  // --- Fortspinnung (P9, V0, bars 4-7): the proven generic SequenceTemplate
  // (AscendingStep, 2 steps, register 79-86). Sits directly after the V0
  // subject (Material->Material boundary) and stays above the V1 answer
  // (max 79). Verbatim reuse of the with_fortspinnung block. ---
  {
    SequenceTemplate tmpl;
    tmpl.pattern = SequencePattern::AscendingStep;
    tmpl.target_start_tick = bar_tick(4);
    tmpl.step_length_ticks = 2 * kTicksPerBar;
    tmpl.num_steps = 2;
    tmpl.voice = 0;
    tmpl.seed_pitches = {79, 84, 86, 84, 79, 84, 86, 84};
    tmpl.seed_durations = {kTicksPerBeat, kTicksPerBeat, kTicksPerBeat, kTicksPerBeat,
                           kTicksPerBeat, kTicksPerBeat, kTicksPerBeat, kTicksPerBeat};
    out.material.sequence_templates.push_back(tmpl);
  }

  // --- Development (P11). Middle entry (V0, bars 16-19, subject -P4 in the
  // dominant key G); dominant pedal (V2, bars 16-19, G3 = 55); diminution
  // variant (V0, bars 20-23); stretto leader appended to the subject (V0,
  // bars 24-27) + stretto follower (V2, enters bar 26, subject -2 octaves);
  // coda (V0, bars 40-41). All verbatim reuse of the with_development block,
  // shifted forward by 4 bars. ---
  {
    auto build_fragment = [&](int base_bar, auto transform) {
      std::vector<MaterialNote> out_notes;
      out_notes.reserve(16);
      for (int n = 0; n < 16; ++n) {
        const int bar = base_bar + n / 4;
        const int beat = n % 4;
        MaterialNote mn;
        mn.start_tick = bar_tick(bar) + static_cast<Tick>(beat) * kTicksPerBeat;
        mn.duration = kTicksPerBeat;
        mn.pitch = static_cast<std::uint8_t>(transform(static_cast<int>(subj_pat[n])));
        out_notes.push_back(mn);
      }
      return out_notes;
    };

    MiddleEntryDecl middle;
    middle.voice = 0;
    middle.related_key_pc = 7;  // V of C = G major.
    middle.notes = build_fragment(16, [](int p) { return p - 5; });
    out.material.middle_entries.push_back(middle);

    PedalPointDecl pedal;
    pedal.voice = 2;
    pedal.start_tick = bar_tick(16);
    pedal.duration = bar_tick(4);
    pedal.pitch = 55;  // G3, dominant pedal.
    pedal.is_dominant = true;
    out.material.pedal_points.push_back(pedal);

    // Diminution (V0, bars 20-23): the subject at half duration (eighths),
    // 16 notes spanning 2 bars, played twice. Sole sounding voice here.
    SubjectVariantDecl variant;
    variant.voice = 0;
    variant.transform = static_cast<std::uint8_t>(motif_ops::EpisodeMotifTransform::Diminish);
    for (int rep = 0; rep < 2; ++rep) {
      const Tick rep_start = bar_tick(20 + 2 * rep);
      for (int n = 0; n < 16; ++n) {
        MaterialNote mn;
        mn.start_tick = rep_start + static_cast<Tick>(n) * (kTicksPerBeat / 2);
        mn.duration = kTicksPerBeat / 2;
        mn.pitch = subj_pat[n];
        variant.notes.push_back(mn);
      }
    }
    out.material.subject_variants.push_back(variant);

    // Stretto leader: subject restated verbatim in V0 at bars 24-27. Appended
    // to material.subject so the V0 SubjectCarrier span at those bars replays
    // it (appended after the annotation pass below so no spurious markers).
    const auto leader = build_fragment(24, [](int p) { return p; });

    // Stretto follower: the full subject restated in V2 at -2 octaves (-24),
    // entering bar 26 (strictly inside the leader's bars 24-27 window) and
    // occupying bars 26-29. The follower stays COMPLETE (all 16 notes sound);
    // the carrier span below reaches kStrettoLastBar so the replay loop never
    // clips a note. -24 preserves the C-major pitch classes; register 47-60
    // clears the leader (71-84). The validator stretto_overlap_valid rule
    // checks (a) the follower enters strictly inside the leader window and (b)
    // follower_notes[i] == subject[i] + interval; StrettoCommitted (bit 35)
    // fires once per replayed follower note. Bars 28-29 register: only V0
    // (Episode, subject range 71-84) and V2 (follower, range 47-60) sound; no
    // V1 voice is active there, so V0 >= V2 holds with no crossing.
    StrettoDecl stretto;
    stretto.leader_voice = 0;
    stretto.follower_voice = 2;
    stretto.leader_entry_tick = bar_tick(24);
    stretto.leader_length_ticks = bar_tick(4);
    stretto.follower_entry_tick = bar_tick(26);
    stretto.interval_semis = -24;
    {
      std::vector<MaterialNote> follower;
      follower.reserve(kStrettoFollowerNotes);
      for (int n = 0; n < kStrettoFollowerNotes; ++n) {
        const int bar = 26 + n / 4;
        const int beat = n % 4;
        MaterialNote mn;
        mn.start_tick = bar_tick(bar) + static_cast<Tick>(beat) * kTicksPerBeat;
        mn.duration = kTicksPerBeat;
        mn.pitch = static_cast<std::uint8_t>(subj_pat[n] - 24);
        follower.push_back(mn);
      }
      stretto.follower_notes = std::move(follower);
    }
    out.material.stretto_entries.push_back(stretto);

    // Coda (V0, bars 40-41): a stepwise C-major close (range 71-79) settling
    // on the tonic. Two bars / 8 quarters; no leap > 2 semitones.
    CodaDecl coda;
    coda.voice = 0;
    static constexpr std::array<std::uint8_t, 8> kCoda = {79, 77, 76, 74, 72, 74, 72, 72};
    for (int n = 0; n < 8; ++n) {
      const int bar = 40 + n / 4;
      const int beat = n % 4;
      add_note(coda.notes, bar_tick(bar) + static_cast<Tick>(beat) * kTicksPerBeat, kTicksPerBeat,
               kCoda[static_cast<std::size_t>(n)]);
    }
    out.material.coda_extensions.push_back(coda);

    // Episode (V0, bars 28-31): Original transform of the first 16 subject
    // notes, re-anchored at bar 28 (subject restatement as episode).
    EpisodeFragment ef;
    ef.transform = static_cast<std::uint8_t>(motif_ops::EpisodeMotifTransform::Original);
    ef.source_start_index = 0;
    ef.source_count = 16;
    ef.voice = 0;
    ef.target_start_tick = bar_tick(28);
    ef.invert_pivot = 72;
    ef.augment_factor = 2;
    ef.diminish_factor = 2;
    out.material.episodes.push_back(ef);

    // --- Rhythm (P12, V0 bars 32-39 + V2 recurrence bars 36-39). A regular
    // 4-bar phrase grid with a quarter-note anacrusis into bar 36. The dotted
    // figure (bars 32-35) and a syncopated consequent with a hemiola at bars
    // 38-39 (V0) run above the rhythmic-motif recurrence (V2, range 60-67). ---
    PhraseStructure& ps = out.material.phrase_structure;
    ps.has_anacrusis = true;
    ps.anacrusis_ticks = kTicksPerBeat;
    ps.phrase_start_ticks.push_back(bar_tick(32));
    ps.phrase_start_ticks.push_back(bar_tick(36));

    const Tick d8 = kTicksPerBeat / 2;
    const Tick dq = kTicksPerBeat;
    const Tick dd = kTicksPerBeat + d8;  // dotted quarter (720)
    const Tick ddh = 3 * kTicksPerBeat;  // dotted half (1440)
    const Tick dh = 2 * kTicksPerBeat;   // half

    // Dotted figure (V0, bars 32-35): dotted-quarter + eighth + two quarters
    // per bar; bar 35 stops a beat early to leave room for the anacrusis. The
    // first note lands on the bar-32 phrase downbeat.
    RhythmFragment dotted;
    dotted.feature = RhythmFragment::Feature::Dotted;
    dotted.voice = 0;
    {
      const std::array<std::uint8_t, 15> pit = {72, 74, 76, 77, 79, 77, 76, 74,
                                                76, 77, 79, 77, 76, 74, 72};
      const std::array<Tick, 15> dur = {dd, d8, dq, dq, dd, d8, dq, dq, dd, d8, dq, dq, dd, d8, dq};
      Tick tick = bar_tick(32);
      for (std::size_t i = 0; i < pit.size(); ++i) {
        add_note(dotted.notes, tick, dur[i], pit[i]);
        tick += dur[i];
      }
    }
    out.material.rhythm_fragments.push_back(dotted);

    // Anacrusis (V0): a quarter-note pickup (B4 = leading tone) on bar 35
    // beat 4, exactly anacrusis_ticks before the bar-36 phrase start.
    RhythmFragment anac;
    anac.feature = RhythmFragment::Feature::Anacrusis;
    anac.voice = 0;
    add_note(anac.notes, bar_tick(36) - dq, dq, 71);
    out.material.rhythm_fragments.push_back(anac);

    // Syncopation (V0, bars 36-39, consequent): off-beat onsets; a hemiola
    // (3+3+2 regrouping) closes bars 38-39. The first note is the bar-36
    // phrase downbeat. Register 71-79 stays above the V2 recurrence (60-67).
    RhythmFragment sync;
    sync.feature = RhythmFragment::Feature::Syncopation;
    sync.voice = 0;
    {
      const std::array<std::array<std::uint8_t, 5>, 2> rows = {
          {{72, 74, 76, 74, 72}, {74, 76, 77, 76, 74}}};
      const std::array<Tick, 5> dur = {d8, dq, dq, dq, d8};
      for (int b = 0; b < 2; ++b) {
        Tick tick = bar_tick(36 + b);
        for (int i = 0; i < 5; ++i) {
          add_note(sync.notes, tick, dur[i],
                   rows[static_cast<std::size_t>(b)][static_cast<std::size_t>(i)]);
          tick += dur[i];
        }
      }
    }
    out.material.rhythm_fragments.push_back(sync);

    // Hemiola (V0, bars 38-39): two dotted-half notes + a half note cut across
    // the 4/4 grid (3+3+2 beats), a 3-against-2 regrouping at the cadence.
    RhythmFragment hemiola;
    hemiola.feature = RhythmFragment::Feature::Hemiola;
    hemiola.voice = 0;
    {
      Tick tick = bar_tick(38);
      add_note(hemiola.notes, tick, ddh, 76);
      tick += ddh;
      add_note(hemiola.notes, tick, ddh, 74);
      tick += ddh;
      add_note(hemiola.notes, tick, dh, 72);
    }
    out.material.rhythm_fragments.push_back(hemiola);

    // Rhythmic-motif recurrence (V2, bars 36-39): the dotted figure's rhythm
    // restated lower (range 60-67), under the syncopated consequent.
    RhythmFragment recur;
    recur.feature = RhythmFragment::Feature::Recurrence;
    recur.voice = 2;
    {
      const std::array<std::uint8_t, 15> pit = {60, 62, 64, 65, 67, 65, 64, 62,
                                                64, 65, 67, 65, 64, 62, 60};
      const std::array<Tick, 15> dur = {dd, d8, dq, dq, dd, d8, dq, dq, dd, d8, dq, dq, dd, d8, dq};
      Tick tick = bar_tick(36);
      for (std::size_t i = 0; i < pit.size(); ++i) {
        add_note(recur.notes, tick, dur[i], pit[i]);
        tick += dur[i];
      }
    }
    out.material.rhythm_fragments.push_back(recur);

    // --- Texture / expression plan (P13). Generous per-voice ranges that
    // bound every Phase14 pitch; organ-manual routing for all 3 voices; a
    // per-voice articulation span; an Affekt velocity curve. No pedal voice
    // (pedal_range_soft_penalty stays inert). ---
    TexturePlan& tp = out.material.texture_plan;
    tp.voice_ranges.push_back({/*voice=*/0, /*lo=*/48, /*hi=*/96});
    tp.voice_ranges.push_back({/*voice=*/1, /*lo=*/40, /*hi=*/88});
    tp.voice_ranges.push_back({/*voice=*/2, /*lo=*/33, /*hi=*/96});
    tp.manual_assignments.push_back({/*voice=*/0, /*manual=*/0});
    tp.manual_assignments.push_back({/*voice=*/1, /*manual=*/0});
    tp.manual_assignments.push_back({/*voice=*/2, /*manual=*/3});
    const Tick piece_end = bar_tick(kBars);
    for (VoiceId v = 0; v < 3; ++v) {
      tp.articulations.push_back({v, /*start_tick=*/0, piece_end, /*kind=*/1});
    }
    tp.affekt_curve_active = true;
    tp.affekt_character = static_cast<std::uint8_t>(seed % 4);
    tp.pedal_voice = 0xFF;

    // --- Cadence + leading-tone annotation (generic path). Annotate the
    // subject's exposition leading-tone cadence at bar 3 beat 3, then append
    // the stretto-leader subject notes (so they pick up no spurious markers). ---
    annotateLeadingToneMarkers(out.material, out.harmony.tonic_pc, out.harmony.is_minor);
    const Tick subject_cadence_tick = bar_tick(kSubjectBars) - kTicksPerBeat;
    for (const auto& marker : out.material.leading_tone_markers) {
      if (marker.fragment != MaterialFragment::Subject)
        continue;
      if (marker.resolution_tick != subject_cadence_tick)
        continue;
      CadenceEvent cadence;
      cadence.tick = marker.resolution_tick;
      cadence.type = CadenceType::Perfect;
      out.harmony.cadences.push_back(cadence);
      if (marker.leading_tick >= kTicksPerBeat) {
        CadentialSixFour six_four;
        six_four.tick = marker.leading_tick - kTicksPerBeat;
        six_four.resolution_tick = marker.leading_tick;
        out.harmony.cadential_six_fours.push_back(six_four);
      }
    }
    annotateCadenceCells(out.material, out.harmony);

    for (const auto& mnote : leader)
      out.material.subject.push_back(mnote);
  }

  // --- VoicePlan. Built explicitly span-by-span (NOT via the generic
  // cascade) so every span lands at its designed bar window. ---
  out.voice_plan.num_voices = 3;
  SpanId next_id = 0;
  auto push_span = [&](VoiceId voice, int first_bar, int last_bar, VoiceIntent intent) {
    Span span;
    span.id = next_id++;
    span.start_tick = bar_tick(first_bar);
    span.end_tick = bar_tick(last_bar + 1);
    span.voice = voice;
    span.intent = intent;
    span.subdivision = subdivision;
    out.voice_plan.spans.push_back(span);
  };

  // V0: subject | fortspinnung | countersubject | middle entry | diminution |
  //     stretto leader | episode | rhythm | rhythm | coda.
  push_span(0, 0, 3, VoiceIntent::SubjectCarrier);
  push_span(0, 4, 7, VoiceIntent::FortspinnungSpan);
  push_span(0, 8, 15, VoiceIntent::CountersubjectCarrier);
  push_span(0, 16, 19, VoiceIntent::MiddleEntryCarrier);
  push_span(0, 20, 23, VoiceIntent::SubjectCarrierDiminished);
  push_span(0, 24, 27, VoiceIntent::SubjectCarrier);
  push_span(0, 28, 31, VoiceIntent::Episode);
  push_span(0, 32, 35, VoiceIntent::RhythmCarrier);
  push_span(0, 36, 39, VoiceIntent::RhythmCarrier);
  push_span(0, 40, 41, VoiceIntent::CodaCarrier);

  // V1: counterline 0-3 (Compose) | answer 4-7 | counterline 8-11 (Compose) |
  //     suspension 12-13 | (rest 14-41).
  for (int bar = 0; bar <= 3; ++bar)
    pushCounterlineBar(out.voice_plan, next_id, 1, bar, subdivision);
  push_span(1, 4, 7, VoiceIntent::AnswerCarrier);
  for (int bar = 8; bar <= 11; ++bar)
    pushCounterlineBar(out.voice_plan, next_id, 1, bar, subdivision);
  push_span(1, 12, 13, VoiceIntent::SuspensionCarrier);

  // V2: (rest 0-7) | subject re-entry 8-11 | NCT 12-15 | pedal 16-19 |
  //     (rest 20-23) | stretto follower 24-29 | (rest 30-35) | recurrence
  //     36-39 | (rest 40-41).
  push_span(2, 8, 11, VoiceIntent::SubjectCarrier);
  push_span(2, 12, 15, VoiceIntent::NctCarrier);
  push_span(2, 16, 19, VoiceIntent::PedalCarrier);
  // Stretto follower enters at bar 26 (after the V0 leader at bar 24) and its
  // full 16 quarter-notes occupy bars 26-29. The carrier window reaches
  // kStrettoLastBar (= bar 29, end_tick = bar30) so the replay loop emits every
  // note with none clipped (the follower stays complete). Bars 28-29 register:
  // only V0 (Episode, subject range 71-84) and V2 (follower = subj_pat - 24,
  // range 47-60) sound; no V1 voice is active there, so V0 >= V2 holds with no
  // crossing.
  push_span(2, 24, kStrettoLastBar, VoiceIntent::StrettoCarrier);
  push_span(2, 36, 39, VoiceIntent::RhythmCarrier);

  return out;
}

}  // namespace

HarnessPhaseSpec phaseSpec(HarnessPhase phase) {
  switch (phase) {
    case HarnessPhase::Phase3:
      return {phase, /*voices=*/2, /*bars=*/8, /*subject_bars=*/8,
              false, false,        false,      false,
              false, false,        false,      false,
              false, false,        false,      false,
              false};
    case HarnessPhase::Phase35:
      return {phase, /*voices=*/2, /*bars=*/4, /*subject_bars=*/4,
              false, false,        false,      false,
              false, false,        false,      false,
              false, false,        false,      false,
              false};
    case HarnessPhase::Phase4:
      return {phase, /*voices=*/2, /*bars=*/8, /*subject_bars=*/4,
              true,  false,        false,      false,
              false, false,        false,      false,
              false, false,        false,      false,
              false};
    case HarnessPhase::Phase5:
      return {phase, /*voices=*/3, /*bars=*/12, /*subject_bars=*/12,
              false, false,        false,       false,
              false, false,        false,       false,
              false, false,        false,       false,
              false};
    case HarnessPhase::Phase6:
      return {phase, /*voices=*/3, /*bars=*/16, /*subject_bars=*/4,
              true,  true,         false,       false,
              false, false,        false,       false,
              false, false,        false,       false,
              false};
    case HarnessPhase::Phase4Sus:
      return {phase, /*voices=*/2, /*bars=*/8, /*subject_bars=*/4,
              true,  false,        true,       false,
              false, false,        false,      false,
              false, false,        false,      false,
              false};
    case HarnessPhase::Phase6Episode:
      return {phase, /*voices=*/3, /*bars=*/16, /*subject_bars=*/4,
              true,  true,         false,       true,
              false, false,        false,       false,
              false, false,        false,       false,
              false};
    case HarnessPhase::Phase6Tonal:
      return {phase, /*voices=*/3, /*bars=*/16, /*subject_bars=*/4,
              true,  true,         false,       false,
              true,  false,        false,       false,
              false, false,        false,       false,
              false};
    case HarnessPhase::Phase7:
      return {phase, /*voices=*/3, /*bars=*/16, /*subject_bars=*/4,
              true,  true,         false,       false,
              false, true,         false,       false,
              false, false,        false,       false,
              false};
    case HarnessPhase::Phase8:
      return {phase, /*voices=*/3, /*bars=*/16, /*subject_bars=*/4,
              true,  true,         false,       false,
              false, true,         true,        false,
              false, false,        false,       false,
              false};
    case HarnessPhase::Phase9:
      return {phase, /*voices=*/3, /*bars=*/16, /*subject_bars=*/4,
              true,  true,         false,       false,
              false, true,         true,        true,
              true,  false,        false,       false,
              false};
    case HarnessPhase::Phase10:
      return {phase, /*voices=*/3, /*bars=*/16, /*subject_bars=*/4,
              true,  true,         false,       false,
              false, true,         true,        false,
              false, false,        false,       false,
              false};
    case HarnessPhase::Phase11:
      // 28 bar / 3 voice. Material assembly reuses with_answer +
      // with_third_entry (subject 0-3, answer 4-7, V2 re-entry 8-11);
      // with_development drives the bars 12-27 carriers and its own
      // voice plan. Degree tagging is on (like Phase7) so the P10
      // strong-4th candidate pre-filter — gated on chord.has_degree —
      // stays active for the exposition's Compose counterlines; without
      // it the composer would pick a strong-beat perfect 4th in the
      // (V0, V1) upper pair and trip fourth_only_on_weak_beat. Modulation
      // stays off (no chromatic idioms); the all-Material development
      // needs no P7/P8 help and gate (4) only needs the P11 bits.
      return {phase, /*voices=*/3, /*bars=*/28, /*subject_bars=*/4,
              true,  true,         false,       false,
              false, true,         false,       false,
              false, true,         false,       false,
              false};
    case HarnessPhase::Phase12:
      // 28 bar / 3 voice. Same exposition assembly as Phase11 (with_answer
      // + with_third_entry + degree tagging for the P10 strong-4th
      // pre-filter), but with_rhythm drives the bars 12-27 rhythm section
      // and its own voice plan instead of with_development.
      return {phase, /*voices=*/3, /*bars=*/28, /*subject_bars=*/4,
              true,  true,         false,       false,
              false, true,         false,       false,
              false, false,        true,        false,
              false};
    case HarnessPhase::Phase13:
      // 16 bar / 3 voice. Reuses the Phase7 exposition assembly (with_answer
      // + with_third_entry + degree tagging for the P10 strong-4th
      // pre-filter); with_texture attaches the texture/expression plan that
      // the Composer's post-pass consumes. Voice density already varies
      // because V2 enters only at bar 8 (2 voices bars 0-7, 3 voices bars
      // 8-15). Modulation stays off so the scored content matches the clean
      // Phase7 exposition.
      return {phase, /*voices=*/3, /*bars=*/16, /*subject_bars=*/4,
              true,  true,         false,       false,
              false, true,         false,       false,
              false, false,        false,       true,
              false};
    case HarnessPhase::Phase14:
      // 42 bar / 3 voice. All thirteen device flags true. A dedicated
      // self-contained builder (buildPhase14Fixture) constructs the whole
      // fixture when with_nct is set, so the P3-P13 layouts above stay
      // byte-identical (this spec's flags only gate the dispatch).
      return {phase, /*voices=*/3, /*bars=*/42, /*subject_bars=*/4,
              true,  true,         true,        true,
              true,  true,         true,        true,
              true,  true,         true,        true,
              true};
  }
  return {phase, 2,     8,     8,     false, false, false, false, false,
          false, false, false, false, false, false, false, false};
}

HarnessFixture buildHarnessFixture(HarnessPhase phase, int seed) {
  const HarnessPhaseSpec spec = phaseSpec(phase);
  HarnessFixture out;

  // Phase14 has its own self-contained builder (anon namespace). Dispatching
  // here keeps the entire P3-P13 assembly below byte-identical.
  if (spec.with_nct) {
    return buildPhase14Fixture(seed);
  }

  const int num_blocks = spec.bars / 4;
  const int subj_a = (seed / 4) % 5;
  const int harm_a = seed % 4;
  const bool eighth = (seed % 2) == 1;
  const Subdivision subdivision = eighth ? Subdivision::Eighth : Subdivision::Quarter;

  auto subj_idx_for = [&](int blk) { return (subj_a + blk) % 5; };
  auto harm_idx_for = [&](int blk) { return (harm_a + blk) % 4; };

  const int subject_bars = spec.subject_bars;
  const int subject_blocks = subject_bars / 4;

  // V0 SubjectCarrier material.
  for (int blk = 0; blk < subject_blocks; ++blk) {
    const auto& pattern = kSubjectPatterns[subj_idx_for(blk)];
    for (int n = 0; n < 16; ++n) {
      MaterialNote mn;
      const int bar_in_block = n / 4;
      const int beat_in_bar = n % 4;
      mn.start_tick = static_cast<Tick>(blk * 4 + bar_in_block) * kTicksPerBar +
                      static_cast<Tick>(beat_in_bar) * kTicksPerBeat;
      mn.duration = kTicksPerBeat;
      mn.pitch = pattern[n];
      out.material.subject.push_back(mn);
    }
  }

  // V2 SubjectCarrier re-entry (Phase 6 only). Pattern -P8 so it sits
  // below the existing two voices without crossing.
  if (spec.with_third_entry) {
    const auto& src = kSubjectPatterns[subj_a];
    const int entry_bar = 2 * subject_bars;
    for (int n = 0; n < 16; ++n) {
      MaterialNote mn;
      const int bar_in_block = n / 4;
      const int beat_in_bar = n % 4;
      mn.start_tick = static_cast<Tick>(entry_bar + bar_in_block) * kTicksPerBar +
                      static_cast<Tick>(beat_in_bar) * kTicksPerBeat;
      mn.duration = kTicksPerBeat;
      mn.pitch = static_cast<std::uint8_t>(src[n] - 12);
      out.material.subject.push_back(mn);
    }
  }

  // V1 AnswerCarrier material (Phase 4+): real answer = subject -P4.
  if (spec.with_answer) {
    const auto& src = kSubjectPatterns[subj_a];
    for (int n = 0; n < 16; ++n) {
      MaterialNote an;
      const int bar_in_block = n / 4;
      const int beat_in_bar = n % 4;
      an.start_tick = static_cast<Tick>(subject_bars + bar_in_block) * kTicksPerBar +
                      static_cast<Tick>(beat_in_bar) * kTicksPerBeat;
      an.duration = kTicksPerBeat;
      an.pitch = static_cast<std::uint8_t>(src[n] - 5);
      out.material.answer.push_back(an);
    }
  }

  // Harmony.
  out.harmony.tonic_pc = 0;
  out.harmony.is_minor = false;
  for (int blk = 0; blk < num_blocks; ++blk) {
    const auto& pattern = kHarmonyPatterns[harm_idx_for(blk)];
    for (int b = 0; b < 4; ++b) {
      ChordEvent c;
      c.start_tick = static_cast<Tick>(blk * 4 + b) * kTicksPerBar;
      c.root_pc = pattern[b].root_pc;
      c.quality = pattern[b].minor ? ChordQuality::Minor : ChordQuality::Major;
      if (spec.with_degree_tagging) {
        // Phase7 enriches every ChordEvent with degree/inversion/function
        // so the Validator's P7 rules (doubling, spacing) fire and the
        // candidate provenance picks up ChordToneRoman / InversionLabel /
        // DoublingChecked / SpacingChecked bits. Mapping is fixed for the
        // C-major harness vocabulary:
        //   (root=0, !minor) → I (Tonic)
        //   (root=5, !minor) → IV (Subdominant)
        //   (root=7, !minor) → V (Dominant)
        //   (root=9, minor)  → vi (Predominant)
        // All Phase7 chords are emitted in root position.
        if (pattern[b].root_pc == 0) {
          c.degree = RomanNumeral::I;
          c.function = HarmonicFunction::T;
        } else if (pattern[b].root_pc == 5) {
          c.degree = RomanNumeral::IV;
          c.function = HarmonicFunction::S;
        } else if (pattern[b].root_pc == 7) {
          c.degree = RomanNumeral::V;
          c.function = HarmonicFunction::D;
        } else if (pattern[b].root_pc == 9 && pattern[b].minor) {
          c.degree = RomanNumeral::VI;
          c.function = HarmonicFunction::Pred;
        }
        c.inversion = ChordInversion::Root;
        c.has_degree = true;
      }
      out.harmony.chords.push_back(c);
    }
  }

  // Phase8 modulation injection. Augments the Phase7 layout with:
  //   - a ModulationEvent at bar 8 (the boundary is the implicit
  //     I-of-C = IV-of-G pivot already at that tick),
  //   - a V/V → V secondary-dominant pair at bars 12-13,
  //   - a borrowed iv (parallel minor mixture) at bar 14,
  //   - a Picardy 3rd marker on the final I chord at bar 15.
  // The pre-bar-12 chord vocabulary is untouched so existing Phase7
  // counterpoint behavior carries forward; only the last 4 bars host
  // the P8 idioms. Bars 12-15 sit entirely outside the V2
  // SubjectCarrier window (bars 8-11) so the chromatic chord tones
  // (F# from V/V, Ab from borrowed iv) do not clash with Material
  // pitches.
  if (spec.with_modulation) {
    ModulationEvent mod;
    mod.tick = static_cast<Tick>(8) * kTicksPerBar;
    mod.from_tonic_pc = 0;
    mod.from_is_minor = false;
    mod.to_tonic_pc = 7;
    mod.to_is_minor = false;
    mod.type = ModulationType::Pivot;
    out.harmony.modulations.push_back(mod);
    for (auto& chord : out.harmony.chords) {
      const int b = static_cast<int>(chord.start_tick / kTicksPerBar);
      if (b == 12) {
        // V/V — D-major secondary dominant of V (G major). secondary_of
        // is the home-key degree being tonicized.
        chord.root_pc = 2;
        chord.quality = ChordQuality::Major;
        chord.degree = RomanNumeral::V;
        chord.function = HarmonicFunction::Pred;
        chord.has_degree = true;
        chord.has_secondary_of = true;
        chord.secondary_of = RomanNumeral::V;
        chord.is_borrowed = false;
        chord.is_picardy = false;
        chord.inversion = ChordInversion::Root;
      } else if (b == 13) {
        // V — G-major resolves the secondary dominant.
        chord.root_pc = 7;
        chord.quality = ChordQuality::Major;
        chord.degree = RomanNumeral::V;
        chord.function = HarmonicFunction::D;
        chord.has_degree = true;
        chord.has_secondary_of = false;
        chord.is_borrowed = false;
        chord.is_picardy = false;
        chord.inversion = ChordInversion::Root;
      } else if (b == 14) {
        // Borrowed iv — F-minor loan from C parallel-minor.
        chord.root_pc = 5;
        chord.quality = ChordQuality::Minor;
        chord.degree = RomanNumeral::IV;
        chord.function = HarmonicFunction::S;
        chord.has_degree = true;
        chord.has_secondary_of = false;
        chord.is_borrowed = true;
        chord.is_picardy = false;
        chord.inversion = ChordInversion::Root;
      } else if (b == 15) {
        // Picardy 3rd — final I (C major). is_picardy=true lets the
        // PicardyThird bit fire on any voice landing on the major
        // third (E natural, pc=4).
        chord.root_pc = 0;
        chord.quality = ChordQuality::Major;
        chord.degree = RomanNumeral::I;
        chord.function = HarmonicFunction::T;
        chord.has_degree = true;
        chord.has_secondary_of = false;
        chord.is_borrowed = false;
        chord.is_picardy = true;
        chord.inversion = ChordInversion::Root;
      }
    }
  }

  // P13 texture / instrument / expression plan. Attached only for Phase13
  // (with_texture); the Composer post-pass reads it after candidate
  // placement to stamp the four P13 bits and apply the velocity curve.
  if (spec.with_texture) {
    TexturePlan& tp = out.material.texture_plan;
    // Generous per-voice MIDI ranges (~2 octaves around each voice center
    // 72 / 64 / 57) that comfortably bound every candidate-search pitch, so
    // voice_range_integrity never fires on valid output while VoiceRangeKept
    // still stamps every note.
    tp.voice_ranges.push_back({/*voice=*/0, /*lo=*/48, /*hi=*/96});
    tp.voice_ranges.push_back({/*voice=*/1, /*lo=*/40, /*hi=*/88});
    tp.voice_ranges.push_back({/*voice=*/2, /*lo=*/33, /*hi=*/81});
    // Organ-manual routing: upper two voices on the Great, lowest on the
    // Pedal manual (documentary; no separate MIDI track in this harness).
    tp.manual_assignments.push_back({/*voice=*/0, /*manual=*/0});
    tp.manual_assignments.push_back({/*voice=*/1, /*manual=*/0});
    tp.manual_assignments.push_back({/*voice=*/2, /*manual=*/3});
    // One detache articulation span per voice spanning the whole piece.
    const Tick piece_end = static_cast<Tick>(spec.bars) * kTicksPerBar;
    for (VoiceId v = 0; v < spec.voices; ++v) {
      tp.articulations.push_back({v, /*start_tick=*/0, piece_end, /*kind=*/1});
    }
    // Affekt velocity curve; character derived from the seed (documentary).
    tp.affekt_curve_active = true;
    tp.affekt_character = static_cast<std::uint8_t>(seed % 4);
    // No pedal voice in this exposition layout: the lowest voice carries the
    // melodic subject re-entry (up to MIDI 72), not a pedal point, so leaving
    // pedal_voice = 0xFF keeps pedal_range_soft_penalty inert here (it is
    // exercised directly by its unit test).
    tp.pedal_voice = 0xFF;
  }

  // VoicePlan.
  out.voice_plan.num_voices = spec.voices;
  SpanId next_id = 0;
  Span subject_span;
  subject_span.id = next_id++;
  subject_span.start_tick = 0;
  subject_span.end_tick = static_cast<Tick>(subject_bars) * kTicksPerBar;
  subject_span.voice = 0;
  subject_span.intent = VoiceIntent::SubjectCarrier;
  out.voice_plan.spans.push_back(subject_span);

  if (spec.with_rhythm) {
    // Phase12 fixed layout (28 bars). Exposition bars 0-11 mirror Phase11.
    // The rhythm section (bars 12-27) is entirely Material: one V0
    // RhythmCarrier per 4-bar phrase (each replays whichever rhythm
    // fragments fall in its window) plus a V2 rhythmic-motif recurrence at
    // bars 16-19. V1 rests after the exposition. Register keeps V0 above
    // V2 at the only shared window (bars 16-19).
    //   V0: subject 0-3 | counterline 4-11 | rhythm phrases 12-15 / 16-19 /
    //       20-23 / 24-27
    //   V1: counterline 0-3 | answer 4-7 | counterline 8-11 | (rest)
    //   V2: (rest) | subject 8-11 | (rest) | recurrence 16-19 | (rest)
    auto pushSpan = [&](VoiceId voice, int first_bar, int last_bar, VoiceIntent intent) {
      Span s;
      s.id = next_id++;
      s.start_tick = static_cast<Tick>(first_bar) * kTicksPerBar;
      s.end_tick = static_cast<Tick>(last_bar + 1) * kTicksPerBar;
      s.voice = voice;
      s.intent = intent;
      s.subdivision = subdivision;
      out.voice_plan.spans.push_back(s);
    };
    for (int b = 4; b <= 11; ++b)
      pushCounterlineBar(out.voice_plan, next_id, 0, b, subdivision);
    pushSpan(0, 12, 15, VoiceIntent::RhythmCarrier);
    pushSpan(0, 16, 19, VoiceIntent::RhythmCarrier);
    pushSpan(0, 20, 23, VoiceIntent::RhythmCarrier);
    pushSpan(0, 24, 27, VoiceIntent::RhythmCarrier);
    for (int b = 0; b <= 3; ++b)
      pushCounterlineBar(out.voice_plan, next_id, 1, b, subdivision);
    pushSpan(1, 4, 7, VoiceIntent::AnswerCarrier);
    for (int b = 8; b <= 11; ++b)
      pushCounterlineBar(out.voice_plan, next_id, 1, b, subdivision);
    pushSpan(2, 8, 11, VoiceIntent::SubjectCarrier);
    pushSpan(2, 16, 19, VoiceIntent::RhythmCarrier);
  } else if (spec.with_development) {
    // Phase11 fixed layout (28 bars). The subject_span (V0 bars 0-3) is
    // already pushed above. Exposition bars 0-11 mirror Phase8 (V0 subject
    // + free counterline, V1 free counterline + answer, V2 subject
    // re-entry). The development bars 12-27 is entirely Material: each
    // development carrier sits directly after that voice's previous
    // Material (or is the voice's final span) so no Compose note is ever
    // immediately followed by a Material note in the same voice — this
    // dodges the unprepared_dissonance boundary failure mode (the rule
    // checks a Compose note's next same-voice note). Register layout
    // keeps V0 highest at every shared tick (no voice crossing):
    //   V0: subject 0-3 | counterline 4-11 | middle entry 12-15 (G/V) |
    //       inverted variant 16-19 | subject leader 20-23 | coda 24-27
    //   V1: counterline 0-3 | answer 4-7 | counterline 8-11 | (rest)
    //   V2: (rest) | subject 8-11 | dominant pedal 12-15 | (rest) |
    //       stretto follower 22-25 | (rest)
    auto pushSpan = [&](VoiceId voice, int first_bar, int last_bar, VoiceIntent intent) {
      Span s;
      s.id = next_id++;
      s.start_tick = static_cast<Tick>(first_bar) * kTicksPerBar;
      s.end_tick = static_cast<Tick>(last_bar + 1) * kTicksPerBar;
      s.voice = voice;
      s.intent = intent;
      s.subdivision = subdivision;
      out.voice_plan.spans.push_back(s);
    };
    // V0 free counterline bars 4-11.
    for (int b = 4; b <= 11; ++b)
      pushCounterlineBar(out.voice_plan, next_id, 0, b, subdivision);
    pushSpan(0, 12, 15, VoiceIntent::MiddleEntryCarrier);
    pushSpan(0, 16, 19, VoiceIntent::SubjectCarrierDiminished);
    pushSpan(0, 20, 23, VoiceIntent::SubjectCarrier);
    pushSpan(0, 24, 27, VoiceIntent::CodaCarrier);
    // V1 counterline 0-3, answer 4-7, counterline 8-11, then rests.
    for (int b = 0; b <= 3; ++b)
      pushCounterlineBar(out.voice_plan, next_id, 1, b, subdivision);
    pushSpan(1, 4, 7, VoiceIntent::AnswerCarrier);
    for (int b = 8; b <= 11; ++b)
      pushCounterlineBar(out.voice_plan, next_id, 1, b, subdivision);
    // V2 subject re-entry 8-11, dominant pedal 12-15, stretto follower 22-25.
    pushSpan(2, 8, 11, VoiceIntent::SubjectCarrier);
    pushSpan(2, 12, 15, VoiceIntent::PedalCarrier);
    pushSpan(2, 22, 25, VoiceIntent::StrettoCarrier);
  } else if (spec.with_third_entry) {
    // Phase6Episode replaces V0 counterline bars [bars - subject_bars, bars)
    // with one Episode span (Original transform of the V0 subject, re-anchored
    // at that bar). Phase6Tonal replaces V0 counterline bars [subject_bars,
    // 2*subject_bars) with one CountersubjectCarrier span that runs against
    // the V1 AnswerCarrier (tonal_answer). Phase9 replaces V0 counterline
    // bars [subject_bars, 2*subject_bars) (the V1 AnswerCarrier window) with
    // one FortspinnungSpan carrying a 2-step ascending sequence. Placing
    // the fortspinnung directly after V0 SubjectCarrier (Material→Material)
    // avoids the Compose→Material boundary issue where the composer cannot
    // see the carrier's first pitch in its lookahead, and keeps V0 still
    // active against the AnswerCarrier in V1. Phase6 keeps all V0
    // counterline bars contiguous.
    const int episode_first_bar = spec.with_episode ? (spec.bars - subject_bars) : -1;
    const int cs_first_bar = spec.with_tonal_answer ? subject_bars : -1;
    const int cs_last_bar = spec.with_tonal_answer ? (2 * subject_bars - 1) : -1;
    const int fs_first_bar = spec.with_fortspinnung ? subject_bars : -1;
    const int fs_last_bar = spec.with_fortspinnung ? (2 * subject_bars - 1) : -1;
    for (int b = subject_bars; b < spec.bars; ++b) {
      if (spec.with_episode && b >= episode_first_bar)
        continue;
      if (spec.with_tonal_answer && b >= cs_first_bar && b <= cs_last_bar)
        continue;
      if (spec.with_fortspinnung && b >= fs_first_bar && b <= fs_last_bar)
        continue;
      pushCounterlineBar(out.voice_plan, next_id, 0, b, subdivision);
    }
    if (spec.with_episode) {
      Span ep;
      ep.id = next_id++;
      ep.start_tick = static_cast<Tick>(episode_first_bar) * kTicksPerBar;
      ep.end_tick = static_cast<Tick>(spec.bars) * kTicksPerBar;
      ep.voice = 0;
      ep.intent = VoiceIntent::Episode;
      ep.subdivision = subdivision;
      out.voice_plan.spans.push_back(ep);
    }
    if (spec.with_tonal_answer) {
      Span cs;
      cs.id = next_id++;
      cs.start_tick = static_cast<Tick>(cs_first_bar) * kTicksPerBar;
      cs.end_tick = static_cast<Tick>(cs_last_bar + 1) * kTicksPerBar;
      cs.voice = 0;
      cs.intent = VoiceIntent::CountersubjectCarrier;
      cs.subdivision = subdivision;
      out.voice_plan.spans.push_back(cs);
    }
    if (spec.with_fortspinnung) {
      Span fs;
      fs.id = next_id++;
      fs.start_tick = static_cast<Tick>(fs_first_bar) * kTicksPerBar;
      fs.end_tick = static_cast<Tick>(fs_last_bar + 1) * kTicksPerBar;
      fs.voice = 0;
      fs.intent = VoiceIntent::FortspinnungSpan;
      fs.subdivision = subdivision;
      out.voice_plan.spans.push_back(fs);
    }
    for (int b = 0; b < subject_bars; ++b) {
      pushCounterlineBar(out.voice_plan, next_id, 1, b, subdivision);
    }
    Span answer_span;
    answer_span.id = next_id++;
    answer_span.start_tick = static_cast<Tick>(subject_bars) * kTicksPerBar;
    answer_span.end_tick = static_cast<Tick>(2 * subject_bars) * kTicksPerBar;
    answer_span.voice = 1;
    answer_span.intent = VoiceIntent::AnswerCarrier;
    out.voice_plan.spans.push_back(answer_span);
    for (int b = 2 * subject_bars; b < spec.bars; ++b) {
      pushCounterlineBar(out.voice_plan, next_id, 1, b, subdivision);
    }
    Span v2_subject;
    v2_subject.id = next_id++;
    v2_subject.start_tick = static_cast<Tick>(2 * subject_bars) * kTicksPerBar;
    v2_subject.end_tick = static_cast<Tick>(3 * subject_bars) * kTicksPerBar;
    v2_subject.voice = 2;
    v2_subject.intent = VoiceIntent::SubjectCarrier;
    out.voice_plan.spans.push_back(v2_subject);
    for (int b = 3 * subject_bars; b < spec.bars; ++b) {
      pushCounterlineBar(out.voice_plan, next_id, 2, b, subdivision);
    }
  } else if (spec.with_answer) {
    // Phase4Sus carves a 2-bar SuspensionCarrier span out of V0
    // counterline bars [subject_bars, subject_bars + 2). The remaining
    // V0 counterline bars run normally on either side. Phase4 (no
    // suspension) keeps all V0 counterline bars contiguous.
    const int sus_first_bar = spec.with_suspension ? subject_bars : -1;
    const int sus_last_bar = spec.with_suspension ? subject_bars + 1 : -1;
    for (int b = subject_bars; b < spec.bars; ++b) {
      if (spec.with_suspension && b >= sus_first_bar && b <= sus_last_bar)
        continue;
      pushCounterlineBar(out.voice_plan, next_id, 0, b, subdivision);
    }
    if (spec.with_suspension) {
      Span sus_span;
      sus_span.id = next_id++;
      sus_span.start_tick = static_cast<Tick>(sus_first_bar) * kTicksPerBar;
      sus_span.end_tick = static_cast<Tick>(sus_last_bar + 1) * kTicksPerBar;
      sus_span.voice = 0;
      sus_span.intent = VoiceIntent::SuspensionCarrier;
      out.voice_plan.spans.push_back(sus_span);
    }
    for (int b = 0; b < subject_bars; ++b) {
      pushCounterlineBar(out.voice_plan, next_id, 1, b, subdivision);
    }
    Span answer_span;
    answer_span.id = next_id++;
    answer_span.start_tick = static_cast<Tick>(subject_bars) * kTicksPerBar;
    answer_span.end_tick = static_cast<Tick>(spec.bars) * kTicksPerBar;
    answer_span.voice = 1;
    answer_span.intent = VoiceIntent::AnswerCarrier;
    out.voice_plan.spans.push_back(answer_span);
  } else {
    for (std::uint8_t v = 1; v < spec.voices; ++v) {
      for (int b = 0; b < spec.bars; ++b) {
        pushCounterlineBar(out.voice_plan, next_id, v, b, subdivision);
      }
    }
  }

  annotateLeadingToneMarkers(out.material, out.harmony.tonic_pc, out.harmony.is_minor);
  const Tick subject_cadence_tick = static_cast<Tick>(subject_bars) * kTicksPerBar - kTicksPerBeat;
  for (const auto& marker : out.material.leading_tone_markers) {
    if (marker.fragment != MaterialFragment::Subject)
      continue;
    if (marker.resolution_tick != subject_cadence_tick)
      continue;
    CadenceEvent cadence;
    cadence.tick = marker.resolution_tick;
    cadence.type = CadenceType::Perfect;
    out.harmony.cadences.push_back(cadence);
    if (marker.leading_tick >= kTicksPerBeat) {
      CadentialSixFour six_four;
      six_four.tick = marker.leading_tick - kTicksPerBeat;
      six_four.resolution_tick = marker.leading_tick;
      out.harmony.cadential_six_fours.push_back(six_four);
    }
  }
  annotateCadenceCells(out.material, out.harmony);

  if (spec.with_tonal_answer) {
    // Phase6Tonal: derive tonal_answer from the V0 subject (first 16 notes)
    // with a 4-note head mutation, anchor at bar `subject_bars`, and set
    // the dispatch flag so AnswerCarrier reads from tonal_answer instead
    // of `answer`. Bach's tonal-answer convention maps the subject's
    // tonic-degree head pitches to the dominant and vice versa.
    std::vector<MaterialNote> subj_head(out.material.subject.begin(),
                                        out.material.subject.begin() + 16);
    out.material.tonal_answer = tonal_answer::deriveTonalAnswer(
        subj_head, out.harmony.tonic_pc, static_cast<Tick>(subject_bars) * kTicksPerBar,
        /*head_length=*/4);
    out.material.use_tonal_answer = true;
    // Phase6Tonal CS material: stationary G5 (pitch 79) for 16 quarter
    // notes so V0 has a sounding note at every beat of the answer
    // window. The Validator's vertical/parallel rules skip both-Material
    // pairs (V0 CS vs V1 tonal_answer are both Material), so a pedal
    // pitch is safe regardless of the seed's tonal_answer head.
    for (int n = 0; n < 16; ++n) {
      MaterialNote cs;
      const int bar_in_block = n / 4;
      const int beat_in_bar = n % 4;
      cs.start_tick = static_cast<Tick>(subject_bars + bar_in_block) * kTicksPerBar +
                      static_cast<Tick>(beat_in_bar) * kTicksPerBeat;
      cs.duration = kTicksPerBeat;
      cs.pitch = 79;
      out.material.countersubject.push_back(cs);
    }
  }

  if (spec.with_episode) {
    // Phase6Episode injects one Episode fragment in V0 covering bars
    // [bars - subject_bars, bars). Transform = Original; source = the
    // first `subject_bars` of V0 SubjectCarrier material (indices
    // [0, 16)). Result re-anchors the subject pitches at the target
    // bar so the V0 line restates the subject in the closing bars —
    // a textbook Bach "subject-reentry-as-episode" recap.
    //
    // EpisodeMotifSourced bit on the emitted notes lets the closure
    // gate (4) confirm Episode derivation actually fired.
    EpisodeFragment ef;
    ef.transform = static_cast<std::uint8_t>(motif_ops::EpisodeMotifTransform::Original);
    ef.source_start_index = 0;
    ef.source_count = 16;  // first 4 bars × 4 beats
    ef.voice = 0;
    ef.target_start_tick = static_cast<Tick>(spec.bars - subject_bars) * kTicksPerBar;
    ef.invert_pivot = 72;
    ef.augment_factor = 2;
    ef.diminish_factor = 2;
    out.material.episodes.push_back(ef);
  }

  if (spec.with_suspension) {
    // One deterministic Sus7_6 in V0 spanning bars [subject_bars,
    // subject_bars + 1). Prep tied across the bar line so the
    // suspension lands on the bar-5 downbeat (strong beat = required
    // by the validator's isStrongBeat semantics). The prep/sus pitch
    // B5 (83) is pc 11, which lies in the consonant intersection of
    // every kSubjectPatterns' answer-V1 column at this tick (the
    // intersection of consonant pcs against V1 pitches across the
    // 5 patterns reduces to {pc 2, pc 11}; B5 is the higher of the two
    // and keeps V0 safely above V1's catalog maximum of 79). Step-down
    // resolution to A5 (81) on beat 2.
    SuspensionPattern sp;
    sp.type = SuspensionType::Sus7_6;
    sp.preparation_tick = static_cast<Tick>(subject_bars) * kTicksPerBar;
    sp.suspension_tick = static_cast<Tick>(subject_bars + 1) * kTicksPerBar;
    sp.resolution_tick = sp.suspension_tick + kTicksPerBeat;
    sp.preparation_pitch = 83;
    sp.suspension_pitch = 83;
    sp.resolution_pitch = 81;
    sp.voice = 0;
    out.material.suspension_patterns.push_back(sp);
  }

  if (spec.with_fortspinnung) {
    // Phase9 SequenceTemplate. Seed = 8-note motif over 2 bars (bars
    // 4-5) in V0. Pattern = AscendingStep (+2 semis per step).
    //
    // Pcs restricted to {0, 2, 7} (C, D, G) so the +2 transpose lands
    // inside C-major diatonic at step 1 (pcs {2, 4, 9}). A further
    // step would produce pc 6 (F#) → cross-relation, hence num_steps
    // is capped at 2. The 2-step pattern fills the full 4-bar V0
    // span (bars 4-7).
    //
    // Excluding pc 11 (B = leading tone in C major) from both step 0
    // and step 1 prevents `doubling_no_leading_tone` clashes with V1
    // AnswerCarrier idx 8 (= subject pattern idx 8 - P4), which for
    // catalog patterns 0 and 3 lands on B (pc 11) — and on harm
    // pattern (harm_a + 1) % 4 = 0, bar 6 chord = V which OWNS the
    // leading tone.
    //
    // Register: V0 must stay above V1 AnswerCarrier across all 5
    // subject patterns. V1 AnswerCarrier = subject pattern - 5
    // semitones; its max value at bars 4-7 is 79 (patterns 1 and 2
    // climb to 84 in idx 3 or idx 8 → 79 after -P4). Seed min = 79
    // (= unison with V1 max for pattern 1 idx 0 = 79); step 1 min =
    // 81. Unisons are not voice_crossing (interval ≥ 0).
    //
    // FortspinnungSpan placement = bars 4-7 sits directly after V0
    // SubjectCarrier (bars 0-3). Both spans are Material, so the
    // SubjectCarrier→FortspinnungSpan boundary has no Compose
    // mediation. The pitch jump 72 → 81 (subject_last → seed[0])
    // is a M6 leap inside Material, which the validator does not
    // analyze for melodic intervals (Material is verbatim).
    SequenceTemplate tmpl;
    tmpl.pattern = SequencePattern::AscendingStep;
    tmpl.target_start_tick = static_cast<Tick>(subject_bars) * kTicksPerBar;
    tmpl.step_length_ticks = 2 * kTicksPerBar;
    tmpl.num_steps = 2;
    tmpl.voice = 0;
    // Seed: G5 C6 D6 C6 G5 C6 D6 C6 — two-bar arpeggiated triad-tone
    // motif on the G-C-D pivot. Step 1 (+2): A5 D6 E6 D6 A5 D6 E6 D6.
    tmpl.seed_pitches = {79, 84, 86, 84, 79, 84, 86, 84};
    tmpl.seed_durations = {kTicksPerBeat, kTicksPerBeat, kTicksPerBeat, kTicksPerBeat,
                           kTicksPerBeat, kTicksPerBeat, kTicksPerBeat, kTicksPerBeat};
    out.material.sequence_templates.push_back(tmpl);
  }

  if (spec.with_imitation_entry) {
    // Phase9 ImitationEntry. Subject (V0) enters at bar 0; real answer
    // (V1) enters at bar `subject_bars` (= 4) with interval -5 semis
    // (real answer = P5 down = subject - 5). This matches the existing
    // Phase4+ harness convention; the declaration is purely documentary
    // so the Validator's imitation_entry_match rule fires the
    // ImitationEntryMatched bit on the entry note of both fragments.
    ImitationEntry entry;
    entry.leader_fragment = MaterialFragment::Subject;
    entry.follower_fragment = MaterialFragment::Answer;
    entry.distance_ticks = static_cast<Tick>(subject_bars) * kTicksPerBar;
    entry.interval_semis = -5;
    out.material.imitation_entries.push_back(entry);
  }

  if (spec.with_development) {
    // Phase11 development material (bars 12-27). Every fragment is the
    // seed's V0 subject pattern (kSubjectPatterns[subj_a]) under a fixed
    // pitch transform, so each device tracks the seed's exposition
    // subject. Anchored 4-bar (16 quarter-note) fragments; registers are
    // chosen so V0 stays above V2 at every shared tick (see voice plan).
    const auto& pat = kSubjectPatterns[subj_a];
    auto buildFragment = [&](int base_bar, auto transform) {
      std::vector<MaterialNote> v;
      v.reserve(16);
      for (int n = 0; n < 16; ++n) {
        MaterialNote mn;
        const int bar_in_block = n / 4;
        const int beat_in_bar = n % 4;
        mn.start_tick = static_cast<Tick>(base_bar + bar_in_block) * kTicksPerBar +
                        static_cast<Tick>(beat_in_bar) * kTicksPerBeat;
        mn.duration = kTicksPerBeat;
        mn.pitch = static_cast<std::uint8_t>(transform(static_cast<int>(pat[n])));
        v.push_back(mn);
      }
      return v;
    };

    // Middle entry: subject down a perfect fourth (-5) → the dominant key
    // (C major - P4 = G major). related_key_pc = 7 (= V of C). Range
    // 66-79 stays above the dominant pedal (G3 = 55). Bars 12-15, V0.
    MiddleEntryDecl middle;
    middle.voice = 0;
    middle.related_key_pc = 7;
    middle.notes = buildFragment(12, [](int p) { return p - 5; });
    out.material.middle_entries.push_back(middle);

    // Dominant pedal point: a single G3 (pc 7) held across bars 12-15 in
    // V2 (the bottom voice), under the middle entry.
    PedalPointDecl pedal;
    pedal.voice = 2;
    pedal.start_tick = static_cast<Tick>(12) * kTicksPerBar;
    pedal.duration = static_cast<Tick>(4) * kTicksPerBar;
    pedal.pitch = 55;
    pedal.is_dominant = true;
    out.material.pedal_points.push_back(pedal);

    // Subject variant: diminution — the subject at half duration (eighth
    // notes), so its 16 notes span 2 bars; played twice to fill bars
    // 16-19. Diminution preserves the subject's pitch sequence (and
    // register, 71-84), so it adds no awkward leaps the way inversion
    // would, and it connects smoothly to the verbatim subject leader at
    // bars 20-23. V0 is the sole sounding voice in this window.
    SubjectVariantDecl variant;
    variant.voice = 0;
    variant.transform = static_cast<std::uint8_t>(motif_ops::EpisodeMotifTransform::Diminish);
    for (int rep = 0; rep < 2; ++rep) {
      const Tick rep_start = static_cast<Tick>(16 + 2 * rep) * kTicksPerBar;
      for (int n = 0; n < 16; ++n) {
        MaterialNote mn;
        mn.start_tick = rep_start + static_cast<Tick>(n) * (kTicksPerBeat / 2);
        mn.duration = kTicksPerBeat / 2;
        mn.pitch = pat[n];
        variant.notes.push_back(mn);
      }
    }
    out.material.subject_variants.push_back(variant);

    // Stretto leader: the subject restated verbatim in V0 at bars 20-23.
    // Appended to material.subject so the V0 SubjectCarrier span at those
    // bars replays it (added after leading-tone / cadence annotation so it
    // gets no spurious cadence markers).
    {
      const auto leader = buildFragment(20, [](int p) { return p; });
      for (const auto& mn : leader)
        out.material.subject.push_back(mn);
    }

    // Stretto follower: subject down two octaves (-24). A 12-multiple
    // transpose preserves the subject's C-major pitch classes, so no Bb
    // is introduced to clash with the leader's B-natural (a -19 "twelfth"
    // would land the follower in F major and trip cross_relation). Range
    // 47-60 stays a clear margin below the leader (71-84), so the bars
    // 22-23 overlap never crosses voices for any subject pattern (the
    // worst early-high / late-low gap across the catalog is 13 < 24).
    // V2, bars 22-25; enters at bar 22 (strictly inside leader bars
    // 20-23).
    StrettoDecl stretto;
    stretto.leader_voice = 0;
    stretto.follower_voice = 2;
    stretto.leader_entry_tick = static_cast<Tick>(20) * kTicksPerBar;
    stretto.leader_length_ticks = static_cast<Tick>(4) * kTicksPerBar;
    stretto.follower_entry_tick = static_cast<Tick>(22) * kTicksPerBar;
    stretto.interval_semis = -24;
    stretto.follower_notes = buildFragment(22, [](int p) { return p - 24; });
    out.material.stretto_entries.push_back(stretto);

    // Coda: a stepwise C-major closing line (range 71-79) settling onto
    // the tonic, bars 24-27 in V0, above the stretto follower's tail.
    // Stepwise motion (no leap > 2 semitones) keeps the model's
    // unresolved-large-leap penalty off the closing phrase.
    CodaDecl coda;
    coda.voice = 0;
    static constexpr std::array<std::uint8_t, 16> kCoda = {79, 77, 76, 74, 72, 74, 76, 77,
                                                           76, 74, 72, 71, 72, 74, 72, 72};
    for (int n = 0; n < 16; ++n) {
      MaterialNote mn;
      const int bar_in_block = n / 4;
      const int beat_in_bar = n % 4;
      mn.start_tick = static_cast<Tick>(24 + bar_in_block) * kTicksPerBar +
                      static_cast<Tick>(beat_in_bar) * kTicksPerBeat;
      mn.duration = kTicksPerBeat;
      mn.pitch = kCoda[n];
      coda.notes.push_back(mn);
    }
    out.material.coda_extensions.push_back(coda);
  }

  if (spec.with_rhythm) {
    // Phase12 rhythm material (bars 12-27). Seed-independent, C-major,
    // register-safe (V0 stays above the V2 recurrence at bars 16-19). The
    // phrase grid is a regular 4-bar period (downbeats every 4 bars), with
    // a quarter-note anacrusis leading into bar 16.
    PhraseStructure& ps = out.material.phrase_structure;
    ps.has_anacrusis = true;
    ps.anacrusis_ticks = kTicksPerBeat;  // quarter-note upbeat
    for (int bar = 0; bar <= 24; bar += 4)
      ps.phrase_start_ticks.push_back(static_cast<Tick>(bar) * kTicksPerBar);

    auto addNote = [](std::vector<MaterialNote>& v, Tick t, Tick d, std::uint8_t p) {
      MaterialNote mn;
      mn.start_tick = t;
      mn.duration = d;
      mn.pitch = p;
      v.push_back(mn);
    };
    const Tick d8 = kTicksPerBeat / 2;   // eighth
    const Tick dq = kTicksPerBeat;       // quarter
    const Tick dd = kTicksPerBeat + d8;  // dotted quarter (720)
    const Tick dh = 2 * kTicksPerBeat;   // half
    const Tick ddh = 3 * kTicksPerBeat;  // dotted half (1440)
    auto bar = [](int b) { return static_cast<Tick>(b) * kTicksPerBar; };

    // Dotted figure (V0, bars 12-15): dotted-quarter + eighth + two
    // quarters per bar; bar 15 stops a beat early to leave room for the
    // anacrusis. First note is a phrase downbeat (PhrasePeriodicityKept).
    RhythmFragment dotted;
    dotted.feature = RhythmFragment::Feature::Dotted;
    dotted.voice = 0;
    {
      const std::array<std::uint8_t, 15> p = {72, 74, 76, 77, 79, 77, 76, 74,
                                              76, 77, 79, 77, 76, 74, 72};
      const std::array<Tick, 15> d = {dd, d8, dq, dq, dd, d8, dq, dq, dd, d8, dq, dq, dd, d8, dq};
      Tick t = bar(12);
      for (std::size_t i = 0; i < p.size(); ++i) {
        addNote(dotted.notes, t, d[i], p[i]);
        t += d[i];
      }
    }
    out.material.rhythm_fragments.push_back(dotted);

    // Anacrusis (V0): a quarter-note pickup (B4 = leading tone) on bar 15
    // beat 4, resolving up into the bar-16 downbeat. Starts exactly
    // anacrusis_ticks before the bar-16 phrase start.
    RhythmFragment anac;
    anac.feature = RhythmFragment::Feature::Anacrusis;
    anac.voice = 0;
    addNote(anac.notes, bar(16) - dq, dq, 71);
    out.material.rhythm_fragments.push_back(anac);

    // Syncopation (V0, bars 16-19, consequent phrase): off-beat onsets
    // (eighth, quarter, quarter, quarter, eighth per bar = onsets on the
    // 1.5 / 2.5 / 3.5 beats). First note is a phrase downbeat.
    RhythmFragment sync;
    sync.feature = RhythmFragment::Feature::Syncopation;
    sync.voice = 0;
    {
      const std::array<std::array<std::uint8_t, 5>, 4> rows = {
          {{72, 74, 76, 74, 72}, {74, 76, 77, 76, 74}, {76, 77, 79, 77, 76}, {74, 76, 74, 72, 71}}};
      const std::array<Tick, 5> d = {d8, dq, dq, dq, d8};
      for (int b = 0; b < 4; ++b) {
        Tick t = bar(16 + b);
        for (int i = 0; i < 5; ++i) {
          addNote(sync.notes, t, d[i],
                  rows[static_cast<std::size_t>(b)][static_cast<std::size_t>(i)]);
          t += d[i];
        }
      }
    }
    out.material.rhythm_fragments.push_back(sync);

    // Antecedent of the 20-23 phrase (V0, bars 20-21): plain quarters; the
    // first note is the bar-20 phrase downbeat (PhrasePeriodicityKept).
    RhythmFragment phrase20;
    phrase20.feature = RhythmFragment::Feature::Dotted;
    phrase20.voice = 0;
    {
      const std::array<std::uint8_t, 8> p = {72, 74, 76, 77, 76, 74, 72, 74};
      Tick t = bar(20);
      for (auto pitch : p) {
        addNote(phrase20.notes, t, dq, pitch);
        t += dq;
      }
    }
    out.material.rhythm_fragments.push_back(phrase20);

    // Hemiola (V0, bars 22-23, cadence approach): two dotted-half notes
    // plus a half note (3 + 3 + 2 beats) cut across the 4+4 barline grid,
    // a 3-against-2 regrouping. All notes carry HemiolaInserted.
    RhythmFragment hemiola;
    hemiola.feature = RhythmFragment::Feature::Hemiola;
    hemiola.voice = 0;
    {
      Tick t = bar(22);
      addNote(hemiola.notes, t, ddh, 76);
      t += ddh;
      addNote(hemiola.notes, t, ddh, 74);
      t += ddh;
      addNote(hemiola.notes, t, dh, 72);
    }
    out.material.rhythm_fragments.push_back(hemiola);

    // Closing phrase (V0, bars 24-27): stepwise descent broadening to the
    // final tonic. First note is the bar-24 phrase downbeat.
    RhythmFragment closing;
    closing.feature = RhythmFragment::Feature::Dotted;
    closing.voice = 0;
    {
      Tick t = bar(24);
      const std::array<std::uint8_t, 8> q = {77, 76, 74, 72, 74, 72, 71, 72};
      for (auto pitch : q) {
        addNote(closing.notes, t, dq, pitch);
        t += dq;
      }
      addNote(closing.notes, bar(26), dh, 71);
      addNote(closing.notes, bar(26) + dh, dh, 72);
      addNote(closing.notes, bar(27), 2 * dh, 72);  // whole-note final tonic
    }
    out.material.rhythm_fragments.push_back(closing);

    // Rhythmic-motif recurrence (V2, bars 16-19): the dotted figure's
    // rhythm restated an octave-and-a-bit lower (range 60-67), under the
    // syncopated consequent. RhythmicMotifRecurrence bit.
    RhythmFragment recur;
    recur.feature = RhythmFragment::Feature::Recurrence;
    recur.voice = 2;
    {
      const std::array<std::uint8_t, 15> p = {60, 62, 64, 65, 67, 65, 64, 62,
                                              64, 65, 67, 65, 64, 62, 60};
      const std::array<Tick, 15> d = {dd, d8, dq, dq, dd, d8, dq, dq, dd, d8, dq, dq, dd, d8, dq};
      Tick t = bar(16);
      for (std::size_t i = 0; i < p.size(); ++i) {
        addNote(recur.notes, t, d[i], p[i]);
        t += d[i];
      }
    }
    out.material.rhythm_fragments.push_back(recur);
  }

  return out;
}

}  // namespace bach::composer
