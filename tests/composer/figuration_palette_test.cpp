#include "composer/figuration_palette.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdlib>
#include <set>
#include <vector>

#include "composer/texture_helpers.h"

namespace bach::composer {
namespace {

// A simple C-major 4-bar cycle plan (I - V - vi - V) whose chord roots track
// the ground pitch classes, mirroring how the ground-variation builders
// construct their plans.
std::vector<CycleBar> majorCyclePlan() {
  return {
      {0, false, 60, 0},  // I : ground C, start C4.
      {7, false, 55, 7},  // V : ground G, start G3.
      {9, true, 57, 9},   // vi: ground A, start A3.
      {7, false, 55, 7},  // V : ground G, start G3.
  };
}

/// @brief True when `pitch`'s class is one of the bar's anchor pitch classes.
bool isAnchorTone(int pitch, const CycleBar& bar, detail::Mode mode) {
  const std::vector<int> pcs = barAnchorPitchClasses(bar, mode);
  const int pc = ((pitch % 12) + 12) % 12;
  return std::find(pcs.begin(), pcs.end(), pc) != pcs.end();
}

/// @brief True when `pitch`'s class is a tone of the chord's triad.
bool isTriadTone(int pitch, const detail::ChordSpec& chord) {
  const int third = chord.minor ? 3 : 4;
  const int pc = ((pitch % 12) + 12) % 12;
  return pc == chord.root_pc % 12 || pc == (chord.root_pc + third) % 12 ||
         pc == (chord.root_pc + 7) % 12;
}

// --- kSawtooth ---------------------------------------------------------------

TEST(FigurationPaletteSawtooth, BeatOnsetsAreChordToneAnchors) {
  const std::vector<CycleBar> plan = majorCyclePlan();
  std::vector<MaterialNote> notes;
  appendSawtoothCycle(notes, 0, plan, /*register_shift=*/0, /*anchor_rotation=*/0,
                      /*notes_per_beat=*/2, detail::Mode::Major);

  ASSERT_EQ(notes.size(), plan.size() * 3 * 2);  // bars * beats * subdivision.
  for (std::size_t i = 0; i < notes.size(); ++i) {
    const MaterialNote& note = notes[i];
    if (note.start_tick % kTicksPerBeat != 0)
      continue;  // only beat onsets are anchor-guaranteed.
    const int bar = static_cast<int>(note.start_tick / kTicksPerBar34);
    EXPECT_TRUE(isAnchorTone(note.pitch, plan[static_cast<std::size_t>(bar)], detail::Mode::Major))
        << "beat onset at tick " << note.start_tick << " pitch " << static_cast<int>(note.pitch);
  }
}

TEST(FigurationPaletteSawtooth, AnchorsStayInsideOctaveBandAroundCenter) {
  const std::vector<CycleBar> plan = majorCyclePlan();
  for (int shift : {0, 5}) {
    std::vector<MaterialNote> notes;
    appendSawtoothCycle(notes, 0, plan, shift, /*anchor_rotation=*/1,
                        /*notes_per_beat=*/1, detail::Mode::Major);
    const int center = plan.front().low_tone + shift;
    for (const MaterialNote& note : notes) {
      EXPECT_GE(static_cast<int>(note.pitch), center - 12);
      EXPECT_LE(static_cast<int>(note.pitch), center + 12);
    }
  }
}

TEST(FigurationPaletteSawtooth, FillsStepTowardTheNextAnchorWithoutLeaps) {
  const std::vector<CycleBar> plan = majorCyclePlan();
  std::vector<MaterialNote> notes;
  appendSawtoothCycle(notes, 0, plan, 0, 0, /*notes_per_beat=*/4, detail::Mode::Major);
  // Within one beat the fill moves at most one diatonic degree per sub-note,
  // except the echappee bend on the final sub (two degrees, at most a third):
  // a fill that would land exactly on the next anchor bends one step past it
  // instead of re-attacking the anchor's pitch. Consecutive notes of the same
  // beat therefore stay within 4 semitones and never repeat a pitch.
  for (std::size_t i = 1; i < notes.size(); ++i) {
    if (notes[i].start_tick / kTicksPerBeat != notes[i - 1].start_tick / kTicksPerBeat)
      continue;
    EXPECT_LE(std::abs(static_cast<int>(notes[i].pitch) - static_cast<int>(notes[i - 1].pitch)), 4)
        << "intra-beat fill leap at tick " << notes[i].start_tick;
    EXPECT_NE(static_cast<int>(notes[i].pitch), static_cast<int>(notes[i - 1].pitch))
        << "intra-beat repeated pitch at tick " << notes[i].start_tick;
  }
}

// --- barAnchorPitchClasses ----------------------------------------------------

// Out-of-scale triad tones flatten to the scale tone a semitone below BEFORE
// the ground-consonance filter. Minor ii (D-F-A): the A flattens to Ab, which
// the filter rejects as a tritone over the D ground -- anchors are {D, F}.
TEST(FigurationPaletteAnchors, MinorTwoChordFlattensRaisedFifthAndRejectsTritone) {
  const CycleBar bar{2, true, 50, 2};  // ii over a D ground (C minor).
  const std::vector<int> pcs = barAnchorPitchClasses(bar, detail::Mode::Minor);
  EXPECT_EQ(pcs, (std::vector<int>{2, 5}));
}

// Major B-root bar (bass B treated as a major root, B-D#-F#): the D# flattens
// to D (a consonant third over the B ground); the F# flattens to F, which the
// filter rejects as a tritone -- anchors are {B, D}.
TEST(FigurationPaletteAnchors, MajorBRootFlattensThirdAndRejectsTritoneFifth) {
  const CycleBar bar{11, false, 59, 11};
  const std::vector<int> pcs = barAnchorPitchClasses(bar, detail::Mode::Major);
  EXPECT_EQ(pcs, (std::vector<int>{11, 2}));
}

// --- kScalarWave (3/4 cycle form) ---------------------------------------------

// Every beat onset of the wave must be consonant with the held ground: either
// one of the bar's anchor pitch classes or a tone whose interval class over
// the ground is consonant (thirds and sixths are as clean as chord tones).
// The free-running wave once anchored only bar downbeats, which left
// dissonant tones on the beats of entire sixteenth-tier cycles -- audible as
// sustained dissonance over the ground. Snapping is reserved for genuinely
// dissonant positions so low density tiers keep a stepwise surface.
TEST(FigurationPaletteScalarWave, BeatOnsetsAreConsonantWithGround) {
  const std::vector<CycleBar> minor_plan = {
      {0, true, 60, 0},     // i  : ground C.
      {10, false, 58, 10},  // VII: ground Bb.
      {2, true, 50, 2},     // ii : ground D (anchors flatten the raised fifth).
      {7, false, 55, 7},    // V  : ground G.
  };
  for (int notes_per_beat : {1, 2, 4}) {
    std::vector<MaterialNote> notes;
    appendScalarWaveCycle(notes, 0, minor_plan, /*band_lo=*/60, /*band_hi=*/79,
                          /*phase_rotation=*/3, /*descending_start=*/false, notes_per_beat,
                          detail::Mode::Minor);
    for (const MaterialNote& note : notes) {
      if (note.start_tick % kTicksPerBeat != 0)
        continue;  // only beat onsets are consonance-guaranteed.
      const int bar = static_cast<int>(note.start_tick / kTicksPerBar34);
      const CycleBar& plan_bar = minor_plan[static_cast<std::size_t>(bar)];
      EXPECT_TRUE(isConsonantIc(static_cast<int>(note.pitch) - plan_bar.ground_pc))
          << "npb " << notes_per_beat << " beat onset at tick " << note.start_tick << " pitch "
          << static_cast<int>(note.pitch);
    }
  }
}

TEST(FigurationPaletteScalarWave, CycleStaysInBandAndConjunct) {
  const std::vector<CycleBar> plan = majorCyclePlan();
  constexpr int kBandLo = 60;
  constexpr int kBandHi = 79;
  for (int rotation : {0, 2, 4}) {
    std::vector<MaterialNote> notes;
    appendScalarWaveCycle(notes, 0, plan, kBandLo, kBandHi, rotation,
                          /*descending_start=*/rotation % 2 == 0, /*notes_per_beat=*/2,
                          detail::Mode::Major);
    ASSERT_EQ(notes.size(), plan.size() * 3 * 2);
    for (std::size_t i = 0; i < notes.size(); ++i) {
      // Band clamp: the wave folds at the edges instead of escaping the band.
      EXPECT_GE(static_cast<int>(notes[i].pitch), kBandLo);
      EXPECT_LE(static_cast<int>(notes[i].pitch), kBandHi);
      if (i == 0)
        continue;
      // Conjunct surface: one diatonic step (<= 2 semitones) per note. A beat
      // onset may combine the wave advance with the chord-tone snap (up to
      // three diatonic steps, <= 5 semitones), never a genuine leap.
      const bool beat_head = notes[i].start_tick % kTicksPerBeat == 0;
      EXPECT_LE(std::abs(static_cast<int>(notes[i].pitch) - static_cast<int>(notes[i - 1].pitch)),
                beat_head ? 5 : 2)
          << "leap at tick " << notes[i].start_tick;
    }
  }
}

// --- kScalarWave (4/4 bar form) -----------------------------------------------

TEST(FigurationPaletteScalarWaveBar, FirstBarOpensOnTriadToneInBand) {
  const detail::ChordSpec chord{0, false};  // C major.
  constexpr int kBase = 60;
  constexpr int kCeil = 84;
  std::vector<MaterialNote> notes;
  int prev_pitch = -1;
  appendScalarWaveBar(notes, /*bar=*/0, chord, detail::Mode::Major, /*notes_per_beat=*/4, kBase,
                      kCeil, /*offset=*/2, prev_pitch);
  ASSERT_EQ(notes.size(), 16u);
  EXPECT_TRUE(isTriadTone(notes.front().pitch, chord));
  for (const MaterialNote& note : notes) {
    EXPECT_GE(static_cast<int>(note.pitch), kBase);
    EXPECT_LE(static_cast<int>(note.pitch), kCeil);
  }
  EXPECT_EQ(prev_pitch, static_cast<int>(notes.back().pitch));
}

TEST(FigurationPaletteScalarWaveBar, ChainsConjunctlyAcrossBars) {
  const detail::ChordSpec tonic{0, false};
  const detail::ChordSpec dominant{7, false};
  constexpr int kBase = 60;
  constexpr int kCeil = 84;
  std::vector<MaterialNote> notes;
  int prev_pitch = -1;
  appendScalarWaveBar(notes, 0, tonic, detail::Mode::Major, 2, kBase, kCeil, 0, prev_pitch);
  const int bar_boundary_prev = prev_pitch;
  appendScalarWaveBar(notes, 1, dominant, detail::Mode::Major, 2, kBase, kCeil, 0, prev_pitch);
  // The second bar's downbeat is the dominant chord tone nearest the previous
  // bar's last pitch (no re-snap to the band floor).
  const MaterialNote& second_bar_head = notes[8];
  EXPECT_EQ(second_bar_head.start_tick, kTicksPerBar);
  EXPECT_TRUE(isTriadTone(second_bar_head.pitch, dominant));
  EXPECT_LE(std::abs(static_cast<int>(second_bar_head.pitch) - bar_boundary_prev), 4)
      << "bar boundary should chain conjunctly, not leap to the band floor";
}

TEST(FigurationPaletteScalarWaveBar, OversizedOffsetFallsBackToBandFloorAnchor) {
  const detail::ChordSpec chord{0, false};
  constexpr int kBase = 60;
  constexpr int kCeil = 72;  // anchor window is [60, 66]: C4 / E4 only.
  std::vector<MaterialNote> notes;
  int prev_pitch = -1;
  appendScalarWaveBar(notes, 0, chord, detail::Mode::Major, 1, kBase, kCeil,
                      /*offset=*/12, prev_pitch);
  // A start offset that escapes the anchor window snaps back to the lowest
  // triad tone at or above the band floor.
  EXPECT_EQ(static_cast<int>(notes.front().pitch), 60);
}

// With rotate_figures the bar figure cycles through four shapes keyed by
// (bar + offset) % 4: broken-third chain, one dive, plain wave, triad
// arpeggio sweep. Pin the rotation breadth: one four-bar cycle produces at
// least three distinct interval multisets, and exactly the arpeggio bar
// consists of triad tones only (every step figure carries non-triad passing
// tones). Losing a figure -- or the rotation itself -- re-concentrates the
// long-form interval-bigram surface this rotation exists to spread.
TEST(FigurationPaletteScalarWaveBar, RotationCyclesFourDistinctFigures) {
  const detail::ChordSpec chord{0, false};
  int prev_pitch = -1;
  std::set<std::multiset<int>> signatures;
  int all_triad_bars = 0;
  for (int bar = 0; bar < 4; ++bar) {
    std::vector<MaterialNote> notes;
    appendScalarWaveBar(notes, bar, chord, detail::Mode::Major, /*notes_per_beat=*/4,
                        /*base_midi=*/60, /*ceil_midi=*/84, /*offset=*/0, prev_pitch,
                        /*rotate_figures=*/true);
    ASSERT_EQ(notes.size(), 16u);
    std::multiset<int> signature;
    bool all_triad = true;
    for (std::size_t idx = 0; idx < notes.size(); ++idx) {
      if (idx > 0) {
        signature.insert(static_cast<int>(notes[idx].pitch) -
                         static_cast<int>(notes[idx - 1].pitch));
      }
      all_triad = all_triad && isTriadTone(static_cast<int>(notes[idx].pitch), chord);
    }
    all_triad_bars += all_triad ? 1 : 0;
    signatures.insert(signature);
  }
  EXPECT_GE(signatures.size(), 3u) << "bar figures collapsed";
  EXPECT_EQ(all_triad_bars, 1) << "expected exactly one arpeggio-sweep bar per cycle";
}

// Triplet mode subdivides every beat into six sixteenth-triplet notes of
// exactly 80 ticks, keeping the downbeat chord-tone anchor and the stepwise
// wave surface (the tightened drive into the fugue).
TEST(FigurationPaletteScalarWaveBar, TripletModeEmits80TickNotesStillChordAnchored) {
  const detail::ChordSpec chord{0, false};  // C major.
  constexpr int kBase = 60;
  constexpr int kCeil = 84;
  std::vector<MaterialNote> notes;
  int prev_pitch = -1;
  appendScalarWaveBar(notes, /*bar=*/0, chord, detail::Mode::Major, /*notes_per_beat=*/4, kBase,
                      kCeil, /*offset=*/0, prev_pitch, /*rotate_figures=*/false, /*triplet=*/true);
  ASSERT_EQ(notes.size(), 24u);  // 6 triplets * 4 beats.
  for (const MaterialNote& note : notes) {
    EXPECT_EQ(note.duration, static_cast<Tick>(80));
    EXPECT_GE(static_cast<int>(note.pitch), kBase);
    EXPECT_LE(static_cast<int>(note.pitch), kCeil);
  }
  // The bar opens on a chord tone, and every note steps by at most one diatonic
  // step (<= 2 semitones) from the previous one -- the conjunct wave surface.
  EXPECT_TRUE(isTriadTone(notes.front().pitch, chord));
  for (std::size_t i = 1; i < notes.size(); ++i) {
    EXPECT_LE(std::abs(static_cast<int>(notes[i].pitch) - static_cast<int>(notes[i - 1].pitch)), 2)
        << "leap in triplet wave at index " << i;
  }
  // Each beat onset lands on a whole-tick beat boundary.
  for (int beat = 0; beat < 4; ++beat) {
    EXPECT_EQ(notes[static_cast<std::size_t>(beat * 6)].start_tick,
              static_cast<Tick>(beat) * kTicksPerBeat);
  }
}

TEST(FigurationPaletteScalarWaveBar, CascadeBarLeapsASixthAndFillsByContraryStep) {
  // The (bar + offset) % 4 == 2 figure is the sixth-leap cascade: with
  // offset 0, bar 2 must contain at least one ascending leap of a sixth or
  // more, and every such leap must land on a chord tone and be followed by
  // descending motion (the classical leap-then-contrary-fill resolution).
  const detail::ChordSpec chord{0, false};
  int prev_pitch = -1;
  std::vector<MaterialNote> notes;
  for (int bar = 0; bar <= 2; ++bar) {
    notes.clear();
    appendScalarWaveBar(notes, bar, chord, detail::Mode::Major, /*notes_per_beat=*/4,
                        /*base_midi=*/60, /*ceil_midi=*/84, /*offset=*/0, prev_pitch,
                        /*rotate_figures=*/true);
  }
  ASSERT_EQ(notes.size(), 16u);
  int ascending_sixth_leaps = 0;
  for (std::size_t idx = 1; idx < notes.size(); ++idx) {
    const int interval =
        static_cast<int>(notes[idx].pitch) - static_cast<int>(notes[idx - 1].pitch);
    if (interval >= 8) {
      ++ascending_sixth_leaps;
      EXPECT_TRUE(isTriadTone(static_cast<int>(notes[idx].pitch), chord))
          << "leap target at index " << idx << " is not a chord tone";
      if (idx + 1 < notes.size()) {
        EXPECT_LT(static_cast<int>(notes[idx + 1].pitch), static_cast<int>(notes[idx].pitch))
            << "leap at index " << idx << " is not filled by contrary descent";
      }
    }
  }
  EXPECT_GE(ascending_sixth_leaps, 1) << "cascade bar emitted no ascending sixth leap";
}

TEST(FigurationPaletteScalarWaveBar, RotatingBarsNeverRepeatThePreviousPitchAtTheSeam) {
  // The conjunct bar chain picks the chord tone nearest the previous bar's
  // last pitch; in rotation mode that candidate must not BE the previous
  // pitch, or nearly every bar seam stamps an interval-0 pair the corpus
  // distribution does not have.
  const detail::ChordSpec chord{0, false};
  int prev_pitch = -1;
  int last_pitch = -1;
  for (int bar = 0; bar < 8; ++bar) {
    std::vector<MaterialNote> notes;
    appendScalarWaveBar(notes, bar, chord, detail::Mode::Major, /*notes_per_beat=*/4,
                        /*base_midi=*/60, /*ceil_midi=*/84, /*offset=*/0, prev_pitch,
                        /*rotate_figures=*/true);
    ASSERT_FALSE(notes.empty());
    if (last_pitch >= 0) {
      EXPECT_NE(static_cast<int>(notes.front().pitch), last_pitch)
          << "bar " << bar << " opens by repeating the previous bar's last pitch";
    }
    last_pitch = static_cast<int>(notes.back().pitch);
  }
}

// --- kFiguration ----------------------------------------------------------------

TEST(FigurationPaletteFigurationWave, DownbeatsAreChordTonesAndLineStaysInBand) {
  const detail::ChordSpec chord{0, false};
  constexpr int kBandLo = 51;
  constexpr int kBandHi = 66;
  ThemeToneRegistry registry;
  FigurationSection section;
  int prev_anchor = 0;
  appendFigurationWaveBar(registry, section, /*bar=*/0, /*voice=*/1, chord, detail::Mode::Major,
                          /*notes_per_beat=*/2, /*offset=*/1, prev_anchor, kBandLo, kBandHi,
                          /*num_voices=*/3);
  ASSERT_EQ(section.notes.size(), 8u);
  EXPECT_TRUE(isTriadTone(section.notes.front().pitch, chord));
  for (const MaterialNote& note : section.notes) {
    EXPECT_GE(static_cast<int>(note.pitch), kBandLo);
    EXPECT_LE(static_cast<int>(note.pitch), kBandHi);
  }
  EXPECT_EQ(prev_anchor, static_cast<int>(section.notes.back().pitch));
}

TEST(FigurationPaletteFigurationWave, FormsNoPerfectParallelAgainstRegistryVoice) {
  // Earlier voice (V0): an ascending C-major scale in quarters across 4 bars --
  // the motion shape most likely to drag a same-direction line into parallels.
  ThemeToneRegistry registry;
  const int kScale[8] = {72, 74, 76, 77, 79, 81, 83, 84};
  for (int bar = 0; bar < 4; ++bar) {
    for (int beat = 0; beat < 4; ++beat) {
      const Tick tick = static_cast<Tick>(bar) * kTicksPerBar + beat * kTicksPerBeat;
      registry.record(tick, /*voice=*/0, kScale[(bar * 4 + beat) % 8], kTicksPerBeat);
    }
  }

  const detail::ChordSpec chord{0, false};
  FigurationSection section;
  int prev_anchor = 0;
  for (int bar = 0; bar < 4; ++bar) {
    appendFigurationWaveBar(registry, section, bar, /*voice=*/1, chord, detail::Mode::Major,
                            /*notes_per_beat=*/2, /*offset=*/0, prev_anchor, 51, 66,
                            /*num_voices=*/3);
  }

  // Check every consecutive pair of figuration notes against V0's concurrent
  // motion: no same-direction arrival on a perfect 5th/8th may survive.
  int parallels = 0;
  for (std::size_t i = 1; i < section.notes.size(); ++i) {
    const MaterialNote& prev = section.notes[i - 1];
    const MaterialNote& curr = section.notes[i];
    const int other_prev = registry.soundingPitchInVoice(0, prev.start_tick);
    const int other_curr = registry.soundingPitchInVoice(0, curr.start_tick);
    if (other_prev < 0 || other_curr < 0)
      continue;
    if (formsPerfectParallel(prev.pitch, curr.pitch, other_prev, other_curr))
      ++parallels;
  }
  EXPECT_EQ(parallels, 0);
}

TEST(FigurationPaletteFigurationWave, BarFigureRotationVariesIntervalVocabulary) {
  // The wave rotates its bar figure by bar % 3: broken-third chain (0), one
  // fourth/fifth dive (1), plain stepwise wave (2). With no other voice in the
  // registry the vetting fallbacks never replace a figure note, so the bar
  // interval vocabulary is the figure's own.
  const detail::ChordSpec chord{0, false};
  auto bar_intervals = [&](int bar) {
    ThemeToneRegistry registry;
    FigurationSection section;
    int prev_anchor = 0;
    appendFigurationWaveBar(registry, section, bar, /*voice=*/1, chord, detail::Mode::Major,
                            /*notes_per_beat=*/2, /*offset=*/1, prev_anchor, 51, 66,
                            /*num_voices=*/3);
    std::vector<int> intervals;
    for (std::size_t i = 1; i < section.notes.size(); ++i) {
      intervals.push_back(std::abs(static_cast<int>(section.notes[i].pitch) -
                                   static_cast<int>(section.notes[i - 1].pitch)));
    }
    return intervals;
  };
  // Broken-third bar: at least two third-sized (or larger) moves.
  const std::vector<int> thirds = bar_intervals(0);
  EXPECT_GE(std::count_if(thirds.begin(), thirds.end(), [](int iv) { return iv >= 3; }), 2)
      << "broken-third bar lost its third vocabulary";
  // Dive bar: at least one fourth-or-larger leap.
  const std::vector<int> dive = bar_intervals(1);
  EXPECT_GE(std::count_if(dive.begin(), dive.end(), [](int iv) { return iv >= 4; }), 1)
      << "dive bar lost its leap";
  // Plain bar: conjunct -- steps dominate (anchor snaps may reach a third).
  const std::vector<int> plain = bar_intervals(2);
  EXPECT_GE(std::count_if(plain.begin(), plain.end(), [](int iv) { return iv <= 2; }),
            static_cast<long>(plain.size()) - 2)
      << "plain wave bar is no longer conjunct";
}

// --- kArpeggio ----------------------------------------------------------------

TEST(FigurationPaletteArpeggio, AllNotesAreTriadTonesInsideBand) {
  const std::vector<CycleBar> plan = majorCyclePlan();
  constexpr int kBandLo = 60;
  constexpr int kBandHi = 79;
  for (int figure = 0; figure < 4; ++figure) {
    for (int npb : {1, 2, 4}) {
      std::vector<MaterialNote> notes;
      appendArpeggioCycle(notes, 0, plan, kBandLo, kBandHi, figure, npb, detail::Mode::Major);
      ASSERT_EQ(notes.size(), plan.size() * 3 * static_cast<std::size_t>(npb));
      for (const MaterialNote& note : notes) {
        const int bar = static_cast<int>(note.start_tick / kTicksPerBar34);
        const CycleBar& cb = plan[static_cast<std::size_t>(bar)];
        const detail::ChordSpec chord{cb.root_pc, cb.minor};
        EXPECT_TRUE(isTriadTone(note.pitch, chord))
            << "figure " << figure << " npb " << npb << " tick " << note.start_tick;
        EXPECT_GE(static_cast<int>(note.pitch), kBandLo);
        EXPECT_LE(static_cast<int>(note.pitch), kBandHi);
      }
    }
  }
}

TEST(FigurationPaletteArpeggio, FigureContoursAreDistinct) {
  const std::vector<CycleBar> plan = majorCyclePlan();
  std::vector<std::vector<std::uint8_t>> first_beats;
  for (int figure = 0; figure < 4; ++figure) {
    std::vector<MaterialNote> notes;
    appendArpeggioCycle(notes, 0, plan, 60, 79, figure, 4, detail::Mode::Major);
    std::vector<std::uint8_t> head(4);
    for (int i = 0; i < 4; ++i)
      head[static_cast<std::size_t>(i)] = notes[static_cast<std::size_t>(i)].pitch;
    for (const auto& seen : first_beats)
      EXPECT_NE(seen, head);
    first_beats.push_back(head);
  }
}

// --- kFiguraCorta ---------------------------------------------------------------

TEST(FigurationPaletteFiguraCorta, LongShortShortCellFillsEveryBeat) {
  const std::vector<CycleBar> plan = majorCyclePlan();
  std::vector<MaterialNote> notes;
  appendFiguraCortaCycle(notes, 0, plan, /*register_shift=*/0, /*anchor_rotation=*/0,
                         detail::Mode::Major);
  ASSERT_EQ(notes.size(), plan.size() * 3 * 3);
  constexpr Tick kEighth = kTicksPerBeat / 2;
  constexpr Tick kSixteenth = kTicksPerBeat / 4;
  for (std::size_t i = 0; i < notes.size(); i += 3) {
    EXPECT_EQ(notes[i].duration, kEighth);
    EXPECT_EQ(notes[i + 1].duration, kSixteenth);
    EXPECT_EQ(notes[i + 2].duration, kSixteenth);
    EXPECT_EQ(notes[i].start_tick % kTicksPerBeat, 0);
    EXPECT_EQ(notes[i + 1].start_tick, notes[i].start_tick + kEighth);
    EXPECT_EQ(notes[i + 2].start_tick, notes[i].start_tick + kEighth + kSixteenth);
  }
}

TEST(FigurationPaletteFiguraCorta, DottedCellFillsEveryBeat) {
  std::vector<MaterialNote> notes;
  const detail::ChordSpec chord{0, false};
  appendFiguraCortaBar(notes, /*bar=*/0, /*start=*/60, chord, detail::Mode::Major,
                       /*figure=*/0, /*dotted=*/true);

  ASSERT_EQ(notes.size(), 8u);
  constexpr Tick kSixteenth = kTicksPerBeat / 4;
  for (std::size_t i = 0; i < notes.size(); i += 2) {
    EXPECT_EQ(notes[i].duration, 3 * kSixteenth);
    EXPECT_EQ(notes[i + 1].duration, kSixteenth);
    EXPECT_EQ(notes[i].start_tick % kTicksPerBeat, 0u);
    EXPECT_EQ(notes[i + 1].start_tick, notes[i].start_tick + 3 * kSixteenth);
    EXPECT_EQ(notes[i + 1].start_tick + notes[i + 1].duration, notes[i].start_tick + kTicksPerBeat);
  }
}

TEST(FigurationPaletteFiguraCorta, BeatOnsetsAreChordToneAnchorsAndCellsConjunct) {
  const std::vector<CycleBar> plan = majorCyclePlan();
  std::vector<MaterialNote> notes;
  appendFiguraCortaCycle(notes, 0, plan, 0, 1, detail::Mode::Major);
  for (std::size_t i = 0; i < notes.size(); ++i) {
    const MaterialNote& note = notes[i];
    if (note.start_tick % kTicksPerBeat == 0) {
      const int bar = static_cast<int>(note.start_tick / kTicksPerBar34);
      EXPECT_TRUE(
          isAnchorTone(note.pitch, plan[static_cast<std::size_t>(bar)], detail::Mode::Major))
          << "beat onset at tick " << note.start_tick;
    }
    if (i > 0 && notes[i].start_tick / kTicksPerBeat == notes[i - 1].start_tick / kTicksPerBeat) {
      // Cells stay within a third between consecutive notes: one diatonic step
      // for plain walks, two degrees across a turn or echappee bend (the bends
      // that keep a stalled walk from repeating a pitch).
      EXPECT_LE(std::abs(static_cast<int>(notes[i].pitch) - static_cast<int>(notes[i - 1].pitch)),
                4)
          << "intra-cell leap at tick " << notes[i].start_tick;
      EXPECT_NE(static_cast<int>(notes[i].pitch), static_cast<int>(notes[i - 1].pitch))
          << "intra-cell repeated pitch at tick " << notes[i].start_tick;
    }
  }
}

// --- kGesture --------------------------------------------------------------------

TEST(FigurationPaletteGesture, MordentOnsetThenDescendingRunThenRest) {
  const detail::ChordSpec chord{0, false};  // C major.
  constexpr int kBandLo = 48;
  constexpr int kBandHi = 84;
  std::vector<MaterialNote> dst;
  appendGestureBar(dst, /*bar=*/0, chord, detail::Mode::Major, kBandLo, kBandHi);
  ASSERT_GE(dst.size(), 3u);
  ASSERT_LE(dst.size(), 8u);
  // Mordent onset: upper diatonic neighbour, then the main (triad) tone.
  EXPECT_EQ(static_cast<int>(dst[0].pitch), detail::scaleUp(dst[1].pitch, 1, detail::Mode::Major));
  EXPECT_TRUE(isTriadTone(dst[1].pitch, chord));
  // Descending run after the mordent.
  for (std::size_t i = 2; i < dst.size(); ++i)
    EXPECT_LT(static_cast<int>(dst[i].pitch), static_cast<int>(dst[i - 1].pitch));
  // The rest of the bar is silent: emitted durations cover at most two beats.
  Tick covered = 0;
  for (const MaterialNote& note : dst) {
    covered += note.duration;
    EXPECT_GE(static_cast<int>(note.pitch), kBandLo);
    EXPECT_LE(static_cast<int>(note.pitch), kBandHi);
  }
  EXPECT_LE(covered, 2 * kTicksPerBeat);
  EXPECT_LE(dst.back().start_tick + dst.back().duration, static_cast<Tick>(2) * kTicksPerBeat);
}

TEST(FigurationPaletteGesture, RunStopsAtBandFloorInsteadOfRepeatingIt) {
  const detail::ChordSpec chord{0, false};
  // A floor right under the main tone leaves room for only a short run.
  std::vector<MaterialNote> dst;
  appendGestureBar(dst, 0, chord, detail::Mode::Major, /*band_lo=*/76, /*band_hi=*/84);
  for (std::size_t i = 1; i < dst.size(); ++i)
    EXPECT_NE(dst[i].pitch, dst[i - 1].pitch) << "repeated pitch at index " << i;
  for (const MaterialNote& note : dst)
    EXPECT_GE(static_cast<int>(note.pitch), 76);
}

// An octave drop lowers the whole gesture by exactly 12 per octave (the mordent
// and the descending run alike), preserving its shape one octave down.
TEST(FigurationPaletteGesture, OctaveDropLowersGestureByTwelve) {
  const detail::ChordSpec chord{0, false};  // C major.
  constexpr int kBandLo = 72;
  constexpr int kBandHi = 88;
  std::vector<MaterialNote> high;
  std::vector<MaterialNote> low;
  appendGestureBar(high, /*bar=*/0, chord, detail::Mode::Major, kBandLo, kBandHi,
                   /*octave_drop=*/0);
  appendGestureBar(low, /*bar=*/0, chord, detail::Mode::Major, kBandLo, kBandHi, /*octave_drop=*/1);
  ASSERT_EQ(high.size(), low.size());
  ASSERT_FALSE(high.empty());
  for (std::size_t i = 0; i < high.size(); ++i) {
    EXPECT_EQ(static_cast<int>(low[i].pitch), static_cast<int>(high[i].pitch) - 12)
        << "note " << i << " not an octave lower";
    EXPECT_EQ(low[i].start_tick, high[i].start_tick);
  }
}

// A drop that would sink the main-tone anchor below band_lo is pulled back to
// the lowest octave that still fits (here -24 clamps to -12).
TEST(FigurationPaletteGesture, OctaveDropClampsToLowestOctaveThatFits) {
  const detail::ChordSpec chord{0, false};
  constexpr int kBandLo = 72;
  constexpr int kBandHi = 88;
  std::vector<MaterialNote> drop_one;
  std::vector<MaterialNote> drop_two;
  appendGestureBar(drop_one, 0, chord, detail::Mode::Major, kBandLo, kBandHi, /*octave_drop=*/1);
  appendGestureBar(drop_two, 0, chord, detail::Mode::Major, kBandLo, kBandHi, /*octave_drop=*/2);
  ASSERT_EQ(drop_one.size(), drop_two.size());
  // The main-tone anchor (second note, after the mordent upper neighbour) stays
  // at or above band_lo, so the -24 request collapses onto the -12 gesture.
  for (std::size_t i = 0; i < drop_one.size(); ++i)
    EXPECT_EQ(drop_two[i].pitch, drop_one[i].pitch);
  EXPECT_GE(static_cast<int>(drop_two[1].pitch), kBandLo);
}

// --- kChordBlock -------------------------------------------------------------------

TEST(FigurationPaletteChordBlock, StackedTriadKeepsVoiceOrderForHalfAndWholeBlocks) {
  const detail::ChordSpec chord{0, false};
  for (Tick block_dur :
       {static_cast<Tick>(2) * kTicksPerBeat, static_cast<Tick>(4) * kTicksPerBeat}) {
    std::vector<std::vector<MaterialNote>> voices(3);
    appendChordBlockBar(voices, /*bar=*/0, chord, detail::Mode::Major, /*top_hi=*/76, block_dur);
    const std::size_t blocks = static_cast<std::size_t>(kTicksPerBar / block_dur);
    for (const auto& voice : voices)
      ASSERT_EQ(voice.size(), blocks);
    for (std::size_t blk = 0; blk < blocks; ++blk) {
      // Voice order: V0 > V1 > V2, all striking together with the block length.
      EXPECT_GT(voices[0][blk].pitch, voices[1][blk].pitch);
      EXPECT_GT(voices[1][blk].pitch, voices[2][blk].pitch);
      for (const auto& voice : voices) {
        EXPECT_TRUE(isTriadTone(voice[blk].pitch, chord));
        EXPECT_EQ(voice[blk].duration, block_dur);
        EXPECT_EQ(voice[blk].start_tick, static_cast<Tick>(blk) * block_dur);
      }
    }
    EXPECT_LE(static_cast<int>(voices[0][0].pitch), 76);
  }
}

// --- kPedalWalk --------------------------------------------------------------------

TEST(FigurationPalettePedalWalk, QuartersAlternateRootAndFifthInsideBand) {
  const detail::ChordSpec tonic{0, false};
  const detail::ChordSpec subdominant{5, false};
  constexpr int kBandLo = 28;
  constexpr int kBandHi = 50;
  std::vector<MaterialNote> dst;
  int prev_pitch = -1;
  appendPedalWalkBar(dst, 0, tonic, detail::Mode::Major, kBandLo, kBandHi, prev_pitch);
  appendPedalWalkBar(dst, 1, subdominant, detail::Mode::Major, kBandLo, kBandHi, prev_pitch);
  ASSERT_EQ(dst.size(), 8u);
  for (std::size_t i = 0; i < dst.size(); ++i) {
    const detail::ChordSpec& chord = (i < 4) ? tonic : subdominant;
    const int pc = dst[i].pitch % 12;
    const bool on_root = pc == chord.root_pc % 12;
    const bool on_fifth = pc == (chord.root_pc + 7) % 12;
    EXPECT_TRUE(on_root || on_fifth) << "pitch class " << pc << " at index " << i;
    EXPECT_EQ(dst[i].duration, kTicksPerBeat);
    EXPECT_GE(static_cast<int>(dst[i].pitch), kBandLo);
    EXPECT_LE(static_cast<int>(dst[i].pitch), kBandHi);
    if (i > 0) {
      EXPECT_LE(std::abs(static_cast<int>(dst[i].pitch) - static_cast<int>(dst[i - 1].pitch)), 12)
          << "walking leap at index " << i;
    }
  }
  EXPECT_EQ(prev_pitch, static_cast<int>(dst.back().pitch));
}

}  // namespace
}  // namespace bach::composer
