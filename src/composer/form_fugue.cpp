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

/// @brief Mirror a C-major diatonic line around its ambit centre in degree space.
///
/// Each note is reflected through the centre of the line's degree ambit
/// (never raw semitones): with d_i the signed diatonic degree distance from
/// the first note to note i, the mirrored distance is (d_min + d_max) - d_i,
/// so every step's direction flips while the line's lowest and highest
/// degrees simply swap in place. Mirroring around the AMBIT CENTRE (rather
/// than the first note) keeps the inverted statement in the same register as
/// the upright one: a first-note mirror hangs an ascending subject downward
/// and sinks the entry to the bottom of its voice band. Because the walk is
/// done with detail::scaleUp / detail::scaleDown in C major, the result stays
/// C-major diatonic by construction; the downstream octaveOffsetForBand step
/// then places it exactly where the upright line would sit.
///
/// @param pat The 16-note C-major diatonic subject line.
/// @return The diatonic melodic inversion of `pat`.
std::array<std::uint8_t, 16> invertDiatonicLine(const std::array<std::uint8_t, 16>& pat) {
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
      walk = scaleUp(walk, 1, Mode::Major);
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
    const int mirrored = mirrored_degrees >= 0 ? scaleUp(anchor, mirrored_degrees, Mode::Major)
                                               : scaleDown(anchor, -mirrored_degrees, Mode::Major);
    inverted[idx] = static_cast<std::uint8_t>(std::clamp(mirrored, 0, 127));
  }
  return inverted;
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
/// @param leader_rhythm Leader per-note durations (one subject statement).
/// @param follower_rhythm Follower per-note durations (zero-length tail
///        entries lay nothing, so a one-bar head vets only its own span).
/// @param delay_bars Follower entry delay in bars (1..kSubjectBars-1).
/// @return The overlap's dissonance profile (see StrettoOverlapProfile).
struct StrettoOverlapProfile {
  bool sustains_sharp = false;  // ic 1/6/11 held for >= a quarter note.
  int overlap_slots = 0;        // sixteenth slots where both lines sound.
  int broad_sharp_slots = 0;    // slots at ic 1/2/6/10/11 (seconds family).
};

