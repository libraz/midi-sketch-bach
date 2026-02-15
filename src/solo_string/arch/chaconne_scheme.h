// ChaconneScheme -- immutable harmonic progression for chaconne form.

#ifndef BACH_SOLO_STRING_ARCH_CHACONNE_SCHEME_H
#define BACH_SOLO_STRING_ARCH_CHACONNE_SCHEME_H

#include <cstdint>
#include <vector>

#include "analysis/fail_report.h"
#include "core/basic_types.h"
#include "harmony/chord_types.h"
#include "harmony/harmonic_event.h"
#include "harmony/harmonic_timeline.h"
#include "harmony/key.h"

namespace bach {

/// @brief A single chord definition within a harmonic scheme.
///
/// Each entry defines an abstract chord (degree + quality) at a beat position
/// within the cycle. Quality is explicit rather than derived from key mode,
/// allowing the scheme to precisely define the BWV1004 progression where
/// natural minor degrees (III, VII) differ from harmonic minor defaults.
struct SchemeEntry {
  ChordDegree degree;
  ChordQuality quality;
  /// Preferred inversion -- advisory for BassRealizer, not enforced by timeline.
  uint8_t preferred_inversion = 0;
  float weight = 1.0f;
  uint8_t position_beats;  ///< Beat offset from cycle start.
  uint8_t duration_beats;  ///< Duration in beats.
};

/// @brief Immutable harmonic progression defining the chaconne structure.
///
/// ChaconneScheme replaces the role of GroundBass for chaconne form: it defines
/// the harmonic foundation as an abstract degree-based chord sequence rather than
/// a fixed bass line. This separation allows BassRealizer to choose inversions
/// and voice leading freely while preserving the harmonic identity.
///
/// The scheme is immutable once constructed. All entries use abstract degrees
/// (ChordDegree) so the same scheme works for any key -- the actual pitches
/// are realized when toTimeline() is called with a specific KeySignature.
///
/// Standard BWV1004 D minor scheme: i-V-i-iv-VII-III-V (7 entries, 16 beats).
class ChaconneScheme {
 public:
  /// @brief Construct from a custom entry sequence.
  /// @param entries Vector of SchemeEntry defining the progression.
  explicit ChaconneScheme(std::vector<SchemeEntry> entries);

  /// @brief Default constructor (empty scheme).
  ChaconneScheme() = default;

  /// @brief Create the standard BWV1004-style D minor scheme.
  /// @return ChaconneScheme with 7 entries spanning 16 beats (4 bars).
  static ChaconneScheme createStandardDMinor();

  /// @brief Create the standard BWV1004 scheme for use with any key.
  ///
  /// Since SchemeEntry uses abstract degrees, the scheme itself is key-agnostic.
  /// This is functionally equivalent to createStandardDMinor(); the key is
  /// applied when toTimeline() is called.
  ///
  /// @param key_sig Target key signature (stored for documentation; does not
  ///        affect the returned scheme entries).
  /// @return ChaconneScheme with the standard BWV1004 progression.
  static ChaconneScheme createForKey(const KeySignature& key_sig);

  /// @brief Generate a HarmonicTimeline from this scheme.
  ///
  /// Realizes the abstract degree-based entries into concrete HarmonicEvents
  /// with MIDI pitches in the specified key. Each event's chord.inversion is
  /// always 0 (root position) -- the preferred_inversion field is only
  /// interpreted by BassRealizer downstream.
  ///
  /// @param key Key signature for pitch realization.
  /// @param duration Total duration for the timeline in ticks. If this differs
  ///        from the scheme's natural length (getLengthTicks()), beat positions
  ///        and durations are scaled proportionally.
  /// @return HarmonicTimeline with one event per SchemeEntry.
  HarmonicTimeline toTimeline(const KeySignature& key, Tick duration) const;

  /// @brief Verify that a timeline's chord sequence matches this scheme.
  ///
  /// Checks degree and quality of each event against the corresponding scheme
  /// entry. Inversion differences are allowed by design (BassRealizer may
  /// choose different inversions). Wrapper around verifyIntegrityReport().
  ///
  /// @param timeline The HarmonicTimeline to verify.
  /// @return true if all degree + quality pairs match, false otherwise.
  bool verifyIntegrity(const HarmonicTimeline& timeline) const;

  /// @brief Detailed integrity verification with FailReport.
  ///
  /// Checks:
  /// 1. Event count matches entry count (StructuralFail / Critical).
  /// 2. Each event's degree matches the entry (StructuralFail / Critical).
  /// 3. Each event's quality matches the entry (StructuralFail / Critical).
  /// 4. Functional harmony: first entry is tonic (MusicalFail / Warning).
  /// 5. Functional harmony: last entry is dominant (MusicalFail / Warning).
  ///
  /// @param timeline The HarmonicTimeline to verify.
  /// @return FailReport with all detected issues.
  FailReport verifyIntegrityReport(const HarmonicTimeline& timeline) const;

  /// @brief Total length of one cycle in ticks.
  /// @return Sum of all entry durations converted to ticks, or 0 if empty.
  Tick getLengthTicks() const;

  /// @brief Number of entries in the scheme.
  /// @return Entry count.
  size_t size() const;

  /// @brief Access the entry list.
  /// @return Const reference to the internal entry vector.
  const std::vector<SchemeEntry>& entries() const;

 private:
  std::vector<SchemeEntry> entries_;
};

}  // namespace bach

#endif  // BACH_SOLO_STRING_ARCH_CHACONNE_SCHEME_H
