// Cadence grammar vocabulary shared by harmony-aware generators.

#ifndef BACH_HARMONY_CADENCE_VOCABULARY_H
#define BACH_HARMONY_CADENCE_VOCABULARY_H

#include <cstdint>
#include <utility>

#include "harmony/harmonic_timeline.h"

namespace bach {

struct CadenceApproach {
  const char* name;
  CadenceType type;
  const int8_t* soprano_approach;
  uint8_t soprano_len;
  const int8_t* bass_approach;
  uint8_t bass_len;
  const char* provenance;
};

struct CadenceInnerVoiceGuidance {
  uint8_t max_leap_semitones = 4;
  bool prefer_4_3_resolution = true;
  bool prefer_7_6_resolution = true;
};

inline constexpr int8_t kPAC_LeadingTone_sop[] = {+1};
inline constexpr int8_t kPAC_LeadingTone_bass[] = {-3};
inline constexpr CadenceApproach kPAC_LeadingTone = {"PAC_LeadingTone",
                                                     CadenceType::Perfect,
                                                     kPAC_LeadingTone_sop,
                                                     1,
                                                     kPAC_LeadingTone_bass,
                                                     1,
                                                     "BWV578 bar 69, BWV575 bar 88"};

inline constexpr int8_t kIAC_ThirdSoprano_sop[] = {+1};
inline constexpr int8_t kIAC_ThirdSoprano_bass[] = {-3};
inline constexpr CadenceApproach kIAC_ThirdSoprano = {"IAC_ThirdSoprano",
                                                      CadenceType::ImperfectAuthentic,
                                                      kIAC_ThirdSoprano_sop,
                                                      1,
                                                      kIAC_ThirdSoprano_bass,
                                                      1,
                                                      "Common imperfect authentic formula"};

inline constexpr int8_t kHC_StepUp_sop[] = {+1, +2};
inline constexpr int8_t kHC_StepUp_bass[] = {+4};
inline constexpr CadenceApproach kHC_StepUp = {"HC_StepUp",
                                               CadenceType::Half,
                                               kHC_StepUp_sop,
                                               2,
                                               kHC_StepUp_bass,
                                               1,
                                               "BWV578 bar 10, BWV576 bar 28"};

inline constexpr int8_t kDC_LeadingTone_sop[] = {+1};
inline constexpr int8_t kDC_LeadingTone_bass[] = {+1};
inline constexpr CadenceApproach kDC_LeadingTone = {"DC_LeadingTone",
                                                    CadenceType::Deceptive,
                                                    kDC_LeadingTone_sop,
                                                    1,
                                                    kDC_LeadingTone_bass,
                                                    1,
                                                    "BWV578 bar 51, BWV574 bar 78"};

inline constexpr int8_t kPHR_Bass_sop[] = {+1};
inline constexpr int8_t kPHR_Bass_bass[] = {-1};
inline constexpr CadenceApproach kPHR_Bass = {
    "PHR_Bass", CadenceType::Phrygian,         kPHR_Bass_sop, 1, kPHR_Bass_bass,
    1,          "BWV578 bar 44, BWV575 bar 36"};

inline constexpr int8_t kPlagal_Amen_sop[] = {-1};
inline constexpr int8_t kPlagal_Amen_bass[] = {-3};
inline constexpr CadenceApproach kPlagal_Amen = {"Plagal_Amen",
                                                 CadenceType::Plagal,
                                                 kPlagal_Amen_sop,
                                                 1,
                                                 kPlagal_Amen_bass,
                                                 1,
                                                 "BWV578 bar 70, BWV577 bar 61"};

inline constexpr CadenceApproach kCadenceApproaches[] = {
    kPAC_LeadingTone, kIAC_ThirdSoprano, kHC_StepUp, kDC_LeadingTone, kPHR_Bass, kPlagal_Amen,
};
inline constexpr size_t kCadenceApproachCount = 6;

inline CadenceInnerVoiceGuidance getInnerVoiceGuidance(CadenceType type) {
  switch (type) {
    case CadenceType::Perfect:
    case CadenceType::PicardyThird:
      return {4, true, true};
    case CadenceType::ImperfectAuthentic:
      return {4, true, true};
    case CadenceType::Half:
      return {5, true, false};
    case CadenceType::Deceptive:
      return {5, false, true};
    case CadenceType::Phrygian:
      return {4, false, true};
    case CadenceType::Plagal:
      return {3, true, false};
  }
  return {4, true, true};
}

inline std::pair<const CadenceApproach*, size_t> getCadenceApproaches(CadenceType type) {
  const CadenceApproach* first = nullptr;
  size_t count = 0;
  for (size_t idx = 0; idx < kCadenceApproachCount; ++idx) {
    if (kCadenceApproaches[idx].type == type) {
      if (first == nullptr)
        first = &kCadenceApproaches[idx];
      ++count;
    }
  }
  return {first, count};
}

}  // namespace bach

#endif  // BACH_HARMONY_CADENCE_VOCABULARY_H
