/// @file
/// @brief Implementation of VoiceManager - voice registration, naming, and range penalties.

#include "counterpoint/voice_manager.h"

#include <algorithm>

namespace bach {

const std::string VoiceManager::kEmptyName;

void VoiceManager::registerVoice(VoiceId voice_id, const std::string& name,
                                 uint8_t low_pitch, uint8_t high_pitch) {
  // Avoid duplicate insertion into the ordered list.
  if (voices_.find(voice_id) == voices_.end()) {
    voice_order_.push_back(voice_id);
  }
  VoiceInfo& info = voices_[voice_id];
  info.name = name;
  info.low = low_pitch;
  info.high = high_pitch;
}

bool VoiceManager::isRegistered(VoiceId voice_id) const {
  return voices_.find(voice_id) != voices_.end();
}

const std::string& VoiceManager::getVoiceName(VoiceId voice_id) const {
  auto iter = voices_.find(voice_id);
  if (iter == voices_.end()) {
    return kEmptyName;
  }
  return iter->second.name;
}

float VoiceManager::pitchPenalty(VoiceId voice_id, uint8_t pitch) const {
  auto iter = voices_.find(voice_id);
  if (iter == voices_.end()) {
    // Unregistered voice -- treat everything as out of range.
    return kPenaltyPerSemitone;
  }

  const VoiceInfo& info = iter->second;

  if (pitch >= info.low && pitch <= info.high) {
    return 0.0f;
  }
  if (pitch < info.low) {
    return static_cast<float>(info.low - pitch) * kPenaltyPerSemitone;
  }
  return static_cast<float>(pitch - info.high) * kPenaltyPerSemitone;
}

size_t VoiceManager::voiceCount() const { return voices_.size(); }

std::vector<VoiceId> VoiceManager::getVoiceIds() const {
  return voice_order_;
}

}  // namespace bach
