#include "composer/candidate_search.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <vector>

namespace bach::composer {

namespace {

constexpr Tick kQuarter = kTicksPerBeat;

// Returns the chord triad pitch classes (0..11) for a ChordEvent.
std::array<std::uint8_t, 3> triadPitchClasses(const ChordEvent& chord) {
  std::uint8_t third_offset = 4;  // Major
  std::uint8_t fifth_offset = 7;
  switch (chord.quality) {
    case ChordQuality::Major:
      third_offset = 4;
      fifth_offset = 7;
      break;
    case ChordQuality::Minor:
      third_offset = 3;
      fifth_offset = 7;
      break;
    case ChordQuality::Diminished:
      third_offset = 3;
      fifth_offset = 6;
      break;
  }
  return {
      static_cast<std::uint8_t>(chord.root_pc % 12),
      static_cast<std::uint8_t>((chord.root_pc + third_offset) % 12),
      static_cast<std::uint8_t>((chord.root_pc + fifth_offset) % 12),
  };
}

const ChordEvent& activeChord(const HarmonicPlan& plan, Tick at) {
  // chords assumed non-empty and sorted by start_tick.
  const ChordEvent* current = &plan.chords.front();
  for (const auto& chord : plan.chords) {
    if (chord.start_tick <= at) {
      current = &chord;
    } else {
      break;
    }
  }
  return *current;
}

bool isStrongBeat(Tick tick) {
  return (tick % kTicksPerBar) == 0;
}

bool isPerfectInterval(int semitones) {
  int abs_semi = std::abs(semitones) % 12;
  return abs_semi == 0 || abs_semi == 7;
}

// Returns the pitch sounding in `voice` at `tick`, or 0 if none.
// Mirrors the validator's voicePitchAt so that search-time rejection
// agrees with post-hoc validation. `notes` is the composer's incremental
// commit log: spans for one voice are contiguous, but voices are not
// interleaved by start_tick, so we cannot break early on a higher
// start_tick — a later entry from a different voice may still match.
std::uint8_t voicePitchAt(const std::vector<NoteEvent>& notes, VoiceId voice, Tick tick) {
  std::uint8_t pitch = 0;
  for (const auto& note : notes) {
    if (note.voice != voice)
      continue;
    if (note.start_tick > tick)
      continue;
    if (note.start_tick <= tick && tick < note.start_tick + note.duration) {
      pitch = note.pitch;
    }
  }
  return pitch;
}

// Returns true iff `semis` is a consonant interval. Consonant set
// (mod 12): unison/octave (0), m3 (3), M3 (4), P4 (5), P5 (7),
// m6 (8), M6 (9). All others are dissonant. Matches the validator's
// vertical_dissonance rule so search-time rejection agrees with
// post-hoc validation.
bool isConsonantInterval(int semis) {
  const int pc = std::abs(semis) % 12;
  return pc == 0 || pc == 3 || pc == 4 || pc == 5 || pc == 7 || pc == 8 || pc == 9;
}

// Returns the pitch of a note that starts at exactly `tick` in `voice`,
// or 0 if no such note exists. Used by the cross-span leave-side
// lookahead: when a Carrier (SubjectCarrier or AnswerCarrier) span has
// already been placed at the next tick, the current weak non-chord-tone
// pick must remain within ±2 semitones of that fixed pitch or the
// Validator's `unprepared_dissonance` rule will fire on the last weak
// position of the preceding Compose span.
std::uint8_t sameVoiceStartingAt(const std::vector<NoteEvent>& placed, VoiceId voice, Tick tick) {
  for (const auto& note : placed) {
    if (note.voice != voice)
      continue;
    if (note.start_tick == tick)
      return note.pitch;
  }
  return 0;
}

// Returns true iff committing `candidate_pitch` in `candidate_voice` at
// the strong beat `cur_tick` forms a dissonant vertical with any
// already-placed voice that is sounding at that tick.
bool createsVerticalDissonance(const std::vector<NoteEvent>& placed, VoiceId candidate_voice,
                               std::uint8_t candidate_pitch, Tick cur_tick) {
  // `placed` is voice-grouped (one voice's spans are appended together),
  // not globally sorted by start_tick — see voicePitchAt() for the same
  // pattern. Skip rather than break so later voices are not missed.
  for (const auto& note : placed) {
    if (note.voice == candidate_voice)
      continue;
    if (note.start_tick > cur_tick)
      continue;
    const bool sounding = note.start_tick <= cur_tick && cur_tick < note.start_tick + note.duration;
    if (!sounding)
      continue;
    const int interval = static_cast<int>(candidate_pitch) - static_cast<int>(note.pitch);
    if (!isConsonantInterval(interval)) {
      return true;
    }
  }
  return false;
}

// Returns true iff committing `candidate_pitch` in `candidate_voice` at
// `cur_tick` would cross any already-placed voice. Convention: lower
// voice index = higher pitch (voice 0 = soprano). A candidate in voice
// V must be >= every placed voice with index > V and <= every placed
// voice with index < V at the same tick.
bool createsVoiceCrossing(const std::vector<NoteEvent>& placed, VoiceId candidate_voice,
                          std::uint8_t candidate_pitch, Tick cur_tick) {
  // See voicePitchAt() for why this loop cannot break on a higher
  // start_tick: `placed` is voice-grouped, not globally sorted.
  for (const auto& note : placed) {
    if (note.voice == candidate_voice)
      continue;
    if (note.start_tick > cur_tick)
      continue;
    const bool sounding = note.start_tick <= cur_tick && cur_tick < note.start_tick + note.duration;
    if (!sounding)
      continue;
    if (candidate_voice < note.voice && candidate_pitch < note.pitch) {
      return true;  // candidate should be above note.voice, but isn't.
    }
    if (candidate_voice > note.voice && candidate_pitch > note.pitch) {
      return true;  // candidate should be below note.voice, but isn't.
    }
  }
  return false;
}

// Returns true iff committing `candidate_pitch` in `candidate_voice` at
// `cur_tick`, given that the same voice held `prev_pitch` at `prev_tick`,
// would form parallel perfect motion against any already-placed voice.
//
// Convention follows Validator: interval = lower_voice_index_pitch -
// higher_voice_index_pitch. Both voices must move and the perfect
// interval must remain identical and non-zero.
bool createsParallelPerfect(const std::vector<NoteEvent>& placed, VoiceId candidate_voice,
                            std::uint8_t candidate_pitch, Tick cur_tick, std::uint8_t prev_pitch,
                            Tick prev_tick) {
  if (prev_pitch == 0)
    return false;  // no prev anchor in this voice yet

  std::vector<VoiceId> other_voices;
  for (const auto& n : placed) {
    if (n.voice == candidate_voice)
      continue;
    if (std::find(other_voices.begin(), other_voices.end(), n.voice) == other_voices.end()) {
      other_voices.push_back(n.voice);
    }
  }

  for (VoiceId ov : other_voices) {
    const std::uint8_t op_now = voicePitchAt(placed, ov, cur_tick);
    const std::uint8_t op_prev = voicePitchAt(placed, ov, prev_tick);
    if (op_now == 0 || op_prev == 0)
      continue;
    const bool both_moved = (candidate_pitch != prev_pitch) && (op_now != op_prev);
    if (!both_moved)
      continue;
    int interval_now;
    int interval_prev;
    if (candidate_voice < ov) {
      interval_now = static_cast<int>(candidate_pitch) - static_cast<int>(op_now);
      interval_prev = static_cast<int>(prev_pitch) - static_cast<int>(op_prev);
    } else {
      interval_now = static_cast<int>(op_now) - static_cast<int>(candidate_pitch);
      interval_prev = static_cast<int>(op_prev) - static_cast<int>(prev_pitch);
    }
    if (isPerfectInterval(interval_now) && isPerfectInterval(interval_prev) &&
        interval_now == interval_prev && interval_now != 0) {
      return true;
    }
  }
  return false;
}

}  // namespace

std::vector<Candidate> CandidateSearch::enumerate(const Span& span,
                                                  const HarmonicPlan& harmonic_plan,
                                                  const Material& material,
                                                  const CandidateContext& context) const {
  std::vector<Candidate> out;

  // Diatonic pitch-class mask for the key declared by the HarmonicPlan.
  // Used as a tiny chromatic penalty at eighth-note weak positions so
  // diatonic step approaches outrank chromatic alternatives of the same
  // step distance. The penalty is small (0.05) so genuinely required
  // chromatic motion is still reachable when no diatonic alternative
  // survives the rule cascade.
  std::array<bool, 12> diatonic_pc = {};
  {
    static constexpr int kMajorOffsets[7] = {0, 2, 4, 5, 7, 9, 11};
    static constexpr int kMinorOffsets[7] = {0, 2, 3, 5, 7, 8, 10};
    const auto* offsets = harmonic_plan.is_minor ? kMinorOffsets : kMajorOffsets;
    for (int i = 0; i < 7; ++i) {
      diatonic_pc[(harmonic_plan.tonic_pc + offsets[i]) % 12] = true;
    }
  }

  if (span.intent == VoiceIntent::SubjectCarrier || span.intent == VoiceIntent::AnswerCarrier) {
    // Replay material verbatim. One Candidate per MaterialNote that falls
    // inside the span window. Score = 1.0. Sets bit 0 of satisfied_rules
    // (ChordTone) when the material pitch matches the triad of the chord
    // active at its onset, so the Composer's leave-side passing-tone
    // flag stays accurate across a Carrier→Compose span boundary.
    //
    // Source list is the subject fragment for SubjectCarrier and the
    // answer fragment for AnswerCarrier; both carry the same verbatim
    // semantics (no candidate search, score = 1.0).
    const auto& source =
        (span.intent == VoiceIntent::SubjectCarrier) ? material.subject : material.answer;
    for (const auto& mnote : source) {
      if (mnote.start_tick < span.start_tick)
        continue;
      if (mnote.start_tick >= span.end_tick)
        break;
      Candidate c;
      c.start_tick = mnote.start_tick;
      c.duration = mnote.duration;
      c.pitch = mnote.pitch;
      c.score = 1.0f;
      c.satisfied_rules = 0;
      const ChordEvent& chord_here = activeChord(harmonic_plan, mnote.start_tick);
      const auto triad_here = triadPitchClasses(chord_here);
      const std::uint8_t pc_m = static_cast<std::uint8_t>(mnote.pitch % 12);
      if (pc_m == triad_here[0] || pc_m == triad_here[1] || pc_m == triad_here[2]) {
        c.satisfied_rules |= 1ull << 0;
      }
      out.push_back(c);
    }
    return out;
  }

  // Compose spans: lay down one note per beat aligned to span.start_tick.
  // Pitch picked from current chord tones near voice_center. Spans with
  // Subdivision::Eighth produce two notes per beat instead — the rest
  // of the rule cascade (vertical, leap, passing-tone) operates per
  // note position and is stride-agnostic.
  const Tick stride = (span.subdivision == Subdivision::Eighth) ? kQuarter / 2 : kQuarter;

  // Local cursor used for vertical (other-voice) parallel checks.
  // Holds the candidate this enumerate() call just committed, or the
  // caller-supplied anchor for the first iteration. The "previous tick"
  // is the actual start_tick of that committed candidate (not its end),
  // so the validator-style lookup at prev_tick observes the same pitch
  // configuration.
  std::uint8_t parallel_prev_pitch = context.prev_pitch;
  Tick parallel_prev_tick = context.prev_end_tick > 0 ? context.prev_end_tick - stride : 0;
  bool have_parallel_anchor = context.prev_pitch != 0;

  // For leap-resolution. We need both the previous pitch and the one
  // before it to detect a leap that the candidate must not continue
  // with another leap. Seed the local cursor from the context so the
  // rule applies across span boundaries, then update on each commit.
  std::uint8_t pre_prev_pitch_local = context.pre_prev_pitch;
  std::uint8_t prev_pitch_local = context.prev_pitch;

  // Leave-side passing-tone tracker. True iff the most recent commit
  // (in this span, or carried from the previous span via context)
  // landed on a non-chord-tone, so the next pitch must be a step
  // (≤2) from prev_pitch_local. Without this, the search can pick a
  // non-triad passing tone whose next-pitch choice ignores the
  // approach-rule recursion, violating the Validator's
  // `unprepared_dissonance` rule. Updated after each commit.
  bool prev_was_pt_local = context.prev_was_passing_tone;

  for (Tick t = span.start_tick; t < span.end_tick; t += stride) {
    const ChordEvent& chord = activeChord(harmonic_plan, t);
    const auto triad = triadPitchClasses(chord);

    // Enumerate triad-tone pitches in [voice_center - 7, voice_center + 12].
    int best_pitch = -1;
    float best_score = -1.0f;
    RuleIdMask best_rules = 0;
    for (int p = context.voice_center - 7; p <= context.voice_center + 12; ++p) {
      if (p < 0 || p > 127)
        continue;
      const std::uint8_t pc = static_cast<std::uint8_t>(p % 12);
      const bool is_triad = (pc == triad[0]) || (pc == triad[1]) || (pc == triad[2]);
      const bool strong = isStrongBeat(t);
      if (strong && !is_triad)
        continue;  // strong-beat consonance rule

      // Vertical rule: reject candidate that crosses any already-placed
      // voice (lower voice index must stay above higher voice index).
      if (context.placed_notes != nullptr &&
          createsVoiceCrossing(*context.placed_notes, span.voice, static_cast<std::uint8_t>(p),
                               t)) {
        continue;
      }

      // Vertical rule: on strong beats, candidate must form a
      // consonant interval with every already-placed voice that is
      // sounding at this tick. Weak beats apply a soft score penalty
      // instead (handled below in the score block) so passing-tone
      // dissonances remain reachable when no consonant option fits.
      const bool vertically_dissonant = context.placed_notes != nullptr &&
                                        createsVerticalDissonance(*context.placed_notes, span.voice,
                                                                  static_cast<std::uint8_t>(p), t);
      if (strong && vertically_dissonant) {
        continue;
      }

      // Vertical rule: reject candidate that creates parallel perfect
      // motion against any already-placed voice. Only attempted when
      // an other-voice context is supplied AND we have a valid prev
      // anchor in this voice.
      if (context.placed_notes != nullptr && have_parallel_anchor &&
          createsParallelPerfect(*context.placed_notes, span.voice, static_cast<std::uint8_t>(p), t,
                                 parallel_prev_pitch, parallel_prev_tick)) {
        continue;
      }

      // Melodic rule: if the previous motion (pre_prev -> prev) was
      // a wide leap (>= P5), forbid a second wide leap from prev to
      // this candidate. The single-leap case stays unconstrained.
      if (pre_prev_pitch_local != 0 && prev_pitch_local != 0) {
        const int delta_pre =
            static_cast<int>(prev_pitch_local) - static_cast<int>(pre_prev_pitch_local);
        const int delta_cur = p - static_cast<int>(prev_pitch_local);
        if (std::abs(delta_pre) >= 7 && std::abs(delta_cur) >= 7) {
          continue;
        }
      }

      // Passing-tone rule (approach side): a weak-beat non-chord tone
      // must be approached by step (<= 2 semis).
      if (!strong && !is_triad && prev_pitch_local != 0) {
        if (std::abs(p - static_cast<int>(prev_pitch_local)) > 2) {
          continue;
        }
      }

      // Passing-tone rule (leave side): if the previous commit was a
      // non-chord-tone (passing tone), the current candidate must be
      // within 2 semis of prev_pitch_local. This is the in-cascade
      // counterpart of the Validator's `unprepared_dissonance` rule —
      // by enforcing it during enumeration we avoid generating
      // notes the Validator would later reject. Without this guard,
      // the search could pick a non-triad p at position k, then at
      // position k+1 pick a triad-tone 3+ semis away (because the
      // triad bonus dominates), violating the rule.
      if (prev_was_pt_local && prev_pitch_local != 0) {
        if (std::abs(p - static_cast<int>(prev_pitch_local)) > 2) {
          continue;
        }
      }

      // Non-triad weak-pick lookahead. When the next position is a
      // strong beat (new bar / chord change), it must be a triad tone
      // of the new chord AND, by leave-side rule above, within ±2 of
      // p. If no triad tone of the next chord fits that ±2 window
      // AND survives vertical-dissonance AND parallel-perfect against
      // already-placed voices at t_next, p is a dead-end — picking
      // it would force the next position to emit nothing. Reject p
      // so the search falls back to a triad of THIS chord (which has
      // no leave-side restriction).
      //
      // Lookahead crosses span boundaries because the test plan often
      // aligns bar = span (so t_next == span.end_tick at the last
      // weak beat). Leave-side enforcement is already span-crossing
      // via CandidateContext::prev_was_passing_tone, so the lookahead
      // must agree.
      if (!strong && !is_triad) {
        const Tick t_next = t + stride;
        // Cross-span fixed-next leave-side check: when a Carrier span
        // (SubjectCarrier or AnswerCarrier) has already placed a note
        // at t_next in this voice, the Validator's
        // `unprepared_dissonance` rule requires |p - fixed_next| ≤ 2.
        // The intra-span leave-side rule below (prev_was_pt_local +
        // approach-side) does not see this because t_next is in a
        // different span. Phase 4 V1 counterline bar 3 → V1
        // AnswerCarrier bar 4 is the canonical case.
        if (context.placed_notes != nullptr) {
          const std::uint8_t fixed_next =
              sameVoiceStartingAt(*context.placed_notes, span.voice, t_next);
          if (fixed_next != 0 && std::abs(p - static_cast<int>(fixed_next)) > 2) {
            continue;
          }
        }
        if (isStrongBeat(t_next)) {
          const ChordEvent& chord_next_lh = activeChord(harmonic_plan, t_next);
          const auto triad_next = triadPitchClasses(chord_next_lh);
          bool has_strong_followup = false;
          for (int q = p - 2; q <= p + 2; ++q) {
            if (q < 0 || q > 127)
              continue;
            const std::uint8_t pc_q = static_cast<std::uint8_t>(q % 12);
            const bool q_is_triad =
                (pc_q == triad_next[0]) || (pc_q == triad_next[1]) || (pc_q == triad_next[2]);
            if (!q_is_triad)
              continue;
            if (context.placed_notes != nullptr &&
                createsVerticalDissonance(*context.placed_notes, span.voice,
                                          static_cast<std::uint8_t>(q), t_next)) {
              continue;
            }
            // Parallel-perfect against placed voices at t_next, with
            // the candidate p (at current tick t) as the "prev" anchor.
            // Without this, the lookahead can claim a triad-tone
            // followup that the actual enumeration at t_next will
            // reject for parallel motion (observed: V2 58→60 across a
            // bar boundary forms a parallel P5 with V0 77→79).
            if (context.placed_notes != nullptr &&
                createsParallelPerfect(*context.placed_notes, span.voice,
                                       static_cast<std::uint8_t>(q), t_next,
                                       static_cast<std::uint8_t>(p), t)) {
              continue;
            }
            has_strong_followup = true;
            break;
          }
          if (!has_strong_followup)
            continue;
        }
      }

      float score = is_triad ? 0.8f : 0.4f;
      // Prefer pitches close to the voice center. Quarter mode uses
      // the original 0.01 weight; Eighth mode uses 0.025 so the
      // tessitura anchor outweighs the per-position prev_pitch_local
      // cursor below — without this stronger anchor at finer
      // resolution, the cursor can drift away from voice_center
      // across consecutive weak-eighth picks, producing descending
      // cascades (see three-voice Eighth V2 at bar 0 mid-bar before
      // this calibration).
      const float center_weight = (span.subdivision == Subdivision::Eighth) ? 0.025f : 0.01f;
      score -= center_weight * static_cast<float>(std::abs(p - context.voice_center));
      // Prefer pitches close to the immediately-preceding pitch. Uses
      // `prev_pitch_local` (the per-position cursor) rather than
      // `context.prev_pitch` (fixed at span entry) so the distance term
      // tracks the just-committed pitch inside a multi-position span.
      // Without this, a candidate's prev-distance is measured against
      // the span-entry pitch even after several intra-span commits —
      // which biases later positions toward the span-entry pitch and
      // can favor a leap back toward span entry over holding the
      // recently-committed pitch.
      if (prev_pitch_local != 0) {
        score -= 0.02f * static_cast<float>(std::abs(p - prev_pitch_local));
      }
      // Soft penalty for weak-beat vertical dissonance. Strong beats
      // are already hard-rejected above. The penalty needs to exceed
      // the prev-distance gap to a reachable consonant alternative, so
      // 0.15 (roughly seven semitones of prev-distance) is enough to
      // flip the choice when one exists but small enough that a fully
      // unreachable consonant set still lets a dissonant pick through.
      if (!strong && vertically_dissonant) {
        score -= 0.15f;
      }

      // Quarter-mode unison-run penalty. When pre_prev and prev are
      // the same pitch (two consecutive unisons), a 3rd consecutive
      // unison candidate gets a 0.15 deduction so the broad step
      // bonus below can pull the search off the held pitch. Gated on
      // pre_prev != 0 to preserve the `QuarterSubdivisionKeepsHeldPitch`
      // regression (its single fixture has pre_prev=0, so 64 wins at
      // delta 0).
      if (span.subdivision == Subdivision::Quarter && !strong && pre_prev_pitch_local != 0 &&
          pre_prev_pitch_local == prev_pitch_local && p == static_cast<int>(prev_pitch_local)) {
        score -= 0.15f;
      }

      // Quarter-mode broad step bonus. Applies on every weak position
      // (not just unison-break) so the search prefers conjunct
      // passing-tone motion over triad-tone leaps whenever both are
      // feasible. Safe because:
      //   - Leave-side enforcement guarantees the non-triad's next
      //     pitch is ≤2 from p, so we don't generate validator-
      //     failing notes.
      //   - 1-step lookahead before strong beats rejects non-triad p
      //     when no triad of the next chord fits ±2 of p AND survives
      //     vertical-dissonance against placed voices.
      //
      // Bonus size 0.42 is the regression-budget maximum: the
      // `QuarterSubdivisionKeepsHeldPitch` fixture asserts unison 64
      // wins at prev=64, center=64; that gives unison score 0.80 vs
      // step candidate 0.40 + S - 0.03, so S < 0.43 preserves the
      // assertion. At 0.42 the unison still wins by 0.01 when no
      // vertical pressure is present, but any vertical penalty on
      // the unison flips the choice to a step.
      //
      // Applies to all voices: bass voices benefit too because the
      // alternative (holding a chord tone that becomes dissonant
      // when V0 moves) carries weak-beat soft penalty anyway, so a
      // diatonic step neighbor often outranks the dissonant unison.
      if (span.subdivision == Subdivision::Quarter && !strong && !is_triad &&
          prev_pitch_local != 0) {
        const int delta = std::abs(p - static_cast<int>(prev_pitch_local));
        if (delta == 1 || delta == 2) {
          score += 0.42f;
        }
      }

      // Chromatic-passing-tone penalty (Quarter, weak-position,
      // non-triad). Mirrors the Eighth-mode chromatic penalty: the
      // step bonus above lifts any non-triad step, including
      // non-diatonic ones; the 0.05 penalty keeps chromatic motion
      // reachable when no diatonic alternative survives the cascade
      // but lets the diatonic neighbor win every tie.
      if (span.subdivision == Subdivision::Quarter && !strong && !is_triad && !diatonic_pc[pc]) {
        score -= 0.05f;
      }

      // Eighth-note motion bias (two parts, only at Subdivision::Eighth):
      //
      // (a) Penalize weak-position unison. Without this, the score
      //     function picks the previous pitch whenever it is a triad
      //     tone near voice_center — the prev-distance penalty alone
      //     is 0 at delta 0. Bach eighth-note counterlines almost
      //     never sit on the same eighth twice in a row.
      //
      // (b) Bonus non-triad step approach. The triad bonus (0.40 gap
      //     between triad and non-triad starting scores) otherwise
      //     dominates, biasing eighth motion toward chord-tone leaps
      //     (m3, P4) rather than the stepwise passing motion that
      //     defines Bach idiom. A 0.40 bonus on non-triad candidates
      //     within a whole step of prev lifts genuine passing tones
      //     above triad-tone leaps of comparable distance.
      //
      // Both branches measure delta against `prev_pitch_local` (the
      // per-position cursor that advances on every commit), not
      // `context.prev_pitch` (which is fixed at span entry). Using the
      // local cursor is what lets the rule see position-to-position
      // motion inside a span; the span-entry value alone would mark
      // two consecutive eighths as non-unison whenever the second
      // happens to equal the span's entry pitch.
      //
      // Quarter subdivision keeps the original scoring: held pitches
      // at quarter resolution are musically valid (suspensions, pedal
      // tones, etc.), and quarter-level triad oscillation is fine.
      if (span.subdivision == Subdivision::Eighth && !strong && prev_pitch_local != 0) {
        const int delta = std::abs(p - static_cast<int>(prev_pitch_local));
        if (delta == 0) {
          score -= 0.15f;
        } else if (!is_triad && (delta == 1 || delta == 2)) {
          score += 0.40f;
        }
      }

      // Chromatic-passing-tone penalty (Eighth-only, weak-position).
      // The step bonus above lifts any non-triad pitch within a whole
      // step of prev; without further bias, a chromatic candidate
      // enumerated before a diatonic alternative wins on equal score
      // (loop iterates pitch ascending and uses strict `>`). The 0.05
      // penalty keeps chromatic motion reachable when the rule cascade
      // leaves no diatonic option, but lets diatonic steps win every
      // tie.
      if (span.subdivision == Subdivision::Eighth && !strong && !is_triad && !diatonic_pc[pc]) {
        score -= 0.05f;
      }

      RuleIdMask rules = 0;
      if (is_triad)
        rules |= 1ull << 0;  // rule[0] = ChordTone
      if (strong && is_triad)
        rules |= 1ull << 1;  // rule[1] = StrongBeatConsonance
      if (context.prev_pitch > 0 && std::abs(p - context.prev_pitch) <= 4) {
        rules |= 1ull << 2;  // rule[2] = SmallStep
      }
      if (context.placed_notes != nullptr) {
        rules |= 1ull << 3;  // rule[3] = ParallelPerfectChecked
        rules |= 1ull << 4;  // rule[4] = VoiceCrossingChecked
        // rule[7] = VerticalConsonanceChecked. Marks the candidate as
        // consonant against all currently-placed voices at this tick.
        // Strong beats are guaranteed (the rejection above would have
        // skipped a dissonant `p`); weak beats only set the bit when
        // they happen to avoid dissonance (i.e. the soft penalty did
        // not have to apply).
        if (!vertically_dissonant) {
          rules |= 1ull << 7;
        }
      }
      if (pre_prev_pitch_local != 0 && prev_pitch_local != 0) {
        rules |= 1ull << 5;  // rule[5] = LeapResolutionChecked
      }
      if (!strong && prev_pitch_local != 0) {
        rules |= 1ull << 6;  // rule[6] = WeakBeatPassingChecked
      }

      if (score > best_score) {
        best_score = score;
        best_pitch = p;
        best_rules = rules;
      }
    }

    if (best_pitch < 0)
      continue;  // span saturated; caller handles

    Candidate c;
    c.start_tick = t;
    c.duration = stride;
    c.pitch = static_cast<std::uint8_t>(best_pitch);
    c.score = best_score;
    c.satisfied_rules = best_rules;
    out.push_back(c);

    parallel_prev_pitch = c.pitch;
    parallel_prev_tick = t;
    have_parallel_anchor = true;
    pre_prev_pitch_local = prev_pitch_local;
    prev_pitch_local = c.pitch;
    // The picked candidate is a triad-tone iff bit 0 of satisfied_rules
    // is set (rule[0] = ChordTone). Use that to update the leave-side
    // tracker — non-triad commits require the next position to be
    // within ±2.
    prev_was_pt_local = ((best_rules & (1ull << 0)) == 0);
  }
  return out;
}

}  // namespace bach::composer
