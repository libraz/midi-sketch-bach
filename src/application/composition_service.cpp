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
#include "composer/renderer.h"
#include "composer/validator.h"
#include "core/instrument_program.h"
#include "core/rng_util.h"
#include "midi/midi_writer.h"
#include "midi/velocity_curve.h"

namespace bach::application {
namespace {

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

void mergeControlChanges(std::vector<CcEvent>* events) {
  if (events == nullptr || events->empty())
    return;
  std::stable_sort(events->begin(), events->end(), [](const CcEvent& lhs, const CcEvent& rhs) {
    if (lhs.tick != rhs.tick)
      return lhs.tick < rhs.tick;
    return lhs.controller < rhs.controller;
  });
  std::vector<CcEvent> merged;
  merged.reserve(events->size());
  for (const CcEvent& event : *events) {
    if (!merged.empty() && merged.back().tick == event.tick &&
        merged.back().controller == event.controller) {
      merged.back().value = std::max(merged.back().value, event.value);
    } else {
      merged.push_back(event);
    }
  }
  *events = std::move(merged);
}

std::uint32_t totalTicks(const std::vector<NoteEvent>& notes) {
  std::uint32_t result = 0;
  for (const auto& note : notes) {
    result = std::max(result, note.start_tick + note.duration);
  }
  return result;
}

bool hasContrastingFugueSection(FormType form) {
  return form == FormType::PreludeAndFugue || form == FormType::ToccataAndFugue ||
         form == FormType::FantasiaAndFugue;
}

void appendSectionTempoChanges(std::vector<TempoEvent>* events, FormType form,
                               const std::vector<Tick>& cadence_ticks, Tick ticks_per_bar,
                               std::uint32_t total_ticks, std::uint16_t bpm) {
  if (events == nullptr || !hasContrastingFugueSection(form) || ticks_per_bar == 0 ||
      total_ticks == 0) {
    return;
  }
  const std::uint16_t fugue_bpm =
      static_cast<std::uint16_t>(std::max(1, (static_cast<int>(bpm) * 108 + 50) / 100));
  for (const Tick cadence_tick : cadence_ticks) {
    const Tick boundary = cadence_tick + ticks_per_bar;
    if (boundary > 0 && boundary < total_ticks) {
      events->push_back({boundary, fugue_bpm});
      return;
    }
  }
}

void sortTempoEvents(std::vector<TempoEvent>* events) {
  if (events == nullptr) {
    return;
  }
  std::stable_sort(
      events->begin(), events->end(),
      [](const TempoEvent& left, const TempoEvent& right) { return left.tick < right.tick; });
  events->erase(std::unique(events->begin(), events->end(),
                            [](const TempoEvent& left, const TempoEvent& right) {
                              return left.tick == right.tick;
                            }),
                events->end());
}

}  // namespace

PerformanceProfile resolvePerformanceProfile(FormType form, InstrumentType instrument) {
  PerformanceProfile profile;
  profile.registration_terraces =
      instrument == InstrumentType::Organ || instrument == InstrumentType::Harpsichord;
  profile.continuous_expression = instrument == InstrumentType::Piano ||
                                  instrument == InstrumentType::Violin ||
                                  instrument == InstrumentType::Cello;
  switch (form) {
    case FormType::TrioSonata:
      profile.final_ritardando = composer::RitardandoStyle::None;
      break;
    case FormType::Fugue:
      profile.final_ritardando = composer::RitardandoStyle::Gentle;
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

void resolveDefaults(CompositionRequest* request) {
  if (request != nullptr && request->bpm == 0) {
    request->bpm = kDefaultBpm;
  }
}

CompositionStatus compose(const CompositionRequest& request, CompositionProduct* out) {
  if (!out) {
    return CompositionStatus::InvalidArgument;
  }
  *out = CompositionProduct{};
  CompositionRequest effective = request;
  resolveDefaults(&effective);
  out->form = effective.form;
  out->key = effective.key;
  out->character = effective.character;
  out->scale = effective.scale;
  out->bpm = effective.bpm;
  out->seed = effective.seed == 0 ? rng::generateRandomSeed() : effective.seed;
  out->instrument = effective.instrument_specified ? effective.instrument
                                                   : defaultInstrumentForForm(effective.form);
  out->performance_profile = resolvePerformanceProfile(effective.form, out->instrument);
  out->form_display = formTypeToDisplayString(effective.form);

  if (!composer::isFormCharacterCompatible(effective.form, effective.character)) {
    out->status = CompositionStatus::IncompatibleCharacter;
    return out->status;
  }
  if (effective.instrument_specified &&
      !isInstrumentCompatibleWithForm(effective.form, effective.instrument)) {
    out->status = CompositionStatus::IncompatibleInstrument;
    return out->status;
  }

  out->resolved_bars =
      composer::resolveBars(effective.form, effective.scale, effective.target_bars);
  composer::ComposeRequest compose_request;
  compose_request.form = effective.form;
  compose_request.is_minor = effective.key.is_minor;
  compose_request.character = effective.character;
  compose_request.target_bars = out->resolved_bars;
  compose_request.seed = out->seed;
  compose_request.enable_free_counterpoint = effective.enable_free_counterpoint;

  composer::HarnessFixture fixture;
  const auto director_status = composer::buildFormFixture(compose_request, &fixture);
  if (director_status == composer::FormDirectorStatus::IncompatibleCharacter) {
    out->status = CompositionStatus::IncompatibleCharacter;
    return out->status;
  }
  if (director_status == composer::FormDirectorStatus::FreeCounterpointUnavailable) {
    out->status = CompositionStatus::FreeCounterpointUnavailable;
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
  ornament.character = effective.character;
  ornament.instrument = out->instrument;
  ornament.mode =
      effective.key.is_minor ? composer::detail::Mode::Minor : composer::detail::Mode::Major;
  ornament.seed = out->seed;
  ornament.ticks_per_bar = ticks_per_bar;
  ornament.ts_numerator = fixture.harmony.ts_numerator;
  ornament.meter_profile = fixture.harmony.meter_profile;
  ornament.harmonic_plan = &fixture.harmony;
  ornament.bpm = effective.bpm;
  ornament.exempt_voices =
      voicesForIntent(fixture.voice_plan, {composer::VoiceIntent::GroundCarrier,
                                           composer::VoiceIntent::PassacagliaGround,
                                           composer::VoiceIntent::GoldbergBassCarrier});
  ornament.skeleton_exempt_voices =
      voicesForIntent(fixture.voice_plan, {composer::VoiceIntent::CantusFirmusCarrier});
  if (effective.form == FormType::GoldbergVariations) {
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
  // The composer deliberately emits instrument-neutral notes. Apply the
  // phrase-aware velocity curve only after FinalScore validation (velocity is
  // not a contrapuntal input), then re-render so MIDI and public event JSON
  // expose the same updated note attributes. This preserves pitch/onset/order
  // and therefore provenance index alignment.
  std::vector<Tick> cadence_ticks;
  cadence_ticks.reserve(fixture.harmony.cadences.size());
  for (const auto& cadence : fixture.harmony.cadences) {
    cadence_ticks.push_back(cadence.tick);
  }
  applyVelocityCurve(out->composition.notes, out->instrument, cadence_ticks);
  out->composition.tracks = composer::Renderer{}.render(out->composition.notes);
  applyInstrument(out->composition.tracks, out->instrument);

  const composer::FormSpec& spec = composer::formSpec(effective.form);
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
    const Tick phrase_climax =
        fixture.climax_end_tick > fixture.climax_start_tick ? fixture.climax_start_tick : 0;
    const auto phrase = composer::buildPhraseDynamics(cycle_count, snap, ticks_per_bar,
                                                      out->total_ticks, phrase_climax);
    for (auto& track : out->composition.tracks) {
      track.cc_events.insert(track.cc_events.end(), phrase.begin(), phrase.end());
    }
  }
  for (auto& track : out->composition.tracks) {
    mergeControlChanges(&track.cc_events);
  }

  out->tempo_events.push_back({0, effective.bpm});
  appendSectionTempoChanges(&out->tempo_events, effective.form, fixture.section_cadence_ticks,
                            ticks_per_bar, out->total_ticks, effective.bpm);
  const auto ritard =
      composer::buildFinalRitardando(effective.bpm, out->total_ticks, ticks_per_bar,
                                     out->performance_profile.final_ritardando, spec.ts_numerator);
  out->tempo_events.insert(out->tempo_events.end(), ritard.begin(), ritard.end());
  sortTempoEvents(&out->tempo_events);
  out->time_signature_events.push_back({0, {spec.ts_numerator, spec.ts_denominator}});

  const auto output_octave_shift =
      selectOutputOctaveShift(out->composition.notes, effective.key.tonic, out->instrument);
  if (!output_octave_shift) {
    out->status = CompositionStatus::MidiFailed;
    return out->status;
  }
  out->output_octave_shift = *output_octave_shift;

  MidiWriter writer;
  if (writer.build(out->composition.tracks, out->tempo_events, out->time_signature_events,
                   effective.key, "", out->output_octave_shift) != MidiWriterStatus::Ok) {
    out->status = CompositionStatus::MidiFailed;
    return out->status;
  }
  out->midi_bytes = writer.toBytes();
  out->total_bars =
      ticks_per_bar == 0
          ? 0
          : static_cast<std::uint16_t>((out->total_ticks + ticks_per_bar - 1) / ticks_per_bar);

  composer::HomepageMeta meta;
  meta.form_name = formTypeToString(effective.form);
  meta.key_name = keySignatureToString(effective.key);
  meta.output_key = effective.key.tonic;
  meta.output_octave_shift = out->output_octave_shift;
  meta.bpm = effective.bpm;
  meta.seed = out->seed;
  meta.total_ticks = out->total_ticks;
  meta.total_bars = out->total_bars;
  meta.tempo_events = out->tempo_events;
  meta.time_signature_events = out->time_signature_events;
  meta.description = out->form_display + " in " + meta.key_name;
  out->homepage_events_json = composer::buildHomepageEventsJson(out->composition, meta);
  out->generated_json = composer::emitGeneratedJson(out->composition.notes,
                                                    out->composition.validation, out->tempo_events);
  out->provenance_json = composer::emitProvenanceJson(out->composition.provenance);
  out->status = CompositionStatus::Ok;
  return out->status;
}

}  // namespace bach::application
