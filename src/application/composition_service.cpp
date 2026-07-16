#include "application/composition_service.h"

#include <algorithm>
#include <cstddef>
#include <initializer_list>
#include <utility>

#include "composer/expression_events.h"
#include "composer/form_director.h"
#include "composer/harness_fixture.h"
#include "composer/json_export.h"
#include "composer/ornament_pass.h"
#include "composer/validator.h"
#include "core/instrument_program.h"
#include "core/rng_util.h"
#include "midi/midi_writer.h"

namespace bach::application {
namespace {

std::string formDisplayName(FormType form) {
  switch (form) {
    case FormType::Fugue:
      return "Fugue";
    case FormType::PreludeAndFugue:
      return "Prelude and Fugue";
    case FormType::TrioSonata:
      return "Trio Sonata";
    case FormType::ChoralePrelude:
      return "Chorale Prelude";
    case FormType::ToccataAndFugue:
      return "Toccata and Fugue";
    case FormType::Passacaglia:
      return "Passacaglia";
    case FormType::FantasiaAndFugue:
      return "Fantasia and Fugue";
    case FormType::CelloPrelude:
      return "Cello Prelude";
    case FormType::Chaconne:
      return "Chaconne";
    case FormType::GoldbergVariations:
      return "Goldberg Variations";
  }
  return "Composition";
}

std::vector<VoiceId> voicesForIntent(const composer::VoicePlan& plan,
                                     std::initializer_list<composer::VoiceIntent> intents) {
  std::vector<VoiceId> voices;
  for (const auto& span : plan.spans) {
    for (const auto intent : intents) {
      if (span.intent == intent) {
        voices.push_back(span.voice);
        break;
      }
    }
  }
  std::sort(voices.begin(), voices.end());
  voices.erase(std::unique(voices.begin(), voices.end()), voices.end());
  return voices;
}

std::uint32_t totalTicks(const std::vector<NoteEvent>& notes) {
  std::uint32_t result = 0;
  for (const auto& note : notes) {
    result = std::max(result, note.start_tick + note.duration);
  }
  return result;
}

}  // namespace

PerformanceProfile resolvePerformanceProfile(FormType form, InstrumentType instrument) {
  PerformanceProfile profile;
  profile.registration_terraces = instrument == InstrumentType::Organ;
  profile.continuous_expression = instrument == InstrumentType::Piano ||
                                  instrument == InstrumentType::Violin ||
                                  instrument == InstrumentType::Cello;
  switch (form) {
    case FormType::Fugue:
    case FormType::TrioSonata:
      profile.final_ritardando = composer::RitardandoStyle::None;
      break;
    case FormType::ToccataAndFugue:
    case FormType::FantasiaAndFugue:
      profile.final_ritardando = composer::RitardandoStyle::Rhetorical;
      break;
    case FormType::PreludeAndFugue:
    case FormType::ChoralePrelude:
    case FormType::Passacaglia:
    case FormType::CelloPrelude:
    case FormType::Chaconne:
    case FormType::GoldbergVariations:
      profile.final_ritardando = composer::RitardandoStyle::Gentle;
      break;
  }
  return profile;
}

CompositionStatus compose(const CompositionRequest& request, CompositionProduct* out) {
  if (!out) {
    return CompositionStatus::InvalidArgument;
  }
  *out = CompositionProduct{};
  if (request.bpm == 0) {
    return CompositionStatus::InvalidArgument;
  }
  out->form = request.form;
  out->key = request.key;
  out->character = request.character;
  out->scale = request.scale;
  out->bpm = request.bpm;
  out->seed = request.seed == 0 ? rng::generateRandomSeed() : request.seed;
  out->instrument =
      request.instrument_specified ? request.instrument : defaultInstrumentForForm(request.form);
  out->performance_profile = resolvePerformanceProfile(request.form, out->instrument);
  out->form_display = formDisplayName(request.form);

  if (!composer::isFormCharacterCompatible(request.form, request.character)) {
    out->status = CompositionStatus::IncompatibleCharacter;
    return out->status;
  }

  out->resolved_bars = composer::resolveBars(request.form, request.scale, request.target_bars);
  composer::ComposeRequest compose_request;
  compose_request.form = request.form;
  compose_request.is_minor = request.key.is_minor;
  compose_request.character = request.character;
  compose_request.target_bars = out->resolved_bars;
  compose_request.seed = out->seed;
  compose_request.enable_free_counterpoint = request.enable_free_counterpoint;

  composer::HarnessFixture fixture;
  const auto director_status = composer::buildFormFixture(compose_request, &fixture);
  if (director_status == composer::FormDirectorStatus::IncompatibleCharacter) {
    out->status = CompositionStatus::IncompatibleCharacter;
    return out->status;
  }
  if (director_status != composer::FormDirectorStatus::Ok) {
    out->status = CompositionStatus::InvalidForm;
    return out->status;
  }

  out->composition =
      composer::Composer{}.run(fixture.material, fixture.harmony, fixture.voice_plan);
  out->generated_json =
      composer::emitGeneratedJson(out->composition.notes, out->composition.validation);
  out->provenance_json = composer::emitProvenanceJson(out->composition.provenance);
  if (out->composition.validation.status != composer::ValidationStatus::Ok) {
    out->diagnostic_json = composer::emitDiagnosticJson(
        out->composition.notes, out->composition.provenance, out->composition.validation);
    out->status = CompositionStatus::GenerationFailed;
    return out->status;
  }

  const Tick ticks_per_bar = fixture.harmony.ticksPerBar();
  out->total_ticks = totalTicks(out->composition.notes);

  composer::OrnamentParams ornament;
  ornament.character = request.character;
  ornament.instrument = out->instrument;
  ornament.mode =
      request.key.is_minor ? composer::detail::Mode::Minor : composer::detail::Mode::Major;
  ornament.seed = out->seed;
  ornament.ticks_per_bar = ticks_per_bar;
  ornament.bpm = request.bpm;
  ornament.exempt_voices =
      voicesForIntent(fixture.voice_plan, {composer::VoiceIntent::GroundCarrier,
                                           composer::VoiceIntent::PassacagliaGround,
                                           composer::VoiceIntent::GoldbergBassCarrier});
  ornament.skeleton_exempt_voices =
      voicesForIntent(fixture.voice_plan, {composer::VoiceIntent::CantusFirmusCarrier});
  if (request.form == FormType::GoldbergVariations) {
    ornament.aria_end_tick = 4 * ticks_per_bar;
  }
  ornament.section_cadence_ticks = fixture.section_cadence_ticks;
  if (fixture.climax_end_tick > fixture.climax_start_tick) {
    ornament.climax_start_tick = fixture.climax_start_tick;
    ornament.climax_end_tick = fixture.climax_end_tick;
  } else {
    const Tick climax_tick =
        static_cast<Tick>(static_cast<std::uint64_t>(out->total_ticks) * 3 / 4);
    ornament.climax_start_tick = climax_tick > ticks_per_bar ? climax_tick - ticks_per_bar : 0;
    ornament.climax_end_tick = climax_tick + ticks_per_bar;
  }
  composer::applyOrnamentPass(out->composition, ornament);

  out->final_validation = composer::Validator{}.validate(
      out->composition.notes, out->composition.provenance, fixture.harmony, fixture.material,
      composer::ValidationScope::FinalScore);
  if (out->final_validation.status != composer::ValidationStatus::Ok) {
    out->generated_json =
        composer::emitGeneratedJson(out->composition.notes, out->final_validation);
    out->provenance_json = composer::emitProvenanceJson(out->composition.provenance);
    out->diagnostic_json = composer::emitDiagnosticJson(
        out->composition.notes, out->composition.provenance, out->final_validation);
    out->status = CompositionStatus::FinalValidationFailed;
    return out->status;
  }
  applyInstrument(out->composition.tracks, out->instrument);

  const composer::FormSpec& spec = composer::formSpec(request.form);
  const std::uint16_t snap = spec.snap_bars == 0 ? 1 : spec.snap_bars;
  std::size_t cycle_count = out->resolved_bars / snap;
  if (cycle_count == 0) {
    cycle_count = 1;
  }
  if (out->performance_profile.registration_terraces) {
    const Tick registration_climax =
        fixture.climax_end_tick > fixture.climax_start_tick ? fixture.climax_start_tick : 0;
    const auto plan = composer::buildRegistrationPlan(
        out->resolved_bars, cycle_count, ticks_per_bar, out->total_ticks, registration_climax);
    const auto terraces =
        composer::buildRegistrationTerraces(fixture.registration_step_ticks, out->total_ticks);
    for (auto& track : out->composition.tracks) {
      track.cc_events.insert(track.cc_events.end(), plan.begin(), plan.end());
      track.cc_events.insert(track.cc_events.end(), terraces.begin(), terraces.end());
    }
  }
  if (out->performance_profile.continuous_expression) {
    const auto phrase =
        composer::buildPhraseDynamics(cycle_count, snap, ticks_per_bar, out->total_ticks);
    for (auto& track : out->composition.tracks) {
      track.cc_events.insert(track.cc_events.end(), phrase.begin(), phrase.end());
    }
  }

  out->tempo_events.push_back({0, request.bpm});
  const auto ritard = composer::buildFinalRitardando(request.bpm, out->total_ticks, ticks_per_bar,
                                                     out->performance_profile.final_ritardando);
  out->tempo_events.insert(out->tempo_events.end(), ritard.begin(), ritard.end());
  out->time_signature_events.push_back({0, {spec.ts_numerator, spec.ts_denominator}});

  MidiWriter writer;
  if (writer.build(out->composition.tracks, out->tempo_events, out->time_signature_events,
                   request.key) != MidiWriterStatus::Ok) {
    out->status = CompositionStatus::MidiFailed;
    return out->status;
  }
  out->midi_bytes = writer.toBytes();
  out->total_bars =
      ticks_per_bar == 0
          ? 0
          : static_cast<std::uint16_t>((out->total_ticks + ticks_per_bar - 1) / ticks_per_bar);

  composer::HomepageMeta meta;
  meta.form_name = formTypeToString(request.form);
  meta.key_name = keySignatureToString(request.key);
  meta.bpm = request.bpm;
  meta.seed = out->seed;
  meta.total_ticks = out->total_ticks;
  meta.total_bars = out->total_bars;
  meta.description = out->form_display + " in " + meta.key_name;
  out->homepage_events_json = composer::buildHomepageEventsJson(out->composition, meta);
  out->generated_json =
      composer::emitGeneratedJson(out->composition.notes, out->composition.validation);
  out->provenance_json = composer::emitProvenanceJson(out->composition.provenance);
  out->status = CompositionStatus::Ok;
  return out->status;
}

}  // namespace bach::application
