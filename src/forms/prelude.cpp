// Implementation of the organ prelude generator.

#include "forms/prelude.h"

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

/// @brief Default prelude length in bars when fugue length is unknown.
constexpr Tick kDefaultPreludeBars = 12;

/// @brief Prelude-to-fugue length ratio (midpoint of 60-80% range).
constexpr float kPreludeFugueRatio = 0.70f;

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

// ---------------------------------------------------------------------------
// Chord tone extraction
// ---------------------------------------------------------------------------

/// @brief Get chord tones as MIDI pitches for a given chord and base octave.
///
/// Returns root, third, and fifth of the chord in the specified octave.
/// For Diminished quality the fifth is a tritone; for Augmented it is
/// an augmented fifth.
///
/// @param chord The chord to extract tones from.
/// @param octave Base octave for pitch calculation.
/// @return Vector of 3 MIDI pitch values (root, third, fifth).
std::vector<uint8_t> getChordTones(const Chord& chord, int octave) {
  std::vector<uint8_t> tones;
  tones.reserve(3);

  int root = (octave + 1) * 12 + (static_cast<int>(chord.root_pitch) % 12);

  // Determine third interval based on quality.
  int third_offset = 4;  // Major third default.
  if (chord.quality == ChordQuality::Minor ||
      chord.quality == ChordQuality::Diminished ||
      chord.quality == ChordQuality::Minor7) {
    third_offset = 3;  // Minor third.
  }

  // Determine fifth interval based on quality.
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

/// @brief Get scale tones within a range for the current key context.
///
/// Builds a set of MIDI pitches that belong to the scale of the given key,
/// spanning from low_pitch to high_pitch.
///
/// @param key Musical key (pitch class of tonic).
/// @param is_minor True for minor mode, false for major.
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

// ---------------------------------------------------------------------------
// Track creation (mirrors fugue_generator organ track setup)
// ---------------------------------------------------------------------------

/// @brief Create MIDI tracks for an organ prelude.
///
/// Channel/program mapping per the organ system spec:
///   Voice 0 -> Ch 0, Church Organ (Manual I / Great)
///   Voice 1 -> Ch 1, Reed Organ   (Manual II / Swell)
///   Voice 2 -> Ch 2, Church Organ (Manual III / Positiv)
///   Voice 3 -> Ch 3, Church Organ (Pedal)
///
/// @param num_voices Number of voices (2-5).
/// @return Vector of Track objects with channel/program/name configured.
std::vector<Track> createPreludeTracks(uint8_t num_voices) {
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
// Pattern generators
// ---------------------------------------------------------------------------

/// @brief Generate ascending/descending scale passage notes for one event span.
///
/// Creates a sequence of notes using scale tones, alternating between
/// ascending and descending patterns based on the pattern_index.
///
/// @param event Current harmonic event.
/// @param voice_idx Voice/track index.
/// @param note_duration Duration per note in ticks.
/// @param pattern_index Selects ascending (even) or descending (odd).
/// @param rng Random number generator for starting pitch selection.
/// @return Vector of NoteEvent entries.
std::vector<NoteEvent> generateScalePassage(const HarmonicEvent& event,
                                            uint8_t voice_idx,
                                            Tick note_duration,
                                            int pattern_index,
                                            std::mt19937& rng) {
  std::vector<NoteEvent> notes;
  uint8_t low_pitch = getVoiceLowPitch(voice_idx);
  uint8_t high_pitch = getVoiceHighPitch(voice_idx);

  auto scale_tones = getScaleTones(event.key, event.is_minor, low_pitch, high_pitch);
  if (scale_tones.empty()) {
    return notes;
  }

  Tick event_duration = event.end_tick - event.tick;
  Tick current_tick = event.tick;
  bool ascending = (pattern_index % 2 == 0);

  // Pick a starting position within the scale tone array.
  size_t start_idx;
  if (ascending) {
    start_idx = rng::rollRange(rng, 0, static_cast<int>(scale_tones.size()) / 3);
  } else {
    int high_start =
        static_cast<int>(scale_tones.size()) - 1;
    int low_start = static_cast<int>(scale_tones.size()) * 2 / 3;
    if (low_start > high_start) low_start = high_start;
    start_idx = static_cast<size_t>(rng::rollRange(rng, low_start, high_start));
  }

  size_t tone_idx = start_idx;

  while (current_tick < event.tick + event_duration) {
    Tick remaining = (event.tick + event_duration) - current_tick;
    Tick dur = (remaining < note_duration) ? remaining : note_duration;
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

    // Advance through scale tones.
    if (ascending) {
      if (tone_idx + 1 < scale_tones.size()) {
        ++tone_idx;
      } else {
        ascending = false;  // Reverse at top.
        if (tone_idx > 0) --tone_idx;
      }
    } else {
      if (tone_idx > 0) {
        --tone_idx;
      } else {
        ascending = true;  // Reverse at bottom.
        ++tone_idx;
      }
    }
  }

  return notes;
}

/// @brief Generate arpeggio pattern notes for one event span.
///
/// Creates notes that arpeggiate through chord tones, optionally including
/// passing tones between them.
///
/// @param event Current harmonic event.
/// @param voice_idx Voice/track index.
/// @param note_duration Duration per note in ticks.
/// @param rng Random number generator.
/// @return Vector of NoteEvent entries.
std::vector<NoteEvent> generateArpeggioPassage(const HarmonicEvent& event,
                                               uint8_t voice_idx,
                                               Tick note_duration,
                                               std::mt19937& rng) {
  std::vector<NoteEvent> notes;
  uint8_t low_pitch = getVoiceLowPitch(voice_idx);
  uint8_t high_pitch = getVoiceHighPitch(voice_idx);

  // Build arpeggio pitches spanning the voice range.
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

  // Collect all chord tones across octaves within the voice range.
  std::vector<uint8_t> arp_pitches;
  for (int octave = 1; octave <= 8; ++octave) {
    int base = octave * 12 + root_pc;
    int pitches[] = {base, base + third_offset, base + fifth_offset};
    for (int pitch : pitches) {
      if (pitch >= static_cast<int>(low_pitch) &&
          pitch <= static_cast<int>(high_pitch) && pitch <= 127) {
        arp_pitches.push_back(static_cast<uint8_t>(pitch));
      }
    }
  }

  if (arp_pitches.empty()) {
    return notes;
  }

  std::sort(arp_pitches.begin(), arp_pitches.end());

  Tick event_duration = event.end_tick - event.tick;
  Tick current_tick = event.tick;

  // Use rng to pick a random starting index within the arpeggio pitches.
  size_t arp_idx = static_cast<size_t>(
      rng::rollRange(rng, 0, static_cast<int>(arp_pitches.size()) - 1));
  bool going_up = rng::rollProbability(rng, 0.6f);

  while (current_tick < event.tick + event_duration) {
    Tick remaining = (event.tick + event_duration) - current_tick;
    Tick dur = (remaining < note_duration) ? remaining : note_duration;
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

  return notes;
}

/// @brief Generate a held chord tone for the middle voice.
///
/// Creates quarter or half note chord tones for a supporting voice.
///
/// @param event Current harmonic event.
/// @param voice_idx Voice/track index.
/// @param rng Random number generator.
/// @return Vector of NoteEvent entries.
std::vector<NoteEvent> generateMiddleVoice(const HarmonicEvent& event,
                                           uint8_t voice_idx,
                                           std::mt19937& rng) {
  std::vector<NoteEvent> notes;
  uint8_t low_pitch = getVoiceLowPitch(voice_idx);
  uint8_t high_pitch = getVoiceHighPitch(voice_idx);

  // Octave for the middle voice: typically octave 3-4.
  int mid_octave = rng::rollRange(rng, 3, 4);
  auto chord_tones = getChordTones(event.chord, mid_octave);

  // Filter to valid range.
  std::vector<uint8_t> valid_tones;
  for (auto tone : chord_tones) {
    if (tone >= low_pitch && tone <= high_pitch) {
      valid_tones.push_back(tone);
    }
  }

  if (valid_tones.empty()) {
    // Fallback: use bass_pitch clamped to range.
    valid_tones.push_back(clampPitch(event.bass_pitch, low_pitch, high_pitch));
  }

  Tick event_duration = event.end_tick - event.tick;
  Tick current_tick = event.tick;

  // Alternate between quarter and half notes.
  bool use_half = rng::rollProbability(rng, 0.4f);

  while (current_tick < event.tick + event_duration) {
    Tick dur = use_half ? kHalfNote : kQuarterNote;
    Tick remaining = (event.tick + event_duration) - current_tick;
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
    use_half = !use_half;  // Alternate rhythm.
  }

  return notes;
}

/// @brief Generate sustained bass notes for the lowest voice.
///
/// Creates half or whole note bass tones, using the harmonic event's
/// bass pitch as the primary tone.
///
/// @param event Current harmonic event.
/// @param voice_idx Voice/track index.
/// @param rng Random number generator.
/// @return Vector of NoteEvent entries.
std::vector<NoteEvent> generateBassVoice(const HarmonicEvent& event,
                                         uint8_t voice_idx,
                                         std::mt19937& rng) {
  std::vector<NoteEvent> notes;
  uint8_t low_pitch = getVoiceLowPitch(voice_idx);
  uint8_t high_pitch = getVoiceHighPitch(voice_idx);

  Tick event_duration = event.end_tick - event.tick;
  Tick current_tick = event.tick;

  // Bass typically uses half or whole notes.
  bool use_whole = rng::rollProbability(rng, 0.5f);

  while (current_tick < event.tick + event_duration) {
    Tick dur = use_whole ? kWholeNote : kHalfNote;
    Tick remaining = (event.tick + event_duration) - current_tick;
    if (dur > remaining) dur = remaining;
    if (dur == 0) break;

    uint8_t bass = clampPitch(static_cast<int>(event.bass_pitch), low_pitch, high_pitch);

    NoteEvent note;
    note.start_tick = current_tick;
    note.duration = dur;
    note.pitch = bass;
    note.velocity = kOrganVelocity;
    note.voice = voice_idx;
    note.source = BachNoteSource::FreeCounterpoint;
    notes.push_back(note);

    current_tick += dur;
    use_whole = !use_whole;
  }

  return notes;
}

// ---------------------------------------------------------------------------
// FreeForm generation
// ---------------------------------------------------------------------------

/// @brief Generate all notes for a FreeForm prelude.
///
/// Voice 0 (top): Passage work - alternating between scale and arpeggio
/// patterns using 8th notes.
/// Voice 1 (middle): Slower movement - quarter and half notes, chord tones.
/// Voice 2 (bottom/pedal): Long notes - half/whole notes, bass notes.
/// Additional voices follow middle or bass patterns.
///
/// @param timeline Harmonic timeline providing chord context.
/// @param num_voices Number of organ voices.
/// @param rng Random number generator.
/// @return Vector of all generated NoteEvents.
std::vector<NoteEvent> generateFreeFormNotes(const HarmonicTimeline& timeline,
                                             uint8_t num_voices,
                                             std::mt19937& rng) {
  std::vector<NoteEvent> all_notes;
  const auto& events = timeline.events();

  for (size_t event_idx = 0; event_idx < events.size(); ++event_idx) {
    const auto& event = events[event_idx];
    int pattern_index = static_cast<int>(event_idx);

    // Voice 0: Passage work with 8th notes (scale and arpeggio patterns).
    if (num_voices >= 1) {
      std::vector<NoteEvent> voice0_notes;
      if (rng::rollProbability(rng, 0.5f)) {
        voice0_notes =
            generateScalePassage(event, 0, kEighthNote, pattern_index, rng);
      } else {
        voice0_notes = generateArpeggioPassage(event, 0, kEighthNote, rng);
      }
      all_notes.insert(all_notes.end(), voice0_notes.begin(), voice0_notes.end());
    }

    // Voice 1: Middle voice with quarter/half notes.
    if (num_voices >= 2) {
      auto voice1_notes = generateMiddleVoice(event, 1, rng);
      all_notes.insert(all_notes.end(), voice1_notes.begin(), voice1_notes.end());
    }

    // Voice 2 (or last voice): Bass voice with half/whole notes.
    uint8_t bass_voice = (num_voices >= 3) ? static_cast<uint8_t>(num_voices - 1) : 1;
    if (num_voices >= 3) {
      auto bass_notes = generateBassVoice(event, bass_voice, rng);
      all_notes.insert(all_notes.end(), bass_notes.begin(), bass_notes.end());
    }

    // Additional middle voices (for 4+ voices): use middle voice patterns.
    for (uint8_t voice_idx = 2; voice_idx < num_voices - 1 && voice_idx < 4; ++voice_idx) {
      auto extra_notes = generateMiddleVoice(event, voice_idx, rng);
      all_notes.insert(all_notes.end(), extra_notes.begin(), extra_notes.end());
    }
  }

  return all_notes;
}

// ---------------------------------------------------------------------------
// Perpetual motion generation
// ---------------------------------------------------------------------------

/// @brief Generate all notes for a Perpetual motion prelude.
///
/// Voice 0 (top): Continuous 16th notes arpeggiating through chord tones
/// and passing tones.
/// Voice 1 (middle): Quarter notes on chord tones.
/// Voice 2 (bottom/pedal): Half/whole notes on bass.
///
/// @param timeline Harmonic timeline providing chord context.
/// @param num_voices Number of organ voices.
/// @param rng Random number generator.
/// @return Vector of all generated NoteEvents.
std::vector<NoteEvent> generatePerpetualNotes(const HarmonicTimeline& timeline,
                                              uint8_t num_voices,
                                              std::mt19937& rng) {
  std::vector<NoteEvent> all_notes;
  const auto& events = timeline.events();

  for (size_t event_idx = 0; event_idx < events.size(); ++event_idx) {
    const auto& event = events[event_idx];

    // Voice 0: Continuous 16th notes (arpeggio pattern).
    if (num_voices >= 1) {
      auto voice0_notes = generateArpeggioPassage(event, 0, kSixteenthNote, rng);
      all_notes.insert(all_notes.end(), voice0_notes.begin(), voice0_notes.end());
    }

    // Voice 1: Quarter notes on chord tones.
    if (num_voices >= 2) {
      auto voice1_notes = generateMiddleVoice(event, 1, rng);
      all_notes.insert(all_notes.end(), voice1_notes.begin(), voice1_notes.end());
    }

    // Voice 2 (or last voice): Bass voice.
    uint8_t bass_voice = (num_voices >= 3) ? static_cast<uint8_t>(num_voices - 1) : 1;
    if (num_voices >= 3) {
      auto bass_notes = generateBassVoice(event, bass_voice, rng);
      all_notes.insert(all_notes.end(), bass_notes.begin(), bass_notes.end());
    }

    // Additional middle voices.
    for (uint8_t voice_idx = 2; voice_idx < num_voices - 1 && voice_idx < 4; ++voice_idx) {
      auto extra_notes = generateMiddleVoice(event, voice_idx, rng);
      all_notes.insert(all_notes.end(), extra_notes.begin(), extra_notes.end());
    }
  }

  return all_notes;
}

/// @brief Sort notes in each track by start_tick for MIDI output.
/// @param tracks Tracks whose notes will be sorted in place.
void sortPreludeTrackNotes(std::vector<Track>& tracks) {
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

/// @brief Clamp voice count to valid range [2, 5].
/// @param num_voices Raw voice count from configuration.
/// @return Clamped voice count.
uint8_t clampPreludeVoiceCount(uint8_t num_voices) {
  if (num_voices < 2) return 2;
  if (num_voices > 5) return 5;
  return num_voices;
}

}  // namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

Tick calculatePreludeLength(Tick fugue_length_ticks) {
  if (fugue_length_ticks > 0) {
    return static_cast<Tick>(static_cast<float>(fugue_length_ticks) * kPreludeFugueRatio);
  }
  return kDefaultPreludeBars * kTicksPerBar;
}

PreludeResult generatePrelude(const PreludeConfig& config) {
  PreludeResult result;
  result.success = false;

  uint8_t num_voices = clampPreludeVoiceCount(config.num_voices);
  std::mt19937 rng(config.seed);

  // Step 1: Calculate target duration.
  Tick target_duration = calculatePreludeLength(config.fugue_length_ticks);

  // Quantize to bar boundaries (round up to the nearest full bar).
  if (target_duration % kTicksPerBar != 0) {
    target_duration = ((target_duration / kTicksPerBar) + 1) * kTicksPerBar;
  }

  // Step 2: Create harmonic timeline with beat-level resolution.
  HarmonicTimeline timeline =
      HarmonicTimeline::createStandard(config.key, target_duration, HarmonicResolution::Beat);

  // Step 3: Generate notes based on PreludeType.
  std::vector<NoteEvent> all_notes;
  if (config.type == PreludeType::Perpetual) {
    all_notes = generatePerpetualNotes(timeline, num_voices, rng);
  } else {
    all_notes = generateFreeFormNotes(timeline, num_voices, rng);
  }

  // Step 4: Create tracks and assign notes by voice_id.
  std::vector<Track> tracks = createPreludeTracks(num_voices);
  for (const auto& note : all_notes) {
    if (note.voice < tracks.size()) {
      tracks[note.voice].notes.push_back(note);
    }
  }

  // Step 5: Sort notes within each track.
  sortPreludeTrackNotes(tracks);

  result.tracks = std::move(tracks);
  result.timeline = std::move(timeline);
  result.total_duration_ticks = target_duration;
  result.success = true;

  return result;
}

}  // namespace bach