StrettoOverlapProfile strettoOverlapProfile(const std::array<std::uint8_t, 16>& leader_pat,
                                            int leader_total,
                                            const std::array<std::uint8_t, 16>& follower_pat,
                                            int follower_total,
                                            const std::array<Tick, 16>& leader_rhythm,
                                            const std::array<Tick, 16>& follower_rhythm,
                                            int delay_bars) {
  constexpr Tick kSlotTick = kTicksPerBeat / 4;  // sixteenth grid.
  const int total_slots = static_cast<int>(barTick(kSubjectBars) / kSlotTick);
  std::vector<int> leader(static_cast<std::size_t>(total_slots), -1);
  std::vector<int> follower(static_cast<std::size_t>(total_slots), -1);
  auto lay = [&](std::vector<int>& line, const std::array<std::uint8_t, 16>& pat, int total,
                 const std::array<Tick, 16>& rhythm, Tick start) {
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
  lay(leader, leader_pat, leader_total, leader_rhythm, 0);
  lay(follower, follower_pat, follower_total, follower_rhythm, barTick(delay_bars));
  StrettoOverlapProfile profile;
  const int sustain_limit = static_cast<int>(kQuarter / kSlotTick);
  int run = 0;
  for (int slot = 0; slot < total_slots; ++slot) {
    bool sharp = false;
    if (leader[static_cast<std::size_t>(slot)] >= 0 &&
        follower[static_cast<std::size_t>(slot)] >= 0) {
      profile.overlap_slots += 1;
      const int ic = std::abs(leader[static_cast<std::size_t>(slot)] -
                              follower[static_cast<std::size_t>(slot)]) %
                     12;
      sharp = (ic == 1 || ic == 6 || ic == 11);
      if (sharp || ic == 2 || ic == 10) {
        profile.broad_sharp_slots += 1;
      }
    }
    run = sharp ? run + 1 : 0;
    if (run >= sustain_limit) {
      profile.sustains_sharp = true;
    }
  }
  return profile;
}

bool strettoSustainsDissonance(const std::array<std::uint8_t, 16>& leader_pat, int leader_total,
                               const std::array<std::uint8_t, 16>& follower_pat, int follower_total,
                               const std::array<Tick, 16>& rhythm, int delay_bars) {
  return strettoOverlapProfile(leader_pat, leader_total, follower_pat, follower_total, rhythm,
                               rhythm, delay_bars)
      .sustains_sharp;
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
  const std::size_t cs_canonical_base = out.material.countersubject.size();
  append_countersubject_from(use_tonal_answer ? out.material.tonal_answer : out.material.answer, 0,
                             barTick(first_bar + 4), barTick(first_bar + 8));
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
    for (const StrettoConfig& config : kStrettoConfigs) {
      if (leader_carry_voice == 1 && config.extra_semis != 0) {
        continue;  // no in-set fifth-up canon against the modal vi leader.
      }
      const int follower_semis = follower_key_semis + config.extra_semis;
      const int follower_off = octaveOffsetForBand(subj_pat, follower_semis, follower_voice);
      const int follower_total = follower_semis + follower_off;
      if (strettoSustainsDissonance(leader_pat, leader_total, subj_pat, follower_total, subj_rhythm,
                                    config.delay_bars)) {
        continue;
      }
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
      // The inverted development cycle mirrors the catalog line in degree space
      // before any key realization; the inversion stays C-major diatonic, so the
      // per-voice realization (V/IV real transposition, vi degree shift) and the
      // octave fit below preserve the declared related key exactly as they do for
      // the upright line.
      std::array<std::uint8_t, 16> me_pat = kSubjectCatalogMajor[slot];
      const bool inverted_here = (cycle == inverted_cycle);
      if (inverted_here) {
        me_pat = invertDiatonicLine(me_pat);
      }
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
        stretto_placed =
            place_stretto_follower(me_real, me_off, carry_voice, follower_voice, me_start,
                                   key_semis, density, &first_delay, &first_total);
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
          const StrettoOverlapProfile first_profile = strettoOverlapProfile(
              me_real, me_off, subj_pat, first_total, subj_rhythm, subj_rhythm, first_delay);
          const bool first_canon_clean =
              4 * first_profile.broad_sharp_slots <= first_profile.overlap_slots;
          for (const SecondConfig& config : kSecondConfigs) {
            if (!first_canon_clean) {
              break;
            }
            if (config.delay_bars <= first_delay) {
              continue;  // the pile-up requires a later entrance than the first.
            }
            if (carry_voice == 1 && config.extra_semis != 0) {
              continue;  // no in-set fifth-up canon against the modal vi leader.
            }
            const int second_semis = second_key_semis + config.extra_semis;
            const int second_off = octaveOffsetForBand(subj_pat, second_semis, third_voice);
            const int second_total = second_semis + second_off;
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
            const StrettoOverlapProfile vs_leader =
                strettoOverlapProfile(me_real, me_off, subj_pat, second_total, subj_rhythm,
                                      head_rhythm, config.delay_bars);
            const StrettoOverlapProfile vs_first =
                strettoOverlapProfile(subj_pat, first_total, subj_pat, second_total, subj_rhythm,
                                      head_rhythm, config.delay_bars - first_delay);
            if (vs_leader.overlap_slots == 0 || vs_leader.sustains_sharp ||
                vs_first.sustains_sharp) {
              continue;  // no overlap to vet means no basis to commit.
            }
            if (4 * vs_leader.broad_sharp_slots > vs_leader.overlap_slots ||
                4 * vs_first.broad_sharp_slots > vs_first.overlap_slots) {
              continue;
            }
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
            break;
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
        pre_coda_stretto_placed = place_stretto_follower(
            me_real, me_off, carry_voice, acc_voice, me_start, key_semis, density, &delay, &total);
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
          (carry_voice != 2 && cycle != pedal_cycle && (fig_offset != 1 || cycle >= 2)) &&
          !second_stretto_placed;
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
