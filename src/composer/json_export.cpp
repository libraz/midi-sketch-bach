#include "composer/json_export.h"

#include <string_view>

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
    writeStr(w, p.source == NoteSource::Material ? "Material" : "Compose");
    w.key("candidate_score");
    w.value(static_cast<double>(p.candidate_score));
    w.key("satisfied_rules");
    // Emit as integer; bach-mcp parses it as a bitset on its side.
    w.value(static_cast<int>(p.satisfied_rules));
    w.key("rejected_alternatives");
    w.value(static_cast<int>(p.rejected_alternatives));
    w.endObject();
  }
  w.endArray();
  w.endObject();
  return w.toString();
}

}  // namespace bach::composer
