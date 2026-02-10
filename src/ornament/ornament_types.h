// Ornament type definitions for Bach ornamentation system.

#ifndef BACH_ORNAMENT_ORNAMENT_TYPES_H
#define BACH_ORNAMENT_ORNAMENT_TYPES_H

#include <cstdint>

namespace bach {

/// Types of ornaments available in the Bach ornamentation system.
enum class OrnamentType : uint8_t {
  Trill,                    // Main note + upper neighbor alternation
  Mordent,                  // Main -> lower -> main (3 notes)
  Pralltriller,             // Upper -> main (short trill)
  Turn,                     // Upper -> main -> lower -> main (4 notes)
  Appoggiatura,             // Grace note on beat
  Schleifer,                // Ascending grace note group
  Vorschlag,                // Grace note before main (pre-stroke)
  Nachschlag,               // Ornamental ending (after-stroke)
  CompoundTrillNachschlag,  // Trill + resolution ending
  CompoundTurnTrill         // Turn followed by trill
};

/// @brief Convert OrnamentType to human-readable string.
/// @param type The ornament type enum value.
/// @return Null-terminated string representation.
const char* ornamentTypeToString(OrnamentType type);

/// Configuration for the ornament engine.
struct OrnamentConfig {
  bool enable_trill = true;
  bool enable_mordent = true;
  bool enable_turn = true;
  bool enable_appoggiatura = true;
  bool enable_pralltriller = true;
  bool enable_vorschlag = true;
  bool enable_nachschlag = true;
  bool enable_compound = true;
  float ornament_density = 0.15f;  // Probability of adding ornament to eligible note
};

}  // namespace bach

#endif  // BACH_ORNAMENT_ORNAMENT_TYPES_H
