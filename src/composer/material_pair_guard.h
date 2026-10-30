#ifndef BACH_COMPOSER_MATERIAL_PAIR_GUARD_H
#define BACH_COMPOSER_MATERIAL_PAIR_GUARD_H

#include <cstdint>
#include <vector>

#include "composer/texture_helpers.h"
#include "core/basic_types.h"

/// @file
/// @brief The one judgement every builder applies when it places a tone of
///        designed material against voices that already sound.
///
/// Generation-scope validation exempts Material/Material pairs, so a builder
/// that emits a closed-rule fault learns about it only when the finished piece
/// is refused. Every placement site therefore judges its tone the way the
/// validator will read it: against what each other voice sounds at the union
/// onset immediately before the tone (sampled at `tick - 1`), and for its whole
/// sustain where register order is concerned. A strict parallel perfect and a
/// voice crossing are the two closed faults; they rank below every in-band
/// alternative, including a passing dissonance and holding the previous tone.

namespace bach::composer {

/// @brief Perfect-interval fault classes, ordered from mildest to worst.
enum class PairFault : std::uint8_t {
  kNone = 0,
  kBattuta = 1,
  kAntiParallel = 2,
  kHidden = 3,
  kStrict = 4,
};

/// @brief True for a strict (true) parallel fifth or octave: the same perfect
///        class at both onsets, reached by same-direction motion.
///
/// The single definition of the strict test for builder guards; the hidden
/// perfect is excluded. Any negative pitch means a voice is silent and the
/// motion cannot be judged.
///
/// @param line_prev The line under construction's previous pitch (-1 = none).
/// @param cand The line under construction's candidate pitch.
/// @param other_prev The other voice's pitch at the previous union onset.
/// @param other_curr The other voice's pitch at the candidate's onset.
bool isStrictParallelPerfect(int line_prev, int cand, int other_prev, int other_curr);

/// @brief Worst fault class one (line, other) motion forms.
PairFault classifyPairFault(int line_prev, int cand, int other_prev, int other_curr);

/// @brief Worst fault class a candidate forms against every concurrent motion.
PairFault worstPairFault(int line_prev, int cand, const std::vector<ConcurrentMotion>& motions);

/// @brief Every other voice's motion into `tick`, read at the union onset just
///        before it -- the pair the validator judges.
///
/// The previous pitch is what each voice sounds at `tick - 1`, so a voice
/// sustaining across faster onsets elsewhere is paired with its held tone, and
/// a rest before `tick` breaks the succession exactly as the validator does.
void unionOnsetMotions(const ThemeToneRegistry& registry, Tick tick, VoiceId voice,
                       VoiceId num_voices, std::vector<ConcurrentMotion>& out_motions);

/// @brief True when `pitch` held by `voice` over [start, end) crosses any other
///        voice sounding in that window (a lower voice index sounds higher).
///
/// A unison is not a crossing, matching the validator.
bool crossesSoundingVoices(const ThemeToneRegistry& registry, VoiceId voice, VoiceId num_voices,
                           int pitch, Tick start, Tick end);

/// @brief Ranks candidate tones for one placement by the shared fault order.
///
/// Lexicographic key: crossing, then strict parallel, then the caller's own
/// site cost, then the lesser fault class, then offer order. A caller that
/// wants the lesser classes to outrank its own preferences folds them into
/// `site_cost`. Offering the previous tone (an oblique hold) always gives the
/// chooser a strict-free candidate.
class MaterialPairChooser {
 public:
  /// @param line_prev The line's pitch sounding just before the onset (-1 = none).
  /// @param motions Concurrent motions into the onset (see unionOnsetMotions).
  MaterialPairChooser(int line_prev, const std::vector<ConcurrentMotion>& motions);

  /// @brief Offer a candidate.
  ///
  /// @param pitch Candidate pitch.
  /// @param site_cost Caller preference, lower is better.
  /// @param crosses Whether the candidate crosses another voice over its sustain.
  void offer(int pitch, int site_cost, bool crosses = false);

  /// @brief Best candidate so far, or -1 when nothing was offered.
  int best() const { return best_pitch_; }

  /// @brief Fault class of the best candidate.
  PairFault bestFault() const { return best_fault_; }

  /// @brief Whether the best candidate crosses another voice.
  bool bestCrosses() const { return best_crosses_; }

 private:
  int line_prev_;
  const std::vector<ConcurrentMotion>& motions_;
  int best_pitch_ = -1;
  int best_cost_ = 0;
  PairFault best_fault_ = PairFault::kNone;
  bool best_crosses_ = false;
};

}  // namespace bach::composer

#endif  // BACH_COMPOSER_MATERIAL_PAIR_GUARD_H
