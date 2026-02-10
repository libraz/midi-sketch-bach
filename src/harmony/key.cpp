// Implementation of key signature relationships and circle-of-fifths utilities.

#include "harmony/key.h"

#include <algorithm>
#include <cstdlib>

namespace bach {

// ---------------------------------------------------------------------------
// Key relationship functions
// ---------------------------------------------------------------------------

KeySignature getDominant(const KeySignature& key_sig) {
  // Dominant = perfect 5th above (+7 semitones), same mode.
  Key dom = static_cast<Key>((static_cast<uint8_t>(key_sig.tonic) + 7) % 12);
  return {dom, key_sig.is_minor};
}

KeySignature getSubdominant(const KeySignature& key_sig) {
  // Subdominant = perfect 4th above (+5 semitones), same mode.
  Key sub = static_cast<Key>((static_cast<uint8_t>(key_sig.tonic) + 5) % 12);
  return {sub, key_sig.is_minor};
}

KeySignature getRelative(const KeySignature& key_sig) {
  if (key_sig.is_minor) {
    // Relative major of minor key: 3 semitones UP.
    Key rel = static_cast<Key>((static_cast<uint8_t>(key_sig.tonic) + 3) % 12);
    return {rel, false};
  }
  // Relative minor of major key: 3 semitones DOWN (= +9 mod 12).
  Key rel = static_cast<Key>((static_cast<uint8_t>(key_sig.tonic) + 9) % 12);
  return {rel, true};
}

KeySignature getParallel(const KeySignature& key_sig) {
  // Same tonic, opposite mode.
  return {key_sig.tonic, !key_sig.is_minor};
}

// ---------------------------------------------------------------------------
// Circle of fifths
// ---------------------------------------------------------------------------

/// @brief Calculate the minimum number of fifths steps between two tonics.
/// @param tonic_a First tonic pitch class.
/// @param tonic_b Second tonic pitch class.
/// @return Steps on circle of fifths (0-6).
static int fifthsDistanceBetweenTonics(Key tonic_a, Key tonic_b) {
  int val_a = static_cast<int>(tonic_a);
  int val_b = static_cast<int>(tonic_b);

  // Each step on the circle of fifths is +7 semitones (mod 12).
  // Find minimum steps going clockwise or counterclockwise.
  int forward = 0;
  int current = val_a;
  for (int step = 0; step <= 6; ++step) {
    if (current == val_b) {
      forward = step;
      break;
    }
    current = (current + 7) % 12;
    forward = step + 1;
  }

  int backward = 0;
  current = val_a;
  for (int step = 0; step <= 6; ++step) {
    if (current == val_b) {
      backward = step;
      break;
    }
    current = (current + 5) % 12;  // Going backward on CoF = +5 semitones
    backward = step + 1;
  }

  return std::min(forward, backward);
}

int circleOfFifthsDistance(const KeySignature& lhs, const KeySignature& rhs) {
  if (lhs.is_minor == rhs.is_minor) {
    // Same mode: direct circle-of-fifths distance between tonics.
    return fifthsDistanceBetweenTonics(lhs.tonic, rhs.tonic);
  }

  // Different modes: route through the relative key.
  // The relative key relationship is "free" (distance 0 between a key
  // and its relative). So we convert one side to the same mode and measure.
  KeySignature lhs_converted = getRelative(lhs);
  // Now lhs_converted has the same mode as rhs.
  return fifthsDistanceBetweenTonics(lhs_converted.tonic, rhs.tonic);
}

bool isCloselyRelated(const KeySignature& lhs, const KeySignature& rhs) {
  return circleOfFifthsDistance(lhs, rhs) <= 1;
}

std::vector<KeySignature> getCloselyRelatedKeys(const KeySignature& key_sig) {
  std::vector<KeySignature> result;
  result.reserve(6);

  // Self.
  result.push_back(key_sig);

  // Dominant (same mode).
  KeySignature dominant = getDominant(key_sig);
  result.push_back(dominant);

  // Subdominant (same mode).
  KeySignature subdominant = getSubdominant(key_sig);
  result.push_back(subdominant);

  // Relative (opposite mode).
  KeySignature relative = getRelative(key_sig);
  result.push_back(relative);

  // Parallel (same tonic, opposite mode).
  KeySignature parallel = getParallel(key_sig);
  // Only add if not already present (e.g. parallel might equal relative in some cases).
  bool parallel_already_present = false;
  for (const auto& existing : result) {
    if (existing == parallel) {
      parallel_already_present = true;
      break;
    }
  }
  if (!parallel_already_present) {
    result.push_back(parallel);
  }

  // Dominant of relative (common closely-related key in Bach).
  KeySignature dom_of_relative = getDominant(relative);
  bool dom_rel_already_present = false;
  for (const auto& existing : result) {
    if (existing == dom_of_relative) {
      dom_rel_already_present = true;
      break;
    }
  }
  if (!dom_rel_already_present) {
    result.push_back(dom_of_relative);
  }

  return result;
}

// ---------------------------------------------------------------------------
// Pitch and string utilities
// ---------------------------------------------------------------------------

uint8_t tonicPitch(Key key, int octave) {
  // MIDI note = (octave + 1) * 12 + pitch_class
  int midi = (octave + 1) * 12 + static_cast<int>(key);
  if (midi < 0) return 0;
  if (midi > 127) return 127;
  return static_cast<uint8_t>(midi);
}

/// @brief Parse the tonic key from a note name prefix.
/// @param name Lowercase note name such as "c", "cs", "eb", "fs".
/// @return Parsed Key, or Key::C on failure.
static Key parseTonicFromName(const std::string& name) {
  if (name == "c")  return Key::C;
  if (name == "cs" || name == "c#") return Key::Cs;
  if (name == "d")  return Key::D;
  if (name == "eb" || name == "d#") return Key::Eb;
  if (name == "e")  return Key::E;
  if (name == "f")  return Key::F;
  if (name == "fs" || name == "f#") return Key::Fs;
  if (name == "g")  return Key::G;
  if (name == "ab" || name == "g#") return Key::Ab;
  if (name == "a")  return Key::A;
  if (name == "bb" || name == "a#") return Key::Bb;
  if (name == "b")  return Key::B;
  return Key::C;
}

KeySignature keySignatureFromString(const std::string& str) {
  KeySignature result;
  result.tonic = Key::C;
  result.is_minor = false;

  // Find the underscore separator.
  auto underscore_pos = str.find('_');
  if (underscore_pos == std::string::npos) {
    // No underscore: try to parse just the note name, assume major.
    std::string lower_str;
    lower_str.reserve(str.size());
    for (char chr : str) {
      lower_str.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(chr))));
    }
    result.tonic = parseTonicFromName(lower_str);
    return result;
  }

  // Extract note name (before underscore) and mode (after underscore).
  std::string note_part = str.substr(0, underscore_pos);
  std::string mode_part = str.substr(underscore_pos + 1);

  // Convert to lowercase for parsing.
  std::string lower_note;
  lower_note.reserve(note_part.size());
  for (char chr : note_part) {
    lower_note.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(chr))));
  }

  std::string lower_mode;
  lower_mode.reserve(mode_part.size());
  for (char chr : mode_part) {
    lower_mode.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(chr))));
  }

  result.tonic = parseTonicFromName(lower_note);
  result.is_minor = (lower_mode == "minor");

  return result;
}

std::string keySignatureToString(const KeySignature& key_sig) {
  std::string result(keyToString(key_sig.tonic));
  result += key_sig.is_minor ? "_minor" : "_major";
  return result;
}

}  // namespace bach
