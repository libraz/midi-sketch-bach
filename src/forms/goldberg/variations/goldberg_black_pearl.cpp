// BlackPearl (Var 25) generator implementation: G minor Adagio with suspension chains.

#include "forms/goldberg/variations/goldberg_black_pearl.h"

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

/// Number of bars per section (for binary repeats).
constexpr int kSectionBars = 16;

/// Melody voice index.
constexpr VoiceId kMelodyVoice = 0;

/// Bass voice index.
constexpr VoiceId kBassVoice = 1;

/// Suspension voice index (upper register, overlapping with melody).
constexpr VoiceId kSuspensionVoice = 0;

/// Melody velocity (soft, Adagio character).
constexpr uint8_t kMelodyVelocity = 65;

/// Bass velocity (slightly quieter than melody).
constexpr uint8_t kBassVelocity = 60;

/// Suspension velocity (expressive, slightly louder for dissonance emphasis).
constexpr uint8_t kSuspensionVelocity = 72;

/// Bass register limits for BlackPearl (C2-C4).
constexpr uint8_t kBassLow = 36;   // C2
constexpr uint8_t kBassHigh = 60;  // C4

/// Melody register limits (C4-C6).
constexpr uint8_t kMelodyLow = 60;   // C4
constexpr uint8_t kMelodyHigh = 84;  // C6

/// Lamento bass chromatic descent from G3 (design value).
/// G-F#-F-E-Eb-D: the chromatic descending tetrachord spanning G to D.
constexpr uint8_t kLamentoPitches[] = {
    55,  // G3
    54,  // F#3
    53,  // F3
    52,  // E3
    51,  // Eb3
    50   // D3
};
constexpr int kLamentoPitchCount = 6;

/// Number of bars spanned by one lamento bass statement.
constexpr int kLamentoSpanBars = 4;

/// Phrase groups where lamento bass appears (0-indexed phrase groups).
/// Phrase groups 0, 2, 4, 6 = bars 0-3, 8-11, 16-19, 24-27.
constexpr int kLamentoPhraseGroups[] = {0, 2, 4, 6};
constexpr int kLamentoPhraseGroupCount = 4;

/// Suspension chain types: interval from bass (7-6, 4-3, 9-8).
/// The suspension interval determines which chord tone is suspended.
enum class ChainType : uint8_t {
  Chain_7_6 = 0,  // 7th resolves to 6th (most common).
  Chain_4_3,       // 4th resolves to 3rd.
  Chain_9_8        // 9th resolves to octave.
};

/// @brief Get the suspension interval for a chain type.
/// @param type The chain type.
/// @return Interval in semitones above the resolution pitch.
int getSuspensionInterval(ChainType type) {
  switch (type) {
    case ChainType::Chain_7_6: return 2;  // Major 2nd above resolution.
    case ChainType::Chain_4_3: return 1;  // Minor 2nd above resolution.
    case ChainType::Chain_9_8: return 2;  // Major 2nd above resolution (octave context).
    default: return 2;
  }
}

/// @brief Select a chain type randomly with 7-6 as the most common.
/// @param rng Random number generator.
/// @return Selected ChainType.
ChainType selectChainType(std::mt19937& rng) {
  std::uniform_int_distribution<int> dist(0, 9);
  int val = dist(rng);
  if (val < 5) return ChainType::Chain_7_6;   // 50%
  if (val < 8) return ChainType::Chain_4_3;   // 30%
  return ChainType::Chain_9_8;                 // 20%
}

/// @brief Get the scale step below a pitch in the given key.
/// @param pitch MIDI pitch.
/// @param key Key signature.
/// @return MIDI pitch one diatonic step below.
uint8_t scaleStepBelow(uint8_t pitch, const KeySignature& key) {
  // Get scale tones in range and find the closest one below.
  uint8_t low = (pitch >= 14) ? static_cast<uint8_t>(pitch - 14) : 0;
  auto tones = getScaleTones(key.tonic, key.is_minor, low, pitch);

  if (tones.size() < 2) {
    // Fallback: descend by a whole step.
    return (pitch >= 2) ? static_cast<uint8_t>(pitch - 2) : pitch;
  }

  // Find pitch in the scale; return the tone just below it.
  for (int idx = static_cast<int>(tones.size()) - 1; idx >= 0; --idx) {
    if (tones[static_cast<size_t>(idx)] < pitch) {
      return tones[static_cast<size_t>(idx)];
    }
  }

  // Pitch is at the bottom of range.
  return (pitch >= 2) ? static_cast<uint8_t>(pitch - 2) : pitch;
}

