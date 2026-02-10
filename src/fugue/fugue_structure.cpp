// Implementation of fugue structure: section management and validation.

#include "fugue/fugue_structure.h"

#include "core/json_helpers.h"

namespace bach {

const char* sectionTypeToString(SectionType type) {
  switch (type) {
    case SectionType::Exposition:   return "Exposition";
    case SectionType::Episode:      return "Episode";
    case SectionType::MiddleEntry:  return "MiddleEntry";
    case SectionType::Stretto:      return "Stretto";
    case SectionType::Coda:         return "Coda";
  }
  return "Unknown";
}

bool FugueStructure::addSection(SectionType type, FuguePhase phase,
                                Tick start_tick, Tick end_tick, Key key) {
  // Validate phase ordering: new phase must be >= last section's phase.
  if (!sections.empty()) {
    auto last_phase = sections.back().phase;
    if (static_cast<uint8_t>(phase) < static_cast<uint8_t>(last_phase)) {
      return false;
    }
  }

  FugueSection section;
  section.type = type;
  section.phase = phase;
  section.start_tick = start_tick;
  section.end_tick = end_tick;
  section.key = key;
  sections.push_back(section);
  return true;
}

Tick FugueStructure::totalDurationTicks() const {
  if (sections.empty()) {
    return 0;
  }
  return sections.back().end_tick;
}

std::vector<FugueSection> FugueStructure::getSectionsByPhase(FuguePhase phase) const {
  std::vector<FugueSection> result;
  for (const auto& section : sections) {
    if (section.phase == phase) {
      result.push_back(section);
    }
  }
  return result;
}

std::vector<FugueSection> FugueStructure::getSectionsByType(SectionType type) const {
  std::vector<FugueSection> result;
  for (const auto& section : sections) {
    if (section.type == type) {
      result.push_back(section);
    }
  }
  return result;
}

std::vector<std::string> FugueStructure::validate() const {
  std::vector<std::string> violations;

  // Check 1: At least one section.
  if (sections.empty()) {
    violations.emplace_back("Structure is empty: no sections defined");
    return violations;
  }

  // Check 2: First section must be Exposition in Establish phase.
  if (sections.front().type != SectionType::Exposition) {
    violations.emplace_back("First section must be Exposition, got " +
                            std::string(sectionTypeToString(sections.front().type)));
  }
  if (sections.front().phase != FuguePhase::Establish) {
    violations.emplace_back("First section must be in Establish phase, got " +
                            std::string(fuguePhaseToString(sections.front().phase)));
  }

  // Check 3: Phases are monotonically non-decreasing.
  for (size_t idx = 1; idx < sections.size(); ++idx) {
    auto prev_phase = static_cast<uint8_t>(sections[idx - 1].phase);
    auto curr_phase = static_cast<uint8_t>(sections[idx].phase);
    if (curr_phase < prev_phase) {
      violations.emplace_back(
          "Phase regression at section " + std::to_string(idx) + ": " +
          fuguePhaseToString(sections[idx].phase) + " after " +
          fuguePhaseToString(sections[idx - 1].phase));
    }
  }

  // Check 4: Tick consistency -- no negative duration, sections do not go backward.
  for (size_t idx = 0; idx < sections.size(); ++idx) {
    if (sections[idx].end_tick < sections[idx].start_tick) {
      violations.emplace_back("Section " + std::to_string(idx) +
                              " has negative duration (start=" +
                              std::to_string(sections[idx].start_tick) + ", end=" +
                              std::to_string(sections[idx].end_tick) + ")");
    }
    if (idx > 0 && sections[idx].start_tick < sections[idx - 1].end_tick) {
      violations.emplace_back("Section " + std::to_string(idx) +
                              " starts before previous section ends (start=" +
                              std::to_string(sections[idx].start_tick) + ", prev_end=" +
                              std::to_string(sections[idx - 1].end_tick) + ")");
    }
  }

  return violations;
}

std::string FugueStructure::toJson() const {
  JsonWriter writer;
  writer.beginObject();

  writer.key("section_count");
  writer.value(static_cast<uint32_t>(sections.size()));

  writer.key("total_duration_ticks");
  writer.value(totalDurationTicks());

  writer.key("sections");
  writer.beginArray();
  for (const auto& section : sections) {
    writer.beginObject();
    writer.key("type");
    writer.value(std::string_view(sectionTypeToString(section.type)));
    writer.key("phase");
    writer.value(std::string_view(fuguePhaseToString(section.phase)));
    writer.key("start_tick");
    writer.value(section.start_tick);
    writer.key("end_tick");
    writer.value(section.end_tick);
    writer.key("duration_ticks");
    writer.value(section.durationTicks());
    writer.key("key");
    writer.value(std::string_view(keyToString(section.key)));
    writer.endObject();
  }
  writer.endArray();

  writer.endObject();
  return writer.toString();
}

}  // namespace bach
