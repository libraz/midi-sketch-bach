// Main Goldberg Variations generator implementation.

#include "forms/goldberg/goldberg_generator.h"

#include <algorithm>
#include <cmath>

#include "core/interval.h"
#include "core/pitch_utils.h"
#include "core/rng_util.h"
#include "core/scale.h"
#include "counterpoint/parallel_repair.h"
#include "forms/goldberg/goldberg_aria.h"
#include "forms/goldberg/goldberg_binary.h"
#include "forms/goldberg/canon/canon_generator.h"
#include "forms/goldberg/canon/canon_types.h"
#include "forms/goldberg/variations/goldberg_black_pearl.h"
#include "forms/goldberg/variations/goldberg_crossing.h"
#include "forms/goldberg/variations/goldberg_dance.h"
#include "forms/goldberg/variations/goldberg_fughetta.h"
#include "forms/goldberg/variations/goldberg_invention.h"
#include "forms/goldberg/variations/goldberg_ornamental.h"
#include "forms/goldberg/variations/goldberg_overture.h"
#include "forms/goldberg/variations/goldberg_quodlibet.h"
#include "forms/goldberg/variations/goldberg_virtuoso.h"
#include "harmony/harmonic_time_warp.h"

namespace bach {

namespace {

/// @brief Golden ratio hash constant for per-variation seed derivation.
constexpr uint32_t kSeedMix = 0x9E3779B9u;

/// @brief GM program number for Harpsichord.
constexpr uint8_t kHarpsichordProgram = 6;

/// @brief Number of voices in the Goldberg texture.
constexpr uint8_t kGoldbergVoices = 5;

/// @brief Harpsichord voice ranges (MIDI note numbers).
/// voice 0 (soprano): C4-F6, voice 1 (alto): C3-F5,
/// voice 2 (tenor): C2-F4, voice 3 (bass): F1-F3, default: F1-F6.
constexpr uint8_t kHarpsichordLow[] = {60, 48, 36, 29, 29};
constexpr uint8_t kHarpsichordHigh[] = {89, 77, 65, 53, 53};
constexpr uint8_t kHarpsichordGlobalLow = 29;
constexpr uint8_t kHarpsichordGlobalHigh = 89;

/// @brief Short passing note threshold for tritone sweep exemption (ticks).
constexpr Tick kPassingNoteThreshold = 240;

/// @brief Number of bars in each binary repeat section.
constexpr int kSectionBars = 16;

/// @brief Total bars per variation.
constexpr int kTotalBars = 32;

/// @brief Merge separate voice note vectors into a single vector.
/// @param vectors Variable number of note vectors to merge.
/// @return Combined vector of all notes.
std::vector<NoteEvent> mergeNotes(std::initializer_list<const std::vector<NoteEvent>*> vectors) {
  size_t total = 0;
  for (const auto* vec : vectors) {
    total += vec->size();
  }
  std::vector<NoteEvent> result;
  result.reserve(total);
  for (const auto* vec : vectors) {
    result.insert(result.end(), vec->begin(), vec->end());
  }
  return result;
}

/// @brief Map a CanonDescriptor interval_semitones to a diatonic degree for CanonSpec.
///
/// The plan stores canon intervals in semitones (0=unison, 2=2nd, 4=3rd, 5=4th,
/// 7=5th, 9=6th, 11=7th, 12=octave, 14=9th). CanonSpec uses diatonic degree
/// (0=unison, 1=2nd, 2=3rd, ..., 8=9th).
///
/// @param semitones Interval in semitones from the descriptor.
/// @return Diatonic degree for CanonSpec.
int semitonesToDiatonicDegree(int semitones) {
  // Map common semitone intervals to diatonic degrees.
  switch (semitones) {
    case 0:  return 0;   // Unison
    case 2:  return 1;   // 2nd
    case 4:  return 2;   // 3rd
    case 5:  return 3;   // 4th
    case 7:  return 4;   // 5th
    case 9:  return 5;   // 6th
    case 11: return 6;   // 7th
    case 12: return 7;   // Octave
    case 14: return 8;   // 9th
    default: return semitones / 2;  // Rough fallback.
  }
}

}  // namespace

GoldbergResult GoldbergGenerator::generate(const GoldbergConfig& config) const {
  GoldbergResult result;

  // Step 1: Seed resolution.
  uint32_t seed = config.seed;
  if (seed == 0) {
    seed = rng::generateRandomSeed();
  }
  result.seed_used = seed;

  // Step 2: Create structural grids (Design Values).
  GoldbergStructuralGrid grid_major = GoldbergStructuralGrid::createMajor();
  GoldbergStructuralGrid grid_minor =
      GoldbergStructuralGrid::createMinor(MinorModeProfile::MixedBaroqueMinor);

  // Step 3: Get variation plan (32 entries).
  std::vector<GoldbergVariationDescriptor> plan = createGoldbergPlan();

  // Step 4: Select variations based on DurationScale.
  std::vector<size_t> selected = selectVariations(plan, config.scale);

  if (selected.empty()) {
    result.success = false;
    result.error_message = "No variations selected for the given scale";
    return result;
  }

  // Step 5: Generate each selected variation.
  // Track 0: Harpsichord I (upper manual, Ch 0) - voices 0, 1
  // Track 1: Harpsichord II (lower manual, Ch 1) - voices 2, 3, 4
  Track track_upper;
  track_upper.name = "Harpsichord I";
  track_upper.channel = 0;
  track_upper.program = kHarpsichordProgram;

  Track track_lower;
  track_lower.name = "Harpsichord II";
  track_lower.channel = 1;
  track_lower.program = kHarpsichordProgram;

  Tick cumulative_offset = 0;
  bool apply_repeats = (config.scale == DurationScale::Full) && config.apply_repeats;

  // Store the original Aria result for da capo reuse.
  AriaResult aria_original;
  bool have_aria = false;

  for (size_t sel_idx = 0; sel_idx < selected.size(); ++sel_idx) {
    size_t plan_idx = selected[sel_idx];
    if (plan_idx >= plan.size()) continue;

    const auto& desc = plan[plan_idx];

    // Per-variation seed: mix base seed with variation number.
    uint32_t var_seed = seed ^ (static_cast<uint32_t>(desc.variation_number) * kSeedMix);

    // Generate variation notes.
    std::vector<NoteEvent> var_notes;

    // Special handling for Aria: Var 0 generates, Var 31 reuses as da capo.
    if (desc.type == GoldbergVariationType::Aria && desc.variation_number == 0) {
      const auto& grid = desc.key.is_minor ? grid_minor : grid_major;
      AriaGenerator aria_gen;
      aria_original = aria_gen.generate(grid, config.key, desc.time_sig, var_seed);
      have_aria = aria_original.success;
      if (have_aria) {
        var_notes = mergeNotes({&aria_original.melody_notes, &aria_original.bass_notes});
      }
    } else if (desc.type == GoldbergVariationType::Aria && desc.variation_number == 31) {
      // Da capo: reuse original Aria with tick offset applied later.
      if (have_aria) {
        AriaResult da_capo = AriaGenerator::createDaCapo(aria_original, 0);
        var_notes = mergeNotes({&da_capo.melody_notes, &da_capo.bass_notes});
      } else {
        // Fallback: generate fresh if Aria was not in the selected set.
        const auto& grid = desc.key.is_minor ? grid_minor : grid_major;
        AriaGenerator aria_gen;
        auto fresh = aria_gen.generate(grid, config.key, desc.time_sig, var_seed);
        if (fresh.success) {
          var_notes = mergeNotes({&fresh.melody_notes, &fresh.bass_notes});
        }
      }
    } else {
      var_notes = generateVariation(desc, grid_major, grid_minor, config.key, var_seed);
    }

    if (var_notes.empty()) continue;

    // Apply binary repeats if Full scale.
    Tick bar_ticks = desc.time_sig.ticksPerBar();
    Tick section_ticks = static_cast<Tick>(kSectionBars) * bar_ticks;
    Tick variation_duration = static_cast<Tick>(kTotalBars) * bar_ticks;

    if (apply_repeats) {
      var_notes = applyBinaryRepeats(var_notes, section_ticks,
                                     config.ornament_variation_on_repeat);
      variation_duration *= 2;  // A-A-B-B doubles the duration.
    }

    // Apply harmonic time warp (Score → Performance).
    {
      const auto& grid = desc.key.is_minor ? grid_minor : grid_major;
      applyHarmonicTimeWarp(var_notes, grid, desc, var_seed);
    }

    // Apply ArticulationProfile.
    applyArticulation(var_notes, desc.articulation);

    // Offset all ticks by cumulative offset.
    for (auto& note : var_notes) {
      note.start_tick += cumulative_offset;
    }

    // Add TempoEvent at variation boundary.
    uint16_t var_bpm = calculateVariationBpm(desc, config.bpm);
    result.tempo_events.push_back({cumulative_offset, var_bpm});

    // Add TimeSignatureEvent at variation boundary.
    result.time_sig_events.push_back({cumulative_offset, desc.time_sig});

    // Split notes into tracks based on voice index.
    // Voices 0, 1 -> Track 0 (upper manual).
    // Voices 2, 3, 4 -> Track 1 (lower manual).
    for (auto& note : var_notes) {
      if (note.voice <= 1) {
        track_upper.notes.push_back(note);
      } else {
        track_lower.notes.push_back(note);
      }
    }

    cumulative_offset += variation_duration;
  }

  // Step 6: Sort notes within each track by start_tick for MIDI compliance.
  auto sortByTick = [](std::vector<NoteEvent>& notes) {
    std::sort(notes.begin(), notes.end(),
              [](const NoteEvent& lhs, const NoteEvent& rhs) {
                return lhs.start_tick < rhs.start_tick;
              });
  };
  sortByTick(track_upper.notes);
  sortByTick(track_lower.notes);

  // Step 6b: Counterpoint repair pipeline.
  // Merge both tracks into a single note vector for cross-voice analysis.
  {
    std::vector<NoteEvent> all_notes;
    all_notes.reserve(track_upper.notes.size() + track_lower.notes.size());
    all_notes.insert(all_notes.end(), track_upper.notes.begin(), track_upper.notes.end());
    all_notes.insert(all_notes.end(), track_lower.notes.begin(), track_lower.notes.end());

    // (a) Parallel perfect consonance repair.
    {
      ParallelRepairParams pp_params;
      pp_params.num_voices = kGoldbergVoices;
      pp_params.scale = config.key.is_minor ? ScaleType::HarmonicMinor : ScaleType::Major;
      pp_params.key_at_tick = [&](Tick) { return config.key.tonic; };
      pp_params.voice_range = [](uint8_t voice) -> std::pair<uint8_t, uint8_t> {
        if (voice < kGoldbergVoices) {
          return {kHarpsichordLow[voice], kHarpsichordHigh[voice]};
        }
        return {kHarpsichordGlobalLow, kHarpsichordGlobalHigh};
      };
      pp_params.max_iterations = 8;
      repairParallelPerfect(all_notes, pp_params);
    }

    // (b) Per-voice tritone sweep: fix tritone leaps between consecutive notes.
    ScaleType tritone_scale =
        config.key.is_minor ? ScaleType::HarmonicMinor : ScaleType::Major;
    for (uint8_t vid = 0; vid < kGoldbergVoices; ++vid) {
      // Collect indices belonging to this voice.
      std::vector<size_t> voice_indices;
      for (size_t idx = 0; idx < all_notes.size(); ++idx) {
        if (all_notes[idx].voice == vid) {
          voice_indices.push_back(idx);
        }
      }
      // Sort by tick for consecutive-pair analysis.
      std::sort(voice_indices.begin(), voice_indices.end(),
                [&all_notes](size_t lhs, size_t rhs) {
                  return all_notes[lhs].start_tick < all_notes[rhs].start_tick;
                });

      uint8_t voice_lo = (vid < kGoldbergVoices) ? kHarpsichordLow[vid] : kHarpsichordGlobalLow;
      uint8_t voice_hi =
          (vid < kGoldbergVoices) ? kHarpsichordHigh[vid] : kHarpsichordGlobalHigh;

      for (size_t pos = 1; pos < voice_indices.size(); ++pos) {
        size_t prev_idx = voice_indices[pos - 1];
        size_t cur_idx = voice_indices[pos];
        auto& prev_note = all_notes[prev_idx];
        auto& cur_note = all_notes[cur_idx];

        // Skip protected notes (Immutable or Structural sources).
        if (getProtectionLevel(prev_note.source) != ProtectionLevel::Flexible) continue;
        if (getProtectionLevel(cur_note.source) != ProtectionLevel::Flexible) continue;

        // Skip short passing notes (both durations <= 240 ticks).
        if (prev_note.duration <= kPassingNoteThreshold &&
            cur_note.duration <= kPassingNoteThreshold) {
          continue;
        }

        int prev_p = static_cast<int>(prev_note.pitch);
        int cur_p = static_cast<int>(cur_note.pitch);
        int simple = interval_util::compoundToSimple(std::abs(cur_p - prev_p));
        if (simple != interval::kTritone) continue;

        // Try adjustments {+1, -1, +2, -2}, pick best non-tritone candidate.
        int best_cand = cur_p;
        int best_cost = 9999;
        for (int delta : {1, -1, 2, -2}) {
          int shifted = cur_p + delta;
          uint8_t snapped =
              scale_util::nearestScaleTone(clampPitch(shifted, voice_lo, voice_hi),
                                           config.key.tonic, tritone_scale);
          int cand = static_cast<int>(snapped);
          int new_simple = interval_util::compoundToSimple(std::abs(cand - prev_p));
          if (new_simple == interval::kTritone) continue;  // Still a tritone.

          // Check forward: avoid creating tritone with next note in voice.
          if (pos + 1 < voice_indices.size()) {
            int next_p = static_cast<int>(all_notes[voice_indices[pos + 1]].pitch);
            int fwd_simple = interval_util::compoundToSimple(std::abs(cand - next_p));
            if (fwd_simple == interval::kTritone) continue;
          }

          // Minimize interval distance; penalize unison with previous note.
          int cost = std::abs(cand - prev_p) + ((cand == prev_p) ? 30 : 0);
          if (cost < best_cost) {
            best_cost = cost;
            best_cand = cand;
          }
        }

        if (best_cand != cur_p) {
          cur_note.pitch = clampPitch(best_cand, voice_lo, voice_hi);
        }
      }
    }

    // (c) Redistribute repaired notes back into upper/lower tracks.
    track_upper.notes.clear();
    track_lower.notes.clear();
    for (auto& note : all_notes) {
      if (note.voice <= 1) {
        track_upper.notes.push_back(note);
      } else {
        track_lower.notes.push_back(note);
      }
    }
    sortByTick(track_upper.notes);
    sortByTick(track_lower.notes);
  }

  // Step 7: Assemble result.
  result.tracks.push_back(std::move(track_upper));
  result.tracks.push_back(std::move(track_lower));

  // Build a harmonic timeline from the major grid for analysis.
  result.timeline = grid_major.toTimeline(config.key, {3, 4});

  result.total_duration_ticks = cumulative_offset;
  result.success = cumulative_offset > 0;
  if (!result.success) {
    result.error_message = "No variations produced any notes";
  }

  return result;
}

std::vector<NoteEvent> GoldbergGenerator::generateVariation(
    const GoldbergVariationDescriptor& desc,
    const GoldbergStructuralGrid& grid_major,
    const GoldbergStructuralGrid& grid_minor,
    const KeySignature& key,
    uint32_t seed) const {
  // Select grid based on variation key.
  const auto& grid = desc.key.is_minor ? grid_minor : grid_major;

  switch (desc.type) {
    case GoldbergVariationType::Aria: {
      // Aria is handled specially in the main loop for da capo support.
      AriaGenerator gen;
      auto result = gen.generate(grid, key, desc.time_sig, seed);
      if (!result.success) return {};
      return mergeNotes({&result.melody_notes, &result.bass_notes});
    }

    case GoldbergVariationType::Canon: {
      CanonSpec spec;
      spec.canon_interval = semitonesToDiatonicDegree(desc.canon.interval_semitones);
      spec.transform = desc.canon.is_inverted ? CanonTransform::Inverted
                                              : CanonTransform::Regular;
      spec.key = {key.tonic, desc.key.is_minor};
      spec.minor_profile = desc.minor_profile;
      spec.delay_bars = desc.canon.delay_beats;
      CanonGenerator gen;
      auto result = gen.generate(spec, grid, desc.time_sig, seed);
      if (!result.success) return {};
      return mergeNotes({&result.dux_notes, &result.comes_notes, &result.bass_notes});
    }

    case GoldbergVariationType::Dance: {
      DanceGenerator gen;
      auto result = gen.generate(desc.variation_number, grid, key, seed);
      if (!result.success) return {};
      return result.notes;
    }

    case GoldbergVariationType::Ornamental:
    case GoldbergVariationType::TrillEtude: {
      OrnamentalGenerator gen;
      auto result = gen.generate(desc.variation_number, grid, key, desc.time_sig, seed);
      if (!result.success) return {};
      return result.notes;
    }

    case GoldbergVariationType::Fughetta:
    case GoldbergVariationType::AllaBreveFugal: {
      FughettaGenerator gen;
      auto result = gen.generate(desc.variation_number, grid, key, desc.time_sig, seed);
      if (!result.success) return {};
      return result.notes;
    }

    case GoldbergVariationType::Invention: {
      InventionGenerator gen;
      auto result = gen.generate(grid, key, desc.time_sig, seed);
      if (!result.success) return {};
      return result.notes;
    }

    case GoldbergVariationType::HandCrossing: {
      CrossingGenerator gen;
      auto result = gen.generate(desc.variation_number, grid, key, desc.time_sig, seed);
      if (!result.success) return {};
      return result.notes;
    }

    case GoldbergVariationType::FrenchOverture: {
      OvertureGenerator gen;
      auto result = gen.generate(grid, key, desc.time_sig, seed);
      if (!result.success) return {};
      return result.notes;
    }

    case GoldbergVariationType::Toccata:
    case GoldbergVariationType::ScalePassage:
    case GoldbergVariationType::BravuraChordal: {
      VirtuosoGenerator gen;
      auto result = gen.generate(desc.variation_number, grid, key, desc.time_sig, seed);
      if (!result.success) return {};
      return result.notes;
    }

    case GoldbergVariationType::BlackPearl: {
      BlackPearlGenerator gen;
      auto result = gen.generate(grid, {key.tonic, true}, desc.time_sig, seed);
      if (!result.success) return {};
      return result.notes;
    }

    case GoldbergVariationType::Quodlibet: {
      QuodlibetGenerator gen;
      auto result = gen.generate(grid, key, desc.time_sig, seed);
      if (!result.success) return {};
      return result.notes;
    }
  }

  return {};  // Unreachable for well-formed descriptors.
}

std::vector<size_t> GoldbergGenerator::selectVariations(
    const std::vector<GoldbergVariationDescriptor>& plan,
    DurationScale scale) const {
  std::vector<size_t> indices;

  switch (scale) {
    case DurationScale::Short:
      // Aria + 10 representative variations + da capo (~12 variations).
      // Covers each generator type at least once.
      indices = {
          0,   // Aria
          1,   // Ornamental (Var 1)
          3,   // Canon at unison (Var 3)
          4,   // Dance Passepied (Var 4)
          7,   // Dance Gigue (Var 7)
          10,  // Fughetta (Var 10)
          14,  // Trill Etude (Var 14)
          16,  // French Overture (Var 16)
          25,  // Black Pearl (Var 25)
          29,  // Bravura Chordal (Var 29)
          30,  // Quodlibet (Var 30)
          31   // Da capo Aria
      };
      break;

    case DurationScale::Medium:
      // ~22 variations: broader coverage.
      indices = {
          0,   // Aria
          1,   // Ornamental (Var 1)
          2,   // Invention (Var 2)
          3,   // Canon at unison (Var 3)
          4,   // Dance Passepied (Var 4)
          5,   // Ornamental (Var 5)
          6,   // Canon at 2nd (Var 6)
          7,   // Dance Gigue (Var 7)
          8,   // Hand Crossing (Var 8)
          9,   // Canon at 3rd (Var 9)
          10,  // Fughetta (Var 10)
          13,  // Ornamental Sarabande (Var 13)
          14,  // Trill Etude (Var 14)
          15,  // Canon at 5th inverted, G minor (Var 15)
          16,  // French Overture (Var 16)
          19,  // Dance Passepied (Var 19)
          22,  // Alla Breve Fugal (Var 22)
          25,  // Black Pearl (Var 25)
          29,  // Bravura Chordal (Var 29)
          30,  // Quodlibet (Var 30)
          31   // Da capo Aria
      };
      break;

    case DurationScale::Long:
    case DurationScale::Full:
      // All 32 entries.
      indices.reserve(plan.size());
      for (size_t idx = 0; idx < plan.size(); ++idx) {
        indices.push_back(idx);
      }
      break;
  }

  return indices;
}

void GoldbergGenerator::applyArticulation(
    std::vector<NoteEvent>& notes,
    ArticulationProfile profile) const {
  float ratio = 1.0f;
  switch (profile) {
    case ArticulationProfile::Legato:
      ratio = 0.95f;
      break;
    case ArticulationProfile::Moderato:
      ratio = 0.85f;
      break;
    case ArticulationProfile::Detache:
      ratio = 0.70f;
      break;
    case ArticulationProfile::FrenchDotted:
      // Dotted notes keep longer ratio; short notes get shorter.
      for (auto& note : notes) {
        Tick beat_dur = kTicksPerBeat;
        if (note.duration >= beat_dur) {
          // Long (dotted) note: full duration.
          note.duration = static_cast<Tick>(note.duration * 0.95f);
        } else {
          // Short note: crisp articulation.
          note.duration = static_cast<Tick>(note.duration * 0.50f);
        }
        if (note.duration == 0) note.duration = 1;
        note.modified_by |= static_cast<uint8_t>(NoteModifiedBy::Articulation);
      }
      return;  // FrenchDotted has its own per-note logic, skip uniform ratio.

    case ArticulationProfile::Brillante:
      // Passages get light articulation; chords (multiple notes at same tick) get full.
      // Simple heuristic: notes shorter than a quarter beat are passage notes.
      for (auto& note : notes) {
        Tick passage_threshold = kTicksPerBeat / 2;
        if (note.duration < passage_threshold) {
          note.duration = static_cast<Tick>(note.duration * 0.60f);
        }
        // Chords keep full duration (no modification).
        if (note.duration == 0) note.duration = 1;
        note.modified_by |= static_cast<uint8_t>(NoteModifiedBy::Articulation);
      }
      return;  // Brillante has its own per-note logic.
  }

  // Apply uniform ratio for Legato, Moderato, Detache.
  for (auto& note : notes) {
    note.duration = static_cast<Tick>(note.duration * ratio);
    if (note.duration == 0) note.duration = 1;
    note.modified_by |= static_cast<uint8_t>(NoteModifiedBy::Articulation);
  }
}

uint16_t GoldbergGenerator::calculateVariationBpm(
    const GoldbergVariationDescriptor& desc,
    uint16_t base_bpm) const {
  if (desc.bpm_override > 0) {
    return desc.bpm_override;
  }
  // Apply Proportionslehre tempo ratio.
  auto [num, den] = desc.tempo_ratio;
  if (den == 0) den = 1;
  uint16_t var_bpm = static_cast<uint16_t>(base_bpm * num / den);
  // Clamp to reasonable range.
  if (var_bpm < 30) var_bpm = 30;
  if (var_bpm > 200) var_bpm = 200;
  return var_bpm;
}

}  // namespace bach
