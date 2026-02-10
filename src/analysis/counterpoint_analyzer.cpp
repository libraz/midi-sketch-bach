// Counterpoint quality analysis implementation.

#include "analysis/counterpoint_analyzer.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "core/basic_types.h"
#include "core/pitch_utils.h"

namespace bach {
namespace {

/// @brief Extract notes for a single voice, sorted by start_tick.
std::vector<NoteEvent> voiceNotes(const std::vector<NoteEvent>& notes, VoiceId voice) {
  std::vector<NoteEvent> result;
  for (const auto& note : notes) {
    if (note.voice == voice) result.push_back(note);
  }
  std::sort(result.begin(), result.end(),
            [](const NoteEvent& lhs, const NoteEvent& rhs) {
              return lhs.start_tick < rhs.start_tick;
            });
  return result;
}

/// @brief Pitch sounding at tick, or -1 if silent.
int soundingPitch(const std::vector<NoteEvent>& sorted_voice, Tick tick) {
  int result = -1;
  for (const auto& note : sorted_voice) {
    if (note.start_tick <= tick && tick < note.start_tick + note.duration)
      result = static_cast<int>(note.pitch);
    if (note.start_tick > tick) break;
  }
  return result;
}

Tick totalEndTick(const std::vector<NoteEvent>& notes) {
  Tick max_end = 0;
  for (const auto& note : notes) {
    Tick end = note.start_tick + note.duration;
    if (end > max_end) max_end = end;
  }
  return max_end;
}

bool isPerfectConsonance(int semitones) {
  int simple = ((semitones % 12) + 12) % 12;
  return simple == 0 || simple == 7;
}

bool isDissonant(int semitones) {
  int simple = ((semitones % 12) + 12) % 12;
  return simple == 1 || simple == 2 || simple == 5 ||
         simple == 6 || simple == 10 || simple == 11;
}

float clamp01(float val) { return val < 0.0f ? 0.0f : (val > 1.0f ? 1.0f : val); }

using VoiceCache = std::vector<std::vector<NoteEvent>>;

VoiceCache buildVoiceCache(const std::vector<NoteEvent>& notes, uint8_t num_voices) {
  VoiceCache voices(num_voices);
  for (uint8_t idx = 0; idx < num_voices; ++idx) voices[idx] = voiceNotes(notes, idx);
  return voices;
}

/// @brief Emit a FailIssue for a detected violation.
FailIssue makeIssue(FailSeverity sev, Tick tick, VoiceId va, VoiceId vb,
                    const std::string& rule, const std::string& desc) {
  FailIssue issue;
  issue.kind = FailKind::MusicalFail;
  issue.severity = sev;
  issue.tick = tick;
  issue.bar = static_cast<uint8_t>(tickToBar(tick));
  issue.beat = beatInBar(tick);
  issue.voice_a = va;
  issue.voice_b = vb;
  issue.rule = rule;
  issue.description = desc;
  return issue;
}

}  // namespace

uint32_t countParallelPerfect(const std::vector<NoteEvent>& notes, uint8_t num_voices) {
  if (num_voices < 2) return 0;
  auto voices = buildVoiceCache(notes, num_voices);
  Tick end = totalEndTick(notes);
  if (end < kTicksPerBeat) return 0;

  uint32_t count = 0;
  for (uint8_t va_idx = 0; va_idx < num_voices; ++va_idx) {
    for (uint8_t vb_idx = va_idx + 1; vb_idx < num_voices; ++vb_idx) {
      int prev_a = -1, prev_b = -1;
      for (Tick beat = 0; beat < end; beat += kTicksPerBeat) {
        int cur_a = soundingPitch(voices[va_idx], beat);
        int cur_b = soundingPitch(voices[vb_idx], beat);
        if (cur_a < 0 || cur_b < 0 || prev_a < 0 || prev_b < 0) {
          prev_a = cur_a; prev_b = cur_b; continue;
        }
        int pi = std::abs(prev_a - prev_b), ci = std::abs(cur_a - cur_b);
        if (isPerfectConsonance(pi) && isPerfectConsonance(ci)) {
          int ma = cur_a - prev_a, mb = cur_b - prev_b;
          bool same_dir = (ma > 0 && mb > 0) || (ma < 0 && mb < 0);
          int ps = ((pi % 12) + 12) % 12, cs = ((ci % 12) + 12) % 12;
          if (same_dir && ps == cs) ++count;
        }
        prev_a = cur_a; prev_b = cur_b;
      }
    }
  }
  return count;
}

uint32_t countHiddenPerfect(const std::vector<NoteEvent>& notes, uint8_t num_voices) {
  if (num_voices < 2) return 0;
  auto voices = buildVoiceCache(notes, num_voices);
  Tick end = totalEndTick(notes);
  if (end < kTicksPerBeat) return 0;

  uint32_t count = 0;
  for (uint8_t va_idx = 0; va_idx < num_voices; ++va_idx) {
    for (uint8_t vb_idx = va_idx + 1; vb_idx < num_voices; ++vb_idx) {
      int prev_a = -1, prev_b = -1;
      for (Tick beat = 0; beat < end; beat += kTicksPerBeat) {
        int cur_a = soundingPitch(voices[va_idx], beat);
        int cur_b = soundingPitch(voices[vb_idx], beat);
        if (cur_a < 0 || cur_b < 0 || prev_a < 0 || prev_b < 0) {
          prev_a = cur_a; prev_b = cur_b; continue;
        }
        int ci = std::abs(cur_a - cur_b), pi = std::abs(prev_a - prev_b);
        int ps = ((pi % 12) + 12) % 12, cs = ((ci % 12) + 12) % 12;
        if (isPerfectConsonance(ci) && ps != cs) {
          int ma = cur_a - prev_a, mb = cur_b - prev_b;
          bool same_dir = (ma > 0 && mb > 0) || (ma < 0 && mb < 0);
          if (same_dir && std::abs(ma) > 2) ++count;
        }
        prev_a = cur_a; prev_b = cur_b;
      }
    }
  }
  return count;
}

uint32_t countVoiceCrossings(const std::vector<NoteEvent>& notes, uint8_t num_voices) {
  if (num_voices < 2) return 0;
  auto voices = buildVoiceCache(notes, num_voices);
  Tick end = totalEndTick(notes);
  if (end == 0) return 0;

  uint32_t count = 0;
  for (Tick beat = 0; beat < end; beat += kTicksPerBeat) {
    for (uint8_t va = 0; va < num_voices; ++va) {
      for (uint8_t vb = va + 1; vb < num_voices; ++vb) {
        int hi = soundingPitch(voices[va], beat);
        int lo = soundingPitch(voices[vb], beat);
        if (hi >= 0 && lo >= 0 && hi < lo) ++count;
      }
    }
  }
  return count;
}

float dissonanceResolutionRate(const std::vector<NoteEvent>& notes, uint8_t num_voices) {
  if (num_voices < 2) return 1.0f;
  auto voices = buildVoiceCache(notes, num_voices);
  Tick end = totalEndTick(notes);
  if (end < kTicksPerBeat) return 1.0f;

  uint32_t total = 0, resolved = 0;
  for (Tick beat = 0; beat + kTicksPerBeat < end; beat += kTicksPerBeat) {
    Tick next = beat + kTicksPerBeat;
    for (uint8_t va = 0; va < num_voices; ++va) {
      for (uint8_t vb = va + 1; vb < num_voices; ++vb) {
        int pa = soundingPitch(voices[va], beat), pb = soundingPitch(voices[vb], beat);
        if (pa < 0 || pb < 0) continue;
        if (!isDissonant(std::abs(pa - pb))) continue;
        ++total;
        int na = soundingPitch(voices[va], next), nb = soundingPitch(voices[vb], next);
        if (na < 0 || nb < 0) continue;
        bool consonant = !isDissonant(std::abs(na - nb));
        bool step_a = std::abs(na - pa) >= 1 && std::abs(na - pa) <= 2;
        bool step_b = std::abs(nb - pb) >= 1 && std::abs(nb - pb) <= 2;
        if (consonant && (step_a || step_b)) ++resolved;
      }
    }
  }
  return total == 0 ? 1.0f : static_cast<float>(resolved) / static_cast<float>(total);
}

uint32_t countAugmentedLeaps(const std::vector<NoteEvent>& notes, uint8_t num_voices) {
  uint32_t count = 0;
  for (uint8_t vid = 0; vid < num_voices; ++vid) {
    auto sorted = voiceNotes(notes, vid);
    for (size_t idx = 1; idx < sorted.size(); ++idx) {
      if (absoluteInterval(sorted[idx].pitch, sorted[idx - 1].pitch) == interval::kTritone)
        ++count;
    }
  }
  return count;
}

CounterpointAnalysisResult analyzeCounterpoint(const std::vector<NoteEvent>& notes,
                                               uint8_t num_voices) {
  CounterpointAnalysisResult result;
  result.parallel_perfect_count = countParallelPerfect(notes, num_voices);
  result.hidden_perfect_count = countHiddenPerfect(notes, num_voices);
  result.voice_crossing_count = countVoiceCrossings(notes, num_voices);
  result.augmented_leap_count = countAugmentedLeaps(notes, num_voices);
  result.dissonance_resolution_rate = dissonanceResolutionRate(notes, num_voices);

  uint32_t violations = result.parallel_perfect_count + result.hidden_perfect_count +
                        result.voice_crossing_count + result.augmented_leap_count;
  Tick end = totalEndTick(notes);
  uint32_t beats = end == 0 ? 0 : static_cast<uint32_t>((end + kTicksPerBeat - 1) / kTicksPerBeat);
  if (beats > 0)
    result.overall_compliance_rate = clamp01(1.0f - static_cast<float>(violations) /
                                                        static_cast<float>(beats));
  return result;
}

FailReport buildCounterpointReport(const std::vector<NoteEvent>& notes, uint8_t num_voices) {
  FailReport report;
  if (num_voices < 2) return report;
  auto voices = buildVoiceCache(notes, num_voices);
  Tick end = totalEndTick(notes);
  if (end < kTicksPerBeat) return report;

  // Parallel and hidden perfects.
  for (uint8_t va = 0; va < num_voices; ++va) {
    for (uint8_t vb = va + 1; vb < num_voices; ++vb) {
      int prev_a = -1, prev_b = -1;
      for (Tick beat = 0; beat < end; beat += kTicksPerBeat) {
        int ca = soundingPitch(voices[va], beat), cb = soundingPitch(voices[vb], beat);
        if (ca < 0 || cb < 0 || prev_a < 0 || prev_b < 0) {
          prev_a = ca; prev_b = cb; continue;
        }
        int pi = std::abs(prev_a - prev_b), ci = std::abs(ca - cb);
        int ma = ca - prev_a, mb = cb - prev_b;
        bool sd = (ma > 0 && mb > 0) || (ma < 0 && mb < 0);
        int ps = ((pi % 12) + 12) % 12, cs = ((ci % 12) + 12) % 12;

        if (isPerfectConsonance(pi) && isPerfectConsonance(ci) && sd && ps == cs) {
          const char* name = (cs == 7) ? "parallel_fifths" : "parallel_octaves";
          const char* desc_type = (cs == 7) ? "5ths" : "8ths";
          report.addIssue(makeIssue(FailSeverity::Critical, beat, va, vb, name,
              "Parallel " + std::string(desc_type) + " between voices " +
              std::to_string(va) + " and " + std::to_string(vb)));
        }
        if (isPerfectConsonance(ci) && ps != cs && sd && std::abs(ma) > 2) {
          report.addIssue(makeIssue(FailSeverity::Warning, beat, va, vb, "hidden_perfect",
              "Hidden perfect interval between voices " +
              std::to_string(va) + " and " + std::to_string(vb)));
        }
        prev_a = ca; prev_b = cb;
      }
    }
  }

  // Voice crossings.
  for (Tick beat = 0; beat < end; beat += kTicksPerBeat) {
    for (uint8_t va = 0; va < num_voices; ++va) {
      for (uint8_t vb = va + 1; vb < num_voices; ++vb) {
        int hi = soundingPitch(voices[va], beat), lo = soundingPitch(voices[vb], beat);
        if (hi >= 0 && lo >= 0 && hi < lo)
          report.addIssue(makeIssue(FailSeverity::Critical, beat, va, vb, "voice_crossing",
              "Voice " + std::to_string(va) + " (pitch " + std::to_string(hi) +
              ") crosses below voice " + std::to_string(vb) +
              " (pitch " + std::to_string(lo) + ")"));
      }
    }
  }

  // Augmented leaps.
  for (uint8_t vid = 0; vid < num_voices; ++vid) {
    for (size_t idx = 1; idx < voices[vid].size(); ++idx) {
      if (absoluteInterval(voices[vid][idx].pitch, voices[vid][idx - 1].pitch) ==
          interval::kTritone) {
        report.addIssue(makeIssue(FailSeverity::Critical, voices[vid][idx].start_tick,
            vid, vid, "augmented_leap",
            "Tritone leap in voice " + std::to_string(vid) + " from pitch " +
            std::to_string(voices[vid][idx - 1].pitch) + " to " +
            std::to_string(voices[vid][idx].pitch)));
      }
    }
  }
  return report;
}

}  // namespace bach
