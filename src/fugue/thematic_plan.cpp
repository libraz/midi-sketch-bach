// Thematic planning layer for fugue generation.

#include "fugue/thematic_plan.h"

#include <algorithm>

#include "core/basic_types.h"

namespace bach {

namespace {

std::vector<NoteEvent> normalizedHead(const std::vector<NoteEvent>& notes, size_t count) {
  if (notes.empty() || count == 0)
    return {};
  size_t actual = std::min(count, notes.size());
  std::vector<NoteEvent> result(notes.begin(), notes.begin() + static_cast<int>(actual));
  Tick origin = result.front().start_tick;
  for (auto& note : result) {
    note.start_tick = (note.start_tick >= origin) ? note.start_tick - origin : 0;
  }
  return result;
}

std::vector<NoteEvent> normalizedTail(const std::vector<NoteEvent>& notes, size_t count) {
  if (notes.empty() || count == 0)
    return {};
  size_t actual = std::min(count, notes.size());
  std::vector<NoteEvent> result(notes.end() - static_cast<int>(actual), notes.end());
  Tick origin = result.front().start_tick;
  for (auto& note : result) {
    note.start_tick = (note.start_tick >= origin) ? note.start_tick - origin : 0;
  }
  return result;
}

Tick totalDuration(const std::vector<NoteEvent>& notes) {
  Tick end_tick = 0;
  for (const auto& note : notes) {
    end_tick = std::max(end_tick, note.start_tick + note.duration);
  }
  return end_tick;
}

PatternCandidate makeCandidate(PatternKind kind, VoiceIntent intent, HarmonicRole role,
                               ThematicProtectionLevel protection, std::vector<NoteEvent> notes,
                               float identity, float replacement_cost,
                               ViolationBudget budget = {}) {
  PatternCandidate candidate;
  candidate.kind = kind;
  candidate.intent = intent;
  candidate.harmonic_role = role;
  candidate.protection = protection;
  candidate.notes = std::move(notes);
  candidate.motif_identity = identity;
  candidate.markov_score = 0.0f;
  candidate.counterpoint_score = 0.0f;
  candidate.replacement_cost = replacement_cost;
  candidate.budget = budget;
  return candidate;
}

ViolationBudget subjectBudget() {
  ViolationBudget budget;
  budget.weak_beat_dissonance = 1;
  return budget;
}

ViolationBudget dialogueBudget() {
  ViolationBudget budget;
  budget.weak_beat_dissonance = 1;
  budget.suspension_dissonance = 1;
  budget.inner_voice_hidden_perfect = 1;
  return budget;
}

ViolationBudget supportBudget() {
  ViolationBudget budget;
  budget.weak_beat_dissonance = 1;
  budget.cross_relation = 1;
  return budget;
}

ViolationBudget cadenceBudget() {
  ViolationBudget budget;
  budget.weak_beat_dissonance = 1;
  budget.suspension_dissonance = 1;
  return budget;
}

std::vector<NoteEvent> makeHeldConsonance(Tick duration) {
  NoteEvent held;
  held.pitch = kMidiC4;
  held.velocity = 80;
  held.start_tick = 0;
  held.duration = std::max(duration, static_cast<Tick>(duration::kQuarterNote));
  held.voice = 0;
  held.source = BachNoteSource::FreeCounterpoint;
  return {held};
}

std::vector<NoteEvent> makeRestPattern(Tick duration) {
  NoteEvent rest;
  rest.pitch = 0;
  rest.velocity = 0;
  rest.start_tick = 0;
  rest.duration = std::max(duration, static_cast<Tick>(duration::kQuarterNote));
  rest.voice = 0;
  rest.source = BachNoteSource::Unknown;
  return {rest};
}

}  // namespace

int ViolationBudget::total() const {
  return weak_beat_dissonance + suspension_dissonance + inner_voice_hidden_perfect +
         cross_relation + unprepared_leap;
}

void PatternDrawer::add(PatternCandidate candidate) {
  candidates_.push_back(std::move(candidate));
  std::stable_sort(candidates_.begin(), candidates_.end(),
                   [](const PatternCandidate& lhs, const PatternCandidate& rhs) {
                     if (lhs.protection != rhs.protection) {
                       return static_cast<uint8_t>(lhs.protection) <
                              static_cast<uint8_t>(rhs.protection);
                     }
                     if (lhs.motif_identity != rhs.motif_identity) {
                       return lhs.motif_identity > rhs.motif_identity;
                     }
                     return lhs.replacement_cost < rhs.replacement_cost;
                   });
}

const std::vector<PatternCandidate>& PatternDrawer::candidates() const {
  return candidates_;
}

const PatternCandidate* PatternDrawer::bestForIntent(VoiceIntent intent) const {
  for (const auto& candidate : candidates_) {
    if (candidate.intent == intent)
      return &candidate;
  }
  return candidates_.empty() ? nullptr : &candidates_.front();
}

size_t PatternDrawer::size() const {
  return candidates_.size();
}

bool PatternDrawer::empty() const {
  return candidates_.empty();
}

ThematicPlan buildThematicPlan(const Subject& subject, const MotifPool& motif_pool,
                               uint8_t num_voices, Key key) {
  ThematicPlan plan;
  plan.key = key;

  std::vector<NoteEvent> subject_head = normalizedHead(subject.notes, 4);
  std::vector<NoteEvent> two_note = normalizedHead(subject.notes, 2);
  std::vector<NoteEvent> three_note = normalizedHead(subject.notes, 3);
  std::vector<NoteEvent> subject_tail = normalizedTail(subject.notes, 3);
  Tick subject_span = std::max(subject.length_ticks, totalDuration(subject.notes));
  if (subject_span == 0)
    subject_span = kTicksPerBar;

  if (!subject_head.empty()) {
    plan.subject_drawer.add(makeCandidate(PatternKind::SubjectHead, VoiceIntent::SubjectCarrier,
                                          HarmonicRole::Thematic, ThematicProtectionLevel::Subject,
                                          subject_head, 1.0f, 100.0f, subjectBudget()));
  }
  if (!subject_tail.empty()) {
    plan.subject_drawer.add(makeCandidate(
        PatternKind::SubjectTail, VoiceIntent::SequentialCounterline, HarmonicRole::Thematic,
        ThematicProtectionLevel::Dialogue, subject_tail, 0.75f, 45.0f, dialogueBudget()));
  }
  if (!two_note.empty()) {
    plan.subject_drawer.add(makeCandidate(PatternKind::TwoNoteCell, VoiceIntent::RepeatedReplyCell,
                                          HarmonicRole::Dialogue, ThematicProtectionLevel::Dialogue,
                                          two_note, 0.9f, 35.0f, dialogueBudget()));
    plan.episode_drawer.add(makeCandidate(PatternKind::RepeatedCell, VoiceIntent::RepeatedReplyCell,
                                          HarmonicRole::Dialogue, ThematicProtectionLevel::Dialogue,
                                          two_note, 0.85f, 35.0f, dialogueBudget()));
  }
  if (!three_note.empty()) {
    plan.episode_drawer.add(makeCandidate(
        PatternKind::ThreeNoteCell, VoiceIntent::SequentialCounterline, HarmonicRole::Passing,
        ThematicProtectionLevel::Dialogue, three_note, 0.8f, 40.0f, dialogueBudget()));
  }

  for (const auto& motif : motif_pool.motifs()) {
    if (motif.notes.empty())
      continue;
    PatternKind kind = PatternKind::Sequence;
    VoiceIntent intent = VoiceIntent::SequentialCounterline;
    HarmonicRole role = HarmonicRole::Passing;
    ThematicProtectionLevel protection = ThematicProtectionLevel::InnerSupport;
    float replacement_cost = 25.0f;

    if (motif.origin == "countersubject") {
      kind = PatternKind::CountersubjectLine;
      intent = VoiceIntent::SequentialCounterline;
      role = HarmonicRole::Dialogue;
      protection = ThematicProtectionLevel::Dialogue;
      replacement_cost = 55.0f;
    } else if (motif.origin == "subject_head") {
      kind = PatternKind::SubjectHead;
      intent = VoiceIntent::SubjectCarrier;
      role = HarmonicRole::Thematic;
      protection = ThematicProtectionLevel::Subject;
      replacement_cost = 100.0f;
    } else if (motif.origin == "subject_tail") {
      kind = PatternKind::SubjectTail;
      replacement_cost = 45.0f;
    }

    PatternCandidate candidate = makeCandidate(
        kind, intent, role, protection, motif.notes, motif.characteristic_score, replacement_cost,
        protection == ThematicProtectionLevel::Subject ? subjectBudget() : dialogueBudget());
    if (kind == PatternKind::CountersubjectLine) {
      plan.countersubject_drawer.add(candidate);
    } else {
      plan.episode_drawer.add(candidate);
    }
  }

  plan.harmonic_support_drawer.add(
      makeCandidate(PatternKind::BassWalk, VoiceIntent::HarmonicSupport, HarmonicRole::Bass,
                    ThematicProtectionLevel::BassSupport,
                    subject_tail.empty() ? makeHeldConsonance(kTicksPerBeat) : subject_tail, 0.45f,
                    20.0f, supportBudget()));

  if (!subject_tail.empty()) {
    plan.cadential_closure_drawer.add(makeCandidate(
        PatternKind::CadenceFormula, VoiceIntent::CadentialClosure, HarmonicRole::Cadence,
        ThematicProtectionLevel::Dialogue, subject_tail, 0.7f, 60.0f, cadenceBudget()));
  }

  plan.filler_drawer.add(makeCandidate(PatternKind::HeldConsonance, VoiceIntent::FillerGap,
                                       HarmonicRole::Filler, ThematicProtectionLevel::Filler,
                                       makeHeldConsonance(kTicksPerBeat), 0.15f, 5.0f));
  plan.filler_drawer.add(makeCandidate(PatternKind::Rest, VoiceIntent::FillerGap,
                                       HarmonicRole::Filler, ThematicProtectionLevel::Filler,
                                       makeRestPattern(kTicksPerBeat), 0.0f, 1.0f));

  plan.recovery_drawer.add(makeCandidate(
      PatternKind::HeldConsonance, VoiceIntent::Recovery, HarmonicRole::Recovery,
      ThematicProtectionLevel::RepairOnly, makeHeldConsonance(kTicksPerBeat), 0.1f, 2.0f));
  plan.recovery_drawer.add(makeCandidate(
      PatternKind::Rest, VoiceIntent::Recovery, HarmonicRole::Recovery,
      ThematicProtectionLevel::RepairOnly, makeRestPattern(kTicksPerBeat), 0.0f, 1.0f));

  for (uint8_t voice = 0; voice < num_voices; ++voice) {
    VoiceLayer layer;
    layer.voice = voice;
    layer.start_tick = 0;
    layer.end_tick = subject_span;
    if (voice == 0) {
      layer.intent = VoiceIntent::SubjectCarrier;
      layer.protection = ThematicProtectionLevel::Subject;
      layer.preferred_pattern = PatternKind::SubjectHead;
    } else if (voice == 1) {
      layer.intent = VoiceIntent::RepeatedReplyCell;
      layer.protection = ThematicProtectionLevel::Dialogue;
      layer.preferred_pattern = PatternKind::TwoNoteCell;
    } else if (voice + 1 == num_voices) {
      layer.intent = VoiceIntent::HarmonicSupport;
      layer.protection = ThematicProtectionLevel::BassSupport;
      layer.preferred_pattern = PatternKind::BassWalk;
    } else {
      layer.intent = VoiceIntent::SequentialCounterline;
      layer.protection = ThematicProtectionLevel::InnerSupport;
      layer.preferred_pattern = PatternKind::ThreeNoteCell;
    }
    plan.voice_layers.push_back(layer);
  }

  if (!plan.voice_layers.empty()) {
    const PatternCandidate* subject_pattern =
        plan.subject_drawer.bestForIntent(VoiceIntent::SubjectCarrier);
    if (subject_pattern) {
      ThematicIntent intent;
      intent.start_tick = 0;
      intent.end_tick = subject_span;
      intent.voice = plan.voice_layers.front().voice;
      intent.intent = VoiceIntent::SubjectCarrier;
      intent.protection = ThematicProtectionLevel::Subject;
      intent.pattern = *subject_pattern;
      intent.protect_contour = true;
      intent.protect_rhythm = true;
      plan.initial_intents.push_back(intent);
    }
  }

  return plan;
}

const char* voiceIntentToString(VoiceIntent intent) {
  switch (intent) {
    case VoiceIntent::SubjectCarrier:
      return "subject_carrier";
    case VoiceIntent::RepeatedReplyCell:
      return "repeated_reply_cell";
    case VoiceIntent::SequentialCounterline:
      return "sequential_counterline";
    case VoiceIntent::HarmonicSupport:
      return "harmonic_support";
    case VoiceIntent::CadentialClosure:
      return "cadential_closure";
    case VoiceIntent::FillerGap:
      return "filler_gap";
    case VoiceIntent::Recovery:
      return "recovery";
  }
  return "unknown";
}

const char* thematicProtectionLevelToString(ThematicProtectionLevel level) {
  switch (level) {
    case ThematicProtectionLevel::GlobalArc:
      return "global_arc";
    case ThematicProtectionLevel::Subject:
      return "subject";
    case ThematicProtectionLevel::Dialogue:
      return "dialogue";
    case ThematicProtectionLevel::InnerSupport:
      return "inner_support";
    case ThematicProtectionLevel::BassSupport:
      return "bass_support";
    case ThematicProtectionLevel::Filler:
      return "filler";
    case ThematicProtectionLevel::RepairOnly:
      return "repair_only";
  }
  return "unknown";
}

const char* patternKindToString(PatternKind kind) {
  switch (kind) {
    case PatternKind::SubjectHead:
      return "subject_head";
    case PatternKind::SubjectTail:
      return "subject_tail";
    case PatternKind::TwoNoteCell:
      return "two_note_cell";
    case PatternKind::ThreeNoteCell:
      return "three_note_cell";
    case PatternKind::RhythmKernel:
      return "rhythm_kernel";
    case PatternKind::CountersubjectLine:
      return "countersubject_line";
    case PatternKind::RepeatedCell:
      return "repeated_cell";
    case PatternKind::Sequence:
      return "sequence";
    case PatternKind::PassingChain:
      return "passing_chain";
    case PatternKind::NeighborChain:
      return "neighbor_chain";
    case PatternKind::SuspensionResolution:
      return "suspension_resolution";
    case PatternKind::BassWalk:
      return "bass_walk";
    case PatternKind::PedalPoint:
      return "pedal_point";
    case PatternKind::CadenceFormula:
      return "cadence_formula";
    case PatternKind::HeldConsonance:
      return "held_consonance";
    case PatternKind::Rest:
      return "rest";
  }
  return "unknown";
}

const char* harmonicRoleToString(HarmonicRole role) {
  switch (role) {
    case HarmonicRole::Thematic:
      return "thematic";
    case HarmonicRole::Dialogue:
      return "dialogue";
    case HarmonicRole::Passing:
      return "passing";
    case HarmonicRole::Neighbor:
      return "neighbor";
    case HarmonicRole::Suspension:
      return "suspension";
    case HarmonicRole::Bass:
      return "bass";
    case HarmonicRole::Pedal:
      return "pedal";
    case HarmonicRole::Cadence:
      return "cadence";
    case HarmonicRole::Filler:
      return "filler";
    case HarmonicRole::Recovery:
      return "recovery";
  }
  return "unknown";
}

const char* violationClassToString(ViolationClass cls) {
  switch (cls) {
    case ViolationClass::AllowedExpressive:
      return "allowed_expressive";
    case ViolationClass::RequiresResolution:
      return "requires_resolution";
    case ViolationClass::Penalized:
      return "penalized";
    case ViolationClass::Forbidden:
      return "forbidden";
  }
  return "unknown";
}

const char* budgetActionToString(BudgetAction action) {
  switch (action) {
    case BudgetAction::SpendBudget:
      return "spend_budget";
    case BudgetAction::RequireResolution:
      return "require_resolution";
    case BudgetAction::ReplaceLowerLayer:
      return "replace_lower_layer";
    case BudgetAction::Reject:
      return "reject";
  }
  return "unknown";
}

BudgetDecision decideBudgetUse(VoiceIntent intent, ThematicProtectionLevel protection,
                               ViolationClass violation_class, bool lower_replacement_available,
                               bool has_resolution) {
  const bool high_protection = protection == ThematicProtectionLevel::GlobalArc ||
                               protection == ThematicProtectionLevel::Subject ||
                               protection == ThematicProtectionLevel::Dialogue;
  const bool low_protection = protection == ThematicProtectionLevel::Filler ||
                              protection == ThematicProtectionLevel::RepairOnly ||
                              intent == VoiceIntent::FillerGap || intent == VoiceIntent::Recovery;

  if (violation_class == ViolationClass::Forbidden) {
    if (lower_replacement_available) {
      return {BudgetAction::ReplaceLowerLayer, false,
              "forbidden violation; replace lower-protection material first"};
    }
    return {BudgetAction::Reject, false, "forbidden violation without available replacement"};
  }

  if (low_protection) {
    if (lower_replacement_available) {
      return {BudgetAction::ReplaceLowerLayer, false,
              "low-protection material should be replaced, not protected by budget"};
    }
    if (violation_class == ViolationClass::AllowedExpressive && has_resolution) {
      return {BudgetAction::SpendBudget, true,
              "low-protection expressive event accepted only because it resolves"};
    }
    return {BudgetAction::Reject, false, "low-protection unresolved violation"};
  }

  if (violation_class == ViolationClass::RequiresResolution) {
    if (has_resolution) {
      return {BudgetAction::RequireResolution, true,
              "protected dialogue may spend budget when resolution is confirmed"};
    }
    if (lower_replacement_available) {
      return {BudgetAction::ReplaceLowerLayer, false,
              "resolution missing; replace lower-protection material"};
    }
    return {BudgetAction::Reject, false, "resolution-required violation has no resolution"};
  }

  if (violation_class == ViolationClass::AllowedExpressive) {
    if (high_protection) {
      return {BudgetAction::SpendBudget, true, "budget preserves high-protection musical intent"};
    }
    if (lower_replacement_available) {
      return {BudgetAction::ReplaceLowerLayer, false,
              "expressive event is not worth spending budget outside protected layers"};
    }
    return {BudgetAction::SpendBudget, true,
            "accepted expressive event with no lower replacement available"};
  }

  if (violation_class == ViolationClass::Penalized) {
    if (high_protection && has_resolution) {
      return {BudgetAction::SpendBudget, true,
              "penalized event is tolerated to preserve resolved protected material"};
    }
    if (lower_replacement_available) {
      return {BudgetAction::ReplaceLowerLayer, false,
              "penalized event should be solved by replacing lower material"};
    }
    return {BudgetAction::Reject, false, "penalized event lacks resolution and replacement"};
  }

  return {BudgetAction::Reject, false, "unclassified budget decision"};
}

}  // namespace bach
