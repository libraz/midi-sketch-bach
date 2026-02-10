# midi-sketch-bach

> **Alpha** — This project is under active development. Features and APIs may change. Demo and pre-built binaries are coming soon.

A MIDI generator dedicated to the instrumental works of Johann Sebastian Bach.

Bach's instrumental music — organ fugues, solo string suites, chamber sonatas — is built on an extraordinary architecture of counterpoint, harmonic logic, and formal structure. This project focuses exclusively on these instrumental works, and attempts to reproduce their structural principles — strict voice-leading, the harmonic flow of the cello suites, the monumental arch of the Chaconne — as playable MIDI, as closely as possible.

Built on the development insights from [midi-sketch](https://github.com/libraz/midi-sketch), a pop/contemporary music generator. Available as a **CLI tool**, **JavaScript/WASM library**, and **interactive web demo**.

## What It Generates

**Organ Works** — counterpoint-driven, multi-voice:

| Form | After the manner of | Voices |
|------|---------------------|--------|
| Prelude and Fugue | BWV 532, 548 | 2-5 |
| Fugue | Strict 3-voice fugue | 2-5 |
| Trio Sonata | BWV 525-530 | 3 |
| Chorale Prelude | BWV 599-650 (Orgelbüchlein) | 3-4 |
| Toccata and Fugue | BWV 565 | 3-4 |
| Passacaglia | BWV 582 | 3-4 |
| Fantasia and Fugue | BWV 537, 542 | 3-4 |

**Solo String Works** — harmony-driven, single line:

| Form | After the manner of | Instrument |
|------|---------------------|------------|
| Cello Prelude | BWV 1007 (Suite No.1) | Cello |
| Chaconne | BWV 1004 (Partita No.2) | Violin |

Every fugue enforces proper exposition, real/tonal answer, countersubject, episodes, and stretto. Parallel fifths and octaves are forbidden. Voice crossing is resolved. The Chaconne follows the three-part arch with ground bass integrity throughout.

## Quick Start

### CLI

```bash
make build
./build/bin/bach_cli                                        # Prelude and Fugue in C major
./build/bin/bach_cli --form fugue --key g_minor --seed 42
./build/bin/bach_cli --form chaconne --scale full
./build/bin/bach_cli --form cello_prelude --bpm 120 -o prelude.mid
```

### JavaScript / WASM

```typescript
import { init, BachGenerator } from '@libraz/midi-sketch-bach';

await init();
const bach = new BachGenerator();
bach.generate({ form: 'chaconne', key: 0, isMinor: true, seed: 42 });

const midi = bach.getMidi();     // Uint8Array
const events = bach.getEvents(); // parsed JSON with note data
bach.destroy();
```

### Web Demo

```bash
make demo   # http://localhost:8080/demo/
```

## CLI Options

| Option | Description | Default |
|--------|-------------|---------|
| `--form FORM` | Composition form | `prelude_and_fugue` |
| `--key KEY` | Key (e.g. `g_minor`, `D_major`) | `C_major` |
| `--voices N` | Number of voices (2-5, organ only) | `3` |
| `--character CH` | `severe`, `playful`, `noble`, `restless` | auto |
| `--instrument INST` | `organ`, `harpsichord`, `piano`, `violin`, `cello`, `guitar` | auto |
| `--scale SCALE` | `short`, `medium`, `long`, `full` | `short` |
| `--bars N` | Target bar count (overrides `--scale`) | - |
| `--bpm N` | Tempo (40-200) | `72` |
| `--seed N` | Random seed (0 = random) | `0` |
| `--json` | Output JSON event data | - |
| `--analyze` | Include analysis metadata | - |
| `-o FILE` | Output path | `output.mid` |

## Build

```bash
make build          # C++ CLI
make test           # Run tests (1100+ C++ / 18 JS)
make quality-gate   # Format + build + test
make wasm           # WASM + JS bindings
```

Requires: C++17 compiler, CMake 3.15+. WASM build requires Emscripten.

## License

[Apache-2.0](LICENSE) / [Commercial](LICENSE-COMMERCIAL) dual license. For commercial inquiries: libraz@libraz.net
