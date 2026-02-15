/// @file
/// @brief SMF Type 0/1 MIDI file reader implementation.

#include "midi/midi_reader.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

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
  FILE* file = std::fopen(path.c_str(), "rb");
  if (!file) {
    error_ = "Failed to open file: " + path;
    return false;
  }

  // Determine file size.
  std::fseek(file, 0, SEEK_END);
  long file_size = std::ftell(file);
  std::fseek(file, 0, SEEK_SET);

  if (file_size <= 0) {
    std::fclose(file);
    error_ = "File is empty or unreadable: " + path;
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

  if (!parseHeader(data.data(), data.size())) {
    return false;
  }

  // Parse tracks starting after the header (14 bytes for MThd).
  size_t offset = 14;
  for (uint16_t idx = 0; idx < midi_.num_tracks; ++idx) {
    if (!parseTrack(data.data(), data.size(), offset)) {
      return false;
    }
  }

  return true;
}

// ---------------------------------------------------------------------------
// MidiReader -- private
// ---------------------------------------------------------------------------

/// @brief Parse the MThd header chunk and populate format, track count, and division.
bool MidiReader::parseHeader(const uint8_t* data, size_t size) {
  // Check "MThd" magic bytes.
  if (std::memcmp(data, "MThd", 4) != 0) {
    error_ = "Invalid MIDI file: missing MThd header";
    return false;
  }

  uint32_t header_len = readBE32(data, 4);
  if (header_len < 6 || size < 8 + header_len) {
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

  // Pending note-on events keyed by (channel << 8 | pitch).
  // Value is the absolute tick of the note-on.
  /// @brief Tracks an unmatched note-on event awaiting its corresponding note-off.
  struct PendingNote {
    uint32_t start_tick;
    uint8_t velocity;
    uint8_t voice;
  };
  // Use a flat array indexed by pitch (0-127) for single-channel tracks.
  // For multi-channel, we use a simple vector scan (tracks are small).
  std::vector<PendingNote> pending_notes(128, {0, 0, 0});
  bool has_pending[128] = {};

  while (offset < track_end) {
    // Read delta time.
    uint32_t delta = readVariableLength(data, offset, track_end);
    abs_tick += delta;

    if (offset >= track_end) break;

    uint8_t byte = data[offset];

    // Meta event
    if (byte == 0xFF) {
      ++offset;  // Skip 0xFF
      if (offset >= track_end) break;

      uint8_t meta_type = data[offset++];
      if (offset >= track_end) break;

      uint32_t meta_len = readVariableLength(data, offset, track_end);
      if (offset + meta_len > track_end) break;

      if (meta_type == 0x03 && meta_len > 0) {
        // Track Name
        parsed_track.name.assign(
            reinterpret_cast<const char*>(data + offset), meta_len);
      } else if (meta_type == 0x51 && meta_len == 3) {
        // Tempo
        uint32_t usec_per_beat =
            (static_cast<uint32_t>(data[offset]) << 16) |
            (static_cast<uint32_t>(data[offset + 1]) << 8) |
             static_cast<uint32_t>(data[offset + 2]);
        if (usec_per_beat > 0) {
          midi_.bpm = static_cast<uint16_t>(kMicrosecondsPerMinute / usec_per_beat);
        }
      } else if (meta_type == 0x01 && meta_len > 0) {
        // Text Event -- check for BACH metadata prefix.
        std::string text(reinterpret_cast<const char*>(data + offset), meta_len);
        constexpr const char* kBachPrefix = "BACH:";
        constexpr size_t kPrefixLen = 5;
        if (text.size() >= kPrefixLen &&
            text.compare(0, kPrefixLen, kBachPrefix) == 0) {
          midi_.metadata = text.substr(kPrefixLen);
        }
      } else if (meta_type == 0x2F) {
        // End of Track
        offset += meta_len;
        break;
      }

      offset += meta_len;
      continue;
    }

    // SysEx event
    if (byte == 0xF0 || byte == 0xF7) {
      ++offset;
      uint32_t sysex_len = readVariableLength(data, offset, track_end);
      offset += sysex_len;
      continue;
    }

    // Channel message
    uint8_t status;
    if (byte & 0x80) {
      // New status byte
      status = byte;
      running_status = status;
      ++offset;
    } else {
      // Running status
      status = running_status;
    }

    uint8_t msg_type = status & 0xF0;
    uint8_t channel = status & 0x0F;

    // Read data bytes based on message type.
    if (msg_type == 0x90 || msg_type == 0x80) {
      // Note On / Note Off: 2 data bytes
      if (offset + 1 >= track_end) break;
      uint8_t pitch = data[offset++] & 0x7F;
      uint8_t velocity = data[offset++] & 0x7F;

      // Note On with velocity 0 is treated as Note Off.
      bool is_note_on = (msg_type == 0x90 && velocity > 0);

      if (is_note_on) {
        pending_notes[pitch] = {abs_tick, velocity, 0};
        has_pending[pitch] = true;
        if (parsed_track.channel == 0) {
          parsed_track.channel = channel;
        }
      } else {
        // Note Off: match with pending note-on.
        if (has_pending[pitch]) {
          NoteEvent note;
          note.start_tick = pending_notes[pitch].start_tick;
          note.duration = abs_tick - pending_notes[pitch].start_tick;
          note.pitch = pitch;
          note.velocity = pending_notes[pitch].velocity;
          note.voice = 0;
          note.source = BachNoteSource::Unknown;  // MIDI import: analysis only, not generation.
          parsed_track.notes.push_back(note);
          has_pending[pitch] = false;
        }
      }
    } else if (msg_type == 0xC0 || msg_type == 0xD0) {
      // Program Change / Channel Pressure: 1 data byte
      if (offset >= track_end) break;
      uint8_t data1 = data[offset++] & 0x7F;
      if (msg_type == 0xC0) {
        parsed_track.program = data1;
        parsed_track.channel = channel;
      }
    } else {
      // All other channel messages: 2 data bytes
      // (Control Change, Pitch Bend, Key Pressure, etc.)
      if (offset + 1 >= track_end) break;
      offset += 2;
    }
  }

  // Advance offset to track end in case we broke out early.
  offset = track_end;

  // Sort notes by start tick.
  std::sort(parsed_track.notes.begin(), parsed_track.notes.end(),
            [](const NoteEvent& lhs, const NoteEvent& rhs) {
              return lhs.start_tick < rhs.start_tick;
            });

  midi_.tracks.push_back(std::move(parsed_track));
  return true;
}

}  // namespace bach
