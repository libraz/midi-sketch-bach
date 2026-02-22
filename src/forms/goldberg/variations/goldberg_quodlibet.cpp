// Quodlibet variation generator implementation for Goldberg Variations (Var 30).
// Combines two folk melodies over the structural grid: humor meets structure.

#include "forms/goldberg/variations/goldberg_quodlibet.h"

#include <algorithm>
#include <random>

#include "core/note_creator.h"
#include "core/pitch_utils.h"
#include "forms/goldberg/goldberg_binary.h"
#include "harmony/chord_types.h"

namespace bach {

namespace {

/// Number of bars in the Goldberg structural grid.
constexpr int kGridBars = 32;

/// Number of bars per section (half of the grid).
constexpr int kSectionBars = 16;

/// Bass voice index.
constexpr uint8_t kBassVoice = 2;

/// Bass register limits (C2-C4).
constexpr uint8_t kBassLow = 36;   // C2
constexpr uint8_t kBassHigh = 60;  // C4

/// Upper melody register limits (G3-D5).
constexpr uint8_t kUpperLow = 55;   // G3
constexpr uint8_t kUpperHigh = 74;  // D5

/// Lower melody register limits (C3-G4).
constexpr uint8_t kLowerLow = 48;   // C3
constexpr uint8_t kLowerHigh = 67;  // G4

/// Bass velocity (slightly quieter than melody voices).
constexpr uint8_t kBassVelocity = 70;

/// Melody velocity.
constexpr uint8_t kMelodyVelocity = 80;

// ---------------------------------------------------------------------------
// Folk melody 1: "Ich bin so lang nicht bei dir g'west"
// (I've been away from you so long)
// Simplified diatonic melody in G major. A gentle, stepwise folk tune
// with a phrase arc that rises to C5 and returns to G4.
// ---------------------------------------------------------------------------

static constexpr uint8_t kMelody1Pitches[] = {
    67, 69, 71, 72, 71, 69, 67, 66, 67,   // G4 A4 B4 C5 B4 A4 G4 F#4 G4
    67, 69, 71, 72, 74, 72, 71, 69, 67     // G4 A4 B4 C5 D5 C5 B4 A4 G4
};

static constexpr Tick kMelody1Durations[] = {
    480, 480, 480, 480, 480, 480, 480, 480, 960,  // quarter notes, last = half
    480, 480, 480, 480, 480, 480, 480, 480, 960   // quarter notes, last = half
};

static constexpr int kMelody1Length = sizeof(kMelody1Pitches) / sizeof(kMelody1Pitches[0]);

// ---------------------------------------------------------------------------
// Folk melody 2: "Kraut und Rueben haben mich vertrieben"
// (Cabbage and turnips drove me away)
// A more rhythmically active melody starting from C5, with shorter note
// values (eighth notes) giving it a bouncy, humorous character.
// ---------------------------------------------------------------------------

static constexpr uint8_t kMelody2Pitches[] = {
    72, 71, 69, 67, 69, 71, 72, 74, 72,   // C5 B4 A4 G4 A4 B4 C5 D5 C5
    71, 69, 67, 66, 67, 69, 71, 69, 67     // B4 A4 G4 F#4 G4 A4 B4 A4 G4
};

static constexpr Tick kMelody2Durations[] = {
    240, 240, 240, 240, 480, 480, 480, 480, 960,  // eighths then quarters, last = half
    240, 240, 240, 240, 480, 480, 480, 480, 960   // eighths then quarters, last = half
};

static constexpr int kMelody2Length = sizeof(kMelody2Pitches) / sizeof(kMelody2Pitches[0]);

/// @brief Build chord tones for a bar's harmony in a given key.
/// @param bar_info Structural bar info with chord degree.
/// @param key Key signature for tonic reference.
/// @param low_pitch Lowest allowed pitch.
/// @param high_pitch Highest allowed pitch.
/// @return Vector of chord-tone MIDI pitches within the range.
std::vector<uint8_t> getBarChordTones(
    const StructuralBarInfo& bar_info,
    const KeySignature& key,
    uint8_t low_pitch,
    uint8_t high_pitch) {
  // Get root pitch class from chord degree and key tonic.
  uint8_t tonic_pc = static_cast<uint8_t>(key.tonic);
  uint8_t degree_offset = key.is_minor
      ? degreeMinorSemitones(bar_info.chord_degree)
      : degreeSemitones(bar_info.chord_degree);
  uint8_t root_pc = (tonic_pc + degree_offset) % 12;

  // Get chord quality for this degree.
  ChordQuality quality = key.is_minor
      ? minorKeyQuality(bar_info.chord_degree)
      : majorKeyQuality(bar_info.chord_degree);

  // Determine third and fifth intervals from quality.
  int third_interval = 4;  // Major third by default.
  int fifth_interval = 7;  // Perfect fifth by default.
  switch (quality) {
    case ChordQuality::Minor:
    case ChordQuality::Minor7:
    case ChordQuality::Diminished:
    case ChordQuality::Diminished7:
    case ChordQuality::HalfDiminished7:
      third_interval = 3;
      break;
    default:
      break;
  }
  if (quality == ChordQuality::Diminished ||
      quality == ChordQuality::Diminished7 ||
      quality == ChordQuality::HalfDiminished7) {
    fifth_interval = 6;
  } else if (quality == ChordQuality::Augmented) {
    fifth_interval = 8;
  }

  uint8_t third_pc = (root_pc + third_interval) % 12;
  uint8_t fifth_pc = (root_pc + fifth_interval) % 12;

  // Collect all pitches within range that match root, third, or fifth.
  std::vector<uint8_t> chord_tones;
  chord_tones.reserve(12);
  for (int pitch = low_pitch; pitch <= high_pitch; ++pitch) {
    int pitch_class = getPitchClass(static_cast<uint8_t>(pitch));
    if (pitch_class == root_pc || pitch_class == third_pc || pitch_class == fifth_pc) {
      chord_tones.push_back(static_cast<uint8_t>(pitch));
    }
  }

  return chord_tones;
}

/// @brief Compute motion direction between two pitches.
/// @param from Starting pitch.
/// @param to_pitch Ending pitch.
/// @return -1 for descending, 0 for same, +1 for ascending.
int motionDirection(uint8_t from, uint8_t to_pitch) {
  if (to_pitch > from) return 1;
  if (to_pitch < from) return -1;
  return 0;
}

}  // namespace

// ---------------------------------------------------------------------------
// QuodlibetGenerator::generate
// ---------------------------------------------------------------------------

QuodlibetResult QuodlibetGenerator::generate(
    const GoldbergStructuralGrid& grid,
    const KeySignature& key,
    const TimeSignature& time_sig,
    uint32_t seed) const {
  QuodlibetResult result;
  std::mt19937 rng(seed);

  Tick ticks_per_bar = time_sig.ticksPerBar();
  std::vector<NoteEvent> all_notes;
  all_notes.reserve(256);

  // Part A (bars 0-15): Melody 1 in upper voice (0), Melody 2 in lower voice (1).
  auto melody1_upper = placeMelodyOnGrid(
      kMelody1Pitches, kMelody1Durations, kMelody1Length,
      0, kSectionBars, grid, key, time_sig, 0, rng);

  auto melody2_lower = placeMelodyOnGrid(
      kMelody2Pitches, kMelody2Durations, kMelody2Length,
      0, kSectionBars, grid, key, time_sig, 1, rng);

  // Part B (bars 16-31): Swap melodies for variety.
  // Melody 2 in upper voice, Melody 1 in lower voice.
  Tick part_b_offset = static_cast<Tick>(kSectionBars) * ticks_per_bar;

  auto melody2_upper = placeMelodyOnGrid(
      kMelody2Pitches, kMelody2Durations, kMelody2Length,
      part_b_offset, kSectionBars, grid, key, time_sig, 0, rng);

  auto melody1_lower = placeMelodyOnGrid(
      kMelody1Pitches, kMelody1Durations, kMelody1Length,
      part_b_offset, kSectionBars, grid, key, time_sig, 1, rng);

  // Merge melody notes.
  all_notes.insert(all_notes.end(), melody1_upper.begin(), melody1_upper.end());
  all_notes.insert(all_notes.end(), melody2_lower.begin(), melody2_lower.end());
  all_notes.insert(all_notes.end(), melody2_upper.begin(), melody2_upper.end());
  all_notes.insert(all_notes.end(), melody1_lower.begin(), melody1_lower.end());

  // Generate structural bass from grid.
  auto bass = generateBassLine(grid, key, time_sig);
  all_notes.insert(all_notes.end(), bass.begin(), bass.end());

  // Sort by start tick.
  std::sort(all_notes.begin(), all_notes.end(),
            [](const NoteEvent& lhs, const NoteEvent& rhs) {
              return lhs.start_tick < rhs.start_tick;
            });

  // Apply binary repeats: ||: A :||: B :||
  Tick section_ticks = static_cast<Tick>(kSectionBars) * ticks_per_bar;
  result.notes = applyBinaryRepeats(all_notes, section_ticks);
  result.success = !result.notes.empty();

  return result;
}

// ---------------------------------------------------------------------------
// QuodlibetGenerator::placeMelodyOnGrid
// ---------------------------------------------------------------------------

std::vector<NoteEvent> QuodlibetGenerator::placeMelodyOnGrid(
    const uint8_t* melody_pitches,
    const Tick* melody_durations,
    int melody_length,
    Tick start_tick,
    int bar_count,
    const GoldbergStructuralGrid& grid,
    const KeySignature& key,
    const TimeSignature& time_sig,
    uint8_t voice,
    std::mt19937& /*rng*/) const {
  std::vector<NoteEvent> notes;
  notes.reserve(static_cast<size_t>(bar_count) * 4);

  Tick ticks_per_bar = time_sig.ticksPerBar();
  Tick section_end = start_tick + static_cast<Tick>(bar_count) * ticks_per_bar;

  // Determine register limits based on voice.
  uint8_t reg_low = (voice == 0) ? kUpperLow : kLowerLow;
  uint8_t reg_high = (voice == 0) ? kUpperHigh : kLowerHigh;

  // Place melody notes in a loop, repeating the melody to fill all bars.
  Tick current_tick = start_tick;
  int melody_idx = 0;
  uint8_t prev_pitch = 0;

  while (current_tick < section_end) {
    uint8_t raw_pitch = melody_pitches[melody_idx];
    Tick raw_duration = melody_durations[melody_idx];

    // Determine which bar this note falls in (relative to grid start).
    int absolute_bar = static_cast<int>(current_tick / ticks_per_bar);
    int grid_bar = absolute_bar % kGridBars;
    const auto& bar_info = grid.getBar(grid_bar);

    // Determine beat position within bar for clash policy.
    Tick pos_in_bar = current_tick % ticks_per_bar;
    int beat_in_current_bar = static_cast<int>(pos_in_bar / kTicksPerBeat);
    bool is_strong_beat = (beat_in_current_bar == 0);

    // Place pitch in the target register using nearestOctaveShift.
    int target_center = (reg_low + reg_high) / 2;
    int diff = static_cast<int>(raw_pitch) - target_center;
    int shift = nearestOctaveShift(diff);
    uint8_t register_pitch = clampPitch(
        static_cast<int>(raw_pitch) - shift, reg_low, reg_high);

    // Harmonic alignment: snap to chord tone on strong beats.
    uint8_t aligned_pitch = register_pitch;
    if (is_strong_beat) {
      // On strong beats, always snap to nearest chord tone within register (grid priority).
      auto chord_tones = getBarChordTones(bar_info, key, reg_low, reg_high);
      if (!chord_tones.empty()) {
        uint8_t best = chord_tones[0];
        int best_dist = absoluteInterval(register_pitch, best);
        for (size_t cti = 1; cti < chord_tones.size(); ++cti) {
          int dist = absoluteInterval(register_pitch, chord_tones[cti]);
          if (dist < best_dist) {
            best_dist = dist;
            best = chord_tones[cti];
          }
        }
        aligned_pitch = best;
      }
    } else {
      // On weak beats, allow small clashes but snap if too far.
      auto chord_tones = getBarChordTones(bar_info, key, reg_low, reg_high);
      if (!chord_tones.empty()) {
        // Find distance to nearest chord tone.
        int min_distance = 127;
        for (auto ct_pitch : chord_tones) {
          int dist = absoluteInterval(register_pitch, ct_pitch);
          if (dist < min_distance) min_distance = dist;
        }
        if (min_distance > QuodlibetClashPolicy::kWeakBeatMaxClash) {
          aligned_pitch = snapToChordTone(register_pitch, bar_info, key);
          aligned_pitch = clampPitch(static_cast<int>(aligned_pitch), reg_low, reg_high);
        }
      }
    }

    // Preserve melody contour on weak beats: if the original melody went up but
    // the adjusted pitch goes down (or vice versa), try to find a chord tone in
    // the right direction. Skip on strong beats to preserve harmonic alignment.
    if (!is_strong_beat && prev_pitch > 0 && melody_idx > 0) {
      int original_dir = motionDirection(melody_pitches[melody_idx - 1], raw_pitch);
      int actual_dir = motionDirection(prev_pitch, aligned_pitch);

      if (original_dir != 0 && actual_dir != original_dir) {
        // Try to find a chord tone that preserves contour.
        auto chord_tones = getBarChordTones(bar_info, key, reg_low, reg_high);
        uint8_t best_contour_pitch = aligned_pitch;
        int best_dist = 127;
        for (auto ct_pitch : chord_tones) {
          int dir = motionDirection(prev_pitch, ct_pitch);
          if (dir == original_dir) {
            int dist = absoluteInterval(register_pitch, ct_pitch);
            if (dist < best_dist) {
              best_dist = dist;
              best_contour_pitch = ct_pitch;
            }
          }
        }
        if (best_dist < 127) {
          aligned_pitch = best_contour_pitch;
        }
      }
    }

    // Clamp duration so it doesn't extend beyond section end.
    Tick clamped_duration = raw_duration;
    if (current_tick + clamped_duration > section_end) {
      clamped_duration = section_end - current_tick;
    }
    if (clamped_duration == 0) break;

    // Create note via createBachNote.
    BachNoteOptions opts{};
    opts.voice = voice;
    opts.desired_pitch = aligned_pitch;
    opts.tick = current_tick;
    opts.duration = clamped_duration;
    opts.velocity = kMelodyVelocity;
    opts.source = BachNoteSource::QuodlibetMelody;

    auto note_result = createBachNote(nullptr, nullptr, nullptr, opts);
    if (note_result.accepted) {
      notes.push_back(note_result.note);
      prev_pitch = note_result.note.pitch;
    }

    current_tick += raw_duration;
    melody_idx = (melody_idx + 1) % melody_length;
  }

  return notes;
}

// ---------------------------------------------------------------------------
// QuodlibetGenerator::generateBassLine
// ---------------------------------------------------------------------------

std::vector<NoteEvent> QuodlibetGenerator::generateBassLine(
    const GoldbergStructuralGrid& grid,
    const KeySignature& /*key*/,
    const TimeSignature& time_sig) const {
  std::vector<NoteEvent> bass_notes;
  bass_notes.reserve(kGridBars * 2);

  Tick ticks_per_bar = time_sig.ticksPerBar();

  for (int bar_idx = 0; bar_idx < kGridBars; ++bar_idx) {
    const auto& bar_info = grid.getBar(bar_idx);
    uint8_t primary_pitch = bar_info.bass_motion.primary_pitch;

    // Place primary pitch in bass register using nearestOctaveShift.
    int target_center = (kBassLow + kBassHigh) / 2;
    int diff = static_cast<int>(primary_pitch) - target_center;
    int shift = nearestOctaveShift(diff);
    uint8_t bass_pitch = clampPitch(
        static_cast<int>(primary_pitch) - shift, kBassLow, kBassHigh);

    Tick bar_start = static_cast<Tick>(bar_idx) * ticks_per_bar;

    // At cadence bars, split into primary + resolution.
    if (bar_info.phrase_pos == PhrasePosition::Cadence &&
        bar_info.bass_motion.resolution_pitch.has_value()) {
      Tick primary_dur = ticks_per_bar * 2 / 3;
      Tick resolution_dur = ticks_per_bar - primary_dur;

      BachNoteOptions primary_opts{};
      primary_opts.voice = kBassVoice;
      primary_opts.desired_pitch = bass_pitch;
      primary_opts.tick = bar_start;
      primary_opts.duration = primary_dur;
      primary_opts.velocity = kBassVelocity;
      primary_opts.source = BachNoteSource::GoldbergBass;

      auto primary_result = createBachNote(nullptr, nullptr, nullptr, primary_opts);
      if (primary_result.accepted) {
        bass_notes.push_back(primary_result.note);
      }

      uint8_t res_pitch_raw = bar_info.bass_motion.resolution_pitch.value();
      int res_diff = static_cast<int>(res_pitch_raw) - target_center;
      int res_shift = nearestOctaveShift(res_diff);
      uint8_t res_pitch = clampPitch(
          static_cast<int>(res_pitch_raw) - res_shift, kBassLow, kBassHigh);

      BachNoteOptions res_opts{};
      res_opts.voice = kBassVoice;
      res_opts.desired_pitch = res_pitch;
      res_opts.tick = bar_start + primary_dur;
      res_opts.duration = resolution_dur;
      res_opts.velocity = kBassVelocity;
      res_opts.source = BachNoteSource::GoldbergBass;

      auto res_result = createBachNote(nullptr, nullptr, nullptr, res_opts);
      if (res_result.accepted) {
        bass_notes.push_back(res_result.note);
      }
    } else {
      BachNoteOptions opts{};
      opts.voice = kBassVoice;
      opts.desired_pitch = bass_pitch;
      opts.tick = bar_start;
      opts.duration = ticks_per_bar;
      opts.velocity = kBassVelocity;
      opts.source = BachNoteSource::GoldbergBass;

      auto note_result = createBachNote(nullptr, nullptr, nullptr, opts);
      if (note_result.accepted) {
        bass_notes.push_back(note_result.note);
      }
    }
  }

  return bass_notes;
}

// ---------------------------------------------------------------------------
// QuodlibetGenerator::snapToChordTone
// ---------------------------------------------------------------------------

uint8_t QuodlibetGenerator::snapToChordTone(
    uint8_t pitch,
    const StructuralBarInfo& bar_info,
    const KeySignature& key) const {
  // Get chord tones across a generous range around the target pitch.
  uint8_t search_low = (pitch >= 12) ? static_cast<uint8_t>(pitch - 12) : 0;
  uint8_t search_high = (pitch <= 115) ? static_cast<uint8_t>(pitch + 12) : 127;

  auto chord_tones = getBarChordTones(bar_info, key, search_low, search_high);
  if (chord_tones.empty()) return pitch;  // Fallback: no chord tones found.

  // Find the nearest chord tone.
  uint8_t best = chord_tones[0];
  int best_dist = absoluteInterval(pitch, best);
  for (size_t idx = 1; idx < chord_tones.size(); ++idx) {
    int dist = absoluteInterval(pitch, chord_tones[idx]);
    if (dist < best_dist) {
      best_dist = dist;
      best = chord_tones[idx];
    }
  }

  return best;
}

// ---------------------------------------------------------------------------
// QuodlibetGenerator::validateCadenceAlignment
// ---------------------------------------------------------------------------

bool QuodlibetGenerator::validateCadenceAlignment(
    const std::vector<NoteEvent>& notes,
    const GoldbergStructuralGrid& grid,
    const TimeSignature& time_sig) const {
  if (notes.empty()) return false;

  Tick ticks_per_bar = time_sig.ticksPerBar();
  int aligned_count = 0;
  int cadence_count = 0;

  for (int bar_idx = 0; bar_idx < kGridBars; ++bar_idx) {
    if (!grid.isCadenceBar(bar_idx)) continue;
    ++cadence_count;

    Tick bar_end = static_cast<Tick>(bar_idx + 1) * ticks_per_bar;

    // Check if any melody note ends near this cadence bar boundary.
    for (const auto& note : notes) {
      Tick note_end = note.start_tick + note.duration;
      // Allow a beat of tolerance for phrase ending alignment.
      if (note_end >= bar_end - kTicksPerBeat && note_end <= bar_end + kTicksPerBeat) {
        ++aligned_count;
        break;
      }
    }
  }

  // At least half of cadence positions should align with phrase endings.
  return cadence_count == 0 || aligned_count * 2 >= cadence_count;
}

}  // namespace bach
