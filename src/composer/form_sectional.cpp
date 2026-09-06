#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

#include "composer/character_profile.h"
#include "composer/figuration.h"
#include "composer/figuration_palette.h"
#include "composer/form_builders.h"
#include "composer/material.h"
#include "composer/minor_material.h"
#include "composer/rule_helpers.h"
#include "composer/span.h"
#include "composer/subject_catalog.h"
#include "composer/texture_helpers.h"
#include "composer/tonal_answer.h"
#include "composer/voice_intent.h"
#include "core/basic_types.h"
#include "core/pitch_utils.h"

namespace bach::composer {

// ---------------------------------------------------------------------------
// Sectional concatenation forms: toccata+fugue and fantasia+fugue. Each is a
// FREE opening section (toccata or fantasia) led by V0, supported by a V2 pedal-
// point layer and V1 punctuation layer (BWV565 / BWV538 tonic / dominant pedal
// idiom), then a 3-voice fugue section to the end, assembled into one fixture.
//
// Both builders are dedicated assemblers (no longer placeholders replaying a
// proven phase fixture). They honour ResolvedRequest length, mode, character,
// and the arc curve.
//
// Free section: the toccata generalizes OrganToccata's archetype machinery to the
// available bars; the fantasia generalizes Fantasia's contrasting-section cycle.
// V0 carries the running figuration (toccata / fantasia archetype); a V2 pedal-
// point layer sustains chord-root tones (whole / half notes, occasionally a
// root<->fifth "walking pedal") beneath it, and a V1 punctuation layer strikes
// short consonant chord tones at section heads. The fugue tail is a self-
// contained 3-entry exposition + optional stretto + a 2-bar Picardy cadence,
// built inline with the PreludeAndFugue idiom (it does NOT share the fugue family's
// assembly cascade, so the two systems stay independent).
//
// EVERY note is NoteSource::Material (verbatim carriers). The validator's
// parallel / hidden-parallel / vertical-dissonance / cross-relation / invertible
// rules all skip a voice pair when BOTH notes are Material, so the only
// inter-voice constraint that fires is voice_crossing. Both forms therefore
// confine each voice's material to a disjoint, strictly-ordered register band
// (V0 highest, V2 lowest), keeping V0 >= V1 >= V2 at every shared tick. The free
// section's V2 pedal and V1 punctuation pick their pitches via the shared tier-
// scored consonantChordTone selector, which reads back V0's sounding pitches
// (ThemeToneRegistry) so the added tones stay consonant, parallel-free, and
// below the concurrent V0 figuration.
// ---------------------------------------------------------------------------

namespace {

using detail::ChordSpec;                    // NOLINT(build/namespaces)
using detail::Mode;                         // NOLINT(build/namespaces)
using detail::subjectIndexFor;              // NOLINT(build/namespaces)
using tables::kSubjectCatalogMajor;         // NOLINT(build/namespaces)
using tables::kSubjectCatalogMajorRhythms;  // NOLINT(build/namespaces)
using tables::kSubjectCatalogMinor;         // NOLINT(build/namespaces)
using tables::kSubjectCatalogMinorRhythms;  // NOLINT(build/namespaces)

constexpr Tick kQuarter = kTicksPerBeat;
constexpr Tick kSixteenth = kTicksPerBeat / 4;

// One subject statement is 16 catalog notes spanning 4 bars. Durations come
// from the per-mode catalog rhythm rows rather than being fixed quarters.
constexpr int kSubjectNotes = 16;
constexpr int kSubjectBars = 4;

// Per-voice register bands (MIDI) for the fugue tail. Disjoint and strictly
// ordered (V0 highest, V2 lowest) so band-confined material never crosses.
// Each band holds one subject statement (major spans 14 semitones, minor 12).
// The real answer descends a fourth before its octave placement.  Its catalog
// minimum is therefore E3 (53), so V1 begins there; V2 ends at E-flat3 (52).
// Those adjacent, non-overlapping bands keep the actual answer out of the bass
// register while still fitting every qualified subject's full range.
constexpr std::array<int, 3> kBandLo = {70, 53, 33};
constexpr std::array<int, 3> kBandHi = {88, 71, 52};

/// @brief Append a single clamped note to a material vector.
void addNote(std::vector<MaterialNote>& dst, Tick tick, Tick dur, int pitch) {
  MaterialNote note;
  note.start_tick = tick;
  note.duration = dur;
  note.pitch = static_cast<std::uint8_t>(std::clamp(pitch, 0, 127));
  dst.push_back(note);
}

/// @brief Convert a bar index to its starting tick.
Tick barTick(int bar) {
  return static_cast<Tick>(bar) * kTicksPerBar;
}

// The tone and onset length of one accompaniment bar. A layer that lays a
// single tone per bar is fully described by that pair, so a later bar carrying
// it again is the earlier bar restated note for note -- the one kind of return
// the ear hears as nothing new, because neither the pitch nor the tread has
// moved. A layer reading this answers such a return by treating the tone
// differently rather than by choosing a different tone.
using SustainedBar = std::pair<int, Tick>;

/// @brief True when this line has already laid a bar of exactly this tone and
///        length.
bool alreadySustained(const std::vector<SustainedBar>& seen, int pitch, Tick duration) {
  return std::find(seen.begin(), seen.end(), SustainedBar{pitch, duration}) != seen.end();
}

// ---------------------------------------------------------------------------
// SectionalAssembly: the accumulator both builders write into. Span ids and the
// next-id counter are shared so the concatenated free + fugue sections stay
// unique across the whole fixture.
// ---------------------------------------------------------------------------
struct SectionalAssembly {
  HarnessFixture* out = nullptr;
  SpanId* next_id = nullptr;
  // Registry of every already-placed fugue-tail note (thematic statements AND
  // figuration / countersubject accompaniment), so a line built later in the
  // deterministic voice order can read what every earlier voice sounds at a
  // given tick. This drives the shared parallel-avoidance machinery: the
  // consonance-aware figuration / countersubject anchors pick a tone that is
  // both consonant with the concurrent theme and parallel-free against every
  // earlier voice (the cardinal Bach prohibition on parallel 5ths/8ths). The
  // free-section layers keep their own local registry; this one is the tail's.
  ThemeToneRegistry theme_tones;
};

/// @brief Append a window-sliced verbatim carrier span.
void pushSpan(SectionalAssembly& asm_ctx, VoiceId voice, int first_bar, int last_bar,
              VoiceIntent intent) {
  Span span;
  span.id = (*asm_ctx.next_id)++;
  span.start_tick = barTick(first_bar);
  span.end_tick = barTick(last_bar + 1);
  span.voice = voice;
  span.intent = intent;
  span.subdivision = Subdivision::Quarter;  // unused by verbatim replay.
  asm_ctx.out->voice_plan.spans.push_back(span);
}

/// @brief Emit HarmonicPlan ChordEvents from a per-bar chord plan.
void emitHarmony(HarnessFixture& out, const std::vector<ChordSpec>& plan, Mode mode) {
  out.harmony.tonic_pc = 0;
  out.harmony.is_minor = (mode == Mode::Minor);
  for (std::size_t bar = 0; bar < plan.size(); ++bar) {
    ChordEvent chord;
    chord.start_tick = barTick(static_cast<int>(bar));
    chord.root_pc = plan[bar].root_pc;
    chord.quality = plan[bar].minor ? ChordQuality::Minor : ChordQuality::Major;
    out.harmony.chords.push_back(chord);
  }
}

// The fugue tail is a strict three-voice texture (V0 highest, V2 lowest).
constexpr VoiceId kTailVoices = 3;

/// @brief Merge consecutive same-pitch notes that abut into one held note.
void coalesceConsecutiveSamePitch(std::vector<MaterialNote>& notes) {
  if (notes.empty()) {
    return;
  }
  std::vector<MaterialNote> merged;
  merged.reserve(notes.size());
  for (const auto& note : notes) {
    if (!merged.empty() && merged.back().pitch == note.pitch &&
        merged.back().start_tick + merged.back().duration == note.start_tick) {
      merged.back().duration += note.duration;
      continue;
    }
    merged.push_back(note);
  }
  notes = std::move(merged);
}

// ---------------------------------------------------------------------------
// Free-section accompaniment layers (V2 pedal + V1 punctuation).
//
// The free opening (toccata / fantasia) is led by V0. Beneath it a V2 pedal-
// point layer sustains chord-root tones (the BWV565 / BWV538 tonic / dominant
// pedal idiom) and a V1 punctuation layer strikes short consonant chord tones at
// section heads. Both layers pick pitches via the shared consonantChordTone
// selector, reading back V0's already-built figuration through a
// ThemeToneRegistry so the added tones stay consonant, parallel-free, and below
// the concurrent V0 line (the order window keeps V0 >= V1 >= V2 at every tick).
//
// To protect the scorer's melodic-interval distribution a long static pedal is
// idiomatic but a "walking pedal" (root <-> fifth oscillation) is introduced on
// a fraction of bars so the bass is mostly-sustained with occasional movement.
// ---------------------------------------------------------------------------

// Per-bar layer requirement for the free section. `pedal` requests the V2 pedal;
// `punctuate` requests a V1 chord-tone strike at the bar head; `homophonic`
// requests a half-note V1+V2 strike together (declamatory chordal texture).
struct FreeLayerPlan {
  bool pedal = false;
  bool punctuate = false;
  bool homophonic = false;
  bool fermata = false;  // whole-note V0+V1+V2 homophonic strike (the metered breath).
};

// Register band for the free-section V1 punctuation and V2 pedal layers. These
// reuse the fugue-tail bands (V1 = [56,71], V2 = [40,55]) so the whole piece
// keeps one consistent per-voice register order; the consonantChordTone order
// window additionally clamps each pick below the concurrent V0 figuration.
constexpr int kFreeV1Lo = 56;
constexpr int kFreeV1Hi = 71;
constexpr int kFreeV2Lo = 40;
constexpr int kFreeV2Hi = 55;

/// @brief Emit the V2 pedal + V1 punctuation layers under the V0 free section.
///
/// @param asm_ctx The accumulator (spans + figuration sections).
/// @param v0_notes The already-built V0 free-section notes (registered so the
///        layers can read what V0 sounds at any tick).
/// @param plan The per-bar chord plan (root supplies the pedal pitch class).
/// @param mode Diatonic mode selecting the scale walker.
/// @param free_bars Number of free-section bars (the layer span).
/// @param layout Per-bar layer requirements (index 0..free_bars-1).
/// @param req The resolved request (seed drives the walking-pedal cadence).
/// @param v1_punct_dur Duration of a plain V1 head punctuation (a homophonic bar
///        always uses a half note so the V1+V2 strike sounds together). A longer
///        punctuation lifts the V1 voice's piece occupancy; a shorter one leaves
///        the bar to the V0 figuration for a lighter accompaniment.
void appendFreeSectionLayers(SectionalAssembly& asm_ctx, const std::vector<MaterialNote>& v0_notes,
                             const std::vector<ChordSpec>& plan, Mode mode, int free_bars,
                             const std::vector<FreeLayerPlan>& layout, const ResolvedRequest& req,
                             Tick v1_punct_dur) {
  HarnessFixture& out = *asm_ctx.out;

  // Read-back of V0's free-section figuration so each added tone can be picked
  // consonant and parallel-free against the concurrent V0 line, with the order
  // window keeping the pedal / punctuation below it.
  ThemeToneRegistry registry;
  for (const auto& note : v0_notes) {
    if (note.start_tick >= barTick(free_bars))
      break;
    registry.record(note.start_tick, /*voice=*/0, static_cast<int>(note.pitch), note.duration);
  }

  // V2 pedal section: contiguous over every pedal bar. Each bar holds the chord
  // root in the V2 band (sustained whole note), except on a "walking pedal" bar
  // (every fourth pedal bar, seed-phased) where the bar splits into two half
  // notes oscillating root -> fifth so a long static pedal does not dominate the
  // melodic-interval distribution. Marked is_pedal_prep so the validator's
  // figuration_harmonic_consistency rule exempts the sustained pedal from the
  // bar-downbeat chord-tone check.
  std::vector<ConcurrentMotion> motions;
  std::vector<int> theme_pitches;
  FigurationSection pedal_section;
  pedal_section.voice = 2;
  pedal_section.is_pedal_prep = true;
  int pedal_first = -1;
  int pedal_last = -1;
  const int walk_phase = static_cast<int>(req.seed % 4);
  // Running count of consecutive identical pedal pitches across emitted onsets
  // (a whole-note bar emits one onset; a half-note bar emits two). When a long
  // static pedal would hold the same chord-root tone past the texture-gate run
  // cap (4 identical pitches in a voice), the pedal walks to an alternate
  // consonant chord tone -- the fifth, or the third when the fifth would form a
  // parallel against an earlier voice -- so the bass keeps a little movement
  // (the BWV565 / BWV538 walking-pedal idiom) while staying consonant and
  // parallel-free. last_pedal_pitch / pedal_run track the most-recently emitted
  // pitch and how many identical onsets precede the next one.
  int last_pedal_pitch = -1;
  int pedal_run = 0;
  // Every bar this pedal has already laid, by tone and by the length of its
  // first onset (a whole bar when held, a half note when walked). A second bar
  // repeating both would be that bar restated note for note, so the pedal
  // reads this to pick the density it has not used on the tone yet.
  std::vector<SustainedBar> pedal_held;
  // Every beat the bar's pedal tone is still sounding at, read from the V0
  // figuration already placed above it. The pedal is struck once or twice a bar
  // while that line attacks four or more times, so the interval that decides
  // whether the vertical is a second inversion almost never falls where the
  // pedal is struck -- a reading taken at the onset alone sees a fraction of
  // what the tone supports.
  std::vector<int> window_pitches;
  std::vector<int> beat_pitches;
  // Whether a candidate leaves a fourth above itself anywhere it sounds. The
  // walking escape below draws on the chord's fifth, and a fifth in the bass
  // under a figuration line stating the root is an unresolved second inversion,
  // so the escape needs to see it or it trades a static run for one.
  const auto leaves_bass_fourth = [&](int cand) {
    const auto is_fourth_above = [&](int sounding) {
      return cand < sounding && isConsonantIc(cand - sounding) &&
             !rule_helpers::isConsonantAboveBass(static_cast<std::uint8_t>(sounding),
                                                 static_cast<std::uint8_t>(cand));
    };
    return std::any_of(theme_pitches.begin(), theme_pitches.end(), is_fourth_above) ||
           std::any_of(window_pitches.begin(), window_pitches.end(), is_fourth_above);
  };
  // How badly a candidate pedal tone reads against the voices already sounding
  // above it. The perfect approaches are ranked, not pooled: the band, the triad
  // and those voices constrain the candidate set at once, so a tone free of every
  // perfect approach frequently does not exist, and a guard that demanded one
  // would keep whatever it started from -- including the true parallel it was
  // called to remove. The contrary arrival at the same perfect class sits just
  // under the true parallel rather than beside the hidden: per unit rate it costs
  // the corpus roughly four times what a hidden approach does, near what a true
  // octave costs, so it is the dearest fault this bass may still pay.
  constexpr int kPedalClean = 0;
  constexpr int kPedalHidden = 1;
  constexpr int kPedalAntiParallel = 2;
  constexpr int kPedalParallel = 3;
  const auto pedal_fault_rank = [&](int cand) {
    if (last_pedal_pitch < 0) {
      return kPedalClean;
    }
    int worst = kPedalClean;
    for (const ConcurrentMotion& motion : motions) {
      if (motion.prev < 0 || motion.curr < 0) {
        continue;
      }
      if (formsStrictPerfectParallel(last_pedal_pitch, cand, motion.prev, motion.curr)) {
        return kPedalParallel;
      }
      if (formsPerfectParallel(last_pedal_pitch, cand, motion.prev, motion.curr)) {
        worst = std::max(worst, kPedalHidden);
      } else if (formsAntiParallelPerfect(last_pedal_pitch, cand, motion.prev, motion.curr)) {
        worst = std::max(worst, kPedalAntiParallel);
      }
    }
    return worst;
  };
  // Pick the pitch for the next pedal onset: the chord root, unless holding it
  // would extend an identical-pitch run to the gate cap, or the root itself
  // moves in a perfect class with a voice above it. Both escapes draw on the
  // same chord tones (third and fifth, each above and below the root) so the
  // step can be the smallest available interval rather than always a leaping
  // fifth -- a conjunct walking bass keeps the melodic-interval distribution
  // near the corpus (the BWV565 / BWV538 walking-pedal idiom).
  auto pedal_pitch = [&](int root, const ChordSpec& chord) {
    const int third_iv = chord.minor ? 3 : 4;
    const int candidates[4] = {root + third_iv, root - (12 - third_iv), root + 7, root - 5};
    int pitch = root;
    // A run at the gate cap has to break whatever the break costs vertically, so
    // it enters the sweep one rung above the worst vertical fault; a root that
    // merely sounds a perfect approach is moved only for something better.
    int start_rank = pedal_fault_rank(root);
    if (last_pedal_pitch == root && pedal_run >= 3) {
      start_rank = std::max(start_rank, kPedalParallel);
    }
    for (int accept = kPedalClean; accept < start_rank; ++accept) {
      int best = -1;
      int best_key = 1 << 30;
      for (int alt : candidates) {
        if (alt < kFreeV2Lo || alt > kFreeV2Hi || alt == root || pedal_fault_rank(alt) > accept) {
          continue;
        }
        // Conjunct still decides between two tones that read alike; the second
        // inversion only outranks it, so the escape reaches for the third before
        // the fifth when both are equally free of a perfect approach.
        const int key =
            (leaves_bass_fourth(alt) ? (1 << 12) : 0) + std::abs(alt - last_pedal_pitch);
        if (key < best_key) {
          best_key = key;
          best = alt;
        }
      }
      if (best >= 0) {
        pitch = best;
        break;
      }
    }
    pedal_run = (pitch == last_pedal_pitch) ? pedal_run + 1 : 1;
    last_pedal_pitch = pitch;
    return pitch;
  };
  for (int bar = 0; bar < free_bars; ++bar) {
    const FreeLayerPlan& pl = layout[static_cast<std::size_t>(bar)];
    if (!pl.pedal && !pl.fermata)
      continue;
    if (pedal_first < 0)
      pedal_first = bar;
    pedal_last = bar;
    const ChordSpec& chord = plan[static_cast<std::size_t>(bar)];
    const Tick bar_start = barTick(bar);
    // Root chord tone in the V2 band, nearest the band centre, consonant and
    // parallel-free against the concurrent V0 figuration.
    const int centre = (kFreeV2Lo + kFreeV2Hi) / 2;
    registry.concurrentThemePitches(bar_start, /*voice=*/2, theme_pitches);
    // One sixteenth back, not one bar: this voice contributes one tone per bar
    // while the figuration above it attacks many times inside that bar, so a
    // bar-back sample compares a motion nobody hears. The union-onset reading
    // (and the ear) pairs the two at the onset just before the bar head.
    registry.concurrentMotions(bar_start - kSixteenth, bar_start, /*voice=*/2,
                               /*num_voices=*/3, motions);
    window_pitches.clear();
    for (Tick beat = kTicksPerBeat; beat < kTicksPerBar; beat += kTicksPerBeat) {
      registry.concurrentThemePitches(bar_start + beat, /*voice=*/2, beat_pitches);
      window_pitches.insert(window_pitches.end(), beat_pitches.begin(), beat_pitches.end());
    }
    int root =
        consonantChordTone(chord, /*voice=*/2, kFreeV2Lo, kFreeV2Hi, centre, theme_pitches,
                           last_pedal_pitch, motions, mode, /*downbeat=*/true, window_pitches,
                           /*parallel_free_over_consonant=*/false, /*sustained_bass=*/true);
    if (bar == free_bars - 1) {
      // The free section's declared half cadence needs the actual lowest voice
      // on V, not merely an arbitrary member of the dominant triad. Which V is
      // not part of that: the pitch class is the design value and the octave is
      // only a convenience, so the band's dominants are ranked against the
      // voices above before the nearest to the band centre wins among equals.
      // Taking the register unranked made this the one onset in the section
      // that could not refuse a perfect approach -- the cadence bar bypasses the
      // ordinary relief below, by design, so nothing downstream would catch it.
      int best_root = -1;
      int best_rank = 1 << 30;
      int best_distance = 1 << 30;
      for (int pitch = kFreeV2Lo; pitch <= kFreeV2Hi; ++pitch) {
        if (pitch % 12 != chord.root_pc % 12)
          continue;
        const int rank = pedal_fault_rank(pitch);
        const int distance = std::abs(pitch - centre);
        if (rank < best_rank || (rank == best_rank && distance < best_distance)) {
          best_root = pitch;
          best_rank = rank;
          best_distance = distance;
        }
      }
      if (best_root >= 0)
        root = best_root;
    }
    if (pl.fermata) {
      // Metered breath: one whole-note chord root in the V2 band, struck with
      // the V0+V1 whole notes (a single homophonic strike, no re-articulation).
      // At the declared free-section half cadence the bass must remain the
      // dominant root.  The ordinary repeated-run relief is inappropriate
      // here: for some pedal histories it substituted the third/fifth after
      // the cadence-specific root selection above.
      const int pitch = bar == free_bars - 1 ? root : pedal_pitch(root, chord);
      if (bar == free_bars - 1) {
        last_pedal_pitch = pitch;
        pedal_run = 1;
      }
      addNote(pedal_section.notes, bar_start, kTicksPerBar, pitch);
      registry.record(bar_start, /*voice=*/2, pitch, kTicksPerBar);
      continue;
    }
    const bool homophonic = pl.homophonic;
    if (homophonic) {
      // Declamatory chordal texture: a half-note strike re-articulated at the
      // bar mid-point, sounding with the concurrent V1 half note. The first
      // strike anchors the chord root; the second alternates when a static run
      // would breach the gate cap.
      const int first = pedal_pitch(root, chord);
      const int second = pedal_pitch(root, chord);
      addNote(pedal_section.notes, bar_start, kTicksPerBeat * 2, first);
      addNote(pedal_section.notes, bar_start + kTicksPerBeat * 2, kTicksPerBeat * 2, second);
      registry.record(bar_start, /*voice=*/2, first, kTicksPerBeat * 2);
      registry.record(bar_start + kTicksPerBeat * 2, /*voice=*/2, second, kTicksPerBeat * 2);
      continue;
    }
    const int pitch = pedal_pitch(root, chord);
    // Which treatment this tone has already had. The pedal states a tone in one
    // of two densities -- held for the whole bar, or struck as a half note and
    // answered by its own fifth (the walking-pedal idiom) -- and a second bar
    // that repeats both the tone and the density is the earlier bar restated
    // note for note. So a returning tone takes whichever density it has not
    // been heard in yet, and the seed-phased walk decides only while both are
    // still free. The bass note itself never moves for this: what changes on
    // the return is how densely the same pedal is stated.
    const bool held_before = alreadySustained(pedal_held, pitch, kTicksPerBar);
    const bool walked_before = alreadySustained(pedal_held, pitch, kTicksPerBeat * 2);
    bool walking = (bar % 4) == walk_phase;
    if (held_before != walked_before)
      walking = !walked_before;
    if (walking) {
      // Half-note root then a half-note fifth above it (still inside the band).
      // The first tone goes through the same ranking as every other pedal onset.
      // Writing the root unranked here left this the one branch where a bar head
      // could take a perfect approach against the figuration above it and keep
      // it: the ordinary branch would have stepped to a chord tone instead. The
      // fifth is measured from the tone actually taken, so the walk keeps its
      // shape wherever the root is displaced.
      int fifth = pitch + 7;
      if (fifth > kFreeV2Hi)
        fifth = pitch - 5;  // fall to the fourth below if the fifth overflows.
      fifth = std::clamp(fifth, kFreeV2Lo, kFreeV2Hi);
      addNote(pedal_section.notes, bar_start, kTicksPerBeat * 2, pitch);
      addNote(pedal_section.notes, bar_start + kTicksPerBeat * 2, kTicksPerBeat * 2, fifth);
      registry.record(bar_start, /*voice=*/2, pitch, kTicksPerBeat * 2);
      registry.record(bar_start + kTicksPerBeat * 2, /*voice=*/2, fifth, kTicksPerBeat * 2);
      last_pedal_pitch = fifth;
      pedal_run = 1;
      pedal_held.push_back({pitch, kTicksPerBeat * 2});
    } else {
      addNote(pedal_section.notes, bar_start, kTicksPerBar, pitch);
      registry.record(bar_start, /*voice=*/2, pitch, kTicksPerBar);
      pedal_held.push_back({pitch, kTicksPerBar});
    }
  }
  if (!pedal_section.notes.empty()) {
    pedal_section.start_tick = barTick(pedal_first);
    pedal_section.end_tick = barTick(pedal_last + 1);
    out.material.figuration_sections.push_back(std::move(pedal_section));
    pushSpan(asm_ctx, 2, pedal_first, pedal_last, VoiceIntent::FigurationCarrier);
  }

  // V1 punctuation section: contiguous over every punctuating bar. A bar-head
  // chord-tone strike (quarter note, or a half note when homophonic) in the V1
  // band, consonant and parallel-free against both V0 and the V2 pedal. The
  // downbeat=true selection guarantees a genuine chord tone so the validator's
  // figuration_harmonic_consistency rule is satisfied (these notes land on bar
  // downbeats and carry FigurationCommitted).
  FigurationSection punct_section;
  punct_section.voice = 1;
  int punct_first = -1;
  int punct_last = -1;
  int v1_prev = -1;
  for (int bar = 0; bar < free_bars; ++bar) {
    const FreeLayerPlan& want = layout[static_cast<std::size_t>(bar)];
    if (!want.punctuate && !want.homophonic && !want.fermata)
      continue;
    if (punct_first < 0)
      punct_first = bar;
    punct_last = bar;
    const ChordSpec& chord = plan[static_cast<std::size_t>(bar)];
    const Tick bar_start = barTick(bar);
    const int centre = (kFreeV1Lo + kFreeV1Hi) / 2;
    registry.concurrentThemePitches(bar_start, /*voice=*/1, theme_pitches);
    registry.concurrentMotions(bar_start - kTicksPerBar, bar_start, /*voice=*/1,
                               /*num_voices=*/3, motions);
    const int pitch = consonantChordTone(chord, /*voice=*/1, kFreeV1Lo, kFreeV1Hi, centre,
                                         theme_pitches, v1_prev, motions, mode, /*downbeat=*/true);
    v1_prev = pitch;
    // A fermata bar strikes one whole note (the metered breath), a homophonic
    // bar a half note (sounding two beats so the V1+V2 chord articulates
    // together), and a plain head punctuation uses the caller-supplied duration
    // (longer to lift V1 occupancy, shorter for a lighter accompaniment).
    // Either way the bar's tail is left to the V0 figuration alone (a breathing,
    // non-saturated accompaniment).
    const Tick dur =
        want.fermata ? kTicksPerBar : (want.homophonic ? kTicksPerBeat * 2 : v1_punct_dur);
    addNote(punct_section.notes, bar_start, dur, pitch);
    registry.record(bar_start, /*voice=*/1, pitch, dur);
  }
  if (!punct_section.notes.empty()) {
    punct_section.start_tick = barTick(punct_first);
    punct_section.end_tick = barTick(punct_last + 1);
    out.material.figuration_sections.push_back(std::move(punct_section));
    pushSpan(asm_ctx, 1, punct_first, punct_last, VoiceIntent::FigurationCarrier);
  }
}

/// @brief Append one V0 declamatory chord-block bar, alternating inversions.
///
/// Two half-note top triad tones per bar (the palette chord block's top line).
/// When this bar's top tone would repeat the previous block bar's, the block is
/// rebuilt one inversion lower (top ceiling just below the repeated tone), so
/// consecutive block bars never stall on one pitch -- adjacent chords sharing a
/// triad tone (I and V share the dominant) would otherwise chain identical
/// half notes past the repeated-run cap.
void appendChordBlockBarAlternating(std::vector<MaterialNote>& dst, int bar, const ChordSpec& chord,
                                    Mode mode, int top_hi, int& prev_top) {
  std::vector<std::vector<MaterialNote>> block(1);
  appendChordBlockBar(block, bar, chord, mode, top_hi, kTicksPerBeat * 2);
  if (!block[0].empty() && static_cast<int>(block[0].front().pitch) == prev_top) {
    block.assign(1, {});
    appendChordBlockBar(block, bar, chord, mode, prev_top - 1, kTicksPerBeat * 2);
  }
  if (!block[0].empty())
    prev_top = block[0].front().pitch;
  for (const auto& note : block[0])
    dst.push_back(note);
}

/// @brief Append one BWV565-style leading-tone diminished-seventh sweep bar (V0).
///
/// The leading-tone diminished seventh of the (minor) tonic -- the four pitch
/// classes {leading_tone, +3, +6, +9} mod 12 (e.g. B-D-F-Ab in A minor) --
/// rolled as a descend-then-ascend arpeggio over the bar. Walking between the
/// four tones is chord-tone arithmetic (not scale-degree walking), so every
/// emitted pitch is one of the four dim7 classes by construction. Octave
/// placement is explicit and confined to the V0 band [band_lo, band_hi]: the
/// roll therefore sounds above the concurrent V1 head punctuation and V2 pedal
/// exactly like a running wave bar (voice_crossing is the only inter-voice rule
/// that fires over this all-Material texture), and the dim7 rolls over the
/// sustained pedal like the model piece.
///
/// @param dst V0 destination note vector.
/// @param bar Absolute bar index (4/4 bar grid).
/// @param tonic_pc Internal minor tonic pitch class; the dim7 is built on its
///        leading tone (tonic_pc + 11).
/// @param band_lo V0 register band floor.
/// @param band_hi V0 register band ceiling.
/// @param triplet When true, six sixteenth-triplet notes per beat (80 ticks
///        each); otherwise four sixteenths per beat -- matching the neighbouring
///        wave bars' subdivision (the toccata's drive tightens to triplets only
///        in the final two running bars).
void appendDim7SweepBar(std::vector<MaterialNote>& dst, int bar, int tonic_pc, int band_lo,
                        int band_hi, bool triplet) {
  const int leading = (((tonic_pc + 11) % 12) + 12) % 12;
  auto is_dim7 = [&](int midi) {
    const int rel = ((((midi % 12) + 12) % 12) - leading + 12) % 12;
    return rel == 0 || rel == 3 || rel == 6 || rel == 9;
  };
  // Ascending ladder of dim7 tones inside the V0 band (the four classes recur
  // every three semitones, so a >= 12-semitone band always holds at least four).
  std::vector<int> ladder;
  for (int midi = band_lo; midi <= band_hi; ++midi) {
    if (is_dim7(midi))
      ladder.push_back(midi);
  }
  if (ladder.empty())
    ladder.push_back(std::clamp(band_lo + leading, band_lo, band_hi));  // defensive.
  // Triangle roll: down from the band top through every dim7 tone, then back up
  // to the top (the recurring BWV565 dim7 arpeggio flourish).
  std::vector<int> roll;
  roll.reserve(ladder.size() * 2);
  for (int idx = static_cast<int>(ladder.size()) - 1; idx >= 0; --idx)
    roll.push_back(ladder[static_cast<std::size_t>(idx)]);
  for (std::size_t idx = 1; idx < ladder.size(); ++idx)
    roll.push_back(ladder[idx]);

  const int notes_per_beat = triplet ? 6 : 4;
  const Tick step = triplet ? (kTicksPerBeat / 6) : kSixteenth;
  std::size_t slot = 0;
  for (int beat = 0; beat < 4; ++beat) {
    for (int sub = 0; sub < notes_per_beat; ++sub) {
      const Tick tick =
          barTick(bar) + static_cast<Tick>(beat) * kTicksPerBeat + static_cast<Tick>(sub) * step;
      addNote(dst, tick, step, roll[slot % roll.size()]);
      ++slot;
    }
  }
}

/// @brief Resolve the active arc point for an absolute bar within the piece.
///
/// The arc spans the whole piece (one cycle per snap window). A bar maps to its
/// cycle by integer division; the index is clamped to the cycle count.
ArcPoint arcForBar(const ResolvedRequest& req, int bar) {
  const int cycle = bar / kSubjectBars;  // snap_bars == 4 for both forms.
  const std::size_t idx =
      static_cast<std::size_t>(std::clamp<int>(cycle, 0, static_cast<int>(req.cycle_count) - 1));
  return req.arc(idx);
}

// ---------------------------------------------------------------------------
// Split policy. The free opening is ~3/8 of the piece (rounded to a multiple of
// 4, never below 8 bars); the rest is the fugue (never below 8 bars so a full
// 3-entry exposition always fits). N = 16 -> 8 + 8.
// ---------------------------------------------------------------------------
struct Split {
  int free_bars;
  int fugue_bars;
};

Split splitBars(int total) {
  int free_bars = ((total * 3 / 8 + 2) / 4) * 4;  // round (3N/8) to nearest 4.
  free_bars = std::max(8, free_bars);
  if (total - free_bars < 8) {
    free_bars = total - 8;
    free_bars = std::max(8, (free_bars / 4) * 4);
  }
  return {free_bars, total - free_bars};
}

// Give the free-section close and the answer entry a real tonal function.
// The final free bar is a half cadence on V.  Two bars before the answer's
// dominant-key pivot, V/V resolves to V; the answer then inhabits that shared
// V chord for four bars before the third, home-key subject entry restores I.
void prepareSectionalFugueHarmony(std::vector<ChordSpec>& plan, int free_bars, Mode mode) {
  if (free_bars <= 0 || static_cast<std::size_t>(free_bars) > plan.size())
    return;
  plan[static_cast<std::size_t>(free_bars - 1)] = ChordSpec{7, false};
  const int answer_bar = free_bars + kSubjectBars;
  if (answer_bar >= 2 && static_cast<std::size_t>(answer_bar) < plan.size()) {
    plan[static_cast<std::size_t>(answer_bar - 2)] = ChordSpec{2, false};  // V/V.
    plan[static_cast<std::size_t>(answer_bar - 1)] =
        ChordSpec{7, mode == Mode::Minor};  // V (major) / v (minor target).
    // In major, V itself is a shared C/G chord. In harmonic minor, v contains
    // the target key's Bb but the home scale's B-natural, so use home i =
    // target iv as the actual common chord, then establish local v one bar
    // later under the answer.
    plan[static_cast<std::size_t>(answer_bar)] =
        mode == Mode::Minor ? ChordSpec{0, true} : ChordSpec{7, false};
    if (mode == Mode::Minor && static_cast<std::size_t>(answer_bar + 1) < plan.size())
      plan[static_cast<std::size_t>(answer_bar + 1)] = ChordSpec{7, true};
  }
}

void annotateSectionalPivotChords(HarnessFixture& out, int free_bars) {
  const int answer_bar = free_bars + kSubjectBars;
  if (answer_bar < 2 || static_cast<std::size_t>(answer_bar) >= out.harmony.chords.size())
    return;
  ChordEvent& secondary = out.harmony.chords[static_cast<std::size_t>(answer_bar - 2)];
  secondary.degree = RomanNumeral::V;
  secondary.function = HarmonicFunction::D;
  secondary.has_degree = true;
  secondary.has_secondary_of = true;
  secondary.secondary_of = RomanNumeral::V;
  ChordEvent& target = out.harmony.chords[static_cast<std::size_t>(answer_bar - 1)];
  target.degree = RomanNumeral::V;
  target.function = HarmonicFunction::D;
  target.has_degree = true;
}

// ---------------------------------------------------------------------------
// Key itinerary.
//
// A planned key area only becomes audible if the notes are spelled in it. Every
// pitch these forms emit comes from a palette helper written on a fixed C
// collection, so material that is merely degree-shifted stays inside the home
// collection however far the plan travels. Two passes carry the plan to the ear:
// the per-bar chord plan is restated in the key sounding at each bar, which puts
// the local spelling into every chord-derived tone (anchors, pedal, punctuation,
// chord blocks) without any selector knowing a modulation exists; and the tones
// between those anchors -- the scalar fills the palette walks in C -- are bent
// into the local collection afterwards.
// ---------------------------------------------------------------------------

// The bar the free section leaves the home key at: one four-bar block, long
// enough to state the tonic before the first departure.
constexpr int kFreeExcursionBar = 4;

/// @brief Stamp the key areas both sectional forms travel through.
///
/// One destination, reached twice. The free section leaves home once its opening
/// block has established the tonic and comes back for its closing half cadence,
/// so that cadence is heard as V of the home key rather than as an arrival. The
/// fugue tail's answer then inhabits the same dominant until the development
/// restores home for the final cadence. The free-section boundaries are phrase
/// modulations -- the section break itself carries them -- while the answer's is
/// the pivot the V/V -> V approach below it already prepares.
///
/// @param harmony Plan receiving the home key and the modulation boundaries.
/// @param total_bars Piece length in bars.
/// @param free_bars Length of the free opening section in bars.
/// @param mode Diatonic mode of the piece.
void planSectionalModulations(HarmonicPlan& harmony, int total_bars, int free_bars, Mode mode) {
  harmony.tonic_pc = 0;
  harmony.is_minor = (mode == Mode::Minor);
  const bool minor = harmony.is_minor;
  constexpr std::uint8_t kDominantPc = 7;

  const int free_return_bar = free_bars - 1;  // the half-cadence bar is home V.
  if (free_return_bar > kFreeExcursionBar) {
    harmony.modulations.push_back(
        {barTick(kFreeExcursionBar), 0, kDominantPc, minor, minor, ModulationType::Phrase});
    harmony.modulations.push_back(
        {barTick(free_return_bar), kDominantPc, 0, minor, minor, ModulationType::Phrase});
  }

  const int answer_bar = free_bars + kSubjectBars;
  if (answer_bar < 2 || answer_bar >= total_bars)
    return;
  harmony.modulations.push_back(
      {barTick(answer_bar), 0, kDominantPc, minor, minor, ModulationType::Pivot});
  // A short sectional form may reach its reserved two-bar home cadence before
  // the full four-bar answer phrase has elapsed.  Restore the home context at
  // that cadence boundary instead of leaving the final C cadence interpreted
  // in the temporary dominant key.
  const int final_cadence_bar = total_bars - 2;
  const int return_bar = std::min(answer_bar + kSubjectBars, final_cadence_bar);
  if (return_bar > answer_bar && return_bar < total_bars) {
    harmony.modulations.push_back(
        {barTick(return_bar), kDominantPc, 0, minor, minor, ModulationType::Phrase});
  }
}

/// @brief True when the key sounding at `tick` is not the piece's home key.
bool isForeignKeyAt(const HarmonicPlan& harmony, Tick tick) {
  const KeyContext local = localKeyAt(harmony, tick);
  return local.tonic_pc != static_cast<std::uint8_t>(harmony.tonic_pc % 12) ||
         local.is_minor != harmony.is_minor;
}

/// @brief True when a pitch belongs to the bar's chord.
bool isChordTone(int pitch, const ChordSpec& chord) {
  const int third_iv = chord.minor ? 3 : 4;
  const int offset = ((pitch - static_cast<int>(chord.root_pc)) % 12 + 12) % 12;
  return offset == 0 || offset == third_iv || offset == 7 || (chord.seventh && offset == 10);
}

/// @brief Restate every bar of the chord plan in the key sounding at it.
///
/// Degree-preserving, so a home progression keeps its function where it lands (V
/// stays V) and picks up whatever accidentals the destination spells it with.
/// Bars in the home key are left untouched, so a piece with no modulation comes
/// out of this exactly as it went in.
///
/// @param plan Per-bar chord plan, modified in place.
/// @param harmony Plan supplying the home key and the modulation boundaries.
void retuneChordPlan(std::vector<ChordSpec>& plan, const HarmonicPlan& harmony) {
  const KeyContext home{static_cast<std::uint8_t>(harmony.tonic_pc % 12), harmony.is_minor};
  for (std::size_t bar = 0; bar < plan.size(); ++bar) {
    const Tick tick = barTick(static_cast<int>(bar));
    if (!isForeignKeyAt(harmony, tick))
      continue;
    const KeyContext local = localKeyAt(harmony, tick);
    plan[bar].root_pc = static_cast<std::uint8_t>(
        transposeIntoKey(static_cast<int>(plan[bar].root_pc), home, local) % 12);
  }
}

// How badly a candidate tone reads against a statement it sounds with. Ranked
// rather than pooled: the four faults are not interchangeable, and a guard that
// unioned them would refuse a bend reading as the battuta the reference corpus
// writes regularly in order to keep a tone that is already a true parallel. The
// order follows the cost the corpus puts on each class -- the battuta is the
// mildest of the motions, the contrary arrival at a perfect class sits just
// under the true parallel.
//
// The mildest tier of all is not a motion at all: it is a tone that newly SITS
// on a perfect class, reached obliquely while the statement sustains. Nothing is
// wrong with that vertical on its own, which is why no motion rule names it, but
// it is the material every one of the motions above is made of -- the line
// leaves that perfect class at the next onset, and if the statement moves with
// it the pair is a true parallel that this restatement created and no later pass
// can see.
constexpr int kBendClean = 0;
constexpr int kBendPerfectArrival = 1;
constexpr int kBendBattuta = 2;
constexpr int kBendHidden = 3;
constexpr int kBendAnti = 4;
constexpr int kBendParallel = 5;

/// @brief Rank the motion from this line's previous tone into `pitch` against
///        every settled line sounding across the same pair of onsets.
///
/// Every fault ranked here is a motion rule, so each settled line is read at
/// BOTH onsets. Which perfect interval a tone sits on says nothing on its own: a
/// static reading cannot separate a parallel fifth from an oblique arrival at
/// the same fifth, and so cannot keep the one out while letting the other
/// through.
///
/// @param settled_lines One monophonic settled line per entry.
/// @param prev_tick Onset of this line's previous note.
/// @param prev_pitch This line's previous pitch (-1 when it has none).
/// @param tick Onset of the note being ranked.
/// @param pitch Candidate pitch for this line.
/// @return The worst fault rank over the lines sounding at both onsets.
int settledMotionRank(const std::vector<std::vector<MaterialNote>>& settled_lines, Tick prev_tick,
                      int prev_pitch, Tick tick, int pitch) {
  if (prev_pitch < 0)
    return kBendClean;
  int worst = kBendClean;
  for (const std::vector<MaterialNote>& line : settled_lines) {
    const int theme_prev = soundingMaterialPitch(line, prev_tick);
    const int theme_curr = soundingMaterialPitch(line, tick);
    if (theme_prev < 0 || theme_curr < 0)
      continue;
    // The classifier reads the register order from its argument positions, so
    // the pair is normalised by what actually sounds on top at the arrival.
    const bool line_on_top = pitch >= theme_curr;
    const int upper_prev = line_on_top ? prev_pitch : theme_prev;
    const int upper_curr = line_on_top ? pitch : theme_curr;
    const int lower_prev = line_on_top ? theme_prev : prev_pitch;
    const int lower_curr = line_on_top ? theme_curr : pitch;
    const PerfectMotionKind kind =
        classifyPerfectMotion(upper_prev, upper_curr, lower_prev, lower_curr);
    if (kind == PerfectMotionKind::ParallelFifth || kind == PerfectMotionKind::ParallelOctave) {
      worst = std::max(worst, kBendParallel);
    } else if (kind == PerfectMotionKind::HiddenFifth || kind == PerfectMotionKind::HiddenOctave) {
      worst = std::max(worst, kBendHidden);
    } else if (isAntiParallelPerfectMotion(upper_prev, upper_curr, lower_prev, lower_curr)) {
      worst = std::max(worst, kBendAnti);
    } else if (isBattutaMotion(upper_prev, upper_curr, lower_prev, lower_curr)) {
      worst = std::max(worst, kBendBattuta);
    }
    const int arrival_ic = ((pitch - theme_curr) % 12 + 12) % 12;
    if (arrival_ic == 0 || arrival_ic == 7) {
      worst = std::max(worst, kBendPerfectArrival);
    }
  }
  return worst;
}

/// @brief Bend the notes from `first` onward into the key sounding under them.
///
/// The bar's own chord tones are left where they are: the harmony's spelling
/// outranks the collection, so a local dominant's raised third survives a bend
/// into a natural-minor collection that does not contain it, and every downbeat
/// anchor stays the chord tone the figuration rule requires. Notes in the home
/// key are untouched as well, which preserves the raised leading tones the
/// minor-key cadence formulas write.
///
/// A bend is refused only when it reads WORSE against the settled lines than the
/// tone it replaces; an equal or better motion is taken. Those lines are
/// verbatim material chosen before this one was restated, so nothing downstream
/// can answer for the pair, and this restatement runs after the
/// parallel-avoidance machinery that chose them. Refusing outright on any
/// perfect motion would be the wrong shape: against a statement that already
/// pins the vertical the unbent tone is regularly no cleaner, and a veto there
/// keeps the fault while also keeping the cross relation the bend was called to
/// remove.
///
/// @param notes Line to restate, modified in place.
/// @param first Index of the first note to consider.
/// @param harmony Plan supplying the home key and the modulation boundaries.
/// @param plan Per-bar chord plan, already restated in the local keys.
/// @param settled_lines Lines already settled against this one, one per entry.
void bendIntoLocalKeys(std::vector<MaterialNote>& notes, std::size_t first,
                       const HarmonicPlan& harmony, const std::vector<ChordSpec>& plan,
                       const std::vector<std::vector<MaterialNote>>& settled_lines = {}) {
  for (std::size_t idx = first; idx < notes.size(); ++idx) {
    MaterialNote& note = notes[idx];
    const std::size_t bar = static_cast<std::size_t>(note.start_tick / kTicksPerBar);
    if (bar >= plan.size() || !isForeignKeyAt(harmony, note.start_tick))
      continue;
    const int pitch = static_cast<int>(note.pitch);
    if (isChordTone(pitch, plan[bar]))
      continue;
    const KeyContext local = localKeyAt(harmony, note.start_tick);
    const int bent = std::clamp(bendIntoKey(pitch, local), 0, 127);
    if (bent == pitch)
      continue;
    // The previous tone is the one this line will actually sound, bent and all,
    // because the restatement walks the line in order.
    const Tick prev_tick = idx > 0 ? notes[idx - 1].start_tick : 0;
    const int prev_pitch = idx > 0 ? static_cast<int>(notes[idx - 1].pitch) : -1;
    if (settledMotionRank(settled_lines, prev_tick, prev_pitch, note.start_tick, bent) >
        settledMotionRank(settled_lines, prev_tick, prev_pitch, note.start_tick, pitch)) {
      continue;
    }
    note.pitch = static_cast<std::uint8_t>(bent);
  }
}

/// @brief Collect the theme statements, one monophonic line per entry.
///
/// Kept separate rather than flattened: the motion rank reads each line at two
/// onsets, and a merged list would pair one line's departure with another's
/// arrival. These are the lines every restatement starts out ranked against.
std::vector<std::vector<MaterialNote>> gatherThemeLines(const Material& material) {
  std::vector<std::vector<MaterialNote>> lines;
  lines.push_back(material.subject);
  lines.push_back(material.answer);
  lines.push_back(material.tonal_answer);
  for (const StrettoDecl& stretto : material.stretto_entries)
    lines.push_back(stretto.follower_notes);
  return lines;
}

/// @brief Bend every accompaniment line of the fixture into its local key.
///
/// The thematic vectors are deliberately excluded. A real answer is already the
/// degree-preserving restatement of the subject in the dominant -- transposing
/// down a fourth and transposing into the dominant collection are the same map --
/// and the subject, its re-entry and the stretto pair all sound in the home key,
/// where a bend is a no-op anyway. Bending them would additionally break the
/// constant transposition the imitation and stretto declarations are checked
/// against.
///
/// @param out Fixture whose accompaniment material is restated.
/// @param plan Per-bar chord plan, already restated in the local keys.
void bendAccompanimentIntoLocalKeys(HarnessFixture& out, const std::vector<ChordSpec>& plan) {
  // The lines are restated in a fixed order and each joins the settled set once
  // it is done, so a line reaches the rank carrying every line settled before
  // it -- the statements first, then the accompaniment top down. The dependency
  // is one-directional by construction: a line cannot see one restated after it,
  // so half of each accompaniment pair is read rather than none.
  std::vector<std::vector<MaterialNote>> settled = gatherThemeLines(out.material);
  const auto settle = [&](std::vector<MaterialNote>& line) {
    bendIntoLocalKeys(line, 0, out.harmony, plan, settled);
    settled.push_back(line);
  };
  // The countersubject meets the statements directly, so it is restated first.
  settle(out.material.countersubject);
  for (VoiceId voice = 0; voice < kTailVoices; ++voice) {
    for (FigurationSection& section : out.material.figuration_sections) {
      if (section.voice == voice)
        settle(section.notes);
    }
    for (CodaDecl& coda : out.material.coda_extensions) {
      if (coda.voice == voice)
        settle(coda.notes);
    }
  }
}

// ---------------------------------------------------------------------------
// appendFugueTail: a self-contained 3-voice fugue from `first_bar` spanning
// `bars` bars (>= 8). Built inline with the PreludeAndFugue idiom (no shared fugue
// assembly). Layout (relative to first_bar):
//   exposition: V0 subject (0-3), V1 answer -P4 (4-7), V2 re-entry -P8 (8-11)
//               when bars >= 12, else a compressed 2-entry exposition.
//   counterline: band-confined figuration in the non-thematic voices, never
//                before that voice's own entry (see `voice_entry_bar`).
//   stretto: two overlapping subject statements <= 1 bar apart near the end
//            (only when bars >= 12); aligned to the climax cycle when possible.
//   cadence: a 2-bar V0 Picardy close on the home tonic.
// All material is Material; band confinement keeps V0 >= V1 >= V2.
// ---------------------------------------------------------------------------
void appendFugueTail(SectionalAssembly& asm_ctx, int first_bar, int bars,
                     const std::vector<ChordSpec>& plan, const ResolvedRequest& req,
                     bool open_development_texture) {
  HarnessFixture& out = *asm_ctx.out;
  const Mode mode = req.mode;
  const int fig_offset = static_cast<int>(req.seed % 4);

  const bool minor_mode = (mode == Mode::Minor);
  const std::uint8_t slot = subjectIndexFor(req.character, minor_mode, req.seed);
  const std::array<std::uint8_t, 16>& subj_pat =
      minor_mode ? kSubjectCatalogMinor[slot] : kSubjectCatalogMajor[slot];
  const std::array<Tick, 16>& subj_rhythm =
      minor_mode ? kSubjectCatalogMinorRhythms[slot] : kSubjectCatalogMajorRhythms[slot];

  // The cadence reserves the final 2 bars; nothing else extends into them.
  const int cadence_start = first_bar + bars - 2;  // first of the 2 cadence bars.

  // Every placed tail note (theme statements AND figuration / countersubject
  // accompaniment) is recorded into this registry as it is stamped, so a line
  // built later in the deterministic voice order reads what every earlier voice
  // sounds at a given tick and selects an anchor that is consonant with the
  // concurrent theme and parallel-free against every earlier voice (the shared
  // parallel-avoidance machinery, identical to the fugue family's wiring).
  ThemeToneRegistry& registry = asm_ctx.theme_tones;

  // Entries are placed at 4-bar offsets (PreludeAndFugue idiom): V0 subject at the
  // boundary, V1 answer four bars later, V2 re-entry eight bars later. The full
  // three-entry exposition is used only when the re-entry (bars 8-11) fits
  // strictly before the reserved cadence bars; otherwise a compressed two-entry
  // exposition is used.
  const bool full_exposition = (first_bar + 11) < cadence_start;

  // A voice's first sound in the exposition is its own entry. Until a voice has
  // stated the subject or the answer it is silent, so the texture accumulates
  // one voice per entry window the way a fugue exposition is heard: V0 alone,
  // then V1 arriving with the answer, then V2 with the third entry. Past its
  // entry window a voice has spoken and is free to accompany, which is why the
  // rule reads as a per-voice earliest bar rather than a section-wide gate --
  // the same bound also releases V2 at the end of a compressed two-entry
  // exposition, where it has no thematic statement to wait for.
  const std::array<int, kTailVoices> voice_entry_bar = {first_bar + 0, first_bar + 4,
                                                        first_bar + 8};

  // --- Clip an accompaniment span forward to the voice's own entry. ---
  // Returns false when the span lies wholly before the entry and must be
  // dropped: a voice yet to speak contributes nothing, not a thinner line.
  auto clip_to_entry = [&](VoiceId voice, int& first, int last) {
    first = std::max(first, voice_entry_bar[voice]);
    return first <= last;
  };

  // --- Stamp a 16-note subject statement transposed by `semis` into `voice`. ---
  // `rhythm` carries the values the statement treads; every statement but the
  // stretto leader uses the catalog row unchanged.
  auto stamp_subject = [&](int base_bar, int semis, int theme_voice,
                           const std::array<Tick, 16>& rhythm) {
    Tick cursor = barTick(base_bar);
    for (int note = 0; note < kSubjectNotes; ++note) {
      const int pitch = static_cast<int>(subj_pat[static_cast<std::size_t>(note)]) + semis;
      const Tick dur = rhythm[static_cast<std::size_t>(note)];
      addNote(out.material.subject, cursor, dur, pitch);
      registry.record(cursor, static_cast<VoiceId>(theme_voice), pitch, dur);
      cursor += dur;
    }
  };

  // --- Add one band-confined, parallel-free figuration counterline span. ---
  // Each beat onset anchors on a consonant, parallel-free chord tone selected
  // against every earlier voice already recorded in the registry; the notes
  // between anchors walk stepwise so the line stays conjunct, and every emitted
  // note is recorded so a later voice avoids a parallel against it.
  auto add_counterline = [&](VoiceId voice, int first, int last, int notes_per_beat,
                             int alternate_notes_per_beat = 0) {
    if (!clip_to_entry(voice, first, last)) {
      return;
    }
    FigurationSection section;
    section.voice = voice;
    section.start_tick = barTick(first);
    section.end_tick = barTick(last + 1);
    int prev_anchor = 0;
    for (int bar = first; bar <= last; ++bar) {
      const int density = alternate_notes_per_beat > 0 && (bar - first) % 2 == 1
                              ? alternate_notes_per_beat
                              : notes_per_beat;
      appendFigurationWaveBar(registry, section, bar, voice, plan[static_cast<std::size_t>(bar)],
                              mode, density, fig_offset, prev_anchor, kBandLo[voice],
                              kBandHi[voice], kTailVoices);
    }
    coalesceConsecutiveSamePitch(section.notes);
    out.material.figuration_sections.push_back(section);
    pushSpan(asm_ctx, voice, first, last, VoiceIntent::FigurationCarrier);
  };

  // --- Add one sustained chord-tone support note per bar. ---
  // A consonant, parallel-free chord tone at each bar head, held for the
  // caller-selected pulse (a whole bar by default). FigurationCarrier replays
  // it verbatim. Used to thicken bars that would otherwise rest a voice without
  // adding a second running figuration line.
  auto add_sustained_support = [&](VoiceId voice, int first, int last,
                                   Tick pulse_duration = kTicksPerBar) {
    if (!clip_to_entry(voice, first, last)) {
      return;
    }
    FigurationSection section;
    section.voice = voice;
    section.start_tick = barTick(first);
    section.end_tick = barTick(last + 1);
    section.is_pedal_prep = true;  // exempt the held tone from the downbeat check.
    std::vector<int> theme_pitches;
    std::vector<ConcurrentMotion> motions;
    // Section seam: this span has no threaded previous tone, but the voice may
    // have sounded right up to its first bar in an earlier span (a theme entry
    // or an earlier support run). Left at -1 every parallel predicate below
    // short-circuits, so the one onset where this line arrives against voices
    // already in motion is the one onset it cannot refuse a perfect approach at.
    int line_prev = registry.soundingPitchInVoice(voice, barTick(first) - kSixteenth);
    const int centre = (kBandLo[voice] + kBandHi[voice]) / 2;
    // Ranked rather than pooled, and read at the grain the other lines move at.
    // This support tone is the one note per bar the lowest voice contributes,
    // so the running lines above it attack many times before it moves again:
    // sampling them a whole bar back compares a motion nobody hears, while the
    // union-onset reading (and the ear) pairs them one sixteenth before the bar
    // head. And with the tone constrained to a chord tone of the bar, inside its
    // band, below both running voices and consonant with every sounding theme
    // tone, the admissible set is small enough that a candidate free of every
    // perfect approach frequently does not exist -- so the true parallel and the
    // hidden perfect have to sit on separate rungs or the design tone stands.
    constexpr int kSupportClean = 0;
    constexpr int kSupportHidden = 1;
    constexpr int kSupportParallel = 2;
    const auto support_fault_rank = [&](int cand) {
      int worst = kSupportClean;
      for (const ConcurrentMotion& motion : motions) {
        if (motion.prev < 0 || motion.curr < 0)
          continue;
        if (formsStrictPerfectParallel(line_prev, cand, motion.prev, motion.curr))
          return kSupportParallel;
        if (formsPerfectParallel(line_prev, cand, motion.prev, motion.curr))
          worst = kSupportHidden;
      }
      return worst;
    };
    // Every beat this tone is still sounding at, read from the voices already
    // placed above it. It is struck once and holds while they attack three or
    // more times, so its onset is the smallest part of what it supports: the
    // interval that decides whether the vertical is a second inversion almost
    // never falls on the beat the bass is struck.
    std::vector<int> window_pitches;
    std::vector<int> beat_pitches;
    for (int bar = first; bar <= last; ++bar) {
      const Tick bar_start = barTick(bar);
      registry.concurrentThemePitches(bar_start, voice, theme_pitches);
      registry.concurrentMotions(bar_start - kSixteenth, bar_start, voice, kTailVoices, motions);
      window_pitches.clear();
      for (Tick beat = kTicksPerBeat; beat < pulse_duration; beat += kTicksPerBeat) {
        registry.concurrentThemePitches(bar_start + beat, voice, beat_pitches);
        window_pitches.insert(window_pitches.end(), beat_pitches.begin(), beat_pitches.end());
      }
      int pitch =
          consonantChordTone(plan[static_cast<std::size_t>(bar)], voice, kBandLo[voice],
                             kBandHi[voice], centre, theme_pitches, line_prev, motions, mode,
                             /*downbeat=*/true, window_pitches,
                             /*parallel_free_over_consonant=*/false, /*sustained_bass=*/true);
      const int design_rank = support_fault_rank(pitch);
      if (design_rank != kSupportClean && line_prev >= 0) {
        // The concurrent voices are all above this one, so a substitute has to
        // stay under the lowest of them; that ordering is what the selector
        // above was holding, and a displacement blind to it would trade a
        // parallel for a crossed voice.
        int order_ceiling = kBandHi[voice];
        for (const ConcurrentMotion& motion : motions) {
          if (motion.curr >= 0 && motion.voice < voice)
            order_ceiling = std::min(order_ceiling, motion.curr);
        }
        const ChordSpec& bar_chord = plan[static_cast<std::size_t>(bar)];
        const int third_semi = bar_chord.minor ? 3 : 4;
        const int triad_pc[3] = {((bar_chord.root_pc % 12) + 12) % 12,
                                 (bar_chord.root_pc + third_semi) % 12,
                                 (bar_chord.root_pc + 7) % 12};
        const int original = pitch;
        for (int accept = kSupportClean; accept < design_rank && pitch == original; ++accept) {
          for (int tone = 0; tone < 3 && pitch == original; ++tone) {
            const int low = kBandLo[voice] + (((triad_pc[tone] - kBandLo[voice]) % 12) + 12) % 12;
            for (int cand = low; cand <= kBandHi[voice]; cand += 12) {
              if (cand == original || cand > order_ceiling)
                continue;
              bool consonant = true;
              for (const int sounding : theme_pitches) {
                if (!isConsonantPair(cand, sounding)) {
                  consonant = false;
                  break;
                }
              }
              if (consonant && support_fault_rank(cand) <= accept) {
                pitch = cand;
                break;
              }
            }
          }
        }
        // Proof of exhaustion, then leave the chord. The scan above offers only
        // triad tones, and against running lines walking through non-chord tones
        // all three pitch classes are regularly blocked at once -- at which point
        // the tone the selector was called to replace stands, true parallel and
        // all. This section already declares itself exempt from the downbeat
        // chord-tone check, so a free diatonic tone is admissible here; it is
        // taken only once no triad tone anywhere in the band would do.
        if (pitch == original && design_rank == kSupportParallel) {
          const int span = std::max(kBandHi[voice] - original, original - kBandLo[voice]);
          for (int dist = 1; dist <= span && pitch == original; ++dist) {
            for (const int sgn : {-1, 1}) {
              const int cand = original + sgn * dist;
              if (cand < kBandLo[voice] || cand > kBandHi[voice] || cand > order_ceiling ||
                  !detail::inScale(cand, mode)) {
                continue;
              }
              bool consonant = true;
              for (const int sounding : theme_pitches) {
                if (!isConsonantPair(cand, sounding)) {
                  consonant = false;
                  break;
                }
              }
              if (consonant && support_fault_rank(cand) == kSupportClean) {
                pitch = cand;
                break;
              }
            }
          }
        }
      }
      addNote(section.notes, bar_start, pulse_duration, pitch);
      registry.record(bar_start, voice, pitch, pulse_duration);
      line_prev = pitch;
    }
    coalesceConsecutiveSamePitch(section.notes);
    out.material.figuration_sections.push_back(section);
    pushSpan(asm_ctx, voice, first, last, VoiceIntent::FigurationCarrier);
  };

  // === EXPOSITION ===========================================================
  const int v0_off = octaveOffsetForBand(subj_pat, 0, 0, kBandLo, kBandHi);
  // Countersubject = a genuine counterline against the entry it accompanies, NOT
  // a parallel-octave doubling of it (the old tail merely octave-shifted the
  // source, which produced the parallel fifths/octaves this rewiring removes).
  // Each note is scored to be (1) consonant with the source note it sounds
  // against, (2) in contrary motion to the source whenever the source moves (so
  // it can never form a parallel fifth/octave), and (3) near the previous
  // counterline pitch, while refusing a long repeated-pitch run (texture gate
  // caps runs at 4). The chosen tone stays inside the voice band so the strict
  // V0 >= V1 >= V2 register order is preserved. Identical scoring to the fugue
  // family's append_countersubject_from. Each note is recorded into the registry
  // so a later voice avoids a parallel against it.
  // The battuta term is on here and off in the fugue family: this tail has no
  // degree-shifted restatement of the countersubject, so the wider ambit that
  // avoiding a battuta costs has nothing downstream that must still octave-fit
  // it.
  auto append_countersubject_from = [&](const std::vector<MaterialNote>& source, int voice,
                                        Tick start, Tick end) {
    appendScoredCountersubject(source, static_cast<VoiceId>(voice), start, end, kBandLo[voice],
                               kBandHi[voice], mode, out.material.countersubject, registry,
                               /*avoid_battuta=*/true);
  };
  stamp_subject(first_bar + 0, v0_off, 0, subj_rhythm);
  out.material.canonical_subject_note_count = kSubjectNotes;
  pushSpan(asm_ctx, 0, first_bar + 0, first_bar + 3, VoiceIntent::SubjectCarrier);
  // The dux is heard alone for its whole statement: no voice accompanies it,
  // because no other voice has entered yet.

  // V1 real answer (subject - P4) in the V1 band, entering one entry-window
  // (4 bars) after the subject. For a short (8-bar) fugue tail the answer is
  // truncated so it never extends into the reserved cadence bars (the validator
  // checks only the answer's first note, so a partial answer stays valid).
  const int answer_off = octaveOffsetForBand(subj_pat, -5, 1, kBandLo, kBandHi);
  const int answer_first = first_bar + 4;
  const int answer_last = std::min(first_bar + 7, cadence_start - 1);
  const Tick answer_end = barTick(answer_last + 1);
  const bool use_tonal_answer = shouldUseTonalAnswer(subj_pat, out.harmony.tonic_pc);
  std::vector<MaterialNote> tonal_answer_seed;
  tonal_answer_seed.reserve(kSubjectNotes);
  Tick answer_cursor = barTick(answer_first);
  for (int note = 0; note < kSubjectNotes && answer_cursor < answer_end; ++note) {
    const Tick dur =
        std::min(subj_rhythm[static_cast<std::size_t>(note)], answer_end - answer_cursor);
    const int pitch = static_cast<int>(subj_pat[static_cast<std::size_t>(note)]) - 5 + answer_off;
    const Tick tick = answer_cursor;
    addNote(out.material.answer, tick, dur, pitch);
    MaterialNote seed_note;
    seed_note.start_tick = tick;
    seed_note.duration = dur;
    seed_note.pitch = static_cast<std::uint8_t>(std::clamp(
        static_cast<int>(subj_pat[static_cast<std::size_t>(note)]) + answer_off, 0, 127));
    tonal_answer_seed.push_back(seed_note);
    if (!use_tonal_answer) {
      registry.record(tick, /*voice=*/1, pitch, dur);
    }
    answer_cursor += subj_rhythm[static_cast<std::size_t>(note)];
  }
  if (use_tonal_answer) {
    out.material.tonal_answer = tonal_answer::deriveTonalAnswer(
        tonal_answer_seed, out.harmony.tonic_pc, barTick(answer_first), 4);
    out.material.use_tonal_answer = true;
    for (const auto& note : out.material.tonal_answer) {
      registry.record(note.start_tick, /*voice=*/1, static_cast<int>(note.pitch), note.duration);
    }
  }
  pushSpan(asm_ctx, 1, answer_first, answer_last, VoiceIntent::AnswerCarrier);
  // V0 countersubject over the answer: fixed recurring counterline instead of
  // free figuration for the comes entry.
  append_countersubject_from(use_tonal_answer ? out.material.tonal_answer : out.material.answer, 0,
                             barTick(answer_first), answer_end);
  pushSpan(asm_ctx, 0, answer_first, answer_last, VoiceIntent::CountersubjectCarrier);
  // The answer bar-group stays two-voiced (comes plus countersubject): the bass
  // register is empty here because V2 has not yet stated the subject, and it
  // fills only when the third entry arrives.

  // Imitation entry declaration (subject leads, answer follows at one entry
  // window). The validator compares the actual first-note pitches, so the
  // declared interval is the real semitone distance between the V0 subject head
  // and the V1 answer head: -P4 plus the band-octave shift difference between
  // the two voices' octave transpositions.
  {
    const auto& selected_answer =
        use_tonal_answer ? out.material.tonal_answer : out.material.answer;
    ImitationEntry entry;
    entry.leader_fragment = MaterialFragment::Subject;
    entry.follower_fragment =
        use_tonal_answer ? MaterialFragment::TonalAnswer : MaterialFragment::Answer;
    entry.leader_voice = 0;
    entry.follower_voice = 1;
    const std::size_t available =
        std::min<std::size_t>(out.material.subject.size(), selected_answer.size());
    while (entry.note_count < available && out.material.subject[entry.note_count].duration ==
                                               selected_answer[entry.note_count].duration) {
      ++entry.note_count;
    }
    entry.distance_ticks =
        selected_answer.front().start_tick - out.material.subject.front().start_tick;
    entry.interval_semis = static_cast<int>(selected_answer.front().pitch) -
                           static_cast<int>(out.material.subject.front().pitch);
    if (use_tonal_answer) {
      entry.tonal_base_interval_semis = answer_off - v0_off - 5;
      entry.has_tonal_base_interval = true;
    }
    out.material.imitation_entries.push_back(entry);
  }

  int next_free_bar = first_bar + 8;  // first bar after the (partial) exposition.
  if (full_exposition) {
    // V2 re-entry (subject - P8) in the V2 band (bars 8-11).
    const int third_off = octaveOffsetForBand(subj_pat, 0, 2, kBandLo, kBandHi);
    stamp_subject(first_bar + 8, third_off, 2, subj_rhythm);
    pushSpan(asm_ctx, 2, first_bar + 8, first_bar + 11, VoiceIntent::SubjectCarrier);
    std::vector<MaterialNote> third_entry_seed;
    Tick third_cursor = barTick(first_bar + 8);
    for (int note = 0; note < kSubjectNotes; ++note) {
      MaterialNote mn;
      mn.start_tick = third_cursor;
      mn.duration = subj_rhythm[static_cast<std::size_t>(note)];
      mn.pitch = static_cast<std::uint8_t>(std::clamp(
          static_cast<int>(subj_pat[static_cast<std::size_t>(note)]) + third_off, 0, 127));
      third_entry_seed.push_back(mn);
      third_cursor += mn.duration;
    }
    append_countersubject_from(third_entry_seed, 1, barTick(first_bar + 8),
                               barTick(first_bar + 12));
    pushSpan(asm_ctx, 1, first_bar + 8, first_bar + 11, VoiceIntent::CountersubjectCarrier);
    // A V0 figuration counterline rides above the V2 re-entry and V1 CS, built
    // last so it reads both lower voices from the registry and stays consonant
    // and parallel-free against them (the FigurationCarrier dispatch matches a
    // section to a span by window AND voice, so distinct-voice sections may
    // share a window).
    add_counterline(0, first_bar + 8, first_bar + 11, 2);
    next_free_bar = first_bar + 12;
  }

  // === DEVELOPMENT + STRETTO ===============================================
  // The tail reserves the final 2 bars for the cadence. Between the exposition
  // and the cadence the texture is figuration, with one stretto (two overlapping
  // subject statements) placed at the climax when the tail is long enough to
  // carry the full exposition.
  //
  // Stretto placement: align the leader to the arc climax cycle when one lands
  // inside the development region; otherwise place it just before the cadence.
  const int v1_alternate_density = open_development_texture ? 4 : 0;
  const Tick bass_pulse = open_development_texture ? 2 * kTicksPerBeat : kTicksPerBar;
  if (full_exposition && cadence_start - next_free_bar >= 4) {
    // Find a climax-cycle downbeat (multiple of 4) inside the development window.
    int leader_bar = -1;
    for (int bar = next_free_bar; bar + 4 <= cadence_start; bar += 4) {
      if (arcForBar(req, bar).is_climax) {
        leader_bar = bar;
        break;
      }
    }
    if (leader_bar < 0) {
      // No climax cycle in range: place the stretto in the last full 4-bar
      // window before the cadence.
      leader_bar = next_free_bar + ((cadence_start - next_free_bar - 4) / 4) * 4;
    }

    // The fill that runs up to the leader is written before the stretto block,
    // not after it, so the block's own lines have a preceding bar to be read
    // against. A line whose previous bar does not exist yet reports no motion at
    // all -- `concurrentMotions` returns a previous pitch of -1 and every
    // parallel predicate short-circuits on it -- so a guard placed after the
    // fill can refuse a parallel that a guard placed before it cannot even see.
    if (leader_bar > next_free_bar) {
      add_counterline(0, next_free_bar, leader_bar - 1, 2);
      add_counterline(1, next_free_bar, leader_bar - 1, 2, v1_alternate_density);
      add_sustained_support(2, next_free_bar, leader_bar - 1, bass_pulse);
    }

    // Leader: a full subject statement in V0 (bars leader_bar .. +3), stated at
    // the exposition's own pitch level -- the band holds one placement of this
    // subject and no other, so the return cannot be answered in a new register.
    // What changes instead is the tread: each pair of equal catalog values is
    // stated long-short (3:1), the dotted treatment a returning subject takes
    // in the reference literature. Every pitch and its order survive, so the
    // ear hears the subject it already knows, played sharper. Only pairs of
    // equal values at least a quarter long are dotted, which keeps the short
    // half of every pair no shorter than an eighth, and the pair's total is
    // unchanged -- so the statement still spans exactly kSubjectBars and the
    // canon window, the follower entry and the span all stand.
    std::array<Tick, 16> leader_rhythm = subj_rhythm;
    for (std::size_t note = 0; note + 1 < leader_rhythm.size(); note += 2) {
      const Tick value = leader_rhythm[note];
      if (value != leader_rhythm[note + 1] || value < kQuarter)
        continue;
      leader_rhythm[note] = value + value / 2;
      leader_rhythm[note + 1] = value - value / 2;
    }
    stamp_subject(leader_bar, v0_off, 0, leader_rhythm);
    pushSpan(asm_ctx, 0, leader_bar, leader_bar + 3, VoiceIntent::SubjectCarrier);

    // Follower: a subject statement in V1 entering after the leader (genuine
    // overlap), truncated at the leader's window end.
    //
    // Both lines are verbatim Material, so the validator skips every vertical
    // rule on the pair and the canon configuration is the only place a parallel
    // between the two theme statements can be answered. Read all four before
    // committing one: a configuration that sounds a true parallel is refused
    // outright, and among the rest the quieter overlap wins with the densest
    // canon breaking ties. Stating the follower an octave below the leader at a
    // one-bar delay -- the configuration this took unconditionally -- makes a
    // parallel octave of every place the subject's own contour repeats.
    struct CanonConfig {
      int delay_bars;
      int extra_semis;
    };
    constexpr std::array<CanonConfig, 4> kCanonConfigs = {{{1, 0}, {2, 0}, {1, 7}, {2, 7}}};
    int follower_off = octaveOffsetForBand(subj_pat, 0, 1, kBandLo, kBandHi);
    int follower_delay = 1;
    {
      int best_index = -1;
      std::array<int, 2> best_key{};
      for (std::size_t idx = 0; idx < kCanonConfigs.size(); ++idx) {
        const CanonConfig& candidate = kCanonConfigs[idx];
        const int candidate_off =
            octaveOffsetForBand(subj_pat, candidate.extra_semis, 1, kBandLo, kBandHi) +
            candidate.extra_semis;
        // The leader treads its dotted values and the follower the plain catalog
        // row, so the two rhythms are passed separately: reading the pair as if
        // both were plain would vet an alignment neither line actually sounds,
        // and the configuration scan exists precisely to refuse the true
        // parallels this overlap would otherwise design in.
        const StrettoOverlapProfile profile =
            strettoOverlapProfile(subj_pat, v0_off, subj_pat, candidate_off, leader_rhythm,
                                  subj_rhythm, candidate.delay_bars, kSubjectBars);
        if (profile.parallel_perfects > 0) {
          continue;
        }
        const std::array<int, 2> key = {profile.sustains_sharp ? 1 : 0, profile.broad_sharp_slots};
        if (best_index < 0 || key < best_key) {
          best_index = static_cast<int>(idx);
          best_key = key;
          follower_off = candidate_off;
          follower_delay = candidate.delay_bars;
        }
      }
    }
    StrettoDecl stretto;
    stretto.leader_voice = 0;
    stretto.follower_voice = 1;
    stretto.leader_entry_tick = barTick(leader_bar);
    stretto.leader_length_ticks = barTick(kSubjectBars);
    stretto.follower_entry_tick = barTick(leader_bar + follower_delay);
    // The validator checks follower_notes[i] == material.subject[i] +
    // interval_semis, where material.subject[0..15] is the V0 exposition subject
    // (transposed by v0_off). The follower lives in the V1 band, so the declared
    // interval is the band-octave difference; the follower pitch is computed from
    // the raw pattern + follower_off, which equals subject[i] + interval_semis.
    stretto.interval_semis = follower_off - v0_off;
    Tick follower_cursor = barTick(leader_bar + follower_delay);
    const Tick follower_end = barTick(leader_bar + kSubjectBars);
    for (int note = 0; note < kSubjectNotes && follower_cursor < follower_end; ++note) {
      const int pitch = static_cast<int>(subj_pat[static_cast<std::size_t>(note)]) + follower_off;
      MaterialNote mn;
      mn.start_tick = follower_cursor;
      mn.duration =
          std::min(subj_rhythm[static_cast<std::size_t>(note)], follower_end - follower_cursor);
      mn.pitch = static_cast<std::uint8_t>(pitch);
      stretto.follower_notes.push_back(mn);
      registry.record(mn.start_tick, /*voice=*/1, pitch, mn.duration);
      follower_cursor += subj_rhythm[static_cast<std::size_t>(note)];
    }
    out.material.stretto_entries.push_back(stretto);
    pushSpan(asm_ctx, 1, leader_bar + follower_delay, leader_bar + 3, VoiceIntent::StrettoCarrier);

    // V2 figuration under the stretto block (band-confined, eighth motion so
    // the bass keeps moving against the overlapped theme statements). The two
    // thematic voices (V0 leader, V1 follower) already sound above it.
    add_counterline(2, leader_bar, leader_bar + 3, 2);

    // The toccata tail opens the development texture: V1 alternates eighth and
    // sixteenth bars while V2 supports only the first half of each bar. The
    // fantasia keeps continuous eighth-note counterlines and whole-bar bass
    // support as part of its denser contrapuntal identity. Both variants keep
    // at least two sounding voices and a three-voice downbeat. Lines are built
    // top-down so each lower line selects consonant, parallel-free tones.
    if (leader_bar + 4 < cadence_start) {
      add_counterline(0, leader_bar + 4, cadence_start - 1, 2);
      add_counterline(1, leader_bar + 4, cadence_start - 1, 2, v1_alternate_density);
      add_sustained_support(2, leader_bar + 4, cadence_start - 1, bass_pulse);
    }
  } else if (cadence_start > next_free_bar) {
    // Short tail: the same three-layer fill up to the cadence.
    add_counterline(0, next_free_bar, cadence_start - 1, 2);
    add_counterline(1, next_free_bar, cadence_start - 1, 2, v1_alternate_density);
    add_sustained_support(2, next_free_bar, cadence_start - 1, bass_pulse);
  }

  // === CADENCE ==============================================================
  // A 2-bar V -> I perfect cadence (the cadence is annotated at the last bar
  // downbeat = the cadence tick; one beat earlier is the approach). The
  // validator's cadence_voice_leading rule (Perfect) requires the upper voice
  // (V0) to resolve the leading tone B -> tonic C and the bass (the lowest
  // sounding voice, V2) to step the dominant root G -> tonic C across the
  // approach -> cadence beats. Material confined to the per-voice bands keeps
  // V0 >= V1 >= V2. Minor + usePicardy(seed) colours the close with a major
  // third E in V0 (documentary Picardy colour; the cadence type stays Perfect
  // because PicardyThird's leading-tone-AND-major-third upper requirement is
  // self-contradictory under the rule).
  int tonic0 = kBandLo[0];
  while (tonic0 % 12 != 0) {
    ++tonic0;  // tonic C inside the V0 band.
  }
  {
    CodaDecl coda;
    coda.voice = 0;
    // The shared cadential landing: an eighth-note approach run rises into a
    // held half-note leading tone B over the penultimate bar's second half
    // (the cadential trill site), then the final bar holds the tonic C as a
    // whole note (the plain resolution). cadence_voice_leading reads the
    // SOUNDING pitch at the approach beat and the cadence downbeat, so the
    // held B still supplies upper_prev = B and the whole-note C upper_now = C.
    const int upper_tonic = tonic0 + 12;
    appendCadentialLanding(coda.notes, barTick(cadence_start), kTicksPerBar, upper_tonic - 1,
                           upper_tonic, mode, kBandLo[0]);
    for (const MaterialNote& note : coda.notes)
      registry.record(note.start_tick, /*voice=*/0, static_cast<int>(note.pitch), note.duration);
    out.material.coda_extensions.push_back(coda);
    pushSpan(asm_ctx, 0, cadence_start, cadence_start + 1, VoiceIntent::CodaCarrier);
  }
  // Cadential bass (V2): an explicit V -> I root motion. The approach beat
  // (cadence_start bar, beat 3) sounds the dominant root G; the cadence downbeat
  // (last bar, beat 0) sounds the tonic root C. The remaining beats hold chord
  // roots so the bass stays band-confined and consonant under V0.
  {
    CodaDecl bass;
    bass.voice = 2;
    int tonic2 = kBandLo[2];
    while (tonic2 % 12 != 0) {
      ++tonic2;  // tonic C inside the V2 band.
    }
    int dominant2 = kBandLo[2];
    while (dominant2 % 12 != 7) {
      ++dominant2;  // dominant G inside the V2 band.
    }
    // Penultimate bar: tonic pedal through the first half, dominant root on the
    // second half (the approach beat samples bass_prev = G). Final bar: the
    // tonic root held as a whole note (the bass joins the held final chord).
    std::array<int, 4> low = {tonic2, tonic2, dominant2, dominant2};
    for (int beat = 0; beat < 4; ++beat) {
      const Tick tick = barTick(cadence_start) + static_cast<Tick>(beat) * kTicksPerBeat;
      const int pitch = std::clamp(low[static_cast<std::size_t>(beat)], kBandLo[2], kBandHi[2]);
      addNote(bass.notes, tick, kQuarter, pitch);
      registry.record(tick, /*voice=*/2, pitch, kQuarter);
    }
    const Tick final_tick = barTick(cadence_start + 1);
    const int final_pitch = std::clamp(tonic2, kBandLo[2], kBandHi[2]);
    addNote(bass.notes, final_tick, kTicksPerBar, final_pitch);
    registry.record(final_tick, /*voice=*/2, final_pitch, kTicksPerBar);
    out.material.coda_extensions.push_back(bass);
    pushSpan(asm_ctx, 2, cadence_start, cadence_start + 1, VoiceIntent::CodaCarrier);
  }

  // V1 inner voice across the 2 cadence bars: held design tones filling the
  // middle register so the final cadence sounds a full three voices instead of
  // the thin V0+V2 close. The penultimate bar holds the dominant G (consonant
  // with the dominant bass and with every beat of the V0 approach run); the
  // final bar holds the third of the closing tonic triad -- E, or Eb in minor
  // unless the seed elects the Picardy lift -- completing the closing triad.
  // is_pedal_prep exempts the held tones from the figuration downbeat
  // chord-tone check.
  {
    int inner_dominant = kBandLo[1];
    while (inner_dominant % 12 != 7) {
      ++inner_dominant;
    }
    // Which G is not a design value -- the pitch class is. Walking up from the
    // band floor in this voice and in the bass lands the two a fifth apart by
    // construction, and the free figuration that runs into them is a fifth apart
    // for the same reason, so the cadence was reached in parallel fifths. This
    // voice is written last, so it is the one end of the pair that can read the
    // other; the register is ranked against it and the lowest G still wins among
    // equals. No register makes the pair clean -- G over C is a fifth in every
    // octave -- so what a rank buys here is the contrary approach in place of the
    // parallel one, which is the milder fault and the one the reference corpus
    // actually writes at a cadence.
    std::vector<ConcurrentMotion> cadence_motions;
    const Tick inner_tick = barTick(cadence_start);
    registry.concurrentMotions(inner_tick - kSixteenth, inner_tick, /*voice=*/1, kTailVoices,
                               cadence_motions);
    const int inner_prev = registry.soundingPitchInVoice(/*voice=*/1, inner_tick - kSixteenth);
    if (inner_prev >= 0) {
      const auto inner_rank = [&](int cand) {
        int worst = 0;
        for (const ConcurrentMotion& motion : cadence_motions) {
          if (motion.prev < 0 || motion.curr < 0) {
            continue;
          }
          if (formsStrictPerfectParallel(inner_prev, cand, motion.prev, motion.curr)) {
            return 3;
          }
          // The contrary arrival ranks ABOVE the hidden perfect: the reference
          // corpus writes it far more sparingly, so scaled by the spread each
          // class occupies there it is the dearer of the two to pay.
          if (formsAntiParallelPerfect(inner_prev, cand, motion.prev, motion.curr)) {
            worst = std::max(worst, 2);
          } else if (formsPerfectParallel(inner_prev, cand, motion.prev, motion.curr)) {
            worst = std::max(worst, 1);
          }
        }
        return worst;
      };
      int best_rank = inner_rank(inner_dominant);
      for (int cand = inner_dominant + 12; cand <= kBandHi[1] && best_rank > 0; cand += 12) {
        const int rank = inner_rank(cand);
        if (rank < best_rank) {
          best_rank = rank;
          inner_dominant = cand;
        }
      }
    }
    const bool picardy_third = mode != Mode::Minor || detail::usePicardy(req.seed);
    int inner_third = kBandLo[1];
    while (inner_third % 12 != (picardy_third ? 4 : 3)) {
      ++inner_third;
    }
    FigurationSection inner;
    inner.voice = 1;
    inner.start_tick = barTick(cadence_start);
    inner.end_tick = barTick(cadence_start + 2);
    inner.is_pedal_prep = true;
    addNote(inner.notes, barTick(cadence_start), kTicksPerBar, inner_dominant);
    registry.record(barTick(cadence_start), /*voice=*/1, inner_dominant, kTicksPerBar);
    addNote(inner.notes, barTick(cadence_start + 1), kTicksPerBar, inner_third);
    registry.record(barTick(cadence_start + 1), /*voice=*/1, inner_third, kTicksPerBar);
    out.material.figuration_sections.push_back(inner);
    pushSpan(asm_ctx, 1, cadence_start, cadence_start + 1, VoiceIntent::FigurationCarrier);
  }

  // Force the final two bars' harmony to V -> I so the annotated perfect cadence
  // is supported by the harmonic plan and the final ChordEvent is the tonic.
  const bool picardy = mode == Mode::Minor && detail::usePicardy(req.seed);
  for (auto& chord : out.harmony.chords) {
    const int bar = static_cast<int>(chord.start_tick / kTicksPerBar);
    if (bar == cadence_start) {
      chord.root_pc = 7;  // V.
      chord.quality = ChordQuality::Major;
    } else if (bar == cadence_start + 1) {
      chord.root_pc = 0;  // I.
      chord.quality = (mode == Mode::Minor && !picardy) ? ChordQuality::Minor : ChordQuality::Major;
      chord.is_picardy = picardy;
    }
  }

  // Final perfect cadence annotation at the last bar downbeat.
  {
    CadenceEvent cadence;
    cadence.tick = barTick(first_bar + bars - 1);
    cadence.type = CadenceType::Perfect;
    out.harmony.cadences.push_back(cadence);
  }
}

}  // namespace

HarnessFixture buildToccataAndFugueForm(const ResolvedRequest& req) {
  HarnessFixture out;
  out.voice_plan.num_voices = 3;
  SpanId next_id = 0;
  SectionalAssembly asm_ctx{&out, &next_id, {}};

  const int total = static_cast<int>(req.bars);
  const Mode mode = req.mode;
  const int harm_idx = static_cast<int>(req.seed % 4);
  const int fig_offset = static_cast<int>(req.seed % 4);
  const Split split = splitBars(total);
  const int free_bars = split.free_bars;

  // Ornament metadata (fixture field only, never a note): the free toccata
  // section closes at its final bar before the fugue enters, and the ornament
  // pass marks that close with a section-cadence trill. The climax window is
  // left unresolved here (callers fall back to their default arc point).
  out.section_cadence_ticks.push_back(barTick(free_bars - 1));

  // Registration terrace (fixture field only, never a note): the organ steps up
  // a stop at the free->fugue boundary, the toccata's one structural energy
  // addition. Organ dynamics move in terraces, not crescendos.
  out.registration_step_ticks.push_back(barTick(free_bars));

  // One per-bar chord plan over the whole piece (free + fugue). The fugue tail
  // reads its slice (bars [free_bars, total)) by absolute bar index. The
  // itinerary is stamped first so the plan can be restated bar by bar in the key
  // that sounds at it; the pivot and half-cadence pins come last, because those
  // chords are design values in the home key whatever surrounds them.
  std::vector<ChordSpec> plan = buildRepeatingChordPlan(total, mode, harm_idx);
  planSectionalModulations(out.harmony, total, free_bars, mode);
  retuneChordPlan(plan, out.harmony);
  prepareSectionalFugueHarmony(plan, free_bars, mode);
  emitHarmony(out, plan, mode);
  annotateSectionalPivotChords(out, free_bars);

  // --- TOCCATA SECTION (bars 0 .. free_bars-1), V0 only. ---
  // Generalize OrganToccata's archetype machinery to the available bars. The
  // archetype (= seed % 4) differs only in SECTION STRUCTURE, not pitch
  // language: every section is the same chord-tone-anchored scalar-wave
  // figuration (gate-3-clearing stepwise motion). Density per bar rises with the
  // arc tier; the register sweeps up the V0 band as the arc climbs.
  const ToccataArchetype archetype = static_cast<ToccataArchetype>(req.seed % 4);
  const SubjectCharacter character = req.character;  // director blocks Noble here.

  // Build the section windows in bars (inclusive ranges) for the active
  // archetype, scaled to free_bars:
  //   Dramaticus  = a short opening flourish (1/4 of the section, >= 4 bars)
  //                 then one continuous figuration section.
  //   Perpetuus   = one continuous section over the whole free span.
  //   Concertato  = alternating 4-bar sections (forte/piano contrast).
  //   Sectionalis = two clearly-broken halves.
  struct BarWindow {
    int first_bar;
    int last_bar;  // inclusive.
  };
  std::vector<BarWindow> windows;
  // Second-window head bar for Dramaticus (also the fermata-breath bar); 0 for
  // every other archetype (unused).
  int dramaticus_flourish = 0;
  switch (archetype) {
    case ToccataArchetype::Dramaticus: {
      // Opening flourish ~1/4 of the section, snapped to a 4-bar grid and kept
      // in [4, free_bars - 4] so both windows are non-empty. The flourish window
      // is the section's first 4-bar block; its first bars carry the octave
      // cascade + doubled statement (single-voice gesture rhetoric), and the
      // pedal / punctuation enter for the rest of the block. This keeps the solo
      // rhetoric while holding the piece mono ratio inside the toccata ceiling.
      int flourish = ((free_bars / 4 + 3) / 4) * 4;
      flourish = std::clamp(flourish, 4, free_bars - 4);
      dramaticus_flourish = flourish;
      windows.push_back({0, flourish - 1});
      windows.push_back({flourish, free_bars - 1});
      break;
    }
    case ToccataArchetype::Perpetuus:
      windows.push_back({0, free_bars - 1});
      break;
    case ToccataArchetype::Concertato:
      for (int bar = 0; bar < free_bars; bar += 4) {
        windows.push_back({bar, std::min(bar + 3, free_bars - 1)});
      }
      break;
    case ToccataArchetype::Sectionalis: {
      const int mid = ((free_bars / 2 + 3) / 4) * 4;  // split point on a 4-bar grid.
      const int split_bar = std::clamp(mid, 4, free_bars - 4);
      windows.push_back({0, split_bar - 1});
      windows.push_back({split_bar, free_bars - 1});
      break;
    }
  }

  // Per-bar material plan (design values): the archetype decides the bar
  // MATERIAL, not only the section structure.
  //   Dramaticus  = bars 0-1 carry the opening gesture (V0 solo: written-out
  //                 mordent + descending run, the rest of the bar silent --
  //                 the BWV565 dramatic-opening rhetoric); bar 2 and the final
  //                 free bar are declamatory chord blocks (V0 block tones over
  //                 the homophonic V1+V2 strike); the two bars before the
  //                 closing block are a pedal solo (V2 walking pedal alone);
  //                 every other bar runs the scalar-wave figuration.
  //   Perpetuus   = continuous figuration with a single closing chord block.
  //   Concertato  = forte/piano echo pairs: even 4-bar windows run sixteenths
  //                 over the full pedal + punctuation texture, odd windows
  //                 answer in eighths with the punctuation only (the dynamic
  //                 terracing itself is the expression pass's concern).
  //   Sectionalis = a running first half, then a declamatory chordal second
  //                 half (per-bar chord blocks).
  enum class FreeBarKind : std::uint8_t {
    kWave,
    kWavePiano,
    kGesture,
    kUnisonGesture,
    kChordBlock,
    kFermataBlock,
    kPedalSolo,
    kDim7Sweep
  };
  std::vector<FreeBarKind> bar_kinds(static_cast<std::size_t>(free_bars), FreeBarKind::kWave);
  // The octave-cascade opening (bars 0-3) and the fermata breath fit only when
  // the free section is long enough to keep them clear of the pedal-solo pair
  // (free_bars - 5, free_bars - 4); the minimal 8-bar section keeps the older
  // two-bar gesture opening instead.
  const bool dramaticus_cascade = (archetype == ToccataArchetype::Dramaticus) && free_bars >= 12;
  switch (archetype) {
    case ToccataArchetype::Dramaticus: {
      if (dramaticus_cascade) {
        // BWV565 octave cascade: the opening gesture stated high (bar 0), an
        // octave lower (bar 1), then doubled in V0+V1 at the low register
        // (bar 2), answered by a declamatory chord block (bar 3).
        bar_kinds[0] = FreeBarKind::kGesture;
        bar_kinds[1] = FreeBarKind::kGesture;
        bar_kinds[2] = FreeBarKind::kUnisonGesture;
        bar_kinds[3] = FreeBarKind::kChordBlock;
        // The written-out fermata between the opening rhetoric and the running
        // figuration (the free-toccata breath, metered): a whole-bar homophonic
        // chord at the second window's head, shifting its wave start one bar on.
        bar_kinds[static_cast<std::size_t>(dramaticus_flourish)] = FreeBarKind::kFermataBlock;
      } else {
        bar_kinds[0] = FreeBarKind::kGesture;
        bar_kinds[1] = FreeBarKind::kGesture;
        bar_kinds[2] = FreeBarKind::kChordBlock;
      }
      bar_kinds[static_cast<std::size_t>(free_bars - 1)] = FreeBarKind::kChordBlock;
      const int pedal_solo = free_bars - 5;  // two bars, ending before the close.
      bar_kinds[static_cast<std::size_t>(pedal_solo)] = FreeBarKind::kPedalSolo;
      bar_kinds[static_cast<std::size_t>(pedal_solo + 1)] = FreeBarKind::kPedalSolo;
      // BWV565 leading-tone diminished-seventh sweep, minor-mode only: the dim7
      // roll answers the fermata breath at the first wave bar after it, and
      // drives into the close at the last wave bar before the closing chord
      // block. Major-mode Dramaticus keeps the plan untouched (the major vii°7
      // would need a pitch class outside the major scale). Only active in the
      // cascade layout (free_bars >= 12), where the fermata and the two design
      // spots exist.
      if (dramaticus_cascade && mode == Mode::Minor) {
        int first_sweep = dramaticus_flourish + 1;
        while (first_sweep < free_bars &&
               bar_kinds[static_cast<std::size_t>(first_sweep)] != FreeBarKind::kWave) {
          ++first_sweep;
        }
        int last_sweep = free_bars - 1;
        while (last_sweep >= 0 &&
               bar_kinds[static_cast<std::size_t>(last_sweep)] != FreeBarKind::kWave) {
          --last_sweep;
        }
        if (first_sweep < free_bars) {
          bar_kinds[static_cast<std::size_t>(first_sweep)] = FreeBarKind::kDim7Sweep;
        }
        if (last_sweep >= 0 && last_sweep != first_sweep) {
          bar_kinds[static_cast<std::size_t>(last_sweep)] = FreeBarKind::kDim7Sweep;
        }
      }
      break;
    }
    case ToccataArchetype::Perpetuus:
      bar_kinds[static_cast<std::size_t>(free_bars - 1)] = FreeBarKind::kChordBlock;
      break;
    case ToccataArchetype::Concertato:
      for (int bar = 0; bar < free_bars; ++bar) {
        if ((bar / 4) % 2 == 1)
          bar_kinds[static_cast<std::size_t>(bar)] = FreeBarKind::kWavePiano;
      }
      break;
    case ToccataArchetype::Sectionalis:
      // The second window (windows[1]) is the declamatory half: chord blocks
      // alternating with running bars (block + flourish pairs), so the chordal
      // rhetoric arrives without flooding the piece with half notes.
      for (int bar = windows.back().first_bar; bar < free_bars; ++bar) {
        if ((bar - windows.back().first_bar) % 2 == 0)
          bar_kinds[static_cast<std::size_t>(bar)] = FreeBarKind::kChordBlock;
      }
      break;
  }

  // Emit one ToccataSection per window (V0). Every section carries the piece's
  // archetype + character; the (character, archetype) pair is checked by the
  // validator's toccata_archetype_compatible rule (Noble x Dramaticus is the
  // only forbidden pair, and the director already blocks Noble for this form).
  // The first bar of each section is is_section_head so SectionTransition fires
  // once per section.
  //
  // Accompaniment layout: every wave bar carries a V2 pedal and a V1 head
  // punctuation. The Dramaticus gesture and pedal-solo bars stay solo (the
  // dramatic rhetoric is a deliberate single-voice gesture; this is why the
  // toccata mono ceiling is not 0), and its chord-block bars take the
  // homophonic V1+V2 strike instead of the running layers.
  std::vector<FreeLayerPlan> layout(static_cast<std::size_t>(free_bars));
  std::vector<MaterialNote> v0_free_notes;
  // V1 doubling of the unison-gesture bar (the low statement 12 below V0),
  // emitted as a verbatim voice-1 ToccataSection after the window loop.
  std::vector<MaterialNote> unison_v1_notes;
  int unison_bar = -1;
  // Top tone of the previous V0 chord block, threaded across all block bars so
  // consecutive blocks alternate inversions (no stalled repeated pitch).
  int block_prev_top = -1;
  for (const BarWindow& win : windows) {
    ToccataSection section;
    section.archetype = archetype;
    section.character = character;
    section.voice = 0;
    section.start_tick = barTick(win.first_bar);
    section.end_tick = barTick(win.last_bar + 1);
    section.is_section_head = true;
    const bool is_flourish_window =
        (archetype == ToccataArchetype::Dramaticus && win.first_bar == 0);
    // The wave chains conjunctly across the bars of this section (reset at the
    // section head so each section keeps its own register identity).
    int prev_pitch = -1;
    for (int bar = win.first_bar; bar <= win.last_bar; ++bar) {
      FreeLayerPlan& lp = layout[static_cast<std::size_t>(bar)];
      const FreeBarKind kind = bar_kinds[static_cast<std::size_t>(bar)];
      // Where this bar's notes start, so the scalar material just written can be
      // restated in the key sounding at it (the palette walks a fixed C
      // collection and would otherwise keep the whole section in the home key).
      const std::size_t before = section.notes.size();
      if (kind == FreeBarKind::kGesture) {
        // V0 solo opening gesture; the bar's tail is silent and no layer enters.
        // In the octave cascade the gesture keeps the window's opening harmony
        // so bar 1 is an exact octave-lower restatement of bar 0 (BWV565).
        const int chord_bar = dramaticus_cascade ? win.first_bar : bar;
        const int octave_drop = (dramaticus_cascade && bar == 1) ? 1 : 0;
        appendGestureBar(section.notes, bar, plan[static_cast<std::size_t>(chord_bar)], mode,
                         kBandLo[0], kBandHi[0], octave_drop);
        bendIntoLocalKeys(section.notes, before, out.harmony, plan);
        prev_pitch = -1;
        continue;
      }
      if (kind == FreeBarKind::kUnisonGesture) {
        // Deliberate BWV565 unison rhetoric: the opening gesture stated low in
        // V0 and doubled exactly 12 below in V1. Both lines are Material, so the
        // validator's parallel-octave rules are skipped by design; the doubled
        // V1 statement is emitted as a verbatim voice-1 ToccataSection below.
        appendGestureBar(section.notes, bar, plan[static_cast<std::size_t>(win.first_bar)], mode,
                         kBandLo[0], kBandHi[0], /*octave_drop=*/2);
        bendIntoLocalKeys(section.notes, before, out.harmony, plan);
        for (std::size_t note_idx = before; note_idx < section.notes.size(); ++note_idx) {
          MaterialNote doubled = section.notes[note_idx];
          doubled.pitch = static_cast<std::uint8_t>(static_cast<int>(doubled.pitch) - 12);
          unison_v1_notes.push_back(doubled);
        }
        unison_bar = bar;
        prev_pitch = -1;
        continue;
      }
      if (kind == FreeBarKind::kFermataBlock) {
        // Metered breath: one whole-note homophonic triad in V0+V1+V2. V0 takes
        // the low triad top here (low-register voicing above the V1 band); the
        // V1 and V2 whole notes are struck by the free-section layers (lp.fermata).
        std::vector<std::vector<MaterialNote>> block(1);
        appendChordBlockBar(block, bar, plan[static_cast<std::size_t>(bar)], mode,
                            /*top_hi=*/kBandLo[0] + 4, kTicksPerBar);
        for (const auto& note : block[0])
          section.notes.push_back(note);
        lp.fermata = true;
        prev_pitch = -1;
        continue;
      }
      if (kind == FreeBarKind::kChordBlock) {
        // V0 chord-block tones (two half-note top triad tones, alternating
        // inversions across consecutive block bars) over the homophonic V1+V2
        // half-note strike (declamatory full texture).
        appendChordBlockBarAlternating(section.notes, bar, plan[static_cast<std::size_t>(bar)],
                                       mode, kBandHi[0] - 4, block_prev_top);
        lp.pedal = true;
        lp.homophonic = true;
        prev_pitch = -1;
        continue;
      }
      if (kind == FreeBarKind::kPedalSolo) {
        // V2 walking pedal alone (emitted below); V0 and V1 rest.
        prev_pitch = -1;
        continue;
      }
      if (kind == FreeBarKind::kWavePiano) {
        // Concertato piano echo: eighths over the V1 punctuation only (the V2
        // pedal rests, thinning the texture against the forte windows).
        const int base = std::clamp(kBandLo[0], kBandLo[0], kBandHi[0] - 12);
        appendScalarWaveBar(section.notes, bar, plan[static_cast<std::size_t>(bar)], mode,
                            /*notes_per_beat=*/2, base, kBandHi[0], fig_offset, prev_pitch);
        bendIntoLocalKeys(section.notes, before, out.harmony, plan);
        lp.punctuate = true;
        continue;
      }
      if (kind == FreeBarKind::kDim7Sweep) {
        // BWV565 leading-tone diminished-seventh roll (minor-mode Dramaticus).
        // The roll subdivision matches the neighbouring wave bars: sixteenths,
        // tightening to sixteenth triplets only in the final two running bars
        // before the close (the same drive condition the wave bars use below).
        // The roll is confined to the V0 band so it sounds above the V1
        // punctuation and V2 pedal exactly like a wave bar; the accompaniment
        // layers are identical to a wave bar's, so the dim7 rolls over the
        // pedal like the model piece. The roll is built on the leading tone of
        // the key sounding at the bar, and is exempt from the bend that follows
        // every other bar: its four tones are a chromatic design value, and a
        // collection that does not contain the leading tone would flatten the
        // one pitch the figure exists to state.
        const bool tighten_sweep = bar >= free_bars - 3;
        appendDim7SweepBar(section.notes, bar,
                           static_cast<int>(localKeyAt(out.harmony, barTick(bar)).tonic_pc),
                           kBandLo[0], kBandHi[0], tighten_sweep);
        lp.pedal = true;
        lp.punctuate = true;
        prev_pitch = -1;
        continue;
      }
      const ArcPoint arc = arcForBar(req, bar);
      // Sixteenth passagework is the toccata's baseline texture (the corpus
      // duration mass sits on sixteenths, and the free section lives in the
      // piece's early arc cycles where the density tier never rises on its
      // own). The deliberate eighth spots remain: the Dramaticus opening
      // flourish keeps rhetorical breadth before the continuous figuration
      // takes over, and the Concertato piano echo bars (handled above) stay
      // at eighths against the forte windows. The arc keeps shaping the
      // register sweep below.
      const int notes_per_beat = is_flourish_window ? 2 : 4;
      // Register sweep: the band floor rises with the arc register shift, clamped
      // so the wave still fits the V0 band.
      const int base = std::clamp(kBandLo[0] + std::max<int>(0, arc.register_shift), kBandLo[0],
                                  kBandHi[0] - 12);
      // The drive to the fugue tightens from sixteenths to triplet sixteenths
      // only for the final two running bars before the closing block. Limiting
      // the burst preserves the rhetorical acceleration without demanding a
      // long, mechanically continuous triplet run from one manual voice.
      const bool tighten =
          (archetype == ToccataArchetype::Dramaticus || archetype == ToccataArchetype::Perpetuus) &&
          bar >= free_bars - 3;
      appendScalarWaveBar(section.notes, bar, plan[static_cast<std::size_t>(bar)], mode,
                          notes_per_beat, base, kBandHi[0], fig_offset, prev_pitch,
                          /*rotate_figures=*/true, /*triplet=*/tighten);
      bendIntoLocalKeys(section.notes, before, out.harmony, plan);
      lp.pedal = true;
      lp.punctuate = true;
    }
    for (const auto& note : section.notes) {
      v0_free_notes.push_back(note);
    }
    out.material.toccata_sections.push_back(std::move(section));
    pushSpan(asm_ctx, 0, win.first_bar, win.last_bar, VoiceIntent::ToccataCarrier);
  }

  // --- Dramaticus unison doubling (V1 restates the low gesture 12 below V0). ---
  // A dedicated one-bar voice-1 ToccataSection (matched by window) so the
  // doubling replays verbatim without the FigurationCommitted downbeat check
  // (the gesture opens on a non-chord neighbour, so a FigurationCarrier would
  // trip figuration_harmonic_consistency).
  if (!unison_v1_notes.empty()) {
    ToccataSection unison_section;
    unison_section.archetype = archetype;
    unison_section.character = character;
    unison_section.voice = 1;
    unison_section.start_tick = barTick(unison_bar);
    unison_section.end_tick = barTick(unison_bar + 1);
    unison_section.is_section_head = false;
    unison_section.notes = std::move(unison_v1_notes);
    out.material.toccata_sections.push_back(std::move(unison_section));
    pushSpan(asm_ctx, 1, unison_bar, unison_bar, VoiceIntent::ToccataCarrier);
  }

  // --- Dramaticus pedal solo (V2 walking pedal alone, root-fifth quarters). ---
  {
    FigurationSection pedal_solo_section;
    pedal_solo_section.voice = 2;
    pedal_solo_section.is_pedal_prep = true;
    int first = -1;
    int last = -1;
    int walk_prev = -1;
    for (int bar = 0; bar < free_bars; ++bar) {
      if (bar_kinds[static_cast<std::size_t>(bar)] != FreeBarKind::kPedalSolo)
        continue;
      if (first < 0)
        first = bar;
      last = bar;
      appendPedalWalkBar(pedal_solo_section.notes, bar, plan[static_cast<std::size_t>(bar)], mode,
                         kFreeV2Lo, kFreeV2Hi, walk_prev);
    }
    if (!pedal_solo_section.notes.empty()) {
      pedal_solo_section.start_tick = barTick(first);
      pedal_solo_section.end_tick = barTick(last + 1);
      out.material.figuration_sections.push_back(std::move(pedal_solo_section));
      pushSpan(asm_ctx, 2, first, last, VoiceIntent::FigurationCarrier);
    }
  }

  // --- ACCOMPANIMENT LAYERS (V2 pedal + V1 punctuation) over the free section.
  // Half-note V1 punctuation so the V1 voice clears the piece-occupancy floor.
  layout.back().fermata = true;  // explicit dominant half-cadence strike.
  appendFreeSectionLayers(asm_ctx, v0_free_notes, plan, mode, free_bars, layout, req,
                          /*v1_punct_dur=*/kTicksPerBeat * 2);

  // --- FUGUE TAIL (bars free_bars .. total-1). ---
  appendFugueTail(asm_ctx, free_bars, split.fugue_bars, plan, req,
                  /*open_development_texture=*/true);

  // Every voice that is not stating the theme is restated in the key sounding
  // under it. Without this the countersubject, the counterlines and the bass
  // support keep the home collection while the answer speaks the dominant, and
  // the piece sounds the two spellings of the same degree at once.
  bendAccompanimentIntoLocalKeys(out, plan);

  return out;
}

HarnessFixture buildFantasiaAndFugueForm(const ResolvedRequest& req) {
  HarnessFixture out;
  out.voice_plan.num_voices = 3;
  SpanId next_id = 0;
  SectionalAssembly asm_ctx{&out, &next_id, {}};

  const int total = static_cast<int>(req.bars);
  const Mode mode = req.mode;
  const int harm_idx = static_cast<int>(req.seed % 4);
  const int fig_offset = static_cast<int>(req.seed % 4);
  const Split split = splitBars(total);
  const int free_bars = split.free_bars;

  // The itinerary is stamped first so the plan can be restated bar by bar in the
  // key that sounds at it; the pivot and half-cadence pins come last, because
  // those chords are design values in the home key whatever surrounds them.
  std::vector<ChordSpec> plan = buildRepeatingChordPlan(total, mode, harm_idx);
  planSectionalModulations(out.harmony, total, free_bars, mode);
  retuneChordPlan(plan, out.harmony);
  prepareSectionalFugueHarmony(plan, free_bars, mode);
  emitHarmony(out, plan, mode);
  annotateSectionalPivotChords(out, free_bars);

  // Ornament metadata (fixture field only, never a note): the free fantasia
  // section closes at its final bar before the fugue enters, and the ornament
  // pass marks that close with a section-cadence trill. The climax window is
  // left unresolved here (callers fall back to their default arc point).
  out.section_cadence_ticks.push_back(barTick(free_bars - 1));

  // Registration terrace (fixture field only, never a note): the organ steps up
  // a stop at the fantasia->fugue boundary, the fantasia's one structural energy
  // addition. Organ dynamics move in terraces, not crescendos.
  out.registration_step_ticks.push_back(barTick(free_bars));

  // --- FANTASIA SECTION (bars 0 .. free_bars-1), V0 only. ---
  // Generalize Fantasia's contrasting-section cycle to free_bars: contiguous
  // 4-bar sections cycling the styles {Free, Fugal, Toccata, Chordal} starting
  // at a (seed % 4) rotation. Per-section density + register come from Fantasia's
  // proven tiers (notes-per-bar 4 / 8 / 16 / 2; centers C3 / C4 / C5 / C4),
  // shifted up by the arc register shift. Contrast is achieved via distinct
  // density + register per section, not via wide leaps, so the melodic-interval
  // cost stays low. The validator's section_contrast_required rule passes
  // because every adjacent pair differs by density >= 2 OR register >= 5: the
  // proven Free/Fugal/Toccata/Chordal tiers keep those deltas, and the rotation
  // preserves the cyclic adjacency (Chordal -> Free wraps to the proven pair).
  struct StyleSpec {
    FantasiaStyle style;
    int notes_per_beat;  // 1 = quarter (4/bar), 2 = eighth (8/bar), 4 = 16th (16/bar).
    int density_level;   // documentary notes-per-bar tier.
    int base_midi;       // register band floor.
  };
  // Fantasia tiers with a COMPRESSED register spacing: the styles are kept at
  // 6-semitone register steps (58 / 64 / 70 / 64) rather than the original
  // 12-semitone spacing. The narrower spacing roughly halves the melodic leap at
  // each section boundary (lowering the scorer's large-leap statistic).
  //
  // section_contrast_required wants every adjacent style pair to differ in
  // EITHER realized density (>= 2 notes/bar) OR mean register (>= 5 semitones).
  // The realized notes-per-beat tiers are kept distinct enough that the DENSITY
  // axis alone separates every adjacency reachable by the rotation:
  //   Free 1 (quarters) / Fugal 2 (eighths) / Toccata 4 (sixteenths) /
  //   Chordal 2 (eighths). The two density-2 styles (Fugal, Chordal) are never
  //   adjacent in the Free->Fugal->Toccata->Chordal cycle, so each adjacency
  //   spans a >= 2 notes-per-bar gap. Chordal was lifted from quarters to eighths
  //   (its density_level documentary tier stays 4) so a sparse rotation -- one
  //   that pairs Chordal with the quarter-note Free -- still carries enough
  //   conjunct scalar motion to keep the corpus melodic-interval / duration
  //   distribution near the reference (the proven model-score lever). The
  //   6-semitone register spacing (58/64/70/64) is an additional contrast margin.
  static const std::array<StyleSpec, 4> kStyles = {{
      {FantasiaStyle::Free, 1, 4, 58},      // sparse quarters (low of the band).
      {FantasiaStyle::Fugal, 2, 8, 64},     // mid eighths.
      {FantasiaStyle::Toccata, 4, 16, 70},  // dense sixteenths (high of the band).
      {FantasiaStyle::Chordal, 2, 4, 64},   // mid eighths (declamatory).
  }};
  const int rotation = static_cast<int>(req.seed % 4);
  // One uniform register lift for the whole fantasia, taken from the climax
  // cycle's arc point, so adjacent-section register deltas stay at the proven
  // 12-semitone spacing (preserving section_contrast_required margins).
  int free_register_lift = 0;
  for (std::size_t cyc = 0; cyc < req.cycle_count; ++cyc) {
    const ArcPoint pt = req.arc(cyc);
    if (pt.is_climax) {
      free_register_lift = std::max<int>(0, pt.register_shift);
      break;
    }
  }

  // Per-section-style accompaniment matrix. The added layers STRENGTHEN the
  // density contrast section_contrast_required measures (the rule samples only
  // V0's FantasiaSectionContrast notes, so the layers never weaken it):
  //   Free    -> V2 pedal only (sparse, improvisatory).
  //   Fugal   -> V2 pedal + V1 head punctuation.
  //   Toccata -> V2 pedal + V1 head punctuation.
  //   Chordal -> V1 + V2 strike together (half-note homophony, declamatory).
  std::vector<FreeLayerPlan> layout(static_cast<std::size_t>(free_bars));
  std::vector<MaterialNote> v0_free_notes;
  // Top tone of the previous Chordal block, threaded across the Chordal bars so
  // consecutive blocks alternate inversions (no stalled repeated pitch).
  int chordal_prev_top = -1;

  int section_index = 0;
  for (int sec_start = 0; sec_start < free_bars; sec_start += 4) {
    const int sec_last = std::min(sec_start + 3, free_bars - 1);
    const StyleSpec& sp = kStyles[static_cast<std::size_t>((rotation + section_index) % 4)];

    FantasiaSection section;
    section.voice = 0;
    section.start_tick = barTick(sec_start);
    section.end_tick = barTick(sec_last + 1);
    section.is_section_head = true;
    section.style = sp.style;
    section.density_level = sp.density_level;
    // The whole fantasia is lifted by ONE uniform register shift (the climax
    // cycle's, applied to every section) so the proven per-section register
    // deltas (12 semitones between adjacent styles) are preserved exactly; a
    // per-section shift could compress the Chordal -> Free wrap below the
    // 5-semitone contrast margin. The proven density deltas (4/8/16/4) already
    // satisfy section_contrast_required for the non-wrap adjacencies.
    const int base = std::clamp(sp.base_midi + free_register_lift, 40, 84);
    // The wave chains conjunctly within the section (reset at the section head so
    // each style keeps its own register identity, preserving the per-section
    // register contrast section_contrast_required measures).
    int prev_pitch = -1;
    for (int bar = sec_start; bar <= sec_last; ++bar) {
      FreeLayerPlan& lp = layout[static_cast<std::size_t>(bar)];
      lp.pedal = true;  // every style carries the pedal.
      // Where this bar's notes start, so the scalar material just written can be
      // restated in the key sounding at it (the palette walks a fixed C
      // collection and would otherwise keep the whole section in the home key).
      const std::size_t before = section.notes.size();
      if (sp.style == FantasiaStyle::Chordal && (bar - sec_start) % 2 == 0) {
        // Declamatory chordal style: V0 half-note chord-block tones (alternating
        // inversions across blocks) over the homophonic V1+V2 strike, on every
        // other bar -- the odd bars answer with the running wave so the chordal
        // rhetoric arrives without flooding the section with half notes.
        appendChordBlockBarAlternating(section.notes, bar, plan[static_cast<std::size_t>(bar)],
                                       mode, base + 14, chordal_prev_top);
        lp.homophonic = true;  // V1 + V2 strike together (half-note chordal).
        prev_pitch = -1;
        continue;
      }
      if (sp.style == FantasiaStyle::Free && bar == sec_start) {
        // A Free section opens with a rhetorical gesture (mordent onset +
        // descending run, the bar tail silent) before the quarter-note wave.
        appendGestureBar(section.notes, bar, plan[static_cast<std::size_t>(bar)], mode, base,
                         base + 14);
        bendIntoLocalKeys(section.notes, before, out.harmony, plan);
        prev_pitch = -1;
        continue;
      }
      appendScalarWaveBar(section.notes, bar, plan[static_cast<std::size_t>(bar)], mode,
                          sp.notes_per_beat, base, base + 14, fig_offset, prev_pitch);
      bendIntoLocalKeys(section.notes, before, out.harmony, plan);
      if (sp.style == FantasiaStyle::Fugal || sp.style == FantasiaStyle::Toccata) {
        lp.punctuate = true;  // V1 head punctuation on top of the pedal.
      }
    }
    for (const auto& note : section.notes) {
      v0_free_notes.push_back(note);
    }
    out.material.fantasia_sections.push_back(std::move(section));
    pushSpan(asm_ctx, 0, sec_start, sec_last, VoiceIntent::FantasiaCarrier);
    ++section_index;
  }

  // --- ACCOMPANIMENT LAYERS (V2 pedal + V1 punctuation) over the free section.
  // Quarter-note V1 punctuation keeps the dense fantasia sections light so the
  // running V0 figuration stays in the foreground (the fantasia gate does not
  // require a V1 occupancy floor; a longer V1 strike would only depress the
  // corpus model score under the dense Toccata-style sections).
  layout.back().fermata = true;  // explicit dominant half-cadence strike.
  appendFreeSectionLayers(asm_ctx, v0_free_notes, plan, mode, free_bars, layout, req,
                          /*v1_punct_dur=*/kTicksPerBeat);

  // --- FUGUE TAIL (bars free_bars .. total-1). ---
  appendFugueTail(asm_ctx, free_bars, split.fugue_bars, plan, req,
                  /*open_development_texture=*/false);

  // Every voice that is not stating the theme is restated in the key sounding
  // under it. Without this the countersubject, the counterlines and the bass
  // support keep the home collection while the answer speaks the dominant, and
  // the piece sounds the two spellings of the same degree at once.
  bendAccompanimentIntoLocalKeys(out, plan);

  return out;
}

}  // namespace bach::composer
