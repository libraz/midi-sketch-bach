/// @file
/// @brief 3-voice invention (Sinfonia-style) variation generator for Goldberg Variations.
///
/// Generates Var 2: imitative counterpoint across 3 voices with a Playful
/// character soggetto. Structure: imitative exposition (bars 1-4), development
/// with inversions and diatonic sequences (bars 5-24), recapitulation (bars 25-32).

#include "forms/goldberg/variations/goldberg_invention.h"

#include <algorithm>
#include <cmath>
#include <random>

#include "core/note_creator.h"
#include "core/pitch_utils.h"
#include "core/scale.h"
#include "forms/goldberg/goldberg_figuren.h"
#include "forms/goldberg/goldberg_soggetto.h"
#include "fugue/subject.h"
#include "transform/motif_transform.h"
#include "transform/sequence.h"

namespace bach {

namespace {

/// Number of voices in a Sinfonia-style invention.
constexpr uint8_t kNumVoices = 3;

/// Voice indices for the 3-voice texture.
constexpr uint8_t kVoiceSoprano = 0;
constexpr uint8_t kVoiceAlto = 1;
constexpr uint8_t kVoiceBass = 2;

/// Register ranges (MIDI pitch) for each voice.
constexpr uint8_t kSopranoLow = 60;   // C4
constexpr uint8_t kSopranoHigh = 84;  // C6
constexpr uint8_t kAltoLow = 53;      // F3
constexpr uint8_t kAltoHigh = 76;     // E5
constexpr uint8_t kBassLow = 43;      // G2
constexpr uint8_t kBassHigh = 64;     // E4

/// @brief Assign voice index and clamp pitches to a voice register.
/// @param notes Notes to assign (modified in place).
/// @param voice_idx Voice index to assign.
/// @param low Minimum MIDI pitch for this register.
/// @param high Maximum MIDI pitch for this register.
void assignVoiceRegister(std::vector<NoteEvent>& notes,
                         uint8_t voice_idx,
                         uint8_t low,
                         uint8_t high) {
  for (auto& note : notes) {
    note.voice = voice_idx;
    // Octave-shift to fit register if needed.
    while (note.pitch < low && note.pitch + 12 <= 127) {
      note.pitch += 12;
    }
    while (note.pitch > high && note.pitch >= 12) {
      note.pitch -= 12;
    }
    note.pitch = clampPitch(static_cast<int>(note.pitch), low, high);
  }
}

/// @brief Set BachNoteSource for all notes.
/// @param notes Notes to tag.
/// @param source Source tag to apply.
void tagSource(std::vector<NoteEvent>& notes, BachNoteSource source) {
  for (auto& note : notes) {
    note.source = source;
  }
}

/// @brief Generate a simple bass line from the structural grid for unfilled bars.
/// @param grid The 32-bar structural grid.
/// @param time_sig Time signature for bar duration.
/// @param start_bar First bar (0-based) to generate.
/// @param end_bar Last bar (exclusive, 0-based).
/// @return Bass notes with whole-bar durations.
std::vector<NoteEvent> generateStructuralBass(
    const GoldbergStructuralGrid& grid,
    const TimeSignature& time_sig,
    int start_bar,
    int end_bar) {
  std::vector<NoteEvent> bass;
  Tick ticks_per_bar = time_sig.ticksPerBar();

  for (int bar = start_bar; bar < end_bar && bar < 32; ++bar) {
    uint8_t pitch = grid.getStructuralBassPitch(bar);
    // Ensure bass pitch is in bass register.
    while (pitch > kBassHigh && pitch >= 12) {
      pitch -= 12;
    }
    while (pitch < kBassLow && pitch + 12 <= 127) {
      pitch += 12;
    }

    BachNoteOptions opts;
    opts.voice = kVoiceBass;
    opts.desired_pitch = pitch;
    opts.tick = static_cast<Tick>(bar) * ticks_per_bar;
    opts.duration = ticks_per_bar;
    opts.velocity = 75;
    opts.source = BachNoteSource::GoldbergInvention;

    auto result = createBachNote(nullptr, nullptr, nullptr, opts);
    if (result.accepted) {
      bass.push_back(result.note);
    }
  }
  return bass;
}

/// @brief Generate free counterpoint fill using Figurenlehre patterns.
/// @param grid Structural grid for harmonic context.
/// @param key Key signature.
/// @param time_sig Time signature.
/// @param voice_idx Voice index for the fill.
/// @param start_bar First bar (0-based).
/// @param end_bar Last bar (exclusive, 0-based).
/// @param seed Random seed.
/// @return Fill notes for the specified bars and voice.
std::vector<NoteEvent> generateFreeCounterpoint(
    const GoldbergStructuralGrid& grid,
    const KeySignature& key,
    const TimeSignature& time_sig,
    uint8_t voice_idx,
    int start_bar,
    int end_bar,
    uint32_t seed,
    uint8_t notes_per_beat = 4) {
  // Use FigurenGenerator with a profile for free counterpoint fill.
  FiguraProfile profile;
  profile.primary = FiguraType::Circulatio;
  profile.secondary = FiguraType::Arpeggio;
  profile.notes_per_beat = notes_per_beat;
  profile.direction = DirectionBias::Symmetric;
  profile.chord_tone_ratio = 0.65f;
  profile.sequence_probability = 0.35f;

  FigurenGenerator figuren;
  auto full_notes = figuren.generate(
      profile, grid, key, time_sig, voice_idx, seed);

  // Filter to only the requested bar range.
  Tick ticks_per_bar = time_sig.ticksPerBar();
  Tick start_tick = static_cast<Tick>(start_bar) * ticks_per_bar;
  Tick end_tick = static_cast<Tick>(end_bar) * ticks_per_bar;

  std::vector<NoteEvent> filtered;
  for (auto& note : full_notes) {
    if (note.start_tick >= start_tick && note.start_tick < end_tick) {
      note.source = BachNoteSource::GoldbergInvention;
      filtered.push_back(note);
    }
  }
  return filtered;
}

/// @brief Select a development technique based on RNG and bar position.
/// @param rng Random number generator.
/// @param bar_in_section Bar offset within development section.
/// @return 0 = soggetto statement, 1 = inversion, 2 = sequence, 3 = free episode.
int selectDevelopmentTechnique(std::mt19937& rng, int bar_in_section) {
  // Favor subject statements early, sequences and free episodes later.
  uint32_t roll = rng() % 100;
  if (bar_in_section < 4) {
    // Early development: more soggetto statements.
    if (roll < 35) return 0;       // Soggetto statement.
    if (roll < 55) return 1;       // Inversion.
    if (roll < 80) return 2;       // Sequence.
    return 3;                      // Free episode.
  }
  if (bar_in_section < 12) {
    // Mid development: balanced.
    if (roll < 25) return 0;
    if (roll < 50) return 1;
    if (roll < 75) return 2;
    return 3;
  }
  // Late development: more sequential and episodic.
  if (roll < 15) return 0;
  if (roll < 35) return 1;
  if (roll < 65) return 2;
  return 3;
}

}  // namespace

// ---------------------------------------------------------------------------
// Main generation pipeline
// ---------------------------------------------------------------------------

InventionResult InventionGenerator::generate(
    const GoldbergStructuralGrid& grid,
    const KeySignature& key,
    const TimeSignature& time_sig,
    uint32_t seed) const {
  InventionResult result;
  std::mt19937 rng(seed);
  Tick ticks_per_bar = time_sig.ticksPerBar();
  ScaleType scale = key.is_minor ? ScaleType::HarmonicMinor : ScaleType::Major;

  // =========================================================================
  // Step 1: Generate soggetto (1-2 bars, Playful character)
  // =========================================================================
  SoggettoParams soggetto_params;
  soggetto_params.length_bars = 2;
  soggetto_params.character = SubjectCharacter::Playful;
  soggetto_params.grid = &grid;
  soggetto_params.start_bar = 1;
  soggetto_params.path_candidates = 12;

  SoggettoGenerator soggetto_gen;
  Subject soggetto = soggetto_gen.generate(
      soggetto_params, key, time_sig, rng());
  if (soggetto.notes.empty()) {
    result.success = false;
    return result;
  }

  Tick soggetto_duration = soggetto.length_ticks;
  auto soggetto_notes = soggetto.notes;

  // Tag soggetto entries as GoldbergSoggetto (they are the subject).
  tagSource(soggetto_notes, BachNoteSource::GoldbergSoggetto);

  // =========================================================================
  // Step 2: Imitative exposition (bars 1-4, 0-based bars 0-3)
  // =========================================================================
  std::vector<NoteEvent> all_notes;
  all_notes.reserve(512);

  // Voice 1 (soprano): soggetto at bars 0-1 (ticks 0 to soggetto_duration).
  {
    auto voice1_entry = soggetto_notes;
    assignVoiceRegister(voice1_entry, kVoiceSoprano,
                        kSopranoLow, kSopranoHigh);
    all_notes.insert(all_notes.end(), voice1_entry.begin(), voice1_entry.end());
  }

  // Voice 2 (alto): soggetto transposed to dominant, starting at bar 1
  // (stretto-like overlap with voice 1).
  {
    // Transpose to dominant: +4 scale degrees (tonic -> dominant).
    auto voice2_entry = transposeMelodyDiatonic(
        soggetto_notes, 4, key.tonic, scale);
    Tick voice2_start = ticks_per_bar;
    // Shift ticks so the entry starts at bar 1.
    Tick original_start = voice2_entry.empty() ? 0 : voice2_entry[0].start_tick;
    for (auto& note : voice2_entry) {
      note.start_tick = note.start_tick - original_start + voice2_start;
    }
    assignVoiceRegister(voice2_entry, kVoiceAlto, kAltoLow, kAltoHigh);
    tagSource(voice2_entry, BachNoteSource::GoldbergSoggetto);
    all_notes.insert(all_notes.end(), voice2_entry.begin(), voice2_entry.end());

    // Free counterpoint for voice 1 during voice 2 entry (bars 1-2).
    auto voice1_fill = generateFreeCounterpoint(
        grid, key, time_sig, kVoiceSoprano, 1, 3, rng());
    assignVoiceRegister(voice1_fill, kVoiceSoprano,
                        kSopranoLow, kSopranoHigh);
    all_notes.insert(all_notes.end(), voice1_fill.begin(), voice1_fill.end());
  }

  // Voice 3 (bass): soggetto at original pitch in bass register, bars 2-3.
  {
    auto voice3_entry = soggetto_notes;
    Tick voice3_start = 2 * ticks_per_bar;
    Tick original_start = voice3_entry.empty() ? 0 : voice3_entry[0].start_tick;
    for (auto& note : voice3_entry) {
      note.start_tick = note.start_tick - original_start + voice3_start;
    }
    assignVoiceRegister(voice3_entry, kVoiceBass, kBassLow, kBassHigh);
    tagSource(voice3_entry, BachNoteSource::GoldbergSoggetto);
    all_notes.insert(all_notes.end(), voice3_entry.begin(), voice3_entry.end());

    // Free counterpoint for voices 1 and 2 during voice 3 entry (bars 2-3).
    auto fill_v1 = generateFreeCounterpoint(
        grid, key, time_sig, kVoiceSoprano, 2, 4, rng());
    assignVoiceRegister(fill_v1, kVoiceSoprano, kSopranoLow, kSopranoHigh);
    all_notes.insert(all_notes.end(), fill_v1.begin(), fill_v1.end());

    auto fill_v2 = generateFreeCounterpoint(
        grid, key, time_sig, kVoiceAlto, 2, 4, rng());
    assignVoiceRegister(fill_v2, kVoiceAlto, kAltoLow, kAltoHigh);
    all_notes.insert(all_notes.end(), fill_v2.begin(), fill_v2.end());
  }

  // Post-exposition link (bar 3): fill remaining voices.
  {
    auto bass_link = generateStructuralBass(grid, time_sig, 3, 4);
    // Only add if voice 3 entry didn't already cover bar 3.
    if (soggetto_duration <= ticks_per_bar) {
      all_notes.insert(all_notes.end(), bass_link.begin(), bass_link.end());
    }
  }

  // =========================================================================
  // Step 3: Development (bars 4-23, 0-based)
  // =========================================================================
  constexpr int kDevStartBar = 4;
  constexpr int kDevEndBar = 24;

  int dev_bar = kDevStartBar;
  while (dev_bar < kDevEndBar) {
    int bar_in_section = dev_bar - kDevStartBar;
    int technique = selectDevelopmentTechnique(rng, bar_in_section);
    int bars_remaining = kDevEndBar - dev_bar;

    switch (technique) {
      case 0: {
        // Soggetto statement in a random voice.
        if (bars_remaining < 2) {
          technique = 3;  // Fall through to free episode.
          break;
        }
        uint8_t voice = static_cast<uint8_t>(rng() % kNumVoices);
        auto entry = soggetto_notes;
        Tick entry_start = static_cast<Tick>(dev_bar) * ticks_per_bar;
        Tick original_start = entry.empty() ? 0 : entry[0].start_tick;
        for (auto& note : entry) {
          note.start_tick = note.start_tick - original_start + entry_start;
        }
        tagSource(entry, BachNoteSource::GoldbergSoggetto);

        // Select register based on voice.
        if (voice == kVoiceSoprano) {
          assignVoiceRegister(entry, kVoiceSoprano,
                              kSopranoLow, kSopranoHigh);
        } else if (voice == kVoiceAlto) {
          assignVoiceRegister(entry, kVoiceAlto, kAltoLow, kAltoHigh);
        } else {
          assignVoiceRegister(entry, kVoiceBass, kBassLow, kBassHigh);
        }
        all_notes.insert(all_notes.end(), entry.begin(), entry.end());

        // Fill other voices with free counterpoint.
        for (uint8_t other = 0; other < kNumVoices; ++other) {
          if (other == voice) continue;
          int fill_end = std::min(dev_bar + 2, kDevEndBar);
          auto fill = generateFreeCounterpoint(
              grid, key, time_sig, other, dev_bar, fill_end, rng());
          if (other == kVoiceSoprano) {
            assignVoiceRegister(fill, kVoiceSoprano,
                                kSopranoLow, kSopranoHigh);
          } else if (other == kVoiceAlto) {
            assignVoiceRegister(fill, kVoiceAlto, kAltoLow, kAltoHigh);
          } else {
            assignVoiceRegister(fill, kVoiceBass, kBassLow, kBassHigh);
          }
          all_notes.insert(all_notes.end(), fill.begin(), fill.end());
        }
        dev_bar += 2;
        continue;
      }

      case 1: {
        // Diatonic inversion of soggetto.
        if (bars_remaining < 2) {
          technique = 3;
          break;
        }
        uint8_t pivot = soggetto_notes.empty()
                            ? 67
                            : soggetto_notes[0].pitch;
        auto inverted = invertMelodyDiatonic(
            soggetto_notes, pivot, key.tonic, scale);
        Tick inv_start = static_cast<Tick>(dev_bar) * ticks_per_bar;
        Tick original_start = inverted.empty() ? 0 : inverted[0].start_tick;
        for (auto& note : inverted) {
          note.start_tick = note.start_tick - original_start + inv_start;
        }
        tagSource(inverted, BachNoteSource::GoldbergInvention);

        uint8_t inv_voice = static_cast<uint8_t>(rng() % kNumVoices);
        if (inv_voice == kVoiceSoprano) {
          assignVoiceRegister(inverted, kVoiceSoprano,
                              kSopranoLow, kSopranoHigh);
        } else if (inv_voice == kVoiceAlto) {
          assignVoiceRegister(inverted, kVoiceAlto, kAltoLow, kAltoHigh);
        } else {
          assignVoiceRegister(inverted, kVoiceBass, kBassLow, kBassHigh);
        }
        all_notes.insert(all_notes.end(), inverted.begin(), inverted.end());

        // Fill other voices.
        for (uint8_t other = 0; other < kNumVoices; ++other) {
          if (other == inv_voice) continue;
          int fill_end = std::min(dev_bar + 2, kDevEndBar);
          auto fill = generateFreeCounterpoint(
              grid, key, time_sig, other, dev_bar, fill_end, rng());
          if (other == kVoiceSoprano) {
            assignVoiceRegister(fill, kVoiceSoprano,
                                kSopranoLow, kSopranoHigh);
          } else if (other == kVoiceAlto) {
            assignVoiceRegister(fill, kVoiceAlto, kAltoLow, kAltoHigh);
          } else {
            assignVoiceRegister(fill, kVoiceBass, kBassLow, kBassHigh);
          }
          all_notes.insert(all_notes.end(), fill.begin(), fill.end());
        }
        dev_bar += 2;
        continue;
      }

      case 2: {
        // Diatonic sequence passage (2-3 repetitions descending by step).
        int seq_bars = std::min(3, bars_remaining);
        if (soggetto_notes.size() < 2) {
          // Not enough motif material; fall through to free episode.
          technique = 3;
          break;
        }

        // Extract head motif (first bar of soggetto) for sequence.
        std::vector<NoteEvent> motif;
        for (const auto& note : soggetto_notes) {
          if (note.start_tick < ticks_per_bar) {
            motif.push_back(note);
          }
        }
        if (motif.empty()) {
          technique = 3;
          break;
        }

        // Shift motif to dev_bar start.
        Tick motif_start = static_cast<Tick>(dev_bar) * ticks_per_bar;
        Tick orig_start = motif[0].start_tick;
        for (auto& note : motif) {
          note.start_tick = note.start_tick - orig_start + motif_start;
        }

        // Original motif in a voice.
        uint8_t seq_voice = static_cast<uint8_t>(rng() % 2);  // Soprano or alto.
        auto seq_motif = motif;
        tagSource(seq_motif, BachNoteSource::GoldbergInvention);
        if (seq_voice == kVoiceSoprano) {
          assignVoiceRegister(seq_motif, kVoiceSoprano,
                              kSopranoLow, kSopranoHigh);
        } else {
          assignVoiceRegister(seq_motif, kVoiceAlto, kAltoLow, kAltoHigh);
        }
        all_notes.insert(all_notes.end(), seq_motif.begin(), seq_motif.end());

        // Sequential repetitions (descending by step, -1 degree).
        int reps = seq_bars - 1;
        if (reps > 0) {
          Tick seq_start_tick = motif_start + motifDuration(motif);
          auto seq_notes = generateDiatonicSequence(
              motif, reps, -1, seq_start_tick, key.tonic, scale);
          tagSource(seq_notes, BachNoteSource::GoldbergInvention);
          if (seq_voice == kVoiceSoprano) {
            assignVoiceRegister(seq_notes, kVoiceSoprano,
                                kSopranoLow, kSopranoHigh);
          } else {
            assignVoiceRegister(seq_notes, kVoiceAlto, kAltoLow, kAltoHigh);
          }
          all_notes.insert(all_notes.end(), seq_notes.begin(), seq_notes.end());
        }

        // Fill other voices.
        for (uint8_t other = 0; other < kNumVoices; ++other) {
          if (other == seq_voice) continue;
          int fill_end = std::min(dev_bar + seq_bars, kDevEndBar);
          auto fill = generateFreeCounterpoint(
              grid, key, time_sig, other, dev_bar, fill_end, rng());
          if (other == kVoiceSoprano) {
            assignVoiceRegister(fill, kVoiceSoprano,
                                kSopranoLow, kSopranoHigh);
          } else if (other == kVoiceAlto) {
            assignVoiceRegister(fill, kVoiceAlto, kAltoLow, kAltoHigh);
          } else {
            assignVoiceRegister(fill, kVoiceBass, kBassLow, kBassHigh);
          }
          all_notes.insert(all_notes.end(), fill.begin(), fill.end());
        }
        dev_bar += seq_bars;
        continue;
      }

      default:
        break;
    }

    // technique == 3 or fallthrough: free episode for all voices.
    {
      int episode_bars = std::min(2, bars_remaining);
      for (uint8_t voice = 0; voice < kNumVoices; ++voice) {
        int fill_end = std::min(dev_bar + episode_bars, kDevEndBar);
        auto fill = generateFreeCounterpoint(
            grid, key, time_sig, voice, dev_bar, fill_end, rng());
        if (voice == kVoiceSoprano) {
          assignVoiceRegister(fill, kVoiceSoprano,
                              kSopranoLow, kSopranoHigh);
        } else if (voice == kVoiceAlto) {
          assignVoiceRegister(fill, kVoiceAlto, kAltoLow, kAltoHigh);
        } else {
          assignVoiceRegister(fill, kVoiceBass, kBassLow, kBassHigh);
        }
        all_notes.insert(all_notes.end(), fill.begin(), fill.end());
      }
      dev_bar += std::min(2, bars_remaining);
    }
  }

  // =========================================================================
  // Step 4: Recapitulation (bars 24-31, 0-based)
  // =========================================================================
  constexpr int kRecapStartBar = 24;
  constexpr int kRecapEndBar = 32;

  // Final soggetto statement in voice 1 (soprano) at bar 24-25.
  {
    auto final_entry = soggetto_notes;
    Tick final_start = static_cast<Tick>(kRecapStartBar) * ticks_per_bar;
    Tick original_start = final_entry.empty() ? 0 : final_entry[0].start_tick;
    for (auto& note : final_entry) {
      note.start_tick = note.start_tick - original_start + final_start;
    }
    tagSource(final_entry, BachNoteSource::GoldbergSoggetto);
    assignVoiceRegister(final_entry, kVoiceSoprano,
                        kSopranoLow, kSopranoHigh);
    all_notes.insert(all_notes.end(), final_entry.begin(), final_entry.end());
  }

  // Dominant soggetto in alto at bar 25-26 (stretto with soprano).
  {
    auto alto_recap = transposeMelodyDiatonic(
        soggetto_notes, 4, key.tonic, scale);
    Tick alto_start = static_cast<Tick>(kRecapStartBar + 1) * ticks_per_bar;
    Tick original_start = alto_recap.empty() ? 0 : alto_recap[0].start_tick;
    for (auto& note : alto_recap) {
      note.start_tick = note.start_tick - original_start + alto_start;
    }
    tagSource(alto_recap, BachNoteSource::GoldbergSoggetto);
    assignVoiceRegister(alto_recap, kVoiceAlto, kAltoLow, kAltoHigh);
    all_notes.insert(all_notes.end(), alto_recap.begin(), alto_recap.end());
  }

  // Free counterpoint + cadential approach for remaining recap bars.
  {
    int fill_start = kRecapStartBar + 2;
    for (uint8_t voice = 0; voice < kNumVoices; ++voice) {
      auto fill = generateFreeCounterpoint(
          grid, key, time_sig, voice, fill_start, kRecapEndBar, rng());
      if (voice == kVoiceSoprano) {
        assignVoiceRegister(fill, kVoiceSoprano,
                            kSopranoLow, kSopranoHigh);
      } else if (voice == kVoiceAlto) {
        assignVoiceRegister(fill, kVoiceAlto, kAltoLow, kAltoHigh);
      } else {
        assignVoiceRegister(fill, kVoiceBass, kBassLow, kBassHigh);
      }
      all_notes.insert(all_notes.end(), fill.begin(), fill.end());
    }
  }

  // Bass structural support for recap.
  {
    auto recap_bass = generateStructuralBass(
        grid, time_sig, kRecapStartBar, kRecapEndBar);
    all_notes.insert(all_notes.end(), recap_bass.begin(), recap_bass.end());
  }

  // =========================================================================
  // Step 5: Finalize -- sort notes, ensure within 32-bar bounds
  // =========================================================================
  Tick max_tick = 32 * ticks_per_bar;
  // Remove notes that extend beyond 32 bars.
  all_notes.erase(
      std::remove_if(all_notes.begin(), all_notes.end(),
                     [max_tick](const NoteEvent& note) {
                       return note.start_tick >= max_tick;
                     }),
      all_notes.end());

  // Trim durations that exceed the 32-bar boundary.
  for (auto& note : all_notes) {
    if (note.start_tick + note.duration > max_tick) {
      note.duration = max_tick - note.start_tick;
    }
  }

  // Sort by start_tick, then by voice.
  std::sort(all_notes.begin(), all_notes.end(),
            [](const NoteEvent& lhs, const NoteEvent& rhs) {
              if (lhs.start_tick != rhs.start_tick) {
                return lhs.start_tick < rhs.start_tick;
              }
              return lhs.voice < rhs.voice;
            });

  result.notes = std::move(all_notes);
  result.success = !result.notes.empty();
  return result;
}

}  // namespace bach
