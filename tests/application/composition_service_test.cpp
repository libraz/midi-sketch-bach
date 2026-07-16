#include "application/composition_service.h"

#include <gtest/gtest.h>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

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

TEST(CompositionServiceTest, RejectsInvalidArgumentsByStatus) {
  CompositionRequest request;
  EXPECT_EQ(compose(request, nullptr), CompositionStatus::InvalidArgument);
  request.bpm = 0;
  CompositionProduct product;
  product.midi_bytes.push_back(1);
  EXPECT_EQ(compose(request, &product), CompositionStatus::InvalidArgument);
  EXPECT_TRUE(product.midi_bytes.empty());
}

TEST(CompositionServiceTest, ResolvesInstrumentAndFormSpecificPerformanceProfiles) {
  const auto organ_fugue = resolvePerformanceProfile(FormType::Fugue, InstrumentType::Organ);
  EXPECT_TRUE(organ_fugue.registration_terraces);
  EXPECT_FALSE(organ_fugue.continuous_expression);
  EXPECT_EQ(organ_fugue.final_ritardando, composer::RitardandoStyle::None);

  const auto organ_toccata =
      resolvePerformanceProfile(FormType::ToccataAndFugue, InstrumentType::Organ);
  EXPECT_EQ(organ_toccata.final_ritardando, composer::RitardandoStyle::Rhetorical);

  const auto cello = resolvePerformanceProfile(FormType::CelloPrelude, InstrumentType::Cello);
  EXPECT_FALSE(cello.registration_terraces);
  EXPECT_TRUE(cello.continuous_expression);
  EXPECT_EQ(cello.final_ritardando, composer::RitardandoStyle::Gentle);

  const auto harpsichord =
      resolvePerformanceProfile(FormType::GoldbergVariations, InstrumentType::Harpsichord);
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
  EXPECT_NE(product.generated_json.find("\"schema_version\":\"generated.v1\""), std::string::npos);
  EXPECT_NE(product.provenance_json.find("\"schema_version\":\"provenance.v1\""),
            std::string::npos);
  EXPECT_TRUE(product.diagnostic_json.empty());
  EXPECT_EQ(product.tempo_events.size(), 1u);
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

TEST(CompositionServiceTest, RejectsIncompatibleFormCharacterBeforeGeneration) {
  CompositionRequest request;
  request.form = FormType::ChoralePrelude;
  request.character = SubjectCharacter::Playful;
  request.bpm = 100;
  CompositionProduct product;
  EXPECT_EQ(compose(request, &product), CompositionStatus::IncompatibleCharacter);
  EXPECT_TRUE(product.midi_bytes.empty());
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

}  // namespace
}  // namespace bach::application
