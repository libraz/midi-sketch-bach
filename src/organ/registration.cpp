/// @file
/// @brief Organ registration plan -- manages stop/volume changes at structural points.

#include "organ/registration.h"

#include <algorithm>

namespace bach {

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

namespace {

/// Number of organ channels (Great=0, Swell=1, Positiv=2, Pedal=3).
constexpr uint8_t kOrganChannelCount = 4;

/// MIDI CC number for Main Volume.
constexpr uint8_t kCcMainVolume = 7;

/// MIDI CC number for Expression.
constexpr uint8_t kCcExpression = 11;

/// MIDI Controller Change status byte base (0xB0).
constexpr uint8_t kControlChangeBase = 0xB0;

/// Maximum MIDI data value.
constexpr uint8_t kMaxMidiValue = 127;

/// @brief Map energy level to CC#7 (volume) value.
/// @param energy Energy level in [0,1].
/// @return CC#7 value (0-127).
uint8_t energyToVolume(float energy) {
  if (energy < 0.3f) return 64;
  if (energy < 0.6f) return 80;
  if (energy < 0.8f) return 100;
  return 120;
}

/// @brief Insert registration CC events into matching tracks.
/// @param tracks Track vector to search and modify.
/// @param events CC events to insert (each has a channel in its status byte).
void insertEventsIntoTracks(std::vector<Track>& tracks,
                            const std::vector<MidiEvent>& events) {
  for (const auto& evt : events) {
    uint8_t event_channel = evt.status & 0x0F;
    for (auto& track : tracks) {
      if (track.channel == event_channel) {
        track.events.push_back(evt);
        break;  // One track per channel for organ
      }
    }
  }
}

}  // namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

RegistrationPlan createDefaultRegistrationPlan() {
  RegistrationPlan plan;

  // Exposition: standard organ tone, quieter
  plan.exposition.manual1_program = 19;   // Church Organ
  plan.exposition.manual2_program = 20;   // Reed Organ
  plan.exposition.manual3_program = 19;   // Church Organ
  plan.exposition.pedal_program = 19;     // Church Organ
  plan.exposition.velocity_hint = 75;

  // Stretto: fuller registration, all Church Organ
  plan.stretto.manual1_program = 19;
  plan.stretto.manual2_program = 19;
  plan.stretto.manual3_program = 19;
  plan.stretto.pedal_program = 19;
  plan.stretto.velocity_hint = 90;

  // Coda: full registration with tutti
  plan.coda.manual1_program = 19;
  plan.coda.manual2_program = 19;
  plan.coda.manual3_program = 19;
  plan.coda.pedal_program = 19;
  plan.coda.velocity_hint = 100;

  return plan;
}

const Registration& getRegistrationForPhase(const RegistrationPlan& plan,
                                            FuguePhase phase,
                                            bool is_coda) {
  if (is_coda) {
    return plan.coda;
  }

  switch (phase) {
    case FuguePhase::Establish:
      return plan.exposition;
    case FuguePhase::Develop:
      return plan.exposition;  // No change during development
    case FuguePhase::Resolve:
      return plan.stretto;
  }

  // Unreachable for well-formed enums, but satisfies compiler warnings.
  return plan.exposition;  // NOLINT(clang-diagnostic-covered-switch-default): enum safety
}

std::vector<MidiEvent> generateRegistrationEvents(const Registration& reg, Tick tick) {
  std::vector<MidiEvent> events;
  events.reserve(kOrganChannelCount * 2);

  uint8_t clamped_value = std::min(reg.velocity_hint, kMaxMidiValue);

  for (uint8_t channel = 0; channel < kOrganChannelCount; ++channel) {
    uint8_t status = kControlChangeBase | channel;

    // CC#7: Main Volume
    MidiEvent vol_event;
    vol_event.tick = tick;
    vol_event.status = status;
    vol_event.data1 = kCcMainVolume;
    vol_event.data2 = clamped_value;
    events.push_back(vol_event);

    // CC#11: Expression
    MidiEvent expr_event;
    expr_event.tick = tick;
    expr_event.status = status;
    expr_event.data1 = kCcExpression;
    expr_event.data2 = clamped_value;
    events.push_back(expr_event);
  }

  return events;
}

void applyRegistrationPlan(std::vector<Track>& tracks,
                           const RegistrationPlan& plan,
                           Tick exposition_tick,
                           Tick stretto_tick,
                           Tick coda_tick) {
  // Always insert exposition registration
  auto expo_events = generateRegistrationEvents(plan.exposition, exposition_tick);
  insertEventsIntoTracks(tracks, expo_events);

  // Insert stretto registration if specified
  if (stretto_tick > 0) {
    auto stretto_events = generateRegistrationEvents(plan.stretto, stretto_tick);
    insertEventsIntoTracks(tracks, stretto_events);
  }

  // Insert coda registration if specified
  if (coda_tick > 0) {
    auto coda_events = generateRegistrationEvents(plan.coda, coda_tick);
    insertEventsIntoTracks(tracks, coda_events);
  }
}

std::vector<MidiEvent> generateEnergyRegistrationEvents(
    const std::vector<std::pair<Tick, float>>& energy_levels, uint8_t num_channels) {
  std::vector<MidiEvent> events;
  events.reserve(energy_levels.size() * num_channels);

  for (const auto& [tick, energy] : energy_levels) {
    uint8_t volume = energyToVolume(energy);
    for (uint8_t channel = 0; channel < num_channels; ++channel) {
      MidiEvent evt;
      evt.tick = tick;
      evt.status = kControlChangeBase | channel;
      evt.data1 = kCcMainVolume;
      evt.data2 = volume;
      events.push_back(evt);
    }
  }

  return events;
}

}  // namespace bach
