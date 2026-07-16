#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "composer/character_profile.h"
#include "composer/figuration.h"
#include "composer/figuration_palette.h"
#include "composer/form_builders.h"
#include "composer/material.h"
#include "composer/minor_material.h"
#include "composer/span.h"
#include "composer/subject_catalog.h"
#include "composer/texture_helpers.h"
#include "composer/tonal_answer.h"
#include "composer/voice_intent.h"
#include "core/basic_types.h"

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
using detail::kHarmonyPatterns;             // NOLINT(build/namespaces)
using detail::kHarmonyPatternsMinor;        // NOLINT(build/namespaces)
using detail::Mode;                         // NOLINT(build/namespaces)
using detail::scaleUp;                      // NOLINT(build/namespaces)
using detail::subjectIndexFor;              // NOLINT(build/namespaces)
using tables::kSubjectCatalogMajor;         // NOLINT(build/namespaces)
using tables::kSubjectCatalogMajorRhythms;  // NOLINT(build/namespaces)
using tables::kSubjectCatalogMinor;         // NOLINT(build/namespaces)
using tables::kSubjectCatalogMinorRhythms;  // NOLINT(build/namespaces)

constexpr Tick kQuarter = kTicksPerBeat;
constexpr Tick kEighth = kTicksPerBeat / 2;
constexpr Tick kSixteenth = kTicksPerBeat / 4;

// One subject statement is 16 catalog notes spanning 4 bars. Durations come
// from the per-mode catalog rhythm rows rather than being fixed quarters.
constexpr int kSubjectNotes = 16;
constexpr int kSubjectBars = 4;

// Per-voice register bands (MIDI) for the fugue tail. Disjoint and strictly
// ordered (V0 highest, V2 lowest) so band-confined material never crosses.
// Each band holds one subject statement (major spans 14 semitones, minor 12).
constexpr int kBandLo[3] = {72, 56, 40};
constexpr int kBandHi[3] = {88, 71, 55};

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

/// @brief Largest whole-octave offset that lands a subject inside a voice band.
///
/// The subject catalog sits in the V0 band by construction; to restate it lower
/// (V1 / V2) the whole line is shifted by whole octaves (preserving pitch
/// classes, so it stays diatonic) until its range fits the band.
///
/// @param subject The 16-note subject pattern (V0-band pitches).
/// @param voice Target voice index (0..2) selecting the band.
/// @return The per-note semitone offset (a multiple of 12) to apply.
int octaveOffsetForBand(const std::array<std::uint8_t, 16>& subject, int voice) {
  int lo = 127;
  int hi = 0;
  for (std::uint8_t pitch : subject) {
    lo = std::min(lo, static_cast<int>(pitch));
    hi = std::max(hi, static_cast<int>(pitch));
  }
  int offset = 0;
  while (hi + offset > kBandHi[voice]) {
    offset -= 12;
  }
  while (lo + offset < kBandLo[voice]) {
    offset += 12;
  }
  return offset;
}

