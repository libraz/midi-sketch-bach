// Implementation of the fantasia free section generator (BWV 537/542 style).

#include "forms/fantasia.h"

#include <algorithm>
#include <random>
#include <vector>

#include "core/gm_program.h"
#include "core/melodic_state.h"
#include "forms/form_utils.h"
#include "fugue/cadence_insertion.h"
#include "core/note_creator.h"
#include "core/pitch_utils.h"
#include "core/rng_util.h"
#include "core/scale.h"
#include "counterpoint/coordinate_voices.h"
#include "counterpoint/leap_resolution.h"
#include "counterpoint/parallel_repair.h"
#include "counterpoint/vertical_context.h"
#include "counterpoint/vertical_safe.h"
#include "harmony/chord_types.h"
#include "harmony/harmonic_event.h"
#include "organ/organ_techniques.h"

namespace bach {

namespace {

using namespace duration;

// ---------------------------------------------------------------------------
// Section texture type for stylus phantasticus alternation
// ---------------------------------------------------------------------------

/// @brief Section texture type for fantasia structural rhythm.
///
/// Fantasia (stylus phantasticus) alternates between rapid passage work
/// and sustained chordal sections. This enum drives duration selection
/// across all voices at each harmonic event.
enum class FantasiaSectionTexture : uint8_t {
  Passage,   ///< Rapid figuration: 16th/8th notes in melody, 8th in counter.
  Chordal,   ///< Sustained: quarter/half in melody, whole in chords, quarter in counter.
  Cadential  ///< Cadence approach: half/whole notes, all voices converge.
};

/// @brief Determine section texture for a given event position.
///
/// The texture alternates in 4-bar groups (2 passage, 2 chordal), with the
/// final 2 bars always cadential. At sequence/harmonic boundaries (detected
/// by chord root change), the texture may shift early.
///
/// @param event_progress Progress of this event within the piece (0.0-1.0).
/// @param bar_idx Zero-based bar index of the event start.
/// @param prev_root_pc Previous event root pitch class (-1 if first).
/// @param curr_root_pc Current event root pitch class.
/// @param total_bars Total number of bars in the piece.
/// @return FantasiaSectionTexture for this event.
FantasiaSectionTexture determineFantasiaSectionTexture(
    float event_progress, int bar_idx, int prev_root_pc, int curr_root_pc,
    int total_bars) {
  // Final 2 bars: cadential convergence.
  if (bar_idx >= total_bars - 2) {
    return FantasiaSectionTexture::Cadential;
  }

  // Harmonic boundary detection: root motion signals section shift.
  bool at_harmonic_boundary = (prev_root_pc >= 0 && curr_root_pc != prev_root_pc);

  // 4-bar alternation cycle: bars 0-1 passage, bars 2-3 chordal.
  int cycle_pos = bar_idx % 4;

  // At harmonic boundaries in the middle of a cycle, shift texture early
  // to create structural motivic organization rather than mechanical alternation.
  if (at_harmonic_boundary && event_progress > 0.15f && event_progress < 0.85f) {
    // Shift: passage sections get an extra push at harmonic boundaries.
    if (cycle_pos == 1 || cycle_pos == 3) {
      return (cycle_pos < 2) ? FantasiaSectionTexture::Chordal
                             : FantasiaSectionTexture::Passage;
    }
  }

  return (cycle_pos < 2) ? FantasiaSectionTexture::Passage
                         : FantasiaSectionTexture::Chordal;
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
                                                std::mt19937& rng,
                                                const VerticalContext* vctx = nullptr,
                                                FantasiaSectionTexture texture =
                                                    FantasiaSectionTexture::Passage) {
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
  MelodicState orn_mel_state;
  uint8_t prev_orn_pitch = scale_tones[tone_idx];

  while (current_tick < event.tick + event_duration) {
    // Section-texture-aware duration selection for melodic voice.
    Tick dur;
    switch (texture) {
      case FantasiaSectionTexture::Passage:
        // Rapid passage work: primarily 16th and 8th notes.
        dur = rng::rollProbability(rng, 0.35f) ? kSixteenthNote : kEighthNote;
        break;
      case FantasiaSectionTexture::Chordal:
        // Sustained chordal: quarter and half notes.
        dur = rng::rollProbability(rng, 0.6f) ? kQuarterNote : kHalfNote;
        break;
      case FantasiaSectionTexture::Cadential:
        // Cadence: half and whole notes for convergence.
        dur = rng::rollProbability(rng, 0.5f) ? kHalfNote : kWholeNote;
        break;
    }
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

    // Category B: safe alternative when vertically unsafe.
    if (vctx && !vctx->isSafe(current_tick, note.voice, note.pitch)) {
      ScaleType mel_scale =
          event.is_minor ? ScaleType::NaturalMinor : ScaleType::Major;
      for (int delta : {1, -1, 2, -2}) {
        uint8_t alt = clampPitch(static_cast<int>(note.pitch) + delta,
                                 kMelodyLow, kMelodyHigh);
        if (scale_util::isScaleTone(alt, event.key, mel_scale) &&
            vctx->isSafe(current_tick, note.voice, alt)) {
          note.pitch = alt;
          break;
        }
      }
    }

    notes.push_back(note);

    updateMelodicState(orn_mel_state, prev_orn_pitch, note.pitch);
    prev_orn_pitch = note.pitch;
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

    // Direction via MelodicState persistence model.
    ascending = (chooseMelodicDirection(orn_mel_state, rng) > 0);
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
                                               std::mt19937& rng,
                                               const VerticalContext* vctx = nullptr,
                                               int total_bars = 0) {
  std::vector<NoteEvent> notes;

  // Sustained chords sit in the middle register of Manual II.
  constexpr uint8_t kChordLow = 52;   // E3
  constexpr uint8_t kChordHigh = 76;  // E5

  if (total_bars <= 0) {
    total_bars = static_cast<int>(target_duration / kTicksPerBar);
  }
  int prev_root_pc = -1;

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

    // Section-texture-aware duration for sustained chords.
    int bar_idx = static_cast<int>(current_tick / kTicksPerBar);
    float progress = target_duration > 0
        ? static_cast<float>(current_tick) / static_cast<float>(target_duration)
        : 0.5f;
    int curr_root_pc = getPitchClass(event.chord.root_pitch);
    FantasiaSectionTexture chd_texture = determineFantasiaSectionTexture(
        progress, bar_idx, prev_root_pc, curr_root_pc, total_bars);
    prev_root_pc = curr_root_pc;

    Tick dur;
    switch (chd_texture) {
      case FantasiaSectionTexture::Passage:
        // During passage sections, chords shorten to quarter/half
        // to create rhythmic contrast with the rapid melody.
        dur = rng::rollProbability(rng, 0.4f) ? kQuarterNote : kHalfNote;
        break;
      case FantasiaSectionTexture::Cadential:
        // Cadence: sustain with whole notes.
        dur = kWholeNote;
        break;
      default:
        // Chordal sections: original half/whole alternation.
        dur = use_whole ? kWholeNote : kHalfNote;
        break;
    }
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

    // Category B: safe alternative when vertically unsafe.
    if (vctx && !vctx->isSafe(current_tick, note.voice, note.pitch)) {
      ScaleType chd_scale =
          event.is_minor ? ScaleType::NaturalMinor : ScaleType::Major;
      for (int delta : {1, -1, 2, -2}) {
        uint8_t alt = clampPitch(static_cast<int>(note.pitch) + delta,
                                 kChordLow, kChordHigh);
        if (scale_util::isScaleTone(alt, event.key, chd_scale) &&
            vctx->isSafe(current_tick, note.voice, alt)) {
          note.pitch = alt;
          break;
        }
      }
    }

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
                                             std::mt19937& rng,
                                             const VerticalContext* vctx = nullptr,
                                             FantasiaSectionTexture texture =
                                                 FantasiaSectionTexture::Passage) {
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
  MelodicState ctr_mel_state;
  uint8_t prev_ctr_pitch = scale_tones[tone_idx];

  while (current_tick < event.tick + event_duration) {
    // Section-texture-aware countermelody duration.
    Tick dur;
    switch (texture) {
      case FantasiaSectionTexture::Passage:
        // Passage: eighth notes for active counterpoint.
        dur = kEighthNote;
        break;
      case FantasiaSectionTexture::Chordal:
        // Chordal: quarter notes for calmer texture.
        dur = kQuarterNote;
        break;
      case FantasiaSectionTexture::Cadential:
        // Cadential: half notes for convergence.
        dur = kHalfNote;
        break;
    }
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

    // Category B: safe alternative when vertically unsafe.
    if (vctx && !vctx->isSafe(current_tick, note.voice, note.pitch)) {
      ScaleType ctr_scale =
          event.is_minor ? ScaleType::NaturalMinor : ScaleType::Major;
      for (int delta : {1, -1, 2, -2}) {
        uint8_t alt = clampPitch(static_cast<int>(note.pitch) + delta,
                                 kCounterLow, kCounterHigh);
        if (scale_util::isScaleTone(alt, event.key, ctr_scale) &&
            vctx->isSafe(current_tick, note.voice, alt)) {
          note.pitch = alt;
          break;
        }
      }
    }

    notes.push_back(note);

    updateMelodicState(ctr_mel_state, prev_ctr_pitch, note.pitch);
    prev_ctr_pitch = note.pitch;
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

    // Direction via MelodicState persistence model.
    ascending = (chooseMelodicDirection(ctr_mel_state, rng) > 0);
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
                                        Tick target_duration,
                                        const VerticalContext* vctx = nullptr) {
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

    // Category B: safe alternative when vertically unsafe.
    if (vctx && !vctx->isSafe(current_tick, 3, bass)) {
      ScaleType bass_scale =
          event.is_minor ? ScaleType::NaturalMinor : ScaleType::Major;
      for (int delta : {1, -1, 2, -2}) {
        uint8_t alt = clampPitch(static_cast<int>(bass) + delta,
                                 organ_range::kPedalLow, organ_range::kPedalHigh);
        if (scale_util::isScaleTone(alt, event.key, bass_scale) &&
            vctx->isSafe(current_tick, 3, alt)) {
          bass = alt;
          break;
        }
      }
    }

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
  // Generation order: harmonic foundation first, gestural voices last.
  // Voice 3 (bass) and Voice 1 (chords) establish the vertical context
  // before Voices 0 and 2 (melody/countermelody) are generated against it.
  std::vector<NoteEvent> all_notes;
  const auto& events = timeline.events();

  // Voice 3 (bass) first -- harmonic foundation.
  if (num_voices >= 4) {
    auto bass_notes = generateSlowBass(timeline, target_duration);
    all_notes.insert(all_notes.end(), bass_notes.begin(), bass_notes.end());
  }

  // VerticalContext grows as voices are added.
  VerticalContext vctx{&all_notes, &timeline, num_voices};

  // Voice 1 (sustained chords) -- harmonic support with section texture.
  int total_bars = config.section_bars;
  if (num_voices >= 2) {
    auto chord_notes = generateSustainedChords(timeline, target_duration, rng,
                                               &vctx, total_bars);
    all_notes.insert(all_notes.end(), chord_notes.begin(), chord_notes.end());
  }

  // Voice 0 (melody) and Voice 2 (countermelody) -- per-event gestural voices.
  // Section texture computed per event for structural motivic organization.
  int prev_melody_root_pc = -1;
  for (size_t event_idx = 0; event_idx < events.size(); ++event_idx) {
    const auto& event = events[event_idx];

    // Compute section texture for this event.
    int bar_idx = static_cast<int>(event.tick / kTicksPerBar);
    float event_progress = target_duration > 0
        ? static_cast<float>(event.tick) / static_cast<float>(target_duration)
        : 0.5f;
    int curr_root_pc = getPitchClass(event.chord.root_pitch);
    FantasiaSectionTexture texture = determineFantasiaSectionTexture(
        event_progress, bar_idx, prev_melody_root_pc, curr_root_pc, total_bars);
    prev_melody_root_pc = curr_root_pc;

    // Voice 0: Ornamental melody with section-texture-aware rhythm.
    if (num_voices >= 1) {
      auto melody_notes = generateOrnamentalMelody(event, rng, &vctx, texture);
      all_notes.insert(all_notes.end(), melody_notes.begin(), melody_notes.end());
    }

    // Voice 2: Countermelody with section-texture-aware rhythm.
    if (num_voices >= 3) {
      auto counter_notes = generateCountermelody(event, rng, &vctx, texture);
      all_notes.insert(all_notes.end(), counter_notes.begin(), counter_notes.end());
    }
  }

  // ---- Cadence insertion: fill harmonic tension gaps ----
  // Fantasia (stylus phantasticus) allows longer stretches without clear
  // cadences, so the detection threshold is relaxed to 24 bars (vs 16 for fugue).
  {
    CadenceDetectionConfig cadence_config;
    cadence_config.max_bars_without_cadence = 24;
    cadence_config.scan_window_bars = 8;
    cadence_config.deceptive_cadence_probability = 0.20f;

    FugueStructure empty_structure;  // No subject entries to exclude.
    VoiceId bass_voice = (num_voices >= 4) ? static_cast<VoiceId>(num_voices - 1)
                                           : static_cast<VoiceId>(num_voices - 1);
    int cadences_inserted = ensureCadentialCoverage(
        all_notes, empty_structure, config.key.tonic, config.key.is_minor,
        bass_voice, num_voices, target_duration,
        config.seed + 66666u, cadence_config);
    (void)cadences_inserted;
  }

  // ---- Unified coordination pass (vertical dissonance control) ----
  {
    CoordinationConfig coord_config;
    coord_config.num_voices = num_voices;
    coord_config.tonic = config.key.tonic;
    coord_config.timeline = &timeline;
    coord_config.voice_range = [](uint8_t v) -> std::pair<uint8_t, uint8_t> {
      switch (v) {
        case 0: return {67, 88};
        case 1: return {52, 76};
        case 2: return {43, 64};
        case 3: return {organ_range::kPedalLow, organ_range::kPedalHigh};
        default: return {52, 76};
      }
    };
    coord_config.form_name = "Fantasia";
    auto form_profile = getFormProfile(FormType::FantasiaAndFugue);
    coord_config.dissonance_policy = form_profile.dissonance_policy;
    all_notes = coordinateVoices(std::move(all_notes), coord_config);
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

    // Leap resolution: fix unresolved melodic leaps.
    {
      LeapResolutionParams lr_params;
      lr_params.num_voices = num_voices;
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
          makeVerticalSafeWithParallelCheck(timeline, all_notes, num_voices);
      resolveLeaps(all_notes, lr_params);

      // Second parallel-perfect repair pass after leap resolution.
      {
        ParallelRepairParams pp_params;
        pp_params.num_voices = num_voices;
        pp_params.scale = config.key.is_minor ? ScaleType::HarmonicMinor : ScaleType::Major;
        pp_params.key_at_tick = lr_params.key_at_tick;
        pp_params.voice_range_static = lr_params.voice_range_static;
        pp_params.max_iterations = 2;
        repairParallelPerfect(all_notes, pp_params);
      }
    }
  }

  // Step 4: Create tracks and assign notes by voice_id.
  std::vector<Track> tracks = form_utils::createOrganTracks(num_voices);
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
  form_utils::sortTrackNotes(tracks);

  result.tracks = std::move(tracks);
  result.timeline = std::move(timeline);
  result.total_duration_ticks = target_duration;
  result.success = true;

  return result;
}

}  // namespace bach
