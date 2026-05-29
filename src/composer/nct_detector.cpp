#include "composer/nct_detector.h"

#include <array>
#include <cstdlib>

namespace bach::composer::nct_detector {

namespace {

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

const ChordEvent& activeChord(const HarmonicPlan& plan, Tick at) {
  const ChordEvent* current = &plan.chords.front();
  for (const auto& chord : plan.chords) {
    if (chord.start_tick <= at) {
      current = &chord;
    } else {
      break;
    }
  }
  return *current;
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
    if (!isChordTone(n0.pitch, activeChord(harmonic_plan, n0.start_tick)))
      continue;
    if (!isChordTone(n4.pitch, activeChord(harmonic_plan, n4.start_tick)))
      continue;
    // n1: step-down NCT (must be lower than n0 by 1-2 semis and NOT a
    // chord tone of its own chord).
    if (n1.pitch >= n0.pitch || !isStep(n0.pitch, n1.pitch))
      continue;
    if (isChordTone(n1.pitch, activeChord(harmonic_plan, n1.start_tick)))
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
    if (!isChordTone(n0.pitch, activeChord(harmonic_plan, n0.start_tick)))
      continue;
    if (!isChordTone(n2.pitch, activeChord(harmonic_plan, n2.start_tick)))
      continue;
    // n1: step-up NCT from n0.
    if (n1.pitch <= n0.pitch || !isStep(n0.pitch, n1.pitch))
      continue;
    if (isChordTone(n1.pitch, activeChord(harmonic_plan, n1.start_tick)))
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
    if (!isChordTone(n0.pitch, activeChord(harmonic_plan, n0.start_tick)))
      continue;
    // n1: step-down NCT relative to its own chord.
    if (n1.pitch >= n0.pitch || !isStep(n0.pitch, n1.pitch))
      continue;
    if (isChordTone(n1.pitch, activeChord(harmonic_plan, n1.start_tick)))
      continue;
    // Anticipation: n1's pitch identically equals a chord-tone of the
    // chord active at i+2 (the next strong-beat chord). Requires
    // i + 2 < notes.size().
    if (i + 2 >= notes.size())
      continue;
    const auto& n2 = notes[i + 2];
    if (n1.pitch != n2.pitch)
      continue;
    if (!isChordTone(n2.pitch, activeChord(harmonic_plan, n2.start_tick)))
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
    if (!isChordTone(n0.pitch, activeChord(harmonic_plan, n0.start_tick)))
      continue;
    if (!isChordTone(n3.pitch, activeChord(harmonic_plan, n3.start_tick)))
      continue;
    // n1: step-down NCT from n0.
    if (n1.pitch >= n0.pitch || !isStep(n0.pitch, n1.pitch))
      continue;
    if (isChordTone(n1.pitch, activeChord(harmonic_plan, n1.start_tick)))
      continue;
    // n2: leap-down NCT from n1 (and not a chord tone).
    if (n2.pitch >= n1.pitch || !isLeap(n1.pitch, n2.pitch))
      continue;
    if (isChordTone(n2.pitch, activeChord(harmonic_plan, n2.start_tick)))
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
