#ifndef BACH_COMPOSER_CANDIDATE_SEARCH_H
#define BACH_COMPOSER_CANDIDATE_SEARCH_H

#include <vector>

#include "composer/candidate.h"
#include "composer/harmonic_plan.h"
#include "composer/material.h"
#include "composer/span.h"
#include "core/basic_types.h"

namespace bach::composer {

// Context handed to candidate enumeration. Only the values the search
// rules need to score a single span are exposed; nothing legacy.
struct CandidateContext {
  // Pitch and tick of the most recently placed note in this voice. Zero
  // pitch means "voice has not started yet" (use the voice center as
  // the implicit anchor).
  std::uint8_t prev_pitch = 0;
  Tick prev_end_tick = 0;

  // Pitch of the note two notes back in this voice. Used together with
  // prev_pitch to enforce leap-resolution across span boundaries.
  // Zero means unavailable.
  std::uint8_t pre_prev_pitch = 0;

  // Per-voice nominal center. Phase 2 uses fixed centers; later phases
  // derive from VoicePlan.
  std::uint8_t voice_center = 60;  // middle C

  // Notes already committed in other voices, used for vertical checks
  // (parallel-perfect avoidance). Null or empty for the first voice;
  // subsequent voices receive the prefix-accumulated note list.
  const std::vector<NoteEvent>* placed_notes = nullptr;

  // True iff `prev_pitch` is a non-chord-tone of the chord active at
  // its onset (i.e. a passing tone). When true, the next pitch in this
  // voice must be within ±2 semis of `prev_pitch` so the Validator's
  // `unprepared_dissonance` rule is satisfied. Carried across span
  // boundaries by the Composer driver so passing-tone resolution
  // continues into the next span.
  bool prev_was_passing_tone = false;
};

// Strong-beat consonance + parallel-perfect avoidance.
//
// Phase 2 scope. No legacy collision_resolver coupling. Search rules:
//   1. SubjectCarrier spans replay Material verbatim (one Candidate
//      per MaterialNote, source = Material, score = 1.0).
//   2. Compose spans enumerate diatonic pitches inside +-octave of the
//      voice center, score them with chord-tone preference, and reject
//      candidates whose only score would come from non-chord-tone usage
//      on a strong beat.
class CandidateSearch {
 public:
  // Returns the candidates for the given span, ordered by score
  // descending. Empty result means the search exhausted with no
  // admissible candidate; caller must trigger span re-generation or
  // back-jump.
  std::vector<Candidate> enumerate(const Span& span, const HarmonicPlan& harmonic_plan,
                                   const Material& material, const CandidateContext& context) const;
};

}  // namespace bach::composer

#endif  // BACH_COMPOSER_CANDIDATE_SEARCH_H
