#include "composer/cadenced_progression.h"

#include <cstddef>

#include "composer/minor_material.h"
#include "core/basic_types.h"

namespace bach::composer {

namespace {

using detail::ChordSpec;
using detail::Mode;

// The cello's implicit-voice shapes need a dedicated migration before they can
// safely admit every natural-minor root. Keep that form-local admissibility
// guard while the shared validator now evaluates the real directional context.
constexpr bool inHarmonicMinor(int pc) {
  const int p = ((pc % 12) + 12) % 12;
  return p == 0 || p == 2 || p == 3 || p == 5 || p == 7 || p == 8 || p == 11;
}

}  // namespace

std::vector<ChordSpec> buildCadencedProgression(int bars, std::uint32_t seed, Mode mode,
                                                bool cello_implicit_safe) {
  const auto& catalog =
      (mode == Mode::Minor) ? detail::kHarmonyPatternsMinor : detail::kHarmonyPatterns;
  std::vector<std::size_t> admissible;
  for (std::size_t pat = 0; pat < catalog.size(); ++pat) {
    bool valid = true;
    if (cello_implicit_safe && mode == Mode::Minor) {
      for (const ChordSpec& spec : catalog[pat])
        valid = valid && inHarmonicMinor(spec.root_pc);
    }
    if (valid)
      admissible.push_back(pat);
  }
  if (admissible.empty())
    admissible.push_back(0);
  std::vector<ChordSpec> chords;
  chords.reserve(static_cast<std::size_t>(bars));
  for (int bar = 0; bar < bars; ++bar) {
    const int block = bar / 4;
    const std::size_t pat =
        admissible[(static_cast<std::size_t>(seed) + static_cast<std::size_t>(block)) %
                   admissible.size()];
    chords.push_back(catalog[pat][static_cast<std::size_t>(bar % 4)]);
  }
  // Design-valued final cadence: V (dominant, always major) then tonic. In
  // minor the tonic stays minor unless the seed elects a Picardy third.
  const bool tonic_minor = (mode == Mode::Minor) && !detail::usePicardy(seed);
  chords[static_cast<std::size_t>(bars - 2)] = {7, false};
  chords[static_cast<std::size_t>(bars - 1)] = {0, tonic_minor};
  return chords;
}

void writeBarChords(HarnessFixture& out, const std::vector<ChordSpec>& chords, Mode mode) {
  out.harmony.tonic_pc = 0;
  out.harmony.is_minor = (mode == Mode::Minor);
  for (std::size_t bar = 0; bar < chords.size(); ++bar) {
    ChordEvent chord;
    chord.start_tick = static_cast<Tick>(bar) * kTicksPerBar;
    chord.root_pc = chords[bar].root_pc;
    if (chords[bar].seventh) {
      chord.quality = chords[bar].minor ? ChordQuality::Minor7 : ChordQuality::Dominant7;
    } else {
      chord.quality = chords[bar].minor ? ChordQuality::Minor : ChordQuality::Major;
    }
    out.harmony.chords.push_back(chord);
  }
}

}  // namespace bach::composer
