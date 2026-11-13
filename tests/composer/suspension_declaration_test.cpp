#include "composer/suspension_declaration.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <vector>

#include "composer/candidate_search.h"
#include "composer/harmonic_plan.h"
#include "composer/material.h"
#include "composer/provenance.h"
#include "composer/span.h"
#include "composer/validator.h"
#include "composer/voice_intent.h"
#include "core/basic_types.h"

namespace bach::composer {
namespace {

constexpr Tick kPrepTick = 3 * kTicksPerBeat;
constexpr Tick kSusTick = kTicksPerBar;
constexpr Tick kResTick = kTicksPerBar + kTicksPerBeat;

HarmonicPlan cMajor() {
  HarmonicPlan plan;
  plan.tonic_pc = 0;
  plan.is_minor = false;
  ChordEvent chord;
  chord.start_tick = 0;
  chord.root_pc = 0;
  chord.quality = ChordQuality::Major;
  plan.chords.push_back(chord);
  return plan;
}

DeclaredSuspensionReading reading(int held, int res, int low_prep, int low_sus, int low_res) {
  DeclaredSuspensionReading r;
  r.preparation_tick = kPrepTick;
  r.suspension_tick = kSusTick;
  r.resolution_tick = kResTick;
  r.held_pitch = held;
  r.resolution_pitch = res;
  r.lowest_other_at_preparation = low_prep;
  r.lowest_other_at_suspension = low_sus;
  r.lowest_other_at_resolution = low_res;
  return r;
}

NoteEvent makeNote(Tick start, Tick dur, std::uint8_t pitch, VoiceId voice) {
  NoteEvent note;
  note.start_tick = start;
  note.duration = dur;
  note.pitch = pitch;
  note.voice = voice;
  note.velocity = 80;
  return note;
}

bool hasSuspensionFailure(const ValidationReport& report) {
  for (const auto& failure : report.failures) {
    if (failure.rule_id.rfind("suspension_", 0) == 0)
      return true;
  }
  return false;
}

bool hasRule(const ValidationReport& report, const std::string& rule_id) {
  for (const auto& failure : report.failures) {
    if (failure.rule_id == rule_id)
      return true;
  }
  return false;
}

SuspensionPattern fourThreePattern() {
  return {SuspensionType::Sus4_3, kPrepTick, kSusTick, kResTick, 67, 67, 65, 0};
}

Span carrierSpan() {
  Span span;
  span.id = 1;
  span.start_tick = kPrepTick;
  span.end_tick = kResTick + 2 * kTicksPerBeat;
  span.voice = 0;
  span.intent = VoiceIntent::SuspensionCarrier;
  return span;
}

}  // namespace

TEST(SuspensionDeclarationTest, ClassifiesEachTypeAgainstTheLowestOtherVoice) {
  const HarmonicPlan plan = cMajor();
  SuspensionType type = SuspensionType::Sus9_8;
  ASSERT_TRUE(classifyDeclaredSuspension(plan, reading(67, 65, 48, 50, 50), &type));
  EXPECT_EQ(type, SuspensionType::Sus4_3);
  ASSERT_TRUE(classifyDeclaredSuspension(plan, reading(69, 67, 60, 59, 59), &type));
  EXPECT_EQ(type, SuspensionType::Sus7_6);
  ASSERT_TRUE(classifyDeclaredSuspension(plan, reading(74, 72, 62, 72, 72), &type));
  EXPECT_EQ(type, SuspensionType::Sus9_8);
  ASSERT_TRUE(classifyDeclaredSuspension(plan, reading(50, 48, 65, 51, 51), &type));
  EXPECT_EQ(type, SuspensionType::Sus2_3);
}

TEST(SuspensionDeclarationTest, RejectsDissonanceAgainstAnUpperVoiceOnly) {
  // The held G is consonant against the lowest voice (C) at the suspension.
  SuspensionType type = SuspensionType::Sus4_3;
  EXPECT_FALSE(classifyDeclaredSuspension(cMajor(), reading(67, 65, 48, 48, 48), &type));
}

TEST(SuspensionDeclarationTest, RejectsDissonantPreparation) {
  SuspensionType type = SuspensionType::Sus4_3;
  EXPECT_FALSE(classifyDeclaredSuspension(cMajor(), reading(67, 65, 61, 50, 50), &type));
}

TEST(SuspensionDeclarationTest, RejectsNonStepResolutionAndSilentBass) {
  SuspensionType type = SuspensionType::Sus4_3;
  EXPECT_FALSE(classifyDeclaredSuspension(cMajor(), reading(67, 62, 48, 50, 50), &type));
  EXPECT_FALSE(classifyDeclaredSuspension(cMajor(), reading(67, 65, 48, -1, 50), &type));
}

TEST(SuspensionDeclarationTest, ToneSpanCoversHeldNoteAndResolutionOnset) {
  const std::vector<SuspensionPattern> patterns = {
      {SuspensionType::Sus4_3, 100, 200, 300, 67, 67, 65, 0}};
  EXPECT_FALSE(isDeclaredSuspensionTone(patterns, 0, 99));
  EXPECT_TRUE(isDeclaredSuspensionTone(patterns, 0, 100));
  EXPECT_TRUE(isDeclaredSuspensionTone(patterns, 0, 299));
  EXPECT_TRUE(isDeclaredSuspensionTone(patterns, 0, 300));
  EXPECT_FALSE(isDeclaredSuspensionTone(patterns, 0, 301));
  EXPECT_FALSE(isDeclaredSuspensionTone(patterns, 1, 150));
}

TEST(SuspensionDeclarationTest, CarrierShipsOneSoundingNoteAcrossTheBarlineAndValidates) {
  Material material;
  material.suspension_patterns.push_back(fourThreePattern());
  const HarmonicPlan plan = cMajor();
  CandidateSearch search;
  CandidateContext ctx;
  const auto cands = search.enumerate(carrierSpan(), plan, material, ctx);

  ASSERT_EQ(cands.size(), 2u);
  EXPECT_EQ(cands[0].start_tick, kPrepTick);
  EXPECT_EQ(cands[0].duration, kResTick - kPrepTick);
  EXPECT_EQ(cands[0].pitch, 67u);
  EXPECT_EQ(cands[0].satisfied_rules, ruleBitMask(RuleBit::SuspensionPrepared));
  EXPECT_EQ(cands[1].start_tick, kResTick);
  EXPECT_EQ(cands[1].satisfied_rules, ruleBitMask(RuleBit::SuspensionResolved));

  std::vector<NoteEvent> notes;
  std::vector<NoteProvenance> prov;
  for (const auto& cand : cands) {
    notes.push_back(makeNote(cand.start_tick, cand.duration, cand.pitch, 0));
    NoteProvenance entry;
    entry.source = NoteSource::Material;
    entry.voice_intent = VoiceIntent::SuspensionCarrier;
    entry.satisfied_rules = cand.satisfied_rules;
    prov.push_back(entry);
  }
  notes.push_back(makeNote(kPrepTick, kTicksPerBeat, 48, 1));
  notes.push_back(makeNote(kSusTick, 2 * kTicksPerBeat, 50, 1));
  prov.resize(notes.size());

  // No note of the suspended voice starts on the barline, yet the figure is a
  // validated prepared dissonance.
  for (const auto& note : notes) {
    if (note.voice == 0)
      EXPECT_NE(note.start_tick, kSusTick);
  }
  const ValidationReport report = Validator{}.validate(notes, prov, plan, material);
  EXPECT_FALSE(hasSuspensionFailure(report));
  EXPECT_FALSE(hasRule(report, "vertical_dissonance"));
}

TEST(SuspensionDeclarationTest, CarrierStampsDeclaredBitsAndTheEmittedFigureValidates) {
  Material material;
  material.suspension_patterns.push_back(fourThreePattern());
  const HarmonicPlan plan = cMajor();
  CandidateSearch search;
  CandidateContext ctx;
  const auto cands = search.enumerate(carrierSpan(), plan, material, ctx);

  std::vector<NoteEvent> notes;
  std::vector<NoteProvenance> prov;
  for (const auto& cand : cands) {
    if (cand.start_tick == kPrepTick)
      EXPECT_EQ(cand.satisfied_rules, ruleBitMask(RuleBit::SuspensionPrepared));
    else if (cand.start_tick == kResTick)
      EXPECT_EQ(cand.satisfied_rules, ruleBitMask(RuleBit::SuspensionResolved));
    notes.push_back(makeNote(cand.start_tick, cand.duration, cand.pitch, 0));
    NoteProvenance entry;
    entry.source = NoteSource::Material;
    entry.voice_intent = VoiceIntent::SuspensionCarrier;
    entry.satisfied_rules = cand.satisfied_rules;
    prov.push_back(entry);
  }
  notes.push_back(makeNote(kPrepTick, kTicksPerBeat, 48, 1));
  notes.push_back(makeNote(kSusTick, 2 * kTicksPerBeat, 50, 1));
  prov.resize(notes.size());

  const ValidationReport report = Validator{}.validate(notes, prov, plan, material);
  EXPECT_FALSE(hasSuspensionFailure(report));
  EXPECT_FALSE(hasRule(report, "vertical_dissonance"));
}

TEST(SuspensionDeclarationTest, AccentMustBeStrictlyStrongerThanResolution) {
  const HarmonicPlan plan = cMajor();
  const auto accented = [&](Tick sus, Tick res) {
    DeclaredSuspensionReading r = reading(67, 65, 48, 50, 50);
    r.preparation_tick = sus - kTicksPerBeat;
    r.suspension_tick = sus;
    r.resolution_tick = res;
    return declaredSuspensionAccented(plan, r);
  };
  // Downbeat resolving on beat 2: stronger suspension.
  EXPECT_TRUE(accented(kTicksPerBar, kTicksPerBar + kTicksPerBeat));
  // Beat 2 resolving on the next downbeat: weaker suspension.
  EXPECT_FALSE(accented(kTicksPerBeat, kTicksPerBar));
  // Beat 2 resolving on beat 4: equal strength.
  EXPECT_FALSE(accented(kTicksPerBeat, 3 * kTicksPerBeat));
  // The same direction through the classifier.
  SuspensionType type = SuspensionType::Sus4_3;
  DeclaredSuspensionReading weak = reading(67, 65, 48, 50, 50);
  weak.preparation_tick = 0;
  weak.suspension_tick = kTicksPerBeat;
  weak.resolution_tick = kTicksPerBar;
  EXPECT_FALSE(classifyDeclaredSuspension(plan, weak, &type));
  EXPECT_TRUE(classifyDeclaredSuspension(plan, reading(67, 65, 48, 50, 50), &type));
}

TEST(SuspensionDeclarationTest, ValidatorAcceptsTheRestruckTwoNoteShapeToo) {
  Material material;
  material.suspension_patterns.push_back(fourThreePattern());
  const std::vector<NoteEvent> notes = {
      makeNote(kPrepTick, kTicksPerBeat, 67, 0),    makeNote(kSusTick, kTicksPerBeat, 67, 0),
      makeNote(kResTick, kTicksPerBeat, 65, 0),     makeNote(kPrepTick, kTicksPerBeat, 48, 1),
      makeNote(kSusTick, 2 * kTicksPerBeat, 50, 1),
  };
  const std::vector<NoteProvenance> prov(notes.size());
  const ValidationReport report = Validator{}.validate(notes, prov, cMajor(), material);
  EXPECT_FALSE(hasSuspensionFailure(report));
}

TEST(SuspensionDeclarationTest, ValidatorRejectsHeldNoteShorterBeforeThanAfterTheBarline) {
  Material material;
  SuspensionPattern pattern = fourThreePattern();
  pattern.preparation_tick = kSusTick - kTicksPerBeat / 2;
  material.suspension_patterns.push_back(pattern);
  const std::vector<NoteEvent> notes = {
      makeNote(pattern.preparation_tick, kTicksPerBeat / 2 + kTicksPerBeat, 67, 0),
      makeNote(kResTick, kTicksPerBeat, 65, 0),
      makeNote(pattern.preparation_tick, kTicksPerBeat / 2, 48, 1),
      makeNote(kSusTick, 2 * kTicksPerBeat, 50, 1),
  };
  const std::vector<NoteProvenance> prov(notes.size());
  const ValidationReport report = Validator{}.validate(notes, prov, cMajor(), material);
  EXPECT_TRUE(hasRule(report, "suspension_preparation_duration"));
}

TEST(SuspensionDeclarationTest, ReplayStampsDeclaredTieBitsOnFigurationNotes) {
  Material material;
  material.suspension_patterns.push_back(fourThreePattern());
  FigurationSection section;
  section.voice = 0;
  section.start_tick = kPrepTick;
  section.end_tick = kResTick + kTicksPerBeat;
  section.notes.push_back({kPrepTick, kResTick - kPrepTick, 67});
  section.notes.push_back({kResTick, kTicksPerBeat, 65});
  material.figuration_sections.push_back(section);
  Span span = carrierSpan();
  span.end_tick = section.end_tick;
  span.intent = VoiceIntent::FigurationCarrier;

  CandidateSearch search;
  CandidateContext ctx;
  const auto cands = search.enumerate(span, cMajor(), material, ctx);
  ASSERT_EQ(cands.size(), 2u);
  EXPECT_TRUE((cands[0].satisfied_rules & ruleBitMask(RuleBit::SuspensionPrepared)).any());
  EXPECT_TRUE((cands[1].satisfied_rules & ruleBitMask(RuleBit::SuspensionResolved)).any());
  EXPECT_FALSE((cands[0].satisfied_rules & ruleBitMask(RuleBit::SuspensionResolved)).any());
}

// --- Stamping on every replay branch -----------------------------------------

constexpr Tick kHeldDur = kResTick - kPrepTick;

std::vector<MaterialNote> heldAndResolution() {
  return {{kPrepTick, kHeldDur, 67}, {kResTick, kTicksPerBeat, 65}};
}

Span replaySpan(VoiceIntent intent) {
  Span span;
  span.id = 1;
  span.start_tick = kPrepTick;
  span.end_tick = kResTick + kTicksPerBeat;
  span.voice = 0;
  span.intent = intent;
  return span;
}

// Puts the held tone and its resolution into the source the intent replays.
// Returns false for an intent whose source cannot express a free two-note line.
bool loadSource(VoiceIntent intent, Material* material) {
  const std::vector<MaterialNote> line = heldAndResolution();
  const Span span = replaySpan(intent);
  switch (intent) {
    case VoiceIntent::SubjectCarrier:
      material->subject = line;
      return true;
    case VoiceIntent::AnswerCarrier:
      material->answer = line;
      return true;
    case VoiceIntent::CountersubjectCarrier:
      material->countersubject = line;
      return true;
    case VoiceIntent::MiddleEntryCarrier: {
      MiddleEntryDecl decl;
      decl.voice = 0;
      decl.notes = line;
      material->middle_entries.push_back(decl);
      return true;
    }
    case VoiceIntent::StrettoCarrier: {
      StrettoDecl decl;
      decl.follower_voice = 0;
      decl.follower_notes = line;
      material->stretto_entries.push_back(decl);
      return true;
    }
    case VoiceIntent::CodaCarrier: {
      CodaDecl decl;
      decl.voice = 0;
      decl.notes = line;
      material->coda_extensions.push_back(decl);
      return true;
    }
    case VoiceIntent::SubjectCarrierInverted: {
      SubjectVariantDecl decl;
      decl.voice = 0;
      decl.notes = line;
      material->subject_variants.push_back(decl);
      return true;
    }
    case VoiceIntent::RhythmCarrier: {
      RhythmFragment frag;
      frag.voice = 0;
      frag.notes = line;
      material->rhythm_fragments.push_back(frag);
      return true;
    }
    case VoiceIntent::NctCarrier:
      material->nct_figures = line;
      return true;
    case VoiceIntent::ArpeggioFlow:
      material->arpeggio_template.notes = line;
      return true;
    case VoiceIntent::VariationCarrier: {
      VariationDecl decl;
      decl.voice = 0;
      decl.start_tick = span.start_tick;
      decl.end_tick = span.end_tick;
      decl.notes = line;
      material->variations.push_back(decl);
      return true;
    }
    case VoiceIntent::FigurationCarrier: {
      FigurationSection section;
      section.voice = 0;
      section.start_tick = span.start_tick;
      section.end_tick = span.end_tick;
      section.notes = line;
      material->figuration_sections.push_back(section);
      return true;
    }
    case VoiceIntent::ToccataCarrier: {
      ToccataSection section;
      section.voice = 0;
      section.start_tick = span.start_tick;
      section.end_tick = span.end_tick;
      section.notes = line;
      material->toccata_sections.push_back(section);
      return true;
    }
    case VoiceIntent::CantusFirmusCarrier:
      material->cantus_firmus = line;
      return true;
    case VoiceIntent::GoldbergVariationCarrier: {
      PassacagliaVariation decl;
      decl.voice = 0;
      decl.start_tick = span.start_tick;
      decl.end_tick = span.end_tick;
      decl.notes = line;
      material->goldberg_variations.push_back(decl);
      return true;
    }
    case VoiceIntent::GoldbergInnerVoiceCarrier:
      material->goldberg_inner_voice = line;
      return true;
    case VoiceIntent::PassacagliaVariation: {
      PassacagliaVariation decl;
      decl.voice = 0;
      decl.start_tick = span.start_tick;
      decl.end_tick = span.end_tick;
      decl.notes = line;
      material->passacaglia_variations.push_back(decl);
      return true;
    }
    case VoiceIntent::TrioVoiceCarrier: {
      TrioVoiceLine decl;
      decl.voice = 0;
      decl.notes = line;
      material->trio_voices.push_back(decl);
      return true;
    }
    case VoiceIntent::FantasiaCarrier: {
      FantasiaSection section;
      section.voice = 0;
      section.start_tick = span.start_tick;
      section.end_tick = span.end_tick;
      section.notes = line;
      material->fantasia_sections.push_back(section);
      return true;
    }
    case VoiceIntent::FortspinnungSpan: {
      SequenceTemplate tmpl;
      tmpl.voice = 0;
      tmpl.target_start_tick = kPrepTick;
      tmpl.num_steps = 1;
      tmpl.seed_pitches = {67, 65};
      tmpl.seed_durations = {kHeldDur, kTicksPerBeat};
      material->sequence_templates.push_back(tmpl);
      return true;
    }
    default:
      return false;
  }
}

TEST(SuspensionStampingTest, EveryReplayBranchStampsTheHeldNoteAndItsResolution) {
  const VoiceIntent intents[] = {VoiceIntent::SubjectCarrier,
                                 VoiceIntent::AnswerCarrier,
                                 VoiceIntent::CountersubjectCarrier,
                                 VoiceIntent::MiddleEntryCarrier,
                                 VoiceIntent::StrettoCarrier,
                                 VoiceIntent::CodaCarrier,
                                 VoiceIntent::SubjectCarrierInverted,
                                 VoiceIntent::RhythmCarrier,
                                 VoiceIntent::NctCarrier,
                                 VoiceIntent::ArpeggioFlow,
                                 VoiceIntent::VariationCarrier,
                                 VoiceIntent::FigurationCarrier,
                                 VoiceIntent::ToccataCarrier,
                                 VoiceIntent::CantusFirmusCarrier,
                                 VoiceIntent::GoldbergVariationCarrier,
                                 VoiceIntent::GoldbergInnerVoiceCarrier,
                                 VoiceIntent::PassacagliaVariation,
                                 VoiceIntent::TrioVoiceCarrier,
                                 VoiceIntent::FantasiaCarrier,
                                 VoiceIntent::FortspinnungSpan};
  const RuleIdMask prepared = ruleBitMask(RuleBit::SuspensionPrepared);
  const RuleIdMask resolved = ruleBitMask(RuleBit::SuspensionResolved);
  for (const VoiceIntent intent : intents) {
    SCOPED_TRACE(static_cast<int>(intent));
    Material material;
    material.suspension_patterns.push_back(fourThreePattern());
    ASSERT_TRUE(loadSource(intent, &material));
    CandidateSearch search;
    CandidateContext ctx;
    const auto cands = search.enumerate(replaySpan(intent), cMajor(), material, ctx);
    ASSERT_EQ(cands.size(), 2u);
    EXPECT_EQ(cands[0].duration, kHeldDur);
    EXPECT_TRUE((cands[0].satisfied_rules & prepared).any());
    EXPECT_FALSE((cands[0].satisfied_rules & resolved).any());
    EXPECT_TRUE((cands[1].satisfied_rules & resolved).any());
    EXPECT_FALSE((cands[1].satisfied_rules & prepared).any());
  }
}

TEST(SuspensionStampingTest, ARestruckSeedGetsNoPreparedBit) {
  Material material;
  material.suspension_patterns.push_back(fourThreePattern());
  SequenceTemplate tmpl;
  tmpl.voice = 0;
  tmpl.target_start_tick = kPrepTick;
  tmpl.num_steps = 1;
  tmpl.seed_pitches = {67, 67, 65};
  tmpl.seed_durations = {kTicksPerBeat, kTicksPerBeat, kTicksPerBeat};
  material.sequence_templates.push_back(tmpl);
  CandidateSearch search;
  CandidateContext ctx;
  const auto cands =
      search.enumerate(replaySpan(VoiceIntent::FortspinnungSpan), cMajor(), material, ctx);
  ASSERT_EQ(cands.size(), 3u);
  for (const auto& cand : cands)
    EXPECT_FALSE((cand.satisfied_rules & ruleBitMask(RuleBit::SuspensionPrepared)).any());
}

TEST(SuspensionStampingTest, ClippedHeldNoteGetsNoPreparedBit) {
  Material material;
  material.suspension_patterns.push_back(fourThreePattern());
  FigurationSection section;
  section.voice = 0;
  section.start_tick = kPrepTick;
  section.end_tick = kResTick + kTicksPerBeat;
  section.notes = heldAndResolution();
  material.figuration_sections.push_back(section);

  Span first = replaySpan(VoiceIntent::FigurationCarrier);
  first.end_tick = kSusTick;
  Span second = replaySpan(VoiceIntent::FigurationCarrier);
  second.start_tick = kSusTick;
  CandidateSearch search;
  CandidateContext ctx;
  const auto head = search.enumerate(first, cMajor(), material, ctx);
  const auto tail = search.enumerate(second, cMajor(), material, ctx);
  ASSERT_EQ(head.size(), 1u);
  EXPECT_EQ(head[0].duration, kSusTick - kPrepTick);
  EXPECT_FALSE((head[0].satisfied_rules & ruleBitMask(RuleBit::SuspensionPrepared)).any());
  ASSERT_GE(tail.size(), 1u);
  EXPECT_FALSE((tail[0].satisfied_rules & ruleBitMask(RuleBit::SuspensionPrepared)).any());
  EXPECT_TRUE((tail.back().satisfied_rules & ruleBitMask(RuleBit::SuspensionResolved)).any());
}

TEST(SuspensionStampingTest, ChainedNoteCarriesBothBitsAndTheValidatorAcceptsBothPatterns) {
  // 4-3 over C3 -> D3, whose resolution is the next 4-3's held tone over C3 -> C3.
  const Tick prep1 = 2 * kTicksPerBeat;
  const Tick sus1 = kTicksPerBar;
  const Tick res1 = sus1 + 2 * kTicksPerBeat;
  const Tick sus2 = 2 * kTicksPerBar;
  const Tick res2 = sus2 + 2 * kTicksPerBeat;
  Material material;
  material.suspension_patterns.push_back(
      {SuspensionType::Sus4_3, prep1, sus1, res1, 67, 67, 65, 0});
  material.suspension_patterns.push_back({SuspensionType::Sus4_3, res1, sus2, res2, 65, 65, 64, 0});
  FigurationSection section;
  section.voice = 0;
  section.start_tick = prep1;
  section.end_tick = res2 + kTicksPerBeat;
  section.notes = {{prep1, res1 - prep1, 67}, {res1, res2 - res1, 65}, {res2, kTicksPerBeat, 64}};
  material.figuration_sections.push_back(section);
  Span span = replaySpan(VoiceIntent::FigurationCarrier);
  span.start_tick = prep1;
  span.end_tick = section.end_tick;

  CandidateSearch search;
  CandidateContext ctx;
  const HarmonicPlan plan = cMajor();
  const auto cands = search.enumerate(span, plan, material, ctx);
  ASSERT_EQ(cands.size(), 3u);
  const RuleIdMask prepared = ruleBitMask(RuleBit::SuspensionPrepared);
  const RuleIdMask resolved = ruleBitMask(RuleBit::SuspensionResolved);
  EXPECT_TRUE((cands[0].satisfied_rules & prepared).any());
  EXPECT_TRUE((cands[1].satisfied_rules & prepared).any());
  EXPECT_TRUE((cands[1].satisfied_rules & resolved).any());
  EXPECT_TRUE((cands[2].satisfied_rules & resolved).any());
  EXPECT_FALSE((cands[2].satisfied_rules & prepared).any());

  std::vector<NoteEvent> notes;
  std::vector<NoteProvenance> prov;
  for (const auto& cand : cands) {
    notes.push_back(makeNote(cand.start_tick, cand.duration, cand.pitch, 0));
    NoteProvenance entry;
    entry.source = NoteSource::Material;
    entry.voice_intent = VoiceIntent::FigurationCarrier;
    entry.satisfied_rules = cand.satisfied_rules;
    prov.push_back(entry);
  }
  notes.push_back(makeNote(prep1, sus1 - prep1, 48, 1));
  notes.push_back(makeNote(sus1, sus2 - sus1, 50, 1));
  notes.push_back(makeNote(sus2, 2 * kTicksPerBar, 48, 1));
  prov.resize(notes.size());
  const ValidationReport report = Validator{}.validate(notes, prov, plan, material);
  EXPECT_FALSE(hasSuspensionFailure(report));
}

TEST(SuspensionStampingTest, NoteBitsFollowTheSoundingExtentAndPitch) {
  const std::vector<SuspensionPattern> patterns = {fourThreePattern()};
  const RuleIdMask prepared = ruleBitMask(RuleBit::SuspensionPrepared);
  const RuleIdMask resolved = ruleBitMask(RuleBit::SuspensionResolved);
  EXPECT_EQ(declaredSuspensionNoteBits(patterns, 0, kPrepTick, kHeldDur, 67), prepared);
  EXPECT_EQ(declaredSuspensionNoteBits(patterns, 0, kPrepTick, kHeldDur - 1, 67), RuleIdMask{});
  EXPECT_EQ(declaredSuspensionNoteBits(patterns, 0, kPrepTick, kHeldDur, 66), RuleIdMask{});
  EXPECT_EQ(declaredSuspensionNoteBits(patterns, 1, kPrepTick, kHeldDur, 67), RuleIdMask{});
  EXPECT_EQ(declaredSuspensionNoteBits(patterns, 0, kResTick, kTicksPerBeat, 65), resolved);
  EXPECT_EQ(declaredSuspensionNoteBits(patterns, 0, kResTick, kTicksPerBeat, 64), RuleIdMask{});
}

TEST(SuspensionStampingTest, HoldsRequiresTheHeldToneToOutlastItsResolutionDelay) {
  const HarmonicPlan plan = cMajor();
  DeclaredSuspensionReading ok = reading(67, 65, 48, 50, 50);
  EXPECT_TRUE(declaredSuspensionHolds(plan, ok, SuspensionType::Sus4_3));
  // Preparation one beat before the barline, resolution two beats after it.
  DeclaredSuspensionReading late = ok;
  late.resolution_tick = kTicksPerBar + 2 * kTicksPerBeat;
  EXPECT_FALSE(declaredSuspensionHolds(plan, late, SuspensionType::Sus4_3));
}

}  // namespace bach::composer
