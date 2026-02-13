/// @file
/// @brief Fughetta and alla breve fugal variation generator.
///
/// Generates 4-voice fugue-like variations for the Goldberg Variations.
/// Var 10 is a standard fughetta with Playful character; Var 22 is a stile
/// antico alla breve fugal variation with Severe character and longer note
/// values.

#include "forms/goldberg/variations/goldberg_fughetta.h"

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

/// Number of voices in fughetta variations.
constexpr int kNumVoices = 4;

/// Exposition entry length in bars.
constexpr int kEntryBars = 2;

/// Total bars in one pass (before repeats).
constexpr int kTotalBars = 32;

/// Register ranges for each voice (MIDI pitch).
/// Voice 0 = soprano, Voice 1 = alto, Voice 2 = tenor, Voice 3 = bass.
struct VoiceRange {
  uint8_t low;
  uint8_t high;
};

constexpr VoiceRange kVoiceRanges[kNumVoices] = {
    {67, 91},  // Soprano: G4 - G6
    {55, 79},  // Alto: G3 - G5
    {48, 72},  // Tenor: C3 - C5
    {36, 60},  // Bass: C2 - C4
};

/// @brief Exposition entry order: soprano, alto, tenor, bass.
/// Each voice enters 2 bars after the previous.
constexpr int kEntryOrder[kNumVoices] = {0, 1, 2, 3};

/// @brief Determine the subject character for a variation number.
/// @param variation_number 10 (Playful) or 22 (Severe).
/// @return SubjectCharacter for the variation.
SubjectCharacter characterForVariation(int variation_number) {
  return (variation_number == 22) ? SubjectCharacter::Severe
                                  : SubjectCharacter::Playful;
}

/// @brief Get the base note duration multiplier for alla breve style.
///
/// Var 22 uses longer note values (half notes predominant instead of quarter).
///
/// @param variation_number 10 or 22.
/// @return Duration multiplier (1 for standard, 2 for alla breve).
int durationMultiplier(int variation_number) {
  return (variation_number == 22) ? 2 : 1;
}

/// @brief Transpose a soggetto entry to fit within a voice's register.
///
/// Applies diatonic transposition and octave adjustment so the entry sits
/// within the target voice's comfortable range.
///
/// @param notes Source soggetto notes.
/// @param voice_idx Target voice index (0-3).
/// @param degree_steps Diatonic degree transposition for answer entries.
/// @param key Key signature.
/// @param scale Scale type.
/// @param start_tick Tick offset for the entry.
/// @return Transposed notes placed at start_tick, assigned to voice_idx.
std::vector<NoteEvent> placeEntry(const std::vector<NoteEvent>& notes,
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

  // Calculate median pitch of transposed entry.
  if (transposed.empty()) return transposed;

  int sum_pitch = 0;
  for (const auto& note : transposed) {
    sum_pitch += static_cast<int>(note.pitch);
  }
  int median_pitch = sum_pitch / static_cast<int>(transposed.size());

  // Target center of the voice range.
  const auto& range = kVoiceRanges[voice_idx];
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
    note.source = BachNoteSource::GoldbergFughetta;
  }

  return transposed;
}

/// @brief Generate free counterpoint fill for a voice over a bar range.
///
/// Creates simple scale-wise motion aligned to the structural grid's harmonic
/// context. Used to fill bars where a voice is not presenting the soggetto.
///
/// @param voice_idx Voice index (0-3).
/// @param start_bar Starting bar (0-based).
/// @param num_bars Number of bars to fill.
/// @param grid Structural grid for harmonic guidance.
/// @param key Key signature.
/// @param time_sig Time signature.
/// @param dur_multiplier Duration multiplier (1=normal, 2=alla breve).
/// @param rng Random number generator.
/// @return Vector of NoteEvents for the free counterpoint fill.
std::vector<NoteEvent> generateFreeCounterpoint(
    int voice_idx,
    int start_bar,
    int num_bars,
    const GoldbergStructuralGrid& grid,
    const KeySignature& key,
    const TimeSignature& time_sig,
    int dur_multiplier,
    std::mt19937& rng) {
  std::vector<NoteEvent> result;
  ScaleType scale = key.is_minor ? ScaleType::HarmonicMinor : ScaleType::Major;
  const auto& range = kVoiceRanges[voice_idx];
  Tick ticks_per_bar = time_sig.ticksPerBar();

  // Base duration for free counterpoint notes.
  Tick base_dur = kTicksPerBeat * static_cast<Tick>(dur_multiplier);

  // Start from the structural bass pitch adjusted to the voice register.
  int bar = start_bar;
  if (bar < 0) bar = 0;
  if (bar >= kTotalBars) return result;

  uint8_t prev_pitch = grid.getStructuralBassPitch(bar);
  // Move to voice register.
  int target_center = (static_cast<int>(range.low) + static_cast<int>(range.high)) / 2;
  int oct_shift = nearestOctaveShift(target_center - static_cast<int>(prev_pitch));
  prev_pitch = clampPitch(static_cast<int>(prev_pitch) + oct_shift, range.low, range.high);
  prev_pitch = scale_util::nearestScaleTone(prev_pitch, key.tonic, scale);

  for (int bar_idx = start_bar; bar_idx < start_bar + num_bars && bar_idx < kTotalBars;
       ++bar_idx) {
    int grid_bar = std::max(0, std::min(31, bar_idx));
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
        // Snap to nearest chord tone (root or fifth of structural bass).
        int root_pc = bass_pc;
        int fifth_pc = (bass_pc + 7) % 12;
        int cur_pc = getPitchClass(prev_pitch);
        // If already on chord tone, keep it; else move toward one.
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
      opts.velocity = 75;
      opts.source = BachNoteSource::GoldbergFughetta;

      auto note_result = createBachNote(nullptr, nullptr, nullptr, opts);
      NoteEvent note = note_result.note;
      note.pitch = new_pitch;
      note.source = BachNoteSource::GoldbergFughetta;
      result.push_back(note);

      prev_pitch = new_pitch;
    }
  }

  return result;
}

