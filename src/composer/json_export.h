#ifndef BACH_COMPOSER_JSON_EXPORT_H
#define BACH_COMPOSER_JSON_EXPORT_H

#include <string>
#include <vector>

#include "composer/provenance.h"
#include "core/basic_types.h"

namespace bach::composer {

// Two-file JSON output.
//
//   * generated.json  — fed to the external bach-mcp evaluator. Notes only.
//                       No VoiceIntent, no satisfied_rules, no
//                       candidate_score, no source, no span_id.
//                       Schema fixed at bach-mcp/schema/ (when Phase 1a
//                       lands the external repo).
//
//   * provenance.json — local audit log. Every note gets its full
//                       NoteProvenance record. Read by developer tools
//                       and tests only. Never handed to the evaluator.
//
// Invariant: generated.json.notes[i] and provenance.json.notes[i] refer
// to the same NoteEvent. Tests enforce this by emitting both from the
// same in-memory vectors in one call.

constexpr std::uint32_t kTicksPerBeatExport = kTicksPerBeat;

// Emit the evaluator-facing JSON. Contains only the polyphonic note
// stream and the minimum metadata an external evaluator needs to
// interpret tick units.
std::string emitGeneratedJson(const std::vector<NoteEvent>& notes);

// Emit the developer-facing audit JSON. One entry per note, parallel to
// generated.json.notes by index, carrying the full NoteProvenance.
std::string emitProvenanceJson(const std::vector<NoteProvenance>& provenance);

}  // namespace bach::composer

#endif  // BACH_COMPOSER_JSON_EXPORT_H
