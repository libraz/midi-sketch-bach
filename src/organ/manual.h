#ifndef BACH_ORGAN_MANUAL_H
#define BACH_ORGAN_MANUAL_H

#include <vector>

#include "core/basic_types.h"
#include "instrument/keyboard/organ_model.h"

namespace bach {

/// @brief Assignment of a voice to an organ manual with its structural role.
struct ManualAssignment {
  VoiceId voice;
  OrganManual manual;
  VoiceRole role;
};

/// @brief Assign voices to organ manuals based on form and voice count.
///
/// Assignment rules by form:
///   - Fugue/PreludeAndFugue/ToccataAndFugue/FantasiaAndFugue/Passacaglia:
///     - 2 voices: Great + Swell
///     - 3 voices: Great + Swell + Positiv
///     - 4 voices: Great + Swell + Positiv + Pedal
///     - 5 voices: Great (x2) + Swell + Positiv + Pedal
///   - TrioSonata:
///     - 3 voices: Great (RH) + Swell (LH) + Pedal
///   - ChoralePrelude:
///     - 4 voices: Great + Swell + Positiv + Pedal (same as fugue)
///
/// VoiceRole assignment:
///   - Fugue-style: Voice 0=Assert, 1=Respond, 2=Propel, 3+=Ground
///   - TrioSonata: All voices get Assert (equal independent voices)
///
/// @param num_voices Number of voices (2-5).
/// @param form The musical form being generated.
/// @return Vector of manual assignments, one per voice. Empty on invalid input.
std::vector<ManualAssignment> assignManuals(uint8_t num_voices, FormType form);

/// @brief Get the MIDI channel for a voice given its manual assignment.
///
/// Delegates to OrganModel::channelForManual. Channel mapping:
///   Great=0, Swell=1, Positiv=2, Pedal=3.
///
/// @param assignment The manual assignment to query.
/// @return MIDI channel number (0-3).
uint8_t channelForAssignment(const ManualAssignment& assignment);

/// @brief Get the GM program number for a voice given its manual assignment.
///
/// Delegates to OrganModel::programForManual. Program mapping:
///   Church Organ (19) for Great/Positiv/Pedal, Reed Organ (20) for Swell.
///
/// @param assignment The manual assignment to query.
/// @return GM program number.
uint8_t programForAssignment(const ManualAssignment& assignment);

/// @brief Check if a pitch is within the playable range for the assigned manual.
///
/// Delegates to OrganModel::isInManualRange using the provided model's
/// configured ranges.
///
/// @param pitch MIDI pitch number (0-127).
/// @param manual Target organ manual.
/// @param model Organ model with range constraints.
/// @return True if the pitch is playable on the specified manual.
bool isPitchPlayableOnManual(uint8_t pitch, OrganManual manual, const OrganModel& model);

}  // namespace bach

#endif  // BACH_ORGAN_MANUAL_H
