#ifndef BACH_ANALYSIS_VOICE_INDEPENDENCE_H
#define BACH_ANALYSIS_VOICE_INDEPENDENCE_H

#include <cstdint>
#include <vector>

#include "core/basic_types.h"

namespace bach {

/// @brief Score breakdown for voice independence analysis.
///
/// Each sub-score measures a different aspect of how independently two
/// voices behave.  All values are in [0, 1] where 1 means fully independent.
struct VoiceIndependenceScore {
  float rhythm_independence = 0.0f;   ///< How different the rhythms are [0,1].
  float contour_independence = 0.0f;  ///< How different the melodic directions are [0,1].
  float register_separation = 0.0f;   ///< How well-separated the pitch ranges are [0,1].

  /// @brief Weighted composite score.
  /// @return Composite score in [0,1].
  ///         Weights: rhythm = 0.4, contour = 0.3, register = 0.3.
  float composite() const;

  /// @brief Check if independence meets the minimum threshold for trio sonata quality.
  /// @return True if composite() >= 0.6.
  bool meetsTrioStandard() const;
};

/// @brief Analyze voice independence between two voices.
///
/// Extracts notes belonging to each voice from the full note list,
/// then compares their rhythmic patterns, melodic contours, and pitch ranges.
///
/// @param notes All notes in the piece (any order).
/// @param voice_a First voice ID.
/// @param voice_b Second voice ID.
/// @return Independence score between the two voices.
///         Returns all-zero score if either voice has no notes.
VoiceIndependenceScore analyzeVoicePair(const std::vector<NoteEvent>& notes,
                                        VoiceId voice_a, VoiceId voice_b);

/// @brief Analyze overall voice independence across all voice pairs.
///
/// Computes pairwise independence for every pair (i, j) where i < j
/// and returns the minimum (worst-case) score.
///
/// @param notes All notes in the piece.
/// @param num_voices Total number of voices (voice IDs 0 .. num_voices-1).
/// @return Minimum pairwise independence score.
///         Returns all-zero score if fewer than 2 voices are present.
VoiceIndependenceScore analyzeOverall(const std::vector<NoteEvent>& notes,
                                      uint8_t num_voices);

/// @brief Calculate rhythm independence between two note sequences.
///
/// Measures how often the two voices have different onset patterns.
/// Simultaneous attacks (within a 16th-note tolerance of 120 ticks)
/// reduce independence.
///
/// @param voice_a_notes Notes for first voice.
/// @param voice_b_notes Notes for second voice.
/// @return Rhythm independence score [0,1].  1.0 = no simultaneous attacks.
///         Returns 0.0 if both voices are empty.
float calculateRhythmIndependence(const std::vector<NoteEvent>& voice_a_notes,
                                  const std::vector<NoteEvent>& voice_b_notes);

/// @brief Calculate contour independence between two note sequences.
///
/// At each beat boundary the melodic direction (up / down / same) is
/// determined for each voice.  Voices moving in opposite directions score
/// higher.
///
/// @param voice_a_notes Notes for first voice (sorted by start_tick).
/// @param voice_b_notes Notes for second voice (sorted by start_tick).
/// @return Contour independence score [0,1].  1.0 = always opposite directions.
///         Returns 0.0 if there are no comparison points.
float calculateContourIndependence(const std::vector<NoteEvent>& voice_a_notes,
                                   const std::vector<NoteEvent>& voice_b_notes);

/// @brief Calculate register separation between two note sequences.
///
/// Measures how well the pitch ranges of the two voices are separated.
/// Overlapping ranges reduce the score.
///
/// @param voice_a_notes Notes for first voice.
/// @param voice_b_notes Notes for second voice.
/// @return Register separation score [0,1].  1.0 = no pitch overlap.
///         Returns 0.0 if either voice is empty.
float calculateRegisterSeparation(const std::vector<NoteEvent>& voice_a_notes,
                                  const std::vector<NoteEvent>& voice_b_notes);

}  // namespace bach

#endif  // BACH_ANALYSIS_VOICE_INDEPENDENCE_H
