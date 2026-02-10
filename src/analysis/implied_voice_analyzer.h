// Implied voice analysis for monophonic lines (solo string).

#ifndef BACH_ANALYSIS_IMPLIED_VOICE_ANALYZER_H
#define BACH_ANALYSIS_IMPLIED_VOICE_ANALYZER_H

#include <cstdint>
#include <vector>

#include "core/basic_types.h"

namespace bach {

/// @brief Result of implied voice analysis for a single-line melody.
struct ImpliedVoiceAnalysisResult {
  float implied_voice_count = 0.0f;       ///< Estimated number of implied voices.
  float register_consistency = 0.0f;      ///< Register coherence within each voice [0,1].
  uint32_t implied_parallel_count = 0;    ///< Parallel 5ths/8ths between implied voices.
  bool passes_quality_gate = false;       ///< True if voice count in [2.3, 2.8] range.
};

/// @brief Analyzer for implied polyphony in monophonic lines.
///
/// Solo string works by Bach (BWV 1001-1006, BWV 1004 Chaconne) use
/// register separation to imply multiple independent voices within a
/// single melodic line. This analyzer reconstructs the implied voices
/// and checks for counterpoint violations between them.
class ImpliedVoiceAnalyzer {
 public:
  /// @brief Analyze implied polyphony in a melody.
  ///
  /// Separates the melody into implied voices using register-based
  /// splitting, then analyzes the resulting voice pairs for parallel
  /// perfect intervals and register consistency.
  ///
  /// @param melody The monophonic note sequence to analyze.
  /// @param register_split_pitch Pitch threshold for voice separation.
  ///        Notes at or above this pitch go to the upper voice;
  ///        notes below go to the lower voice.
  /// @return Analysis result with voice count, consistency, and violations.
  static ImpliedVoiceAnalysisResult analyze(
      const std::vector<NoteEvent>& melody,
      uint8_t register_split_pitch);

  /// @brief Estimate the optimal split pitch from the melody's register range.
  ///
  /// Computes the median pitch of the melody, which typically serves
  /// as a natural voice separation point.
  ///
  /// @param melody The note sequence to analyze.
  /// @return The estimated split pitch (median of the melody).
  static uint8_t estimateSplitPitch(const std::vector<NoteEvent>& melody);
};

}  // namespace bach

#endif  // BACH_ANALYSIS_IMPLIED_VOICE_ANALYZER_H
