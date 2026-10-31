#include "composer/validator.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include "composer/composer.h"
#include "composer/counterpoint_budget.h"
#include "composer/form_director.h"
#include "composer/harmonic_plan.h"
#include "composer/material.h"
#include "composer/motif_ops.h"
#include "composer/provenance.h"
#include "composer/rule_helpers.h"
#include "composer/span.h"
#include "composer/validation.h"
#include "composer/voice_onset_index.h"
#include "core/basic_types.h"

namespace bach::composer {
namespace {

// Single C major chord covering the whole piece.
HarmonicPlan cMajorWhole() {
  HarmonicPlan plan;
  plan.tonic_pc = 0;
  plan.is_minor = false;
  ChordEvent c;
  c.start_tick = 0;
  c.root_pc = 0;
  c.quality = ChordQuality::Major;
  plan.chords.push_back(c);
  return plan;
}

NoteEvent makeNote(Tick start, Tick dur, std::uint8_t pitch, VoiceId voice) {
  NoteEvent n;
  n.start_tick = start;
  n.duration = dur;
  n.pitch = pitch;
  n.voice = voice;
  n.velocity = 80;
  return n;
}

NoteProvenance makeProv(SpanId sid, NoteSource src) {
  NoteProvenance p;
  p.span_id = sid;
  p.source = src;
  return p;
}

NoteProvenance makeProvIntent(SpanId sid, NoteSource src, VoiceIntent intent) {
  NoteProvenance p = makeProv(sid, src);
  p.voice_intent = intent;
  return p;
}

void bindAuthoredNote(const NoteEvent& note, NoteProvenance* provenance) {
  provenance->has_authored_note = true;
  provenance->authored_start_tick = note.start_tick;
  provenance->authored_duration = note.duration;
  provenance->authored_pitch = note.pitch;
}

bool hasRule(const ValidationReport& r, const std::string& rule_id) {
  for (const auto& f : r.failures) {
    if (f.rule_id == rule_id)
      return true;
  }
  return false;
}

bool hasInformational(const ValidationReport& r, const std::string& rule_id) {
  for (const auto& f : r.informational) {
    if (f.rule_id == rule_id)
      return true;
  }
  return false;
}

HarmonicPlan cMajorWithCadence(CadenceType type) {
  HarmonicPlan plan = cMajorWhole();
  CadenceEvent cadence;
  cadence.tick = kTicksPerBeat;
  cadence.type = type;
  plan.cadences.push_back(cadence);
  return plan;
}

HarmonicPlan cMajorDominantWhole() {
  HarmonicPlan plan = cMajorWhole();
  plan.chords.front().root_pc = 7;
  plan.chords.front().quality = ChordQuality::Major;
  plan.chords.front().degree = RomanNumeral::V;
  plan.chords.front().function = HarmonicFunction::D;
  plan.chords.front().has_degree = true;
  return plan;
}

}  // namespace

TEST(ValidatorTest, EmptyInputPasses) {
  Validator v;
  ValidationReport r = v.validate({}, {}, cMajorWhole());
  EXPECT_EQ(r.status, ValidationStatus::Ok);
  EXPECT_TRUE(r.failures.empty());
}

TEST(ValidatorTest, TrioPolicyAllowsOneUpperVoiceExchangeOnly) {
  HarmonicPlan plan = cMajorWhole();
  plan.voice_crossing_policy = VoiceCrossingPolicy::AllowTrioUpperMomentary;
  const std::vector<NoteEvent> notes = {
      makeNote(0, kTicksPerBeat, 72, 0),
      makeNote(0, kTicksPerBeat, 60, 1),
      makeNote(kTicksPerBeat, kTicksPerBeat, 62, 0),
      makeNote(kTicksPerBeat, kTicksPerBeat, 64, 1),
      makeNote(2 * kTicksPerBeat, kTicksPerBeat, 72, 0),
      makeNote(2 * kTicksPerBeat, kTicksPerBeat, 60, 1),
  };
  std::vector<NoteProvenance> provenance(
      notes.size(), makeProvIntent(7, NoteSource::Material, VoiceIntent::TrioVoiceCarrier));

  const ValidationReport report = Validator{}.validate(notes, provenance, plan);
  EXPECT_FALSE(hasRule(report, "voice_crossing"));
  EXPECT_FALSE(hasRule(report, "parallel_fifth"));
  EXPECT_FALSE(hasRule(report, "parallel_octave"));
}

TEST(ValidatorTest, TrioPolicyRejectsSustainedOrNonTrioCrossing) {
  HarmonicPlan plan = cMajorWhole();
  plan.voice_crossing_policy = VoiceCrossingPolicy::AllowTrioUpperMomentary;
  const std::vector<NoteEvent> notes = {
      makeNote(0, kTicksPerBeat, 72, 0),
      makeNote(0, kTicksPerBeat, 60, 1),
      makeNote(kTicksPerBeat, kTicksPerBeat, 62, 0),
      makeNote(kTicksPerBeat, kTicksPerBeat, 64, 1),
      makeNote(2 * kTicksPerBeat, kTicksPerBeat, 61, 0),
      makeNote(2 * kTicksPerBeat, kTicksPerBeat, 65, 1),
  };
  std::vector<NoteProvenance> provenance(
      notes.size(), makeProvIntent(7, NoteSource::Material, VoiceIntent::TrioVoiceCarrier));
  EXPECT_TRUE(hasRule(Validator{}.validate(notes, provenance, plan), "voice_crossing"));

  provenance[3].voice_intent = VoiceIntent::SequentialCounterline;
  EXPECT_TRUE(hasRule(Validator{}.validate(notes, provenance, plan), "voice_crossing"));
}

TEST(ValidatorTest, TrioPolicyRequiresOverlappingManualRegisters) {
  HarmonicPlan plan = cMajorWhole();
  plan.voice_crossing_policy = VoiceCrossingPolicy::AllowTrioUpperMomentary;
  const std::vector<NoteEvent> notes = {
      makeNote(0, kTicksPerBeat, 84, 0),
      makeNote(kTicksPerBeat, kTicksPerBeat, 83, 0),
      makeNote(0, kTicksPerBeat, 60, 1),
      makeNote(kTicksPerBeat, kTicksPerBeat, 59, 1),
  };
  std::vector<NoteProvenance> provenance(
      notes.size(), makeProvIntent(7, NoteSource::Material, VoiceIntent::TrioVoiceCarrier));

  EXPECT_TRUE(
      hasRule(Validator{}.validate(notes, provenance, plan), "trio_upper_register_overlap"));
}

TEST(ValidatorTest, ReportsSubjectFeaturesAsInfoMetrics) {
  Material material;
  const std::uint8_t pitches[] = {60, 62, 64, 65, 67, 69, 71, 72};
  for (std::size_t i = 0; i < std::size(pitches); ++i) {
    MaterialNote note;
    note.start_tick = static_cast<Tick>(i) * kTicksPerBeat;
    note.duration = kTicksPerBeat;
    note.pitch = pitches[i];
    material.subject.push_back(note);
  }

  ValidationReport r = Validator{}.validate({}, {}, cMajorWhole(), material);
  EXPECT_EQ(r.status, ValidationStatus::Ok);
  ASSERT_EQ(r.subject_features.size(), 1u);
  EXPECT_EQ(r.subject_features[0].length, 8);
  EXPECT_EQ(r.subject_features[0].range_semitones, 12);
  EXPECT_EQ(r.subject_features[0].unique_pitch_classes, 7);
  EXPECT_EQ(r.subject_features[0].opening_interval, 2);
  EXPECT_EQ(r.subject_features[0].unique_intervals, 2);
  EXPECT_EQ(r.subject_features[0].max_leap, 2);
}

TEST(ValidatorTest, ReportsStreamSegregationCellDivergenceAsInfoMetrics) {
  Material material;
  const std::uint8_t pitches[] = {60, 65, 70, 75, 80, 85, 90, 95};
  for (std::size_t i = 0; i < std::size(pitches); ++i) {
    MaterialNote note;
    note.start_tick = static_cast<Tick>(i) * (kTicksPerBeat / 4);
    note.duration = kTicksPerBeat / 4;
    note.pitch = pitches[i];
    material.arpeggio_template.notes.push_back(note);
  }
  material.arpeggio_template.group_size = 4;

  ValidationReport r = Validator{}.validate({}, {}, cMajorWhole(), material);
  EXPECT_EQ(r.status, ValidationStatus::Ok);
  ASSERT_EQ(r.stream_segregation.size(), 1u);
  EXPECT_EQ(r.stream_segregation[0].detected_stream_count, 1);
  EXPECT_EQ(r.stream_segregation[0].cell_based_stream_count, 2);
  EXPECT_EQ(r.stream_segregation[0].cell_count, 2);
  EXPECT_TRUE(r.stream_segregation[0].disagrees_with_cell_counterpoint);
}

TEST(ValidatorTest, ReportsTextureMetricsAsInfoMetrics) {
  std::vector<NoteEvent> notes = {
      makeNote(0, kTicksPerBeat, 60, 0),
      makeNote(kTicksPerBeat, kTicksPerBeat, 60, 0),
      makeNote(kTicksPerBeat * 2, kTicksPerBeat, 62, 0),
      makeNote(kTicksPerBeat * 4, kTicksPerBeat, 97, 0),
      makeNote(0, kTicksPerBeat * 2, 48, 1),
      makeNote(kTicksPerBeat * 2, kTicksPerBeat * 2, 50, 1),
      makeNote(kTicksPerBeat * 4, kTicksPerBeat, 52, 1),
  };
  std::vector<NoteProvenance> prov(notes.size(), makeProv(0, NoteSource::Material));

  ValidationReport r = Validator{}.validate(notes, prov, cMajorWhole());
  ASSERT_EQ(r.texture_metrics.size(), 1u);
  const TextureMetrics& metrics = r.texture_metrics[0];
  EXPECT_EQ(metrics.max_active_voices, 2);
  EXPECT_NEAR(metrics.avg_active_voices, 1.8, 0.0001);
  // One segment [1440,1920) of width 480 over total span 2400 has exactly one
  // active voice (voice 1 sustains while voice 0 rests): 480 / 2400 = 0.2.
  EXPECT_NEAR(metrics.mono_ratio, 0.2, 0.0001);
  EXPECT_EQ(metrics.compass_violation_count, 1);
  EXPECT_NEAR(metrics.register_overlap_ratio, 0.0, 0.0001);
  ASSERT_EQ(metrics.voices.size(), 2u);
  EXPECT_EQ(metrics.voices[0].voice, 0);
  EXPECT_NEAR(metrics.voices[0].silence_ratio, 0.2, 0.0001);
  EXPECT_EQ(metrics.voices[0].max_repeated_run, 2);
  EXPECT_EQ(metrics.voices[0].min_pitch, 60);
  EXPECT_EQ(metrics.voices[0].max_pitch, 97);
  EXPECT_EQ(metrics.voices[1].voice, 1);
  EXPECT_NEAR(metrics.voices[1].silence_ratio, 0.0, 0.0001);
  EXPECT_EQ(metrics.voices[1].max_repeated_run, 1);
}

TEST(ValidatorTest, MonoRatioIsOneForSingleVoicePiece) {
  // A purely monophonic line: exactly one voice sounds across the whole span.
  std::vector<NoteEvent> notes = {
      makeNote(0, kTicksPerBeat, 60, 0),
      makeNote(kTicksPerBeat, kTicksPerBeat, 62, 0),
      makeNote(kTicksPerBeat * 2, kTicksPerBeat, 64, 0),
  };
  std::vector<NoteProvenance> prov(notes.size(), makeProv(0, NoteSource::Material));

  ValidationReport r = Validator{}.validate(notes, prov, cMajorWhole());
  ASSERT_EQ(r.texture_metrics.size(), 1u);
  EXPECT_NEAR(r.texture_metrics[0].mono_ratio, 1.0, 0.0001);
}

TEST(ValidatorTest, MonoRatioIsZeroForConstantTwoVoiceTexture) {
  // Two voices sound together for the entire span: never exactly one active.
  std::vector<NoteEvent> notes = {
      makeNote(0, kTicksPerBeat * 2, 60, 0),
      makeNote(kTicksPerBeat * 2, kTicksPerBeat * 2, 62, 0),
      makeNote(0, kTicksPerBeat * 2, 48, 1),
      makeNote(kTicksPerBeat * 2, kTicksPerBeat * 2, 50, 1),
  };
  std::vector<NoteProvenance> prov(notes.size(), makeProv(0, NoteSource::Material));

  ValidationReport r = Validator{}.validate(notes, prov, cMajorWhole());
  ASSERT_EQ(r.texture_metrics.size(), 1u);
  EXPECT_NEAR(r.texture_metrics[0].mono_ratio, 0.0, 0.0001);
}

TEST(ValidatorTest, MonoRatioIsHalfForHalfMonoHalfDuo) {
  // First half: two voices sound together (active = 2). Second half: only
  // voice 0 sounds (active = 1). Equal tick widths -> mono_ratio = 0.5.
  std::vector<NoteEvent> notes = {
      makeNote(0, kTicksPerBeat * 2, 60, 0),
      makeNote(kTicksPerBeat * 2, kTicksPerBeat * 2, 62, 0),
      makeNote(0, kTicksPerBeat * 2, 48, 1),
  };
  std::vector<NoteProvenance> prov(notes.size(), makeProv(0, NoteSource::Material));

  ValidationReport r = Validator{}.validate(notes, prov, cMajorWhole());
  ASSERT_EQ(r.texture_metrics.size(), 1u);
  EXPECT_NEAR(r.texture_metrics[0].mono_ratio, 0.5, 0.0001);
}

TEST(ValidatorTest, MonoRatioDefaultsToZero) {
  // The validator emits no texture metrics for empty input; the metric's
  // empty-input contract is the default-constructed value of 0.0.
  std::vector<NoteEvent> notes;
  std::vector<NoteProvenance> prov;

  ValidationReport r = Validator{}.validate(notes, prov, cMajorWhole());
  EXPECT_TRUE(r.texture_metrics.empty());

  const TextureMetrics empty_metrics;
  EXPECT_NEAR(empty_metrics.mono_ratio, 0.0, 0.0001);
}

TEST(ValidatorTest, StrongBeatConsonancePasses) {
  // Compose note on bar 0 beat 1, pitch = G (triad fifth of C). OK.
  std::vector<NoteEvent> notes = {makeNote(0, kTicksPerBeat, 67, 0)};
  std::vector<NoteProvenance> prov = {makeProv(0, NoteSource::Compose)};
  ValidationReport r = Validator{}.validate(notes, prov, cMajorWhole());
  EXPECT_EQ(r.status, ValidationStatus::Ok);
}

TEST(ValidatorTest, StrongBeatDissonanceFailsForComposeSource) {
  // F# (pc 6) on bar 0 beat 1 over C major triad {0,4,7}. Dissonant.
  std::vector<NoteEvent> notes = {makeNote(0, kTicksPerBeat, 66, 0)};
  std::vector<NoteProvenance> prov = {makeProv(0, NoteSource::Compose)};
  ValidationReport r = Validator{}.validate(notes, prov, cMajorWhole());
  EXPECT_EQ(r.status, ValidationStatus::FailedSpan);
  EXPECT_TRUE(hasRule(r, "strong_beat_dissonance"));
}

TEST(ValidatorTest, StrongBeatDissonanceIgnoredForMaterialSource) {
  // Same dissonance as above, but the note is sourced from Material. The
  // validator must not blame Material (those pitches are inputs, not search
  // outputs).
  std::vector<NoteEvent> notes = {makeNote(0, kTicksPerBeat, 66, 0)};
  std::vector<NoteProvenance> prov = {makeProv(0, NoteSource::Material)};
  ValidationReport r = Validator{}.validate(notes, prov, cMajorWhole());
  EXPECT_EQ(r.status, ValidationStatus::Ok);
}

TEST(ValidatorTest, CommonTimeBeatThreeDissonanceFailsButBeatTwoIsWeak) {
  std::vector<NoteProvenance> prov = {makeProv(0, NoteSource::Compose)};
  ValidationReport beat_two =
      Validator{}.validate({makeNote(kTicksPerBeat, kTicksPerBeat, 66, 0)}, prov, cMajorWhole());
  EXPECT_FALSE(hasRule(beat_two, "strong_beat_dissonance"));

  ValidationReport beat_three = Validator{}.validate(
      {makeNote(2 * kTicksPerBeat, kTicksPerBeat, 66, 0)}, prov, cMajorWhole());
  EXPECT_TRUE(hasRule(beat_three, "strong_beat_dissonance"));
}

TEST(ValidatorTest, SarabandeBeatTwoDissonanceFailsButStandardTripleAllowsIt) {
  HarmonicPlan standard = cMajorWhole();
  standard.ts_numerator = 3;
  standard.ts_denominator = 4;
  HarmonicPlan sarabande = standard;
  sarabande.meter_profile = MeterProfile::SarabandeTriple;
  const std::vector<NoteEvent> notes = {makeNote(kTicksPerBeat, kTicksPerBeat, 66, 0)};
  const std::vector<NoteProvenance> prov = {makeProv(0, NoteSource::Compose)};
  EXPECT_FALSE(hasRule(Validator{}.validate(notes, prov, standard), "strong_beat_dissonance"));
  EXPECT_TRUE(hasRule(Validator{}.validate(notes, prov, sarabande), "strong_beat_dissonance"));
}

TEST(ValidatorTest, ParallelFifthFails) {
  // Two voices, two beats. Voice 0: C5 → D5. Voice 1: F4 → G4.
  // Intervals: 60-53=7 (P5), 62-55=7 (P5). Identical perfect, both moved.
  std::vector<NoteEvent> notes = {
      makeNote(0, kTicksPerBeat, 60, 0),
      makeNote(0, kTicksPerBeat, 53, 1),
      makeNote(kTicksPerBeat, kTicksPerBeat, 62, 0),
      makeNote(kTicksPerBeat, kTicksPerBeat, 55, 1),
  };
  std::vector<NoteProvenance> prov(notes.size(), makeProv(0, NoteSource::Compose));
  ValidationReport r = Validator{}.validate(notes, prov, cMajorWhole());
  EXPECT_EQ(r.status, ValidationStatus::FailedSpan);
  EXPECT_TRUE(hasRule(r, "parallel_fifth"));
}

TEST(ValidatorTest, ParallelOctaveFails) {
  // Voice 0: C5 → D5 (60→62). Voice 1: C4 → D4 (48→50).
  // Intervals: 12, 12. Same perfect octave, both moved.
  std::vector<NoteEvent> notes = {
      makeNote(0, kTicksPerBeat, 60, 0),
      makeNote(0, kTicksPerBeat, 48, 1),
      makeNote(kTicksPerBeat, kTicksPerBeat, 62, 0),
      makeNote(kTicksPerBeat, kTicksPerBeat, 50, 1),
  };
  std::vector<NoteProvenance> prov(notes.size(), makeProv(0, NoteSource::Compose));
  ValidationReport r = Validator{}.validate(notes, prov, cMajorWhole());
  EXPECT_EQ(r.status, ValidationStatus::FailedSpan);
  EXPECT_TRUE(hasRule(r, "parallel_octave"));
}

namespace {

// The ParallelOctaveFails material: voice 0 C5 -> D5 over voice 1 C4 -> D4, an
// octave apart on both onsets. Read as two parts it is a parallel octave; read
// as one line doubled at the lower octave it is a registration effect.
std::vector<NoteEvent> octaveDoubledNotes() {
  return {
      makeNote(0, kTicksPerBeat, 60, 0),
      makeNote(0, kTicksPerBeat, 48, 1),
      makeNote(kTicksPerBeat, kTicksPerBeat, 62, 0),
      makeNote(kTicksPerBeat, kTicksPerBeat, 50, 1),
  };
}

DoublingWindow octaveDoubling(Tick start_tick, Tick end_tick, int semitones) {
  DoublingWindow window;
  window.lead_voice = 0;
  window.doubled_voice = 1;
  window.start_tick = start_tick;
  window.end_tick = end_tick;
  window.semitones = semitones;
  return window;
}

bool hasRuleKind(const ValidationReport& report, const std::string& rule_id, FailKind kind) {
  for (const ValidationFailure& failure : report.failures) {
    if (failure.rule_id == rule_id && failure.kind == kind)
      return true;
  }
  return false;
}

const RuleObservation* observationFor(const ValidationReport& report, const std::string& rule_id) {
  for (const RuleObservation& entry : report.observations) {
    if (entry.rule_id == rule_id)
      return &entry;
  }
  return nullptr;
}

}  // namespace

TEST(ValidatorTest, DeclaredDoublingReadsTheOctavePairAsOneLine) {
  const std::vector<NoteEvent> notes = octaveDoubledNotes();
  const std::vector<NoteProvenance> prov(notes.size(), makeProv(0, NoteSource::Compose));
  Material material;
  material.declared_doublings.push_back(octaveDoubling(0, 2 * kTicksPerBeat, -12));

  const ValidationReport r = Validator{}.validate(notes, prov, cMajorWhole(), material);
  EXPECT_FALSE(hasRule(r, "declared_doubling_integrity"));
  EXPECT_FALSE(hasRule(r, "parallel_octave"));
  // The finding is dropped before it is tallied: a line measured against itself
  // is not a counterpoint observation, and one left countable would keep the
  // rule open for every form that writes a doubling.
  EXPECT_EQ(observationFor(r, "parallel_octave"), nullptr);
  EXPECT_EQ(r.status, ValidationStatus::Ok);
}

TEST(ValidatorTest, DeclaredDoublingWithTheWrongIntervalTripsIntegrityAndIsNotHonoured) {
  // The notes are an octave apart; the declaration claims a major seventh. It
  // therefore describes something that is not in the score, which is a
  // structural error -- and the parallel it would have excused still fires.
  const std::vector<NoteEvent> notes = octaveDoubledNotes();
  const std::vector<NoteProvenance> prov(notes.size(), makeProv(0, NoteSource::Compose));
  Material material;
  material.declared_doublings.push_back(octaveDoubling(0, 2 * kTicksPerBeat, -11));

  const ValidationReport r = Validator{}.validate(notes, prov, cMajorWhole(), material);
  EXPECT_TRUE(hasRuleKind(r, "declared_doubling_integrity", FailKind::StructuralFail));
  EXPECT_TRUE(hasRule(r, "parallel_octave"));
  EXPECT_EQ(r.status, ValidationStatus::FailedSpan);
}

TEST(ValidatorTest, DeclaredDoublingWhoseVoicesCarryDifferentNoteCountsTripsIntegrity) {
  // The doubled voice sounds a tone the lead voice never states, so the two
  // streams are not one line however close they otherwise run.
  std::vector<NoteEvent> notes = octaveDoubledNotes();
  notes.push_back(makeNote(kTicksPerBeat / 2, kTicksPerBeat / 2, 49, 1));
  const std::vector<NoteProvenance> prov(notes.size(), makeProv(0, NoteSource::Compose));
  Material material;
  material.declared_doublings.push_back(octaveDoubling(0, 2 * kTicksPerBeat, -12));

  const ValidationReport r = Validator{}.validate(notes, prov, cMajorWhole(), material);
  EXPECT_TRUE(hasRuleKind(r, "declared_doubling_integrity", FailKind::StructuralFail));
}

TEST(ValidatorTest, DeclaredDoublingDoesNotReachOutsideItsOwnTicks) {
  // The window stops before the second onset, which is the one the parallel is
  // reported at. Outside the declared ticks the same two voices are ordinary
  // parts again, so the rule applies to them normally.
  const std::vector<NoteEvent> notes = octaveDoubledNotes();
  const std::vector<NoteProvenance> prov(notes.size(), makeProv(0, NoteSource::Compose));
  Material material;
  material.declared_doublings.push_back(octaveDoubling(0, kTicksPerBeat, -12));

  const ValidationReport r = Validator{}.validate(notes, prov, cMajorWhole(), material);
  EXPECT_FALSE(hasRule(r, "declared_doubling_integrity"));
  EXPECT_TRUE(hasRule(r, "parallel_octave"));
}

TEST(ValidatorTest, DeclaredDoublingOverAnEmptyWindowTripsIntegrity) {
  // A window naming no notes describes no line, so it cannot be a doubling.
  const std::vector<NoteEvent> notes = octaveDoubledNotes();
  const std::vector<NoteProvenance> prov(notes.size(), makeProv(0, NoteSource::Compose));
  Material material;
  material.declared_doublings.push_back(octaveDoubling(8 * kTicksPerBeat, 12 * kTicksPerBeat, -12));

  const ValidationReport r = Validator{}.validate(notes, prov, cMajorWhole(), material);
  EXPECT_TRUE(hasRuleKind(r, "declared_doubling_integrity", FailKind::StructuralFail));
}

TEST(ValidatorTest, ParallelFifthAcrossASilenceBetweenOnsetsPasses) {
  // The same two fifths as ParallelFifthFails, but both voices fall silent for
  // a beat in between and re-enter together. Nothing starts inside that
  // silence, so the walk over the union of onsets has no tick to notice the
  // rest at -- and the rule's own reason for breaking a succession at a rest is
  // that nobody hears motion across one.
  std::vector<NoteEvent> notes = {
      makeNote(0, kTicksPerBeat, 60, 0),
      makeNote(0, kTicksPerBeat, 53, 1),
      makeNote(3 * kTicksPerBeat, kTicksPerBeat, 62, 0),
      makeNote(3 * kTicksPerBeat, kTicksPerBeat, 55, 1),
  };
  std::vector<NoteProvenance> prov(notes.size(), makeProv(0, NoteSource::Compose));
  prov[1].source = NoteSource::Material;
  prov[3].source = NoteSource::Material;
  ValidationReport r = Validator{}.validate(notes, prov, cMajorWhole());
  EXPECT_FALSE(hasRule(r, "parallel_fifth"));
}

TEST(ValidatorTest, ParallelFifthAcrossOneVoicesSilencePasses) {
  // Only the lower voice drops out. The succession is broken by either voice's
  // silence, not only by both falling silent together.
  std::vector<NoteEvent> notes = {
      makeNote(0, 3 * kTicksPerBeat, 60, 0),
      makeNote(0, kTicksPerBeat, 53, 1),
      makeNote(3 * kTicksPerBeat, kTicksPerBeat, 62, 0),
      makeNote(3 * kTicksPerBeat, kTicksPerBeat, 55, 1),
  };
  std::vector<NoteProvenance> prov(notes.size(), makeProv(0, NoteSource::Compose));
  prov[1].source = NoteSource::Material;
  prov[3].source = NoteSource::Material;
  ValidationReport r = Validator{}.validate(notes, prov, cMajorWhole());
  EXPECT_FALSE(hasRule(r, "parallel_fifth"));
}

TEST(ValidatorTest, ParallelFifthAcrossAnUnbrokenSustainStillFails) {
  // The counterpart the two tests above must not weaken: the voices sustain
  // right up to the arrival with no silence anywhere, so the succession holds
  // and the fifths are still reported.
  std::vector<NoteEvent> notes = {
      makeNote(0, 3 * kTicksPerBeat, 60, 0),
      makeNote(0, 3 * kTicksPerBeat, 53, 1),
      makeNote(3 * kTicksPerBeat, kTicksPerBeat, 62, 0),
      makeNote(3 * kTicksPerBeat, kTicksPerBeat, 55, 1),
  };
  std::vector<NoteProvenance> prov(notes.size(), makeProv(0, NoteSource::Compose));
  prov[1].source = NoteSource::Material;
  prov[3].source = NoteSource::Material;
  ValidationReport r = Validator{}.validate(notes, prov, cMajorWhole());
  EXPECT_TRUE(hasRule(r, "parallel_fifth"));
}

TEST(ValidatorTest, OneStationaryVoicePasses) {
  // P5 → P5 but lower voice does not move (interval stays at 7 only because
  // the upper one moves to a different note that recreates a P5 — but here
  // the lower is held, so the interval changes). Specifically: upper C5→C5
  // (held), lower F3→F3 → interval is the same but neither voice moved.
  // That's a held P5, not a parallel motion. Detection requires *both* to
  // move; the current validator's heuristic (identical interval with both
  // changed positions) does not flag this.
  //
  // We model this by having upper hold a C5 across two beats while lower
  // holds an F3. Two distinct notes per voice with the same pitch — the
  // pitch-at-tick lookup yields {C5,F3} then {C5,F3}, identical interval.
  // Both endpoints contribute the SAME interval; the validator must not
  // confuse "held = parallel motion".
  std::vector<NoteEvent> notes = {
      makeNote(0, kTicksPerBeat, 60, 0),
      makeNote(0, kTicksPerBeat, 53, 1),
      makeNote(kTicksPerBeat, kTicksPerBeat, 60, 0),
      makeNote(kTicksPerBeat, kTicksPerBeat, 53, 1),
  };
  std::vector<NoteProvenance> prov(notes.size(), makeProv(0, NoteSource::Compose));
  // Use bar 2 onwards so strong-beat consonance is fine for both notes;
  // we already start at tick 0 which is also a strong beat. C5 (pc 0) is
  // root of C major triad, so consonant. F3 (pc 5) however is NOT in the
  // triad — to isolate the parallel-motion check, mark F3 voice as
  // Material so strong-beat-dissonance doesn't fire.
  prov[1].source = NoteSource::Material;
  prov[3].source = NoteSource::Material;
  ValidationReport r = Validator{}.validate(notes, prov, cMajorWhole());
  EXPECT_FALSE(hasRule(r, "parallel_fifth"));
  EXPECT_FALSE(hasRule(r, "parallel_octave"));
}

TEST(ValidatorTest, CleanVoiceOrderingPasses) {
  // Voice 0 (soprano) above voice 1 (alto): C5 over E4.
  std::vector<NoteEvent> notes = {
      makeNote(0, kTicksPerBeat, 60, 0),
      makeNote(0, kTicksPerBeat, 52, 1),
  };
  std::vector<NoteProvenance> prov(notes.size(), makeProv(0, NoteSource::Compose));
  ValidationReport r = Validator{}.validate(notes, prov, cMajorWhole());
  EXPECT_FALSE(hasRule(r, "voice_crossing"));
}

TEST(ValidatorTest, VoiceCrossingFails) {
  // Voice 0 (soprano) BELOW voice 1 (alto): E4 (52) under G4 (55).
  // By convention voice 0 should be above voice 1.
  std::vector<NoteEvent> notes = {
      makeNote(0, kTicksPerBeat, 52, 0),
      makeNote(0, kTicksPerBeat, 55, 1),
  };
  std::vector<NoteProvenance> prov(notes.size(), makeProv(0, NoteSource::Compose));
  ValidationReport r = Validator{}.validate(notes, prov, cMajorWhole());
  EXPECT_EQ(r.status, ValidationStatus::FailedSpan);
  EXPECT_TRUE(hasRule(r, "voice_crossing"));
}

TEST(ValidatorTest, VoiceCrossingIgnoredWhenOneVoiceSilent) {
  // Only voice 0 sounds at tick 0. No crossing to detect.
  std::vector<NoteEvent> notes = {
      makeNote(0, kTicksPerBeat, 60, 0),
      makeNote(kTicksPerBeat, kTicksPerBeat, 55, 1),
  };
  std::vector<NoteProvenance> prov(notes.size(), makeProv(0, NoteSource::Compose));
  ValidationReport r = Validator{}.validate(notes, prov, cMajorWhole());
  EXPECT_FALSE(hasRule(r, "voice_crossing"));
}

TEST(ValidatorTest, SingleLeapAlonePasses) {
  // C4 -> G4 is a P5 (7 semis), but it's the only motion. No
  // consecutive_leaps trigger.
  std::vector<NoteEvent> notes = {
      makeNote(0, kTicksPerBeat, 60, 0),
      makeNote(kTicksPerBeat, kTicksPerBeat, 67, 0),
  };
  std::vector<NoteProvenance> prov(notes.size(), makeProv(0, NoteSource::Compose));
  ValidationReport r = Validator{}.validate(notes, prov, cMajorWhole());
  EXPECT_FALSE(hasRule(r, "consecutive_leaps"));
}

TEST(ValidatorTest, LeapFollowedByStepPasses) {
  // C4 -> G4 (P5) -> A4 (step). Leap then step. OK.
  std::vector<NoteEvent> notes = {
      makeNote(0, kTicksPerBeat, 60, 0),
      makeNote(kTicksPerBeat, kTicksPerBeat, 67, 0),
      makeNote(2 * kTicksPerBeat, kTicksPerBeat, 69, 0),
  };
  std::vector<NoteProvenance> prov(notes.size(), makeProv(0, NoteSource::Compose));
  ValidationReport r = Validator{}.validate(notes, prov, cMajorWhole());
  EXPECT_FALSE(hasRule(r, "consecutive_leaps"));
}

TEST(ValidatorTest, ConsecutiveLeapsFails) {
  // C4 -> G4 (P5, 7 semis) -> D5 (P5, 7 semis). Two leaps in a row.
  std::vector<NoteEvent> notes = {
      makeNote(0, kTicksPerBeat, 60, 0),
      makeNote(kTicksPerBeat, kTicksPerBeat, 67, 0),
      makeNote(2 * kTicksPerBeat, kTicksPerBeat, 74, 0),
  };
  std::vector<NoteProvenance> prov(notes.size(), makeProv(0, NoteSource::Compose));
  ValidationReport r = Validator{}.validate(notes, prov, cMajorWhole());
  EXPECT_EQ(r.status, ValidationStatus::FailedSpan);
  EXPECT_TRUE(hasRule(r, "consecutive_leaps"));
}

TEST(ValidatorTest, ConsecutiveLeapsAcrossVoicesNotAggregated) {
  // Each voice has only one note, so no triple exists. No leap rule
  // can fire even though pitch differences span two voices.
  std::vector<NoteEvent> notes = {
      makeNote(0, kTicksPerBeat, 60, 0),
      makeNote(0, kTicksPerBeat, 53, 1),
  };
  std::vector<NoteProvenance> prov(notes.size(), makeProv(0, NoteSource::Compose));
  ValidationReport r = Validator{}.validate(notes, prov, cMajorWhole());
  EXPECT_FALSE(hasRule(r, "consecutive_leaps"));
}

TEST(ValidatorTest, WeakBeatPassingToneStepApproachAndLeavePasses) {
  // Voice 0: C5 (60, triad) -> D5 (62, non-triad on weak beat) -> E5
  // (64, triad). Both intervals are 2 semis (steps). D5 is a clean
  // passing tone — must not be flagged.
  std::vector<NoteEvent> notes = {
      makeNote(0, kTicksPerBeat, 60, 0),
      makeNote(kTicksPerBeat, kTicksPerBeat, 62, 0),
      makeNote(2 * kTicksPerBeat, kTicksPerBeat, 64, 0),
  };
  std::vector<NoteProvenance> prov(notes.size(), makeProv(0, NoteSource::Compose));
  ValidationReport r = Validator{}.validate(notes, prov, cMajorWhole());
  EXPECT_FALSE(hasRule(r, "unprepared_dissonance"));
}

TEST(ValidatorTest, WeakBeatNonChordToneLeapApproachFails) {
  // Voice 0: C5 (60, triad) -> A5 (69, non-triad on weak beat) -> G5
  // (67, triad). Approach is a leap of 9 semis (M6). The non-triad
  // middle note is not properly prepared.
  std::vector<NoteEvent> notes = {
      makeNote(0, kTicksPerBeat, 60, 0),
      makeNote(kTicksPerBeat, kTicksPerBeat, 69, 0),
      makeNote(2 * kTicksPerBeat, kTicksPerBeat, 67, 0),
  };
  std::vector<NoteProvenance> prov(notes.size(), makeProv(0, NoteSource::Compose));
  ValidationReport r = Validator{}.validate(notes, prov, cMajorWhole());
  EXPECT_EQ(r.status, ValidationStatus::FailedSpan);
  EXPECT_TRUE(hasRule(r, "unprepared_dissonance"));
}

TEST(ValidatorTest, WeakBeatNonChordToneLeapLeaveFails) {
  // Approach is OK (step), but leave is a leap. Still flagged.
  std::vector<NoteEvent> notes = {
      makeNote(0, kTicksPerBeat, 60, 0),
      makeNote(kTicksPerBeat, kTicksPerBeat, 62, 0),
      makeNote(2 * kTicksPerBeat, kTicksPerBeat, 69, 0),
  };
  std::vector<NoteProvenance> prov(notes.size(), makeProv(0, NoteSource::Compose));
  ValidationReport r = Validator{}.validate(notes, prov, cMajorWhole());
  EXPECT_TRUE(hasRule(r, "unprepared_dissonance"));
}

TEST(ValidatorTest, VoiceBoundaryNonChordToneExempt) {
  // The last note in a voice has no next; the passing-tone rule must
  // not fire when either prev or next is missing.
  std::vector<NoteEvent> notes = {
      makeNote(0, kTicksPerBeat, 60, 0),
      makeNote(kTicksPerBeat, kTicksPerBeat, 62, 0),
  };
  std::vector<NoteProvenance> prov(notes.size(), makeProv(0, NoteSource::Compose));
  ValidationReport r = Validator{}.validate(notes, prov, cMajorWhole());
  EXPECT_FALSE(hasRule(r, "unprepared_dissonance"));
}

TEST(ValidatorTest, WeakBeatNonChordToneOnMaterialSourceIgnored) {
  // Material-source notes are inputs, not search outputs. They must
  // not be subjected to candidate-search rules even when a dissonance
  // would otherwise trigger.
  std::vector<NoteEvent> notes = {
      makeNote(0, kTicksPerBeat, 60, 0),
      makeNote(kTicksPerBeat, kTicksPerBeat, 69, 0),
      makeNote(2 * kTicksPerBeat, kTicksPerBeat, 67, 0),
  };
  std::vector<NoteProvenance> prov(notes.size(), makeProv(0, NoteSource::Compose));
  prov[1].source = NoteSource::Material;  // the offending middle note
  ValidationReport r = Validator{}.validate(notes, prov, cMajorWhole());
  EXPECT_FALSE(hasRule(r, "unprepared_dissonance"));
}

TEST(ValidatorTest, FinalScoreAuditAcceptsConsonantResolvedMaterial) {
  std::vector<NoteEvent> notes = {
      makeNote(0, kTicksPerBeat, 60, 0),
      makeNote(kTicksPerBeat, kTicksPerBeat, 62, 0),
      makeNote(0, kTicksPerBeat, 52, 1),
      makeNote(kTicksPerBeat, kTicksPerBeat, 53, 1),
  };
  std::vector<NoteProvenance> prov(notes.size(), makeProv(0, NoteSource::Material));

  ValidationReport report =
      Validator{}.validate(notes, prov, cMajorWhole(), Material{}, ValidationScope::FinalScore);
  EXPECT_EQ(report.status, ValidationStatus::Ok);
  EXPECT_TRUE(report.failures.empty());
}

TEST(ValidatorTest, FinalScoreAuditCatchesMaterialParallelFifth) {
  std::vector<NoteEvent> notes = {
      makeNote(0, kTicksPerBeat, 60, 0),
      makeNote(kTicksPerBeat, kTicksPerBeat, 62, 0),
      makeNote(0, kTicksPerBeat, 53, 1),
      makeNote(kTicksPerBeat, kTicksPerBeat, 55, 1),
  };
  std::vector<NoteProvenance> prov(notes.size(), makeProv(0, NoteSource::Material));

  ValidationReport generation = Validator{}.validate(notes, prov, cMajorWhole());
  ValidationReport report =
      Validator{}.validate(notes, prov, cMajorWhole(), Material{}, ValidationScope::FinalScore);
  EXPECT_FALSE(hasRule(generation, "parallel_fifth"));
  EXPECT_EQ(report.status, ValidationStatus::FailedSpan);
  EXPECT_TRUE(hasRule(report, "parallel_fifth"));
}

TEST(ValidatorTest, FinalScoreReportsDeclaredMaterialParallelAsInformational) {
  std::vector<NoteEvent> notes = {
      makeNote(0, kTicksPerBeat, 60, 0),
      makeNote(kTicksPerBeat, kTicksPerBeat, 62, 0),
      makeNote(0, kTicksPerBeat, 53, 1),
      makeNote(kTicksPerBeat, kTicksPerBeat, 55, 1),
  };
  std::vector<NoteProvenance> prov(notes.size(), makeProv(0, NoteSource::Material));
  for (std::size_t i = 0; i < notes.size(); ++i)
    bindAuthoredNote(notes[i], &prov[i]);

  ValidationReport report =
      Validator{}.validate(notes, prov, cMajorWhole(), Material{}, ValidationScope::FinalScore);
  EXPECT_EQ(report.status, ValidationStatus::Ok);
  EXPECT_TRUE(report.failures.empty());
  EXPECT_TRUE(hasInformational(report, "parallel_fifth"));
}

TEST(ValidatorTest, FinalScoreAuditCatchesOrnamentParallelFifth) {
  std::vector<NoteEvent> notes = {
      makeNote(kTicksPerBeat, kTicksPerBeat, 60, 0),
      makeNote(2 * kTicksPerBeat, kTicksPerBeat, 62, 0),
      makeNote(kTicksPerBeat, kTicksPerBeat, 53, 1),
      makeNote(2 * kTicksPerBeat, kTicksPerBeat, 55, 1),
  };
  std::vector<NoteProvenance> prov(notes.size(), makeProv(0, NoteSource::Ornament));

  ValidationReport generation = Validator{}.validate(notes, prov, cMajorWhole());
  ValidationReport report =
      Validator{}.validate(notes, prov, cMajorWhole(), Material{}, ValidationScope::FinalScore);
  EXPECT_FALSE(hasRule(generation, "parallel_fifth"));
  EXPECT_EQ(report.status, ValidationStatus::FailedSpan);
  EXPECT_TRUE(hasRule(report, "parallel_fifth"));
}

TEST(ValidatorTest, FinalScoreAuditCatchesMaterialCrossRelation) {
  std::vector<NoteEvent> notes = {
      makeNote(kTicksPerBeat, kTicksPerBeat, 65, 0),
      makeNote(kTicksPerBeat, kTicksPerBeat, 66, 1),
  };
  std::vector<NoteProvenance> prov = {
      makeProv(3, NoteSource::Material),
      makeProv(4, NoteSource::Material),
  };

  ValidationReport generation = Validator{}.validate(notes, prov, cMajorWhole());
  ValidationReport report =
      Validator{}.validate(notes, prov, cMajorWhole(), Material{}, ValidationScope::FinalScore);
  EXPECT_FALSE(hasRule(generation, "cross_relation"));
  EXPECT_EQ(report.status, ValidationStatus::FailedSpan);
  EXPECT_TRUE(hasRule(report, "cross_relation"));
  bool found_span = false;
  for (const auto& failure : report.failures) {
    if (failure.rule_id == "cross_relation" && failure.span_id == 4) {
      found_span = true;
    }
  }
  EXPECT_TRUE(found_span);
}

TEST(ValidatorTest, FinalScoreAuditCatchesAccentedMaterialDissonance) {
  std::vector<NoteEvent> notes = {makeNote(0, kTicksPerBeat, 66, 0)};
  std::vector<NoteProvenance> prov = {makeProv(7, NoteSource::Material)};

  ValidationReport report =
      Validator{}.validate(notes, prov, cMajorWhole(), Material{}, ValidationScope::FinalScore);
  EXPECT_EQ(report.status, ValidationStatus::FailedSpan);
  EXPECT_TRUE(hasRule(report, "strong_beat_dissonance"));
}

TEST(ValidatorTest, FinalScoreReportsDeclaredAccentDissonanceAsInformational) {
  std::vector<NoteEvent> notes = {makeNote(0, kTicksPerBeat, 66, 0)};
  std::vector<NoteProvenance> prov = {makeProv(7, NoteSource::Material)};
  bindAuthoredNote(notes.front(), &prov.front());

  ValidationReport report =
      Validator{}.validate(notes, prov, cMajorWhole(), Material{}, ValidationScope::FinalScore);
  EXPECT_EQ(report.status, ValidationStatus::Ok);
  EXPECT_TRUE(report.failures.empty());
  EXPECT_TRUE(hasInformational(report, "strong_beat_dissonance"));
}

TEST(ValidatorTest, FinalScoreAuditCatchesMaterialMelodicLeap) {
  std::vector<NoteEvent> notes = {
      makeNote(0, kTicksPerBeat, 60, 0),
      makeNote(kTicksPerBeat, kTicksPerBeat, 66, 0),
  };
  std::vector<NoteProvenance> prov(notes.size(), makeProv(0, NoteSource::Material));

  ValidationReport report =
      Validator{}.validate(notes, prov, cMajorWhole(), Material{}, ValidationScope::FinalScore);
  EXPECT_EQ(report.status, ValidationStatus::FailedSpan);
  EXPECT_TRUE(hasRule(report, "tritone_melodic"));
}

TEST(ValidatorTest, FinalScoreAuditCatchesUnresolvedOrnamentLeadingTone) {
  std::vector<NoteEvent> notes = {
      makeNote(0, kTicksPerBeat, 71, 0),
      makeNote(kTicksPerBeat, kTicksPerBeat, 69, 0),
  };
  std::vector<NoteProvenance> prov(notes.size(), makeProv(0, NoteSource::Ornament));

  ValidationReport report = Validator{}.validate(notes, prov, cMajorDominantWhole(), Material{},
                                                 ValidationScope::FinalScore);
  EXPECT_EQ(report.status, ValidationStatus::FailedSpan);
  EXPECT_TRUE(hasRule(report, "leading_tone_resolution"));
}

TEST(ValidatorTest, FinalScoreAuditRejectsMissingProvenance) {
  std::vector<NoteEvent> notes = {makeNote(0, kTicksPerBeat, 60, 0)};

  ValidationReport report =
      Validator{}.validate(notes, {}, cMajorWhole(), Material{}, ValidationScope::FinalScore);
  ASSERT_EQ(report.status, ValidationStatus::FailedSpan);
  ASSERT_EQ(report.failures.size(), 1u);
  EXPECT_EQ(report.failures[0].rule_id, "note_provenance_alignment");
  EXPECT_EQ(report.failures[0].kind, FailKind::StructuralFail);
}

TEST(ValidatorTest, FinalScoreAuditAcceptsExactAuthoredCarrierContext) {
  std::vector<NoteEvent> notes = {makeNote(0, kTicksPerBeat, 66, 0)};
  std::vector<NoteProvenance> provenance = {makeProv(7, NoteSource::Material)};
  bindAuthoredNote(notes.front(), &provenance.front());

  const ValidationReport report = Validator{}.validate(notes, provenance, cMajorWhole(), Material{},
                                                       ValidationScope::FinalScore);
  EXPECT_EQ(report.status, ValidationStatus::Ok);
  EXPECT_FALSE(hasRule(report, "strong_beat_dissonance"));
}

TEST(ValidatorTest, FinalScoreAuditRejectsCarrierMutationAgainstAuthoredContext) {
  const NoteEvent authored = makeNote(0, kTicksPerBeat, 60, 0);
  std::vector<NoteEvent> notes = {authored};
  std::vector<NoteProvenance> provenance = {makeProv(7, NoteSource::Material)};
  bindAuthoredNote(authored, &provenance.front());
  notes.front().pitch = 66;

  const ValidationReport report = Validator{}.validate(notes, provenance, cMajorWhole(), Material{},
                                                       ValidationScope::FinalScore);
  EXPECT_EQ(report.status, ValidationStatus::FailedSpan);
  EXPECT_TRUE(hasRule(report, "carrier_declaration_integrity"));
  EXPECT_TRUE(hasRule(report, "strong_beat_dissonance"));
}

TEST(ValidatorTest, FinalScoreAuditBindsOrnamentToAuthoredCarrier) {
  const NoteEvent authored = makeNote(0, kTicksPerBeat, 60, 0);
  std::vector<NoteEvent> notes = {
      makeNote(0, kTicksPerBeat / 2, 62, 0),
      makeNote(kTicksPerBeat / 2, kTicksPerBeat / 2, 60, 0),
  };
  std::vector<NoteProvenance> provenance(notes.size(), makeProv(8, NoteSource::Ornament));
  for (auto& p : provenance) {
    bindAuthoredNote(authored, &p);
    p.satisfied_rules |= ruleBitMask(RuleBit::OrnamentRealized);
  }

  ValidationReport report = Validator{}.validate(notes, provenance, cMajorWhole(), Material{},
                                                 ValidationScope::FinalScore);
  EXPECT_EQ(report.status, ValidationStatus::Ok);

  notes.front().pitch = 66;
  report = Validator{}.validate(notes, provenance, cMajorWhole(), Material{},
                                ValidationScope::FinalScore);
  EXPECT_EQ(report.status, ValidationStatus::FailedSpan);
  EXPECT_TRUE(hasRule(report, "ornament_declaration_integrity"));

  notes = {makeNote(0, kTicksPerBeat / 2, 62, 0)};
  provenance.resize(1);
  report = Validator{}.validate(notes, provenance, cMajorWhole(), Material{},
                                ValidationScope::FinalScore);
  EXPECT_EQ(report.status, ValidationStatus::FailedSpan);
  EXPECT_TRUE(hasRule(report, "ornament_group_integrity"));
}

TEST(ValidatorTest, VerticalConsonancePasses) {
  // Voice 0 = C5 (60), voice 1 = E4 (52). Interval 8 (m6) is
  // consonant. Strong beat.
  std::vector<NoteEvent> notes = {
      makeNote(0, kTicksPerBeat, 60, 0),
      makeNote(0, kTicksPerBeat, 52, 1),
  };
  std::vector<NoteProvenance> prov(notes.size(), makeProv(0, NoteSource::Compose));
  ValidationReport r = Validator{}.validate(notes, prov, cMajorWhole());
  EXPECT_FALSE(hasRule(r, "vertical_dissonance"));
}

TEST(ValidatorTest, VerticalDissonanceOnStrongBeatFails) {
  // Voice 0 = G5 (79, Material), voice 1 = F4 (65, Compose). Interval
  // 14 -> 2 (M2) is dissonant. Strong beat. The Compose side gets the
  // blame.
  std::vector<NoteEvent> notes = {
      makeNote(0, kTicksPerBeat, 79, 0),
      makeNote(0, kTicksPerBeat, 65, 1),
  };
  std::vector<NoteProvenance> prov = {
      makeProv(0, NoteSource::Material),
      makeProv(1, NoteSource::Compose),
  };
  ValidationReport r = Validator{}.validate(notes, prov, cMajorWhole());
  EXPECT_EQ(r.status, ValidationStatus::FailedSpan);
  EXPECT_TRUE(hasRule(r, "vertical_dissonance"));
  // The flagged failure must reference the Compose-source span (id 1).
  bool found_compose_blame = false;
  for (const auto& f : r.failures) {
    if (f.rule_id == "vertical_dissonance" && f.span_id == 1) {
      found_compose_blame = true;
    }
  }
  EXPECT_TRUE(found_compose_blame);
}

TEST(ValidatorTest, VerticalDissonanceBothMaterialNotFlagged) {
  // Both notes are Material. Even though they form M2, the composer
  // cannot fix fixed inputs, so no failure is emitted.
  std::vector<NoteEvent> notes = {
      makeNote(0, kTicksPerBeat, 79, 0),
      makeNote(0, kTicksPerBeat, 65, 1),
  };
  std::vector<NoteProvenance> prov = {
      makeProv(0, NoteSource::Material),
      makeProv(1, NoteSource::Material),
  };
  ValidationReport r = Validator{}.validate(notes, prov, cMajorWhole());
  EXPECT_FALSE(hasRule(r, "vertical_dissonance"));
}

TEST(ValidatorTest, VerticalDissonanceOnWeakBeatNotFlagged) {
  // Dissonant pair on a weak beat is not flagged by the vertical
  // rule (weak-beat dissonance is governed by rule 4, not rule 5).
  std::vector<NoteEvent> notes = {
      makeNote(kTicksPerBeat, kTicksPerBeat, 79, 0),
      makeNote(kTicksPerBeat, kTicksPerBeat, 65, 1),
  };
  std::vector<NoteProvenance> prov(notes.size(), makeProv(0, NoteSource::Compose));
  ValidationReport r = Validator{}.validate(notes, prov, cMajorWhole());
  EXPECT_FALSE(hasRule(r, "vertical_dissonance"));
}

TEST(ValidatorTest, ContraryMotionThroughPerfectIsClean) {
  // Voice 0: C5 → G5 (60→67, +7). Voice 1: G3 → C3 (55→48, -7).
  // Intervals at the two ticks: 60-55=5 (P4), 67-48=19 ≡ 7 mod 12 (P5).
  // Different intervals → no parallel.
  std::vector<NoteEvent> notes = {
      makeNote(0, kTicksPerBeat, 60, 0),
      makeNote(0, kTicksPerBeat, 55, 1),
      makeNote(kTicksPerBeat, kTicksPerBeat, 67, 0),
      makeNote(kTicksPerBeat, kTicksPerBeat, 48, 1),
  };
  std::vector<NoteProvenance> prov(notes.size(), makeProv(0, NoteSource::Compose));
  ValidationReport r = Validator{}.validate(notes, prov, cMajorWhole());
  EXPECT_FALSE(hasRule(r, "parallel_fifth"));
  EXPECT_FALSE(hasRule(r, "parallel_octave"));
}

TEST(ValidatorTest, AugmentedMelodicIntervalFails) {
  std::vector<NoteEvent> notes = {
      makeNote(0, kTicksPerBeat, 60, 0),
      makeNote(kTicksPerBeat, kTicksPerBeat, 63, 0),
  };
  std::vector<NoteProvenance> prov(notes.size(), makeProv(0, NoteSource::Compose));
  ValidationReport r = Validator{}.validate(notes, prov, cMajorWhole());
  EXPECT_TRUE(hasRule(r, "augmented_melodic"));
}

TEST(ValidatorTest, TritoneMelodicFails) {
  std::vector<NoteEvent> notes = {
      makeNote(0, kTicksPerBeat, 60, 0),
      makeNote(kTicksPerBeat, kTicksPerBeat, 66, 0),
  };
  std::vector<NoteProvenance> prov(notes.size(), makeProv(0, NoteSource::Compose));
  ValidationReport r = Validator{}.validate(notes, prov, cMajorWhole());
  EXPECT_TRUE(hasRule(r, "tritone_melodic"));
}

TEST(ValidatorTest, DiminishedMelodicIntervalFails) {
  std::vector<NoteEvent> notes = {
      makeNote(0, kTicksPerBeat, 72, 0),
      makeNote(kTicksPerBeat, kTicksPerBeat, 61, 0),
  };
  std::vector<NoteProvenance> prov(notes.size(), makeProv(0, NoteSource::Compose));
  ValidationReport r = Validator{}.validate(notes, prov, cMajorWhole());
  EXPECT_TRUE(hasRule(r, "diminished_melodic"));
}

TEST(ValidatorTest, LeadingToneResolutionPassesWhenRaisedToTonic) {
  std::vector<NoteEvent> notes = {
      makeNote(0, kTicksPerBeat, 71, 0),
      makeNote(kTicksPerBeat, kTicksPerBeat, 72, 0),
  };
  std::vector<NoteProvenance> prov(notes.size(), makeProv(0, NoteSource::Compose));
  ValidationReport r = Validator{}.validate(notes, prov, cMajorDominantWhole());
  EXPECT_FALSE(hasRule(r, "leading_tone_resolution"));
}

TEST(ValidatorTest, LeadingToneResolutionFailsWhenUnresolved) {
  std::vector<NoteEvent> notes = {
      makeNote(0, kTicksPerBeat, 71, 0),
      makeNote(kTicksPerBeat, kTicksPerBeat, 69, 0),
  };
  std::vector<NoteProvenance> prov(notes.size(), makeProv(0, NoteSource::Compose));
  ValidationReport r = Validator{}.validate(notes, prov, cMajorDominantWhole());
  EXPECT_TRUE(hasRule(r, "leading_tone_resolution"));
}

TEST(ValidatorTest, MinorNaturalDescendingSixSevenAreContextualScaleTones) {
  HarmonicPlan plan = cMajorWhole();
  plan.is_minor = true;
  plan.chords.front().quality = ChordQuality::Minor;
  std::vector<NoteEvent> notes = {
      makeNote(0, kTicksPerBeat, 70, 0),
      makeNote(kTicksPerBeat, kTicksPerBeat, 68, 0),
  };
  std::vector<NoteProvenance> prov(notes.size(), makeProv(0, NoteSource::Compose));
  ValidationReport r = Validator{}.validate(notes, prov, plan);
  EXPECT_FALSE(hasRule(r, "augmented_melodic"));
  EXPECT_FALSE(hasRule(r, "leading_tone_resolution"));
}

TEST(ValidatorTest, MinorRaisedSixSevenAscendingAvoidsAugmentedSecond) {
  HarmonicPlan plan = cMajorWhole();
  plan.is_minor = true;
  plan.chords.front().quality = ChordQuality::Minor;
  std::vector<NoteEvent> notes = {
      makeNote(0, kTicksPerBeat, 68, 0),
      makeNote(kTicksPerBeat, kTicksPerBeat, 69, 0),
      makeNote(2 * kTicksPerBeat, kTicksPerBeat, 71, 0),
      makeNote(3 * kTicksPerBeat, kTicksPerBeat, 72, 0),
  };
  std::vector<NoteProvenance> prov(notes.size(), makeProv(0, NoteSource::Compose));
  ValidationReport r = Validator{}.validate(notes, prov, plan);
  EXPECT_FALSE(hasRule(r, "augmented_melodic"));
}

TEST(ValidatorTest, MinorDominantRequiresRaisedLeadingToneResolution) {
  HarmonicPlan plan = cMajorDominantWhole();
  plan.is_minor = true;
  std::vector<NoteEvent> notes = {
      makeNote(0, kTicksPerBeat, 71, 0),
      makeNote(kTicksPerBeat, kTicksPerBeat, 69, 0),
  };
  std::vector<NoteProvenance> prov(notes.size(), makeProv(0, NoteSource::Compose));
  ValidationReport r = Validator{}.validate(notes, prov, plan);
  EXPECT_TRUE(hasRule(r, "leading_tone_resolution"));
}

TEST(ValidatorTest, MinorModalSubtonicDoesNotActAsLeadingTone) {
  HarmonicPlan plan = cMajorWhole();
  plan.is_minor = true;
  plan.chords.front().quality = ChordQuality::Minor;
  std::vector<NoteEvent> notes = {
      makeNote(0, kTicksPerBeat, 70, 0),
      makeNote(kTicksPerBeat, kTicksPerBeat, 68, 0),
  };
  std::vector<NoteProvenance> prov(notes.size(), makeProv(0, NoteSource::Compose));
  ValidationReport r = Validator{}.validate(notes, prov, plan);
  EXPECT_FALSE(hasRule(r, "leading_tone_resolution"));
}

TEST(ValidatorTest, HiddenParallelFifthFails) {
  std::vector<NoteEvent> notes = {
      makeNote(0, kTicksPerBeat, 60, 0),
      makeNote(0, kTicksPerBeat, 52, 1),
      makeNote(kTicksPerBeat, kTicksPerBeat, 64, 0),
      makeNote(kTicksPerBeat, kTicksPerBeat, 57, 1),
  };
  std::vector<NoteProvenance> prov(notes.size(), makeProv(0, NoteSource::Compose));
  ValidationReport r = Validator{}.validate(notes, prov, cMajorWhole());
  EXPECT_TRUE(hasRule(r, "hidden_parallel_fifth"));
}

TEST(ValidatorTest, HiddenParallelOctaveFails) {
  std::vector<NoteEvent> notes = {
      makeNote(0, kTicksPerBeat, 60, 0),
      makeNote(0, kTicksPerBeat, 45, 1),
      makeNote(kTicksPerBeat, kTicksPerBeat, 64, 0),
      makeNote(kTicksPerBeat, kTicksPerBeat, 52, 1),
  };
  std::vector<NoteProvenance> prov(notes.size(), makeProv(0, NoteSource::Compose));
  ValidationReport r = Validator{}.validate(notes, prov, cMajorWhole());
  EXPECT_TRUE(hasRule(r, "hidden_parallel_octave"));
}

TEST(ValidatorTest, ContraryMotionIntoPerfectIsNotHiddenParallel) {
  std::vector<NoteEvent> notes = {
      makeNote(0, kTicksPerBeat, 60, 0),
      makeNote(0, kTicksPerBeat, 52, 1),
      makeNote(kTicksPerBeat, kTicksPerBeat, 62, 0),
      makeNote(kTicksPerBeat, kTicksPerBeat, 55, 1),
  };
  notes[3].pitch =
      67;  // lower-index convention aside, the pair no longer approaches by same direction.
  std::vector<NoteProvenance> prov(notes.size(), makeProv(0, NoteSource::Compose));
  ValidationReport r = Validator{}.validate(notes, prov, cMajorWhole());
  EXPECT_FALSE(hasRule(r, "hidden_parallel_fifth"));
  EXPECT_FALSE(hasRule(r, "hidden_parallel_octave"));
}

TEST(ValidatorTest, CrossRelationSimultaneousFails) {
  std::vector<NoteEvent> notes = {
      makeNote(0, kTicksPerBeat, 65, 0),  // F
      makeNote(0, kTicksPerBeat, 66, 1),  // F#
  };
  std::vector<NoteProvenance> prov(notes.size(), makeProv(0, NoteSource::Compose));
  ValidationReport r = Validator{}.validate(notes, prov, cMajorWhole());
  EXPECT_TRUE(hasRule(r, "cross_relation"));
}

TEST(ValidatorTest, CrossRelationAdjacentFails) {
  std::vector<NoteEvent> notes = {
      makeNote(0, kTicksPerBeat, 65, 0),
      makeNote(kTicksPerBeat, kTicksPerBeat, 66, 1),
  };
  std::vector<NoteProvenance> prov(notes.size(), makeProv(0, NoteSource::Compose));
  ValidationReport r = Validator{}.validate(notes, prov, cMajorWhole());
  EXPECT_TRUE(hasRule(r, "cross_relation"));
}

// The same pair, at the same distance, with each voice striking twice in
// between. A tick window reads them as successive; they are four onsets apart
// and nothing connects them, so the rule must not fire. Every note here is
// diatonic apart from the two that carry the inflection, so nothing else in the
// passage can raise the finding.
TEST(ValidatorTest, CrossRelationIgnoresOnsetsWithFigurationBetweenThem) {
  const Tick sixteenth = kTicksPerBeat / 4;
  std::vector<NoteEvent> notes = {
      makeNote(0, sixteenth, 65, 0),
      makeNote(sixteenth, sixteenth, 67, 0),
      makeNote(2 * sixteenth, sixteenth, 69, 0),
      makeNote(3 * sixteenth, sixteenth, 71, 0),
      makeNote(0, sixteenth, 48, 1),
      makeNote(sixteenth, sixteenth, 50, 1),
      makeNote(2 * sixteenth, sixteenth, 52, 1),
      makeNote(kTicksPerBeat, kTicksPerBeat, 66, 1),
  };
  std::vector<NoteProvenance> prov(notes.size(), makeProv(4, NoteSource::Compose));

  EXPECT_FALSE(hasRule(Validator{}.validate(notes, prov, cMajorWhole()), "cross_relation"));
}

TEST(ValidatorTest, NaturalHalfStepIsNotCrossRelation) {
  std::vector<NoteEvent> notes = {
      makeNote(0, kTicksPerBeat, 64, 0),  // E
      makeNote(0, kTicksPerBeat, 65, 1),  // F
  };
  std::vector<NoteProvenance> prov(notes.size(), makeProv(0, NoteSource::Compose));
  ValidationReport r = Validator{}.validate(notes, prov, cMajorWhole());
  EXPECT_FALSE(hasRule(r, "cross_relation"));
}

TEST(ValidatorTest, CadenceVoiceLeadingAcceptsAllCadenceTypes) {
  struct Case {
    CadenceType type;
    std::uint8_t upper_prev;
    std::uint8_t upper_now;
    std::uint8_t bass_prev;
    std::uint8_t bass_now;
  };
  const std::vector<Case> cases = {
      {CadenceType::Perfect, 71, 72, 55, 48},   {CadenceType::ImperfectAuthentic, 62, 64, 55, 48},
      {CadenceType::Plagal, 65, 64, 53, 48},    {CadenceType::Half, 64, 62, 48, 55},
      {CadenceType::Deceptive, 71, 72, 55, 57}, {CadenceType::Phrygian, 65, 67, 56, 55},
  };

  for (const auto& c : cases) {
    std::vector<NoteEvent> notes = {
        makeNote(0, kTicksPerBeat, c.upper_prev, 0),
        makeNote(0, kTicksPerBeat, c.bass_prev, 1),
        makeNote(kTicksPerBeat, kTicksPerBeat, c.upper_now, 0),
        makeNote(kTicksPerBeat, kTicksPerBeat, c.bass_now, 1),
    };
    std::vector<NoteProvenance> prov(notes.size(), makeProv(0, NoteSource::Material));
    ValidationReport r = Validator{}.validate(notes, prov, cMajorWithCadence(c.type));
    EXPECT_FALSE(hasRule(r, "cadence_voice_leading")) << static_cast<int>(c.type);
  }
}

TEST(ValidatorTest, HalfCadenceAcceptsDominantArrivalAfterBassRest) {
  HarmonicPlan plan = cMajorWithCadence(CadenceType::Half);
  std::vector<NoteEvent> notes = {
      makeNote(0, kTicksPerBeat, 64, 0),
      makeNote(kTicksPerBeat, kTicksPerBeat, 62, 0),
      makeNote(kTicksPerBeat, kTicksPerBeat, 55, 1),
  };
  std::vector<NoteProvenance> prov(notes.size(), makeProv(0, NoteSource::Material));
  const ValidationReport report = Validator{}.validate(notes, prov, plan);
  EXPECT_FALSE(hasRule(report, "cadence_voice_leading"));
}

TEST(ValidatorTest, HalfCadenceStillRejectsNonDominantBassArrival) {
  HarmonicPlan plan = cMajorWithCadence(CadenceType::Half);
  std::vector<NoteEvent> notes = {
      makeNote(0, kTicksPerBeat, 64, 0),
      makeNote(kTicksPerBeat, kTicksPerBeat, 60, 0),
      makeNote(kTicksPerBeat, kTicksPerBeat, 48, 1),
  };
  std::vector<NoteProvenance> prov(notes.size(), makeProv(0, NoteSource::Material));
  const ValidationReport report = Validator{}.validate(notes, prov, plan);
  EXPECT_TRUE(hasRule(report, "cadence_voice_leading"));
}

TEST(ValidatorTest, MinorDeceptiveCadenceResolvesBassToFlatSix) {
  HarmonicPlan plan = cMajorWithCadence(CadenceType::Deceptive);
  plan.is_minor = true;
  std::vector<NoteEvent> notes = {
      makeNote(0, kTicksPerBeat, 71, 0),
      makeNote(0, kTicksPerBeat, 55, 1),
      makeNote(kTicksPerBeat, kTicksPerBeat, 72, 0),
      makeNote(kTicksPerBeat, kTicksPerBeat, 56, 1),
  };
  std::vector<NoteProvenance> prov(notes.size(), makeProv(0, NoteSource::Material));
  const ValidationReport report = Validator{}.validate(notes, prov, plan);
  EXPECT_FALSE(hasRule(report, "cadence_voice_leading"));
}

namespace {

// C minor, V on beat one and the closing tonic on beat two, declared as a
// PicardyThird cadence; `flagged` sets is_picardy on the tonic.
HarmonicPlan cMinorPicardyClose(bool flagged) {
  HarmonicPlan plan;
  plan.tonic_pc = 0;
  plan.is_minor = true;
  ChordEvent dominant;
  dominant.start_tick = 0;
  dominant.root_pc = 7;
  dominant.quality = ChordQuality::Major;
  plan.chords.push_back(dominant);
  ChordEvent tonic;
  tonic.start_tick = kTicksPerBeat;
  tonic.root_pc = 0;
  tonic.quality = flagged ? ChordQuality::Major : ChordQuality::Minor;
  tonic.is_picardy = flagged;
  plan.chords.push_back(tonic);
  plan.cadences.push_back({kTicksPerBeat, CadenceType::PicardyThird});
  return plan;
}

// C minor iv6 -> V declared as a Phrygian cadence on beat two.
HarmonicPlan cMinorPhrygianClose() {
  HarmonicPlan plan;
  plan.tonic_pc = 0;
  plan.is_minor = true;
  ChordEvent subdominant;
  subdominant.start_tick = 0;
  subdominant.root_pc = 5;
  subdominant.quality = ChordQuality::Minor;
  subdominant.inversion = ChordInversion::First;
  plan.chords.push_back(subdominant);
  ChordEvent dominant;
  dominant.start_tick = kTicksPerBeat;
  dominant.root_pc = 7;
  dominant.quality = ChordQuality::Major;
  plan.chords.push_back(dominant);
  plan.cadences.push_back({kTicksPerBeat, CadenceType::Phrygian});
  return plan;
}

ValidationReport validateMaterial(const std::vector<NoteEvent>& notes, const HarmonicPlan& plan) {
  std::vector<NoteProvenance> prov(notes.size(), makeProv(0, NoteSource::Material));
  return Validator{}.validate(notes, prov, plan);
}

}  // namespace

// Textbook Picardy close: soprano B -> C, inner D -> E, bass G -> C.
TEST(ValidatorTest, PicardyCadenceAcceptsTextbookPolyphonicClose) {
  const std::vector<NoteEvent> notes = {
      makeNote(0, kTicksPerBeat, 71, 0),
      makeNote(0, kTicksPerBeat, 62, 1),
      makeNote(0, kTicksPerBeat, 55, 2),
      makeNote(kTicksPerBeat, kTicksPerBeat, 72, 0),
      makeNote(kTicksPerBeat, kTicksPerBeat, 64, 1),
      makeNote(kTicksPerBeat, kTicksPerBeat, 48, 2),
  };
  const ValidationReport report = validateMaterial(notes, cMinorPicardyClose(true));
  EXPECT_FALSE(hasRule(report, "cadence_voice_leading"));
  EXPECT_FALSE(hasRule(report, "picardy_third"));
}

// A single line resolves the leading tone and then states the raised third.
TEST(ValidatorTest, PicardyCadenceAcceptsTextbookMonophonicClose) {
  const std::vector<NoteEvent> notes = {
      makeNote(0, kTicksPerBeat, 71, 0),
      makeNote(kTicksPerBeat, kTicksPerBeat / 2, 72, 0),
      makeNote(kTicksPerBeat + kTicksPerBeat / 2, kTicksPerBeat / 2, 76, 0),
  };
  const ValidationReport report = validateMaterial(notes, cMinorPicardyClose(true));
  EXPECT_FALSE(hasRule(report, "cadence_voice_leading"));
  EXPECT_FALSE(hasRule(report, "picardy_third"));
}

TEST(ValidatorTest, PicardyChordWithMinorOrAbsentThirdFails) {
  for (const std::uint8_t inner : {std::uint8_t{63}, std::uint8_t{67}}) {
    const std::vector<NoteEvent> notes = {
        makeNote(0, kTicksPerBeat, 71, 0),
        makeNote(0, kTicksPerBeat, 62, 1),
        makeNote(0, kTicksPerBeat, 55, 2),
        makeNote(kTicksPerBeat, kTicksPerBeat, 72, 0),
        makeNote(kTicksPerBeat, kTicksPerBeat, inner, 1),
        makeNote(kTicksPerBeat, kTicksPerBeat, 48, 2),
    };
    EXPECT_TRUE(hasRule(validateMaterial(notes, cMinorPicardyClose(true)), "picardy_third"))
        << static_cast<int>(inner);
  }
  // A monophonic close that never states the raised third.
  const std::vector<NoteEvent> line = {
      makeNote(0, kTicksPerBeat, 71, 0),
      makeNote(kTicksPerBeat, kTicksPerBeat, 72, 0),
  };
  EXPECT_TRUE(hasRule(validateMaterial(line, cMinorPicardyClose(true)), "picardy_third"));
}

TEST(ValidatorTest, PicardyCadenceRequiresTheFlaggedChord) {
  const std::vector<NoteEvent> notes = {
      makeNote(0, kTicksPerBeat, 71, 0),
      makeNote(0, kTicksPerBeat, 62, 1),
      makeNote(0, kTicksPerBeat, 55, 2),
      makeNote(kTicksPerBeat, kTicksPerBeat, 72, 0),
      makeNote(kTicksPerBeat, kTicksPerBeat, 64, 1),
      makeNote(kTicksPerBeat, kTicksPerBeat, 48, 2),
  };
  EXPECT_TRUE(hasRule(validateMaterial(notes, cMinorPicardyClose(false)), "cadence_voice_leading"));
}

// Textbook Phrygian half cadence: bass Ab -> G under soprano F -> G.
TEST(ValidatorTest, PhrygianCadenceAcceptsTextbookVoicing) {
  const std::vector<NoteEvent> notes = {
      makeNote(0, kTicksPerBeat, 65, 0),
      makeNote(0, kTicksPerBeat, 60, 1),
      makeNote(0, kTicksPerBeat, 56, 2),
      makeNote(kTicksPerBeat, kTicksPerBeat, 67, 0),
      makeNote(kTicksPerBeat, kTicksPerBeat, 59, 1),
      makeNote(kTicksPerBeat, kTicksPerBeat, 55, 2),
  };
  EXPECT_FALSE(hasRule(validateMaterial(notes, cMinorPhrygianClose()), "cadence_voice_leading"));
  // The upper voice moving b2 -> 2 is not the Phrygian formula.
  std::vector<NoteEvent> wrong = notes;
  wrong[0].pitch = 61;
  wrong[3].pitch = 62;
  EXPECT_TRUE(hasRule(validateMaterial(wrong, cMinorPhrygianClose()), "cadence_voice_leading"));
  // A single line may state either outer voice.
  for (const auto& pair : {std::pair<std::uint8_t, std::uint8_t>{65, 67},
                           std::pair<std::uint8_t, std::uint8_t>{68, 67}}) {
    const std::vector<NoteEvent> line = {
        makeNote(0, kTicksPerBeat, pair.first, 0),
        makeNote(kTicksPerBeat, kTicksPerBeat, pair.second, 0),
    };
    EXPECT_FALSE(hasRule(validateMaterial(line, cMinorPhrygianClose()), "cadence_voice_leading"))
        << static_cast<int>(pair.first);
  }
}

// The cadence cells, realized literally, satisfy the validator branch for
// their own type in both the polyphonic and the monophonic path.
TEST(ValidatorTest, PicardyAndPhrygianCellsSatisfyTheirCadenceBranch) {
  for (const bool picardy : {true, false}) {
    const HarmonicPlan plan = picardy ? cMinorPicardyClose(true) : cMinorPhrygianClose();
    Material material;
    annotateCadenceCells(material, plan);
    ASSERT_EQ(material.cadence_cells.size(), 1u);
    const CadenceCell& cell = material.cadence_cells.front();
    const auto nearest = [](int pc, int target) {
      int pitch = target - 6;
      while (pitch % 12 != pc)
        ++pitch;
      return static_cast<std::uint8_t>(pitch);
    };
    const std::uint8_t sop_prev = nearest(cell.soprano_approach_pc, 72);
    const std::uint8_t sop_now = nearest(cell.soprano_cadence_pc, sop_prev);
    const std::uint8_t bass_prev = nearest(cell.bass_approach_pc, 52);
    const std::uint8_t bass_now = nearest(cell.bass_cadence_pc, bass_prev);
    const std::vector<NoteEvent> duet = {
        makeNote(0, kTicksPerBeat, sop_prev, 0),
        makeNote(0, kTicksPerBeat, bass_prev, 1),
        makeNote(kTicksPerBeat, kTicksPerBeat, sop_now, 0),
        makeNote(kTicksPerBeat, kTicksPerBeat, bass_now, 1),
    };
    const ValidationReport duet_report = validateMaterial(duet, plan);
    EXPECT_FALSE(hasRule(duet_report, "cadence_voice_leading")) << picardy;
    EXPECT_FALSE(hasRule(duet_report, "picardy_third")) << picardy;
    const std::vector<NoteEvent> line = {
        makeNote(0, kTicksPerBeat, sop_prev, 0),
        makeNote(kTicksPerBeat, kTicksPerBeat, sop_now, 0),
    };
    const ValidationReport line_report = validateMaterial(line, plan);
    EXPECT_FALSE(hasRule(line_report, "cadence_voice_leading")) << picardy;
    EXPECT_FALSE(hasRule(line_report, "picardy_third")) << picardy;
  }
}

TEST(ValidatorTest, MinorImperfectAuthenticAcceptsDeclaredPicardyThird) {
  HarmonicPlan plan = cMajorWithCadence(CadenceType::ImperfectAuthentic);
  plan.is_minor = true;
  ChordEvent final = plan.chords.front();
  final.start_tick = kTicksPerBeat;
  final.is_picardy = true;
  plan.chords.push_back(final);
  std::vector<NoteEvent> notes = {
      makeNote(0, kTicksPerBeat, 72, 0),
      makeNote(0, kTicksPerBeat, 55, 1),
      makeNote(kTicksPerBeat, kTicksPerBeat, 76, 0),
      makeNote(kTicksPerBeat, kTicksPerBeat, 48, 1),
  };
  std::vector<NoteProvenance> prov(notes.size(), makeProv(0, NoteSource::Material));
  const ValidationReport report = Validator{}.validate(notes, prov, plan);
  EXPECT_FALSE(hasRule(report, "cadence_voice_leading"));
}

TEST(ValidatorTest, AuthenticCadenceAcceptsDeclaredSecondInversionDominantBass) {
  HarmonicPlan plan = cMajorWithCadence(CadenceType::ImperfectAuthentic);
  plan.chords.front().root_pc = 7;
  plan.chords.front().quality = ChordQuality::Dominant7;
  plan.chords.front().degree = RomanNumeral::V;
  plan.chords.front().inversion = ChordInversion::Second;
  plan.chords.front().has_degree = true;
  ChordEvent tonic = plan.chords.front();
  tonic.start_tick = kTicksPerBeat;
  tonic.root_pc = 0;
  tonic.quality = ChordQuality::Major;
  tonic.degree = RomanNumeral::I;
  tonic.inversion = ChordInversion::Root;
  plan.chords.push_back(tonic);
  std::vector<NoteEvent> notes = {
      makeNote(0, kTicksPerBeat, 74, 0),
      makeNote(0, kTicksPerBeat, 62, 1),  // D: fifth of V, declared in the bass.
      makeNote(kTicksPerBeat, kTicksPerBeat, 76, 0),
      makeNote(kTicksPerBeat, kTicksPerBeat, 60, 1),
  };
  std::vector<NoteProvenance> prov(notes.size(), makeProv(0, NoteSource::Material));
  const ValidationReport report = Validator{}.validate(notes, prov, plan);
  EXPECT_FALSE(hasRule(report, "cadence_voice_leading"));
}

TEST(ValidatorTest, CadenceVoiceLeadingFailsBrokenPac) {
  std::vector<NoteEvent> notes = {
      makeNote(0, kTicksPerBeat, 69, 0),
      makeNote(0, kTicksPerBeat, 55, 1),
      makeNote(kTicksPerBeat, kTicksPerBeat, 72, 0),
      makeNote(kTicksPerBeat, kTicksPerBeat, 48, 1),
  };
  std::vector<NoteProvenance> prov(notes.size(), makeProv(0, NoteSource::Material));
  ValidationReport r = Validator{}.validate(notes, prov, cMajorWithCadence(CadenceType::Perfect));
  EXPECT_EQ(r.status, ValidationStatus::FailedSpan);
  EXPECT_TRUE(hasRule(r, "cadence_voice_leading"));
}

namespace {

// C major: V7 in `inversion` on beat one, root-position I on beat two, and a
// cadence of `type` on beat two. Upper B4 -> C5 over a bass of `bass_prev`
// -> C4.
ValidationReport validateDominantInversionCadence(CadenceType type, ChordInversion inversion,
                                                  std::uint8_t bass_prev) {
  HarmonicPlan plan = cMajorWithCadence(type);
  plan.chords.front().root_pc = 7;
  plan.chords.front().quality = ChordQuality::Dominant7;
  plan.chords.front().degree = RomanNumeral::V;
  plan.chords.front().inversion = inversion;
  plan.chords.front().has_degree = true;
  ChordEvent tonic = plan.chords.front();
  tonic.start_tick = kTicksPerBeat;
  tonic.root_pc = 0;
  tonic.quality = ChordQuality::Major;
  tonic.degree = RomanNumeral::I;
  tonic.inversion = ChordInversion::Root;
  plan.chords.push_back(tonic);
  std::vector<NoteEvent> notes = {
      makeNote(0, kTicksPerBeat, 71, 0),
      makeNote(0, kTicksPerBeat, bass_prev, 1),
      makeNote(kTicksPerBeat, kTicksPerBeat, 72, 0),
      makeNote(kTicksPerBeat, kTicksPerBeat, 60, 1),
  };
  std::vector<NoteProvenance> prov(notes.size(), makeProv(0, NoteSource::Material));
  return Validator{}.validate(notes, prov, plan);
}

}  // namespace

// A perfect authentic cadence needs both chords in root position: the bass
// states 5 -> 1. V4/3 (D in the bass) and V6 (B in the bass) are imperfect.
TEST(ValidatorTest, PerfectCadenceRejectsInvertedDominant) {
  EXPECT_TRUE(
      hasRule(validateDominantInversionCadence(CadenceType::Perfect, ChordInversion::Second, 62),
              "cadence_voice_leading"));
  EXPECT_TRUE(
      hasRule(validateDominantInversionCadence(CadenceType::Perfect, ChordInversion::First, 59),
              "cadence_voice_leading"));
}

TEST(ValidatorTest, PerfectCadenceAcceptsRootPositionDominantBass) {
  EXPECT_FALSE(
      hasRule(validateDominantInversionCadence(CadenceType::Perfect, ChordInversion::Root, 55),
              "cadence_voice_leading"));
}

// The inverted arrivals the perfect cadence refuses are imperfect authentic.
TEST(ValidatorTest, ImperfectAuthenticCadenceKeepsInvertedDominant) {
  EXPECT_FALSE(hasRule(
      validateDominantInversionCadence(CadenceType::ImperfectAuthentic, ChordInversion::Second, 62),
      "cadence_voice_leading"));
  EXPECT_FALSE(hasRule(
      validateDominantInversionCadence(CadenceType::ImperfectAuthentic, ChordInversion::First, 59),
      "cadence_voice_leading"));
}

TEST(ValidatorTest, CadenceMonophonicPerfectAcceptsLeadingToneResolution) {
  std::vector<NoteEvent> notes = {
      makeNote(0, kTicksPerBeat, 71, 0),
      makeNote(kTicksPerBeat, kTicksPerBeat, 72, 0),
  };
  std::vector<NoteProvenance> prov(notes.size(), makeProv(0, NoteSource::Material));
  ValidationReport r = Validator{}.validate(notes, prov, cMajorWithCadence(CadenceType::Perfect));
  EXPECT_FALSE(hasRule(r, "cadence_voice_leading"));
}

TEST(ValidatorTest, CadenceMonophonicBrokenResolutionClassifiedMusical) {
  std::vector<NoteEvent> notes = {
      makeNote(0, kTicksPerBeat, 69, 0),
      makeNote(kTicksPerBeat, kTicksPerBeat, 72, 0),
  };
  std::vector<NoteProvenance> prov(notes.size(), makeProv(0, NoteSource::Material));
  ValidationReport r = Validator{}.validate(notes, prov, cMajorWithCadence(CadenceType::Perfect));
  EXPECT_EQ(r.status, ValidationStatus::FailedSpan);
  ASSERT_TRUE(hasRule(r, "cadence_voice_leading"));
  for (const auto& f : r.failures) {
    if (f.rule_id == "cadence_voice_leading")
      EXPECT_EQ(f.kind, FailKind::MusicalFail);
  }
}

TEST(ValidatorTest, CadenceBeforeOneBeatClassifiedStructural) {
  HarmonicPlan plan = cMajorWithCadence(CadenceType::Perfect);
  plan.cadences.front().tick = 0;
  std::vector<NoteEvent> notes = {makeNote(0, kTicksPerBeat, 72, 0)};
  std::vector<NoteProvenance> prov(notes.size(), makeProv(0, NoteSource::Material));
  ValidationReport r = Validator{}.validate(notes, prov, plan);
  EXPECT_EQ(r.status, ValidationStatus::FailedSpan);
  bool found_structural = false;
  for (const auto& f : r.failures) {
    if (f.rule_id == "cadence_voice_leading") {
      EXPECT_EQ(f.kind, FailKind::StructuralFail);
      found_structural = true;
    }
  }
  EXPECT_TRUE(found_structural);
}

TEST(ValidatorTest, ValidationNeverMutatesNotesOrProvenance) {
  std::vector<NoteEvent> notes = {
      makeNote(0, kTicksPerBeat, 60, 0),
      makeNote(0, kTicksPerBeat, 53, 1),
      makeNote(kTicksPerBeat, kTicksPerBeat, 62, 0),
      makeNote(kTicksPerBeat, kTicksPerBeat, 55, 1),
  };
  std::vector<NoteProvenance> provenance = {
      makeProv(0, NoteSource::Compose),
      makeProv(1, NoteSource::Material),
      makeProv(2, NoteSource::Compose),
      makeProv(3, NoteSource::Material),
  };
  const std::vector<NoteEvent> notes_before = notes;
  const std::vector<NoteProvenance> provenance_before = provenance;

  static_cast<void>(Validator{}.validate(notes, provenance, cMajorWhole()));

  ASSERT_EQ(notes.size(), notes_before.size());
  ASSERT_EQ(provenance.size(), provenance_before.size());
  for (std::size_t i = 0; i < notes.size(); ++i) {
    EXPECT_EQ(notes[i].start_tick, notes_before[i].start_tick);
    EXPECT_EQ(notes[i].duration, notes_before[i].duration);
    EXPECT_EQ(notes[i].pitch, notes_before[i].pitch);
    EXPECT_EQ(notes[i].velocity, notes_before[i].velocity);
    EXPECT_EQ(notes[i].voice, notes_before[i].voice);
    EXPECT_EQ(notes[i].source, notes_before[i].source);
    EXPECT_EQ(provenance[i].span_id, provenance_before[i].span_id);
    EXPECT_EQ(provenance[i].source, provenance_before[i].source);
    EXPECT_EQ(provenance[i].voice_intent, provenance_before[i].voice_intent);
    EXPECT_EQ(provenance[i].satisfied_rules, provenance_before[i].satisfied_rules);
    EXPECT_EQ(provenance[i].rejected_alternatives, provenance_before[i].rejected_alternatives);
  }
}

TEST(ValidatorTest, MusicalViolationClassifiedMusical) {
  // F# (pc 6) on strong beat over C major is a strong_beat_dissonance.
  // Default classification is MusicalFail.
  std::vector<NoteEvent> notes = {makeNote(0, kTicksPerBeat, 66, 0)};
  std::vector<NoteProvenance> prov = {makeProv(0, NoteSource::Compose)};
  ValidationReport r = Validator{}.validate(notes, prov, cMajorWhole());
  ASSERT_FALSE(r.failures.empty());
  bool found_musical = false;
  for (const auto& f : r.failures) {
    if (f.rule_id == "strong_beat_dissonance") {
      EXPECT_EQ(f.kind, FailKind::MusicalFail);
      found_musical = true;
    }
  }
  EXPECT_TRUE(found_musical);
}

namespace {

// Build a minimal two-voice (V0 upper, V1 bass) layout with a single
// Sus4_3 in V0 against a C in V1. Beats: prep=beat0 (G over C = P5
// consonant), suspension=beat1 (G held = 4th over C = dissonant),
// resolution=beat2 (F-down... actually 4-3 means 4→3 so G→F-natural is
// step-down of -2 semis, F over C = 4th). For simplicity place V1's
// pitch class at G's preparation point so the preparation is consonant.
HarmonicPlan cMajorTwoBars() {
  HarmonicPlan plan;
  plan.tonic_pc = 0;
  plan.is_minor = false;
  ChordEvent c0;
  c0.start_tick = 0;
  c0.root_pc = 0;
  c0.quality = ChordQuality::Major;
  plan.chords.push_back(c0);
  return plan;
}

}  // namespace

TEST(ValidatorTest, SuspensionPreparationAndResolutionAcceptValidPattern) {
  // Sus7_6 in V0 over V1 bass at C: prep=B (M7-ish? we use a consonance
  // for prep). Concretely: V0 plays D5 (62) at beat 0 over C3 (48): M10
  // (semis = 14, mod 12 = 2)... pick consonant. Use V0=A4 (69) over
  // V1=C3 (48): interval 21 mod 12 = 9 (M6, consonant) — OK prep.
  // Tie A4 into beat 1 = suspension over the bass C3 → m7-ish (21 →
  // dissonant). Resolution: A4 → G4 (67) — step down of 2 semis. This
  // is a 7-6 pattern (m7 → m6).
  std::vector<NoteEvent> notes = {
      makeNote(0, kTicksPerBeat, 69, 0),                  // V0 prep A4
      makeNote(kTicksPerBeat, kTicksPerBeat, 69, 0),      // V0 suspension A4
      makeNote(kTicksPerBeat * 2, kTicksPerBeat, 67, 0),  // V0 resolution G4
      makeNote(0, kTicksPerBeat * 3, 48, 1),              // V1 bass C3 (held)
  };
  std::vector<NoteProvenance> prov(notes.size(), makeProv(0, NoteSource::Material));
  Material material;
  SuspensionPattern sp;
  sp.type = SuspensionType::Sus7_6;
  sp.preparation_tick = 0;
  sp.suspension_tick = kTicksPerBeat;
  sp.resolution_tick = kTicksPerBeat * 2;
  sp.preparation_pitch = 69;
  sp.suspension_pitch = 69;
  sp.resolution_pitch = 67;
  sp.voice = 0;
  material.suspension_patterns.push_back(sp);
  ValidationReport r = Validator{}.validate(notes, prov, cMajorTwoBars(), material);
  EXPECT_FALSE(hasRule(r, "suspension_preparation"));
  EXPECT_FALSE(hasRule(r, "suspension_resolution_step_down"));
}

TEST(ValidatorTest, DeclaredPreparedFourThreeAllowsAccentedFourthAboveBass) {
  const Tick prep_tick = 3 * kTicksPerBeat;
  const Tick suspension_tick = kTicksPerBar;
  const Tick resolution_tick = kTicksPerBar + kTicksPerBeat;
  std::vector<NoteEvent> notes = {
      makeNote(prep_tick, kTicksPerBeat, 67, 0),        // G over C: prepared P5
      makeNote(suspension_tick, kTicksPerBeat, 67, 0),  // G over D: P4
      makeNote(resolution_tick, kTicksPerBeat, 65, 0),  // F over D: m3
      makeNote(prep_tick, kTicksPerBeat, 48, 1),
      makeNote(suspension_tick, 2 * kTicksPerBeat, 50, 1),
  };
  std::vector<NoteProvenance> prov(notes.size(), makeProv(0, NoteSource::Compose));
  Material material;
  material.suspension_patterns.push_back(
      {SuspensionType::Sus4_3, prep_tick, suspension_tick, resolution_tick, 67, 67, 65, 0});
  ValidationReport r = Validator{}.validate(notes, prov, cMajorTwoBars(), material);
  EXPECT_FALSE(hasRule(r, "vertical_dissonance"));
  EXPECT_FALSE(hasRule(r, "suspension_preparation"));
  EXPECT_FALSE(hasRule(r, "suspension_preparation_duration"));
  EXPECT_FALSE(hasRule(r, "suspension_metrical_accent"));
  EXPECT_FALSE(hasRule(r, "suspension_interval"));
  EXPECT_FALSE(hasRule(r, "suspension_resolution_step_down"));
}

TEST(ValidatorTest, SuspensionPreparationMustBeAtLeastAsLongAsSuspension) {
  const Tick prep_tick = 3 * kTicksPerBeat;
  const Tick suspension_tick = kTicksPerBar;
  const Tick resolution_tick = kTicksPerBar + kTicksPerBeat;
  std::vector<NoteEvent> notes = {
      makeNote(prep_tick, kTicksPerBeat / 2, 67, 0),
      makeNote(suspension_tick, kTicksPerBeat, 67, 0),
      makeNote(resolution_tick, kTicksPerBeat, 65, 0),
      makeNote(prep_tick, kTicksPerBeat, 48, 1),
      makeNote(suspension_tick, 2 * kTicksPerBeat, 50, 1),
  };
  std::vector<NoteProvenance> prov(notes.size(), makeProv(0, NoteSource::Material));
  Material material;
  material.suspension_patterns.push_back(
      {SuspensionType::Sus4_3, prep_tick, suspension_tick, resolution_tick, 67, 67, 65, 0});
  const ValidationReport r = Validator{}.validate(notes, prov, cMajorTwoBars(), material);
  EXPECT_TRUE(hasRule(r, "suspension_preparation_duration"));
}

namespace {

// A 4-3 (G4 over C3 prepared, held over D3, resolving to F4) at the given
// ticks in 4/4 C major. The preparation lasts until the suspension.
ValidationReport validateTimedFourThree(Tick prep_tick, Tick suspension_tick,
                                        Tick resolution_tick) {
  std::vector<NoteEvent> notes = {
      makeNote(prep_tick, suspension_tick - prep_tick, 67, 0),
      makeNote(suspension_tick, resolution_tick - suspension_tick, 67, 0),
      makeNote(resolution_tick, kTicksPerBeat, 65, 0),
      makeNote(prep_tick, suspension_tick - prep_tick, 48, 1),
      makeNote(suspension_tick, resolution_tick + kTicksPerBeat - suspension_tick, 50, 1),
  };
  std::vector<NoteProvenance> prov(notes.size(), makeProv(0, NoteSource::Material));
  Material material;
  material.suspension_patterns.push_back(
      {SuspensionType::Sus4_3, prep_tick, suspension_tick, resolution_tick, 67, 67, 65, 0});
  return Validator{}.validate(notes, prov, cMajorTwoBars(), material);
}

}  // namespace

// The dissonance must fall on a beat stronger than its resolution:
// beat 2 (weak) resolving onto beat 3 (secondary accent) is backwards.
TEST(ValidatorTest, SuspensionWeakerThanItsResolutionFails) {
  const ValidationReport r = validateTimedFourThree(0, kTicksPerBeat, 2 * kTicksPerBeat);
  EXPECT_TRUE(hasRule(r, "suspension_metrical_accent"));
}

// Preparation on beat 1, dissonance on beat 3, resolution on beat 4: the
// preparation is the strongest of the three, which common practice allows.
TEST(ValidatorTest, SuspensionStrongerThanResolutionPassesWhateverThePreparation) {
  const ValidationReport r = validateTimedFourThree(0, 2 * kTicksPerBeat, 3 * kTicksPerBeat);
  EXPECT_FALSE(hasRule(r, "suspension_metrical_accent"));
  EXPECT_FALSE(hasRule(r, "suspension_preparation"));
  EXPECT_FALSE(hasRule(r, "suspension_preparation_duration"));
  EXPECT_FALSE(hasRule(r, "suspension_interval"));
  EXPECT_FALSE(hasRule(r, "suspension_resolution_step_down"));
}

// Equal strength is not stronger: beat 2 resolving on its own off-beat fails,
// while a downbeat resolving onto the beat-3 secondary accent passes.
TEST(ValidatorTest, SuspensionMetricalAccentRequiresStrictlyStrongerThanResolution) {
  EXPECT_TRUE(hasRule(validateTimedFourThree(0, kTicksPerBeat, kTicksPerBeat + kTicksPerBeat / 2),
                      "suspension_metrical_accent"));
  EXPECT_FALSE(hasRule(
      validateTimedFourThree(3 * kTicksPerBeat, kTicksPerBar, kTicksPerBar + 2 * kTicksPerBeat),
      "suspension_metrical_accent"));
}

TEST(ValidatorTest, SuspensionIntervalTableAcceptsAllFourTypes) {
  struct IntervalCase {
    SuspensionType type;
    std::uint8_t prep_pitch;
    std::uint8_t suspension_pitch;
    std::uint8_t resolution_pitch;
    std::uint8_t other_at_prep;
    std::uint8_t other_at_suspension;
    std::uint8_t other_at_resolution;
    VoiceId suspension_voice;
  };
  const IntervalCase cases[] = {
      {SuspensionType::Sus4_3, 67, 67, 65, 48, 50, 50, 0},
      {SuspensionType::Sus7_6, 70, 70, 69, 63, 60, 60, 0},
      {SuspensionType::Sus9_8, 74, 74, 72, 67, 60, 60, 0},
      {SuspensionType::Sus2_3, 50, 50, 48, 65, 64, 64, 1},
  };
  const Tick prep_tick = 3 * kTicksPerBeat;
  const Tick suspension_tick = kTicksPerBar;
  const Tick resolution_tick = kTicksPerBar + kTicksPerBeat;
  for (const IntervalCase& c : cases) {
    const VoiceId other_voice = c.suspension_voice == 0 ? 1 : 0;
    std::vector<NoteEvent> notes = {
        makeNote(prep_tick, kTicksPerBeat, c.prep_pitch, c.suspension_voice),
        makeNote(suspension_tick, kTicksPerBeat, c.suspension_pitch, c.suspension_voice),
        makeNote(resolution_tick, kTicksPerBeat, c.resolution_pitch, c.suspension_voice),
        makeNote(prep_tick, kTicksPerBeat, c.other_at_prep, other_voice),
        makeNote(suspension_tick, kTicksPerBeat, c.other_at_suspension, other_voice),
        makeNote(resolution_tick, kTicksPerBeat, c.other_at_resolution, other_voice),
    };
    std::vector<NoteProvenance> prov(notes.size(), makeProv(0, NoteSource::Material));
    Material material;
    material.suspension_patterns.push_back({c.type, prep_tick, suspension_tick, resolution_tick,
                                            c.prep_pitch, c.suspension_pitch, c.resolution_pitch,
                                            c.suspension_voice});
    const ValidationReport r = Validator{}.validate(notes, prov, cMajorTwoBars(), material);
    EXPECT_FALSE(hasRule(r, "suspension_preparation")) << static_cast<int>(c.type);
    EXPECT_FALSE(hasRule(r, "suspension_preparation_duration")) << static_cast<int>(c.type);
    EXPECT_FALSE(hasRule(r, "suspension_metrical_accent")) << static_cast<int>(c.type);
    EXPECT_FALSE(hasRule(r, "suspension_interval")) << static_cast<int>(c.type);
    EXPECT_FALSE(hasRule(r, "suspension_resolution_step_down")) << static_cast<int>(c.type);
  }
}

TEST(ValidatorTest, AccentedFourThreeWithoutDeclarationFails) {
  const Tick prep_tick = 3 * kTicksPerBeat;
  const Tick suspension_tick = kTicksPerBar;
  const Tick resolution_tick = kTicksPerBar + kTicksPerBeat;
  std::vector<NoteEvent> notes = {
      makeNote(prep_tick, kTicksPerBeat, 67, 0),
      makeNote(suspension_tick, kTicksPerBeat, 67, 0),
      makeNote(resolution_tick, kTicksPerBeat, 65, 0),
      makeNote(prep_tick, kTicksPerBeat, 48, 1),
      makeNote(suspension_tick, 2 * kTicksPerBeat, 50, 1),
  };
  std::vector<NoteProvenance> prov(notes.size(), makeProv(0, NoteSource::Compose));
  ValidationReport r = Validator{}.validate(notes, prov, cMajorTwoBars(), Material{});
  EXPECT_TRUE(hasRule(r, "vertical_dissonance"));
}

TEST(ValidatorTest, FourThreeDeclarationWithMismatchedResolutionFailsClosed) {
  const Tick prep_tick = 3 * kTicksPerBeat;
  const Tick suspension_tick = kTicksPerBar;
  const Tick resolution_tick = kTicksPerBar + kTicksPerBeat;
  std::vector<NoteEvent> notes = {
      makeNote(prep_tick, kTicksPerBeat, 67, 0),
      makeNote(suspension_tick, kTicksPerBeat, 67, 0),
      makeNote(resolution_tick, kTicksPerBeat, 65, 0),
      makeNote(prep_tick, kTicksPerBeat, 48, 1),
      makeNote(suspension_tick, 2 * kTicksPerBeat, 50, 1),
  };
  std::vector<NoteProvenance> prov(notes.size(), makeProv(0, NoteSource::Compose));
  Material material;
  material.suspension_patterns.push_back(
      {SuspensionType::Sus4_3, prep_tick, suspension_tick, resolution_tick, 67, 67, 64, 0});
  ValidationReport r = Validator{}.validate(notes, prov, cMajorTwoBars(), material);
  EXPECT_TRUE(hasRule(r, "vertical_dissonance"));
  EXPECT_TRUE(hasRule(r, "suspension_preparation"));
}

namespace {

// ii (D, `prep_quality`) for bar 0, V (G) for bar 1, in C major.
HarmonicPlan twoToFive(ChordQuality prep_quality) {
  HarmonicPlan plan;
  plan.tonic_pc = 0;
  plan.is_minor = false;
  ChordEvent two;
  two.start_tick = 0;
  two.root_pc = 2;
  two.quality = prep_quality;
  plan.chords.push_back(two);
  ChordEvent five;
  five.start_tick = kTicksPerBar;
  five.root_pc = 7;
  five.quality = ChordQuality::Major;
  plan.chords.push_back(five);
  return plan;
}

// A 4-3 over G2 whose fourth is prepared on beat 4 of the ii bar over D3.
ValidationReport validateIiFourThree(int prepared, ChordQuality prep_quality) {
  const Tick prep_tick = 3 * kTicksPerBeat;
  const Tick suspension_tick = kTicksPerBar;
  const Tick resolution_tick = kTicksPerBar + kTicksPerBeat;
  std::vector<NoteEvent> notes = {
      makeNote(prep_tick, kTicksPerBeat, static_cast<std::uint8_t>(prepared), 0),
      makeNote(suspension_tick, kTicksPerBeat, static_cast<std::uint8_t>(prepared), 0),
      makeNote(resolution_tick, kTicksPerBeat, static_cast<std::uint8_t>(prepared - 1), 0),
      makeNote(0, kTicksPerBar, 50, 1),                // D3 under the ii bar
      makeNote(suspension_tick, kTicksPerBar, 43, 1),  // G2 under V
  };
  std::vector<NoteProvenance> prov(notes.size(), makeProv(0, NoteSource::Material));
  Material material;
  material.suspension_patterns.push_back({SuspensionType::Sus4_3, prep_tick, suspension_tick,
                                          resolution_tick, static_cast<std::uint8_t>(prepared),
                                          static_cast<std::uint8_t>(prepared),
                                          static_cast<std::uint8_t>(prepared - 1), 0});
  return Validator{}.validate(notes, prov, twoToFive(prep_quality), material);
}

}  // namespace

// C5 over D3 is a minor seventh, dissonant over the bass, but it is the seventh
// of the declared ii7: the prepared seventh the ii7 - V 4-3 cadence rests on.
TEST(ValidatorTest, SuspensionPreparedAsTheDeclaredChordSeventhIsAccepted) {
  EXPECT_FALSE(hasRule(validateIiFourThree(72, ChordQuality::Minor7), "suspension_preparation"));
}

// The same C5 under a plain ii triad has no declared seventh to be, and a
// dissonant tone that belongs to no declared chord is refused whatever the
// chord: the exception is the declared seventh and nothing else.
TEST(ValidatorTest, DissonantPreparationOutsideTheDeclaredSeventhStillFails) {
  EXPECT_TRUE(hasRule(validateIiFourThree(72, ChordQuality::Minor), "suspension_preparation"));
  // E5 over D3 is a ninth, and E is no tone of D F A C.
  EXPECT_TRUE(hasRule(validateIiFourThree(76, ChordQuality::Minor7), "suspension_preparation"));
}

TEST(ValidatorTest, DeclaredCadentialSixFourAllowsFourthResolvingToDominantThird) {
  HarmonicPlan plan = cMajorWhole();
  ChordEvent dominant;
  dominant.start_tick = kTicksPerBeat;
  dominant.root_pc = 7;
  dominant.quality = ChordQuality::Dominant7;
  plan.chords.push_back(dominant);
  plan.cadential_six_fours.push_back({0, kTicksPerBeat, SixFourType::Cadential});
  std::vector<NoteEvent> notes = {
      makeNote(0, kTicksPerBeat, 60, 0),
      makeNote(kTicksPerBeat, kTicksPerBeat, 59, 0),
      makeNote(0, 2 * kTicksPerBeat, 43, 1),
  };
  std::vector<NoteProvenance> prov(notes.size(), makeProv(0, NoteSource::Compose));
  ValidationReport r = Validator{}.validate(notes, prov, plan, Material{});
  EXPECT_FALSE(hasRule(r, "vertical_dissonance"));
}

TEST(ValidatorTest, CadentialSixFourWithoutDeclarationFails) {
  HarmonicPlan plan = cMajorWhole();
  ChordEvent dominant;
  dominant.start_tick = kTicksPerBeat;
  dominant.root_pc = 7;
  dominant.quality = ChordQuality::Dominant7;
  plan.chords.push_back(dominant);
  std::vector<NoteEvent> notes = {
      makeNote(0, kTicksPerBeat, 60, 0),
      makeNote(kTicksPerBeat, kTicksPerBeat, 59, 0),
      makeNote(0, 2 * kTicksPerBeat, 43, 1),
  };
  std::vector<NoteProvenance> prov(notes.size(), makeProv(0, NoteSource::Compose));
  ValidationReport r = Validator{}.validate(notes, prov, plan, Material{});
  EXPECT_TRUE(hasRule(r, "vertical_dissonance"));
}

TEST(ValidatorTest, CadentialSixFourWithWrongResolutionFailsClosed) {
  HarmonicPlan plan = cMajorWhole();
  ChordEvent dominant;
  dominant.start_tick = kTicksPerBeat;
  dominant.root_pc = 7;
  dominant.quality = ChordQuality::Dominant7;
  plan.chords.push_back(dominant);
  plan.cadential_six_fours.push_back({0, kTicksPerBeat, SixFourType::Cadential});
  std::vector<NoteEvent> notes = {
      makeNote(0, kTicksPerBeat, 60, 0),
      makeNote(kTicksPerBeat, kTicksPerBeat, 58, 0),
      makeNote(0, 2 * kTicksPerBeat, 43, 1),
  };
  std::vector<NoteProvenance> prov(notes.size(), makeProv(0, NoteSource::Compose));
  ValidationReport r = Validator{}.validate(notes, prov, plan, Material{});
  EXPECT_TRUE(hasRule(r, "vertical_dissonance"));
}

namespace {

// One downbeat sonority in C major over a G chord of `quality`, every note
// generated so the vertical rule is evaluated rather than exempted.
ValidationReport validateOverG(ChordQuality quality, const std::vector<std::uint8_t>& pitches) {
  HarmonicPlan plan;
  plan.tonic_pc = 0;
  ChordEvent chord;
  chord.root_pc = 7;
  chord.quality = quality;
  plan.chords.push_back(chord);
  std::vector<NoteEvent> notes;
  for (std::size_t voice = 0; voice < pitches.size(); ++voice)
    notes.push_back(makeNote(0, kTicksPerBeat, pitches[voice], static_cast<VoiceId>(voice)));
  std::vector<NoteProvenance> prov(notes.size(), makeProv(0, NoteSource::Compose));
  return Validator{}.validate(notes, prov, plan, Material{});
}

}  // namespace

// F4 / B3 / G2 under a declared G7: the tritone F-B and the minor seventh F-G
// are both intervals inside the declared chord, which strong_beat_dissonance
// already reads as harmony; the vertical rule agrees.
TEST(ValidatorTest, VerticalPairInsideDeclaredSeventhChordIsLicensed) {
  const ValidationReport r = validateOverG(ChordQuality::Dominant7, {65, 59, 43});
  EXPECT_FALSE(hasRule(r, "vertical_dissonance"));
  EXPECT_FALSE(hasRule(r, "strong_beat_dissonance"));
}

// The same sonority under a plain G triad declares no seventh to be.
TEST(ValidatorTest, SeventhSonorityUnderATriadStillFails) {
  EXPECT_TRUE(hasRule(validateOverG(ChordQuality::Major, {65, 59, 43}), "vertical_dissonance"));
}

// A dissonance with a tone outside the declared chord is not licensed: A4 is a
// ninth over G2 and no tone of G B D F.
TEST(ValidatorTest, NonMemberDissonanceUnderSeventhChordStillFails) {
  EXPECT_TRUE(hasRule(validateOverG(ChordQuality::Dominant7, {69, 59, 43}), "vertical_dissonance"));
}

// A fourth over the bass is licensed only as part of the declared seventh
// sonority (V4/3: G over D); the same fourth over a triad, or a fourth whose
// upper tone is foreign to the seventh chord, still fails.
TEST(ValidatorTest, FourthOverBassFollowsTheDeclaredSeventhSonority) {
  EXPECT_FALSE(hasRule(validateOverG(ChordQuality::Dominant7, {55, 50}), "vertical_dissonance"));
  EXPECT_TRUE(hasRule(validateOverG(ChordQuality::Major, {55, 50}), "vertical_dissonance"));
  EXPECT_TRUE(hasRule(validateOverG(ChordQuality::Dominant7, {60, 55}), "vertical_dissonance"));
}

TEST(ValidatorTest, SuspensionResolutionFailsOnLeap) {
  // Sus7_6 with V0 prep=A4 → sus=A4 → resolution=D4 (62), a 7-semi leap
  // instead of a step. Resolution rule must fire.
  std::vector<NoteEvent> notes = {
      makeNote(0, kTicksPerBeat, 69, 0),
      makeNote(kTicksPerBeat, kTicksPerBeat, 69, 0),
      makeNote(kTicksPerBeat * 2, kTicksPerBeat, 62, 0),
      makeNote(0, kTicksPerBeat * 3, 48, 1),
  };
  std::vector<NoteProvenance> prov(notes.size(), makeProv(0, NoteSource::Material));
  Material material;
  SuspensionPattern sp;
  sp.type = SuspensionType::Sus7_6;
  sp.preparation_tick = 0;
  sp.suspension_tick = kTicksPerBeat;
  sp.resolution_tick = kTicksPerBeat * 2;
  sp.preparation_pitch = 69;
  sp.suspension_pitch = 69;
  sp.resolution_pitch = 62;
  sp.voice = 0;
  material.suspension_patterns.push_back(sp);
  ValidationReport r = Validator{}.validate(notes, prov, cMajorTwoBars(), material);
  EXPECT_EQ(r.status, ValidationStatus::FailedSpan);
  EXPECT_TRUE(hasRule(r, "suspension_resolution_step_down"));
}

TEST(ValidatorTest, SuspensionPreparationFailsWhenNotTied) {
  // Sus7_6 declared prep=A4 but the V0 prep note actually plays G4 (67)
  // and only jumps to A4 on the suspension — no tie. Preparation rule
  // must fire because prep_actual (G4) != sus_actual (A4).
  std::vector<NoteEvent> notes = {
      makeNote(0, kTicksPerBeat, 67, 0),                  // prep G4 (NOT tied)
      makeNote(kTicksPerBeat, kTicksPerBeat, 69, 0),      // sus A4
      makeNote(kTicksPerBeat * 2, kTicksPerBeat, 67, 0),  // res G4
      makeNote(0, kTicksPerBeat * 3, 48, 1),
  };
  std::vector<NoteProvenance> prov(notes.size(), makeProv(0, NoteSource::Material));
  Material material;
  SuspensionPattern sp;
  sp.type = SuspensionType::Sus7_6;
  sp.preparation_tick = 0;
  sp.suspension_tick = kTicksPerBeat;
  sp.resolution_tick = kTicksPerBeat * 2;
  sp.preparation_pitch = 67;
  sp.suspension_pitch = 69;
  sp.resolution_pitch = 67;
  sp.voice = 0;
  material.suspension_patterns.push_back(sp);
  ValidationReport r = Validator{}.validate(notes, prov, cMajorTwoBars(), material);
  EXPECT_EQ(r.status, ValidationStatus::FailedSpan);
  EXPECT_TRUE(hasRule(r, "suspension_preparation"));
}

namespace {

// A 2-3 bass suspension across the bar line in C major: V1 prepares
// `bass_prep` under V0's `upper_prep` on beat 4, holds it into the downbeat
// while V0 moves to `upper_sus`, then moves to `bass_res` on beat 2 under V0's
// `upper_res`. `suspended_voice` names the voice the declaration suspends.
struct TwoThreeCase {
  std::uint8_t upper_prep = 65;  // F4 over D3: a tenth.
  std::uint8_t upper_sus = 64;   // E4 over the held D3: a ninth.
  std::uint8_t upper_res = 64;   // E4 over C3: a tenth.
  std::uint8_t bass_prep = 50;
  std::uint8_t bass_res = 48;
  VoiceId suspended_voice = 1;
  bool declared = true;
};

ValidationReport validateTwoThree(const TwoThreeCase& c) {
  const Tick prep_tick = 3 * kTicksPerBeat;
  const Tick suspension_tick = kTicksPerBar;
  const Tick resolution_tick = kTicksPerBar + kTicksPerBeat;
  std::vector<NoteEvent> notes = {
      makeNote(prep_tick, kTicksPerBeat, c.upper_prep, 0),
      makeNote(suspension_tick, kTicksPerBeat, c.upper_sus, 0),
      makeNote(resolution_tick, kTicksPerBeat, c.upper_res, 0),
      makeNote(prep_tick, kTicksPerBeat, c.bass_prep, 1),
      makeNote(suspension_tick, kTicksPerBeat, c.bass_prep, 1),
      makeNote(resolution_tick, kTicksPerBeat, c.bass_res, 1),
  };
  std::vector<NoteProvenance> prov(notes.size(), makeProv(0, NoteSource::Compose));
  Material material;
  if (c.declared) {
    const bool lower = c.suspended_voice == 1;
    const std::uint8_t held = lower ? c.bass_prep : c.upper_sus;
    material.suspension_patterns.push_back({SuspensionType::Sus2_3, prep_tick, suspension_tick,
                                            resolution_tick, held, held,
                                            lower ? c.bass_res : c.upper_res, c.suspended_voice});
  }
  return Validator{}.validate(notes, prov, cMajorTwoBars(), material);
}

bool hasAnySuspensionFailure(const ValidationReport& r) {
  return hasRule(r, "suspension_preparation") || hasRule(r, "suspension_preparation_duration") ||
         hasRule(r, "suspension_metrical_accent") || hasRule(r, "suspension_interval") ||
         hasRule(r, "suspension_resolution_step_down");
}

}  // namespace

// The 2-3 is a bass suspension: the held lower voice sits a ninth under the
// upper voice on the accent, then steps down to make a tenth. The declared
// figure satisfies every suspension rule and licenses its accented ninth.
TEST(ValidatorTest, Sus2_3BassResolvingDownToATenthPasses) {
  const ValidationReport r = validateTwoThree({});
  EXPECT_FALSE(hasAnySuspensionFailure(r));
  EXPECT_FALSE(hasRule(r, "vertical_dissonance"));
}

TEST(ValidatorTest, Sus2_3UndeclaredNinthOverTheBassFails) {
  TwoThreeCase undeclared;
  undeclared.declared = false;
  EXPECT_TRUE(hasRule(validateTwoThree(undeclared), "vertical_dissonance"));
}

// The bass rising out of the second is not a suspension resolution at all,
// so the step rule fails and the accented second is not licensed.
TEST(ValidatorTest, Sus2_3RisingBassFailsResolutionAndLicensesNothing) {
  TwoThreeCase rising;
  rising.bass_res = 52;   // D3 -> E3
  rising.upper_res = 67;  // G4 over E3: a tenth again.
  const ValidationReport r = validateTwoThree(rising);
  EXPECT_TRUE(hasRule(r, "suspension_resolution_step_down"));
  EXPECT_TRUE(hasRule(r, "vertical_dissonance"));
}

// Declaring the upper voice as the suspended one describes no 2-3: the
// suspended voice of a 2-3 is the lower of the pair.
TEST(ValidatorTest, Sus2_3SuspendedUpperVoiceFailsInterval) {
  TwoThreeCase upper;
  upper.suspended_voice = 0;
  upper.upper_prep = 64;  // V0 holds E4 from beat 4 into the downbeat ...
  upper.upper_res = 62;   // ... and steps down to D4 over C3.
  upper.bass_prep = 48;   // C3 under the preparation, held.
  upper.bass_res = 48;
  const ValidationReport r = validateTwoThree(upper);
  EXPECT_TRUE(hasRule(r, "suspension_interval"));
}

// A minor ninth or a simple second qualifies as the dissonance; a third of
// descent is a leap, not a resolution, and the result must be a third.
TEST(ValidatorTest, Sus2_3BoundaryIntervalsAndStepSize) {
  TwoThreeCase minor_ninth;
  minor_ninth.upper_prep = 65;  // F4 over D3
  minor_ninth.upper_sus = 63;   // Eb4 over D3: a minor ninth.
  minor_ninth.upper_res = 63;   // Eb4 over C3: a minor tenth.
  EXPECT_FALSE(hasAnySuspensionFailure(validateTwoThree(minor_ninth)));

  TwoThreeCase simple_second;
  simple_second.upper_prep = 53;  // F3 over D3
  simple_second.upper_sus = 52;   // E3 over D3: a major second.
  simple_second.upper_res = 52;   // E3 over C3: a major third.
  EXPECT_FALSE(hasAnySuspensionFailure(validateTwoThree(simple_second)));

  TwoThreeCase leap;
  leap.bass_res = 47;   // D3 -> B2: three semitones.
  leap.upper_res = 62;  // D4 over B2: a tenth.
  EXPECT_TRUE(hasRule(validateTwoThree(leap), "suspension_resolution_step_down"));

  TwoThreeCase into_fourth;
  into_fourth.upper_res = 65;  // F4 over C3: an eleventh.
  EXPECT_TRUE(hasRule(validateTwoThree(into_fourth), "suspension_interval"));
}

// Episode motif-derivation tests. Subject = C4-D4-E4-F4 in voice 0;
// Episode span in voice 1 is expected to replay one of the five
// transforms. Validator must accept byte-matching notes and fail on
// any mismatch.

namespace {

MaterialNote subjectNote(Tick start, Tick dur, std::uint8_t pitch) {
  MaterialNote n;
  n.start_tick = start;
  n.duration = dur;
  n.pitch = pitch;
  return n;
}

Material subjectAscendingScale() {
  Material m;
  m.subject = {subjectNote(0, kTicksPerBeat, 60), subjectNote(kTicksPerBeat, kTicksPerBeat, 62),
               subjectNote(2 * kTicksPerBeat, kTicksPerBeat, 64),
               subjectNote(3 * kTicksPerBeat, kTicksPerBeat, 65)};
  return m;
}

EpisodeFragment makeEpisode(motif_ops::EpisodeMotifTransform transform, VoiceId voice,
                            Tick target_start) {
  EpisodeFragment f;
  f.transform = static_cast<std::uint8_t>(transform);
  f.source_start_index = 0;
  f.source_count = 0;
  f.voice = voice;
  f.target_start_tick = target_start;
  f.invert_pivot = 60;
  f.augment_factor = 2;
  f.diminish_factor = 2;
  return f;
}

}  // namespace

TEST(ValidatorTest, EpisodeMotifDerivedOriginalPasses) {
  Material material = subjectAscendingScale();
  material.episodes.push_back(
      makeEpisode(motif_ops::EpisodeMotifTransform::Original, 1, 4 * kTicksPerBeat));

  std::vector<NoteEvent> notes;
  // Subject in V0 bars 0-3.
  for (const auto& m : material.subject)
    notes.push_back(makeNote(m.start_tick, m.duration, m.pitch, 0));
  // Episode in V1 bars 4-7 = subject pitches at offset 4q (Original).
  for (std::size_t i = 0; i < material.subject.size(); ++i) {
    const auto& m = material.subject[i];
    notes.push_back(makeNote(m.start_tick + 4 * kTicksPerBeat, m.duration, m.pitch, 1));
  }
  std::vector<NoteProvenance> prov(notes.size(), makeProv(0, NoteSource::Material));

  ValidationReport r = Validator{}.validate(notes, prov, cMajorWhole(), material);
  EXPECT_FALSE(hasRule(r, "episode_motif_derived"));
}

namespace {

// The subject in V0 and an inverted episode in V1 four beats later, the
// episode sounding `inverted`, validated against `plan`.
ValidationReport validateInvertedEpisode(const Material& subject_material,
                                         const std::vector<std::uint8_t>& inverted,
                                         const HarmonicPlan& plan) {
  Material material = subject_material;
  material.episodes.push_back(
      makeEpisode(motif_ops::EpisodeMotifTransform::Invert, 1, 4 * kTicksPerBeat));
  std::vector<NoteEvent> notes;
  for (const auto& m : material.subject)
    notes.push_back(makeNote(m.start_tick, m.duration, m.pitch, 0));
  for (std::size_t i = 0; i < inverted.size(); ++i)
    notes.push_back(makeNote((i + 4) * kTicksPerBeat, kTicksPerBeat, inverted[i], 1));
  std::vector<NoteProvenance> prov(notes.size(), makeProv(0, NoteSource::Material));
  return Validator{}.validate(notes, prov, plan, material);
}

HarmonicPlan cMinorWhole() {
  HarmonicPlan plan = cMajorWhole();
  plan.is_minor = true;
  plan.chords.front().quality = ChordQuality::Minor;
  return plan;
}

}  // namespace

// Inversion mirrors scale degrees about the pivot, so C D E F about C in C
// major is C B A G -- the line stays in the key.
TEST(ValidatorTest, EpisodeMotifDerivedInvertPasses) {
  EXPECT_FALSE(
      hasRule(validateInvertedEpisode(subjectAscendingScale(), {60, 59, 57, 55}, cMajorWhole()),
              "episode_motif_derived"));
}

// The semitone mirror (C Bb Ab G) leaves C major and is not the declared
// inversion; neither is a mirror one degree off.
TEST(ValidatorTest, EpisodeMotifDerivedInvertRejectsChromaticAndWrongDegreeMirrors) {
  EXPECT_TRUE(
      hasRule(validateInvertedEpisode(subjectAscendingScale(), {60, 58, 56, 55}, cMajorWhole()),
              "episode_motif_derived"));
  EXPECT_TRUE(
      hasRule(validateInvertedEpisode(subjectAscendingScale(), {60, 59, 57, 53}, cMajorWhole()),
              "episode_motif_derived"));
}

// In C minor, C D Eb F about C mirrors to C Bb Ab G in the minor collection;
// the semitone mirror would land on A natural.
TEST(ValidatorTest, EpisodeMotifDerivedInvertUsesTheMinorCollection) {
  Material minor_subject;
  minor_subject.subject = {subjectNote(0, kTicksPerBeat, 60),
                           subjectNote(kTicksPerBeat, kTicksPerBeat, 62),
                           subjectNote(2 * kTicksPerBeat, kTicksPerBeat, 63),
                           subjectNote(3 * kTicksPerBeat, kTicksPerBeat, 65)};
  EXPECT_FALSE(hasRule(validateInvertedEpisode(minor_subject, {60, 58, 56, 55}, cMinorWhole()),
                       "episode_motif_derived"));
  EXPECT_TRUE(hasRule(validateInvertedEpisode(minor_subject, {60, 58, 57, 55}, cMinorWhole()),
                      "episode_motif_derived"));
}

TEST(ValidatorTest, EpisodeMotifDerivedRetrogradePasses) {
  Material material = subjectAscendingScale();
  material.episodes.push_back(
      makeEpisode(motif_ops::EpisodeMotifTransform::Retrograde, 1, 4 * kTicksPerBeat));

  std::vector<NoteEvent> notes;
  for (const auto& m : material.subject)
    notes.push_back(makeNote(m.start_tick, m.duration, m.pitch, 0));
  // Retrograde: 65, 64, 62, 60 starting at bar 4.
  std::vector<std::uint8_t> retro = {65, 64, 62, 60};
  for (std::size_t i = 0; i < retro.size(); ++i) {
    notes.push_back(makeNote((i + 4) * kTicksPerBeat, kTicksPerBeat, retro[i], 1));
  }
  std::vector<NoteProvenance> prov(notes.size(), makeProv(0, NoteSource::Material));

  ValidationReport r = Validator{}.validate(notes, prov, cMajorWhole(), material);
  EXPECT_FALSE(hasRule(r, "episode_motif_derived"));
}

TEST(ValidatorTest, EpisodeMotifDerivedAugmentPasses) {
  Material material = subjectAscendingScale();
  material.episodes.push_back(
      makeEpisode(motif_ops::EpisodeMotifTransform::Augment, 1, 8 * kTicksPerBeat));

  std::vector<NoteEvent> notes;
  for (const auto& m : material.subject)
    notes.push_back(makeNote(m.start_tick, m.duration, m.pitch, 0));
  // Augment by 2: pitches 60, 62, 64, 65; duration 2q each; start at bar 8.
  std::vector<std::uint8_t> aug_pitch = {60, 62, 64, 65};
  for (std::size_t i = 0; i < aug_pitch.size(); ++i) {
    notes.push_back(
        makeNote(8 * kTicksPerBeat + 2 * i * kTicksPerBeat, 2 * kTicksPerBeat, aug_pitch[i], 1));
  }
  std::vector<NoteProvenance> prov(notes.size(), makeProv(0, NoteSource::Material));

  ValidationReport r = Validator{}.validate(notes, prov, cMajorWhole(), material);
  EXPECT_FALSE(hasRule(r, "episode_motif_derived"));
}

TEST(ValidatorTest, EpisodeMotifDerivedDiminishPasses) {
  Material material;
  // Source with 2q durations so diminish-by-2 yields 1q durations.
  material.subject = {subjectNote(0, 2 * kTicksPerBeat, 60),
                      subjectNote(2 * kTicksPerBeat, 2 * kTicksPerBeat, 62),
                      subjectNote(4 * kTicksPerBeat, 2 * kTicksPerBeat, 64)};
  material.episodes.push_back(
      makeEpisode(motif_ops::EpisodeMotifTransform::Diminish, 1, 8 * kTicksPerBeat));

  std::vector<NoteEvent> notes;
  for (const auto& m : material.subject)
    notes.push_back(makeNote(m.start_tick, m.duration, m.pitch, 0));
  // Diminish by 2: pitches 60, 62, 64; duration q each; start at bar 8.
  std::vector<std::uint8_t> dim_pitch = {60, 62, 64};
  for (std::size_t i = 0; i < dim_pitch.size(); ++i) {
    notes.push_back(
        makeNote(8 * kTicksPerBeat + i * kTicksPerBeat, kTicksPerBeat, dim_pitch[i], 1));
  }
  std::vector<NoteProvenance> prov(notes.size(), makeProv(0, NoteSource::Material));

  ValidationReport r = Validator{}.validate(notes, prov, cMajorWhole(), material);
  EXPECT_FALSE(hasRule(r, "episode_motif_derived"));
}

TEST(ValidatorTest, EpisodeMotifDerivedPitchMismatchFails) {
  Material material = subjectAscendingScale();
  material.episodes.push_back(
      makeEpisode(motif_ops::EpisodeMotifTransform::Original, 1, 4 * kTicksPerBeat));

  std::vector<NoteEvent> notes;
  for (const auto& m : material.subject)
    notes.push_back(makeNote(m.start_tick, m.duration, m.pitch, 0));
  // Inject wrong second pitch (62 → 67).
  notes.push_back(makeNote(4 * kTicksPerBeat, kTicksPerBeat, 60, 1));
  notes.push_back(makeNote(5 * kTicksPerBeat, kTicksPerBeat, 67, 1));  // mismatch
  notes.push_back(makeNote(6 * kTicksPerBeat, kTicksPerBeat, 64, 1));
  notes.push_back(makeNote(7 * kTicksPerBeat, kTicksPerBeat, 65, 1));
  std::vector<NoteProvenance> prov(notes.size(), makeProv(0, NoteSource::Material));

  ValidationReport r = Validator{}.validate(notes, prov, cMajorWhole(), material);
  EXPECT_EQ(r.status, ValidationStatus::FailedSpan);
  EXPECT_TRUE(hasRule(r, "episode_motif_derived"));
  // Mismatch failure must carry MusicalFail FailKind.
  bool kind_ok = false;
  for (const auto& f : r.failures) {
    if (f.rule_id == "episode_motif_derived") {
      kind_ok = f.kind == FailKind::MusicalFail;
      break;
    }
  }
  EXPECT_TRUE(kind_ok);
}

TEST(ValidatorTest, EpisodeMotifDerivedMissingNoteFails) {
  Material material = subjectAscendingScale();
  material.episodes.push_back(
      makeEpisode(motif_ops::EpisodeMotifTransform::Original, 1, 4 * kTicksPerBeat));

  std::vector<NoteEvent> notes;
  for (const auto& m : material.subject)
    notes.push_back(makeNote(m.start_tick, m.duration, m.pitch, 0));
  // Episode emits only the first two notes (rest absent → mismatch on
  // tick 6q / 7q because no note matches the expected start_tick).
  notes.push_back(makeNote(4 * kTicksPerBeat, kTicksPerBeat, 60, 1));
  notes.push_back(makeNote(5 * kTicksPerBeat, kTicksPerBeat, 62, 1));
  std::vector<NoteProvenance> prov(notes.size(), makeProv(0, NoteSource::Material));

  ValidationReport r = Validator{}.validate(notes, prov, cMajorWhole(), material);
  EXPECT_EQ(r.status, ValidationStatus::FailedSpan);
  EXPECT_TRUE(hasRule(r, "episode_motif_derived"));
}

// Tonal answer + Countersubject Validator tests.

TEST(ValidatorTest, TonalAnswerDominantMappingPassesForTonicHeadToDominantPc) {
  Material material;
  // Subject opens on C4 (60, tonic). Tonal answer head G4 (67, dominant pc).
  material.subject = {subjectNote(0, kTicksPerBeat, 60)};
  material.tonal_answer = {subjectNote(0, kTicksPerBeat, 67)};
  material.use_tonal_answer = true;
  std::vector<NoteEvent> notes = {makeNote(0, kTicksPerBeat, 60, 0)};
  std::vector<NoteProvenance> prov(notes.size(), makeProv(0, NoteSource::Material));
  ValidationReport r = Validator{}.validate(notes, prov, cMajorWhole(), material);
  EXPECT_FALSE(hasRule(r, "tonal_answer_dominant_mapping"));
}

TEST(ValidatorTest, TonalAnswerDominantMappingFailsForTonicHeadToWrongPc) {
  Material material;
  // Tonal answer head is D4 (62, supertonic) — wrong. Expected dominant pc.
  material.subject = {subjectNote(0, kTicksPerBeat, 60)};
  material.tonal_answer = {subjectNote(0, kTicksPerBeat, 62)};
  material.use_tonal_answer = true;
  std::vector<NoteEvent> notes = {makeNote(0, kTicksPerBeat, 60, 0)};
  std::vector<NoteProvenance> prov(notes.size(), makeProv(0, NoteSource::Material));
  ValidationReport r = Validator{}.validate(notes, prov, cMajorWhole(), material);
  EXPECT_EQ(r.status, ValidationStatus::FailedSpan);
  EXPECT_TRUE(hasRule(r, "tonal_answer_dominant_mapping"));
}

TEST(ValidatorTest, TonalAnswerDominantMappingPassesForDominantHeadToTonicPc) {
  Material material;
  // Subject opens on G4 (67, dominant). Tonal answer head C5 (72, tonic pc).
  material.subject = {subjectNote(0, kTicksPerBeat, 67)};
  material.tonal_answer = {subjectNote(0, kTicksPerBeat, 72)};
  material.use_tonal_answer = true;
  std::vector<NoteEvent> notes = {makeNote(0, kTicksPerBeat, 67, 0)};
  std::vector<NoteProvenance> prov(notes.size(), makeProv(0, NoteSource::Material));
  ValidationReport r = Validator{}.validate(notes, prov, cMajorWhole(), material);
  EXPECT_FALSE(hasRule(r, "tonal_answer_dominant_mapping"));
}

TEST(ValidatorTest, TonalAnswerDominantMappingVacuousForMiddleDegreeHead) {
  Material material;
  // Subject opens on E4 (64, mediant). Mapping rule is silent.
  material.subject = {subjectNote(0, kTicksPerBeat, 64)};
  material.tonal_answer = {subjectNote(0, kTicksPerBeat, 59)};  // real answer base
  material.use_tonal_answer = true;
  std::vector<NoteEvent> notes = {makeNote(0, kTicksPerBeat, 64, 0)};
  std::vector<NoteProvenance> prov(notes.size(), makeProv(0, NoteSource::Material));
  ValidationReport r = Validator{}.validate(notes, prov, cMajorWhole(), material);
  EXPECT_FALSE(hasRule(r, "tonal_answer_dominant_mapping"));
}

TEST(ValidatorTest, TonalAnswerDominantMappingSkippedWhenFlagUnset) {
  Material material;
  material.subject = {subjectNote(0, kTicksPerBeat, 60)};
  // Wrong mapping but flag is off → rule must NOT fire.
  material.tonal_answer = {subjectNote(0, kTicksPerBeat, 62)};
  material.use_tonal_answer = false;
  std::vector<NoteEvent> notes = {makeNote(0, kTicksPerBeat, 60, 0)};
  std::vector<NoteProvenance> prov(notes.size(), makeProv(0, NoteSource::Material));
  ValidationReport r = Validator{}.validate(notes, prov, cMajorWhole(), material);
  EXPECT_FALSE(hasRule(r, "tonal_answer_dominant_mapping"));
}

TEST(ValidatorTest, CountersubjectContinuousPassesForFullCoverage) {
  Material material;
  material.subject = {subjectNote(0, kTicksPerBeat, 60)};
  // Answer occupies bars 4-7 (16 beats). CS in V0 covers all 16 beats.
  for (int i = 0; i < 16; ++i) {
    material.answer.push_back(subjectNote(4 * kTicksPerBar + i * kTicksPerBeat, kTicksPerBeat, 60));
  }
  for (int i = 0; i < 16; ++i) {
    material.countersubject.push_back(
        subjectNote(4 * kTicksPerBar + i * kTicksPerBeat, kTicksPerBeat, 71));
  }
  std::vector<NoteEvent> notes;
  std::vector<NoteProvenance> prov;
  for (const auto& m : material.countersubject) {
    notes.push_back(makeNote(m.start_tick, m.duration, m.pitch, 0));
    prov.push_back(makeProvIntent(0, NoteSource::Material, VoiceIntent::CountersubjectCarrier));
  }
  for (const auto& m : material.answer) {
    notes.push_back(makeNote(m.start_tick, m.duration, m.pitch, 1));
    prov.push_back(makeProvIntent(0, NoteSource::Material, VoiceIntent::AnswerCarrier));
  }
  ValidationReport r = Validator{}.validate(notes, prov, cMajorWhole(), material);
  EXPECT_FALSE(hasRule(r, "countersubject_continuous"));
}

TEST(ValidatorTest, CountersubjectContinuousFailsForGap) {
  Material material;
  material.subject = {subjectNote(0, kTicksPerBeat, 60)};
  for (int i = 0; i < 16; ++i) {
    material.answer.push_back(subjectNote(4 * kTicksPerBar + i * kTicksPerBeat, kTicksPerBeat, 60));
  }
  // CS declared as 16 beats but emit only 8 → gap at beat 9..
  for (int i = 0; i < 16; ++i) {
    material.countersubject.push_back(
        subjectNote(4 * kTicksPerBar + i * kTicksPerBeat, kTicksPerBeat, 71));
  }
  std::vector<NoteEvent> notes;
  std::vector<NoteProvenance> prov;
  for (int i = 0; i < 8; ++i) {
    notes.push_back(makeNote(4 * kTicksPerBar + i * kTicksPerBeat, kTicksPerBeat, 71, 0));
    prov.push_back(makeProvIntent(0, NoteSource::Material, VoiceIntent::CountersubjectCarrier));
  }
  for (const auto& m : material.answer) {
    notes.push_back(makeNote(m.start_tick, m.duration, m.pitch, 1));
    prov.push_back(makeProvIntent(0, NoteSource::Material, VoiceIntent::AnswerCarrier));
  }
  ValidationReport r = Validator{}.validate(notes, prov, cMajorWhole(), material);
  EXPECT_EQ(r.status, ValidationStatus::FailedSpan);
  EXPECT_TRUE(hasRule(r, "countersubject_continuous"));
}

TEST(ValidatorTest, CountersubjectContinuousSkippedWhenCSEmpty) {
  Material material;
  for (int i = 0; i < 4; ++i) {
    material.answer.push_back(subjectNote(i * kTicksPerBeat, kTicksPerBeat, 60));
  }
  // No CS declared → rule is vacuously satisfied.
  std::vector<NoteEvent> notes;
  std::vector<NoteProvenance> prov;
  ValidationReport r = Validator{}.validate(notes, prov, cMajorWhole(), material);
  EXPECT_FALSE(hasRule(r, "countersubject_continuous"));
}

TEST(ValidatorTest, CountersubjectContinuousChecksDeclaredVoiceNotAlwaysV0) {
  // The CS is carried in V1 (NOT V0). V1 has a continuity gap (only the
  // first half of the answer window is covered), while V0 sounds across
  // the whole window. The rule must sample V1 (derived from the
  // CountersubjectCarrier provenance), so it FIRES on the V1 gap rather
  // than passing on V0's full coverage.
  Material material;
  material.subject = {subjectNote(0, kTicksPerBeat, 60)};
  for (int idx = 0; idx < 16; ++idx) {
    material.answer.push_back(
        subjectNote(4 * kTicksPerBar + idx * kTicksPerBeat, kTicksPerBeat, 60));
  }
  for (int idx = 0; idx < 16; ++idx) {
    material.countersubject.push_back(
        subjectNote(4 * kTicksPerBar + idx * kTicksPerBeat, kTicksPerBeat, 71));
  }
  std::vector<NoteEvent> notes;
  std::vector<NoteProvenance> prov;
  // V0 fully covers the window but is NOT the countersubject voice.
  for (int idx = 0; idx < 16; ++idx) {
    notes.push_back(makeNote(4 * kTicksPerBar + idx * kTicksPerBeat, kTicksPerBeat, 60, 0));
    prov.push_back(makeProvIntent(0, NoteSource::Material, VoiceIntent::AnswerCarrier));
  }
  // V1 carries the CS but only covers the first 8 beats → gap from beat 9.
  for (int idx = 0; idx < 8; ++idx) {
    notes.push_back(makeNote(4 * kTicksPerBar + idx * kTicksPerBeat, kTicksPerBeat, 71, 1));
    prov.push_back(makeProvIntent(0, NoteSource::Material, VoiceIntent::CountersubjectCarrier));
  }
  ValidationReport r = Validator{}.validate(notes, prov, cMajorWhole(), material);
  EXPECT_TRUE(hasRule(r, "countersubject_continuous"));
}

TEST(ValidatorTest, CountersubjectContinuousPassesForContinuousNonZeroVoice) {
  // Converse of the above: the CS in V1 fully covers the window, while V0
  // has a gap. The rule samples V1 (the declared CS voice), so V0's gap is
  // irrelevant and the rule does NOT fire.
  Material material;
  material.subject = {subjectNote(0, kTicksPerBeat, 60)};
  for (int idx = 0; idx < 16; ++idx) {
    material.answer.push_back(
        subjectNote(4 * kTicksPerBar + idx * kTicksPerBeat, kTicksPerBeat, 60));
  }
  for (int idx = 0; idx < 16; ++idx) {
    material.countersubject.push_back(
        subjectNote(4 * kTicksPerBar + idx * kTicksPerBeat, kTicksPerBeat, 71));
  }
  std::vector<NoteEvent> notes;
  std::vector<NoteProvenance> prov;
  // V0 has a gap (only 8 beats) but is NOT the countersubject voice.
  for (int idx = 0; idx < 8; ++idx) {
    notes.push_back(makeNote(4 * kTicksPerBar + idx * kTicksPerBeat, kTicksPerBeat, 60, 0));
    prov.push_back(makeProvIntent(0, NoteSource::Material, VoiceIntent::AnswerCarrier));
  }
  // V1 carries the CS and fully covers the window.
  for (int idx = 0; idx < 16; ++idx) {
    notes.push_back(makeNote(4 * kTicksPerBar + idx * kTicksPerBeat, kTicksPerBeat, 71, 1));
    prov.push_back(makeProvIntent(0, NoteSource::Material, VoiceIntent::CountersubjectCarrier));
  }
  ValidationReport r = Validator{}.validate(notes, prov, cMajorWhole(), material);
  EXPECT_FALSE(hasRule(r, "countersubject_continuous"));
}

TEST(ValidatorTest, CrossRelationSkippedWhenBothMaterial) {
  // F natural (pc 5) vs F# (pc 6) in different voices form a cross
  // relation, but both notes are NoteSource::Material — the composer
  // cannot edit fixed inputs, so the rule must NOT fire.
  std::vector<NoteEvent> notes = {
      makeNote(0, kTicksPerBeat, 65, 0),  // F
      makeNote(0, kTicksPerBeat, 66, 1),  // F#
  };
  std::vector<NoteProvenance> prov(notes.size(), makeProv(0, NoteSource::Material));
  ValidationReport r = Validator{}.validate(notes, prov, cMajorWhole());
  EXPECT_FALSE(hasRule(r, "cross_relation"));
}

TEST(ValidatorTest, CrossRelationFiresWhenOneIsCompose) {
  // Same cross-relation pair, but one note is Compose-sourced. The
  // composer can fix it, so the rule FIRES.
  std::vector<NoteEvent> notes = {
      makeNote(0, kTicksPerBeat, 65, 0),  // F (Material)
      makeNote(0, kTicksPerBeat, 66, 1),  // F# (Compose)
  };
  std::vector<NoteProvenance> prov = {
      makeProv(0, NoteSource::Material),
      makeProv(0, NoteSource::Compose),
  };
  ValidationReport r = Validator{}.validate(notes, prov, cMajorWhole());
  EXPECT_TRUE(hasRule(r, "cross_relation"));
}

TEST(ValidatorTest, EpisodeMotifDerivedEmptyEpisodesListsPasses) {
  // No EpisodeFragments => rule is vacuously satisfied even with
  // arbitrary content.
  Material material;
  material.subject = subjectAscendingScale().subject;
  std::vector<NoteEvent> notes;
  for (const auto& m : material.subject)
    notes.push_back(makeNote(m.start_tick, m.duration, m.pitch, 0));
  std::vector<NoteProvenance> prov(notes.size(), makeProv(0, NoteSource::Material));

  ValidationReport r = Validator{}.validate(notes, prov, cMajorWhole(), material);
  EXPECT_FALSE(hasRule(r, "episode_motif_derived"));
}

// ===================================================================
// Functional-harmony doubling / spacing rules.
// ===================================================================
//
// The three rules (doubling_no_leading_tone, doubling_no_seventh,
// spacing_adjacent_voices_within_octave) only fire when the active
// ChordEvent declares `has_degree = true` — that's how callers opt
// into the stricter regime. Tests below build small HarmonicPlans
// inline so each scenario is self-contained.

namespace {

HarmonicPlan gMajorDominantWithDegree() {
  // Single G-major (dominant of C) chord region with has_degree set.
  HarmonicPlan plan;
  plan.tonic_pc = 0;
  plan.is_minor = false;
  plan.enforce_chordal_upper_voice_spacing = true;
  ChordEvent v;
  v.start_tick = 0;
  v.root_pc = 7;
  v.quality = ChordQuality::Major;
  v.degree = RomanNumeral::V;
  v.inversion = ChordInversion::Root;
  v.function = HarmonicFunction::D;
  v.has_degree = true;
  plan.chords.push_back(v);
  return plan;
}

HarmonicPlan gDominant7WithDegree() {
  HarmonicPlan plan;
  plan.tonic_pc = 0;
  plan.is_minor = false;
  ChordEvent v7;
  v7.start_tick = 0;
  v7.root_pc = 7;
  v7.quality = ChordQuality::Dominant7;
  v7.degree = RomanNumeral::V;
  v7.inversion = ChordInversion::Root;
  v7.function = HarmonicFunction::D;
  v7.has_degree = true;
  plan.chords.push_back(v7);
  return plan;
}

HarmonicPlan cTonicWithDegree() {
  HarmonicPlan plan;
  plan.tonic_pc = 0;
  plan.is_minor = false;
  ChordEvent i;
  i.start_tick = 0;
  i.root_pc = 0;
  i.quality = ChordQuality::Major;
  i.degree = RomanNumeral::I;
  i.inversion = ChordInversion::Root;
  i.function = HarmonicFunction::T;
  i.has_degree = true;
  plan.chords.push_back(i);
  return plan;
}

}  // namespace

TEST(ValidatorTest, DoublingNoLeadingToneFailsForDoubledB) {
  // V chord in C major: G/B/D. Doubling B (the leading tone) across
  // two voices fires the rule.
  const Tick beat = kTicksPerBeat;
  std::vector<NoteEvent> notes = {
      makeNote(0, beat, 83, 0),  // B5
      makeNote(0, beat, 71, 1),  // B4 — duplicate leading tone
      makeNote(0, beat, 67, 2),  // G4
  };
  std::vector<NoteProvenance> prov(notes.size(), makeProv(0, NoteSource::Compose));
  ValidationReport r = Validator{}.validate(notes, prov, gMajorDominantWithDegree(), Material{});
  EXPECT_TRUE(hasRule(r, "doubling_no_leading_tone"));
}

TEST(ValidatorTest, DoublingNoLeadingTonePassesForSingleLeadingTone) {
  const Tick beat = kTicksPerBeat;
  std::vector<NoteEvent> notes = {
      makeNote(0, beat, 79, 0),  // G5
      makeNote(0, beat, 71, 1),  // B4 — only leading tone
      makeNote(0, beat, 67, 2),  // G4 (root double, allowed)
  };
  std::vector<NoteProvenance> prov(notes.size(), makeProv(0, NoteSource::Compose));
  ValidationReport r = Validator{}.validate(notes, prov, gMajorDominantWithDegree(), Material{});
  EXPECT_FALSE(hasRule(r, "doubling_no_leading_tone"));
}

TEST(ValidatorTest, DoublingNoLeadingToneSkippedOnChordsWithoutDegree) {
  // Same doubled B, but plan defaults (has_degree=false) — rule must
  // skip silently for fixtures without functional-harmony data.
  HarmonicPlan plan = gMajorDominantWithDegree();
  plan.chords.front().has_degree = false;
  const Tick beat = kTicksPerBeat;
  std::vector<NoteEvent> notes = {
      makeNote(0, beat, 83, 0),
      makeNote(0, beat, 71, 1),
      makeNote(0, beat, 67, 2),
  };
  std::vector<NoteProvenance> prov(notes.size(), makeProv(0, NoteSource::Compose));
  ValidationReport r = Validator{}.validate(notes, prov, plan, Material{});
  EXPECT_FALSE(hasRule(r, "doubling_no_leading_tone"));
}

TEST(ValidatorTest, DoublingNoLeadingToneIgnoredOnTonicChord) {
  // I chord does not contain the leading tone, so doubling B has no
  // bearing on this rule.
  const Tick beat = kTicksPerBeat;
  std::vector<NoteEvent> notes = {
      makeNote(0, beat, 72, 0),  // C5
      makeNote(0, beat, 71, 1),  // B4 — non-chord-tone, but rule is silent
      makeNote(0, beat, 67, 2),  // G4
  };
  std::vector<NoteProvenance> prov(notes.size(), makeProv(0, NoteSource::Compose));
  ValidationReport r = Validator{}.validate(notes, prov, cTonicWithDegree(), Material{});
  EXPECT_FALSE(hasRule(r, "doubling_no_leading_tone"));
}

TEST(ValidatorTest, DoublingNoSeventhFailsForDoubledF) {
  // V7 in C: G B D F. Doubling F (the chord seventh) fires the rule.
  const Tick beat = kTicksPerBeat;
  std::vector<NoteEvent> notes = {
      makeNote(0, beat, 77, 0),  // F5 — seventh
      makeNote(0, beat, 71, 1),  // B4
      makeNote(0, beat, 65, 2),  // F4 — duplicate seventh
      makeNote(0, beat, 55, 3),  // G3
  };
  std::vector<NoteProvenance> prov(notes.size(), makeProv(0, NoteSource::Compose));
  ValidationReport r = Validator{}.validate(notes, prov, gDominant7WithDegree(), Material{});
  EXPECT_TRUE(hasRule(r, "doubling_no_seventh"));
}

TEST(ValidatorTest, DoublingNoSeventhPassesForSingleSeventh) {
  const Tick beat = kTicksPerBeat;
  std::vector<NoteEvent> notes = {
      makeNote(0, beat, 77, 0),  // F5 — seventh
      makeNote(0, beat, 74, 1),  // D5
      makeNote(0, beat, 71, 2),  // B4
      makeNote(0, beat, 55, 3),  // G3
  };
  std::vector<NoteProvenance> prov(notes.size(), makeProv(0, NoteSource::Compose));
  ValidationReport r = Validator{}.validate(notes, prov, gDominant7WithDegree(), Material{});
  EXPECT_FALSE(hasRule(r, "doubling_no_seventh"));
}

TEST(ValidatorTest, DoublingNoSeventhSkippedOnPlainTriad) {
  // V triad: no 7th, rule must not fire even if F sounds (which would
  // be a NCT for the triad).
  const Tick beat = kTicksPerBeat;
  std::vector<NoteEvent> notes = {
      makeNote(0, beat, 77, 0),
      makeNote(0, beat, 65, 1),
      makeNote(0, beat, 55, 2),
  };
  std::vector<NoteProvenance> prov(notes.size(), makeProv(0, NoteSource::Compose));
  ValidationReport r = Validator{}.validate(notes, prov, gMajorDominantWithDegree(), Material{});
  EXPECT_FALSE(hasRule(r, "doubling_no_seventh"));
}

TEST(ValidatorTest, SpacingFailsForUpperVoicesOverOctave) {
  // V chord; V0=G5 (79), V1=B3 (59), V2=G3 (55). V0-V1 gap = 20
  // semitones > 12 → spacing rule fires.
  const Tick beat = kTicksPerBeat;
  std::vector<NoteEvent> notes = {
      makeNote(0, beat, 79, 0),
      makeNote(0, beat, 59, 1),
      makeNote(0, beat, 55, 2),
  };
  std::vector<NoteProvenance> prov(notes.size(), makeProv(0, NoteSource::Compose));
  ValidationReport r = Validator{}.validate(notes, prov, gMajorDominantWithDegree(), Material{});
  EXPECT_TRUE(hasRule(r, "spacing_adjacent_voices_within_octave"));
}

TEST(ValidatorTest, SpacingPassesForTightUpperVoices) {
  // V0=G5 (79), V1=D5 (74), V2=B4 (71). All adjacent gaps within an
  // octave.
  const Tick beat = kTicksPerBeat;
  std::vector<NoteEvent> notes = {
      makeNote(0, beat, 79, 0),
      makeNote(0, beat, 74, 1),
      makeNote(0, beat, 71, 2),
  };
  std::vector<NoteProvenance> prov(notes.size(), makeProv(0, NoteSource::Compose));
  ValidationReport r = Validator{}.validate(notes, prov, gMajorDominantWithDegree(), Material{});
  EXPECT_FALSE(hasRule(r, "spacing_adjacent_voices_within_octave"));
}

TEST(ValidatorTest, SpacingAllowsLargeBassTenorGap) {
  // 4-voice spacing: upper pairs S/A and A/T must stay within an
  // octave; the T/B pair (V2-V3) may exceed. Here the bass sits very
  // low (G2 = 43) but A/T (V1-V2) stays compact.
  const Tick beat = kTicksPerBeat;
  std::vector<NoteEvent> notes = {
      makeNote(0, beat, 79, 0),  // G5
      makeNote(0, beat, 74, 1),  // D5
      makeNote(0, beat, 71, 2),  // B4
      makeNote(0, beat, 43, 3),  // G2 — wide gap below tenor
  };
  std::vector<NoteProvenance> prov(notes.size(), makeProv(0, NoteSource::Compose));
  ValidationReport r = Validator{}.validate(notes, prov, gMajorDominantWithDegree(), Material{});
  EXPECT_FALSE(hasRule(r, "spacing_adjacent_voices_within_octave"));
}

TEST(ValidatorTest, SpacingSkippedForTwoVoiceTexture) {
  // Only 2 voices → spacing rule has no upper-pair triple to check.
  const Tick beat = kTicksPerBeat;
  std::vector<NoteEvent> notes = {
      makeNote(0, beat, 79, 0),
      makeNote(0, beat, 43, 1),
  };
  std::vector<NoteProvenance> prov(notes.size(), makeProv(0, NoteSource::Compose));
  ValidationReport r = Validator{}.validate(notes, prov, gMajorDominantWithDegree(), Material{});
  EXPECT_FALSE(hasRule(r, "spacing_adjacent_voices_within_octave"));
}

// modulation_pivot_chord_required tests.
//
// Plan layout: a single-chord pivot at the modulation tick. A valid
// pivot must be diatonic in both keys. The unit tests below isolate
// the rule by giving the plan exactly one chord at the pivot tick;
// real fixtures interleave pre- and post-pivot chords.

namespace {

// Helper plan with a single ChordEvent at the pivot tick + a
// ModulationEvent declaring C major → G major Pivot at the same tick.
HarmonicPlan cToGPivotPlan(std::uint8_t pivot_root_pc, ChordQuality pivot_quality,
                           RomanNumeral pivot_degree) {
  HarmonicPlan plan;
  plan.tonic_pc = 0;
  plan.is_minor = false;
  ChordEvent pivot;
  pivot.start_tick = 0;
  pivot.root_pc = pivot_root_pc;
  pivot.quality = pivot_quality;
  pivot.degree = pivot_degree;
  pivot.function = HarmonicFunction::Pred;
  pivot.has_degree = true;
  plan.chords.push_back(pivot);
  ModulationEvent mod;
  mod.tick = 0;
  mod.from_tonic_pc = 0;
  mod.to_tonic_pc = 7;
  mod.type = ModulationType::Pivot;
  plan.modulations.push_back(mod);
  return plan;
}

}  // namespace

TEST(ValidatorTest, ModulationPivotChordRequiredPassesForValidPivot) {
  // ii in C (D-F-A minor) = vi in G (D-F#-A) — but wait, F natural is
  // diatonic in C but not in G. A truly shared chord is IV in C = bVII
  // in G or… use vi in C (A-C-E) = ii in G (A-C-E minor): all three
  // pcs A=9, C=0, E=4 belong to both C major {0,2,4,5,7,9,11} and G
  // major {7,9,11,0,2,4,6}? E=4 is in C but G major has 6 (F#), not 4
  // (E). Wait — E IS in G major scale (G A B C D E F#). Yes, 4 is in
  // G's scale. So A-C-E is diatonic in both. Use it as the pivot
  // (vi in C / ii in G).
  HarmonicPlan plan = cToGPivotPlan(9, ChordQuality::Minor, RomanNumeral::VI);
  ValidationReport r = Validator{}.validate({}, {}, plan, Material{});
  EXPECT_FALSE(hasRule(r, "modulation_pivot_chord_required"));
}

TEST(ValidatorTest, ModulationPivotChordRequiredFailsForNonPivot) {
  // V7 in C (G-B-D-F) — F is not in G major scale (G A B C D E F#),
  // so the chord is not diatonic in the target key. Rule fires.
  HarmonicPlan plan = cToGPivotPlan(7, ChordQuality::Dominant7, RomanNumeral::V);
  ValidationReport r = Validator{}.validate({}, {}, plan, Material{});
  EXPECT_TRUE(hasRule(r, "modulation_pivot_chord_required"));
}

TEST(ValidatorTest, ModulationPivotChordRequiredSkippedForPhraseType) {
  // Phrase modulation is exempt — no pivot needed.
  HarmonicPlan plan = cToGPivotPlan(7, ChordQuality::Dominant7, RomanNumeral::V);
  plan.modulations.front().type = ModulationType::Phrase;
  ValidationReport r = Validator{}.validate({}, {}, plan, Material{});
  EXPECT_FALSE(hasRule(r, "modulation_pivot_chord_required"));
}

TEST(ValidatorTest, ModulationPivotChordRequiredSkippedWhenNoModulations) {
  HarmonicPlan plan = cMajorWhole();
  ValidationReport r = Validator{}.validate({}, {}, plan, Material{});
  EXPECT_FALSE(hasRule(r, "modulation_pivot_chord_required"));
}

// secondary_dominant_resolution tests.
//
// Plan: chord 0 = V/V (D-major) with has_secondary_of=true and
// secondary_of=V. Chord 1 = V (G-major) at the next tick. The
// secondary leading tone is F# (pc=6), which should rise to G (pc=7)
// in some voice.

namespace {

HarmonicPlan secondaryDominantPlan(bool include_resolution, bool wrong_degree = false) {
  HarmonicPlan plan;
  plan.tonic_pc = 0;
  plan.is_minor = false;
  ChordEvent v_of_v;
  v_of_v.start_tick = 0;
  v_of_v.root_pc = 2;  // D
  v_of_v.quality = ChordQuality::Major;
  v_of_v.degree = RomanNumeral::V;
  v_of_v.function = HarmonicFunction::Pred;
  v_of_v.has_degree = true;
  v_of_v.has_secondary_of = true;
  v_of_v.secondary_of = RomanNumeral::V;
  plan.chords.push_back(v_of_v);
  if (include_resolution) {
    ChordEvent resolution;
    resolution.start_tick = kTicksPerBeat;
    resolution.root_pc = 7;
    resolution.quality = ChordQuality::Major;
    resolution.degree = wrong_degree ? RomanNumeral::I : RomanNumeral::V;
    resolution.function = HarmonicFunction::D;
    resolution.has_degree = true;
    plan.chords.push_back(resolution);
  }
  return plan;
}

}  // namespace

TEST(ValidatorTest, SecondaryDominantResolutionPassesForCleanResolution) {
  // V/V → V with F#5 (66) → G5 (67) in voice 0 (the LT resolves up).
  const Tick beat = kTicksPerBeat;
  std::vector<NoteEvent> notes = {
      makeNote(0, beat, 66, 0),     // F#5
      makeNote(0, beat, 62, 1),     // D5 (root)
      makeNote(beat, beat, 67, 0),  // G5
      makeNote(beat, beat, 59, 1),  // B4
  };
  std::vector<NoteProvenance> prov(notes.size(), makeProv(0, NoteSource::Compose));
  ValidationReport r = Validator{}.validate(
      notes, prov, secondaryDominantPlan(/*include_resolution=*/true), Material{});
  EXPECT_FALSE(hasRule(r, "secondary_dominant_resolution"));
}

TEST(ValidatorTest, SecondaryDominantResolutionAllowsFreeVoiceLeading) {
  // V/V → V where F# does not rise by step. The
  // rule is degree-pairing only; voice-leading details (LT rise) are
  // not part of the failure condition. The rule should pass when the
  // resolution chord is correctly identified as V.
  const Tick beat = kTicksPerBeat;
  std::vector<NoteEvent> notes = {
      makeNote(0, beat, 66, 0),
      makeNote(0, beat, 62, 1),
      makeNote(beat, beat, 62, 0),
      makeNote(beat, beat, 59, 1),
  };
  std::vector<NoteProvenance> prov(notes.size(), makeProv(0, NoteSource::Compose));
  ValidationReport r = Validator{}.validate(
      notes, prov, secondaryDominantPlan(/*include_resolution=*/true), Material{});
  EXPECT_FALSE(hasRule(r, "secondary_dominant_resolution"));
}

TEST(ValidatorTest, SecondaryDominantResolutionFailsForWrongDegreeNext) {
  // V/V followed by I (instead of V) — degree mismatch.
  const Tick beat = kTicksPerBeat;
  std::vector<NoteEvent> notes = {
      makeNote(0, beat, 66, 0),
      makeNote(0, beat, 62, 1),
      makeNote(beat, beat, 67, 0),
      makeNote(beat, beat, 60, 1),
  };
  std::vector<NoteProvenance> prov(notes.size(), makeProv(0, NoteSource::Compose));
  ValidationReport r = Validator{}.validate(
      notes, prov, secondaryDominantPlan(/*include_resolution=*/true, /*wrong_degree=*/true),
      Material{});
  EXPECT_TRUE(hasRule(r, "secondary_dominant_resolution"));
}

// sequence_pattern_consistency tests.
//
// Seed motif: 2 notes (C5=72, D5=74) each 1 quarter long. Each pattern moves
// the seed by scale degrees of the key, so in C major:
//   DescendingFifths: -4 degrees -> step0={72,74}, step1={65,67}, step2={59,60}
//   DescendingStep:   -1 degree  -> step0={72,74}, step1={71,72}, step2={69,71}
//   AscendingStep:    +1 degree  -> step0={72,74}, step1={74,76}, step2={76,77}

namespace {

SequenceTemplate makeSeqTemplate(SequencePattern pattern, std::uint8_t num_steps,
                                 std::vector<std::uint8_t> seed = {72, 74}) {
  SequenceTemplate t;
  t.pattern = pattern;
  t.target_start_tick = 0;
  t.step_length_ticks = 2 * kTicksPerBeat;
  t.num_steps = num_steps;
  t.voice = 0;
  t.seed_pitches = std::move(seed);
  t.seed_durations = {kTicksPerBeat, kTicksPerBeat};
  return t;
}

// Two quarter notes per step, one step every half note, in voice 0.
std::vector<NoteEvent> renderSteps(const std::vector<std::array<std::uint8_t, 2>>& steps) {
  std::vector<NoteEvent> notes;
  for (std::size_t k = 0; k < steps.size(); ++k) {
    const Tick base_tick = static_cast<Tick>(k) * 2 * kTicksPerBeat;
    notes.push_back(makeNote(base_tick, kTicksPerBeat, steps[k][0], 0));
    notes.push_back(makeNote(base_tick + kTicksPerBeat, kTicksPerBeat, steps[k][1], 0));
  }
  return notes;
}

std::vector<NoteEvent> renderSequence(SequencePattern pattern, std::uint8_t num_steps,
                                      bool tamper_step1_pitch = false) {
  std::vector<std::array<std::uint8_t, 2>> table;
  switch (pattern) {
    case SequencePattern::DescendingFifths:
      table = {{72, 74}, {65, 67}, {59, 60}};
      break;
    case SequencePattern::DescendingStep:
      table = {{72, 74}, {71, 72}, {69, 71}};
      break;
    case SequencePattern::AscendingStep:
      table = {{72, 74}, {74, 76}, {76, 77}};
      break;
  }
  table.resize(num_steps);
  if (tamper_step1_pitch && num_steps > 1)
    ++table[1][0];  // intentionally off-pattern
  return renderSteps(table);
}

ValidationReport validateSequence(const SequenceTemplate& tmpl, const std::vector<NoteEvent>& notes,
                                  const HarmonicPlan& plan) {
  Material m;
  m.sequence_templates.push_back(tmpl);
  std::vector<NoteProvenance> prov(notes.size(), makeProv(0, NoteSource::Material));
  return Validator{}.validate(notes, prov, plan, m);
}

// C minor with i from the downbeat and V from the second step on.
HarmonicPlan cMinorTonicThenDominant() {
  HarmonicPlan plan;
  plan.tonic_pc = 0;
  plan.is_minor = true;
  ChordEvent tonic;
  tonic.root_pc = 0;
  tonic.quality = ChordQuality::Minor;
  plan.chords.push_back(tonic);
  ChordEvent dominant;
  dominant.start_tick = 2 * kTicksPerBeat;
  dominant.root_pc = 7;
  dominant.quality = ChordQuality::Major;
  plan.chords.push_back(dominant);
  return plan;
}

HarmonicPlan cMinorTonicOnly() {
  HarmonicPlan plan = cMinorTonicThenDominant();
  plan.chords.pop_back();
  return plan;
}

}  // namespace

TEST(ValidatorTest, SequencePatternConsistencyPassesForDescendingFifths) {
  Material m;
  m.sequence_templates.push_back(makeSeqTemplate(SequencePattern::DescendingFifths, 3));
  auto notes = renderSequence(SequencePattern::DescendingFifths, 3);
  std::vector<NoteProvenance> prov(notes.size(), makeProv(0, NoteSource::Material));
  ValidationReport r = Validator{}.validate(notes, prov, cMajorWhole(), m);
  EXPECT_FALSE(hasRule(r, "sequence_pattern_consistency"));
}

TEST(ValidatorTest, SequencePatternConsistencyPassesForDescendingStep) {
  Material m;
  m.sequence_templates.push_back(makeSeqTemplate(SequencePattern::DescendingStep, 3));
  auto notes = renderSequence(SequencePattern::DescendingStep, 3);
  std::vector<NoteProvenance> prov(notes.size(), makeProv(0, NoteSource::Material));
  ValidationReport r = Validator{}.validate(notes, prov, cMajorWhole(), m);
  EXPECT_FALSE(hasRule(r, "sequence_pattern_consistency"));
}

TEST(ValidatorTest, SequencePatternConsistencyPassesForAscendingStep) {
  Material m;
  m.sequence_templates.push_back(makeSeqTemplate(SequencePattern::AscendingStep, 3));
  auto notes = renderSequence(SequencePattern::AscendingStep, 3);
  std::vector<NoteProvenance> prov(notes.size(), makeProv(0, NoteSource::Material));
  ValidationReport r = Validator{}.validate(notes, prov, cMajorWhole(), m);
  EXPECT_FALSE(hasRule(r, "sequence_pattern_consistency"));
}

TEST(ValidatorTest, SequencePatternConsistencyFailsForWrongPitch) {
  Material m;
  m.sequence_templates.push_back(makeSeqTemplate(SequencePattern::DescendingFifths, 3));
  auto notes = renderSequence(SequencePattern::DescendingFifths, 3, /*tamper_step1_pitch=*/true);
  std::vector<NoteProvenance> prov(notes.size(), makeProv(0, NoteSource::Material));
  ValidationReport r = Validator{}.validate(notes, prov, cMajorWhole(), m);
  EXPECT_TRUE(hasRule(r, "sequence_pattern_consistency"));
}

TEST(ValidatorTest, SequencePatternConsistencyFailsForMissingStep) {
  Material m;
  m.sequence_templates.push_back(makeSeqTemplate(SequencePattern::AscendingStep, 3));
  auto notes = renderSequence(SequencePattern::AscendingStep, 2);  // drop step 2
  std::vector<NoteProvenance> prov(notes.size(), makeProv(0, NoteSource::Material));
  ValidationReport r = Validator{}.validate(notes, prov, cMajorWhole(), m);
  EXPECT_TRUE(hasRule(r, "sequence_pattern_consistency"));
}

TEST(ValidatorTest, SequencePatternConsistencySkippedWhenNoTemplate) {
  Material m;
  auto notes = renderSequence(SequencePattern::DescendingFifths, 3);
  std::vector<NoteProvenance> prov(notes.size(), makeProv(0, NoteSource::Material));
  ValidationReport r = Validator{}.validate(notes, prov, cMajorWhole(), m);
  EXPECT_FALSE(hasRule(r, "sequence_pattern_consistency"));
}

// A step is a scale degree, not two semitones: the chromatic descent
// C D -> Bb C -> Ab Bb leaves C major and is not the declared sequence, and a
// step that moves two degrees is the wrong step.
TEST(ValidatorTest, SequencePatternConsistencyRejectsChromaticAndWrongDegreeSteps) {
  const SequenceTemplate tmpl = makeSeqTemplate(SequencePattern::DescendingStep, 3);
  EXPECT_TRUE(
      hasRule(validateSequence(tmpl, renderSteps({{72, 74}, {70, 72}, {68, 70}}), cMajorWhole()),
              "sequence_pattern_consistency"));
  EXPECT_TRUE(
      hasRule(validateSequence(tmpl, renderSteps({{72, 74}, {69, 71}, {69, 71}}), cMajorWhole()),
              "sequence_pattern_consistency"));
}

// In C minor the seed C Eb steps down through the minor collection: Bb D, then
// Ab C. The semitone transposition (Bb Db) is not in the key.
TEST(ValidatorTest, SequencePatternConsistencyStepsThroughTheMinorCollection) {
  const SequenceTemplate tmpl = makeSeqTemplate(SequencePattern::DescendingStep, 3, {72, 75});
  EXPECT_FALSE(hasRule(
      validateSequence(tmpl, renderSteps({{72, 75}, {70, 74}, {68, 72}}), cMinorTonicOnly()),
      "sequence_pattern_consistency"));
  EXPECT_TRUE(hasRule(
      validateSequence(tmpl, renderSteps({{72, 75}, {70, 73}, {68, 71}}), cMinorTonicOnly()),
      "sequence_pattern_consistency"));
}

// Over the minor dominant the step takes the raised seventh: G C stepping down
// under V is F B-natural, as the key is spelled under that harmony.
TEST(ValidatorTest, SequencePatternConsistencyRaisesTheMinorSeventhOverTheDominant) {
  const SequenceTemplate tmpl = makeSeqTemplate(SequencePattern::DescendingStep, 2, {67, 72});
  EXPECT_FALSE(
      hasRule(validateSequence(tmpl, renderSteps({{67, 72}, {65, 71}}), cMinorTonicThenDominant()),
              "sequence_pattern_consistency"));
  EXPECT_TRUE(
      hasRule(validateSequence(tmpl, renderSteps({{67, 72}, {65, 70}}), cMinorTonicThenDominant()),
              "sequence_pattern_consistency"));
}

// imitation_entry_match tests.
//
// Subject opens at tick 0 with C5 (72). Real answer enters 4 beats
// later at G4 (67), which equals 72 + (-5) semitones (real answer).

namespace {

Material makeImitationMaterial(Tick distance, int interval,
                               std::uint8_t follower_pitch_override = 0,
                               Tick follower_tick_override = static_cast<Tick>(-1)) {
  Material m;
  MaterialNote subj_head;
  subj_head.start_tick = 0;
  subj_head.duration = kTicksPerBeat;
  subj_head.pitch = 72;
  m.subject.push_back(subj_head);
  MaterialNote ans_head;
  ans_head.start_tick =
      (follower_tick_override == static_cast<Tick>(-1)) ? distance : follower_tick_override;
  ans_head.duration = kTicksPerBeat;
  ans_head.pitch = follower_pitch_override > 0
                       ? follower_pitch_override
                       : static_cast<std::uint8_t>(static_cast<int>(72) + interval);
  m.answer.push_back(ans_head);
  ImitationEntry entry;
  entry.leader_fragment = MaterialFragment::Subject;
  entry.follower_fragment = MaterialFragment::Answer;
  entry.distance_ticks = distance;
  entry.interval_semis = interval;
  m.imitation_entries.push_back(entry);
  return m;
}

}  // namespace

TEST(ValidatorTest, ImitationEntryMatchPassesForRealAnswerP5Down) {
  Material m = makeImitationMaterial(4 * kTicksPerBeat, -5);
  ValidationReport r = Validator{}.validate({}, {}, cMajorWhole(), m);
  EXPECT_FALSE(hasRule(r, "imitation_entry_match"));
}

TEST(ValidatorTest, ImitationEntryMatchPassesForP8Up) {
  Material m = makeImitationMaterial(4 * kTicksPerBeat, 12);
  ValidationReport r = Validator{}.validate({}, {}, cMajorWhole(), m);
  EXPECT_FALSE(hasRule(r, "imitation_entry_match"));
}

TEST(ValidatorTest, ImitationEntryMatchFailsForWrongInterval) {
  Material m = makeImitationMaterial(4 * kTicksPerBeat, -5, /*follower_pitch_override=*/65);
  ValidationReport r = Validator{}.validate({}, {}, cMajorWhole(), m);
  EXPECT_TRUE(hasRule(r, "imitation_entry_match"));
}

TEST(ValidatorTest, ImitationEntryMatchFailsForWrongDistance) {
  Material m = makeImitationMaterial(4 * kTicksPerBeat, -5, /*follower_pitch_override=*/0,
                                     /*follower_tick_override=*/3 * kTicksPerBeat);
  ValidationReport r = Validator{}.validate({}, {}, cMajorWhole(), m);
  EXPECT_TRUE(hasRule(r, "imitation_entry_match"));
}

TEST(ValidatorTest, ImitationEntryMatchChecksFullRealAnswerContourAndRhythm) {
  Material m = makeImitationMaterial(4 * kTicksPerBeat, -5);
  m.subject.push_back({kTicksPerBeat, kTicksPerBeat / 2, 74});
  m.answer.push_back({5 * kTicksPerBeat, kTicksPerBeat / 2, 70});  // expected 69
  m.imitation_entries.front().note_count = 2;
  ValidationReport r = Validator{}.validate({}, {}, cMajorWhole(), m);
  EXPECT_TRUE(hasRule(r, "imitation_entry_match"));
}

TEST(ValidatorTest, ImitationEntryMatchSkippedWhenNoEntry) {
  Material m;
  m.subject.push_back({0, kTicksPerBeat, 72});
  m.answer.push_back({4 * kTicksPerBeat, kTicksPerBeat, 67});
  ValidationReport r = Validator{}.validate({}, {}, cMajorWhole(), m);
  EXPECT_FALSE(hasRule(r, "imitation_entry_match"));
}

TEST(ValidatorTest, SecondaryDominantResolutionVacuousWhenLeadingToneAbsent) {
  // V/V with no F# voiced — the LT-rise test is vacuously satisfied,
  // but degree-mismatch can still fire. Here we provide a correct V
  // next chord and no F# anywhere, so the rule passes.
  const Tick beat = kTicksPerBeat;
  std::vector<NoteEvent> notes = {
      makeNote(0, beat, 62, 0),     // D5 (root only)
      makeNote(0, beat, 50, 1),     // D4
      makeNote(beat, beat, 67, 0),  // G5
      makeNote(beat, beat, 55, 1),  // G4
  };
  std::vector<NoteProvenance> prov(notes.size(), makeProv(0, NoteSource::Compose));
  ValidationReport r = Validator{}.validate(
      notes, prov, secondaryDominantPlan(/*include_resolution=*/true), Material{});
  EXPECT_FALSE(hasRule(r, "secondary_dominant_resolution"));
}

// === Invertible counterpoint at the octave ===
//
// All invertibility tests use a 3-voice texture (V0=soprano, V1=alto, V2=inert
// bass). The Validator checks only the adjacent UPPER pair (V0, V1);
// the bottom pair (V1, V2) is excluded, so the tested interval lives
// in V0-V1 and V2 is kept well below to avoid voice-crossing noise.

TEST(ValidatorTest, InvertibleAt8vaFailsParallelOctavesUpperPair) {
  // V0/V1 form an octave at bar0 beat1 and bar1 beat1; both voices move.
  std::vector<NoteEvent> notes = {
      makeNote(0, kTicksPerBar, 72, 0),             // C5  V0
      makeNote(0, kTicksPerBar, 60, 1),             // C4  V1 -> octave
      makeNote(0, kTicksPerBar, 36, 2),             // C2  V2 (inert)
      makeNote(kTicksPerBar, kTicksPerBar, 74, 0),  // D5  V0
      makeNote(kTicksPerBar, kTicksPerBar, 62, 1),  // D4  V1 -> octave
      makeNote(kTicksPerBar, kTicksPerBar, 36, 2),  // C2  V2
  };
  std::vector<NoteProvenance> prov = {
      makeProv(0, NoteSource::Compose),  makeProv(1, NoteSource::Compose),
      makeProv(2, NoteSource::Material), makeProv(3, NoteSource::Compose),
      makeProv(4, NoteSource::Compose),  makeProv(5, NoteSource::Material),
  };
  ValidationReport r = Validator{}.validate(notes, prov, cMajorWhole(), Material{});
  EXPECT_TRUE(hasRule(r, "invertible_at_octave"));
}

TEST(ValidatorTest, InvertibleAt8vaPassesParallelFifthsUpperPair) {
  // V0/V1 form a perfect 5th at both strong ticks; both move. 5ths are
  // tolerated (they invert to 4ths).
  std::vector<NoteEvent> notes = {
      makeNote(0, kTicksPerBar, 67, 0),             // G4  V0
      makeNote(0, kTicksPerBar, 60, 1),             // C4  V1 -> 5th
      makeNote(0, kTicksPerBar, 36, 2),             // C2  V2
      makeNote(kTicksPerBar, kTicksPerBar, 69, 0),  // A4  V0
      makeNote(kTicksPerBar, kTicksPerBar, 62, 1),  // D4  V1 -> 5th
      makeNote(kTicksPerBar, kTicksPerBar, 36, 2),  // C2  V2
  };
  std::vector<NoteProvenance> prov(notes.size(), makeProv(0, NoteSource::Compose));
  ValidationReport r = Validator{}.validate(notes, prov, cMajorWhole(), Material{});
  EXPECT_FALSE(hasRule(r, "invertible_at_octave"));
}

TEST(ValidatorTest, InvertibleAt8vaPassesObliqueOctave) {
  // Octave at both ticks but only V0 moves; V1 stays static -> not a
  // parallel (both_moved false).
  std::vector<NoteEvent> notes = {
      makeNote(0, kTicksPerBar, 72, 0),             // C5  V0
      makeNote(0, kTicksPerBar, 60, 1),             // C4  V1
      makeNote(0, kTicksPerBar, 36, 2),             // C2  V2
      makeNote(kTicksPerBar, kTicksPerBar, 72, 0),  // C5  V0 (repeats)
      makeNote(kTicksPerBar, kTicksPerBar, 60, 1),  // C4  V1 (static)
      makeNote(kTicksPerBar, kTicksPerBar, 36, 2),  // C2  V2
  };
  std::vector<NoteProvenance> prov(notes.size(), makeProv(0, NoteSource::Compose));
  ValidationReport r = Validator{}.validate(notes, prov, cMajorWhole(), Material{});
  EXPECT_FALSE(hasRule(r, "invertible_at_octave"));
}

TEST(ValidatorTest, InvertibleAt8vaSkippedWhenBothMaterial) {
  // Parallel octaves in V0/V1 but both notes are Material at each tick.
  std::vector<NoteEvent> notes = {
      makeNote(0, kTicksPerBar, 72, 0),             // C5  V0
      makeNote(0, kTicksPerBar, 60, 1),             // C4  V1
      makeNote(0, kTicksPerBar, 36, 2),             // C2  V2
      makeNote(kTicksPerBar, kTicksPerBar, 74, 0),  // D5  V0
      makeNote(kTicksPerBar, kTicksPerBar, 62, 1),  // D4  V1
      makeNote(kTicksPerBar, kTicksPerBar, 36, 2),  // C2  V2
  };
  std::vector<NoteProvenance> prov = {
      makeProv(0, NoteSource::Material), makeProv(1, NoteSource::Material),
      makeProv(2, NoteSource::Material), makeProv(3, NoteSource::Material),
      makeProv(4, NoteSource::Material), makeProv(5, NoteSource::Material),
  };
  ValidationReport r = Validator{}.validate(notes, prov, cMajorWhole(), Material{});
  EXPECT_FALSE(hasRule(r, "invertible_at_octave"));
}

TEST(ValidatorTest, InvertibleAt8vaFiresWhenOneSideCompose) {
  // Parallel octaves with V0 Material, V1 Compose -> at-least-one
  // non-Material satisfied, rule fires.
  std::vector<NoteEvent> notes = {
      makeNote(0, kTicksPerBar, 72, 0),             // C5  V0
      makeNote(0, kTicksPerBar, 60, 1),             // C4  V1
      makeNote(0, kTicksPerBar, 36, 2),             // C2  V2
      makeNote(kTicksPerBar, kTicksPerBar, 74, 0),  // D5  V0
      makeNote(kTicksPerBar, kTicksPerBar, 62, 1),  // D4  V1
      makeNote(kTicksPerBar, kTicksPerBar, 36, 2),  // C2  V2
  };
  std::vector<NoteProvenance> prov = {
      makeProv(0, NoteSource::Material), makeProv(1, NoteSource::Compose),
      makeProv(2, NoteSource::Material), makeProv(3, NoteSource::Material),
      makeProv(4, NoteSource::Compose),  makeProv(5, NoteSource::Material),
  };
  ValidationReport r = Validator{}.validate(notes, prov, cMajorWhole(), Material{});
  EXPECT_TRUE(hasRule(r, "invertible_at_octave"));
}

TEST(ValidatorTest, InvertibleAt8vaIgnoresWeakBeatOctaves) {
  // Parallel octaves placed on weak-beat ticks only (beat 2 of bars);
  // strong-beat scope means no failure.
  const Tick weak0 = kTicksPerBeat;
  const Tick weak1 = kTicksPerBar + kTicksPerBeat;
  std::vector<NoteEvent> notes = {
      makeNote(weak0, kTicksPerBeat, 72, 0),  // C5  V0
      makeNote(weak0, kTicksPerBeat, 60, 1),  // C4  V1
      makeNote(weak0, kTicksPerBeat, 36, 2),  // C2  V2
      makeNote(weak1, kTicksPerBeat, 74, 0),  // D5  V0
      makeNote(weak1, kTicksPerBeat, 62, 1),  // D4  V1
      makeNote(weak1, kTicksPerBeat, 36, 2),  // C2  V2
  };
  std::vector<NoteProvenance> prov(notes.size(), makeProv(0, NoteSource::Compose));
  ValidationReport r = Validator{}.validate(notes, prov, cMajorWhole(), Material{});
  EXPECT_FALSE(hasRule(r, "invertible_at_octave"));
}

TEST(ValidatorTest, InvertibleAt8vaSkipsBottomPairIn3Voices) {
  // Parallel octaves in the bottom pair (V1/V2); V0/V1 consonant.
  // Bottom pair is excluded from the invertibility rule, so no failure.
  std::vector<NoteEvent> notes = {
      makeNote(0, kTicksPerBar, 76, 0),             // E5  V0
      makeNote(0, kTicksPerBar, 60, 1),             // C4  V1
      makeNote(0, kTicksPerBar, 48, 2),             // C3  V2 -> octave w/ V1
      makeNote(kTicksPerBar, kTicksPerBar, 77, 0),  // F5  V0
      makeNote(kTicksPerBar, kTicksPerBar, 62, 1),  // D4  V1
      makeNote(kTicksPerBar, kTicksPerBar, 50, 2),  // D3  V2 -> octave w/ V1
  };
  std::vector<NoteProvenance> prov(notes.size(), makeProv(0, NoteSource::Compose));
  ValidationReport r = Validator{}.validate(notes, prov, cMajorWhole(), Material{});
  EXPECT_FALSE(hasRule(r, "invertible_at_octave"));
}

TEST(ValidatorTest, FourthAboveActualBassFailsOnStructuralAccent) {
  std::vector<NoteEvent> notes = {
      makeNote(0, kTicksPerBar, 65, 0),  // F4
      makeNote(0, kTicksPerBar, 60, 1),  // C4 actual bass -> P4
  };
  std::vector<NoteProvenance> prov(notes.size(), makeProv(0, NoteSource::Compose));
  ValidationReport r = Validator{}.validate(notes, prov, cMajorWhole(), Material{});
  EXPECT_TRUE(hasRule(r, "vertical_dissonance"));
}

TEST(ValidatorTest, FourthAboveBassPassesOnWeakBeat) {
  const Tick weak = kTicksPerBeat;
  std::vector<NoteEvent> notes = {
      makeNote(weak, kTicksPerBeat, 65, 0),
      makeNote(weak, kTicksPerBeat, 60, 1),
  };
  std::vector<NoteProvenance> prov(notes.size(), makeProv(0, NoteSource::Compose));
  ValidationReport r = Validator{}.validate(notes, prov, cMajorWhole(), Material{});
  EXPECT_FALSE(hasRule(r, "vertical_dissonance"));
}

TEST(ValidatorTest, UpperVoiceFourthPassesWhenBothNotesAreConsonantAboveBass) {
  std::vector<NoteEvent> notes = {
      makeNote(0, kTicksPerBar, 65, 0),  // F4
      makeNote(0, kTicksPerBar, 60, 1),  // C4 -> upper-voice P4
      makeNote(0, kTicksPerBar, 53, 2),  // F3 actual bass
  };
  std::vector<NoteProvenance> prov(notes.size(), makeProv(0, NoteSource::Compose));
  ValidationReport r = Validator{}.validate(notes, prov, cMajorWhole(), Material{});
  EXPECT_FALSE(hasRule(r, "vertical_dissonance"));
}

TEST(ValidatorTest, FourthAboveBassSkipsFixedMaterialDuringGenerationValidation) {
  std::vector<NoteEvent> notes = {
      makeNote(0, kTicksPerBar, 65, 0),
      makeNote(0, kTicksPerBar, 60, 1),
  };
  std::vector<NoteProvenance> prov(notes.size(), makeProv(0, NoteSource::Material));
  ValidationReport r = Validator{}.validate(notes, prov, cMajorWhole(), Material{});
  EXPECT_FALSE(hasRule(r, "vertical_dissonance"));
}

// middle_entry_in_related_key tests. Home key is C major
// (cMajorWhole), so related keys are V=G(7), vi=A(9), IV=F(5), ii=D(2).

TEST(ValidatorTest, MiddleEntryInRelatedKeyPassesForDominant) {
  Material m;
  MiddleEntryDecl entry;
  entry.voice = 0;
  entry.related_key_pc = 7;  // V (G major)
  // All G-major diatonic: G(67) A(69) B(71) D(74).
  entry.notes = {{0, kTicksPerBeat, 67},
                 {kTicksPerBeat, kTicksPerBeat, 69},
                 {2 * kTicksPerBeat, kTicksPerBeat, 71},
                 {3 * kTicksPerBeat, kTicksPerBeat, 74}};
  m.middle_entries.push_back(entry);
  ValidationReport r = Validator{}.validate({}, {}, cMajorWhole(), m);
  EXPECT_FALSE(hasRule(r, "middle_entry_in_related_key"));
}

TEST(ValidatorTest, MiddleEntryInRelatedKeyFailsForUnrelatedKey) {
  Material m;
  MiddleEntryDecl entry;
  entry.voice = 0;
  entry.related_key_pc = 1;  // C# — not V/vi/IV/ii of C
  entry.notes = {{0, kTicksPerBeat, 61}};
  m.middle_entries.push_back(entry);
  ValidationReport r = Validator{}.validate({}, {}, cMajorWhole(), m);
  EXPECT_TRUE(hasRule(r, "middle_entry_in_related_key"));
}

TEST(ValidatorTest, MiddleEntryInRelatedKeyFailsForNonDiatonicNote) {
  Material m;
  MiddleEntryDecl entry;
  entry.voice = 0;
  entry.related_key_pc = 7;  // declared G major...
  // ...but F-natural (65, pc 5) is not in G major (which has F#=6).
  entry.notes = {{0, kTicksPerBeat, 67}, {kTicksPerBeat, kTicksPerBeat, 65}};
  m.middle_entries.push_back(entry);
  ValidationReport r = Validator{}.validate({}, {}, cMajorWhole(), m);
  EXPECT_TRUE(hasRule(r, "middle_entry_in_related_key"));
}

TEST(ValidatorTest, MiddleEntryInRelatedKeySkippedWhenEmpty) {
  Material m;
  ValidationReport r = Validator{}.validate({}, {}, cMajorWhole(), m);
  EXPECT_FALSE(hasRule(r, "middle_entry_in_related_key"));
}

// stretto_overlap_valid tests. The follower must enter strictly
// inside the leader's window and be the subject transposed by
// interval_semis.

namespace {

Material makeStrettoMaterial(Tick leader_entry, Tick leader_len, Tick follower_entry, int interval,
                             bool corrupt_pitch = false) {
  Material m;
  m.subject = {{0, kTicksPerBeat, 72}, {kTicksPerBeat, kTicksPerBeat, 74}};
  StrettoDecl s;
  s.leader_voice = 0;
  s.follower_voice = 2;
  s.leader_entry_tick = leader_entry;
  s.leader_length_ticks = leader_len;
  s.follower_entry_tick = follower_entry;
  s.interval_semis = interval;
  for (const auto& n : m.subject) {
    int p = static_cast<int>(n.pitch) + interval;
    if (corrupt_pitch)
      p += 1;
    s.follower_notes.push_back(
        {n.start_tick + follower_entry, n.duration, static_cast<std::uint8_t>(p)});
  }
  m.stretto_entries.push_back(s);
  return m;
}

}  // namespace

TEST(ValidatorTest, StrettoOverlapValidPassesForOverlappingOctave) {
  Material m = makeStrettoMaterial(0, 4 * kTicksPerBeat, 2 * kTicksPerBeat, -12);
  ValidationReport r = Validator{}.validate({}, {}, cMajorWhole(), m);
  EXPECT_FALSE(hasRule(r, "stretto_overlap_valid"));
}

TEST(ValidatorTest, StrettoOverlapValidFailsWhenNoOverlap) {
  // Follower enters exactly when the leader ends — no overlap.
  Material m = makeStrettoMaterial(0, 4 * kTicksPerBeat, 4 * kTicksPerBeat, -12);
  ValidationReport r = Validator{}.validate({}, {}, cMajorWhole(), m);
  EXPECT_TRUE(hasRule(r, "stretto_overlap_valid"));
}

TEST(ValidatorTest, StrettoOverlapValidFailsForWrongTransposition) {
  Material m = makeStrettoMaterial(0, 4 * kTicksPerBeat, 2 * kTicksPerBeat, -12,
                                   /*corrupt_pitch=*/true);
  ValidationReport r = Validator{}.validate({}, {}, cMajorWhole(), m);
  EXPECT_TRUE(hasRule(r, "stretto_overlap_valid"));
}

TEST(ValidatorTest, StrettoOverlapValidSkippedWhenEmpty) {
  Material m;
  ValidationReport r = Validator{}.validate({}, {}, cMajorWhole(), m);
  EXPECT_FALSE(hasRule(r, "stretto_overlap_valid"));
}

// pedal_point_tonic_or_dominant tests. Home key C: tonic pc 0,
// dominant pc 7.

TEST(ValidatorTest, PedalPointTonicPasses) {
  Material m;
  m.pedal_points.push_back({2, 0, 4 * kTicksPerBar, 60, false});  // C, pc 0
  ValidationReport r = Validator{}.validate({}, {}, cMajorWhole(), m);
  EXPECT_FALSE(hasRule(r, "pedal_point_tonic_or_dominant"));
}

TEST(ValidatorTest, PedalPointDominantPasses) {
  Material m;
  m.pedal_points.push_back({2, 0, 4 * kTicksPerBar, 55, true});  // G, pc 7
  ValidationReport r = Validator{}.validate({}, {}, cMajorWhole(), m);
  EXPECT_FALSE(hasRule(r, "pedal_point_tonic_or_dominant"));
}

TEST(ValidatorTest, PedalPointSubdominantFails) {
  Material m;
  m.pedal_points.push_back({2, 0, 4 * kTicksPerBar, 65, false});  // F, pc 5
  ValidationReport r = Validator{}.validate({}, {}, cMajorWhole(), m);
  EXPECT_TRUE(hasRule(r, "pedal_point_tonic_or_dominant"));
}

TEST(ValidatorTest, PedalPointSkippedWhenEmpty) {
  Material m;
  ValidationReport r = Validator{}.validate({}, {}, cMajorWhole(), m);
  EXPECT_FALSE(hasRule(r, "pedal_point_tonic_or_dominant"));
}

// phrase_periodicity_4_or_8_bar tests.

TEST(ValidatorTest, PhrasePeriodicityPassesForFourBarGrid) {
  Material m;
  for (int b = 0; b <= 12; b += 4)
    m.phrase_structure.phrase_start_ticks.push_back(static_cast<Tick>(b) * kTicksPerBar);
  ValidationReport r = Validator{}.validate({}, {}, cMajorWhole(), m);
  EXPECT_FALSE(hasRule(r, "phrase_periodicity_4_or_8_bar"));
}

TEST(ValidatorTest, PhrasePeriodicityPassesForEightBarSpan) {
  Material m;
  m.phrase_structure.phrase_start_ticks = {0, 8 * kTicksPerBar, 16 * kTicksPerBar};
  ValidationReport r = Validator{}.validate({}, {}, cMajorWhole(), m);
  EXPECT_FALSE(hasRule(r, "phrase_periodicity_4_or_8_bar"));
}

TEST(ValidatorTest, PhrasePeriodicityAcceptsElidedAndExtendedPhrases) {
  Material m;
  m.phrase_structure.phrase_start_ticks = {0, 3 * kTicksPerBar, 8 * kTicksPerBar};
  ValidationReport r = Validator{}.validate({}, {}, cMajorWhole(), m);
  EXPECT_FALSE(hasRule(r, "phrase_periodicity_4_or_8_bar"));
}

TEST(ValidatorTest, PhrasePeriodicityRejectsImplausiblyShortPhrase) {
  Material m;
  m.phrase_structure.phrase_start_ticks = {0, 2 * kTicksPerBar};
  ValidationReport r = Validator{}.validate({}, {}, cMajorWhole(), m);
  EXPECT_TRUE(hasRule(r, "phrase_periodicity_4_or_8_bar"));
}

TEST(ValidatorTest, PhrasePeriodicitySkippedWhenSinglePhrase) {
  Material m;
  m.phrase_structure.phrase_start_ticks = {0};
  ValidationReport r = Validator{}.validate({}, {}, cMajorWhole(), m);
  EXPECT_FALSE(hasRule(r, "phrase_periodicity_4_or_8_bar"));
}

// anacrusis_consistent tests. The pickup must begin exactly
// anacrusis_ticks before some declared phrase start.

TEST(ValidatorTest, AnacrusisConsistentPassesForAlignedPickup) {
  Material m;
  m.phrase_structure.has_anacrusis = true;
  m.phrase_structure.anacrusis_ticks = kTicksPerBeat;
  m.phrase_structure.phrase_start_ticks = {0, 4 * kTicksPerBar};
  RhythmFragment frag;
  frag.feature = RhythmFragment::Feature::Anacrusis;
  frag.voice = 0;
  frag.notes.push_back({4 * kTicksPerBar - kTicksPerBeat, kTicksPerBeat, 71});
  m.rhythm_fragments.push_back(frag);
  ValidationReport r = Validator{}.validate({}, {}, cMajorWhole(), m);
  EXPECT_FALSE(hasRule(r, "anacrusis_consistent"));
}

TEST(ValidatorTest, AnacrusisConsistentFailsForMisalignedPickup) {
  Material m;
  m.phrase_structure.has_anacrusis = true;
  m.phrase_structure.anacrusis_ticks = kTicksPerBeat;
  m.phrase_structure.phrase_start_ticks = {0, 4 * kTicksPerBar};
  RhythmFragment frag;
  frag.feature = RhythmFragment::Feature::Anacrusis;
  frag.voice = 0;
  // Pickup not anacrusis_ticks before any phrase start.
  frag.notes.push_back({4 * kTicksPerBar - 2 * kTicksPerBeat, kTicksPerBeat, 71});
  m.rhythm_fragments.push_back(frag);
  ValidationReport r = Validator{}.validate({}, {}, cMajorWhole(), m);
  EXPECT_TRUE(hasRule(r, "anacrusis_consistent"));
}

TEST(ValidatorTest, AnacrusisConsistentFailsWhenDeclaredButZeroLength) {
  Material m;
  m.phrase_structure.has_anacrusis = true;
  m.phrase_structure.anacrusis_ticks = 0;
  m.phrase_structure.phrase_start_ticks = {0, 4 * kTicksPerBar};
  ValidationReport r = Validator{}.validate({}, {}, cMajorWhole(), m);
  EXPECT_TRUE(hasRule(r, "anacrusis_consistent"));
}

TEST(ValidatorTest, AnacrusisConsistentFailsWhenFragmentWithoutDeclaration) {
  Material m;
  m.phrase_structure.has_anacrusis = false;
  RhythmFragment frag;
  frag.feature = RhythmFragment::Feature::Anacrusis;
  frag.voice = 0;
  frag.notes.push_back({0, kTicksPerBeat, 71});
  m.rhythm_fragments.push_back(frag);
  ValidationReport r = Validator{}.validate({}, {}, cMajorWhole(), m);
  EXPECT_TRUE(hasRule(r, "anacrusis_consistent"));
}

TEST(ValidatorTest, AnacrusisConsistentPassesWhenNoAnacrusis) {
  Material m;  // default: no anacrusis, no fragments
  ValidationReport r = Validator{}.validate({}, {}, cMajorWhole(), m);
  EXPECT_FALSE(hasRule(r, "anacrusis_consistent"));
}

// --- Texture / instrument / expression rules ---

TEST(ValidatorTest, VoiceRangeIntegrityPassesWhenInRange) {
  Material m;
  m.texture_plan.voice_ranges.push_back({/*voice=*/0, /*lo=*/60, /*hi=*/72});
  std::vector<NoteEvent> notes = {makeNote(0, kTicksPerBeat, 67, 0)};
  std::vector<NoteProvenance> prov = {makeProv(0, NoteSource::Compose)};
  ValidationReport r = Validator{}.validate(notes, prov, cMajorWhole(), m);
  EXPECT_FALSE(hasRule(r, "voice_range_integrity"));
}

TEST(ValidatorTest, VoiceRangeIntegrityFailsAboveCeiling) {
  Material m;
  m.texture_plan.voice_ranges.push_back({/*voice=*/0, /*lo=*/60, /*hi=*/72});
  std::vector<NoteEvent> notes = {makeNote(0, kTicksPerBeat, 84, 0)};
  std::vector<NoteProvenance> prov = {makeProv(0, NoteSource::Material)};
  ValidationReport r = Validator{}.validate(notes, prov, cMajorWhole(), m);
  EXPECT_TRUE(hasRule(r, "voice_range_integrity"));
}

TEST(ValidatorTest, VoiceRangeIntegrityFailsBelowFloor) {
  Material m;
  m.texture_plan.voice_ranges.push_back({/*voice=*/1, /*lo=*/48, /*hi=*/72});
  std::vector<NoteEvent> notes = {makeNote(0, kTicksPerBeat, 36, 1)};
  std::vector<NoteProvenance> prov = {makeProv(0, NoteSource::Material)};
  ValidationReport r = Validator{}.validate(notes, prov, cMajorWhole(), m);
  EXPECT_TRUE(hasRule(r, "voice_range_integrity"));
}

TEST(ValidatorTest, VoiceRangeIntegritySkippedForUndeclaredVoice) {
  Material m;
  // Only voice 0 has a declared range; voice 1's wild pitch is ignored.
  m.texture_plan.voice_ranges.push_back({/*voice=*/0, /*lo=*/60, /*hi=*/72});
  std::vector<NoteEvent> notes = {makeNote(0, kTicksPerBeat, 120, 1)};
  std::vector<NoteProvenance> prov = {makeProv(0, NoteSource::Compose)};
  ValidationReport r = Validator{}.validate(notes, prov, cMajorWhole(), m);
  EXPECT_FALSE(hasRule(r, "voice_range_integrity"));
}

TEST(ValidatorTest, VoiceRangeIntegritySkippedWhenNoRangesDeclared) {
  Material m;  // default: empty texture_plan
  std::vector<NoteEvent> notes = {makeNote(0, kTicksPerBeat, 127, 0)};
  std::vector<NoteProvenance> prov = {makeProv(0, NoteSource::Compose)};
  ValidationReport r = Validator{}.validate(notes, prov, cMajorWhole(), m);
  EXPECT_FALSE(hasRule(r, "voice_range_integrity"));
}

TEST(ValidatorTest, PedalRangeSoftPenaltyPassesInsideCompass) {
  Material m;
  m.texture_plan.pedal_voice = 2;
  // C1 (24) .. D3 (50): squarely inside the ideal pedal compass.
  std::vector<NoteEvent> notes = {makeNote(0, kTicksPerBeat, 36, 2)};
  std::vector<NoteProvenance> prov = {makeProv(0, NoteSource::Material)};
  ValidationReport r = Validator{}.validate(notes, prov, cMajorWhole(), m);
  EXPECT_FALSE(hasRule(r, "pedal_range_soft_penalty"));
}

TEST(ValidatorTest, PedalRangeSoftPenaltyPassesInSoftMargin) {
  Material m;
  m.texture_plan.pedal_voice = 2;
  // 55 sits just above the D3 (50) soft ceiling but inside the playable
  // band [12, 62]: a gradual penalty applies at scoring time, NOT a
  // hard rejection, so the guard must not fire.
  std::vector<NoteEvent> notes = {makeNote(0, kTicksPerBeat, 55, 2)};
  std::vector<NoteProvenance> prov = {makeProv(0, NoteSource::Material)};
  ValidationReport r = Validator{}.validate(notes, prov, cMajorWhole(), m);
  EXPECT_FALSE(hasRule(r, "pedal_range_soft_penalty"));
}

TEST(ValidatorTest, PedalRangeSoftPenaltyFailsBeyondPlayableBand) {
  Material m;
  m.texture_plan.pedal_voice = 2;
  // 80 is far above the playable pedal band: physically impossible.
  std::vector<NoteEvent> notes = {makeNote(0, kTicksPerBeat, 80, 2)};
  std::vector<NoteProvenance> prov = {makeProv(0, NoteSource::Material)};
  ValidationReport r = Validator{}.validate(notes, prov, cMajorWhole(), m);
  EXPECT_TRUE(hasRule(r, "pedal_range_soft_penalty"));
}

TEST(ValidatorTest, PedalRangeSoftPenaltyIgnoresNonPedalVoices) {
  Material m;
  m.texture_plan.pedal_voice = 2;
  // Out-of-band pitch in voice 0 (not the pedal voice) is ignored.
  std::vector<NoteEvent> notes = {makeNote(0, kTicksPerBeat, 96, 0)};
  std::vector<NoteProvenance> prov = {makeProv(0, NoteSource::Compose)};
  ValidationReport r = Validator{}.validate(notes, prov, cMajorWhole(), m);
  EXPECT_FALSE(hasRule(r, "pedal_range_soft_penalty"));
}

TEST(ValidatorTest, PedalRangeSoftPenaltySkippedWhenNoPedalVoice) {
  Material m;  // default: pedal_voice == 0xFF
  std::vector<NoteEvent> notes = {makeNote(0, kTicksPerBeat, 96, 0)};
  std::vector<NoteProvenance> prov = {makeProv(0, NoteSource::Compose)};
  ValidationReport r = Validator{}.validate(notes, prov, cMajorWhole(), m);
  EXPECT_FALSE(hasRule(r, "pedal_range_soft_penalty"));
}

// B1 regression: the parallel-perfect (and hidden-parallel) Material skip
// must gate on BOTH voices being Material, not just the lower (blamed)
// voice. A Compose upper voice running parallels against a Material lower
// voice is real and composer-fixable, so the rule MUST fire.

TEST(ValidatorTest, ParallelFifthFiresWhenUpperComposeLowerMaterial) {
  // Voice 0 (upper, Compose): C5 (60) -> D5 (62). Voice 1 (lower, Material):
  // F4 (53) -> G4 (55). Intervals 7, 7 (parallel P5), both voices moved.
  std::vector<NoteEvent> notes = {
      makeNote(0, kTicksPerBeat, 60, 0),
      makeNote(0, kTicksPerBeat, 53, 1),
      makeNote(kTicksPerBeat, kTicksPerBeat, 62, 0),
      makeNote(kTicksPerBeat, kTicksPerBeat, 55, 1),
  };
  std::vector<NoteProvenance> prov = {
      makeProv(0, NoteSource::Compose),
      makeProv(1, NoteSource::Material),
      makeProv(2, NoteSource::Compose),
      makeProv(3, NoteSource::Material),
  };
  ValidationReport r = Validator{}.validate(notes, prov, cMajorWhole());
  EXPECT_EQ(r.status, ValidationStatus::FailedSpan);
  EXPECT_TRUE(hasRule(r, "parallel_fifth"));
}

TEST(ValidatorTest, ParallelFifthStaysSkippedWhenBothMaterial) {
  // Same parallel fifths but BOTH voices are Material: the conflict-immune
  // zone is preserved (the composer cannot edit fixed inputs).
  std::vector<NoteEvent> notes = {
      makeNote(0, kTicksPerBeat, 60, 0),
      makeNote(0, kTicksPerBeat, 53, 1),
      makeNote(kTicksPerBeat, kTicksPerBeat, 62, 0),
      makeNote(kTicksPerBeat, kTicksPerBeat, 55, 1),
  };
  std::vector<NoteProvenance> prov(notes.size(), makeProv(0, NoteSource::Material));
  ValidationReport r = Validator{}.validate(notes, prov, cMajorWhole());
  EXPECT_FALSE(hasRule(r, "parallel_fifth"));
}

TEST(ValidatorTest, HiddenParallelFifthFiresWhenUpperComposeLowerMaterial) {
  // Similar motion into a perfect fifth: V0 (upper, Compose) C5 (60) -> E5
  // (64); V1 (lower, Material) E4 (52) -> A4 (57). Prev interval 8 (m6, not
  // perfect), current interval 7 (P5), both rise -> hidden parallel fifth.
  std::vector<NoteEvent> notes = {
      makeNote(0, kTicksPerBeat, 60, 0),
      makeNote(0, kTicksPerBeat, 52, 1),
      makeNote(kTicksPerBeat, kTicksPerBeat, 64, 0),
      makeNote(kTicksPerBeat, kTicksPerBeat, 57, 1),
  };
  std::vector<NoteProvenance> prov = {
      makeProv(0, NoteSource::Compose),
      makeProv(1, NoteSource::Material),
      makeProv(2, NoteSource::Compose),
      makeProv(3, NoteSource::Material),
  };
  ValidationReport r = Validator{}.validate(notes, prov, cMajorWhole());
  EXPECT_TRUE(hasRule(r, "hidden_parallel_fifth"));
}

TEST(ValidatorTest, HiddenParallelFifthStaysSkippedWhenBothMaterial) {
  std::vector<NoteEvent> notes = {
      makeNote(0, kTicksPerBeat, 60, 0),
      makeNote(0, kTicksPerBeat, 52, 1),
      makeNote(kTicksPerBeat, kTicksPerBeat, 64, 0),
      makeNote(kTicksPerBeat, kTicksPerBeat, 57, 1),
  };
  std::vector<NoteProvenance> prov(notes.size(), makeProv(0, NoteSource::Material));
  ValidationReport r = Validator{}.validate(notes, prov, cMajorWhole());
  EXPECT_FALSE(hasRule(r, "hidden_parallel_fifth"));
}

// B3: suspension_seventh_sixth verifies a Sus7_6 carrier forms a genuine
// 7th over the lowest sounding voice on the dissonance beat and resolves to
// a 6th. Gated on the SuspensionPrepared / SuspensionResolved provenance
// bits so it is a no-op outside real SuspensionCarrier spans.

namespace {

// Build a Sus7_6 fixture in V0 over a V1 bass, with the prep/res notes
// carrying the SuspensionPrepared / SuspensionResolved provenance bits.
// `sus_pitch` / `res_pitch` parameterize the suspended and resolution
// pitches so a test can make either a genuine 7-6 or a wrong interval.
struct Sus76Fixture {
  std::vector<NoteEvent> notes;
  std::vector<NoteProvenance> prov;
  Material material;
};

Sus76Fixture makeSus76(std::uint8_t prep_pitch, std::uint8_t sus_pitch, std::uint8_t res_pitch,
                       std::uint8_t bass_pitch) {
  Sus76Fixture fix;
  fix.notes = {
      makeNote(0, kTicksPerBeat, prep_pitch, 0),                 // V0 prep
      makeNote(kTicksPerBeat, kTicksPerBeat, sus_pitch, 0),      // V0 suspension
      makeNote(kTicksPerBeat * 2, kTicksPerBeat, res_pitch, 0),  // V0 resolution
      makeNote(0, kTicksPerBeat * 3, bass_pitch, 1),             // V1 bass (held)
  };
  fix.prov = std::vector<NoteProvenance>(fix.notes.size(), makeProv(0, NoteSource::Material));
  fix.prov[0].satisfied_rules = ruleBitMask(RuleBit::SuspensionPrepared);
  fix.prov[2].satisfied_rules = ruleBitMask(RuleBit::SuspensionResolved);
  SuspensionPattern sp;
  sp.type = SuspensionType::Sus7_6;
  sp.preparation_tick = 0;
  sp.suspension_tick = kTicksPerBeat;
  sp.resolution_tick = kTicksPerBeat * 2;
  sp.preparation_pitch = prep_pitch;
  sp.suspension_pitch = sus_pitch;
  sp.resolution_pitch = res_pitch;
  sp.voice = 0;
  fix.material.suspension_patterns.push_back(sp);
  return fix;
}

}  // namespace

TEST(ValidatorTest, SuspensionSeventhSixthPassesOnRealSeventh) {
  // Bass C3 (48). Suspension Bb4 (70): 70-48=22, ic 10 (m7). Resolution
  // A4 (69): 69-48=21, ic 9 (M6). Genuine 7-6 -> rule passes.
  Sus76Fixture fix = makeSus76(/*prep=*/70, /*sus=*/70, /*res=*/69, /*bass=*/48);
  ValidationReport r = Validator{}.validate(fix.notes, fix.prov, cMajorTwoBars(), fix.material);
  EXPECT_FALSE(hasRule(r, "suspension_seventh_sixth"));
}

TEST(ValidatorTest, SuspensionSeventhSixthFiresOnWrongInterval) {
  // Bass C3 (48). Suspension C5 (72): 72-48=24, ic 0 (octave, NOT a 7th).
  // Resolution B4 (71): 71-48=23, ic 11 (maj 7th, NOT a 6th). Rule fires.
  Sus76Fixture fix = makeSus76(/*prep=*/72, /*sus=*/72, /*res=*/71, /*bass=*/48);
  ValidationReport r = Validator{}.validate(fix.notes, fix.prov, cMajorTwoBars(), fix.material);
  EXPECT_EQ(r.status, ValidationStatus::FailedSpan);
  EXPECT_TRUE(hasRule(r, "suspension_seventh_sixth"));
}

TEST(ValidatorTest, SuspensionSeventhSixthSkippedWithoutProvenanceBits) {
  // Same wrong interval as above but the prep/res notes carry no suspension
  // provenance bits (Material with satisfied_rules == 0). The rule must be a
  // no-op so phases with no shipped SuspensionCarrier are never penalized.
  Sus76Fixture fix = makeSus76(/*prep=*/72, /*sus=*/72, /*res=*/71, /*bass=*/48);
  fix.prov[0].satisfied_rules = 0;
  fix.prov[2].satisfied_rules = 0;
  ValidationReport r = Validator{}.validate(fix.notes, fix.prov, cMajorTwoBars(), fix.material);
  EXPECT_FALSE(hasRule(r, "suspension_seventh_sixth"));
}

// countersubject_invertible: a countersubject (voice 0, CountersubjectInvertible
// bit) sounding against a subject (voice 1, SubjectCarrier intent). Two bar
// downbeats (strong beats). The upper countersubject stays above the lower
// subject so no voice-crossing fires.
TEST(ValidatorTest, CountersubjectInvertiblePassesOnConsonantStrongBeats) {
  // Bar 0 downbeat: cs E4 (64) over subject C4 (60) -> major third (interval 4).
  // Bar 1 downbeat: cs F4 (65) over subject A3 (57) -> minor sixth (interval 8).
  // Neither reduces to a perfect fifth, so the pair is invertible at the octave.
  std::vector<NoteEvent> notes = {
      makeNote(0, kTicksPerBar, 64, 0),
      makeNote(kTicksPerBar, kTicksPerBar, 65, 0),
      makeNote(0, kTicksPerBar, 60, 1),
      makeNote(kTicksPerBar, kTicksPerBar, 57, 1),
  };
  std::vector<NoteProvenance> prov = {
      makeProvIntent(0, NoteSource::Material, VoiceIntent::CountersubjectCarrier),
      makeProvIntent(0, NoteSource::Material, VoiceIntent::CountersubjectCarrier),
      makeProvIntent(1, NoteSource::Material, VoiceIntent::SubjectCarrier),
      makeProvIntent(1, NoteSource::Material, VoiceIntent::SubjectCarrier),
  };
  prov[0].satisfied_rules |= ruleBitMask(RuleBit::CountersubjectInvertible);
  prov[1].satisfied_rules |= ruleBitMask(RuleBit::CountersubjectInvertible);

  ValidationReport r = Validator{}.validate(notes, prov, cMajorWhole());
  EXPECT_FALSE(hasInformational(r, "countersubject_invertible"));
  EXPECT_FALSE(hasRule(r, "countersubject_invertible"));
}

TEST(ValidatorTest, CountersubjectInvertibleReportsStrongBeatPerfectFifth) {
  // Bar 1 downbeat: cs G4 (67) over subject C4 (60) -> perfect fifth (interval
  // 7), which inverts to a fourth. The rule records ONE informational
  // observation (MusicalFail kind) but does NOT gate: status stays Ok and the
  // gating failures list stays empty.
  std::vector<NoteEvent> notes = {
      makeNote(0, kTicksPerBar, 64, 0),
      makeNote(kTicksPerBar, kTicksPerBar, 67, 0),
      makeNote(0, kTicksPerBar, 60, 1),
      makeNote(kTicksPerBar, kTicksPerBar, 60, 1),
  };
  std::vector<NoteProvenance> prov = {
      makeProvIntent(0, NoteSource::Material, VoiceIntent::CountersubjectCarrier),
      makeProvIntent(0, NoteSource::Material, VoiceIntent::CountersubjectCarrier),
      makeProvIntent(1, NoteSource::Material, VoiceIntent::SubjectCarrier),
      makeProvIntent(1, NoteSource::Material, VoiceIntent::SubjectCarrier),
  };
  prov[0].satisfied_rules |= ruleBitMask(RuleBit::CountersubjectInvertible);
  prov[1].satisfied_rules |= ruleBitMask(RuleBit::CountersubjectInvertible);

  ValidationReport r = Validator{}.validate(notes, prov, cMajorWhole());
  EXPECT_TRUE(hasInformational(r, "countersubject_invertible"));
  EXPECT_FALSE(hasRule(r, "countersubject_invertible"));
  for (const auto& f : r.informational) {
    if (f.rule_id == "countersubject_invertible") {
      EXPECT_EQ(f.kind, FailKind::MusicalFail);
      // Located at the countersubject note; outside the finding recorder, so no tally.
      EXPECT_EQ(f.span_id, 0u);
    }
  }
  for (const auto& observation : r.observations)
    EXPECT_NE(observation.rule_id, "countersubject_invertible");
  EXPECT_EQ(counterpointRuleGeometry("countersubject_invertible"), RuleGeometry::Vertical);
}

TEST(ValidatorTest, CountersubjectInvertibleIgnoresFreeFigurationSharingAVoice) {
  // Voice 0 carries the countersubject in bar 0 and free figuration in bar 1; voice 1 carries
  // the subject in bar 0 and free figuration in bar 1. The perfect fifth in bar 1 is between
  // two free-figuration notes, so no countersubject/subject pair forms it.
  std::vector<NoteEvent> notes = {
      makeNote(0, kTicksPerBar, 64, 0),
      makeNote(kTicksPerBar, kTicksPerBar, 67, 0),
      makeNote(0, kTicksPerBar, 60, 1),
      makeNote(kTicksPerBar, kTicksPerBar, 60, 1),
  };
  std::vector<NoteProvenance> prov = {
      makeProvIntent(0, NoteSource::Material, VoiceIntent::CountersubjectCarrier),
      makeProvIntent(0, NoteSource::Material, VoiceIntent::SequentialCounterline),
      makeProvIntent(1, NoteSource::Material, VoiceIntent::SubjectCarrier),
      makeProvIntent(1, NoteSource::Material, VoiceIntent::SequentialCounterline),
  };
  prov[0].satisfied_rules |= ruleBitMask(RuleBit::CountersubjectInvertible);

  ValidationReport r = Validator{}.validate(notes, prov, cMajorWhole());
  EXPECT_FALSE(hasInformational(r, "countersubject_invertible"));
}

TEST(ValidatorTest, CountersubjectInvertibleReadsMiddleEntrySubjectStatements) {
  std::vector<NoteEvent> notes = {
      makeNote(0, kTicksPerBar, 67, 0),
      makeNote(0, kTicksPerBar, 60, 1),
  };
  std::vector<NoteProvenance> prov = {
      makeProvIntent(0, NoteSource::Material, VoiceIntent::CountersubjectCarrier),
      makeProvIntent(1, NoteSource::Material, VoiceIntent::MiddleEntryCarrier),
  };
  prov[0].satisfied_rules |= ruleBitMask(RuleBit::CountersubjectInvertible);

  ValidationReport r = Validator{}.validate(notes, prov, cMajorWhole());
  EXPECT_TRUE(hasInformational(r, "countersubject_invertible"));
}

TEST(CompassCountTest, JudgesSoundingPitchesAgainstTheInstrumentRange) {
  // 90 is inside the violin and harpsichord range {36, 96} but above the organ's 84.
  std::vector<NoteEvent> notes = {
      makeNote(0, kTicksPerBeat, 40, 0),
      makeNote(0, kTicksPerBeat, 60, 0),
      makeNote(0, kTicksPerBeat, 90, 0),
  };
  EXPECT_EQ(countCompassViolations(notes, InstrumentType::Violin), 0);
  EXPECT_EQ(countCompassViolations(notes, InstrumentType::Harpsichord), 0);
  EXPECT_EQ(countCompassViolations(notes, InstrumentType::Organ), 1);
  // A +7 sounding shift moves 90 past the violin ceiling.
  EXPECT_EQ(countCompassViolations(notes, InstrumentType::Violin, 7), 1);
  // Composed-frame texture metrics keep judging the organ compass.
  EXPECT_EQ(computeTextureMetrics(notes).compass_violation_count, 1);
}

TEST(SpanIdFormatTest, BudgetFailureCarriesTheLocatedSpanOrRendersAsAbsent) {
  ValidationReport report;
  RuleObservation observation;
  observation.rule_id = "parallel_fifth";
  observation.total = 1;
  report.observations.push_back(observation);

  // No located finding: the failure has no span and must not print the raw sentinel.
  applyCounterpointBudget(FormType::ChoralePrelude, &report);
  ASSERT_EQ(report.failures.size(), 1u);
  EXPECT_EQ(report.failures[0].span_id, kInvalidSpanId);
  EXPECT_EQ(formatSpanId(report.failures[0].span_id), "no span");

  // A recorded finding of the same rule locates the promoted failure.
  report.failures.clear();
  ValidationFailure finding;
  finding.span_id = 5;
  finding.rule_id = "parallel_fifth";
  report.informational.push_back(finding);
  applyCounterpointBudget(FormType::ChoralePrelude, &report);
  ASSERT_EQ(report.failures.size(), 1u);
  EXPECT_EQ(report.failures[0].span_id, 5u);
  EXPECT_EQ(formatSpanId(report.failures[0].span_id), "span 5");
}

TEST(SpanIdFormatTest, SentinelRendersAsAbsentNeverAsANumber) {
  EXPECT_EQ(formatSpanId(kInvalidSpanId), "no span");
  EXPECT_EQ(formatSpanId(7), "span 7");
  EXPECT_EQ(formatSpanId(kInvalidSpanId).find("4294967295"), std::string::npos);
}

TEST(ValidatorTest, CountersubjectInvertibleInertWithoutBit) {
  // Same perfect fifth on the strong beat, but no CountersubjectInvertible bit:
  // the rule is inert (no countersubject declared in this fixture).
  std::vector<NoteEvent> notes = {
      makeNote(kTicksPerBar, kTicksPerBar, 67, 0),
      makeNote(kTicksPerBar, kTicksPerBar, 60, 1),
  };
  std::vector<NoteProvenance> prov = {
      makeProvIntent(0, NoteSource::Material, VoiceIntent::SequentialCounterline),
      makeProvIntent(1, NoteSource::Material, VoiceIntent::SubjectCarrier),
  };
  ValidationReport r = Validator{}.validate(notes, prov, cMajorWhole());
  EXPECT_FALSE(hasInformational(r, "countersubject_invertible"));
  EXPECT_FALSE(hasRule(r, "countersubject_invertible"));
}

namespace {

// Deterministic pseudo-random source for the equivalence sweeps below.
class SweepRandom {
 public:
  explicit SweepRandom(std::uint32_t seed) : state_(seed) {}
  std::uint32_t below(std::uint32_t bound) {
    state_ = state_ * 1664525u + 1013904223u;
    return (state_ >> 8) % bound;
  }

