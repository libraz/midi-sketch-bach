// Virtuoso variation generator implementation for Goldberg Variations.

#include "forms/goldberg/variations/goldberg_virtuoso.h"

#include <algorithm>
#include <random>

#include "core/note_creator.h"
#include "core/pitch_utils.h"
#include "forms/goldberg/goldberg_binary.h"
#include "forms/goldberg/goldberg_figuren.h"

namespace bach {

namespace {

/// Number of bars in the Goldberg structural grid.
constexpr int kGridBars = 32;

/// Number of bars per section for binary repeats.
constexpr int kBarsPerSection = 16;

/// Bass voice velocity (slightly quieter than melody).
constexpr uint8_t kBassVelocity = 70;

/// Bass register limits (C2-C4).
constexpr uint8_t kBassLow = 36;   // C2
constexpr uint8_t kBassHigh = 60;  // C4

/// Climax velocity boost for Var 29 (absolute ceiling).
constexpr uint8_t kClimaxVelocityMax = 110;

/// Climax velocity minimum for Var 29.
constexpr uint8_t kClimaxVelocityMin = 85;

/// Climax register expansion: maximum upward shift in semitones.
constexpr int kClimaxRegisterExpand = 6;

}  // namespace

// ---------------------------------------------------------------------------
// VirtuosoGenerator::isSupportedVariation
// ---------------------------------------------------------------------------

bool VirtuosoGenerator::isSupportedVariation(int variation_number) {
  return variation_number == 11 || variation_number == 23 || variation_number == 29;
}

// ---------------------------------------------------------------------------
// VirtuosoGenerator::getVoiceCount
// ---------------------------------------------------------------------------

uint8_t VirtuosoGenerator::getVoiceCount(int variation_number) {
  if (variation_number == 29) {
    // BravuraChordal climax: 4 voices for full chordal texture.
    return 4;
  }
  // Toccata and ScalePassage: 2 voices (melody + bass).
  return 2;
}

// ---------------------------------------------------------------------------
// VirtuosoGenerator::buildProfile
// ---------------------------------------------------------------------------

FiguraProfile VirtuosoGenerator::buildProfile(int variation_number,
                                              GoldbergVariationType& type) {
  FiguraProfile profile;

  switch (variation_number) {
    case 11:
      // Var 11: Toccata. Wide chord spread via Arpeggio, alternating direction.
      type = GoldbergVariationType::Toccata;
      profile.primary = FiguraType::Arpeggio;
      profile.secondary = FiguraType::Batterie;
      profile.notes_per_beat = 4;
      profile.direction = DirectionBias::Alternating;
      profile.chord_tone_ratio = 0.7f;
      profile.sequence_probability = 0.3f;
      break;

    case 23:
      // Var 23: ScalePassage. Rapid scale runs via Tirata, ascending bias.
      type = GoldbergVariationType::ScalePassage;
      profile.primary = FiguraType::Tirata;
      profile.secondary = FiguraType::Circulatio;
      profile.notes_per_beat = 4;
      profile.direction = DirectionBias::Ascending;
      profile.chord_tone_ratio = 0.4f;
      profile.sequence_probability = 0.5f;
      break;

    case 29:
      // Var 29: BravuraChordal (BWV 988 CLIMAX).
      // Bariolage register alternation with chordal density.
      type = GoldbergVariationType::BravuraChordal;
      profile.primary = FiguraType::Bariolage;
      profile.secondary = FiguraType::Arpeggio;
      profile.notes_per_beat = 4;
      profile.direction = DirectionBias::Alternating;
      profile.chord_tone_ratio = 0.6f;
      profile.sequence_probability = 0.2f;
      break;

    default:
      // Fallback: Toccata profile.
      type = GoldbergVariationType::Toccata;
      profile.primary = FiguraType::Arpeggio;
      profile.secondary = FiguraType::Batterie;
      profile.notes_per_beat = 4;
      profile.direction = DirectionBias::Alternating;
      profile.chord_tone_ratio = 0.7f;
      profile.sequence_probability = 0.3f;
      break;
  }

  return profile;
}

// ---------------------------------------------------------------------------
// VirtuosoGenerator::generate
// ---------------------------------------------------------------------------

VirtuosoResult VirtuosoGenerator::generate(
    int variation_number,
    const GoldbergStructuralGrid& grid,
    const KeySignature& key,
    const TimeSignature& time_sig,
    uint32_t seed) const {
  VirtuosoResult result;

  if (!isSupportedVariation(variation_number)) {
    return result;  // success=false for unsupported variation numbers.
  }

  GoldbergVariationType var_type{};
  FiguraProfile profile = buildProfile(variation_number, var_type);
  uint8_t voice_count = getVoiceCount(variation_number);

  FigurenGenerator figuren;

  std::vector<NoteEvent> all_notes;

  // Generate melodic voices via FigurenGenerator.
  // For Var 29 (climax), generate additional inner voices for chordal texture.
  uint8_t melody_voices = (variation_number == 29) ? 3 : 1;

  for (uint8_t voice_idx = 0; voice_idx < melody_voices; ++voice_idx) {
    // Derive independent seeds for each voice.
    uint32_t voice_seed = seed + static_cast<uint32_t>(voice_idx) * 1000;

    auto voice_notes = figuren.generate(
        profile, grid, key, time_sig, voice_idx, voice_seed);

    // Set source and voice index.
    for (auto& note : voice_notes) {
      note.source = BachNoteSource::GoldbergFigura;
      note.voice = voice_idx;
    }

    all_notes.insert(all_notes.end(), voice_notes.begin(), voice_notes.end());
  }

  // Generate bass line from structural grid.
  uint8_t bass_voice = voice_count - 1;
  auto bass = generateBassLine(grid, time_sig, bass_voice);
  all_notes.insert(all_notes.end(), bass.begin(), bass.end());

  // Var 29 special: BWV 988 CLIMAX intensification.
  if (variation_number == 29) {
    applyClimaxIntensification(all_notes, grid, time_sig);
  }

  // Sort by start_tick for consistent ordering.
  std::sort(all_notes.begin(), all_notes.end(),
            [](const NoteEvent& lhs, const NoteEvent& rhs) {
              return lhs.start_tick < rhs.start_tick;
            });

  // Apply binary repeats: ||: A(16 bars) :||: B(16 bars) :||
  Tick section_ticks = static_cast<Tick>(kBarsPerSection) * time_sig.ticksPerBar();
  result.notes = applyBinaryRepeats(all_notes, section_ticks, false);
  result.success = !result.notes.empty();

  return result;
}

// ---------------------------------------------------------------------------
// VirtuosoGenerator::generateBassLine
// ---------------------------------------------------------------------------

std::vector<NoteEvent> VirtuosoGenerator::generateBassLine(
    const GoldbergStructuralGrid& grid,
    const TimeSignature& time_sig,
    uint8_t bass_voice) const {
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
      primary_opts.voice = bass_voice;
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
      res_opts.voice = bass_voice;
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
      opts.voice = bass_voice;
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
// VirtuosoGenerator::applyClimaxIntensification
// ---------------------------------------------------------------------------

void VirtuosoGenerator::applyClimaxIntensification(
    std::vector<NoteEvent>& notes,
    const GoldbergStructuralGrid& grid,
    const TimeSignature& time_sig) const {
  Tick ticks_per_bar = time_sig.ticksPerBar();

  for (auto& note : notes) {
    // Determine which bar this note belongs to.
    int bar_idx = static_cast<int>(note.start_tick / ticks_per_bar);
    if (bar_idx >= kGridBars) bar_idx = kGridBars - 1;

    // Scale velocity based on tension profile for dramatic climax shape.
    float tension = grid.getTension(bar_idx).aggregate();

    // Map tension [0.0, 1.0] to velocity [kClimaxVelocityMin, kClimaxVelocityMax].
    float velocity_f = static_cast<float>(kClimaxVelocityMin) +
                       tension * static_cast<float>(kClimaxVelocityMax - kClimaxVelocityMin);
    uint8_t new_velocity = static_cast<uint8_t>(
        std::min(static_cast<float>(kClimaxVelocityMax), velocity_f));
    if (new_velocity > note.velocity) {
      note.velocity = new_velocity;
    }

    // Expand upper register for melodic voices (not bass) during high-tension bars.
    if (note.source != BachNoteSource::GoldbergBass && tension > 0.6f) {
      int expand = static_cast<int>(tension * static_cast<float>(kClimaxRegisterExpand));
      int expanded_pitch = static_cast<int>(note.pitch) + expand;
      note.pitch = clampPitch(expanded_pitch, organ_range::kManual1Low,
                             organ_range::kManual1High);
    }
  }
}

}  // namespace bach