bool shouldUseTonalAnswer(const std::array<std::uint8_t, 16>& subject, std::uint8_t tonic_pc) {
  const std::uint8_t tonic = static_cast<std::uint8_t>(tonic_pc % 12);
  const std::uint8_t dominant = static_cast<std::uint8_t>((tonic + 7) % 12);
  const std::uint8_t first = static_cast<std::uint8_t>(subject[0] % 12);
  if (first == dominant) {
    return true;
  }
  if (first != tonic) {
    return false;
  }
  for (int i = 1; i < 4; ++i) {
    if (subject[static_cast<std::size_t>(i)] % 12 == dominant) {
      return true;
    }
  }
  return false;
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

/// @brief Build the per-bar chord plan for the whole piece.
///
/// One diatonic triad per bar, cycling the mode's 4-chord harmony catalog by
/// 4-bar block. Deterministic from (seed, mode). The minor catalog's V chord is
/// major (harmonic-minor dominant), so the leading tone lives in the harmony
/// only -- the free figuration walks the natural-minor scale and never injects a
/// B natural into a stepwise line (avoiding the Ab->B augmented 2nd).
std::vector<ChordSpec> buildChordPlan(int total_bars, Mode mode, int harm_idx) {
  const auto& patterns = (mode == Mode::Minor) ? kHarmonyPatternsMinor : kHarmonyPatterns;
  std::vector<ChordSpec> plan;
  plan.reserve(static_cast<std::size_t>(total_bars));
  for (int bar = 0; bar < total_bars; ++bar) {
    const auto& pattern = patterns[static_cast<std::size_t>((harm_idx + bar / 4) % 4)];
    plan.push_back(pattern[static_cast<std::size_t>(bar % 4)]);
  }
  return plan;
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

/// @brief Append one bar of theme-consonant, parallel-free scalar figuration.
///
/// The parallel-aware figuration shared with the fugue family (form_fugue's
/// appendFigurationBar). Every beat opens on a consonant chord tone chosen via
/// the tier-scored consonantChordTone selector (consonant ∧ parallel-free first,
/// then consonant, then least-dissonant), reading back every earlier-placed
/// voice from `registry` so the on-beat verticals stay consonant and no
/// same-direction perfect fifth/octave is formed against any earlier voice. The
/// notes between anchors walk by single scale steps (the corpus melodic mass is
/// on steps); a wave step that would form a parallel reverses direction, and the
/// per-tick voice order V0 >= V1 >= V2 is held by clamping each note inside the
/// concurrent voices' order window. Every emitted note is recorded back into the
/// registry so a voice placed later in the same window avoids a parallel here.
///
/// @param registry Read/written for the inter-voice parallel-avoidance lookup.
/// @param section Figuration section receiving the bar's notes.
/// @param bar Absolute bar index.
/// @param voice Voice index (selects the band the wave is clamped into).
/// @param chord The bar's chord (supplies the per-beat chord tones).
/// @param mode Diatonic mode selecting the scale walker.
/// @param notes_per_beat Subdivision density (1 / 2 / 4).
/// @param offset Seed-derived start-register offset above the band floor.
/// @param prev_anchor Running anchor threaded across bars; updated to the bar's
///        last anchor so the next bar's first anchor chains stepwise from it.
void appendFigurationBar(ThemeToneRegistry& registry, FigurationSection& section, int bar,
                         int voice, const ChordSpec& chord, Mode mode, int notes_per_beat,
                         int offset, int& prev_anchor) {
  int center = scaleUp(kBandLo[voice], offset + 2, mode);
  if (center > kBandHi[voice] - 4) {
    center = scaleUp(kBandLo[voice], offset, mode);
  }
  if (prev_anchor <= 0) {
    // Section seam: seed the line's audible "previous pitch" from what this
    // voice actually sounded just before the bar (a theme entry or an earlier
    // figuration span), falling back to the register centre when the voice
    // was silent. A synthetic centre here would let a seam arrival land an
    // undetectable parallel against a voice moving across the same seam.
    const int sounding =
        registry.soundingPitchInVoice(static_cast<VoiceId>(voice), barTick(bar) - kSixteenth);
    prev_anchor = (sounding >= 0) ? sounding : center;
  }
  const Tick step =
      (notes_per_beat == 4) ? kSixteenth : ((notes_per_beat == 2) ? kEighth : kQuarter);
  std::vector<int> theme_pitches;
  // Mutable: the window stretches to contain a beat anchor snapped outside it
  // (see the anchor commit below).
  int wave_lo = std::max(kBandLo[voice], center - 5);
  int wave_hi = std::min(kBandHi[voice], center + 5);
  int cursor = std::clamp(prev_anchor, wave_lo, wave_hi);
  int dir = (cursor <= center) ? 1 : -1;
  // Wave stride for the current beat: 1 scale degree on most beats (stepwise
  // passing motion), widened to 2 degrees (a broken-third chain) on every
  // third beat. A wave that only ever steps stacks the whole line into the
  // three step|step interval-bigram bins -- a concentration the reference
  // corpus never reaches -- while the rotated third-chains supply the inside-
  // beat skips the corpus writes. The stride changes nothing else: the
  // parallel / harshness / voice-order machinery below vets every candidate
  // the same way at either stride.
  int wave_degrees = 1;
  auto stepScale = [&](int from, int direction) {
    return direction > 0 ? scaleUp(from, wave_degrees, mode) : scaleDown(from, wave_degrees, mode);
  };
  int last_pitch = cursor;
  // The line's audibly-previous pitch: the last *emitted* note, not the last
  // beat anchor. Faster lines move between anchors, and a parallel is heard
  // from the note actually sounding immediately before the new onset. -1 until
  // the line has emitted (or chained from) a real note, which disables the
  // parallel check on a section-opening anchor.
  int line_prev = (prev_anchor > 0) ? prev_anchor : -1;
  std::vector<ConcurrentMotion> motions;
  for (int beat = 0; beat < 4; ++beat) {
    wave_degrees = ((bar + beat) % 3 == 1) ? 2 : 1;
    const Tick beat_tick = barTick(bar) + static_cast<Tick>(beat) * kTicksPerBeat;
    registry.concurrentThemePitches(beat_tick, static_cast<VoiceId>(voice), theme_pitches);
    // Sample the earlier voices' previous pitch one sixteenth before the onset:
    // the finest subdivision any line uses. This reproduces the union-onset
    // note pair the validator judges, regardless of this line's own stride --
    // a beat-wide window would read a 16th-note voice four notes back and miss
    // the audible motion into this onset.
    registry.concurrentMotions(beat_tick - kSixteenth, beat_tick, static_cast<VoiceId>(voice),
                               kTailVoices, motions);
    const int anchor =
        consonantChordTone(chord, voice, kBandLo[voice], kBandHi[voice], cursor, theme_pitches,
                           line_prev, motions, mode, beat == 0, /*window_pitches=*/{},
                           /*parallel_free_over_consonant=*/true);
    cursor = std::clamp(anchor, kBandLo[voice], kBandHi[voice]);
    // The consonance / parallel constraints can snap the anchor outside the
    // working wave window. Stretch the window to contain it: with the cursor
    // outside, every wave step would reflect onto the single pitch one step
    // back toward the window -- no alternative candidates -- so the parallel
    // veto would have nothing to displace to.
    wave_lo = std::min(wave_lo, cursor);
    wave_hi = std::max(wave_hi, cursor);
    for (int sub = 0; sub < notes_per_beat; ++sub) {
      const Tick tick =
          barTick(bar) + static_cast<Tick>(beat) * kTicksPerBeat + static_cast<Tick>(sub) * step;
      int pitch;
      if (sub == 0) {
        pitch = cursor;
      } else {
        const int from = cursor;
        auto step_from = [&](int direction) {
          int candidate = stepScale(from, direction);
          if (candidate > wave_hi) {
            candidate = stepScale(from, -1);
          } else if (candidate < wave_lo) {
            candidate = stepScale(from, 1);
          }
          return std::clamp(candidate, kBandLo[voice], kBandHi[voice]);
        };
        int next = step_from(dir);
        // Sixteenth-grain window for the same reason as the anchor above: an
        // eighth/quarter-stride wave sampling its own stride back would miss
        // the audible motion of an already-placed sixteenth line.
        registry.concurrentMotions(tick - kSixteenth, tick, static_cast<VoiceId>(voice),
                                   kTailVoices, motions);
        auto wave_is_parallel = [&](int cand) {
          for (const ConcurrentMotion& motion : motions) {
            if (formsPerfectParallel(from, cand, motion.prev, motion.curr)) {
              return true;
            }
          }
          return false;
        };
        if (wave_is_parallel(next)) {
          const int reversed = step_from(-dir);
          if (!wave_is_parallel(reversed)) {
            dir = -dir;
            next = reversed;
          }
        }
        // Harshness-aware wave: a passing tone that lands a minor 2nd, tritone,
        // or major 7th against a concurrently sounding earlier voice is the
        // sharpest off-beat clash two independent wave lines can produce. Every
        // sixteenth slot the candidate sounds through is scanned, so a slower
        // line cannot sustain into a clash an already-placed faster line lands
        // mid-duration. Reverse direction (still a single scale step) when the
        // reversed step is both parallel-free and clash-free; milder seconds /
        // sevenths are left alone so ordinary passing motion over a sustained
        // tone survives.
        auto wave_is_harsh = [&](int cand) {
          for (VoiceId other = 0; other < kTailVoices; ++other) {
            if (other == static_cast<VoiceId>(voice)) {
              continue;
            }
            for (Tick slot = tick; slot < tick + step; slot += kSixteenth) {
              const int sounding = registry.soundingPitchInVoice(other, slot);
              if (sounding < 0) {
                continue;
              }
              const int ic = std::abs(cand - sounding) % 12;
              if (ic == 1 || ic == 6 || ic == 11) {
                return true;
              }
            }
          }
          return false;
        };
        if (!wave_is_parallel(next) && wave_is_harsh(next)) {
          const int reversed = step_from(-dir);
          if (!wave_is_parallel(reversed) && !wave_is_harsh(reversed)) {
            dir = -dir;
            next = reversed;
          } else {
            // Both single steps clash (or the reversed step lands a parallel):
            // try a third-skip in either direction before accepting the clash.
            // A scale-third skip is the smallest non-step move and reads as an
            // ordinary chord-tone skip inside figuration.
            for (const int skip_dir : {dir, -dir}) {
              const int skip = (skip_dir > 0) ? scaleUp(from, 2, mode) : scaleDown(from, 2, mode);
              if (skip < wave_lo || skip > wave_hi) {
                continue;
              }
              if (!wave_is_parallel(skip) && !wave_is_harsh(skip)) {
                next = skip;
                break;
              }
            }
          }
        }
        int order_ceiling = kBandHi[voice];
        int order_floor = kBandLo[voice];
        for (const ConcurrentMotion& motion : motions) {
          if (motion.curr < 0) {
            continue;
          }
          if (motion.voice < voice) {
            order_ceiling = std::min(order_ceiling, motion.curr);
          } else if (motion.voice > voice) {
            order_floor = std::max(order_floor, motion.curr);
          }
        }
        if (order_floor <= order_ceiling) {
          next = std::clamp(next, order_floor, order_ceiling);
          if (next == from && order_floor < order_ceiling) {
            int up = stepScale(from, 1);
            int down = scaleDown(from, 1, mode);
            if (up <= order_ceiling && up != from) {
              next = up;
            } else if (down >= order_floor && down != from) {
              next = down;
            }
          }
        }
        if (next > from) {
          dir = 1;
        } else if (next < from) {
          dir = -1;
        }
        cursor = next;
        pitch = cursor;
      }
      last_pitch = pitch;
      line_prev = pitch;
      addNote(section.notes, tick, step, pitch);
      registry.record(tick, static_cast<VoiceId>(voice), pitch, step);
    }
  }
  prev_anchor = last_pitch;
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
  // Pick the pitch for the next pedal onset: the chord root, unless holding it
  // would extend an identical-pitch run to the gate cap, in which case the fifth
  // (else the third) above the root is taken, falling within the V2 band and
  // parallel-free against the concurrent earlier voices.
  auto pedal_pitch = [&](int root, const ChordSpec& chord) {
    int pitch = root;
    if (last_pedal_pitch == root && pedal_run >= 3) {
      // Candidate chord tones (third and fifth, each both above and below the
      // root) so the alternation can move by the smallest available interval
      // rather than always leaping a fifth -- a conjunct walking bass keeps the
      // melodic-interval distribution near the corpus and avoids stacking wide
      // leaps. Ordered by increasing distance from the root.
      const int third_iv = chord.minor ? 3 : 4;
      const int candidates[4] = {root + third_iv, root - (12 - third_iv), root + 7, root - 5};
      int best = -1;
      int best_dist = 1 << 30;
      for (int alt : candidates) {
        if (alt < kFreeV2Lo || alt > kFreeV2Hi || alt == root) {
          continue;
        }
        bool parallel = false;
        for (const ConcurrentMotion& motion : motions) {
          if (formsPerfectParallel(last_pedal_pitch, alt, motion.prev, motion.curr)) {
            parallel = true;
            break;
          }
        }
        if (parallel) {
          continue;
        }
        const int dist = std::abs(alt - last_pedal_pitch);
        if (dist < best_dist) {
          best_dist = dist;
          best = alt;
        }
      }
      if (best >= 0) {
        pitch = best;
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
    registry.concurrentMotions(bar_start - kTicksPerBar, bar_start, /*voice=*/2,
                               /*num_voices=*/3, motions);
    const int root = consonantChordTone(chord, /*voice=*/2, kFreeV2Lo, kFreeV2Hi, centre,
                                        theme_pitches, /*line_prev=*/-1, motions, mode,
                                        /*downbeat=*/true);
    if (pl.fermata) {
      // Metered breath: one whole-note chord root in the V2 band, struck with
      // the V0+V1 whole notes (a single homophonic strike, no re-articulation).
      const int pitch = pedal_pitch(root, chord);
      addNote(pedal_section.notes, bar_start, kTicksPerBar, pitch);
      registry.record(bar_start, /*voice=*/2, pitch, kTicksPerBar);
      continue;
    }
    const bool homophonic = pl.homophonic;
    const bool walking = !homophonic && (bar % 4) == walk_phase;
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
    } else if (walking) {
      // Half-note root then a half-note fifth above (still inside the band).
      int fifth = root + 7;
      if (fifth > kFreeV2Hi)
        fifth = root - 5;  // fall to the fourth below if the fifth overflows.
      fifth = std::clamp(fifth, kFreeV2Lo, kFreeV2Hi);
      addNote(pedal_section.notes, bar_start, kTicksPerBeat * 2, root);
      addNote(pedal_section.notes, bar_start + kTicksPerBeat * 2, kTicksPerBeat * 2, fifth);
      registry.record(bar_start, /*voice=*/2, root, kTicksPerBeat * 2);
      registry.record(bar_start + kTicksPerBeat * 2, /*voice=*/2, fifth, kTicksPerBeat * 2);
      last_pedal_pitch = fifth;
      pedal_run = 1;
    } else {
      const int pitch = pedal_pitch(root, chord);
      addNote(pedal_section.notes, bar_start, kTicksPerBar, pitch);
      registry.record(bar_start, /*voice=*/2, pitch, kTicksPerBar);
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
///        wave bars' subdivision (the toccata's drive tightens to triplets in
///        the section's second half).
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

// ---------------------------------------------------------------------------
// appendFugueTail: a self-contained 3-voice fugue from `first_bar` spanning
// `bars` bars (>= 8). Built inline with the PreludeAndFugue idiom (no shared fugue
// assembly). Layout (relative to first_bar):
//   exposition: V0 subject (0-3), V1 answer -P4 (4-7), V2 re-entry -P8 (8-11)
//               when bars >= 12, else a compressed 2-entry exposition.
//   counterline: band-confined figuration in the non-thematic voices.
//   stretto: two overlapping subject statements <= 1 bar apart near the end
//            (only when bars >= 12); aligned to the climax cycle when possible.
//   cadence: a 2-bar V0 Picardy close on the home tonic.
// All material is Material; band confinement keeps V0 >= V1 >= V2.
// ---------------------------------------------------------------------------
void appendFugueTail(SectionalAssembly& asm_ctx, int first_bar, int bars,
                     const std::vector<ChordSpec>& plan, const ResolvedRequest& req) {
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

  // --- Stamp a 16-note subject statement transposed by `semis` into `voice`. ---
  auto stamp_subject = [&](int base_bar, int semis, int theme_voice) {
    Tick cursor = barTick(base_bar);
    for (int note = 0; note < kSubjectNotes; ++note) {
      const int pitch = static_cast<int>(subj_pat[static_cast<std::size_t>(note)]) + semis;
      const Tick dur = subj_rhythm[static_cast<std::size_t>(note)];
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
  auto add_counterline = [&](VoiceId voice, int first, int last, int notes_per_beat) {
    FigurationSection section;
    section.voice = voice;
    section.start_tick = barTick(first);
    section.end_tick = barTick(last + 1);
    int prev_anchor = 0;
    for (int bar = first; bar <= last; ++bar) {
      appendFigurationBar(registry, section, bar, voice, plan[static_cast<std::size_t>(bar)], mode,
                          notes_per_beat, fig_offset, prev_anchor);
    }
    coalesceConsecutiveSamePitch(section.notes);
    out.material.figuration_sections.push_back(section);
    pushSpan(asm_ctx, voice, first, last, VoiceIntent::FigurationCarrier);
  };

  // --- Add one sustained chord-tone support note over [first_bar, last_bar]. ---
  // A consonant, parallel-free chord tone of the first bar's chord, held as one
  // whole-bar-per-bar tone in the voice band (FigurationCarrier so it replays
  // verbatim). Used to thicken bars that would otherwise rest a voice (the
  // second half of the solo subject entry, the post-stretto fill, the cadence)
  // up to a >= 2-voice texture without adding a second running figuration line.
  auto add_sustained_support = [&](VoiceId voice, int first, int last) {
    FigurationSection section;
    section.voice = voice;
    section.start_tick = barTick(first);
    section.end_tick = barTick(last + 1);
    section.is_pedal_prep = true;  // exempt the held tone from the downbeat check.
    std::vector<int> theme_pitches;
    std::vector<ConcurrentMotion> motions;
    int line_prev = -1;
    const int centre = (kBandLo[voice] + kBandHi[voice]) / 2;
    for (int bar = first; bar <= last; ++bar) {
      const Tick bar_start = barTick(bar);
      registry.concurrentThemePitches(bar_start, voice, theme_pitches);
      registry.concurrentMotions(bar_start - kTicksPerBar, bar_start, voice, kTailVoices, motions);
      const int pitch =
          consonantChordTone(plan[static_cast<std::size_t>(bar)], voice, kBandLo[voice],
                             kBandHi[voice], centre, theme_pitches, line_prev, motions, mode,
                             /*downbeat=*/true);
      addNote(section.notes, bar_start, kTicksPerBar, pitch);
      registry.record(bar_start, voice, pitch, kTicksPerBar);
      line_prev = pitch;
    }
    coalesceConsecutiveSamePitch(section.notes);
    out.material.figuration_sections.push_back(section);
    pushSpan(asm_ctx, voice, first, last, VoiceIntent::FigurationCarrier);
  };

  // === EXPOSITION ===========================================================
  const int v0_off = octaveOffsetForBand(subj_pat, 0);
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
  auto append_countersubject_from = [&](const std::vector<MaterialNote>& source, int voice,
                                        Tick start, Tick end) {
    const int center = (kBandLo[voice] + kBandHi[voice]) / 2;
    // Pass 1: select one anchor per source note (consonant against the source,
    // contrary-motion preferred, repeated-pitch capped -- the scoring is
    // unchanged; pass 2 only decides the rhythm each anchor is realized with).
    struct CsAnchor {
      Tick tick;
      Tick dur;
      int pitch;
    };
    std::vector<CsAnchor> anchors;
    int prev_cs = -1;
    int prev_src = -1;
    int repeat_run = 1;  // consecutive equal counterline pitches so far.
    for (const auto& note : source) {
      if (note.start_tick < start || note.start_tick >= end)
        continue;
      const int src = static_cast<int>(note.pitch);
      const int src_dir = (prev_src < 0) ? 0 : (src > prev_src ? 1 : (src < prev_src ? -1 : 0));
      const int target = (prev_cs < 0) ? center : prev_cs;
      int best = -1;
      int best_score = 1 << 30;
      for (int pitch = kBandLo[voice]; pitch <= kBandHi[voice]; ++pitch) {
        if (!detail::inScale(pitch, mode)) {
          continue;
        }
        const bool consonant = isConsonantIc(pitch - src);
        const int cs_dir = (prev_cs < 0) ? 0 : (pitch > prev_cs ? 1 : (pitch < prev_cs ? -1 : 0));
        const bool similar = (src_dir != 0 && cs_dir == src_dir);
        const int cur_ic = ((std::abs(pitch - src) % 12) + 12) % 12;
        const bool perfect_arrival = (cur_ic == 0 || cur_ic == 7);
        const bool repeats_prev = (pitch == prev_cs);
        int score = std::abs(pitch - target);
        if (!consonant) {
          score += 10000;  // dissonance against the source is the worst.
        }
        if (similar && perfect_arrival) {
          score += 4000;  // forbidden parallel arrival.
        } else if (similar) {
          score += 200;  // prefer genuine contrary motion.
        }
        if (repeats_prev && src_dir != 0) {
          score += 300;
        }
        if (repeats_prev && repeat_run >= 4) {
          score += 100000;
        }
        if (score < best_score) {
          best_score = score;
          best = pitch;
        }
      }
      const int pitch = (best >= 0) ? best : std::clamp(target, kBandLo[voice], kBandHi[voice]);
      repeat_run = (pitch == prev_cs) ? repeat_run + 1 : 1;
      anchors.push_back({note.start_tick, note.duration, pitch});
      prev_cs = pitch;
      prev_src = src;
    }
    // Pass 2: emit. A quarter-note anchor is realized with a complementary
    // rhythm rotating per beat -- kept quarter / sixteenth run toward the next
    // anchor / broken-chord arc -- so the counterline moves against the
    // theme's longer values (the corpus duration profile is sixteenth-
    // dominant) while the rotation keeps the bigram surface diverse (any
    // single figure applied uniformly over-concentrates it). Interior tones
    // move only while the theme voice holds its pitch, so each onset pair the
    // ear (and the parallel detector) samples is oblique motion; the vetted
    // anchors remain the only simultaneous-motion points.
    for (std::size_t i = 0; i < anchors.size(); ++i) {
      const CsAnchor& a = anchors[i];
      const bool has_next = (i + 1 < anchors.size());
      const int figure = static_cast<int>((a.tick / kTicksPerBeat) % 3);
      if (a.dur != kTicksPerBeat || !has_next || figure == 0) {
        addNote(out.material.countersubject, a.tick, a.dur, a.pitch);
        registry.record(a.tick, static_cast<VoiceId>(voice), a.pitch, a.dur);
        continue;
      }
      const Tick sixteenth = kTicksPerBeat / 4;
      std::array<int, 4> figure_pitches{a.pitch, a.pitch, a.pitch, a.pitch};
      if (figure == 1) {
        // Sixteenth run toward the next anchor, folding back once the target
        // is reached so the anchor arrives by step instead of being overshot.
        const int run_target = anchors[i + 1].pitch;
        int dir = (run_target > a.pitch) ? 1 : -1;
        int cur = a.pitch;
        for (int k = 1; k < 4; ++k) {
          cur = (dir > 0) ? detail::scaleUp(cur, 1, mode) : detail::scaleDown(cur, 1, mode);
          cur = std::clamp(cur, kBandLo[voice], kBandHi[voice]);
          if ((dir > 0 && cur >= run_target) || (dir < 0 && cur <= run_target)) {
            dir = -dir;
          }
          figure_pitches[static_cast<std::size_t>(k)] = cur;
        }
      } else {
        // Broken-chord arc (anchor / 3rd / 5th / 3rd), arcing downward when
        // the band top leaves no room above the anchor.
        const int dir = (a.pitch + 7 <= kBandHi[voice]) ? 1 : -1;
        const int third =
            (dir > 0) ? detail::scaleUp(a.pitch, 2, mode) : detail::scaleDown(a.pitch, 2, mode);
        const int fifth =
            (dir > 0) ? detail::scaleUp(a.pitch, 4, mode) : detail::scaleDown(a.pitch, 4, mode);
        figure_pitches = {a.pitch, third, fifth, third};
      }
      for (int k = 0; k < 4; ++k) {
        const int p =
            std::clamp(figure_pitches[static_cast<std::size_t>(k)], kBandLo[voice], kBandHi[voice]);
        addNote(out.material.countersubject, a.tick + k * sixteenth, sixteenth, p);
        registry.record(a.tick + k * sixteenth, static_cast<VoiceId>(voice), p, sixteenth);
      }
    }
  };
  stamp_subject(first_bar + 0, v0_off, 0);
  pushSpan(asm_ctx, 0, first_bar + 0, first_bar + 3, VoiceIntent::SubjectCarrier);
  // Texture thickening of the solo subject entry: the subject head enters alone
  // (authentic fugue rhetoric) for its first two bars, then a V2 sustained
  // chord-tone support joins for the remaining two bars so only the opening
  // gesture is monophonic (capping the tail's solo contribution to the piece
  // mono ratio at two bars instead of four).
  add_sustained_support(2, first_bar + 2, first_bar + 3);

  // V1 real answer (subject - P4) in the V1 band, entering one entry-window
  // (4 bars) after the subject. For a short (8-bar) fugue tail the answer is
  // truncated so it never extends into the reserved cadence bars (the validator
  // checks only the answer's first note, so a partial answer stays valid).
  const int answer_off = octaveOffsetForBand(subj_pat, 1);
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
  // V2 sustained chord-tone support under the answer entry, built last so it
  // reads the V1 answer and V0 countersubject from the registry and stays
  // consonant and parallel-free below them. This makes the answer bar-group a
  // full three-voice exposition texture (matching the fugue family) instead of
  // the two-voice answer + countersubject that left the bass register empty.
  add_sustained_support(2, answer_first, answer_last);

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
    const int third_off = octaveOffsetForBand(subj_pat, 2);
    stamp_subject(first_bar + 8, third_off, 2);
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

    // Leader: a full subject statement in V0 (bars leader_bar .. +3).
    stamp_subject(leader_bar, v0_off, 0);
    pushSpan(asm_ctx, 0, leader_bar, leader_bar + 3, VoiceIntent::SubjectCarrier);

    // Follower: a subject statement in V1 entering one bar later (genuine
    // overlap), 12 notes (3 bars) so it stays inside the development window.
    const int follower_off = octaveOffsetForBand(subj_pat, 1);
    StrettoDecl stretto;
    stretto.leader_voice = 0;
    stretto.follower_voice = 1;
    stretto.leader_entry_tick = barTick(leader_bar);
    stretto.leader_length_ticks = barTick(kSubjectBars);
    stretto.follower_entry_tick = barTick(leader_bar + 1);
    // The validator checks follower_notes[i] == material.subject[i] +
    // interval_semis, where material.subject[0..15] is the V0 exposition subject
    // (transposed by v0_off). The follower lives in the V1 band, so the declared
    // interval is the band-octave difference; the follower pitch is computed from
    // the raw pattern + follower_off, which equals subject[i] + interval_semis.
    stretto.interval_semis = follower_off - v0_off;
    Tick follower_cursor = barTick(leader_bar + 1);
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
    pushSpan(asm_ctx, 1, leader_bar + 1, leader_bar + 3, VoiceIntent::StrettoCarrier);

    // V2 figuration under the stretto block (band-confined, eighth motion so
    // the bass keeps moving against the overlapped theme statements). The two
    // thematic voices (V0 leader, V1 follower) already sound above it.
    add_counterline(2, leader_bar, leader_bar + 3, 2);

    // Figuration fill before and after the stretto block, keeping the full
    // three-voice texture through the development: V0 and V1 eighth-note wave
    // counterlines in their disjoint bands over a V2 sustained chord-tone
    // support (a single-anchor-per-beat V1 degenerates into a two-pitch
    // pendulum once same-pitch quarters coalesce; the eighth wave walks the
    // scale between anchors like the stretto-window bass line). Lines are
    // built top-down so each lower line reads everything already sounding
    // above it from the registry and picks consonant, parallel-free tones
    // (long fills previously rested V1 entirely, thinning the fugue to two
    // voices between the exposition and the stretto).
    if (leader_bar > next_free_bar) {
      add_counterline(0, next_free_bar, leader_bar - 1, 2);
      add_counterline(1, next_free_bar, leader_bar - 1, 2);
      add_sustained_support(2, next_free_bar, leader_bar - 1);
    }
    if (leader_bar + 4 < cadence_start) {
      add_counterline(0, leader_bar + 4, cadence_start - 1, 2);
      add_counterline(1, leader_bar + 4, cadence_start - 1, 2);
      add_sustained_support(2, leader_bar + 4, cadence_start - 1);
    }
  } else if (cadence_start > next_free_bar) {
    // Short tail: the same three-layer fill up to the cadence.
    add_counterline(0, next_free_bar, cadence_start - 1, 2);
    add_counterline(1, next_free_bar, cadence_start - 1, 2);
    add_sustained_support(2, next_free_bar, cadence_start - 1);
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
  for (auto& chord : out.harmony.chords) {
    const int bar = static_cast<int>(chord.start_tick / kTicksPerBar);
    if (bar == cadence_start) {
      chord.root_pc = 7;  // V.
      chord.quality = ChordQuality::Major;
    } else if (bar == cadence_start + 1) {
      chord.root_pc = 0;  // I.
      chord.quality = ChordQuality::Major;
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
  // reads its slice (bars [free_bars, total)) by absolute bar index.
  const std::vector<ChordSpec> plan = buildChordPlan(total, mode, harm_idx);
  emitHarmony(out, plan, mode);

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
      if (kind == FreeBarKind::kGesture) {
        // V0 solo opening gesture; the bar's tail is silent and no layer enters.
        // In the octave cascade the gesture keeps the window's opening harmony
        // so bar 1 is an exact octave-lower restatement of bar 0 (BWV565).
        const int chord_bar = dramaticus_cascade ? win.first_bar : bar;
        const int octave_drop = (dramaticus_cascade && bar == 1) ? 1 : 0;
        appendGestureBar(section.notes, bar, plan[static_cast<std::size_t>(chord_bar)], mode,
                         kBandLo[0], kBandHi[0], octave_drop);
        prev_pitch = -1;
        continue;
      }
      if (kind == FreeBarKind::kUnisonGesture) {
        // Deliberate BWV565 unison rhetoric: the opening gesture stated low in
        // V0 and doubled exactly 12 below in V1. Both lines are Material, so the
        // validator's parallel-octave rules are skipped by design; the doubled
        // V1 statement is emitted as a verbatim voice-1 ToccataSection below.
        const std::size_t before = section.notes.size();
        appendGestureBar(section.notes, bar, plan[static_cast<std::size_t>(win.first_bar)], mode,
                         kBandLo[0], kBandHi[0], /*octave_drop=*/2);
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
        lp.punctuate = true;
        continue;
      }
      if (kind == FreeBarKind::kDim7Sweep) {
        // BWV565 leading-tone diminished-seventh roll (minor-mode Dramaticus).
        // The roll subdivision matches the neighbouring wave bars: sixteenths,
        // tightening to sixteenth triplets in the section's second half (the
        // same drive condition the wave bars use below). The roll is confined to
        // the V0 band so it sounds above the V1 punctuation and V2 pedal exactly
        // like a wave bar; the accompaniment layers are identical to a wave bar's
        // (V2 pedal + V1 head punctuation), so the dim7 rolls over the pedal like
        // the model piece.
        const bool tighten_sweep = bar >= free_bars / 2;
        appendDim7SweepBar(section.notes, bar, static_cast<int>(out.harmony.tonic_pc), kBandLo[0],
                           kBandHi[0], tighten_sweep);
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
      // The drive to the fugue tightens from sixteenths to triplet sixteenths:
      // in the section's last half the Dramaticus and Perpetuus wave bars
      // subdivide into sixteenth triplets.
      const bool tighten =
          (archetype == ToccataArchetype::Dramaticus || archetype == ToccataArchetype::Perpetuus) &&
          bar >= free_bars / 2;
      appendScalarWaveBar(section.notes, bar, plan[static_cast<std::size_t>(bar)], mode,
                          notes_per_beat, base, kBandHi[0], fig_offset, prev_pitch,
                          /*rotate_figures=*/true, /*triplet=*/tighten);
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
  appendFreeSectionLayers(asm_ctx, v0_free_notes, plan, mode, free_bars, layout, req,
                          /*v1_punct_dur=*/kTicksPerBeat * 2);

  // --- FUGUE TAIL (bars free_bars .. total-1). ---
  appendFugueTail(asm_ctx, free_bars, split.fugue_bars, plan, req);

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

  const std::vector<ChordSpec> plan = buildChordPlan(total, mode, harm_idx);
  emitHarmony(out, plan, mode);

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
        prev_pitch = -1;
        continue;
      }
      appendScalarWaveBar(section.notes, bar, plan[static_cast<std::size_t>(bar)], mode,
                          sp.notes_per_beat, base, base + 14, fig_offset, prev_pitch);
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
  appendFreeSectionLayers(asm_ctx, v0_free_notes, plan, mode, free_bars, layout, req,
                          /*v1_punct_dur=*/kTicksPerBeat);

  // --- FUGUE TAIL (bars free_bars .. total-1). ---
  appendFugueTail(asm_ctx, free_bars, split.fugue_bars, plan, req);

  return out;
}

}  // namespace bach::composer
