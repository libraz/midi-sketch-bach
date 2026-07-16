#include "composer/rule_helpers.h"

#include <algorithm>
#include <cstdlib>

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

namespace {

const ChordEvent* chordAt(const HarmonicPlan& plan, Tick tick) {
  const ChordEvent* active = nullptr;
  for (const auto& chord : plan.chords) {
    if (chord.start_tick > tick)
      break;
    active = &chord;
  }
  return active;
}

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

}  // namespace

TonalContext tonalContextAt(const HarmonicPlan& plan, Tick tick) {
  TonalContext context;
  context.tonic_pc = static_cast<std::uint8_t>(plan.tonic_pc % 12);
  context.is_minor = plan.is_minor;
  for (const auto& modulation : plan.modulations) {
    if (modulation.tick > tick)
      break;
    context.tonic_pc = static_cast<std::uint8_t>(modulation.to_tonic_pc % 12);
    context.is_minor = modulation.to_is_minor;
  }
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
  const bool dominant = chord->function == HarmonicFunction::D ||
                        chord->degree == RomanNumeral::V ||
                        chord->quality == ChordQuality::Dominant7;
  context.has_active_leading_tone = dominant;
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

bool isCrossRelationPc(std::uint8_t a, std::uint8_t b) {
  const std::uint8_t lo = std::min(a, b);
  const std::uint8_t hi = std::max(a, b);
  return (lo == 0 && hi == 1) || (lo == 2 && hi == 3) || (lo == 5 && hi == 6) ||
         (lo == 7 && hi == 8) || (lo == 9 && hi == 10);
}

namespace {

// Seven diatonic pitch classes for the plan's key. Mirrors the local
// scalePcs() previously duplicated in validator.cpp.
std::array<std::uint8_t, 7> scalePcs(const HarmonicPlan& plan) {
  if (plan.is_minor) {
    return {
        static_cast<std::uint8_t>(plan.tonic_pc % 12),
        static_cast<std::uint8_t>((plan.tonic_pc + 2) % 12),
        static_cast<std::uint8_t>((plan.tonic_pc + 3) % 12),
        static_cast<std::uint8_t>((plan.tonic_pc + 5) % 12),
        static_cast<std::uint8_t>((plan.tonic_pc + 7) % 12),
        static_cast<std::uint8_t>((plan.tonic_pc + 8) % 12),
        static_cast<std::uint8_t>((plan.tonic_pc + 11) % 12),
    };
  }
  return {
      static_cast<std::uint8_t>(plan.tonic_pc % 12),
      static_cast<std::uint8_t>((plan.tonic_pc + 2) % 12),
      static_cast<std::uint8_t>((plan.tonic_pc + 4) % 12),
      static_cast<std::uint8_t>((plan.tonic_pc + 5) % 12),
      static_cast<std::uint8_t>((plan.tonic_pc + 7) % 12),
      static_cast<std::uint8_t>((plan.tonic_pc + 9) % 12),
      static_cast<std::uint8_t>((plan.tonic_pc + 11) % 12),
  };
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

bool isForbiddenMelodicLeap(std::uint8_t from, std::uint8_t to, const HarmonicPlan& plan) {
  const int semis = std::abs(static_cast<int>(to) - static_cast<int>(from)) % 12;
  return semis == 6 || isAugmentedMelodicInterval(from, to, plan) ||
         isDiminishedMelodicInterval(from, to);
}

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

std::uint8_t sameVoiceStartingAt(const std::vector<NoteEvent>& placed, VoiceId voice, Tick tick) {
  for (const auto& note : placed) {
    if (note.voice != voice)
      continue;
    if (note.start_tick == tick)
      return note.pitch;
  }
  return 0;
}

bool createsVoiceCrossing(const std::vector<NoteEvent>& placed, VoiceId candidate_voice,
                          std::uint8_t candidate_pitch, Tick cur_tick) {
  for (const auto& note : placed) {
    if (note.voice == candidate_voice)
      continue;
    if (note.start_tick > cur_tick)
      continue;
    const bool sounding = note.start_tick <= cur_tick && cur_tick < note.start_tick + note.duration;
    if (!sounding)
      continue;
    if (candidate_voice < note.voice && candidate_pitch < note.pitch) {
      return true;
    }
    if (candidate_voice > note.voice && candidate_pitch > note.pitch) {
      return true;
    }
  }
  return false;
}

bool createsVerticalDissonance(const std::vector<NoteEvent>& placed, VoiceId candidate_voice,
                               std::uint8_t candidate_pitch, Tick cur_tick) {
  std::uint8_t bass_pitch = candidate_pitch;
  for (const auto& note : placed) {
    if (note.voice == candidate_voice || note.start_tick > cur_tick)
      continue;
    if (note.start_tick <= cur_tick && cur_tick < note.start_tick + note.duration)
      bass_pitch = std::min(bass_pitch, note.pitch);
  }
  for (const auto& note : placed) {
    if (note.voice == candidate_voice)
      continue;
    if (note.start_tick > cur_tick)
      continue;
    const bool sounding = note.start_tick <= cur_tick && cur_tick < note.start_tick + note.duration;
    if (!sounding)
      continue;
    if (!isBassSensitiveConsonance(candidate_pitch, note.pitch, bass_pitch)) {
      return true;
    }
  }
  return false;
}

namespace {

std::vector<VoiceId> collectOtherVoices(const std::vector<NoteEvent>& placed,
                                        VoiceId candidate_voice) {
  std::vector<VoiceId> other_voices;
  for (const auto& n : placed) {
    if (n.voice == candidate_voice)
      continue;
    if (std::find(other_voices.begin(), other_voices.end(), n.voice) == other_voices.end()) {
      other_voices.push_back(n.voice);
    }
  }
  return other_voices;
}

}  // namespace

bool createsParallelPerfect(const std::vector<NoteEvent>& placed, VoiceId candidate_voice,
                            std::uint8_t candidate_pitch, Tick cur_tick, std::uint8_t prev_pitch,
                            Tick prev_tick) {
  if (prev_pitch == 0)
    return false;
  for (VoiceId ov : collectOtherVoices(placed, candidate_voice)) {
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

bool createsParallelPerfectAcrossOnset(const std::vector<NoteEvent>& placed,
                                       VoiceId candidate_voice, std::uint8_t candidate_pitch,
                                       Tick cur_tick, std::uint8_t prev_pitch, Tick prev_tick) {
  if (prev_pitch == 0)
    return false;
  for (VoiceId ov : collectOtherVoices(placed, candidate_voice)) {
    // Closing the faster-voice blind spot: the candidate voice's own previous
    // onset (prev_tick) may be older than the other voice's most recent onset.
    // The validator samples the union of onsets, so it compares the candidate's
    // move against the other voice's pitch at the LAST union tick before
    // cur_tick. Find that tick: the other voice's latest onset strictly inside
    // (prev_tick, cur_tick). When present, the other voice has already moved by
    // cur_tick relative to that union tick, and a parallel can form even though
    // it did not move between prev_tick and cur_tick.
    Tick mid_tick = prev_tick;
    bool have_mid = false;
    for (const auto& note : placed) {
      if (note.voice != ov)
        continue;
      if (note.start_tick > prev_tick && note.start_tick < cur_tick) {
        if (!have_mid || note.start_tick > mid_tick) {
          mid_tick = note.start_tick;
          have_mid = true;
        }
      }
    }
    if (!have_mid)
      continue;  // no intermediate onset; createsParallelPerfect covers this.
    const std::uint8_t op_now = voicePitchAt(placed, ov, cur_tick);
    const std::uint8_t op_mid = voicePitchAt(placed, ov, mid_tick);
    if (op_now == 0 || op_mid == 0)
      continue;
    // The candidate sustains prev_pitch across mid_tick (it has no onset there),
    // so its motion into cur_tick is prev_pitch -> candidate_pitch. The other
    // voice moves op_mid -> op_now. Both must move to form a parallel.
    const bool both_moved = (candidate_pitch != prev_pitch) && (op_now != op_mid);
    if (!both_moved)
      continue;
    int interval_now;
    int interval_mid;
    if (candidate_voice < ov) {
      interval_now = static_cast<int>(candidate_pitch) - static_cast<int>(op_now);
      interval_mid = static_cast<int>(prev_pitch) - static_cast<int>(op_mid);
    } else {
      interval_now = static_cast<int>(op_now) - static_cast<int>(candidate_pitch);
      interval_mid = static_cast<int>(op_mid) - static_cast<int>(prev_pitch);
    }
    if (isPerfectInterval(interval_now) && isPerfectInterval(interval_mid) &&
        interval_now == interval_mid && interval_now != 0) {
      return true;
    }
  }
  return false;
}

bool createsParallelOctave(const std::vector<NoteEvent>& placed, VoiceId candidate_voice,
                           std::uint8_t candidate_pitch, Tick cur_tick, std::uint8_t prev_pitch,
                           Tick prev_tick) {
  if (prev_pitch == 0)
    return false;
  for (VoiceId ov : collectOtherVoices(placed, candidate_voice)) {
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
    // Parallel OCTAVE only: both intervals reduce mod 12 to 0 (unison or
    // octave or double octave...). Class equality is enough — the
    // Validator's invertible_at_octave rule fails octave-class to
    // octave-class motion even when the literal sizes differ (a double
    // octave contracting to an octave is still a parallel octave), so the
    // filter must match or candidates slip through to a validation fail.
    // Parallel fifths are handled by createsParallelPerfect and are
    // intentionally excluded here so cadence cells can bypass them.
    const bool is_oct_now = std::abs(interval_now) % 12 == 0 && interval_now != 0;
    const bool is_oct_prev = std::abs(interval_prev) % 12 == 0 && interval_prev != 0;
    if (is_oct_now && is_oct_prev) {
      return true;
    }
  }
  return false;
}

bool createsHiddenParallelPerfect(const std::vector<NoteEvent>& placed, VoiceId candidate_voice,
                                  std::uint8_t candidate_pitch, Tick cur_tick,
                                  std::uint8_t prev_pitch, Tick prev_tick) {
  if (prev_pitch == 0)
    return false;
  for (VoiceId ov : collectOtherVoices(placed, candidate_voice)) {
    const std::uint8_t op_now = voicePitchAt(placed, ov, cur_tick);
    const std::uint8_t op_prev = voicePitchAt(placed, ov, prev_tick);
    if (op_now == 0 || op_prev == 0)
      continue;
    const int this_motion = static_cast<int>(candidate_pitch) - static_cast<int>(prev_pitch);
    const int other_motion = static_cast<int>(op_now) - static_cast<int>(op_prev);
    if (this_motion == 0 || other_motion == 0)
      continue;
    if ((this_motion > 0) != (other_motion > 0))
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
    if (isPerfectInterval(interval_now) && !isPerfectInterval(interval_prev)) {
      return true;
    }
  }
  return false;
}

bool createsHiddenParallelPerfectAcrossOnset(const std::vector<NoteEvent>& placed,
                                             VoiceId candidate_voice, std::uint8_t candidate_pitch,
                                             Tick cur_tick, std::uint8_t prev_pitch,
                                             Tick prev_tick) {
  if (prev_pitch == 0)
    return false;
  for (VoiceId ov : collectOtherVoices(placed, candidate_voice)) {
    // Same faster-voice blind spot as createsParallelPerfectAcrossOnset: the
    // validator compares against the union tick immediately before cur_tick,
    // which may be an intermediate onset of the faster other voice.
    Tick mid_tick = prev_tick;
    bool have_mid = false;
    for (const auto& note : placed) {
      if (note.voice != ov)
        continue;
      if (note.start_tick > prev_tick && note.start_tick < cur_tick) {
        if (!have_mid || note.start_tick > mid_tick) {
          mid_tick = note.start_tick;
          have_mid = true;
        }
      }
    }
    if (!have_mid)
      continue;
    const std::uint8_t op_now = voicePitchAt(placed, ov, cur_tick);
    const std::uint8_t op_mid = voicePitchAt(placed, ov, mid_tick);
    if (op_now == 0 || op_mid == 0)
      continue;
    const int this_motion = static_cast<int>(candidate_pitch) - static_cast<int>(prev_pitch);
    const int other_motion = static_cast<int>(op_now) - static_cast<int>(op_mid);
    if (this_motion == 0 || other_motion == 0)
      continue;
    if ((this_motion > 0) != (other_motion > 0))
      continue;
    int interval_now;
    int interval_mid;
    if (candidate_voice < ov) {
      interval_now = static_cast<int>(candidate_pitch) - static_cast<int>(op_now);
      interval_mid = static_cast<int>(prev_pitch) - static_cast<int>(op_mid);
    } else {
      interval_now = static_cast<int>(op_now) - static_cast<int>(candidate_pitch);
      interval_mid = static_cast<int>(op_mid) - static_cast<int>(prev_pitch);
    }
    if (isPerfectInterval(interval_now) && !isPerfectInterval(interval_mid)) {
      return true;
    }
  }
  return false;
}

bool createsCrossRelation(const std::vector<NoteEvent>& placed, VoiceId candidate_voice,
                          std::uint8_t candidate_pitch, Tick cur_tick) {
  const std::uint8_t pc = pitchClass(candidate_pitch);
  for (const auto& note : placed) {
    if (note.voice == candidate_voice)
      continue;
    const bool simultaneous =
        note.start_tick <= cur_tick && cur_tick < note.start_tick + note.duration;
    const bool adjacent = std::abs(static_cast<int>(note.start_tick) -
                                   static_cast<int>(cur_tick)) <= static_cast<int>(kTicksPerBeat);
    if (!simultaneous && !adjacent)
      continue;
    if (isCrossRelationPc(pc, pitchClass(note.pitch))) {
      return true;
    }
  }
  return false;
}

}  // namespace bach::composer::rule_helpers
