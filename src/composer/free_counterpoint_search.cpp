#include "composer/free_counterpoint_search.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <string_view>
#include <vector>

#include "composer/melodic_tables.h"
#include "composer/rule_helpers.h"

namespace bach::composer {

namespace {

constexpr Tick kQuarter = kTicksPerBeat;

struct MelodicScoringConfig {
  bool use_shadow_selection = true;
  bool step_bonus_enabled = true;
  float proximity_weight = 2.0f;
  float range_weight = 1.0f;
  float scale_degree_weight = 0.0f;
  float markov_weight = 0.0f;
  float local_rule_adjustment_weight = 4.0f;
};

bool envEquals(const char* name, std::string_view value) {
  const char* raw = std::getenv(name);
  return raw != nullptr && value == raw;
}

float envFloat(const char* name, float fallback) {
  const char* raw = std::getenv(name);
  if (raw == nullptr || raw[0] == '\0') {
    return fallback;
  }
  char* end = nullptr;
  const float parsed = std::strtof(raw, &end);
  return end != raw ? parsed : fallback;
}

MelodicScoringConfig melodicScoringConfig() {
  MelodicScoringConfig config;
  if (envEquals("BACH_MELODIC_SELECTION", "current")) {
    config.use_shadow_selection = false;
  } else if (envEquals("BACH_MELODIC_SELECTION", "shadow")) {
    config.use_shadow_selection = true;
  }
  config.step_bonus_enabled = !envEquals("BACH_MELODIC_STEP_BONUS", "0");
  config.proximity_weight = envFloat("BACH_MELODIC_WP", 2.0f);
  config.range_weight = envFloat("BACH_MELODIC_WR", 1.0f);
  config.scale_degree_weight = envFloat("BACH_MELODIC_WSD", 0.0f);
  config.markov_weight = envFloat("BACH_MELODIC_WM", 0.0f);
  return config;
}

// Harmonic primitives are shared with composer.cpp via rule_helpers; the
// local aliases keep the existing call sites (triadPitchClasses, activeChord)
// unchanged while routing through the single shared implementation.
using rule_helpers::activeChord;
using rule_helpers::isStructuralAccent;
using rule_helpers::triadPitchClasses;

tables::GaussianFit gaussianFitFor(MelodicCorpusCategory category) {
  switch (category) {
    case MelodicCorpusCategory::SoloString:
      return tables::kGaussianFitSoloString;
    case MelodicCorpusCategory::Chorale:
      return tables::kGaussianFitChorale;
    case MelodicCorpusCategory::Organ:
      return tables::kGaussianFitOrgan;
  }
  return tables::kGaussianFitOrgan;
}

float computeShadowScore(int pitch, bool is_triad, const CandidateContext& context,
                         const std::vector<std::uint8_t>& recent_pitches,
                         std::uint8_t pre_prev_pitch_local, std::uint8_t prev_pitch_local,
                         bool is_minor_mode, bool include_markov,
                         const MelodicScoringConfig& config) {
  const tables::GaussianFit fit = gaussianFitFor(context.melodic_category);
  const float chord_term = is_triad ? 0.8f : 0.4f;
  const int mode_index = is_minor_mode ? 1 : 0;
  const float scale_degree = tables::kScaleDegreeLogP[mode_index][pitch % 12];

  float proximity = 0.0f;
  if (prev_pitch_local != 0) {
    const float delta = static_cast<float>(pitch) - static_cast<float>(prev_pitch_local);
    proximity = -(delta * delta) / (2.0f * fit.vp);
  }

  float range_center = static_cast<float>(context.voice_center);
  if (!recent_pitches.empty()) {
    int sum = 0;
    const std::size_t begin = recent_pitches.size() > 8 ? recent_pitches.size() - 8 : 0;
    for (std::size_t i = begin; i < recent_pitches.size(); ++i) {
      sum += static_cast<int>(recent_pitches[i]);
    }
    range_center = static_cast<float>(sum) / static_cast<float>(recent_pitches.size() - begin);
  }
  const float range_delta = static_cast<float>(pitch) - range_center;
  const float range = -(range_delta * range_delta) / (2.0f * fit.vr);

  float markov = 0.0f;
  if (include_markov && pre_prev_pitch_local != 0 && prev_pitch_local != 0) {
    const int prev_int = std::max(-12, std::min(12, static_cast<int>(prev_pitch_local) -
                                                        static_cast<int>(pre_prev_pitch_local)));
    const int cur_int = std::max(-12, std::min(12, pitch - static_cast<int>(prev_pitch_local)));
    markov = tables::kIntervalLogP[prev_int + 12][cur_int + 12];
  }

  return chord_term + (config.proximity_weight * proximity) + (config.range_weight * range) +
         (config.scale_degree_weight * scale_degree) + (config.markov_weight * markov);
}

// Imports of shared rule primitives (defined in composer/rule_helpers.cpp).
// Local re-exports keep the existing call sites unchanged while routing
// all rule semantics through the single shared implementation.
using rule_helpers::applyP7Bits;
using rule_helpers::applyP8Bits;
using rule_helpers::cadenceCellAt;
using rule_helpers::createsCrossRelation;
using rule_helpers::createsHiddenParallelPerfect;
using rule_helpers::createsHiddenParallelPerfectAcrossOnset;
using rule_helpers::createsParallelPerfect;
using rule_helpers::createsParallelPerfectAcrossOnset;
using rule_helpers::createsVerticalDissonance;
using rule_helpers::createsVoiceCrossing;
using rule_helpers::isStrongBeat;
using rule_helpers::sameVoiceStartingAt;
using rule_helpers::voicePitchAt;

// Guard form: returns true if `previous` is not a leading tone OR
// `candidate` is a valid stepwise upward resolution. Used by the
// candidate enumerator's "may I pick this next pitch?" gate.
bool resolvesLeadingTone(std::uint8_t previous, int candidate, const HarmonicPlan& plan,
                         Tick leading_tick) {
  if (!rule_helpers::isContextualLeadingTone(previous, plan, leading_tick))
    return true;
  return rule_helpers::isContextualLeadingToneResolution(previous, candidate, plan, leading_tick);
}

// Cross-span lookahead context for sameVoiceStartingAt: when a Carrier
// (SubjectCarrier or AnswerCarrier) span has already been placed at the
// next tick, the current weak non-chord-tone pick must remain within ±2
// semitones of that fixed pitch or the Validator's `unprepared_dissonance`
// rule will fire on the last weak position of the preceding Compose span.

bool hasContraryMotion(const std::vector<NoteEvent>& placed, VoiceId candidate_voice,
                       std::uint8_t candidate_pitch, Tick cur_tick, std::uint8_t prev_pitch,
                       Tick prev_tick) {
  if (prev_pitch == 0)
    return false;
  const int this_motion = static_cast<int>(candidate_pitch) - static_cast<int>(prev_pitch);
  if (this_motion == 0)
    return false;

  std::vector<VoiceId> other_voices;
  for (const auto& n : placed) {
    if (n.voice == candidate_voice)
      continue;
    if (std::find(other_voices.begin(), other_voices.end(), n.voice) == other_voices.end()) {
      other_voices.push_back(n.voice);
    }
  }
  for (VoiceId ov : other_voices) {
    const std::uint8_t op_now = voicePitchAt(placed, ov, cur_tick);
    const std::uint8_t op_prev = voicePitchAt(placed, ov, prev_tick);
    if (op_now == 0 || op_prev == 0)
      continue;
    const int other_motion = static_cast<int>(op_now) - static_cast<int>(op_prev);
    if (other_motion == 0)
      continue;
    if ((this_motion > 0 && other_motion < 0) || (this_motion < 0 && other_motion > 0)) {
      return true;
    }
  }
  return false;
}

bool createsVoiceCrossingDuring(const std::vector<NoteEvent>& placed, VoiceId candidate_voice,
                                std::uint8_t candidate_pitch, Tick start, Tick duration) {
  const Tick end = start + duration;
  for (const auto& note : placed) {
    if (note.voice == candidate_voice)
      continue;
    if (note.start_tick >= end || note.start_tick + note.duration <= start)
      continue;
    if (candidate_voice < note.voice && candidate_pitch < note.pitch)
      return true;
    if (candidate_voice > note.voice && candidate_pitch > note.pitch)
      return true;
  }
  return false;
}

bool createsVerticalDissonanceDuring(const std::vector<NoteEvent>& placed, VoiceId candidate_voice,
                                     std::uint8_t candidate_pitch, Tick start, Tick duration) {
  const Tick end = start + duration;
  for (const auto& note : placed) {
    if (note.voice == candidate_voice)
      continue;
    if (note.start_tick >= end || note.start_tick + note.duration <= start)
      continue;
    const Tick sample_tick = std::max(start, note.start_tick);
    if (rule_helpers::createsVerticalDissonance(placed, candidate_voice, candidate_pitch,
                                                sample_tick)) {
      return true;
    }
  }
  return false;
}

int trailingPitchRunLength(const std::vector<std::uint8_t>& pitches, std::uint8_t pitch) {
  int run = 0;
  for (auto it = pitches.rbegin(); it != pitches.rend(); ++it) {
    if (*it != pitch) {
      break;
    }
    ++run;
  }
  return run;
}

std::uint8_t nextSameVoiceStartingAfter(const std::vector<NoteEvent>& placed, VoiceId voice,
                                        Tick tick) {
  Tick best_tick = 0;
  std::uint8_t best_pitch = 0;
  for (const auto& note : placed) {
    if (note.voice != voice)
      continue;
    if (note.start_tick <= tick)
      continue;
    if (best_pitch == 0 || note.start_tick < best_tick) {
      best_tick = note.start_tick;
      best_pitch = note.pitch;
    }
  }
  return best_pitch;
}

}  // namespace

std::vector<Candidate> composeFreeSpan(const Span& span, const HarmonicPlan& harmonic_plan,
                                       const Material& material, const CandidateContext& context,
                                       std::size_t* saturated_positions) {
  std::vector<Candidate> out;

  // Compose spans: lay down one note per beat aligned to span.start_tick.
  // Pitch picked from current chord tones near voice_center. Spans with
  // Subdivision::Eighth produce two notes per beat instead — the rest
  // of the rule cascade (vertical, leap, passing-tone) operates per
  // note position and is stride-agnostic.
  const Tick stride = (span.subdivision == Subdivision::Eighth) ? kQuarter / 2 : kQuarter;
  const MelodicScoringConfig scoring_config = melodicScoringConfig();

  // Local cursor used for vertical (other-voice) parallel checks.
  // Holds the candidate this enumerate() call just committed, or the
  // caller-supplied anchor for the first iteration. The "previous tick"
  // is the actual start_tick of that committed candidate (not its end),
  // so the validator-style lookup at prev_tick observes the same pitch
  // configuration.
  std::uint8_t parallel_prev_pitch = context.prev_pitch;
  Tick parallel_prev_tick = context.prev_end_tick > 0 ? context.prev_end_tick - stride : 0;
  bool have_parallel_anchor = context.prev_pitch != 0;

  // For leap-resolution. We need both the previous pitch and the one
  // before it to detect a leap that the candidate must not continue
  // with another leap. Seed the local cursor from the context so the
  // rule applies across span boundaries, then update on each commit.
  std::uint8_t pre_prev_pitch_local = context.pre_prev_pitch;
  std::uint8_t prev_pitch_local = context.prev_pitch;
  std::vector<std::uint8_t> recent_pitches;
  if (context.pre_prev_pitch != 0) {
    recent_pitches.push_back(context.pre_prev_pitch);
  }
  if (context.prev_pitch != 0) {
    recent_pitches.push_back(context.prev_pitch);
  }

  // Leave-side passing-tone tracker. True iff the most recent commit
  // (in this span, or carried from the previous span via context)
  // landed on a non-chord-tone, so the next pitch must be a step
  // (≤2) from prev_pitch_local. Without this, the search can pick a
  // non-triad passing tone whose next-pitch choice ignores the
  // approach-rule recursion, violating the Validator's
  // `unprepared_dissonance` rule. Updated after each commit.
  bool prev_was_pt_local = context.prev_was_passing_tone;

  for (Tick t = span.start_tick; t < span.end_tick; t += stride) {
    const ChordEvent& chord = activeChord(harmonic_plan, t);
    const auto triad = triadPitchClasses(chord);
    const CadenceCell* cadence_cell = cadenceCellAt(material, t);
    const bool force_cadence_pc = cadence_cell != nullptr;
    const bool force_bass_cadence_pc = cadence_cell != nullptr && span.voice > 0;
    const std::uint8_t forced_cadence_pc =
        (cadence_cell != nullptr && span.voice == 0)
            ? ((t == cadence_cell->approach_tick) ? cadence_cell->soprano_approach_pc
                                                  : cadence_cell->soprano_cadence_pc)
            : ((cadence_cell != nullptr && t == cadence_cell->approach_tick)
                   ? cadence_cell->bass_approach_pc
                   : ((cadence_cell != nullptr) ? cadence_cell->bass_cadence_pc : 0));

    // Enumerate triad-tone pitches in [voice_center - 7, voice_center + 12].
    int best_pitch = -1;
    float best_score = -1.0f;
    float best_selection_score = -1.0e9f;
    float best_shadow_score = 0.0f;
    int best_shadow_pitch = -1;
    float best_shadow_winner_score = -1.0e9f;
    int best_shadow_pitch_without_markov = -1;
    float best_shadow_winner_score_without_markov = -1.0e9f;
    RuleIdMask best_rules = 0;
    for (int p = context.voice_center - 7; p <= context.voice_center + 12; ++p) {
      if (p < 0 || p > 127)
        continue;
      const std::uint8_t pc = static_cast<std::uint8_t>(p % 12);
      if (force_cadence_pc && pc != forced_cadence_pc)
        continue;
      const bool is_triad = (pc == triad[0]) || (pc == triad[1]) || (pc == triad[2]);
      const bool strong = isStructuralAccent(harmonic_plan, t);
      const bool strict_harmonic_support = span.intent == VoiceIntent::HarmonicSupport;
      if (prev_pitch_local != 0 &&
          !resolvesLeadingTone(prev_pitch_local, p, harmonic_plan, parallel_prev_tick)) {
        continue;
      }
      if (strong && !is_triad)
        continue;  // strong-beat consonance rule
      if (strict_harmonic_support && !is_triad)
        continue;
      if (strict_harmonic_support &&
          trailingPitchRunLength(recent_pitches, static_cast<std::uint8_t>(p)) >= 4) {
        continue;
      }

      // Vertical rule: reject candidate that crosses any already-placed
      // voice (lower voice index must stay above higher voice index).
      if (context.placed_notes != nullptr &&
          createsVoiceCrossing(*context.placed_notes, span.voice, static_cast<std::uint8_t>(p),
                               t)) {
        continue;
      }
      if (strict_harmonic_support && context.placed_notes != nullptr &&
          createsVoiceCrossingDuring(*context.placed_notes, span.voice,
                                     static_cast<std::uint8_t>(p), t, stride)) {
        continue;
      }

      // P7 spacing pre-filter. When the active chord declares
      // has_degree=true, reject any candidate that would push an
      // upper-voice adjacent pair past an octave. We check only the
      // pairs the Validator's spacing rule covers (top N-2 pairs in
      // an N-voice texture). Without this filter the Composer can
      // pick a candidate pitch that drops too low (a textbook
      // "open" spacing) which the Validator rejects after the fact
      // and forces a seed-level fail.
      if (chord.has_degree && context.placed_notes != nullptr) {
        bool spacing_violation = false;
        for (const auto& placed : *context.placed_notes) {
          if (placed.voice == span.voice)
            continue;
          if (placed.start_tick > t)
            continue;
          if (t >= placed.start_tick + placed.duration)
            continue;
          // Spacing check only applies to upper-voice pairs: skip
          // when the lower-indexed voice of the pair (this voice or
          // the placed voice) is the bottom voice of the texture.
          // Voice id convention: V0 = top, V_{N-1} = bottom.
          const VoiceId hi_voice = std::min(placed.voice, span.voice);
          const VoiceId lo_voice = std::max(placed.voice, span.voice);
          // Only adjacent voice pairs constrained.
          if (lo_voice != hi_voice + 1)
            continue;
          // Skip the bottom-of-texture pair: spacing rule excludes
          // the lowest pair (V_{N-2} — V_{N-1}). The pair to exclude
          // has lo_voice == N - 1 (i.e. the bottom voice is in the
          // pair). For N = 3 only (V0, V1) is checked; (V1, V2) is
          // skipped because lo_voice = 2 = N - 1.
          if (context.num_voices >= 2 && lo_voice == context.num_voices - 1)
            continue;
          const int gap = std::abs(static_cast<int>(p) - static_cast<int>(placed.pitch));
          if (gap > 12) {
            spacing_violation = true;
            break;
          }
        }
        if (spacing_violation)
          continue;
      }

      // P7 doubling pre-filter. When the active chord declares
      // has_degree=true AND owns the leading tone (chord triad
      // contains tonic+11), reject any candidate whose pc matches
      // the leading tone if a previously-placed voice is already
      // sounding the leading tone at this tick. This keeps the
      // Composer from picking a doubled leading tone that the
      // Validator's `doubling_no_leading_tone` rule would later
      // reject. Without this, FugueHarmonized carriers (whose pitches are
      // fixed Material) can coincide with a Compose voice landing on
      // B, fail validation, and bounce the seed.
      if (chord.has_degree && context.placed_notes != nullptr) {
        const auto tonal_context = rule_helpers::tonalContextAt(harmonic_plan, t);
        const std::uint8_t lt_pc = tonal_context.leading_tone_pc;
        const bool chord_owns_lt =
            tonal_context.has_active_leading_tone &&
            ((triad[0] == lt_pc) || (triad[1] == lt_pc) || (triad[2] == lt_pc));
        if (chord_owns_lt && pc == lt_pc) {
          bool other_voice_has_lt = false;
          for (const auto& placed : *context.placed_notes) {
            if (placed.voice == span.voice)
              continue;
            if (placed.start_tick > t)
              continue;
            if (t >= placed.start_tick + placed.duration)
              continue;
            if (static_cast<std::uint8_t>(placed.pitch % 12) == lt_pc) {
              other_voice_has_lt = true;
              break;
            }
          }
          if (other_voice_has_lt)
            continue;
        }
      }

      // Vertical rule: on strong beats, candidate must form a
      // consonant interval with every already-placed voice that is
      // sounding at this tick. Weak beats apply a soft score penalty
      // instead (handled below in the score block) so passing-tone
      // dissonances remain reachable when no consonant option fits.
      const bool vertically_dissonant = context.placed_notes != nullptr &&
                                        createsVerticalDissonance(*context.placed_notes, span.voice,
                                                                  static_cast<std::uint8_t>(p), t);
      if ((strong || strict_harmonic_support) && vertically_dissonant) {
        continue;
      }
      if (strict_harmonic_support && context.placed_notes != nullptr &&
          createsVerticalDissonanceDuring(*context.placed_notes, span.voice,
                                          static_cast<std::uint8_t>(p), t, stride)) {
        continue;
      }

      // Vertical rule: reject candidate that creates parallel perfect
      // motion against any already-placed voice. Only attempted when
      // an other-voice context is supplied AND we have a valid prev
      // anchor in this voice.
      //
      // Cadence cells bypass the general parallel-perfect rule because
      // their bass pitch class is forced — accepting an occasional
      // parallel fifth into the cadence is preferred over failing the
      // span. Parallel octaves remain prohibited even under a cadence
      // cell (see createsParallelOctave below).
      if (!force_bass_cadence_pc && context.placed_notes != nullptr && have_parallel_anchor &&
          createsParallelPerfect(*context.placed_notes, span.voice, static_cast<std::uint8_t>(p), t,
                                 parallel_prev_pitch, parallel_prev_tick)) {
        continue;
      }
      // Faster-voice parallel: a Material figuration voice may move between this
      // (slower) Compose voice's onsets, so a parallel can form at the
      // intermediate union tick that the onset-to-onset check above misses.
      if (!force_bass_cadence_pc && context.placed_notes != nullptr && have_parallel_anchor &&
          createsParallelPerfectAcrossOnset(*context.placed_notes, span.voice,
                                            static_cast<std::uint8_t>(p), t, parallel_prev_pitch,
                                            parallel_prev_tick)) {
        continue;
      }
      if (context.placed_notes != nullptr && have_parallel_anchor &&
          rule_helpers::createsParallelOctave(*context.placed_notes, span.voice,
                                              static_cast<std::uint8_t>(p), t, parallel_prev_pitch,
                                              parallel_prev_tick)) {
        continue;
      }
      // Both of the following reach their perfect interval by CONTRARY motion,
      // so none of the parallel checks above can see either. They follow the
      // same cadence bypass: a cadence cell pins its bass pitch class and has no
      // candidate left to move to.
      if (!force_bass_cadence_pc && context.placed_notes != nullptr && have_parallel_anchor &&
          rule_helpers::createsAntiParallelPerfect(*context.placed_notes, span.voice,
                                                   static_cast<std::uint8_t>(p), t,
                                                   parallel_prev_pitch, parallel_prev_tick)) {
        continue;
      }
      if (!force_bass_cadence_pc && context.placed_notes != nullptr && have_parallel_anchor &&
          rule_helpers::createsBattuta(*context.placed_notes, span.voice,
                                       static_cast<std::uint8_t>(p), t, parallel_prev_pitch,
                                       parallel_prev_tick)) {
        continue;
      }

      if (!force_bass_cadence_pc && context.placed_notes != nullptr && have_parallel_anchor &&
          createsHiddenParallelPerfect(*context.placed_notes, span.voice,
                                       static_cast<std::uint8_t>(p), t, parallel_prev_pitch,
                                       parallel_prev_tick)) {
        continue;
      }
      if (!force_bass_cadence_pc && context.placed_notes != nullptr && have_parallel_anchor &&
          createsHiddenParallelPerfectAcrossOnset(*context.placed_notes, span.voice,
                                                  static_cast<std::uint8_t>(p), t,
                                                  parallel_prev_pitch, parallel_prev_tick)) {
        continue;
      }
      if (!force_bass_cadence_pc && context.placed_notes != nullptr &&
          createsCrossRelation(*context.placed_notes, span.voice, static_cast<std::uint8_t>(p),
                               t)) {
        continue;
      }
      // Cadence cells force the approach/cadence pitch classes in every
      // voice (forced_cadence_pc above) and bypass the vertical filters for
      // the forced notes themselves. A free candidate within the Validator's
      // cross-relation adjacency window (one beat) of a cell tick is judged
      // against pitches that may not be placed yet but are already known, so
      // reject any candidate whose pc forms a cross relation with a pc the
      // cell will force. Without this, a chromatic chord tone (e.g. the G#
      // of V/vi) can land a beat from the forced cadence G and the Validator
      // rejects the seed after the fact.
      if (!force_cadence_pc) {
        bool clashes_forced_cadence_pc = false;
        for (const auto& cell : material.cadence_cells) {
          const struct {
            Tick tick;
            std::uint8_t soprano_pc;
            std::uint8_t bass_pc;
          } points[2] = {{cell.approach_tick, cell.soprano_approach_pc, cell.bass_approach_pc},
                         {cell.cadence_tick, cell.soprano_cadence_pc, cell.bass_cadence_pc}};
          for (const auto& point : points) {
            const Tick window_lo =
                point.tick > kTicksPerBeat ? point.tick - kTicksPerBeat : Tick{0};
            if (t < window_lo || t > point.tick + kTicksPerBeat)
              continue;
            if (rule_helpers::isCrossRelationPc(pc, point.soprano_pc) ||
                rule_helpers::isCrossRelationPc(pc, point.bass_pc)) {
              clashes_forced_cadence_pc = true;
              break;
            }
          }
          if (clashes_forced_cadence_pc)
            break;
        }
        if (clashes_forced_cadence_pc)
          continue;
      }

      // Melodic rule (mirrors Validator Rule P1): reject a forbidden
      // melodic leap — tritone, augmented 2nd/4th, or diminished 5th/octave
      // — from the previous pitch to this candidate. Without this pre-filter
      // the search can pick a best-scoring triad tone that forms such a leap;
      // the Validator then rejects it after the fact and bounces the whole
      // seed (observed once the FugueComplete counterline was raised toward the high
      // subject). Scoped exactly like the Validator: skipped under a cadence
      // cell (force_bass_cadence_pc) and when a secondary-dominant chord is
      // active at either endpoint (its non-diatonic chord tones are idiomatic).
      if (!force_bass_cadence_pc && prev_pitch_local != 0) {
        const ChordEvent& chord_at_prev = activeChord(harmonic_plan, parallel_prev_tick);
        const bool secondary_active = chord.has_secondary_of || chord_at_prev.has_secondary_of;
        if (!secondary_active &&
            rule_helpers::isForbiddenMelodicLeap(prev_pitch_local, static_cast<std::uint8_t>(p),
                                                 harmonic_plan)) {
          continue;
        }
      }

      // Melodic rule: if the previous motion (pre_prev -> prev) was
      // a wide leap (>= P5), forbid a second wide leap from prev to
      // this candidate. The single-leap case stays unconstrained.
      if (!force_bass_cadence_pc && pre_prev_pitch_local != 0 && prev_pitch_local != 0) {
        const int delta_pre =
            static_cast<int>(prev_pitch_local) - static_cast<int>(pre_prev_pitch_local);
        const int delta_cur = p - static_cast<int>(prev_pitch_local);
        if (std::abs(delta_pre) >= 7 && std::abs(delta_cur) >= 7) {
          continue;
        }
      }

      // Passing-tone rule (approach side): a weak-beat non-chord tone
      // must be approached by step (<= 2 semis).
      if (!force_bass_cadence_pc && !strong && !is_triad && prev_pitch_local != 0) {
        if (std::abs(p - static_cast<int>(prev_pitch_local)) > 2) {
          continue;
        }
      }

      // Passing-tone rule (leave side): if the previous commit was a
      // non-chord-tone (passing tone), the current candidate must be
      // within 2 semis of prev_pitch_local. This is the in-cascade
      // counterpart of the Validator's `unprepared_dissonance` rule —
      // by enforcing it during enumeration we avoid generating
      // notes the Validator would later reject. Without this guard,
      // the search could pick a non-triad p at position k, then at
      // position k+1 pick a triad-tone 3+ semis away (because the
      // triad bonus dominates), violating the rule.
      if (!force_bass_cadence_pc && prev_was_pt_local && prev_pitch_local != 0) {
        if (std::abs(p - static_cast<int>(prev_pitch_local)) > 2) {
          continue;
        }
      }

      if (rule_helpers::isContextualLeadingTone(static_cast<std::uint8_t>(p), harmonic_plan, t)) {
        const Tick t_next = t + stride;
        bool has_resolution = false;
        if (context.placed_notes != nullptr) {
          const std::uint8_t fixed_next =
              sameVoiceStartingAt(*context.placed_notes, span.voice, t_next);
          if (fixed_next != 0) {
            has_resolution =
                resolvesLeadingTone(static_cast<std::uint8_t>(p), fixed_next, harmonic_plan, t);
          } else {
            const std::uint8_t future_fixed =
                nextSameVoiceStartingAfter(*context.placed_notes, span.voice, t_next);
            if (future_fixed != 0) {
              has_resolution =
                  resolvesLeadingTone(static_cast<std::uint8_t>(p), future_fixed, harmonic_plan, t);
            }
          }
        }
        const CadenceCell* immediate_cadence_cell = cadenceCellAt(material, t_next);
        if (immediate_cadence_cell != nullptr) {
          const std::uint8_t immediate_forced_pc =
              (span.voice == 0) ? ((t_next == immediate_cadence_cell->approach_tick)
                                       ? immediate_cadence_cell->soprano_approach_pc
                                       : immediate_cadence_cell->soprano_cadence_pc)
                                : ((t_next == immediate_cadence_cell->approach_tick)
                                       ? immediate_cadence_cell->bass_approach_pc
                                       : immediate_cadence_cell->bass_cadence_pc);
          bool cadence_can_resolve = false;
          for (int q = static_cast<int>(p) + 1; q <= static_cast<int>(p) + 2; ++q) {
            if (q < 0 || q > 127)
              continue;
            if (static_cast<std::uint8_t>(q % 12) != immediate_forced_pc)
              continue;
            if (resolvesLeadingTone(static_cast<std::uint8_t>(p), q, harmonic_plan, t)) {
              cadence_can_resolve = true;
              break;
            }
          }
          if (!cadence_can_resolve)
            continue;
        }
        if (!has_resolution && t_next + stride < span.end_tick) {
          const CadenceCell* next_cadence_cell = cadenceCellAt(material, t_next);
          const bool next_force_cadence_pc = next_cadence_cell != nullptr;
          const std::uint8_t next_forced_cadence_pc =
              (next_cadence_cell != nullptr && span.voice == 0)
                  ? ((t_next == next_cadence_cell->approach_tick)
                         ? next_cadence_cell->soprano_approach_pc
                         : next_cadence_cell->soprano_cadence_pc)
                  : ((next_cadence_cell != nullptr && t_next == next_cadence_cell->approach_tick)
                         ? next_cadence_cell->bass_approach_pc
                         : ((next_cadence_cell != nullptr) ? next_cadence_cell->bass_cadence_pc
                                                           : 0));
          for (int q = p + 1; q <= p + 2; ++q) {
            if (q < context.voice_center - 7 || q > context.voice_center + 12)
              continue;
            const std::uint8_t q_pc_for_cadence = static_cast<std::uint8_t>(q % 12);
            if (next_force_cadence_pc && q_pc_for_cadence != next_forced_cadence_pc)
              continue;
            if (!resolvesLeadingTone(static_cast<std::uint8_t>(p), q, harmonic_plan, t))
              continue;
            if (context.placed_notes != nullptr &&
                createsVoiceCrossing(*context.placed_notes, span.voice,
                                     static_cast<std::uint8_t>(q), t_next)) {
              continue;
            }
            if (context.placed_notes != nullptr &&
                createsVerticalDissonance(*context.placed_notes, span.voice,
                                          static_cast<std::uint8_t>(q), t_next)) {
              continue;
            }
            if (context.placed_notes != nullptr &&
                createsParallelPerfect(*context.placed_notes, span.voice,
                                       static_cast<std::uint8_t>(q), t_next,
                                       static_cast<std::uint8_t>(p), t)) {
              continue;
            }
            if (context.placed_notes != nullptr &&
                createsHiddenParallelPerfect(*context.placed_notes, span.voice,
                                             static_cast<std::uint8_t>(q), t_next,
                                             static_cast<std::uint8_t>(p), t)) {
              continue;
            }
            if (context.placed_notes != nullptr &&
                createsCrossRelation(*context.placed_notes, span.voice,
                                     static_cast<std::uint8_t>(q), t_next)) {
              continue;
            }
            const ChordEvent& q_chord = activeChord(harmonic_plan, t_next);
            const auto q_triad = triadPitchClasses(q_chord);
            const std::uint8_t q_pc = static_cast<std::uint8_t>(q % 12);
            const bool q_is_triad = q_pc == q_triad[0] || q_pc == q_triad[1] || q_pc == q_triad[2];
            if (isStructuralAccent(harmonic_plan, t_next) && !q_is_triad) {
              continue;
            }
            if (!isStructuralAccent(harmonic_plan, t_next) && !q_is_triad &&
                context.placed_notes != nullptr) {
              const Tick q_next_tick = t_next + stride;
              const std::uint8_t fixed_after_q =
                  sameVoiceStartingAt(*context.placed_notes, span.voice, q_next_tick);
              if (fixed_after_q == 0 || std::abs(q - static_cast<int>(fixed_after_q)) > 2) {
                continue;
              }
            }
            has_resolution = true;
            break;
          }
        }
        if (!has_resolution) {
          continue;
        }
      }

      // Non-triad weak-pick lookahead. When the next position is a
      // strong beat (new bar / chord change), it must be a triad tone
      // of the new chord AND, by leave-side rule above, within ±2 of
      // p. If no triad tone of the next chord fits that ±2 window
      // AND survives vertical-dissonance AND parallel-perfect against
      // already-placed voices at t_next, p is a dead-end — picking
      // it would force the next position to emit nothing. Reject p
      // so the search falls back to a triad of THIS chord (which has
      // no leave-side restriction).
      //
      // Lookahead crosses span boundaries because the test plan often
      // aligns bar = span (so t_next == span.end_tick at the last
      // weak beat). Leave-side enforcement is already span-crossing
      // via CandidateContext::prev_was_passing_tone, so the lookahead
      // must agree.
      if (!force_bass_cadence_pc && !strong && !is_triad) {
        const Tick t_next = t + stride;
        // Cross-span fixed-next leave-side check: when a Carrier span
        // (SubjectCarrier or AnswerCarrier) has already placed a note
        // at t_next in this voice, the Validator's
        // `unprepared_dissonance` rule requires |p - fixed_next| ≤ 2.
        // The intra-span leave-side rule below (prev_was_pt_local +
        // approach-side) does not see this because t_next is in a
        // different span. A V1 counterline note in bar 3 feeding into a
        // V1 AnswerCarrier note in bar 4 is the canonical case.
        if (context.placed_notes != nullptr) {
          const std::uint8_t fixed_next =
              sameVoiceStartingAt(*context.placed_notes, span.voice, t_next);
          bool has_known_followup = fixed_next != 0;
          if (fixed_next != 0 && std::abs(p - static_cast<int>(fixed_next)) > 2) {
            continue;
          }
          if (fixed_next == 0) {
            const std::uint8_t future_fixed =
                nextSameVoiceStartingAfter(*context.placed_notes, span.voice, t_next);
            has_known_followup = future_fixed != 0;
            if (future_fixed != 0 && std::abs(p - static_cast<int>(future_fixed)) > 2) {
              continue;
            }
          }
          if (!has_known_followup && t_next >= span.end_tick) {
            continue;
          }
        }
        if (t_next < span.end_tick) {
          const ChordEvent& chord_next_lh = activeChord(harmonic_plan, t_next);
          const auto triad_next = triadPitchClasses(chord_next_lh);
          const bool next_strong = isStructuralAccent(harmonic_plan, t_next);
          bool has_step_followup = false;
          for (int q = p - 2; q <= p + 2; ++q) {
            if (q < context.voice_center - 7 || q > context.voice_center + 12)
              continue;
            if (q < 0 || q > 127)
              continue;
            const std::uint8_t q_pc = static_cast<std::uint8_t>(q % 12);
            const bool q_is_triad =
                (q_pc == triad_next[0]) || (q_pc == triad_next[1]) || (q_pc == triad_next[2]);
            if (next_strong && !q_is_triad)
              continue;
            if (context.placed_notes != nullptr &&
                createsVoiceCrossing(*context.placed_notes, span.voice,
                                     static_cast<std::uint8_t>(q), t_next)) {
              continue;
            }
            if (context.placed_notes != nullptr && next_strong &&
                createsVerticalDissonance(*context.placed_notes, span.voice,
                                          static_cast<std::uint8_t>(q), t_next)) {
              continue;
            }
            if (context.placed_notes != nullptr &&
                createsParallelPerfect(*context.placed_notes, span.voice,
                                       static_cast<std::uint8_t>(q), t_next,
                                       static_cast<std::uint8_t>(p), t)) {
              continue;
            }
            if (context.placed_notes != nullptr &&
                rule_helpers::createsParallelOctave(*context.placed_notes, span.voice,
                                                    static_cast<std::uint8_t>(q), t_next,
                                                    static_cast<std::uint8_t>(p), t)) {
              continue;
            }
            if (context.placed_notes != nullptr &&
                createsHiddenParallelPerfect(*context.placed_notes, span.voice,
                                             static_cast<std::uint8_t>(q), t_next,
                                             static_cast<std::uint8_t>(p), t)) {
              continue;
            }
            if (context.placed_notes != nullptr &&
                createsCrossRelation(*context.placed_notes, span.voice,
                                     static_cast<std::uint8_t>(q), t_next)) {
              continue;
            }
            has_step_followup = true;
            break;
          }
          if (!has_step_followup)
            continue;
        }
        if (isStructuralAccent(harmonic_plan, t_next)) {
          const ChordEvent& chord_next_lh = activeChord(harmonic_plan, t_next);
          const auto triad_next = triadPitchClasses(chord_next_lh);
          bool has_strong_followup = false;
          for (int q = p - 2; q <= p + 2; ++q) {
            if (q < 0 || q > 127)
              continue;
            const std::uint8_t pc_q = static_cast<std::uint8_t>(q % 12);
            const bool q_is_triad =
                (pc_q == triad_next[0]) || (pc_q == triad_next[1]) || (pc_q == triad_next[2]);
            if (!q_is_triad)
              continue;
            if (context.placed_notes != nullptr &&
                createsVerticalDissonance(*context.placed_notes, span.voice,
                                          static_cast<std::uint8_t>(q), t_next)) {
              continue;
            }
            // Parallel-perfect against placed voices at t_next, with
            // the candidate p (at current tick t) as the "prev" anchor.
            // Without this, the lookahead can claim a triad-tone
            // followup that the actual enumeration at t_next will
            // reject for parallel motion (observed: V2 58→60 across a
            // bar boundary forms a parallel P5 with V0 77→79).
            if (context.placed_notes != nullptr &&
                createsParallelPerfect(*context.placed_notes, span.voice,
                                       static_cast<std::uint8_t>(q), t_next,
                                       static_cast<std::uint8_t>(p), t)) {
              continue;
            }
            has_strong_followup = true;
            break;
          }
          if (!has_strong_followup)
            continue;
        }
      }

      float score = is_triad ? 0.8f : 0.4f;
      float independent_adjustment = 0.0f;
      // Prefer pitches close to the voice center. Quarter mode uses
      // the original 0.01 weight; Eighth mode uses 0.025 so the
      // tessitura anchor outweighs the per-position prev_pitch_local
      // cursor below — without this stronger anchor at finer
      // resolution, the cursor can drift away from voice_center
      // across consecutive weak-eighth picks, producing descending
      // cascades (see three-voice Eighth V2 at bar 0 mid-bar before
      // this calibration).
      const float center_weight = (span.subdivision == Subdivision::Eighth) ? 0.025f : 0.01f;
      score -= center_weight * static_cast<float>(std::abs(p - context.voice_center));
      // Prefer pitches close to the immediately-preceding pitch. Uses
      // `prev_pitch_local` (the per-position cursor) rather than
      // `context.prev_pitch` (fixed at span entry) so the distance term
      // tracks the just-committed pitch inside a multi-position span.
      // Without this, a candidate's prev-distance is measured against
      // the span-entry pitch even after several intra-span commits —
      // which biases later positions toward the span-entry pitch and
      // can favor a leap back toward span entry over holding the
      // recently-committed pitch.
      if (prev_pitch_local != 0) {
        score -= 0.02f * static_cast<float>(std::abs(p - prev_pitch_local));
      }
      // Soft penalty for weak-beat vertical dissonance. Strong beats
      // are already hard-rejected above. The penalty needs to exceed
      // the prev-distance gap to a reachable consonant alternative, so
      // 0.15 (roughly seven semitones of prev-distance) is enough to
      // flip the choice when one exists but small enough that a fully
      // unreachable consonant set still lets a dissonant pick through.
      if (!strong && vertically_dissonant) {
        score -= 0.15f;
        independent_adjustment -= 0.15f;
      }
      if (context.placed_notes != nullptr && have_parallel_anchor &&
          hasContraryMotion(*context.placed_notes, span.voice, static_cast<std::uint8_t>(p), t,
                            parallel_prev_pitch, parallel_prev_tick)) {
        score += 0.10f;
        independent_adjustment += 0.10f;
      }
      if (force_bass_cadence_pc) {
        score += 0.50f;
        independent_adjustment += 0.50f;
      }

      // P8 Picardy 3rd bias. The final picardy chord's identity is
      // carried by its major third (E natural in C major). Without
      // this bias the per-voice prev-distance heuristic pins every
      // voice on the chord root and the major third never sounds.
      // The bonus is small enough (0.05) to lose to a voice already
      // sitting on a chord tone close to its prev pitch, but large
      // enough to flip the choice when two chord tones tie on
      // prev-distance.
      if (chord.is_picardy) {
        const std::uint8_t major_third_pc = static_cast<std::uint8_t>((chord.root_pc + 4) % 12);
        if (static_cast<std::uint8_t>(pc) == major_third_pc) {
          score += 0.05f;
          independent_adjustment += 0.05f;
        }
      }

      // Quarter-mode unison-run penalty. When pre_prev and prev are
      // the same pitch (two consecutive unisons), a 3rd consecutive
      // unison candidate gets a 0.15 deduction so the broad step
      // bonus below can pull the search off the held pitch. Gated on
      // pre_prev != 0 to preserve the `QuarterSubdivisionKeepsHeldPitch`
      // regression (its single fixture has pre_prev=0, so 64 wins at
      // delta 0).
      if (span.subdivision == Subdivision::Quarter && !strong && pre_prev_pitch_local != 0 &&
          pre_prev_pitch_local == prev_pitch_local && p == static_cast<int>(prev_pitch_local)) {
        score -= 0.15f;
        independent_adjustment -= 0.15f;
      }

      // Quarter-mode broad step bonus. Applies on every weak position
      // (not just unison-break) so the search prefers conjunct
      // passing-tone motion over triad-tone leaps whenever both are
      // feasible. Safe because:
      //   - Leave-side enforcement guarantees the non-triad's next
      //     pitch is ≤2 from p, so we don't generate validator-
      //     failing notes.
      //   - 1-step lookahead before strong beats rejects non-triad p
      //     when no triad of the next chord fits ±2 of p AND survives
      //     vertical-dissonance against placed voices.
      //
      // Bonus size 0.42 is the regression-budget maximum: the
      // `QuarterSubdivisionKeepsHeldPitch` fixture asserts unison 64
      // wins at prev=64, center=64; that gives unison score 0.80 vs
      // step candidate 0.40 + S - 0.03, so S < 0.43 preserves the
      // assertion. At 0.42 the unison still wins by 0.01 when no
      // vertical pressure is present, but any vertical penalty on
      // the unison flips the choice to a step.
      //
      // Applies to all voices: bass voices benefit too because the
      // alternative (holding a chord tone that becomes dissonant
      // when V0 moves) carries weak-beat soft penalty anyway, so a
      // diatonic step neighbor often outranks the dissonant unison.
      if (scoring_config.step_bonus_enabled && span.subdivision == Subdivision::Quarter &&
          !strong && !is_triad && prev_pitch_local != 0) {
        const int delta = std::abs(p - static_cast<int>(prev_pitch_local));
        if (delta == 1 || delta == 2) {
          score += 0.42f;
          independent_adjustment += 0.42f;
        }
      }

      // Chromatic-passing-tone penalty (Quarter, weak-position,
      // non-triad). Mirrors the Eighth-mode chromatic penalty: the
      // step bonus above lifts any non-triad step, including
      // non-diatonic ones; the 0.05 penalty keeps chromatic motion
      // reachable when no diatonic alternative survives the cascade
      // but lets the diatonic neighbor win every tie.
      const int melodic_motion = prev_pitch_local == 0 ? 0 : p - static_cast<int>(prev_pitch_local);
      if (span.subdivision == Subdivision::Quarter && !strong && !is_triad &&
          !rule_helpers::isContextualScalePitch(static_cast<std::uint8_t>(p), harmonic_plan, t,
                                                melodic_motion)) {
        score -= 0.05f;
        independent_adjustment -= 0.05f;
      }

      if (strict_harmonic_support &&
          trailingPitchRunLength(recent_pitches, static_cast<std::uint8_t>(p)) >= 3) {
        score -= 0.60f;
        independent_adjustment -= 0.60f;
      }

      // Eighth-note motion bias (two parts, only at Subdivision::Eighth):
      //
      // (a) Penalize weak-position unison. Without this, the score
      //     function picks the previous pitch whenever it is a triad
      //     tone near voice_center — the prev-distance penalty alone
      //     is 0 at delta 0. Bach eighth-note counterlines almost
      //     never sit on the same eighth twice in a row.
      //
      // (b) Bonus non-triad step approach. The triad bonus (0.40 gap
      //     between triad and non-triad starting scores) otherwise
      //     dominates, biasing eighth motion toward chord-tone leaps
      //     (m3, P4) rather than the stepwise passing motion that
      //     defines Bach idiom. A 0.40 bonus on non-triad candidates
      //     within a whole step of prev lifts genuine passing tones
      //     above triad-tone leaps of comparable distance.
      //
      // Both branches measure delta against `prev_pitch_local` (the
      // per-position cursor that advances on every commit), not
      // `context.prev_pitch` (which is fixed at span entry). Using the
      // local cursor is what lets the rule see position-to-position
      // motion inside a span; the span-entry value alone would mark
      // two consecutive eighths as non-unison whenever the second
      // happens to equal the span's entry pitch.
      //
      // Quarter subdivision keeps the original scoring: held pitches
      // at quarter resolution are musically valid (suspensions, pedal
      // tones, etc.), and quarter-level triad oscillation is fine.
      if (scoring_config.step_bonus_enabled && span.subdivision == Subdivision::Eighth && !strong &&
          prev_pitch_local != 0) {
        const int delta = std::abs(p - static_cast<int>(prev_pitch_local));
        if (delta == 0) {
          score -= 0.15f;
          independent_adjustment -= 0.15f;
        } else if (!is_triad && (delta == 1 || delta == 2)) {
          score += 0.40f;
          independent_adjustment += 0.40f;
        }
      }

      // Chromatic-passing-tone penalty (Eighth-only, weak-position).
      // The step bonus above lifts any non-triad pitch within a whole
      // step of prev; without further bias, a chromatic candidate
      // enumerated before a diatonic alternative wins on equal score
      // (loop iterates pitch ascending and uses strict `>`). The 0.05
      // penalty keeps chromatic motion reachable when the rule cascade
      // leaves no diatonic option, but lets diatonic steps win every
      // tie.
      if (span.subdivision == Subdivision::Eighth && !strong && !is_triad &&
          !rule_helpers::isContextualScalePitch(static_cast<std::uint8_t>(p), harmonic_plan, t,
                                                melodic_motion)) {
        score -= 0.05f;
        independent_adjustment -= 0.05f;
      }

      RuleIdMask rules = 0;
      if (is_triad)
        rules |= ruleBitMask(RuleBit::ChordTone);
      applyP7Bits(rules, chord, static_cast<std::uint8_t>(pc), is_triad);
      applyP8Bits(rules, harmonic_plan, chord, static_cast<std::uint8_t>(pc), is_triad);
      // P10 invertibility confirmation. Set InvertibleAt8va when we have
      // placed context AND this candidate does NOT form a perfect 4th
      // (class 5) on a strong beat with the sounding upper-ADJACENT
      // voice (V_{span.voice-1}). A strong-beat 4th in the upper pair
      // inverts to a 5th, so a clean candidate is one that avoids it.
      // The bit is a confirming check: the Composer already prefers
      // consonant strong-beat verticals, so this accrues on most
      // candidates rather than constraining selection.
      //
      // Scope: only the upper-adjacent pairs the P7 spacing / P10
      // validator rules cover (top N-2 pairs). The bottom pair
      // (V_{N-2}, V_{N-1}) is excluded, so the bass voice (the last
      // voice index) never lights the bit against its lower-adjacent
      // neighbor.
      const bool is_bottom_voice = context.num_voices > 0 && span.voice + 1 >= context.num_voices;
      if (context.placed_notes != nullptr && span.voice > 0 && !is_bottom_voice) {
        bool forms_strong_fourth = false;
        if (strong) {
          // Sounding pitch of the immediately-higher voice (V-1).
          const VoiceId upper_adjacent = static_cast<VoiceId>(span.voice - 1);
          std::uint8_t upper_pitch = 0;
          for (const auto& placed : *context.placed_notes) {
            if (placed.voice != upper_adjacent)
              continue;
            if (placed.start_tick > t)
              continue;
            if (t >= placed.start_tick + placed.duration)
              continue;
            upper_pitch = placed.pitch;
          }
          if (upper_pitch != 0) {
            const int cls = std::abs(static_cast<int>(upper_pitch) - p) % 12;
            forms_strong_fourth = (cls == 5);
          }
        }
        if (!forms_strong_fourth) {
          rules |= ruleBitMask(RuleBit::InvertibleAt8va);
        }
      }
      if (strong && is_triad)
        rules |= ruleBitMask(RuleBit::StrongBeatConsonance);
      if (context.prev_pitch > 0 && std::abs(p - context.prev_pitch) <= 4) {
        rules |= ruleBitMask(RuleBit::SmallStep);
      }
      if (context.placed_notes != nullptr) {
        rules |= ruleBitMask(RuleBit::ParallelPerfectChecked);
        rules |= ruleBitMask(RuleBit::HiddenParallelChecked);
        rules |= ruleBitMask(RuleBit::VoiceCrossingChecked);
        rules |= ruleBitMask(RuleBit::CrossRelationChecked);
        // rule[7] = VerticalConsonanceChecked. Marks the candidate as
        // consonant against all currently-placed voices at this tick.
        // Strong beats are guaranteed (the rejection above would have
        // skipped a dissonant `p`); weak beats only set the bit when
        // they happen to avoid dissonance (i.e. the soft penalty did
        // not have to apply).
        if (!vertically_dissonant) {
          rules |= ruleBitMask(RuleBit::VerticalConsonanceChecked);
        }
      }
      if (pre_prev_pitch_local != 0 && prev_pitch_local != 0) {
        rules |= ruleBitMask(RuleBit::LeapResolutionChecked);
      }
      if (!strong && prev_pitch_local != 0) {
        rules |= ruleBitMask(RuleBit::WeakBeatPassingChecked);
      }
      if (prev_pitch_local != 0 && rule_helpers::isContextualLeadingTone(
                                       prev_pitch_local, harmonic_plan, parallel_prev_tick)) {
        rules |= ruleBitMask(RuleBit::LeadingToneResolved);
      }
      if (cadence_cell != nullptr) {
        rules |= ruleBitMask(RuleBit::CadenceCellCommitted);
        if (t == cadence_cell->cadence_tick) {
          rules |= ruleBitMask(RuleBit::CadenceVoiceLeadingChecked);
        }
      }

      const float shadow_score =
          computeShadowScore(p, is_triad, context, recent_pitches, pre_prev_pitch_local,
                             prev_pitch_local, harmonic_plan.is_minor, true, scoring_config);
      const float shadow_score_without_markov =
          computeShadowScore(p, is_triad, context, recent_pitches, pre_prev_pitch_local,
                             prev_pitch_local, harmonic_plan.is_minor, false, scoring_config);

      if (shadow_score > best_shadow_winner_score) {
        best_shadow_winner_score = shadow_score;
        best_shadow_pitch = p;
      }
      if (shadow_score_without_markov > best_shadow_winner_score_without_markov) {
        best_shadow_winner_score_without_markov = shadow_score_without_markov;
        best_shadow_pitch_without_markov = p;
      }

      const float local_adjustment_weight =
          context.placed_notes != nullptr ? scoring_config.local_rule_adjustment_weight : 1.0f;
      const float selection_score =
          scoring_config.use_shadow_selection
              ? shadow_score + (local_adjustment_weight * independent_adjustment)
              : score;
      if (selection_score > best_selection_score) {
        best_selection_score = selection_score;
        best_score = selection_score;
        best_shadow_score = shadow_score;
        best_pitch = p;
        best_rules = rules;
      }
    }

    if (best_pitch < 0) {
      if (force_cadence_pc) {
        int fallback_pitch = -1;
        int fallback_distance = 10000;
        const int fallback_lo =
            force_bass_cadence_pc ? (context.voice_center - 19) : (context.voice_center - 7);
        const int fallback_hi = context.voice_center + 12;
        for (int p = fallback_lo; p <= fallback_hi; ++p) {
          if (p < 0 || p > 127)
            continue;
          if (static_cast<std::uint8_t>(p % 12) != forced_cadence_pc)
            continue;
          const int cadence_center =
              force_bass_cadence_pc ? (context.voice_center - 12) : context.voice_center;
          const int distance = std::abs(p - cadence_center);
          if (distance < fallback_distance) {
            fallback_distance = distance;
            fallback_pitch = p;
          }
        }
        if (fallback_pitch >= 0) {
          best_pitch = fallback_pitch;
          best_score = 0.0f;
          best_selection_score = 0.0f;
          best_shadow_score = 0.0f;
          best_rules = ruleBitMask(RuleBit::CadenceCellCommitted);
          if (t == cadence_cell->cadence_tick) {
            best_rules |= ruleBitMask(RuleBit::CadenceVoiceLeadingChecked);
          }
          const std::uint8_t pc = static_cast<std::uint8_t>(best_pitch % 12);
          if (pc == triad[0] || pc == triad[1] || pc == triad[2]) {
            best_rules |= ruleBitMask(RuleBit::ChordTone);
          }
        }
      }
    }

    if (best_pitch < 0 && prev_pitch_local != 0 &&
        rule_helpers::isContextualLeadingTone(prev_pitch_local, harmonic_plan,
                                              parallel_prev_tick)) {
      for (int p = static_cast<int>(prev_pitch_local) + 1;
           p <= static_cast<int>(prev_pitch_local) + 2; ++p) {
        if (p < 0 || p > 127)
          continue;
        if (!resolvesLeadingTone(prev_pitch_local, p, harmonic_plan, parallel_prev_tick))
          continue;
        const std::uint8_t pc = static_cast<std::uint8_t>(p % 12);
        const bool is_triad_fallback = (pc == triad[0]) || (pc == triad[1]) || (pc == triad[2]);
        const bool fallback_strong = isStructuralAccent(harmonic_plan, t);
        if (fallback_strong && !is_triad_fallback)
          continue;
        if (!fallback_strong && !is_triad_fallback) {
          const Tick t_next = t + stride;
          bool leaves_by_step = false;
          if (context.placed_notes != nullptr) {
            const std::uint8_t fixed_next =
                sameVoiceStartingAt(*context.placed_notes, span.voice, t_next);
            if (fixed_next != 0 && std::abs(p - static_cast<int>(fixed_next)) <= 2) {
              leaves_by_step = true;
            }
          }
          if (!leaves_by_step)
            continue;
        }
        if (context.placed_notes != nullptr &&
            createsVoiceCrossing(*context.placed_notes, span.voice, static_cast<std::uint8_t>(p),
                                 t)) {
          continue;
        }
        if (context.placed_notes != nullptr &&
            createsVerticalDissonance(*context.placed_notes, span.voice,
                                      static_cast<std::uint8_t>(p), t)) {
          continue;
        }
        if (context.placed_notes != nullptr && have_parallel_anchor &&
            createsParallelPerfect(*context.placed_notes, span.voice, static_cast<std::uint8_t>(p),
                                   t, parallel_prev_pitch, parallel_prev_tick)) {
          continue;
        }
        if (context.placed_notes != nullptr && have_parallel_anchor &&
            rule_helpers::createsParallelOctave(*context.placed_notes, span.voice,
                                                static_cast<std::uint8_t>(p), t,
                                                parallel_prev_pitch, parallel_prev_tick)) {
          continue;
        }
        if (context.placed_notes != nullptr && have_parallel_anchor &&
            createsHiddenParallelPerfect(*context.placed_notes, span.voice,
                                         static_cast<std::uint8_t>(p), t, parallel_prev_pitch,
                                         parallel_prev_tick)) {
          continue;
        }
        if (context.placed_notes != nullptr &&
            createsCrossRelation(*context.placed_notes, span.voice, static_cast<std::uint8_t>(p),
                                 t)) {
          continue;
        }
        best_pitch = p;
        best_score = 0.0f;
        best_selection_score = 0.0f;
        best_shadow_score = 0.0f;
        best_rules = (ruleBitMask(RuleBit::LeadingToneResolved));
        if (is_triad_fallback) {
          best_rules |= ruleBitMask(RuleBit::ChordTone);
        }
        break;
      }
    }

    if (best_pitch < 0) {
      // No admissible candidate at this position: a silent hole. Per the
      // no-fallback principle we emit nothing here; report the saturation
      // so the caller can escalate it to ValidationStatus::FailedSeed
      // instead of letting the hole pass silently as success.
      if (saturated_positions != nullptr)
        ++*saturated_positions;
      continue;
    }

    Candidate c;
    c.start_tick = t;
    c.duration = stride;
    c.pitch = static_cast<std::uint8_t>(best_pitch);
    c.score = best_score;
    c.shadow_score = best_shadow_score;
    c.shadow_winning_pitch =
        static_cast<std::uint8_t>(best_shadow_pitch >= 0 ? best_shadow_pitch : best_pitch);
    c.shadow_winning_pitch_without_markov = static_cast<std::uint8_t>(
        best_shadow_pitch_without_markov >= 0 ? best_shadow_pitch_without_markov : best_pitch);
    c.satisfied_rules = best_rules;
    out.push_back(c);

    parallel_prev_pitch = c.pitch;
    parallel_prev_tick = t;
    have_parallel_anchor = true;
    pre_prev_pitch_local = prev_pitch_local;
    prev_pitch_local = c.pitch;
    recent_pitches.push_back(c.pitch);
    // The picked candidate is a triad-tone iff bit 0 of satisfied_rules
    // is set (rule[0] = ChordTone). Use that to update the leave-side
    // tracker — non-triad commits require the next position to be
    // within ±2.
    prev_was_pt_local = ((best_rules & (ruleBitMask(RuleBit::ChordTone))) == 0);
  }
  return out;
}

}  // namespace bach::composer
