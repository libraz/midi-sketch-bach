// Voice registration, naming, and pitch-range penalty calculation.

#ifndef BACH_COUNTERPOINT_VOICE_MANAGER_H
#define BACH_COUNTERPOINT_VOICE_MANAGER_H

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "core/basic_types.h"

namespace bach {

/// @brief Manages voice metadata: name, range, and pitch penalty.
///
/// Every voice that participates in counterpoint must be registered here
/// before generation begins.  The pitchPenalty() function follows the same
/// soft-penalty pattern as pedal_constraints.h -- 0.0 inside the ideal
/// range, linearly increasing per semitone outside.
class VoiceManager {
 public:
  /// @brief Register a voice with a human-readable name and pitch range.
  /// @param voice_id Unique voice identifier.
  /// @param name Display name (e.g. "Soprano", "Pedal").
  /// @param low_pitch Lowest playable MIDI pitch (inclusive).
  /// @param high_pitch Highest playable MIDI pitch (inclusive).
  void registerVoice(VoiceId voice_id, const std::string& name,
                     uint8_t low_pitch, uint8_t high_pitch);

  /// @brief Check whether a voice has been registered.
  bool isRegistered(VoiceId voice_id) const;

  /// @brief Get the display name of a registered voice.
  /// @return Name string.  Empty string if the voice is not registered.
  const std::string& getVoiceName(VoiceId voice_id) const;

  /// @brief Calculate soft pitch-range penalty.
  ///
  /// Returns 0.0 if the pitch is within [low, high].  For pitches
  /// outside, the penalty increases linearly at kPenaltyPerSemitone
  /// per semitone of distance from the nearest boundary.
  ///
  /// @param voice_id Voice to evaluate.
  /// @param pitch MIDI note number.
  /// @return Penalty value (0.0 = in range, >0 = outside).
  float pitchPenalty(VoiceId voice_id, uint8_t pitch) const;

  /// @brief Number of registered voices.
  size_t voiceCount() const;

  /// @brief Get all registered voice IDs (in registration order).
  std::vector<VoiceId> getVoiceIds() const;

  /// Penalty cost per semitone outside the ideal range.
  static constexpr float kPenaltyPerSemitone = 5.0f;

 private:
  struct VoiceInfo {
    std::string name;
    uint8_t low = 0;
    uint8_t high = 127;
  };

  std::map<VoiceId, VoiceInfo> voices_;
  std::vector<VoiceId> voice_order_;  ///< Maintains registration order.

  static const std::string kEmptyName;
};

}  // namespace bach

#endif  // BACH_COUNTERPOINT_VOICE_MANAGER_H
