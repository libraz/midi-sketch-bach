// Goldberg Aria (Sarabande) generator implementation.
// Two-stage: (A) skeleton placement from generated AriaTheme, (B) ornament insertion.

#include "forms/goldberg/goldberg_aria.h"

#include <algorithm>
#include <random>

#include "core/note_creator.h"
#include "core/pitch_utils.h"
#include "forms/goldberg/goldberg_aria_theme.h"
#include "forms/goldberg/goldberg_binary.h"
#include "ornament/ornament_engine.h"

namespace bach {

namespace {

/// Number of bars in the Goldberg structural grid.
constexpr int kGridBars = 32;

/// Melody voice velocity (uniform — Sarabande beat 2 weight comes from
/// pitch/harmonic language, not dynamics).
constexpr uint8_t kMelodyVelocity = 80;

/// Bass voice velocity (slightly quieter than melody).
constexpr uint8_t kBassVelocity = 70;

/// Bass register limits (C2-C4).
constexpr uint8_t kBassLow = 36;   // C2
constexpr uint8_t kBassHigh = 60;  // C4

/// Place skeleton notes for one bar based on AriaTheme and BeatFunction.
///
/// BeatFunction controls articulation:
///   Stable/Passing: new note on the beat (quarter note).
///   Hold: extend previous note (no new onset).
///   Suspension43: beat 1 duration extends into beat 2 head, resolution on beat 2 weak part.
///   Appoggiatura: non-chord onset on beat 2, resolution mid-beat.
void placeSkeletonBar(
    std::vector<NoteEvent>& notes,
    const AriaTheme& theme,
    int bar_idx,
    Tick bar_start,
    Tick beat_duration,
    const KeySignature& key) {
  for (int beat = 0; beat < 3; ++beat) {
    BeatFunction func = theme.getFunction(bar_idx, beat);
    uint8_t pitch = theme.getPitch(bar_idx, beat);

    Tick beat_start = bar_start + static_cast<Tick>(beat) * beat_duration;

    // Hold: extend previous note.
    if (func == BeatFunction::Hold) {
      if (!notes.empty()) {
        auto& prev = notes.back();
        if (prev.start_tick >= bar_start ||
            prev.start_tick + prev.duration >= bar_start) {
          prev.duration += beat_duration;
          continue;
        }
      }
      // Fallback: place as normal note if no previous note to extend.
    }

    if (func == BeatFunction::Suspension43 && beat == 1) {
      // Suspension: extend beat 1 note into first half of beat 2.
      if (!notes.empty()) {
        auto& prev = notes.back();
        Tick half_beat = beat_duration / 2;
        prev.duration += half_beat;

        // Extend beat 1, then place weak-part onset with the suspension pitch.
        BachNoteOptions res_opts{};
        res_opts.voice = 0;
        res_opts.desired_pitch = pitch;
        res_opts.tick = beat_start + half_beat;
        res_opts.duration = beat_duration - half_beat;
        res_opts.velocity = kMelodyVelocity;
        res_opts.source = BachNoteSource::GoldbergAria;
        auto res = createBachNote(nullptr, nullptr, nullptr, res_opts);
        if (res.accepted) notes.push_back(res.note);
        continue;
      }
    }

    if (func == BeatFunction::Appoggiatura && beat == 1) {
      // Appoggiatura: non-chord onset on beat head, resolution mid-beat.
      Tick half_beat = beat_duration / 2;

      BachNoteOptions app_opts{};
      app_opts.voice = 0;
      app_opts.desired_pitch = pitch;
      app_opts.tick = beat_start;
      app_opts.duration = half_beat;
      app_opts.velocity = kMelodyVelocity;
      app_opts.source = BachNoteSource::GoldbergAria;
      auto app_res = createBachNote(nullptr, nullptr, nullptr, app_opts);
      if (app_res.accepted) notes.push_back(app_res.note);

      // Resolution: the beat 3 pitch provides the resolution.
      // Place it as the second half of beat 2.
      uint8_t res_pitch = theme.getPitch(bar_idx, 2);
      BachNoteOptions res_opts{};
      res_opts.voice = 0;
      res_opts.desired_pitch = res_pitch;
      res_opts.tick = beat_start + half_beat;
      res_opts.duration = beat_duration - half_beat;
      res_opts.velocity = kMelodyVelocity;
      res_opts.source = BachNoteSource::GoldbergAria;
      auto res = createBachNote(nullptr, nullptr, nullptr, res_opts);
      if (res.accepted) notes.push_back(res.note);
      continue;
    }

    // Normal placement: Stable, Passing, or unhandled.
    BachNoteOptions opts{};
    opts.voice = 0;
    opts.desired_pitch = pitch;
    opts.tick = beat_start;
    opts.duration = beat_duration;
    opts.velocity = kMelodyVelocity;
    opts.source = BachNoteSource::GoldbergAria;

    auto result = createBachNote(nullptr, nullptr, nullptr, opts);
    if (result.accepted) {
      notes.push_back(result.note);
    }
  }

  (void)key;
}

/// Ornament density per phrase position (bar_in_phrase 1-4).
float getOrnamentDensity(uint8_t bar_in_phrase, bool next_is_cadence) {
  if (next_is_cadence) return 0.25f;  // Peak before cadence.
  switch (bar_in_phrase) {
    case 1: return 0.08f;   // Opening: restrained.
    case 2: return 0.15f;   // Expansion.
    case 3: return 0.20f;   // Intensification.
    case 4: return 0.10f;   // Cadence: don't disturb resolution.
    default: return 0.10f;
  }
}

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

