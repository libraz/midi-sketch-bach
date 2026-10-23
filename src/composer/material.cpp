#include "composer/material.h"

#include <array>

#include "composer/rule_helpers.h"

namespace bach::composer {

namespace {

std::uint8_t pitchClass(std::uint8_t pitch) {
  return static_cast<std::uint8_t>(pitch % 12);
}

detail::Mode modeOf(const KeyContext& key) {
  return key.is_minor ? detail::Mode::Minor : detail::Mode::Major;
}

// Semitone offsets of the seven degrees above the tonic, with no harmony: minor
// is the natural minor (detail::spellScale).
detail::ScaleSpelling degreesOf(const KeyContext& key) {
  return detail::spellScale(0, modeOf(key), 0);
}

// Euclidean division, so a pitch below the tonic still resolves to the degree
// it occupies rather than to a negative remainder.
int floorDiv(int value, int divisor) {
  const int quotient = value / divisor;
  return (value % divisor != 0 && ((value < 0) != (divisor < 0))) ? quotient - 1 : quotient;
}

std::uint8_t leadingTonePc(std::uint8_t tonic_pc, bool /*is_minor*/) {
  return static_cast<std::uint8_t>((tonic_pc + 11) % 12);
}

void annotateFragment(std::vector<LeadingToneMarker>& markers,
                      const std::vector<MaterialNote>& notes, MaterialFragment fragment,
                      std::uint8_t tonic_pc, bool is_minor) {
  const std::uint8_t leading_pc = leadingTonePc(tonic_pc, is_minor);
  const std::uint8_t tonic = static_cast<std::uint8_t>(tonic_pc % 12);
  for (std::size_t i = 0; i + 1 < notes.size(); ++i) {
    const MaterialNote& current = notes[i];
    const MaterialNote& next = notes[i + 1];
    if (pitchClass(current.pitch) != leading_pc)
      continue;
    if (pitchClass(next.pitch) != tonic)
      continue;
    if (next.pitch <= current.pitch)
      continue;
    LeadingToneMarker marker;
    marker.fragment = fragment;
    marker.leading_index = i;
    marker.resolution_index = i + 1;
    marker.leading_tick = current.start_tick;
    marker.resolution_tick = next.start_tick;
    marker.leading_pitch = current.pitch;
    marker.resolution_pitch = next.pitch;
    marker.tonic_pc = tonic;
    markers.push_back(marker);
  }
}

}  // namespace

KeyContext localKeyAt(const HarmonicPlan& plan, Tick tick) {
  return rule_helpers::keyAt(plan, tick);
}

bool inKey(int pitch, const KeyContext& key) {
  return degreeInKey(pitch, key) >= 0;
}

int degreeInKey(int pitch, const KeyContext& key) {
  const int offset = ((pitch - static_cast<int>(key.tonic_pc)) % 12 + 12) % 12;
  const detail::ScaleSpelling degrees = degreesOf(key);
  for (int degree = 0; degree < 7; ++degree) {
    if (degrees[static_cast<std::size_t>(degree)] == offset)
      return degree;
  }
  return -1;
}

int transposeIntoKey(int pitch, const KeyContext& from, const KeyContext& to) {
  const int relative = pitch - static_cast<int>(from.tonic_pc);
  const int octave = floorDiv(relative, 12);
  const int offset = relative - 12 * octave;
  const detail::ScaleSpelling source = degreesOf(from);
  const detail::ScaleSpelling target = degreesOf(to);
  // Walk down to the degree at or below the pitch and carry whatever semitone
  // separates them, so a chromatic tone stays an inflection of the same degree
  // instead of collapsing onto a diatonic neighbour.
  int degree = 6;
  while (degree > 0 && source[static_cast<std::size_t>(degree)] > offset)
    --degree;
  const int inflection = offset - source[static_cast<std::size_t>(degree)];
  return static_cast<int>(to.tonic_pc) + 12 * octave + target[static_cast<std::size_t>(degree)] +
         inflection;
}

int bendIntoKey(int pitch, const KeyContext& key, detail::PcMask chord_pcs) {
  const auto member = [&](int candidate) {
    return detail::inSpelledScale(candidate, key.tonic_pc, modeOf(key), chord_pcs);
  };
  if (member(pitch))
    return pitch;
  // A spelled scale leaves at most a three-semitone gap (only where both ends are
  // chord tones), so the search settles within that distance. Upward is tried
  // first, which spells a raised leading tone rather than flattening onto the
  // degree below it.
  for (int distance = 1; distance <= 3; ++distance) {
    if (member(pitch + distance))
      return pitch + distance;
    if (member(pitch - distance))
      return pitch - distance;
  }
  return pitch;
}

void annotateLeadingToneMarkers(Material& material, std::uint8_t tonic_pc, bool is_minor) {
  material.leading_tone_markers.clear();
  annotateFragment(material.leading_tone_markers, material.subject, MaterialFragment::Subject,
                   tonic_pc, is_minor);
  annotateFragment(material.leading_tone_markers, material.answer, MaterialFragment::Answer,
                   tonic_pc, is_minor);
}

void annotateCadenceCells(Material& material, const HarmonicPlan& harmonic_plan) {
  material.cadence_cells.clear();
  const std::uint8_t tonic = static_cast<std::uint8_t>(harmonic_plan.tonic_pc % 12);
  for (const auto& cadence : harmonic_plan.cadences) {
    CadenceCell cell;
    cell.type = cadence.type;
    cell.cadence_tick = cadence.tick;
    cell.approach_tick = (cadence.tick >= kTicksPerBeat) ? cadence.tick - kTicksPerBeat : 0;

    const std::uint8_t major_third = static_cast<std::uint8_t>((tonic + 4) % 12);
    const std::uint8_t minor_third = static_cast<std::uint8_t>((tonic + 3) % 12);
    const std::uint8_t third_above_tonic = harmonic_plan.is_minor ? minor_third : major_third;

    switch (cadence.type) {
      case CadenceType::Perfect:
        cell.soprano_approach_pc = static_cast<std::uint8_t>((tonic + 11) % 12);
        cell.soprano_cadence_pc = tonic;
        cell.bass_approach_pc = static_cast<std::uint8_t>((tonic + 7) % 12);
        cell.bass_cadence_pc = tonic;
        break;
      case CadenceType::ImperfectAuthentic:
        cell.soprano_approach_pc = static_cast<std::uint8_t>((tonic + 2) % 12);
        cell.soprano_cadence_pc = third_above_tonic;
        cell.bass_approach_pc = static_cast<std::uint8_t>((tonic + 7) % 12);
        cell.bass_cadence_pc = tonic;
        break;
      case CadenceType::PicardyThird:
        // The soprano steps onto the raised third, so the cell sounds the
        // Picardy third itself whatever the texture's voice count.
        cell.soprano_approach_pc = static_cast<std::uint8_t>((tonic + 2) % 12);
        cell.soprano_cadence_pc = major_third;
        cell.bass_approach_pc = static_cast<std::uint8_t>((tonic + 7) % 12);
        cell.bass_cadence_pc = tonic;
        break;
      case CadenceType::Plagal:
        cell.soprano_approach_pc = static_cast<std::uint8_t>((tonic + 5) % 12);
        cell.soprano_cadence_pc = third_above_tonic;
        cell.bass_approach_pc = static_cast<std::uint8_t>((tonic + 5) % 12);
        cell.bass_cadence_pc = tonic;
        break;
      case CadenceType::Half:
        cell.soprano_approach_pc = third_above_tonic;
        cell.soprano_cadence_pc = static_cast<std::uint8_t>((tonic + 2) % 12);
        cell.bass_approach_pc = tonic;
        cell.bass_cadence_pc = static_cast<std::uint8_t>((tonic + 7) % 12);
        break;
      case CadenceType::Deceptive:
        cell.soprano_approach_pc = static_cast<std::uint8_t>((tonic + 11) % 12);
        cell.soprano_cadence_pc = tonic;
        cell.bass_approach_pc = static_cast<std::uint8_t>((tonic + 7) % 12);
        cell.bass_cadence_pc =
            static_cast<std::uint8_t>((tonic + (harmonic_plan.is_minor ? 8 : 9)) % 12);
        break;
      case CadenceType::Phrygian:
        // iv6 -> V: soprano 4 -> 5 over the half-step bass b6 -> 5.
        cell.soprano_approach_pc = static_cast<std::uint8_t>((tonic + 5) % 12);
        cell.soprano_cadence_pc = static_cast<std::uint8_t>((tonic + 7) % 12);
        cell.bass_approach_pc = static_cast<std::uint8_t>((tonic + 8) % 12);
        cell.bass_cadence_pc = static_cast<std::uint8_t>((tonic + 7) % 12);
        break;
    }
    material.cadence_cells.push_back(cell);
  }
}

}  // namespace bach::composer
