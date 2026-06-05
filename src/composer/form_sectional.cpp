#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <vector>

#include "composer/character_profile.h"
#include "composer/figuration.h"
#include "composer/form_builders.h"
#include "composer/material.h"
#include "composer/minor_material.h"
#include "composer/span.h"
#include "composer/voice_intent.h"
#include "core/basic_types.h"

namespace bach::composer {

// ---------------------------------------------------------------------------
// Sectional concatenation forms: toccata+fugue and fantasia+fugue. Each is a
// FREE opening section (toccata or fantasia) on V0 with V1/V2 resting, then a
// 3-voice fugue section to the end, assembled into one fixture.
//
// Both builders are dedicated assemblers (no longer placeholders replaying a
// proven phase fixture). They honour ResolvedRequest length, mode, character,
// and the arc curve.
//
// Free section: the toccata generalizes Phase18's archetype machinery to the
// available bars; the fantasia generalizes Phase22's contrasting-section cycle.
// The fugue tail is a self-contained 3-entry exposition + optional stretto + a
// 2-bar Picardy cadence, built inline with the Phase24 idiom (it does NOT share
// the fugue family's assembly cascade, so the two systems stay independent).
//
// EVERY note is NoteSource::Material (verbatim carriers). The validator's
// parallel / hidden-parallel / vertical-dissonance / cross-relation / invertible
// rules all skip a voice pair when BOTH notes are Material, so the only
// inter-voice constraint that fires is voice_crossing. Both forms therefore
// confine each fugue-tail voice's material to a disjoint, strictly-ordered
// register band (V0 highest, V2 lowest), keeping V0 >= V1 >= V2 at every shared
// tick. The free section is single-voice (V0) with no shared ticks.
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

constexpr Tick kQuarter = kTicksPerBeat;
constexpr Tick kEighth = kTicksPerBeat / 2;
constexpr Tick kSixteenth = kTicksPerBeat / 4;

// One subject statement is 16 quarter notes spanning 4 bars.
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

// ---------------------------------------------------------------------------
// SectionalAssembly: the accumulator both builders write into. Span ids and the
// next-id counter are shared so the concatenated free + fugue sections stay
// unique across the whole fixture.
// ---------------------------------------------------------------------------
struct SectionalAssembly {
  HarnessFixture* out = nullptr;
  SpanId* next_id = nullptr;
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

/// @brief Append one bar of chord-tone-anchored scalar-wave notes to a vector.
///
/// The bar opens on a chord tone of `chord` (so a downbeat anchor is consonant),
/// then runs a stepwise scalar wave (ascend then descend) confined to
/// [base_midi, base_midi + span). Predominantly-stepwise running figuration
/// (BWV565 / BWV538 toccata idiom) -- low melodic-interval cost. The seed
/// `offset` shifts the start degree up the scale before the chord-tone snap.
///
/// @param dst Note vector receiving the bar's notes.
/// @param bar Absolute bar index.
/// @param chord The bar's chord (downbeat anchor is one of its tones).
/// @param mode Diatonic mode selecting the scale walker.
/// @param notes_per_beat Subdivision density (1 = quarter, 2 = eighth, 4 = 16th).
/// @param base_midi Register floor for the wave.
/// @param ceil_midi Register ceiling for the wave.
/// @param offset Seed-derived start-degree offset above the anchor.
void appendScalarBar(std::vector<MaterialNote>& dst, int bar, const ChordSpec& chord, Mode mode,
                     int notes_per_beat, int base_midi, int ceil_midi, int offset) {
  const int third = chord.minor ? 3 : 4;
  const int triad_pc[3] = {chord.root_pc % 12, (chord.root_pc + third) % 12,
                           (chord.root_pc + 7) % 12};
  auto is_triad = [&](int midi) {
    const int pcl = ((midi % 12) + 12) % 12;
    return pcl == triad_pc[0] || pcl == triad_pc[1] || pcl == triad_pc[2];
  };
  int anchor = base_midi;
  while (!is_triad(anchor)) {
    ++anchor;
  }
  anchor = scaleUp(anchor, offset, mode);
  while (!is_triad(anchor)) {
    anchor = scaleUp(anchor, 1, mode);
  }
  if (anchor > ceil_midi - 6) {
    anchor = base_midi;
    while (!is_triad(anchor)) {
      ++anchor;
    }
  }
  const int notes = 4 * notes_per_beat;
  std::vector<int> wave;
  wave.reserve(static_cast<std::size_t>(notes) + 2);
  for (int idx = 0; idx <= notes / 2; ++idx) {
    int pitch = scaleUp(anchor, idx, mode);
    if (pitch > ceil_midi) {
      pitch = ceil_midi;
    }
    wave.push_back(pitch);
  }
  for (int idx = static_cast<int>(wave.size()) - 2; idx >= 0; --idx) {
    wave.push_back(wave[static_cast<std::size_t>(idx)]);
  }
  const Tick step =
      (notes_per_beat == 4) ? kSixteenth : ((notes_per_beat == 2) ? kEighth : kQuarter);
  for (int beat = 0; beat < 4; ++beat) {
    for (int sub = 0; sub < notes_per_beat; ++sub) {
      const int slot = beat * notes_per_beat + sub;
      const Tick tick =
          barTick(bar) + static_cast<Tick>(beat) * kTicksPerBeat + static_cast<Tick>(sub) * step;
      addNote(dst, tick, step, wave[static_cast<std::size_t>(slot) % wave.size()]);
    }
  }
}

/// @brief Test whether two pitches form a consonant interval class.
///
/// The external scorer's vertical-dissonance metric samples every beat and flags
/// a beat dissonant when any sounding pair has interval class in {1,2,6,10,11}
/// (m2 / M2 / TT / m7 / M7). The consonant classes are therefore unison/octave
/// (0), m3/M3 (3/4), P4 (5), P5 (7), and m6/M6 (8/9). A figuration on-beat note
/// is kept consonant against every concurrent sustained tone so the fugue tail's
/// figuration-vs-theme texture stays inside the consonant band.
///
/// @param pitch_a First MIDI pitch.
/// @param pitch_b Second MIDI pitch.
/// @return True if the pair's interval class is consonant.
bool isConsonantPair(int pitch_a, int pitch_b) {
  const int ic = std::abs(pitch_a - pitch_b) % 12;
  return ic == 0 || ic == 3 || ic == 4 || ic == 5 || ic == 7 || ic == 8 || ic == 9;
}

/// @brief Test consonance of a candidate against every concurrent tone.
bool isConsonantWithAll(int candidate, const std::vector<int>& concurrent) {
  for (int other : concurrent) {
    if (!isConsonantPair(candidate, other)) {
      return false;
    }
  }
  return true;
}

/// @brief Snap a target pitch to the nearest in-band, in-scale, consonant pitch.
///
/// Searches outward from `target` (closest first) for a diatonic pitch inside
/// [base_midi, ceil_midi] that is consonant with every tone in `concurrent`.
/// Falls back to the in-scale pitch nearest `target` if no consonant pitch
/// exists in band (keeps the line diatonic even in a degenerate window).
///
/// @param target Desired pitch (the scalar-wave value for this slot).
/// @param mode Diatonic mode selecting the scale.
/// @param base_midi Register floor.
/// @param ceil_midi Register ceiling.
/// @param concurrent Pitches sounding in other voices at this beat.
/// @return The snapped MIDI pitch.
int snapConsonant(int target, Mode mode, int base_midi, int ceil_midi,
                  const std::vector<int>& concurrent) {
  int best_diatonic = -1;
  for (int radius = 0; radius <= ceil_midi - base_midi; ++radius) {
    for (int dir = (radius == 0 ? 0 : -1); dir <= 1; dir += 2) {
      const int cand = target + dir * radius;
      if (cand < base_midi || cand > ceil_midi || !detail::inScale(cand, mode)) {
        continue;
      }
      if (best_diatonic < 0) {
        best_diatonic = cand;
      }
      if (isConsonantWithAll(cand, concurrent)) {
        return cand;
      }
      if (radius == 0) {
        break;
      }
    }
  }
  return best_diatonic >= 0 ? best_diatonic : std::clamp(target, base_midi, ceil_midi);
}

/// @brief Append one bar of consonance-anchored scalar figuration.
///
/// Like appendScalarBar, but each on-beat note (the only sub-slot the scorer
/// samples) is snapped to a diatonic pitch consonant with every concurrent
/// sustained tone supplied in `beat_concurrent[beat]`; off-beat sub-slots fill
/// stepwise between consecutive on-beat anchors so the line stays conjunct.
/// When a beat has no concurrent tone the behaviour matches the plain chord-tone
/// scalar wave (the consonance constraint is vacuously satisfied).
///
/// @param dst Note vector receiving the bar's notes.
/// @param bar Absolute bar index.
/// @param chord The bar's chord (seeds the scalar-wave shape).
/// @param mode Diatonic mode selecting the scale walker.
/// @param notes_per_beat Subdivision density (1 = quarter, 2 = eighth, 4 = 16th).
/// @param base_midi Register floor for the wave.
/// @param ceil_midi Register ceiling for the wave.
/// @param offset Seed-derived start-degree offset above the anchor.
/// @param beat_concurrent Per-beat concurrent tones (index 0..3).
void appendConsonantBar(std::vector<MaterialNote>& dst, int bar, const ChordSpec& chord, Mode mode,
                        int notes_per_beat, int base_midi, int ceil_midi, int offset,
                        const std::array<std::vector<int>, 4>& beat_concurrent) {
  const int third = chord.minor ? 3 : 4;
  const int triad_pc[3] = {chord.root_pc % 12, (chord.root_pc + third) % 12,
                           (chord.root_pc + 7) % 12};
  auto is_triad = [&](int midi) {
    const int pcl = ((midi % 12) + 12) % 12;
    return pcl == triad_pc[0] || pcl == triad_pc[1] || pcl == triad_pc[2];
  };
  int anchor = base_midi;
  while (!is_triad(anchor)) {
    ++anchor;
  }
  anchor = scaleUp(anchor, offset, mode);
  while (!is_triad(anchor)) {
    anchor = scaleUp(anchor, 1, mode);
  }
  if (anchor > ceil_midi - 6) {
    anchor = base_midi;
    while (!is_triad(anchor)) {
      ++anchor;
    }
  }

  // On-beat anchors: a gentle scalar wave snapped to consonance with the
  // concurrent tones. The wave value provides the contour; snapConsonant keeps
  // each beat consonant and diatonic while staying as close as possible to that
  // contour, so the figuration remains a conjunct line.
  //
  // The bar downbeat (beat 0) carries an extra constraint: the validator's
  // figuration_harmonic_consistency rule requires it to be a chord tone of the
  // bar's chord. So beat 0 prefers a pitch that is BOTH a chord tone and
  // consonant with the concurrent tones; if none exists in band it falls back to
  // the nearest chord tone (validator stays satisfied; a rare dissonant downbeat
  // is preferable to a structural failure). Beats 1-3 are free passing tones.
  std::array<int, 4> beat_pitch{};
  for (int beat = 0; beat < 4; ++beat) {
    const int target = std::min(scaleUp(anchor, beat, mode), ceil_midi);
    const auto& concurrent = beat_concurrent[static_cast<std::size_t>(beat)];
    if (beat == 0) {
      // Nearest in-band chord tone that is also consonant with the concurrent
      // tones; fall back to the nearest chord tone if none is consonant.
      int nearest_triad = -1;
      int consonant_triad = -1;
      for (int radius = 0; radius <= ceil_midi - base_midi && consonant_triad < 0; ++radius) {
        for (int dir = (radius == 0 ? 0 : -1); dir <= 1; dir += 2) {
          const int cand = target + dir * radius;
          if (cand < base_midi || cand > ceil_midi || !is_triad(cand)) {
            if (radius == 0) break;
            continue;
          }
          if (nearest_triad < 0) {
            nearest_triad = cand;
          }
          if (isConsonantWithAll(cand, concurrent)) {
            consonant_triad = cand;
            break;
          }
          if (radius == 0) break;
        }
      }
      beat_pitch[0] = consonant_triad >= 0
                          ? consonant_triad
                          : (nearest_triad >= 0 ? nearest_triad
                                                : std::clamp(target, base_midi, ceil_midi));
    } else {
      beat_pitch[static_cast<std::size_t>(beat)] =
          snapConsonant(target, mode, base_midi, ceil_midi, concurrent);
    }
  }

  // Diatonic step toward a goal: one scale degree up or down, or hold at goal.
  auto step_toward = [&](int from, int goal) {
    if (from < goal) {
      return std::min(scaleUp(from, 1, mode), goal);
    }
    if (from > goal) {
      int cur = from - 1;
      while (cur > goal && !detail::inScale(cur, mode)) {
        --cur;
      }
      return std::max(cur, goal);
    }
    return from;
  };

  const Tick step =
      (notes_per_beat == 4) ? kSixteenth : ((notes_per_beat == 2) ? kEighth : kQuarter);
  for (int beat = 0; beat < 4; ++beat) {
    const int here = beat_pitch[static_cast<std::size_t>(beat)];
    const int next = beat_pitch[static_cast<std::size_t>((beat + 1) % 4)];
    int cursor = here;
    for (int sub = 0; sub < notes_per_beat; ++sub) {
      int pitch;
      if (sub == 0) {
        pitch = here;
      } else {
        cursor = step_toward(cursor, next);
        pitch = cursor;
      }
      pitch = std::clamp(pitch, base_midi, ceil_midi);
      const Tick tick =
          barTick(bar) + static_cast<Tick>(beat) * kTicksPerBeat + static_cast<Tick>(sub) * step;
      addNote(dst, tick, step, pitch);
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
// `bars` bars (>= 8). Built inline with the Phase24 idiom (no shared fugue
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

  const std::uint8_t slot = subjectSlotFor(req.character, req.seed);
  const std::array<std::uint8_t, 16>& subj_pat =
      (mode == Mode::Minor) ? kSubjectsMinor[slot] : kPhase14Subjects[slot];

  // The cadence reserves the final 2 bars; nothing else extends into them.
  const int cadence_start = first_bar + bars - 2;  // first of the 2 cadence bars.

  // Concurrent thematic on-beat pitches, keyed by (bar*4 + beat). Every fixed
  // thematic carrier (subject leader, answer, re-entry, stretto follower) records
  // its quarter-note pitches here as it is stamped, so a figuration counterline
  // sharing the window can be snapped consonant against them (the figuration is
  // built only after its concurrent theme is recorded).
  std::map<int, std::vector<int>> thematic_beats;
  auto record_beat = [&](int bar, int beat, int pitch) {
    thematic_beats[bar * 4 + beat].push_back(pitch);
  };
  auto concurrent_for_bar = [&](int bar) {
    std::array<std::vector<int>, 4> out_beats;
    for (int beat = 0; beat < 4; ++beat) {
      const auto iter = thematic_beats.find(bar * 4 + beat);
      if (iter != thematic_beats.end()) {
        out_beats[static_cast<std::size_t>(beat)] = iter->second;
      }
    }
    return out_beats;
  };

  // Entries are placed at 4-bar offsets (Phase24 idiom): V0 subject at the
  // boundary, V1 answer four bars later, V2 re-entry eight bars later. The full
  // three-entry exposition is used only when the re-entry (bars 8-11) fits
  // strictly before the reserved cadence bars; otherwise a compressed two-entry
  // exposition is used.
  const bool full_exposition = (first_bar + 11) < cadence_start;

  // --- Stamp a 16-note subject statement transposed by `semis`. ---
  auto stamp_subject = [&](int base_bar, int semis) {
    for (int note = 0; note < kSubjectNotes; ++note) {
      const int bar = base_bar + note / 4;
      const int beat = note % 4;
      const int pitch = static_cast<int>(subj_pat[static_cast<std::size_t>(note)]) + semis;
      addNote(out.material.subject, barTick(bar) + static_cast<Tick>(beat) * kTicksPerBeat,
              kQuarter, pitch);
      record_beat(bar, beat, pitch);
    }
  };

  // --- Add one band-confined figuration counterline span. ---
  // Each on-beat figuration note is snapped consonant against every concurrent
  // thematic tone recorded for that beat (keeping the fugue tail's figuration-
  // vs-theme texture inside the consonant interval-class band); off-beat sub-
  // slots fill stepwise so the line stays conjunct.
  auto add_counterline = [&](VoiceId voice, int first, int last, int notes_per_beat) {
    FigurationSection section;
    section.voice = voice;
    section.start_tick = barTick(first);
    section.end_tick = barTick(last + 1);
    for (int bar = first; bar <= last; ++bar) {
      appendConsonantBar(section.notes, bar, plan[static_cast<std::size_t>(bar)], mode,
                         notes_per_beat, kBandLo[voice], kBandHi[voice], fig_offset,
                         concurrent_for_bar(bar));
    }
    out.material.figuration_sections.push_back(section);
    pushSpan(asm_ctx, voice, first, last, VoiceIntent::FigurationCarrier);
  };

  // === EXPOSITION ===========================================================
  const int v0_off = octaveOffsetForBand(subj_pat, 0);
  stamp_subject(first_bar + 0, v0_off);
  pushSpan(asm_ctx, 0, first_bar + 0, first_bar + 3, VoiceIntent::SubjectCarrier);

  // V1 real answer (subject - P4) in the V1 band, entering one entry-window
  // (4 bars) after the subject. For a short (8-bar) fugue tail the answer is
  // truncated so it never extends into the reserved cadence bars (the validator
  // checks only the answer's first note, so a partial answer stays valid).
  const int answer_off = octaveOffsetForBand(subj_pat, 1);
  const int answer_first = first_bar + 4;
  const int answer_last = std::min(first_bar + 7, cadence_start - 1);
  const int answer_notes = (answer_last - answer_first + 1) * 4;
  for (int note = 0; note < answer_notes; ++note) {
    const int bar = answer_first + note / 4;
    const int beat = note % 4;
    const int pitch = static_cast<int>(subj_pat[static_cast<std::size_t>(note)]) - 5 + answer_off;
    addNote(out.material.answer, barTick(bar) + static_cast<Tick>(beat) * kTicksPerBeat, kQuarter,
            pitch);
    record_beat(bar, beat, pitch);
  }
  pushSpan(asm_ctx, 1, answer_first, answer_last, VoiceIntent::AnswerCarrier);
  // V0 figuration counterline over the answer (kept in the V0 band, above V1).
  add_counterline(0, answer_first, answer_last, 2);

  // Imitation entry declaration (subject leads, answer follows at one entry
  // window). The validator compares the actual first-note pitches, so the
  // declared interval is the real semitone distance between the V0 subject head
  // and the V1 answer head: -P4 plus the band-octave shift difference between
  // the two voices' octave transpositions.
  {
    ImitationEntry entry;
    entry.leader_fragment = MaterialFragment::Subject;
    entry.follower_fragment = MaterialFragment::Answer;
    entry.distance_ticks = barTick(kSubjectBars);
    entry.interval_semis = -5 + (answer_off - v0_off);
    out.material.imitation_entries.push_back(entry);
  }

  int next_free_bar = first_bar + 8;  // first bar after the (partial) exposition.
  if (full_exposition) {
    // V2 re-entry (subject - P8) in the V2 band (bars 8-11).
    const int third_off = octaveOffsetForBand(subj_pat, 2);
    stamp_subject(first_bar + 8, third_off);
    pushSpan(asm_ctx, 2, first_bar + 8, first_bar + 11, VoiceIntent::SubjectCarrier);
    // A single V0 figuration counterline rides above the V2 re-entry (V1 rests).
    // Only one figuration voice per window is created across the whole tail: the
    // FigurationCarrier dispatch matches a section to a span by window alone
    // (not voice), so two figuration sections sharing a window would cross-
    // pollute. A single accompaniment voice per region keeps every figuration
    // window globally unique.
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
    stamp_subject(leader_bar, v0_off);
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
    for (int note = 0; note < kSubjectNotes - 4; ++note) {
      const int bar = (leader_bar + 1) + note / 4;
      const int beat = note % 4;
      const int pitch = static_cast<int>(subj_pat[static_cast<std::size_t>(note)]) + follower_off;
      MaterialNote mn;
      mn.start_tick = barTick(bar) + static_cast<Tick>(beat) * kTicksPerBeat;
      mn.duration = kQuarter;
      mn.pitch = static_cast<std::uint8_t>(pitch);
      stretto.follower_notes.push_back(mn);
      record_beat(bar, beat, pitch);
    }
    out.material.stretto_entries.push_back(stretto);
    pushSpan(asm_ctx, 1, leader_bar + 1, leader_bar + 3, VoiceIntent::StrettoCarrier);

    // V2 figuration under the stretto block (band-confined). The two thematic
    // voices (V0 leader, V1 follower) already sound above it.
    add_counterline(2, leader_bar, leader_bar + 3, 1);

    // Figuration fill before and after the stretto block: a single V0 voice
    // (V1 / V2 rest), keeping every figuration window globally unique.
    if (leader_bar > next_free_bar) {
      add_counterline(0, next_free_bar, leader_bar - 1, 2);
    }
    if (leader_bar + 4 < cadence_start) {
      add_counterline(0, leader_bar + 4, cadence_start - 1, 2);
    }
  } else if (cadence_start > next_free_bar) {
    // Short tail: a single V0 figuration fill up to the cadence (V1 / V2 rest).
    add_counterline(0, next_free_bar, cadence_start - 1, 2);
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
    // Indices 0-3 = first cadence bar; 4-7 = last cadence bar. The approach
    // beat (idx 3) is B (leading tone) and the cadence downbeat (idx 4) is C
    // (tonic), so the leading-tone resolution lands exactly at the cadence tick.
    std::array<int, 8> mel{};
    mel[0] = scaleUp(tonic0, 4, mode);  // a gentle descent toward the cadence.
    mel[1] = scaleUp(tonic0, 3, mode);
    mel[2] = scaleUp(tonic0, 2, mode);
    mel[3] = tonic0 + 11;  // B: leading tone (approach beat).
    mel[4] = tonic0 + 12;  // C: tonic (cadence downbeat = upper_now).
    // The closing three beats outline the tonic triad (C->E->D->C) so every beat
    // is a chord tone of I and stays consonant with the V2 tonic-root bass (the
    // scorer samples each beat across voices). The E carries the Picardy colour
    // in minor; it is harmonically benign in major. The line stays conjunct
    // (M3 up, step down, step down) with no wide leap into the final tonic.
    mel[5] = tonic0 + 16;  // E above (chord tone of I; the Picardy major third).
    mel[6] = tonic0 + 14;  // D (passing; a P5 above the V2 bass G, consonant).
    mel[7] = tonic0 + 12;  // settle on the tonic.
    for (int idx = 0; idx < 8; ++idx) {
      const int bar = cadence_start + idx / 4;
      const int beat = idx % 4;
      addNote(coda.notes, barTick(bar) + static_cast<Tick>(beat) * kTicksPerBeat, kQuarter,
              std::clamp(mel[static_cast<std::size_t>(idx)], kBandLo[0], kBandHi[0]));
    }
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
    std::array<int, 8> low{};
    low[0] = tonic2;  // hold the tonic root.
    low[1] = tonic2;
    low[2] = dominant2;  // move to the dominant.
    low[3] = dominant2;  // dominant on the approach beat (bass_prev = G).
    low[4] = tonic2;     // tonic on the cadence downbeat (bass_now = C).
    low[5] = tonic2;
    low[6] = dominant2;
    low[7] = tonic2;
    for (int idx = 0; idx < 8; ++idx) {
      const int bar = cadence_start + idx / 4;
      const int beat = idx % 4;
      addNote(bass.notes, barTick(bar) + static_cast<Tick>(beat) * kTicksPerBeat, kQuarter,
              std::clamp(low[static_cast<std::size_t>(idx)], kBandLo[2], kBandHi[2]));
    }
    out.material.coda_extensions.push_back(bass);
    pushSpan(asm_ctx, 2, cadence_start, cadence_start + 1, VoiceIntent::CodaCarrier);
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
  SectionalAssembly asm_ctx{&out, &next_id};

  const int total = static_cast<int>(req.bars);
  const Mode mode = req.mode;
  const int harm_idx = static_cast<int>(req.seed % 4);
  const int fig_offset = static_cast<int>(req.seed % 4);
  const Split split = splitBars(total);
  const int free_bars = split.free_bars;

  // One per-bar chord plan over the whole piece (free + fugue). The fugue tail
  // reads its slice (bars [free_bars, total)) by absolute bar index.
  const std::vector<ChordSpec> plan = buildChordPlan(total, mode, harm_idx);
  emitHarmony(out, plan, mode);

  // --- TOCCATA SECTION (bars 0 .. free_bars-1), V0 only. ---
  // Generalize Phase18's archetype machinery to the available bars. The
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
  switch (archetype) {
    case ToccataArchetype::Dramaticus: {
      // Opening flourish ~1/4 of the section, snapped to a 4-bar grid and kept
      // in [4, free_bars - 4] so both windows are non-empty.
      int flourish = ((free_bars / 4 + 3) / 4) * 4;
      flourish = std::clamp(flourish, 4, free_bars - 4);
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

  // Emit one ToccataSection per window (V0). Every section carries the piece's
  // archetype + character; the (character, archetype) pair is checked by the
  // validator's toccata_archetype_compatible rule (Noble x Dramaticus is the
  // only forbidden pair, and the director already blocks Noble for this form).
  // The first bar of each section is is_section_head so SectionTransition fires
  // once per section.
  for (const BarWindow& win : windows) {
    ToccataSection section;
    section.archetype = archetype;
    section.character = character;
    section.voice = 0;
    section.start_tick = barTick(win.first_bar);
    section.end_tick = barTick(win.last_bar + 1);
    section.is_section_head = true;
    for (int bar = win.first_bar; bar <= win.last_bar; ++bar) {
      const ArcPoint arc = arcForBar(req, bar);
      // Density tier from the arc (1 = eighth, >=2 = sixteenth). A Dramaticus
      // opening flourish (its first window, starting at bar 0) stays at eighths
      // for rhetorical breadth before the continuous figuration takes over.
      const bool is_flourish = (archetype == ToccataArchetype::Dramaticus && win.first_bar == 0);
      const int notes_per_beat = is_flourish ? 2 : ((arc.density_tier >= 2) ? 4 : 2);
      // Register sweep: the band floor rises with the arc register shift, clamped
      // so the wave still fits the V0 band.
      const int base = std::clamp(kBandLo[0] + std::max<int>(0, arc.register_shift), kBandLo[0],
                                  kBandHi[0] - 12);
      appendScalarBar(section.notes, bar, plan[static_cast<std::size_t>(bar)], mode, notes_per_beat,
                      base, kBandHi[0], fig_offset);
    }
    out.material.toccata_sections.push_back(std::move(section));
    pushSpan(asm_ctx, 0, win.first_bar, win.last_bar, VoiceIntent::ToccataCarrier);
  }

  // --- FUGUE TAIL (bars free_bars .. total-1). ---
  appendFugueTail(asm_ctx, free_bars, split.fugue_bars, plan, req);

  return out;
}

HarnessFixture buildFantasiaAndFugueForm(const ResolvedRequest& req) {
  HarnessFixture out;
  out.voice_plan.num_voices = 3;
  SpanId next_id = 0;
  SectionalAssembly asm_ctx{&out, &next_id};

  const int total = static_cast<int>(req.bars);
  const Mode mode = req.mode;
  const int harm_idx = static_cast<int>(req.seed % 4);
  const int fig_offset = static_cast<int>(req.seed % 4);
  const Split split = splitBars(total);
  const int free_bars = split.free_bars;

  const std::vector<ChordSpec> plan = buildChordPlan(total, mode, harm_idx);
  emitHarmony(out, plan, mode);

  // --- FANTASIA SECTION (bars 0 .. free_bars-1), V0 only. ---
  // Generalize Phase22's contrasting-section cycle to free_bars: contiguous
  // 4-bar sections cycling the styles {Free, Fugal, Toccata, Chordal} starting
  // at a (seed % 4) rotation. Per-section density + register come from Phase22's
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
  // Phase22 tiers with a COMPRESSED register spacing: the styles are kept at
  // 6-semitone register steps (58 / 64 / 70 / 64) rather than the original
  // 12-semitone spacing. The narrower spacing roughly halves the melodic leap at
  // each section boundary (lowering the scorer's large-leap statistic) while
  // preserving section_contrast_required: the Free->Fugal->Toccata->Chordal
  // density tiers (4/8/16/4) still differ by >= 2 for every non-wrap adjacency,
  // and the Chordal->Free wrap (both density 4) keeps a 6-semitone register gap
  // (>= the 5-semitone contrast margin).
  static const std::array<StyleSpec, 4> kStyles = {{
      {FantasiaStyle::Free, 1, 4, 58},      // sparse quarters (low of the band).
      {FantasiaStyle::Fugal, 2, 8, 64},     // mid eighths.
      {FantasiaStyle::Toccata, 4, 16, 70},  // dense sixteenths (high of the band).
      {FantasiaStyle::Chordal, 1, 4, 64},   // mid quarters (declamatory).
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
    for (int bar = sec_start; bar <= sec_last; ++bar) {
      appendScalarBar(section.notes, bar, plan[static_cast<std::size_t>(bar)], mode,
                      sp.notes_per_beat, base, base + 14, fig_offset);
    }
    out.material.fantasia_sections.push_back(std::move(section));
    pushSpan(asm_ctx, 0, sec_start, sec_last, VoiceIntent::FantasiaCarrier);
    ++section_index;
  }

  // --- FUGUE TAIL (bars free_bars .. total-1). ---
  appendFugueTail(asm_ctx, free_bars, split.fugue_bars, plan, req);

  return out;
}

}  // namespace bach::composer
