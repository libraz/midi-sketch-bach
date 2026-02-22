// Implementation of ChaconneEngine -- BWV1004-style chaconne generation.

#include "solo_string/arch/chaconne_engine.h"

#include <algorithm>
#include <random>

#include "core/pitch_utils.h"
#include "core/rng_util.h"
#include "harmony/harmonic_timeline.h"
#include "harmony/key.h"
#include "instrument/bowed/cello_model.h"
#include "instrument/bowed/violin_model.h"
#include "instrument/fretted/guitar_model.h"
#include "solo_string/arch/bass_realizer.h"
#include "solo_string/arch/chaconne_scheme.h"
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
    case InstrumentType::Violin: {
      ViolinModel model;
      return {40, model.getLowestPitch(), model.getHighestPitch()};
    }
    case InstrumentType::Cello: {
      CelloModel model;
      return {42, model.getLowestPitch(), model.getHighestPitch()};
    }
    case InstrumentType::Guitar: {
      GuitarModel model;
      return {24, model.getLowestPitch(), model.getHighestPitch()};
    }
    default: {
      ViolinModel model;
      return {40, model.getLowestPitch(), model.getHighestPitch()};
    }
  }
}



/// @brief Select a RhythmProfile based on VariationType and VariationRole.
///
/// Maps variation character to appropriate rhythmic subdivisions with
/// weighted random selection for variety across seeds. Each VariationType
/// targets a distinct rhythmic character to ensure contrast between
/// successive variations:
///
/// - Theme: broad quarter notes (thematic statement)
/// - Lyrical: flowing 8th/dotted-8th/triplet
/// - Rhythmic: energetic dotted-8th/mixed/triplet
/// - Virtuosic: rapid 16th/mixed figuration
/// - Chordal: broad quarter notes (harmonic weight)
///
/// Role constraints override type preferences at structural boundaries:
/// - Establish: sparse (quarter/eighth) for opening statement
/// - Resolve: broad quarter or half-note feel for finality
/// - Accumulate: type-driven (Virtuosic or Chordal) for climax contrast
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
  // Resolve: primarily QuarterNote for finality, occasionally DottedEighth
  // for a graceful closing gesture rather than mechanical uniformity.
  if (role == VariationRole::Resolve) {
    return rng::rollProbability(rng, 0.80f) ? RhythmProfile::QuarterNote
                                             : RhythmProfile::DottedEighth;
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
      // Chordal variations emphasize harmonic weight with broad note values.
      // QuarterNote is the primary choice; DottedEighth provides a stately
      // French overture character. EighthNote only as a light alternative.
      std::vector<RhythmProfile> opts = {
          RhythmProfile::QuarterNote,
          RhythmProfile::DottedEighth,
          RhythmProfile::EighthNote};
      std::vector<float> wts = {0.50f, 0.30f, 0.20f};
      return rng::selectWeighted(rng, opts, wts);
    }
  }
  return RhythmProfile::EighthNote;
}

/// @brief Classify a TextureType into voice-count category for arc tracking.
///
/// Categories match BWV1004_5 reference texture density analysis:
///   - 1-voice: SingleLine, Arpeggiated, ScalePassage (single melodic line)
///   - 2-voice: ImpliedPolyphony, Bariolage (implied or actual double stops)
///   - 3-voice: FullChords (3-4 note chords, climax only)
///
/// @param texture The texture type to classify.
/// @return 1, 2, or 3 indicating the voice-count category.
int textureVoiceCategory(TextureType texture) {
  switch (texture) {
    case TextureType::SingleLine:
    case TextureType::Arpeggiated:
    case TextureType::ScalePassage:
      return 1;
    case TextureType::ImpliedPolyphony:
    case TextureType::Bariolage:
      return 2;
    case TextureType::FullChords:
      return 3;
  }
  return 1;
}

