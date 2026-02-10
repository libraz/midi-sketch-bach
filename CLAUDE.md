# midi-sketch-bach - Bach Instrumental MIDI Generator

C++17 MIDI generator for J.S. Bach instrumental works. Structure-faithful reproduction of organ fugues and solo string pieces.

---

## 1. Prohibited Actions (CRITICAL)

**These actions are strictly forbidden. Violation causes data loss or incorrect fixes.**

| Action | Why | Instead |
|--------|-----|---------|
| `git stash` | **ABSOLUTELY FORBIDDEN** - loses context | `git show HEAD:file`, `git diff` |
| `git reset --hard` | Destroys local changes | `git diff` to review |
| `git checkout <file>` | Overwrites WIP | Comment out code |
| `rm <file>` | Deletion doesn't fix bugs | Fix the code |
| Hardcode for specific input | Breaks other inputs | Find general solution |
| Code change without understanding | Creates new bugs | Analyze output first |
| Run full batch test just to find failures | Large test suite, enormous waste | Save results to file, then grep |
| Merge two generation systems into one | **Organ and Solo String are separate by design** | Keep them independent |
| Modify GroundBass notes | **Immutable by design principle** | Fix the generation logic instead |
| Change VoiceRole/VariationRole at runtime | **Roles are const, set at construction** | Use ImmutableContext pattern |

---

## 2. Design Principles (Always consult these)

Full details: `backup/project_plan_2026-02-10.md` Section 0

### Principle 1: Role is Immutable
- Roles assigned to Section/Variation/Voice **cannot be changed**
- Use `ImmutableContext<RoleT>` with const member, no setter

### Principle 2: Semantic Destruction = Immediate FAIL
- Structural order violation, ground bass modification, Role/Phase mismatch -> **instant FAIL**
- Log with `FailKind { STRUCTURAL_FAIL, MUSICAL_FAIL, CONFIG_FAIL }` + location + metric

### Principle 3: Reduce Generation
- Fewer choices, not more parameters. Fixed values are fine.

### Principle 4: Trust Design Values
- Climax/Peak/Stretto-peak: **do not search**. Output design values directly.
- Generate only the journey; compose the destination.

---

## 3. Workflow

### 3.1 Before ANY Code Change

```
1. Run:     ./build/bin/bach_cli --analyze
2. Check:   output.json for counterpoint violations
3. Listen:  output.mid in DAW
4. Locate:  Use provenance.source to find generation phase
5. Fix:     Modify the identified component
```

### 3.2 Completion Checklist

| Check | Verify |
|-------|--------|
| Counterpoint rules correct | No parallel 5ths/8ths, voice crossing resolved |
| Structure preserved | FuguePhase/ArcPhase/VariationRole order intact |
| Generalizes | Works for all seeds, keys, forms |
| No regression | `make test` passes |
| Root cause fixed | Understand WHY it was broken |
| Test added | New case in `tests/` |
| Roles unchanged | ImmutableContext not violated |

### 3.3 Fix Priority

| Priority | Component | Location | When |
|----------|-----------|----------|------|
| 1 | Counterpoint rules | `src/counterpoint/*.cpp` | Parallel 5ths/8ths, voice crossing |
| 2 | Fugue structure | `src/fugue/*.cpp` | Wrong exposition/episode/stretto |
| 3 | Subject quality | `src/fugue/subject*.cpp` | Poor subject, answer errors |
| 4 | Voice assignment | `src/organ/manual.cpp` | Wrong manual/pedal allocation |
| 5 | Arpeggio patterns | `src/solo_string/flow/*.cpp` | Monotone, stagnation |
| 6 | Texture generation | `src/solo_string/arch/*.cpp` | Texture collapse |
| 7 | MIDI writing | `src/midi/midi_writer.cpp` | Output format |

---

## 4. Architecture

### 4.1 Two Generation Systems (DO NOT MERGE)

```
midi-sketch-bach
├── Organ System         Counterpoint-driven, multi-voice, fugue structure
│   └── CounterpointEngine + FugueGenerator
│
└── Solo String System   Harmony-driven, single line, texture management
    ├── Flow (BWV1007)   HarmonicArpeggioEngine
    └── Arch (BWV1004)   ChaconneEngine
```

### 4.2 MIDI Channel Mapping

#### Organ System

| Voice | MIDI Ch | GM Program | Range |
|-------|---------|-----------|-------|
| Manual I (Great) | 0 | Church Organ (19) | C2-C6 (36-96) |
| Manual II (Swell) | 1 | Reed Organ (20) | C2-C6 (36-96) |
| Manual III (Positiv) | 2 | Church Organ (19) | C3-C6 (48-96) |
| Pedal | 3 | Church Organ (19) | C1-D3 (24-50) soft penalty |
| Metadata | 15 | - | - |

