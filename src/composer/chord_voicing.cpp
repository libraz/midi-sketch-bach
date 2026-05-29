#include "composer/chord_voicing.h"

namespace bach::composer {

namespace {

void triadOffsets(ChordQuality quality, std::uint8_t* third, std::uint8_t* fifth) {
  switch (quality) {
    case ChordQuality::Major:
    case ChordQuality::Major7:
    case ChordQuality::Dominant7:
      *third = 4;
      *fifth = 7;
      return;
    case ChordQuality::Minor:
    case ChordQuality::Minor7:
      *third = 3;
      *fifth = 7;
      return;
    case ChordQuality::Diminished:
    case ChordQuality::HalfDiminished7:
    case ChordQuality::Diminished7:
      *third = 3;
      *fifth = 6;
      return;
    case ChordQuality::Augmented:
      *third = 4;
      *fifth = 8;
      return;
  }
  *third = 4;
  *fifth = 7;
}

}  // namespace

std::array<std::uint8_t, 4> chordPitchClasses(const ChordEvent& chord, std::size_t* out_count) {
  std::array<std::uint8_t, 4> pcs = {0, 0, 0, 0};
  std::uint8_t third = 4;
  std::uint8_t fifth = 7;
  triadOffsets(chord.quality, &third, &fifth);
  pcs[0] = static_cast<std::uint8_t>(chord.root_pc % 12);
  pcs[1] = static_cast<std::uint8_t>((chord.root_pc + third) % 12);
  pcs[2] = static_cast<std::uint8_t>((chord.root_pc + fifth) % 12);
  std::size_t count = 3;
  if (hasSeventh(chord.quality)) {
    pcs[3] = static_cast<std::uint8_t>((chord.root_pc + seventhOffset(chord.quality)) % 12);
    count = 4;
  }
  if (out_count != nullptr)
    *out_count = count;
  return pcs;
}

bool hasSeventh(ChordQuality quality) {
  return quality == ChordQuality::Major7 || quality == ChordQuality::Minor7 ||
         quality == ChordQuality::HalfDiminished7 || quality == ChordQuality::Diminished7 ||
         quality == ChordQuality::Dominant7;
}

std::uint8_t seventhOffset(ChordQuality quality) {
  switch (quality) {
    case ChordQuality::Major7:
      return 11;
    case ChordQuality::Minor7:
    case ChordQuality::HalfDiminished7:
    case ChordQuality::Dominant7:
      return 10;
    case ChordQuality::Diminished7:
      return 9;
    default:
      return 0;
  }
}

std::uint8_t bassPitchClassFor(const ChordEvent& chord) {
  std::size_t count = 0;
  const auto pcs = chordPitchClasses(chord, &count);
  switch (chord.inversion) {
    case ChordInversion::Root:
      return pcs[0];
    case ChordInversion::First:
      return pcs[1];
    case ChordInversion::Second:
      return pcs[2];
    case ChordInversion::Third:
      return (count >= 4) ? pcs[3] : pcs[0];
  }
  return pcs[0];
}

std::uint8_t leadingTonePitchClass(std::uint8_t tonic_pc) {
  return static_cast<std::uint8_t>((static_cast<int>(tonic_pc % 12) + 11) % 12);
}

bool isLeadingTonePc(std::uint8_t pc, std::uint8_t tonic_pc) {
  return static_cast<std::uint8_t>(pc % 12) == leadingTonePitchClass(tonic_pc);
}

SixFourType classifySixFour(const ChordEvent* prev, const ChordEvent& six_four,
                            const ChordEvent* next) {
  if (next != nullptr && next->has_degree && next->degree == RomanNumeral::V) {
    return SixFourType::Cadential;
  }
  const std::uint8_t bass_pc = bassPitchClassFor(six_four);
  const bool prev_bass_steps_in =
      (prev != nullptr) && (((bass_pc + 12 - bassPitchClassFor(*prev)) % 12) <= 2 ||
                            ((bassPitchClassFor(*prev) + 12 - bass_pc) % 12) <= 2);
  const bool next_bass_steps_out =
      (next != nullptr) && (((bassPitchClassFor(*next) + 12 - bass_pc) % 12) <= 2 ||
                            ((bass_pc + 12 - bassPitchClassFor(*next)) % 12) <= 2);
  const bool prev_bass_same = (prev != nullptr) && (bassPitchClassFor(*prev) == bass_pc);
  const bool next_bass_same = (next != nullptr) && (bassPitchClassFor(*next) == bass_pc);

  if (prev_bass_same && next_bass_same) {
    return SixFourType::Neighboring;
  }
  if (prev_bass_steps_in && next_bass_steps_out) {
    return SixFourType::Passing;
  }
  return SixFourType::Cadential;
}

}  // namespace bach::composer
