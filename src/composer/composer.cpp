#include "composer/composer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

#include "composer/candidate_search.h"
#include "composer/nct_detector.h"
#include "composer/renderer.h"
#include "composer/rule_helpers.h"
#include "composer/validator.h"

namespace bach::composer {

bool isComposerLibLinked() {
  return true;
}

namespace {

// Harmonic primitives are shared with candidate_search.cpp via rule_helpers.
using rule_helpers::activeChord;
using rule_helpers::triadPitchClasses;

// Read-only inputs a post-pass may consult. Bundled so every pass shares one
// signature and the ordered list below documents the pipeline at one site.
struct PostPassContext {
  const Material& material;
  const HarmonicPlan& harmonic_plan;
};

// A note-list post-pass run after candidate placement + sorting and before
// validation.
//
// Contract (relied on by provenance index alignment): a PostPass MUST NOT
// change the count, order, pitch, or onset of `notes`; it may set note
// velocity and OR provenance bits only. `notes` and `provenance` are index-
// aligned and must stay so.
using PostPass = void (*)(std::vector<NoteEvent>& notes, std::vector<NoteProvenance>& provenance,
                          const PostPassContext& ctx);

// Placeholder onset velocity stamped at placement time. The texture/expression
// post-pass (applyTextureExpression) overwrites this with the Affekt-driven
// phrase-arch value whenever the Material carries a texture plan; fixtures
// without one keep this organ-plausible default.
constexpr std::uint8_t kDefaultVelocity = 80;

struct VoiceCursor {
  std::uint8_t prev_pitch = 0;
  std::uint8_t pre_prev_pitch = 0;
  Tick prev_end_tick = 0;
  bool prev_was_passing_tone = false;
};

// Seed a CandidateContext from already-committed notes. Fugue exposition
// layouts can place V1's AnswerCarrier (bars 4-7) in Pass 1 before V1's
// counterline (bars 0-3) is processed in Pass 2.
// A per-voice scalar cursor would carry stale values across that jump,
// so each span re-derives its previous-pitch chain by scanning the
// in-progress notes list for the latest entries strictly before its
// start_tick.
VoiceCursor seedCursor(VoiceId voice, Tick span_start, const std::vector<NoteEvent>& placed,
                       const HarmonicPlan& harmonic_plan) {
  VoiceCursor c;
  const NoteEvent* last = nullptr;
  const NoteEvent* second = nullptr;
  for (const auto& n : placed) {
    if (n.voice != voice)
      continue;
    if (n.start_tick >= span_start)
      continue;
    if (last == nullptr || n.start_tick > last->start_tick) {
      second = last;
      last = &n;
    } else if (second == nullptr || n.start_tick > second->start_tick) {
      second = &n;
    }
  }
  if (last != nullptr) {
    c.prev_pitch = last->pitch;
    c.prev_end_tick = last->start_tick + last->duration;
    const ChordEvent& chord = activeChord(harmonic_plan, last->start_tick);
    const auto triad = triadPitchClasses(chord);
    const auto pc = static_cast<std::uint8_t>(last->pitch % 12);
    const bool is_chord_tone = (pc == triad[0] || pc == triad[1] || pc == triad[2]);
    c.prev_was_passing_tone = !is_chord_tone;
  }
  if (second != nullptr) {
    c.pre_prev_pitch = second->pitch;
  }
  return c;
}

MelodicCorpusCategory inferMelodicCategory(const Material& material) {
  if (!material.arpeggio_template.notes.empty() || !material.variations.empty() ||
      !material.ground_bass.empty()) {
    return MelodicCorpusCategory::SoloString;
  }
  if (!material.cantus_firmus.empty() || !material.cf_embellished.empty()) {
    return MelodicCorpusCategory::Chorale;
  }
  return MelodicCorpusCategory::Organ;
}

// Texture / instrument / expression post-pass.
//
// Runs after candidate placement and sorting, before validation. It reads
// the render-time attributes in Material::texture_plan and (a) replaces
// each note's velocity with an Affekt-driven phrase-arch value and (b) ORs
// the four texture/expression provenance bits onto the matching notes. It
// never changes a note's pitch or onset, so carrier replay and scored content
// are unaffected; a default-constructed TexturePlan makes the whole pass a
// no-op (fixtures without a texture plan are untouched).
void applyTextureExpression(std::vector<NoteEvent>& notes, std::vector<NoteProvenance>& provenance,
                            const PostPassContext& ctx) {
  const TexturePlan& plan = ctx.material.texture_plan;
  const bool any_attribute = !plan.voice_ranges.empty() || !plan.manual_assignments.empty() ||
                             !plan.articulations.empty() || plan.affekt_curve_active;
  if (!any_attribute)
    return;

  // Piece extent for the velocity arch (peak at the temporal center,
  // tapering toward the opening and the final cadence).
  Tick total = 1;
  for (const auto& n : notes) {
    total = std::max(total, n.start_tick + n.duration);
  }
  // Character-dependent base velocity (documentary SubjectCharacter cast).
  // Kept near the organ default 80 so even a velocity-sensitive scorer
  // sees an organ-plausible dynamic.
  const int base = 76 + (plan.affekt_character % 4) * 2;  // 76..82
  constexpr int kArchAmplitude = 10;

  for (std::size_t i = 0; i < notes.size(); ++i) {
    NoteEvent& note = notes[i];
    RuleIdMask& rules = provenance[i].satisfied_rules;

    for (const auto& range : plan.voice_ranges) {
      if (range.voice == note.voice) {
        if (note.pitch >= range.lo && note.pitch <= range.hi) {
          rules |= ruleBitMask(RuleBit::VoiceRangeKept);
        }
        break;
      }
    }
    for (const auto& routing : plan.manual_assignments) {
      if (routing.voice == note.voice) {
        rules |= ruleBitMask(RuleBit::ManualAssigned);
        break;
      }
    }
    for (const auto& art : plan.articulations) {
      if (art.voice == note.voice && note.start_tick >= art.start_tick &&
          note.start_tick < art.end_tick) {
        rules |= ruleBitMask(RuleBit::ArticulationApplied);
        break;
      }
    }
    if (plan.affekt_curve_active) {
      // Triangular arch in [0, 1]: 1.0 at the center, 0.0 at the edges.
      const double center = static_cast<double>(total) / 2.0;
      const double dist =
          center > 0.0 ? std::abs(static_cast<double>(note.start_tick) - center) / center : 0.0;
      const double arch = 1.0 - dist;  // peak at center
      int velocity = base + static_cast<int>(arch * kArchAmplitude);
      velocity = std::max(1, std::min(127, velocity));
      note.velocity = static_cast<std::uint8_t>(velocity);
      rules |= ruleBitMask(RuleBit::AffektCurveApplied);
    }
  }
}

// Non-chord-tone figure detection post-pass. Groups the final
// sorted notes by voice, runs the four nct_detector figures on each
// voice's time-ordered list, and ORs the matching RuleBit onto the
// NCT note via an index map back to the global note array. Pure: never
// changes pitch or onset (same no-op safety as applyTextureExpression).
void applyNctDetection(std::vector<NoteEvent>& notes, std::vector<NoteProvenance>& provenance,
                       const PostPassContext& ctx) {
  const HarmonicPlan& harmonic_plan = ctx.harmonic_plan;
  if (notes.empty() || harmonic_plan.chords.empty())
    return;

  // Distinct voices present in the (already sorted) note list.
  std::vector<VoiceId> voices;
  for (const auto& note : notes) {
    if (std::find(voices.begin(), voices.end(), note.voice) == voices.end()) {
      voices.push_back(note.voice);
    }
  }

  for (VoiceId voice : voices) {
    // Build this voice's time-ordered single-voice list plus a parallel
    // map back to the index in `notes`. `notes` is already sorted by
    // (start_tick, voice), so collecting in order preserves time order.
    std::vector<MaterialNote> voice_notes;
    std::vector<std::size_t> index_map;
    for (std::size_t idx = 0; idx < notes.size(); ++idx) {
      if (notes[idx].voice != voice)
        continue;
      MaterialNote mnote;
      mnote.start_tick = notes[idx].start_tick;
      mnote.duration = notes[idx].duration;
      mnote.pitch = notes[idx].pitch;
      voice_notes.push_back(mnote);
      index_map.push_back(idx);
    }

    const auto stamp = [&](const std::vector<nct_detector::NctHit>& hits) {
      for (const auto& hit : hits) {
        if (hit.nct_index >= index_map.size())
          continue;
        RuleBit bit = RuleBit::CambiataDetected;
        switch (hit.figure) {
          case nct_detector::NctFigure::Cambiata:
            bit = RuleBit::CambiataDetected;
            break;
          case nct_detector::NctFigure::Echappee:
            bit = RuleBit::EchappeeDetected;
            break;
          case nct_detector::NctFigure::Anticipation:
            bit = RuleBit::AnticipationDetected;
            break;
          case nct_detector::NctFigure::NotaCambiata:
            bit = RuleBit::NotaCambiataDetected;
            break;
        }
        // The NCT bits document authored NctCarrier figures, not incidental
        // melodic shapes in free counterpoint. Restrict the stamp to notes the
        // planner declared as NCT carriers so the post-pass is a no-op on
        // phases without NCT figures (surrounding non-carrier notes still
        // supply the melodic window the detectors need).
        const std::size_t gidx = index_map[hit.nct_index];
        if (provenance[gidx].voice_intent != VoiceIntent::NctCarrier)
          continue;
        provenance[gidx].satisfied_rules |= ruleBitMask(bit);
      }
    };

    stamp(nct_detector::detectCambiata(voice_notes, harmonic_plan));
    stamp(nct_detector::detectEchappee(voice_notes, harmonic_plan));
    stamp(nct_detector::detectAnticipation(voice_notes, harmonic_plan));
    stamp(nct_detector::detectNotaCambiata(voice_notes, harmonic_plan));
  }
}

}  // namespace

ComposeResult Composer::run(const Material& material, const HarmonicPlan& harmonic_plan,
                            const VoicePlan& voice_plan) const {
  ComposeResult result;

  CandidateSearch search;
  // Count of Compose positions that exhausted with no admissible candidate
  // (silent holes) across every span pass. Escalated to FailedSeed below.
  std::size_t saturated_total = 0;
  // Voice center heuristic: voice 0 = soprano area, voice 1 = alto, etc.
  // 4-voice Bach ranges cluster roughly at 72 / 64 / 57 / 48 MIDI.
  static constexpr std::uint8_t kVoiceCenter[] = {72, 64, 57, 48};

  auto processSpan = [&](const Span& span) {
    CandidateContext ctx;
    const VoiceCursor cur = seedCursor(span.voice, span.start_tick, result.notes, harmonic_plan);
    ctx.prev_pitch = cur.prev_pitch;
    ctx.pre_prev_pitch = cur.pre_prev_pitch;
    ctx.prev_end_tick = cur.prev_end_tick;
    ctx.prev_was_passing_tone = cur.prev_was_passing_tone;
    if (span.voice < sizeof(kVoiceCenter) / sizeof(kVoiceCenter[0])) {
      ctx.voice_center = kVoiceCenter[span.voice];
    }
    // A non-zero per-span override wins over the global per-voice default.
    // Lets a planner raise the tessitura anchor where a voice's default
    // center sits below the active spacing-valid band (see Span::voice_center).
    if (span.voice_center != 0) {
      ctx.voice_center = span.voice_center;
    }
    // Hand the search a view of every note committed so far so it can
    // run the vertical (parallel-perfect, voice-crossing) checks.
    ctx.placed_notes = &result.notes;
    ctx.num_voices = voice_plan.num_voices;
    ctx.melodic_category = inferMelodicCategory(material);

    const auto candidates = search.enumerate(span, harmonic_plan, material, ctx, &saturated_total);

    // Commit policy: take all candidates in order. Each is a
    // separate emitted note (search enumerates note positions inside a
    // span, not alternatives for one position).
    for (const auto& cand : candidates) {
      NoteEvent note;
      note.start_tick = cand.start_tick;
      note.duration = cand.duration;
      note.pitch = cand.pitch;
      note.voice = span.voice;
      note.velocity = kDefaultVelocity;
      result.notes.push_back(note);

      NoteProvenance prov;
      prov.span_id = span.id;
      prov.voice_intent = span.intent;
      prov.candidate_score = cand.score;
      prov.shadow_score = cand.shadow_score;
      prov.shadow_winning_pitch = cand.shadow_winning_pitch;
      prov.shadow_winning_pitch_without_markov = cand.shadow_winning_pitch_without_markov;
      prov.source = isCarrierIntent(span.intent) ? NoteSource::Material : NoteSource::Compose;
      if (prov.source == NoteSource::Material) {
        prov.has_authored_note = true;
        prov.authored_start_tick = note.start_tick;
        prov.authored_duration = note.duration;
        prov.authored_pitch = note.pitch;
      }
      prov.satisfied_rules = cand.satisfied_rules;
      prov.rejected_alternatives = 0;  // commit policy does not rank alternatives
      result.provenance.push_back(prov);
    }
  };

  // Pass 1: Carrier spans (SubjectCarrier, AnswerCarrier) replay Material
  // verbatim and must be in place before any Compose span depends on them
  // for the vertical-perfect check or for cursor seeding. Carriers do not
  // depend on each other, so their relative order is the VoicePlan-stored
  // order.
  for (const auto& span : voice_plan.spans) {
    if (isCarrierIntent(span.intent))
      processSpan(span);
  }
  // Pass 2: Compose spans, in (start_tick, voice) order rather than
  // VoicePlan storage order. Storage order groups spans by voice, so a
  // late-voice span early in time would otherwise be searched after an
  // early-voice span late in time — the searcher's parallel/hidden
  // filters then see no other-voice pitch at the previous union tick
  // (op_prev == 0) and skip, letting the Validator reject the seed
  // after the fact. Time ordering guarantees every span is searched
  // with all temporally-earlier notes already placed. seedCursor()
  // re-derives the per-voice previous-pitch chain from result.notes
  // for every span, so it is order-robust either way.
  std::vector<const Span*> compose_spans;
  for (const auto& span : voice_plan.spans) {
    if (!isCarrierIntent(span.intent))
      compose_spans.push_back(&span);
  }
  std::stable_sort(compose_spans.begin(), compose_spans.end(), [](const Span* a, const Span* b) {
    if (a->start_tick != b->start_tick)
      return a->start_tick < b->start_tick;
    return a->voice < b->voice;
  });
  for (const Span* span : compose_spans) {
    processSpan(*span);
  }

  // Sort notes by (start_tick, voice) for deterministic downstream order.
  // Provenance must stay aligned with notes, so sort indices in tandem.
  std::vector<std::size_t> order(result.notes.size());
  for (std::size_t i = 0; i < order.size(); ++i)
    order[i] = i;
  std::sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
    const auto& na = result.notes[a];
    const auto& nb = result.notes[b];
    if (na.start_tick != nb.start_tick)
      return na.start_tick < nb.start_tick;
    return na.voice < nb.voice;
  });
  std::vector<NoteEvent> sorted_notes;
  std::vector<NoteProvenance> sorted_prov;
  sorted_notes.reserve(result.notes.size());
  sorted_prov.reserve(result.provenance.size());
  for (std::size_t idx : order) {
    sorted_notes.push_back(result.notes[idx]);
    sorted_prov.push_back(result.provenance[idx]);
  }
  result.notes = std::move(sorted_notes);
  result.provenance = std::move(sorted_prov);

  // Post-passes run after sort and before validation, in this fixed order.
  // Each obeys the PostPass contract (no count/order/pitch/onset change;
  // velocity + provenance bits only), so provenance stays index-aligned with
  // notes. Adding a future pass is one array entry.
  //   [0] texture / instrument / expression (velocity curve + bits).
  //   [1] non-chord-tone figure bit stamping on the sorted note list.
  static constexpr PostPass kPostPasses[] = {applyTextureExpression, applyNctDetection};
  const PostPassContext post_ctx{material, harmonic_plan};
  for (PostPass pass : kPostPasses) {
    pass(result.notes, result.provenance, post_ctx);
  }

  Validator validator;
  result.validation = validator.validate(result.notes, result.provenance, harmonic_plan, material,
                                         ValidationScope::Generation);

  // Visibility surface for the no-fallback principle. A Compose position that
  // exhausted all candidates emitted no note (a rest) instead of a default
  // pitch. Carrier-only fixtures never reach this path, so the count is zero
  // unless a span was opened to the candidate search (e.g. via the
  // free-counterpoint opt-in, ComposeRequest::enable_free_counterpoint).
  // Record the count so the hole is no longer invisible; resting is the correct
  // response to "no consonant candidate" (better a rest than a wrong note), so
  // this is NOT escalated to a validation failure. Callers that want exhaustion
  // to be fatal can gate on this count explicitly.
  result.saturated_positions = saturated_total;

  Renderer renderer;
  result.tracks = renderer.render(result.notes);

  return result;
}

}  // namespace bach::composer
