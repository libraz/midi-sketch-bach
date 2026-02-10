/// @file
/// @brief Organ manual assignment -- routes voices to manuals based on form and voice count.

#include "organ/manual.h"

namespace bach {

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

namespace {

/// @brief Determine VoiceRole for a voice in fugue-style forms.
/// @param voice_index Zero-based voice index.
/// @return Appropriate VoiceRole for the given position.
VoiceRole fugueRoleForVoice(uint8_t voice_index) {
  switch (voice_index) {
    case 0: return VoiceRole::Assert;
    case 1: return VoiceRole::Respond;
    case 2: return VoiceRole::Propel;
    default: return VoiceRole::Ground;
  }
}

/// @brief Assign manuals for fugue-style forms (Fugue, PreludeAndFugue, etc.).
/// @param num_voices Number of voices (2-5).
/// @return Vector of ManualAssignment, empty if num_voices is out of range.
std::vector<ManualAssignment> assignFugueManuals(uint8_t num_voices) {
  std::vector<ManualAssignment> result;

  if (num_voices < 2 || num_voices > 5) {
    return result;
  }

  result.reserve(num_voices);

  switch (num_voices) {
    case 2:
      // 2 voices: Great + Swell
      result.push_back({0, OrganManual::Great, fugueRoleForVoice(0)});
      result.push_back({1, OrganManual::Swell, fugueRoleForVoice(1)});
      break;

    case 3:
      // 3 voices: Great + Swell + Positiv (no pedal)
      result.push_back({0, OrganManual::Great, fugueRoleForVoice(0)});
      result.push_back({1, OrganManual::Swell, fugueRoleForVoice(1)});
      result.push_back({2, OrganManual::Positiv, fugueRoleForVoice(2)});
      break;

    case 4:
      // 4 voices: Great + Swell + Positiv + Pedal
      result.push_back({0, OrganManual::Great, fugueRoleForVoice(0)});
      result.push_back({1, OrganManual::Swell, fugueRoleForVoice(1)});
      result.push_back({2, OrganManual::Positiv, fugueRoleForVoice(2)});
      result.push_back({3, OrganManual::Pedal, fugueRoleForVoice(3)});
      break;

    case 5:
      // 5 voices: 2 on Great + Swell + Positiv + Pedal
      result.push_back({0, OrganManual::Great, fugueRoleForVoice(0)});
      result.push_back({1, OrganManual::Great, fugueRoleForVoice(1)});
      result.push_back({2, OrganManual::Swell, fugueRoleForVoice(2)});
      result.push_back({3, OrganManual::Positiv, fugueRoleForVoice(3)});
      result.push_back({4, OrganManual::Pedal, fugueRoleForVoice(4)});
      break;
  }

  return result;
}

/// @brief Assign manuals for trio sonata form (BWV 525-530).
///
/// Trio sonatas have 3 equal independent voices:
///   Voice 0 -> Great (right hand)
///   Voice 1 -> Swell (left hand)
///   Voice 2 -> Pedal
/// All voices receive VoiceRole::Assert since they are equal.
///
/// @return Vector of 3 ManualAssignment entries.
std::vector<ManualAssignment> assignTrioSonataManuals() {
  return {
      {0, OrganManual::Great, VoiceRole::Assert},
      {1, OrganManual::Swell, VoiceRole::Assert},
      {2, OrganManual::Pedal, VoiceRole::Assert},
  };
}

/// @brief Check if a form type uses fugue-style manual assignment.
/// @param form The form type to check.
/// @return True if the form follows fugue assignment rules.
bool isFugueStyleForm(FormType form) {
  switch (form) {
    case FormType::Fugue:
    case FormType::PreludeAndFugue:
    case FormType::ToccataAndFugue:
    case FormType::FantasiaAndFugue:
    case FormType::Passacaglia:
    case FormType::ChoralePrelude:
      return true;
    default:
      return false;
  }
}

}  // namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

std::vector<ManualAssignment> assignManuals(uint8_t num_voices, FormType form) {
  if (num_voices < 2 || num_voices > 5) {
    return {};
  }

  if (form == FormType::TrioSonata) {
    // Trio sonata is always 3 voices regardless of num_voices parameter
    return assignTrioSonataManuals();
  }

  if (isFugueStyleForm(form)) {
    return assignFugueManuals(num_voices);
  }

  // Unsupported form types (solo string forms should not use organ manual assignment)
  return {};
}

uint8_t channelForAssignment(const ManualAssignment& assignment) {
  return OrganModel::channelForManual(assignment.manual);
}

uint8_t programForAssignment(const ManualAssignment& assignment) {
  return OrganModel::programForManual(assignment.manual);
}

bool isPitchPlayableOnManual(uint8_t pitch, OrganManual manual, const OrganModel& model) {
  return model.isInManualRange(pitch, manual);
}

}  // namespace bach
