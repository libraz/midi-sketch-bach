// Linear per-bar form builder tests: cello prelude (BWV1007 monophonic
// arpeggio flow) and trio sonata (three independent voices).
//
// Covers the full design matrix for both real builders:
//   seeds {1, 5, 42, 99} x {Major, Minor} x bars {natural, 2x natural, 128}.
// Asserts, per cell:
//   - the full Composer validates Ok (no failures);
//   - the build is deterministic (identical notes for identical inputs);
//   - the note span ends at bars * kTicksPerBar;
//   - cello stays monophonic (no overlapping onsets / single voice);
//   - trio carries three distinct voices, the pedal voice is all-quarters in
//     the low register, density rises toward the climax cycle;
//   - the final bar's harmony is the tonic.

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <map>
#include <set>
#include <utility>
#include <vector>

#include "composer/arc.h"
#include "composer/composer.h"
#include "composer/form_director.h"
#include "core/basic_types.h"

namespace bach::composer {
namespace {

constexpr std::array<std::uint32_t, 4> kSeeds = {{1, 5, 42, 99}};
constexpr std::array<bool, 2> kMinorFlags = {{false, true}};

// Resolve the three test lengths for a form: natural, 2x natural, and 128
// (each still passes through resolveBars snap/clamp inside the director).
std::array<std::uint16_t, 3> testLengths(FormType form) {
  const FormSpec& spec = formSpec(form);
  return {{spec.natural_bars, static_cast<std::uint16_t>(spec.natural_bars * 2), 128}};
}

ComposeResult build(FormType form, std::uint32_t seed, bool is_minor, std::uint16_t bars,
                    HarnessFixture* fixture_out) {
  ComposeRequest req;
  req.form = form;
  req.seed = seed;
  req.is_minor = is_minor;
  req.target_bars = bars;
  HarnessFixture fixture;
  EXPECT_EQ(buildFormFixture(req, &fixture), FormDirectorStatus::Ok);
  if (fixture_out != nullptr)
    *fixture_out = fixture;
  return Composer{}.run(fixture.material, fixture.harmony, fixture.voice_plan);
}

std::string firstFailure(const ComposeResult& r) {
  return r.validation.failures.empty() ? std::string{} : r.validation.failures.front().rule_id;
}

// Resolve the realized bar count the director snapped a target to (the
// HarmonicPlan carries one chord per bar).
int realizedBars(const HarnessFixture& fx) {
  return static_cast<int>(fx.harmony.chords.size());
}

// --- CelloPrelude -----------------------------------------------------------

TEST(FormLinearCello, ValidatesCleanAcrossMatrix) {
  for (std::uint32_t seed : kSeeds) {
    for (bool minor : kMinorFlags) {
      for (std::uint16_t bars : testLengths(FormType::CelloPrelude)) {
        HarnessFixture fx;
        const ComposeResult r = build(FormType::CelloPrelude, seed, minor, bars, &fx);
        EXPECT_EQ(r.validation.status, ValidationStatus::Ok)
            << "seed=" << seed << " minor=" << minor << " bars=" << bars
            << " first=" << firstFailure(r);
        EXPECT_TRUE(r.validation.failures.empty())
            << "seed=" << seed << " minor=" << minor << " bars=" << bars
            << " first=" << firstFailure(r);
        EXPECT_FALSE(r.notes.empty());
      }
    }
  }
}

TEST(FormLinearCello, IsDeterministic) {
  for (std::uint32_t seed : kSeeds) {
    for (bool minor : kMinorFlags) {
      const std::uint16_t bars = testLengths(FormType::CelloPrelude)[1];
      const ComposeResult a = build(FormType::CelloPrelude, seed, minor, bars, nullptr);
      const ComposeResult b = build(FormType::CelloPrelude, seed, minor, bars, nullptr);
      ASSERT_EQ(a.notes.size(), b.notes.size()) << "seed=" << seed << " minor=" << minor;
      for (std::size_t i = 0; i < a.notes.size(); ++i) {
        EXPECT_EQ(a.notes[i].start_tick, b.notes[i].start_tick);
        EXPECT_EQ(a.notes[i].pitch, b.notes[i].pitch);
        EXPECT_EQ(a.notes[i].voice, b.notes[i].voice);
      }
    }
  }
}

TEST(FormLinearCello, SpanEndsAtBarBoundaryAndIsMonophonic) {
  for (std::uint32_t seed : kSeeds) {
    for (bool minor : kMinorFlags) {
      for (std::uint16_t bars : testLengths(FormType::CelloPrelude)) {
        HarnessFixture fx;
        const ComposeResult r = build(FormType::CelloPrelude, seed, minor, bars, &fx);
        ASSERT_FALSE(r.notes.empty());
        const int actual_bars = realizedBars(fx);
        const Tick piece_end = static_cast<Tick>(actual_bars) * kTicksPerBar;

        // Notes sorted by onset; last note must end at the final bar boundary,
        // and no two notes may overlap (monophonic single voice).
        std::vector<NoteEvent> notes = r.notes;
        std::sort(notes.begin(), notes.end(), [](const NoteEvent& x, const NoteEvent& y) {
          return x.start_tick < y.start_tick;
        });
        const NoteEvent& last = notes.back();
        EXPECT_EQ(last.start_tick + last.duration, piece_end)
            << "seed=" << seed << " minor=" << minor << " bars=" << bars;
        for (std::size_t i = 0; i + 1 < notes.size(); ++i) {
          EXPECT_EQ(notes[i].voice, 0) << "Flow is monophonic";
          EXPECT_LE(notes[i].start_tick + notes[i].duration, notes[i + 1].start_tick)
              << "overlap at i=" << i << " seed=" << seed;
        }
      }
    }
  }
}

TEST(FormLinearCello, FinalBarHarmonyIsTonic) {
  for (std::uint32_t seed : kSeeds) {
    for (bool minor : kMinorFlags) {
      for (std::uint16_t bars : testLengths(FormType::CelloPrelude)) {
        HarnessFixture fx;
        build(FormType::CelloPrelude, seed, minor, bars, &fx);
        ASSERT_FALSE(fx.harmony.chords.empty());
        EXPECT_EQ(fx.harmony.chords.back().root_pc, 0)
            << "seed=" << seed << " minor=" << minor << " bars=" << bars;
      }
    }
  }
}

// The cello line mixes running figuration with pedal-point bariolage (the
// BWV1007 note language): most adjacent sixteenths move by a step or small
// skip, the bariolage bars add deliberate pedal-to-top leaps, and there are
// no remote (> octave) leaps. The real BWV1007/1 prelude's leap (> P5) share
// is 0.162; the 0.20 ceiling keeps the line inside that envelope while still
// guarding the catastrophic broken-chord regression (constant 7-16 semitone
// root-fifth-third jumps on every note, leap share ~0.5+).
TEST(FormLinearCello, IsStepwiseDominantWithNoRemoteLeaps) {
  for (std::uint32_t seed : kSeeds) {
    for (bool minor : kMinorFlags) {
      for (std::uint16_t bars : testLengths(FormType::CelloPrelude)) {
        const ComposeResult r = build(FormType::CelloPrelude, seed, minor, bars, nullptr);
        std::vector<NoteEvent> notes = r.notes;
        std::sort(notes.begin(), notes.end(), [](const NoteEvent& x, const NoteEvent& y) {
          return x.start_tick < y.start_tick;
        });
        int leaps = 0;
        int remote = 0;
        for (std::size_t idx = 1; idx < notes.size(); ++idx) {
          const int interval =
              static_cast<int>(notes[idx].pitch) - static_cast<int>(notes[idx - 1].pitch);
          if (std::abs(interval) > 7)
            ++leaps;
          if (std::abs(interval) > 12)
            ++remote;
        }
        const double leap_ratio =
            notes.size() > 1 ? static_cast<double>(leaps) / static_cast<double>(notes.size() - 1)
                             : 0.0;
        EXPECT_LE(leap_ratio, 0.20) << "seed=" << seed << " minor=" << minor << " bars=" << bars;
        EXPECT_EQ(remote, 0) << "seed=" << seed << " minor=" << minor << " bars=" << bars;
      }
    }
  }
}

// The cello's per-beat cell contour rotates per 4-bar cycle: consecutive
// cycles trace distinct figures, locked via the cell's interval signature
// (pitch deltas from the cell's first note). Every shape still opens on the
// anchor, so the implicit-stream invariants (per-cell min/max constant within
// the bar) keep holding -- asserted by the existing suite; here only the
// rotation itself is locked.
TEST(FormLinearCello, CellContourRotatesAcrossCycles) {
  for (std::uint32_t seed : kSeeds) {
    const ComposeResult r = build(FormType::CelloPrelude, seed, /*is_minor=*/false, 32, nullptr);
    std::vector<NoteEvent> notes = r.notes;
    std::sort(notes.begin(), notes.end(),
              [](const NoteEvent& x, const NoteEvent& y) { return x.start_tick < y.start_tick; });
    // 30 sixteenth bars + the two held cadential-landing notes.
    ASSERT_GE(notes.size(), 30u * 16u + 2u);
    std::set<std::array<int, 3>> signatures;
    for (int cycle = 0; cycle < 8; ++cycle) {
      // First cell (four sixteenths) of the cycle's first bar.
      const std::size_t base = static_cast<std::size_t>(cycle) * 4 * 16;
      std::array<int, 3> sig{};
      for (int idx = 1; idx < 4; ++idx)
        sig[static_cast<std::size_t>(idx - 1)] =
            static_cast<int>(notes[base + static_cast<std::size_t>(idx)].pitch) -
            static_cast<int>(notes[base].pitch);
      signatures.insert(sig);
    }
    EXPECT_GE(signatures.size(), 2u) << "seed " << seed << ": cell contour must rotate";
  }
}

// A long (64-bar) cello line keeps its melodic-interval BIGRAM surface spread:
// the reference corpus's most common bigram carries only ~2.6% of the mass,
// while a single figure looped bar after bar concentrates >12% on one pendulum
// bigram. The four-figure rotation (oscillation with per-cell shapes, half-bar
// run arches, broken-third chains, bariolage) holds the top bigram at or below
// a tenth of all transitions.
TEST(FormLinearCello, LongFormBigramSurfaceStaysSpread) {
  for (std::uint32_t seed : {1u, 5u, 42u}) {
    const ComposeResult r = build(FormType::CelloPrelude, seed, /*is_minor=*/false, 64, nullptr);
    std::vector<NoteEvent> notes = r.notes;
    std::sort(notes.begin(), notes.end(),
              [](const NoteEvent& x, const NoteEvent& y) { return x.start_tick < y.start_tick; });
    ASSERT_GE(notes.size(), 3u);
    std::map<std::pair<int, int>, int> bigrams;
    int total = 0;
    for (std::size_t idx = 2; idx < notes.size(); ++idx) {
      const int first =
          static_cast<int>(notes[idx - 1].pitch) - static_cast<int>(notes[idx - 2].pitch);
      const int second =
          static_cast<int>(notes[idx].pitch) - static_cast<int>(notes[idx - 1].pitch);
      ++bigrams[{first, second}];
      ++total;
    }
    int top = 0;
    for (const auto& [bigram, count] : bigrams)
      top = std::max(top, count);
    EXPECT_LE(static_cast<double>(top) / static_cast<double>(total), 0.10)
        << "seed " << seed << ": top bigram " << top << "/" << total;
  }
}

// --- TrioSonata -------------------------------------------------------------

TEST(FormLinearTrio, ValidatesCleanAcrossMatrix) {
  for (std::uint32_t seed : kSeeds) {
    for (bool minor : kMinorFlags) {
      for (std::uint16_t bars : testLengths(FormType::TrioSonata)) {
        HarnessFixture fx;
        const ComposeResult r = build(FormType::TrioSonata, seed, minor, bars, &fx);
        EXPECT_EQ(r.validation.status, ValidationStatus::Ok)
            << "seed=" << seed << " minor=" << minor << " bars=" << bars
            << " first=" << firstFailure(r);
        EXPECT_TRUE(r.validation.failures.empty())
            << "seed=" << seed << " minor=" << minor << " bars=" << bars
            << " first=" << firstFailure(r);
        EXPECT_FALSE(r.notes.empty());
      }
    }
  }
}

TEST(FormLinearTrio, IsDeterministic) {
  for (std::uint32_t seed : kSeeds) {
    for (bool minor : kMinorFlags) {
      const std::uint16_t bars = testLengths(FormType::TrioSonata)[1];
      const ComposeResult a = build(FormType::TrioSonata, seed, minor, bars, nullptr);
      const ComposeResult b = build(FormType::TrioSonata, seed, minor, bars, nullptr);
      ASSERT_EQ(a.notes.size(), b.notes.size()) << "seed=" << seed << " minor=" << minor;
      for (std::size_t i = 0; i < a.notes.size(); ++i) {
        EXPECT_EQ(a.notes[i].start_tick, b.notes[i].start_tick);
        EXPECT_EQ(a.notes[i].pitch, b.notes[i].pitch);
        EXPECT_EQ(a.notes[i].voice, b.notes[i].voice);
      }
    }
  }
}

TEST(FormLinearTrio, ThreeDistinctVoicesAndPedalIsLowQuarters) {
  for (std::uint32_t seed : kSeeds) {
    for (bool minor : kMinorFlags) {
      for (std::uint16_t bars : testLengths(FormType::TrioSonata)) {
        HarnessFixture fx;
        const ComposeResult r = build(FormType::TrioSonata, seed, minor, bars, &fx);
        ASSERT_FALSE(r.notes.empty());

        // Three distinct voices present.
        bool seen[3] = {false, false, false};
        for (const NoteEvent& note : r.notes) {
          ASSERT_LT(note.voice, 3) << "unexpected voice " << static_cast<int>(note.voice);
          seen[note.voice] = true;
        }
        EXPECT_TRUE(seen[0] && seen[1] && seen[2])
            << "seed=" << seed << " minor=" << minor << " bars=" << bars;

        // Pedal voice (V2): every note is a quarter note (kTicksPerBeat) in the
        // low register (<= G3 = 55), except the final bar where the pedal
        // joins the held closing chord with one whole-note root.
        const Tick final_bar_tick = static_cast<Tick>(realizedBars(fx) - 1) * kTicksPerBar;
        int pedal_notes = 0;
        for (const NoteEvent& note : r.notes) {
          if (note.voice != 2)
            continue;
          ++pedal_notes;
          if (note.start_tick >= final_bar_tick) {
            EXPECT_EQ(note.duration, static_cast<Tick>(kTicksPerBar))
                << "pedal final note not held; seed=" << seed;
          } else {
            EXPECT_EQ(note.duration, static_cast<Tick>(kTicksPerBeat))
                << "pedal not a quarter; seed=" << seed;
          }
          EXPECT_LE(note.pitch, 55) << "pedal out of low register; seed=" << seed;
        }
        // One quarter per beat, except the held final bar (one whole note).
        EXPECT_EQ(pedal_notes, 4 * (realizedBars(fx) - 1) + 1)
            << "seed=" << seed << " minor=" << minor << " bars=" << bars;
      }
    }
  }
}

TEST(FormLinearTrio, UpperVoicesStayAbovePedal) {
  // The three voices keep their register bands (V0 high, V1 mid, V2 low) so no
  // voice crossing occurs: the pedal's highest note is below the mid voice's
  // lowest, which is below the top voice's lowest.
  for (std::uint32_t seed : kSeeds) {
    for (bool minor : kMinorFlags) {
      const std::uint16_t bars = testLengths(FormType::TrioSonata)[0];
      const ComposeResult r = build(FormType::TrioSonata, seed, minor, bars, nullptr);
      std::uint8_t v2_max = 0;
      std::uint8_t v1_min = 255;
      std::uint8_t v0_min = 255;
      for (const NoteEvent& note : r.notes) {
        if (note.voice == 2)
          v2_max = std::max(v2_max, note.pitch);
        else if (note.voice == 1)
          v1_min = std::min(v1_min, note.pitch);
        else if (note.voice == 0)
          v0_min = std::min(v0_min, note.pitch);
      }
      EXPECT_LT(v2_max, v1_min) << "pedal crosses mid; seed=" << seed << " minor=" << minor;
      EXPECT_LT(v1_min, v0_min) << "mid floor below top floor; seed=" << seed;
    }
  }
}

TEST(FormLinearTrio, DensityRisesTowardClimaxCycle) {
  // The upper-voice note count in the climax 4-bar cycle exceeds the count in
  // the opening (Establish) cycle: the arc density curve genuinely shapes the
  // texture. Use a long piece so the curve has room.
  for (std::uint32_t seed : kSeeds) {
    const std::uint16_t bars = 128;
    const ComposeResult r = build(FormType::TrioSonata, seed, /*is_minor=*/false, bars, nullptr);

    // Locate the climax cycle.
    const std::size_t cycle_count = static_cast<std::size_t>(bars) / 4;
    std::size_t climax_cycle = 0;
    for (std::size_t c = 0; c < cycle_count; ++c) {
      if (arcPoint(c, cycle_count).is_climax) {
        climax_cycle = c;
        break;
      }
    }
    ASSERT_GT(climax_cycle, 0u);

    auto upperNotesInCycle = [&](std::size_t cycle) {
      const Tick lo = static_cast<Tick>(cycle * 4) * kTicksPerBar;
      const Tick hi = lo + static_cast<Tick>(4) * kTicksPerBar;
      int count = 0;
      for (const NoteEvent& note : r.notes) {
        if (note.voice == 2)
          continue;  // pedal density is constant.
        if (note.start_tick >= lo && note.start_tick < hi)
          ++count;
      }
      return count;
    };

    EXPECT_GT(upperNotesInCycle(climax_cycle), upperNotesInCycle(0))
        << "seed=" << seed << " climax_cycle=" << climax_cycle;
  }
}

TEST(FormLinearTrio, FinalBarHarmonyIsTonic) {
  for (std::uint32_t seed : kSeeds) {
    for (bool minor : kMinorFlags) {
      for (std::uint16_t bars : testLengths(FormType::TrioSonata)) {
        HarnessFixture fx;
        build(FormType::TrioSonata, seed, minor, bars, &fx);
        ASSERT_FALSE(fx.harmony.chords.empty());
        EXPECT_EQ(fx.harmony.chords.back().root_pc, 0)
            << "seed=" << seed << " minor=" << minor << " bars=" << bars;
      }
    }
  }
}

// Every beat onset of every trio voice lands on a chord tone, so the per-beat
// vertical sample (the scorer's vertical_dissonance_ratio) is consonant: at
// each beat the sounding pitches form only consonant interval classes. This
// guards the per-beat chord-tone anchoring of the two manual voices that drives
// the dissonance ratio to ~0.
TEST(FormLinearTrio, BeatOnsetsAreVerticallyConsonant) {
  // Dissonant interval classes (mod 12): m2 M2 TT m7 M7.
  auto isDissonant = [](int interval_class) {
    const int ic = ((interval_class % 12) + 12) % 12;
    return ic == 1 || ic == 2 || ic == 6 || ic == 10 || ic == 11;
  };
  for (std::uint32_t seed : kSeeds) {
    for (bool minor : kMinorFlags) {
      for (std::uint16_t bars : testLengths(FormType::TrioSonata)) {
        HarnessFixture fx;
        const ComposeResult r = build(FormType::TrioSonata, seed, minor, bars, &fx);
        ASSERT_FALSE(r.notes.empty());
        const Tick piece_end = static_cast<Tick>(realizedBars(fx)) * kTicksPerBar;

        int samples = 0;
        int dissonant = 0;
        for (Tick tick = 0; tick < piece_end; tick += kTicksPerBeat) {
          std::vector<std::uint8_t> sounding;
          for (const NoteEvent& note : r.notes) {
            if (note.start_tick <= tick && tick < note.start_tick + note.duration)
              sounding.push_back(note.pitch);
          }
          if (sounding.size() < 2)
            continue;
          ++samples;
          bool bad = false;
          for (std::size_t lhs = 0; lhs < sounding.size(); ++lhs)
            for (std::size_t rhs = lhs + 1; rhs < sounding.size(); ++rhs)
              if (isDissonant(static_cast<int>(sounding[lhs]) - static_cast<int>(sounding[rhs])))
                bad = true;
          if (bad)
            ++dissonant;
        }
        const double vdr =
            samples > 0 ? static_cast<double>(dissonant) / static_cast<double>(samples) : 0.0;
        EXPECT_LE(vdr, 0.12) << "seed=" << seed << " minor=" << minor << " bars=" << bars;
      }
    }
  }
}

// Each trio voice moves predominantly by step or small skip with no remote
// (> octave) leaps: the per-voice large-leap fraction stays small. This guards
// the across-bar voice-leading of the manual voices and the compact pedal
// voicing that removed the cycle-boundary register jumps.
TEST(FormLinearTrio, VoicesAreStepwiseWithNoRemoteLeaps) {
  for (std::uint32_t seed : kSeeds) {
    for (bool minor : kMinorFlags) {
      for (std::uint16_t bars : testLengths(FormType::TrioSonata)) {
        const ComposeResult r = build(FormType::TrioSonata, seed, minor, bars, nullptr);
        std::array<std::vector<NoteEvent>, 3> by_voice;
        for (const NoteEvent& note : r.notes) {
          ASSERT_LT(note.voice, 3);
          by_voice[note.voice].push_back(note);
        }
        int leaps = 0;
        int remote = 0;
        int intervals = 0;
        for (auto& voice_notes : by_voice) {
          std::sort(
              voice_notes.begin(), voice_notes.end(),
              [](const NoteEvent& x, const NoteEvent& y) { return x.start_tick < y.start_tick; });
          for (std::size_t idx = 1; idx < voice_notes.size(); ++idx) {
            const int interval = static_cast<int>(voice_notes[idx].pitch) -
                                 static_cast<int>(voice_notes[idx - 1].pitch);
            ++intervals;
            if (std::abs(interval) > 7)
              ++leaps;
            if (std::abs(interval) > 12)
              ++remote;
          }
        }
        const double leap_ratio =
            intervals > 0 ? static_cast<double>(leaps) / static_cast<double>(intervals) : 0.0;
        EXPECT_LE(leap_ratio, 0.08) << "seed=" << seed << " minor=" << minor << " bars=" << bars;
        EXPECT_EQ(remote, 0) << "seed=" << seed << " minor=" << minor << " bars=" << bars;
      }
    }
  }
}

// --- Trio vocabulary rotation -------------------------------------------------

// The manual voices rotate their intra-beat figure vocabulary per 4-bar cycle
// (arc / oscillation / figura corta), phase-shifted by one slot. Locked from
// the material via the DENSE line's per-cycle signature: the figura corta
// cycles carry mixed eighth+sixteenth bars, the uniform-sixteenth cycles split
// into the rising arc (3 distinct pitches inside a beat) and the neighbour
// oscillation (2 distinct pitches inside a beat) -- so the dense line must
// show at least two distinct signatures across the piece.
TEST(FormLinearTrio, DenseLineRotatesIntraBeatVocabulary) {
  for (std::uint32_t seed : kSeeds) {
    HarnessFixture fx;
    build(FormType::TrioSonata, seed, /*is_minor=*/false, 32, &fx);
    ASSERT_GE(fx.material.trio_voices.size(), 2u);
    const int cycles = realizedBars(fx) / 4;
    ASSERT_GE(cycles, 3);

    // Signature of one cycle's dense line, read from its first bar:
    //   'c' = figura corta (mixed durations), 'a' = sixteenth arc (3 distinct
    //   pitches in the first beat), 'o' = sixteenth oscillation (2 distinct).
    std::set<char> signatures;
    for (int cycle = 0; cycle < cycles; ++cycle) {
      // The dense sixteenth line alternates voices per cycle (V0 on even).
      const TrioVoiceLine& dense =
          fx.material.trio_voices[static_cast<std::size_t>(cycle % 2 == 0 ? 0 : 1)];
      const Tick bar_start = static_cast<Tick>(cycle) * 4 * kTicksPerBar;
      std::vector<const MaterialNote*> beat_notes;
      bool mixed_durations = false;
      Tick first_dur = 0;
      for (const auto& n : dense.notes) {
        if (n.start_tick < bar_start || n.start_tick >= bar_start + kTicksPerBar)
          continue;
        if (first_dur == 0)
          first_dur = n.duration;
        else if (n.duration != first_dur)
          mixed_durations = true;
        if (n.start_tick < bar_start + kTicksPerBeat)
          beat_notes.push_back(&n);
      }
      ASSERT_FALSE(beat_notes.empty()) << "seed " << seed << " cycle " << cycle;
      if (mixed_durations) {
        signatures.insert('c');
        continue;
      }
      std::set<std::uint8_t> distinct;
      for (const MaterialNote* n : beat_notes)
        distinct.insert(n->pitch);
      signatures.insert(distinct.size() >= 3 ? 'a' : 'o');
    }
    EXPECT_GE(signatures.size(), 2u)
        << "seed " << seed << ": the dense line must rotate its vocabulary";
  }
}

}  // namespace
}  // namespace bach::composer
