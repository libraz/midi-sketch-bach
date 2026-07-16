/// @file
/// @brief SMF Type 0/1 MIDI file reader implementation.

#include "midi/midi_reader.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <deque>
#include <limits>
#include <map>

#include "midi/midi_stream.h"

namespace bach {

// ---------------------------------------------------------------------------
// ParsedMidi
// ---------------------------------------------------------------------------

const ParsedTrack* ParsedMidi::getTrack(const std::string& name) const {
  for (const auto& track : tracks) {
    if (track.name == name) {
      return &track;
    }
  }
  return nullptr;
}

// ---------------------------------------------------------------------------
// MidiReader -- public
// ---------------------------------------------------------------------------

bool MidiReader::read(const std::string& path) {
  midi_ = ParsedMidi{};
  error_.clear();

  FILE* file = std::fopen(path.c_str(), "rb");
  if (!file) {
    error_ = "Failed to open file: " + path;
    return false;
  }

  // Determine file size.
  if (std::fseek(file, 0, SEEK_END) != 0) {
    std::fclose(file);
    error_ = "Failed to determine file size: " + path;
    return false;
  }
  const long file_size = std::ftell(file);
  if (std::fseek(file, 0, SEEK_SET) != 0) {
    std::fclose(file);
    error_ = "Failed to rewind file: " + path;
    return false;
  }

  if (file_size <= 0) {
    std::fclose(file);
    error_ = "File is empty or unreadable: " + path;
    return false;
  }

  if (static_cast<unsigned long>(file_size) > kMaxMidiInputBytes) {
    std::fclose(file);
    error_ = "MIDI file exceeds the 16 MiB input limit: " + path;
    return false;
  }

  std::vector<uint8_t> data(static_cast<size_t>(file_size));
  size_t bytes_read = std::fread(data.data(), 1, data.size(), file);
  std::fclose(file);

  if (bytes_read != data.size()) {
    error_ = "Failed to read complete file: " + path;
    return false;
  }

  return read(data);
}

bool MidiReader::read(const std::vector<uint8_t>& data) {
  midi_ = ParsedMidi{};
  error_.clear();

  if (data.size() < 14) {
    error_ = "Data too small to be a valid MIDI file";
    return false;
  }

  if (data.size() > kMaxMidiInputBytes) {
    error_ = "MIDI data exceeds the 16 MiB input limit";
    return false;
  }

  size_t offset = 0;
  if (!parseHeader(data.data(), data.size(), &offset)) {
    midi_ = ParsedMidi{};
    return false;
  }

  for (uint16_t idx = 0; idx < midi_.num_tracks; ++idx) {
    if (!parseTrack(data.data(), data.size(), offset)) {
      midi_ = ParsedMidi{};
      return false;
    }
  }

  return true;
}

// ---------------------------------------------------------------------------
// MidiReader -- private
// ---------------------------------------------------------------------------

/// @brief Parse the MThd header chunk and populate format, track count, and division.
bool MidiReader::parseHeader(const uint8_t* data, size_t size, size_t* track_offset) {
  if (!data || !track_offset || size < 14) {
    error_ = "Invalid MIDI header buffer";
    return false;
  }
  // Check "MThd" magic bytes.
  if (std::memcmp(data, "MThd", 4) != 0) {
    error_ = "Invalid MIDI file: missing MThd header";
    return false;
  }

  uint32_t header_len = readBE32(data, 4);
  if (header_len < 6 || header_len > size - 8) {
    error_ = "Invalid MIDI header length";
    return false;
  }

  midi_.format = readBE16(data, 8);
  midi_.num_tracks = readBE16(data, 10);
  midi_.division = readBE16(data, 12);

  // Validate format.
  if (midi_.format > 2) {
    error_ = "Unsupported MIDI format: " + std::to_string(midi_.format);
    return false;
  }

  *track_offset = 8 + static_cast<size_t>(header_len);

  return true;
}

/// @brief Parse a single MTrk chunk, converting MIDI events into NoteEvent objects.
bool MidiReader::parseTrack(const uint8_t* data, size_t size, size_t& offset) {
  // Check "MTrk" magic bytes.
  if (offset + 8 > size) {
    error_ = "Unexpected end of data before track chunk";
    return false;
  }

  if (std::memcmp(data + offset, "MTrk", 4) != 0) {
    error_ = "Invalid track chunk: missing MTrk header";
    return false;
  }

  uint32_t track_len = readBE32(data, offset + 4);
  offset += 8;  // Skip "MTrk" + length

  if (offset + track_len > size) {
    error_ = "Track chunk exceeds file size";
    return false;
  }

  size_t track_end = offset + track_len;
  ParsedTrack parsed_track;

  // Running status for channel messages.
  uint8_t running_status = 0;
  uint32_t abs_tick = 0;

  /// @brief Tracks an unmatched note-on event awaiting its corresponding note-off.
  struct PendingNote {
    uint32_t start_tick;
    uint8_t velocity;
  };
  std::map<uint16_t, std::deque<PendingNote>> pending_notes;
  bool has_channel = false;
  bool saw_end_of_track = false;

  const auto fail = [this](const char* message) {
    error_ = message;
    return false;
  };

  while (offset < track_end) {
    // Read delta time.
    uint32_t delta = 0;
    if (!readVariableLength(data, offset, track_end, &delta)) {
      return fail("Invalid or truncated event delta-time VLQ");
    }
    if (delta > std::numeric_limits<uint32_t>::max() - abs_tick) {
      return fail("MIDI event tick overflows uint32");
    }
    abs_tick += delta;

    if (offset >= track_end) {
      return fail("Track ends after delta time without an event");
    }

    uint8_t byte = data[offset];

    // Meta event
    if (byte == 0xFF) {
      ++offset;  // Skip 0xFF
      running_status = 0;
      if (offset >= track_end) {
        return fail("Truncated MIDI meta event type");
      }

      uint8_t meta_type = data[offset++];
      uint32_t meta_len = 0;
      if (!readVariableLength(data, offset, track_end, &meta_len)) {
        return fail("Invalid or truncated MIDI meta-event length");
      }
      if (meta_len > track_end - offset) {
        return fail("MIDI meta event exceeds track chunk");
      }

      if (meta_type == 0x03 && meta_len > 0) {
        // Track Name
        parsed_track.name.assign(reinterpret_cast<const char*>(data + offset), meta_len);
      } else if (meta_type == 0x51 && meta_len == 3) {
        // Tempo
        uint32_t usec_per_beat = (static_cast<uint32_t>(data[offset]) << 16) |
                                 (static_cast<uint32_t>(data[offset + 1]) << 8) |
                                 static_cast<uint32_t>(data[offset + 2]);
        if (usec_per_beat == 0) {
          return fail("MIDI tempo meta event contains zero microseconds per beat");
        }
        midi_.bpm = static_cast<uint16_t>(kMicrosecondsPerMinute / usec_per_beat);
      } else if (meta_type == 0x59 && meta_len == 2) {
        const int8_t accidentals = static_cast<int8_t>(data[offset]);
        const uint8_t mode = data[offset + 1];
        if (mode > 1 || !keySignatureFromMidi(accidentals, mode == 1, &midi_.key_signature)) {
          return fail("Invalid MIDI key-signature meta event");
        }
        midi_.has_key_signature = true;
      } else if (meta_type == 0x01 && meta_len > 0) {
        // Text Event -- check for BACH metadata prefix.
        std::string text(reinterpret_cast<const char*>(data + offset), meta_len);
        constexpr const char* kBachPrefix = "BACH:";
        constexpr size_t kPrefixLen = 5;
        if (text.size() >= kPrefixLen && text.compare(0, kPrefixLen, kBachPrefix) == 0) {
          midi_.metadata = text.substr(kPrefixLen);
        }
      } else if (meta_type == 0x2F) {
        // End of Track
        if (meta_len != 0) {
          return fail("End-of-track meta event must have zero length");
        }
        offset += meta_len;
        saw_end_of_track = true;
        if (offset != track_end) {
          return fail("Data remains after end-of-track meta event");
        }
        break;
      }

      offset += meta_len;
      continue;
    }

    // SysEx event
    if (byte == 0xF0 || byte == 0xF7) {
      ++offset;
      running_status = 0;
      uint32_t sysex_len = 0;
      if (!readVariableLength(data, offset, track_end, &sysex_len)) {
        return fail("Invalid or truncated SysEx length");
      }
      if (sysex_len > track_end - offset) {
        return fail("SysEx event exceeds track chunk");
      }
      offset += sysex_len;
      continue;
    }

    // Channel message
    uint8_t status;
    if (byte & 0x80) {
      // New status byte
      status = byte;
      if (status >= 0xF0) {
        return fail("Unsupported system status in MIDI track");
      }
      running_status = status;
      ++offset;
    } else {
      // Running status
      if (running_status == 0) {
        return fail("MIDI running status used before a channel status");
      }
      status = running_status;
    }

    uint8_t msg_type = status & 0xF0;
    uint8_t channel = status & 0x0F;

    // Read data bytes based on message type.
    if (msg_type == 0x90 || msg_type == 0x80) {
      // Note On / Note Off: 2 data bytes
      if (track_end - offset < 2) {
        return fail("Truncated MIDI note event");
      }
      const uint8_t pitch = data[offset++];
      const uint8_t velocity = data[offset++];
      if (pitch >= 0x80 || velocity >= 0x80) {
        return fail("MIDI note data byte has status bit set");
      }

      // Note On with velocity 0 is treated as Note Off.
      bool is_note_on = (msg_type == 0x90 && velocity > 0);

      if (is_note_on) {
        const uint16_t key = static_cast<uint16_t>(channel) * 128u + pitch;
        pending_notes[key].push_back({abs_tick, velocity});
        if (!has_channel) {
          parsed_track.channel = channel;
          has_channel = true;
        }
      } else {
        // Note Off: match with pending note-on.
        const uint16_t key = static_cast<uint16_t>(channel) * 128u + pitch;
        auto pending = pending_notes.find(key);
        if (pending != pending_notes.end() && !pending->second.empty()) {
          const PendingNote note_on = pending->second.front();
          pending->second.pop_front();
          NoteEvent note;
          note.start_tick = note_on.start_tick;
          note.duration = abs_tick - note_on.start_tick;
          note.pitch = pitch;
          note.velocity = note_on.velocity;
          note.voice = channel;
          note.source = BachNoteSource::Unknown;  // MIDI import: analysis only, not generation.
          parsed_track.notes.push_back(note);
          if (pending->second.empty()) {
            pending_notes.erase(pending);
          }
        } else {
          return fail("MIDI note-off has no matching note-on on its channel");
        }
      }
    } else if (msg_type == 0xC0 || msg_type == 0xD0) {
      // Program Change / Channel Pressure: 1 data byte
      if (offset >= track_end) {
        return fail("Truncated one-byte MIDI channel event");
      }
      const uint8_t data1 = data[offset++];
      if (data1 >= 0x80) {
        return fail("MIDI channel data byte has status bit set");
      }
      if (msg_type == 0xC0) {
        parsed_track.program = data1;
        parsed_track.channel = channel;
      }
    } else {
      // All other channel messages: 2 data bytes
      // (Control Change, Pitch Bend, Key Pressure, etc.)
      if (track_end - offset < 2) {
        return fail("Truncated two-byte MIDI channel event");
      }
      if (data[offset] >= 0x80 || data[offset + 1] >= 0x80) {
        return fail("MIDI channel data byte has status bit set");
      }
      offset += 2;
    }
  }

  if (!saw_end_of_track) {
    return fail("Track chunk is missing an end-of-track event");
  }
  if (!pending_notes.empty()) {
    return fail("Track ends with unmatched note-on events");
  }

  // Sort notes by start tick.
  std::sort(
      parsed_track.notes.begin(), parsed_track.notes.end(),
      [](const NoteEvent& lhs, const NoteEvent& rhs) { return lhs.start_tick < rhs.start_tick; });

  midi_.tracks.push_back(std::move(parsed_track));
  return true;
}

}  // namespace bach
