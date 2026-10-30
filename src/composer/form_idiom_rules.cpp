#include "composer/form_idiom_rules.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iterator>
#include <limits>
#include <utility>
#include <vector>

#include "composer/chord_voicing.h"
#include "composer/rule_helpers.h"
#include "core/pitch_utils.h"

namespace bach::composer {

namespace {

using rule_helpers::activeChord;
using rule_helpers::isStructuralAccent;

// Organ Toccata. Self-contained (character, archetype) compatibility
// predicate for the toccata_archetype_compatible rule. This does NOT reuse the
// legacy isCharacterFormCompatible (which keys off the legacy FormType). The
// only forbidden pair is Noble x Dramaticus: the Dramaticus archetype is the
// prototypical dramatic toccata (BWV565-style free, virtuosic opening),
// antithetical to the dignified Noble affect (the Noble character must not
// take a dramatic toccata). The switch is exhaustive and extensible: add cases
// to forbid further pairs.
bool isToccataPairCompatible(SubjectCharacter character, ToccataArchetype archetype) {
  if (character == SubjectCharacter::Noble && archetype == ToccataArchetype::Dramaticus)
    return false;
  return true;
}

bool isPerfectFifth(int semitones) {
  return std::abs(semitones) % 12 == 7;
}

struct TickInterval {
  Tick start = 0;
  Tick end = 0;

  bool operator==(const TickInterval& other) const {
    return start == other.start && end == other.end;
  }
};

struct PitchInterval {
  Tick start = 0;
  Tick end = 0;
  std::uint8_t pitch = 0;
};

struct NoteKey {
  Tick start = 0;
  Tick duration = 0;
  std::uint8_t pitch = 0;

  bool operator<(const NoteKey& other) const {
    if (start != other.start)
      return start < other.start;
    if (duration != other.duration)
      return duration < other.duration;
    return pitch < other.pitch;
  }

