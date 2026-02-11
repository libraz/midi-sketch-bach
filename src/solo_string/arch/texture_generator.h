// Texture generation for chaconne variations (Arch system, BWV1004 style).

#ifndef BACH_SOLO_STRING_ARCH_TEXTURE_GENERATOR_H
#define BACH_SOLO_STRING_ARCH_TEXTURE_GENERATOR_H

#include <cstdint>
#include <utility>
#include <vector>

#include "core/basic_types.h"
#include "harmony/harmonic_timeline.h"
#include "harmony/key.h"
#include "solo_string/arch/variation_types.h"

namespace bach {

/// @brief Rhythmic subdivision profile for texture generation.
///
/// Each profile defines a distinct rhythmic character for a beat (480 ticks).
/// Selected per-variation based on VariationType and VariationRole.
enum class RhythmProfile : uint8_t {
  QuarterNote,   ///< 1 note/beat (480 ticks) -- Theme, sparse textures
  EighthNote,    ///< 2 notes/beat (240 ticks) -- standard SingleLine default
  DottedEighth,  ///< dotted-8th + 16th (360+120) -- French overture style
  Triplet,       ///< 3 notes/beat (160 ticks) -- dance-like ternary feel
  Sixteenth,     ///< 4 notes/beat (120 ticks) -- standard Arpeggiated default
  Mixed8th16th   ///< 8th + 2x16th (240+120+120) -- Bach's characteristic figuration
};

/// @brief Get the rhythmic subdivisions for a given profile within one beat.
///
/// Returns a vector of {offset, duration} pairs representing the rhythmic
/// grid within a single beat. The sum of durations equals beat_ticks.
///
/// @param profile The rhythm profile to use.
/// @param beat_ticks Duration of one beat in ticks (default: kTicksPerBeat = 480).
/// @return Vector of {offset, duration} pairs for one beat.
std::vector<std::pair<Tick, Tick>> getRhythmSubdivisions(
    RhythmProfile profile, Tick beat_ticks = kTicksPerBeat);

/// @brief Context for texture generation within a single chaconne variation.
///
/// Carries all the information needed to generate notes for one variation:
/// texture type, key, register bounds, rhythmic density, and climax status.
/// Built by the ChaconneEngine from the variation plan and config.
struct TextureContext {
  TextureType texture = TextureType::SingleLine;
  KeySignature key = {Key::D, true};  ///< D minor default (BWV1004)
  Tick start_tick = 0;                ///< Absolute start tick of this variation
  Tick duration_ticks = 0;            ///< Length (typically 4 bars = 7680 ticks)
  uint8_t register_low = 55;         ///< G3 (violin default low)
  uint8_t register_high = 93;        ///< A6 (violin default high)
  bool is_major_section = false;      ///< True for Illuminate variations
  bool is_climax = false;             ///< True only for Accumulate variations
  float rhythm_density = 1.0f;        ///< 1.0 = normal, 0.6 = major section cap
  uint32_t seed = 0;                  ///< RNG seed for this variation
  RhythmProfile rhythm_profile = RhythmProfile::EighthNote;  ///< Rhythmic subdivision
  VariationType variation_type = VariationType::Theme;        ///< Character of variation
};

/// @brief Generate notes for a single variation using the specified texture.
///
/// Dispatches to the appropriate texture-specific generator based on
/// ctx.texture. Each texture type produces a distinct musical character:
///
/// - SingleLine: Simple melody following chord tones (8th notes)
/// - ImpliedPolyphony: Alternating upper/lower voices (2.3-2.8 implied)
/// - FullChords: 3-4 note simultaneous chords (climax only)
/// - Arpeggiated: Broken chord patterns (reuses Flow ArpeggioPattern)
/// - ScalePassage: Scale-based passage work connecting chord tones
/// - Bariolage: Open string alternation pattern
///
/// All generated notes use BachNoteSource::TextureNote provenance.
///
/// @param ctx Texture generation context with timing, register, and key info.
/// @param timeline Harmonic timeline for chord/key lookup at each tick.
/// @return Vector of NoteEvents for the variation. Empty if generation fails
///         (e.g. FullChords requested without is_climax).
std::vector<NoteEvent> generateTexture(const TextureContext& ctx,
                                       const HarmonicTimeline& timeline);

/// @brief Generate a SingleLine texture -- simple melody following chord tones.
///
/// For each beat, selects a chord tone and generates 8th-note pairs.
/// Prefers stepwise motion between notes for melodic coherence.
///
/// @param ctx Texture generation context.
/// @param timeline Harmonic timeline for chord lookup.
/// @return Vector of NoteEvents, approximately 2 notes per beat.
std::vector<NoteEvent> generateSingleLine(const TextureContext& ctx,
                                          const HarmonicTimeline& timeline);

/// @brief Generate an ImpliedPolyphony texture -- alternating upper/lower voices.
///
/// Divides the register into upper and lower halves and alternates between
/// them on 8th-note grid, creating the illusion of two simultaneous voices
/// on a single string instrument. Targets 2.3-2.8 implied voice count.
///
/// @param ctx Texture generation context.
/// @param timeline Harmonic timeline for chord lookup.
/// @return Vector of NoteEvents with register alternation pattern.
std::vector<NoteEvent> generateImpliedPolyphony(const TextureContext& ctx,
                                                const HarmonicTimeline& timeline);

/// @brief Generate a FullChords texture -- 3-4 note simultaneous chords.
///
/// Only generates output when ctx.is_climax is true (returns empty otherwise).
/// Produces arpeggiated chord rolls: first 2 notes are short grace notes
/// (60 ticks), followed by 1-2 sustained notes (beat-length). This represents
/// the physical reality of bowed string chord execution.
///
/// @param ctx Texture generation context (must have is_climax = true).
/// @param timeline Harmonic timeline for chord lookup.
/// @return Vector of NoteEvents forming chord rolls, or empty if not climax.
std::vector<NoteEvent> generateFullChords(const TextureContext& ctx,
                                          const HarmonicTimeline& timeline);

/// @brief Generate an Arpeggiated texture -- broken chord patterns.
///
/// Reuses ArpeggioPattern generation from the Flow system to create
/// 16th-note arpeggiated figures over the harmonic progression.
///
/// @param ctx Texture generation context.
/// @param timeline Harmonic timeline for chord lookup.
/// @return Vector of NoteEvents in arpeggiated 16th-note patterns.
std::vector<NoteEvent> generateArpeggiated(const TextureContext& ctx,
                                           const HarmonicTimeline& timeline);

/// @brief Generate a ScalePassage texture -- scale runs connecting chord tones.
///
/// For each beat, creates a 4-note 16th-note run between the current chord
/// tone and the next, using scale degrees from the current key. Alternates
/// ascending and descending direction for variety.
///
/// @param ctx Texture generation context.
/// @param timeline Harmonic timeline for chord lookup.
/// @return Vector of NoteEvents forming scale passage figures.
std::vector<NoteEvent> generateScalePassage(const TextureContext& ctx,
                                            const HarmonicTimeline& timeline);

/// @brief Generate a Bariolage texture -- open string alternation.
///
/// Creates rapid alternation between a stopped note (chord tone) and the
/// nearest open string. Uses violin open strings G3(55), D4(62), A4(69),
/// E5(76) by default. Produces the characteristic oscillating timbre of
/// Bach's bowed string writing (e.g. BWV 1004 Chaconne).
///
/// @param ctx Texture generation context.
/// @param timeline Harmonic timeline for chord lookup.
/// @return Vector of NoteEvents with stopped/open alternation pattern.
std::vector<NoteEvent> generateBariolage(const TextureContext& ctx,
                                         const HarmonicTimeline& timeline);

}  // namespace bach

#endif  // BACH_SOLO_STRING_ARCH_TEXTURE_GENERATOR_H
