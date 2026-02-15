// Implementation of note source and transform step string conversions.

#include "core/note_source.h"

#include <string>

#include "core/basic_types.h"

namespace bach {

const char* bachNoteSourceToString(BachNoteSource source) {
  switch (source) {
    case BachNoteSource::Unknown:          return "unknown";
    case BachNoteSource::FugueSubject:     return "fugue_subject";
    case BachNoteSource::SubjectCore:     return "subject_core";
    case BachNoteSource::FugueAnswer:      return "fugue_answer";
    case BachNoteSource::Countersubject:   return "countersubject";
    case BachNoteSource::EpisodeMaterial:  return "episode_material";
    case BachNoteSource::FreeCounterpoint: return "free_counterpoint";
    case BachNoteSource::CantusFixed:      return "cantus_fixed";
    case BachNoteSource::Ornament:         return "ornament";
    case BachNoteSource::PedalPoint:       return "pedal_point";
    case BachNoteSource::ArpeggioFlow:     return "arpeggio_flow";
    case BachNoteSource::TextureNote:      return "texture_note";
    case BachNoteSource::GroundBass:       return "ground_bass";
    case BachNoteSource::CollisionAvoid:   return "collision_avoid";
    case BachNoteSource::PostProcess:      return "post_process";
    case BachNoteSource::ChromaticPassing: return "chromatic_passing";
    case BachNoteSource::FalseEntry:      return "false_entry";
    case BachNoteSource::Coda:            return "coda";
    case BachNoteSource::SequenceNote:    return "sequence_note";
    case BachNoteSource::CanonDux:        return "canon_dux";
    case BachNoteSource::CanonComes:      return "canon_comes";
    case BachNoteSource::CanonFreeBass:   return "canon_free_bass";
    case BachNoteSource::GoldbergAria:    return "goldberg_aria";
    case BachNoteSource::GoldbergBass:    return "goldberg_bass";
    case BachNoteSource::GoldbergFigura:  return "goldberg_figura";
    case BachNoteSource::GoldbergSoggetto: return "goldberg_soggetto";
    case BachNoteSource::GoldbergDance:   return "goldberg_dance";
    case BachNoteSource::GoldbergFughetta: return "goldberg_fughetta";
    case BachNoteSource::GoldbergInvention: return "goldberg_invention";
    case BachNoteSource::QuodlibetMelody: return "quodlibet_melody";
    case BachNoteSource::GoldbergOverture: return "goldberg_overture";
    case BachNoteSource::GoldbergSuspension: return "goldberg_suspension";
    case BachNoteSource::ChaconneBass: return "chaconne_bass";
    case BachNoteSource::PreludeFiguration: return "prelude_figuration";
    case BachNoteSource::ToccataGesture: return "toccata_gesture";
    case BachNoteSource::GrandPause: return "grand_pause";
  }
  return "unknown";
}

const char* bachTransformStepToString(BachTransformStep step) {
  switch (step) {
    case BachTransformStep::None:           return "none";
    case BachTransformStep::TonalAnswer:    return "tonal_answer";
    case BachTransformStep::RealAnswer:     return "real_answer";
    case BachTransformStep::Inversion:      return "inversion";
    case BachTransformStep::Retrograde:     return "retrograde";
    case BachTransformStep::Augmentation:   return "augmentation";
    case BachTransformStep::Diminution:     return "diminution";
    case BachTransformStep::Sequence:       return "sequence";
    case BachTransformStep::CollisionAvoid: return "collision_avoid";
    case BachTransformStep::RangeClamp:     return "range_clamp";
    case BachTransformStep::OctaveAdjust:   return "octave_adjust";
    case BachTransformStep::KeyTranspose:   return "key_transpose";
  }
  return "unknown";
}

ProtectionLevel getProtectionLevel(BachNoteSource source) {
  switch (source) {
    case BachNoteSource::SubjectCore:
    case BachNoteSource::CantusFixed:
    case BachNoteSource::GroundBass:
    case BachNoteSource::GoldbergBass:
    case BachNoteSource::QuodlibetMelody:
      return ProtectionLevel::Immutable;

    case BachNoteSource::FugueSubject:
      return ProtectionLevel::SemiImmutable;

    case BachNoteSource::FugueAnswer:
    case BachNoteSource::Countersubject:
    case BachNoteSource::PedalPoint:
    case BachNoteSource::FalseEntry:
    case BachNoteSource::Coda:
    case BachNoteSource::SequenceNote:
    case BachNoteSource::CanonDux:
    case BachNoteSource::CanonComes:
    case BachNoteSource::GoldbergAria:
    case BachNoteSource::GoldbergSoggetto:
    case BachNoteSource::GoldbergFughetta:
      return ProtectionLevel::Structural;

    case BachNoteSource::EpisodeMaterial:
    case BachNoteSource::FreeCounterpoint:
    case BachNoteSource::Ornament:
    case BachNoteSource::ArpeggioFlow:
    case BachNoteSource::TextureNote:
    case BachNoteSource::CollisionAvoid:
    case BachNoteSource::PostProcess:
    case BachNoteSource::ChromaticPassing:
    case BachNoteSource::Unknown:
    case BachNoteSource::CanonFreeBass:
    case BachNoteSource::GoldbergFigura:
    case BachNoteSource::GoldbergDance:
    case BachNoteSource::GoldbergInvention:
    case BachNoteSource::GoldbergOverture:
    case BachNoteSource::GoldbergSuspension:
      return ProtectionLevel::Flexible;

    case BachNoteSource::ChaconneBass:
    case BachNoteSource::ToccataGesture:
      return ProtectionLevel::Structural;

    case BachNoteSource::GrandPause:
      return ProtectionLevel::Immutable;

    case BachNoteSource::PreludeFiguration:
      return ProtectionLevel::Flexible;
  }
  return ProtectionLevel::Flexible;
}

std::string noteModifiedByToString(uint8_t flags) {
  if (flags == 0) return "none";
  std::string result;
  auto append = [&](uint8_t bit, const char* name) {
    if (flags & bit) {
      if (!result.empty()) result += ',';
      result += name;
    }
  };
  append(static_cast<uint8_t>(NoteModifiedBy::ParallelRepair),  "parallel_repair");
  append(static_cast<uint8_t>(NoteModifiedBy::ChordToneSnap),   "chord_tone_snap");
  append(static_cast<uint8_t>(NoteModifiedBy::LeapResolution),   "leap_resolution");
  append(static_cast<uint8_t>(NoteModifiedBy::OverlapTrim),      "overlap_trim");
  append(static_cast<uint8_t>(NoteModifiedBy::OctaveAdjust),     "octave_adjust");
  append(static_cast<uint8_t>(NoteModifiedBy::Articulation),     "articulation");
  append(static_cast<uint8_t>(NoteModifiedBy::RepeatedNoteRep),  "repeated_note_rep");
  return result;
}

int countUnknownSource(const std::vector<NoteEvent>& notes) {
  int count = 0;
  for (const auto& n : notes) {
    if (n.source == BachNoteSource::Unknown) {
      ++count;
    }
  }
  return count;
}

}  // namespace bach
