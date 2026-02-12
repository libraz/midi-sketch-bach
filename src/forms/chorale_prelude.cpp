// Implementation of the chorale prelude generator (BWV 599-650 style).

#include "forms/chorale_prelude.h"

#include <algorithm>
#include <cstdint>
#include <vector>

#include "core/gm_program.h"
#include "core/note_creator.h"
#include "core/pitch_utils.h"
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
// Simple deterministic RNG (LCG)
// ---------------------------------------------------------------------------

/// @brief Simple linear congruential generator for deterministic note choices.
/// @param state Mutable RNG state, updated in place.
/// @return Pseudo-random value in [0, 32767].
uint32_t nextRng(uint32_t& state) {
  state = state * 1103515245u + 12345u;
  return (state >> 16) & 0x7FFFu;
}

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
/// Pitches are in the Swell manual range (C4-C5 region for clarity).
///
/// @param melody The chorale melody to place.
/// @param track Swell track to populate.
void placeCantus(const ChoraleMelody& melody, Track& track) {
  Tick current_tick = 0;
  for (size_t idx = 0; idx < melody.note_count; ++idx) {
    Tick dur = static_cast<Tick>(melody.notes[idx].duration_beats) * kTicksPerBeat;
    uint8_t pitch = melody.notes[idx].pitch;

    // Cantus sits in the soprano register on Swell: transpose up one octave
    // so figuration (alto/tenor) sits clearly below it.
    pitch = clampPitch(static_cast<int>(pitch) + 12, 72, 91);  // C5-G6 (soprano)

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
/// Creates 8th and 16th note figurations using scale tones and chord tones
/// from the harmonic timeline. The figuration weaves around the cantus pitch.
///
/// @param cantus_tick Start tick of the cantus note.
/// @param cantus_dur Duration of the cantus note in ticks.
/// @param timeline Harmonic timeline for chord context.
/// @param key_sig Key signature for scale tone lookup.
/// @param rng_state Mutable RNG state.
/// @return Vector of NoteEvents for the counterpoint voice.
std::vector<NoteEvent> generateFiguration(Tick cantus_tick, Tick cantus_dur,
                                          const HarmonicTimeline& timeline,
                                          const KeySignature& key_sig,
                                          uint32_t& rng_state) {
  std::vector<NoteEvent> notes;

  // Counterpoint range: below cantus (C5+), in the tenor/alto region (C3-B4).
  constexpr uint8_t kFigLow = 48;
  constexpr uint8_t kFigHigh = 71;  // B4 (clear gap below cantus at C5)

  ScaleType scale_type = key_sig.is_minor ? ScaleType::HarmonicMinor : ScaleType::Major;

  // Collect scale tones in the figuration range.
  std::vector<uint8_t> scale_tones;
  for (int pitch = static_cast<int>(kFigLow); pitch <= static_cast<int>(kFigHigh); ++pitch) {
    if (scale_util::isScaleTone(static_cast<uint8_t>(pitch), key_sig.tonic, scale_type)) {
      scale_tones.push_back(static_cast<uint8_t>(pitch));
    }
  }

  if (scale_tones.empty()) {
    return notes;
  }

  Tick current_tick = cantus_tick;
  Tick end_tick = cantus_tick + cantus_dur;

  // Start in the middle of available tones.
  size_t tone_idx = scale_tones.size() / 2;
  bool ascending = (nextRng(rng_state) % 2) == 0;

  while (current_tick < end_tick) {
    // Alternate between 8th and 16th notes for rhythmic variety.
    Tick dur = (nextRng(rng_state) % 3 == 0) ? kSixteenthNote : kEighthNote;
    Tick remaining = end_tick - current_tick;
    if (dur > remaining) dur = remaining;
    if (dur == 0) break;

    // On strong beats, prefer chord tones from the harmonic timeline.
    if (current_tick % kTicksPerBeat == 0) {
      const HarmonicEvent& event = timeline.getAt(current_tick);
      if (!isChordTone(scale_tones[tone_idx], event)) {
        for (size_t search = 1; search < scale_tones.size(); ++search) {
          size_t up = tone_idx + search;
          size_t down = (tone_idx >= search) ? tone_idx - search : scale_tones.size();
          if (up < scale_tones.size() && isChordTone(scale_tones[up], event)) {
            tone_idx = up;
            break;
          }
          if (down < scale_tones.size() && isChordTone(scale_tones[down], event)) {
            tone_idx = down;
            break;
          }
        }
      }
    }

    NoteEvent note;
    note.start_tick = current_tick;
    note.duration = dur;
    note.pitch = scale_tones[tone_idx];
    note.velocity = kOrganVelocity;
    note.voice = 0;
    note.source = BachNoteSource::FreeCounterpoint;
    notes.push_back(note);

    current_tick += dur;

    // Step through scale tones with occasional leaps.
    int step = (nextRng(rng_state) % 4 == 0) ? 2 : 1;

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

    // Occasionally reverse direction for musical interest.
    if (nextRng(rng_state) % 5 == 0) {
      ascending = !ascending;
    }
  }

  return notes;
}

// ---------------------------------------------------------------------------
// Pedal bass generation
// ---------------------------------------------------------------------------

/// @brief Generate pedal bass notes against one cantus note.
///
/// Creates quarter and half note bass tones using the chord root and fifth
/// from the harmonic timeline, clamped to the pedal range (C1-D3).
///
/// @param cantus_tick Start tick of the cantus note.
/// @param cantus_dur Duration of the cantus note in ticks.
/// @param timeline Harmonic timeline for chord context.
/// @param rng_state Mutable RNG state.
/// @return Vector of NoteEvents for the pedal voice.
std::vector<NoteEvent> generatePedalBass(Tick cantus_tick, Tick cantus_dur,
                                         const HarmonicTimeline& timeline,
                                         uint32_t& rng_state) {
  std::vector<NoteEvent> notes;

  Tick current_tick = cantus_tick;
  Tick end_tick = cantus_tick + cantus_dur;

  while (current_tick < end_tick) {
    // Look up the current chord from the harmonic timeline.
    const HarmonicEvent& event = timeline.getAt(current_tick);

    uint8_t bass = clampPitch(static_cast<int>(event.bass_pitch),
                              organ_range::kPedalLow, organ_range::kPedalHigh);

    // Compute the fifth above the root within pedal range.
    int fifth_pitch = static_cast<int>(bass) + interval::kPerfect5th;
    uint8_t fifth = clampPitch(fifth_pitch, organ_range::kPedalLow,
                               organ_range::kPedalHigh);

    // Choose between root and fifth.
    bool use_root = (nextRng(rng_state) % 3 != 0);  // ~67% root, 33% fifth.
    uint8_t chosen_pitch = use_root ? bass : fifth;

    // Alternate between quarter and half note durations.
    Tick dur = (nextRng(rng_state) % 2 == 0) ? kQuarterNote : kHalfNote;
    Tick remaining = end_tick - current_tick;
    if (dur > remaining) dur = remaining;
    if (dur == 0) break;

    NoteEvent note;
    note.start_tick = current_tick;
    note.duration = dur;
    note.pitch = chosen_pitch;
    note.velocity = kOrganVelocity;
    note.voice = 2;
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
  uint32_t rng_state = config.seed;
  Tick cantus_tick = 0;

  for (size_t idx = 0; idx < melody.note_count; ++idx) {
    Tick cantus_dur =
        static_cast<Tick>(melody.notes[idx].duration_beats) * kTicksPerBeat;

    // Counterpoint figuration on Great (track 0).
    auto fig_notes = generateFiguration(cantus_tick, cantus_dur, timeline,
                                        config.key, rng_state);
    for (auto& note : fig_notes) {
      tracks[0].notes.push_back(note);
    }

    // Pedal bass (track 2).
    auto pedal_notes = generatePedalBass(cantus_tick, cantus_dur, timeline,
                                         rng_state);
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
        {48, 71},  // Voice 0: Great figuration (C3-B4)
        {72, 91},  // Voice 1: Swell cantus (C5-G6)
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

      std::vector<NoteEvent> coordinated;
      coordinated.reserve(all_notes.size());
      int accepted_count = 0;
      int total_count = 0;

      size_t idx = 0;
      while (idx < all_notes.size()) {
        Tick current_tick = all_notes[idx].start_tick;
        size_t group_end = idx;
        while (group_end < all_notes.size() &&
               all_notes[group_end].start_tick == current_tick) {
          ++group_end;
        }

        // Priority: cantus (immutable) → pedal → figuration.
        std::sort(all_notes.begin() + static_cast<ptrdiff_t>(idx),
                  all_notes.begin() + static_cast<ptrdiff_t>(group_end),
                  [](const NoteEvent& a, const NoteEvent& b) {
                    // Cantus (voice 1) first, then pedal (voice 2), then figuration (voice 0).
                    auto priority = [](const NoteEvent& n) -> int {
                      if (n.source == BachNoteSource::CantusFixed) return 0;
                      if (n.source == BachNoteSource::PedalPoint) return 1;
                      return 2;
                    };
                    return priority(a) < priority(b);
                  });

        for (size_t j = idx; j < group_end; ++j) {
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

          BachNoteOptions opts;
          opts.voice = note.voice;
          opts.desired_pitch = note.pitch;
          opts.tick = note.start_tick;
          opts.duration = note.duration;
          opts.velocity = note.velocity;
          opts.source = note.source;

          auto result = createBachNote(&cp_state, &cp_rules, &cp_resolver, opts);
          if (result.accepted) {
            coordinated.push_back(result.note);
            ++accepted_count;
          }
        }
        idx = group_end;
      }

      fprintf(stderr, "[ChoralePrelude] createBachNote: accepted %d/%d (%.0f%%)\n",
              accepted_count, total_count,
              total_count > 0 ? 100.0 * accepted_count / total_count : 0.0);
      all_notes = std::move(coordinated);
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
