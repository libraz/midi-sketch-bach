// Implementation of chaconne variation type utilities.

#include "solo_string/arch/variation_types.h"

#include <algorithm>

namespace bach {

const char* textureTypeToString(TextureType type) {
  switch (type) {
    case TextureType::SingleLine:       return "SingleLine";
    case TextureType::ImpliedPolyphony: return "ImpliedPolyphony";
    case TextureType::FullChords:       return "FullChords";
    case TextureType::Arpeggiated:      return "Arpeggiated";
    case TextureType::ScalePassage:     return "ScalePassage";
    case TextureType::Bariolage:        return "Bariolage";
  }
  return "Unknown";  // NOLINT(clang-diagnostic-covered-switch-default): future-proof
}

const char* variationTypeToString(VariationType type) {
  switch (type) {
    case VariationType::Theme:     return "Theme";
    case VariationType::Lyrical:   return "Lyrical";
    case VariationType::Rhythmic:  return "Rhythmic";
    case VariationType::Virtuosic: return "Virtuosic";
    case VariationType::Chordal:   return "Chordal";
  }
  return "Unknown";  // NOLINT(clang-diagnostic-covered-switch-default): future-proof
}

std::vector<VariationType> getAllowedTypes(VariationRole role) {
  switch (role) {
    case VariationRole::Establish:
      return {VariationType::Theme, VariationType::Lyrical};
    case VariationRole::Develop:
      return {VariationType::Rhythmic, VariationType::Lyrical};
    case VariationRole::Destabilize:
      return {VariationType::Virtuosic, VariationType::Rhythmic};
    case VariationRole::Illuminate:
      return {VariationType::Lyrical, VariationType::Chordal};
    case VariationRole::Accumulate:
      return {VariationType::Virtuosic, VariationType::Chordal};
    case VariationRole::Resolve:
      return {VariationType::Theme};
  }
  return {};  // NOLINT(clang-diagnostic-covered-switch-default): future-proof
}

bool isTypeAllowedForRole(VariationType type, VariationRole role) {
  auto allowed = getAllowedTypes(role);
  return std::find(allowed.begin(), allowed.end(), type) != allowed.end();
}

bool isRoleOrderValid(const std::vector<VariationRole>& roles) {
  if (roles.empty()) {
    return false;
  }

  // Count specific roles.
  int accumulate_count = 0;
  int resolve_count = 0;

  for (const auto& role : roles) {
    if (role == VariationRole::Accumulate) {
      ++accumulate_count;
    }
    if (role == VariationRole::Resolve) {
      ++resolve_count;
    }
  }

  // Accumulate must appear exactly 3 times.
  if (accumulate_count != 3) {
    return false;
  }

  // Resolve must appear exactly once and be the final role.
  if (resolve_count != 1 || roles.back() != VariationRole::Resolve) {
    return false;
  }

  // Check monotonic non-decreasing order with Destabilize exception.
  // Destabilize can appear both before Illuminate (pre-major) and after
  // Illuminate (post-major, leading into Accumulate).
  //
  // The valid pattern allows transitions where the role value stays the same
  // or increases, except that Destabilize (2) may follow Illuminate (3).
  // We track whether we have passed the Illuminate section to allow
  // the second Destabilize block.
  bool passed_illuminate = false;

  for (size_t idx = 1; idx < roles.size(); ++idx) {
    auto prev = static_cast<uint8_t>(roles[idx - 1]);
    auto curr = static_cast<uint8_t>(roles[idx]);

    if (roles[idx - 1] == VariationRole::Illuminate) {
      passed_illuminate = true;
    }

    // Allow Destabilize after Illuminate (the second Destabilize block).
    if (passed_illuminate && roles[idx] == VariationRole::Destabilize &&
        roles[idx - 1] == VariationRole::Illuminate) {
      continue;
    }

    // Otherwise, require monotonically non-decreasing order.
    if (curr < prev) {
      return false;
    }
  }

  return true;
}

}  // namespace bach
