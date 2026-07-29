#ifndef BACH_COMPOSER_JSON_EXPORT_H
#define BACH_COMPOSER_JSON_EXPORT_H

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "composer/composer.h"
#include "composer/provenance.h"
#include "core/basic_types.h"

namespace bach::composer {

// Two-file JSON output.
//
//   * generated.json  — fed to the external bach-mcp evaluator. Notes only.
//                       No VoiceIntent, no satisfied_rules, no
//                       candidate_score, no source, no span_id.
//                       Schema fixed at bach-mcp/schema/ in the external repo.
//
//   * provenance.json — local audit log. Every note gets its full
//                       NoteProvenance record. Read by developer tools
//                       and tests only. Never handed to the evaluator.
//
// Invariant: generated.json.notes[i] and provenance.json.notes[i] refer
// to the same NoteEvent. Tests enforce this by emitting both from the
// same in-memory vectors in one call.

constexpr std::uint32_t kTicksPerBeatExport = kTicksPerBeat;

// Emit the evaluator-facing JSON. Contains the polyphonic note stream, the
// minimum metadata an external evaluator needs to interpret tick units,
// an optional MIDI-aligned tempo map, and optional info-level validation
// metrics. Legacy overloads omit tempo rather than guessing it.
std::string emitGeneratedJson(const std::vector<NoteEvent>& notes);
std::string emitGeneratedJson(const std::vector<NoteEvent>& notes,
                              const ValidationReport& validation);
std::string emitGeneratedJson(const std::vector<NoteEvent>& notes,
                              const ValidationReport& validation,
                              const std::vector<TempoEvent>& tempo_events);

// Emit the developer-facing audit JSON. One entry per note, parallel to
// generated.json.notes by index, carrying the full NoteProvenance.
std::string emitProvenanceJson(const std::vector<NoteProvenance>& provenance);

/// @brief Emit a self-contained failed-run diagnostic without changing generated.v1.
///
/// diagnostic.v1 carries validation status/failures plus index-parallel note and
/// provenance records. It is intended for CLI/C/WASM diagnostics, never for the
/// external scorer contract.
std::string emitDiagnosticJson(const std::vector<NoteEvent>& notes,
                               const std::vector<NoteProvenance>& provenance,
                               const ValidationReport& validation);

/// @brief Render a NoteSource enumerator to its lowercase wire string.
/// @param source Provenance source of a note.
/// @return One of "material", "compose", "ornament".
/// @note Used by the homepage events JSON. The provenance audit JSON uses
///       its own capitalized labels and is unaffected.
std::string_view noteSourceToWire(NoteSource source);

/// @brief Caller-supplied metadata for the homepage events JSON.
///
/// Key names and the human description are pre-rendered strings so this header
/// stays inside the composer isolation boundary (no legacy key/form helpers
/// are reachable from src/composer/). The C API / CLI computes these outside
/// the composer and passes them in verbatim.
struct HomepageMeta {
  std::string form_name;          ///< e.g. "fugue" (formTypeToString).
  std::string key_name;           ///< e.g. "G minor" (caller-rendered).
  Key output_key = Key::C;        ///< Transposition applied only to emitted event pitches.
  int output_octave_shift = 0;    ///< Output-only whole-octave instrument adjustment.
  std::uint16_t bpm = 0;          ///< Tempo in beats per minute.
  std::uint32_t seed = 0;         ///< Generation seed.
  std::uint32_t total_ticks = 0;  ///< Total length in ticks.
  std::uint16_t total_bars = 0;   ///< Total length in bars.
  std::vector<TempoEvent> tempo_events;
  std::vector<TimeSignatureEvent> time_signature_events;
  std::string description;  ///< Human-readable, e.g. "Fugue in G minor".
};

/// @brief Emit the homepage-facing events JSON consumed by the web SPA.
///
/// The output schema is a frozen contract: top-level form/key/bpm/seed/
/// total_ticks/total_bars/description plus tempo and time-signature maps, then a "tracks" array of
/// {name, channel, program, note_count, notes, control_changes[]} objects. Each note carries
/// {pitch, velocity, start_tick, duration, voice, source}. Tracks are taken
/// from result.tracks verbatim (the caller fills name/program via
/// applyInstrument beforehand). Event pitches are transposed from the internal
/// C-major representation to HomepageMeta::output_key. The per-note "source"
/// is resolved before transposition by matching each track note back to
/// result.notes (and its index-parallel result.provenance) on
/// (voice, start_tick, pitch, duration); duplicate tuples are consumed in
/// order. An unmatched note defaults to "compose".
///
/// @param result Finished compose result (tracks, notes, provenance).
/// @param meta Caller-rendered metadata.
/// @return Serialized homepage events JSON string.
std::string buildHomepageEventsJson(const ComposeResult& result, const HomepageMeta& meta);

}  // namespace bach::composer

#endif  // BACH_COMPOSER_JSON_EXPORT_H
