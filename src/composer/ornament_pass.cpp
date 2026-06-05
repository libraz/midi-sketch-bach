#include "composer/ornament_pass.h"

#include <algorithm>
#include <cstdint>
#include <vector>

#include "composer/character_profile.h"
#include "composer/minor_material.h"
#include "composer/renderer.h"

namespace bach::composer {

namespace {

using detail::inScale;
using detail::Mode;

constexpr Tick kQuarter = duration::kQuarterNote;  // 480
constexpr Tick kHalf = duration::kHalfNote;        // 960
constexpr int kMidiMin = 0;
constexpr int kMidiMax = 127;

// Deterministic placement hash keyed by (seed, bar, voice). Pure: no RNG.
// SplitMix64-style mix so adjacent (bar, voice) keys do not correlate.
std::uint64_t placementHash(std::uint32_t seed, int bar, VoiceId voice) {
  std::uint64_t x = (static_cast<std::uint64_t>(seed) << 32) ^
                    (static_cast<std::uint64_t>(static_cast<std::uint32_t>(bar)) << 8) ^
                    static_cast<std::uint64_t>(voice);
  x += 0x9E3779B97F4A7C15ull;
  x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
  x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
  return x ^ (x >> 31);
}

// Diatonic upper neighbour of `pitch` in `mode`. Walks up by semitone until a
// scale member is reached. Returns -1 when no in-range neighbour exists.
int upperNeighbour(int pitch, Mode mode) {
  for (int add = 1; add <= 2; ++add) {
    const int cand = pitch + add;
    if (cand > kMidiMax)
      return -1;
    if (inScale(cand, mode))
      return cand;
  }
  return -1;
}

// Diatonic lower neighbour of `pitch` in `mode`. In minor at a cadence the
// degree just below the tonic must be the raised leading tone (B natural below
// C), not the natural-minor subtonic (Bb), so a descending neighbour reaching
// the tonic does not undershoot to a whole step where a half step is wanted.
// `cadence_context` selects that raised-leading-tone neighbour when the main
// tone is a tonic-class pitch in minor mode. Returns -1 when no in-range
// neighbour exists.
int lowerNeighbour(int pitch, Mode mode, bool cadence_context) {
  if (mode == Mode::Minor && cadence_context && ((pitch % 12) == 0)) {
    // Leading tone B natural sits one semitone below the C tonic.
    const int cand = pitch - 1;
    return cand >= kMidiMin ? cand : -1;
  }
  for (int sub = 1; sub <= 2; ++sub) {
    const int cand = pitch - sub;
    if (cand < kMidiMin)
      return -1;
    if (inScale(cand, mode))
      return cand;
  }
  return -1;
}

// A ready-to-emit ornament expansion: the sub-notes that replace one candidate
// over its original [start, start+dur) span. Empty when the note could not be
// ornamented (caller then keeps the original note untouched).
struct Expansion {
  std::vector<NoteEvent> notes;
};

// Build a trill expansion over [base.start, base.start+base.duration). The
// figure alternates main/upper (32nd/16th legacy pacing), then ends with a
// lower-neighbour Nachschlag pair before the final main tone. The sub-note
// durations sum exactly to base.duration (the final tone absorbs the
// remainder), so total time is preserved.
Expansion buildTrill(const NoteEvent& base, int upper, int lower, Tick sub) {
  Expansion exp;
  const Tick span_end = base.start_tick + base.duration;

  // Number of whole sub-notes that fit; reserve the last two slots for the
  // Nachschlag (lower neighbour then main).
  std::vector<NoteEvent> figure;
  Tick cursor = base.start_tick;
  while (cursor + sub <= span_end) {
    NoteEvent note = base;
    note.duration = sub;
    note.start_tick = cursor;
    note.source = BachNoteSource::Ornament;
    figure.push_back(note);
    cursor += sub;
  }
  // Need at least a main + upper + Nachschlag-lower + main to be a trill.
  if (figure.size() < 4)
    return exp;  // caller keeps the original note.

  for (std::size_t idx = 0; idx < figure.size(); ++idx)
    figure[idx].pitch = static_cast<std::uint8_t>((idx % 2 == 0) ? base.pitch : upper);

  // Nachschlag: penultimate slot becomes the lower neighbour, last slot the
  // resolved main tone.
  figure[figure.size() - 2].pitch = static_cast<std::uint8_t>(lower);
  figure[figure.size() - 1].pitch = base.pitch;

  // Absorb any rounding remainder into the final tone so the span is covered
  // exactly without changing total duration.
  figure.back().duration = span_end - figure.back().start_tick;

  exp.notes = std::move(figure);
  return exp;
}

// Build a mordent expansion: main (short) -> lower neighbour (short) -> main
// (remainder). Sub-note durations sum exactly to base.duration.
Expansion buildMordent(const NoteEvent& base, int lower) {
  Expansion exp;
  const Tick span_end = base.start_tick + base.duration;
  const Tick short_dur = base.duration / 8;
  if (short_dur == 0 || 2 * short_dur >= base.duration)
    return exp;  // too short to subdivide meaningfully.

  NoteEvent first = base;
  first.duration = short_dur;
  first.source = BachNoteSource::Ornament;
  first.pitch = base.pitch;

  NoteEvent second = base;
  second.start_tick = base.start_tick + short_dur;
  second.duration = short_dur;
  second.source = BachNoteSource::Ornament;
  second.pitch = static_cast<std::uint8_t>(lower);

  NoteEvent third = base;
  third.start_tick = base.start_tick + 2 * short_dur;
  third.duration = span_end - third.start_tick;
  third.source = BachNoteSource::Ornament;
  third.pitch = base.pitch;

  exp.notes = {first, second, third};
  return exp;
}

// Lowest concurrent pitch among notes that sound at `tick` (start <= tick <
// start+dur). Returns INT_MAX-equivalent sentinel (256) when nothing sounds.
int lowestSoundingPitch(const std::vector<NoteEvent>& notes, Tick tick) {
  int lowest = 256;
  for (const auto& note : notes) {
    if (note.start_tick <= tick && tick < note.start_tick + note.duration) {
      if (note.pitch < lowest)
        lowest = note.pitch;
    }
  }
  return lowest;
}

// Lowest pitch among the higher-than-`ref_voice` voices that sound at `tick`.
// Used as a ceiling: an ornament tone must not cross above the next-higher
// voice's concurrent pitch. Returns 256 when no higher voice sounds.
int nextHigherVoiceCeiling(const std::vector<NoteEvent>& notes, VoiceId ref_voice,
                           std::uint8_t ref_pitch, Tick tick) {
  int ceiling = 256;
  for (const auto& note : notes) {
    if (note.voice == ref_voice)
      continue;
    if (note.start_tick <= tick && tick < note.start_tick + note.duration) {
      // A higher voice is one whose concurrent pitch sits above the candidate.
      if (note.pitch > ref_pitch && note.pitch < ceiling)
        ceiling = note.pitch;
    }
  }
  return ceiling;
}

bool isExempt(const std::vector<VoiceId>& exempt, VoiceId voice) {
  return std::find(exempt.begin(), exempt.end(), voice) != exempt.end();
}

// Latest tick reached by any note (start + duration), i.e. the piece length.
Tick longestEnd(const std::vector<NoteEvent>& notes) {
  Tick last = 0;
  for (const auto& note : notes) {
    const Tick end = note.start_tick + note.duration;
    if (end > last)
      last = end;
  }
  return last;
}

}  // namespace

std::uint8_t effectiveOrnamentDensity(SubjectCharacter character, InstrumentType instrument) {
  int density = detail::characterProfile(character).ornament_density;
  switch (instrument) {
    case InstrumentType::Harpsichord:
      density += 1;
      break;
    case InstrumentType::Violin:
    case InstrumentType::Cello:
    case InstrumentType::Guitar:
      density -= 1;
      break;
    case InstrumentType::Organ:
    case InstrumentType::Piano:
      break;
  }
  if (density < 0)
    density = 0;
  if (density > 2)
    density = 2;
  return static_cast<std::uint8_t>(density);
}

void applyOrnamentPass(ComposeResult& result, const OrnamentParams& params) {
  if (result.notes.empty())
    return;
  if (result.notes.size() != result.provenance.size())
    return;  // index-parallel invariant broken upstream; refuse to corrupt it.

  const Tick tpb = params.ticks_per_bar > 0 ? params.ticks_per_bar : kTicksPerBar;
  const std::uint8_t density = effectiveOrnamentDensity(params.character, params.instrument);
  const int total_bars = static_cast<int>((longestEnd(result.notes) + tpb - 1) / tpb);

  // The cadence window is the last two bars; the penultimate strong beat
  // before the final cadence is where the priority cadence trill lands.
  const int cadence_window_start_bar = total_bars >= 2 ? total_bars - 2 : 0;

  // 32nd-note trill pacing (legacy: fast notes >= quarter); mordent uses a
  // short fraction of the note (handled in buildMordent).
  const Tick trill_sub = duration::kThirtySecondNote;  // 60

  // Build replacement views WITHOUT mutating the live note list until the full
  // plan is assembled (build-then-swap).
  std::vector<NoteEvent> out_notes;
  std::vector<NoteProvenance> out_prov;
  out_notes.reserve(result.notes.size() * 2);
  out_prov.reserve(result.provenance.size() * 2);

  for (std::size_t idx = 0; idx < result.notes.size(); ++idx) {
    const NoteEvent& note = result.notes[idx];
    const NoteProvenance& prov = result.provenance[idx];

    Expansion exp;
    const bool already_ornament = note.source == BachNoteSource::Ornament ||
                                  prov.source == NoteSource::Ornament;

    if (!already_ornament && note.duration >= kQuarter && !isExempt(params.exempt_voices, note.voice)) {
      const int bar = static_cast<int>(note.start_tick / tpb);
      const Tick pos_in_bar = note.start_tick % tpb;
      const bool is_downbeat = pos_in_bar == 0;
      const bool in_cadence_window = bar >= cadence_window_start_bar;
      // Strong beats fall on the half-bar grid (beat 1 and the mid-bar beat).
      // The penultimate strong beat before the final cadence is the priority
      // cadence-trill site; admitting every strong beat inside the last two
      // bars keeps the rule robust to 3/4 vs 4/4 meter.
      const Tick half_bar = tpb / 2 > 0 ? tpb / 2 : kQuarter;
      const bool is_strong_beat = (pos_in_bar % half_bar) == 0;

      // Clean-bass guard: never ornament the lowest-sounding voice.
      const int lowest = lowestSoundingPitch(result.notes, note.start_tick);
      const bool is_bass = note.pitch <= lowest;

      // Neighbour availability.
      const int upper = upperNeighbour(note.pitch, params.mode);
      const int lower = lowerNeighbour(note.pitch, params.mode, in_cadence_window);

      const bool neighbours_ok = upper >= 0 && lower >= 0;

      // Ceiling guard: the upper neighbour must not cross above the next-higher
      // voice's concurrent pitch.
      const int ceiling = nextHigherVoiceCeiling(result.notes, note.voice, note.pitch,
                                                 note.start_tick);
      const bool upper_clears_ceiling = upper >= 0 && upper < ceiling;

      if (!is_bass && neighbours_ok && upper_clears_ceiling) {
        const std::uint64_t roll = placementHash(params.seed, bar, note.voice);

        // Decision priority. Cadence trills first (always, every density).
        bool want_trill = false;
        bool want_mordent = false;

        if (in_cadence_window && is_strong_beat) {
          // Priority cadence trill: the strong beat in the last two bars.
          want_trill = true;
        } else if (density >= 1 && is_downbeat && (bar % 4 == 0)) {
          // Downbeat mordent every 4 bars (quarter notes only).
          if (note.duration == kQuarter)
            want_mordent = true;
          else
            want_trill = false;  // not a mordent target; leave for inner-trill rule.
        }

        if (!want_trill && !want_mordent && density >= 2 && note.duration >= kHalf &&
            (bar % 2 == 0)) {
          // Inner trills on long notes every 2 bars.
          want_trill = true;
        }

        // A small deterministic gate so not literally every eligible note in a
        // dense passage is ornamented (keeps the texture musical). Cadence
        // trills bypass the gate (they are mandatory).
        const bool mandatory = in_cadence_window && is_strong_beat;
        const bool gate_open = mandatory || (roll & 1ull) == 0ull;

        if (gate_open) {
          if (want_mordent)
            exp = buildMordent(note, lower);
          else if (want_trill)
            exp = buildTrill(note, upper, lower, trill_sub);
        }
      }
    }

    if (exp.notes.empty()) {
      out_notes.push_back(note);
      out_prov.push_back(prov);
      continue;
    }

    // Expand: each ornament sub-note inherits the original note's provenance
    // (span_id, voice_intent) but is re-sourced as Ornament so the post-pass
    // provenance is honest and the validator's Compose-only strong-beat rule
    // skips these mode-constrained decoration tones.
    for (const auto& sub : exp.notes) {
      out_notes.push_back(sub);
      NoteProvenance sub_prov = prov;
      sub_prov.source = NoteSource::Ornament;
      out_prov.push_back(sub_prov);
    }
  }

  result.notes = std::move(out_notes);
  result.provenance = std::move(out_prov);

  // Re-render tracks from the ornamented note list so the per-voice copies
  // mirror result.notes exactly (the Renderer groups by voice and clamps
  // same-voice overlap; it never moves pitch or onset).
  result.tracks = Renderer{}.render(result.notes);
}

}  // namespace bach::composer
