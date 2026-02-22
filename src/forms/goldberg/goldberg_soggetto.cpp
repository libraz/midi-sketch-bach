/// @file
/// @brief Soggetto generator for Goldberg Variations Inventio mode.
///
/// Generates short subjects (1-4 bars) aligned to the structural grid.
/// Reuses the fugue pipeline: motif templates, goal tone, N-candidate scoring.

#include "forms/goldberg/goldberg_soggetto.h"

#include <algorithm>
#include <cmath>
#include <random>

#include "core/interval.h"
#include "core/pitch_utils.h"
#include "core/rng_util.h"
#include "core/scale.h"
#include "fugue/motif_template.h"
#include "fugue/subject.h"
#include "fugue/subject_params.h"

namespace bach {

namespace {

/// @brief Determine the target ending pitch class based on CadenceType.
/// @param cadence Cadence type at the ending bar.
/// @param key Key signature.
/// @return Target pitch class (0-11) for the ending note.
int cadenceTargetPitchClass(CadenceType cadence, const KeySignature& key) {
  int tonic_pc = static_cast<int>(key.tonic);
  switch (cadence) {
    case CadenceType::Perfect:
    case CadenceType::Plagal:
    case CadenceType::PicardyThird:
      return tonic_pc;  // End on tonic.
    case CadenceType::Half:
    case CadenceType::Phrygian:
      return (tonic_pc + 7) % 12;  // End on dominant.
    case CadenceType::Deceptive:
      // Deceptive: end on vi (submediant).
      return key.is_minor ? (tonic_pc + 8) % 12    // bVI in minor
                          : (tonic_pc + 9) % 12;   // vi in major
  }
  return tonic_pc;
}

/// @brief Check if a pitch class is a chord tone of a given degree in the key.
/// @param pitch_class Pitch class (0-11).
/// @param degree Chord degree.
/// @param key Key signature.
/// @return True if the pitch class is root, third, or fifth of the chord.
bool isChordTone(int pitch_class, ChordDegree degree, const KeySignature& key) {
  int root_offset = key.is_minor ? degreeMinorSemitones(degree)
                                 : degreeSemitones(degree);
  int root_pc = (static_cast<int>(key.tonic) + root_offset) % 12;

  ChordQuality quality = key.is_minor ? minorKeyQuality(degree)
                                      : majorKeyQuality(degree);

  // Build chord tones based on quality.
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

}  // namespace

// ---------------------------------------------------------------------------
// GoalTone computation
// ---------------------------------------------------------------------------

GoalTone SoggettoGenerator::computeGridAlignedGoalTone(
    const SoggettoParams& params,
    std::mt19937& rng) const {
  // Start with base GoalTone from character.
  GoalTone goal = goalToneForCharacter(params.character, rng);

  if (params.grid == nullptr || params.length_bars == 0) {
    return goal;
  }

  // Find the Intensification bar within the soggetto's span.
  // Grid uses 0-based indexing; start_bar is 1-based.
  int grid_start = static_cast<int>(params.start_bar) - 1;
  int span_end = grid_start + static_cast<int>(params.length_bars);

  // Search for Intensification position within the span.
  float intensification_ratio = goal.position_ratio;
  for (int bar = grid_start; bar < span_end && bar < 32; ++bar) {
    if (params.grid->getPhrasePosition(bar) == PhrasePosition::Intensification) {
      // Place climax at the Intensification bar's relative position.
      int bar_offset = bar - grid_start;
      intensification_ratio =
          (static_cast<float>(bar_offset) + 0.5f) /
          static_cast<float>(params.length_bars);
      break;
    }
  }

  goal.position_ratio = std::clamp(intensification_ratio, 0.1f, 0.9f);
  return goal;
}

// ---------------------------------------------------------------------------
// Candidate generation
// ---------------------------------------------------------------------------

std::vector<std::vector<NoteEvent>> SoggettoGenerator::generateCandidates(
    const SoggettoParams& params,
    const GoalTone& goal,
    const KeySignature& key,
    const TimeSignature& time_sig,
    std::mt19937& rng) const {
  std::vector<std::vector<NoteEvent>> candidates;
  candidates.reserve(static_cast<size_t>(params.path_candidates));

  int key_offset = static_cast<int>(key.tonic);
  ScaleType scale = key.is_minor ? ScaleType::HarmonicMinor : ScaleType::Major;
  Tick ticks_per_bar = time_sig.ticksPerBar();
  Tick total_ticks = static_cast<Tick>(params.length_bars) * ticks_per_bar;

  // Compute pitch range for the soggetto register.
  // Use bass motion primary_pitch from the grid as register anchor.
  constexpr int kBaseNote = 60;
  int register_anchor = kBaseNote;
  if (params.grid != nullptr) {
    int grid_bar = static_cast<int>(params.start_bar) - 1;
    int bass_anchor = static_cast<int>(
        params.grid->getStructuralBassPitch(grid_bar)) + 12;
    // Blend bass-derived anchor with Aria melody pitch for register affinity.
    // 7/8 harmonic (bass) + 1/8 melodic (theme) — light touch to preserve
    // harmonic differentiation while adding subtle melodic coherence.
    if (grid_bar >= 0 && grid_bar < 32 &&
        params.grid->getBar(grid_bar).aria_melody[0] > 0) {
      int aria_anchor = static_cast<int>(
          params.grid->getBar(grid_bar).aria_melody[0]);
      register_anchor = (bass_anchor * 7 + aria_anchor) / 8;
    } else {
      register_anchor = bass_anchor;
    }
  }

  int pitch_floor = std::max(static_cast<int>(organ_range::kManual1Low), register_anchor - 7);
  int pitch_ceil = std::min(static_cast<int>(organ_range::kManual1High), register_anchor + 14);

  // Determine start degree and pitch.
  int start_degree = 0;  // Start on tonic.
  int start_pitch = degreeToPitch(start_degree, register_anchor, key_offset, scale);
  start_pitch = std::max(pitch_floor, std::min(pitch_ceil, start_pitch));

  // Compute climax pitch from goal.
  int climax_pitch = start_pitch +
      static_cast<int>(static_cast<float>(pitch_ceil - start_pitch) *
                        goal.pitch_ratio);
  climax_pitch = std::max(pitch_floor, std::min(pitch_ceil, climax_pitch));
  Tick climax_tick =
      static_cast<Tick>(static_cast<float>(total_ticks) * goal.position_ratio);

  // Determine cadence target (if grid provides one at end bar).
  std::optional<int> cadence_target_pc;
  if (params.grid != nullptr) {
    int end_bar = static_cast<int>(params.start_bar) - 1 +
                  static_cast<int>(params.length_bars) - 1;
    auto cad = params.grid->getCadenceType(end_bar);
    if (cad.has_value()) {
      cadence_target_pc = cadenceTargetPitchClass(cad.value(), key);
    }
  }

  int max_leap = maxLeapForCharacter(params.character);

  for (int idx = 0; idx < params.path_candidates; ++idx) {
    uint32_t path_seed = rng() ^ (static_cast<uint32_t>(idx) * 0x5A3B7C1Du);
    std::mt19937 path_rng(path_seed);

    // Select motif template pair.
    uint32_t template_idx = path_rng() % 4;
    auto [motif_a, motif_b] = motifTemplatesForCharacter(
        params.character, template_idx);

    std::vector<NoteEvent> notes;
    notes.reserve(16);
    Tick current_tick = 0;

    // --- Motif A: ascending toward climax ---
    int current_degree = start_degree;
    int current_pitch = start_pitch;

    for (size_t mot_idx = 0;
         mot_idx < motif_a.degree_offsets.size() && current_tick < climax_tick;
         ++mot_idx) {
      Tick dur = (mot_idx < motif_a.durations.size())
                     ? motif_a.durations[mot_idx]
                     : kTicksPerBeat;
      if (current_tick + dur > climax_tick) {
        dur = climax_tick - current_tick;
        if (dur < kTicksPerBeat / 4) break;
      }

      int target_degree = start_degree + motif_a.degree_offsets[mot_idx];

      // Interpolate toward climax.
      float progress = (climax_tick > 0)
                            ? static_cast<float>(current_tick) /
                                  static_cast<float>(climax_tick)
                            : 0.0f;
      int interp_pitch = start_pitch +
          static_cast<int>(
              static_cast<float>(climax_pitch - start_pitch) * progress);

      int target_pitch = degreeToPitch(target_degree, register_anchor,
                                        key_offset, scale);
      if (target_pitch < interp_pitch) {
        target_degree += (interp_pitch - target_pitch + 1) / 2;
      }

      int pitch = snapToScale(
          degreeToPitch(target_degree, register_anchor, key_offset, scale),
          key.tonic, scale, pitch_floor, climax_pitch);

      // Clamp leap from previous note.
      if (!notes.empty()) {
        int prev_p = static_cast<int>(notes.back().pitch);
        pitch = clampLeap(pitch, prev_p, params.character, key.tonic, scale,
                          pitch_floor, climax_pitch, path_rng);
        pitch = avoidUnison(pitch, prev_p, key.tonic, scale,
                            pitch_floor, climax_pitch);
      }

      NoteFunction func = (mot_idx < motif_a.functions.size())
                               ? motif_a.functions[mot_idx]
                               : NoteFunction::StructuralTone;

      NoteEvent note;
      note.start_tick = current_tick;
      note.duration = dur;
      note.pitch = clampPitch(pitch, 0, 127);
      note.velocity = 80;
      note.voice = 0;
      note.source = BachNoteSource::GoldbergSoggetto;
      notes.push_back(note);

      current_tick += dur;
      current_pitch = pitch;
      current_degree = target_degree;
      (void)func;  // Function used for scoring, not placement.
    }

    // --- Climax note ---
    if (current_tick < total_ticks) {
      Tick climax_dur = kTicksPerBeat;
      if (current_tick + climax_dur > total_ticks) {
        climax_dur = total_ticks - current_tick;
      }

      int final_climax = climax_pitch;
      if (!notes.empty()) {
        int prev_p = static_cast<int>(notes.back().pitch);
        final_climax = clampLeap(climax_pitch, prev_p, params.character,
                                  key.tonic, scale, pitch_floor, pitch_ceil,
                                  path_rng);
      }

      NoteEvent note;
      note.start_tick = current_tick;
      note.duration = climax_dur;
      note.pitch = clampPitch(final_climax, 0, 127);
      note.velocity = 80;
      note.voice = 0;
      note.source = BachNoteSource::GoldbergSoggetto;
      notes.push_back(note);

      current_tick += climax_dur;
      current_pitch = final_climax;
    }

    // --- Motif B: descending from climax ---
    for (size_t mot_idx = 0;
         mot_idx < motif_b.degree_offsets.size() && current_tick < total_ticks;
         ++mot_idx) {
      Tick dur = (mot_idx < motif_b.durations.size())
                     ? motif_b.durations[mot_idx]
                     : kTicksPerBeat;
      if (current_tick + dur > total_ticks) {
        dur = total_ticks - current_tick;
        if (dur < kTicksPerBeat / 4) break;
      }

      int climax_abs_degree = scale_util::pitchToAbsoluteDegree(
          clampPitch(current_pitch, 0, 127),
          key.tonic, scale);
      int target_abs = climax_abs_degree + motif_b.degree_offsets[mot_idx];
      int pitch = snapToScale(
          static_cast<int>(scale_util::absoluteDegreeToPitch(
              target_abs, key.tonic, scale)),
          key.tonic, scale, pitch_floor, pitch_ceil);

      if (!notes.empty()) {
        int prev_p = static_cast<int>(notes.back().pitch);
        pitch = clampLeap(pitch, prev_p, params.character, key.tonic, scale,
                          pitch_floor, pitch_ceil, path_rng);
        pitch = avoidUnison(pitch, prev_p, key.tonic, scale,
                            pitch_floor, pitch_ceil);
      }

      NoteEvent note;
      note.start_tick = current_tick;
      note.duration = dur;
      note.pitch = clampPitch(pitch, 0, 127);
      note.velocity = 80;
      note.voice = 0;
      note.source = BachNoteSource::GoldbergSoggetto;
      notes.push_back(note);

      current_tick += dur;
      current_pitch = pitch;
    }

    // --- Cadence alignment: adjust ending note to match grid CadenceType ---
    if (cadence_target_pc.has_value() && !notes.empty()) {
      int prev_pitch_for_end =
          (notes.size() >= 2)
              ? static_cast<int>(notes[notes.size() - 2].pitch)
              : static_cast<int>(notes.back().pitch);
      int ending = normalizeEndingPitch(
          cadence_target_pc.value(), prev_pitch_for_end, max_leap,
          key.tonic, scale, pitch_floor, pitch_ceil);
      notes.back().pitch = clampPitch(ending, 0, 127);
    }

    // Quantize to 16th-note grid.
    constexpr Tick kTickQuantum = kTicksPerBeat / 4;  // 120
    for (auto& evt : notes) {
      evt.start_tick = (evt.start_tick / kTickQuantum) * kTickQuantum;
    }
    // Fix overlaps from quantization.
    for (size_t nidx = 0; nidx + 1 < notes.size(); ++nidx) {
      Tick next_start = notes[nidx + 1].start_tick;
      if (notes[nidx].start_tick + notes[nidx].duration > next_start) {
        notes[nidx].duration = next_start - notes[nidx].start_tick;
        if (notes[nidx].duration < kTickQuantum) {
          notes[nidx].duration = kTickQuantum;
        }
      }
    }

    // Post-processing leap enforcement.
    for (size_t nidx = 1; nidx < notes.size(); ++nidx) {
      int prev_p = static_cast<int>(notes[nidx - 1].pitch);
      int cur_p = static_cast<int>(notes[nidx].pitch);
      int interval_dist = std::abs(cur_p - prev_p);
      if (interval_dist > max_leap) {
        int direction = (cur_p > prev_p) ? 1 : -1;
        for (int attempt = max_leap; attempt >= 0; --attempt) {
          int cand = prev_p + direction * attempt;
          cand = std::max(pitch_floor, std::min(pitch_ceil, cand));
          int snapped = snapToScale(cand, key.tonic, scale,
                                     pitch_floor, pitch_ceil);
          if (std::abs(snapped - prev_p) <= max_leap) {
            notes[nidx].pitch = clampPitch(snapped, 0, 127);
            break;
          }
        }
      }
    }

    if (!notes.empty()) {
      candidates.push_back(std::move(notes));
    }
  }

  return candidates;
}

// ---------------------------------------------------------------------------
// Grid alignment scoring
// ---------------------------------------------------------------------------

float SoggettoGenerator::scoreGridAlignment(
    const std::vector<NoteEvent>& candidate,
    const SoggettoParams& params,
    const KeySignature& key,
    const TimeSignature& time_sig) const {
  if (candidate.empty() || params.grid == nullptr) {
    return 0.0f;
  }

  float score = 0.0f;
  Tick ticks_per_bar = time_sig.ticksPerBar();
  int grid_start = static_cast<int>(params.start_bar) - 1;
  int max_leap = maxLeapForCharacter(params.character);

  for (size_t idx = 0; idx < candidate.size(); ++idx) {
    const auto& note = candidate[idx];

    // Determine which grid bar this note falls in.
    int bar_offset = static_cast<int>(note.start_tick / ticks_per_bar);
    int grid_bar = grid_start + bar_offset;
    if (grid_bar < 0) grid_bar = 0;
    if (grid_bar > 31) grid_bar = 31;

    const auto& bar_info = params.grid->getBar(grid_bar);

    // Score harmonic alignment: is the pitch a chord tone?
    int pitch_class = getPitchClass(note.pitch);
    if (isChordTone(pitch_class, bar_info.chord_degree, key)) {
      score += 1.0f;
    }

    // Score phrase position alignment.
    if (bar_info.phrase_pos == PhrasePosition::Intensification) {
      // Higher pitches expected at intensification.
      if (idx > 0 && note.pitch > candidate[idx - 1].pitch) {
        score += 0.5f;
      }
    } else if (bar_info.phrase_pos == PhrasePosition::Cadence) {
      // Descending motion expected at cadence.
      if (idx > 0 && note.pitch <= candidate[idx - 1].pitch) {
        score += 0.5f;
      }
    }

    // Penalize leaps exceeding character's maxLeap.
    if (idx > 0) {
      int interval_dist = absoluteInterval(note.pitch, candidate[idx - 1].pitch);
      if (interval_dist > max_leap) {
        score -= 1.0f;
      }
    }
  }

  // Cadence alignment bonus: does the ending match the CadenceType?
  int end_bar = grid_start + static_cast<int>(params.length_bars) - 1;
  if (end_bar >= 0 && end_bar < 32) {
    auto cad = params.grid->getCadenceType(end_bar);
    if (cad.has_value()) {
      int target_pc = cadenceTargetPitchClass(cad.value(), key);
      int ending_pc = getPitchClass(candidate.back().pitch);
      if (ending_pc == target_pc) {
        score += 2.0f;
      }
    }
  }

  return score;
}

// ---------------------------------------------------------------------------
// Main generation pipeline
// ---------------------------------------------------------------------------

Subject SoggettoGenerator::generate(
    const SoggettoParams& params,
    const KeySignature& key,
    const TimeSignature& time_sig,
    uint32_t seed) const {
  Subject subject;
  subject.key = key.tonic;
  subject.is_minor = key.is_minor;
  subject.character = params.character;

  // Clamp length_bars to [1, 4].
  uint8_t bars = params.length_bars;
  if (bars < 1) bars = 1;
  if (bars > 4) bars = 4;

  Tick ticks_per_bar = time_sig.ticksPerBar();
  subject.length_ticks = static_cast<Tick>(bars) * ticks_per_bar;

  // Create the working params with clamped bars.
  SoggettoParams working_params = params;
  working_params.length_bars = bars;

  // Step 1: Create RNG from seed.
  std::mt19937 rng(seed);

  // Step 2: Compute grid-aligned GoalTone.
  GoalTone goal = computeGridAlignedGoalTone(working_params, rng);

  // Step 3: Generate N candidate paths.
  auto candidates = generateCandidates(working_params, goal, key, time_sig, rng);

  // Step 4: Score each candidate and select best.
  float best_score = -1.0f;
  size_t best_idx = 0;

  for (size_t idx = 0; idx < candidates.size(); ++idx) {
    float alignment_score = scoreGridAlignment(
        candidates[idx], working_params, key, time_sig);
    if (alignment_score > best_score) {
      best_score = alignment_score;
      best_idx = idx;
    }
  }

  // Step 5: Wrap best candidate in Subject.
  if (!candidates.empty()) {
    subject.notes = std::move(candidates[best_idx]);
  }

  return subject;
}

}  // namespace bach
