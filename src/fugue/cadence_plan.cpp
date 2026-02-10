// Implementation of cadence plan generation and application.

#include "fugue/cadence_plan.h"

namespace bach {

CadencePlan CadencePlan::createForFugue(const FugueStructure& structure,
                                         const KeySignature& home_key,
                                         bool is_minor) {
  CadencePlan plan;

  if (structure.sections.empty()) return plan;

  const auto& sections = structure.sections;
  int num_episodes = 0;
  int total_episodes = 0;

  // Count total episodes for determining which is final.
  for (const auto& sec : sections) {
    if (sec.type == SectionType::Episode) ++total_episodes;
  }

  for (size_t idx = 0; idx < sections.size(); ++idx) {
    const auto& section = sections[idx];
    KeySignature section_key;
    section_key.tonic = section.key;
    section_key.is_minor = is_minor;

    switch (section.type) {
      case SectionType::Exposition: {
        // Exposition end: Half cadence to drive into development.
        CadencePoint cp;
        cp.tick = section.end_tick - kTicksPerBar;
        cp.type = CadenceType::Half;
        cp.key = section_key;
        plan.points.push_back(cp);
        break;
      }

      case SectionType::Episode: {
        ++num_episodes;
        CadencePoint cp;
        cp.tick = section.end_tick - kTicksPerBar;
        cp.key = section_key;

        if (num_episodes == total_episodes) {
          // Final episode (return to home key): Perfect cadence.
          cp.type = CadenceType::Perfect;
          cp.key = home_key;
        } else {
          // Interior episodes: Half cadence to prepare next key.
          cp.type = CadenceType::Half;
        }
        plan.points.push_back(cp);
        break;
      }

      case SectionType::Stretto: {
        // Check if there's a section before stretto (pre-stretto boundary).
        if (idx > 0) {
          CadencePoint deceptive;
          deceptive.tick = section.start_tick;
          deceptive.type = CadenceType::Deceptive;
          deceptive.key = home_key;
          plan.points.push_back(deceptive);
        }
        break;
      }

      case SectionType::Coda: {
        // Final cadence: Perfect (V7->I).
        CadencePoint cp;
        cp.tick = section.start_tick;
        cp.type = CadenceType::Perfect;
        cp.key = home_key;
        plan.points.push_back(cp);

        // In minor keys: Picardy third on the very last chord.
        if (is_minor) {
          CadencePoint picardy;
          picardy.tick = section.end_tick - kTicksPerBeat;
          picardy.type = CadenceType::PicardyThird;
          picardy.key = home_key;
          plan.points.push_back(picardy);
        }
        break;
      }

      case SectionType::MiddleEntry:
        // No cadence at middle entries (they emerge from episodes).
        break;
    }
  }

  return plan;
}

void CadencePlan::applyTo(HarmonicTimeline& timeline) const {
  for (const auto& cp : points) {
    timeline.applyCadence(cp.type, cp.key);
  }
}

}  // namespace bach
