/// @file
/// @brief Implementation of BachRuleEvaluator - Bach-style counterpoint rules
/// with context-aware relaxations for P4 consonance, hidden perfects,
/// temporary voice crossings, and weak-beat dissonance tolerance.

#include "counterpoint/bach_rule_evaluator.h"

#include <cmath>

#include "core/basic_types.h"
#include "core/interval.h"
#include "core/pitch_utils.h"
#include "counterpoint/counterpoint_state.h"

namespace bach {

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

BachRuleEvaluator::BachRuleEvaluator(uint8_t num_voices)
    : num_voices_(num_voices) {}

void BachRuleEvaluator::setFreeCounterpoint(bool enabled) {
  free_counterpoint_ = enabled;
}

bool BachRuleEvaluator::isFreeCounterpoint() const {
  return free_counterpoint_;
}

uint8_t BachRuleEvaluator::numVoices() const {
  return num_voices_;
}

// ---------------------------------------------------------------------------
// Interval classification
// ---------------------------------------------------------------------------

/// @brief Check if a reduced interval (0-11) is consonant regardless of beat position.
///
/// Used for weak-beat dissonance gating in free counterpoint mode: consonances
/// pass immediately, while dissonances are rejected so that the collision
/// resolver's NHT check (passing tone / neighbor tone) can evaluate with
/// next_pitch context.
///
/// @param reduced Interval modulo 12, in range [0, 11].
/// @return True if the interval is a consonance (P1/P5/m3/M3/m6/M6).




bool BachRuleEvaluator::isIntervalConsonant(int semitones,
                                            bool is_strong_beat) const {
  // Free counterpoint mode: consonances always OK on weak beats.
  // Dissonances on weak beats are rejected so that the collision resolver's
  // NHT check (passing tone / neighbor tone) can evaluate with next_pitch context.
  if (free_counterpoint_ && !is_strong_beat) {
    int reduced = interval_util::compoundToSimple(semitones);
    return interval_util::isConsonance(reduced);
  }

  // Normalize to single-octave interval.
  int reduced = interval_util::compoundToSimple(semitones);

  switch (reduced) {
    // Perfect consonances.
    case interval::kUnison:      // P1 (and P8 via mod 12)
    case interval::kPerfect5th:  // P5
      return true;

    // Imperfect consonances.
    case interval::kMinor3rd:
    case interval::kMajor3rd:
    case interval::kMinor6th:
    case interval::kMajor6th:
      return true;

    // P4 -- consonant in Bach style when 3+ voices are present.
    // In a 2-voice context, P4 remains dissonant (same as Fux).
    case interval::kPerfect4th:
      return num_voices_ >= 3;

    // All other intervals are dissonant.
    default:
      return false;
  }
}

// ---------------------------------------------------------------------------
// Motion classification
// ---------------------------------------------------------------------------

MotionType BachRuleEvaluator::classifyMotion(uint8_t prev1, uint8_t curr1,
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

const NoteEvent* BachRuleEvaluator::getPreviousNote(
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

bool BachRuleEvaluator::hasParallelPerfect(const CounterpointState& state,
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
// Hidden perfect detection -- more lenient than Fux
// ---------------------------------------------------------------------------

bool BachRuleEvaluator::hasHiddenPerfect(const CounterpointState& state,
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

  // Bach relaxation: hidden fifths/octaves are allowed when EITHER voice
  // approaches by step (<=2 semitones).  Fux only allows the upper voice.
  uint8_t upper_prev = std::max(prev1->pitch, prev2->pitch);
  uint8_t upper_curr = std::max(curr1->pitch, curr2->pitch);
  int upper_motion = std::abs(static_cast<int>(upper_curr) -
                              static_cast<int>(upper_prev));

  uint8_t lower_prev = std::min(prev1->pitch, prev2->pitch);
  uint8_t lower_curr = std::min(curr1->pitch, curr2->pitch);
  int lower_motion = std::abs(static_cast<int>(lower_curr) -
                              static_cast<int>(lower_prev));

  if (upper_motion <= 2 || lower_motion <= 2) {
    return false;  // Either voice approaches by step -- allowed in Bach style.
  }

  return true;
}

// ---------------------------------------------------------------------------
// Voice crossing detection -- allows temporary crossings
// ---------------------------------------------------------------------------

bool BachRuleEvaluator::isCrossingTemporary(const CounterpointState& state,
                                            VoiceId voice1, VoiceId voice2,
                                            Tick tick) const {
  // Check if voices return to proper order at the next beat.
  Tick next_beat = tick + kTicksPerBeat;

  const NoteEvent* next1 = state.getNoteAt(voice1, next_beat);
  const NoteEvent* next2 = state.getNoteAt(voice2, next_beat);

  if (!next1 || !next2) {
    // No notes at next beat -- cannot confirm resolution; treat as persistent.
    return false;
  }

  // Convention: voice1 < voice2 means voice1 is the upper voice.
  // Check that proper ordering is restored at the next beat.
  if (voice1 < voice2) {
    return next1->pitch >= next2->pitch;  // Upper voice back above.
  }
  return next1->pitch <= next2->pitch;  // Lower voice back below.
}

bool BachRuleEvaluator::hasVoiceCrossing(const CounterpointState& state,
                                         VoiceId voice1, VoiceId voice2,
                                         Tick tick) const {
  const NoteEvent* note1 = state.getNoteAt(voice1, tick);
  const NoteEvent* note2 = state.getNoteAt(voice2, tick);
  if (!note1 || !note2) return false;

  // Check if voices are in the wrong order right now.
  bool crossed = false;
  if (voice1 < voice2) {
    crossed = note1->pitch < note2->pitch;
  } else {
    crossed = note1->pitch > note2->pitch;
  }

  if (!crossed) return false;

  // Bach relaxation: temporary crossings (resolving within 1 beat) are allowed.
  if (isCrossingTemporary(state, voice1, voice2, tick)) {
    return false;  // Temporary crossing -- not a violation.
  }

  return true;  // Persistent crossing -- violation.
}

// ---------------------------------------------------------------------------
// Full validation
// ---------------------------------------------------------------------------

std::vector<RuleViolation> BachRuleEvaluator::validate(
    const CounterpointState& state, Tick from_tick, Tick to_tick) const {
  std::vector<RuleViolation> violations;

  const auto& voices = state.getActiveVoices();
  if (voices.size() < 2) return violations;

  // Check every beat position in the range.
  for (Tick tick = from_tick; tick < to_tick; tick += kTicksPerBeat) {
    bool is_strong_beat = (tick % kTicksPerBeat == 0);

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
