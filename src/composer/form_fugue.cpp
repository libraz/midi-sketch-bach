#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "composer/character_profile.h"
#include "composer/chord_voicing.h"
#include "composer/figuration.h"
#include "composer/figuration_palette.h"
#include "composer/form_builders.h"
#include "composer/material.h"
#include "composer/minor_material.h"
#include "composer/motif_ops.h"
#include "composer/span.h"
#include "composer/subject_catalog.h"
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

using detail::ChordSpec;                    // NOLINT(build/namespaces)
using detail::Mode;                         // NOLINT(build/namespaces)
using detail::scaleUp;                      // NOLINT(build/namespaces)
using detail::subjectIndexFor;              // NOLINT(build/namespaces)
using tables::kSubjectCatalogMajor;         // NOLINT(build/namespaces)
using tables::kSubjectCatalogMajorRhythms;  // NOLINT(build/namespaces)
using tables::kSubjectCatalogMinor;         // NOLINT(build/namespaces)
using tables::kSubjectCatalogMinorRhythms;  // NOLINT(build/namespaces)

constexpr Tick kQuarter = kTicksPerBeat;

#include "composer/tables/entry_plan_stats.inc"

// One subject statement is 16 catalog notes spanning 4 bars. Durations come
// from the per-mode catalog rhythm rows rather than being fixed quarters.
constexpr int kSubjectNotes = 16;
constexpr int kSubjectBars = 4;

// Per-voice register bands (MIDI). The bands are disjoint and strictly ordered
// (V0 highest, V2 lowest). Every voice's material is octave-fit so its highest
// note stays at or below the band ceiling (octaveOffsetForBand is ceiling-first).
// The V0 ceiling is the practical manual compass top used by the texture gate;
// keeping it at C6 prevents the old D7-range fixture artifact from returning.
constexpr std::array<int, 3> kBandLo = {67, 51, 33};
constexpr std::array<int, 3> kBandHi = {84, 66, 50};

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

/// @brief Mirror a mode-diatonic line around its ambit centre in degree space.
///
/// Each note is reflected through the centre of the line's degree ambit
/// (never raw semitones): with d_i the signed diatonic degree distance from
/// the first note to note i, the mirrored distance is (d_min + d_max) - d_i,
/// so every step's direction flips while the line's lowest and highest
/// degrees simply swap in place. Mirroring around the AMBIT CENTRE (rather
/// than the first note) keeps the inverted statement in the same register as
/// the upright one: a first-note mirror hangs an ascending subject downward
/// and sinks the entry to the bottom of its voice band. Because the walk is
/// done with detail::scaleUp / detail::scaleDown in the selected mode, the
/// result stays in that modal collection; the downstream octaveOffsetForBand step
/// then places it exactly where the upright line would sit.
///
/// @param pat The 16-note mode-diatonic subject line.
/// @param mode Subject mode.
/// @return The diatonic melodic inversion of `pat`.
std::array<std::uint8_t, 16> invertDiatonicLine(const std::array<std::uint8_t, 16>& pat,
                                                Mode mode) {
  const int anchor = static_cast<int>(pat[0]);
  // Signed diatonic degree distance from the anchor to each note.
  std::array<int, 16> degrees{};
  int d_min = 0;
  int d_max = 0;
  for (std::size_t idx = 0; idx < pat.size(); ++idx) {
    const int pitch = static_cast<int>(pat[idx]);
    int steps = 0;
    int walk = std::min(anchor, pitch);
    const int top = std::max(anchor, pitch);
    while (walk < top) {
      walk = scaleUp(walk, 1, mode);
      ++steps;
    }
    degrees[idx] = (pitch >= anchor) ? steps : -steps;
    d_min = std::min(d_min, degrees[idx]);
    d_max = std::max(d_max, degrees[idx]);
  }
  // Reflect through the ambit centre: d -> (d_min + d_max) - d.
  const int mirror_sum = d_min + d_max;
  std::array<std::uint8_t, 16> inverted{};
  for (std::size_t idx = 0; idx < pat.size(); ++idx) {
    const int mirrored_degrees = mirror_sum - degrees[idx];
    const int mirrored = mirrored_degrees >= 0 ? scaleUp(anchor, mirrored_degrees, mode)
                                               : scaleDown(anchor, -mirrored_degrees, mode);
    inverted[idx] = static_cast<std::uint8_t>(std::clamp(mirrored, 0, 127));
  }
  return inverted;
}

// Major-mode middle entries rotate V / vi / IV. In minor, a degree shift of
// the selected minor subject stays inside the home minor collection while its
// declared stations rotate v / III / iv; this avoids importing the major
// catalog's A/B/E naturals into a C-minor fugue.
constexpr std::array<std::uint8_t, 3> kVoiceKeyPc = {7, 9, 5};       // V0->V, V1->vi, V2->IV.
constexpr std::array<int, 3> kVoiceKeySemis = {7, 9, 5};             // diatonic offsets.
constexpr std::array<std::uint8_t, 3> kMinorVoiceKeyPc = {7, 3, 5};  // V0->v, V1->III, V2->iv.
constexpr std::array<int, 3> kMinorVoiceDegreeShift = {4, 2, 3};

std::uint8_t middleEntryKeyPc(int voice, Mode mode) {
  const std::size_t index = static_cast<std::size_t>(voice);
  return mode == Mode::Minor ? kMinorVoiceKeyPc[index] : kVoiceKeyPc[index];
}

// Diatonic degree shift (never a raw semitone transposition) that restates the
// canonical countersubject in a middle entry's related key, keyed by the
// carrying voice. V0->V is up a fifth (+4 degrees), V1->vi up a sixth (+5),
// V2->IV up a fourth (+3): degree shifting keeps the line home-diatonic, so it
// forms no cross-relation against the home-scale texture around the entry.
constexpr std::array<int, 3> kCountersubjectDegreeShift = {4, 5, 3};

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
constexpr int kEntryDecileCount =
    static_cast<int>(sizeof(kEntryIntervalDeciles) / sizeof(kEntryIntervalDeciles[0]));

int adaptCorpusEntryInterval(int corpus_interval_bars) {
  // The annotation corpus's principal subjects are about two bars long while
  // this builder's subject is fixed at four. Map the corpus episode component
  // (start gap minus mean subject length) into this builder's established
  // four-to-six-bar development-episode band. This preserves lower/middle/
  // upper corpus quantiles as 8/9/10-bar cycles without overcrowding the
  // generated four-bar subjects.
  const int corpus_subject_bars = std::clamp(static_cast<int>(kSubjectLengthMeanBars + 0.5), 1, 4);
  const int corpus_episode_bars = std::max(0, corpus_interval_bars - corpus_subject_bars);
  const int corpus_tail_episode_bars =
      std::max(1, kEntryIntervalDeciles[kEntryDecileCount - 1] - corpus_subject_bars);
  constexpr int kMappedMinEpisodeBars = 4;
  constexpr int kMappedEpisodeRange = kMaxEpisodeBars - kMappedMinEpisodeBars;
  const int mapped_episode =
      kMappedMinEpisodeBars +
      (std::min(corpus_episode_bars, corpus_tail_episode_bars) * kMappedEpisodeRange +
       corpus_tail_episode_bars / 2) /
          corpus_tail_episode_bars;
  return kSubjectBars + mapped_episode;
}

int entryIntervalForCycle(std::uint32_t seed, int cycle) {
  const int corpus_interval =
      kEntryIntervalDeciles[(static_cast<int>(seed % kEntryDecileCount) + cycle * 2) %
                            kEntryDecileCount];
  const int raw = adaptCorpusEntryInterval(corpus_interval);
  // Clamp only to the builder's structural episode window. The old additional
  // Q1/Q3 clamp was applied to beat-valued constants and collapsed all nine
  // deciles to two outcomes; after unit normalization it would erase the
  // corpus tail again. The corrected table now produces 8-, 9-, and 10-bar
  // cycles across a full seed/cycle rotation.
  return std::clamp(raw, kSubjectBars + kMinEpisodeBars, kSubjectBars + kMaxEpisodeBars);
}

bool useVariableEntrySchedule(int fugue_bars) {
  const bool short_form = fugue_bars < 20;
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
                       int notes_per_beat, int offset, bool is_pedal_prep = false,
                       bool cadential_close_last_bar = false) {
  FigurationSection section;
  section.voice = voice;
  section.start_tick = barTick(first_bar);
  section.end_tick = barTick(last_bar + 1);
  section.is_pedal_prep = is_pedal_prep;
  // Running anchor threaded across the section's bars so consecutive bar-edge
  // anchors chain stepwise (no leap at the bar boundary). Seeded by the first
  // bar's centre inside appendFigurationWaveBar (prev_anchor <= 0).
  int prev_anchor = 0;
  for (int bar = first_bar; bar <= last_bar; ++bar) {
    // When requested, the span's final bar closes on a held mid-bar anchor so
    // the ornament pass has a strong-beat quarter-or-longer top note for the
    // mandatory section-cadence trill.
    const bool cadential_close = cadential_close_last_bar && (bar == last_bar);
    appendFigurationWaveBar(asm_ctx.theme_tones, section, bar, voice,
                            chords[static_cast<std::size_t>(bar - plan_base)], mode, notes_per_beat,
                            offset, prev_anchor, kBandLo[voice], kBandHi[voice], kFugueVoices,
                            cadential_close);
  }
  coalesceConsecutiveSamePitch(section.notes);
  asm_ctx.out->material.figuration_sections.push_back(section);
  pushSpan(asm_ctx, voice, first_bar, last_bar, VoiceIntent::FigurationCarrier);
}

// --- Fugue tonal plan --------------------------------------------------------
// The fugue's harmony is a piece-level tonal design rather than a rotating
// 4-bar loop: every region states the function the form assigns to it.
//   exposition    subject / third-entry bars affirm the home key; the answer
//                 bars sit on the dominant (the real answer IS the dominant
//                 statement).
//   middle entry  the 4 entry bars state the entry's related key with one
//                 neighbouring pre-dominant colour.
//   episode       a diatonic descending-fifths chain constructed BACKWARD from
//                 the next station's key, so every episode drives into the
//                 following entry (or the coda's home return) instead of
//                 circling a generic loop.
//   pedal cycle   dominant prolongation under the held pedal.
//   coda          home cadence (the final two bars stay pinned V -> I).
// Every chord is spelled inside the working diatonic vocabulary (major: the
// diatonic triads with the B-rooted diminished folded out of the chain; minor:
// the harmonic-minor vocabulary kHarmonyPatternsMinor speaks -- i / III / iv /
// V / VI / VII), so figuration anchored on these chords keeps the
// figuration-stays-diatonic contract.

/// @brief Diatonic triad quality for a chord root in the home key.
ChordSpec diatonicChord(int root_pc, Mode mode) {
  const bool minor_triad = (mode == Mode::Minor) ? (root_pc == 0 || root_pc == 5)
                                                 : (root_pc == 2 || root_pc == 4 || root_pc == 9);
  return ChordSpec{static_cast<std::uint8_t>(root_pc), minor_triad};
}

/// @brief Root of the diatonic chord a fifth above `root_pc` (its predecessor
/// in a descending-fifths chain).
///
/// Major walks the closed cycle C<-G<-Dm<-Am<-Em<-F<-C (the B-rooted
/// diminished link is folded into an F->Em step, which keeps every chain chord
/// a representable diatonic triad); minor walks i<-V<-VI<-III<-VII<-iv<-i, the
/// lament circle with the harmonic-minor dominant. Roots outside the cycle
/// (the minor-mode vi station) enter through the dominant.
int fifthAboveRoot(int root_pc, Mode mode) {
  if (mode == Mode::Minor) {
    switch (root_pc) {
      case 0:
        return 7;
      case 7:
        return 8;
      case 8:
        return 3;
      case 3:
        return 10;
      case 10:
        return 5;
      case 5:
        return 0;
      default:
        return 7;
    }
  }
  switch (root_pc) {
    case 0:
      return 7;
    case 7:
      return 2;
    case 2:
      return 9;
    case 9:
      return 4;
    case 4:
      return 5;
    case 5:
      return 0;
    default:
      return 7;
  }
}

