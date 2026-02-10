// Fugue structure: section definitions and overall fugue layout.
//
// A fugue is divided into sections (Exposition, Episode, MiddleEntry,
// Stretto, Coda), each assigned to a FuguePhase (Establish, Develop,
// Resolve). The phase order is strictly monotonic and never reversed.

#ifndef BACH_FUGUE_FUGUE_STRUCTURE_H
#define BACH_FUGUE_FUGUE_STRUCTURE_H

#include <cstdint>
#include <string>
#include <vector>

#include "core/basic_types.h"

namespace bach {

/// Section types that make up a fugue's formal structure.
enum class SectionType : uint8_t {
  Exposition,    // Initial subject/answer entries
  Episode,       // Modulatory, sequential material
  MiddleEntry,   // Subject re-entry in related key
  Stretto,       // Overlapping subject entries
  Coda           // Final closing section
};

/// @brief Convert SectionType to human-readable string.
/// @param type The section type enum value.
/// @return Null-terminated string representation.
const char* sectionTypeToString(SectionType type);

/// A single section within the fugue structure.
struct FugueSection {
  SectionType type = SectionType::Exposition;
  FuguePhase phase = FuguePhase::Establish;
  Tick start_tick = 0;
  Tick end_tick = 0;
  Key key = Key::C;

  /// @brief Get the duration of this section in ticks.
  /// @return end_tick - start_tick.
  Tick durationTicks() const { return end_tick - start_tick; }
};

/// The complete formal structure of a fugue, holding all sections in order.
///
/// Sections are added sequentially and must respect the FuguePhase ordering
/// constraint: Establish <= Develop <= Resolve (never backward).
struct FugueStructure {
  std::vector<FugueSection> sections;

  /// @brief Add a section to the fugue structure.
  ///
  /// Validates that the FuguePhase does not regress (phase must be >= the
  /// phase of the last added section).
  ///
  /// @param type Section type (Exposition, Episode, etc.).
  /// @param phase Fugue phase (Establish, Develop, Resolve).
  /// @param start_tick Start position in ticks.
  /// @param end_tick End position in ticks.
  /// @param key Musical key for this section.
  /// @return True if the section was added, false if phase ordering violated.
  bool addSection(SectionType type, FuguePhase phase,
                  Tick start_tick, Tick end_tick, Key key = Key::C);

  /// @brief Get total duration in ticks (end of last section).
  /// @return End tick of the final section, or 0 if empty.
  Tick totalDurationTicks() const;

  /// @brief Get the number of sections.
  /// @return Section count.
  size_t sectionCount() const { return sections.size(); }

  /// @brief Get all sections belonging to a specific phase.
  /// @param phase The fugue phase to filter by.
  /// @return Vector of matching sections.
  std::vector<FugueSection> getSectionsByPhase(FuguePhase phase) const;

  /// @brief Get all sections of a specific type.
  /// @param type The section type to filter by.
  /// @return Vector of matching sections.
  std::vector<FugueSection> getSectionsByType(SectionType type) const;

  /// @brief Validate the structural integrity of the fugue.
  ///
  /// Checks:
  ///   1. At least one section exists.
  ///   2. Phases are monotonically non-decreasing.
  ///   3. First section is Exposition with FuguePhase::Establish.
  ///   4. Start/end ticks are consistent (no overlap, no negative duration).
  ///
  /// @return Vector of violation descriptions (empty if valid).
  std::vector<std::string> validate() const;

  /// @brief Serialize the structure to a JSON string.
  /// @return JSON representation of the fugue structure.
  std::string toJson() const;
};

}  // namespace bach

#endif  // BACH_FUGUE_FUGUE_STRUCTURE_H
