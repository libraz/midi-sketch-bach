#include "composer/json_export.h"

#include <cstddef>
#include <map>
#include <queue>
#include <string_view>
#include <tuple>

#include "core/json_helpers.h"

namespace bach::composer {

namespace {

// JsonWriter has overloads for string_view / int / double / bool / null but
// none for const char*. A raw string literal passed to value() decays to
// const char* and the compiler picks the bool overload, producing literal
// "true". Always route string literals through this wrapper.
void writeStr(JsonWriter& w, std::string_view s) {
  w.value(s);
}

Tick computeDuration(const std::vector<NoteEvent>& notes) {
  Tick last = 0;
  for (const auto& n : notes) {
    const Tick end = n.start_tick + n.duration;
    if (end > last)
      last = end;
  }
  return last;
}

}  // namespace

std::string emitGeneratedJson(const std::vector<NoteEvent>& notes) {
  ValidationReport empty_report;
  return emitGeneratedJson(notes, empty_report);
}

std::string emitGeneratedJson(const std::vector<NoteEvent>& notes,
                              const ValidationReport& validation) {
  JsonWriter w;
  w.beginObject();
  w.key("schema_version");
  writeStr(w, "generated.v1");
  w.key("ticks_per_beat");
  w.value(static_cast<int>(kTicksPerBeatExport));
  w.key("duration_ticks");
  w.value(static_cast<int>(computeDuration(notes)));
  w.key("notes");
  w.beginArray();
  for (std::size_t i = 0; i < notes.size(); ++i) {
    w.beginObject();
    w.key("index");
    w.value(static_cast<int>(i));
    w.key("start_tick");
    w.value(static_cast<int>(notes[i].start_tick));
    w.key("duration");
    w.value(static_cast<int>(notes[i].duration));
    w.key("pitch");
    w.value(static_cast<int>(notes[i].pitch));
    w.key("voice");
    w.value(static_cast<int>(notes[i].voice));
    w.key("velocity");
    w.value(static_cast<int>(notes[i].velocity));
    w.endObject();
  }
  w.endArray();
  if (!validation.subject_features.empty() || !validation.stream_segregation.empty() ||
      !validation.texture_metrics.empty()) {
    w.key("info");
    w.beginObject();
    if (!validation.subject_features.empty()) {
      w.key("subject_features");
      w.beginArray();
      for (const auto& features : validation.subject_features) {
        w.beginObject();
        w.key("length");
        w.value(features.length);
        w.key("range_semitones");
        w.value(features.range_semitones);
        w.key("unique_pitch_classes");
        w.value(features.unique_pitch_classes);
        w.key("opening_interval");
        w.value(features.opening_interval);
        w.key("unique_intervals");
        w.value(features.unique_intervals);
        w.key("max_leap");
        w.value(features.max_leap);
        w.endObject();
      }
      w.endArray();
    }
    if (!validation.stream_segregation.empty()) {
      w.key("stream_segregation");
      w.beginArray();
      for (const auto& span : validation.stream_segregation) {
        w.beginObject();
        w.key("span_id");
        if (span.span_id == kInvalidSpanId) {
          w.valueNull();
        } else {
          w.value(static_cast<int>(span.span_id));
        }
        w.key("detected_stream_count");
        w.value(span.detected_stream_count);
        w.key("cell_based_stream_count");
        w.value(span.cell_based_stream_count);
        w.key("cell_count");
        w.value(span.cell_count);
        w.key("disagrees_with_cell_counterpoint");
        w.value(span.disagrees_with_cell_counterpoint);
        w.key("stream_separation_semitones");
        w.value(span.stream_separation_semitones);
        w.key("transition_note_indices");
        w.beginArray();
        for (int index : span.transition_note_indices) {
          w.value(index);
        }
        w.endArray();
        w.endObject();
      }
      w.endArray();
    }
    if (!validation.texture_metrics.empty()) {
      w.key("texture_metrics");
      w.beginArray();
      for (const auto& metrics : validation.texture_metrics) {
        w.beginObject();
        w.key("max_active_voices");
        w.value(metrics.max_active_voices);
        w.key("avg_active_voices");
        w.value(metrics.avg_active_voices);
        w.key("mono_ratio");
        w.value(metrics.mono_ratio);
        w.key("compass_violation_count");
        w.value(metrics.compass_violation_count);
        w.key("register_overlap_ratio");
        w.value(metrics.register_overlap_ratio);
        w.key("voices");
        w.beginArray();
        for (const auto& voice : metrics.voices) {
          w.beginObject();
          w.key("voice");
          w.value(static_cast<int>(voice.voice));
          w.key("silence_ratio");
          w.value(voice.silence_ratio);
          w.key("max_repeated_run");
          w.value(voice.max_repeated_run);
          w.key("min_pitch");
          w.value(voice.min_pitch);
          w.key("max_pitch");
          w.value(voice.max_pitch);
          w.endObject();
        }
        w.endArray();
        w.endObject();
      }
      w.endArray();
    }
    w.endObject();
  }
  w.endObject();
  return w.toString();
}

std::string emitProvenanceJson(const std::vector<NoteProvenance>& provenance) {
  JsonWriter w;
  w.beginObject();
  w.key("schema_version");
  writeStr(w, "provenance.v1");
  w.key("notes");
  w.beginArray();
  for (std::size_t i = 0; i < provenance.size(); ++i) {
    const auto& p = provenance[i];
    w.beginObject();
    w.key("index");
    w.value(static_cast<int>(i));
    w.key("span_id");
    if (p.span_id == kInvalidSpanId) {
      w.valueNull();
    } else {
      w.value(static_cast<int>(p.span_id));
    }
    w.key("voice_intent");
    writeStr(w, voiceIntentToString(p.voice_intent));
    w.key("source");
    const char* source_str = "Compose";
    if (p.source == NoteSource::Material)
      source_str = "Material";
    else if (p.source == NoteSource::Ornament)
      source_str = "Ornament";
    writeStr(w, source_str);
    w.key("candidate_score");
    w.value(static_cast<double>(p.candidate_score));
    // Shadow (corpus-scorer audit) fields are emitted ONLY for Compose notes.
    // Material and Ornament notes never run the candidate scorer, so their
    // shadow fields carry no decision — emitting the default zeros would read
    // as "the scorer chose pitch 0 with probability 0" and pollute the natural
    // `shadow_winning_pitch != pitch` (scorer-disagreed) diagnostic with one
    // false hit per verbatim note. Omitting the keys (same pattern as the
    // optional satisfied_rules_high lane below) keeps a provenance trace
    // unambiguous: shadow_* present ⟺ this note was scored.
    if (p.source == NoteSource::Compose) {
      w.key("shadow_score");
      w.value(static_cast<double>(p.shadow_score));
      w.key("shadow_winning_pitch");
      w.value(static_cast<int>(p.shadow_winning_pitch));
      w.key("shadow_winning_pitch_without_markov");
      w.value(static_cast<int>(p.shadow_winning_pitch_without_markov));
    }
    w.key("satisfied_rules");
    // The low 64 rule bits always emit as one integer. The high lane (bits
    // 64..127) emits as a separate optional `satisfied_rules_high` integer ONLY
    // when nonzero, so every note that uses no high-lane bit stays byte-identical
    // to the pre-high-lane output. The first high-lane bit in use is
    // CountersubjectInvertible (bit 64); notes without it omit the field.
    w.value(p.satisfied_rules.low64());
    if (p.satisfied_rules.high64() != 0) {
      w.key("satisfied_rules_high");
      w.value(p.satisfied_rules.high64());
    }
    w.key("rejected_alternatives");
    w.value(static_cast<int>(p.rejected_alternatives));
    w.endObject();
  }
  w.endArray();
  w.endObject();
  return w.toString();
}

std::string_view noteSourceToWire(NoteSource source) {
  switch (source) {
    case NoteSource::Material:
      return "material";
    case NoteSource::Ornament:
      return "ornament";
    case NoteSource::Compose:
      return "compose";
  }
  // Defensive default; the enum is exhaustively handled above.
  return "compose";
}

std::string buildHomepageEventsJson(const ComposeResult& result, const HomepageMeta& meta) {
  // Build a lookup from (voice, start_tick, pitch, duration) to a queue of
  // indices into result.notes (parallel to result.provenance). Tracks
  // partition the same notes by voice preserving order, so identical tuples
  // are consumed first-in first-out as track notes are visited in order.
  using NoteKey = std::tuple<int, Tick, std::uint8_t, Tick>;
  std::map<NoteKey, std::queue<std::size_t>> index_by_key;
  for (std::size_t idx = 0; idx < result.notes.size(); ++idx) {
    const NoteEvent& note = result.notes[idx];
    const NoteKey key{static_cast<int>(note.voice), note.start_tick, note.pitch, note.duration};
    index_by_key[key].push(idx);
  }

  JsonWriter w;
  w.beginObject();
  w.key("form");
  writeStr(w, meta.form_name);
  w.key("key");
  writeStr(w, meta.key_name);
  w.key("bpm");
  w.value(static_cast<int>(meta.bpm));
  w.key("seed");
  w.value(static_cast<std::uint32_t>(meta.seed));
  w.key("total_ticks");
  w.value(static_cast<std::uint32_t>(meta.total_ticks));
  w.key("total_bars");
  w.value(static_cast<int>(meta.total_bars));
  w.key("description");
  writeStr(w, meta.description);

  w.key("tracks");
  w.beginArray();
  for (const Track& track : result.tracks) {
    w.beginObject();
    w.key("name");
    writeStr(w, track.name);
    w.key("channel");
    w.value(static_cast<int>(track.channel));
    w.key("program");
    w.value(static_cast<int>(track.program));
    w.key("note_count");
    w.value(static_cast<int>(track.notes.size()));
    w.key("notes");
    w.beginArray();
    for (const NoteEvent& note : track.notes) {
      // Resolve provenance source by consuming the matching index queue.
      NoteSource source = NoteSource::Compose;
      const NoteKey key{static_cast<int>(note.voice), note.start_tick, note.pitch, note.duration};
      auto found = index_by_key.find(key);
      if (found != index_by_key.end() && !found->second.empty()) {
        const std::size_t note_idx = found->second.front();
        found->second.pop();
        if (note_idx < result.provenance.size())
          source = result.provenance[note_idx].source;
      }

      w.beginObject();
      w.key("pitch");
      w.value(static_cast<int>(note.pitch));
      w.key("velocity");
      w.value(static_cast<int>(note.velocity));
      w.key("start_tick");
      w.value(static_cast<int>(note.start_tick));
      w.key("duration");
      w.value(static_cast<int>(note.duration));
      w.key("voice");
      w.value(static_cast<int>(note.voice));
      w.key("source");
      writeStr(w, noteSourceToWire(source));
      w.endObject();
    }
    w.endArray();
    w.endObject();
  }
  w.endArray();
  w.endObject();
  return w.toString();
}

}  // namespace bach::composer