/// @brief Generate episodic development using soggetto fragments.
///
/// After the exposition (first 8 bars), fills the remaining bars with
/// soggetto inversions, diatonic sequences, and free counterpoint.
///
/// @param soggetto The original soggetto subject.
/// @param start_bar First bar of development (0-based).
/// @param end_bar Last bar of development (exclusive, 0-based).
/// @param grid Structural grid.
/// @param key Key signature.
/// @param time_sig Time signature.
/// @param dur_multiplier Duration multiplier.
/// @param rng Random number generator.
/// @return Vector of NoteEvents for the development section.
std::vector<NoteEvent> generateDevelopment(
    const Subject& soggetto,
    int start_bar,
    int end_bar,
    const GoldbergStructuralGrid& grid,
    const KeySignature& key,
    const TimeSignature& time_sig,
    int dur_multiplier,
    std::mt19937& rng) {
  std::vector<NoteEvent> result;
  ScaleType scale = key.is_minor ? ScaleType::HarmonicMinor : ScaleType::Major;
  Tick ticks_per_bar = time_sig.ticksPerBar();

  if (soggetto.notes.empty()) return result;

  // Extract Kopfmotiv (head motif) for sequential development.
  auto kopfmotiv = soggetto.extractKopfmotiv(4);

  int current_bar = start_bar;
  int dev_voice = 0;  // Rotate through voices.

  while (current_bar < end_bar) {
    int remaining_bars = end_bar - current_bar;
    Tick current_tick = static_cast<Tick>(current_bar) * ticks_per_bar;

    // Strategy selection based on grid position and randomness.
    std::uniform_int_distribution<int> strategy_dist(0, 2);
    int strategy = strategy_dist(rng);

    if (strategy == 0 && remaining_bars >= 4) {
      // Diatonic sequence of the Kopfmotiv (descending by step, 3 repetitions).
      auto sequence = generateDiatonicSequence(kopfmotiv, 3, -1,
                                                current_tick, key.tonic, scale);
      for (auto& note : sequence) {
        note.voice = static_cast<VoiceId>(dev_voice % kNumVoices);
        const auto& range = kVoiceRanges[note.voice];
        note.pitch = clampPitch(static_cast<int>(note.pitch), range.low, range.high);
        note.source = BachNoteSource::GoldbergFughetta;
      }
      result.insert(result.end(), sequence.begin(), sequence.end());

      // Fill other voices with free counterpoint.
      for (int vox = 0; vox < kNumVoices; ++vox) {
        if (vox == dev_voice % kNumVoices) continue;
        auto fill = generateFreeCounterpoint(
            vox, current_bar, std::min(4, remaining_bars),
            grid, key, time_sig, dur_multiplier, rng);
        result.insert(result.end(), fill.begin(), fill.end());
      }

      current_bar += 4;
      dev_voice++;
    } else if (strategy == 1 && remaining_bars >= 2) {
      // Inverted soggetto entry in a rotating voice.
      uint8_t pivot = soggetto.notes[0].pitch;
      auto inverted = invertMelodyDiatonic(soggetto.notes, pivot, key.tonic, scale);
      int entry_voice = dev_voice % kNumVoices;
      auto placed = placeEntry(inverted, entry_voice, 0, key.tonic, scale, current_tick);
      result.insert(result.end(), placed.begin(), placed.end());

      // Fill other voices with free counterpoint.
      for (int vox = 0; vox < kNumVoices; ++vox) {
        if (vox == entry_voice) continue;
        auto fill = generateFreeCounterpoint(
            vox, current_bar, std::min(kEntryBars, remaining_bars),
            grid, key, time_sig, dur_multiplier, rng);
        result.insert(result.end(), fill.begin(), fill.end());
      }

      current_bar += kEntryBars;
      dev_voice++;
    } else {
      // Free counterpoint fill for all voices.
      int fill_bars = std::min(2, remaining_bars);
      for (int vox = 0; vox < kNumVoices; ++vox) {
        auto fill = generateFreeCounterpoint(
            vox, current_bar, fill_bars,
            grid, key, time_sig, dur_multiplier, rng);
        result.insert(result.end(), fill.begin(), fill.end());
      }
      current_bar += fill_bars;
      dev_voice++;
    }
  }

  return result;
}

