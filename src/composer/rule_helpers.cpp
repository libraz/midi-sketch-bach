#include "composer/rule_helpers.h"

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <iterator>

#include "composer/chord_voicing.h"
#include "core/pitch_utils.h"

namespace bach::composer::rule_helpers {

std::array<std::uint8_t, 3> triadPitchClasses(const ChordEvent& chord) {
  std::uint8_t third_offset = 4;  // Major
  std::uint8_t fifth_offset = 7;
  switch (chord.quality) {
    case ChordQuality::Major:
    case ChordQuality::Major7:
    case ChordQuality::Dominant7:
      third_offset = 4;
      fifth_offset = 7;
      break;
    case ChordQuality::Minor:
    case ChordQuality::Minor7:
      third_offset = 3;
      fifth_offset = 7;
      break;
    case ChordQuality::Diminished:
    case ChordQuality::HalfDiminished7:
    case ChordQuality::Diminished7:
      third_offset = 3;
      fifth_offset = 6;
      break;
    case ChordQuality::Augmented:
      third_offset = 4;
      fifth_offset = 8;
      break;
  }
  return {
      static_cast<std::uint8_t>(chord.root_pc % 12),
      static_cast<std::uint8_t>((chord.root_pc + third_offset) % 12),
      static_cast<std::uint8_t>((chord.root_pc + fifth_offset) % 12),
  };
}

const ChordEvent& activeChord(const HarmonicPlan& plan, Tick at) {
  // Keep the reference-returning helper total: malformed/partial plans are
  // rejected by their callers' validation, but must not dereference front().
  // The neutral tonic fallback lets those callers report a normal validation
  // failure rather than crashing across the C/WASM boundary.
  static const ChordEvent kFallback{};
  if (plan.chords.empty())
    return kFallback;
  const auto next =
      std::upper_bound(plan.chords.begin(), plan.chords.end(), at,
                       [](Tick tick, const ChordEvent& chord) { return tick < chord.start_tick; });
  return next == plan.chords.begin() ? plan.chords.front() : *std::prev(next);
}

bool isStrongBeat(Tick tick, Tick ticks_per_bar) {
  if (ticks_per_bar == 0)
    ticks_per_bar = kTicksPerBar;  // defensive: never modulo by zero.
  return (tick % ticks_per_bar) == 0;
}

MetricalStrength metricalStrengthAt(const HarmonicPlan& plan, Tick tick) {
  const Tick ticks_per_bar = plan.ticksPerBar();
  if (ticks_per_bar == 0 || plan.ts_denominator == 0) {
    return MetricalStrength::Weak;
  }
  const Tick beat_ticks = kTicksPerBeat * 4 / plan.ts_denominator;
  if (beat_ticks == 0) {
    return MetricalStrength::Weak;
  }
  const Tick position = tick % ticks_per_bar;
  if (position == 0) {
    return MetricalStrength::Strong;
  }

  const bool compound = plan.ts_numerator > 3 && plan.ts_numerator % 3 == 0;
  if (compound) {
    const Tick pulse_ticks = beat_ticks * 3;
    return pulse_ticks > 0 && position % pulse_ticks == 0 ? MetricalStrength::Medium
                                                          : MetricalStrength::Weak;
  }
  if (position % beat_ticks != 0) {
    return MetricalStrength::Weak;
  }
  const Tick beat = position / beat_ticks;
  if (plan.ts_numerator == 4 && beat == 2) {
    return MetricalStrength::Medium;
  }
  if (plan.ts_numerator == 3 && plan.meter_profile == MeterProfile::SarabandeTriple && beat == 1) {
    return MetricalStrength::Medium;
  }
  if (plan.ts_numerator > 4 && plan.ts_numerator % 2 == 0 && beat == plan.ts_numerator / 2) {
    return MetricalStrength::Medium;
  }
  return MetricalStrength::Weak;
}

bool isStructuralAccent(const HarmonicPlan& plan, Tick tick) {
  return metricalStrengthAt(plan, tick) != MetricalStrength::Weak;
}

std::uint8_t pitchClass(std::uint8_t pitch) {
  return static_cast<std::uint8_t>(pitch % 12);
}

bool isLeadingTone(std::uint8_t pitch, const HarmonicPlan& plan) {
  return pitchClass(pitch) == static_cast<std::uint8_t>((plan.tonic_pc + 11) % 12);
}

bool isLeadingToneResolution(std::uint8_t leading, int resolution, const HarmonicPlan& plan) {
  if (resolution < 0 || resolution > 127)
    return false;
  const std::uint8_t tonic_pc = static_cast<std::uint8_t>(plan.tonic_pc % 12);
  if (static_cast<std::uint8_t>(resolution % 12) != tonic_pc)
    return false;
  return resolution > static_cast<int>(leading) &&
         std::abs(resolution - static_cast<int>(leading)) <= 2;
}

const ChordEvent* chordAt(const HarmonicPlan& plan, Tick tick) {
  const auto next = std::upper_bound(
      plan.chords.begin(), plan.chords.end(), tick,
      [](Tick value, const ChordEvent& chord) { return value < chord.start_tick; });
  return next == plan.chords.begin() ? nullptr : &*std::prev(next);
}

namespace {

bool chordContainsPc(const ChordEvent& chord, std::uint8_t pc) {
  int third = 4;
  int fifth = 7;
  switch (chord.quality) {
    case ChordQuality::Minor:
    case ChordQuality::Minor7:
      third = 3;
      break;
    case ChordQuality::Diminished:
    case ChordQuality::HalfDiminished7:
    case ChordQuality::Diminished7:
      third = 3;
      fifth = 6;
      break;
    case ChordQuality::Augmented:
      fifth = 8;
      break;
    default:
      break;
  }
  const std::uint8_t root = static_cast<std::uint8_t>(chord.root_pc % 12);
  if (pc == root || pc == static_cast<std::uint8_t>((root + third) % 12) ||
      pc == static_cast<std::uint8_t>((root + fifth) % 12)) {
    return true;
  }
  int seventh = -1;
  switch (chord.quality) {
    case ChordQuality::Dominant7:
    case ChordQuality::Minor7:
    case ChordQuality::HalfDiminished7:
      seventh = 10;
      break;
    case ChordQuality::Major7:
      seventh = 11;
      break;
    case ChordQuality::Diminished7:
      seventh = 9;
      break;
    default:
      break;
  }
  return seventh >= 0 && pc == static_cast<std::uint8_t>((root + seventh) % 12);
}

// Seven diatonic pitch classes for a key. Minor uses the harmonic form, whose
// raised 7th is the degree the dominant needs.
std::array<std::uint8_t, 7> scalePcs(std::uint8_t tonic_pc, bool is_minor) {
  const int tonic = tonic_pc % 12;
  const std::array<int, 7> steps = is_minor ? std::array<int, 7>{0, 2, 3, 5, 7, 8, 11}
                                            : std::array<int, 7>{0, 2, 4, 5, 7, 9, 11};
  std::array<std::uint8_t, 7> pcs{};
  for (std::size_t idx = 0; idx < pcs.size(); ++idx) {
    pcs[idx] = static_cast<std::uint8_t>((tonic + steps[idx]) % 12);
  }
  return pcs;
}

// The scale degree `pc` represents, returned as that degree's own pitch class,
// or -1 when `pc` is a semitone away from no degree at all. A diatonic pitch
// class represents itself; a chromatic one is read as the raised form of the
// degree a semitone below in preference to the lowered form of the degree a
// semitone above, which is what makes F# in C major the raised 4th rather than
// a lowered 5th.
int inflectedDegreePc(std::uint8_t pc, const std::array<std::uint8_t, 7>& scale) {
  const auto in_scale = [&scale](int candidate) {
    return std::find(scale.begin(), scale.end(), static_cast<std::uint8_t>(candidate)) !=
           scale.end();
  };
  const int here = pc % 12;
  if (in_scale(here))
    return here;
  const int below = (here + 11) % 12;
  if (in_scale(below))
    return below;
  const int above = (here + 1) % 12;
  if (in_scale(above))
    return above;
  return -1;
}

}  // namespace

KeyContext keyAt(const HarmonicPlan& plan, Tick tick) {
  KeyContext key;
  key.tonic_pc = static_cast<std::uint8_t>(plan.tonic_pc % 12);
  key.is_minor = plan.is_minor;
  // Latest boundary at or before `tick`, later list entries winning ties, so an
  // out-of-order list resolves the same as a sorted one.
  Tick latest = 0;
  bool found = false;
  for (const auto& modulation : plan.modulations) {
    if (modulation.tick > tick || (found && modulation.tick < latest))
      continue;
    latest = modulation.tick;
    found = true;
    key.tonic_pc = static_cast<std::uint8_t>(modulation.to_tonic_pc % 12);
    key.is_minor = modulation.to_is_minor;
  }
  return key;
}

bool isDiatonicPc(std::uint8_t pc, std::uint8_t tonic_pc, bool is_minor) {
  const auto scale = scalePcs(tonic_pc, is_minor);
  return std::find(scale.begin(), scale.end(), static_cast<std::uint8_t>(pc % 12)) != scale.end();
}

TonalContext tonalContextAt(const HarmonicPlan& plan, Tick tick) {
  TonalContext context;
  const KeyContext key = keyAt(plan, tick);
  context.tonic_pc = key.tonic_pc;
  context.is_minor = key.is_minor;
  context.leading_tone_pc = static_cast<std::uint8_t>((context.tonic_pc + 11) % 12);
  context.resolution_pc = context.tonic_pc;
  const ChordEvent* chord = chordAt(plan, tick);
  if (chord == nullptr)
    return context;
  if (chord->has_secondary_of) {
    context.is_secondary_dominant = true;
    context.has_active_leading_tone = true;
    context.leading_tone_pc = static_cast<std::uint8_t>((chord->root_pc + 4) % 12);
    context.resolution_pc = static_cast<std::uint8_t>((chord->root_pc + 5) % 12);
    return context;
  }
  // A chord carries the leading tone when it is the local key's major dominant
  // or its leading-tone triad, read against the same key as leading_tone_pc.
  const int relative = (static_cast<int>(chord->root_pc) - context.tonic_pc + 12) % 12;
  const bool major_quality =
      chord->quality == ChordQuality::Major || chord->quality == ChordQuality::Dominant7;
  const bool diminished_quality = chord->quality == ChordQuality::Diminished ||
                                  chord->quality == ChordQuality::HalfDiminished7 ||
                                  chord->quality == ChordQuality::Diminished7;
  context.has_active_leading_tone =
      (relative == 7 && major_quality) || (relative == 11 && diminished_quality);
  return context;
}

bool isContextualScalePitch(std::uint8_t pitch, const HarmonicPlan& plan, Tick tick,
                            int melodic_motion) {
  const TonalContext context = tonalContextAt(plan, tick);
  const std::uint8_t pc = static_cast<std::uint8_t>(pitch % 12);
  const ChordEvent* chord = chordAt(plan, tick);
  if (chord != nullptr && (chord->has_secondary_of || chord->is_borrowed) &&
      chordContainsPc(*chord, pc)) {
    return true;
  }
  const int relative = (static_cast<int>(pc) - context.tonic_pc + 12) % 12;
  if (!context.is_minor) {
    return relative == 0 || relative == 2 || relative == 4 || relative == 5 || relative == 7 ||
           relative == 9 || relative == 11;
  }
  if (relative == 0 || relative == 2 || relative == 3 || relative == 5 || relative == 7)
    return true;
  if (relative == 8 || relative == 10)
    return melodic_motion <= 0 && !context.has_active_leading_tone;
  if (relative == 9)
    return melodic_motion > 0;
  if (relative == 11)
    return melodic_motion > 0 || context.has_active_leading_tone;
  return false;
}

detail::PcMask chordPcsAt(const HarmonicPlan& plan, Tick tick) {
  const ChordEvent* chord = chordAt(plan, tick);
  if (chord == nullptr)
    return 0;
  detail::PcMask mask = 0;
  for (int pc = 0; pc < 12; ++pc) {
    if (chordContainsPc(*chord, static_cast<std::uint8_t>(pc)))
      mask = static_cast<detail::PcMask>(mask | detail::pcBit(pc));
  }
  // A minor-quality chord whose perfect fifth the local minor does not hold is
  // the key's diminished ii or vii written as a plain minor triad.
  const KeyContext key = keyAt(plan, tick);
  const int fifth = chord->root_pc + 7;
  if (key.is_minor &&
      (chord->quality == ChordQuality::Minor || chord->quality == ChordQuality::Minor7) &&
      !detail::inSpelledScale(fifth, key.tonic_pc, detail::Mode::Minor, 0)) {
    mask = static_cast<detail::PcMask>((mask & ~detail::pcBit(fifth)) | detail::pcBit(fifth - 1));
  }
  return mask;
}

bool isContextualLeadingTone(std::uint8_t pitch, const HarmonicPlan& plan, Tick tick) {
  const TonalContext context = tonalContextAt(plan, tick);
  return context.has_active_leading_tone && pitch % 12 == context.leading_tone_pc;
}

bool isContextualLeadingToneResolution(std::uint8_t leading, int resolution,
                                       const HarmonicPlan& plan, Tick tick) {
  if (resolution < 0 || resolution > 127)
    return false;
  const TonalContext context = tonalContextAt(plan, tick);
  return context.has_active_leading_tone && leading % 12 == context.leading_tone_pc &&
         resolution % 12 == context.resolution_pc && resolution > leading &&
         resolution - static_cast<int>(leading) <= 2;
}

bool isContextualAugmentedMelodicInterval(std::uint8_t from, std::uint8_t to,
                                          const HarmonicPlan& plan, Tick from_tick, Tick to_tick) {
  const int signed_motion = static_cast<int>(to) - static_cast<int>(from);
  const int semis = std::abs(signed_motion) % 12;
  if (semis == 6)
    return true;
  if (semis != 3)
    return false;
  const int direction = (signed_motion > 0) - (signed_motion < 0);
  return !isContextualScalePitch(from, plan, from_tick, direction) ||
         !isContextualScalePitch(to, plan, to_tick, direction);
}

bool isPerfectInterval(int semitones) {
  const int abs_semi = std::abs(semitones) % 12;
  return abs_semi == 0 || abs_semi == 7;
}

bool isConsonantInterval(int semis) {
  const int pc = std::abs(semis) % 12;
  return pc == 0 || pc == 3 || pc == 4 || pc == 5 || pc == 7 || pc == 8 || pc == 9;
}

bool isConsonantAboveBass(std::uint8_t pitch, std::uint8_t bass_pitch) {
  if (pitch < bass_pitch)
    return false;
  const int pc = (static_cast<int>(pitch) - static_cast<int>(bass_pitch)) % 12;
  return pc == 0 || pc == 3 || pc == 4 || pc == 7 || pc == 8 || pc == 9;
}

bool isBassSensitiveConsonance(std::uint8_t pitch_a, std::uint8_t pitch_b,
                               std::uint8_t bass_pitch) {
  const int interval = static_cast<int>(pitch_a) - static_cast<int>(pitch_b);
  const int pc = std::abs(interval) % 12;
  if (pc != 5)
    return isConsonantInterval(interval);
  if (std::min(pitch_a, pitch_b) == bass_pitch)
    return false;
  return isConsonantAboveBass(pitch_a, bass_pitch) && isConsonantAboveBass(pitch_b, bass_pitch);
}

bool isCrossRelationPc(std::uint8_t a, std::uint8_t b, std::uint8_t tonic_pc, bool is_minor) {
  const std::uint8_t pc_a = static_cast<std::uint8_t>(a % 12);
  const std::uint8_t pc_b = static_cast<std::uint8_t>(b % 12);
  if (pc_a == pc_b)
    return false;
  const auto scale = scalePcs(tonic_pc, is_minor);
  const int degree_a = inflectedDegreePc(pc_a, scale);
  const int degree_b = inflectedDegreePc(pc_b, scale);
  if (degree_a < 0 || degree_b < 0)
    return false;
  return degree_a == degree_b;
}

namespace {

// Seven diatonic pitch classes for the plan's key. Mirrors the local
// scalePcs() previously duplicated in validator.cpp.
std::array<std::uint8_t, 7> scalePcs(const HarmonicPlan& plan) {
  return scalePcs(static_cast<std::uint8_t>(plan.tonic_pc % 12), plan.is_minor);
}

int scaleIndex(std::uint8_t pc, const HarmonicPlan& plan) {
  const auto pcs = scalePcs(plan);
  for (int i = 0; i < static_cast<int>(pcs.size()); ++i) {
    if (pcs[static_cast<std::size_t>(i)] == pc)
      return i;
  }
  return -1;
}

}  // namespace

bool isAugmentedMelodicInterval(std::uint8_t from, std::uint8_t to, const HarmonicPlan& plan) {
  const int semis = std::abs(static_cast<int>(to) - static_cast<int>(from)) % 12;
  if (semis == 6)
    return true;  // augmented fourth spelling is indistinguishable from tritone in MIDI.
  if (semis != 3)
    return false;
  const int a = scaleIndex(pitchClass(from), plan);
  const int b = scaleIndex(pitchClass(to), plan);
  if (a < 0 || b < 0)
    return true;
  const int degree_distance = std::abs(a - b);
  return degree_distance == 1 || degree_distance == 6;
}

bool isDiminishedMelodicInterval(std::uint8_t from, std::uint8_t to) {
  const int semis = std::abs(static_cast<int>(to) - static_cast<int>(from)) % 12;
  return semis == 6 || semis == 11;
}

bool isForbiddenMelodicLeap(std::uint8_t from, std::uint8_t to, const HarmonicPlan& plan,
                            Tick from_tick, Tick to_tick) {
  const int semis = std::abs(static_cast<int>(to) - static_cast<int>(from)) % 12;
  // Do not reinterpret natural-minor descending b7/b6 through a fixed
  // harmonic-minor collection. The contextual predicate preserves that line
  // while still rejecting an actual augmented second.
  return semis == 6 || isContextualAugmentedMelodicInterval(from, to, plan, from_tick, to_tick) ||
         isDiminishedMelodicInterval(from, to);
}

bool createsVoiceCrossing(const VoiceOnsetIndex& placed, VoiceId candidate_voice,
                          std::uint8_t candidate_pitch, Tick cur_tick) {
  bool crossing = false;
  for (const VoiceId other : placed.activeVoices()) {
    if (other == candidate_voice)
      continue;
    placed.forEachSoundingAt(other, cur_tick, [&](std::size_t index) {
      const std::uint8_t pitch = placed.note(index).pitch;
      crossing = (candidate_voice < other && candidate_pitch < pitch) ||
                 (candidate_voice > other && candidate_pitch > pitch);
      return !crossing;
    });
    if (crossing)
      return true;
  }
  return false;
}

bool createsVerticalDissonance(const VoiceOnsetIndex& placed, VoiceId candidate_voice,
                               std::uint8_t candidate_pitch, Tick cur_tick) {
  std::uint8_t bass_pitch = candidate_pitch;
  for (const VoiceId other : placed.activeVoices()) {
    if (other == candidate_voice)
      continue;
    placed.forEachSoundingAt(other, cur_tick, [&](std::size_t index) {
      bass_pitch = std::min(bass_pitch, placed.note(index).pitch);
      return true;
    });
  }
  for (const VoiceId other : placed.activeVoices()) {
    if (other == candidate_voice)
      continue;
    bool dissonant = false;
    placed.forEachSoundingAt(other, cur_tick, [&](std::size_t index) {
      dissonant = !isBassSensitiveConsonance(candidate_pitch, placed.note(index).pitch, bass_pitch);
      return !dissonant;
    });
    if (dissonant)
      return true;
  }
  return false;
}

bool createsParallelPerfect(const VoiceOnsetIndex& placed, VoiceId candidate_voice,
                            std::uint8_t candidate_pitch, Tick cur_tick, std::uint8_t prev_pitch,
                            Tick prev_tick) {
  if (prev_pitch == 0)
    return false;
  for (const VoiceId ov : placed.activeVoices()) {
    if (ov == candidate_voice)
      continue;
    const std::uint8_t op_now = placed.pitchAt(ov, cur_tick);
    const std::uint8_t op_prev = placed.pitchAt(ov, prev_tick);
    if (op_now == 0 || op_prev == 0)
      continue;
    const bool parallel =
        candidate_voice < ov
            ? isParallelPerfectMotion(prev_pitch, candidate_pitch, op_prev, op_now)
            : isParallelPerfectMotion(op_prev, op_now, prev_pitch, candidate_pitch);
    if (parallel) {
      return true;
    }
  }
  return false;
}

bool createsParallelPerfectAcrossOnset(const VoiceOnsetIndex& placed, VoiceId candidate_voice,
                                       std::uint8_t candidate_pitch, Tick cur_tick,
                                       std::uint8_t prev_pitch, Tick prev_tick) {
  if (prev_pitch == 0)
    return false;
  for (const VoiceId ov : placed.activeVoices()) {
    if (ov == candidate_voice)
      continue;
    // Closing the faster-voice blind spot: the candidate voice's own previous
    // onset (prev_tick) may be older than the other voice's most recent onset.
    // The validator samples the union of onsets, so it compares the candidate's
    // move against the other voice's pitch at the LAST union tick before
    // cur_tick. Find that tick: the other voice's latest onset strictly inside
    // (prev_tick, cur_tick). When present, the other voice has already moved by
    // cur_tick relative to that union tick, and a parallel can form even though
    // it did not move between prev_tick and cur_tick.
    Tick mid_tick = prev_tick;
    const bool have_mid = placed.lastOnsetBetween(ov, prev_tick, cur_tick, &mid_tick);
    if (!have_mid)
      continue;  // no intermediate onset; createsParallelPerfect covers this.
    const std::uint8_t op_now = placed.pitchAt(ov, cur_tick);
    const std::uint8_t op_mid = placed.pitchAt(ov, mid_tick);
    if (op_now == 0 || op_mid == 0)
      continue;
    const bool parallel =
        candidate_voice < ov ? isParallelPerfectMotion(prev_pitch, candidate_pitch, op_mid, op_now)
                             : isParallelPerfectMotion(op_mid, op_now, prev_pitch, candidate_pitch);
    if (parallel) {
      return true;
    }
  }
  return false;
}

bool createsBattuta(const VoiceOnsetIndex& placed, VoiceId candidate_voice,
                    std::uint8_t candidate_pitch, Tick cur_tick, std::uint8_t prev_pitch,
                    Tick prev_tick) {
  if (prev_pitch == 0)
    return false;
  for (const VoiceId ov : placed.activeVoices()) {
    if (ov == candidate_voice)
      continue;
    const std::uint8_t op_now = placed.pitchAt(ov, cur_tick);
    const std::uint8_t op_prev = placed.pitchAt(ov, prev_tick);
    if (op_now == 0 || op_prev == 0)
      continue;
    const bool battuta = candidate_voice < ov
                             ? isBattutaMotion(prev_pitch, candidate_pitch, op_prev, op_now)
                             : isBattutaMotion(op_prev, op_now, prev_pitch, candidate_pitch);
    if (battuta) {
      return true;
    }
  }
  return false;
}

bool createsAntiParallelPerfect(const VoiceOnsetIndex& placed, VoiceId candidate_voice,
                                std::uint8_t candidate_pitch, Tick cur_tick,
                                std::uint8_t prev_pitch, Tick prev_tick) {
  if (prev_pitch == 0)
    return false;
  for (const VoiceId ov : placed.activeVoices()) {
    if (ov == candidate_voice)
      continue;
    const std::uint8_t op_now = placed.pitchAt(ov, cur_tick);
    const std::uint8_t op_prev = placed.pitchAt(ov, prev_tick);
    if (op_now == 0 || op_prev == 0)
      continue;
    // The predicate is symmetric in its two pairs, so the ordering here is for
    // consistency with the sibling helpers rather than for correctness.
    const bool anti =
        candidate_voice < ov
            ? isAntiParallelPerfectMotion(prev_pitch, candidate_pitch, op_prev, op_now)
            : isAntiParallelPerfectMotion(op_prev, op_now, prev_pitch, candidate_pitch);
    if (anti) {
      return true;
    }
  }
  return false;
}

bool createsParallelOctave(const VoiceOnsetIndex& placed, VoiceId candidate_voice,
                           std::uint8_t candidate_pitch, Tick cur_tick, std::uint8_t prev_pitch,
                           Tick prev_tick) {
  if (prev_pitch == 0)
    return false;
  for (const VoiceId ov : placed.activeVoices()) {
    if (ov == candidate_voice)
      continue;
    const std::uint8_t op_now = placed.pitchAt(ov, cur_tick);
    const std::uint8_t op_prev = placed.pitchAt(ov, prev_tick);
    if (op_now == 0 || op_prev == 0)
      continue;
    const PerfectMotionKind kind =
        candidate_voice < ov ? classifyPerfectMotion(prev_pitch, candidate_pitch, op_prev, op_now)
                             : classifyPerfectMotion(op_prev, op_now, prev_pitch, candidate_pitch);
    if (kind == PerfectMotionKind::ParallelOctave) {
      return true;
    }
  }
  return false;
}

bool createsHiddenParallelPerfect(const VoiceOnsetIndex& placed, VoiceId candidate_voice,
                                  std::uint8_t candidate_pitch, Tick cur_tick,
                                  std::uint8_t prev_pitch, Tick prev_tick) {
  if (prev_pitch == 0)
    return false;
  for (const VoiceId ov : placed.activeVoices()) {
    if (ov == candidate_voice)
      continue;
    const std::uint8_t op_now = placed.pitchAt(ov, cur_tick);
    const std::uint8_t op_prev = placed.pitchAt(ov, prev_tick);
    if (op_now == 0 || op_prev == 0)
      continue;
    const PerfectMotionKind kind =
        candidate_voice < ov ? classifyPerfectMotion(prev_pitch, candidate_pitch, op_prev, op_now)
                             : classifyPerfectMotion(op_prev, op_now, prev_pitch, candidate_pitch);
    if (kind == PerfectMotionKind::HiddenFifth || kind == PerfectMotionKind::HiddenOctave) {
      return true;
    }
  }
  return false;
}

bool createsHiddenParallelPerfectAcrossOnset(const VoiceOnsetIndex& placed, VoiceId candidate_voice,
                                             std::uint8_t candidate_pitch, Tick cur_tick,
                                             std::uint8_t prev_pitch, Tick prev_tick) {
  if (prev_pitch == 0)
    return false;
  for (const VoiceId ov : placed.activeVoices()) {
    if (ov == candidate_voice)
      continue;
    // Same faster-voice blind spot as createsParallelPerfectAcrossOnset: the
    // validator compares against the union tick immediately before cur_tick,
    // which may be an intermediate onset of the faster other voice.
    Tick mid_tick = prev_tick;
    const bool have_mid = placed.lastOnsetBetween(ov, prev_tick, cur_tick, &mid_tick);
    if (!have_mid)
      continue;
    const std::uint8_t op_now = placed.pitchAt(ov, cur_tick);
    const std::uint8_t op_mid = placed.pitchAt(ov, mid_tick);
    if (op_now == 0 || op_mid == 0)
      continue;
    const PerfectMotionKind kind =
        candidate_voice < ov ? classifyPerfectMotion(prev_pitch, candidate_pitch, op_mid, op_now)
                             : classifyPerfectMotion(op_mid, op_now, prev_pitch, candidate_pitch);
    if (kind == PerfectMotionKind::HiddenFifth || kind == PerfectMotionKind::HiddenOctave) {
      return true;
    }
  }
  return false;
}

bool createsCrossRelation(const VoiceOnsetIndex& placed, VoiceId candidate_voice,
                          std::uint8_t candidate_pitch, Tick cur_tick, const HarmonicPlan& plan) {
  const std::uint8_t pc = pitchClass(candidate_pitch);
  const KeyContext candidate_key = keyAt(plan, cur_tick);
  // Judge the pair in the key of the later onset, so an alteration arriving
  // after a modulation is read in the key it arrives in.
  const auto clashes = [&](const NoteEvent& note) {
    const KeyContext key =
        note.start_tick > cur_tick ? keyAt(plan, note.start_tick) : candidate_key;
    return isCrossRelationPc(pc, pitchClass(note.pitch), key.tonic_pc, key.is_minor);
  };
  const Tick window_begin = cur_tick > kTicksPerBeat ? cur_tick - kTicksPerBeat : 0;
  for (const VoiceId other : placed.activeVoices()) {
    if (other == candidate_voice)
      continue;
    bool found = false;
    placed.forEachSoundingAt(other, cur_tick, [&](std::size_t index) {
      found = clashes(placed.note(index));
      return !found;
    });
    if (found)
      return true;
    const auto& onsets = placed.voiceNotes(other);
    for (auto it = placed.firstStartingFrom(other, window_begin);
         it != onsets.end() && placed.note(*it).start_tick <= cur_tick + kTicksPerBeat; ++it) {
      if (clashes(placed.note(*it)))
        return true;
    }
  }
  return false;
}

const CadenceCell* cadenceCellAt(const Material& material, Tick tick) {
  for (const auto& cell : material.cadence_cells) {
    if (cell.approach_tick == tick || cell.cadence_tick == tick)
      return &cell;
  }
  return nullptr;
}

void applyP7Bits(RuleIdMask& rules, const ChordEvent& chord, std::uint8_t pc, bool is_chord_tone) {
  if (!chord.has_degree)
    return;
  rules |= ruleBitMask(RuleBit::DoublingChecked);
  if (is_chord_tone)
    rules |= ruleBitMask(RuleBit::ChordToneRoman);
  if (pc == bassPitchClassFor(chord))
    rules |= ruleBitMask(RuleBit::InversionLabel);
}

void applyP8Bits(RuleIdMask& rules, const HarmonicPlan& plan, const ChordEvent& chord,
                 std::uint8_t pc, bool is_chord_tone) {
  // Needs the plan's switch, which applyP7Bits does not receive.
  if (chord.has_degree && plan.enforce_chordal_upper_voice_spacing)
    rules |= ruleBitMask(RuleBit::SpacingChecked);
  const KeyContext key = keyAt(plan, chord.start_tick);
  const bool away_from_home = key.tonic_pc != plan.tonic_pc % 12 || key.is_minor != plan.is_minor;
  if (away_from_home && isDiatonicPc(pc, key.tonic_pc, key.is_minor))
    rules |= ruleBitMask(RuleBit::ModulationCommitted);
  const auto at =
      std::lower_bound(plan.chords.begin(), plan.chords.end(), chord.start_tick,
                       [](const ChordEvent& event, Tick tick) { return event.start_tick < tick; });
  if (at != plan.chords.begin() && chord.has_degree) {
    const ChordEvent& previous = *std::prev(at);
    if (previous.has_secondary_of && previous.secondary_of == chord.degree)
      rules |= ruleBitMask(RuleBit::SecondaryDominantResolved);
  }
  if (chord.is_picardy) {
    const std::uint8_t major_third_pc = static_cast<std::uint8_t>((chord.root_pc + 4) % 12);
    if (pc == major_third_pc) {
      rules |= ruleBitMask(RuleBit::PicardyThird);
    }
  }
  if (chord.is_borrowed && is_chord_tone) {
    rules |= ruleBitMask(RuleBit::ModalMixture);
  }
}

}  // namespace bach::composer::rule_helpers