#### Solo String System

| Voice | MIDI Ch | GM Program | Range |
|-------|---------|-----------|-------|
| Cello | 0 | Cello (42) | C2-A5 (36-81) |
| Violin | 0 | Violin (40) | G3-C7 (55-96) |
| Guitar | 0 | Nylon Guitar (24) | E2-B5 (40-83) |
| Metadata | 15 | - | - |

### 4.3 Key Directories

```
src/core/          - basic_types, pitch_utils, note_source, note_creator, rng_util
src/organ/         - organ_config, manual, registration, pedal_constraints
src/counterpoint/  - counterpoint_state, i_rule_evaluator, fux/bach_rules, collision_resolver
src/fugue/         - subject, answer, countersubject, exposition, episode, stretto, generator
src/instrument/    - IPhysicalPerformer, keyboard/, fretted/, bowed/
src/harmony/       - harmonic_timeline, harmonic_event, chord_types, key
src/forms/         - prelude, chorale_prelude, trio_sonata, toccata, passacaglia
src/solo_string/
  flow/            - arpeggio_pattern, harmonic_arpeggio_engine, flow_analyzer
  arch/            - ground_bass, texture_generator, chaconne_engine, chaconne_analyzer
src/ornament/      - trill, mordent, turn, ornament_engine
src/transform/     - motif_transform, sequence, inversion, augmentation
src/midi/          - midi_writer, midi_reader
src/analysis/      - counterpoint_analyzer, fugue_analyzer, voice_independence, fail_report
tests/             - mirrors src/ structure
scripts/           - bach_analyzer/ (Python analysis)
```

### 4.4 Core Enums

```cpp
// Organ: Global time axis
enum class FuguePhase { Establish, Develop, Resolve };
// Order: Establish -> Develop -> Resolve (no reversal)

// Organ: Voice structural roles
enum class VoiceRole { Assert, Respond, Propel, Ground };
// Immutable. Never swap mid-generation.

// Organ: Subject personality (affects entire fugue)
enum class SubjectCharacter { Severe, Playful, Noble, Restless };
// Phase restrictions: Severe/Playful (Ph1-2), Noble (Ph3+), Restless (Ph4+)

// Solo String Flow: Global arc
enum class ArcPhase { Ascent, Peak, Descent };
// Peak = exactly 1 section. Config-fixed (seed-independent).

// Solo String Arch: Variation structural roles
enum class VariationRole { Establish, Develop, Destabilize, Illuminate, Accumulate, Resolve };
// Fixed order. Accumulate = exactly 3. Resolve = Theme only.

// Failure classification
enum class FailKind { STRUCTURAL_FAIL, MUSICAL_FAIL, CONFIG_FAIL };
```

### 4.5 Design Decisions (Do Not Change)

| Decision | Reason |
|----------|--------|
| C major internally | Transpose at output |
| 480 ticks/beat | Standard MIDI resolution, midi-sketch compatible |
| Organ velocity = 80 fixed | Pipe organs have no velocity sensitivity |
| Pedal range: soft penalty | Not hard rejection; gradual penalty outside C1-D3 |
| Two separate generation systems | Organ (counterpoint) vs Solo String (harmony) are fundamentally different |
| Roles are const | `ImmutableContext<RoleT>` prevents runtime mutation |
| Climax = design values | Peak/Stretto-top/ClimaxDesign output directly, no search |
| GlobalArc/FuguePhase = config-fixed | Seed-independent. "Meaning axis" never changes on regeneration |

---

## 5. Pitch Safety

### Creating Notes (ALWAYS use createBachNote)

```cpp
#include "core/note_creator.h"

BachNoteOptions opts;
opts.voice = voice_id;
opts.desired_pitch = pitch;
opts.tick = tick;
opts.duration = duration;
opts.source = BachNoteSource::FugueSubject;
opts.entry_number = 1;

auto result = createBachNote(state, rules, resolver, opts);
if (result.accepted) {
  // Note placed successfully with counterpoint rules satisfied
}
```

### Note Provenance (BachNoteSource)

| source | Generator | Fix Location |
|--------|-----------|-------------|
| `fugue_subject` | Subject entry | `src/fugue/subject.cpp` |
| `fugue_answer` | Answer (Real/Tonal) | `src/fugue/answer.cpp` |
| `countersubject` | Countersubject | `src/fugue/countersubject.cpp` |
| `episode_material` | Episode motif | `src/fugue/episode.cpp` |
| `free_counterpoint` | Free counterpoint | `src/counterpoint/` |
| `cantus_fixed` | Cantus firmus (immutable) | `src/counterpoint/cantus_firmus.cpp` |
| `ornament` | Ornament (trill, mordent) | `src/ornament/` |
| `pedal_point` | Pedal point | `src/fugue/` |
| `arpeggio_flow` | Flow arpeggio | `src/solo_string/flow/` |
| `texture_note` | Arch texture | `src/solo_string/arch/texture_generator.cpp` |
| `ground_bass` | Ground bass (immutable) | `src/solo_string/arch/ground_bass.cpp` |
| `collision_avoid` | Collision avoidance | `src/counterpoint/collision_resolver.cpp` |
| `post_process` | Post-processing | varies |

