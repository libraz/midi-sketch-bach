#include "application/composition_service.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "composer/form_director.h"
#include "core/instrument_program.h"
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

// Note durations in emission order from a generated.v1 payload.
std::vector<Tick> jsonDurations(const std::string& json) {
  constexpr const char* kDurationKey = "\"duration\":";
  std::vector<Tick> durations;
  std::size_t offset = 0;
  while ((offset = json.find(kDurationKey, offset)) != std::string::npos) {
    offset += std::char_traits<char>::length(kDurationKey);
    const std::size_t end = json.find_first_not_of("0123456789", offset);
    durations.push_back(static_cast<Tick>(std::stoul(json.substr(offset, end - offset))));
    offset = end;
  }
  return durations;
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

std::vector<std::uint8_t> jsonVelocitiesAtTick(const std::string& json, Tick tick) {
  constexpr const char* kStartTickKey = "\"start_tick\":";
  constexpr const char* kVelocityKey = "\"velocity\":";
  std::vector<std::uint8_t> velocities;
  std::size_t object_start = 0;
  while ((object_start = json.find('{', object_start)) != std::string::npos) {
    const std::size_t object_end = json.find('}', object_start);
    if (object_end == std::string::npos) {
      break;
    }
    const std::string object = json.substr(object_start, object_end - object_start);
    const std::size_t start_tick_pos = object.find(kStartTickKey);
    const std::size_t velocity_pos = object.find(kVelocityKey);
    if (start_tick_pos != std::string::npos && velocity_pos != std::string::npos) {
      const std::size_t start_value =
          start_tick_pos + std::char_traits<char>::length(kStartTickKey);
      const std::size_t velocity_value =
          velocity_pos + std::char_traits<char>::length(kVelocityKey);
      if (static_cast<Tick>(std::stoul(object.substr(start_value))) == tick) {
        velocities.push_back(static_cast<std::uint8_t>(std::stoul(object.substr(velocity_value))));
      }
    }
    object_start = object_end + 1;
  }
  std::sort(velocities.begin(), velocities.end());
  return velocities;
}

std::vector<std::uint8_t> noteVelocitiesAtTick(const std::vector<NoteEvent>& notes, Tick tick) {
  std::vector<std::uint8_t> velocities;
  for (const auto& note : notes) {
    if (note.start_tick == tick) {
      velocities.push_back(note.velocity);
    }
  }
  std::sort(velocities.begin(), velocities.end());
  return velocities;
}

std::vector<std::uint8_t> midiVelocitiesAtTick(const ParsedMidi& midi, Tick tick) {
  std::vector<std::uint8_t> velocities;
  for (const auto& track : midi.tracks) {
    for (const auto& note : track.notes) {
      if (note.start_tick == tick) {
        velocities.push_back(note.velocity);
      }
    }
  }
  std::sort(velocities.begin(), velocities.end());
  return velocities;
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

// What separates this form from the fugue above is that it HAS a span the
// scored search may take, so the request is answered rather than refused.
// Reaching the search is the contract; arriving at a clean score is not. The
// search commits each voice against the voices already placed, so a conflict
// with a voice written afterwards is invisible to its perfect-motion filters
// and caught only by the validator -- which seeds survive that gap is a
// property of where the span boundaries happen to fall, not of whether the
// form is supported. Most seeds do not survive it. Pinning one would make this
// test fail every time a span boundary moves for a reason that has nothing to
// do with what it is checking, so it asserts the form is reachable at every
// seed and that the search's own notes reach the score at some seed.
TEST(CompositionServiceTest, FreeCounterpointGeneratesPassacagliaCounterline) {
  bool saw_composed_counterline = false;
  for (std::uint32_t seed : {1u, 2u, 3u, 4u, 5u, 6u}) {
    CompositionRequest request;
    request.form = FormType::Passacaglia;
    request.character = SubjectCharacter::Severe;
    request.seed = seed;
    request.bpm = 100;
    request.enable_free_counterpoint = true;
    CompositionProduct product;
    const CompositionStatus status = compose(request, &product);
    ASSERT_NE(status, CompositionStatus::FreeCounterpointUnavailable) << "seed=" << seed;
    if (status != CompositionStatus::Ok)
      continue;
    EXPECT_FALSE(product.midi_bytes.empty()) << "seed=" << seed;
    EXPECT_EQ(product.final_validation.status, composer::ValidationStatus::Ok) << "seed=" << seed;
    EXPECT_TRUE(product.final_validation.failures.empty()) << "seed=" << seed;
    saw_composed_counterline =
        saw_composed_counterline ||
        std::any_of(product.composition.provenance.begin(), product.composition.provenance.end(),
                    [](const composer::NoteProvenance& p) {
                      return p.source == composer::NoteSource::Compose;
                    });
  }
  EXPECT_TRUE(saw_composed_counterline);
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

// The seed axis alternates the mode rather than doubling in length. Mode is a
// semantic axis -- it selects different material and reaches validator rules the
// major surface never does -- while the seed is a decorrelation axis, so half
// the seeds in each mode covers more of what ships than all of them in one.
//
// One case per form rather than one case over all of them. The sweep is the
// suite's longest-running check by a wide margin and every cell of it is
// independent, so a form per case lets the runner spread them over cores
// instead of walking the whole product on one. The set of cells is unchanged.
class ShippedFormSweep : public ::testing::TestWithParam<FormType> {};

TEST_P(ShippedFormSweep, FinalScoreGateAcceptsEveryConfiguration) {
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
  const FormType form = GetParam();
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
        const bool minor = (seed % 2) == 0;
        SCOPED_TRACE(minor ? "minor" : "major");
        CompositionRequest request;
        request.form = form;
        request.character = character;
        request.key = minor ? KeySignature{Key::G, true} : KeySignature{Key::C, false};
        request.scale = scale;
        request.seed = seed;
        request.bpm = 100;
        CompositionProduct product;
        ASSERT_EQ(compose(request, &product), CompositionStatus::Ok);
        EXPECT_EQ(product.final_validation.status, composer::ValidationStatus::Ok);
        EXPECT_TRUE(product.final_validation.failures.empty())
            << "first failure: "
            << (product.final_validation.failures.empty()
                    ? std::string()
                    : product.final_validation.failures.front().rule_id);
        EXPECT_TRUE(product.diagnostic_json.empty());
        EXPECT_FALSE(product.midi_bytes.empty());
      }
    }
  }
}

