/// @file
/// @brief Implementation of MotionAnalyzer - voice-pair motion classification and statistics.

#include "counterpoint/motion_analyzer.h"

#include <algorithm>
#include <set>

#include "counterpoint/counterpoint_state.h"

namespace bach {

// ---------------------------------------------------------------------------
// MotionStats
// ---------------------------------------------------------------------------

float MotionAnalyzer::MotionStats::contraryRatio() const {
  int total_count = total();
  if (total_count == 0) {
    return 0.0f;
  }
  return static_cast<float>(contrary) / static_cast<float>(total_count);
}

// ---------------------------------------------------------------------------
// MotionAnalyzer
// ---------------------------------------------------------------------------

MotionAnalyzer::MotionAnalyzer(const IRuleEvaluator& rules) : rules_(rules) {}

MotionType MotionAnalyzer::classifyMotion(uint8_t prev1, uint8_t curr1,
                                          uint8_t prev2,
                                          uint8_t curr2) const {
  return rules_.classifyMotion(prev1, curr1, prev2, curr2);
}

MotionAnalyzer::MotionStats MotionAnalyzer::analyzeVoicePair(
    const CounterpointState& state, VoiceId voice1, VoiceId voice2) const {
  MotionStats stats;

  const auto& notes1 = state.getVoiceNotes(voice1);
  const auto& notes2 = state.getVoiceNotes(voice2);

  if (notes1.size() < 2 || notes2.size() < 2) {
    return stats;  // Need at least two notes in each voice.
  }

  // Collect all unique start_tick values where both voices have notes,
  // then iterate through consecutive pairs.
  std::set<Tick> ticks_set;
  for (const auto& note : notes1) {
    ticks_set.insert(note.start_tick);
  }
  for (const auto& note : notes2) {
    ticks_set.insert(note.start_tick);
  }

  std::vector<Tick> ticks(ticks_set.begin(), ticks_set.end());

  // For each consecutive tick pair, look up the note sounding in each
  // voice at both the previous and current tick.
  for (size_t idx = 1; idx < ticks.size(); ++idx) {
    Tick prev_tick = ticks[idx - 1];
    Tick curr_tick = ticks[idx];

    const NoteEvent* prev_note1 = state.getNoteAt(voice1, prev_tick);
    const NoteEvent* curr_note1 = state.getNoteAt(voice1, curr_tick);
    const NoteEvent* prev_note2 = state.getNoteAt(voice2, prev_tick);
    const NoteEvent* curr_note2 = state.getNoteAt(voice2, curr_tick);

    // Skip if any voice is silent at either tick.
    if (!prev_note1 || !curr_note1 || !prev_note2 || !curr_note2) {
      continue;
    }

    // Only classify when at least one voice has actually changed pitch.
    if (prev_note1->pitch == curr_note1->pitch &&
        prev_note2->pitch == curr_note2->pitch) {
      continue;
    }

    MotionType motion = rules_.classifyMotion(
        prev_note1->pitch, curr_note1->pitch,
        prev_note2->pitch, curr_note2->pitch);

    switch (motion) {
      case MotionType::Parallel: ++stats.parallel; break;
      case MotionType::Similar:  ++stats.similar;  break;
      case MotionType::Contrary: ++stats.contrary; break;
      case MotionType::Oblique:  ++stats.oblique;  break;
    }
  }

  return stats;
}

}  // namespace bach
