# midi-sketch-bach

[![CI](https://img.shields.io/github/actions/workflow/status/libraz/midi-sketch-bach/ci.yml?branch=main&label=CI)](https://github.com/libraz/midi-sketch-bach/actions)
[![codecov](https://codecov.io/gh/libraz/midi-sketch-bach/branch/main/graph/badge.svg)](https://codecov.io/gh/libraz/midi-sketch-bach)
[![Live Demo](https://img.shields.io/badge/demo-bach.midi--sketch.libraz.net-6b21a8)](https://bach.midi-sketch.libraz.net/)
[![License](https://img.shields.io/badge/license-AGPL--3.0%20%2F%20Commercial-green)](LICENSE)

> **Alpha** — This project is under active development. Features and APIs may change. Pre-built binaries are coming soon.

**🎹 Try it now: [bach.midi-sketch.libraz.net](https://bach.midi-sketch.libraz.net/)** — generate and play Bach-style pieces right in your browser (WebAssembly). Full [documentation](https://bach.midi-sketch.libraz.net/docs/getting-started) and a [counterpoint course](https://bach.midi-sketch.libraz.net/docs/counterpoint/intervals) live on the same site.

A MIDI generator dedicated to the instrumental works of Johann Sebastian Bach.

Bach's instrumental music — organ fugues, solo string suites, chamber sonatas — is built on an extraordinary architecture of counterpoint, harmonic logic, and formal structure. This project focuses exclusively on these instrumental works, and attempts to reproduce their structural principles — strict voice-leading, the harmonic flow of the cello suites, the polyphonic texture of organ fugues — as playable MIDI, as closely as possible.

There are still many known issues across counterpoint rules, voice-leading, and musical structure. Contributions and feedback are welcome.

Built on the development insights from [midi-sketch](https://github.com/libraz/midi-sketch), a pop/contemporary music generator. Available as a **CLI tool**, **JavaScript/WASM library**, and **interactive web demo**.

## What It Generates

**Organ Works** — counterpoint-driven, multi-voice:

| Form | After the manner of | Voices |
|------|---------------------|--------|
| Prelude and Fugue | BWV 532, 548 | 3 |
| Fugue | Strict 3-voice fugue | 3 |
| Trio Sonata | BWV 525-530 | 3 |
| Chorale Prelude | BWV 599-650 (Orgelbüchlein) | 3 |
| Toccata and Fugue | BWV 565 | 3 |
| Passacaglia | BWV 582 | 3 |
| Fantasia and Fugue | BWV 537, 542 | 3 |

**String Works** — harmony-driven writing for a solo-string timbre; the
Chaconne uses a polyphonic realization around its immutable bass:

| Form | After the manner of | Instrument |
|------|---------------------|------------|
| Cello Prelude | BWV 1007 (Suite No.1) | Cello |
| Chaconne | BWV 1004 (Partita No.2) | Violin |

**Keyboard Works** — variation form:

| Form | After the manner of | Voices |
|------|---------------------|--------|
| Goldberg Variations | BWV 988 | 3 |

`Goldberg Variations` is a compressed structural model, not a reconstruction of
the BWV 988 score. Its dedicated 32-tone aria-bass phrase is mapped to four-bar
slots; `--scale full` produces 128 bars: aria, 30 variations (nine canons and a
two-tune Quodlibet slot), and an aria restatement with an explicit terminal
coda.

Every fugue builds an exposition, real/tonal answer, countersubject, episodes,
and stretto. The final-score validator audits parallel fifths/octaves and voice
crossing on the emitted notes and reports any authored-material findings.

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
bach.generate({ form: 'fugue', key: 0, isMinor: true, seed: 42 });

const midi = bach.getMidi();     // Uint8Array
const events = bach.getEvents(); // parsed JSON with note data
bach.destroy();
```

### Web Demo

Hosted: **[bach.midi-sketch.libraz.net](https://bach.midi-sketch.libraz.net/)** — or run locally:

```bash
make demo   # http://localhost:8080/demo/
```

## CLI Options

| Option | Description | Default |
|--------|-------------|---------|
| `--form FORM` | Composition form | `fugue` |
| `--key KEY` | Key (e.g. `g_minor`, `D_major`) | `C_major` |
| `--character CH` | `severe`, `playful`, `noble`, `restless` | `severe` |
| `--instrument INST` | `organ`, `harpsichord`, `piano`, `violin`, `cello`, `guitar` | auto |
| `--scale SCALE` | `short`, `medium`, `long`, `full` | `short` |
| `--bars N` | Target bar count (overrides `--scale`) | - |
| `--bpm N` | Tempo (40-200) | `100` |
| `--seed N` | Random seed (0 = random) | `0` |
| `--json` | Output JSON event data | - |
| `--generated-json` | Output generated.v1 + provenance.v1 JSON | - |
| `--free-counterpoint` | Experimental: generate the Passacaglia secondary counterline via scored search (off by default); other forms report an explicit unavailable diagnostic | - |
| `-o FILE` | Output path | `output.mid` |

## Build

```bash
make build          # C++ CLI
make test           # Run C++ tests (980+ via ctest)
yarn test           # Run JS/WASM tests (210+ via vitest)
make quality-gate   # Format + build + test
make wasm           # WASM + JS bindings
```

Requires: C++17 compiler, CMake 3.15+. WASM build requires Emscripten.

## License

[AGPL-3.0](LICENSE) / [Commercial](LICENSE-COMMERCIAL) dual license. Free to use, modify, and redistribute under AGPL-3.0; embedding in closed-source products or proprietary SaaS requires a commercial license. For commercial inquiries: libraz@libraz.net
