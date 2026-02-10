// Helper for reading/writing binary MIDI data (variable-length quantities,
// big-endian integers).

#ifndef BACH_MIDI_STREAM_H
#define BACH_MIDI_STREAM_H

#include <cstdint>
#include <vector>

namespace bach {

/// Microseconds per minute constant for MIDI tempo meta-events.
constexpr uint32_t kMicrosecondsPerMinute = 60000000;

/// @brief Write a variable-length quantity (VLQ) to a byte buffer.
/// @param buf Destination buffer (bytes are appended).
/// @param value The unsigned value to encode (max 0x0FFFFFFF).
void writeVariableLength(std::vector<uint8_t>& buf, uint32_t value);

/// @brief Read a variable-length quantity from raw MIDI data.
/// @param data Pointer to the raw byte stream.
/// @param offset Current read position; advanced past the VLQ on return.
/// @param max_size Total size of the data buffer (bounds check).
/// @return Decoded unsigned value, or 0 if the read would exceed max_size.
uint32_t readVariableLength(const uint8_t* data, size_t& offset, size_t max_size);

/// @brief Write a big-endian uint16 to a byte buffer.
/// @param buf Destination buffer (2 bytes appended).
/// @param value The 16-bit value.
void writeBE16(std::vector<uint8_t>& buf, uint16_t value);

/// @brief Write a big-endian uint32 to a byte buffer.
/// @param buf Destination buffer (4 bytes appended).
/// @param value The 32-bit value.
void writeBE32(std::vector<uint8_t>& buf, uint32_t value);

/// @brief Read a big-endian uint16 from raw data at a given offset.
/// @param data Pointer to the raw byte stream.
/// @param offset Byte position to read from.
/// @return Decoded 16-bit value.
uint16_t readBE16(const uint8_t* data, size_t offset);

/// @brief Read a big-endian uint32 from raw data at a given offset.
/// @param data Pointer to the raw byte stream.
/// @param offset Byte position to read from.
/// @return Decoded 32-bit value.
uint32_t readBE32(const uint8_t* data, size_t offset);

}  // namespace bach

#endif  // BACH_MIDI_STREAM_H
