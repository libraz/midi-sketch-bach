#include "composer/material_pair_guard.h"

#include <algorithm>

#include "core/pitch_utils.h"

namespace bach::composer {

bool isStrictParallelPerfect(int line_prev, int cand, int other_prev, int other_curr) {
  return classifyPairFault(line_prev, cand, other_prev, other_curr) == PairFault::kStrict;
}

PairFault classifyPairFault(int line_prev, int cand, int other_prev, int other_curr) {
  if (line_prev < 0 || cand < 0 || other_prev < 0 || other_curr < 0)
    return PairFault::kNone;
  // The upper line of the arriving pair goes first, as every predicate expects.
  const bool line_upper = cand >= other_curr;
  const int upper_prev = line_upper ? line_prev : other_prev;
  const int upper_curr = line_upper ? cand : other_curr;
  const int lower_prev = line_upper ? other_prev : line_prev;
  const int lower_curr = line_upper ? other_curr : cand;
  switch (classifyPerfectMotion(upper_prev, upper_curr, lower_prev, lower_curr)) {
    case PerfectMotionKind::ParallelFifth:
    case PerfectMotionKind::ParallelOctave:
      return PairFault::kStrict;
    case PerfectMotionKind::HiddenFifth:
    case PerfectMotionKind::HiddenOctave:
      return PairFault::kHidden;
    case PerfectMotionKind::None:
      break;
  }
  if (isAntiParallelPerfectMotion(upper_prev, upper_curr, lower_prev, lower_curr))
    return PairFault::kAntiParallel;
  if (isBattutaMotion(upper_prev, upper_curr, lower_prev, lower_curr))
    return PairFault::kBattuta;
  return PairFault::kNone;
}

PairFault worstPairFault(int line_prev, int cand, const std::vector<ConcurrentMotion>& motions) {
  PairFault worst = PairFault::kNone;
  for (const ConcurrentMotion& motion : motions)
    worst = std::max(worst, classifyPairFault(line_prev, cand, motion.prev, motion.curr));
  return worst;
}

void unionOnsetMotions(const ThemeToneRegistry& registry, Tick tick, VoiceId voice,
                       VoiceId num_voices, std::vector<ConcurrentMotion>& out_motions) {
  if (tick == 0) {
    out_motions.clear();
    return;
  }
  registry.concurrentMotions(tick - 1, tick, voice, num_voices, out_motions);
}

bool crossesSoundingVoices(const ThemeToneRegistry& registry, VoiceId voice, VoiceId num_voices,
                           int pitch, Tick start, Tick end) {
  for (VoiceId other = 0; other < num_voices; ++other) {
    if (other == voice)
      continue;
    int lowest = -1;
    int highest = -1;
    registry.pitchRangeDuring(other, start, end, lowest, highest);
    if (lowest < 0)
      continue;
    if (other < voice ? lowest < pitch : highest > pitch)
      return true;
  }
  return false;
}

MaterialPairChooser::MaterialPairChooser(int line_prev,
                                         const std::vector<ConcurrentMotion>& motions)
    : line_prev_(line_prev), motions_(motions) {}

void MaterialPairChooser::offer(int pitch, int site_cost, bool crosses) {
  const PairFault fault = worstPairFault(line_prev_, pitch, motions_);
  if (best_pitch_ >= 0) {
    const bool strict = fault == PairFault::kStrict;
    const bool best_strict = best_fault_ == PairFault::kStrict;
    if (crosses != best_crosses_) {
      if (crosses)
        return;
    } else if (strict != best_strict) {
      if (strict)
        return;
    } else if (site_cost != best_cost_) {
      if (site_cost > best_cost_)
        return;
    } else if (fault >= best_fault_) {
      return;
    }
  }
  best_pitch_ = pitch;
  best_cost_ = site_cost;
  best_fault_ = fault;
  best_crosses_ = crosses;
}

}  // namespace bach::composer
