// Implementation of the organ passacaglia generator (BWV 582 style).

#include "forms/passacaglia.h"

#include <algorithm>
#include <random>
#include <vector>

#include "analysis/counterpoint_analyzer.h"
#include "core/gm_program.h"
#include "core/note_creator.h"
#include "core/pitch_utils.h"
#include "core/rng_util.h"
#include "core/scale.h"
#include "counterpoint/bach_rule_evaluator.h"
#include "counterpoint/collision_resolver.h"
#include "counterpoint/counterpoint_state.h"
#include "harmony/chord_types.h"
#include "harmony/harmonic_event.h"
#include "organ/organ_techniques.h"

namespace bach {

namespace {

using namespace duration;

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
  // Tightened voice ranges for minimal overlap (max ~16st soprano-alto,
  // ~7st alto-tenor) while ensuring climax headroom in soprano.
  switch (voice_idx) {
    case 0: return 60;                        // C4 — soprano
    case 1: return 55;                        // G3 — alto
    case 2: return 48;                        // C3 — tenor
    case 3: return organ_range::kPedalLow;    // 24 (C1) — bass (unchanged)
    default: return 60;
  }
}

/// @brief Get the organ manual high pitch for a voice index.
/// @param voice_idx Voice index (0-based).
/// @return High MIDI pitch bound for the manual.
uint8_t getVoiceHighPitch(uint8_t voice_idx) {
  // Tightened voice ranges — matches getVoiceLowPitch().
  switch (voice_idx) {
    case 0: return 88;                        // E6 — soprano
    case 1: return 76;                        // E5 — alto
    case 2: return 69;                        // A4 — tenor
    case 3: return organ_range::kPedalHigh;   // 50 (D3) — bass (unchanged)
    default: return 88;
  }
}

// ---------------------------------------------------------------------------
// Ground bass generation (Baroque template-based)
// ---------------------------------------------------------------------------

/// @brief 6 Baroque-style ground bass templates (scale degrees, 0=tonic, 7=octave).
///
/// Each template defines 8 bars of melodic motion. The last 2 bars are
/// overwritten by enforceCadentialTail() to guarantee V-I closure.
/// Templates are drawn from common Baroque passacaglia/chaconne patterns.
static constexpr int kGroundBassTemplates[][8] = {
    {0, 0, 2, 3, 4, 5, 6, 7},  // 0: Ascending scale (BWV 582-inspired)
    {7, 6, 5, 4, 3, 2, 1, 0},  // 1: Descending octave scale (lamento)
    {0, 1, 2, 3, 4, 3, 4, 0},  // 2: Arch with neighbor-tone (Buxtehude)
    {7, 6, 5, 4, 3, 2, 4, 0},  // 3: Descending with cadential leap (Handel)
    {0, 2, 4, 2, 0, 2, 4, 7},  // 4: Triadic outline (French Baroque)
    {0, 1, 2, 3, 4, 2, 1, 0},  // 5: Half-scale ascent with return
};
static constexpr int kNumGroundBassTemplates = 6;

/// @brief Enforce cadential closure on the last 2 notes of a ground bass.
///
/// bar[n-2] receives a dominant-area degree (4, 6, or 2, weighted toward 4).
/// bar[n-1] receives the tonic (degree 0).
///
/// @param degrees Mutable vector of scale degrees.
/// @param rng Random number generator for cadential pre-dominant selection.
void enforceCadentialTail(std::vector<int>& degrees, std::mt19937& rng) {
  if (degrees.size() < 2) return;
  size_t n = degrees.size();

  // Dominant-area candidate degrees for penultimate bar.
  static constexpr int kCandidates[] = {4, 5, 6, 3, 2};
  static constexpr int kNumCandidates = 5;

  int best = 4;  // Default: dominant.
  if (n >= 3) {
    int preceding = degrees[n - 3];

    // Find candidates within 2 steps of preceding (stepwise or 3rd leap max).
    int close[kNumCandidates];
    int close_count = 0;
    for (int i = 0; i < kNumCandidates; ++i) {
      if (std::abs(kCandidates[i] - preceding) <= 2) {
        close[close_count++] = kCandidates[i];
      }
    }
    if (close_count > 0) {
      best = close[rng::rollRange(rng, 0, close_count - 1)];
    }
  }

  degrees[n - 2] = best;
  degrees[n - 1] = 0;  // Tonic.
}

/// @brief Ensure same-pitch repetition only at bar 0-1 (opening emphasis).
///
/// If consecutive degrees match at any position other than index 1,
/// nudge the later degree up by 1 to break the repetition.
///
/// @param degrees Mutable vector of scale degrees.
void sanitizeConsecutivePitches(std::vector<int>& degrees) {
  for (size_t idx = 1; idx < degrees.size(); ++idx) {
    if (degrees[idx] == degrees[idx - 1] && idx != 1) {
      // Nudge in the direction of the prevailing melodic motion.
      int nudge = 1;  // Default: ascending.
      if (idx >= 2 && degrees[idx - 1] < degrees[idx - 2]) {
        nudge = -1;  // Descending contour: nudge downward.
      }
      degrees[idx] = degrees[idx] + nudge;
      // Normalize to [0, 7] range to avoid relying on degreeToPitch wrapping.
      if (degrees[idx] < 0) degrees[idx] += 7;
      if (degrees[idx] > 7) degrees[idx] -= 7;
    }
  }
}

/// @brief Select a ground bass template index with key-dependent weighting.
///
/// Minor keys favor lamento/descending patterns; major keys favor
/// arch/triadic patterns.
///
/// @param rng Random number generator.
/// @param is_minor True for minor key.
/// @return Template index in [0, kNumGroundBassTemplates).
int selectGroundBassTemplate(std::mt19937& rng, bool is_minor) {
  // Weights for each of the 6 templates (sum = 100).
  static constexpr int kMinorWeights[] = {25, 25, 15, 15, 10, 10};
  static constexpr int kMajorWeights[] = {15, 15, 20, 20, 15, 15};

  const int* weights = is_minor ? kMinorWeights : kMajorWeights;
  int roll = rng::rollRange(rng, 1, 100);
  int cumulative = 0;
  for (int idx = 0; idx < kNumGroundBassTemplates; ++idx) {
    cumulative += weights[idx];
    if (roll <= cumulative) return idx;
  }
  return 0;
}

/// @brief Build ground bass pitches using Baroque-style templates.
///
/// Selects a pattern template weighted by key mode, applies cadential
/// tail enforcement (V-I), sanitizes consecutive-pitch violations,
/// and converts scale degrees to MIDI pitches in pedal range.
///
/// @param key Key signature (tonic + mode).
/// @param num_notes Number of notes (1 per bar).
/// @param rng Random number generator.
/// @return Vector of MIDI pitches for the ground bass, within pedal range.
std::vector<uint8_t> buildGroundBassPitches(const KeySignature& key, int num_notes,
                                            std::mt19937& rng) {
  std::vector<uint8_t> pitches;
  if (num_notes <= 0) return pitches;
  pitches.reserve(static_cast<size_t>(num_notes));

  // Single note: just the tonic.
  if (num_notes == 1) {
    int tonic = static_cast<int>(tonicPitch(key.tonic, 2));
    pitches.push_back(clampPitch(tonic, organ_range::kPedalLow,
                                 organ_range::kPedalHigh));
    return pitches;
  }

  // Scale types: NaturalMinor for body, HarmonicMinor for cadential degree 6.
  ScaleType body_scale = key.is_minor ? ScaleType::NaturalMinor : ScaleType::Major;
  ScaleType cadence_scale = key.is_minor ? ScaleType::HarmonicMinor : ScaleType::Major;

  // Octave placement: ensure degree 7 (tonic + octave) fits in pedal range.
  int tonic_pitch = static_cast<int>(tonicPitch(key.tonic, 2));
  if (tonic_pitch + 12 > static_cast<int>(organ_range::kPedalHigh)) {
    tonic_pitch = static_cast<int>(tonicPitch(key.tonic, 1));
  }
  int key_offset = tonic_pitch % 12;
  int base_note = tonic_pitch - key_offset;

  // Select template and build degree sequence.
  int tmpl_idx = selectGroundBassTemplate(rng, key.is_minor);
  std::vector<int> degrees;
  degrees.reserve(static_cast<size_t>(num_notes));

  if (num_notes <= 8) {
    // Take the first num_notes degrees from the template.
    for (int idx = 0; idx < num_notes; ++idx) {
      degrees.push_back(kGroundBassTemplates[tmpl_idx][idx]);
    }
  } else {
    // For >8 bars: simple cyclic repetition of the full template.
    // Baroque passacaglia ground bass is a strict ostinato (exact repetition).
    for (int idx = 0; idx < num_notes; ++idx) {
      degrees.push_back(kGroundBassTemplates[tmpl_idx][idx % 8]);
    }
  }

  // Enforce cadential closure (last 2 bars: V-preparation -> I).
  enforceCadentialTail(degrees, rng);

  // Sanitize consecutive same-degree outside opening.
  sanitizeConsecutivePitches(degrees);

  // Convert degrees to MIDI pitches.
  for (size_t idx = 0; idx < degrees.size(); ++idx) {
    int degree = degrees[idx];
    // Use cadence_scale for the penultimate bar's degree 6 (leading tone).
    ScaleType scale = body_scale;
    if (idx == degrees.size() - 2 && degree == 6) {
      scale = cadence_scale;
    }
    int midi_pitch = degreeToPitch(degree, base_note, key_offset, scale);
    pitches.push_back(
        clampPitch(midi_pitch, organ_range::kPedalLow, organ_range::kPedalHigh));
  }

  // Smooth leaps in the body. Leave cadential tail (last 2 notes) unsmoothed:
  // leading-tone resolution (e.g. B->C in C minor, 11 semitones) is idiomatic.
  size_t smooth_end = pitches.size() > 2 ? pitches.size() - 2 : pitches.size();
  for (size_t idx = 1; idx < smooth_end; ++idx) {
    int interval = static_cast<int>(pitches[idx]) - static_cast<int>(pitches[idx - 1]);
    // Ground bass tolerates wider leaps than upper voices (structural role).
    // Threshold: major 6th (9 semitones). Perfect 5th and minor 6th are idiomatic.
    if (std::abs(interval) > 9) {
      int adjusted = static_cast<int>(pitches[idx]);
      if (interval > 0) {
        adjusted -= 12;  // Leap up too large: bring down an octave.
      } else {
        adjusted += 12;  // Leap down too large: bring up an octave.
      }
      pitches[idx] = clampPitch(adjusted, organ_range::kPedalLow,
                                organ_range::kPedalHigh);
    }
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

  Tick end_tick = start_tick + static_cast<Tick>(bars) * kTicksPerBar;
  Tick current_tick = start_tick;

  // Initial scale tones for starting position.
  const HarmonicEvent& first_event = timeline.getAt(start_tick);
  auto scale_tones = getScaleTones(first_event.key, first_event.is_minor,
                                   low_pitch, high_pitch);
  if (scale_tones.empty()) return notes;

  // Start roughly in the middle of the scale range.
  size_t tone_idx = scale_tones.size() / 2;
  bool ascending = rng::rollProbability(rng, 0.5f);

  while (current_tick < end_tick) {
    // Re-acquire scale tones per beat from timeline (harmony may change).
    const HarmonicEvent& event = timeline.getAt(current_tick);
    auto new_tones = getScaleTones(event.key, event.is_minor,
                                   low_pitch, high_pitch);
    if (new_tones.empty()) { current_tick += kEighthNote; continue; }

    // Re-map position if scale changed.
    if (new_tones != scale_tones) {
      uint8_t prev_pitch = scale_tones.empty() ? 0 : scale_tones[tone_idx];
      scale_tones = std::move(new_tones);
      if (!notes.empty()) {
        tone_idx = findClosestToneIndex(scale_tones, notes.back().pitch);
      } else {
        tone_idx = findClosestToneIndex(scale_tones, prev_pitch);
      }
    }

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

  Tick end_tick = start_tick + static_cast<Tick>(bars) * kTicksPerBar;
  Tick current_tick = start_tick;

  // Initial scale tones for starting position.
  const HarmonicEvent& first_event = timeline.getAt(start_tick);
  auto scale_tones = getScaleTones(first_event.key, first_event.is_minor,
                                   low_pitch, high_pitch);
  if (scale_tones.empty()) return notes;

  // Start in the upper portion of the range for intensity.
  size_t tone_idx = scale_tones.size() * 2 / 3;
  bool ascending = rng::rollProbability(rng, 0.5f);

  while (current_tick < end_tick) {
    // Re-acquire scale tones per beat from timeline (harmony may change).
    const HarmonicEvent& event = timeline.getAt(current_tick);
    auto new_tones = getScaleTones(event.key, event.is_minor,
                                   low_pitch, high_pitch);
    if (new_tones.empty()) { current_tick += kSixteenthNote; continue; }

    // Re-map position if scale changed.
    if (new_tones != scale_tones) {
      uint8_t prev_pitch = scale_tones.empty() ? 0 : scale_tones[tone_idx];
      scale_tones = std::move(new_tones);
      if (!notes.empty()) {
        tone_idx = findClosestToneIndex(scale_tones, notes.back().pitch);
      } else {
        tone_idx = findClosestToneIndex(scale_tones, prev_pitch);
      }
    }

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

/// Create a HarmonicTimeline derived from the ground bass pitches.
/// Each bass note maps to one bar of harmony, repeated for all variations.
/// @param ground_bass The immutable ground bass note sequence.
/// @param key Key signature.
/// @param num_variations Number of variations.
/// @return A HarmonicTimeline with bar-level resolution driven by the bass.
HarmonicTimeline createPassacagliaTimeline(
    const std::vector<NoteEvent>& ground_bass,
    const KeySignature& key,
    int num_variations) {
  HarmonicTimeline timeline;
  ScaleType scale_type = key.is_minor ? ScaleType::HarmonicMinor
                                      : ScaleType::Major;
  int bass_octave = 2;  // Bass pitch octave for chord construction.

  // Build one variation's worth of harmonic events from ground bass.
  std::vector<HarmonicEvent> var_template;
  var_template.reserve(ground_bass.size());

  for (const auto& bass_note : ground_bass) {
    int degree = 0;  // Default to tonic for non-diatonic pitches.
    scale_util::pitchToScaleDegree(bass_note.pitch, key.tonic, scale_type,
                                   degree);
    ChordDegree chord_degree = scaleDegreeToChordDegree(degree, key.is_minor);

    // Build chord from degree.
    Chord chord;
    chord.degree = chord_degree;
    chord.quality = key.is_minor ? minorKeyQuality(chord_degree)
                                 : majorKeyQuality(chord_degree);

    // Force V to Major quality in minor keys (harmonic minor convention).
    if (key.is_minor && chord_degree == ChordDegree::V) {
      chord.quality = ChordQuality::Major;
    }

    uint8_t semitone_offset = key.is_minor
                                  ? degreeMinorSemitones(chord_degree)
                                  : degreeSemitones(chord_degree);
    int root_midi = (bass_octave + 1) * 12 +
                    static_cast<int>(key.tonic) + semitone_offset;
    chord.root_pitch = static_cast<uint8_t>(
        root_midi > 127 ? 127 : (root_midi < 0 ? 0 : root_midi));

    HarmonicEvent event;
    event.tick = bass_note.start_tick;
    event.end_tick = bass_note.start_tick + bass_note.duration;
    event.key = key.tonic;
    event.is_minor = key.is_minor;
    event.chord = chord;
    event.bass_pitch = bass_note.pitch;
    event.weight = 1.0f;

    var_template.push_back(event);
  }

  // Replicate the template for each variation with time offset.
  Tick variation_duration = ground_bass.empty()
                                ? 0
                                : ground_bass.back().start_tick +
                                      ground_bass.back().duration;

  for (int var_idx = 0; var_idx < num_variations; ++var_idx) {
    Tick offset = static_cast<Tick>(var_idx) * variation_duration;
    for (const auto& tmpl : var_template) {
      HarmonicEvent event = tmpl;
      event.tick = tmpl.tick + offset;
      event.end_tick = tmpl.end_tick + offset;
      timeline.addEvent(event);
    }
  }

  return timeline;
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

  int num_notes = bars;  // 1 whole note per bar.
  auto pitches = buildGroundBassPitches(key, num_notes, rng);

  Tick current_tick = 0;
  for (int idx = 0; idx < num_notes; ++idx) {
    NoteEvent note;
    note.start_tick = current_tick;
    note.duration = kWholeNote;
    note.pitch = pitches[static_cast<size_t>(idx)];
    note.velocity = kOrganVelocity;
    // TODO: ground bass voice index should be injected, not hardcoded.
    note.voice = 3;  // Pedal voice.
    note.source = BachNoteSource::GroundBass;
    notes.push_back(note);

    current_tick += kWholeNote;
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

  // Step 2: Create bass-driven harmonic timeline (1 chord per bar, derived
  // from ground bass pitches via scale degree → chord degree mapping).
  HarmonicTimeline timeline = createPassacagliaTimeline(
      ground_bass, config.key, config.num_variations);

  // Step 3: Create tracks.
  std::vector<Track> tracks = createPassacagliaTracks(num_voices);

  // Step 4: For each variation, place ground bass and generate upper voices
  // through createBachNote() for vertical coordination.
  uint8_t pedal_track_idx = static_cast<uint8_t>(num_voices - 1);

  // Shared counterpoint infrastructure (one evaluator/resolver for all variations).
  BachRuleEvaluator cp_rules(num_voices);
  cp_rules.setFreeCounterpoint(true);  // Allow weak-beat non-harmonic tones.
  CollisionResolver cp_resolver;

  for (int var_idx = 0; var_idx < config.num_variations; ++var_idx) {
    Tick var_start = static_cast<Tick>(var_idx) * variation_duration;

    // Fresh CounterpointState per variation (resets voice interactions).
    CounterpointState cp_state;
    cp_state.setKey(config.key.tonic);
    for (uint8_t v = 0; v < num_voices; ++v) {
      cp_state.registerVoice(v, getVoiceLowPitch(v), getVoiceHighPitch(v));
    }

    // Place ground bass as immutable (register in cp_state for coordination).
    for (const auto& bass_note : ground_bass) {
      NoteEvent shifted_note = bass_note;
      shifted_note.start_tick = var_start + bass_note.start_tick;
      shifted_note.voice = pedal_track_idx;
      cp_state.addNote(pedal_track_idx, shifted_note);
      tracks[pedal_track_idx].notes.push_back(shifted_note);
    }

    // Generate upper voices through createBachNote for vertical coordination.
    for (uint8_t voice_idx = 0; voice_idx < num_voices - 1; ++voice_idx) {
      auto raw_notes = generateVariationNotes(
          var_idx, var_start, config.ground_bass_bars, voice_idx, timeline, rng);

      uint8_t prev_pitch = 0;
      for (const auto& note : raw_notes) {
        BachNoteOptions opts;
        opts.voice = voice_idx;
        opts.desired_pitch = note.pitch;
        opts.tick = note.start_tick;
        opts.duration = note.duration;
        opts.velocity = note.velocity;
        opts.source = note.source;
        if (prev_pitch > 0) {
          opts.prev_pitches[0] = prev_pitch;
          opts.prev_count = 1;
        }

        auto result_note = createBachNote(&cp_state, &cp_rules, &cp_resolver,
                                          opts);
        if (result_note.accepted) {
          tracks[voice_idx].notes.push_back(result_note.note);
          prev_pitch = result_note.final_pitch;
        }
      }
    }
    // cp_state is destroyed at scope end (variation boundary reset).
  }

  // Step 5: Post-validate through counterpoint engine.
  if (num_voices >= 2) {
    // Collect all notes from all tracks.
    std::vector<NoteEvent> all_notes;
    for (const auto& track : tracks) {
      all_notes.insert(all_notes.end(), track.notes.begin(), track.notes.end());
    }

    // Build voice ranges from the hierarchical range functions.
    std::vector<std::pair<uint8_t, uint8_t>> voice_ranges;
    for (uint8_t v = 0; v < num_voices; ++v) {
      voice_ranges.emplace_back(getVoiceLowPitch(v), getVoiceHighPitch(v));
    }

    PostValidateStats stats;
    auto validated = postValidateNotes(
        std::move(all_notes), num_voices, config.key, voice_ranges, &stats);

    // Redistribute validated notes back to tracks.
    for (auto& track : tracks) {
      track.notes.clear();
    }
    for (auto& note : validated) {
      if (note.voice < num_voices) {
        tracks[note.voice].notes.push_back(std::move(note));
      }
    }
  }

  // Step 6: Sort notes within each track.
  sortTrackNotes(tracks);

  // Step 7: Run pairwise counterpoint check and log violations as warnings.
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

  // ---------------------------------------------------------------------------
  // Shared organ techniques: Picardy, variation registration (no pedal point
  // since GroundBass already serves as pedal foundation)
  // ---------------------------------------------------------------------------

  // Picardy third (minor keys only, final variation).
  if (config.enable_picardy && config.key.is_minor) {
    for (auto& track : tracks) {
      applyPicardyToFinalChord(track.notes, config.key,
                               total_duration - kTicksPerBar);
    }
  }

  // Variation registration plan (gradual crescendo).
  Tick var_dur = static_cast<Tick>(config.ground_bass_bars) * kTicksPerBar;
  auto reg_plan = createVariationRegistrationPlan(
      config.num_variations, var_dur);
  applyExtendedRegistrationPlan(tracks, reg_plan);

  result.tracks = std::move(tracks);
  result.timeline = std::move(timeline);
  result.total_duration_ticks = total_duration;
  result.success = true;

  return result;
}

}  // namespace bach
