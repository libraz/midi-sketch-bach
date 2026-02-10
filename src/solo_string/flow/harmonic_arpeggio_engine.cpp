// Implementation of the harmonic arpeggio engine for BWV1007-style flow generation.

#include "solo_string/flow/harmonic_arpeggio_engine.h"

#include <algorithm>
#include <cmath>
#include <random>

#include "core/note_source.h"
#include "core/rng_util.h"
#include "harmony/chord_types.h"
#include "harmony/harmonic_event.h"
#include "harmony/harmonic_timeline.h"
#include "harmony/key.h"
#include "instrument/bowed/cello_model.h"
#include "instrument/bowed/violin_model.h"
#include "solo_string/flow/arpeggio_pattern.h"

namespace bach {

namespace {

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

/// @brief Number of 16th-note subdivisions per beat.
constexpr int kSubdivisionsPerBeat = 4;

/// @brief Tick duration of a single 16th note.
constexpr Tick kSixteenthDuration = kTicksPerBeat / kSubdivisionsPerBeat;  // 120

/// @brief Default velocity for solo string notes (expressive, not organ-fixed).
constexpr uint8_t kBaseVelocity = 72;

/// @brief Velocity boost for harmonically weighted notes (weight >= 1.5).
constexpr uint8_t kWeightedVelocityBoost = 12;

/// @brief GM program numbers for solo string instruments.
constexpr uint8_t kCelloProgram = 42;
constexpr uint8_t kViolinProgram = 40;
constexpr uint8_t kGuitarProgram = 24;

/// @brief MIDI channel for the solo instrument.
constexpr uint8_t kSoloChannel = 0;

/// @brief Harmonic weight assigned to Peak sections (design value, not searched).
constexpr float kPeakHarmonicWeight = 2.0f;

/// @brief Major scale intervals in semitones (for degree-to-pitch mapping).
constexpr int kMajorScaleIntervals[7] = {0, 2, 4, 5, 7, 9, 11};

/// @brief Natural minor scale intervals in semitones.
constexpr int kMinorScaleIntervals[7] = {0, 2, 3, 5, 7, 8, 10};

// ---------------------------------------------------------------------------
// Register range for arc-controlled register evolution
// ---------------------------------------------------------------------------

/// @brief Register range (MIDI pitch bounds) that evolves with ArcPhase.
struct RegisterRange {
  uint8_t low = 0;
  uint8_t high = 127;
};

// ---------------------------------------------------------------------------
// Harmonic progression templates
// ---------------------------------------------------------------------------

/// @brief Progression templates for building richer harmonic timelines.
///
/// These extend the basic I-IV-V-I with idiomatic Bach solo string progressions.
/// The progressions cycle through with section variety.
struct ProgressionEntry {
  ChordDegree degree;
  float weight;
};

/// @brief Standard BWV1007-style 4-bar progression: i - iv - V - i (minor).
///                                                or: I - IV - V - I (major).
constexpr ProgressionEntry kProgression_A[4] = {
    {ChordDegree::I, 1.0f},
    {ChordDegree::IV, 0.5f},
    {ChordDegree::V, 0.75f},
    {ChordDegree::I, 1.0f}
};

/// @brief Secondary progression: i - vi - ii - V (leading to next section).
constexpr ProgressionEntry kProgression_B[4] = {
    {ChordDegree::I, 1.0f},
    {ChordDegree::vi, 0.5f},
    {ChordDegree::ii, 0.6f},
    {ChordDegree::V, 0.8f}
};

/// @brief Climactic progression: i - iv - vii - III (minor) or I - IV - vii - V (major).
constexpr ProgressionEntry kProgression_C[4] = {
    {ChordDegree::I, 1.0f},
    {ChordDegree::IV, 0.8f},
    {ChordDegree::viiDim, 0.9f},
    {ChordDegree::V, 1.0f}
};

// ---------------------------------------------------------------------------
// Helper: map scale degree to semitone offset
// ---------------------------------------------------------------------------

/// @brief Convert a scale degree (0-based) to a semitone offset from tonic.
///
/// Handles octave wrapping: degree 7 = root+octave, degree 9 = 3rd+octave, etc.
///
/// @param degree 0-based scale degree (0=root, 1=2nd, 2=3rd, ..., 6=7th, 7=root+8va).
/// @param is_minor True for natural minor scale intervals.
/// @return Semitone offset from tonic (can exceed 12 for octave-wrapped degrees).
int degreeToPitchOffset(int degree, bool is_minor) {
  if (degree < 0) {
    // Negative degrees: wrap downward.
    int octave_down = (-degree + 6) / 7;
    int wrapped = degree + octave_down * 7;
    int base_offset = is_minor ? kMinorScaleIntervals[wrapped % 7]
                               : kMajorScaleIntervals[wrapped % 7];
    return base_offset - octave_down * 12;
  }

  int octave = degree / 7;
  int scale_idx = degree % 7;
  int base_offset = is_minor ? kMinorScaleIntervals[scale_idx]
                             : kMajorScaleIntervals[scale_idx];
  return base_offset + octave * 12;
}

/// @brief Compute the MIDI pitch for a chord degree in a given key and octave.
///
/// @param chord_root_pitch MIDI pitch of the chord root.
/// @param pattern_degree The scale degree from the arpeggio pattern (0=root, 2=3rd, 4=5th).
/// @param is_minor Whether the current key is minor.
/// @return MIDI pitch for the pattern degree relative to the chord root.
uint8_t patternDegreeToMidiPitch(uint8_t chord_root_pitch, int pattern_degree,
                                 bool is_minor) {
  int offset = degreeToPitchOffset(pattern_degree, is_minor);
  int result = static_cast<int>(chord_root_pitch) + offset;
  if (result < 0) result = 0;
  if (result > 127) result = 127;
  return static_cast<uint8_t>(result);
}

// ---------------------------------------------------------------------------
// Helper: place pitch within register range
// ---------------------------------------------------------------------------

/// @brief Adjust a MIDI pitch to fall within the given register range.
///
/// Uses octave transposition to bring the pitch into range, preserving
/// the pitch class. If no octave works, clamps to the nearest boundary.
///
/// @param pitch Input MIDI pitch.
/// @param range The target register range.
/// @return MIDI pitch adjusted to fit within [range.low, range.high].
uint8_t fitToRegister(uint8_t pitch, const RegisterRange& range) {
  if (pitch >= range.low && pitch <= range.high) {
    return pitch;
  }

  int pitch_class = static_cast<int>(pitch) % 12;

  // Find the best octave placement within range.
  int best_pitch = -1;
  int best_distance = 999;

  for (int octave = 0; octave <= 10; ++octave) {
    int candidate = (octave + 1) * 12 + pitch_class;
    if (candidate < static_cast<int>(range.low) ||
        candidate > static_cast<int>(range.high)) {
      continue;
    }

    int distance = std::abs(candidate - static_cast<int>(pitch));
    if (distance < best_distance) {
      best_distance = distance;
      best_pitch = candidate;
    }
  }

  if (best_pitch >= 0) {
    return static_cast<uint8_t>(best_pitch);
  }

  // No octave fits -- clamp to the nearest range boundary.
  if (pitch < range.low) return range.low;
  return range.high;
}

// ---------------------------------------------------------------------------
// Helper: compute register range for a given section based on ArcPhase
// ---------------------------------------------------------------------------

/// @brief Compute the register range for a section based on ArcPhase and position.
///
/// Register evolution follows the GlobalArc:
///   - Ascent: starts from mid-range, gradually widens upward
///   - Peak: full instrument range (design values, output directly)
///   - Descent: gradually narrows toward the low end
///
/// @param phase Current ArcPhase for this section.
/// @param progress Fractional position within the phase (0.0 = start, 1.0 = end).
/// @param inst_low Lowest MIDI pitch of the instrument.
/// @param inst_high Highest MIDI pitch of the instrument.
/// @return RegisterRange for note placement.
RegisterRange computeRegisterRange(ArcPhase phase, float progress,
                                   uint8_t inst_low, uint8_t inst_high) {
  RegisterRange range;
  int full_range = static_cast<int>(inst_high) - static_cast<int>(inst_low);
  int mid_point = static_cast<int>(inst_low) + full_range / 2;

  switch (phase) {
    case ArcPhase::Ascent: {
      // Start from lower-mid range, expand upward as progress increases.
      // Low stays near instrument low; high expands from mid to instrument high.
      range.low = inst_low;
      int expanding_high = mid_point + static_cast<int>(
          static_cast<float>(inst_high - mid_point) * progress);
      range.high = static_cast<uint8_t>(
          std::min(expanding_high, static_cast<int>(inst_high)));
      break;
    }
    case ArcPhase::Peak: {
      // Full instrument range -- design values output directly, no computation.
      range.low = inst_low;
      range.high = inst_high;
      break;
    }
    case ArcPhase::Descent: {
      // Gradually contract from full range toward the low register.
      // High shrinks from instrument high toward mid-point.
      // Low stays at instrument low.
      range.low = inst_low;
      int contracting_high = inst_high - static_cast<int>(
          static_cast<float>(inst_high - mid_point) * progress);
      range.high = static_cast<uint8_t>(
          std::max(contracting_high, mid_point));
      break;
    }
  }

  return range;
}

// ---------------------------------------------------------------------------
// Helper: select progression for a section
// ---------------------------------------------------------------------------

/// @brief Select a harmonic progression template for a section.
///
/// @param section_idx 0-based section index.
/// @param phase ArcPhase of this section.
/// @param total_sections Total number of sections in the piece.
/// @return Pointer to a 4-element ProgressionEntry array.
const ProgressionEntry* selectProgressionForSection(int section_idx,
                                                    ArcPhase phase,
                                                    int total_sections) {
  // Peak section always uses the climactic progression.
  if (phase == ArcPhase::Peak) {
    return kProgression_C;
  }

  // Last section (Descent end) returns to the basic progression.
  if (section_idx == total_sections - 1) {
    return kProgression_A;
  }

  // Alternate between A and B for variety.
  if (section_idx % 2 == 0) {
    return kProgression_A;
  }
  return kProgression_B;
}

// ---------------------------------------------------------------------------
// Helper: assign pattern roles within a section
// ---------------------------------------------------------------------------

/// @brief Assign PatternRole to each bar within a section.
///
/// The role order is Drive -> Expand -> Sustain -> Release (monotonic, no reversal).
/// Short sections (1-2 bars) use only Drive + Release.
/// Longer sections distribute all four roles.
///
/// @param bars_in_section Number of bars in this section.
/// @return Vector of PatternRole, one per bar.
std::vector<PatternRole> assignPatternRoles(int bars_in_section) {
  std::vector<PatternRole> roles;
  roles.reserve(static_cast<size_t>(bars_in_section));

  if (bars_in_section <= 0) {
    return roles;
  }

  if (bars_in_section == 1) {
    roles.push_back(PatternRole::Drive);
    return roles;
  }

  if (bars_in_section == 2) {
    roles.push_back(PatternRole::Drive);
    roles.push_back(PatternRole::Release);
    return roles;
  }

  if (bars_in_section == 3) {
    roles.push_back(PatternRole::Drive);
    roles.push_back(PatternRole::Expand);
    roles.push_back(PatternRole::Release);
    return roles;
  }

  // 4+ bars: distribute all four roles.
  // First bar: Drive. Last bar: Release.
  // Middle bars split between Expand and Sustain.
  roles.push_back(PatternRole::Drive);

  int middle_bars = bars_in_section - 2;
  int expand_count = (middle_bars + 1) / 2;
  int sustain_count = middle_bars - expand_count;

  for (int idx = 0; idx < expand_count; ++idx) {
    roles.push_back(PatternRole::Expand);
  }
  for (int idx = 0; idx < sustain_count; ++idx) {
    roles.push_back(PatternRole::Sustain);
  }

  roles.push_back(PatternRole::Release);

  return roles;
}

// ---------------------------------------------------------------------------
// Helper: get chord tones as scale degrees
// ---------------------------------------------------------------------------

/// @brief Get the standard chord tones as scale degrees for a chord quality.
///
/// Returns pattern degrees (0-based from chord root) for the chord type:
///   - Major/Minor triad: root(0), 3rd(2), 5th(4)
///   - Diminished: root(0), 3rd(2), 5th(4)  (diminished 5th handled by scale)
///   - Seventh chords: root(0), 3rd(2), 5th(4), 7th(6)
///
/// @param quality The chord quality.
/// @return Vector of scale degrees for arpeggio generation.
std::vector<int> getChordDegrees(ChordQuality quality) {
  switch (quality) {
    case ChordQuality::Major:
    case ChordQuality::Minor:
    case ChordQuality::Diminished:
    case ChordQuality::Augmented:
      return {0, 2, 4};  // Triad: root, 3rd, 5th

    case ChordQuality::Dominant7:
    case ChordQuality::Minor7:
    case ChordQuality::MajorMajor7:
      return {0, 2, 4, 6};  // Seventh: root, 3rd, 5th, 7th
  }
  return {0, 2, 4};
}

// ---------------------------------------------------------------------------
// Helper: check open string preference
// ---------------------------------------------------------------------------

/// @brief Determine if open strings should be preferred for a pitch in context.
///
/// Open string usage is a defining characteristic of BWV1007 writing.
/// Preference increases during cadence sections and for certain ArcPhases.
///
/// @param pitch MIDI pitch to check.
/// @param open_string_pitches Open string MIDI pitches of the instrument.
/// @param open_string_count Number of open strings.
/// @param bias Open string preference bias [0.0, 1.0].
/// @param rng Random number generator for stochastic decisions.
/// @return True if the pitch should use an open string variant.
bool shouldPreferOpenString(uint8_t pitch,
                            const std::vector<uint8_t>& open_string_pitches,
                            float bias, std::mt19937& rng) {
  // Check if pitch matches any open string (in any octave).
  int pitch_class = static_cast<int>(pitch) % 12;
  bool has_open_match = false;

  for (uint8_t open_pitch : open_string_pitches) {
    if (static_cast<int>(open_pitch) % 12 == pitch_class) {
      has_open_match = true;
      break;
    }
  }

  if (!has_open_match) {
    return false;
  }

  return rng::rollProbability(rng, bias);
}

/// @brief Find the nearest open string pitch for a given pitch class.
///
/// @param pitch_class The pitch class (0-11) to match.
/// @param open_string_pitches Open string MIDI pitches.
/// @param range Register range to constrain the result.
/// @return The open string MIDI pitch within range, or 0 if none fits.
uint8_t findOpenStringPitch(int pitch_class,
                            const std::vector<uint8_t>& open_string_pitches,
                            const RegisterRange& range) {
  for (uint8_t open_pitch : open_string_pitches) {
    if (static_cast<int>(open_pitch) % 12 == pitch_class &&
        open_pitch >= range.low && open_pitch <= range.high) {
      return open_pitch;
    }
  }
  return 0;
}

// ---------------------------------------------------------------------------
// Helper: compute cadence-adjusted parameters
// ---------------------------------------------------------------------------

/// @brief Determine if a bar is within the cadence region.
/// @param bar_idx 0-based bar index.
/// @param total_bars Total number of bars in the piece.
/// @param cadence_bars Number of cadence bars from config.
/// @return True if this bar falls within the cadence region.
bool isInCadenceRegion(int bar_idx, int total_bars, int cadence_bars) {
  return bar_idx >= total_bars - cadence_bars;
}

/// @brief Compute the cadence progress (0.0 at start of cadence, 1.0 at final bar).
/// @param bar_idx 0-based bar index.
/// @param total_bars Total number of bars.
/// @param cadence_bars Number of cadence bars.
/// @return Progress fraction [0.0, 1.0] within the cadence, or 0.0 if not in cadence.
float cadenceProgress(int bar_idx, int total_bars, int cadence_bars) {
  int cadence_start = total_bars - cadence_bars;
  if (bar_idx < cadence_start || cadence_bars <= 0) {
    return 0.0f;
  }
  return static_cast<float>(bar_idx - cadence_start) /
         static_cast<float>(cadence_bars);
}

// ---------------------------------------------------------------------------
// Helper: get instrument properties
// ---------------------------------------------------------------------------

/// @brief Instrument properties needed for generation.
struct InstrumentProps {
  uint8_t lowest_pitch = 0;
  uint8_t highest_pitch = 127;
  uint8_t gm_program = 0;
  std::vector<uint8_t> open_strings;
  std::string name;
};

/// @brief Populate instrument properties based on InstrumentType.
///
/// Creates a temporary instrument model to extract range, tuning, and GM program.
/// Falls back to reasonable defaults for unsupported instruments.
///
/// @param instrument The instrument type.
/// @return InstrumentProps with range, program, and open string information.
InstrumentProps getInstrumentProps(InstrumentType instrument) {
  InstrumentProps props;

  switch (instrument) {
    case InstrumentType::Cello: {
      CelloModel cello;
      props.lowest_pitch = cello.getLowestPitch();
      props.highest_pitch = cello.getHighestPitch();
      props.gm_program = kCelloProgram;
      props.open_strings = cello.getTuning();
      props.name = "Cello";
      break;
    }
    case InstrumentType::Violin: {
      ViolinModel violin;
      props.lowest_pitch = violin.getLowestPitch();
      props.highest_pitch = violin.getHighestPitch();
      props.gm_program = kViolinProgram;
      props.open_strings = violin.getTuning();
      props.name = "Violin";
      break;
    }
    case InstrumentType::Guitar: {
      // Guitar defaults (standard tuning: E2 A2 D3 G3 B3 E4).
      props.lowest_pitch = 40;  // E2
      props.highest_pitch = 83;  // B5
      props.gm_program = kGuitarProgram;
      props.open_strings = {40, 45, 50, 55, 59, 64};  // E2 A2 D3 G3 B3 E4
      props.name = "Guitar";
      break;
    }
    default: {
      // Fallback to cello range for any unsupported instrument.
      CelloModel cello;
      props.lowest_pitch = cello.getLowestPitch();
      props.highest_pitch = cello.getHighestPitch();
      props.gm_program = kCelloProgram;
      props.open_strings = cello.getTuning();
      props.name = "Cello";
      break;
    }
  }

  return props;
}

// ---------------------------------------------------------------------------
// Build harmonic timeline for the piece
// ---------------------------------------------------------------------------

/// @brief Build a bar-resolution harmonic timeline for the entire piece.
///
/// Creates a harmonic event for each bar, cycling through diatonic progressions
/// that vary by section and ArcPhase. Peak sections receive elevated harmonic weight.
///
/// @param config The flow configuration.
/// @param arc_config Validated GlobalArcConfig with phase assignments.
/// @return HarmonicTimeline at bar resolution.
HarmonicTimeline buildFlowTimeline(const ArpeggioFlowConfig& config,
                                   const GlobalArcConfig& arc_config) {
  HarmonicTimeline timeline;

  int total_sections = config.num_sections;

  // Map section_id -> ArcPhase for quick lookup.
  auto getPhaseForSection = [&](int section_idx) -> ArcPhase {
    for (const auto& [sid, phase] : arc_config.phase_assignment) {
      if (static_cast<int>(sid) == section_idx) {
        return phase;
      }
    }
    return ArcPhase::Ascent;  // Fallback
  };

  Tick current_tick = 0;

  for (int section_idx = 0; section_idx < total_sections; ++section_idx) {
    ArcPhase phase = getPhaseForSection(section_idx);
    const ProgressionEntry* progression =
        selectProgressionForSection(section_idx, phase, total_sections);

    for (int bar_in_section = 0; bar_in_section < config.bars_per_section;
         ++bar_in_section) {
      // Cycle through the 4-chord progression within each section.
      int prog_idx = bar_in_section % 4;

      // Build the chord for this bar.
      ChordDegree degree = progression[prog_idx].degree;
      float base_weight = progression[prog_idx].weight;

      // Peak sections get elevated weight (design value).
      if (phase == ArcPhase::Peak) {
        base_weight = kPeakHarmonicWeight;
      }

      // Build chord in octave 3 (appropriate for cello/violin bass register).
      Chord chord;
      chord.degree = degree;
      chord.quality = config.key.is_minor ? minorKeyQuality(degree)
                                          : majorKeyQuality(degree);

      uint8_t semitone_offset = config.key.is_minor
                                    ? degreeMinorSemitones(degree)
                                    : degreeSemitones(degree);
      int root_midi = 4 * 12 + static_cast<int>(config.key.tonic) + semitone_offset;
      chord.root_pitch = static_cast<uint8_t>(
          std::min(std::max(root_midi, 0), 127));
      chord.inversion = 0;

      // Bass pitch in octave 2.
      int bass_pc = static_cast<int>(chord.root_pitch) % 12;
      int bass_midi = 3 * 12 + bass_pc;  // Octave 2 = (2+1)*12
      if (bass_midi > 127) bass_midi = 127;
      if (bass_midi < 0) bass_midi = 0;

      HarmonicEvent event;
      event.tick = current_tick;
      event.end_tick = current_tick + kTicksPerBar;
      event.key = config.key.tonic;
      event.is_minor = config.key.is_minor;
      event.chord = chord;
      event.bass_pitch = static_cast<uint8_t>(bass_midi);
      event.weight = base_weight;
      event.is_immutable = false;

      timeline.addEvent(event);
      current_tick += kTicksPerBar;
    }
  }

  return timeline;
}

// ---------------------------------------------------------------------------
// Generate notes for a single bar
// ---------------------------------------------------------------------------

/// @brief Generate the 16th-note arpeggio events for a single bar.
///
/// For each beat in the bar, generates kSubdivisionsPerBeat (4) note events
/// cycling through the pattern degrees, mapping each to a MIDI pitch via
/// the harmonic event and register range.
///
/// @param bar_tick Start tick of this bar.
/// @param harm_event Current harmonic event for pitch context.
/// @param pattern Arpeggio pattern for this bar.
/// @param reg_range Register range for pitch placement.
/// @param instrument Instrument properties for open string preference.
/// @param open_string_bias Probability of preferring open strings [0.0, 1.0].
/// @param is_cadence_bar True if this bar is in the cadence region.
/// @param cadence_prog Progress within cadence (0.0-1.0) for rhythmic simplification.
/// @param rhythm_simplification Amount of rhythmic simplification [0.0, 1.0].
/// @param rng Random number generator.
/// @return Vector of NoteEvent for this bar.
std::vector<NoteEvent> generateBarNotes(
    Tick bar_tick,
    const HarmonicEvent& harm_event,
    const ArpeggioPattern& pattern,
    const RegisterRange& reg_range,
    const InstrumentProps& instrument,
    float open_string_bias,
    bool is_cadence_bar,
    float cadence_prog,
    float rhythm_simplification,
    std::mt19937& rng) {
  std::vector<NoteEvent> notes;

  if (pattern.degrees.empty()) {
    return notes;
  }

  // In cadence region, occasionally merge consecutive 16ths into 8ths.
  bool simplify_rhythm = is_cadence_bar &&
                         rng::rollProbability(rng, rhythm_simplification * cadence_prog);

  int degree_count = static_cast<int>(pattern.degrees.size());
  int note_idx = 0;

  for (int beat_idx = 0; beat_idx < kBeatsPerBar; ++beat_idx) {
    for (int sub_idx = 0; sub_idx < kSubdivisionsPerBeat; ++sub_idx) {
      Tick note_tick = bar_tick +
                       static_cast<Tick>(beat_idx) * kTicksPerBeat +
                       static_cast<Tick>(sub_idx) * kSixteenthDuration;

      // Select the pattern degree (cycle through pattern.degrees).
      int pattern_degree = pattern.degrees[note_idx % degree_count];

      // Map the pattern degree to a MIDI pitch.
      uint8_t raw_pitch = patternDegreeToMidiPitch(
          harm_event.chord.root_pitch, pattern_degree, harm_event.is_minor);

      // Fit to register range.
      uint8_t pitch = fitToRegister(raw_pitch, reg_range);

      // Open string preference: if this pitch class matches an open string,
      // and the bias roll succeeds, use the open string pitch directly.
      float effective_bias = open_string_bias;
      if (is_cadence_bar) {
        // Increase open string usage in cadence (per CadenceConfig).
        effective_bias = std::min(1.0f, effective_bias + 0.2f * cadence_prog);
      }

      if (shouldPreferOpenString(pitch, instrument.open_strings,
                                 effective_bias, rng)) {
        int pitch_class = static_cast<int>(pitch) % 12;
        uint8_t open_pitch = findOpenStringPitch(
            pitch_class, instrument.open_strings, reg_range);
        if (open_pitch > 0) {
          pitch = open_pitch;
        }
      }

      // Clamp to instrument range.
      if (pitch < instrument.lowest_pitch) pitch = instrument.lowest_pitch;
      if (pitch > instrument.highest_pitch) pitch = instrument.highest_pitch;

      // Determine note duration.
      Tick duration = kSixteenthDuration;
      if (simplify_rhythm && sub_idx % 2 == 0 &&
          sub_idx + 1 < kSubdivisionsPerBeat) {
        // Merge into an 8th note.
        duration = kSixteenthDuration * 2;
        ++sub_idx;  // Skip the next subdivision
      }

      // Calculate velocity based on harmonic weight and beat position.
      uint8_t velocity = kBaseVelocity;
      if (harm_event.weight >= 1.5f) {
        velocity = static_cast<uint8_t>(
            std::min(static_cast<int>(kBaseVelocity) + kWeightedVelocityBoost, 127));
      }
      // Slight accent on beat 1 and beat 3 (strong beats in 4/4).
      if (sub_idx == 0 && (beat_idx == 0 || beat_idx == 2)) {
        velocity = static_cast<uint8_t>(std::min(static_cast<int>(velocity) + 6, 127));
      }

      NoteEvent note;
      note.start_tick = note_tick;
      note.duration = duration;
      note.pitch = pitch;
      note.velocity = velocity;
      note.voice = 0;  // Solo instrument, single voice

      notes.push_back(note);
      ++note_idx;
    }
  }

  return notes;
}

/// @brief Generate notes for the final bar of the piece.
///
/// The final bar closes in the low register with the tonic, using longer
/// note values for a definitive ending. This is a design value (not searched).
///
/// @param bar_tick Start tick of the final bar.
/// @param key_sig Key signature for tonic resolution.
/// @param instrument Instrument properties.
/// @return Vector of NoteEvent for the final bar.
std::vector<NoteEvent> generateFinalBar(Tick bar_tick,
                                        const KeySignature& key_sig,
                                        const InstrumentProps& instrument) {
  std::vector<NoteEvent> notes;

  // Final bar: resolving whole note on the tonic in the lowest comfortable octave.
  // Place tonic in the octave nearest the instrument's low range.
  int tonic_pc = static_cast<int>(key_sig.tonic);

  // Find the lowest octave placement that is playable.
  uint8_t final_pitch = instrument.lowest_pitch;
  for (int octave = 1; octave <= 6; ++octave) {
    int candidate = (octave + 1) * 12 + tonic_pc;
    if (candidate >= static_cast<int>(instrument.lowest_pitch) &&
        candidate <= static_cast<int>(instrument.highest_pitch)) {
      final_pitch = static_cast<uint8_t>(candidate);
      break;
    }
  }

  // Lead-in: a descending scale fragment in the first half of the bar.
  // 5th -> 3rd -> 2nd -> tonic, each a quarter note.
  int scale_degrees[4];
  if (key_sig.is_minor) {
    // Minor: 5th(7), 3rd(3), 2nd(2), root(0) in semitone offsets.
    scale_degrees[0] = 4;  // 5th (scale degree)
    scale_degrees[1] = 2;  // 3rd
    scale_degrees[2] = 1;  // 2nd
    scale_degrees[3] = 0;  // root
  } else {
    scale_degrees[0] = 4;  // 5th
    scale_degrees[1] = 2;  // 3rd
    scale_degrees[2] = 1;  // 2nd
    scale_degrees[3] = 0;  // root
  }

  for (int idx = 0; idx < 4; ++idx) {
    int offset = degreeToPitchOffset(scale_degrees[idx], key_sig.is_minor);
    int midi_pitch = static_cast<int>(final_pitch) + offset;
    if (midi_pitch < static_cast<int>(instrument.lowest_pitch)) {
      midi_pitch = static_cast<int>(instrument.lowest_pitch);
    }
    if (midi_pitch > static_cast<int>(instrument.highest_pitch)) {
      midi_pitch = static_cast<int>(instrument.highest_pitch);
    }

    NoteEvent note;
    note.start_tick = bar_tick + static_cast<Tick>(idx) * kTicksPerBeat;
    note.duration = kTicksPerBeat;
    note.pitch = static_cast<uint8_t>(midi_pitch);
    note.velocity = static_cast<uint8_t>(kBaseVelocity - idx * 4);  // Diminuendo
    note.voice = 0;

    notes.push_back(note);
  }

  return notes;
}

}  // namespace

// ===========================================================================
// Public API
// ===========================================================================

ArpeggioFlowResult generateArpeggioFlow(const ArpeggioFlowConfig& config) {
  ArpeggioFlowResult result;

  // ---------------------------------------------------------------------------
  // Step 0: Validate configuration
  // ---------------------------------------------------------------------------

  if (config.num_sections < 3) {
    result.success = false;
    result.error_message = "num_sections must be >= 3 for a valid GlobalArc";
    return result;
  }

  if (config.bars_per_section <= 0) {
    result.success = false;
    result.error_message = "bars_per_section must be > 0";
    return result;
  }

  // Resolve seed.
  uint32_t effective_seed = config.seed;
  if (effective_seed == 0) {
    std::random_device device;
    effective_seed = device();
    if (effective_seed == 0) effective_seed = 1;
  }
  result.seed_used = effective_seed;
  std::mt19937 rng(effective_seed);

  // ---------------------------------------------------------------------------
  // Step 1: Resolve and validate GlobalArc
  // ---------------------------------------------------------------------------

  GlobalArcConfig arc_config = config.arc;
  if (arc_config.phase_assignment.empty()) {
    arc_config = createDefaultArcConfig(config.num_sections);
  }

  if (!validateGlobalArcConfig(arc_config)) {
    result.success = false;
    result.error_message = "Invalid GlobalArcConfig: must have monotonic Ascent->Peak->Descent "
                           "with exactly 1 Peak section";
    return result;
  }

  // Verify the arc covers all sections.
  if (static_cast<int>(arc_config.phase_assignment.size()) != config.num_sections) {
    result.success = false;
    result.error_message = "GlobalArcConfig phase_assignment size does not match num_sections";
    return result;
  }

  // ---------------------------------------------------------------------------
  // Step 2: Get instrument properties
  // ---------------------------------------------------------------------------

  InstrumentProps instrument = getInstrumentProps(config.instrument);

  // ---------------------------------------------------------------------------
  // Step 3: Build harmonic timeline
  // ---------------------------------------------------------------------------

  HarmonicTimeline timeline = buildFlowTimeline(config, arc_config);

  int total_bars = config.num_sections * config.bars_per_section;
  Tick total_duration = static_cast<Tick>(total_bars) * kTicksPerBar;

  // ---------------------------------------------------------------------------
  // Step 4: Generate notes section by section
  // ---------------------------------------------------------------------------

  // Build a section_id -> ArcPhase lookup.
  auto getPhaseForSection = [&](int section_idx) -> ArcPhase {
    for (const auto& [sid, phase] : arc_config.phase_assignment) {
      if (static_cast<int>(sid) == section_idx) {
        return phase;
      }
    }
    return ArcPhase::Ascent;
  };

  std::vector<NoteEvent> all_notes;
  all_notes.reserve(static_cast<size_t>(total_bars) * 16);  // ~16 notes/bar

  // Track how many sections are in each phase for progress calculation.
  int ascent_count = 0;
  int descent_count = 0;
  for (const auto& [sid, phase] : arc_config.phase_assignment) {
    if (phase == ArcPhase::Ascent) ++ascent_count;
    if (phase == ArcPhase::Descent) ++descent_count;
  }

  int ascent_seen = 0;
  int descent_seen = 0;

  // Default open string bias for non-cadence bars.
  constexpr float kDefaultOpenStringBias = 0.3f;

  for (int section_idx = 0; section_idx < config.num_sections; ++section_idx) {
    ArcPhase phase = getPhaseForSection(section_idx);

    // Calculate phase progress (0.0 to 1.0 within the phase).
    float phase_progress = 0.0f;
    switch (phase) {
      case ArcPhase::Ascent:
        phase_progress = ascent_count > 1
            ? static_cast<float>(ascent_seen) / static_cast<float>(ascent_count - 1)
            : 1.0f;
        ++ascent_seen;
        break;
      case ArcPhase::Peak:
        phase_progress = 1.0f;  // Peak is always at maximum
        break;
      case ArcPhase::Descent:
        phase_progress = descent_count > 1
            ? static_cast<float>(descent_seen) / static_cast<float>(descent_count - 1)
            : 1.0f;
        ++descent_seen;
        break;
    }

    // Compute register range for this section.
    RegisterRange reg_range = computeRegisterRange(
        phase, phase_progress, instrument.lowest_pitch, instrument.highest_pitch);

    // Assign PatternRoles for each bar in this section.
    std::vector<PatternRole> bar_roles = assignPatternRoles(config.bars_per_section);

    for (int bar_in_section = 0; bar_in_section < config.bars_per_section;
         ++bar_in_section) {
      int global_bar_idx = section_idx * config.bars_per_section + bar_in_section;
      Tick bar_tick = static_cast<Tick>(global_bar_idx) * kTicksPerBar;

      // Check if this is the very last bar -> use the final bar generator.
      if (global_bar_idx == total_bars - 1) {
        auto final_notes = generateFinalBar(bar_tick, config.key, instrument);
        all_notes.insert(all_notes.end(), final_notes.begin(), final_notes.end());
        continue;
      }

      // Get the harmonic event for this bar.
      const HarmonicEvent& harm_event = timeline.getAt(bar_tick);

      // Get chord degrees for the current chord.
      std::vector<int> chord_degrees = getChordDegrees(harm_event.chord.quality);

      // Get PatternRole for this bar.
      PatternRole role = PatternRole::Drive;  // Default
      if (bar_in_section < static_cast<int>(bar_roles.size())) {
        role = bar_roles[static_cast<size_t>(bar_in_section)];
      }

      // Determine open string preference based on phase and cadence.
      bool use_open_strings = (phase == ArcPhase::Descent) ||
                              rng::rollProbability(rng, kDefaultOpenStringBias);

      // Generate the arpeggio pattern for this bar.
      ArpeggioPattern pattern = generatePattern(
          chord_degrees, phase, role, use_open_strings);

      // Cadence processing.
      bool is_cadence = isInCadenceRegion(
          global_bar_idx, total_bars, config.cadence.cadence_bars);
      float cad_prog = cadenceProgress(
          global_bar_idx, total_bars, config.cadence.cadence_bars);

      float open_bias = is_cadence ? config.cadence.open_string_bias
                                   : kDefaultOpenStringBias;

      // If in cadence and restrict_high_register is set, lower the ceiling.
      RegisterRange effective_range = reg_range;
      if (is_cadence && config.cadence.restrict_high_register) {
        int range_span = static_cast<int>(effective_range.high) -
                         static_cast<int>(effective_range.low);
        int reduction = static_cast<int>(static_cast<float>(range_span) * 0.3f * cad_prog);
        int new_high = static_cast<int>(effective_range.high) - reduction;
        if (new_high < static_cast<int>(effective_range.low) + 12) {
          new_high = static_cast<int>(effective_range.low) + 12;
        }
        effective_range.high = static_cast<uint8_t>(
            std::min(new_high, static_cast<int>(instrument.highest_pitch)));
      }

      // Generate notes for this bar.
      auto bar_notes = generateBarNotes(
          bar_tick, harm_event, pattern, effective_range,
          instrument, open_bias, is_cadence, cad_prog,
          config.cadence.rhythm_simplification, rng);

      all_notes.insert(all_notes.end(), bar_notes.begin(), bar_notes.end());
    }
  }

  // ---------------------------------------------------------------------------
  // Step 5: Build the output track
  // ---------------------------------------------------------------------------

  Track track;
  track.channel = kSoloChannel;
  track.program = instrument.gm_program;
  track.name = instrument.name;
  track.notes = std::move(all_notes);

  result.tracks.push_back(std::move(track));
  result.total_duration_ticks = total_duration;
  result.success = true;

  return result;
}

}  // namespace bach
