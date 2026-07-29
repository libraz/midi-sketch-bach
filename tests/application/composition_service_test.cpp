#include "application/composition_service.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "composer/form_director.h"
#include "core/pitch_utils.h"
#include "midi/midi_reader.h"

namespace bach::application {
namespace {

CompositionRequest fugueRequest(std::uint32_t seed) {
  CompositionRequest request;
  request.form = FormType::Fugue;
  request.key = {Key::G, true};
  request.character = SubjectCharacter::Severe;
  request.instrument = InstrumentType::Organ;
  request.instrument_specified = true;
  request.scale = DurationScale::Short;
  request.bpm = 100;
  request.seed = seed;
  return request;
}

std::vector<std::uint8_t> homepagePitches(const std::string& json) {
  constexpr const char* kPitchKey = "\"pitch\":";
  std::vector<std::uint8_t> pitches;
  std::size_t offset = 0;
  while ((offset = json.find(kPitchKey, offset)) != std::string::npos) {
    offset += std::char_traits<char>::length(kPitchKey);
    const std::size_t end = json.find_first_not_of("0123456789", offset);
    pitches.push_back(static_cast<std::uint8_t>(std::stoul(json.substr(offset, end - offset))));
    offset = end;
  }
  return pitches;
}

std::vector<std::uint8_t> midiPitches(const ParsedMidi& midi) {
  std::vector<std::uint8_t> pitches;
  for (const auto& track : midi.tracks) {
    for (const auto& note : track.notes) {
      pitches.push_back(note.pitch);
    }
  }
  return pitches;
}

TEST(CompositionServiceTest, RejectsInvalidArgumentsByStatus) {
  CompositionRequest request;
  EXPECT_EQ(compose(request, nullptr), CompositionStatus::InvalidArgument);
}

TEST(CompositionServiceTest, ResolvesZeroBpmToSharedDefault) {
  CompositionRequest request;
  request.seed = 99;
  CompositionProduct product;
  ASSERT_EQ(compose(request, &product), CompositionStatus::Ok);
  EXPECT_EQ(product.bpm, kDefaultBpm);
}

TEST(CompositionServiceTest, ResolvesInstrumentAndFormSpecificPerformanceProfiles) {
  const auto organ_fugue = resolvePerformanceProfile(FormType::Fugue, InstrumentType::Organ);
  EXPECT_TRUE(organ_fugue.registration_terraces);
  EXPECT_FALSE(organ_fugue.continuous_expression);
  EXPECT_EQ(organ_fugue.final_ritardando, composer::RitardandoStyle::Gentle);

  const auto organ_toccata =
      resolvePerformanceProfile(FormType::ToccataAndFugue, InstrumentType::Organ);
  EXPECT_EQ(organ_toccata.final_ritardando, composer::RitardandoStyle::Rhetorical);

  const auto cello = resolvePerformanceProfile(FormType::CelloPrelude, InstrumentType::Cello);
  EXPECT_FALSE(cello.registration_terraces);
  EXPECT_TRUE(cello.continuous_expression);
  EXPECT_EQ(cello.final_ritardando, composer::RitardandoStyle::Gentle);

  const auto harpsichord =
      resolvePerformanceProfile(FormType::GoldbergVariations, InstrumentType::Harpsichord);
  EXPECT_TRUE(harpsichord.registration_terraces);
  EXPECT_FALSE(harpsichord.continuous_expression);
  EXPECT_EQ(harpsichord.final_ritardando, composer::RitardandoStyle::Gentle);
}

TEST(CompositionServiceTest, ResolvesAndOwnsCompleteProductOutput) {
  CompositionProduct product;
  ASSERT_EQ(compose(fugueRequest(404), &product), CompositionStatus::Ok);
  EXPECT_EQ(product.status, CompositionStatus::Ok);
  EXPECT_EQ(product.seed, 404u);
  EXPECT_EQ(product.instrument, InstrumentType::Organ);
  EXPECT_GT(product.total_ticks, 0u);
  EXPECT_GT(product.total_bars, 0u);
  EXPECT_FALSE(product.composition.notes.empty());
  EXPECT_EQ(product.composition.notes.size(), product.composition.provenance.size());
  EXPECT_FALSE(product.midi_bytes.empty());
  EXPECT_NE(product.homepage_events_json.find("\"form\":\"fugue\""), std::string::npos);
  EXPECT_NE(product.homepage_events_json.find("\"tempos\":[{\"tick\":0,\"bpm\":100}"),
            std::string::npos);
  EXPECT_NE(product.homepage_events_json.find("\"time_signatures\":[{\"tick\":0,"),
            std::string::npos);
  EXPECT_NE(product.homepage_events_json.find("\"control_changes\":["), std::string::npos);
  EXPECT_NE(product.generated_json.find("\"schema_version\":\"generated.v1\""), std::string::npos);
  EXPECT_NE(product.generated_json.find("\"tempos\":[{\"tick\":0,\"bpm\":100}"), std::string::npos);
  EXPECT_NE(product.provenance_json.find("\"schema_version\":\"provenance.v1\""),
            std::string::npos);
  EXPECT_TRUE(product.diagnostic_json.empty());
  ASSERT_FALSE(product.tempo_events.empty());
  EXPECT_EQ(product.tempo_events.front().tick, 0u);
  EXPECT_EQ(product.tempo_events.front().bpm, 100u);
  for (std::size_t i = 1; i < product.tempo_events.size(); ++i) {
    EXPECT_GE(product.tempo_events[i].tick, product.tempo_events[i - 1].tick);
  }
  bool saw_registration = false;
  for (const auto& track : product.composition.tracks) {
    for (const auto& event : track.cc_events) {
      saw_registration = saw_registration || event.controller == 7;
      EXPECT_NE(event.controller, 11);
    }
  }
  EXPECT_TRUE(saw_registration);

  MidiReader reader;
  ASSERT_TRUE(reader.read(product.midi_bytes)) << reader.getError();
  ASSERT_TRUE(reader.getParsedMidi().has_key_signature);
  EXPECT_EQ(reader.getParsedMidi().key_signature, product.key);
}

TEST(CompositionServiceTest, NonCEventPitchesMatchRenderedMidi) {
  CompositionProduct product;
  ASSERT_EQ(compose(fugueRequest(407), &product), CompositionStatus::Ok);

  MidiReader reader;
  ASSERT_TRUE(reader.read(product.midi_bytes)) << reader.getError();
  const std::vector<std::uint8_t> event_pitches = homepagePitches(product.homepage_events_json);
  const std::vector<std::uint8_t> midi_pitches = midiPitches(reader.getParsedMidi());
  ASSERT_FALSE(event_pitches.empty());
  EXPECT_EQ(event_pitches, midi_pitches);

  std::vector<std::uint8_t> sorted_event_pitches = event_pitches;
  std::sort(sorted_event_pitches.begin(), sorted_event_pitches.end());
  std::vector<std::uint8_t> expected_pitches;
  for (const auto& note : product.composition.notes) {
    expected_pitches.push_back(transposePitch(note.pitch, product.key.tonic));
  }
  std::sort(expected_pitches.begin(), expected_pitches.end());
  EXPECT_EQ(sorted_event_pitches, expected_pitches);
}

TEST(CompositionServiceTest, RejectsIncompatibleFormCharacterBeforeGeneration) {
  CompositionRequest request;
  request.form = FormType::ChoralePrelude;
  request.character = SubjectCharacter::Playful;
  request.bpm = 100;
  CompositionProduct product;
  EXPECT_EQ(compose(request, &product), CompositionStatus::IncompatibleCharacter);
  EXPECT_TRUE(product.midi_bytes.empty());
}

TEST(CompositionServiceTest, FreeCounterpointRejectsFormsWithoutASecondaryTarget) {
  CompositionRequest request;
  request.form = FormType::Fugue;
  request.character = SubjectCharacter::Severe;
  request.seed = 1;
  request.bpm = 100;
  request.enable_free_counterpoint = true;
  CompositionProduct product;
  EXPECT_EQ(compose(request, &product), CompositionStatus::FreeCounterpointUnavailable);
  EXPECT_TRUE(product.midi_bytes.empty());
}

TEST(CompositionServiceTest, FreeCounterpointGeneratesPassacagliaCounterline) {
  CompositionRequest request;
  request.form = FormType::Passacaglia;
  request.character = SubjectCharacter::Severe;
  request.seed = 1;
  request.bpm = 100;
  request.enable_free_counterpoint = true;
  CompositionProduct product;
  ASSERT_EQ(compose(request, &product), CompositionStatus::Ok);
  EXPECT_FALSE(product.midi_bytes.empty());
  EXPECT_EQ(product.final_validation.status, composer::ValidationStatus::Ok);
  EXPECT_TRUE(product.final_validation.failures.empty());
  EXPECT_TRUE(std::any_of(
      product.composition.provenance.begin(), product.composition.provenance.end(),
      [](const composer::NoteProvenance& p) { return p.source == composer::NoteSource::Compose; }));
}

TEST(CompositionServiceTest, GoldbergFullResolvesCompletePublicLayout) {
  CompositionRequest request;
  request.form = FormType::GoldbergVariations;
  request.scale = DurationScale::Full;
  request.character = SubjectCharacter::Severe;
  request.instrument = InstrumentType::Harpsichord;
  request.instrument_specified = true;
  request.seed = 988;
  request.bpm = 84;
  CompositionProduct product;
  ASSERT_EQ(compose(request, &product), CompositionStatus::Ok);
  EXPECT_EQ(product.resolved_bars, 128);
  EXPECT_EQ(product.total_bars, 128);
  EXPECT_EQ(product.performance_profile.final_ritardando, composer::RitardandoStyle::Gentle);
  EXPECT_EQ(product.tempo_events.size(), 5u);
  EXPECT_NE(product.generated_json.find("\"tempos\":[{\"tick\":0,\"bpm\":84}"), std::string::npos);
}

TEST(CompositionServiceTest, FinalScoreGateAcceptsEveryShippedForm) {
  constexpr FormType forms[] = {
      FormType::Fugue,
      FormType::PreludeAndFugue,
      FormType::TrioSonata,
      FormType::ChoralePrelude,
      FormType::ToccataAndFugue,
      FormType::Passacaglia,
      FormType::FantasiaAndFugue,
      FormType::CelloPrelude,
      FormType::Chaconne,
      FormType::GoldbergVariations,
  };
  for (FormType form : forms) {
    SCOPED_TRACE(formTypeToString(form));
    CompositionRequest request;
    request.form = form;
    request.character = SubjectCharacter::Severe;
    request.scale = DurationScale::Short;
    request.seed = 42;
    request.bpm = 100;
    CompositionProduct product;
    ASSERT_EQ(compose(request, &product), CompositionStatus::Ok);
    EXPECT_EQ(product.final_validation.status, composer::ValidationStatus::Ok);
    EXPECT_TRUE(product.final_validation.failures.empty());
    EXPECT_TRUE(product.diagnostic_json.empty());
    EXPECT_FALSE(product.midi_bytes.empty());
  }
}

TEST(CompositionServiceTest, FinalScoreGateSweepsEveryShippedFormConfiguration) {
  constexpr FormType forms[] = {
      FormType::Fugue,
      FormType::PreludeAndFugue,
      FormType::TrioSonata,
      FormType::ChoralePrelude,
      FormType::ToccataAndFugue,
      FormType::Passacaglia,
      FormType::FantasiaAndFugue,
      FormType::CelloPrelude,
      FormType::Chaconne,
      FormType::GoldbergVariations,
  };
  constexpr SubjectCharacter characters[] = {
      SubjectCharacter::Severe,
      SubjectCharacter::Playful,
      SubjectCharacter::Noble,
      SubjectCharacter::Restless,
  };
  constexpr DurationScale scales[] = {
      DurationScale::Short,
      DurationScale::Medium,
      DurationScale::Long,
      DurationScale::Full,
  };
  for (FormType form : forms) {
    for (SubjectCharacter character : characters) {
      if (!composer::isFormCharacterCompatible(form, character)) {
        continue;
      }
      for (DurationScale scale : scales) {
        for (std::uint32_t seed = 1; seed <= 20; ++seed) {
          SCOPED_TRACE(formTypeToString(form));
          SCOPED_TRACE(subjectCharacterToString(character));
          SCOPED_TRACE(durationScaleToString(scale));
          SCOPED_TRACE(seed);
          CompositionRequest request;
          request.form = form;
          request.character = character;
          request.scale = scale;
          request.seed = seed;
          request.bpm = 100;
          CompositionProduct product;
          ASSERT_EQ(compose(request, &product), CompositionStatus::Ok);
          EXPECT_EQ(product.final_validation.status, composer::ValidationStatus::Ok);
          EXPECT_TRUE(product.final_validation.failures.empty());
          EXPECT_TRUE(product.diagnostic_json.empty());
          EXPECT_FALSE(product.midi_bytes.empty());
        }
      }
    }
  }
}

TEST(CompositionServiceTest, NonKeyboardInstrumentUsesPhraseVelocityCurve) {
  CompositionRequest request;
  request.form = FormType::Chaconne;
  request.character = SubjectCharacter::Severe;
  request.instrument = InstrumentType::Violin;
  request.instrument_specified = true;
  request.seed = 42;
  request.bpm = 100;
  CompositionProduct product;
  ASSERT_EQ(compose(request, &product), CompositionStatus::Ok);

  std::vector<std::uint8_t> velocities;
  for (const auto& note : product.composition.notes) {
    velocities.push_back(note.velocity);
  }
  std::sort(velocities.begin(), velocities.end());
  velocities.erase(std::unique(velocities.begin(), velocities.end()), velocities.end());
  EXPECT_GT(velocities.size(), 1u);
}

TEST(CompositionServiceTest, ExplicitIncompatibleInstrumentFailsBeforeGeneration) {
  CompositionRequest request = fugueRequest(42);
  request.instrument = InstrumentType::Cello;
  request.instrument_specified = true;
  CompositionProduct product;
  EXPECT_EQ(compose(request, &product), CompositionStatus::IncompatibleInstrument);
  EXPECT_TRUE(product.composition.notes.empty());
  EXPECT_TRUE(product.midi_bytes.empty());
}

TEST(CompositionServiceTest, RegistrationControlChangesMergeSameTickAndController) {
  CompositionProduct product;
  ASSERT_EQ(compose(fugueRequest(42), &product), CompositionStatus::Ok);
  for (const auto& track : product.composition.tracks) {
    for (std::size_t i = 0; i < track.cc_events.size(); ++i) {
      for (std::size_t j = i + 1; j < track.cc_events.size(); ++j) {
        EXPECT_FALSE(track.cc_events[i].tick == track.cc_events[j].tick &&
                     track.cc_events[i].controller == track.cc_events[j].controller);
      }
    }
  }
}

TEST(CompositionServiceTest, ContrastingSectionAddsFugueTempoChange) {
  CompositionRequest request;
  request.form = FormType::ToccataAndFugue;
  request.character = SubjectCharacter::Severe;
  request.scale = DurationScale::Short;
  request.seed = 42;
  request.bpm = 100;
  CompositionProduct product;
  ASSERT_EQ(compose(request, &product), CompositionStatus::Ok);

  EXPECT_GE(product.tempo_events.size(), 6u);
  EXPECT_TRUE(std::any_of(
      product.tempo_events.begin(), product.tempo_events.end(),
      [&](const TempoEvent& event) { return event.tick > 0 && event.bpm > product.bpm; }));
}

TEST(CompositionServiceTest, HarpsichordUsesRegistrationTerraces) {
  CompositionRequest request;
  request.form = FormType::GoldbergVariations;
  request.character = SubjectCharacter::Severe;
  request.instrument = InstrumentType::Harpsichord;
  request.instrument_specified = true;
  request.scale = DurationScale::Short;
  request.seed = 42;
  request.bpm = 100;
  CompositionProduct product;
  ASSERT_EQ(compose(request, &product), CompositionStatus::Ok);

  std::vector<std::uint8_t> registration_values;
  for (const auto& track : product.composition.tracks) {
    for (const auto& event : track.cc_events) {
      if (event.controller == 7) {
        registration_values.push_back(event.value);
      }
    }
  }
  std::sort(registration_values.begin(), registration_values.end());
  registration_values.erase(std::unique(registration_values.begin(), registration_values.end()),
                            registration_values.end());
  EXPECT_GT(registration_values.size(), 1u);
}

TEST(CompositionServiceTest, PassacagliaRestlessLongSeedTwoCompletesWithoutVoiceCrossing) {
  CompositionRequest request;
  request.form = FormType::Passacaglia;
  request.character = SubjectCharacter::Restless;
  request.scale = DurationScale::Long;
  request.seed = 2;
  CompositionProduct product;
  ASSERT_EQ(compose(request, &product), CompositionStatus::Ok);
  EXPECT_EQ(product.final_validation.status, composer::ValidationStatus::Ok);
  EXPECT_TRUE(product.final_validation.failures.empty());
  EXPECT_FALSE(product.midi_bytes.empty());
}

TEST(CompositionServiceTest, PassacagliaRestlessLongSeedSixCompletesWithoutVoiceCrossing) {
  CompositionRequest request;
  request.form = FormType::Passacaglia;
  request.character = SubjectCharacter::Restless;
  request.scale = DurationScale::Long;
  request.seed = 6;
  CompositionProduct product;
  ASSERT_EQ(compose(request, &product), CompositionStatus::Ok);
  EXPECT_EQ(product.final_validation.status, composer::ValidationStatus::Ok);
  EXPECT_TRUE(product.final_validation.failures.empty());
  EXPECT_FALSE(product.midi_bytes.empty());
}

TEST(CompositionServiceTest, FantasiaRestlessShortSeedSixteenKeepsSectionContrast) {
  CompositionRequest request;
  request.form = FormType::FantasiaAndFugue;
  request.character = SubjectCharacter::Restless;
  request.scale = DurationScale::Short;
  request.seed = 16;
  CompositionProduct product;
  ASSERT_EQ(compose(request, &product), CompositionStatus::Ok);
  EXPECT_EQ(product.final_validation.status, composer::ValidationStatus::Ok);
  EXPECT_TRUE(product.final_validation.failures.empty());
  EXPECT_FALSE(product.midi_bytes.empty());
}

TEST(CompositionServiceTest, CliAdapterProducesByteIdenticalMidi) {
  const std::string path = "/tmp/bach-application-service-cli-test.mid";
  std::remove(path.c_str());
  const std::string command =
      std::string("\"") + BACH_CLI_PATH +
      "\" --form fugue --scale short --seed 405 --key g_minor --instrument organ --bpm 100 -o " +
      path + " >/dev/null 2>/dev/null";
  ASSERT_EQ(std::system(command.c_str()), 0);

  std::ifstream file(path, std::ios::binary);
  ASSERT_TRUE(file.is_open());
  const std::vector<std::uint8_t> cli_bytes((std::istreambuf_iterator<char>(file)),
                                            std::istreambuf_iterator<char>());
  std::remove(path.c_str());

  CompositionProduct direct;
  ASSERT_EQ(compose(fugueRequest(405), &direct), CompositionStatus::Ok);
  EXPECT_EQ(cli_bytes, direct.midi_bytes);
}

TEST(CompositionServiceTest, CliDefaultsMatchSharedServiceDefaults) {
  const std::string path = "/tmp/bach-application-service-cli-defaults.mid";
  std::remove(path.c_str());
  const std::string command =
      std::string("\"") + BACH_CLI_PATH + "\" --seed 407 -o " + path + " >/dev/null 2>/dev/null";
  ASSERT_EQ(std::system(command.c_str()), 0);
  std::ifstream file(path, std::ios::binary);
  ASSERT_TRUE(file.is_open());
  const std::vector<std::uint8_t> cli_bytes((std::istreambuf_iterator<char>(file)),
                                            std::istreambuf_iterator<char>());
  std::remove(path.c_str());

  CompositionRequest request;
  request.seed = 407;
  CompositionProduct direct;
  ASSERT_EQ(compose(request, &direct), CompositionStatus::Ok);
  EXPECT_EQ(cli_bytes, direct.midi_bytes);
  EXPECT_EQ(direct.form, FormType::Fugue);
  EXPECT_EQ(direct.scale, DurationScale::Short);
  EXPECT_EQ(direct.bpm, 100);
}

TEST(CompositionServiceTest, CliJsonSidecarsNeverOverwriteMidiOutput) {
  struct Case {
    const char* output;
    const char* events;
  };
  ASSERT_EQ(std::system("mkdir -p /tmp/bach-cli.v1.2"), 0);
  const std::array<Case, 5> cases = {
      {{"/tmp/bach-cli-events.json", "/tmp/bach-cli-events.json.events.json"},
       {"/tmp/bach-cli-events.mid", "/tmp/bach-cli-events.json"},
       {"/tmp/bach-cli-events", "/tmp/bach-cli-events.json"},
       {"/tmp/bach-cli.v1.2/song.mid", "/tmp/bach-cli.v1.2/song.json"},
       {"/tmp/.bach-cli-events", "/tmp/.bach-cli-events.json"}}};
  for (const Case& test_case : cases) {
    std::remove(test_case.output);
    std::remove(test_case.events);
    const std::string command = std::string("\"") + BACH_CLI_PATH +
                                "\" --form fugue --scale short --seed 405 --json -o " +
                                test_case.output + " >/dev/null 2>/dev/null";
    ASSERT_EQ(std::system(command.c_str()), 0) << test_case.output;

    std::ifstream midi_file(test_case.output, std::ios::binary);
    ASSERT_TRUE(midi_file.is_open()) << test_case.output;
    std::array<char, 4> midi_header = {};
    midi_file.read(midi_header.data(), static_cast<std::streamsize>(midi_header.size()));
    EXPECT_EQ(std::string(midi_header.data(), midi_header.size()), "MThd");

    std::ifstream events_file(test_case.events);
    ASSERT_TRUE(events_file.is_open()) << test_case.events;
    const std::string events((std::istreambuf_iterator<char>(events_file)),
                             std::istreambuf_iterator<char>());
    EXPECT_NE(events.find("\"tracks\":["), std::string::npos);
    const std::size_t final = events.find_last_not_of(" \t\r\n");
    ASSERT_NE(final, std::string::npos);
    EXPECT_EQ(events[final], '}') << "event JSON was truncated or contaminated";

    std::remove(test_case.output);
    std::remove(test_case.events);
  }
  EXPECT_EQ(std::remove("/tmp/bach-cli.v1.2"), 0);
}

}  // namespace
}  // namespace bach::application
