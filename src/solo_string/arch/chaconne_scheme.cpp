// Implementation of ChaconneScheme -- immutable harmonic progression for chaconne form.

#include "solo_string/arch/chaconne_scheme.h"

#include <algorithm>
#include <string>

#include "core/pitch_utils.h"

namespace bach {

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

ChaconneScheme::ChaconneScheme(std::vector<SchemeEntry> entries)
    : entries_(std::move(entries)) {}

// ---------------------------------------------------------------------------
// Factory methods
// ---------------------------------------------------------------------------

ChaconneScheme ChaconneScheme::createStandardDMinor() {
  // BWV1004-style chaconne progression: i - V - i - iv - VII - III - V
  // 7 entries spanning 16 beats (4 bars in 4/4).
  //
  // Degree mapping for D minor context:
  //   i   = ChordDegree::I,    Minor   (D minor tonic)
  //   V   = ChordDegree::V,    Major   (A major dominant)
  //   iv  = ChordDegree::IV,   Minor   (G minor subdominant)
  //   VII = ChordDegree::bVII, Major   (C major subtonic, natural minor VII)
  //   III = ChordDegree::iii,  Major   (F major mediant, natural minor III)
  std::vector<SchemeEntry> entries;
  entries.reserve(7);

  // position_beats: 0  4  6  8  10  12  14  (total = 16 beats)
  entries.push_back({ChordDegree::I,    ChordQuality::Minor, 0, 1.0f,   0, 4});  // i
  entries.push_back({ChordDegree::V,    ChordQuality::Major, 0, 0.75f,  4, 2});  // V
  entries.push_back({ChordDegree::I,    ChordQuality::Minor, 0, 1.0f,   6, 2});  // i
  entries.push_back({ChordDegree::IV,   ChordQuality::Minor, 0, 0.5f,   8, 2});  // iv
  entries.push_back({ChordDegree::bVII, ChordQuality::Major, 0, 0.5f,  10, 2});  // VII
  entries.push_back({ChordDegree::iii,  ChordQuality::Major, 0, 0.5f,  12, 2});  // III
  entries.push_back({ChordDegree::V,    ChordQuality::Major, 0, 0.75f, 14, 2});  // V (cadential)

  return ChaconneScheme(std::move(entries));
}

ChaconneScheme ChaconneScheme::createForKey(const KeySignature& /*key_sig*/) {
  // SchemeEntry uses abstract degrees, so the scheme is key-agnostic.
  // The key is applied when toTimeline() is called.
  return createStandardDMinor();
}

// ---------------------------------------------------------------------------
// Timeline generation
// ---------------------------------------------------------------------------

HarmonicTimeline ChaconneScheme::toTimeline(const KeySignature& key,
                                             Tick duration) const {
  HarmonicTimeline timeline;
  if (entries_.empty()) {
    return timeline;
  }

  constexpr int kChordOctave = 4;
  constexpr int kBassOctave = 2;

  Tick natural_length = getLengthTicks();

  for (const auto& entry : entries_) {
    Tick tick = static_cast<Tick>(entry.position_beats) * kTicksPerBeat;
    Tick end_tick = tick + static_cast<Tick>(entry.duration_beats) * kTicksPerBeat;

    // Scale to target duration if different from natural length.
    if (duration != natural_length && natural_length > 0) {
      tick = tick * duration / natural_length;
      end_tick = end_tick * duration / natural_length;
    }

    // Compute semitone offset from tonic using the appropriate scale context.
    uint8_t semitone_offset = key.is_minor ? degreeMinorSemitones(entry.degree)
                                           : degreeSemitones(entry.degree);

    // Root pitch at chord octave.
    int root_midi = (kChordOctave + 1) * 12 +
                    static_cast<int>(key.tonic) + semitone_offset;

    Chord chord;
    chord.degree = entry.degree;
    chord.quality = entry.quality;  // Explicit from scheme, not derived from key.
    chord.root_pitch = clampPitch(root_midi, 0, 127);
    chord.inversion = 0;  // Always root position in timeline.

    // Bass pitch = root at bass octave.
    int bass_midi = (kBassOctave + 1) * 12 +
                    static_cast<int>(key.tonic) + semitone_offset;

    HarmonicEvent event;
    event.tick = tick;
    event.end_tick = end_tick;
    event.key = key.tonic;
    event.is_minor = key.is_minor;
    event.chord = chord;
    event.bass_pitch = clampPitch(bass_midi, 0, 127);
    event.weight = entry.weight;
    event.is_immutable = true;  // Scheme-derived events are immutable.

    timeline.addEvent(event);
  }

  return timeline;
}

// ---------------------------------------------------------------------------
// Integrity verification
// ---------------------------------------------------------------------------

bool ChaconneScheme::verifyIntegrity(const HarmonicTimeline& timeline) const {
  return !verifyIntegrityReport(timeline).hasCritical();
}

FailReport ChaconneScheme::verifyIntegrityReport(
    const HarmonicTimeline& timeline) const {
  FailReport report;
  const auto& events = timeline.events();

  // Check 1: Event count must match entry count.
  if (events.size() != entries_.size()) {
    FailIssue issue;
    issue.kind = FailKind::StructuralFail;
    issue.severity = FailSeverity::Critical;
    issue.rule = "scheme_event_count_mismatch";
    issue.description = "Expected " + std::to_string(entries_.size()) +
                        " harmonic events, found " +
                        std::to_string(events.size());
    report.addIssue(issue);
    return report;  // Cannot check per-entry matches if counts differ.
  }

  // Check 2-3: Per-entry degree and quality match.
  for (size_t idx = 0; idx < entries_.size(); ++idx) {
    const auto& entry = entries_[idx];
    const auto& event = events[idx];

    if (event.chord.degree != entry.degree) {
      FailIssue issue;
      issue.kind = FailKind::StructuralFail;
      issue.severity = FailSeverity::Critical;
      issue.tick = event.tick;
      issue.bar = static_cast<uint8_t>(tickToBar(event.tick));
      issue.beat = beatInBar(event.tick);
      issue.rule = "scheme_degree_mismatch";
      issue.description = "Entry " + std::to_string(idx) + ": expected degree " +
                          chordDegreeToString(entry.degree) + ", found " +
                          chordDegreeToString(event.chord.degree);
      report.addIssue(issue);
    }

    if (event.chord.quality != entry.quality) {
      FailIssue issue;
      issue.kind = FailKind::StructuralFail;
      issue.severity = FailSeverity::Critical;
      issue.tick = event.tick;
      issue.bar = static_cast<uint8_t>(tickToBar(event.tick));
      issue.beat = beatInBar(event.tick);
      issue.rule = "scheme_quality_mismatch";
      issue.description = "Entry " + std::to_string(idx) + ": expected quality " +
                          chordQualityToString(entry.quality) + ", found " +
                          chordQualityToString(event.chord.quality);
      report.addIssue(issue);
    }
  }

  // Check 4: Functional harmony -- first entry should be tonic (degree I).
  if (!entries_.empty() && entries_.front().degree != ChordDegree::I) {
    FailIssue issue;
    issue.kind = FailKind::MusicalFail;
    issue.severity = FailSeverity::Warning;
    issue.tick = 0;
    issue.rule = "scheme_missing_tonic_opening";
    issue.description = "Scheme does not begin on tonic (I); starts on " +
                        std::string(chordDegreeToString(entries_.front().degree));
    report.addIssue(issue);
  }

  // Check 5: Functional harmony -- last entry should be dominant (degree V)
  // to enable V->i resolution across cycle boundaries.
  if (!entries_.empty() && entries_.back().degree != ChordDegree::V) {
    FailIssue issue;
    issue.kind = FailKind::MusicalFail;
    issue.severity = FailSeverity::Warning;
    issue.tick = events.empty() ? 0 : events.back().tick;
    issue.rule = "scheme_missing_cadential_dominant";
    issue.description = "Scheme does not end on dominant (V); ends on " +
                        std::string(chordDegreeToString(entries_.back().degree));
    report.addIssue(issue);
  }

  return report;
}

// ---------------------------------------------------------------------------
// Accessors
// ---------------------------------------------------------------------------

Tick ChaconneScheme::getLengthTicks() const {
  if (entries_.empty()) {
    return 0;
  }

  // Find the maximum extent: position_beats + duration_beats across all entries.
  uint32_t max_end_beats = 0;
  for (const auto& entry : entries_) {
    uint32_t end_beats = static_cast<uint32_t>(entry.position_beats) +
                         static_cast<uint32_t>(entry.duration_beats);
    if (end_beats > max_end_beats) {
      max_end_beats = end_beats;
    }
  }

  return static_cast<Tick>(max_end_beats) * kTicksPerBeat;
}

size_t ChaconneScheme::size() const {
  return entries_.size();
}

const std::vector<SchemeEntry>& ChaconneScheme::entries() const {
  return entries_;
}

}  // namespace bach
