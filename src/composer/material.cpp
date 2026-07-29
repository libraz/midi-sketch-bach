#include "composer/material.h"

namespace bach::composer {

namespace {

std::uint8_t pitchClass(std::uint8_t pitch) {
  return static_cast<std::uint8_t>(pitch % 12);
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
        cell.soprano_approach_pc = static_cast<std::uint8_t>((tonic + 11) % 12);
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
        cell.soprano_approach_pc = static_cast<std::uint8_t>((tonic + 1) % 12);
        cell.soprano_cadence_pc = static_cast<std::uint8_t>((tonic + 2) % 12);
        cell.bass_approach_pc = static_cast<std::uint8_t>((tonic + 8) % 12);
        cell.bass_cadence_pc = static_cast<std::uint8_t>((tonic + 7) % 12);
        break;
    }
    material.cadence_cells.push_back(cell);
  }
}

}  // namespace bach::composer