/// @brief Apply texture arc bias to override the role-selected texture.
///
/// Uses the TextureArcTarget for the variation's role to probabilistically
/// override the selected texture towards the arc's target distribution.
/// This is a soft bias: it only overrides with a probability proportional
/// to the difference between the current category and the arc target.
///
/// Anchor roles (Establish, Resolve) and Accumulate are not overridden,
/// as their textures are fixed by design.
///
/// @param rng Mersenne Twister RNG.
/// @param current_texture The texture selected by role-based logic.
/// @param role The variation's structural role.
/// @return Potentially overridden TextureType.
TextureType applyTextureArcBias(std::mt19937& rng, TextureType current_texture,
                                VariationRole role) {
  // Anchor roles and Accumulate have fixed textures -- do not override.
  if (role == VariationRole::Establish || role == VariationRole::Resolve ||
      role == VariationRole::Accumulate) {
    return current_texture;
  }

  TextureArcTarget arc = getTextureArcTarget(role);
  int category = textureVoiceCategory(current_texture);

  // If the arc strongly favors SingleLine and we picked a multi-voice texture,
  // probabilistically override to SingleLine.
  if (category > 1 && arc.single_line_ratio > 0.55f) {
    float override_prob = (arc.single_line_ratio - 0.50f);  // 0.0 to 0.20
    if (rng::rollProbability(rng, override_prob)) {
      // Pick a 1-voice texture randomly from the palette.
      return rng::selectWeighted(
          rng,
          std::vector<TextureType>{TextureType::SingleLine, TextureType::Arpeggiated,
                                   TextureType::ScalePassage},
          {0.50f, 0.30f, 0.20f});
    }
  }

  // If the arc wants more multi-voice and we picked SingleLine,
  // probabilistically override to a 2-voice texture.
  if (category == 1 && arc.double_stop_ratio + arc.chord_ratio > 0.35f) {
    float override_prob = (arc.double_stop_ratio + arc.chord_ratio - 0.30f) * 0.5f;
    if (rng::rollProbability(rng, override_prob)) {
      return rng::selectWeighted(
          rng,
          std::vector<TextureType>{TextureType::ImpliedPolyphony, TextureType::Bariolage},
          {0.60f, 0.40f});
    }
  }

  return current_texture;
}

