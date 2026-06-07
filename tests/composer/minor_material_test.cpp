#include "composer/minor_material.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "composer/figuration.h"

namespace bach::composer::detail {
namespace {

// C natural/harmonic/melodic minor pitch-class union: C D Eb F G Ab A Bb B.
constexpr bool inMinorUnion(int pitch) {
  const int pcl = ((pitch % 12) + 12) % 12;
  return pcl == 0 || pcl == 2 || pcl == 3 || pcl == 5 || pcl == 7 || pcl == 8 || pcl == 9 ||
         pcl == 10 || pcl == 11;
}

// Adjacent-interval bound from the catalog contract.
constexpr bool intervalAllowed(int semis) {
  const int abs_iv = semis < 0 ? -semis : semis;
  return abs_iv == 1 || abs_iv == 2 || abs_iv == 3 || abs_iv == 4 || abs_iv == 5 || abs_iv == 7 ||
         abs_iv == 8 || abs_iv == 12;
}

// Augmented-2nd adjacency: |interval| == 3 between pitch classes Ab(8) and B(11).
bool isAug2(int lhs, int rhs) {
  const int abs_iv = (lhs - rhs) < 0 ? (rhs - lhs) : (lhs - rhs);
  if (abs_iv != 3) {
    return false;
  }
  const int lo = ((lhs % 12) + 12) % 12;
  const int hi = ((rhs % 12) + 12) % 12;
  return (lo == 8 && hi == 11) || (lo == 11 && hi == 8);
}

TEST(MinorMaterialTest, SubjectsAllInMinorScaleUnion) {
  for (std::size_t slot = 0; slot < kSubjectsMinor.size(); ++slot) {
    for (std::uint8_t pitch : kSubjectsMinor[slot]) {
      EXPECT_TRUE(inMinorUnion(pitch)) << "slot " << slot << " pitch " << int(pitch);
    }
  }
}

TEST(MinorMaterialTest, SubjectsRespectIntervalBounds) {
  for (std::size_t slot = 0; slot < kSubjectsMinor.size(); ++slot) {
    const auto& melody = kSubjectsMinor[slot];
    for (std::size_t idx = 1; idx < melody.size(); ++idx) {
      const int semis = int(melody[idx]) - int(melody[idx - 1]);
      EXPECT_TRUE(intervalAllowed(semis))
          << "slot " << slot << " step " << idx << " interval " << semis;
    }
  }
}

TEST(MinorMaterialTest, SubjectsHaveNoAugmentedSecond) {
  for (std::size_t slot = 0; slot < kSubjectsMinor.size(); ++slot) {
    const auto& melody = kSubjectsMinor[slot];
    for (std::size_t idx = 1; idx < melody.size(); ++idx) {
      EXPECT_FALSE(isAug2(melody[idx - 1], melody[idx]))
          << "slot " << slot << " step " << idx << " (" << int(melody[idx - 1]) << "->"
          << int(melody[idx]) << ")";
    }
  }
}

TEST(MinorMaterialTest, SubjectsEndWithLeadingToneTail) {
  for (std::size_t slot = 0; slot < kSubjectsMinor.size(); ++slot) {
    const auto& melody = kSubjectsMinor[slot];
    EXPECT_EQ(melody[14], 71) << "slot " << slot;
    EXPECT_EQ(melody[15], 72) << "slot " << slot;
  }
}

TEST(MinorMaterialTest, SubjectsStayInRegister) {
  for (std::size_t slot = 0; slot < kSubjectsMinor.size(); ++slot) {
    for (std::uint8_t pitch : kSubjectsMinor[slot]) {
      EXPECT_GE(pitch, 70) << "slot " << slot;
      EXPECT_LE(pitch, 84) << "slot " << slot;
    }
  }
}

TEST(MinorMaterialTest, HarmonyPatternsMinorRootsAndDominant) {
  for (std::size_t pat = 0; pat < kHarmonyPatternsMinor.size(); ++pat) {
    const auto& chords = kHarmonyPatternsMinor[pat];
    for (const auto& chord : chords) {
      const int root = chord.root_pc;
      EXPECT_TRUE(root == 0 || root == 5 || root == 7 || root == 8 || root == 10)
          << "pattern " << pat << " root " << root;
      // V (root 7) is always major (harmonic-minor dominant).
      if (root == 7) {
        EXPECT_FALSE(chord.minor) << "pattern " << pat << " V must be major";
      }
    }
    // Every pattern opens on i (C minor).
    EXPECT_EQ(chords[0].root_pc, 0u) << "pattern " << pat;
    EXPECT_TRUE(chords[0].minor) << "pattern " << pat;
  }
}

TEST(MinorMaterialTest, HarmonyPatternEndings) {
  // Pattern 0 (i-iv-V-i) and 3 (i-V-VI-i) close on i; pattern 1 (i-VI-iv-V)
  // and 2 (i-VII-VI-V) close on the major dominant V.
  EXPECT_EQ(kHarmonyPatternsMinor[0][3].root_pc, 0u);
  EXPECT_TRUE(kHarmonyPatternsMinor[0][3].minor);
  EXPECT_EQ(kHarmonyPatternsMinor[3][3].root_pc, 0u);
  EXPECT_TRUE(kHarmonyPatternsMinor[3][3].minor);
  EXPECT_EQ(kHarmonyPatternsMinor[1][3].root_pc, 7u);
  EXPECT_FALSE(kHarmonyPatternsMinor[1][3].minor);
  EXPECT_EQ(kHarmonyPatternsMinor[2][3].root_pc, 7u);
  EXPECT_FALSE(kHarmonyPatternsMinor[2][3].minor);
}

TEST(MinorMaterialTest, GroundDescendingContourAndRegister) {
  ASSERT_EQ(kGroundMinorDescent.size(), 8u);
  // Overall descending contour: last note strictly below the first.
  EXPECT_LT(kGroundMinorDescent.back(), kGroundMinorDescent.front());
  for (std::uint8_t pitch : kGroundMinorDescent) {
    EXPECT_GE(pitch, 36) << "ground register low";
    EXPECT_LE(pitch, 48) << "ground register high";
  }
  // No augmented-2nd adjacency in the ground line either.
  for (std::size_t idx = 1; idx < kGroundMinorDescent.size(); ++idx) {
    EXPECT_FALSE(isAug2(kGroundMinorDescent[idx - 1], kGroundMinorDescent[idx])) << "step " << idx;
  }
}

// --- Seed-selectable ground variant tables ----------------------------------

// C-major diatonic membership (the major ground tables stay strictly diatonic).
constexpr bool inMajorScale(int pitch) {
  const int pcl = ((pitch % 12) + 12) % 12;
  return pcl == 0 || pcl == 2 || pcl == 4 || pcl == 5 || pcl == 7 || pcl == 9 || pcl == 11;
}

// C natural-minor diatonic membership (the minor ground tables avoid the
// raised melodic/harmonic degrees entirely).
constexpr bool inNaturalMinor(int pitch) {
  const int pcl = ((pitch % 12) + 12) % 12;
  return pcl == 0 || pcl == 2 || pcl == 3 || pcl == 5 || pcl == 7 || pcl == 8 || pcl == 10;
}

// One row per ground variant: a pointer/extent view so the 8-bar passacaglia
// and 4-bar chaconne/goldberg tables share the same data-driven checks.
struct GroundVariantCase {
  const char* table;
  std::size_t variant;
  const std::uint8_t* pitches;
  std::size_t bars;
  bool minor_mode;
};

std::vector<GroundVariantCase> allGroundVariantCases() {
  std::vector<GroundVariantCase> cases;
  for (std::size_t v = 0; v < kGroundVariantCount; ++v) {
    cases.push_back({"passacaglia_minor", v, kPassacagliaGroundsMinor[v].data(), 8, true});
    cases.push_back({"passacaglia_major", v, kPassacagliaGroundsMajor[v].data(), 8, false});
    cases.push_back({"chaconne_minor", v, kChaconneGroundsMinor[v].data(), 4, true});
    cases.push_back({"chaconne_major", v, kChaconneGroundsMajor[v].data(), 4, false});
    cases.push_back({"goldberg_minor", v, kGoldbergGroundsMinor[v].data(), 4, true});
    cases.push_back({"goldberg_major", v, kGoldbergGroundsMajor[v].data(), 4, false});
  }
  return cases;
}

TEST(MinorMaterialTest, GroundVariantsDiatonicAndInBassRegister) {
  for (const GroundVariantCase& c : allGroundVariantCases()) {
    for (std::size_t i = 0; i < c.bars; ++i) {
      const int pitch = c.pitches[i];
      EXPECT_GE(pitch, 36) << c.table << " variant " << c.variant << " bar " << i;
      EXPECT_LE(pitch, 48) << c.table << " variant " << c.variant << " bar " << i;
      EXPECT_TRUE(c.minor_mode ? inNaturalMinor(pitch) : inMajorScale(pitch))
          << c.table << " variant " << c.variant << " bar " << i << " pitch " << pitch;
      if (i > 0) {
        EXPECT_FALSE(isAug2(c.pitches[i - 1], c.pitches[i]))
            << c.table << " variant " << c.variant << " step " << i;
      }
    }
  }
}

TEST(MinorMaterialTest, NewGroundVariantsEndOnTonicOrDominant) {
  // Variants 1 and 2 must close on degree 1 or 5 so the final bar resolves
  // naturally into the next cycle's tonic head. Variant 0 is the historical
  // table and keeps its original ending (goldberg major ends on vi).
  for (const GroundVariantCase& c : allGroundVariantCases()) {
    if (c.variant == 0)
      continue;
    const int last_pc = c.pitches[c.bars - 1] % 12;
    EXPECT_TRUE(last_pc == 0 || last_pc == 7)
        << c.table << " variant " << c.variant << " ends on pc " << last_pc;
  }
}

TEST(MinorMaterialTest, GroundVariantZeroIsHistorical) {
  // Variant 0 must stay byte-identical to the historical tables so seeds that
  // are multiples of kGroundVariantCount reproduce the previous output.
  for (std::size_t i = 0; i < 8; ++i)
    EXPECT_EQ(kPassacagliaGroundsMinor[0][i], kGroundMinorDescent[i]) << "bar " << i;
  EXPECT_EQ(kPassacagliaGroundsMajor[0],
            (std::array<std::uint8_t, 8>{48, 47, 45, 43, 41, 40, 38, 36}));
  EXPECT_EQ(kChaconneGroundsMinor[0], (std::array<std::uint8_t, 4>{48, 46, 44, 43}));
  EXPECT_EQ(kChaconneGroundsMajor[0], (std::array<std::uint8_t, 4>{48, 47, 45, 43}));
  EXPECT_EQ(kGoldbergGroundsMajor[0], (std::array<std::uint8_t, 4>{36, 41, 43, 45}));
  EXPECT_EQ(kGoldbergGroundsMinor[0], (std::array<std::uint8_t, 4>{36, 41, 43, 36}));
}

TEST(MinorMaterialTest, GroundVariantsArePairwiseDistinct) {
  // The three variants of every table must genuinely differ, so regeneration
  // with a different seed family is audible in the ground line.
  const auto cases = allGroundVariantCases();
  for (std::size_t a = 0; a < cases.size(); ++a) {
    for (std::size_t b = a + 1; b < cases.size(); ++b) {
      const auto& lhs = cases[a];
      const auto& rhs = cases[b];
      if (std::string(lhs.table) != rhs.table)
        continue;
      EXPECT_FALSE(std::equal(lhs.pitches, lhs.pitches + lhs.bars, rhs.pitches))
          << lhs.table << " variants " << lhs.variant << " and " << rhs.variant;
    }
  }
}

TEST(MinorMaterialTest, GroundVariantIndexCyclesBySeed) {
  EXPECT_EQ(groundVariantIndex(1), 1u);
  EXPECT_EQ(groundVariantIndex(2), 2u);
  EXPECT_EQ(groundVariantIndex(3), 0u);  // variant-0 probe seed (seed 0 = CLI auto).
  EXPECT_EQ(groundVariantIndex(4), 1u);  // wraps: same ground family as seed 1.
  EXPECT_EQ(groundVariantIndex(6), 0u);
}

TEST(MinorMaterialTest, DiatonicTriadQuality) {
  // Major mode: ii / iii / vi are minor, I / IV / V (and a B bass) major.
  EXPECT_FALSE(diatonicTriadMinor(0, false));
  EXPECT_TRUE(diatonicTriadMinor(2, false));
  EXPECT_TRUE(diatonicTriadMinor(4, false));
  EXPECT_FALSE(diatonicTriadMinor(5, false));
  EXPECT_FALSE(diatonicTriadMinor(7, false));
  EXPECT_TRUE(diatonicTriadMinor(9, false));
  EXPECT_FALSE(diatonicTriadMinor(11, false));
  // Minor mode: i / ii / iv minor; III / V (harmonic dominant) / VI / VII major.
  EXPECT_TRUE(diatonicTriadMinor(0, true));
  EXPECT_TRUE(diatonicTriadMinor(2, true));
  EXPECT_FALSE(diatonicTriadMinor(3, true));
  EXPECT_TRUE(diatonicTriadMinor(5, true));
  EXPECT_FALSE(diatonicTriadMinor(7, true));
  EXPECT_FALSE(diatonicTriadMinor(8, true));
  EXPECT_FALSE(diatonicTriadMinor(10, true));
}

TEST(MinorMaterialTest, MinorScaleUpHarmonicContextRaisesSixSeven) {
  // Ascending from G (67) over a V chord must yield A natural (69) then B
  // natural (71): the melodic-minor upper tetrachord, NO Ab->B aug-2nd.
  EXPECT_EQ(minorScaleUp(67, 1, /*harmonic_context=*/true), 69);  // G -> A natural
  EXPECT_EQ(minorScaleUp(67, 2, /*harmonic_context=*/true), 71);  // G -> A -> B natural
  EXPECT_EQ(minorScaleUp(67, 3, /*harmonic_context=*/true), 72);  // -> C
  // The 6->7 hop itself is A natural -> B natural (interval 2), never Ab->B (3).
  EXPECT_EQ(minorScaleUp(69, 1, /*harmonic_context=*/true), 71);
}

TEST(MinorMaterialTest, MinorScaleUpNaturalContextUsesFlatSixSeven) {
  // Natural-minor context: G(67) -> Ab(68) -> Bb(70).
  EXPECT_EQ(minorScaleUp(67, 1, /*harmonic_context=*/false), 68);  // G -> Ab
  EXPECT_EQ(minorScaleUp(67, 2, /*harmonic_context=*/false), 70);  // -> Bb
  EXPECT_EQ(minorScaleUp(67, 3, /*harmonic_context=*/false), 72);  // -> C
}

TEST(MinorMaterialTest, MinorScaleUpDeterministicAndLowerTetrachordShared) {
  // The lower tetrachord (C D Eb F) is identical in both contexts.
  for (int steps = 0; steps <= 3; ++steps) {
    EXPECT_EQ(minorScaleUp(60, steps, true), minorScaleUp(60, steps, false)) << "steps " << steps;
  }
  // Determinism: repeated calls agree.
  EXPECT_EQ(minorScaleUp(67, 5, true), minorScaleUp(67, 5, true));
  EXPECT_EQ(minorScaleUp(60, 0, true), 60);  // zero steps is identity.
}

TEST(MinorMaterialTest, UsePicardyDeterministicByParity) {
  EXPECT_TRUE(usePicardy(0));
  EXPECT_TRUE(usePicardy(42));
  EXPECT_FALSE(usePicardy(1));
  EXPECT_FALSE(usePicardy(7));
  // Determinism: same seed always agrees.
  EXPECT_EQ(usePicardy(12345), usePicardy(12345));
}

}  // namespace
}  // namespace bach::composer::detail
