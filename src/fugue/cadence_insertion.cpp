// Cadence detection and insertion: harmonic tension release management.

#include "fugue/cadence_insertion.h"

#include <algorithm>
#include <cmath>
#include <random>

#include "core/note_source.h"
#include "core/pitch_utils.h"
#include "core/rng_util.h"
#include "fugue/cadence_vocabulary.h"
#include "fugue/voice_registers.h"

namespace bach {

namespace {

/// @brief Search window radius in ticks for cadential resolution detection.
/// 2 beats = 960 ticks at 480 ticks/beat.
constexpr Tick kCadenceWindowRadius = kTicksPerBeat * 2;

/// @brief Minimum gap between inserted cadences to avoid clustering.
/// Set to 8 bars to prevent over-saturation.
constexpr Tick kMinCadenceSpacing = kTicksPerBar * 8;

/// @brief Duration of the dominant note in the cadential formula.
constexpr Tick kCadenceDominantDuration = kTicksPerBeat;

/// @brief Duration of the resolution note in the cadential formula.
constexpr Tick kCadenceResolutionDuration = kTicksPerBeat * 2;

/// @brief Organ velocity for cadence notes (consistent with fugue).
constexpr uint8_t kCadenceVelocity = 80;

/// @brief Get the leading tone pitch class for a given key.
/// @param key The tonic key.
/// @return Pitch class of the leading tone (tonic - 1 semitone, mod 12).
int leadingTonePitchClass(Key key) {
  return (static_cast<int>(key) + 11) % 12;
}

/// @brief Get the dominant pitch class (scale degree 5) for a given key.
/// @param key The tonic key.
/// @return Pitch class of the dominant (tonic + 7 semitones, mod 12).
int dominantPitchClass(Key key) {
  return (static_cast<int>(key) + 7) % 12;
}

/// @brief Get the submediant pitch class (scale degree 6) for a given key.
/// @param key The tonic key.
/// @param is_minor True for minor key (flat 6th = +8), false for major (+9).
/// @return Pitch class of the sixth degree.
int submediantPitchClass(Key key, bool is_minor) {
  int offset = is_minor ? 8 : 9;
  return (static_cast<int>(key) + offset) % 12;
}

/// @brief Find the nearest MIDI pitch with a given pitch class within a range.
/// @param target_pc Target pitch class (0-11).
/// @param reference Reference MIDI pitch (for proximity).
/// @param range_lo Minimum MIDI pitch.
/// @param range_hi Maximum MIDI pitch.
/// @return Nearest MIDI pitch with the target pitch class, clamped to range.
uint8_t nearestPitchInRange(int target_pc, uint8_t reference,
                            uint8_t range_lo, uint8_t range_hi) {
  int best = -1;
  int best_dist = 999;
  for (int oct = 0; oct <= 10; ++oct) {
    int candidate = target_pc + oct * 12;
    if (candidate < static_cast<int>(range_lo)) continue;
    if (candidate > static_cast<int>(range_hi)) break;
    int dist = std::abs(candidate - static_cast<int>(reference));
    if (dist < best_dist) {
      best_dist = dist;
      best = candidate;
    }
  }
  if (best < 0) {
    // Fallback: clamp to range boundary.
    return clampPitch(target_pc + 36, range_lo, range_hi);
  }
  return static_cast<uint8_t>(best);
}

}  // namespace

bool hasCadentialResolution(const std::vector<NoteEvent>& notes, Tick tick, Key key) {
  int tonic_pc = static_cast<int>(key);
  int leading_pc = leadingTonePitchClass(key);

  // Define the search window: [tick - 2 beats, tick + 2 beats].
  Tick window_start = (tick > kCadenceWindowRadius) ? tick - kCadenceWindowRadius : 0;
  Tick window_end = tick + kCadenceWindowRadius;

  // Collect leading tone and tonic occurrences within the window.
  struct PitchOccurrence {
    Tick start_tick;
    uint8_t voice;
  };

  std::vector<PitchOccurrence> leading_tones;
  std::vector<PitchOccurrence> tonic_notes;

  for (const auto& note : notes) {
    if (note.start_tick < window_start || note.start_tick > window_end) continue;

    int note_pc = getPitchClass(note.pitch);
    if (note_pc == leading_pc) {
      leading_tones.push_back({note.start_tick, note.voice});
    } else if (note_pc == tonic_pc) {
      tonic_notes.push_back({note.start_tick, note.voice});
    }
  }

  // Check for leading tone -> tonic resolution: the leading tone must precede
  // the tonic in time (any voice). Allow cross-voice resolution as well since
  // Bach frequently resolves the leading tone in a different voice.
  for (const auto& lead : leading_tones) {
    for (const auto& tonic : tonic_notes) {
      if (tonic.start_tick > lead.start_tick &&
          tonic.start_tick <= lead.start_tick + kCadenceWindowRadius) {
        return true;
      }
    }
  }

  return false;
}

bool isInSubjectEntry(const FugueStructure& structure, Tick tick) {
  for (const auto& section : structure.sections) {
    if (section.type == SectionType::Exposition ||
        section.type == SectionType::MiddleEntry ||
        section.type == SectionType::Stretto) {
      if (tick >= section.start_tick && tick < section.end_tick) {
        return true;
      }
    }
  }
  return false;
}

std::vector<CadenceDeficiency> detectCadenceDeficiencies(
    const std::vector<NoteEvent>& notes,
    const FugueStructure& structure,
    Key key,
    Tick total_duration,
    const CadenceDetectionConfig& config) {
  std::vector<CadenceDeficiency> deficiencies;

  if (total_duration == 0 || notes.empty()) return deficiencies;

  Tick max_gap_ticks = static_cast<Tick>(config.max_bars_without_cadence) * kTicksPerBar;
  Tick scan_step = static_cast<Tick>(config.scan_window_bars) * kTicksPerBar;

  // Step 1: Find all existing cadential resolution points.
  std::vector<Tick> existing_cadences;
  for (Tick scan_tick = 0; scan_tick < total_duration; scan_tick += kTicksPerBar) {
    if (hasCadentialResolution(notes, scan_tick, key)) {
      existing_cadences.push_back(scan_tick);
    }
  }

  // Step 2: Find gaps between consecutive cadences that exceed the threshold.
  // Treat tick 0 and total_duration as implicit cadence points.
  std::vector<Tick> boundaries;
  boundaries.push_back(0);
  for (Tick cad : existing_cadences) {
    boundaries.push_back(cad);
  }
  boundaries.push_back(total_duration);

  // Remove duplicates and sort.
  std::sort(boundaries.begin(), boundaries.end());
  boundaries.erase(std::unique(boundaries.begin(), boundaries.end()), boundaries.end());

  Tick last_inserted = 0;
  for (size_t idx = 1; idx < boundaries.size(); ++idx) {
    Tick gap = boundaries[idx] - boundaries[idx - 1];
    if (gap <= max_gap_ticks) continue;

    // This gap is too long. Find a good insertion point.
    // Prefer the middle of the gap, snapped to a bar boundary.
    Tick gap_start = boundaries[idx - 1];
    Tick gap_end = boundaries[idx];
    Tick mid = gap_start + gap / 2;

    // Snap to bar boundary (end of bar).
    Tick bar_aligned = (mid / kTicksPerBar) * kTicksPerBar;
    if (bar_aligned < gap_start + scan_step) {
      bar_aligned = gap_start + scan_step;
      bar_aligned = (bar_aligned / kTicksPerBar) * kTicksPerBar;
    }
    if (bar_aligned >= gap_end) {
      bar_aligned = gap_end - kTicksPerBar;
      if (bar_aligned < gap_start) bar_aligned = gap_start;
      bar_aligned = (bar_aligned / kTicksPerBar) * kTicksPerBar;
    }

    // Skip if this point is within a subject entry (structural articulation).
    if (isInSubjectEntry(structure, bar_aligned)) continue;

    // Skip if too close to the last inserted cadence.
    if (bar_aligned > 0 && bar_aligned - last_inserted < kMinCadenceSpacing &&
        last_inserted > 0) {
      continue;
    }

    CadenceDeficiency deficiency;
    deficiency.region_start = gap_start;
    deficiency.region_end = gap_end;
    deficiency.insertion_tick = bar_aligned;
    deficiencies.push_back(deficiency);
    last_inserted = bar_aligned;

    // If the remaining gap after insertion is still too large, scan further.
    // Insert the insertion tick as a new boundary so subsequent gaps are checked.
    // (This is handled implicitly by the loop continuing through boundaries.)
  }

  return deficiencies;
}

int insertCadentialFormulas(
    std::vector<NoteEvent>& notes,
    const std::vector<CadenceDeficiency>& deficiencies,
    Key key,
    bool is_minor,
    VoiceId bass_voice,
    uint8_t num_voices,
    uint32_t seed,
    const CadenceDetectionConfig& config) {
  if (deficiencies.empty()) return 0;

  std::mt19937 cadence_rng(seed + 77777u);
  auto [range_lo, range_hi] = getFugueVoiceRange(bass_voice, num_voices);

  int inserted_count = 0;

  for (const auto& deficiency : deficiencies) {
    Tick insertion_tick = deficiency.insertion_tick;

    // Determine cadence type: perfect (V->I) or deceptive (V->vi).
    bool use_deceptive = rng::rollProbability(cadence_rng, config.deceptive_cadence_probability);

    // Calculate pitches.
    int dom_pc = dominantPitchClass(key);
    int tonic_pc = static_cast<int>(key);
    int resolution_pc = use_deceptive ? submediantPitchClass(key, is_minor) : tonic_pc;

    // Find a reference pitch from existing bass notes near the insertion point.
    uint8_t ref_pitch = 0;
    for (auto rit = notes.rbegin(); rit != notes.rend(); ++rit) {
      if (rit->voice == bass_voice && rit->start_tick < insertion_tick) {
        ref_pitch = rit->pitch;
        break;
      }
    }
    if (ref_pitch == 0) {
      ref_pitch = clampPitch(36 + static_cast<int>(key), range_lo, range_hi);
    }

    uint8_t dominant_pitch = nearestPitchInRange(dom_pc, ref_pitch, range_lo, range_hi);
    uint8_t resolution_pitch = nearestPitchInRange(resolution_pc, dominant_pitch,
                                                   range_lo, range_hi);

    // Ensure dominant -> resolution moves downward (typical bass motion V->I).
    // If resolution is above dominant, try an octave lower.
    if (resolution_pitch >= dominant_pitch && !use_deceptive) {
      int lower = static_cast<int>(resolution_pitch) - 12;
      if (lower >= static_cast<int>(range_lo)) {
        resolution_pitch = static_cast<uint8_t>(lower);
      }
    }

    // Insert dominant note 1 beat before the insertion tick.
    Tick dom_tick = (insertion_tick >= kTicksPerBeat) ? insertion_tick - kTicksPerBeat
                                                     : 0;

    NoteEvent dom_note;
    dom_note.start_tick = dom_tick;
    dom_note.duration = kCadenceDominantDuration;
    dom_note.pitch = dominant_pitch;
    dom_note.velocity = kCadenceVelocity;
    dom_note.voice = bass_voice;
    dom_note.source = BachNoteSource::EpisodeMaterial;
    notes.push_back(dom_note);

    // Insert resolution note at the insertion tick.
    NoteEvent res_note;
    res_note.start_tick = insertion_tick;
    res_note.duration = kCadenceResolutionDuration;
    res_note.pitch = resolution_pitch;
    res_note.velocity = kCadenceVelocity;
    res_note.voice = bass_voice;
    res_note.source = BachNoteSource::EpisodeMaterial;
    notes.push_back(res_note);

    ++inserted_count;
  }

  return inserted_count;
}

int ensureCadentialCoverage(
    std::vector<NoteEvent>& notes,
    const FugueStructure& structure,
    Key key,
    bool is_minor,
    VoiceId bass_voice,
    uint8_t num_voices,
    Tick total_duration,
    uint32_t seed,
    const CadenceDetectionConfig& config) {
  auto deficiencies = detectCadenceDeficiencies(notes, structure, key,
                                                total_duration, config);
  if (deficiencies.empty()) return 0;

  return insertCadentialFormulas(notes, deficiencies, key, is_minor,
                                bass_voice, num_voices, seed, config);
}

bool isInCadenceZone(Tick tick, const std::vector<Tick>& cadence_ticks,
                     Tick window_beats) {
  Tick window = window_beats * kTicksPerBeat;
  for (Tick cad : cadence_ticks) {
    if (cad < window) continue;
    if (tick >= cad - window && tick < cad) return true;
  }
  return false;
}

std::vector<Tick> extractCadenceTicks(const CadencePlan& plan) {
  std::vector<Tick> ticks;
  ticks.reserve(plan.points.size());
  for (const auto& pt : plan.points) {
    ticks.push_back(pt.tick);
  }
  std::sort(ticks.begin(), ticks.end());
  return ticks;
}

int applyCadenceApproachToVoices(
    std::vector<NoteEvent>& notes,
    const CadencePlan& plan,
    Key /* key */,
    bool /* is_minor */,
    uint8_t num_voices,
    uint32_t seed) {
  if (plan.points.empty() || notes.empty() || num_voices == 0) return 0;

  std::mt19937 approach_rng(seed ^ 0xCADE0001u);
  int shaped_count = 0;

  // Cadence window: 2 beats before each cadence tick.
  constexpr Tick kWindowBeats = 2;
  constexpr Tick kWindowTicks = kWindowBeats * kTicksPerBeat;

  // Voice range lookup for pitch clamping.
  auto [sop_lo, sop_hi] = getFugueVoiceRange(0, num_voices);
  auto [bass_lo, bass_hi] = getFugueVoiceRange(num_voices - 1, num_voices);

  for (const auto& cadence_point : plan.points) {
    Tick cadence_tick = cadence_point.tick;
    CadenceType ctype = cadence_point.type;
    if (cadence_tick < kWindowTicks) continue;

    Tick window_start = cadence_tick - kWindowTicks;

    // Find matching approaches for this cadence type.
    auto [approaches, approach_count] = getCadenceApproaches(ctype);
    if (approaches == nullptr || approach_count == 0) continue;

    // Select approach randomly.
    size_t approach_idx = approach_rng() % approach_count;
    const CadenceApproach& approach = approaches[approach_idx];

    // Collect soprano (voice 0) and bass (last voice) notes in the window.
    VoiceId bass_voice = num_voices - 1;
    std::vector<size_t> sop_indices;
    std::vector<size_t> bass_indices;
    for (size_t i = 0; i < notes.size(); ++i) {
      if (notes[i].start_tick >= window_start &&
          notes[i].start_tick < cadence_tick) {
        if (notes[i].voice == 0) {
          // Skip structurally protected notes.
          auto prot = getProtectionLevel(notes[i].source);
          if (prot <= ProtectionLevel::SemiImmutable) continue;
          sop_indices.push_back(i);
        } else if (notes[i].voice == bass_voice) {
          auto prot = getProtectionLevel(notes[i].source);
          if (prot <= ProtectionLevel::SemiImmutable) continue;
          bass_indices.push_back(i);
        }
      }
    }

    // Sort by start_tick.
    auto tick_cmp = [&notes](size_t a, size_t b) {
      return notes[a].start_tick < notes[b].start_tick;
    };
    std::sort(sop_indices.begin(), sop_indices.end(), tick_cmp);
    std::sort(bass_indices.begin(), bass_indices.end(), tick_cmp);

    bool shaped = false;

    // Shape soprano: apply approach.soprano_approach to the last N notes.
    if (!sop_indices.empty() && approach.soprano_len > 0) {
      size_t apply_count = std::min(static_cast<size_t>(approach.soprano_len),
                                     sop_indices.size());
      size_t start_from = sop_indices.size() - apply_count;
      // Get reference pitch from the note before the window.
      uint8_t ref_pitch = notes[sop_indices[start_from]].pitch;

      for (size_t j = 0; j < apply_count; ++j) {
        size_t note_idx = sop_indices[start_from + j];
        int degree_step = approach.soprano_approach[j];
        // Convert degree step to approximate semitones (diatonic step ~= 2 semitones).
        int semitone_shift = degree_step * 2;
        int new_pitch = static_cast<int>(ref_pitch) + semitone_shift;
        new_pitch = clampPitch(new_pitch, sop_lo, sop_hi);
        notes[note_idx].pitch = static_cast<uint8_t>(new_pitch);
        notes[note_idx].source = BachNoteSource::CadenceApproach;
        ref_pitch = static_cast<uint8_t>(new_pitch);
      }
      shaped = true;
    }

    // Shape bass: apply approach.bass_approach to the last N notes.
    if (!bass_indices.empty() && approach.bass_len > 0) {
      size_t apply_count = std::min(static_cast<size_t>(approach.bass_len),
                                     bass_indices.size());
      size_t start_from = bass_indices.size() - apply_count;
      uint8_t ref_pitch = notes[bass_indices[start_from]].pitch;

      for (size_t j = 0; j < apply_count; ++j) {
        size_t note_idx = bass_indices[start_from + j];
        int degree_step = approach.bass_approach[j];
        int semitone_shift = degree_step * 2;
        int new_pitch = static_cast<int>(ref_pitch) + semitone_shift;
        new_pitch = clampPitch(new_pitch, bass_lo, bass_hi);
        notes[note_idx].pitch = static_cast<uint8_t>(new_pitch);
        notes[note_idx].source = BachNoteSource::CadenceApproach;
        ref_pitch = static_cast<uint8_t>(new_pitch);
      }
      shaped = true;
    }

    if (shaped) ++shaped_count;
  }

  return shaped_count;
}

}  // namespace bach
