// AriaTheme implementation: generative 2-layer Sarabande melody.
// Kern (Beat 1): chord tone scoring with softmax selection.
// Surface (Beat 2/3): BeatFunction probability tables + resolution.

#include "forms/goldberg/goldberg_aria_theme.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <random>
#include <vector>

#include "core/pitch_utils.h"
#include "core/scale.h"
#include "harmony/chord_tone_utils.h"
#include "harmony/chord_types.h"

namespace bach {

namespace {

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

constexpr uint8_t kSopranoLow = 67;   // G4
constexpr uint8_t kSopranoHigh = 81;  // A5
constexpr uint8_t kSopranoCenter = 74;  // D5
constexpr int kMaxLeap = 7;  // Perfect 5th in semitones.
constexpr int kMaxSamePitchRun = 2;  // Max consecutive same Beat 1 pitch.
constexpr float kSoftmaxTemp = 0.8f;
constexpr float kMinConsonantRatio = 0.6f;  // 2/3 beats must be chord tones.

// Arc factors for register targets per PhrasePosition.
constexpr float kArcOpening = 0.6f;
constexpr float kArcExpansion = 0.8f;
constexpr float kArcIntensification = 1.0f;
constexpr float kArcCadence = 0.5f;

// Kern scoring weights.
constexpr float kStepWeight = 4.0f;
constexpr float kDirectionWeight = 2.0f;
constexpr float kRegisterWeight = 1.5f;
constexpr float kVarietyWeight = 1.0f;
constexpr float kCadenceWeight = 3.0f;

// Tonic/dominant pitch classes for G major.
constexpr int kTonicPC = 7;     // G
constexpr int kDominantPC = 2;  // D

// ---------------------------------------------------------------------------
// Local scale neighbor (reimplemented from goldberg_figuren.cpp)
// ---------------------------------------------------------------------------

uint8_t scaleNeighbor(uint8_t pitch, int direction, const KeySignature& key) {
  ScaleType st = key.is_minor ? ScaleType::NaturalMinor : ScaleType::Major;
  const int* scale = getScaleIntervals(st);
  int root_pc = static_cast<int>(key.tonic);
  int pitch_val = static_cast<int>(pitch);

  if (direction > 0) {
    for (int oct = -1; oct <= 1; ++oct) {
      for (int deg = 0; deg < 7; ++deg) {
        int candidate = (pitch_val / 12) * 12 + root_pc + scale[deg] + oct * 12;
        if (candidate > pitch_val) {
          return clampPitch(candidate, kSopranoLow, kSopranoHigh);
        }
      }
    }
    return clampPitch(pitch_val + 2, kSopranoLow, kSopranoHigh);
  }

  // Descending.
  for (int oct = 1; oct >= -1; --oct) {
    for (int deg = 6; deg >= 0; --deg) {
      int candidate = (pitch_val / 12) * 12 + root_pc + scale[deg] + oct * 12;
      if (candidate < pitch_val) {
        return clampPitch(candidate, kSopranoLow, kSopranoHigh);
      }
    }
  }
  return clampPitch(pitch_val - 2, kSopranoLow, kSopranoHigh);
}

// ---------------------------------------------------------------------------
// Chord tone helpers
// ---------------------------------------------------------------------------

/// Build a Chord from grid bar info for use with collectChordTonesInRange.
/// Root pitch is derived from chord degree + tonic (not bass, which may be inverted).
Chord chordFromBar(const StructuralBarInfo& bar, Key tonic) {
  Chord chord;
  chord.degree = bar.chord_degree;
  chord.quality = majorKeyQuality(bar.chord_degree);
  // Root pitch from degree semitone offset + tonic pitch class.
  uint8_t semi = degreeSemitones(bar.chord_degree);
  chord.root_pitch = static_cast<uint8_t>((static_cast<int>(tonic) + semi) % 12);
  chord.inversion = 0;
  return chord;
}

bool isChordTone(uint8_t pitch, const std::vector<uint8_t>& chord_tones) {
  for (uint8_t ct : chord_tones) {
    if (getPitchClass(pitch) == getPitchClass(ct)) return true;
  }
  return false;
}

// ---------------------------------------------------------------------------
// Phase A: Phrase contour → register targets
// ---------------------------------------------------------------------------

struct RegisterTargets {
  std::array<uint8_t, 32> targets;
};

RegisterTargets computeRegisterTargets(const GoldbergStructuralGrid& grid,
                                       std::mt19937& rng) {
  RegisterTargets result{};
  std::uniform_int_distribution<int> jitter(-2, 2);

  for (int bar = 0; bar < 32; ++bar) {
    const auto& info = grid.getBar(bar);
    float arc = 0.0f;
    switch (info.phrase_pos) {
      case PhrasePosition::Opening: arc = kArcOpening; break;
      case PhrasePosition::Expansion: arc = kArcExpansion; break;
      case PhrasePosition::Intensification: arc = kArcIntensification; break;
      case PhrasePosition::Cadence: arc = kArcCadence; break;
    }

    float melodic_tension = info.tension.melodic;
    float target_f = static_cast<float>(kSopranoCenter) +
                     arc * melodic_tension *
                         static_cast<float>(kSopranoHigh - kSopranoCenter);

    int target = static_cast<int>(target_f) + jitter(rng);

    // Cadence bars: pull toward tonic (G5=79) or dominant (D5=74).
    if (info.cadence.has_value()) {
      if (info.cadence.value() == CadenceType::Perfect) {
        target = 79 + jitter(rng);  // G5 neighborhood.
      } else {
        target = 74 + jitter(rng);  // D5 neighborhood.
      }
    }

    result.targets[static_cast<size_t>(bar)] =
        clampPitch(target, kSopranoLow, kSopranoHigh);
  }
  return result;
}

// ---------------------------------------------------------------------------
// Phase B: Kern layer — Beat 1 chord tone selection
// ---------------------------------------------------------------------------

struct KernScore {
  float step_bonus;
  float direction_bonus;
  float register_bonus;
  float variety_bonus;
  float cadence_bonus;

