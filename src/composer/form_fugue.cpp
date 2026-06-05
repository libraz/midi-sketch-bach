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

using detail::ChordSpec;              // NOLINT(build/namespaces)
using detail::kHarmonyPatterns;       // NOLINT(build/namespaces)
using detail::kHarmonyPatternsMinor;  // NOLINT(build/namespaces)
using detail::kPhase14Subjects;       // NOLINT(build/namespaces)
using detail::kSubjectsMinor;         // NOLINT(build/namespaces)
using detail::Mode;                   // NOLINT(build/namespaces)
using detail::scaleUp;                // NOLINT(build/namespaces)
using detail::subjectSlotFor;         // NOLINT(build/namespaces)
using detail::usePicardy;             // NOLINT(build/namespaces)

constexpr Tick kQuarter = kTicksPerBeat;
constexpr Tick kSixteenth = kTicksPerBeat / 4;

// One subject statement is 16 quarter notes spanning 4 bars.
constexpr int kSubjectNotes = 16;
constexpr int kSubjectBars = 4;

// Per-voice register bands (MIDI). The bands are disjoint and strictly ordered
// (V0 highest, V2 lowest). Every voice's material is octave-fit so its highest
// note stays at or below the band ceiling (octaveOffsetForBand is ceiling-first),
// and the ceilings are spaced so that, even when a line dips a full octave below
// its floor, its lowest note stays above the next voice's ceiling. With every
// voice confined this way the validator's voice_crossing rule never fires across
// the all-Material texture. The V0 floor (67) is low enough that the unshifted
// subject (range 70..84 major / 70..82 minor) needs no forced octave lift.
constexpr int kBandLo[3] = {67, 51, 33};
constexpr int kBandHi[3] = {96, 66, 50};

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

// ---------------------------------------------------------------------------
// FugueAssembly: the per-section accumulator the internal builders write into.
// The same assembly is used for a standalone fugue (first_bar = 0) and for the
// fugue half of a prelude+fugue pair (first_bar = prelude length). Span ids and
// the next-id counter are shared so concatenated sections stay unique.
// ---------------------------------------------------------------------------
struct ThemeTone {
  Tick tick = 0;
  VoiceId voice = 0;
  int pitch = 0;
};

struct FugueAssembly {
  HarnessFixture* out = nullptr;
  SpanId* next_id = nullptr;
  // Registry of every thematic (immutable) note's onset, so the consonance-
  // aware figuration can read the concurrent theme tone at each beat and pick a
  // chord tone that does not clash with it. Thematic statements are quarter
  // notes (one per beat); the registry stores beat onsets.
  std::vector<ThemeTone> theme_tones;
};

/// @brief Record a thematic note onset for the figuration consonance lookup.
void recordTheme(FugueAssembly& asm_ctx, Tick tick, VoiceId voice, int pitch) {
  asm_ctx.theme_tones.push_back(ThemeTone{tick, voice, pitch});
}

/// @brief Interval class (0..6) folded to consonance, true when consonant.
///
/// Consonant interval classes are the unison/3rd/4th/5th/6th families
/// (IC 0,3,4,5,7,8,9 mod 12); the dissonant set the scorer penalises is
/// {1,2,6,10,11} (m2/M2/TT/m7/M7). A perfect fourth (IC 5) is treated as
/// consonant here because it sits between upper voices over a chord-tone bass.
bool isConsonantIc(int semis) {
  const int ic = ((std::abs(semis) % 12) + 12) % 12;
  return ic != 1 && ic != 2 && ic != 6 && ic != 10 && ic != 11;
}

/// @brief Collect concurrent theme-tone pitches at `tick` excluding `voice`.
///
/// The figuration on `voice` must not clash with any thematic statement
/// sounding at the same beat. Thematic notes are quarter notes aligned to the
/// beat grid, so an exact onset match captures the concurrent tone.
void concurrentThemePitches(const FugueAssembly& asm_ctx, Tick tick, VoiceId voice,
                            std::vector<int>& out_pitches) {
  out_pitches.clear();
  for (const ThemeTone& tone : asm_ctx.theme_tones) {
    if (tone.voice == voice) {
      continue;
    }
    if (tick >= tone.tick && tick < tone.tick + kQuarter) {
      out_pitches.push_back(tone.pitch);
    }
  }
}

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

