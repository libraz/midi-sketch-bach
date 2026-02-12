// Implementation of the chorale prelude generator (BWV 599-650 style).

#include "forms/chorale_prelude.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

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
constexpr uint8_t kPedalVoice = 2;       // Pedal: bass

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

/// @brief Create the 3 MIDI tracks for a chorale prelude.
///
/// Track layout:
///   Track 0: Counterpoint voice on Great (ch 0, Church Organ).
///   Track 1: Cantus firmus on Swell (ch 1, Reed Organ).
///   Track 2: Pedal bass (ch 3, Church Organ).
///
/// @return Vector of 3 Track objects with channel/program/name configured.
std::vector<Track> createChoralePreludeTracks() {
  std::vector<Track> tracks;
  tracks.reserve(3);

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

/// @brief Create a HarmonicTimeline driven by cantus firmus pitches.
///
/// Each cantus note maps to a harmonic event using scale degree analysis.
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

    // Bass pitch from chord root (not raw cantus pitch — may be inverted).
    uint8_t bass_pitch = chord.root_pitch;

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
                                          std::mt19937& rng) {
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
    // Blend toward raw_center for continuity between cantus notes.
    center = static_cast<uint8_t>(
        (static_cast<int>(hint_center) + static_cast<int>(raw_center)) / 2);
    center = clampPitch(static_cast<int>(center), kFigLow, kFigHigh);
  }

  uint8_t eff_low = clampPitch(static_cast<int>(center) - 9, kFigLow, kFigHigh);
  uint8_t eff_high = clampPitch(static_cast<int>(center) + 9, kFigLow, kFigHigh);

  ScaleType scale_type = key_sig.is_minor ? ScaleType::HarmonicMinor
                                          : ScaleType::Major;

  // Collect scale tones in the effective range.
  std::vector<uint8_t> scale_tones;
  for (int p = static_cast<int>(eff_low); p <= static_cast<int>(eff_high); ++p) {
    if (scale_util::isScaleTone(static_cast<uint8_t>(p), key_sig.tonic,
                                scale_type)) {
      scale_tones.push_back(static_cast<uint8_t>(p));
    }
  }

  if (scale_tones.empty()) {
    return notes;
  }

  Tick current_tick = cantus_tick;
  Tick end_tick = cantus_tick + cantus_dur;

  // Start near center pitch.
  size_t tone_idx = 0;
  int min_dist = 999;
  for (size_t i = 0; i < scale_tones.size(); ++i) {
    int d = std::abs(static_cast<int>(scale_tones[i]) -
                     static_cast<int>(center));
    if (d < min_dist) {
      min_dist = d;
      tone_idx = i;
    }
  }

  bool ascending = rng::rollProbability(rng, 0.5f);
  int direction_count = 0;
  int direction_target = 3 + rng::rollRange(rng, 0, 1);
  Tick prev_harm_start = timeline.getAt(cantus_tick).tick;
  int neighbor_return = -1;  // Saved tone_idx for neighbor return, -1 = none.

  while (current_tick < end_tick) {
    // 3a: Duration from {kSixteenthNote, kEighthNote, kDottedEighth}.
    Tick dur;
    int rval = rng::rollRange(rng, 0, 9);
    if (rval < 3) {
      dur = kSixteenthNote;
    } else if (rval < 8) {
      dur = kEighthNote;
    } else {
      dur = kDottedEighth;
    }
    Tick remaining = end_tick - current_tick;
    if (dur > remaining) dur = remaining;
    if (dur == 0) break;

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
        // Avoid repeat: if snap recreates previous pitch, revert to
        // allow passing tone rather than stagnation.
        if (found && !notes.empty() &&
            scale_tones[tone_idx] == notes.back().pitch) {
          tone_idx = saved_idx;
        }
      }
    }

    // Avoid consecutive pitch repeats — nudge in current direction.
    // Baroque figuration uses held notes for repeats, not re-attacks.
    if (!notes.empty() && scale_tones[tone_idx] == notes.back().pitch) {
      if (ascending && tone_idx + 1 < scale_tones.size()) {
        tone_idx += 1;
      } else if (!ascending && tone_idx >= 1) {
        tone_idx -= 1;
      } else if (tone_idx + 1 < scale_tones.size()) {
        tone_idx += 1;
      } else if (tone_idx >= 1) {
        tone_idx -= 1;
      }
    }

    NoteEvent note;
    note.start_tick = current_tick;
    note.duration = dur;
    note.pitch = scale_tones[tone_idx];
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

    // 3c: 20% neighbor tone — step away by 1, return next iteration.
    if (!did_step && rng::rollProbability(rng, 0.2f)) {
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

    // Normal stepping with directional persistence.
    if (!did_step) {
      int step = rng::rollProbability(rng, 0.25f) ? 2 : 1;
      ++direction_count;

      bool hit_boundary = false;
      if (ascending) {
        if (tone_idx + step < scale_tones.size()) {
          tone_idx += step;
        } else {
          // Range boundary: always reverse and step back to avoid repeats.
          hit_boundary = true;
          ascending = false;
          if (tone_idx >= static_cast<size_t>(step)) {
            tone_idx -= step;
          } else {
            tone_idx = 0;
          }
          direction_count = 0;
          direction_target = 3 + rng::rollRange(rng, 0, 1);
        }
      } else {
        if (tone_idx >= static_cast<size_t>(step)) {
          tone_idx -= step;
        } else {
          hit_boundary = true;
          ascending = true;
          if (tone_idx + step < scale_tones.size()) {
            tone_idx += step;
          } else if (!scale_tones.empty()) {
            tone_idx = scale_tones.size() - 1;
          }
          direction_count = 0;
          direction_target = 3 + rng::rollRange(rng, 0, 1);
        }
      }

      // 3c: Random direction reversal — only when persistence target met
      // and not at a harmonic boundary (boundary reversals handled above).
      if (!hit_boundary) {
        bool can_reverse = direction_count >= direction_target &&
                           !at_harmony_boundary;
        if (can_reverse && rng::rollProbability(rng, 0.2f)) {
          ascending = !ascending;
          direction_count = 0;
          direction_target = 3 + rng::rollRange(rng, 0, 1);
        }
      }
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
/// @param cantus_tick Start tick of the cantus note.
/// @param cantus_dur Duration of the cantus note in ticks.
/// @param timeline Harmonic timeline for chord context.
/// @param piece_end_tick End tick of the entire piece (for cadence detection).
/// @param rng Mersenne Twister RNG instance.
/// @return Vector of NoteEvents for the pedal voice.
std::vector<NoteEvent> generatePedalBass(Tick cantus_tick, Tick cantus_dur,
                                         const HarmonicTimeline& timeline,
                                         Tick piece_end_tick,
                                         std::mt19937& rng) {
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

    if (chosen_pitch == octave && octave != bass) {
      ++consecutive_octave;
    } else {
      consecutive_octave = 0;
    }

    Tick dur = rng::rollProbability(rng, 0.5f) ? kQuarterNote : kHalfNote;
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

  // Step 6: Generate counterpoint and pedal for each cantus note.
  std::mt19937 rng(config.seed);
  Tick cantus_tick = 0;
  uint8_t fig_center = 0;  // Figuration center hint (0 = first call).

  for (size_t idx = 0; idx < melody.note_count; ++idx) {
    Tick cantus_dur =
        static_cast<Tick>(melody.notes[idx].duration_beats) * kTicksPerBeat;

    // Counterpoint figuration on Great (track 0).
    uint8_t cantus_pitch = clampPitch(
        static_cast<int>(melody.notes[idx].pitch), 60, 71);
    auto fig_notes = generateFiguration(cantus_tick, cantus_dur, timeline,
                                        config.key, cantus_pitch,
                                        fig_center, rng);
    for (auto& note : fig_notes) {
      tracks[0].notes.push_back(note);
    }

    // Pedal bass (track 2).
    auto pedal_notes = generatePedalBass(cantus_tick, cantus_dur, timeline,
                                         total_duration, rng);
    for (auto& note : pedal_notes) {
      tracks[2].notes.push_back(note);
    }

    cantus_tick += cantus_dur;
  }

  // Step 6b: Post-validate through counterpoint engine.
  {
    constexpr uint8_t kChoraleVoices = 3;
    std::vector<NoteEvent> all_notes;
    for (const auto& track : tracks) {
      all_notes.insert(all_notes.end(), track.notes.begin(), track.notes.end());
    }

    // Tag sources: cantus = CantusFixed (already tagged), figuration = FreeCounterpoint,
    // pedal = PedalPoint.
    for (auto& n : all_notes) {
      if (n.source == BachNoteSource::Unknown) {
        n.source = isPedalVoice(n.voice, kChoraleVoices) ? BachNoteSource::PedalPoint
                                  : BachNoteSource::FreeCounterpoint;
      }
    }

    std::vector<std::pair<uint8_t, uint8_t>> voice_ranges = {
        {72, 88},  // Voice 0: Great figuration (C5-E6, soprano)
        {60, 71},  // Voice 1: Swell cantus (C4-B4, alto/tenor)
        {organ_range::kPedalLow, organ_range::kPedalHigh}};    // Voice 2: Pedal

    // ---- createBachNote coordination pass ----
    {
      BachRuleEvaluator cp_rules(kChoraleVoices);
      cp_rules.setFreeCounterpoint(true);
      CollisionResolver cp_resolver;
      cp_resolver.setHarmonicTimeline(&timeline);
      CounterpointState cp_state;
      cp_state.setKey(config.key.tonic);
      for (uint8_t v = 0; v < kChoraleVoices; ++v) {
        cp_state.registerVoice(v, voice_ranges[v].first, voice_ranges[v].second);
      }

      std::sort(all_notes.begin(), all_notes.end(),
                [](const NoteEvent& a, const NoteEvent& b) {
                  return a.start_tick < b.start_tick;
                });

      // Pre-compute cantus pitch lookup for crossing detection.
      auto cantus_pitch_at = [&tracks](Tick tick) -> int {
        for (const auto& n : tracks[kCantusVoice].notes) {
          if (tick >= n.start_tick && tick < n.start_tick + n.duration) {
            return static_cast<int>(n.pitch);
          }
        }
        return -1;
      };

      std::vector<NoteEvent> coordinated;
      coordinated.reserve(all_notes.size());
      int accepted_count = 0;
      int total_count = 0;
      int fig_accepted = 0;
      int fig_crossing = 0;
      int fig_range = 0;
      int fig_harmony = 0;
      int fig_other = 0;

      size_t note_idx = 0;
      while (note_idx < all_notes.size()) {
        Tick current_tick = all_notes[note_idx].start_tick;
        size_t group_end = note_idx;
        while (group_end < all_notes.size() &&
               all_notes[group_end].start_tick == current_tick) {
          ++group_end;
        }

        // Priority: cantus (immutable) -> pedal -> figuration.
        std::sort(all_notes.begin() + static_cast<ptrdiff_t>(note_idx),
                  all_notes.begin() + static_cast<ptrdiff_t>(group_end),
                  [](const NoteEvent& a, const NoteEvent& b) {
                    auto priority = [](const NoteEvent& n) -> int {
                      if (n.source == BachNoteSource::CantusFixed) return 0;
                      if (n.source == BachNoteSource::PedalPoint) return 1;
                      return 2;
                    };
                    return priority(a) < priority(b);
                  });

        for (size_t j = note_idx; j < group_end; ++j) {
          const auto& note = all_notes[j];
          ++total_count;

          // Cantus and pedal are immutable — register directly.
          if (note.source == BachNoteSource::CantusFixed ||
              note.source == BachNoteSource::PedalPoint) {
            cp_state.addNote(note.voice, note);
            coordinated.push_back(note);
            ++accepted_count;
            continue;
          }

          // Figuration rejection classification (short-circuit).
          // (A) Crossing: figuration pitch at or below cantus.
          int cantus_p = cantus_pitch_at(note.start_tick);
          if (cantus_p >= 0 && static_cast<int>(note.pitch) <= cantus_p) {
            ++fig_crossing;
            continue;
          }

          // (B) Range: outside registered voice range.
          if (note.pitch < voice_ranges[kFigurationVoice].first ||
              note.pitch > voice_ranges[kFigurationVoice].second) {
            ++fig_range;
            continue;
          }

          // (C) Harmony: non-chord-tone on strong beat.
          if (note.start_tick % kTicksPerBeat == 0) {
            const HarmonicEvent& harm = timeline.getAt(note.start_tick);
            if (!isChordTone(note.pitch, harm)) {
              ++fig_harmony;
              continue;
            }
          }

          // (D) createBachNote for remaining validation.
          BachNoteOptions opts;
          opts.voice = note.voice;
          opts.desired_pitch = note.pitch;
          opts.tick = note.start_tick;
          opts.duration = note.duration;
          opts.velocity = note.velocity;
          opts.source = note.source;

          auto cp_result = createBachNote(&cp_state, &cp_rules, &cp_resolver, opts);
          if (cp_result.accepted) {
            coordinated.push_back(cp_result.note);
            ++accepted_count;
            ++fig_accepted;
          } else {
            ++fig_other;
          }
        }
        note_idx = group_end;
      }

      fprintf(stderr,
              "[ChoralePrelude] figuration: accepted=%d, rejected:"
              " crossing=%d range=%d harmony=%d other=%d\n",
              fig_accepted, fig_crossing, fig_range, fig_harmony, fig_other);
      fprintf(stderr, "[ChoralePrelude] createBachNote: accepted %d/%d (%.0f%%)\n",
              accepted_count, total_count,
              total_count > 0 ? 100.0 * accepted_count / total_count : 0.0);
      all_notes = std::move(coordinated);

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

      float medians[3] = {0, 0, 0};
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
    }

    // ---- Figuration pitch entropy check ----
    {
      int hist[12] = {};
      int fig_note_count = 0;
      uint8_t fig_min = 127, fig_max = 0;
      for (const auto& n : all_notes) {
        if (n.voice == kFigurationVoice) {
          hist[n.pitch % 12]++;
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

    PostValidateStats stats;
    auto validated = postValidateNotes(
        std::move(all_notes), kChoraleVoices, config.key, voice_ranges, &stats);

    for (auto& track : tracks) {
      track.notes.clear();
    }
    for (auto& note : validated) {
      if (note.voice < kChoraleVoices) {
        tracks[note.voice].notes.push_back(std::move(note));
      }
    }
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
  for (auto& track : tracks) {
    std::sort(track.notes.begin(), track.notes.end(),
              [](const NoteEvent& lhs, const NoteEvent& rhs) {
                if (lhs.start_tick != rhs.start_tick) {
                  return lhs.start_tick < rhs.start_tick;
                }
                return lhs.pitch < rhs.pitch;
              });
  }

  result.tracks = std::move(tracks);
  result.timeline = std::move(timeline);
  result.total_duration_ticks = total_duration;
  result.success = true;

  return result;
}

}  // namespace bach