 private:
  std::uint32_t state_;
};

// Notes in `voice_count` voices on a sixteenth grid: mostly successive tones,
// with same-voice overlaps, shared onsets, rests and zero-length notes mixed in.
std::vector<NoteEvent> sweepNotes(std::uint32_t seed, int voice_count, int per_voice) {
  SweepRandom random(seed);
  std::vector<NoteEvent> notes;
  for (int voice = 0; voice < voice_count; ++voice) {
    Tick cursor = random.below(4) * 120;
    for (int idx = 0; idx < per_voice; ++idx) {
      const Tick duration = random.below(10) == 0 ? 0 : (1 + random.below(8)) * 120;
      const auto pitch = static_cast<std::uint8_t>(48 + 12 * (2 - voice % 3) + random.below(14));
      notes.push_back(makeNote(cursor, duration, pitch, static_cast<VoiceId>(voice)));
      const std::uint32_t step = random.below(12);
      if (step == 0)
        continue;  // next note shares this onset
      cursor += step < 3 ? duration / 2 + 120 : duration + (step == 11 ? 480 : 0);
      if (duration == 0)
        cursor += 120;
    }
  }
  // Interleave voices so note-list order differs from onset order.
  std::vector<NoteEvent> shuffled;
  while (!notes.empty()) {
    const std::size_t pick = random.below(static_cast<std::uint32_t>(notes.size()));
    shuffled.push_back(notes[pick]);
    notes.erase(notes.begin() + static_cast<std::ptrdiff_t>(pick));
  }
  return shuffled;
}

}  // namespace

TEST(ValidatorEquivalenceTest, TextureMetricsMatchPerIntervalRescan) {
  for (std::uint32_t seed = 1; seed <= 40; ++seed) {
    const std::vector<NoteEvent> notes = sweepNotes(seed, 1 + seed % 4, 30);
    std::vector<Tick> bounds;
    std::vector<VoiceId> voices;
    for (const auto& note : notes) {
      bounds.push_back(note.start_tick);
      bounds.push_back(note.start_tick + note.duration);
      if (std::find(voices.begin(), voices.end(), note.voice) == voices.end())
        voices.push_back(note.voice);
    }
    std::sort(bounds.begin(), bounds.end());
    bounds.erase(std::unique(bounds.begin(), bounds.end()), bounds.end());
    int max_active = 0;
    long active_ticks = 0;
    long mono_ticks = 0;
    long total_ticks = 0;
    for (std::size_t idx = 0; idx + 1 < bounds.size(); ++idx) {
      const Tick begin = bounds[idx];
      const Tick end = bounds[idx + 1];
      int active = 0;
      for (VoiceId voice : voices) {
        active += std::any_of(notes.begin(), notes.end(), [&](const NoteEvent& note) {
          return note.voice == voice && note.start_tick < end &&
                 begin < note.start_tick + note.duration;
        });
      }
      max_active = std::max(max_active, active);
      active_ticks += static_cast<long>(active) * static_cast<long>(end - begin);
      mono_ticks += active == 1 ? static_cast<long>(end - begin) : 0;
      total_ticks += static_cast<long>(end - begin);
    }
    const TextureMetrics metrics = computeTextureMetrics(notes);
    EXPECT_EQ(metrics.max_active_voices, max_active) << "seed " << seed;
    EXPECT_EQ(metrics.avg_active_voices,
              static_cast<double>(active_ticks) / static_cast<double>(total_ticks))
        << "seed " << seed;
    EXPECT_EQ(metrics.mono_ratio,
              static_cast<double>(mono_ticks) / static_cast<double>(total_ticks))
        << "seed " << seed;
  }
}

TEST(ValidatorEquivalenceTest, OnsetIndexSoundingQueriesMatchLinearScan) {
  for (std::uint32_t seed = 1; seed <= 20; ++seed) {
    const std::vector<NoteEvent> notes = sweepNotes(seed, 3, 40);
    const VoiceOnsetIndex index(notes);
    Tick last_tick = 0;
    for (const auto& note : notes)
      last_tick = std::max<Tick>(last_tick, note.start_tick + note.duration);
    for (VoiceId voice = 0; voice < 3; ++voice) {
      for (Tick tick = 0; tick <= last_tick + 120; tick += 60) {
        // The latest onset wins; among equal onsets, the last in note-list order.
        std::size_t expected = notes.size();
        std::vector<std::size_t> sounding;
        for (std::size_t idx = 0; idx < notes.size(); ++idx) {
          const NoteEvent& note = notes[idx];
          if (note.voice != voice ||
              !(note.start_tick <= tick && tick < note.start_tick + note.duration))
            continue;
          sounding.push_back(idx);
          if (expected == notes.size() || note.start_tick >= notes[expected].start_tick)
            expected = idx;
        }
        EXPECT_EQ(index.soundingAt(voice, tick), expected) << "seed " << seed << " tick " << tick;
        std::vector<std::size_t> visited;
        index.forEachSoundingAt(voice, tick, [&visited](std::size_t idx) {
          visited.push_back(idx);
          return true;
        });
        std::sort(visited.begin(), visited.end());
        EXPECT_EQ(visited, sounding) << "seed " << seed << " tick " << tick;
      }
    }
  }
}

TEST(ValidatorEquivalenceTest, CrossRelationFindingsMatchAllPairsWalk) {
  HarmonicPlan plan = cMajorWhole();
  plan.modulations.push_back({2 * kTicksPerBar, 0, 7, false, false, ModulationType::Phrase});
  plan.modulations.push_back({5 * kTicksPerBar, 7, 9, false, true, ModulationType::Phrase});
  for (std::uint32_t seed = 1; seed <= 12; ++seed) {
    const std::vector<NoteEvent> notes = sweepNotes(seed, 3, 36);
    SweepRandom random(seed * 7919u);
    std::vector<NoteProvenance> prov;
    for (std::size_t idx = 0; idx < notes.size(); ++idx) {
      prov.push_back(makeProv(static_cast<SpanId>(idx),
                              random.below(3) == 0 ? NoteSource::Compose : NoteSource::Material));
    }
    const VoiceOnsetIndex index(notes);
    for (const ValidationScope scope : {ValidationScope::Generation, ValidationScope::FinalScore}) {
      // Every later note of another voice, in index order, judged by the rule's
      // own predicate. Final-score findings between two composer-authored
      // (Compose) notes are routed to informational evidence; the rest gate.
      std::vector<SpanId> expected;
      std::vector<SpanId> expected_informational;
      for (std::size_t i = 0; i < notes.size(); ++i) {
        for (std::size_t j = i + 1; j < notes.size(); ++j) {
          const NoteEvent& lhs = notes[i];
          const NoteEvent& rhs = notes[j];
          if (lhs.voice == rhs.voice)
            continue;
          if (scope == ValidationScope::Generation && prov[i].source == NoteSource::Material &&
              prov[j].source == NoteSource::Material)
            continue;
          const bool overlap =
              (lhs.start_tick <= rhs.start_tick &&
               rhs.start_tick < lhs.start_tick + lhs.duration) ||
              (rhs.start_tick <= lhs.start_tick && lhs.start_tick < rhs.start_tick + rhs.duration);
          const Tick first = std::min(lhs.start_tick, rhs.start_tick);
          const Tick second = std::max(lhs.start_tick, rhs.start_tick);
          const bool adjacent = !index.hasOnsetBetween(lhs.voice, first, second) &&
                                !index.hasOnsetBetween(rhs.voice, first, second);
          if (!overlap && !adjacent)
            continue;
          const auto key = rule_helpers::keyAt(plan, second);
          if (!rule_helpers::isCrossRelationPc(rule_helpers::pitchClass(lhs.pitch),
                                               rule_helpers::pitchClass(rhs.pitch), key.tonic_pc,
                                               key.is_minor))
            continue;
          const bool authored_pair = scope == ValidationScope::FinalScore &&
                                     prov[i].source == NoteSource::Compose &&
                                     prov[j].source == NoteSource::Compose;
          (authored_pair ? expected_informational : expected).push_back(prov[j].span_id);
        }
      }
      const ValidationReport report = Validator{}.validate(notes, prov, plan, Material{}, scope);
      std::vector<SpanId> reported;
      std::vector<SpanId> reported_informational;
      for (const auto& finding : report.informational) {
        if (finding.rule_id == "cross_relation")
          reported_informational.push_back(finding.span_id);
      }
      for (const auto& failure : report.failures) {
        if (failure.rule_id == "cross_relation")
          reported.push_back(failure.span_id);
      }
      int observed = 0;
      for (const auto& observation : report.observations) {
        if (observation.rule_id == "cross_relation")
          observed = observation.total;
      }
      EXPECT_EQ(reported, expected) << "seed " << seed;
      EXPECT_EQ(reported_informational, expected_informational) << "seed " << seed;
      EXPECT_EQ(observed, static_cast<int>(expected.size() + expected_informational.size()))
          << "seed " << seed;
    }
  }
}

TEST(ValidatorEquivalenceTest, TrioIndependenceReadsTheFirstListedNoteOfASharedOnset) {
  // Voice 0 strikes C5 and C6 together; the earlier-listed C5 is its pitch, so
  // both voices rise into the next onset and the pair is fully dependent.
  const std::vector<NoteEvent> notes = {
      makeNote(0, kTicksPerBeat, 60, 0),
      makeNote(0, kTicksPerBeat, 50, 1),
      makeNote(0, kTicksPerBeat, 72, 0),
      makeNote(kTicksPerBeat, kTicksPerBeat, 65, 0),
      makeNote(kTicksPerBeat, kTicksPerBeat, 55, 1),
  };
  std::vector<NoteProvenance> prov;
  for (std::size_t idx = 0; idx < notes.size(); ++idx) {
    NoteProvenance entry = makeProv(0, NoteSource::Material);
    entry.satisfied_rules |= ruleBitMask(RuleBit::TrioVoiceIndependent);
    prov.push_back(entry);
  }
  EXPECT_TRUE(hasRule(Validator{}.validate(notes, prov, cMajorWhole(), Material{}),
                      "voice_independence_threshold"));
}

TEST(ValidatorEquivalenceTest, GoldbergAriaBassNeedsEveryDeclaredToneReplayed) {
  Material material;
  material.goldberg_aria_bass = {{0, kTicksPerBar, 48}, {kTicksPerBar, kTicksPerBar, 43}};
  material.goldberg_aria_bass_period = 2 * kTicksPerBar;
  std::vector<NoteEvent> notes;
  for (Tick base = 0; base < 4 * kTicksPerBar; base += 2 * kTicksPerBar) {
    notes.push_back(makeNote(base, kTicksPerBar, 48, 1));
    notes.push_back(makeNote(base + kTicksPerBar, kTicksPerBar, 43, 1));
  }
  std::vector<NoteProvenance> prov(notes.size(), makeProv(0, NoteSource::Material));
  for (auto& entry : prov)
    entry.satisfied_rules |= ruleBitMask(RuleBit::GoldbergBassReplayed);
  const auto fires = [&](const std::vector<NoteEvent>& score,
                         const std::vector<NoteProvenance>& provenance) {
    return hasRule(Validator{}.validate(score, provenance, cMajorWhole(), material),
                   "goldberg_aria_bass_immutable");
  };

  EXPECT_FALSE(fires(notes, prov));
  std::vector<NoteEvent> shortened = notes;
  shortened[3].duration = kTicksPerBeat;
  EXPECT_TRUE(fires(shortened, prov));
  std::vector<NoteProvenance> unstamped(notes.size(), makeProv(0, NoteSource::Material));
  EXPECT_TRUE(fires(notes, unstamped));
}

namespace {

// Three voices around a declared 4-3 suspension of C5 over a G2 bass on the
// second downbeat; `middle_pitch` is the inner voice sounding with it.
int verticalFindingsAroundSuspension(std::uint8_t middle_pitch) {
  const std::vector<NoteEvent> notes = {
      makeNote(0, 2880, 72, 0),         makeNote(2880, 960, 71, 0),
      makeNote(0, kTicksPerBar, 64, 1), makeNote(kTicksPerBar, 960, middle_pitch, 1),
      makeNote(0, kTicksPerBar, 48, 2), makeNote(kTicksPerBar, kTicksPerBar, 43, 2),
  };
  std::vector<NoteProvenance> prov(notes.size(), makeProv(1, NoteSource::Compose));
  Material material;
  SuspensionPattern suspension;
  suspension.type = SuspensionType::Sus4_3;
  suspension.preparation_tick = 0;
  suspension.suspension_tick = kTicksPerBar;
  suspension.resolution_tick = 2880;
  suspension.preparation_pitch = 72;
  suspension.suspension_pitch = 72;
  suspension.resolution_pitch = 71;
  suspension.voice = 0;
  material.suspension_patterns.push_back(suspension);
  const ValidationReport report = Validator{}.validate(notes, prov, cMajorWhole(), material);
  for (const auto& observation : report.observations) {
    if (observation.rule_id == "vertical_dissonance")
      return observation.total;
  }
  return 0;
}

}  // namespace

TEST(ValidatorTest, DeclaredSuspensionExcusesTheInnerVoiceAFourthBelowIt) {
  // G3 is a fourth below the suspended C5 and an octave above the bass.
  EXPECT_EQ(verticalFindingsAroundSuspension(55), 0);
}

TEST(ValidatorTest, DeclaredSuspensionDoesNotExcuseAnInnerVoiceDissonantWithTheBass) {
  // F5 is a fourth above the suspended C5 but a seventh above the bass, so both
  // of its pairs stay dissonant.
  EXPECT_EQ(verticalFindingsAroundSuspension(77), 2);
}

TEST(CounterpointBudgetTest, BudgetFailureLeavesTheFindingTallyUntouched) {
  ASSERT_TRUE(counterpointRuleIsClosed(FormType::Fugue, "parallel_fifth"));
  ValidationReport report;
  ValidationFailure finding;
  finding.span_id = 3;
  finding.rule_id = "parallel_fifth";
  report.informational = {finding, finding};
  RuleObservation observation;
  observation.rule_id = "parallel_fifth";
  observation.total = 2;
  report.observations.push_back(observation);

  applyCounterpointBudget(FormType::Fugue, &report);

  ASSERT_EQ(report.failures.size(), 1u);
  EXPECT_EQ(report.failures[0].rule_id, "parallel_fifth");
  // The promoted failure is located at the first recorded finding of the rule.
  EXPECT_EQ(report.failures[0].span_id, 3u);
  const RuleObservation& tally = report.observations[0];
  EXPECT_EQ(tally.gated, 0);
  EXPECT_EQ(tally.exempted, 0);
  EXPECT_EQ(tally.total - tally.gated - tally.exempted,
            static_cast<int>(report.informational.size()));
}

namespace {

struct ImmutableReplayCase {
  HarnessFixture fixture;
  ComposeResult score;
  VoiceIntent protected_intent = VoiceIntent::SubjectCarrier;
  std::string rule_id;
};

bool buildImmutableReplayCase(FormType form, std::uint16_t bars, VoiceIntent protected_intent,
                              const char* rule_id, ImmutableReplayCase* out_case) {
  ComposeRequest request;
  request.form = form;
  request.seed = 42;
  request.target_bars = bars;
  if (buildFormFixture(request, &out_case->fixture) != FormDirectorStatus::Ok)
    return false;
  out_case->score = Composer{}.run(out_case->fixture.material, out_case->fixture.harmony,
                                   out_case->fixture.voice_plan);
  out_case->protected_intent = protected_intent;
  out_case->rule_id = rule_id;
  return true;
}

std::vector<std::size_t> protectedNoteIndices(const ImmutableReplayCase& replay) {
  std::vector<std::size_t> indices;
  for (std::size_t index = 0; index < replay.score.provenance.size(); ++index) {
    if (replay.score.provenance[index].voice_intent == replay.protected_intent)
      indices.push_back(index);
  }
  return indices;
}

void eraseAlignedNote(ComposeResult* score, std::size_t index) {
  score->notes.erase(score->notes.begin() + static_cast<std::ptrdiff_t>(index));
  score->provenance.erase(score->provenance.begin() + static_cast<std::ptrdiff_t>(index));
}

bool hasProvenanceBit(const NoteProvenance& provenance, RuleBit bit) {
  return (provenance.satisfied_rules & ruleBitMask(bit)) != 0;
}

ValidationReport validateReplay(const ImmutableReplayCase& replay, const ComposeResult& score) {
  return Validator{}.validate(score.notes, score.provenance, replay.fixture.harmony,
                              replay.fixture.material, ValidationScope::FinalScore);
}

void expectStructuralImmutableFailure(const ValidationReport& report, const char* rule_id) {
  bool found = false;
  for (const ValidationFailure& failure : report.failures) {
    if (failure.rule_id == rule_id && failure.kind == FailKind::StructuralFail) {
      found = true;
      break;
    }
  }
  EXPECT_TRUE(found) << "expected StructuralFail for " << rule_id;
}

}  // namespace

TEST(ValidatorImmutableReplayTest, GeneratedProtectedLinesFailClosedForMissingAndDuplicateNotes) {
  struct CaseSpec {
    FormType form;
    std::uint16_t bars;
    VoiceIntent intent;
    const char* rule_id;
  };
  const CaseSpec specs[] = {
      {FormType::Chaconne, 24, VoiceIntent::GroundCarrier, "ground_bass_immutable"},
      {FormType::Passacaglia, 24, VoiceIntent::PassacagliaGround, "passacaglia_ground_immutable"},
      {FormType::ChoralePrelude, 24, VoiceIntent::CantusFirmusCarrier, "cantus_firmus_immutable"},
      {FormType::GoldbergVariations, 128, VoiceIntent::GoldbergBassCarrier,
       "goldberg_aria_bass_immutable"},
  };

  for (const CaseSpec& spec : specs) {
    ImmutableReplayCase replay;
    ASSERT_TRUE(buildImmutableReplayCase(spec.form, spec.bars, spec.intent, spec.rule_id, &replay));
    const std::vector<std::size_t> indices = protectedNoteIndices(replay);
    ASSERT_GE(indices.size(), 3u) << spec.rule_id;
    EXPECT_FALSE(hasRule(validateReplay(replay, replay.score), spec.rule_id)) << spec.rule_id;

    for (const std::size_t protected_position :
         {std::size_t{0}, indices.size() / 2, indices.size() - 1}) {
      ComposeResult missing = replay.score;
      eraseAlignedNote(&missing, indices[protected_position]);
      expectStructuralImmutableFailure(validateReplay(replay, missing), spec.rule_id);
    }

    ComposeResult missing_all = replay.score;
    for (auto iter = indices.rbegin(); iter != indices.rend(); ++iter)
      eraseAlignedNote(&missing_all, *iter);
    expectStructuralImmutableFailure(validateReplay(replay, missing_all), spec.rule_id);

    ComposeResult duplicate = replay.score;
    duplicate.notes.push_back(replay.score.notes[indices.front()]);
    duplicate.provenance.push_back(replay.score.provenance[indices.front()]);
    expectStructuralImmutableFailure(validateReplay(replay, duplicate), spec.rule_id);
  }
}

TEST(ValidatorImmutableReplayTest, GroundSubdivisionsAndCantusOffbeatEmbellishmentsRemainValid) {
  Material ground_material;
  ground_material.ground_bass = {{0, 2 * kTicksPerBar, 48}};
  ground_material.ground_bass_period = 2 * kTicksPerBar;
  const std::vector<NoteEvent> subdivided_ground = {makeNote(0, kTicksPerBar, 48, 1),
                                                    makeNote(kTicksPerBar, kTicksPerBar, 48, 1)};
  std::vector<NoteProvenance> ground_provenance(subdivided_ground.size(),
                                                makeProv(1, NoteSource::Material));
  for (NoteProvenance& provenance : ground_provenance)
    provenance.satisfied_rules |= ruleBitMask(RuleBit::GroundBassReplayed);
  EXPECT_FALSE(hasRule(
      Validator{}.validate(subdivided_ground, ground_provenance, cMajorWhole(), ground_material),
      "ground_bass_immutable"));

  Material cf_material;
  cf_material.cantus_firmus = {{0, kTicksPerBar, 72}};
  const std::vector<NoteEvent> embellished_cf = {
      makeNote(0, kTicksPerBeat, 72, 1),
      makeNote(kTicksPerBeat, kTicksPerBar - kTicksPerBeat, 74, 1)};
  std::vector<NoteProvenance> cf_provenance(embellished_cf.size(),
                                            makeProv(2, NoteSource::Material));
  for (NoteProvenance& provenance : cf_provenance)
    provenance.satisfied_rules |= ruleBitMask(RuleBit::CantusFirmusReplayed);
  EXPECT_FALSE(
      hasRule(Validator{}.validate(embellished_cf, cf_provenance, cMajorWhole(), cf_material),
              "cantus_firmus_immutable"));
}

TEST(ValidatorImmutableReplayTest, EmptyDeclarationsDoNotConstrainStampedNotes) {
  const std::vector<NoteEvent> notes = {makeNote(0, kTicksPerBeat, 48, 0),
                                        makeNote(kTicksPerBeat, kTicksPerBeat, 52, 1),
                                        makeNote(2 * kTicksPerBeat, kTicksPerBeat, 55, 2),
                                        makeNote(3 * kTicksPerBeat, kTicksPerBeat, 72, 3)};
  std::vector<NoteProvenance> provenance(notes.size(), makeProv(0, NoteSource::Material));
  provenance[0].satisfied_rules |= ruleBitMask(RuleBit::GroundBassReplayed);
  provenance[1].satisfied_rules |= ruleBitMask(RuleBit::PassacagliaGroundReplayed);
  provenance[2].satisfied_rules |= ruleBitMask(RuleBit::GoldbergBassReplayed);
  provenance[3].satisfied_rules |= ruleBitMask(RuleBit::CantusFirmusReplayed);

  const ValidationReport report =
      Validator{}.validate(notes, provenance, cMajorWhole(), Material{});
  EXPECT_FALSE(hasRule(report, "ground_bass_immutable"));
  EXPECT_FALSE(hasRule(report, "passacaglia_ground_immutable"));
  EXPECT_FALSE(hasRule(report, "goldberg_aria_bass_immutable"));
  EXPECT_FALSE(hasRule(report, "cantus_firmus_immutable"));
}

TEST(ValidatorImmutableReplayTest, OversizedPeriodicDeclarationFailsBeforeExpansion) {
  constexpr Tick kLargePeriod = static_cast<Tick>(0x80000000u);
  Material material;
  material.ground_bass = {{0, kLargePeriod, 48}};
  material.ground_bass_period = kLargePeriod;
  VariationDecl variation;
  variation.end_tick = std::numeric_limits<Tick>::max();
  material.variations.push_back(variation);

  const std::vector<NoteEvent> notes = {makeNote(0, kLargePeriod, 48, 1)};
  std::vector<NoteProvenance> provenance(1, makeProv(1, NoteSource::Material));
  provenance.front().satisfied_rules |= ruleBitMask(RuleBit::GroundBassReplayed);

  const ValidationReport report = Validator{}.validate(notes, provenance, cMajorWhole(), material);
  expectStructuralImmutableFailure(report, "ground_bass_immutable");
}

TEST(ValidatorImmutableReplayTest, UpperCodaStampCannotExcuseMissingGoldbergBass) {
  ImmutableReplayCase replay;
  ASSERT_TRUE(buildImmutableReplayCase(FormType::GoldbergVariations, 128,
                                       VoiceIntent::GoldbergBassCarrier,
                                       "goldberg_aria_bass_immutable", &replay));
  ASSERT_FALSE(replay.fixture.material.goldberg_aria_bass.empty());
  ASSERT_FALSE(replay.fixture.material.coda_extensions.empty());
  EXPECT_FALSE(hasRule(validateReplay(replay, replay.score), replay.rule_id));

  Tick score_end = 0;
  for (const NoteEvent& note : replay.score.notes)
    score_end = std::max(score_end, note.start_tick + note.duration);
  const Tick terminal_window = score_end - replay.fixture.material.goldberg_aria_bass_period;
  Tick declared_coda_start = std::numeric_limits<Tick>::max();
  for (const CodaDecl& coda : replay.fixture.material.coda_extensions) {
    if (coda.voice != 2)
      continue;
    for (const MaterialNote& note : coda.notes)
      declared_coda_start = std::min(declared_coda_start, note.start_tick);
  }
  ASSERT_LT(terminal_window, declared_coda_start);

  const std::vector<std::size_t> protected_indices = protectedNoteIndices(replay);
  const auto target =
      std::find_if(protected_indices.begin(), protected_indices.end(), [&](std::size_t index) {
        const Tick tick = replay.score.notes[index].start_tick;
        return tick > terminal_window && tick < declared_coda_start;
      });
  ASSERT_NE(target, protected_indices.end());
  ComposeResult fake_upper_coda = replay.score;
  fake_upper_coda.notes.push_back(makeNote(terminal_window, kTicksPerBeat, 84, 0));
  NoteProvenance upper_coda_provenance = makeProv(0, NoteSource::Material);
  upper_coda_provenance.voice_intent = VoiceIntent::CodaCarrier;
  upper_coda_provenance.satisfied_rules |= ruleBitMask(RuleBit::CodaCommitted);
  bindAuthoredNote(fake_upper_coda.notes.back(), &upper_coda_provenance);
  fake_upper_coda.provenance.push_back(upper_coda_provenance);
  eraseAlignedNote(&fake_upper_coda, *target);

  expectStructuralImmutableFailure(validateReplay(replay, fake_upper_coda), replay.rule_id.c_str());
}

TEST(ValidatorImmutableReplayTest, DeclaredTerminalCodaMustMatchTheProtectedVoice) {
  ImmutableReplayCase replay;
  ASSERT_TRUE(buildImmutableReplayCase(FormType::GoldbergVariations, 128,
                                       VoiceIntent::GoldbergBassCarrier,
                                       "goldberg_aria_bass_immutable", &replay));
  ASSERT_FALSE(replay.fixture.material.coda_extensions.empty());
  EXPECT_FALSE(hasRule(validateReplay(replay, replay.score), replay.rule_id));

  ComposeResult mismatched_coda = replay.score;
  bool changed_protected_coda = false;
  for (std::size_t index = 0; index < mismatched_coda.notes.size(); ++index) {
    NoteProvenance& provenance = mismatched_coda.provenance[index];
    if (mismatched_coda.notes[index].voice == 2 &&
        hasProvenanceBit(provenance, RuleBit::CodaCommitted)) {
      ++mismatched_coda.notes[index].pitch;
      bindAuthoredNote(mismatched_coda.notes[index], &provenance);
      changed_protected_coda = true;
      break;
    }
  }
  ASSERT_TRUE(changed_protected_coda);
  expectStructuralImmutableFailure(validateReplay(replay, mismatched_coda), replay.rule_id.c_str());
}

TEST(ValidatorImmutableReplayTest, ZeroDurationPeriodicDeclarationFailsBeforeExpansion) {
  Material material;
  material.ground_bass = {{1, 0, 48}};
  material.ground_bass_period = 1;
  VariationDecl variation;
  variation.end_tick = std::numeric_limits<Tick>::max();
  material.variations.push_back(variation);

  const ValidationReport report = Validator{}.validate({}, {}, cMajorWhole(), material);
  expectStructuralImmutableFailure(report, "ground_bass_immutable");
}

TEST(ValidatorImmutableReplayTest, UnsplitCanonicalGroundAndCantusCanHoldAcrossBarHeads) {
  Material ground_material;
  ground_material.ground_bass = {{0, 2 * kTicksPerBar, 48}};
  ground_material.ground_bass_period = 2 * kTicksPerBar;
  const std::vector<NoteEvent> held_ground = {makeNote(0, 2 * kTicksPerBar, 48, 1)};
  std::vector<NoteProvenance> ground_provenance(1, makeProv(1, NoteSource::Material));
  ground_provenance.front().satisfied_rules |= ruleBitMask(RuleBit::GroundBassReplayed);
  EXPECT_FALSE(
      hasRule(Validator{}.validate(held_ground, ground_provenance, cMajorWhole(), ground_material),
              "ground_bass_immutable"));

  Material cf_material;
  cf_material.cantus_firmus = {{0, kTicksPerBar, 72}, {kTicksPerBar, kTicksPerBar, 72}};
  const std::vector<NoteEvent> held_cantus = {makeNote(0, 2 * kTicksPerBar, 72, 1)};
  std::vector<NoteProvenance> cf_provenance(1, makeProv(2, NoteSource::Material));
  cf_provenance.front().satisfied_rules |= ruleBitMask(RuleBit::CantusFirmusReplayed);
  EXPECT_FALSE(hasRule(Validator{}.validate(held_cantus, cf_provenance, cMajorWhole(), cf_material),
                       "cantus_firmus_immutable"));
}

TEST(ValidatorImmutableReplayTest, OffbeatDifferentPitchHeldAcrossBarHeadFailsForGroundAndCantus) {
  Material ground_material;
  ground_material.ground_bass = {{0, kTicksPerBar, 48}, {kTicksPerBar, kTicksPerBar, 48}};
  ground_material.ground_bass_period = 2 * kTicksPerBar;
  const std::vector<NoteEvent> wrong_ground = {
      makeNote(0, kTicksPerBar - 1, 48, 1),
      makeNote(kTicksPerBar - 1, kTicksPerBeat, 50, 1),
  };
  std::vector<NoteProvenance> ground_provenance(wrong_ground.size(),
                                                makeProv(1, NoteSource::Material));
  for (NoteProvenance& provenance : ground_provenance)
    provenance.satisfied_rules |= ruleBitMask(RuleBit::GroundBassReplayed);
  expectStructuralImmutableFailure(
      Validator{}.validate(wrong_ground, ground_provenance, cMajorWhole(), ground_material),
      "ground_bass_immutable");

  Material cf_material;
  cf_material.cantus_firmus = {{0, kTicksPerBar, 72}, {kTicksPerBar, kTicksPerBar, 72}};
  const std::vector<NoteEvent> wrong_cantus = {
      makeNote(0, kTicksPerBar - 1, 72, 1),
      makeNote(kTicksPerBar - 1, kTicksPerBeat, 74, 1),
  };
  std::vector<NoteProvenance> cf_provenance(wrong_cantus.size(), makeProv(2, NoteSource::Material));
  for (NoteProvenance& provenance : cf_provenance)
    provenance.satisfied_rules |= ruleBitMask(RuleBit::CantusFirmusReplayed);
  expectStructuralImmutableFailure(
      Validator{}.validate(wrong_cantus, cf_provenance, cMajorWhole(), cf_material),
      "cantus_firmus_immutable");
}

namespace {

struct SyntheticImitationReplay {
  Material material;
  std::vector<NoteEvent> notes;
  std::vector<NoteProvenance> provenance;
};

SyntheticImitationReplay makeSyntheticImitationReplay() {
  SyntheticImitationReplay replay;
  replay.material.subject = {{0, kTicksPerBeat, 72}, {kTicksPerBeat, kTicksPerBeat, 74}};
  replay.material.answer = {{4 * kTicksPerBeat, kTicksPerBeat, 67},
                            {5 * kTicksPerBeat, kTicksPerBeat, 69}};
  ImitationEntry entry;
  entry.note_count = 2;
  entry.distance_ticks = 4 * kTicksPerBeat;
  entry.interval_semis = -5;
  replay.material.imitation_entries.push_back(entry);

  const auto append_line = [&replay](const std::vector<MaterialNote>& material_line, VoiceId voice,
                                     VoiceIntent intent, SpanId span_id) {
    for (std::size_t index = 0; index < material_line.size(); ++index) {
      const MaterialNote& material_note = material_line[index];
      NoteEvent note =
          makeNote(material_note.start_tick, material_note.duration, material_note.pitch, voice);
      NoteProvenance provenance = makeProvIntent(span_id, NoteSource::Material, intent);
      bindAuthoredNote(note, &provenance);
      if (index == 0)
        provenance.satisfied_rules |= ruleBitMask(RuleBit::ImitationEntryMatched);
      replay.notes.push_back(note);
      replay.provenance.push_back(provenance);
    }
  };
  append_line(replay.material.subject, 0, VoiceIntent::SubjectCarrier, 1);
  append_line(replay.material.answer, 1, VoiceIntent::AnswerCarrier, 2);
  return replay;
}

ValidationReport validateSyntheticImitation(const SyntheticImitationReplay& replay,
                                            ValidationScope scope) {
  return Validator{}.validate(replay.notes, replay.provenance, cMajorWhole(), replay.material,
                              scope);
}

const std::vector<MaterialNote>& imitationFragmentForTest(const Material& material,
                                                          MaterialFragment fragment) {
  switch (fragment) {
    case MaterialFragment::Subject:
      return material.subject;
    case MaterialFragment::Answer:
      return material.answer;
    case MaterialFragment::TonalAnswer:
      return material.tonal_answer;
    case MaterialFragment::Countersubject:
      return material.countersubject;
  }
  return material.subject;
}

VoiceIntent imitationIntentForTest(MaterialFragment fragment) {
  switch (fragment) {
    case MaterialFragment::Subject:
      return VoiceIntent::SubjectCarrier;
    case MaterialFragment::Answer:
    case MaterialFragment::TonalAnswer:
      return VoiceIntent::AnswerCarrier;
    case MaterialFragment::Countersubject:
      return VoiceIntent::CountersubjectCarrier;
  }
  return VoiceIntent::FillerGap;
}

std::vector<std::size_t> exactImitationEvents(const ComposeResult& score, const Material& material,
                                              const ImitationEntry& entry, bool leader) {
  const MaterialFragment fragment = leader ? entry.leader_fragment : entry.follower_fragment;
  const std::vector<MaterialNote>& line = imitationFragmentForTest(material, fragment);
  const std::size_t start_index = leader ? entry.leader_start_index : entry.follower_start_index;
  const std::size_t available = start_index < line.size() ? line.size() - start_index : 0;
  const std::size_t count = entry.note_count == 0 ? available : entry.note_count;
  const VoiceId voice = leader ? entry.leader_voice : entry.follower_voice;
  const VoiceIntent intent = imitationIntentForTest(fragment);
  std::vector<std::size_t> matched;
  for (std::size_t offset = 0; offset < count && start_index + offset < line.size(); ++offset) {
    const MaterialNote& expected = line[start_index + offset];
    for (std::size_t index = 0; index < score.notes.size() && index < score.provenance.size();
         ++index) {
      const NoteEvent& note = score.notes[index];
      const NoteProvenance& provenance = score.provenance[index];
      if (note.voice == voice && note.start_tick == expected.start_tick &&
          note.duration == expected.duration && note.pitch == expected.pitch &&
          provenance.voice_intent == intent && provenance.source == NoteSource::Material) {
        matched.push_back(index);
        break;
      }
    }
  }
  return matched;
}

struct PublicImitationReplay {
  HarnessFixture fixture;
  ComposeResult score;
};

bool buildPublicImitationReplay(PublicImitationReplay* replay) {
  ComposeRequest request;
  request.form = FormType::Fugue;
  request.seed = 42;
  request.target_bars = 128;
  if (buildFormFixture(request, &replay->fixture) != FormDirectorStatus::Ok ||
      replay->fixture.material.imitation_entries.empty()) {
    return false;
  }
  replay->score =
      Composer{}.run(replay->fixture.material, replay->fixture.harmony, replay->fixture.voice_plan);
  return true;
}

void appendAuthoredTestNote(std::vector<NoteEvent>* notes, std::vector<NoteProvenance>* provenance,
                            Tick start, Tick duration, std::uint8_t pitch, VoiceId voice,
                            VoiceIntent intent, SpanId span_id,
                            NoteSource source = NoteSource::Material) {
  NoteEvent note = makeNote(start, duration, pitch, voice);
  NoteProvenance prov = makeProvIntent(span_id, source, intent);
  bindAuthoredNote(note, &prov);
  notes->push_back(note);
  provenance->push_back(prov);
}

}  // namespace

TEST(ValidatorImitationIntegrityTest, PublicFugueReplayFailsClosedForMissingAndDuplicateNotes) {
  PublicImitationReplay replay;
  ASSERT_TRUE(buildPublicImitationReplay(&replay));
  const ImitationEntry& entry = replay.fixture.material.imitation_entries.front();
  const std::vector<std::size_t> follower_indices =
      exactImitationEvents(replay.score, replay.fixture.material, entry, /*leader=*/false);
  ASSERT_GE(follower_indices.size(), 3u);

  for (ValidationScope scope : {ValidationScope::Generation, ValidationScope::FinalScore})
    EXPECT_FALSE(
        hasRule(Validator{}.validate(replay.score.notes, replay.score.provenance,
                                     replay.fixture.harmony, replay.fixture.material, scope),
                "imitation_entry_realization"));

  for (const std::size_t position :
       {std::size_t{0}, follower_indices.size() / 2, follower_indices.size() - 1}) {
    ComposeResult missing = replay.score;
    eraseAlignedNote(&missing, follower_indices[position]);
    for (ValidationScope scope : {ValidationScope::Generation, ValidationScope::FinalScore}) {
      const ValidationReport report =
          Validator{}.validate(missing.notes, missing.provenance, replay.fixture.harmony,
                               replay.fixture.material, scope);
      expectStructuralImmutableFailure(report, "imitation_entry_realization");
    }
  }

  ComposeResult missing_all = replay.score;
  for (auto iter = follower_indices.rbegin(); iter != follower_indices.rend(); ++iter)
    eraseAlignedNote(&missing_all, *iter);
  for (ValidationScope scope : {ValidationScope::Generation, ValidationScope::FinalScore}) {
    const ValidationReport report =
        Validator{}.validate(missing_all.notes, missing_all.provenance, replay.fixture.harmony,
                             replay.fixture.material, scope);
    expectStructuralImmutableFailure(report, "imitation_entry_realization");
  }

  ComposeResult duplicate = replay.score;
  duplicate.notes.push_back(replay.score.notes[follower_indices.front()]);
  duplicate.provenance.push_back(replay.score.provenance[follower_indices.front()]);
  expectStructuralImmutableFailure(
      Validator{}.validate(duplicate.notes, duplicate.provenance, replay.fixture.harmony,
                           replay.fixture.material, ValidationScope::FinalScore),
      "imitation_entry_realization");
}

TEST(ValidatorImitationIntegrityTest,
     FinalScoreRequiresActualReplayAndAcceptsCompleteOrnamentGroup) {
  SyntheticImitationReplay replay = makeSyntheticImitationReplay();
  EXPECT_FALSE(hasRule(validateSyntheticImitation(replay, ValidationScope::FinalScore),
                       "imitation_entry_realization"));

  SyntheticImitationReplay empty_actual = replay;
  empty_actual.notes.clear();
  empty_actual.provenance.clear();
  expectStructuralImmutableFailure(
      validateSyntheticImitation(empty_actual, ValidationScope::FinalScore),
      "imitation_entry_realization");
  EXPECT_FALSE(hasRule(validateSyntheticImitation(empty_actual, ValidationScope::Generation),
                       "imitation_entry_realization"));

  NoteProvenance authored = replay.provenance.front();
  authored.source = NoteSource::Ornament;
  authored.satisfied_rules |= ruleBitMask(RuleBit::OrnamentRealized);
  NoteEvent upper = makeNote(0, kTicksPerBeat / 2, 74, 0);
  NoteEvent main = makeNote(kTicksPerBeat / 2, kTicksPerBeat / 2, 72, 0);
  NoteProvenance upper_provenance = authored;
  NoteProvenance main_provenance = authored;
  main_provenance.satisfied_rules = ruleBitMask(RuleBit::OrnamentRealized);
  replay.notes.erase(replay.notes.begin());
  replay.provenance.erase(replay.provenance.begin());
  replay.notes.insert(replay.notes.begin(), {upper, main});
  replay.provenance.insert(replay.provenance.begin(), {upper_provenance, main_provenance});
  EXPECT_FALSE(hasRule(validateSyntheticImitation(replay, ValidationScope::FinalScore),
                       "imitation_entry_realization"));

  replay.provenance[2].span_id = 9;
  expectStructuralImmutableFailure(validateSyntheticImitation(replay, ValidationScope::FinalScore),
                                   "imitation_entry_realization");
}

TEST(ValidatorImitationIntegrityTest, InvalidExplicitBoundsFailButEmptyEndWindowIsInert) {
  SyntheticImitationReplay replay = makeSyntheticImitationReplay();
  replay.material.imitation_entries.front().leader_start_index = replay.material.subject.size();
  expectStructuralImmutableFailure(validateSyntheticImitation(replay, ValidationScope::Generation),
                                   "imitation_entry_match");

  replay = makeSyntheticImitationReplay();
  replay.material.imitation_entries.front().note_count = 3;
  expectStructuralImmutableFailure(validateSyntheticImitation(replay, ValidationScope::Generation),
                                   "imitation_entry_match");

  replay = makeSyntheticImitationReplay();
  ImitationEntry& entry = replay.material.imitation_entries.front();
  entry.leader_start_index = replay.material.subject.size();
  entry.follower_start_index = replay.material.answer.size();
  entry.note_count = 0;
  replay.notes.clear();
  replay.provenance.clear();
  EXPECT_FALSE(hasRule(validateSyntheticImitation(replay, ValidationScope::Generation),
                       "imitation_entry_match"));
  EXPECT_FALSE(hasRule(validateSyntheticImitation(replay, ValidationScope::Generation),
                       "imitation_entry_realization"));
}

TEST(ValidatorCountersubjectIntegrityTest, MissingCoverageFailsOnlyWhenAnswerCarrierIsEmitted) {
  Material material;
  for (int index = 0; index < 4; ++index)
    material.answer.push_back({index * kTicksPerBeat, kTicksPerBeat, 60});
  material.countersubject = {{0, 4 * kTicksPerBeat, 71}};

  std::vector<NoteEvent> notes;
  std::vector<NoteProvenance> provenance;
  appendAuthoredTestNote(&notes, &provenance, 0, kTicksPerBeat, 69, 0,
                         VoiceIntent::CountersubjectCarrier, 1);
  for (ValidationScope scope : {ValidationScope::Generation, ValidationScope::FinalScore})
    EXPECT_FALSE(hasRule(Validator{}.validate(notes, provenance, cMajorWhole(), material, scope),
                         "countersubject_continuous"));

  for (int index = 0; index < 4; ++index)
    appendAuthoredTestNote(&notes, &provenance, index * kTicksPerBeat, kTicksPerBeat, 60, 1,
                           VoiceIntent::AnswerCarrier, 2);
  for (ValidationScope scope : {ValidationScope::Generation, ValidationScope::FinalScore})
    EXPECT_TRUE(hasRule(Validator{}.validate(notes, provenance, cMajorWhole(), material, scope),
                        "countersubject_continuous"));
}

TEST(ValidatorCountersubjectIntegrityTest, CountersubjectDeclaredAfterAnswerWindowIsDeferred) {
  Material material;
  material.answer = {{0, 4 * kTicksPerBeat, 60}};
  material.countersubject = {{4 * kTicksPerBeat, 4 * kTicksPerBeat, 71}};

  std::vector<NoteEvent> notes;
  std::vector<NoteProvenance> provenance;
  appendAuthoredTestNote(&notes, &provenance, 0, 4 * kTicksPerBeat, 60, 1,
                         VoiceIntent::AnswerCarrier, 2);
  appendAuthoredTestNote(&notes, &provenance, 4 * kTicksPerBeat, 4 * kTicksPerBeat, 71, 0,
                         VoiceIntent::CountersubjectCarrier, 3);

  for (ValidationScope scope : {ValidationScope::Generation, ValidationScope::FinalScore})
    EXPECT_FALSE(hasRule(Validator{}.validate(notes, provenance, cMajorWhole(), material, scope),
                         "countersubject_continuous"));
}

TEST(ValidatorCountersubjectIntegrityTest,
     CoverageUsesEarliestCountersubjectVoiceAndValidOrnament) {
  Material material;
  for (int index = 0; index < 4; ++index)
    material.answer.push_back({index * kTicksPerBeat, kTicksPerBeat, 60});
  material.countersubject = {{0, 4 * kTicksPerBeat, 71}};

  std::vector<NoteEvent> notes;
  std::vector<NoteProvenance> provenance;
  for (int index = 0; index < 4; ++index)
    appendAuthoredTestNote(&notes, &provenance, index * kTicksPerBeat, kTicksPerBeat, 60, 1,
                           VoiceIntent::AnswerCarrier, 2);
  // Deliberately insert the later voice first; the earlier overlapping line in V2 wins.
  appendAuthoredTestNote(&notes, &provenance, kTicksPerBeat, kTicksPerBeat, 70, 0,
                         VoiceIntent::CountersubjectCarrier, 3);

  NoteEvent ornament_first = makeNote(0, 2 * kTicksPerBeat, 73, 2);
  NoteEvent ornament_last = makeNote(2 * kTicksPerBeat, 2 * kTicksPerBeat, 69, 2);
  NoteProvenance ornament_provenance =
      makeProvIntent(4, NoteSource::Ornament, VoiceIntent::CountersubjectCarrier);
  ornament_provenance.has_authored_note = true;
  ornament_provenance.authored_start_tick = 0;
  ornament_provenance.authored_duration = 4 * kTicksPerBeat;
  ornament_provenance.authored_pitch = 69;
  ornament_provenance.satisfied_rules |= ruleBitMask(RuleBit::OrnamentRealized);
  notes.push_back(ornament_first);
  provenance.push_back(ornament_provenance);
  notes.push_back(ornament_last);
  provenance.push_back(ornament_provenance);

  for (ValidationScope scope : {ValidationScope::Generation, ValidationScope::FinalScore})
    EXPECT_FALSE(hasRule(Validator{}.validate(notes, provenance, cMajorWhole(), material, scope),
                         "countersubject_continuous"));
}

TEST(ValidatorCountersubjectIntegrityTest,
     CoverageSelectsEarliestRawOnsetBeforeClippingToAnswerWindow) {
  Material material;
  material.answer = {{kTicksPerBeat, 4 * kTicksPerBeat, 60}};
  material.countersubject = {{kTicksPerBeat, 4 * kTicksPerBeat, 71}};

  std::vector<NoteEvent> notes;
  std::vector<NoteProvenance> provenance;
  appendAuthoredTestNote(&notes, &provenance, kTicksPerBeat / 2, 9 * kTicksPerBeat / 2, 69, 0,
                         VoiceIntent::CountersubjectCarrier, 3);
  appendAuthoredTestNote(&notes, &provenance, 0, 2 * kTicksPerBeat, 70, 2,
                         VoiceIntent::CountersubjectCarrier, 4);
  appendAuthoredTestNote(&notes, &provenance, kTicksPerBeat, 4 * kTicksPerBeat, 60, 1,
                         VoiceIntent::AnswerCarrier, 5);

  for (ValidationScope scope : {ValidationScope::Generation, ValidationScope::FinalScore})
    EXPECT_TRUE(hasRule(Validator{}.validate(notes, provenance, cMajorWhole(), material, scope),
                        "countersubject_continuous"));
}

TEST(ValidatorCountersubjectIntegrityTest,
     InvalidOrnamentAuthorshipCannotFillCountersubjectCoverage) {
  Material material;
  material.answer = {{0, 4 * kTicksPerBeat, 60}};
  material.countersubject = {{0, 4 * kTicksPerBeat, 69}};

  std::vector<NoteEvent> notes;
  std::vector<NoteProvenance> provenance;
  appendAuthoredTestNote(&notes, &provenance, 0, 4 * kTicksPerBeat, 60, 1,
                         VoiceIntent::AnswerCarrier, 2);

  NoteProvenance invalid_ornament =
      makeProvIntent(3, NoteSource::Ornament, VoiceIntent::CountersubjectCarrier);
  invalid_ornament.has_authored_note = true;
  invalid_ornament.authored_start_tick = 1;
  invalid_ornament.authored_duration = 4 * kTicksPerBeat;
  invalid_ornament.authored_pitch = 69;
  invalid_ornament.satisfied_rules |= ruleBitMask(RuleBit::OrnamentRealized);
  notes.push_back(makeNote(0, 2 * kTicksPerBeat, 70, 0));
  provenance.push_back(invalid_ornament);
  notes.push_back(makeNote(2 * kTicksPerBeat, 2 * kTicksPerBeat, 69, 0));
  provenance.push_back(invalid_ornament);

  for (ValidationScope scope : {ValidationScope::Generation, ValidationScope::FinalScore})
    EXPECT_TRUE(hasRule(Validator{}.validate(notes, provenance, cMajorWhole(), material, scope),
                        "countersubject_continuous"));
}

TEST(ValidatorCountersubjectIntegrityTest, ComposeCountersubjectIntentCanCoverSelectedVoice) {
  Material material;
  material.answer = {{0, 4 * kTicksPerBeat, 60}};
  material.countersubject = {{0, 4 * kTicksPerBeat, 69}};

  std::vector<NoteEvent> notes;
  std::vector<NoteProvenance> provenance;
  appendAuthoredTestNote(&notes, &provenance, 0, 4 * kTicksPerBeat, 60, 1,
                         VoiceIntent::AnswerCarrier, 2);
  appendAuthoredTestNote(&notes, &provenance, 0, 4 * kTicksPerBeat, 69, 0,
                         VoiceIntent::CountersubjectCarrier, 3, NoteSource::Compose);

  for (ValidationScope scope : {ValidationScope::Generation, ValidationScope::FinalScore})
    EXPECT_FALSE(hasRule(Validator{}.validate(notes, provenance, cMajorWhole(), material, scope),
                         "countersubject_continuous"));
}

TEST(ValidatorCountersubjectIntegrityTest, OtherIntentAndOtherCountersubjectVoiceCannotFillGaps) {
  Material material;
  for (int index = 0; index < 4; ++index)
    material.answer.push_back({index * kTicksPerBeat, kTicksPerBeat, 60});
  material.countersubject = {{0, 4 * kTicksPerBeat, 71}};

  std::vector<NoteEvent> notes;
  std::vector<NoteProvenance> provenance;
  for (int index = 0; index < 4; ++index)
    appendAuthoredTestNote(&notes, &provenance, index * kTicksPerBeat, kTicksPerBeat, 60, 1,
                           VoiceIntent::AnswerCarrier, 2);
  appendAuthoredTestNote(&notes, &provenance, 0, 2 * kTicksPerBeat, 71, 0,
                         VoiceIntent::CountersubjectCarrier, 3);
  appendAuthoredTestNote(&notes, &provenance, 2 * kTicksPerBeat, 2 * kTicksPerBeat, 72, 2,
                         VoiceIntent::CountersubjectCarrier, 4);
  appendAuthoredTestNote(&notes, &provenance, 2 * kTicksPerBeat, 2 * kTicksPerBeat, 60, 0,
                         VoiceIntent::HarmonicSupport, 5);

  for (ValidationScope scope : {ValidationScope::Generation, ValidationScope::FinalScore})
    EXPECT_TRUE(hasRule(Validator{}.validate(notes, provenance, cMajorWhole(), material, scope),
                        "countersubject_continuous"));
}

TEST(ValidatorCountersubjectIntegrityTest, PublicFugueFailsClosedWhenCountersubjectIsMissing) {
  PublicImitationReplay replay;
  ASSERT_TRUE(buildPublicImitationReplay(&replay));
  EXPECT_FALSE(hasRule(
      Validator{}.validate(replay.score.notes, replay.score.provenance, replay.fixture.harmony,
                           replay.fixture.material, ValidationScope::Generation),
      "countersubject_continuous"));

  ComposeResult missing = replay.score;
  for (std::size_t index = missing.provenance.size(); index > 0; --index) {
    if (missing.provenance[index - 1].voice_intent == VoiceIntent::CountersubjectCarrier)
      eraseAlignedNote(&missing, index - 1);
  }
  for (ValidationScope scope : {ValidationScope::Generation, ValidationScope::FinalScore})
    EXPECT_TRUE(
        hasRule(Validator{}.validate(missing.notes, missing.provenance, replay.fixture.harmony,
                                     replay.fixture.material, scope),
                "countersubject_continuous"));
}

}  // namespace bach::composer
