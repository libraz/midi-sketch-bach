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

bool isStrongBeat(Tick tick) {
  return (tick % kTicksPerBar) == 0;
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

bool isPerfectInterval(int semitones) {
  const int abs_semi = std::abs(semitones) % 12;
  return abs_semi == 0 || abs_semi == 7;
}

bool isConsonantInterval(int semis) {
  const int pc = std::abs(semis) % 12;
  return pc == 0 || pc == 3 || pc == 4 || pc == 5 || pc == 7 || pc == 8 || pc == 9;
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
    // octave or double octave...) and stay identical. Parallel fifths are
    // handled by createsParallelPerfect and are intentionally excluded
    // here so cadence cells can bypass them.
    const bool is_oct_now = std::abs(interval_now) % 12 == 0 && interval_now != 0;
    const bool is_oct_prev = std::abs(interval_prev) % 12 == 0 && interval_prev != 0;
    if (is_oct_now && is_oct_prev && interval_now == interval_prev) {
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
