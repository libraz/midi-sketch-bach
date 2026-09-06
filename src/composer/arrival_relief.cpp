#include "composer/arrival_relief.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>

#include "composer/bar_material.h"

namespace bach::composer {

namespace {

using detail::Mode;

// Displace an arrival that the way in cannot answer for.
//
// Reached only when the tone before it is a bar head, which is structural and
// may not move, and the arrival itself is not -- so the arrival is a tone of the
// figure and the one free end left.
//
// How far it may travel is read off the figure rather than fixed: no further
// than the wider of the two intervals it already spans, and at least a step. A
// tone embedded in a run may then only step, which is what keeps the run's
// conjunct surface, while one the design already leaps to and from may be
// re-aimed as freely as the design itself moves. A repair that trades a
// contrapuntal blemish for a hole in the figure is not a repair.
//
// Non-regressive at both ends, like the way-in re-aim: the replacement lowers
// the fault formed arriving here and may not raise the one formed leaving.
void relieveRunningArrival(const std::vector<MaterialNote*>& line, std::size_t arrival_idx,
                           const ThemeToneRegistry& registry, VoiceId voice, VoiceId num_voices,
                           Mode mode, const std::vector<ConcurrentMotion>& into_arrival,
                           int approach, int original_rank) {
  MaterialNote& note = *line[arrival_idx];
  const int original = static_cast<int>(note.pitch);

  std::vector<ConcurrentMotion> at_arrival;
  registry.concurrentMotions(note.start_tick - 1, note.start_tick, voice, num_voices, at_arrival);
  bool original_consonant = true;
  for (const ConcurrentMotion& motion : at_arrival) {
    if (!isConsonantPair(original, motion.curr)) {
      original_consonant = false;
      break;
    }
  }

  std::vector<ConcurrentMotion> out_of_arrival;
  int next_pitch = -1;
  if (arrival_idx + 1 < line.size()) {
    const MaterialNote& next = *line[arrival_idx + 1];
    next_pitch = static_cast<int>(next.pitch);
    registry.concurrentMotions(next.start_tick - kSixteenth, next.start_tick, voice, num_voices,
                               out_of_arrival);
  }
  const int exit_ceiling =
      next_pitch < 0 ? 0 : perfectFaultRank(original, next_pitch, out_of_arrival);
  int displacement = std::max(2, std::abs(original - approach));
  if (next_pitch >= 0)
    displacement = std::max(displacement, std::abs(original - next_pitch));

  // A replacement that takes a neighbour's pitch the displaced tone did not
  // already share lengthens a static run, so it is tried only after the window
  // has been swept without one.
  const auto flattens = [&](int cand) {
    return (cand == approach && original != approach) ||
           (next_pitch >= 0 && cand == next_pitch && original != next_pitch);
  };
  for (int accept = 0; accept < original_rank; ++accept) {
    for (const bool allow_flatten : {false, true}) {
      for (int dist = 1; dist <= displacement; ++dist) {
        for (const int sgn : {-1, 1}) {
          const int cand = original + sgn * dist;
          if (!detail::inScale(cand, mode) || (!allow_flatten && flattens(cand)))
            continue;
          bool admissible = true;
          for (const ConcurrentMotion& motion : at_arrival) {
            // A lower voice index sounds higher.
            if (motion.voice < voice ? cand >= motion.curr : cand <= motion.curr) {
              admissible = false;
              break;
            }
            if (original_consonant && !isConsonantPair(cand, motion.curr)) {
              admissible = false;
              break;
            }
          }
          if (!admissible || perfectFaultRank(approach, cand, into_arrival) > accept)
            continue;
          if (next_pitch >= 0 && perfectFaultRank(cand, next_pitch, out_of_arrival) > exit_ceiling)
            continue;
          note.pitch = static_cast<std::uint8_t>(cand);
          return;
        }
      }
    }
  }
}

}  // namespace

int perfectFaultRank(int prev, int curr, const std::vector<ConcurrentMotion>& motions) {
  int worst = 0;
  for (const ConcurrentMotion& motion : motions) {
    if (formsPerfectParallel(prev, curr, motion.prev, motion.curr))
      return 3;
    if (formsAntiParallelPerfect(prev, curr, motion.prev, motion.curr))
      worst = std::max(worst, 2);
    else if (formsBattuta(prev, curr, motion.prev, motion.curr))
      worst = std::max(worst, 1);
  }
  return worst;
}

std::vector<MaterialNote*> lineInTickOrder(const std::vector<std::vector<MaterialNote>*>& parts) {
  std::vector<MaterialNote*> line;
  for (std::vector<MaterialNote>* part : parts) {
    for (MaterialNote& note : *part)
      line.push_back(&note);
  }
  std::stable_sort(line.begin(), line.end(), [](const MaterialNote* lhs, const MaterialNote* rhs) {
    return lhs->start_tick < rhs->start_tick;
  });
  return line;
}

void relieveArrivals(const std::vector<MaterialNote*>& line, const ThemeToneRegistry& registry,
                     VoiceId voice, VoiceId num_voices, int bars, Mode mode, Tick arrival_grain) {
  const int arrival_count =
      arrival_grain > 0 ? static_cast<int>(static_cast<Tick>(bars) * kTicksPerBar / arrival_grain)
                        : 0;
  std::vector<ConcurrentMotion> into_head;
  std::vector<ConcurrentMotion> into_onset;
  std::vector<ConcurrentMotion> at_onset;
  for (int arrival_index = 1; arrival_index < arrival_count; ++arrival_index) {
    const Tick head = static_cast<Tick>(arrival_index) * arrival_grain;
    std::size_t approach_idx = line.size();
    std::size_t arrival_idx = line.size();
    for (std::size_t idx = 0; idx < line.size(); ++idx) {
      const Tick start = line[idx]->start_tick;
      if (start < head)
        approach_idx = idx;
      else if (start == head)
        arrival_idx = idx;
      else
        break;
    }
    if (arrival_idx == line.size() || approach_idx == line.size())
      continue;
    MaterialNote& approach = *line[approach_idx];
    const int arrival = static_cast<int>(line[arrival_idx]->pitch);
    const int original = static_cast<int>(approach.pitch);
    const int own_prev = approach_idx > 0 ? static_cast<int>(line[approach_idx - 1]->pitch) : -1;

    // Sampled one sixteenth back, the grain at which a union-onset reading pairs
    // an arrival with each other voice's last preceding onset.
    into_head.clear();
    registry.concurrentMotions(head - kSixteenth, head, voice, num_voices, into_head);
    const int original_rank = perfectFaultRank(original, arrival, into_head);
    if (original_rank == 0)
      continue;

    if (approach.start_tick % kTicksPerBar == 0) {
      // Nothing on the way in is free: the tone before this arrival is a bar
      // head, pinned at both ends. Where the arrival is a bar head too there is
      // no free tone at all and the fault stands. Off the downbeat the arrival
      // is instead a running tone, so it is the one that moves -- by a step, so
      // the figure keeps its conjunct surface, and only where it neither
      // dissolves the register order nor worsens the motion out of the beat.
      if (head % kTicksPerBar == 0)
        continue;
      relieveRunningArrival(line, arrival_idx, registry, voice, num_voices, mode, into_head,
                            original, original_rank);
      continue;
    }

    into_onset.clear();
    registry.concurrentMotions(approach.start_tick - kSixteenth, approach.start_tick, voice,
                               num_voices, into_onset);
    const int onset_ceiling = perfectFaultRank(own_prev, original, into_onset);
    at_onset.clear();
    registry.concurrentMotions(approach.start_tick - 1, approach.start_tick, voice, num_voices,
                               at_onset);
    bool original_consonant = true;
    for (const ConcurrentMotion& motion : at_onset) {
      if (!isConsonantPair(original, motion.curr)) {
        original_consonant = false;
        break;
      }
    }
    // Whether the fault being repaired is a true parallel rather than one of the
    // weaker perfect approaches. Only that one is worth a vertical price.
    bool original_strict = false;
    for (const ConcurrentMotion& motion : into_head) {
      if (formsStrictPerfectParallel(original, arrival, motion.prev, motion.curr)) {
        original_strict = true;
        break;
      }
    }
    // The replacement sits between two fixed tones, and both intervals need a
    // ceiling: bounding only the leap into the head leaves the approach free to
    // be reached by a leap of its own, which is a registral break whether or not
    // it resolves the perfect interval. The two ceilings are not the same size.
    // Leaving the head is the constrained end, so it holds to what the designed
    // tone already spanned; entering is the free end, and is asked only not to
    // exceed an octave, because tightening it there costs more perfect intervals
    // elsewhere than the wider choice ever buys.
    const int leap_ceiling = std::max(7, std::abs(arrival - original));
    const int entry_ceiling = own_prev < 0 ? 0 : std::max(12, std::abs(original - own_prev));
    // Whether the replacement takes a neighbour's pitch that the displaced tone
    // did not already share. Such a tone lengthens a static run, so it is tried
    // only once the window has been swept without one -- ranked, not vetoed,
    // because a rule this pass has closed leaves no room to decline a repair.
    auto flattens = [&](int cand) {
      return (cand == own_prev && original != own_prev) || (cand == arrival && original != arrival);
    };
    auto admissible = [&](int cand, bool allow_dissonance) {
      if (!detail::inScale(cand, mode) || std::abs(arrival - cand) > leap_ceiling)
        return false;
      if (own_prev >= 0 && std::abs(cand - own_prev) > entry_ceiling)
        return false;
      for (const ConcurrentMotion& motion : at_onset) {
        // A lower voice index sounds higher.
        if (motion.voice < voice ? cand >= motion.curr : cand <= motion.curr)
          return false;
        if (original_consonant && !allow_dissonance && !isConsonantPair(cand, motion.curr))
          return false;
      }
      return perfectFaultRank(own_prev, cand, into_onset) <= onset_ceiling;
    };

    // Clean first, then progressively less clean, but never at or below the
    // rank the displaced tone already carried.
    //
    // The sweep covers the whole admissible window and is only ORDERED by
    // distance from the tone it displaces. What constrains a replacement is the
    // two intervals it spans; staying near the designed tone is a preference.
    // Bounding the sweep at the designed tone instead conflated the two, and put
    // the stepwise neighbourhood of the arrival out of reach exactly when the
    // design leaps into the head -- and a step into a perfect interval is the one
    // approach that is legal however the other voice moves.
    //
    // The consonance the replacement inherits is a preference, not a bound. A
    // tone displacing a consonant one is asked to stay consonant, so the pass
    // cannot trade a perfect interval for a vertical clash. But where the fault
    // is a TRUE parallel and the consonant window comes back empty, the tone
    // stands and the parallel ships -- and a passing dissonance is the smaller
    // fault of the two. So the consonance clause is dropped on a second sweep,
    // reached only after the first has been walked in full: the same trade the
    // bass's own forward guard makes one voice below.
    const int reach = 2 * leap_ceiling;
    bool placed = false;
    for (const bool allow_dissonance : {false, true}) {
      if (allow_dissonance && (placed || !original_strict || !original_consonant))
        break;
      for (int accept = 0; accept < original_rank && !placed; ++accept) {
        for (const bool allow_flatten : {false, true}) {
          for (int dist = 1; dist <= reach && !placed; ++dist) {
            for (const int sgn : {-1, 1}) {
              const int cand = original + sgn * dist;
              if ((allow_flatten || !flattens(cand)) && admissible(cand, allow_dissonance) &&
                  perfectFaultRank(cand, arrival, into_head) <= accept) {
                approach.pitch = static_cast<std::uint8_t>(cand);
                placed = true;
                break;
              }
            }
          }
          if (placed)
            break;
        }
      }
    }
  }
}

}  // namespace bach::composer
