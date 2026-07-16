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
  EXPECT_EQ(bach_get_diagnostic(nullptr), nullptr);
  EXPECT_EQ(bach_get_diagnostic(owner.get()), nullptr);
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
    EXPECT_EQ(generate(owner.get(), json), BACH_ERROR_INVALID_CONFIG) << json;
  }

  const std::string oversized(64 * 1024 + 1, ' ');
  EXPECT_EQ(generate(owner.get(), oversized), BACH_ERROR_INVALID_CONFIG);
}

TEST(BachCApiTest, RejectsFractionalNegativeOverflowAndWrongTypedFields) {
  BachHandleOwner owner;
  ASSERT_NE(owner.get(), nullptr);

  const std::vector<std::string> invalid_config = {
      "{\"bpm\":100.5}",       "{\"bpm\":-1}",
      "{\"bpm\":65536}",       "{\"seed\":-1}",
      "{\"seed\":4294967296}", "{\"seed\":1.5}",
      "{\"target_bars\":-1}",  "{\"target_bars\":65536}",
      "{\"target_bars\":1.5}", "{\"num_voices\":2.5}",
      "{\"is_minor\":1}",
  };
  for (const auto& json : invalid_config) {
    EXPECT_EQ(generate(owner.get(), json), BACH_ERROR_INVALID_CONFIG) << json;
  }

  EXPECT_EQ(generate(owner.get(), "{\"form\":1.5}"), BACH_ERROR_INVALID_FORM);
  EXPECT_EQ(generate(owner.get(), "{\"form\":10}"), BACH_ERROR_INVALID_FORM);
  EXPECT_EQ(generate(owner.get(), "{\"key\":1.5}"), BACH_ERROR_INVALID_KEY);
  EXPECT_EQ(generate(owner.get(), "{\"key\":12}"), BACH_ERROR_INVALID_KEY);
  EXPECT_EQ(generate(owner.get(), "{\"character\":4}"), BACH_ERROR_INVALID_CHARACTER);
  EXPECT_EQ(generate(owner.get(), "{\"instrument\":6}"), BACH_ERROR_INVALID_INSTRUMENT);
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

  EXPECT_EQ(generate(owner.get(), "{\"seed\":-1}"), BACH_ERROR_INVALID_CONFIG);
  EXPECT_EQ(bach_get_midi(owner.get()), nullptr);
  EXPECT_EQ(bach_get_events(owner.get()), nullptr);
  const BachInfo* info = bach_get_info(owner.get());
  ASSERT_NE(info, nullptr);
  EXPECT_EQ(info->total_ticks, 0u);
  EXPECT_EQ(info->total_bars, 0u);
  EXPECT_EQ(info->track_count, 0u);
  EXPECT_EQ(info->seed_used, 0u);
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
}

}  // namespace
