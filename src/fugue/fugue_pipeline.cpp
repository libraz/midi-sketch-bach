// Fugue pipeline: constraint-driven generation with 4-step architecture.
// Replaces the monolithic generateFugue() with buildMaterial -> planStructure
// -> generateSections -> finalize.

#include "fugue/fugue_pipeline.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <climits>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <map>
#include <optional>
#include <random>

#include "constraint/constraint_state.h"
#include "constraint/obligation_analyzer.h"
#include "core/markov_tables.h"
#include "core/interval.h"
#include "core/note_source.h"
#include "core/pitch_utils.h"
#include "core/rng_util.h"
#include "core/scale.h"
#include "core/form_profile.h"
#include "core/note_creator.h"
#include "counterpoint/bach_rule_evaluator.h"
#include "counterpoint/collision_resolver.h"
#include "counterpoint/counterpoint_state.h"
#include "fugue/answer.h"
#include "fugue/archetype_policy.h"
#include "fugue/cadence_insertion.h"
#include "fugue/cadence_plan.h"
#include "fugue/countersubject.h"
#include "fugue/episode.h"
#include "fugue/exposition.h"
#include "fugue/middle_entry.h"
#include "fugue/motif_pool.h"
#include "fugue/stretto.h"
#include "fugue/subject.h"
#include "fugue/subject_validator.h"
#include "fugue/tonal_plan.h"
#include "fugue/voice_registers.h"
#include "harmony/chord_tone_utils.h"
#include "harmony/chord_types.h"
#include "harmony/harmonic_rhythm.h"
#include "harmony/modulation_plan.h"
#include "organ/manual.h"
#include "organ/organ_techniques.h"
#include "organ/registration.h"