---

## 6. Debugging

### 6.1 Note Provenance in output.json

```json
{
  "pitch": 62, "velocity": 80,
  "start_ticks": 1920, "duration_ticks": 480,
  "voice": "alto",
  "provenance": {
    "source": "fugue_answer",
    "original_pitch": 64,
    "chord_degree": 3,
    "lookup_tick": 1920,
    "transform_steps": ["tonal_answer", "collision_avoid"],
    "entry_number": 2
  }
}
```

### 6.2 Counterpoint Snapshot

```bash
./build/bin/bach_cli --analyze --input fugue.mid --bar 5
# -> Dumps all voice states at bar 5
```

### 6.3 FailReport

```bash
./build/bin/bach_cli --validate --input fugue.mid --json
# -> FailReport JSON with kind/severity/tick/voice/rule
```

### 6.4 Fail Fast Conditions (Instant FAIL, no retry)

| Condition | System | Check |
|-----------|--------|-------|
| `globalArcScore != 1.0` | Flow | ArcPhase order or Peak not unique |
| `dramaturgicOrderScore != 1.0` | Flow | PatternRole order violated |
| `groundBassIntegrity != 1.0` | Arch | Ground bass modified |
| `roleOrderScore != 1.0` | Arch | VariationRole order violated |
| `climaxPresenceScore != 1.0` | Arch | Accumulate != 3 or design conditions unmet |
| `impliedPolyphonyScore` out of range | Arch | Implied voices not 2.3-2.8 |
| `expositionCompletenessScore != 1.0` | Organ | Incomplete exposition |
| `isCharacterFormCompatible = false` | Organ | Forbidden Character x Form combo |

---

## 7. CLI Reference

### 7.1 Commands

```bash
make build                                    # Build
make test                                     # Run tests
make quality-gate                             # Run quality checks
./build/bin/bach_cli                          # Generate (default: prelude_and_fugue)
./build/bin/bach_cli --analyze --input X.mid  # Analyze external MIDI
./build/bin/bach_cli --validate --input X.mid # Validate (PASS/FAIL)
./build/bin/bach_cli --regenerate --input X.mid # Regenerate from embedded config
```

### 7.2 Options

| Option | Description |
|--------|-------------|
| `--seed N` | Random seed (0 = auto) |
| `--key KEY` | Key (e.g. `g_minor`, `C_major`) |
| `--form FORM` | Form type (see below) |
| `--voices N` | Number of voices (2-5) |
| `--bpm N` | BPM (40-200) |
| `--character CH` | Subject character: `severe`, `playful`, `noble`, `restless` |
| `--instrument INST` | `organ`, `harpsichord`, `piano`, `violin`, `cello`, `guitar` |
| `--json` | JSON output (output.json) |
| `--analyze` | Generate + analysis |
| `--strict` | No retry (debug mode) |
| `--verbose-retry` | Log retry process |
| `--max-retry N` | Global retry limit (default: 3) |
| `-o FILE` | Output file path |

### 7.3 Form Types

```bash
# Organ System
--form prelude_and_fugue    # Default
--form fugue                # Fugue only
--form trio_sonata          # BWV 525-530 style
--form chorale_prelude      # BWV 599-650 style
--form toccata_and_fugue    # BWV 565 style
--form passacaglia          # BWV 582 style
--form fantasia_and_fugue   # BWV 537/542 style

# Solo String - Flow
--form cello_prelude        # BWV 1007-1 style

# Solo String - Arch
--form chaconne             # BWV 1004-5 style
```

### 7.4 Instrument Auto-Detection

| Form | Default Instrument |
|------|-------------------|
| fugue, prelude_and_fugue, trio_sonata, chorale_prelude | organ |
| cello_prelude, cello_suite | cello |
| chaconne, violin_partita | violin |

---

## 8. Code Standards

| Area | Standard |
|------|----------|
| Language | C++17 |
| Style | Google Style, clang-format |
| Indent | 2 spaces |
| Column limit | 100 |
| Errors | Return codes, no exceptions |
| Dependencies | None (future WASM support) |
| Comments | English only |
| Documentation | Doxygen-style (`@brief`, `@param`, `@return`) for public APIs |
| Copyright | No copyright headers in source files |

### Naming

