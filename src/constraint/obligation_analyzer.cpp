// Obligation analyzer implementation.

#include "constraint/obligation_analyzer.h"

#include <algorithm>
#include <cmath>

#include "core/scale.h"

namespace bach {
namespace {

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

constexpr int kLeapThresholdSemitones = 5;  // P4 or larger triggers LeapResolve
constexpr Tick kLeapResolveWindow = kTicksPerBeat * 2;  // 2 beats to resolve
constexpr Tick kLeadingToneDeadline = kTicksPerBeat * 2;
constexpr Tick kSeventhDeadline = kTicksPerBeat * 2;
constexpr Tick kCadenceStableWindow = kTicksPerBeat * 4;  // Last bar
constexpr int kCadenceApproachNotes = 4;  // Last N notes for approach detection

// Harmonic impulse analysis window (in ticks).
constexpr Tick kHarmonicWindowSize = kTicksPerBeat * 2;

// Scale degree 3 (0-based) = 4th degree, acts as 7th of V.
constexpr int kSubdominantDegree = 3;

// ---------------------------------------------------------------------------
// P1.b2: Scale function identification
// ---------------------------------------------------------------------------

struct ScaleInfo {
  int degree;        // 0-based scale degree (0=root, 6=7th), -1 if chromatic
  bool is_chromatic;
};

ScaleInfo identifyScaleFunction(uint8_t pitch, Key key, bool is_minor) {
  ScaleType scale = is_minor ? ScaleType::HarmonicMinor : ScaleType::Major;
  int degree = -1;
  bool on_scale = scale_util::pitchToScaleDegree(pitch, key, scale, degree);

  if (!on_scale && is_minor) {
    // Try natural minor for non-harmonic-minor tones.
    on_scale =
        scale_util::pitchToScaleDegree(pitch, key, ScaleType::NaturalMinor, degree);
  }

  return {degree, !on_scale};
}

/// @brief Check if a pitch is the leading tone (half step below tonic).
bool isLeadingTone(uint8_t pitch, Key key, bool /*is_minor*/) {
  uint8_t tonic_pc = static_cast<uint8_t>(key);
  uint8_t pitch_pc = pitch % 12;
  uint8_t leading_pc = (tonic_pc + 11) % 12;  // Half step below tonic
  return pitch_pc == leading_pc;
}

/// @brief Check if a pitch functions as a chord seventh (4th degree = 7th of V).
bool isChordSeventh(uint8_t pitch, Key key, bool is_minor) {
  ScaleInfo info = identifyScaleFunction(pitch, key, is_minor);
  return !info.is_chromatic && info.degree == kSubdominantDegree;
}

// ---------------------------------------------------------------------------
// P1.b3: LeadingTone / Seventh detection
// ---------------------------------------------------------------------------

void detectLeadingTones(const std::vector<NoteEvent>& notes, Key key,
                        bool is_minor,
                        std::vector<ObligationNode>& obligations,
                        uint16_t& next_id) {
  for (size_t i = 0; i < notes.size(); ++i) {
    if (isLeadingTone(notes[i].pitch, key, is_minor)) {
      ObligationNode node;
      node.id = next_id++;
      node.type = ObligationType::LeadingTone;
      node.origin = notes[i].start_tick;
      node.start_tick = notes[i].start_tick + notes[i].duration;
      node.deadline = node.start_tick + kLeadingToneDeadline;
      node.direction = +1;  // Must resolve upward
      node.strength = ObligationStrength::Structural;
      node.required_interval_semitones = +1;  // Half step up
      obligations.push_back(node);
    }
  }
}

void detectSevenths(const std::vector<NoteEvent>& notes, Key key,
                    bool is_minor,
                    std::vector<ObligationNode>& obligations,
                    uint16_t& next_id) {
  for (size_t i = 0; i < notes.size(); ++i) {
    if (isChordSeventh(notes[i].pitch, key, is_minor)) {
      ObligationNode node;
      node.id = next_id++;
      node.type = ObligationType::Seventh;
      node.origin = notes[i].start_tick;
      node.start_tick = notes[i].start_tick + notes[i].duration;
      node.deadline = node.start_tick + kSeventhDeadline;
      node.direction = -1;  // Must resolve downward
      node.strength = ObligationStrength::Soft;
      node.required_interval_semitones = -1;  // Step down
      obligations.push_back(node);
    }
  }
}

// ---------------------------------------------------------------------------
// P1.b4: LeapResolve detection
// ---------------------------------------------------------------------------

void detectLeapResolves(const std::vector<NoteEvent>& notes,
                        std::vector<ObligationNode>& obligations,
                        uint16_t& next_id) {
  for (size_t i = 1; i < notes.size(); ++i) {
    int interval = static_cast<int>(notes[i].pitch) -
                   static_cast<int>(notes[i - 1].pitch);
    int abs_interval = std::abs(interval);
    if (abs_interval >= kLeapThresholdSemitones) {
      ObligationNode node;
      node.id = next_id++;
      node.type = ObligationType::LeapResolve;
      node.origin = notes[i].start_tick;
      node.start_tick = notes[i].start_tick;
      node.deadline = notes[i].start_tick + kLeapResolveWindow;
      node.direction = (interval > 0) ? -1 : +1;  // Contrary motion
      node.strength = ObligationStrength::Soft;
      node.required_interval_semitones = 0;  // Any stepwise
      obligations.push_back(node);
    }
  }
}

// ---------------------------------------------------------------------------
// P1.b5: StrongBeatHarm detection
// ---------------------------------------------------------------------------

void detectStrongBeatHarm(const std::vector<NoteEvent>& notes,
                          std::vector<ObligationNode>& obligations,
                          uint16_t& next_id) {
  for (size_t i = 0; i < notes.size(); ++i) {
    Tick beat_pos = notes[i].start_tick % kTicksPerBar;
    // Strong beats: beat 1 (0) and beat 3 (kTicksPerBeat * 2)
    bool is_strong = (beat_pos == 0) ||
                     (beat_pos == kTicksPerBeat * 2);
    if (is_strong) {
      ObligationNode node;
      node.id = next_id++;
      node.type = ObligationType::StrongBeatHarm;
      node.origin = notes[i].start_tick;
      node.start_tick = notes[i].start_tick;
      node.deadline = notes[i].start_tick;  // Instantaneous gate
      node.require_strong_beat = true;
      node.strength = ObligationStrength::Structural;
      obligations.push_back(node);
    }
  }
}

// ---------------------------------------------------------------------------
// P1.b6: CadenceStable detection
// ---------------------------------------------------------------------------

void detectCadenceStable(const std::vector<NoteEvent>& notes, Key key,
                         bool is_minor,
                         std::vector<ObligationNode>& obligations,
                         uint16_t& next_id) {
  if (notes.size() < 2) return;

  Tick subject_end = notes.back().start_tick + notes.back().duration;
  Tick cadence_start = (subject_end > kCadenceStableWindow)
                           ? subject_end - kCadenceStableWindow
                           : 0;

  // Check if the last note is on a stable degree (root or 5th).
  ScaleInfo last_info =
      identifyScaleFunction(notes.back().pitch, key, is_minor);
  bool ends_stable =
      !last_info.is_chromatic && (last_info.degree == 0 || last_info.degree == 4);

  if (!ends_stable) {
    ObligationNode node;
    node.id = next_id++;
    node.type = ObligationType::CadenceStable;
    node.origin = notes.back().start_tick;
    node.start_tick = cadence_start;
    node.deadline = subject_end;
    node.strength = ObligationStrength::Soft;
    obligations.push_back(node);
  }
}

// ---------------------------------------------------------------------------
// P1.b7: CadenceApproach detection
// ---------------------------------------------------------------------------

void detectCadenceApproach(const std::vector<NoteEvent>& notes, Key /*key*/,
                           bool /*is_minor*/,
                           std::vector<ObligationNode>& obligations,
                           uint16_t& next_id) {
  if (notes.size() < 3) return;

  Tick subject_end = notes.back().start_tick + notes.back().duration;
  size_t approach_start =
      (notes.size() > static_cast<size_t>(kCadenceApproachNotes))
          ? notes.size() - kCadenceApproachNotes
          : 0;

  // Check if the final notes form a stepwise approach to tonic or dominant.
  bool has_stepwise_approach = false;
  for (size_t i = approach_start + 1; i < notes.size(); ++i) {
    int interval =
        std::abs(static_cast<int>(notes[i].pitch) -
                 static_cast<int>(notes[i - 1].pitch));
    if (interval <= 2) {  // Stepwise (semitone or whole tone)
      has_stepwise_approach = true;
    }
  }

  // Create CadenceApproach obligation for the final segment.
  // Section-final cadences get Structural strength;
  // internal cadences get Soft (to allow evaded/deceptive cadences).
  ObligationNode node;
  node.id = next_id++;
  node.type = ObligationType::CadenceApproach;
  node.origin = notes[approach_start].start_tick;
  node.start_tick = notes[approach_start].start_tick;
  node.deadline = subject_end;
  // Default to Soft; caller upgrades to Structural for section-final cadences.
  node.strength =
      has_stepwise_approach ? ObligationStrength::Soft : ObligationStrength::Structural;
  obligations.push_back(node);
}

// ---------------------------------------------------------------------------
// P1.b8: HarmonicImpulse extraction
// ---------------------------------------------------------------------------

/// @brief Estimate implied chord degree from a pitch-class set.
/// Returns 1-7 (scale degree) and a confidence strength.
struct ImpliedHarmony {
  uint8_t degree;   // 1-7
  float strength;   // 0.0-1.0
};

ImpliedHarmony estimateImpliedDegree(const std::vector<uint8_t>& pitches,
                                     Key key, bool is_minor) {
  if (pitches.empty()) return {1, 0.0f};

  // Count pitch classes relative to tonic.
  uint8_t tonic_pc = static_cast<uint8_t>(key);
  int pc_counts[12] = {};
  for (uint8_t p : pitches) {
    pc_counts[(p - tonic_pc + 12) % 12]++;
  }

  // Simple triad matching: check for root-position triads on each degree.
  // Major scale intervals: 0, 2, 4, 5, 7, 9, 11
  static constexpr int kMajorIntervals[7] = {0, 2, 4, 5, 7, 9, 11};
  static constexpr int kMinorIntervals[7] = {0, 2, 3, 5, 7, 8, 10};
  const int* intervals = is_minor ? kMinorIntervals : kMajorIntervals;

  int best_degree = 0;
  int best_score = 0;
  for (int d = 0; d < 7; ++d) {
    int root = intervals[d];
    int third = intervals[(d + 2) % 7];
    int fifth = intervals[(d + 4) % 7];
    int score = pc_counts[root % 12] * 3 + pc_counts[third % 12] * 2 +
                pc_counts[fifth % 12] * 2;
    if (score > best_score) {
      best_score = score;
      best_degree = d;
    }
  }

  float total_notes = static_cast<float>(pitches.size());
  float strength = std::min(static_cast<float>(best_score) / (total_notes * 3.0f), 1.0f);
  return {static_cast<uint8_t>(best_degree + 1), strength};
}

/// @brief Map 1-7 scale degree to tension level (simplified from computeHarmonicTension).
float degreeTensionLevel(uint8_t degree, bool is_minor) {
  // I=0.0, ii=0.3, iii=0.2, IV=0.3, V=0.6, vi=0.0, vii=0.9
  static constexpr float kMajorTension[7] = {0.0f, 0.3f, 0.2f, 0.3f,
                                              0.6f, 0.0f, 0.9f};
  static constexpr float kMinorTension[7] = {0.0f, 0.3f, 0.2f, 0.3f,
                                              0.6f, 0.1f, 0.9f};
  if (degree < 1 || degree > 7) return 0.5f;
  return is_minor ? kMinorTension[degree - 1] : kMajorTension[degree - 1];
}

/// @brief Determine directional tendency: T→D = +1, T→S = -1, return = 0.
int8_t directionalTendency(uint8_t degree) {
  switch (degree) {
    case 5:
    case 7:
      return +1;  // Dominant direction
    case 2:
    case 4:
      return -1;  // Subdominant direction
    default:
      return 0;  // Tonic/return
  }
}

void extractHarmonicImpulses(const std::vector<NoteEvent>& notes,
                             Key key, bool is_minor,
                             std::vector<HarmonicImpulse>& impulses) {
  if (notes.empty()) return;

  Tick subject_start = notes.front().start_tick;
  Tick subject_end = notes.back().start_tick + notes.back().duration;

  for (Tick window_start = subject_start; window_start < subject_end;
       window_start += kHarmonicWindowSize) {
    Tick window_end = window_start + kHarmonicWindowSize;

    std::vector<uint8_t> window_pitches;
    for (const auto& n : notes) {
      Tick note_end = n.start_tick + n.duration;
      if (n.start_tick < window_end && note_end > window_start) {
        window_pitches.push_back(n.pitch);
      }
    }

    if (window_pitches.empty()) continue;

    ImpliedHarmony implied = estimateImpliedDegree(window_pitches, key, is_minor);
    if (implied.strength < 0.1f) continue;

    HarmonicImpulse imp;
    imp.tick = window_start;
    imp.implied_degree = implied.degree;
    imp.strength = implied.strength;
    imp.directional_tendency = directionalTendency(implied.degree);
    imp.tension_level = degreeTensionLevel(implied.degree, is_minor);
    impulses.push_back(imp);
  }
}

// ---------------------------------------------------------------------------
// P1.b9: RegisterTrajectory extraction
// ---------------------------------------------------------------------------

RegisterTrajectory extractRegisterTrajectory(const std::vector<NoteEvent>& notes) {
  RegisterTrajectory traj;
  if (notes.empty()) return traj;

  traj.opening_pitch = notes.front().pitch;
  traj.closing_pitch = notes.back().pitch;

  // Find peak pitch and its position.
  uint8_t peak = 0;
  size_t peak_idx = 0;
  for (size_t i = 0; i < notes.size(); ++i) {
    if (notes[i].pitch > peak) {
      peak = notes[i].pitch;
      peak_idx = i;
    }
  }
  traj.peak_pitch = peak;

  Tick subject_start = notes.front().start_tick;
  Tick subject_end = notes.back().start_tick + notes.back().duration;
  Tick total_len = subject_end - subject_start;
  if (total_len > 0) {
    traj.peak_position =
        static_cast<float>(notes[peak_idx].start_tick - subject_start) /
        static_cast<float>(total_len);
  }

  // Overall direction: compare opening and closing pitch.
  int diff = static_cast<int>(traj.closing_pitch) -
             static_cast<int>(traj.opening_pitch);
  if (diff > 2)
    traj.overall_direction = +1;
  else if (diff < -2)
    traj.overall_direction = -1;
  else
    traj.overall_direction = 0;  // Return type

  return traj;
}

// ---------------------------------------------------------------------------
// P1.b10: AccentContour extraction
// ---------------------------------------------------------------------------

AccentContour extractAccentContour(const std::vector<NoteEvent>& notes) {
  AccentContour contour;
  if (notes.empty()) return contour;

  Tick subject_start = notes.front().start_tick;
  Tick subject_end = notes.back().start_tick + notes.back().duration;
  Tick total_len = subject_end - subject_start;
  if (total_len == 0) return contour;

  Tick third = total_len / 3;
  float front_accent = 0.0f, mid_accent = 0.0f, tail_accent = 0.0f;
  int syncopation_count = 0;

  for (const auto& n : notes) {
    Tick rel = n.start_tick - subject_start;
    Tick beat_pos = n.start_tick % kTicksPerBeat;

    // Accent weight: strong beat + long duration.
    bool on_strong_beat = (n.start_tick % kTicksPerBar == 0) ||
                          (n.start_tick % kTicksPerBar == kTicksPerBeat * 2);
    float weight = static_cast<float>(n.duration) / static_cast<float>(kTicksPerBeat);
    if (on_strong_beat) weight *= 1.5f;

    // Syncopation: note starts on weak beat but is relatively long.
    if (!on_strong_beat && beat_pos != 0 && n.duration >= kTicksPerBeat) {
      syncopation_count++;
    }

    if (rel < third) {
      front_accent += weight;
    } else if (rel < third * 2) {
      mid_accent += weight;
    } else {
      tail_accent += weight;
    }
  }

  float total_accent = front_accent + mid_accent + tail_accent;
  if (total_accent > 0.0f) {
    contour.front_weight = front_accent / total_accent;
    contour.mid_weight = mid_accent / total_accent;
    contour.tail_weight = tail_accent / total_accent;
  }
  contour.syncopation_ratio =
      static_cast<float>(syncopation_count) / static_cast<float>(notes.size());

  return contour;
}

// ---------------------------------------------------------------------------
// P1.c1: Density metrics
// ---------------------------------------------------------------------------

struct DensityMetrics {
  float peak_density;
  float avg_density;
  float synchronous_pressure;
};

DensityMetrics computeDensityMetrics(
    const std::vector<ObligationNode>& obligations,
    Tick subject_start, Tick subject_end) {
  DensityMetrics metrics = {0.0f, 0.0f, 0.0f};
  if (obligations.empty() || subject_end <= subject_start) return metrics;

  float weighted_sum = 0.0f;
  int ticks_with_debt = 0;
  int ticks_with_debt_and_gate = 0;

  // Sample at beat resolution for efficiency.
  constexpr Tick kSampleStep = kTicksPerBeat / 4;  // 16th note resolution
  int sample_count = 0;

  for (Tick t = subject_start; t < subject_end; t += kSampleStep) {
    int debt_count = 0;
    bool has_gate = false;

    for (const auto& ob : obligations) {
      if (!ob.is_active_at(t)) continue;
      if (ob.is_debt()) {
        debt_count++;
      } else if (ob.type == ObligationType::StrongBeatHarm) {
        has_gate = true;
      }
    }

    if (static_cast<float>(debt_count) > metrics.peak_density) {
      metrics.peak_density = static_cast<float>(debt_count);
    }
    weighted_sum += static_cast<float>(debt_count);
    sample_count++;

    if (debt_count > 0) ticks_with_debt++;
    if (debt_count > 0 && has_gate) ticks_with_debt_and_gate++;
  }

  if (sample_count > 0) {
    metrics.avg_density = weighted_sum / static_cast<float>(sample_count);
  }
  if (ticks_with_debt > 0) {
    metrics.synchronous_pressure =
        static_cast<float>(ticks_with_debt_and_gate) /
        static_cast<float>(ticks_with_debt);
  }

  return metrics;
}

// ---------------------------------------------------------------------------
// P1.e: Stretto feasibility matrix helpers
// ---------------------------------------------------------------------------

/// @brief Maximum number of simultaneous voices to evaluate for stretto.
constexpr int kMaxStrettoVoices = 5;

/// @brief Minimum number of voices for stretto (at least 2 subject presentations).
constexpr int kMinStrettoVoices = 2;

/// @brief Sampling resolution for stretto analysis (16th note).
constexpr Tick kStrettoSampleStep = kTicksPerBeat / 4;

/// @brief Collect note onset ticks relative to subject start.
/// Returns a sorted vector of ticks where notes begin (relative to tick 0).
std::vector<Tick> collectRelativeOnsets(const std::vector<NoteEvent>& notes) {
  if (notes.empty()) return {};
  Tick base = notes.front().start_tick;
  std::vector<Tick> onsets;
  onsets.reserve(notes.size());
  for (const auto& note : notes) {
    onsets.push_back(note.start_tick - base);
  }
  return onsets;
}

/// @brief Compute peak obligation density excess from overlaid subject profiles.
///
/// In stretto, each voice independently carries its own obligations. The single-voice
/// obligation density is the baseline. This metric measures the **excess** obligation
/// pressure beyond what would exist without stretto overlap. Specifically, it computes
/// the average per-voice debt count at the densest point, normalized so that a single
/// voice's obligations contribute 0 (baseline) and additional overlapping obligations
/// from other voices contribute proportionally.
///
/// @param obligations Original single-voice obligations.
/// @param offset Inter-subject onset distance in ticks.
/// @param num_voices Number of simultaneous subject presentations.
/// @param subject_length Total subject duration in ticks.
/// @return Peak excess obligation density (0 = no excess, higher = more conflict).
float computePeakObligation(const std::vector<ObligationNode>& obligations,
                            Tick offset, int num_voices, Tick subject_length) {
  if (obligations.empty()) return 0.0f;

  // Compute single-voice peak debt as baseline.
  float single_voice_peak = 0.0f;
  for (Tick sample = 0; sample < subject_length; sample += kStrettoSampleStep) {
    int debt_count = 0;
    for (const auto& obl : obligations) {
      if (!obl.is_debt()) continue;
      if (sample >= obl.start_tick && sample <= obl.deadline) {
        debt_count++;
      }
    }
    single_voice_peak = std::max(single_voice_peak, static_cast<float>(debt_count));
  }

  // Now compute the peak with overlaid voices, measuring excess above baseline.
  Tick total_span = subject_length + static_cast<Tick>(offset) * (num_voices - 1);
  float peak_excess = 0.0f;

  for (Tick sample = 0; sample < total_span; sample += kStrettoSampleStep) {
    int total_debt = 0;
    int active_voices = 0;

    for (int voice_idx = 0; voice_idx < num_voices; ++voice_idx) {
      Tick voice_offset = static_cast<Tick>(voice_idx) * offset;

      if (sample < voice_offset || sample >= voice_offset + subject_length) {
        continue;
      }
      active_voices++;

      for (const auto& obl : obligations) {
        if (!obl.is_debt()) continue;
        Tick shifted_start = obl.start_tick + voice_offset;
        Tick shifted_deadline = obl.deadline + voice_offset;
        if (sample >= shifted_start && sample <= shifted_deadline) {
          total_debt++;
        }
      }
    }

    if (active_voices <= 1) continue;

    // Excess = total debt beyond what each voice would independently carry.
    // If N voices each have their own obligations, the expected independent total
    // is active_voices * avg_per_voice. The excess is what is above single_voice_peak.
    float excess = static_cast<float>(total_debt) -
                   single_voice_peak * static_cast<float>(active_voices);
    peak_excess = std::max(peak_excess, std::max(excess, 0.0f));
  }

  return peak_excess;
}

/// @brief Estimate vertical clash probability from strong-beat collisions.
///
/// When multiple voices have note onsets simultaneously on strong beats, there is a
/// higher probability of parallel perfect consonances (P5/P8). This estimates that
/// probability by counting simultaneous strong-beat onsets across all voice pairs.
///
/// @param onsets Relative onset ticks of the subject.
/// @param offset Inter-subject onset distance.
/// @param num_voices Number of simultaneous subject presentations.
/// @param subject_length Total subject duration.
/// @return Estimated vertical clash probability in [0.0, 1.0].
float estimateVerticalClash(const std::vector<Tick>& onsets, Tick offset,
                            int num_voices, Tick subject_length) {
  if (onsets.empty() || num_voices < 2) return 0.0f;

  int strong_beat_collisions = 0;
  int total_strong_beats = 0;

  Tick total_span = subject_length + static_cast<Tick>(offset) * (num_voices - 1);

  // Scan all strong beat positions in the combined timeline.
  for (Tick tick = 0; tick < total_span; tick += kTicksPerBeat) {
    if (!isStrongBeatInBar(tick)) continue;
    total_strong_beats++;

    // Count how many voices have an onset near this strong beat.
    int voices_with_onset = 0;
    for (int voice_idx = 0; voice_idx < num_voices; ++voice_idx) {
      Tick voice_shift = static_cast<Tick>(voice_idx) * offset;

      for (Tick onset : onsets) {
        Tick shifted = onset + voice_shift;
        // Allow a small tolerance window (within a 16th note).
        if (shifted >= tick && shifted < tick + kStrettoSampleStep) {
          voices_with_onset++;
          break;  // Count each voice at most once per beat.
        }
      }
    }

    if (voices_with_onset >= 2) {
      strong_beat_collisions++;
    }
  }

  if (total_strong_beats == 0) return 0.0f;
  return static_cast<float>(strong_beat_collisions) /
         static_cast<float>(total_strong_beats);
}

/// @brief Compute rhythmic interference: ratio of ticks with multiple accent collisions.
///
/// An "accent" is a note onset on a strong beat or a note with duration >= quarter note.
/// Interference measures how often multiple voices have accents at the same tick.
///
/// @param notes Subject notes.
/// @param offset Inter-subject onset distance.
/// @param num_voices Number of simultaneous subject presentations.
/// @param subject_length Total subject duration.
/// @return Rhythmic interference ratio in [0.0, 1.0].
float computeRhythmicInterference(const std::vector<NoteEvent>& notes, Tick offset,
                                   int num_voices, Tick subject_length) {
  if (notes.empty() || num_voices < 2) return 0.0f;

  Tick base = notes.front().start_tick;
  Tick total_span = subject_length + static_cast<Tick>(offset) * (num_voices - 1);
  int collision_samples = 0;
  int total_samples = 0;

  for (Tick sample = 0; sample < total_span; sample += kStrettoSampleStep) {
    total_samples++;
    int accent_voices = 0;

    for (int voice_idx = 0; voice_idx < num_voices; ++voice_idx) {
      Tick voice_shift = static_cast<Tick>(voice_idx) * offset;

      if (sample < voice_shift || sample >= voice_shift + subject_length) {
        continue;
      }

      // Check if any note in this voice creates an accent at this sample position.
      for (const auto& note : notes) {
        Tick rel_onset = note.start_tick - base + voice_shift;
        // Accent: onset on strong beat or long note.
        bool is_onset = (sample >= rel_onset && sample < rel_onset + kStrettoSampleStep);
        if (is_onset) {
          bool is_accent = isStrongBeatInBar(rel_onset) ||
                           note.duration >= kTicksPerBeat;
          if (is_accent) {
            accent_voices++;
            break;  // One accent per voice per sample is enough.
          }
        }
      }
    }

    if (accent_voices >= 2) {
      collision_samples++;
    }
  }

  if (total_samples == 0) return 0.0f;
  return static_cast<float>(collision_samples) / static_cast<float>(total_samples);
}

/// @brief Compute register overlap between overlaid subject presentations.
///
/// Uses the subject's pitch range to estimate how much voices overlap in register
/// when the same subject is presented at different offsets. Since all voices present
/// the same subject (possibly transposed), register overlap is based on the timing
/// overlap fraction scaled by the pitch range narrowness.
///
/// @param register_arc Subject's register trajectory.
/// @param offset Inter-subject onset distance.
/// @param num_voices Number of simultaneous subject presentations.
/// @param subject_length Total subject duration.
/// @return Register overlap ratio in [0.0, 1.0].
float computeRegisterOverlap(const RegisterTrajectory& register_arc, Tick offset,
                              int num_voices, Tick subject_length) {
  if (num_voices < 2 || subject_length == 0) return 0.0f;

  // Pitch range of the subject.
  int range = static_cast<int>(register_arc.peak_pitch) -
              static_cast<int>(std::min(register_arc.opening_pitch,
                                        register_arc.closing_pitch));
  if (range <= 0) range = 1;

  // Temporal overlap: what fraction of time do multiple voices overlap.
  // Voice i starts at offset*i and ends at offset*i + subject_length.
  // The overlap between consecutive voices = max(0, subject_length - offset).
  Tick temporal_overlap_ticks = 0;
  if (offset < subject_length) {
    temporal_overlap_ticks = subject_length - offset;
  }
  float temporal_ratio =
      static_cast<float>(temporal_overlap_ticks) / static_cast<float>(subject_length);

  // Narrow range subjects have higher register overlap risk.
  // An octave (12 semitones) is the "standard" separation; narrower ranges overlap more.
  float range_factor = std::min(12.0f / static_cast<float>(range), 1.0f);

  // Combined: temporal overlap weighted by range narrowness.
  // More voices increase the overlap density.
  float voice_factor = static_cast<float>(num_voices - 1) /
                       static_cast<float>(kMaxStrettoVoices - 1);
  return std::min(temporal_ratio * range_factor * (0.5f + 0.5f * voice_factor), 1.0f);
}

/// @brief Compute perceptual overlap based on AccentContour collision.
///
/// When accent peaks of overlaid subjects coincide, the listener cannot distinguish
/// individual voices. This scores the degree of accent peak coincidence.
///
/// @param contour Subject's accent contour.
/// @param offset Inter-subject onset distance.
/// @param subject_length Total subject duration.
/// @param num_voices Number of simultaneous presentations.
/// @return Perceptual overlap score in [0.0, 1.0].
float computePerceptualOverlap(const AccentContour& contour, Tick offset,
                                Tick subject_length, int num_voices) {
  if (num_voices < 2 || subject_length == 0) return 0.0f;

  // Determine where the accent peak is (front, mid, or tail).
  // The peak third has the highest weight.
  float peak_weight = std::max({contour.front_weight, contour.mid_weight,
                                contour.tail_weight});
  if (peak_weight < 0.01f) return 0.0f;

  // Find which third contains the peak.
  Tick third = subject_length / 3;
  Tick peak_center = 0;
  if (contour.front_weight >= contour.mid_weight &&
      contour.front_weight >= contour.tail_weight) {
    peak_center = third / 2;  // Center of front third.
  } else if (contour.mid_weight >= contour.tail_weight) {
    peak_center = third + third / 2;  // Center of mid third.
  } else {
    peak_center = third * 2 + third / 2;  // Center of tail third.
  }

  // Check if peaks of consecutive voices land in the same third of another voice.
  float collision_score = 0.0f;
  int pair_count = 0;

  for (int voice_a = 0; voice_a < num_voices; ++voice_a) {
    for (int voice_b = voice_a + 1; voice_b < num_voices; ++voice_b) {
      pair_count++;

      // Voice B's peak position relative to voice A's timeline.
      Tick shift = static_cast<Tick>(voice_b - voice_a) * offset;
      Tick peak_b_in_a = peak_center + shift;

      // Check if peak_b falls within voice A's subject duration.
      Tick voice_a_start = static_cast<Tick>(voice_a) * offset;
      Tick voice_a_end = voice_a_start + subject_length;

      if (peak_b_in_a >= voice_a_start && peak_b_in_a < voice_a_end) {
        // Determine which third of voice A the collision falls in.
        Tick rel_pos = peak_b_in_a - voice_a_start;
        float coinciding_weight = 0.0f;
        if (rel_pos < third) {
          coinciding_weight = contour.front_weight;
        } else if (rel_pos < third * 2) {
          coinciding_weight = contour.mid_weight;
        } else {
          coinciding_weight = contour.tail_weight;
        }

        // High collision when the coinciding region also has a high accent weight.
        collision_score += coinciding_weight * peak_weight;
      }
    }
  }

  if (pair_count == 0) return 0.0f;
  return std::min(collision_score / static_cast<float>(pair_count), 1.0f);
}

/// @brief Compute cadence conflict score.
///
/// CadenceApproach obligations from different voices may conflict when one voice
/// is approaching a cadence while another is in its development phase. This scores
/// the degree to which cadence-related obligations overlap across voices.
///
/// @param obligations Original single-voice obligations.
/// @param offset Inter-subject onset distance.
/// @param num_voices Number of simultaneous subject presentations.
/// @param subject_length Total subject duration.
/// @return Cadence conflict score in [0.0, 1.0].
float computeCadenceConflict(const std::vector<ObligationNode>& obligations,
                              Tick offset, int num_voices, Tick subject_length) {
  if (num_voices < 2) return 0.0f;

  // Collect cadence obligations (CadenceStable and CadenceApproach).
  std::vector<const ObligationNode*> cadence_obs;
  for (const auto& obl : obligations) {
    if (obl.type == ObligationType::CadenceStable ||
        obl.type == ObligationType::CadenceApproach) {
      cadence_obs.push_back(&obl);
    }
  }

  if (cadence_obs.empty()) return 0.0f;

  // For each pair of voices, check if cadence regions overlap with non-cadence
  // regions of the other voice (i.e., one voice needs cadential stability while
  // another is in the middle of its subject).
  int conflict_count = 0;
  int pair_count = 0;

  for (int voice_a = 0; voice_a < num_voices; ++voice_a) {
    for (int voice_b = voice_a + 1; voice_b < num_voices; ++voice_b) {
      pair_count++;
      Tick shift = static_cast<Tick>(voice_b - voice_a) * offset;

      for (const auto* cad_obl : cadence_obs) {
        // Voice A's cadence region.
        Tick cad_start_a = cad_obl->start_tick +
                           static_cast<Tick>(voice_a) * offset;
        Tick cad_end_a = cad_obl->deadline +
                         static_cast<Tick>(voice_a) * offset;

        // Voice B's subject is active from voice_b*offset to voice_b*offset + subject_length.
        Tick voice_b_start = static_cast<Tick>(voice_b) * offset;
        Tick voice_b_mid_end = voice_b_start + subject_length / 2;  // First half = development.

        // Conflict: voice A's cadence overlaps voice B's development phase.
        if (cad_start_a < voice_b_mid_end && cad_end_a > voice_b_start) {
          conflict_count++;
        }

        // Also check reverse: voice B's cadence vs voice A's development.
        Tick cad_start_b = cad_obl->start_tick + shift +
                           static_cast<Tick>(voice_a) * offset;
        Tick cad_end_b = cad_obl->deadline + shift +
                         static_cast<Tick>(voice_a) * offset;
        Tick voice_a_start = static_cast<Tick>(voice_a) * offset;
        Tick voice_a_mid_end = voice_a_start + subject_length / 2;

        if (cad_start_b < voice_a_mid_end && cad_end_b > voice_a_start) {
          conflict_count++;
        }
      }
    }
  }

  // Normalize: max possible conflicts = pair_count * cadence_obs.size() * 2.
  int max_conflicts = pair_count * static_cast<int>(cadence_obs.size()) * 2;
  if (max_conflicts == 0) return 0.0f;
  return std::min(static_cast<float>(conflict_count) /
                      static_cast<float>(max_conflicts),
                  1.0f);
}

}  // anonymous namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

SubjectConstraintProfile analyzeObligations(
    const std::vector<NoteEvent>& notes, Key key, bool is_minor) {
  SubjectConstraintProfile profile;
  if (notes.empty()) return profile;

  uint16_t next_id = 0;

  // P1.b3: LeadingTone / Seventh detection
  detectLeadingTones(notes, key, is_minor, profile.obligations, next_id);
  detectSevenths(notes, key, is_minor, profile.obligations, next_id);

  // P1.b4: LeapResolve detection
  detectLeapResolves(notes, profile.obligations, next_id);

  // P1.b5: StrongBeatHarm detection
  detectStrongBeatHarm(notes, profile.obligations, next_id);

  // P1.b6: CadenceStable detection
  detectCadenceStable(notes, key, is_minor, profile.obligations, next_id);

  // P1.b7: CadenceApproach detection
  detectCadenceApproach(notes, key, is_minor, profile.obligations, next_id);

  // P1.c1: Density metrics
  Tick subject_start = notes.front().start_tick;
  Tick subject_end = notes.back().start_tick + notes.back().duration;
  DensityMetrics dm =
      computeDensityMetrics(profile.obligations, subject_start, subject_end);
  profile.peak_density = dm.peak_density;
  profile.avg_density = dm.avg_density;
  profile.synchronous_pressure = dm.synchronous_pressure;

  // P1.b8: HarmonicImpulse extraction
  extractHarmonicImpulses(notes, key, is_minor, profile.harmonic_impulses);

  // P1.b9: RegisterTrajectory extraction
  profile.register_arc = extractRegisterTrajectory(notes);

  // P1.b10: AccentContour extraction
  profile.accent_contour = extractAccentContour(notes);

  // Imitation characteristics (basic heuristics, refined in Phase 1e).
  // Tonal answer feasibility: subject starts on tonic or dominant.
  ScaleInfo first_info = identifyScaleFunction(notes.front().pitch, key, is_minor);
  profile.tonal_answer_feasible =
      !first_info.is_chromatic && (first_info.degree == 0 || first_info.degree == 4);

  // Invertible at octave: check if range fits within an octave.
  uint8_t min_pitch = 127, max_pitch = 0;
  for (const auto& n : notes) {
    min_pitch = std::min(min_pitch, n.pitch);
    max_pitch = std::max(max_pitch, n.pitch);
  }
  profile.invertible_8ve = (max_pitch - min_pitch) <= 12;

  // Cadence gravity: ratio of CadenceStable/CadenceApproach obligations.
  int cadence_obs = 0;
  for (const auto& ob : profile.obligations) {
    if (ob.type == ObligationType::CadenceStable ||
        ob.type == ObligationType::CadenceApproach) {
      cadence_obs++;
    }
  }
  profile.cadence_gravity =
      static_cast<float>(cadence_obs) /
      std::max(static_cast<float>(profile.obligations.size()), 1.0f);

  // P1.e: Stretto feasibility matrix
  Tick subject_length = subject_end - subject_start;
  computeStrettoFeasibility(profile, notes, subject_length);

  return profile;
}

void computeStrettoFeasibility(
    SubjectConstraintProfile& profile,
    const std::vector<NoteEvent>& notes,
    Tick subject_length) {
  profile.stretto_matrix.clear();

  if (notes.empty() || subject_length <= kTicksPerBeat) return;

  std::vector<Tick> onsets = collectRelativeOnsets(notes);

  // Evaluate all offset/voice combinations.
  // Offset range: 1 beat to subject_length - 1 beat, stepping by half beats for
  // practical resolution without excessive computation.
  constexpr Tick kOffsetStep = kTicksPerBeat / 2;

  for (Tick offset = kTicksPerBeat; offset < subject_length; offset += kOffsetStep) {
    for (int num_voices = kMinStrettoVoices; num_voices <= kMaxStrettoVoices;
         ++num_voices) {
      StrettoFeasibilityEntry entry;
      entry.offset_ticks = static_cast<int>(offset);
      entry.num_voices = num_voices;

      // Peak obligation density across overlaid profiles.
      entry.peak_obligation = computePeakObligation(
          profile.obligations, offset, num_voices, subject_length);

      // Musical indices.
      entry.vertical_clash = estimateVerticalClash(
          onsets, offset, num_voices, subject_length);

      entry.rhythmic_interference = computeRhythmicInterference(
          notes, offset, num_voices, subject_length);

      entry.register_overlap = computeRegisterOverlap(
          profile.register_arc, offset, num_voices, subject_length);

      entry.perceptual_overlap_score = computePerceptualOverlap(
          profile.accent_contour, offset, subject_length, num_voices);

      entry.cadence_conflict_score = computeCadenceConflict(
          profile.obligations, offset, num_voices, subject_length);

      profile.stretto_matrix.push_back(entry);
    }
  }
}

}  // namespace bach