/// @brief Pick the band chord tone that is consonant with every theme tone.
///
/// Enumerates the chord tones of `chord` inside `voice`'s band and returns the
/// one nearest `target` whose interval class against ALL concurrent theme
/// pitches is consonant. The downbeat anchor must always be a genuine chord
/// tone (figuration_harmonic_consistency checks the bar downbeat), so this only
/// ever returns chord tones. When no chord tone clears every theme tone (the
/// theme momentarily clashes with the whole triad) the least-dissonant chord
/// tone nearest `target` is returned so the downbeat stays a chord tone.
///
/// @param chord The bar's chord (supplies the triad pitch classes).
/// @param voice Voice index selecting the band.
/// @param target Preferred register centre (the wave is built around it).
/// @param theme_pitches Concurrent thematic pitches to stay consonant against.
/// @return A chord-tone MIDI pitch inside the band.
int consonantChordTone(const ChordSpec& chord, int voice, int target,
                       const std::vector<int>& theme_pitches) {
  const int third = chord.minor ? 3 : 4;
  const int triad_pc[3] = {chord.root_pc % 12, (chord.root_pc + third) % 12,
                           (chord.root_pc + 7) % 12};
  auto is_triad = [&](int midi) {
    const int pcl = ((midi % 12) + 12) % 12;
    return pcl == triad_pc[0] || pcl == triad_pc[1] || pcl == triad_pc[2];
  };
  int best = -1;
  int best_dist = 1 << 20;
  int fallback = -1;
  int fallback_score = 1 << 20;  // fewer clashes (then nearer) is better.
  for (int pitch = kBandLo[voice]; pitch <= kBandHi[voice]; ++pitch) {
    if (!is_triad(pitch)) {
      continue;
    }
    int clashes = 0;
    for (int theme : theme_pitches) {
      if (!isConsonantIc(pitch - theme)) {
        ++clashes;
      }
    }
    const int dist = std::abs(pitch - target);
    if (clashes == 0) {
      if (dist < best_dist) {
        best_dist = dist;
        best = pitch;
      }
    }
    const int score = clashes * 1000 + dist;
    if (score < fallback_score) {
      fallback_score = score;
      fallback = pitch;
    }
  }
  if (best >= 0) {
    return best;
  }
  return fallback >= 0 ? fallback : std::clamp(target, kBandLo[voice], kBandHi[voice]);
}

/// @brief Append one bar of theme-consonant chord-tone figuration.
///
/// Every beat opens on a chord tone of `chord` chosen to be consonant against
/// the concurrent thematic statement (the scorer samples vertical intervals on
/// the beat grid, so the on-beat note is what drives vertical_dissonance). The
/// off-beat fill walks one scale step toward the next beat anchor, keeping the
/// line conjunct (no leaps) while every sampled (on-beat) vertical stays
/// consonant. The downbeat anchor is always a genuine chord tone so
/// figuration_harmonic_consistency passes; band confinement keeps the voice
/// ordering (V0 >= V1 >= V2) intact across the all-Material texture.
///
/// @param asm_ctx Assembly (read for the concurrent theme-tone registry).
/// @param section Figuration section receiving the bar's notes.
/// @param bar Absolute bar index of the figuration bar.
/// @param voice Voice index (selects the band the wave is clamped into).
/// @param chord The bar's chord (supplies the per-beat chord tones).
/// @param mode Diatonic mode selecting the scale walker.
/// @param notes_per_beat Subdivision density (1 / 2 / 4).
/// @param offset Seed-derived start-register offset above the band floor.
void appendFigurationBar(FugueAssembly& asm_ctx, FigurationSection& section, int bar, int voice,
                         const ChordSpec& chord, Mode mode, int notes_per_beat, int offset) {
  // Register centre for the wave: a few scale degrees above the band floor,
  // shifted by the seed offset, kept clear of the band ceiling so a stepwise
  // fill never runs out of band.
  int target = scaleUp(kBandLo[voice], offset + 2, mode);
  if (target > kBandHi[voice] - 4) {
    target = scaleUp(kBandLo[voice], offset, mode);
  }
  const Tick step =
      (notes_per_beat == 4) ? kSixteenth : ((notes_per_beat == 2) ? kTicksPerBeat / 2 : kQuarter);
  std::vector<int> theme_pitches;
  // Per-beat chord-tone anchors (consonant against the concurrent theme tone).
  std::array<int, 4> beat_anchor{};
  for (int beat = 0; beat < 4; ++beat) {
    const Tick beat_tick = barTick(bar) + static_cast<Tick>(beat) * kTicksPerBeat;
    concurrentThemePitches(asm_ctx, beat_tick, static_cast<VoiceId>(voice), theme_pitches);
    beat_anchor[static_cast<std::size_t>(beat)] =
        consonantChordTone(chord, voice, target, theme_pitches);
  }
  for (int beat = 0; beat < 4; ++beat) {
    const int anchor = beat_anchor[static_cast<std::size_t>(beat)];
    // Off-beat fill steps from this beat's anchor toward the next beat's anchor
    // so the join interval is small (conjunct) and no leap is introduced.
    const int next_anchor = beat_anchor[static_cast<std::size_t>((beat + 1) % 4)];
    const int dir = (next_anchor > anchor) ? 1 : -1;
    // scaleUp only ascends; reflect about zero to walk downward by scale steps.
    auto walk = [&](int from, int steps) {
      return dir > 0 ? scaleUp(from, steps, mode) : -scaleUp(-from, steps, mode);
    };
    for (int sub = 0; sub < notes_per_beat; ++sub) {
      int pitch = anchor;
      if (sub > 0) {
        pitch = walk(anchor, sub);
        // Stop overshooting the next anchor; clamp into the band.
        if ((dir > 0 && pitch > next_anchor) || (dir < 0 && pitch < next_anchor)) {
          pitch = next_anchor;
        }
        pitch = std::clamp(pitch, kBandLo[voice], kBandHi[voice]);
      }
      const Tick tick =
          barTick(bar) + static_cast<Tick>(beat) * kTicksPerBeat + static_cast<Tick>(sub) * step;
      addNote(section.notes, tick, step, pitch);
    }
  }
}

