// Harmonic tension computation -- shared utility for chord-based tension levels.

#include "harmony/harmonic_tension.h"

#include "harmony/harmonic_event.h"

namespace bach {

float computeHarmonicTension(ChordDegree degree, ChordQuality quality, int inversion) {
  switch (degree) {
    case ChordDegree::I:
      return 0.0f;
    case ChordDegree::vi:
      return 0.0f;
    case ChordDegree::iii:
      return 0.2f;
    case ChordDegree::IV:
      return 0.3f;
    case ChordDegree::ii:
      return 0.3f;
    case ChordDegree::V:
      if (quality == ChordQuality::Dominant7) {
        // Inversion-based tension differentiation for V7.
        // 0=root (0.8), 1=3rd in bass (0.7), 3=7th in bass (0.9), other=0.8.
        if (inversion == 1) return 0.7f;
        if (inversion == 3) return 0.9f;
        return 0.8f;
      }
      return 0.6f;
    case ChordDegree::V_of_V:
    case ChordDegree::V_of_vi:
    case ChordDegree::V_of_IV:
    case ChordDegree::V_of_ii:
    case ChordDegree::V_of_iii:
      return 0.7f;
    case ChordDegree::viiDim:
      if (quality == ChordQuality::Diminished7) return 1.0f;
      return 0.9f;
    case ChordDegree::bII:
      return 0.6f;
    case ChordDegree::bVI:
    case ChordDegree::bVII:
    case ChordDegree::bIII:
      return 0.4f;
    default:
      return 0.5f;
  }
}

float computeHarmonicTension(const HarmonicEvent& harm) {
  return computeHarmonicTension(harm.chord.degree, harm.chord.quality, harm.chord.inversion);
}

}  // namespace bach
