#include "composer/nct_detector.h"

#include <array>
#include <cstdlib>

#include "composer/rule_helpers.h"

namespace bach::composer::nct_detector {

namespace {

using rule_helpers::activeChord;

std::array<std::uint8_t, 3> triadPitchClasses(const ChordEvent& chord) {
  std::uint8_t third_offset = 4;
  std::uint8_t fifth_offset = 7;
  switch (chord.quality) {
    case ChordQuality::Major:
    case ChordQuality::Major7:
    case ChordQuality::Dominant7:
      third_offset = 4;
      fifth_offset = 7;
      break;
    case ChordQuality::Minor:
    case ChordQuality::Minor7:
      third_offset = 3;
      fifth_offset = 7;
      break;
    case ChordQuality::Diminished:
    case ChordQuality::HalfDiminished7:
    case ChordQuality::Diminished7:
      third_offset = 3;
      fifth_offset = 6;
      break;
    case ChordQuality::Augmented:
      third_offset = 4;
      fifth_offset = 8;
      break;
  }
  return {
      static_cast<std::uint8_t>(chord.root_pc % 12),
      static_cast<std::uint8_t>((chord.root_pc + third_offset) % 12),
      static_cast<std::uint8_t>((chord.root_pc + fifth_offset) % 12),
  };
}

bool isChordTone(std::uint8_t pitch, const ChordEvent& chord) {
  const auto triad = triadPitchClasses(chord);
  const std::uint8_t pc = static_cast<std::uint8_t>(pitch % 12);
  return pc == triad[0] || pc == triad[1] || pc == triad[2];
}

bool isStep(std::uint8_t a, std::uint8_t b) {
  const int d = std::abs(static_cast<int>(a) - static_cast<int>(b));
  return d == 1 || d == 2;
}

bool isLeap(std::uint8_t a, std::uint8_t b) {
  return std::abs(static_cast<int>(a) - static_cast<int>(b)) >= 3;
}

// Temporal contiguity guard (M4). Two notes adjacent in the voice vector
// are only melodically contiguous when the earlier note ends exactly where
// the later note begins. A rest/gap (or an overlap, or a backwards/
// disordered pair) breaks the melodic window, so the figure must be
// rejected. Used for the purely melodic figures (cambiata / echappee /
// nota cambiata) whose neighbourhood notes run back-to-back.
bool isAdjacent(const MaterialNote& earlier, const MaterialNote& later) {
  return earlier.start_tick + earlier.duration == later.start_tick;
}

}  // namespace

std::vector<NctHit> detectCambiata(const std::vector<MaterialNote>& notes,
                                   const HarmonicPlan& harmonic_plan) {
  std::vector<NctHit> hits;
  if (harmonic_plan.chords.empty())
    return hits;
  for (std::size_t i = 0; i + 4 < notes.size(); ++i) {
    const auto& n0 = notes[i];
    const auto& n1 = notes[i + 1];
    const auto& n2 = notes[i + 2];
    const auto& n3 = notes[i + 3];
    const auto& n4 = notes[i + 4];
    // M4: the five notes must be melodically back-to-back (no rest/gap).
    if (!isAdjacent(n0, n1) || !isAdjacent(n1, n2) || !isAdjacent(n2, n3) || !isAdjacent(n3, n4))
      continue;
    // M5: anchor the whole figure's chord-tone / NCT classification to the
    // chord active at the figure's first note.
    const ChordEvent& anchor = activeChord(harmonic_plan, n0.start_tick);
    if (!isChordTone(n0.pitch, anchor))
      continue;
    if (!isChordTone(n4.pitch, anchor))
      continue;
    // n1: step-down NCT (must be lower than n0 by 1-2 semis and NOT a
    // chord tone of the anchor chord).
    if (n1.pitch >= n0.pitch || !isStep(n0.pitch, n1.pitch))
      continue;
    if (isChordTone(n1.pitch, anchor))
      continue;
    // n2: leap-down from n1.
    if (n2.pitch >= n1.pitch || !isLeap(n1.pitch, n2.pitch))
      continue;
    // n3: step-up from n2.
    if (n3.pitch <= n2.pitch || !isStep(n2.pitch, n3.pitch))
      continue;
    // n4: step-up from n3 (the closing chord-tone).
    if (n4.pitch <= n3.pitch || !isStep(n3.pitch, n4.pitch))
      continue;
    NctHit hit;
    hit.figure = NctFigure::Cambiata;
    hit.nct_index = i + 1;
    hit.window_start = i;
    hit.window_end = i + 5;
    hits.push_back(hit);
  }
  return hits;
}

std::vector<NctHit> detectEchappee(const std::vector<MaterialNote>& notes,
                                   const HarmonicPlan& harmonic_plan) {
  std::vector<NctHit> hits;
  if (harmonic_plan.chords.empty())
    return hits;
  for (std::size_t i = 0; i + 2 < notes.size(); ++i) {
    const auto& n0 = notes[i];
    const auto& n1 = notes[i + 1];
    const auto& n2 = notes[i + 2];
    // M4: the three notes must be melodically back-to-back (no rest/gap).
    if (!isAdjacent(n0, n1) || !isAdjacent(n1, n2))
      continue;
    // M5: anchor the whole figure's classification to the first note's chord.
    const ChordEvent& anchor = activeChord(harmonic_plan, n0.start_tick);
    if (!isChordTone(n0.pitch, anchor))
      continue;
    if (!isChordTone(n2.pitch, anchor))
      continue;
    // n1: step-up NCT from n0.
    if (n1.pitch <= n0.pitch || !isStep(n0.pitch, n1.pitch))
      continue;
    if (isChordTone(n1.pitch, anchor))
      continue;
    // n2: leap-down from n1.
    if (n2.pitch >= n1.pitch || !isLeap(n1.pitch, n2.pitch))
      continue;
    NctHit hit;
    hit.figure = NctFigure::Echappee;
    hit.nct_index = i + 1;
    hit.window_start = i;
    hit.window_end = i + 3;
    hits.push_back(hit);
  }
  return hits;
}

std::vector<NctHit> detectAnticipation(const std::vector<MaterialNote>& notes,
                                       const HarmonicPlan& harmonic_plan) {
  std::vector<NctHit> hits;
  if (harmonic_plan.chords.empty())
    return hits;
  for (std::size_t i = 0; i + 1 < notes.size(); ++i) {
    const auto& n0 = notes[i];
    const auto& n1 = notes[i + 1];
    // M4: n0 and n1 are the melodic approach; they must run forward in time
    // without overlap. (Unlike the purely melodic figures, an anticipation
    // is, by definition, temporally separated from its resolution, so the
    // n1 -> resolution leg is not required to be back-to-back.)
    if (n0.start_tick + n0.duration > n1.start_tick)
      continue;
    // M5: anchor n0 / n1 classification to the chord active at n0.
    const ChordEvent& anchor = activeChord(harmonic_plan, n0.start_tick);
    if (!isChordTone(n0.pitch, anchor))
      continue;
    // n1: step-down NCT relative to the anchor chord.
    if (n1.pitch >= n0.pitch || !isStep(n0.pitch, n1.pitch))
      continue;
    if (isChordTone(n1.pitch, anchor))
      continue;
    // Anticipation: n1's pitch identically equals a chord-tone of the
    // chord active at i+2 (the next strong-beat chord). Requires
    // i + 2 < notes.size().
    if (i + 2 >= notes.size())
      continue;
    const auto& n2 = notes[i + 2];
    if (n1.pitch != n2.pitch)
      continue;
    const ChordEvent& resolution_chord = activeChord(harmonic_plan, n2.start_tick);
    if (!isChordTone(n2.pitch, resolution_chord))
      continue;
    // M6: the figure must anticipate the NEXT chord, so a real chord change
    // must separate the anticipation note from its resolution. Reject when
    // n1 and n2 fall under the same chord event.
    if (activeChord(harmonic_plan, n1.start_tick).start_tick == resolution_chord.start_tick)
      continue;
    NctHit hit;
    hit.figure = NctFigure::Anticipation;
    hit.nct_index = i + 1;
    hit.window_start = i;
    hit.window_end = i + 3;
    hits.push_back(hit);
  }
  return hits;
}

std::vector<NctHit> detectNotaCambiata(const std::vector<MaterialNote>& notes,
                                       const HarmonicPlan& harmonic_plan) {
  std::vector<NctHit> hits;
  if (harmonic_plan.chords.empty())
    return hits;
  for (std::size_t i = 0; i + 3 < notes.size(); ++i) {
    const auto& n0 = notes[i];
    const auto& n1 = notes[i + 1];
    const auto& n2 = notes[i + 2];
    const auto& n3 = notes[i + 3];
    // M4: the four notes must be melodically back-to-back (no rest/gap).
    if (!isAdjacent(n0, n1) || !isAdjacent(n1, n2) || !isAdjacent(n2, n3))
      continue;
    // M5: anchor the whole figure's classification to the first note's chord.
    const ChordEvent& anchor = activeChord(harmonic_plan, n0.start_tick);
    if (!isChordTone(n0.pitch, anchor))
      continue;
    if (!isChordTone(n3.pitch, anchor))
      continue;
    // n1: step-down NCT from n0.
    if (n1.pitch >= n0.pitch || !isStep(n0.pitch, n1.pitch))
      continue;
    if (isChordTone(n1.pitch, anchor))
      continue;
    // n2: leap-down NCT from n1 (and not a chord tone of the anchor).
    if (n2.pitch >= n1.pitch || !isLeap(n1.pitch, n2.pitch))
      continue;
    if (isChordTone(n2.pitch, anchor))
      continue;
    // n3: step-up chord-tone closing.
    if (n3.pitch <= n2.pitch || !isStep(n2.pitch, n3.pitch))
      continue;
    NctHit hit;
    hit.figure = NctFigure::NotaCambiata;
    hit.nct_index = i + 1;
    hit.window_start = i;
    hit.window_end = i + 4;
    hits.push_back(hit);
  }
  return hits;
}

}  // namespace bach::composer::nct_detector