  bool operator==(const NoteKey& other) const {
    return start == other.start && duration == other.duration && pitch == other.pitch;
  }
};

bool checkedTickEnd(Tick start, Tick duration, Tick* end) {
  const std::uint64_t wide_end = static_cast<std::uint64_t>(start) + duration;
  if (wide_end > std::numeric_limits<Tick>::max())
    return false;
  *end = static_cast<Tick>(wide_end);
  return true;
}

bool materialEnd(const std::vector<MaterialNote>& material_notes, Tick* end) {
  *end = 0;
  for (const MaterialNote& note : material_notes) {
    Tick note_end = 0;
    if (!checkedTickEnd(note.start_tick, note.duration, &note_end))
      return false;
    *end = std::max(*end, note_end);
  }
  return true;
}

bool scoreEnd(const std::vector<NoteEvent>& notes, Tick* end) {
  *end = 0;
  for (const NoteEvent& note : notes) {
    Tick note_end = 0;
    if (!checkedTickEnd(note.start_tick, note.duration, &note_end))
      return false;
    *end = std::max(*end, note_end);
  }
  return true;
}

bool authoritativeExtent(const HarmonicPlan& harmonic_plan, const Material& material,
                         Tick ticks_per_bar, const std::vector<NoteEvent>& notes, Tick* extent) {
  Tick end = 0;
  for (const ChordEvent& chord : harmonic_plan.chords) {
    Tick chord_end = 0;
    if (!checkedTickEnd(chord.start_tick, ticks_per_bar, &chord_end))
      return false;
    end = std::max(end, chord_end);
  }
  for (const VariationDecl& variation : material.variations)
    end = std::max(end, variation.end_tick);
  for (const PassacagliaVariation& variation : material.passacaglia_variations)
    end = std::max(end, variation.end_tick);
  for (const PassacagliaVariation& variation : material.goldberg_variations)
    end = std::max(end, variation.end_tick);

  Tick declaration_end = 0;
  for (const std::vector<MaterialNote>* line :
       {&material.ground_bass, &material.passacaglia_ground, &material.goldberg_aria_bass,
        &material.cantus_firmus, &material.cf_embellished}) {
    if (!materialEnd(*line, &declaration_end))
      return false;
    end = std::max(end, declaration_end);
  }
  for (const CodaDecl& coda : material.coda_extensions) {
    if (!materialEnd(coda.notes, &declaration_end))
      return false;
    end = std::max(end, declaration_end);
  }

  const bool has_owning_blocks = !material.variations.empty() ||
                                 !material.passacaglia_variations.empty() ||
                                 !material.goldberg_variations.empty() ||
                                 !material.cantus_firmus.empty() || harmonic_plan.chords.size() > 1;
  if (!has_owning_blocks) {
    Tick emitted_end = 0;
    if (!scoreEnd(notes, &emitted_end))
      return false;
    end = std::max(end, emitted_end);
  }
  *extent = end;
  return true;
}

bool usesPeriodicReplay(Tick period, Tick cycle, Tick extent, Tick declaration_end) {
  return period > 0 && period < extent && (cycle == 0 || cycle >= period) &&
         declaration_end <= period;
}

bool appendExpectedNote(const MaterialNote& source, std::uint64_t start, Tick truncate_at,
                        bool truncate, std::vector<MaterialNote>* expanded) {
  if (start >= truncate_at)
    return true;
  if (start > std::numeric_limits<Tick>::max())
    return false;
  MaterialNote note = source;
  note.start_tick = static_cast<Tick>(start);
  if (truncate) {
    const std::uint64_t wide_end = start + note.duration;
    if (wide_end > truncate_at)
      note.duration = static_cast<Tick>(truncate_at - note.start_tick);
  }
  Tick note_end = 0;
  if (note.duration == 0 || !checkedTickEnd(note.start_tick, note.duration, &note_end))
    return false;
  expanded->push_back(note);
  return true;
}

bool expandGroundDeclaration(const std::vector<MaterialNote>& declaration, Tick period, Tick cycle,
                             Tick extent, Tick truncate_at, bool truncate,
                             std::size_t available_replay_notes,
                             std::vector<MaterialNote>* expanded) {
  Tick declaration_end = 0;
  if (!materialEnd(declaration, &declaration_end))
    return false;
  const bool tiled = usesPeriodicReplay(period, cycle, extent, declaration_end);
  const Tick expected_limit = truncate ? std::min(extent, truncate_at) : extent;
  std::uint64_t required_count = 0;
  for (const MaterialNote& source : declaration) {
    if (source.duration == 0 || (tiled && source.start_tick >= period) ||
        source.start_tick >= expected_limit) {
      continue;
    }
    const std::uint64_t remaining = static_cast<std::uint64_t>(expected_limit) - source.start_tick;
    const std::uint64_t copies =
        tiled ? (remaining + period - 1) / period : static_cast<std::uint64_t>(1);
    if (copies > std::numeric_limits<std::uint64_t>::max() - required_count)
      return false;
    required_count += copies;
    if (required_count > available_replay_notes ||
        required_count > std::numeric_limits<std::size_t>::max()) {
      return false;
    }
  }

  if (required_count == 0)
    return false;

  expanded->clear();
  expanded->reserve(static_cast<std::size_t>(required_count));
  if (tiled) {
    for (std::uint64_t base = 0; base < expected_limit; base += period) {
      for (const MaterialNote& source : declaration) {
        if (source.start_tick >= period)
          continue;
        const std::uint64_t start = base + source.start_tick;
        if (start >= expected_limit)
          continue;
        if (!appendExpectedNote(source, start, truncate_at, truncate, expanded))
          return false;
      }
    }
  } else {
    for (const MaterialNote& source : declaration) {
      if (source.start_tick >= expected_limit)
        continue;
      if (!appendExpectedNote(source, source.start_tick, truncate_at, truncate, expanded))
        return false;
    }
  }
  return true;
}

std::size_t replayCount(const std::vector<NoteEvent>& notes,
                        const std::vector<NoteProvenance>& provenance, RuleBit bit) {
  std::size_t count = 0;
  for (std::size_t index = 0; index < notes.size() && index < provenance.size(); ++index)
    count += hasRuleBit(provenance, index, bit) ? 1 : 0;
  return count;
}

bool normalizeSingleCoverage(std::vector<TickInterval>* intervals) {
  std::sort(intervals->begin(), intervals->end(),
            [](const TickInterval& left, const TickInterval& right) {
              if (left.start != right.start)
                return left.start < right.start;
              return left.end < right.end;
            });
  std::vector<TickInterval> merged;
  merged.reserve(intervals->size());
  for (const TickInterval& interval : *intervals) {
    if (interval.end <= interval.start)
      return false;
    if (!merged.empty() && interval.start < merged.back().end)
      return false;
    if (!merged.empty() && interval.start == merged.back().end) {
      merged.back().end = interval.end;
    } else {
      merged.push_back(interval);
    }
  }
  *intervals = std::move(merged);
  return true;
}

bool hasExactSingleCoverage(const std::vector<MaterialNote>& expected,
                            const std::vector<TickInterval>& replayed_intervals) {
  std::vector<TickInterval> expected_intervals;
  expected_intervals.reserve(expected.size());
  for (const MaterialNote& note : expected) {
    Tick note_end = 0;
    if (!checkedTickEnd(note.start_tick, note.duration, &note_end))
      return false;
    expected_intervals.push_back({note.start_tick, note_end});
  }
  std::vector<TickInterval> actual_intervals = replayed_intervals;
  if (!normalizeSingleCoverage(&expected_intervals) ||
      !normalizeSingleCoverage(&actual_intervals)) {
    return false;
  }
  return expected_intervals == actual_intervals;
}

std::vector<NoteKey> materialKeys(const std::vector<MaterialNote>& notes, Tick before_tick) {
  std::vector<NoteKey> keys;
  keys.reserve(notes.size());
  for (const MaterialNote& note : notes) {
    if (note.start_tick < before_tick)
      keys.push_back({note.start_tick, note.duration, note.pitch});
  }
  std::sort(keys.begin(), keys.end());
  return keys;
}

std::vector<NoteKey> replayKeys(const std::vector<NoteEvent>& notes,
                                const std::vector<NoteProvenance>& provenance, RuleBit bit) {
  std::vector<NoteKey> keys;
  keys.reserve(std::min(notes.size(), provenance.size()));
  for (std::size_t index = 0; index < notes.size() && index < provenance.size(); ++index) {
    if (hasRuleBit(provenance, index, bit))
      keys.push_back({notes[index].start_tick, notes[index].duration, notes[index].pitch});
  }
  std::sort(keys.begin(), keys.end());
  return keys;
}

bool findProtectedVoice(const std::vector<NoteEvent>& notes,
                        const std::vector<NoteProvenance>& provenance, RuleBit bit,
                        VoiceId* protected_voice, bool* has_replay) {
  *has_replay = false;
  bool one_voice = true;
  for (std::size_t index = 0; index < notes.size() && index < provenance.size(); ++index) {
    if (!hasRuleBit(provenance, index, bit))
      continue;
    if (!*has_replay) {
      *protected_voice = notes[index].voice;
      *has_replay = true;
    } else if (*protected_voice != notes[index].voice) {
      one_voice = false;
    }
  }
  return one_voice;
}

struct TerminalCoda {
  bool declared = false;
  bool matches = false;
  Tick start = 0;
  std::vector<MaterialNote> notes;
};

TerminalCoda findTerminalCoda(const std::vector<CodaDecl>& declarations,
                              const std::vector<NoteEvent>& notes,
                              const std::vector<NoteProvenance>& provenance,
                              VoiceId protected_voice, Tick extent, Tick cycle) {
  TerminalCoda result;
  if (extent == 0 || cycle == 0)
    return result;
  const Tick last_cycle_start = extent > cycle ? extent - cycle : 0;
  Tick coda_end = 0;
  result.start = extent;
  for (const CodaDecl& declaration : declarations) {
    if (declaration.voice != protected_voice || declaration.notes.empty())
      continue;
    bool terminal = true;
    Tick declaration_start = extent;
    Tick declaration_end = 0;
    for (const MaterialNote& note : declaration.notes) {
      Tick note_end = 0;
      if (note.duration == 0 || !checkedTickEnd(note.start_tick, note.duration, &note_end) ||
          note.start_tick < last_cycle_start || note_end > extent) {
        terminal = false;
        continue;
      }
      declaration_start = std::min(declaration_start, note.start_tick);
      declaration_end = std::max(declaration_end, note_end);
    }
    if (terminal && declaration_end == extent) {
      result.notes.insert(result.notes.end(), declaration.notes.begin(), declaration.notes.end());
      result.start = std::min(result.start, declaration_start);
      coda_end = std::max(coda_end, declaration_end);
    }
  }
  if (result.notes.empty() || coda_end != extent) {
    result.start = extent;
    result.notes.clear();
    return result;
  }
  result.declared = true;

  std::vector<NoteKey> expected;
  expected.reserve(result.notes.size());
  for (const MaterialNote& note : result.notes)
    expected.push_back({note.start_tick, note.duration, note.pitch});
  std::sort(expected.begin(), expected.end());

  std::vector<NoteKey> committed;
  bool coda_carrier_only = true;
  for (std::size_t index = 0; index < notes.size() && index < provenance.size(); ++index) {
    if (notes[index].voice == protected_voice && notes[index].start_tick >= last_cycle_start &&
        hasRuleBit(provenance, index, RuleBit::CodaCommitted)) {
      coda_carrier_only &= provenance[index].voice_intent == VoiceIntent::CodaCarrier;
      committed.push_back({notes[index].start_tick, notes[index].duration, notes[index].pitch});
    }
  }
  std::sort(committed.begin(), committed.end());
  result.matches = coda_carrier_only && expected == committed;
  return result;
}

std::vector<TickInterval> replayIntervals(const std::vector<NoteEvent>& notes,
                                          const std::vector<NoteProvenance>& provenance,
                                          RuleBit bit, bool include_terminal_coda,
                                          VoiceId protected_voice, bool* valid) {
  std::vector<TickInterval> intervals;
  intervals.reserve(std::min(notes.size(), provenance.size()));
  *valid = true;
  for (std::size_t index = 0; index < notes.size() && index < provenance.size(); ++index) {
    if (hasRuleBit(provenance, index, bit) ||
        (include_terminal_coda && notes[index].voice == protected_voice &&
         hasRuleBit(provenance, index, RuleBit::CodaCommitted))) {
      Tick note_end = 0;
      if (!checkedTickEnd(notes[index].start_tick, notes[index].duration, &note_end)) {
        *valid = false;
        continue;
      }
      intervals.push_back({notes[index].start_tick, note_end});
    }
  }
  return intervals;
}

bool pitchIntervalsMatchAtBarHeads(std::vector<PitchInterval>* expected,
                                   std::vector<PitchInterval>* actual, Tick ticks_per_bar) {
  if (ticks_per_bar == 0 || expected->empty() || actual->empty())
    return false;

  const auto sortIntervals = [](std::vector<PitchInterval>* intervals) {
    std::sort(intervals->begin(), intervals->end(),
              [](const PitchInterval& lhs, const PitchInterval& rhs) {
                if (lhs.start != rhs.start)
                  return lhs.start < rhs.start;
                return lhs.end < rhs.end;
              });
  };
  sortIntervals(expected);
  sortIntervals(actual);

  std::vector<TickInterval> expected_coverage;
  expected_coverage.reserve(expected->size());
  for (const PitchInterval& note : *expected) {
    if (note.end <= note.start)
      return false;
    expected_coverage.push_back({note.start, note.end});
  }
  std::vector<TickInterval> actual_coverage;
  actual_coverage.reserve(actual->size());
  for (const PitchInterval& note : *actual) {
    if (note.end <= note.start)
      return false;
    actual_coverage.push_back({note.start, note.end});
  }
  if (!normalizeSingleCoverage(&expected_coverage) || !normalizeSingleCoverage(&actual_coverage) ||
      expected_coverage != actual_coverage) {
    return false;
  }

  // Coverage permits subdivision and ties, but forbids both overlap and gaps.
  // The two sorted timelines therefore identify one sounding note per tick.
  // Compare one bar head in each constant-pitch intersection rather than
  // walking every bar of a potentially very long declared line.
  std::size_t expected_index = 0;
  std::size_t actual_index = 0;
  while (expected_index < expected->size() && actual_index < actual->size()) {
    const PitchInterval& expected_note = (*expected)[expected_index];
    const PitchInterval& actual_note = (*actual)[actual_index];
    const Tick overlap_start = std::max(expected_note.start, actual_note.start);
    const Tick overlap_end = std::min(expected_note.end, actual_note.end);
    if (overlap_start < overlap_end) {
      const std::uint64_t first_head =
          ((static_cast<std::uint64_t>(overlap_start) + ticks_per_bar - 1) / ticks_per_bar) *
          ticks_per_bar;
      if (first_head < overlap_end && expected_note.pitch != actual_note.pitch)
        return false;
    }
    if (expected_note.end <= actual_note.end)
      ++expected_index;
    if (actual_note.end <= expected_note.end)
      ++actual_index;
  }
  return true;
}

bool protectedBarHeadsMatch(const std::vector<MaterialNote>& expanded,
                            const std::vector<NoteEvent>& notes,
                            const std::vector<NoteProvenance>& provenance, RuleBit bit,
                            Tick ticks_per_bar) {
  std::vector<PitchInterval> expected;
  expected.reserve(expanded.size());
  for (const MaterialNote& note : expanded) {
    Tick end = 0;
    if (!checkedTickEnd(note.start_tick, note.duration, &end))
      return false;
    expected.push_back({note.start_tick, end, note.pitch});
  }
  std::vector<PitchInterval> actual;
  actual.reserve(std::min(notes.size(), provenance.size()));
  for (std::size_t index = 0; index < notes.size() && index < provenance.size(); ++index) {
    if (!hasRuleBit(provenance, index, bit))
      continue;
    Tick end = 0;
    if (!checkedTickEnd(notes[index].start_tick, notes[index].duration, &end))
      return false;
    actual.push_back({notes[index].start_tick, end, notes[index].pitch});
  }
  return pitchIntervalsMatchAtBarHeads(&expected, &actual, ticks_per_bar);
}

}  // namespace

std::array<std::uint8_t, 3> triadFor(const ChordEvent& chord) {
  std::uint8_t third = 4;
  std::uint8_t fifth = 7;
  switch (chord.quality) {
    case ChordQuality::Major:
    case ChordQuality::Major7:
    case ChordQuality::Dominant7:
      third = 4;
      fifth = 7;
      break;
    case ChordQuality::Minor:
    case ChordQuality::Minor7:
      third = 3;
      fifth = 7;
      break;
    case ChordQuality::Diminished:
    case ChordQuality::HalfDiminished7:
    case ChordQuality::Diminished7:
      third = 3;
      fifth = 6;
      break;
    case ChordQuality::Augmented:
      third = 4;
      fifth = 8;
      break;
  }
  return {
      static_cast<std::uint8_t>(chord.root_pc % 12),
      static_cast<std::uint8_t>((chord.root_pc + third) % 12),
      static_cast<std::uint8_t>((chord.root_pc + fifth) % 12),
  };
}

bool hasRuleBit(const std::vector<NoteProvenance>& provenance, std::size_t index, RuleBit bit) {
  if (index >= provenance.size())
    return false;
  return (provenance[index].satisfied_rules & (ruleBitMask(bit))) != 0;
}

void checkFormIdiomRules(const FormIdiomContext& context, ValidationReport* out_report) {
  const std::vector<NoteEvent>& notes = context.notes;
  const std::vector<NoteProvenance>& provenance = context.provenance;
  const HarmonicPlan& harmonic_plan = context.harmonic_plan;
  const Material& material = context.material;
  const VoiceOnsetIndex& onset_index = context.onset_index;
  const Tick ticks_per_bar = context.ticks_per_bar;
  ValidationReport& report = *out_report;

  // Solo String Flow (BWV1007): implicit-voice rules over the
  // ArpeggioFlow line. The figure is regular, so collecting every
  // ArpeggioFlowActive note in onset order and partitioning into contiguous
  // cells of arpeggio_template.group_size reconstructs the implicit voices a
  // listener tracks. The two principal implicit voices are register-defined:
  // the bass stream is the lowest pitch of each cell, the top stream the
  // highest. (Register, not slot position, is what the ear segregates —
  // BWV1007's oscillating figures put the perceived top in the middle of the
  // written cell.)
  {
    const int group_size = material.arpeggio_template.group_size;
    std::vector<std::size_t> flow_indices;
    for (std::size_t i = 0; i < notes.size(); ++i) {
      if (i < provenance.size() && hasRuleBit(provenance, i, RuleBit::ArpeggioFlowActive))
        flow_indices.push_back(i);
    }
    std::sort(flow_indices.begin(), flow_indices.end(), [&](std::size_t a, std::size_t b) {
      return notes[a].start_tick < notes[b].start_tick;
    });

    const std::size_t g = (group_size >= 2) ? static_cast<std::size_t>(group_size) : 0;
    const std::size_t cell_count = (g >= 2) ? flow_indices.size() / g : 0;

    if (cell_count >= 2) {
      // Per-cell bass (min pitch) and top (max pitch) streams.
      std::vector<std::uint8_t> bass_stream;
      std::vector<std::uint8_t> top_stream;
      bass_stream.reserve(cell_count);
      top_stream.reserve(cell_count);
      for (std::size_t cell = 0; cell < cell_count; ++cell) {
        std::uint8_t lo = 255;
        std::uint8_t hi = 0;
        for (std::size_t k = 0; k < g; ++k) {
          const std::uint8_t p = notes[flow_indices[cell * g + k]].pitch;
          lo = std::min(lo, p);
          hi = std::max(hi, p);
        }
        bass_stream.push_back(lo);
        top_stream.push_back(hi);
      }

      // implicit_voice_counterpoint: the bass and top implicit streams must
      // each be melodically valid — no forbidden augmented / tritone /
      // diminished leap between consecutive cells. Reuses the shared
      // rule_helpers predicate so the implicit lines of the solo arpeggio are
      // held to the same melodic standard as the Organ Compose voices.
      bool implicit_ok = true;
      for (const std::vector<std::uint8_t>* stream : {&bass_stream, &top_stream}) {
        for (std::size_t cell = 1; cell < cell_count; ++cell) {
          const Tick from_tick = notes[flow_indices[(cell - 1) * g]].start_tick;
          const Tick to_tick = notes[flow_indices[cell * g]].start_tick;
          if (rule_helpers::isForbiddenMelodicLeap((*stream)[cell - 1], (*stream)[cell],
                                                   harmonic_plan, from_tick, to_tick)) {
            implicit_ok = false;
            break;
          }
        }
        if (!implicit_ok)
          break;
      }
      if (!implicit_ok) {
        ValidationFailure failure;
        failure.rule_id = "implicit_voice_counterpoint";
        failure.kind = FailKind::MusicalFail;
        report.failures.push_back(failure);
      }

      // arpeggio_no_parallel_perfect: the bass and top implicit streams must
      // not move in parallel perfect 5ths/8ves across consecutive cells — the
      // broken-chord equivalent of consecutive parallel perfects between two
      // real voices. Flagged only when both cells frame the same perfect
      // interval class AND both streams move in the same (nonzero) direction;
      // oblique / contrary / static motion is permitted.
      bool parallel_ok = true;
      for (std::size_t cell = 1; cell < cell_count && parallel_ok; ++cell) {
        parallel_ok = !isParallelPerfectMotion(top_stream[cell - 1], top_stream[cell],
                                               bass_stream[cell - 1], bass_stream[cell]);
      }
      if (!parallel_ok) {
        ValidationFailure failure;
        failure.rule_id = "arpeggio_no_parallel_perfect";
        failure.kind = FailKind::MusicalFail;
        report.failures.push_back(failure);
      }
    }
  }

  // Ground replay must cover the declared line once and preserve each bar head.
  // The line may be subdivided or decorated away from the bar head, but those
  // notes cannot create a gap or overlap in its protected time.
  const auto checkImmutableGround = [&](const std::vector<MaterialNote>& ground, Tick period,
                                        Tick cycle, RuleBit replay_bit, const char* rule_id) {
    if (ground.empty())
      return;
    Tick line_extent = 0;
    if (!authoritativeExtent(harmonic_plan, material, ticks_per_bar, notes, &line_extent)) {
      ValidationFailure failure;
      failure.rule_id = rule_id;
      failure.kind = FailKind::StructuralFail;
      report.failures.push_back(failure);
      return;
    }

    VoiceId protected_voice = 0;
    bool has_replay = false;
    const bool one_voice =
        findProtectedVoice(notes, provenance, replay_bit, &protected_voice, &has_replay);
    TerminalCoda coda;
    if (one_voice && has_replay)
      coda = findTerminalCoda(material.coda_extensions, notes, provenance, protected_voice,
                              line_extent, cycle > 0 ? cycle : period);
    const bool use_coda = coda.declared && coda.matches;
    std::vector<MaterialNote> expected;
    const bool expected_valid = expandGroundDeclaration(
        ground, period, cycle, line_extent, use_coda ? coda.start : line_extent, use_coda,
        replayCount(notes, provenance, replay_bit), &expected);

    bool ground_ok = one_voice && expected_valid;
    if (coda.declared && !coda.matches)
      ground_ok = false;

    std::vector<TickInterval> expected_intervals;
    expected_intervals.reserve(expected.size() + (use_coda ? coda.notes.size() : 0));
    for (const MaterialNote& note : expected) {
      Tick note_end = 0;
      if (!checkedTickEnd(note.start_tick, note.duration, &note_end))
        ground_ok = false;
      else
        expected_intervals.push_back({note.start_tick, note_end});
    }
    if (use_coda) {
      for (const MaterialNote& note : coda.notes)
        expected_intervals.push_back({note.start_tick, note.start_tick + note.duration});
    }
    bool replayed_intervals_valid = false;
    std::vector<TickInterval> actual_intervals =
        replayIntervals(notes, provenance, replay_bit, use_coda && one_voice && has_replay,
                        protected_voice, &replayed_intervals_valid);
    std::vector<TickInterval> normalized_expected = expected_intervals;
    const bool coverage_ok = expected_valid && replayed_intervals_valid &&
                             normalizeSingleCoverage(&normalized_expected) &&
                             normalizeSingleCoverage(&actual_intervals) &&
                             normalized_expected == actual_intervals;
    const bool heads_ok = expected_valid && protectedBarHeadsMatch(expected, notes, provenance,
                                                                   replay_bit, ticks_per_bar);
    Tick protected_end = 0;
    if (!materialEnd(expected, &protected_end))
      ground_ok = false;
    if (use_coda)
      protected_end = line_extent;
    if (protected_end < line_extent && !use_coda)
      ground_ok = false;
    if (!coverage_ok || !heads_ok || !has_replay || !ground_ok) {
      ValidationFailure failure;
      failure.rule_id = rule_id;
      failure.kind = FailKind::StructuralFail;
      report.failures.push_back(failure);
    }
  };

  checkImmutableGround(material.ground_bass, material.ground_bass_period,
                       material.ground_bass_cycle, RuleBit::GroundBassReplayed,
                       "ground_bass_immutable");
  checkImmutableGround(material.passacaglia_ground, material.passacaglia_ground_period,
                       material.passacaglia_ground_cycle, RuleBit::PassacagliaGroundReplayed,
                       "passacaglia_ground_immutable");

  // Goldberg retains the aria's exact surface in every block. Compare sorted
  // multisets so missing events, repeated events and differences in the
  // Dotted/Pulsed/Gathering surfaces all remain visible.
  if (!material.goldberg_aria_bass.empty()) {
    Tick line_extent = 0;
    bool bass_ok = authoritativeExtent(harmonic_plan, material, ticks_per_bar, notes, &line_extent);
    VoiceId protected_voice = 0;
    bool has_replay = false;
    const bool one_voice = findProtectedVoice(notes, provenance, RuleBit::GoldbergBassReplayed,
                                              &protected_voice, &has_replay);
    TerminalCoda coda;
    if (one_voice && has_replay)
      coda = findTerminalCoda(
          material.coda_extensions, notes, provenance, protected_voice, line_extent,
          material.goldberg_aria_bass_cycle > 0 ? material.goldberg_aria_bass_cycle
                                                : material.goldberg_aria_bass_period);
    const bool use_coda = coda.declared && coda.matches;
    Tick bass_declaration_end = 0;
    const bool bass_declaration_valid =
        materialEnd(material.goldberg_aria_bass, &bass_declaration_end);
    std::vector<MaterialNote> expected_notes;
    const bool expected_valid =
        bass_declaration_valid &&
        expandGroundDeclaration(material.goldberg_aria_bass, material.goldberg_aria_bass_period,
                                material.goldberg_aria_bass_cycle, line_extent,
                                use_coda ? coda.start : line_extent, use_coda,
                                replayCount(notes, provenance, RuleBit::GoldbergBassReplayed),
                                &expected_notes);
    bass_ok = bass_ok && expected_valid;
    const std::vector<NoteKey> expected = materialKeys(expected_notes, line_extent);
    const std::vector<NoteKey> replayed =
        replayKeys(notes, provenance, RuleBit::GoldbergBassReplayed);
    const bool heads_ok =
        expected_valid && protectedBarHeadsMatch(expected_notes, notes, provenance,
                                                 RuleBit::GoldbergBassReplayed, ticks_per_bar);
    const bool periodic =
        usesPeriodicReplay(material.goldberg_aria_bass_period, material.goldberg_aria_bass_cycle,
                           line_extent, bass_declaration_end);
    const bool missing_terminal = !periodic && bass_declaration_end < line_extent && !use_coda;
    if (!bass_ok || !one_voice || !has_replay || expected != replayed || !heads_ok ||
        (coda.declared && !coda.matches) || missing_terminal) {
      ValidationFailure failure;
      failure.rule_id = "goldberg_aria_bass_immutable";
      failure.kind = FailKind::StructuralFail;
      report.failures.push_back(failure);
    }
  }

  // variation_role_ornament_constraint: a Ground-role variation states the
  // ground bass plainly and must stay un-ornamented — no note may subdivide
  // below the beat. Any note shorter than a quarter (kTicksPerBeat) inside a
  // Ground-role VariationDecl is an illegal ornamental subdivision. Read from
  // the material decls directly (mirroring the other Material-decl rules); one
  // flagged note is enough to fail the span. Inert when material.variations is
  // empty (fixtures that declare no variations).
  for (const VariationDecl& var : material.variations) {
    if (var.role != VariationRole::Ground)
      continue;
    bool ornamented = false;
    for (const MaterialNote& note : var.notes) {
      if (note.duration < kTicksPerBeat) {
        ornamented = true;
        break;
      }
    }
    if (ornamented) {
      ValidationFailure failure;
      failure.rule_id = "variation_role_ornament_constraint";
      failure.kind = FailKind::MusicalFail;
      report.failures.push_back(failure);
      break;
    }
  }

  // figuration_harmonic_consistency: a free-form organ-prelude figuration line
  // (FigurationCarrier) outlines the underlying harmony. The figuration is
  // anchored to the harmony at each bar downbeat: the note that opens a bar must
  // be a chord tone of that bar's chord. Within the bar the line is free to run
  // scalewise (passing / neighbour tones are idiomatic), so only the bar
  // downbeat is constrained. Pedal-point notes (PedalPreparation) are exempt: a
  // pedal is by definition a single sustained pitch held against changing
  // harmony. FigurationAnchorRelaxed notes are likewise exempt: the figuration
  // is the only voice this rule binds, while the theme entries sounding against
  // it walk freely through non-chord tones, and the two constraints can close on
  // each other until no chord tone in the voice band is playable at all. The
  // builder stamps that bit only after scanning the whole band, so the exemption
  // is carried by proof rather than granted by default.
  // For each note stamped FigurationCommitted (and not PedalPreparation)
  // whose onset lands on a bar downbeat (start_tick % ticks_per_bar == 0), resolve
  // the active ChordEvent (latest plan.chords entry with start_tick <= the note's
  // onset) and reuse the same chord-tone arithmetic the doubling/spacing rules use: triadFor
  // for the root/third/fifth and hasSeventh/seventhOffset for the seventh. If any
  // bar-downbeat figuration note's pitch class is not a chord tone, push ONE
  // MusicalFail. The rule is inert when no FigurationCommitted note exists
  // (fixtures that declare no figuration sections).
  {
    bool figuration_inconsistent = false;
    for (std::size_t i = 0; i < notes.size() && !figuration_inconsistent; ++i) {
      if (!hasRuleBit(provenance, i, RuleBit::FigurationCommitted))
        continue;
      if (hasRuleBit(provenance, i, RuleBit::PedalPreparation))
        continue;  // pedal points are held against changing harmony.
      if (hasRuleBit(provenance, i, RuleBit::FigurationAnchorRelaxed))
        continue;  // no chord tone was reachable; the builder proved exhaustion.
      const auto& note = notes[i];
      const Tick authored_tick =
          provenance[i].has_authored_note ? provenance[i].authored_start_tick : note.start_tick;
      const std::uint8_t authored_pitch =
          provenance[i].has_authored_note ? provenance[i].authored_pitch : note.pitch;
      if (authored_tick % ticks_per_bar != 0)
        continue;  // only the bar downbeat is harmonically anchored.
      const auto& chord = activeChord(harmonic_plan, authored_tick);
      const auto triad = triadFor(chord);
      const std::uint8_t pc = static_cast<std::uint8_t>(authored_pitch % 12);
      bool is_chord_tone = (pc == triad[0] || pc == triad[1] || pc == triad[2]);
      if (!is_chord_tone && hasSeventh(chord.quality)) {
        const std::uint8_t seventh_pc =
            static_cast<std::uint8_t>((chord.root_pc + seventhOffset(chord.quality)) % 12);
        is_chord_tone = (pc == seventh_pc);
      }
      if (!is_chord_tone)
        figuration_inconsistent = true;
    }
    if (figuration_inconsistent) {
      ValidationFailure failure;
      failure.rule_id = "figuration_harmonic_consistency";
      failure.kind = FailKind::MusicalFail;
      report.failures.push_back(failure);
    }
  }

  // toccata_archetype_compatible: an organ toccata's piece-level design pairs a
  // SubjectCharacter affect with one of the four ToccataArchetypes. Some pairs
  // are musically antithetical (see isToccataPairCompatible: Noble x Dramaticus
  // is forbidden). For each ToccataSection in material.toccata_sections, the
  // (character, archetype) pair must be compatible. If any section is an
  // incompatible pair, push ONE MusicalFail. The rule is inert when
  // material.toccata_sections is empty (fixtures that declare none).
  {
    bool toccata_incompatible = false;
    for (const auto& section : material.toccata_sections) {
      if (!isToccataPairCompatible(section.character, section.archetype)) {
        toccata_incompatible = true;
        break;
      }
    }
    if (toccata_incompatible) {
      ValidationFailure failure;
      failure.rule_id = "toccata_archetype_compatible";
      failure.kind = FailKind::MusicalFail;
      report.failures.push_back(failure);
    }
  }

  // The cantus may change pitch between downbeats, but its declared timeline
  // must be covered once without holes or overlaps. Each structural bar head
  // still has to appear once with the skeleton pitch.
  if (!material.cantus_firmus.empty()) {
    const std::vector<MaterialNote>& cantus =
        material.cf_is_embellished ? material.cf_embellished : material.cantus_firmus;
    bool replayed_intervals_valid = false;
    const std::vector<TickInterval> replayed_intervals = replayIntervals(
        notes, provenance, RuleBit::CantusFirmusReplayed, false, 0, &replayed_intervals_valid);
    std::vector<TickInterval> cantus_intervals;
    cantus_intervals.reserve(cantus.size());
    bool cantus_intervals_valid = true;
    for (const MaterialNote& note : cantus) {
      Tick note_end = 0;
      if (!checkedTickEnd(note.start_tick, note.duration, &note_end)) {
        cantus_intervals_valid = false;
        break;
      }
      cantus_intervals.push_back({note.start_tick, note_end});
    }
    const bool replay_covers_skeleton =
        hasExactSingleCoverage(material.cantus_firmus, replayed_intervals);
    const bool bar_heads_match = protectedBarHeadsMatch(
        material.cantus_firmus, notes, provenance, RuleBit::CantusFirmusReplayed, ticks_per_bar);
    Tick extent = 0;
    Tick skeleton_end = 0;
    const bool extent_valid =
        authoritativeExtent(harmonic_plan, material, ticks_per_bar, notes, &extent);
    const bool skeleton_valid = materialEnd(material.cantus_firmus, &skeleton_end);
    const bool reaches_form_end =
        extent_valid && skeleton_valid && (extent == 0 || skeleton_end >= extent);
    const bool same_declared_coverage =
        cantus_intervals_valid && hasExactSingleCoverage(material.cantus_firmus, cantus_intervals);
    if (!replayed_intervals_valid || !same_declared_coverage || !replay_covers_skeleton ||
        !bar_heads_match || !reaches_form_end) {
      ValidationFailure failure;
      failure.rule_id = "cantus_firmus_immutable";
      failure.kind = FailKind::StructuralFail;
      report.failures.push_back(failure);
    }
  }

  // Organ Trio Sonata: voice_independence_threshold. A trio sonata's
  // defining technique is THREE independent voices (RH = Great, LH = Swell,
  // Pedal). This rule operates ONLY on notes carrying the TrioVoiceIndependent
  // bit; it groups them by voice and measures the pairwise voice independence of
  // the (up to three) trio voices, soft-failing (MusicalFail, per the
  // "voice independence >= 0.6" soft-penalty convention) below 0.6.
  //
  // Self-contained independence metric (does NOT call into src/analysis/):
  // Build the sorted set of all distinct onset ticks across both voices of a
  // pair. Walk adjacent onset boundaries; at each boundary t (after the first),
  // each voice is in one of three motion states relative to the previous
  // boundary: it has a NEW onset at t (it moved/re-articulated) or it does NOT
  // (it is sustaining / silent). A boundary is counted as INDEPENDENT when the
  // two voices differ in motion direction or rhythm:
  //   (a) rhythmic independence — exactly one voice has a new onset at t (the
  //       other sustains): oblique motion / differing rhythm; OR
  //   (b) both voices have a new onset at t but their pitch motions (sign of the
  //       interval from each voice's previous sounding pitch) are NOT the same
  //       non-zero direction — i.e. contrary, oblique (one static), or one moves
  //       while the other repeats. Only genuine PARALLEL/SIMILAR motion (both
  //       move the same non-zero direction) counts as dependent.
  // The pair's independence score = independent_boundaries / total_boundaries;
  // the rule's score is the mean across all voice pairs. Inert when fewer than
  // two trio voices are present. Deterministic and pure.
  {
    std::vector<VoiceId> trio_voice_ids;
    for (std::size_t i = 0; i < notes.size(); ++i) {
      if (!hasRuleBit(provenance, i, RuleBit::TrioVoiceIndependent))
        continue;
      if (std::find(trio_voice_ids.begin(), trio_voice_ids.end(), notes[i].voice) ==
          trio_voice_ids.end()) {
        trio_voice_ids.push_back(notes[i].voice);
      }
    }
    if (trio_voice_ids.size() >= 2) {
      std::sort(trio_voice_ids.begin(), trio_voice_ids.end());
      // Per-voice trio notes ordered by onset, note-list order on ties.
      std::vector<std::vector<std::size_t>> trio_notes(trio_voice_ids.size());
      for (std::size_t slot = 0; slot < trio_voice_ids.size(); ++slot) {
        for (std::size_t i = 0; i < notes.size(); ++i) {
          if (notes[i].voice == trio_voice_ids[slot] &&
              hasRuleBit(provenance, i, RuleBit::TrioVoiceIndependent))
            trio_notes[slot].push_back(i);
        }
        std::stable_sort(trio_notes[slot].begin(), trio_notes[slot].end(),
                         [&](std::size_t lhs, std::size_t rhs) {
                           return notes[lhs].start_tick < notes[rhs].start_tick;
                         });
      }
      const auto firstOnsetAtOrAfter = [&](const std::vector<std::size_t>& line, Tick at) {
        return std::lower_bound(line.begin(), line.end(), at, [&](std::size_t index, Tick value) {
          return notes[index].start_tick < value;
        });
      };
      // Writes the pitch of the latest trio onset <= `at` into `out_pitch`, or
      // returns false when the voice has no trio note there. Among notes sharing
      // that onset the first in note-list order wins.
      auto onsetPitch = [&](std::size_t slot, Tick at, int* out_pitch) -> bool {
        const auto& line = trio_notes[slot];
        const auto after = std::upper_bound(
            line.begin(), line.end(), at,
            [&](Tick value, std::size_t index) { return value < notes[index].start_tick; });
        if (after == line.begin())
          return false;
        const auto latest = firstOnsetAtOrAfter(line, notes[*std::prev(after)].start_tick);
        if (out_pitch != nullptr)
          *out_pitch = static_cast<int>(notes[*latest].pitch);
        return true;
      };
      auto hasOnsetAt = [&](std::size_t slot, Tick at) -> bool {
        const auto& line = trio_notes[slot];
        const auto it = firstOnsetAtOrAfter(line, at);
        return it != line.end() && notes[*it].start_tick == at;
      };

      double independence_sum = 0.0;
      int pair_count = 0;
      for (std::size_t a = 0; a < trio_voice_ids.size(); ++a) {
        for (std::size_t b = a + 1; b < trio_voice_ids.size(); ++b) {
          // Distinct onset ticks across both voices, sorted ascending.
          std::vector<Tick> boundaries;
          boundaries.reserve(trio_notes[a].size() + trio_notes[b].size());
          for (const std::size_t slot : {a, b}) {
            for (const std::size_t index : trio_notes[slot])
              boundaries.push_back(notes[index].start_tick);
          }
          std::sort(boundaries.begin(), boundaries.end());
          boundaries.erase(std::unique(boundaries.begin(), boundaries.end()), boundaries.end());
          if (boundaries.size() < 2) {
            // Not enough motion to assess; treat as fully independent (no drag).
            independence_sum += 1.0;
            ++pair_count;
            continue;
          }
          int independent = 0;
          int total = 0;
          for (std::size_t k = 1; k < boundaries.size(); ++k) {
            const Tick t = boundaries[k];
            const Tick prev = boundaries[k - 1];
            const bool a_onset = hasOnsetAt(a, t);
            const bool b_onset = hasOnsetAt(b, t);
            ++total;
            if (a_onset != b_onset) {
              // Exactly one voice re-articulated: rhythmic independence.
              ++independent;
              continue;
            }
            // Both re-articulated: compare pitch-motion directions.
            int a_now = 0, a_prev = 0, b_now = 0, b_prev = 0;
            const bool a_known = onsetPitch(a, t, &a_now) && onsetPitch(a, prev, &a_prev);
            const bool b_known = onsetPitch(b, t, &b_now) && onsetPitch(b, prev, &b_prev);
            if (!a_known || !b_known) {
              // No prior sounding pitch for one voice: cannot establish genuine
              // same-direction motion, so this boundary is not dependent.
              ++independent;
              continue;
            }
            const int a_dir = (a_now > a_prev) ? 1 : (a_now < a_prev ? -1 : 0);
            const int b_dir = (b_now > b_prev) ? 1 : (b_now < b_prev ? -1 : 0);
            // Dependent only when both move the SAME non-zero direction
            // (parallel / similar motion). Otherwise independent.
            if (!(a_dir != 0 && a_dir == b_dir)) {
              ++independent;
            }
          }
          const double score =
              (total > 0) ? static_cast<double>(independent) / static_cast<double>(total) : 1.0;
          independence_sum += score;
          ++pair_count;
        }
      }
      const double mean_independence =
          (pair_count > 0) ? independence_sum / static_cast<double>(pair_count) : 1.0;
      if (mean_independence < 0.6) {
        ValidationFailure failure;
        failure.rule_id = "voice_independence_threshold";
        failure.kind = FailKind::MusicalFail;
        report.failures.push_back(failure);
      }
    }
  }

  // Organ Fantasia: section_contrast_required. A fantasia's defining
  // technique is CONTRASTING sections (free / fugal / toccata-like / chordal).
  // This rule operates ONLY on notes carrying the FantasiaSectionContrast bit.
  // It walks adjacent FantasiaSection windows (material.fantasia_sections, in
  // declaration order) and, for each section, measures two self-contained
  // traits from the emitted FantasiaSectionContrast notes whose onset falls in
  // [start_tick, end_tick):
  //   - density: notes per bar = note_count * ticks_per_bar / window_ticks,
  //              rounded down (the realized notes-per-bar tier).
  //   - mean register: integer mean MIDI pitch of the section's notes.
  // CRITERION: each ADJACENT pair of sections must differ in EITHER density
  // (|density_a - density_b| >= kMinDensityMargin = 2 notes/bar) OR mean
  // register (|register_a - register_b| >= kMinRegisterMargin = 5 semitones,
  // a perfect fourth). A pair that is near-identical in BOTH (density within 1
  // AND register within 4) is NOT contrasting and pushes ONE MusicalFail (SOFT).
  // Inert when fewer than 2 sections carry the bit. Deterministic and pure;
  // no src/analysis/ calls. Heeds the sentinel lesson: running maxima are not
  // seeded with -1 on unsigned types (note counts / ticks accumulate from 0).
  {
    constexpr int kMinDensityMargin = 2;   // notes/bar difference for contrast.
    constexpr int kMinRegisterMargin = 5;  // semitone difference for contrast.
    struct SectionStat {
      int density = 0;        // notes per bar (realized).
      int mean_register = 0;  // mean MIDI pitch.
      int note_count = 0;
    };
    std::vector<SectionStat> stats;
    stats.reserve(material.fantasia_sections.size());
    for (const auto& section : material.fantasia_sections) {
      int note_count = 0;
      long pitch_sum = 0;
      for (std::size_t i = 0; i < notes.size(); ++i) {
        if (!hasRuleBit(provenance, i, RuleBit::FantasiaSectionContrast))
          continue;
        // Section contrast is a property of the declared Fantasia carriers.
        // Ornament expansion inherits the carrier bit for provenance
        // traceability, but it must not inflate a sparse section's structural
        // density or blur its mean register after FinalScore decoration.
        if (i < provenance.size() && provenance[i].source == NoteSource::Ornament)
          continue;
        if (notes[i].start_tick < section.start_tick || notes[i].start_tick >= section.end_tick)
          continue;
        ++note_count;
        pitch_sum += static_cast<long>(notes[i].pitch);
      }
      if (note_count == 0)
        continue;  // no realized notes for this section window.
      const Tick window_ticks =
          (section.end_tick > section.start_tick) ? (section.end_tick - section.start_tick) : 0;
      SectionStat stat;
      stat.note_count = note_count;
      stat.density = (window_ticks > 0)
                         ? static_cast<int>(static_cast<long>(note_count) * ticks_per_bar /
                                            static_cast<long>(window_ticks))
                         : 0;
      stat.mean_register = static_cast<int>(pitch_sum / note_count);
      stats.push_back(stat);
    }
    if (stats.size() >= 2) {
      bool uncontrasting_pair = false;
      for (std::size_t i = 1; i < stats.size(); ++i) {
        const int density_diff = std::abs(stats[i].density - stats[i - 1].density);
        const int register_diff = std::abs(stats[i].mean_register - stats[i - 1].mean_register);
        const bool contrasts =
            (density_diff >= kMinDensityMargin) || (register_diff >= kMinRegisterMargin);
        if (!contrasts) {
          uncontrasting_pair = true;
          break;
        }
      }
      if (uncontrasting_pair) {
        ValidationFailure failure;
        failure.rule_id = "section_contrast_required";
        failure.kind = FailKind::MusicalFail;
        report.failures.push_back(failure);
      }
    }
  }

  // Fugue countersubject: countersubject_invertible. A countersubject written in
  // invertible (double) counterpoint at the octave must work both above and
  // below the subject. Under octave inversion a perfect fifth
  // becomes a perfect fourth (a dissonance against the bass), so a perfect fifth
  // on a strong beat between the countersubject and the subject/answer it sounds
  // against is not strictly invertible at the octave. This rule operates ONLY on
  // notes carrying the CountersubjectInvertible bit (the first high-lane RuleBit).
  // It identifies the countersubject voice(s) from those notes and the
  // subject/answer voice(s) from notes whose provenance voice_intent is
  // SubjectCarrier or AnswerCarrier, then walks every strong-beat tick where a
  // countersubject voice and a subject/answer voice both sound. If the vertical
  // interval reduces to a perfect fifth (|interval| % 12 == 7) at any such tick,
  // the pair is not invertible at the octave.
  //
  // This is reported INFORMATIONALLY (report.informational), not as a gating
  // failure: free-style fugue countersubjects in the existing corpus routinely
  // place consonant fifths against the subject on strong beats (strict
  // invertibility is a design constraint of double-counterpoint countersubjects,
  // not of every countersubject), so a hard or soft FAILURE here would penalize
  // established pieces. The observation is recorded for provenance/audit; it
  // never sets status or empties-failures. It is pushed directly rather than
  // through the counterpoint-finding recorder, so it carries no span, no rule
  // geometry and no observation tally. Inert when no CountersubjectInvertible
  // note or no overlapping subject/answer voice exists (most fixtures declare no
  // countersubject). Deterministic and pure; reads pitches through the shared
  // onset index the invertible_at_octave rule uses.
  {
    std::vector<VoiceId> cs_voices;
    std::vector<VoiceId> sa_voices;
    Tick cs_first = std::numeric_limits<Tick>::max();
    Tick cs_last = 0;
    bool has_cs = false;
    for (std::size_t i = 0; i < notes.size(); ++i) {
      if (i >= provenance.size())
        continue;
      if (hasRuleBit(provenance, i, RuleBit::CountersubjectInvertible)) {
        if (std::find(cs_voices.begin(), cs_voices.end(), notes[i].voice) == cs_voices.end())
          cs_voices.push_back(notes[i].voice);
        cs_first = std::min(cs_first, notes[i].start_tick);
        cs_last = std::max(cs_last, notes[i].start_tick + notes[i].duration);
        has_cs = true;
      } else if (provenance[i].voice_intent == VoiceIntent::SubjectCarrier ||
                 provenance[i].voice_intent == VoiceIntent::AnswerCarrier) {
        if (std::find(sa_voices.begin(), sa_voices.end(), notes[i].voice) == sa_voices.end())
          sa_voices.push_back(notes[i].voice);
      }
    }
    if (has_cs && !cs_voices.empty() && !sa_voices.empty()) {
      // Distinct strong-beat onset ticks inside the countersubject's span; the
      // pair only needs checking where the countersubject actually sounds.
      std::vector<Tick> ticks;
      for (std::size_t i = 0; i < notes.size(); ++i) {
        const Tick t = notes[i].start_tick;
        if (t < cs_first || t >= cs_last)
          continue;
        if (!isStructuralAccent(harmonic_plan, t))
          continue;
        ticks.push_back(t);
      }
      std::sort(ticks.begin(), ticks.end());
      ticks.erase(std::unique(ticks.begin(), ticks.end()), ticks.end());

      bool non_invertible = false;
      for (Tick t : ticks) {
        if (non_invertible)
          break;
        for (VoiceId cs : cs_voices) {
          const std::uint8_t cs_pitch = onset_index.pitchAt(cs, t);
          if (cs_pitch == 0)
            continue;
          for (VoiceId sa : sa_voices) {
            if (sa == cs)
              continue;
            const std::uint8_t sa_pitch = onset_index.pitchAt(sa, t);
            if (sa_pitch == 0)
              continue;
            const int interval = static_cast<int>(cs_pitch) - static_cast<int>(sa_pitch);
            // A perfect fifth (and its compound forms) inverts to a fourth.
            if (isPerfectFifth(interval)) {
              non_invertible = true;
              break;
            }
          }
          if (non_invertible)
            break;
        }
      }
      if (non_invertible) {
        ValidationFailure observation;
        observation.rule_id = "countersubject_invertible";
        observation.kind = FailKind::MusicalFail;
        report.informational.push_back(observation);
      }
    }
  }
}

}  // namespace bach::composer
