// Implementation of the organ toccata free section generator (BWV 565 style).

#include "forms/toccata.h"

#include <algorithm>
#include <random>
#include <vector>

#include "core/gm_program.h"
#include "core/pitch_utils.h"
#include "core/rng_util.h"
#include "core/scale.h"
#include "harmony/chord_types.h"
#include "harmony/harmonic_event.h"

namespace bach {

namespace {

/// @brief Organ velocity (pipe organs have no velocity sensitivity).
constexpr uint8_t kOrganVelocity = 80;

/// @brief Duration of a 16th note in ticks.
constexpr Tick kSixteenthNote = kTicksPerBeat / 4;  // 120

/// @brief Duration of an 8th note in ticks.
constexpr Tick kEighthNote = kTicksPerBeat / 2;  // 240

/// @brief Duration of a quarter note in ticks.
constexpr Tick kQuarterNote = kTicksPerBeat;  // 480

/// @brief Duration of a half note in ticks.
constexpr Tick kHalfNote = kTicksPerBeat * 2;  // 960

/// @brief Duration of a whole note in ticks.
constexpr Tick kWholeNote = kTicksPerBeat * 4;  // 1920

/// @brief MIDI channel for Manual I (Great).
constexpr uint8_t kGreatChannel = 0;

/// @brief MIDI channel for Manual II (Swell).
constexpr uint8_t kSwellChannel = 1;

/// @brief MIDI channel for Pedal.
constexpr uint8_t kPedalChannel = 3;

/// @brief Minimum allowed voice count for toccata generation.
constexpr uint8_t kMinVoices = 2;

/// @brief Maximum allowed voice count for toccata generation.
constexpr uint8_t kMaxVoices = 5;

/// @brief Proportion of total bars allocated to the opening gesture.
constexpr float kOpeningProportion = 0.25f;

/// @brief Proportion of total bars allocated to the recitative section.
constexpr float kRecitativeProportion = 0.50f;

// ---------------------------------------------------------------------------
// Scale tone extraction
// ---------------------------------------------------------------------------

/// @brief Get scale tones within a pitch range for the given key context.
///
/// Collects all MIDI pitches in [low_pitch, high_pitch] that belong to the
/// scale determined by the key and mode.
///
/// @param key Musical key (tonic pitch class).
/// @param is_minor True for minor mode (uses harmonic minor), false for major.
/// @param low_pitch Lowest MIDI pitch to include.
/// @param high_pitch Highest MIDI pitch to include.
/// @return Vector of scale-member MIDI pitches in ascending order.
std::vector<uint8_t> getScaleTones(Key key, bool is_minor, uint8_t low_pitch,
                                   uint8_t high_pitch) {
  std::vector<uint8_t> tones;
  ScaleType scale_type = is_minor ? ScaleType::HarmonicMinor : ScaleType::Major;

  for (int pitch = static_cast<int>(low_pitch);
       pitch <= static_cast<int>(high_pitch); ++pitch) {
    if (scale_util::isScaleTone(static_cast<uint8_t>(pitch), key, scale_type)) {
      tones.push_back(static_cast<uint8_t>(pitch));
    }
  }

  return tones;
}

/// @brief Get chord tones as MIDI pitches for a given chord and base octave.
///
/// Returns root, third, and fifth of the chord in the specified octave,
/// adjusting intervals for chord quality.
///
/// @param chord The chord to extract tones from.
/// @param octave Base octave for pitch calculation.
/// @return Vector of 3 MIDI pitch values (root, third, fifth).
std::vector<uint8_t> getChordTones(const Chord& chord, int octave) {
  std::vector<uint8_t> tones;
  tones.reserve(3);

  int root = (octave + 1) * 12 + (static_cast<int>(chord.root_pitch) % 12);

  int third_offset = 4;  // Major third default.
  if (chord.quality == ChordQuality::Minor ||
      chord.quality == ChordQuality::Diminished ||
      chord.quality == ChordQuality::Minor7) {
    third_offset = 3;  // Minor third.
  }

  int fifth_offset = 7;  // Perfect fifth default.
  if (chord.quality == ChordQuality::Diminished) {
    fifth_offset = 6;  // Diminished fifth.
  } else if (chord.quality == ChordQuality::Augmented) {
    fifth_offset = 8;  // Augmented fifth.
  }

  auto clamp_midi = [](int pitch) -> uint8_t {
    if (pitch < 0) return 0;
    if (pitch > 127) return 127;
    return static_cast<uint8_t>(pitch);
  };

  tones.push_back(clamp_midi(root));
  tones.push_back(clamp_midi(root + third_offset));
  tones.push_back(clamp_midi(root + fifth_offset));

  return tones;
}

// ---------------------------------------------------------------------------
// Track creation
// ---------------------------------------------------------------------------

/// @brief Create MIDI tracks for a toccata.
///
/// Channel/program mapping per the organ system specification:
///   Voice 0 -> Ch 0, Church Organ (Manual I / Great)
///   Voice 1 -> Ch 1, Reed Organ   (Manual II / Swell)
///   Voice 2 -> Ch 3, Church Organ (Pedal)
///
/// For 4+ voices, additional manual tracks are created on Ch 2.
///
/// @param num_voices Number of voices (2-5, clamped).
/// @return Vector of Track objects with channel/program/name configured.
std::vector<Track> createToccataTracks(uint8_t num_voices) {
  std::vector<Track> tracks;
  tracks.reserve(num_voices);

  struct TrackSpec {
    uint8_t channel;
    uint8_t program;
    const char* name;
  };

  // Toccata mapping: voice 0=Great, voice 1=Swell, voice 2=Pedal.
  // Additional voices map to Manual III, then Manual IV.
  static constexpr TrackSpec kSpecs[] = {
      {kGreatChannel, GmProgram::kChurchOrgan, "Manual I (Great)"},
      {kSwellChannel, GmProgram::kReedOrgan, "Manual II (Swell)"},
      {kPedalChannel, GmProgram::kChurchOrgan, "Pedal"},
      {2, GmProgram::kChurchOrgan, "Manual III (Positiv)"},
      {4, GmProgram::kChurchOrgan, "Manual IV"},
  };

  for (uint8_t idx = 0; idx < num_voices && idx < 5; ++idx) {
    Track track;
    track.channel = kSpecs[idx].channel;
    track.program = kSpecs[idx].program;
    track.name = kSpecs[idx].name;
    tracks.push_back(track);
  }

  return tracks;
}

// ---------------------------------------------------------------------------
// Pitch range helpers
// ---------------------------------------------------------------------------

/// @brief Get the low pitch bound for a toccata voice index.
///
/// Voice 0 = Manual I (Great), Voice 1 = Manual II (Swell),
/// Voice 2 = Pedal, Voice 3+ = Manual III.
///
/// @param voice_idx Voice index (0-based).
/// @return Low MIDI pitch bound.
uint8_t getToccataLowPitch(uint8_t voice_idx) {
  switch (voice_idx) {
    case 0: return organ_range::kManual1Low;   // 36
    case 1: return organ_range::kManual2Low;   // 36
    case 2: return organ_range::kPedalLow;     // 24
    case 3: return organ_range::kManual3Low;   // 48
    default: return organ_range::kManual1Low;
  }
}

/// @brief Get the high pitch bound for a toccata voice index.
/// @param voice_idx Voice index (0-based).
/// @return High MIDI pitch bound.
uint8_t getToccataHighPitch(uint8_t voice_idx) {
  switch (voice_idx) {
    case 0: return organ_range::kManual1High;  // 96
    case 1: return organ_range::kManual2High;  // 96
    case 2: return organ_range::kPedalHigh;    // 50
    case 3: return organ_range::kManual3High;  // 96
    default: return organ_range::kManual1High;
  }
}

// ---------------------------------------------------------------------------
// Opening gesture generation (25% of section)
// ---------------------------------------------------------------------------

/// @brief Generate the opening dramatic gesture with fast scale runs.
///
/// Creates rapid 16th-note scale passages on Manual I (voice 0), mimicking
/// the virtuosic opening of BWV 565. Patterns alternate between ascending
/// and descending runs with occasional arpeggio passages.
///
/// @param timeline Harmonic timeline for chord/key context.
/// @param key_sig Key signature for scale tone lookup.
/// @param start_tick Start tick of the opening section.
/// @param end_tick End tick of the opening section (exclusive).
/// @param rng Random number generator.
/// @return Vector of NoteEvents for the opening gesture on voice 0.
std::vector<NoteEvent> generateOpeningGesture(const HarmonicTimeline& timeline,
                                              const KeySignature& key_sig,
                                              Tick start_tick, Tick end_tick,
                                              std::mt19937& rng) {
  (void)timeline;  // Available for future chord-aware passage generation.
  std::vector<NoteEvent> notes;
  uint8_t low_pitch = getToccataLowPitch(0);
  uint8_t high_pitch = getToccataHighPitch(0);

  auto scale_tones = getScaleTones(key_sig.tonic, key_sig.is_minor, low_pitch, high_pitch);
  if (scale_tones.empty()) {
    return notes;
  }

  Tick current_tick = start_tick;
  bool ascending = true;

  // Start high in the range for dramatic effect (BWV 565 opens high).
  size_t tone_idx = scale_tones.size() * 3 / 4;

  while (current_tick < end_tick) {
    Tick remaining = end_tick - current_tick;
    Tick dur = kSixteenthNote;
    if (dur > remaining) dur = remaining;
    if (dur == 0) break;

    NoteEvent note;
    note.start_tick = current_tick;
    note.duration = dur;
    note.pitch = scale_tones[tone_idx];
    note.velocity = kOrganVelocity;
    note.voice = 0;
    note.source = BachNoteSource::FreeCounterpoint;
    notes.push_back(note);

    current_tick += dur;

    // Determine step size: mostly stepwise with occasional leaps.
    int step = rng::rollProbability(rng, 0.15f) ? 2 : 1;

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

    // Occasionally reverse direction for dramatic sweep.
    if (rng::rollProbability(rng, 0.12f)) {
      ascending = !ascending;
    }
  }

  return notes;
}

// ---------------------------------------------------------------------------
// Recitative / exploration section (50% of section)
// ---------------------------------------------------------------------------

/// @brief Generate the recitative/exploration section with freer passages.
///
/// Creates alternating 8th-note passages on Manual II (voice 1), with
/// dramatic pauses (rests) interspersed for rhetorical effect. The texture
/// is more exploratory and speech-like compared to the opening runs.
///
/// @param timeline Harmonic timeline for chord/key context.
/// @param key_sig Key signature for scale tone lookup.
/// @param start_tick Start tick of the recitative section.
/// @param end_tick End tick of the recitative section (exclusive).
/// @param rng Random number generator.
/// @return Vector of NoteEvents for the recitative on voice 1.
std::vector<NoteEvent> generateRecitative(const HarmonicTimeline& timeline,
                                          const KeySignature& key_sig,
                                          Tick start_tick, Tick end_tick,
                                          std::mt19937& rng) {
  (void)timeline;  // Available for future chord-aware passage generation.
  std::vector<NoteEvent> notes;
  uint8_t low_pitch = getToccataLowPitch(1);
  uint8_t high_pitch = getToccataHighPitch(1);

  auto scale_tones = getScaleTones(key_sig.tonic, key_sig.is_minor, low_pitch, high_pitch);
  if (scale_tones.empty()) {
    return notes;
  }

  Tick current_tick = start_tick;

  // Start in the middle of the range for the recitative voice.
  size_t tone_idx = scale_tones.size() / 2;
  bool ascending = rng::rollProbability(rng, 0.5f);

  while (current_tick < end_tick) {
    Tick remaining = end_tick - current_tick;

    // Insert dramatic pauses (~20% of the time).
    if (rng::rollProbability(rng, 0.20f)) {
      Tick pause_dur = kEighthNote;
      if (pause_dur > remaining) pause_dur = remaining;
      current_tick += pause_dur;
      continue;
    }

    // Alternate between 8th notes and occasional quarter notes.
    Tick dur = rng::rollProbability(rng, 0.25f) ? kQuarterNote : kEighthNote;
    if (dur > remaining) dur = remaining;
    if (dur == 0) break;

    NoteEvent note;
    note.start_tick = current_tick;
    note.duration = dur;
    note.pitch = scale_tones[tone_idx];
    note.velocity = kOrganVelocity;
    note.voice = 1;
    note.source = BachNoteSource::FreeCounterpoint;
    notes.push_back(note);

    current_tick += dur;

    // Move through scale tones with occasional leaps for recitative character.
    int step = rng::rollProbability(rng, 0.20f) ? rng::rollRange(rng, 2, 3) : 1;

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

    // Frequently reverse direction for rhetorical speech-like contour.
    if (rng::rollProbability(rng, 0.18f)) {
      ascending = !ascending;
    }
  }

  return notes;
}

// ---------------------------------------------------------------------------
// Drive to cadence (25% of section)
// ---------------------------------------------------------------------------

/// @brief Generate the drive-to-cadence section with building energy.
///
/// Creates 16th-note passages with increasing density across all active
/// voices. Voice 0 (Manual I) carries the primary rapid figuration,
/// voice 1 (Manual II) adds supporting figures, and additional voices
/// contribute energy buildup.
///
/// @param timeline Harmonic timeline for chord/key context.
/// @param key_sig Key signature for scale tone lookup.
/// @param num_voices Number of active voices.
/// @param start_tick Start tick of the cadential drive.
/// @param end_tick End tick of the cadential drive (exclusive).
/// @param rng Random number generator.
/// @return Vector of NoteEvents across multiple voices.
std::vector<NoteEvent> generateDriveToCadence(const HarmonicTimeline& timeline,
                                              const KeySignature& key_sig,
                                              uint8_t num_voices,
                                              Tick start_tick, Tick end_tick,
                                              std::mt19937& rng) {
  std::vector<NoteEvent> notes;

  // Voice 0 (Manual I): rapid 16th-note scale runs.
  {
    uint8_t low_pitch = getToccataLowPitch(0);
    uint8_t high_pitch = getToccataHighPitch(0);
    auto scale_tones = getScaleTones(key_sig.tonic, key_sig.is_minor, low_pitch, high_pitch);

    if (!scale_tones.empty()) {
      Tick current_tick = start_tick;
      size_t tone_idx = rng::rollRange(rng, 0,
                                       static_cast<int>(scale_tones.size()) / 3);
      bool ascending = true;

      while (current_tick < end_tick) {
        Tick remaining = end_tick - current_tick;
        Tick dur = kSixteenthNote;
        if (dur > remaining) dur = remaining;
        if (dur == 0) break;

        NoteEvent note;
        note.start_tick = current_tick;
        note.duration = dur;
        note.pitch = scale_tones[tone_idx];
        note.velocity = kOrganVelocity;
        note.voice = 0;
        note.source = BachNoteSource::FreeCounterpoint;
        notes.push_back(note);

        current_tick += dur;

        if (ascending) {
          if (tone_idx + 1 < scale_tones.size()) {
            ++tone_idx;
          } else {
            ascending = false;
            if (tone_idx > 0) --tone_idx;
          }
        } else {
          if (tone_idx > 0) {
            --tone_idx;
          } else {
            ascending = true;
            ++tone_idx;
          }
        }
      }
    }
  }

  // Voice 1 (Manual II): supporting 16th-note figures.
  if (num_voices >= 2) {
    uint8_t low_pitch = getToccataLowPitch(1);
    uint8_t high_pitch = getToccataHighPitch(1);
    auto scale_tones = getScaleTones(key_sig.tonic, key_sig.is_minor, low_pitch, high_pitch);

    if (!scale_tones.empty()) {
      Tick current_tick = start_tick;
      size_t tone_idx = scale_tones.size() / 2;
      bool ascending = false;  // Start descending for contrary motion.

      while (current_tick < end_tick) {
        Tick remaining = end_tick - current_tick;
        Tick dur = kSixteenthNote;
        if (dur > remaining) dur = remaining;
        if (dur == 0) break;

        NoteEvent note;
        note.start_tick = current_tick;
        note.duration = dur;
        note.pitch = scale_tones[tone_idx];
        note.velocity = kOrganVelocity;
        note.voice = 1;
        note.source = BachNoteSource::FreeCounterpoint;
        notes.push_back(note);

        current_tick += dur;

        int step = rng::rollProbability(rng, 0.10f) ? 2 : 1;
        if (ascending) {
          if (tone_idx + step < scale_tones.size()) {
            tone_idx += step;
          } else {
            ascending = false;
            if (tone_idx >= static_cast<size_t>(step)) tone_idx -= step;
          }
        } else {
          if (tone_idx >= static_cast<size_t>(step)) {
            tone_idx -= step;
          } else {
            ascending = true;
            tone_idx += step;
          }
        }
      }
    }
  }

  // Additional voices (3+): chord-tone support with quarter/half notes.
  for (uint8_t voice_idx = 3; voice_idx < num_voices && voice_idx < 5; ++voice_idx) {
    uint8_t low_pitch = getToccataLowPitch(voice_idx);
    uint8_t high_pitch = getToccataHighPitch(voice_idx);

    Tick current_tick = start_tick;
    while (current_tick < end_tick) {
      const HarmonicEvent& event = timeline.getAt(current_tick);
      auto chord_tones = getChordTones(event.chord, 4);

      std::vector<uint8_t> valid_tones;
      for (auto tone : chord_tones) {
        if (tone >= low_pitch && tone <= high_pitch) {
          valid_tones.push_back(tone);
        }
      }

      if (valid_tones.empty()) {
        valid_tones.push_back(clampPitch(static_cast<int>(event.bass_pitch),
                                         low_pitch, high_pitch));
      }

      Tick remaining = end_tick - current_tick;
      Tick dur = kQuarterNote;
      if (dur > remaining) dur = remaining;
      if (dur == 0) break;

      NoteEvent note;
      note.start_tick = current_tick;
      note.duration = dur;
      note.pitch = rng::selectRandom(rng, valid_tones);
      note.velocity = kOrganVelocity;
      note.voice = voice_idx;
      note.source = BachNoteSource::FreeCounterpoint;
      notes.push_back(note);

      current_tick += dur;
    }
  }

  return notes;
}

// ---------------------------------------------------------------------------
// Pedal point generation
// ---------------------------------------------------------------------------

/// @brief Generate sustained pedal point notes for the pedal voice.
///
/// Creates long held notes on the tonic and dominant in the pedal range,
/// providing harmonic foundation during the opening and cadential sections.
///
/// @param key_sig Key signature for determining tonic/dominant pitches.
/// @param voice_idx Pedal voice index (typically 2).
/// @param start_tick Start tick of the pedal section.
/// @param end_tick End tick of the pedal section (exclusive).
/// @param rng Random number generator.
/// @return Vector of NoteEvents for the pedal voice.
std::vector<NoteEvent> generatePedalPoint(const KeySignature& key_sig,
                                          uint8_t voice_idx,
                                          Tick start_tick, Tick end_tick,
                                          std::mt19937& rng) {
  std::vector<NoteEvent> notes;
  uint8_t low_pitch = getToccataLowPitch(voice_idx);
  uint8_t high_pitch = getToccataHighPitch(voice_idx);

  // Tonic pedal note in octave 2 (organ pedal register).
  int tonic_midi = static_cast<int>(key_sig.tonic) + 36;  // Octave 2.
  uint8_t tonic_pitch = clampPitch(tonic_midi, low_pitch, high_pitch);

  // Dominant (perfect 5th above tonic).
  int dominant_midi = tonic_midi + interval::kPerfect5th;
  uint8_t dominant_pitch = clampPitch(dominant_midi, low_pitch, high_pitch);

  Tick current_tick = start_tick;

  while (current_tick < end_tick) {
    Tick remaining = end_tick - current_tick;

    // Use whole notes or half notes for sustained pedal.
    Tick dur = rng::rollProbability(rng, 0.6f) ? kWholeNote : kHalfNote;
    if (dur > remaining) dur = remaining;
    if (dur == 0) break;

    // Alternate between tonic and dominant with tonic bias.
    uint8_t chosen_pitch = rng::rollProbability(rng, 0.70f) ? tonic_pitch : dominant_pitch;

    NoteEvent note;
    note.start_tick = current_tick;
    note.duration = dur;
    note.pitch = chosen_pitch;
    note.velocity = kOrganVelocity;
    note.voice = voice_idx;
    note.source = BachNoteSource::PedalPoint;
    notes.push_back(note);

    current_tick += dur;
  }

  return notes;
}

/// @brief Clamp voice count to valid range [2, 5].
/// @param num_voices Raw voice count from configuration.
/// @return Clamped voice count.
uint8_t clampToccataVoiceCount(uint8_t num_voices) {
  if (num_voices < kMinVoices) return kMinVoices;
  if (num_voices > kMaxVoices) return kMaxVoices;
  return num_voices;
}

/// @brief Sort notes in each track by start_tick for MIDI output.
/// @param tracks Tracks whose notes will be sorted in place.
void sortToccataTrackNotes(std::vector<Track>& tracks) {
  for (auto& track : tracks) {
    std::sort(track.notes.begin(), track.notes.end(),
              [](const NoteEvent& lhs, const NoteEvent& rhs) {
                if (lhs.start_tick != rhs.start_tick) {
                  return lhs.start_tick < rhs.start_tick;
                }
                return lhs.pitch < rhs.pitch;
              });
  }
}

}  // namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

ToccataResult generateToccata(const ToccataConfig& config) {
  ToccataResult result;
  result.success = false;

  // Step 1: Validate and clamp configuration.
  uint8_t num_voices = clampToccataVoiceCount(config.num_voices);

  if (config.section_bars <= 0) {
    result.error_message = "section_bars must be positive";
    return result;
  }

  std::mt19937 rng(config.seed);

  // Step 2: Calculate total duration and segment boundaries.
  Tick total_duration = static_cast<Tick>(config.section_bars) * kTicksPerBar;

  Tick opening_bars = static_cast<Tick>(
      static_cast<float>(config.section_bars) * kOpeningProportion);
  if (opening_bars < 1) opening_bars = 1;

  Tick recit_bars = static_cast<Tick>(
      static_cast<float>(config.section_bars) * kRecitativeProportion);
  if (recit_bars < 1) recit_bars = 1;

  // Drive gets the remainder to ensure exact total.
  Tick drive_bars = static_cast<Tick>(config.section_bars) - opening_bars - recit_bars;
  if (drive_bars < 1) {
    // For very short sections, steal from recitative.
    if (recit_bars > 1) {
      --recit_bars;
      ++drive_bars;
    } else {
      drive_bars = 1;
    }
  }

  Tick opening_start = 0;
  Tick opening_end = opening_bars * kTicksPerBar;
  Tick recit_start = opening_end;
  Tick recit_end = recit_start + recit_bars * kTicksPerBar;
  Tick drive_start = recit_end;
  Tick drive_end = total_duration;

  // Step 3: Create harmonic timeline with beat-level resolution.
  HarmonicTimeline timeline =
      HarmonicTimeline::createStandard(config.key, total_duration, HarmonicResolution::Beat);

  // Step 4: Generate notes for each section.
  std::vector<NoteEvent> all_notes;

  // Opening gesture: fast scale runs on voice 0 (Manual I).
  auto opening_notes = generateOpeningGesture(
      timeline, config.key, opening_start, opening_end, rng);
  all_notes.insert(all_notes.end(), opening_notes.begin(), opening_notes.end());

  // Recitative: freer passages on voice 1 (Manual II).
  auto recit_notes = generateRecitative(
      timeline, config.key, recit_start, recit_end, rng);
  all_notes.insert(all_notes.end(), recit_notes.begin(), recit_notes.end());

  // Drive to cadence: all voices active.
  auto drive_notes = generateDriveToCadence(
      timeline, config.key, num_voices, drive_start, drive_end, rng);
  all_notes.insert(all_notes.end(), drive_notes.begin(), drive_notes.end());

  // Step 5: Generate pedal points for opening and ending sections.
  if (num_voices >= 3) {
    // Pedal during opening.
    auto opening_pedal = generatePedalPoint(
        config.key, 2, opening_start, opening_end, rng);
    all_notes.insert(all_notes.end(), opening_pedal.begin(), opening_pedal.end());

    // Pedal during drive to cadence.
    auto drive_pedal = generatePedalPoint(
        config.key, 2, drive_start, drive_end, rng);
    all_notes.insert(all_notes.end(), drive_pedal.begin(), drive_pedal.end());
  }

  // Step 6: Create tracks and assign notes by voice_id.
  std::vector<Track> tracks = createToccataTracks(num_voices);
  for (const auto& note : all_notes) {
    if (note.voice < tracks.size()) {
      tracks[note.voice].notes.push_back(note);
    }
  }

  // Step 7: Sort notes within each track.
  sortToccataTrackNotes(tracks);

  result.tracks = std::move(tracks);
  result.timeline = std::move(timeline);
  result.total_duration_ticks = total_duration;
  result.opening_start = opening_start;
  result.opening_end = opening_end;
  result.recit_start = recit_start;
  result.recit_end = recit_end;
  result.drive_start = drive_start;
  result.drive_end = drive_end;
  result.success = true;

  return result;
}

}  // namespace bach
