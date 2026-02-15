/// @file
/// @brief Implementation of FuxRuleEvaluator - Fux strict counterpoint rule checking.

#include "counterpoint/fux_rule_evaluator.h"

#include <cmath>

#include "core/basic_types.h"
#include "core/interval.h"
#include "core/pitch_utils.h"
#include "counterpoint/counterpoint_state.h"

namespace bach {

// ---------------------------------------------------------------------------
// Interval classification
// ---------------------------------------------------------------------------



bool FuxRuleEvaluator::isIntervalConsonant(int semitones,
                                           bool is_strong_beat) const {
  // In Fux strict counterpoint the consonance classification does not
  // change between strong and weak beats -- dissonance handling is the
  // caller's responsibility (via SpeciesRules).  We accept the param to
  // satisfy the interface and allow future Bach-style extensions.
  (void)is_strong_beat;

  // Normalize to single-octave interval.
  int reduced = interval_util::compoundToSimple(semitones);

  switch (reduced) {
    // Perfect consonances.
    case interval::kUnison:     // P1 (and P8 via mod 12)
    case interval::kPerfect5th: // P5
      return true;

    // Imperfect consonances.
    case interval::kMinor3rd:
    case interval::kMajor3rd:
    case interval::kMinor6th:
    case interval::kMajor6th:
      return true;

    // P4 -- dissonant in two-voice counterpoint (Fux).
    case interval::kPerfect4th:
      return false;

    // All other intervals are dissonant.
    default:
      return false;
  }
}

// ---------------------------------------------------------------------------
// Motion classification
// ---------------------------------------------------------------------------

MotionType FuxRuleEvaluator::classifyMotion(uint8_t prev1, uint8_t curr1,
                                            uint8_t prev2,
                                            uint8_t curr2) const {
  int dir1 = static_cast<int>(curr1) - static_cast<int>(prev1);
  int dir2 = static_cast<int>(curr2) - static_cast<int>(prev2);

  // Oblique: one voice is stationary.
  if (dir1 == 0 || dir2 == 0) {
    return MotionType::Oblique;
  }

  // Contrary: voices move in opposite directions.
  if ((dir1 > 0 && dir2 < 0) || (dir1 < 0 && dir2 > 0)) {
    return MotionType::Contrary;
  }

  // Both voices move in the same direction.
  int interval_prev = std::abs(static_cast<int>(prev1) -
                               static_cast<int>(prev2));
  int interval_curr = std::abs(static_cast<int>(curr1) -
                               static_cast<int>(curr2));

  // Parallel: same direction AND same interval size.
  if (interval_prev == interval_curr) {
    return MotionType::Parallel;
  }

  // Similar: same direction but different interval size.
  return MotionType::Similar;
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

const NoteEvent* FuxRuleEvaluator::getPreviousNote(
    const CounterpointState& state, VoiceId voice_id, Tick tick) {
  const auto& notes = state.getVoiceNotes(voice_id);

  // Find the last note that starts strictly before `tick`.
  const NoteEvent* prev = nullptr;
  for (const auto& note : notes) {
    if (note.start_tick >= tick) {
      break;
    }
    prev = &note;
  }
  return prev;
}

// ---------------------------------------------------------------------------
// Parallel perfect detection
// ---------------------------------------------------------------------------

bool FuxRuleEvaluator::hasParallelPerfect(const CounterpointState& state,
                                          VoiceId voice1, VoiceId voice2,
                                          Tick tick) const {
  const NoteEvent* curr1 = state.getNoteAt(voice1, tick);
  const NoteEvent* curr2 = state.getNoteAt(voice2, tick);
  if (!curr1 || !curr2) return false;

  const NoteEvent* prev1 = getPreviousNote(state, voice1, tick);
  const NoteEvent* prev2 = getPreviousNote(state, voice2, tick);
  if (!prev1 || !prev2) return false;

  int prev_interval = std::abs(static_cast<int>(prev1->pitch) -
                               static_cast<int>(prev2->pitch));
  int curr_interval = std::abs(static_cast<int>(curr1->pitch) -
                               static_cast<int>(curr2->pitch));

  // Both intervals must be perfect consonances (P1/P5/P8).
  if (!interval_util::isPerfectConsonance(prev_interval) ||
      !interval_util::isPerfectConsonance(curr_interval)) {
    return false;
  }

  // Both intervals must be the same type (both P5, or both P1/P8).
  int prev_reduced = interval_util::compoundToSimple(prev_interval);
  int curr_reduced = interval_util::compoundToSimple(curr_interval);
  if (prev_reduced != curr_reduced) return false;

  // Motion must be in the same direction (parallel, not contrary).
  MotionType motion = classifyMotion(prev1->pitch, curr1->pitch,
                                     prev2->pitch, curr2->pitch);
  return motion == MotionType::Parallel || motion == MotionType::Similar;
}

// ---------------------------------------------------------------------------
// Hidden perfect detection
// ---------------------------------------------------------------------------

bool FuxRuleEvaluator::hasHiddenPerfect(const CounterpointState& state,
                                        VoiceId voice1, VoiceId voice2,
                                        Tick tick) const {
  const NoteEvent* curr1 = state.getNoteAt(voice1, tick);
  const NoteEvent* curr2 = state.getNoteAt(voice2, tick);
  if (!curr1 || !curr2) return false;

  const NoteEvent* prev1 = getPreviousNote(state, voice1, tick);
  const NoteEvent* prev2 = getPreviousNote(state, voice2, tick);
  if (!prev1 || !prev2) return false;

  int curr_interval = std::abs(static_cast<int>(curr1->pitch) -
                               static_cast<int>(curr2->pitch));

  // The arriving interval must be a perfect consonance.
  if (!interval_util::isPerfectConsonance(curr_interval)) return false;

  // The previous interval must NOT be the same perfect consonance.
  int prev_interval = std::abs(static_cast<int>(prev1->pitch) -
                               static_cast<int>(prev2->pitch));
  int prev_reduced = interval_util::compoundToSimple(prev_interval);
  int curr_reduced = interval_util::compoundToSimple(curr_interval);
  if (prev_reduced == curr_reduced) {
    return false;  // That would be parallel, not hidden.
  }

  // Voices must move in the same direction (similar motion).
  MotionType motion = classifyMotion(prev1->pitch, curr1->pitch,
                                     prev2->pitch, curr2->pitch);
  if (motion != MotionType::Similar && motion != MotionType::Parallel) {
    return false;
  }

  // Hidden fifths/octaves are only a violation when the upper voice
  // does NOT approach by step.  Check if the upper voice moved by
  // step (1-2 semitones).
  uint8_t upper_prev = std::max(prev1->pitch, prev2->pitch);
  uint8_t upper_curr = std::max(curr1->pitch, curr2->pitch);
  int upper_motion = std::abs(static_cast<int>(upper_curr) -
                              static_cast<int>(upper_prev));
  if (upper_motion <= 2) {
    return false;  // Upper voice approaches by step -- allowed.
  }

  return true;
}

// ---------------------------------------------------------------------------
// Voice crossing detection
// ---------------------------------------------------------------------------

bool FuxRuleEvaluator::hasVoiceCrossing(const CounterpointState& state,
                                        VoiceId voice1, VoiceId voice2,
                                        Tick tick) const {
  const NoteEvent* note1 = state.getNoteAt(voice1, tick);
  const NoteEvent* note2 = state.getNoteAt(voice2, tick);
  if (!note1 || !note2) return false;

  // Convention: voice1 should be the higher voice (lower VoiceId =
  // higher pitch in standard SATB ordering).  If voice1 < voice2,
  // then voice1 is "upper" and should have pitch >= voice2's pitch.
  if (voice1 < voice2) {
    return note1->pitch < note2->pitch;
  }
  return note1->pitch > note2->pitch;
}

// ---------------------------------------------------------------------------
// Full validation
// ---------------------------------------------------------------------------

std::vector<RuleViolation> FuxRuleEvaluator::validate(
    const CounterpointState& state, Tick from_tick, Tick to_tick) const {
  std::vector<RuleViolation> violations;

  const auto& voices = state.getActiveVoices();
  if (voices.size() < 2) return violations;

  // Check every beat position in the range.
  for (Tick tick = from_tick; tick < to_tick; tick += kTicksPerBeat) {
    bool is_strong_beat = (beatInBar(tick) == 0 || beatInBar(tick) == 2);

    // Check all voice pairs.
    for (size_t idx_a = 0; idx_a < voices.size(); ++idx_a) {
      for (size_t idx_b = idx_a + 1; idx_b < voices.size(); ++idx_b) {
        VoiceId va = voices[idx_a];
        VoiceId vb = voices[idx_b];

        // Check interval consonance.
        const NoteEvent* note_a = state.getNoteAt(va, tick);
        const NoteEvent* note_b = state.getNoteAt(vb, tick);
        if (note_a && note_b && is_strong_beat) {
          int ivl = std::abs(static_cast<int>(note_a->pitch) -
                             static_cast<int>(note_b->pitch));
          if (!isIntervalConsonant(ivl, is_strong_beat)) {
            RuleViolation viol;
            viol.voice1 = va;
            viol.voice2 = vb;
            viol.tick = tick;
            viol.rule = "dissonance_on_strong_beat";
            viol.severity = 1;
            violations.push_back(viol);
          }
        }

        // Parallel perfect consonances (separate fifths vs octaves).
        if (hasParallelPerfect(state, va, vb, tick)) {
          RuleViolation viol;
          viol.voice1 = va;
          viol.voice2 = vb;
          viol.tick = tick;
          // Determine if it's fifths or octaves from current interval.
          const NoteEvent* ca = state.getNoteAt(va, tick);
          const NoteEvent* cb = state.getNoteAt(vb, tick);
          int ivl_mod = (ca && cb)
              ? interval_util::compoundToSimple(absoluteInterval(ca->pitch, cb->pitch))
              : 0;
          viol.rule = (ivl_mod == 7) ? "parallel_fifths" : "parallel_octaves";
          viol.severity = 1;
          violations.push_back(viol);
        }

        // Hidden perfect consonances (separate fifths vs octaves).
        if (hasHiddenPerfect(state, va, vb, tick)) {
          RuleViolation viol;
          viol.voice1 = va;
          viol.voice2 = vb;
          viol.tick = tick;
          const NoteEvent* ca = state.getNoteAt(va, tick);
          const NoteEvent* cb = state.getNoteAt(vb, tick);
          int ivl_mod = (ca && cb)
              ? interval_util::compoundToSimple(absoluteInterval(ca->pitch, cb->pitch))
              : 0;
          viol.rule = (ivl_mod == 7) ? "hidden_fifths" : "hidden_octaves";
          viol.severity = 0;  // Warning, not error.
          violations.push_back(viol);
        }

        // Voice crossing.
        if (hasVoiceCrossing(state, va, vb, tick)) {
          RuleViolation viol;
          viol.voice1 = va;
          viol.voice2 = vb;
          viol.tick = tick;
          viol.rule = "voice_crossing";
          viol.severity = 1;
          violations.push_back(viol);
        }
      }
    }
  }

  return violations;
}

}  // namespace bach
