// Implementation of enum-to-string and string-to-enum conversions.

#include "core/basic_types.h"

namespace bach {

const char* voiceRoleToString(VoiceRole role) {
  switch (role) {
    case VoiceRole::Assert:  return "Assert";
    case VoiceRole::Respond: return "Respond";
    case VoiceRole::Propel:  return "Propel";
    case VoiceRole::Ground:  return "Ground";
  }
  return "Unknown";
}

const char* fuguePhaseToString(FuguePhase phase) {
  switch (phase) {
    case FuguePhase::Establish: return "Establish";
    case FuguePhase::Develop:   return "Develop";
    case FuguePhase::Resolve:   return "Resolve";
  }
  return "Unknown";
}

const char* subjectCharacterToString(SubjectCharacter character) {
  switch (character) {
    case SubjectCharacter::Severe:   return "Severe";
    case SubjectCharacter::Playful:  return "Playful";
    case SubjectCharacter::Noble:    return "Noble";
    case SubjectCharacter::Restless: return "Restless";
  }
  return "Unknown";
}

const char* arcPhaseToString(ArcPhase phase) {
  switch (phase) {
    case ArcPhase::Ascent:  return "Ascent";
    case ArcPhase::Peak:    return "Peak";
    case ArcPhase::Descent: return "Descent";
  }
  return "Unknown";
}

const char* variationRoleToString(VariationRole role) {
  switch (role) {
    case VariationRole::Establish:   return "Establish";
    case VariationRole::Develop:     return "Develop";
    case VariationRole::Destabilize: return "Destabilize";
    case VariationRole::Illuminate:  return "Illuminate";
    case VariationRole::Accumulate:  return "Accumulate";
    case VariationRole::Resolve:     return "Resolve";
  }
  return "Unknown";
}

const char* failKindToString(FailKind kind) {
  switch (kind) {
    case FailKind::StructuralFail: return "StructuralFail";
    case FailKind::MusicalFail:    return "MusicalFail";
    case FailKind::ConfigFail:     return "ConfigFail";
  }
  return "Unknown";
}

const char* formTypeToString(FormType form) {
  switch (form) {
    case FormType::Fugue:             return "fugue";
    case FormType::PreludeAndFugue:   return "prelude_and_fugue";
    case FormType::TrioSonata:        return "trio_sonata";
    case FormType::ChoralePrelude:    return "chorale_prelude";
    case FormType::ToccataAndFugue:   return "toccata_and_fugue";
    case FormType::Passacaglia:       return "passacaglia";
    case FormType::FantasiaAndFugue:  return "fantasia_and_fugue";
    case FormType::CelloPrelude:      return "cello_prelude";
    case FormType::Chaconne:          return "chaconne";
  }
  return "unknown";
}

FormType formTypeFromString(const std::string& str) {
  if (str == "fugue")               return FormType::Fugue;
  if (str == "prelude_and_fugue")   return FormType::PreludeAndFugue;
  if (str == "trio_sonata")         return FormType::TrioSonata;
  if (str == "chorale_prelude")     return FormType::ChoralePrelude;
  if (str == "toccata_and_fugue")   return FormType::ToccataAndFugue;
  if (str == "passacaglia")         return FormType::Passacaglia;
  if (str == "fantasia_and_fugue")  return FormType::FantasiaAndFugue;
  if (str == "cello_prelude")       return FormType::CelloPrelude;
  if (str == "chaconne")            return FormType::Chaconne;
  return FormType::Fugue;  // Default
}

const char* keyToString(Key key) {
  switch (key) {
    case Key::C:  return "C";
    case Key::Cs: return "C#";
    case Key::D:  return "D";
    case Key::Eb: return "Eb";
    case Key::E:  return "E";
    case Key::F:  return "F";
    case Key::Fs: return "F#";
    case Key::G:  return "G";
    case Key::Ab: return "Ab";
    case Key::A:  return "A";
    case Key::Bb: return "Bb";
    case Key::B:  return "B";
  }
  return "?";
}

const char* instrumentTypeToString(InstrumentType inst) {
  switch (inst) {
    case InstrumentType::Organ:       return "organ";
    case InstrumentType::Harpsichord: return "harpsichord";
    case InstrumentType::Piano:       return "piano";
    case InstrumentType::Violin:      return "violin";
    case InstrumentType::Cello:       return "cello";
    case InstrumentType::Guitar:      return "guitar";
  }
  return "unknown";
}

const char* durationScaleToString(DurationScale scale) {
  switch (scale) {
    case DurationScale::Short:  return "short";
    case DurationScale::Medium: return "medium";
    case DurationScale::Long:   return "long";
    case DurationScale::Full:   return "full";
  }
  return "unknown";
}

DurationScale durationScaleFromString(const std::string& str) {
  if (str == "short")  return DurationScale::Short;
  if (str == "medium") return DurationScale::Medium;
  if (str == "long")   return DurationScale::Long;
  if (str == "full")   return DurationScale::Full;
  return DurationScale::Short;  // Default
}

}  // namespace bach
