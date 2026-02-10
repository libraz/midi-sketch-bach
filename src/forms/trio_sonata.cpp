// Implementation of the trio sonata generator (BWV 525-530 style).

#include "forms/trio_sonata.h"

#include <algorithm>
#include <random>
#include <vector>

#include "core/gm_program.h"
#include "core/pitch_utils.h"
#include "core/rng_util.h"
#include "core/scale.h"
#include "harmony/chord_types.h"
#include "harmony/harmonic_event.h"
#include "harmony/harmonic_timeline.h"

namespace bach {

namespace {

/// @brief Organ velocity (pipe organs have no velocity sensitivity).
constexpr uint8_t kOrganVelocity = 80;

/// @brief Number of bars for fast outer movements (1st, 3rd).
constexpr Tick kFastMovementBars = 16;

/// @brief Number of bars for slow middle movement (2nd).
constexpr Tick kSlowMovementBars = 12;

/// @brief Duration of a 16th note in ticks.
constexpr Tick kSixteenthNote = kTicksPerBeat / 4;  // 120

/// @brief Duration of an 8th note in ticks.
constexpr Tick kEighthNote = kTicksPerBeat / 2;  // 240

/// @brief Duration of a quarter note in ticks.
constexpr Tick kQuarterNote = kTicksPerBeat;  // 480

/// @brief Duration of a half note in ticks.
constexpr Tick kHalfNote = kTicksPerBeat * 2;  // 960

/// @brief Seed offset for movement 2 (ensures different RNG state).
constexpr uint32_t kMovement2SeedOffset = 1000;

/// @brief Seed offset for movement 3 (ensures different RNG state).
constexpr uint32_t kMovement3SeedOffset = 2000;

/// @brief Number of voices in a trio sonata (always 3).
constexpr uint8_t kTrioVoiceCount = 3;

/// @brief MIDI channel for right hand (Great).
constexpr uint8_t kRhChannel = 0;

/// @brief MIDI channel for left hand (Swell).
constexpr uint8_t kLhChannel = 1;

/// @brief MIDI channel for pedal.
constexpr uint8_t kPedalChannel = 3;

// ---------------------------------------------------------------------------
// Track creation
// ---------------------------------------------------------------------------

/// @brief Create the 3 MIDI tracks for a trio sonata movement.
///
/// Channel/program mapping per the organ system spec for trio sonata:
///   Voice 0 -> Ch 0, Church Organ (Great / Right Hand)
///   Voice 1 -> Ch 1, Reed Organ   (Swell / Left Hand)
///   Voice 2 -> Ch 3, Church Organ (Pedal)
///
/// @return Vector of 3 Track objects with channel/program/name configured.
std::vector<Track> createTrioSonataTracks() {
  std::vector<Track> tracks;
  tracks.reserve(kTrioVoiceCount);

  Track rh_track;
  rh_track.channel = kRhChannel;
  rh_track.program = GmProgram::kChurchOrgan;
  rh_track.name = "Right Hand (Great)";
  tracks.push_back(rh_track);

  Track lh_track;
  lh_track.channel = kLhChannel;
  lh_track.program = GmProgram::kReedOrgan;
  lh_track.name = "Left Hand (Swell)";
  tracks.push_back(lh_track);

  Track pedal_track;
  pedal_track.channel = kPedalChannel;
  pedal_track.program = GmProgram::kChurchOrgan;
  pedal_track.name = "Pedal";
  tracks.push_back(pedal_track);

  return tracks;
}

// ---------------------------------------------------------------------------
// Scale tone extraction
// ---------------------------------------------------------------------------

/// @brief Get scale tones within a pitch range for a given key context.
///
/// @param key Musical key (pitch class of tonic).
/// @param is_minor True for minor mode, false for major.
/// @param low_pitch Lowest MIDI pitch to include.
/// @param high_pitch Highest MIDI pitch to include.
/// @return Vector of scale-member MIDI pitches in ascending order.
std::vector<uint8_t> getTrioScaleTones(Key key, bool is_minor, uint8_t low_pitch,
                                       uint8_t high_pitch) {
  std::vector<uint8_t> tones;
  ScaleType scale_type = is_minor ? ScaleType::HarmonicMinor : ScaleType::Major;

  for (int pitch = static_cast<int>(low_pitch); pitch <= static_cast<int>(high_pitch); ++pitch) {
    if (scale_util::isScaleTone(static_cast<uint8_t>(pitch), key, scale_type)) {
      tones.push_back(static_cast<uint8_t>(pitch));
    }
  }

  return tones;
}

// ---------------------------------------------------------------------------
// Voice generation: Right Hand (Voice 0, Great)
// ---------------------------------------------------------------------------

/// @brief Generate right hand melodic line for one harmonic event span.
///
/// Creates a melodic line using chord tones and scale tones. Fast movements
/// use 8th and 16th notes; slow movements use quarter and 8th notes.
///
/// @param event Current harmonic event.
/// @param is_slow True for slow movement (longer note values).
/// @param rng Random number generator.
/// @return Vector of NoteEvent entries for the right hand.
std::vector<NoteEvent> generateRightHand(const HarmonicEvent& event, bool is_slow,
                                         std::mt19937& rng) {
  std::vector<NoteEvent> notes;

  // Right hand range: upper part of Great manual (C4-C6 = 60-96).
  constexpr uint8_t kRhLow = 60;
  constexpr uint8_t kRhHigh = organ_range::kManual1High;  // 96

  auto scale_tones = getTrioScaleTones(event.key, event.is_minor, kRhLow, kRhHigh);
  if (scale_tones.empty()) {
    return notes;
  }

  Tick event_duration = event.end_tick - event.tick;
  Tick current_tick = event.tick;

  // Note durations: fast movements mix 8th and 16th; slow movements use quarter and 8th.
  Tick primary_dur = is_slow ? kQuarterNote : kEighthNote;
  Tick secondary_dur = is_slow ? kEighthNote : kSixteenthNote;

  // Start in the middle of the available scale tones.
  size_t tone_idx = scale_tones.size() / 2;

  // Determine melodic direction for this event span.
  bool ascending = rng::rollProbability(rng, 0.5f);

  while (current_tick < event.tick + event_duration) {
    // Choose note duration: primarily use the primary duration, occasionally secondary.
    Tick dur = rng::rollProbability(rng, 0.6f) ? primary_dur : secondary_dur;
    Tick remaining = (event.tick + event_duration) - current_tick;
    if (dur > remaining) dur = remaining;
    if (dur == 0) break;

    NoteEvent note;
    note.start_tick = current_tick;
    note.duration = dur;
    note.pitch = scale_tones[tone_idx];
    note.velocity = kOrganVelocity;
    note.voice = 0;
    notes.push_back(note);

    current_tick += dur;

    // Step through scale tones with occasional leaps (skip a tone).
    int step = rng::rollProbability(rng, 0.3f) ? 2 : 1;

    if (ascending) {
      if (tone_idx + step < scale_tones.size()) {
        tone_idx += step;
      } else {
        ascending = false;
        if (tone_idx >= static_cast<size_t>(step)) {
          tone_idx -= step;
        } else {
          tone_idx = 0;
        }
      }
    } else {
      if (tone_idx >= static_cast<size_t>(step)) {
        tone_idx -= step;
      } else {
        ascending = true;
        if (tone_idx + step < scale_tones.size()) {
          tone_idx += step;
        } else if (!scale_tones.empty()) {
          tone_idx = scale_tones.size() - 1;
        }
      }
    }

    // Occasionally change direction for melodic interest.
    if (rng::rollProbability(rng, 0.2f)) {
      ascending = !ascending;
    }
  }

  return notes;
}

// ---------------------------------------------------------------------------
// Voice generation: Left Hand (Voice 1, Swell)
// ---------------------------------------------------------------------------

/// @brief Generate left hand counter-melody for one harmonic event span.
///
/// Creates a complementary line to the right hand. Uses offset rhythms
/// (when RH has short notes, LH has longer notes and vice versa) for
/// rhythmic independence. Occupies a lower register than the right hand.
///
/// @param event Current harmonic event.
/// @param is_slow True for slow movement.
/// @param rng Random number generator.
/// @return Vector of NoteEvent entries for the left hand.
std::vector<NoteEvent> generateLeftHand(const HarmonicEvent& event, bool is_slow,
                                        std::mt19937& rng) {
  std::vector<NoteEvent> notes;

  // Left hand range: middle part of Swell manual (C3-B4 = 48-71).
  constexpr uint8_t kLhLow = organ_range::kManual2Low;  // 36
  constexpr uint8_t kLhHigh = 71;

  auto scale_tones = getTrioScaleTones(event.key, event.is_minor, kLhLow, kLhHigh);
  if (scale_tones.empty()) {
    return notes;
  }

  Tick event_duration = event.end_tick - event.tick;
  Tick current_tick = event.tick;

  // Complementary rhythm: when RH is fast, LH is slower and vice versa.
  Tick primary_dur = is_slow ? kEighthNote : kQuarterNote;
  Tick secondary_dur = is_slow ? kSixteenthNote : kEighthNote;

  // Offset start by half a beat for rhythmic independence.
  Tick offset = kEighthNote;
  if (current_tick + offset < event.tick + event_duration) {
    // Add a rest at the beginning (skip to offset position).
    current_tick += offset;
  }

  // Start in upper portion of the range for register separation from pedal.
  size_t tone_idx = scale_tones.size() * 2 / 3;

  // Opposite direction from typical RH pattern for contour independence.
  bool ascending = rng::rollProbability(rng, 0.4f);

  while (current_tick < event.tick + event_duration) {
    Tick dur = rng::rollProbability(rng, 0.55f) ? primary_dur : secondary_dur;
    Tick remaining = (event.tick + event_duration) - current_tick;
    if (dur > remaining) dur = remaining;
    if (dur == 0) break;

    NoteEvent note;
    note.start_tick = current_tick;
    note.duration = dur;
    note.pitch = scale_tones[tone_idx];
    note.velocity = kOrganVelocity;
    note.voice = 1;
    notes.push_back(note);

    current_tick += dur;

    // Step through tones, favoring stepwise motion.
    int step = rng::rollProbability(rng, 0.25f) ? 2 : 1;

    if (ascending) {
      if (tone_idx + step < scale_tones.size()) {
        tone_idx += step;
      } else {
        ascending = false;
        if (tone_idx >= static_cast<size_t>(step)) {
          tone_idx -= step;
        } else {
          tone_idx = 0;
        }
      }
    } else {
      if (tone_idx >= static_cast<size_t>(step)) {
        tone_idx -= step;
      } else {
        ascending = true;
        if (tone_idx + step < scale_tones.size()) {
          tone_idx += step;
        } else if (!scale_tones.empty()) {
          tone_idx = scale_tones.size() - 1;
        }
      }
    }

    // Change direction occasionally.
    if (rng::rollProbability(rng, 0.25f)) {
      ascending = !ascending;
    }
  }

  return notes;
}

// ---------------------------------------------------------------------------
// Voice generation: Pedal (Voice 2, Pedal)
// ---------------------------------------------------------------------------

/// @brief Generate pedal bass line for one harmonic event span.
///
/// Creates a bass foundation using chord roots and fifths. Uses half and
/// quarter notes for stability, with occasional walking bass patterns.
///
/// @param event Current harmonic event.
/// @param is_slow True for slow movement (even longer note values).
/// @param rng Random number generator.
/// @return Vector of NoteEvent entries for the pedal.
std::vector<NoteEvent> generatePedalLine(const HarmonicEvent& event, bool is_slow,
                                         std::mt19937& rng) {
  std::vector<NoteEvent> notes;

  Tick event_duration = event.end_tick - event.tick;
  Tick current_tick = event.tick;

  // Pedal typically uses half notes; slow movements may use whole notes.
  Tick primary_dur = is_slow ? kHalfNote : kQuarterNote;
  Tick secondary_dur = is_slow ? kQuarterNote : kHalfNote;

  // Get bass pitch clamped to pedal range.
  uint8_t bass = clampPitch(static_cast<int>(event.bass_pitch),
                            organ_range::kPedalLow, organ_range::kPedalHigh);

  // Also compute the fifth above the root within pedal range.
  int fifth_pitch = static_cast<int>(bass) + interval::kPerfect5th;
  uint8_t fifth = clampPitch(fifth_pitch, organ_range::kPedalLow, organ_range::kPedalHigh);

  // Alternate between root and fifth for harmonic motion.
  bool use_root = true;

  while (current_tick < event.tick + event_duration) {
    Tick dur = rng::rollProbability(rng, 0.6f) ? primary_dur : secondary_dur;
    Tick remaining = (event.tick + event_duration) - current_tick;
    if (dur > remaining) dur = remaining;
    if (dur == 0) break;

    NoteEvent note;
    note.start_tick = current_tick;
    note.duration = dur;
    note.pitch = use_root ? bass : fifth;
    note.velocity = kOrganVelocity;
    note.voice = 2;
    notes.push_back(note);

    current_tick += dur;
    use_root = !use_root;

    // Occasionally stay on the root for stability.
    if (rng::rollProbability(rng, 0.3f)) {
      use_root = true;
    }
  }

  return notes;
}

// ---------------------------------------------------------------------------
// Movement generation
// ---------------------------------------------------------------------------

/// @brief Generate a single trio sonata movement.
///
/// Creates 3 independent voices over a harmonic timeline. Each voice
/// occupies a distinct register and uses complementary rhythms.
///
/// @param key_sig Key signature for this movement.
/// @param num_bars Number of bars in this movement.
/// @param bpm Tempo for this movement.
/// @param seed Random seed for this movement.
/// @param is_slow True if this is the slow middle movement.
/// @return TrioSonataMovement with 3 tracks populated.
TrioSonataMovement generateMovement(const KeySignature& key_sig, Tick num_bars,
                                    uint16_t bpm, uint32_t seed, bool is_slow) {
  TrioSonataMovement movement;
  movement.bpm = bpm;
  movement.key = key_sig;

  Tick duration = num_bars * kTicksPerBar;
  movement.total_duration_ticks = duration;

  std::mt19937 rng(seed);

  // Create harmonic timeline at beat resolution.
  HarmonicTimeline timeline =
      HarmonicTimeline::createStandard(key_sig, duration, HarmonicResolution::Beat);

  // Create tracks.
  std::vector<Track> tracks = createTrioSonataTracks();

  // Generate notes for each harmonic event.
  const auto& events = timeline.events();
  for (const auto& event : events) {
    // Voice 0: Right hand melody.
    auto rh_notes = generateRightHand(event, is_slow, rng);
    for (auto& note : rh_notes) {
      tracks[0].notes.push_back(note);
    }

    // Voice 1: Left hand counter-melody.
    auto lh_notes = generateLeftHand(event, is_slow, rng);
    for (auto& note : lh_notes) {
      tracks[1].notes.push_back(note);
    }

    // Voice 2: Pedal bass line.
    auto pedal_notes = generatePedalLine(event, is_slow, rng);
    for (auto& note : pedal_notes) {
      tracks[2].notes.push_back(note);
    }
  }

  // Sort notes within each track by start_tick.
  for (auto& track : tracks) {
    std::sort(track.notes.begin(), track.notes.end(),
              [](const NoteEvent& lhs, const NoteEvent& rhs) {
                if (lhs.start_tick != rhs.start_tick) {
                  return lhs.start_tick < rhs.start_tick;
                }
                return lhs.pitch < rhs.pitch;
              });
  }

  movement.tracks = std::move(tracks);
  return movement;
}

}  // namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

TrioSonataResult generateTrioSonata(const TrioSonataConfig& config) {
  TrioSonataResult result;
  result.success = false;

  // Movement 1: Fast, home key.
  TrioSonataMovement mov1 = generateMovement(
      config.key, kFastMovementBars, config.bpm_fast, config.seed, false);

  // Movement 2: Slow, related key (relative major/minor).
  KeySignature slow_key = getRelative(config.key);
  TrioSonataMovement mov2 = generateMovement(
      slow_key, kSlowMovementBars, config.bpm_slow, config.seed + kMovement2SeedOffset, true);

  // Movement 3: Fast, home key.
  TrioSonataMovement mov3 = generateMovement(
      config.key, kFastMovementBars, config.bpm_fast, config.seed + kMovement3SeedOffset, false);

  result.movements.push_back(std::move(mov1));
  result.movements.push_back(std::move(mov2));
  result.movements.push_back(std::move(mov3));
  result.success = true;

  return result;
}

}  // namespace bach
