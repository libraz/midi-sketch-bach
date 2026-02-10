#ifndef BACH_COUNTERPOINT_SUSPENSION_CHAIN_H
#define BACH_COUNTERPOINT_SUSPENSION_CHAIN_H

#include <vector>

#include "core/basic_types.h"

namespace bach {

/// @brief Type of suspension by interval.
enum class SuspensionType : uint8_t {
  Sus4_3,  ///< 4th resolving to 3rd (most common in Bach)
  Sus7_6,  ///< 7th resolving to 6th
  Sus9_8,  ///< 9th resolving to octave
  Sus2_3   ///< 2nd resolving to 3rd (bass suspension, ascending)
};

/// @brief A single suspension event in a chain.
struct SuspensionEvent {
  SuspensionType type;
  Tick preparation_tick = 0;  ///< Consonant preparation beat.
  Tick dissonance_tick = 0;   ///< Held note becomes dissonant (strong beat).
  Tick resolution_tick = 0;   ///< Stepwise resolution.
  uint8_t suspended_pitch = 0;    ///< The held pitch.
  uint8_t resolution_pitch = 0;   ///< The resolution target pitch.
  VoiceId voice = 0;
};

/// @brief A chain of connected suspensions.
struct SuspensionChain {
  std::vector<SuspensionEvent> events;
  Tick start_tick = 0;
  Tick end_tick = 0;
};

/// @brief Generate a suspension chain for a voice pair.
///
/// Creates a sequence of connected suspensions where the resolution of one
/// becomes the preparation for the next. The chain types are fixed design
/// values: 4-3, 7-6, 9-8, 2-3 (Principle 4: Trust Design Values).
///
/// @param start_tick Starting tick position.
/// @param num_suspensions Number of suspensions in the chain (1-4).
/// @param base_pitch Starting pitch for the suspended voice.
/// @param voice Voice ID for the suspended notes.
/// @param type Primary suspension type for the chain.
/// @return Generated suspension chain with preparation-dissonance-resolution events.
SuspensionChain generateSuspensionChain(Tick start_tick, uint8_t num_suspensions,
                                        uint8_t base_pitch, VoiceId voice,
                                        SuspensionType type = SuspensionType::Sus4_3);

/// @brief Convert SuspensionType to human-readable string.
const char* suspensionTypeToString(SuspensionType type);

/// @brief Get the resolution interval in semitones for a suspension type.
/// @return Negative for downward resolution, positive for upward (Sus2_3).
int resolutionInterval(SuspensionType type);

}  // namespace bach

#endif  // BACH_COUNTERPOINT_SUSPENSION_CHAIN_H
