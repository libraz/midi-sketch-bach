#include <gtest/gtest.h>

#include <atomic>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <thread>
#include <vector>

#include "application/composition_service.h"
#include "bach_c.h"
#include "midi/midi_reader.h"

namespace {

class BachHandleOwner {
 public:
  BachHandleOwner() : handle_(bach_create()) {}
  ~BachHandleOwner() { bach_destroy(handle_); }

  BachHandleOwner(const BachHandleOwner&) = delete;
  BachHandleOwner& operator=(const BachHandleOwner&) = delete;

  BachHandle get() const { return handle_; }

 private:
  BachHandle handle_;
};

BachError generate(BachHandle handle, const std::string& json) {
  return bach_generate_from_json(handle, json.data(), json.size());
}

std::string takeJson(BachEventData* data) {
  if (data == nullptr) {
    return {};
  }
  std::string json(data->json, data->length);
  bach_free_events(data);
  return json;
}

std::string validConfig(uint32_t seed) {
  return "{\"form\":\"fugue\",\"character\":\"severe\",\"instrument\":\"organ\","
         "\"scale\":\"short\",\"bpm\":100,\"seed\":" +
         std::to_string(seed) + "}";
}

TEST(BachCApiTest, RejectsNullArgumentsWithoutThrowing) {
  BachHandleOwner owner;
  ASSERT_NE(owner.get(), nullptr);

  EXPECT_EQ(bach_generate_from_json(nullptr, "{}", 2), BACH_ERROR_INVALID_PARAM);
  EXPECT_EQ(bach_generate_from_json(owner.get(), nullptr, 0), BACH_ERROR_INVALID_PARAM);
  EXPECT_EQ(bach_get_info(nullptr), nullptr);
  EXPECT_EQ(bach_get_info(owner.get()), nullptr);
  EXPECT_EQ(bach_get_info_json(nullptr), nullptr);
  EXPECT_EQ(bach_get_info_json(owner.get()), nullptr);
  EXPECT_EQ(bach_get_diagnostic(nullptr), nullptr);
  EXPECT_EQ(bach_get_diagnostic(owner.get()), nullptr);
  EXPECT_EQ(bach_get_generated(nullptr), nullptr);
  EXPECT_EQ(bach_get_generated(owner.get()), nullptr);
  EXPECT_EQ(bach_get_provenance(nullptr), nullptr);
  EXPECT_EQ(bach_get_provenance(owner.get()), nullptr);
}

TEST(BachCApiTest, RejectsMalformedTruncatedNestedAndOversizedJson) {
  BachHandleOwner owner;
  ASSERT_NE(owner.get(), nullptr);

  const std::vector<std::string> invalid = {
      "",
      "{",
      "{\"form\":",
      "{\"form\":\"fugue\"",
      "{} trailing",
      "{\"x\":tru}",
      "{\"x\":\"\\q\"}",
      "{\"x\":{}}",
      "{\"x\":[]}",
      "{\"x\":1,}",
      "{\"x\":1,\"x\":2}",
      "{\"x\":1e400}",
  };
  for (const auto& json : invalid) {
    EXPECT_EQ(generate(owner.get(), json), BACH_ERROR_INVALID_JSON) << json;
  }

  const std::string oversized(64 * 1024 + 1, ' ');
  EXPECT_EQ(generate(owner.get(), oversized), BACH_ERROR_INVALID_JSON);
}

TEST(BachCApiTest, RejectsFractionalNegativeOverflowAndWrongTypedFields) {
  BachHandleOwner owner;
  ASSERT_NE(owner.get(), nullptr);

  for (const auto& json : {"{\"bpm\":100.5}", "{\"bpm\":-1}", "{\"bpm\":65536}"}) {
    EXPECT_EQ(generate(owner.get(), json), BACH_ERROR_INVALID_BPM) << json;
  }
  for (const auto& json : {"{\"seed\":-1}", "{\"seed\":4294967296}", "{\"seed\":1.5}"}) {
    EXPECT_EQ(generate(owner.get(), json), BACH_ERROR_INVALID_SEED) << json;
  }
  for (const auto& json :
       {"{\"target_bars\":-1}", "{\"target_bars\":65536}", "{\"target_bars\":1.5}"}) {
    EXPECT_EQ(generate(owner.get(), json), BACH_ERROR_INVALID_TARGET_BARS) << json;
  }
  EXPECT_EQ(generate(owner.get(), "{\"is_minor\":1}"), BACH_ERROR_INVALID_IS_MINOR);
  for (const auto& json : {"{\"num_voices\":2.5}", "{\"num_voices\":-1}", "{\"num_voices\":256}"}) {
    EXPECT_EQ(generate(owner.get(), json), BACH_ERROR_INVALID_NUM_VOICES) << json;
  }

  EXPECT_EQ(generate(owner.get(), "{\"form\":1.5}"), BACH_ERROR_INVALID_FORM);
  EXPECT_EQ(generate(owner.get(), "{\"form\":10}"), BACH_ERROR_INVALID_FORM);
  EXPECT_EQ(generate(owner.get(), "{\"key\":1.5}"), BACH_ERROR_INVALID_KEY);
  EXPECT_EQ(generate(owner.get(), "{\"key\":12}"), BACH_ERROR_INVALID_KEY);
  EXPECT_EQ(generate(owner.get(), "{\"character\":4}"), BACH_ERROR_INVALID_CHARACTER);
  EXPECT_EQ(generate(owner.get(), "{\"instrument\":6}"), BACH_ERROR_INVALID_INSTRUMENT);
  EXPECT_EQ(generate(owner.get(), "{\"form\":\"fugue\",\"instrument\":\"cello\",\"seed\":42}"),
            BACH_ERROR_INCOMPATIBLE_INSTRUMENT_FORM);
  EXPECT_EQ(generate(owner.get(), "{\"scale\":\"not_a_scale\"}"), BACH_ERROR_INVALID_SCALE);
}

TEST(BachCApiTest, RejectsUnknownConfigurationFields) {
  BachHandleOwner owner;
  ASSERT_NE(owner.get(), nullptr);

  EXPECT_EQ(generate(owner.get(), "{\"tempo\":100}"), BACH_ERROR_UNKNOWN_CONFIG_FIELD);
  EXPECT_EQ(generate(owner.get(), "{\"bars\":16}"), BACH_ERROR_UNKNOWN_CONFIG_FIELD);
  EXPECT_EQ(generate(owner.get(), "{\"comment\":\"test\"}"), BACH_ERROR_UNKNOWN_CONFIG_FIELD);
}

TEST(BachCApiTest, EnumeratedKeyNamesRoundTripThroughGenerate) {
  BachHandleOwner owner;
  ASSERT_NE(owner.get(), nullptr);

  for (uint8_t key_id = 0; key_id < bach_key_count(); ++key_id) {
    const std::string key_name = bach_key_name(key_id);
    EXPECT_EQ(
        generate(owner.get(), "{\"form\":\"fugue\",\"key\":\"" + key_name + "\",\"seed\":42}"),
        BACH_OK)
        << key_name;
  }
}

TEST(BachCApiTest, DeterministicMalformedByteCorpusDoesNotEscapeTheAbi) {
  BachHandleOwner owner;
  ASSERT_NE(owner.get(), nullptr);

  std::vector<std::string> corpus;
  corpus.emplace_back("\0", 1);
  corpus.emplace_back("{\0}", 3);
  corpus.emplace_back("{\"x\":\xff}", 7);
  corpus.emplace_back("[1,2,3]");
  corpus.emplace_back("{\"x\":\"\\uD800\"}");
  corpus.emplace_back("{\"x\":00}");
  corpus.emplace_back("{\"x\":+1}");

  for (const auto& bytes : corpus) {
    EXPECT_NE(generate(owner.get(), bytes), BACH_OK);
  }
}

TEST(BachCApiTest, FailedRegenerationClearsThePublicResultSnapshot) {
  BachHandleOwner owner;
  ASSERT_NE(owner.get(), nullptr);
  ASSERT_EQ(generate(owner.get(), validConfig(101)), BACH_OK);
  EXPECT_EQ(bach_get_diagnostic(owner.get()), nullptr);

  BachMidiData* midi = bach_get_midi(owner.get());
  ASSERT_NE(midi, nullptr);
  EXPECT_GT(midi->size, 0u);
  bach_free_midi(midi);
  ASSERT_GT(bach_get_info(owner.get())->total_ticks, 0u);
  BachEventData* info_json = bach_get_info_json(owner.get());
  ASSERT_NE(info_json, nullptr);
  const std::string info_text(info_json->json, info_json->length);
  EXPECT_NE(info_text.find("\"totalBars\":"), std::string::npos);
  EXPECT_NE(info_text.find("\"seedUsed\":101"), std::string::npos);
  bach_free_events(info_json);

  EXPECT_EQ(generate(owner.get(), "{\"seed\":-1}"), BACH_ERROR_INVALID_SEED);
  EXPECT_EQ(bach_get_midi(owner.get()), nullptr);
  EXPECT_EQ(bach_get_events(owner.get()), nullptr);
  EXPECT_EQ(bach_get_generated(owner.get()), nullptr);
  EXPECT_EQ(bach_get_provenance(owner.get()), nullptr);
  EXPECT_EQ(bach_get_info(owner.get()), nullptr);
  EXPECT_EQ(bach_get_info_json(owner.get()), nullptr);
}

TEST(BachCApiTest, ExposesGeneratedAndProvenanceJsonAfterSuccessfulGeneration) {
  BachHandleOwner owner;
  ASSERT_NE(owner.get(), nullptr);
  ASSERT_EQ(generate(owner.get(), validConfig(333)), BACH_OK);

  const std::string generated = takeJson(bach_get_generated(owner.get()));
  const std::string provenance = takeJson(bach_get_provenance(owner.get()));
  EXPECT_NE(generated.find("\"schema_version\":\"generated.v1\""), std::string::npos);
  EXPECT_NE(provenance.find("\"schema_version\":\"provenance.v1\""), std::string::npos);
  EXPECT_NE(provenance.find("\"satisfied_rules\":\""), std::string::npos);
}

TEST(BachCApiTest, InfoStorageIsStableAndIndependentForDistinctHandles) {
  BachHandleOwner first;
  BachHandleOwner second;
  ASSERT_NE(first.get(), nullptr);
  ASSERT_NE(second.get(), nullptr);
  ASSERT_EQ(generate(first.get(), validConfig(201)), BACH_OK);

  const BachInfo* first_info = bach_get_info(first.get());
  ASSERT_NE(first_info, nullptr);
  const BachInfo first_snapshot = *first_info;
  ASSERT_EQ(first_snapshot.seed_used, 201u);

  ASSERT_EQ(generate(second.get(), validConfig(202)), BACH_OK);
  const BachInfo* second_info = bach_get_info(second.get());
  ASSERT_NE(second_info, nullptr);
  EXPECT_NE(first_info, second_info);
  EXPECT_EQ(second_info->seed_used, 202u);
  EXPECT_EQ(first_info, bach_get_info(first.get()));
  EXPECT_EQ(first_info->seed_used, first_snapshot.seed_used);
  EXPECT_EQ(first_info->total_ticks, first_snapshot.total_ticks);

  std::atomic<bool> consistent{true};
  auto read_repeatedly = [&consistent](BachHandle handle, uint32_t expected_seed) {
    for (int i = 0; i < 10000; ++i) {
      const BachInfo* info = bach_get_info(handle);
      if (!info || info->seed_used != expected_seed || info->total_ticks == 0) {
        consistent.store(false, std::memory_order_relaxed);
        return;
      }
    }
  };
  std::thread first_reader(read_repeatedly, first.get(), 201u);
  std::thread second_reader(read_repeatedly, second.get(), 202u);
  first_reader.join();
  second_reader.join();
  EXPECT_TRUE(consistent.load(std::memory_order_relaxed));
}

TEST(BachCApiTest, RequestedTonicAndModeReachMidiKeySignatureMetadata) {
  BachHandleOwner owner;
  ASSERT_NE(owner.get(), nullptr);
  const std::string json =
      "{\"form\":\"fugue\",\"character\":\"severe\",\"instrument\":\"organ\","
      "\"scale\":\"short\",\"bpm\":100,\"seed\":303,\"key\":7,\"is_minor\":true}";
  ASSERT_EQ(generate(owner.get(), json), BACH_OK);

  BachMidiData* midi = bach_get_midi(owner.get());
  ASSERT_NE(midi, nullptr);
  const std::vector<uint8_t> bytes(midi->data, midi->data + midi->size);
  bach_free_midi(midi);

  bach::MidiReader reader;
  ASSERT_TRUE(reader.read(bytes)) << reader.getError();
  ASSERT_TRUE(reader.getParsedMidi().has_key_signature);
  EXPECT_EQ(reader.getParsedMidi().key_signature.tonic, bach::Key::G);
  EXPECT_TRUE(reader.getParsedMidi().key_signature.is_minor);
}

TEST(BachCApiTest, EnumeratedCharacterNamesRoundTripThroughGenerate) {
  for (uint8_t id = 0; id < bach_character_count(); ++id) {
    BachHandleOwner owner;
    ASSERT_NE(owner.get(), nullptr);
    const std::string json = std::string("{\"form\":\"fugue\",\"character\":\"") +
                             bach_character_name(id) +
                             "\",\"instrument\":\"organ\",\"scale\":\"short\","
                             "\"bpm\":100,\"seed\":303}";
    EXPECT_EQ(generate(owner.get(), json), BACH_OK) << bach_character_name(id);
  }
}

TEST(BachCApiTest, CAdapterMatchesSharedCompositionServiceBytes) {
  BachHandleOwner owner;
  ASSERT_NE(owner.get(), nullptr);
  const std::string json =
      "{\"form\":\"fugue\",\"character\":\"severe\",\"instrument\":\"organ\","
      "\"scale\":\"short\",\"bpm\":100,\"seed\":406,\"key\":7,\"is_minor\":true}";
  ASSERT_EQ(generate(owner.get(), json), BACH_OK);
  BachMidiData* midi = bach_get_midi(owner.get());
  ASSERT_NE(midi, nullptr);
  const std::vector<uint8_t> c_bytes(midi->data, midi->data + midi->size);
  bach_free_midi(midi);
  BachEventData* events = bach_get_events(owner.get());
  ASSERT_NE(events, nullptr);
  const std::string c_events(events->json, events->length);
  bach_free_events(events);

  bach::application::CompositionRequest request;
  request.form = bach::FormType::Fugue;
  request.key = {bach::Key::G, true};
  request.character = bach::SubjectCharacter::Severe;
  request.instrument = bach::InstrumentType::Organ;
  request.instrument_specified = true;
  request.scale = bach::DurationScale::Short;
  request.bpm = 100;
  request.seed = 406;
  bach::application::CompositionProduct direct;
  ASSERT_EQ(bach::application::compose(request, &direct), bach::application::CompositionStatus::Ok);
  EXPECT_EQ(c_bytes, direct.midi_bytes);
  EXPECT_EQ(c_events, direct.homepage_events_json);
}

TEST(BachCApiTest, DefaultConfigurationMatchesSharedServiceDefaults) {
  BachHandleOwner owner;
  ASSERT_NE(owner.get(), nullptr);
  ASSERT_EQ(generate(owner.get(), "{\"seed\":407}"), BACH_OK);
  BachMidiData* midi = bach_get_midi(owner.get());
  ASSERT_NE(midi, nullptr);
  const std::vector<uint8_t> c_bytes(midi->data, midi->data + midi->size);
  bach_free_midi(midi);

  bach::application::CompositionRequest request;
  request.seed = 407;
  bach::application::CompositionProduct direct;
  ASSERT_EQ(bach::application::compose(request, &direct), bach::application::CompositionStatus::Ok);
  EXPECT_EQ(c_bytes, direct.midi_bytes);
  EXPECT_EQ(direct.form, bach::FormType::Fugue);
  EXPECT_EQ(direct.scale, bach::DurationScale::Short);
  EXPECT_EQ(direct.bpm, 100);
}

}  // namespace
