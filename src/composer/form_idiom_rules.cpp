#include "composer/form_idiom_rules.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <limits>
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

  // Immutable-ground skeleton rules. One shared check for both ground
  // families: an immutable ground (Chaconne ground bass / Passacaglia
  // ground) is the harmonic skeleton of its form and must be replayed
  // unchanged on every cycle. The check is a bar-head skeleton match (the
  // same granularity as cantus_firmus_immutable): a replayed ground may
  // subdivide a bar rhythmically (repeated same-pitch notes, the late-cycle
  // quarter-note intensification), but each ground note on a bar head must
  // equal the canonical material tone sounding at the same cycle-relative
  // position. Any altered, transposed, or reordered restatement changes some
  // bar head and fails. Off-downbeat ground notes are unconstrained (the
  // builders keep their pitch equal to the bar tone by construction). The
  // rule is inert when either the canonical ground or the stamped run is
  // empty (fixtures that declare no ground).
  const auto checkImmutableGround = [&](const std::vector<MaterialNote>& ground,
                                        Tick declared_period, RuleBit replay_bit,
                                        const char* rule_id) {
    bool has_ground_note = false;
    bool ground_ok = true;
    const std::size_t n = ground.size();
    if (n > 0) {
      const Tick period =
          declared_period > 0 ? declared_period : static_cast<Tick>(n) * ticks_per_bar;
      for (std::size_t i = 0; i < notes.size() && ground_ok; ++i) {
        if (i >= provenance.size() || !hasRuleBit(provenance, i, replay_bit))
          continue;
        has_ground_note = true;
        const auto& note = notes[i];
        if (note.start_tick % ticks_per_bar != 0)
          continue;  // only the bar head carries the skeleton tone.
        // Canonical tone: the material note sounding at this cycle-relative
        // position (the latest material onset at or before it).
        const Tick cycle_tick = note.start_tick % period;
        int expected = -1;
        for (const MaterialNote& mnote : ground) {
          if (mnote.start_tick <= cycle_tick)
            expected = mnote.pitch;
        }
        if (expected >= 0 && note.pitch != expected)
          ground_ok = false;
      }
    }
    if (has_ground_note && !ground_ok) {
      ValidationFailure failure;
      failure.rule_id = rule_id;
      failure.kind = FailKind::StructuralFail;
      report.failures.push_back(failure);
    }
  };
  // Solo String Arch (BWV1004 Chaconne).
  checkImmutableGround(material.ground_bass, material.ground_bass_period,
                       RuleBit::GroundBassReplayed, "ground_bass_immutable");
  // Organ Passacaglia.
  checkImmutableGround(material.passacaglia_ground, material.passacaglia_ground_period,
                       RuleBit::PassacagliaGroundReplayed, "passacaglia_ground_immutable");
  // Goldberg's compressed aria-bass phrase is a dedicated 32-tone declaration,
  // not a passacaglia bar-head ground. Every declared onset/duration/pitch must
  // recur exactly in every four-bar variation block. A terminal CodaCarrier
  // may replace the final bar with a tonic cadence; that explicit extension is
  // outside the immutable aria-bass declaration.
  if (!material.goldberg_aria_bass.empty() && material.goldberg_aria_bass_period > 0) {
    Tick score_end = 0;
    for (const auto& note : notes)
      score_end = std::max(score_end, note.start_tick + note.duration);
    Tick immutable_end = score_end;
    const Tick terminal_window = score_end > material.goldberg_aria_bass_period
                                     ? score_end - material.goldberg_aria_bass_period
                                     : 0;
    for (std::size_t i = 0; i < notes.size(); ++i) {
      if (notes[i].start_tick >= terminal_window &&
          hasRuleBit(provenance, i, RuleBit::CodaCommitted)) {
        immutable_end = std::min(immutable_end, notes[i].start_tick);
      }
    }
    bool any = false;
    bool ok = true;
    for (Tick base = 0; base < immutable_end && ok; base += material.goldberg_aria_bass_period) {
      for (const auto& expected : material.goldberg_aria_bass) {
        const Tick tick = base + expected.start_tick;
        if (tick >= immutable_end)
          break;
        bool found = false;
        for (std::size_t i = 0; i < notes.size(); ++i) {
          if (!hasRuleBit(provenance, i, RuleBit::GoldbergBassReplayed))
            continue;
          any = true;
          if (notes[i].start_tick == tick && notes[i].duration == expected.duration &&
              notes[i].pitch == expected.pitch) {
            found = true;
            break;
          }
        }
        if (!found) {
          ok = false;
          break;
        }
      }
    }
    if (!any || !ok) {
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
  // onset) and reuse the same chord-tone arithmetic the P7 rules use: triadFor
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

  // cantus_firmus_immutable: the cantus firmus skeleton (the fixed chorale tune,
  // material.cantus_firmus) is the structural backbone of the chorale prelude and
  // is immutable. A CantusFirmusCarrier may replay an embellished
  // line, but each bar's DOWNBEAT tone must still equal the skeleton tone for
  // that bar. Gather every note stamped CantusFirmusReplayed in onset order; for
  // each whose onset lands on a bar downbeat (start_tick % ticks_per_bar == 0),
  // look up the expected skeleton tone material.cantus_firmus[bar_index] where
  // bar_index = start_tick / ticks_per_bar (bounds-guarded). If the replayed
  // downbeat pitch differs from the skeleton pitch, push ONE StructuralFail.
  // Off-downbeat embellishment notes are unconstrained. The rule is inert when
  // material.cantus_firmus is empty or no CantusFirmusReplayed note exists
  // (fixtures that declare no cantus firmus).
  {
    std::vector<std::size_t> cf_indices;
    for (std::size_t i = 0; i < notes.size(); ++i) {
      if (i < provenance.size() && hasRuleBit(provenance, i, RuleBit::CantusFirmusReplayed))
        cf_indices.push_back(i);
    }
    std::sort(cf_indices.begin(), cf_indices.end(), [&](std::size_t a, std::size_t b) {
      return notes[a].start_tick < notes[b].start_tick;
    });

    if (!material.cantus_firmus.empty() && !cf_indices.empty()) {
      bool cf_altered = false;
      for (std::size_t idx : cf_indices) {
        const auto& note = notes[idx];
        if (note.start_tick % ticks_per_bar != 0)
          continue;  // only bar downbeats are constrained.
        const std::size_t bar_index = static_cast<std::size_t>(note.start_tick / ticks_per_bar);
        if (bar_index >= material.cantus_firmus.size())
          continue;  // out of skeleton range; unconstrained.
        if (note.pitch != material.cantus_firmus[bar_index].pitch) {
          cf_altered = true;
          break;
        }
      }
      if (cf_altered) {
        ValidationFailure failure;
        failure.rule_id = "cantus_firmus_immutable";
        failure.kind = FailKind::StructuralFail;
        report.failures.push_back(failure);
      }
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
      // Per-voice onset->pitch maps, restricted to TrioVoiceIndependent notes.
      // Returns true and writes the sounding pitch (latest trio onset <= `at`)
      // into `out_pitch`, or false when `voice` has no trio note sounding at
      // `at`. A `bool found` flag is used instead of a sentinel because `Tick`
      // is unsigned: a `-1` seed would wrap to 0xFFFFFFFF and never be exceeded
      // by a real start_tick, defeating the "latest onset" comparison.
      auto onsetPitch = [&](VoiceId voice, Tick at, int* out_pitch) -> bool {
        bool found = false;
        Tick best = 0;
        int pitch = -1;
        for (std::size_t i = 0; i < notes.size(); ++i) {
          if (notes[i].voice != voice)
            continue;
          if (!hasRuleBit(provenance, i, RuleBit::TrioVoiceIndependent))
            continue;
          if (notes[i].start_tick <= at && (!found || notes[i].start_tick > best)) {
            found = true;
            best = notes[i].start_tick;
            pitch = static_cast<int>(notes[i].pitch);
          }
        }
        if (found && out_pitch != nullptr)
          *out_pitch = pitch;
        return found;
      };
      auto hasOnsetAt = [&](VoiceId voice, Tick at) -> bool {
        for (std::size_t i = 0; i < notes.size(); ++i) {
          if (notes[i].voice != voice)
            continue;
          if (!hasRuleBit(provenance, i, RuleBit::TrioVoiceIndependent))
            continue;
          if (notes[i].start_tick == at)
            return true;
        }
        return false;
      };

      double independence_sum = 0.0;
      int pair_count = 0;
      for (std::size_t a = 0; a < trio_voice_ids.size(); ++a) {
        for (std::size_t b = a + 1; b < trio_voice_ids.size(); ++b) {
          const VoiceId va = trio_voice_ids[a];
          const VoiceId vb = trio_voice_ids[b];
          // Distinct onset ticks across both voices, sorted ascending.
          std::vector<Tick> boundaries;
          for (std::size_t i = 0; i < notes.size(); ++i) {
            if (!hasRuleBit(provenance, i, RuleBit::TrioVoiceIndependent))
              continue;
            if (notes[i].voice != va && notes[i].voice != vb)
              continue;
            if (std::find(boundaries.begin(), boundaries.end(), notes[i].start_tick) ==
                boundaries.end()) {
              boundaries.push_back(notes[i].start_tick);
            }
          }
          std::sort(boundaries.begin(), boundaries.end());
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
            const bool a_onset = hasOnsetAt(va, t);
            const bool b_onset = hasOnsetAt(vb, t);
            ++total;
            if (a_onset != b_onset) {
              // Exactly one voice re-articulated: rhythmic independence.
              ++independent;
              continue;
            }
            // Both re-articulated: compare pitch-motion directions.
            int a_now = 0, a_prev = 0, b_now = 0, b_prev = 0;
            const bool a_known = onsetPitch(va, t, &a_now) && onsetPitch(va, prev, &a_prev);
            const bool b_known = onsetPitch(vb, t, &b_now) && onsetPitch(vb, prev, &b_prev);
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

  // Fugue countersubject: countersubject_invertible. A fugue's countersubject
  // is conceived in invertible (double) counterpoint at the octave: it must work
  // both above and below the subject. Under octave inversion a perfect fifth
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
  // never sets status or empties-failures. Inert when no CountersubjectInvertible
  // note or no overlapping subject/answer voice exists (most fixtures declare no
  // countersubject). Deterministic and pure; reuses the same isStrongBeat /
  // voicePitchAt helpers the invertible_at_octave rule uses.
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
        if (std::find(ticks.begin(), ticks.end(), t) == ticks.end())
          ticks.push_back(t);
      }
      std::sort(ticks.begin(), ticks.end());

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
