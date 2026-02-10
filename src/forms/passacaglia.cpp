// Implementation of the organ passacaglia generator (BWV 582 style).

#include "forms/passacaglia.h"

#include <algorithm>
#include <random>
#include <vector>

#include "analysis/counterpoint_analyzer.h"
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

// ---------------------------------------------------------------------------
// Track creation (organ channel mapping)
// ---------------------------------------------------------------------------

/// @brief Create MIDI tracks for an organ passacaglia.
///
/// Channel/program mapping per the organ system spec:
///   Voice 0 -> Ch 0, Church Organ (Manual I / Great)
///   Voice 1 -> Ch 1, Reed Organ   (Manual II / Swell)
///   Voice 2 -> Ch 2, Church Organ (Manual III / Positiv)
///   Voice 3 -> Ch 3, Church Organ (Pedal)
///
/// @param num_voices Number of voices (3-5).
/// @return Vector of Track objects with channel/program/name configured.
std::vector<Track> createPassacagliaTracks(uint8_t num_voices) {
  std::vector<Track> tracks;
  tracks.reserve(num_voices);

  struct TrackSpec {
    uint8_t channel;
    uint8_t program;
    const char* name;
  };

  static constexpr TrackSpec kSpecs[] = {
      {0, GmProgram::kChurchOrgan, "Manual I (Great)"},
      {1, GmProgram::kReedOrgan, "Manual II (Swell)"},
      {2, GmProgram::kChurchOrgan, "Manual III (Positiv)"},
      {3, GmProgram::kChurchOrgan, "Pedal"},
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

/// @brief Get the organ manual low pitch for a voice index.
/// @param voice_idx Voice index (0-based).
/// @return Low MIDI pitch bound for the manual.
uint8_t getVoiceLowPitch(uint8_t voice_idx) {
  switch (voice_idx) {
    case 0: return organ_range::kManual1Low;   // 36
    case 1: return organ_range::kManual2Low;   // 36
    case 2: return organ_range::kManual3Low;   // 48
    case 3: return organ_range::kPedalLow;     // 24
    default: return organ_range::kManual1Low;
  }
}

/// @brief Get the organ manual high pitch for a voice index.
/// @param voice_idx Voice index (0-based).
/// @return High MIDI pitch bound for the manual.
uint8_t getVoiceHighPitch(uint8_t voice_idx) {
  switch (voice_idx) {
    case 0: return organ_range::kManual1High;  // 96
    case 1: return organ_range::kManual2High;  // 96
    case 2: return organ_range::kManual3High;  // 96
    case 3: return organ_range::kPedalHigh;    // 50
    default: return organ_range::kManual1High;
  }
}

// ---------------------------------------------------------------------------
// Scale and chord tone utilities
// ---------------------------------------------------------------------------

/// @brief Get scale tones within a range for the given key context.
///
/// @param key Musical key (pitch class of tonic).
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
/// Returns root, third, and fifth of the chord in the specified octave.
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
// Ground bass generation (inner helper)
// ---------------------------------------------------------------------------

/// @brief Build the descending bass pitch sequence for the ground bass theme.
///
/// Creates a descending line from tonic to dominant in the specified key,
/// using harmonic minor for minor keys. The pattern follows BWV 582 style:
/// stepwise descent from root, ending with a V-I cadential approach.
///
/// @param key Key signature (tonic + mode).
/// @param num_notes Total number of notes needed (2 per bar).
/// @param rng Random number generator for minor embellishments.
/// @return Vector of MIDI pitches for the ground bass, clamped to pedal range.
std::vector<uint8_t> buildGroundBassPitches(const KeySignature& key, int num_notes,
                                            std::mt19937& rng) {
  std::vector<uint8_t> pitches;
  pitches.reserve(static_cast<size_t>(num_notes));

  ScaleType scale_type = key.is_minor ? ScaleType::HarmonicMinor : ScaleType::Major;

  // Build the descending line in pedal octave (octave 2, MIDI base ~36).
  // Start on the tonic, descend stepwise through scale degrees.
  int tonic_pitch = static_cast<int>(tonicPitch(key.tonic, 2));

  // Collect scale tones in the pedal range for the key.
  std::vector<uint8_t> pedal_scale = getScaleTones(
      key.tonic, key.is_minor, organ_range::kPedalLow, organ_range::kPedalHigh);

  if (pedal_scale.empty()) {
    // Fallback: just use the tonic clamped to pedal range.
    uint8_t clamped = clampPitch(tonic_pitch, organ_range::kPedalLow,
                                 organ_range::kPedalHigh);
    for (int idx = 0; idx < num_notes; ++idx) {
      pitches.push_back(clamped);
    }
    return pitches;
  }

  // Find the index of the tonic (or nearest) in the pedal scale.
  uint8_t clamped_tonic = clampPitch(tonic_pitch, organ_range::kPedalLow,
                                     organ_range::kPedalHigh);
  size_t tonic_idx = 0;
  int min_dist = 127;
  for (size_t idx = 0; idx < pedal_scale.size(); ++idx) {
    int dist = absoluteInterval(pedal_scale[idx], clamped_tonic);
    if (dist < min_dist) {
      min_dist = dist;
      tonic_idx = idx;
    }
  }

  // BWV 582-style descending ground bass pattern:
  // Notes proceed stepwise downward from tonic, then cadential V-I.
  // With 16 notes (8 bars x 2 per bar), the pattern is:
  //   Notes 0..N-3: descending stepwise from tonic
  //   Note N-2: dominant (scale degree 5)
  //   Note N-1: tonic (return home)
  //
  // For fewer notes we compress proportionally.

  // Find dominant pitch in pedal range.
  int dom_pitch = tonic_pitch + interval::kPerfect5th;
  // If dominant is above pedal range, go down an octave.
  if (dom_pitch > static_cast<int>(organ_range::kPedalHigh)) {
    dom_pitch -= interval::kOctave;
  }
  uint8_t dominant = clampPitch(dom_pitch, organ_range::kPedalLow,
                                organ_range::kPedalHigh);
  // Snap dominant to nearest scale tone.
  dominant = scale_util::nearestScaleTone(dominant, key.tonic, scale_type);

  int descent_notes = num_notes - 2;  // Reserve last 2 for V-I cadence.
  if (descent_notes < 1) descent_notes = 1;

  // Descend from tonic, stepping down through scale degrees.
  size_t current_idx = tonic_idx;
  for (int idx = 0; idx < descent_notes; ++idx) {
    pitches.push_back(pedal_scale[current_idx]);
    if (current_idx > 0) {
      --current_idx;
    } else {
      // At bottom of pedal range, hold or step up slightly.
      // Add slight variety: occasionally repeat lowest or step up.
      if (rng::rollProbability(rng, 0.3f) && current_idx + 1 < pedal_scale.size()) {
        current_idx = 1;
      }
      // Otherwise stay at bottom.
    }
  }

  // Cadential ending: V -> I.
  pitches.push_back(dominant);
  pitches.push_back(pedal_scale[tonic_idx]);

  // Ensure we have exactly num_notes. Pad or truncate if needed.
  while (static_cast<int>(pitches.size()) < num_notes) {
    pitches.push_back(pedal_scale[tonic_idx]);
  }
  if (static_cast<int>(pitches.size()) > num_notes) {
    pitches.resize(static_cast<size_t>(num_notes));
  }

  return pitches;
}

// ---------------------------------------------------------------------------
// Variation stage generators
// ---------------------------------------------------------------------------

/// @brief Generate quarter-note chord tones for the Establish stage (variations 0-2).
///
/// Creates simple quarter-note lines using chord tones from the harmonic
/// timeline, providing a stable harmonic foundation.
///
/// @param start_tick Starting tick of this variation.
/// @param bars Number of bars in one ground bass cycle.
/// @param voice_idx Voice/track index for this part.
/// @param timeline Harmonic timeline for chord context.
/// @param rng Random number generator.
/// @return Vector of NoteEvents.
std::vector<NoteEvent> generateEstablishVariation(Tick start_tick, int bars,
                                                  uint8_t voice_idx,
                                                  const HarmonicTimeline& timeline,
                                                  std::mt19937& rng) {
  std::vector<NoteEvent> notes;
  uint8_t low_pitch = getVoiceLowPitch(voice_idx);
  uint8_t high_pitch = getVoiceHighPitch(voice_idx);

  Tick end_tick = start_tick + static_cast<Tick>(bars) * kTicksPerBar;
  Tick current_tick = start_tick;

  // Choose a comfortable octave for this voice.
  int base_octave = (voice_idx <= 1) ? rng::rollRange(rng, 4, 5) : 3;

  while (current_tick < end_tick) {
    const HarmonicEvent& event = timeline.getAt(current_tick);
    auto chord_tones = getChordTones(event.chord, base_octave);

    // Filter to valid range.
    std::vector<uint8_t> valid_tones;
    for (auto tone : chord_tones) {
      if (tone >= low_pitch && tone <= high_pitch) {
        valid_tones.push_back(tone);
      }
    }
    if (valid_tones.empty()) {
      valid_tones.push_back(clampPitch(static_cast<int>(event.bass_pitch) + 12,
                                       low_pitch, high_pitch));
    }

    Tick dur = kQuarterNote;
    Tick remaining = end_tick - current_tick;
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

  return notes;
}

/// @brief Generate eighth-note scale passages for early Develop stage (variations 3-5).
///
/// Creates flowing eighth-note lines moving stepwise through scale tones,
/// providing melodic interest against the ground bass.
///
/// @param start_tick Starting tick of this variation.
/// @param bars Number of bars in one ground bass cycle.
/// @param voice_idx Voice/track index for this part.
/// @param timeline Harmonic timeline for chord context.
/// @param rng Random number generator.
/// @return Vector of NoteEvents.
std::vector<NoteEvent> generateDevelopEarlyVariation(Tick start_tick, int bars,
                                                     uint8_t voice_idx,
                                                     const HarmonicTimeline& timeline,
                                                     std::mt19937& rng) {
  std::vector<NoteEvent> notes;
  uint8_t low_pitch = getVoiceLowPitch(voice_idx);
  uint8_t high_pitch = getVoiceHighPitch(voice_idx);

  const HarmonicEvent& first_event = timeline.getAt(start_tick);
  auto scale_tones = getScaleTones(first_event.key, first_event.is_minor,
                                   low_pitch, high_pitch);
  if (scale_tones.empty()) return notes;

  Tick end_tick = start_tick + static_cast<Tick>(bars) * kTicksPerBar;
  Tick current_tick = start_tick;

  // Start roughly in the middle of the scale range.
  size_t tone_idx = scale_tones.size() / 2;
  bool ascending = rng::rollProbability(rng, 0.5f);

  while (current_tick < end_tick) {
    Tick dur = kEighthNote;
    Tick remaining = end_tick - current_tick;
    if (dur > remaining) dur = remaining;
    if (dur == 0) break;

    NoteEvent note;
    note.start_tick = current_tick;
    note.duration = dur;
    note.pitch = scale_tones[tone_idx];
    note.velocity = kOrganVelocity;
    note.voice = voice_idx;
    note.source = BachNoteSource::FreeCounterpoint;
    notes.push_back(note);

    current_tick += dur;

    // Stepwise motion with occasional direction change.
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

    // Occasional direction reversal for musical interest.
    if (rng::rollProbability(rng, 0.15f)) {
      ascending = !ascending;
    }
  }

  return notes;
}

/// @brief Generate eighth-note arpeggios for late Develop stage (variations 6-8).
///
/// Creates arpeggio patterns using chord tones spanning the voice range,
/// providing harmonic clarity with faster motion.
///
/// @param start_tick Starting tick of this variation.
/// @param bars Number of bars in one ground bass cycle.
/// @param voice_idx Voice/track index for this part.
/// @param timeline Harmonic timeline for chord context.
/// @param rng Random number generator.
/// @return Vector of NoteEvents.
std::vector<NoteEvent> generateDevelopLateVariation(Tick start_tick, int bars,
                                                    uint8_t voice_idx,
                                                    const HarmonicTimeline& timeline,
                                                    std::mt19937& rng) {
  std::vector<NoteEvent> notes;
  uint8_t low_pitch = getVoiceLowPitch(voice_idx);
  uint8_t high_pitch = getVoiceHighPitch(voice_idx);

  Tick end_tick = start_tick + static_cast<Tick>(bars) * kTicksPerBar;
  Tick current_tick = start_tick;

  while (current_tick < end_tick) {
    const HarmonicEvent& event = timeline.getAt(current_tick);

    // Build arpeggio pitches from chord tones across octaves.
    int root_pc = static_cast<int>(event.chord.root_pitch) % 12;
    int third_offset = 4;
    if (event.chord.quality == ChordQuality::Minor ||
        event.chord.quality == ChordQuality::Diminished ||
        event.chord.quality == ChordQuality::Minor7) {
      third_offset = 3;
    }
    int fifth_offset = 7;
    if (event.chord.quality == ChordQuality::Diminished) {
      fifth_offset = 6;
    }

    std::vector<uint8_t> arp_pitches;
    for (int octave = 1; octave <= 8; ++octave) {
      int base = octave * 12 + root_pc;
      int candidates[] = {base, base + third_offset, base + fifth_offset};
      for (int pitch : candidates) {
        if (pitch >= static_cast<int>(low_pitch) &&
            pitch <= static_cast<int>(high_pitch) && pitch <= 127) {
          arp_pitches.push_back(static_cast<uint8_t>(pitch));
        }
      }
    }

    if (arp_pitches.empty()) {
      current_tick += kEighthNote;
      continue;
    }

    std::sort(arp_pitches.begin(), arp_pitches.end());

    size_t arp_idx = static_cast<size_t>(
        rng::rollRange(rng, 0, static_cast<int>(arp_pitches.size()) - 1));
    bool going_up = rng::rollProbability(rng, 0.6f);

    // Fill one beat at a time to allow chord changes.
    Tick beat_end = current_tick + kTicksPerBeat;
    if (beat_end > end_tick) beat_end = end_tick;

    while (current_tick < beat_end) {
      Tick dur = kEighthNote;
      Tick remaining = beat_end - current_tick;
      if (dur > remaining) dur = remaining;
      if (dur == 0) break;

      NoteEvent note;
      note.start_tick = current_tick;
      note.duration = dur;
      note.pitch = arp_pitches[arp_idx];
      note.velocity = kOrganVelocity;
      note.voice = voice_idx;
      note.source = BachNoteSource::FreeCounterpoint;
      notes.push_back(note);

      current_tick += dur;

      // Zigzag through arpeggio pitches.
      if (going_up) {
        if (arp_idx + 1 < arp_pitches.size()) {
          ++arp_idx;
        } else {
          going_up = false;
          if (arp_idx > 0) --arp_idx;
        }
      } else {
        if (arp_idx > 0) {
          --arp_idx;
        } else {
          going_up = true;
          ++arp_idx;
        }
      }
    }
  }

  return notes;
}

/// @brief Generate sixteenth-note figurations for Accumulate/Resolve stage
///        (variations 9-11).
///
/// Creates rapid sixteenth-note passages mixing scale tones and chord tones,
/// providing the climactic intensity before the final resolution.
///
/// @param start_tick Starting tick of this variation.
/// @param bars Number of bars in one ground bass cycle.
/// @param voice_idx Voice/track index for this part.
/// @param timeline Harmonic timeline for chord context.
/// @param rng Random number generator.
/// @return Vector of NoteEvents.
std::vector<NoteEvent> generateAccumulateVariation(Tick start_tick, int bars,
                                                   uint8_t voice_idx,
                                                   const HarmonicTimeline& timeline,
                                                   std::mt19937& rng) {
  std::vector<NoteEvent> notes;
  uint8_t low_pitch = getVoiceLowPitch(voice_idx);
  uint8_t high_pitch = getVoiceHighPitch(voice_idx);

  const HarmonicEvent& first_event = timeline.getAt(start_tick);
  auto scale_tones = getScaleTones(first_event.key, first_event.is_minor,
                                   low_pitch, high_pitch);
  if (scale_tones.empty()) return notes;

  Tick end_tick = start_tick + static_cast<Tick>(bars) * kTicksPerBar;
  Tick current_tick = start_tick;

  // Start in the upper portion of the range for intensity.
  size_t tone_idx = scale_tones.size() * 2 / 3;
  bool ascending = rng::rollProbability(rng, 0.5f);

  while (current_tick < end_tick) {
    Tick dur = kSixteenthNote;
    Tick remaining = end_tick - current_tick;
    if (dur > remaining) dur = remaining;
    if (dur == 0) break;

    NoteEvent note;
    note.start_tick = current_tick;
    note.duration = dur;
    note.pitch = scale_tones[tone_idx];
    note.velocity = kOrganVelocity;
    note.voice = voice_idx;
    note.source = BachNoteSource::FreeCounterpoint;
    notes.push_back(note);

    current_tick += dur;

    // Rapid stepwise motion with occasional leaps.
    int step = rng::rollProbability(rng, 0.2f) ? 2 : 1;

    if (ascending) {
      if (tone_idx + static_cast<size_t>(step) < scale_tones.size()) {
        tone_idx += static_cast<size_t>(step);
      } else {
        ascending = false;
        if (tone_idx >= static_cast<size_t>(step)) {
          tone_idx -= static_cast<size_t>(step);
        } else {
          tone_idx = 0;
        }
      }
    } else {
      if (tone_idx >= static_cast<size_t>(step)) {
        tone_idx -= static_cast<size_t>(step);
      } else {
        ascending = true;
        if (tone_idx + static_cast<size_t>(step) < scale_tones.size()) {
          tone_idx += static_cast<size_t>(step);
        } else if (!scale_tones.empty()) {
          tone_idx = scale_tones.size() - 1;
        }
      }
    }

    // Frequent direction changes for figuration character.
    if (rng::rollProbability(rng, 0.2f)) {
      ascending = !ascending;
    }
  }

  return notes;
}

/// @brief Generate upper voice notes for a single variation at the appropriate
///        complexity stage.
///
/// Routes to one of four stage generators based on variation index:
///   - Variations 0-2:  Quarter note chord tones (Establish)
///   - Variations 3-5:  Eighth note scale passages (Develop early)
///   - Variations 6-8:  Eighth note arpeggios (Develop late)
///   - Variations 9+:   Sixteenth note figurations (Accumulate/Resolve)
///
/// @param variation_idx Zero-based variation index.
/// @param start_tick Starting tick of this variation.
/// @param bars Number of bars in one ground bass cycle.
/// @param voice_idx Voice/track index.
/// @param timeline Harmonic timeline.
/// @param rng Random number generator.
/// @return Vector of NoteEvents for this voice in this variation.
std::vector<NoteEvent> generateVariationNotes(int variation_idx, Tick start_tick,
                                              int bars, uint8_t voice_idx,
                                              const HarmonicTimeline& timeline,
                                              std::mt19937& rng) {
  if (variation_idx < 3) {
    return generateEstablishVariation(start_tick, bars, voice_idx, timeline, rng);
  } else if (variation_idx < 6) {
    return generateDevelopEarlyVariation(start_tick, bars, voice_idx, timeline, rng);
  } else if (variation_idx < 9) {
    return generateDevelopLateVariation(start_tick, bars, voice_idx, timeline, rng);
  } else {
    return generateAccumulateVariation(start_tick, bars, voice_idx, timeline, rng);
  }
}

/// @brief Sort notes in each track by start_tick, breaking ties by pitch.
/// @param tracks Tracks whose notes will be sorted in place.
void sortTrackNotes(std::vector<Track>& tracks) {
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

/// @brief Clamp voice count to valid range [3, 5].
/// @param num_voices Raw voice count from configuration.
/// @return Clamped voice count.
uint8_t clampVoiceCount(uint8_t num_voices) {
  if (num_voices < 3) return 3;
  if (num_voices > 5) return 5;
  return num_voices;
}

}  // namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

std::vector<NoteEvent> generatePassacagliaGroundBass(const KeySignature& key,
                                                     int bars, uint32_t seed) {
  std::vector<NoteEvent> notes;

  if (bars <= 0) return notes;

  std::mt19937 rng(seed);

  int num_notes = bars * 2;  // 2 half notes per bar.
  auto pitches = buildGroundBassPitches(key, num_notes, rng);

  Tick current_tick = 0;
  for (int idx = 0; idx < num_notes; ++idx) {
    NoteEvent note;
    note.start_tick = current_tick;
    note.duration = kHalfNote;
    note.pitch = pitches[static_cast<size_t>(idx)];
    note.velocity = kOrganVelocity;
    note.voice = 3;  // Pedal voice.
    note.source = BachNoteSource::GroundBass;
    notes.push_back(note);

    current_tick += kHalfNote;
  }

  return notes;
}

PassacagliaResult generatePassacaglia(const PassacagliaConfig& config) {
  PassacagliaResult result;
  result.success = false;

  // Validate configuration.
  if (config.num_variations <= 0 || config.ground_bass_bars <= 0) {
    result.error_message = "Invalid variation or bar count";
    return result;
  }

  uint8_t num_voices = clampVoiceCount(config.num_voices);
  std::mt19937 rng(config.seed);

  // Step 1: Generate immutable ground bass theme.
  std::vector<NoteEvent> ground_bass =
      generatePassacagliaGroundBass(config.key, config.ground_bass_bars, config.seed);

  if (ground_bass.empty()) {
    result.error_message = "Failed to generate ground bass";
    return result;
  }

  Tick variation_duration =
      static_cast<Tick>(config.ground_bass_bars) * kTicksPerBar;
  Tick total_duration =
      static_cast<Tick>(config.num_variations) * variation_duration;

  // Step 2: Create harmonic timeline spanning all variations.
  HarmonicTimeline timeline = HarmonicTimeline::createStandard(
      config.key, total_duration, HarmonicResolution::Beat);

  // Step 3: Create tracks.
  std::vector<Track> tracks = createPassacagliaTracks(num_voices);

  // Step 4: For each variation, place ground bass and generate upper voices.
  for (int var_idx = 0; var_idx < config.num_variations; ++var_idx) {
    Tick var_start = static_cast<Tick>(var_idx) * variation_duration;

    // Place ground bass (immutable, identical in every variation).
    // The pedal track is the last track (index num_voices - 1) which maps to
    // channel 3 for 4-voice setup, but we always use the track that has
    // channel 3 (Pedal). For standard 4-voice, that is track index 3.
    uint8_t pedal_track_idx = static_cast<uint8_t>(num_voices - 1);
    for (const auto& bass_note : ground_bass) {
      NoteEvent shifted_note = bass_note;
      shifted_note.start_tick = var_start + bass_note.start_tick;
      tracks[pedal_track_idx].notes.push_back(shifted_note);
    }

    // Generate upper voices (all tracks except pedal).
    for (uint8_t voice_idx = 0; voice_idx < num_voices - 1; ++voice_idx) {
      auto var_notes = generateVariationNotes(
          var_idx, var_start, config.ground_bass_bars, voice_idx, timeline, rng);
      for (auto& note : var_notes) {
        tracks[voice_idx].notes.push_back(note);
      }
    }
  }

  // Step 5: Sort notes within each track.
  sortTrackNotes(tracks);

  // Step 6: Run pairwise counterpoint check and log violations as warnings.
  if (num_voices >= 2) {
    std::vector<NoteEvent> all_notes;
    for (const auto& track : tracks) {
      all_notes.insert(all_notes.end(), track.notes.begin(), track.notes.end());
    }
    auto cp_result = analyzeCounterpoint(all_notes, num_voices);
    if (cp_result.parallel_perfect_count > 0 ||
        cp_result.voice_crossing_count > 0) {
      result.counterpoint_violations =
          cp_result.parallel_perfect_count + cp_result.voice_crossing_count;
    }
  }

  result.tracks = std::move(tracks);
  result.timeline = std::move(timeline);
  result.total_duration_ticks = total_duration;
  result.success = true;

  return result;
}

}  // namespace bach
