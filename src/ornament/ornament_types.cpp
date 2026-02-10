// Ornament type string conversion implementations.

#include "ornament/ornament_types.h"

namespace bach {

const char* ornamentTypeToString(OrnamentType type) {
  switch (type) {
    case OrnamentType::Trill:        return "trill";
    case OrnamentType::Mordent:      return "mordent";
    case OrnamentType::Pralltriller: return "pralltriller";
    case OrnamentType::Turn:         return "turn";
    case OrnamentType::Appoggiatura: return "appoggiatura";
    case OrnamentType::Schleifer:    return "schleifer";
  }
  return "unknown";
}

}  // namespace bach
