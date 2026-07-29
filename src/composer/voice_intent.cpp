#include "composer/voice_intent.h"

#include <cstddef>

#include "composer/provenance.h"

namespace bach::composer {

namespace {

// Per-intent descriptor table, indexed by the VoiceIntent enumerator value.
// This is the single source of truth that voiceIntentToString,
// isCarrierIntent, and (later) the CandidateSearch replay dispatch all read,
// replacing the previously dual-encoded scattered switch / `||` chains.
//
// Order MUST match the VoiceIntent enumerator order
// (0..GoldbergInnerVoiceCarrier=32); the static_assert below guards the size so a
// future intent cannot silently miss an entry.
//
// provenance_bit is only meaningful when has_provenance_bit is true. A
// placeholder of ChordTone is used otherwise (never read).
constexpr IntentDescriptor kIntentTable[] = {
    // SubjectCarrier (0): verbatim subject; stamps ChordTone/markers per note,
    // no single identity bit.
    {"SubjectCarrier", true, ReplayKind::kVerbatimVector, ChordTone, false},
    // RepeatedReplyCell (1): free counterpoint.
    {"RepeatedReplyCell", false, ReplayKind::kCompose, ChordTone, false},
    // SequentialCounterline (2): free counterpoint.
    {"SequentialCounterline", false, ReplayKind::kCompose, ChordTone, false},
    // HarmonicSupport (3): free counterpoint.
    {"HarmonicSupport", false, ReplayKind::kCompose, ChordTone, false},
    // CadentialClosure (4): free counterpoint.
    {"CadentialClosure", false, ReplayKind::kCompose, ChordTone, false},
    // FillerGap (5): free counterpoint.
    {"FillerGap", false, ReplayKind::kCompose, ChordTone, false},
    // AnswerCarrier (6): verbatim answer / tonal answer; TonalAnswerMapped is
    // conditional on the tonal-answer source, so no single primary bit.
    {"AnswerCarrier", true, ReplayKind::kVerbatimVector, ChordTone, false},
    // SuspensionCarrier (7): three-note prep/sus/res; bits live on different
    // notes, no single primary bit.
    {"SuspensionCarrier", true, ReplayKind::kTriple, ChordTone, false},
    // Episode (8): motif-transform derived; every note carries
    // EpisodeMotifSourced.
    {"Episode", true, ReplayKind::kTransform, EpisodeMotifSourced, true},
    // CountersubjectCarrier (9): every note carries CountersubjectActive.
    {"CountersubjectCarrier", true, ReplayKind::kVerbatimVector, CountersubjectActive, true},
    // FortspinnungSpan (10): every note carries FortspinnungSourced.
    {"FortspinnungSpan", true, ReplayKind::kSequence, FortspinnungSourced, true},
    // MiddleEntryCarrier (11): MiddleEntryCommitted.
    {"MiddleEntryCarrier", true, ReplayKind::kVerbatimVector, MiddleEntryCommitted, true},
    // StrettoCarrier (12): StrettoCommitted.
    {"StrettoCarrier", true, ReplayKind::kVerbatimVector, StrettoCommitted, true},
    // PedalCarrier (13): PedalCommitted.
    {"PedalCarrier", true, ReplayKind::kVerbatimVector, PedalCommitted, true},
    // CodaCarrier (14): CodaCommitted.
    {"CodaCarrier", true, ReplayKind::kVerbatimVector, CodaCommitted, true},
    // SubjectCarrierAugmented (15): SubjectVariantApplied.
    {"SubjectCarrierAugmented", true, ReplayKind::kVerbatimVector, SubjectVariantApplied, true},
    // SubjectCarrierDiminished (16): SubjectVariantApplied.
    {"SubjectCarrierDiminished", true, ReplayKind::kVerbatimVector, SubjectVariantApplied, true},
    // SubjectCarrierInverted (17): SubjectVariantApplied.
    {"SubjectCarrierInverted", true, ReplayKind::kVerbatimVector, SubjectVariantApplied, true},
    // RhythmCarrier (18): feature-dependent bit chosen per fragment, no single
    // primary bit.
    {"RhythmCarrier", true, ReplayKind::kVerbatimVector, ChordTone, false},
    // NctCarrier (19): figure bits stamped later by the NCT post-pass, none
    // here.
    {"NctCarrier", true, ReplayKind::kVerbatimVector, ChordTone, false},
    // ArpeggioFlow (20): every note carries ArpeggioFlowActive (and
    // ImplicitVoiceTracked, stamped alongside in the replay branch).
    {"ArpeggioFlow", true, ReplayKind::kVerbatimVector, ArpeggioFlowActive, true},
    // GroundCarrier (21): every note carries GroundBassReplayed.
    {"GroundCarrier", true, ReplayKind::kVerbatimVector, GroundBassReplayed, true},
    // VariationCarrier (22): every note carries VariationRoleApplied (the
    // TextureDensityShift bit is stamped on a variation's first note in the
    // replay branch, not encoded here).
    {"VariationCarrier", true, ReplayKind::kVerbatimVector, VariationRoleApplied, true},
    // FigurationCarrier (23): every note carries FigurationCommitted (the
    // CadenzaApplied / PedalPreparation bits are OR-ed per-section in the
    // replay branch, not encoded here).
    {"FigurationCarrier", true, ReplayKind::kVerbatimVector, FigurationCommitted, true},
    // ToccataCarrier (24): every note carries ToccataArchetypeApplied (the
    // SectionTransition bit is OR-ed onto a section head's first note in the
    // replay branch, not encoded here).
    {"ToccataCarrier", true, ReplayKind::kVerbatimVector, ToccataArchetypeApplied, true},
    // CantusFirmusCarrier (25): every note carries CantusFirmusReplayed (the
    // CFEmbellishmentApplied bit is OR-ed onto every note when the embellished
    // line is replayed, not encoded here).
    {"CantusFirmusCarrier", true, ReplayKind::kVerbatimVector, CantusFirmusReplayed, true},
    // PassacagliaGround (26): every note carries PassacagliaGroundReplayed (the
    // period-tiled immutable 8-bar ground bass).
    {"PassacagliaGround", true, ReplayKind::kVerbatimVector, PassacagliaGroundReplayed, true},
    // PassacagliaVariation (27): every note carries VariationApplied (the
    // ClimaxPlaced bit is OR-ed onto every note of an is_climax variation block in
    // the replay branch, not encoded here).
    {"PassacagliaVariation", true, ReplayKind::kVerbatimVector, VariationApplied, true},
    // TrioVoiceCarrier (28): every note carries TrioVoiceIndependent (the
    // independence of the three replayed voices is measured by the Validator's
    // voice_independence_threshold rule from notes carrying this bit).
    {"TrioVoiceCarrier", true, ReplayKind::kVerbatimVector, TrioVoiceIndependent, true},
    // FantasiaCarrier (29): every note carries FantasiaSectionContrast (the
    // adjacent-section contrast in density / register is measured by the
    // Validator's section_contrast_required rule from notes carrying this bit).
    {"FantasiaCarrier", true, ReplayKind::kVerbatimVector, FantasiaSectionContrast, true},
    {"GoldbergBassCarrier", true, ReplayKind::kVerbatimVector, GoldbergBassReplayed, true},
    {"GoldbergVariationCarrier", true, ReplayKind::kVerbatimVector, GoldbergVariationRealized,
     true},
    {"GoldbergInnerVoiceCarrier", true, ReplayKind::kVerbatimVector, GoldbergInnerVoiceRealized,
     true},
};

constexpr std::size_t kIntentCount =
    static_cast<std::size_t>(VoiceIntent::GoldbergInnerVoiceCarrier) + 1;

static_assert(sizeof(kIntentTable) / sizeof(kIntentTable[0]) == kIntentCount,
              "kIntentTable must have one entry per VoiceIntent enumerator");

const IntentDescriptor kUnknownDescriptor = {"Unknown", false, ReplayKind::kInvalid, ChordTone,
                                             false};

}  // namespace

const IntentDescriptor& describeIntent(VoiceIntent intent) {
  const auto index = static_cast<std::size_t>(intent);
  if (index >= kIntentCount)
    return kUnknownDescriptor;
  return kIntentTable[index];
}

const char* voiceIntentToString(VoiceIntent intent) {
  return describeIntent(intent).name;
}

bool isCarrierIntent(VoiceIntent intent) {
  return describeIntent(intent).is_carrier;
}

}  // namespace bach::composer
