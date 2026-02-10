#ifndef BACH_ORGAN_REGISTRATION_H
#define BACH_ORGAN_REGISTRATION_H

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "core/basic_types.h"
#include "fugue/fugue_structure.h"

namespace bach {

/// @brief Registration setting for one structural point.
/// Specifies GM program number for each manual and a velocity hint.
struct Registration {
  uint8_t manual1_program = 19;  // Great: Church Organ
  uint8_t manual2_program = 20;  // Swell: Reed Organ
  uint8_t manual3_program = 19;  // Positiv: Church Organ
  uint8_t pedal_program = 19;    // Pedal: Church Organ
  uint8_t velocity_hint = 80;    // Volume level hint
};

/// @brief Registration plan with 3 structural trigger points.
/// Only changes at these points; no mid-phrase changes.
struct RegistrationPlan {
  Registration exposition;  // Start of exposition (FuguePhase::Establish)
  Registration stretto;     // Start of stretto section (FuguePhase::Resolve)
  Registration coda;        // Coda section
};

/// @brief Create a default registration plan.
///
/// Exposition: standard organ tone (prog 19/20/19/19, vel 75).
/// Stretto: fuller registration (prog 19/19/19/19, vel 90).
/// Coda: full registration with tutti (prog 19/19/19/19, vel 100).
///
/// @return A RegistrationPlan with default settings for each structural point.
RegistrationPlan createDefaultRegistrationPlan();

/// @brief Get the Registration for a given fugue phase and tick position.
/// @param plan The registration plan.
/// @param phase Current fugue phase.
/// @param is_coda True if we are in the coda section.
/// @return The appropriate Registration.
const Registration& getRegistrationForPhase(const RegistrationPlan& plan,
                                            FuguePhase phase,
                                            bool is_coda = false);

/// @brief Generate MIDI CC events for a registration change.
///
/// Creates CC#7 (Volume) and CC#11 (Expression) events for each organ channel
/// (channels 0-3). Returns 8 events total (4 channels x 2 CCs).
///
/// @param reg Registration to apply.
/// @param tick Tick position for the events.
/// @return Vector of MidiEvents (CC#7 + CC#11 for channels 0-3).
std::vector<MidiEvent> generateRegistrationEvents(const Registration& reg, Tick tick);

/// @brief Apply a registration plan to tracks by inserting CC events.
///
/// Inserts registration change events at the appropriate tick positions.
/// Events are always inserted at exposition_tick. Events at stretto_tick and
/// coda_tick are only inserted when their values are greater than zero.
///
/// @param tracks Tracks to modify (events vector will be appended to).
/// @param plan Registration plan.
/// @param exposition_tick Start tick of exposition.
/// @param stretto_tick Start tick of stretto (0 = no stretto).
/// @param coda_tick Start tick of coda (0 = no coda).
void applyRegistrationPlan(std::vector<Track>& tracks,
                           const RegistrationPlan& plan,
                           Tick exposition_tick,
                           Tick stretto_tick = 0,
                           Tick coda_tick = 0);

/// @brief Generate CC#7 (Volume) events from energy curve at section boundaries.
///
/// For each {tick, energy} pair, emits a CC#7 event on each organ channel.
/// This provides a finer-grained dynamic curve than the 3-point RegistrationPlan.
///
/// @param energy_levels Vector of {tick, energy} pairs at section boundaries.
/// @param num_channels Number of organ channels (typically 4).
/// @return Vector of MidiEvents (CC#7) for volume control.
std::vector<MidiEvent> generateEnergyRegistrationEvents(
    const std::vector<std::pair<Tick, float>>& energy_levels, uint8_t num_channels);

// ---------------------------------------------------------------------------
// Extended N-point registration plan
// ---------------------------------------------------------------------------

/// @brief A registration change point at a specific tick position.
struct RegistrationPoint {
  Tick tick = 0;                ///< Tick position for this registration change.
  Registration registration;    ///< The registration settings to apply.
  std::string label;            ///< Descriptive label (e.g., "exposition", "episode_1").
};

/// @brief Extended registration plan with N structural trigger points.
///
/// The N-point plan provides finer-grained registration changes at section
/// boundaries (episode starts, middle entries) in addition to the standard
/// 3-point plan (exposition, stretto, coda).
struct ExtendedRegistrationPlan {
  std::vector<RegistrationPoint> points;  ///< Registration points sorted by tick.

  /// @brief Add a registration point.
  /// @param tick Tick position.
  /// @param reg Registration settings.
  /// @param label Descriptive label.
  void addPoint(Tick tick, const Registration& reg, const std::string& label = "");

  /// @brief Get the number of registration points.
  /// @return Number of points in the plan.
  size_t size() const { return points.size(); }

  /// @brief Check if the plan is empty.
  /// @return True if no registration points exist.
  bool empty() const { return points.empty(); }
};

/// @brief Create an extended registration plan from a fugue structure.
///
/// Generates registration points at every section boundary using design value
/// tables (Principle 4: Trust Design Values). Velocity hints per section type:
///   - Exposition: 75 (standard organ tone)
///   - Episode: 70-90 (gradual curve following energy position)
///   - MiddleEntry: 80-95 (slight boost for subject prominence)
///   - Stretto: 95 (climactic section)
///   - Coda: 100 (full registration)
///
/// @param sections Fugue structure sections for timing and type information.
/// @param total_duration Total piece duration for energy curve computation.
/// @return ExtendedRegistrationPlan with N points (one per section).
ExtendedRegistrationPlan createExtendedRegistrationPlan(
    const std::vector<FugueSection>& sections, Tick total_duration);

/// @brief Apply an extended registration plan to tracks by inserting CC events.
///
/// For each RegistrationPoint, generates CC#7 (Volume) and CC#11 (Expression)
/// events and inserts them into matching tracks by channel.
///
/// @param tracks Tracks to modify (events appended to matching channels).
/// @param plan Extended registration plan.
void applyExtendedRegistrationPlan(std::vector<Track>& tracks,
                                    const ExtendedRegistrationPlan& plan);

}  // namespace bach

#endif  // BACH_ORGAN_REGISTRATION_H
