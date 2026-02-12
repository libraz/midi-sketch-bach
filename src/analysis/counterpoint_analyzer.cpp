// Counterpoint quality analysis implementation.

#include "analysis/counterpoint_analyzer.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "core/basic_types.h"
#include "core/interval.h"
#include "core/note_source.h"
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

/// @brief Source of the note sounding at tick, or BachNoteSource::Unknown if silent.
BachNoteSource soundingSource(const std::vector<NoteEvent>& sorted_voice, Tick tick) {
  BachNoteSource result = BachNoteSource::Unknown;
  for (const auto& note : sorted_voice) {
    if (note.start_tick <= tick && tick < note.start_tick + note.duration)
      result = note.source;
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
        if (interval_util::isPerfectConsonance(pi) && interval_util::isPerfectConsonance(ci)) {
          int ma = cur_a - prev_a, mb = cur_b - prev_b;
          bool same_dir = (ma > 0 && mb > 0) || (ma < 0 && mb < 0);
          int ps = interval_util::compoundToSimple(pi), cs = interval_util::compoundToSimple(ci);
          if (same_dir && ps == cs) ++count;
        }
        prev_a = cur_a; prev_b = cur_b;
      }
    }
  }
  return count;
}

namespace {

/// @brief Count parallel perfects where both notes at both beats are structural.
uint32_t countStructuralParallels(const VoiceCache& voices, uint8_t num_voices,
                                   Tick end) {
  uint32_t count = 0;
  for (uint8_t va_idx = 0; va_idx < num_voices; ++va_idx) {
    for (uint8_t vb_idx = va_idx + 1; vb_idx < num_voices; ++vb_idx) {
      int prev_a = -1, prev_b = -1;
      BachNoteSource prev_src_a = BachNoteSource::Unknown;
      BachNoteSource prev_src_b = BachNoteSource::Unknown;
      for (Tick beat = 0; beat < end; beat += kTicksPerBeat) {
        int cur_a = soundingPitch(voices[va_idx], beat);
        int cur_b = soundingPitch(voices[vb_idx], beat);
        BachNoteSource src_a = soundingSource(voices[va_idx], beat);
        BachNoteSource src_b = soundingSource(voices[vb_idx], beat);
        if (cur_a < 0 || cur_b < 0 || prev_a < 0 || prev_b < 0) {
          prev_a = cur_a; prev_b = cur_b;
          prev_src_a = src_a; prev_src_b = src_b;
          continue;
        }
        int pi = std::abs(prev_a - prev_b), ci = std::abs(cur_a - cur_b);
        if (interval_util::isPerfectConsonance(pi) && interval_util::isPerfectConsonance(ci)) {
          int ma = cur_a - prev_a, mb = cur_b - prev_b;
          bool same_dir = (ma > 0 && mb > 0) || (ma < 0 && mb < 0);
          int ps = interval_util::compoundToSimple(pi), cs = interval_util::compoundToSimple(ci);
          if (same_dir && ps == cs) {
            // Both notes at both beats must be structural.
            if (isStructuralSource(prev_src_a) && isStructuralSource(prev_src_b) &&
                isStructuralSource(src_a) && isStructuralSource(src_b)) {
              ++count;
            }
          }
        }
        prev_a = cur_a; prev_b = cur_b;
        prev_src_a = src_a; prev_src_b = src_b;
      }
    }
  }
  return count;
}

}  // namespace

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
        int ps = interval_util::compoundToSimple(pi), cs = interval_util::compoundToSimple(ci);
        if (interval_util::isPerfectConsonance(ci) && ps != cs) {
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
        if (hi >= 0 && lo >= 0 && hi < lo) {
          // 1-beat lookahead: skip temporary crossings that resolve immediately.
          Tick next_beat = beat + kTicksPerBeat;
          if (next_beat < end) {
            int next_hi = soundingPitch(voices[va], next_beat);
            int next_lo = soundingPitch(voices[vb], next_beat);
            if (next_hi >= 0 && next_lo >= 0 && next_hi >= next_lo) {
              continue;
            }
          }
          ++count;
        }
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
        if (classifyInterval(std::abs(pa - pb)) != IntervalQuality::Dissonance) continue;
        ++total;
        int na = soundingPitch(voices[va], next), nb = soundingPitch(voices[vb], next);
        if (na < 0 || nb < 0) continue;
        bool consonant = classifyInterval(std::abs(na - nb)) != IntervalQuality::Dissonance;
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

  if (num_voices >= 2 && result.parallel_perfect_count > 0) {
    auto voices = buildVoiceCache(notes, num_voices);
    Tick end = totalEndTick(notes);
    result.structural_parallel_count =
        countStructuralParallels(voices, num_voices, end);
  }

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
        int ps = interval_util::compoundToSimple(pi), cs = interval_util::compoundToSimple(ci);

        if (interval_util::isPerfectConsonance(pi) && interval_util::isPerfectConsonance(ci) && sd && ps == cs) {
          const char* name = (cs == 7) ? "parallel_fifths" : "parallel_octaves";
          const char* desc_type = (cs == 7) ? "5ths" : "8ths";
          report.addIssue(makeIssue(FailSeverity::Critical, beat, va, vb, name,
              "Parallel " + std::string(desc_type) + " between voices " +
              std::to_string(va) + " and " + std::to_string(vb)));
        }
        if (interval_util::isPerfectConsonance(ci) && ps != cs && sd && std::abs(ma) > 2) {
          report.addIssue(makeIssue(FailSeverity::Warning, beat, va, vb, "hidden_perfect",
              "Hidden perfect interval between voices " +
              std::to_string(va) + " and " + std::to_string(vb)));
        }
        prev_a = ca; prev_b = cb;
      }
    }
  }

  // Voice crossings (with 1-beat lookahead to exclude temporary crossings).
  for (Tick beat = 0; beat < end; beat += kTicksPerBeat) {
    for (uint8_t va = 0; va < num_voices; ++va) {
      for (uint8_t vb = va + 1; vb < num_voices; ++vb) {
        int hi = soundingPitch(voices[va], beat), lo = soundingPitch(voices[vb], beat);
        if (hi >= 0 && lo >= 0 && hi < lo) {
          // Skip temporary crossings that resolve at the next beat.
          Tick next_beat = beat + kTicksPerBeat;
          if (next_beat < end) {
            int next_hi = soundingPitch(voices[va], next_beat);
            int next_lo = soundingPitch(voices[vb], next_beat);
            if (next_hi >= 0 && next_lo >= 0 && next_hi >= next_lo) {
              continue;
            }
          }
          report.addIssue(makeIssue(FailSeverity::Critical, beat, va, vb, "voice_crossing",
              "Voice " + std::to_string(va) + " (pitch " + std::to_string(hi) +
              ") crosses below voice " + std::to_string(vb) +
              " (pitch " + std::to_string(lo) + ")"));
        }
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

float bassLineStepwiseRatio(const std::vector<NoteEvent>& notes, uint8_t num_voices) {
  if (num_voices == 0) return 1.0f;
  // Bass voice is the highest voice ID (lowest register).
  auto bass_notes = voiceNotes(notes, num_voices - 1);
  if (bass_notes.size() < 2) return 1.0f;

  uint32_t total = 0;
  uint32_t stepwise = 0;
  for (size_t idx = 1; idx < bass_notes.size(); ++idx) {
    int interval = absoluteInterval(bass_notes[idx].pitch, bass_notes[idx - 1].pitch);
    ++total;
    if (interval >= 1 && interval <= 2) ++stepwise;
  }
  return total == 0 ? 1.0f : static_cast<float>(stepwise) / static_cast<float>(total);
}

float voiceLeadingSmoothness(const std::vector<NoteEvent>& notes, uint8_t num_voices) {
  if (num_voices == 0) return 0.0f;

  float total_avg = 0.0f;
  uint8_t voices_with_motion = 0;

  for (uint8_t vid = 0; vid < num_voices; ++vid) {
    auto sorted = voiceNotes(notes, vid);
    if (sorted.size() < 2) continue;

    float voice_sum = 0.0f;
    uint32_t intervals = 0;
    for (size_t idx = 1; idx < sorted.size(); ++idx) {
      int leap = absoluteInterval(sorted[idx].pitch, sorted[idx - 1].pitch);
      voice_sum += static_cast<float>(leap);
      ++intervals;
    }
    if (intervals > 0) {
      total_avg += voice_sum / static_cast<float>(intervals);
      ++voices_with_motion;
    }
  }

  return voices_with_motion == 0 ? 0.0f : total_avg / static_cast<float>(voices_with_motion);
}

float contraryMotionRate(const std::vector<NoteEvent>& notes, uint8_t num_voices) {
  if (num_voices < 2) return 0.0f;
  auto voices = buildVoiceCache(notes, num_voices);
  Tick end = totalEndTick(notes);
  if (end < kTicksPerBeat) return 0.0f;

  uint32_t total_transitions = 0;
  uint32_t contrary_count = 0;

  for (uint8_t vid = 0; vid + 1 < num_voices; ++vid) {
    int prev_a = -1, prev_b = -1;
    for (Tick beat = 0; beat < end; beat += kTicksPerBeat) {
      int cur_a = soundingPitch(voices[vid], beat);
      int cur_b = soundingPitch(voices[vid + 1], beat);
      if (cur_a < 0 || cur_b < 0 || prev_a < 0 || prev_b < 0) {
        prev_a = cur_a;
        prev_b = cur_b;
        continue;
      }
      int motion_a = cur_a - prev_a;
      int motion_b = cur_b - prev_b;
      // Only count when both voices actually move.
      if (motion_a != 0 && motion_b != 0) {
        ++total_transitions;
        bool contrary = (motion_a > 0 && motion_b < 0) || (motion_a < 0 && motion_b > 0);
        if (contrary) ++contrary_count;
      }
      prev_a = cur_a;
      prev_b = cur_b;
    }
  }

  return total_transitions == 0
             ? 0.0f
             : static_cast<float>(contrary_count) / static_cast<float>(total_transitions);
}

float leapResolutionRate(const std::vector<NoteEvent>& notes, uint8_t num_voices) {
  if (num_voices == 0) return 1.0f;

  uint32_t total_leaps = 0;
  uint32_t resolved_leaps = 0;

  for (uint8_t vid = 0; vid < num_voices; ++vid) {
    auto sorted = voiceNotes(notes, vid);
    if (sorted.size() < 2) continue;

    for (size_t idx = 1; idx < sorted.size(); ++idx) {
      int leap = static_cast<int>(sorted[idx].pitch) -
                 static_cast<int>(sorted[idx - 1].pitch);
      int abs_leap = std::abs(leap);
      if (abs_leap < 4) continue;  // Not a leap.

      ++total_leaps;
      // Check if the next note resolves by step in the opposite direction.
      if (idx + 1 < sorted.size()) {
        int resolution = static_cast<int>(sorted[idx + 1].pitch) -
                         static_cast<int>(sorted[idx].pitch);
        int abs_resolution = std::abs(resolution);
        bool step = abs_resolution >= 1 && abs_resolution <= 2;
        bool opposite = (leap > 0 && resolution < 0) || (leap < 0 && resolution > 0);
        if (step && opposite) ++resolved_leaps;
      }
    }
  }

  return total_leaps == 0 ? 1.0f
                          : static_cast<float>(resolved_leaps) / static_cast<float>(total_leaps);
}

// ---------------------------------------------------------------------------
// Rhythm and texture metrics
// ---------------------------------------------------------------------------

float rhythmDiversityScore(const std::vector<NoteEvent>& notes,
                           uint8_t num_voices) {
  (void)num_voices;  // Score spans all voices collectively.
  if (notes.empty()) return 1.0f;

  // Count occurrences of each distinct duration.
  std::vector<std::pair<Tick, int>> duration_counts;
  for (const auto& note : notes) {
    bool found = false;
    for (auto& dcount : duration_counts) {
      if (dcount.first == note.duration) {
        ++dcount.second;
        found = true;
        break;
      }
    }
    if (!found) {
      duration_counts.push_back({note.duration, 1});
    }
  }

  // Find the most common duration's ratio.
  int max_count = 0;
  for (const auto& dcount : duration_counts) {
    if (dcount.second > max_count) max_count = dcount.second;
  }

  float max_ratio = static_cast<float>(max_count) / static_cast<float>(notes.size());

  // Score: 1.0 when max_ratio <= 0.3, linearly decreasing to 0.0 at 1.0.
  if (max_ratio <= 0.3f) return 1.0f;
  float score = 1.0f - (max_ratio - 0.3f) / 0.7f;
  if (score < 0.0f) score = 0.0f;
  return score;
}

float textureDensityVariance(const std::vector<NoteEvent>& notes,
                             uint8_t num_voices) {
  (void)num_voices;  // Density is measured across all voices.
  if (notes.empty()) return 0.0f;

  // Find the tick range.
  Tick min_tick = notes[0].start_tick;
  Tick max_tick = 0;
  for (const auto& note : notes) {
    if (note.start_tick < min_tick) min_tick = note.start_tick;
    Tick end = note.start_tick + note.duration;
    if (end > max_tick) max_tick = end;
  }

  if (max_tick <= min_tick) return 0.0f;

  // Count simultaneous notes per beat.
  std::vector<int> beat_counts;
  for (Tick tick = min_tick; tick < max_tick; tick += kTicksPerBeat) {
    int count = 0;
    for (const auto& note : notes) {
      if (note.start_tick <= tick && tick < note.start_tick + note.duration) {
        ++count;
      }
    }
    beat_counts.push_back(count);
  }

  if (beat_counts.size() < 2) return 0.0f;

  // Calculate mean and standard deviation.
  float sum = 0.0f;
  for (int cnt : beat_counts) sum += static_cast<float>(cnt);
  float mean = sum / static_cast<float>(beat_counts.size());

  float var_sum = 0.0f;
  for (int cnt : beat_counts) {
    float diff = static_cast<float>(cnt) - mean;
    var_sum += diff * diff;
  }
  float variance = var_sum / static_cast<float>(beat_counts.size());

  return std::sqrt(variance);
}

// ---------------------------------------------------------------------------
// Cross-relation detection
// ---------------------------------------------------------------------------

uint32_t countCrossRelations(const std::vector<NoteEvent>& notes, uint8_t num_voices,
                              Tick proximity_threshold) {
  if (num_voices < 2) return 0;

  uint32_t count = 0;

  // For each pair of voices, check for chromatic conflicts within proximity.
  for (uint8_t va = 0; va < num_voices; ++va) {
    auto va_notes = voiceNotes(notes, va);
    for (uint8_t vb = va + 1; vb < num_voices; ++vb) {
      auto vb_notes = voiceNotes(notes, vb);

      for (const auto& na : va_notes) {
        int na_pc = static_cast<int>(na.pitch) % 12;

        for (const auto& nb : vb_notes) {
          // Check temporal proximity.
          Tick tick_dist = (na.start_tick > nb.start_tick)
                               ? na.start_tick - nb.start_tick
                               : nb.start_tick - na.start_tick;
          if (tick_dist > proximity_threshold) continue;

          int nb_pc = static_cast<int>(nb.pitch) % 12;

          // Cross-relation: same letter name but different accidental.
          // Detect by checking if the two pitch classes differ by exactly 1
          // semitone (e.g., B-natural=11 vs B-flat=10, or F#=6 vs F=5).
          int pc_diff = std::abs(na_pc - nb_pc);
          if (pc_diff == 1 || pc_diff == 11) {
            // Additional check: only count if they share the same diatonic
            // base. Heuristic: both pitch classes map to adjacent slots in
            // the chromatic scale that correspond to the same letter.
            // Pairs: (0,1) C/C#, (2,3) D/Eb, (4,5) E/F excluded (different letter),
            //        (5,6) F/F#, (7,8) G/Ab, (9,10) A/Bb, (10,11) Bb/B
            int lower_pc = (na_pc < nb_pc) ? na_pc : nb_pc;
            // Exclude E/F (4,5) and B/C (11,0) which are natural half steps.
            if (lower_pc != 4 && !(lower_pc == 11 && pc_diff == 11)) {
              ++count;
            }
          }
        }
      }
    }
  }

  return count;
}

}  // namespace bach
