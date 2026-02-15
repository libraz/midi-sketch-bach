/// @file
/// @brief French Overture variation generator (Var 16).
///
/// Var 16 is the midpoint of BWV 988, marking the transition from the first
/// half of the variations to the second. It is structured as a two-section
/// French Overture: a stately Grave with dotted rhythms (alla breve feel)
/// followed by a lively Fugato with staggered voice entries and sequential
/// development.

#include "forms/goldberg/variations/goldberg_overture.h"

#include <algorithm>
#include <random>

#include "core/note_creator.h"
#include "core/pitch_utils.h"
#include "core/scale.h"
#include "forms/goldberg/goldberg_binary.h"
#include "forms/goldberg/goldberg_figuren.h"
#include "forms/goldberg/goldberg_soggetto.h"
#include "fugue/subject.h"
#include "transform/motif_transform.h"
#include "transform/sequence.h"

namespace bach {

namespace {

/// Total bars in the variation (before repeats).
constexpr int kTotalBars = 32;

/// Bars per section (Grave = first 16, Fugato = last 16).
constexpr int kSectionBars = 16;

/// Number of voices in the Grave section (melody + bass).
constexpr int kGraveVoices = 2;

/// Number of voices in the Fugato section (3-voice fugue exposition).
constexpr int kFugatoVoices = 3;

/// Soggetto length for the Fugato in bars.
constexpr int kSoggettoBars = 1;

/// Bass register limits (C2-C4), consistent with AriaGenerator.
constexpr uint8_t kBassLow = 36;   // C2
constexpr uint8_t kBassHigh = 60;  // C4

/// Bass velocity (slightly quieter than melody).
constexpr uint8_t kBassVelocity = 70;

/// Grave melody velocity (stately, forte).
constexpr uint8_t kGraveVelocity = 85;

/// Fugato velocity (lively, moderately strong).
constexpr uint8_t kFugatoVelocity = 78;

/// Register ranges for Fugato voices (MIDI pitch).
/// Voice 0 = soprano, Voice 1 = alto, Voice 2 = tenor.
struct VoiceRange {
  uint8_t low;
  uint8_t high;
};

constexpr VoiceRange kFugatoRanges[kFugatoVoices] = {
    {67, 91},  // Soprano: G4 - G6
    {55, 79},  // Alto: G3 - G5
    {43, 67},  // Tenor: G2 - G4
};

/// @brief Transpose and place a soggetto entry into a target voice's register.
///
/// Applies diatonic transposition and octave adjustment so the entry sits
/// within the target voice's comfortable range.
///
/// @param notes Source soggetto notes.
/// @param voice_idx Target voice index (0-2).
/// @param degree_steps Diatonic degree transposition for answer entries.
/// @param key Key signature.
/// @param scale Scale type.
/// @param start_tick Tick offset for the entry.
/// @return Transposed notes placed at start_tick, assigned to voice_idx.
std::vector<NoteEvent> placeFugatoEntry(const std::vector<NoteEvent>& notes,
                                        int voice_idx,
                                        int degree_steps,
                                        Key key,
                                        ScaleType scale,
                                        Tick start_tick) {
  // First transpose diatonically.
  std::vector<NoteEvent> transposed =
      (degree_steps != 0)
          ? transposeMelodyDiatonic(notes, degree_steps, key, scale)
          : notes;

  if (transposed.empty()) return transposed;

  // Calculate median pitch of transposed entry.
  int sum_pitch = 0;
  for (const auto& note : transposed) {
    sum_pitch += static_cast<int>(note.pitch);
  }
  int median_pitch = sum_pitch / static_cast<int>(transposed.size());

  // Target center of the voice range.
  int clamped_idx = std::max(0, std::min(kFugatoVoices - 1, voice_idx));
  const auto& range = kFugatoRanges[clamped_idx];
  int target_center = (static_cast<int>(range.low) + static_cast<int>(range.high)) / 2;

  // Find the nearest octave shift to bring median_pitch close to target_center.
  int shift = nearestOctaveShift(target_center - median_pitch);

  // Apply offset and clamp.
  Tick base_tick = transposed[0].start_tick;
  for (auto& note : transposed) {
    int new_pitch = static_cast<int>(note.pitch) + shift;
    note.pitch = clampPitch(new_pitch, range.low, range.high);
    note.start_tick = (note.start_tick - base_tick) + start_tick;
    note.voice = static_cast<VoiceId>(voice_idx);
    note.velocity = kFugatoVelocity;
    note.source = BachNoteSource::GoldbergOverture;
  }

  return transposed;
}

/// @brief Generate free counterpoint fill for a Fugato voice over a bar range.
///
/// Creates simple scale-wise motion aligned to the structural grid's harmonic
/// context. Used to fill bars where a voice is not presenting the soggetto.
///
/// @param voice_idx Voice index (0-2).
/// @param start_bar Starting bar (0-based, absolute).
/// @param num_bars Number of bars to fill.
/// @param grid Structural grid for harmonic guidance.
/// @param key Key signature.
/// @param time_sig Time signature.
/// @param rng Random number generator.
/// @return Vector of NoteEvents for the free counterpoint fill.
std::vector<NoteEvent> generateFugatoCounterpoint(
    int voice_idx,
    int start_bar,
    int num_bars,
    const GoldbergStructuralGrid& grid,
    const KeySignature& key,
    const TimeSignature& time_sig,
    std::mt19937& rng) {
  std::vector<NoteEvent> result;
  ScaleType scale = key.is_minor ? ScaleType::HarmonicMinor : ScaleType::Major;

  int clamped_idx = std::max(0, std::min(kFugatoVoices - 1, voice_idx));
  const auto& range = kFugatoRanges[clamped_idx];
  Tick ticks_per_bar = time_sig.ticksPerBar();

  // Fugato uses shorter note values (3/8 feel): eighth notes.
  Tick base_dur = kTicksPerBeat / 2;  // Eighth note.

  int bar = std::max(0, start_bar);
  if (bar >= kTotalBars) return result;

  // Start from the structural bass pitch adjusted to the voice register.
  int grid_bar = std::min(31, bar);
  uint8_t prev_pitch = grid.getStructuralBassPitch(grid_bar);
  int target_center = (static_cast<int>(range.low) + static_cast<int>(range.high)) / 2;
  int oct_shift = nearestOctaveShift(target_center - static_cast<int>(prev_pitch));
  prev_pitch = clampPitch(static_cast<int>(prev_pitch) + oct_shift, range.low, range.high);
  prev_pitch = scale_util::nearestScaleTone(prev_pitch, key.tonic, scale);

  for (int bar_idx = start_bar; bar_idx < start_bar + num_bars && bar_idx < kTotalBars;
       ++bar_idx) {
    grid_bar = std::max(0, std::min(31, bar_idx));
    Tick bar_start = static_cast<Tick>(bar_idx) * ticks_per_bar;
    int notes_in_bar = static_cast<int>(ticks_per_bar / base_dur);
    if (notes_in_bar < 1) notes_in_bar = 1;

    // Get structural bass pitch for harmonic alignment.
    uint8_t bass_pitch = grid.getStructuralBassPitch(grid_bar);
    int bass_pc = getPitchClass(bass_pitch);

    for (int note_idx = 0; note_idx < notes_in_bar; ++note_idx) {
      // Choose next pitch: stepwise motion with occasional small leap.
      std::uniform_int_distribution<int> step_dist(-2, 2);
      int step = step_dist(rng);

      // On strong beats, prefer chord tones.
      if (note_idx == 0) {
        int root_pc = bass_pc;
        int fifth_pc = (bass_pc + 7) % 12;
        int cur_pc = getPitchClass(prev_pitch);
        if (cur_pc != root_pc && cur_pc != fifth_pc) {
          step = (root_pc > cur_pc) ? 1 : -1;
        }
      }

      int abs_deg = scale_util::pitchToAbsoluteDegree(prev_pitch, key.tonic, scale);
      int new_abs_deg = abs_deg + step;
      uint8_t new_pitch = scale_util::absoluteDegreeToPitch(new_abs_deg, key.tonic, scale);
      new_pitch = clampPitch(static_cast<int>(new_pitch), range.low, range.high);

      BachNoteOptions opts;
      opts.voice = static_cast<VoiceId>(voice_idx);
      opts.desired_pitch = new_pitch;
      opts.tick = bar_start + static_cast<Tick>(note_idx) * base_dur;
      opts.duration = base_dur;
      opts.velocity = kFugatoVelocity;
      opts.source = BachNoteSource::GoldbergOverture;

      auto note_result = createBachNote(nullptr, nullptr, nullptr, opts);
      if (!note_result.accepted) continue;
      NoteEvent note = note_result.note;
      note.pitch = new_pitch;
      note.source = BachNoteSource::GoldbergOverture;
      result.push_back(note);

      prev_pitch = new_pitch;
    }
  }

  return result;
}

}  // namespace

// ---------------------------------------------------------------------------
// OvertureGenerator::generate
// ---------------------------------------------------------------------------

OvertureResult OvertureGenerator::generate(
    const GoldbergStructuralGrid& grid,
    const KeySignature& key,
    const TimeSignature& time_sig,
    uint32_t seed) const {
  OvertureResult result;
  result.success = false;

  std::mt19937 rng(seed);
  Tick ticks_per_bar = time_sig.ticksPerBar();

  // --- Generate Grave section (bars 0-15) ---
  auto grave_notes = generateGrave(grid, key, time_sig, rng);

  // --- Generate Fugato section (bars 16-31) ---
  Tick fugato_start_tick = static_cast<Tick>(kSectionBars) * ticks_per_bar;
  auto fugato_notes = generateFugato(grid, key, time_sig, fugato_start_tick, rng);

  // --- Concatenate both sections ---
  std::vector<NoteEvent> all_notes;
  all_notes.reserve(grave_notes.size() + fugato_notes.size());
  all_notes.insert(all_notes.end(), grave_notes.begin(), grave_notes.end());
  all_notes.insert(all_notes.end(), fugato_notes.begin(), fugato_notes.end());

  // Ensure all notes have correct source.
  for (auto& note : all_notes) {
    note.source = BachNoteSource::GoldbergOverture;
  }

  // Sort by start_tick for clean output.
  std::sort(all_notes.begin(), all_notes.end(),
            [](const NoteEvent& lhs, const NoteEvent& rhs) {
              return lhs.start_tick < rhs.start_tick;
            });

  // --- Apply binary repeats ---
  Tick section_ticks = static_cast<Tick>(kSectionBars) * ticks_per_bar;
  all_notes = applyBinaryRepeats(all_notes, section_ticks, false);

  result.notes = std::move(all_notes);
  result.success = !result.notes.empty();
  return result;
}

// ---------------------------------------------------------------------------
// OvertureGenerator::generateGrave
// ---------------------------------------------------------------------------

std::vector<NoteEvent> OvertureGenerator::generateGrave(
    const GoldbergStructuralGrid& grid,
    const KeySignature& key,
    const TimeSignature& time_sig,
    std::mt19937& rng) const {
  std::vector<NoteEvent> grave_notes;

  // FiguraProfile for DottedGrave: stately dotted rhythms, French style.
  FiguraProfile grave_profile;
  grave_profile.primary = FiguraType::DottedGrave;
  grave_profile.secondary = FiguraType::DottedGrave;
  grave_profile.notes_per_beat = 1;  // Alla breve feel: longer note values.
  grave_profile.direction = DirectionBias::Symmetric;
  grave_profile.chord_tone_ratio = 0.7f;
  grave_profile.sequence_probability = 0.2f;

  // Generate melody via FigurenGenerator (voice 0 = upper register).
  FigurenGenerator figuren;
  auto melody = figuren.generate(grave_profile, grid, key, time_sig, 0, rng());

  // Filter to first 16 bars only.
  Tick ticks_per_bar = time_sig.ticksPerBar();
  Tick section_end = static_cast<Tick>(kSectionBars) * ticks_per_bar;

  std::vector<NoteEvent> filtered_melody;
  filtered_melody.reserve(melody.size() / 2);
  for (auto& note : melody) {
    if (note.start_tick < section_end) {
      note.source = BachNoteSource::GoldbergOverture;
      note.voice = 0;
      note.velocity = kGraveVelocity;
      filtered_melody.push_back(note);
    }
  }

  // Generate bass line for bars 0-15.
  auto bass = generateBassLine(grid, key, time_sig, 0, kSectionBars);

  grave_notes.reserve(filtered_melody.size() + bass.size());
  grave_notes.insert(grave_notes.end(), filtered_melody.begin(), filtered_melody.end());
  grave_notes.insert(grave_notes.end(), bass.begin(), bass.end());

  return grave_notes;
}

// ---------------------------------------------------------------------------
// OvertureGenerator::generateFugato
// ---------------------------------------------------------------------------

std::vector<NoteEvent> OvertureGenerator::generateFugato(
    const GoldbergStructuralGrid& grid,
    const KeySignature& key,
    const TimeSignature& time_sig,
    Tick start_tick,
    std::mt19937& rng) const {
  std::vector<NoteEvent> fugato_notes;
  fugato_notes.reserve(256);

  ScaleType scale = key.is_minor ? ScaleType::HarmonicMinor : ScaleType::Major;
  Tick ticks_per_bar = time_sig.ticksPerBar();

  // --- Step 1: Generate soggetto (1-bar subject) with Playful character ---
  SoggettoGenerator soggetto_gen;
  SoggettoParams soggetto_params;
  soggetto_params.length_bars = kSoggettoBars;
  soggetto_params.character = SubjectCharacter::Playful;
  soggetto_params.grid = &grid;
  soggetto_params.start_bar = kSectionBars + 1;  // 1-based, start at bar 17.
  soggetto_params.path_candidates = 8;

  Subject soggetto = soggetto_gen.generate(soggetto_params, key, time_sig, rng());
  if (soggetto.notes.empty()) return fugato_notes;

  // --- Step 2: Build 3-voice fugue exposition (bars 16-19) ---
  // Voice 0 (soprano): bar 16, soggetto on tonic.
  // Voice 1 (alto): bar 17, answer at dominant (degree +4).
  // Voice 2 (tenor): bar 18, soggetto on tonic.
  constexpr int kAnswerDegreeStep = 4;  // Diatonic 5th (tonic -> dominant).

  int fugato_start_bar = kSectionBars;  // Bar 16 (0-indexed).

  for (int entry_idx = 0; entry_idx < kFugatoVoices; ++entry_idx) {
    int entry_bar = fugato_start_bar + entry_idx * kSoggettoBars;
    Tick entry_tick = static_cast<Tick>(entry_bar) * ticks_per_bar;

    // Alternate tonic/dominant entries.
    int degree_step = (entry_idx % 2 == 0) ? 0 : kAnswerDegreeStep;

    auto entry = placeFugatoEntry(soggetto.notes, entry_idx, degree_step,
                                  key.tonic, scale, entry_tick);
    fugato_notes.insert(fugato_notes.end(), entry.begin(), entry.end());

    // Fill previously entered voices with free counterpoint during this entry.
    for (int prev = 0; prev < entry_idx; ++prev) {
      auto fill = generateFugatoCounterpoint(
          prev, entry_bar, kSoggettoBars, grid, key, time_sig, rng);
      fugato_notes.insert(fugato_notes.end(), fill.begin(), fill.end());
    }
  }

  // --- Step 3: Sequential development (bars 19-27) ---
  int dev_start_bar = fugato_start_bar + kFugatoVoices * kSoggettoBars;
  int dev_end_bar = kTotalBars - 3;  // Leave 3 bars for final section.

  if (!soggetto.notes.empty() && dev_start_bar < dev_end_bar) {
    auto kopfmotiv = soggetto.extractKopfmotiv(4);

    int current_bar = dev_start_bar;
    int dev_voice = 0;

    while (current_bar < dev_end_bar) {
      int remaining_bars = dev_end_bar - current_bar;
      Tick current_tick = static_cast<Tick>(current_bar) * ticks_per_bar;

      std::uniform_int_distribution<int> strategy_dist(0, 2);
      int strategy = strategy_dist(rng);

      if (strategy == 0 && remaining_bars >= 3) {
        // Diatonic sequence of the Kopfmotiv (descending by step, 2 repetitions).
        auto sequence = generateDiatonicSequence(kopfmotiv, 2, -1,
                                                  current_tick, key.tonic, scale);
        for (auto& note : sequence) {
          note.voice = static_cast<VoiceId>(dev_voice % kFugatoVoices);
          const auto& range = kFugatoRanges[note.voice];
          note.pitch = clampPitch(static_cast<int>(note.pitch), range.low, range.high);
          note.source = BachNoteSource::GoldbergOverture;
        }
        fugato_notes.insert(fugato_notes.end(), sequence.begin(), sequence.end());

        // Fill other voices.
        for (int vox = 0; vox < kFugatoVoices; ++vox) {
          if (vox == dev_voice % kFugatoVoices) continue;
          auto fill = generateFugatoCounterpoint(
              vox, current_bar, std::min(3, remaining_bars),
              grid, key, time_sig, rng);
          fugato_notes.insert(fugato_notes.end(), fill.begin(), fill.end());
        }

        current_bar += 3;
        dev_voice++;
      } else if (strategy == 1 && remaining_bars >= kSoggettoBars) {
        // Inverted soggetto entry in a rotating voice.
        uint8_t pivot = soggetto.notes[0].pitch;
        auto inverted = invertMelodyDiatonic(soggetto.notes, pivot, key.tonic, scale);
        int entry_voice = dev_voice % kFugatoVoices;
        auto placed = placeFugatoEntry(inverted, entry_voice, 0,
                                       key.tonic, scale, current_tick);
        fugato_notes.insert(fugato_notes.end(), placed.begin(), placed.end());

        // Fill other voices.
        for (int vox = 0; vox < kFugatoVoices; ++vox) {
          if (vox == entry_voice) continue;
          auto fill = generateFugatoCounterpoint(
              vox, current_bar, kSoggettoBars,
              grid, key, time_sig, rng);
          fugato_notes.insert(fugato_notes.end(), fill.begin(), fill.end());
        }

        current_bar += kSoggettoBars;
        dev_voice++;
      } else {
        // Free counterpoint fill for all voices.
        int fill_bars = std::min(2, remaining_bars);
        for (int vox = 0; vox < kFugatoVoices; ++vox) {
          auto fill = generateFugatoCounterpoint(
              vox, current_bar, fill_bars,
              grid, key, time_sig, rng);
          fugato_notes.insert(fugato_notes.end(), fill.begin(), fill.end());
        }
        current_bar += fill_bars;
        dev_voice++;
      }
    }
  }

  // --- Step 4: Final bars (bars 29-31): free counterpoint cadential approach ---
  int final_start_bar = std::max(dev_end_bar, kTotalBars - 3);
  if (final_start_bar < kTotalBars) {
    int final_bars = kTotalBars - final_start_bar;
    for (int vox = 0; vox < kFugatoVoices; ++vox) {
      auto fill = generateFugatoCounterpoint(
          vox, final_start_bar, final_bars,
          grid, key, time_sig, rng);
      fugato_notes.insert(fugato_notes.end(), fill.begin(), fill.end());
    }
  }

  return fugato_notes;
}

// ---------------------------------------------------------------------------
// OvertureGenerator::generateBassLine
// ---------------------------------------------------------------------------

std::vector<NoteEvent> OvertureGenerator::generateBassLine(
    const GoldbergStructuralGrid& grid,
    const KeySignature& /*key*/,
    const TimeSignature& time_sig,
    int start_bar,
    int num_bars) const {
  std::vector<NoteEvent> bass_notes;
  bass_notes.reserve(static_cast<size_t>(num_bars) * 2);

  Tick ticks_per_bar = time_sig.ticksPerBar();
  int target_center = (kBassLow + kBassHigh) / 2;  // ~C3 (48)

  for (int bar_idx = start_bar; bar_idx < start_bar + num_bars && bar_idx < kTotalBars;
       ++bar_idx) {
    int grid_bar = std::max(0, std::min(31, bar_idx));
    const auto& bar_info = grid.getBar(grid_bar);
    uint8_t primary_pitch = bar_info.bass_motion.primary_pitch;

    // Place primary pitch in bass register (C2-C4) using nearestOctaveShift.
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
      primary_opts.source = BachNoteSource::GoldbergOverture;

      auto primary_result = createBachNote(nullptr, nullptr, nullptr, primary_opts);
      if (primary_result.accepted) {
        bass_notes.push_back(primary_result.note);
      }

      // Resolution note.
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
      res_opts.source = BachNoteSource::GoldbergOverture;

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
      opts.source = BachNoteSource::GoldbergOverture;

      auto note_result = createBachNote(nullptr, nullptr, nullptr, opts);
      if (note_result.accepted) {
        bass_notes.push_back(note_result.note);
      }
    }
  }

  return bass_notes;
}

}  // namespace bach