| Type | Convention | Example |
|------|------------|---------|
| Class | PascalCase | `FugueGenerator` |
| Function | camelCase | `generateSubject` |
| Variable | snake_case | `num_voices` |
| Constant | kPascalCase | `kTicksPerBeat` |
| Enum | PascalCase | `FormType::Fugue` |

### Git Commit Rules

| Type | Prefix |
|------|--------|
| Feature | `feat:` |
| Bug fix | `fix:` |
| Test | `test:` |
| Refactor | `refactor:` |
| Docs | `docs:` |

**Commit content restrictions (CRITICAL):**
- **NO development phase references** - Never include "Phase 1", "Phase 1.5", "Phase 2" etc.
- **NO internal process details** - No WIP, sprint numbers, milestone names
- **NO AI agent references** - No mention of agents or automated tools
- Commit messages must describe **what** and **why** in technical terms only
- Delegate commit writing to `commit-writer` agent for proper formatting

---

## 9. Reference

### Time

```cpp
kTicksPerBeat = 480;
kTicksPerBar = 1920;  // 4/4
seconds = ticks / kTicksPerBeat / bpm * 60.0;
```

### Counterpoint Intervals

| Interval | Semitones | Classification |
|----------|-----------|---------------|
| Perfect unison | 0 | Perfect consonance |
| Minor 3rd | 3 | Imperfect consonance |
| Major 3rd | 4 | Imperfect consonance |
| Perfect 4th | 5 | Context-dependent |
| Perfect 5th | 7 | Perfect consonance |
| Minor 6th | 8 | Imperfect consonance |
| Major 6th | 9 | Imperfect consonance |
| Perfect octave | 12 | Perfect consonance |
| Minor 2nd | 1 | Dissonant |
| Major 2nd | 2 | Dissonant |
| Tritone | 6 | Dissonant |
| Minor 7th | 10 | Dissonant |
| Major 7th | 11 | Dissonant |

### Forbidden Parallels

| Violation | Description | Severity |
|-----------|-------------|----------|
| Parallel 5ths | Two voices move in 5ths in same direction | Critical |
| Parallel 8ths | Two voices move in octaves in same direction | Critical |
| Hidden 5ths/8ths | Same direction motion arriving at 5th/8th | Warning |
| Voice crossing | Upper voice goes below lower voice | Error |
| Augmented leap | Augmented 4th/5th jump | Error |

### Sub-Agent Routing

Delegate to specialized agents in `.claude/agents/`. Each agent has project-specific knowledge including code standards (English-only, Doxygen, no copyright) and Bach domain rules.

| Task | Agent | Routing Trigger |
|------|-------|-----------------|
| Implement features, fix bugs | `code-writer` | Code changes to src/ |
| Write/fix tests | `testing-specialist` | Code changes to tests/ |
| Design review, interface design | `architecture-advisor` | New components, module design |
| Hot path optimization | `performance-optimizer` | Performance issues |
| Create commits | `commit-writer` | `git commit` / `/commit` |
| Error handling design | `error-handler` | Error codes, FailKind usage |
| Debug infrastructure | `observability-specialist` | Debug output, provenance |
| Thread safety | `concurrency-expert` | Concurrent access patterns |
| Resource management | `resource-manager` | RAII, smart pointers, lifecycle |
| Codebase audit | `system-reviewer` | Full quality review |

**Agent responsibilities include:**
- Enforcing code standards (English, Doxygen, no copyright, C++17)
- Bach domain rules (counterpoint, immutability, two-system separation)
- Commit message restrictions (no phase references, no AI mentions)

---

## 10. Implementation Plan

Detailed implementation plan: `docs/plan/`

| File | Phase | Content |
|------|-------|---------|
| `00_overview.md` | Overview | Agent teams, conventions, file layout |
| `01_phase0_foundation.md` | Phase 0 | Build system, core migration, instruments |
| `02_phase1_counterpoint.md` | Phase 1 | Counterpoint engine (3-class design) |
| `03_phase1_5_subject.md` | Phase 1.5 | Subject generation, 7-dim validation |
| `04_phase2_fugue.md` | Phase 2 | 3-voice fugue structure |
| `05_phase3_4_prelude_trio.md` | Phase 3-4 | Prelude + 4-5 voice + trio sonata |
| `06_phase5_6_ornament_quality.md` | Phase 5-6 | Ornaments + quality gate |
| `07_phase7_flow.md` | Phase 7 | Solo String Flow (BWV1007) |
| `08_phase8_arch.md` | Phase 8 | Solo String Arch (BWV1004) |

### Batch Test Rules (CRITICAL)

| Bad | Why | Good |
|-----|-----|------|
| `make test \| grep FAIL` | Runs all then pipes = waste | `make test > /tmp/test_result.txt 2>&1` then grep |
| Re-run just to check results | Same results exist | Reuse saved result file |
| Full test on every fix | Slow | Run related tests: `--gtest_filter="*TestName*"` |