namespace bach {

namespace {

// ===========================================================================
// Internal data structures
// ===========================================================================

/// @brief All pre-computed material for fugue generation.
struct FugueMaterial {
  Subject subject;
  int subject_attempts = 0;
  SubjectConstraintProfile constraint_profile;
  Answer answer;
  Countersubject countersubject;
  Countersubject countersubject_2;
  MotifPool motif_pool;
};

/// @brief Planned section in the fugue structure.
struct PlannedSection {
  SectionType type = SectionType::Exposition;
  FuguePhase phase = FuguePhase::Establish;
  Key key = Key::C;
  Key prev_key = Key::C;            // Key before this section (for modulation tracking)
  Tick start_tick = 0;
  Tick end_tick = 0;
  int episode_index = -1;
  VoiceId entry_voice = 0;
  bool is_false_entry = false;
  bool companion_needed = false;     // True for MiddleEntry sections needing companion counterpoint
  float energy_level = 0.5f;
};

/// @brief Complete structural plan for the fugue.
struct FuguePlan {
  TonalPlan tonal_plan;
  ModulationPlan modulation_plan;
  HarmonicTimeline detailed_timeline;
  Tick estimated_duration = 0;
  std::vector<PlannedSection> sections;
  CadencePlan cadence_plan;
};

// ===========================================================================
// Constants
// ===========================================================================

/// @brief Minimum number of voices for a fugue.
constexpr uint8_t kMinVoices = 2;

/// @brief Maximum number of voices for a fugue.
constexpr uint8_t kMaxVoices = 5;

/// @brief Minimum total fugue length in bars.
constexpr Tick kMinFugueBars = 12;

/// @brief Duration of coda in bars (expanded from 2 to 4 for richer endings).
constexpr Tick kCodaBars = 4;

/// @brief Organ velocity (pipe organs have no velocity sensitivity).
constexpr uint8_t kOrganVelocity = 80;

/// @brief Duration of the dominant pedal in bars (placed before stretto).
constexpr Tick kDominantPedalBars = 4;

// ===========================================================================
// Helper functions (shared with fugue_generator.cpp)
// ===========================================================================

/// @brief Clamp voice count to valid range [2, 5].
/// @param num_voices Raw voice count from configuration.
/// @return Clamped voice count.
uint8_t clampVoiceCount(uint8_t num_voices) {
  if (num_voices < kMinVoices) return kMinVoices;
  if (num_voices > kMaxVoices) return kMaxVoices;
  return num_voices;
}

/// @brief Human-readable name for an organ manual.
/// @param manual OrganManual enum value.
/// @return Descriptive string for the manual.
const char* manualTrackName(OrganManual manual) {
  switch (manual) {
    case OrganManual::Great:   return "Manual I (Great)";
    case OrganManual::Swell:   return "Manual II (Swell)";
    case OrganManual::Positiv: return "Manual III (Positiv)";
    case OrganManual::Pedal:   return "Pedal";
  }
  return "Unknown Manual";
}

/// @brief Create MIDI tracks for an organ fugue using assignManuals().
///
/// Delegates to the organ manual assignment system (organ/manual.h) which
/// handles voice-to-manual routing for all voice counts (2-5):
///   2 voices: Great + Swell
///   3 voices: Great + Swell + Positiv
///   4 voices: Great + Swell + Positiv + Pedal
///   5 voices: Great (x2) + Swell + Positiv + Pedal
///
/// @param num_voices Number of voices (2-5).
/// @return Vector of Track objects with channel/program/name configured.
std::vector<Track> createOrganTracks(uint8_t num_voices) {
  auto assignments = assignManuals(num_voices, FormType::Fugue);

  std::vector<Track> tracks;
  tracks.reserve(assignments.size());

  for (const auto& assignment : assignments) {
    Track track;
    track.channel = channelForAssignment(assignment);
    track.program = programForAssignment(assignment);
    track.name = manualTrackName(assignment.manual);
    tracks.push_back(track);
  }

  return tracks;
}

/// @brief Distribute notes into tracks by voice_id.
///
/// Each note's voice field determines which track it belongs to.
/// Notes with voice_id >= tracks.size() are silently discarded.
///
/// @param notes All collected notes from all fugue sections.
/// @param tracks Output tracks (notes appended to matching track).
void assignNotesToTracks(const std::vector<NoteEvent>& notes,
                         std::vector<Track>& tracks) {
  for (const auto& note : notes) {
    if (note.voice < tracks.size()) {
      tracks[note.voice].notes.push_back(note);
    }
  }
}

/// @brief Sort notes in each track by start_tick for MIDI output.
/// @param tracks Tracks whose notes will be sorted in place.
void sortTrackNotes(std::vector<Track>& tracks) {
  for (auto& track : tracks) {
    std::sort(track.notes.begin(), track.notes.end(),
              [](const NoteEvent& lhs, const NoteEvent& rhs) {
                if (lhs.start_tick != rhs.start_tick) {
                  return lhs.start_tick < rhs.start_tick;
                }
                return lhs.pitch < rhs.pitch;
              });
  }
}

/// @brief Generate a valid subject with retry logic.
///
/// Attempts to generate a subject that passes the SubjectValidator's
/// quality threshold (composite >= 0.7). Each attempt uses a different
/// seed derived from the base seed. If all attempts fail, returns the
/// last generated subject as a best-effort fallback.
///
/// @param config Fugue configuration.
/// @param attempts_out Output: number of attempts used.
/// @return Generated Subject (may not be acceptable if all retries failed).
Subject generateValidSubject(const FugueConfig& config, int& attempts_out) {
  SubjectGenerator gen;
  SubjectValidator validator;

  Subject best_subject;
  float best_composite = -1.0f;

  for (int attempt = 0; attempt < config.max_subject_retries; ++attempt) {
    // Wrapping on uint32_t overflow is safe: any value is a valid RNG seed.
    // With max_subject_retries <= ~10, overflow does not occur in practice.
    uint32_t attempt_seed = config.seed + static_cast<uint32_t>(attempt) * 1000003u;
    Subject subject = gen.generate(config, attempt_seed);
    SubjectScore score = validator.evaluate(subject);
    float composite = score.composite();

    attempts_out = attempt + 1;

    if (score.isAcceptable()) {
      return subject;
    }

    // Track best attempt for fallback.
    if (composite > best_composite) {
      best_composite = composite;
      best_subject = subject;
    }
  }

  // Fallback: return the best attempt even if below threshold.
  return best_subject;
}

/// @brief Generate pedal point notes for a given pitch and duration.
///
/// The pedal is split into bar-length tied notes for better MIDI compatibility
/// (many MIDI renderers handle shorter notes more reliably than very long ones).
///
/// @param pitch MIDI pitch of the pedal note.
/// @param start_tick Start position in ticks.
/// @param duration Total duration of the pedal in ticks.
/// @param voice_id Voice for the pedal (lowest voice).
/// @return Vector of pedal point NoteEvents, each one bar long (or shorter
///         for the final segment).
std::vector<NoteEvent> generatePedalPoint(uint8_t pitch, Tick start_tick,
                                          Tick duration, VoiceId voice_id) {
  std::vector<NoteEvent> notes;
  Tick remaining = duration;
  Tick tick = start_tick;
  while (remaining > 0) {
    Tick note_dur = std::min(remaining, static_cast<Tick>(kTicksPerBar));
    NoteEvent evt;
    evt.pitch = pitch;
    evt.start_tick = tick;
    evt.duration = note_dur;
    evt.velocity = kOrganVelocity;
    evt.voice = voice_id;
    evt.source = BachNoteSource::PedalPoint;
    notes.push_back(evt);
    tick += note_dur;
    remaining -= note_dur;
  }
  return notes;
}

/// @brief Remove notes from the lowest voice that overlap with a pedal region.
///
/// Any existing note in the specified voice whose time interval intersects
/// [region_start, region_end) is removed, making room for the pedal point.
/// This catches notes that start before the region but extend into it.
///
/// @param all_notes The collection of all notes (modified in place).
/// @param voice_id Voice whose notes should be removed.
/// @param region_start Start tick of the pedal region (inclusive).
/// @param region_end End tick of the pedal region (exclusive).
void removeLowestVoiceNotes(std::vector<NoteEvent>& all_notes,
                            VoiceId voice_id,
                            Tick region_start, Tick region_end) {
  all_notes.erase(
      std::remove_if(all_notes.begin(), all_notes.end(),
                     [voice_id, region_start, region_end](const NoteEvent& evt) {
                       if (evt.voice != voice_id) return false;
                       Tick note_end = evt.start_tick + evt.duration;
                       return evt.start_tick < region_end && note_end > region_start;
                     }),
      all_notes.end());
}

/// @brief Get tonic bass pitch adjusted for the actual lowest voice range.
///
/// For 4+ voices (pedaliter), returns the standard pedal-register tonic (octave 2).
/// For 2-3 voices (manualiter), octave-shifts to fit the lowest voice range
/// per Baroque manualiter practice (BWV 552/2, 541/2).
static uint8_t tonicBassPitchForVoices(Key key, uint8_t num_voices) {
  auto [lo, hi] = getFugueVoiceRange(num_voices - 1, num_voices);
  int base = 36 + static_cast<int>(key);  // Octave 2 default
  int center = (static_cast<int>(lo) + static_cast<int>(hi)) / 2;
  int shift = nearestOctaveShift(center - base);
  int adjusted = base + shift;
  return clampPitch(adjusted, lo, hi);
}

/// @brief Extract the last pitch played by a specific voice before a given tick.
/// @param notes All fugue notes (sorted by tick).
/// @param before_tick Only consider notes starting before this tick.
/// @param voice Target voice ID.
/// @return Last pitch (0 if no notes found for this voice).
static uint8_t extractVoiceLastPitch(
    const std::vector<NoteEvent>& notes, Tick before_tick, VoiceId voice) {
  uint8_t last_pitch = 0;
  for (auto iter = notes.rbegin(); iter != notes.rend(); ++iter) {
    if (iter->voice == voice && iter->start_tick < before_tick) {
      last_pitch = iter->pitch;
      break;
    }
  }
  return last_pitch;
}

/// @brief Find the nearest chord tone to a given pitch within a max distance.
/// @param pitch Source pitch.
/// @param target_pc Target pitch class (0-11).
/// @param max_dist Maximum semitone distance.
/// @return Nearest pitch with the target pitch class, or the original pitch if none found.
static uint8_t nearestPitchWithPC(uint8_t pitch, int target_pc, int max_dist = 7) {
  int best = pitch;
  int best_dist = 999;
  for (int d = -max_dist; d <= max_dist; ++d) {
    int cand = static_cast<int>(pitch) + d;
    if (cand < 0 || cand > 127) continue;
    if (getPitchClass(static_cast<uint8_t>(cand)) == getPitchClassSigned(target_pc)) {
      if (std::abs(d) < best_dist) {
        best_dist = std::abs(d);
        best = cand;
      }
    }
  }
  return clampPitch(best, 0, 127);
}

/// @brief Create 3-stage coda notes for a richer fugue ending.
///
/// Stage 1 (bars 1-2): Subject head fragment in voice 0 over tonic pedal.
///   Upper voices play held chord tones.
/// Stage 2 (bar 3): V7-I perfect cadence progression.
///   Leading tone resolution in upper voices.
/// Stage 3 (bar 4): Final sustained tonic chord (all voices).
///   Minor keys get Picardy third (raised 3rd).
///
/// @param start_tick When the coda begins.
/// @param duration Total coda duration in ticks.
/// @param key Musical key for the tonic chord.
/// @param num_voices Number of voices.
/// @param is_minor True for minor key (Picardy third in stage 3).
/// @return Vector of coda notes.
std::vector<NoteEvent> createCodaNotes(Tick start_tick, Tick duration,
                                       Key key, uint8_t num_voices,
                                       bool is_minor = false,
                                       const uint8_t* last_pitches = nullptr) {
  std::vector<NoteEvent> notes;

  int tonic_pitch = static_cast<int>(kMidiC4) + static_cast<int>(key);
  Tick bar_dur = kTicksPerBar;

  // Stage 1 (bars 1-2): Subject head motif in voice 0 + held chord tones.
  Tick stage1_dur = bar_dur * 2;
  if (stage1_dur > duration) stage1_dur = duration;

  // Voice 0: subject head fragment (rising from tonic to 5th and back).
  // When last_pitches is available, shift the motif to the nearest octave
  // to minimize the entry jump.
  {
    Tick sub_dur = kTicksPerBeat;
    int third = is_minor ? 3 : 4;
    int base_tonic = tonic_pitch;
    // Octave-shift the motif base to stay close to the previous pitch.
    if (last_pitches && last_pitches[0] > 0) {
      auto [v0_lo, v0_hi] = getFugueVoiceRange(0, num_voices);
      int prev = static_cast<int>(last_pitches[0]);
      int best_dist = std::abs(prev - base_tonic);
      for (int oct : {-12, 12, -24, 24}) {
        int cand = tonic_pitch + oct;
        if (cand < v0_lo || cand + 7 > v0_hi) continue;  // Entire motif must fit
        int dist = std::abs(prev - cand);
        if (dist < best_dist) {
          best_dist = dist;
          base_tonic = cand;
        }
      }
      // Hard gate: max leap of 12 semitones (octave)
      int leap = std::abs(prev - base_tonic);
      if (leap > 12) base_tonic = tonic_pitch;
    }
    int head_pitches[] = {base_tonic, base_tonic + 2, base_tonic + third, base_tonic + 7,
                          base_tonic + third, base_tonic + 2, base_tonic, base_tonic};
    int head_count = std::min(8, static_cast<int>(stage1_dur / sub_dur));
    auto [head_lo, head_hi] = getFugueVoiceRange(0, num_voices);
    for (int idx = 0; idx < head_count; ++idx) {
      NoteEvent note;
      note.start_tick = start_tick + static_cast<Tick>(idx) * sub_dur;
      note.duration = sub_dur;
      note.pitch = clampPitch(head_pitches[idx], head_lo, head_hi);
      note.velocity = kOrganVelocity;
      note.voice = 0;
      note.source = BachNoteSource::Coda;
      notes.push_back(note);
    }
  }

  // Other voices: approach nearest tonic chord tone from their last pitch.
  // If last_pitches is provided, use voice-leading; otherwise use fixed offsets.
  int chord_third = is_minor ? 3 : 4;
  {
    // Tonic chord pitch classes: root, 3rd, 5th.
    int tonic_pc = getPitchClass(static_cast<uint8_t>(tonic_pitch));
    int third_pc = (tonic_pitch + chord_third) % 12;
    int fifth_pc = (tonic_pitch + 7) % 12;
    int chord_pcs[] = {tonic_pc, third_pc, fifth_pc};

    int stage1_offsets[] = {7, chord_third, -12, 12};
    uint8_t count = std::min(static_cast<uint8_t>(num_voices - 1), static_cast<uint8_t>(4));
    std::sort(stage1_offsets, stage1_offsets + count, std::greater<int>());

    for (uint8_t idx = 0; idx < count; ++idx) {
      uint8_t voice_idx = 1 + idx;
      auto [vlo, vhi] = getFugueVoiceRange(voice_idx, num_voices);
      uint8_t target_pitch;

      if (last_pitches && last_pitches[voice_idx] > 0) {
        // Voice-leading: find nearest chord tone to previous pitch.
        uint8_t prev = last_pitches[voice_idx];
        int best_dist = 999;
        target_pitch = clampPitch(tonic_pitch + stage1_offsets[idx], vlo, vhi);
        for (int pc : chord_pcs) {
          uint8_t cand = nearestPitchWithPC(prev, pc, 7);
          int dist = std::abs(static_cast<int>(cand) - static_cast<int>(prev));
          if (dist < best_dist) {
            best_dist = dist;
            target_pitch = cand;
          }
        }
        target_pitch = clampPitch(static_cast<int>(target_pitch), vlo, vhi);
      } else {
        // Fallback: use fixed offsets.
        target_pitch = clampPitch(tonic_pitch + stage1_offsets[idx], vlo, vhi);
      }

      NoteEvent note;
      note.start_tick = start_tick;
      note.duration = stage1_dur;
      note.pitch = target_pitch;
      note.velocity = kOrganVelocity;
      note.voice = voice_idx;
      note.source = BachNoteSource::Coda;
      notes.push_back(note);
    }

    // Fix Stage 1 voice crossing: ensure higher voice indices have lower pitches.
    // Stage 1 held chords are sustained tones, so pitch reordering is safe.
    // IMPORTANT: preserve voice-leading proximity to last_pitches to avoid
    // large jumps at coda entry (inner voices max 7st, outer voices max 12st).
    {
      // Collect held chord notes (voice >= 1, start_tick == start_tick).
      std::vector<size_t> held_indices;
      for (size_t idx = 0; idx < notes.size(); ++idx) {
        if (notes[idx].voice >= 1 && notes[idx].start_tick == start_tick &&
            notes[idx].duration == stage1_dur) {
          held_indices.push_back(idx);
        }
      }
      // Use voice 0's maximum pitch during Stage 1 as upper bound.
      // The motif rises to base_tonic+7 and falls back, so held chords should
      // be below the peak (not the last note) to avoid forced large jumps.
      uint8_t voice0_max_pitch = 0;
      for (const auto& nev : notes) {
        if (nev.voice == 0 && nev.pitch > voice0_max_pitch) {
          voice0_max_pitch = nev.pitch;
        }
      }

      // Fix voice crossing while preserving voice-leading proximity.
      // Greedy top-down: for each voice (1, 2, ...), find the closest chord
      // tone to last_pitches that is also <= upper_bound (pitch of voice above).
      // This avoids the sort-then-reassign approach that can break proximity
      // by assigning a wrong pitch class to a voice.
      uint8_t upper_bound = (voice0_max_pitch > 0) ? voice0_max_pitch : 127;
      for (size_t idx = 0; idx < held_indices.size(); ++idx) {
        size_t note_idx = held_indices[idx];
        uint8_t voice_idx = notes[note_idx].voice;
        auto [vlo, vhi] = getFugueVoiceRange(voice_idx, num_voices);
        uint8_t current_pitch = notes[note_idx].pitch;

        // If already within bounds, keep it.
        if (current_pitch <= upper_bound && current_pitch >= vlo) {
          upper_bound = current_pitch;
          continue;
        }

        // Re-find the best chord tone: within voice range, <= upper_bound,
        // and closest to last_pitches[voice_idx] (or current pitch as fallback).
        uint8_t ref_pitch = (last_pitches && last_pitches[voice_idx] > 0)
                                ? last_pitches[voice_idx]
                                : current_pitch;
        int best_dist = 999;
        uint8_t best_pitch = clampPitch(static_cast<int>(current_pitch), vlo,
                                        std::min(static_cast<int>(upper_bound),
                                                 static_cast<int>(vhi)));
        for (int pc : chord_pcs) {
          // Scan octaves within the voice range.
          int base = pc;
          while (base < vlo) base += 12;
          for (int p = base; p <= vhi && p <= static_cast<int>(upper_bound); p += 12) {
            if (p < vlo) continue;
            int dist = std::abs(p - static_cast<int>(ref_pitch));
            if (dist < best_dist) {
              best_dist = dist;
              best_pitch = static_cast<uint8_t>(p);
            }
          }
        }
        notes[note_idx].pitch = best_pitch;
        upper_bound = best_pitch;
      }

      // Final enforcement: bubble-sort to guarantee strict descending order.
      // The greedy pass above can miss edge cases when voice-leading pulls
      // a higher voice below a lower voice's chord tone.
      for (size_t idx = 0; idx + 1 < held_indices.size(); ++idx) {
        for (size_t jdx = idx + 1; jdx < held_indices.size(); ++jdx) {
          if (notes[held_indices[jdx]].pitch >= notes[held_indices[idx]].pitch) {
            std::swap(notes[held_indices[idx]].pitch,
                      notes[held_indices[jdx]].pitch);
          }
        }
      }
    }

    // Consonance check: ensure held chord tones (voices 1+) are consonant
    // with voice 0's first note on the strong beat.  If dissonant, try
    // octave shifts of the offending voice within its register, preserving
    // the chord degree.  Only apply to the first beat (start_tick).
    if (!notes.empty()) {
      // Find voice 0's first note pitch.
      uint8_t voice0_first_pitch = 0;
      for (const auto& note : notes) {
        if (note.voice == 0 && note.start_tick == start_tick) {
          voice0_first_pitch = note.pitch;
          break;
        }
      }

      if (voice0_first_pitch > 0) {
        for (size_t idx = 0; idx < notes.size(); ++idx) {
          if (notes[idx].voice == 0) continue;
          if (notes[idx].start_tick != start_tick) continue;

          int diff = std::abs(static_cast<int>(notes[idx].pitch) -
                              static_cast<int>(voice0_first_pitch));
          int simple = interval_util::compoundToSimple(diff);
          if (interval_util::isConsonance(simple) && diff >= 3) continue;

          // Dissonant or too close: try octave shifts.
          auto [vlo, vhi] = getFugueVoiceRange(notes[idx].voice, num_voices);
          int orig_pc = getPitchClass(notes[idx].pitch);
          uint8_t best_pitch = notes[idx].pitch;
          int best_cost = INT32_MAX;

          for (int shift = -36; shift <= 36; shift += 12) {
            if (shift == 0) continue;
            int cand = static_cast<int>(notes[idx].pitch) + shift;
            if (cand < vlo || cand > vhi) continue;
            if (getPitchClass(static_cast<uint8_t>(cand)) != orig_pc) continue;

            // Check consonance with voice 0.
            int cand_diff = std::abs(cand - static_cast<int>(voice0_first_pitch));
            int cand_simple = interval_util::compoundToSimple(cand_diff);
            if (!interval_util::isConsonance(cand_simple) || cand_diff < 3) continue;

            // Check no voice crossing with adjacent voices.
            bool crosses = false;
            for (size_t jdx = 0; jdx < notes.size(); ++jdx) {
              if (jdx == idx || notes[jdx].start_tick != start_tick) continue;
              if (notes[jdx].voice == notes[idx].voice) continue;
              if (notes[jdx].voice < notes[idx].voice && cand > notes[jdx].pitch) {
                crosses = true;
                break;
              }
              if (notes[jdx].voice > notes[idx].voice && cand < notes[jdx].pitch) {
                crosses = true;
                break;
              }
            }
            if (crosses) continue;

            // Cost: voice-leading distance from original + imperfect consonance bonus.
            int cost = std::abs(shift);
            if (cand_simple == 3 || cand_simple == 4 ||
                cand_simple == 8 || cand_simple == 9) {
              cost -= 6;  // Prefer imperfect consonances.
            }
            if (cost < best_cost) {
              best_cost = cost;
              best_pitch = static_cast<uint8_t>(cand);
            }
          }

          // If octave shift didn't help, try +-3rd and +-6th within chord.
          if (best_cost == INT32_MAX) {
            int tonic_pc_local = getPitchClass(static_cast<uint8_t>(tonic_pitch));
            int third_pc_local = (tonic_pitch + chord_third) % 12;
            int fifth_pc_local = (tonic_pitch + 7) % 12;
            int chord_pcs_local[] = {tonic_pc_local, third_pc_local, fifth_pc_local};

            for (int ct_pc : chord_pcs_local) {
              if (ct_pc == orig_pc) continue;  // Same degree, already tried octave shifts.
              // Find nearest pitch with this chord PC.
              for (int oct = -24; oct <= 24; oct += 12) {
                int base = static_cast<int>(notes[idx].pitch) + oct;
                int target = base - (base % 12) + ct_pc;
                if (target < base - 6) target += 12;
                if (target > base + 6) target -= 12;
                if (target < vlo || target > vhi) continue;

                // Check consonance with voice 0.
                int cand_diff = std::abs(target - static_cast<int>(voice0_first_pitch));
                int cand_simple = interval_util::compoundToSimple(cand_diff);
                if (!interval_util::isConsonance(cand_simple) || cand_diff < 3) continue;

                // Check no crossing.
                bool crosses = false;
                for (size_t jdx = 0; jdx < notes.size(); ++jdx) {
                  if (jdx == idx || notes[jdx].start_tick != start_tick) continue;
                  if (notes[jdx].voice == notes[idx].voice) continue;
                  if (notes[jdx].voice < notes[idx].voice && target > notes[jdx].pitch) {
                    crosses = true;
                    break;
                  }
                  if (notes[jdx].voice > notes[idx].voice && target < notes[jdx].pitch) {
                    crosses = true;
                    break;
                  }
                }
                if (crosses) continue;

                // Voice-leading cost from original position.
                int cost = std::abs(target - static_cast<int>(notes[idx].pitch));
                if (cand_simple == 3 || cand_simple == 4 ||
                    cand_simple == 8 || cand_simple == 9) {
                  cost -= 3;
                }
                if (cost < best_cost) {
                  best_cost = cost;
                  best_pitch = static_cast<uint8_t>(target);
                }
              }
            }
          }

          if (best_pitch != notes[idx].pitch) {
            notes[idx].pitch = best_pitch;
          }
        }
      }
    }

    // Inter-voice consonance among held chord tones: ensure all pairs
    // have at least a minor 3rd spacing and form consonant intervals.
    // This is a soft fix -- only adjusts obviously dissonant pairs via
    // octave shift of the more flexible voice.
    for (size_t idx = 0; idx < notes.size(); ++idx) {
      if (notes[idx].voice == 0 || notes[idx].start_tick != start_tick) continue;
      for (size_t jdx = idx + 1; jdx < notes.size(); ++jdx) {
        if (notes[jdx].voice == 0 || notes[jdx].start_tick != start_tick) continue;
        int diff = std::abs(static_cast<int>(notes[idx].pitch) -
                            static_cast<int>(notes[jdx].pitch));
        if (diff >= 3) continue;  // Sufficient spacing.
        // Too close (unison or minor 2nd): shift the lower-priority voice.
        size_t fix_idx = (notes[idx].voice > notes[jdx].voice) ? idx : jdx;
        int fix_shift =
            (notes[fix_idx].pitch < notes[fix_idx == idx ? jdx : idx].pitch) ? -12 : 12;
        int shifted = static_cast<int>(notes[fix_idx].pitch) + fix_shift;
        auto [flo, fhi] = getFugueVoiceRange(notes[fix_idx].voice, num_voices);
        if (shifted >= flo && shifted <= fhi) {
          notes[fix_idx].pitch = static_cast<uint8_t>(shifted);
        }
      }
    }

    // Final strict descending order enforcement.
    // Consonance adjustments above can break the ordering established by
    // the earlier bubble sort.  Re-sort and resolve equal pitches.
    {
      std::vector<size_t> fh;
      for (size_t idx = 0; idx < notes.size(); ++idx) {
        if (notes[idx].voice >= 1 && notes[idx].start_tick == start_tick &&
            notes[idx].duration == stage1_dur) {
          fh.push_back(idx);
        }
      }
      std::sort(fh.begin(), fh.end(),
                [&notes](size_t a, size_t b) {
                  return notes[a].voice < notes[b].voice;
                });
      // Descending sort by pitch.
      for (size_t idx = 0; idx + 1 < fh.size(); ++idx) {
        for (size_t jdx = idx + 1; jdx < fh.size(); ++jdx) {
          if (notes[fh[jdx]].pitch >= notes[fh[idx]].pitch) {
            std::swap(notes[fh[idx]].pitch, notes[fh[jdx]].pitch);
          }
        }
      }
      // Resolve equal pitches: shift each voice strictly below the previous.
      // Use smaller steps (semitones) first, then octave shift, to avoid
      // large voice-leading jumps.
      for (size_t idx = 1; idx < fh.size(); ++idx) {
        if (notes[fh[idx]].pitch >= notes[fh[idx - 1]].pitch) {
          auto [vlo, vhi] = getFugueVoiceRange(notes[fh[idx]].voice, num_voices);
          uint8_t target = notes[fh[idx - 1]].pitch;
          // Try nearest chord tone below target.
          int best = -1;
          for (int pc : chord_pcs) {
            for (int p = static_cast<int>(target) - 1; p >= vlo; p--) {
              if (p % 12 == pc) {
                if (best < 0 || p > best) best = p;
                break;
              }
            }
          }
          if (best >= vlo && best < static_cast<int>(target)) {
            notes[fh[idx]].pitch = static_cast<uint8_t>(best);
          }
        }
      }
    }

  }

  // =========================================================================
  // Stage 2 (bar 3): V7-I cadence -- 3-chord chain optimization.
  // Stage 3 (bar 4): Final sustained tonic chord.
  //
  // Strategy: "outer voices first, inner voices by search"
  //   1. Fix outer voices (soprano/bass) with cadential voice-leading constraints
  //   2. Search inner voice candidates to minimize total cost
  //   3. Reject any solution with voice crossing or parallel 5/8
  // =========================================================================
  if (duration > stage1_dur) {
    Tick stage2_start = start_tick + stage1_dur;
    Tick stage2_dur = std::min(bar_dur, duration - stage1_dur);
    Tick half_bar = stage2_dur / 2;

    // Collect Stage 1 end pitches for voice-leading reference.
    // Voice 0: last note of head motif; others: held chord tone.
    uint8_t stage1_end[5] = {0, 0, 0, 0, 0};
    for (const auto& nev : notes) {
      if (nev.voice < 5) {
        // For voice 0, we want the last note; for others, the held note.
        if (nev.voice == 0) {
          Tick note_end = nev.start_tick + nev.duration;
          if (note_end <= stage2_start + 1) {
            stage1_end[0] = nev.pitch;
          }
        } else {
          stage1_end[nev.voice] = nev.pitch;
        }
      }
    }

    // Dominant and tonic pitch classes for V7 and I chords.
    int dom_root = (tonic_pitch + 7) % 12;       // G
    int dom_third = (tonic_pitch + 11) % 12;      // B (leading tone)
    int dom_fifth = (tonic_pitch + 14) % 12;      // D
    int dom_seventh = (tonic_pitch + 17) % 12;    // F (7th)
    int tonic_root = tonic_pitch % 12;            // C
    int picardy_third = (tonic_pitch + 4) % 12;   // E (always major for Picardy)
    int res_third_pc = is_minor ? picardy_third : ((tonic_pitch + 4) % 12);
    int tonic_fifth = (tonic_pitch + 7) % 12;     // G

    int v7_pcs[] = {dom_root, dom_third, dom_fifth, dom_seventh};
    int i_pcs[] = {tonic_root, res_third_pc, tonic_fifth};

    uint8_t voice_count = std::min(num_voices, static_cast<uint8_t>(5));
    uint8_t sop = 0;
    uint8_t bass = voice_count - 1;

    // Helper: generate pitch candidates for a voice given target PCs.
    auto generateCandidates = [](uint8_t prev_pitch, const int* pcs, int num_pcs,
                                  uint8_t range_lo, uint8_t range_hi) {
      std::vector<uint8_t> cands;
      for (int pc_idx = 0; pc_idx < num_pcs; ++pc_idx) {
        int pc = pcs[pc_idx];
        // Search +-2 octaves from prev_pitch.
        for (int oct = -24; oct <= 24; oct += 12) {
          int base = (prev_pitch > 0) ? static_cast<int>(prev_pitch) + oct
                                      : static_cast<int>(range_lo) + oct;
          // Find nearest pitch with target PC.
          int target = base - (base % 12) + pc;
          if (target < base - 6) target += 12;
          if (target > base + 6) target -= 12;
          if (target >= static_cast<int>(range_lo) &&
              target <= static_cast<int>(range_hi)) {
            cands.push_back(static_cast<uint8_t>(target));
          }
        }
      }
      // Deduplicate.
      std::sort(cands.begin(), cands.end());
      cands.erase(std::unique(cands.begin(), cands.end()), cands.end());
      return cands;
    };

    // Limit candidates by distance from reference pitch, keeping top N.
    auto limitCandidates = [](std::vector<uint8_t>& cands, uint8_t ref, size_t max_n) {
      if (cands.size() <= max_n) return;
      std::sort(cands.begin(), cands.end(), [ref](uint8_t lhs, uint8_t rhs) {
        return std::abs(static_cast<int>(lhs) - static_cast<int>(ref)) <
               std::abs(static_cast<int>(rhs) - static_cast<int>(ref));
      });
      cands.resize(max_n);
    };

    // === Outer voices first ===
    auto [sop_lo, sop_hi] = getFugueVoiceRange(sop, num_voices);
    auto [bass_lo, bass_hi] = getFugueVoiceRange(bass, num_voices);

    // V7 candidates for soprano and bass (limited to top 3 by proximity).
    auto sop_v7_cands = generateCandidates(stage1_end[sop], v7_pcs, 4, sop_lo, sop_hi);
    limitCandidates(sop_v7_cands, stage1_end[sop], 3);
    auto bass_v7_cands = generateCandidates(stage1_end[bass], v7_pcs, 4, bass_lo, bass_hi);
    limitCandidates(bass_v7_cands, stage1_end[bass], 3);
    // Prefer bass on dominant root.
    {
      std::vector<uint8_t> root_first;
      std::vector<uint8_t> others;
      for (auto p : bass_v7_cands) {
        if (getPitchClass(p) == dom_root)
          root_first.push_back(p);
        else
          others.push_back(p);
      }
      root_first.insert(root_first.end(), others.begin(), others.end());
      bass_v7_cands = root_first;
    }

    // Score a 3-chord sequence for all voices.
    // v7_pitches[v], i_pitches[v], final_pitches[v] for each voice v.
    struct CodaSolution {
      uint8_t v7[5] = {};
      uint8_t i_chord[5] = {};
      uint8_t final_chord[5] = {};
      int cost = INT32_MAX;
    };

    auto scoreSolution = [&](const CodaSolution& sol) -> int {
      int cost = 0;

      for (uint8_t v = 0; v < voice_count; ++v) {
        // Voice-leading distance: stage1->V7, V7->I, I->final.
        int d1 = std::abs(static_cast<int>(sol.v7[v]) - static_cast<int>(stage1_end[v]));
        int d2 = std::abs(static_cast<int>(sol.i_chord[v]) - static_cast<int>(sol.v7[v]));
        int d3 = std::abs(static_cast<int>(sol.final_chord[v]) - static_cast<int>(sol.i_chord[v]));
        cost += 10 * (d1 + d2 + d3);

        // Excessive leap penalty (>12st).
        if (d1 > 12) cost += 200 * (d1 - 12);
        if (d2 > 12) cost += 200 * (d2 - 12);
        if (d3 > 12) cost += 200 * (d3 - 12);
      }

      // Leading-tone resolution: any voice with V7 leading tone should resolve up by semitone.
      for (uint8_t v = 0; v < voice_count; ++v) {
        if (getPitchClass(sol.v7[v]) == dom_third) {
          int resolution = static_cast<int>(sol.i_chord[v]) - static_cast<int>(sol.v7[v]);
          if (resolution != 1) cost += 50;  // Leading tone must resolve up by semitone.
        }
      }

      // Seventh resolution: any voice with V7 seventh should resolve down by step.
      for (uint8_t v = 0; v < voice_count; ++v) {
        if (getPitchClass(sol.v7[v]) == dom_seventh) {
          int resolution = static_cast<int>(sol.i_chord[v]) - static_cast<int>(sol.v7[v]);
          if (resolution != -1 && resolution != -2) cost += 50;
        }
      }

      // Contrary outer voice motion bonus (V7->I: soprano up, bass down or vice versa).
      {
        int sop_motion = static_cast<int>(sol.i_chord[sop]) - static_cast<int>(sol.v7[sop]);
        int bass_motion = static_cast<int>(sol.i_chord[bass]) - static_cast<int>(sol.v7[bass]);
        if ((sop_motion > 0 && bass_motion < 0) || (sop_motion < 0 && bass_motion > 0)) {
          cost -= 10;
        }
      }

      // Voice crossing/unison check -- instant rejection.
      for (int chord_idx = 0; chord_idx < 3; ++chord_idx) {
        const uint8_t* pitches = (chord_idx == 0) ? sol.v7
                               : (chord_idx == 1) ? sol.i_chord
                               : sol.final_chord;
        for (uint8_t v = 0; v + 1 < voice_count; ++v) {
          if (pitches[v] <= pitches[v + 1]) {
            return INT32_MAX;  // Voice crossing or unison: reject.
          }
        }
      }

      // Parallel perfect 5ths/octaves check between consecutive chords.
      auto checkParallels = [&](const uint8_t* chord_a, const uint8_t* chord_b) -> int {
        int pen = 0;
        for (uint8_t va = 0; va < voice_count; ++va) {
          for (uint8_t vb = va + 1; vb < voice_count; ++vb) {
            int interval_a = std::abs(static_cast<int>(chord_a[va]) -
                                       static_cast<int>(chord_a[vb]));
            int interval_b = std::abs(static_cast<int>(chord_b[va]) -
                                       static_cast<int>(chord_b[vb]));
            int simple_a = interval_util::compoundToSimple(interval_a);
            int simple_b = interval_util::compoundToSimple(interval_b);
            // Parallel perfect unisons, 5ths, or octaves.
            if ((simple_a == 0 || simple_a == 7) &&
                (simple_b == 0 || simple_b == 7) &&
                simple_a == simple_b) {
              // Check both voices move in same direction.
              int motion_a = static_cast<int>(chord_b[va]) - static_cast<int>(chord_a[va]);
              int motion_b = static_cast<int>(chord_b[vb]) - static_cast<int>(chord_a[vb]);
              if (motion_a != 0 && motion_b != 0 &&
                  ((motion_a > 0) == (motion_b > 0))) {
                return INT32_MAX;  // Parallel 5/8: reject.
              }
            }
          }
        }
        return pen;
      };

      int par1 = checkParallels(sol.v7, sol.i_chord);
      if (par1 == INT32_MAX) return INT32_MAX;
      cost += par1;
      int par2 = checkParallels(sol.i_chord, sol.final_chord);
      if (par2 == INT32_MAX) return INT32_MAX;
      cost += par2;

      // Hidden 5/8 on outer voices (penalty, not rejection).
      auto checkHidden58 = [&](const uint8_t* chord_a, const uint8_t* chord_b) -> int {
        int sop_m = static_cast<int>(chord_b[sop]) - static_cast<int>(chord_a[sop]);
        int bass_m = static_cast<int>(chord_b[bass]) - static_cast<int>(chord_a[bass]);
        if (sop_m != 0 && bass_m != 0 && ((sop_m > 0) == (bass_m > 0))) {
          int interval = std::abs(static_cast<int>(chord_b[sop]) -
                                   static_cast<int>(chord_b[bass]));
          int simple = interval_util::compoundToSimple(interval);
          if (simple == 0 || simple == 7) {
            return 100;  // Hidden 5/8 penalty.
          }
        }
        return 0;
      };

      cost += checkHidden58(sol.v7, sol.i_chord);
      cost += checkHidden58(sol.i_chord, sol.final_chord);

      // 4-3 suspension bonus: voice holds from V7 to I chord.
      for (uint8_t v = 0; v < voice_count; ++v) {
        if (sol.v7[v] == sol.i_chord[v]) {
          // Check if held note forms a 4th resolving to 3rd with any lower voice.
          for (uint8_t vb = v + 1; vb < voice_count; ++vb) {
            int interval_v7 = std::abs(static_cast<int>(sol.v7[v]) -
                                        static_cast<int>(sol.v7[vb]));
            int simple = interval_util::compoundToSimple(interval_v7);
            if (simple == 5) {  // Perfect 4th
              cost -= 20;  // 4-3 suspension bonus.
            }
          }
        }
      }

      return cost;
    };

    CodaSolution best;

    // I chord PCs for the final chord (Picardy third for minor).
    int final_pcs[] = {tonic_root, picardy_third, tonic_fifth};

    // Search: iterate outer voice candidates, then fill inner voices.
    for (auto sop_v7 : sop_v7_cands) {
      // I chord: soprano resolves leading tone -> tonic (if leading tone),
      // otherwise nearest I chord tone.
      std::vector<uint8_t> sop_i_cands;
      if (getPitchClass(sop_v7) == dom_third) {
        // Leading tone must resolve up by semitone.
        uint8_t resolved = sop_v7 + 1;
        if (resolved >= sop_lo && resolved <= sop_hi) {
          sop_i_cands.push_back(resolved);
        }
      }
      if (sop_i_cands.empty()) {
        sop_i_cands = generateCandidates(sop_v7, i_pcs, 3, sop_lo, sop_hi);
      }
      limitCandidates(sop_i_cands, sop_v7, 2);

      for (auto bass_v7 : bass_v7_cands) {
        // Bass: V7 root -> tonic root (standard bass resolution).
        std::vector<uint8_t> bass_i_cands;
        if (getPitchClass(bass_v7) == dom_root) {
          // Dominant root -> tonic root (down P5 or up P4).
          for (int d : {-7, 5, -19, 17}) {
            int cand = static_cast<int>(bass_v7) + d;
            if (cand >= bass_lo && cand <= bass_hi &&
                getPitchClass(static_cast<uint8_t>(cand)) == tonic_root) {
              bass_i_cands.push_back(static_cast<uint8_t>(cand));
            }
          }
        }
        if (bass_i_cands.empty()) {
          bass_i_cands = generateCandidates(bass_v7, i_pcs, 3, bass_lo, bass_hi);
        }
        limitCandidates(bass_i_cands, bass_v7, 2);

        for (auto sop_i : sop_i_cands) {
          for (auto bass_i : bass_i_cands) {
            // Quick crossing check on outer voices.
            if (sop_v7 < bass_v7 || sop_i < bass_i) continue;

            // Final chord candidates for outer voices (limited to top 2).
            auto sop_final_cands = generateCandidates(sop_i, final_pcs, 3, sop_lo, sop_hi);
            limitCandidates(sop_final_cands, sop_i, 2);
            auto bass_final_cands = generateCandidates(bass_i, final_pcs, 3, bass_lo, bass_hi);
            limitCandidates(bass_final_cands, bass_i, 2);

            for (auto sop_final : sop_final_cands) {
              for (auto bass_final : bass_final_cands) {
                if (sop_final < bass_final) continue;

                // === Inner voices: enumerate all combinations ===
                if (voice_count <= 2) {
                  CodaSolution sol;
                  sol.v7[sop] = sop_v7;
                  sol.v7[bass] = bass_v7;
                  sol.i_chord[sop] = sop_i;
                  sol.i_chord[bass] = bass_i;
                  sol.final_chord[sop] = sop_final;
                  sol.final_chord[bass] = bass_final;
                  sol.cost = scoreSolution(sol);
                  if (sol.cost < best.cost) best = sol;
                  continue;
                }

                // Generate inner voice candidates.
                struct InnerCands {
                  std::vector<uint8_t> v7;
                  std::vector<uint8_t> i_chord;
                  std::vector<uint8_t> final_chord;
                };
                std::vector<InnerCands> inner(voice_count - 2);

                for (uint8_t iv = 0; iv < voice_count - 2; ++iv) {
                  uint8_t voice_idx = 1 + iv;
                  auto [vlo, vhi] = getFugueVoiceRange(voice_idx, num_voices);
                  inner[iv].v7 = generateCandidates(stage1_end[voice_idx], v7_pcs, 4, vlo, vhi);
                  inner[iv].i_chord = generateCandidates(0, i_pcs, 3, vlo, vhi);
                  inner[iv].final_chord = generateCandidates(0, final_pcs, 3, vlo, vhi);

                  // Limit candidates to keep search space manageable.
                  // Sort by distance from previous pitch and keep top 5.
                  auto limitByDist = [](std::vector<uint8_t>& cands, uint8_t ref, int max_n) {
                    if (ref == 0 || static_cast<int>(cands.size()) <= max_n) return;
                    std::sort(cands.begin(), cands.end(), [ref](uint8_t lhs, uint8_t rhs) {
                      return std::abs(static_cast<int>(lhs) - static_cast<int>(ref)) <
                             std::abs(static_cast<int>(rhs) - static_cast<int>(ref));
                    });
                    cands.resize(static_cast<size_t>(max_n));
                  };
                  limitByDist(inner[iv].v7, stage1_end[voice_idx], 2);
                  uint8_t v7_ref = inner[iv].v7.empty() ? stage1_end[voice_idx]
                                                         : inner[iv].v7[0];
                  limitByDist(inner[iv].i_chord, v7_ref, 2);
                  uint8_t i_ref = inner[iv].i_chord.empty() ? v7_ref
                                                             : inner[iv].i_chord[0];
                  limitByDist(inner[iv].final_chord, i_ref, 2);
                }

                // Enumerate inner voice combinations.
                // For 3 voices: 1 inner voice; 4 voices: 2 inner; 5 voices: 3 inner.
                uint8_t n_inner = voice_count - 2;

                // Recursive lambda to enumerate inner voice assignments.
                std::function<void(uint8_t, CodaSolution&)> enumerate;
                enumerate = [&](uint8_t depth, CodaSolution& partial) {
                  if (depth == n_inner) {
                    partial.cost = scoreSolution(partial);
                    if (partial.cost < best.cost) best = partial;
                    return;
                  }
                  uint8_t voice_idx = 1 + depth;
                  for (auto pv7 : inner[depth].v7) {
                    // Early pruning: skip if voice-leading distance alone
                    // already exceeds best known cost.
                    int d1 = std::abs(static_cast<int>(pv7) -
                                       static_cast<int>(stage1_end[voice_idx]));
                    if (10 * d1 >= best.cost) continue;
                    for (auto pi : inner[depth].i_chord) {
                      for (auto pf : inner[depth].final_chord) {
                        partial.v7[voice_idx] = pv7;
                        partial.i_chord[voice_idx] = pi;
                        partial.final_chord[voice_idx] = pf;
                        enumerate(depth + 1, partial);
                      }
                    }
                  }
                };

                CodaSolution partial;
                partial.v7[sop] = sop_v7;
                partial.v7[bass] = bass_v7;
                partial.i_chord[sop] = sop_i;
                partial.i_chord[bass] = bass_i;
                partial.final_chord[sop] = sop_final;
                partial.final_chord[bass] = bass_final;
                enumerate(0, partial);
              }
            }
          }
        }
      }
    }

    // Emit V7 chord notes (first half of Stage 2).
    // Layer 2 safety net: enforce strict descending pitch order on the
    // winning solution (search or fallback).  This is normally a no-op when
    // scoreSolution already rejects crossings, but guards against edge cases
    // where candidate generation produces borderline solutions.
    auto projectStrictOrderU8 = [&](uint8_t* pitches) {
      int upper = INT_MAX;
      for (uint8_t v = 0; v < voice_count; ++v) {
        int p = static_cast<int>(pitches[v]);
        auto [vlo, vhi] = getFugueVoiceRange(v, num_voices);

        if (p < upper) {
          upper = p;
          continue;
        }

        // p >= upper: voice crossing/unison.
        int pc = getPitchClass(static_cast<uint8_t>(p));
        bool found = false;
        for (int cand = p - 12; cand >= static_cast<int>(vlo); cand -= 12) {
          if (cand < upper &&
              getPitchClass(static_cast<uint8_t>(cand)) == pc) {
            pitches[v] = static_cast<uint8_t>(cand);
            upper = cand;
            found = true;
            break;
          }
        }
        if (found) continue;

        int ceiling = std::min(upper - 1, static_cast<int>(vhi));
        if (ceiling >= static_cast<int>(vlo)) {
          pitches[v] = static_cast<uint8_t>(ceiling);
          upper = ceiling;
        } else {
          pitches[v] = vlo;
          upper = static_cast<int>(vlo) - 1;
        }
      }
    };

    if (best.cost < INT32_MAX) {
      projectStrictOrderU8(best.v7);
      projectStrictOrderU8(best.i_chord);
      projectStrictOrderU8(best.final_chord);

      for (uint8_t v = 0; v < voice_count; ++v) {
        NoteEvent note;
        note.start_tick = stage2_start;
        note.duration = half_bar;
        note.pitch = best.v7[v];
        note.velocity = kOrganVelocity;
        note.voice = v;
        note.source = BachNoteSource::Coda;
        notes.push_back(note);
      }

      // Emit I chord notes (second half of Stage 2).
      for (uint8_t v = 0; v < voice_count; ++v) {
        NoteEvent note;
        note.start_tick = stage2_start + half_bar;
        note.duration = stage2_dur - half_bar;
        note.pitch = best.i_chord[v];
        note.velocity = kOrganVelocity;
        note.voice = v;
        note.source = BachNoteSource::Coda;
        notes.push_back(note);
      }

      // Emit Stage 3 final tonic chord.
      Tick stage2_actual = stage2_dur;
      Tick stage3_start = start_tick + stage1_dur + stage2_actual;
      if (stage3_start < start_tick + duration) {
        Tick stage3_dur = start_tick + duration - stage3_start;
        for (uint8_t v = 0; v < voice_count; ++v) {
          NoteEvent note;
          note.start_tick = stage3_start;
          note.duration = stage3_dur;
          note.pitch = best.final_chord[v];
          note.velocity = kOrganVelocity;
          note.voice = v;
          note.source = BachNoteSource::Coda;
          notes.push_back(note);
        }
      }
    } else {
      // Fallback: emit simple V7-I if optimization found no valid solution.
      int dom_pitch = tonic_pitch + 7;
      int res_third = is_minor ? 3 : 4;

      // Layer 1: Voice-count-specific offsets.
      // 3-voice (and 2-voice): LT/7th/root for V7 -- omit 5th to avoid crossing.
      int dom7_offsets[5];
      int tonic_offsets_fb[5];
      int final_offsets_fb[5];
      uint8_t count = std::min(num_voices, static_cast<uint8_t>(5));

      if (num_voices <= 3) {
        // V7: LT(soprano), 7th(inner), root(bass) -- 5th omitted.
        dom7_offsets[0] = 4;    // B4 (leading tone)
        dom7_offsets[1] = -2;   // F4 (7th)
        dom7_offsets[2] = -12;  // G3 (root)
        // I: tonic(sop), 3rd(inner), root(bass).
        tonic_offsets_fb[0] = 12;         // C5
        tonic_offsets_fb[1] = res_third;  // E4/Eb4
        tonic_offsets_fb[2] = 0;          // C4
        // Final: tonic(sop), 3rd(inner, Picardy), root(bass).
        final_offsets_fb[0] = 12;  // C5
        final_offsets_fb[1] = 4;   // E4 (always major for Picardy)
        final_offsets_fb[2] = 0;   // C4
      } else {
        // 4-5 voice: original offsets.
        int dom7_src[] = {4, 7, -2, -12, -17};
        int tonic_src[] = {12, 7, res_third, 0, -12};
        int final_src[] = {12, 7, 4, 0, -12};
        for (uint8_t v = 0; v < count; ++v) {
          dom7_offsets[v] = dom7_src[v];
          tonic_offsets_fb[v] = tonic_src[v];
          final_offsets_fb[v] = final_src[v];
        }
      }

      // Compute fallback pitches into arrays.
      int v7_pitches[5] = {};
      int i_pitches[5] = {};
      int final_pitches[5] = {};
      for (uint8_t v = 0; v < count; ++v) {
        auto [vlo, vhi] = getFugueVoiceRange(v, num_voices);
        v7_pitches[v] = static_cast<int>(clampPitch(dom_pitch + dom7_offsets[v], vlo, vhi));
        i_pitches[v] = static_cast<int>(clampPitch(tonic_pitch + tonic_offsets_fb[v], vlo, vhi));
        final_pitches[v] = static_cast<int>(
            clampPitch(tonic_pitch + final_offsets_fb[v], vlo, vhi));
      }

      // Harsh dissonance guard: check each V7 pitch against the tonic bass
      // (pedal from Stage 1). If m2(1)/TT(6)/M7(11) is formed, try octave
      // shifts to find a safe register that avoids the clash.
      int tonic_bass = static_cast<int>(
          clampPitch(tonic_pitch, bass_lo, bass_hi));
      for (uint8_t v = 0; v < count; ++v) {
        int simple = interval_util::compoundToSimple(
            absoluteInterval(static_cast<uint8_t>(v7_pitches[v]),
                             static_cast<uint8_t>(tonic_bass)));
        if (simple == 1 || simple == 6 || simple == 11) {
          auto [range_lo, range_hi] = getFugueVoiceRange(v, num_voices);
          for (int shift : {12, -12, 24, -24}) {
            int alt = v7_pitches[v] + shift;
            if (alt < range_lo || alt > range_hi) continue;
            int alt_simple = interval_util::compoundToSimple(
                absoluteInterval(static_cast<uint8_t>(alt),
                                 static_cast<uint8_t>(tonic_bass)));
            if (alt_simple != 1 && alt_simple != 6 && alt_simple != 11) {
              v7_pitches[v] = alt;
              break;
            }
          }
        }
      }

      // Layer 2: Top-down greedy projection -- ensure strict descending order.
      // Priority: (1) strict v0 > v1 > v2 > ..., (2) preserve pitch class.
      auto projectStrictOrder = [&](int* pitches) {
        int upper = INT_MAX;
        for (uint8_t v = 0; v < count; ++v) {
          int p = pitches[v];
          auto [vlo, vhi] = getFugueVoiceRange(v, num_voices);

          if (p < upper) {
            upper = p;
            continue;
          }

          // p >= upper: voice crossing. Rescue search.
          // (A) Same PC, octave below (within range, below upper).
          int pc = getPitchClass(static_cast<uint8_t>(p));
          bool found = false;
          for (int cand = p - 12; cand >= static_cast<int>(vlo); cand -= 12) {
            if (cand < upper &&
                getPitchClass(static_cast<uint8_t>(cand)) == pc) {
              pitches[v] = cand;
              upper = cand;
              found = true;
              break;
            }
          }
          if (found) continue;

          // (B) clampPitch: PC may change, but strict order is priority.
          int ceiling = std::min(upper - 1, static_cast<int>(vhi));
          if (ceiling >= static_cast<int>(vlo)) {
            pitches[v] = ceiling;
            upper = ceiling;
          } else {
            // (C) Extreme fallback: set to voice range low.
            pitches[v] = static_cast<int>(vlo);
            upper = static_cast<int>(vlo) - 1;
          }
        }
      };

      projectStrictOrder(v7_pitches);
      projectStrictOrder(i_pitches);
      projectStrictOrder(final_pitches);

      // Emit V7 and I chord notes.
      for (uint8_t v = 0; v < count; ++v) {
        NoteEvent v7_note;
        v7_note.start_tick = stage2_start;
        v7_note.duration = half_bar;
        v7_note.pitch = static_cast<uint8_t>(v7_pitches[v]);
        v7_note.velocity = kOrganVelocity;
        v7_note.voice = v;
        v7_note.source = BachNoteSource::Coda;
        notes.push_back(v7_note);

        NoteEvent i_note;
        i_note.start_tick = stage2_start + half_bar;
        i_note.duration = stage2_dur - half_bar;
        i_note.pitch = static_cast<uint8_t>(i_pitches[v]);
        i_note.velocity = kOrganVelocity;
        i_note.voice = v;
        i_note.source = BachNoteSource::Coda;
        notes.push_back(i_note);
      }

      // Stage 3 fallback.
      Tick stage3_start = start_tick + stage1_dur + stage2_dur;
      if (stage3_start < start_tick + duration) {
        Tick stage3_dur = start_tick + duration - stage3_start;
        for (uint8_t v = 0; v < count; ++v) {
          NoteEvent note;
          note.start_tick = stage3_start;
          note.duration = stage3_dur;
          note.pitch = static_cast<uint8_t>(final_pitches[v]);
          note.velocity = kOrganVelocity;
          note.voice = v;
          note.source = BachNoteSource::Coda;
          notes.push_back(note);
        }
      }
    }
  }

  // Verification pass: log any remaining violations (diagnostic only).
  {
    // Check voice crossing across all Stage 2/3 chords.
    auto checkCrossing = [&](const char* label, Tick tick) {
      uint8_t pitches_at_tick[5] = {0, 0, 0, 0, 0};
      for (const auto& nev : notes) {
        if (nev.start_tick <= tick && tick < nev.start_tick + nev.duration && nev.voice < 5) {
          pitches_at_tick[nev.voice] = nev.pitch;
        }
      }
      uint8_t count = std::min(num_voices, static_cast<uint8_t>(5));
      for (uint8_t v = 0; v + 1 < count; ++v) {
        if (pitches_at_tick[v] > 0 && pitches_at_tick[v + 1] > 0 &&
            pitches_at_tick[v] <= pitches_at_tick[v + 1]) {
          std::fprintf(stderr, "[createCodaNotes] WARNING: %s voice crossing v%u(%u) < v%u(%u)\n",
                       label, v, pitches_at_tick[v], v + 1, pitches_at_tick[v + 1]);
        }
      }
    };

    Tick stage2_start = start_tick + stage1_dur;
    Tick stage2_dur = std::min(bar_dur, duration - stage1_dur);
    Tick half_bar = stage2_dur / 2;

    if (duration > stage1_dur) {
      checkCrossing("V7", stage2_start);
      checkCrossing("I", stage2_start + half_bar);
      Tick stage3_start = start_tick + stage1_dur + stage2_dur;
      if (stage3_start < start_tick + duration) {
        checkCrossing("final", stage3_start);
      }
    }
  }

  return notes;
}

/// @brief Select how many voices should be active for an episode.
///
/// Based on BWV578 analysis: 3-voice texture dominates. Episodes in the
/// develop phase should typically use num_voices-1 active voices.
///
/// @param num_voices Total voice count.
/// @param phase_pos Current fugue phase position (0.0-1.0).
/// @param density_target Texture density target.
/// @param rng Random number generator.
/// @return Number of active voices for this episode.
uint8_t selectEpisodeVoiceCount(uint8_t num_voices, float phase_pos,
                                const TextureDensityTarget& density_target,
                                std::mt19937& rng) {
  if (num_voices <= 2) return num_voices;  // Can't reduce below 2.

  // In develop phase (0.25-0.70): mostly N-1 voices.
  float target = density_target.develop_density;
  if (phase_pos >= 0.70f) {
    target = density_target.stretto_density;
  }

  uint8_t target_voices = static_cast<uint8_t>(
      std::round(static_cast<float>(num_voices) * target));
  if (target_voices < 2) target_voices = 2;
  if (target_voices > num_voices) target_voices = num_voices;

  // Probabilistic: 85% use target, 15% use full (for variety).
  std::uniform_real_distribution<float> dist(0.0f, 1.0f);
  if (dist(rng) < 0.15f) {
    return num_voices;
  }
  return target_voices;
}

/// @brief Select which voice to rest during an episode.
///
/// Rotates resting voice to ensure all voices get periodic rest.
/// Avoids resting the entry voice (if in a middle entry section).
///
/// @param num_voices Total voice count.
/// @param episode_idx Episode index (for rotation).
/// @param entry_voice Voice with the current subject entry (-1 if none).
/// @return Voice ID to rest, or num_voices if no rest.
uint8_t selectRestingVoice(uint8_t num_voices, int episode_idx,
                           int entry_voice = -1) {
  if (num_voices <= 2) return num_voices;  // No rest possible.

  // Rotate through voices, skipping the entry voice.
  uint8_t candidate = static_cast<uint8_t>(episode_idx % num_voices);
  if (static_cast<int>(candidate) == entry_voice) {
    candidate = (candidate + 1) % num_voices;
  }
  return candidate;
}

// ===========================================================================
// Pipeline step 1: Build material
// ===========================================================================

/// @brief Generate and validate subject, answer, countersubject,
///        constraint profile, and motif pool.
/// @param config Fugue configuration.
/// @return All pre-computed material for the fugue.
FugueMaterial buildMaterial(const FugueConfig& config) {
  FugueMaterial mat;

  uint8_t num_voices = clampVoiceCount(config.num_voices);

  // Generate subject with retry logic.
  mat.subject = generateValidSubject(config, mat.subject_attempts);
  if (mat.subject.notes.empty()) {
    return mat;
  }

  // Analyze subject obligations for constraint-driven generation.
  mat.constraint_profile = analyzeObligations(
      mat.subject.notes, config.key, config.is_minor);

  // Generate answer (Real/Tonal auto-detection).
  const ArchetypePolicy& policy = getArchetypePolicy(config.archetype);
  mat.answer = generateAnswer(mat.subject, config.answer_type, policy.preferred_answer);

  // Generate countersubject(s).
  mat.countersubject = generateCountersubject(
      mat.subject, config.seed + 1000, 5, config.archetype);

  if (num_voices >= 4) {
    mat.countersubject_2 = generateSecondCountersubject(
        mat.subject, mat.countersubject, config.seed + 5000, 5, config.archetype);
  }

  // Build motif pool for Fortspinnung-based episodes.
  mat.motif_pool.build(mat.subject.notes, mat.countersubject.notes,
                       mat.subject.character);

  return mat;
}

// ===========================================================================
// Pipeline step 2: Plan structure
// ===========================================================================

/// @brief Create tonal plan, modulation plan, section layout, and energy curve.
/// @param config Fugue configuration.
/// @param material Pre-computed material from buildMaterial().
/// @return Complete structural plan for the fugue.
FuguePlan planStructure(const FugueConfig& config, const FugueMaterial& material) {
  FuguePlan plan;

  uint8_t num_voices = clampVoiceCount(config.num_voices);

  // Estimate total duration using structural formula.
  Tick expo_ticks = static_cast<Tick>(num_voices) * material.subject.length_ticks;
  Tick episode_bars_tick = kTicksPerBar * static_cast<Tick>(config.episode_bars);
  Tick develop_ticks = (episode_bars_tick * static_cast<Tick>(config.develop_pairs) * 2) +
                       material.subject.length_ticks * static_cast<Tick>(config.develop_pairs);
  Tick return_ep_ticks = kTicksPerBar * static_cast<Tick>(config.episode_bars);
  Tick pedal_ticks = kTicksPerBar * kDominantPedalBars;
  Tick stretto_ticks = material.subject.length_ticks * 2;
  Tick coda_ticks = kTicksPerBar * kCodaBars;
  plan.estimated_duration = expo_ticks + develop_ticks + return_ep_ticks + pedal_ticks +
                            stretto_ticks + coda_ticks;
  Tick min_duration = kTicksPerBar * kMinFugueBars;
  if (plan.estimated_duration < min_duration) {
    plan.estimated_duration = min_duration;
  }

  // Create modulation plan.
  if (config.has_modulation_plan) {
    plan.modulation_plan = config.modulation_plan;
  } else {
    plan.modulation_plan = config.is_minor
        ? ModulationPlan::createForMinor(config.key)
        : ModulationPlan::createForMajor(config.key);
  }

  // Generate structure-aligned tonal plan.
  plan.tonal_plan = generateStructureAlignedTonalPlan(
      config, plan.modulation_plan, material.subject.length_ticks,
      plan.estimated_duration);

  // Create beat-resolution harmonic timeline.
  plan.detailed_timeline = plan.tonal_plan.toDetailedTimeline(plan.estimated_duration);

  // Plan section layout (exposition + develop pairs + return + pedal + stretto + coda).
  Tick current_tick = 0;

  // Section: Exposition (Establish).
  {
    PlannedSection expo_section;
    expo_section.type = SectionType::Exposition;
    expo_section.phase = FuguePhase::Establish;
    expo_section.key = config.key;
    expo_section.start_tick = current_tick;
    expo_section.end_tick = current_tick + expo_ticks;
    expo_section.energy_level = FugueEnergyCurve::getLevel(current_tick, plan.estimated_duration);
    plan.sections.push_back(expo_section);
    current_tick = expo_section.end_tick;
  }

  // Sections: Develop pairs (Episode + MiddleEntry).
  Key prev_key = config.key;
  for (int pair_idx = 0; pair_idx < config.develop_pairs; ++pair_idx) {
    int ep_bars = config.episode_bars;
    if (pair_idx % 2 != 0) {
      ep_bars += 1;
    }
    Tick episode_duration = kTicksPerBar * static_cast<Tick>(ep_bars);
    Key target_key = plan.modulation_plan.getTargetKey(pair_idx, config.key);

    // Episode section.
    PlannedSection ep_section;
    ep_section.type = SectionType::Episode;
    ep_section.phase = FuguePhase::Develop;
    ep_section.key = target_key;
    ep_section.prev_key = prev_key;
    ep_section.start_tick = current_tick;
    ep_section.end_tick = current_tick + episode_duration;
    ep_section.episode_index = pair_idx;
    ep_section.energy_level = FugueEnergyCurve::getLevel(current_tick, plan.estimated_duration);
    plan.sections.push_back(ep_section);
    current_tick += episode_duration;

    // MiddleEntry section.
    PlannedSection me_section;
    me_section.type = SectionType::MiddleEntry;
    me_section.phase = FuguePhase::Develop;
    me_section.key = target_key;
    me_section.prev_key = prev_key;
    me_section.start_tick = current_tick;
    me_section.end_tick = current_tick + material.subject.length_ticks;
    me_section.entry_voice = static_cast<VoiceId>(pair_idx % num_voices);
    me_section.companion_needed = true;
    me_section.energy_level = FugueEnergyCurve::getLevel(current_tick, plan.estimated_duration);
    plan.sections.push_back(me_section);
    current_tick += material.subject.length_ticks;

    prev_key = target_key;
  }

  // Section: Return Episode (transition back to home key).
  {
    Tick return_ep_duration = kTicksPerBar * static_cast<Tick>(config.episode_bars);
    PlannedSection return_section;
    return_section.type = SectionType::Episode;
    return_section.phase = FuguePhase::Develop;
    return_section.key = config.key;
    return_section.prev_key = prev_key;
    return_section.start_tick = current_tick;
    return_section.end_tick = current_tick + return_ep_duration;
    return_section.episode_index = config.develop_pairs;  // Next index after develop pairs
    return_section.energy_level = FugueEnergyCurve::getLevel(current_tick, plan.estimated_duration);
    plan.sections.push_back(return_section);
    current_tick += return_ep_duration;
    prev_key = config.key;
  }

  // Section: Stretto (Resolve).
  {
    PlannedSection stretto_section;
    stretto_section.type = SectionType::Stretto;
    stretto_section.phase = FuguePhase::Resolve;
    stretto_section.key = config.key;
    stretto_section.start_tick = current_tick;
    stretto_section.end_tick = current_tick + stretto_ticks;
    stretto_section.energy_level = FugueEnergyCurve::getLevel(
        current_tick, plan.estimated_duration);
    plan.sections.push_back(stretto_section);
    current_tick += stretto_ticks;
  }

  // Section: Coda (Resolve).
  {
    PlannedSection coda_section;
    coda_section.type = SectionType::Coda;
    coda_section.phase = FuguePhase::Resolve;
    coda_section.key = config.key;
    coda_section.start_tick = current_tick;
    coda_section.end_tick = current_tick + coda_ticks;
    coda_section.energy_level = 1.0f;
    plan.sections.push_back(coda_section);
  }

  return plan;
}

// ===========================================================================
// Pipeline step 3: Generate sections
// ===========================================================================

/// @brief Iterate planned sections and call existing sub-generators.
/// @param config Fugue configuration.
/// @param material Pre-computed material.
/// @param plan Structural plan.
/// @param structure Output FugueStructure for section tracking.
/// @return All generated notes across all sections.
std::vector<NoteEvent> generateSections(
    const FugueConfig& config, const FugueMaterial& material,
    const FuguePlan& plan, FugueStructure& structure) {
  uint8_t num_voices = clampVoiceCount(config.num_voices);

  // 1. Initialize counterpoint infrastructure.
  CounterpointState cp_state;
  BachRuleEvaluator cp_rules(num_voices);
  cp_rules.setFreeCounterpoint(true);
  CollisionResolver cp_resolver;
  cp_resolver.setHarmonicTimeline(&plan.detailed_timeline);

  // Pipeline-level constraint state for cross-section distribution tracking.
  ConstraintState pipeline_cs;
  pipeline_cs.gravity.melodic_model = &kFugueUpperMarkov;
  pipeline_cs.gravity.vertical_table = &kFugueVerticalTable;
  pipeline_cs.total_duration = plan.estimated_duration;
  // Collect cadence positions from the plan.
  for (const auto& sec : plan.sections) {
    if (sec.type == SectionType::Coda || sec.type == SectionType::Stretto) {
      pipeline_cs.cadence_ticks.push_back(sec.start_tick);
    }
  }

  // Helper: record section notes in pipeline accumulator.
  auto record_notes = [&pipeline_cs](const std::vector<NoteEvent>& notes, Key key) {
    for (const auto& nev : notes) {
      int degree = 0;
      scale_util::pitchToScaleDegree(nev.pitch, key, ScaleType::Major, degree);
      pipeline_cs.accumulator.recordNote(nev.duration, degree);
    }
  };

  for (uint8_t vid = 0; vid < num_voices; ++vid) {
    auto [lo, hi] = getFugueVoiceRange(vid, num_voices);
    cp_state.registerVoice(vid, lo, hi);
  }
  cp_state.setKey(config.key);

  // 2. Tracking state.
  std::vector<NoteEvent> all_notes;
  Tick current_tick = 0;
  uint32_t entries_seen_mask = 0;

  // RNGs for false entry and density decisions.
  std::mt19937 false_entry_rng(config.seed + 9999u);
  std::mt19937 density_rng(config.seed + 8888u);

  // Character-based false entry probability.
  float false_entry_prob = 0.0f;
  switch (material.subject.character) {
    case SubjectCharacter::Restless: false_entry_prob = 0.40f; break;
    case SubjectCharacter::Playful:  false_entry_prob = 0.30f; break;
    case SubjectCharacter::Noble:    false_entry_prob = 0.15f; break;
    case SubjectCharacter::Severe:   false_entry_prob = 0.10f; break;
  }
  false_entry_prob = std::clamp(
      false_entry_prob + rng::rollFloat(false_entry_rng, -0.05f, 0.05f),
      0.0f, 1.0f);

  VoiceId lowest_voice = num_voices - 1;

  // 3. Section-by-section generation.
  for (size_t sec_idx = 0; sec_idx < plan.sections.size(); ++sec_idx) {
    const auto& section = plan.sections[sec_idx];

    switch (section.type) {
      case SectionType::Exposition: {
        Exposition expo = buildExposition(
            material.subject, material.answer, material.countersubject,
            config, config.seed,
            cp_state, cp_rules, cp_resolver,
            plan.detailed_timeline, plan.estimated_duration);
        structure.addSection(SectionType::Exposition, FuguePhase::Establish,
                             0, expo.total_ticks, config.key);
        auto expo_notes = expo.allNotes();
        all_notes.insert(all_notes.end(), expo_notes.begin(), expo_notes.end());
        record_notes(expo_notes, config.key);
        current_tick = expo.total_ticks;
        break;
      }

      case SectionType::Episode: {
        Tick episode_duration = section.end_tick - section.start_tick;
        uint32_t pair_seed_base = config.seed +
            static_cast<uint32_t>(section.episode_index) * 2000u + 2000u;

        pipeline_cs.gravity.phase = FuguePhase::Develop;
        pipeline_cs.gravity.energy = section.energy_level;
        Episode episode = generateFortspinnungEpisode(
            material.subject, material.motif_pool,
            current_tick, episode_duration,
            section.prev_key, section.key, num_voices, pair_seed_base,
            section.episode_index, section.energy_level,
            cp_state, cp_rules, cp_resolver, plan.detailed_timeline,
            0, &pipeline_cs.accumulator);

        // Texture density management.
        float ep_phase_pos = static_cast<float>(current_tick) /
            static_cast<float>(plan.estimated_duration);
        uint8_t ep_active = selectEpisodeVoiceCount(
            num_voices, ep_phase_pos, config.density_target, density_rng);
        if (ep_active < num_voices) {
          uint8_t rest_voice = selectRestingVoice(
              num_voices, section.episode_index, -1);
          episode.notes.erase(
              std::remove_if(episode.notes.begin(), episode.notes.end(),
                             [rest_voice](const NoteEvent& evt) {
                               return evt.voice == rest_voice;
                             }),
              episode.notes.end());
        }

        structure.addSection(SectionType::Episode, FuguePhase::Develop,
                             current_tick, current_tick + episode_duration,
                             section.key);
        all_notes.insert(all_notes.end(),
                         episode.notes.begin(), episode.notes.end());
        record_notes(episode.notes, section.key);
        current_tick += episode_duration;
        break;
      }

      case SectionType::MiddleEntry: {
        Key target_key = section.key;

        // Count how many MiddleEntry sections precede this one for pair index.
        int me_count = 0;
        for (size_t prev_sec = 0; prev_sec < sec_idx; ++prev_sec) {
          if (plan.sections[prev_sec].type == SectionType::MiddleEntry) ++me_count;
        }
        int pair_idx = me_count;

        // Voice selection: rotation + unseen priority + bass forcing.
        uint8_t entry_voice;
        {
          uint8_t bass_voice = num_voices - 1;
          bool bass_has_entry = (entries_seen_mask & (1u << bass_voice)) != 0;
          int threshold = std::max(1, config.develop_pairs * 2 / 3);
          if (!bass_has_entry && pair_idx >= threshold && num_voices >= 4) {
            entry_voice = bass_voice;
          } else {
            uint8_t candidate = static_cast<uint8_t>(pair_idx % num_voices);
            for (uint8_t vid = 0; vid < num_voices; ++vid) {
              uint8_t check = (candidate + vid) % num_voices;
              if ((entries_seen_mask & (1u << check)) == 0) {
                candidate = check;
                break;
              }
            }
            entry_voice = candidate;
          }
          entries_seen_mask |= (1u << entry_voice);
        }

        // False entry decision.
        std::uniform_real_distribution<float> false_dist(0.0f, 1.0f);
        bool use_false_entry = (pair_idx > 0) &&
            (false_dist(false_entry_rng) < false_entry_prob);

        uint8_t entry_last = extractVoiceLastPitch(
            all_notes, current_tick, entry_voice);
        float me_phase_pos = static_cast<float>(current_tick) /
            static_cast<float>(plan.estimated_duration);

        MiddleEntry middle_entry = use_false_entry
            ? generateFalseEntry(material.subject, target_key, current_tick,
                                 entry_voice, num_voices, 3, me_phase_pos)
            : generateMiddleEntry(material.subject, target_key,
                                  current_tick, entry_voice, num_voices,
                                  cp_state, cp_rules, cp_resolver,
                                  plan.detailed_timeline, entry_last,
                                  me_phase_pos);

        Tick middle_end = middle_entry.end_tick;
        if (middle_end <= current_tick) {
          middle_end = current_tick + material.subject.length_ticks;
          if (middle_end <= current_tick) {
            middle_end = current_tick + kTicksPerBar * 2;
          }
        }

        structure.addSection(SectionType::MiddleEntry, FuguePhase::Develop,
                             current_tick, middle_end, target_key);
        all_notes.insert(all_notes.end(),
                         middle_entry.notes.begin(), middle_entry.notes.end());
        record_notes(middle_entry.notes, target_key);

        // Companion counterpoint for non-entry voices.
        if (section.companion_needed) {
          Tick me_duration = middle_end - current_tick;
          if (me_duration > 0 && num_voices >= 2) {
            uint32_t companion_seed = config.seed +
                static_cast<uint32_t>(pair_idx) * 2000u + 2500u;
            float me_energy = FugueEnergyCurve::getLevel(
                current_tick, plan.estimated_duration);
            pipeline_cs.gravity.phase = FuguePhase::Develop;
            pipeline_cs.gravity.energy = me_energy;
            Episode companion = generateFortspinnungEpisode(
                material.subject, material.motif_pool,
                current_tick, me_duration,
                target_key, target_key, num_voices,
                companion_seed, pair_idx, me_energy,
                cp_state, cp_rules, cp_resolver, plan.detailed_timeline,
                0, &pipeline_cs.accumulator);
            // Remove entry voice notes from companion.
            companion.notes.erase(
                std::remove_if(companion.notes.begin(), companion.notes.end(),
                               [entry_voice](const NoteEvent& evt) {
                                 return evt.voice == entry_voice;
                               }),
                companion.notes.end());
            // Additional voice rest for texture variety (4+ voices).
            float ep_phase_pos2 = static_cast<float>(current_tick) /
                static_cast<float>(plan.estimated_duration);
            uint8_t ep_active2 = selectEpisodeVoiceCount(
                num_voices, ep_phase_pos2, config.density_target, density_rng);
            if (ep_active2 < num_voices && num_voices >= 4) {
              uint8_t companion_rest = selectRestingVoice(
                  num_voices, pair_idx + 100, entry_voice);
              if (companion_rest != entry_voice) {
                companion.notes.erase(
                    std::remove_if(companion.notes.begin(),
                                   companion.notes.end(),
                                   [companion_rest](const NoteEvent& evt) {
                                     return evt.voice == companion_rest;
                                   }),
                    companion.notes.end());
              }
            }
            all_notes.insert(all_notes.end(),
                             companion.notes.begin(), companion.notes.end());
            record_notes(companion.notes, target_key);
          }
        }

        current_tick = middle_end;
        break;
      }

      case SectionType::Stretto: {
        // Dominant pedal (4 bars before stretto).
        Tick pedal_duration = kTicksPerBar * kDominantPedalBars;
        {
          auto [ped_lo, ped_hi] = getFugueVoiceRange(lowest_voice, num_voices);
          uint8_t tonic_for_pedal = tonicBassPitchForVoices(config.key,
                                                             num_voices);
          uint8_t dominant_pitch = clampPitch(
              static_cast<int>(tonic_for_pedal) + interval::kPerfect5th,
              ped_lo, ped_hi);

          removeLowestVoiceNotes(all_notes, lowest_voice,
                                 current_tick, current_tick + pedal_duration);
          auto dominant_pedal = generatePedalPoint(
              dominant_pitch, current_tick, pedal_duration, lowest_voice);
          all_notes.insert(all_notes.end(),
                           dominant_pedal.begin(), dominant_pedal.end());
          record_notes(dominant_pedal, config.key);

          // Register pedal notes in counterpoint state.
          for (const auto& note : dominant_pedal) {
            cp_state.addNote(note.voice, note);
          }

          // Upper voice episode over pedal.
          uint8_t upper_voices = num_voices > 1 ? num_voices - 1 : 1;
          float pedal_energy = FugueEnergyCurve::getLevel(
              current_tick, plan.estimated_duration);
          pipeline_cs.gravity.phase = FuguePhase::Resolve;
          pipeline_cs.gravity.energy = pedal_energy;
          Episode pedal_episode = generateFortspinnungEpisode(
              material.subject, material.motif_pool,
              current_tick, pedal_duration,
              config.key, config.key, upper_voices,
              config.seed + static_cast<uint32_t>(
                  config.develop_pairs + 1) * 2000u + 7000u,
              config.develop_pairs + 1, pedal_energy,
              cp_state, cp_rules, cp_resolver, plan.detailed_timeline,
              dominant_pitch, &pipeline_cs.accumulator);

          // Texture reduction: rest one upper voice during pedal episode
          // to prevent tutti saturation (4+ voices).
          if (upper_voices >= 3) {
            uint8_t pedal_rest = static_cast<uint8_t>(
                (config.develop_pairs + 2) % upper_voices);
            pedal_episode.notes.erase(
                std::remove_if(pedal_episode.notes.begin(),
                               pedal_episode.notes.end(),
                               [pedal_rest](const NoteEvent& evt) {
                                 return evt.voice == pedal_rest;
                               }),
                pedal_episode.notes.end());
          }

          all_notes.insert(all_notes.end(),
                           pedal_episode.notes.begin(),
                           pedal_episode.notes.end());
          record_notes(pedal_episode.notes, config.key);
          current_tick += pedal_duration;
        }

        // Stretto body.
        uint8_t stretto_last[5] = {0, 0, 0, 0, 0};
        for (uint8_t vid = 0; vid < num_voices && vid < 5; ++vid) {
          stretto_last[vid] = extractVoiceLastPitch(all_notes, current_tick, vid);
        }
        Stretto stretto = generateStretto(
            material.subject, config.key, current_tick,
            num_voices, config.seed + 4000,
            material.subject.character,
            cp_state, cp_rules, cp_resolver,
            plan.detailed_timeline, stretto_last,
            plan.estimated_duration);
        Tick stretto_end = stretto.end_tick;
        if (stretto_end <= current_tick) {
          stretto_end = current_tick + kTicksPerBar * 2;
        }
        structure.addSection(SectionType::Stretto, FuguePhase::Resolve,
                             current_tick, stretto_end, config.key);
        auto stretto_notes = stretto.allNotes();
        all_notes.insert(all_notes.end(),
                         stretto_notes.begin(), stretto_notes.end());
        record_notes(stretto_notes, config.key);
        current_tick = stretto_end;
        break;
      }

      case SectionType::Coda: {
        Tick coda_duration = kTicksPerBar * kCodaBars;
        structure.addSection(SectionType::Coda, FuguePhase::Resolve,
                             current_tick, current_tick + coda_duration,
                             config.key);
        // Voice-leading: extract last pitches for smooth coda transitions.
        uint8_t coda_last_pitches[5] = {0, 0, 0, 0, 0};
        for (uint8_t vid = 0; vid < num_voices && vid < 5; ++vid) {
          coda_last_pitches[vid] = extractVoiceLastPitch(
              all_notes, current_tick, vid);
        }
        auto coda_notes = createCodaNotes(
            current_tick, coda_duration, config.key, num_voices,
            config.is_minor, coda_last_pitches);
        all_notes.insert(all_notes.end(),
                         coda_notes.begin(), coda_notes.end());
        record_notes(coda_notes, config.key);

        // Tonic pedal in coda Stage 3 (last bar).
        {
          uint8_t tonic_pitch = tonicBassPitchForVoices(
              config.key, num_voices);
          Tick pedal_start = current_tick + kTicksPerBar * 3;
          Tick pedal_dur = current_tick + coda_duration - pedal_start;
          removeLowestVoiceNotes(all_notes, lowest_voice,
                                 pedal_start,
                                 current_tick + coda_duration);
          auto tonic_pedal = generatePedalPoint(
              tonic_pitch, pedal_start, pedal_dur, lowest_voice);
          all_notes.insert(all_notes.end(),
                           tonic_pedal.begin(), tonic_pedal.end());
        }

        current_tick += coda_duration;
        break;
      }
    }
  }

  return all_notes;
}

// ===========================================================================
// Pipeline step 4: Finalize
// ===========================================================================

/// @brief Minimal post-processing and track assembly.
/// @param config Fugue configuration.
/// @param material Pre-computed material.
/// @param plan Structural plan.
/// @param structure Fugue structure with sections.
/// @param all_notes All generated notes (moved in).
/// @return Finalized FugueResult with tracks and structure.
FugueResult finalize(
    const FugueConfig& config, const FugueMaterial& material,
    const FuguePlan& plan, FugueStructure& structure,
    std::vector<NoteEvent> all_notes) {
  FugueResult result;

  uint8_t num_voices = clampVoiceCount(config.num_voices);

  // --- Cadence insertion ---
  {
    CadenceDetectionConfig cadence_config;
    cadence_config.max_bars_without_cadence = 16;
    cadence_config.scan_window_bars = 8;
    cadence_config.deceptive_cadence_probability = 0.20f;

    ensureCadentialCoverage(
        all_notes, structure, config.key, config.is_minor,
        num_voices - 1, num_voices,
        plan.estimated_duration,
        config.seed + 66666u, cadence_config);

    KeySignature home_key_sig;
    home_key_sig.tonic = config.key;
    home_key_sig.is_minor = config.is_minor;
    CadencePlan cadence_plan = CadencePlan::createForFugue(
        structure, home_key_sig, config.is_minor);

    applyCadenceApproachToVoices(
        all_notes, cadence_plan, config.key, config.is_minor,
        num_voices, config.seed + 88888u);
  }

  // --- Within-voice overlap removal ---
  {
    std::sort(all_notes.begin(), all_notes.end(),
              [](const NoteEvent& lhs, const NoteEvent& rhs) {
                if (lhs.voice != rhs.voice) return lhs.voice < rhs.voice;
                if (lhs.start_tick != rhs.start_tick) return lhs.start_tick < rhs.start_tick;
                return lhs.duration > rhs.duration;
              });

    // Remove same-tick duplicates within voice.
    all_notes.erase(
        std::unique(all_notes.begin(), all_notes.end(),
                    [](const NoteEvent& lhs, const NoteEvent& rhs) {
                      return lhs.voice == rhs.voice && lhs.start_tick == rhs.start_tick;
                    }),
        all_notes.end());

    // Truncate overlapping notes.
    for (size_t idx = 0; idx + 1 < all_notes.size(); ++idx) {
      if (all_notes[idx].voice != all_notes[idx + 1].voice) continue;
      Tick end_tick = all_notes[idx].start_tick + all_notes[idx].duration;
      if (end_tick > all_notes[idx + 1].start_tick) {
        all_notes[idx].duration = all_notes[idx + 1].start_tick - all_notes[idx].start_tick;
        if (all_notes[idx].duration == 0) all_notes[idx].duration = 1;
      }
    }
  }

  // --- Strong-beat consonance enforcement ---
  // Adjust episode material notes on strong beats that form dissonant
  // intervals with other simultaneously-sounding notes.  Lighter than
  // the old 11-pass post-validation but catches the worst offenders.
  {
    Tick total_ticks = plan.estimated_duration;
    for (Tick beat = 0; beat < total_ticks; beat += kTicksPerBeat) {
      // Collect indices of notes active at this beat.
      std::vector<size_t> active;
      for (size_t idx = 0; idx < all_notes.size(); ++idx) {
        if (all_notes[idx].start_tick <= beat &&
            all_notes[idx].start_tick + all_notes[idx].duration > beat) {
          active.push_back(idx);
        }
      }
      if (active.size() < 2) continue;

      // Check episode material notes starting on this beat.
      for (size_t a : active) {
        if (all_notes[a].source != BachNoteSource::EpisodeMaterial &&
            all_notes[a].source != BachNoteSource::FreeCounterpoint) {
          continue;
        }
        if (all_notes[a].start_tick != beat) continue;

        bool has_dissonance = false;
        for (size_t b : active) {
          if (a == b) continue;
          int diff = std::abs(static_cast<int>(all_notes[a].pitch) -
                              static_cast<int>(all_notes[b].pitch));
          int simple = interval_util::compoundToSimple(diff);
          if (!interval_util::isConsonance(simple) && diff > 0 && diff < 36) {
            has_dissonance = true;
            break;
          }
        }
        if (!has_dissonance) continue;

        // Collect previous-beat pitches per voice for parallel checking.
        Tick prev_beat = (beat >= kTicksPerBeat) ? beat - kTicksPerBeat : 0;
        std::map<uint8_t, uint8_t> prev_pitches;
        if (beat > 0) {
          for (const auto& n : all_notes) {
            if (n.start_tick <= prev_beat &&
                n.start_tick + n.duration > prev_beat) {
              prev_pitches[n.voice] = n.pitch;
            }
          }
        }
        uint8_t this_voice = all_notes[a].voice;

        // Try small adjustments (±1..±3 semitones) to find consonance.
        // Reject candidates that create parallel P5 or P8.
        uint8_t orig = all_notes[a].pitch;
        for (int delta : {1, -1, 2, -2, 3, -3}) {
          int cand = static_cast<int>(orig) + delta;
          if (cand < 0 || cand > 127) continue;
          bool ok = true;
          for (size_t b : active) {
            if (a == b) continue;
            int diff = std::abs(cand - static_cast<int>(all_notes[b].pitch));
            int simple = interval_util::compoundToSimple(diff);
            if (!interval_util::isConsonance(simple) && diff > 0 && diff < 36) {
              ok = false;
              break;
            }
            // Parallel check: if new interval is P1, P5, or P8, and the
            // same voice pair had the same interval class on the previous
            // beat, reject this candidate.
            if (simple == 0 || simple == 7) {
              uint8_t other_voice = all_notes[b].voice;
              auto it_this = prev_pitches.find(this_voice);
              auto it_other = prev_pitches.find(other_voice);
              if (it_this != prev_pitches.end() &&
                  it_other != prev_pitches.end()) {
                int prev_diff = std::abs(
                    static_cast<int>(it_this->second) -
                    static_cast<int>(it_other->second));
                int prev_simple = interval_util::compoundToSimple(prev_diff);
                if (prev_simple == simple) {
                  ok = false;
                  break;
                }
              }
            }
          }
          if (ok) {
            all_notes[a].pitch = static_cast<uint8_t>(cand);
            break;
          }
        }
      }
    }
  }

  // --- Pedal consonance enforcement ---
  // Ensure episode material notes on strong beats are consonant with the
  // dominant pedal.  Uses the first pedal pitch and spans from first pedal
  // start to last pedal end (matching how vertical analysis sees it).
  // Only adjusts if the new pitch is also consonant with all other active
  // notes, to avoid creating new dissonances.
  {
    Tick first_pedal_start = 0, last_pedal_end = 0;
    uint8_t first_pedal_pitch = 0;
    for (const auto& note : all_notes) {
      if (note.source != BachNoteSource::PedalPoint) continue;
      if (first_pedal_pitch == 0) {
        first_pedal_pitch = note.pitch;
        first_pedal_start = note.start_tick;
      }
      Tick nend = note.start_tick + note.duration;
      if (nend > last_pedal_end) last_pedal_end = nend;
    }

    if (first_pedal_pitch > 0) {
      for (size_t ai = 0; ai < all_notes.size(); ++ai) {
        auto& note = all_notes[ai];
        if (note.source != BachNoteSource::EpisodeMaterial) continue;
        if (note.start_tick < first_pedal_start ||
            note.start_tick >= last_pedal_end) {
          continue;
        }
        if (note.start_tick % kTicksPerBeat != 0) continue;

        int diff = std::abs(static_cast<int>(note.pitch) -
                            static_cast<int>(first_pedal_pitch));
        int simple = diff % 12;
        bool consonant = (simple == 0 || simple == 3 || simple == 4 ||
                          simple == 7 || simple == 8 || simple == 9);
        if (consonant) continue;

        // Collect other active notes at this tick for cross-check.
        std::vector<uint8_t> others;
        for (size_t bi = 0; bi < all_notes.size(); ++bi) {
          if (bi == ai) continue;
          if (all_notes[bi].start_tick <= note.start_tick &&
              all_notes[bi].start_tick + all_notes[bi].duration > note.start_tick) {
            others.push_back(all_notes[bi].pitch);
          }
        }

        // Find nearest pitch consonant with pedal AND all other voices.
        int best_delta = 0;
        int best_cost = 999;
        for (int delta : {1, -1, 2, -2, 3, -3, 4, -4}) {
          int cand = static_cast<int>(note.pitch) + delta;
          if (cand < 0 || cand > 127) continue;

          // Check pedal consonance.
          int cdiff = std::abs(cand - static_cast<int>(first_pedal_pitch));
          int csimple = cdiff % 12;
          bool ccons = (csimple == 0 || csimple == 3 || csimple == 4 ||
                        csimple == 7 || csimple == 8 || csimple == 9);
          if (!ccons) continue;

          // Check consonance with all other active voices.
          bool all_ok = true;
          for (uint8_t op : others) {
            int odiff = std::abs(cand - static_cast<int>(op));
            int osimple = interval_util::compoundToSimple(odiff);
            if (!interval_util::isConsonance(osimple) && odiff > 0 && odiff < 36) {
              all_ok = false;
              break;
            }
          }
          if (all_ok && std::abs(delta) < best_cost) {
            best_delta = delta;
            best_cost = std::abs(delta);
          }
        }
        if (best_delta != 0) {
          note.pitch = static_cast<uint8_t>(
              static_cast<int>(note.pitch) + best_delta);
        }
      }
    }
  }

  // --- Create organ tracks + assign + sort ---
  result.tracks = createOrganTracks(num_voices);
  assignNotesToTracks(all_notes, result.tracks);
  sortTrackNotes(result.tracks);

  // --- Bar-resolution timeline ---
  {
    HarmonicTimeline bar_timeline;
    struct Region {
      Key key;
      Tick start;
      Tick end;
    };
    std::vector<Region> regions;
    const auto& mods = plan.tonal_plan.modulations;
    Tick total_ticks = plan.estimated_duration;
    if (mods.empty()) {
      regions.push_back({config.key, 0, total_ticks});
    } else {
      if (mods[0].tick > 0) {
        regions.push_back({config.key, 0, mods[0].tick});
      }
      for (size_t idx = 0; idx < mods.size(); ++idx) {
        Tick rstart = mods[idx].tick;
        Tick rend = (idx + 1 < mods.size()) ? mods[idx + 1].tick : total_ticks;
        if (rend > rstart) {
          regions.push_back({mods[idx].target_key, rstart, rend});
        }
      }
    }
    for (const auto& region : regions) {
      Tick rdur = region.end - region.start;
      if (rdur == 0) continue;
      KeySignature ks;
      ks.tonic = region.key;
      ks.is_minor = config.is_minor;
      HarmonicTimeline rtl = HarmonicTimeline::createProgression(
          ks, rdur, HarmonicResolution::Bar, ProgressionType::CircleOfFifths);
      for (const auto& evt : rtl.events()) {
        HarmonicEvent offset_ev = evt;
        offset_ev.tick += region.start;
        offset_ev.end_tick += region.start;
        bar_timeline.addEvent(offset_ev);
      }
    }
    result.timeline = bar_timeline;
  }

  // --- Harmonic rhythm factors ---
  {
    auto cadence_ticks_vec = extractCadenceTicks(
        CadencePlan::createForFugue(
            structure,
            KeySignature{config.key, config.is_minor},
            config.is_minor));
    applyRhythmFactors(result.timeline.mutableEvents(),
                        plan.estimated_duration, cadence_ticks_vec);
  }

  // --- Picardy third ---
  if (config.enable_picardy && config.is_minor) {
    KeySignature home_key_sig;
    home_key_sig.tonic = config.key;
    home_key_sig.is_minor = config.is_minor;
    for (auto& track : result.tracks) {
      applyPicardyToFinalChord(track.notes, home_key_sig,
                               plan.estimated_duration - kTicksPerBar);
    }
  }

  // --- Extended registration ---
  {
    auto ext_plan = createExtendedRegistrationPlan(
        structure.sections, plan.estimated_duration);
    applyExtendedRegistrationPlan(result.tracks, ext_plan);
  }

  // --- Re-sort tracks after Picardy and registration modifications ---
  sortTrackNotes(result.tracks);

  // --- Populate result fields ---
  result.success = true;
  result.structure = std::move(structure);
  result.attempts = material.subject_attempts;
  result.generation_timeline = plan.detailed_timeline;

  return result;
}

}  // namespace

// ===========================================================================
// Public API
// ===========================================================================

FugueResult generateFuguePipeline(const FugueConfig& config) {
  // Step 1: Build material.
  FugueMaterial material = buildMaterial(config);
  if (material.subject.notes.empty()) {
    FugueResult result;
    result.error_message = "Failed to generate a valid subject";
    return result;
  }

  // Step 2: Plan structure.
  FuguePlan plan = planStructure(config, material);

  // Step 3: Generate sections.
  FugueStructure structure;
  std::vector<NoteEvent> all_notes = generateSections(config, material, plan, structure);

  // Step 4: Finalize.
  return finalize(config, material, plan, structure, std::move(all_notes));
}

}  // namespace bach
