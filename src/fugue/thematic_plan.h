// Thematic planning layer for fugue generation.
//
// This module captures the musical intent that should exist before local
// pitch-by-pitch constraint solving.  It is intentionally independent from the
// current finalize repair sweeps so the generator can migrate from
// repair-driven output toward protected subject/dialogue structures.

#ifndef BACH_FUGUE_THEMATIC_PLAN_H
#define BACH_FUGUE_THEMATIC_PLAN_H

#include <cstdint>
#include <vector>

#include "core/basic_types.h"
#include "fugue/motif_pool.h"
#include "fugue/subject.h"

namespace bach {

/// @brief Musical role assigned to a voice over a time span.
enum class VoiceIntent : uint8_t {
  SubjectCarrier,         ///< Preserve subject contour/rhythm/entry timing.
  RepeatedReplyCell,      ///< Intentional short repeated-cell dialogue.
  SequentialCounterline,  ///< Stepwise or sequential counterline.
  HarmonicSupport,        ///< Bass/inner support for harmony and cadence.
  CadentialClosure,       ///< Cadence preparation and closing formula.
  FillerGap,              ///< Low-protection local filler.
  Recovery                ///< Replacement material after a failed constraint pass.
};

/// @brief Protection priority. Lower numeric levels are harder to change.
enum class ThematicProtectionLevel : uint8_t {
  GlobalArc = 0,
  Subject = 1,
  Dialogue = 2,
  InnerSupport = 3,
  BassSupport = 4,
  Filler = 5,
  RepairOnly = 6
};

/// @brief Reusable melodic pattern categories used by drawers.
enum class PatternKind : uint8_t {
  SubjectHead,
  SubjectTail,
  TwoNoteCell,
  ThreeNoteCell,
  RhythmKernel,
  CountersubjectLine,
  RepeatedCell,
  Sequence,
  PassingChain,
  NeighborChain,
  SuspensionResolution,
  BassWalk,
  PedalPoint,
  CadenceFormula,
  HeldConsonance,
  Rest
};

/// @brief Harmonic job of a pattern candidate.
enum class HarmonicRole : uint8_t {
  Thematic,
  Dialogue,
  Passing,
  Neighbor,
  Suspension,
  Bass,
  Pedal,
  Cadence,
  Filler,
  Recovery
};

/// @brief Contextual classification for a detected violation.
enum class ViolationClass : uint8_t { AllowedExpressive, RequiresResolution, Penalized, Forbidden };

/// @brief High-level action after contextual violation classification.
enum class BudgetAction : uint8_t {
  SpendBudget,        ///< Preserve this structure and consume its budget.
  RequireResolution,  ///< Allow only with a confirmed musical resolution.
  ReplaceLowerLayer,  ///< Do not spend budget; replace a lower-protection pattern.
  Reject              ///< The violation is structurally unacceptable here.
};

/// @brief Decision explaining how a violation should be handled.
struct BudgetDecision {
  BudgetAction action = BudgetAction::Reject;
  bool consumes_budget = false;
  const char* reason = "";
};

/// @brief Contextual budget for rule deviations.
///
/// Budgets are not global "permission slips".  They should be consumed only
/// when preserving a higher-protection structure is musically preferable to
/// replacing a lower-protection pattern.
struct ViolationBudget {
  int weak_beat_dissonance = 0;
  int suspension_dissonance = 0;
  int inner_voice_hidden_perfect = 0;
  int cross_relation = 0;
  int unprepared_leap = 0;

  int total() const;
};

/// @brief A reusable musical structure available to a drawer.
struct PatternCandidate {
  PatternKind kind = PatternKind::SubjectHead;
  VoiceIntent intent = VoiceIntent::SubjectCarrier;
  HarmonicRole harmonic_role = HarmonicRole::Thematic;
  ThematicProtectionLevel protection = ThematicProtectionLevel::Subject;
  std::vector<NoteEvent> notes;
  float motif_identity = 0.0f;
  float markov_score = 0.0f;
  float counterpoint_score = 0.0f;
  float replacement_cost = 0.0f;
  ViolationBudget budget;
};

/// @brief Named collection of candidates for a specific musical purpose.
class PatternDrawer {
 public:
  void add(PatternCandidate candidate);
  const std::vector<PatternCandidate>& candidates() const;
  const PatternCandidate* bestForIntent(VoiceIntent intent) const;
  size_t size() const;
  bool empty() const;

 private:
  std::vector<PatternCandidate> candidates_;
};

/// @brief Planned role for a voice over a span.
struct VoiceLayer {
  VoiceId voice = 0;
  Tick start_tick = 0;
  Tick end_tick = 0;
  VoiceIntent intent = VoiceIntent::FillerGap;
  ThematicProtectionLevel protection = ThematicProtectionLevel::Filler;
  PatternKind preferred_pattern = PatternKind::Rest;
};

/// @brief A span-level intent used by future section generators.
struct ThematicIntent {
  Tick start_tick = 0;
  Tick end_tick = 0;
  VoiceId voice = 0;
  VoiceIntent intent = VoiceIntent::FillerGap;
  ThematicProtectionLevel protection = ThematicProtectionLevel::Filler;
  PatternCandidate pattern;
  bool protect_contour = false;
  bool protect_rhythm = false;
};

/// @brief High-level thematic material and protection plan.
struct ThematicPlan {
  Key key = Key::C;
  std::vector<VoiceLayer> voice_layers;
  PatternDrawer subject_drawer;
  PatternDrawer countersubject_drawer;
  PatternDrawer episode_drawer;
  PatternDrawer harmonic_support_drawer;
  PatternDrawer cadential_closure_drawer;
  PatternDrawer filler_drawer;
  PatternDrawer recovery_drawer;
  std::vector<ThematicIntent> initial_intents;
};

/// @brief Build initial thematic planning data from existing assets.
///
/// This intentionally does not replace the current generator yet.  It creates
/// the protected material inventory that later section generators can consume.
ThematicPlan buildThematicPlan(const Subject& subject, const MotifPool& motif_pool,
                               uint8_t num_voices, Key key);

const char* voiceIntentToString(VoiceIntent intent);
const char* thematicProtectionLevelToString(ThematicProtectionLevel level);
const char* patternKindToString(PatternKind kind);
const char* harmonicRoleToString(HarmonicRole role);
const char* violationClassToString(ViolationClass cls);
const char* budgetActionToString(BudgetAction action);

/// @brief Decide whether to spend budget or replace lower-protection material.
BudgetDecision decideBudgetUse(VoiceIntent intent, ThematicProtectionLevel protection,
                               ViolationClass violation_class, bool lower_replacement_available,
                               bool has_resolution);

}  // namespace bach

#endif  // BACH_FUGUE_THEMATIC_PLAN_H
