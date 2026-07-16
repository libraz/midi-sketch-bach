/// @file
/// @brief SMF Type 1 MIDI file writer implementation.

#include "midi/midi_writer.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "midi/midi_stream.h"

namespace bach {

namespace {

/// @brief Internal event representation for sorting before writing.
struct WriteEvent {
  uint32_t tick = 0;
  uint8_t status = 0;
  uint8_t data1 = 0;
  uint8_t data2 = 0;
  int priority = 0;  // Lower = earlier at same tick (note-off before note-on)
};

/// @brief Apply key transposition and clamp to valid MIDI range.
/// @param pitch Original pitch in C major context.
/// @param key Target key (Key::C = 0 offset, Key::Cs = +1, etc.).
/// @return Transposed and clamped MIDI pitch.
uint8_t applyKeyTranspose(uint8_t pitch, Key key) {
  int offset = static_cast<int>(key);
  int result = static_cast<int>(pitch) + offset;
  if (result < 0)
    result = 0;
  if (result > 127)
    result = 127;
  return static_cast<uint8_t>(result);
}

constexpr std::uint32_t kMaxMidiTempoUsec = 0x00FFFFFF;

bool isValidTempo(std::uint16_t bpm) {
  return bpm > 0 && kMicrosecondsPerMinute / bpm <= kMaxMidiTempoUsec;
}

bool isValidTimeSignature(const TimeSignature& time_signature) {
  const std::uint8_t denominator = time_signature.denominator;
  return time_signature.numerator > 0 && denominator > 0 && (denominator & (denominator - 1)) == 0;
}

}  // namespace

MidiWriter::MidiWriter() = default;

MidiWriterStatus MidiWriter::build(const std::vector<Track>& tracks,
                                   const std::vector<TempoEvent>& tempo_events, Key key,
                                   const std::string& metadata) {
  return build(tracks, tempo_events, {}, KeySignature{key, false}, metadata);
}

MidiWriterStatus MidiWriter::build(const std::vector<Track>& tracks,
                                   const std::vector<TempoEvent>& tempo_events,
                                   const KeySignature& key_signature, const std::string& metadata) {
  return build(tracks, tempo_events, {}, key_signature, metadata);
}

MidiWriterStatus MidiWriter::build(const std::vector<Track>& tracks,
                                   const std::vector<TempoEvent>& tempo_events,
                                   const std::vector<TimeSignatureEvent>& time_sig_events, Key key,
                                   const std::string& metadata) {
  return build(tracks, tempo_events, time_sig_events, KeySignature{key, false}, metadata);
}

MidiWriterStatus MidiWriter::build(const std::vector<Track>& tracks,
                                   const std::vector<TempoEvent>& tempo_events,
                                   const std::vector<TimeSignatureEvent>& time_sig_events,
                                   const KeySignature& key_signature, const std::string& metadata) {
  data_.clear();

  for (const auto& tempo : tempo_events) {
    if (!isValidTempo(tempo.bpm)) {
      return MidiWriterStatus::InvalidTempo;
    }
  }
  for (const auto& time_signature : time_sig_events) {
    if (!isValidTimeSignature(time_signature.time_sig)) {
      return MidiWriterStatus::InvalidTimeSignature;
    }
  }

  // Count non-empty tracks, plus one for the metadata track.
  uint16_t num_content_tracks = 0;
  for (const auto& track : tracks) {
    if (!track.notes.empty() || !track.events.empty()) {
      ++num_content_tracks;
    }
  }
  uint16_t total_tracks = num_content_tracks + 1;  // +1 for metadata track

  writeHeader(total_tracks, kTicksPerBeat);
  writeMetadataTrack(tempo_events, time_sig_events, key_signature, metadata);

  for (const auto& track : tracks) {
    if (!track.notes.empty() || !track.events.empty()) {
      writeTrack(track, key_signature.tonic);
    }
  }
  return MidiWriterStatus::Ok;
}

std::vector<uint8_t> MidiWriter::toBytes() const {
  return data_;
}

bool MidiWriter::writeToFile(const std::string& path) const {
  FILE* file = std::fopen(path.c_str(), "wb");
  if (!file) {
    return false;
  }
  size_t written = std::fwrite(data_.data(), 1, data_.size(), file);
  std::fclose(file);
  return written == data_.size();
}

void MidiWriter::writeHeader(uint16_t num_tracks, uint16_t division) {
  // "MThd" chunk identifier
  data_.push_back('M');
  data_.push_back('T');
  data_.push_back('h');
  data_.push_back('d');

  // Header length: always 6
  writeBE32(data_, 6);

  // Format: 1 (multi-track)
  writeBE16(data_, 1);

  // Number of tracks
  writeBE16(data_, num_tracks);

  // Division (ticks per quarter note)
  writeBE16(data_, division);
}

void MidiWriter::writeTrack(const Track& track, Key key) {
  std::vector<uint8_t> track_buf;

  // Program change at tick 0
  writeVariableLength(track_buf, 0);  // Delta time = 0
  track_buf.push_back(static_cast<uint8_t>(0xC0 | (track.channel & 0x0F)));
  track_buf.push_back(track.program & 0x7F);

  // Track name meta-event if present
  if (!track.name.empty()) {
    writeVariableLength(track_buf, 0);  // Delta time = 0
    track_buf.push_back(0xFF);          // Meta event
    track_buf.push_back(0x03);          // Track Name
    writeVariableLength(track_buf, static_cast<uint32_t>(track.name.size()));
    for (char chr : track.name) {
      track_buf.push_back(static_cast<uint8_t>(chr));
    }
  }

  // Convert NoteEvents to on/off event pairs.
  std::vector<WriteEvent> events;
  events.reserve(track.notes.size() * 2 + track.events.size());

  for (const auto& note : track.notes) {
    uint8_t out_pitch = applyKeyTranspose(note.pitch, key);

    WriteEvent on_event;
    on_event.tick = note.start_tick;
    on_event.status = static_cast<uint8_t>(0x90 | (track.channel & 0x0F));
    on_event.data1 = out_pitch;
    on_event.data2 = note.velocity;
    on_event.priority = 1;  // Note-on after note-off at same tick
    events.push_back(on_event);

    WriteEvent off_event;
    off_event.tick = note.start_tick + note.duration;
    off_event.status = static_cast<uint8_t>(0x80 | (track.channel & 0x0F));
    off_event.data1 = out_pitch;
    off_event.data2 = 0;
    off_event.priority = 0;  // Note-off before note-on at same tick
    events.push_back(off_event);
  }

  // Include raw MidiEvents from the track.
  for (const auto& evt : track.events) {
    WriteEvent raw_event;
    raw_event.tick = evt.tick;
    raw_event.status = evt.status;
    raw_event.data1 = evt.data1;
    raw_event.data2 = evt.data2;
    raw_event.priority = 2;  // Raw events after note-on/off at same tick
    events.push_back(raw_event);
  }

  // Include arc-driven Control-Change events from the track. The channel comes
  // from the track itself (CcEvent is channel-agnostic). CC events sort first
  // among events sharing a tick (priority -1) so an expression change takes
  // effect before any note-on at the same position. When cc_events is empty,
  // no events are added here and the output is byte-identical to the pre-CC
  // writer.
  events.reserve(events.size() + track.cc_events.size());
  for (const auto& cc : track.cc_events) {
    WriteEvent cc_event;
    cc_event.tick = cc.tick;
    cc_event.status = static_cast<uint8_t>(0xB0 | (track.channel & 0x0F));
    cc_event.data1 = cc.controller;
    cc_event.data2 = cc.value;
    cc_event.priority = -1;  // Control change before note-off/on at same tick
    events.push_back(cc_event);
  }

  // Sort by tick, then by priority (note-off before note-on).
  std::sort(events.begin(), events.end(), [](const WriteEvent& lhs, const WriteEvent& rhs) {
    if (lhs.tick != rhs.tick)
      return lhs.tick < rhs.tick;
    return lhs.priority < rhs.priority;
  });

  // Write events with delta times.
  uint32_t prev_tick = 0;
  for (const auto& evt : events) {
    uint32_t delta = evt.tick - prev_tick;
    writeVariableLength(track_buf, delta);
    track_buf.push_back(evt.status);
    track_buf.push_back(evt.data1 & 0x7F);
    track_buf.push_back(evt.data2 & 0x7F);
    prev_tick = evt.tick;
  }

  // End of Track meta-event
  writeVariableLength(track_buf, 0);
  track_buf.push_back(0xFF);
  track_buf.push_back(0x2F);
  track_buf.push_back(0x00);

  // Write MTrk chunk header + data
  data_.push_back('M');
  data_.push_back('T');
  data_.push_back('r');
  data_.push_back('k');
  writeBE32(data_, static_cast<uint32_t>(track_buf.size()));
  data_.insert(data_.end(), track_buf.begin(), track_buf.end());
}

void MidiWriter::writeMetadataTrack(const std::vector<TempoEvent>& tempo_events,
                                    const std::vector<TimeSignatureEvent>& time_sig_events,
                                    const KeySignature& key_signature,
                                    const std::string& metadata) {
  std::vector<uint8_t> track_buf;

  // Track name: "BACH"
  writeVariableLength(track_buf, 0);  // Delta time = 0
  track_buf.push_back(0xFF);          // Meta event
  track_buf.push_back(0x03);          // Track Name
  constexpr uint8_t kNameLen = 4;
  writeVariableLength(track_buf, kNameLen);
  track_buf.push_back('B');
  track_buf.push_back('A');
  track_buf.push_back('C');
  track_buf.push_back('H');

  // Embed metadata as a whole-file text event at tick 0 (delta 0).
  if (!metadata.empty()) {
    std::string text_payload = "BACH:" + metadata;
    writeVariableLength(track_buf, 0);
    track_buf.push_back(0xFF);
    track_buf.push_back(0x01);  // Text Event
    writeVariableLength(track_buf, static_cast<uint32_t>(text_payload.size()));
    for (char chr : text_payload) {
      track_buf.push_back(static_cast<uint8_t>(chr));
    }
  }

  // Tempo and time-signature changes share a single meta-event timeline and
  // must be written in one tick-ordered pass. Writing tempo and time signature
  // as two independent passes makes the time-signature delta underflow whenever
  // a later tempo change (e.g. a closing ritardando) is written before a
  // tick-0 time signature, corrupting every meta-event after it.
  struct MetaEvent {
    Tick tick;
    int order;                     // tie-break at equal tick: tempo (0) before time signature (1)
    std::vector<uint8_t> payload;  // event bytes excluding the leading delta
  };
  std::vector<MetaEvent> meta_events;

  // Key signature (FF 59 02): signed flats/sharps byte plus mode byte.
  const int8_t accidentals = keySignatureAccidentals(key_signature);
  meta_events.push_back({0,
                         2,
                         {0xFF, 0x59, 0x02, static_cast<uint8_t>(accidentals),
                          static_cast<uint8_t>(key_signature.is_minor ? 1 : 0)}});

  // Tempo map (FF 51 03). Default to 120 BPM at tick 0 when none supplied.
  std::vector<TempoEvent> tempos = tempo_events;
  if (tempos.empty()) {
    tempos.push_back({0, 120});
  }
  for (const auto& evt : tempos) {
    uint32_t usec_per_beat = kMicrosecondsPerMinute / evt.bpm;
    meta_events.push_back({evt.tick,
                           0,
                           {0xFF, 0x51, 0x03, static_cast<uint8_t>((usec_per_beat >> 16) & 0xFF),
                            static_cast<uint8_t>((usec_per_beat >> 8) & 0xFF),
                            static_cast<uint8_t>(usec_per_beat & 0xFF)}});
  }

  // Time signatures (FF 58 04). Default to 4/4 at tick 0 when none supplied.
  std::vector<TimeSignatureEvent> time_sigs = time_sig_events;
  if (time_sigs.empty()) {
    time_sigs.push_back({0, {4, 4}});
  }
  for (const auto& ts_evt : time_sigs) {
    // Denominator is encoded as log2: 4->2, 8->3, 2->1, 16->4.
    uint8_t denom_log2 = 0;
    uint8_t denom = ts_evt.time_sig.denominator;
    while (denom > 1) {
      denom >>= 1;
      ++denom_log2;
    }
    meta_events.push_back({ts_evt.tick,
                           1,
                           {0xFF, 0x58, 0x04, ts_evt.time_sig.numerator, denom_log2,
                            0x18,     // 24 MIDI clocks per metronome click
                            0x08}});  // 8 thirty-second notes per 24 MIDI clocks
  }

  std::stable_sort(meta_events.begin(), meta_events.end(),
                   [](const MetaEvent& lhs, const MetaEvent& rhs) {
                     if (lhs.tick != rhs.tick)
                       return lhs.tick < rhs.tick;
                     return lhs.order < rhs.order;
                   });

  uint32_t prev_tick = 0;
  for (const auto& evt : meta_events) {
    writeVariableLength(track_buf, evt.tick - prev_tick);
    track_buf.insert(track_buf.end(), evt.payload.begin(), evt.payload.end());
    prev_tick = evt.tick;
  }

  // End of Track
  writeVariableLength(track_buf, 0);
  track_buf.push_back(0xFF);
  track_buf.push_back(0x2F);
  track_buf.push_back(0x00);

  // Write MTrk chunk
  data_.push_back('M');
  data_.push_back('T');
  data_.push_back('r');
  data_.push_back('k');
  writeBE32(data_, static_cast<uint32_t>(track_buf.size()));
  data_.insert(data_.end(), track_buf.begin(), track_buf.end());
}

}  // namespace bach
