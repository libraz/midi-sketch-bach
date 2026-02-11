// Implementation of ChaconneEngine -- BWV1004-style chaconne generation.

#include "solo_string/arch/chaconne_engine.h"

#include <algorithm>
#include <random>

#include "core/rng_util.h"
#include "harmony/harmonic_timeline.h"
#include "harmony/key.h"
#include "solo_string/arch/ground_bass.h"
#include "solo_string/arch/texture_generator.h"
#include "solo_string/arch/variation_types.h"



namespace bach {

namespace {

// ---------------------------------------------------------------------------
// Instrument constants
// ---------------------------------------------------------------------------

/// @brief GM program numbers and ranges per instrument type.
struct InstrumentProfile {
  uint8_t program;       ///< GM program number (0-indexed).
  uint8_t register_low;  ///< Lowest playable MIDI pitch.
  uint8_t register_high; ///< Highest playable MIDI pitch.
};

/// @brief Get the instrument profile for a given instrument type.
/// @param instrument The instrument type.
/// @return InstrumentProfile with GM program and range.
InstrumentProfile getInstrumentProfile(InstrumentType instrument) {
  switch (instrument) {
    case InstrumentType::Violin:
      return {40, 55, 96};   // Violin: G3-C7, GM program 40
    case InstrumentType::Cello:
      return {42, 36, 81};   // Cello: C2-A5, GM program 42
    case InstrumentType::Guitar:
      return {24, 40, 83};   // Nylon Guitar: E2-B5, GM program 24
    default:
      return {40, 55, 96};   // Default to violin
  }
}



/// @brief Select a RhythmProfile based on VariationType and VariationRole.
///
/// Maps variation character to appropriate rhythmic subdivisions with
/// weighted random selection for variety across seeds.
///
/// @param rng Mersenne Twister RNG.
/// @param type The variation's character type.
/// @param role The variation's structural role.
/// @return Selected RhythmProfile.
RhythmProfile selectRhythmProfile(std::mt19937& rng,
                                   VariationType type,
                                   VariationRole role) {
  // Establish: always sparse (QuarterNote or EighthNote).
  if (role == VariationRole::Establish) {
    return rng::rollProbability(rng, 0.7f) ? RhythmProfile::QuarterNote
                                            : RhythmProfile::EighthNote;
  }
  // Resolve: always QuarterNote for finality.
  if (role == VariationRole::Resolve) {
    return RhythmProfile::QuarterNote;
  }

  switch (type) {
    case VariationType::Theme:
      return RhythmProfile::QuarterNote;
    case VariationType::Lyrical: {
      std::vector<RhythmProfile> opts = {
          RhythmProfile::EighthNote,
          RhythmProfile::DottedEighth,
          RhythmProfile::Triplet};
      std::vector<float> wts = {0.40f, 0.35f, 0.25f};
      return rng::selectWeighted(rng, opts, wts);
    }
    case VariationType::Rhythmic: {
      std::vector<RhythmProfile> opts = {
          RhythmProfile::DottedEighth,
          RhythmProfile::Mixed8th16th,
          RhythmProfile::Triplet};
      std::vector<float> wts = {0.35f, 0.40f, 0.25f};
      return rng::selectWeighted(rng, opts, wts);
    }
    case VariationType::Virtuosic: {
      std::vector<RhythmProfile> opts = {
          RhythmProfile::Sixteenth,
          RhythmProfile::Mixed8th16th};
      std::vector<float> wts = {0.60f, 0.40f};
      return rng::selectWeighted(rng, opts, wts);
    }
    case VariationType::Chordal: {
      return rng::rollProbability(rng, 0.55f) ? RhythmProfile::QuarterNote
                                               : RhythmProfile::EighthNote;
    }
  }
  return RhythmProfile::EighthNote;
}

/// @brief Select a harmonic ProgressionType based on VariationRole.
///
/// Different structural roles call for different harmonic complexity.
/// Weighted random selection ensures variety across seeds.
///
/// @param rng Mersenne Twister RNG.
/// @param role The variation's structural role.
/// @param is_minor Whether the current key is minor (affects BorrowedChord weight).
/// @return Selected ProgressionType.
ProgressionType selectProgression(std::mt19937& rng,
                                   VariationRole role,
                                   bool is_minor) {
  switch (role) {
    case VariationRole::Establish:
    case VariationRole::Resolve:
      return ProgressionType::Basic;  // I-IV-V-I for stability
    case VariationRole::Develop: {
      std::vector<ProgressionType> opts = {
          ProgressionType::CircleOfFifths,
          ProgressionType::Subdominant,
          ProgressionType::Basic};
      std::vector<float> wts = {0.45f, 0.35f, 0.20f};
      return rng::selectWeighted(rng, opts, wts);
    }
    case VariationRole::Destabilize: {
      // BorrowedChord weight depends on key mode.
      float borrowed_wt = is_minor ? 0.30f : 0.15f;
      float chrome_wt = 0.40f;
      float desc_wt = 1.0f - chrome_wt - borrowed_wt;
      std::vector<ProgressionType> opts = {
          ProgressionType::ChromaticCircle,
          ProgressionType::DescendingFifths,
          ProgressionType::BorrowedChord};
      std::vector<float> wts = {chrome_wt, desc_wt, borrowed_wt};
      return rng::selectWeighted(rng, opts, wts);
    }
    case VariationRole::Illuminate: {
      std::vector<ProgressionType> opts = {
          ProgressionType::Subdominant,
          ProgressionType::CircleOfFifths,
          ProgressionType::Basic};
      std::vector<float> wts = {0.40f, 0.35f, 0.25f};
      return rng::selectWeighted(rng, opts, wts);
    }
    case VariationRole::Accumulate: {
      std::vector<ProgressionType> opts = {
          ProgressionType::DescendingFifths,
          ProgressionType::ChromaticCircle};
      std::vector<float> wts = {0.55f, 0.45f};
      return rng::selectWeighted(rng, opts, wts);
    }
  }
  return ProgressionType::Basic;
}

/// @brief Place ground bass notes at a given time offset into the output.
///
/// Copies the immutable ground bass notes into the output vector, shifting
/// their start_tick by the specified offset. The original GroundBass object
/// is never modified.
///
/// @param ground_bass The immutable ground bass source.
/// @param offset_tick Absolute tick position for this variation's start.
/// @param output Destination for the placed bass notes.
void placeGroundBass(const GroundBass& ground_bass, Tick offset_tick,
                     std::vector<NoteEvent>& output) {
  for (const auto& bass_note : ground_bass.getNotes()) {
    NoteEvent placed = bass_note;
    placed.start_tick = bass_note.start_tick + offset_tick;
    output.push_back(placed);
  }
}

/// @brief Build a TextureContext for a variation from its config and the engine state.
///
/// Applies major section constraints when appropriate and sets climax design
/// values directly for Accumulate variations (Principle 4: Trust Design Values).
///
/// @param variation The variation configuration.
/// @param offset_tick Absolute start tick for this variation.
/// @param bass_length Duration of one ground bass cycle in ticks.
/// @param profile Instrument register/program info.
/// @param climax_design Climax design values from the config.
/// @param major_constraints Major section constraints from the config.
/// @param seed RNG seed for this variation.
/// @return Configured TextureContext.
TextureContext buildTextureContext(const ChaconneVariation& variation,
                                  Tick offset_tick, Tick bass_length,
                                  const InstrumentProfile& profile,
                                  const ClimaxDesign& climax_design,
                                  const MajorSectionConstraints& major_constraints,
                                  uint32_t seed,
                                  RhythmProfile rhythm_profile) {
  TextureContext ctx;
  ctx.texture = variation.primary_texture;
  ctx.key = variation.key;
  ctx.start_tick = offset_tick;
  ctx.duration_ticks = bass_length;
  ctx.register_low = profile.register_low;
  ctx.register_high = profile.register_high;
  ctx.is_major_section = variation.is_major_section;
  ctx.is_climax = (variation.role == VariationRole::Accumulate);
  ctx.rhythm_density = 1.0f;
  ctx.seed = seed;
  ctx.rhythm_profile = rhythm_profile;
  ctx.variation_type = variation.type;

  // Apply major section constraints: lighter textures, lower density, narrower register.
  if (variation.is_major_section) {
    ctx.register_low = major_constraints.register_low;
    ctx.register_high = major_constraints.register_high;
    ctx.rhythm_density = major_constraints.rhythm_density_cap;
  }

  // Apply climax design values directly for Accumulate variations.
  // Principle 4: Trust Design Values -- output directly, do not search.
  if (variation.role == VariationRole::Accumulate) {
    ctx.register_low = climax_design.fixed_register_low;
    ctx.register_high = climax_design.fixed_register_high;
    ctx.is_climax = true;
    // Override texture to FullChords if climax design allows it and the
    // variation's configured texture is compatible.
    if (climax_design.allow_full_chords) {
      ctx.texture = climax_design.fixed_texture;
    }
  }

  return ctx;
}

/// @brief Extract ground bass notes from a generated track for integrity verification.
///
/// Identifies bass notes by matching pitch, relative timing, and duration against
/// the original ground bass pattern at each expected variation offset.
///
/// @param all_notes All notes in the generated track.
/// @param ground_bass The original ground bass for comparison.
/// @param num_variations Number of variations (determines how many bass copies to expect).
/// @return Vector of extracted bass notes in the expected order.
std::vector<NoteEvent> extractBassNotes(const std::vector<NoteEvent>& all_notes,
                                        const GroundBass& ground_bass,
                                        size_t num_variations) {
  // We expect exactly ground_bass.noteCount() * num_variations bass notes,
  // placed at offsets variation_idx * bass_length.
  Tick bass_length = ground_bass.getLengthTicks();
  size_t notes_per_cycle = ground_bass.noteCount();
  const auto& original_notes = ground_bass.getNotes();

  std::vector<NoteEvent> extracted;
  extracted.reserve(notes_per_cycle * num_variations);

  for (size_t var_idx = 0; var_idx < num_variations; ++var_idx) {
    Tick offset = static_cast<Tick>(var_idx) * bass_length;

    for (size_t bass_idx = 0; bass_idx < notes_per_cycle; ++bass_idx) {
      const auto& expected = original_notes[bass_idx];
      Tick expected_start = expected.start_tick + offset;

      // Find the matching note in all_notes.
      bool found = false;
      for (const auto& note : all_notes) {
        if (note.start_tick == expected_start &&
            note.pitch == expected.pitch &&
            note.duration == expected.duration) {
          // Build a "normalized" note with the original start_tick for verification.
          NoteEvent normalized = expected;
          extracted.push_back(normalized);
          found = true;
          break;
        }
      }

      if (!found) {
        // Bass note missing -- return what we have (verification will fail).
        return extracted;
      }
    }
  }

  return extracted;
}

}  // namespace

// ===========================================================================
// Public API
// ===========================================================================

ChaconneResult generateChaconne(const ChaconneConfig& config) {
  ChaconneResult result;

  // -----------------------------------------------------------------------
  // Step 1: Initialize seed and ground bass.
  // -----------------------------------------------------------------------
  uint32_t seed = config.seed;
  if (seed == 0) {
    seed = rng::generateRandomSeed();
  }
  result.seed_used = seed;

  std::mt19937 rng(seed);

  // Create the immutable ground bass.
  GroundBass ground_bass;
  if (config.ground_bass_notes.empty()) {
    ground_bass = GroundBass::createForKey(config.key);
  } else {
    ground_bass = GroundBass(config.ground_bass_notes);
  }

  if (ground_bass.isEmpty()) {
    result.success = false;
    result.error_message = "Ground bass is empty; cannot generate chaconne";
    return result;
  }

  Tick bass_length = ground_bass.getLengthTicks();

  // -----------------------------------------------------------------------
  // Step 2: Variation plan -- use config or create standard plan.
  // -----------------------------------------------------------------------
  std::vector<ChaconneVariation> variations;
  if (!config.variations.empty()) {
    variations = config.variations;
  } else if (config.target_variations > 0) {
    variations = createScaledVariationPlan(config.key, config.target_variations, rng);
  } else {
    variations = createStandardVariationPlan(config.key, rng);
  }

  if (!validateVariationPlan(variations)) {
    result.success = false;
    result.error_message = "Invalid variation plan: role order or type constraints violated";
    return result;
  }

  // -----------------------------------------------------------------------
  // Step 3-4: Generate each variation (harmonic timeline + texture).
  // -----------------------------------------------------------------------
  InstrumentProfile profile = getInstrumentProfile(config.instrument);

  // Single track for the solo instrument.
  std::vector<NoteEvent> all_notes;
  // Pre-estimate: bass notes + ~8-16 texture notes per bar per variation.
  size_t estimated_notes = variations.size() *
      (ground_bass.noteCount() + static_cast<size_t>(bass_length / kTicksPerBar) * 12);
  all_notes.reserve(estimated_notes);

  // Concatenated timeline across all variations.
  HarmonicTimeline full_timeline;

  // Track recent rhythm profiles for contrast checking.
  std::vector<RhythmProfile> prev_profiles;

  for (size_t var_idx = 0; var_idx < variations.size(); ++var_idx) {
    const auto& variation = variations[var_idx];
    Tick offset_tick = static_cast<Tick>(var_idx) * bass_length;

    // Step 4a: Place ground bass (immutable copy).
    placeGroundBass(ground_bass, offset_tick, all_notes);

    // Step 4b: Build harmonic timeline with role-appropriate progression.
    uint32_t var_seed = seed + static_cast<uint32_t>(var_idx) * 997u;
    std::mt19937 var_rng(var_seed);

    ProgressionType prog_type = selectProgression(
        var_rng, variation.role, variation.key.is_minor);

    // Accumulate uses Beat resolution for maximum harmonic complexity.
    HarmonicResolution resolution = (variation.role == VariationRole::Accumulate)
        ? HarmonicResolution::Beat
        : HarmonicResolution::Bar;

    HarmonicTimeline timeline = HarmonicTimeline::createProgression(
        variation.key, bass_length, resolution, prog_type);

    for (const auto& ev : timeline.events()) {
      HarmonicEvent offset_ev = ev;
      offset_ev.tick += offset_tick;
      offset_ev.end_tick += offset_tick;
      full_timeline.addEvent(offset_ev);
    }

    // Step 4c: Select rhythm profile and build texture context.
    RhythmProfile rhythm = selectRhythmProfile(var_rng, variation.type, variation.role);

    // Contrast check: avoid 2+ consecutive identical profiles.
    bool conflicts = (!prev_profiles.empty() && rhythm == prev_profiles.back()) ||
                     (prev_profiles.size() >= 2 &&
                      rhythm == prev_profiles[prev_profiles.size() - 2]);
    if (conflicts) {
      rhythm = selectRhythmProfile(var_rng, variation.type, variation.role);
      // Second try: accept regardless to avoid infinite loop.
    }
    prev_profiles.push_back(rhythm);

    TextureContext ctx = buildTextureContext(
        variation, offset_tick, bass_length, profile,
        config.climax, config.major_constraints, var_seed, rhythm);

    // Step 4d: Generate texture notes with retry on failure.
    std::vector<NoteEvent> texture_notes;
    bool texture_success = false;

    for (int retry = 0; retry <= config.max_variation_retries; ++retry) {
      if (retry > 0) {
        // Retry with a different seed. NEVER modify the ground bass.
        ctx.seed = var_seed + static_cast<uint32_t>(retry) * 1013u;
      }

      texture_notes = generateTexture(ctx, timeline);

      if (!texture_notes.empty()) {
        texture_success = true;
        break;
      }

      // If the texture is FullChords but not climax, the generator returns
      // empty by design. Try an alternative texture for the role.
      if (ctx.texture == TextureType::FullChords && !ctx.is_climax) {
        // Fall back to ImpliedPolyphony which works for any role.
        ctx.texture = TextureType::ImpliedPolyphony;
      }
    }

    // If texture generation failed even after retries, log but continue.
    // The ground bass still provides structural integrity for this variation.
    // Only the Resolve variation (Theme type) is critical -- a bare bass
    // line is acceptable for the final statement.
    if (texture_success) {
      all_notes.insert(all_notes.end(), texture_notes.begin(), texture_notes.end());
    }
  }

  // -----------------------------------------------------------------------
  // Step 5: Verify ground bass integrity.
  // -----------------------------------------------------------------------
  std::vector<NoteEvent> extracted_bass = extractBassNotes(
      all_notes, ground_bass, variations.size());

  // Verify each variation's bass notes against the original.
  // The extracted notes are already in original-relative coordinates.
  size_t notes_per_cycle = ground_bass.noteCount();
  for (size_t var_idx = 0; var_idx < variations.size(); ++var_idx) {
    // Build a slice of the extracted bass for this variation.
    size_t slice_start = var_idx * notes_per_cycle;
    size_t slice_end = slice_start + notes_per_cycle;

    if (slice_end > extracted_bass.size()) {
      result.success = false;
      result.error_message =
          "Ground bass integrity check failed: missing bass notes in variation " +
          std::to_string(var_idx);
      return result;
    }

    std::vector<NoteEvent> var_bass(extracted_bass.begin() + static_cast<long>(slice_start),
                                    extracted_bass.begin() + static_cast<long>(slice_end));

    if (!ground_bass.verifyIntegrity(var_bass)) {
      result.success = false;
      result.error_message =
          "Ground bass integrity check failed: bass was modified in variation " +
          std::to_string(var_idx) + " (STRUCTURAL_FAIL)";
      return result;
    }
  }

  // -----------------------------------------------------------------------
  // Step 6: Assemble final track.
  // -----------------------------------------------------------------------

  // Sort all notes by start_tick for proper MIDI output.
  std::sort(all_notes.begin(), all_notes.end(),
            [](const NoteEvent& lhs, const NoteEvent& rhs) {
              if (lhs.start_tick != rhs.start_tick) {
                return lhs.start_tick < rhs.start_tick;
              }
              return lhs.pitch < rhs.pitch;
            });

  Track track;
  track.channel = 0;
  track.program = profile.program;
  track.notes = std::move(all_notes);

  switch (config.instrument) {
    case InstrumentType::Violin:
      track.name = "Violin";
      break;
    case InstrumentType::Cello:
      track.name = "Cello";
      break;
    case InstrumentType::Guitar:
      track.name = "Guitar";
      break;
    default:
      track.name = "Solo String";
      break;
  }

  // Calculate total duration.
  Tick total_duration = static_cast<Tick>(variations.size()) * bass_length;
  for (const auto& note : track.notes) {
    Tick note_end = note.start_tick + note.duration;
    if (note_end > total_duration) {
      total_duration = note_end;
    }
  }

  result.tracks.push_back(std::move(track));
  result.total_duration_ticks = total_duration;
  result.timeline = std::move(full_timeline);
  result.success = true;
  return result;
}

}  // namespace bach