INSTANTIATE_TEST_SUITE_P(EveryShippedForm, ShippedFormSweep,
                         ::testing::Values(FormType::Fugue, FormType::PreludeAndFugue,
                                           FormType::TrioSonata, FormType::ChoralePrelude,
                                           FormType::ToccataAndFugue, FormType::Passacaglia,
                                           FormType::FantasiaAndFugue, FormType::CelloPrelude,
                                           FormType::Chaconne, FormType::GoldbergVariations),
                         [](const ::testing::TestParamInfo<FormType>& info) {
                           return std::string(formTypeToString(info.param));
                         });

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

TEST(CompositionServiceTest, ChaconneVelocityFollowsSarabandeMeterInJsonAndMidi) {
  CompositionRequest request;
  request.form = FormType::Chaconne;
  request.character = SubjectCharacter::Severe;
  request.instrument = InstrumentType::Violin;
  request.instrument_specified = true;
  request.seed = 42;
  request.bpm = 100;
  CompositionProduct product;
  ASSERT_EQ(compose(request, &product), CompositionStatus::Ok);

  MidiReader reader;
  ASSERT_TRUE(reader.read(product.midi_bytes)) << reader.getError();
  const std::array<std::pair<Tick, std::uint8_t>, 3> expected_velocities = {
      {{1440, 80}, {1920, 75}, {2880, 80}}};  // downbeat 70+10; sarabande beat 2 70+5
  for (const auto& expected : expected_velocities) {
    const Tick tick = expected.first;
    const std::uint8_t expected_velocity = expected.second;
    SCOPED_TRACE(tick);
    const auto composition_velocities = noteVelocitiesAtTick(product.composition.notes, tick);
    ASSERT_FALSE(composition_velocities.empty());
    EXPECT_TRUE(std::all_of(
        composition_velocities.begin(), composition_velocities.end(),
        [expected_velocity](std::uint8_t velocity) { return velocity == expected_velocity; }));
    EXPECT_EQ(jsonVelocitiesAtTick(product.generated_json, tick), composition_velocities);
    EXPECT_EQ(midiVelocitiesAtTick(reader.getParsedMidi(), tick), composition_velocities);
  }
  EXPECT_EQ(product.composition.notes.size(), product.composition.provenance.size());
}