  // Generate seed-dependent Aria melody.
  AriaTheme theme = generateAriaMelody(grid, key, seed);

  Tick ticks_per_bar = time_sig.ticksPerBar();
  Tick beat_duration = static_cast<Tick>(ticks_per_bar / time_sig.beatsPerBar());

  // --- Stage A: Skeleton placement from generated AriaTheme ---
  std::vector<NoteEvent> melody;
  melody.reserve(kGridBars * 3);

  for (int bar_idx = 0; bar_idx < kGridBars; ++bar_idx) {
    Tick bar_start = static_cast<Tick>(bar_idx) * ticks_per_bar;
    placeSkeletonBar(melody, theme, bar_idx, bar_start, beat_duration, key);
  }

  // --- Stage B: Ornament insertion (phrase-dependent density) ---
  applyOrnaments(melody, grid, key, time_sig, rng);

  // Generate bass line from structural grid (unchanged).
  auto bass = generateBassLine(grid, key, time_sig, rng);

  result.melody_notes = std::move(melody);
  result.bass_notes = std::move(bass);
  result.theme = theme;
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

  result.theme = original.theme;
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
  bass_notes.reserve(kGridBars * 2);

  Tick ticks_per_bar = time_sig.ticksPerBar();

  for (int bar_idx = 0; bar_idx < kGridBars; ++bar_idx) {
    const auto& bar_info = grid.getBar(bar_idx);
    uint8_t primary_pitch = bar_info.bass_motion.primary_pitch;

    int target_center = (kBassLow + kBassHigh) / 2;
    int diff = static_cast<int>(primary_pitch) - target_center;
    int shift = nearestOctaveShift(diff);
    uint8_t bass_pitch = clampPitch(
        static_cast<int>(primary_pitch) - shift, kBassLow, kBassHigh);

    Tick bar_start = static_cast<Tick>(bar_idx) * ticks_per_bar;

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
    const GoldbergStructuralGrid& grid,
    const KeySignature& /*key*/,
    const TimeSignature& time_sig,
    std::mt19937& rng) const {
  Tick ticks_per_bar = time_sig.ticksPerBar();

  // Apply ornaments per phrase segment with varying density.
  // Process bar by bar, adjusting ornament config per phrase position.
  std::vector<NoteEvent> result_notes;
  result_notes.reserve(notes.size() * 2);

  // Group notes by bar for per-bar ornament density.
  for (int bar_idx = 0; bar_idx < kGridBars; ++bar_idx) {
    Tick bar_start = static_cast<Tick>(bar_idx) * ticks_per_bar;
    Tick bar_end = bar_start + ticks_per_bar;

    const auto& bar_info = grid.getBar(bar_idx);
    bool next_is_cadence = (bar_idx < 31) &&
                           grid.getBar(bar_idx + 1).cadence.has_value();
    float density = getOrnamentDensity(bar_info.bar_in_phrase, next_is_cadence);

    // Collect notes in this bar.
    std::vector<NoteEvent> bar_notes;
    for (const auto& note : notes) {
      if (note.start_tick >= bar_start && note.start_tick < bar_end) {
        bar_notes.push_back(note);
      }
    }

    if (bar_notes.empty()) continue;

    OrnamentConfig config;
    config.enable_trill = true;
    config.enable_mordent = true;
    config.enable_turn = true;
    config.enable_vorschlag = true;
    config.enable_appoggiatura = false;  // Appoggiatura is in melody skeleton.
    config.enable_pralltriller = false;
    config.enable_nachschlag = false;
    config.enable_compound = false;
    config.ornament_density = density;

    OrnamentContext context;
    context.config = config;
    context.role = VoiceRole::Respond;
    context.seed = rng();

    auto ornamented = bach::applyOrnaments(bar_notes, context);

    for (auto& note : ornamented) {
      if (note.source != BachNoteSource::Ornament) {
        note.source = BachNoteSource::GoldbergAria;
      }
      result_notes.push_back(note);
    }
  }

  notes = std::move(result_notes);
}

}  // namespace bach
