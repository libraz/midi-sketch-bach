// Goldberg Aria (Sarabande) generator implementation.

#include "forms/goldberg/goldberg_aria.h"

#include <algorithm>
#include <random>

#include "core/note_creator.h"
#include "core/pitch_utils.h"
#include "forms/goldberg/goldberg_binary.h"
#include "forms/goldberg/goldberg_figuren.h"
#include "ornament/ornament_engine.h"

namespace bach {

namespace {

/// Number of bars in the Goldberg structural grid.
constexpr int kGridBars = 32;

/// Bass voice velocity (slightly quieter than melody).
constexpr uint8_t kBassVelocity = 70;

/// Bass register limits (C2-C4).
constexpr uint8_t kBassLow = 36;   // C2
constexpr uint8_t kBassHigh = 60;  // C4

/// Ornament density for the Aria (highest among all variations).
constexpr float kAriaOrnamentDensity = 0.35f;

}  // namespace

// ---------------------------------------------------------------------------
// AriaGenerator::generate
// ---------------------------------------------------------------------------

AriaResult AriaGenerator::generate(
    const GoldbergStructuralGrid& grid,
    const KeySignature& key,
    const TimeSignature& time_sig,
    uint32_t seed) const {
  AriaResult result;
  std::mt19937 rng(seed);

  // Sarabande FiguraProfile: slow, stately, beat 2 emphasis.
  FiguraProfile aria_profile;
  aria_profile.primary = FiguraType::Sarabande;
  aria_profile.secondary = FiguraType::Sarabande;
  aria_profile.notes_per_beat = 1;
  aria_profile.direction = DirectionBias::Symmetric;
  aria_profile.chord_tone_ratio = 0.8f;
  aria_profile.sequence_probability = 0.2f;

  // Generate melody via FigurenGenerator (voice 0 = upper register).
  FigurenGenerator figuren;
  auto melody = figuren.generate(aria_profile, grid, key, time_sig, 0, seed);

  // Set BachNoteSource to GoldbergAria for all melody notes.
  for (auto& note : melody) {
    note.source = BachNoteSource::GoldbergAria;
    note.voice = 0;
  }

  // Generate bass line from structural grid.
  auto bass = generateBassLine(grid, key, time_sig, rng);

  // Apply ornaments to melody (Aria has the highest ornament density).
  applyOrnaments(melody, key, time_sig, rng);

  result.melody_notes = std::move(melody);
  result.bass_notes = std::move(bass);
  result.success = true;

  return result;
}

// ---------------------------------------------------------------------------
// AriaGenerator::createDaCapo
// ---------------------------------------------------------------------------

AriaResult AriaGenerator::createDaCapo(
    const AriaResult& original,
    Tick tick_offset) {
  AriaResult result;

  result.melody_notes.reserve(original.melody_notes.size());
  for (auto note : original.melody_notes) {
    note.start_tick += tick_offset;
    result.melody_notes.push_back(note);
  }

  result.bass_notes.reserve(original.bass_notes.size());
  for (auto note : original.bass_notes) {
    note.start_tick += tick_offset;
    result.bass_notes.push_back(note);
  }

  result.success = original.success;
  return result;
}

// ---------------------------------------------------------------------------
// AriaGenerator::generateBassLine
// ---------------------------------------------------------------------------

std::vector<NoteEvent> AriaGenerator::generateBassLine(
    const GoldbergStructuralGrid& grid,
    const KeySignature& /*key*/,
    const TimeSignature& time_sig,
    std::mt19937& /*rng*/) const {
  std::vector<NoteEvent> bass_notes;
  bass_notes.reserve(kGridBars * 2);  // Up to 2 notes per bar at cadences.

  Tick ticks_per_bar = time_sig.ticksPerBar();

  for (int bar_idx = 0; bar_idx < kGridBars; ++bar_idx) {
    const auto& bar_info = grid.getBar(bar_idx);
    uint8_t primary_pitch = bar_info.bass_motion.primary_pitch;

    // Place primary pitch in bass register (C2-C4) using nearestOctaveShift.
    int target_center = (kBassLow + kBassHigh) / 2;  // ~C3 (48)
    int diff = static_cast<int>(primary_pitch) - target_center;
    int shift = nearestOctaveShift(diff);
    uint8_t bass_pitch = clampPitch(
        static_cast<int>(primary_pitch) - shift, kBassLow, kBassHigh);

    Tick bar_start = static_cast<Tick>(bar_idx) * ticks_per_bar;

    // At cadence bars, split into two notes: primary + resolution.
    if (bar_info.phrase_pos == PhrasePosition::Cadence &&
        bar_info.bass_motion.resolution_pitch.has_value()) {
      // Primary note: first 2/3 of bar.
      Tick primary_dur = ticks_per_bar * 2 / 3;
      Tick resolution_dur = ticks_per_bar - primary_dur;

      BachNoteOptions primary_opts{};
      primary_opts.voice = 1;
      primary_opts.desired_pitch = bass_pitch;
      primary_opts.tick = bar_start;
      primary_opts.duration = primary_dur;
      primary_opts.velocity = kBassVelocity;
      primary_opts.source = BachNoteSource::GoldbergBass;

      auto primary_result = createBachNote(nullptr, nullptr, nullptr, primary_opts);
      if (primary_result.accepted) {
        bass_notes.push_back(primary_result.note);
      }

      // Resolution note: last 1/3 of bar.
      uint8_t res_pitch_raw = bar_info.bass_motion.resolution_pitch.value();
      int res_diff = static_cast<int>(res_pitch_raw) - target_center;
      int res_shift = nearestOctaveShift(res_diff);
      uint8_t res_pitch = clampPitch(
          static_cast<int>(res_pitch_raw) - res_shift, kBassLow, kBassHigh);

      BachNoteOptions res_opts{};
      res_opts.voice = 1;
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
      // Non-cadence bar: one note spanning the full bar.
      BachNoteOptions opts{};
      opts.voice = 1;
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
// AriaGenerator::applyOrnaments
// ---------------------------------------------------------------------------

void AriaGenerator::applyOrnaments(
    std::vector<NoteEvent>& notes,
    const KeySignature& /*key*/,
    const TimeSignature& /*time_sig*/,
    std::mt19937& rng) const {
  // Build an OrnamentContext with high density for the Aria.
  OrnamentConfig config;
  config.enable_trill = true;
  config.enable_mordent = true;
  config.enable_turn = true;
  config.enable_appoggiatura = true;
  config.enable_pralltriller = true;
  config.enable_vorschlag = true;
  config.enable_nachschlag = true;
  config.enable_compound = true;
  config.ornament_density = kAriaOrnamentDensity;

  OrnamentContext context;
  context.config = config;
  // Use Respond role (normal density, not Subject which reduces density).
  context.role = VoiceRole::Respond;
  context.seed = rng();

  // Apply ornaments. The OrnamentEngine will select appropriate ornament types
  // based on metric position (trills on strong beats, mordents on weak beats).
  auto ornamented = bach::applyOrnaments(notes, context);

  // Set GoldbergAria source on all ornamented notes for provenance.
  for (auto& note : ornamented) {
    if (note.source == BachNoteSource::Ornament) {
      // Keep Ornament source for ornamental notes -- they originated from
      // the ornament engine but belong to the Aria voice.
    } else {
      note.source = BachNoteSource::GoldbergAria;
    }
  }

  notes = std::move(ornamented);
}

}  // namespace bach
