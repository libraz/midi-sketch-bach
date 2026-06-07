#include "composer/figuration_palette.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdlib>
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
  // i.e. at most 2 semitones between consecutive notes of the same beat.
  for (std::size_t i = 1; i < notes.size(); ++i) {
    if (notes[i].start_tick / kTicksPerBeat != notes[i - 1].start_tick / kTicksPerBeat)
      continue;
    EXPECT_LE(std::abs(static_cast<int>(notes[i].pitch) - static_cast<int>(notes[i - 1].pitch)), 2)
        << "intra-beat fill leap at tick " << notes[i].start_tick;
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

// Every beat onset of the wave must be one of the bar's anchor pitch classes
// (consonant with the held ground). The free-running wave once anchored only
// bar downbeats, which left non-chord tones on the beats of entire sixteenth-
// tier cycles -- audible as sustained dissonance over the ground.
TEST(FigurationPaletteScalarWave, BeatOnsetsAreAnchorTones) {
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
        continue;  // only beat onsets are anchor-guaranteed.
      const int bar = static_cast<int>(note.start_tick / kTicksPerBar34);
      EXPECT_TRUE(
          isAnchorTone(note.pitch, minor_plan[static_cast<std::size_t>(bar)], detail::Mode::Minor))
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
      EXPECT_LE(std::abs(static_cast<int>(notes[i].pitch) - static_cast<int>(notes[i - 1].pitch)),
                2)
          << "intra-cell leap at tick " << notes[i].start_tick;
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