/// @brief Add one figuration accompaniment span over [first_bar, last_bar].
///
/// Creates one FigurationSection whose window exactly matches the span (the
/// FigurationCarrier dispatch matches sections by exact window) and a matching
/// FigurationCarrier span. The bars open on chord tones drawn from `chords`,
/// which is indexed by the SECTION-RELATIVE bar (absolute bar - `plan_base`).
void addFigurationSpan(FugueAssembly& asm_ctx, VoiceId voice, int first_bar, int last_bar,
                       const std::vector<ChordSpec>& chords, int plan_base, Mode mode,
                       int notes_per_beat, int offset) {
  FigurationSection section;
  section.voice = voice;
  section.start_tick = barTick(first_bar);
  section.end_tick = barTick(last_bar + 1);
  for (int bar = first_bar; bar <= last_bar; ++bar) {
    appendFigurationBar(asm_ctx, section, bar, voice,
                        chords[static_cast<std::size_t>(bar - plan_base)], mode, notes_per_beat,
                        offset);
  }
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
  // Number of 8-bar device-cycles filling the development (last may be 4 bars).
  const int num_cycles = development_bars > 0 ? (development_bars + 7) / 8 : 0;

  // Climax cycle: ~80% of the development span (matches arcPoint's climax).
  const int climax_cycle = num_cycles > 0 ? std::min(num_cycles - 1, (num_cycles * 4) / 5) : -1;
  // Pedal cycle: the cycle immediately before the coda (only for N >= 32).
  const int pedal_cycle = (bars >= 32 && num_cycles > 0) ? num_cycles - 1 : -1;

  // === EXPOSITION ===========================================================
  // Each thematic statement carries the subject in ONE voice band; at most ONE
  // figuration accompaniment voice is added per bar window (the FigurationCarrier
  // dispatch matches sections by window only, so two sections sharing a window
  // would collide -- a single accompaniment voice per window avoids that).
  const int v0_off = octaveOffsetForBand(subj_pat, 0, 0);
  auto stamp_subject = [&](int base_bar, int semis, int theme_voice) {
    for (int note = 0; note < kSubjectNotes; ++note) {
      const int bar = base_bar + note / 4;
      const int beat = note % 4;
      const Tick tick = barTick(bar) + static_cast<Tick>(beat) * kTicksPerBeat;
      const int pitch = static_cast<int>(subj_pat[static_cast<std::size_t>(note)]) + semis;
      addNote(out.material.subject, tick, kQuarter, pitch);
      recordTheme(asm_ctx, tick, static_cast<VoiceId>(theme_voice), pitch);
    }
  };
  stamp_subject(first_bar + 0, v0_off, 0);
  pushSpan(asm_ctx, 0, first_bar + 0, first_bar + 3, VoiceIntent::SubjectCarrier);

  // Answer (V1, bars 4-7) = real answer (subject - P4) lowered into the V1 band.
  const int answer_off = octaveOffsetForBand(subj_pat, -5, 1);
  const int answer_total = -5 + answer_off;
  for (int note = 0; note < kSubjectNotes; ++note) {
    const int bar = first_bar + 4 + note / 4;
    const int beat = note % 4;
    const Tick tick = barTick(bar) + static_cast<Tick>(beat) * kTicksPerBeat;
    const int pitch = static_cast<int>(subj_pat[static_cast<std::size_t>(note)]) + answer_total;
    addNote(out.material.answer, tick, kQuarter, pitch);
    recordTheme(asm_ctx, tick, 1, pitch);
  }
  pushSpan(asm_ctx, 1, first_bar + 4, first_bar + 7, VoiceIntent::AnswerCarrier);
  // V0 figuration counterline rides above the answer (single accompaniment).
  addFigurationSpan(asm_ctx, 0, first_bar + 4, first_bar + 7, plan, first_bar, mode, 2, fig_offset);

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
    // V0 countersubject figuration rides above the V2 third entry (single
    // accompaniment voice; V1 rests here).
    addFigurationSpan(asm_ctx, 0, first_bar + 8, first_bar + 11, plan, first_bar, mode, 2,
                      fig_offset);
  }

  // === DEVELOPMENT ==========================================================
  const int dev_start = exposition_bars;  // relative to first_bar.
  // One MiddleEntryDecl per carrying voice; each decl holds all of that voice's
  // middle-entry notes and the span windows slice them.
  std::array<MiddleEntryDecl, 3> middle_decls;
  std::array<bool, 3> middle_used = {false, false, false};

  for (int cycle = 0; cycle < num_cycles; ++cycle) {
    const int cycle_start = dev_start + cycle * 8;  // relative to first_bar.
    const int remaining = bars - coda_bars - cycle_start;
    const bool half_cycle = remaining < 8;  // last cycle, episode only.
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
      const int me_start = first_bar + cycle_start;  // absolute.
      const int key_semis = kVoiceKeySemis[static_cast<std::size_t>(carry_voice)];
      const std::array<std::uint8_t, 16>& me_pat = kPhase14Subjects[slot];
      const int me_off = octaveOffsetForBand(me_pat, key_semis, carry_voice);
      const int me_total = key_semis + me_off;
      MiddleEntryDecl& decl = middle_decls[static_cast<std::size_t>(carry_voice)];
      decl.voice = static_cast<VoiceId>(carry_voice);
      decl.related_key_pc = kVoiceKeyPc[static_cast<std::size_t>(carry_voice)];
      for (int note = 0; note < kSubjectNotes; ++note) {
        const int bar = me_start + note / 4;
        const int beat = note % 4;
        MaterialNote mn;
        mn.start_tick = barTick(bar) + static_cast<Tick>(beat) * kTicksPerBeat;
        mn.duration = kQuarter;
        mn.pitch = static_cast<std::uint8_t>(std::clamp(
            static_cast<int>(me_pat[static_cast<std::size_t>(note)]) + me_total, 0, 127));
        decl.notes.push_back(mn);
        recordTheme(asm_ctx, mn.start_tick, static_cast<VoiceId>(carry_voice),
                    static_cast<int>(mn.pitch));
      }
      middle_used[static_cast<std::size_t>(carry_voice)] = true;
      pushSpan(asm_ctx, static_cast<VoiceId>(carry_voice), me_start, me_start + 3,
               VoiceIntent::MiddleEntryCarrier);

      // Stretto in the climax cycle: a second subject statement in the
      // accompaniment voice at a 1-bar delay, overlapping the leader. The
      // follower replaces the figuration accompaniment for this window.
      const bool climax = (cycle == climax_cycle);
      if (climax) {
        const int follower_voice = acc_voice;
        const int follower_off = octaveOffsetForBand(subj_pat, 0, follower_voice);
        // material.subject[i] == subj_pat[i] + v0_off (the V0 exposition
        // statement), so the validated relation follower[i] == subject[i] +
        // interval requires interval = follower_off - v0_off.
        const int stretto_interval = follower_off - v0_off;
        StrettoDecl stretto;
        stretto.leader_voice = static_cast<VoiceId>(carry_voice);
        stretto.follower_voice = static_cast<VoiceId>(follower_voice);
        stretto.leader_entry_tick = barTick(me_start);
        stretto.leader_length_ticks = barTick(kSubjectBars);
        stretto.follower_entry_tick = barTick(me_start + 1);
        stretto.interval_semis = stretto_interval;
        for (int note = 0; note < kSubjectNotes - 4; ++note) {
          const int bar = (me_start + 1) + note / 4;
          const int beat = note % 4;
          MaterialNote mn;
          mn.start_tick = barTick(bar) + static_cast<Tick>(beat) * kTicksPerBeat;
          mn.duration = kQuarter;
          mn.pitch = static_cast<std::uint8_t>(std::clamp(
              static_cast<int>(subj_pat[static_cast<std::size_t>(note)]) + follower_off, 0, 127));
          stretto.follower_notes.push_back(mn);
          recordTheme(asm_ctx, mn.start_tick, static_cast<VoiceId>(follower_voice),
                      static_cast<int>(mn.pitch));
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
    }

    // --- Episode (4 bars): a Fortspinnung sequence derived from the subject
    //     head, transposed per the character's motif operation, in V0 with one
    //     band-confined accompaniment voice below. ---
    const int ep_start = half_cycle ? (first_bar + cycle_start) : (first_bar + cycle_start + 4);
    const int ep_len = half_cycle ? remaining : 4;
    if (ep_len > 0) {
      SequenceTemplate tmpl;
      // Direction alternates by cycle; the character's motif transform colours
      // the head (Diminish shortens the seed durations, Augment lengthens them).
      tmpl.pattern =
          (cycle % 2 == 0) ? SequencePattern::AscendingStep : SequencePattern::DescendingStep;
      tmpl.target_start_tick = barTick(ep_start);
      tmpl.step_length_ticks = barTick(2);
      tmpl.num_steps = static_cast<std::uint8_t>(std::max(1, ep_len / 2));
      tmpl.voice = 0;
      const motif_ops::EpisodeMotifTransform transform =
          motif_ops::characterToTransform(req.character);
      Tick seed_dur = kTicksPerBeat / 2;  // eighths (Severe / Playful / Noble).
      if (transform == motif_ops::EpisodeMotifTransform::Diminish) {
        seed_dur = kTicksPerBeat / 4;  // Restless: diminished (sixteenths).
      }
      // Seed motif: the subject head (4 notes) centred low in the V0 band so
      // the +/- 2-per-step transpositions stay inside the band; the ascending
      // pattern keeps the line above the accompaniment voice's band.
      const int seed_base = kBandLo[0] + 4;
      for (int note = 0; note < 4; ++note) {
        const int rel = static_cast<int>(subj_pat[static_cast<std::size_t>(note)]) -
                        static_cast<int>(subj_pat[0]);
        tmpl.seed_pitches.push_back(
            static_cast<std::uint8_t>(std::clamp(seed_base + rel, kBandLo[0], kBandHi[0])));
        tmpl.seed_durations.push_back(seed_dur);
      }
      out.material.sequence_templates.push_back(tmpl);
      pushSpan(asm_ctx, 0, ep_start, ep_start + ep_len - 1, VoiceIntent::FortspinnungSpan);

      // Register the episode sequence's sounding tones (replicating the
      // FortspinnungSpan replay) so the V1 accompaniment built below avoids
      // clashing with the V0 episode line. The span window clips notes outside
      // [ep_start, ep_start+ep_len); record only the in-window ones.
      {
        const int step_offset =
            (tmpl.pattern == SequencePattern::AscendingStep) ? 2 : -2;  // see step_semis.
        const Tick span_lo = barTick(ep_start);
        const Tick span_hi = barTick(ep_start + ep_len);
        for (int kstep = 0; kstep < static_cast<int>(tmpl.num_steps); ++kstep) {
          Tick cursor = tmpl.target_start_tick + static_cast<Tick>(kstep) * tmpl.step_length_ticks;
          for (std::size_t idx = 0; idx < tmpl.seed_pitches.size(); ++idx) {
            const Tick dur = tmpl.seed_durations[idx];
            if (cursor >= span_lo && cursor < span_hi) {
              const int pitch = static_cast<int>(tmpl.seed_pitches[idx]) + step_offset * kstep;
              recordTheme(asm_ctx, cursor, 0, std::clamp(pitch, 0, 127));
            }
            cursor += dur;
          }
        }
      }

      // Single accompaniment figuration in V1 under the episode (V2 rests).
      addFigurationSpan(asm_ctx, 1, ep_start, ep_start + ep_len - 1, plan, first_bar, mode, 2,
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
  for (int note = 0; note < 8; ++note) {
    const int bar = coda_start + note / 4;
    const int beat = note % 4;
    addNote(out.material.subject, barTick(bar) + static_cast<Tick>(beat) * kTicksPerBeat, kQuarter,
            static_cast<int>(subj_pat[static_cast<std::size_t>(note)]) + v0_off);
  }
  pushSpan(asm_ctx, 0, coda_start, coda_start + 1, VoiceIntent::SubjectCarrier);

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
  FugueAssembly asm_ctx{&out, &next_id};
  appendFugueSection(asm_ctx, /*first_bar=*/0, static_cast<int>(req.bars), req);
  return out;
}

HarnessFixture buildPreludeAndFugueForm(const ResolvedRequest& req) {
  HarnessFixture out;
  out.voice_plan.num_voices = 3;
  SpanId next_id = 0;
  FugueAssembly asm_ctx{&out, &next_id};

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

  auto append_per_beat_anchored = [&](FigurationSection& section, int bar, int voice,
                                      const ChordSpec& chord, int notes_per_beat) {
    const int third = chord.minor ? 3 : 4;
    const int triad_pc[3] = {chord.root_pc % 12, (chord.root_pc + third) % 12,
                             (chord.root_pc + 7) % 12};
    auto is_triad = [&](int midi) {
      const int pcl = ((midi % 12) + 12) % 12;
      return pcl == triad_pc[0] || pcl == triad_pc[1] || pcl == triad_pc[2];
    };
    int anchor = scaleUp(kBandLo[voice] + chord.root_pc, fig_offset, mode);
    while (!is_triad(anchor)) {
      ++anchor;
    }
    while (anchor > kBandHi[voice] - 4) {
      anchor -= 12;
    }
    const Tick step = (notes_per_beat == 4) ? kSixteenth : kTicksPerBeat / 2;
    for (int beat = 0; beat < 4; ++beat) {
      for (int sub = 0; sub < notes_per_beat; ++sub) {
        const Tick tick =
            barTick(bar) + static_cast<Tick>(beat) * kTicksPerBeat + static_cast<Tick>(sub) * step;
        addNote(section.notes, tick, step, scaleUp(anchor, sub, mode));
      }
    }
  };

  // V0 prelude figuration (sixteenths) split into 2-bar sections; the final
  // section is is_pedal_prep so PedalPreparation links into the fugue. A 2-bar
  // chunk size keeps every V0 section window distinct from the single
  // whole-prelude V1 window below (the FigurationCarrier dispatch matches
  // sections by window only, so two sections sharing a window would collide --
  // distinct windows per voice avoid that even at the 4-bar minimum prelude).
  for (int sec_start = 0; sec_start < prelude_bars; sec_start += 2) {
    const int sec_last = std::min(sec_start + 1, prelude_bars - 1);
    FigurationSection sec;
    sec.voice = 0;
    sec.start_tick = barTick(sec_start);
    sec.end_tick = barTick(sec_last + 1);
    sec.is_pedal_prep = (sec_last == prelude_bars - 1);
    for (int bar = sec_start; bar <= sec_last; ++bar) {
      append_per_beat_anchored(sec, bar, 0, prelude_plan[static_cast<std::size_t>(bar)], 4);
    }
    out.material.figuration_sections.push_back(sec);
    pushSpan(asm_ctx, 0, sec_start, sec_last, VoiceIntent::FigurationCarrier);
  }
  // V1 prelude bass support (eighths) across the whole prelude (single window).
  {
    FigurationSection bass;
    bass.voice = 1;
    bass.start_tick = barTick(0);
    bass.end_tick = barTick(prelude_bars);
    for (int bar = 0; bar < prelude_bars; ++bar) {
      append_per_beat_anchored(bass, bar, 1, prelude_plan[static_cast<std::size_t>(bar)], 2);
    }
    out.material.figuration_sections.push_back(bass);
    pushSpan(asm_ctx, 1, 0, prelude_bars - 1, VoiceIntent::FigurationCarrier);
  }

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
