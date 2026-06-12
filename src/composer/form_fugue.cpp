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
using detail::kHarmonyPatterns;             // NOLINT(build/namespaces)
using detail::kHarmonyPatternsMinor;        // NOLINT(build/namespaces)
using detail::Mode;                         // NOLINT(build/namespaces)
using detail::scaleUp;                      // NOLINT(build/namespaces)
using detail::subjectIndexFor;              // NOLINT(build/namespaces)
using tables::kSubjectCatalogMajor;         // NOLINT(build/namespaces)
using tables::kSubjectCatalogMajorRhythms;  // NOLINT(build/namespaces)
using tables::kSubjectCatalogMinor;         // NOLINT(build/namespaces)
using tables::kSubjectCatalogMinorRhythms;  // NOLINT(build/namespaces)

// Minor-mode middle entries restate the major catalog row at the same index
// (the related keys are major), so every minor index must also be a valid
// major index.
static_assert(kSubjectCatalogMinor.size() <= kSubjectCatalogMajor.size(),
              "minor catalog indices must be valid in the major catalog");

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

/// @brief Whether a candidate stretto canon sustains a sharp dissonance.
///
/// Lays the leader and the delayed follower on a sixteenth grid and scans the
/// overlap for interval class 1, 6 or 11 (the semitone/tritone family) held
/// for a full quarter note or longer. Brief passing seconds are idiomatic in a
/// stretto, but both lines are verbatim Material -- the validator skips every
/// dissonance rule on Material x Material pairs -- so a beat-long m2/M7
/// between the two theme statements would ship unflagged. The caller uses this
/// to vet each (delay, interval) configuration before committing the canon.
///
/// @param leader_pat The leader's 16-note pattern (middle-entry material).
/// @param leader_total Total semitone shift applied to the leader.
/// @param follower_pat The follower's 16-note pattern (exposition subject).
/// @param follower_total Total semitone shift applied to the follower.
/// @param rhythm Shared per-note durations (one subject statement).
/// @param delay_bars Follower entry delay in bars (1..kSubjectBars-1).
/// @return True when the overlap sustains a sharp dissonance for >= a quarter.
bool strettoSustainsDissonance(const std::array<std::uint8_t, 16>& leader_pat, int leader_total,
                               const std::array<std::uint8_t, 16>& follower_pat, int follower_total,
                               const std::array<Tick, 16>& rhythm, int delay_bars) {
  constexpr Tick kSlotTick = kTicksPerBeat / 4;  // sixteenth grid.
  const int total_slots = static_cast<int>(barTick(kSubjectBars) / kSlotTick);
  std::vector<int> leader(static_cast<std::size_t>(total_slots), -1);
  std::vector<int> follower(static_cast<std::size_t>(total_slots), -1);
  auto lay = [&](std::vector<int>& line, const std::array<std::uint8_t, 16>& pat, int total,
                 Tick start) {
    Tick cursor = start;
    for (int note = 0; note < kSubjectNotes; ++note) {
      const Tick dur = rhythm[static_cast<std::size_t>(note)];
      for (Tick t = cursor; t < cursor + dur; t += kSlotTick) {
        const int slot = static_cast<int>(t / kSlotTick);
        if (slot >= total_slots) {
          return;  // the follower is truncated at the leader's end.
        }
        line[static_cast<std::size_t>(slot)] =
            static_cast<int>(pat[static_cast<std::size_t>(note)]) + total;
      }
      cursor += dur;
    }
  };
  lay(leader, leader_pat, leader_total, 0);
  lay(follower, follower_pat, follower_total, barTick(delay_bars));
  const int sustain_limit = static_cast<int>(kQuarter / kSlotTick);
  int run = 0;
  for (int slot = 0; slot < total_slots; ++slot) {
    bool sharp = false;
    if (leader[static_cast<std::size_t>(slot)] >= 0 &&
        follower[static_cast<std::size_t>(slot)] >= 0) {
      const int ic = std::abs(leader[static_cast<std::size_t>(slot)] -
                              follower[static_cast<std::size_t>(slot)]) %
                     12;
      sharp = (ic == 1 || ic == 6 || ic == 11);
    }
    run = sharp ? run + 1 : 0;
    if (run >= sustain_limit) {
      return true;
    }
  }
  return false;
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
// V0 / V1 / V2 by cycle index, so the plan rotates across the development).
// The keys are V / vi / IV (the validator's admissible related keys; the home
// tonic I is NOT admissible). V and IV entries are REAL transpositions: +7 /
// +5 maps a C-major subject onto the G / F major scales exactly, so every
// note's pitch class is diatonic to the declared major key. The vi entry is
// NOT a real transposition -- +9 would realize vi as A MAJOR (C#/F#/G#),
// bi-tonal against the home-key texture -- it is a diatonic degree shift (up
// five home-scale degrees), stating the subject in the relative NATURAL minor
// whose pitch-class set equals the home major scale; the validator checks vi
// entries against that natural-minor set. kVoiceKeySemis therefore feeds the
// real-transposition voices only (the vi realization is built per note).
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
  // bar's centre inside appendFigurationWaveBar (prev_anchor <= 0).
  int prev_anchor = 0;
  for (int bar = first_bar; bar <= last_bar; ++bar) {
    appendFigurationWaveBar(asm_ctx.theme_tones, section, bar, voice,
                            chords[static_cast<std::size_t>(bar - plan_base)], mode, notes_per_beat,
                            offset, prev_anchor, kBandLo[voice], kBandHi[voice], kFugueVoices);
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
    return (cycle == pedal_cycle && rotation_voice == 1) ? 0 : rotation_voice;
  };
  // Pass 1: station chords (entries and the coda), so every episode's target
  // bar is already filled when the chains are built.
  for (int cycle = 0; cycle < static_cast<int>(windows.size()); ++cycle) {
    const DevelopmentWindow& window = windows[static_cast<std::size_t>(cycle)];
    if (!window.has_entry) {
      continue;
    }
    const std::uint8_t key_pc = kVoiceKeyPc[static_cast<std::size_t>(carry_voice_for(cycle))];
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
  // Exposition: 12 bars normally, compressed to 8 for short fugues (N<=20).
  const bool short_form = bars <= 20;
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

  // Per-bar chord plan for the whole fugue (section-relative bars), derived
  // from the tonal stations the layout above fixes. The final two bars are
  // pinned to a V -> I authentic cadence so the explicit cadential bass
  // (dominant then tonic) is harmonically consistent and the
  // cadence_voice_leading rule reads a true V->I.
  std::vector<ChordSpec> plan =
      buildFugueTonalPlan(bars, mode, exposition_bars, coda_bars, development_windows, pedal_cycle);
  plan[static_cast<std::size_t>(bars - 2)] = ChordSpec{7, false};                // V (G major).
  plan[static_cast<std::size_t>(bars - 1)] = ChordSpec{0, mode == Mode::Minor};  // I.
  emitHarmony(out, plan, mode, first_bar);

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
    // Entries rotate through the voices, EXCEPT in the pedal cycle when the
    // rotation would land on V1: the dominant pedal prepares the home-key
    // return, and V1's relative-minor entry deflects away from that function
    // at exactly the wrong moment (its F natural also leans on the held G).
    // That cycle's entry is re-carried by V0 instead -- a dominant-key (G
    // major) statement over the dominant pedal is the textbook pedal
    // preparation, and V0's per-voice key declaration is unchanged. V2's IV
    // key (F major) holds no sharp interval against G, so its rotation slot
    // stays.
    const int rotation_voice = cycle % 3;
    const int carry_voice = (cycle == pedal_cycle && rotation_voice == 1) ? 0 : rotation_voice;
    // Accompaniment density rises with the arc; figuration accompanies the
    // highest non-carrying voice (one accompaniment voice per window).
    const int acc_voice = (carry_voice == 0) ? 1 : 0;
    const int density = std::clamp<int>(1 + arc.density_tier, 1, 2);

    if (!half_cycle) {
      // --- Middle entry (4 bars): the subject restated in the carrying voice in
      //     a related key (V / vi / IV, keyed by the carrying voice so each
      //     voice stays in one key). V and IV are REAL transpositions of the
      //     MAJOR subject catalog (+7 / +5 maps the C-major line onto the G /
      //     F major scales exactly). The vi entry is a DIATONIC degree shift
      //     instead -- up five C-major scale degrees, which states the subject
      //     in the relative (natural) minor: a real +9 transposition would
      //     realize vi as A MAJOR, whose C#/F#/G# turn the development
      //     bi-tonal against the C-major figuration around it. The realized
      //     line is then octave-fit into the voice band. ---
      const int me_start = first_bar + window.entry_start;  // absolute.
      const int key_semis = kVoiceKeySemis[static_cast<std::size_t>(carry_voice)];
      const std::array<std::uint8_t, 16>& me_pat = kSubjectCatalogMajor[slot];
      std::array<std::uint8_t, 16> me_real;
      for (int note = 0; note < kSubjectNotes; ++note) {
        const int base = static_cast<int>(me_pat[static_cast<std::size_t>(note)]);
        // The catalog line is C-major diatonic, so the degree walk stays on
        // the home scale in both modes (minor pieces also restate the major
        // catalog here; see the comment above).
        me_real[static_cast<std::size_t>(note)] = static_cast<std::uint8_t>(
            carry_voice == 1 ? detail::scaleUp(base, 5, Mode::Major) : base + key_semis);
      }
      const int me_off = octaveOffsetForBand(me_real, 0, carry_voice);
      MiddleEntryDecl& decl = middle_decls[static_cast<std::size_t>(carry_voice)];
      decl.voice = static_cast<VoiceId>(carry_voice);
      decl.related_key_pc = kVoiceKeyPc[static_cast<std::size_t>(carry_voice)];
      Tick me_cursor = barTick(me_start);
      for (int note = 0; note < kSubjectNotes; ++note) {
        MaterialNote mn;
        mn.start_tick = me_cursor;
        mn.duration = subj_rhythm[static_cast<std::size_t>(note)];
        mn.pitch = static_cast<std::uint8_t>(
            std::clamp(static_cast<int>(me_real[static_cast<std::size_t>(note)]) + me_off, 0, 127));
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
      // accompaniment voice, overlapping the leader. The follower restates the
      // subject in the leader's key, octave-fit into the follower's band, so
      // the overlap forms a single-key canon instead of clashing bi-tonally.
      // The follower must stay a REAL transposition of the subject (the
      // validator's stretto_overlap_valid relation is verbatim), so when the
      // leader is the vi entry -- a diatonic degree shift whose pitch set is
      // the home scale -- the only real transpositions inside that set are
      // home-key statements: the vi cycle uses follower_semis = 0 and skips
      // the fifth-up configs (their F# would strike a false relation against
      // the leader's F natural). The canon's (delay, interval) configuration
      // is vetted against strettoSustainsDissonance in preference order -- the
      // 1-bar octave canon (densest overlap) first, then the 2-bar delay, then
      // a fifth-up canon (the subject/answer pairing) at the same delays. The
      // subject is not designed for self-canon at every offset, so the first
      // configuration with no sustained sharp dissonance wins; when none
      // qualifies the stretto is dropped and the window keeps the
      // consonance-aware figuration accompaniment below instead. A committed
      // follower replaces the figuration accompaniment for this window.
      const bool climax = (cycle == climax_cycle);
      bool stretto_placed = false;
      if (climax) {
        const int follower_voice = acc_voice;
        struct StrettoConfig {
          int delay_bars;
          int extra_semis;
        };
        constexpr std::array<StrettoConfig, 4> kStrettoConfigs = {{{1, 0}, {2, 0}, {1, 7}, {2, 7}}};
        const int follower_key_semis = (carry_voice == 1) ? 0 : key_semis;
        for (const StrettoConfig& config : kStrettoConfigs) {
          if (carry_voice == 1 && config.extra_semis != 0) {
            continue;  // no in-set fifth-up canon against the modal vi leader.
          }
          const int follower_semis = follower_key_semis + config.extra_semis;
          const int follower_off = octaveOffsetForBand(subj_pat, follower_semis, follower_voice);
          const int follower_total = follower_semis + follower_off;
          if (strettoSustainsDissonance(me_real, me_off, subj_pat, follower_total, subj_rhythm,
                                        config.delay_bars)) {
            continue;
          }
          // material.subject[i] == subj_pat[i] + v0_off (the V0 exposition
          // statement), so the validated relation follower[i] == subject[i] +
          // interval requires interval = follower_total - v0_off. This keeps
          // the validator's stretto_overlap_valid verbatim-transposition
          // relation exact while the follower sits in its canon key.
          const int stretto_interval = follower_total - v0_off;
          StrettoDecl stretto;
          stretto.leader_voice = static_cast<VoiceId>(carry_voice);
          stretto.follower_voice = static_cast<VoiceId>(follower_voice);
          stretto.leader_entry_tick = barTick(me_start);
          stretto.leader_length_ticks = barTick(kSubjectBars);
          stretto.follower_entry_tick = barTick(me_start + config.delay_bars);
          stretto.interval_semis = stretto_interval;
          Tick follower_cursor = barTick(me_start + config.delay_bars);
          const Tick follower_end = barTick(me_start + kSubjectBars);
          for (int note = 0; note < kSubjectNotes && follower_cursor < follower_end; ++note) {
            MaterialNote mn;
            mn.start_tick = follower_cursor;
            mn.duration = std::min(subj_rhythm[static_cast<std::size_t>(note)],
                                   follower_end - follower_cursor);
            mn.pitch = static_cast<std::uint8_t>(std::clamp(
                static_cast<int>(subj_pat[static_cast<std::size_t>(note)]) + follower_total, 0,
                127));
            stretto.follower_notes.push_back(mn);
            asm_ctx.theme_tones.record(mn.start_tick, static_cast<VoiceId>(follower_voice),
                                       static_cast<int>(mn.pitch), mn.duration);
            follower_cursor += subj_rhythm[static_cast<std::size_t>(note)];
          }
          out.material.stretto_entries.push_back(stretto);
          pushSpan(asm_ctx, static_cast<VoiceId>(follower_voice), me_start + config.delay_bars,
                   me_start + 3, VoiceIntent::StrettoCarrier);
          if (config.delay_bars > 1) {
            // Keep the follower's voice sounding while it waits for the
            // delayed entrance, so the climax window does not thin to two
            // voices before the canon arrives.
            addFigurationSpan(asm_ctx, static_cast<VoiceId>(follower_voice), me_start,
                              me_start + config.delay_bars - 1, plan, first_bar, mode, density,
                              fig_offset);
          }
          stretto_placed = true;
          break;
        }
      }
      if (stretto_placed) {
        // Canon committed above; nothing else occupies the follower voice.
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
        // The recurring countersubject rides in the highest non-carrying
        // voice -- the same designed consonant/contrary mechanism the
        // exposition answer carries -- rather than a reactive figuration
        // wave. The entry window is the most constraint-hostile context the
        // wave faces (its vetoes starve against the verbatim entry line); a
        // counterline derived FROM the entry is consonant and contrary by
        // construction, and restating the countersubject at every middle
        // entry returns a recurring identity the ear can track through the
        // development.
        append_countersubject_from(decl.notes, acc_voice, barTick(me_start),
                                   barTick(me_start + kSubjectBars));
        pushSpan(asm_ctx, static_cast<VoiceId>(acc_voice), me_start, me_start + 3,
                 VoiceIntent::CountersubjectCarrier);
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
        tmpl.step_length_ticks = stride;
        tmpl.num_steps = 1;
        tmpl.voice = 0;
        Tick cursor = tmpl.target_start_tick;
        for (int slot = 0; slot < static_cast<int>(model_deg.size()); ++slot) {
          const int deg = model_deg[static_cast<std::size_t>(slot)] + stride_shift(kstep);
          const int pitch = std::clamp(degree_shift(seed_base, deg), kBandLo[0], kBandHi[0]);
          const Tick dur = model_dur[static_cast<std::size_t>(slot)];
          tmpl.seed_pitches.push_back(static_cast<std::uint8_t>(pitch));
          tmpl.seed_durations.push_back(dur);
          // Register the sounding tone (replicating the FortspinnungSpan
          // replay, which window-clips) so the V1/V2 accompaniment built
          // below avoids clashing with the V0 episode line.
          if (cursor >= span_lo && cursor < span_hi) {
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

  // V0 cadence figure (CodaCarrier, bars coda_start+2 .. coda_start+3): the
  // shared cadential landing. An eighth-note approach run rises into a held
  // half-note leading tone B over the bar's second half (the cadential trill
  // site the ornament pass decorates), then the final bar holds the tonic C as
  // a whole note (the plain resolution). The cadence_voice_leading rule reads
  // the SOUNDING pitch at the approach beat (penultimate-bar beat 3) and the
  // final downbeat: the held B spans that approach beat and the whole-note C
  // covers the downbeat, so upper_prev = B resolves to upper_now = C exactly
  // as the 8-quarter figure did.
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
    appendCadentialLanding(coda.notes, barTick(coda_start + 2), kTicksPerBar, tonic - 1, tonic,
                           mode, kBandLo[0]);
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
    out.material.figuration_sections.push_back(inner);
    pushSpan(asm_ctx, 1, coda_start + 2, coda_start + 3, VoiceIntent::FigurationCarrier);
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
