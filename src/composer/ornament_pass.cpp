#include "composer/ornament_pass.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>

#include "composer/character_profile.h"
#include "composer/renderer.h"
#include "composer/texture_helpers.h"
#include "composer/validator.h"

namespace bach::composer {

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

// Whether a long cadence trill on (seed, bar, voice) opens with the
// von-unten doppelt-cadence (its lower turn note) rather than the appuy
// (upper-neighbour) opening. Pure; the single source of that choice.
bool cadenceTrillOpensVonUnten(std::uint32_t seed, int bar, VoiceId voice) {
  return ((placementHash(seed, bar, voice) >> 1) & 1ull) != 0ull;
}

namespace {

using detail::inScale;
using detail::Mode;

constexpr Tick kQuarter = duration::kQuarterNote;  // 480
constexpr Tick kHalf = duration::kHalfNote;        // 960
constexpr Tick kEighth = duration::kEighthNote;    // 240
constexpr int kMidiMin = 0;
constexpr int kMidiMax = 127;

// Diatonic upper neighbour of `pitch` in `mode`. Walks up by semitone until a
// scale member is reached. Returns -1 when no in-range neighbour exists.
//
// Minor-mode auxiliaries need no melodic-minor raising here: the cadential
// trill sites are the leading tone (B natural, upper auxiliary C = tonic, a
// half step) and the supertonic (D, upper auxiliary Eb = the diatonic third, a
// half-step trill that is the idiomatic minor-cadence figure). The natural
// minor membrane already yields both.
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
  if (mode == Mode::Minor && cadence_context) {
    const int pc = pitch % 12;
    if (pc == 0) {
      // Leading tone B natural sits one semitone below the C tonic.
      const int cand = pitch - 1;
      return cand >= kMidiMin ? cand : -1;
    }
    if (pc == 11) {
      // Below the raised leading tone the melodic-minor sixth degree (A
      // natural) is wanted, not the natural-minor Bb which would put a
      // chromatic Bb-B step inside the Nachschlag.
      const int cand = pitch - 2;
      return cand >= kMidiMin ? cand : -1;
    }
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

// How a trill opens. Baroque trills start on the upper auxiliary; the long
// (>= half note) forms additionally take one of the Explication's compound
// openings before the alternation.
enum class TrillOnset : std::uint8_t {
  UpperStart,  // plain short trill: upper/main alternation from the upper tone.
  Appuy,       // held upper appoggiatura (the 4-3 suspension over V), then alternation.
  VonUnten,    // doppelt-cadence: two-note prefix from below (lower -> main), then alternation.
};

// Build a trill expansion over [base.start, base.start+base.duration). The
// alternation starts on the upper auxiliary (Baroque standard; at a cadence
// the upper tone is the suspension over the dominant) and ends with a
// lower-neighbour Nachschlag pair before the final main tone. Appuy opens with
// a single held upper-neighbour note (max(span/4, eighth), capped at a half
// note); VonUnten opens with a lower -> main two-note prefix. The sub-note
// durations sum exactly to base.duration (the final tone absorbs the
// remainder), so total time is preserved.
Expansion buildTrill(const NoteEvent& base, int upper, int lower, Tick sub, TrillOnset onset) {
  Expansion exp;
  const Tick span_end = base.start_tick + base.duration;

  std::vector<NoteEvent> figure;
  Tick cursor = base.start_tick;
  const auto push = [&](int pitch, Tick dur) {
    NoteEvent note = base;
    note.start_tick = cursor;
    note.duration = dur;
    note.pitch = static_cast<std::uint8_t>(pitch);
    note.source = BachNoteSource::Ornament;
    figure.push_back(note);
    cursor += dur;
  };

  // Opening prefix.
  bool next_is_upper = true;  // the alternation proper starts on the upper tone.
  if (onset == TrillOnset::Appuy) {
    Tick appuy = std::max(base.duration / 4, kEighth);
    if (appuy > kHalf)
      appuy = kHalf;
    push(upper, appuy);
    next_is_upper = false;  // the held upper tone already sounded; continue on main.
  } else if (onset == TrillOnset::VonUnten) {
    push(lower, sub);
    push(base.pitch, sub);
  }
  const std::size_t prefix = figure.size();

  // Alternation slots; the last two are re-pitched into the Nachschlag below.
  while (cursor + sub <= span_end) {
    push(next_is_upper ? upper : base.pitch, sub);
    next_is_upper = !next_is_upper;
  }
  // Need at least upper + main + Nachschlag-lower + main to be a trill.
  if (figure.size() - prefix < 4)
    return exp;  // caller keeps the original note.

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

// Build an appoggiatura expansion: the accented neighbour leans on the beat
// for half the note's value (two thirds when the value is dotted -- the
// Explication's dotted-note rule), then resolves into the main tone for the
// remainder. The caller supplies the neighbour; the descending resolution
// from the upper neighbour (accent fallend) is the default form.
Expansion buildAppoggiatura(const NoteEvent& base, int neighbour) {
  Expansion exp;
  constexpr Tick kDottedEighth = duration::kEighthNote * 3 / 2;
  const bool dotted = (base.duration % kDottedEighth) == 0;
  const Tick lean = dotted ? base.duration * 2 / 3 : base.duration / 2;
  if (lean == 0 || lean >= base.duration)
    return exp;

  NoteEvent first = base;
  first.duration = lean;
  first.pitch = static_cast<std::uint8_t>(neighbour);
  first.source = BachNoteSource::Ornament;

  NoteEvent second = base;
  second.start_tick = base.start_tick + lean;
  second.duration = base.duration - lean;
  second.source = BachNoteSource::Ornament;

  exp.notes = {first, second};
  return exp;
}

// Build a turn expansion (gruppetto): upper - main - lower in 32nds, then the
// main tone holds the remainder of the span. Total duration is preserved.
Expansion buildTurn(const NoteEvent& base, int upper, int lower) {
  Expansion exp;
  const Tick sub = duration::kThirtySecondNote;
  if (base.duration < 4 * sub)
    return exp;  // the held tail must remain the longest tone.

  Tick cursor = base.start_tick;
  const auto push = [&](int pitch, Tick dur) {
    NoteEvent note = base;
    note.start_tick = cursor;
    note.duration = dur;
    note.pitch = static_cast<std::uint8_t>(pitch);
    note.source = BachNoteSource::Ornament;
    exp.notes.push_back(note);
    cursor += dur;
  };
  push(upper, sub);
  push(base.pitch, sub);
  push(lower, sub);
  push(base.pitch, base.duration - 3 * sub);
  return exp;
}

// Build a slide expansion (Schleifer): two rising 32nds from the diatonic
// third below (two steps below, then one step below), then the held main
// tone -- the figure fills the gap a rising leap left open underneath the
// arrival tone. Total duration is preserved.
Expansion buildSlide(const NoteEvent& base, Mode mode) {
  Expansion exp;
  const Tick sub = duration::kThirtySecondNote;
  if (base.duration < 4 * sub)
    return exp;
  const int below1 = lowerNeighbour(base.pitch, mode, /*cadence_context=*/false);
  const int below2 = below1 >= 0 ? lowerNeighbour(below1, mode, /*cadence_context=*/false) : -1;
  if (below2 < 0)
    return exp;

  Tick cursor = base.start_tick;
  const auto push = [&](int pitch, Tick dur) {
    NoteEvent note = base;
    note.start_tick = cursor;
    note.duration = dur;
    note.pitch = static_cast<std::uint8_t>(pitch);
    note.source = BachNoteSource::Ornament;
    exp.notes.push_back(note);
    cursor += dur;
  };
  push(below2, sub);
  push(below1, sub);
  push(base.pitch, base.duration - 2 * sub);
  return exp;
}

// Build a mordent expansion: main (short) -> lower neighbour (short) -> main
// (remainder). Sub-note durations sum exactly to base.duration. The short
// tones never go below a 32nd (60 ticks): an eighth-note mordent is played
// 32nd-32nd-16th, not with 64th flickers.
Expansion buildMordent(const NoteEvent& base, int lower) {
  Expansion exp;
  const Tick span_end = base.start_tick + base.duration;
  const Tick short_dur = std::max(base.duration / 8, duration::kThirtySecondNote);
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

// Highest pitch sounding at `tick` in any voice other than `voice`. Returns -1
// when no other voice sounds. Used to keep the cadence ornament on the TOP
// line only: two voices trilling the final cadence simultaneously clash at the
// alternation level.
int highestOtherSoundingPitch(const std::vector<NoteEvent>& notes, VoiceId voice, Tick tick) {
  int highest = -1;
  for (const auto& note : notes) {
    if (note.voice == voice)
      continue;
    if (note.start_tick <= tick && tick < note.start_tick + note.duration) {
      if (note.pitch > highest)
        highest = note.pitch;
    }
  }
  return highest;
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
  const int ornament_pitch_ceiling = (params.instrument == InstrumentType::Organ) ? 84 : kMidiMax;
  const int total_bars = static_cast<int>((longestEnd(result.notes) + tpb - 1) / tpb);

  // The cadence window is the last two bars; the penultimate strong beat
  // before the final cadence is where the priority cadence trill lands.
  const int cadence_window_start_bar = total_bars >= 2 ? total_bars - 2 : 0;

  // Interior section-cadence bars: each entry of section_cadence_ticks names
  // the bar that closes an inner section (a fugue exposition, a toccata's
  // free section). These bars behave like the final cadence -- a mandatory
  // top-line-only strong-beat trill site -- at lower intensity (short trill).
  // Bars at or past the final cadence window are dropped: the final window's
  // long cadential trill wins.
  std::vector<int> section_cadence_bars;
  section_cadence_bars.reserve(params.section_cadence_ticks.size());
  for (const Tick tick : params.section_cadence_ticks) {
    const int bar = static_cast<int>(tick / tpb);
    if (bar < cadence_window_start_bar) {
      section_cadence_bars.push_back(bar);
    }
  }
  std::sort(section_cadence_bars.begin(), section_cadence_bars.end());

  // The designed mid-piece sub-cadence: the 4-bar phrase boundary nearest the
  // piece midpoint. Like the final cadence it bypasses the placement gate (a
  // design value, not a probabilistic site), so every character -- including
  // density 0 -- decorates at least one mid-piece phrase boundary whenever an
  // eligible note sounds there.
  const int mid_boundary_bar = total_bars >= 8 ? ((total_bars / 2) / 4) * 4 - 1 : -1;

  // Trill pacing follows tempo: 32nds at a moderate tempo, 16ths above 100 bpm
  // so the alternation never exceeds a playable rate. Mordents use a short
  // fraction of the note (handled in buildMordent).
  const Tick trill_sub = params.bpm <= 100 ? duration::kThirtySecondNote : duration::kSixteenthNote;

  // Final-note protection: the last attack of every voice is the resolution
  // tone and must sound plain, so it is never an ornament candidate.
  std::array<Tick, 256> last_onset{};
  for (const auto& note : result.notes) {
    if (note.start_tick > last_onset[note.voice])
      last_onset[note.voice] = note.start_tick;
  }

  // A single-voice piece (the solo cello line) has no bass to keep clean: the
  // clean-bass guard below only applies when at least two voices sound.
  bool multi_voice = false;
  for (const auto& note : result.notes) {
    if (note.voice != result.notes.front().voice) {
      multi_voice = true;
      break;
    }
  }

  // Per-note previous attack in the same voice (pitch and duration), resolved
  // in onset order. The melodic approach selects the ornament site: a falling
  // third leaves a gap the appoggiatura fills, a stepwise descent carries the
  // port-de-voix repetition, a rising leap invites the slide underneath the
  // arrival, and a tone longer than its predecessor stands isolated (turn).
  std::vector<int> prev_pitch_in_voice(result.notes.size(), -1);
  std::vector<Tick> prev_dur_in_voice(result.notes.size(), 0);
  {
    std::vector<std::size_t> order(result.notes.size());
    for (std::size_t i = 0; i < order.size(); ++i)
      order[i] = i;
    std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
      return result.notes[a].start_tick < result.notes[b].start_tick;
    });
    std::array<int, 256> last_in_voice{};
    last_in_voice.fill(-1);
    for (const std::size_t k : order) {
      const VoiceId v = result.notes[k].voice;
      if (last_in_voice[v] >= 0) {
        const std::size_t prev_idx = static_cast<std::size_t>(last_in_voice[v]);
        const NoteEvent& prev = result.notes[prev_idx];
        // An ornament sub-note is not a structural approach tone: leaving the
        // context unset closes the approach sites behind expanded notes, which
        // keeps a second application of the pass a strict no-op.
        const bool prev_is_ornament = prev.source == BachNoteSource::Ornament ||
                                      result.provenance[prev_idx].source == NoteSource::Ornament;
        if (!prev_is_ornament) {
          prev_pitch_in_voice[k] = static_cast<int>(prev.pitch);
          prev_dur_in_voice[k] = prev.duration;
        }
      }
      last_in_voice[v] = static_cast<int>(k);
    }
  }

  // Build replacement views WITHOUT mutating the live note list until the full
  // plan is assembled (build-then-swap).
  std::vector<NoteEvent> out_notes;
  std::vector<NoteProvenance> out_prov;
  out_notes.reserve(result.notes.size() * 2);
  out_prov.reserve(result.provenance.size() * 2);

  // Ornament windows already committed in this pass: (voice, start, end, base
  // pitch). Two voices alternating ornaments at the SAME time with their base
  // tones a perfect interval apart chain parallel octaves/fifths at the
  // alternation grain (each upper-neighbour strike moves both voices the same
  // direction into the same interval class) -- the dense-ornament characters
  // measured up to four such chains per piece. A candidate whose window
  // overlaps an already-committed ornament in another voice at ic 0/7 stays a
  // plain tone instead; any other interval keeps its ornament (the moving
  // seconds/thirds are ordinary heterophony, not parallels).
  struct OrnamentWindow {
    VoiceId voice;
    Tick start;
    Tick end;
    int base_pitch;
  };
  std::vector<OrnamentWindow> committed_ornaments;
  auto clashes_committed_ornament = [&](const NoteEvent& cand) {
    for (const OrnamentWindow& win : committed_ornaments) {
      if (win.voice == cand.voice)
        continue;
      if (cand.start_tick >= win.end || win.start >= cand.start_tick + cand.duration)
        continue;
      const int ic = std::abs(static_cast<int>(cand.pitch) - win.base_pitch) % 12;
      if (ic == 0 || ic == 7)
        return true;
    }
    return false;
  };

  // Voices present, for the motion-level parallel guard below.
  std::array<bool, 16> voice_present{};
  for (const NoteEvent& n : result.notes) {
    if (n.voice < 16) {
      voice_present[n.voice] = true;
    }
  }

  // Sounding pitch of voice `v` at `tick` in the final picture: the notes
  // already emitted this pass (including committed ornament sub-notes) plus
  // the not-yet-processed tail of the original list. Latest onset wins,
  // mirroring the union-onset sampling the validator uses.
  auto sounding_in_voice = [&](VoiceId v, Tick tick, std::size_t tail_begin) -> int {
    int best_pitch = -1;
    Tick best_start = 0;  // valid only while best_pitch >= 0 (Tick is unsigned).
    auto scan = [&](const NoteEvent& n) {
      if (n.voice != v) {
        return;
      }
      if (n.start_tick <= tick && tick < n.start_tick + n.duration &&
          (best_pitch < 0 || n.start_tick >= best_start)) {
        best_start = n.start_tick;
        best_pitch = static_cast<int>(n.pitch);
      }
    };
    for (const NoteEvent& n : out_notes) {
      scan(n);
    }
    for (std::size_t j = tail_begin; j < result.notes.size(); ++j) {
      scan(result.notes[j]);
    }
    return best_pitch;
  };

  // Motion-level parallel guard. The window check above only compares ornament
  // BASE pitches, but an ornament run can track another voice's moving line --
  // Material sixteenths, or another ornament's sub-notes -- in parallel
  // fifths/octaves transition by transition (the bases can sit at any
  // interval). Walk every transition the expansion would create: sub-note to
  // sub-note, plus the entry/exit transitions where the expansion changes the
  // arrival or departure tone. Suppress the ornament (stay plain) when any of
  // them forms a parallel or hidden perfect against any other voice.
  auto expansion_forms_parallel = [&](const Expansion& cand_exp, const NoteEvent& base,
                                      std::size_t idx) {
    struct Transition {
      int from;
      int to;
      Tick at;
    };
    std::vector<Transition> transitions;
    for (std::size_t s = 0; s + 1 < cand_exp.notes.size(); ++s) {
      transitions.push_back({static_cast<int>(cand_exp.notes[s].pitch),
                             static_cast<int>(cand_exp.notes[s + 1].pitch),
                             cand_exp.notes[s + 1].start_tick});
    }
    if (!cand_exp.notes.empty() &&
        cand_exp.notes.front().pitch != base.pitch) {  // changed arrival tone.
      const int own_prev = sounding_in_voice(base.voice, base.start_tick - 1, idx + 1);
      if (own_prev >= 0) {
        transitions.push_back(
            {own_prev, static_cast<int>(cand_exp.notes.front().pitch), base.start_tick});
      }
    }
    if (!cand_exp.notes.empty() &&
        cand_exp.notes.back().pitch != base.pitch) {  // changed departure tone.
      const NoteEvent* next_own = nullptr;
      for (std::size_t j = idx + 1; j < result.notes.size(); ++j) {
        const NoteEvent& n = result.notes[j];
        if (n.voice == base.voice && n.start_tick >= base.start_tick + base.duration &&
            (next_own == nullptr || n.start_tick < next_own->start_tick)) {
          next_own = &n;
        }
      }
      if (next_own != nullptr) {
        transitions.push_back({static_cast<int>(cand_exp.notes.back().pitch),
                               static_cast<int>(next_own->pitch), next_own->start_tick});
      }
    }
    for (const Transition& tr : transitions) {
      if (tr.from == tr.to) {
        continue;
      }
      for (VoiceId v = 0; v < static_cast<VoiceId>(voice_present.size()); ++v) {
        if (!voice_present[v] || v == base.voice) {
          continue;
        }
        const int other_curr = sounding_in_voice(v, tr.at, idx + 1);
        if (other_curr < 0) {
          continue;
        }
        const int other_prev = sounding_in_voice(v, tr.at - 1, idx + 1);
        if (formsPerfectParallel(tr.from, tr.to, other_prev, other_curr)) {
          return true;
        }
      }
    }
    return false;
  };

  // Sustained-dissonance guard. An expansion may replace the arrival tone
  // with a leaning neighbour held for half the note's value (appoggiatura,
  // appuy trill opening). The base tone was validated against the texture,
  // but the neighbour was not -- held against another voice's sustained tone
  // it can sound a m2/M7/tritone for a beat or more, and the validator never
  // re-checks ornament output. Suppress the ornament when any pitch-altered
  // sub-note would hold a sharp interval class (1, 6 or 11) against any
  // sounding voice for a quarter note or longer; shorter decoration tones
  // (32nd/16th neighbours) pass untouched.
  auto expansion_sustains_dissonance = [&](const Expansion& cand_exp, const NoteEvent& base,
                                           std::size_t idx) {
    constexpr Tick kSlot = duration::kSixteenthNote;
    const int sustain_slots = static_cast<int>(kQuarter / kSlot);
    for (const NoteEvent& sub : cand_exp.notes) {
      if (sub.pitch == base.pitch || sub.duration < kQuarter) {
        continue;  // unchanged tone, or too short to sustain a clash.
      }
      for (VoiceId v = 0; v < static_cast<VoiceId>(voice_present.size()); ++v) {
        if (!voice_present[v] || v == base.voice) {
          continue;
        }
        int run = 0;
        for (Tick t = sub.start_tick; t < sub.start_tick + sub.duration; t += kSlot) {
          const int other = sounding_in_voice(v, t, idx + 1);
          bool sharp = false;
          if (other >= 0) {
            const int ic = std::abs(static_cast<int>(sub.pitch) - other) % 12;
            sharp = (ic == 1 || ic == 6 || ic == 11);
          }
          run = sharp ? run + 1 : 0;
          if (run >= sustain_slots) {
            return true;
          }
        }
      }
    }
    return false;
  };

  for (std::size_t idx = 0; idx < result.notes.size(); ++idx) {
    const NoteEvent& note = result.notes[idx];
    const NoteProvenance& prov = result.provenance[idx];

    Expansion exp;
    const bool already_ornament =
        note.source == BachNoteSource::Ornament || prov.source == NoteSource::Ornament;

    // Voice-level exemptions. Hard-exempt voices (ground carriers) never take
    // an ornament. Skeleton-exempt voices (cantus firmus) keep their bar-head
    // onsets immutable but may decorate within-bar tones -- EXCEPT under
    // Severe, whose plain-CF subtype keeps the whole line bare.
    const bool skeleton_voice = isExempt(params.skeleton_exempt_voices, note.voice);
    const bool skeleton_plain = skeleton_voice && params.character == SubjectCharacter::Severe;

    // Eighth notes are admitted as mordent candidates at the phrase-boundary
    // sites only (the per-rule conditions below re-narrow longer figures to
    // quarter+); sixteenths and shorter are never ornamented.
    if (!already_ornament && note.duration >= kEighth &&
        !isExempt(params.exempt_voices, note.voice) && !skeleton_plain &&
        note.start_tick != last_onset[note.voice]) {
      const int bar = static_cast<int>(note.start_tick / tpb);
      const Tick pos_in_bar = note.start_tick % tpb;
      const bool is_downbeat = pos_in_bar == 0;
      const bool in_cadence_window = bar >= cadence_window_start_bar;
      const bool in_section_cadence =
          std::binary_search(section_cadence_bars.begin(), section_cadence_bars.end(), bar);
      // Strong beats fall on the half-bar grid (beat 1 and the mid-bar beat).
      // The penultimate strong beat before the final cadence is the priority
      // cadence-trill site; admitting every strong beat inside the last two
      // bars keeps the rule robust to 3/4 vs 4/4 meter.
      const Tick half_bar = tpb / 2 > 0 ? tpb / 2 : kQuarter;
      const bool is_strong_beat = (pos_in_bar % half_bar) == 0;

      // Clean-bass guard: never ornament the lowest-sounding voice. A
      // single-voice piece has no bass to protect, so the guard is multi-voice
      // only.
      const int lowest = lowestSoundingPitch(result.notes, note.start_tick);
      const bool is_bass = multi_voice && note.pitch <= lowest;

      // Cadence ornaments belong to the TOP sounding line only: simultaneous
      // trills in two voices clash at the alternation level, so a note with a
      // higher voice sounding above it takes no ornament inside the cadence
      // window or an interior section-cadence bar. A note with NO other voice
      // sounding is a solo entry -- the one context where the clean-bass guard
      // lifts (pedal-solo mordent).
      const int highest_other =
          highestOtherSoundingPitch(result.notes, note.voice, note.start_tick);
      const bool is_top = highest_other <= static_cast<int>(note.pitch);
      const bool solo_now = highest_other < 0;

      // Neighbour availability.
      const int upper = upperNeighbour(note.pitch, params.mode);
      const int lower = lowerNeighbour(note.pitch, params.mode, in_cadence_window);

      const bool neighbours_ok = upper >= 0 && upper <= ornament_pitch_ceiling && lower >= 0;

      // Ceiling guard: the upper neighbour must not cross above the next-higher
      // voice's concurrent pitch.
      const int ceiling =
          nextHigherVoiceCeiling(result.notes, note.voice, note.pitch, note.start_tick);
      const bool upper_clears_ceiling = upper >= 0 && upper < ceiling;

      if (neighbours_ok && upper_clears_ceiling &&
          (is_top || (!in_cadence_window && !in_section_cadence))) {
        const std::uint64_t roll = placementHash(params.seed, bar, note.voice);

        // Climax uplift window: decoration intensifies where the macro energy
        // arc peaks. Inside the window the density reads one tier higher
        // (cap 2) and the generic gate below opens more often.
        const bool in_climax = params.climax_end_tick > params.climax_start_tick &&
                               note.start_tick >= params.climax_start_tick &&
                               note.start_tick < params.climax_end_tick;
        const std::uint8_t local_density =
            (in_climax && density < 2) ? static_cast<std::uint8_t>(density + 1) : density;

        // Phrase boundaries: the last bar of each 4-bar phrase (outside the
        // final cadence window) is a natural sub-cadence; ornaments cluster
        // there so decoration spreads across the piece instead of bunching in
        // the closing bars. These are candidates only -- the deterministic
        // gate below still applies (never mandatory).
        const bool is_phrase_boundary = (bar % 4 == 3) && !in_cadence_window;

        // Melodic approach context (same-voice previous attack).
        const int prev_pitch = prev_pitch_in_voice[idx];
        const Tick prev_dur = prev_dur_in_voice[idx];
        const bool falling_third_gap =
            prev_pitch >= 0 && (prev_pitch - static_cast<int>(note.pitch) == 3 ||
                                prev_pitch - static_cast<int>(note.pitch) == 4);
        const bool stepwise_descent = prev_pitch >= 0 && prev_pitch == upper;
        const bool rising_leap = prev_pitch >= 0 && static_cast<int>(note.pitch) - prev_pitch >= 5;
        const bool isolated_long = prev_pitch >= 0 && prev_dur > 0 && prev_dur < note.duration;

        // Character x vocabulary grammar (design table, Reduce Generation:
        // the figure each character places at each site class is a design
        // value, not a search). The cadence rows are shared and mandatory --
        // every character closes with the long cadential trill, and every
        // character marks an interior section-cadence bar with a short
        // strong-beat trill.
        //   Severe   : boundary -> appoggiatura (the linear stile antico
        //              suspends rather than strikes); interior -> nothing
        //              beyond the section-cadence trill.
        //   Noble    : boundary -> appoggiatura on a matched approach, turn
        //              otherwise; interior -> turn on half-or-longer tones.
        //   Restless : boundary -> short trill (quarter-or-longer tones) /
        //              mordent (shorter); interior -> downbeat mordent (the
        //              percussive idiom).
        //   Playful  : boundary -> slide under a leap approach / turn (long
        //              tones) / mordent (quarters); interior -> inner trill
        //              on strong-beat quarter-or-longer tones every other
        //              bar, slide under leap arrivals, downbeat mordent on
        //              quarters.
        // Approach-matched figures (appoggiatura, slide, isolated turn) carry
        // their own placement-hash quantile gate; design-value figures run
        // through the generic gate below. Priority on a contested note:
        // cadential trill > appoggiatura > turn > slide > mordent.
        bool want_trill = false;
        bool want_mordent = false;
        bool want_appoggiatura = false;
        bool want_turn = false;
        bool want_slide = false;
        bool self_gated = false;  // the site already consumed its own quantile gate.

        if (is_bass) {
          // Pedal-solo mordent (the organ-toccata idiom): the clean-bass
          // guard lifts ONLY while no other voice sounds at the onset, so a
          // solo pedal entry may strike a downbeat mordent; the bass under
          // ensemble texture stays plain.
          if (solo_now && is_downbeat && note.duration >= kQuarter && !in_cadence_window)
            want_mordent = true;
        } else if (skeleton_voice) {
          // Embellished cantus firmus: the bar-head onsets are the immutable
          // skeleton (the validator matches them verbatim) and always stay
          // plain; within-bar tones of a quarter or longer may carry a turn
          // (a short trill for the percussive Restless) -- the chorale CF
          // moves in quarters, so the embellishment lives on its passing
          // tones. The window keeps off the cadence so the closing CF phrase
          // stays chorale-plain.
          if (!is_downbeat && note.duration >= kQuarter && !in_cadence_window) {
            if (params.character == SubjectCharacter::Restless)
              want_trill = true;
            else
              want_turn = true;
          }
        } else if (in_cadence_window && is_strong_beat && note.duration >= kQuarter) {
          // Priority cadence trill: the strong beat in the last two bars.
          want_trill = true;
        } else if (in_section_cadence && is_strong_beat && note.duration >= kQuarter) {
          // Interior section cadence: every character marks the section close
          // with a short trill, the same rhetoric as the final cadence at
          // lower intensity.
          want_trill = true;
        } else if (note.start_tick < params.aria_end_tick && note.duration >= kHalf) {
          // Goldberg aria showcase: the opening aria decorates its long tones
          // for every character (one density tier above the normal grammar),
          // alternating the lean and the gruppetto by placement hash.
          if (((roll >> 2) & 1ull) == 0ull)
            want_appoggiatura = true;
          else
            want_turn = true;
        } else if (local_density == 0) {
          // Sparse uplift: a density-0 character marks every other phrase
          // boundary (and the designed mid-piece boundary) with its boundary
          // figure, so the piece is not bare until the final cadence. These
          // are design values (one site per 8 bars), not gated sites. The
          // suspension-leaning characters take the appoggiatura; Restless
          // keeps its percussive mordent.
          if ((bar % 8 == 7 || bar == mid_boundary_bar) && !in_cadence_window && is_downbeat) {
            if (params.character == SubjectCharacter::Restless)
              want_mordent = true;
            else
              want_appoggiatura = true;
          }
        } else if (!in_cadence_window) {
          switch (params.character) {
            case SubjectCharacter::Severe:
              // Density above 0 only via instrument scaling (harpsichord):
              // the old-style character still confines itself to boundary
              // suspensions.
              if (is_phrase_boundary && is_strong_beat && note.duration >= kQuarter)
                want_appoggiatura = true;
              break;
            case SubjectCharacter::Noble:
              if (is_phrase_boundary && is_strong_beat && note.duration >= kQuarter) {
                if ((falling_third_gap || stepwise_descent) && ((roll >> 2) & 1ull) == 0ull) {
                  // Matched approach: the lean fills the falling-third gap
                  // (primary) or repeats the previous tone (port de voix).
                  want_appoggiatura = true;
                  self_gated = true;
                } else {
                  want_turn = true;
                }
              } else if (note.duration >= kHalf) {
                // Interior turns animate the long tones only.
                if (is_downbeat && (bar % 2 == 0)) {
                  want_turn = true;
                } else if (isolated_long && ((roll >> 3) & 3ull) == 0ull) {
                  want_turn = true;
                  self_gated = true;
                }
              }
              break;
            case SubjectCharacter::Restless:
              if (is_phrase_boundary && is_strong_beat) {
                // Quarter-or-longer boundary tones take a short trill, the
                // rest a mordent.
                if (note.duration >= kQuarter)
                  want_trill = true;
                else
                  want_mordent = true;
              } else if (is_downbeat && (bar % 2 == 0) &&
                         (note.duration == kQuarter || note.duration == kHalf)) {
                want_mordent = true;
              }
              break;
            case SubjectCharacter::Playful:
              if (is_phrase_boundary && is_strong_beat && note.duration >= kQuarter) {
                if (rising_leap && ((roll >> 5) & 1ull) == 0ull) {
                  want_slide = true;
                  self_gated = true;
                } else if (note.duration >= kHalf) {
                  want_turn = true;
                } else {
                  want_mordent = true;
                }
              } else if (rising_leap && note.duration >= kQuarter && ((roll >> 5) & 1ull) == 0ull) {
                // Slide underneath a tone entered by a rising leap of a
                // fourth or more: the Schleifer fills the gap the leap left.
                want_slide = true;
                self_gated = true;
              } else if (note.duration >= kQuarter && is_strong_beat && (bar % 2 == 0)) {
                // Inner trills on strong-beat quarter-or-longer notes every
                // 2 bars.
                want_trill = true;
              } else if (is_downbeat && (bar % 2 == 0) && note.duration == kQuarter) {
                // Downbeat quarter mordents keep the densest character at
                // least as active as the sparser ones on plain textures.
                want_mordent = true;
              }
              break;
          }
        }

        // A small deterministic gate so not literally every eligible note in a
        // dense passage is ornamented (keeps the texture musical). Cadence
        // trills (final and interior section cadences alike), the designed
        // mid-piece boundary, the density-0 boundary figures (already
        // one-per-8-bars sparse), and the approach-matched figures (each
        // carries its own quantile gate above) bypass the gate. Inside the
        // climax window a second hash bit joins the gate, so it opens for
        // ~3 in 4 sites instead of 1 in 2.
        const bool mandatory = (in_cadence_window || in_section_cadence) && is_strong_beat;
        const bool gate_open = mandatory || bar == mid_boundary_bar ||
                               (local_density == 0 && (want_mordent || want_appoggiatura)) ||
                               self_gated || (roll & 1ull) == 0ull ||
                               (in_climax && ((roll >> 6) & 1ull) == 0ull);

        if (gate_open) {
          if (want_appoggiatura) {
            exp = buildAppoggiatura(note, upper);
          } else if (want_turn) {
            exp = buildTurn(note, upper, lower);
          } else if (want_slide) {
            exp = buildSlide(note, params.mode);
          } else if (want_mordent) {
            exp = buildMordent(note, lower);
          } else if (want_trill) {
            // Long trills (>= half) open with the held upper appoggiatura
            // (appuy); at the cadence the placement hash deterministically
            // mixes in the von-unten doppelt-cadence opening so closing
            // formulas are not all identical. Short trills alternate plain
            // from the upper tone.
            TrillOnset onset = TrillOnset::UpperStart;
            if (note.duration >= kHalf) {
              const bool von_unten =
                  in_cadence_window && cadenceTrillOpensVonUnten(params.seed, bar, note.voice);
              onset = von_unten ? TrillOnset::VonUnten : TrillOnset::Appuy;
            }
            // Long trills (longer than a quarter) pace at sixteenths: a
            // stately alternation suits the held cadential tone, where a
            // run of 32nds would read as a buzz.
            const Tick sub = note.duration > kQuarter
                                 ? std::max(trill_sub, duration::kSixteenthNote)
                                 : trill_sub;
            exp = buildTrill(note, upper, lower, sub, onset);
          }
        }
      }
    }

    if (!exp.notes.empty() &&
        (clashes_committed_ornament(note) || expansion_forms_parallel(exp, note, idx) ||
         expansion_sustains_dissonance(exp, note, idx)))
      exp.notes.clear();  // would clash against another voice: stay plain.

    if (exp.notes.empty()) {
      out_notes.push_back(note);
      out_prov.push_back(prov);
      continue;
    }
    committed_ornaments.push_back({note.voice, note.start_tick, note.start_tick + note.duration,
                                   static_cast<int>(note.pitch)});

    // Expand: each ornament sub-note inherits the original note's provenance
    // (span_id, voice_intent) but is re-sourced as Ornament so the post-pass
    // provenance is honest and the validator's Compose-only strong-beat rule
    // skips these mode-constrained decoration tones.
    for (const auto& sub : exp.notes) {
      out_notes.push_back(sub);
      NoteProvenance sub_prov = prov;
      sub_prov.source = NoteSource::Ornament;
      sub_prov.satisfied_rules |= ruleBitMask(RuleBit::OrnamentRealized);
      out_prov.push_back(sub_prov);
    }
  }

  result.notes = std::move(out_notes);
  result.provenance = std::move(out_prov);

  // Re-render tracks from the ornamented note list so the per-voice copies
  // mirror result.notes exactly (the Renderer groups by voice and clamps
  // same-voice overlap; it never moves pitch or onset).
  result.tracks = Renderer{}.render(result.notes);

  // Refresh the embedded texture metrics so they describe the FINAL
  // (ornamented) note list: downstream JSON consumers recompute metrics from
  // the emitted notes and must see the same values.
  if (!result.validation.texture_metrics.empty()) {
    result.validation.texture_metrics.clear();
    result.validation.texture_metrics.push_back(computeTextureMetrics(result.notes));
  }
}

}  // namespace bach::composer