/// @brief Build a TextureContext for a variation from its config and the engine state.
///
/// Applies texture arc bias for non-anchor roles, major section constraints
/// when appropriate, and sets climax design values directly for Accumulate
/// variations (Principle 4: Trust Design Values).
/// Accumulate variations are differentiated by accumulate_index (0=build-up,
/// 1=peak, 2+=wind-down).
///
/// @param variation The variation configuration.
/// @param offset_tick Absolute start tick for this variation.
/// @param bass_length Duration of one ground bass cycle in ticks.
/// @param profile Instrument register/program info.
/// @param climax_design Climax design values from the config.
/// @param major_constraints Major section constraints from the config.
/// @param seed RNG seed for this variation.
/// @param rhythm_profile Selected rhythm profile for this variation.
/// @param accumulate_index Position within Accumulate block (0-based), or -1 if not Accumulate.
/// @return Configured TextureContext.
TextureContext buildTextureContext(const ChaconneVariation& variation,
                                  Tick offset_tick, Tick bass_length,
                                  const InstrumentProfile& profile,
                                  const ClimaxDesign& climax_design,
                                  const MajorSectionConstraints& major_constraints,
                                  uint32_t seed,
                                  RhythmProfile rhythm_profile,
                                  int accumulate_index) {
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

  // Apply texture arc bias for non-anchor roles (soft override toward BWV1004 distribution).
  {
    std::mt19937 arc_rng(seed + 0xA2C0u);  // Separate RNG stream for arc bias.
    ctx.texture = applyTextureArcBias(arc_rng, ctx.texture, variation.role);
  }

  // Apply seed-dependent register variation for non-anchor, non-climax variations.
  // Establish, Resolve, and Accumulate retain design-fixed register.
  if (variation.role != VariationRole::Establish &&
      variation.role != VariationRole::Resolve &&
      variation.role != VariationRole::Accumulate) {
    std::mt19937 reg_rng(seed);
    int reg_offset = rng::rollRange(reg_rng, -3, 3);
    ctx.register_low = static_cast<uint8_t>(clampPitch(
        static_cast<int>(profile.register_low) + reg_offset,
        profile.register_low, profile.register_high));
    ctx.register_high = static_cast<uint8_t>(clampPitch(
        static_cast<int>(profile.register_high) - std::abs(reg_offset),
        profile.register_low, profile.register_high));
    // Ensure minimum range of 12 semitones.
    if (ctx.register_high - ctx.register_low < 12) {
      ctx.register_high = static_cast<uint8_t>(std::min(
          static_cast<int>(ctx.register_low) + 12,
          static_cast<int>(profile.register_high)));
    }
  }

  // Apply major section constraints: lighter textures, lower density, narrower register.
  if (variation.is_major_section) {
    ctx.register_low = major_constraints.register_low;
    ctx.register_high = major_constraints.register_high;
    ctx.rhythm_density = major_constraints.rhythm_density_cap;
  }

  // Apply Accumulate differentiation based on position within the climax block.
  // Principle 4: Trust Design Values, but allow staged build-up across 3 variations.
  if (variation.role == VariationRole::Accumulate) {
    ctx.is_climax = true;
    switch (accumulate_index) {
      case 0:
        // Build-up: ImpliedPolyphony with slightly narrowed register.
        ctx.texture = TextureType::ImpliedPolyphony;
        ctx.register_low = climax_design.fixed_register_low;
        ctx.register_high = static_cast<uint8_t>(std::max(
            static_cast<int>(climax_design.fixed_register_high) - 5,
            static_cast<int>(climax_design.fixed_register_low) + 12));
        break;
      case 1:
        // Climax peak: FullChords with full register.
        ctx.register_low = climax_design.fixed_register_low;
        ctx.register_high = climax_design.fixed_register_high;
        if (climax_design.allow_full_chords) {
          ctx.texture = climax_design.fixed_texture;
        }
        break;
      default: {
        // Wind-down: seed-dependent texture, register narrowed from below.
        ctx.register_low = static_cast<uint8_t>(std::min(
            static_cast<int>(climax_design.fixed_register_low) + 3,
            static_cast<int>(climax_design.fixed_register_high) - 12));
        ctx.register_high = climax_design.fixed_register_high;
        // 50/50 between FullChords and ImpliedPolyphony based on seed.
        std::mt19937 accum_rng(seed);
        if (climax_design.allow_full_chords && rng::rollProbability(accum_rng, 0.5f)) {
          ctx.texture = climax_design.fixed_texture;
        } else {
          ctx.texture = TextureType::ImpliedPolyphony;
        }
        break;
      }
    }
  }

  return ctx;
}


}  // namespace

// ===========================================================================
// Public API
// ===========================================================================

