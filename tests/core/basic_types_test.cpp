// Tests for core/basic_types.h -- enum conversions, timing utilities, struct layout.

#include "core/basic_types.h"

#include <gtest/gtest.h>

#include <string>

namespace bach {
namespace {

// ---------------------------------------------------------------------------
// Timing constants and conversions
// ---------------------------------------------------------------------------

TEST(BasicTypesTest, TimingConstants) {
  EXPECT_EQ(kTicksPerBeat, 480u);
  EXPECT_EQ(kBeatsPerBar, 4u);
  EXPECT_EQ(kTicksPerBar, 1920u);
  EXPECT_EQ(kMidiC4, 60u);
}

TEST(BasicTypesTest, TickToBar) {
  EXPECT_EQ(tickToBar(0), 0u);
  EXPECT_EQ(tickToBar(1919), 0u);
  EXPECT_EQ(tickToBar(1920), 1u);
  EXPECT_EQ(tickToBar(3840), 2u);
  EXPECT_EQ(tickToBar(5000), 2u);
}

TEST(BasicTypesTest, TickToBeat) {
  EXPECT_EQ(tickToBeat(0), 0u);
  EXPECT_EQ(tickToBeat(479), 0u);
  EXPECT_EQ(tickToBeat(480), 1u);
  EXPECT_EQ(tickToBeat(960), 2u);
  EXPECT_EQ(tickToBeat(1920), 4u);
}

TEST(BasicTypesTest, PositionInBar) {
  EXPECT_EQ(positionInBar(0), 0u);
  EXPECT_EQ(positionInBar(480), 480u);
  EXPECT_EQ(positionInBar(1920), 0u);
  EXPECT_EQ(positionInBar(2400), 480u);
}

TEST(BasicTypesTest, BeatInBar) {
  EXPECT_EQ(beatInBar(0), 0u);
  EXPECT_EQ(beatInBar(480), 1u);
  EXPECT_EQ(beatInBar(960), 2u);
  EXPECT_EQ(beatInBar(1440), 3u);
  EXPECT_EQ(beatInBar(1920), 0u);  // Next bar, beat 0
}

TEST(BasicTypesTest, BarToTick) {
  EXPECT_EQ(barToTick(0), 0u);
  EXPECT_EQ(barToTick(1), 1920u);
  EXPECT_EQ(barToTick(4), 7680u);
}

// ---------------------------------------------------------------------------
// VoiceRole
// ---------------------------------------------------------------------------

TEST(BasicTypesTest, VoiceRoleToString) {
  EXPECT_STREQ(voiceRoleToString(VoiceRole::Assert), "Assert");
  EXPECT_STREQ(voiceRoleToString(VoiceRole::Respond), "Respond");
  EXPECT_STREQ(voiceRoleToString(VoiceRole::Propel), "Propel");
  EXPECT_STREQ(voiceRoleToString(VoiceRole::Ground), "Ground");
}

// ---------------------------------------------------------------------------
// FuguePhase
// ---------------------------------------------------------------------------

TEST(BasicTypesTest, FuguePhaseToString) {
  EXPECT_STREQ(fuguePhaseToString(FuguePhase::Establish), "Establish");
  EXPECT_STREQ(fuguePhaseToString(FuguePhase::Develop), "Develop");
  EXPECT_STREQ(fuguePhaseToString(FuguePhase::Resolve), "Resolve");
}

// ---------------------------------------------------------------------------
// SubjectCharacter
// ---------------------------------------------------------------------------

TEST(BasicTypesTest, SubjectCharacterToString) {
  EXPECT_STREQ(subjectCharacterToString(SubjectCharacter::Severe), "Severe");
  EXPECT_STREQ(subjectCharacterToString(SubjectCharacter::Playful), "Playful");
  EXPECT_STREQ(subjectCharacterToString(SubjectCharacter::Noble), "Noble");
  EXPECT_STREQ(subjectCharacterToString(SubjectCharacter::Restless), "Restless");
}

// ---------------------------------------------------------------------------
// ArcPhase
// ---------------------------------------------------------------------------

TEST(BasicTypesTest, ArcPhaseToString) {
  EXPECT_STREQ(arcPhaseToString(ArcPhase::Ascent), "Ascent");
  EXPECT_STREQ(arcPhaseToString(ArcPhase::Peak), "Peak");
  EXPECT_STREQ(arcPhaseToString(ArcPhase::Descent), "Descent");
}

// ---------------------------------------------------------------------------
// VariationRole
// ---------------------------------------------------------------------------

TEST(BasicTypesTest, VariationRoleToString) {
  EXPECT_STREQ(variationRoleToString(VariationRole::Establish), "Establish");
  EXPECT_STREQ(variationRoleToString(VariationRole::Develop), "Develop");
  EXPECT_STREQ(variationRoleToString(VariationRole::Destabilize), "Destabilize");
  EXPECT_STREQ(variationRoleToString(VariationRole::Illuminate), "Illuminate");
  EXPECT_STREQ(variationRoleToString(VariationRole::Accumulate), "Accumulate");
  EXPECT_STREQ(variationRoleToString(VariationRole::Resolve), "Resolve");
}

// ---------------------------------------------------------------------------
// FailKind
// ---------------------------------------------------------------------------

TEST(BasicTypesTest, FailKindToString) {
  EXPECT_STREQ(failKindToString(FailKind::StructuralFail), "StructuralFail");
  EXPECT_STREQ(failKindToString(FailKind::MusicalFail), "MusicalFail");
  EXPECT_STREQ(failKindToString(FailKind::ConfigFail), "ConfigFail");
}

// ---------------------------------------------------------------------------
// FormType
// ---------------------------------------------------------------------------

TEST(BasicTypesTest, FormTypeToString) {
  EXPECT_STREQ(formTypeToString(FormType::Fugue), "fugue");
  EXPECT_STREQ(formTypeToString(FormType::PreludeAndFugue), "prelude_and_fugue");
  EXPECT_STREQ(formTypeToString(FormType::TrioSonata), "trio_sonata");
  EXPECT_STREQ(formTypeToString(FormType::ChoralePrelude), "chorale_prelude");
  EXPECT_STREQ(formTypeToString(FormType::ToccataAndFugue), "toccata_and_fugue");
  EXPECT_STREQ(formTypeToString(FormType::Passacaglia), "passacaglia");
  EXPECT_STREQ(formTypeToString(FormType::FantasiaAndFugue), "fantasia_and_fugue");
  EXPECT_STREQ(formTypeToString(FormType::CelloPrelude), "cello_prelude");
  EXPECT_STREQ(formTypeToString(FormType::Chaconne), "chaconne");
}

TEST(BasicTypesTest, FormTypeFromStringValidInputs) {
  EXPECT_EQ(formTypeFromString("fugue"), FormType::Fugue);
  EXPECT_EQ(formTypeFromString("prelude_and_fugue"), FormType::PreludeAndFugue);
  EXPECT_EQ(formTypeFromString("trio_sonata"), FormType::TrioSonata);
  EXPECT_EQ(formTypeFromString("chorale_prelude"), FormType::ChoralePrelude);
  EXPECT_EQ(formTypeFromString("toccata_and_fugue"), FormType::ToccataAndFugue);
  EXPECT_EQ(formTypeFromString("passacaglia"), FormType::Passacaglia);
  EXPECT_EQ(formTypeFromString("fantasia_and_fugue"), FormType::FantasiaAndFugue);
  EXPECT_EQ(formTypeFromString("cello_prelude"), FormType::CelloPrelude);
  EXPECT_EQ(formTypeFromString("chaconne"), FormType::Chaconne);
}

TEST(BasicTypesTest, FormTypeFromStringUnknownDefaultsToFugue) {
  EXPECT_EQ(formTypeFromString("unknown_form"), FormType::Fugue);
  EXPECT_EQ(formTypeFromString(""), FormType::Fugue);
}

TEST(BasicTypesTest, FormTypeRoundTrip) {
  // Verify that toString -> fromString round-trips for all form types.
  FormType forms[] = {
      FormType::Fugue,         FormType::PreludeAndFugue,
      FormType::TrioSonata,    FormType::ChoralePrelude,
      FormType::ToccataAndFugue, FormType::Passacaglia,
      FormType::FantasiaAndFugue, FormType::CelloPrelude,
      FormType::Chaconne};

  for (auto form : forms) {
    std::string name = formTypeToString(form);
    EXPECT_EQ(formTypeFromString(name), form) << "Round-trip failed for: " << name;
  }
}

// ---------------------------------------------------------------------------
// Key
// ---------------------------------------------------------------------------

TEST(BasicTypesTest, KeyToString) {
  EXPECT_STREQ(keyToString(Key::C), "C");
  EXPECT_STREQ(keyToString(Key::Cs), "C#");
  EXPECT_STREQ(keyToString(Key::D), "D");
  EXPECT_STREQ(keyToString(Key::Eb), "Eb");
  EXPECT_STREQ(keyToString(Key::E), "E");
  EXPECT_STREQ(keyToString(Key::F), "F");
  EXPECT_STREQ(keyToString(Key::Fs), "F#");
  EXPECT_STREQ(keyToString(Key::G), "G");
  EXPECT_STREQ(keyToString(Key::Ab), "Ab");
  EXPECT_STREQ(keyToString(Key::A), "A");
  EXPECT_STREQ(keyToString(Key::Bb), "Bb");
  EXPECT_STREQ(keyToString(Key::B), "B");
}

// ---------------------------------------------------------------------------
// InstrumentType
// ---------------------------------------------------------------------------

TEST(BasicTypesTest, InstrumentTypeToString) {
  EXPECT_STREQ(instrumentTypeToString(InstrumentType::Organ), "organ");
  EXPECT_STREQ(instrumentTypeToString(InstrumentType::Harpsichord), "harpsichord");
  EXPECT_STREQ(instrumentTypeToString(InstrumentType::Piano), "piano");
  EXPECT_STREQ(instrumentTypeToString(InstrumentType::Violin), "violin");
  EXPECT_STREQ(instrumentTypeToString(InstrumentType::Cello), "cello");
  EXPECT_STREQ(instrumentTypeToString(InstrumentType::Guitar), "guitar");
}

// ---------------------------------------------------------------------------
// Struct defaults
// ---------------------------------------------------------------------------

TEST(BasicTypesTest, NoteEventDefaults) {
  NoteEvent note;
  EXPECT_EQ(note.start_tick, 0u);
  EXPECT_EQ(note.duration, 0u);
  EXPECT_EQ(note.pitch, 0u);
  EXPECT_EQ(note.velocity, 80u);  // Organ default
  EXPECT_EQ(note.voice, 0u);
}

TEST(BasicTypesTest, TrackDefaults) {
  Track track;
  EXPECT_EQ(track.channel, 0u);
  EXPECT_EQ(track.program, 0u);
  EXPECT_TRUE(track.name.empty());
  EXPECT_TRUE(track.notes.empty());
  EXPECT_TRUE(track.events.empty());
}

TEST(BasicTypesTest, TempoEventDefaults) {
  TempoEvent tempo;
  EXPECT_EQ(tempo.tick, 0u);
  EXPECT_EQ(tempo.bpm, 120u);
}

TEST(BasicTypesTest, MidiEventDefaults) {
  MidiEvent event;
  EXPECT_EQ(event.tick, 0u);
  EXPECT_EQ(event.status, 0u);
  EXPECT_EQ(event.data1, 0u);
  EXPECT_EQ(event.data2, 0u);
}

// ---------------------------------------------------------------------------
// NoteEvent: modified_by field and struct layout
// ---------------------------------------------------------------------------

TEST(NoteEventTest, NewNoteHasNoModifications) {
  NoteEvent note;
  EXPECT_EQ(note.modified_by, 0);
}

TEST(NoteEventTest, SizeIs16Bytes) {
  // NoteEvent is a hot struct in tight loops; guard against accidental bloat.
  EXPECT_EQ(sizeof(NoteEvent), 16u);
}

TEST(NoteEventTest, ModifiedByAccumulatesFlags) {
  NoteEvent note;
  note.modified_by |= static_cast<uint8_t>(NoteModifiedBy::ParallelRepair);
  note.modified_by |= static_cast<uint8_t>(NoteModifiedBy::OverlapTrim);
  EXPECT_EQ(note.modified_by, 0x09);  // 1 | 8
}

}  // namespace
}  // namespace bach
