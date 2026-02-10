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
  if (result < 0) result = 0;
  if (result > 127) result = 127;
  return static_cast<uint8_t>(result);
}

}  // namespace

MidiWriter::MidiWriter() = default;

void MidiWriter::build(const std::vector<Track>& tracks,
                        const std::vector<TempoEvent>& tempo_events,
                        Key key, const std::string& metadata) {
  data_.clear();

  // Count non-empty tracks, plus one for the metadata track.
  uint16_t num_content_tracks = 0;
  for (const auto& track : tracks) {
    if (!track.notes.empty() || !track.events.empty()) {
      ++num_content_tracks;
    }
  }
  uint16_t total_tracks = num_content_tracks + 1;  // +1 for metadata track

  writeHeader(total_tracks, kTicksPerBeat);
  writeMetadataTrack(tempo_events, metadata);

  for (const auto& track : tracks) {
    if (!track.notes.empty() || !track.events.empty()) {
      writeTrack(track, key);
    }
  }
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

  // Sort by tick, then by priority (note-off before note-on).
  std::sort(events.begin(), events.end(),
            [](const WriteEvent& lhs, const WriteEvent& rhs) {
              if (lhs.tick != rhs.tick) return lhs.tick < rhs.tick;
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

  // Sort tempo events by tick and write each as FF 51 03.
  std::vector<TempoEvent> sorted_events = tempo_events;
  std::sort(sorted_events.begin(), sorted_events.end(),
            [](const TempoEvent& lhs, const TempoEvent& rhs) {
              return lhs.tick < rhs.tick;
            });

  // If no events provided, write a default 120 BPM event at tick 0.
  if (sorted_events.empty()) {
    sorted_events.push_back({0, 120});
  }

  uint32_t prev_tick = 0;
  for (const auto& evt : sorted_events) {
    uint32_t delta = evt.tick - prev_tick;
    uint32_t usec_per_beat = kMicrosecondsPerMinute / evt.bpm;
    writeVariableLength(track_buf, delta);
    track_buf.push_back(0xFF);
    track_buf.push_back(0x51);
    track_buf.push_back(0x03);  // Length = 3 bytes
    track_buf.push_back(static_cast<uint8_t>((usec_per_beat >> 16) & 0xFF));
    track_buf.push_back(static_cast<uint8_t>((usec_per_beat >> 8) & 0xFF));
    track_buf.push_back(static_cast<uint8_t>(usec_per_beat & 0xFF));
    prev_tick = evt.tick;
  }

  // Time signature meta-event: FF 58 04 nn dd cc bb
  // 4/4 time: numerator=4, denominator=2 (2^2=4), 24 MIDI clocks/click, 8 32nds/beat
  writeVariableLength(track_buf, 0);
  track_buf.push_back(0xFF);
  track_buf.push_back(0x58);
  track_buf.push_back(0x04);  // Length = 4 bytes
  track_buf.push_back(0x04);  // Numerator: 4
  track_buf.push_back(0x02);  // Denominator: 2^2 = 4
  track_buf.push_back(0x18);  // 24 MIDI clocks per metronome click
  track_buf.push_back(0x08);  // 8 thirty-second notes per 24 MIDI clocks

  // Embed metadata as a text event if non-empty.
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
