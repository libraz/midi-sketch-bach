// Implementation of natural harmonics detection and marking.

#include "instrument/bowed/harmonics.h"

#include <algorithm>

#include "instrument/bowed/bow_direction.h"

namespace bach {

std::vector<uint8_t> getNaturalHarmonicPitches(InstrumentType instrument) {
  auto open_strings = getOpenStrings(instrument);
  std::vector<uint8_t> harmonics;

  for (uint8_t open_pitch : open_strings) {
    // Octave harmonic (1/2 string length): open + 12 semitones.
    uint8_t octave_harmonic = open_pitch + 12;
    if (octave_harmonic <= 127) {
      harmonics.push_back(octave_harmonic);
    }

    // Fifth harmonic (1/3 string length): open + 19 semitones (octave + fifth).
    uint8_t fifth_harmonic = open_pitch + 19;
    if (fifth_harmonic <= 127) {
      harmonics.push_back(fifth_harmonic);
    }
  }

  std::sort(harmonics.begin(), harmonics.end());
  // Remove duplicates (e.g., different strings may produce the same harmonic pitch).
  harmonics.erase(std::unique(harmonics.begin(), harmonics.end()), harmonics.end());
  return harmonics;
}

bool isNaturalHarmonic(uint8_t pitch, InstrumentType instrument) {
  auto harmonics = getNaturalHarmonicPitches(instrument);
  return std::binary_search(harmonics.begin(), harmonics.end(), pitch);
}

void markHarmonics(std::vector<NoteEvent>& notes, InstrumentType instrument,
                   ArcPhase arc_phase) {
  // Harmonics are only used at climactic moments.
  if (arc_phase != ArcPhase::Peak) return;

  auto harmonics = getNaturalHarmonicPitches(instrument);

  for (auto& note : notes) {
    // Minimum duration: note must be sustained long enough for the harmonic to ring.
    if (note.duration < kTicksPerBeat) continue;

    // Check if the pitch is a natural harmonic.
    if (std::binary_search(harmonics.begin(), harmonics.end(), note.pitch)) {
      note.is_harmonic = 1;
    }
  }
}

}  // namespace bach
