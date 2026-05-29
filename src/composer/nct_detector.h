#ifndef BACH_COMPOSER_NCT_DETECTOR_H
#define BACH_COMPOSER_NCT_DETECTOR_H

#include <cstddef>
#include <vector>

#include "composer/harmonic_plan.h"
#include "composer/material.h"

namespace bach::composer::nct_detector {

// Type of non-chord-tone figure detected. A figure is "the local melodic
// shape around a single non-chord-tone (NCT) note that explains why the
// NCT is admissible in strict-counterpoint terms." Each figure has a
// fixed neighbourhood width in melody-note positions:
//
//   * Cambiata          5 notes (chord, step-down NCT, leap-down,
//                                step-up, step-up chord-tone)
//   * Echappee          3 notes (chord, step-up NCT, leap-down chord-tone)
//   * Anticipation      2 notes (chord-tone, step-down NCT whose pitch
//                                equals the next strong-beat chord-tone)
//   * NotaCambiata      4 notes (chord, step-down NCT, leap-down,
//                                step-up chord-tone — Bach's compressed
//                                form)
enum class NctFigure : std::uint8_t {
  Cambiata = 0,
  Echappee = 1,
  Anticipation = 2,
  NotaCambiata = 3,
};

// Result of detecting one figure. `nct_index` is the position (inside the
// supplied note list) of the NCT itself — the note that does NOT belong
// to the chord at its onset. Callers can use this index to wire the
// corresponding RuleBit (CambiataDetected etc.) into provenance.
struct NctHit {
  NctFigure figure = NctFigure::Cambiata;
  std::size_t nct_index = 0;
  std::size_t window_start = 0;  // Inclusive start of the figure's window.
  std::size_t window_end = 0;    // Exclusive end of the figure's window.
};

// Detect all Cambiata figures in `notes`. `notes` must be a single voice
// in time order. `harmonic_plan` provides chord context for chord-tone
// classification (notes whose pitch class is NOT in the active triad
// count as candidate NCTs).
std::vector<NctHit> detectCambiata(const std::vector<MaterialNote>& notes,
                                   const HarmonicPlan& harmonic_plan);

// Echappee: chord-tone → step-up NCT → leap-down chord-tone. The NCT
// resolves by skip (>=3 semitones) downward to a chord-tone in the
// active chord at its onset.
std::vector<NctHit> detectEchappee(const std::vector<MaterialNote>& notes,
                                   const HarmonicPlan& harmonic_plan);

// Anticipation: chord-tone → step-down NCT whose pitch identically
// equals the next strong-beat chord-tone (the NCT "anticipates" the
// next chord's pitch). Detector looks one position ahead.
std::vector<NctHit> detectAnticipation(const std::vector<MaterialNote>& notes,
                                       const HarmonicPlan& harmonic_plan);

// Nota cambiata (4-note compressed form): chord-tone → step-down NCT →
// leap-down NCT → step-up chord-tone. The middle two notes are both
// outside the active triad.
std::vector<NctHit> detectNotaCambiata(const std::vector<MaterialNote>& notes,
                                       const HarmonicPlan& harmonic_plan);

}  // namespace bach::composer::nct_detector

#endif  // BACH_COMPOSER_NCT_DETECTOR_H
