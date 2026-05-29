#include "fugue/thematic_plan.h"

#include <gtest/gtest.h>

#include "core/basic_types.h"
#include "fugue/motif_pool.h"
#include "fugue/subject.h"

namespace bach {
namespace {

NoteEvent note(uint8_t pitch, Tick start, Tick duration) {
  NoteEvent n;
  n.pitch = pitch;
  n.velocity = 80;
  n.start_tick = start;
  n.duration = duration;
  n.voice = 0;
  n.source = BachNoteSource::FugueSubject;
  return n;
}

Subject makeSubject() {
  Subject subject;
  subject.key = Key::C;
  subject.character = SubjectCharacter::Restless;
  subject.notes = {
      note(60, 0, duration::kSixteenthNote),
      note(62, duration::kSixteenthNote, duration::kSixteenthNote),
      note(64, duration::kEighthNote, duration::kSixteenthNote),
      note(65, duration::kEighthNote + duration::kSixteenthNote, duration::kSixteenthNote),
      note(64, duration::kQuarterNote, duration::kQuarterNote),
      note(62, duration::kHalfNote, duration::kQuarterNote),
      note(60, duration::kHalfNote + duration::kQuarterNote, duration::kQuarterNote),
  };
  subject.length_ticks = kTicksPerBar;
  return subject;
}

}  // namespace

TEST(ThematicPlanTest, BuildsProtectedSubjectAndDialogueDrawers) {
  Subject subject = makeSubject();
  MotifPool pool;
  pool.build(subject.notes, {}, subject.character);

  ThematicPlan plan = buildThematicPlan(subject, pool, 4, Key::C);

  EXPECT_EQ(plan.key, Key::C);
  ASSERT_FALSE(plan.subject_drawer.empty());
  ASSERT_FALSE(plan.episode_drawer.empty());
  ASSERT_FALSE(plan.cadential_closure_drawer.empty());

  const PatternCandidate* subject_candidate =
      plan.subject_drawer.bestForIntent(VoiceIntent::SubjectCarrier);
  ASSERT_NE(subject_candidate, nullptr);
  EXPECT_EQ(subject_candidate->protection, ThematicProtectionLevel::Subject);
  EXPECT_EQ(subject_candidate->kind, PatternKind::SubjectHead);
  EXPECT_GT(subject_candidate->replacement_cost, 90.0f);
  EXPECT_EQ(subject_candidate->budget.weak_beat_dissonance, 1);

  const PatternCandidate* reply_candidate =
      plan.episode_drawer.bestForIntent(VoiceIntent::RepeatedReplyCell);
  ASSERT_NE(reply_candidate, nullptr);
  EXPECT_EQ(reply_candidate->intent, VoiceIntent::RepeatedReplyCell);
  EXPECT_EQ(reply_candidate->protection, ThematicProtectionLevel::Dialogue);
  EXPECT_GE(reply_candidate->notes.size(), 2u);

  const PatternCandidate* cadence_candidate =
      plan.cadential_closure_drawer.bestForIntent(VoiceIntent::CadentialClosure);
  ASSERT_NE(cadence_candidate, nullptr);
  EXPECT_EQ(cadence_candidate->kind, PatternKind::CadenceFormula);
  EXPECT_EQ(cadence_candidate->harmonic_role, HarmonicRole::Cadence);
  EXPECT_EQ(cadence_candidate->protection, ThematicProtectionLevel::Dialogue);
  EXPECT_GT(cadence_candidate->replacement_cost, 50.0f);
}

TEST(ThematicPlanTest, AssignsVoiceLayersByProtectionPriority) {
  Subject subject = makeSubject();
  MotifPool pool;
  pool.build(subject.notes, {}, subject.character);

  ThematicPlan plan = buildThematicPlan(subject, pool, 4, Key::C);

  ASSERT_EQ(plan.voice_layers.size(), 4u);
  EXPECT_EQ(plan.voice_layers[0].intent, VoiceIntent::SubjectCarrier);
  EXPECT_EQ(plan.voice_layers[0].protection, ThematicProtectionLevel::Subject);
  EXPECT_EQ(plan.voice_layers[1].intent, VoiceIntent::RepeatedReplyCell);
  EXPECT_EQ(plan.voice_layers[1].protection, ThematicProtectionLevel::Dialogue);
  EXPECT_EQ(plan.voice_layers[2].intent, VoiceIntent::SequentialCounterline);
  EXPECT_EQ(plan.voice_layers[2].protection, ThematicProtectionLevel::InnerSupport);
  EXPECT_EQ(plan.voice_layers[3].intent, VoiceIntent::HarmonicSupport);
  EXPECT_EQ(plan.voice_layers[3].protection, ThematicProtectionLevel::BassSupport);

  ASSERT_FALSE(plan.initial_intents.empty());
  EXPECT_TRUE(plan.initial_intents[0].protect_contour);
  EXPECT_TRUE(plan.initial_intents[0].protect_rhythm);
}

TEST(ThematicPlanTest, RecoveryDrawerUsesLowCostReplaceablePatterns) {
  Subject subject = makeSubject();
  MotifPool pool;
  pool.build(subject.notes, {}, subject.character);

  ThematicPlan plan = buildThematicPlan(subject, pool, 4, Key::C);

  ASSERT_GE(plan.recovery_drawer.size(), 2u);
  const PatternCandidate* recovery = plan.recovery_drawer.bestForIntent(VoiceIntent::Recovery);
  ASSERT_NE(recovery, nullptr);
  EXPECT_EQ(recovery->intent, VoiceIntent::Recovery);
  EXPECT_EQ(recovery->protection, ThematicProtectionLevel::RepairOnly);
  EXPECT_LE(recovery->replacement_cost, 2.0f);
}

TEST(ThematicPlanTest, StringConversionsExposeStableNames) {
  EXPECT_STREQ(voiceIntentToString(VoiceIntent::RepeatedReplyCell), "repeated_reply_cell");
  EXPECT_STREQ(thematicProtectionLevelToString(ThematicProtectionLevel::Dialogue), "dialogue");
  EXPECT_STREQ(patternKindToString(PatternKind::SuspensionResolution), "suspension_resolution");
  EXPECT_STREQ(harmonicRoleToString(HarmonicRole::Recovery), "recovery");
  EXPECT_STREQ(violationClassToString(ViolationClass::RequiresResolution), "requires_resolution");
  EXPECT_STREQ(budgetActionToString(BudgetAction::ReplaceLowerLayer), "replace_lower_layer");
}

TEST(ThematicPlanTest, BudgetDecisionSpendsOnlyForProtectedResolvedIntent) {
  BudgetDecision subject_weak =
      decideBudgetUse(VoiceIntent::SubjectCarrier, ThematicProtectionLevel::Subject,
                      ViolationClass::AllowedExpressive, true, false);
  EXPECT_EQ(subject_weak.action, BudgetAction::SpendBudget);
  EXPECT_TRUE(subject_weak.consumes_budget);

  BudgetDecision dialogue_suspension =
      decideBudgetUse(VoiceIntent::RepeatedReplyCell, ThematicProtectionLevel::Dialogue,
                      ViolationClass::RequiresResolution, true, true);
  EXPECT_EQ(dialogue_suspension.action, BudgetAction::RequireResolution);
  EXPECT_TRUE(dialogue_suspension.consumes_budget);
}

TEST(ThematicPlanTest, BudgetDecisionReplacesLowProtectionMaterialFirst) {
  BudgetDecision filler_clash =
      decideBudgetUse(VoiceIntent::FillerGap, ThematicProtectionLevel::Filler,
                      ViolationClass::Penalized, true, false);
  EXPECT_EQ(filler_clash.action, BudgetAction::ReplaceLowerLayer);
  EXPECT_FALSE(filler_clash.consumes_budget);

  BudgetDecision forbidden_outer =
      decideBudgetUse(VoiceIntent::SubjectCarrier, ThematicProtectionLevel::Subject,
                      ViolationClass::Forbidden, true, true);
  EXPECT_EQ(forbidden_outer.action, BudgetAction::ReplaceLowerLayer);
  EXPECT_FALSE(forbidden_outer.consumes_budget);
}

TEST(ThematicPlanTest, BudgetDecisionRejectsUnresolvedProtectedDissonance) {
  BudgetDecision unresolved =
      decideBudgetUse(VoiceIntent::RepeatedReplyCell, ThematicProtectionLevel::Dialogue,
                      ViolationClass::RequiresResolution, false, false);
  EXPECT_EQ(unresolved.action, BudgetAction::Reject);
  EXPECT_FALSE(unresolved.consumes_budget);
}

}  // namespace bach