/// @brief Build the Suspirans FiguraProfile for BlackPearl melody.
/// Slow, sighing figures with descending bias.
FiguraProfile buildSuspiransProfile() {
  return {
      FiguraType::Suspirans,   // primary
      FiguraType::Suspirans,   // secondary (consistent sighing character)
      1,                       // notes_per_beat (slow Adagio density)
      DirectionBias::Descending,
      0.5f,                    // chord_tone_ratio (moderate, allow passing tones)
      0.1f                     // sequence_probability (low, organic)
  };
}

}  // namespace

// ---------------------------------------------------------------------------
// BlackPearlGenerator::generate
// ---------------------------------------------------------------------------

BlackPearlResult BlackPearlGenerator::generate(const GoldbergStructuralGrid& grid,
                                                const KeySignature& key,
                                                const TimeSignature& time_sig,
                                                uint32_t seed) const {
  BlackPearlResult result;
  std::mt19937 rng(seed);

  Tick ticks_per_bar = time_sig.ticksPerBar();
  Tick beat_duration = static_cast<Tick>(ticks_per_bar / time_sig.beatsPerBar());

  // -----------------------------------------------------------------------
  // Step 1: Generate melody using FigurenGenerator with Suspirans profile.
  // -----------------------------------------------------------------------
  FigurenGenerator figuren;
  FiguraProfile suspirans_profile = buildSuspiransProfile();

  auto melody = figuren.generate(suspirans_profile, grid, key, time_sig,
                                  kMelodyVoice, seed);

  // Set source and velocity for Adagio character.
  for (auto& note : melody) {
    note.source = BachNoteSource::GoldbergFigura;
    note.voice = kMelodyVoice;
    note.velocity = kMelodyVelocity;
  }

  // -----------------------------------------------------------------------
  // Step 2: Determine lamento bass bars and generate bass lines.
  // -----------------------------------------------------------------------
  std::vector<bool> lamento_bars(kGridBars, false);

  std::vector<NoteEvent> bass_notes;
  bass_notes.reserve(kGridBars * 2);

  // Place lamento bass at designated phrase groups.
  for (int grp_idx = 0; grp_idx < kLamentoPhraseGroupCount; ++grp_idx) {
    int phrase_group = kLamentoPhraseGroups[grp_idx];
    int start_bar = phrase_group * 4;

    // Mark bars as covered by lamento.
    for (int bar_offset = 0; bar_offset < kLamentoSpanBars && (start_bar + bar_offset) < kGridBars;
         ++bar_offset) {
      lamento_bars[static_cast<size_t>(start_bar + bar_offset)] = true;
    }

    Tick lamento_start = static_cast<Tick>(start_bar) * ticks_per_bar;
    auto lamento = generateLamentoBass(lamento_start, kLamentoSpanBars, ticks_per_bar, key);
    bass_notes.insert(bass_notes.end(), lamento.begin(), lamento.end());
  }

  // Generate structural bass for non-lamento bars.
  auto structural_bass = generateStructuralBass(grid, time_sig, lamento_bars);
  bass_notes.insert(bass_notes.end(), structural_bass.begin(), structural_bass.end());

  // -----------------------------------------------------------------------
  // Step 3: Insert suspension chains at phrase boundaries.
  // -----------------------------------------------------------------------
  int suspension_count = 0;
  std::vector<NoteEvent> suspension_notes;

  for (int bar_idx = 0; bar_idx < kGridBars; ++bar_idx) {
    PhrasePosition pos = grid.getPhrasePosition(bar_idx);

    // Insert suspensions at Intensification and Cadence bars.
    if (pos != PhrasePosition::Intensification && pos != PhrasePosition::Cadence) {
      continue;
    }

    // Check probability for chain extension.
    std::uniform_real_distribution<float> prob_dist(0.0f, 1.0f);
    if (prob_dist(rng) > profile_.chain_probability + 0.3f) {
      continue;  // Skip this position (but with high baseline probability).
    }

    // Determine chain length.
    std::uniform_int_distribution<int> len_dist(1, profile_.max_chain_length);
    int chain_length = len_dist(rng);

    // Start pitch: pick a melody-register pitch near the structural bass + octave.
    uint8_t bass_pitch = grid.getStructuralBassPitch(bar_idx);
    // Suspension in melody register: bass pitch + 12 (octave above, in melody range).
    uint8_t start_pitch = clampPitch(
        static_cast<int>(bass_pitch) + 12 + 7, kMelodyLow, kMelodyHigh);

    Tick chain_start = static_cast<Tick>(bar_idx) * ticks_per_bar;

    auto chain = generateSuspensionChain(start_pitch, chain_length,
                                          chain_start, beat_duration, key, rng);
    suspension_count += chain_length;
    suspension_notes.insert(suspension_notes.end(), chain.begin(), chain.end());
  }

  result.suspension_count = suspension_count;

  // -----------------------------------------------------------------------
  // Step 4: Merge all notes and apply binary repeats.
  // -----------------------------------------------------------------------
  std::vector<NoteEvent> all_notes;
  all_notes.reserve(melody.size() + bass_notes.size() + suspension_notes.size());
  all_notes.insert(all_notes.end(), melody.begin(), melody.end());
  all_notes.insert(all_notes.end(), bass_notes.begin(), bass_notes.end());
  all_notes.insert(all_notes.end(), suspension_notes.begin(), suspension_notes.end());

  // Sort by start_tick for consistent ordering.
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
// BlackPearlGenerator::generateSuspensionChain
// ---------------------------------------------------------------------------

std::vector<NoteEvent> BlackPearlGenerator::generateSuspensionChain(
    uint8_t start_pitch,
    int chain_length,
    Tick start_tick,
    Tick beat_duration,
    const KeySignature& key,
    std::mt19937& rng) const {
  std::vector<NoteEvent> notes;
  notes.reserve(static_cast<size_t>(chain_length) * 3);

  uint8_t current_pitch = start_pitch;
  Tick current_tick = start_tick;

  for (int chain_idx = 0; chain_idx < chain_length; ++chain_idx) {
    ChainType chain_type = selectChainType(rng);

    // Phase 1: Preparation (consonant, on weak beat).
    // Duration: one beat.
    {
      BachNoteOptions opts{};
      opts.voice = kSuspensionVoice;
      opts.desired_pitch = current_pitch;
      opts.tick = current_tick;
      opts.duration = beat_duration;
      opts.velocity = kSuspensionVelocity - 8;  // Slightly softer preparation.
      opts.source = BachNoteSource::GoldbergSuspension;

      auto note_result = createBachNote(nullptr, nullptr, nullptr, opts);
      if (note_result.accepted) {
        notes.push_back(note_result.note);
      }
    }
    current_tick += beat_duration;

    // Phase 2: Suspension (dissonant, on strong beat).
    // Same pitch held over -- the dissonance arises from the harmonic context.
    // Duration: one beat (held over the barline or strong beat).
    {
      BachNoteOptions opts{};
      opts.voice = kSuspensionVoice;
      opts.desired_pitch = current_pitch;
      opts.tick = current_tick;
      opts.duration = beat_duration;
      opts.velocity = kSuspensionVelocity;  // Full volume for dissonance emphasis.
      opts.source = BachNoteSource::GoldbergSuspension;

      auto note_result = createBachNote(nullptr, nullptr, nullptr, opts);
      if (note_result.accepted) {
        notes.push_back(note_result.note);
      }
    }
    current_tick += beat_duration;

    // Phase 3: Resolution (descending step).
    uint8_t resolution_pitch = scaleStepBelow(current_pitch, key);
    int susp_interval = getSuspensionInterval(chain_type);
    // Verify the interval relationship; adjust if needed.
    if (absoluteInterval(current_pitch, resolution_pitch) > 3) {
      // Fallback: resolve by minor 2nd down.
      resolution_pitch = static_cast<uint8_t>(
          std::max(0, static_cast<int>(current_pitch) - susp_interval));
    }

    {
      BachNoteOptions opts{};
      opts.voice = kSuspensionVoice;
      opts.desired_pitch = resolution_pitch;
      opts.tick = current_tick;
      opts.duration = beat_duration;
      opts.velocity = kSuspensionVelocity - 4;  // Slightly softer resolution.
      opts.source = BachNoteSource::GoldbergSuspension;

      auto note_result = createBachNote(nullptr, nullptr, nullptr, opts);
      if (note_result.accepted) {
        notes.push_back(note_result.note);
      }
    }
    current_tick += beat_duration;

    // For chain continuation: the resolution becomes the next preparation.
    current_pitch = resolution_pitch;
  }

  return notes;
}

// ---------------------------------------------------------------------------
// BlackPearlGenerator::generateLamentoBass
// ---------------------------------------------------------------------------

std::vector<NoteEvent> BlackPearlGenerator::generateLamentoBass(
    Tick start_tick,
    int span_bars,
    Tick bar_duration,
    const KeySignature& /*key*/) const {
  std::vector<NoteEvent> notes;
  notes.reserve(kLamentoPitchCount);

  // Distribute lamento pitches evenly across the span.
  Tick total_duration = static_cast<Tick>(span_bars) * bar_duration;
  Tick note_duration = total_duration / static_cast<Tick>(kLamentoPitchCount);

  // Ensure minimum duration of one beat.
  if (note_duration < kTicksPerBeat) {
    note_duration = kTicksPerBeat;
  }

  for (int pitch_idx = 0; pitch_idx < kLamentoPitchCount; ++pitch_idx) {
    Tick note_start = start_tick + static_cast<Tick>(pitch_idx) * note_duration;

    BachNoteOptions opts{};
    opts.voice = kBassVoice;
    opts.desired_pitch = kLamentoPitches[pitch_idx];
    opts.tick = note_start;
    opts.duration = note_duration;
    opts.velocity = kBassVelocity;
    opts.source = BachNoteSource::GoldbergBass;

    auto note_result = createBachNote(nullptr, nullptr, nullptr, opts);
    if (note_result.accepted) {
      notes.push_back(note_result.note);
    }
  }

  return notes;
}

// ---------------------------------------------------------------------------
// BlackPearlGenerator::generateStructuralBass
// ---------------------------------------------------------------------------

std::vector<NoteEvent> BlackPearlGenerator::generateStructuralBass(
    const GoldbergStructuralGrid& grid,
    const TimeSignature& time_sig,
    const std::vector<bool>& lamento_bars) const {
  std::vector<NoteEvent> bass_notes;
  bass_notes.reserve(kGridBars);

  Tick ticks_per_bar = time_sig.ticksPerBar();

  for (int bar_idx = 0; bar_idx < kGridBars; ++bar_idx) {
    // Skip bars covered by lamento bass.
    if (lamento_bars[static_cast<size_t>(bar_idx)]) {
      continue;
    }

    const auto& bar_info = grid.getBar(bar_idx);
    uint8_t primary_pitch = bar_info.bass_motion.primary_pitch;

    // Place primary pitch in bass register using nearestOctaveShift.
    int target_center = (kBassLow + kBassHigh) / 2;
    int diff = static_cast<int>(primary_pitch) - target_center;
    int shift = nearestOctaveShift(diff);
    uint8_t bass_pitch = clampPitch(
        static_cast<int>(primary_pitch) - shift, kBassLow, kBassHigh);

    Tick bar_start = static_cast<Tick>(bar_idx) * ticks_per_bar;

    // At cadence bars with resolution, split into primary + resolution.
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

      // Resolution note.
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
      // Non-cadence bar: one note spanning the full bar.
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

}  // namespace bach
