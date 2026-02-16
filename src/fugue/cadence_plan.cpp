// Implementation of cadence plan generation and application.

#include "fugue/cadence_plan.h"

#include <optional>

#include "fugue/cadence_vocabulary.h"

namespace bach {

namespace {

/// @brief Try to match a CadenceContextRule based on structural context.
///
/// Scans kCadenceContextRules[] in priority order and returns the first matching
/// CadenceType. Harmonic conditions (requires_dominant_preparation,
/// requires_tonic_stability) are deferred to Phase D when HarmonicTimeline is
/// available; for now these checks always pass.
///
/// @param section_type The current section's type.
/// @param structure The complete fugue structure (used for lookahead).
/// @param section_idx Index of the current section in structure.sections.
/// @param num_episodes Number of episodes seen so far (including current if episode).
/// @param total_episodes Total episode count in the fugue.
/// @param last_pac_tick Tick of the most recent perfect cadence (0 if none).
/// @param total_duration Total fugue duration in ticks.
/// @return The matched CadenceType, or nullopt if no rule matches.
std::optional<CadenceType> matchContextRule(SectionType section_type,
                                            const FugueStructure& structure,
                                            size_t section_idx,
                                            int num_episodes,
                                            int total_episodes,
                                            Tick last_pac_tick,
                                            Tick total_duration) {
  bool is_exposition_end = (section_type == SectionType::Exposition);
  bool is_episode_end = (section_type == SectionType::Episode);
  bool is_section_final = (num_episodes == total_episodes && is_episode_end) ||
                          (section_type == SectionType::Coda);
  bool is_near_stretto = false;
  // Check if next section is Stretto.
  if (section_idx + 1 < structure.sections.size()) {
    is_near_stretto = (structure.sections[section_idx + 1].type == SectionType::Stretto);
  }

  Tick current_tick = structure.sections[section_idx].end_tick;
  float phase_pos = total_duration > 0
                        ? static_cast<float>(current_tick) / static_cast<float>(total_duration)
                        : 0.0f;
  int bars_since_last_pac =
      (last_pac_tick > 0)
          ? static_cast<int>((current_tick - last_pac_tick) / kTicksPerBar)
          : 999;

  for (const auto& rule : kCadenceContextRules) {
    // Check structural conditions.
    if (rule.requires_exposition_end && !is_exposition_end) continue;
    if (rule.requires_episode_end && !is_episode_end) continue;
    if (rule.requires_section_final && !is_section_final) continue;
    if (rule.avoid_near_stretto && is_near_stretto) continue;

    // Check phase position.
    if (phase_pos < rule.min_phase_pos) continue;

    // Check PAC spacing.
    if (rule.type == CadenceType::Perfect &&
        bars_since_last_pac < rule.min_bars_since_last_pac) {
      continue;
    }

    // Harmonic conditions (requires_dominant_preparation, requires_tonic_stability)
    // are deferred to Phase D when HarmonicTimeline is available.
    // For now, skip these checks (always pass).

    return rule.type;
  }

  return std::nullopt;
}

}  // namespace

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

  Tick last_pac_tick = 0;
  Tick total_duration = sections.back().end_tick;

  for (size_t idx = 0; idx < sections.size(); ++idx) {
    const auto& section = sections[idx];
    KeySignature section_key;
    section_key.tonic = section.key;
    section_key.is_minor = is_minor;

    // Track episode count before rule matching (needed for is_section_final check).
    if (section.type == SectionType::Episode) {
      ++num_episodes;
    }

    // Stretto and Coda have special handling that the context rules don't fully
    // capture (deceptive pre-stretto placement, Picardy third). MiddleEntry
    // never gets a cadence. These bypass the rule table entirely.
    if (section.type == SectionType::Stretto ||
        section.type == SectionType::Coda ||
        section.type == SectionType::MiddleEntry) {
      switch (section.type) {
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
          CadencePoint coda_cp;
          coda_cp.tick = section.start_tick;
          coda_cp.type = CadenceType::Perfect;
          coda_cp.key = home_key;
          plan.points.push_back(coda_cp);
          last_pac_tick = coda_cp.tick;

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

        default:
          break;
      }
      continue;
    }

    // For Exposition and Episode sections, try the context rule table first.
    auto matched = matchContextRule(section.type, structure, idx, num_episodes,
                                    total_episodes, last_pac_tick, total_duration);
    if (matched) {
      CadencePoint matched_cp;
      matched_cp.tick = section.end_tick - kTicksPerBar;
      matched_cp.type = *matched;
      matched_cp.key = section_key;
      plan.points.push_back(matched_cp);
      if (*matched == CadenceType::Perfect) {
        last_pac_tick = matched_cp.tick;
      }
    } else {
      // Fallback: use existing logic for this section type.
      switch (section.type) {
        case SectionType::Exposition: {
          // Exposition end: Half cadence to drive into development.
          CadencePoint fallback_cp;
          fallback_cp.tick = section.end_tick - kTicksPerBar;
          fallback_cp.type = CadenceType::Half;
          fallback_cp.key = section_key;
          plan.points.push_back(fallback_cp);
          break;
        }

        case SectionType::Episode: {
          CadencePoint fallback_cp;
          fallback_cp.tick = section.end_tick - kTicksPerBar;
          fallback_cp.key = section_key;

          if (num_episodes == total_episodes) {
            // Final episode (return to home key): Perfect cadence.
            fallback_cp.type = CadenceType::Perfect;
            fallback_cp.key = home_key;
            last_pac_tick = fallback_cp.tick;
          } else {
            // Interior episodes: Half cadence to prepare next key.
            fallback_cp.type = CadenceType::Half;
          }
          plan.points.push_back(fallback_cp);
          break;
        }

        default:
          break;
      }
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
