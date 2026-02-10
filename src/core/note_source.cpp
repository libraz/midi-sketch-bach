// Implementation of note source and transform step string conversions.

#include "core/note_source.h"

namespace bach {

const char* bachNoteSourceToString(BachNoteSource source) {
  switch (source) {
    case BachNoteSource::Unknown:          return "unknown";
    case BachNoteSource::FugueSubject:     return "fugue_subject";
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

}  // namespace bach