ChaconneResult generateChaconne(const ChaconneConfig& config) {
  ChaconneResult result;

  // -----------------------------------------------------------------------
  // Step 1: Initialize seed and harmonic scheme.
  // -----------------------------------------------------------------------
  uint32_t seed = config.seed;
  if (seed == 0) {
    seed = rng::generateRandomSeed();
  }
  result.seed_used = seed;

  std::mt19937 rng(seed);

  InstrumentProfile profile = getInstrumentProfile(config.instrument);

  // Create the immutable harmonic scheme.
  ChaconneScheme scheme;
  if (!config.custom_scheme.empty()) {
    scheme = ChaconneScheme(config.custom_scheme);
  } else {
    scheme = ChaconneScheme::createForKey(config.key);
  }

  if (scheme.size() == 0) {
    result.success = false;
    result.error_message = "Harmonic scheme is empty; cannot generate chaconne";
    return result;
  }

  Tick bass_length = scheme.getLengthTicks();

  // -----------------------------------------------------------------------
  // Step 2: Variation plan.
  // -----------------------------------------------------------------------
  std::vector<ChaconneVariation> variations;
  if (!config.variations.empty()) {
    variations = config.variations;
  } else if (config.target_variations > 0) {
    variations = createScaledVariationPlan(config.key, config.target_variations, rng);
  } else {
    variations = createStandardVariationPlan(config.key, rng);
  }

  auto plan_report = validateVariationPlanReport(variations);
  if (plan_report.hasCritical()) {
    result.success = false;
    result.error_message = "Invalid variation plan: " + plan_report.toJson();
    return result;
  }

  // -----------------------------------------------------------------------
  // Step 3-4: Generate each variation.
  // -----------------------------------------------------------------------
  std::vector<NoteEvent> all_notes;
  size_t estimated_notes = variations.size() *
      (scheme.size() * 4 + static_cast<size_t>(bass_length / kTicksPerBar) * 12);
  all_notes.reserve(estimated_notes);

  HarmonicTimeline full_timeline;
  std::vector<RhythmProfile> prev_profiles;
  int accumulate_count_so_far = 0;

  for (size_t var_idx = 0; var_idx < variations.size(); ++var_idx) {
    const auto& variation = variations[var_idx];
    int accumulate_index = -1;
    if (variation.role == VariationRole::Accumulate) {
      accumulate_index = accumulate_count_so_far++;
    }
    Tick offset_tick = static_cast<Tick>(var_idx) * bass_length;

    // Step 4a: Generate bass line from harmonic scheme (role-dependent).
    uint32_t var_seed = rng::splitmix32(seed, static_cast<uint32_t>(var_idx));

    std::vector<NoteEvent> bass_notes = realizeBass(
        scheme, variation.key, variation.role,
        profile.register_low, profile.register_high,
        var_seed,
        std::max(accumulate_index, 0));

    // Offset bass notes to this variation's position.
    for (auto& note : bass_notes) {
      note.start_tick += offset_tick;
    }
    all_notes.insert(all_notes.end(), bass_notes.begin(), bass_notes.end());

    // Step 4b: Build harmonic timeline from scheme (immutable per variation).
    HarmonicTimeline timeline = scheme.toTimeline(variation.key, bass_length);

    for (const auto& ev : timeline.events()) {
      HarmonicEvent offset_ev = ev;
      offset_ev.tick += offset_tick;
      offset_ev.end_tick += offset_tick;
      full_timeline.addEvent(offset_ev);
    }

    // Step 4c: Select rhythm profile and build texture context.
    std::mt19937 var_rng(var_seed);
    RhythmProfile rhythm = selectRhythmProfile(var_rng, variation.type, variation.role);

    // Avoid consecutive identical rhythm profiles to ensure audible contrast
    // between adjacent variations. Re-roll up to 3 times if a conflict is found.
    for (int reroll = 0; reroll < 3; ++reroll) {
      bool conflicts = (!prev_profiles.empty() && rhythm == prev_profiles.back()) ||
                       (prev_profiles.size() >= 2 &&
                        rhythm == prev_profiles[prev_profiles.size() - 2]);
      if (!conflicts) break;
      rhythm = selectRhythmProfile(var_rng, variation.type, variation.role);
    }
    prev_profiles.push_back(rhythm);

    TextureContext ctx = buildTextureContext(
        variation, offset_tick, bass_length, profile,
        config.climax, config.major_constraints, var_seed, rhythm,
        accumulate_index);

    // Step 4d: Generate texture notes with retry.
    std::vector<NoteEvent> texture_notes;
    bool texture_success = false;

    for (int retry = 0; retry <= config.max_variation_retries; ++retry) {
      if (retry > 0) {
        ctx.seed = rng::splitmix32(var_seed, static_cast<uint32_t>(retry) + 100u);
      }

      texture_notes = generateTexture(ctx, timeline);

      if (!texture_notes.empty()) {
        texture_success = true;
        break;
      }

      if (ctx.texture == TextureType::FullChords && !ctx.is_climax) {
        ctx.texture = TextureType::ImpliedPolyphony;
      }
    }

    if (texture_success) {
      all_notes.insert(all_notes.end(), texture_notes.begin(), texture_notes.end());
    }
  }

  // -----------------------------------------------------------------------
  // Step 5: Verify harmonic scheme integrity (per-variation check).
  // -----------------------------------------------------------------------
  // The full_timeline is a concatenation of per-variation timelines. Verify
  // each variation's segment independently against the scheme, since
  // verifyIntegrityReport expects exactly scheme.size() events.
  {
    size_t events_per_var = scheme.size();
    const auto& all_events = full_timeline.events();
    for (size_t var_idx = 0; var_idx < variations.size(); ++var_idx) {
      size_t start_idx = var_idx * events_per_var;
      if (start_idx + events_per_var > all_events.size()) {
        result.success = false;
        result.error_message = "Variation " + std::to_string(var_idx) +
            ": insufficient timeline events";
        return result;
      }

      // Extract this variation's events into a temporary timeline.
      Tick var_offset = static_cast<Tick>(var_idx) * bass_length;
      HarmonicTimeline var_timeline;
      for (size_t eidx = start_idx; eidx < start_idx + events_per_var; ++eidx) {
        HarmonicEvent ev = all_events[eidx];
        // Remove the variation offset so the timeline is 0-based.
        ev.tick -= var_offset;
        ev.end_tick -= var_offset;
        var_timeline.addEvent(ev);
      }

      auto integrity_report = scheme.verifyIntegrityReport(var_timeline);
      if (integrity_report.hasCritical()) {
        result.success = false;
        result.error_message = "Harmonic scheme integrity check failed: " +
            integrity_report.toJson();
        return result;
      }
    }
  }

  // -----------------------------------------------------------------------
  // Step 6: Separate bass and texture, then clean up each independently.
  // -----------------------------------------------------------------------
  // BWV 1004/5 is written with implicit polyphony (2-voice 20%, 3-voice 19%).
  // Bass and texture are independent voices that belong on separate tracks.
  // Cleaning up overlaps within each track independently avoids the problem
  // of cross-voice overlap truncation destroying bass note durations.

  std::vector<NoteEvent> bass_notes;
  std::vector<NoteEvent> texture_notes;
  bass_notes.reserve(all_notes.size() / 4);
  texture_notes.reserve(all_notes.size());

  for (auto& note : all_notes) {
    if (note.source == BachNoteSource::ChaconneBass ||
        note.source == BachNoteSource::GroundBass) {
      bass_notes.push_back(std::move(note));
    } else {
      texture_notes.push_back(std::move(note));
    }
  }
  all_notes.clear();  // Release memory; no longer needed.

  // --- Overlap cleanup helper (applied to each track independently) ---
  // Three-step cleanup for monophonic voice:
  //   6a: Remove exact duplicates (same tick + same pitch), keeping longer.
  //   6b: Stagger same-tick chord notes by kChordStagger ticks each.
  //   6c: Truncate ALL remaining time-based overlaps unconditionally.
  constexpr Tick kChordStagger = 60;  // Matches kMinArticulatedDuration

  auto cleanupOverlaps = [](std::vector<NoteEvent>& notes) {
    if (notes.empty()) return;

    // Step 6a: Remove exact duplicates (same tick, same voice, same pitch).
    std::sort(notes.begin(), notes.end(),
              [](const NoteEvent& lhs, const NoteEvent& rhs) {
                if (lhs.voice != rhs.voice) return lhs.voice < rhs.voice;
                if (lhs.start_tick != rhs.start_tick) return lhs.start_tick < rhs.start_tick;
                if (lhs.pitch != rhs.pitch) return lhs.pitch < rhs.pitch;
                return lhs.duration > rhs.duration;
              });
    notes.erase(
        std::unique(notes.begin(), notes.end(),
                    [](const NoteEvent& lhs, const NoteEvent& rhs) {
                      return lhs.voice == rhs.voice &&
                             lhs.start_tick == rhs.start_tick &&
                             lhs.pitch == rhs.pitch;
                    }),
        notes.end());

    // Step 6b: Stagger same-tick notes by kChordStagger ticks each.
    std::sort(notes.begin(), notes.end(),
              [](const NoteEvent& lhs, const NoteEvent& rhs) {
                if (lhs.start_tick != rhs.start_tick) return lhs.start_tick < rhs.start_tick;
                return lhs.pitch < rhs.pitch;
              });
    for (size_t idx = 0; idx < notes.size(); /* advanced inside */) {
      Tick group_tick = notes[idx].start_tick;
      size_t group_end = idx + 1;
      while (group_end < notes.size() &&
             notes[group_end].start_tick == group_tick) {
        ++group_end;
      }
      size_t group_size = group_end - idx;
      if (group_size > 1) {
        for (size_t pos = 1; pos < group_size; ++pos) {
          Tick offset = kChordStagger * static_cast<Tick>(pos);
          notes[idx + pos].start_tick += offset;
          if (notes[idx + pos].duration > offset) {
            notes[idx + pos].duration -= offset;
          } else {
            notes[idx + pos].duration = 1;
          }
        }
      }
      idx = group_end;
    }

    // Step 6c: Truncate remaining time-based overlaps.
    std::sort(notes.begin(), notes.end(),
              [](const NoteEvent& lhs, const NoteEvent& rhs) {
                if (lhs.start_tick != rhs.start_tick) return lhs.start_tick < rhs.start_tick;
                return lhs.pitch < rhs.pitch;
              });
    for (size_t idx = 0; idx + 1 < notes.size(); ++idx) {
      Tick end_tick = notes[idx].start_tick + notes[idx].duration;
      if (end_tick > notes[idx + 1].start_tick) {
        Tick new_dur = notes[idx + 1].start_tick - notes[idx].start_tick;
        if (new_dur == 0) new_dur = 1;
        notes[idx].duration = new_dur;
        notes[idx].modified_by |= static_cast<uint8_t>(NoteModifiedBy::OverlapTrim);
      }
    }
  };

  cleanupOverlaps(bass_notes);
  cleanupOverlaps(texture_notes);

  // Clamp excessive leaps (>12 semitones) in texture notes only.
  // Bass notes preserve structural integrity.
  std::sort(texture_notes.begin(), texture_notes.end(),
            [](const NoteEvent& lhs, const NoteEvent& rhs) {
              if (lhs.start_tick != rhs.start_tick) return lhs.start_tick < rhs.start_tick;
              return lhs.pitch < rhs.pitch;
            });
  for (size_t idx = 1; idx < texture_notes.size(); ++idx) {
    int leap = absoluteInterval(texture_notes[idx].pitch, texture_notes[idx - 1].pitch);
    if (leap > 12) {
      int pc = getPitchClass(texture_notes[idx].pitch);
      int prev = static_cast<int>(texture_notes[idx - 1].pitch);
      int best = static_cast<int>(texture_notes[idx].pitch);
      int best_dist = leap;
      for (int oct = 0; oct <= 10; ++oct) {
        int cand = oct * 12 + pc;
        if (cand < static_cast<int>(profile.register_low) ||
            cand > static_cast<int>(profile.register_high)) continue;
        int dist = std::abs(cand - prev);
        if (dist < best_dist) {
          best_dist = dist;
          best = cand;
        }
      }
      texture_notes[idx].pitch = static_cast<uint8_t>(best);
      texture_notes[idx].modified_by |= static_cast<uint8_t>(NoteModifiedBy::OctaveAdjust);
    }
  }

  // -----------------------------------------------------------------------
  // Step 7: Assemble two tracks (bass + texture).
  // -----------------------------------------------------------------------
  std::string instrument_name;
  switch (config.instrument) {
    case InstrumentType::Violin: instrument_name = "Violin"; break;
    case InstrumentType::Cello:  instrument_name = "Cello";  break;
    case InstrumentType::Guitar: instrument_name = "Guitar";  break;
    default:                     instrument_name = "Solo String"; break;
  }

  // Track 0: Bass (channel 0).
  Track bass_track;
  bass_track.channel = 0;
  bass_track.program = profile.program;
  bass_track.name = instrument_name + " Bass";
  bass_track.notes = std::move(bass_notes);

  // Track 1: Texture (channel 1, same program for same timbre).
  Track texture_track;
  texture_track.channel = 1;
  texture_track.program = profile.program;
  texture_track.name = instrument_name;
  texture_track.notes = std::move(texture_notes);

  Tick total_duration = static_cast<Tick>(variations.size()) * bass_length;
  for (const auto& note : bass_track.notes) {
    Tick note_end = note.start_tick + note.duration;
    if (note_end > total_duration) total_duration = note_end;
  }
  for (const auto& note : texture_track.notes) {
    Tick note_end = note.start_tick + note.duration;
    if (note_end > total_duration) total_duration = note_end;
  }

  result.tracks.push_back(std::move(bass_track));
  result.tracks.push_back(std::move(texture_track));
  result.total_duration_ticks = total_duration;
  result.timeline = std::move(full_timeline);
  result.success = true;
  return result;
}

}  // namespace bach