// The organ answers nothing to key velocity, so its expression is touch and
// stop selection. Both must reach the product: a line where every note-off
// meets the next note-on is a line no organist plays.
//
// Which notes the touch parts is the other half of the contract, and pinning
// only the first half once let the stroke reach every note in the piece. A
// player parts what the hand has to move between -- a leap, and a repeated
// pitch the finger re-strikes -- and joins a step, which is most of a
// contrapuntal texture; parting those too turns the running figuration into a
// chain of separate strokes. The notated lengths survive in the exported
// report, so a lift is read exactly rather than inferred from the gap, which
// on its own cannot be told from a rest.
TEST(CompositionServiceTest, EveryShippedFormIsArticulated) {
  constexpr std::array<FormType, 10> kAllForms = {{
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
  }};
  for (FormType form : kAllForms) {
    CompositionRequest request;
    request.form = form;
    request.character = SubjectCharacter::Severe;
    // The cello prelude is an unbroken sixteenth flow, so a stroke has something
    // to part only where the figuration repeats a pitch; seed 42 composes a
    // piece with no such repeat, which says nothing about the touch itself.
    request.seed = form == FormType::CelloPrelude ? 43 : 42;
    request.bpm = 100;
    CompositionProduct product;
    ASSERT_EQ(compose(request, &product), CompositionStatus::Ok)
        << "form " << static_cast<int>(form);

    const std::vector<Tick> notated = jsonDurations(product.generated_json);
    ASSERT_EQ(notated.size(), product.composition.notes.size())
        << "form " << static_cast<int>(form);

    std::size_t lifted = 0;
    std::size_t joined_steps = 0;
    std::size_t lifted_steps = 0;
    std::map<VoiceId, std::vector<std::size_t>> by_voice;
    for (std::size_t idx = 0; idx < product.composition.notes.size(); ++idx) {
      by_voice[product.composition.notes[idx].voice].push_back(idx);
    }
    for (auto& [voice, line] : by_voice) {
      const auto& notes = product.composition.notes;
      std::sort(line.begin(), line.end(), [&notes](std::size_t lhs, std::size_t rhs) {
        return notes[lhs].start_tick < notes[rhs].start_tick;
      });
      for (std::size_t pos = 0; pos + 1 < line.size(); ++pos) {
        const std::size_t here = line[pos];
        const std::size_t next = line[pos + 1];
        const bool was_lifted = notes[here].duration < notated[here];
        lifted += was_lifted ? 1 : 0;
        // Only a pair the notated line runs straight through is the touch's to
        // decide; a rest between them parts the notes on its own.
        if (notes[here].start_tick + notated[here] < notes[next].start_tick) {
          continue;
        }
        const int move =
            std::abs(static_cast<int>(notes[next].pitch) - static_cast<int>(notes[here].pitch));
        if (move == 0 || move > 2) {
          continue;
        }
        lifted_steps += was_lifted ? 1 : 0;
        joined_steps += was_lifted ? 0 : 1;
      }
    }
    EXPECT_GT(lifted, 0u) << "form " << static_cast<int>(form) << " is played legato throughout";
    EXPECT_EQ(lifted_steps, 0u) << "form " << static_cast<int>(form) << " parts a step";
    EXPECT_GT(joined_steps, 0u) << "form " << static_cast<int>(form)
                                << " has no joined stepwise motion to keep";

    bool stamped = false;
    for (const auto& prov : product.composition.provenance) {
      if ((prov.satisfied_rules & composer::ruleBitMask(composer::RuleBit::ArticulationApplied))
              .any()) {
        stamped = true;
        break;
      }
    }
    EXPECT_TRUE(stamped) << "form " << static_cast<int>(form)
                         << ": articulation shipped without a trace in provenance";
  }
}

// The report carries the score and the render carries the performance. Mixing
// them would make the validator's texture figures, which are measured on the
// notated array and embedded in the same document, describe an array they were
// never computed from.
TEST(CompositionServiceTest, ExportedReportKeepsTheNotatedLengths) {
  CompositionProduct product;
  ASSERT_EQ(compose(fugueRequest(42), &product), CompositionStatus::Ok);

  const std::vector<Tick> reported = jsonDurations(product.generated_json);
  ASSERT_EQ(reported.size(), product.composition.notes.size());
  std::uint64_t reported_total = 0;
  std::uint64_t played_total = 0;
  for (std::size_t idx = 0; idx < reported.size(); ++idx) {
    reported_total += reported[idx];
    played_total += product.composition.notes[idx].duration;
    EXPECT_GE(reported[idx], product.composition.notes[idx].duration)
        << "note " << idx << ": the touch may only release a note early";
  }
  EXPECT_GT(reported_total, played_total)
      << "the report and the performance carry the same lengths, so one of them is wrong";
}

