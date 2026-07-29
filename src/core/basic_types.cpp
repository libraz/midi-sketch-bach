// Implementation of enum-to-string and string-to-enum conversions.

#include "core/basic_types.h"

#include <array>

namespace bach {
namespace {

constexpr std::array<const char*, 10> kFormDisplayNames = {
    "Fugue",       "Prelude and Fugue",  "Trio Sonata",   "Chorale Prelude", "Toccata and Fugue",
    "Passacaglia", "Fantasia and Fugue", "Cello Prelude", "Chaconne",        "Goldberg Variations",
};
static_assert(kFormDisplayNames.size() ==
                  static_cast<std::size_t>(FormType::GoldbergVariations) + 1,
              "Form display names must cover every FormType");

}  // namespace

const char* voiceRoleToString(VoiceRole role) {
  switch (role) {
    case VoiceRole::Assert:
      return "Assert";
    case VoiceRole::Respond:
      return "Respond";
    case VoiceRole::Propel:
      return "Propel";
    case VoiceRole::Ground:
      return "Ground";
  }
  return "Unknown";
}

const char* fuguePhaseToString(FuguePhase phase) {
  switch (phase) {
    case FuguePhase::Establish:
      return "Establish";
    case FuguePhase::Develop:
      return "Develop";
    case FuguePhase::Resolve:
      return "Resolve";
    case FuguePhase::Conclude:
      return "Conclude";
  }
  return "Unknown";
}

const char* subjectCharacterToString(SubjectCharacter character) {
  switch (character) {
    case SubjectCharacter::Severe:
      return "Severe";
    case SubjectCharacter::Playful:
      return "Playful";
    case SubjectCharacter::Noble:
      return "Noble";
    case SubjectCharacter::Restless:
      return "Restless";
  }
  return "Unknown";
}

const char* toccataArchetypeToString(ToccataArchetype archetype) {
  switch (archetype) {
    case ToccataArchetype::Dramaticus:
      return "dramaticus";
    case ToccataArchetype::Perpetuus:
      return "perpetuus";
    case ToccataArchetype::Concertato:
      return "concertato";
    case ToccataArchetype::Sectionalis:
      return "sectionalis";
  }
  return "unknown";
}

ToccataArchetype toccataArchetypeFromString(const std::string& str) {
  if (str == "dramaticus")
    return ToccataArchetype::Dramaticus;
  if (str == "perpetuus")
    return ToccataArchetype::Perpetuus;
  if (str == "concertato")
    return ToccataArchetype::Concertato;
  if (str == "sectionalis")
    return ToccataArchetype::Sectionalis;
  return ToccataArchetype::Dramaticus;  // Default
}

const char* fugueArchetypeToString(FugueArchetype archetype) {
  switch (archetype) {
    case FugueArchetype::Compact:
      return "Compact";
    case FugueArchetype::Cantabile:
      return "Cantabile";
    case FugueArchetype::Invertible:
      return "Invertible";
    case FugueArchetype::Chromatic:
      return "Chromatic";
  }
  return "Compact";
}

const char* arcPhaseToString(ArcPhase phase) {
  switch (phase) {
    case ArcPhase::Ascent:
      return "Ascent";
    case ArcPhase::Peak:
      return "Peak";
    case ArcPhase::Descent:
      return "Descent";
  }
  return "Unknown";
}

const char* variationRoleToString(VariationRole role) {
  switch (role) {
    case VariationRole::Establish:
      return "Establish";
    case VariationRole::Develop:
      return "Develop";
    case VariationRole::Destabilize:
      return "Destabilize";
    case VariationRole::Illuminate:
      return "Illuminate";
    case VariationRole::Accumulate:
      return "Accumulate";
    case VariationRole::Resolve:
      return "Resolve";
  }
  return "Unknown";
}

const char* failKindToString(FailKind kind) {
  switch (kind) {
    case FailKind::StructuralFail:
      return "StructuralFail";
    case FailKind::MusicalFail:
      return "MusicalFail";
    case FailKind::ConfigFail:
      return "ConfigFail";
  }
  return "Unknown";
}

const char* formTypeToString(FormType form) {
  switch (form) {
    case FormType::Fugue:
      return "fugue";
    case FormType::PreludeAndFugue:
      return "prelude_and_fugue";
    case FormType::TrioSonata:
      return "trio_sonata";
    case FormType::ChoralePrelude:
      return "chorale_prelude";
    case FormType::ToccataAndFugue:
      return "toccata_and_fugue";
    case FormType::Passacaglia:
      return "passacaglia";
    case FormType::FantasiaAndFugue:
      return "fantasia_and_fugue";
    case FormType::CelloPrelude:
      return "cello_prelude";
    case FormType::Chaconne:
      return "chaconne";
    case FormType::GoldbergVariations:
      return "goldberg_variations";
  }
  return "unknown";
}

const char* formTypeToDisplayString(FormType form) {
  const std::size_t index = static_cast<std::size_t>(form);
  return index < kFormDisplayNames.size() ? kFormDisplayNames[index] : "Composition";
}

FormType formTypeFromString(const std::string& str) {
  if (str == "fugue")
    return FormType::Fugue;
  if (str == "prelude_and_fugue")
    return FormType::PreludeAndFugue;
  if (str == "trio_sonata")
    return FormType::TrioSonata;
  if (str == "chorale_prelude")
    return FormType::ChoralePrelude;
  if (str == "toccata_and_fugue")
    return FormType::ToccataAndFugue;
  if (str == "passacaglia")
    return FormType::Passacaglia;
  if (str == "fantasia_and_fugue")
    return FormType::FantasiaAndFugue;
  if (str == "cello_prelude")
    return FormType::CelloPrelude;
  if (str == "chaconne")
    return FormType::Chaconne;
  if (str == "goldberg_variations")
    return FormType::GoldbergVariations;
  return FormType::Fugue;  // Default
}

const char* keyToString(Key key) {
  switch (key) {
    case Key::C:
      return "C";
    case Key::Cs:
      return "C#";
    case Key::D:
      return "D";
    case Key::Eb:
      return "Eb";
    case Key::E:
      return "E";
    case Key::F:
      return "F";
    case Key::Fs:
      return "F#";
    case Key::G:
      return "G";
    case Key::Ab:
      return "Ab";
    case Key::A:
      return "A";
    case Key::Bb:
      return "Bb";
    case Key::B:
      return "B";
  }
  return "?";
}

const char* instrumentTypeToString(InstrumentType inst) {
  switch (inst) {
    case InstrumentType::Organ:
      return "organ";
    case InstrumentType::Harpsichord:
      return "harpsichord";
    case InstrumentType::Piano:
      return "piano";
    case InstrumentType::Violin:
      return "violin";
    case InstrumentType::Cello:
      return "cello";
    case InstrumentType::Guitar:
      return "guitar";
  }
  return "unknown";
}

InstrumentType defaultInstrumentForForm(FormType form) {
  switch (form) {
    case FormType::Fugue:
    case FormType::PreludeAndFugue:
    case FormType::TrioSonata:
    case FormType::ChoralePrelude:
    case FormType::ToccataAndFugue:
    case FormType::Passacaglia:
    case FormType::FantasiaAndFugue:
      return InstrumentType::Organ;

    case FormType::CelloPrelude:
      return InstrumentType::Cello;

    case FormType::Chaconne:
      return InstrumentType::Violin;

    case FormType::GoldbergVariations:
      return InstrumentType::Harpsichord;
  }

  return InstrumentType::Organ;
}

bool isInstrumentCompatibleWithForm(FormType form, InstrumentType instrument) {
  switch (form) {
    case FormType::Fugue:
    case FormType::PreludeAndFugue:
    case FormType::TrioSonata:
    case FormType::ChoralePrelude:
    case FormType::ToccataAndFugue:
    case FormType::Passacaglia:
    case FormType::FantasiaAndFugue:
      return instrument == InstrumentType::Organ;
    case FormType::CelloPrelude:
      return instrument == InstrumentType::Cello;
    case FormType::Chaconne:
      return instrument == InstrumentType::Violin;
    case FormType::GoldbergVariations:
      return instrument == InstrumentType::Harpsichord || instrument == InstrumentType::Piano;
  }
  return false;
}

InstrumentType instrumentTypeFromString(const std::string& str) {
  if (str == "organ")
    return InstrumentType::Organ;
  if (str == "harpsichord")
    return InstrumentType::Harpsichord;
  if (str == "piano")
    return InstrumentType::Piano;
  if (str == "violin")
    return InstrumentType::Violin;
  if (str == "cello")
    return InstrumentType::Cello;
  if (str == "guitar")
    return InstrumentType::Guitar;
  return InstrumentType::Organ;
}

const char* durationScaleToString(DurationScale scale) {
  switch (scale) {
    case DurationScale::Short:
      return "short";
    case DurationScale::Medium:
      return "medium";
    case DurationScale::Long:
      return "long";
    case DurationScale::Full:
      return "full";
  }
  return "unknown";
}

DurationScale durationScaleFromString(const std::string& str) {
  if (str == "short")
    return DurationScale::Short;
  if (str == "medium")
    return DurationScale::Medium;
  if (str == "long")
    return DurationScale::Long;
  if (str == "full")
    return DurationScale::Full;
  return DurationScale::Short;  // Default
}

MetricalStrength getMetricalStrength(int beat_in_bar, MeterProfile profile) {
  if (beat_in_bar == 0)
    return MetricalStrength::Strong;
  if (profile == MeterProfile::SarabandeTriple && beat_in_bar == 1) {
    return MetricalStrength::Medium;
  }
  return MetricalStrength::Weak;
}

}  // namespace bach