/// @brief 4-bar harmonic progression for an entry window, keyed by the entry's
/// related key (home / V / vi / IV), spelled in home-key diatonic chords.
///
/// Outer bars carry the entry key's tonic function; the inner bars add its
/// subdominant/dominant colour from inside the home vocabulary, so the
/// accompaniment states the entry's key without leaving the working scale.
/// The minor-mode vi entry (a degree-shifted line whose pitch set is the home
/// MAJOR scale) gets dominant-set support: V is the only minor-vocabulary
/// triad fully inside that line's pitch world.
std::array<ChordSpec, 4> entryProgression(std::uint8_t key_pc, Mode mode) {
  if (mode == Mode::Minor) {
    switch (key_pc) {
      case 7:
        return {{{7, false}, {0, true}, {8, false}, {7, false}}};
      case 3:
        // The III-related subject is degree-shifted inside C minor rather
        // than chromatically transposed. Support it with the home dominant:
        // this preserves its shared pitch collection and gives the entry the
        // same dominant arrival as the v station instead of falsely resetting
        // the tonal plan to I.
        return {{{7, false}, {0, true}, {7, false}, {7, false}}};
      case 9:
        return {{{7, false}, {0, true}, {7, false}, {7, false}}};
      case 5:
        return {{{5, true}, {0, true}, {10, false}, {5, true}}};
      default:
        return {{{0, true}, {5, true}, {7, false}, {0, true}}};
    }
  }
  switch (key_pc) {
    case 7:
      return {{{7, false}, {0, false}, {9, true}, {7, false}}};
    case 9:
      return {{{9, true}, {2, true}, {4, true}, {9, true}}};
    case 5:
      return {{{5, false}, {0, false}, {2, true}, {5, false}}};
    default:
      return {{{0, false}, {5, false}, {7, false}, {0, false}}};
  }
}

/// @brief Build the fugue's per-bar chord plan from its tonal stations.
///
/// `windows` / `pedal_cycle` are the already-computed development layout; bars
/// are section-relative. Entry windows take their key's progression (the pedal
/// cycle takes dominant prolongation instead), episode windows take the
/// backward descending-fifths chain into the next station, and the coda takes
/// the home progression. The caller pins the final V -> I afterwards.
std::vector<ChordSpec> buildFugueTonalPlan(int bars, Mode mode, int exposition_bars, int coda_bars,
                                           const std::vector<DevelopmentWindow>& windows,
                                           int pedal_cycle) {
  const bool minor = (mode == Mode::Minor);
  std::vector<ChordSpec> plan(static_cast<std::size_t>(bars), ChordSpec{0, minor});
  const auto home_prog = entryProgression(0, mode);
  const auto dominant_prog = entryProgression(7, mode);
  for (int bar = 0; bar < std::min(bars, exposition_bars); ++bar) {
    const bool answer_bars = (bar >= 4 && bar < 8);
    plan[static_cast<std::size_t>(bar)] = answer_bars
                                              ? dominant_prog[static_cast<std::size_t>(bar - 4)]
                                              : home_prog[static_cast<std::size_t>(bar % 4)];
  }
  const auto carry_voice_for = [&](int cycle) {
    const int rotation_voice = cycle % 3;
    // The pedal cycle's station is a dominant prolongation (V/I64 over the
    // held dominant), so the entry ALWAYS states in the dominant key on V0:
    // letting the vi or IV rotation stand would stamp a foreign-key subject
    // (the IV entry's Bb) over the V pedal -- a bitonal grind -- and, with a
    // V2 carry, would push the pedal into the middle voice while the bass
    // walks underneath it.
    return (cycle == pedal_cycle && rotation_voice != 0) ? 0 : rotation_voice;
  };
  // Pass 1: station chords (entries and the coda), so every episode's target
  // bar is already filled when the chains are built.
  for (int cycle = 0; cycle < static_cast<int>(windows.size()); ++cycle) {
    const DevelopmentWindow& window = windows[static_cast<std::size_t>(cycle)];
    if (!window.has_entry) {
      continue;
    }
    const std::uint8_t key_pc = middleEntryKeyPc(carry_voice_for(cycle), mode);
    // The pedal window prolongs the dominant by alternating V with the tonic
    // 6/4 colour over the held pedal (the textbook dominant-pedal harmony).
    // The alternation also keeps the counterline's admissible chord-tone set
    // wide: a window of straight V chords starves the wave against the
    // entry's subject tail (two-tone wobble).
    const std::array<ChordSpec, 4> prog =
        (cycle == pedal_cycle)
            ? std::array<ChordSpec, 4>{{{7, false}, {0, minor}, {7, false}, {0, minor}}}
            : entryProgression(key_pc, mode);
    for (int k = 0; k < kSubjectBars && window.entry_start + k < bars; ++k) {
      plan[static_cast<std::size_t>(window.entry_start + k)] = prog[static_cast<std::size_t>(k)];
    }
  }
  for (int k = 0; k < coda_bars && k < 4; ++k) {
    const int bar = bars - coda_bars + k;
    if (bar >= 0 && bar < bars) {
      plan[static_cast<std::size_t>(bar)] = home_prog[static_cast<std::size_t>(k)];
    }
  }
  // Pass 2: episode chains, in reverse window order. Each chain is built
  // backward from the root of the chord on the bar RIGHT AFTER the episode --
  // the next entry's opening chord, the coda's home return, or (for an episode
  // followed by a trailing episode-only window) that window's first chain
  // chord, which the reverse order has already placed. Reading the actual next
  // bar keeps consecutive episodes one unbroken chain.
  for (int cycle = static_cast<int>(windows.size()) - 1; cycle >= 0; --cycle) {
    const DevelopmentWindow& window = windows[static_cast<std::size_t>(cycle)];
    const int after_end = window.episode_start + window.episode_len;
    int root = (after_end < bars)
                   ? static_cast<int>(plan[static_cast<std::size_t>(after_end)].root_pc)
                   : 0;
    for (int k = window.episode_len - 1; k >= 0; --k) {
      root = fifthAboveRoot(root, mode);
      if (window.episode_start + k < bars) {
        plan[static_cast<std::size_t>(window.episode_start + k)] = diatonicChord(root, mode);
      }
    }
  }
  return plan;
}

/// @brief Spell every bar acting as a dominant with its seventh.
///
/// A major triad whose root lies a fifth above the next bar's root is that bar's
/// dominant, and a dominant states its function through the tritone between its
/// third and its seventh. The flag is DERIVED from the progression rather than
/// declared per bar, so a plan with no fifth-fall stays entirely triadic and the
/// secondary dominants the caller pins in front of a related-key entry pick the
/// seventh up on the same rule as the home dominant. Applied to the finished
/// plan, after every pin, so the final V -> I is covered too.
///
/// `triad_only_bars` are the bars of the related-key approach, which keep the
/// plain triad for two separate reasons. The pivot itself is a harmony both keys
/// own, and the seventh is exactly the tone that stops it being shared -- G7
/// belongs to C alone, so spelling the C-to-G pivot that way would deny the
/// modulation the common chord it turns on. The secondary dominant in front of
/// it is chromatic already through its third; giving it a seventh as well would
/// claim a four-tone chromatic chord over a bar whose voices are verbatim
/// thematic material, chosen without reference to it -- and where the subject
/// and its countersubject happen to share that pitch class, the spelling turns
/// their plain octave into a doubled seventh that no voice can be moved off.
void markDominantSevenths(std::vector<ChordSpec>& plan, const std::vector<int>& triad_only_bars,
                          Mode mode) {
  for (std::size_t bar = 0; bar + 1 < plan.size(); ++bar) {
    if (plan[bar].minor)
      continue;
    if (std::find(triad_only_bars.begin(), triad_only_bars.end(), static_cast<int>(bar)) !=
        triad_only_bars.end())
      continue;
    const int resolution = (plan[bar].root_pc + 5) % 12;
    if (plan[bar + 1].root_pc % 12 != resolution)
      continue;
    // The seventh must belong to the working scale. Every accompaniment line in
    // this form is diatonic, so a chromatic seventh would not be a tone they can
    // reach -- it would only be a chord tone nothing plays. It is the tonic that
    // this excludes: I falling to IV is a fifth-fall like any other, but its
    // seventh is the flat seventh degree, which turns the home chord into a
    // secondary dominant and takes the figuration out of the key.
    if (!detail::inScale(detail::chordSeventhPc(plan[bar]), mode))
      continue;
    plan[bar].seventh = true;
  }
}

/// @brief Emit the HarmonicPlan ChordEvents from a per-bar chord plan.
void emitHarmony(HarnessFixture& out, const std::vector<ChordSpec>& plan, Mode mode, int base_bar) {
  out.harmony.tonic_pc = 0;
  out.harmony.is_minor = (mode == Mode::Minor);
  for (std::size_t bar = 0; bar < plan.size(); ++bar) {
    ChordEvent chord;
    chord.start_tick = barTick(base_bar + static_cast<int>(bar));
    chord.root_pc = plan[bar].root_pc;
    if (plan[bar].seventh) {
      chord.quality = plan[bar].minor ? ChordQuality::Minor7 : ChordQuality::Dominant7;
    } else {
      chord.quality = plan[bar].minor ? ChordQuality::Minor : ChordQuality::Major;
    }
    out.harmony.chords.push_back(chord);
  }
}

bool isPivotCapableRelatedKey(std::uint8_t key_pc, Mode mode) {
  if (mode == Mode::Minor) {
    return key_pc == 7 || key_pc == 5;
  }
  return key_pc == 7 || key_pc == 9 || key_pc == 5;
}

bool isRelatedKeyMinor(std::uint8_t key_pc, Mode mode) {
  if (mode == Mode::Minor) {
    return key_pc == 5;
  }
  return key_pc == 9;
}