/// @brief Apply alla breve duration scaling to soggetto notes.
///
/// For Var 22, doubles all note durations to create the characteristic
/// longer note values of stile antico.
///
/// @param notes Notes to scale (modified in place).
/// @param multiplier Duration multiplier.
void applyDurationScaling(std::vector<NoteEvent>& notes, int multiplier) {
  if (multiplier <= 1) return;
  Tick base_tick = notes.empty() ? 0 : notes[0].start_tick;
  for (auto& note : notes) {
    Tick relative = note.start_tick - base_tick;
    note.start_tick = base_tick + relative * static_cast<Tick>(multiplier);
    note.duration *= static_cast<Tick>(multiplier);
  }
}

}  // namespace

// ---------------------------------------------------------------------------
// Public: generate
// ---------------------------------------------------------------------------

FughettaResult FughettaGenerator::generate(
    int variation_number,
    const GoldbergStructuralGrid& grid,
    const KeySignature& key,
    const TimeSignature& time_sig,
    uint32_t seed) const {
  FughettaResult result;
  result.success = false;

  std::mt19937 rng(seed);
  Tick ticks_per_bar = time_sig.ticksPerBar();
  ScaleType scale = key.is_minor ? ScaleType::HarmonicMinor : ScaleType::Major;
  SubjectCharacter character = characterForVariation(variation_number);
  int dur_mult = durationMultiplier(variation_number);

  // --- Step 1: Generate soggetto (2-bar subject) ---
  SoggettoGenerator soggetto_gen;
  SoggettoParams soggetto_params;
  soggetto_params.length_bars = kEntryBars;
  soggetto_params.character = character;
  soggetto_params.grid = &grid;
  soggetto_params.start_bar = 1;  // 1-based.
  soggetto_params.path_candidates = 8;

  Subject soggetto = soggetto_gen.generate(soggetto_params, key, time_sig, seed);
  if (soggetto.notes.empty()) return result;

  // Apply alla breve duration scaling if needed.
  if (dur_mult > 1) {
    applyDurationScaling(soggetto.notes, dur_mult);
    soggetto.length_ticks *= static_cast<Tick>(dur_mult);
  }

  std::vector<NoteEvent> all_notes;
  all_notes.reserve(512);

  // --- Step 2: Build 4-voice exposition (bars 0-7) ---
  // Voice 0 (soprano): bars 0-1, soggetto on tonic.
  // Voice 1 (alto): bars 2-3, answer transposed to dominant (degree +4).
  // Voice 2 (tenor): bars 4-5, soggetto on tonic (degree 0).
  // Voice 3 (bass): bars 6-7, answer on dominant (degree +4).
  constexpr int kAnswerDegreeStep = 4;  // Diatonic 5th (tonic -> dominant).

  for (int entry_idx = 0; entry_idx < kNumVoices; ++entry_idx) {
    int voice_idx = kEntryOrder[entry_idx];
    int entry_bar = entry_idx * kEntryBars;
    Tick entry_tick = static_cast<Tick>(entry_bar) * ticks_per_bar;

    // Alternate tonic/dominant entries.
    int degree_step = (entry_idx % 2 == 0) ? 0 : kAnswerDegreeStep;

    auto entry = placeEntry(soggetto.notes, voice_idx, degree_step,
                            key.tonic, scale, entry_tick);
    all_notes.insert(all_notes.end(), entry.begin(), entry.end());

    // Fill previously entered voices with free counterpoint during this entry.
    for (int prev = 0; prev < entry_idx; ++prev) {
      int prev_voice = kEntryOrder[prev];
      auto fill = generateFreeCounterpoint(
          prev_voice, entry_bar, kEntryBars, grid, key, time_sig, dur_mult, rng);
      all_notes.insert(all_notes.end(), fill.begin(), fill.end());
    }
  }

  // --- Step 3: Development (bars 8-31) ---
  int dev_start_bar = kNumVoices * kEntryBars;  // Bar 8.
  auto development = generateDevelopment(
      soggetto, dev_start_bar, kTotalBars, grid, key, time_sig, dur_mult, rng);
  all_notes.insert(all_notes.end(), development.begin(), development.end());

  // --- Step 4: Apply binary repeats ---
  Tick section_ticks = 16 * ticks_per_bar;
  all_notes = applyBinaryRepeats(all_notes, section_ticks, false);

  result.notes = std::move(all_notes);
  result.success = !result.notes.empty();
  return result;
}

}  // namespace bach
