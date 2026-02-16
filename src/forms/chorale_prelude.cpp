// Implementation of the chorale prelude generator (BWV 599-650 style).

#include "forms/chorale_prelude.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

#include "core/gm_program.h"
#include "core/interval.h"
#include "core/melodic_state.h"
#include "core/note_creator.h"
#include "core/note_source.h"
#include "core/pitch_utils.h"
#include "core/rng_util.h"
#include "core/scale.h"
#include "counterpoint/bach_rule_evaluator.h"
#include "counterpoint/collision_resolver.h"
#include "counterpoint/coordinate_voices.h"
#include "forms/form_utils.h"
#include "counterpoint/counterpoint_state.h"
#include "counterpoint/leap_resolution.h"
#include "counterpoint/parallel_repair.h"
#include "counterpoint/vertical_context.h"
#include "counterpoint/vertical_safe.h"
#include "harmony/chord_types.h"
#include "harmony/harmonic_event.h"
#include "harmony/harmonic_timeline.h"
#include "organ/organ_techniques.h"

namespace bach {

namespace {

using namespace duration;

/// @brief MIDI channel for Great manual (counterpoint voice).
constexpr uint8_t kGreatChannel = 0;

/// @brief MIDI channel for Swell manual (cantus firmus).
constexpr uint8_t kSwellChannel = 1;

/// @brief MIDI channel for Pedal.
constexpr uint8_t kPedalChannel = 3;

/// @brief Voice indices for chorale prelude voices.
constexpr uint8_t kFigurationVoice = 0;  // Great: ornamental soprano
constexpr uint8_t kCantusVoice = 1;      // Swell: cantus firmus (tenor/alto)
constexpr uint8_t kInnerVoice = 2;  // Great: inner voice
constexpr uint8_t kPedalVoice = 3;       // Pedal: bass

/// @brief Total number of voices in a chorale prelude.
constexpr uint8_t kChoraleVoices = 4;

/// @brief Number of built-in chorale melodies.
constexpr int kChoraleCount = 3;

// ---------------------------------------------------------------------------
// Chorale melody representation
// ---------------------------------------------------------------------------

/// @brief A single note in a chorale melody (pitch + duration in beats).
struct ChoraleNote {
  uint8_t pitch;          ///< MIDI pitch in C major context.
  uint8_t duration_beats; ///< Duration in beats (4 = whole note, 8 = breve).
};

/// @brief A built-in chorale melody with name and notes.
struct ChoraleMelody {
  const char* name;
  const ChoraleNote* notes;
  size_t note_count;
};

// ---------------------------------------------------------------------------
// Built-in chorale melodies (simple public domain hymn tunes in C major)
// ---------------------------------------------------------------------------

/// "Wachet auf" (Wake, Awake) -- ascending/descending stepwise melody.
/// Inspired by the opening of Philipp Nicolai's hymn tune (1599).
constexpr ChoraleNote kWachetAuf[] = {
    {60, 4}, {62, 4}, {64, 4}, {65, 4},  // C D E F (ascending)
    {67, 8},                               // G (held)
    {65, 4}, {64, 4}, {62, 4}, {60, 4},  // F E D C (descending)
    {62, 4}, {64, 8},                      // D E (half close)
    {67, 4}, {65, 4}, {64, 4}, {62, 4},  // G F E D
    {60, 8},                               // C (final)
};

/// "Nun komm" (Now Come) -- stepwise motion melody with gentle arc.
/// Inspired by the Advent hymn tune (15th century).
constexpr ChoraleNote kNunKomm[] = {
    {64, 4}, {62, 4}, {60, 4}, {62, 4},  // E D C D
    {64, 4}, {64, 4}, {64, 8},            // E E E (held)
    {65, 4}, {67, 4}, {69, 4}, {67, 4},  // F G A G
    {65, 4}, {64, 4}, {62, 4}, {60, 8},  // F E D C (final)
};

/// "Ein feste Burg" (A Mighty Fortress) -- bold, assertive melody.
/// Inspired by Martin Luther's hymn tune (1529).
constexpr ChoraleNote kEinFesteBurg[] = {
    {67, 4}, {67, 4}, {67, 4}, {64, 4},  // G G G E
    {65, 4}, {67, 4}, {69, 4}, {67, 8},  // F G A G (held)
    {65, 4}, {64, 4}, {62, 4}, {64, 4},  // F E D E
    {60, 4}, {62, 4}, {60, 8},            // C D C (final)
};

/// @brief Array of all built-in chorale melodies.
const ChoraleMelody kChorales[] = {
    {"Wachet auf", kWachetAuf, sizeof(kWachetAuf) / sizeof(kWachetAuf[0])},
    {"Nun komm", kNunKomm, sizeof(kNunKomm) / sizeof(kNunKomm[0])},
    {"Ein feste Burg", kEinFesteBurg, sizeof(kEinFesteBurg) / sizeof(kEinFesteBurg[0])},
};

// ---------------------------------------------------------------------------
// Track creation
// ---------------------------------------------------------------------------

/// @brief Create the 4 MIDI tracks for a chorale prelude.
///
/// Track layout:
///   Track 0: Counterpoint voice on Great (ch 0, Church Organ).
///   Track 1: Cantus firmus on Swell (ch 1, Reed Organ).
///   Track 2: Inner voice on Great (ch 0, Church Organ) — shares channel with Track 0.
///   Track 3: Pedal bass (ch 3, Church Organ).
///
/// @return Vector of 4 Track objects with channel/program/name configured.
std::vector<Track> createChoralePreludeTracks() {
  std::vector<Track> tracks;
  tracks.reserve(4);

  Track counterpoint_track;
  counterpoint_track.channel = kGreatChannel;
  counterpoint_track.program = GmProgram::kChurchOrgan;
  counterpoint_track.name = "Counterpoint (Great)";
  tracks.push_back(counterpoint_track);

  Track cantus_track;
  cantus_track.channel = kSwellChannel;
  cantus_track.program = GmProgram::kReedOrgan;
  cantus_track.name = "Cantus Firmus (Swell)";
  tracks.push_back(cantus_track);

  Track inner_track;
  inner_track.channel = kGreatChannel;
  inner_track.program = GmProgram::kChurchOrgan;
  inner_track.name = "Inner Voice (Great)";
  tracks.push_back(inner_track);

  Track pedal_track;
  pedal_track.channel = kPedalChannel;
  pedal_track.program = GmProgram::kChurchOrgan;
  pedal_track.name = "Pedal";
  tracks.push_back(pedal_track);

  return tracks;
}

// ---------------------------------------------------------------------------
// Cantus firmus placement
// ---------------------------------------------------------------------------

/// @brief Calculate total duration of a chorale melody in ticks.
/// @param melody The chorale melody.
/// @return Total duration in ticks.
Tick calculateChoraleDuration(const ChoraleMelody& melody) {
  Tick total = 0;
  for (size_t idx = 0; idx < melody.note_count; ++idx) {
    total += static_cast<Tick>(melody.notes[idx].duration_beats) * kTicksPerBeat;
  }
  return total;
}

/// @brief Get the third interval in semitones for a chord quality.
/// @param quality Chord quality.
/// @return 4 for major-quality chords, 3 for minor/diminished.
int choraleThirdInterval(ChordQuality quality) {
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
/// @param quality Chord quality.
/// @return 6 for diminished, 8 for augmented, 7 otherwise.
int choraleFifthInterval(ChordQuality quality) {
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

/// @brief Compute bass pitch with inversion support.
/// @param chord The chord (inversion field determines bass note).
/// @param bass_octave Octave for the bass note (typically 2).
/// @return MIDI pitch for the bass.
uint8_t choraleBassPitch(const Chord& chord, int bass_octave) {
  int root_pc = getPitchClass(chord.root_pitch);
  int bass_pc = root_pc;
  if (chord.inversion == 1) {
    bass_pc = (root_pc + choraleThirdInterval(chord.quality)) % 12;
  } else if (chord.inversion == 2) {
    bass_pc = (root_pc + choraleFifthInterval(chord.quality)) % 12;
  }
  int bass_midi = (bass_octave + 1) * 12 + bass_pc;
  return clampPitch(bass_midi, 0, 127);
}

/// @brief Create a HarmonicTimeline driven by cantus firmus pitches.
///
/// Each cantus note maps to a harmonic event using scale degree analysis.
/// Applies inversions for bass smoothness: vi always gets first inversion,
/// ii gets first inversion when preceded by I or iii.
/// @param melody The chorale melody.
/// @param key Key signature.
/// @return A HarmonicTimeline with cantus-driven harmony.
HarmonicTimeline createChoraleTimeline(const ChoraleMelody& melody,
                                       const KeySignature& key) {
  HarmonicTimeline timeline;
  ScaleType scale_type = key.is_minor ? ScaleType::HarmonicMinor
                                      : ScaleType::Major;
  int bass_octave = 2;

  std::vector<HarmonicEvent> events;
  events.reserve(melody.note_count);

  Tick current_tick = 0;
  for (size_t idx = 0; idx < melody.note_count; ++idx) {
    Tick dur = static_cast<Tick>(melody.notes[idx].duration_beats) * kTicksPerBeat;
    uint8_t pitch = clampPitch(static_cast<int>(melody.notes[idx].pitch), 60, 79);

    int degree = 0;
    scale_util::pitchToScaleDegree(pitch, key.tonic, scale_type, degree);
    ChordDegree chord_degree = scaleDegreeToChordDegree(degree, key.is_minor);

    Chord chord;
    chord.degree = chord_degree;
    chord.quality = key.is_minor ? minorKeyQuality(chord_degree)
                                 : majorKeyQuality(chord_degree);
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

    // Step 1b: Inversion logic for bass smoothness.
    // vi always gets first inversion; ii gets first inversion when preceded by I or iii.
    if (chord_degree == ChordDegree::vi) {
      chord.inversion = 1;
    } else if (chord_degree == ChordDegree::ii && !events.empty()) {
      ChordDegree prev_degree = events.back().chord.degree;
      if (prev_degree == ChordDegree::I || prev_degree == ChordDegree::iii) {
        chord.inversion = 1;
      }
    }

    // Use inversion-aware bass pitch calculation.
    uint8_t bass_pitch = choraleBassPitch(chord, bass_octave);

    HarmonicEvent event;
    event.tick = current_tick;
    event.end_tick = current_tick + dur;
    event.key = key.tonic;
    event.is_minor = key.is_minor;
    event.chord = chord;
    event.bass_pitch = bass_pitch;
    event.weight = 1.0f;

    events.push_back(event);
    current_tick += dur;
  }

  for (const auto& ev : events) {
    timeline.addEvent(ev);
  }
  return timeline;
}

/// @brief Place the cantus firmus notes onto the Swell track.
///
/// The cantus is placed with long note values as specified in the melody.
/// Pitches are placed in the alto/tenor register (C4-B4, BWV 639 style).
///
/// @param melody The chorale melody to place.
/// @param track Swell track to populate.
void placeCantus(const ChoraleMelody& melody, Track& track) {
  Tick current_tick = 0;
  for (size_t idx = 0; idx < melody.note_count; ++idx) {
    Tick dur = static_cast<Tick>(melody.notes[idx].duration_beats) * kTicksPerBeat;
    uint8_t pitch = melody.notes[idx].pitch;

    // Cantus sits in the alto/tenor register on Swell (BWV 639 style).
    // Figuration (soprano) sits above it on the Great manual.
    pitch = clampPitch(static_cast<int>(pitch), 60, 71);  // C4-B4 (alto/tenor)

    NoteEvent note;
    note.start_tick = current_tick;
    note.duration = dur;
    note.pitch = pitch;
    note.velocity = kOrganVelocity;
    note.voice = 1;
    note.source = BachNoteSource::CantusFixed;
    track.notes.push_back(note);

    current_tick += dur;
  }
}

/// Insert passing tones into long cantus notes (weak-beat, stepwise only).
void addCantusPassingTones(Track& track,
                           const std::pair<uint8_t, uint8_t>& voice_range,
                           Key key, bool is_minor) {
  ScaleType scale = is_minor ? ScaleType::HarmonicMinor : ScaleType::Major;
  std::vector<NoteEvent> enriched;
  auto& notes = track.notes;

  for (size_t idx = 0; idx < notes.size(); ++idx) {
    auto note = notes[idx];
    // Only enrich notes > 2 beats that have a next note.
    if (note.duration <= kHalfNote || idx + 1 >= notes.size()) {
      enriched.push_back(note);
      continue;
    }

    uint8_t next_pitch = notes[idx + 1].pitch;
    int cur_deg = scale_util::pitchToAbsoluteDegree(note.pitch, key, scale);
    int next_deg = scale_util::pitchToAbsoluteDegree(next_pitch, key, scale);
    int deg_dist = std::abs(next_deg - cur_deg);

    // Only insert passing tone if next note is 2 degrees away (stepwise).
    if (deg_dist != 2) {
      enriched.push_back(note);
      continue;
    }

    Tick half = note.duration / 2;
    // Weak-beat constraint: passing tone must land on beat 1 or 3.
    Tick pt_start = note.start_tick + half;
    uint8_t pt_beat = beatInBar(pt_start);
    if (pt_beat != 1 && pt_beat != 3) {
      enriched.push_back(note);
      continue;
    }

    int dir = (next_deg > cur_deg) ? 1 : -1;
    uint8_t passing = scale_util::absoluteDegreeToPitch(cur_deg + dir, key, scale);
    passing = clampPitch(static_cast<int>(passing),
                         voice_range.first, voice_range.second);

    // Split: first half original, second half passing tone.
    note.duration = half;
    enriched.push_back(note);

    NoteEvent pt_note;
    pt_note.start_tick = pt_start;
    pt_note.duration = half;
    pt_note.pitch = passing;
    pt_note.velocity = note.velocity;
    pt_note.voice = note.voice;
    pt_note.source = BachNoteSource::CantusFixed;
    enriched.push_back(pt_note);
  }
  track.notes = std::move(enriched);
}

// ---------------------------------------------------------------------------
// Figuration motif extraction and application
// ---------------------------------------------------------------------------

/// A motif extracted from the first figuration segment.
struct FigurationMotif {
  int intervals[4] = {};    // Diatonic intervals (signed, relative to first note).
  Tick durations[5] = {};   // Duration of each note.
  int length = 0;           // Number of notes (3-5).
  bool valid = false;
};

/// Extract a motif from the first few notes of a figuration segment.
FigurationMotif extractMotif(const std::vector<NoteEvent>& notes) {
  FigurationMotif motif;
  if (notes.size() < 3) return motif;

  int count = std::min(static_cast<int>(notes.size()), 5);
  motif.length = count;
  motif.durations[0] = notes[0].duration;
  for (int idx = 1; idx < count; ++idx) {
    motif.intervals[idx - 1] = static_cast<int>(notes[idx].pitch) -
                                 static_cast<int>(notes[idx - 1].pitch);
    motif.durations[idx] = notes[idx].duration;
  }
  motif.valid = true;
  return motif;
}

/// Apply a motif starting from a given anchor pitch.
/// Returns the notes generated by applying the motif intervals.
std::vector<NoteEvent> applyMotif(const FigurationMotif& motif,
                                   uint8_t anchor_pitch, Tick start_tick,
                                   uint8_t voice, uint8_t low, uint8_t high,
                                   const KeySignature& key_sig) {
  std::vector<NoteEvent> notes;
  if (!motif.valid || motif.length < 3) return notes;

  ScaleType scale = key_sig.is_minor ? ScaleType::NaturalMinor : ScaleType::Major;

  uint8_t current_pitch = anchor_pitch;
  Tick current_tick = start_tick;

  for (int idx = 0; idx < motif.length; ++idx) {
    uint8_t pit = clampPitch(static_cast<int>(current_pitch), low, high);
    // Ensure scale tone.
    if (!scale_util::isScaleTone(pit, key_sig.tonic, scale)) {
      // Snap to nearest scale tone.
      for (int delta = 1; delta <= 2; ++delta) {
        if (pit + delta <= high &&
            scale_util::isScaleTone(pit + delta, key_sig.tonic, scale)) {
          pit = pit + delta;
          break;
        }
        if (pit >= delta + low &&
            scale_util::isScaleTone(pit - delta, key_sig.tonic, scale)) {
          pit = pit - delta;
          break;
        }
      }
    }

    NoteEvent note;
    note.start_tick = current_tick;
    note.duration = motif.durations[idx];
    note.pitch = pit;
    note.velocity = kOrganVelocity;
    note.voice = voice;
    note.source = BachNoteSource::FreeCounterpoint;
    notes.push_back(note);

    current_tick += motif.durations[idx];
    if (idx < motif.length - 1) {
      current_pitch = clampPitch(static_cast<int>(pit) + motif.intervals[idx], low, high);
    }
  }
  return notes;
}

// ---------------------------------------------------------------------------
// Counterpoint figuration (Great manual)
// ---------------------------------------------------------------------------

/// @brief Generate ornamental counterpoint against one cantus note.
///
/// Creates figurations (16th, 8th, dotted-8th) using scale tones and chord
/// tones in the soprano region above the cantus (BWV 639 style). Uses
/// register center control (cantus + 12-17, ±9 window) with hint-based
/// continuity, directional persistence (3-4 notes), and neighbor tones (20%).
///
/// @param cantus_tick Start tick of the cantus note.
/// @param cantus_dur Duration of the cantus note in ticks.
/// @param timeline Harmonic timeline for chord context.
/// @param key_sig Key signature for scale tone lookup.
/// @param cantus_pitch Current cantus note pitch (for center calculation).
/// @param hint_center Mutable center hint for continuity (0 = first call).
/// @param rng Mersenne Twister RNG instance.
/// @return Vector of NoteEvents for the counterpoint voice.
std::vector<NoteEvent> generateFiguration(Tick cantus_tick, Tick cantus_dur,
                                          const HarmonicTimeline& timeline,
                                          const KeySignature& key_sig,
                                          uint8_t cantus_pitch,
                                          uint8_t& hint_center,
                                          std::mt19937& rng,
                                          const FigurationMotif* motif = nullptr,
                                          const VerticalContext* vctx = nullptr) {
  std::vector<NoteEvent> notes;

  constexpr uint8_t kFigLow = 72;
  constexpr uint8_t kFigHigh = 88;
  constexpr Tick kDottedEighth = kEighthNote + kSixteenthNote;  // 360

  // 3b: Register center control — center = cantus_pitch + 12~17, ±9 window.
  uint8_t offset = 12 + static_cast<uint8_t>(rng::rollRange(rng, 0, 5));
  uint8_t raw_center = clampPitch(
      static_cast<int>(cantus_pitch) + offset, kFigLow, kFigHigh);
  uint8_t center;
  if (hint_center == 0) {
    center = raw_center;
  } else {
    // Blend toward raw_center (60%) for responsive register tracking.
    center = static_cast<uint8_t>(
        (static_cast<int>(hint_center) * 4 + static_cast<int>(raw_center) * 6) / 10);
    center = clampPitch(static_cast<int>(center), kFigLow, kFigHigh);
  }

  uint8_t eff_low = clampPitch(static_cast<int>(center) - 12, kFigLow, kFigHigh);
  uint8_t eff_high = clampPitch(static_cast<int>(center) + 12, kFigLow, kFigHigh);

  // Figuration runs use natural minor to avoid augmented 2nd exposure;
  // harmonic minor reserved for cadence/leading-tone contexts.
  ScaleType run_scale_type = key_sig.is_minor ? ScaleType::NaturalMinor
                                              : ScaleType::Major;
  ScaleType cadence_scale_type = key_sig.is_minor ? ScaleType::HarmonicMinor
                                                  : ScaleType::Major;
  (void)cadence_scale_type;  // Reserved for future cadence-context use.

  // Collect scale tones in the effective range (using run scale type).
  std::vector<uint8_t> scale_tones;
  for (int p = static_cast<int>(eff_low); p <= static_cast<int>(eff_high); ++p) {
    if (scale_util::isScaleTone(static_cast<uint8_t>(p), key_sig.tonic,
                                run_scale_type)) {
      scale_tones.push_back(static_cast<uint8_t>(p));
    }
  }

  if (scale_tones.empty()) {
    return notes;
  }

  Tick current_tick = cantus_tick;
  Tick end_tick = cantus_tick + cantus_dur;

  // Start near previous call's ending pitch for Fortspinnung continuity.
  // First call (hint_center == 0) uses the blended center.
  uint8_t start_ref = (hint_center != 0) ? hint_center : center;
  size_t tone_idx = 0;
  int min_dist = 999;
  for (size_t i = 0; i < scale_tones.size(); ++i) {
    int d = std::abs(static_cast<int>(scale_tones[i]) -
                     static_cast<int>(start_ref));
    if (d < min_dist) {
      min_dist = d;
      tone_idx = i;
    }
  }

  // Motif presentation: anchor on cantus_pitch.
  if (motif && motif->valid && cantus_dur > kWholeNote) {
    uint8_t anchor = clampPitch(static_cast<int>(cantus_pitch) + 12, kFigLow, kFigHigh);
    auto motif_notes = applyMotif(*motif, anchor, cantus_tick, kFigurationVoice,
                                   kFigLow, kFigHigh, key_sig);
    Tick motif_end = cantus_tick;
    for (const auto& mn : motif_notes) {
      notes.push_back(mn);
      Tick end = mn.start_tick + mn.duration;
      if (end > motif_end) motif_end = end;
    }
    // Advance current_tick past the motif.
    if (motif_end > current_tick) {
      current_tick = motif_end;
      // Update tone_idx to nearest scale tone to the last motif note.
      if (!motif_notes.empty()) {
        uint8_t last_motif_pitch = motif_notes.back().pitch;
        int md = 999;
        for (size_t mi = 0; mi < scale_tones.size(); ++mi) {
          int dist = std::abs(static_cast<int>(scale_tones[mi]) -
                              static_cast<int>(last_motif_pitch));
          if (dist < md) { md = dist; tone_idx = mi; }
        }
      }
    }
  }

  bool ascending = rng::rollProbability(rng, 0.5f);
  int run_remaining = 0;  // Steps remaining in current directional run (Fortspinnung).
  bool prev_step_was_skip = false;  // Consecutive skip guard (max 1 per run).
  Tick prev_harm_start = timeline.getAt(cantus_tick).tick;
  int neighbor_return = -1;  // Saved tone_idx for neighbor return, -1 = none.

  // Cadence convergence window: last 2 beats of this cantus segment broaden rhythm.
  Tick cadence_window = 2 * kTicksPerBeat;

  while (current_tick < end_tick) {
    // 3a: Beat-position-aware duration selection for structural rhythm.
    // Downbeats anchor with quarter notes, middle beats use eighth-note
    // ornamental figuration, and pre-cadence regions broaden to quarter/half.
    uint8_t beat = beatInBar(current_tick);
    bool near_cadence = (end_tick - current_tick) <= cadence_window;
    Tick dur;

    if (near_cadence) {
      // Cadence convergence: broaden to quarter/half notes.
      dur = rng::rollProbability(rng, 0.6f) ? kQuarterNote : kHalfNote;
    } else if (beat == 0) {
      // Downbeat: quarter note anchor (stable metric position).
      dur = kQuarterNote;
    } else if (beat == 3) {
      // Beat 4: prepare next bar with quarter note.
      dur = rng::rollProbability(rng, 0.7f) ? kQuarterNote : kEighthNote;
    } else {
      // Beats 2-3: ornamental eighth notes with occasional 16th for activity.
      int rval = rng::rollRange(rng, 0, 9);
      if (rval < 2) {
        dur = kSixteenthNote;
      } else if (rval < 7) {
        dur = kEighthNote;
      } else {
        dur = kEighthNote + kSixteenthNote;  // Dotted eighth.
      }
    }
    Tick remaining = end_tick - current_tick;
    if (dur > remaining) dur = remaining;
    if (dur == 0) break;

    size_t prev_tone_idx = tone_idx;  // Save for run disruption check.

    // On strong beats, prefer chord tones from the harmonic timeline.
    // Direction-aware: prefer chord tones in current travel direction
    // to avoid oscillation (snap-back repeats).
    if (current_tick % kTicksPerBeat == 0) {
      const HarmonicEvent& event = timeline.getAt(current_tick);
      if (!isChordTone(scale_tones[tone_idx], event)) {
        size_t saved_idx = tone_idx;
        bool found = false;
        for (size_t search = 1; search < scale_tones.size() && !found;
             ++search) {
          size_t forward, backward;
          if (ascending) {
            forward = tone_idx + search;
            backward = (tone_idx >= search) ? tone_idx - search
                                            : scale_tones.size();
          } else {
            forward = (tone_idx >= search) ? tone_idx - search
                                           : scale_tones.size();
            backward = tone_idx + search;
          }
          if (forward < scale_tones.size() &&
              isChordTone(scale_tones[forward], event)) {
            tone_idx = forward;
            found = true;
          } else if (backward < scale_tones.size() &&
                     isChordTone(scale_tones[backward], event)) {
            tone_idx = backward;
            found = true;
          }
        }
        // Cantus protection: on strong beats, prefer chord tones away from cantus.
        if (found) {
          int cantus_dist = std::abs(static_cast<int>(scale_tones[tone_idx]) -
                                     static_cast<int>(cantus_pitch));
          if (cantus_dist <= 5) {
            size_t alt_idx = ascending
                ? (tone_idx >= 2 ? tone_idx - 2 : 0)
                : std::min(tone_idx + 2, scale_tones.size() - 1);
            if (alt_idx < scale_tones.size() &&
                isChordTone(scale_tones[alt_idx], event) &&
                std::abs(static_cast<int>(scale_tones[alt_idx]) -
                         static_cast<int>(cantus_pitch)) > 5) {
              tone_idx = alt_idx;
            }
          }
        }

        // Avoid repeat: if snap recreates previous pitch, revert to
        // allow passing tone rather than stagnation.
        if (found && !notes.empty() &&
            scale_tones[tone_idx] == notes.back().pitch) {
          tone_idx = saved_idx;
        }
      }
    }

    // Avoid consecutive pitch repeats — skip by 2nd for variety.
    if (!notes.empty() && scale_tones[tone_idx] == notes.back().pitch) {
      if (ascending && tone_idx + 2 < scale_tones.size()) {
        tone_idx += 2;
      } else if (!ascending && tone_idx >= 2) {
        tone_idx -= 2;
      } else if (tone_idx + 2 < scale_tones.size()) {
        tone_idx += 2;
      } else if (tone_idx >= 2) {
        tone_idx -= 2;
      } else if (tone_idx + 1 < scale_tones.size()) {
        tone_idx += 1;
      } else if (tone_idx >= 1) {
        tone_idx -= 1;
      }
    }

    // Run disruption check: end run if chord-tone snap moved significantly
    // against direction or made a large jump (even in run direction).
    if (run_remaining > 0) {
      int snap_delta = static_cast<int>(tone_idx) - static_cast<int>(prev_tone_idx);
      bool snap_against = (ascending && snap_delta < -1) ||
                          (!ascending && snap_delta > 1);
      bool large_jump = std::abs(snap_delta) >= 3;
      if (snap_against || large_jump) {
        run_remaining = 0;
      }
    }

    uint8_t fig_pitch = scale_tones[tone_idx];

    // Category A: vctx safety filter — try stepwise alternatives if unsafe.
    if (vctx && !vctx->isSafe(current_tick, kFigurationVoice, fig_pitch)) {
      for (int delta : {1, -1, 2, -2}) {
        uint8_t alt = clampPitch(static_cast<int>(fig_pitch) + delta,
                                 eff_low, eff_high);
        if (scale_util::isScaleTone(alt, key_sig.tonic, run_scale_type) &&
            vctx->isSafe(current_tick, kFigurationVoice, alt)) {
          fig_pitch = alt;
          break;
        }
      }
    }

    NoteEvent note;
    note.start_tick = current_tick;
    note.duration = dur;
    note.pitch = fig_pitch;
    note.velocity = kOrganVelocity;
    note.voice = kFigurationVoice;
    note.source = BachNoteSource::FreeCounterpoint;
    notes.push_back(note);

    current_tick += dur;

    // 3c: Check harmonic event boundary for direction control.
    Tick check_tick = (current_tick < end_tick) ? current_tick : end_tick - 1;
    const HarmonicEvent& next_harm = timeline.getAt(check_tick);
    bool at_harmony_boundary = (next_harm.tick != prev_harm_start);
    if (at_harmony_boundary) {
      prev_harm_start = next_harm.tick;
    }

    // ---- Stepping logic ----

    // 3c: Handle neighbor return (second half of neighbor tone pattern).
    bool did_step = false;
    if (neighbor_return >= 0) {
      tone_idx = static_cast<size_t>(neighbor_return);
      neighbor_return = -1;
      did_step = true;
    }

    // 3c: Neighbor tone — step away by 1, return next iteration.
    // Reduced during active runs to preserve directional coherence.
    bool active_run = run_remaining > 0 && !at_harmony_boundary;
    float neighbor_prob = active_run ? 0.10f : 0.20f;
    if (!did_step && rng::rollProbability(rng, neighbor_prob)) {
      int saved = static_cast<int>(tone_idx);
      bool stepped = false;
      if (ascending && tone_idx + 1 < scale_tones.size()) {
        tone_idx += 1;
        stepped = true;
      } else if (!ascending && tone_idx >= 1) {
        tone_idx -= 1;
        stepped = true;
      }
      if (stepped) {
        neighbor_return = saved;
        did_step = true;
      }
    }

    // Guided run model (Fortspinnung): coherent directional runs of 4-8 notes.
    if (!did_step) {
      // Plan new run if current run is exhausted or at harmonic boundary.
      if (run_remaining <= 0 || at_harmony_boundary) {
        // Register balance: bias direction toward under-represented register.
        if (scale_tones.size() > 1) {
          float rel_pos = static_cast<float>(tone_idx) /
                          static_cast<float>(scale_tones.size() - 1);
          if (rel_pos > 0.7f) {
            ascending = false;  // Near top: descend.
          } else if (rel_pos < 0.3f) {
            ascending = true;   // Near bottom: ascend.
          } else {
            // Mid-register: alternate at harmonic boundaries.
            ascending = at_harmony_boundary ? !ascending : ascending;
          }
        }
        // Run length 4-8 notes (BWV 599-650 typical phrase segment).
        // Short runs (4) near scale boundaries to avoid rapid bouncing.
        int max_run = (scale_tones.size() <= 6) ? 3 : 4;
        run_remaining = 4 + rng::rollRange(rng, 0, max_run);
        // Safety cap: never exceed available scale tones + 1 boundary step.
        run_remaining = std::min(run_remaining,
                                 static_cast<int>(scale_tones.size()) + 1);
        prev_step_was_skip = false;
      }

      // Execute run: stepwise with occasional skip (max 1 consecutive).
      int step;
      if (prev_step_was_skip) {
        step = 1;  // After skip, force stepwise (no consecutive 3rds).
      } else {
        step = rng::rollProbability(rng, 0.75f) ? 1 : 2;
      }
      prev_step_was_skip = (step >= 2);

      if (ascending) {
        if (tone_idx + step < scale_tones.size()) {
          tone_idx += step;
        } else {
          // Register boundary: reverse and end run.
          ascending = false;
          run_remaining = 0;
          if (tone_idx >= 1) tone_idx -= 1;
        }
      } else {
        if (tone_idx >= static_cast<size_t>(step)) {
          tone_idx -= step;
        } else {
          // Register boundary: reverse and end run.
          ascending = true;
          run_remaining = 0;
          if (tone_idx + 1 < scale_tones.size()) tone_idx += 1;
        }
      }

      run_remaining--;
    }
  }

  // Update hint_center for next cantus note continuity.
  if (!notes.empty()) {
    hint_center = notes.back().pitch;
  }

  return notes;
}

// ---------------------------------------------------------------------------
// Pedal bass generation
// ---------------------------------------------------------------------------

/// @brief Generate pedal bass notes against one cantus note.
///
/// Creates quarter and half note bass tones using root (50%), fifth (30%),
/// or octave-below (20%) from the harmonic timeline, clamped to pedal range.
/// Octave is constrained by distance (>7 from prev) and consecutive count (max 2).
/// Final 2 beats before piece end use root-fixed for cadence.
///
/// Strong beats (0, 2) enforce consonance with the cantus: if the chosen pitch
/// is dissonant, falls back to bass_pitch, fifth, then root.
/// Weak beats (1, 3) allow passing tones when stepwise from previous note.
///
/// @param cantus_tick Start tick of the cantus note.
/// @param cantus_dur Duration of the cantus note in ticks.
/// @param timeline Harmonic timeline for chord context.
/// @param piece_end_tick End tick of the entire piece (for cadence detection).
/// @param cantus_pitch Current cantus note pitch for consonance checking.
/// @param next_segment_chord_tones Reserved for future use (may be nullptr).
/// @param rng Mersenne Twister RNG instance.
/// @return Vector of NoteEvents for the pedal voice.
std::vector<NoteEvent> generatePedalBass(Tick cantus_tick, Tick cantus_dur,
                                         const HarmonicTimeline& timeline,
                                         Tick piece_end_tick,
                                         uint8_t cantus_pitch,
                                         const std::vector<NoteEvent>* next_segment_chord_tones,
                                         std::mt19937& rng) {
  (void)next_segment_chord_tones;  // Reserved for future use.
  std::vector<NoteEvent> notes;

  Tick current_tick = cantus_tick;
  Tick end_tick = cantus_tick + cantus_dur;
  int consecutive_octave = 0;

  while (current_tick < end_tick) {
    const HarmonicEvent& event = timeline.getAt(current_tick);

    uint8_t bass = clampPitch(static_cast<int>(event.bass_pitch),
                              organ_range::kPedalLow + 2,
                              organ_range::kPedalHigh - 2);

    int fifth_pitch = static_cast<int>(bass) + interval::kPerfect5th;
    uint8_t fifth = clampPitch(fifth_pitch, organ_range::kPedalLow + 2,
                               organ_range::kPedalHigh - 2);

    int octave_pitch = static_cast<int>(bass) - interval::kOctave;
    uint8_t octave = clampPitch(octave_pitch, organ_range::kPedalLow + 2,
                                organ_range::kPedalHigh - 2);

    uint8_t chosen_pitch;

    // Cadence: final 2 beats are root-fixed.
    if (current_tick >= piece_end_tick - 2 * kTicksPerBeat) {
      chosen_pitch = bass;
    } else {
      // Octave constraints: distance > 7 from previous note, or
      // 2+ consecutive octaves -> octave forbidden.
      bool octave_allowed = (octave != bass);
      if (!notes.empty()) {
        int dist = std::abs(static_cast<int>(octave) -
                            static_cast<int>(notes.back().pitch));
        if (dist > 7) octave_allowed = false;
      }
      if (consecutive_octave >= 2) octave_allowed = false;

      // root(50%) / fifth(30%) / octave(20%).
      int rval = rng::rollRange(rng, 0, 9);
      if (rval < 5) {
        chosen_pitch = bass;
      } else if (rval < 8) {
        chosen_pitch = fifth;
      } else {
        chosen_pitch = octave_allowed ? octave : bass;
      }
    }

    // Step 1a: Strong/weak beat consonance rules.
    uint8_t beat = beatInBar(current_tick);
    bool is_strong_beat = (beat == 0 || beat == 2);

    if (is_strong_beat) {
      // Strong beats: enforce consonance with cantus.
      int ivl = interval_util::compoundToSimple(
          std::abs(static_cast<int>(chosen_pitch) - static_cast<int>(cantus_pitch)));
      if (!interval_util::isConsonance(ivl)) {
        // Fallback chain: bass_pitch (inversion-aware), fifth, root.
        // Try bass_pitch first (already inversion-aware from timeline).
        int bass_ivl = interval_util::compoundToSimple(
            std::abs(static_cast<int>(bass) - static_cast<int>(cantus_pitch)));
        if (interval_util::isConsonance(bass_ivl)) {
          chosen_pitch = bass;
        } else {
          int fifth_ivl = interval_util::compoundToSimple(
              std::abs(static_cast<int>(fifth) - static_cast<int>(cantus_pitch)));
          if (interval_util::isConsonance(fifth_ivl)) {
            chosen_pitch = fifth;
          } else {
            // Root fallback: compute root pitch in pedal range.
            uint8_t root = clampPitch(static_cast<int>(event.chord.root_pitch),
                                      organ_range::kPedalLow + 2,
                                      organ_range::kPedalHigh - 2);
            // Octave-adjust root to pedal range if needed.
            while (root > organ_range::kPedalHigh - 2 && root >= 12) {
              root -= 12;
            }
            chosen_pitch = root;
          }
        }
      }
    } else {
      // Weak beats (1, 3): allow passing tones if stepwise from previous.
      if (!notes.empty()) {
        int step_from_prev = std::abs(static_cast<int>(chosen_pitch) -
                                      static_cast<int>(notes.back().pitch));
        if (step_from_prev <= 2) {
          // Stepwise motion allowed — passing tone is acceptable.
          // No additional consonance check on weak beats.
        } else {
          // Non-stepwise on weak beat: enforce consonance with cantus.
          int ivl = interval_util::compoundToSimple(
              std::abs(static_cast<int>(chosen_pitch) -
                       static_cast<int>(cantus_pitch)));
          if (!interval_util::isConsonance(ivl)) {
            chosen_pitch = bass;  // Fall back to chord bass.
          }
        }
      }
    }

    if (chosen_pitch == octave && octave != bass) {
      ++consecutive_octave;
    } else {
      consecutive_octave = 0;
    }

    // Beat-position pattern: half on downbeat, quarter elsewhere.
    Tick dur;
    if (beat == 0) {
      dur = kHalfNote;  // Downbeat: anchor with half note.
    } else if (beat == 2 && rng::rollProbability(rng, 0.3f)) {
      dur = kQuarterNote + kEighthNote;  // Beat 3: occasional dotted quarter.
    } else {
      dur = kQuarterNote;  // Other beats: quarter note.
    }
    Tick remaining = end_tick - current_tick;
    if (dur > remaining) dur = remaining;
    if (dur == 0) break;

    NoteEvent note;
    note.start_tick = current_tick;
    note.duration = dur;
    note.pitch = chosen_pitch;
    note.velocity = kOrganVelocity;
    note.voice = kPedalVoice;
    note.source = BachNoteSource::PedalPoint;
    notes.push_back(note);

    current_tick += dur;
  }

  return notes;
}

// ---------------------------------------------------------------------------
// Strong/weak beat rule functions (Step 4)
// ---------------------------------------------------------------------------

/// Chord tones available at the next strong beat.
struct ChordToneSet {
  uint8_t tones[6];  // max 6 (root, 3rd, 5th in 2 octaves)
  int count = 0;

  bool contains(uint8_t pitch) const {
    int pc_val = getPitchClass(pitch);
    for (int idx = 0; idx < count; ++idx) {
      if (getPitchClass(tones[idx]) == pc_val) return true;
    }
    return false;
  }
};

/// Get chord tones at the next strong beat within a voice range.
ChordToneSet getNextStrongBeatChordTones(const HarmonicTimeline& timeline,
                                          Tick current_tick,
                                          uint8_t voice_low,
                                          uint8_t voice_high) {
  ChordToneSet result;
  // Find the next strong beat (beat 0 or 2).
  Tick bar_pos = current_tick % kTicksPerBar;
  Tick next_strong;
  if (bar_pos < kTicksPerBeat * 2) {
    next_strong = current_tick - bar_pos + kTicksPerBeat * 2;
  } else {
    next_strong = current_tick - bar_pos + kTicksPerBar;
  }
  // If next_strong == current_tick, advance to next strong beat.
  if (next_strong <= current_tick) {
    next_strong += kTicksPerBeat * 2;
  }

  const HarmonicEvent& event = timeline.getAt(next_strong);
  // Collect chord tones in the voice range.
  for (int pit = static_cast<int>(voice_low); pit <= static_cast<int>(voice_high); ++pit) {
    if (isChordTone(static_cast<uint8_t>(pit), event)) {
      if (result.count < 6) {
        result.tones[result.count++] = static_cast<uint8_t>(pit);
      }
    }
  }
  return result;
}

/// Check if a pitch is safe on a strong beat against sounding voices.
/// Requires consonance with all sounding voices. Exception: prepared suspensions.
/// @param pitch The pitch to check.
/// @param cantus_pitch Current cantus pitch.
/// @param pedal_pitch Current pedal pitch (0 if no pedal sounding).
/// @param prev_pitch Previous pitch of this voice (0 if none).
/// @param timeline Harmonic timeline.
/// @param tick Current tick.
/// @return true if the pitch is acceptable on a strong beat.
bool isStrongBeatSafe(uint8_t pitch, uint8_t cantus_pitch,
                      uint8_t pedal_pitch, uint8_t prev_pitch,
                      const HarmonicTimeline& timeline, Tick tick) {
  (void)timeline;
  (void)tick;
  // Check consonance with cantus.
  int ivl_cantus = interval_util::compoundToSimple(
      std::abs(static_cast<int>(pitch) - static_cast<int>(cantus_pitch)));
  if (!interval_util::isConsonance(ivl_cantus)) {
    // Exception: suspension -- previous pitch held over, creating dissonance.
    // Suspension requires: prev_pitch == pitch (held note) and was consonant before.
    // This is a simplified check; full suspension validation is in the FSM.
    if (prev_pitch == pitch) {
      return true;  // Allow as potential suspension (FSM will validate resolution).
    }
    return false;
  }

  // Check consonance with pedal (if sounding).
  if (pedal_pitch > 0) {
    int ivl_pedal = interval_util::compoundToSimple(
        std::abs(static_cast<int>(pitch) - static_cast<int>(pedal_pitch)));
    // 4th against bass is dissonant.
    if (!interval_util::isConsonance(ivl_pedal)) {
      if (prev_pitch == pitch) return true;  // Suspension exception.
      return false;
    }
  }

  return true;
}

/// Check if a pitch is allowed on a weak beat.
/// Allows passing/neighbor tones if stepwise and resolvable to next strong beat chords.
/// @param pitch The pitch to check.
/// @param prev_pitch Previous pitch of this voice.
/// @param next_chord_tones Chord tones at the next strong beat.
/// @param cantus_pitch Current cantus pitch.
/// @return true if the pitch is acceptable on a weak beat.
bool isWeakBeatAllowed(uint8_t pitch, uint8_t prev_pitch,
                       const ChordToneSet& next_chord_tones,
                       uint8_t cantus_pitch) {
  // Chord tones are always allowed.
  int ivl_cantus = interval_util::compoundToSimple(
      std::abs(static_cast<int>(pitch) - static_cast<int>(cantus_pitch)));
  if (interval_util::isConsonance(ivl_cantus)) {
    return true;
  }

  // Non-chord-tone check: must be stepwise AND resolvable.
  if (prev_pitch == 0) return false;  // No context for stepwise check.

  int step = std::abs(static_cast<int>(pitch) - static_cast<int>(prev_pitch));
  // Condition (i): stepwise = semitone (1) or whole-tone (2).
  if (step > 2) return false;

  // Condition (ii): must be able to resolve stepwise to a next-strong-beat chord tone.
  // Check if any chord tone in the set is within 2 semitones of this pitch.
  for (int idx = 0; idx < next_chord_tones.count; ++idx) {
    int resolve_dist = std::abs(static_cast<int>(pitch) -
                                static_cast<int>(next_chord_tones.tones[idx]));
    if (resolve_dist <= 2) return true;
  }

  return false;
}

// ---------------------------------------------------------------------------
// Inner voice generation (Step 2)
// ---------------------------------------------------------------------------

/// FSM states for inner voice generation.
enum class InnerFsmState { Prepare, Dissonance, Resolve };

/// @brief Generate inner voice notes against one cantus segment.
///
/// Uses a 3-state FSM: Prepare (consonant chord tone) -> Dissonance (suspension
/// on strong beat) -> Resolve (stepwise descent to consonance). The density
/// adapts to cantus note duration: eighth notes for long cantus notes,
/// quarter notes for short ones.
///
/// @param cantus_tick Start tick of the cantus note segment.
/// @param cantus_dur Duration of the cantus note in ticks.
/// @param cantus_pitch Current cantus pitch.
/// @param timeline Harmonic timeline.
/// @param key_sig Key signature.
/// @param pedal_notes Pedal notes in this segment (for vertical checking).
/// @param prev_inner_pitch Previous inner voice pitch (0 = first call).
/// @param rng Mersenne Twister RNG instance.
/// @return Vector of NoteEvents for the inner voice.
std::vector<NoteEvent> generateInnerVoice(
    Tick cantus_tick, Tick cantus_dur, uint8_t cantus_pitch,
    const HarmonicTimeline& timeline, const KeySignature& key_sig,
    const std::vector<NoteEvent>& pedal_notes,
    uint8_t& prev_inner_pitch, std::mt19937& rng,
    const VerticalContext* vctx = nullptr) {
  std::vector<NoteEvent> notes;

  constexpr uint8_t kInnerLow = 48;   // C3
  constexpr uint8_t kInnerHigh = 67;  // G4

  // Dynamic upper limit: stay below cantus.
  uint8_t eff_high = std::min(kInnerHigh,
      static_cast<uint8_t>(std::max(static_cast<int>(kInnerLow),
                                     static_cast<int>(cantus_pitch) - 2)));

  ScaleType scale_type = key_sig.is_minor ? ScaleType::NaturalMinor : ScaleType::Major;

  // Collect scale tones in effective range.
  std::vector<uint8_t> scale_tones;
  for (int pit = kInnerLow; pit <= static_cast<int>(eff_high); ++pit) {
    if (scale_util::isScaleTone(static_cast<uint8_t>(pit), key_sig.tonic, scale_type)) {
      scale_tones.push_back(static_cast<uint8_t>(pit));
    }
  }
  if (scale_tones.empty()) return notes;

  // Density control: base duration adapts to cantus length.
  // Long cantus notes (>= whole) -> eighth notes; shorter -> quarter.
  // Beat-position and cadence convergence applied dynamically in the loop.
  Tick base_note_dur = (cantus_dur >= kWholeNote) ? kEighthNote : kQuarterNote;
  Tick cadence_window_inner = 2 * kTicksPerBeat;

  // Helper: find pedal pitch at a given tick.
  auto pedal_pitch_at = [&pedal_notes](Tick tick) -> uint8_t {
    for (const auto& evt : pedal_notes) {
      if (tick >= evt.start_tick && tick < evt.start_tick + evt.duration) {
        return evt.pitch;
      }
    }
    return 0;
  };

  // Helper: find nearest chord tone in scale_tones.
  auto nearest_chord_tone = [&](Tick tick) -> uint8_t {
    const HarmonicEvent& evt = timeline.getAt(tick);
    uint8_t best = scale_tones[scale_tones.size() / 2];  // Default to middle.
    int best_dist = 999;
    for (uint8_t tone : scale_tones) {
      if (isChordTone(tone, evt)) {
        int dist = (prev_inner_pitch > 0)
            ? std::abs(static_cast<int>(tone) - static_cast<int>(prev_inner_pitch))
            : std::abs(static_cast<int>(tone) - static_cast<int>(cantus_pitch) + 12);
        if (dist < best_dist) {
          best_dist = dist;
          best = tone;
        }
      }
    }
    return best;
  };

  // Helper: resolve by step (half/whole tone down, rarely up).
  auto resolve_step = [&](uint8_t from_pitch, Tick tick) -> uint8_t {
    const HarmonicEvent& evt = timeline.getAt(tick);
    (void)evt;
    // Try down first (standard suspension resolution).
    for (int delta : {-1, -2, 1, 2}) {
      int cand = static_cast<int>(from_pitch) + delta;
      if (cand < kInnerLow || cand > static_cast<int>(eff_high)) continue;
      uint8_t cand_pitch = static_cast<uint8_t>(cand);
      if (!scale_util::isScaleTone(cand_pitch, key_sig.tonic, scale_type)) continue;
      // Resolution must be consonant with cantus.
      int ivl = interval_util::compoundToSimple(
          std::abs(static_cast<int>(cand_pitch) - static_cast<int>(cantus_pitch)));
      if (interval_util::isConsonance(ivl)) {
        // Also check pedal consonance.
        uint8_t ped = pedal_pitch_at(tick);
        if (ped > 0) {
          int ped_ivl = interval_util::compoundToSimple(
              std::abs(static_cast<int>(cand_pitch) - static_cast<int>(ped)));
          if (!interval_util::isConsonance(ped_ivl)) continue;
        }
        return cand_pitch;
      }
    }
    // Fallback: nearest chord tone.
    return nearest_chord_tone(tick);
  };

  InnerFsmState state = InnerFsmState::Prepare;
  Tick current_tick = cantus_tick;
  Tick end_tick = cantus_tick + cantus_dur;

  while (current_tick < end_tick) {
    // Beat-position-aware inner voice duration:
    // Downbeats use longer notes (quarter), middle beats use base duration,
    // cadence regions broaden to quarter/half for convergence with CF.
    uint8_t beat = beatInBar(current_tick);
    bool near_cadence_inner = (end_tick - current_tick) <= cadence_window_inner;
    Tick dur;
    if (near_cadence_inner) {
      dur = kQuarterNote;  // Cadence: converge to quarter notes.
    } else if (beat == 0) {
      dur = kQuarterNote;  // Downbeat: anchor with quarter note.
    } else {
      dur = base_note_dur;  // Use base density for middle beats.
    }
    Tick remaining = end_tick - current_tick;
    if (dur > remaining) dur = remaining;
    if (dur == 0) break;

    bool is_strong = (beat == 0 || beat == 2);
    uint8_t pedal_p = pedal_pitch_at(current_tick);
    uint8_t chosen_pitch = 0;

    switch (state) {
      case InnerFsmState::Prepare: {
        // Place a chord tone (consonant with cantus and pedal).
        chosen_pitch = nearest_chord_tone(current_tick);
        // Verify strong-beat safety.
        if (is_strong && !isStrongBeatSafe(chosen_pitch, cantus_pitch, pedal_p,
                                            prev_inner_pitch, timeline,
                                            current_tick)) {
          // Try alternate chord tones.
          const HarmonicEvent& evt = timeline.getAt(current_tick);
          for (uint8_t tone : scale_tones) {
            if (isChordTone(tone, evt) &&
                isStrongBeatSafe(tone, cantus_pitch, pedal_p, prev_inner_pitch,
                                  timeline, current_tick)) {
              chosen_pitch = tone;
              break;
            }
          }
        }
        // Check weak-beat allowance.
        if (!is_strong && prev_inner_pitch > 0) {
          ChordToneSet next_cts = getNextStrongBeatChordTones(
              timeline, current_tick, kInnerLow, eff_high);
          if (!isWeakBeatAllowed(chosen_pitch, prev_inner_pitch, next_cts,
                                 cantus_pitch)) {
            chosen_pitch = nearest_chord_tone(current_tick);
          }
        }

        // Category A: vctx safety filter — try chord-tone alternatives if unsafe.
        if (vctx && !vctx->isSafe(current_tick, kInnerVoice, chosen_pitch)) {
          const HarmonicEvent& evt = timeline.getAt(current_tick);
          for (uint8_t tone : scale_tones) {
            if (isChordTone(tone, evt) &&
                vctx->isSafe(current_tick, kInnerVoice, tone)) {
              chosen_pitch = tone;
              break;
            }
          }
        }

        // Transition: if next beat is strong and chord changes, consider suspension.
        Tick next_tick = current_tick + dur;
        if (next_tick < end_tick) {
          uint8_t next_beat = beatInBar(next_tick);
          bool next_strong = (next_beat == 0 || next_beat == 2);
          if (next_strong) {
            const HarmonicEvent& curr_ev = timeline.getAt(current_tick);
            const HarmonicEvent& next_ev = timeline.getAt(next_tick);
            // Chord change + current pitch is consonant + will be dissonant.
            if (curr_ev.tick != next_ev.tick &&
                isChordTone(chosen_pitch, curr_ev)) {
              int next_ivl = interval_util::compoundToSimple(
                  std::abs(static_cast<int>(chosen_pitch) -
                           static_cast<int>(cantus_pitch)));
              // Will be dissonant at next strong beat -> prepare for suspension.
              if (!interval_util::isConsonance(next_ivl) ||
                  !isChordTone(chosen_pitch, next_ev)) {
                // Only prepare if we can resolve.
                Tick resolve_tick = next_tick + dur;
                if (resolve_tick < end_tick) {
                  uint8_t resolved = resolve_step(chosen_pitch, resolve_tick);
                  if (resolved != chosen_pitch) {
                    state = InnerFsmState::Dissonance;
                    // Don't change chosen_pitch -- it's the preparation tone.
                  }
                }
              }
            }
          }
        }
        // Occasional suspension attempt (30% probability on prepare beats).
        if (state == InnerFsmState::Prepare &&
            rng::rollProbability(rng, 0.30f)) {
          Tick next_tick2 = current_tick + dur;
          if (next_tick2 < end_tick) {
            uint8_t next_beat2 = beatInBar(next_tick2);
            if (next_beat2 == 0 || next_beat2 == 2) {
              state = InnerFsmState::Dissonance;
            }
          }
        }
        break;
      }

      case InnerFsmState::Dissonance: {
        // Hold previous pitch (suspension) on strong beat.
        if (prev_inner_pitch > 0 && prev_inner_pitch >= kInnerLow &&
            prev_inner_pitch <= eff_high) {
          chosen_pitch = prev_inner_pitch;
          // Verify: is it actually dissonant? If consonant, just treat as Prepare.
          int ivl = interval_util::compoundToSimple(
              std::abs(static_cast<int>(chosen_pitch) -
                       static_cast<int>(cantus_pitch)));
          const HarmonicEvent& evt = timeline.getAt(current_tick);
          if (interval_util::isConsonance(ivl) && isChordTone(chosen_pitch, evt)) {
            // Not actually dissonant -- revert to Prepare.
            state = InnerFsmState::Prepare;
          } else {
            state = InnerFsmState::Resolve;
          }
        } else {
          // No previous pitch to hold -- fall back to Prepare.
          chosen_pitch = nearest_chord_tone(current_tick);
          state = InnerFsmState::Prepare;
        }
        break;
      }

      case InnerFsmState::Resolve: {
        // Resolve by step (half/whole tone, preferring downward).
        if (prev_inner_pitch > 0) {
          chosen_pitch = resolve_step(prev_inner_pitch, current_tick);
        } else {
          chosen_pitch = nearest_chord_tone(current_tick);
        }
        state = InnerFsmState::Prepare;
        break;
      }
    }

    // Final safety clamp.
    chosen_pitch = clampPitch(static_cast<int>(chosen_pitch), kInnerLow, eff_high);

    NoteEvent note;
    note.start_tick = current_tick;
    note.duration = dur;
    note.pitch = chosen_pitch;
    note.velocity = kOrganVelocity;
    note.voice = kInnerVoice;
    note.source = BachNoteSource::FreeCounterpoint;
    notes.push_back(note);

    prev_inner_pitch = chosen_pitch;
    current_tick += dur;
  }

  return notes;
}

// ---------------------------------------------------------------------------
// CF-aware rest policy — thin inner voice texture while preserving CF
// ---------------------------------------------------------------------------

/// @brief Count the number of distinct voices sounding at a given tick.
///
/// Scans all notes to find those whose time range [start_tick, start_tick + duration)
/// contains the query tick.
///
/// @param all_notes All notes across all voices.
/// @param tick The tick position to query.
/// @return Number of distinct voices with notes sounding at this tick.
int countActiveVoicesAt(const std::vector<NoteEvent>& all_notes, Tick tick) {
  bool active[kChoraleVoices] = {};
  int count = 0;
  for (const auto& note : all_notes) {
    if (note.voice >= kChoraleVoices) continue;
    if (active[note.voice]) continue;
    if (tick >= note.start_tick && tick < note.start_tick + note.duration) {
      active[note.voice] = true;
      ++count;
    }
  }
  return count;
}

/// @brief Apply CF-aware rest policy to thin inner voice texture.
///
/// The cantus firmus voice (kCantusVoice) is never modified. Inner voices
/// (voices that are neither the CF nor the bass) have their attack frequency
/// reduced at non-cadence points where texture density exceeds 3 simultaneous
/// voices. This is achieved by extending the previous inner voice note's
/// duration to absorb the current note, effectively creating a tied note
/// instead of a re-attack.
///
/// Only notes with source == FreeCounterpoint (Flexible protection) are
/// candidates for absorption. Cadence points (final 2 bars) always preserve
/// all voices for convergence.
///
/// @param tracks The 4 chorale prelude tracks (modified in place).
/// @param total_duration Total piece duration in ticks.
/// @return Number of inner voice attacks absorbed.
int applyCFAwareRestPolicy(std::vector<Track>& tracks, Tick total_duration) {
  int absorbed = 0;

  // Cadence region: final 2 bars — all voices should converge.
  Tick cadence_start = (total_duration > kTicksPerBar * 2)
      ? total_duration - kTicksPerBar * 2
      : total_duration;

  // Build a flat note list for density queries.
  std::vector<NoteEvent> all_notes;
  for (const auto& track : tracks) {
    all_notes.insert(all_notes.end(), track.notes.begin(), track.notes.end());
  }

  // Process the inner voice track (track index kInnerVoice = 2).
  auto& inner_notes = tracks[kInnerVoice].notes;
  if (inner_notes.size() < 2) return 0;

  // Sort inner notes by start_tick for sequential processing.
  std::sort(inner_notes.begin(), inner_notes.end(),
            [](const NoteEvent& lhs, const NoteEvent& rhs) {
              return lhs.start_tick < rhs.start_tick;
            });

  // Mark notes for removal by setting duration to 0.
  for (size_t idx = 1; idx < inner_notes.size(); ++idx) {
    auto& current = inner_notes[idx];

    // Never absorb in cadence region.
    if (current.start_tick >= cadence_start) continue;

    // Only absorb Flexible-protection notes.
    if (current.source != BachNoteSource::FreeCounterpoint) continue;

    // Check density at this note's start tick.
    int density = countActiveVoicesAt(all_notes, current.start_tick);
    if (density <= 3) continue;

    // Absorb: extend previous note's duration to cover this one.
    auto& previous = inner_notes[idx - 1];
    Tick extended_end = current.start_tick + current.duration;
    Tick prev_end = previous.start_tick + previous.duration;
    if (extended_end > prev_end) {
      previous.duration = extended_end - previous.start_tick;
    }

    // Mark current note for removal.
    current.duration = 0;
    ++absorbed;

    // Skip every other note at most — preserve some inner voice motion.
    // This ensures we do not absorb consecutive notes, keeping texture alive.
    ++idx;
  }

  // Remove absorbed notes (duration == 0).
  inner_notes.erase(
      std::remove_if(inner_notes.begin(), inner_notes.end(),
                     [](const NoteEvent& note) { return note.duration == 0; }),
      inner_notes.end());

  if (absorbed > 0) {
    fprintf(stderr,
            "[ChoralePrelude] CF-aware rest policy: absorbed %d inner voice"
            " attacks to thin texture\n", absorbed);
  }

  return absorbed;
}

}  // namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

ChoralePreludeResult generateChoralePrelude(const ChoralePreludeConfig& config) {
  ChoralePreludeResult result;
  result.success = false;

  // Step 1: Select chorale melody based on seed.
  int chorale_index = static_cast<int>(config.seed % kChoraleCount);
  const ChoraleMelody& melody = kChorales[chorale_index];

  // Step 2: Calculate total duration from the cantus firmus.
  Tick total_duration = calculateChoraleDuration(melody);
  if (total_duration == 0) {
    return result;
  }
  result.total_duration_ticks = total_duration;

  // Step 3: Create cantus-driven harmonic timeline.
  HarmonicTimeline timeline = createChoraleTimeline(melody, config.key);

  // Step 4: Create tracks.
  std::vector<Track> tracks = createChoralePreludeTracks();

  // Step 5: Place cantus firmus on Swell (track 1).
  placeCantus(melody, tracks[1]);
  addCantusPassingTones(tracks[1], {60, 71},
                        config.key.tonic, config.key.is_minor);

  // Step 6: Generate pedal, inner voice, and figuration for each cantus note.
  // Order: pedal first (references cantus only), inner voice (references cantus
  // + pedal), figuration last (references cantus + pedal + inner).
  //
  // VerticalContext wiring: placed_notes grows as voices are generated.
  // Each subsequent voice sees all previously placed notes for vertical safety.
  std::mt19937 rng(config.seed);
  Tick cantus_tick = 0;
  uint8_t fig_center = 0;      // Figuration center hint (0 = first call).
  uint8_t inner_center = 0;    // Inner voice previous pitch (0 = first call).
  FigurationMotif fig_motif;   // Motif extracted from first figuration segment.

  // Seed placed_notes with cantus (immutable, placed first).
  std::vector<NoteEvent> placed_notes;
  placed_notes.reserve(tracks[kCantusVoice].notes.size() * 4);
  for (const auto& note : tracks[kCantusVoice].notes) {
    placed_notes.push_back(note);
  }

  VerticalContext vctx;
  vctx.placed_notes = &placed_notes;
  vctx.timeline = &timeline;
  vctx.num_voices = kChoraleVoices;

  for (size_t idx = 0; idx < melody.note_count; ++idx) {
    Tick cantus_dur =
        static_cast<Tick>(melody.notes[idx].duration_beats) * kTicksPerBeat;
    uint8_t cantus_pitch = clampPitch(
        static_cast<int>(melody.notes[idx].pitch), 60, 71);

    // 1. Pedal first (references cantus only) — track 3.
    auto pedal_notes = generatePedalBass(cantus_tick, cantus_dur, timeline,
                                         total_duration, cantus_pitch,
                                         nullptr, rng);
    for (auto& note : pedal_notes) {
      tracks[3].notes.push_back(note);
      placed_notes.push_back(note);
    }

    // 2. Inner voice (references cantus + pedal via vctx) — track 2.
    auto inner_notes = generateInnerVoice(cantus_tick, cantus_dur, cantus_pitch,
                                           timeline, config.key, tracks[3].notes,
                                           inner_center, rng, &vctx);
    for (auto& note : inner_notes) {
      tracks[2].notes.push_back(note);
      placed_notes.push_back(note);
    }

    // 3. Figuration last (references cantus + pedal + inner via vctx) — track 0.
    auto fig_notes = generateFiguration(cantus_tick, cantus_dur, timeline,
                                        config.key, cantus_pitch,
                                        fig_center, rng,
                                        fig_motif.valid ? &fig_motif : nullptr,
                                        &vctx);
    // Extract motif from first segment.
    if (idx == 0 && !fig_motif.valid && fig_notes.size() >= 3) {
      fig_motif = extractMotif(fig_notes);
    }
    for (auto& note : fig_notes) {
      tracks[0].notes.push_back(note);
      placed_notes.push_back(note);
    }

    cantus_tick += cantus_dur;
  }

  // Step 6a: CF-aware rest policy — thin inner voice texture while preserving CF.
  // The cantus firmus voice is never modified. Inner voice attacks are absorbed
  // (extended duration) at non-cadence points where texture density > 3 voices.
  applyCFAwareRestPolicy(tracks, total_duration);

  // Step 6b: Post-validate through counterpoint engine.
  {
    std::vector<NoteEvent> all_notes;
    for (const auto& track : tracks) {
      all_notes.insert(all_notes.end(), track.notes.begin(), track.notes.end());
    }

    // Tag sources: cantus = CantusFixed (already tagged), figuration = FreeCounterpoint,
    // inner = FreeCounterpoint, pedal = PedalPoint.
    assert(countUnknownSource(all_notes) == 0 &&
           "All notes should have source set by generators");

    std::vector<std::pair<uint8_t, uint8_t>> voice_ranges = {
        {72, 88},  // Voice 0: Great figuration (C5-E6, soprano)
        {60, 71},  // Voice 1: Swell cantus (C4-B4, alto/tenor)
        {48, 67},  // Voice 2: Inner voice (C3-G4)
        {organ_range::kPedalLow, organ_range::kPedalHigh}};  // Voice 3: Pedal

    // ---- Pre-filter + unified coordination pass ----
    {
      // Cantus pitch lookup for crossing detection.
      auto cantus_pitch_at = [&tracks](Tick tick) -> int {
        for (const auto& n : tracks[kCantusVoice].notes) {
          if (tick >= n.start_tick && tick < n.start_tick + n.duration) {
            return static_cast<int>(n.pitch);
          }
        }
        return -1;
      };

      // Pre-filter: reject inner voice out of range, figuration crossing/range/harmony.
      int fig_crossing = 0, fig_range = 0, fig_harmony = 0;
      all_notes.erase(
          std::remove_if(
              all_notes.begin(), all_notes.end(),
              [&](const NoteEvent& note) {
                if (note.source == BachNoteSource::CantusFixed ||
                    note.source == BachNoteSource::PedalPoint) {
                  return false;
                }

                // Inner voice: range check only.
                if (note.voice == kInnerVoice) {
                  return note.pitch < voice_ranges[kInnerVoice].first ||
                         note.pitch > voice_ranges[kInnerVoice].second;
                }

                // Figuration: crossing + range + harmony.
                int cantus_p = cantus_pitch_at(note.start_tick);
                if (cantus_p >= 0 &&
                    static_cast<int>(note.pitch) <= cantus_p) {
                  ++fig_crossing;
                  return true;
                }
                if (note.pitch < voice_ranges[kFigurationVoice].first ||
                    note.pitch > voice_ranges[kFigurationVoice].second) {
                  ++fig_range;
                  return true;
                }
                if (note.start_tick % kTicksPerBeat == 0) {
                  const HarmonicEvent& harm =
                      timeline.getAt(note.start_tick);
                  if (!isChordTone(note.pitch, harm)) {
                    ++fig_harmony;
                    return true;
                  }
                }
                return false;
              }),
          all_notes.end());

      fprintf(stderr,
              "[ChoralePrelude] figuration pre-filter:"
              " crossing=%d range=%d harmony=%d\n",
              fig_crossing, fig_range, fig_harmony);

      // Unified coordination pass — all remaining notes are pre-validated.
      CoordinationConfig coord_config;
      coord_config.num_voices = kChoraleVoices;
      coord_config.tonic = config.key.tonic;
      coord_config.timeline = &timeline;
      coord_config.voice_range =
          [&voice_ranges](uint8_t v) -> std::pair<uint8_t, uint8_t> {
        if (v < voice_ranges.size()) return voice_ranges[v];
        return {36, 96};
      };
      coord_config.immutable_sources = {BachNoteSource::CantusFixed,
                                        BachNoteSource::PedalPoint,
                                        BachNoteSource::FreeCounterpoint};
      coord_config.priority = [](const NoteEvent& n) -> int {
        if (n.source == BachNoteSource::CantusFixed) return 0;
        if (n.source == BachNoteSource::PedalPoint) return 1;
        if (n.voice == kInnerVoice) return 2;
        return 3;
      };
      coord_config.form_name = "ChoralePrelude";
      auto form_profile = getFormProfile(FormType::ChoralePrelude);
      coord_config.dissonance_policy = form_profile.dissonance_policy;
      all_notes = coordinateVoices(std::move(all_notes), coord_config);

      // Post-rejection repeat mitigation (weak-beat only, chord-tone-aware).
      // Shifts repeated figuration notes by nearest scale step to prevent
      // consecutive same-pitch merges. Strong beats are exempt to avoid
      // reintroducing dissonance on metrically accented positions.
      {
        // Build scale-tone lookup for index-based shifting.
        ScaleType shift_scale = config.key.is_minor ? ScaleType::NaturalMinor
                                                    : ScaleType::Major;
        std::vector<uint8_t> shift_tones;
        for (int p = voice_ranges[kFigurationVoice].first;
             p <= voice_ranges[kFigurationVoice].second; ++p) {
          if (scale_util::isScaleTone(static_cast<uint8_t>(p), config.key.tonic,
                                      shift_scale)) {
            shift_tones.push_back(static_cast<uint8_t>(p));
          }
        }

        uint8_t last_fig_pitch = 0;
        bool has_last_fig = false;
        for (auto& note : all_notes) {
          if (note.voice != kFigurationVoice) continue;
          if (has_last_fig && note.pitch == last_fig_pitch) {
            // Strong beats: only shift to a chord tone (harmonic safety).
            // Weak beats: shift to any scale tone.
            bool on_strong_beat = (note.start_tick % kTicksPerBeat == 0);

            // Find exact position in shift_tones.
            auto it = std::find(shift_tones.begin(), shift_tones.end(),
                                note.pitch);
            if (it != shift_tones.end()) {
              size_t st_idx = static_cast<size_t>(
                  std::distance(shift_tones.begin(), it));

              // Try adjacent scale-tone indices: +/-1, +/-2.
              for (int d : {1, -1, 2, -2}) {
                int cand_idx = static_cast<int>(st_idx) + d;
                if (cand_idx < 0 ||
                    cand_idx >= static_cast<int>(shift_tones.size())) continue;
                uint8_t cand = shift_tones[static_cast<size_t>(cand_idx)];
                if (cand == last_fig_pitch) continue;

                if (on_strong_beat) {
                  // Strong beat: require chord tone.
                  const HarmonicEvent& harm = timeline.getAt(note.start_tick);
                  if (!isChordTone(cand, harm)) continue;
                }

                note.pitch = cand;
                break;
              }
              // If no safe shift found, leave unchanged (merge will handle it).
            }
          }
          last_fig_pitch = note.pitch;
          has_last_fig = true;
        }
      }

      // Merge consecutive repeated pitches in figuration into single
      // held notes (Baroque notation convention: re-attacks → sustained).
      {
        std::vector<NoteEvent> merged;
        merged.reserve(all_notes.size());
        for (const auto& n : all_notes) {
          if (n.voice == kFigurationVoice && !merged.empty() &&
              merged.back().voice == kFigurationVoice &&
              merged.back().pitch == n.pitch &&
              merged.back().start_tick + merged.back().duration ==
                  n.start_tick) {
            merged.back().duration += n.duration;
          } else {
            merged.push_back(n);
          }
        }
        int merged_count = static_cast<int>(all_notes.size() - merged.size());
        if (merged_count > 0) {
          fprintf(stderr,
                  "[ChoralePrelude] merged %d consecutive repeated figuration"
                  " notes\n", merged_count);
        }
        all_notes = std::move(merged);
      }
    }

    // ---- Voice pitch ordering check (median-based) ----
    {
      std::vector<std::vector<uint8_t>> voice_pitches(kChoraleVoices);
      for (const auto& n : all_notes) {
        if (n.voice < kChoraleVoices) {
          voice_pitches[n.voice].push_back(n.pitch);
        }
      }

      float medians[4] = {0, 0, 0, 0};
      for (uint8_t v = 0; v < kChoraleVoices; ++v) {
        auto& vp = voice_pitches[v];
        if (vp.size() >= 5) {
          std::sort(vp.begin(), vp.end());
          size_t mid = vp.size() / 2;
          medians[v] = (vp.size() % 2 == 0)
                           ? (vp[mid - 1] + vp[mid]) / 2.0f
                           : static_cast<float>(vp[mid]);
          float q25 = static_cast<float>(vp[vp.size() / 4]);
          float q75 = static_cast<float>(vp[vp.size() * 3 / 4]);
          fprintf(stderr,
                  "[ChoralePrelude] voice %d: median=%.0f IQR=[%.0f, %.0f] n=%zu\n",
                  v, medians[v], q25, q75, vp.size());
        } else {
          fprintf(stderr, "[ChoralePrelude] voice %d: n=%zu (skipped, < 5 notes)\n",
                  v, vp.size());
        }
      }

      // Check voice ordering: figuration > cantus > inner > pedal.
      if (voice_pitches[0].size() >= 5 && voice_pitches[1].size() >= 5 &&
          medians[0] <= medians[1]) {
        fprintf(stderr,
                "[ChoralePrelude] WARNING: voice 0 median (%.0f) <= "
                "voice 1 median (%.0f)\n",
                medians[0], medians[1]);
      }
      if (voice_pitches[1].size() >= 5 && voice_pitches[2].size() >= 5 &&
          medians[1] <= medians[2]) {
        fprintf(stderr,
                "[ChoralePrelude] WARNING: voice 1 median (%.0f) <= "
                "voice 2 median (%.0f)\n",
                medians[1], medians[2]);
      }
      if (voice_pitches[2].size() >= 5 && voice_pitches[3].size() >= 5 &&
          medians[2] <= medians[3]) {
        fprintf(stderr,
                "[ChoralePrelude] WARNING: voice 2 median (%.0f) <= "
                "voice 3 median (%.0f)\n",
                medians[2], medians[3]);
      }
    }

    // ---- Figuration pitch entropy check ----
    {
      int hist[12] = {};
      int fig_note_count = 0;
      uint8_t fig_min = 127, fig_max = 0;
      for (const auto& n : all_notes) {
        if (n.voice == kFigurationVoice) {
          hist[getPitchClass(n.pitch)]++;
          ++fig_note_count;
          if (n.pitch < fig_min) fig_min = n.pitch;
          if (n.pitch > fig_max) fig_max = n.pitch;
        }
      }
      if (fig_note_count > 0) {
        float h = 0.0f;
        int unique_pc = 0;
        for (int i = 0; i < 12; ++i) {
          if (hist[i] > 0) {
            ++unique_pc;
            float p = static_cast<float>(hist[i]) / fig_note_count;
            h -= p * std::log2(p);
          }
        }
        fprintf(stderr,
                "[ChoralePrelude] figuration entropy=%.2f unique_pc=%d"
                " range=%d (pitch %d-%d)\n",
                h, unique_pc, fig_max - fig_min, fig_min, fig_max);
        if (h < 2.0f) {
          fprintf(stderr,
                  "[ChoralePrelude] WARNING: low figuration entropy"
                  " (%.2f < 2.0)\n", h);
        }
        if (fig_max - fig_min < 12) {
          fprintf(stderr,
                  "[ChoralePrelude] WARNING: narrow figuration range"
                  " (%d < 12)\n", fig_max - fig_min);
        }
      }
    }

    ProtectionOverrides overrides = {{kPedalVoice, ProtectionLevel::Immutable}};
    PostValidateStats stats;
    auto validated = postValidateNotes(
        std::move(all_notes), kChoraleVoices, config.key, voice_ranges, &stats,
        overrides);

    // Leap resolution: fix unresolved melodic leaps.
    {
      LeapResolutionParams lr_params;
      lr_params.num_voices = kChoraleVoices;
      lr_params.key_at_tick = [&](Tick) { return config.key.tonic; };
      lr_params.scale_at_tick = [&](Tick t) {
        const auto& ev = timeline.getAt(t);
        return ev.is_minor ? ScaleType::HarmonicMinor : ScaleType::Major;
      };
      lr_params.voice_range_static = [&](uint8_t v) -> std::pair<uint8_t, uint8_t> {
        if (v < voice_ranges.size()) return voice_ranges[v];
        return {0, 127};
      };
      lr_params.is_chord_tone = [&](Tick t, uint8_t p) {
        return isChordTone(p, timeline.getAt(t));
      };
      lr_params.vertical_safe =
          makeVerticalSafeWithParallelCheck(timeline, validated, kChoraleVoices);
      resolveLeaps(validated, lr_params);

      // Second parallel-perfect repair pass after leap resolution.
      {
        ParallelRepairParams pp_params;
        pp_params.num_voices = kChoraleVoices;
        pp_params.scale = config.key.is_minor ? ScaleType::HarmonicMinor : ScaleType::Major;
        pp_params.key_at_tick = lr_params.key_at_tick;
        pp_params.voice_range_static = lr_params.voice_range_static;
        pp_params.max_iterations = 2;
        repairParallelPerfect(validated, pp_params);
      }
    }

    for (auto& track : tracks) {
      track.notes.clear();
    }
    for (auto& note : validated) {
      if (note.voice < kChoraleVoices) {
        tracks[note.voice].notes.push_back(std::move(note));
      }
    }

    // ---- Verification metrics (Step 8) ----

    // Pedal coverage metric.
    {
      Tick pedal_covered = 0;
      for (const auto& evt : tracks[3].notes) {
        pedal_covered += evt.duration;
      }
      float pedal_coverage = total_duration > 0
          ? 100.0f * static_cast<float>(pedal_covered) / total_duration
          : 0.0f;
      fprintf(stderr, "[ChoralePrelude] pedal coverage=%.0f%%\n", pedal_coverage);
    }

    // Strong-beat dissonance rate (among upper 3 voices).
    {
      int strong_beat_total = 0;
      int strong_beat_dissonant = 0;
      Tick cadence_start = total_duration > kTicksPerBar * 2
          ? total_duration - kTicksPerBar * 2 : 0;

      // Rebuild validated from tracks for iteration (original was moved-from).
      std::vector<NoteEvent> all_validated;
      for (const auto& track : tracks) {
        all_validated.insert(all_validated.end(),
                             track.notes.begin(), track.notes.end());
      }

      for (const auto& evt : all_validated) {
        if (evt.voice == kCantusVoice) continue;  // Skip cantus (reference).
        uint8_t beat = beatInBar(evt.start_tick);
        if (beat != 0 && beat != 2) continue;  // Only strong beats.
        if (evt.start_tick >= cadence_start) continue;  // Exclude cadence window.

        ++strong_beat_total;
        // Find cantus pitch at this tick.
        int cantus_p = -1;
        for (const auto& cn : tracks[kCantusVoice].notes) {
          if (evt.start_tick >= cn.start_tick &&
              evt.start_tick < cn.start_tick + cn.duration) {
            cantus_p = cn.pitch;
            break;
          }
        }
        if (cantus_p >= 0) {
          int ivl = interval_util::compoundToSimple(
              std::abs(static_cast<int>(evt.pitch) - cantus_p));
          if (!interval_util::isConsonance(ivl)) {
            ++strong_beat_dissonant;
          }
        }
      }

      float dissonance_rate = strong_beat_total > 0
          ? 100.0f * strong_beat_dissonant / strong_beat_total
          : 0.0f;
      fprintf(stderr,
              "[ChoralePrelude] strong-beat dissonance=%.0f%% (%d/%d)"
              " [target: <=10%%]\n",
              dissonance_rate, strong_beat_dissonant, strong_beat_total);
    }

    // Inner voice presence.
    fprintf(stderr, "[ChoralePrelude] inner voice notes=%zu\n",
            tracks[2].notes.size());
  }

  // ---------------------------------------------------------------------------
  // Shared organ techniques: Picardy, registration
  // ---------------------------------------------------------------------------

  // Picardy third (minor keys only).
  if (config.enable_picardy && config.key.is_minor && total_duration > kTicksPerBar) {
    for (auto& track : tracks) {
      applyPicardyToFinalChord(track.notes, config.key,
                               total_duration - kTicksPerBar);
    }
  }

  // 2-point registration (mezzo -> forte).
  {
    ExtendedRegistrationPlan reg_plan;
    reg_plan.addPoint(0, OrganRegistrationPresets::mezzo(), "opening");
    Tick mid = total_duration / 2;
    reg_plan.addPoint(mid, OrganRegistrationPresets::forte(), "closing");
    applyExtendedRegistrationPlan(tracks, reg_plan);
  }

  // Step 7: Sort notes within each track by start_tick.
  form_utils::sortTrackNotes(tracks);

  // Final parallel repair pass: catch parallels introduced by post-processing
  // (Picardy third, registration changes).
  {
    std::vector<NoteEvent> final_notes;
    for (const auto& track : tracks) {
      final_notes.insert(final_notes.end(), track.notes.begin(), track.notes.end());
    }

    // Count melodic direction changes before repair for sanity check.
    auto countDirectionChanges = [](const std::vector<NoteEvent>& notes,
                                    uint8_t nv) -> int {
      std::vector<std::vector<uint8_t>> voice_pitches(nv);
      std::vector<std::vector<Tick>> voice_ticks(nv);
      for (const auto& note : notes) {
        if (note.voice < nv) {
          voice_pitches[note.voice].push_back(note.pitch);
          voice_ticks[note.voice].push_back(note.start_tick);
        }
      }
      int changes = 0;
      for (uint8_t vid = 0; vid < nv; ++vid) {
        auto& pitches = voice_pitches[vid];
        auto& ticks = voice_ticks[vid];
        if (pitches.size() < 3) continue;
        std::vector<size_t> order(pitches.size());
        for (size_t idx = 0; idx < order.size(); ++idx) order[idx] = idx;
        std::sort(order.begin(), order.end(),
                  [&ticks](size_t lhs, size_t rhs) {
                    return ticks[lhs] < ticks[rhs];
                  });
        int prev_dir = 0;
        for (size_t idx = 1; idx < order.size(); ++idx) {
          int diff = static_cast<int>(pitches[order[idx]]) -
                     static_cast<int>(pitches[order[idx - 1]]);
          int dir = (diff > 0) ? 1 : ((diff < 0) ? -1 : 0);
          if (dir != 0 && prev_dir != 0 && dir != prev_dir) ++changes;
          if (dir != 0) prev_dir = dir;
        }
      }
      return changes;
    };

    int dir_changes_before = countDirectionChanges(final_notes, kChoraleVoices);

    ParallelRepairParams pp_final;
    pp_final.num_voices = kChoraleVoices;
    pp_final.scale = config.key.is_minor ? ScaleType::HarmonicMinor : ScaleType::Major;
    pp_final.key_at_tick = [&](Tick) { return config.key.tonic; };
    // Voice ranges: v0=figuration(C5-E6), v1=cantus(C4-B4),
    // v2=inner(C3-G4), v3=pedal(C1-D3).
    static constexpr std::pair<uint8_t, uint8_t> kChoraleRanges[4] = {
        {72, 88}, {60, 71}, {48, 67},
        {organ_range::kPedalLow, organ_range::kPedalHigh}};
    pp_final.voice_range_static =
        [](uint8_t vid) -> std::pair<uint8_t, uint8_t> {
      if (vid < kChoraleVoices) return kChoraleRanges[vid];
      return {0, 127};
    };
    pp_final.max_iterations = 2;
    repairParallelPerfect(final_notes, pp_final);

    int dir_changes_after = countDirectionChanges(final_notes, kChoraleVoices);
    if (dir_changes_before > 0 &&
        dir_changes_after > dir_changes_before * 120 / 100) {
      fprintf(stderr,
              "[ChoralePrelude] WARNING: final parallel repair increased "
              "direction changes %d -> %d (+%.0f%%)\n",
              dir_changes_before, dir_changes_after,
              100.0f * (dir_changes_after - dir_changes_before) /
                  dir_changes_before);
    }

    // Redistribute repaired notes back to tracks.
    for (auto& track : tracks) track.notes.clear();
    for (auto& note : final_notes) {
      if (note.voice < kChoraleVoices) {
        tracks[note.voice].notes.push_back(std::move(note));
      }
    }
    form_utils::sortTrackNotes(tracks);
  }

  result.tracks = std::move(tracks);
  result.timeline = std::move(timeline);
  result.total_duration_ticks = total_duration;
  result.success = true;

  return result;
}

}  // namespace bach
