// Implementation of HarmonicTimeline -- variable-density harmonic event sequence.

#include "harmony/harmonic_timeline.h"

namespace bach {

// ---------------------------------------------------------------------------
// Static default event
// ---------------------------------------------------------------------------

const HarmonicEvent HarmonicTimeline::kDefaultEvent = {
    0,            // tick
    0,            // end_tick
    Key::C,       // key
    false,        // is_minor
    {ChordDegree::I, ChordQuality::Major, 60, 0},  // chord: C major root position
    48,           // bass_pitch: C3
    1.0f,         // weight
    false         // is_immutable
};

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

HarmonicTimeline::HarmonicTimeline() = default;

// ---------------------------------------------------------------------------
// Event management
// ---------------------------------------------------------------------------

void HarmonicTimeline::addEvent(const HarmonicEvent& event) {
  events_.push_back(event);
}

const std::vector<HarmonicEvent>& HarmonicTimeline::events() const {
  return events_;
}

Tick HarmonicTimeline::totalDuration() const {
  if (events_.empty()) {
    return 0;
  }
  return events_.back().end_tick;
}

size_t HarmonicTimeline::size() const {
  return events_.size();
}

// ---------------------------------------------------------------------------
// Lookup
// ---------------------------------------------------------------------------

int HarmonicTimeline::findEventIndex(Tick tick) const {
  // Linear scan backward from the end. For typical timeline sizes (tens to
  // low hundreds of events), this is efficient and avoids binary search
  // complexity for edge cases (overlapping end_tick boundaries).
  int last_valid = -1;
  for (int idx = 0; idx < static_cast<int>(events_.size()); ++idx) {
    if (events_[idx].tick <= tick) {
      last_valid = idx;
    } else {
      break;  // Events are in chronological order.
    }
  }
  return last_valid;
}

const HarmonicEvent& HarmonicTimeline::getAt(Tick tick) const {
  int idx = findEventIndex(tick);
  if (idx < 0) {
    return kDefaultEvent;
  }
  return events_[idx];
}

Key HarmonicTimeline::getKeyAt(Tick tick) const {
  return getAt(tick).key;
}

const Chord& HarmonicTimeline::getChordAt(Tick tick) const {
  return getAt(tick).chord;
}

bool HarmonicTimeline::isKeyChange(Tick tick) const {
  int idx = findEventIndex(tick);
  if (idx < 0) {
    return false;
  }

  // Only a key change if this event starts exactly at this tick.
  if (events_[idx].tick != tick) {
    return false;
  }

  // First event is a key change if it differs from default (C major).
  if (idx == 0) {
    return events_[0].key != Key::C || events_[0].is_minor;
  }

  // Key change if current event's key differs from the previous event's key.
  return events_[idx].key != events_[idx - 1].key ||
         events_[idx].is_minor != events_[idx - 1].is_minor;
}

// ---------------------------------------------------------------------------
// Standard progression generation
// ---------------------------------------------------------------------------

/// @brief Build a chord for a given degree in a key.
/// @param key_sig Key signature (tonic + mode).
/// @param degree Chord degree to build.
/// @param octave Octave for the root pitch.
/// @return Chord with quality and root pitch populated.
static Chord buildChord(const KeySignature& key_sig, ChordDegree degree, int octave) {
  Chord chord;
  chord.degree = degree;
  chord.quality = key_sig.is_minor ? minorKeyQuality(degree)
                                   : majorKeyQuality(degree);

  uint8_t semitone_offset = key_sig.is_minor ? degreeMinorSemitones(degree)
                                             : degreeSemitones(degree);
  int root_midi = (octave + 1) * 12 + static_cast<int>(key_sig.tonic) + semitone_offset;
  chord.root_pitch = static_cast<uint8_t>(root_midi > 127 ? 127 : root_midi);
  chord.inversion = 0;

  return chord;
}

/// @brief Calculate bass pitch for a chord.
/// @param chord The chord.
/// @param bass_octave Octave for the bass note (typically 2 or 3).
/// @return MIDI pitch for the bass.
static uint8_t computeBassPitch(const Chord& chord, int bass_octave) {
  // Root position: bass = root in bass octave.
  int root_pc = static_cast<int>(chord.root_pitch) % 12;
  int bass_midi = (bass_octave + 1) * 12 + root_pc;
  if (bass_midi > 127) bass_midi = 127;
  if (bass_midi < 0) bass_midi = 0;
  return static_cast<uint8_t>(bass_midi);
}

HarmonicTimeline HarmonicTimeline::createStandard(const KeySignature& key_sig,
                                                   Tick duration,
                                                   HarmonicResolution resolution) {
  HarmonicTimeline timeline;

  if (duration == 0) {
    return timeline;
  }

  // Determine event duration based on resolution.
  Tick event_length = 0;
  switch (resolution) {
    case HarmonicResolution::Beat:
      event_length = kTicksPerBeat;
      break;
    case HarmonicResolution::Bar:
      event_length = kTicksPerBar;
      break;
    case HarmonicResolution::Section:
      // Section resolution: divide into 4 sections for the I-IV-V-I progression.
      event_length = duration / 4;
      if (event_length == 0) event_length = duration;
      break;
  }

  // Standard progression: I - IV - V - I
  // This is the fundamental cadential skeleton used throughout Bach.
  constexpr int kProgressionLength = 4;
  constexpr ChordDegree kProgression[kProgressionLength] = {
      ChordDegree::I, ChordDegree::IV, ChordDegree::V, ChordDegree::I};

  // Metric weight pattern: strong - weak - strong - strong (cadential).
  constexpr float kWeights[kProgressionLength] = {1.0f, 0.5f, 0.75f, 1.0f};

  constexpr int kChordOctave = 4;
  constexpr int kBassOctave = 2;

  Tick current_tick = 0;
  int progression_idx = 0;

  while (current_tick < duration) {
    ChordDegree degree = kProgression[progression_idx % kProgressionLength];
    float weight = kWeights[progression_idx % kProgressionLength];

    Chord chord = buildChord(key_sig, degree, kChordOctave);
    uint8_t bass = computeBassPitch(chord, kBassOctave);

    Tick event_end = current_tick + event_length;
    if (event_end > duration) {
      event_end = duration;
    }

    HarmonicEvent event;
    event.tick = current_tick;
    event.end_tick = event_end;
    event.key = key_sig.tonic;
    event.is_minor = key_sig.is_minor;
    event.chord = chord;
    event.bass_pitch = bass;
    event.weight = weight;
    event.is_immutable = false;

    timeline.addEvent(event);

    current_tick = event_end;
    ++progression_idx;
  }

  return timeline;
}

}  // namespace bach
