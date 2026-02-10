/// @file
/// @brief Binary MIDI stream helper implementations (VLQ, big-endian I/O).

#include "midi/midi_stream.h"

namespace bach {

/// @brief Encode a value as a MIDI variable-length quantity and append to buf.
/// @param buf Destination byte buffer.
/// @param value Value to encode (clamped to 0x0FFFFFFF per MIDI spec).
void writeVariableLength(std::vector<uint8_t>& buf, uint32_t value) {
  // VLQ encoding: 7 bits per byte, MSB continuation flag.
  // Max 4 bytes for values up to 0x0FFFFFFF.
  if (value > 0x0FFFFFFF) {
    value = 0x0FFFFFFF;  // Clamp to MIDI spec maximum
  }

  // Build bytes in reverse order, then push in correct order.
  uint8_t encoded[4];
  int num_bytes = 0;

  encoded[num_bytes++] = static_cast<uint8_t>(value & 0x7F);
  value >>= 7;

  while (value > 0) {
    encoded[num_bytes++] = static_cast<uint8_t>((value & 0x7F) | 0x80);
    value >>= 7;
  }

  // Push in reverse order (most significant byte first).
  for (int idx = num_bytes - 1; idx >= 0; --idx) {
    buf.push_back(encoded[idx]);
  }
}

/// @brief Decode a MIDI variable-length quantity from a byte stream.
/// @param data Pointer to the raw byte stream.
/// @param offset Current read position; advanced past the VLQ bytes on return.
/// @param max_size Upper bound of readable bytes.
/// @return Decoded integer value.
uint32_t readVariableLength(const uint8_t* data, size_t& offset, size_t max_size) {
  uint32_t result = 0;
  int bytes_read = 0;
  constexpr int kMaxVlqBytes = 4;

  while (offset < max_size && bytes_read < kMaxVlqBytes) {
    uint8_t byte = data[offset++];
    ++bytes_read;
    result = (result << 7) | static_cast<uint32_t>(byte & 0x7F);
    if ((byte & 0x80) == 0) {
      return result;
    }
  }

  // Reached max bytes or end of data -- return what we have.
  return result;
}

/// @brief Write a 16-bit value in big-endian order to buf.
/// @param buf Destination byte buffer.
/// @param value 16-bit value to write.
void writeBE16(std::vector<uint8_t>& buf, uint16_t value) {
  buf.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
  buf.push_back(static_cast<uint8_t>(value & 0xFF));
}

/// @brief Write a 32-bit value in big-endian order to buf.
/// @param buf Destination byte buffer.
/// @param value 32-bit value to write.
void writeBE32(std::vector<uint8_t>& buf, uint32_t value) {
  buf.push_back(static_cast<uint8_t>((value >> 24) & 0xFF));
  buf.push_back(static_cast<uint8_t>((value >> 16) & 0xFF));
  buf.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
  buf.push_back(static_cast<uint8_t>(value & 0xFF));
}

/// @brief Read a 16-bit big-endian value from a byte buffer.
/// @param data Pointer to the raw byte stream.
/// @param offset Byte offset to read from.
/// @return Decoded 16-bit value.
uint16_t readBE16(const uint8_t* data, size_t offset) {
  return static_cast<uint16_t>(
      (static_cast<uint16_t>(data[offset]) << 8) |
       static_cast<uint16_t>(data[offset + 1]));
}

/// @brief Read a 32-bit big-endian value from a byte buffer.
/// @param data Pointer to the raw byte stream.
/// @param offset Byte offset to read from.
/// @return Decoded 32-bit value.
uint32_t readBE32(const uint8_t* data, size_t offset) {
  return (static_cast<uint32_t>(data[offset]) << 24) |
         (static_cast<uint32_t>(data[offset + 1]) << 16) |
         (static_cast<uint32_t>(data[offset + 2]) << 8) |
          static_cast<uint32_t>(data[offset + 3]);
}

}  // namespace bach
