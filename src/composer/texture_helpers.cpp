#include "composer/texture_helpers.h"

#include <algorithm>

#include "composer/minor_material.h"
#include "composer/rule_helpers.h"
#include "core/pitch_utils.h"

namespace bach::composer {

int octaveOffsetForBand(const std::array<std::uint8_t, 16>& subject, int base_semis, int voice,
                        const std::array<int, 3>& band_lo, const std::array<int, 3>& band_hi) {
  if (voice < 0 || voice >= static_cast<int>(band_lo.size())) {
    return 0;
  }
  int lo = 127;
  int hi = 0;
  for (std::uint8_t pitch : subject) {
    lo = std::min(lo, static_cast<int>(pitch) + base_semis);
    hi = std::max(hi, static_cast<int>(pitch) + base_semis);
  }
  int offset = 0;
  while (hi + offset > band_hi[static_cast<std::size_t>(voice)]) {
    offset -= 12;
  }
  while (lo + offset < band_lo[static_cast<std::size_t>(voice)] &&
         hi + offset + 12 <= band_hi[static_cast<std::size_t>(voice)]) {
    offset += 12;
  }
  return offset;
}

bool shouldUseTonalAnswer(const std::array<std::uint8_t, 16>& subject, std::uint8_t tonic_pc) {
  const std::uint8_t tonic = static_cast<std::uint8_t>(tonic_pc % 12);
  const std::uint8_t dominant = static_cast<std::uint8_t>((tonic + 7) % 12);
  const std::uint8_t first = static_cast<std::uint8_t>(subject[0] % 12);
  if (first == dominant) {
    return true;
  }
  if (first != tonic) {
    return false;
  }
  for (int i = 1; i < 4; ++i) {
    if (subject[static_cast<std::size_t>(i)] % 12 == dominant) {
      return true;
    }
  }
  return false;
}

std::vector<detail::ChordSpec> buildRepeatingChordPlan(int total_bars, detail::Mode mode,
                                                       int harmony_index) {
  const auto& patterns =
      mode == detail::Mode::Minor ? detail::kHarmonyPatternsMinor : detail::kHarmonyPatterns;
  std::vector<detail::ChordSpec> plan;
  plan.reserve(static_cast<std::size_t>(total_bars));
  for (int bar = 0; bar < total_bars; ++bar) {
    const auto& pattern = patterns[static_cast<std::size_t>((harmony_index + bar / 4) % 4)];
    plan.push_back(pattern[static_cast<std::size_t>(bar % 4)]);
  }
  return plan;
}

void markDominantSevenths(std::vector<detail::ChordSpec>& plan,
                          const std::vector<int>& triad_only_bars, detail::Mode mode, bool cyclic) {
  const std::size_t last = cyclic ? plan.size() : (plan.empty() ? 0 : plan.size() - 1);
  for (std::size_t bar = 0; bar < last; ++bar) {
    if (plan[bar].minor)
      continue;
    if (std::find(triad_only_bars.begin(), triad_only_bars.end(), static_cast<int>(bar)) !=
        triad_only_bars.end())
      continue;
    const std::size_t next = (bar + 1) % plan.size();
    const int resolution = (plan[bar].root_pc + 5) % 12;
    if (plan[next].root_pc % 12 != resolution)
      continue;
    if (!detail::inScale(detail::chordSeventhPc(plan[bar]), mode))
      continue;
    plan[bar].seventh = true;
  }
}

bool installSuspensionCarrier(Material& material, VoicePlan& voice_plan,
                              const SuspensionPattern& pattern) {
  if (pattern.preparation_tick >= pattern.suspension_tick ||
      pattern.suspension_tick >= pattern.resolution_tick)
    return false;
  // Resolution occupies one beat, then a one-beat rest completes the
  // two-beat figure before the original carrier resumes.
  const Tick carrier_end = pattern.resolution_tick + 2 * kTicksPerBeat;
  std::size_t containing = voice_plan.spans.size();
  SpanId next_id = 0;
  for (std::size_t i = 0; i < voice_plan.spans.size(); ++i) {
    const Span& span = voice_plan.spans[i];
    if (span.id != kInvalidSpanId)
      next_id = std::max(next_id, static_cast<SpanId>(span.id + 1));
    if (span.voice == pattern.voice && span.start_tick <= pattern.preparation_tick &&
        span.end_tick >= carrier_end && span.intent != VoiceIntent::SuspensionCarrier) {
      containing = i;
    }
  }
  if (containing == voice_plan.spans.size())
    return false;

  const Span original = voice_plan.spans[containing];
  std::vector<Span> replacement;
  replacement.reserve(3);
  if (original.start_tick < pattern.preparation_tick) {
    Span before = original;
    before.end_tick = pattern.preparation_tick;
    replacement.push_back(before);
  }
  Span suspension = original;
  suspension.id = replacement.empty() ? original.id : next_id++;
  suspension.start_tick = pattern.preparation_tick;
  suspension.end_tick = carrier_end;
  suspension.intent = VoiceIntent::SuspensionCarrier;
  replacement.push_back(suspension);
  if (carrier_end < original.end_tick) {
    Span after = original;
    after.id = next_id++;
    after.start_tick = carrier_end;
    replacement.push_back(after);
  }

  voice_plan.spans.erase(voice_plan.spans.begin() + static_cast<std::ptrdiff_t>(containing));
  voice_plan.spans.insert(voice_plan.spans.begin() + static_cast<std::ptrdiff_t>(containing),
                          replacement.begin(), replacement.end());
  material.suspension_patterns.push_back(pattern);
  return true;
}

int soundingMaterialPitch(const std::vector<MaterialNote>& notes, Tick tick) {
  int pitch = -1;
  Tick latest = 0;
  bool found = false;
  for (const auto& note : notes) {
    if (tick < note.start_tick || tick >= note.start_tick + note.duration)
      continue;
    if (!found || note.start_tick >= latest) {
      found = true;
      latest = note.start_tick;
      pitch = static_cast<int>(note.pitch);
    }
  }
  return pitch;
}

bool designUpperSuspension(SuspensionType type, Tick preparation_tick, Tick suspension_tick,
                           Tick resolution_tick, VoiceId voice, std::uint8_t bass_at_preparation,
                           std::uint8_t bass_at_suspension, std::uint8_t bass_at_resolution,
                           std::uint8_t upper_at_preparation, std::uint8_t upper_at_suspension,
                           std::uint8_t upper_at_resolution, int band_lo, int band_hi,
                           detail::Mode mode, SuspensionPattern* pattern) {
  if (pattern == nullptr || type == SuspensionType::Sus2_3 || preparation_tick >= suspension_tick ||
      suspension_tick >= resolution_tick || bass_at_preparation == 0 || bass_at_suspension == 0 ||
      bass_at_resolution == 0 || upper_at_preparation == 0 || upper_at_suspension == 0 ||
      upper_at_resolution == 0 || bass_at_preparation > 127 || bass_at_suspension > 127 ||
      bass_at_resolution > 127 || upper_at_preparation > 127 || upper_at_suspension > 127 ||
      upper_at_resolution > 127)
    return false;
  auto intervalClass = [](int upper, int bass) { return ((upper - bass) % 12 + 12) % 12; };
  for (int suspended = band_hi; suspended >= band_lo; --suspended) {
    if (!detail::inScale(suspended, mode))
      continue;
    if (!rule_helpers::isConsonantAboveBass(static_cast<std::uint8_t>(suspended),
                                            bass_at_preparation))
      continue;
    if (!isConsonantPair(suspended, upper_at_preparation) ||
        !isConsonantPair(suspended, upper_at_suspension))
      continue;
    for (int step = 1; step <= 2; ++step) {
      const int resolution = suspended - step;
      if (resolution < band_lo || !detail::inScale(resolution, mode))
        continue;
      if (!isConsonantPair(resolution, upper_at_resolution))
        continue;
      const int sus_ic = intervalClass(suspended, bass_at_suspension);
      const int res_ic = intervalClass(resolution, bass_at_resolution);
      bool matches = false;
      switch (type) {
        case SuspensionType::Sus4_3:
          matches = sus_ic == 5 && (res_ic == 3 || res_ic == 4);
          break;
        case SuspensionType::Sus7_6:
          matches = (sus_ic == 10 || sus_ic == 11) && (res_ic == 8 || res_ic == 9);
          break;
        case SuspensionType::Sus9_8:
          matches = (sus_ic == 1 || sus_ic == 2) && res_ic == 0;
          break;
        case SuspensionType::Sus2_3:
          break;
      }
      if (!matches)
        continue;
      pattern->type = type;
      pattern->preparation_tick = preparation_tick;
      pattern->suspension_tick = suspension_tick;
      pattern->resolution_tick = resolution_tick;
      pattern->preparation_pitch = static_cast<std::uint8_t>(suspended);
      pattern->suspension_pitch = static_cast<std::uint8_t>(suspended);
      pattern->resolution_pitch = static_cast<std::uint8_t>(resolution);
      pattern->voice = voice;
      return true;
    }
  }
  return false;
}

void ThemeToneRegistry::record(Tick tick, VoiceId voice, int pitch, Tick duration) {
  tones_.push_back(ThemeTone{tick, duration, voice, pitch});
}

int ThemeToneRegistry::soundingPitchInVoice(VoiceId voice, Tick tick) const {
  int pitch = -1;
  // Signed sentinel: Tick is unsigned, so a Tick(-1) start would wrap to the
  // maximum value and never lose the "latest onset wins" comparison.
  long long best_start = -1;
  for (const ThemeTone& tone : tones_) {
    if (tone.voice != voice) {
      continue;
    }
    if (tick >= tone.tick &&
        tick<tone.tick + tone.duration&& static_cast<long long>(tone.tick)> best_start) {
      best_start = static_cast<long long>(tone.tick);
      pitch = tone.pitch;
    }
  }
  return pitch;
}

void ThemeToneRegistry::concurrentThemePitches(Tick tick, VoiceId voice,
                                               std::vector<int>& out_pitches) const {
  out_pitches.clear();
  for (const ThemeTone& tone : tones_) {
    if (tone.voice == voice) {
      continue;
    }
    if (tick >= tone.tick && tick < tone.tick + tone.duration) {
      out_pitches.push_back(tone.pitch);
    }
  }
}

void ThemeToneRegistry::concurrentMotions(Tick prev_tick, Tick tick, VoiceId voice,
                                          VoiceId num_voices,
                                          std::vector<ConcurrentMotion>& out_motions) const {
  out_motions.clear();
  for (VoiceId other = 0; other < num_voices; ++other) {
    if (other == voice) {
      continue;
    }
    const int curr = soundingPitchInVoice(other, tick);
    if (curr < 0) {
      continue;
    }
    const int prev = (prev_tick >= 0) ? soundingPitchInVoice(other, prev_tick) : -1;
    out_motions.push_back(ConcurrentMotion{other, prev, curr});
  }
}

void appendScoredCountersubject(const std::vector<MaterialNote>& source, VoiceId voice, Tick start,
                                Tick end, int band_lo, int band_hi, detail::Mode mode,
                                std::vector<MaterialNote>& destination, ThemeToneRegistry& registry,
                                bool avoid_battuta, bool source_is_lowest) {
  struct Anchor {
    Tick tick = 0;
    Tick duration = 0;
    int pitch = 0;
  };

  const int center = (band_lo + band_hi) / 2;
  std::vector<Anchor> anchors;
  int previous_counter = -1;
  int previous_source = -1;
  int repeat_run = 1;
  for (const MaterialNote& note : source) {
    if (note.start_tick < start || note.start_tick >= end) {
      continue;
    }
    const int source_pitch = static_cast<int>(note.pitch);
    const int source_direction =
        previous_source < 0
            ? 0
            : (source_pitch > previous_source ? 1 : (source_pitch < previous_source ? -1 : 0));
    const int target = previous_counter < 0 ? center : previous_counter;
    int best_pitch = -1;
    int best_score = 1 << 30;
    for (int pitch = band_lo; pitch <= band_hi; ++pitch) {
      if (!detail::inScale(pitch, mode)) {
        continue;
      }
      const bool consonant = isConsonantIc(pitch - source_pitch);
      const int counter_direction =
          previous_counter < 0
              ? 0
              : (pitch > previous_counter ? 1 : (pitch < previous_counter ? -1 : 0));
      const bool similar = source_direction != 0 && counter_direction == source_direction;
      const int interval_class = std::abs(pitch - source_pitch) % 12;
      const bool perfect_arrival = interval_class == 0 || interval_class == 7;
      const bool repeats_previous = pitch == previous_counter;
      int score = std::abs(pitch - target);
      if (!consonant) {
        score += 10000;
      }
      // A fourth is consonant between upper voices and a dissonance over the
      // bass. When nothing sounds under the source, the source IS the bass, so
      // the fourth this line would leave above it is a second inversion the ear
      // waits on. Ranked at half the dissonance charge: enough that any
      // consonance in the band wins, not so much that the line leaps out of its
      // own shape when the band offers none.
      if (source_is_lowest && interval_class == 5) {
        score += 5000;
      }
      if (similar && perfect_arrival) {
        score += 4000;
      } else if (similar) {
        score += 200;
      }
      // Ottava battuta: contrary motion into an octave the pair was not on,
      // this line leaping DOWN into it. Charged in the same units as the
      // melodic distance above rather than as a veto -- the alternative is a
      // WIDER leap in the same direction, and a term large enough to always win
      // would buy the arrival at any leap at all. A third's worth of extra
      // distance is the price the line pays; past that the battuta is the
      // better note.
      if (avoid_battuta && formsBattuta(previous_counter, pitch, previous_source, source_pitch)) {
        score += 4;
      }
      if (repeats_previous && source_direction != 0) {
        score += 300;
      }
      if (repeats_previous && repeat_run >= 4) {
        score += 100000;
      }
      if (score < best_score) {
        best_score = score;
        best_pitch = pitch;
      }
    }
    const int pitch = best_pitch >= 0 ? best_pitch : std::clamp(target, band_lo, band_hi);
    repeat_run = pitch == previous_counter ? repeat_run + 1 : 1;
    anchors.push_back({note.start_tick, note.duration, pitch});
    previous_counter = pitch;
    previous_source = source_pitch;
  }

  // Span seam. The repair below returns its argument untouched while there is no
  // emitted predecessor, so with the line started at -1 the one anchor left
  // unjudged is the span's FIRST -- and that is the seam arrival, the onset where
  // this voice and the entry it accompanies both move at once. Seeded from what
  // this voice sounds a sixteenth before the span, the motion the pass could
  // never see becomes the first one it judges.
  int previous_emitted = -1;
  Tick previous_tick = 0;
  constexpr Tick kSeamLookback = kTicksPerBeat / 4;  // one sixteenth.
  if (start >= kSeamLookback) {
    const int seam_pitch = registry.soundingPitchInVoice(voice, start - kSeamLookback);
    if (seam_pitch >= 0) {
      previous_emitted = seam_pitch;
      previous_tick = start - kSeamLookback;
    }
  }
  // Realization-time repair, and the only place in this function that reads the
  // tone actually emitted. Pass 1 scores one anchor per source note, so a beat
  // realized as four sixteenths hands the next anchor a predecessor the scorer
  // never saw -- the pair that ships is not the pair that was judged, and a
  // sixteenth returning from an arpeggio can leap into an octave the anchor it
  // was derived from approached by step.
  //
  // The two faults are ranked, never pooled: a candidate that sounds a true
  // parallel is refused however clean it is of anything else, and the battuta
  // is only allowed to choose among the candidates that already cleared the
  // parallel. When nothing clears both, the parallel-free tone stands and keeps
  // its battuta, because that trade only ever runs one way.
  auto refineAgainstEmitted = [&](int proposed, Tick tick) {
    if (previous_emitted < 0)
      return proposed;
    std::vector<ConcurrentMotion> motions;
    registry.concurrentMotions(previous_tick, tick, voice, /*num_voices=*/3, motions);
    const auto has_parallel = [&](int candidate) {
      return std::any_of(motions.begin(), motions.end(), [&](const ConcurrentMotion& motion) {
        return formsStrictPerfectParallel(previous_emitted, candidate, motion.prev, motion.curr);
      });
    };
    const auto has_battuta = [&](int candidate) {
      return std::any_of(motions.begin(), motions.end(), [&](const ConcurrentMotion& motion) {
        return formsBattuta(previous_emitted, candidate, motion.prev, motion.curr);
      });
    };
    const bool proposed_parallel = has_parallel(proposed);
    if (!proposed_parallel && !has_battuta(proposed))
      return proposed;

    std::vector<int> sounding;
    registry.concurrentThemePitches(tick, voice, sounding);
    const auto consonant_here = [&](int candidate) {
      return std::all_of(sounding.begin(), sounding.end(),
                         [&](int other) { return isConsonantIc(candidate - other); });
    };
    // Nearest first in both sweeps, so a repair moves the line as little as the
    // fault allows.
    for (const bool also_battuta_free : {true, false}) {
      if (!also_battuta_free && !proposed_parallel)
        break;  // the proposed tone is already parallel-free; nothing to fall back to.
      for (int degrees = 1; degrees <= 4; ++degrees) {
        for (int candidate : {detail::scaleUp(proposed, degrees, mode),
                              detail::scaleDown(proposed, degrees, mode)}) {
          if (candidate < band_lo || candidate > band_hi || has_parallel(candidate))
            continue;
          if (also_battuta_free && has_battuta(candidate))
            continue;
          if (consonant_here(candidate))
            return candidate;
        }
      }
    }
    return proposed;
  };

  for (std::size_t index = 0; index < anchors.size(); ++index) {
    const Anchor& anchor = anchors[index];
    const bool has_next = index + 1 < anchors.size();
    const int figure = static_cast<int>((anchor.tick / kTicksPerBeat) % 3);
    if (anchor.duration != kTicksPerBeat || !has_next || figure == 0) {
      const int pitch = refineAgainstEmitted(anchor.pitch, anchor.tick);
      destination.push_back({anchor.tick, anchor.duration, static_cast<std::uint8_t>(pitch)});
      registry.record(anchor.tick, voice, pitch, anchor.duration);
      previous_emitted = pitch;
      previous_tick = anchor.tick;
      continue;
    }

    const Tick sixteenth = kTicksPerBeat / 4;
    std::array<int, 4> pitches{anchor.pitch, anchor.pitch, anchor.pitch, anchor.pitch};
    if (figure == 1) {
      const int run_target = anchors[index + 1].pitch;
      int direction = run_target > anchor.pitch ? 1 : -1;
      int current = anchor.pitch;
      for (int slot = 1; slot < 4; ++slot) {
        current =
            direction > 0 ? detail::scaleUp(current, 1, mode) : detail::scaleDown(current, 1, mode);
        current = std::clamp(current, band_lo, band_hi);
        if ((direction > 0 && current >= run_target) || (direction < 0 && current <= run_target)) {
          direction = -direction;
        }
        pitches[static_cast<std::size_t>(slot)] = current;
      }
    } else {
      const int direction = anchor.pitch + 7 <= band_hi ? 1 : -1;
      const int third = direction > 0 ? detail::scaleUp(anchor.pitch, 2, mode)
                                      : detail::scaleDown(anchor.pitch, 2, mode);
      const int fifth = direction > 0 ? detail::scaleUp(anchor.pitch, 4, mode)
                                      : detail::scaleDown(anchor.pitch, 4, mode);
      pitches = {anchor.pitch, third, fifth, third};
    }
    for (int slot = 0; slot < 4; ++slot) {
      const Tick tick = anchor.tick + static_cast<Tick>(slot) * sixteenth;
      const int pitch = refineAgainstEmitted(
          std::clamp(pitches[static_cast<std::size_t>(slot)], band_lo, band_hi), tick);
      destination.push_back({tick, sixteenth, static_cast<std::uint8_t>(pitch)});
      registry.record(tick, voice, pitch, sixteenth);
      previous_emitted = pitch;
      previous_tick = tick;
    }
  }
}

bool formsStrictPerfectParallel(int line_prev, int cand, int other_prev, int other_curr) {
  if (line_prev < 0 || other_prev < 0 || other_curr < 0) {
    return false;  // need both voices' two onsets to judge motion.
  }
  if (cand >= other_curr)
    return isParallelPerfectMotion(line_prev, cand, other_prev, other_curr);
  return isParallelPerfectMotion(other_prev, other_curr, line_prev, cand);
}

bool formsPerfectParallel(int line_prev, int cand, int other_prev, int other_curr) {
  if (line_prev < 0 || other_prev < 0 || other_curr < 0) {
    return false;  // need both voices' two onsets to judge motion.
  }
  if (cand >= other_curr)
    return isForbiddenPerfectMotion(line_prev, cand, other_prev, other_curr);
  return isForbiddenPerfectMotion(other_prev, other_curr, line_prev, cand);
}

bool formsAntiParallelPerfect(int line_prev, int cand, int other_prev, int other_curr) {
  if (line_prev < 0 || other_prev < 0 || other_curr < 0) {
    return false;  // need both voices' two onsets to judge motion.
  }
  // The predicate is symmetric in its two pairs, so this ordering matches the
  // sibling helpers rather than affecting the answer.
  if (cand >= other_curr)
    return isAntiParallelPerfectMotion(line_prev, cand, other_prev, other_curr);
  return isAntiParallelPerfectMotion(other_prev, other_curr, line_prev, cand);
}

bool formsBattuta(int line_prev, int cand, int other_prev, int other_curr) {
  if (line_prev < 0 || other_prev < 0 || other_curr < 0) {
    return false;  // need both voices' two onsets to judge motion.
  }
  // isBattutaMotion reads the upper line from the arrival, so this ordering only
  // settles an exact unison; it matches the sibling predicates so all three
  // describe the same pair of lines.
  if (cand >= other_curr)
    return isBattutaMotion(line_prev, cand, other_prev, other_curr);
  return isBattutaMotion(other_prev, other_curr, line_prev, cand);
}

int consonantChordTone(const detail::ChordSpec& chord, int voice, int band_lo, int band_hi,
                       int target, const std::vector<int>& theme_pitches, int line_prev,
                       const std::vector<ConcurrentMotion>& motions, detail::Mode mode,
                       bool downbeat, const std::vector<int>& window_pitches,
                       bool parallel_free_over_consonant, bool sustained_bass) {
  const int third = chord.minor ? 3 : 4;
  const int triad_pc[3] = {chord.root_pc % 12, (chord.root_pc + third) % 12,
                           (chord.root_pc + 7) % 12};
  const int seventh_pc = chord.seventh ? detail::chordSeventhPc(chord) : -1;
  auto is_triad = [&](int midi) {
    const int pcl = ((midi % 12) + 12) % 12;
    return pcl == triad_pc[0] || pcl == triad_pc[1] || pcl == triad_pc[2];
  };
  // Chord membership including the seventh. A bar head is the only onset held to
  // the chord, so a seventh chord that could not offer its seventh there would
  // sound as the plain triad it is not.
  auto is_chord_tone = [&](int midi) {
    const int pcl = ((midi % 12) + 12) % 12;
    return is_triad(midi) || pcl == seventh_pc;
  };
  // The seventh is a single dissonant tone with one obligation -- to fall by
  // step -- so two voices holding it would have to resolve in parallel. When an
  // earlier voice already sounds it, this voice takes another chord tone.
  bool seventh_taken = false;
  if (seventh_pc >= 0) {
    for (int theme : theme_pitches)
      seventh_taken = seventh_taken || (((theme % 12) + 12) % 12) == seventh_pc;
    for (const ConcurrentMotion& motion : motions) {
      if (motion.curr >= 0)
        seventh_taken = seventh_taken || (((motion.curr % 12) + 12) % 12) == seventh_pc;
    }
  }
  auto is_anchor_tone = [&](int midi) {
    const int pcl = ((midi % 12) + 12) % 12;
    if (seventh_taken && pcl == seventh_pc)
      return false;
    return downbeat ? is_chord_tone(midi) : (is_chord_tone(midi) || detail::inScale(midi, mode));
  };
  auto is_parallel = [&](int cand) {
    for (const ConcurrentMotion& motion : motions) {
      if (formsPerfectParallel(line_prev, cand, motion.prev, motion.curr)) {
        return true;
      }
    }
    return false;
  };
  auto is_battuta = [&](int cand) {
    for (const ConcurrentMotion& motion : motions) {
      if (formsBattuta(line_prev, cand, motion.prev, motion.curr)) {
        return true;
      }
    }
    return false;
  };
  auto is_anti_parallel = [&](int cand) {
    for (const ConcurrentMotion& motion : motions) {
      if (formsAntiParallelPerfect(line_prev, cand, motion.prev, motion.curr)) {
        return true;
      }
    }
    return false;
  };
  // Voice-ordering window: a lower-indexed voice sounds higher (V0 highest).
  // The anchor must stay at or below every concurrent lower-index voice and at
  // or above every concurrent higher-index voice, so the per-tick order
  // V0 >= V1 >= V2 holds even when a verbatim entry briefly leaves its own band
  // (e.g. a wide stretto follower) -- the band filter alone cannot guarantee it.
  int order_ceiling = band_hi;
  int order_floor = band_lo;
  for (const ConcurrentMotion& motion : motions) {
    if (motion.curr < 0) {
      continue;
    }
    if (motion.voice < voice) {
      order_ceiling = std::min(order_ceiling, motion.curr);  // stay below it.
    } else if (motion.voice > voice) {
      order_floor = std::max(order_floor, motion.curr);  // stay above it.
    }
  }
  // Four nested preferences. Within each tier, fewer mid-window clashes win
  // first (a sustained anchor should not be struck against by a mid-beat
  // dissonance from an already-placed faster line), then "nearest target".
  // The tier key packs (window clashes, distance) so onset consonance still
  // dominates and an empty window reproduces the previous nearest-wins order.
  // Parallel-freedom outranks consonance: when every consonant tone is
  // parallel-tied (earlier-placed verbatim or ornament lines can pin the
  // vertical that way), a mildly dissonant parallel-free tone -- including an
  // oblique repeat of the previous pitch -- beats a parallel fifth/octave,
  // the cardinal prohibition. The one exception: a parallel-free tone that
  // strikes ic 1/6/11 against a sounding theme tone is excluded from the
  // escape tier, so a consonant-but-parallel tone wins over a wrong-note
  // clash with the foreground line.
  int consonant_free = -1;  // consonant, parallel-free AND battuta-free (best).
  int consonant_free_key = 1 << 20;
  // Consonant and parallel-free, but arriving at an octave by a downward leap
  // against a rising voice. Its own tier, immediately below the fully clean one
  // and above everything that tolerates a parallel: a battuta is a real fault
  // worth stepping off when a clean tone exists, but a far milder one than the
  // parallel fifth/octave the lower tiers accept, so it must never be dodged at
  // that price. When no candidate is a battuta this tier stays empty and the
  // whole selection is unchanged.
  int consonant_battuta = -1;
  int consonant_battuta_key = 1 << 20;
  // Consonant, free of every same-direction perfect, but sounding a perfect
  // interval its pair was ALREADY on, reached the other way round. Its own tier
  // BELOW the battuta, which is the one place in this selector where a milder-
  // sounding fault outranks a harsher-sounding one. The reference corpus is
  // what decides it: measured as overshoot scaled by the spread each class
  // occupies in the corpus, the contrary repeat costs several times what the
  // battuta does, because the works themselves write it far more sparingly.
  // Without this tier the contrary repeat is invisible here and rides in the
  // fully clean tier, which is how a beat anchor comes to answer a rising line
  // by leaping down onto the octave it just left.
  //
  // Every onset ranks the contrary repeat, but what an onset will PAY to avoid
  // one depends on where it falls, so the tier sits on a different rung of the
  // chain below.
  //
  // On the downbeat it sits BELOW the parallel-free escape, and that placement
  // is what makes it reach anything. A bar head is the only onset this selector
  // holds to the chord: three pitch classes, and a sounding theme tone routinely
  // leaves exactly one of them consonant, so the contrary repeat is not one
  // candidate among several -- it is the whole consonant set, and a tier above
  // the escape would return it every time. Below the escape the anchor takes the
  // diatonic tone that brushes the theme instead. That is a real trade and it is
  // paid in the dissonance the corpus counterpoint model has no component for,
  // so it was measured on its own terms: over the whole sweep the share of beat
  // onsets carrying a sounding second, seventh or tritone moves by less than a
  // fifth of a percentage point, against a contrary column that falls by well
  // over half.
  //
  // Off the downbeat it sits ABOVE the escape instead. The whole scale is
  // admissible there, so demoting the contrary repeat below a clean tone costs
  // nothing when one exists and the tier is pure gain; but letting it fall past
  // the escape would send an anchor off the chord for a fault that only ever
  // arises when the chord is already spent -- and an anchor is the register the
  // bars after it start from, so that displacement surfaces later as bar heads
  // with no playable chord tone left.
  int consonant_anti = -1;
  int consonant_anti_key = 1 << 20;
  int free_any = -1;  // parallel-free, mildest clash profile (second).
  int free_any_key = 1 << 28;
  int consonant_any = -1;  // consonant, parallel allowed (third).
  int consonant_any_key = 1 << 20;
  int fallback = -1;  // least dissonant (last resort).
  int fallback_score = 1 << 20;
  // Only apply the ordering window when it leaves a non-empty range; a degenerate
  // window (a concurrent voice already outside this band's order) falls back to
  // the band so a candidate always exists.
  const bool order_window_usable = order_floor <= order_ceiling;
  // Lowest pitch any already-placed voice sounds at this onset. A candidate
  // below it is the bass of the vertical, which is the only position where the
  // perfect fourth counts as a dissonance; above it the fourth is a consonance
  // and nothing here changes.
  constexpr int kAbovePitchRange = 128;
  int lowest_placed = kAbovePitchRange;
  for (int theme : theme_pitches) {
    lowest_placed = std::min(lowest_placed, theme);
  }
  for (const ConcurrentMotion& motion : motions) {
    if (motion.curr >= 0) {
      lowest_placed = std::min(lowest_placed, motion.curr);
    }
  }
  for (int pitch = band_lo; pitch <= band_hi; ++pitch) {
    if (!is_anchor_tone(pitch)) {
      continue;
    }
    if (order_window_usable && (pitch < order_floor || pitch > order_ceiling)) {
      continue;  // would cross a concurrent voice; never admissible.
    }
    int clashes = 0;
    int weighted_clashes = 0;     // sharp ic 1/6/11 clashes count double.
    bool sharp_vs_theme = false;  // strikes ic 1/6/11 against a theme tone.
    // True when this candidate would sound below every already-placed voice, so
    // the intervals it forms are bass intervals rather than upper-voice ones.
    const bool is_bass = pitch < lowest_placed;
    // Whether the candidate leaves a fourth above itself while it is the bass --
    // an unresolved second inversion. Not a clash: a clash moves the candidate
    // between preference tiers, and the tiers here rank parallel-freedom, which
    // must not be traded for an inversion. It rides in the ordering key instead,
    // so a bass free of the fourth beats one that carries it whenever both are
    // otherwise equally admissible, and the fourth is still written where the
    // chord and the voice order leave nothing else -- which is how a passing or
    // cadential six-four survives.
    bool bass_fourth = false;
    auto add_clash = [&](int sounding, bool is_theme) {
      // Detected before the tests below, both of which would wave it through:
      // the interval class is consonant, and the pair that spells it -- the
      // chord's fifth under its root -- is two tones of the same chord.
      if (is_bass && isConsonantIc(pitch - sounding) &&
          !rule_helpers::isConsonantAboveBass(static_cast<std::uint8_t>(sounding),
                                              static_cast<std::uint8_t>(pitch))) {
        bass_fourth = true;
        return;
      }
      if (isConsonantIc(pitch - sounding)) {
        return;
      }
      // Two tones of the SAME chord state the harmony; they do not clash within
      // it. On a plain triad every such pair is already consonant, so this is
      // inert there and reachable only under a declared seventh, where the pair
      // it admits is the tritone between third and seventh -- the interval that
      // defines a dominant. Judging that pair by interval class alone is what
      // made the selector unable to place the sonority it was asked for.
      if (is_chord_tone(pitch) && is_chord_tone(sounding)) {
        return;
      }
      const int ic = std::abs(pitch - sounding) % 12;
      ++clashes;
      const bool sharp = (ic == 1 || ic == 6 || ic == 11);
      weighted_clashes += sharp ? 2 : 1;
      if (sharp && is_theme) {
        sharp_vs_theme = true;
      }
    };
    for (int theme : theme_pitches) {
      add_clash(theme, /*is_theme=*/true);
    }
    // Also stay consonant against every earlier voice sounding at this onset
    // (not just the theme): an off-downbeat diatonic anchor would otherwise be
    // free to clash with another figuration voice on the beat grid.
    for (const ConcurrentMotion& motion : motions) {
      if (motion.curr >= 0) {
        add_clash(motion.curr, /*is_theme=*/false);
      }
    }
    // The second inversion the onset test above refuses can just as well arrive
    // a beat later: this tone is still sounding when the voices above it attack
    // again, so a bass ranked against its onset alone is ranked against a
    // fraction of what it actually supports. What carries over unchanged is that
    // the fourth is a dissonance in one position only -- lying under SOME voice
    // is not being the bass, and above a lower voice the fourth is a consonance
    // like any other -- so the reading needs the candidate to be lowest at the
    // onset and to stay lowest for as long as it holds.
    bool bass_through_window = sustained_bass && is_bass;
    for (int sounding : window_pitches) {
      bass_through_window = bass_through_window && pitch < sounding;
    }
    int window_clashes = 0;
    for (int sounding : window_pitches) {
      if (bass_through_window && isConsonantIc(pitch - sounding) &&
          !rule_helpers::isConsonantAboveBass(static_cast<std::uint8_t>(sounding),
                                              static_cast<std::uint8_t>(pitch))) {
        bass_fourth = true;
      }
      if (!isConsonantIc(pitch - sounding) && !(is_chord_tone(pitch) && is_chord_tone(sounding))) {
        ++window_clashes;
      }
    }
    const int dist = std::abs(pitch - target);
    // Distance stays below 128 (one MIDI band), so a 128 stride keeps the
    // (window clashes, distance) order strictly lexicographic. A candidate
    // farther than a fifth from the running pitch is demoted below every
    // near candidate regardless of its clash count: an anchor lurching a
    // sixth or more is audibly worse than tolerating a mid-window passing
    // clash near the line (the bass otherwise flees to a clash-free tone a
    // tenth away whenever a faster upper line brushes the near candidates).
    // The second inversion sits between the two: worse than a passing clash
    // inside the window, better than that lurch -- the fourth is one interval
    // the ear resolves against the harmony, while a sixth-wide jump rewrites
    // the line.
    // Admissibility -- consonance, parallels, voice order -- is unaffected.
    const int far_stride = (dist > 7) ? (1 << 13) : 0;
    const int key = far_stride + (bass_fourth ? (1 << 12) : 0) + window_clashes * 128 + dist;
    if (clashes == 0) {
      if (key < consonant_any_key) {
        consonant_any_key = key;
        consonant_any = pitch;
      }
      if (!is_parallel(pitch)) {
        if (is_battuta(pitch)) {
          if (key < consonant_battuta_key) {
            consonant_battuta_key = key;
            consonant_battuta = pitch;
          }
        } else if (is_anti_parallel(pitch)) {
          if (key < consonant_anti_key) {
            consonant_anti_key = key;
            consonant_anti = pitch;
          }
        } else if (key < consonant_free_key) {
          consonant_free_key = key;
          consonant_free = pitch;
        }
      }
    } else if (!is_parallel(pitch) && !sharp_vs_theme) {
      // Weighted clashes dominate the packed (window clashes, distance) key so
      // the mildest clash profile wins, nearest-target breaking ties. A
      // candidate that strikes ic 1/6/11 against a sounding THEME tone never
      // enters this tier: the theme is the foreground line, so a quarter-long
      // m2/M7/tritone against it is more audible than the perfect-interval
      // parallel this tier exists to dodge -- such a candidate may only
      // survive as the last-resort fallback.
      const int free_key = weighted_clashes * (1 << 15) + key;
      if (free_key < free_any_key) {
        free_any_key = free_key;
        free_any = pitch;
      }
    }
    // The clash stride must dominate the packed key even with the far-anchor
    // demotion and the second-inversion rung folded in (key < 1<<14), so the
    // last resort still minimizes dissonance first and distance second.
    //
    // Between tones that tie on dissonance it takes the one free of a perfect
    // parallel. Every tier above this one refuses a parallel outright, so an
    // onset that reaches here has none of them available -- and nothing further
    // down the chain re-examines the choice. A last resort blind to the cardinal
    // prohibition is therefore the one path by which an exhausted onset ships a
    // parallel fifth or octave it had an equally dissonant alternative to.
    const int score = clashes * (1 << 17) + (is_parallel(pitch) ? (1 << 15) : 0) + key;
    if (score < fallback_score) {
      fallback_score = score;
      fallback = pitch;
    }
  }
  if (consonant_free >= 0) {
    return consonant_free;
  }
  if (consonant_battuta >= 0) {
    return consonant_battuta;
  }
  if (!downbeat && consonant_anti >= 0) {
    return consonant_anti;
  }
  // Tier order between "parallel-free but clashing" and "consonant but
  // parallel" is a per-form contract: fugue-family figuration prefers the
  // parallel-free escape (a parallel fifth/octave is the cardinal
  // prohibition), while ground-variation forms hold every beat onset
  // mutually consonant over the held ground and keep consonance first.
  if (parallel_free_over_consonant && free_any >= 0) {
    return free_any;
  }
  if (consonant_anti >= 0) {
    return consonant_anti;
  }
  if (consonant_any >= 0) {
    return consonant_any;
  }
  if (free_any >= 0) {
    return free_any;
  }
  return fallback >= 0 ? fallback : std::clamp(target, band_lo, band_hi);
}

StrettoOverlapProfile strettoOverlapProfile(const std::array<std::uint8_t, 16>& leader_pat,
                                            int leader_total,
                                            const std::array<std::uint8_t, 16>& follower_pat,
                                            int follower_total,
                                            const std::array<Tick, 16>& leader_rhythm,
                                            const std::array<Tick, 16>& follower_rhythm,
                                            int delay_bars, int window_bars) {
  constexpr Tick kSlotTick = kTicksPerBeat / 4;  // sixteenth grid.
  constexpr Tick kSustainLimit = kTicksPerBeat;  // a quarter note.
  constexpr int kStrettoPatternNotes = 16;
  const int total_slots = static_cast<int>(barToTick(window_bars) / kSlotTick);
  std::vector<int> leader(static_cast<std::size_t>(total_slots), -1);
  std::vector<int> follower(static_cast<std::size_t>(total_slots), -1);
  auto lay = [&](std::vector<int>& line, const std::array<std::uint8_t, 16>& pat, int total,
                 const std::array<Tick, 16>& rhythm, Tick start) {
    Tick cursor = start;
    for (int note = 0; note < kStrettoPatternNotes; ++note) {
      const Tick dur = rhythm[static_cast<std::size_t>(note)];
      for (Tick t = cursor; t < cursor + dur; t += kSlotTick) {
        const int slot = static_cast<int>(t / kSlotTick);
        if (slot >= total_slots) {
          return;  // the follower is truncated at the leader's end.
        }
        line[static_cast<std::size_t>(slot)] =
            static_cast<int>(pat[static_cast<std::size_t>(note)]) + total;
      }
      cursor += dur;
    }
  };
  lay(leader, leader_pat, leader_total, leader_rhythm, 0);
  lay(follower, follower_pat, follower_total, follower_rhythm, barToTick(delay_bars));
  StrettoOverlapProfile profile;
  const int sustain_limit = static_cast<int>(kSustainLimit / kSlotTick);
  int run = 0;
  int prev_leader = -1;
  int prev_follower = -1;
  for (int slot = 0; slot < total_slots; ++slot) {
    const int lead = leader[static_cast<std::size_t>(slot)];
    const int foll = follower[static_cast<std::size_t>(slot)];
    bool sharp = false;
    if (lead >= 0 && foll >= 0) {
      profile.overlap_slots += 1;
      const int ic = std::abs(lead - foll) % 12;
      sharp = (ic == 1 || ic == 6 || ic == 11);
      if (sharp || ic == 2 || ic == 10) {
        profile.broad_sharp_slots += 1;
      }
      if (formsStrictPerfectParallel(prev_leader, lead, prev_follower, foll)) {
        profile.parallel_perfects += 1;
      }
    }
    prev_leader = lead;
    prev_follower = foll;
    run = sharp ? run + 1 : 0;
    if (run >= sustain_limit) {
      profile.sustains_sharp = true;
    }
  }
  return profile;
}

void appendCadentialLanding(std::vector<MaterialNote>& line, Tick penult_bar_start,
                            Tick ticks_per_bar, int prefinal, int final_pitch, detail::Mode mode,
                            int band_lo, const detail::ChordSpec* downbeat_chord,
                            bool prefer_descending, bool lift_to_context,
                            std::uint8_t ts_numerator) {
  const Tick eighth = duration::kEighthNote;
  const Tick prefinal_tick =
      ts_numerator == 3 ? ticks_per_bar / static_cast<Tick>(3) : ticks_per_bar / 2;
  const int run_len = static_cast<int>(prefinal_tick / eighth);  // 4 in 4/4, 2 in 3/4.

  if (lift_to_context) {
    // The formula's register is the caller's design value, but the line it
    // interrupts may be running an octave higher (a climax block compressing
    // against the keyboard ceiling). When the last figuration pitch before
    // the landing sits more than a major sixth above the approach run's entry
    // tone, re-octave the whole formula upward: pitch classes are preserved,
    // so consonance against the other voices is unchanged, and the close
    // connects from the line instead of falling off a registral cliff. The
    // lift never carries the trill site past d''' (MIDI 86, the keyboard
    // ceiling; +2 covers the trill's upper neighbour).
    int last_pitch = -1;
    Tick last_tick = 0;
    for (const MaterialNote& note : line) {
      if (note.start_tick < penult_bar_start && (last_pitch < 0 || note.start_tick >= last_tick)) {
        last_tick = note.start_tick;
        last_pitch = note.pitch;
      }
    }
    if (last_pitch >= 0) {
      const int run_entry = detail::scaleDown(prefinal, run_len, mode);
      const int gap_plain = std::abs(last_pitch - run_entry);
      const int gap_lifted = std::abs(last_pitch - (run_entry + 12));
      if (gap_plain > 9 && gap_lifted < gap_plain && prefinal + 14 <= 86) {
        prefinal += 12;
        final_pitch += 12;
      }
    }
  }

  // Drop everything from the penultimate bar on; the landing replaces it.
  line.erase(
      std::remove_if(line.begin(), line.end(),
                     [&](const MaterialNote& note) { return note.start_tick >= penult_bar_start; }),
      line.end());

  // Approach run INTO the pre-final tone. Ascending by preference: in minor an
  // ascent into the raised leading tone takes the melodic-minor sixth degree
  // (A natural), avoiding the augmented second the natural-minor walk would
  // make. When the ascent would dip below the band floor (a narrow band whose
  // tonic hugs the floor), the run flips to a descent from above instead.
  std::vector<int> run(static_cast<std::size_t>(run_len));
  if (mode == detail::Mode::Minor && ((prefinal % 12) == 11)) {
    // Melodic-minor ascent into the leading tone: ... Eb F G A(natural) -> B.
    static constexpr int kMelodicOffsets[4] = {-8, -6, -4, -2};
    for (int idx = 0; idx < run_len; ++idx)
      run[static_cast<std::size_t>(idx)] = prefinal + kMelodicOffsets[4 - run_len + idx];
  } else {
    for (int idx = 0; idx < run_len; ++idx)
      run[static_cast<std::size_t>(idx)] = detail::scaleDown(prefinal, run_len - idx, mode);
  }
  if (prefer_descending || run.front() < band_lo) {
    // Descending run from above: e.g. F E D C -> B.
    for (int idx = 0; idx < run_len; ++idx)
      run[static_cast<std::size_t>(idx)] = detail::scaleUp(prefinal, run_len - idx, mode);
  }
  if (downbeat_chord != nullptr) {
    // Snap the downbeat eighth to the nearest chord tone (the figuration
    // downbeat rule reads the bar-head onset).
    const int third = downbeat_chord->minor ? 3 : 4;
    const int triad_pc[3] = {downbeat_chord->root_pc % 12, (downbeat_chord->root_pc + third) % 12,
                             (downbeat_chord->root_pc + 7) % 12};
    auto is_triad = [&](int midi) {
      const int pcl = ((midi % 12) + 12) % 12;
      return pcl == triad_pc[0] || pcl == triad_pc[1] || pcl == triad_pc[2];
    };
    int snapped = run.front();
    for (int delta = 0; delta <= 6; ++delta) {
      if (is_triad(run.front() + delta)) {
        snapped = run.front() + delta;
        break;
      }
      if (is_triad(run.front() - delta)) {
        snapped = run.front() - delta;
        break;
      }
    }
    run.front() = snapped;
  }

  for (int idx = 0; idx < run_len; ++idx) {
    MaterialNote note;
    note.start_tick = penult_bar_start + static_cast<Tick>(idx) * eighth;
    note.duration = eighth;
    note.pitch = static_cast<std::uint8_t>(std::clamp(run[static_cast<std::size_t>(idx)], 0, 127));
    line.push_back(note);
  }

  // Held pre-final tone from the cadence's structural beat (the trill site).
  MaterialNote held;
  held.start_tick = penult_bar_start + prefinal_tick;
  held.duration = ticks_per_bar - prefinal_tick;
  held.pitch = static_cast<std::uint8_t>(std::clamp(prefinal, 0, 127));
  line.push_back(held);

  // Full-bar final tone (the held resolution).
  MaterialNote last;
  last.start_tick = penult_bar_start + ticks_per_bar;
  last.duration = ticks_per_bar;
  last.pitch = static_cast<std::uint8_t>(std::clamp(final_pitch, 0, 127));
  line.push_back(last);
}

void appendCadentialSixFourLanding(std::vector<MaterialNote>& line, Tick penult_bar_start,
                                   Tick ticks_per_bar, int tonic, int leading_tone) {
  line.erase(
      std::remove_if(line.begin(), line.end(),
                     [&](const MaterialNote& note) { return note.start_tick >= penult_bar_start; }),
      line.end());
  const Tick resolution = kTicksPerBeat;
  MaterialNote six_four;
  six_four.start_tick = penult_bar_start;
  six_four.duration = resolution;
  six_four.pitch = static_cast<std::uint8_t>(std::clamp(tonic, 0, 127));
  line.push_back(six_four);
  MaterialNote dominant;
  dominant.start_tick = penult_bar_start + resolution;
  dominant.duration = ticks_per_bar - resolution;
  dominant.pitch = static_cast<std::uint8_t>(std::clamp(leading_tone, 0, 127));
  line.push_back(dominant);
  MaterialNote final_tonic;
  final_tonic.start_tick = penult_bar_start + ticks_per_bar;
  final_tonic.duration = ticks_per_bar;
  final_tonic.pitch = static_cast<std::uint8_t>(std::clamp(tonic, 0, 127));
  line.push_back(final_tonic);
}

void appendCompactCadentialLanding(std::vector<MaterialNote>& line, Tick final_bar_start,
                                   Tick ticks_per_bar, int prefinal, int final_pitch,
                                   std::uint8_t ts_numerator) {
  line.erase(
      std::remove_if(line.begin(), line.end(),
                     [&](const MaterialNote& note) { return note.start_tick >= final_bar_start; }),
      line.end());
  // Four-four cadences turn on beat three, retaining the historic half-bar
  // split. In triple metre, the dominant/trill occupies beat one only and
  // resolves on the real second beat, not the 3/2-beat arithmetic midpoint.
  const Tick resolution_tick =
      ts_numerator == 3 ? ticks_per_bar / static_cast<Tick>(3) : ticks_per_bar / 2;
  MaterialNote held;
  held.start_tick = final_bar_start;
  held.duration = resolution_tick;
  held.pitch = static_cast<std::uint8_t>(std::clamp(prefinal, 0, 127));
  line.push_back(held);
  MaterialNote last;
  last.start_tick = final_bar_start + resolution_tick;
  last.duration = ticks_per_bar - resolution_tick;
  last.pitch = static_cast<std::uint8_t>(std::clamp(final_pitch, 0, 127));
  line.push_back(last);
}

}  // namespace bach::composer
