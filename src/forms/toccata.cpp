// Implementation of the organ toccata free section generator (BWV 565 style).

#include "forms/toccata.h"

#include <algorithm>
#include <cmath>
#include <random>
#include <vector>

#include "core/gm_program.h"
#include "core/note_creator.h"
#include "core/pitch_utils.h"
#include "core/rng_util.h"
#include "core/scale.h"
#include "harmony/chord_types.h"
#include "harmony/key.h"
#include "harmony/harmonic_event.h"
#include "ornament/ornament_engine.h"
#include "organ/organ_techniques.h"
#include "organ/registration.h"

namespace bach {

namespace {

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

using namespace duration;

constexpr uint8_t kGreatChannel = 0;
constexpr uint8_t kSwellChannel = 1;
constexpr uint8_t kPedalChannel = 3;

constexpr uint8_t kMinVoices = 2;
constexpr uint8_t kMaxVoices = 5;

constexpr float kOpeningProportion = 0.25f;
constexpr float kRecitativeProportion = 0.50f;

// ---------------------------------------------------------------------------
// Track creation
// ---------------------------------------------------------------------------

std::vector<Track> createToccataTracks(uint8_t num_voices) {
  std::vector<Track> tracks;
  tracks.reserve(num_voices);

  struct TrackSpec {
    uint8_t channel;
    uint8_t program;
    const char* name;
  };

  static constexpr TrackSpec kSpecs[] = {
      {kGreatChannel, GmProgram::kChurchOrgan, "Manual I (Great)"},
      {kSwellChannel, GmProgram::kReedOrgan, "Manual II (Swell)"},
      {kPedalChannel, GmProgram::kChurchOrgan, "Pedal"},
      {2, GmProgram::kChurchOrgan, "Manual III (Positiv)"},
      {4, GmProgram::kChurchOrgan, "Manual IV"},
  };

  for (uint8_t idx = 0; idx < num_voices && idx < 5; ++idx) {
    Track track;
    track.channel = kSpecs[idx].channel;
    track.program = kSpecs[idx].program;
    track.name = kSpecs[idx].name;
    tracks.push_back(track);
  }

  return tracks;
}

// ---------------------------------------------------------------------------
// Pitch range helpers
// ---------------------------------------------------------------------------

uint8_t getToccataLowPitch(uint8_t voice_idx) {
  switch (voice_idx) {
    case 0: return organ_range::kManual1Low;
    case 1: return organ_range::kManual2Low;
    case 2: return organ_range::kPedalLow;
    case 3: return organ_range::kManual3Low;
    default: return organ_range::kManual1Low;
  }
}

uint8_t getToccataHighPitch(uint8_t voice_idx) {
  switch (voice_idx) {
    case 0: return organ_range::kManual1High;
    case 1: return organ_range::kManual2High;
    case 2: return organ_range::kPedalHigh;
    case 3: return organ_range::kManual3High;
    default: return organ_range::kManual1High;
  }
}

// ---------------------------------------------------------------------------
// NoteEvent creation helper
// ---------------------------------------------------------------------------

NoteEvent makeNote(Tick tick, Tick dur, uint8_t pitch, uint8_t voice,
                   BachNoteSource source = BachNoteSource::FreeCounterpoint) {
  NoteEvent n;
  n.start_tick = tick;
  n.duration = dur;
  n.pitch = pitch;
  n.velocity = kOrganVelocity;
  n.voice = voice;
  n.source = source;
  return n;
}

// ---------------------------------------------------------------------------
// Toccata harmonic plan -- baroque-faithful chord progression
// ---------------------------------------------------------------------------

HarmonicTimeline buildToccataHarmonicPlan(
    const KeySignature& key_sig,
    Tick opening_start, Tick opening_end,
    Tick recit_start, Tick recit_end,
    Tick drive_start, Tick drive_end) {
  HarmonicTimeline timeline;
  uint8_t tpc = static_cast<uint8_t>(key_sig.tonic);
  bool minor = key_sig.is_minor;

  auto addChord = [&](Tick tick, Tick end_tick, ChordDegree degree,
                      ChordQuality quality, int root_offset,
                      float weight = 1.0f) {
    HarmonicEvent e;
    e.tick = tick;
    e.end_tick = end_tick;
    e.key = key_sig.tonic;
    e.is_minor = minor;
    e.chord.degree = degree;
    e.chord.quality = quality;
    e.chord.root_pitch = static_cast<uint8_t>((tpc + root_offset) % 12);
    e.bass_pitch = clampPitch(36 + (tpc + root_offset) % 12,
                              organ_range::kPedalLow, organ_range::kPedalHigh);
    e.weight = weight;
    timeline.addEvent(e);
  };

  // Opening: V -> i -> V (dominant opening, BWV 565 style)
  Tick o_dur = opening_end - opening_start;
  Tick o_mid = opening_start + o_dur / 2;
  Tick o_late = opening_start + o_dur * 4 / 5;

  addChord(opening_start, o_mid,
           ChordDegree::V, ChordQuality::Major, 7);
  addChord(o_mid, o_late,
           ChordDegree::I, minor ? ChordQuality::Minor : ChordQuality::Major, 0);
  addChord(o_late, opening_end,
           ChordDegree::V, ChordQuality::Major, 7);

  // Recitative: i -> viio7/V -> V -> VI -> iv -> viio7 -> V
  Tick r_dur = recit_end - recit_start;
  Tick r15 = recit_start + r_dur * 15 / 100;
  Tick r30 = recit_start + r_dur * 30 / 100;
  Tick r45 = recit_start + r_dur * 45 / 100;
  Tick r60 = recit_start + r_dur * 60 / 100;
  Tick r75 = recit_start + r_dur * 75 / 100;
  Tick r90 = recit_start + r_dur * 90 / 100;

  addChord(recit_start, r15,
           ChordDegree::I, minor ? ChordQuality::Minor : ChordQuality::Major, 0);
  addChord(r15, r30,
           ChordDegree::viiDim, ChordQuality::Diminished7, 6);  // viio7/V
  addChord(r30, r45,
           ChordDegree::V, ChordQuality::Major, 7);
  addChord(r45, r60,
           ChordDegree::vi, minor ? ChordQuality::Major : ChordQuality::Minor,
           minor ? 8 : 9);  // VI (deceptive)
  addChord(r60, r75,
           ChordDegree::IV, minor ? ChordQuality::Minor : ChordQuality::Major, 5);
  addChord(r75, r90,
           ChordDegree::viiDim, ChordQuality::Diminished7, 11);  // viio7
  addChord(r90, recit_end,
           ChordDegree::V, ChordQuality::Major, 7);

  // Drive: i -> iv -> V/V -> V7 -> I (Picardy third)
  Tick d_dur = drive_end - drive_start;
  Tick d25 = drive_start + d_dur * 25 / 100;
  Tick d50 = drive_start + d_dur * 50 / 100;
  Tick d75 = drive_start + d_dur * 75 / 100;
  Tick d95 = drive_start + d_dur * 95 / 100;

  addChord(drive_start, d25,
           ChordDegree::I, minor ? ChordQuality::Minor : ChordQuality::Major, 0);
  addChord(d25, d50,
           ChordDegree::IV, minor ? ChordQuality::Minor : ChordQuality::Major, 5);
  addChord(d50, d75,
           ChordDegree::V_of_V, ChordQuality::Major, 2);
  addChord(d75, d95,
           ChordDegree::V, ChordQuality::Dominant7, 7);
  // Picardy third: always Major for final chord
  addChord(d95, drive_end,
           ChordDegree::I, ChordQuality::Major, 0, 1.5f);

  return timeline;
}

// ---------------------------------------------------------------------------
// Opening helpers
// ---------------------------------------------------------------------------

/// @brief Generate a mordent wave pattern: held note + mordent + descending scale.
/// @return Tick position after the wave ends.
Tick generateMordentWave(uint8_t start_pitch,
                         const std::vector<uint8_t>& scale_tones,
                         Tick start_tick, Tick section_end,
                         std::vector<NoteEvent>& notes,
                         uint8_t low0, uint8_t high0,
                         uint8_t low1, uint8_t high1) {
  if (scale_tones.empty() || start_tick >= section_end) return start_tick;

  Tick tick = start_tick;
  uint8_t p0 = clampPitch(start_pitch, low0, high0);
  uint8_t p1 = clampPitch(static_cast<int>(start_pitch) - 12, low1, high1);

  // 1. Held note (dotted quarter) -- parallel octaves on voice 0 + voice 1
  Tick held_dur = std::min(kDottedQuarter, section_end - tick);
  if (held_dur == 0) return tick;
  notes.push_back(makeNote(tick, held_dur, p0, 0));
  notes.push_back(makeNote(tick, held_dur, p1, 1));
  tick += held_dur;
  if (tick >= section_end) return tick;

  // 2. Mordent: main -> diatonic lower neighbor -> main (3 x 32nd)
  // Find the scale tone just below the starting pitch for each voice.
  uint8_t lower0 = clampPitch(static_cast<int>(p0) - 1, low0, high0);
  for (size_t si = 0; si < scale_tones.size(); ++si) {
    if (scale_tones[si] >= p0 && si > 0) {
      lower0 = scale_tones[si - 1];
      break;
    }
  }
  uint8_t lower1 = clampPitch(static_cast<int>(lower0) - 12, low1, high1);
  uint8_t mordent_pitches0[] = {p0, lower0, p0};
  uint8_t mordent_pitches1[] = {p1, lower1, p1};

  for (int i = 0; i < 3 && tick < section_end; ++i) {
    Tick dur = std::min(kThirtySecondNote, section_end - tick);
    if (dur == 0) break;
    notes.push_back(makeNote(tick, dur, mordent_pitches0[i], 0));
    notes.push_back(makeNote(tick, dur, mordent_pitches1[i], 1));
    tick += dur;
  }
  if (tick >= section_end) return tick;

  // 3. Descending scale run from start_pitch down ~one octave (32nd notes)
  // Find scale tone index nearest to p0
  size_t top_idx = 0;
  for (size_t i = 0; i < scale_tones.size(); ++i) {
    if (scale_tones[i] <= p0) top_idx = i;
  }

  uint8_t target = clampPitch(static_cast<int>(p0) - 12, low0, high0);
  for (size_t i = top_idx; i > 0 && tick < section_end; --i) {
    if (scale_tones[i] < target) break;
    Tick dur = std::min(kThirtySecondNote, section_end - tick);
    if (dur == 0) break;
    uint8_t sp0 = clampPitch(scale_tones[i], low0, high0);
    uint8_t sp1 = clampPitch(static_cast<int>(scale_tones[i]) - 12, low1, high1);
    notes.push_back(makeNote(tick, dur, sp0, 0));
    notes.push_back(makeNote(tick, dur, sp1, 1));
    tick += dur;
  }

  return tick;
}

/// @brief Generate a block chord across voices 0, 1, and optionally 2.
void generateBlockChordNotes(const KeySignature& key_sig, Tick tick, Tick end_tick,
                             std::vector<NoteEvent>& notes,
                             uint8_t num_voices) {
  Tick dur = std::min(kWholeNote, end_tick - tick);
  if (dur == 0) return;

  uint8_t tpc = static_cast<uint8_t>(key_sig.tonic);
  uint8_t third = clampPitch(60 + tpc + (key_sig.is_minor ? 3 : 4),
                             getToccataLowPitch(1), getToccataHighPitch(1));
  uint8_t fifth = clampPitch(60 + tpc + 7,
                             getToccataLowPitch(0), getToccataHighPitch(0));

  notes.push_back(makeNote(tick, dur, fifth, 0));
  notes.push_back(makeNote(tick, dur, third, 1));
  if (num_voices >= 3) {
    uint8_t bass = clampPitch(36 + tpc,
                              getToccataLowPitch(2), getToccataHighPitch(2));
    notes.push_back(makeNote(tick, dur, bass, 2, BachNoteSource::PedalPoint));
  }
}

/// @brief Generate arpeggio flourish on voice 0 using chord tones.
void generateArpeggioFlourish(const HarmonicTimeline& timeline,
                              Tick start_tick, Tick end_tick,
                              std::vector<NoteEvent>& notes,
                              std::mt19937& rng) {
  uint8_t low = getToccataLowPitch(0);
  uint8_t high = getToccataHighPitch(0);

  const HarmonicEvent& event = timeline.getAt(start_tick);
  auto chord_tones = collectChordTonesInRange(event.chord, low, high);
  if (chord_tones.empty()) return;

  Tick tick = start_tick;
  bool ascending = true;
  size_t idx = chord_tones.size() / 2;

  while (tick < end_tick) {
    // Refresh chord tones at bar boundaries
    if (tick > start_tick && positionInBar(tick) == 0) {
      const HarmonicEvent& ev = timeline.getAt(tick);
      auto new_tones = collectChordTonesInRange(ev.chord, low, high);
      if (!new_tones.empty()) {
        chord_tones = new_tones;
        idx = std::min(idx, chord_tones.size() - 1);
      }
    }

    Tick dur = std::min(kSixteenthNote, end_tick - tick);
    if (dur == 0) break;

    notes.push_back(makeNote(tick, dur, chord_tones[idx], 0));
    tick += dur;

    // Zigzag with occasional direction reversal
    if (rng::rollProbability(rng, 0.10f)) ascending = !ascending;

    if (ascending) {
      if (idx + 1 < chord_tones.size()) ++idx;
      else { ascending = false; if (idx > 0) --idx; }
    } else {
      if (idx > 0) --idx;
      else { ascending = true; if (idx + 1 < chord_tones.size()) ++idx; }
    }
  }
}

// ---------------------------------------------------------------------------
// Opening gesture (BWV 565 style)
// ---------------------------------------------------------------------------

std::vector<NoteEvent> generateOpeningGesture(const HarmonicTimeline& timeline,
                                              const KeySignature& key_sig,
                                              Tick start_tick, Tick end_tick,
                                              std::mt19937& rng) {
  std::vector<NoteEvent> notes;
  uint8_t low0 = getToccataLowPitch(0);
  uint8_t high0 = getToccataHighPitch(0);
  uint8_t low1 = getToccataLowPitch(1);
  uint8_t high1 = getToccataHighPitch(1);

  auto scale_tones0 = getScaleTones(key_sig.tonic, key_sig.is_minor, low0, high0);
  if (scale_tones0.empty()) return notes;

  Tick total_opening = end_tick - start_tick;
  Tick opening_bars = total_opening / kTicksPerBar;

  // Dominant in octave 5 for dramatic opening (BWV 565: A5)
  uint8_t dominant_pc = static_cast<uint8_t>(getDominant(key_sig).tonic);
  uint8_t start_pitch1 = clampPitch(72 + dominant_pc, low0, high0);

  // One scale step below dominant for second wave
  size_t dom_idx = 0;
  for (size_t i = 0; i < scale_tones0.size(); ++i) {
    if (scale_tones0[i] <= start_pitch1) dom_idx = i;
  }
  uint8_t start_pitch2 = (dom_idx > 0) ? scale_tones0[dom_idx - 1]
                                        : scale_tones0[0];

  Tick tick = start_tick;

  if (opening_bars >= 4) {
    // Full pattern: wave1 + pause + wave2 + pause + block chord + arpeggio
    tick = generateMordentWave(start_pitch1, scale_tones0, tick, end_tick,
                               notes, low0, high0, low1, high1);
    tick = std::min(tick + kGrandPauseDuration, end_tick);

    if (tick < end_tick - kTicksPerBar * 2) {
      tick = generateMordentWave(start_pitch2, scale_tones0, tick, end_tick,
                                 notes, low0, high0, low1, high1);
      tick = std::min(tick + kGrandPauseDuration, end_tick);
    }

    // Block chord (tonic, 1 bar)
    if (tick < end_tick - kTicksPerBar) {
      generateBlockChordNotes(key_sig, tick, end_tick, notes, 3);
      tick += std::min(kWholeNote, end_tick - tick);
    }

    // Arpeggio flourish through remaining opening (voice 0 coexists with
    // pedal solo on voice 2 in the final bars).
    if (tick < end_tick) {
      generateArpeggioFlourish(timeline, tick, end_tick, notes, rng);
    }
  } else if (opening_bars >= 2) {
    // Simplified: single wave + arpeggio
    tick = generateMordentWave(start_pitch1, scale_tones0, tick, end_tick,
                               notes, low0, high0, low1, high1);
    tick = std::min(tick + kGrandPauseDuration, end_tick);

    if (tick < end_tick) {
      generateArpeggioFlourish(timeline, tick, end_tick, notes, rng);
    }
  } else {
    // Very short: just arpeggio
    generateArpeggioFlourish(timeline, start_tick, end_tick, notes, rng);
  }

  return notes;
}

// ---------------------------------------------------------------------------
// Recitative -- Stylus Phantasticus (free rhetorical style)
// ---------------------------------------------------------------------------

/// @brief Select a duration from the recitative weighted table.
Tick selectRecitDuration(std::mt19937& rng) {
  float roll = rng::rollFloat(rng, 0.0f, 1.0f);
  if (roll < 0.10f) return kThirtySecondNote;
  if (roll < 0.30f) return kSixteenthNote;
  if (roll < 0.60f) return kEighthNote;
  if (roll < 0.80f) return kQuarterNote;
  if (roll < 0.90f) return kDottedQuarter;
  return kHalfNote;
}

/// @brief Select a signed scale-step interval for recitative melody.
/// @return Number of scale steps (positive = ascending, negative = descending).
int selectRecitScaleStep(std::mt19937& rng, bool ascending) {
  float roll = rng::rollFloat(rng, 0.0f, 1.0f);
  int magnitude;
  if (roll < 0.50f) magnitude = 1;                                  // Step (2nd)
  else if (roll < 0.75f) magnitude = rng::rollRange(rng, 2, 3);    // Skip (3rd/4th)
  else if (roll < 0.90f) magnitude = rng::rollRange(rng, 4, 5);    // Leap (5th/6th)
  else magnitude = 7;                                                // Octave
  return ascending ? magnitude : -magnitude;
}

std::vector<NoteEvent> generateRecitative(const HarmonicTimeline& timeline,
                                          const KeySignature& key_sig,
                                          Tick start_tick, Tick end_tick,
                                          std::mt19937& rng) {
  std::vector<NoteEvent> notes;
  uint8_t low0 = getToccataLowPitch(0);
  uint8_t high0 = getToccataHighPitch(0);
  uint8_t low1 = getToccataLowPitch(1);
  uint8_t high1 = getToccataHighPitch(1);

  Tick recit_dur = end_tick - start_tick;
  Tick recit_bars = recit_dur / kTicksPerBar;
  if (recit_bars == 0) return notes;

  // Grand pause positions (1/3 and 2/3, only for >= 8 bars)
  Tick gp_bar1 = (recit_bars >= 8) ? (recit_bars / 3) : recit_bars + 1;
  Tick gp_bar2 = (recit_bars >= 8) ? (recit_bars * 2 / 3) : recit_bars + 1;

  // Build scale-tone arrays for both manuals.
  auto scale0 = getScaleTones(key_sig.tonic, key_sig.is_minor, low0, high0);
  auto scale1 = getScaleTones(key_sig.tonic, key_sig.is_minor, low1, high1);
  if (scale0.empty() || scale1.empty()) return notes;

  // Start melody in the middle of the scale.
  size_t mel_idx0 = scale0.size() / 2;
  size_t mel_idx1 = scale1.size() / 2;
  bool ascending = true;

  for (Tick bar = 0; bar < recit_bars; ++bar) {
    Tick bar_start = start_tick + bar * kTicksPerBar;
    Tick bar_end = bar_start + kTicksPerBar;

    // Grand pause: skip this bar (silence on all voices)
    if (bar == gp_bar1 || bar == gp_bar2) continue;

    // Determine voice assignment (swap every 2 bars for manual contrast)
    uint8_t melody_voice = ((bar / 2) % 2 == 0) ? 0 : 1;
    uint8_t chord_voice = 1 - melody_voice;
    const auto& mel_scale = (melody_voice == 0) ? scale0 : scale1;
    size_t& mel_idx = (melody_voice == 0) ? mel_idx0 : mel_idx1;
    uint8_t ch_low = (chord_voice == 0) ? low0 : low1;
    uint8_t ch_high = (chord_voice == 0) ? high0 : high1;

    // --- Melody line (scale-indexed movement) ---
    Tick tick = bar_start;
    while (tick < bar_end) {
      Tick dur = selectRecitDuration(rng);
      if (tick + dur > bar_end) dur = bar_end - tick;
      if (dur == 0) break;

      int step = selectRecitScaleStep(rng, ascending);
      int new_idx = static_cast<int>(mel_idx) + step;
      new_idx = std::max(0, std::min(new_idx,
                                     static_cast<int>(mel_scale.size()) - 1));
      mel_idx = static_cast<size_t>(new_idx);

      notes.push_back(makeNote(tick, dur, mel_scale[mel_idx], melody_voice));
      tick += dur;

      if (rng::rollProbability(rng, 0.15f)) ascending = !ascending;
    }

    // --- Chord layer (sustained diatonic tones) ---
    // Use scale tones for the chord voice to stay diatonic when the harmonic
    // plan contains secondary chords (viio7/V, etc.).
    const auto& ch_scale = (chord_voice == 0) ? scale0 : scale1;
    tick = bar_start;
    while (tick < bar_end) {
      const HarmonicEvent& event = timeline.getAt(tick);
      auto chord_tones = getChordTones(event.chord, 3);

      // Filter to diatonic tones in range.
      std::vector<uint8_t> valid;
      ScaleType sc_type = key_sig.is_minor ? ScaleType::HarmonicMinor
                                           : ScaleType::Major;
      for (auto t : chord_tones) {
        if (t >= ch_low && t <= ch_high &&
            scale_util::isScaleTone(t, key_sig.tonic, sc_type)) {
          valid.push_back(t);
        }
      }
      if (valid.empty()) {
        // Retry octave 4 with diatonic filter.
        chord_tones = getChordTones(event.chord, 4);
        for (auto t : chord_tones) {
          if (t >= ch_low && t <= ch_high &&
              scale_util::isScaleTone(t, key_sig.tonic, sc_type)) {
            valid.push_back(t);
          }
        }
      }
      if (valid.empty()) {
        // Fall back: pick nearest diatonic tone to chord root.
        uint8_t root = clampPitch(static_cast<int>(event.bass_pitch),
                                  ch_low, ch_high);
        // Snap to nearest scale tone.
        for (const auto& st : ch_scale) {
          if (st >= root) { valid.push_back(st); break; }
        }
        if (valid.empty() && !ch_scale.empty()) {
          valid.push_back(ch_scale.back());
        }
      }

      Tick dur = rng::rollProbability(rng, 0.5f) ? kHalfNote : kWholeNote;
      if (tick + dur > bar_end) dur = bar_end - tick;
      if (dur == 0) break;

      notes.push_back(makeNote(tick, dur, rng::selectRandom(rng, valid),
                               chord_voice));
      tick += dur;
    }
  }

  return notes;
}

// ---------------------------------------------------------------------------
// Drive to cadence -- rhythmic acceleration + Picardy ending
// ---------------------------------------------------------------------------

std::vector<NoteEvent> generateDriveToCadence(const HarmonicTimeline& timeline,
                                              const KeySignature& key_sig,
                                              uint8_t num_voices,
                                              Tick start_tick, Tick end_tick,
                                              std::mt19937& rng) {
  std::vector<NoteEvent> notes;

  Tick drive_dur = end_tick - start_tick;
  Tick p1_end = start_tick + drive_dur * 30 / 100;
  Tick p2_end = start_tick + drive_dur * 60 / 100;
  Tick p3_end = start_tick + drive_dur * 90 / 100;
  // Phase 4: p3_end to end_tick

  uint8_t low0 = getToccataLowPitch(0);
  uint8_t high0 = getToccataHighPitch(0);
  uint8_t low1 = getToccataLowPitch(1);
  uint8_t high1 = getToccataHighPitch(1);

  auto scale0 = getScaleTones(key_sig.tonic, key_sig.is_minor, low0, high0);
  auto scale1 = getScaleTones(key_sig.tonic, key_sig.is_minor, low1, high1);
  if (scale0.empty() || scale1.empty()) return notes;

  // Voice 0: start low-mid, ascending
  size_t idx0 = scale0.size() / 3;
  bool asc0 = true;
  // Voice 1: start mid-high, descending (contrary motion)
  size_t idx1 = scale1.size() * 2 / 3;
  bool asc1 = false;

  // --- Phase 1 (30%): 8th note arpeggios, contrary motion ---
  {
    Tick tick = start_tick;
    size_t i0 = idx0, i1 = idx1;
    while (tick < p1_end) {
      Tick dur = std::min(kEighthNote, p1_end - tick);
      if (dur == 0) break;

      notes.push_back(makeNote(tick, dur, scale0[i0], 0));
      notes.push_back(makeNote(tick, dur, scale1[i1], 1));
      tick += dur;

      // Voice 0 ascending, Voice 1 descending
      if (i0 + 1 < scale0.size()) ++i0;
      else { i0 = scale0.size() / 3; }  // Reset to mid-low

      if (i1 > 0) --i1;
      else { i1 = scale1.size() * 2 / 3; }  // Reset to mid-high
    }
    idx0 = i0;
    idx1 = i1;
  }

  // --- Phase 2 (30%): 16th note arpeggios + pedal ---
  {
    Tick tick = p1_end;
    size_t i0 = idx0, i1 = idx1;
    while (tick < p2_end) {
      Tick dur = std::min(kSixteenthNote, p2_end - tick);
      if (dur == 0) break;

      notes.push_back(makeNote(tick, dur, scale0[i0], 0));
      notes.push_back(makeNote(tick, dur, scale1[i1], 1));
      tick += dur;

      int step = rng::rollProbability(rng, 0.15f) ? 2 : 1;
      if (asc0) {
        if (i0 + step < scale0.size()) i0 += step;
        else { asc0 = false; if (i0 >= static_cast<size_t>(step)) i0 -= step; }
      } else {
        if (i0 >= static_cast<size_t>(step)) i0 -= step;
        else { asc0 = true; if (i0 + step < scale0.size()) i0 += step; }
      }

      if (asc1) {
        if (i1 + step < scale1.size()) i1 += step;
        else { asc1 = false; if (i1 >= static_cast<size_t>(step)) i1 -= step; }
      } else {
        if (i1 >= static_cast<size_t>(step)) i1 -= step;
        else { asc1 = true; if (i1 + step < scale1.size()) i1 += step; }
      }
    }
    idx0 = i0;
  }

  // --- Phase 3 (30%): parallel diatonic 3rds in 16th notes ---
  {
    Tick tick = p2_end;
    size_t i0 = idx0;
    constexpr int kParallelScaleSteps = 2;  // Diatonic 3rd = 2 scale steps below

    while (tick < p3_end) {
      Tick dur = std::min(kSixteenthNote, p3_end - tick);
      if (dur == 0) break;

      uint8_t p0 = scale0[i0];
      // Find voice 1 pitch: 2 scale steps below in scale1 (diatonic 3rd)
      uint8_t p1_par = p0;
      for (size_t s1i = 0; s1i < scale1.size(); ++s1i) {
        if (scale1[s1i] >= p0 && s1i >= kParallelScaleSteps) {
          p1_par = scale1[s1i - kParallelScaleSteps];
          break;
        }
      }
      p1_par = clampPitch(static_cast<int>(p1_par), low1, high1);

      notes.push_back(makeNote(tick, dur, p0, 0));
      notes.push_back(makeNote(tick, dur, p1_par, 1));
      tick += dur;

      if (i0 + 1 < scale0.size()) ++i0;
      else i0 = scale0.size() / 2;  // Reset to mid
    }
  }

  // --- Phase 4 (10%): V7 arpeggio sweep + Picardy block chord ---
  {
    Tick tick = p3_end;
    uint8_t tpc = static_cast<uint8_t>(key_sig.tonic);

    // V7 arpeggio (32nd notes ascending) on voice 0
    Tick chord_start = end_tick - kWholeNote;
    if (chord_start < p3_end + kTicksPerBar / 2) {
      chord_start = p3_end + (end_tick - p3_end) / 2;
    }

    // V7 chord tones for arpeggio run
    const HarmonicEvent& v7_event = timeline.getAt(tick);
    auto v7_tones = collectChordTonesInRange(v7_event.chord, low0, high0);
    if (!v7_tones.empty()) {
      size_t vi = 0;
      while (tick < chord_start) {
        Tick dur = std::min(kThirtySecondNote, chord_start - tick);
        if (dur == 0) break;
        notes.push_back(makeNote(tick, dur, v7_tones[vi], 0));
        tick += dur;
        if (vi + 1 < v7_tones.size()) ++vi;
        else vi = 0;
      }
    }

    // Final Picardy block chord (I Major)
    tick = chord_start;
    if (tick < end_tick) {
      Tick dur = end_tick - tick;
      // Voice 0: major 3rd (the Picardy note)
      uint8_t picardy_3rd = clampPitch(60 + (tpc + 4) % 12, low0, high0);
      // Voice 1: 5th
      uint8_t fifth = clampPitch(60 + (tpc + 7) % 12, low1, high1);
      notes.push_back(makeNote(tick, dur, picardy_3rd, 0));
      notes.push_back(makeNote(tick, dur, fifth, 1));

      // Pedal: root
      if (num_voices >= 3) {
        uint8_t bass = clampPitch(36 + tpc,
                                  getToccataLowPitch(2), getToccataHighPitch(2));
        notes.push_back(makeNote(tick, dur, bass, 2, BachNoteSource::PedalPoint));
      }
    }
  }

  // --- Additional voices (3+): chord-tone support with quarter notes ---
  for (uint8_t vi = 3; vi < num_voices && vi < 5; ++vi) {
    uint8_t lo = getToccataLowPitch(vi);
    uint8_t hi = getToccataHighPitch(vi);

    Tick tick = start_tick;
    while (tick < end_tick) {
      const HarmonicEvent& event = timeline.getAt(tick);
      auto chord_tones = getChordTones(event.chord, 4);

      std::vector<uint8_t> valid;
      for (auto t : chord_tones) {
        if (t >= lo && t <= hi) valid.push_back(t);
      }
      if (valid.empty()) {
        valid.push_back(clampPitch(static_cast<int>(event.bass_pitch), lo, hi));
      }

      Tick dur = std::min(kQuarterNote, end_tick - tick);
      if (dur == 0) break;

      notes.push_back(makeNote(tick, dur, rng::selectRandom(rng, valid), vi));
      tick += dur;
    }
  }

  return notes;
}

// ---------------------------------------------------------------------------
// Pedal helpers
// ---------------------------------------------------------------------------

/// @brief Generate pedal solo passage at end of opening (2 bars).
std::vector<NoteEvent> generatePedalSolo(const KeySignature& key_sig,
                                         Tick start_tick, Tick end_tick,
                                         std::mt19937& rng) {
  std::vector<NoteEvent> notes;
  uint8_t low = organ_range::kPedalLow;
  uint8_t high = organ_range::kPedalHigh;

  auto scale = getScaleTones(key_sig.tonic, key_sig.is_minor, low, high);
  if (scale.empty()) return notes;

  Tick mid = start_tick + (end_tick - start_tick) / 2;

  // Bar 1: descending scale from dominant area
  uint8_t dominant_pc = static_cast<uint8_t>(getDominant(key_sig).tonic);
  size_t start_idx = scale.size() - 1;
  for (size_t i = 0; i < scale.size(); ++i) {
    if (scale[i] % 12 == dominant_pc && scale[i] >= 36) {
      start_idx = i;
      break;
    }
  }

  {
    size_t idx = start_idx;
    Tick tick = start_tick;
    while (tick < mid && tick < end_tick) {
      Tick dur = std::min(kEighthNote, mid - tick);
      if (dur == 0) break;
      notes.push_back(makeNote(tick, dur, scale[idx], 2, BachNoteSource::PedalPoint));
      tick += dur;
      if (idx > 0) --idx;
      else idx = start_idx;
    }
  }

  // Bar 2: tonic-area arpeggiated motion
  {
    uint8_t tonic_pc = static_cast<uint8_t>(key_sig.tonic);
    size_t tonic_idx = 0;
    for (size_t i = 0; i < scale.size(); ++i) {
      if (scale[i] % 12 == tonic_pc) { tonic_idx = i; break; }
    }

    bool asc = true;
    size_t idx = tonic_idx;
    Tick tick = mid;
    while (tick < end_tick) {
      Tick dur = std::min(kEighthNote, end_tick - tick);
      if (dur == 0) break;
      notes.push_back(makeNote(tick, dur, scale[idx], 2, BachNoteSource::PedalPoint));
      tick += dur;

      int step = rng::rollProbability(rng, 0.3f) ? 2 : 1;
      if (asc) {
        if (idx + step < scale.size()) idx += step;
        else { asc = false; if (idx >= static_cast<size_t>(step)) idx -= step; }
      } else {
        if (idx >= static_cast<size_t>(step)) idx -= step;
        else { asc = true; if (idx + step < scale.size()) idx += step; }
      }
    }
  }

  return notes;
}

/// @brief Generate rhythmic pedal for the drive section.
std::vector<NoteEvent> generateRhythmicPedal(const KeySignature& key_sig,
                                             Tick start_tick, Tick end_tick,
                                             Tick phase3_start) {
  std::vector<NoteEvent> notes;
  uint8_t low = organ_range::kPedalLow;
  uint8_t high = organ_range::kPedalHigh;

  uint8_t tpc = static_cast<uint8_t>(key_sig.tonic);
  uint8_t tonic = clampPitch(36 + tpc, low, high);
  uint8_t dominant = clampPitch(36 + static_cast<int>(getDominant(key_sig).tonic),
                                low, high);

  Tick tick = start_tick;
  while (tick < end_tick) {
    if (tick >= phase3_start) {
      // Phase 3: 8th note tonic-dominant alternation
      Tick dur = std::min(kEighthNote, end_tick - tick);
      if (dur == 0) break;
      bool use_tonic = ((tick / kEighthNote) % 2 == 0);
      notes.push_back(makeNote(tick, dur, use_tonic ? tonic : dominant,
                               2, BachNoteSource::PedalPoint));
      tick += dur;
    } else {
      // Phase 2: quarter-quarter-half pattern (T-D-T)
      struct Step { uint8_t pitch; Tick dur; };
      Step pattern[] = {
        {tonic, kQuarterNote},
        {dominant, kQuarterNote},
        {tonic, kHalfNote}
      };
      for (auto& s : pattern) {
        if (tick >= end_tick || tick >= phase3_start) break;
        Tick bound = std::min(end_tick, phase3_start);
        Tick dur = std::min(s.dur, bound - tick);
        if (dur == 0) break;
        notes.push_back(makeNote(tick, dur, s.pitch, 2, BachNoteSource::PedalPoint));
        tick += dur;
      }
    }
  }

  return notes;
}

/// @brief Generate sustained tonic pedal during opening waves.
std::vector<NoteEvent> generateOpeningPedal(const KeySignature& key_sig,
                                            Tick start_tick, Tick end_tick) {
  std::vector<NoteEvent> notes;
  uint8_t tpc = static_cast<uint8_t>(key_sig.tonic);
  uint8_t tonic = clampPitch(36 + tpc,
                             organ_range::kPedalLow, organ_range::kPedalHigh);

  Tick tick = start_tick;
  while (tick < end_tick) {
    Tick dur = std::min(kWholeNote, end_tick - tick);
    if (dur == 0) break;
    notes.push_back(makeNote(tick, dur, tonic, 2, BachNoteSource::PedalPoint));
    tick += dur;
  }

  return notes;
}

// ---------------------------------------------------------------------------
// Utility
// ---------------------------------------------------------------------------

uint8_t clampToccataVoiceCount(uint8_t num_voices) {
  if (num_voices < kMinVoices) return kMinVoices;
  if (num_voices > kMaxVoices) return kMaxVoices;
  return num_voices;
}

void sortToccataTrackNotes(std::vector<Track>& tracks) {
  for (auto& track : tracks) {
    std::sort(track.notes.begin(), track.notes.end(),
              [](const NoteEvent& lhs, const NoteEvent& rhs) {
                if (lhs.start_tick != rhs.start_tick) {
                  return lhs.start_tick < rhs.start_tick;
                }
                return lhs.pitch < rhs.pitch;
              });
  }
}

}  // namespace

// ---------------------------------------------------------------------------
// Dramaticus archetype (BWV 565 style)
// ---------------------------------------------------------------------------

ToccataResult generateDramaticusToccata(const ToccataConfig& config) {
  ToccataResult result;
  result.success = false;

  uint8_t num_voices = clampToccataVoiceCount(config.num_voices);

  if (config.total_bars <= 0) {
    result.error_message = "total_bars must be positive";
    return result;
  }

  std::mt19937 rng(config.seed);

  // Section boundaries
  Tick total_duration = static_cast<Tick>(config.total_bars) * kTicksPerBar;

  Tick opening_bars = static_cast<Tick>(
      static_cast<float>(config.total_bars) * kOpeningProportion);
  if (opening_bars < 1) opening_bars = 1;

  Tick recit_bars = static_cast<Tick>(
      static_cast<float>(config.total_bars) * kRecitativeProportion);
  if (recit_bars < 1) recit_bars = 1;

  Tick drive_bars = static_cast<Tick>(config.total_bars) - opening_bars - recit_bars;
  if (drive_bars < 1) {
    if (recit_bars > 1) {
      --recit_bars;
      ++drive_bars;
    } else {
      drive_bars = 1;
    }
  }

  Tick opening_start = 0;
  Tick opening_end = opening_bars * kTicksPerBar;
  Tick recit_start = opening_end;
  Tick recit_end = recit_start + recit_bars * kTicksPerBar;
  Tick drive_start = recit_end;
  Tick drive_end = total_duration;

  // Build toccata-specific harmonic plan
  HarmonicTimeline timeline = buildToccataHarmonicPlan(
      config.key, opening_start, opening_end,
      recit_start, recit_end, drive_start, drive_end);

  // Generate notes for each section (rng consumed in deterministic order)
  auto opening_notes = generateOpeningGesture(
      timeline, config.key, opening_start, opening_end, rng);

  auto recit_notes = generateRecitative(
      timeline, config.key, recit_start, recit_end, rng);

  auto drive_notes = generateDriveToCadence(
      timeline, config.key, num_voices, drive_start, drive_end, rng);

  // Pedal solo (last 2 bars of opening, consumes rng after drive for determinism)
  std::vector<NoteEvent> pedal_solo_notes;
  if (num_voices >= 3 && opening_bars >= 6) {
    Tick solo_start = opening_end - 2 * kTicksPerBar;
    pedal_solo_notes = generatePedalSolo(config.key, solo_start, opening_end, rng);
  }

  // Apply ornaments per section
  auto applyOrnamentsToSection = [&](std::vector<NoteEvent>& section_notes,
                                     float density, VoiceRole role,
                                     const std::vector<Tick>& cadence_ticks) {
    OrnamentContext ctx;
    ctx.config.ornament_density = density;
    ctx.role = role;
    ctx.seed = config.seed;
    ctx.timeline = &timeline;
    ctx.cadence_ticks = cadence_ticks;
    section_notes = applyOrnaments(section_notes, ctx);
  };

  applyOrnamentsToSection(opening_notes, 0.12f, VoiceRole::Propel, {});
  applyOrnamentsToSection(recit_notes, 0.10f, VoiceRole::Respond,
                          {recit_end});
  applyOrnamentsToSection(drive_notes, 0.08f, VoiceRole::Propel,
                          {drive_end - kTicksPerBar});

  // Build all_notes from ornamented sections + pedal
  std::vector<NoteEvent> all_notes;
  all_notes.insert(all_notes.end(), opening_notes.begin(), opening_notes.end());
  all_notes.insert(all_notes.end(), recit_notes.begin(), recit_notes.end());
  all_notes.insert(all_notes.end(), drive_notes.begin(), drive_notes.end());

  // Add pedal notes (generated separately, not ornamented)
  if (num_voices >= 3) {
    // Sustained tonic pedal during opening (up to pedal solo start)
    Tick pedal_end_opening = (opening_bars >= 6) ? (opening_end - 2 * kTicksPerBar)
                                                 : opening_end;
    auto pedal_op = generateOpeningPedal(config.key, opening_start, pedal_end_opening);
    all_notes.insert(all_notes.end(), pedal_op.begin(), pedal_op.end());

    // Pedal solo (last 2 bars of opening)
    all_notes.insert(all_notes.end(), pedal_solo_notes.begin(),
                     pedal_solo_notes.end());

    // Sustained tonic pedal during recitative (harmonic foundation).
    // Continues through grand pause bars to maintain bass presence.
    auto pedal_recit = generateOpeningPedal(config.key, recit_start, recit_end);
    all_notes.insert(all_notes.end(), pedal_recit.begin(), pedal_recit.end());

    // Sustained tonic pedal for drive phase 1 (before rhythmic pedal)
    Tick drive_dur = drive_end - drive_start;
    Tick rp_start = drive_start + drive_dur * 30 / 100;
    auto pedal_drive_p1 = generateOpeningPedal(config.key, drive_start, rp_start);
    all_notes.insert(all_notes.end(), pedal_drive_p1.begin(), pedal_drive_p1.end());

    // Rhythmic pedal for drive (phases 2+)
    Tick rp_phase3 = drive_start + drive_dur * 60 / 100;
    Tick rp_end = drive_start + drive_dur * 90 / 100;
    if (rp_start < rp_end) {
      auto rp = generateRhythmicPedal(config.key, rp_start, rp_end, rp_phase3);
      all_notes.insert(all_notes.end(), rp.begin(), rp.end());
    }
  }

  // Post-validate through counterpoint engine.
  if (num_voices >= 2) {
    std::vector<std::pair<uint8_t, uint8_t>> voice_ranges;
    for (uint8_t v = 0; v < num_voices; ++v) {
      voice_ranges.emplace_back(getToccataLowPitch(v), getToccataHighPitch(v));
    }

    // Tag untagged notes: voice 2 = Pedal (Structural), others = FreeCounterpoint.
    for (auto& n : all_notes) {
      if (n.source == BachNoteSource::Unknown) {
        n.source = isPedalVoice(n.voice, num_voices) ? BachNoteSource::PedalPoint
                                  : BachNoteSource::FreeCounterpoint;
      }
    }

    PostValidateStats stats;
    all_notes = postValidateNotes(
        std::move(all_notes), num_voices, config.key, voice_ranges, &stats);
  }

  // Create tracks and assign notes by voice
  std::vector<Track> tracks = createToccataTracks(num_voices);
  for (const auto& note : all_notes) {
    if (note.voice < tracks.size()) {
      tracks[note.voice].notes.push_back(note);
    }
  }

  // Apply registration plan
  ExtendedRegistrationPlan reg_plan;

  Registration pleno;
  pleno.velocity_hint = 100;
  reg_plan.addPoint(opening_start, pleno, "pleno");

  if (num_voices >= 3 && opening_bars >= 6) {
    Registration pedal_solo_reg;
    pedal_solo_reg.velocity_hint = 60;
    reg_plan.addPoint(opening_end - 2 * kTicksPerBar, pedal_solo_reg, "pedal_solo");
  }

  Registration swell_solo;
  swell_solo.velocity_hint = 65;
  reg_plan.addPoint(recit_start, swell_solo, "swell_solo");

  Registration coupled;
  coupled.velocity_hint = 90;
  reg_plan.addPoint(drive_start, coupled, "coupled");

  if (drive_end > 2 * kTicksPerBar) {
    Registration tutti;
    tutti.velocity_hint = 110;
    reg_plan.addPoint(drive_end - 2 * kTicksPerBar, tutti, "tutti");
  }

  applyExtendedRegistrationPlan(tracks, reg_plan);

  // Sort notes within each track
  sortToccataTrackNotes(tracks);

  // Within-voice overlap cleanup: remove same-tick duplicates and truncate overlaps.
  for (auto& track : tracks) {
    auto& notes = track.notes;
    if (notes.size() < 2) continue;

    // Sort by start_tick, then duration descending (keep longer note on dedup).
    std::sort(notes.begin(), notes.end(),
              [](const NoteEvent& a, const NoteEvent& b) {
                if (a.start_tick != b.start_tick) return a.start_tick < b.start_tick;
                return a.duration > b.duration;
              });

    // Remove same-tick duplicates.
    notes.erase(
        std::unique(notes.begin(), notes.end(),
                    [](const NoteEvent& a, const NoteEvent& b) {
                      return a.start_tick == b.start_tick;
                    }),
        notes.end());

    // Truncate overlapping notes.
    for (size_t i = 0; i + 1 < notes.size(); ++i) {
      Tick end_tick = notes[i].start_tick + notes[i].duration;
      if (end_tick > notes[i + 1].start_tick) {
        notes[i].duration = notes[i + 1].start_tick - notes[i].start_tick;
        if (notes[i].duration == 0) notes[i].duration = 1;
      }
    }
  }

  // Picardy third (minor keys only).
  if (config.enable_picardy && config.key.is_minor) {
    for (auto& track : tracks) {
      applyPicardyToFinalChord(track.notes, config.key,
                               total_duration - kTicksPerBar);
    }
  }

  result.tracks = std::move(tracks);
  result.timeline = std::move(timeline);
  result.total_duration_ticks = total_duration;
  result.archetype = ToccataArchetype::Dramaticus;

  // Populate structured sections.
  result.sections = {
      {ToccataSectionId::Opening, opening_start, opening_end},
      {ToccataSectionId::Recitative, recit_start, recit_end},
      {ToccataSectionId::Drive, drive_start, drive_end},
  };

  // Legacy fields.
  result.opening_start = opening_start;
  result.opening_end = opening_end;
  result.recit_start = recit_start;
  result.recit_end = recit_end;
  result.drive_start = drive_start;
  result.drive_end = drive_end;
  result.success = true;

  return result;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

ToccataResult generateToccata(const ToccataConfig& config) {
  switch (config.archetype) {
    case ToccataArchetype::Dramaticus:
      return generateDramaticusToccata(config);
    case ToccataArchetype::Perpetuus:
      return generatePerpetuusToccata(config);
    case ToccataArchetype::Concertato:
      return generateConcertatoToccata(config);
    case ToccataArchetype::Sectionalis:
      return generateSectionalisToccata(config);
  }
  // Unreachable, but satisfy compiler.
  ToccataResult result;
  result.error_message = "Unknown toccata archetype";
  return result;
}

}  // namespace bach