RomanNumeral relatedKeyDegree(std::uint8_t key_pc, Mode mode) {
  if (mode == Mode::Minor) {
    return key_pc == 5 ? RomanNumeral::IV : RomanNumeral::V;
  }
  switch (key_pc) {
    case 9:
      return RomanNumeral::VI;
    case 5:
      return RomanNumeral::IV;
    default:
      return RomanNumeral::V;
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
  const int fig_offset = static_cast<int>(req.seed % 4);

  // Qualified-catalog subject index (character class + seed) -> the V0-band
  // subject pattern and its paired rhythm row.
  const bool minor_mode = (mode == Mode::Minor);
  const std::uint8_t slot = subjectIndexFor(req.character, minor_mode, req.seed);
  const std::array<std::uint8_t, 16>& subj_pat =
      minor_mode ? kSubjectCatalogMinor[slot] : kSubjectCatalogMajor[slot];
  const std::array<Tick, 16>& subj_rhythm =
      minor_mode ? kSubjectCatalogMinorRhythms[slot] : kSubjectCatalogMajorRhythms[slot];

  // --- Length partition. ---
  // Exposition: 12 bars normally, compressed to 8 only below the public
  // 20-bar minimum. Every shipped fugue therefore contains all three entries.
  const bool short_form = bars < 20;
  const int exposition_bars = short_form ? 8 : 12;
  constexpr int coda_bars = 4;

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
  int climax_cycle =
      development_windows.empty()
          ? -1
          : (variable_entry_schedule && !entry_cycles.empty()
                 ? entry_cycles[static_cast<std::size_t>(
                       std::min<int>(static_cast<int>(entry_cycles.size()) - 1,
                                     static_cast<int>(entry_cycles.size()) * 4 / 5))]
                 : std::min<int>(static_cast<int>(development_windows.size()) - 1,
                                 static_cast<int>(development_windows.size()) * 4 / 5));
  // Pedal cycle: the last middle-entry cycle before the coda. The dominant
  // pedal is proportional to development weight rather than an absolute bar
  // count -- any development with at least two entry-carrying cycles has room
  // for a dominant prolongation before the home return, so the pedal appears
  // whenever entry_cycles.size() >= 2 (this subsumes the old N >= 32 rule: the
  // fixed 8-bar schedule cannot pack two entry cycles below 30 bars, so every
  // formerly pedalled length is unchanged and only the 30..31-bar band gains a
  // pedal).
  const int pedal_cycle = (entry_cycles.size() < 2 || development_windows.empty())
                              ? -1
                              : (variable_entry_schedule && !entry_cycles.empty()
                                     ? entry_cycles.back()
                                     : static_cast<int>(development_windows.size()) - 1);
  // In the smallest two-entry development, the nominal 80% climax lands on
  // the same final entry as the dominant pedal. The pedal owns that pre-coda
  // slot; move the climax to the preceding entry so both devices remain
  // present instead of silently suppressing the pedal branch below.
  if (pedal_cycle >= 0 && climax_cycle == pedal_cycle && entry_cycles.size() >= 2) {
    climax_cycle = entry_cycles[entry_cycles.size() - 2];
  }
  // The second entry-carrying development cycle states the subject inverted --
  // one mirrored entry per fugue, and only when the development is long enough
  // to spare a canonical restatement (>= 3 entry cycles). The stretto climax
  // and the dominant-pedal cycle keep the upright subject.
  int inverted_cycle = -1;
  if (entry_cycles.size() >= 3) {
    const int candidate = entry_cycles[1];
    if (candidate != climax_cycle && candidate != pedal_cycle) {
      inverted_cycle = candidate;
    }
  }
  // Second stretto moment: a development long enough to spare a canonical
  // restatement (>= 4 entry cycles) may reprise the stretto once more just
  // before the pedal/coda. Roughly one fugue in three does so -- kStrettoRate,
  // the corpus rate -- gated deterministically on the seed (23 keeps the
  // modulus prime and the truncated rate approximately corpus-accurate). The
  // dominant pedal always owns its slot, so the candidate is chosen strictly
  // before pedal_cycle and never coincides with the climax or inverted cycle.
  int second_stretto_cycle = -1;
  if (entry_cycles.size() >= 4 &&
      static_cast<int>(req.seed % 23) < static_cast<int>(kStrettoRate * 23)) {
    for (int idx = static_cast<int>(entry_cycles.size()) - 1; idx >= 0; --idx) {
      const int cyc = entry_cycles[static_cast<std::size_t>(idx)];
      if (pedal_cycle >= 0 && cyc >= pedal_cycle) {
        continue;  // the pedal owns the last entry cycle.
      }
      if (cyc == climax_cycle || cyc == inverted_cycle) {
        continue;
      }
      second_stretto_cycle = cyc;
      break;
    }
  }
  // Long fugues reserve two otherwise ordinary entry cycles for rhythmic
  // subject variants. Augmentation states the first half of the subject at
  // double values; diminution states the complete subject twice at half
  // values. Special cycles (inversion, stretto climax/reprise, dominant pedal)
  // retain their own rhetoric.
  int augmentation_cycle = -1;
  int diminution_cycle = -1;
  if (bars >= 96) {
    for (int cycle : entry_cycles) {
      if (cycle == climax_cycle || cycle == pedal_cycle || cycle == inverted_cycle ||
          cycle == second_stretto_cycle) {
        continue;
      }
      if (augmentation_cycle < 0) {
        augmentation_cycle = cycle;
      } else {
        diminution_cycle = cycle;
        break;
      }
    }
  }

  // --- Ornament/expression metadata (fixture fields only, never a note). ---
  // The exposition's final bar closes the fugue's first section: the ornament
  // pass marks it with a section-cadence trill. The climax window is the
  // climax cycle's real bar span (entry + episode), so decoration intensifies
  // exactly where the texture peaks (stretto / densest accompaniment).
  out.section_cadence_ticks.push_back(barTick(first_bar + exposition_bars - 1));
  if (climax_cycle >= 0) {
    const DevelopmentWindow& climax_window =
        development_windows[static_cast<std::size_t>(climax_cycle)];
    const int climax_first =
        climax_window.has_entry ? climax_window.entry_start : climax_window.episode_start;
    const int climax_end = climax_window.episode_start + climax_window.episode_len;
    out.climax_start_tick = barTick(first_bar + climax_first);
    out.climax_end_tick = barTick(first_bar + climax_end);
  }

  // Per-bar chord plan for the whole fugue (section-relative bars), derived
  // from the tonal stations the layout above fixes. The final two bars are
  // pinned to a V -> I authentic cadence so the explicit cadential bass
  // (dominant then tonic) is harmonically consistent and the
  // cadence_voice_leading rule reads a true V->I.
  std::vector<ChordSpec> plan =
      buildFugueTonalPlan(bars, mode, exposition_bars, coda_bars, development_windows, pedal_cycle);
  // Prepare related-key entries with V/X -> X in the two bars immediately
  // before their common-chord pivot. The target X is diatonic in the home key,
  // so the shared pivot remains valid while the preceding secondary dominant
  // gives the approach genuine directional harmonic force.
  for (int cycle = 0; cycle < static_cast<int>(development_windows.size()); ++cycle) {
    const DevelopmentWindow& window = development_windows[static_cast<std::size_t>(cycle)];
    const std::uint8_t key_pc = middleEntryKeyPc(cycle % 3, mode);
    if (!window.has_entry || cycle == pedal_cycle || !isPivotCapableRelatedKey(key_pc, mode) ||
        window.entry_start < 2) {
      continue;
    }
    plan[static_cast<std::size_t>(window.entry_start - 2)] =
        ChordSpec{static_cast<std::uint8_t>((key_pc + 7) % 12), false};
    plan[static_cast<std::size_t>(window.entry_start - 1)] =
        ChordSpec{key_pc, isRelatedKeyMinor(key_pc, mode)};
  }
  const bool picardy = mode == Mode::Minor && detail::usePicardy(req.seed);
  plan[static_cast<std::size_t>(bars - 2)] = ChordSpec{7, false};  // V (G major).
  plan[static_cast<std::size_t>(bars - 1)] =
      ChordSpec{0, mode == Mode::Minor && !picardy};  // I / Picardy I.
  // The related-key approach: the secondary dominant the loop above pinned and
  // the pivot the modulation pass below declares. Both are derived here from the
  // same conditions those passes use, so the chord spelling cannot drift apart
  // from the modulation it belongs to.
  std::vector<int> triad_only_bars;
  for (int cycle = 0; cycle < static_cast<int>(development_windows.size()); ++cycle) {
    const DevelopmentWindow& window = development_windows[static_cast<std::size_t>(cycle)];
    if (!window.has_entry || cycle == pedal_cycle)
      continue;
    if (!isPivotCapableRelatedKey(middleEntryKeyPc(cycle % 3, mode), mode))
      continue;
    triad_only_bars.push_back(window.entry_start);
    if (window.entry_start >= 2)
      triad_only_bars.push_back(window.entry_start - 2);
  }
  markDominantSevenths(plan, triad_only_bars, mode);
  emitHarmony(out, plan, mode, first_bar);
  for (ChordEvent& chord : out.harmony.chords) {
    if (chord.start_tick == barTick(first_bar + bars - 1)) {
      chord.is_picardy = picardy;
    }
  }

  for (int cycle = 0; cycle < static_cast<int>(development_windows.size()); ++cycle) {
    const DevelopmentWindow& window = development_windows[static_cast<std::size_t>(cycle)];
    const std::uint8_t key_pc = middleEntryKeyPc(cycle % 3, mode);
    if (!window.has_entry || cycle == pedal_cycle || !isPivotCapableRelatedKey(key_pc, mode) ||
        window.entry_start < 2) {
      continue;
    }
    const Tick secondary_tick = barTick(first_bar + window.entry_start - 2);
    const Tick target_tick = secondary_tick + kTicksPerBar;
    for (ChordEvent& chord : out.harmony.chords) {
      if (chord.start_tick == secondary_tick) {
        chord.degree = RomanNumeral::V;
        chord.function = HarmonicFunction::D;
        chord.has_degree = true;
        chord.has_secondary_of = true;
        chord.secondary_of = relatedKeyDegree(key_pc, mode);
      } else if (chord.start_tick == target_tick) {
        chord.degree = relatedKeyDegree(key_pc, mode);
        chord.has_degree = true;
      }
    }
  }

  // Related-key statements begin on a harmony shared by the home key and the
  // local key (V/I for G, vi/i for A, IV/i for F). Declare that actual pivot
  // and restore the home key when the four-bar entry closes, so downstream
  // consumers such as OrnamentPass can select local-scale auxiliaries without
  // leaking the temporary tonicization into the following episode.
  for (int cycle = 0; cycle < static_cast<int>(development_windows.size()); ++cycle) {
    const DevelopmentWindow& window = development_windows[static_cast<std::size_t>(cycle)];
    if (!window.has_entry || cycle == pedal_cycle) {
      continue;
    }
    const std::uint8_t key_pc = middleEntryKeyPc(cycle % 3, mode);
    if (!isPivotCapableRelatedKey(key_pc, mode)) {
      continue;
    }
    const Tick pivot_tick = barTick(first_bar + window.entry_start);
    out.harmony.modulations.push_back({pivot_tick, 0, key_pc, mode == Mode::Minor,
                                       isRelatedKeyMinor(key_pc, mode), ModulationType::Pivot});
    const Tick return_tick = pivot_tick + 4 * kTicksPerBar;
    if (return_tick < barTick(first_bar + bars)) {
      out.harmony.modulations.push_back({return_tick, key_pc, 0, isRelatedKeyMinor(key_pc, mode),
                                         mode == Mode::Minor, ModulationType::Phrase});
    }
  }
  std::stable_sort(out.harmony.modulations.begin(), out.harmony.modulations.end(),
                   [](const ModulationEvent& left, const ModulationEvent& right) {
                     return left.tick < right.tick;
                   });

  // === EXPOSITION ===========================================================
  // Each thematic statement carries the subject in ONE voice band; at most ONE
  // figuration accompaniment voice is added per bar window (the FigurationCarrier
  // dispatch matches sections by window only, so two sections sharing a window
  // would collide -- a single accompaniment voice per window avoids that).
  const int v0_off = octaveOffsetForBand(subj_pat, 0, 0, kBandLo, kBandHi);
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
    appendScoredCountersubject(source, static_cast<VoiceId>(voice), start, end, kBandLo[voice],
                               kBandHi[voice], mode, out.material.countersubject,
                               asm_ctx.theme_tones);
  };
  // The piece's ONE canonical countersubject, snapshotted from the exposition
  // (re-based to tick 0) once the answer counterline is derived. Every later
  // entry restates THIS line -- octave-invertible, degree-shifted to the entry
  // key -- so the countersubject keeps a single recurring identity the ear can
  // track, instead of a fresh reactive counterline per entry.
  std::vector<MaterialNote> canonical_cs;

  // Degree-shift the canonical countersubject into an entry key, then octave-fit
  // the whole line (one offset, a multiple of 12) into the accompaniment voice
  // band. Returns false -- so the caller falls back to a reactive counterline --
  // when there is no canonical line yet or the shifted line is wider than the
  // band (a voice-band violation the strict register order forbids).
  auto build_restatement = [&](int degree_offset, int voice, Tick anchor_start,
                               std::vector<MaterialNote>* dst) -> bool {
    dst->clear();
    if (canonical_cs.empty()) {
      return false;
    }
    std::vector<int> shifted;
    shifted.reserve(canonical_cs.size());
    int lo = 127;
    int hi = 0;
    int sum = 0;
    for (const auto& note : canonical_cs) {
      const int base = static_cast<int>(note.pitch);
      const int pitch = (degree_offset >= 0) ? scaleUp(base, degree_offset, mode)
                                             : scaleDown(base, -degree_offset, mode);
      shifted.push_back(pitch);
      lo = std::min(lo, pitch);
      hi = std::max(hi, pitch);
      sum += pitch;
    }
    const int mean = sum / static_cast<int>(shifted.size());
    int octave = 0;
    while (mean + octave < kBandLo[voice]) {
      octave += 12;
    }
    while (mean + octave > kBandHi[voice]) {
      octave -= 12;
    }
    if (lo + octave < kBandLo[voice] || hi + octave > kBandHi[voice]) {
      return false;  // no whole-octave position fits the whole line in the band.
    }
    for (std::size_t idx = 0; idx < canonical_cs.size(); ++idx) {
      MaterialNote note;
      note.start_tick = anchor_start + canonical_cs[idx].start_tick;
      note.duration = canonical_cs[idx].duration;
      note.pitch = static_cast<std::uint8_t>(std::clamp(shifted[idx] + octave, 0, 127));
      dst->push_back(note);
    }
    return true;
  };

  // Static compatibility of a candidate countersubject restatement against the
  // entry line it will sound with, over the 4-bar entry window. Both lines are
  // verbatim Material, so the validator skips every dissonance rule on the pair;
  // this vets the two combinations the ear rejects that would otherwise ship
  // unflagged: (a) a sharp dissonance (ic 1/6/11) sustained for a half note or
  // more, and (b) a similar-motion arrival on a perfect interval (ic 0/7) at a
  // shared onset. Band violations are already ruled out by build_restatement.
  auto restatement_compatible = [&](const std::vector<MaterialNote>& counter,
                                    const std::vector<MaterialNote>& entry,
                                    Tick win_start) -> bool {
    constexpr Tick kSlotTick = kTicksPerBeat / 4;  // sixteenth grid.
    const int total_slots = static_cast<int>(barTick(kSubjectBars) / kSlotTick);
    std::vector<int> counter_grid(static_cast<std::size_t>(total_slots), -1);
    std::vector<int> entry_grid(static_cast<std::size_t>(total_slots), -1);
    const auto lay = [&](std::vector<int>& grid, const std::vector<MaterialNote>& notes) {
      for (const auto& note : notes) {
        const Tick rel = note.start_tick - win_start;
        if (rel < 0) {
          continue;
        }
        for (Tick tick = rel; tick < rel + note.duration; tick += kSlotTick) {
          const int slot = static_cast<int>(tick / kSlotTick);
          if (slot >= total_slots) {
            break;
          }
          grid[static_cast<std::size_t>(slot)] = static_cast<int>(note.pitch);
        }
      }
    };
    lay(counter_grid, counter);
    lay(entry_grid, entry);
    // (a) sustained sharp dissonance.
    const int sustain_limit = static_cast<int>((2 * kQuarter) / kSlotTick);  // half note.
    int run = 0;
    for (int slot = 0; slot < total_slots; ++slot) {
      const int cnt = counter_grid[static_cast<std::size_t>(slot)];
      const int ent = entry_grid[static_cast<std::size_t>(slot)];
      bool sharp = false;
      if (cnt >= 0 && ent >= 0) {
        const int interval_class = std::abs(cnt - ent) % 12;
        sharp = (interval_class == 1 || interval_class == 6 || interval_class == 11);
      }
      run = sharp ? run + 1 : 0;
      if (run >= sustain_limit) {
        return false;
      }
    }
    // (b) similar-motion perfect arrival at a shared onset. motion_at returns the
    // direction into `tick` (+1/-1/0), sets the arriving pitch, or -2 when the
    // line does not attack there.
    const auto motion_at = [](const std::vector<MaterialNote>& notes, Tick tick,
                              int* pitch_out) -> int {
      int prev = -1;
      for (const auto& note : notes) {
        if (note.start_tick == tick) {
          *pitch_out = static_cast<int>(note.pitch);
          if (prev < 0) {
            return 0;  // first note: oblique arrival.
          }
          const int diff = static_cast<int>(note.pitch) - prev;
          return diff > 0 ? 1 : (diff < 0 ? -1 : 0);
        }
        prev = static_cast<int>(note.pitch);
      }
      *pitch_out = -1;
      return -2;
    };
    for (const auto& note : counter) {
      int counter_pitch = -1;
      int entry_pitch = -1;
      const int counter_dir = motion_at(counter, note.start_tick, &counter_pitch);
      const int entry_dir = motion_at(entry, note.start_tick, &entry_pitch);
      if (entry_dir == -2 || counter_pitch < 0 || entry_pitch < 0) {
        continue;  // no shared onset here.
      }
      const int interval_class = std::abs(counter_pitch - entry_pitch) % 12;
      const bool perfect = (interval_class == 0 || interval_class == 7);
      const bool similar = (counter_dir != 0 && counter_dir == entry_dir);
      if (similar && perfect) {
        return false;
      }
    }
    return true;
  };

  // Emit the recurring countersubject over an entry window: restate the canonical
  // line degree-shifted to the entry key and octave-fit into `acc_voice`, commit
  // it when the static combination check passes, otherwise fall back to the
  // reactive consonant/contrary counterline derived from the entry itself. The
  // caller pushes the CountersubjectCarrier span in both cases.
  auto emit_recurring_countersubject = [&](int degree_offset, int acc_voice, int win_start_bar,
                                           const std::vector<MaterialNote>& entry_line) {
    const Tick win_start = barTick(win_start_bar);
    const Tick win_end = barTick(win_start_bar + kSubjectBars);
    std::vector<MaterialNote> restated;
    if (build_restatement(degree_offset, acc_voice, win_start, &restated) &&
        restatement_compatible(restated, entry_line, win_start)) {
      for (const auto& note : restated) {
        addNote(out.material.countersubject, note.start_tick, note.duration,
                static_cast<int>(note.pitch));
        asm_ctx.theme_tones.record(note.start_tick, static_cast<VoiceId>(acc_voice),
                                   static_cast<int>(note.pitch), note.duration);
      }
    } else {
      append_countersubject_from(entry_line, acc_voice, win_start, win_end);
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
  // The third entry's line: the subject an octave down, octave-fit into the V2
  // band. Built through one definition so the canonical countersubject can be
  // vetted against the very line it will later be restated over, and the line
  // stamped at the entry cannot drift from the line that vetted it.
  auto third_entry_seed_at = [&](int base_bar) {
    const int off = octaveOffsetForBand(subj_pat, -12, 2, kBandLo, kBandHi);
    std::vector<MaterialNote> seed;
    seed.reserve(kSubjectNotes);
    Tick cursor = barTick(base_bar);
    for (int note = 0; note < kSubjectNotes; ++note) {
      MaterialNote mn;
      mn.start_tick = cursor;
      mn.duration = subj_rhythm[static_cast<std::size_t>(note)];
      mn.pitch = static_cast<std::uint8_t>(std::clamp(
          static_cast<int>(subj_pat[static_cast<std::size_t>(note)]) - 12 + off, 0, 127));
      seed.push_back(mn);
      cursor += mn.duration;
    }
    return seed;
  };

  stamp_subject(first_bar + 0, v0_off, 0);
  out.material.canonical_subject_note_count = kSubjectNotes;
  pushSpan(asm_ctx, 0, first_bar + 0, first_bar + 3, VoiceIntent::SubjectCarrier);

  // Answer (V1, bars 4-7) = real answer (subject - P4) lowered into the V1 band.
  const int answer_off = octaveOffsetForBand(subj_pat, -5, 1, kBandLo, kBandHi);
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
  const std::size_t cs_canonical_base = out.material.countersubject.size();
  const std::vector<MaterialNote>& answer_source =
      use_tonal_answer ? out.material.tonal_answer : out.material.answer;
  const Tick cs_start = barTick(first_bar + 4);
  const Tick cs_end = barTick(first_bar + 8);
  // Derive the line twice and keep the better one. The battuta-avoiding
  // derivation is the stronger counterpoint, but this same line is what every
  // later entry restates, and a line whose restatement no longer combines costs
  // the fugue its one recurring counter-identity -- the worse loss of the two.
  //
  // Avoiding a battuta means taking a WIDER leap in place of the one that
  // arrived at the octave, so the avoiding line reaches further than the plain
  // one; a restatement has to octave-fit the whole line into a lower voice's
  // band, and a line that reaches further stops fitting. The trial is therefore
  // confined to the ambit the plain line already occupies, so it can only trade
  // pitches inside that reach, never widen it. It is then adopted only when its
  // degree-3 restatement still combines with the third entry it will meet.
  bool battuta_free_adopted = false;
  std::vector<MaterialNote> plain_cs;
  ThemeToneRegistry plain_tones = asm_ctx.theme_tones;
  appendScoredCountersubject(answer_source, 0, cs_start, cs_end, kBandLo[0], kBandHi[0], mode,
                             plain_cs, plain_tones);
  // Battutas the realized line forms against the entry it accompanies, read at
  // the line's own onsets against whatever the entry sounds under them. The
  // narrowed trial band changes the candidate set for EVERY note, not only the
  // ones that arrived at an octave, so the trial is adopted only when it wins
  // on the count it exists to lower.
  const auto battuta_count = [&](const std::vector<MaterialNote>& counter) {
    int count = 0;
    int prev_counter = -1;
    int prev_source = -1;
    for (const MaterialNote& note : counter) {
      const int source = soundingMaterialPitch(answer_source, note.start_tick);
      const int pitch = static_cast<int>(note.pitch);
      if (formsBattuta(prev_counter, pitch, prev_source, source)) {
        ++count;
      }
      prev_counter = pitch;
      prev_source = source;
    }
    return count;
  };
  if (!short_form && !plain_cs.empty()) {
    int plain_lo = 127;
    int plain_hi = 0;
    for (const MaterialNote& note : plain_cs) {
      plain_lo = std::min(plain_lo, static_cast<int>(note.pitch));
      plain_hi = std::max(plain_hi, static_cast<int>(note.pitch));
    }
    // The reach the restatement can still absorb: it octave-fits the whole line
    // into the accompaniment band, so what it cannot take is SPAN, not
    // position. Whatever the plain line leaves unused of that span is the room
    // the trial may spend on a wider leap away from an octave arrival.
    const int slack = std::max(0, ((kBandHi[1] - kBandLo[1]) - (plain_hi - plain_lo)) / 2);
    ThemeToneRegistry trial_tones = asm_ctx.theme_tones;
    std::vector<MaterialNote> trial_cs;
    appendScoredCountersubject(answer_source, 0, cs_start, cs_end,
                               std::max(kBandLo[0], plain_lo - slack),
                               std::min(kBandHi[0], plain_hi + slack), mode, trial_cs, trial_tones,
                               /*avoid_battuta=*/true);
    if (!trial_cs.empty() && battuta_count(trial_cs) < battuta_count(plain_cs)) {
      const Tick origin = trial_cs.front().start_tick;
      canonical_cs.clear();
      for (const MaterialNote& src : trial_cs) {
        MaterialNote note;
        note.start_tick = src.start_tick - origin;
        note.duration = src.duration;
        note.pitch = src.pitch;
        canonical_cs.push_back(note);
      }
      const Tick third_start = barTick(first_bar + 8);
      std::vector<MaterialNote> restated;
      if (build_restatement(3, 1, third_start, &restated) &&
          restatement_compatible(restated, third_entry_seed_at(first_bar + 8), third_start)) {
        out.material.countersubject.insert(out.material.countersubject.end(), trial_cs.begin(),
                                           trial_cs.end());
        asm_ctx.theme_tones = trial_tones;
        battuta_free_adopted = true;
      }
      // The snapshot below rebuilds canonical_cs from whichever line landed.
      canonical_cs.clear();
    }
  }
  if (!battuta_free_adopted) {
    out.material.countersubject.insert(out.material.countersubject.end(), plain_cs.begin(),
                                       plain_cs.end());
    asm_ctx.theme_tones = plain_tones;
  }
  pushSpan(asm_ctx, 0, first_bar + 4, first_bar + 7, VoiceIntent::CountersubjectCarrier);
  // Snapshot the just-derived answer counterline (re-based to tick 0) as the
  // piece's canonical countersubject for every later restatement.
  if (out.material.countersubject.size() > cs_canonical_base) {
    const Tick origin = out.material.countersubject[cs_canonical_base].start_tick;
    for (std::size_t idx = cs_canonical_base; idx < out.material.countersubject.size(); ++idx) {
      const MaterialNote& src = out.material.countersubject[idx];
      MaterialNote note;
      note.start_tick = src.start_tick - origin;
      note.duration = src.duration;
      note.pitch = src.pitch;
      canonical_cs.push_back(note);
    }
  }
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
      entry.tonal_base_interval_semis = answer_total - v0_off;
      entry.has_tonal_base_interval = true;
    }
    out.material.imitation_entries.push_back(entry);
  }

  if (!short_form) {
    // Third entry (V2, bars 8-11) = subject - P8 lowered into the V2 band.
    const int third_off = octaveOffsetForBand(subj_pat, -12, 2, kBandLo, kBandHi);
    stamp_subject(first_bar + 8, -12 + third_off, 2);
    pushSpan(asm_ctx, 2, first_bar + 8, first_bar + 11, VoiceIntent::SubjectCarrier);
    // V1 countersubject plus V0 figuration makes the third entry a real 3-voice
    // texture instead of a two-voice carrier with a resting middle voice.
    const std::vector<MaterialNote> third_entry_seed = third_entry_seed_at(first_bar + 8);
    // The canonical countersubject was derived against the ANSWER -- the
    // subject a fifth above the home statement -- so restating it against the
    // home-pitch third entry must shift by that same relative interval: up a
    // fourth in degree space (the diatonic equivalent of down a fifth, before
    // the whole-line octave fit). This preserves the vertical relations already
    // sounded in the answer window; a degree-0 restatement keeps the CS's
    // absolute pitch but shifts every interval against the entry by a fifth,
    // flipping half the consonances into sustained sevenths. Restating it here
    // makes the countersubject's identity audible from the exposition on; the
    // static check falls back to a reactive line if dissonant.
    emit_recurring_countersubject(3, 1, first_bar + 8, third_entry_seed);
    pushSpan(asm_ctx, 1, first_bar + 8, first_bar + 11, VoiceIntent::CountersubjectCarrier);
    // V0 figuration rides above the V2 third entry. The exposition's section
    // cadence lands on this span's final bar, so its second half closes on a
    // held mid-bar anchor -- the strong-beat top note the ornament pass needs
    // for the mandatory section-cadence trill (the running eighth wave carries
    // no such note otherwise).
    addFigurationSpan(asm_ctx, 0, first_bar + 8, first_bar + 11, plan, first_bar, mode, 2,
                      fig_offset, /*is_pedal_prep=*/false, /*cadential_close_last_bar=*/true);
  }

  // === DEVELOPMENT ==========================================================
  // One MiddleEntryDecl per carrying voice; each decl holds all of that voice's
  // middle-entry notes and the span windows slice them.
  std::array<MiddleEntryDecl, 3> middle_decls;
  std::array<bool, 3> middle_used = {false, false, false};

  // Commit a single stretto follower against a middle-entry leader, trying the
  // canon configurations in preference order (the densest 1-bar octave canon
  // first, then a 2-bar delay, then a fifth-up canon at each delay). The
  // follower restates the exposition subject in the leader's key, octave-fit
  // into its own band, truncated at the leader window end, and is vetted for a
  // sustained sharp dissonance before it is committed. Returns true and reports
  // the committed delay / total semitone shift, so a further follower can pile
  // on after it. The climax cycle and the corpus-rate-gated pre-coda cycle both
  // drive this same path (byte-identical to the former inline climax logic).
  auto place_stretto_follower = [&](const std::array<std::uint8_t, 16>& leader_pat,
                                    int leader_total, int leader_carry_voice, int follower_voice,
                                    int entry_bar, int key_semis, int density, int* out_delay,
                                    int* out_total) -> bool {
    struct StrettoConfig {
      int delay_bars;
      int extra_semis;
    };
    constexpr std::array<StrettoConfig, 4> kStrettoConfigs = {{{1, 0}, {2, 0}, {1, 7}, {2, 7}}};
    const int follower_key_semis = (leader_carry_voice == 1) ? 0 : key_semis;
    // Read every configuration before committing one. The configurations differ
    // in the delay and in whether the canon sounds at the octave or at the
    // fifth, and both of those decide how often the two statements step into a
    // perfect interval together -- which the preference order has no way to see,
    // and which no later pass can answer for, since a follower that is re-aimed
    // is no longer an imitation.
    //
    // A window states its canon only if some configuration keeps clear of a
    // sustained sharp dissonance AND some configuration keeps clear of a true
    // parallel; failing either, the canon is not worth what it would cost and
    // the window is left to its ordinary texture. Among the rest the two faults
    // are ranked rather than pooled, and the parallel outranks the dissonance: a
    // beat-long second between the two theme statements is a matter of degree,
    // while the parallel is the cardinal prohibition, so a configuration that
    // sounds one is refused even when it is the only quiet one on offer. The
    // preference order (densest canon first) breaks the remaining ties, so a
    // window whose configurations are equally clean commits exactly what it
    // committed before.
    struct ConfigRead {
      bool considered = false;
      int parallels = 0;
      int sustains_sharp = 0;
      int follower_total = 0;
    };
    std::array<ConfigRead, kStrettoConfigs.size()> reads{};
    bool any_quiet = false;
    bool any_parallel_free = false;
    for (std::size_t idx = 0; idx < kStrettoConfigs.size(); ++idx) {
      const StrettoConfig& candidate = kStrettoConfigs[idx];
      if (leader_carry_voice == 1 && candidate.extra_semis != 0) {
        continue;  // no in-set fifth-up canon against the modal vi leader.
      }
      const int candidate_semis = follower_key_semis + candidate.extra_semis;
      const int candidate_off =
          octaveOffsetForBand(subj_pat, candidate_semis, follower_voice, kBandLo, kBandHi);
      const int candidate_total = candidate_semis + candidate_off;
      const StrettoOverlapProfile profile =
          strettoOverlapProfile(leader_pat, leader_total, subj_pat, candidate_total, subj_rhythm,
                                subj_rhythm, candidate.delay_bars, kSubjectBars);
      ConfigRead& read = reads[idx];
      read.considered = true;
      read.parallels = profile.parallel_perfects;
      read.sustains_sharp = profile.sustains_sharp ? 1 : 0;
      read.follower_total = candidate_total;
      any_quiet = any_quiet || !profile.sustains_sharp;
      any_parallel_free = any_parallel_free || profile.parallel_perfects == 0;
    }
    int best_index = -1;
    int best_follower_total = 0;
    if (any_quiet && any_parallel_free) {
      for (std::size_t idx = 0; idx < reads.size(); ++idx) {
        const ConfigRead& read = reads[idx];
        if (!read.considered) {
          continue;
        }
        if (best_index >= 0) {
          const ConfigRead& best = reads[static_cast<std::size_t>(best_index)];
          if (read.parallels > best.parallels)
            continue;
          if (read.parallels == best.parallels && read.sustains_sharp >= best.sustains_sharp)
            continue;
        }
        best_index = static_cast<int>(idx);
        best_follower_total = read.follower_total;
      }
    }
    if (best_index >= 0) {
      const StrettoConfig& config = kStrettoConfigs[static_cast<std::size_t>(best_index)];
      const int follower_total = best_follower_total;
      // material.subject[i] == subj_pat[i] + v0_off (the V0 exposition
      // statement), so the validated relation follower[i] == subject[i] +
      // interval requires interval = follower_total - v0_off. This keeps the
      // validator's stretto_overlap_valid verbatim-transposition relation exact
      // while the follower sits in its canon key.
      const int stretto_interval = follower_total - v0_off;
      StrettoDecl stretto;
      stretto.leader_voice = static_cast<VoiceId>(leader_carry_voice);
      stretto.follower_voice = static_cast<VoiceId>(follower_voice);
      stretto.leader_entry_tick = barTick(entry_bar);
      stretto.leader_length_ticks = barTick(kSubjectBars);
      stretto.follower_entry_tick = barTick(entry_bar + config.delay_bars);
      stretto.interval_semis = stretto_interval;
      Tick follower_cursor = barTick(entry_bar + config.delay_bars);
      const Tick follower_end = barTick(entry_bar + kSubjectBars);
      for (int note = 0; note < kSubjectNotes && follower_cursor < follower_end; ++note) {
        MaterialNote mn;
        mn.start_tick = follower_cursor;
        mn.duration =
            std::min(subj_rhythm[static_cast<std::size_t>(note)], follower_end - follower_cursor);
        mn.pitch = static_cast<std::uint8_t>(std::clamp(
            static_cast<int>(subj_pat[static_cast<std::size_t>(note)]) + follower_total, 0, 127));
        stretto.follower_notes.push_back(mn);
        asm_ctx.theme_tones.record(mn.start_tick, static_cast<VoiceId>(follower_voice),
                                   static_cast<int>(mn.pitch), mn.duration);
        follower_cursor += subj_rhythm[static_cast<std::size_t>(note)];
      }
      out.material.stretto_entries.push_back(stretto);
      pushSpan(asm_ctx, static_cast<VoiceId>(follower_voice), entry_bar + config.delay_bars,
               entry_bar + 3, VoiceIntent::StrettoCarrier);
      if (config.delay_bars > 1) {
        // Keep the follower's voice sounding while it waits for the delayed
        // entrance, so the window does not thin before the canon arrives.
        addFigurationSpan(asm_ctx, static_cast<VoiceId>(follower_voice), entry_bar,
                          entry_bar + config.delay_bars - 1, plan, first_bar, mode, density,
                          fig_offset);
      }
      if (out_delay != nullptr) {
        *out_delay = config.delay_bars;
      }
      if (out_total != nullptr) {
        *out_total = follower_total;
      }
      return true;
    }
    return false;
  };

  for (int cycle = 0; cycle < static_cast<int>(development_windows.size()); ++cycle) {
    const DevelopmentWindow& window = development_windows[static_cast<std::size_t>(cycle)];
    const bool half_cycle = !window.has_entry;
    const ArcPoint arc = req.arc(
        static_cast<std::size_t>(std::min<int>(cycle, static_cast<int>(req.cycle_count) - 1)));
    // Entries rotate through the voices, EXCEPT in the pedal cycle: the
    // dominant pedal prepares the home-key return, so that cycle's entry is
    // ALWAYS re-carried by V0 -- a dominant-key (G major) statement over the
    // dominant pedal is the textbook pedal preparation, and it matches the
    // window's pinned V/I64 chord alternation. Letting the vi rotation stand
    // deflects to the relative minor at exactly the wrong moment, and letting
    // the IV rotation stand stamps an F-major subject (with its Bb) against
    // the held G AND pushes the pedal into the middle voice while the bass
    // walks underneath it -- the seconds-family grind that made every
    // pedal window the piece's roughest four bars.
    const int rotation_voice = cycle % 3;
    const int carry_voice = (cycle == pedal_cycle && rotation_voice != 0) ? 0 : rotation_voice;
    // Accompaniment density rises with the arc; figuration accompanies the
    // highest non-carrying voice (one accompaniment voice per window).
    const int acc_voice = (carry_voice == 0) ? 1 : 0;
    const int density = std::clamp<int>(1 + arc.density_tier, 1, 2);

    if (!half_cycle) {
      // --- Middle entry (4 bars): the selected form subject restated in the
      //     carrying voice. Major pieces retain the V / vi / IV realization;
      //     minor pieces move the minor catalog by home-minor scale degrees to
      //     v / III / iv, preserving the minor pitch collection. ---
      const int me_start = first_bar + window.entry_start;  // absolute.
      const int key_semis = kVoiceKeySemis[static_cast<std::size_t>(carry_voice)];
      // The inverted development cycle mirrors the selected catalog line in
      // degree space before its related-key realization.
      std::array<std::uint8_t, 16> me_pat = subj_pat;
      const bool inverted_here = (cycle == inverted_cycle);
      const bool augmented_here = (cycle == augmentation_cycle);
      const bool diminished_here = (cycle == diminution_cycle);
      if (inverted_here) {
        me_pat = invertDiatonicLine(me_pat, mode);
      }
      std::array<std::uint8_t, 16> me_real;
      for (int note = 0; note < kSubjectNotes; ++note) {
        const int base = static_cast<int>(me_pat[static_cast<std::size_t>(note)]);
        const int realized =
            mode == Mode::Minor
                ? detail::scaleUp(base,
                                  kMinorVoiceDegreeShift[static_cast<std::size_t>(carry_voice)],
                                  Mode::Minor)
                : (carry_voice == 1 ? detail::scaleUp(base, 5, Mode::Major) : base + key_semis);
        me_real[static_cast<std::size_t>(note)] = static_cast<std::uint8_t>(realized);
      }
      const int me_off = octaveOffsetForBand(me_real, 0, carry_voice, kBandLo, kBandHi);
      MiddleEntryDecl& decl = middle_decls[static_cast<std::size_t>(carry_voice)];
      decl.voice = static_cast<VoiceId>(carry_voice);
      decl.related_key_pc = middleEntryKeyPc(carry_voice, mode);
      Tick me_cursor = barTick(me_start);
      const Tick me_end = barTick(me_start + kSubjectBars);
      int note = 0;
      while (me_cursor < me_end && (diminished_here || note < kSubjectNotes)) {
        const int source_note = note % kSubjectNotes;
        Tick duration = subj_rhythm[static_cast<std::size_t>(source_note)];
        if (augmented_here)
          duration *= 2;
        if (diminished_here)
          duration = std::max<Tick>(1, duration / 2);
        duration = std::min(duration, me_end - me_cursor);
        MaterialNote mn;
        mn.start_tick = me_cursor;
        mn.duration = duration;
        mn.pitch = static_cast<std::uint8_t>(std::clamp(
            static_cast<int>(me_real[static_cast<std::size_t>(source_note)]) + me_off, 0, 127));
        decl.notes.push_back(mn);
        asm_ctx.theme_tones.record(mn.start_tick, static_cast<VoiceId>(carry_voice),
                                   static_cast<int>(mn.pitch), mn.duration);
        me_cursor += mn.duration;
        ++note;
      }
      std::uint8_t transform =
          static_cast<std::uint8_t>(motif_ops::EpisodeMotifTransform::Original);
      if (inverted_here)
        transform = static_cast<std::uint8_t>(motif_ops::EpisodeMotifTransform::Invert);
      else if (augmented_here)
        transform = static_cast<std::uint8_t>(motif_ops::EpisodeMotifTransform::Augment);
      else if (diminished_here)
        transform = static_cast<std::uint8_t>(motif_ops::EpisodeMotifTransform::Diminish);
      if (transform != static_cast<std::uint8_t>(motif_ops::EpisodeMotifTransform::Original)) {
        decl.transform_regions.push_back({barTick(me_start), me_end, transform});
      }
      middle_used[static_cast<std::size_t>(carry_voice)] = true;
      pushSpan(asm_ctx, static_cast<VoiceId>(carry_voice), me_start, me_start + 3,
               VoiceIntent::MiddleEntryCarrier);

      // Stretto in the climax cycle: a subject statement in the accompaniment
      // voice, overlapping the leader. The follower restates the subject in the
      // leader's key, octave-fit into the follower's band, so the overlap forms
      // a single-key canon instead of clashing bi-tonally. When the leader is
      // the vi entry -- a diatonic degree shift whose pitch set is the home
      // scale -- the only real transpositions inside that set are home-key
      // statements, so place_stretto_follower drops the fifth-up configs there
      // (their F# would strike a false relation against the leader's F natural).
      // A committed follower replaces the figuration accompaniment for this
      // window; when no configuration qualifies the stretto is dropped and the
      // window keeps its normal accompaniment below.
      const bool climax = (cycle == climax_cycle);
      bool stretto_placed = false;
      bool second_stretto_placed = false;
      bool pre_coda_stretto_placed = false;
      if (climax) {
        const int follower_voice = acc_voice;
        int first_delay = 0;
        int first_total = 0;
        stretto_placed = place_stretto_follower(me_real, me_off, carry_voice, follower_voice,
                                                me_start, mode == Mode::Minor ? 0 : key_semis,
                                                density, &first_delay, &first_total);
        if (stretto_placed) {
          // Three-voice stretto: pile a SECOND follower into the remaining voice
          // (neither the leader nor the first follower) so the climax states the
          // subject in all three voices at staggered delays -- a maestrale
          // pile-up. The three subject statements OWN the texture here, so this
          // voice's normal accompaniment (bass support / middle fill / recurring
          // CS) is suppressed below via second_stretto_placed. The second
          // follower must enter after the first (delay 2 or 3, always > the
          // first delay) and is vetted for a sustained sharp dissonance TWICE --
          // against the leader and against the first follower -- both of which
          // must pass. As with the first follower, the vi leader admits only the
          // in-set (home) transposition, so its fifth-up configs are skipped.
          const int third_voice = 3 - carry_voice - follower_voice;
          struct SecondConfig {
            int delay_bars;
            int extra_semis;
          };
          constexpr std::array<SecondConfig, 4> kSecondConfigs = {{{2, 0}, {2, 7}, {3, 0}, {3, 7}}};
          const int second_key_semis = (carry_voice == 1) ? 0 : key_semis;
          // Pile up a THIRD statement only when the committed two-voice canon
          // is itself clean: when the leader/first-follower overlap already
          // runs the seconds family on more than a quarter of its shared slots,
          // a third line can only thicken that wash, so the window keeps the
          // two-voice stretto.
          const StrettoOverlapProfile first_profile =
              strettoOverlapProfile(me_real, me_off, subj_pat, first_total, subj_rhythm,
                                    subj_rhythm, first_delay, kSubjectBars);
          const bool first_canon_clean =
              4 * first_profile.broad_sharp_slots <= first_profile.overlap_slots;
          // The third statement is a FALSE ENTRY: the subject's one-bar head
          // only. A full third statement of this catalog's subjects against
          // themselves at a 1-3 bar delay always sustains the seconds family
          // somewhere in the 3-4 shared bars, so a complete triple canon is
          // structurally unavailable; the head quotation piling in late is
          // the idiomatic maestrale gesture that stays vettable. Zero-length
          // tail durations keep the profile scan to the head's single bar.
          std::array<Tick, 16> head_rhythm{};
          int head_notes = 0;
          Tick head_span = 0;
          while (head_notes < kSubjectNotes && head_span < kTicksPerBar) {
            head_rhythm[static_cast<std::size_t>(head_notes)] =
                subj_rhythm[static_cast<std::size_t>(head_notes)];
            head_span += subj_rhythm[static_cast<std::size_t>(head_notes)];
            ++head_notes;
          }
          // Vet the head against the leader AND the first follower: no
          // sustained sharp dissonance, and the seconds family (ic
          // 1/2/6/10/11) on at most a quarter of the shared slots -- the
          // transient wash is what makes a pile-up read as mud instead of
          // tension. Failing every config keeps the two-voice stretto.
          //
          // Among the configurations that clear those, the one whose head steps
          // into the fewest perfect intervals with either line already sounding
          // is committed; the preference order breaks ties. The head is a third
          // verbatim statement of the same material, so it meets both other
          // lines on the same terms the pair below already answers for.
          int best_second = -1;
          int best_second_parallels = 0;
          int best_second_total = 0;
          for (std::size_t idx = 0; first_canon_clean && idx < kSecondConfigs.size(); ++idx) {
            const SecondConfig& candidate = kSecondConfigs[idx];
            if (candidate.delay_bars <= first_delay) {
              continue;  // the pile-up requires a later entrance than the first.
            }
            if (carry_voice == 1 && candidate.extra_semis != 0) {
              continue;  // no in-set fifth-up canon against the modal vi leader.
            }
            const int candidate_semis = second_key_semis + candidate.extra_semis;
            const int candidate_off =
                octaveOffsetForBand(subj_pat, candidate_semis, third_voice, kBandLo, kBandHi);
            const int candidate_total = candidate_semis + candidate_off;
            const StrettoOverlapProfile vs_leader =
                strettoOverlapProfile(me_real, me_off, subj_pat, candidate_total, subj_rhythm,
                                      head_rhythm, candidate.delay_bars, kSubjectBars);
            const StrettoOverlapProfile vs_first = strettoOverlapProfile(
                subj_pat, first_total, subj_pat, candidate_total, subj_rhythm, head_rhythm,
                candidate.delay_bars - first_delay, kSubjectBars);
            if (vs_leader.overlap_slots == 0 || vs_leader.sustains_sharp ||
                vs_first.sustains_sharp) {
              continue;  // no overlap to vet means no basis to commit.
            }
            if (4 * vs_leader.broad_sharp_slots > vs_leader.overlap_slots ||
                4 * vs_first.broad_sharp_slots > vs_first.overlap_slots) {
              continue;
            }
            const int parallels = vs_leader.parallel_perfects + vs_first.parallel_perfects;
            if (best_second < 0 || parallels < best_second_parallels) {
              best_second = static_cast<int>(idx);
              best_second_parallels = parallels;
              best_second_total = candidate_total;
            }
          }
          if (best_second >= 0) {
            const SecondConfig& config = kSecondConfigs[static_cast<std::size_t>(best_second)];
            const int second_total = best_second_total;
            // Same verbatim-transposition bookkeeping as the first follower:
            // interval = second_total - v0_off so stretto_overlap_valid stays
            // exact against material.subject (the head is a prefix, so the
            // per-index relation holds).
            const int second_interval = second_total - v0_off;
            StrettoDecl stretto;
            stretto.leader_voice = static_cast<VoiceId>(carry_voice);
            stretto.follower_voice = static_cast<VoiceId>(third_voice);
            stretto.leader_entry_tick = barTick(me_start);
            stretto.leader_length_ticks = barTick(kSubjectBars);
            stretto.follower_entry_tick = barTick(me_start + config.delay_bars);
            stretto.interval_semis = second_interval;
            Tick cursor = barTick(me_start + config.delay_bars);
            const Tick end = barTick(me_start + kSubjectBars);
            for (int note = 0; note < head_notes && cursor < end; ++note) {
              MaterialNote mn;
              mn.start_tick = cursor;
              mn.duration = std::min(subj_rhythm[static_cast<std::size_t>(note)], end - cursor);
              mn.pitch = static_cast<std::uint8_t>(std::clamp(
                  static_cast<int>(subj_pat[static_cast<std::size_t>(note)]) + second_total, 0,
                  127));
              stretto.follower_notes.push_back(mn);
              asm_ctx.theme_tones.record(mn.start_tick, static_cast<VoiceId>(third_voice),
                                         static_cast<int>(mn.pitch), mn.duration);
              cursor += subj_rhythm[static_cast<std::size_t>(note)];
            }
            out.material.stretto_entries.push_back(stretto);
            pushSpan(asm_ctx, static_cast<VoiceId>(third_voice), me_start + config.delay_bars,
                     me_start + 3, VoiceIntent::StrettoCarrier);
            // The third voice RESTS until its delayed entrance: a stretto voice
            // entering out of silence is the idiomatic pile-up rhetoric, and
            // filling the wait with figuration doubles the transient
            // seconds-density of the window (the wash that reads as mud).
            second_stretto_placed = true;
          }
        }
      } else if (cycle == second_stretto_cycle) {
        // Second stretto moment: a single-follower stretto restated in
        // the last entry cycle before the pedal/coda, reusing the climax code
        // path. It replaces the recurring-countersubject accompaniment in the
        // accompaniment voice, mirroring how the climax follower replaces the
        // figuration accompaniment. When no configuration qualifies the cycle
        // keeps its recurring countersubject below.
        int delay = 0;
        int total = 0;
        pre_coda_stretto_placed =
            place_stretto_follower(me_real, me_off, carry_voice, acc_voice, me_start,
                                   mode == Mode::Minor ? 0 : key_semis, density, &delay, &total);
      }
      if (stretto_placed || pre_coda_stretto_placed) {
        // A follower already occupies the accompaniment voice this cycle.
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
        // The remaining voice carries figuration over the pedal. Without it the
        // pedal cycle is a two-voice texture, and once the subject reaches its
        // long-note tail the dominant-pedal bars -- the spot that should build
        // toward the coda -- decay to one or two attacks per bar. The pedal is
        // registered as a sounding tone first so the wave's consonance
        // machinery hears it (in minor the diatonic Ab would otherwise sustain
        // a minor ninth over the held G); the entry tones are already
        // registered above, so the line is the standard pedal-preparation
        // counterline.
        asm_ctx.theme_tones.record(barTick(me_start), static_cast<VoiceId>(pedal_voice),
                                   pedal_pitch, barTick(kSubjectBars));
        const int free_voice = 3 - carry_voice - pedal_voice;
        addFigurationSpan(asm_ctx, static_cast<VoiceId>(free_voice), me_start, me_start + 3, plan,
                          first_bar, mode, density, fig_offset);
      } else {
        // The ONE canonical countersubject rides in the highest non-carrying
        // voice, restated by octave-invertible degree shift into the entry key
        // (V0->V, V1->vi, V2->IV) so the same line recurs against every entry
        // and the ear tracks a single countersubject identity through the
        // development. When the restatement combines dissonantly with the entry
        // line (the static check fails), the window falls back to a reactive
        // consonant/contrary counterline derived FROM the entry, which the
        // constraint-hostile entry window is guaranteed to admit.
        emit_recurring_countersubject(
            kCountersubjectDegreeShift[static_cast<std::size_t>(carry_voice)], acc_voice, me_start,
            decl.notes);
        pushSpan(asm_ctx, static_cast<VoiceId>(acc_voice), me_start, me_start + 3,
                 VoiceIntent::CountersubjectCarrier);
      }
      // When the middle entry is carried by V2, the figuration accompaniment
      // lands on V0 and the middle voice would otherwise rest. Fill V1 with
      // chord-tone figuration so all three voices sound through the entry. The
      // pedal cycle already places a held tone in V1, so it is excluded. The V1
      // figuration is verbatim Material (both-Material with the V0 figuration, so
      // the upper-pair invertible / fourth checks are skipped); V2 harmonic
      // support, placed afterward, avoids parallels against it. A three-voice
      // stretto pile-up already owns every voice, so both accompaniment fills
      // are suppressed for that cycle (second_stretto_placed).
      const bool add_middle_bass_support =
          (carry_voice != 2 && cycle != pedal_cycle) && !second_stretto_placed;
      const bool fill_middle_voice =
          (carry_voice == 2 && cycle != pedal_cycle) && !second_stretto_placed;
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

    // --- Episode: a full-coverage Fortspinnung sequence in V0, restated one
    //     diatonic step DOWN per 2-bar stride in lockstep with the per-bar
    //     descending-fifths chord chain (a chord pair descends by a second
    //     every 2 bars, so the melodic sequence and the harmony move
    //     together), aimed so the final stride lands around the next
    //     station's chord in the V0 band. ---
    const int ep_start = first_bar + window.episode_start;
    const int ep_len = window.episode_len;
    if (ep_len > 0) {
      const auto degree_shift = [&](int base, int degrees) {
        return degrees >= 0 ? scaleUp(base, degrees, mode) : scaleDown(base, -degrees, mode);
      };
      // Subject head rebuilt DIATONICALLY (semitone interval -> scale degrees,
      // |rel| <= 12): a real semitone transposition chained per step would walk
      // every episode out of the key (a whole-tone smear). The degrees are then
      // FOLDED into the compact ambit [-2, +4]: the V0 band is ~10 diatonic
      // degrees wide, and an unfolded octave-leaping head plus the stride
      // descent would clamp into band-edge plateaus, so wide head intervals
      // keep their pitch-class contour an octave closer.
      constexpr std::array<int, 13> kSemisToDegrees = {0, 1, 1, 2, 2, 3, 3, 4, 5, 5, 6, 6, 7};
      std::array<int, 4> head_deg{};
      for (int note = 0; note < 4; ++note) {
        const int rel = static_cast<int>(subj_pat[static_cast<std::size_t>(note)]) -
                        static_cast<int>(subj_pat[0]);
        const int degrees = kSemisToDegrees[static_cast<std::size_t>(std::min(std::abs(rel), 12))];
        int folded = (rel >= 0) ? degrees : -degrees;
        while (folded > 3) {
          folded -= 7;
        }
        while (folded < -3) {
          folded += 7;
        }
        head_deg[static_cast<std::size_t>(note)] = folded;
      }
      // Keep the opening of every Fortspinnung model tied to the subject. It
      // is the downbeat-facing part that the accompaniment is pre-voiced
      // against; replacing it with a countersubject/inversion raised the
      // score-level vertical-dissonance rate. The closing turn rotates among
      // three contours instead, so episodes remain distinct without breaking
      // their chord-tone-oriented opening.
      const int episode_model = cycle % 3;
      // 2-bar Fortspinnung model: the head (the only leaps in the model --
      // they ARE the motif) in eighths, a sixteenth Spielfigur descent to the
      // model floor, a quarter-note arrival one step below it, and a rising
      // eighth approach into a half-note close that hands over to the next
      // stride one degree below this one. The spun continuation moves by
      // single degrees or chained thirds (the melodic surfaces the scorer
      // rewards), and the rhythm covers four duration classes (sixteenth /
      // eighth / quarter / half) so the episode does not flatten the piece's
      // duration and beat-position distributions. 16 slots fill the whole
      // stride: beats 1-2 head, 3-4 Spielfigur, 5 arrival, 6 approach, 7-8
      // close.
      std::array<int, 16> model_deg{};
      std::array<Tick, 16> model_dur{};
      for (int i = 0; i < 4; ++i) {
        model_deg[static_cast<std::size_t>(i)] = head_deg[static_cast<std::size_t>(i)];
        model_dur[static_cast<std::size_t>(i)] = kTicksPerBeat / 2;
      }
      // Spielfigur (slots 4..11, sixteenths): stepwise walk from the head's
      // last degree down to -4, zigzagging upward while there is slack so
      // every move stays +-1.
      {
        int cur = head_deg[3];
        bool up_next = true;
        for (int slot = 4; slot < 12; ++slot) {
          const int rem = 12 - slot;    // moves left, including this one.
          const int dist = cur - (-4);  // descent still needed.
          if (dist >= rem) {
            --cur;
          } else if (up_next && dist <= rem - 2) {
            ++cur;
            up_next = false;
          } else {
            --cur;
            up_next = true;
          }
          model_deg[static_cast<std::size_t>(slot)] = cur;
          model_dur[static_cast<std::size_t>(slot)] = kTicksPerBeat / 4;
        }
      }
      // Slots 12..15: quarter arrival on the floor, eighth approach, half
      // close. The -2 close steps up into the next stride's opening (which
      // sits one degree below this stride's).
      model_deg[12] = -5;
      model_dur[12] = kTicksPerBeat;
      model_deg[13] = -4;
      model_dur[13] = kTicksPerBeat / 2;
      model_deg[14] = -3;
      model_dur[14] = kTicksPerBeat / 2;
      model_deg[15] = -2;
      model_dur[15] = 2 * kTicksPerBeat;
      if (episode_model == 1) {
        model_deg[15] = -1;
      } else if (episode_model == 2) {
        model_deg[15] = -3;
      }
      // One SequenceTemplate per stride (num_steps = 1 each; the verbatim
      // step-0 check in sequence_pattern_consistency covers every emitted
      // note). Strides descend one degree per 2 bars in lockstep with the
      // chain; long sequences relaunch an octave up after every third stride
      // -- the idiomatic register reset that keeps a long chain from sinking
      // out of the band (the chain harmony is octave-invariant).
      const int steps = std::max(1, (ep_len + 1) / 2);
      constexpr int kRelaunchEvery = 3;
      const auto stride_shift = [](int k) { return -k + 7 * (k / kRelaunchEvery); };
      // Goal-tone seeding: aim the walk so every stride OPENS ON ITS OWN
      // BAR'S CHAIN CHORD ROOT (stated mid-band in V0). The chain descends
      // one degree per chord pair -- the same rate as the stride descent --
      // so one offset aligns every stride at once: the opening sits on the
      // first bar's root, the Spielfigur floor (-4) on the second bar's
      // root, the arrival (-5) is its lower appoggiatura, and the half-note
      // close (-2) is the second bar's chord THIRD, consonant by
      // construction so the accompaniment wave keeps the full triad
      // admissible under it. Relative to the chord AFTER the episode, the
      // final stride's opening bar sits two fifths up (+1 degree) for a
      // 2-bar final stride, one fifth up (-3 degrees octave-folded) for a
      // clipped odd-length final stride.
      const std::uint8_t target_root =
          plan[static_cast<std::size_t>(window.episode_start + ep_len)].root_pc;
      const int band_center = (kBandLo[0] + kBandHi[0]) / 2;
      int target_pitch = kBandLo[0];
      while (target_pitch % 12 != static_cast<int>(target_root)) {
        ++target_pitch;
      }
      while (target_pitch < band_center - 6) {
        target_pitch += 12;
      }
      while (!detail::inScale(target_pitch, mode)) {
        ++target_pitch;
      }
      const int aim_offset = (ep_len % 2 == 0) ? 1 : -3;
      int seed_base = degree_shift(target_pitch, aim_offset - stride_shift(steps - 1));
      // Band fit over the whole walk, OCTAVE-quantized so the chord
      // alignment above survives (an octave keeps every degree's pitch
      // class). Only when no octave position fits does the single-degree
      // nudge trade alignment for register; per-note clamping below stays as
      // the last resort.
      const int hi_deg = *std::max_element(model_deg.begin(), model_deg.end());
      const int lo_deg = *std::min_element(model_deg.begin(), model_deg.end());
      int min_shift = 0;
      int max_shift = 0;
      for (int k = 0; k < steps; ++k) {
        min_shift = std::min(min_shift, stride_shift(k));
        max_shift = std::max(max_shift, stride_shift(k));
      }
      while (degree_shift(seed_base, hi_deg + max_shift) > kBandHi[0]) {
        seed_base -= 12;
      }
      while (degree_shift(seed_base, lo_deg + min_shift) < kBandLo[0]) {
        seed_base += 12;
      }
      while (degree_shift(seed_base, hi_deg + max_shift) > kBandHi[0]) {
        seed_base = scaleDown(seed_base, 1, mode);
      }
      const Tick stride = barTick(2);
      const Tick span_lo = barTick(ep_start);
      const Tick span_hi = barTick(ep_start + ep_len);
      for (int kstep = 0; kstep < steps; ++kstep) {
        SequenceTemplate tmpl;
        tmpl.pattern = SequencePattern::DescendingStep;
        tmpl.target_start_tick = barTick(ep_start) + static_cast<Tick>(kstep) * stride;
        // A corpus-derived odd-length episode ends with a one-bar partial
        // stride. Declare that actual window instead of advertising two bars
        // and relying on span clipping, so sequence validation remains exact.
        tmpl.step_length_ticks = std::min(stride, span_hi - tmpl.target_start_tick);
        tmpl.num_steps = 1;
        tmpl.voice = 0;
        Tick cursor = tmpl.target_start_tick;
        for (int slot = 0; slot < static_cast<int>(model_deg.size()); ++slot) {
          if (cursor >= span_hi) {
            break;
          }
          const int deg = model_deg[static_cast<std::size_t>(slot)] + stride_shift(kstep);
          const int pitch = std::clamp(degree_shift(seed_base, deg), kBandLo[0], kBandHi[0]);
          const Tick dur = std::min(model_dur[static_cast<std::size_t>(slot)], span_hi - cursor);
          tmpl.seed_pitches.push_back(static_cast<std::uint8_t>(pitch));
          tmpl.seed_durations.push_back(dur);
          // Register the sounding tone (replicating the FortspinnungSpan
          // replay, which window-clips) so the V1/V2 accompaniment built
          // below avoids clashing with the V0 episode line.
          if (cursor >= span_lo) {
            asm_ctx.theme_tones.record(cursor, 0, pitch, dur);
          }
          cursor += dur;
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
      // the texture. The V1 figuration alternates its subdivision tier
      // (eighths / sixteenths) across episodes and rotates its register
      // offset, so the development's counterlines vary audibly and the
      // piece keeps its sixteenth-note duration mass; the V2 bass walks in
      // quarter-note chord roots a register below it.
      const int v1_notes_per_beat = (cycle % 2 == 1) ? 4 : 2;
      addFigurationSpan(asm_ctx, 1, ep_start, ep_start + ep_len - 1, plan, first_bar, mode,
                        v1_notes_per_beat, (fig_offset + cycle) % 4);
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

  // V0 cadence figure (CodaCarrier, bars coda_start+2 .. coda_start+3): a
  // cadential I6/4 -> V -> I. The opening C above the dominant pedal is the
  // dissonant fourth of the second-inversion tonic; it resolves down to B for
  // the dominant, then the final bar holds C. The ornament pass may decorate
  // the B, but the structural pitches stay explicit in the material.
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
    appendCadentialSixFourLanding(coda.notes, barTick(coda_start + 2), kTicksPerBar, tonic,
                                  tonic - 1);
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
    // Register the design tones like any other figuration. Everything that reads
    // the surface reads this registry, so a section missing from it is silence as
    // far as every later guard is concerned -- and the seam reliever below then
    // sees the preceding span hand over to a rest and leaves its tail alone,
    // which is how the wave walks into this arrival in parallel octaves.
    for (const MaterialNote& note : bass.notes) {
      asm_ctx.theme_tones.record(note.start_tick, 2, static_cast<int>(note.pitch), note.duration);
    }
    out.material.figuration_sections.push_back(bass);
    pushSpan(asm_ctx, 2, coda_start + 2, coda_start + 3, VoiceIntent::FigurationCarrier);
  }

  // V1 inner voice across the 2 cadence bars: held design tones filling the
  // middle register so the close sounds a full triad instead of the bare
  // V0+V2 octaves. The penultimate bar holds the dominant G (consonant with
  // the G bass and with every beat of the V0 approach run); the final bar
  // holds the third of the closing tonic triad -- E, or Eb in minor unless
  // the seed elects the Picardy lift. is_pedal_prep exempts the held tones
  // from the figuration downbeat chord-tone check (the per-bar plan is not
  // pinned to V -> I here; the cadence voicing is a design value).
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
    inner.start_tick = barTick(coda_start + 2);
    inner.end_tick = barTick(coda_start + coda_bars);
    inner.is_pedal_prep = true;
    addNote(inner.notes, barTick(coda_start + 2), kTicksPerBar, inner_dominant);
    addNote(inner.notes, barTick(coda_start + 3), kTicksPerBar, inner_third);
    for (const MaterialNote& note : inner.notes) {
      asm_ctx.theme_tones.record(note.start_tick, 1, static_cast<int>(note.pitch), note.duration);
    }
    out.material.figuration_sections.push_back(inner);
    pushSpan(asm_ctx, 1, coda_start + 2, coda_start + 3, VoiceIntent::FigurationCarrier);
  }

  // Final-cadence annotation: a perfect cadence on the final bar downbeat.
  {
    const Tick six_four_tick = barTick(coda_start + 2);
    for (ChordEvent& chord : out.harmony.chords) {
      if (chord.start_tick == six_four_tick) {
        chord.root_pc = 0;
        chord.quality = ChordQuality::Major;
        chord.degree = RomanNumeral::I;
        chord.inversion = ChordInversion::Second;
        chord.function = HarmonicFunction::T;
        chord.has_degree = true;
        break;
      }
    }
    ChordEvent dominant;
    dominant.start_tick = six_four_tick + kTicksPerBeat;
    dominant.root_pc = 7;
    dominant.quality = ChordQuality::Dominant7;
    dominant.degree = RomanNumeral::V;
    dominant.inversion = ChordInversion::Root;
    dominant.function = HarmonicFunction::D;
    dominant.has_degree = true;
    out.harmony.chords.push_back(dominant);
    std::stable_sort(out.harmony.chords.begin(), out.harmony.chords.end(),
                     [](const ChordEvent& left, const ChordEvent& right) {
                       return left.start_tick < right.start_tick;
                     });
    CadenceEvent cadence;
    cadence.tick = barTick(coda_start + coda_bars - 1);
    cadence.type = CadenceType::Perfect;
    out.harmony.cadences.push_back(cadence);
    out.harmony.cadential_six_fours.push_back(
        {six_four_tick, six_four_tick + kTicksPerBeat, SixFourType::Cadential});
  }
}

/// @brief Re-aim the tone an accompaniment span hands over with.
///
/// A span boundary is the one place in this form where two voices can both
/// begin verbatim material on the same tick: an entry arrives in one voice
/// while another voice's accompaniment ends, and each was written without the
/// other's next tone. Neither head may move -- both are theme statements at
/// designed levels -- but the accompaniment tone handing over to one of them is
/// free, and it is what decides the interval class the pair leaves behind. Read
/// here rather than while the span is built, because the boundary's other side
/// does not exist until every voice is placed.
///
/// Two classes are repaired, ranked: the TRUE parallel, and the contrary
/// arrival at the perfect class the pair had just left. The hidden approach is
/// not among them -- it is what a re-aim over fixed heads trades into, and
/// paying for one with another buys nothing -- but the contrary arrival does not
/// belong in that group. The reference corpus writes it far more sparingly than
/// the hidden approach, and scaled by the spread each class occupies there it
/// costs several times as much. The replacement keeps the span's band and its
/// scale, and holds to the leaps the displaced tone already spanned; it may take
/// a dissonance only where the tone it replaces was consonant and no consonant
/// tone improves on the fault, which is the same order of preference the
/// accompaniment's own anchor guard uses one layer down.
void relieveFigurationSeams(FugueAssembly& asm_ctx, Mode mode) {
  struct Replacement {
    Tick start = 0;
    Tick duration = 0;
    VoiceId voice = 0;
    int pitch = 0;
  };
  // A seam can hand over in two voices at once, and the registry cannot be
  // amended in place (its lookup keeps the earliest matching onset), so a tone
  // already re-aimed here is read back from this list rather than from it.
  std::vector<Replacement> replaced;
  // A figuration tone authored on a bar downbeat is the bar's harmonic anchor
  // and may only be exchanged for another tone of the same chord; anywhere else
  // in the bar the line is free to any scale tone. The chord tone set is the
  // shared one, so a seam over a dominant seventh may still be relieved onto the
  // seventh the anchor selector was allowed to place there.
  const std::vector<ChordEvent>& chords = asm_ctx.out->harmony.chords;
  auto anchors_bar_harmony = [&](Tick tick, int pitch) {
    const ChordEvent* active = nullptr;
    for (const ChordEvent& chord : chords) {
      if (chord.start_tick <= tick && (active == nullptr || chord.start_tick >= active->start_tick))
        active = &chord;
    }
    if (active == nullptr) {
      return false;
    }
    std::size_t tone_count = 0;
    const auto tones = chordPitchClasses(*active, &tone_count);
    const auto pitch_class = static_cast<std::uint8_t>(((pitch % 12) + 12) % 12);
    for (std::size_t tone = 0; tone < tone_count; ++tone) {
      if (tones[tone] == pitch_class)
        return true;
    }
    return false;
  };
  auto sounding = [&](VoiceId voice, Tick tick) {
    for (const Replacement& rep : replaced) {
      if (rep.voice == voice && tick >= rep.start && tick < rep.start + rep.duration)
        return rep.pitch;
    }
    return asm_ctx.theme_tones.soundingPitchInVoice(voice, tick);
  };

  for (FigurationSection& section : asm_ctx.out->material.figuration_sections) {
    if (section.notes.empty()) {
      continue;
    }
    MaterialNote& tail = section.notes.back();
    const VoiceId voice = section.voice;
    const Tick seam = tail.start_tick + tail.duration;
    const int original = static_cast<int>(tail.pitch);
    const int own_next = sounding(voice, seam);
    const int own_prev = section.notes.size() > 1
                             ? static_cast<int>(section.notes[section.notes.size() - 2].pitch)
                             : sounding(voice, tail.start_tick > 0 ? tail.start_tick - 1 : Tick{0});
    if (own_next < 0) {
      continue;  // the voice rests after the span: nothing hands over.
    }

    // How badly the handover reads, ranked rather than pooled. The true parallel
    // is the cardinal prohibition; the contrary arrival at the same perfect class
    // is the next thing down, and the reference corpus prices it at several times
    // a hidden approach because the works write it far more sparingly. Ranking
    // the two is what lets a parallel seam still be relieved onto a contrary
    // arrival -- strictly better than what it replaces -- while a contrary seam
    // may only be relieved onto a tone free of both. Pooling them would let a
    // parallel go unrelieved whenever the only escape was a contrary one.
    constexpr int kSeamClean = 0;
    constexpr int kSeamAntiParallel = 1;
    constexpr int kSeamParallel = 2;
    auto seam_fault = [&](int cand) {
      int worst = kSeamClean;
      for (VoiceId other = 0; other < kFugueVoices; ++other) {
        if (other == voice)
          continue;
        const int other_prev = sounding(other, seam - 1);
        const int other_curr = sounding(other, seam);
        if (formsStrictPerfectParallel(cand, own_next, other_prev, other_curr))
          return kSeamParallel;
        if (formsAntiParallelPerfect(cand, own_next, other_prev, other_curr))
          worst = std::max(worst, kSeamAntiParallel);
      }
      return worst;
    };
    const int original_fault = seam_fault(original);
    if (original_fault == kSeamClean) {
      continue;
    }

    bool original_consonant = true;
    for (VoiceId other = 0; other < kFugueVoices; ++other) {
      if (other == voice)
        continue;
      const int at_tail = sounding(other, tail.start_tick);
      if (at_tail >= 0 && !isConsonantPair(original, at_tail))
        original_consonant = false;
    }
    const int entry_ceiling = own_prev < 0 ? 0 : std::max(7, std::abs(original - own_prev));
    const int exit_ceiling = std::max(12, std::abs(own_next - original));

    const bool anchors_harmony = tail.start_tick % kTicksPerBar == 0;
    auto admissible = [&](int cand, bool allow_dissonance) {
      if (cand < kBandLo[voice] || cand > kBandHi[voice] || !detail::inScale(cand, mode))
        return false;
      if (anchors_harmony && !anchors_bar_harmony(tail.start_tick, cand))
        return false;
      if (own_prev >= 0 && std::abs(cand - own_prev) > entry_ceiling)
        return false;
      if (std::abs(own_next - cand) > exit_ceiling)
        return false;
      for (VoiceId other = 0; other < kFugueVoices; ++other) {
        if (other == voice)
          continue;
        const int at_tail = sounding(other, tail.start_tick);
        if (at_tail >= 0 && original_consonant && !allow_dissonance &&
            !isConsonantPair(cand, at_tail))
          return false;
        if (formsStrictPerfectParallel(own_prev, cand, sounding(other, tail.start_tick - 1),
                                       at_tail))
          return false;
      }
      return seam_fault(cand) < original_fault;
    };

    const int reach = std::max(entry_ceiling, exit_ceiling);
    bool placed = false;
    for (const bool allow_dissonance : {false, true}) {
      if (allow_dissonance && (placed || !original_consonant))
        break;
      for (int dist = 1; dist <= reach && !placed; ++dist) {
        for (const int sgn : {-1, 1}) {
          const int cand = original + sgn * dist;
          if (admissible(cand, allow_dissonance)) {
            tail.pitch = static_cast<std::uint8_t>(cand);
            replaced.push_back({tail.start_tick, tail.duration, voice, cand});
            placed = true;
            break;
          }
        }
      }
    }
  }
}

/// @brief Take the seventh back out of any chord the voices do not support.
///
/// The spelling is derived from the progression, but most of what sounds at a
/// bar head is verbatim thematic material laid out without reference to it. Two
/// voices already on the pitch class the spelling would name as the seventh are
/// not a seventh chord being voiced -- they are a doubling, and naming it turns
/// a plain octave or unison into a voice-leading fault that neither line can be
/// moved off, because neither line is the composer's to move. The triad is then
/// the truthful reading of what sounds, and no note changes.
///
/// A figuration voice sounding the seventh is left alone: that tone WAS chosen
/// against the spelling, and retracting it underneath would strand the bar head
/// off its own chord.
void retractUnsupportedSevenths(FugueAssembly& asm_ctx) {
  const std::vector<FigurationSection>& sections = asm_ctx.out->material.figuration_sections;
  for (ChordEvent& chord : asm_ctx.out->harmony.chords) {
    if (!hasSeventh(chord.quality)) {
      continue;
    }
    const int seventh_pc = (chord.root_pc + seventhOffset(chord.quality)) % 12;
    auto sounds_seventh = [&](int pitch) {
      return pitch >= 0 && ((pitch % 12) + 12) % 12 == seventh_pc;
    };
    int voices_on_seventh = 0;
    for (VoiceId voice = 0; voice < kFugueVoices; ++voice) {
      if (sounds_seventh(asm_ctx.theme_tones.soundingPitchInVoice(voice, chord.start_tick))) {
        ++voices_on_seventh;
      }
    }
    if (voices_on_seventh < 2) {
      continue;
    }
    bool figuration_holds_it = false;
    for (const FigurationSection& section : sections) {
      for (const MaterialNote& note : section.notes) {
        if (note.start_tick <= chord.start_tick &&
            chord.start_tick < note.start_tick + note.duration &&
            sounds_seventh(static_cast<int>(note.pitch))) {
          figuration_holds_it = true;
        }
      }
    }
    if (figuration_holds_it) {
      continue;
    }
    chord.quality =
        chord.quality == ChordQuality::Minor7 ? ChordQuality::Minor : ChordQuality::Major;
  }
}

}  // namespace

HarnessFixture buildFugueForm(const ResolvedRequest& req) {
  HarnessFixture out;
  out.voice_plan.num_voices = 3;
  SpanId next_id = 0;
  FugueAssembly asm_ctx{&out, &next_id, {}};
  appendFugueSection(asm_ctx, /*first_bar=*/0, static_cast<int>(req.bars), req);
  relieveFigurationSeams(asm_ctx, req.mode);
  retractUnsupportedSevenths(asm_ctx);
  return out;
}

HarnessFixture buildPreludeAndFugueForm(const ResolvedRequest& req) {
  HarnessFixture out;
  out.voice_plan.num_voices = 3;
  SpanId next_id = 0;
  FugueAssembly asm_ctx{&out, &next_id, {}};

  const int total = static_cast<int>(req.bars);
  // Prelude length = N/3 rounded to 4, clamped to [4, 32]; the rest is fugue
  // (kept >= 20 so the fugue half always carries all three entries + coda).
  int prelude_bars = ((total / 3 + 2) / 4) * 4;
  prelude_bars = std::clamp(prelude_bars, 4, 32);
  if (total - prelude_bars < 20) {
    prelude_bars = total - 20;
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
  std::vector<ChordSpec> prelude_plan = buildRepeatingChordPlan(prelude_bars, mode, harm_idx);
  // No related-key approach interrupts the prelude's repeating pattern, so every
  // fifth-fall in it is a plain dominant and no bar has to be held back to a
  // triad. Nothing here is verbatim thematic material either: all three voices
  // are figuration reading the same chord, which is the texture a chain of
  // seventh chords is written for.
  markDominantSevenths(prelude_plan, /*triad_only_bars=*/{}, mode);
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

  // The prelude closes before the fugue enters. Preserve that structural
  // boundary for ornamentation, tempo segmentation, and organ/harpsichord
  // registration terraces.
  out.section_cadence_ticks.push_back(barTick(prelude_bars - 1));
  out.registration_step_ticks.push_back(barTick(prelude_bars));

  // --- FUGUE (bars prelude_bars .. total-1). Reuse the full fugue assembly at
  //     a bar offset; span ids continue from the prelude (shared next_id). ---
  appendFugueSection(asm_ctx, prelude_bars, fugue_bars, req);
  relieveFigurationSeams(asm_ctx, req.mode);
  retractUnsupportedSevenths(asm_ctx);

  // Keep the concatenated HarmonicPlan chords in tick order.
  std::stable_sort(
      out.harmony.chords.begin(), out.harmony.chords.end(),
      [](const ChordEvent& lhs, const ChordEvent& rhs) { return lhs.start_tick < rhs.start_tick; });

  return out;
}

}  // namespace bach::composer
