// Tests for counterpoint/vertical_safe.h -- vertical safety callbacks
// for resolveLeaps / repairRepeatedNotes.

#include "counterpoint/vertical_safe.h"

#include <gtest/gtest.h>

#include <vector>

#include "core/basic_types.h"
#include "core/note_source.h"
#include "harmony/chord_types.h"
#include "harmony/harmonic_event.h"
#include "harmony/harmonic_timeline.h"

namespace bach {
namespace {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

NoteEvent makeNote(Tick tick, Tick dur, uint8_t pitch, uint8_t voice) {
  NoteEvent n;
  n.start_tick = tick;
  n.duration = dur;
  n.pitch = pitch;
  n.voice = voice;
  n.source = BachNoteSource::FreeCounterpoint;
  return n;
}

// Simple C major timeline: one I-chord event covering the entire duration.
// Chord tones: C(0), E(4), G(7).
HarmonicTimeline makeCMajorTimeline(Tick duration) {
  HarmonicTimeline tl;
  HarmonicEvent ev;
  ev.tick = 0;
  ev.end_tick = duration;
  ev.key = Key::C;
  ev.is_minor = false;
  ev.chord = Chord{ChordDegree::I, ChordQuality::Major, 60, 0};
  ev.bass_pitch = 48;  // C3
  tl.addEvent(ev);
  return tl;
}

// ==========================================================================
// makeVerticalSafeCallback tests
// ==========================================================================

TEST(VerticalSafeCallback, ConsonanceAccepted) {
  // Voice 0: E4(64) sounding at tick 0 (strong beat).
  // Candidate: voice 1, pitch G4(67) at tick 0.
  // G is a chord tone of C major I -> accepted via chord-tone shortcut.
  auto tl = makeCMajorTimeline(kTicksPerBar);
  std::vector<NoteEvent> notes = {
      makeNote(0, kTicksPerBeat, 64, 0),  // E4, voice 0
  };
  auto safe = makeVerticalSafeCallback(tl, notes, 2);
  EXPECT_TRUE(safe(0, 1, 67));  // G4: chord tone -> safe.
}

TEST(VerticalSafeCallback, NonChordToneConsonanceAccepted) {
  // Voice 0: C4(60) sounding at tick 0 (strong beat).
  // Candidate: voice 1, pitch B4(71) at tick 0.
  // B is NOT a chord tone. |60-71|=11, compoundToSimple=11. Not consonant.
  // Actually let's use A4(69): |60-69|=9, simple=9 (M6). Consonant. Not chord tone.
  auto tl = makeCMajorTimeline(kTicksPerBar);
  std::vector<NoteEvent> notes = {
      makeNote(0, kTicksPerBeat, 60, 0),  // C4, voice 0
  };
  auto safe = makeVerticalSafeCallback(tl, notes, 2);
  EXPECT_TRUE(safe(0, 1, 69));  // A4: M6 with C4, consonant, not chord tone.
}

TEST(VerticalSafeCallback, DissonanceRejected) {
  // Voice 0: C4(60) sounding at tick 0 (strong beat).
  // Candidate: voice 1, pitch Db4(61) at tick 0.
  // Db is NOT a chord tone. |60-61|=1 (m2) -> dissonant.
  auto tl = makeCMajorTimeline(kTicksPerBar);
  std::vector<NoteEvent> notes = {
      makeNote(0, kTicksPerBeat, 60, 0),
  };
  auto safe = makeVerticalSafeCallback(tl, notes, 2);
  EXPECT_FALSE(safe(0, 1, 61));  // Db4: m2 with C4 -> rejected.
}

TEST(VerticalSafeCallback, WeakBeatAlwaysSafe) {
  // Same dissonant setup, but at tick 480 (beat 1 = weak).
  // Weak beats bypass all checks.
  auto tl = makeCMajorTimeline(kTicksPerBar);
  std::vector<NoteEvent> notes = {
      makeNote(kTicksPerBeat, kTicksPerBeat, 60, 0),  // C4 at beat 1
  };
  auto safe = makeVerticalSafeCallback(tl, notes, 2);
  EXPECT_TRUE(safe(kTicksPerBeat, 1, 61));  // Db4 on weak beat -> safe.
}

TEST(VerticalSafeCallback, Beat3WeakSafe) {
  // Beat 3 (tick 1440) is also weak.
  auto tl = makeCMajorTimeline(kTicksPerBar * 2);
  std::vector<NoteEvent> notes = {
      makeNote(3 * kTicksPerBeat, kTicksPerBeat, 60, 0),  // C4 at beat 3
  };
  auto safe = makeVerticalSafeCallback(tl, notes, 2);
  EXPECT_TRUE(safe(3 * kTicksPerBeat, 1, 61));  // Dissonant but weak beat.
}

TEST(VerticalSafeCallback, ChordToneAlwaysSafe) {
  // Voice 0: Bb3(58) at tick 0 (strong beat, not in C major chord).
  // Candidate: pitch C4(60) -- chord tone of I chord.
  // Even though Bb-C = m2 (dissonant), C is a chord tone -> safe.
  auto tl = makeCMajorTimeline(kTicksPerBar);
  std::vector<NoteEvent> notes = {
      makeNote(0, kTicksPerBeat, 58, 0),  // Bb3, voice 0
  };
  auto safe = makeVerticalSafeCallback(tl, notes, 2);
  EXPECT_TRUE(safe(0, 1, 60));  // C4: chord tone -> safe despite m2 with Bb.
}

TEST(VerticalSafeCallback, P4UpperVoicesAccepted) {
  // 3 voices. Voice 0 (bass): C3(48). Voice 1: E4(64).
  // Candidate for voice 2: A4(69) at tick 0.
  // |69-64| = 5 (P4), but neither 69 nor 64 is the bass (48).
  // |69-48| = 21, simple=9 (M6), consonant.
  // P4 between upper voices is acceptable.
  auto tl = makeCMajorTimeline(kTicksPerBar);
  std::vector<NoteEvent> notes = {
      makeNote(0, kTicksPerBeat, 48, 0),  // C3, bass
      makeNote(0, kTicksPerBeat, 64, 1),  // E4, upper
  };
  auto safe = makeVerticalSafeCallback(tl, notes, 3);
  EXPECT_TRUE(safe(0, 2, 69));  // A4: P4 with E4, both upper -> accepted.
}

TEST(VerticalSafeCallback, P4WithBassRejected) {
  // 2 voices. Voice 0: F3(53) at tick 0.
  // Candidate: voice 1, pitch Bb3(58) at tick 0.
  // |58-53| = 5 (P4). In 2-voice texture, P4 is dissonant.
  // Bb is not a chord tone of C major I.
  auto tl = makeCMajorTimeline(kTicksPerBar);
  std::vector<NoteEvent> notes = {
      makeNote(0, kTicksPerBeat, 53, 0),  // F3
  };
  auto safe = makeVerticalSafeCallback(tl, notes, 2);
  EXPECT_FALSE(safe(0, 1, 58));  // Bb3: P4 with bass in 2-voice -> rejected.
}

TEST(VerticalSafeCallback, NoSoundingVoicesAccepted) {
  // No other voices sounding at the tick -> all consonance checks pass vacuously.
  auto tl = makeCMajorTimeline(kTicksPerBar);
  std::vector<NoteEvent> notes;  // Empty.
  auto safe = makeVerticalSafeCallback(tl, notes, 2);
  // Bb(58) is not a chord tone, but no voices to conflict with.
  EXPECT_TRUE(safe(0, 0, 58));
}

TEST(VerticalSafeCallback, StrongBeatZeroAndTwo) {
  // Verify beat 0 and beat 2 are both treated as strong (accented).
  auto tl = makeCMajorTimeline(kTicksPerBar * 2);
  std::vector<NoteEvent> notes = {
      makeNote(0, kTicksPerBar, 60, 0),  // C4 spanning entire bar
  };
  auto safe = makeVerticalSafeCallback(tl, notes, 2);
  // Beat 0 (tick 0): Db4(61) dissonant -> rejected.
  EXPECT_FALSE(safe(0, 1, 61));
  // Beat 2 (tick 960): Db4(61) dissonant -> rejected.
  EXPECT_FALSE(safe(2 * kTicksPerBeat, 1, 61));
}

TEST(VerticalSafeCallback, VoiceNotSoundingAtTick) {
  // Voice 0 note ends before the candidate tick -> no conflict.
  auto tl = makeCMajorTimeline(kTicksPerBar);
  std::vector<NoteEvent> notes = {
      makeNote(0, kTicksPerBeat, 60, 0),  // C4, ends at tick 480
  };
  auto safe = makeVerticalSafeCallback(tl, notes, 2);
  // At tick 960 (beat 2), voice 0 is silent. No conflict.
  EXPECT_TRUE(safe(2 * kTicksPerBeat, 1, 61));  // Db4: no sounding pair.
}

// ==========================================================================
// makeVerticalSafeWithParallelCheck tests
// ==========================================================================

TEST(VerticalSafeParallel, ParallelFifthsRejected) {
  // Voice 0: C4(60) at tick 480, D4(62) at tick 960.
  // Voice 1: G4(67) at tick 480.
  // Candidate for voice 1 at tick 960 (beat 2, strong): A4(69).
  // Consonance: |69-62|=7 (P5), consonant. A4 not chord tone. Passes.
  // Parallel: prev_tick = 960-480 = 480.
  //   other_curr(v0, 960) = 62. other_prev(v0, 480) = 60.
  //   cand_prev(v1, 480) = 67.
  //   curr_interval = |69-62| = 7. prev_interval = |67-60| = 7.
  //   Both P5 (isPerfectConsonance). compoundToSimple: both 7.
  //   motion_cand = 69-67 = +2. motion_other = 62-60 = +2.
  //   Same direction -> PARALLEL. Rejected.
  auto tl = makeCMajorTimeline(kTicksPerBar * 2);
  std::vector<NoteEvent> notes = {
      makeNote(kTicksPerBeat, kTicksPerBeat, 60, 0),      // C4 at beat 1
      makeNote(2 * kTicksPerBeat, kTicksPerBeat, 62, 0),  // D4 at beat 2
      makeNote(kTicksPerBeat, kTicksPerBeat, 67, 1),      // G4 at beat 1
  };
  auto safe = makeVerticalSafeWithParallelCheck(tl, notes, 2);
  EXPECT_FALSE(safe(2 * kTicksPerBeat, 1, 69));  // Parallel P5 -> rejected.
}

TEST(VerticalSafeParallel, ParallelOctavesRejected) {
  // Voice 0: C4(60) at tick 480, D4(62) at tick 960.
  // Voice 1: C5(72) at tick 480.
  // Candidate for voice 1 at tick 960: D5(74).
  // curr_interval = |74-62| = 12. prev_interval = |72-60| = 12.
  // Both octaves. compoundToSimple: both 0 (unison/octave class).
  // motion_cand = 74-72 = +2. motion_other = 62-60 = +2. Same direction.
  // Parallel P8 -> rejected.
  auto tl = makeCMajorTimeline(kTicksPerBar * 2);
  std::vector<NoteEvent> notes = {
      makeNote(kTicksPerBeat, kTicksPerBeat, 60, 0),
      makeNote(2 * kTicksPerBeat, kTicksPerBeat, 62, 0),
      makeNote(kTicksPerBeat, kTicksPerBeat, 72, 1),
  };
  auto safe = makeVerticalSafeWithParallelCheck(tl, notes, 2);
  EXPECT_FALSE(safe(2 * kTicksPerBeat, 1, 74));  // Parallel P8 -> rejected.
}

TEST(VerticalSafeParallel, ContraryMotionAccepted) {
  // Voice 0: C4(60) at tick 480, D4(62) at tick 960 (up by 2).
  // Voice 1: C5(72) at tick 480.
  // Candidate for voice 1 at tick 960: B4(71) (down by 1) -> contrary motion.
  // curr_interval = |71-62| = 9. prev_interval = |72-60| = 12.
  // compoundToSimple(9) = 9. compoundToSimple(12) = 0. Different -> no parallel.
  // Consonance: |71-62| = 9 (M6), consonant. B4 not chord tone. Passes.
  auto tl = makeCMajorTimeline(kTicksPerBar * 2);
  std::vector<NoteEvent> notes = {
      makeNote(kTicksPerBeat, kTicksPerBeat, 60, 0),
      makeNote(2 * kTicksPerBeat, kTicksPerBeat, 62, 0),
      makeNote(kTicksPerBeat, kTicksPerBeat, 72, 1),
  };
  auto safe = makeVerticalSafeWithParallelCheck(tl, notes, 2);
  EXPECT_TRUE(safe(2 * kTicksPerBeat, 1, 71));  // Contrary motion -> accepted.
}

TEST(VerticalSafeParallel, ObliqueMotionAccepted) {
  // Voice 0: C4(60) at tick 480 (dur=2 beats, spans to tick 1440).
  // Voice 1: G4(67) at tick 480.
  // Candidate for voice 1 at tick 960: G4(67) (same pitch = stationary).
  // curr_interval = |67-60| = 7 (P5). prev_interval = |67-60| = 7 (P5).
  // Same simple interval (both 7). Both perfect consonance.
  // motion_cand = 67-67 = 0 -> oblique. Allowed (the code checks motion_cand != 0).
  // Consonance: G4 is a chord tone -> passes via chord-tone shortcut.
  auto tl = makeCMajorTimeline(kTicksPerBar * 2);
  std::vector<NoteEvent> notes = {
      makeNote(kTicksPerBeat, 2 * kTicksPerBeat, 60, 0),  // C4 sustained
      makeNote(kTicksPerBeat, kTicksPerBeat, 67, 1),       // G4 at beat 1
  };
  auto safe = makeVerticalSafeWithParallelCheck(tl, notes, 2);
  EXPECT_TRUE(safe(2 * kTicksPerBeat, 1, 67));  // Oblique motion -> accepted.
}

TEST(VerticalSafeParallel, NoPreviousBeatSafe) {
  // At tick 0, prev_tick = 0 (since tick < kTicksPerBeat).
  // prev_tick == tick -> no previous beat data -> safe.
  // Consonance: G4(67) is a chord tone -> passes consonance.
  auto tl = makeCMajorTimeline(kTicksPerBar);
  std::vector<NoteEvent> notes = {
      makeNote(0, kTicksPerBeat, 60, 0),  // C4 at tick 0
  };
  auto safe = makeVerticalSafeWithParallelCheck(tl, notes, 2);
  EXPECT_TRUE(safe(0, 1, 67));  // No previous beat -> safe.
}

TEST(VerticalSafeParallel, DifferentPerfectIntervalAccepted) {
  // P5 -> P8 (different simple interval type).
  // Voice 0: C4(60) at tick 480, D4(62) at tick 960.
  // Voice 1: G4(67) at tick 480.
  // Candidate for voice 1 at tick 960: D5(74).
  // curr_interval = |74-62| = 12 -> simple=0 (P8).
  // prev_interval = |67-60| = 7 -> simple=7 (P5).
  // Different simple interval types -> no parallel violation.
  // Consonance: D5(74) -> 74%12=2 (D). Not a chord tone of C-I.
  //   |74-62| = 12, simple=0 (P1/P8). isConsonance(0) = true. Passes.
  auto tl = makeCMajorTimeline(kTicksPerBar * 2);
  std::vector<NoteEvent> notes = {
      makeNote(kTicksPerBeat, kTicksPerBeat, 60, 0),
      makeNote(2 * kTicksPerBeat, kTicksPerBeat, 62, 0),
      makeNote(kTicksPerBeat, kTicksPerBeat, 67, 1),
  };
  auto safe = makeVerticalSafeWithParallelCheck(tl, notes, 2);
  EXPECT_TRUE(safe(2 * kTicksPerBeat, 1, 74));  // P5->P8: different -> accepted.
}

TEST(VerticalSafeParallel, ThirdPartyPairIgnored) {
  // 3 voices. Voices 0 and 1 move in parallel P5 with each other,
  // but the callback only checks the candidate voice (voice 2) against others.
  // Voice 0: C4(60) at tick 480, D4(62) at tick 960.
  // Voice 1: G4(67) at tick 480, A4(69) at tick 960 (parallel P5 with voice 0).
  // Voice 2: E4(64) at tick 480.
  // Candidate for voice 2 at tick 960: F4(65).
  //
  // Check voice 2 (cand=65) vs voice 0 (curr=62):
  //   curr = |65-62| = 3 (m3). isPerfectConsonance(3) = false. Skip parallel.
  //   isConsonance(3) = true. Pass.
  // Check voice 2 (cand=65) vs voice 1 (curr=69):
  //   curr = |65-69| = 4 (M3). isPerfectConsonance(4) = false. Skip parallel.
  //   isConsonance(4) = true. Pass.
  // The parallel P5 between voice 0 and 1 is NOT checked.
  //
  // Consonance: F4(65) -> 65%12=5 (F). Not a chord tone.
  //   All intervals consonant (m3 + M3). Passes.
  auto tl = makeCMajorTimeline(kTicksPerBar * 2);
  std::vector<NoteEvent> notes = {
      makeNote(kTicksPerBeat, kTicksPerBeat, 60, 0),       // C4
      makeNote(2 * kTicksPerBeat, kTicksPerBeat, 62, 0),   // D4
      makeNote(kTicksPerBeat, kTicksPerBeat, 67, 1),       // G4
      makeNote(2 * kTicksPerBeat, kTicksPerBeat, 69, 1),   // A4
      makeNote(kTicksPerBeat, kTicksPerBeat, 64, 2),       // E4
  };
  auto safe = makeVerticalSafeWithParallelCheck(tl, notes, 3);
  EXPECT_TRUE(safe(2 * kTicksPerBeat, 2, 65));  // Third-party parallel ignored.
}

TEST(VerticalSafeParallel, TiedNoteContinuation) {
  // Voice 0 sustains C4 across two beats (tied note).
  // Voice 1: G4(67) at tick 480.
  // Candidate for voice 1 at tick 960: G4(67) (same pitch).
  // Both voices are stationary: motion_cand=0, motion_other=0.
  // Oblique motion -> allowed despite same perfect interval.
  auto tl = makeCMajorTimeline(kTicksPerBar * 2);
  std::vector<NoteEvent> notes = {
      makeNote(kTicksPerBeat, 2 * kTicksPerBeat, 60, 0),  // C4 sustained across beats
      makeNote(kTicksPerBeat, kTicksPerBeat, 67, 1),       // G4 at beat 1
  };
  auto safe = makeVerticalSafeWithParallelCheck(tl, notes, 2);
  EXPECT_TRUE(safe(2 * kTicksPerBeat, 1, 67));  // Both stationary -> oblique -> safe.
}

TEST(VerticalSafeParallel, RestVoiceSkipped) {
  // Voice 1 has no note at the previous beat (silent).
  // Voice 0: D4(62) at tick 960.
  // Voice 1: (nothing at tick 480).
  // Candidate for voice 1 at tick 960: A4(69).
  // pitchAt(voice 1, 480) = -1 -> skip parallel check for this pair.
  // Consonance: A4(69) not chord tone. |69-62|=7 (P5), consonant. Passes.
  auto tl = makeCMajorTimeline(kTicksPerBar * 2);
  std::vector<NoteEvent> notes = {
      makeNote(2 * kTicksPerBeat, kTicksPerBeat, 62, 0),  // D4 at beat 2
  };
  auto safe = makeVerticalSafeWithParallelCheck(tl, notes, 2);
  EXPECT_TRUE(safe(2 * kTicksPerBeat, 1, 69));  // Voice 1 silent at prev -> safe.
}

TEST(VerticalSafeParallel, CandVoiceSilentAtPrevBeat) {
  // Candidate voice has no note at the previous beat.
  // Voice 0: C4(60) at tick 480, D4(62) at tick 960.
  // Voice 1: (nothing at tick 480).
  // Candidate for voice 1 at tick 960: A4(69).
  // cand_prev = pitchAt(voice 1, 480) = -1 -> skip.
  auto tl = makeCMajorTimeline(kTicksPerBar * 2);
  std::vector<NoteEvent> notes = {
      makeNote(kTicksPerBeat, kTicksPerBeat, 60, 0),
      makeNote(2 * kTicksPerBeat, kTicksPerBeat, 62, 0),
  };
  auto safe = makeVerticalSafeWithParallelCheck(tl, notes, 2);
  EXPECT_TRUE(safe(2 * kTicksPerBeat, 1, 69));  // Cand voice silent at prev -> safe.
}

TEST(VerticalSafeParallel, ParallelUnisonsRejected) {
  // Unisons (P1) are also perfect consonances.
  // Voice 0: C4(60) at tick 480, D4(62) at tick 960.
  // Voice 1: C4(60) at tick 480.
  // Candidate for voice 1 at tick 960: D4(62).
  // curr_interval = |62-62| = 0 (P1). prev_interval = |60-60| = 0 (P1).
  // Both P1, same simple (0). motion_cand = 62-60 = +2. motion_other = 62-60 = +2.
  // Same direction -> parallel unisons -> rejected.
  // Consonance: D4(62) -> 62%12=2 (D). Not chord tone. |62-62|=0, consonant.
  auto tl = makeCMajorTimeline(kTicksPerBar * 2);
  std::vector<NoteEvent> notes = {
      makeNote(kTicksPerBeat, kTicksPerBeat, 60, 0),
      makeNote(2 * kTicksPerBeat, kTicksPerBeat, 62, 0),
      makeNote(kTicksPerBeat, kTicksPerBeat, 60, 1),
  };
  auto safe = makeVerticalSafeWithParallelCheck(tl, notes, 2);
  EXPECT_FALSE(safe(2 * kTicksPerBeat, 1, 62));  // Parallel unisons -> rejected.
}

TEST(VerticalSafeParallel, ImperfectConsonanceParallelAllowed) {
  // Parallel M3 is allowed (only P1, P5, P8 are checked).
  // Voice 0: C4(60) at tick 480, D4(62) at tick 960.
  // Voice 1: E4(64) at tick 480.
  // Candidate for voice 1 at tick 960: F#4(66).
  // prev: |60-64|=4 (M3). curr: |62-66|=4 (M3). Both M3.
  // isPerfectConsonance(4) = false -> skip parallel check. Accepted.
  // Consonance: F#(66) not chord tone. |66-62|=4 (M3), consonant.
  auto tl = makeCMajorTimeline(kTicksPerBar * 2);
  std::vector<NoteEvent> notes = {
      makeNote(kTicksPerBeat, kTicksPerBeat, 60, 0),
      makeNote(2 * kTicksPerBeat, kTicksPerBeat, 62, 0),
      makeNote(kTicksPerBeat, kTicksPerBeat, 64, 1),
  };
  auto safe = makeVerticalSafeWithParallelCheck(tl, notes, 2);
  EXPECT_TRUE(safe(2 * kTicksPerBeat, 1, 66));  // Parallel M3 -> allowed.
}

TEST(VerticalSafeParallel, WeakBeatBypassesParallelCheck) {
  // At a weak beat (beat 1), the callback returns true immediately
  // even if motion would create parallel P5.
  auto tl = makeCMajorTimeline(kTicksPerBar * 2);
  std::vector<NoteEvent> notes = {
      makeNote(0, kTicksPerBeat, 60, 0),                  // C4 at beat 0
      makeNote(kTicksPerBeat, kTicksPerBeat, 62, 0),      // D4 at beat 1
      makeNote(0, kTicksPerBeat, 67, 1),                  // G4 at beat 0
  };
  auto safe = makeVerticalSafeWithParallelCheck(tl, notes, 2);
  // Beat 1 (tick 480) is weak -> returns true before parallel check.
  EXPECT_TRUE(safe(kTicksPerBeat, 1, 69));  // Would be parallel P5, but weak beat.
}

TEST(VerticalSafeParallel, ChordToneBypassesParallelCheck) {
  // Chord tone candidates bypass all checks (including parallel).
  // Voice 0: C4(60) at tick 480, D4(62) at tick 960.
  // Voice 1: G4(67) at tick 480.
  // Candidate for voice 1 at tick 960: E4(64).
  // E is a chord tone of C major I -> returns true before parallel check.
  auto tl = makeCMajorTimeline(kTicksPerBar * 2);
  std::vector<NoteEvent> notes = {
      makeNote(kTicksPerBeat, kTicksPerBeat, 60, 0),
      makeNote(2 * kTicksPerBeat, kTicksPerBeat, 62, 0),
      makeNote(kTicksPerBeat, kTicksPerBeat, 67, 1),
  };
  auto safe = makeVerticalSafeWithParallelCheck(tl, notes, 2);
  EXPECT_TRUE(safe(2 * kTicksPerBeat, 1, 64));  // E4: chord tone -> bypasses all.
}

TEST(VerticalSafeParallel, CompoundIntervalParallelDetected) {
  // Parallel P5 across octaves (compound interval).
  // Voice 0: C3(48) at tick 480, D3(50) at tick 960.
  // Voice 1: G4(67) at tick 480.
  // Candidate for voice 1 at tick 960: A4(69).
  // prev_interval = |67-48| = 19 -> isPerfectConsonance(19): simple=7. Yes.
  // curr_interval = |69-50| = 19 -> isPerfectConsonance(19): simple=7. Yes.
  // compoundToSimple: both 7. Same.
  // motion_cand = 69-67 = +2. motion_other = 50-48 = +2. Same direction.
  // Parallel P5 (compound) -> rejected.
  // Consonance: A4(69) not chord tone. |69-50|=19, simple=7 (P5). consonant.
  auto tl = makeCMajorTimeline(kTicksPerBar * 2);
  std::vector<NoteEvent> notes = {
      makeNote(kTicksPerBeat, kTicksPerBeat, 48, 0),
      makeNote(2 * kTicksPerBeat, kTicksPerBeat, 50, 0),
      makeNote(kTicksPerBeat, kTicksPerBeat, 67, 1),
  };
  auto safe = makeVerticalSafeWithParallelCheck(tl, notes, 2);
  EXPECT_FALSE(safe(2 * kTicksPerBeat, 1, 69));  // Compound parallel P5 -> rejected.
}

TEST(VerticalSafeParallel, SimilarMotionSameDirectionRequired) {
  // Motion in opposite directions is not parallel, even with same interval type.
  // Actually, same interval + contrary motion is technically impossible for
  // perfect intervals (the interval must change), but let's verify the logic:
  // Voice 0: D4(62) at tick 480, E4(64) at tick 960 (up by 2).
  // Voice 1: A4(69) at tick 480.
  // Candidate for voice 1 at tick 960: F4(65) (down by 4).
  // prev = |69-62| = 7 (P5). curr = |65-64| = 1 (m2). Different -> no parallel.
  // Consonance: F4(65) not chord tone. |65-64|=1 (m2). Dissonant -> rejected.
  // (This tests that the consonance check catches it, not the parallel check.)
  auto tl = makeCMajorTimeline(kTicksPerBar * 2);
  std::vector<NoteEvent> notes = {
      makeNote(kTicksPerBeat, kTicksPerBeat, 62, 0),
      makeNote(2 * kTicksPerBeat, kTicksPerBeat, 64, 0),
      makeNote(kTicksPerBeat, kTicksPerBeat, 69, 1),
  };
  auto safe = makeVerticalSafeWithParallelCheck(tl, notes, 2);
  EXPECT_FALSE(safe(2 * kTicksPerBeat, 1, 65));  // m2 dissonance -> rejected.
}

TEST(VerticalSafeParallel, OneVoiceStationaryOtherMoves) {
  // Voice 0 stationary, voice 1 moves: oblique motion.
  // Voice 0: G3(55) at tick 480 (dur=2 beats).
  // Voice 1: D4(62) at tick 480.
  // Candidate for voice 1 at tick 960: G3(55).
  // prev = |62-55| = 7 (P5). curr = |55-55| = 0 (P1).
  // Different simple: 7 != 0. Not parallel.
  // Consonance: G3(55) -> 55%12=7 (G). Chord tone of C-I -> safe.
  auto tl = makeCMajorTimeline(kTicksPerBar * 2);
  std::vector<NoteEvent> notes = {
      makeNote(kTicksPerBeat, 2 * kTicksPerBeat, 55, 0),
      makeNote(kTicksPerBeat, kTicksPerBeat, 62, 1),
  };
  auto safe = makeVerticalSafeWithParallelCheck(tl, notes, 2);
  EXPECT_TRUE(safe(2 * kTicksPerBeat, 1, 55));  // Oblique: voice 0 stationary.
}

}  // namespace
}  // namespace bach
