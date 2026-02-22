// Ornamental variation and trill etude generator implementation.

#include "forms/goldberg/variations/goldberg_ornamental.h"

#include <algorithm>
#include <random>

#include "core/note_creator.h"
#include "core/pitch_utils.h"
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

/// Ornament density for ornamental variations (restrained piano style).
constexpr float kOrnamentalDensity = 0.08f;

/// Ornament density for trill etude variations (trill-focused, restrained).
constexpr float kTrillEtudeDensity = 0.20f;

}  // namespace

// ---------------------------------------------------------------------------
// OrnamentalGenerator::isSupportedVariation
// ---------------------------------------------------------------------------

bool OrnamentalGenerator::isSupportedVariation(int variation_number) {
  return variation_number == 1 || variation_number == 5 ||
         variation_number == 13 || variation_number == 14 ||
         variation_number == 28;
}

// ---------------------------------------------------------------------------
// OrnamentalGenerator::buildProfile
// ---------------------------------------------------------------------------

FiguraProfile OrnamentalGenerator::buildProfile(int variation_number,
                                                GoldbergVariationType& type) {
  FiguraProfile profile;

  switch (variation_number) {
    case 1:
      // Var 1: Ornamental Circulatio, 4 notes/beat, moderate chord tone ratio.
      type = GoldbergVariationType::Ornamental;
      profile.primary = FiguraType::Circulatio;
      profile.secondary = FiguraType::Circulatio;
      profile.notes_per_beat = 4;
      profile.direction = DirectionBias::Symmetric;
      profile.chord_tone_ratio = 0.6f;
      profile.sequence_probability = 0.3f;
      break;

    case 5:
      // Var 5: Ornamental Circulatio, ascending bias, slightly freer.
      type = GoldbergVariationType::Ornamental;
      profile.primary = FiguraType::Circulatio;
      profile.secondary = FiguraType::Circulatio;
      profile.notes_per_beat = 4;
      profile.direction = DirectionBias::Ascending;
      profile.chord_tone_ratio = 0.5f;
      profile.sequence_probability = 0.3f;
      break;

    case 13:
      // Var 13: Sarabande ornamental, slow and stately, high chord tone ratio.
      type = GoldbergVariationType::Ornamental;
      profile.primary = FiguraType::Sarabande;
      profile.secondary = FiguraType::Sarabande;
      profile.notes_per_beat = 1;
      profile.direction = DirectionBias::Symmetric;
      profile.chord_tone_ratio = 0.8f;
      profile.sequence_probability = 0.2f;
      break;

    case 14:
      // Var 14: Trill etude, Trillo pattern, 4 notes/beat, trill-like texture.
      type = GoldbergVariationType::TrillEtude;
      profile.primary = FiguraType::Trillo;
      profile.secondary = FiguraType::Trillo;
      profile.notes_per_beat = 4;
      profile.direction = DirectionBias::Symmetric;
      profile.chord_tone_ratio = 0.7f;
      profile.sequence_probability = 0.1f;
      break;

    case 28:
      // Var 28: Trill etude, Trillo pattern, 4 notes/beat, virtuosic.
      type = GoldbergVariationType::TrillEtude;
      profile.primary = FiguraType::Trillo;
      profile.secondary = FiguraType::Trillo;
      profile.notes_per_beat = 4;
      profile.direction = DirectionBias::Symmetric;
      profile.chord_tone_ratio = 0.7f;
      profile.sequence_probability = 0.1f;
      break;

    default:
      // Fallback: treat as Var 1 profile.
      type = GoldbergVariationType::Ornamental;
      profile.primary = FiguraType::Circulatio;
      profile.secondary = FiguraType::Circulatio;
      profile.notes_per_beat = 4;
      profile.direction = DirectionBias::Symmetric;
      profile.chord_tone_ratio = 0.6f;
      profile.sequence_probability = 0.3f;
      break;
  }

  return profile;
}

// ---------------------------------------------------------------------------
// OrnamentalGenerator::generate
// ---------------------------------------------------------------------------

OrnamentalResult OrnamentalGenerator::generate(
    int variation_number,
    const GoldbergStructuralGrid& grid,
    const KeySignature& key,
    const TimeSignature& time_sig,
    uint32_t seed) const {
  OrnamentalResult result;

  if (!isSupportedVariation(variation_number)) {
    return result;  // success=false for unsupported variation numbers.
  }

  std::mt19937 rng(seed);

  // Build variation-specific FiguraProfile.
  GoldbergVariationType var_type{};
  FiguraProfile profile = buildProfile(variation_number, var_type);

  // Generate melody via FigurenGenerator (voice 0 = upper register).
  FigurenGenerator figuren;
  auto melody = figuren.generate(profile, grid, key, time_sig, 0, rng(),
                                   nullptr, 0.5f);

  // Set BachNoteSource to GoldbergFigura for all melody notes.
  for (auto& note : melody) {
    note.source = BachNoteSource::GoldbergFigura;
    note.voice = 0;
  }

  // Apply ornaments based on variation type.
  bool is_trill_etude = (var_type == GoldbergVariationType::TrillEtude);
  applyOrnaments(melody, is_trill_etude, rng());

  // Generate bass line from structural grid.
  auto bass = generateBassLine(grid, time_sig);

  // Merge melody and bass into result.
  result.notes.reserve(melody.size() + bass.size());
  result.notes.insert(result.notes.end(), melody.begin(), melody.end());
  result.notes.insert(result.notes.end(), bass.begin(), bass.end());

  // Sort by start_tick for consistent ordering.
  std::sort(result.notes.begin(), result.notes.end(),
            [](const NoteEvent& lhs, const NoteEvent& rhs) {
              return lhs.start_tick < rhs.start_tick;
            });

  result.success = true;
  return result;
}

// ---------------------------------------------------------------------------
// OrnamentalGenerator::generateBassLine
// ---------------------------------------------------------------------------

std::vector<NoteEvent> OrnamentalGenerator::generateBassLine(
    const GoldbergStructuralGrid& grid,
    const TimeSignature& time_sig) const {
  std::vector<NoteEvent> bass_notes;
  bass_notes.reserve(kGridBars * 2);

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
// OrnamentalGenerator::applyOrnaments
// ---------------------------------------------------------------------------

void OrnamentalGenerator::applyOrnaments(
    std::vector<NoteEvent>& notes,
    bool is_trill_etude,
    uint32_t seed) const {
  // Piano style: disable harpsichord-specific ornaments.
  OrnamentConfig config;
  config.enable_turn = false;
  config.enable_appoggiatura = false;
  config.enable_pralltriller = false;
  config.enable_vorschlag = false;
  config.enable_nachschlag = false;
  config.enable_compound = false;

  if (is_trill_etude) {
    // Trill etudes: trill-only, restrained density.
    config.enable_trill = true;
    config.enable_mordent = false;
    config.ornament_density = kTrillEtudeDensity;
  } else {
    // Ornamental variations: sparse trills + mordents.
    config.enable_trill = true;
    config.enable_mordent = true;
    config.ornament_density = kOrnamentalDensity;
  }

  OrnamentContext context;
  context.config = config;
  context.role = VoiceRole::Respond;
  context.seed = seed;

  auto ornamented = bach::applyOrnaments(notes, context);

  // Preserve GoldbergFigura source on non-ornament notes.
  for (auto& note : ornamented) {
    if (note.source != BachNoteSource::Ornament) {
      note.source = BachNoteSource::GoldbergFigura;
    }
  }

  notes = std::move(ornamented);
}

}  // namespace bach
