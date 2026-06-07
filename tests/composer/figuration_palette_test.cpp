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

// --- kScalarWave (3/4 cycle form) ---------------------------------------------

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
      // Conjunct surface: one diatonic step (<= 2 semitones) per note. A bar
      // downbeat may combine the wave advance with the chord-tone re-anchor
      // step (two diatonic steps, <= 4 semitones), never a genuine leap.
      const bool bar_head = notes[i].start_tick % kTicksPerBar34 == 0;
      EXPECT_LE(std::abs(static_cast<int>(notes[i].pitch) - static_cast<int>(notes[i - 1].pitch)),
                bar_head ? 4 : 2)
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

}  // namespace
}  // namespace bach::composer
