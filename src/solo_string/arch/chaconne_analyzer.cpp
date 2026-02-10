// Implementation of ChaconneAnalyzer -- quality metrics for BWV1004-style chaconne pieces.

#include "solo_string/arch/chaconne_analyzer.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "core/basic_types.h"
#include "core/pitch_utils.h"
#include "solo_string/arch/chaconne_config.h"
#include "solo_string/arch/ground_bass.h"
#include "solo_string/arch/variation_types.h"

namespace bach {

// ===========================================================================
// Internal helpers
// ===========================================================================

namespace {

/// @brief Collect all NoteEvents from all tracks into a single sorted vector.
/// @param tracks Input tracks.
/// @return All notes sorted by start_tick.
std::vector<NoteEvent> collectAllNotes(const std::vector<Track>& tracks) {
  std::vector<NoteEvent> all_notes;
  for (const auto& track : tracks) {
    all_notes.insert(all_notes.end(), track.notes.begin(), track.notes.end());
  }
  std::sort(all_notes.begin(), all_notes.end(),
            [](const NoteEvent& lhs, const NoteEvent& rhs) {
              return lhs.start_tick < rhs.start_tick;
            });
  return all_notes;
}

/// @brief Get notes within a tick range [start, end).
/// @param all_notes Sorted notes.
/// @param start Start tick (inclusive).
/// @param end End tick (exclusive).
/// @return Notes whose start_tick falls within the range.
std::vector<NoteEvent> notesInRange(const std::vector<NoteEvent>& all_notes,
                                    Tick start, Tick end) {
  std::vector<NoteEvent> result;
  for (const auto& note : all_notes) {
    if (note.start_tick >= end) break;
    if (note.start_tick >= start) {
      result.push_back(note);
    }
  }
  return result;
}

/// @brief Get the tick range for a variation index.
/// @param variation_idx 0-based variation index.
/// @param variation_length Length of one variation in ticks.
/// @return Pair of (start_tick inclusive, end_tick exclusive).
std::pair<Tick, Tick> variationTickRange(int variation_idx, Tick variation_length) {
  Tick start = static_cast<Tick>(variation_idx) * variation_length;
  Tick end = start + variation_length;
  return {start, end};
}

/// @brief Get the average pitch of a set of notes.
/// @param notes Notes to analyze.
/// @return Average MIDI pitch, or 0.0 if empty.
float averagePitch(const std::vector<NoteEvent>& notes) {
  if (notes.empty()) return 0.0f;
  float sum = 0.0f;
  for (const auto& note : notes) {
    sum += static_cast<float>(note.pitch);
  }
  return sum / static_cast<float>(notes.size());
}

/// @brief Compute note density (notes per tick) for a set of notes in a tick range.
/// @param notes Notes in the range.
/// @param range_ticks Total tick length of the range.
/// @return Notes per tick, or 0.0 if range is zero.
float noteDensity(const std::vector<NoteEvent>& notes, Tick range_ticks) {
  if (range_ticks == 0) return 0.0f;
  return static_cast<float>(notes.size()) / static_cast<float>(range_ticks);
}

}  // namespace

// ===========================================================================
// Metric implementations
// ===========================================================================

namespace {

/// @brief Verify ground bass integrity across all variations (INSTANT FAIL if != 1.0).
///
/// For each variation, extracts the lowest-pitch notes at the expected ground bass
/// tick positions (offset within the variation) and compares them against the
/// original ground bass pattern. Uses GroundBass::verifyIntegrity().
///
/// @param all_notes All notes sorted by start_tick.
/// @param config Chaconne config with variation plan.
/// @param ground_bass The original immutable ground bass.
/// @return 1.0 if ALL variations preserve the bass, 0.0 if any modification detected.
float computeGroundBassIntegrity(const std::vector<NoteEvent>& all_notes,
                                 const ChaconneConfig& config,
                                 const GroundBass& ground_bass) {
  if (ground_bass.isEmpty() || config.variations.empty()) {
    return 0.0f;
  }

  Tick bass_length = ground_bass.getLengthTicks();
  if (bass_length == 0) {
    return 0.0f;
  }

  const auto& bass_notes = ground_bass.getNotes();
  int num_variations = static_cast<int>(config.variations.size());

  for (int var_idx = 0; var_idx < num_variations; ++var_idx) {
    auto [var_start, var_end] = variationTickRange(var_idx, bass_length);
    auto var_notes = notesInRange(all_notes, var_start, var_end);

    // For each expected bass note, find the lowest-pitch note at that position
    // within this variation.
    std::vector<NoteEvent> extracted_bass;
    extracted_bass.reserve(bass_notes.size());

    for (const auto& original_note : bass_notes) {
      // The expected position is the original note's offset + variation start.
      Tick expected_tick = var_start + original_note.start_tick;

      // Find the lowest-pitch note at or near the expected tick.
      // Allow a small tolerance window of 1 tick for floating-point drift.
      NoteEvent best_match{};
      bool found = false;

      for (const auto& note : var_notes) {
        if (note.start_tick == expected_tick) {
          if (!found || note.pitch < best_match.pitch) {
            best_match = note;
            // Store with offset relative to variation start for integrity check.
            best_match.start_tick = note.start_tick - var_start;
            found = true;
          }
        }
      }

      if (!found) {
        // Missing bass note -- integrity violated.
        return 0.0f;
      }

      extracted_bass.push_back(best_match);
    }

    // Verify the extracted bass against the original.
    if (!ground_bass.verifyIntegrity(extracted_bass)) {
      return 0.0f;
    }
  }

  return 1.0f;
}

/// @brief Validate variation role order (INSTANT FAIL if != 1.0).
///
/// Extracts the VariationRole sequence from config.variations and calls
/// isRoleOrderValid().
///
/// @param config Chaconne config with variation plan.
/// @return 1.0 if valid, 0.0 if any order violation.
float computeRoleOrderScore(const ChaconneConfig& config) {
  if (config.variations.empty()) {
    return 0.0f;
  }

  std::vector<VariationRole> roles;
  roles.reserve(config.variations.size());
  for (const auto& var : config.variations) {
    roles.push_back(var.role);
  }

  return isRoleOrderValid(roles) ? 1.0f : 0.0f;
}

/// @brief Validate climax presence (INSTANT FAIL if != 1.0).
///
/// Checks:
/// - Accumulate role appears exactly 3 times.
/// - Accumulate variations are positioned within the config's ClimaxDesign
///   position_ratio range.
///
/// @param config Chaconne config with variation plan and climax design.
/// @return 1.0 if exactly 3 Accumulate at correct position, 0.0 otherwise.
float computeClimaxPresenceScore(const ChaconneConfig& config) {
  if (config.variations.empty()) {
    return 0.0f;
  }

  int accumulate_count = 0;
  int first_accumulate_idx = -1;
  int last_accumulate_idx = -1;
  int num_variations = static_cast<int>(config.variations.size());

  for (int idx = 0; idx < num_variations; ++idx) {
    if (config.variations[static_cast<size_t>(idx)].role == VariationRole::Accumulate) {
      ++accumulate_count;
      if (first_accumulate_idx < 0) {
        first_accumulate_idx = idx;
      }
      last_accumulate_idx = idx;
    }
  }

  // Must be exactly 3 Accumulate variations.
  if (accumulate_count != 3) {
    return 0.0f;
  }

  // Check position ratio: the center of the Accumulate block should fall within
  // [position_ratio_min, position_ratio_max] of the total variation count.
  // Using the center avoids penalizing legitimate placements where the block
  // spans slightly beyond the boundary while remaining correctly positioned.
  float center_ratio =
      (static_cast<float>(first_accumulate_idx) +
       static_cast<float>(last_accumulate_idx + 1)) /
      (2.0f * static_cast<float>(num_variations));

  if (center_ratio < config.climax.position_ratio_min ||
      center_ratio > config.climax.position_ratio_max) {
    return 0.0f;
  }

  return 1.0f;
}

/// @brief Measure implied polyphony in ImpliedPolyphony texture sections (INSTANT FAIL).
///
/// For each time point in an ImpliedPolyphony variation, counts distinct register
/// bands (octave-wide) with active notes. Averages across all ImpliedPolyphony
/// sections. Score = 1.0 if average is in [2.3, 2.8], 0.0 if outside.
/// If no ImpliedPolyphony textures exist, score = 1.0 (not applicable).
///
/// @param all_notes All notes sorted by start_tick.
/// @param config Chaconne config with variation plan.
/// @param ground_bass Ground bass for variation length.
/// @param[out] avg_voice_count Diagnostic: average implied voice count.
/// @return 1.0 if in range or not applicable, 0.0 if out of range.
float computeImpliedPolyphonyScore(const std::vector<NoteEvent>& all_notes,
                                   const ChaconneConfig& config,
                                   const GroundBass& ground_bass,
                                   float& avg_voice_count) {
  avg_voice_count = 0.0f;

  Tick bass_length = ground_bass.getLengthTicks();
  if (bass_length == 0) {
    return 1.0f;  // Not applicable.
  }

  // Collect indices of ImpliedPolyphony variations.
  std::vector<int> implied_poly_indices;
  for (int idx = 0; idx < static_cast<int>(config.variations.size()); ++idx) {
    if (config.variations[static_cast<size_t>(idx)].primary_texture ==
        TextureType::ImpliedPolyphony) {
      implied_poly_indices.push_back(idx);
    }
  }

  if (implied_poly_indices.empty()) {
    return 1.0f;  // Not applicable when no ImpliedPolyphony textures exist.
  }

  // Measure implied voices at each beat within ImpliedPolyphony variations.
  float total_voice_count = 0.0f;
  int total_beats = 0;

  for (int var_idx : implied_poly_indices) {
    auto [var_start, var_end] = variationTickRange(var_idx, bass_length);
    auto var_notes = notesInRange(all_notes, var_start, var_end);

    if (var_notes.empty()) continue;

    // Sample at each beat position within the variation.
    for (Tick beat_tick = var_start; beat_tick < var_end; beat_tick += kTicksPerBeat) {
      // Find all notes active at this beat (start <= beat_tick < start + duration).
      std::set<int> active_bands;
      for (const auto& note : var_notes) {
        Tick note_end = note.start_tick + note.duration;
        if (note.start_tick <= beat_tick && beat_tick < note_end) {
          // Register band = octave (pitch / 12).
          int band = static_cast<int>(note.pitch) / 12;
          active_bands.insert(band);
        }
      }

      // Also count notes that start within a half-beat window to capture
      // alternating voice patterns typical of implied polyphony.
      Tick window_end = beat_tick + kTicksPerBeat / 2;
      std::set<int> nearby_bands;
      for (const auto& note : var_notes) {
        if (note.start_tick >= beat_tick && note.start_tick < window_end) {
          int band = static_cast<int>(note.pitch) / 12;
          nearby_bands.insert(band);
        }
      }

      // Use the larger of active or nearby band counts.
      int voice_count = static_cast<int>(
          std::max(active_bands.size(), nearby_bands.size()));
      if (voice_count > 0) {
        total_voice_count += static_cast<float>(voice_count);
        ++total_beats;
      }
    }
  }

  if (total_beats == 0) {
    return 1.0f;  // No beats to analyze.
  }

  avg_voice_count = total_voice_count / static_cast<float>(total_beats);

  // Score = 1.0 if average is in [2.3, 2.8], 0.0 if outside.
  constexpr float kMinImpliedVoices = 2.3f;
  constexpr float kMaxImpliedVoices = 2.8f;

  if (avg_voice_count >= kMinImpliedVoices && avg_voice_count <= kMaxImpliedVoices) {
    return 1.0f;
  }

  return 0.0f;
}

/// @brief Measure texture type diversity using Shannon entropy (threshold >= 0.7).
///
/// Counts occurrences of each TextureType across all variations and calculates
/// normalized Shannon entropy. Higher entropy = more diverse textures.
///
/// @param config Chaconne config with variation plan.
/// @return Score in [0.0, 1.0].
float computeVariationDiversity(const ChaconneConfig& config) {
  if (config.variations.empty()) {
    return 0.0f;
  }

  // Count texture type occurrences.
  std::map<TextureType, int> texture_counts;
  for (const auto& var : config.variations) {
    ++texture_counts[var.primary_texture];
  }

  int num_types = static_cast<int>(texture_counts.size());
  if (num_types <= 1) {
    return 0.0f;  // No diversity at all.
  }

  // Calculate Shannon entropy: H = -sum(p * log2(p)).
  float total = static_cast<float>(config.variations.size());
  float entropy = 0.0f;

  for (const auto& [tex_type, count] : texture_counts) {
    float prob = static_cast<float>(count) / total;
    if (prob > 0.0f) {
      entropy -= prob * std::log2(prob);
    }
  }

  // Normalize by maximum possible entropy (log2 of total number of TextureType values).
  // There are 6 TextureType values defined.
  constexpr int kTotalTextureTypes = 6;
  float max_entropy = std::log2(static_cast<float>(
      std::min(kTotalTextureTypes, static_cast<int>(config.variations.size()))));

  if (max_entropy <= 0.0f) {
    return 0.0f;
  }

  float normalized = entropy / max_entropy;
  if (normalized > 1.0f) normalized = 1.0f;

  return normalized;
}

/// @brief Measure texture transition smoothness between consecutive variations
///        (threshold >= 0.5).
///
/// Penalizes identical textures in adjacent variations and rewards smooth transitions.
/// Transition smoothness is based on a predefined affinity between texture pairs.
///
/// @param config Chaconne config with variation plan.
/// @return Score in [0.0, 1.0].
float computeTextureTransitionScore(const ChaconneConfig& config) {
  if (config.variations.size() < 2) {
    return 1.0f;  // No transitions to evaluate.
  }

  int total_transitions = static_cast<int>(config.variations.size()) - 1;
  float total_score = 0.0f;

  for (int idx = 1; idx < static_cast<int>(config.variations.size()); ++idx) {
    TextureType prev = config.variations[static_cast<size_t>(idx - 1)].primary_texture;
    TextureType curr = config.variations[static_cast<size_t>(idx)].primary_texture;

    if (prev == curr) {
      // Identical adjacent textures: penalize (score 0.0 for this transition).
      total_score += 0.0f;
      continue;
    }

    // Smooth transitions are rewarded; large jumps are partially penalized.
    // Define transition affinity based on musical similarity.
    // SingleLine <-> ImpliedPolyphony: smooth (adding voices)
    // ImpliedPolyphony <-> FullChords: smooth (density increase)
    // SingleLine <-> Arpeggiated: smooth (broken chords from line)
    // Arpeggiated <-> ImpliedPolyphony: smooth (rhythmic similarity)
    // ScalePassage <-> Arpeggiated: smooth (passage types)
    // Bariolage <-> Arpeggiated: smooth (string technique)
    // Others: moderate
    // SingleLine <-> FullChords: large jump

    auto pair = std::make_pair(
        std::min(static_cast<uint8_t>(prev), static_cast<uint8_t>(curr)),
        std::max(static_cast<uint8_t>(prev), static_cast<uint8_t>(curr)));

    // Smooth pairs (score 1.0).
    bool is_smooth =
        // SingleLine(0) <-> ImpliedPolyphony(1)
        (pair == std::make_pair(uint8_t{0}, uint8_t{1})) ||
        // ImpliedPolyphony(1) <-> FullChords(2)
        (pair == std::make_pair(uint8_t{1}, uint8_t{2})) ||
        // SingleLine(0) <-> Arpeggiated(3)
        (pair == std::make_pair(uint8_t{0}, uint8_t{3})) ||
        // ImpliedPolyphony(1) <-> Arpeggiated(3)
        (pair == std::make_pair(uint8_t{1}, uint8_t{3})) ||
        // Arpeggiated(3) <-> ScalePassage(4)
        (pair == std::make_pair(uint8_t{3}, uint8_t{4})) ||
        // Arpeggiated(3) <-> Bariolage(5)
        (pair == std::make_pair(uint8_t{3}, uint8_t{5})) ||
        // ScalePassage(4) <-> Bariolage(5)
        (pair == std::make_pair(uint8_t{4}, uint8_t{5}));

    if (is_smooth) {
      total_score += 1.0f;
    } else {
      // Moderate transition: partial credit.
      total_score += 0.5f;
    }
  }

  return total_score / static_cast<float>(total_transitions);
}

/// @brief Measure section balance (minor-front / major / minor-back) proportions
///        (threshold >= 0.7).
///
/// Ideal proportions: minor front ~30%, major ~20%, minor back + climax ~50%.
/// Score based on how close to ideal proportions using L1 distance.
///
/// @param config Chaconne config with variation plan.
/// @return Score in [0.0, 1.0].
float computeSectionBalance(const ChaconneConfig& config) {
  if (config.variations.empty()) {
    return 0.0f;
  }

  int num_variations = static_cast<int>(config.variations.size());
  int minor_front = 0;
  int major_section = 0;
  int minor_back = 0;

  // Minor front: variations before the first major section variation.
  // Major: variations with is_major_section = true.
  // Minor back: variations after the last major section variation.
  int first_major = -1;
  int last_major = -1;

  for (int idx = 0; idx < num_variations; ++idx) {
    if (config.variations[static_cast<size_t>(idx)].is_major_section) {
      ++major_section;
      if (first_major < 0) first_major = idx;
      last_major = idx;
    }
  }

  if (first_major < 0) {
    // No major section: treat as all minor.
    // Still score based on the overall structure (Establish + Develop + ... pattern).
    return 0.5f;
  }

  minor_front = first_major;
  minor_back = num_variations - last_major - 1;

  float total = static_cast<float>(num_variations);
  float front_ratio = static_cast<float>(minor_front) / total;
  float major_ratio = static_cast<float>(major_section) / total;
  float back_ratio = static_cast<float>(minor_back) / total;

  // Ideal proportions.
  constexpr float kIdealFront = 0.30f;
  constexpr float kIdealMajor = 0.20f;
  constexpr float kIdealBack = 0.50f;

  // L1 distance from ideal.
  float distance = std::abs(front_ratio - kIdealFront) +
                   std::abs(major_ratio - kIdealMajor) +
                   std::abs(back_ratio - kIdealBack);

  // Maximum possible L1 distance is 2.0 (completely wrong proportions).
  // Score = 1.0 - distance/2.0, clamped to [0, 1].
  float score = 1.0f - distance / 2.0f;
  if (score < 0.0f) score = 0.0f;

  return score;
}

/// @brief Measure major section distinctness from minor sections (threshold >= 0.6).
///
/// Compares average register height, note density, and texture variety between
/// the major and minor sections. A higher score indicates the major section has
/// a clearly different "personality".
///
/// @param all_notes All notes sorted by start_tick.
/// @param config Chaconne config with variation plan.
/// @param ground_bass Ground bass for variation length.
/// @return Score in [0.0, 1.0].
float computeMajorSectionSeparation(const std::vector<NoteEvent>& all_notes,
                                    const ChaconneConfig& config,
                                    const GroundBass& ground_bass) {
  if (config.variations.empty() || ground_bass.isEmpty()) {
    return 0.0f;
  }

  Tick bass_length = ground_bass.getLengthTicks();
  if (bass_length == 0) {
    return 0.0f;
  }

  // Collect notes for major and minor sections.
  std::vector<NoteEvent> major_notes;
  std::vector<NoteEvent> minor_notes;
  std::set<TextureType> major_textures;
  std::set<TextureType> minor_textures;

  for (int idx = 0; idx < static_cast<int>(config.variations.size()); ++idx) {
    auto [var_start, var_end] = variationTickRange(idx, bass_length);
    auto var_notes = notesInRange(all_notes, var_start, var_end);

    if (config.variations[static_cast<size_t>(idx)].is_major_section) {
      major_notes.insert(major_notes.end(), var_notes.begin(), var_notes.end());
      major_textures.insert(
          config.variations[static_cast<size_t>(idx)].primary_texture);
    } else {
      minor_notes.insert(minor_notes.end(), var_notes.begin(), var_notes.end());
      minor_textures.insert(
          config.variations[static_cast<size_t>(idx)].primary_texture);
    }
  }

  if (major_notes.empty() || minor_notes.empty()) {
    return 0.0f;
  }

  float score = 0.0f;
  int checks = 0;

  // Check 1: Register height difference.
  // Major section should typically have a different (often higher/lighter) register.
  float major_avg = averagePitch(major_notes);
  float minor_avg = averagePitch(minor_notes);
  float register_diff = std::abs(major_avg - minor_avg);

  ++checks;
  // 6+ semitones difference = full credit; less = proportional.
  constexpr float kRegisterDiffTarget = 6.0f;
  score += std::min(register_diff / kRegisterDiffTarget, 1.0f);

  // Check 2: Note density difference.
  // Count major and minor total ticks for density calculation.
  int major_count = 0;
  int minor_count = 0;
  Tick major_ticks = 0;
  Tick minor_ticks = 0;

  for (int idx = 0; idx < static_cast<int>(config.variations.size()); ++idx) {
    if (config.variations[static_cast<size_t>(idx)].is_major_section) {
      ++major_count;
    } else {
      ++minor_count;
    }
  }
  major_ticks = static_cast<Tick>(major_count) * bass_length;
  minor_ticks = static_cast<Tick>(minor_count) * bass_length;

  float major_density = noteDensity(major_notes, major_ticks);
  float minor_density = noteDensity(minor_notes, minor_ticks);

  ++checks;
  if (major_density > 0.0f || minor_density > 0.0f) {
    float max_density = std::max(major_density, minor_density);
    float density_diff = std::abs(major_density - minor_density) / max_density;
    score += std::min(density_diff * 2.0f, 1.0f);  // Scale up: 50% diff = full credit.
  }

  // Check 3: Texture variety difference.
  // The major and minor sections should use different texture types.
  ++checks;
  // Count textures exclusive to each section.
  int exclusive_textures = 0;
  for (const auto& tex : major_textures) {
    if (minor_textures.find(tex) == minor_textures.end()) {
      ++exclusive_textures;
    }
  }
  for (const auto& tex : minor_textures) {
    if (major_textures.find(tex) == major_textures.end()) {
      ++exclusive_textures;
    }
  }

  int total_textures = static_cast<int>(major_textures.size()) +
                       static_cast<int>(minor_textures.size());
  if (total_textures > 0) {
    score += static_cast<float>(exclusive_textures) / static_cast<float>(total_textures);
  }

  if (checks == 0) return 0.0f;
  return score / static_cast<float>(checks);
}

/// @brief Measure voice switch frequency (reasonable range).
///
/// Counts how often the melody line switches between upper and lower register
/// halves. A switch is when consecutive notes cross the median pitch.
/// Score = 1.0 if frequency is in [0.1, 0.5] per beat, lower otherwise.
///
/// @param all_notes All notes sorted by start_tick.
/// @param config Chaconne config for total piece information.
/// @param ground_bass Ground bass for piece length calculation.
/// @return Score in [0.0, 1.0].
float computeVoiceSwitchFrequency(const std::vector<NoteEvent>& all_notes,
                                  const ChaconneConfig& config,
                                  const GroundBass& ground_bass) {
  if (all_notes.size() < 2 || ground_bass.isEmpty()) {
    return 0.0f;
  }

  Tick bass_length = ground_bass.getLengthTicks();
  if (bass_length == 0) {
    return 0.0f;
  }

  // Calculate total beats in the piece.
  Tick total_ticks =
      static_cast<Tick>(config.variations.size()) * bass_length;
  float total_beats = static_cast<float>(total_ticks) / static_cast<float>(kTicksPerBeat);

  if (total_beats <= 0.0f) {
    return 0.0f;
  }

  // Find the median pitch.
  std::vector<uint8_t> pitches;
  pitches.reserve(all_notes.size());
  for (const auto& note : all_notes) {
    pitches.push_back(note.pitch);
  }
  std::sort(pitches.begin(), pitches.end());
  uint8_t median_pitch = pitches[pitches.size() / 2];

  // Count register switches: transitions across the median pitch.
  int switch_count = 0;
  bool prev_above = (all_notes[0].pitch > median_pitch);

  for (size_t idx = 1; idx < all_notes.size(); ++idx) {
    bool curr_above = (all_notes[idx].pitch > median_pitch);
    if (curr_above != prev_above) {
      ++switch_count;
    }
    prev_above = curr_above;
  }

  float frequency = static_cast<float>(switch_count) / total_beats;

  // Score = 1.0 if frequency in [0.1, 0.5], reduced outside that range.
  constexpr float kMinFreq = 0.1f;
  constexpr float kMaxFreq = 0.5f;

  if (frequency >= kMinFreq && frequency <= kMaxFreq) {
    return 1.0f;
  }

  if (frequency < kMinFreq) {
    // Too few switches -- proportional penalty.
    return (kMinFreq > 0.0f) ? (frequency / kMinFreq) : 0.0f;
  }

  // frequency > kMaxFreq: too many switches -- proportional penalty.
  // At 2x the max, score = 0.
  float excess = frequency - kMaxFreq;
  float penalty = excess / kMaxFreq;
  float score = 1.0f - penalty;
  if (score < 0.0f) score = 0.0f;

  return score;
}

}  // namespace

// ===========================================================================
// ChaconneAnalysisResult methods
// ===========================================================================

bool ChaconneAnalysisResult::isPass() const {
  // Instant-FAIL checks (exact values required).
  if (ground_bass_integrity != 1.0f) return false;
  if (role_order_score != 1.0f) return false;
  if (climax_presence_score != 1.0f) return false;
  if (implied_polyphony_score != 1.0f) return false;

  // Threshold checks.
  if (variation_diversity < 0.7f) return false;
  if (texture_transition_score < 0.5f) return false;
  if (section_balance < 0.7f) return false;
  if (major_section_separation < 0.6f) return false;

  // voice_switch_frequency: no hard threshold, but include in pass check
  // as a reasonable minimum. A score of 0.0 indicates problematic content.
  if (voice_switch_frequency <= 0.0f) return false;

  return true;
}

std::vector<std::string> ChaconneAnalysisResult::getFailures() const {
  std::vector<std::string> failures;

  auto formatMetric = [](const char* name, float value, const char* requirement) {
    std::ostringstream oss;
    oss << name << ": " << value << " (" << requirement << ")";
    return oss.str();
  };

  // Instant-FAIL checks.
  if (ground_bass_integrity != 1.0f) {
    failures.push_back(
        formatMetric("ground_bass_integrity", ground_bass_integrity, "must be 1.0"));
  }
  if (role_order_score != 1.0f) {
    failures.push_back(
        formatMetric("role_order_score", role_order_score, "must be 1.0"));
  }
  if (climax_presence_score != 1.0f) {
    failures.push_back(
        formatMetric("climax_presence_score", climax_presence_score, "must be 1.0"));
  }
  if (implied_polyphony_score != 1.0f) {
    failures.push_back(
        formatMetric("implied_polyphony_score", implied_polyphony_score, "must be 1.0"));
  }

  // Threshold checks.
  if (variation_diversity < 0.7f) {
    failures.push_back(
        formatMetric("variation_diversity", variation_diversity, "threshold: 0.70"));
  }
  if (texture_transition_score < 0.5f) {
    failures.push_back(formatMetric("texture_transition_score", texture_transition_score,
                                    "threshold: 0.50"));
  }
  if (section_balance < 0.7f) {
    failures.push_back(
        formatMetric("section_balance", section_balance, "threshold: 0.70"));
  }
  if (major_section_separation < 0.6f) {
    failures.push_back(formatMetric("major_section_separation", major_section_separation,
                                    "threshold: 0.60"));
  }
  if (voice_switch_frequency <= 0.0f) {
    failures.push_back(formatMetric("voice_switch_frequency", voice_switch_frequency,
                                    "must be > 0.0"));
  }

  return failures;
}

std::string ChaconneAnalysisResult::summary() const {
  std::ostringstream oss;
  oss << "ChaconneAnalysisResult (" << (isPass() ? "PASS" : "FAIL") << ")\n";
  oss << "  Instant-FAIL metrics:\n";
  oss << "    ground_bass_integrity:     " << ground_bass_integrity
      << (ground_bass_integrity == 1.0f ? " [OK]" : " [FAIL]") << "\n";
  oss << "    role_order_score:          " << role_order_score
      << (role_order_score == 1.0f ? " [OK]" : " [FAIL]") << "\n";
  oss << "    climax_presence_score:     " << climax_presence_score
      << (climax_presence_score == 1.0f ? " [OK]" : " [FAIL]") << "\n";
  oss << "    implied_polyphony_score:   " << implied_polyphony_score
      << (implied_polyphony_score == 1.0f ? " [OK]" : " [FAIL]") << "\n";
  oss << "  Threshold metrics:\n";
  oss << "    variation_diversity:       " << variation_diversity
      << (variation_diversity >= 0.7f ? " [OK]" : " [FAIL]") << "\n";
  oss << "    texture_transition_score:  " << texture_transition_score
      << (texture_transition_score >= 0.5f ? " [OK]" : " [FAIL]") << "\n";
  oss << "    section_balance:           " << section_balance
      << (section_balance >= 0.7f ? " [OK]" : " [FAIL]") << "\n";
  oss << "    major_section_separation:  " << major_section_separation
      << (major_section_separation >= 0.6f ? " [OK]" : " [FAIL]") << "\n";
  oss << "    voice_switch_frequency:    " << voice_switch_frequency
      << (voice_switch_frequency > 0.0f ? " [OK]" : " [FAIL]") << "\n";
  oss << "  Diagnostics:\n";
  oss << "    implied_voice_count_avg:   " << implied_voice_count_avg << "\n";
  oss << "    accumulate_count:          " << accumulate_count << "\n";

  return oss.str();
}

// ===========================================================================
// Main analysis entry point
// ===========================================================================

ChaconneAnalysisResult analyzeChaconne(const std::vector<Track>& tracks,
                                       const ChaconneConfig& config,
                                       const GroundBass& ground_bass) {
  ChaconneAnalysisResult result;

  auto all_notes = collectAllNotes(tracks);

  // Count Accumulate variations for diagnostic output.
  for (const auto& var : config.variations) {
    if (var.role == VariationRole::Accumulate) {
      ++result.accumulate_count;
    }
  }

  // Instant-FAIL metrics.
  result.ground_bass_integrity =
      computeGroundBassIntegrity(all_notes, config, ground_bass);
  result.role_order_score = computeRoleOrderScore(config);
  result.climax_presence_score = computeClimaxPresenceScore(config);
  result.implied_polyphony_score = computeImpliedPolyphonyScore(
      all_notes, config, ground_bass, result.implied_voice_count_avg);

  // Threshold metrics (config-derived).
  result.variation_diversity = computeVariationDiversity(config);
  result.texture_transition_score = computeTextureTransitionScore(config);
  result.section_balance = computeSectionBalance(config);

  // Threshold metrics (note-derived).
  result.major_section_separation =
      computeMajorSectionSeparation(all_notes, config, ground_bass);
  result.voice_switch_frequency =
      computeVoiceSwitchFrequency(all_notes, config, ground_bass);

  return result;
}

}  // namespace bach
