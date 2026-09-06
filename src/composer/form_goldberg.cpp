#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <vector>

#include "composer/arc.h"
#include "composer/arrival_relief.h"
#include "composer/bar_material.h"
#include "composer/character_profile.h"
#include "composer/figuration.h"
#include "composer/figuration_palette.h"
#include "composer/form_builders.h"
#include "composer/material.h"
#include "composer/minor_material.h"
#include "composer/motif_ops.h"
#include "composer/span.h"
#include "composer/texture_helpers.h"
#include "composer/voice_intent.h"
#include "core/basic_types.h"
#include "core/pitch_utils.h"

namespace bach::composer {

// ---------------------------------------------------------------------------
// The Goldberg-style immutable-bass variation set.
//
// Honours ResolvedRequest length / mode / character / arc. It reuses the proven
// GoldbergVariations (goldberg) note language -- an immutable tiled ground under
// rising-density scalar-wave variations -- but generalises the layout to any
// snapped bar count, derives the per-variation design from (seed, indices) only,
// and selects the ground from the mode.
// ---------------------------------------------------------------------------

// --- Goldberg variation framework ------------------------------------------

GoldbergVariationKind goldbergVariationKind(std::size_t variation_index) {
  // BWV988 scheme: every third variation is a canon (variations 3, 6, ..., 27),
  // at a rising imitation interval (unison, 2nd, 3rd, ... 9th). `variation_index`
  // is zero-based, so the 1-based variation number is variation_index + 1; a
  // canon is variation number v where v % 3 == 0. The final variation of the
  // full 30-variation set (v == 30) is NOT a canon: it is the densest figuration
  // peak (the BWV988 "Quodlibet" slot), so the canon rule is capped at v < 30.
  const std::size_t variation_number = variation_index + 1;
  if (variation_number == 30)
    return GoldbergVariationKind::Quodlibet;
  if (variation_number % 3 == 0 && variation_number < 30)
    return GoldbergVariationKind::Canon;
  return GoldbergVariationKind::Figuration;
}

namespace {

using detail::Mode;

// The Goldberg ground tables live in kGoldbergGroundsMajor /
// kGoldbergGroundsMinor (seed-selected design variants, period 4 bars, all
// tones C2-region chord roots so the variation downbeat anchoring stays
// consonant and the ground tiles exactly).

// Goldberg figuration palette (design table): the pattern idiom each
// non-climax figuration variation block takes, rotated by (seed +
// variation_index) so consecutive variations alternate idioms. The climax
// block and variation 30 are design values (the densest anchored scalar wave)
// and bypass the rotation; aria and canon blocks have their own layouts.
// No kArpeggio here: broken-chord blocks raise the melodic-interval cost (the
// dominant scorer feature) and the goldberg sits close to the model
// threshold, so its rotation keeps to the stepwise idioms.
constexpr PatternKind kGoldbergPalette[2] = {PatternKind::kScalarWave, PatternKind::kFiguraCorta};

// Per-bar chord for the ground cycle: the chord root IS the ground tone's
// pitch class, with the diatonic triad quality on that degree (in minor the V
// is the harmonic-minor major dominant), so the harmony stays consonant with
// the bass for every ground variant.
BarChord goldbergBarChord(std::uint8_t ground_pitch, Mode mode) {
  const std::uint8_t pc = static_cast<std::uint8_t>(ground_pitch % 12u);
  return {pc, detail::diatonicTriadMinor(pc, mode == Mode::Minor)};
}

// The eight eighth-note positions of one aria-bass bar. Each bar articulates
// its root, third and fifth and returns to the root on both structural accents,
// so the bass does not merely hold the bar's root: it states the bar chord
// within the bar. Any voice above that figures the same triad therefore meets
// it on a perfect interval off the downbeat as readily as on it, which is why
// this shape is stated once and read by everything that has to answer for it.
//
// The first half connects the root to its third and back through the scale
// degree between them, the way a continuo bass walks; the second half states
// the triad downwards from the fifth into the next bar's root. A bar built
// only from root, third and fifth moves by a third or a fifth at every one of
// its eight positions, which is what makes a chain of same-direction thirds the
// bass's whole vocabulary -- the melodic-interval cost the figuration above is
// already written to keep down. Positions one and three carry the fill: both
// are metrically weak, and both are approached and left by step, so the tone is
// a passing/neighbour tone rather than an accented dissonance.
//
// `fill_thirds` is settled once for the whole piece from the ground itself, so
// every reader of this shape sees the same bass.
constexpr Tick kAriaBassUnit = kTicksPerBeat / 2;
std::array<int, 8> goldbergAriaBassBar(int root, Mode mode, bool fill_thirds) {
  const BarChord chord = goldbergBarChord(static_cast<std::uint8_t>(root), mode);
  const int third = root + (chord.minor ? 3 : 4);
  const int fifth = root + 7;
  const std::array<int, 8> plain = {root, third, fifth, third, root, fifth, third, root};
  if (!fill_thirds)
    return plain;
  // The scale degree between root and third, read under the bar's own harmony:
  // over a minor-key dominant that is the raised sixth, which is the only tone
  // that reaches the major third by step instead of by an augmented second.
  const detail::ChordSpec spec{chord.root_pc, chord.minor};
  const int fill = detail::melodicScaleStep(root, /*direction=*/1, mode, &spec);
  // A ground tone whose third the scale cannot reach by a single step keeps the
  // plain triad statement rather than inventing a chromatic passing tone.
  if (fill <= root || fill >= third)
    return plain;
  return {root, fill, third, fill, root, fifth, third, root};
}

// Diatonic transpose a pitch UP by `degrees` scale steps (degrees may be 0 =
// unison). Octave membership is preserved because scaleUp walks the scale.
int transposeUp(int pitch, int degrees, Mode mode) {
  return degrees <= 0 ? pitch : detail::scaleUp(pitch, degrees, mode);
}

// Append an aria bar (the m=2 two-half-notes SPECIAL layout from GoldbergVariations): a
// half note on the wave start, then a half note on the NEXT CHORD TONE up. Both
// half notes are chord tones of the bar's triad, so each sampled beat (the bar's
// two half-note onsets) is consonant against the ground tone below (itself a
// chord root). Used for the opening aria and (when N >= 24) the da-capo
// restatement.
void appendAriaBar(PassacagliaVariation& var, int bar, const BarChord& chord, Mode mode,
                   int register_base, int offset) {
  const int start =
      snapUpToChordTone(detail::scaleUp(register_base, offset, mode), chord.root_pc, chord.minor);
  const Tick base = barTick(bar);
  var.notes.push_back(materialNote(base, kHalf, start));
  var.notes.push_back(
      materialNote(base + kHalf, kHalf, chordToneAbove(start, chord.root_pc, chord.minor)));
}

// Snapped goldberg figuration start with the keyboard ceiling applied. The
// figure built on the start (the contour ladder, the figura corta cell) rises
// up to a ninth above it, so a start above MIDI 77 would carry the line past
// d''' (MIDI 86), the top of the Bach keyboard compass. Walk such starts down
// to the highest chord tone at or under the cap -- the arc's climax register
// lift compresses against the instrument ceiling, exactly as the real
// keyboard writing does.
int goldbergFigurationStart(int register_base, int offset, const BarChord& chord, Mode mode) {
  int snapped =
      snapUpToChordTone(detail::scaleUp(register_base, offset, mode), chord.root_pc, chord.minor);
  constexpr int kFigStartCeiling = 77;
  if (snapped > kFigStartCeiling) {
    const int third = chord.minor ? 3 : 4;
    const int triad_pc[3] = {chord.root_pc % 12, (chord.root_pc + third) % 12,
                             (chord.root_pc + 7) % 12};
    auto is_triad = [&](int midi) {
      const int pcl = ((midi % 12) + 12) % 12;
      return pcl == triad_pc[0] || pcl == triad_pc[1] || pcl == triad_pc[2];
    };
    int capped = kFigStartCeiling;
    while (capped > 0 && !is_triad(capped))
      --capped;
    snapped = capped;
  }
  return snapped;
}

// Append a figuration variation bar: a downbeat-anchored scalar wave with
// `notes_per_beat` subdivision (1 = quarters, 2 = eighths, 4 = sixteenths).
void appendVariationBar(PassacagliaVariation& var, int bar, const BarChord& chord, Mode mode,
                        int notes_per_beat, int register_base, int offset) {
  const int snapped = goldbergFigurationStart(register_base, offset, chord, mode);
  emitAnchoredBar(bar, snapped, chord, mode, notes_per_beat, [&](Tick start, Tick dur, int pitch) {
    var.notes.push_back(materialNote(start, dur, pitch));
  });
}

// Canon register anchor. Narrow canons place the dux in this upper band and
// imitate below; canons at the fifth and wider place the dux one octave lower
// and imitate above, matching the rising-interval layout without crossing the
// physical V0/V1 register order.
constexpr int kCanonLeaderBase = 72;  // C5: aligned with the figuration band.

// The leader tones a canon bar may be built on: chord tones of that bar's
// ground chord, taken downwards from the ceiling of the leader band.
//
// The band is one octave deep, which is the shallowest depth that always holds
// all three tones of a triad -- a narrower one leaves bars with a single
// candidate, and a bar with no choice cannot answer for what it collides with.
// Depth is what the band is for; the register order it used to guarantee by
// being narrow is now read off the assembled block instead, where it is a
// measured fact rather than a bound taken on trust.
//
// Always three per bar. Where the band holds fewer the last is repeated, so a
// choice over this set is always defined and never has to test for emptiness.
std::array<std::array<int, 3>, 4> canonLeaderCandidates(int pitch_ceiling, Mode mode,
                                                        const std::array<std::uint8_t, 4>& ground) {
  const int pitch_floor = pitch_ceiling - 11;
  std::array<std::array<int, 3>, 4> tones{};
  for (int bar = 0; bar < 4; ++bar) {
    const BarChord chord = goldbergBarChord(ground[static_cast<std::size_t>(bar)], mode);
    const int third = chord.minor ? 3 : 4;
    const std::array<int, 3> chord_pcs = {static_cast<int>(chord.root_pc),
                                          (static_cast<int>(chord.root_pc) + third) % 12,
                                          (static_cast<int>(chord.root_pc) + 7) % 12};
    int count = 0;
    for (int pitch = pitch_ceiling; pitch >= pitch_floor && count < 3; --pitch) {
      const int pc = pitch % 12;
      if (pc == chord_pcs[0] || pc == chord_pcs[1] || pc == chord_pcs[2])
        tones[static_cast<std::size_t>(bar)][static_cast<std::size_t>(count++)] = pitch;
    }
    // An octave-deep band cannot miss a chord tone, but fail closed to the
    // snapped one rather than to silence if a future ground table ever does.
    if (count == 0) {
      int fallback = snapUpToChordTone(pitch_floor, chord.root_pc, chord.minor);
      while (fallback > pitch_ceiling)
        fallback -= 12;
      tones[static_cast<std::size_t>(bar)][0] = fallback;
      count = 1;
    }
    while (count < 3) {
      tones[static_cast<std::size_t>(bar)][static_cast<std::size_t>(count)] =
          tones[static_cast<std::size_t>(bar)][static_cast<std::size_t>(count - 1)];
      ++count;
    }
  }
  return tones;
}

// The dux cell: a six-note soggetto with an eighth-eighth-quarter rhythm in each
// half-bar, its chord-tone beat onsets connected by contrary neighbours.
std::vector<MaterialNote> canonSoggetto(int tone, bool rising, Mode mode) {
  const int first_neighbour =
      rising ? detail::scaleUp(tone, 1, mode) : detail::scaleDown(tone, 1, mode);
  const int second_neighbour =
      rising ? detail::scaleDown(tone, 1, mode) : detail::scaleUp(tone, 1, mode);
  std::vector<MaterialNote> soggetto;
  soggetto.reserve(6);
  soggetto.push_back(materialNote(0, kEighth, tone));
  soggetto.push_back(materialNote(kEighth, kEighth, first_neighbour));
  soggetto.push_back(materialNote(kTicksPerBeat, kTicksPerBeat, tone));
  soggetto.push_back(materialNote(kHalf, kEighth, tone));
  soggetto.push_back(materialNote(kHalf + kEighth, kEighth, second_neighbour));
  soggetto.push_back(materialNote(kHalf + kTicksPerBeat, kTicksPerBeat, tone));
  return soggetto;
}

// The two lines of one canon block, before either is assigned to a voice.
struct CanonLines {
  std::vector<MaterialNote> dux;
  std::vector<MaterialNote> comes;
};

// Lay out a canon block from a leader-tone assignment. The dux states the
// soggetto once per bar, re-anchored to the same source cell; the comes is an
// exact constant-semitone copy of it, delayed one bar and truncated at the
// block end.
// The soggetto's neighbours alternate direction bar by bar, which is the shape
// the cell is designed to have. `rising` carries that default in and lets a
// caller depart from it where the default is what a fault is made of.
constexpr std::array<bool, 4> kCanonSoggettoParity = {{true, false, true, false}};

CanonLines layOutCanon(const std::array<int, 4>& designed, int block_start_bar,
                       int source_register_shift, int comes_shift, Mode mode,
                       const std::array<bool, 4>& rising) {
  CanonLines lines;
  lines.dux.reserve(24);
  for (int local = 0; local < 4; ++local) {
    const int bar = block_start_bar + local;
    const int tone = designed[static_cast<std::size_t>(bar % 4)] + source_register_shift;
    const auto source = canonSoggetto(tone, rising[static_cast<std::size_t>(local)], mode);
    auto anchored = motif_ops::reanchorMelody(source, barTick(bar));
    lines.dux.insert(lines.dux.end(), anchored.begin(), anchored.end());
  }
  lines.comes.reserve(18);
  const Tick block_end = barTick(block_start_bar + 4);
  for (const auto& note : lines.dux) {
    const Tick delayed = note.start_tick + kTicksPerBar;
    if (delayed >= block_end)
      continue;
    MaterialNote copy = note;
    copy.start_tick = delayed;
    copy.pitch = static_cast<std::uint8_t>(static_cast<int>(copy.pitch) + comes_shift);
    lines.comes.push_back(copy);
  }
  return lines;
}

// Read a laid-out four-bar block against the aria bass it will sound over.
//
// A block whose upper voices are settled here is a closed system. The aria bass
// repeats on exactly the four-bar period the block spans, it is immutable by
// contract, and the relief pass that answers for the free figuration elsewhere
// deliberately skips the imitative blocks -- the canon pair cannot be re-aimed
// one end at a time without dissolving the imitation. So the whole three-voice
// surface follows from the choices made here and can be read before a note is
// committed, at the grain an external reading pairs the voices at: every onset
// of any voice, against whatever the others are sounding then.
//
// Reported worst first, so the array compares as a preference order: a crossing
// or a unison breaks the register order the form is built on, a true parallel is the fault
// the ear names, then the weaker perfect approaches, then the dissonant
// simultaneities. A caller with its own terms to weigh interleaves them.
std::array<int, 6> goldbergBlockFaults(const std::vector<MaterialNote>& upper,
                                       const std::vector<MaterialNote>& inner, int block_start_bar,
                                       const std::array<std::uint8_t, 4>& ground, Mode mode,
                                       bool fill_thirds) {
  std::vector<MaterialNote> bass;
  bass.reserve(32);
  for (int local = 0; local < 4; ++local) {
    const int bar = block_start_bar + local;
    const std::array<int, 8> phrase =
        goldbergAriaBassBar(ground[static_cast<std::size_t>(bar % 4)], mode, fill_thirds);
    for (std::size_t pos = 0; pos < phrase.size(); ++pos) {
      bass.push_back(materialNote(barTick(bar) + static_cast<Tick>(pos) * kAriaBassUnit,
                                  kAriaBassUnit, phrase[pos]));
    }
  }
  // Register order, highest first, matching the physical voice indices.
  const std::vector<MaterialNote>* voices[3] = {&upper, &inner, &bass};

  std::vector<Tick> onsets;
  for (const std::vector<MaterialNote>* voice : voices) {
    for (const MaterialNote& note : *voice)
      onsets.push_back(note.start_tick);
  }
  std::sort(onsets.begin(), onsets.end());
  onsets.erase(std::unique(onsets.begin(), onsets.end()), onsets.end());

  std::array<int, 6> score{};
  std::array<int, 3> prev = {-1, -1, -1};
  for (const Tick tick : onsets) {
    std::array<int, 3> curr = {-1, -1, -1};
    for (std::size_t idx = 0; idx < 3; ++idx) {
      for (const MaterialNote& note : *voices[idx]) {
        if (note.start_tick <= tick && tick < note.start_tick + note.duration) {
          curr[idx] = static_cast<int>(note.pitch);
          break;
        }
      }
    }
    for (std::size_t above = 0; above < 3; ++above) {
      for (std::size_t below = above + 1; below < 3; ++below) {
        if (curr[above] < 0 || curr[below] < 0)
          continue;
        // A crossing and a meeting are counted apart, and the two kinds of
        // meeting apart from each other. Only the crossing is a rule: the
        // validator reads the register order off the notes and fails a piece
        // that inverts it. A unison is not -- it costs a pair its audible
        // independence for one onset -- but it is not one thing either. Touching
        // the immutable ground removes the independence of the voice the whole
        // form is built over; the two upper voices touching is what a canon pair
        // does when its lines cross paths. Pooled with the crossing, either
        // blemish inherits a rule's weight and the design ships a true parallel
        // octave rather than allow it: the cardinal prohibition paid for a
        // matter of taste.
        if (curr[above] < curr[below])
          ++score[0];
        else if (curr[above] == curr[below])
          ++(below == 2 ? score[2] : score[3]);
        if (!isConsonantPair(curr[above], curr[below]))
          ++score[5];
        if (prev[above] < 0 || prev[below] < 0)
          continue;
        if (formsStrictPerfectParallel(prev[above], curr[above], prev[below], curr[below]))
          ++score[1];
        else if (formsPerfectParallel(prev[above], curr[above], prev[below], curr[below]) ||
                 formsAntiParallelPerfect(prev[above], curr[above], prev[below], curr[below]) ||
                 formsBattuta(prev[above], curr[above], prev[below], curr[below]))
          ++score[4];
      }
    }
    prev = curr;
  }
  return score;
}

// The tones and the cell shape a canon block is assembled from.
struct CanonDesign {
  std::array<int, 4> assignment{};
  std::array<bool, 4> rising = kCanonSoggettoParity;
  // Whether the assembly kept is free of both a crossing and a true parallel.
  // A block is built from the best assembly whether or not one was reachable,
  // so this is what a caller asking "is this bass writable at all" reads.
  bool clean = false;
};

// Choose the leader tones for a canon block, and where they cannot answer on
// their own, the direction of the soggetto's neighbours as well.
//
// Exhaustive over the candidate set rather than a left-to-right walk. A bar's
// tone answers for two bars at once -- it is the dux in its own bar and the
// comes in the next -- so a per-bar cost cannot be settled before the neighbour
// it will be echoed against is known. Three tones in each of four bars is a
// small enough set to read every assembly of it exactly.
//
// The tones alone answer for every block but the widest canons, where the comes
// is pinned under the top of the keyboard and the dux above the arpeggiating
// bass and the band holds one representative of each chord tone -- no slack
// anywhere in it. Only there is the cell's own alternating shape opened as well,
// and only after the tones are proved unable to clear a crossing and a true
// parallel together. The alternation is a design value, so a departure from it
// is scored and minimised: a block that never needed one keeps it exactly.
CanonDesign designCanonBlock(int pitch_ceiling, Mode mode,
                             const std::array<std::uint8_t, 4>& ground, int source_register_shift,
                             int comes_shift, bool imitate_above, bool fill_thirds) {
  const std::array<std::array<int, 3>, 4> candidates =
      canonLeaderCandidates(pitch_ceiling, mode, ground);
  CanonDesign chosen;
  std::array<int, 9> best{};
  bool have_best = false;
  bool clean = false;
  // Pass 0 holds the cell's designed alternation; pass 1 opens it. The second
  // pass runs only when the first cannot come back clean.
  for (int pass = 0; pass < 2 && !clean; ++pass) {
    const std::size_t shapes = pass == 0 ? 1u : 16u;
    std::array<std::size_t, 4> pick{};
    for (pick[0] = 0; pick[0] < 3; ++pick[0]) {
      for (pick[1] = 0; pick[1] < 3; ++pick[1]) {
        for (pick[2] = 0; pick[2] < 3; ++pick[2]) {
          for (pick[3] = 0; pick[3] < 3; ++pick[3]) {
            std::array<int, 4> assignment{};
            for (std::size_t bar = 0; bar < 4; ++bar)
              assignment[bar] = candidates[bar][pick[bar]];
            for (std::size_t shape = 0; shape < shapes; ++shape) {
              std::array<bool, 4> rising = kCanonSoggettoParity;
              int shape_deviation = 0;
              if (pass == 1) {
                for (std::size_t bar = 0; bar < 4; ++bar) {
                  rising[bar] = ((shape >> bar) & 1u) != 0u;
                  if (rising[bar] != kCanonSoggettoParity[bar])
                    ++shape_deviation;
                }
              }
              const CanonLines lines =
                  layOutCanon(assignment, /*block_start_bar=*/0, source_register_shift, comes_shift,
                              mode, rising);
              const std::array<int, 6> faults = goldbergBlockFaults(
                  imitate_above ? lines.comes : lines.dux, imitate_above ? lines.dux : lines.comes,
                  /*block_start_bar=*/0, ground, mode, fill_thirds);
              // The leader's own bar-to-bar steps, which the comes inherits
              // exactly: a tritone or a seventh between adjacent bars is
              // unsingable however well it behaves against the other voices, so
              // it is weighed above the faults that only the combination
              // produces, and total travel breaks ties last so the contour walks
              // rather than leaps.
              int unsingable = 0;
              int travel = 0;
              for (std::size_t bar = 1; bar < 4; ++bar) {
                const int step = std::abs(assignment[bar] - assignment[bar - 1]);
                if (step == interval::kTritone || step >= interval::kMinor7th)
                  ++unsingable;
                travel += step;
              }
              // A crossing is a rule and comes first, a true parallel is the
              // cardinal prohibition and comes next, and the leader's
              // singability follows because the comes inherits every step of it.
              // Only then a voice touching the ground, the upper pair touching
              // each other, the weaker perfect approaches, the dissonance, the
              // departure from the cell's designed shape and the distance
              // travelled: preferences in descending weight, and none of them
              // worth a parallel.
              const std::array<int, 9> score = {faults[0], faults[1],       unsingable,
                                                faults[2], faults[3],       faults[4],
                                                faults[5], shape_deviation, travel};
              if (!have_best || score < best) {
                best = score;
                chosen.assignment = assignment;
                chosen.rising = rising;
                have_best = true;
              }
              clean = clean || (faults[0] == 0 && faults[1] == 0);
            }
          }
        }
      }
    }
  }
  // A clean assembly, where one exists, is always the one kept: the crossing
  // and the true parallel are the first two terms of the score.
  chosen.clean = clean;
  return chosen;
}

// Everything about a canon block that follows from its imitation interval
// alone. Derived in one place so the probe that asks whether a ground can be
// written in canon at all and the builder that writes it read the same layout.
//
// Unison through fourth canons imitate below: physical V0 is the dux and V1 the
// comes. Fifth and wider canons imitate above: physical V1 is the lower dux and
// V0 the comes. This preserves the validator's V0 > V1 register convention
// while making the wide canon's musical direction genuinely upward.
struct CanonLayout {
  bool imitate_above = false;
  int source_register_shift = 0;
  int comes_shift = 0;
  int design_ceiling = 0;
};

CanonLayout canonLayout(int imitation_degrees, Mode mode) {
  const int imitation_semitones =
      transposeUp(kCanonLeaderBase, imitation_degrees, mode) - kCanonLeaderBase;
  CanonLayout layout;
  layout.imitate_above = imitation_degrees >= 4;
  layout.source_register_shift = layout.imitate_above ? -12 : 12;
  layout.comes_shift = layout.imitate_above ? imitation_semitones + 12 : imitation_semitones - 24;
  layout.design_ceiling = imitation_degrees >= 8 ? 70 : 72;
  return layout;
}

// The imitation intervals the form can reach: canon number c = variation
// number / 3 runs 1..9 across the full BWV988 set, and the interval is c - 1
// diatonic degrees above the unison.
constexpr int kCanonImitationDegreeCount = 9;

// Whether the walking aria bass leaves every canon interval writable. The
// filled bass moves by step where the plain triad statement leapt, which gives
// a canon's two voices one more chance to meet a perfect interval in parallel;
// a ground that leaves some imitation interval with no assignment clearing both
// a crossing and a true parallel takes the plain bass instead. The answer
// follows from the ground alone, so it is settled once and the bass and every
// reader of it agree for the whole piece.
bool ariaBassFillAdmitsEveryCanon(Mode mode, const std::array<std::uint8_t, 4>& ground) {
  for (int imitation_degrees = 0; imitation_degrees < kCanonImitationDegreeCount;
       ++imitation_degrees) {
    const CanonLayout layout = canonLayout(imitation_degrees, mode);
    const CanonDesign probe =
        designCanonBlock(layout.design_ceiling, mode, ground, layout.source_register_shift,
                         layout.comes_shift, layout.imitate_above, /*fill_thirds=*/true);
    if (!probe.clean)
      return false;
  }
  return true;
}

// Build one canonic variation block (a 4-bar window).
void buildCanonBlock(PassacagliaVariation& principal, std::vector<MaterialNote>& inner_notes,
                     int block_start_bar, int imitation_degrees, Mode mode,
                     const std::array<std::uint8_t, 4>& ground, bool fill_thirds) {
  const CanonLayout layout = canonLayout(imitation_degrees, mode);
  const bool imitate_above = layout.imitate_above;
  const int source_register_shift = layout.source_register_shift;
  const int comes_shift = layout.comes_shift;
  const CanonDesign designed =
      designCanonBlock(layout.design_ceiling, mode, ground, source_register_shift, comes_shift,
                       imitate_above, fill_thirds);
  const CanonLines lines = layOutCanon(designed.assignment, block_start_bar, source_register_shift,
                                       comes_shift, mode, designed.rising);

  if (imitate_above) {
    principal.notes.insert(principal.notes.end(), lines.comes.begin(), lines.comes.end());
    inner_notes.insert(inner_notes.end(), lines.dux.begin(), lines.dux.end());
  } else {
    principal.notes.insert(principal.notes.end(), lines.dux.begin(), lines.dux.end());
    inner_notes.insert(inner_notes.end(), lines.comes.begin(), lines.comes.end());
  }
}

}  // namespace

HarnessFixture buildGoldbergVariationsForm(const ResolvedRequest& req) {
  HarnessFixture out;
  constexpr int kCycleBars = 4;
  const int bars = static_cast<int>(req.bars);
  const Mode mode = req.mode;
  const int offset = static_cast<int>(req.seed % 4u);
  const detail::CharacterProfile& profile = detail::characterProfile(req.character);

  // Block layout: block 0 (bars 0-3) is the aria; blocks 1..K are 4-bar
  // figuration variations; when N >= 24 the final block restates the aria
  // (da capo). K is derived from the bar count so the variation COUNT is
  // length-driven.
  const int num_blocks = bars / kCycleBars;  // bars is snapped to a multiple of 4.
  const bool da_capo = bars >= 24;
  const int da_capo_block = da_capo ? num_blocks - 1 : -1;

  // Dedicated compressed aria-bass phrase: 32 structural tones across four
  // bars (eight eighth-note positions per bar). Each bar articulates its root,
  // third, and fifth while returning to the root on both structural accents.
  // The complete phrase is repeated unchanged through every variation and da
  // capo; it is not represented as Passacaglia material.
  const std::size_t ground_variant = detail::groundVariantIndex(req.seed);
  const auto& ground = (mode == Mode::Major) ? detail::kGoldbergGroundsMajor[ground_variant]
                                             : detail::kGoldbergGroundsMinor[ground_variant];
  const bool fill_thirds = ariaBassFillAdmitsEveryCanon(mode, ground);
  for (int bar = 0; bar < kCycleBars; ++bar) {
    const std::array<int, 8> phrase =
        goldbergAriaBassBar(ground[static_cast<std::size_t>(bar)], mode, fill_thirds);
    for (std::size_t pos = 0; pos < phrase.size(); ++pos) {
      out.material.goldberg_aria_bass.push_back(materialNote(
          barTick(bar) + static_cast<Tick>(pos) * kAriaBassUnit, kAriaBassUnit, phrase[pos]));
    }
  }
  out.material.goldberg_aria_bass_period = static_cast<Tick>(kCycleBars) * kTicksPerBar;

  // Canon follower line (V1). Populated only for canonic variation blocks; the
  // follower notes for every canon block are appended here in time order (one
  // block at a time), then handed to a single TrioVoiceLine on V1. V1 is silent
  // outside canon blocks (no notes), keeping the texture clean and the validator
  // quiet for the figuration / aria blocks. The follower carries the
  // TrioVoiceIndependent bit, but because it is the ONLY voice carrying that bit
  // the voice_independence_threshold rule stays inert (it needs >= 2 such
  // voices), so no soft-fail is introduced.
  std::vector<MaterialNote> inner_voice;
  std::vector<int> inner_blocks;
  // The canon blocks alone, whose variation line may not be re-aimed.
  std::vector<int> canon_blocks;

  // Per-bar harmony (ground cycle, tiled). Drives the variation downbeat anchor.
  // The final bar is an explicit tonic arrival rather than one more aria-bass
  // repetition: the latter may end on V or vi depending on the selected ground
  // variant. Minor-mode cadences retain the deterministic Picardy policy.
  const BarChord final_cadence_chord = {0, mode == Mode::Minor && !detail::usePicardy(req.seed)};
  out.harmony.tonic_pc = 0;
  out.harmony.is_minor = (mode == Mode::Minor);
  for (int bar = 0; bar < bars; ++bar) {
    const BarChord chord =
        bar == bars - 1
            ? final_cadence_chord
            : goldbergBarChord(ground[static_cast<std::size_t>(bar % kCycleBars)], mode);
    ChordEvent ce;
    ce.start_tick = barTick(bar);
    ce.root_pc = chord.root_pc;
    ce.quality = chord.minor ? ChordQuality::Minor : ChordQuality::Major;
    out.harmony.chords.push_back(ce);
  }

  // The climax block is the arc climax cycle (~80% of the span), NOT necessarily
  // the last block. The aria (block 0) and any da-capo block are never the
  // climax.
  const int cycle_count = static_cast<int>(req.cycle_count);
  int climax_block = -1;
  for (int blk = 0; blk < num_blocks; ++blk) {
    if (blk == 0 || blk == da_capo_block)
      continue;
    const int cycle = cycle_count > 0 ? (blk * cycle_count) / num_blocks : 0;
    if (req.arc(static_cast<std::size_t>(cycle)).is_climax) {
      climax_block = blk;
      break;
    }
  }
  // If the arc placed its climax on a block we excluded (aria / da-capo), fall
  // back to the last figuration variation block so a climax is always present.
  if (climax_block < 0) {
    for (int blk = num_blocks - 1; blk >= 1; --blk) {
      if (blk != da_capo_block) {
        climax_block = blk;
        break;
      }
    }
  }

  // Variation register anchor: ~C5 region (72), well above the C2 ground.
  constexpr int kVarRegisterBase = 72;

  for (int blk = 0; blk < num_blocks; ++blk) {
    PassacagliaVariation var;
    var.voice = 0;
    var.start_tick = barTick(blk * kCycleBars);
    var.end_tick = var.start_tick + static_cast<Tick>(kCycleBars) * kTicksPerBar;
    var.is_climax = (blk == climax_block);

    const bool is_aria = (blk == 0) || (blk == da_capo_block);
    if (is_aria) {
      var.density_level = 0;
      for (int local = 0; local < kCycleBars; ++local) {
        const int bar = blk * kCycleBars + local;
        appendAriaBar(var, bar,
                      goldbergBarChord(ground[static_cast<std::size_t>(bar % kCycleBars)], mode),
                      mode, kVarRegisterBase, offset);
      }
      if (blk == num_blocks - 1) {
        // Keep the aria's m=2 rhythm at the da-capo, but turn its final bar
        // into a dominant-to-tonic upper-voice arrival over the coda bass.
        // These two notes are both consonant with the final tonic harmony.
        var.notes[var.notes.size() - 2].pitch = kVarRegisterBase + 7;
        var.notes.back().pitch = kVarRegisterBase;
      }
      out.material.goldberg_variations.push_back(var);
      continue;
    }

    // Variation block. The variation index is the post-aria ordinal (the aria is
    // block 0; the first variation is block 1 => variation_index 0). The kind
    // dispatch routes Figuration vs Canon (BWV988: every third variation is a
    // canon at a rising imitation interval).
    const std::size_t variation_index = static_cast<std::size_t>(blk - 1);
    const std::size_t variation_number = variation_index + 1;  // 1-based (1..K).
    const int cycle = cycle_count > 0 ? (blk * cycle_count) / num_blocks : 0;
    const ArcPoint point = req.arc(static_cast<std::size_t>(cycle));

    switch (goldbergVariationKind(variation_index)) {
      case GoldbergVariationKind::Canon: {
        // Canon number c = variation_number / 3 (1..9 across the full set);
        // imitation interval = (c - 1) diatonic degrees above unison (unison,
        // 2nd, 3rd, ... 9th), following the BWV988 scheme. The leader runs in
        // eighths (canons cap at eighths so the two clear lines stay legible),
        // so density_level is the eighth tier regardless of the arc.
        const int canon_number = static_cast<int>(variation_number / 3);
        const int imitation_degrees = canon_number - 1;  // 0 = unison canon.
        var.density_level = 1;
        buildCanonBlock(var, inner_voice, blk * kCycleBars, imitation_degrees, mode, ground,
                        fill_thirds);
        inner_blocks.push_back(blk);
        canon_blocks.push_back(blk);
        break;
      }
      case GoldbergVariationKind::Quodlibet: {
        // Variation 30 combines the dense principal figuration with a second,
        // independently recurring chord-tone tune in the middle register.
        var.density_level = 2;
        for (int local = 0; local < kCycleBars; ++local) {
          const int bar = blk * kCycleBars + local;
          appendVariationBar(
              var, bar, goldbergBarChord(ground[static_cast<std::size_t>(bar % kCycleBars)], mode),
              mode, 4, kVarRegisterBase, offset);
        }
        // The tune states each bar's triad in one rotation held across the whole
        // block, so what recurs is its shape. Which rotation that is has to be
        // read off the texture rather than fixed: the bass arpeggiates the same
        // triad underneath at its own pace, so a rotation that happens to reach
        // the same chord tones in step with it doubles the bass instead of
        // answering it. Every rotation is laid out against the figuration
        // already settled above and the bass below, and the cleanest is kept.
        std::vector<MaterialNote> tune;
        std::array<int, 6> best_score{};
        for (int rotation = 0; rotation < 4; ++rotation) {
          std::vector<MaterialNote> candidate;
          candidate.reserve(16);
          for (int local = 0; local < kCycleBars; ++local) {
            const int bar = blk * kCycleBars + local;
            const BarChord chord =
                goldbergBarChord(ground[static_cast<std::size_t>(bar % kCycleBars)], mode);
            const int root_pc = chord.root_pc;
            const int third_pc = (root_pc + (chord.minor ? 3 : 4)) % 12;
            const int fifth_pc = (root_pc + 7) % 12;
            const std::array<int, 4> theme_pcs = {third_pc, fifth_pc, root_pc, fifth_pc};
            for (std::size_t beat = 0; beat < theme_pcs.size(); ++beat) {
              const int pc =
                  theme_pcs[(beat + static_cast<std::size_t>(rotation)) % theme_pcs.size()];
              int pitch = 60 + ((pc - 60) % 12 + 12) % 12;
              while (pitch > 67)
                pitch -= 12;
              candidate.push_back(materialNote(
                  barTick(bar) + static_cast<Tick>(beat) * kTicksPerBeat, kTicksPerBeat, pitch));
            }
          }
          const std::array<int, 6> score = goldbergBlockFaults(
              var.notes, candidate, blk * kCycleBars, ground, mode, fill_thirds);
          if (rotation == 0 || score < best_score) {
            best_score = score;
            tune = std::move(candidate);
          }
        }
        inner_voice.insert(inner_voice.end(), tune.begin(), tune.end());
        inner_blocks.push_back(blk);
        break;
      }
      case GoldbergVariationKind::Figuration:
      default: {
        int notes_per_beat = notesPerBeatFor(point, profile.density_bias);
        const bool design_peak = var.is_climax || variation_number == 30;
        if (var.is_climax)
          notes_per_beat = 4;  // the arc climax block is the densest by design.
        // The final variation of the full set (variation 30) is the design
        // secondary peak (the BWV988 Quodlibet slot): the densest figuration
        // tier directly, no search.
        if (variation_number == 30)
          notes_per_beat = 4;
        if (profile.prefer_dotted && notes_per_beat < 4)
          notes_per_beat = 2;  // Noble keeps a moderate, dignified subdivision.
        var.density_level = notes_per_beat == 1 ? 0 : (notes_per_beat == 4 ? 2 : 1);
        const int register_base = kVarRegisterBase + static_cast<int>(point.register_shift);
        // Pattern selection: the climax block and variation 30 are design
        // values (the densest anchored scalar wave); other figuration blocks
        // rotate the goldberg palette so consecutive variations alternate
        // idioms (the BWV988 figuration-type rotation).
        const PatternKind pattern = design_peak
                                        ? PatternKind::kScalarWave
                                        : kGoldbergPalette[(req.seed + variation_index) % 2];
        for (int local = 0; local < kCycleBars; ++local) {
          const int bar = blk * kCycleBars + local;
          const BarChord chord =
              goldbergBarChord(ground[static_cast<std::size_t>(bar % kCycleBars)], mode);
          switch (pattern) {
            case PatternKind::kFiguraCorta:
              appendFiguraCortaBar(var.notes, bar,
                                   goldbergFigurationStart(register_base, offset, chord, mode),
                                   detail::ChordSpec{chord.root_pc, chord.minor}, mode);
              break;
            case PatternKind::kScalarWave:
            default:
              appendVariationBar(var, bar, chord, mode, notes_per_beat, register_base, offset);
              break;
          }
        }
        break;
      }
    }
    // Cadential landing on the piece's final variation block (the da-capo aria
    // already closes in plain half notes, so it keeps its own layout). The
    // tiled ground does not cadence -- its final bar may sit on V or vi -- so
    // both landing tones are chosen for consonance: the final tone is the
    // tonic when the final bar's chord contains it, else the chord root; the
    // pre-final tone is its diatonic upper step when the penultimate chord
    // supports it, else the step below, else an anticipation.
    if (blk == num_blocks - 1 && !is_aria && !var.notes.empty()) {
      const BarChord final_chord = final_cadence_chord;
      const BarChord penult_chord =
          goldbergBarChord(ground[static_cast<std::size_t>((bars - 2) % kCycleBars)], mode);
      auto consonant_with = [&](int pitch, const BarChord& chord) {
        const int third = chord.minor ? 3 : 4;
        const int triad_pc[3] = {chord.root_pc % 12, (chord.root_pc + third) % 12,
                                 (chord.root_pc + 7) % 12};
        for (int tone : triad_pc) {
          if (!isConsonantIc(pitch - tone))
            return false;
        }
        return true;
      };
      int final_tone = kVarRegisterBase;  // C5 tonic by preference.
      if (!consonant_with(final_tone, final_chord)) {
        // Fall back to the final chord's root in the variation register.
        final_tone = kVarRegisterBase + ((final_chord.root_pc - kVarRegisterBase) % 12 + 12) % 12;
        if (final_tone > kVarRegisterBase + 6)
          final_tone -= 12;
      }
      int prefinal = final_tone;  // anticipation fallback.
      const int step_above = detail::scaleUp(final_tone, 1, mode);
      if (consonant_with(step_above, penult_chord)) {
        prefinal = step_above;
      } else if (consonant_with(final_tone - 1, penult_chord)) {
        prefinal = final_tone - 1;
      }
      appendCadentialLanding(var.notes, barTick(bars - 2), kTicksPerBar, prefinal, final_tone, mode,
                             /*band_lo=*/62, /*downbeat_chord=*/nullptr,
                             /*prefer_descending=*/false, /*lift_to_context=*/true);
    }

    out.material.goldberg_variations.push_back(var);
  }

  out.material.goldberg_inner_voice = std::move(inner_voice);

  // VoicePlan (3 voices, strictly ordered by register so voice_crossing never
  // fires): V0 = principal variation, V1 = canon/quodlibet inner line, and V2
  // = the immutable aria-bass phrase. All use dedicated Goldberg carriers.
  out.voice_plan.num_voices = 3;
  SpanId next_span_id = 0;

  Span ground_span;
  ground_span.id = next_span_id++;
  ground_span.start_tick = 0;
  const Tick final_bar_tick = barTick(bars - 1);
  const Tick final_approach_tick = final_bar_tick - kTicksPerBeat;
  ground_span.end_tick = final_approach_tick;
  ground_span.voice = 2;
  ground_span.intent = VoiceIntent::GoldbergBassCarrier;
  ground_span.subdivision = Subdivision::Quarter;
  out.voice_plan.spans.push_back(ground_span);

  // The immutable four-bar aria bass deliberately stops before the final bar.
  // Its explicit tonic coda resolves the otherwise open V/vi terminal ground.
  CodaDecl coda;
  coda.voice = 2;
  coda.notes.push_back(materialNote(final_approach_tick, kTicksPerBeat, 43));
  coda.notes.push_back(materialNote(final_bar_tick, kTicksPerBar, ground.front()));
  out.material.coda_extensions.push_back(std::move(coda));

  // The variation line is the only voice here that may move at all. The aria
  // bass is immutable by contract -- goldberg_aria_bass_immutable demands every
  // one of its onsets verbatim -- and it is also the voice the variation runs
  // into: the bar chord's root IS the ground tone's pitch class, the variation
  // is anchored three octaves above the same class, and both derive their bar
  // tone from that one root, so they arrive congruently by construction rather
  // than by accident. Nothing in the variation loop can see this, because the
  // Goldberg builder keeps no registry of the voices it has already placed.
  // The congruence is not confined to the bar head either: the aria bass walks
  // that same triad through the bar while the variation figures it above, so
  // the two lines meet on a perfect interval off the downbeat as well.
  // Relieved here, where the whole texture is finally known, on the same terms
  // as the chorale prelude: the tone that moves is the onset before each
  // arrival. Read at the eighth, because that is how often the aria bass moves
  // -- both lines are predominantly stepwise, so they run into a perfect
  // interval together on the bass's own off-beats and not only on the beats.
  {
    ThemeToneRegistry relief_registry;
    const Tick ground_period = out.material.goldberg_aria_bass_period;
    for (Tick offset_tick = 0; offset_tick < final_approach_tick; offset_tick += ground_period) {
      for (const MaterialNote& note : out.material.goldberg_aria_bass) {
        const Tick start = offset_tick + note.start_tick;
        if (start < final_approach_tick)
          relief_registry.record(start, /*voice=*/2, static_cast<int>(note.pitch), note.duration);
      }
    }
    for (const MaterialNote& note : out.material.coda_extensions.back().notes)
      relief_registry.record(note.start_tick, /*voice=*/2, static_cast<int>(note.pitch),
                             note.duration);
    for (const MaterialNote& note : out.material.goldberg_inner_voice)
      relief_registry.record(note.start_tick, /*voice=*/1, static_cast<int>(note.pitch),
                             note.duration);
    // Canon blocks are excluded. Their variation line is the canon LEADER, and
    // the follower on V1 is that leader plus one fixed imitation interval;
    // moving a leader tone without moving its comes breaks the imitation the
    // block exists to state. The quodlibet block also carries a second line on
    // V1, but that one is a free chord-tone tune rather than a copy of the
    // variation, so its figuration answers to nothing but its own line and is
    // relieved like any other -- against the tune, which the registry holds.
    std::vector<bool> imitative(out.material.goldberg_variations.size(), false);
    for (int blk : canon_blocks) {
      if (blk >= 0 && static_cast<std::size_t>(blk) < imitative.size())
        imitative[static_cast<std::size_t>(blk)] = true;
    }
    std::vector<std::vector<MaterialNote>*> blocks;
    blocks.reserve(out.material.goldberg_variations.size());
    for (std::size_t idx = 0; idx < out.material.goldberg_variations.size(); ++idx) {
      if (!imitative[idx])
        blocks.push_back(&out.material.goldberg_variations[idx].notes);
    }
    relieveArrivals(lineInTickOrder(blocks), relief_registry, /*voice=*/0,
                    /*num_voices=*/3, bars, mode, /*arrival_grain=*/kAriaBassUnit);
  }

  Span coda_span;
  coda_span.id = next_span_id++;
  coda_span.start_tick = final_approach_tick;
  coda_span.end_tick = barTick(bars);
  coda_span.voice = 2;
  coda_span.intent = VoiceIntent::CodaCarrier;
  coda_span.subdivision = Subdivision::Quarter;
  out.voice_plan.spans.push_back(coda_span);

  ChordEvent approach;
  approach.start_tick = final_approach_tick;
  approach.root_pc = 7;
  approach.quality = ChordQuality::Dominant7;
  approach.degree = RomanNumeral::V;
  approach.function = HarmonicFunction::D;
  approach.has_degree = true;
  out.harmony.chords.insert(out.harmony.chords.end() - 1, approach);
  out.harmony.cadences.push_back({final_bar_tick, CadenceType::ImperfectAuthentic});

  // V0 principal line: one dedicated variation span per block.
  for (int blk = 0; blk < num_blocks; ++blk) {
    Span var_span;
    var_span.id = next_span_id++;
    var_span.start_tick = barTick(blk * kCycleBars);
    var_span.end_tick = var_span.start_tick + static_cast<Tick>(kCycleBars) * kTicksPerBar;
    var_span.voice = 0;
    var_span.intent = VoiceIntent::GoldbergVariationCarrier;
    var_span.subdivision = Subdivision::Quarter;
    out.voice_plan.spans.push_back(var_span);
  }

  // V1 inner line: canon followers plus the variation-30 Quodlibet tune.
  for (int blk : inner_blocks) {
    Span follower_span;
    follower_span.id = next_span_id++;
    follower_span.start_tick = barTick(blk * kCycleBars);
    follower_span.end_tick =
        follower_span.start_tick + static_cast<Tick>(kCycleBars) * kTicksPerBar;
    follower_span.voice = 1;
    follower_span.intent = VoiceIntent::GoldbergInnerVoiceCarrier;
    follower_span.subdivision = Subdivision::Quarter;
    out.voice_plan.spans.push_back(follower_span);
  }

  return out;
}

}  // namespace bach::composer
