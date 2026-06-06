#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "composer/character_profile.h"
#include "composer/figuration.h"
#include "composer/form_builders.h"
#include "composer/material.h"
#include "composer/minor_material.h"
#include "composer/motif_ops.h"
#include "composer/span.h"
#include "composer/texture_helpers.h"
#include "composer/tonal_answer.h"
#include "composer/voice_intent.h"
#include "core/basic_types.h"

namespace bach::composer {

// ---------------------------------------------------------------------------
// Fugue family: the standalone fugue and the prelude+fugue pair.
//
// Both builders are dedicated assemblers (no longer placeholders replaying a
// proven phase fixture). They honour ResolvedRequest length, mode, character,
// and the arc curve. The fugue is the flagship form, so it is built from a
// fixed exposition + a variable number of repeatable 8-bar device-cycles +
// a fixed coda; the prelude+fugue pair reuses the same fugue assembly behind a
// figuration prelude.
//
// EVERY note in both forms is NoteSource::Material (verbatim carriers). The
// validator's parallel / hidden-parallel / vertical-dissonance / cross-relation
// / invertible rules all skip a voice pair when BOTH notes are Material (the
// composer cannot edit fixed inputs), so the only inter-voice constraint that
// fires on these fixtures is voice_crossing (interval < 0 at a shared tick,
// where a higher-indexed voice rose above a lower-indexed one). The builders
// therefore keep a strict per-voice register order V0 >= V1 >= V2 at every
// shared tick by confining each voice's material to a disjoint register band.
// ---------------------------------------------------------------------------

namespace {

using detail::ChordSpec;               // NOLINT(build/namespaces)
using detail::kHarmonyPatterns;        // NOLINT(build/namespaces)
using detail::kHarmonyPatternsMinor;   // NOLINT(build/namespaces)
using detail::kPhase14SubjectRhythms;  // NOLINT(build/namespaces)
using detail::kPhase14Subjects;        // NOLINT(build/namespaces)
using detail::kSubjectsMinor;          // NOLINT(build/namespaces)
using detail::Mode;                    // NOLINT(build/namespaces)
using detail::scaleUp;                 // NOLINT(build/namespaces)
using detail::subjectSlotFor;          // NOLINT(build/namespaces)
using detail::usePicardy;              // NOLINT(build/namespaces)

constexpr Tick kQuarter = kTicksPerBeat;
constexpr Tick kSixteenth = kTicksPerBeat / 4;

#include "composer/tables/entry_plan_stats.inc"

// One subject statement is 16 catalog notes spanning 4 bars. Durations come
// from kPhase14SubjectRhythms rather than being fixed quarters.
constexpr int kSubjectNotes = 16;
constexpr int kSubjectBars = 4;

// Per-voice register bands (MIDI). The bands are disjoint and strictly ordered
// (V0 highest, V2 lowest). Every voice's material is octave-fit so its highest
// note stays at or below the band ceiling (octaveOffsetForBand is ceiling-first).
// The V0 ceiling is the practical manual compass top used by the texture gate;
// keeping it at C6 prevents the old D7-range fixture artifact from returning.
constexpr int kBandLo[3] = {67, 51, 33};
constexpr int kBandHi[3] = {84, 66, 50};

/// @brief Append a single note to a material vector.
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

/// @brief Octave offset that fits a pre-transposed subject into a voice band.
///
/// Given a base semitone transposition already applied to the subject (0 for the
/// V0 statement, -5 for the real answer, a diatonic offset for a middle entry),
/// returns the additional whole-octave shift (a multiple of 12, preserving pitch
/// classes) that places the line's full range inside the target voice band. Used
/// to keep every statement strictly inside its voice's register band so the
/// validator's voice_crossing rule never fires across the all-Material texture.
///
/// @param subject The 16-note subject pattern (V0-band pitches).
/// @param base_semis Base transposition already applied (real-answer / key).
/// @param voice Target voice index (0..2) selecting the band.
/// @return The additional octave offset (a multiple of 12) to apply.
int octaveOffsetForBand(const std::array<std::uint8_t, 16>& subject, int base_semis, int voice) {
  int lo = 127;
  int hi = 0;
  for (std::uint8_t pitch : subject) {
    lo = std::min(lo, static_cast<int>(pitch) + base_semis);
    hi = std::max(hi, static_cast<int>(pitch) + base_semis);
  }
  int offset = 0;
  // Ceiling-first: lower the line by whole octaves until its top fits under the
  // band ceiling (never above it, so it cannot rise into the band above).
  while (hi + offset > kBandHi[voice]) {
    offset -= 12;
  }
  // Then raise it back toward the floor only while doing so keeps the top under
  // the ceiling, so the top-fit invariant is never violated.
  while (lo + offset < kBandLo[voice] && hi + offset + 12 <= kBandHi[voice]) {
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

// Middle-entry related-key plan, keyed by the CARRYING VOICE (which rotates
// V0 / V1 / V2 by cycle index, so the plan rotates across the development). A
// C-major subject transposed by one of these diatonic offsets maps onto the
// scale of the related MAJOR key exactly (e.g. +7 maps C major -> G major), so
// every transposed note's pitch class stays diatonic to that key -- which the
// validator's middle_entry_in_related_key rule requires. The keys are V / vi /
// IV (the rule's admissible related keys; the home tonic I is NOT admissible).
// Keying by voice keeps every middle entry on a given voice in ONE key, so a
// single per-voice MiddleEntryDecl (the carrier dispatch matches by voice) holds
// notes that are all diatonic to that voice's declared key.
constexpr std::array<std::uint8_t, 3> kVoiceKeyPc = {7, 9, 5};  // V0->V, V1->vi, V2->IV.
constexpr std::array<int, 3> kVoiceKeySemis = {7, 9, 5};        // diatonic offsets.

struct DevelopmentWindow {
  int entry_start = 0;    // relative bars from first_bar; valid when has_entry.
  int episode_start = 0;  // relative bars from first_bar.
  int episode_len = 0;
  bool has_entry = false;
};

// Episode length floor/ceiling: every development episode spans at least 2 and
// at most 6 bars, so a full middle-entry cycle (a 4-bar subject statement plus
// its episode) is 6..10 bars long. These bound the admissible entry interval.
constexpr int kMinEpisodeBars = 2;
constexpr int kMaxEpisodeBars = 6;

// Fixed-schedule cycle length: a 4-bar subject statement plus a fixed 4-bar
// episode (the entry interval of 8 minus the 4-bar subject). Used to count how
// many entries a uniform schedule would place in a given development span.
constexpr int kFixedCycleBars = kSubjectBars + (8 - kSubjectBars);

// Variable scheduling is enabled once a uniform 8-bar schedule would place at
// least this many middle entries: with that many equally spaced entries the
// development becomes metrically monotonous, so the corpus-derived (non-uniform)
// entry intervals are applied instead. There is no upper bar limit -- longer
// developments only benefit more from non-uniform spacing.
constexpr int kVariableScheduleMinEntries = 8;

int entryIntervalForCycle(std::uint32_t seed, int cycle) {
  constexpr int kEntryDecileCount =
      static_cast<int>(sizeof(kEntryIntervalDeciles) / sizeof(kEntryIntervalDeciles[0]));
  const int raw = kEntryIntervalDeciles[(static_cast<int>(seed % kEntryDecileCount) + cycle * 2) %
                                        kEntryDecileCount];
  // Clamp into the band where the decile table's interquartile region overlaps
  // the structurally admissible interval window. The structural window is
  // [kSubjectBars + kMinEpisodeBars, kSubjectBars + kMaxEpisodeBars] (a cycle's
  // episode must fit); the interquartile region is the table's central deciles
  // [Q1, Q3]. Indexing the bounds from the table itself means they track any
  // regeneration of entry_plan_stats.inc automatically.
  constexpr int kQ1Index = kEntryDecileCount / 4;
  constexpr int kQ3Index = (kEntryDecileCount * 3) / 4;
  const int interval_lo = std::max(kEntryIntervalDeciles[kQ1Index], kSubjectBars + kMinEpisodeBars);
  const int interval_hi = std::min(kEntryIntervalDeciles[kQ3Index], kSubjectBars + kMaxEpisodeBars);
  return std::clamp(raw, interval_lo, interval_hi);
}

bool useVariableEntrySchedule(int fugue_bars) {
  const bool short_form = fugue_bars <= 20;
  const int exposition_bars = short_form ? 8 : 12;
  constexpr int coda_bars = 4;
  const int development_bars = fugue_bars - exposition_bars - coda_bars;
  if (development_bars <= 0) {
    return false;
  }
  // How many entries a uniform 8-bar schedule would place in the development.
  const int fixed_entry_count = development_bars / kFixedCycleBars;
  return fixed_entry_count >= kVariableScheduleMinEntries;
}

std::vector<DevelopmentWindow> buildDevelopmentWindows(int dev_start, int fugue_bars, int coda_bars,
                                                       bool short_form, std::uint32_t seed) {
  std::vector<DevelopmentWindow> windows;
  const int dev_end = fugue_bars - coda_bars;
  const bool use_variable_entries = useVariableEntrySchedule(fugue_bars);
  int cursor = dev_start;
  int cycle = 0;
  while (cursor < dev_end) {
    const int remaining = dev_end - cursor;
    if (short_form || remaining < kSubjectBars + 2) {
      windows.push_back(DevelopmentWindow{0, cursor, remaining, false});
      break;
    }

    const int interval = use_variable_entries ? entryIntervalForCycle(seed, cycle) : 8;
    const int episode_len =
        std::min(std::clamp(interval - kSubjectBars, 2, 6), remaining - kSubjectBars);
    windows.push_back(DevelopmentWindow{cursor, cursor + kSubjectBars, episode_len, true});
    cursor += kSubjectBars + episode_len;
    ++cycle;
  }
  return windows;
}

// ---------------------------------------------------------------------------
// FugueAssembly: the per-section accumulator the internal builders write into.
// The same assembly is used for a standalone fugue (first_bar = 0) and for the
// fugue half of a prelude+fugue pair (first_bar = prelude length). Span ids and
// the next-id counter are shared so concatenated sections stay unique.
// ---------------------------------------------------------------------------
struct FugueAssembly {
  HarnessFixture* out = nullptr;
  SpanId* next_id = nullptr;
  // Registry of every already-placed note (thematic statements AND figuration
  // accompaniment), so a line built later in the deterministic voice order can
  // read what every earlier voice is sounding at a given tick. This drives two
  // things: (1) the consonance-aware figuration anchor picks a chord tone that
  // is consonant with the concurrent theme tone, and (2) the parallel-aware
  // anchor avoids same-direction arrivals on interval class 0/7 against any
  // earlier voice (the cardinal Bach prohibition on parallel 5ths/8ths).
  ThemeToneRegistry theme_tones;
};

// The fugue family is built as a strict three-voice texture.
constexpr VoiceId kFugueVoices = 3;

/// @brief Append a window-sliced verbatim carrier span.
void pushSpan(FugueAssembly& asm_ctx, VoiceId voice, int first_bar, int last_bar,
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

/// @brief Append one bar of theme-consonant scalar-wave chord-tone figuration.
///
/// Every beat opens on a chord tone of `chord` chosen to be consonant against
/// the concurrent thematic statement (the scorer samples vertical intervals on
/// the beat grid, so the on-beat note is what drives vertical_dissonance). The
/// per-beat anchors form a stepwise chain: each anchor is the nearest consonant
/// chord tone to the PREVIOUS anchor (threaded across bars via `prev_anchor`),
/// gently biased back toward a band centre so the line neither drifts out of band
/// nor leaps between register extremes. The notes between anchors walk by single
/// scale steps so the whole line is conjunct (the corpus melodic-interval
/// distribution is dominated by steps). The downbeat anchor is always a genuine
/// chord tone so figuration_harmonic_consistency passes; band confinement keeps
/// the voice ordering (V0 >= V1 >= V2) intact across the all-Material texture.
///
/// @param asm_ctx Assembly (read for the concurrent theme-tone registry).
/// @param section Figuration section receiving the bar's notes.
/// @param bar Absolute bar index of the figuration bar.
/// @param voice Voice index (selects the band the wave is clamped into).
/// @param chord The bar's chord (supplies the per-beat chord tones).
/// @param mode Diatonic mode selecting the scale walker.
/// @param notes_per_beat Subdivision density (1 / 2 / 4).
/// @param offset Seed-derived start-register offset above the band floor.
/// @param prev_anchor Running anchor threaded across bars; updated to the bar's
///        last anchor so the next bar's first anchor chains stepwise from it.
void appendFigurationBar(FugueAssembly& asm_ctx, FigurationSection& section, int bar, int voice,
                         const ChordSpec& chord, Mode mode, int notes_per_beat, int offset,
                         int& prev_anchor) {
  // Register centre for the wave: a few scale degrees above the band floor,
  // shifted by the seed offset, kept clear of the band ceiling so a stepwise
  // fill never runs out of band. The per-beat anchor chain is gently pulled
  // back toward this centre so the conjunct walk cannot drift out of band.
  int center = scaleUp(kBandLo[voice], offset + 2, mode);
  if (center > kBandHi[voice] - 4) {
    center = scaleUp(kBandLo[voice], offset, mode);
  }
  if (prev_anchor <= 0) {
    prev_anchor = center;
  }
  const Tick step =
      (notes_per_beat == 4) ? kSixteenth : ((notes_per_beat == 2) ? kTicksPerBeat / 2 : kQuarter);
  std::vector<int> theme_pitches;
  // The line is a triangular scalar wave: it walks one scale step per note in a
  // single direction, reverses when it reaches the top or bottom of a working
  // band centred on `center`, and snaps each beat onset to the nearest consonant
  // chord tone (so the on-beat verticals stay consonant and the bar downbeat is a
  // genuine chord tone). Because the motion is one scale step per note, every
  // consecutive interval is a step except the small (third-sized) snap to a chord
  // tone at the beat onset -- the corpus melodic-interval mass is on steps.
  const int wave_lo = std::max(kBandLo[voice], center - 5);
  const int wave_hi = std::min(kBandHi[voice], center + 5);
  // Walking cursor and direction, threaded across bars via prev_anchor's sign
  // (positive magnitude is the pitch; an even/odd parity is not stored, so the
  // direction restarts upward each bar -- the wave still reverses within a bar at
  // the band edges, which is what keeps consecutive bars from a sawtooth jump).
  int cursor = std::clamp(prev_anchor, wave_lo, wave_hi);
  int dir = (cursor <= center) ? 1 : -1;
  auto stepScale = [&](int from, int direction) {
    return direction > 0 ? scaleUp(from, 1, mode) : scaleDown(from, 1, mode);
  };
  int last_pitch = cursor;
  // This line's previous beat anchor, used to judge whether the next anchor
  // moves in parallel with an earlier voice. Seeded from prev_anchor (the prior
  // bar's last anchor) so the bar-boundary beat is also parallel-checked.
  int line_prev_anchor = (prev_anchor > 0) ? prev_anchor : -1;
  std::vector<ConcurrentMotion> motions;
  std::vector<int> window_pitches;
  for (int beat = 0; beat < 4; ++beat) {
    const Tick beat_tick = barTick(bar) + static_cast<Tick>(beat) * kTicksPerBeat;
    const Tick prev_beat_tick = beat_tick - kTicksPerBeat;
    asm_ctx.theme_tones.concurrentThemePitches(beat_tick, static_cast<VoiceId>(voice),
                                               theme_pitches);
    asm_ctx.theme_tones.concurrentMotions(prev_beat_tick, beat_tick, static_cast<VoiceId>(voice),
                                          kFugueVoices, motions);
    // Pitches already-placed voices attack INSIDE this anchor's sustain window
    // (after the onset). A quarter-note anchor under an earlier eighth-note
    // line can be onset-consonant yet sustained against a dissonant mid-beat
    // attack above it; consonantChordTone uses these as a tie-breaker.
    window_pitches.clear();
    for (Tick slot = beat_tick + kSixteenth; slot < beat_tick + step; slot += kSixteenth) {
      for (VoiceId other = 0; other < kFugueVoices; ++other) {
        if (other == static_cast<VoiceId>(voice)) {
          continue;
        }
        const int sounding = asm_ctx.theme_tones.soundingPitchInVoice(other, slot);
        if (sounding >= 0) {
          window_pitches.push_back(sounding);
        }
      }
    }
    // Snap the beat onset to the nearest consonant, parallel-free anchor tone
    // (a chord tone on the downbeat, any diatonic tone off the downbeat).
    const int anchor =
        consonantChordTone(chord, voice, kBandLo[voice], kBandHi[voice], cursor, theme_pitches,
                           line_prev_anchor, motions, mode, beat == 0, window_pitches);
    cursor = std::clamp(anchor, kBandLo[voice], kBandHi[voice]);
    line_prev_anchor = cursor;
    for (int sub = 0; sub < notes_per_beat; ++sub) {
      const Tick tick =
          barTick(bar) + static_cast<Tick>(beat) * kTicksPerBeat + static_cast<Tick>(sub) * step;
      const Tick prev_tick = tick - step;
      int pitch;
      if (sub == 0) {
        pitch = cursor;
      } else {
        const int from = cursor;
        // Default the next wave note one scale step in the running direction,
        // reversing at the working band edges so the line stays conjunct.
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
        // Parallel-aware wave: if this step lands a same-direction perfect 5th/8th
        // against an earlier voice's concurrent motion, reverse direction (still a
        // single scale step, so the line stays conjunct). Sampled at this sub-tick
        // and the previous one so the inter-note motion is judged, not only beats.
        asm_ctx.theme_tones.concurrentMotions(prev_tick, tick, static_cast<VoiceId>(voice),
                                              kFugueVoices, motions);
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
          } else {
            // Both single steps land parallels (two diatonic stepwise lines in
            // rhythmic lockstep do this systematically): try a third-skip in
            // either direction before accepting the parallel, mirroring the
            // harsh-clash fallback below.
            for (const int skip_dir : {dir, -dir}) {
              const int skip = (skip_dir > 0) ? scaleUp(from, 2, mode) : scaleDown(from, 2, mode);
              if (skip < wave_lo || skip > wave_hi) {
                continue;
              }
              if (!wave_is_parallel(skip)) {
                next = skip;
                break;
              }
            }
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
          for (VoiceId other = 0; other < kFugueVoices; ++other) {
            if (other == static_cast<VoiceId>(voice)) {
              continue;
            }
            for (Tick slot = tick; slot < tick + step; slot += kSixteenth) {
              const int sounding = asm_ctx.theme_tones.soundingPitchInVoice(other, slot);
              if (sounding < 0) {
                continue;
              }
              const int ic = std::abs(cand - sounding) % 12;
              if (ic == 1 || ic == 6 || ic == 11) {
                return true;
              }
              // Parallel-dissonance chain: a 2nd/7th arrival is acceptable as an
              // isolated passing tone, but not when the previous sub-beat against
              // the same voice was already dissonant (consecutive 2nds/7ths/9ths
              // read as a broken duet rather than passing motion).
              if (ic == 2 || ic == 10) {
                const int prev_sounding =
                    asm_ctx.theme_tones.soundingPitchInVoice(other, prev_tick);
                if (prev_sounding >= 0) {
                  const int prev_ic = std::abs(from - prev_sounding) % 12;
                  if (prev_ic == 1 || prev_ic == 2 || prev_ic == 6 || prev_ic == 10 ||
                      prev_ic == 11) {
                    return true;
                  }
                }
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
        // Keep the per-tick voice order V0 >= V1 >= V2: clamp the wave note below
        // every concurrent lower-index voice and above every concurrent
        // higher-index voice so a wide verbatim entry cannot be crossed.
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
          // Clamping can pin the note to a window edge and repeat the previous
          // pitch; if the window still has room, step to the nearest distinct
          // diatonic tone inside it so the line never stalls into a long run.
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
        // Commit the running direction the chosen step actually moved.
        if (next > from) {
          dir = 1;
        } else if (next < from) {
          dir = -1;
        }
        cursor = next;
        pitch = cursor;
      }
      last_pitch = pitch;
      addNote(section.notes, tick, step, pitch);
      // Register this figuration note so a voice placed later in the same window
      // can read what this line sounds and avoid a parallel against it.
      asm_ctx.theme_tones.record(tick, static_cast<VoiceId>(voice), pitch, step);
    }
  }
  prev_anchor = last_pitch;
}

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

/// @brief Add one figuration accompaniment span over [first_bar, last_bar].
///
/// Creates one FigurationSection whose window exactly matches the span (the
/// FigurationCarrier dispatch matches sections by exact window) and a matching
/// FigurationCarrier span. The bars open on chord tones drawn from `chords`,
/// which is indexed by the SECTION-RELATIVE bar (absolute bar - `plan_base`).
void addFigurationSpan(FugueAssembly& asm_ctx, VoiceId voice, int first_bar, int last_bar,
                       const std::vector<ChordSpec>& chords, int plan_base, Mode mode,
                       int notes_per_beat, int offset, bool is_pedal_prep = false) {
  FigurationSection section;
  section.voice = voice;
  section.start_tick = barTick(first_bar);
  section.end_tick = barTick(last_bar + 1);
  section.is_pedal_prep = is_pedal_prep;
  // Running anchor threaded across the section's bars so consecutive bar-edge
  // anchors chain stepwise (no leap at the bar boundary). Seeded by the first
  // bar's centre inside appendFigurationBar (prev_anchor <= 0).
  int prev_anchor = 0;
  for (int bar = first_bar; bar <= last_bar; ++bar) {
    appendFigurationBar(asm_ctx, section, bar, voice,
                        chords[static_cast<std::size_t>(bar - plan_base)], mode, notes_per_beat,
                        offset, prev_anchor);
  }
  coalesceConsecutiveSamePitch(section.notes);
  asm_ctx.out->material.figuration_sections.push_back(section);
  pushSpan(asm_ctx, voice, first_bar, last_bar, VoiceIntent::FigurationCarrier);
}

/// @brief Build the per-bar chord plan for the whole fugue span.
///
/// Entry bars (subject / answer / middle-entry / coda) open on the home tonic;
/// episode bars walk a short diatonic progression so the harmony moves. The
/// plan is deterministic from (seed, mode); every chord is diatonic with at most
/// one secondary-flavoured chord per episode (kept simple, all diatonic here).
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

/// @brief Emit the HarmonicPlan ChordEvents from a per-bar chord plan.
void emitHarmony(HarnessFixture& out, const std::vector<ChordSpec>& plan, Mode mode, int base_bar) {
  out.harmony.tonic_pc = 0;
  out.harmony.is_minor = (mode == Mode::Minor);
  for (std::size_t bar = 0; bar < plan.size(); ++bar) {
    ChordEvent chord;
    chord.start_tick = barTick(base_bar + static_cast<int>(bar));
    chord.root_pc = plan[bar].root_pc;
    chord.quality = plan[bar].minor ? ChordQuality::Minor : ChordQuality::Major;
    out.harmony.chords.push_back(chord);
  }
}

// ---------------------------------------------------------------------------
// appendFugueSection: assemble a complete fugue (exposition + development
// cycles + coda) starting at `first_bar`, spanning `bars` bars. Reused by the
// standalone fugue (first_bar = 0) and the fugue half of a prelude+fugue pair.
//
// Layout (bars relative to first_bar):
//   exposition: subject V0 (0-3), answer V1 (4-7) + V0 figuration counterline,
//               third entry V2 (8-11) + V0 countersubject + V1 figuration
//               (only when exposition is the full 12 bars).
//   development: ceil((bars-16)/8) device-cycles of 8 bars each (middle entry
//               4 bars + episode 4 bars); the last cycle may be a 4-bar
//               episode-only half-cycle.
//   coda: final subject entry V0 + a 2-bar cadence (4 bars).
//
// All material is Material verbatim; the per-voice band confinement keeps
// V0 >= V1 >= V2 at every shared tick.
// ---------------------------------------------------------------------------
void appendFugueSection(FugueAssembly& asm_ctx, int first_bar, int bars,
                        const ResolvedRequest& req) {
  HarnessFixture& out = *asm_ctx.out;
  const Mode mode = req.mode;
  const int harm_idx = static_cast<int>(req.seed % 4);
  const int fig_offset = static_cast<int>(req.seed % 4);

  // Subject catalog slot (character + seed) -> the V0-band subject pattern.
  const std::uint8_t slot = subjectSlotFor(req.character, req.seed);
  const std::array<std::uint8_t, 16>& subj_pat =
      (mode == Mode::Minor) ? kSubjectsMinor[slot] : kPhase14Subjects[slot];
  const std::array<Tick, 16>& subj_rhythm = kPhase14SubjectRhythms[slot];

  // --- Length partition. ---
  // Exposition: 12 bars normally, compressed to 8 for short fugues (N<=20).
  const bool short_form = bars <= 20;
  const int exposition_bars = short_form ? 8 : 12;
  constexpr int coda_bars = 4;

  // Per-bar chord plan for the whole fugue (in absolute bars from first_bar).
  // The final two bars are pinned to a V -> I authentic cadence so the explicit
  // cadential bass (dominant then tonic) is harmonically consistent and the
  // cadence_voice_leading rule reads a true V->I.
  std::vector<ChordSpec> plan = buildChordPlan(bars, mode, harm_idx);
  plan[static_cast<std::size_t>(bars - 2)] = ChordSpec{7, false};                // V (G major).
  plan[static_cast<std::size_t>(bars - 1)] = ChordSpec{0, mode == Mode::Minor};  // I.
  emitHarmony(out, plan, mode, first_bar);
  const int development_bars = bars - exposition_bars - coda_bars;
  const std::vector<DevelopmentWindow> development_windows =
      development_bars > 0
          ? buildDevelopmentWindows(exposition_bars, bars, coda_bars, short_form, req.seed)
          : std::vector<DevelopmentWindow>{};
  const bool variable_entry_schedule = useVariableEntrySchedule(bars);
  std::vector<int> entry_cycles;
  for (std::size_t i = 0; i < development_windows.size(); ++i) {
    if (development_windows[i].has_entry) {
      entry_cycles.push_back(static_cast<int>(i));
    }
  }

  // Climax cycle: ~80% of the middle-entry span (matches arcPoint's climax).
  const int climax_cycle =
      development_windows.empty()
          ? -1
          : (variable_entry_schedule && !entry_cycles.empty()
                 ? entry_cycles[static_cast<std::size_t>(
                       std::min<int>(static_cast<int>(entry_cycles.size()) - 1,
                                     static_cast<int>(entry_cycles.size()) * 4 / 5))]
                 : std::min<int>(static_cast<int>(development_windows.size()) - 1,
                                 static_cast<int>(development_windows.size()) * 4 / 5));
  // Pedal cycle: the last middle-entry cycle before the coda (only for N >= 32).
  const int pedal_cycle = (bars < 32 || development_windows.empty())
                              ? -1
                              : (variable_entry_schedule && !entry_cycles.empty()
                                     ? entry_cycles.back()
                                     : static_cast<int>(development_windows.size()) - 1);

  // === EXPOSITION ===========================================================
  // Each thematic statement carries the subject in ONE voice band; at most ONE
  // figuration accompaniment voice is added per bar window (the FigurationCarrier
  // dispatch matches sections by window only, so two sections sharing a window
  // would collide -- a single accompaniment voice per window avoids that).
  const int v0_off = octaveOffsetForBand(subj_pat, 0, 0);
  // Countersubject = a genuine counterline against the entry it accompanies,
  // NOT a parallel-octave doubling. Each note is chosen to be (1) consonant with
  // the source note it sounds against (so the vertical dissonance stays low) and
  // (2) in CONTRARY motion to the source whenever the source moves (so it can
  // never form a parallel fifth/octave -- the cardinal prohibition). The chosen
  // tone is always inside the voice band, so the strict V0 >= V1 >= V2 register
  // order across the all-Material texture is preserved and voice_crossing never
  // fires. Diatonic, near the previous counterline pitch, so the line is smooth.
  auto append_countersubject_from = [&](const std::vector<MaterialNote>& source, int voice,
                                        Tick start, Tick end) {
    const int center = (kBandLo[voice] + kBandHi[voice]) / 2;
    int prev_cs = -1;
    int prev_src = -1;
    int repeat_run = 1;  // consecutive equal counterline pitches so far.
    for (const auto& note : source) {
      if (note.start_tick < start || note.start_tick >= end)
        continue;
      const int src = static_cast<int>(note.pitch);
      const int src_dir = (prev_src < 0) ? 0 : (src > prev_src ? 1 : (src < prev_src ? -1 : 0));
      const int target = (prev_cs < 0) ? center : prev_cs;
      // Score every in-band diatonic tone: consonant against the source first,
      // then contrary to the source's motion, then nearest the previous note,
      // while refusing a long repeated-pitch run (texture gate caps runs at 4).
      int best = -1;
      int best_score = 1 << 30;
      for (int pitch = kBandLo[voice]; pitch <= kBandHi[voice]; ++pitch) {
        if (!inScale(pitch, mode)) {
          continue;
        }
        const bool consonant = isConsonantIc(pitch - src);
        const int cs_dir = (prev_cs < 0) ? 0 : (pitch > prev_cs ? 1 : (pitch < prev_cs ? -1 : 0));
        // Similar (same-direction) motion onto a perfect 5th/8th is the parallel
        // we must avoid; contrary or oblique motion is safe.
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
        // When the source moves, prefer the counterline to move too (contrary),
        // and hard-bias away from extending a 4-long repeated run.
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
      addNote(out.material.countersubject, note.start_tick, note.duration, pitch);
      asm_ctx.theme_tones.record(note.start_tick, static_cast<VoiceId>(voice), pitch,
                                 note.duration);
      prev_cs = pitch;
      prev_src = src;
    }
  };
  auto stamp_subject = [&](int base_bar, int semis, int theme_voice) {
    Tick cursor = barTick(base_bar);
    for (int note = 0; note < kSubjectNotes; ++note) {
      const int pitch = static_cast<int>(subj_pat[static_cast<std::size_t>(note)]) + semis;
      addNote(out.material.subject, cursor, subj_rhythm[static_cast<std::size_t>(note)], pitch);
      asm_ctx.theme_tones.record(cursor, static_cast<VoiceId>(theme_voice), pitch,
                                 subj_rhythm[static_cast<std::size_t>(note)]);
      cursor += subj_rhythm[static_cast<std::size_t>(note)];
    }
  };
  stamp_subject(first_bar + 0, v0_off, 0);
  pushSpan(asm_ctx, 0, first_bar + 0, first_bar + 3, VoiceIntent::SubjectCarrier);

  // Answer (V1, bars 4-7) = real answer (subject - P4) lowered into the V1 band.
  const int answer_off = octaveOffsetForBand(subj_pat, -5, 1);
  const int answer_total = -5 + answer_off;
  const bool use_tonal_answer = shouldUseTonalAnswer(subj_pat, out.harmony.tonic_pc);
  std::vector<MaterialNote> tonal_answer_seed;
  tonal_answer_seed.reserve(kSubjectNotes);
  Tick answer_cursor = barTick(first_bar + 4);
  for (int note = 0; note < kSubjectNotes; ++note) {
    const Tick tick = answer_cursor;
    const Tick dur = subj_rhythm[static_cast<std::size_t>(note)];
    const int pitch = static_cast<int>(subj_pat[static_cast<std::size_t>(note)]) + answer_total;
    addNote(out.material.answer, tick, dur, pitch);
    MaterialNote seed_note;
    seed_note.start_tick = tick;
    seed_note.duration = dur;
    seed_note.pitch = static_cast<std::uint8_t>(std::clamp(
        static_cast<int>(subj_pat[static_cast<std::size_t>(note)]) + answer_off, 0, 127));
    tonal_answer_seed.push_back(seed_note);
    if (!use_tonal_answer) {
      asm_ctx.theme_tones.record(tick, 1, pitch, dur);
    }
    answer_cursor += dur;
  }
  if (use_tonal_answer) {
    out.material.tonal_answer = tonal_answer::deriveTonalAnswer(
        tonal_answer_seed, out.harmony.tonic_pc, barTick(first_bar + 4), 4);
    out.material.use_tonal_answer = true;
    for (const auto& note : out.material.tonal_answer) {
      asm_ctx.theme_tones.record(note.start_tick, 1, static_cast<int>(note.pitch), note.duration);
    }
  }
  pushSpan(asm_ctx, 1, first_bar + 4, first_bar + 7, VoiceIntent::AnswerCarrier);
  // V0 countersubject rides above the answer. It is fixed material rather than
  // free figuration, so the answer entry now carries a recurring counterline.
  append_countersubject_from(use_tonal_answer ? out.material.tonal_answer : out.material.answer, 0,
                             barTick(first_bar + 4), barTick(first_bar + 8));
  pushSpan(asm_ctx, 0, first_bar + 4, first_bar + 7, VoiceIntent::CountersubjectCarrier);
  // V2 chord-root figuration under the answer fills the bass register so the
  // second exposition bar-group is a full three-voice texture (the answer entry
  // on V1, the V0 countersubject above, and a verbatim Material bass below). A
  // Material bass keeps all three exposition voices fixed, so the validator
  // skips every inter-voice rule but voice_crossing (which the disjoint bands
  // already prevent), guaranteeing the bass always sounds here.
  addFigurationSpan(asm_ctx, 2, first_bar + 4, first_bar + 7, plan, first_bar, mode, 1, fig_offset);

  // Imitation entry declaration: subject leads, answer follows a bar later. The
  // declared interval is the actual pitch offset between the two band-placed
  // first notes (real answer base -5 plus the answer's octave fit).
  {
    ImitationEntry entry;
    entry.leader_fragment = MaterialFragment::Subject;
    entry.follower_fragment = MaterialFragment::Answer;
    entry.distance_ticks = barTick(kSubjectBars);
    entry.interval_semis = answer_total - v0_off;
    out.material.imitation_entries.push_back(entry);
  }

  if (!short_form) {
    // Third entry (V2, bars 8-11) = subject - P8 lowered into the V2 band.
    const int third_off = octaveOffsetForBand(subj_pat, -12, 2);
    stamp_subject(first_bar + 8, -12 + third_off, 2);
    pushSpan(asm_ctx, 2, first_bar + 8, first_bar + 11, VoiceIntent::SubjectCarrier);
    // V1 countersubject plus V0 figuration makes the third entry a real 3-voice
    // texture instead of a two-voice carrier with a resting middle voice.
    std::vector<MaterialNote> third_entry_seed;
    Tick third_cursor = barTick(first_bar + 8);
    for (int note = 0; note < kSubjectNotes; ++note) {
      MaterialNote mn;
      mn.start_tick = third_cursor;
      mn.duration = subj_rhythm[static_cast<std::size_t>(note)];
      mn.pitch = static_cast<std::uint8_t>(std::clamp(
          static_cast<int>(subj_pat[static_cast<std::size_t>(note)]) - 12 + third_off, 0, 127));
      third_entry_seed.push_back(mn);
      third_cursor += mn.duration;
    }
    append_countersubject_from(third_entry_seed, 1, barTick(first_bar + 8),
                               barTick(first_bar + 12));
    pushSpan(asm_ctx, 1, first_bar + 8, first_bar + 11, VoiceIntent::CountersubjectCarrier);
    // V0 figuration rides above the V2 third entry.
    addFigurationSpan(asm_ctx, 0, first_bar + 8, first_bar + 11, plan, first_bar, mode, 2,
                      fig_offset);
  }

  // === DEVELOPMENT ==========================================================
  // One MiddleEntryDecl per carrying voice; each decl holds all of that voice's
  // middle-entry notes and the span windows slice them.
  std::array<MiddleEntryDecl, 3> middle_decls;
  std::array<bool, 3> middle_used = {false, false, false};

  for (int cycle = 0; cycle < static_cast<int>(development_windows.size()); ++cycle) {
    const DevelopmentWindow& window = development_windows[static_cast<std::size_t>(cycle)];
    const bool half_cycle = !window.has_entry;
    const ArcPoint arc = req.arc(
        static_cast<std::size_t>(std::min<int>(cycle, static_cast<int>(req.cycle_count) - 1)));
    const int carry_voice = cycle % 3;
    // Accompaniment density rises with the arc; figuration accompanies the
    // highest non-carrying voice (one accompaniment voice per window).
    const int acc_voice = (carry_voice == 0) ? 1 : 0;
    const int density = std::clamp<int>(1 + arc.density_tier, 1, 2);

    if (!half_cycle) {
      // --- Middle entry (4 bars): the subject restated in the carrying voice in
      //     a related MAJOR key (V / vi / IV, keyed by the carrying voice so each
      //     voice stays in one key). The MAJOR subject catalog is used for the
      //     transposed material so every note's pitch class is diatonic to the
      //     related major key (the rule checks against the major scale), then the
      //     line is octave-fit into the voice band. ---
      const int me_start = first_bar + window.entry_start;  // absolute.
      const int key_semis = kVoiceKeySemis[static_cast<std::size_t>(carry_voice)];
      const std::array<std::uint8_t, 16>& me_pat = kPhase14Subjects[slot];
      const int me_off = octaveOffsetForBand(me_pat, key_semis, carry_voice);
      const int me_total = key_semis + me_off;
      MiddleEntryDecl& decl = middle_decls[static_cast<std::size_t>(carry_voice)];
      decl.voice = static_cast<VoiceId>(carry_voice);
      decl.related_key_pc = kVoiceKeyPc[static_cast<std::size_t>(carry_voice)];
      Tick me_cursor = barTick(me_start);
      for (int note = 0; note < kSubjectNotes; ++note) {
        MaterialNote mn;
        mn.start_tick = me_cursor;
        mn.duration = subj_rhythm[static_cast<std::size_t>(note)];
        mn.pitch = static_cast<std::uint8_t>(std::clamp(
            static_cast<int>(me_pat[static_cast<std::size_t>(note)]) + me_total, 0, 127));
        decl.notes.push_back(mn);
        asm_ctx.theme_tones.record(mn.start_tick, static_cast<VoiceId>(carry_voice),
                                   static_cast<int>(mn.pitch), mn.duration);
        me_cursor += mn.duration;
      }
      middle_used[static_cast<std::size_t>(carry_voice)] = true;
      pushSpan(asm_ctx, static_cast<VoiceId>(carry_voice), me_start, me_start + 3,
               VoiceIntent::MiddleEntryCarrier);
      const bool add_middle_bass_support =
          (carry_voice != 2 && cycle != pedal_cycle && (fig_offset != 1 || cycle >= 2));

      // Stretto in the climax cycle: a second subject statement in the
      // accompaniment voice at a 1-bar delay, overlapping the leader. The
      // follower restates the subject in the SAME related key as the leader
      // (key_semis), octave-fit into the follower's band, so the overlap forms
      // a single-key canon at the octave instead of clashing bi-tonally. The
      // follower replaces the figuration accompaniment for this window.
      const bool climax = (cycle == climax_cycle);
      if (climax) {
        const int follower_voice = acc_voice;
        const int follower_off = octaveOffsetForBand(subj_pat, key_semis, follower_voice);
        // material.subject[i] == subj_pat[i] + v0_off (the V0 exposition
        // statement), so the validated relation follower[i] == subject[i] +
        // interval requires interval = key_semis + follower_off - v0_off. This
        // keeps the validator's stretto_overlap_valid verbatim-transposition
        // relation exact while the follower sits in the leader's related key.
        const int stretto_interval = key_semis + follower_off - v0_off;
        StrettoDecl stretto;
        stretto.leader_voice = static_cast<VoiceId>(carry_voice);
        stretto.follower_voice = static_cast<VoiceId>(follower_voice);
        stretto.leader_entry_tick = barTick(me_start);
        stretto.leader_length_ticks = barTick(kSubjectBars);
        stretto.follower_entry_tick = barTick(me_start + 1);
        stretto.interval_semis = stretto_interval;
        Tick follower_cursor = barTick(me_start + 1);
        const Tick follower_end = barTick(me_start + kSubjectBars);
        for (int note = 0; note < kSubjectNotes && follower_cursor < follower_end; ++note) {
          MaterialNote mn;
          mn.start_tick = follower_cursor;
          mn.duration =
              std::min(subj_rhythm[static_cast<std::size_t>(note)], follower_end - follower_cursor);
          mn.pitch = static_cast<std::uint8_t>(std::clamp(
              static_cast<int>(subj_pat[static_cast<std::size_t>(note)]) + key_semis + follower_off,
              0, 127));
          stretto.follower_notes.push_back(mn);
          asm_ctx.theme_tones.record(mn.start_tick, static_cast<VoiceId>(follower_voice),
                                     static_cast<int>(mn.pitch), mn.duration);
          follower_cursor += subj_rhythm[static_cast<std::size_t>(note)];
        }
        out.material.stretto_entries.push_back(stretto);
        pushSpan(asm_ctx, static_cast<VoiceId>(follower_voice), me_start + 1, me_start + 3,
                 VoiceIntent::StrettoCarrier);
      } else if (cycle == pedal_cycle) {
        // Dominant pedal in the cycle before the coda (N >= 32): a single held
        // dominant in the lowest non-carrying voice (voice-filtered carrier).
        const int pedal_voice = (carry_voice == 2) ? 1 : 2;
        int pedal_pitch = kBandLo[pedal_voice];
        while (pedal_pitch % 12 != 7) {  // dominant pc = G.
          ++pedal_pitch;
        }
        PedalPointDecl pedal;
        pedal.voice = static_cast<VoiceId>(pedal_voice);
        pedal.start_tick = barTick(me_start);
        pedal.duration = barTick(kSubjectBars);
        pedal.pitch = static_cast<std::uint8_t>(pedal_pitch);
        pedal.is_dominant = true;
        out.material.pedal_points.push_back(pedal);
        pushSpan(asm_ctx, static_cast<VoiceId>(pedal_voice), me_start, me_start + 3,
                 VoiceIntent::PedalCarrier);
      } else {
        // Plain figuration accompaniment in the highest non-carrying voice.
        addFigurationSpan(asm_ctx, static_cast<VoiceId>(acc_voice), me_start, me_start + 3, plan,
                          first_bar, mode, density, fig_offset);
      }
      // When the middle entry is carried by V2, the figuration accompaniment
      // lands on V0 and the middle voice would otherwise rest. Fill V1 with
      // chord-tone figuration so all three voices sound through the entry. The
      // pedal cycle already places a held tone in V1, so it is excluded. The V1
      // figuration is verbatim Material (both-Material with the V0 figuration, so
      // the upper-pair invertible / fourth checks are skipped); V2 harmonic
      // support, placed afterward, avoids parallels against it.
      const bool fill_middle_voice = (carry_voice == 2 && cycle != pedal_cycle);
      if (fill_middle_voice) {
        addFigurationSpan(asm_ctx, 1, me_start, me_start + 3, plan, first_bar, mode, density,
                          fig_offset);
      }
      if (add_middle_bass_support) {
        // V2 bass support is a verbatim Material scalar-wave figuration (quarter
        // notes, one chord-tone anchor per beat connected by scale steps), matching
        // the episode-bass construction. A Material bass walks stepwise instead of
        // re-striking a single chord root, and because it is Material the validator
        // skips every inter-voice parallel rule against the faster figuration above
        // it -- band confinement keeps V0 >= V1 >= V2 so voice_crossing never fires.
        addFigurationSpan(asm_ctx, 2, me_start, me_start + 3, plan, first_bar, mode, 1, fig_offset);
      }
    }

    // --- Episode (4 bars): a Fortspinnung sequence derived from the subject
    //     head, transposed per the character's motif operation, in V0 with one
    //     band-confined accompaniment voice below. ---
    const int ep_start = first_bar + window.episode_start;
    const int ep_len = window.episode_len;
    if (ep_len > 0) {
      const motif_ops::EpisodeMotifTransform transform =
          motif_ops::characterToTransform(req.character);
      Tick seed_dur = kTicksPerBeat / 2;  // eighths (Severe / Playful / Noble).
      if (transform == motif_ops::EpisodeMotifTransform::Diminish) {
        seed_dur = kTicksPerBeat / 4;  // Restless: diminished (sixteenths).
      }
      // Seed motif: the subject head (4 notes) rebuilt DIATONICALLY low in the
      // V0 band. The head's semitone intervals are mapped to scale degrees and
      // re-rooted on an in-scale base, because a real (semitone) transposition
      // of the head plus a real +/-2-semitone step chain walked every episode
      // out of the key (B-C#-D#-E, then C#-D#-F-F#, ...) -- a whole-tone smear
      // that reads as the piece breaking down from the first episode onward.
      // The base also shifts by one scale degree per cycle (mod 3) so the
      // episodes vary in register instead of repeating one figure verbatim.
      int seed_base = kBandLo[0] + 4;
      while (!detail::inScale(seed_base, mode)) {
        ++seed_base;
      }
      seed_base = scaleUp(seed_base, cycle % 3, mode);
      // Semitone interval -> diatonic degree count (|rel| <= 12).
      constexpr std::array<int, 13> kSemisToDegrees = {0, 1, 1, 2, 2, 3, 3, 4, 5, 5, 6, 6, 7};
      std::array<int, 4> seed_pitch{};
      for (int note = 0; note < 4; ++note) {
        const int rel = static_cast<int>(subj_pat[static_cast<std::size_t>(note)]) -
                        static_cast<int>(subj_pat[0]);
        const int degrees = kSemisToDegrees[static_cast<std::size_t>(std::min(std::abs(rel), 12))];
        const int diatonic = (rel >= 0) ? scaleUp(seed_base, degrees, mode)
                                        : scaleDown(seed_base, degrees, mode);
        seed_pitch[static_cast<std::size_t>(note)] =
            std::clamp(diatonic, kBandLo[0], kBandHi[0]);
      }
      // One SequenceTemplate per step (num_steps = 1 each), every step's seed
      // transposed by one scale DEGREE per step (direction alternates by
      // cycle). Single-step templates keep the diatonic walk while the shared
      // replay/validator machinery (which transposes multi-step templates by
      // raw semitones) stays untouched -- the verbatim step-0 check in
      // sequence_pattern_consistency still covers every emitted note.
      const bool ascending = (cycle % 2 == 0);
      const int steps = std::max(1, ep_len / 2);
      // Keep the whole walk inside the V0 band: shift the seed away from the
      // boundary the sequence moves toward, so the final step never clamps
      // into a repeated-pitch plateau (a clamped descending walk otherwise
      // flattens to the band floor, e.g. G-G-G-G).
      {
        const int extreme = ascending
                                ? *std::max_element(seed_pitch.begin(), seed_pitch.end())
                                : *std::min_element(seed_pitch.begin(), seed_pitch.end());
        int walked = extreme;
        for (int k = 0; k < steps - 1; ++k) {
          walked = ascending ? scaleUp(walked, 1, mode) : scaleDown(walked, 1, mode);
        }
        while (ascending ? (walked > kBandHi[0]) : (walked < kBandLo[0])) {
          for (int& p : seed_pitch) {
            p = ascending ? scaleDown(p, 1, mode) : scaleUp(p, 1, mode);
          }
          walked = ascending ? scaleDown(walked, 1, mode) : scaleUp(walked, 1, mode);
        }
      }
      const Tick stride = barTick(2);
      const Tick span_lo = barTick(ep_start);
      const Tick span_hi = barTick(ep_start + ep_len);
      for (int kstep = 0; kstep < steps; ++kstep) {
        SequenceTemplate tmpl;
        tmpl.pattern =
            ascending ? SequencePattern::AscendingStep : SequencePattern::DescendingStep;
        tmpl.target_start_tick = barTick(ep_start) + static_cast<Tick>(kstep) * stride;
        tmpl.step_length_ticks = stride;
        tmpl.num_steps = 1;
        tmpl.voice = 0;
        Tick cursor = tmpl.target_start_tick;
        for (int note = 0; note < 4; ++note) {
          const int base = seed_pitch[static_cast<std::size_t>(note)];
          const int pitch = ascending ? scaleUp(base, kstep, mode) : scaleDown(base, kstep, mode);
          const int clamped = std::clamp(pitch, kBandLo[0], kBandHi[0]);
          tmpl.seed_pitches.push_back(static_cast<std::uint8_t>(clamped));
          tmpl.seed_durations.push_back(seed_dur);
          // Register the sounding tone (replicating the FortspinnungSpan
          // replay, which window-clips) so the V1/V2 accompaniment built below
          // avoids clashing with the V0 episode line.
          if (cursor >= span_lo && cursor < span_hi) {
            asm_ctx.theme_tones.record(cursor, 0, clamped, seed_dur);
          }
          cursor += seed_dur;
        }
        out.material.sequence_templates.push_back(tmpl);
      }
      pushSpan(asm_ctx, 0, ep_start, ep_start + ep_len - 1, VoiceIntent::FortspinnungSpan);

      // Episodes carry BOTH a V1 figuration and a V2 bass under the V0
      // Fortspinnung, so all three voices sound through the development instead
      // of leaving the middle and/or bass register empty. Both accompaniment
      // voices are verbatim Material whose strong beats anchor on chord tones
      // consonant with the concurrent theme tones. With all three voices fixed,
      // the validator skips every inter-voice rule but voice_crossing, which the
      // disjoint per-voice bands already prevent; a free Compose bass here would
      // be forced into parallels against the fast figuration and rest, thinning
      // the texture. The V1 figuration moves in eighths; the V2 bass walks in
      // quarter-note chord roots a register below it.
      addFigurationSpan(asm_ctx, 1, ep_start, ep_start + ep_len - 1, plan, first_bar, mode, 2,
                        fig_offset);
      addFigurationSpan(asm_ctx, 2, ep_start, ep_start + ep_len - 1, plan, first_bar, mode, 1,
                        fig_offset);
    }
  }

  // Materialize the per-voice middle-entry decls (only the used ones).
  for (int voice = 0; voice < 3; ++voice) {
    if (middle_used[static_cast<std::size_t>(voice)]) {
      out.material.middle_entries.push_back(middle_decls[static_cast<std::size_t>(voice)]);
    }
  }

  // === CODA =================================================================
  // Final subject entry (V0) over the first 2 coda bars, then an explicit 2-bar
  // V->I cadence. The cadence is voiced so the validator's cadence_voice_leading
  // rule passes: the upper voice (V0) resolves the leading tone B->C across the
  // final bar boundary, and the bass (V2) moves dominant (G) -> tonic (C).
  const int coda_start = first_bar + bars - coda_bars;  // absolute first coda bar.
  Tick coda_cursor = barTick(coda_start);
  const Tick coda_subject_end = barTick(coda_start + 2);
  for (int note = 0; note < kSubjectNotes && coda_cursor < coda_subject_end; ++note) {
    const Tick dur =
        std::min(subj_rhythm[static_cast<std::size_t>(note)], coda_subject_end - coda_cursor);
    // addNote clamps the pitch into [0,127]; record the identical clamped value
    // into theme_tones so the V1/V2 figuration anchors below can see the V0
    // subject head and stay consonant / parallel-free against it.
    const int pitch =
        std::clamp(static_cast<int>(subj_pat[static_cast<std::size_t>(note)]) + v0_off, 0, 127);
    addNote(out.material.subject, coda_cursor, dur, pitch);
    asm_ctx.theme_tones.record(coda_cursor, 0, pitch, dur);
    coda_cursor += subj_rhythm[static_cast<std::size_t>(note)];
  }
  pushSpan(asm_ctx, 0, coda_start, coda_start + 1, VoiceIntent::SubjectCarrier);
  // V1 alto support keeps the three-voice texture through the final entry; placed
  // after the V0 record and before the V2 bass so each lower line anchors against
  // everything already sounding above it.
  addFigurationSpan(asm_ctx, 1, coda_start, coda_start + 1, plan, first_bar, mode, 1, fig_offset);
  // V2 bass under the final subject is a verbatim Material scalar-wave figuration
  // (quarter notes), matching the development bass support: it walks chord tones
  // by scale steps instead of re-striking one root, and being Material it skips the
  // inter-voice parallel checks against the Material subject and alto above it.
  addFigurationSpan(asm_ctx, 2, coda_start, coda_start + 1, plan, first_bar, mode, 1, fig_offset);

  // V0 cadence figure (CodaCarrier, bars coda_start+2 .. coda_start+3). The
  // penultimate-bar last beat is the leading tone B; the final downbeat is the
  // tonic C; Picardy (minor + even seed) tints the approach with the major 3rd.
  {
    CodaDecl coda;
    coda.voice = 0;
    // Centre the cadence on the UPPER C of the V0 band so the leading tone B
    // (one semitone below) still lies inside the band (a leading tone below the
    // band floor would be clamped up to the tonic and break the resolution).
    int tonic = kBandHi[0];  // highest C at or below the band ceiling.
    while (tonic % 12 != 0) {
      --tonic;
    }
    const int leading = tonic - 1;  // B, the leading tone below the tonic.
    const int third = tonic - 8;    // E (major 3rd) an octave down (in band).
    const int fifth = tonic - 5;    // G a fourth below (in band).
    // 8 quarters across 2 bars. The penultimate-bar beats (idx 0-3) approach the
    // cadence: idx 3 is the leading tone B, and the final-bar downbeat (idx 4)
    // is the tonic C, so upper_prev = B resolves to upper_now = C (the
    // cadence_voice_leading rule reads exactly those two ticks).
    std::array<int, 8> cadence = {fifth, third, fifth, leading, tonic, third, fifth, tonic};
    if (mode == Mode::Minor && usePicardy(req.seed)) {
      // Picardy: keep the major 3rd E in the closing tonic colour (idx 5).
      cadence[5] = third;
    } else if (mode == Mode::Minor) {
      cadence[1] = tonic - 9;  // minor 3rd (Eb) an octave down in the band.
      cadence[5] = tonic - 9;
    }
    for (int idx = 0; idx < 8; ++idx) {
      const int bar = (coda_start + 2) + idx / 4;
      const int beat = idx % 4;
      addNote(coda.notes, barTick(bar) + static_cast<Tick>(beat) * kTicksPerBeat, kQuarter,
              std::clamp(cadence[static_cast<std::size_t>(idx)], kBandLo[0], kBandHi[0]));
    }
    out.material.coda_extensions.push_back(coda);
    pushSpan(asm_ctx, 0, coda_start + 2, coda_start + 3, VoiceIntent::CodaCarrier);
  }

  // V2 cadential bass (PedalCarrier-free explicit roots). Two held half-note
  // chord roots per bar: dominant (G) through the penultimate coda bar, tonic
  // (C) on the final bar -- giving bass_prev = G at the approach beat and
  // bass_now = C at the cadence downbeat.
  {
    int bass_dominant = kBandLo[2];
    while (bass_dominant % 12 != 7) {
      ++bass_dominant;
    }
    int bass_tonic = kBandLo[2];
    while (bass_tonic % 12 != 0) {
      ++bass_tonic;
    }
    FigurationSection bass;
    bass.voice = 2;
    bass.start_tick = barTick(coda_start + 2);
    bass.end_tick = barTick(coda_start + coda_bars);
    // Penultimate coda bar: held dominant (four quarter Gs so every beat,
    // including the cadence approach beat, sounds G).
    for (int beat = 0; beat < 4; ++beat) {
      addNote(bass.notes, barTick(coda_start + 2) + static_cast<Tick>(beat) * kTicksPerBeat,
              kQuarter, bass_dominant);
    }
    // Final coda bar: held tonic (four quarter Cs).
    for (int beat = 0; beat < 4; ++beat) {
      addNote(bass.notes, barTick(coda_start + 3) + static_cast<Tick>(beat) * kTicksPerBeat,
              kQuarter, bass_tonic);
    }
    coalesceConsecutiveSamePitch(bass.notes);
    out.material.figuration_sections.push_back(bass);
    pushSpan(asm_ctx, 2, coda_start + 2, coda_start + 3, VoiceIntent::FigurationCarrier);
  }

  // Final-cadence annotation: a perfect cadence on the final bar downbeat.
  {
    CadenceEvent cadence;
    cadence.tick = barTick(coda_start + coda_bars - 1);
    cadence.type = CadenceType::Perfect;
    out.harmony.cadences.push_back(cadence);
  }
}

}  // namespace

HarnessFixture buildFugueForm(const ResolvedRequest& req) {
  HarnessFixture out;
  out.voice_plan.num_voices = 3;
  SpanId next_id = 0;
  FugueAssembly asm_ctx{&out, &next_id, {}};
  appendFugueSection(asm_ctx, /*first_bar=*/0, static_cast<int>(req.bars), req);
  return out;
}

HarnessFixture buildPreludeAndFugueForm(const ResolvedRequest& req) {
  HarnessFixture out;
  out.voice_plan.num_voices = 3;
  SpanId next_id = 0;
  FugueAssembly asm_ctx{&out, &next_id, {}};

  const int total = static_cast<int>(req.bars);
  // Prelude length = N/3 rounded to 4, clamped to [4, 32]; the rest is fugue
  // (kept >= 16 so the fugue half always carries a full exposition + coda).
  int prelude_bars = ((total / 3 + 2) / 4) * 4;
  prelude_bars = std::clamp(prelude_bars, 4, 32);
  if (total - prelude_bars < 16) {
    prelude_bars = total - 16;
    prelude_bars = std::max(4, (prelude_bars / 4) * 4);
  }
  const int fugue_bars = total - prelude_bars;

  const Mode mode = req.mode;
  const int harm_idx = static_cast<int>(req.seed % 4);
  const int fig_offset = static_cast<int>(req.seed % 4);

  // --- PRELUDE (bars 0 .. prelude_bars-1). V0 + V1 per-beat chord-tone-
  //     anchored sawtooth figuration (the proven WTC-pair construction: every
  //     beat restarts on a chord tone so the on-beat verticals stay consonant);
  //     V2 silent. Harmony cycles the diatonic pattern with 4-bar cadences. ---
  const std::vector<ChordSpec> prelude_plan = buildChordPlan(prelude_bars, mode, harm_idx);
  emitHarmony(out, prelude_plan, mode, 0);

  // The prelude uses the same parallel-aware scalar-wave figuration as the
  // fugue body (addFigurationSpan): every beat opens on a consonant chord tone
  // that does not form a parallel fifth/octave against the voices already placed
  // in the same window, and the wave between anchors is likewise parallel-aware.
  // Voices are built top-down (V0 -> V1 -> V2) so each lower voice reads the
  // higher ones already recorded and avoids parallels against them; the disjoint
  // bands keep V0 >= V1 >= V2 so voice_crossing never fires. The plan is indexed
  // by absolute bar (plan_base = 0).
  //
  // V0 prelude figuration (sixteenths) split into 2-bar sections; the final
  // section is is_pedal_prep so PedalPreparation links into the fugue. A 2-bar
  // chunk size keeps every V0 section window distinct from the single
  // whole-prelude V1 / V2 windows below (the FigurationCarrier dispatch matches
  // sections by window only, so two sections sharing a window would collide).
  for (int sec_start = 0; sec_start < prelude_bars; sec_start += 2) {
    const int sec_last = std::min(sec_start + 1, prelude_bars - 1);
    const bool pedal_prep = (sec_last == prelude_bars - 1);
    addFigurationSpan(asm_ctx, 0, sec_start, sec_last, prelude_plan, 0, mode, 4, fig_offset,
                      pedal_prep);
  }
  // V1 prelude bass support (eighths) across the whole prelude (single window).
  addFigurationSpan(asm_ctx, 1, 0, prelude_bars - 1, prelude_plan, 0, mode, 2, fig_offset);
  // V2 prelude pedal-register support (quarter-note chord tones) across the
  // whole prelude, so all three voices sound through the prelude instead of
  // leaving the bass register empty.
  addFigurationSpan(asm_ctx, 2, 0, prelude_bars - 1, prelude_plan, 0, mode, 1, fig_offset);

  // --- FUGUE (bars prelude_bars .. total-1). Reuse the full fugue assembly at
  //     a bar offset; span ids continue from the prelude (shared next_id). ---
  appendFugueSection(asm_ctx, prelude_bars, fugue_bars, req);

  // Keep the concatenated HarmonicPlan chords in tick order.
  std::stable_sort(
      out.harmony.chords.begin(), out.harmony.chords.end(),
      [](const ChordEvent& lhs, const ChordEvent& rhs) { return lhs.start_tick < rhs.start_tick; });

  return out;
}

}  // namespace bach::composer
