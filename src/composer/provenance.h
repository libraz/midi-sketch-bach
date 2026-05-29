#ifndef BACH_COMPOSER_PROVENANCE_H
#define BACH_COMPOSER_PROVENANCE_H

#include <cstdint>

#include "composer/span.h"
#include "composer/voice_intent.h"

namespace bach::composer {

// Where a note's pitch and duration came from.
//
// Material  = input motif (subject, answer, fixed counterline). Pitches
//             and durations are inputs, not search outputs. CandidateSearch
//             does not enumerate alternatives for Material notes.
// Compose   = chosen by CandidateSearch from the candidate set. Every
//             Compose note is reproducible from (span_id, candidate_score,
//             satisfied_rules).
//
// No other values are permitted. In particular there is no "Repair" or
// "Snap" source; the composer pipeline does not run post-generation
// pitch-modifying passes (see rebuild plan §禁止事項).
enum class NoteSource : std::uint8_t {
  Material = 0,
  Compose = 1,
};

// Bitset over rule IDs. Each bit position is a CandidateSearch rule that
// the chosen candidate satisfies. Provides MUS-ROVER-style interpretable
// rule traces: a downstream analyzer can read this back to explain why
// the candidate was admitted.
//
// 64 bits is a soft cap on rule count for Phase 3-6. If the rule set
// outgrows that, widen to std::array<uint64_t, N> before changing the
// JSON schema.
using RuleIdMask = std::uint64_t;

enum RuleBit : std::uint8_t {
  ChordTone = 0,
  StrongBeatConsonance = 1,
  SmallStep = 2,
  ParallelPerfectChecked = 3,
  VoiceCrossingChecked = 4,
  LeapResolutionChecked = 5,
  WeakBeatPassingChecked = 6,
  VerticalConsonanceChecked = 7,
  LeadingToneResolved = 8,
  HiddenParallelChecked = 9,
  CrossRelationChecked = 10,
  CadenceCellCommitted = 11,
  CadenceVoiceLeadingChecked = 12,
  // P4 (Suspension + non-chord-tone recognition).
  SuspensionPrepared = 13,
  SuspensionResolved = 14,
  CambiataDetected = 15,
  EchappeeDetected = 16,
  AnticipationDetected = 17,
  NotaCambiataDetected = 18,
  // P5 (Episode = motif transform derived).
  EpisodeMotifSourced = 19,
  // P6 (Tonal answer + Countersubject).
  TonalAnswerMapped = 20,
  CountersubjectActive = 21,
  // P7 (Functional harmony: degree/inversion/doubling/spacing).
  // ChordToneRoman: the candidate's pitch class belongs to the
  //   triad/seventh chord declared by ChordEvent.degree+quality.
  // InversionLabel: the active ChordEvent has has_degree=true and
  //   the bass voice's pitch class matches ChordEvent.inversion's
  //   chord factor (root/third/fifth/seventh).
  // DoublingChecked: the candidate participated in a doubling
  //   verification at its tick (no leading-tone double, no 7th
  //   double).
  // SpacingChecked: the candidate participated in spacing
  //   verification (upper-voice intervals stayed within one octave).
  ChordToneRoman = 22,
  InversionLabel = 23,
  DoublingChecked = 24,
  SpacingChecked = 25,
  // P8 (Modulation / Tonicization / Borrowed chords).
  // ModulationCommitted: the candidate sits at-or-after a
  //   HarmonicPlan::ModulationEvent tick and its pitch class is
  //   diatonic in the new key area.
  // SecondaryDominantResolved: the candidate's chord follows a
  //   has_secondary_of=true chord and its degree matches the
  //   secondary_of value (i.e. the secondary dominant resolved as
  //   declared).
  // PicardyThird: the candidate is the major third of a final tonic
  //   chord in a minor-mode piece (is_picardy=true on the chord).
  // ModalMixture: the candidate's chord carries is_borrowed=true
  //   (loan from the parallel mode).
  ModulationCommitted = 26,
  SecondaryDominantResolved = 27,
  PicardyThird = 28,
  ModalMixture = 29,
  // P9 (Fortspinnung + Sequence + Imitation).
  // FortspinnungSourced: the candidate's pitch and tick came from a
  //   SequenceTemplate replay (not free-search and not Episode replay).
  //   Set by CandidateSearch on every note of a FortspinnungSpan.
  // SequenceStep: the candidate sits at-or-after a SequenceTemplate
  //   step boundary AND its pitch matches the seed motif transposed by
  //   the pattern's step semitone offset.
  // ImitationEntryMatched: the candidate participates in a declared
  //   ImitationEntry (leader voice's first note tick, or follower
  //   voice's first note tick at leader.tick + distance with the
  //   declared interval offset).
  FortspinnungSourced = 30,
  SequenceStep = 31,
  ImitationEntryMatched = 32,
  // P10 (Invertible counterpoint at the octave).
  // InvertibleAt8va: the Compose candidate, against the sounding
  //   upper-adjacent voice at this tick, does NOT form a perfect 4th
  //   on a strong beat (a 4th inverts to a 5th under octave inversion),
  //   i.e. the candidate passed the invertibility check. Only set when
  //   placed_notes is available and the pair is an upper-adjacent pair.
  InvertibleAt8va = 33,
};

// Per-note provenance record.
//
// Invariant: every NoteEvent emitted by Renderer carries one
// NoteProvenance. provenance.json is built from this collection.
// generated.json (the evaluation export) excludes these fields.
struct NoteProvenance {
  SpanId span_id = kInvalidSpanId;
  VoiceIntent voice_intent = VoiceIntent::FillerGap;
  float candidate_score = 0.0f;
  NoteSource source = NoteSource::Compose;
  RuleIdMask satisfied_rules = 0;
  std::uint16_t rejected_alternatives = 0;
};

}  // namespace bach::composer

#endif  // BACH_COMPOSER_PROVENANCE_H
