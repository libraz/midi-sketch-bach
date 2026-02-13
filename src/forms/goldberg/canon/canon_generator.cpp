// 3-voice canon generator for Goldberg Variations.
//
// Implements the 5-phase beat-by-beat forward generation algorithm:
//   Phase 0: Setup (DuxBuffer, soggetto skeleton, scale candidates).
//   Phase 1: Dux-only period (beat 0 to delay_beats - 1).
//   Phase 2: Three-voice canon (beat delay_beats to total_beats - 1).
//   Phase 3: Comes tail (derive remaining comes after dux stops).

#include "forms/goldberg/canon/canon_generator.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <random>

#include "core/interval.h"
#include "core/note_creator.h"
#include "core/pitch_utils.h"
#include "core/scale.h"
#include "fugue/subject.h"

namespace bach {

namespace {

/// Dux register range (approximately C4-C6).
constexpr uint8_t kDuxLow = 60;
constexpr uint8_t kDuxHigh = 84;

/// Bass register range (approximately C2-C4).
constexpr uint8_t kBassLow = 36;
constexpr uint8_t kBassHigh = 60;

/// Minimum acceptable score threshold for candidate selection.
/// If all candidates score below this, backtracking is triggered.
constexpr float kBacktrackThreshold = -5.0f;

/// Maximum backtracks per 4-bar phrase.
constexpr int kMaxBacktracksPerPhrase = 2;

/// Maximum beats to backtrack at once.
constexpr int kMaxBacktrackBeats = 3;

/// @brief Determine soggetto character from canon interval.
///
/// Small intervals (unison, 2nd) get Severe character for strict contrapuntal
/// feel. Larger intervals (5th+) get Noble character for broader melodic sweep.
///
/// @param canon_interval Diatonic interval (0-8).
/// @return SubjectCharacter matching the interval's expressive range.
SubjectCharacter characterForInterval(int canon_interval) {
  if (canon_interval <= 1) return SubjectCharacter::Severe;
  if (canon_interval <= 3) return SubjectCharacter::Playful;
  return SubjectCharacter::Noble;
}

/// @brief Get ScaleType for a key signature and minor profile.
/// @param key Key signature.
/// @param profile Minor mode profile (used only for minor keys).
/// @return Appropriate ScaleType.
ScaleType getScaleForKey(const KeySignature& key, MinorModeProfile profile) {
  if (!key.is_minor) return ScaleType::Major;
  switch (profile) {
    case MinorModeProfile::NaturalMinor:
      return ScaleType::NaturalMinor;
    case MinorModeProfile::HarmonicMinor:
      return ScaleType::HarmonicMinor;
    case MinorModeProfile::MixedBaroqueMinor:
      return ScaleType::NaturalMinor;  // Base scale; alterations at note level.
  }
  return ScaleType::NaturalMinor;
}

}  // namespace

// ---------------------------------------------------------------------------
// Public: generate
// ---------------------------------------------------------------------------

CanonResult CanonGenerator::generate(const CanonSpec& spec,
                                     const GoldbergStructuralGrid& grid,
                                     const TimeSignature& time_sig,
                                     uint32_t seed) const {
  CanonResult result;
  result.success = false;

  // --- Phase 0: Setup ---
  std::mt19937 rng(seed);
  DuxBuffer buffer(spec, time_sig);

  int beats_per_bar = static_cast<int>(time_sig.beatsPerBar());
  int total_bars = 32;
  int total_beats = total_bars * beats_per_bar;
  int delay_beats = buffer.delayBeats();
  Tick ticks_per_beat = time_sig.ticksPerBar() / time_sig.beatsPerBar();

  SubjectCharacter character = characterForInterval(spec.canon_interval);
  int max_leap = maxLeapForCharacter(character);
  ScaleType scale = getScaleForKey(spec.key, spec.minor_profile);

  // Generate a 2-bar soggetto skeleton for the opening dux phrase.
  SoggettoGenerator soggetto_gen;
  SoggettoParams soggetto_params;
  soggetto_params.length_bars = 2;
  soggetto_params.character = character;
  soggetto_params.grid = &grid;
  soggetto_params.start_bar = 1;  // 1-based.
  soggetto_params.path_candidates = 8;

  Subject soggetto = soggetto_gen.generate(soggetto_params, spec.key, time_sig, seed);

  // Determine starting pitch from soggetto, or fallback to tonic.
  uint8_t start_pitch = tonicPitch(spec.key.tonic, 4);  // G4=67 for G major.
  if (!soggetto.notes.empty()) {
    start_pitch = soggetto.notes[0].pitch;
    // Ensure start pitch is within dux register.
    start_pitch = clampPitch(static_cast<int>(start_pitch), kDuxLow, kDuxHigh);
  }

  // Reserve space.
  result.dux_notes.reserve(static_cast<size_t>(total_beats));
  result.comes_notes.reserve(static_cast<size_t>(total_beats));
  result.bass_notes.reserve(static_cast<size_t>(total_beats));

  uint8_t prev_dux_pitch = start_pitch;
  int backtrack_count = 0;

  // Track phrase-level backtracks (reset every 4 bars).
  int phrase_backtracks = 0;
  int current_phrase_start = 0;

  // --- Phase 1: Dux-only (beat 0 to delay_beats - 1) ---
  for (int beat = 0; beat < delay_beats && beat < total_beats; ++beat) {
    int bar = beat / beats_per_bar;
    int beat_in_bar = beat % beats_per_bar;
    const auto& bar_info = grid.getBar(bar);
    MetricalStrength strength = getMetricalStrength(beat_in_bar,
                                                     MeterProfile::StandardTriple);

    uint8_t dux_pitch;

    // Use soggetto skeleton for guidance in the opening phrase.
    size_t soggetto_idx = static_cast<size_t>(beat);
    if (soggetto_idx < soggetto.notes.size()) {
      dux_pitch = soggetto.notes[soggetto_idx].pitch;
      dux_pitch = clampPitch(static_cast<int>(dux_pitch), kDuxLow, kDuxHigh);
      // Snap to scale for safety.
      dux_pitch = scale_util::nearestScaleTone(dux_pitch, spec.key.tonic, scale);
    } else {
      // Generate from candidates.
      auto candidates = buildCandidates(prev_dux_pitch, spec.key,
                                        spec.key.is_minor, max_leap);
      if (candidates.empty()) {
        // Fallback: use previous pitch.
        dux_pitch = prev_dux_pitch;
      } else {
        dux_pitch = selectBestDuxPitch(candidates, prev_dux_pitch, buffer,
                                       beat, grid, bar_info, strength,
                                       0, 0, character, rng);
      }
    }

    // Create dux note via createBachNote.
    BachNoteOptions opts;
    opts.voice = 0;
    opts.desired_pitch = dux_pitch;
    opts.tick = static_cast<Tick>(beat) * ticks_per_beat;
    opts.duration = ticks_per_beat;
    opts.velocity = 80;
    opts.source = BachNoteSource::CanonDux;

    auto note_result = createBachNote(nullptr, nullptr, nullptr, opts);
    NoteEvent dux_note = note_result.note;
    dux_note.pitch = dux_pitch;  // Phase 0: no adjustment.
    dux_note.source = BachNoteSource::CanonDux;

    buffer.recordDux(beat, dux_note);
    result.dux_notes.push_back(dux_note);
    prev_dux_pitch = dux_pitch;
  }

  // --- Phase 2: Three-voice canon (beat delay_beats to total_beats - 1) ---
  for (int beat = delay_beats; beat < total_beats; ++beat) {
    int bar = beat / beats_per_bar;
    int beat_in_bar = beat % beats_per_bar;
    const auto& bar_info = grid.getBar(bar);
    MetricalStrength strength = getMetricalStrength(beat_in_bar,
                                                     MeterProfile::StandardTriple);

    // Reset phrase backtrack counter at phrase boundaries.
    int phrase_idx = bar / 4;
    if (phrase_idx != current_phrase_start / 4) {
      phrase_backtracks = 0;
      current_phrase_start = bar * beats_per_bar;
    }

    // (A) Comes derivation: deterministic from past dux.
    uint8_t comes_pitch = 0;
    auto comes_opt = buffer.deriveComes(beat);
    if (comes_opt.has_value()) {
      result.comes_notes.push_back(comes_opt.value());
      comes_pitch = comes_opt->pitch;
    }

    // (B) Dux generation with scoring.
    auto candidates = buildCandidates(prev_dux_pitch, spec.key,
                                      spec.key.is_minor, max_leap);

    uint8_t bass_pitch = 0;
    if (!result.bass_notes.empty()) {
      bass_pitch = result.bass_notes.back().pitch;
    }

    uint8_t dux_pitch;
    if (candidates.empty()) {
      dux_pitch = prev_dux_pitch;
    } else {
      dux_pitch = selectBestDuxPitch(candidates, prev_dux_pitch, buffer,
                                     beat, grid, bar_info, strength,
                                     comes_pitch, bass_pitch, character,
                                     rng);

      // Check if best candidate is below backtrack threshold.
      float best_score = scoreDuxCandidate(dux_pitch, prev_dux_pitch, buffer,
                                           beat, grid, bar_info, strength,
                                           comes_pitch, bass_pitch, character);

      if (best_score < kBacktrackThreshold &&
          phrase_backtracks < kMaxBacktracksPerPhrase) {
        // Local backtrack: erase last few beats and retry with different RNG.
        int backtrack_beats = std::min(kMaxBacktrackBeats,
                                       beat - delay_beats);
        if (backtrack_beats > 0) {
          // Remove last backtrack_beats dux notes.
          for (int idx = 0; idx < backtrack_beats &&
                            !result.dux_notes.empty(); ++idx) {
            result.dux_notes.pop_back();
          }
          // Remove corresponding comes notes.
          for (int idx = 0; idx < backtrack_beats &&
                            !result.comes_notes.empty(); ++idx) {
            auto& last = result.comes_notes.back();
            // Only remove if it was derived after the backtrack point.
            if (last.start_tick >= static_cast<Tick>(beat - backtrack_beats) *
                                       ticks_per_beat) {
              result.comes_notes.pop_back();
            }
          }
          // Remove bass notes in the backtrack window.
          while (!result.bass_notes.empty() &&
                 result.bass_notes.back().start_tick >=
                     static_cast<Tick>(beat - backtrack_beats) * ticks_per_beat) {
            result.bass_notes.pop_back();
          }

          // Rewind beat counter and prev_dux_pitch.
          beat -= backtrack_beats;
          prev_dux_pitch = result.dux_notes.empty()
                               ? start_pitch
                               : result.dux_notes.back().pitch;

          backtrack_count++;
          phrase_backtracks++;

          // Advance RNG to get different results on retry.
          rng.discard(7);

          continue;  // Re-enter the loop at the earlier beat.
        }
      }
    }

    // Create and record dux note.
    BachNoteOptions dux_opts;
    dux_opts.voice = 0;
    dux_opts.desired_pitch = dux_pitch;
    dux_opts.tick = static_cast<Tick>(beat) * ticks_per_beat;
    dux_opts.duration = ticks_per_beat;
    dux_opts.velocity = 80;
    dux_opts.source = BachNoteSource::CanonDux;

    auto dux_result = createBachNote(nullptr, nullptr, nullptr, dux_opts);
    NoteEvent dux_note = dux_result.note;
    dux_note.pitch = dux_pitch;
    dux_note.source = BachNoteSource::CanonDux;

    buffer.recordDux(beat, dux_note);
    result.dux_notes.push_back(dux_note);
    prev_dux_pitch = dux_pitch;

    // (C) Free bass generation: on strong beats only (beat 1 of each bar).
    if (beat_in_bar == 0) {
      NoteEvent bass_note = generateBassNote(
          bar_info,
          static_cast<Tick>(beat) * ticks_per_beat,
          static_cast<Tick>(beats_per_bar) * ticks_per_beat,  // One bar duration.
          spec.key,
          dux_pitch,
          comes_pitch,
          rng);
      result.bass_notes.push_back(bass_note);
    }
  }

  // --- Phase 3: Comes tail (derive remaining comes after dux stops) ---
  // The last delay_beats of dux produce comes notes that extend beyond
  // the dux's final beat.
  for (int beat = total_beats; beat < total_beats + delay_beats; ++beat) {
    auto comes_opt = buffer.deriveComes(beat);
    if (comes_opt.has_value()) {
      result.comes_notes.push_back(comes_opt.value());
    }
  }

  result.backtrack_count = backtrack_count;
  result.success = !result.dux_notes.empty() && !result.comes_notes.empty();
  return result;
}

// ---------------------------------------------------------------------------
// Private: scoreDuxCandidate
// ---------------------------------------------------------------------------

float CanonGenerator::scoreDuxCandidate(uint8_t candidate,
                                        uint8_t prev_dux_pitch,
                                        const DuxBuffer& buffer,
                                        int current_beat,
                                        const GoldbergStructuralGrid& grid,
                                        const StructuralBarInfo& bar_info,
                                        MetricalStrength strength,
                                        uint8_t comes_pitch,
                                        uint8_t bass_pitch,
                                        SubjectCharacter character) const {
  float score = 0.0f;
  const auto& spec = buffer.spec();

  // 1. Consonance with comes.
  if (comes_pitch > 0) {
    int interval_with_comes = interval_util::compoundToSimple(
        std::abs(static_cast<int>(candidate) - static_cast<int>(comes_pitch)));

    if (interval_util::isConsonance(interval_with_comes)) {
      score += 2.0f;
      if (strength == MetricalStrength::Strong &&
          interval_util::isPerfectConsonance(interval_with_comes)) {
        score += 1.0f;
      }
    } else if (strength == MetricalStrength::Strong) {
      score -= CanonMetricalRules::kUnpreparedDissonancePenalty;
    } else if (strength == MetricalStrength::Medium) {
      score -= CanonMetricalRules::kAccentedPassingPenalty;
    } else {
      score -= CanonMetricalRules::kSuspensionPenalty;
    }
  }

  // 2. Melodic quality.
  int leap = std::abs(static_cast<int>(candidate) -
                      static_cast<int>(prev_dux_pitch));
  int max_leap = maxLeapForCharacter(character);

  if (leap <= 2) {
    score += 1.0f;  // Stepwise motion bonus.
  } else if (leap > max_leap) {
    score -= 1.0f * static_cast<float>(leap - max_leap);
  }

  if (candidate == prev_dux_pitch) {
    score -= 0.5f;  // Repeated pitch penalty.
  }

  // 3. Harmonic alignment: chord tone bonus.
  int pitch_class = getPitchClass(candidate);
  if (isBarChordTone(pitch_class, bar_info, spec.key)) {
    score += (strength == MetricalStrength::Strong) ? 1.5f : 0.5f;
  }

  // 4. Forward constraint: preview future comes consonance.
  if (bass_pitch > 0) {
    uint8_t future_comes = buffer.previewFutureComes(candidate);
    int future_interval = interval_util::compoundToSimple(
        std::abs(static_cast<int>(future_comes) -
                 static_cast<int>(bass_pitch)));
    if (interval_util::isConsonance(future_interval)) {
      score += 0.5f;
    } else {
      score -= 0.5f;
    }
  }

  // 5. Climax alignment.
  score -= buffer.scoreClimaxAlignment(candidate, current_beat, grid);

  // 6. Cadence alignment.
  if (bar_info.cadence.has_value() &&
      bar_info.phrase_pos == PhrasePosition::Cadence) {
    CadenceType cad = bar_info.cadence.value();
    int tonic_pc = static_cast<int>(spec.key.tonic);
    int target_pc;

    switch (cad) {
      case CadenceType::Perfect:
      case CadenceType::Plagal:
      case CadenceType::PicardyThird:
        target_pc = tonic_pc;
        break;
      case CadenceType::Half:
      case CadenceType::Phrygian:
        target_pc = (tonic_pc + 7) % 12;  // Dominant.
        break;
      case CadenceType::Deceptive:
        target_pc = spec.key.is_minor ? (tonic_pc + 8) % 12    // bVI
                                      : (tonic_pc + 9) % 12;   // vi
        break;
    }

    if (pitch_class == target_pc) {
      score += 2.0f;  // Strong cadence alignment bonus.
    } else {
      score -= CanonMetricalRules::kCadenceMisalignPenalty * 0.5f;
    }
  }

  // 7. Consonance with bass.
  if (bass_pitch > 0) {
    int interval_with_bass = interval_util::compoundToSimple(
        std::abs(static_cast<int>(candidate) - static_cast<int>(bass_pitch)));
    if (interval_util::isConsonance(interval_with_bass)) {
      score += 0.5f;
    }
  }

  return score;
}

// ---------------------------------------------------------------------------
// Private: selectBestDuxPitch
// ---------------------------------------------------------------------------

uint8_t CanonGenerator::selectBestDuxPitch(
    const std::vector<uint8_t>& candidates,
    uint8_t prev_dux_pitch,
    const DuxBuffer& buffer,
    int current_beat,
    const GoldbergStructuralGrid& grid,
    const StructuralBarInfo& bar_info,
    MetricalStrength strength,
    uint8_t comes_pitch,
    uint8_t bass_pitch,
    SubjectCharacter character,
    std::mt19937& rng) const {
  if (candidates.empty()) return prev_dux_pitch;

  // Small random perturbation range for tie-breaking and seed-dependent
  // variation. Keeps the scoring deterministic in direction but allows
  // different seeds to produce different melodies when candidates score
  // similarly.
  constexpr float kPerturbationRange = 0.3f;
  std::uniform_real_distribution<float> perturb_dist(0.0f, kPerturbationRange);

  float best_score = -1000.0f;
  uint8_t best_pitch = candidates[0];

  for (uint8_t cand : candidates) {
    float cur_score = scoreDuxCandidate(cand, prev_dux_pitch, buffer,
                                        current_beat, grid, bar_info,
                                        strength, comes_pitch, bass_pitch,
                                        character);
    // Add small random perturbation for seed-dependent variation.
    cur_score += perturb_dist(rng);

    if (cur_score > best_score) {
      best_score = cur_score;
      best_pitch = cand;
    }
  }

  return best_pitch;
}

// ---------------------------------------------------------------------------
// Private: generateBassNote
// ---------------------------------------------------------------------------

NoteEvent CanonGenerator::generateBassNote(const StructuralBarInfo& bar_info,
                                           Tick tick,
                                           Tick dur,
                                           const KeySignature& key,
                                           uint8_t dux_pitch,
                                           uint8_t comes_pitch,
                                           std::mt19937& /*rng*/) const {
  // Use structural bass pitch as the target.
  uint8_t target_pitch = bar_info.bass_motion.primary_pitch;

  // Ensure bass is in bass register (C2-C4).
  int bass_candidate = static_cast<int>(target_pitch);
  while (bass_candidate > kBassHigh) bass_candidate -= 12;
  while (bass_candidate < kBassLow) bass_candidate += 12;

  uint8_t bass_pitch = clampPitch(bass_candidate, kBassLow, kBassHigh);

  // Check consonance with dux and comes; try octave adjustments if dissonant.
  auto check_consonance = [](uint8_t bass, uint8_t other) -> bool {
    if (other == 0) return true;
    int interval = interval_util::compoundToSimple(
        std::abs(static_cast<int>(bass) - static_cast<int>(other)));
    return interval_util::isConsonance(interval);
  };

  // If dissonant with either voice, try adjacent scale tones in bass register.
  if (!check_consonance(bass_pitch, dux_pitch) ||
      !check_consonance(bass_pitch, comes_pitch)) {
    // Try a few scale-adjacent options.
    uint8_t best_bass = bass_pitch;
    float best_score = -100.0f;

    auto scale_tones = getScaleTones(key.tonic, key.is_minor,
                                     kBassLow, kBassHigh);
    for (uint8_t tone : scale_tones) {
      float tone_score = 0.0f;

      // Consonance with dux.
      if (dux_pitch > 0) {
        int interval = interval_util::compoundToSimple(
            std::abs(static_cast<int>(tone) - static_cast<int>(dux_pitch)));
        if (interval_util::isConsonance(interval)) tone_score += 1.0f;
        else tone_score -= 2.0f;
      }

      // Consonance with comes.
      if (comes_pitch > 0) {
        int interval = interval_util::compoundToSimple(
            std::abs(static_cast<int>(tone) - static_cast<int>(comes_pitch)));
        if (interval_util::isConsonance(interval)) tone_score += 1.0f;
        else tone_score -= 2.0f;
      }

      // Proximity to structural bass pitch.
      int dist = std::abs(static_cast<int>(tone) - static_cast<int>(bass_pitch));
      tone_score -= static_cast<float>(dist) * 0.1f;

      if (tone_score > best_score) {
        best_score = tone_score;
        best_bass = tone;
      }
    }

    bass_pitch = best_bass;
  }

  // Use resolution pitch at cadence bars.
  if (bar_info.bass_motion.resolution_pitch.has_value()) {
    // Place resolution pitch in the bass register.
    int res = static_cast<int>(bar_info.bass_motion.resolution_pitch.value());
    while (res > kBassHigh) res -= 12;
    while (res < kBassLow) res += 12;
    // Split duration: primary for first 2/3, resolution for last 1/3.
    // For simplicity, use primary pitch for the full bar.
    // Bass resolution is a structural refinement for future enhancement.
    (void)res;
  }

  // Create the bass note.
  BachNoteOptions bass_opts;
  bass_opts.voice = 2;
  bass_opts.desired_pitch = bass_pitch;
  bass_opts.tick = tick;
  bass_opts.duration = dur;
  bass_opts.velocity = 70;
  bass_opts.source = BachNoteSource::CanonFreeBass;

  auto bass_result = createBachNote(nullptr, nullptr, nullptr, bass_opts);
  NoteEvent note = bass_result.note;
  note.pitch = bass_pitch;
  note.source = BachNoteSource::CanonFreeBass;
  return note;
}

// ---------------------------------------------------------------------------
// Private: isBarChordTone
// ---------------------------------------------------------------------------

bool CanonGenerator::isBarChordTone(int pitch_class,
                                    const StructuralBarInfo& bar_info,
                                    const KeySignature& key) const {
  // Compute root offset from chord degree.
  int root_offset = key.is_minor
                        ? static_cast<int>(degreeMinorSemitones(bar_info.chord_degree))
                        : static_cast<int>(degreeSemitones(bar_info.chord_degree));
  int root_pc = (static_cast<int>(key.tonic) + root_offset) % 12;

  // Determine chord quality.
  ChordQuality quality = key.is_minor
                              ? minorKeyQuality(bar_info.chord_degree)
                              : majorKeyQuality(bar_info.chord_degree);

  // Compute third and fifth offsets.
  int third_offset = 0;
  int fifth_offset = 0;
  switch (quality) {
    case ChordQuality::Major:
    case ChordQuality::Dominant7:
    case ChordQuality::MajorMajor7:
      third_offset = 4;
      fifth_offset = 7;
      break;
    case ChordQuality::Minor:
    case ChordQuality::Minor7:
      third_offset = 3;
      fifth_offset = 7;
      break;
    case ChordQuality::Diminished:
    case ChordQuality::Diminished7:
    case ChordQuality::HalfDiminished7:
      third_offset = 3;
      fifth_offset = 6;
      break;
    case ChordQuality::Augmented:
      third_offset = 4;
      fifth_offset = 8;
      break;
    default:
      third_offset = 4;
      fifth_offset = 7;
      break;
  }

  int third_pc = (root_pc + third_offset) % 12;
  int fifth_pc = (root_pc + fifth_offset) % 12;

  return pitch_class == root_pc || pitch_class == third_pc ||
         pitch_class == fifth_pc;
}

// ---------------------------------------------------------------------------
// Private: buildCandidates
// ---------------------------------------------------------------------------

std::vector<uint8_t> CanonGenerator::buildCandidates(
    uint8_t ref_pitch,
    const KeySignature& key,
    bool is_minor,
    int max_leap) const {
  // Get all scale tones within max_leap of the reference pitch,
  // clamped to the dux register.
  int low = std::max(static_cast<int>(kDuxLow),
                     static_cast<int>(ref_pitch) - max_leap);
  int high = std::min(static_cast<int>(kDuxHigh),
                      static_cast<int>(ref_pitch) + max_leap);

  if (low > high) {
    // Fallback: widen range slightly.
    low = std::max(static_cast<int>(kDuxLow),
                   static_cast<int>(ref_pitch) - 12);
    high = std::min(static_cast<int>(kDuxHigh),
                    static_cast<int>(ref_pitch) + 12);
  }

  auto tones = getScaleTones(key.tonic, is_minor,
                              static_cast<uint8_t>(std::max(0, low)),
                              static_cast<uint8_t>(std::min(127, high)));

  return tones;
}

}  // namespace bach
