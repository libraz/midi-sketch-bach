// Simplified MIDI writer for Bach. Writes SMF Type 1 files from Track objects.

#ifndef BACH_MIDI_WRITER_H
#define BACH_MIDI_WRITER_H

#include <cstdint>
#include <string>
#include <vector>

#include "core/basic_types.h"
#include "midi/midi_stream.h"

namespace bach {

/// @brief MIDI file writer that produces Standard MIDI File (SMF) Type 1 output.
///
/// Converts internal Track representations (NoteEvent-based, C-major) into
/// proper SMF binary data with key transposition, tempo, and metadata embedding.
class MidiWriter {
 public:
  MidiWriter();

  /// @brief Build complete MIDI data from tracks.
  /// @param tracks Vector of Track objects containing notes and events.
  /// @param tempo_events Tempo map: sorted vector of TempoEvent (tick, bpm).
  ///        Must contain at least one event. First event should be at tick 0.
  /// @param key Output key for transposition (C = no transposition).
  /// @param metadata Optional JSON metadata string to embed as a text event.
  void build(const std::vector<Track>& tracks,
             const std::vector<TempoEvent>& tempo_events,
             Key key = Key::C, const std::string& metadata = "");

  /// @brief Get the binary MIDI data after build().
  /// @return Byte vector containing complete SMF Type 1 data.
  std::vector<uint8_t> toBytes() const;

  /// @brief Write built MIDI data to a file.
  /// @param path Output file path.
  /// @return True if the file was written successfully.
  bool writeToFile(const std::string& path) const;

 private:
  std::vector<uint8_t> data_;

  /// Write the MThd (file header) chunk.
  void writeHeader(uint16_t num_tracks, uint16_t division);

  /// Write a single track as an MTrk chunk with note events.
  void writeTrack(const Track& track, Key key);

  /// Write the metadata track (tempo map, time signature, BACH metadata text).
  void writeMetadataTrack(const std::vector<TempoEvent>& tempo_events,
                          const std::string& metadata);
};

}  // namespace bach

#endif  // BACH_MIDI_WRITER_H