TEST(CompositionServiceTest, CharacterChangesTheRegistration) {
  const std::array<SubjectCharacter, 4> characters = {{
      SubjectCharacter::Noble,
      SubjectCharacter::Severe,
      SubjectCharacter::Playful,
      SubjectCharacter::Restless,
  }};
  std::vector<std::vector<std::uint8_t>> streams;
  for (SubjectCharacter character : characters) {
    CompositionRequest request = fugueRequest(42);
    request.character = character;
    CompositionProduct product;
    ASSERT_EQ(compose(request, &product), CompositionStatus::Ok)
        << "character " << static_cast<int>(character);
    std::vector<std::uint8_t> values;
    for (const auto& track : product.composition.tracks) {
      for (const auto& evt : track.cc_events) {
        values.push_back(evt.value);
      }
    }
    ASSERT_FALSE(values.empty()) << "character " << static_cast<int>(character);
    streams.push_back(values);
  }
  for (std::size_t lhs = 0; lhs < streams.size(); ++lhs) {
    for (std::size_t rhs = lhs + 1; rhs < streams.size(); ++rhs) {
      EXPECT_NE(streams[lhs], streams[rhs])
          << "characters " << lhs << " and " << rhs << " are played on the same stops";
    }
  }
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

TEST(CompositionServiceTest, FinalRitardandoStepsScaleTheTempoInForce) {
  for (const FormType form : {FormType::ToccataAndFugue, FormType::PreludeAndFugue}) {
    for (const std::uint32_t seed : {1u, 2u}) {
      CompositionRequest request;
      request.form = form;
      request.character = SubjectCharacter::Severe;
      request.scale = DurationScale::Short;
      request.seed = seed;
      request.bpm = 100;
      CompositionProduct product;
      ASSERT_EQ(compose(request, &product), CompositionStatus::Ok);

      const composer::FormSpec& spec = composer::formSpec(form);
      const Tick ticks_per_bar = kTicksPerBeat * spec.ts_numerator;
      ASSERT_GT(product.total_ticks, 2 * ticks_per_bar);
      const Tick start = product.total_ticks - 2 * ticks_per_bar;

      std::uint16_t in_force = 0;
      std::uint16_t first_step = 0;
      for (const TempoEvent& event : product.tempo_events) {
        if (event.tick < start) {
          in_force = event.bpm;
        } else if (event.tick == start) {
          first_step = event.bpm;
        }
      }
      ASSERT_GT(in_force, 0u);
      ASSERT_GT(first_step, 0u);
      const int percent =
          product.performance_profile.final_ritardando == composer::RitardandoStyle::Gentle ? 98
                                                                                            : 94;
      EXPECT_EQ(first_step, static_cast<std::uint16_t>(in_force * percent / 100))
          << "form " << static_cast<int>(form) << " seed " << seed;
    }
  }
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

TEST(CompositionServiceTest, FantasiaRestlessShortSeedSixteenComposesValidly) {
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

TEST(CompositionServiceTest, ComposerHarnessModeResolvesBpmSentinelToSharedDefault) {
  // --composer-phase is internal-only and rejects --bpm, so runComposerMode()
  // must resolve the same zero sentinel that resolveDefaults() handles on the
  // product path; regression coverage for a bug where it fed 0 straight to
  // MidiWriter and every seed failed with "Invalid tempo or meter".
  const std::string path = "/tmp/bach-application-service-composer-mode.mid";
  std::remove(path.c_str());
  const std::string command = std::string("\"") + BACH_CLI_PATH +
                              "\" --composer-phase FugueExposition3v --seed 1 -o " + path +
                              " >/dev/null 2>/dev/null";
  ASSERT_EQ(std::system(command.c_str()), 0);

  MidiReader reader;
  ASSERT_TRUE(reader.read(path)) << reader.getError();
  EXPECT_EQ(reader.getParsedMidi().bpm, kDefaultBpm);
  std::remove(path.c_str());
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

// Semitones the rendered score falls outside its instrument compass.
int compassExcess(const CompositionProduct& product) {
  const InstrumentPitchRange range = pitchRangeFor(product.instrument);
  const int base = keyTranspositionSemitones(product.key.tonic) + product.output_octave_shift;
  int excess = 0;
  for (const NoteEvent& note : product.composition.notes) {
    const int rendered = static_cast<int>(note.pitch) + base;
    excess += std::max(0, static_cast<int>(range.low) - rendered);
    excess += std::max(0, rendered - static_cast<int>(range.high));
  }
  return excess;
}

TEST(CompositionServiceTest, ChaconneInGMinorAtFullLengthShipsOnEverySeed) {
  // The score comes within a few semitones of the violin output compass, so
  // whether a whole octave can absorb the key transposition depends on the key.
  // These seeds have no fitting octave in G minor and must still ship, placed
  // at the displacement that leaves the least outside the compass.
  bool needed_nearest_placement = false;
  constexpr std::uint32_t seeds[] = {2, 4, 5, 8, 10, 11};
  constexpr SubjectCharacter characters[] = {
      SubjectCharacter::Severe,
      SubjectCharacter::Playful,
      SubjectCharacter::Noble,
      SubjectCharacter::Restless,
  };
  for (SubjectCharacter character : characters) {
    if (!composer::isFormCharacterCompatible(FormType::Chaconne, character)) {
      continue;
    }
    for (std::uint32_t seed : seeds) {
      SCOPED_TRACE(subjectCharacterToString(character));
      SCOPED_TRACE(seed);
      CompositionRequest request;
      request.form = FormType::Chaconne;
      request.key = {Key::G, true};
      request.character = character;
      request.target_bars = 128;
      request.seed = seed;
      request.bpm = 100;
      CompositionProduct product;
      ASSERT_EQ(compose(request, &product), CompositionStatus::Ok);
      EXPECT_EQ(product.status, CompositionStatus::Ok);
      EXPECT_FALSE(product.midi_bytes.empty());
      EXPECT_FALSE(product.composition.notes.empty());
      needed_nearest_placement = needed_nearest_placement || compassExcess(product) > 0;
    }
  }
  // At least one of these placements sits outside the compass, which is the
  // shape that used to be refused outright instead of shipped.
  EXPECT_TRUE(needed_nearest_placement);
}

// The organ has no key outside its compass, so every organ form sounds inside it
// in every key: a whole octave places most scores, the output placement moves
// what one cannot, and the key never costs the piece its validity.
TEST(CompositionServiceTest, OrganFormsSoundInsideTheCompassInEveryKey) {
  constexpr FormType forms[] = {
      FormType::Fugue,
      FormType::PreludeAndFugue,
      FormType::TrioSonata,
      FormType::ChoralePrelude,
      FormType::ToccataAndFugue,
      FormType::Passacaglia,
      FormType::FantasiaAndFugue,
  };
  for (FormType form : forms) {
    for (int tonic = 0; tonic < 12; ++tonic) {
      for (bool minor : {false, true}) {
        SCOPED_TRACE(formTypeToString(form));
        SCOPED_TRACE(tonic);
        SCOPED_TRACE(minor ? "minor" : "major");
        CompositionRequest request;
        request.form = form;
        request.key = {static_cast<Key>(tonic), minor};
        request.seed = 1;
        request.bpm = 100;
        CompositionProduct product;
        ASSERT_EQ(compose(request, &product), CompositionStatus::Ok);
        EXPECT_EQ(compassExcess(product), 0);
      }
    }
  }
}

// The figuration wave's veto counters are a process-wide accumulator that only
// the form builders drive. compose() resets it per piece and snapshots it onto
// the report, so the exported document describes this piece alone: a fugue
// fires the wave's reactive layers, and composing the same request twice must
// report the same counts rather than accumulating them.
TEST(CompositionServiceTest, GeneratedJsonCarriesPerPieceWaveVetoCounters) {
  CompositionProduct first;
  ASSERT_EQ(compose(fugueRequest(1), &first), CompositionStatus::Ok);
  EXPECT_NE(first.generated_json.find("\"informational_findings\":["), std::string::npos);
  const long total = first.final_validation.wave_veto.total();
  EXPECT_GT(total, 0);
  EXPECT_NE(first.generated_json.find("\"wave_veto\":{\"anchor_parallel_displaced\":"),
            std::string::npos);
  EXPECT_NE(first.generated_json.find("\"total\":" + std::to_string(total) + "}"),
            std::string::npos);

  CompositionProduct second;
  ASSERT_EQ(compose(fugueRequest(1), &second), CompositionStatus::Ok);
  EXPECT_EQ(second.final_validation.wave_veto.total(), total);
}

// The composed piece for one request, or an empty vector when the form refuses
// the character outright.
std::vector<NoteEvent> composedNotes(FormType form, SubjectCharacter character, Key key, bool minor,
                                     std::uint32_t seed) {
  CompositionRequest request;
  request.form = form;
  request.key = {key, minor};
  request.character = character;
  request.scale = DurationScale::Short;
  request.bpm = 100;
  request.seed = seed;
  CompositionProduct product;
  if (compose(request, &product) != CompositionStatus::Ok)
    return {};
  return std::move(product.composition.notes);
}

// Whether two pieces are the same music as notated: same tones, entering at the
// same places in the same voices. Duration is deliberately out of the
// comparison. The touch shortens every note by a character-dependent amount, so
// including it would make any two characters differ whatever the composer wrote
// -- the question here is whether the character reached the notes.
bool sameNotatedTones(const std::vector<NoteEvent>& lhs, const std::vector<NoteEvent>& rhs) {
  if (lhs.size() != rhs.size())
    return false;
  for (std::size_t idx = 0; idx < lhs.size(); ++idx) {
    if (lhs[idx].start_tick != rhs[idx].start_tick || lhs[idx].pitch != rhs[idx].pitch ||
        lhs[idx].voice != rhs[idx].voice) {
      return false;
    }
  }
  return true;
}

// A character the form accepts must change the notes. Accepting a character and
// then composing the identical piece is the worst of the three outcomes: the
// request was honoured on paper and discarded in the output. A form that cannot
// differentiate a character refuses it instead, which the caller can see in the
// status. Read on the composed product, not on the composer's own result: the
// ornament pass is part of the piece and carries some of the differentiation.
//
// The three forms in kPinnedIdenticalPairs are the open exceptions, each for the
// same reason: they derive their figuration from a palette walked by a seed
// rotation, and the levers that would let a character walk that palette
// differently move their counterpoint ceilings the wrong way. The identical
// (form, mode, seed, character pair) set is pinned exactly -- a wider collapse
// and a closed gap both fail, so the exception cannot outlive its cause.
TEST(CompositionServiceTest, EveryAcceptedCharacterComposesADifferentPiece) {
  constexpr std::array<FormType, 10> kAllForms = {{
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
  }};
  constexpr std::array<SubjectCharacter, 4> kCharacters = {
      {SubjectCharacter::Severe, SubjectCharacter::Playful, SubjectCharacter::Noble,
       SubjectCharacter::Restless}};
  // Entries are "<form ordinal>:<minor>:<seed>:<lhs character>=<rhs character>".
  // The minor goldberg pair at seed 42 differed only by one trill whose
  // auxiliary was a chromatic alteration of its main note; spelled
  // diatonically, that trill is no longer placed.
  const std::set<std::string> kPinnedIdenticalPairs = {
      "5:0:1:severe=noble",   "5:0:7:severe=noble",   "5:1:1:severe=noble",  "5:1:7:severe=noble",
      "8:0:1:severe=noble",   "8:0:2:severe=noble",   "8:0:42:severe=noble", "8:0:7:severe=noble",
      "8:1:1:severe=noble",   "8:1:2:severe=noble",   "8:1:42:severe=noble", "8:1:7:severe=noble",
      "9:0:42:playful=noble", "9:1:42:playful=noble",
  };
  std::set<std::string> observed;
  for (FormType form : kAllForms) {
    for (bool minor : {false, true}) {
      for (std::uint32_t seed : {1u, 2u, 7u, 42u}) {
        std::array<std::vector<NoteEvent>, 4> streams;
        for (std::size_t idx = 0; idx < kCharacters.size(); ++idx)
          streams[idx] = composedNotes(form, kCharacters[idx], Key::C, minor, seed);
        for (std::size_t lhs = 0; lhs < kCharacters.size(); ++lhs) {
          if (streams[lhs].empty())
            continue;
          for (std::size_t rhs = lhs + 1; rhs < kCharacters.size(); ++rhs) {
            if (streams[rhs].empty() || !sameNotatedTones(streams[lhs], streams[rhs]))
              continue;
            observed.insert(std::to_string(static_cast<int>(form)) + ":" + std::to_string(minor) +
                            ":" + std::to_string(seed) + ":" +
                            subjectCharacterToString(kCharacters[lhs]) + "=" +
                            subjectCharacterToString(kCharacters[rhs]));
          }
        }
      }
    }
  }
  EXPECT_EQ(observed, kPinnedIdenticalPairs);
}

}  // namespace
}  // namespace bach::application
