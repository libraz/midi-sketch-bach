#include "composer/composer.h"

#include <algorithm>
#include <array>
#include <cstdint>

#include "composer/candidate_search.h"
#include "composer/renderer.h"
#include "composer/validator.h"

namespace bach::composer {

bool isComposerLibLinked() {
  return true;
}

namespace {

const ChordEvent& activeChord(const HarmonicPlan& plan, Tick at) {
  const ChordEvent* current = &plan.chords.front();
  for (const auto& chord : plan.chords) {
    if (chord.start_tick <= at) {
      current = &chord;
    } else {
      break;
    }
  }
  return *current;
}

std::array<std::uint8_t, 3> triadPC(const ChordEvent& chord) {
  std::uint8_t third = 4;
  std::uint8_t fifth = 7;
  switch (chord.quality) {
    case ChordQuality::Major:
    case ChordQuality::Major7:
    case ChordQuality::Dominant7:
      third = 4;
      fifth = 7;
      break;
    case ChordQuality::Minor:
    case ChordQuality::Minor7:
      third = 3;
      fifth = 7;
      break;
    case ChordQuality::Diminished:
    case ChordQuality::HalfDiminished7:
    case ChordQuality::Diminished7:
      third = 3;
      fifth = 6;
      break;
    case ChordQuality::Augmented:
      third = 4;
      fifth = 8;
      break;
  }
  return {
      static_cast<std::uint8_t>(chord.root_pc % 12),
      static_cast<std::uint8_t>((chord.root_pc + third) % 12),
      static_cast<std::uint8_t>((chord.root_pc + fifth) % 12),
  };
}

struct VoiceCursor {
  std::uint8_t prev_pitch = 0;
  std::uint8_t pre_prev_pitch = 0;
  Tick prev_end_tick = 0;
  bool prev_was_passing_tone = false;
};

// Seed a CandidateContext from already-committed notes. Phase 4 added
// fugue exposition layouts where V1's AnswerCarrier (bars 4-7) is placed
// in Pass 1 before V1's counterline (bars 0-3) is processed in Pass 2.
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
    const auto triad = triadPC(chord);
    const auto pc = static_cast<std::uint8_t>(last->pitch % 12);
    const bool is_chord_tone = (pc == triad[0] || pc == triad[1] || pc == triad[2]);
    c.prev_was_passing_tone = !is_chord_tone;
  }
  if (second != nullptr) {
    c.pre_prev_pitch = second->pitch;
  }
  return c;
}

bool isCarrierIntent(VoiceIntent intent) {
  return intent == VoiceIntent::SubjectCarrier || intent == VoiceIntent::AnswerCarrier ||
         intent == VoiceIntent::SuspensionCarrier || intent == VoiceIntent::Episode ||
         intent == VoiceIntent::CountersubjectCarrier;
}

}  // namespace

ComposeResult Composer::run(const Material& material, const HarmonicPlan& harmonic_plan,
                            const VoicePlan& voice_plan) const {
  ComposeResult result;

  CandidateSearch search;
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
    // Hand the search a view of every note committed so far so it can
    // run the vertical (parallel-perfect, voice-crossing) checks.
    ctx.placed_notes = &result.notes;
    ctx.num_voices = voice_plan.num_voices;

    const auto candidates = search.enumerate(span, harmonic_plan, material, ctx);

    // Phase 2 commit policy: take all candidates in order. Each is a
    // separate emitted note (search enumerates note positions inside a
    // span, not alternatives for one position).
    for (const auto& cand : candidates) {
      NoteEvent note;
      note.start_tick = cand.start_tick;
      note.duration = cand.duration;
      note.pitch = cand.pitch;
      note.voice = span.voice;
      note.velocity = 80;
      result.notes.push_back(note);

      NoteProvenance prov;
      prov.span_id = span.id;
      prov.voice_intent = span.intent;
      prov.candidate_score = cand.score;
      prov.source = isCarrierIntent(span.intent) ? NoteSource::Material : NoteSource::Compose;
      prov.satisfied_rules = cand.satisfied_rules;
      prov.rejected_alternatives = 0;  // Phase 2 has no alternative ranking
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
  // Pass 2: Compose spans. seedCursor() re-derives the per-voice
  // previous-pitch chain from result.notes for every span so the cursor
  // stays correct even when a voice's Carrier span sits in the future
  // (Phase 4: V1 counterline bars 0-3 follows V1 AnswerCarrier bars 4-7
  // in placement order but precedes it in time).
  for (const auto& span : voice_plan.spans) {
    if (!isCarrierIntent(span.intent))
      processSpan(span);
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

  Validator validator;
  result.validation = validator.validate(result.notes, result.provenance, harmonic_plan, material);

  Renderer renderer;
  result.tracks = renderer.render(result.notes);

  return result;
}

}  // namespace bach::composer
