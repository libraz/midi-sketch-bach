/// @file
/// @brief Tempo map generators for per-form MIDI tempo events.

#include "harmony/tempo_map.h"

#include <algorithm>
#include <cmath>

namespace bach {

namespace {

/// @brief Minimum allowed BPM value.
constexpr uint16_t kMinBpm = 40;

/// @brief Maximum allowed BPM value.
constexpr uint16_t kMaxBpm = 200;

/// @brief Number of bars before stretto that get dominant pedal broadening.
constexpr int kDominantPedalBars = 2;

/// @brief Phrase boundary interval in bars for fantasia breathing points.
constexpr int kFantasiaPhraseInterval = 4;

/// @brief Number of final bars in fantasia that get additional broadening.
constexpr int kFantasiaFinalBars = 2;

// Fugue tempo percentages (Principle 4: fixed design values).
constexpr float kFugueExposition = 0.0f;
constexpr float kFugueEpisode = 3.0f;
constexpr float kFugueMiddleEntry = 0.0f;
constexpr float kFugueDominantPedal = -5.0f;
constexpr float kFugueStretto = 4.0f;
constexpr float kFugueCoda = -10.0f;

// Toccata tempo percentages (Principle 4: fixed design values).
constexpr float kToccataOpening = 8.0f;
constexpr float kToccataPreRecit = -15.0f;
constexpr float kToccataRecitBase = -10.0f;
constexpr float kToccataFermata = -25.0f;
constexpr float kToccataDrive = 12.0f;
constexpr float kToccataTransition = -8.0f;

// Fantasia tempo percentages (Principle 4: fixed design values).
constexpr float kFantasiaBase = -5.0f;
constexpr float kFantasiaPhraseBoundary = -10.0f;  // cumulative: base + phrase
constexpr float kFantasiaFinal = -13.0f;           // cumulative: base + final

// Passacaglia tempo percentages (Principle 4: fixed design values).
constexpr float kPassacagliaBase = 0.0f;
constexpr float kPassacagliaFinal = -3.0f;

}  // namespace

uint16_t adjustBpm(uint16_t base_bpm, float percent_change) {
  float adjusted = static_cast<float>(base_bpm) * (1.0f + percent_change / 100.0f);
  int rounded = static_cast<int>(std::round(adjusted));
  if (rounded < kMinBpm) rounded = kMinBpm;
  if (rounded > kMaxBpm) rounded = kMaxBpm;
  return static_cast<uint16_t>(rounded);
}

std::vector<TempoEvent> generateFugueTempoMap(const FugueStructure& structure,
                                               uint16_t base_bpm) {
  std::vector<TempoEvent> events;

  if (structure.sections.empty()) {
    events.push_back({0, base_bpm});
    return events;
  }

  // Find the first Stretto section tick for dominant pedal zone calculation.
  Tick stretto_start = 0;
  bool has_stretto = false;
  for (const auto& section : structure.sections) {
    if (section.type == SectionType::Stretto) {
      stretto_start = section.start_tick;
      has_stretto = true;
      break;
    }
  }

  Tick dom_pedal_tick = 0;
  if (has_stretto && stretto_start >= kDominantPedalBars * kTicksPerBar) {
    dom_pedal_tick = stretto_start - kDominantPedalBars * kTicksPerBar;
  }

  bool dom_pedal_emitted = false;

  for (const auto& section : structure.sections) {
    float percent = kFugueExposition;

    switch (section.type) {
      case SectionType::Exposition:
        percent = kFugueExposition;
        break;
      case SectionType::Episode:
        percent = kFugueEpisode;
        break;
      case SectionType::MiddleEntry:
        percent = kFugueMiddleEntry;
        break;
      case SectionType::Stretto:
        percent = kFugueStretto;
        break;
      case SectionType::Coda:
        percent = kFugueCoda;
        break;
    }

    // Insert dominant pedal zone event before stretto if applicable.
    if (has_stretto && !dom_pedal_emitted &&
        section.start_tick >= dom_pedal_tick &&
        section.type != SectionType::Coda) {
      if (dom_pedal_tick > 0 && dom_pedal_tick < section.start_tick) {
        events.push_back({dom_pedal_tick, adjustBpm(base_bpm, kFugueDominantPedal)});
        dom_pedal_emitted = true;
      }
    }

    events.push_back({section.start_tick, adjustBpm(base_bpm, percent)});

    if (section.type == SectionType::Stretto) {
      dom_pedal_emitted = true;
    }
  }

  // Sort by tick and deduplicate at same tick (keep last).
  std::sort(events.begin(), events.end(),
            [](const TempoEvent& a, const TempoEvent& b) {
              return a.tick < b.tick;
            });

  return events;
}

std::vector<TempoEvent> generateToccataTempoMap(Tick opening_start, Tick opening_end,
                                                 Tick recit_start, Tick recit_end,
                                                 Tick drive_start, Tick drive_end,
                                                 uint16_t base_bpm) {
  std::vector<TempoEvent> events;

  // Opening gesture: energetic.
  events.push_back({opening_start, adjustBpm(base_bpm, kToccataOpening)});

  // Pre-recitative ritardando (last bar of opening).
  if (opening_end > kTicksPerBar) {
    Tick pre_recit_tick = opening_end - kTicksPerBar;
    events.push_back({pre_recit_tick, adjustBpm(base_bpm, kToccataPreRecit)});
  }

  // Recitative base tempo.
  events.push_back({recit_start, adjustBpm(base_bpm, kToccataRecitBase)});

  // Fermata spots: at 1/3 and 2/3 of recitative.
  Tick recit_duration = recit_end - recit_start;
  if (recit_duration > kTicksPerBar * 3) {
    Tick fermata1 = recit_start + recit_duration / 3;
    Tick fermata2 = recit_start + recit_duration * 2 / 3;
    uint16_t fermata_bpm = adjustBpm(base_bpm, kToccataFermata);
    uint16_t recit_bpm = adjustBpm(base_bpm, kToccataRecitBase);

    events.push_back({fermata1, fermata_bpm});
    // Return to recitative tempo after fermata.
    events.push_back({fermata1 + kTicksPerBar, recit_bpm});
    events.push_back({fermata2, fermata_bpm});
    events.push_back({fermata2 + kTicksPerBar, recit_bpm});
  }

  // Drive to cadence: accelerando.
  events.push_back({drive_start, adjustBpm(base_bpm, kToccataDrive)});

  // Transition ritardando at end (last bar).
  if (drive_end > kTicksPerBar) {
    Tick transition_tick = drive_end - kTicksPerBar;
    events.push_back({transition_tick, adjustBpm(base_bpm, kToccataTransition)});
  }

  std::sort(events.begin(), events.end(),
            [](const TempoEvent& a, const TempoEvent& b) {
              return a.tick < b.tick;
            });

  return events;
}

std::vector<TempoEvent> generateFantasiaTempoMap(Tick total_duration, int section_bars,
                                                  uint16_t base_bpm) {
  std::vector<TempoEvent> events;

  // Contemplative base tempo.
  events.push_back({0, adjustBpm(base_bpm, kFantasiaBase)});

  // Phrase boundary ritardando every kFantasiaPhraseInterval bars.
  for (int bar = kFantasiaPhraseInterval; bar < section_bars - kFantasiaFinalBars;
       bar += kFantasiaPhraseInterval) {
    Tick boundary_tick = static_cast<Tick>(bar) * kTicksPerBar;
    if (boundary_tick < total_duration) {
      events.push_back({boundary_tick, adjustBpm(base_bpm, kFantasiaPhraseBoundary)});
      // Return to base tempo after 1 bar.
      Tick resume_tick = boundary_tick + kTicksPerBar;
      if (resume_tick < total_duration) {
        events.push_back({resume_tick, adjustBpm(base_bpm, kFantasiaBase)});
      }
    }
  }

  // Final broadening: last 2 bars.
  if (section_bars > kFantasiaFinalBars) {
    Tick final_tick = static_cast<Tick>(section_bars - kFantasiaFinalBars) * kTicksPerBar;
    if (final_tick < total_duration) {
      events.push_back({final_tick, adjustBpm(base_bpm, kFantasiaFinal)});
    }
  }

  std::sort(events.begin(), events.end(),
            [](const TempoEvent& a, const TempoEvent& b) {
              return a.tick < b.tick;
            });

  return events;
}

std::vector<TempoEvent> generatePassacagliaTempoMap(int num_variations,
                                                     int ground_bass_bars,
                                                     uint16_t base_bpm) {
  std::vector<TempoEvent> events;

  // Steady base tempo for all variations.
  events.push_back({0, adjustBpm(base_bpm, kPassacagliaBase)});

  // Slight broadening on the final variation.
  if (num_variations > 1 && ground_bass_bars > 0) {
    Tick final_var_tick =
        static_cast<Tick>(num_variations - 1) * static_cast<Tick>(ground_bass_bars) * kTicksPerBar;
    events.push_back({final_var_tick, adjustBpm(base_bpm, kPassacagliaFinal)});
  }

  return events;
}

std::vector<TempoEvent> generateCadenceRitardando(uint16_t base_bpm,
                                                    const std::vector<Tick>& cadence_ticks) {
  std::vector<TempoEvent> events;

  const uint16_t bpm_2beats = static_cast<uint16_t>(
      std::round(static_cast<float>(base_bpm) * kRitardandoFactor2Beats));
  const uint16_t bpm_1beat = static_cast<uint16_t>(
      std::round(static_cast<float>(base_bpm) * kRitardandoFactor1Beat));
  const uint16_t bpm_cadence = static_cast<uint16_t>(
      std::round(static_cast<float>(base_bpm) * kRitardandoFactorCadence));

  for (const Tick cadence_tick : cadence_ticks) {
    // Skip cadences too early for a full 2-beat lead-in.
    if (cadence_tick < 2 * kTicksPerBeat) {
      continue;
    }

    // 3-stage deceleration.
    events.push_back({cadence_tick - 2 * kTicksPerBeat, bpm_2beats});
    events.push_back({cadence_tick - 1 * kTicksPerBeat, bpm_1beat});
    events.push_back({cadence_tick, bpm_cadence});

    // A tempo restoration on the next beat.
    events.push_back({cadence_tick + kTicksPerBeat, base_bpm});
  }

  // Sort by tick.
  std::sort(events.begin(), events.end(),
            [](const TempoEvent& lhs, const TempoEvent& rhs) {
              return lhs.tick < rhs.tick;
            });

  // Deduplicate: keep only the last event at each tick position.
  // After sorting, consecutive events with the same tick keep the last one.
  if (events.size() > 1) {
    std::vector<TempoEvent> deduped;
    deduped.reserve(events.size());
    for (size_t idx = 0; idx < events.size(); ++idx) {
      // Skip if the next event has the same tick (keep the later one).
      if (idx + 1 < events.size() && events[idx].tick == events[idx + 1].tick) {
        continue;
      }
      deduped.push_back(events[idx]);
    }
    events = std::move(deduped);
  }

  return events;
}

}  // namespace bach
