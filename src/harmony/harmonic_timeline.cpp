// Implementation of HarmonicTimeline -- variable-density harmonic event sequence.

#include "harmony/harmonic_timeline.h"

#include "core/pitch_utils.h"

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
    1.0f,         // rhythm_factor (normal speed)
    false,        // is_immutable
    Key::C,       // modulation_target (no modulation)
    false         // has_modulation
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

std::vector<HarmonicEvent>& HarmonicTimeline::mutableEvents() {
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
/// @param inversion Chord inversion (0=root, 1=first, 2=second).
/// @return Chord with quality and root pitch populated.
static Chord buildChord(const KeySignature& key_sig, ChordDegree degree, int octave,
                        uint8_t inversion = 0) {
  Chord chord;
  chord.degree = degree;
  chord.quality = key_sig.is_minor ? minorKeyQuality(degree)
                                   : majorKeyQuality(degree);

  uint8_t semitone_offset = key_sig.is_minor ? degreeMinorSemitones(degree)
                                             : degreeSemitones(degree);
  int root_midi = (octave + 1) * 12 + static_cast<int>(key_sig.tonic) + semitone_offset;
  chord.root_pitch = static_cast<uint8_t>(root_midi > 127 ? 127 : root_midi);
  chord.inversion = inversion;

  return chord;
}

/// @brief Build a chord with a specific quality override.
static Chord buildChordWithQuality(const KeySignature& key_sig, ChordDegree degree,
                                   ChordQuality quality, int octave,
                                   uint8_t inversion = 0) {
  Chord chord = buildChord(key_sig, degree, octave, inversion);
  chord.quality = quality;
  return chord;
}

/// @brief Get the third interval in semitones for a chord quality.
static int thirdInterval(ChordQuality quality) {
  switch (quality) {
    case ChordQuality::Major:
    case ChordQuality::Dominant7:
    case ChordQuality::MajorMajor7:
    case ChordQuality::Augmented:
    case ChordQuality::AugmentedSixth:
    case ChordQuality::AugSixthItalian:
    case ChordQuality::AugSixthFrench:
    case ChordQuality::AugSixthGerman:
      return 4;
    default:
      return 3;
  }
}

/// @brief Get the fifth interval in semitones for a chord quality.
static int fifthInterval(ChordQuality quality) {
  switch (quality) {
    case ChordQuality::Diminished:
    case ChordQuality::AugSixthItalian:
    case ChordQuality::AugSixthFrench:
      return 6;
    case ChordQuality::Augmented:
    case ChordQuality::AugmentedSixth:
    case ChordQuality::AugSixthGerman:
      return 8;
    default:
      return 7;
  }
}

/// @brief Calculate bass pitch for a chord, respecting inversions.
/// @param chord The chord (inversion field determines bass note).
/// @param bass_octave Octave for the bass note (typically 2 or 3).
/// @return MIDI pitch for the bass.
static uint8_t computeBassPitch(const Chord& chord, int bass_octave) {
  int root_pc = getPitchClass(chord.root_pitch);
  int bass_pc = root_pc;

  if (chord.inversion == 1) {
    bass_pc = (root_pc + thirdInterval(chord.quality)) % 12;
  } else if (chord.inversion == 2) {
    bass_pc = (root_pc + fifthInterval(chord.quality)) % 12;
  }

  int bass_midi = (bass_octave + 1) * 12 + bass_pc;
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

  // Standard progression: I - IV - V7 - I
  // This is the fundamental cadential skeleton used throughout Bach.
  // V chord uses Dominant7 quality for stronger cadential function.
  constexpr int kProgressionLength = 4;
  constexpr ChordDegree kProgression[kProgressionLength] = {
      ChordDegree::I, ChordDegree::IV, ChordDegree::V, ChordDegree::I};

  // Whether each chord should override to Dominant7 quality.
  constexpr bool kDom7Override[kProgressionLength] = {false, false, true, false};

  // Metric weight pattern: strong - weak - strong - strong (cadential).
  constexpr float kWeights[kProgressionLength] = {1.0f, 0.5f, 0.75f, 1.0f};

  constexpr int kChordOctave = 4;
  constexpr int kBassOctave = 2;

  Tick current_tick = 0;
  int progression_idx = 0;

  while (current_tick < duration) {
    ChordDegree degree = kProgression[progression_idx % kProgressionLength];
    float weight = kWeights[progression_idx % kProgressionLength];
    bool dom7 = kDom7Override[progression_idx % kProgressionLength];

    Chord chord = dom7 ? buildChordWithQuality(key_sig, degree, ChordQuality::Dominant7,
                                               kChordOctave)
                       : buildChord(key_sig, degree, kChordOctave);
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

// ---------------------------------------------------------------------------
// Progression templates
// ---------------------------------------------------------------------------

/// @brief Progression entry with degree, quality override, inversion, and weight.
struct ProgEntry {
  ChordDegree degree;
  ChordQuality quality_override;   // If Augmented, use default key quality
  uint8_t inversion;
  float weight;
  bool use_quality_override;
};

static const ProgEntry kCircleOfFifths[] = {
    {ChordDegree::I,   ChordQuality::Major, 0, 1.0f, false},
    {ChordDegree::vi,  ChordQuality::Minor, 1, 0.5f, false},  // 1st inversion for bass smoothness
    {ChordDegree::ii,  ChordQuality::Minor, 0, 0.5f, false},
    {ChordDegree::V,   ChordQuality::Dominant7, 0, 0.75f, true},
    {ChordDegree::I,   ChordQuality::Major, 0, 1.0f, false},
};

static const ProgEntry kSubdominant[] = {
    {ChordDegree::I,   ChordQuality::Major, 0, 1.0f, false},
    {ChordDegree::IV,  ChordQuality::Major, 0, 0.5f, false},
    {ChordDegree::ii,  ChordQuality::Minor, 1, 0.5f, false},  // 1st inversion for bass smoothness
    {ChordDegree::V,   ChordQuality::Dominant7, 0, 0.75f, true},
    {ChordDegree::I,   ChordQuality::Major, 0, 1.0f, false},
};

static const ProgEntry kChromaticCircle[] = {
    {ChordDegree::I,      ChordQuality::Major,    0, 1.0f,  false},
    {ChordDegree::V_of_vi, ChordQuality::Dominant7, 0, 0.5f,  true},
    {ChordDegree::vi,     ChordQuality::Minor,    1, 0.5f,  false},  // 1st inversion
    {ChordDegree::V_of_V, ChordQuality::Dominant7, 0, 0.75f, true},
    {ChordDegree::V,      ChordQuality::Major,    0, 0.75f, false},
    {ChordDegree::I,      ChordQuality::Major,    0, 1.0f,  false},
};

static const ProgEntry kBorrowedChord[] = {
    {ChordDegree::I,    ChordQuality::Major,    0, 1.0f,  false},
    {ChordDegree::bVI,  ChordQuality::Major,    0, 0.5f,  true},
    {ChordDegree::IV,   ChordQuality::Major,    0, 0.5f,  false},
    {ChordDegree::V,    ChordQuality::Dominant7, 0, 0.75f, true},
    {ChordDegree::I,    ChordQuality::Major,    0, 1.0f,  false},
};

/// Descending 5th sequence: I-IV-vii°-iii-vi-ii-V7-I.
/// A fundamental Baroque harmonic pattern found throughout Bach's works.
/// Inversions on vii°, iii, and ii create a smooth descending bass line.
static const ProgEntry kDescendingFifths[] = {
    {ChordDegree::I,      ChordQuality::Major,      0, 1.0f,  false},
    {ChordDegree::IV,     ChordQuality::Major,      0, 0.5f,  false},
    {ChordDegree::viiDim, ChordQuality::Diminished, 1, 0.5f,  true},   // 1st inversion
    {ChordDegree::iii,    ChordQuality::Minor,      1, 0.5f,  false},  // 1st inversion
    {ChordDegree::vi,     ChordQuality::Minor,      0, 0.5f,  false},
    {ChordDegree::ii,     ChordQuality::Minor,      1, 0.5f,  false},  // 1st inversion
    {ChordDegree::V,      ChordQuality::Dominant7,  0, 0.75f, true},
    {ChordDegree::I,      ChordQuality::Major,      0, 1.0f,  false},
};

HarmonicTimeline HarmonicTimeline::createProgression(const KeySignature& key_sig,
                                                      Tick duration,
                                                      HarmonicResolution resolution,
                                                      ProgressionType prog_type) {
  if (prog_type == ProgressionType::Basic) {
    return createStandard(key_sig, duration, resolution);
  }

  HarmonicTimeline timeline;
  if (duration == 0) return timeline;

  const ProgEntry* prog = nullptr;
  int prog_len = 0;
  switch (prog_type) {
    case ProgressionType::CircleOfFifths:
      prog = kCircleOfFifths;
      prog_len = 5;
      break;
    case ProgressionType::Subdominant:
      prog = kSubdominant;
      prog_len = 5;
      break;
    case ProgressionType::ChromaticCircle:
      prog = kChromaticCircle;
      prog_len = 6;
      break;
    case ProgressionType::BorrowedChord:
      prog = kBorrowedChord;
      prog_len = 5;
      break;
    case ProgressionType::DescendingFifths:
      prog = kDescendingFifths;
      prog_len = 8;
      break;
    default:
      return createStandard(key_sig, duration, resolution);
  }

  Tick event_length = 0;
  switch (resolution) {
    case HarmonicResolution::Beat:   event_length = kTicksPerBeat; break;
    case HarmonicResolution::Bar:    event_length = kTicksPerBar;  break;
    case HarmonicResolution::Section:
      event_length = duration / prog_len;
      if (event_length == 0) event_length = duration;
      break;
  }

  constexpr int kChordOctave = 4;
  constexpr int kBassOctave = 2;

  Tick current_tick = 0;
  int progression_idx = 0;

  while (current_tick < duration) {
    const auto& entry = prog[progression_idx % prog_len];

    Chord chord;
    if (entry.use_quality_override) {
      chord = buildChordWithQuality(key_sig, entry.degree, entry.quality_override,
                                    kChordOctave, entry.inversion);
    } else {
      chord = buildChord(key_sig, entry.degree, kChordOctave, entry.inversion);
    }

    uint8_t bass = computeBassPitch(chord, kBassOctave);

    Tick event_end = current_tick + event_length;
    if (event_end > duration) event_end = duration;

    HarmonicEvent event;
    event.tick = current_tick;
    event.end_tick = event_end;
    event.key = key_sig.tonic;
    event.is_minor = key_sig.is_minor;
    event.chord = chord;
    event.bass_pitch = bass;
    event.weight = entry.weight;
    event.is_immutable = false;

    timeline.addEvent(event);

    current_tick = event_end;
    ++progression_idx;
  }

  return timeline;
}

// ---------------------------------------------------------------------------
// Cadence application
// ---------------------------------------------------------------------------

void HarmonicTimeline::applyCadence(CadenceType cadence, const KeySignature& key_sig) {
  if (events_.empty()) return;

  constexpr int kChordOctave = 4;
  constexpr int kBassOctave = 2;

  auto& last = events_.back();

  switch (cadence) {
    case CadenceType::Perfect: {
      // Make the penultimate chord V7 if we have at least 2 events.
      if (events_.size() >= 2) {
        auto& penult = events_[events_.size() - 2];
        penult.chord = buildChordWithQuality(key_sig, ChordDegree::V,
                                             ChordQuality::Dominant7, kChordOctave);
        penult.bass_pitch = computeBassPitch(penult.chord, kBassOctave);
      }
      last.chord = buildChord(key_sig, ChordDegree::I, kChordOctave);
      last.bass_pitch = computeBassPitch(last.chord, kBassOctave);
      break;
    }
    case CadenceType::Deceptive: {
      // V -> vi instead of V -> I
      if (events_.size() >= 2) {
        auto& penult = events_[events_.size() - 2];
        penult.chord = buildChordWithQuality(key_sig, ChordDegree::V,
                                             ChordQuality::Dominant7, kChordOctave);
        penult.bass_pitch = computeBassPitch(penult.chord, kBassOctave);
      }
      last.chord = buildChord(key_sig, ChordDegree::vi, kChordOctave);
      last.bass_pitch = computeBassPitch(last.chord, kBassOctave);
      break;
    }
    case CadenceType::Half: {
      // End on V
      last.chord = buildChord(key_sig, ChordDegree::V, kChordOctave);
      last.bass_pitch = computeBassPitch(last.chord, kBassOctave);
      break;
    }
    case CadenceType::Phrygian: {
      // iv6 -> V (minor key). Set penultimate to iv in first inversion.
      if (events_.size() >= 2) {
        auto& penult = events_[events_.size() - 2];
        penult.chord = buildChord(key_sig, ChordDegree::IV, kChordOctave, 1);
        penult.bass_pitch = computeBassPitch(penult.chord, kBassOctave);
      }
      last.chord = buildChord(key_sig, ChordDegree::V, kChordOctave);
      last.bass_pitch = computeBassPitch(last.chord, kBassOctave);
      break;
    }
    case CadenceType::PicardyThird: {
      // Final chord is I major even in minor key.
      last.chord = buildChordWithQuality(key_sig, ChordDegree::I,
                                         ChordQuality::Major, kChordOctave);
      last.bass_pitch = computeBassPitch(last.chord, kBassOctave);
      break;
    }
    case CadenceType::Plagal: {
      // IV -> I (plagal cadence, sometimes called "amen cadence").
      if (events_.size() >= 2) {
        auto& penult = events_[events_.size() - 2];
        penult.chord = buildChord(key_sig, ChordDegree::IV, kChordOctave);
        penult.bass_pitch = computeBassPitch(penult.chord, kBassOctave);
      }
      last.chord = buildChord(key_sig, ChordDegree::I, kChordOctave);
      last.bass_pitch = computeBassPitch(last.chord, kBassOctave);
      break;
    }
  }
}

}  // namespace bach
