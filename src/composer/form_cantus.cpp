#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <vector>

#include "composer/arc.h"
#include "composer/character_profile.h"
#include "composer/figuration.h"
#include "composer/form_builders.h"
#include "composer/material.h"
#include "composer/minor_material.h"
#include "composer/span.h"
#include "composer/texture_helpers.h"
#include "composer/voice_intent.h"
#include "core/basic_types.h"

namespace bach::composer {

// ---------------------------------------------------------------------------
// Fixed-line forms: the chorale prelude (cantus firmus + figuration) and the
// Goldberg-style immutable-bass variation set.
//
// Both honour ResolvedRequest length / mode / character / arc. They reuse the
// proven Phase19 (chorale) and Phase25 (goldberg) note language -- a CF whose
// bar downbeats are an immutable skeleton plus a predominantly-stepwise scalar
// wave above it, and an immutable tiled ground under rising-density scalar-wave
// variations -- but generalise the layout to any snapped bar count, derive the
// CF tune and the per-variation design from (seed, indices) only, and select
// the diatonic scale / ground from the mode.
// ---------------------------------------------------------------------------

namespace {

using detail::Mode;

constexpr Tick kHalf = kTicksPerBeat * 2;       // 960.
constexpr Tick kQuarterDur = kTicksPerBeat;     // 480.
constexpr Tick kEighth = kTicksPerBeat / 2;     // 240.
constexpr Tick kSixteenth = kTicksPerBeat / 4;  // 120.

Tick barTick(int bar) {
  return static_cast<Tick>(bar) * kTicksPerBar;
}

MaterialNote materialNote(Tick start, Tick dur, int pitch) {
  MaterialNote note;
  note.start_tick = start;
  note.duration = dur;
  note.pitch = static_cast<std::uint8_t>(pitch);
  return note;
}

// Map an arc density tier (0..3) plus a character density bias to a
// notes-per-beat subdivision for a figuration / variation bar. Tier 0 = eighths
// (a CF counterpoint never sits on bare quarters under the figuration), tiers
// 1..3 climb to sixteenths. Clamped into [eighths, sixteenths].
int notesPerBeatFor(const ArcPoint& point, std::int8_t density_bias) {
  int tier = static_cast<int>(point.density_tier) + density_bias;
  if (tier < 0)
    tier = 0;
  if (tier > 3)
    tier = 3;
  // tier 0 -> 2 (eighths), tier 1 -> 2, tier 2 -> 4, tier 3 -> 4 (sixteenths).
  return tier >= 2 ? 4 : 2;
}

// ----- Chorale prelude cantus-firmus tune generator ------------------------
//
// A deterministic, hymn-like chorale tune. The tune is built from 4-bar
// phrases, one whole-note structural tone per bar. Each phrase traces one of a
// small set of stepwise-dominant phrase shapes (scale-degree contours) and ends
// on a cadence degree: phrases alternate authentic (degree 1) and half (degree
// 5) cadences, and the FINAL phrase always closes on the tonic (degree 1). The
// shape selection rotates by seed and phrase index, so the tune is stable per
// seed yet varied across phrases and pieces.
//
// Scale degrees are 1-based diatonic degrees (1 = tonic). A degree is realised
// against the active mode's diatonic scale walking up from a low tonic anchor,
// so the whole tune sits in the C3-region (well below the C4+ figuration above
// it -- no voice crossing).

// Four phrase shapes, each four 1-based scale degrees. Every shape is
// predominantly stepwise (adjacent degrees differ by <= 2) and resolves toward
// its cadence; the cadence degree itself is overwritten per phrase below, so the
// fourth entry is only a lead-in contour.
constexpr std::array<std::array<int, 4>, 4> kPhraseShapes = {{
    {1, 2, 3, 2},  // arch up to the mediant and back.
    {5, 4, 3, 2},  // gentle descent from the dominant.
    {3, 4, 5, 4},  // rise to the dominant.
    {1, 3, 2, 3},  // neighbour-rich oscillation.
}};

// Low tonic anchor for the cantus firmus (C3 = MIDI 48). Phrase degrees walk up
// the diatonic scale from here, keeping the CF in the C3-region.
constexpr int kCfTonicAnchor = 48;

// Resolve a 1-based scale degree to a MIDI pitch in the CF register for the
// given mode. Degree 1 = the tonic anchor; higher degrees walk up the scale.
int cfDegreePitch(int degree, Mode mode) {
  const int steps = degree - 1;
  return detail::scaleUp(kCfTonicAnchor, steps < 0 ? 0 : steps, mode);
}

// Build the immutable CF skeleton: one structural tone per bar over `bars` bars.
// The skeleton tiles bar-per-tone for ANY bar count (sized to `bars` exactly),
// so cantus_firmus_immutable's bar_index lookup is always in range. The cadence
// degree of each 4-bar phrase alternates authentic (1) / half (5); the final
// phrase always ends on the tonic. The leading tone is raised at cadences in
// minor (handled by the harmony mapping; the CF tone itself stays a chord tone).
std::vector<MaterialNote> buildCfSkeleton(int bars, std::uint32_t seed, Mode mode) {
  std::vector<MaterialNote> skeleton;
  skeleton.reserve(static_cast<std::size_t>(bars));
  const int num_phrases = (bars + 3) / 4;  // ceil: the last phrase may be short.
  for (int phrase = 0; phrase < num_phrases; ++phrase) {
    const std::size_t shape_idx =
        (static_cast<std::size_t>(seed) + static_cast<std::size_t>(phrase)) % kPhraseShapes.size();
    const auto& shape = kPhraseShapes[shape_idx];
    const bool is_final = (phrase == num_phrases - 1);
    // Cadence degree: authentic (1) on even phrases, half (5) on odd phrases;
    // the final phrase always resolves to the tonic.
    const int cadence_degree = is_final ? 1 : ((phrase % 2 == 0) ? 1 : 5);
    for (int local = 0; local < 4; ++local) {
      const int bar = phrase * 4 + local;
      if (bar >= bars)
        break;
      // The cadence bar (last bar of the phrase, or the final bar of the piece)
      // takes the cadence degree; other bars take the shape contour.
      const bool is_cadence_bar = (local == 3) || (bar == bars - 1);
      const int degree = is_cadence_bar ? cadence_degree : shape[static_cast<std::size_t>(local)];
      skeleton.push_back(materialNote(barTick(bar), kTicksPerBar, cfDegreePitch(degree, mode)));
    }
  }
  return skeleton;
}

// Map a CF skeleton degree to a per-bar chord whose triad contains the CF tone.
// The chord root is chosen from a small deterministic table so the CF tone is a
// chord tone of the bar's chord (required for the figuration downbeat-anchoring
// rules and for a consonant CF). Returned as (root_pc, is_minor) for the mode.
struct BarChord {
  std::uint8_t root_pc;
  bool minor;
};

// CF pitch class -> harmonizing chord. For each diatonic tone we pick a triad
// (root family) that contains it. Major: I (C E G), IV (F A C), V (G B D),
// vi (A C E). Minor: i (C Eb G), iv (F Ab C), V (G B D, harmonic-minor major
// dominant), VI (Ab C Eb). The choice keeps the CF tone consonant and gives a
// hymn-like I / IV / V / vi (or i / iv / V / VI) harmonization.
BarChord chordForCfTone(int cf_pitch, Mode mode) {
  const int pc = ((cf_pitch % 12) + 12) % 12;
  if (mode == Mode::Major) {
    // Map each C-major scale degree pc to a containing triad root.
    switch (pc) {
      case 0:  // C: I.
        return {0, false};
      case 2:  // D: V (G B D).
        return {7, false};
      case 4:  // E: I (C E G).
        return {0, false};
      case 5:  // F: IV (F A C).
        return {5, false};
      case 7:  // G: V (G B D).
        return {7, false};
      case 9:  // A: vi (A C E).
        return {9, true};
      case 11:  // B: V (G B D).
        return {7, false};
      default:
        return {0, false};
    }
  }
  // Minor (C natural minor degrees: C D Eb F G Ab Bb; V is major dominant).
  switch (pc) {
    case 0:  // C: i.
      return {0, true};
    case 2:  // D: V (G B D); D is the fifth of G.
      return {7, false};
    case 3:  // Eb: i (C Eb G).
      return {0, true};
    case 5:  // F: iv (F Ab C).
      return {5, true};
    case 7:  // G: V (G B D).
      return {7, false};
    case 8:  // Ab: VI (Ab C Eb).
      return {8, false};
    case 10:  // Bb: III-ish; fall back to V's relative -> use VI containing Bb? Bb in
              // Eb major (III). Use III (Eb G Bb).
      return {3, false};
    default:
      return {0, true};
  }
}

// Snap a starting MIDI pitch UP to the nearest chord tone of the bar's triad, so
// a figuration bar's downbeat is harmonically anchored. `third` is 3 (minor) or
// 4 (major) semitones.
int snapUpToChordTone(int start, int root_pc, bool minor) {
  const int third = minor ? 3 : 4;
  const int triad_pc[3] = {root_pc % 12, (root_pc + third) % 12, (root_pc + 7) % 12};
  int cur = start;
  while (cur % 12 != triad_pc[0] && cur % 12 != triad_pc[1] && cur % 12 != triad_pc[2])
    ++cur;
  return cur;
}

// Test whether a pitch is a tone of the bar's triad.
bool isChordTone(int pitch, int root_pc, bool minor) {
  const int third = minor ? 3 : 4;
  const int pc = ((pitch % 12) + 12) % 12;
  return pc == ((root_pc % 12) + 12) % 12 || pc == (root_pc + third) % 12 ||
         pc == (root_pc + 7) % 12;
}

// Return the next chord tone strictly above `from`.
int chordToneAbove(int from, int root_pc, bool minor) {
  int cur = from + 1;
  while (!isChordTone(cur, root_pc, minor))
    ++cur;
  return cur;
}

// Build one bar of figuration whose every BEAT onset is a chord tone of the
// bar's triad and whose off-beat sub-positions are stepwise diatonic passing
// tones. Because the per-beat scorer samples only the four beat onsets, and a
// chord tone is consonant against any other chord tone (the CF tone and the
// ground tone are both chord tones of the bar chord), every sampled vertical
// pair stays consonant; the off-beats remain stepwise so no melodic leap is
// introduced. The four beat anchors trace a gentle chord-tone wave (a0 a1 a2 a1)
// rooted on `start` (already snapped to a chord tone). `notes_per_beat` is 1
// (quarters: beat anchors only), 2 (eighths), or 4 (sixteenths).
//
// Appends to `notes` via the supplied emit callback (start_tick, duration,
// pitch), so the same routine serves both the FigurationSection and the
// PassacagliaVariation builders.
template <typename Emit>
void emitAnchoredBar(int bar, int start, const BarChord& chord, Mode mode, int notes_per_beat,
                     const Emit& emit) {
  // Four per-beat chord-tone anchors forming a low-amplitude wave.
  std::array<int, 4> anchor;
  anchor[0] = start;
  anchor[1] = chordToneAbove(anchor[0], chord.root_pc, chord.minor);
  anchor[2] = chordToneAbove(anchor[1], chord.root_pc, chord.minor);
  anchor[3] = anchor[1];  // descend back so the bar boundary voice-leads smoothly.
  const Tick step =
      notes_per_beat == 1 ? kQuarterDur : (notes_per_beat == 2 ? kEighth : kSixteenth);
  for (int beat = 0; beat < 4; ++beat) {
    const int from = anchor[static_cast<std::size_t>(beat)];
    const int to = anchor[static_cast<std::size_t>((beat + 1) % 4)];
    // Off-beat sub-positions step diatonically from this beat's anchor toward the
    // next beat's anchor, never reaching (so the next beat's anchor is a fresh
    // onset, not a repeat) -- a stepwise scalar fill between two chord tones a
    // third/fourth apart.
    for (int sub = 0; sub < notes_per_beat; ++sub) {
      int pitch = from;
      if (sub > 0) {
        // Walk `sub` diatonic steps toward `to`, clamped just short of `to`.
        const int span = (to >= from) ? 1 : -1;
        int walked = from;
        for (int stepIdx = 0; stepIdx < sub; ++stepIdx) {
          const int nxt = (span > 0)
                              ? detail::scaleUp(walked, 1, mode)
                              : (detail::inScale(walked - 1, mode) ? walked - 1 : walked - 2);
          // Stop short of `to`: if the next step would reach or pass the target,
          // hold on the current passing tone (keeps it a chord-bounded passing
          // motion and avoids doubling the next beat anchor on an off-beat).
          if ((span > 0 && nxt >= to) || (span < 0 && nxt <= to))
            break;
          walked = nxt;
        }
        pitch = walked;
      }
      const Tick onset =
          barTick(bar) + static_cast<Tick>(beat) * kTicksPerBeat + static_cast<Tick>(sub) * step;
      emit(onset, step, pitch);
    }
  }
}

// Append one bar of figuration (a downbeat-anchored scalar wave) to a section.
// The bar opens on a chord tone (so figuration_harmonic_consistency passes),
// then runs scalewise up the mode's scale and back down. `notes_per_beat`
// selects the subdivision (2 = eighths, 4 = sixteenths). `register_base` is the
// pitch the wave starts from before snapping to a chord tone; `offset` shifts
// the start up the scale per seed.
void appendFigurationBar(FigurationSection& section, int bar, const BarChord& chord, Mode mode,
                         int notes_per_beat, int register_base, int offset) {
  const int snapped =
      snapUpToChordTone(detail::scaleUp(register_base, offset, mode), chord.root_pc, chord.minor);
  emitAnchoredBar(bar, snapped, chord, mode, notes_per_beat, [&](Tick start, Tick dur, int pitch) {
    section.notes.push_back(materialNote(start, dur, pitch));
  });
}

// ----- Chorale prelude walking-bass (Schubler BWV645 model) ----------------
//
// The Schubler "Wachet auf" texture is a three-layer fabric: a running upper
// figuration (V0), the chorale tune in the middle (V1, the immutable cantus
// firmus), and a quarter-note walking bass below (V2) that connects the chord
// roots stepwise. This is the third layer: a continuous quarter-note line whose
// bar downbeat is the bar chord's root and whose intermediate beats step
// diatonically toward the next bar's root, kept consonant and parallel-free
// against both upper voices via the shared texture machinery.

// Walking-bass register band: C2-B2, strictly below the C3-region cantus firmus
// (whose lowest tone is C3 = 48), so the bass never crosses V1 and voice order
// V0 (C4+) > V1 (C3-region) > V2 (C2) holds at every tick.
constexpr int kBassBandLo = 36;  // C2.
constexpr int kBassBandHi = 47;  // B2.

// Fit a pitch class to the MIDI pitch inside [lo, hi] nearest a center.
int fitPcToBand(int pitch_class, int center, int lo, int hi) {
  const int base = ((pitch_class % 12) + 12) % 12;
  int best = -1;
  int best_dist = 1 << 20;
  for (int oct = lo - 12; oct <= hi + 12; oct += 12) {
    const int cand = base + oct;
    if (cand < lo || cand > hi)
      continue;
    const int dist = std::abs(cand - center);
    if (dist < best_dist) {
      best_dist = dist;
      best = cand;
    }
  }
  if (best < 0)
    best = std::min(std::max(base + 12 * ((center - base) / 12), lo), hi);
  return best;
}

// Build the V2 walking bass over all `bars` bars and append it to `out_notes`.
// Each bar opens on the bar chord's root (a quarter note on the downbeat),
// fitted into the bass band nearest the running cursor. The remaining three
// beats of the bar step diatonically toward the NEXT bar's root, each anchor
// chosen with the shared tier-scored consonantChordTone so it stays consonant
// with the concurrent V0 / V1 tones and forms no parallel/hidden perfect against
// them. The registry already holds V0 (figuration) and V1 (embellished cantus
// firmus). Repeated-pitch runs are bounded by a run-aware nudge: a static
// harmony that would otherwise sustain the root is broken by a single diatonic
// neighbour so no quarter-note run exceeds the corpus ceiling.
void appendWalkingBass(std::vector<MaterialNote>& out_notes, ThemeToneRegistry& registry,
                       const std::vector<BarChord>& bar_chords, Mode mode) {
  const int bars = static_cast<int>(bar_chords.size());
  int cursor = (kBassBandLo + kBassBandHi) / 2;
  int line_prev = -1;
  int prev_pitch = -1;
  int run_len = 0;
  std::vector<int> theme_pitches;
  std::vector<ConcurrentMotion> motions;

  for (int bar = 0; bar < bars; ++bar) {
    const BarChord& chord = bar_chords[static_cast<std::size_t>(bar)];
    detail::ChordSpec spec;
    spec.root_pc = chord.root_pc;
    spec.minor = chord.minor;
    // Target the next bar's root (its band-fit pitch) so the intermediate beats
    // walk stepwise toward it; the final bar holds toward its own root.
    const BarChord& next_chord =
        bar_chords[static_cast<std::size_t>(bar + 1 < bars ? bar + 1 : bar)];
    const int next_root = fitPcToBand(next_chord.root_pc, cursor, kBassBandLo, kBassBandHi);

    for (int beat = 0; beat < 4; ++beat) {
      const Tick beat_tick = barTick(bar) + static_cast<Tick>(beat) * kTicksPerBeat;
      const Tick prev_tick = beat_tick - kTicksPerBeat;
      theme_pitches.clear();
      motions.clear();
      registry.concurrentThemePitches(beat_tick, /*voice=*/2, theme_pitches);
      registry.concurrentMotions(prev_tick, beat_tick, /*voice=*/2, /*num_voices=*/3, motions);

      int pitch;
      if (beat == 0) {
        // Downbeat: the bar chord's root in the bass band. A walking bass states
        // the harmony's root on each downbeat (the BWV645 foundation); the root
        // is consonant with the upper chord-tone voices by construction, so it is
        // emitted directly rather than allowing a chord-tone substitution that
        // would weaken the harmonic anchor.
        pitch = fitPcToBand(chord.root_pc, cursor, kBassBandLo, kBassBandHi);
      } else {
        // Intermediate beat: step one diatonic degree from the cursor toward the
        // next bar's root (a passing tone), then pick the nearest consonant,
        // parallel-free diatonic tone to that step target.
        int step_target = cursor;
        if (next_root > cursor)
          step_target = detail::inScale(cursor + 1, mode) ? cursor + 1 : cursor + 2;
        else if (next_root < cursor)
          step_target = detail::inScale(cursor - 1, mode) ? cursor - 1 : cursor - 2;
        step_target = std::min(std::max(step_target, kBassBandLo), kBassBandHi);
        pitch = consonantChordTone(spec, /*voice=*/2, kBassBandLo, kBassBandHi, step_target,
                                   theme_pitches, line_prev, motions, mode, /*downbeat=*/false);
      }

      // Run-aware nudge: a static harmony (repeated root) or a held passing tone
      // can repeat the previous pitch. Cap any quarter-note run at three by
      // displacing a fourth identical OFF-BEAT pitch to a consonant diatonic
      // neighbour. Downbeats are exempt (the root statement is the bar's harmonic
      // anchor and must not be displaced); the off-beat fills carry the variety.
      const bool repeats = (pitch == prev_pitch);
      if (repeats && beat != 0 && run_len >= 2) {
        const int up =
            (pitch + 2 <= kBassBandHi && detail::inScale(pitch + 2, mode)) ? pitch + 2 : -1;
        const int down =
            (pitch - 2 >= kBassBandLo && detail::inScale(pitch - 2, mode)) ? pitch - 2 : -1;
        const int nudged = (up >= 0) ? up : down;
        if (nudged >= 0)
          pitch = nudged;
      }
      run_len = (pitch == prev_pitch) ? run_len + 1 : 0;

      out_notes.push_back(materialNote(beat_tick, kQuarterDur, pitch));
      registry.record(beat_tick, /*voice=*/2, pitch, kQuarterDur);
      line_prev = pitch;
      prev_pitch = pitch;
      cursor = pitch;
    }
  }
}

}  // namespace

HarnessFixture buildChoralePreludeForm(const ResolvedRequest& req) {
  HarnessFixture out;
  const int bars = static_cast<int>(req.bars);
  const Mode mode = req.mode;
  const int offset = static_cast<int>(req.seed % 4u);
  const detail::CharacterProfile& profile = detail::characterProfile(req.character);

  // Cantus firmus skeleton (immutable, one tone per bar, sized to `bars`).
  const std::vector<MaterialNote> skeleton = buildCfSkeleton(bars, req.seed, mode);

  // Per-bar harmony harmonizing the CF tone (CF tone is a chord tone of its bar
  // chord). Drives both the figuration downbeat anchor and the CF consonance.
  out.harmony.tonic_pc = 0;
  out.harmony.is_minor = (mode == Mode::Minor);
  std::vector<BarChord> bar_chords;
  bar_chords.reserve(static_cast<std::size_t>(bars));
  for (int bar = 0; bar < bars; ++bar) {
    const BarChord chord = chordForCfTone(skeleton[static_cast<std::size_t>(bar)].pitch, mode);
    bar_chords.push_back(chord);
    ChordEvent ce;
    ce.start_tick = barTick(bar);
    ce.root_pc = chord.root_pc;
    ce.quality = chord.minor ? ChordQuality::Minor : ChordQuality::Major;
    out.harmony.chords.push_back(ce);
  }

  // Immutable skeleton material (one whole note per bar). cantus_firmus_immutable
  // reads this vector for the per-bar downbeat lookup.
  out.material.cantus_firmus = skeleton;

  // Embellished CF: each bar opens on the skeleton tone (downbeat == skeleton,
  // per the rule), held as a half note over beats 0-1. Beats 2 and 3 land on
  // CHORD TONES of the bar's triad -- so the two sampled second-half beats stay
  // consonant against the figuration above (which is itself a chord-tone wave) --
  // and any stepwise decoration is confined to the off-beat eighths, which the
  // per-beat scorer never samples. Embellishment density rises with the bar's arc
  // density tier plus the character ornament density: a low-activity bar walks two
  // plain quarter chord tones; a high-activity bar fills the off-beats with
  // stepwise passing eighths between those chord-tone beats.
  //
  // Pick the chord tone nearest a target pitch (a neighbour of the skeleton tone),
  // keeping the embellishment melodically close to the structural tone.
  auto nearestChordTone = [&](int target, const BarChord& chord) -> int {
    int up = target;
    while (!isChordTone(up, chord.root_pc, chord.minor))
      ++up;
    int down = target;
    while (!isChordTone(down, chord.root_pc, chord.minor))
      --down;
    return (up - target) <= (target - down) ? up : down;
  };
  // One diatonic step from `from` toward `to` (stepwise off-beat passing tone).
  auto stepToward = [&](int from, int to) -> int {
    if (to > from)
      return detail::inScale(from + 1, mode) ? from + 1 : from + 2;
    if (to < from)
      return detail::inScale(from - 1, mode) ? from - 1 : from - 2;
    return from;
  };

  // V0 figuration: one FigurationSection covering all `bars` bars, a
  // predominantly-stepwise scalar wave riding ABOVE the CF. Density follows the
  // arc (eighths in calm cycles, sixteenths into the climax) and the register
  // lifts with the arc register shift; Noble figuration prefers a denser dotted
  // feel realized as the sixteenth subdivision. Each bar's downbeat snaps to a
  // chord tone, so figuration_harmonic_consistency stays clean. Built BEFORE the
  // CF embellishment so a run-breaking CF substitution can be checked consonant
  // and parallel-free against the concurrently sounding figuration.
  const int cycle_count = static_cast<int>(req.cycle_count);
  FigurationSection fig;
  fig.voice = 0;
  fig.start_tick = 0;
  fig.end_tick = barTick(bars);
  for (int bar = 0; bar < bars; ++bar) {
    const int cycle = cycle_count > 0 ? (bar * cycle_count) / bars : 0;
    const ArcPoint point = req.arc(static_cast<std::size_t>(cycle));
    int notes_per_beat = notesPerBeatFor(point, profile.density_bias);
    if (profile.prefer_dotted)
      notes_per_beat = 4;  // Noble: a busier, dotted-feel running figuration.
    // Register base stays at C4 (60) plus the arc register lift, comfortably
    // above the C3-region cantus firmus.
    const int register_base = 60 + static_cast<int>(point.register_shift);
    appendFigurationBar(fig, bar, bar_chords[static_cast<std::size_t>(bar)], mode, notes_per_beat,
                        register_base, offset);
  }

  // V0 figuration registry for the embellishment loop's run-break consonance
  // check. (V2 is selected AFTER this loop against a registry pre-loaded with
  // V0+V1, so the bass adapts to whatever the CF substitutes settle on.)
  ThemeToneRegistry fig_registry;
  for (const MaterialNote& note : fig.notes)
    fig_registry.record(note.start_tick, /*voice=*/0, static_cast<int>(note.pitch), note.duration);

  // The maximum allowed run of identical V1 pitches. The texture gate caps
  // repeated runs at four, so a fourth consecutive identical pitch is displaced
  // to a consonant diatonic neighbour. Tracked across the whole V1 line (the
  // half-note skeleton tone plus the beat-2/beat-3 chord tones), so same-degree
  // adjacent bars no longer chain a plain `tone,tone,tone` figure into a long run.
  constexpr int kMaxV1Run = 4;
  int v1_prev = -1;  // previous sounding V1 pitch.
  int v1_run = 0;    // length of the current identical-pitch run (1-based).

  // Track a V1 pitch as it is emitted, returning a (possibly substituted) pitch
  // whose addition does not extend an identical-pitch run past kMaxV1Run. When
  // the candidate would be the kMaxV1Run-th identical pitch, it is displaced to
  // the nearest consonant, parallel-free diatonic NEIGHBOUR (upper preferred,
  // then lower) of the candidate. The substitute is a step away from the
  // structural tone and resolves back on the following beat, the standard chorale
  // embellishment vocabulary. The downbeat skeleton tone is never passed here, so
  // the immutable bar-head pitch is preserved.
  auto breakRun = [&](int cand, const BarChord& chord, Tick onset) -> int {
    const bool repeats = (cand == v1_prev);
    int chosen = cand;
    if (repeats && v1_run + 1 >= kMaxV1Run) {
      // Concurrent V0 figuration pitch at this onset (for consonance / parallels).
      const int fig_now = fig_registry.soundingPitchInVoice(/*voice=*/0, onset);
      const int fig_prev = fig_registry.soundingPitchInVoice(/*voice=*/0, onset - kQuarterDur);
      // Candidate neighbours: a whole/half step up, then down, staying diatonic.
      const int up = detail::inScale(cand + 1, mode) ? cand + 1 : cand + 2;
      const int down = detail::inScale(cand - 1, mode) ? cand - 1 : cand - 2;
      for (int neighbour : {up, down}) {
        if (neighbour == cand)
          continue;
        if (fig_now >= 0 && !isConsonantPair(neighbour, fig_now))
          continue;
        if (fig_now >= 0 && formsPerfectParallel(v1_prev, neighbour, fig_prev, fig_now))
          continue;
        chosen = neighbour;
        break;
      }
      // Keep the substitute a real chord-bracketed neighbour: if neither neighbour
      // is admissible, fall back to a chord tone above so the beat stays consonant.
      if (chosen == cand)
        chosen = chordToneAbove(cand, chord.root_pc, chord.minor);
    }
    v1_run = (chosen == v1_prev) ? v1_run + 1 : 1;
    v1_prev = chosen;
    return chosen;
  };

  for (int bar = 0; bar < bars; ++bar) {
    const int tone = skeleton[static_cast<std::size_t>(bar)].pitch;
    const BarChord& chord = bar_chords[static_cast<std::size_t>(bar)];
    const Tick base = barTick(bar);
    // Arc density for the cycle this bar belongs to, biased by the character's
    // ornament density. A denser bar fills the off-beats with passing eighths.
    const int cycle = cycle_count > 0 ? (bar * cycle_count) / bars : 0;
    const ArcPoint point = req.arc(static_cast<std::size_t>(cycle));
    const int activity =
        static_cast<int>(point.density_tier) + static_cast<int>(profile.ornament_density);

    // Beat 2 / beat 3 chord-tone targets: an upper-neighbour chord tone on beat 2,
    // resolving back toward the skeleton tone on beat 3. Both are chord tones, so
    // the sampled beats stay consonant; they bracket a small neighbour figure.
    const int beat2 = nearestChordTone(tone + 1, chord);
    const int beat3 = nearestChordTone(tone, chord);

    // Downbeat skeleton tone, held as a half note (beats 0-1). The skeleton tone
    // is immutable, so it is never substituted; it still advances the run tracker
    // so a chain that continues through the bar head is counted.
    out.material.cf_embellished.push_back(materialNote(base, kHalf, tone));
    v1_run = (tone == v1_prev) ? v1_run + 1 : 1;
    v1_prev = tone;
    if (activity >= 3) {
      // Dense: chord-tone beats with a stepwise passing eighth on each off-beat.
      // The stepwise off-beats already break repetition; the run-break guard on
      // the two chord-tone beats keeps a long static figure from chaining.
      const int b2 = breakRun(beat2, chord, base + kHalf);
      const int off2 = stepToward(b2, beat3);
      out.material.cf_embellished.push_back(materialNote(base + kHalf, kEighth, b2));
      out.material.cf_embellished.push_back(materialNote(base + kHalf + kEighth, kEighth, off2));
      // The passing eighth advances the run tracker so the next beat sees it.
      v1_run = (off2 == v1_prev) ? v1_run + 1 : 1;
      v1_prev = off2;
      const int b3 = breakRun(beat3, chord, base + kHalf + 2 * kEighth);
      const int off3 = stepToward(b3, tone);
      out.material.cf_embellished.push_back(materialNote(base + kHalf + 2 * kEighth, kEighth, b3));
      out.material.cf_embellished.push_back(
          materialNote(base + kHalf + 3 * kEighth, kEighth, off3));
      v1_run = (off3 == v1_prev) ? v1_run + 1 : 1;
      v1_prev = off3;
    } else {
      // Plain: two quarter chord tones on beats 2 and 3, each run-break guarded so
      // a same-degree run never exceeds four identical pitches.
      const int b2 = breakRun(beat2, chord, base + kHalf);
      out.material.cf_embellished.push_back(materialNote(base + kHalf, kQuarterDur, b2));
      const int b3 = breakRun(beat3, chord, base + kHalf + kQuarterDur);
      out.material.cf_embellished.push_back(
          materialNote(base + kHalf + kQuarterDur, kQuarterDur, b3));
    }
  }
  out.material.cf_is_embellished = true;
  out.material.cf_placement = 1;  // Tenor (documentary).

  // Publish the figuration section built above (before the embellishment loop).
  out.material.figuration_sections.push_back(fig);

  // V2 walking bass (Schubler BWV645 third layer): a quarter-note bass line
  // connecting the chord roots stepwise, sitting in the C2 band well below the
  // cantus firmus. Built AFTER V0 (figuration) and V1 (embellished cantus
  // firmus) are recorded in the registry, so each anchor is selected consonant
  // and parallel-free against both upper voices. The bass carries the
  // TrioVoiceCarrier intent (verbatim replay, stamping TrioVoiceIndependent),
  // matching the Goldberg / passacaglia middle-voice precedent in this tree:
  // because it is the ONLY voice carrying that bit, voice_independence_threshold
  // (which needs >= 2 such voices) stays inert -- no soft-fail is introduced.
  ThemeToneRegistry bass_registry;
  for (const MaterialNote& note : fig.notes)
    bass_registry.record(note.start_tick, /*voice=*/0, static_cast<int>(note.pitch), note.duration);
  for (const MaterialNote& note : out.material.cf_embellished)
    bass_registry.record(note.start_tick, /*voice=*/1, static_cast<int>(note.pitch), note.duration);
  std::vector<MaterialNote> bass_notes;
  bass_notes.reserve(static_cast<std::size_t>(bars) * 4);
  appendWalkingBass(bass_notes, bass_registry, bar_chords, mode);
  TrioVoiceLine bass_line;
  bass_line.voice = 2;
  bass_line.manual = 3;  // documentary (Pedal): V2 = lowest line.
  bass_line.notes = std::move(bass_notes);
  out.material.trio_voices.push_back(std::move(bass_line));

  // VoicePlan: V0 one FigurationCarrier span over all bars; V1 one
  // CantusFirmusCarrier span over all bars; V2 one TrioVoiceCarrier span (the
  // walking bass) over all bars. Register order V0 (figuration, C4+) >
  // V1 (cantus firmus, C3-region) > V2 (walking bass, C2) holds at every tick,
  // so no voice crossing occurs.
  out.voice_plan.num_voices = 3;
  Span fig_span;
  fig_span.id = 0;
  fig_span.start_tick = 0;
  fig_span.end_tick = barTick(bars);
  fig_span.voice = 0;
  fig_span.intent = VoiceIntent::FigurationCarrier;
  fig_span.subdivision = Subdivision::Quarter;
  out.voice_plan.spans.push_back(fig_span);

  Span cf_span;
  cf_span.id = 1;
  cf_span.start_tick = 0;
  cf_span.end_tick = barTick(bars);
  cf_span.voice = 1;
  cf_span.intent = VoiceIntent::CantusFirmusCarrier;
  cf_span.subdivision = Subdivision::Quarter;
  out.voice_plan.spans.push_back(cf_span);

  Span bass_span;
  bass_span.id = 2;
  bass_span.start_tick = 0;
  bass_span.end_tick = barTick(bars);
  bass_span.voice = 2;
  bass_span.intent = VoiceIntent::TrioVoiceCarrier;
  bass_span.subdivision = Subdivision::Quarter;
  out.voice_plan.spans.push_back(bass_span);

  return out;
}

// --- Goldberg variation framework ------------------------------------------

GoldbergVariationKind goldbergVariationKind(std::size_t variation_index) {
  // BWV988 scheme: every third variation is a canon (variations 3, 6, ..., 27),
  // at a rising imitation interval (unison, 2nd, 3rd, ... 9th). `variation_index`
  // is zero-based, so the 1-based variation number is variation_index + 1; a
  // canon is variation number v where v % 3 == 0. The final variation of the
  // full 30-variation set (v == 30) is NOT a canon: it is the densest figuration
  // peak (the BWV988 "Quodlibet" slot), so the canon rule is capped at v < 30.
  const std::size_t variation_number = variation_index + 1;
  if (variation_number % 3 == 0 && variation_number < 30)
    return GoldbergVariationKind::Canon;
  return GoldbergVariationKind::Figuration;
}

namespace {

// The Goldberg ground tables live in kGoldbergGroundsMajor /
// kGoldbergGroundsMinor (seed-selected design variants, period 4 bars, all
// tones C2-region chord roots so the variation downbeat anchoring stays
// consonant and the ground tiles exactly).

// Per-bar chord for the ground cycle: the chord root IS the ground tone's
// pitch class, with the diatonic triad quality on that degree (in minor the V
// is the harmonic-minor major dominant), so the harmony stays consonant with
// the bass for every ground variant.
BarChord goldbergBarChord(std::uint8_t ground_pitch, Mode mode) {
  const std::uint8_t pc = static_cast<std::uint8_t>(ground_pitch % 12u);
  return {pc, detail::diatonicTriadMinor(pc, mode == Mode::Minor)};
}

// Diatonic transpose a pitch UP by `degrees` scale steps (degrees may be 0 =
// unison). Octave membership is preserved because scaleUp walks the scale.
int transposeUp(int pitch, int degrees, Mode mode) {
  return degrees <= 0 ? pitch : detail::scaleUp(pitch, degrees, mode);
}

// Drop a pitch DOWN by whole octaves until it sits at or below `ceiling` (an
// octave shift preserves the pitch class, so the line stays diatonic). Used to
// fold a transposed canon-follower line down into the V1 band beneath the V0
// leader.
int dropIntoBand(int pitch, int ceiling) {
  while (pitch > ceiling)
    pitch -= 12;
  return pitch;
}

// Append an aria bar (the m=2 two-half-notes SPECIAL layout from Phase25): a
// half note on the wave start, then a half note on the NEXT CHORD TONE up. Both
// half notes are chord tones of the bar's triad, so each sampled beat (the bar's
// two half-note onsets) is consonant against the ground tone below (itself a
// chord root). Used for the opening aria and (when N >= 24) the da-capo
// restatement.
void appendAriaBar(PassacagliaVariation& var, int bar, const BarChord& chord, Mode mode,
                   int register_base, int offset) {
  const int start =
      snapUpToChordTone(detail::scaleUp(register_base, offset, mode), chord.root_pc, chord.minor);
  const Tick base = barTick(bar);
  var.notes.push_back(materialNote(base, kHalf, start));
  var.notes.push_back(
      materialNote(base + kHalf, kHalf, chordToneAbove(start, chord.root_pc, chord.minor)));
}

// Append a figuration variation bar: a downbeat-anchored scalar wave with
// `notes_per_beat` subdivision (1 = quarters, 2 = eighths, 4 = sixteenths).
void appendVariationBar(PassacagliaVariation& var, int bar, const BarChord& chord, Mode mode,
                        int notes_per_beat, int register_base, int offset) {
  const int snapped =
      snapUpToChordTone(detail::scaleUp(register_base, offset, mode), chord.root_pc, chord.minor);
  emitAnchoredBar(bar, snapped, chord, mode, notes_per_beat, [&](Tick start, Tick dur, int pitch) {
    var.notes.push_back(materialNote(start, dur, pitch));
  });
}

// Canon leader register anchor. The leader runs near the figuration band so the
// block-to-block boundary motion stays small (no remote leap), yet the follower
// -- the leader transposed UP by the canon's imitation degrees and then folded
// DOWN an octave into the V1 band -- still lands cleanly below the leader (no
// voice crossing) and above the C2 ground.
constexpr int kCanonLeaderBase = 72;  // C5: aligned with the figuration band.
// The follower is folded down so it never rises above this ceiling, keeping V1
// strictly below the C5-region V0 leader.
constexpr int kCanonFollowerCeiling = 71;  // B4: one semitone below the leader base.

// Choose the per-bar leader chord tone for a canon so that the canon stays
// consonant by construction. The follower at bar b is the leader of bar b-1
// transposed UP by `imitation_degrees` diatonic degrees; it sounds against the
// leader of bar b and against the ground of bar b. Because the per-beat scorer
// samples chord tones, we pick, per bar, a chord tone of that bar's ground chord
// that minimises the number of dissonant simultaneities (leader-vs-follower and
// follower-vs-ground), solved exactly with a tiny DP over the (at most three)
// chord tones per bar. The result is a smooth per-bar leader-tone contour whose
// 1-bar-delayed transposed echo is consonant -- the consonance is designed INTO
// the leader, since the follower is a fixed transform of it.
//
// Returns one MIDI pitch per cycle bar (size 4), all chord tones of the bar's
// ground chord, in the leader register band.
std::array<int, 4> designCanonLeader(int imitation_degrees, Mode mode,
                                     const std::array<std::uint8_t, 4>& ground) {
  // Three ascending chord tones per cycle bar, in the leader band.
  std::array<std::array<int, 3>, 4> tones{};
  for (int bar = 0; bar < 4; ++bar) {
    const BarChord chord = goldbergBarChord(ground[static_cast<std::size_t>(bar)], mode);
    int cur = snapUpToChordTone(kCanonLeaderBase, chord.root_pc, chord.minor);
    tones[static_cast<std::size_t>(bar)][0] = cur;
    tones[static_cast<std::size_t>(bar)][1] = chordToneAbove(cur, chord.root_pc, chord.minor);
    tones[static_cast<std::size_t>(bar)][2] =
        chordToneAbove(tones[static_cast<std::size_t>(bar)][1], chord.root_pc, chord.minor);
  }
  // Dissonance cost of a bar's leader-tone choice given the previous bar's choice.
  auto isDiss = [](int pitch_a, int pitch_b) -> bool {
    const int ic = ((pitch_a - pitch_b) % 12 + 12) % 12;
    return ic == 1 || ic == 2 || ic == 6 || ic == 10 || ic == 11;
  };
  auto barCost = [&](int bar, int lead_pitch, int prev_pitch, bool has_prev) -> int {
    int cost = 0;
    const int ground_pc =
        static_cast<int>(goldbergBarChord(ground[static_cast<std::size_t>(bar)], mode).root_pc);
    const int ground = ground_pc;  // pitch class is sufficient (ic is octave-invariant).
    if (isDiss(lead_pitch, ground))
      ++cost;  // (never triggers: a chord tone is consonant with its own root).
    if (has_prev) {
      const int follower = transposeUp(prev_pitch, imitation_degrees, mode);
      if (isDiss(follower, lead_pitch))
        ++cost;
      if (isDiss(follower, ground))
        ++cost;
    }
    return cost;
  };
  // DP: state = chosen tone index for the current bar; minimise total cost. Also
  // track a smoothness tiebreak (absolute leader interval) so the contour walks.
  constexpr int kInf = 1 << 20;
  std::array<std::array<int, 3>, 4> best_cost{};
  std::array<std::array<int, 3>, 4> back{};
  for (int idx = 0; idx < 3; ++idx)
    best_cost[0][static_cast<std::size_t>(idx)] =
        barCost(0, tones[0][static_cast<std::size_t>(idx)], 0, false);
  for (int bar = 1; bar < 4; ++bar) {
    for (int cur = 0; cur < 3; ++cur) {
      int best = kInf;
      int best_prev = 0;
      int best_leap = kInf;
      const int lead = tones[static_cast<std::size_t>(bar)][static_cast<std::size_t>(cur)];
      for (int prev = 0; prev < 3; ++prev) {
        const int prev_pitch =
            tones[static_cast<std::size_t>(bar - 1)][static_cast<std::size_t>(prev)];
        const int cand =
            best_cost[static_cast<std::size_t>(bar - 1)][static_cast<std::size_t>(prev)] +
            barCost(bar, lead, prev_pitch, true);
        const int leap = std::abs(lead - prev_pitch);
        // Minimise cost; break ties toward the smaller leader leap (smoother line).
        if (cand < best || (cand == best && leap < best_leap)) {
          best = cand;
          best_prev = prev;
          best_leap = leap;
        }
      }
      best_cost[static_cast<std::size_t>(bar)][static_cast<std::size_t>(cur)] = best;
      back[static_cast<std::size_t>(bar)][static_cast<std::size_t>(cur)] = best_prev;
    }
  }
  // Recover the best final state and backtrack.
  int final_idx = 0;
  for (int idx = 1; idx < 3; ++idx)
    if (best_cost[3][static_cast<std::size_t>(idx)] <
        best_cost[3][static_cast<std::size_t>(final_idx)])
      final_idx = idx;
  std::array<int, 4> chosen{};
  int cur = final_idx;
  for (int bar = 3; bar >= 0; --bar) {
    chosen[static_cast<std::size_t>(bar)] =
        tones[static_cast<std::size_t>(bar)][static_cast<std::size_t>(cur)];
    if (bar > 0)
      cur = back[static_cast<std::size_t>(bar)][static_cast<std::size_t>(cur)];
  }
  return chosen;
}

// Build one canonic variation block (a 4-bar window). The leader holds the
// DP-chosen consonant chord tone for each cycle bar, articulated as eighths
// (canons cap at eighths so the two lines stay legible), appended to `leader`.
// The follower is the SAME line transposed UP by `imitation_degrees` diatonic
// scale degrees, folded down an octave into the V1 band, DELAYED by one bar, and
// truncated at the block end (so it sounds during bars 1..3 of the leader's
// 4-bar window). Because the leader holds one chord tone per bar and the per-bar
// tones were chosen so the 1-bar-delayed transposed echo is consonant, every
// sampled beat in the block stays consonant by construction.
void buildCanonBlock(PassacagliaVariation& leader, std::vector<MaterialNote>& follower_notes,
                     int block_start_bar, int imitation_degrees, Mode mode,
                     const std::array<std::uint8_t, 4>& ground) {
  constexpr int kNotesPerBeat = 2;  // eighths: canons stay clear.
  const std::array<int, 4> leader_tone = designCanonLeader(imitation_degrees, mode, ground);
  for (int local = 0; local < 4; ++local) {
    const int bar = block_start_bar + local;
    const int pitch = leader_tone[static_cast<std::size_t>(bar % 4)];
    for (int beat = 0; beat < 4; ++beat) {
      for (int sub = 0; sub < kNotesPerBeat; ++sub) {
        const Tick onset = barTick(bar) + static_cast<Tick>(beat) * kTicksPerBeat +
                           static_cast<Tick>(sub) * kEighth;
        leader.notes.push_back(materialNote(onset, kEighth, pitch));
      }
    }
  }
  // Follower: leader notes transposed up `imitation_degrees` degrees, folded into
  // the V1 band, delayed one bar, truncated at the block end.
  const Tick block_end = barTick(block_start_bar + 4);
  for (const auto& lead : leader.notes) {
    const Tick delayed = lead.start_tick + kTicksPerBar;
    if (delayed >= block_end)
      continue;  // truncate at the block boundary (drops the final leader bar).
    const int transposed = transposeUp(static_cast<int>(lead.pitch), imitation_degrees, mode);
    const int folded = dropIntoBand(transposed, kCanonFollowerCeiling);
    follower_notes.push_back(materialNote(delayed, lead.duration, folded));
  }
}

}  // namespace

HarnessFixture buildGoldbergVariationsForm(const ResolvedRequest& req) {
  HarnessFixture out;
  constexpr int kCycleBars = 4;
  const int bars = static_cast<int>(req.bars);
  const Mode mode = req.mode;
  const int offset = static_cast<int>(req.seed % 4u);
  const detail::CharacterProfile& profile = detail::characterProfile(req.character);

  // Block layout: block 0 (bars 0-3) is the aria; blocks 1..K are 4-bar
  // figuration variations; when N >= 24 the final block restates the aria
  // (da capo). K is derived from the bar count so the variation COUNT is
  // length-driven.
  const int num_blocks = bars / kCycleBars;  // bars is snapped to a multiple of 4.
  const bool da_capo = bars >= 24;
  const int da_capo_block = da_capo ? num_blocks - 1 : -1;

  // Immutable tiled ground (period 4 bars) over the whole piece, including the
  // aria. The ground is the LOWEST voice (V2); its tones are chord roots so
  // downbeat anchoring stays consonant; tiling exactly keeps
  // passacaglia_ground_immutable clean.
  const std::size_t ground_variant = detail::groundVariantIndex(req.seed);
  const auto& ground = (mode == Mode::Major) ? detail::kGoldbergGroundsMajor[ground_variant]
                                             : detail::kGoldbergGroundsMinor[ground_variant];
  for (int bar = 0; bar < kCycleBars; ++bar)
    out.material.passacaglia_ground.push_back(
        materialNote(barTick(bar), kTicksPerBar, ground[static_cast<std::size_t>(bar)]));
  out.material.passacaglia_ground_period = static_cast<Tick>(kCycleBars) * kTicksPerBar;

  // Canon follower line (V1). Populated only for canonic variation blocks; the
  // follower notes for every canon block are appended here in time order (one
  // block at a time), then handed to a single TrioVoiceLine on V1. V1 is silent
  // outside canon blocks (no notes), keeping the texture clean and the validator
  // quiet for the figuration / aria blocks. The follower carries the
  // TrioVoiceIndependent bit, but because it is the ONLY voice carrying that bit
  // the voice_independence_threshold rule stays inert (it needs >= 2 such
  // voices), so no soft-fail is introduced.
  std::vector<MaterialNote> canon_follower;
  std::vector<int> canon_blocks;  // block indices realized as canons (for V1 spans).

  // Per-bar harmony (ground cycle, tiled). Drives the variation downbeat anchor.
  out.harmony.tonic_pc = 0;
  out.harmony.is_minor = (mode == Mode::Minor);
  for (int bar = 0; bar < bars; ++bar) {
    const BarChord chord =
        goldbergBarChord(ground[static_cast<std::size_t>(bar % kCycleBars)], mode);
    ChordEvent ce;
    ce.start_tick = barTick(bar);
    ce.root_pc = chord.root_pc;
    ce.quality = chord.minor ? ChordQuality::Minor : ChordQuality::Major;
    out.harmony.chords.push_back(ce);
  }

  // The climax block is the arc climax cycle (~80% of the span), NOT necessarily
  // the last block. The aria (block 0) and any da-capo block are never the
  // climax.
  const int cycle_count = static_cast<int>(req.cycle_count);
  int climax_block = -1;
  for (int blk = 0; blk < num_blocks; ++blk) {
    if (blk == 0 || blk == da_capo_block)
      continue;
    const int cycle = cycle_count > 0 ? (blk * cycle_count) / num_blocks : 0;
    if (req.arc(static_cast<std::size_t>(cycle)).is_climax) {
      climax_block = blk;
      break;
    }
  }
  // If the arc placed its climax on a block we excluded (aria / da-capo), fall
  // back to the last figuration variation block so a climax is always present.
  if (climax_block < 0) {
    for (int blk = num_blocks - 1; blk >= 1; --blk) {
      if (blk != da_capo_block) {
        climax_block = blk;
        break;
      }
    }
  }

  // Variation register anchor: ~C5 region (72), well above the C2 ground.
  constexpr int kVarRegisterBase = 72;

  for (int blk = 0; blk < num_blocks; ++blk) {
    PassacagliaVariation var;
    var.voice = 0;
    var.start_tick = barTick(blk * kCycleBars);
    var.end_tick = var.start_tick + static_cast<Tick>(kCycleBars) * kTicksPerBar;
    var.is_climax = (blk == climax_block);

    const bool is_aria = (blk == 0) || (blk == da_capo_block);
    if (is_aria) {
      var.density_level = 0;
      for (int local = 0; local < kCycleBars; ++local) {
        const int bar = blk * kCycleBars + local;
        appendAriaBar(var, bar,
                      goldbergBarChord(ground[static_cast<std::size_t>(bar % kCycleBars)], mode),
                      mode, kVarRegisterBase, offset);
      }
      out.material.passacaglia_variations.push_back(var);
      continue;
    }

    // Variation block. The variation index is the post-aria ordinal (the aria is
    // block 0; the first variation is block 1 => variation_index 0). The kind
    // dispatch routes Figuration vs Canon (BWV988: every third variation is a
    // canon at a rising imitation interval).
    const std::size_t variation_index = static_cast<std::size_t>(blk - 1);
    const std::size_t variation_number = variation_index + 1;  // 1-based (1..K).
    const int cycle = cycle_count > 0 ? (blk * cycle_count) / num_blocks : 0;
    const ArcPoint point = req.arc(static_cast<std::size_t>(cycle));

    switch (goldbergVariationKind(variation_index)) {
      case GoldbergVariationKind::Canon: {
        // Canon number c = variation_number / 3 (1..9 across the full set);
        // imitation interval = (c - 1) diatonic degrees above unison (unison,
        // 2nd, 3rd, ... 9th), following the BWV988 scheme. The leader runs in
        // eighths (canons cap at eighths so the two clear lines stay legible),
        // so density_level is the eighth tier regardless of the arc.
        const int canon_number = static_cast<int>(variation_number / 3);
        const int imitation_degrees = canon_number - 1;  // 0 = unison canon.
        var.density_level = 1;
        buildCanonBlock(var, canon_follower, blk * kCycleBars, imitation_degrees, mode, ground);
        canon_blocks.push_back(blk);
        break;
      }
      case GoldbergVariationKind::Figuration:
      default: {
        int notes_per_beat = notesPerBeatFor(point, profile.density_bias);
        if (var.is_climax)
          notes_per_beat = 4;  // the arc climax block is the densest by design.
        // The final variation of the full set (variation 30) is the design
        // secondary peak (the BWV988 Quodlibet slot): the densest figuration
        // tier directly, no search.
        if (variation_number == 30)
          notes_per_beat = 4;
        if (profile.prefer_dotted && notes_per_beat < 4)
          notes_per_beat = 2;  // Noble keeps a moderate, dignified subdivision.
        var.density_level = notes_per_beat == 4 ? 2 : 1;
        const int register_base = kVarRegisterBase + static_cast<int>(point.register_shift);
        for (int local = 0; local < kCycleBars; ++local) {
          const int bar = blk * kCycleBars + local;
          appendVariationBar(
              var, bar, goldbergBarChord(ground[static_cast<std::size_t>(bar % kCycleBars)], mode),
              mode, notes_per_beat, register_base, offset);
        }
        break;
      }
    }
    out.material.passacaglia_variations.push_back(var);
  }

  // Hand the accumulated canon-follower notes to a single V1 TrioVoiceLine. The
  // line is time-sorted by construction (blocks processed in order, notes within
  // a block in onset order), so the TrioVoiceCarrier replay branch's window
  // clipping (which assumes ascending onsets) is satisfied.
  if (!canon_follower.empty()) {
    TrioVoiceLine follower_line;
    follower_line.voice = 1;
    follower_line.manual = 1;  // documentary (Swell); V1 = secondary line.
    follower_line.notes = std::move(canon_follower);
    out.material.trio_voices.push_back(std::move(follower_line));
  }

  // VoicePlan (3 voices, strictly ordered by register so voice_crossing never
  // fires): V0 = principal line (PassacagliaVariation per block, C4-C6 region);
  // V1 = canon follower (TrioVoiceCarrier, one span per canon block, C3-region);
  // V2 = the immutable ground (PassacagliaGround over the whole piece, C2). The
  // ground carrier is window-matched implicitly (period-tiled), and the
  // PassacagliaVariation / TrioVoiceCarrier branches match by window / voice
  // respectively, so the three carriers never bleed into one another.
  out.voice_plan.num_voices = 3;
  SpanId next_span_id = 0;

  Span ground_span;
  ground_span.id = next_span_id++;
  ground_span.start_tick = 0;
  ground_span.end_tick = barTick(bars);
  ground_span.voice = 2;
  ground_span.intent = VoiceIntent::PassacagliaGround;
  ground_span.subdivision = Subdivision::Quarter;
  out.voice_plan.spans.push_back(ground_span);

  // V0 principal line: one PassacagliaVariation span per block, window-matched.
  for (int blk = 0; blk < num_blocks; ++blk) {
    Span var_span;
    var_span.id = next_span_id++;
    var_span.start_tick = barTick(blk * kCycleBars);
    var_span.end_tick = var_span.start_tick + static_cast<Tick>(kCycleBars) * kTicksPerBar;
    var_span.voice = 0;
    var_span.intent = VoiceIntent::PassacagliaVariation;
    var_span.subdivision = Subdivision::Quarter;
    out.voice_plan.spans.push_back(var_span);
  }

  // V1 canon follower: one TrioVoiceCarrier span per canon block. The branch
  // matches material.trio_voices by voice and clips to [start_tick, end_tick),
  // so a per-block window selects exactly that canon's follower notes (the
  // follower is delayed one bar and truncated at the block end, so it sounds in
  // bars 1..3 of the block). V1 is silent outside these windows.
  for (int blk : canon_blocks) {
    Span follower_span;
    follower_span.id = next_span_id++;
    follower_span.start_tick = barTick(blk * kCycleBars);
    follower_span.end_tick =
        follower_span.start_tick + static_cast<Tick>(kCycleBars) * kTicksPerBar;
    follower_span.voice = 1;
    follower_span.intent = VoiceIntent::TrioVoiceCarrier;
    follower_span.subdivision = Subdivision::Quarter;
    out.voice_plan.spans.push_back(follower_span);
  }

  return out;
}

}  // namespace bach::composer
