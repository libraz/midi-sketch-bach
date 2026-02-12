// Implementation of the fantasia free section generator (BWV 537/542 style).

#include "forms/fantasia.h"

#include <algorithm>
#include <random>
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
#include "organ/organ_techniques.h"

namespace bach {

namespace {

using namespace duration;

// ---------------------------------------------------------------------------
// Track creation
// ---------------------------------------------------------------------------

/// @brief Create MIDI tracks for a fantasia.
///
/// Channel/program mapping per the organ system spec:
///   Voice 0 -> Ch 0, Church Organ (Manual I / Great)
///   Voice 1 -> Ch 1, Reed Organ   (Manual II / Swell)
///   Voice 2 -> Ch 2, Church Organ (Manual III / Positiv)
///   Voice 3 -> Ch 3, Church Organ (Pedal)
///
/// @param num_voices Number of voices (2-5).
/// @return Vector of Track objects with channel/program/name configured.
std::vector<Track> createFantasiaTracks(uint8_t num_voices) {
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
// Voice generators
// ---------------------------------------------------------------------------

/// @brief Generate ornamental melody for Voice 0 (Great/Manual I).
///
/// Creates a contemplative, weaving melody around chord tones using
/// quarter and eighth note values. Characteristic of fantasia style,
/// the melody alternates between stepwise motion and occasional leaps
/// to chord tones.
///
/// @param event Current harmonic event providing chord context.
/// @param rng Random number generator for pitch/rhythm choices.
/// @return Vector of NoteEvents for the ornamental melody voice.
std::vector<NoteEvent> generateOrnamentalMelody(const HarmonicEvent& event,
                                                std::mt19937& rng) {
  std::vector<NoteEvent> notes;

  // Ornamental melody sits in the upper register of Manual I.
  constexpr uint8_t kMelodyLow = 67;   // G4
  constexpr uint8_t kMelodyHigh = 88;  // E6

  auto scale_tones = getScaleTones(event.key, event.is_minor, kMelodyLow, kMelodyHigh);
  if (scale_tones.empty()) {
    return notes;
  }

  // Get chord tones for occasional anchoring.
  auto chord_tones = getChordTones(event.chord, 4);
  std::vector<uint8_t> valid_chord_tones;
  for (auto tone : chord_tones) {
    if (tone >= kMelodyLow && tone <= kMelodyHigh) {
      valid_chord_tones.push_back(tone);
    }
  }

  Tick event_duration = event.end_tick - event.tick;
  Tick current_tick = event.tick;

  // Start from a chord tone if available, otherwise middle of scale.
  size_t tone_idx = scale_tones.size() / 2;
  if (!valid_chord_tones.empty()) {
    // Find nearest scale tone index to the first chord tone.
    uint8_t target = valid_chord_tones[0];
    for (size_t idx = 0; idx < scale_tones.size(); ++idx) {
      if (scale_tones[idx] >= target) {
        tone_idx = idx;
        break;
      }
    }
  }

  bool ascending = rng::rollProbability(rng, 0.5f);

  while (current_tick < event.tick + event_duration) {
    // Ornamental melody uses quarter and eighth notes.
    Tick dur = rng::rollProbability(rng, 0.4f) ? kEighthNote : kQuarterNote;
    Tick remaining = (event.tick + event_duration) - current_tick;
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

    // Stepwise motion with occasional leaps for ornamental character.
    int step = 1;
    if (rng::rollProbability(rng, 0.2f)) {
      step = rng::rollRange(rng, 2, 3);  // Occasional leap.
    }

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
    if (rng::rollProbability(rng, 0.15f)) {
      ascending = !ascending;
    }
  }

  return notes;
}

/// @brief Generate sustained chords for Voice 1 (Swell/Manual II).
///
/// Creates half and whole note chord tones representing the sustained
/// harmonic foundation that is the hallmark of the fantasia style.
/// The Swell manual carries the meditative harmonic core.
///
/// This function generates over the full target duration, looking up the
/// harmonic timeline for chord context at each note start. This allows
/// notes to span multiple beat-level timeline events.
///
/// @param timeline Harmonic timeline for chord context lookup.
/// @param target_duration Total duration in ticks.
/// @param rng Random number generator for tone selection.
/// @return Vector of NoteEvents for the sustained chord voice.
std::vector<NoteEvent> generateSustainedChords(const HarmonicTimeline& timeline,
                                               Tick target_duration,
                                               std::mt19937& rng) {
  std::vector<NoteEvent> notes;

  // Sustained chords sit in the middle register of Manual II.
  constexpr uint8_t kChordLow = 52;   // E3
  constexpr uint8_t kChordHigh = 76;  // E5

  Tick current_tick = 0;
  bool use_whole = rng::rollProbability(rng, 0.5f);

  while (current_tick < target_duration) {
    // Look up the current chord from the harmonic timeline.
    const HarmonicEvent& event = timeline.getAt(current_tick);

    // Build chord tones in octaves 3 and 4.
    std::vector<uint8_t> all_chord_tones;
    for (int octave = 3; octave <= 4; ++octave) {
      auto tones = getChordTones(event.chord, octave);
      for (auto tone : tones) {
        if (tone >= kChordLow && tone <= kChordHigh) {
          all_chord_tones.push_back(tone);
        }
      }
    }

    if (all_chord_tones.empty()) {
      // Fallback: use bass pitch clamped to range.
      all_chord_tones.push_back(
          clampPitch(static_cast<int>(event.bass_pitch), kChordLow, kChordHigh));
    }

    Tick dur = use_whole ? kWholeNote : kHalfNote;
    Tick remaining = target_duration - current_tick;
    if (dur > remaining) dur = remaining;
    if (dur == 0) break;

    NoteEvent note;
    note.start_tick = current_tick;
    note.duration = dur;
    note.pitch = rng::selectRandom(rng, all_chord_tones);
    note.velocity = kOrganVelocity;
    note.voice = 1;
    note.source = BachNoteSource::FreeCounterpoint;
    notes.push_back(note);

    current_tick += dur;
    use_whole = !use_whole;  // Alternate rhythm for variety.
  }

  return notes;
}

/// @brief Generate light countermelody for Voice 2 (Positiv/Manual III).
///
/// Creates a gentle eighth note countermelody that weaves between the
/// ornamental melody above and the sustained chords. Uses scale tones
/// with occasional chord tone anchoring.
///
/// @param event Current harmonic event providing chord context.
/// @param rng Random number generator for pitch choices.
/// @return Vector of NoteEvents for the countermelody voice.
std::vector<NoteEvent> generateCountermelody(const HarmonicEvent& event,
                                             std::mt19937& rng) {
  std::vector<NoteEvent> notes;

  // Countermelody sits in the Positiv range, focusing on the middle register.
  constexpr uint8_t kCounterLow = 43;   // G2
  constexpr uint8_t kCounterHigh = 64;  // E4

  auto scale_tones =
      getScaleTones(event.key, event.is_minor, kCounterLow, kCounterHigh);
  if (scale_tones.empty()) {
    return notes;
  }

  Tick event_duration = event.end_tick - event.tick;
  Tick current_tick = event.tick;

  // Start from the lower third of the range for contrast with melody.
  size_t tone_idx = scale_tones.size() / 3;
  bool ascending = rng::rollProbability(rng, 0.5f);

  while (current_tick < event.tick + event_duration) {
    // Countermelody uses eighth notes for light texture.
    Tick dur = kEighthNote;
    Tick remaining = (event.tick + event_duration) - current_tick;
    if (dur > remaining) dur = remaining;
    if (dur == 0) break;

    NoteEvent note;
    note.start_tick = current_tick;
    note.duration = dur;
    note.pitch = scale_tones[tone_idx];
    note.velocity = kOrganVelocity;
    note.voice = 2;
    note.source = BachNoteSource::FreeCounterpoint;
    notes.push_back(note);

    current_tick += dur;

    // Gentle stepwise motion with occasional direction changes.
    int step = 1;
    if (rng::rollProbability(rng, 0.15f)) {
      step = 2;  // Occasional skip for variety.
    }

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

    // Occasional direction reversal.
    if (rng::rollProbability(rng, 0.2f)) {
      ascending = !ascending;
    }
  }

  return notes;
}

/// @brief Generate slow bass notes for the Pedal voice.
///
/// Creates whole note bass tones using the harmonic timeline's bass pitch.
/// The pedal provides a deep, steady foundation characteristic of
/// the contemplative fantasia style.
///
/// This function generates over the full target duration, looking up the
/// harmonic timeline for bass context at each note start. This allows
/// whole notes to span multiple beat-level timeline events.
///
/// @param timeline Harmonic timeline for bass pitch lookup.
/// @param target_duration Total duration in ticks.
/// @return Vector of NoteEvents for the pedal bass voice.
std::vector<NoteEvent> generateSlowBass(const HarmonicTimeline& timeline,
                                        Tick target_duration) {
  std::vector<NoteEvent> notes;

  Tick current_tick = 0;

  // Pedal uses whole notes exclusively for slow, sustained bass.
  while (current_tick < target_duration) {
    // Look up the current chord from the harmonic timeline.
    const HarmonicEvent& event = timeline.getAt(current_tick);

    Tick dur = kWholeNote;
    Tick remaining = target_duration - current_tick;
    if (dur > remaining) dur = remaining;
    if (dur == 0) break;

    uint8_t bass = clampPitch(static_cast<int>(event.bass_pitch),
                              organ_range::kPedalLow, organ_range::kPedalHigh);

    NoteEvent note;
    note.start_tick = current_tick;
    note.duration = dur;
    note.pitch = bass;
    note.velocity = kOrganVelocity;
    note.voice = 3;
    note.source = BachNoteSource::PedalPoint;
    notes.push_back(note);

    current_tick += dur;
  }

  return notes;
}

// ---------------------------------------------------------------------------
// Note sorting
// ---------------------------------------------------------------------------

/// @brief Sort notes in each track by start_tick for MIDI output.
/// @param tracks Tracks whose notes will be sorted in place.
void sortFantasiaTrackNotes(std::vector<Track>& tracks) {
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
uint8_t clampFantasiaVoiceCount(uint8_t num_voices) {
  if (num_voices < 2) return 2;
  if (num_voices > 5) return 5;
  return num_voices;
}

}  // namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

FantasiaResult generateFantasia(const FantasiaConfig& config) {
  FantasiaResult result;
  result.success = false;

  uint8_t num_voices = clampFantasiaVoiceCount(config.num_voices);
  std::mt19937 rng(config.seed);

  // Step 1: Calculate target duration from section_bars.
  Tick target_duration = static_cast<Tick>(config.section_bars) * kTicksPerBar;
  if (target_duration == 0) {
    result.error_message = "section_bars must be > 0";
    return result;
  }

  // Step 2: Create harmonic timeline with Baroque-favored progression.
  const ProgressionType kProgTypes[] = {
      ProgressionType::DescendingFifths,
      ProgressionType::ChromaticCircle,
      ProgressionType::BorrowedChord,
  };
  auto prog = kProgTypes[config.seed % 3];
  HarmonicTimeline timeline = HarmonicTimeline::createProgression(
      config.key, target_duration, HarmonicResolution::Beat, prog);
  timeline.applyCadence(CadenceType::Perfect, config.key);

  // Step 3: Generate notes for each voice.
  //
  // Voices 0 and 2 use short note values (quarter/eighth) and are generated
  // per-event from the beat-level timeline. Voices 1 and 3 use long note
  // values (half/whole) that span multiple beats, so they are generated over
  // the full duration with timeline lookups at each note start.
  std::vector<NoteEvent> all_notes;
  const auto& events = timeline.events();

  // Per-event generation for short-note voices.
  for (size_t event_idx = 0; event_idx < events.size(); ++event_idx) {
    const auto& event = events[event_idx];

    // Voice 0: Ornamental melody (quarter/eighth notes on Great).
    if (num_voices >= 1) {
      auto melody_notes = generateOrnamentalMelody(event, rng);
      all_notes.insert(all_notes.end(), melody_notes.begin(), melody_notes.end());
    }

    // Voice 2: Light countermelody (eighth notes on Positiv).
    if (num_voices >= 3) {
      auto counter_notes = generateCountermelody(event, rng);
      all_notes.insert(all_notes.end(), counter_notes.begin(), counter_notes.end());
    }
  }

  // Full-duration generation for long-note voices.
  // Voice 1: Sustained chords (half/whole notes on Swell).
  if (num_voices >= 2) {
    auto chord_notes = generateSustainedChords(timeline, target_duration, rng);
    all_notes.insert(all_notes.end(), chord_notes.begin(), chord_notes.end());
  }

  // Voice 3 (Pedal): Whole note bass foundation.
  if (num_voices >= 4) {
    auto bass_notes = generateSlowBass(timeline, target_duration);
    all_notes.insert(all_notes.end(), bass_notes.begin(), bass_notes.end());
  }

  // ---- createBachNote coordination pass (vertical dissonance control) ----
  {
    auto fantasia_low = [](uint8_t v) -> uint8_t {
      switch (v) {
        case 0: return 67;  // melody
        case 1: return 52;  // chords
        case 2: return 43;  // counter
        case 3: return organ_range::kPedalLow;
        default: return 52;
      }
    };
    auto fantasia_high = [](uint8_t v) -> uint8_t {
      switch (v) {
        case 0: return 88;
        case 1: return 76;
        case 2: return 64;
        case 3: return organ_range::kPedalHigh;
        default: return 76;
      }
    };

    BachRuleEvaluator cp_rules(num_voices);
    cp_rules.setFreeCounterpoint(true);
    CollisionResolver cp_resolver;
    cp_resolver.setHarmonicTimeline(&timeline);
    CounterpointState cp_state;
    cp_state.setKey(config.key.tonic);
    for (uint8_t v = 0; v < num_voices; ++v) {
      cp_state.registerVoice(v, fantasia_low(v), fantasia_high(v));
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

      // Sort group: pedal first, then lower voices first.
      std::sort(all_notes.begin() + static_cast<ptrdiff_t>(idx),
                all_notes.begin() + static_cast<ptrdiff_t>(group_end),
                [num_voices](const NoteEvent& a, const NoteEvent& b) {
                  bool a_pedal = (a.voice == num_voices - 1 && num_voices >= 4);
                  bool b_pedal = (b.voice == num_voices - 1 && num_voices >= 4);
                  if (a_pedal != b_pedal) return a_pedal;
                  return a.voice > b.voice;
                });

      for (size_t j = idx; j < group_end; ++j) {
        const auto& note = all_notes[j];
        ++total_count;

        if (note.source == BachNoteSource::PedalPoint) {
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

    fprintf(stderr, "[Fantasia] createBachNote: accepted %d/%d (%.0f%%)\n",
            accepted_count, total_count,
            total_count > 0 ? 100.0 * accepted_count / total_count : 0.0);
    all_notes = std::move(coordinated);
  }

  // ---- postValidateNotes safety net (parallel 5ths/8ths repair) ----
  {
    std::vector<std::pair<uint8_t, uint8_t>> voice_ranges;
    for (uint8_t v = 0; v < num_voices; ++v) {
      uint8_t lo = (v == 0) ? 67 : (v == 1) ? 52 : (v == 2) ? 43
                                              : organ_range::kPedalLow;
      uint8_t hi = (v == 0) ? 88 : (v == 1) ? 76 : (v == 2) ? 64
                                              : organ_range::kPedalHigh;
      voice_ranges.push_back({lo, hi});
    }
    PostValidateStats pv_stats;
    all_notes = postValidateNotes(
        std::move(all_notes), num_voices, config.key, voice_ranges, &pv_stats);
  }

  // Step 4: Create tracks and assign notes by voice_id.
  std::vector<Track> tracks = createFantasiaTracks(num_voices);
  for (const auto& note : all_notes) {
    if (note.voice < tracks.size()) {
      tracks[note.voice].notes.push_back(note);
    }
  }

  // ---------------------------------------------------------------------------
  // Shared organ techniques: pedal point, Picardy, registration
  // ---------------------------------------------------------------------------

  // Cadential pedal point on tonic (last 2 bars).
  if (num_voices >= 4 && target_duration > kTicksPerBar * 2) {
    Tick pedal_start = target_duration - kTicksPerBar * 2;
    auto pedal_notes = generateCadentialPedal(
        config.key, pedal_start, target_duration,
        PedalPointType::Tonic, num_voices - 1);
    for (auto& n : pedal_notes) {
      if (n.voice < tracks.size()) {
        tracks[n.voice].notes.push_back(n);
      }
    }
  }

  // Picardy third (minor keys only).
  if (config.enable_picardy && config.key.is_minor) {
    for (auto& track : tracks) {
      applyPicardyToFinalChord(track.notes, config.key,
                               target_duration - kTicksPerBar);
    }
  }

  // Simple 3-point registration plan.
  auto reg_plan = createSimpleRegistrationPlan(0, target_duration);
  applyExtendedRegistrationPlan(tracks, reg_plan);

  // Step 5: Sort notes within each track.
  sortFantasiaTrackNotes(tracks);

  result.tracks = std::move(tracks);
  result.timeline = std::move(timeline);
  result.total_duration_ticks = target_duration;
  result.success = true;

  return result;
}

}  // namespace bach