  float total() const {
    return kStepWeight * step_bonus + kDirectionWeight * direction_bonus +
           kRegisterWeight * register_bonus + kVarietyWeight * variety_bonus +
           kCadenceWeight * cadence_bonus;
  }
};

float computeStepBonus(uint8_t candidate, uint8_t prev_pitch) {
  if (prev_pitch == 0) return 0.5f;
  int interval = absoluteInterval(candidate, prev_pitch);
  if (interval <= 2) return 1.0f;   // 2nd
  if (interval <= 4) return 0.5f;   // 3rd
  if (interval <= 7) return -0.2f;  // 4th-5th
  if (interval == 0) return -0.4f;  // unison
  return -1.0f;                     // > 5th
}

float computeDirectionBonus(uint8_t candidate, uint8_t prev_pitch,
                            uint8_t target) {
  if (prev_pitch == 0) return 0.0f;
  int target_dir = (target > prev_pitch) ? 1 : ((target < prev_pitch) ? -1 : 0);
  int actual_dir =
      (candidate > prev_pitch) ? 1 : ((candidate < prev_pitch) ? -1 : 0);
  return (target_dir == actual_dir) ? 1.0f : -0.3f;
}

float computeRegisterBonus(uint8_t candidate, uint8_t target) {
  int dist = absoluteInterval(candidate, target);
  if (dist <= 3) return 1.0f;
  if (dist <= 6) return 0.5f;
  if (dist <= 9) return 0.0f;
  return -0.5f;
}

float computeVarietyBonus(uint8_t candidate,
                          const uint8_t* recent_downbeats, int count) {
  for (int idx = 0; idx < count; ++idx) {
    if (recent_downbeats[idx] == candidate) return -0.5f;
  }
  return 0.2f;
}

float computeCadenceBonus(uint8_t candidate,
                          const StructuralBarInfo& bar_info) {
  if (!bar_info.cadence.has_value()) return 0.0f;
  int pc = getPitchClass(candidate);
  if (bar_info.cadence.value() == CadenceType::Perfect) {
    return (pc == kTonicPC) ? 1.0f : -0.3f;
  }
  // Half cadence.
  return (pc == kDominantPC) ? 1.0f : -0.3f;
}

uint8_t selectKernPitch(const std::vector<uint8_t>& chord_tones,
                        uint8_t prev_pitch, uint8_t register_target,
                        const uint8_t* recent_downbeats, int recent_count,
                        const StructuralBarInfo& bar_info,
                        std::mt19937& rng) {
  if (chord_tones.empty()) return kSopranoCenter;

  // Score each candidate.
  std::vector<float> scores(chord_tones.size());
  for (size_t idx = 0; idx < chord_tones.size(); ++idx) {
    KernScore ks;
    ks.step_bonus = computeStepBonus(chord_tones[idx], prev_pitch);
    ks.direction_bonus =
        computeDirectionBonus(chord_tones[idx], prev_pitch, register_target);
    ks.register_bonus =
        computeRegisterBonus(chord_tones[idx], register_target);
    ks.variety_bonus =
        computeVarietyBonus(chord_tones[idx], recent_downbeats, recent_count);
    ks.cadence_bonus = computeCadenceBonus(chord_tones[idx], bar_info);
    scores[idx] = ks.total();
  }

  // Softmax selection.
  float max_score = *std::max_element(scores.begin(), scores.end());
  std::vector<float> weights(scores.size());
  float sum = 0.0f;
  for (size_t idx = 0; idx < scores.size(); ++idx) {
    weights[idx] = std::exp((scores[idx] - max_score) / kSoftmaxTemp);
    sum += weights[idx];
  }

  std::uniform_real_distribution<float> dist(0.0f, sum);
  float pick = dist(rng);
  float cumulative = 0.0f;
  for (size_t idx = 0; idx < weights.size(); ++idx) {
    cumulative += weights[idx];
    if (pick <= cumulative) return chord_tones[idx];
  }
  return chord_tones.back();
}

// ---------------------------------------------------------------------------
// Phase C: Surface layer — Beat 2 (BeatFunction selection)
// ---------------------------------------------------------------------------

struct BeatFuncProb {
  float stable;
  float suspension43;
  float appoggiatura;
  float hold;
};

BeatFuncProb getBeatFuncProb(PhrasePosition pos, bool is_cadence,
                             CadenceType cad_type) {
  if (is_cadence) {
    if (cad_type == CadenceType::Perfect) {
      return {0.25f, 0.55f, 0.15f, 0.05f};
    }
    // Half cadence.
    return {0.65f, 0.15f, 0.0f, 0.20f};
  }

  switch (pos) {
    case PhrasePosition::Opening:
      return {0.40f, 0.25f, 0.20f, 0.15f};
    case PhrasePosition::Expansion:
      return {0.30f, 0.25f, 0.25f, 0.20f};
    case PhrasePosition::Intensification:
      return {0.20f, 0.35f, 0.30f, 0.15f};
    case PhrasePosition::Cadence:
      return {0.50f, 0.30f, 0.10f, 0.10f};
  }
  return {0.40f, 0.25f, 0.20f, 0.15f};
}

BeatFunction selectBeatFunction(const BeatFuncProb& prob,
                                std::mt19937& rng) {
  std::uniform_real_distribution<float> dist(0.0f, 1.0f);
  float pick = dist(rng);
  float cum = prob.stable;
  if (pick < cum) return BeatFunction::Stable;
  cum += prob.suspension43;
  if (pick < cum) return BeatFunction::Suspension43;
  cum += prob.appoggiatura;
  if (pick < cum) return BeatFunction::Appoggiatura;
  return BeatFunction::Hold;
}

/// Compute Beat 2 pitch based on function.
/// Returns {pitch, func} where func may be downgraded to Stable on validation failure.
AriaThemeBeat computeBeat2(BeatFunction func, uint8_t beat1_pitch,
                           const std::vector<uint8_t>& chord_tones,
                           const KeySignature& key,
                           std::mt19937& rng) {
  switch (func) {
    case BeatFunction::Suspension43: {
      // Suspension: hold beat1 pitch, will resolve on beat 3.
      // Validation: resolution (scaleNeighbor down) should be a chord tone.
      uint8_t resolution = scaleNeighbor(beat1_pitch, -1, key);
      if (isChordTone(resolution, chord_tones) ||
          getPitchClass(resolution) == getPitchClass(beat1_pitch)) {
        return {beat1_pitch, BeatFunction::Suspension43};
      }
      // Fallback to Stable.
      break;
    }
    case BeatFunction::Appoggiatura: {
      // Appoggiatura: non-chord tone ±1 scale step from nearest chord tone.
      std::uniform_int_distribution<int> dir_dist(0, 1);
      int dir = dir_dist(rng) == 0 ? 1 : -1;
      uint8_t nct = nearestChordTone(beat1_pitch, chord_tones);
      uint8_t app_pitch = scaleNeighbor(nct, dir, key);
      // Verify resolution: opposite step should be a chord tone.
      uint8_t resolution = scaleNeighbor(app_pitch, -dir, key);
      if (isChordTone(resolution, chord_tones) && !isChordTone(app_pitch, chord_tones)) {
        return {app_pitch, BeatFunction::Appoggiatura};
      }
      // Try opposite direction.
      app_pitch = scaleNeighbor(nct, -dir, key);
      resolution = scaleNeighbor(app_pitch, dir, key);
      if (isChordTone(resolution, chord_tones) && !isChordTone(app_pitch, chord_tones)) {
        return {app_pitch, BeatFunction::Appoggiatura};
      }
      // Fallback to Stable.
      break;
    }
    case BeatFunction::Hold:
      return {0, BeatFunction::Hold};
    default:
      break;
  }

  // Stable: nearest chord tone different from beat 1 (prefer stepwise).
  uint8_t best = beat1_pitch;
  int best_dist = 999;
  for (uint8_t ct : chord_tones) {
    int dist = absoluteInterval(ct, beat1_pitch);
    if (dist > 0 && dist < best_dist) {
      best_dist = dist;
      best = ct;
    }
  }
  // If no different chord tone found, keep beat1.
  if (best == beat1_pitch && !chord_tones.empty()) {
    best = chord_tones[0];
  }
  return {best, BeatFunction::Stable};
}

// ---------------------------------------------------------------------------
// Phase D: Beat 3 (resolution / passing)
// ---------------------------------------------------------------------------

AriaThemeBeat computeBeat3(BeatFunction beat2_func, uint8_t beat2_pitch,
                           uint8_t beat1_pitch,
                           uint8_t next_bar_target,
                           const std::vector<uint8_t>& chord_tones,
                           const KeySignature& key,
                           const StructuralBarInfo& bar_info,
                           std::mt19937& rng) {
  switch (beat2_func) {
    case BeatFunction::Suspension43:
    case BeatFunction::Appoggiatura: {
      // Must resolve stepwise down.
      uint8_t resolved = scaleNeighbor(beat2_pitch, -1, key);
      return {resolved, BeatFunction::Stable};
    }
    case BeatFunction::Hold: {
      // Passing or stable.
      std::uniform_real_distribution<float> dist(0.0f, 1.0f);
      if (dist(rng) < 0.6f) {
        // Passing toward next bar target.
        int dir = (next_bar_target > beat1_pitch) ? 1 : -1;
        uint8_t passing = scaleNeighbor(beat1_pitch, dir, key);
        // Cadence bar Beat 3: bias toward tonic/dominant.
        if (bar_info.cadence.has_value()) {
          passing = nearestChordTone(passing, chord_tones);
        }
        return {passing, BeatFunction::Passing};
      }
      return {nearestChordTone(beat1_pitch, chord_tones), BeatFunction::Stable};
    }
    default: {
      // Stable beat 2 → passing(70%) or stable(30%).
      std::uniform_real_distribution<float> dist(0.0f, 1.0f);
      if (dist(rng) < 0.7f) {
        int dir = (next_bar_target > beat2_pitch) ? 1 : -1;
        uint8_t passing = scaleNeighbor(beat2_pitch, dir, key);
        // Cadence bar Beat 3: pull toward tonic/dominant.
        if (bar_info.cadence.has_value()) {
          int pc = getPitchClass(passing);
          bool is_good = (bar_info.cadence.value() == CadenceType::Perfect)
                             ? (pc == kTonicPC)
                             : (pc == kDominantPC);
          if (!is_good) {
            // Add +0.5 register bias toward cadence target.
            uint8_t target = (bar_info.cadence.value() == CadenceType::Perfect)
                                 ? static_cast<uint8_t>(79)  // G5
                                 : static_cast<uint8_t>(74); // D5
            int d = (target > beat2_pitch) ? 1 : -1;
            passing = scaleNeighbor(beat2_pitch, d, key);
          }
        }
        return {passing, BeatFunction::Passing};
      }
      return {nearestChordTone(beat2_pitch, chord_tones), BeatFunction::Stable};
    }
  }
}

// ---------------------------------------------------------------------------
// Phase E: Post-processing (validateAndClamp)
// ---------------------------------------------------------------------------

void validateAndClamp(AriaTheme& theme, const GoldbergStructuralGrid& grid,
                      const KeySignature& key) {
  // 1. Clamp all pitches to soprano range.
  for (auto& beat : theme.beats) {
    if (beat.pitch > 0) {
      beat.pitch = clampPitch(static_cast<int>(beat.pitch),
                              kSopranoLow, kSopranoHigh);
    }
  }

  // 2. Fix excessive leaps between adjacent sounding pitches.
  //    Skip suspension/appoggiatura and their resolutions to preserve voice-leading.
  uint8_t prev_sounding = 0;
  for (int idx = 0; idx < AriaTheme::kTotalBeats; ++idx) {
    auto& beat = theme.beats[static_cast<size_t>(idx)];
    if (beat.pitch == 0) continue;

    // Don't modify suspension/appoggiatura resolutions.
    bool is_protected = false;
    if (idx > 0) {
      auto prev_func = theme.beats[static_cast<size_t>(idx - 1)].func;
      if (prev_func == BeatFunction::Suspension43 ||
          prev_func == BeatFunction::Appoggiatura) {
        is_protected = true;
      }
    }

    if (!is_protected && prev_sounding > 0 &&
        absoluteInterval(beat.pitch, prev_sounding) > kMaxLeap) {
      int dir = (beat.pitch > prev_sounding) ? -1 : 1;
      beat.pitch = scaleNeighbor(prev_sounding, -dir, key);
      beat.pitch = clampPitch(static_cast<int>(beat.pitch),
                              kSopranoLow, kSopranoHigh);
    }

    prev_sounding = beat.pitch;
  }

  // 3. Consonant ratio: each bar must have ≥2/3 chord tone beats.
  for (int bar = 0; bar < 32; ++bar) {
    const auto& bar_info = grid.getBar(bar);
    Chord chord = chordFromBar(bar_info, key.tonic);
    auto chord_tones = collectChordTonesInRange(chord, kSopranoLow, kSopranoHigh);
    if (chord_tones.empty()) continue;

    int ct_count = 0;
    int non_hold_count = 0;
    for (int beat = 0; beat < 3; ++beat) {
      auto& b = theme.beats[static_cast<size_t>(bar * 3 + beat)];
      if (b.pitch == 0) continue;
      ++non_hold_count;
      if (isChordTone(b.pitch, chord_tones)) ++ct_count;
    }

    if (non_hold_count > 0 &&
        static_cast<float>(ct_count) / static_cast<float>(non_hold_count) <
            kMinConsonantRatio) {
      // Snap the non-chord-tone beat furthest from any chord tone.
      // Skip beats that are suspension/appoggiatura or their resolutions.
      int worst_beat = -1;
      int worst_dist = 0;
      for (int beat = 0; beat < 3; ++beat) {
        auto& b = theme.beats[static_cast<size_t>(bar * 3 + beat)];
        if (b.pitch == 0) continue;
        // Protect non-chord tones that serve as suspensions/appoggiaturas.
        if (b.func == BeatFunction::Suspension43 ||
            b.func == BeatFunction::Appoggiatura) continue;
        // Protect resolution beats (beat after suspension/appoggiatura).
        if (beat > 0) {
          auto prev_func = theme.beats[static_cast<size_t>(bar * 3 + beat - 1)].func;
          if (prev_func == BeatFunction::Suspension43 ||
              prev_func == BeatFunction::Appoggiatura) continue;
        }
        if (!isChordTone(b.pitch, chord_tones)) {
          int dist = absoluteInterval(b.pitch,
                                      nearestChordTone(b.pitch, chord_tones));
          if (dist > worst_dist) {
            worst_dist = dist;
            worst_beat = beat;
          }
        }
      }
      if (worst_beat >= 0) {
        auto& b = theme.beats[static_cast<size_t>(bar * 3 + worst_beat)];
        b.pitch = nearestChordTone(b.pitch, chord_tones);
      }
    }
  }

  // 4. Beat 1 final snap: ensure all Beat 1 pitches are chord tones.
  for (int bar = 0; bar < 32; ++bar) {
    auto& b1 = theme.beats[static_cast<size_t>(bar * 3)];
    const auto& bar_info = grid.getBar(bar);
    Chord chord = chordFromBar(bar_info, key.tonic);
    auto chord_tones = collectChordTonesInRange(chord, kSopranoLow, kSopranoHigh);
    if (!chord_tones.empty() && !isChordTone(b1.pitch, chord_tones)) {
      b1.pitch = nearestChordTone(b1.pitch, chord_tones);
    }
  }

  // 5. Re-check leaps after Beat 1 snapping (protect resolutions).
  prev_sounding = 0;
  for (int idx = 0; idx < AriaTheme::kTotalBeats; ++idx) {
    auto& beat = theme.beats[static_cast<size_t>(idx)];
    if (beat.pitch == 0) continue;

    bool is_protected = false;
    if (idx > 0) {
      auto prev_func = theme.beats[static_cast<size_t>(idx - 1)].func;
      if (prev_func == BeatFunction::Suspension43 ||
          prev_func == BeatFunction::Appoggiatura) {
        is_protected = true;
      }
    }

    if (!is_protected && prev_sounding > 0 &&
        absoluteInterval(beat.pitch, prev_sounding) > kMaxLeap) {
      int dir = (beat.pitch > prev_sounding) ? -1 : 1;
      beat.pitch = scaleNeighbor(prev_sounding, -dir, key);
      beat.pitch = clampPitch(static_cast<int>(beat.pitch),
                              kSopranoLow, kSopranoHigh);
    }
    prev_sounding = beat.pitch;
  }

  // 6. Normalize Hold beats to pitch=0.
  for (auto& beat : theme.beats) {
    if (beat.func == BeatFunction::Hold) {
      beat.pitch = 0;
    }
  }
}

}  // namespace

// ---------------------------------------------------------------------------
// AriaTheme accessors
// ---------------------------------------------------------------------------

uint8_t AriaTheme::getPitch(int bar, int beat) const {
  int idx = std::clamp(bar, 0, kBars - 1) * kBeatsPerBar +
            std::clamp(beat, 0, kBeatsPerBar - 1);

  if (beats[static_cast<size_t>(idx)].pitch > 0) {
    return beats[static_cast<size_t>(idx)].pitch;
  }

  // Walk backwards to find the most recent non-zero pitch.
  for (int scan = idx - 1; scan >= 0; --scan) {
    if (beats[static_cast<size_t>(scan)].pitch > 0) {
      return beats[static_cast<size_t>(scan)].pitch;
    }
  }

  // Fallback: G5 (tonic).
  return 79;
}

BeatFunction AriaTheme::getFunction(int bar, int beat) const {
  int idx = std::clamp(bar, 0, kBars - 1) * kBeatsPerBar +
            std::clamp(beat, 0, kBeatsPerBar - 1);
  return beats[static_cast<size_t>(idx)].func;
}

std::array<uint8_t, 4> AriaTheme::getDownbeatFragment(int start_bar,
                                                        int length_bars) const {
  std::array<uint8_t, 4> result = {0, 0, 0, 0};
  int count = std::min(length_bars, 4);
  for (int idx = 0; idx < count; ++idx) {
    int bar = start_bar + idx;
    if (bar >= 0 && bar < kBars) {
      result[static_cast<size_t>(idx)] = getPitch(bar, 0);
    }
  }
  return result;
}

// ---------------------------------------------------------------------------
// generateAriaMelody — 2-layer generative Sarabande melody
// ---------------------------------------------------------------------------

AriaTheme generateAriaMelody(const GoldbergStructuralGrid& grid,
                             const KeySignature& key,
                             uint32_t seed) {
  std::mt19937 rng(seed);
  AriaTheme theme{};

  // Phase A: Compute register targets from phrase contour.
  auto targets = computeRegisterTargets(grid, rng);

  // Phase B: Kern layer — Beat 1 chord tone selection.
  uint8_t prev_kern = 0;
  uint8_t recent_downbeats[3] = {0, 0, 0};
  int same_pitch_run = 0;

  for (int bar = 0; bar < 32; ++bar) {
    const auto& bar_info = grid.getBar(bar);
    Chord chord = chordFromBar(bar_info, key.tonic);
    auto chord_tones = collectChordTonesInRange(chord, kSopranoLow, kSopranoHigh);

    if (chord_tones.empty()) {
      // Fallback: use center pitch.
      theme.beats[static_cast<size_t>(bar * 3)] = {kSopranoCenter,
                                                    BeatFunction::Stable};
      prev_kern = kSopranoCenter;
      continue;
    }

    int recent_count = std::min(bar, 3);

    uint8_t kern = selectKernPitch(
        chord_tones, prev_kern, targets.targets[static_cast<size_t>(bar)],
        recent_downbeats, recent_count, bar_info, rng);

    // Safety valve S3: no triple same-pitch downbeat.
    if (kern == prev_kern) {
      ++same_pitch_run;
      if (same_pitch_run >= kMaxSamePitchRun) {
        // Force stepwise movement — try preferred direction first, then opposite.
        uint8_t original_kern = kern;
        int dir = (targets.targets[static_cast<size_t>(bar)] > kern) ? 1 : -1;
        for (int attempt = 0; attempt < 2; ++attempt) {
          int try_dir = (attempt == 0) ? dir : -dir;
          uint8_t stepped = scaleNeighbor(original_kern, try_dir, key);
          stepped = clampPitch(static_cast<int>(stepped), kSopranoLow, kSopranoHigh);
          if (isChordTone(stepped, chord_tones) && stepped != original_kern) {
            kern = stepped;
            break;
          }
          uint8_t snapped = nearestChordTone(stepped, chord_tones);
          if (snapped != original_kern) {
            kern = snapped;
            break;
          }
        }
        // Last resort: pick any different chord tone.
        if (kern == original_kern) {
          for (uint8_t ct : chord_tones) {
            if (ct != original_kern) {
              kern = ct;
              break;
            }
          }
        }
        same_pitch_run = 0;
      }
    } else {
      same_pitch_run = 0;
    }

    theme.beats[static_cast<size_t>(bar * 3)] = {kern, BeatFunction::Stable};

    // Shift recent downbeats.
    recent_downbeats[2] = recent_downbeats[1];
    recent_downbeats[1] = recent_downbeats[0];
    recent_downbeats[0] = kern;
    prev_kern = kern;
  }

  // Phase C+D: Surface layer — Beat 2 and Beat 3.
  for (int bar = 0; bar < 32; ++bar) {
    const auto& bar_info = grid.getBar(bar);
    Chord chord = chordFromBar(bar_info, key.tonic);
    auto chord_tones = collectChordTonesInRange(chord, kSopranoLow, kSopranoHigh);
    if (chord_tones.empty()) {
      chord_tones.push_back(kSopranoCenter);
    }

    uint8_t beat1_pitch = theme.beats[static_cast<size_t>(bar * 3)].pitch;

    // Beat 2: select function and compute pitch.
    bool is_cadence = bar_info.cadence.has_value();
    CadenceType cad_type = is_cadence ? bar_info.cadence.value() : CadenceType::Perfect;
    BeatFuncProb prob = getBeatFuncProb(bar_info.phrase_pos, is_cadence, cad_type);
    BeatFunction beat2_func = selectBeatFunction(prob, rng);

    AriaThemeBeat beat2 = computeBeat2(beat2_func, beat1_pitch, chord_tones, key, rng);
    theme.beats[static_cast<size_t>(bar * 3 + 1)] = beat2;

    // Beat 3: resolution based on beat 2 function.
    uint8_t next_bar_target = (bar < 31)
        ? targets.targets[static_cast<size_t>(bar + 1)]
        : static_cast<uint8_t>(79);  // Final bar → G5.

    uint8_t beat2_sounding = (beat2.pitch > 0) ? beat2.pitch : beat1_pitch;
    AriaThemeBeat beat3 = computeBeat3(
        beat2.func, beat2_sounding, beat1_pitch, next_bar_target,
        chord_tones, key, bar_info, rng);
    theme.beats[static_cast<size_t>(bar * 3 + 2)] = beat3;
  }

  // Phase E: Post-processing.
  validateAndClamp(theme, grid, key);

  return theme;
}

}  // namespace bach
