// Simplified MIDI reader for Bach. Reads SMF Type 1 files into ParsedMidi.

#ifndef BACH_MIDI_READER_H
#define BACH_MIDI_READER_H

#include <cstdint>
#include <string>
#include <vector>

#include "core/basic_types.h"

namespace bach {

/// A single parsed MIDI track with its associated channel, program, and notes.
struct ParsedTrack {
  std::string name;
  uint8_t channel = 0;
  uint8_t program = 0;
  std::vector<NoteEvent> notes;
};

/// Complete parsed representation of a Standard MIDI File.
struct ParsedMidi {
  uint16_t format = 0;
  uint16_t num_tracks = 0;
  uint16_t division = 480;
  uint16_t bpm = 120;
  std::vector<ParsedTrack> tracks;
  std::string metadata;  // BACH: metadata payload if present

  /// @brief Find a track by name.
  /// @param name Track name to search for.
  /// @return Pointer to the matching track, or nullptr if not found.
  const ParsedTrack* getTrack(const std::string& name) const;

  /// @brief Check whether BACH-specific metadata was found in the file.
  /// @return True if the metadata string is non-empty.
  bool hasBachMetadata() const { return !metadata.empty(); }
};

/// @brief MIDI file reader that parses SMF Type 0/1 files into ParsedMidi.
///
/// Handles note on/off pairing, running status, tempo meta-events,
/// and BACH metadata text events.
class MidiReader {
 public:
  MidiReader() = default;

  /// @brief Read and parse a MIDI file from disk.
  /// @param path File path to read.
  /// @return True on success. On failure, call getError() for details.
  bool read(const std::string& path);

  /// @brief Read and parse MIDI data from a byte buffer.
  /// @param data Raw MIDI file bytes.
  /// @return True on success. On failure, call getError() for details.
  bool read(const std::vector<uint8_t>& data);

  /// @brief Get the parsed MIDI data (valid after a successful read).
  /// @return Const reference to the ParsedMidi structure.
  const ParsedMidi& getParsedMidi() const { return midi_; }

  /// @brief Get the error message from the last failed read().
  /// @return Human-readable error string.
  const std::string& getError() const { return error_; }

 private:
  ParsedMidi midi_;
  std::string error_;

  /// Parse the MThd header chunk.
  bool parseHeader(const uint8_t* data, size_t size);

  /// Parse a single MTrk chunk starting at offset.
  bool parseTrack(const uint8_t* data, size_t size, size_t& offset);
};

}  // namespace bach

#endif  // BACH_MIDI_READER_H
